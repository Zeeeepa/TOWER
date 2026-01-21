#include "owl_network_interceptor.h"
#include "logger.h"
#include <sstream>
#include <chrono>
#include <algorithm>
#include <iomanip>

// Helper function to escape JSON strings properly
static std::string EscapeJsonString(const std::string& input) {
  std::ostringstream ss;
  for (auto c : input) {
    switch (c) {
      case '"': ss << "\\\""; break;
      case '\\': ss << "\\\\"; break;
      case '\b': ss << "\\b"; break;
      case '\f': ss << "\\f"; break;
      case '\n': ss << "\\n"; break;
      case '\r': ss << "\\r"; break;
      case '\t': ss << "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          // Control characters - escape as unicode
          ss << "\\u" << std::hex << std::setfill('0') << std::setw(4) << static_cast<int>(c);
        } else {
          ss << c;
        }
        break;
    }
  }
  return ss.str();
}

// Helper to serialize a header map to JSON object string
static std::string HeadersToJson(const std::map<std::string, std::string>& headers) {
  std::ostringstream ss;
  ss << "{";
  bool first = true;
  for (const auto& h : headers) {
    if (!first) ss << ",";
    first = false;
    ss << "\"" << EscapeJsonString(h.first) << "\":\"" << EscapeJsonString(h.second) << "\"";
  }
  ss << "}";
  return ss.str();
}

OwlNetworkInterceptor* OwlNetworkInterceptor::GetInstance() {
  static OwlNetworkInterceptor instance;
  return &instance;
}

OwlNetworkInterceptor::OwlNetworkInterceptor() {
  LOG_DEBUG("NetworkInterceptor", "Network interceptor initialized");
}

OwlNetworkInterceptor::~OwlNetworkInterceptor() {}

std::string OwlNetworkInterceptor::AddRule(const InterceptionRule& rule) {
  std::lock_guard<std::mutex> lock(mutex_);

  InterceptionRule new_rule = rule;
  if (new_rule.id.empty()) {
    new_rule.id = "rule_" + std::to_string(++rule_counter_);
  }

  // Use empty string as key for global rules, or context_id for context-specific
  std::string key = "";  // Global by default
  rules_[key].push_back(new_rule);

  LOG_DEBUG("NetworkInterceptor", "Added rule: " + new_rule.id +
           " pattern=" + new_rule.url_pattern +
           " action=" + std::to_string(static_cast<int>(new_rule.action)));

  return new_rule.id;
}

bool OwlNetworkInterceptor::RemoveRule(const std::string& rule_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  for (auto& pair : rules_) {
    auto& rules = pair.second;
    auto it = std::remove_if(rules.begin(), rules.end(),
                             [&rule_id](const InterceptionRule& r) {
                               return r.id == rule_id;
                             });
    if (it != rules.end()) {
      rules.erase(it, rules.end());
      LOG_DEBUG("NetworkInterceptor", "Removed rule: " + rule_id);
      return true;
    }
  }
  return false;
}

void OwlNetworkInterceptor::ClearRules(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (context_id.empty()) {
    rules_.clear();
    LOG_DEBUG("NetworkInterceptor", "Cleared all rules");
  } else {
    rules_.erase(context_id);
    LOG_DEBUG("NetworkInterceptor", "Cleared rules for context: " + context_id);
  }
}

std::vector<InterceptionRule> OwlNetworkInterceptor::GetRules(
    const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<InterceptionRule> result;

  // Add global rules
  auto global_it = rules_.find("");
  if (global_it != rules_.end()) {
    result.insert(result.end(), global_it->second.begin(),
                  global_it->second.end());
  }

  // Add context-specific rules if context_id provided
  if (!context_id.empty()) {
    auto ctx_it = rules_.find(context_id);
    if (ctx_it != rules_.end()) {
      result.insert(result.end(), ctx_it->second.begin(), ctx_it->second.end());
    }
  }

  return result;
}

