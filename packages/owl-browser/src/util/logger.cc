#include "logger.h"
#include <mutex>
#include <unistd.h>  // for getpid(), write()
#include <fcntl.h>   // for open()
#include <cstring>   // for strlen()
#include <cerrno>    // for errno

namespace OlibLogger {

// Default to INFO level - DEBUG only enabled when OWL_DEBUG_BUILD is defined
#ifdef OWL_DEBUG_BUILD
Level Logger::current_level_ = DEBUG;
#else
Level Logger::current_level_ = INFO;
#endif

static std::mutex log_mutex;
static int log_file_fd = -1;  // Use POSIX file descriptor instead of C++ stream
static std::string log_file_path_global;

void Logger::Init() {
#ifdef OWL_DEBUG_BUILD
  current_level_ = DEBUG;
#else
  current_level_ = INFO;
#endif
}

void Logger::Init(const std::string& log_file_path) {
#ifdef OWL_DEBUG_BUILD
  current_level_ = DEBUG;
#else
  current_level_ = INFO;
#endif
  log_file_path_global = log_file_path;

  // Open log file using POSIX open() with O_APPEND for multi-process safety
  // O_APPEND ensures atomic writes are appended even from multiple processes
  log_file_fd = open(log_file_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);

  if (log_file_fd < 0) {
    std::cerr << "[Logger] ERROR: Failed to open log file: " << log_file_path << std::endl;
  }
}

void Logger::SetLevel(Level level) {
  current_level_ = level;
}

Level Logger::GetLevel() {
  return current_level_;
}

std::string Logger::GetTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch()) % 1000;
  auto timer = std::chrono::system_clock::to_time_t(now);
  std::tm bt = *std::localtime(&timer);

  std::ostringstream oss;
  oss << std::put_time(&bt, "%H:%M:%S");
  oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
  return oss.str();
}

std::string Logger::LevelToString(Level level) {
  switch (level) {
    case DEBUG: return "DEBUG";
    case INFO:  return "INFO ";
    case WARN:  return "WARN ";
    case ERROR: return "ERROR";
    default:    return "UNKNOWN";
  }
}

void Logger::Log(Level level, const std::string& component, const std::string& message) {
  if (level < current_level_) {
    return;
  }

  std::lock_guard<std::mutex> lock(log_mutex);

  // Format the log message
  std::string log_line = "[" + GetTimestamp() + "] " +
                         "[" + LevelToString(level) + "] " +
                         "[" + component + "] " +
                         message + "\n";

  // Write to stderr
  std::cerr << log_line;

  // Write to log file if configured (open fresh for multi-process safety)
  if (!log_file_path_global.empty()) {
    int fd = open(log_file_path_global.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
      ssize_t bytes_written = write(fd, log_line.c_str(), log_line.length());
      (void)bytes_written;  // Intentionally ignore - logging shouldn't fail the application
      close(fd);  // Close immediately after write
    }
  }
}

void Logger::Debug(const std::string& component, const std::string& message) {
  Log(DEBUG, component, message);
}

void Logger::Info(const std::string& component, const std::string& message) {
  Log(INFO, component, message);
}

void Logger::Warn(const std::string& component, const std::string& message) {
  Log(WARN, component, message);
}

void Logger::Error(const std::string& component, const std::string& message) {
  Log(ERROR, component, message);
}

} // namespace OlibLogger
