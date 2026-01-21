#ifndef OWL_NETWORK_INTERCEPTOR_H_
#define OWL_NETWORK_INTERCEPTOR_H_

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <functional>
#include <atomic>

// Network interception rule types
enum class InterceptionAction {
  ALLOW,      // Allow request to proceed
  BLOCK,      // Block the request entirely
  MOCK,       // Return mock response
  MODIFY,     // Modify request before sending
  REDIRECT    // Redirect to different URL
};

// Rule for intercepting network requests
struct InterceptionRule {
  std::string id;                      // Unique rule ID
  std::string url_pattern;             // URL pattern (glob or regex)
  bool is_regex;                       // true = regex, false = glob pattern
  InterceptionAction action;           // Action to take
  std::string redirect_url;            // For REDIRECT action
  std::string mock_body;               // For MOCK action - response body
  std::string mock_content_type;       // For MOCK action - content type
  int mock_status_code;                // For MOCK action - HTTP status
  std::map<std::string, std::string> mock_headers;  // For MOCK action
  std::map<std::string, std::string> modify_headers; // For MODIFY action
  bool enabled;                        // Rule is active

  InterceptionRule() : is_regex(false), action(InterceptionAction::ALLOW),
                       mock_status_code(200), enabled(true) {}
};

// Captured network request information
struct CapturedRequest {
  std::string request_id;
  std::string url;
  std::string method;
  std::map<std::string, std::string> headers;
  std::string post_data;
  int64_t timestamp;
  std::string resource_type;
};

// Captured network response information
struct CapturedResponse {
  std::string request_id;
  std::string url;
  int status_code;
  std::map<std::string, std::string> headers;
  int64_t response_length;
  int64_t duration_ms;
  std::string error;
  std::string response_body;      // Response body content (limited size, text only)
  std::string content_type;       // Content-Type header for body interpretation
  bool body_truncated;            // True if body was truncated due to size limit

  CapturedResponse() : status_code(0), response_length(0), duration_ms(0), body_truncated(false) {}
};

// Network interceptor singleton class
class OwlNetworkInterceptor {
 public:
  static OwlNetworkInterceptor* GetInstance();

  // Rule management
  std::string AddRule(const InterceptionRule& rule);
  bool RemoveRule(const std::string& rule_id);
  void ClearRules(const std::string& context_id = "");
  std::vector<InterceptionRule> GetRules(const std::string& context_id = "") const;

  // Enable/disable interception for a context
  void EnableInterception(const std::string& context_id, bool enable);
  bool IsInterceptionEnabled(const std::string& context_id) const;

  // Check if URL matches any rule and get action
  InterceptionAction CheckRequest(const std::string& context_id,
                                   const std::string& url,
                                   InterceptionRule* matched_rule = nullptr);

  // Network logging
  void EnableLogging(const std::string& context_id, bool enable);
  bool IsLoggingEnabled(const std::string& context_id) const;
  void LogRequest(const std::string& context_id, const CapturedRequest& request);
  void LogResponse(const std::string& context_id, const CapturedResponse& response);

  // Get captured network data
  std::vector<CapturedRequest> GetCapturedRequests(const std::string& context_id) const;
  std::vector<CapturedResponse> GetCapturedResponses(const std::string& context_id) const;
  void ClearCapturedData(const std::string& context_id);

  // Get network log as JSON
  std::string GetNetworkLogJSON(const std::string& context_id) const;

 private:
  OwlNetworkInterceptor();
  ~OwlNetworkInterceptor();

  bool MatchesPattern(const std::string& url, const std::string& pattern, bool is_regex) const;

  mutable std::mutex mutex_;

  // Context ID -> Rules mapping
  std::map<std::string, std::vector<InterceptionRule>> rules_;

  // Context ID -> enabled state
  std::map<std::string, bool> interception_enabled_;
  std::map<std::string, bool> logging_enabled_;

  // Captured network data per context
  std::map<std::string, std::vector<CapturedRequest>> captured_requests_;
  std::map<std::string, std::vector<CapturedResponse>> captured_responses_;

  std::atomic<uint64_t> rule_counter_{0};
  std::atomic<uint64_t> request_counter_{0};
};

#endif  // OWL_NETWORK_INTERCEPTOR_H_
