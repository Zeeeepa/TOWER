#pragma once

#include <string>
#include <sstream>

// Action status codes for validation responses
// These provide detailed information about what happened during an action
enum class ActionStatus {
  // Success statuses
  OK,                        // Action completed successfully

  // Browser/context errors
  BROWSER_NOT_FOUND,         // Context ID doesn't exist or browser is closed
  BROWSER_NOT_READY,         // Browser exists but not ready (initializing)
  CONTEXT_NOT_FOUND,         // Context not found

  // Navigation errors
  NAVIGATION_FAILED,         // Navigation failed (network error, timeout, etc.)
  NAVIGATION_TIMEOUT,        // Navigation didn't complete in time
  PAGE_LOAD_ERROR,           // Page failed to load (HTTP error, DNS error, etc.)
  REDIRECT_DETECTED,         // Page redirected to different URL
  CAPTCHA_DETECTED,          // Page appears to show a CAPTCHA
  FIREWALL_DETECTED,         // Web firewall/bot protection challenge detected

  // Element interaction errors
  ELEMENT_NOT_FOUND,         // Element selector didn't match any element
  ELEMENT_NOT_VISIBLE,       // Element exists but is not visible
  ELEMENT_NOT_INTERACTABLE,  // Element visible but cannot be interacted with
  ELEMENT_STALE,             // Element was found but is no longer in DOM
  MULTIPLE_ELEMENTS,         // Selector matched multiple elements (ambiguous)

  // Action execution errors
  CLICK_FAILED,              // Click action failed
  CLICK_INTERCEPTED,         // Click would be received by another element
  TYPE_FAILED,               // Type action failed - verification showed text not entered
  TYPE_PARTIAL,              // Some but not all text was entered
  SCROLL_FAILED,             // Scroll action failed
  FOCUS_FAILED,              // Focus action failed - verification showed element not focused
  BLUR_FAILED,               // Blur action failed - verification showed element still focused
  CLEAR_FAILED,              // Clear input failed - verification showed field still has content
  PICK_FAILED,               // Dropdown selection verification failed
  OPTION_NOT_FOUND,          // Requested dropdown option doesn't exist
  UPLOAD_FAILED,             // File upload failed - file not set on input
  FRAME_SWITCH_FAILED,       // Failed to switch to frame
  TAB_SWITCH_FAILED,         // Failed to switch to tab
  DIALOG_NOT_HANDLED,        // Dialog was not handled properly

  // Validation errors
  INVALID_SELECTOR,          // Selector syntax is invalid
  INVALID_URL,               // URL is malformed or not allowed
  INVALID_PARAMETER,         // A parameter has invalid value

  // System errors
  INTERNAL_ERROR,            // Unexpected internal error
  TIMEOUT,                   // Generic timeout
  NETWORK_TIMEOUT,           // Network idle wait timed out
  WAIT_TIMEOUT,              // Wait condition timed out
  VERIFICATION_TIMEOUT,      // Post-action verification timed out (NEW)

  // Unknown
  UNKNOWN                    // Unknown status
};

// Verification level controls how thoroughly actions are validated
enum class VerificationLevel {
  NONE,       // Fire-and-forget - fastest, no post-action verification
  BASIC,      // Pre-action checks only (element exists, visible)
  STANDARD,   // Basic + post-action state verification (default)
  STRICT      // Standard + wait for DOM/network stabilization
};

// Convert VerificationLevel to string
inline const char* VerificationLevelToString(VerificationLevel level) {
  switch (level) {
    case VerificationLevel::NONE: return "none";
    case VerificationLevel::BASIC: return "basic";
    case VerificationLevel::STANDARD: return "standard";
    case VerificationLevel::STRICT: return "strict";
    default: return "standard";
  }
}

// Parse string to VerificationLevel
inline VerificationLevel ParseVerificationLevel(const std::string& str) {
  if (str == "none") return VerificationLevel::NONE;
  if (str == "basic") return VerificationLevel::BASIC;
  if (str == "strict") return VerificationLevel::STRICT;
  return VerificationLevel::STANDARD;  // default
}

