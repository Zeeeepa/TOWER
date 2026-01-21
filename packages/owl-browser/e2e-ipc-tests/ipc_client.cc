#include "ipc_client.h"
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>
#include <cstring>
#include <sstream>

// Forward declaration
static bool LooksLikeJsonResponse(const std::string& line);

// ============================================================================
// IPCClient Implementation
// ============================================================================

IPCClient::IPCClient(const std::string& browser_path)
    : browser_path_(browser_path) {}

IPCClient::~IPCClient() {
    Stop();
}

bool IPCClient::Start(const std::string& instance_id, ConnectionMode mode) {
    instance_id_ = instance_id.empty() ?
        "ipc_test_" + std::to_string(time(nullptr)) : instance_id;

    // Create pipes
    int stdin_pipe[2];
    int stdout_pipe[2];

    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
        std::cerr << "[ERROR] Failed to create pipes" << std::endl;
        return false;
    }

    child_pid_ = fork();

    if (child_pid_ < 0) {
        std::cerr << "[ERROR] Fork failed" << std::endl;
        return false;
    }

    if (child_pid_ == 0) {
        // Child process
        close(stdin_pipe[1]);  // Close write end of stdin pipe
        close(stdout_pipe[0]); // Close read end of stdout pipe

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);

        // Redirect stderr to /dev/null - we only want JSON responses on stdout
        // This prevents log spam from interfering with IPC communication
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);

        // Build arguments
        std::vector<const char*> args;
        args.push_back(browser_path_.c_str());

        std::string instance_arg = "--instance-id=" + instance_id_;
        args.push_back(instance_arg.c_str());
        args.push_back(nullptr);

        execv(browser_path_.c_str(), const_cast<char* const*>(args.data()));

        // If execv returns, it failed
        std::cerr << "[ERROR] Failed to execute browser: " << strerror(errno) << std::endl;
        _exit(1);
    }

    // Parent process
    close(stdin_pipe[0]);  // Close read end of stdin pipe
    close(stdout_pipe[1]); // Close write end of stdout pipe

    stdin_fd_ = stdin_pipe[1];
    stdout_fd_ = stdout_pipe[0];

    // Set non-blocking mode for reading
    int flags = fcntl(stdout_fd_, F_GETFL, 0);
    fcntl(stdout_fd_, F_SETFL, flags | O_NONBLOCK);

    // Wait for READY signal and detect socket path
    if (!WaitForReady()) {
        std::cerr << "[ERROR] Browser did not become ready" << std::endl;
        Stop();
        return false;
    }

    // Try socket mode if requested or auto-detected
    if (mode == ConnectionMode::AUTO || mode == ConnectionMode::SOCKET) {
        if (!socket_path_.empty() && ConnectSocket()) {
            active_mode_ = ConnectionMode::SOCKET;
            if (verbose_) {
                std::cerr << "[IPC] Connected via socket: " << socket_path_ << std::endl;
            }
            // Keep stdin_fd_ open to prevent browser from exiting
            // The browser's stdin reader thread might exit if stdin closes
            // Only close stdout_fd_ since we're reading from socket now
            close(stdout_fd_);
            stdout_fd_ = -1;
            // Small delay to ensure socket connection is fully established
            usleep(100000);  // 100ms
            return true;
        } else if (mode == ConnectionMode::SOCKET) {
            std::cerr << "[ERROR] Socket mode requested but not available" << std::endl;
            Stop();
            return false;
        }
        // Fall through to pipe mode for AUTO
    }

    // Use pipe mode
    active_mode_ = ConnectionMode::PIPE;
    if (verbose_) {
        std::cerr << "[IPC] Using pipe mode (stdin/stdout)" << std::endl;
    }

    return true;
}

void IPCClient::Stop() {
    // Close socket if in socket mode
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }

    if (stdin_fd_ >= 0) {
        close(stdin_fd_);
        stdin_fd_ = -1;
    }

    if (stdout_fd_ >= 0) {
        close(stdout_fd_);
        stdout_fd_ = -1;
    }

    if (child_pid_ > 0) {
        // Send shutdown command first (graceful)
        // Then SIGTERM
        kill(child_pid_, SIGTERM);

        // Wait with timeout
        int status;
        int wait_result = 0;
        for (int i = 0; i < 50; i++) {  // 5 seconds total
            wait_result = waitpid(child_pid_, &status, WNOHANG);
            if (wait_result != 0) break;
            usleep(100000);  // 100ms
        }

        if (wait_result == 0) {
            // Force kill
            kill(child_pid_, SIGKILL);
            waitpid(child_pid_, &status, 0);
        }

        child_pid_ = -1;
    }
}

