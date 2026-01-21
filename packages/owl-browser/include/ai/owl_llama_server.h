#ifndef OWL_LLAMA_SERVER_H_
#define OWL_LLAMA_SERVER_H_

#include <string>
#include <memory>
#include <chrono>
#include "include/cef_base.h"

// High-performance llama-server subprocess manager
// Spawns llama.cpp server as child process and manages its lifecycle
class OwlLlamaServer {
 public:
  struct Config {
    std::string model_path = "models/llm-assist.gguf";
    std::string host = "127.0.0.1";
    int port = 8095;
    int context_size = 32768;  // Large context for parallel requests (-c)
    int threads = 4;  // Physical cores, not hyperthreads
    int gpu_layers = 99;  // Use all GPU layers (Metal on macOS)
    int batch_size = 2048;  // Larger batch for better throughput with parallel
    int parallel_slots = 16;  // Number of parallel request slots (-np)
    bool continuous_batching = true;  // Enable continuous batching (-cb)
    bool use_mmap = true;  // Memory-mapped model loading (faster startup)
    bool use_mlock = false;  // Lock model in RAM (prevents swapping, uses more RAM)
    bool flash_attention = true;  // Enable flash attention for efficiency (-fa)
  };

  OwlLlamaServer();
  ~OwlLlamaServer();

  // Start llama-server subprocess with optimized settings
  bool Start(const Config& config);

  // Stop llama-server subprocess gracefully
  void Stop();

  // Check if server is ready to accept requests
  bool IsReady() const { return is_ready_; }

  // Get server URL for HTTP requests
  std::string GetServerURL() const;

  // Get server uptime in seconds
  double GetUptimeSeconds() const;

 private:
  bool is_ready_;
  int port_;
  std::string host_;
  std::chrono::steady_clock::time_point start_time_;

#ifdef OS_POSIX
  pid_t server_pid_;
#elif OS_WINDOWS
  HANDLE server_process_;
#endif

  // Wait for server to become ready (health check polling)
  bool WaitForReady(int timeout_ms = 30000);

  // Health check - ping /health endpoint
  bool HealthCheck();

  // Find llama-server binary in app bundle or development paths
  std::string FindLlamaServerBinary();

  // Find model file in app bundle or development paths
  std::string FindModelFile(const std::string& model_path);

  // Find mmproj file in app bundle or development paths (optional for vision support)
  std::string FindMmprojFile();
};

#endif  // OWL_LLAMA_SERVER_H_