// Convert ActionStatus to string code
inline const char* ActionStatusToCode(ActionStatus status) {
  switch (status) {
    case ActionStatus::OK: return "ok";
    case ActionStatus::BROWSER_NOT_FOUND: return "browser_not_found";
    case ActionStatus::BROWSER_NOT_READY: return "browser_not_ready";
    case ActionStatus::CONTEXT_NOT_FOUND: return "context_not_found";
    case ActionStatus::NAVIGATION_FAILED: return "navigation_failed";
    case ActionStatus::NAVIGATION_TIMEOUT: return "navigation_timeout";
    case ActionStatus::PAGE_LOAD_ERROR: return "page_load_error";
    case ActionStatus::REDIRECT_DETECTED: return "redirect_detected";
    case ActionStatus::CAPTCHA_DETECTED: return "captcha_detected";
    case ActionStatus::FIREWALL_DETECTED: return "firewall_detected";
    case ActionStatus::ELEMENT_NOT_FOUND: return "element_not_found";
    case ActionStatus::ELEMENT_NOT_VISIBLE: return "element_not_visible";
    case ActionStatus::ELEMENT_NOT_INTERACTABLE: return "element_not_interactable";
    case ActionStatus::ELEMENT_STALE: return "element_stale";
    case ActionStatus::MULTIPLE_ELEMENTS: return "multiple_elements";
    case ActionStatus::CLICK_FAILED: return "click_failed";
    case ActionStatus::CLICK_INTERCEPTED: return "click_intercepted";
    case ActionStatus::TYPE_FAILED: return "type_failed";
    case ActionStatus::TYPE_PARTIAL: return "type_partial";
    case ActionStatus::SCROLL_FAILED: return "scroll_failed";
    case ActionStatus::FOCUS_FAILED: return "focus_failed";
    case ActionStatus::BLUR_FAILED: return "blur_failed";
    case ActionStatus::CLEAR_FAILED: return "clear_failed";
    case ActionStatus::PICK_FAILED: return "pick_failed";
    case ActionStatus::OPTION_NOT_FOUND: return "option_not_found";
    case ActionStatus::UPLOAD_FAILED: return "upload_failed";
    case ActionStatus::FRAME_SWITCH_FAILED: return "frame_switch_failed";
    case ActionStatus::TAB_SWITCH_FAILED: return "tab_switch_failed";
    case ActionStatus::DIALOG_NOT_HANDLED: return "dialog_not_handled";
    case ActionStatus::INVALID_SELECTOR: return "invalid_selector";
    case ActionStatus::INVALID_URL: return "invalid_url";
    case ActionStatus::INVALID_PARAMETER: return "invalid_parameter";
    case ActionStatus::INTERNAL_ERROR: return "internal_error";
    case ActionStatus::TIMEOUT: return "timeout";
    case ActionStatus::NETWORK_TIMEOUT: return "network_timeout";
    case ActionStatus::WAIT_TIMEOUT: return "wait_timeout";
    case ActionStatus::VERIFICATION_TIMEOUT: return "verification_timeout";
    default: return "unknown";
  }
}

