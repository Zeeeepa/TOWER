#include "owl_image_captcha_solver.h"
#include "owl_captcha_utils.h"
#include "owl_llm_client.h"
#include "owl_semantic_matcher.h"
#include "owl_client.h"
#include "owl_render_tracker.h"
#include "owl_browser_manager.h"
#include "owl_native_screenshot.h"
#include "logger.h"
#include <sstream>
#include <fstream>
#include <algorithm>
#include <random>
#include <ctime>
#include <set>
#include <thread>
#include <chrono>

// Helper: Draw a filled rectangle on BGRA buffer
static void DrawFilledRect(uint8_t* bgra, int img_w, int img_h, int x, int y, int w, int h,
                           uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
  for (int py = y; py < y + h && py < img_h; py++) {
    for (int px = x; px < x + w && px < img_w; px++) {
      if (px >= 0 && py >= 0) {
        int idx = (py * img_w + px) * 4;
        bgra[idx + 0] = b;
        bgra[idx + 1] = g;
        bgra[idx + 2] = r;
        bgra[idx + 3] = a;
      }
    }
  }
}

// Helper: Draw a clean digit on BGRA buffer (smooth, sans-serif style)
static void DrawDigit(uint8_t* bgra, int img_w, int img_h, int x, int y, int digit,
                      uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
  // Clean 7x9 bitmap font for digits 0-9 (sans-serif inspired)
  static const uint8_t font[10][9] = {
    {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C}, // 0
    {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E}, // 1
    {0x3C, 0x66, 0x06, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E}, // 2
    {0x3C, 0x66, 0x06, 0x06, 0x1C, 0x06, 0x06, 0x66, 0x3C}, // 3
    {0x0C, 0x1C, 0x2C, 0x4C, 0x4C, 0x7E, 0x0C, 0x0C, 0x0C}, // 4
    {0x7E, 0x60, 0x60, 0x7C, 0x06, 0x06, 0x06, 0x66, 0x3C}, // 5
    {0x1C, 0x30, 0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x3C}, // 6
    {0x7E, 0x06, 0x0C, 0x0C, 0x18, 0x18, 0x30, 0x30, 0x30}, // 7
    {0x3C, 0x66, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x66, 0x3C}, // 8
    {0x3C, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0C, 0x38}  // 9
  };

  if (digit < 0 || digit > 9) return;

  for (int row = 0; row < 9; row++) {
    for (int col = 0; col < 7; col++) {
      if (font[digit][row] & (1 << (6 - col))) {
        int px = x + col;
        int py = y + row;
        if (px >= 0 && px < img_w && py >= 0 && py < img_h) {
          int idx = (py * img_w + px) * 4;
          bgra[idx + 0] = b;
          bgra[idx + 1] = g;
          bgra[idx + 2] = r;
          bgra[idx + 3] = a;
        }
      }
    }
  }
}

// Draw numbered overlays on BGRA pixel buffer
static void DrawNumberedOverlaysOnBGRA(uint8_t* bgra, int img_w, int img_h,
                                       const std::vector<ElementRenderInfo>& grid_items,
                                       int crop_x, int crop_y) {
  for (size_t i = 0; i < grid_items.size() && i < 9; i++) {
    // Calculate position relative to cropped image
    int local_x = grid_items[i].x - crop_x + 5;  // 5px padding from left
    int local_y = grid_items[i].y - crop_y + 5;  // 5px padding from top

    // Draw red background badge (20x18 pixels - tighter fit)
    DrawFilledRect(bgra, img_w, img_h, local_x, local_y, 20, 18, 0, 0, 220, 230);  // Red background

    // Draw white border (2px)
    DrawFilledRect(bgra, img_w, img_h, local_x, local_y, 20, 2, 255, 255, 255, 255);  // Top
    DrawFilledRect(bgra, img_w, img_h, local_x, local_y + 16, 20, 2, 255, 255, 255, 255);  // Bottom
    DrawFilledRect(bgra, img_w, img_h, local_x, local_y, 2, 18, 255, 255, 255, 255);  // Left
    DrawFilledRect(bgra, img_w, img_h, local_x + 18, local_y, 2, 18, 255, 255, 255, 255);  // Right

    // Draw digit in white (centered: 7px width + 6.5px padding = 20px total)
    DrawDigit(bgra, img_w, img_h, local_x + 7, local_y + 4, i, 255, 255, 255, 255);
  }
}

OlibImageCaptchaSolver::OlibImageCaptchaSolver(OwlLLMClient* llm_client)
    : llm_client_(llm_client), auto_submit_(true), allow_skip_(true) {
  LOG_DEBUG("ImageCaptchaSolver", "Initialized");
}

OlibImageCaptchaSolver::~OlibImageCaptchaSolver() {}

