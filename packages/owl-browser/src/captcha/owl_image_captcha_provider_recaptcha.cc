#include "owl_image_captcha_provider_recaptcha.h"
#include "owl_virtual_camera.h"
#include "owl_browser_manager.h"
#include "owl_llm_client.h"
#include "owl_render_tracker.h"
#include "owl_semantic_matcher.h"
#include "owl_native_screenshot.h"
#include "owl_platform_utils.h"
#include "logger.h"
#include "include/cef_process_message.h"
#include "include/cef_app.h"

#include <thread>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <random>
#include <cmath>
#include <atomic>

// Human-like random delay helper
static int RandomDelay(int min_ms, int max_ms) {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(min_ms, max_ms);
  return dis(gen);
}

// Small random offset for click position (not always exact center)
static int RandomOffset(int range) {
  return RandomDelay(-range, range);
}

// Human-like curved mouse movement using bezier curve
// NOTE: last_cursor_x/y are now passed by reference for per-instance tracking
static void HumanMouseMoveTo(CefRefPtr<CefBrowserHost> host, int end_x, int end_y,
                              int& last_cursor_x, int& last_cursor_y) {
  static std::random_device rd;
  static std::mt19937 gen(rd());

  int start_x = last_cursor_x;
  int start_y = last_cursor_y;

  // Calculate distance
  double dx = end_x - start_x;
  double dy = end_y - start_y;
  double distance = std::sqrt(dx * dx + dy * dy);

  // Skip movement if distance is very small
  if (distance < 10) {
    last_cursor_x = end_x;
    last_cursor_y = end_y;
    return;
  }

  // Calculate steps based on distance (1 step per 5-10 pixels)
  std::uniform_int_distribution<> step_dis(5, 10);
  int steps = std::max(10, static_cast<int>(distance / step_dis(gen)));

  // Generate bezier control points for curved path
  std::uniform_real_distribution<> curve_dis(-0.25, 0.25);
  double perp_x = -dy;
  double perp_y = dx;
  double perp_len = std::sqrt(perp_x * perp_x + perp_y * perp_y);
  if (perp_len > 0) {
    perp_x /= perp_len;
    perp_y /= perp_len;
  }

  double curve_factor = curve_dis(gen);
  double ctrl_x = start_x + dx * 0.4 + perp_x * distance * curve_factor;
  double ctrl_y = start_y + dy * 0.4 + perp_y * distance * curve_factor;

  double curve_factor2 = curve_dis(gen) * 0.5;
  double ctrl2_x = start_x + dx * 0.7 + perp_x * distance * curve_factor2;
  double ctrl2_y = start_y + dy * 0.7 + perp_y * distance * curve_factor2;

  CefMouseEvent mouse_event;
  mouse_event.modifiers = 0;

  // Move along cubic bezier curve
  std::uniform_int_distribution<> delay_dis(3, 10);
  std::uniform_int_distribution<> jitter_dis(-1, 1);

  for (int s = 1; s <= steps; s++) {
    double t = static_cast<double>(s) / steps;

    // Cubic bezier formula
    double t2 = t * t;
    double t3 = t2 * t;
    double mt = 1 - t;
    double mt2 = mt * mt;
    double mt3 = mt2 * mt;

    double px = mt3 * start_x + 3 * mt2 * t * ctrl_x + 3 * mt * t2 * ctrl2_x + t3 * end_x;
    double py = mt3 * start_y + 3 * mt2 * t * ctrl_y + 3 * mt * t2 * ctrl2_y + t3 * end_y;

    // Add micro-jitter
    int jx = jitter_dis(gen);
    int jy = jitter_dis(gen);

    mouse_event.x = static_cast<int>(px) + jx;
    mouse_event.y = static_cast<int>(py) + jy;

    host->SendMouseMoveEvent(mouse_event, false);
    OwlBrowserManager::PumpMessageLoopIfNeeded();

    // Easing: slower at start and end
    int delay = delay_dis(gen);
    if (t < 0.15 || t > 0.85) delay += 4;
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
  }

  // Ensure we end exactly at target
  mouse_event.x = end_x;
  mouse_event.y = end_y;
  host->SendMouseMoveEvent(mouse_event, false);
  OwlBrowserManager::PumpMessageLoopIfNeeded();

  last_cursor_x = end_x;
  last_cursor_y = end_y;
}

// Human-like native click with curved movement and delay between down/up
// NOTE: last_cursor_x/y are now passed by reference for per-instance tracking
static void HumanNativeClick(CefRefPtr<CefBrowserHost> host, int x, int y,
                              int& last_cursor_x, int& last_cursor_y,
                              int width = 0, int height = 0) {
  // Add small random offset if element size is provided (not always exact center)
  int offset_x = (width > 10) ? RandomOffset(width / 6) : 0;
  int offset_y = (height > 10) ? RandomOffset(height / 6) : 0;

  int target_x = x + offset_x;
  int target_y = y + offset_y;

  // Move cursor in a curved path to target
  HumanMouseMoveTo(host, target_x, target_y, last_cursor_x, last_cursor_y);

  // Small pause before clicking (like human aiming)
  std::this_thread::sleep_for(std::chrono::milliseconds(RandomDelay(30, 80)));

  // Click
  CefMouseEvent mouse_event;
  mouse_event.x = target_x;
  mouse_event.y = target_y;
  mouse_event.modifiers = 0;

  host->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 1);
  // Human click duration: 50-120ms between down and up
  std::this_thread::sleep_for(std::chrono::milliseconds(RandomDelay(50, 120)));
  host->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 1);

  // Update cursor position
  last_cursor_x = target_x;
  last_cursor_y = target_y;
}

// Inject virtual camera frame into browser via JavaScript (all frames)
static void InjectCameraFrame(CefRefPtr<CefBrowser> browser, const std::string& base64_jpeg) {
  if (base64_jpeg.empty()) {
    return;
  }

  // Call the injected JavaScript function to update video canvas
  // Execute in ALL frames since the video may be in a nested iframe
  std::string js = "if (window.__injectVirtualCameraFrame) { "
                   "window.__injectVirtualCameraFrame('" + base64_jpeg + "'); "
                   "}";

  // Get all frame identifiers and inject into each
  std::vector<CefString> frame_ids;
  browser->GetFrameIdentifiers(frame_ids);

  for (const CefString& frame_id : frame_ids) {
    CefRefPtr<CefFrame> frame = browser->GetFrameByIdentifier(frame_id);
    if (frame && frame->IsValid()) {
      frame->ExecuteJavaScript(js, frame->GetURL(), 0);
    }
  }
}

