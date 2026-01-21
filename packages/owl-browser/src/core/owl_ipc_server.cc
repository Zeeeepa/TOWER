/**
 * Owl Browser IPC Server Implementation
 *
 * Linux/macOS: Multi-threaded Unix Domain Socket server
 * Windows: Falls back to stdin/stdout (single IPC)
 */

#include "owl_ipc_server.h"
#include "logger.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <cstdarg>

#if defined(OS_LINUX) || defined(OS_MACOS)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#endif

namespace owl {

#if defined(OS_LINUX) || defined(OS_MACOS)
// Helper for string formatting
static std::string Format(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return std::string(buf);
}
#endif

// ============================================================================
// IPCServer Implementation
// ============================================================================

IPCServer::IPCServer() {
}

IPCServer::~IPCServer() {
    Stop();
}

bool IPCServer::IsMultiIPCSupported() {
#if defined(OS_LINUX) || defined(OS_MACOS)
    return true;
#else
    return false;
#endif
}

bool IPCServer::Initialize(const std::string& instance_id, CommandHandler handler) {
    instance_id_ = instance_id;
    command_handler_ = handler;

#if defined(OS_LINUX) || defined(OS_MACOS)
    // Create socket path
    socket_path_ = "/tmp/owl_browser_" + instance_id + ".sock";

    // Remove existing socket file if present
    unlink(socket_path_.c_str());

    // Create Unix Domain Socket
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        LOG_ERROR("IPCServer", Format("Failed to create socket: %s", strerror(errno)));
        return false;
    }

    // Set socket options
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind to socket path
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("IPCServer", Format("Failed to bind socket: %s", strerror(errno)));
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    // Listen for connections (backlog of 128 for high concurrency with 64 socket pool)
    if (listen(server_fd_, 128) < 0) {
        LOG_ERROR("IPCServer", Format("Failed to listen on socket: %s", strerror(errno)));
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    LOG_DEBUG("IPCServer", "Multi-IPC server initialized at " + socket_path_);
    return true;
#else
    // macOS: No-op, we use stdin/stdout
    LOG_DEBUG("IPCServer", "Single-IPC mode (macOS)");
    return true;
#endif
}

void IPCServer::Start() {
    running_ = true;

#if defined(OS_LINUX) || defined(OS_MACOS)
    // Start accept loop in background thread
    accept_thread_ = std::thread(&IPCServer::AcceptLoop, this);
    LOG_DEBUG("IPCServer", "Accept loop started");
#endif
}

void IPCServer::Stop() {
    running_ = false;

#if defined(OS_LINUX) || defined(OS_MACOS)
    // Close server socket to unblock accept()
    if (server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }

    // Wait for accept thread
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    // Wait for all worker threads
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& t : worker_threads_) {
            if (t.joinable()) {
                t.join();
            }
        }
        worker_threads_.clear();
    }

    // Remove socket file
    if (!socket_path_.empty()) {
        unlink(socket_path_.c_str());
    }

    LOG_DEBUG("IPCServer", "Server stopped");
#endif
}

size_t IPCServer::GetActiveConnections() const {
#if defined(OS_LINUX) || defined(OS_MACOS)
    return active_connections_.load();
#else
    return 1;  // Single stdin/stdout connection
#endif
}

size_t IPCServer::GetTotalCommandsProcessed() const {
#if defined(OS_LINUX) || defined(OS_MACOS)
    return total_commands_.load();
#else
    return 0;
#endif
}

#if defined(OS_LINUX) || defined(OS_MACOS)
void IPCServer::AcceptLoop() {
    LOG_DEBUG("IPCServer", "Accept loop running on socket " + socket_path_);

    while (running_) {
        struct pollfd pfd;
        pfd.fd = server_fd_;
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, 10);  // 10ms timeout for faster connection handling

        if (ret < 0) {
            if (errno == EINTR) continue;
            if (!running_) break;
            LOG_ERROR("IPCServer", Format("Poll error: %s", strerror(errno)));
            break;
        }

        if (ret == 0) continue;  // Timeout

        if (pfd.revents & POLLIN) {
            struct sockaddr_un client_addr;
            socklen_t client_len = sizeof(client_addr);

            int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;
                if (!running_) break;
                LOG_ERROR("IPCServer", Format("Accept error: %s", strerror(errno)));
                continue;
            }

            // Set client socket to non-blocking
            int flags = fcntl(client_fd, F_GETFL, 0);
            fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

            // Spawn worker thread for this connection
            int worker_id = next_worker_id_.fetch_add(1);
            active_connections_.fetch_add(1);

            LOG_DEBUG("IPCServer", Format("New connection accepted, worker #%d (active: %zu)",
                     worker_id, active_connections_.load()));

            // Store thread and start it
            std::lock_guard<std::mutex> lock(mutex_);
            worker_threads_.emplace_back(&IPCServer::WorkerThread, this, client_fd, worker_id);
        }
    }

    LOG_DEBUG("IPCServer", "Accept loop exiting");
}