ImageCaptchaSolveResult OlibImageCaptchaSolver::Solve(
    const std::string& context_id,
    CefRefPtr<CefBrowser> browser,
    const CaptchaClassificationResult& classification,
    int max_attempts) {

  LOG_DEBUG("ImageCaptchaSolver", "Starting image CAPTCHA solve (max attempts: " +
           std::to_string(max_attempts) + ")");

  ImageCaptchaSolveResult result;
  result.success = false;
  result.confidence = 0.0;
  result.attempts = 0;
  result.needs_skip = false;

  if (!llm_client_) {
    result.error_message = "LLM client not available";
    LOG_ERROR("ImageCaptchaSolver", result.error_message);
    return result;
  }

  if (classification.type != CaptchaType::IMAGE_SELECTION) {
    result.error_message = "Not an image-selection CAPTCHA";
    LOG_ERROR("ImageCaptchaSolver", result.error_message);
    return result;
  }

  if (classification.grid_items.empty()) {
    result.error_message = "No grid items found";
    LOG_ERROR("ImageCaptchaSolver", result.error_message);
    return result;
  }

  // Note: Target will be extracted inside the attempt loop after each skip/reload

  // CRITICAL: Unfreeze frame FIRST so element scanner can track current DOM state
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  client->UnfreezeFrameCache();
  LOG_DEBUG("ImageCaptchaSolver", "Frame unfrozen for auto-verify detection");

  // Wait a moment for element scanner to update
  OwlCaptchaUtils::Wait(300);

  // Check if CAPTCHA already auto-verified
  // IMPORTANT: First wait for challenge to appear, THEN check if it disappears
  if (!classification.challenge_element.empty()) {
    LOG_DEBUG("ImageCaptchaSolver", "Waiting for challenge to appear or auto-verify");

    // Poll to see if challenge appears or "Verified" indicator shows
    const int max_polls = 30;  // 30 * 200ms = 6 seconds
    bool challenge_appeared = false;
    bool verified_shown = false;

    for (int i = 0; i < max_polls; i++) {
      // Check if captcha-text shows "Verified" FIRST (priority check)
      // Use semantic matcher instead of stub GetTextContent()
      std::string captcha_text = "";
      OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
      if (matcher) {
        std::vector<ElementSemantics> elements = matcher->GetAllElements(context_id);
        for (const auto& elem : elements) {
          if (elem.selector.find(".captcha-text") != std::string::npos ||
              elem.selector.find("captcha-text") != std::string::npos) {
            captcha_text = elem.text;
            break;
          }
        }
      }

      LOG_DEBUG("ImageCaptchaSolver", "Poll " + std::to_string(i) + ": .captcha-text = '" + captcha_text + "'");
      bool is_verified = (captcha_text.find("Verified") != std::string::npos);

      if (is_verified) {
        LOG_DEBUG("ImageCaptchaSolver", "Verified indicator found at poll " + std::to_string(i) + " (text: '" + captcha_text + "')");
        verified_shown = true;
        break;
      }

      // Only check challenge visibility if not verified
      bool challenge_visible = OwlCaptchaUtils::IsVisible(browser, classification.challenge_element);

      if (challenge_visible) {
        LOG_DEBUG("ImageCaptchaSolver", "Challenge appeared at poll " + std::to_string(i));
        // Wait one more cycle and re-check verification before proceeding
        OwlCaptchaUtils::Wait(300);

        // Re-check with semantic matcher
        captcha_text = "";
        if (matcher) {
          std::vector<ElementSemantics> elements = matcher->GetAllElements(context_id);
          for (const auto& elem : elements) {
            if (elem.selector.find(".captcha-text") != std::string::npos ||
                elem.selector.find("captcha-text") != std::string::npos) {
              captcha_text = elem.text;
              break;
            }
          }
        }

        LOG_DEBUG("ImageCaptchaSolver", "After wait: .captcha-text = '" + captcha_text + "'");
        if (captcha_text.find("Verified") != std::string::npos) {
          LOG_DEBUG("ImageCaptchaSolver", "Verified immediately after challenge appeared");
          verified_shown = true;
        } else {
          challenge_appeared = true;
        }
        break;
      }

      OwlCaptchaUtils::Wait(200);
    }

    if (verified_shown) {
      LOG_DEBUG("ImageCaptchaSolver", "CAPTCHA auto-verified - verified indicator shown");
      result.success = true;
      result.confidence = 1.0;
      result.attempts = 0;
      return result;
    }

    if (!challenge_appeared) {
      LOG_DEBUG("ImageCaptchaSolver", "CAPTCHA auto-verified - challenge never appeared after 6s");
      result.success = true;
      result.confidence = 0.8;  // Lower confidence since we're assuming
      result.attempts = 0;
      return result;
    }

    LOG_DEBUG("ImageCaptchaSolver", "Challenge appeared, proceeding with solve");
  }

  // Frame already unfrozen earlier for auto-verify detection

  // CRITICAL: Click checkbox FIRST if it exists (triggers challenge grid)
  // Without this, grid items will never appear!
  if (!classification.checkbox_selector.empty()) {
    LOG_DEBUG("ImageCaptchaSolver", "Clicking checkbox to trigger challenge: " +
             classification.checkbox_selector);

    bool checkbox_clicked = false;

    // Try multiple selectors - the checkbox input is often hidden (size 0x0)
    // so we need to click its label or container instead
    std::vector<std::string> selectors_to_try = {
      ".captcha-checkbox-label",           // Label for the hidden checkbox
      ".captcha-checkbox",                 // Container div
      "label[for='captchaCheck']",         // Label by for attribute
      classification.checkbox_selector     // The actual checkbox (may be hidden)
    };

    for (const auto& selector : selectors_to_try) {
      LOG_DEBUG("ImageCaptchaSolver", "Trying to click: " + selector);

      // Check if element exists and has valid size
      OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
      if (tracker) {
        auto elements = tracker->GetAllVisibleElements(context_id);
        for (const auto& el : elements) {
          // Match by selector (check if selector contains our target)
          if (el.selector.find(selector) != std::string::npos ||
              (selector[0] == '.' && el.selector.find(selector.substr(1)) != std::string::npos)) {
            // Check if element has valid size (not hidden)
            if (el.width > 0 && el.height > 0) {
              LOG_DEBUG("ImageCaptchaSolver", "Found visible element: " + el.selector +
                        " at (" + std::to_string(el.x) + "," + std::to_string(el.y) +
                        ") size: " + std::to_string(el.width) + "x" + std::to_string(el.height));
              checkbox_clicked = OwlCaptchaUtils::ClickElement(browser, context_id, el.selector);
              if (checkbox_clicked) {
                LOG_DEBUG("ImageCaptchaSolver", "Clicked element: " + el.selector);
                break;
              }
            }
          }
        }
        if (checkbox_clicked) break;
      }
    }

    // If still not clicked, try semantic matcher as last resort
    if (!checkbox_clicked) {
      LOG_DEBUG("ImageCaptchaSolver", "CSS selectors failed, trying semantic match");
      OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
      if (matcher) {
        std::vector<std::string> captcha_descriptions = {
          "captcha checkbox",
          "I'm not a robot",
          "verify you are human"
        };

        for (const auto& desc : captcha_descriptions) {
          std::vector<ElementMatch> elements = matcher->FindByDescription(context_id, desc, 3);
          if (!elements.empty() && elements[0].element.width > 0) {
            LOG_DEBUG("ImageCaptchaSolver", "Found via semantic: " + elements[0].element.selector);
            checkbox_clicked = OwlCaptchaUtils::ClickElement(browser, context_id, elements[0].element.selector);
            if (checkbox_clicked) break;
          }
        }
      }
    }

    if (checkbox_clicked) {
      LOG_DEBUG("ImageCaptchaSolver", "Checkbox clicked successfully");
      OwlCaptchaUtils::Wait(1000);
    } else {
      LOG_WARN("ImageCaptchaSolver", "Failed to click checkbox");
    }
  }

  // CRITICAL: Check if grid items actually exist (not just empty container)
  // An empty #imageGrid container still counts as "visible" even without challenge
  LOG_DEBUG("ImageCaptchaSolver", "Checking for challenge grid items");
  bool has_grid_items = false;
  const int max_grid_polls = 15;  // 15 * 300ms = 4.5 seconds max

  for (int i = 0; i < max_grid_polls; i++) {
    // CRITICAL: Trigger fresh DOM scan to get current state, not stale data
    // Without this, IsVisible uses old scanner data from before auto-verify
    LOG_DEBUG("ImageCaptchaSolver", "Triggering DOM scan for fresh grid state (poll " + std::to_string(i) + ")");
    client->WaitForElementScan(browser, context_id, 1000);

    // Now check render tracker for actual grid items with className
    OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
    std::vector<ElementRenderInfo> all_elements = tracker->GetAllVisibleElements(context_id);

    int grid_count = 0;
    for (const auto& elem : all_elements) {
      if (elem.className.find("grid-item") != std::string::npos) {
        grid_count++;
      }
    }

    LOG_DEBUG("ImageCaptchaSolver", "Poll " + std::to_string(i) + ": Found " +
              std::to_string(grid_count) + " grid items in fresh scan");

    if (grid_count >= 9) {
      has_grid_items = true;
      LOG_DEBUG("ImageCaptchaSolver", "Grid items found after " + std::to_string(i * 300) + "ms");
      break;
    }

    OwlCaptchaUtils::Wait(300);
  }

  if (!has_grid_items) {
    LOG_DEBUG("ImageCaptchaSolver", "CAPTCHA already verified - no challenge grid items detected");
    result.success = true;
    result.confidence = 1.0;  // High confidence - definitely verified
    result.attempts = 0;  // 0 attempts means auto-verified
    return result;
  }

  LOG_DEBUG("ImageCaptchaSolver", "Challenge grid items detected, proceeding with solve");

  // Attempt to solve with retries
  for (int attempt = 0; attempt < max_attempts; attempt++) {
    result.attempts++;
    LOG_DEBUG("ImageCaptchaSolver", "Attempt " + std::to_string(attempt + 1) + "/" +
             std::to_string(max_attempts));

    // CRITICAL: Ensure frame is completely unfrozen before this attempt
    CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
    OwlClient* client = static_cast<OwlClient*>(client_base.get());
    client->UnfreezeFrameCache();
    LOG_DEBUG("ImageCaptchaSolver", "Frame unfrozen");

    // CRITICAL: Poll for grid items to actually exist in DOM using JavaScript
    // After Skip or initial load, JavaScript needs time to generate grid items
    LOG_DEBUG("ImageCaptchaSolver", "Polling for grid items to exist in DOM");
    bool grid_items_exist = false;
    const int max_grid_polls = 30;  // 30 * 200ms = 6 seconds max

    for (int i = 0; i < max_grid_polls; i++) {
      // Use JavaScript to count grid items directly (fast, no DOM scan needed)
      std::string check_script = R"(
        (function() {
          return document.querySelectorAll('.grid-item').length;
        })();
      )";

      // Execute JavaScript and check count
      CefRefPtr<CefFrame> frame = browser->GetMainFrame();
      // For now, use simple visibility check as proxy
      // (ExecuteJavaScriptAndGetResult would be async, so we use IsVisible)
      if (OwlCaptchaUtils::IsVisible(browser, ".grid-item")) {
        LOG_DEBUG("ImageCaptchaSolver", "Grid items detected in DOM after " +
                  std::to_string(i * 200) + "ms");
        grid_items_exist = true;
        break;
      }

      OwlCaptchaUtils::Wait(200);
    }

    if (!grid_items_exist) {
      LOG_ERROR("ImageCaptchaSolver", "Grid items never appeared in DOM after 6 seconds");
      result.error_message = "Grid items failed to load";
      continue;
    }

    // Additional wait to ensure all 9 grid items and their images are fully loaded
    LOG_DEBUG("ImageCaptchaSolver", "Grid detected, waiting for all items and images to load");
    OwlCaptchaUtils::Wait(800);

    // CRITICAL: Scroll grid into view AFTER confirming it exists
    LOG_DEBUG("ImageCaptchaSolver", "Scrolling grid into view");
    std::string scroll_script = R"(
      (function() {
        const grid = document.getElementById('imageGrid');
        if (grid) {
          const rect = grid.getBoundingClientRect();
          const padding = 150;
          const targetY = window.scrollY + rect.top - padding;
          window.scrollTo({top: Math.max(0, targetY), behavior: 'instant'});
        }
      })();
    )";
    browser->GetMainFrame()->ExecuteJavaScript(scroll_script, browser->GetMainFrame()->GetURL(), 0);
    LOG_DEBUG("ImageCaptchaSolver", "Waiting for scroll to complete");
    OwlCaptchaUtils::Wait(400);  // Wait for scroll

    // Trigger final DOM scan with grid in correct position and fully loaded
    LOG_DEBUG("ImageCaptchaSolver", "Triggering final DOM scan");
    client->WaitForElementScan(browser, context_id, 3000);
    LOG_DEBUG("ImageCaptchaSolver", "Element scan complete");

    // CRITICAL: Freeze frame cache NOW to capture current scrolled viewport
    // This ensures CaptureGridScreenshot reads the post-scroll frame, not pre-scroll
    LOG_DEBUG("ImageCaptchaSolver", "Freezing frame cache to capture scrolled viewport");
    client->FreezeFrameCache();

    // Wait for freeze to take effect and ensure we have captured current frame
    LOG_DEBUG("ImageCaptchaSolver", "Waiting for frame cache freeze to complete");
    OwlCaptchaUtils::Wait(200);

    // Extract target from semantic matcher (with fresh scan data)
    std::string current_target = ExtractTargetFromChallenge(context_id, browser, classification.challenge_element);

    // Fallback to classification if extraction fails
    if (current_target.empty()) {
      LOG_WARN("ImageCaptchaSolver", "Failed to extract target from page, using classification");
      current_target = classification.target_description;
    }

    // Final fallback
    if (current_target.empty()) {
      LOG_WARN("ImageCaptchaSolver", "No target description available, using generic fallback");
      current_target = "objects";
    }

    // Update result with current target
    result.target_detected = current_target;
    LOG_DEBUG("ImageCaptchaSolver", "Current target for attempt " + std::to_string(attempt + 1) + ": '" + result.target_detected + "'");

    // Capture screenshot with numbered overlays
    // CaptureGridScreenshot now handles: wait for images, add overlays, capture, remove overlays
    LOG_DEBUG("ImageCaptchaSolver", "Capturing grid screenshot with numbered overlays");
    std::vector<uint8_t> grid_screenshot = CaptureGridScreenshot(browser, context_id, "#imageGrid");

    if (grid_screenshot.empty()) {
      LOG_ERROR("ImageCaptchaSolver", "Failed to capture grid screenshot");
      result.error_message = "Failed to capture grid";
      continue;
    }

    LOG_DEBUG("ImageCaptchaSolver", "Grid screenshot captured: " +
              std::to_string(grid_screenshot.size()) + " bytes");

    // Save numbered grid screenshot for debugging (only in debug builds)