// ============================================================================
// CONSTRUCTOR & INITIALIZATION
// ============================================================================

RecaptchaImageCaptchaProvider::RecaptchaImageCaptchaProvider() {
  InitializeConfig();
  InitializeGesturePaths();
}

void RecaptchaImageCaptchaProvider::InitializeConfig() {
  // Use correct field names from ImageCaptchaProviderConfig struct
  config_.checkbox_selector = ".recaptcha-checkbox-border, #recaptcha-anchor";
  config_.challenge_container_selector = ".rc-imageselect, .rc-image-tile-wrapper";
  config_.submit_button_selector = "#recaptcha-verify-button";
  config_.grid_container_selector = ".rc-imageselect-table-33, .rc-imageselect-table-44";
  config_.grid_item_selector = ".rc-imageselect-tile";
  config_.uses_iframe = true;
  config_.iframe_selector = "iframe[src*='recaptcha'], iframe[title*='reCAPTCHA']";
  config_.challenge_iframe_selector = "iframe[src*='bframe']";
}

void RecaptchaImageCaptchaProvider::InitializeGesturePaths() {
  // Get the gestures directory from app bundle resources
  // The gestures are inside statics/assets/gestures
  std::string resources_dir = OlibPlatform::GetResourcesDir();
  if (!resources_dir.empty()) {
    gestures_directory_ = resources_dir + "/statics/assets/gestures";
  } else {
    // Fallback for development
    gestures_directory_ = "statics/assets/gestures";
  }

  // Map gesture types to image filenames
  // NOTE: Image file naming is counterintuitive:
  // - "raised_hand.jpeg" contains an open palm with all 5 fingers visible
  // - "open_palm.jpeg" contains a fist with thumb sideways (like a thumbs-up rotated)
  gesture_paths_[RecaptchaGesture::RAISED_HAND] = "raised_hand.jpeg";
  gesture_paths_[RecaptchaGesture::CLOSED_FIST] = "closed_fist.jpeg";
  gesture_paths_[RecaptchaGesture::THUMBS_UP] = "thumbs_up.jpeg";
  gesture_paths_[RecaptchaGesture::THUMBS_DOWN] = "thumbs_down.jpeg";
  gesture_paths_[RecaptchaGesture::OPEN_PALM] = "raised_hand.jpeg";  // raised_hand.jpeg has actual open palm
  gesture_paths_[RecaptchaGesture::PEACE_SIGN] = "peace_sign.jpeg";
  gesture_paths_[RecaptchaGesture::POINTING_UP] = "pointing_up.jpeg";
  gesture_paths_[RecaptchaGesture::THREE_FINGERS] = "three_fingers.jpeg";
  gesture_paths_[RecaptchaGesture::FOUR_FINGERS] = "four_fingers.jpeg";

  LOG_DEBUG("reCAPTCHA", "Gesture paths initialized from: " + gestures_directory_);
}

// ============================================================================
// DETECTION
// ============================================================================

double RecaptchaImageCaptchaProvider::DetectProvider(
    CefRefPtr<CefBrowser> browser,
    const CaptchaClassificationResult& classification) {

  // Check for reCAPTCHA iframe
  std::string script = R"JS(
    (function() {
      var iframes = document.querySelectorAll('iframe');
      for (var i = 0; i < iframes.length; i++) {
        var src = iframes[i].src || '';
        if (src.includes('recaptcha') || src.includes('google.com/recaptcha')) {
          return 'found';
        }
      }
      // Also check for checkbox
      var checkbox = document.querySelector('.g-recaptcha, .recaptcha-checkbox');
      if (checkbox) return 'found';
      return 'not_found';
    })();
  )JS";

  std::string result = ExecuteJavaScript(browser, script);
  return (result == "found") ? 0.9 : 0.0;
}

// ============================================================================
// MAIN SOLVE ENTRY POINT
// ============================================================================

ImageCaptchaSolveResult RecaptchaImageCaptchaProvider::Solve(
    const std::string& context_id,
    CefRefPtr<CefBrowser> browser,
    const CaptchaClassificationResult& classification,
    OwlLLMClient* llm_client,
    int max_attempts) {

  LOG_DEBUG("reCAPTCHA", "Starting reCAPTCHA solve (max_attempts=" + std::to_string(max_attempts) + ")");

  // Store context_id for helper methods that need it
  current_context_id_ = context_id;

  ImageCaptchaSolveResult result;
  result.success = false;
  result.provider = ImageCaptchaProviderType::RECAPTCHA;

  // Step 1: Click the checkbox
  LOG_DEBUG("reCAPTCHA", "Clicking checkbox");
  if (!ClickCheckbox(browser, context_id, classification)) {
    result.error_message = "Failed to click reCAPTCHA checkbox";
    LOG_ERROR("reCAPTCHA", result.error_message);
    return result;
  }

  // Wait for checkbox animation and potential auto-verify
  Wait(300);

  // Check if auto-verified (no challenge)
  if (IsAutoVerified(browser, classification)) {
    LOG_DEBUG("reCAPTCHA", "Auto-verified, no challenge needed");
    result.success = true;
    return result;
  }

  // Check for liveness button and try liveness challenge
  LOG_DEBUG("reCAPTCHA", "Looking for liveness challenge option");

  for (int attempt = 0; attempt < max_attempts; attempt++) {
    LOG_DEBUG("reCAPTCHA", "Attempt " + std::to_string(attempt + 1) + "/" + std::to_string(max_attempts));

    // First check if we're already on the liveness screen
    if (IsOnLivenessScreen(browser)) {
      LOG_DEBUG("reCAPTCHA", "On liveness screen, solving...");
      result = SolveLivenessChallenge(context_id, browser, llm_client);
      if (result.success) {
        LOG_DEBUG("reCAPTCHA", "Liveness challenge solved");
        return result;
      }
    }

    // Try to click liveness button
    if (HasLivenessButton(browser)) {
      LOG_DEBUG("reCAPTCHA", "Found liveness button, switching to camera challenge");

      if (ClickLivenessButton(browser)) {
        Wait(200);
        result = SolveLivenessChallenge(context_id, browser, llm_client);

        if (result.success) {
          LOG_DEBUG("reCAPTCHA", "Liveness challenge solved");
          return result;
        }
      }
    }

    // Check if solved
    if (CheckVerificationSuccess(context_id, browser)) {
      result.success = true;
      LOG_DEBUG("reCAPTCHA", "reCAPTCHA solved");
      return result;
    }

    // Brief wait between attempts
    Wait(300);
  }

  result.error_message = "Failed to solve reCAPTCHA after " + std::to_string(max_attempts) + " attempts";
  LOG_ERROR("reCAPTCHA", result.error_message);
  return result;
}

