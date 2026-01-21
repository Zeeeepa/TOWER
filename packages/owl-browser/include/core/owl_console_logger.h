#ifndef OWL_CONSOLE_LOGGER_H_
#define OWL_CONSOLE_LOGGER_H_

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <cstdint>

// Console log entry - stores a single console message
struct ConsoleLogEntry {
  int64_t timestamp;           // Unix timestamp in milliseconds
  std::string level;           // "debug", "info", "warn", "error", "log"
  std::string message;         // The console message
  std::string source;          // Source URL
  int line;                    // Line number

  ConsoleLogEntry() : timestamp(0), line(0) {}
};

// Console logger singleton class - stores console logs per context
class OwlConsoleLogger {
 public:
  static OwlConsoleLogger* GetInstance();

  // Log a console message
  void LogMessage(const std::string& context_id,
                  const std::string& level,
                  const std::string& message,
                  const std::string& source,
                  int line);

  // Get console logs for a context
  // Optional filters: level (exact match), filter (substring in message), limit (max entries)
  std::vector<ConsoleLogEntry> GetLogs(const std::string& context_id,
                                        const std::string& level_filter = "",
                                        const std::string& text_filter = "",
                                        int limit = 0) const;

  // Get console logs as JSON
  std::string GetLogsJSON(const std::string& context_id,
                          const std::string& level_filter = "",
                          const std::string& text_filter = "",
                          int limit = 0) const;

  // Clear console logs for a context
  void ClearLogs(const std::string& context_id);

  // Enable/disable logging for a context (enabled by default)
  void EnableLogging(const std::string& context_id, bool enable);
  bool IsLoggingEnabled(const std::string& context_id) const;

 private:
  OwlConsoleLogger();
  ~OwlConsoleLogger();

  // Escape string for JSON output
  std::string EscapeJsonString(const std::string& str) const;

  mutable std::mutex mutex_;

  // Context ID -> log entries mapping
  std::map<std::string, std::vector<ConsoleLogEntry>> logs_;

  // Context ID -> enabled state (default true)
  std::map<std::string, bool> logging_enabled_;

  // Maximum entries per context to prevent memory issues
  static constexpr size_t kMaxEntriesPerContext = 1000;
};

#endif  // OWL_CONSOLE_LOGGER_H_
