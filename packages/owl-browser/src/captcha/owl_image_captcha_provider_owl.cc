#include "owl_image_captcha_provider_owl.h"
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
#include <thread>
#include <chrono>
#include <set>

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

OwlImageCaptchaProvider::OwlImageCaptchaProvider() {
  InitializeConfig();
  LOG_DEBUG("OwlImageCaptchaProvider", "Initialized");
}

void OwlImageCaptchaProvider::InitializeConfig() {
  // Owl CAPTCHA provider configuration
  config_.grid_container_selector = "#imageGrid";
  config_.grid_item_selector = ".grid-item";
  config_.grid_item_class = "grid-item";
  config_.default_grid_size = 9;

  config_.challenge_container_selector = ".captcha-container, #captchaContainer";
  config_.challenge_title_selector = ".challenge-title, #captchaChallenge";
  config_.target_text_selector = "#challengeTarget";

  config_.checkbox_selector = "#captchaCheck";
  config_.submit_button_selector = "#verifyBtn";
  config_.skip_button_selector = "#skipBtn, .btn-secondary";
  config_.refresh_button_selector = "";

  config_.uses_iframe = false;

  // Optimized timing - reduced from original values
  config_.click_delay_min_ms = 100;      // Was 200
  config_.click_delay_max_ms = 250;      // Was 450
  config_.post_checkbox_wait_ms = 300;   // Was 1000 - checkbox click doesn't need long wait
  config_.post_submit_wait_ms = 500;     // Was 2000 - verification polling handles the rest
  config_.grid_load_timeout_ms = 4000;   // Was 6000
}

double OwlImageCaptchaProvider::DetectProvider(CefRefPtr<CefBrowser> browser,
                                               const CaptchaClassificationResult& classification) {
  if (!browser || !browser->GetMainFrame()) {
    return 0.0;
  }

  double confidence = 0.0;

  // Check if URL is owl:// scheme (internal test pages)
  std::string url = browser->GetMainFrame()->GetURL().ToString();
  if (url.find("owl://") == 0) {
    confidence += 0.5;  // High base confidence for internal pages
  }

  // Use JavaScript to check for Owl-specific CAPTCHA elements
  // This is our generic/internal CAPTCHA - look for standard image grid patterns
  std::string detect_script = R"(
    (function() {
      var score = 0;

      // Check for Owl-specific elements
      if (document.querySelector('#imageGrid')) score += 0.4;
      if (document.querySelector('.captcha-container')) score += 0.2;
      if (document.querySelector('#captchaCheck')) score += 0.2;
      if (document.querySelector('#verifyBtn')) score += 0.1;
      if (document.querySelector('.image-grid')) score += 0.3;
      if (document.querySelector('.captcha-challenge')) score += 0.2;

      // Generic image CAPTCHA patterns (fallback detection)
      // Check for grid of clickable images without external provider markers
      var hasImageGrid = document.querySelector('[class*="grid"]') &&
                         document.querySelectorAll('[class*="grid"] img').length >= 4;
      if (hasImageGrid) score += 0.2;

      // Ensure this is NOT a known external provider
      // If we detect external provider markers, reduce confidence
      var iframes = document.querySelectorAll('iframe');
      for (var i = 0; i < iframes.length; i++) {
        var src = iframes[i].src || '';
        if (src.includes('google.com/recaptcha') ||
            src.includes('hcaptcha.com') ||
            src.includes('cloudflare.com')) {
          // External provider detected - this is NOT our internal CAPTCHA
          return 0.0;
        }
      }

      return Math.min(score, 1.0);
    })();
  )";

  // Execute JavaScript and get result
  std::string result = OwlCaptchaUtils::ExecuteJavaScriptAndGetResult(browser, detect_script);
  if (!result.empty() && result != "null" && result != "undefined") {
    char* end = nullptr;
    double js_score = strtod(result.c_str(), &end);
    if (end != result.c_str() && js_score >= 0.0 && js_score <= 1.0) {
      confidence += js_score;
    }
  }

  LOG_DEBUG("OwlImageCaptchaProvider", "Detection confidence: " + std::to_string(confidence));

  return std::min(confidence, 1.0);
}