void OwlNetworkInterceptor::EnableInterception(const std::string& context_id,
                                                bool enable) {
  std::lock_guard<std::mutex> lock(mutex_);
  interception_enabled_[context_id] = enable;
  LOG_DEBUG("NetworkInterceptor",
           "Interception " + std::string(enable ? "enabled" : "disabled") +
               " for context: " + context_id);
}

bool OwlNetworkInterceptor::IsInterceptionEnabled(
    const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = interception_enabled_.find(context_id);
  return it != interception_enabled_.end() && it->second;
}

// Simple glob pattern matching without exceptions
static bool GlobMatch(const std::string& text, const std::string& pattern, size_t ti, size_t pi) {
  while (pi < pattern.length()) {
    char p = pattern[pi];
    if (p == '*') {
      // Match any sequence
      while (pi < pattern.length() && pattern[pi] == '*') pi++;
      if (pi == pattern.length()) return true;  // Trailing * matches everything
      // Try to match the rest
      for (size_t i = ti; i <= text.length(); i++) {
        if (GlobMatch(text, pattern, i, pi)) return true;
      }
      return false;
    } else if (p == '?') {
      // Match single character
      if (ti >= text.length()) return false;
      ti++;
      pi++;
    } else {
      // Literal character (case insensitive)
      if (ti >= text.length()) return false;
      char t = text[ti];
      if (std::tolower(static_cast<unsigned char>(t)) !=
          std::tolower(static_cast<unsigned char>(p))) {
        return false;
      }
      ti++;
      pi++;
    }
  }
  return ti == text.length();
}

bool OwlNetworkInterceptor::MatchesPattern(const std::string& url,
                                            const std::string& pattern,
                                            bool is_regex) const {
  if (pattern.empty()) return false;

  if (is_regex) {
    // Simple substring match for regex mode (without actual regex since exceptions are disabled)
    // This is a simplified fallback - just check if pattern is a substring
    std::string lower_url = url;
    std::string lower_pattern = pattern;
    std::transform(lower_url.begin(), lower_url.end(), lower_url.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::transform(lower_pattern.begin(), lower_pattern.end(), lower_pattern.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return lower_url.find(lower_pattern) != std::string::npos;
  } else {
    // Glob pattern matching (simple implementation without exceptions)
    // * matches any sequence of characters
    // ? matches single character
    return GlobMatch(url, pattern, 0, 0);
  }
}

InterceptionAction OwlNetworkInterceptor::CheckRequest(
    const std::string& context_id, const std::string& url,
    InterceptionRule* matched_rule) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Check if interception is enabled for this context
  auto enabled_it = interception_enabled_.find(context_id);
  if (enabled_it == interception_enabled_.end() || !enabled_it->second) {
    return InterceptionAction::ALLOW;
  }

  // Get all applicable rules (global + context-specific)
  std::vector<const InterceptionRule*> applicable_rules;

  auto global_it = rules_.find("");
  if (global_it != rules_.end()) {
    for (const auto& rule : global_it->second) {
      if (rule.enabled) {
        applicable_rules.push_back(&rule);
      }
    }
  }

  auto ctx_it = rules_.find(context_id);
  if (ctx_it != rules_.end()) {
    for (const auto& rule : ctx_it->second) {
      if (rule.enabled) {
        applicable_rules.push_back(&rule);
      }
    }
  }

  // Check each rule
  for (const auto* rule : applicable_rules) {
    if (MatchesPattern(url, rule->url_pattern, rule->is_regex)) {
      if (matched_rule) {
        *matched_rule = *rule;
      }
      LOG_DEBUG("NetworkInterceptor",
                "URL matched rule " + rule->id + ": " + url);
      return rule->action;
    }
  }

  return InterceptionAction::ALLOW;
}

void OwlNetworkInterceptor::EnableLogging(const std::string& context_id,
                                           bool enable) {
  std::lock_guard<std::mutex> lock(mutex_);
  logging_enabled_[context_id] = enable;
}

bool OwlNetworkInterceptor::IsLoggingEnabled(
    const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = logging_enabled_.find(context_id);
  return it != logging_enabled_.end() && it->second;
}

void OwlNetworkInterceptor::LogRequest(const std::string& context_id,
                                        const CapturedRequest& request) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = logging_enabled_.find(context_id);
  if (it == logging_enabled_.end() || !it->second) {
    return;
  }

  captured_requests_[context_id].push_back(request);

  // Limit captured requests to prevent memory issues
  auto& requests = captured_requests_[context_id];
  if (requests.size() > 1000) {
    requests.erase(requests.begin(), requests.begin() + 100);
  }
}

