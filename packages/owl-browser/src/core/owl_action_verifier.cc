#include "owl_action_verifier.h"
#include "owl_client.h"
#include "owl_render_tracker.h"
#include "logger.h"
#include <thread>
#include <algorithm>

namespace owl {

// Singleton instance
ActionVerifier* ActionVerifier::instance_ = nullptr;
std::mutex ActionVerifier::instance_mutex_;

ActionVerifier* ActionVerifier::GetInstance() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (!instance_) {
    instance_ = new ActionVerifier();
  }
  return instance_;
}

PreCheckResult ActionVerifier::CheckClickTarget(
    CefRefPtr<CefBrowser> browser,
    const std::string& context_id,
    const std::string& selector,
    VerificationLevel level) {

  // For NONE level, skip all checks
  if (level == VerificationLevel::NONE) {
    return PreCheckResult::OK(0, 0, 0, 0);
  }

  // Get element bounds from RenderTracker
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  ElementRenderInfo info;

  if (!tracker->GetElementBounds(context_id, selector, info)) {
    LOG_DEBUG("ActionVerifier", "CheckClickTarget: Element not found: " + selector);
    return PreCheckResult::Fail(ActionStatus::ELEMENT_NOT_FOUND,
                                "Element not found: " + selector);
  }

  // Check visibility
  if (!info.visible) {
    LOG_DEBUG("ActionVerifier", "CheckClickTarget: Element not visible: " + selector);
    return PreCheckResult::Fail(ActionStatus::ELEMENT_NOT_VISIBLE,
                                "Element not visible: " + selector);
  }

  // Check element has dimensions
  if (info.width <= 0 || info.height <= 0) {
    LOG_DEBUG("ActionVerifier", "CheckClickTarget: Element has no dimensions: " + selector);
    return PreCheckResult::Fail(ActionStatus::ELEMENT_NOT_VISIBLE,
                                "Element has no visible dimensions: " + selector);
  }

  // For STANDARD and STRICT, check if element is intercepted by another
  if (level >= VerificationLevel::STANDARD) {
    int center_x = info.x + info.width / 2;
    int center_y = info.y + info.height / 2;

    std::string element_at_point = GetElementAtPoint(browser, context_id, center_x, center_y);

    // Check if the element at point matches our target
    if (!element_at_point.empty()) {
      // Normalize selectors for comparison
      bool is_match = (element_at_point == selector);

      // Also check if element_at_point is contained in selector (e.g., "#btn" matches "#btn span")
      if (!is_match && selector.find(element_at_point) != std::string::npos) {
        is_match = true;
      }

      // Check if selector is contained in element_at_point
      if (!is_match && element_at_point.find(selector) != std::string::npos) {
        is_match = true;
      }

      // Check by ID match
      if (!is_match && !info.id.empty()) {
        if (element_at_point == "#" + info.id ||
            element_at_point.find("#" + info.id) != std::string::npos) {
          is_match = true;
        }
      }

      if (!is_match) {
        // Check z-index to determine if it's truly intercepted
        ElementRenderInfo point_info;
        if (tracker->GetElementBounds(context_id, element_at_point, point_info)) {
          // If the element at point has higher z-index and completely overlaps,
          // it's likely intercepting
          if (point_info.z_index > info.z_index) {
            LOG_WARN("ActionVerifier", "Click may be intercepted by: " + element_at_point +
                     " (z-index: " + std::to_string(point_info.z_index) +
                     " vs " + std::to_string(info.z_index) + ")");

            // For STRICT mode, fail immediately
            if (level == VerificationLevel::STRICT) {
              PreCheckResult result = PreCheckResult::Fail(
                  ActionStatus::CLICK_INTERCEPTED,
                  "Click would be intercepted by: " + element_at_point);
              result.intercepting_selector = element_at_point;
              return result;
            }
            // For STANDARD, just log warning but allow click
          }
        }
      }
    }
  }

  // All checks passed
  PreCheckResult result = PreCheckResult::OK(info.x, info.y, info.width, info.height);
  result.is_visible = info.visible;
  result.is_interactable = true;
  return result;
}

