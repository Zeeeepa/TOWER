#ifndef OLIB_LOGGER_H_
#define OLIB_LOGGER_H_

#include <string>
#include <sstream>
#include <iostream>
#include <chrono>
#include <iomanip>

namespace OlibLogger {

enum Level {
  DEBUG,
  INFO,
  WARN,
  ERROR
};

class Logger {
public:
  static void Init();
  static void Init(const std::string& log_file_path);  // Initialize with log file
  static void SetLevel(Level level);
  static Level GetLevel();  // Get current log level
  static void Log(Level level, const std::string& component, const std::string& message);

  // Convenience methods
  static void Debug(const std::string& component, const std::string& message);
  static void Info(const std::string& component, const std::string& message);
  static void Warn(const std::string& component, const std::string& message);
  static void Error(const std::string& component, const std::string& message);

private:
  static Level current_level_;
  static std::string GetTimestamp();
  static std::string LevelToString(Level level);
};

} // namespace OlibLogger

// Convenience macros - LOG_DEBUG only compiles in debug builds
#ifdef OWL_DEBUG_BUILD
  #define LOG_DEBUG(component, msg) OlibLogger::Logger::Debug(component, msg)
#else
  #define LOG_DEBUG(component, msg) ((void)0)  // No-op in release builds
#endif

#define LOG_INFO(component, msg) OlibLogger::Logger::Info(component, msg)
#define LOG_WARN(component, msg) OlibLogger::Logger::Warn(component, msg)
#define LOG_ERROR(component, msg) OlibLogger::Logger::Error(component, msg)

#endif  // OLIB_LOGGER_H_