// ============================================================================
// LIVENESS CHALLENGE
// ============================================================================

ImageCaptchaSolveResult RecaptchaImageCaptchaProvider::SolveLivenessChallenge(
    const std::string& context_id,
    CefRefPtr<CefBrowser> browser,
    OwlLLMClient* llm_client) {

  LOG_DEBUG("reCAPTCHA", "Starting liveness challenge for context: " + context_id);

  ImageCaptchaSolveResult result;
  result.success = false;
  result.provider = ImageCaptchaProviderType::RECAPTCHA;

  // Initialize per-context virtual camera (for thread-safe concurrent usage)
  auto* camera = OwlVirtualCamera::VirtualCameraManager::GetInstanceForContext(context_id);
  camera->Initialize(gestures_directory_);

  // Set background image (person sitting in front of camera)
  std::string bg_path = gestures_directory_ + "/background.jpeg";
  if (!camera->SetBackgroundImage(bg_path)) {
    LOG_WARN("reCAPTCHA", "Failed to set background image: " + bg_path);
  }

  // Start background thread to continuously pump frames to the browser
  std::atomic<bool> stop_frame_pump{false};
  std::thread frame_pump_thread([&browser, camera, &stop_frame_pump]() {
    LOG_DEBUG("reCAPTCHA", "Virtual camera frame pump started");
    while (!stop_frame_pump.load()) {
      std::string frame_data = camera->GetCurrentFrameBase64JPEG(75);
      if (!frame_data.empty()) {
        InjectCameraFrame(browser, frame_data);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 fps
    }
    LOG_DEBUG("reCAPTCHA", "Virtual camera frame pump stopped");
  });

  // Click the "Next" button if present (intro screen)
  Wait(200);
  if (ClickLivenessNextButton(browser)) {
    LOG_DEBUG("reCAPTCHA", "Clicked Next button on intro screen");
    Wait(300);
  }

  // Show background briefly while camera initializes
  Wait(500);

  // Track last gesture shown - start with UNKNOWN so first gesture is always shown
  RecaptchaGesture last_gesture_shown = RecaptchaGesture::UNKNOWN;

  // Loop to handle gesture recognition
  for (int gesture_attempt = 0; gesture_attempt < 10; gesture_attempt++) {
    LOG_DEBUG("reCAPTCHA", "Gesture attempt " + std::to_string(gesture_attempt + 1));

    // Check if captcha is complete (camera box is gone)
    if (IsLivenessComplete(browser)) {
      result.success = true;
      stop_frame_pump.store(true);
      if (frame_pump_thread.joinable()) {
        frame_pump_thread.join();
      }
      camera->ClearOverlay();
      camera->ClearBackground();
      OwlVirtualCamera::VirtualCameraManager::ReleaseInstanceForContext(context_id);
      return result;
    }

    // Scan all frames for the gesture emoji IMG element
    RecaptchaGesture gesture = GetRequestedGestureFromDOM(browser);

    if (gesture == RecaptchaGesture::UNKNOWN) {
      LOG_DEBUG("reCAPTCHA", "Could not identify gesture from DOM, retrying");
      Wait(500);
      continue;
    }

    // Check if this is a new gesture request
    if (gesture == last_gesture_shown) {
      LOG_DEBUG("reCAPTCHA", "Same gesture still requested, waiting for recognition");
      Wait(500);
      continue;
    }

    LOG_DEBUG("reCAPTCHA", "New gesture requested: " + std::to_string(static_cast<int>(gesture)));

    // Clear overlay first, show background briefly
    camera->ClearOverlay();
    Wait(300);

    // Set up virtual camera to show the identified gesture
    if (!SetupVirtualCameraForGesture(gesture)) {
      LOG_ERROR("reCAPTCHA", "Failed to set up virtual camera for gesture");
      Wait(500);
      continue;
    }

    last_gesture_shown = gesture;
    Wait(2500);  // Display gesture for recognition
  }

  stop_frame_pump.store(true);
  if (frame_pump_thread.joinable()) {
    frame_pump_thread.join();
  }

  // Clean up virtual camera on failure
  camera->ClearOverlay();
  camera->ClearBackground();
  OwlVirtualCamera::VirtualCameraManager::ReleaseInstanceForContext(context_id);

  result.error_message = "Failed to complete liveness challenge";
  return result;
}

bool RecaptchaImageCaptchaProvider::ClickLivenessButton(CefRefPtr<CefBrowser> browser) {
  LOG_DEBUG("reCAPTCHA", "Clicking liveness button");
  return ClickElementInBframe(browser, current_context_id_, "recaptcha-liveness-button");
}

bool RecaptchaImageCaptchaProvider::ClickLivenessNextButton(CefRefPtr<CefBrowser> browser) {
  LOG_DEBUG("reCAPTCHA", "Looking for Next button on liveness intro screen");

  // Get bframe position first
  int bframe_x, bframe_y, bframe_w, bframe_h;
  if (!GetBframePosition(browser, current_context_id_, bframe_x, bframe_y, bframe_w, bframe_h)) {
    LOG_WARN("reCAPTCHA", "Could not find bframe for Next button click");
    bframe_x = bframe_y = 0;
  }

  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (!tracker) {
    LOG_WARN("reCAPTCHA", "Render tracker not available");
    return false;
  }

  // The "Next" button is inside a NESTED iframe within the bframe
  // We need to find and scan ALL frames, including nested ones
  std::string liveness_context_id = current_context_id_ + "_recaptcha_liveness";

  // Poll for the liveness screen to load
  for (int retry = 0; retry < 15; retry++) {
    // Get all frames including nested ones
    std::vector<CefString> frame_ids;
    browser->GetFrameIdentifiers(frame_ids);

    LOG_DEBUG("reCAPTCHA", "Retry " + std::to_string(retry + 1) + ": Found " +
              std::to_string(frame_ids.size()) + " frames total");

    // Look for a liveness-related frame or any frame that might contain "Next"
    for (const CefString& frame_id : frame_ids) {
      CefRefPtr<CefFrame> frame = browser->GetFrameByIdentifier(frame_id);
      if (!frame) continue;

      std::string url = frame->GetURL().ToString();

      // Skip frames that are clearly not the liveness intro
      // Look for frames that might contain the liveness content
      bool is_candidate = (url.find("liveness") != std::string::npos ||
                           url.find("recaptcha") != std::string::npos ||
                           url.find("google.com") != std::string::npos ||
                           url.empty() ||  // about:blank frames might contain injected content
                           url.find("about:") != std::string::npos);

      if (!is_candidate) continue;

      LOG_DEBUG("reCAPTCHA", "Scanning frame: " + url.substr(0, 80));

      // Clear and scan this frame
      tracker->ClearContext(liveness_context_id);

      CefRefPtr<CefProcessMessage> scan_msg = CefProcessMessage::Create("scan_element");
      CefRefPtr<CefListValue> args = scan_msg->GetArgumentList();
      args->SetString(0, liveness_context_id);
      args->SetString(1, "*");
      frame->SendProcessMessage(PID_RENDERER, scan_msg);

      // Wait for scan
      for (int i = 0; i < 20; i++) {
        OwlBrowserManager::PumpMessageLoopIfNeeded();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      // Check for "Next" text in this frame's elements
      std::vector<ElementRenderInfo> elements = tracker->GetAllVisibleElements(liveness_context_id);

      for (const auto& elem : elements) {
        std::string text_lower = elem.text;
        std::transform(text_lower.begin(), text_lower.end(), text_lower.begin(), ::tolower);

        if (text_lower.find("next") != std::string::npos && elem.width > 0 && elem.height > 0) {
          // The element position is relative to its frame
          int abs_x = bframe_x + elem.x;
          int abs_y = bframe_y + elem.y;
          int center_x = abs_x + (elem.width / 2);
          int center_y = abs_y + (elem.height / 2);

          LOG_DEBUG("reCAPTCHA", "Clicking Next button at (" +
                    std::to_string(center_x) + "," + std::to_string(center_y) + ")");

          auto host = browser->GetHost();
          if (host) {
            host->SetFocus(true);
            HumanNativeClick(host, center_x, center_y, last_cursor_x_, last_cursor_y_, elem.width, elem.height);
            return true;
          }
        }
      }
    }

    // Wait before retry
    LOG_DEBUG("reCAPTCHA", "Next button not found yet, waiting...");
    Wait(200);
  }

  LOG_WARN("reCAPTCHA", "Next button not found after retries");
  return false;
}

bool RecaptchaImageCaptchaProvider::ClickElementInBframe(CefRefPtr<CefBrowser> browser,
                                                          const std::string& context_id,
                                                          const std::string& element_selector_or_id) {
  LOG_DEBUG("reCAPTCHA", "ClickElementInBframe: " + element_selector_or_id);

  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (!tracker) {
    LOG_WARN("reCAPTCHA", "Render tracker not available");
    return false;
  }

  // Step 1: Get bframe position from main frame
  int bframe_x, bframe_y, bframe_w, bframe_h;
  if (!GetBframePosition(browser, context_id, bframe_x, bframe_y, bframe_w, bframe_h)) {
    LOG_WARN("reCAPTCHA", "Could not find bframe position");
    return false;
  }

  // Step 2: Rescan challenge frame to get element positions
  RescanChallengeFrame(browser, context_id);

  // Step 3: Find the element in challenge frame
  std::string challenge_context_id = context_id + "_recaptcha_challenge";
  std::vector<ElementRenderInfo> elements = tracker->GetAllVisibleElements(challenge_context_id);

  LOG_DEBUG("reCAPTCHA", "Searching " + std::to_string(elements.size()) +
            " elements for: " + element_selector_or_id);

  for (const auto& elem : elements) {
    // Match by id or selector or className
    bool matches = (elem.id == element_selector_or_id ||
                    elem.selector.find(element_selector_or_id) != std::string::npos ||
                    elem.className.find(element_selector_or_id) != std::string::npos);

    if (matches) {
      // Calculate absolute position = bframe + element position within iframe
      int abs_x = bframe_x + elem.x;
      int abs_y = bframe_y + elem.y;
      int center_x = abs_x + (elem.width / 2);
      int center_y = abs_y + (elem.height / 2);

      LOG_DEBUG("reCAPTCHA", "Native click on " + element_selector_or_id + " at (" +
                std::to_string(center_x) + "," + std::to_string(center_y) + ")");

      // Send native mouse events
      auto host = browser->GetHost();
      if (host) {
        host->SetFocus(true);
        HumanNativeClick(host, center_x, center_y, last_cursor_x_, last_cursor_y_, elem.width, elem.height);
        return true;
      }
    }
  }

  LOG_WARN("reCAPTCHA", "Element not found in tracker: " + element_selector_or_id);

  return false;
}

// Scan ALL nested frames and find IMG elements with alt attributes for gesture detection
RecaptchaGesture RecaptchaImageCaptchaProvider::GetRequestedGestureFromDOM(
    CefRefPtr<CefBrowser> browser) {

  LOG_DEBUG("reCAPTCHA", "Scanning frames for gesture emoji");

  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (!tracker) {
    LOG_WARN("reCAPTCHA", "Render tracker not available");
    return RecaptchaGesture::UNKNOWN;
  }

  // Get ALL frames in the browser (including nested iframes)
  std::vector<CefString> frame_ids;
  browser->GetFrameIdentifiers(frame_ids);

  LOG_DEBUG("reCAPTCHA", "Found " + std::to_string(frame_ids.size()) + " frames to scan");

  // Scan each frame for IMG elements with gesture alt text
  for (const CefString& frame_id : frame_ids) {
    CefRefPtr<CefFrame> frame = browser->GetFrameByIdentifier(frame_id);
    if (!frame) continue;

    std::string url = frame->GetURL().ToString();

    // Only scan frames that might contain the gesture (hand-gestures or recaptcha frames)
    bool is_gesture_frame = (url.find("hand-gestures") != std::string::npos ||
                             url.find("recaptcha") != std::string::npos ||
                             url.find("google.com") != std::string::npos);

    if (!is_gesture_frame) continue;

    // Create a unique context ID for this frame scan
    std::string frame_context = current_context_id_ + "_frame_" + frame_id.ToString().substr(0, 8);

    // Clear previous scan data and trigger new scan
    tracker->ClearContext(frame_context);

    CefRefPtr<CefProcessMessage> scan_msg = CefProcessMessage::Create("scan_element");
    CefRefPtr<CefListValue> args = scan_msg->GetArgumentList();
    args->SetString(0, frame_context);
    args->SetString(1, "*");  // Scan all elements
    frame->SendProcessMessage(PID_RENDERER, scan_msg);

    // Wait for scan to complete
    for (int i = 0; i < 30; i++) {
      OwlBrowserManager::PumpMessageLoopIfNeeded();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Check scanned elements for IMG with gesture alt text
    // Use GetAllElements to include hidden elements (gesture IMGs have visible=false)
    auto elements = tracker->GetAllElements(frame_context);

#ifdef OWL_DEBUG_BUILD
    // Log what we find in the hand-gestures frame specifically
    bool is_hand_gestures = (url.find("hand-gestures") != std::string::npos);

    LOG_DEBUG("reCAPTCHA", "Frame " + url.substr(0, 60) + " has " +
              std::to_string(elements.size()) + " elements");

    // In hand-gestures frame, log elements for debugging
    if (is_hand_gestures) {
      LOG_DEBUG("reCAPTCHA", "Hand-gestures frame has " + std::to_string(elements.size()) + " elements");
    }
#endif

    // Find gesture IMGs and determine which is active
    // Strategy: The active gesture has SHORTER className (no hidden class suffix)
    // Don't rely on specific class names as they are obfuscated/dynamic
    std::vector<std::pair<RecaptchaGesture, std::string>> gesture_imgs;  // gesture, className
    std::vector<std::pair<RecaptchaGesture, std::string>> gesture_alts;  // gesture, alt text

    for (const auto& elem : elements) {
      // Look for IMG elements with alt attribute
      if (elem.tag == "img") {
        if (elem.alt.empty()) {
          continue;  // Skip IMG without alt
        }

        // Try parsing gesture from alt text
        RecaptchaGesture gesture = ParseGestureFromAltText(elem.alt);
        if (gesture == RecaptchaGesture::UNKNOWN) {
          continue;  // Not a gesture image
        }

        gesture_imgs.push_back({gesture, elem.className});
        gesture_alts.push_back({gesture, elem.alt});
      }
    }

    // If only one gesture IMG, use it (camera screen shows single indicator)
    if (gesture_imgs.size() == 1) {
      LOG_DEBUG("reCAPTCHA", "Using gesture: " + gesture_alts[0].second);
      return gesture_imgs[0].first;
    }

    // If multiple gesture IMGs, find the one with SHORTEST className
    // (the active one has fewer classes - no hidden class appended)
    if (gesture_imgs.size() > 1) {
      size_t min_class_len = SIZE_MAX;
      int active_idx = -1;

      for (size_t i = 0; i < gesture_imgs.size(); i++) {
        size_t class_len = gesture_imgs[i].second.length();
        if (class_len < min_class_len) {
          min_class_len = class_len;
          active_idx = static_cast<int>(i);
        }
      }

      if (active_idx >= 0) {
        LOG_DEBUG("reCAPTCHA", "Using active gesture: " + gesture_alts[active_idx].second);
        return gesture_imgs[active_idx].first;
      }
    }
  }

  LOG_WARN("reCAPTCHA", "No gesture IMG found in any frame");
  return RecaptchaGesture::UNKNOWN;
}

// Parse gesture from alt text (like "Open palm", "Closed fist", "Victory", etc.)
RecaptchaGesture RecaptchaImageCaptchaProvider::ParseGestureFromAltText(const std::string& alt) {
  // Convert to uppercase for case-insensitive matching
  std::string upper = alt;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

  LOG_DEBUG("reCAPTCHA", "Parsing gesture from alt: " + alt);

  // Map alt text to gestures - based on observed values
  // "Open palm" -> open_palm
  if (upper.find("OPEN") != std::string::npos && upper.find("PALM") != std::string::npos) {
    return RecaptchaGesture::OPEN_PALM;
  }
  // "Closed fist" -> closed_fist
  if (upper.find("CLOSED") != std::string::npos && upper.find("FIST") != std::string::npos) {
    return RecaptchaGesture::CLOSED_FIST;
  }
  if (upper == "FIST" || upper.find("FIST") != std::string::npos) {
    return RecaptchaGesture::CLOSED_FIST;
  }
  // "Pointing up" -> pointing_up
  if (upper.find("POINTING") != std::string::npos && upper.find("UP") != std::string::npos) {
    return RecaptchaGesture::POINTING_UP;
  }
  // "Thumb up" or "Thumbs up" -> thumbs_up
  if (upper.find("THUMB") != std::string::npos && upper.find("UP") != std::string::npos) {
    return RecaptchaGesture::THUMBS_UP;
  }
  // "Thumb down" or "Thumbs down" -> thumbs_down
  if (upper.find("THUMB") != std::string::npos && upper.find("DOWN") != std::string::npos) {
    return RecaptchaGesture::THUMBS_DOWN;
  }
  // "Victory" or "Peace" -> peace_sign
  if (upper.find("VICTORY") != std::string::npos || upper.find("PEACE") != std::string::npos) {
    return RecaptchaGesture::PEACE_SIGN;
  }
  // "Raised hand" -> raised_hand
  if (upper.find("RAISED") != std::string::npos && upper.find("HAND") != std::string::npos) {
    return RecaptchaGesture::RAISED_HAND;
  }
  // "Three fingers" -> three_fingers
  if (upper.find("THREE") != std::string::npos && upper.find("FINGER") != std::string::npos) {
    return RecaptchaGesture::THREE_FINGERS;
  }
  // "Four fingers" -> four_fingers
  if (upper.find("FOUR") != std::string::npos && upper.find("FINGER") != std::string::npos) {
    return RecaptchaGesture::FOUR_FINGERS;
  }
  // Also check for emoji unicode names that might appear
  if (upper.find("HAND") != std::string::npos && upper.find("RAISED") == std::string::npos) {
    // Generic "hand" without "raised" might be open palm
    if (upper.find("OPEN") != std::string::npos || upper.find("SPLAYED") != std::string::npos) {
      return RecaptchaGesture::OPEN_PALM;
    }
  }

  LOG_WARN("reCAPTCHA", "Unknown gesture alt text: '" + alt + "' - please add mapping");
  return RecaptchaGesture::UNKNOWN;
}

bool RecaptchaImageCaptchaProvider::SetupVirtualCameraForGesture(RecaptchaGesture gesture) {
  // Use per-context camera for thread-safe concurrent usage
  auto* camera = OwlVirtualCamera::VirtualCameraManager::GetInstanceForContext(current_context_id_);

  std::string gesture_path = GetGestureImagePath(gesture);
  if (gesture_path.empty()) {
    LOG_ERROR("reCAPTCHA", "No image path for gesture");
    return false;
  }

  LOG_DEBUG("reCAPTCHA", "Setting gesture overlay: " + gesture_path);

  // Set the gesture image as overlay on the background
  if (!camera->SetOverlayImage(gesture_path)) {
    LOG_ERROR("reCAPTCHA", "Failed to set gesture overlay image");
    return false;
  }

  return true;
}

std::string RecaptchaImageCaptchaProvider::GetGestureImagePath(RecaptchaGesture gesture) {
  auto it = gesture_paths_.find(gesture);
  if (it == gesture_paths_.end()) {
    return "";
  }
  return gestures_directory_ + "/" + it->second;
}

bool RecaptchaImageCaptchaProvider::WaitForGestureRecognition(
    CefRefPtr<CefBrowser> browser,
    int timeout_ms) {

  LOG_DEBUG("reCAPTCHA", "Waiting for gesture recognition (timeout=" + std::to_string(timeout_ms) + "ms)");

  int elapsed = 0;
  int check_interval = 500;

  while (elapsed < timeout_ms) {
    // Check if liveness challenge is complete or progressing
    if (IsLivenessComplete(browser)) {
      return true;
    }

    // Check for success indicators in the challenge frame
    std::string script = R"JS(
      (function() {
        var frames = document.querySelectorAll('iframe');
        for (var i = 0; i < frames.length; i++) {
          try {
            var doc = frames[i].contentDocument || frames[i].contentWindow.document;
            // Look for success/progress indicators
            var success = doc.querySelector('.gesture-success, .gesture-complete, .checkmark');
            if (success) return 'success';
            // Look for "next gesture" prompt (means current gesture accepted)
            var next = doc.querySelector('.next-gesture, .gesture-prompt');
            if (next && next.style.display !== 'none') return 'next';
          } catch(e) {}
        }
        return 'waiting';
      })();
    )JS";

    std::string result = ExecuteJavaScript(browser, script);
    if (result == "success" || result == "next") {
      LOG_DEBUG("reCAPTCHA", "Gesture recognition: " + result);
      return true;
    }

    Wait(check_interval);
    elapsed += check_interval;
  }

  return false;
}

bool RecaptchaImageCaptchaProvider::IsLivenessComplete(CefRefPtr<CefBrowser> browser) {
  // Check if we're back to verified state
  if (IsCheckboxVerified(browser)) {
    return true;
  }

  // Check for completion in liveness frame
  std::string script = R"JS(
    (function() {
      var frames = document.querySelectorAll('iframe');
      for (var i = 0; i < frames.length; i++) {
        try {
          var doc = frames[i].contentDocument || frames[i].contentWindow.document;
          // Look for completion indicators
          var complete = doc.querySelector('.liveness-complete, .verification-success');
          if (complete) return 'complete';
        } catch(e) {}
      }
      return 'incomplete';
    })();
  )JS";

  std::string result = ExecuteJavaScript(browser, script);
  return result == "complete";
}

// ============================================================================
// CHECKBOX & VERIFICATION
// ============================================================================

bool RecaptchaImageCaptchaProvider::ClickCheckbox(
    CefRefPtr<CefBrowser> browser,
    const std::string& context_id,
    const CaptchaClassificationResult& classification) {

  LOG_DEBUG("reCAPTCHA", "Clicking reCAPTCHA checkbox using native events");

  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (tracker) {
    // Find the anchor iframe in main frame
    std::vector<ElementRenderInfo> main_elements = tracker->GetAllVisibleElements(context_id);
    int anchor_x = 0, anchor_y = 0, anchor_w = 0, anchor_h = 0;

    for (const auto& elem : main_elements) {
      std::string tag_upper = elem.tag;
      std::transform(tag_upper.begin(), tag_upper.end(), tag_upper.begin(), ::toupper);

      // Anchor iframe is typically 300x78 or similar small size
      if (tag_upper == "IFRAME" && elem.width > 200 && elem.width < 400 && elem.height < 150 && elem.height > 50) {
        anchor_x = elem.x;
        anchor_y = elem.y;
        anchor_w = elem.width;
        anchor_h = elem.height;
        LOG_DEBUG("reCAPTCHA", "Found anchor iframe at: " + std::to_string(anchor_x) + "," + std::to_string(anchor_y));
        break;
      }
    }

    // If anchor found via tracker, click center of anchor iframe
    if (anchor_x > 0 || anchor_y > 0) {
      int center_x = anchor_x + (anchor_w / 2) - 10;
      int center_y = anchor_y + (anchor_h / 2);

      LOG_DEBUG("reCAPTCHA", "Native click on anchor iframe at (" + std::to_string(center_x) + "," + std::to_string(center_y) + ")");

      auto host = browser->GetHost();
      if (host) {
        host->SetFocus(true);
        HumanNativeClick(host, center_x, center_y, last_cursor_x_, last_cursor_y_, anchor_w, anchor_h);
        return true;
      }
    }

    // If anchor iframe not found in tracker, trigger a scan
    LOG_DEBUG("reCAPTCHA", "Anchor iframe not found, triggering scan");

    CefRefPtr<CefProcessMessage> scan_msg = CefProcessMessage::Create("scan_element");
    CefRefPtr<CefListValue> args = scan_msg->GetArgumentList();
    args->SetString(0, context_id);
    args->SetString(1, "*");
    browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, scan_msg);

    for (int i = 0; i < 20; i++) {
      OwlBrowserManager::PumpMessageLoopIfNeeded();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Try again with rescanned elements
    main_elements = tracker->GetAllVisibleElements(context_id);
    for (const auto& elem : main_elements) {
      std::string tag_upper = elem.tag;
      std::transform(tag_upper.begin(), tag_upper.end(), tag_upper.begin(), ::toupper);

      if (tag_upper == "IFRAME" && elem.width > 200 && elem.width < 400 && elem.height < 150 && elem.height > 50) {
        int center_x = elem.x + (elem.width / 2) - 10;
        int center_y = elem.y + (elem.height / 2);

        LOG_DEBUG("reCAPTCHA", "Native click on rescanned anchor");
        auto host = browser->GetHost();
        if (host) {
          host->SetFocus(true);
          HumanNativeClick(host, center_x, center_y, last_cursor_x_, last_cursor_y_, elem.width, elem.height);
          return true;
        }
        break;
      }
    }
  }

  // Fallback to JavaScript click
  LOG_DEBUG("reCAPTCHA", "Using JavaScript fallback for checkbox click");

  std::string script = R"JS(
    (function() {
      // Find the reCAPTCHA anchor iframe
      var frames = document.querySelectorAll('iframe');
      for (var i = 0; i < frames.length; i++) {
        var src = frames[i].src || '';
        if (src.includes('anchor') || src.includes('recaptcha')) {
          try {
            var doc = frames[i].contentDocument || frames[i].contentWindow.document;
            var checkbox = doc.querySelector('.recaptcha-checkbox-border, #recaptcha-anchor');
            if (checkbox) {
              checkbox.click();
              return 'clicked';
            }
          } catch(e) {}
        }
      }
      // Try direct click on g-recaptcha
      var container = document.querySelector('.g-recaptcha');
      if (container) {
        container.click();
        return 'clicked_container';
      }
      return 'not_found';
    })();
  )JS";

  std::string result = ExecuteJavaScript(browser, script);
  bool clicked = (result == "clicked" || result == "clicked_container");

  if (!clicked) {
    LOG_ERROR("reCAPTCHA", "Failed to click checkbox: " + result);
  }

  return clicked;
}