void IPCServer::WorkerThread(int client_fd, int worker_id) {
    LOG_DEBUG("IPCServer", Format("Worker #%d started", worker_id));

    std::string buffer;
    buffer.reserve(64 * 1024);  // 64KB initial buffer

    char read_buf[4096];

    while (running_) {
        struct pollfd pfd;
        pfd.fd = client_fd;
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, 100);  // 100ms timeout

        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (ret == 0) continue;  // Timeout

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            LOG_DEBUG("IPCServer", Format("Worker #%d: client disconnected", worker_id));
            break;
        }

        if (pfd.revents & POLLIN) {
            ssize_t n = read(client_fd, read_buf, sizeof(read_buf) - 1);
            if (n <= 0) {
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
                LOG_DEBUG("IPCServer", Format("Worker #%d: read returned %zd", worker_id, n));
                break;
            }

            read_buf[n] = '\0';
            buffer += read_buf;

            // Process complete lines (JSON commands end with newline)
            size_t pos;
            while ((pos = buffer.find('\n')) != std::string::npos) {
                std::string command = buffer.substr(0, pos);
                buffer.erase(0, pos + 1);

                if (!command.empty()) {
                    if (!ProcessAndRespond(client_fd, command)) {
                        LOG_ERROR("IPCServer", Format("Worker #%d: failed to process command", worker_id));
                    }
                }
            }
        }
    }

    close(client_fd);
    active_connections_.fetch_sub(1);
    LOG_DEBUG("IPCServer", Format("Worker #%d exiting (active: %zu)", worker_id, active_connections_.load()));
}

bool IPCServer::ProcessAndRespond(int client_fd, const std::string& command) {
    if (!command_handler_) {
        LOG_ERROR("IPCServer", "No command handler set");
        return false;
    }

    total_commands_.fetch_add(1);

    // Process command (this may take time for complex operations)
    std::string response = command_handler_(command);

    // Ensure response ends with newline
    if (response.empty() || response.back() != '\n') {
        response += '\n';
    }

    // Send response
    size_t total_sent = 0;
    while (total_sent < response.size()) {
        ssize_t n = write(client_fd, response.c_str() + total_sent, response.size() - total_sent);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Wait for socket to be writable
                struct pollfd pfd = {client_fd, POLLOUT, 0};
                poll(&pfd, 1, 1000);
                continue;
            }
            LOG_ERROR("IPCServer", Format("Write error: %s", strerror(errno)));
            return false;
        }
        total_sent += n;
    }

    return true;
}
#endif

// ============================================================================
// IPCClient Implementation
// ============================================================================

IPCClient::IPCClient() {
}

IPCClient::~IPCClient() {
    Disconnect();
}

bool IPCClient::Connect(const std::string& instance_id) {
#if defined(OS_LINUX) || defined(OS_MACOS)
    socket_path_ = "/tmp/owl_browser_" + instance_id + ".sock";

    socket_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        LOG_ERROR("IPCClient", Format("Failed to create socket: %s", strerror(errno)));
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("IPCClient", Format("Failed to connect to %s: %s", socket_path_.c_str(), strerror(errno)));
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    connected_ = true;
    LOG_DEBUG("IPCClient", "Connected to " + socket_path_);
    return true;
#else
    // macOS: Not supported
    return false;
#endif
}

void IPCClient::Disconnect() {
#if defined(OS_LINUX) || defined(OS_MACOS)
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
#endif
    connected_ = false;
}

bool IPCClient::IsConnected() const {
    return connected_.load();
}

