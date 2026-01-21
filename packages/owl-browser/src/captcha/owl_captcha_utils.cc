#include "owl_captcha_utils.h"
#include "owl_client.h"
#include "owl_semantic_matcher.h"
#include "owl_render_tracker.h"
#include "owl_browser_manager.h"
#include "owl_native_screenshot.h"
#include "logger.h"
#include "include/cef_app.h"
#include <thread>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <condition_variable>

// DOM Visitor to get element bounds synchronously from C++ DOM access
class ElementBoundsVisitor : public CefDOMVisitor {
public:
  ElementBoundsVisitor(const std::string& selector)
    : found_(false), x_(0), y_(0), width_(0), height_(0), selector_(selector), complete_(false) {}

  void Visit(CefRefPtr<CefDOMDocument> document) override {
    if (!document) {
      SignalComplete();
      return;
    }

    // Query the element using CSS selector
    // For selectors like "#captchaImage", extract the ID
    std::string id = selector_;
    if (id.length() > 0 && id[0] == '#') {
      id = id.substr(1);
    }

    CefRefPtr<CefDOMNode> element = document->GetElementById(id);

    if (element && element->IsElement()) {
      // Get bounding rectangle from DOM
      CefRect rect = element->GetElementBounds();

      x_ = rect.x;
      y_ = rect.y;
      width_ = rect.width;
      height_ = rect.height;
      found_ = true;

      LOG_DEBUG("ElementBoundsVisitor", "Found element bounds from DOM: x=" + std::to_string(x_) +
               " y=" + std::to_string(y_) + " w=" + std::to_string(width_) +
               " h=" + std::to_string(height_));
    } else {
      LOG_WARN("ElementBoundsVisitor", "Element not found in DOM: " + selector_);
    }

    SignalComplete();
  }

  void SignalComplete() {
    std::lock_guard<std::mutex> lock(mutex_);
    complete_ = true;
    cv_.notify_one();
  }

  bool WaitForCompletion(int timeout_ms = 1000) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                       [this] { return complete_; });
  }

  bool found_;
  int x_, y_, width_, height_;

private:
  std::string selector_;
  bool complete_;
  std::mutex mutex_;
  std::condition_variable cv_;

  IMPLEMENT_REFCOUNTING(ElementBoundsVisitor);
};

bool OwlCaptchaUtils::ScrollIntoView(CefRefPtr<CefBrowser> browser,
                                      const std::string& selector,
                                      const std::string& align) {
  LOG_DEBUG("CaptchaUtils", "Scrolling element into view: " + selector);

  if (!browser || !browser->GetMainFrame()) {
    LOG_ERROR("CaptchaUtils", "Invalid browser or frame");
    return false;
  }

  std::string scroll_script = R"(
    (function() {
      var elem = document.querySelector(')" + selector + R"(');
      if (!elem) return false;

      elem.scrollIntoView({
        behavior: 'smooth',
        block: ')" + align + R"(',
        inline: 'center'
      });

      return true;
    })();
  )";

  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  frame->ExecuteJavaScript(scroll_script, frame->GetURL(), 0);

  // Wait for scroll to complete
  Wait(500);

  LOG_DEBUG("CaptchaUtils", "Scroll complete");
  return true;
}