void OwlNetworkInterceptor::LogResponse(const std::string& context_id,
                                         const CapturedResponse& response) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = logging_enabled_.find(context_id);
  if (it == logging_enabled_.end() || !it->second) {
    return;
  }

  captured_responses_[context_id].push_back(response);

  // Limit captured responses
  auto& responses = captured_responses_[context_id];
  if (responses.size() > 1000) {
    responses.erase(responses.begin(), responses.begin() + 100);
  }
}

std::vector<CapturedRequest> OwlNetworkInterceptor::GetCapturedRequests(
    const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = captured_requests_.find(context_id);
  if (it != captured_requests_.end()) {
    return it->second;
  }
  return {};
}

std::vector<CapturedResponse> OwlNetworkInterceptor::GetCapturedResponses(
    const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = captured_responses_.find(context_id);
  if (it != captured_responses_.end()) {
    return it->second;
  }
  return {};
}

void OwlNetworkInterceptor::ClearCapturedData(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  captured_requests_.erase(context_id);
  captured_responses_.erase(context_id);
}

std::string OwlNetworkInterceptor::GetNetworkLogJSON(
    const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::stringstream ss;
  ss << "{\"requests\":[";

  auto req_it = captured_requests_.find(context_id);
  if (req_it != captured_requests_.end()) {
    bool first = true;
    for (const auto& req : req_it->second) {
      if (!first) ss << ",";
      first = false;
      ss << "{\"id\":\"" << EscapeJsonString(req.request_id) << "\""
         << ",\"url\":\"" << EscapeJsonString(req.url) << "\""
         << ",\"method\":\"" << EscapeJsonString(req.method) << "\""
         << ",\"timestamp\":" << req.timestamp
         << ",\"resourceType\":\"" << EscapeJsonString(req.resource_type) << "\""
         << ",\"headers\":" << HeadersToJson(req.headers);

      // Include request body (POST data) if present
      if (!req.post_data.empty()) {
        ss << ",\"body\":\"" << EscapeJsonString(req.post_data) << "\"";
      }

      ss << "}";
    }
  }

  ss << "],\"responses\":[";

  auto resp_it = captured_responses_.find(context_id);
  if (resp_it != captured_responses_.end()) {
    bool first = true;
    for (const auto& resp : resp_it->second) {
      if (!first) ss << ",";
      first = false;
      ss << "{\"id\":\"" << EscapeJsonString(resp.request_id) << "\""
         << ",\"url\":\"" << EscapeJsonString(resp.url) << "\""
         << ",\"status\":" << resp.status_code
         << ",\"size\":" << resp.response_length
         << ",\"duration\":" << resp.duration_ms
         << ",\"headers\":" << HeadersToJson(resp.headers);

      // Include content type if present
      if (!resp.content_type.empty()) {
        ss << ",\"contentType\":\"" << EscapeJsonString(resp.content_type) << "\"";
      }

      // Include response body if captured
      if (!resp.response_body.empty()) {
        ss << ",\"body\":\"" << EscapeJsonString(resp.response_body) << "\"";
        if (resp.body_truncated) {
          ss << ",\"bodyTruncated\":true";
        }
      }

      // Include error if present
      if (!resp.error.empty()) {
        ss << ",\"error\":\"" << EscapeJsonString(resp.error) << "\"";
      }

      ss << "}";
    }
  }

  ss << "]}";
  return ss.str();
}