PreCheckResult ActionVerifier::CheckTypeTarget(
    CefRefPtr<CefBrowser> browser,
    const std::string& context_id,
    const std::string& selector,
    VerificationLevel level) {

  // First do click target check
  PreCheckResult click_check = CheckClickTarget(browser, context_id, selector, level);
  if (!click_check.can_proceed) {
    return click_check;
  }

  // Skip additional checks for NONE level
  if (level == VerificationLevel::NONE) {
    return click_check;
  }

  // Additionally verify it's an input-capable element
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  ElementRenderInfo info;

  if (tracker->GetElementBounds(context_id, selector, info)) {
    if (!IsInputElement(info.tag, "", info.role)) {
      // Check if it might be contenteditable
      // Note: We can't easily check contenteditable from cached info,
      // so we'll be lenient here and let the actual action fail if needed
      LOG_DEBUG("ActionVerifier", "CheckTypeTarget: Element may not be input-capable: " +
                selector + " (tag=" + info.tag + ")");

      // Only fail in STRICT mode
      if (level == VerificationLevel::STRICT) {
        return PreCheckResult::Fail(ActionStatus::ELEMENT_NOT_INTERACTABLE,
                                    "Element is not a text input: " + selector);
      }
    }
  }

  return click_check;
}

PostCheckResult ActionVerifier::VerifyClickEffect(
    CefRefPtr<CefBrowser> browser,
    const std::string& context_id,
    const std::string& selector,
    const std::string& pre_focus_selector,
    int timeout_ms) {

  // Brief delay for DOM to update
  std::this_thread::sleep_for(std::chrono::milliseconds(2));

  // Quick check - did focus change?
  std::string new_focus = GetActiveElement(browser, context_id);

  // Success if focus changed to target element
  if (!new_focus.empty() && !selector.empty()) {
    // Check exact match or contains match
    if (new_focus == selector ||
        new_focus.find(selector) != std::string::npos ||
        selector.find(new_focus) != std::string::npos) {
      return {true, ActionStatus::OK, "Click verified: focus changed to target", ""};
    }
  }

  // Success if focus changed at all (click had some effect)
  if (new_focus != pre_focus_selector && !new_focus.empty()) {
    return {true, ActionStatus::OK, "Click verified: focus changed", ""};
  }

  // Focus didn't change - this might be OK for non-focusable elements like buttons
  // Return success but with informational message
  return {true, ActionStatus::OK, "Click executed (no focus change detected)", ""};
}

PostCheckResult ActionVerifier::VerifyTypeValue(
    CefRefPtr<CefBrowser> browser,
    const std::string& context_id,
    const std::string& selector,
    const std::string& expected_value,
    int timeout_ms) {

  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  if (!client_base) {
    LOG_WARN("ActionVerifier", "VerifyTypeValue: Could not get client");
    return {false, ActionStatus::INTERNAL_ERROR, "Could not get client for verification", ""};
  }

  OwlClient* client = static_cast<OwlClient*>(client_base.get());

  // Reset and request verification
  client->ResetVerification(context_id);

  // Send verification request to renderer
  CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("verify_input_value");
  CefRefPtr<CefListValue> args = msg->GetArgumentList();
  args->SetString(0, context_id);
  args->SetString(1, selector);
  args->SetString(2, expected_value);
  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, msg);

  // Wait for response
  if (!client->WaitForVerification(context_id, timeout_ms)) {
    LOG_DEBUG("ActionVerifier", "VerifyTypeValue: Verification timed out for " + selector);
    return {true, ActionStatus::VERIFICATION_TIMEOUT,
            "Type verification timed out", ""};
  }

  // Get result
  OwlClient::VerificationResult result = client->GetVerificationResult(context_id);

  if (result.success) {
    return {true, ActionStatus::OK, "Type verified successfully", result.actual_value};
  }

  // Check for partial match (text started to be entered)
  if (!result.actual_value.empty() && !expected_value.empty()) {
    // Check if actual is a prefix of expected (partial entry)
    if (expected_value.find(result.actual_value) == 0) {
      return {false, ActionStatus::TYPE_PARTIAL,
              "Partial text entered", result.actual_value};
    }
  }

  return {false, ActionStatus::TYPE_FAILED,
          result.error_message.empty() ? "Text mismatch" : result.error_message,
          result.actual_value};
}