bool RecaptchaImageCaptchaProvider::IsAutoVerified(
    CefRefPtr<CefBrowser> browser,
    const CaptchaClassificationResult& classification) {

  Wait(500);  // Give it time to verify
  return IsCheckboxVerified(browser);
}

bool RecaptchaImageCaptchaProvider::IsCheckboxVerified(CefRefPtr<CefBrowser> browser) {
  std::string script = R"JS(
    (function() {
      var frames = document.querySelectorAll('iframe');
      for (var i = 0; i < frames.length; i++) {
        try {
          var doc = frames[i].contentDocument || frames[i].contentWindow.document;
          var checked = doc.querySelector('.recaptcha-checkbox-checked, [aria-checked="true"]');
          if (checked) return 'verified';
        } catch(e) {}
      }
      return 'not_verified';
    })();
  )JS";

  std::string result = ExecuteJavaScript(browser, script);
  return result == "verified";
}

bool RecaptchaImageCaptchaProvider::CheckVerificationSuccess(
    const std::string& context_id,
    CefRefPtr<CefBrowser> browser) {

  return IsCheckboxVerified(browser);
}

bool RecaptchaImageCaptchaProvider::IsOnLivenessScreen(CefRefPtr<CefBrowser> browser) {
  LOG_DEBUG("reCAPTCHA", "IsOnLivenessScreen: Checking if on liveness screen via IPC");

  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (!tracker) return false;

  // Scan challenge frame
  RescanChallengeFrame(browser, current_context_id_);

  std::string challenge_context_id = current_context_id_ + "_recaptcha_challenge";
  std::vector<ElementRenderInfo> elements = tracker->GetAllVisibleElements(challenge_context_id);

  for (const auto& elem : elements) {
    // Look for liveness container which indicates we're on the liveness screen
    if (elem.className.find("recaptchaJavascriptChallengeLivenessContainer") != std::string::npos ||
        elem.id == "rc-liveness") {
      if (elem.width > 0 && elem.height > 0) {
        LOG_DEBUG("reCAPTCHA", "Found liveness container");
        return true;
      }
    }
  }

  return false;
}

