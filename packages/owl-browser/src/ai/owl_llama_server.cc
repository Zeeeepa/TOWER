#include "owl_llama_server.h"
#include "owl_platform_utils.h"
#include "logger.h"
#include <curl/curl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <thread>
#include <chrono>
#include <vector>

OwlLlamaServer::OwlLlamaServer()
    : is_ready_(false),
      port_(8095),
      host_("127.0.0.1"),
      server_pid_(-1) {
}

OwlLlamaServer::~OwlLlamaServer() {
  Stop();
}

std::string OwlLlamaServer::FindLlamaServerBinary() {
  // During build, llama-server is copied to:
  // macOS: owl_browser.app/Contents/MacOS/llama-server
  // Linux: same directory as executable or /usr/local/bin

  // Get executable directory using cross-platform function
  std::string exe_dir = OlibPlatform::GetExecutableDir();
  std::string bundle_bin_path;

  if (!exe_dir.empty()) {
    bundle_bin_path = exe_dir + "/llama-server";
  }

  const char* possible_paths[] = {
    bundle_bin_path.empty() ? NULL : bundle_bin_path.c_str(),  // App bundle or install dir
    "./llama-server",  // Same directory as executable (relative)
    "llama-server",    // Current directory
    "/usr/local/bin/llama-server",  // Standard Linux install location
    "/usr/bin/llama-server",  // Alternative Linux location
    "../Resources/llama-server",  // macOS app bundle Resources (fallback)
    "third_party/llama.cpp/bin/llama-server",  // Development build
    NULL
  };

  for (int i = 0; possible_paths[i] != NULL; i++) {
    if (access(possible_paths[i], X_OK) == 0) {
      LOG_DEBUG("LlamaServer", "Found llama-server at: " + std::string(possible_paths[i]));
      return std::string(possible_paths[i]);
    }
  }

  LOG_ERROR("LlamaServer", "llama-server binary not found in any expected location");
  return "";
}

std::string OwlLlamaServer::FindModelFile(const std::string& model_path) {
  // During build, model is copied to:
  // macOS: owl_browser.app/Contents/Resources/models/llm-assist.gguf
  // Linux: /usr/local/share/owl-browser/models/llm-assist.gguf or next to executable

  // Get resources directory using cross-platform function
  std::string resources_dir = OlibPlatform::GetResourcesDir();
  std::string bundle_model_path;

  if (!resources_dir.empty()) {
    bundle_model_path = resources_dir + "/models/llm-assist.gguf";
  }

  const char* model_paths[] = {
    bundle_model_path.empty() ? NULL : bundle_model_path.c_str(),  // App bundle or install dir
    model_path.c_str(),  // Provided path
    "/usr/local/share/owl-browser/models/llm-assist.gguf",  // Standard Linux location
    "../Resources/models/llm-assist.gguf",  // macOS app bundle (relative)
    "models/llm-assist.gguf",  // Development
    NULL
  };

  for (int i = 0; model_paths[i] != NULL; i++) {
    if (access(model_paths[i], R_OK) == 0) {
      LOG_DEBUG("LlamaServer", "Found model at: " + std::string(model_paths[i]));
      return std::string(model_paths[i]);
    }
  }

  LOG_ERROR("LlamaServer", "Model not found. Searched:");
  for (int i = 0; model_paths[i] != NULL; i++) {
    if (model_paths[i] != NULL) {
      LOG_ERROR("LlamaServer", "  - " + std::string(model_paths[i]));
    }
  }
  return "";
}

std::string OwlLlamaServer::FindMmprojFile() {
  // During build, mmproj is copied to:
  // macOS: owl_browser.app/Contents/Resources/models/mmproj-llm-assist.gguf
  // Linux: /usr/local/share/owl-browser/models/mmproj-llm-assist.gguf

  // Get resources directory using cross-platform function
  std::string resources_dir = OlibPlatform::GetResourcesDir();
  std::string bundle_mmproj_path;

  if (!resources_dir.empty()) {
    bundle_mmproj_path = resources_dir + "/models/mmproj-llm-assist.gguf";
  }

  const char* mmproj_paths[] = {
    bundle_mmproj_path.empty() ? NULL : bundle_mmproj_path.c_str(),  // App bundle or install dir
    "/usr/local/share/owl-browser/models/mmproj-llm-assist.gguf",  // Standard Linux location
    "../Resources/models/mmproj-llm-assist.gguf",  // macOS app bundle (relative)
    "models/mmproj-llm-assist.gguf",  // Development
    NULL
  };

  for (int i = 0; mmproj_paths[i] != NULL; i++) {
    if (access(mmproj_paths[i], R_OK) == 0) {
      LOG_DEBUG("LlamaServer", "Found mmproj at: " + std::string(mmproj_paths[i]));
      return std::string(mmproj_paths[i]);
    }
  }

  LOG_WARN("LlamaServer", "Mmproj file not found - vision features will not work");
  LOG_WARN("LlamaServer", "Searched locations:");
  for (int i = 0; mmproj_paths[i] != NULL; i++) {
    if (mmproj_paths[i] != NULL) {
      LOG_WARN("LlamaServer", "  - " + std::string(mmproj_paths[i]));
    }
  }
  return "";
}

