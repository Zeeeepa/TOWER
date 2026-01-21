#pragma once

#include <string>
#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>
#include <memory>
#include <condition_variable>
#include <unordered_map>
#include "json.hpp"

using json = nlohmann::json;

// Connection mode enum
enum class ConnectionMode {
    AUTO,       // Auto-detect: try socket first, fallback to pipe
    SOCKET,     // Socket only (fail if not available)
    PIPE        // Pipe only (stdin/stdout)
};

class IPCClient {
public:
    explicit IPCClient(const std::string& browser_path);
    ~IPCClient();

    // Lifecycle
    bool Start(const std::string& instance_id = "", ConnectionMode mode = ConnectionMode::AUTO);
    void Stop();
    bool IsRunning() const;

    // Get browser process ID
    pid_t GetBrowserPID() const { return child_pid_; }

    // Get connection mode
    ConnectionMode GetConnectionMode() const { return active_mode_; }
    bool IsSocketMode() const { return active_mode_ == ConnectionMode::SOCKET; }

    // Get socket path (if socket mode)
    const std::string& GetSocketPath() const { return socket_path_; }

    // Get instance ID
    const std::string& GetInstanceId() const { return instance_id_; }

    // Send command and wait for response
    json Send(const std::string& method, const json& params = json::object());

    // Raw send for debugging
    std::string SendRaw(const std::string& json_command);

    // Get timing metrics
    double GetLastResponseTimeMs() const { return last_response_time_ms_; }
    double GetLastParseTimeMs() const { return last_parse_time_ms_; }
    int64_t GetLastRequestSize() const { return last_request_size_; }
    int64_t GetLastResponseSize() const { return last_response_size_; }

    // Get browser path
    const std::string& GetBrowserPath() const { return browser_path_; }

    // Enable verbose debugging output
    void SetVerbose(bool verbose) { verbose_ = verbose; }

private:
    std::string browser_path_;
    std::string instance_id_;
    std::string socket_path_;
    ConnectionMode active_mode_ = ConnectionMode::PIPE;

    pid_t child_pid_ = -1;
    int stdin_fd_ = -1;   // Write to browser (pipe mode)
    int stdout_fd_ = -1;  // Read from browser (pipe mode)
    int socket_fd_ = -1;  // Socket for socket mode

    std::atomic<int> command_id_{1};
    std::mutex io_mutex_;

    // Timing metrics
    double last_response_time_ms_ = 0.0;
    double last_parse_time_ms_ = 0.0;
    int64_t last_request_size_ = 0;
    int64_t last_response_size_ = 0;

    // Read buffer for handling partial lines
    std::string read_buffer_;

    // Verbose output
    bool verbose_ = false;

    bool WaitForReady(int timeout_ms = 30000);
    std::string ReadLine(int timeout_ms = 60000);
    bool WriteLine(const std::string& line);

    // Socket-specific methods
    bool ConnectSocket();
    std::string ReadLineSocket(int timeout_ms = 60000);
    bool WriteLineSocket(const std::string& line);
};

/**
 * Socket-only IPC Client for parallel connections
 * Connects to an already-running browser instance via socket
 */
class SocketClient {
public:
    SocketClient();
    ~SocketClient();

    // Connect to an existing browser's socket
    bool Connect(const std::string& socket_path);
    void Disconnect();
    bool IsConnected() const { return connected_; }

    // Send command and wait for response
    json Send(const std::string& method, const json& params = json::object());
    std::string SendRaw(const std::string& json_command, int timeout_ms = 60000);

    // Metrics
    double GetLastResponseTimeMs() const { return last_response_time_ms_; }

    // Enable verbose debugging output
    void SetVerbose(bool verbose) { verbose_ = verbose; }

private:
    int socket_fd_ = -1;
    std::string socket_path_;
    std::atomic<bool> connected_{false};
    std::atomic<int> command_id_{1};
    std::mutex io_mutex_;
    std::string read_buffer_;
    double last_response_time_ms_ = 0.0;
    bool verbose_ = false;

    std::string ReadLine(int timeout_ms);
    bool WriteLine(const std::string& line);
};

/**
 * Connection pool for parallel socket connections to same browser
 * Allows running multiple tests in parallel
 */
class IPCConnectionPool {
public:
    explicit IPCConnectionPool(size_t pool_size = 4);
    ~IPCConnectionPool();

    // Initialize pool by connecting to the socket
    bool Initialize(const std::string& socket_path);

    // Get an available connection (blocks if all busy)
    SocketClient* AcquireConnection(int timeout_ms = 5000);

    // Return connection to pool
    void ReleaseConnection(SocketClient* client);

    // Get connection for specific context (affinity)
    SocketClient* GetConnectionForContext(const std::string& context_id);

    // Stats
    size_t GetPoolSize() const { return pool_size_; }
    size_t GetActiveConnections() const { return active_count_; }
    size_t GetAvailableConnections() const;

    // Enable verbose output on all connections
    void SetVerbose(bool verbose);

private:
    size_t pool_size_;
    std::string socket_path_;
    std::vector<std::unique_ptr<SocketClient>> clients_;
    std::vector<bool> client_in_use_;
    std::atomic<size_t> active_count_{0};
    mutable std::mutex pool_mutex_;
    std::condition_variable pool_cv_;
    std::unordered_map<std::string, size_t> context_affinity_;
    bool verbose_ = false;
};