bool IPCClient::IsRunning() const {
    if (child_pid_ <= 0) return false;

    int status;
    int result = waitpid(child_pid_, &status, WNOHANG);
    return result == 0;  // Still running
}

bool IPCClient::WaitForReady(int timeout_ms) {
    // Browser initialization takes about 15 seconds, use at least 30s timeout
    if (timeout_ms < 30000) timeout_ms = 30000;

    auto start = std::chrono::steady_clock::now();

    if (verbose_) {
        std::cerr << "[IPC] WaitForReady: Starting with timeout " << timeout_ms << "ms" << std::endl;
    }

    // Wait for READY signal from browser, also look for MULTI_IPC_READY
    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();

        if (elapsed > timeout_ms) {
            if (verbose_) {
                std::cerr << "[IPC] WaitForReady: Timeout after " << elapsed << "ms" << std::endl;
            }
            return false;
        }

        // Check if process died
        if (!IsRunning()) {
            if (verbose_) {
                std::cerr << "[IPC] WaitForReady: Process died" << std::endl;
            }
            return false;
        }

        // Try to read a line (with short timeout)
        std::string line = ReadLine(1000);
        if (verbose_ && !line.empty()) {
            std::cerr << "[IPC] WaitForReady read: " << line.substr(0, 100) << (line.length() > 100 ? "..." : "") << std::endl;
        }

        // Check for MULTI_IPC_READY - extract socket path
        if (line.find("MULTI_IPC_READY") != std::string::npos) {
            // Parse socket path from "MULTI_IPC_READY /tmp/owl_browser_xxx.sock"
            size_t space_pos = line.find(' ');
            if (space_pos != std::string::npos) {
                socket_path_ = line.substr(space_pos + 1);
                // Trim whitespace
                while (!socket_path_.empty() &&
                       (socket_path_.back() == ' ' || socket_path_.back() == '\n' || socket_path_.back() == '\r')) {
                    socket_path_.pop_back();
                }
                if (verbose_) {
                    std::cerr << "[IPC] Detected socket path: " << socket_path_ << std::endl;
                }
            }
            continue;  // Keep waiting for READY
        }

        // Look for READY signal
        if (line.find("READY") != std::string::npos) {
            if (verbose_) {
                std::cerr << "[IPC] WaitForReady: Got READY signal" << std::endl;
            }
            // Give browser a moment to be fully ready for commands
            usleep(100000);  // 100ms
            return true;
        }
    }
}

bool IPCClient::ConnectSocket() {
    if (socket_path_.empty()) {
        return false;
    }

    socket_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        if (verbose_) {
            std::cerr << "[IPC] Failed to create socket: " << strerror(errno) << std::endl;
        }
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        if (verbose_) {
            std::cerr << "[IPC] Failed to connect to socket " << socket_path_
                      << ": " << strerror(errno) << std::endl;
        }
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    // Set non-blocking mode
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);

    return true;
}