// Human-readable message for ActionStatus
inline const char* ActionStatusToMessage(ActionStatus status) {
  switch (status) {
    case ActionStatus::OK: return "Action completed successfully";
    case ActionStatus::BROWSER_NOT_FOUND: return "Browser context not found";
    case ActionStatus::BROWSER_NOT_READY: return "Browser is not ready";
    case ActionStatus::CONTEXT_NOT_FOUND: return "Context not found";
    case ActionStatus::NAVIGATION_FAILED: return "Navigation failed";
    case ActionStatus::NAVIGATION_TIMEOUT: return "Navigation timed out";
    case ActionStatus::PAGE_LOAD_ERROR: return "Page failed to load";
    case ActionStatus::REDIRECT_DETECTED: return "Page redirected";
    case ActionStatus::CAPTCHA_DETECTED: return "CAPTCHA detected on page";
    case ActionStatus::FIREWALL_DETECTED: return "Web firewall/bot protection detected";
    case ActionStatus::ELEMENT_NOT_FOUND: return "Element not found";
    case ActionStatus::ELEMENT_NOT_VISIBLE: return "Element is not visible";
    case ActionStatus::ELEMENT_NOT_INTERACTABLE: return "Element cannot be interacted with";
    case ActionStatus::ELEMENT_STALE: return "Element is no longer in the page";
    case ActionStatus::MULTIPLE_ELEMENTS: return "Multiple elements matched selector";
    case ActionStatus::CLICK_FAILED: return "Click action failed";
    case ActionStatus::CLICK_INTERCEPTED: return "Click intercepted by another element";
    case ActionStatus::TYPE_FAILED: return "Type action failed";
    case ActionStatus::TYPE_PARTIAL: return "Only partial text was entered";
    case ActionStatus::SCROLL_FAILED: return "Scroll action failed";
    case ActionStatus::FOCUS_FAILED: return "Focus action failed";
    case ActionStatus::BLUR_FAILED: return "Blur action failed";
    case ActionStatus::CLEAR_FAILED: return "Clear input action failed";
    case ActionStatus::PICK_FAILED: return "Dropdown selection failed";
    case ActionStatus::OPTION_NOT_FOUND: return "Option not found in dropdown";
    case ActionStatus::UPLOAD_FAILED: return "File upload failed";
    case ActionStatus::FRAME_SWITCH_FAILED: return "Failed to switch to frame";
    case ActionStatus::TAB_SWITCH_FAILED: return "Failed to switch to tab";
    case ActionStatus::DIALOG_NOT_HANDLED: return "Dialog was not handled";
    case ActionStatus::INVALID_SELECTOR: return "Invalid selector syntax";
    case ActionStatus::INVALID_URL: return "Invalid URL";
    case ActionStatus::INVALID_PARAMETER: return "Invalid parameter value";
    case ActionStatus::INTERNAL_ERROR: return "Internal error";
    case ActionStatus::TIMEOUT: return "Operation timed out";
    case ActionStatus::NETWORK_TIMEOUT: return "Network did not become idle in time";
    case ActionStatus::WAIT_TIMEOUT: return "Wait condition not met in time";
    case ActionStatus::VERIFICATION_TIMEOUT: return "Action verification timed out";
    default: return "Unknown error";
  }
}

// Structured result for browser actions
// Provides success/failure status plus detailed information
struct ActionResult {
  bool success;               // True if action completed successfully
  ActionStatus status;        // Detailed status code
  std::string message;        // Human-readable message

  // Optional additional fields for specific errors
  std::string selector;       // For element errors: the selector that failed
  std::string url;            // For navigation errors: the URL involved
  std::string error_code;     // For HTTP/network errors: the error code
  int http_status;            // For navigation: HTTP status code
  int element_count;          // For multiple_elements: how many matched

  // Convenience constructors
  ActionResult() : success(false), status(ActionStatus::UNKNOWN), http_status(0), element_count(0) {}

  // Create a success result
  static ActionResult Success() {
    ActionResult r;
    r.success = true;
    r.status = ActionStatus::OK;
    r.message = ActionStatusToMessage(ActionStatus::OK);
    return r;
  }

  // Create a success result with custom message
  static ActionResult Success(const std::string& msg) {
    ActionResult r;
    r.success = true;
    r.status = ActionStatus::OK;
    r.message = msg;
    return r;
  }

  // Create a failure result
  static ActionResult Failure(ActionStatus status, const std::string& msg = "") {
    ActionResult r;
    r.success = false;
    r.status = status;
    r.message = msg.empty() ? ActionStatusToMessage(status) : msg;
    return r;
  }

  // Create element not found error
  static ActionResult ElementNotFound(const std::string& selector) {
    ActionResult r;
    r.success = false;
    r.status = ActionStatus::ELEMENT_NOT_FOUND;
    r.message = "Element not found: " + selector;
    r.selector = selector;
    return r;
  }

  // Create element not visible error
  static ActionResult ElementNotVisible(const std::string& selector) {
    ActionResult r;
    r.success = false;
    r.status = ActionStatus::ELEMENT_NOT_VISIBLE;
    r.message = "Element not visible: " + selector;
    r.selector = selector;
    return r;
  }