ImageCaptchaSolveResult OwlImageCaptchaProvider::Solve(
    const std::string& context_id,
    CefRefPtr<CefBrowser> browser,
    const CaptchaClassificationResult& classification,
    OwlLLMClient* llm_client,
    int max_attempts) {

  LOG_DEBUG("OwlImageCaptchaProvider", "Starting solve (max_attempts=" + std::to_string(max_attempts) + ")");

  ImageCaptchaSolveResult result;
  result.success = false;
  result.confidence = 0.0;
  result.attempts = 0;
  result.needs_skip = false;
  result.provider = ImageCaptchaProviderType::OWL;

  if (!llm_client) {
    result.error_message = "LLM client not available";
    LOG_ERROR("OwlImageCaptchaProvider", result.error_message);
    return result;
  }

  if (classification.type != CaptchaType::IMAGE_SELECTION) {
    result.error_message = "Not an image-selection CAPTCHA";
    LOG_ERROR("OwlImageCaptchaProvider", result.error_message);
    return result;
  }

  // NOTE: Don't check for grid_items.empty() here - grid items only appear AFTER clicking checkbox!

  // Unfreeze frame cache for DOM updates
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  client->UnfreezeFrameCache();

  // Minimal wait for frame unfreeze (was 300ms)
  Wait(50);

  // First check if already verified before clicking checkbox
  {
    OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
    if (matcher) {
      std::vector<ElementSemantics> elements = matcher->GetAllElements(context_id);
      for (const auto& elem : elements) {
        if (elem.selector.find(".captcha-text") != std::string::npos ||
            elem.selector.find("captcha-text") != std::string::npos) {
          if (elem.text.find("Verified") != std::string::npos) {
            LOG_DEBUG("OwlImageCaptchaProvider", "Already verified");
            result.success = true;
            result.confidence = 1.0;
            result.attempts = 0;
            return result;
          }
          break;
        }
      }
    }
  }

  // Click checkbox FIRST to trigger the challenge
  bool checkbox_clicked = false;
  if (!classification.checkbox_selector.empty()) {
    LOG_DEBUG("OwlImageCaptchaProvider", "Clicking checkbox to trigger challenge");
    checkbox_clicked = ClickCheckbox(browser, context_id, classification);
    if (!checkbox_clicked) {
      LOG_ERROR("OwlImageCaptchaProvider", "Failed to click checkbox - returning failure");
      result.success = false;
      result.error_message = "Failed to click CAPTCHA checkbox";
      return result;
    }
    Wait(config_.post_checkbox_wait_ms);
  }

  // Now wait for the challenge to appear after clicking checkbox
  if (!classification.challenge_element.empty()) {
    LOG_DEBUG("OwlImageCaptchaProvider", "Waiting for challenge to appear after checkbox click");

    const int max_polls = 30;
    bool challenge_appeared = false;
    bool verified_shown = false;

    for (int i = 0; i < max_polls; i++) {
      // Check for verified indicator
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

      if (captcha_text.find("Verified") != std::string::npos) {
        verified_shown = true;
        break;
      }

      bool challenge_visible = OwlCaptchaUtils::IsVisible(browser, classification.challenge_element);

      if (challenge_visible) {
        Wait(100);  // Reduced from 300ms
        // Re-check verification
        captcha_text = "";
        if (matcher) {
          std::vector<ElementSemantics> elements = matcher->GetAllElements(context_id);
          for (const auto& elem : elements) {
            if (elem.selector.find(".captcha-text") != std::string::npos) {
              captcha_text = elem.text;
              break;
            }
          }
        }
        if (captcha_text.find("Verified") != std::string::npos) {
          verified_shown = true;
        } else {
          challenge_appeared = true;
        }
        break;
      }

      Wait(100);  // Reduced from 200ms
    }

    if (verified_shown) {
      LOG_DEBUG("OwlImageCaptchaProvider", "Auto-verified after checkbox click");
      result.success = true;
      result.confidence = 1.0;
      result.attempts = 0;
      return result;
    }

    // Only assume auto-verified if checkbox was successfully clicked
    if (!challenge_appeared && checkbox_clicked) {
      LOG_DEBUG("OwlImageCaptchaProvider", "Challenge never appeared after successful click - may be auto-verified");
      result.success = true;
      result.confidence = 0.8;
      result.attempts = 0;
      return result;
    } else if (!challenge_appeared) {
      LOG_ERROR("OwlImageCaptchaProvider", "Challenge never appeared and checkbox click status unknown");
      result.success = false;
      result.error_message = "Challenge never appeared after checkbox interaction";
      return result;
    }
  }

  // Check for grid items
  LOG_DEBUG("OwlImageCaptchaProvider", "Checking for challenge grid items");
  bool has_grid_items = false;
  const int max_grid_polls = 15;

  for (int i = 0; i < max_grid_polls; i++) {
    client->WaitForElementScan(browser, context_id, 1000);

    OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
    std::vector<ElementRenderInfo> all_elements = tracker->GetAllVisibleElements(context_id);

    int grid_count = 0;
    for (const auto& elem : all_elements) {
      if (elem.className.find("grid-item") != std::string::npos) {
        grid_count++;
      }
    }

    if (grid_count >= 9) {
      has_grid_items = true;
      break;
    }

    Wait(150);
  }

  if (!has_grid_items) {
    // Challenge appeared but no grid items found - this is a failure, not auto-verified
    // If it was truly auto-verified, we would have detected "Verified" text earlier
    LOG_ERROR("OwlImageCaptchaProvider", "No grid items found after challenge appeared - CAPTCHA interaction failed");
    result.success = false;
    result.error_message = "Challenge appeared but no grid items loaded";
    return result;
  }

  // Solve attempts
  for (int attempt = 0; attempt < max_attempts; attempt++) {
    result.attempts++;
    LOG_DEBUG("OwlImageCaptchaProvider", "Attempt " + std::to_string(attempt + 1) + "/" +
             std::to_string(max_attempts));

    client->UnfreezeFrameCache();

    // Wait for grid items
    bool grid_items_exist = false;
    const int max_grid_item_polls = 30;

    for (int i = 0; i < max_grid_item_polls; i++) {
      if (OwlCaptchaUtils::IsVisible(browser, ".grid-item")) {
        grid_items_exist = true;
        break;
      }
      Wait(100);  // Reduced from 200ms
    }

    if (!grid_items_exist) {
      result.error_message = "Grid items failed to load";
      continue;
    }

    Wait(200);  // Reduced from 800ms - images should already be loaded

    // Scroll grid into view
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
    Wait(150);  // Reduced from 400ms - instant scroll

    // DOM scan and freeze
    client->WaitForElementScan(browser, context_id, 2000);  // Reduced from 3000ms
    client->FreezeFrameCache();
    // Removed Wait(200) - freeze is immediate

    // Extract target
    std::string current_target = ExtractTarget(context_id, browser, classification);
    if (current_target.empty()) {
      current_target = classification.target_description;
    }
    if (current_target.empty()) {
      current_target = "objects";
    }
    result.target_detected = current_target;

    LOG_DEBUG("OwlImageCaptchaProvider", "Target: '" + result.target_detected + "'");

    // Capture screenshot
    std::vector<uint8_t> grid_screenshot = CaptureGridScreenshot(browser, context_id);

    if (grid_screenshot.empty()) {
      result.error_message = "Failed to capture grid";
      continue;
    }

    // Identify matches
    std::vector<int> matching_indices = IdentifyMatchingImages(
        grid_screenshot, result.target_detected,
        classification.grid_size, llm_client);

    if (matching_indices.empty()) {
      if (allow_skip_ && !classification.skip_button.empty()) {
        result.needs_skip = true;
        SkipChallenge(browser, context_id);
        continue;
      }
      result.error_message = "No matching images found";
      continue;
    }

    result.selected_indices = matching_indices;

    // Randomize click order
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::vector<int> shuffled_indices = matching_indices;
    std::shuffle(shuffled_indices.begin(), shuffled_indices.end(), gen);

    // Human-like delay before clicking (reduced from 600-1000)
    std::uniform_int_distribution<> delay_dist(300, 500);
    Wait(delay_dist(gen));

    // Click grid items
    for (size_t i = 0; i < shuffled_indices.size(); i++) {
      int index = shuffled_indices[i];

      if (index < 0 || index >= static_cast<int>(classification.grid_items.size())) {
        continue;
      }

      ClickGridItem(browser, context_id, index);

      if (i < shuffled_indices.size() - 1) {
        std::uniform_int_distribution<> interval_dist(
            config_.click_delay_min_ms, config_.click_delay_max_ms);
        Wait(interval_dist(gen));
      }
    }

    // Submit verification
    if (auto_submit_ && !classification.submit_button.empty()) {
      Wait(200);  // Reduced from 500ms

      if (!SubmitVerification(browser, context_id)) {
        result.error_message = "Failed to submit";
        continue;
      }

      Wait(config_.post_submit_wait_ms);

      if (CheckVerificationSuccess(context_id, browser)) {
        LOG_DEBUG("OwlImageCaptchaProvider", "CAPTCHA solved");
        result.success = true;
        result.confidence = 0.85;
        result.attempts = attempt + 1;
        return result;
      } else {
        if (allow_skip_ && !classification.skip_button.empty()) {
          SkipChallenge(browser, context_id);
        }
      }
    } else {
      result.success = true;
      result.confidence = 0.7;
      return result;
    }
  }

  result.error_message = "All attempts failed";
  result.needs_skip = true;
  return result;
}