std::string IPCClient::ReadLine(int timeout_ms) {
    char buffer[4096];

    auto start = std::chrono::steady_clock::now();

    while (true) {
        // First check if we have a complete line in the buffer
        size_t newline_pos = read_buffer_.find('\n');
        if (newline_pos != std::string::npos) {
            std::string line = read_buffer_.substr(0, newline_pos);
            read_buffer_.erase(0, newline_pos + 1);
            // Remove carriage return if present
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return line;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();

        if (elapsed > timeout_ms) {
            break;
        }

        struct pollfd pfd;
        pfd.fd = stdout_fd_;
        pfd.events = POLLIN;

        int poll_result = poll(&pfd, 1, std::min(100, timeout_ms - static_cast<int>(elapsed)));

        if (poll_result > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(stdout_fd_, buffer, sizeof(buffer) - 1);
            if (n > 0) {
                buffer[n] = '\0';
                read_buffer_ += buffer;
            } else if (n == 0) {
                // EOF - process closed
                if (verbose_) {
                    std::cerr << "[IPC] ReadLine: EOF received, buffer='" << read_buffer_.substr(0, 50) << "'" << std::endl;
                }
                break;
            }
        } else if (poll_result < 0 && errno != EINTR) {
            break;
        }
    }

    // Return whatever is in the buffer if we timeout
    std::string result = read_buffer_;
    read_buffer_.clear();
    return result;
}

std::string IPCClient::ReadLineSocket(int timeout_ms) {
    char buffer[4096];

    auto start = std::chrono::steady_clock::now();

    while (true) {
        // First check if we have a complete line in the buffer
        size_t newline_pos = read_buffer_.find('\n');
        if (newline_pos != std::string::npos) {
            std::string line = read_buffer_.substr(0, newline_pos);
            read_buffer_.erase(0, newline_pos + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return line;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();

        if (elapsed > timeout_ms) {
            break;
        }

        struct pollfd pfd;
        pfd.fd = socket_fd_;
        pfd.events = POLLIN;

        int poll_result = poll(&pfd, 1, std::min(100, timeout_ms - static_cast<int>(elapsed)));

        if (poll_result > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = recv(socket_fd_, buffer, sizeof(buffer) - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                read_buffer_ += buffer;
            } else if (n == 0) {
                // Socket closed
                if (verbose_) {
                    std::cerr << "[IPC] ReadLineSocket: Connection closed" << std::endl;
                }
                break;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                if (verbose_) {
                    std::cerr << "[IPC] ReadLineSocket: Error " << strerror(errno) << std::endl;
                }
                break;
            }
        } else if (poll_result < 0 && errno != EINTR) {
            break;
        }
    }

    std::string result = read_buffer_;
    read_buffer_.clear();
    return result;
}

bool IPCClient::WriteLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(io_mutex_);

    std::string data = line + "\n";
    ssize_t written = write(stdin_fd_, data.c_str(), data.length());
    return written == static_cast<ssize_t>(data.length());
}

bool IPCClient::WriteLineSocket(const std::string& line) {
    std::lock_guard<std::mutex> lock(io_mutex_);

    std::string data = line + "\n";
    ssize_t written = send(socket_fd_, data.c_str(), data.length(), 0);
    return written == static_cast<ssize_t>(data.length());
}

// Helper to check if a line looks like a JSON response (starts with {"id":)
static bool LooksLikeJsonResponse(const std::string& line) {
    if (line.empty()) return false;
    // JSON responses start with {"id": or {\"id\"
    if (line[0] == '{') {
        // Check for {"id or { "id
        size_t pos = 1;
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
        if (pos + 4 < line.size() && line.substr(pos, 4) == "\"id\"") {
            return true;
        }
    }
    return false;
}

json IPCClient::Send(const std::string& method, const json& params) {
    int id = command_id_++;

    // Build command
    json command = {{"id", id}, {"method", method}};

    // Merge params
    for (auto& [key, value] : params.items()) {
        command[key] = value;
    }

    std::string json_str = command.dump();
    last_request_size_ = json_str.size();

    // Time the request
    auto start = std::chrono::high_resolution_clock::now();

    if (verbose_) {
        std::cerr << "[IPC] Sending ID " << id << ": " << method
                  << " (mode: " << (active_mode_ == ConnectionMode::SOCKET ? "socket" : "pipe") << ")" << std::endl;
    }

    // Write command
    bool write_ok = false;
    if (active_mode_ == ConnectionMode::SOCKET) {
        write_ok = WriteLineSocket(json_str);
    } else {
        write_ok = WriteLine(json_str);
    }

    if (!write_ok) {
        return {{"error", "Failed to write command"}};
    }

    // Read response - skip log lines until we get JSON response with matching ID
    std::string response_str;
    int max_attempts = 1000;  // Allow many log lines
    for (int attempt = 0; attempt < max_attempts; attempt++) {
        if (active_mode_ == ConnectionMode::SOCKET) {
            response_str = ReadLineSocket(30000);
        } else {
            response_str = ReadLine(30000);
        }

        if (response_str.empty()) {
            if (verbose_) {
                std::cerr << "[IPC] ID " << id << " (" << method << "): Empty response (timeout) after "
                          << attempt << " log lines, buffer_size=" << read_buffer_.size()
                          << ", running=" << IsRunning() << std::endl;
            }
            return {{"error", "No response received (timeout)"}};
        }

        // Skip log lines - only process lines that look like JSON responses
        if (!LooksLikeJsonResponse(response_str)) {
            if (verbose_ && attempt < 5) {
                std::cerr << "[IPC] ID " << id << " skipping log: " << response_str.substr(0, 80)
                          << (response_str.length() > 80 ? "..." : "") << std::endl;
            }
            continue;
        }

        if (verbose_) {
            std::cerr << "[IPC] ID " << id << " got JSON: " << response_str.substr(0, 100)
                      << (response_str.length() > 100 ? "..." : "") << std::endl;
        }

        // Try to parse as JSON and check ID
        try {
            json response = json::parse(response_str);
            if (response.contains("id")) {
                int response_id = response["id"].get<int>();
                if (response_id == id) {
                    // Found our response!
                    auto end = std::chrono::high_resolution_clock::now();
                    last_response_time_ms_ = std::chrono::duration<double, std::milli>(end - start).count();
                    last_response_size_ = response_str.size();
                    last_parse_time_ms_ = 0;  // Already parsed
                    return response;
                }
                // Wrong ID - might be stale response, skip it
                continue;
            }
        } catch (const json::parse_error&) {
            // Not valid JSON, skip it
            continue;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    last_response_time_ms_ = std::chrono::duration<double, std::milli>(end - start).count();

    return {{"error", "No response received (max attempts reached)"}};
}

std::string IPCClient::SendRaw(const std::string& json_command) {
    auto start = std::chrono::high_resolution_clock::now();

    bool write_ok = false;
    if (active_mode_ == ConnectionMode::SOCKET) {
        write_ok = WriteLineSocket(json_command);
    } else {
        write_ok = WriteLine(json_command);
    }

    if (!write_ok) {
        return "{\"error\": \"Failed to write command\"}";
    }

    std::string response;
    if (active_mode_ == ConnectionMode::SOCKET) {
        response = ReadLineSocket(120000);
    } else {
        response = ReadLine(120000);
    }

    auto end = std::chrono::high_resolution_clock::now();
    last_response_time_ms_ = std::chrono::duration<double, std::milli>(end - start).count();

    return response;
}


// ============================================================================
// SocketClient Implementation
// ============================================================================

SocketClient::SocketClient() {}

SocketClient::~SocketClient() {
    Disconnect();
}

bool SocketClient::Connect(const std::string& socket_path) {
    socket_path_ = socket_path;

    socket_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        if (verbose_) {
            std::cerr << "[SocketClient] Failed to create socket: " << strerror(errno) << std::endl;
        }
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        if (verbose_) {
            std::cerr << "[SocketClient] Failed to connect to " << socket_path
                      << ": " << strerror(errno) << std::endl;
        }
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    // Set non-blocking mode
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);

    connected_ = true;
    if (verbose_) {
        std::cerr << "[SocketClient] Connected to " << socket_path << std::endl;
    }
    return true;
}

void SocketClient::Disconnect() {
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    connected_ = false;
}

std::string SocketClient::ReadLine(int timeout_ms) {
    char buffer[4096];

    auto start = std::chrono::steady_clock::now();

    while (true) {
        size_t newline_pos = read_buffer_.find('\n');
        if (newline_pos != std::string::npos) {
            std::string line = read_buffer_.substr(0, newline_pos);
            read_buffer_.erase(0, newline_pos + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return line;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();

        if (elapsed > timeout_ms) {
            break;
        }

        struct pollfd pfd;
        pfd.fd = socket_fd_;
        pfd.events = POLLIN;

        int poll_result = poll(&pfd, 1, std::min(100, timeout_ms - static_cast<int>(elapsed)));

        if (poll_result > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = recv(socket_fd_, buffer, sizeof(buffer) - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                read_buffer_ += buffer;
            } else if (n == 0) {
                connected_ = false;
                break;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                connected_ = false;
                break;
            }
        } else if (poll_result < 0 && errno != EINTR) {
            break;
        }
    }

    std::string result = read_buffer_;
    read_buffer_.clear();
    return result;
}

bool SocketClient::WriteLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(io_mutex_);

    std::string data = line + "\n";
    ssize_t written = send(socket_fd_, data.c_str(), data.length(), 0);
    return written == static_cast<ssize_t>(data.length());
}

json SocketClient::Send(const std::string& method, const json& params) {
    int id = command_id_++;

    json command = {{"id", id}, {"method", method}};
    for (auto& [key, value] : params.items()) {
        command[key] = value;
    }

    std::string json_str = command.dump();

    auto start = std::chrono::high_resolution_clock::now();

    if (verbose_) {
        std::cerr << "[SocketClient] Sending ID " << id << ": " << method << std::endl;
    }

    if (!WriteLine(json_str)) {
        return {{"error", "Failed to write command"}};
    }

    // Read response
    std::string response_str;
    int max_attempts = 1000;
    for (int attempt = 0; attempt < max_attempts; attempt++) {
        response_str = ReadLine(30000);

        if (response_str.empty()) {
            return {{"error", "No response received (timeout)"}};
        }

        if (!LooksLikeJsonResponse(response_str)) {
            continue;
        }

        try {
            json response = json::parse(response_str);
            if (response.contains("id")) {
                int response_id = response["id"].get<int>();
                if (response_id == id) {
                    auto end = std::chrono::high_resolution_clock::now();
                    last_response_time_ms_ = std::chrono::duration<double, std::milli>(end - start).count();
                    return response;
                }
            }
        } catch (const json::parse_error&) {
            continue;
        }
    }

    return {{"error", "No response received (max attempts reached)"}};
}

std::string SocketClient::SendRaw(const std::string& json_command, int timeout_ms) {
    auto start = std::chrono::high_resolution_clock::now();

    if (!WriteLine(json_command)) {
        return "{\"error\": \"Failed to write command\"}";
    }

    std::string response = ReadLine(timeout_ms);

    auto end = std::chrono::high_resolution_clock::now();
    last_response_time_ms_ = std::chrono::duration<double, std::milli>(end - start).count();

    return response;
}


// ============================================================================
// IPCConnectionPool Implementation
// ============================================================================

IPCConnectionPool::IPCConnectionPool(size_t pool_size)
    : pool_size_(pool_size) {}

IPCConnectionPool::~IPCConnectionPool() {
    // Disconnect all clients
    for (auto& client : clients_) {
        if (client) {
            client->Disconnect();
        }
    }
}

bool IPCConnectionPool::Initialize(const std::string& socket_path) {
    socket_path_ = socket_path;

    std::lock_guard<std::mutex> lock(pool_mutex_);

    clients_.clear();
    client_in_use_.clear();

    size_t connected = 0;
    for (size_t i = 0; i < pool_size_; i++) {
        auto client = std::make_unique<SocketClient>();
        client->SetVerbose(verbose_);

        if (client->Connect(socket_path)) {
            clients_.push_back(std::move(client));
            client_in_use_.push_back(false);
            connected++;
        } else {
            std::cerr << "[Pool] Failed to connect client " << i << std::endl;
        }
    }

    if (connected == 0) {
        std::cerr << "[Pool] Failed to connect any clients to " << socket_path << std::endl;
        return false;
    }

    std::cerr << "[Pool] Initialized " << connected << "/" << pool_size_
              << " connections to " << socket_path << std::endl;

    return true;
}

SocketClient* IPCConnectionPool::AcquireConnection(int timeout_ms) {
    std::unique_lock<std::mutex> lock(pool_mutex_);

    auto start = std::chrono::steady_clock::now();

    while (true) {
        // Find available connection
        for (size_t i = 0; i < clients_.size(); i++) {
            if (!client_in_use_[i] && clients_[i]->IsConnected()) {
                client_in_use_[i] = true;
                active_count_++;
                return clients_[i].get();
            }
        }

        // Check timeout
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();

        if (elapsed >= timeout_ms) {
            return nullptr;  // Timeout
        }

        // Wait for a connection to become available
        pool_cv_.wait_for(lock, std::chrono::milliseconds(100));
    }
}

void IPCConnectionPool::ReleaseConnection(SocketClient* client) {
    std::lock_guard<std::mutex> lock(pool_mutex_);

    for (size_t i = 0; i < clients_.size(); i++) {
        if (clients_[i].get() == client) {
            client_in_use_[i] = false;
            active_count_--;
            pool_cv_.notify_one();
            return;
        }
    }
}

SocketClient* IPCConnectionPool::GetConnectionForContext(const std::string& context_id) {
    std::unique_lock<std::mutex> lock(pool_mutex_);

    // Check if we have affinity for this context
    auto it = context_affinity_.find(context_id);
    if (it != context_affinity_.end()) {
        size_t idx = it->second;
        if (idx < clients_.size() && clients_[idx]->IsConnected()) {
            // Wait for this specific connection if in use
            while (client_in_use_[idx]) {
                pool_cv_.wait_for(lock, std::chrono::milliseconds(100));
            }
            client_in_use_[idx] = true;
            active_count_++;
            return clients_[idx].get();
        }
    }

    // No affinity, find any available connection
    lock.unlock();
    SocketClient* client = AcquireConnection(5000);

    if (client) {
        lock.lock();
        // Record affinity
        for (size_t i = 0; i < clients_.size(); i++) {
            if (clients_[i].get() == client) {
                context_affinity_[context_id] = i;
                break;
            }
        }
    }

    return client;
}

size_t IPCConnectionPool::GetAvailableConnections() const {
    std::lock_guard<std::mutex> lock(pool_mutex_);

    size_t available = 0;
    for (size_t i = 0; i < clients_.size(); i++) {
        if (!client_in_use_[i] && clients_[i]->IsConnected()) {
            available++;
        }
    }
    return available;
}

void IPCConnectionPool::SetVerbose(bool verbose) {
    verbose_ = verbose;
    std::lock_guard<std::mutex> lock(pool_mutex_);
    for (auto& client : clients_) {
        if (client) {
            client->SetVerbose(verbose);
        }
    }
}