  // Create navigation failed error
  static ActionResult NavigationFailed(const std::string& url, const std::string& error = "") {
    ActionResult r;
    r.success = false;
    r.status = ActionStatus::NAVIGATION_FAILED;
    r.message = "Navigation failed: " + url + (error.empty() ? "" : " - " + error);
    r.url = url;
    r.error_code = error;
    return r;
  }

  // Create navigation timeout error
  static ActionResult NavigationTimeout(const std::string& url, int timeout_ms) {
    ActionResult r;
    r.success = false;
    r.status = ActionStatus::NAVIGATION_TIMEOUT;
    r.message = "Navigation timed out after " + std::to_string(timeout_ms) + "ms: " + url;
    r.url = url;
    return r;
  }

  // Create page load error with HTTP status
  static ActionResult PageLoadError(const std::string& url, int http_status, const std::string& error = "") {
    ActionResult r;
    r.success = false;
    r.status = ActionStatus::PAGE_LOAD_ERROR;
    r.message = "Page load error (" + std::to_string(http_status) + "): " + url;
    r.url = url;
    r.http_status = http_status;
    r.error_code = error;
    return r;
  }

  // Create browser not found error
  static ActionResult BrowserNotFound(const std::string& context_id) {
    ActionResult r;
    r.success = false;
    r.status = ActionStatus::BROWSER_NOT_FOUND;
    r.message = "Browser not found for context: " + context_id;
    return r;
  }

  // Create captcha detected result
  static ActionResult CaptchaDetected(const std::string& url) {
    ActionResult r;
    r.success = false;
    r.status = ActionStatus::CAPTCHA_DETECTED;
    r.message = "CAPTCHA detected on page: " + url;
    r.url = url;
    return r;
  }

  // Create redirect detected result
  static ActionResult RedirectDetected(const std::string& original_url, const std::string& final_url) {
    ActionResult r;
    r.success = false;
    r.status = ActionStatus::REDIRECT_DETECTED;
    r.message = "Page redirected from " + original_url + " to " + final_url;
    r.url = final_url;
    return r;
  }

  // Create firewall detected result
  static ActionResult FirewallDetected(const std::string& url, const std::string& provider,
                                        const std::string& challenge_type = "") {
    ActionResult r;
    r.success = false;
    r.status = ActionStatus::FIREWALL_DETECTED;
    std::string msg = "Web firewall detected: " + provider;
    if (!challenge_type.empty()) {
      msg += " (" + challenge_type + ")";
    }
    msg += " on " + url;
    r.message = msg;
    r.url = url;
    r.error_code = provider;  // Store provider in error_code field
    return r;
  }

  // Create click intercepted error
  static ActionResult ClickIntercepted(const std::string& target_selector,
                                       const std::string& intercepting_selector) {
    ActionResult r;
    r.success = false;
    r.status = ActionStatus::CLICK_INTERCEPTED;
    r.message = "Click on " + target_selector + " would be intercepted by " + intercepting_selector;
    r.selector = target_selector;
    r.error_code = intercepting_selector;  // Store intercepting element
    return r;
  }

  // Create verification timeout result (success with warning - action likely succeeded)
  static ActionResult VerificationTimeout(const std::string& action, const std::string& selector) {
    ActionResult r;
    r.success = true;  // Action likely succeeded, just couldn't verify
    r.status = ActionStatus::VERIFICATION_TIMEOUT;
    r.message = action + " executed but verification timed out for: " + selector;
    r.selector = selector;
    return r;
  }

  // Create pick failed error
  static ActionResult PickFailed(const std::string& selector,
                                  const std::string& expected_value,
                                  const std::string& actual_value) {
    ActionResult r;
    r.success = false;
    r.status = ActionStatus::PICK_FAILED;
    r.message = "Selection failed: expected '" + expected_value + "' but got '" + actual_value + "'";
    r.selector = selector;
    r.error_code = actual_value;  // Store actual value
    return r;
  }