bool RecaptchaImageCaptchaProvider::HasLivenessButton(CefRefPtr<CefBrowser> browser) {
  LOG_DEBUG("reCAPTCHA", "HasLivenessButton: Checking for liveness button via IPC");

  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (!tracker) {
    LOG_WARN("reCAPTCHA", "HasLivenessButton: Render tracker not available");
    return false;
  }

  // Scan the challenge frame to find the liveness button
  RescanChallengeFrame(browser, current_context_id_);

  std::string challenge_context_id = current_context_id_ + "_recaptcha_challenge";
  std::vector<ElementRenderInfo> elements = tracker->GetAllVisibleElements(challenge_context_id);

  LOG_DEBUG("reCAPTCHA", "HasLivenessButton: Checking " + std::to_string(elements.size()) + " elements");

  for (const auto& elem : elements) {
    // Look for VISIBLE liveness button by id or class (must have size > 0)
    bool is_liveness_button = (elem.id == "recaptcha-liveness-button" ||
                               elem.className.find("rc-button-liveness") != std::string::npos);

    if (is_liveness_button && elem.width > 0 && elem.height > 0) {
      LOG_DEBUG("reCAPTCHA", "Found visible liveness button");
      return true;
    }
  }

  LOG_DEBUG("reCAPTCHA", "HasLivenessButton: Visible liveness button not found");

  return false;
}