std::vector<uint8_t> OwlCaptchaUtils::CaptureElementScreenshot(
    CefRefPtr<CefBrowser> browser,
    const std::string& context_id,
    const std::string& selector) {

  LOG_DEBUG("CaptchaUtils", "Capturing element screenshot: " + selector + " (context: " + context_id + ")");

  std::vector<uint8_t> result;

  if (!browser || !browser->GetHost()) {
    LOG_ERROR("CaptchaUtils", "Invalid browser or host");
    return result;
  }

  // Get client first
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  if (!client_base) {
    LOG_ERROR("CaptchaUtils", "Failed to get browser client");
    return result;
  }

  OwlClient* client = static_cast<OwlClient*>(client_base.get());

  // Unfreeze cache so ScrollIntoView can update it with the new view
  client->UnfreezeFrameCache();
  LOG_DEBUG("CaptchaUtils", "Unfroze cache before scroll");

  // CRITICAL: Wait for element scanner to rescan DOM with new state
  LOG_DEBUG("CaptchaUtils", "Waiting for element scan to update with new DOM state");
  client->WaitForElementScan(browser, context_id, 2000);

  // Scroll element into view (will trigger paint and update cache)
  ScrollIntoView(browser, selector, "center");
  Wait(200);  // Wait for scroll animation and paint to complete

  // Get element bounds from semantic matcher (with rescanned DOM)
  std::string bounds_json = GetElementBounds(browser, context_id, selector);
  LOG_DEBUG("CaptchaUtils", "Element bounds: " + bounds_json);

  // Parse bounds from JSON (simple parsing)
  int x = 0, y = 0, width = 0, height = 0;
  bool bounds_valid = false;

  if (!bounds_json.empty() && bounds_json != "null") {
    // Simple JSON parsing for {"x":100,"y":200,"width":300,"height":150}
    size_t x_pos = bounds_json.find("\"x\":");
    size_t y_pos = bounds_json.find("\"y\":");
    size_t w_pos = bounds_json.find("\"width\":");
    size_t h_pos = bounds_json.find("\"height\":");

    if (x_pos != std::string::npos && y_pos != std::string::npos &&
        w_pos != std::string::npos && h_pos != std::string::npos) {
      // Parse integers without exceptions (CEF builds with -fno-exceptions)
      x = std::atoi(bounds_json.substr(x_pos + 4).c_str());
      y = std::atoi(bounds_json.substr(y_pos + 4).c_str());
      width = std::atoi(bounds_json.substr(w_pos + 8).c_str());
      height = std::atoi(bounds_json.substr(h_pos + 9).c_str());

      // Validate parsed values
      if (width > 0 && height > 0) {
        LOG_DEBUG("CaptchaUtils", "Cropping to element bounds: x=" + std::to_string(x) +
                 " y=" + std::to_string(y) + " w=" + std::to_string(width) +
                 " h=" + std::to_string(height));
        bounds_valid = true;
      } else {
        LOG_WARN("CaptchaUtils", "Invalid bounds values");
      }
    } else {
      LOG_WARN("CaptchaUtils", "Invalid bounds format");
    }
  } else {
    LOG_WARN("CaptchaUtils", "Element bounds not found");
  }

  // Capture screenshot using appropriate method based on mode
  if (bounds_valid) {
    // Check if we're in UI mode (windowed) or headless mode
    if (OwlBrowserManager::UsesRunMessageLoop()) {
      // UI MODE: Use native macOS screenshot (windowed rendering doesn't populate frame cache)
      LOG_DEBUG("CaptchaUtils", "UI mode detected - using native macOS screenshot for element capture");

      #ifdef BUILD_UI
      // CaptureNativeScreenshot expects grid_items for overlays, but we don't need overlays for text captcha
      // Pass empty vector for grid_items
      std::vector<ElementRenderInfo> empty_grid;
      result = CaptureNativeScreenshot(browser, x, y, width, height, empty_grid, x, y);

      if (result.empty()) {
        LOG_ERROR("CaptchaUtils", "Native screenshot capture failed");
      } else {
        LOG_DEBUG("CaptchaUtils", "Native screenshot element capture successful");
      }
      #else
      LOG_ERROR("CaptchaUtils", "UI mode screenshot not available in headless build");
      #endif
    } else {
      // HEADLESS MODE: Use frame cache (existing approach)
      LOG_DEBUG("CaptchaUtils", "Headless mode - using frame cache for element capture");
      bool success = client->GetCroppedScreenshotFromCache(&result, x, y, width, height);
      if (!success) {
        LOG_ERROR("CaptchaUtils", "Failed to get cropped screenshot from cache");
      } else {
        // Freeze cache again after successful capture to preserve this exact state for solving
        client->FreezeFrameCache();
        LOG_DEBUG("CaptchaUtils", "Froze cache after element capture");
      }
    }
  } else {
    LOG_ERROR("CaptchaUtils", "Cannot capture screenshot without valid bounds");
  }

  if (result.empty()) {
    LOG_ERROR("CaptchaUtils", "Screenshot capture failed - empty result");
  }

  return result;
}