#ifdef OWL_DEBUG_BUILD
    std::string debug_path = "/tmp/captcha_numbered_grid_attempt_" + std::to_string(attempt + 1) + ".png";
    std::ofstream debug_file(debug_path, std::ios::binary);
    if (debug_file.is_open()) {
      debug_file.write(reinterpret_cast<const char*>(grid_screenshot.data()), grid_screenshot.size());
      debug_file.close();
      LOG_DEBUG("ImageCaptchaSolver", "Saved numbered grid to " + debug_path);
    }
#endif

    LOG_DEBUG("ImageCaptchaSolver", "Attempt " + std::to_string(attempt + 1) + "/" + std::to_string(max_attempts) +
             " - Target: '" + result.target_detected + "'");

    // Identify matching images using vision model (one-shot)
    std::vector<int> matching_indices = IdentifyMatchingImages(
        grid_screenshot,
        result.target_detected,
        classification.grid_size
    );

    // Overlays already removed inside CaptureGridScreenshot

    // Log what vision model selected
    std::stringstream indices_str;
    if (!matching_indices.empty()) {
      indices_str << "[";
      for (size_t i = 0; i < matching_indices.size(); i++) {
        if (i > 0) indices_str << ", ";
        indices_str << matching_indices[i];
      }
      indices_str << "]";
      LOG_DEBUG("ImageCaptchaSolver", "Vision model selected: " + indices_str.str() +
               " for target '" + result.target_detected + "'");
    } else {
      indices_str << "[]";
      LOG_DEBUG("ImageCaptchaSolver", "Vision model selected: [] (no matches) for target '" +
               result.target_detected + "'");
    }

    // Save attempt details to text file (only in debug builds)