CefRefPtr<CefFrame> RecaptchaImageCaptchaProvider::GetChallengeFrame(CefRefPtr<CefBrowser> browser) {
  if (!browser) return nullptr;

  // Get all frames and find the challenge frame (bframe)
  std::vector<CefString> frame_ids;
  browser->GetFrameIdentifiers(frame_ids);

  for (const CefString& frame_id : frame_ids) {
    CefRefPtr<CefFrame> frame = browser->GetFrameByIdentifier(frame_id);
    if (frame) {
      std::string url = frame->GetURL().ToString();
      // Challenge frame URL contains "bframe"
      if (url.find("recaptcha") != std::string::npos && url.find("bframe") != std::string::npos) {
        LOG_DEBUG("reCAPTCHA", "Found challenge frame: " + url);
        return frame;
      }
    }
  }

  return nullptr;
}

void RecaptchaImageCaptchaProvider::RescanChallengeFrame(CefRefPtr<CefBrowser> browser,
                                                         const std::string& context_id) {
  CefRefPtr<CefFrame> challenge_frame = GetChallengeFrame(browser);
  if (!challenge_frame) {
    LOG_WARN("reCAPTCHA", "RescanChallengeFrame: No challenge frame found");
    return;
  }

  std::string challenge_context_id = context_id + "_recaptcha_challenge";

  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (!tracker) return;

  // Clear previous data
  tracker->ClearContext(challenge_context_id);

  // Request fresh scan via IPC to renderer process
  CefRefPtr<CefProcessMessage> scan_msg = CefProcessMessage::Create("scan_element");
  CefRefPtr<CefListValue> args = scan_msg->GetArgumentList();
  args->SetString(0, challenge_context_id);
  args->SetString(1, "*");  // Scan all elements
  challenge_frame->SendProcessMessage(PID_RENDERER, scan_msg);

  LOG_DEBUG("reCAPTCHA", "Triggered IPC scan for challenge frame context: " + challenge_context_id);

  // Wait for scan completion - pump message loop
  for (int i = 0; i < 30; i++) {
    OwlBrowserManager::PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}


void RecaptchaImageCaptchaProvider::RescanMainFrame(CefRefPtr<CefBrowser> browser,
                                                     const std::string& context_id) {
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (!tracker) return;

  // Request fresh scan via IPC to renderer process
  CefRefPtr<CefProcessMessage> scan_msg = CefProcessMessage::Create("scan_element");
  CefRefPtr<CefListValue> args = scan_msg->GetArgumentList();
  args->SetString(0, context_id);
  args->SetString(1, "*");  // Scan all elements
  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, scan_msg);

  LOG_DEBUG("reCAPTCHA", "Triggered IPC scan for main frame context: " + context_id);

  // Wait for scan completion - pump message loop
  for (int i = 0; i < 20; i++) {
    OwlBrowserManager::PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

bool RecaptchaImageCaptchaProvider::GetBframePosition(CefRefPtr<CefBrowser> browser,
                                                       const std::string& context_id,
                                                       int& bframe_x, int& bframe_y,
                                                       int& bframe_w, int& bframe_h) {
  bframe_x = bframe_y = 0;
  bframe_w = 400;
  bframe_h = 580;

  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (!tracker) return false;

  std::vector<ElementRenderInfo> main_elements = tracker->GetAllVisibleElements(context_id);
  LOG_DEBUG("reCAPTCHA", "Scanning " + std::to_string(main_elements.size()) + " elements for bframe");

  for (const auto& elem : main_elements) {
    std::string tag_upper = elem.tag;
    std::transform(tag_upper.begin(), tag_upper.end(), tag_upper.begin(), ::toupper);

    // bframe iframe is typically 400x580 or larger
    if (tag_upper == "IFRAME" && elem.width > 350 && elem.height > 400) {
      bframe_x = elem.x;
      bframe_y = elem.y;
      bframe_w = elem.width;
      bframe_h = elem.height;
      LOG_DEBUG("reCAPTCHA", "Found bframe at: " + std::to_string(bframe_x) + "," + std::to_string(bframe_y));
      return true;
    }
  }

  // If not found, try rescanning main frame
  RescanMainFrame(browser, context_id);

  main_elements = tracker->GetAllVisibleElements(context_id);
  for (const auto& elem : main_elements) {
    std::string tag_upper = elem.tag;
    std::transform(tag_upper.begin(), tag_upper.end(), tag_upper.begin(), ::toupper);

    if (tag_upper == "IFRAME" && elem.width > 350 && elem.height > 400) {
      bframe_x = elem.x;
      bframe_y = elem.y;
      bframe_w = elem.width;
      bframe_h = elem.height;
      LOG_DEBUG("reCAPTCHA", "Found bframe after rescan");
      return true;
    }
  }

  LOG_WARN("reCAPTCHA", "Could not find bframe position");
  return false;
}

// ============================================================================
// GRID CHALLENGE METHODS (Minimal implementation - focus is on liveness)
// ============================================================================

std::string RecaptchaImageCaptchaProvider::ExtractTarget(
    const std::string& context_id,
    CefRefPtr<CefBrowser> browser,
    const CaptchaClassificationResult& classification) {

  std::string script = R"JS(
    (function() {
      var frames = document.querySelectorAll('iframe');
      for (var i = 0; i < frames.length; i++) {
        try {
          var doc = frames[i].contentDocument || frames[i].contentWindow.document;
          var prompt = doc.querySelector('.rc-imageselect-desc-no-canonical, .rc-imageselect-desc');
          if (prompt) return prompt.textContent;
        } catch(e) {}
      }
      return '';
    })();
  )JS";

  return ExecuteJavaScript(browser, script);
}

std::vector<uint8_t> RecaptchaImageCaptchaProvider::CaptureGridScreenshot(
    CefRefPtr<CefBrowser> browser,
    const std::string& context_id) {

  auto* manager = OwlBrowserManager::GetInstance();
  return manager->Screenshot(context_id);
}

bool RecaptchaImageCaptchaProvider::ClickGridItem(
    CefRefPtr<CefBrowser> browser,
    const std::string& context_id,
    int grid_index) {

  std::string script = R"JS(
    (function(index) {
      var frames = document.querySelectorAll('iframe');
      for (var i = 0; i < frames.length; i++) {
        try {
          var doc = frames[i].contentDocument || frames[i].contentWindow.document;
          var tiles = doc.querySelectorAll('.rc-image-tile-wrapper');
          if (tiles.length > index) {
            tiles[index].click();
            return 'clicked';
          }
        } catch(e) {}
      }
      return 'not_found';
    })()JS" + std::to_string(grid_index) + R"JS();)JS";

  std::string result = ExecuteJavaScript(browser, script);
  return result == "clicked";
}