std::string OwlCaptchaUtils::GetElementBounds(CefRefPtr<CefBrowser> browser,
                                               const std::string& context_id,
                                               const std::string& selector) {
  LOG_DEBUG("CaptchaUtils", "Getting element bounds: " + selector + " (context: " + context_id + ")");

  if (!browser || !browser->GetMainFrame()) {
    LOG_ERROR("CaptchaUtils", "Invalid browser or frame");
    return "null";
  }

  // Use semantic matcher to get element bounds from tracked elements
  OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
  if (!matcher) {
    LOG_ERROR("CaptchaUtils", "Semantic matcher not available");
    return "null";
  }

  // Get all elements for this context (already rescanned by WaitForElementScan)
  std::vector<ElementSemantics> all_elements = matcher->GetAllElements(context_id);
  LOG_DEBUG("CaptchaUtils", "Found " + std::to_string(all_elements.size()) + " total elements in context");

  // Find element matching selector
  ElementSemantics* matched_elem = nullptr;
  for (auto& elem : all_elements) {
    // Try exact selector match first
    if (elem.selector == selector) {
      matched_elem = &elem;
      LOG_DEBUG("CaptchaUtils", "Matched by exact selector: " + elem.selector);
      break;
    }

    // Match by ID
    if (selector.find("#") == 0) {
      std::string id = selector.substr(1);
      // Check elem.id field
      if (elem.id == id) {
        matched_elem = &elem;
        LOG_DEBUG("CaptchaUtils", "Matched by ID field: " + elem.id + " (selector: " + elem.selector + ")");
        break;
      }
      // Also check if selector appears in elem.selector (e.g. "DIV#imageGrid@x,y")
      if (elem.selector.find(selector) != std::string::npos) {
        matched_elem = &elem;
        LOG_DEBUG("CaptchaUtils", "Matched by selector substring: " + elem.selector);
        break;
      }
    }
    // Match by class
    else if (selector.find(".") == 0) {
      std::string class_name = selector.substr(1);
      // Check if selector contains class in elem.selector
      if (elem.selector.find("." + class_name) != std::string::npos) {
        matched_elem = &elem;
        LOG_DEBUG("CaptchaUtils", "Matched by class in selector: " + elem.selector);
        break;
      }
    }
    // Match by tag
    else {
      if (elem.tag == selector) {
        matched_elem = &elem;
        LOG_DEBUG("CaptchaUtils", "Matched by tag: " + elem.tag);
        break;
      }
    }
  }

  if (!matched_elem) {
    LOG_ERROR("CaptchaUtils", "No element found matching selector: " + selector);
    return "null";
  }

  // Add small padding to ensure we don't crop edges
  const int padding = 10;
  int x = std::max(0, matched_elem->x - padding);
  int y = std::max(0, matched_elem->y - padding);
  int width = matched_elem->width + (padding * 2);
  int height = matched_elem->height + (padding * 2);

  std::ostringstream result;
  result << "{\"x\":" << x << ",\"y\":" << y
         << ",\"width\":" << width << ",\"height\":" << height << "}";

  LOG_DEBUG("CaptchaUtils", "Element bounds from semantic matcher: " + result.str());
  return result.str();
}

bool OwlCaptchaUtils::WaitForVisible(CefRefPtr<CefBrowser> browser,
                                      const std::string& selector,
                                      int timeout_ms) {
  LOG_DEBUG("CaptchaUtils", "Waiting for element to be visible: " + selector +
            " (timeout: " + std::to_string(timeout_ms) + "ms)");

  int elapsed = 0;
  int check_interval = 100;  // Check every 100ms

  while (elapsed < timeout_ms) {
    if (IsVisible(browser, selector)) {
      LOG_DEBUG("CaptchaUtils", "Element became visible after " +
                std::to_string(elapsed) + "ms");
      return true;
    }

    Wait(check_interval);
    elapsed += check_interval;
  }

  LOG_WARN("CaptchaUtils", "Timeout waiting for element to be visible");
  return false;
}