#ifdef OWL_DEBUG_BUILD
    std::string txt_path = "/tmp/captcha_numbered_grid_attempt_" + std::to_string(attempt + 1) + ".txt";
    std::ofstream txt_file(txt_path);
    if (txt_file.is_open()) {
      txt_file << "CAPTCHA Solve Attempt " << (attempt + 1) << "/" << max_attempts << "\n";
      txt_file << "Target: " << result.target_detected << "\n";
      txt_file << "Grid Size: " << classification.grid_size << " squares\n";
      txt_file << "Selected Indices: " << indices_str.str() << "\n";
      txt_file.close();
      LOG_DEBUG("ImageCaptchaSolver", "Saved attempt details to " + txt_path);
    }
#endif

    if (matching_indices.empty()) {
      LOG_WARN("ImageCaptchaSolver", "Vision model found no matching images");

      // If allowed, skip to get a new challenge
      if (allow_skip_ && !classification.skip_button.empty()) {
        LOG_DEBUG("ImageCaptchaSolver", "No matches found - skipping to get new challenge");
        result.needs_skip = true;

        // Click skip button - JavaScript will call showChallenge() -> generateImages()
        bool skipped = SkipChallenge(browser, context_id, classification.skip_button);
        if (skipped) {
          LOG_DEBUG("ImageCaptchaSolver", "Skip clicked, continuing to next attempt");
          // Next attempt will poll for grid visibility
          continue;
        }
      }

      result.error_message = "No matching images found";
      continue;
    }

    result.selected_indices = matching_indices;
    LOG_DEBUG("ImageCaptchaSolver", "Vision model identified " +
             std::to_string(matching_indices.size()) + " matching images");

    // Randomize click order to avoid linear/grid pattern detection
    std::vector<int> shuffled_indices = matching_indices;
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::shuffle(shuffled_indices.begin(), shuffled_indices.end(), gen);

    // IMPORTANT: Wait before first click (600-1000ms) to simulate human thinking time
    // Bot detection penalizes clicks < 500ms after challenge appears
    std::uniform_int_distribution<> delay_dist(600, 1000);
    int first_click_delay = delay_dist(gen);
    LOG_DEBUG("ImageCaptchaSolver", "Waiting " + std::to_string(first_click_delay) + "ms before first click (human-like)");
    OwlCaptchaUtils::Wait(first_click_delay);

    // Highlight and click each matching grid item with human-like timing
    for (size_t i = 0; i < shuffled_indices.size(); i++) {
      int index = shuffled_indices[i];

      if (index < 0 || index >= static_cast<int>(classification.grid_items.size())) {
        LOG_WARN("ImageCaptchaSolver", "Invalid grid index: " + std::to_string(index));
        continue;
      }

      LOG_DEBUG("ImageCaptchaSolver", "Clicking grid item " + std::to_string(index) +
                " (" + std::to_string(i + 1) + "/" + std::to_string(shuffled_indices.size()) + ")");

      // Highlight first for visual feedback
      HighlightGridItem(browser, index, classification.grid_items);
      OwlCaptchaUtils::Wait(150);

      // Click the item
      bool clicked = ClickGridItem(browser, context_id, index, classification.grid_items);

      if (!clicked) {
        LOG_WARN("ImageCaptchaSolver", "Failed to click grid item " + std::to_string(index));
      }

      // IMPORTANT: Add random delays between clicks (200-450ms) to simulate human behavior
      // This creates variance in timing and prevents bot detection from flagging consistent intervals
      if (i < shuffled_indices.size() - 1) {  // Don't wait after last click
        std::uniform_int_distribution<> interval_dist(200, 450);
        int click_interval = interval_dist(gen);
        LOG_DEBUG("ImageCaptchaSolver", "Waiting " + std::to_string(click_interval) + "ms before next click");
        OwlCaptchaUtils::Wait(click_interval);
      }
    }

    LOG_DEBUG("ImageCaptchaSolver", "All grid items clicked");

    // Submit verification if auto-submit enabled
    if (auto_submit_ && !classification.submit_button.empty()) {
      LOG_DEBUG("ImageCaptchaSolver", "Submitting verification");

      OwlCaptchaUtils::Wait(500);  // Small delay before submit

      bool submitted = SubmitVerification(browser, context_id, classification.submit_button);

      if (!submitted) {
        LOG_ERROR("ImageCaptchaSolver", "Failed to submit verification");
        result.error_message = "Failed to submit";
        continue;
      }

      // Wait and check for verification success
      OwlCaptchaUtils::Wait(2000);  // Wait for verification

      bool success = CheckVerificationSuccess(context_id, browser, classification.challenge_element);

      if (success) {
        LOG_DEBUG("ImageCaptchaSolver", "CAPTCHA solved on attempt " + std::to_string(attempt + 1) +
                 "/" + std::to_string(max_attempts) + " (" + std::to_string(matching_indices.size()) + " squares)");
        result.success = true;
        result.confidence = 0.85;  // Good confidence if verification passed
        result.attempts = attempt + 1;  // Record which attempt succeeded
        return result;
      } else {
        LOG_WARN("ImageCaptchaSolver", "Attempt " + std::to_string(attempt + 1) + " verification failed, skipping to get new challenge");

        // Skip to get a new challenge instead of retrying the same one
        if (allow_skip_ && !classification.skip_button.empty()) {
          LOG_DEBUG("ImageCaptchaSolver", "Clicking skip for new challenge");
          bool skipped = SkipChallenge(browser, context_id, classification.skip_button);
          if (skipped) {
            LOG_DEBUG("ImageCaptchaSolver", "Skip clicked, continuing to next attempt");
            // Next attempt will poll for grid visibility
          }
        }
      }
    } else {
      // Not auto-submitting, consider success
      LOG_DEBUG("ImageCaptchaSolver", "Items selected (auto-submit disabled)");
      result.success = true;
      result.confidence = 0.7;  // Moderate confidence without verification
      return result;
    }
  }

  // All attempts failed
  result.error_message = "All attempts failed";
  result.needs_skip = true;
  LOG_ERROR("ImageCaptchaSolver", "CAPTCHA solve failed after " + std::to_string(max_attempts) + " attempts");

  return result;
}

bool OlibImageCaptchaSolver::ScrollChallengeIntoView(
    CefRefPtr<CefBrowser> browser,
    const std::string& challenge_selector) {

  LOG_DEBUG("ImageCaptchaSolver", "Scrolling challenge into view: " + challenge_selector);

  return OwlCaptchaUtils::ScrollIntoView(browser, challenge_selector, "center");
}