std::string IPCClient::SendCommand(const std::string& command, int timeout_ms) {
#if defined(OS_LINUX) || defined(OS_MACOS)
    if (!connected_ || socket_fd_ < 0) {
        return "";
    }

    std::lock_guard<std::mutex> lock(send_mutex_);

    // Send command with newline
    std::string cmd = command;
    if (cmd.empty() || cmd.back() != '\n') {
        cmd += '\n';
    }

    size_t total_sent = 0;
    while (total_sent < cmd.size()) {
        ssize_t n = write(socket_fd_, cmd.c_str() + total_sent, cmd.size() - total_sent);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd = {socket_fd_, POLLOUT, 0};
                poll(&pfd, 1, 100);
                continue;
            }
            LOG_ERROR("IPCClient", Format("Send error: %s", strerror(errno)));
            return "";
        }
        total_sent += n;
    }

    // Read response
    std::string response;
    response.reserve(64 * 1024);
    char buf[4096];

    auto start = std::chrono::steady_clock::now();

    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeout_ms) {
            LOG_ERROR("IPCClient", Format("Response timeout after %dms", timeout_ms));
            return "";
        }

        struct pollfd pfd;
        pfd.fd = socket_fd_;
        pfd.events = POLLIN;

        int remaining = timeout_ms - static_cast<int>(elapsed);
        int poll_timeout = std::min(100, remaining);
        int ret = poll(&pfd, 1, poll_timeout);

        if (ret < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("IPCClient", Format("Poll error: %s", strerror(errno)));
            return "";
        }

        if (ret == 0) continue;  // Timeout, try again

        if (pfd.revents & POLLIN) {
            ssize_t n = read(socket_fd_, buf, sizeof(buf) - 1);
            if (n <= 0) {
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
                LOG_ERROR("IPCClient", Format("Read error: %zd", n));
                return "";
            }
            buf[n] = '\0';
            response += buf;

            // Check for complete response (ends with newline)
            if (!response.empty() && response.back() == '\n') {
                response.pop_back();  // Remove trailing newline
                return response;
            }
        }
    }
#else
    return "";  // Not supported on macOS
#endif
}

// ============================================================================
// IPCConnectionPool Implementation
// ============================================================================

IPCConnectionPool::IPCConnectionPool(size_t pool_size)
    : pool_size_(pool_size) {
}

IPCConnectionPool::~IPCConnectionPool() {
    for (auto& client : clients_) {
        if (client) {
            client->Disconnect();
        }
    }
}

bool IPCConnectionPool::Initialize(const std::string& instance_id) {
    instance_id_ = instance_id;

#if defined(OS_LINUX) || defined(OS_MACOS)
    clients_.resize(pool_size_);
    client_in_use_.resize(pool_size_, false);

    size_t connected = 0;
    for (size_t i = 0; i < pool_size_; i++) {
        clients_[i] = std::make_unique<IPCClient>();
        if (clients_[i]->Connect(instance_id)) {
            connected++;
        }
    }

    LOG_DEBUG("IPCConnectionPool", Format("Initialized %zu/%zu connections", connected, pool_size_));
    return connected > 0;
#else
    return false;
#endif
}

std::string IPCConnectionPool::SendCommand(const std::string& command, int timeout_ms) {
    std::unique_lock<std::mutex> lock(pool_mutex_);

    // Wait for available connection
    pool_cv_.wait(lock, [this]() {
        for (size_t i = 0; i < client_in_use_.size(); i++) {
            if (!client_in_use_[i] && clients_[i] && clients_[i]->IsConnected()) {
                return true;
            }
        }
        return false;
    });

    // Find available connection
    size_t idx = SIZE_MAX;
    for (size_t i = 0; i < client_in_use_.size(); i++) {
        if (!client_in_use_[i] && clients_[i] && clients_[i]->IsConnected()) {
            idx = i;
            client_in_use_[i] = true;
            break;
        }
    }

    lock.unlock();

    if (idx == SIZE_MAX) {
        return "";
    }

    // Send command
    std::string response = clients_[idx]->SendCommand(command, timeout_ms);

    // Return connection to pool
    lock.lock();
    client_in_use_[idx] = false;
    lock.unlock();
    pool_cv_.notify_one();

    return response;
}

IPCClient* IPCConnectionPool::GetConnectionForContext(const std::string& context_id) {
    std::lock_guard<std::mutex> lock(pool_mutex_);

    // Check for existing affinity
    auto it = context_affinity_.find(context_id);
    if (it != context_affinity_.end()) {
        size_t idx = it->second;
        if (idx < clients_.size() && !client_in_use_[idx]) {
            client_in_use_[idx] = true;
            return clients_[idx].get();
        }
    }

    // Find any available connection
    for (size_t i = 0; i < client_in_use_.size(); i++) {
        if (!client_in_use_[i] && clients_[i] && clients_[i]->IsConnected()) {
            client_in_use_[i] = true;
            context_affinity_[context_id] = i;
            return clients_[i].get();
        }
    }

    return nullptr;
}

void IPCConnectionPool::ReturnConnection(IPCClient* client) {
    std::lock_guard<std::mutex> lock(pool_mutex_);

    for (size_t i = 0; i < clients_.size(); i++) {
        if (clients_[i].get() == client) {
            client_in_use_[i] = false;
            break;
        }
    }

    pool_cv_.notify_one();
}

size_t IPCConnectionPool::GetAvailableConnections() const {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    size_t count = 0;
    for (size_t i = 0; i < client_in_use_.size(); i++) {
        if (!client_in_use_[i] && clients_[i] && clients_[i]->IsConnected()) {
            count++;
        }
    }
    return count;
}

} // namespace owl