bool OwlCaptchaUtils::IsVisible(CefRefPtr<CefBrowser> browser,
                                 const std::string& selector) {
  std::string visibility_script = R"(
    (function() {
      var elem = document.querySelector(')" + selector + R"(');
      if (!elem) return false;

      var style = window.getComputedStyle(elem);
      if (style.display === 'none' || style.visibility === 'hidden' || style.opacity === '0') {
        return false;
      }

      var rect = elem.getBoundingClientRect();
      return rect.width > 0 && rect.height > 0;
    })();
  )";

  // Would execute and return result in production
  return true;  // Assume visible for now
}

bool OwlCaptchaUtils::ElementExists(CefRefPtr<CefBrowser> browser,
                                     const std::string& selector) {
  std::string exists_script = R"(
    (function() {
      return document.querySelector(')" + selector + R"(') !== null;
    })();
  )";

  // Would execute and return result
  return true;  // Assume exists for now
}

bool OwlCaptchaUtils::ClickElement(CefRefPtr<CefBrowser> browser,
                                    const std::string& context_id,
                                    const std::string& selector,
                                    int max_retries) {
  LOG_DEBUG("CaptchaUtils", "Clicking element (C++ native CEF mouse events): " + selector +
            " (max retries: " + std::to_string(max_retries) + ")");

  if (!browser || !browser->GetMainFrame()) {
    LOG_ERROR("CaptchaUtils", "Invalid browser or frame");
    return false;
  }

  // IMPORTANT: Handle comma-separated selectors (e.g., "#skipBtn, .btn-secondary")
  // The render tracker doesn't support CSS selector lists, so we need to try each selector individually
  std::vector<std::string> selectors;

  if (selector.find(',') != std::string::npos) {
    // Split by comma
    std::stringstream ss(selector);
    std::string item;
    while (std::getline(ss, item, ',')) {
      // Trim whitespace
      item.erase(0, item.find_first_not_of(" \t\n\r"));
      item.erase(item.find_last_not_of(" \t\n\r") + 1);
      if (!item.empty()) {
        selectors.push_back(item);
      }
    }
    LOG_DEBUG("CaptchaUtils", "Split selector into " + std::to_string(selectors.size()) + " parts");
  } else {
    selectors.push_back(selector);
  }

  // Try each selector until one succeeds
  for (const auto& single_selector : selectors) {
    LOG_DEBUG("CaptchaUtils", "Trying selector: " + single_selector);

    // Step 1: Trigger element scan to ensure element is tracked
    LOG_DEBUG("CaptchaUtils", "Triggering element scan for: " + single_selector);
    CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("scan_element");
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    args->SetString(0, context_id);
    args->SetString(1, single_selector);
    browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);

    // Wait for scan to complete
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count() < 500) {
      OwlBrowserManager::PumpMessageLoopIfNeeded();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Step 2: Get element bounds from render tracker
    OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
    ElementRenderInfo info;

    if (!tracker->GetElementBounds(context_id, single_selector, info)) {
      LOG_DEBUG("CaptchaUtils", "Selector '" + single_selector + "' not found, trying next...");
      continue;  // Try next selector
    }

    if (!info.visible) {
      LOG_WARN("CaptchaUtils", "Element is not visible (clicking anyway): " + single_selector);
      // Continue anyway - element might be off-screen but still clickable
    }

    LOG_DEBUG("CaptchaUtils", "Found element at (" + std::to_string(info.x) + "," +
              std::to_string(info.y) + ") " + std::to_string(info.width) + "x" +
              std::to_string(info.height));

    // Step 3: Send C++ native CEF mouse events
    int click_x = info.x + (info.width / 2);
    int click_y = info.y + (info.height / 2);

    LOG_DEBUG("CaptchaUtils", "Clicking at (" + std::to_string(click_x) + "," +
              std::to_string(click_y) + ") using native CEF mouse events");

    auto host = browser->GetHost();

    // Ensure browser has focus
    host->SetFocus(true);
    OwlBrowserManager::PumpMessageLoopIfNeeded();

    // Send mouse events (move, down, up)
    CefMouseEvent mouse_event;
    mouse_event.x = click_x;
    mouse_event.y = click_y;
    mouse_event.modifiers = 0;

    // Mouse move
    LOG_DEBUG("CaptchaUtils", "Sending mouse move");
    host->SendMouseMoveEvent(mouse_event, false);
    OwlBrowserManager::PumpMessageLoopIfNeeded();

    // Mouse down
    LOG_DEBUG("CaptchaUtils", "Sending mouse down");
    host->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 1);
    OwlBrowserManager::PumpMessageLoopIfNeeded();

    // Mouse up
    LOG_DEBUG("CaptchaUtils", "Sending mouse up");
    host->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 1);
    OwlBrowserManager::PumpMessageLoopIfNeeded();

    // Wait for click to be processed
    Wait(200);

    LOG_DEBUG("CaptchaUtils", "C++ native click executed successfully for: " + single_selector);
    return true;  // Success! Element found and clicked
  }

  // If we get here, none of the selectors worked
  LOG_ERROR("CaptchaUtils", "Failed to click element - none of the selectors matched: " + selector);
  return false;
}