bool OwlImageCaptchaProvider::IsAutoVerified(CefRefPtr<CefBrowser> browser,
                                             const CaptchaClassificationResult& classification) {
  OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
  if (!matcher) return false;

  // This requires a context_id, but since we don't have one in this method,
  // we'll return false and let Solve() handle auto-verification detection
  return false;
}

bool OwlImageCaptchaProvider::ClickCheckbox(CefRefPtr<CefBrowser> browser,
                                            const std::string& context_id,
                                            const CaptchaClassificationResult& classification) {
  LOG_DEBUG("OwlImageCaptchaProvider", "Clicking checkbox: " + classification.checkbox_selector);

  // Use BrowserManager's Click function - it handles scrolling, coordinate updates, etc.
  // This is the same Click that works for all form interactions
  OwlBrowserManager* manager = OwlBrowserManager::GetInstance();

  // Try clicking the checkbox label first (more reliable than hidden input)
  std::vector<std::string> selectors_to_try = {
    ".captcha-checkbox-label",
    "label.captcha-checkbox-label",
    ".captcha-checkbox",
    "label[for='captchaCheck']",
    classification.checkbox_selector
  };

  for (const auto& selector : selectors_to_try) {
    if (selector.empty()) continue;

    LOG_DEBUG("OwlImageCaptchaProvider", "Trying to click selector: " + selector);
    ActionResult result = manager->Click(context_id, selector);

    if (result.success) {
      LOG_DEBUG("OwlImageCaptchaProvider", "Checkbox clicked via BrowserManager: " + selector);
      return true;
    }
  }

  // Fallback: try semantic matching with "I'm not a robot" text
  OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
  if (matcher) {
    std::vector<ElementMatch> matches = matcher->FindByDescription(context_id, "I'm not a robot");

    for (const auto& match : matches) {
      if (match.element.width > 0 && match.element.height > 0 && match.confidence > 0.3) {
        // Use position-based click through BrowserManager
        std::string pos_selector = std::to_string(match.element.x + match.element.width / 2) + "x" +
                                   std::to_string(match.element.y + match.element.height / 2);
        LOG_DEBUG("OwlImageCaptchaProvider", "Trying semantic match position click: " + pos_selector);

        ActionResult result = manager->Click(context_id, pos_selector);
        if (result.success) {
          LOG_DEBUG("OwlImageCaptchaProvider", "Checkbox clicked via semantic match position");
          return true;
        }
      }
    }
  }

  LOG_ERROR("OwlImageCaptchaProvider", "Failed to click checkbox - no matching element found");
  return false;
}