  // Create option not found error
  static ActionResult OptionNotFound(const std::string& selector, const std::string& option_value) {
    ActionResult r;
    r.success = false;
    r.status = ActionStatus::OPTION_NOT_FOUND;
    r.message = "Option not found: '" + option_value + "' in " + selector;
    r.selector = selector;
    r.error_code = option_value;
    return r;
  }

  // Create type partial result (some text entered)
  static ActionResult TypePartial(const std::string& selector,
                                   const std::string& expected,
                                   const std::string& actual) {
    ActionResult r;
    r.success = false;
    r.status = ActionStatus::TYPE_PARTIAL;
    r.message = "Partial text entered: expected '" + expected + "' but got '" + actual + "'";
    r.selector = selector;
    r.error_code = actual;  // Store actual value
    return r;
  }

  // Create element not interactable error
  static ActionResult ElementNotInteractable(const std::string& selector, const std::string& reason = "") {
    ActionResult r;
    r.success = false;
    r.status = ActionStatus::ELEMENT_NOT_INTERACTABLE;
    r.message = "Element not interactable: " + selector + (reason.empty() ? "" : " - " + reason);
    r.selector = selector;
    if (!reason.empty()) {
      r.error_code = reason;
    }
    return r;
  }

  // Create upload failed error
  static ActionResult UploadFailed(const std::string& selector, const std::string& reason = "") {
    ActionResult r;
    r.success = false;
    r.status = ActionStatus::UPLOAD_FAILED;
    r.message = "File upload failed: " + selector + (reason.empty() ? "" : " - " + reason);
    r.selector = selector;
    return r;
  }

  // Create frame switch failed error
  static ActionResult FrameSwitchFailed(const std::string& frame_id, const std::string& reason = "") {
    ActionResult r;
    r.success = false;
    r.status = ActionStatus::FRAME_SWITCH_FAILED;
    r.message = "Failed to switch to frame: " + frame_id + (reason.empty() ? "" : " - " + reason);
    r.selector = frame_id;
    return r;
  }

  // Create tab switch failed error
  static ActionResult TabSwitchFailed(const std::string& tab_id, const std::string& reason = "") {
    ActionResult r;
    r.success = false;
    r.status = ActionStatus::TAB_SWITCH_FAILED;
    r.message = "Failed to switch to tab: " + tab_id + (reason.empty() ? "" : " - " + reason);
    r.error_code = tab_id;
    return r;
  }

  // Create dialog not handled error
  static ActionResult DialogNotHandled(const std::string& dialog_type, const std::string& reason = "") {
    ActionResult r;
    r.success = false;
    r.status = ActionStatus::DIALOG_NOT_HANDLED;
    r.message = "Dialog not handled: " + dialog_type + (reason.empty() ? "" : " - " + reason);
    r.error_code = dialog_type;
    return r;
  }

  // Convert to JSON string for IPC response
  std::string ToJSON() const {
    std::ostringstream json;
    json << "{\"success\":" << (success ? "true" : "false");
    json << ",\"status\":\"" << ActionStatusToCode(status) << "\"";
    json << ",\"message\":\"" << EscapeJSON(message) << "\"";

    // Add optional fields if present
    if (!selector.empty()) {
      json << ",\"selector\":\"" << EscapeJSON(selector) << "\"";
    }
    if (!url.empty()) {
      json << ",\"url\":\"" << EscapeJSON(url) << "\"";
    }
    if (!error_code.empty()) {
      json << ",\"error_code\":\"" << EscapeJSON(error_code) << "\"";
    }
    if (http_status != 0) {
      json << ",\"http_status\":" << http_status;
    }
    if (element_count > 0) {
      json << ",\"element_count\":" << element_count;
    }

    json << "}";
    return json.str();
  }

private:
  // Helper to escape JSON strings
  static std::string EscapeJSON(const std::string& str) {
    std::string escaped;
    escaped.reserve(str.length());
    for (char c : str) {
      switch (c) {
        case '"':  escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
          if (c < 32) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
            escaped += buf;
          } else {
            escaped += c;
          }
      }
    }
    return escaped;
  }
};