bool OwlCaptchaUtils::TypeIntoElement(CefRefPtr<CefBrowser> browser,
                                       const std::string& context_id,
                                       const std::string& selector,
                                       const std::string& text) {
  LOG_DEBUG("CaptchaUtils", "Typing into element: " + selector +
            " (text length: " + std::to_string(text.length()) + ")");

  if (!browser || !browser->GetMainFrame()) {
    LOG_ERROR("CaptchaUtils", "Invalid browser or frame");
    return false;
  }

  // Step 1: Trigger element scan to ensure input element is tracked
  LOG_DEBUG("CaptchaUtils", "Triggering element scan for: " + selector);
  CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("scan_element");
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  args->SetString(0, context_id);
  args->SetString(1, selector);
  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);

  // Wait for scan to complete
  auto start = std::chrono::steady_clock::now();
  while (std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start).count() < 500) {
    OwlBrowserManager::PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Step 2: Get element bounds from render tracker
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  ElementRenderInfo info;

  if (!tracker->GetElementBounds(context_id, selector, info)) {
    LOG_ERROR("CaptchaUtils", "Element not found in render tracker: " + selector);
    return false;
  }

  if (!info.visible) {
    LOG_ERROR("CaptchaUtils", "Element is not visible: " + selector);
    return false;
  }

  LOG_DEBUG("CaptchaUtils", "Found element at (" + std::to_string(info.x) + "," +
            std::to_string(info.y) + ") " + std::to_string(info.width) + "x" +
            std::to_string(info.height));

  // Step 2: Click element to focus it using real mouse events
  int click_x = info.x + (info.width / 2);
  int click_y = info.y + (info.height / 2);

  LOG_DEBUG("CaptchaUtils", "Clicking input at (" + std::to_string(click_x) + "," +
            std::to_string(click_y) + ") to focus");

  auto host = browser->GetHost();

  // Ensure browser has focus
  LOG_DEBUG("CaptchaUtils", "Setting browser focus");
  host->SetFocus(true);
  OwlBrowserManager::PumpMessageLoopIfNeeded();

  // Send mouse click to focus the input
  CefMouseEvent mouse_event;
  mouse_event.x = click_x;
  mouse_event.y = click_y;
  mouse_event.modifiers = 0;

  // Mouse move
  LOG_DEBUG("CaptchaUtils", "Sending mouse move");
  host->SendMouseMoveEvent(mouse_event, false);
  OwlBrowserManager::PumpMessageLoopIfNeeded();

  // Mouse down
  LOG_DEBUG("CaptchaUtils", "Sending mouse down");
  host->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 1);
  OwlBrowserManager::PumpMessageLoopIfNeeded();

  // Mouse up
  LOG_DEBUG("CaptchaUtils", "Sending mouse up");
  host->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 1);
  OwlBrowserManager::PumpMessageLoopIfNeeded();

  // Wait for focus to take effect
  LOG_DEBUG("CaptchaUtils", "Waiting for focus");
  Wait(200);

  // Step 3: Send IPC message to renderer to set value and trigger DOM events
  // This is a native C++ solution that runs in the renderer process where the DOM lives
  LOG_DEBUG("CaptchaUtils", "Sending type_into_element IPC message to renderer");

  CefRefPtr<CefProcessMessage> type_message = CefProcessMessage::Create("type_into_element");
  CefRefPtr<CefListValue> type_args = type_message->GetArgumentList();
  type_args->SetString(0, selector);
  type_args->SetString(1, text);

  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, type_message);

  // Wait for renderer to process and trigger paint
  // The renderer will:
  // 1. Find the element using V8/DOM
  // 2. Set the value directly
  // 3. Trigger 'input' and 'change' events
  // 4. This triggers a paint with the new value
  LOG_DEBUG("CaptchaUtils", "Waiting for renderer to process and paint");
  Wait(300);  // Wait for IPC + DOM update + paint

  // Pump message loop to ensure paint is processed
  auto paint_start = std::chrono::steady_clock::now();
  while (std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - paint_start).count() < 200) {
    OwlBrowserManager::PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  LOG_DEBUG("CaptchaUtils", "Text entered successfully: '" + text + "'");
  return true;
}