std::string OwlImageCaptchaProvider::ExtractTarget(const std::string& context_id,
                                                    CefRefPtr<CefBrowser> browser,
                                                    const CaptchaClassificationResult& classification) {
  LOG_DEBUG("OwlImageCaptchaProvider", "Extracting target from challenge");

  OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
  if (!matcher) return "";

  std::vector<ElementSemantics> all_elements = matcher->GetAllElements(context_id);

  // Find challengeTarget element
  for (const auto& elem : all_elements) {
    if (elem.id == "challengeTarget" ||
        elem.selector.find("#challengeTarget") != std::string::npos) {
      LOG_DEBUG("OwlImageCaptchaProvider", "Found target: '" + elem.text + "'");
      return elem.text;
    }
  }

  // Fallback: parse challenge text
  std::string challenge_text;
  for (const auto& elem : all_elements) {
    if (elem.selector.find(classification.challenge_element) != std::string::npos) {
      challenge_text = elem.text;
      break;
    }
  }

  if (challenge_text.empty()) return "";

  // Parse "Select all squares with [target]" pattern
  std::string lower_text = challenge_text;
  std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), ::tolower);

  std::string target;
  size_t pos = lower_text.find("select all squares with");
  if (pos != std::string::npos) {
    size_t start = pos + 23;
    while (start < challenge_text.length() && std::isspace(challenge_text[start])) {
      start++;
    }
    if (start < challenge_text.length()) {
      target = challenge_text.substr(start);
    }
  }

  // Clean up
  if (!target.empty()) {
    target.erase(0, target.find_first_not_of(" \t\n\r"));
    target.erase(target.find_last_not_of(" \t\n\r.") + 1);
  }

  return target;
}