bool OwlLlamaServer::Start(const Config& config) {
  if (is_ready_) {
    LOG_DEBUG("LlamaServer", "Already running");
    return true;
  }

  // Check if server is already running on this port (from previous run)
  if (HealthCheck()) {
    LOG_DEBUG("LlamaServer", "Found existing llama-server on port " + std::to_string(config.port) + ", reusing it");
    port_ = config.port;
    host_ = config.host;
    is_ready_ = true;
    start_time_ = std::chrono::steady_clock::now();
    return true;
  }

  // Find model file
  std::string model_path = FindModelFile(config.model_path);
  if (model_path.empty()) {
    return false;
  }

  // Find mmproj file (optional - for vision model support)
  std::string mmproj_path = FindMmprojFile();

  // Find binary
  std::string bin_path = FindLlamaServerBinary();
  if (bin_path.empty()) {
    return false;
  }

  port_ = config.port;
  host_ = config.host;

  LOG_DEBUG("LlamaServer", "Starting llama-server with high-performance concurrent settings...");
  LOG_DEBUG("LlamaServer", "Binary: " + bin_path);
  LOG_DEBUG("LlamaServer", "Model: " + model_path);
  if (!mmproj_path.empty()) {
    LOG_DEBUG("LlamaServer", "Mmproj: " + mmproj_path);
  }
  LOG_DEBUG("LlamaServer", "Port: " + std::to_string(port_));
  LOG_DEBUG("LlamaServer", "Threads: " + std::to_string(config.threads));
  LOG_DEBUG("LlamaServer", "GPU layers: " + std::to_string(config.gpu_layers));
  LOG_DEBUG("LlamaServer", "Context size: " + std::to_string(config.context_size));
  LOG_DEBUG("LlamaServer", "Batch size: " + std::to_string(config.batch_size));
  LOG_DEBUG("LlamaServer", "Parallel slots: " + std::to_string(config.parallel_slots));
  LOG_DEBUG("LlamaServer", "Continuous batching: " + std::string(config.continuous_batching ? "enabled" : "disabled"));
  LOG_DEBUG("LlamaServer", "Flash attention: " + std::string(config.flash_attention ? "enabled" : "disabled"));

#ifdef OS_POSIX
  server_pid_ = fork();

  if (server_pid_ == 0) {
    // Child process - exec llama-server

    // Build argument strings (must persist for execv)
    std::vector<std::string> arg_strings;
    arg_strings.push_back(bin_path);
    arg_strings.push_back("--model");
    arg_strings.push_back(model_path);
    arg_strings.push_back("--port");
    arg_strings.push_back(std::to_string(config.port));
    arg_strings.push_back("--host");
    arg_strings.push_back(config.host);
    arg_strings.push_back("--ctx-size");
    arg_strings.push_back(std::to_string(config.context_size));
    arg_strings.push_back("--threads");
    arg_strings.push_back(std::to_string(config.threads));
    arg_strings.push_back("--n-gpu-layers");
    arg_strings.push_back(std::to_string(config.gpu_layers));
    arg_strings.push_back("--batch-size");
    arg_strings.push_back(std::to_string(config.batch_size));

    // Add mmproj for vision model support (if available)
    if (!mmproj_path.empty()) {
      arg_strings.push_back("--mmproj");
      arg_strings.push_back(mmproj_path);
    }

    // Enable parallel processing for concurrent tile classification
    arg_strings.push_back("--parallel");
    arg_strings.push_back(std::to_string(config.parallel_slots));

    // Enable continuous batching for better throughput with parallel requests
    if (config.continuous_batching) {
      arg_strings.push_back("--cont-batching");
    }

    // Enable flash attention for efficiency
    // Note: newer llama.cpp requires --flash-attn to have a value (on/off/auto)
    if (config.flash_attention) {
      arg_strings.push_back("--flash-attn");
      arg_strings.push_back("on");
    }

    // Build argv array
    std::vector<const char*> argv;
    for (const auto& str : arg_strings) {
      argv.push_back(str.c_str());
    }
    argv.push_back(NULL);

    // Redirect stdout/stderr to log file for debugging
    // This helps diagnose startup issues with llama-server
    int log_fd = open("/tmp/llama-server.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log_fd != -1) {
      dup2(log_fd, STDOUT_FILENO);
      dup2(log_fd, STDERR_FILENO);
      close(log_fd);
    }

    execv(bin_path.c_str(), (char* const*)argv.data());

    // If we get here, exec failed
    _exit(1);
  } else if (server_pid_ < 0) {
    LOG_ERROR("LlamaServer", "Failed to fork process");
    return false;
  }
#endif

  start_time_ = std::chrono::steady_clock::now();

  // Wait for server to be ready
  LOG_DEBUG("LlamaServer", "Waiting for llama-server to load model...");
  if (!WaitForReady()) {
    LOG_ERROR("LlamaServer", "Server failed to start within timeout");
    Stop();
    return false;
  }

  LOG_DEBUG("LlamaServer", "✓ llama-server ready and accepting requests");
  return true;
}

