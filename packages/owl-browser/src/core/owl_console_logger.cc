#include "owl_console_logger.h"
#include "logger.h"
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>

OwlConsoleLogger* OwlConsoleLogger::GetInstance() {
  static OwlConsoleLogger instance;
  return &instance;
}

OwlConsoleLogger::OwlConsoleLogger() {
  LOG_DEBUG("ConsoleLogger", "Console logger initialized");
}

OwlConsoleLogger::~OwlConsoleLogger() {}

void OwlConsoleLogger::LogMessage(const std::string& context_id,
                                   const std::string& level,
                                   const std::string& message,
                                   const std::string& source,
                                   int line) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Check if logging is enabled for this context (default: enabled)
  auto enabled_it = logging_enabled_.find(context_id);
  if (enabled_it != logging_enabled_.end() && !enabled_it->second) {
    return;
  }

  ConsoleLogEntry entry;
  entry.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  entry.level = level;
  entry.message = message;
  entry.source = source;
  entry.line = line;

  logs_[context_id].push_back(entry);

  // Limit entries per context to prevent memory issues
  auto& entries = logs_[context_id];
  if (entries.size() > kMaxEntriesPerContext) {
    // Remove oldest 100 entries when limit exceeded
    entries.erase(entries.begin(), entries.begin() + 100);
  }
}

std::vector<ConsoleLogEntry> OwlConsoleLogger::GetLogs(
    const std::string& context_id,
    const std::string& level_filter,
    const std::string& text_filter,
    int limit) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<ConsoleLogEntry> result;

  auto it = logs_.find(context_id);
  if (it == logs_.end()) {
    return result;
  }

  const auto& entries = it->second;

  // Iterate in reverse to get newest first, then reverse at end
  for (auto rit = entries.rbegin(); rit != entries.rend(); ++rit) {
    const auto& entry = *rit;

    // Apply level filter if specified
    if (!level_filter.empty() && entry.level != level_filter) {
      continue;
    }

    // Apply text filter if specified (case-insensitive substring match)
    if (!text_filter.empty()) {
      std::string lower_message = entry.message;
      std::string lower_filter = text_filter;
      std::transform(lower_message.begin(), lower_message.end(),
                     lower_message.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      std::transform(lower_filter.begin(), lower_filter.end(),
                     lower_filter.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      if (lower_message.find(lower_filter) == std::string::npos) {
        continue;
      }
    }

    result.push_back(entry);

    // Apply limit if specified
    if (limit > 0 && static_cast<int>(result.size()) >= limit) {
      break;
    }
  }

  // Reverse to restore chronological order (oldest first)
  std::reverse(result.begin(), result.end());

  return result;
}

std::string OwlConsoleLogger::EscapeJsonString(const std::string& str) const {
  std::ostringstream ss;
  for (char c : str) {
    switch (c) {
      case '"':  ss << "\\\""; break;
      case '\\': ss << "\\\\"; break;
      case '\b': ss << "\\b"; break;
      case '\f': ss << "\\f"; break;
      case '\n': ss << "\\n"; break;
      case '\r': ss << "\\r"; break;
      case '\t': ss << "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          // Control character - output as \u00XX
          ss << "\\u00" << std::hex << std::setfill('0') << std::setw(2)
             << static_cast<int>(static_cast<unsigned char>(c));
        } else {
          ss << c;
        }
        break;
    }
  }
  return ss.str();
}

std::string OwlConsoleLogger::GetLogsJSON(
    const std::string& context_id,
    const std::string& level_filter,
    const std::string& text_filter,
    int limit) const {
  std::vector<ConsoleLogEntry> entries = GetLogs(context_id, level_filter,
                                                  text_filter, limit);

  std::ostringstream ss;
  ss << "{\"logs\":[";

  bool first = true;
  for (const auto& entry : entries) {
    if (!first) ss << ",";
    first = false;

    ss << "{\"timestamp\":" << entry.timestamp
       << ",\"level\":\"" << EscapeJsonString(entry.level) << "\""
       << ",\"message\":\"" << EscapeJsonString(entry.message) << "\""
       << ",\"source\":\"" << EscapeJsonString(entry.source) << "\""
       << ",\"line\":" << entry.line << "}";
  }

  ss << "],\"count\":" << entries.size() << "}";
  return ss.str();
}

void OwlConsoleLogger::ClearLogs(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  logs_.erase(context_id);
  LOG_DEBUG("ConsoleLogger", "Cleared console logs for context: " + context_id);
}

void OwlConsoleLogger::EnableLogging(const std::string& context_id, bool enable) {
  std::lock_guard<std::mutex> lock(mutex_);
  logging_enabled_[context_id] = enable;
  LOG_DEBUG("ConsoleLogger",
            "Console logging " + std::string(enable ? "enabled" : "disabled") +
                " for context: " + context_id);
}

bool OwlConsoleLogger::IsLoggingEnabled(const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = logging_enabled_.find(context_id);
  // Default to enabled if not explicitly set
  return it == logging_enabled_.end() || it->second;
}