bool RecaptchaImageCaptchaProvider::SubmitVerification(
    CefRefPtr<CefBrowser> browser,
    const std::string& context_id) {

  std::string script = R"JS(
    (function() {
      var frames = document.querySelectorAll('iframe');
      for (var i = 0; i < frames.length; i++) {
        try {
          var doc = frames[i].contentDocument || frames[i].contentWindow.document;
          var btn = doc.querySelector('#recaptcha-verify-button');
          if (btn) {
            btn.click();
            return 'clicked';
          }
        } catch(e) {}
      }
      return 'not_found';
    })();
  )JS";

  std::string result = ExecuteJavaScript(browser, script);
  return result == "clicked";
}

bool RecaptchaImageCaptchaProvider::SkipChallenge(
    CefRefPtr<CefBrowser> browser,
    const std::string& context_id) {

  // Click reload button to get a new challenge
  std::string script = R"JS(
    (function() {
      var frames = document.querySelectorAll('iframe');
      for (var i = 0; i < frames.length; i++) {
        try {
          var doc = frames[i].contentDocument || frames[i].contentWindow.document;
          var btn = doc.querySelector('#recaptcha-reload-button');
          if (btn) {
            btn.click();
            return 'clicked';
          }
        } catch(e) {}
      }
      return 'not_found';
    })();
  )JS";

  std::string result = ExecuteJavaScript(browser, script);
  return result == "clicked";
}

std::vector<int> RecaptchaImageCaptchaProvider::IdentifyMatchingImages(
    const std::vector<uint8_t>& grid_screenshot,
    const std::string& target_description,
    int grid_size,
    OwlLLMClient* llm_client) {

  // Use base class implementation for grid challenges
  return ImageCaptchaProviderBase::IdentifyMatchingImages(
      grid_screenshot, target_description, grid_size, llm_client);
}