std::vector<uint8_t> OlibImageCaptchaSolver::CaptureGridScreenshot(
    CefRefPtr<CefBrowser> browser,
    const std::string& context_id,
    const std::string& grid_selector) {

  LOG_DEBUG("ImageCaptchaSolver", "Capturing grid screenshot: " + grid_selector);

  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();

  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  if (!client_base) {
    LOG_ERROR("ImageCaptchaSolver", "Failed to get browser client");
    return std::vector<uint8_t>();
  }
  OwlClient* client = static_cast<OwlClient*>(client_base.get());

  // Get grid item positions from render tracker (already scanned and fresh)
  std::vector<ElementRenderInfo> all_elements = tracker->GetAllVisibleElements(context_id);

  LOG_DEBUG("ImageCaptchaSolver", "Found " + std::to_string(all_elements.size()) +
            " total elements in render tracker");

#ifdef OWL_DEBUG_BUILD
  // DEBUG: Log first 15 elements with their className
  LOG_DEBUG("ImageCaptchaSolver", "First 15 elements:");
  for (size_t i = 0; i < std::min(size_t(15), all_elements.size()); i++) {
    const auto& elem = all_elements[i];
    LOG_DEBUG("ImageCaptchaSolver", "  [" + std::to_string(i) + "] tag=" + elem.tag +
              " id='" + elem.id + "' class='" + elem.className +
              "' pos=(" + std::to_string(elem.x) + "," + std::to_string(elem.y) + ")");
  }
#endif

  std::vector<ElementRenderInfo> grid_items;
  std::set<std::pair<int, int>> seen_positions;

  for (const auto& elem : all_elements) {
    if (elem.className.find("grid-item") == std::string::npos) continue;

    LOG_DEBUG("ImageCaptchaSolver", "Found grid-item: tag=" + elem.tag + " class='" +
              elem.className + "' pos=(" + std::to_string(elem.x) + "," +
              std::to_string(elem.y) + ")");

    bool is_duplicate = false;
    for (const auto& seen_pos : seen_positions) {
      if (abs(elem.x - seen_pos.first) < 5 && abs(elem.y - seen_pos.second) < 5) {
        is_duplicate = true;
        break;
      }
    }
    if (!is_duplicate) {
      grid_items.push_back(elem);
      seen_positions.insert({elem.x, elem.y});
    }
  }

  if (grid_items.empty()) {
    LOG_ERROR("ImageCaptchaSolver", "No grid items found - checked " +
              std::to_string(all_elements.size()) + " elements");
    // DEBUG: Log all elements with "item" or "grid" in className
    LOG_DEBUG("ImageCaptchaSolver", "Elements with 'item' or 'grid' in className:");
    for (const auto& elem : all_elements) {
      if (elem.className.find("item") != std::string::npos ||
          elem.className.find("grid") != std::string::npos) {
        LOG_DEBUG("ImageCaptchaSolver", "  tag=" + elem.tag + " class='" + elem.className + "'");
      }
    }
    client->FreezeFrameCache();
    return std::vector<uint8_t>();
  }

  LOG_DEBUG("ImageCaptchaSolver", "Found " + std::to_string(grid_items.size()) + " grid items");

  // Sort by position
  std::sort(grid_items.begin(), grid_items.end(),
            [](const ElementRenderInfo& a, const ElementRenderInfo& b) {
              if (abs(a.y - b.y) > 10) return a.y < b.y;
              return a.x < b.x;
            });

  if (grid_items.size() > 9) grid_items.resize(9);

  // Calculate bounding box
  int min_x = grid_items[0].x;
  int min_y = grid_items[0].y;
  int max_x = grid_items[0].x + grid_items[0].width;
  int max_y = grid_items[0].y + grid_items[0].height;

  for (const auto& item : grid_items) {
    min_x = std::min(min_x, item.x);
    min_y = std::min(min_y, item.y);
    max_x = std::max(max_x, item.x + item.width);
    max_y = std::max(max_y, item.y + item.height);
  }

  const int margin = 5;
  min_x = std::max(0, min_x - margin);
  min_y = std::max(0, min_y - margin);
  int width = max_x - min_x + margin;
  int height = max_y - min_y + margin;

  LOG_DEBUG("ImageCaptchaSolver", "Grid bounds: x=" + std::to_string(min_x) +
           " y=" + std::to_string(min_y) + " w=" + std::to_string(width) +
           " h=" + std::to_string(height));

  // Choose capture method based on browser mode
  std::vector<uint8_t> bgra_pixels;

  if (OwlBrowserManager::UsesRunMessageLoop()) {
    // UI MODE: Use native macOS screenshot API (no page reload, reliable)
    LOG_DEBUG("ImageCaptchaSolver", "UI mode detected - using native macOS screenshot");

#ifdef BUILD_UI
    // Capture using native API with numbered overlays (only available in UI build)
    std::vector<uint8_t> png_data = CaptureNativeScreenshot(browser, min_x, min_y, width, height, grid_items, min_x, min_y);

    if (png_data.empty()) {
      LOG_ERROR("ImageCaptchaSolver", "Native screenshot capture failed");
      return std::vector<uint8_t>();
    }

    LOG_DEBUG("ImageCaptchaSolver", "Native screenshot with overlays successful");

    return png_data;
#else
    // UI build not available - should never reach here
    LOG_ERROR("ImageCaptchaSolver", "UI mode screenshot not available in headless build");
    return std::vector<uint8_t>();
#endif

  } else {
    // HEADLESS MODE: Use frame cache (existing approach)
    LOG_DEBUG("ImageCaptchaSolver", "Headless mode - using frame cache");

    bool success = client->GetCroppedBGRAFromCache(&bgra_pixels, min_x, min_y, width, height);

    if (!success || bgra_pixels.empty()) {
      LOG_ERROR("ImageCaptchaSolver", "Failed to capture BGRA pixels from cache");
      return std::vector<uint8_t>();
    }
  }

  // CRITICAL: GetCroppedBGRAFromCache may return a smaller buffer if requested
  // dimensions exceed frame bounds. Calculate actual dimensions from buffer size.
  size_t expected_size = static_cast<size_t>(width) * height * 4;
  if (bgra_pixels.size() < expected_size) {
    // Buffer is smaller than expected - recalculate actual dimensions
    size_t actual_pixels = bgra_pixels.size() / 4;
    int actual_width = width;
    int actual_height = static_cast<int>(actual_pixels / width);
    if (actual_height <= 0) {
      LOG_ERROR("ImageCaptchaSolver", "Buffer size mismatch: expected " +
                std::to_string(expected_size) + " got " + std::to_string(bgra_pixels.size()));
      return std::vector<uint8_t>();
    }
    width = actual_width;
    height = actual_height;
    LOG_DEBUG("ImageCaptchaSolver", "Adjusted dimensions to " +
              std::to_string(width) + "x" + std::to_string(height));
  }

  LOG_DEBUG("ImageCaptchaSolver", "Captured BGRA: " + std::to_string(width) + "x" + std::to_string(height));

  // Draw numbered overlays in C++ directly on BGRA pixels
  DrawNumberedOverlaysOnBGRA(bgra_pixels.data(), width, height, grid_items, min_x, min_y);

  LOG_DEBUG("ImageCaptchaSolver", "Added numbered overlays in C++");

  // Encode annotated BGRA to PNG
  std::vector<uint8_t> png_output;
  client->EncodePNGFromBGRA(png_output, bgra_pixels.data(), width, height);

  return png_output;
}