// Callback class to capture JavaScript execution result
class JSExecutionCallback : public CefV8Handler {
public:
  std::string result;
  bool complete = false;
  std::mutex mutex;
  std::condition_variable cv;

  bool Execute(const CefString& name,
              CefRefPtr<CefV8Value> object,
              const CefV8ValueList& arguments,
              CefRefPtr<CefV8Value>& retval,
              CefString& exception) override {
    return false;
  }

  IMPLEMENT_REFCOUNTING(JSExecutionCallback);
};

std::string OwlCaptchaUtils::ExecuteJavaScriptAndGetResult(
    CefRefPtr<CefBrowser> browser,
    const std::string& script) {

  if (!browser || !browser->GetMainFrame()) {
    return "";
  }

  // Create a unique callback name
  std::string callback_name = "__owl_js_callback_" +
    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

  // Wrap script to call our callback with the result
  std::string wrapped_script =
    "(function() {"
    "  try {"
    "    var __result = " + script + ";"
    "    if (window." + callback_name + ") {"
    "      window." + callback_name + "(__result);"
    "    }"
    "  } catch(e) {"
    "    if (window." + callback_name + ") {"
    "      window." + callback_name + "(null);"
    "    }"
    "  }"
    "})();";

  // Set up a global function that JavaScript can call to return the result
  std::string setup_callback =
    "window." + callback_name + " = function(val) {"
    "  window." + callback_name + "_result = (val === null || val === undefined) ? 'null' : JSON.stringify(val);"
    "  window." + callback_name + "_done = true;"
    "};";

  browser->GetMainFrame()->ExecuteJavaScript(setup_callback, browser->GetMainFrame()->GetURL(), 0);
  Wait(50);  // Let callback setup execute

  // Execute the wrapped script
  browser->GetMainFrame()->ExecuteJavaScript(wrapped_script, browser->GetMainFrame()->GetURL(), 0);
  Wait(150);  // Wait for execution

  // Retrieve the result
  std::string check_result =
    "(function() {"
    "  if (window." + callback_name + "_done) {"
    "    var res = window." + callback_name + "_result;"
    "    delete window." + callback_name + ";"
    "    delete window." + callback_name + "_result;"
    "    delete window." + callback_name + "_done;"
    "    return res;"
    "  }"
    "  return '';"
    "})();";

  // For now, return empty - we need a better mechanism
  // This is still async and we can't directly get the result
  return "";
}

void OwlCaptchaUtils::Wait(int milliseconds) {
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

bool OwlCaptchaUtils::HighlightElement(CefRefPtr<CefBrowser> browser,
                                        const std::string& selector,
                                        const std::string& border_color,
                                        const std::string& background_color,
                                        int duration_ms) {
  LOG_DEBUG("CaptchaUtils", "Highlighting element: " + selector);

  if (!browser || !browser->GetMainFrame()) {
    return false;
  }

  std::string highlight_script = R"(
    (function() {
      var elem = document.querySelector(')" + selector + R"(');
      if (!elem) return false;

      // Store original styles
      var originalBorder = elem.style.border;
      var originalBackground = elem.style.background;
      var originalOutline = elem.style.outline;

      // Apply highlight
      elem.style.border = '3px solid )" + border_color + R"(';
      elem.style.background = ')" + background_color + R"(';
      elem.style.outline = '2px solid )" + border_color + R"(';
      elem.style.outlineOffset = '2px';

      // Remove highlight after duration (if specified)
      )" + (duration_ms > 0 ? R"(
      setTimeout(function() {
        elem.style.border = originalBorder;
        elem.style.background = originalBackground;
        elem.style.outline = originalOutline;
      }, )" + std::to_string(duration_ms) + R"();
      )" : "") + R"(

      return true;
    })();
  )";

  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  frame->ExecuteJavaScript(highlight_script, frame->GetURL(), 0);

  return true;
}