std::vector<std::tuple<int, int, int, int>> OwlImageCaptchaProvider::GetGridItemPositions(
    CefRefPtr<CefBrowser> browser) {

  std::vector<std::tuple<int, int, int, int>> positions;

  // This would be implemented using render tracker
  // For now, return empty (the main capture method handles this)

  return positions;
}

std::vector<uint8_t> OwlImageCaptchaProvider::CaptureGridScreenshot(CefRefPtr<CefBrowser> browser,
                                                                     const std::string& context_id) {
  LOG_DEBUG("OwlImageCaptchaProvider", "Capturing grid screenshot");

  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();

  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  if (!client_base) return {};
  OwlClient* client = static_cast<OwlClient*>(client_base.get());

  // Get grid items
  std::vector<ElementRenderInfo> all_elements = tracker->GetAllVisibleElements(context_id);
  std::vector<ElementRenderInfo> grid_items;
  std::set<std::pair<int, int>> seen_positions;

  for (const auto& elem : all_elements) {
    if (elem.className.find("grid-item") == std::string::npos) continue;

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
    LOG_ERROR("OwlImageCaptchaProvider", "No grid items found");
    return {};
  }

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

  std::vector<uint8_t> bgra_pixels;

  if (OwlBrowserManager::UsesRunMessageLoop()) {
#ifdef BUILD_UI
    std::vector<uint8_t> png_data = CaptureNativeScreenshot(browser, min_x, min_y, width, height, grid_items, min_x, min_y);
    return png_data;
#else
    return {};
#endif
  } else {
    bool success = client->GetCroppedBGRAFromCache(&bgra_pixels, min_x, min_y, width, height);
    if (!success || bgra_pixels.empty()) return {};
  }

  // CRITICAL: GetCroppedBGRAFromCache may return a smaller buffer if requested
  // dimensions exceed frame bounds. Calculate actual dimensions from buffer size.
  size_t expected_size = static_cast<size_t>(width) * height * 4;
  if (bgra_pixels.size() < expected_size) {
    // Buffer is smaller than expected - recalculate actual dimensions
    // Assume square aspect ratio for safety, or use width as primary dimension
    size_t actual_pixels = bgra_pixels.size() / 4;
    int actual_width = width;
    int actual_height = static_cast<int>(actual_pixels / width);
    if (actual_height <= 0) {
      LOG_ERROR("OwlImageCaptchaProvider", "Buffer size mismatch: expected " +
                std::to_string(expected_size) + " got " + std::to_string(bgra_pixels.size()));
      return {};
    }
    width = actual_width;
    height = actual_height;
    LOG_DEBUG("OwlImageCaptchaProvider", "Adjusted dimensions to " +
              std::to_string(width) + "x" + std::to_string(height));
  }

  // Draw overlays
  DrawNumberedOverlaysOnBGRA(bgra_pixels.data(), width, height, grid_items, min_x, min_y);

  // Encode to PNG
  std::vector<uint8_t> png_output;
  client->EncodePNGFromBGRA(png_output, bgra_pixels.data(), width, height);

  return png_output;
}