std::vector<int> OlibImageCaptchaSolver::IdentifyMatchingImages(
    const std::vector<uint8_t>& grid_screenshot,
    const std::string& target_description,
    int grid_size) {

  LOG_DEBUG("ImageCaptchaSolver", "Identifying images matching: '" +
            target_description + "' (grid size: " + std::to_string(grid_size) + ")");

  if (!llm_client_) {
    LOG_ERROR("ImageCaptchaSolver", "LLM client not available");
    return {};
  }

  // Convert image to base64
  std::string image_base64 = OwlCaptchaUtils::Base64Encode(grid_screenshot);

  // Create SIMPLE vision prompt optimized for small 2B model
  std::stringstream prompt_stream;

  // OUTPUT FORMAT FIRST - most important for small models
  prompt_stream << "OUTPUT ONLY NUMBERS. Example: 0,3,5\n\n";

  // Keep it VERY simple and direct for small model
  prompt_stream << "Grid image with red numbered squares.\n";

  if (grid_size == 9) {
    prompt_stream << "Numbers: 0,1,2,3,4,5,6,7,8\n\n";
  } else if (grid_size == 16) {
    prompt_stream << "Numbers: 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15\n\n";
  }

  prompt_stream << "Task: Find ALL squares that contain \"" << target_description << "\".\n\n";

  // Universal guidance with emphasis on variations and flexibility
  prompt_stream << "IMPORTANT - Select ALL instances, including:\n"
                << "✓ ANY angle (front, side, back, top, diagonal)\n"
                << "✓ ANY size (close-up, far away, small, large)\n"
                << "✓ Partial view (edge of frame, half visible)\n"
                << "✓ Different styles/colors/shapes of the same object\n"
                << "✓ Blurry or low quality images\n\n"
                << "How to identify \"" << target_description << "\":\n"
                << "- If you can tell it's a " << target_description << ", select it\n"
                << "- Different angles/perspectives still count as the same object\n"
                << "- Even small or partially visible " << target_description << " should be selected\n"
                << "- Focus on WHAT the object IS, not how it looks\n\n"
                << "Only skip if:\n"
                << "✗ Completely different object type\n"
                << "✗ Just background/scenery without the object\n\n";

  prompt_stream << "Final rules:\n"
                << "- Usually 3+ squares match (be generous, not strict)\n"
                << "- When in doubt, include it (better to over-select than miss objects)\n"
                << "- Don't explain, just output numbers\n\n"
                << "Answer with ONLY numbers (example: 0,3,5 or none):";

  std::string system_prompt = "Output only comma-separated numbers. No explanations.";

  // Call vision model
  LOG_DEBUG("ImageCaptchaSolver", "Calling vision model for grid analysis...");

  auto response = llm_client_->CompleteWithImage(
      prompt_stream.str(),
      image_base64,
      system_prompt,
      100,   // max_tokens
      0.5    // higher temperature for more flexible/generous selection
  );

  if (!response.success) {
    LOG_ERROR("ImageCaptchaSolver", "Vision model error: " + response.error);
    return {};
  }

  // Parse response
  std::string indices_str = response.content;

  // Trim whitespace
  indices_str.erase(0, indices_str.find_first_not_of(" \t\n\r"));
  indices_str.erase(indices_str.find_last_not_of(" \t\n\r") + 1);

  LOG_DEBUG("ImageCaptchaSolver", "Vision model raw response: '" + indices_str + "'");

  // Check for "none"
  std::string lower_response = indices_str;
  std::transform(lower_response.begin(), lower_response.end(), lower_response.begin(), ::tolower);

  if (lower_response.find("none") != std::string::npos) {
    LOG_DEBUG("ImageCaptchaSolver", "Vision model found no matches");
    return {};
  }

  // Parse comma-separated indices
  std::vector<int> indices;
  std::stringstream ss(indices_str);
  std::string item;

  while (std::getline(ss, item, ',')) {
    // Trim whitespace from each item
    item.erase(0, item.find_first_not_of(" \t\n\r"));
    item.erase(item.find_last_not_of(" \t\n\r") + 1);

    // Convert to integer (simple parsing without exceptions)
    if (!item.empty() && item.find_first_not_of("0123456789") == std::string::npos) {
      int index = std::atoi(item.c_str());

      // Validate index range
      if (index >= 0 && index < grid_size) {
        indices.push_back(index);
      } else {
        LOG_WARN("ImageCaptchaSolver", "Index out of range: " + std::to_string(index));
      }
    } else {
      LOG_WARN("ImageCaptchaSolver", "Failed to parse index: '" + item + "'");
    }
  }

  LOG_DEBUG("ImageCaptchaSolver", "Parsed " + std::to_string(indices.size()) +
           " matching indices");

  return indices;
}

bool OlibImageCaptchaSolver::HighlightGridItem(
    CefRefPtr<CefBrowser> browser,
    int grid_index,
    const std::vector<std::string>& grid_selectors) {

  if (grid_index < 0 || grid_index >= static_cast<int>(grid_selectors.size())) {
    return false;
  }

  std::string selector = grid_selectors[grid_index];
  LOG_DEBUG("ImageCaptchaSolver", "Highlighting grid item " + std::to_string(grid_index) +
            ": " + selector);

  return OwlCaptchaUtils::HighlightElement(browser, selector, "#4CAF50",
                                            "rgba(76, 175, 80, 0.3)", 1000);
}