bool OwlCaptchaUtils::RemoveHighlight(CefRefPtr<CefBrowser> browser,
                                       const std::string& selector) {
  LOG_DEBUG("CaptchaUtils", "Removing highlight from: " + selector);

  if (!browser || !browser->GetMainFrame()) {
    return false;
  }

  std::string remove_script = R"(
    (function() {
      var elem = document.querySelector(')" + selector + R"(');
      if (!elem) return false;

      elem.style.border = '';
      elem.style.background = '';
      elem.style.outline = '';
      elem.style.outlineOffset = '';

      return true;
    })();
  )";

  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  frame->ExecuteJavaScript(remove_script, frame->GetURL(), 0);

  return true;
}

std::vector<std::string> OwlCaptchaUtils::GetAllElements(
    CefRefPtr<CefBrowser> browser,
    const std::string& selector) {

  std::vector<std::string> elements;

  // Would execute JavaScript to get all matching elements
  // For now, return empty
  return elements;
}

std::string OwlCaptchaUtils::GetTextContent(CefRefPtr<CefBrowser> browser,
                                             const std::string& selector) {
  std::string text_script = R"(
    (function() {
      var elem = document.querySelector(')" + selector + R"(');
      return elem ? elem.textContent.trim() : '';
    })();
  )";

  // Would execute and return result
  return "";
}

std::string OwlCaptchaUtils::GetAttribute(CefRefPtr<CefBrowser> browser,
                                           const std::string& selector,
                                           const std::string& attribute) {
  std::string attr_script = R"(
    (function() {
      var elem = document.querySelector(')" + selector + R"(');
      return elem ? elem.getAttribute(')" + attribute + R"(') : '';
    })();
  )";

  // Would execute and return result
  return "";
}

std::string OwlCaptchaUtils::Base64Encode(const std::vector<uint8_t>& data) {
  static const char base64_chars[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string encoded;
  int i = 0;
  int j = 0;
  uint8_t char_array_3[3];
  uint8_t char_array_4[4];

  for (uint8_t byte : data) {
    char_array_3[i++] = byte;
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
      char_array_4[3] = char_array_3[2] & 0x3f;

      for (i = 0; i < 4; i++)
        encoded += base64_chars[char_array_4[i]];
      i = 0;
    }
  }

  if (i) {
    for (j = i; j < 3; j++)
      char_array_3[j] = '\0';

    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

    for (j = 0; j < i + 1; j++)
      encoded += base64_chars[char_array_4[j]];

    while (i++ < 3)
      encoded += '=';
  }

  return encoded;
}

std::vector<uint8_t> OwlCaptchaUtils::Base64Decode(const std::string& encoded) {
  static const std::string base64_chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::vector<uint8_t> decoded;
  int i = 0;
  int j = 0;
  uint8_t char_array_4[4], char_array_3[3];

  for (char c : encoded) {
    if (c == '=') break;
    if (!isalnum(c) && c != '+' && c != '/') continue;

    char_array_4[i++] = c;
    if (i == 4) {
      for (i = 0; i < 4; i++)
        char_array_4[i] = base64_chars.find(char_array_4[i]);

      char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
      char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
      char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

      for (i = 0; i < 3; i++)
        decoded.push_back(char_array_3[i]);
      i = 0;
    }
  }

  if (i) {
    for (j = i; j < 4; j++)
      char_array_4[j] = 0;

    for (j = 0; j < 4; j++)
      char_array_4[j] = base64_chars.find(char_array_4[j]);

    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
    char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);

    for (j = 0; j < i - 1; j++)
      decoded.push_back(char_array_3[j]);
  }

  return decoded;
}