PostCheckResult ActionVerifier::VerifySelectValue(
    CefRefPtr<CefBrowser> browser,
    const std::string& context_id,
    const std::string& selector,
    const std::string& expected_value,
    int timeout_ms) {

  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  if (!client_base) {
    return {false, ActionStatus::INTERNAL_ERROR, "Could not get client", ""};
  }

  OwlClient* client = static_cast<OwlClient*>(client_base.get());

  // Reset and request verification
  client->ResetVerification(context_id);

  // Send verification request
  CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("verify_select_value");
  CefRefPtr<CefListValue> args = msg->GetArgumentList();
  args->SetString(0, context_id);
  args->SetString(1, selector);
  args->SetString(2, expected_value);
  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, msg);

  // Wait for response
  if (!client->WaitForVerification(context_id, timeout_ms)) {
    return {true, ActionStatus::VERIFICATION_TIMEOUT,
            "Select verification timed out", ""};
  }

  OwlClient::VerificationResult result = client->GetVerificationResult(context_id);

  if (result.success) {
    return {true, ActionStatus::OK, "Selection verified", result.actual_value};
  }

  return {false, ActionStatus::PICK_FAILED,
          "Selection mismatch", result.actual_value};
}

PostCheckResult ActionVerifier::VerifyFocus(
    CefRefPtr<CefBrowser> browser,
    const std::string& context_id,
    const std::string& selector,
    bool should_be_focused,
    int timeout_ms) {

  std::string active = GetActiveElement(browser, context_id);

  // Check if element matches
  bool is_focused = false;
  if (!active.empty() && !selector.empty()) {
    is_focused = (active == selector ||
                  active.find(selector) != std::string::npos ||
                  selector.find(active) != std::string::npos);
  }

  if (is_focused == should_be_focused) {
    return {true, ActionStatus::OK,
            should_be_focused ? "Element is focused" : "Element is not focused",
            active};
  }

  return {false,
          should_be_focused ? ActionStatus::FOCUS_FAILED : ActionStatus::BLUR_FAILED,
          should_be_focused ? "Element did not receive focus" : "Element still has focus",
          active};
}

std::string ActionVerifier::GetElementAtPoint(
    CefRefPtr<CefBrowser> browser,
    const std::string& context_id,
    int x, int y) {

  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  if (!client_base) {
    return "";
  }

  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  client->ResetVerification(context_id);

  // Send request to renderer
  CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("get_element_at_point");
  CefRefPtr<CefListValue> args = msg->GetArgumentList();
  args->SetString(0, context_id);
  args->SetInt(1, x);
  args->SetInt(2, y);
  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, msg);

  // Wait for response (short timeout)
  if (client->WaitForVerification(context_id, 50)) {
    OwlClient::VerificationResult result = client->GetVerificationResult(context_id);
    return result.active_element_selector;
  }

  return "";
}

std::string ActionVerifier::GetActiveElement(
    CefRefPtr<CefBrowser> browser,
    const std::string& context_id) {

  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  if (!client_base) {
    return "";
  }

  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  client->ResetVerification(context_id);

  // Send request to renderer
  CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("get_active_element");
  CefRefPtr<CefListValue> args = msg->GetArgumentList();
  args->SetString(0, context_id);
  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, msg);

  // Wait for response (short timeout)
  if (client->WaitForVerification(context_id, 50)) {
    OwlClient::VerificationResult result = client->GetVerificationResult(context_id);
    return result.active_element_selector;
  }

  return "";
}

bool ActionVerifier::IsInputElement(const std::string& tag,
                                     const std::string& content_editable,
                                     const std::string& role) {
  // Normalize tag to uppercase
  std::string upper_tag = tag;
  std::transform(upper_tag.begin(), upper_tag.end(), upper_tag.begin(), ::toupper);

  // Standard input elements
  if (upper_tag == "INPUT" || upper_tag == "TEXTAREA" || upper_tag == "SELECT") {
    return true;
  }

  // Contenteditable
  if (content_editable == "true" || content_editable == "plaintext-only") {
    return true;
  }

  // ARIA textbox role
  if (role == "textbox" || role == "combobox" || role == "searchbox") {
    return true;
  }

  return false;
}

bool ActionVerifier::SendVerificationRequest(
    CefRefPtr<CefBrowser> browser,
    const std::string& message_name,
    const std::string& context_id,
    const std::vector<std::string>& args) {

  CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create(message_name);
  CefRefPtr<CefListValue> msg_args = msg->GetArgumentList();

  msg_args->SetString(0, context_id);
  for (size_t i = 0; i < args.size(); i++) {
    msg_args->SetString(i + 1, args[i]);
  }

  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, msg);
  return true;
}

bool ActionVerifier::WaitForVerificationResponse(
    OwlClient* client,
    const std::string& context_id,
    int timeout_ms) {

  return client->WaitForVerification(context_id, timeout_ms);
}

}  // namespace owl