bool OwlImageCaptchaProvider::ClickGridItem(CefRefPtr<CefBrowser> browser,
                                            const std::string& context_id,
                                            int grid_index) {
  LOG_DEBUG("OwlImageCaptchaProvider", "Clicking grid item " + std::to_string(grid_index));

  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  std::vector<ElementRenderInfo> all_elements = tracker->GetAllVisibleElements(context_id);

  std::vector<ElementRenderInfo> grid_items;
  for (const auto& elem : all_elements) {
    if (elem.className.find("grid-item") != std::string::npos) {
      grid_items.push_back(elem);
    }
  }

  if (grid_items.empty()) return false;

  // Sort by position
  std::sort(grid_items.begin(), grid_items.end(),
            [](const ElementRenderInfo& a, const ElementRenderInfo& b) {
              if (abs(a.y - b.y) > 10) return a.y < b.y;
              return a.x < b.x;
            });

  if (grid_index >= static_cast<int>(grid_items.size())) return false;

  const ElementRenderInfo& target = grid_items[grid_index];

  auto host = browser->GetHost();
  host->SetFocus(true);

  CefMouseEvent mouse_event;
  mouse_event.x = target.x + (target.width / 2);
  mouse_event.y = target.y + (target.height / 2);

  host->SendMouseMoveEvent(mouse_event, false);
  Wait(50);
  host->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 1);
  host->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 1);

  return true;
}

bool OwlImageCaptchaProvider::SubmitVerification(CefRefPtr<CefBrowser> browser,
                                                  const std::string& context_id) {
  LOG_DEBUG("OwlImageCaptchaProvider", "Submitting verification");
  return OwlCaptchaUtils::ClickElement(browser, context_id, config_.submit_button_selector);
}

bool OwlImageCaptchaProvider::SkipChallenge(CefRefPtr<CefBrowser> browser,
                                            const std::string& context_id) {
  LOG_DEBUG("OwlImageCaptchaProvider", "Skipping challenge");
  return OwlCaptchaUtils::ClickElement(browser, context_id, config_.skip_button_selector);
}

bool OwlImageCaptchaProvider::CheckVerificationSuccess(const std::string& context_id,
                                                        CefRefPtr<CefBrowser> browser) {
  LOG_DEBUG("OwlImageCaptchaProvider", "Checking verification status");

  // Optimized polling: faster checks, shorter total wait
  // Most verifications complete within 2 seconds
  const int max_polls = 30;           // Was 50 (30 * 100ms = 3s max)
  const int poll_interval_ms = 100;   // Was 200

  for (int i = 0; i < max_polls; i++) {
    CefRefPtr<OwlClient> client = static_cast<OwlClient*>(browser->GetHost()->GetClient().get());
    if (client) {
      client->WaitForElementScan(browser, context_id, 100);
    }

    bool challenge_visible = OwlCaptchaUtils::IsVisible(browser, config_.challenge_title_selector);

    if (!challenge_visible) {
      LOG_DEBUG("OwlImageCaptchaProvider", "Challenge hidden - success");
      return true;
    }

    // Check for "Verified" text
    OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
    if (matcher) {
      std::vector<ElementSemantics> elements = matcher->GetAllElements(context_id);
      for (const auto& elem : elements) {
        std::string text_lower = elem.text;
        std::transform(text_lower.begin(), text_lower.end(), text_lower.begin(), ::tolower);

        if (text_lower.find("verified") != std::string::npos) {
          LOG_DEBUG("OwlImageCaptchaProvider", "Verified indicator found");
          return true;
        }
      }
    }

    Wait(poll_interval_ms);
  }

  return false;
}