void OwlLlamaServer::Stop() {
  if (!is_ready_ && server_pid_ <= 0) {
    return;
  }

  LOG_DEBUG("LlamaServer", "Stopping llama-server...");

#ifdef OS_POSIX
  // Only kill if we own the process (server_pid_ > 0)
  // If server_pid_ == -1, we're reusing an existing server
  if (server_pid_ > 0) {
    LOG_DEBUG("LlamaServer", "Terminating owned llama-server process (PID: " + std::to_string(server_pid_) + ")");

    // Send SIGTERM for graceful shutdown
    kill(server_pid_, SIGTERM);

    // Wait up to 2 seconds for graceful shutdown
    for (int i = 0; i < 20; i++) {
      int status;
      pid_t result = waitpid(server_pid_, &status, WNOHANG);
      if (result != 0) {
        LOG_DEBUG("LlamaServer", "Process exited gracefully");
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Force kill if still running
    int status;
    pid_t result = waitpid(server_pid_, &status, WNOHANG);
    if (result == 0) {
      LOG_WARN("LlamaServer", "Force killing process");
      kill(server_pid_, SIGKILL);
      waitpid(server_pid_, NULL, 0);
    }

    server_pid_ = -1;
  } else if (server_pid_ == -1) {
    LOG_DEBUG("LlamaServer", "Reused server - not terminating (will be reused by next browser instance)");
  }
#endif

  is_ready_ = false;
  LOG_DEBUG("LlamaServer", "Stopped");
}

std::string OwlLlamaServer::GetServerURL() const {
  return "http://" + host_ + ":" + std::to_string(port_);
}

double OwlLlamaServer::GetUptimeSeconds() const {
  if (!is_ready_) {
    return 0.0;
  }
  auto now = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);
  return duration.count() / 1000.0;
}

bool OwlLlamaServer::WaitForReady(int timeout_ms) {
  auto start = std::chrono::steady_clock::now();

  LOG_DEBUG("LlamaServer", "Polling health endpoint...");

  int attempt = 0;
  while (true) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();

    if (elapsed > timeout_ms) {
      LOG_ERROR("LlamaServer", "Timeout waiting for server (30s)");
      return false;
    }

    if (HealthCheck()) {
      is_ready_ = true;
#ifdef OWL_DEBUG_BUILD
      double elapsed_sec = elapsed / 1000.0;
      LOG_DEBUG("LlamaServer", "Server ready after " + std::to_string(elapsed_sec) + "s");
#endif
      return true;
    }

    // Log progress every 5 seconds
    attempt++;
    if (attempt % 50 == 0) {  // Every 5 seconds (100ms * 50)
      LOG_DEBUG("LlamaServer", "Still loading model... (" + std::to_string(elapsed / 1000) + "s)");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

// Callback for curl
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  ((std::string*)userp)->append((char*)contents, size * nmemb);
  return size * nmemb;
}

bool OwlLlamaServer::HealthCheck() {
  CURL* curl = curl_easy_init();
  if (!curl) {
    return false;
  }

  std::string response;
  std::string url = GetServerURL() + "/health";

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 1L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);  // Thread-safe

  CURLcode res = curl_easy_perform(curl);
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_easy_cleanup(curl);

  return (res == CURLE_OK && http_code == 200);
}