bool OlibImageCaptchaSolver::ClickGridItem(
    CefRefPtr<CefBrowser> browser,
    const std::string& context_id,
    int grid_index,
    const std::vector<std::string>& grid_selectors) {

  if (grid_index < 0 || grid_index >= static_cast<int>(grid_selectors.size())) {
    return false;
  }

  LOG_DEBUG("ImageCaptchaSolver", "Clicking grid item " + std::to_string(grid_index));

  // IMPORTANT: Instead of using compound selectors like .grid-item[data-index=N],
  // get all .grid-item elements from render tracker and click by index
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  std::vector<ElementRenderInfo> all_elements = tracker->GetAllVisibleElements(context_id);

  // Filter for grid items (elements with class="grid-item")
  std::vector<ElementRenderInfo> grid_items;
  for (const auto& elem : all_elements) {
    if (elem.className.find("grid-item") != std::string::npos) {
      grid_items.push_back(elem);
    }
  }

  LOG_DEBUG("ImageCaptchaSolver", "Found " + std::to_string(grid_items.size()) + " grid items in render tracker");

  if (grid_items.empty()) {
    LOG_ERROR("ImageCaptchaSolver", "No grid items found in render tracker");
    return false;
  }

  // Sort grid items by position (top-to-bottom, left-to-right)
  // This ensures grid_items[0] is top-left, grid_items[8] is bottom-right
  std::sort(grid_items.begin(), grid_items.end(),
            [](const ElementRenderInfo& a, const ElementRenderInfo& b) {
              if (abs(a.y - b.y) > 10) {  // Different rows (allow 10px tolerance)
                return a.y < b.y;
              }
              return a.x < b.x;  // Same row, sort by x
            });

  if (grid_index >= static_cast<int>(grid_items.size())) {
    LOG_ERROR("ImageCaptchaSolver", "Grid index " + std::to_string(grid_index) +
              " out of range (found " + std::to_string(grid_items.size()) + " items)");
    return false;
  }

  // Click the grid item at the specified index using C++ native CEF mouse events
  const ElementRenderInfo& target = grid_items[grid_index];
  LOG_DEBUG("ImageCaptchaSolver", "Clicking grid item at position (" +
            std::to_string(target.x) + "," + std::to_string(target.y) + ")");

  auto host = browser->GetHost();
  host->SetFocus(true);

  CefMouseEvent mouse_event;
  mouse_event.x = target.x + (target.width / 2);
  mouse_event.y = target.y + (target.height / 2);

  host->SendMouseMoveEvent(mouse_event, false);
  OwlCaptchaUtils::Wait(50);  // Small delay for natural movement
  host->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 1);  // Mouse down
  host->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 1);   // Mouse up

  LOG_DEBUG("ImageCaptchaSolver", "C++ native click executed for grid item " + std::to_string(grid_index));
  return true;
}

bool OlibImageCaptchaSolver::SubmitVerification(
    CefRefPtr<CefBrowser> browser,
    const std::string& context_id,
    const std::string& submit_selector) {

  LOG_DEBUG("ImageCaptchaSolver", "Submitting verification: " + submit_selector);

  return OwlCaptchaUtils::ClickElement(browser, context_id, submit_selector);
}

bool OlibImageCaptchaSolver::SkipChallenge(
    CefRefPtr<CefBrowser> browser,
    const std::string& context_id,
    const std::string& skip_selector) {

  LOG_DEBUG("ImageCaptchaSolver", "Skipping challenge: " + skip_selector);

  bool clicked = OwlCaptchaUtils::ClickElement(browser, context_id, skip_selector);

  // No wait needed here - caller will handle frame unfreeze and updates
  return clicked;
}

bool OlibImageCaptchaSolver::WaitForImageLoad(
    CefRefPtr<CefBrowser> browser,
    const std::vector<std::string>& grid_selectors,
    int timeout_ms) {

  LOG_DEBUG("ImageCaptchaSolver", "Waiting for images to load");

  // Wait for first grid item to be visible as indicator
  if (!grid_selectors.empty()) {
    return OwlCaptchaUtils::WaitForVisible(browser, grid_selectors[0], timeout_ms);
  }

  return false;
}

bool OlibImageCaptchaSolver::CheckVerificationSuccess(
    const std::string& context_id,
    CefRefPtr<CefBrowser> browser,
    const std::string& challenge_selector) {

  LOG_DEBUG("ImageCaptchaSolver", "Polling for verification status");

  // Poll for up to 10 seconds with 200ms intervals (CAPTCHA can take 7+ seconds to verify)
  const int max_polls = 50;  // 50 * 200ms = 10 seconds
  const int poll_interval_ms = 200;

  for (int i = 0; i < max_polls; i++) {
    // Force element scan to update SemanticMatcher cache
    // This ensures we're not checking stale data
    CefRefPtr<OwlClient> client = static_cast<OwlClient*>(browser->GetHost()->GetClient().get());
    if (client) {
      client->WaitForElementScan(browser, context_id, 100);  // Short timeout, just to process pending DOM updates
    }

    // Check if challenge is still visible
    bool challenge_visible = OwlCaptchaUtils::IsVisible(browser, challenge_selector);

    if (!challenge_visible) {
      LOG_DEBUG("ImageCaptchaSolver", "Challenge hidden - verification successful");
      return true;
    }

    // Check if "Verified" text appeared in ANY element
    // Use semantic matcher to scan all elements for "Verified" text
    OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
    if (matcher) {
      std::vector<ElementSemantics> elements = matcher->GetAllElements(context_id);
      for (const auto& elem : elements) {
        // Check any element's text for "Verified" (case-insensitive)
        std::string text_lower = elem.text;
        std::transform(text_lower.begin(), text_lower.end(), text_lower.begin(), ::tolower);

        if (text_lower.find("verified") != std::string::npos) {
          LOG_DEBUG("ImageCaptchaSolver", "Verified indicator found in element (selector: '" +
                   elem.selector + "', text: '" + elem.text + "') - success");
          return true;
        }
      }
    }

    OwlCaptchaUtils::Wait(poll_interval_ms);
  }

  LOG_DEBUG("ImageCaptchaSolver", "Challenge still visible after polling - verification failed");
  return false;
}

std::string OlibImageCaptchaSolver::ExecuteJavaScriptAndGetResult(
    CefRefPtr<CefBrowser> browser,
    const std::string& script) {

  return OwlCaptchaUtils::ExecuteJavaScriptAndGetResult(browser, script);
}

std::vector<uint8_t> OlibImageCaptchaSolver::IsolatedScreenshot(
    CefRefPtr<CefBrowser> browser,
    const std::string& context_id,
    const std::string& element_selector) {

  LOG_DEBUG("ImageCaptchaSolver", "Taking isolated screenshot: " + element_selector + " (context: " + context_id + ")");

  return OwlCaptchaUtils::CaptureElementScreenshot(browser, context_id, element_selector);
}

bool OlibImageCaptchaSolver::AddNumberedOverlays(
    CefRefPtr<CefBrowser> browser,
    const std::vector<std::string>& grid_selectors) {

  LOG_DEBUG("ImageCaptchaSolver", "Adding numbered overlays to grid items");

  if (!browser || !browser->GetMainFrame()) {
    return false;
  }

  // IMPORTANT: Query all .grid-item elements and add overlays by index
  // This matches the clicking logic where we sort grid items by position
  std::string overlay_script = R"(
(function() {
  // Get all grid items
  var items = Array.from(document.querySelectorAll('.grid-item'));

  // Sort by position (top-to-bottom, left-to-right) to match clicking logic
  items.sort(function(a, b) {
    var rectA = a.getBoundingClientRect();
    var rectB = b.getBoundingClientRect();

    // If different rows (tolerance of 10px)
    if (Math.abs(rectA.top - rectB.top) > 10) {
      return rectA.top - rectB.top;
    }
    // Same row, sort by x
    return rectA.left - rectB.left;
  });

  // Add numbered overlays to each item
  items.forEach(function(elem, i) {
    var overlay = document.createElement('div');
    overlay.className = '_captcha_number_overlay';
    overlay.textContent = String(i);
    overlay.style.position = 'absolute';
    overlay.style.top = '5px';
    overlay.style.left = '5px';
    overlay.style.backgroundColor = 'rgba(255, 0, 0, 0.9)';
    overlay.style.color = 'white';
    overlay.style.padding = '4px 8px';
    overlay.style.fontSize = '16px';
    overlay.style.fontWeight = 'bold';
    overlay.style.borderRadius = '4px';
    overlay.style.zIndex = '9999';
    overlay.style.border = '2px solid white';
    overlay.style.boxShadow = '0 2px 4px rgba(0,0,0,0.5)';

    // Ensure parent has position context
    if (window.getComputedStyle(elem).position === 'static') {
      elem.style.position = 'relative';
    }
    elem.appendChild(overlay);
  });

  return items.length;
})();
)";

  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  frame->ExecuteJavaScript(overlay_script, frame->GetURL(), 0);

  // Wait for overlays to render
  OwlCaptchaUtils::Wait(200);

  LOG_DEBUG("ImageCaptchaSolver", "Numbered overlays added to " +
            std::to_string(grid_selectors.size()) + " grid items");
  return true;
}

