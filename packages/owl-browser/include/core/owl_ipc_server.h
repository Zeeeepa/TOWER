#pragma once

/**
 * Owl Browser IPC Server - Multi-threaded Unix Domain Socket IPC
 *
 * This provides a multi-connection IPC server for Linux and macOS that allows
 * parallel command processing across multiple contexts.
 *
 * On Windows, we fall back to the single stdin/stdout pipe model.
 * On Linux/macOS, each client connection gets its own thread for processing.
 */

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <unordered_map>
#include <memory>

#if defined(OS_LINUX) || defined(OS_MACOS)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace owl {

// Command handler callback type
// Takes command JSON string, returns response JSON string
using CommandHandler = std::function<std::string(const std::string&)>;

/**
 * Multi-threaded IPC Server using Unix Domain Sockets (Linux/macOS)
 *
 * Architecture:
 * - Main thread accepts new connections
 * - Each connection gets a dedicated worker thread
 * - Commands are processed in parallel across connections
 * - Each connection can handle multiple sequential commands
 */
class IPCServer {
public:
    IPCServer();
    ~IPCServer();

    // Initialize the server
    // On Linux: Creates Unix Domain Socket at /tmp/owl_browser_{instance_id}.sock
    // On macOS: Falls back to stdin/stdout mode
    bool Initialize(const std::string& instance_id, CommandHandler handler);

    // Start accepting connections (blocking on Linux, returns immediately on macOS)
    void Start();

    // Stop the server and all worker threads
    void Stop();

    // Check if multi-IPC is supported on this platform
    static bool IsMultiIPCSupported();

    // Get the socket path (Linux only)
    std::string GetSocketPath() const { return socket_path_; }

    // Get statistics
    size_t GetActiveConnections() const;
    size_t GetTotalCommandsProcessed() const;

private:
#if defined(OS_LINUX) || defined(OS_MACOS)
    // Accept loop - runs in main server thread
    void AcceptLoop();

    // Worker thread - handles one client connection
    void WorkerThread(int client_fd, int worker_id);

    // Process a single command and send response
    bool ProcessAndRespond(int client_fd, const std::string& command);

    // Socket management
    int server_fd_ = -1;
    std::string socket_path_;

    // Worker management
    std::vector<std::thread> worker_threads_;
    std::atomic<int> next_worker_id_{0};
    std::atomic<size_t> active_connections_{0};
    std::atomic<size_t> total_commands_{0};

    // Thread for accept loop
    std::thread accept_thread_;
#endif

    // Common members
    std::atomic<bool> running_{false};
    CommandHandler command_handler_;
    std::string instance_id_;
    mutable std::mutex mutex_;
#if !defined(OS_LINUX) && !defined(OS_MACOS)
    std::string socket_path_;  // Only needed for Windows to satisfy GetSocketPath()
#endif
};

/**
 * IPC Client for connecting to the multi-IPC server (Linux/macOS)
 * Used by the HTTP server to send commands to the browser process
 */
class IPCClient {
public:
    IPCClient();
    ~IPCClient();

    // Connect to the browser's IPC server
    bool Connect(const std::string& instance_id);

    // Disconnect from the server
    void Disconnect();

    // Send a command and wait for response
    // Returns empty string on error
    std::string SendCommand(const std::string& command, int timeout_ms = 30000);

    // Check if connected
    bool IsConnected() const;

    // Check if multi-IPC is supported
    static bool IsMultiIPCSupported() { return IPCServer::IsMultiIPCSupported(); }

private:
#if defined(OS_LINUX) || defined(OS_MACOS)
    int socket_fd_ = -1;
    std::string socket_path_;
    std::mutex send_mutex_;  // Serialize sends on this connection
#endif
    std::atomic<bool> connected_{false};
};

/**
 * Connection pool for managing multiple IPC connections
 * Allows parallel command sending to the browser
 */
class IPCConnectionPool {
public:
    explicit IPCConnectionPool(size_t pool_size = 8);
    ~IPCConnectionPool();

    // Initialize connections to browser instance
    bool Initialize(const std::string& instance_id);

    // Send command using any available connection
    // This allows true parallel command processing
    std::string SendCommand(const std::string& command, int timeout_ms = 30000);

    // Get a dedicated connection for a specific context
    // This ensures commands for same context go through same connection
    IPCClient* GetConnectionForContext(const std::string& context_id);

    // Return connection to pool
    void ReturnConnection(IPCClient* client);

    // Stats
    size_t GetPoolSize() const { return pool_size_; }
    size_t GetAvailableConnections() const;

private:
    size_t pool_size_;
    std::string instance_id_;
    std::vector<std::unique_ptr<IPCClient>> clients_;
    std::vector<bool> client_in_use_;
    mutable std::mutex pool_mutex_;
    std::condition_variable pool_cv_;

    // Context-to-connection mapping for affinity
    std::unordered_map<std::string, size_t> context_affinity_;
};

} // namespace owl
