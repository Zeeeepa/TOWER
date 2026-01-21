#pragma once

#include <string>
#include <functional>
#include <chrono>
#include <vector>
#include <mutex>
#include "include/cef_browser.h"
#include "action_result.h"

// Forward declarations
class OwlClient;
class OwlRenderTracker;
struct ElementRenderInfo;

namespace owl {

// Configuration for action verification
struct VerificationConfig {
  VerificationLevel level = VerificationLevel::STANDARD;
  int timeout_ms = 100;               // Max time for verification
  bool allow_partial_match = false;   // For Type: accept partial text
};

// Pre-action check result
struct PreCheckResult {
  bool can_proceed;
  ActionStatus status;
  std::string message;

  // Element info (if found)
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  bool is_visible = false;
  bool is_interactable = false;
  std::string intercepting_selector;  // If another element would receive events

  static PreCheckResult OK(int x, int y, int w, int h) {
    PreCheckResult r;
    r.can_proceed = true;
    r.status = ActionStatus::OK;
    r.x = x;
    r.y = y;
    r.width = w;
    r.height = h;
    r.is_visible = true;
    r.is_interactable = true;
    return r;
  }

  static PreCheckResult Fail(ActionStatus status, const std::string& msg) {
    PreCheckResult r;
    r.can_proceed = false;
    r.status = status;
    r.message = msg;
    return r;
  }
};

// Post-action verification result
struct PostCheckResult {
  bool verified;
  ActionStatus status;
  std::string message;
  std::string actual_value;  // For type/pick verification
};

// Action Verifier - coordinates pre/post action verification
// Singleton pattern for global access
class ActionVerifier {
public:
  static ActionVerifier* GetInstance();

  // Pre-action checks
  // Returns PreCheckResult indicating if action can proceed

  // Check if element can be clicked
  // - Verifies element exists
  // - Verifies element is visible
  // - For STANDARD+: checks if element is covered by another element
  PreCheckResult CheckClickTarget(CefRefPtr<CefBrowser> browser,
                                   const std::string& context_id,
                                   const std::string& selector,
                                   VerificationLevel level);

  // Check if element can receive text input
  // - All checks from CheckClickTarget
  // - Verifies element is an input, textarea, or contenteditable
  PreCheckResult CheckTypeTarget(CefRefPtr<CefBrowser> browser,
                                  const std::string& context_id,
                                  const std::string& selector,
                                  VerificationLevel level);

  // Post-action verification
  // Returns PostCheckResult indicating if action succeeded

  // Verify click had an effect
  // - Checks if focus changed (for focusable elements)
  // - Checks if navigation started (for links)
  // - Checks for DOM mutation
  PostCheckResult VerifyClickEffect(CefRefPtr<CefBrowser> browser,
                                     const std::string& context_id,
                                     const std::string& selector,
                                     const std::string& pre_focus_selector,
                                     int timeout_ms = 10);

  // Verify text was entered correctly
  // - Compares actual input value to expected
  // - Returns TYPE_PARTIAL if partial match detected
  PostCheckResult VerifyTypeValue(CefRefPtr<CefBrowser> browser,
                                   const std::string& context_id,
                                   const std::string& selector,
                                   const std::string& expected_value,
                                   int timeout_ms = 200);

  // Verify dropdown selection
  // - Checks selected value/text matches expected
  PostCheckResult VerifySelectValue(CefRefPtr<CefBrowser> browser,
                                     const std::string& context_id,
                                     const std::string& selector,
                                     const std::string& expected_value,
                                     int timeout_ms = 100);

  // Verify focus state
  // - Checks if element is/is not the active element
  PostCheckResult VerifyFocus(CefRefPtr<CefBrowser> browser,
                               const std::string& context_id,
                               const std::string& selector,
                               bool should_be_focused,
                               int timeout_ms = 50);

  // Utility methods

  // Get element at specific screen coordinates (hit test)
  // Returns selector of element at point, empty string if none
  std::string GetElementAtPoint(CefRefPtr<CefBrowser> browser,
                                 const std::string& context_id,
                                 int x, int y);

  // Get currently focused element
  // Returns selector of active element, empty string if none or body
  std::string GetActiveElement(CefRefPtr<CefBrowser> browser,
                                const std::string& context_id);

  // Check if element is input-capable (input, textarea, contenteditable)
  bool IsInputElement(const std::string& tag,
                      const std::string& content_editable,
                      const std::string& role);

private:
  ActionVerifier() = default;
  ~ActionVerifier() = default;

  // Singleton instance
  static ActionVerifier* instance_;
  static std::mutex instance_mutex_;

  // IPC helpers - send message and wait for response
  bool SendVerificationRequest(CefRefPtr<CefBrowser> browser,
                               const std::string& message_name,
                               const std::string& context_id,
                               const std::vector<std::string>& args);

  bool WaitForVerificationResponse(OwlClient* client,
                                    const std::string& context_id,
                                    int timeout_ms);
};

}  // namespace owl