bool OlibImageCaptchaSolver::RemoveNumberedOverlays(CefRefPtr<CefBrowser> browser) {
  LOG_DEBUG("ImageCaptchaSolver", "Removing numbered overlays");

  if (!browser || !browser->GetMainFrame()) {
    return false;
  }

  std::string remove_script = R"(
    (function() {
      var overlays = document.querySelectorAll('._captcha_number_overlay');
      overlays.forEach(function(overlay) {
        overlay.remove();
      });
      return true;
    })();
  )";

  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  frame->ExecuteJavaScript(remove_script, frame->GetURL(), 0);

  LOG_DEBUG("ImageCaptchaSolver", "Numbered overlays removed");
  return true;
}

std::string OlibImageCaptchaSolver::ExtractTargetFromChallenge(
    const std::string& context_id,
    CefRefPtr<CefBrowser> browser,
    const std::string& challenge_selector) {

  LOG_DEBUG("ImageCaptchaSolver", "Extracting target from challenge: " + challenge_selector);

  if (context_id.empty() || !browser || !browser->GetMainFrame() || challenge_selector.empty()) {
    LOG_WARN("ImageCaptchaSolver", "Invalid parameters for target extraction");
    return "";
  }

  // Get semantic matcher to access freshly scanned elements
  // (WaitForElementScan was just called before this function)
  OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
  if (!matcher) {
    LOG_WARN("ImageCaptchaSolver", "Semantic matcher not available");
    return "";
  }

  // Get all elements for this context (freshly scanned)
  std::vector<ElementSemantics> all_elements = matcher->GetAllElements(context_id);
  LOG_DEBUG("ImageCaptchaSolver", "Found " + std::to_string(all_elements.size()) + " tracked elements for target extraction");

#ifdef OWL_DEBUG_BUILD
  // DEBUG: Log first 20 elements to see what's being tracked
  LOG_DEBUG("ImageCaptchaSolver", "First 20 tracked elements:");
  for (size_t i = 0; i < std::min(size_t(20), all_elements.size()); i++) {
    const auto& elem = all_elements[i];
    LOG_DEBUG("ImageCaptchaSolver", "  [" + std::to_string(i) + "] id='" + elem.id + "' selector='" + elem.selector + "' text='" + elem.text.substr(0, 50) + "'");
  }
#endif

#ifdef OWL_DEBUG_BUILD
  // DEBUG: Log all elements that have an ID set
  LOG_DEBUG("ImageCaptchaSolver", "All elements with IDs:");
  for (const auto& elem : all_elements) {
    if (!elem.id.empty()) {
      LOG_DEBUG("ImageCaptchaSolver", "  id='" + elem.id + "' selector='" + elem.selector + "' text='" + elem.text.substr(0, 50) + "'");
    }
  }
#endif

  // Find the challengeTarget element
  std::string target;
  for (const auto& elem : all_elements) {
    // Check if this is the challengeTarget element by ID or selector
    if (elem.id == "challengeTarget" ||
        elem.selector.find("#challengeTarget") != std::string::npos ||
        elem.selector.find("STRONG#challengeTarget") != std::string::npos) {
      target = elem.text;
      LOG_DEBUG("ImageCaptchaSolver", "Found challengeTarget element with text: '" + target + "'");
      break;
    }
  }

  if (!target.empty()) {
    return target;
  }

  LOG_DEBUG("ImageCaptchaSolver", "No #challengeTarget found in " + std::to_string(all_elements.size()) + " elements, trying to parse full challenge text");

  // Fallback: Find challenge element and parse its text
  std::string challenge_text;
  for (const auto& elem : all_elements) {
    if (elem.selector == challenge_selector ||
        elem.selector.find(challenge_selector) != std::string::npos) {
      challenge_text = elem.text;
      LOG_DEBUG("ImageCaptchaSolver", "Found challenge element with text: '" + challenge_text + "'");
      break;
    }
  }

  if (challenge_text.empty()) {
    LOG_WARN("ImageCaptchaSolver", "No text found in challenge element");
    return "";
  }

  LOG_DEBUG("ImageCaptchaSolver", "Challenge text: '" + challenge_text + "'");

  // Parse target from text like "Select all squares with bicycles" -> "bicycles"
  // Common patterns:
  // - "Select all squares with [target]"
  // - "select all squares with [target]"
  // - "Click all images with [target]"

  std::string lower_text = challenge_text;
  std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), ::tolower);

  // Reuse the target variable from above (already declared on line 528)
  target = "";

  // Try "select all squares with"
  size_t pos = lower_text.find("select all squares with");
  if (pos != std::string::npos) {
    size_t start = pos + 23;  // Length of "select all squares with"
    // Skip whitespace
    while (start < challenge_text.length() && std::isspace(challenge_text[start])) {
      start++;
    }
    if (start < challenge_text.length()) {
      target = challenge_text.substr(start);
    }
  }

  // Try "click all images with" if above didn't match
  if (target.empty()) {
    pos = lower_text.find("click all images with");
    if (pos != std::string::npos) {
      size_t start = pos + 21;  // Length of "click all images with"
      // Skip whitespace
      while (start < challenge_text.length() && std::isspace(challenge_text[start])) {
        start++;
      }
      if (start < challenge_text.length()) {
        target = challenge_text.substr(start);
      }
    }
  }

  // Try "select images with" if above didn't match
  if (target.empty()) {
    pos = lower_text.find("select images with");
    if (pos != std::string::npos) {
      size_t start = pos + 18;  // Length of "select images with"
      // Skip whitespace
      while (start < challenge_text.length() && std::isspace(challenge_text[start])) {
        start++;
      }
      if (start < challenge_text.length()) {
        target = challenge_text.substr(start);
      }
    }
  }

  // Clean up target (trim trailing periods, etc.)
  if (!target.empty()) {
    // Trim whitespace
    target.erase(0, target.find_first_not_of(" \t\n\r"));
    target.erase(target.find_last_not_of(" \t\n\r.") + 1);
  }

  if (target.empty()) {
    LOG_WARN("ImageCaptchaSolver", "Failed to parse target from challenge text");
  } else {
    LOG_DEBUG("ImageCaptchaSolver", "Extracted target: '" + target + "'");
  }

  return target;
}
