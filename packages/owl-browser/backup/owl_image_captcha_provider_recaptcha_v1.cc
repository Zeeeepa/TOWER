#include "owl_image_captcha_provider_recaptcha.h"
#include "owl_captcha_utils.h"
#include "owl_llm_client.h"
#include "owl_semantic_matcher.h"
#include "owl_client.h"
#include "owl_render_tracker.h"
#include "owl_browser_manager.h"
#include "owl_native_screenshot.h"
#include "owl_image_enhancer.h"
#include "logger.h"
#include "include/cef_process_message.h"
#include "include/cef_app.h"
#include <sstream>
#include <algorithm>
#include <random>
#include <thread>
#include <chrono>
#include <set>
#include <fstream>
#include <climits>
#include <regex>

// Counter for debug image filenames (used in CaptureGridScreenshot)
static int g_recaptcha_debug_counter __attribute__((unused)) = 0;

// Challenge mode enumeration
enum class RecaptchaChallengeMode {
  UNKNOWN,
  STATIC_3X3,         // 3x3 grid, "Select all images with X" - 9 separate images
  DYNAMIC_3X3,        // 3x3 grid, "Click verify once there are none left" - tiles fade/replace
  STATIC_4X4          // 4x4 grid, "Select all squares with X" - ONE image split into 16 squares
};

// Tile state tracking for dynamic mode
struct TileState {
  bool selected = false;      // Has been clicked at least once
  bool has_new_image = false; // Image was replaced after clicking
  int click_count = 0;        // How many times this tile was clicked
  std::string image_hash;     // Hash of tile image to detect changes (future use)
};

RecaptchaImageCaptchaProvider::RecaptchaImageCaptchaProvider()
    : current_grid_size_(9), is_dynamic_mode_(false), current_grid_type_(RecaptchaGridType::SEPARATE_IMAGES) {
  InitializeConfig();
  LOG_INFO("RecaptchaImageCaptchaProvider", "Initialized");
}

void RecaptchaImageCaptchaProvider::InitializeConfig() {
  // reCAPTCHA v2 specific configuration
  // Note: reCAPTCHA renders in iframes

  // Checkbox iframe
  config_.iframe_selector = "iframe[src*='recaptcha'][src*='anchor']";

  // Challenge iframe (appears after clicking checkbox)
  config_.challenge_iframe_selector = "iframe[src*='recaptcha'][src*='bframe']";

  config_.uses_iframe = true;

  // Inside challenge iframe
  config_.grid_container_selector = ".rc-imageselect-challenge";
  config_.grid_item_selector = ".rc-imageselect-tile";
  config_.grid_item_class = "rc-imageselect-tile";
  config_.default_grid_size = 9;  // Can also be 16 for 4x4

  config_.challenge_container_selector = ".rc-imageselect";
  config_.challenge_title_selector = ".rc-imageselect-desc-wrapper";
  config_.target_text_selector = ".rc-imageselect-desc strong, .rc-imageselect-desc-no-canonical";

  config_.checkbox_selector = ".recaptcha-checkbox-border";
  config_.submit_button_selector = "#recaptcha-verify-button";
  config_.skip_button_selector = "#recaptcha-reload-button";  // Reload for new challenge
  config_.refresh_button_selector = "#recaptcha-reload-button";
  config_.audio_button_selector = "#recaptcha-audio-button";

  // Timing - reCAPTCHA needs longer delays to appear natural
  config_.click_delay_min_ms = 300;
  config_.click_delay_max_ms = 600;
  config_.post_checkbox_wait_ms = 2000;  // Wait for challenge to load
  config_.post_submit_wait_ms = 3000;    // Verification can take longer
  config_.grid_load_timeout_ms = 10000;  // reCAPTCHA can be slow
}

double RecaptchaImageCaptchaProvider::DetectProvider(CefRefPtr<CefBrowser> browser,
                                                     const CaptchaClassificationResult& classification) {
  if (!browser || !browser->GetMainFrame()) {
    return 0.0;
  }

  double confidence = 0.0;

  // Check page URL and loaded resources for reCAPTCHA indicators
  std::string url = browser->GetMainFrame()->GetURL().ToString();

  // If page URL contains recaptcha, high confidence
  if (url.find("recaptcha") != std::string::npos ||
      url.find("google.com/recaptcha") != std::string::npos) {
    confidence += 0.8;
  }

  // Use JavaScript to check for reCAPTCHA-specific elements and scripts
  std::string detect_script = R"(
    (function() {
      var score = 0;

      // Check for reCAPTCHA iframes (loaded from google.com)
      var iframes = document.querySelectorAll('iframe');
      for (var i = 0; i < iframes.length; i++) {
        var src = iframes[i].src || '';
        if (src.includes('google.com/recaptcha') || src.includes('recaptcha/api')) {
          score += 0.5;
          break;
        }
      }

      // Check for reCAPTCHA scripts
      var scripts = document.querySelectorAll('script');
      for (var i = 0; i < scripts.length; i++) {
        var src = scripts[i].src || '';
        if (src.includes('google.com/recaptcha') || src.includes('recaptcha/api')) {
          score += 0.3;
          break;
        }
      }

      // Check for grecaptcha object
      if (typeof grecaptcha !== 'undefined') {
        score += 0.2;
      }

      // Check for g-recaptcha class
      if (document.querySelector('.g-recaptcha')) {
        score += 0.1;
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

  LOG_DEBUG("RecaptchaImageCaptchaProvider", "Detection confidence: " + std::to_string(confidence));

  return std::min(confidence, 1.0);
}

CefRefPtr<CefFrame> RecaptchaImageCaptchaProvider::GetChallengeFrame(CefRefPtr<CefBrowser> browser) {
  if (!browser) return nullptr;

  // Get all frames and find the challenge frame
  std::vector<CefString> frame_ids;
  browser->GetFrameIdentifiers(frame_ids);

  for (const CefString& frame_id : frame_ids) {
    CefRefPtr<CefFrame> frame = browser->GetFrameByIdentifier(frame_id);
    if (frame) {
      std::string url = frame->GetURL().ToString();
      // Challenge frame URL contains "bframe"
      if (url.find("recaptcha") != std::string::npos && url.find("bframe") != std::string::npos) {
        LOG_DEBUG("RecaptchaImageCaptchaProvider", "Found challenge frame: " + url);
        return frame;
      }
    }
  }

  return nullptr;
}

bool RecaptchaImageCaptchaProvider::SwitchToChallengeIframe(CefRefPtr<CefBrowser> browser) {
  CefRefPtr<CefFrame> challenge_frame = GetChallengeFrame(browser);
  return challenge_frame != nullptr;
}

bool RecaptchaImageCaptchaProvider::IsDynamicTileMode(CefRefPtr<CefBrowser> browser) {
  // reCAPTCHA challenge types:
  //
  // 1. STATIC 3x3 - "Select all images with X"
  //    - Table class: rc-imageselect-table-33
  //    - 9 separate images, each is different
  //    - Click matching images once, then verify
  //
  // 2. DYNAMIC 3x3 - "Select all images with X. Click verify once there are none left."
  //    - Table class: rc-imageselect-table-33
  //    - Clicked tiles get REPLACED with new images
  //    - Must keep clicking until no more matches, then verify
  //    - Key identifier: "Click verify once there are none left" in instructions
  //
  // 3. STATIC 4x4 - "Select all squares with X"
  //    - Table class: rc-imageselect-table-44
  //    - One large image split into 16 squares
  //    - Select squares containing the object, then verify (or Skip if none)
  //
  // The DEFINITIVE indicator for dynamic mode is the instruction text containing:
  // "Click verify once there are none left"

  CefRefPtr<CefFrame> frame = GetChallengeFrame(browser);
  if (!frame) return false;

  // Check tracker for dynamic mode text indicators
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (tracker) {
    std::string challenge_context_id = "dynamic_check";
    tracker->ClearContext(challenge_context_id);

    // Scan challenge frame
    CefRefPtr<CefProcessMessage> scan_msg = CefProcessMessage::Create("scan_element");
    CefRefPtr<CefListValue> args = scan_msg->GetArgumentList();
    args->SetString(0, challenge_context_id);
    args->SetString(1, "*");
    frame->SendProcessMessage(PID_RENDERER, scan_msg);

    // Wait for scan
    for (int i = 0; i < 20; i++) {
      CefDoMessageLoopWork();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::vector<ElementRenderInfo> elements = tracker->GetAllVisibleElements(challenge_context_id);
    for (const auto& elem : elements) {
      // Only check the instruction description elements
      // Class: rc-imageselect-desc-no-canonical or rc-imageselect-desc
      if (elem.className.find("rc-imageselect-desc") == std::string::npos) {
        continue;
      }

      std::string text = elem.text;
      // Convert to lowercase for comparison
      std::transform(text.begin(), text.end(), text.begin(), ::tolower);

      // Dynamic mode ONLY indicator: "Click verify once there are none left"
      // This exact phrase appears in the instruction span for dynamic challenges
      if (text.find("click verify once there are none left") != std::string::npos ||
          text.find("once there are none left") != std::string::npos) {
        LOG_INFO("RecaptchaImageCaptchaProvider", "Detected DYNAMIC tile mode: instruction contains 'once there are none left'");
        is_dynamic_mode_ = true;
        return true;
      }
    }
  }

  LOG_INFO("RecaptchaImageCaptchaProvider", "Static tile mode (no dynamic indicators found)");
  is_dynamic_mode_ = false;
  return false;
}

bool RecaptchaImageCaptchaProvider::WaitForTileUpdate(CefRefPtr<CefBrowser> browser, int timeout_ms) {
  // In dynamic mode, wait for tile animations/transitions to complete
  // reCAPTCHA dynamic tiles:
  // 1. Fade out old image (~300ms CSS transition)
  // 2. Show loading placeholder/spinner
  // 3. Network request for new image (variable, 500-2000ms depending on network)
  // 4. Fade in new image (~300ms CSS transition)
  // Total can be 1500-3000ms depending on network
  //
  // Use a reliable fixed wait that covers most cases
  // 2000ms base + extra buffer for slow networks

  LOG_DEBUG("RecaptchaImageCaptchaProvider", "Waiting for tile update (timeout: " +
            std::to_string(timeout_ms) + "ms)");

  // Wait in chunks to allow for any async rendering
  const int chunk_size = 500;
  int waited = 0;

  while (waited < timeout_ms) {
    int to_wait = std::min(chunk_size, timeout_ms - waited);
    Wait(to_wait);
    waited += to_wait;
  }

  LOG_DEBUG("RecaptchaImageCaptchaProvider", "Tile wait complete after " +
            std::to_string(waited) + "ms");
  return true;
}

ImageCaptchaSolveResult RecaptchaImageCaptchaProvider::Solve(
    const std::string& context_id,
    CefRefPtr<CefBrowser> browser,
    const CaptchaClassificationResult& classification,
    OwlLLMClient* llm_client,
    int max_attempts) {

  LOG_INFO("RecaptchaImageCaptchaProvider", "Starting reCAPTCHA solve (max attempts: " +
           std::to_string(max_attempts) + ")");

  ImageCaptchaSolveResult result;
  result.success = false;
  result.confidence = 0.0;
  result.attempts = 0;
  result.needs_skip = false;
  result.provider = ImageCaptchaProviderType::RECAPTCHA;

  // Reset state tracking
  selected_tiles_.clear();
  is_dynamic_mode_ = false;
  current_grid_size_ = 9;

  if (!llm_client) {
    result.error_message = "LLM client not available";
    LOG_ERROR("RecaptchaImageCaptchaProvider", result.error_message);
    return result;
  }

  static std::random_device rd;
  static std::mt19937 gen(rd());

  // ============ STEP 1: CLICK CHECKBOX ============
  // reCAPTCHA flow starts with clicking the checkbox in anchor iframe
  LOG_INFO("RecaptchaImageCaptchaProvider", "Step 1: Clicking checkbox");

  bool checkbox_clicked = ClickCheckbox(browser, context_id, classification);
  if (!checkbox_clicked) {
    LOG_WARN("RecaptchaImageCaptchaProvider", "Failed to click checkbox, may already be clicked");
  }

  // Wait for challenge to appear or auto-verify
  Wait(config_.post_checkbox_wait_ms);

  // ============ STEP 2: CHECK FOR AUTO-VERIFY OR CHALLENGE ============
  LOG_DEBUG("RecaptchaImageCaptchaProvider", "Step 2: Checking for auto-verify");

  // Check if already verified (no challenge appears)
  if (IsCheckboxVerified(browser)) {
    LOG_INFO("RecaptchaImageCaptchaProvider", "reCAPTCHA auto-verified - checkbox shows verified!");
    result.success = true;
    result.confidence = 1.0;
    result.attempts = 0;
    return result;
  }

  // Wait for challenge iframe with timeout
  CefRefPtr<CefFrame> challenge_frame = nullptr;
  const int max_wait_polls = 30;  // 3 seconds total

  for (int i = 0; i < max_wait_polls; i++) {
    challenge_frame = GetChallengeFrame(browser);
    if (challenge_frame) {
      LOG_INFO("RecaptchaImageCaptchaProvider", "Challenge frame appeared after " +
               std::to_string(i * 100) + "ms");
      break;
    }

    // Also check if it auto-verified while waiting
    if (IsCheckboxVerified(browser)) {
      LOG_INFO("RecaptchaImageCaptchaProvider", "reCAPTCHA auto-verified during wait!");
      result.success = true;
      result.confidence = 1.0;
      result.attempts = 0;
      return result;
    }

    Wait(100);
  }

  if (!challenge_frame) {
    // Final check for auto-verify
    if (IsCheckboxVerified(browser)) {
      result.success = true;
      result.confidence = 0.95;
      return result;
    }
    result.error_message = "Challenge iframe not found and not auto-verified";
    return result;
  }

  // ============ STEP 3: MAIN SOLVE LOOP ============
  for (int attempt = 0; attempt < max_attempts; attempt++) {
    result.attempts++;
    LOG_INFO("RecaptchaImageCaptchaProvider", "====== Attempt " + std::to_string(attempt + 1) +
             "/" + std::to_string(max_attempts) + " ======");

    // Reset tile states for this attempt
    std::vector<TileState> tile_states(16);  // Max 16 tiles for 4x4
    selected_tiles_.clear();

    // Wait for challenge to be ready and rescan
    WaitForChallengeTransition(browser, context_id, 2000);
    RescanChallengeFrame(browser, context_id);

    // Detect grid size (3x3 = 9 or 4x4 = 16)
    int grid_size = DetectGridSize(browser, context_id);
    current_grid_size_ = grid_size;
    bool is_4x4 = (grid_size == 16);
    LOG_INFO("RecaptchaImageCaptchaProvider", "Grid size: " + std::to_string(grid_size) +
             " (" + std::to_string(is_4x4 ? 4 : 3) + "x" +
             std::to_string(is_4x4 ? 4 : 3) + ")");

    // Extract target description
    std::string current_target = ExtractTarget(context_id, browser, classification);
    if (current_target.empty()) {
      current_target = classification.target_description;
    }
    if (current_target.empty()) {
      current_target = "objects";
    }
    result.target_detected = current_target;
    LOG_INFO("RecaptchaImageCaptchaProvider", "Target: '" + result.target_detected + "'");

    // Check if dynamic mode (tiles replace after clicking) - only for 3x3 grids
    // 4x4 grids are NEVER dynamic - they're always one image split into squares
    is_dynamic_mode_ = !is_4x4 && IsDynamicTileMode(browser);

    // Detect grid type: SEPARATE_IMAGES vs SLICED_IMAGE
    // This is CRITICAL for the vision prompt
    current_grid_type_ = DetectGridType(browser, context_id);

    if (is_4x4) {
      LOG_INFO("RecaptchaImageCaptchaProvider", "*** 4x4 SLICED IMAGE MODE (object detection) ***");
    } else if (is_dynamic_mode_) {
      LOG_INFO("RecaptchaImageCaptchaProvider", "*** DYNAMIC 3x3 SEPARATE IMAGES MODE ***");
    } else if (current_grid_type_ == RecaptchaGridType::SLICED_IMAGE) {
      LOG_INFO("RecaptchaImageCaptchaProvider", "*** STATIC 3x3 SLICED IMAGE MODE (object detection) ***");
    } else {
      LOG_INFO("RecaptchaImageCaptchaProvider", "*** STATIC 3x3 SEPARATE IMAGES MODE ***");
    }

    // Capture grid screenshot
    std::vector<uint8_t> grid_screenshot = CaptureGridScreenshot(browser, context_id);
    if (grid_screenshot.empty()) {
      LOG_ERROR("RecaptchaImageCaptchaProvider", "Failed to capture grid - refreshing");
      SkipChallenge(browser, context_id);
      Wait(2000);
      continue;
    }

    // Identify matching images with vision model
    std::vector<int> matching_indices = IdentifyMatchingImages(
        grid_screenshot, result.target_detected, grid_size, llm_client);

    LOG_INFO("RecaptchaImageCaptchaProvider", "Vision identified " +
             std::to_string(matching_indices.size()) + " matching tiles");

    // Handle "no matches" case differently based on grid type
    if (matching_indices.empty()) {
      if (is_4x4) {
        // For 4x4 grids, no matches might be valid - click Skip button
        LOG_INFO("RecaptchaImageCaptchaProvider", "No matches in 4x4 grid - clicking Skip");

        // Try to find and click Skip button (different from reload button)
        bool skip_clicked = ClickSkipButton(browser, context_id);
        if (skip_clicked) {
          Wait(2000);

          // Check if we succeeded
          if (IsCheckboxVerified(browser)) {
            LOG_INFO("RecaptchaImageCaptchaProvider", "*** reCAPTCHA SOLVED via Skip! ***");
            result.success = true;
            result.confidence = 0.85;
            result.attempts = attempt + 1;
            return result;
          }
          continue;  // Try next challenge
        }
      }

      LOG_WARN("RecaptchaImageCaptchaProvider", "No matches found - refreshing challenge");
      SkipChallenge(browser, context_id);
      Wait(2000);
      continue;
    }

    result.selected_indices = matching_indices;

    // Human-like thinking delay before clicking
    std::uniform_int_distribution<> think_delay(600, 1200);
    Wait(think_delay(gen));

    // ============ CLICK MATCHING TILES ============
    std::vector<int> shuffled_indices = matching_indices;
    std::shuffle(shuffled_indices.begin(), shuffled_indices.end(), gen);

    for (size_t i = 0; i < shuffled_indices.size(); i++) {
      int index = shuffled_indices[i];
      LOG_DEBUG("RecaptchaImageCaptchaProvider", "Clicking tile " + std::to_string(index));
      ClickGridItem(browser, context_id, index);
      selected_tiles_.insert(index);
      tile_states[index].selected = true;
      tile_states[index].click_count++;

      // Human-like inter-click delay
      if (i < shuffled_indices.size() - 1) {
        std::uniform_int_distribution<> click_delay(
            config_.click_delay_min_ms, config_.click_delay_max_ms);
        Wait(click_delay(gen));
      }
    }

    // ============ DYNAMIC MODE HANDLING ============
    // In dynamic mode, clicked tiles are replaced with new images
    // We need to keep checking and clicking until no more matches
    // This can require up to 5 rounds in some cases
    if (is_dynamic_mode_) {
      LOG_INFO("RecaptchaImageCaptchaProvider", "Entering dynamic mode loop (max 5 rounds)");

      std::set<int> tiles_to_check(shuffled_indices.begin(), shuffled_indices.end());
      const int max_dynamic_rounds = 5;  // Increased from 3 to handle more rounds
      int consecutive_empty = 0;  // Track consecutive rounds with no new matches

      for (int round = 0; round < max_dynamic_rounds && !tiles_to_check.empty(); round++) {
        LOG_INFO("RecaptchaImageCaptchaProvider", "Dynamic round " + std::to_string(round + 1) +
                  "/" + std::to_string(max_dynamic_rounds) +
                  " - checking " + std::to_string(tiles_to_check.size()) + " replaced tiles");

        // CRITICAL: Wait for tile fade-out AND fade-in animation to complete
        // Use smart waiting that checks for actual tile loading completion
        // Timeout of 3000ms should cover even slow network loads
        WaitForTileUpdate(browser, 3000);

        // Force rescan to get fresh tile positions (important if grid shifted)
        RescanChallengeFrame(browser, context_id);

        // Short buffer after rescan for DOM to settle
        Wait(100);

        std::vector<uint8_t> new_screenshot = CaptureGridScreenshot(browser, context_id);
        if (new_screenshot.empty()) {
          LOG_WARN("RecaptchaImageCaptchaProvider", "Failed to capture for dynamic check");
          break;
        }

        // Re-analyze ONLY the tiles that were just replaced
        // Build a custom prompt that tells the vision model to focus on specific tiles
        std::vector<int> all_matches = IdentifyMatchingImages(
            new_screenshot, result.target_detected, grid_size, llm_client);

        // CRITICAL: Only consider tiles that were just replaced
        // Other tiles still have their old images (already processed)
        std::vector<int> new_matches;
        for (int idx : all_matches) {
          if (tiles_to_check.count(idx) > 0) {
            new_matches.push_back(idx);
          }
        }

        if (new_matches.empty()) {
          consecutive_empty++;
          LOG_INFO("RecaptchaImageCaptchaProvider", "No matches in replaced tiles (consecutive: " +
                   std::to_string(consecutive_empty) + ")");

          // If 2 consecutive rounds with no matches, assume we're done
          if (consecutive_empty >= 2) {
            LOG_INFO("RecaptchaImageCaptchaProvider", "2 consecutive empty rounds - done with dynamic mode");
            break;
          }

          // Mark tiles as having new images but no matches
          for (int idx : tiles_to_check) {
            tile_states[idx].has_new_image = true;
          }
          tiles_to_check.clear();
          continue;
        }

        consecutive_empty = 0;  // Reset counter since we found matches
        LOG_INFO("RecaptchaImageCaptchaProvider", "Found " + std::to_string(new_matches.size()) +
                 " new matches in replaced tiles");

        // Reset for next round
        tiles_to_check.clear();

        // Human-like delay before clicking new matches
        std::uniform_int_distribution<> think_delay2(400, 800);
        Wait(think_delay2(gen));

        // Click new matches
        std::shuffle(new_matches.begin(), new_matches.end(), gen);
        for (size_t i = 0; i < new_matches.size(); i++) {
          int index = new_matches[i];
          ClickGridItem(browser, context_id, index);
          selected_tiles_.insert(index);
          tile_states[index].click_count++;
          tiles_to_check.insert(index);  // Track for next round
          result.selected_indices.push_back(index);

          if (i < new_matches.size() - 1) {
            std::uniform_int_distribution<> click_delay(
                config_.click_delay_min_ms, config_.click_delay_max_ms);
            Wait(click_delay(gen));
          }
        }
      }

      LOG_INFO("RecaptchaImageCaptchaProvider", "Dynamic mode complete - proceeding to verify");
    }

    // ============ SUBMIT VERIFICATION ============
    if (auto_submit_) {
      // Pre-submit delay - slightly longer for more natural behavior
      std::uniform_int_distribution<> pre_submit(400, 800);
      Wait(pre_submit(gen));

      LOG_INFO("RecaptchaImageCaptchaProvider", "Submitting verification...");

      if (!SubmitVerification(browser, context_id)) {
        LOG_ERROR("RecaptchaImageCaptchaProvider", "Failed to click verify button");
        continue;
      }

      // Wait for result - verification can take 2-4 seconds
      Wait(config_.post_submit_wait_ms);

      // ============ CHECK RESULT ============
      // First check if checkbox is verified (success)
      if (IsCheckboxVerified(browser)) {
        LOG_INFO("RecaptchaImageCaptchaProvider", "*** reCAPTCHA SOLVED! ***");
        result.success = true;
        result.confidence = 0.9;
        result.attempts = attempt + 1;
        return result;
      }

      // Check if challenge frame disappeared (another success indicator)
      challenge_frame = GetChallengeFrame(browser);
      if (!challenge_frame) {
        // Double-check checkbox
        if (IsCheckboxVerified(browser)) {
          LOG_INFO("RecaptchaImageCaptchaProvider", "*** reCAPTCHA SOLVED! (frame gone) ***");
          result.success = true;
          result.confidence = 0.85;
          result.attempts = attempt + 1;
          return result;
        }
        // Might be transitioning to new challenge
        Wait(500);
        challenge_frame = GetChallengeFrame(browser);
      }

      // Check for error messages
      std::string error = CheckForErrorMessage(browser, context_id);
      if (error == "retry") {
        LOG_WARN("RecaptchaImageCaptchaProvider", "Wrong selections - new challenge will appear");
        // reCAPTCHA typically auto-refreshes after wrong answer
        Wait(1500);
        continue;
      } else if (error == "select_more") {
        LOG_WARN("RecaptchaImageCaptchaProvider", "Missed some tiles - need to select more");

        // IMPORTANT: Handle "select more" case by re-analyzing without refreshing
        // The challenge is still showing - we just need to find additional tiles
        RescanChallengeFrame(browser, context_id);
        Wait(500);

        std::vector<uint8_t> more_screenshot = CaptureGridScreenshot(browser, context_id);
        if (!more_screenshot.empty()) {
          std::vector<int> additional_matches = IdentifyMatchingImages(
              more_screenshot, result.target_detected, grid_size, llm_client);

          // Click any tiles not already selected
          bool clicked_more = false;
          for (int idx : additional_matches) {
            if (selected_tiles_.count(idx) == 0) {
              LOG_INFO("RecaptchaImageCaptchaProvider", "Clicking additional tile: " + std::to_string(idx));
              ClickGridItem(browser, context_id, idx);
              selected_tiles_.insert(idx);
              clicked_more = true;
              Wait(GetRandomClickDelay());
            }
          }

          if (clicked_more) {
            // Try submitting again
            Wait(500);
            SubmitVerification(browser, context_id);
            Wait(config_.post_submit_wait_ms);

            if (IsCheckboxVerified(browser)) {
              LOG_INFO("RecaptchaImageCaptchaProvider", "*** reCAPTCHA SOLVED after selecting more! ***");
              result.success = true;
              result.confidence = 0.85;
              result.attempts = attempt + 1;
              return result;
            }
          }
        }

        // If still not solved, continue to next attempt
        Wait(500);
        continue;
      }

      // If we get here, might still be processing or got a new challenge
      LOG_WARN("RecaptchaImageCaptchaProvider", "Result unclear - checking status");
      Wait(1000);

      // One more verification check
      if (IsCheckboxVerified(browser)) {
        LOG_INFO("RecaptchaImageCaptchaProvider", "*** reCAPTCHA SOLVED (delayed confirmation)! ***");
        result.success = true;
        result.confidence = 0.8;
        result.attempts = attempt + 1;
        return result;
      }

    } else {
      // No auto-submit, just return after clicking
      result.success = true;
      result.confidence = 0.7;
      return result;
    }
  }

  // All attempts exhausted
  result.error_message = "All " + std::to_string(max_attempts) + " attempts failed";
  result.needs_skip = true;
  LOG_ERROR("RecaptchaImageCaptchaProvider", result.error_message);
  return result;
}

bool RecaptchaImageCaptchaProvider::IsAutoVerified(CefRefPtr<CefBrowser> browser,
                                                   const CaptchaClassificationResult& classification) {
  // Check if the checkbox is checked (green checkmark)
  // This would be in the anchor iframe

  // Look for the checked state class on the checkbox
  // reCAPTCHA adds class "recaptcha-checkbox-checked" when verified

  // For now, check if challenge frame doesn't appear after clicking
  CefRefPtr<CefFrame> challenge_frame = GetChallengeFrame(browser);
  if (!challenge_frame) {
    // No challenge frame might mean auto-verified
    // But we should also verify the checkbox shows success
    return false;  // Conservative - let caller handle this
  }

  return false;
}

bool RecaptchaImageCaptchaProvider::ClickCheckbox(CefRefPtr<CefBrowser> browser,
                                                  const std::string& context_id,
                                                  const CaptchaClassificationResult& classification) {
  LOG_INFO("RecaptchaImageCaptchaProvider", "Clicking reCAPTCHA checkbox using IPC");

  // The checkbox is in the anchor iframe
  // We need to find the iframe position and click using native CEF mouse events

#ifdef BUILD_UI
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (tracker) {
    // Step 1: Find the anchor iframe in main frame
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
        LOG_INFO("RecaptchaImageCaptchaProvider", "Found anchor iframe at: " +
                  std::to_string(anchor_x) + "," + std::to_string(anchor_y) +
                  " size: " + std::to_string(anchor_w) + "x" + std::to_string(anchor_h));
        break;
      }
    }

    // If anchor found via tracker, try scanning the anchor iframe for checkbox position
    if (anchor_x > 0 || anchor_y > 0) {
      // Find the anchor frame
      std::vector<CefString> frame_ids;
      browser->GetFrameIdentifiers(frame_ids);

      for (const CefString& frame_id : frame_ids) {
        CefRefPtr<CefFrame> frame = browser->GetFrameByIdentifier(frame_id);
        if (frame) {
          std::string url = frame->GetURL().ToString();
          if (url.find("recaptcha") != std::string::npos && url.find("anchor") != std::string::npos) {
            // Scan the anchor frame
            std::string anchor_context_id = context_id + "_recaptcha_anchor";
            tracker->ClearContext(anchor_context_id);

            CefRefPtr<CefProcessMessage> scan_msg = CefProcessMessage::Create("scan_element");
            CefRefPtr<CefListValue> args = scan_msg->GetArgumentList();
            args->SetString(0, anchor_context_id);
            args->SetString(1, "*");
            frame->SendProcessMessage(PID_RENDERER, scan_msg);

            // Wait for scan
            for (int i = 0; i < 20; i++) {
              CefDoMessageLoopWork();
              std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            // Find checkbox in anchor frame
            std::vector<ElementRenderInfo> anchor_elements = tracker->GetAllVisibleElements(anchor_context_id);
            for (const auto& elem : anchor_elements) {
              if (elem.className.find("recaptcha-checkbox") != std::string::npos ||
                  elem.id == "recaptcha-anchor") {
                // Calculate absolute position
                int abs_x = anchor_x + elem.x;
                int abs_y = anchor_y + elem.y;
                int center_x = abs_x + (elem.width / 2);
                int center_y = abs_y + (elem.height / 2);

                LOG_INFO("RecaptchaImageCaptchaProvider", "Native click on checkbox at absolute (" +
                          std::to_string(center_x) + "," + std::to_string(center_y) + ")");

                auto host = browser->GetHost();
                if (host) {
                  host->SetFocus(true);
                  CefMouseEvent mouse_event;
                  mouse_event.x = center_x;
                  mouse_event.y = center_y;
                  mouse_event.modifiers = 0;

                  host->SendMouseMoveEvent(mouse_event, false);
                  Wait(100);
                  host->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 1);
                  host->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 1);
                  return true;
                }
              }
            }

            // Fallback: Click center of anchor iframe (checkbox is usually centered)
            int center_x = anchor_x + (anchor_w / 2) - 10;  // Slightly left of center where checkbox is
            int center_y = anchor_y + (anchor_h / 2);

            LOG_INFO("RecaptchaImageCaptchaProvider", "Native click on anchor iframe center at (" +
                      std::to_string(center_x) + "," + std::to_string(center_y) + ")");

            auto host = browser->GetHost();
            if (host) {
              host->SetFocus(true);
              CefMouseEvent mouse_event;
              mouse_event.x = center_x;
              mouse_event.y = center_y;
              mouse_event.modifiers = 0;

              host->SendMouseMoveEvent(mouse_event, false);
              Wait(100);
              host->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 1);
              host->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 1);
              return true;
            }
            break;
          }
        }
      }
    }

    // If no anchor iframe found in tracker, trigger a scan of main frame
    LOG_INFO("RecaptchaImageCaptchaProvider", "Anchor iframe not found - triggering main frame scan");

    CefRefPtr<CefProcessMessage> scan_msg = CefProcessMessage::Create("scan_element");
    CefRefPtr<CefListValue> args = scan_msg->GetArgumentList();
    args->SetString(0, context_id);
    args->SetString(1, "*");
    browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, scan_msg);

    for (int i = 0; i < 20; i++) {
      CefDoMessageLoopWork();
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

        LOG_INFO("RecaptchaImageCaptchaProvider", "Native click on rescanned anchor at (" +
                  std::to_string(center_x) + "," + std::to_string(center_y) + ")");

        auto host = browser->GetHost();
        if (host) {
          host->SetFocus(true);
          CefMouseEvent mouse_event;
          mouse_event.x = center_x;
          mouse_event.y = center_y;
          mouse_event.modifiers = 0;

          host->SendMouseMoveEvent(mouse_event, false);
          Wait(100);
          host->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 1);
          host->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 1);
          return true;
        }
      }
    }
  }
#endif

  // Fallback for headless mode: Use JavaScript click
  LOG_WARN("RecaptchaImageCaptchaProvider", "Using JS fallback for checkbox click");

  std::vector<CefString> frame_ids;
  browser->GetFrameIdentifiers(frame_ids);

  for (const CefString& frame_id : frame_ids) {
    CefRefPtr<CefFrame> frame = browser->GetFrameByIdentifier(frame_id);
    if (frame) {
      std::string url = frame->GetURL().ToString();
      if (url.find("recaptcha") != std::string::npos && url.find("anchor") != std::string::npos) {
        std::string click_script = R"(
          (function() {
            var checkbox = document.querySelector('.recaptcha-checkbox-border');
            if (checkbox) {
              checkbox.click();
              return true;
            }
            var cb = document.querySelector('#recaptcha-anchor');
            if (cb) {
              cb.click();
              return true;
            }
            return false;
          })();
        )";

        frame->ExecuteJavaScript(click_script, frame->GetURL(), 0);
        return true;
      }
    }
  }

  return OwlCaptchaUtils::ClickElement(browser, context_id, config_.checkbox_selector);
}

std::string RecaptchaImageCaptchaProvider::ExtractTarget(const std::string& context_id,
                                                          CefRefPtr<CefBrowser> browser,
                                                          const CaptchaClassificationResult& classification) {
  LOG_DEBUG("RecaptchaImageCaptchaProvider", "Extracting target from reCAPTCHA challenge");

  CefRefPtr<CefFrame> frame = GetChallengeFrame(browser);
  if (!frame) {
    LOG_WARN("RecaptchaImageCaptchaProvider", "Challenge frame not found");
    return "";
  }

  // Get challenge context ID for this frame (must match CaptureGridScreenshot)
  std::string challenge_context_id = context_id + "_recaptcha_challenge";

  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (!tracker) {
    LOG_WARN("RecaptchaImageCaptchaProvider", "No render tracker available");
    return "";
  }

  // CRITICAL: Clear any stale data and trigger IPC scan of challenge frame FIRST
  // This ensures we have fresh element data before trying to extract target
  tracker->ClearContext(challenge_context_id);

  // Send IPC message to scan the challenge frame
  CefRefPtr<CefProcessMessage> scan_msg = CefProcessMessage::Create("scan_element");
  CefRefPtr<CefListValue> args = scan_msg->GetArgumentList();
  args->SetString(0, challenge_context_id);
  args->SetString(1, "*");  // Scan all elements
  frame->SendProcessMessage(PID_RENDERER, scan_msg);

  // Wait for IPC round-trip to complete
  for (int i = 0; i < 30; i++) {
    CefDoMessageLoopWork();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Now read elements from tracker
  std::vector<ElementRenderInfo> elements = tracker->GetAllVisibleElements(challenge_context_id);
  LOG_DEBUG("RecaptchaImageCaptchaProvider", "Found " + std::to_string(elements.size()) +
            " elements in challenge frame for target extraction");

  // Method 1: Look for STRONG elements with short text (the target word)
  // reCAPTCHA puts the target in <strong> like: <strong>cars</strong>
  for (const auto& elem : elements) {
    if (elem.tag == "STRONG" && !elem.text.empty() && elem.text.length() < 30) {
      std::string target = elem.text;
      // Clean up: Sometimes text includes nearby content, filter out instruction text
      // Look for common instruction phrases and truncate before them
      size_t cut_pos = target.find("If ");
      if (cut_pos == std::string::npos) cut_pos = target.find("Click ");
      if (cut_pos == std::string::npos) cut_pos = target.find("Select ");
      if (cut_pos == std::string::npos) cut_pos = target.find("verify");
      if (cut_pos != std::string::npos && cut_pos > 0) {
        target = target.substr(0, cut_pos);
      }
      // Trim trailing whitespace
      while (!target.empty() && (target.back() == ' ' || target.back() == '\n' || target.back() == '\t')) {
        target.pop_back();
      }
      if (!target.empty() && target.length() < 25) {
        LOG_INFO("RecaptchaImageCaptchaProvider", "Found target from STRONG element: '" + target + "'");
        return target;
      }
    }
  }

  // Method 2: Look for elements with rc-imageselect-desc class containing target text
  for (const auto& elem : elements) {
    if (elem.className.find("rc-imageselect-desc") != std::string::npos &&
        elem.className.find("wrapper") == std::string::npos) {
      // Parse "Select all images with X" pattern from text
      std::string text = elem.text;
      // Look for text after "with " or "containing "
      size_t pos = text.find(" with ");
      if (pos == std::string::npos) pos = text.find(" containing ");
      if (pos == std::string::npos) pos = text.find(" of ");

      if (pos != std::string::npos) {
        std::string target = text.substr(pos + 6);  // Skip " with " etc
        // Clean up: truncate before instruction phrases
        size_t cut_pos = target.find("If ");
        if (cut_pos == std::string::npos) cut_pos = target.find("Click ");
        if (cut_pos == std::string::npos) cut_pos = target.find("verify");
        if (cut_pos != std::string::npos && cut_pos > 0) {
          target = target.substr(0, cut_pos);
        }
        // Trim trailing period and whitespace
        while (!target.empty() && (target.back() == '.' || target.back() == ' ' || target.back() == '\n')) {
          target.pop_back();
        }
        if (!target.empty() && target.length() < 30) {
          LOG_INFO("RecaptchaImageCaptchaProvider", "Found target from desc element: '" + target + "'");
          return target;
        }
      }
    }
  }

  // Method 3: Check any element text for common reCAPTCHA target words
  for (const auto& elem : elements) {
    if (!elem.text.empty() && elem.text.length() < 50 && elem.text.length() > 2) {
      std::string text = elem.text;
      // Common reCAPTCHA targets - single words that are likely the target
      if ((text.find("bicycle") != std::string::npos && text.length() < 20) ||
          (text.find("bus") != std::string::npos && text.length() < 10) ||
          (text.find("car") != std::string::npos && text.length() < 10) ||
          (text.find("crosswalk") != std::string::npos && text.length() < 20) ||
          (text.find("fire hydrant") != std::string::npos) ||
          (text.find("motorcycle") != std::string::npos) ||
          (text.find("traffic light") != std::string::npos) ||
          (text.find("boat") != std::string::npos && text.length() < 10) ||
          (text.find("bridge") != std::string::npos && text.length() < 15) ||
          (text.find("chimney") != std::string::npos) ||
          (text.find("palm") != std::string::npos && text.length() < 15) ||
          (text.find("stair") != std::string::npos && text.length() < 15) ||
          (text.find("taxi") != std::string::npos && text.length() < 10) ||
          (text.find("tractor") != std::string::npos)) {
        LOG_INFO("RecaptchaImageCaptchaProvider", "Found target from element text: '" + text + "'");
        return text;
      }
    }
  }

  LOG_WARN("RecaptchaImageCaptchaProvider", "Could not extract target text from " +
           std::to_string(elements.size()) + " elements, using fallback");
  return "";
}

std::vector<uint8_t> RecaptchaImageCaptchaProvider::CaptureGridScreenshot(CefRefPtr<CefBrowser> browser,
                                                                           const std::string& context_id) {
  LOG_DEBUG("RecaptchaImageCaptchaProvider", "Capturing reCAPTCHA grid screenshot");

  CefRefPtr<CefFrame> challenge_frame = GetChallengeFrame(browser);
  if (!challenge_frame) {
    LOG_ERROR("RecaptchaImageCaptchaProvider", "Challenge frame not found");
    return {};
  }

  // Step 1: Get the challenge iframe's position in the main document
  // Execute JS in main frame to find the bframe iframe position
  std::string iframe_pos_script = R"(
    (function() {
      var iframe = document.querySelector('iframe[src*="bframe"]');
      if (!iframe) return null;
      var rect = iframe.getBoundingClientRect();
      window.__owl_iframe_pos = {
        x: Math.round(rect.left + window.scrollX),
        y: Math.round(rect.top + window.scrollY),
        width: Math.round(rect.width),
        height: Math.round(rect.height)
      };
      return window.__owl_iframe_pos;
    })();
  )";

  browser->GetMainFrame()->ExecuteJavaScript(iframe_pos_script, browser->GetMainFrame()->GetURL(), 0);
  Wait(100);

  // Step 2: Get the grid container and tile positions within the challenge iframe
  std::string grid_info_script = R"(
    (function() {
      var container = document.querySelector('.rc-imageselect-challenge');
      if (!container) {
        container = document.querySelector('.rc-imageselect-table-33, .rc-imageselect-table-44, .rc-imageselect-table');
      }
      if (!container) return null;

      var containerRect = container.getBoundingClientRect();

      // Get tile positions
      var tiles = document.querySelectorAll('.rc-imageselect-tile');
      var tileData = [];
      for (var i = 0; i < tiles.length && i < 16; i++) {
        var rect = tiles[i].getBoundingClientRect();
        tileData.push({
          x: Math.round(rect.left),
          y: Math.round(rect.top),
          width: Math.round(rect.width),
          height: Math.round(rect.height)
        });
      }

      window.__owl_grid_info = {
        container: {
          x: Math.round(containerRect.left),
          y: Math.round(containerRect.top),
          width: Math.round(containerRect.width),
          height: Math.round(containerRect.height)
        },
        tiles: tileData,
        tileCount: tileData.length
      };
      return window.__owl_grid_info;
    })();
  )";

  challenge_frame->ExecuteJavaScript(grid_info_script, challenge_frame->GetURL(), 0);
  Wait(200);

  // Step 3: Read back positions via render tracker or estimate
  // Since we can't directly get JS results, we'll use estimated positions
  // based on typical reCAPTCHA layout

  // Get client for native screenshot
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  if (!client_base) {
    LOG_ERROR("RecaptchaImageCaptchaProvider", "Failed to get browser client");
    return {};
  }

  // For UI mode, use native screenshot which captures what's actually on screen
  if (!OwlBrowserManager::UsesRunMessageLoop()) {
    LOG_WARN("RecaptchaImageCaptchaProvider", "Headless mode - iframe capture not supported");
    return {};
  }

#ifdef BUILD_UI
  // Use render tracker to get iframe and grid positions if available
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();

  // Try to find the challenge popup/iframe position
  int iframe_x = 0, iframe_y = 0, iframe_w = 400, iframe_h = 580;

  // CRITICAL: Get the bframe position via JavaScript in the main frame
  // The bframe is dynamically positioned and may not be in render tracker
  std::string bframe_pos_script = R"(
    (function() {
      var bframe = document.querySelector('iframe[src*="bframe"]');
      if (bframe) {
        var rect = bframe.getBoundingClientRect();
        // Store in a data attribute we can read
        document.body.setAttribute('data-owl-bframe-x', Math.round(rect.left));
        document.body.setAttribute('data-owl-bframe-y', Math.round(rect.top));
        document.body.setAttribute('data-owl-bframe-w', Math.round(rect.width));
        document.body.setAttribute('data-owl-bframe-h', Math.round(rect.height));
        return true;
      }
      return false;
    })();
  )";

  browser->GetMainFrame()->ExecuteJavaScript(bframe_pos_script, browser->GetMainFrame()->GetURL(), 0);

  // Pump message loop to let JS execute
  for (int i = 0; i < 10; i++) {
    CefDoMessageLoopWork();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Try to read the bframe position from render tracker (which scans body attributes)
  // Since we can't directly read JS results, trigger a quick scan to pick up the attribute
  if (tracker) {
    // First, check if we can find the bframe iframe directly
    std::vector<ElementRenderInfo> elements = tracker->GetAllVisibleElements(context_id);
    LOG_INFO("RecaptchaImageCaptchaProvider", "Scanning " + std::to_string(elements.size()) + " elements for bframe");

    for (const auto& elem : elements) {
      std::string tag_upper = elem.tag;
      std::transform(tag_upper.begin(), tag_upper.end(), tag_upper.begin(), ::toupper);

      if (tag_upper == "IFRAME") {
        LOG_DEBUG("RecaptchaImageCaptchaProvider", "Found iframe: " +
                  std::to_string(elem.width) + "x" + std::to_string(elem.height) +
                  " at (" + std::to_string(elem.x) + "," + std::to_string(elem.y) + ")");

        // Check if this is the large challenge popup iframe (bframe ~400x580)
        if (elem.width > 350 && elem.height > 400) {
          iframe_x = elem.x;
          iframe_y = elem.y;
          iframe_w = elem.width;
          iframe_h = elem.height;
          LOG_INFO("RecaptchaImageCaptchaProvider", "Found bframe in tracker at: " +
                    std::to_string(iframe_x) + "," + std::to_string(iframe_y) +
                    " size: " + std::to_string(iframe_w) + "x" + std::to_string(iframe_h));
          break;
        }
      }
    }
  }

  // If bframe not found in tracker, trigger a fresh scan of main frame
  // The bframe is dynamically created and might not have been scanned yet
  if (iframe_x == 0 && iframe_y == 0 && tracker) {
    LOG_INFO("RecaptchaImageCaptchaProvider", "Bframe not in initial scan - triggering main frame rescan");

    // Trigger scan of main frame
    CefRefPtr<CefProcessMessage> scan_msg = CefProcessMessage::Create("scan_element");
    CefRefPtr<CefListValue> args = scan_msg->GetArgumentList();
    args->SetString(0, context_id);
    args->SetString(1, "*");
    browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, scan_msg);

    // Wait for scan
    for (int i = 0; i < 20; i++) {
      CefDoMessageLoopWork();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Try again to find bframe
    std::vector<ElementRenderInfo> elements = tracker->GetAllVisibleElements(context_id);
    LOG_INFO("RecaptchaImageCaptchaProvider", "Rescan found " + std::to_string(elements.size()) + " elements");

    for (const auto& elem : elements) {
      std::string tag_upper = elem.tag;
      std::transform(tag_upper.begin(), tag_upper.end(), tag_upper.begin(), ::toupper);

      if (tag_upper == "IFRAME" && elem.width > 350 && elem.height > 400) {
        iframe_x = elem.x;
        iframe_y = elem.y;
        iframe_w = elem.width;
        iframe_h = elem.height;
        LOG_INFO("RecaptchaImageCaptchaProvider", "Found bframe in rescan at: " +
                  std::to_string(iframe_x) + "," + std::to_string(iframe_y));
        break;
      }
    }
  }

  // Still not found - look for anchor and estimate bframe position
  if (iframe_x == 0 && iframe_y == 0 && tracker) {
    std::vector<ElementRenderInfo> elements = tracker->GetAllVisibleElements(context_id);
    int anchor_x = 0, anchor_y = 0;

    for (const auto& elem : elements) {
      std::string tag_upper = elem.tag;
      std::transform(tag_upper.begin(), tag_upper.end(), tag_upper.begin(), ::toupper);

      if (tag_upper == "IFRAME" && elem.width > 250 && elem.width < 400 && elem.height < 150) {
        anchor_x = elem.x;
        anchor_y = elem.y;
        LOG_INFO("RecaptchaImageCaptchaProvider", "Found anchor at: " +
                  std::to_string(anchor_x) + "," + std::to_string(anchor_y) +
                  " size: " + std::to_string(elem.width) + "x" + std::to_string(elem.height));
        break;
      }
    }

    if (anchor_x > 0) {
      // reCAPTCHA bframe popup typically appears OVERLAPPING the anchor, not below it
      // The popup's top aligns with around the anchor's position or slightly above
      iframe_x = anchor_x;
      iframe_y = anchor_y - 60;  // Popup overlaps anchor, starts above its center
      if (iframe_y < 0) iframe_y = 0;
      LOG_INFO("RecaptchaImageCaptchaProvider", "Estimated bframe from anchor at: " +
                std::to_string(iframe_x) + "," + std::to_string(iframe_y));
    }
  }

  // Final fallback - use a reasonable default
  if (iframe_x == 0 && iframe_y == 0) {
    LOG_WARN("RecaptchaImageCaptchaProvider", "Could not find bframe - using viewport-based fallback");
    // Assume bframe is in the upper-left area of viewport
    iframe_x = 30;
    iframe_y = 100;
  }

  LOG_INFO("RecaptchaImageCaptchaProvider", "Bframe position: " +
            std::to_string(iframe_x) + "," + std::to_string(iframe_y) +
            " size: " + std::to_string(iframe_w) + "x" + std::to_string(iframe_h));

  // Build grid item positions for overlay
  // reCAPTCHA uses 3x3 grid (9 tiles) or 4x4 grid (16 tiles)
  std::vector<ElementRenderInfo> grid_items;

  // Default values
  int grid_size = 3;
  int tile_size = 126;  // 3x3 default
  int tile_gap = 2;

  // Grid position within the iframe (relative to iframe top-left)
  int grid_internal_x = 0;
  int grid_internal_y = 0;

  // Actual grid dimensions (calculated from tile positions)
  int actual_grid_width = 0;
  int actual_grid_height = 0;

  // Step 1: Trigger IPC scan of challenge frame to get DOM elements
  // This allows us to scan cross-origin iframe content via its own renderer
  if (challenge_frame) {
    std::string challenge_context_id = context_id + "_recaptcha_challenge";

    // CRITICAL: Clear any stale data from previous scans BEFORE triggering new scan
    // This ensures we get fresh tile positions (important when error message pushes grid)
    if (tracker) {
      tracker->ClearContext(challenge_context_id);
    }

    // Send scan request to the challenge frame's renderer process
    CefRefPtr<CefProcessMessage> scan_msg = CefProcessMessage::Create("scan_element");
    CefRefPtr<CefListValue> args = scan_msg->GetArgumentList();
    args->SetString(0, challenge_context_id);
    args->SetString(1, "*");  // Scan all elements
    challenge_frame->SendProcessMessage(PID_RENDERER, scan_msg);

    LOG_INFO("RecaptchaImageCaptchaProvider", "Triggered DOM scan for challenge frame context: " + challenge_context_id);

    // Pump message loop to process IPC round-trip (scan request -> renderer scan -> results back)
    // The renderer needs time to scan DOM and send results back
    for (int i = 0; i < 30; i++) {
      CefDoMessageLoopWork();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Now read elements from render tracker for the challenge context
    if (tracker) {
      std::vector<ElementRenderInfo> challenge_elements = tracker->GetAllVisibleElements(challenge_context_id);
      LOG_INFO("RecaptchaImageCaptchaProvider", "Challenge frame scan found " +
               std::to_string(challenge_elements.size()) + " elements");

      // Find grid tiles and detect grid size
      // Track MIN and MAX positions to calculate exact grid bounds
      int tile_count = 0;
      int min_tile_x = INT_MAX, min_tile_y = INT_MAX;
      int max_tile_x = 0, max_tile_y = 0;
      int tile_width = 0, tile_height = 0;
      int error_message_height = 0;  // Height of "Please try again" message if present

      for (const auto& elem : challenge_elements) {
        // Look for tile elements by class
        if (elem.className.find("rc-imageselect-tile") != std::string::npos) {
          tile_count++;
          // Track min/max positions to find exact grid bounds
          if (elem.x < min_tile_x) min_tile_x = elem.x;
          if (elem.y < min_tile_y) min_tile_y = elem.y;
          if (elem.x > max_tile_x) max_tile_x = elem.x;
          if (elem.y > max_tile_y) max_tile_y = elem.y;
          if (tile_width == 0) {
            tile_width = elem.width;
            tile_height = elem.height;
          }
          LOG_DEBUG("RecaptchaImageCaptchaProvider", "Found tile #" + std::to_string(tile_count) +
                    " at (" + std::to_string(elem.x) + "," + std::to_string(elem.y) +
                    ") size " + std::to_string(elem.width) + "x" + std::to_string(elem.height));
        }

        // Detect error message that pushes the grid down
        // Classes: rc-imageselect-incorrect-response, rc-imageselect-error-select-more, etc.
        if (elem.className.find("rc-imageselect-incorrect") != std::string::npos ||
            elem.className.find("rc-imageselect-error") != std::string::npos) {
          error_message_height = elem.height;
          LOG_INFO("RecaptchaImageCaptchaProvider", "Found error message element: " +
                   elem.className + " height: " + std::to_string(error_message_height));
        }

        // Check for table class to detect grid type
        if (elem.className.find("rc-imageselect-table-44") != std::string::npos) {
          grid_size = 4;
          tile_size = 90;
          LOG_INFO("RecaptchaImageCaptchaProvider", "Detected 4x4 grid from table class");
        } else if (elem.className.find("rc-imageselect-table-33") != std::string::npos) {
          grid_size = 3;
          tile_size = 126;
          LOG_INFO("RecaptchaImageCaptchaProvider", "Detected 3x3 grid from table class");
        }
      }

      // If error message was detected, the bframe has moved UP on the page
      // We need to re-scan main frame to get the updated bframe position
      if (error_message_height > 0) {
        LOG_INFO("RecaptchaImageCaptchaProvider", "Error message present (height: " +
                 std::to_string(error_message_height) + "px) - re-scanning for bframe position");

        // Re-scan main frame to get updated bframe position
        CefRefPtr<CefProcessMessage> rescan_msg = CefProcessMessage::Create("scan_element");
        CefRefPtr<CefListValue> rescan_args = rescan_msg->GetArgumentList();
        rescan_args->SetString(0, context_id);
        rescan_args->SetString(1, "*");
        browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, rescan_msg);

        // Wait for rescan
        for (int i = 0; i < 15; i++) {
          CefDoMessageLoopWork();
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Get updated bframe position
        std::vector<ElementRenderInfo> updated_elements = tracker->GetAllVisibleElements(context_id);
        for (const auto& elem : updated_elements) {
          std::string tag_upper = elem.tag;
          std::transform(tag_upper.begin(), tag_upper.end(), tag_upper.begin(), ::toupper);

          if (tag_upper == "IFRAME" && elem.width > 350 && elem.height > 400) {
            int new_iframe_y = elem.y;
            if (new_iframe_y != iframe_y) {
              LOG_INFO("RecaptchaImageCaptchaProvider", "Bframe moved from Y=" +
                       std::to_string(iframe_y) + " to Y=" + std::to_string(new_iframe_y));
              iframe_y = new_iframe_y;
              iframe_x = elem.x;
            }
            break;
          }
        }
      }

      LOG_INFO("RecaptchaImageCaptchaProvider", "Total tiles found: " + std::to_string(tile_count));

      // Use actual tile dimensions if found
      if (tile_width > 0 && tile_height > 0) {
        tile_size = tile_width;
        LOG_INFO("RecaptchaImageCaptchaProvider", "Using detected tile size: " + std::to_string(tile_size));
      }

      // Determine grid size from tile count if not detected from class
      if (tile_count >= 16) {
        grid_size = 4;
        if (tile_size == 126) tile_size = 90;  // Adjust if not already set
      } else if (tile_count >= 9 && grid_size != 4) {
        grid_size = 3;
      }

      // Use minimum tile position as grid start (top-left corner)
      // These positions are IFRAME-RELATIVE (from getBoundingClientRect in iframe context)
      if (min_tile_x < INT_MAX && min_tile_y < INT_MAX) {
        grid_internal_x = min_tile_x;
        grid_internal_y = min_tile_y;
        LOG_INFO("RecaptchaImageCaptchaProvider", "Grid top-left corner (iframe-relative): (" +
                 std::to_string(grid_internal_x) + "," + std::to_string(grid_internal_y) + ")");

        // Calculate actual grid dimensions from tile positions
        // This gives us exact grid size without assumed gaps
        if (max_tile_x > 0 && max_tile_y > 0 && tile_width > 0 && tile_height > 0) {
          actual_grid_width = max_tile_x - min_tile_x + tile_width;
          actual_grid_height = max_tile_y - min_tile_y + tile_height;
          LOG_INFO("RecaptchaImageCaptchaProvider", "Actual grid dimensions: " +
                   std::to_string(actual_grid_width) + "x" + std::to_string(actual_grid_height));
        }
      }

      // NOTE: Don't clear context here - SolveWithVision needs to query it for grid size detection
      // Context will be cleared at the start of next CaptureGridScreenshot or ExtractTarget call
    }
  }

  // Fallback: If we couldn't get grid info from iframe scan, estimate based on iframe position
  if (grid_internal_x == 0 && grid_internal_y == 0) {
    // Typical reCAPTCHA layout within 400x580 iframe:
    // - Header ~65px, grid starts around y=65-70
    // - Grid is centered, left padding ~9px for 3x3, ~17px for 4x4
    grid_internal_x = (grid_size == 4) ? 17 : 9;
    grid_internal_y = 65;
    LOG_WARN("RecaptchaImageCaptchaProvider", "Using fallback grid position: (" +
             std::to_string(grid_internal_x) + "," + std::to_string(grid_internal_y) + ")");
  }

  LOG_INFO("RecaptchaImageCaptchaProvider", "Detected " + std::to_string(grid_size) + "x" +
           std::to_string(grid_size) + " grid (tile size: " + std::to_string(tile_size) + "px)");

  // Calculate grid position in screen coordinates
  // iframe position + internal grid position within iframe
  int grid_start_x = iframe_x + grid_internal_x;
  int grid_start_y = iframe_y + grid_internal_y;

  LOG_INFO("RecaptchaImageCaptchaProvider", "Grid starts at screen position (" +
           std::to_string(grid_start_x) + "," + std::to_string(grid_start_y) + ")");

  // Build tile positions for numbered overlays
  for (int row = 0; row < grid_size; row++) {
    for (int col = 0; col < grid_size; col++) {
      ElementRenderInfo tile;
      tile.x = grid_start_x + col * (tile_size + tile_gap);
      tile.y = grid_start_y + row * (tile_size + tile_gap);
      tile.width = tile_size;
      tile.height = tile_size;
      tile.visible = true;
      grid_items.push_back(tile);
    }
  }

  LOG_INFO("RecaptchaImageCaptchaProvider", "Created " + std::to_string(grid_items.size()) +
           " grid items for overlay");

  // Calculate capture region to include the full grid
  // Use actual dimensions if available, otherwise fall back to formula
  int capture_x = grid_start_x;
  int capture_y = grid_start_y;
  int capture_w, capture_h;

  if (actual_grid_width > 0 && actual_grid_height > 0) {
    // Use exact dimensions from tile positions (no assumed gaps)
    capture_w = actual_grid_width;
    capture_h = actual_grid_height;
    LOG_INFO("RecaptchaImageCaptchaProvider", "Using actual grid dimensions for capture: " +
             std::to_string(capture_w) + "x" + std::to_string(capture_h));
  } else {
    // Fallback to formula-based calculation
    int total_grid_size = grid_size * tile_size + (grid_size - 1) * tile_gap;
    capture_w = total_grid_size;
    capture_h = total_grid_size;
    LOG_INFO("RecaptchaImageCaptchaProvider", "Using formula-based grid dimensions: " +
             std::to_string(capture_w) + "x" + std::to_string(capture_h));
  }

  LOG_DEBUG("RecaptchaImageCaptchaProvider", "Capturing region: " +
            std::to_string(capture_x) + "," + std::to_string(capture_y) +
            " size: " + std::to_string(capture_w) + "x" + std::to_string(capture_h));

  // Capture native screenshot with numbered overlays
  std::vector<uint8_t> png_data = CaptureNativeScreenshot(
      browser, capture_x, capture_y, capture_w, capture_h,
      grid_items, capture_x, capture_y);

  if (png_data.empty()) {
    LOG_ERROR("RecaptchaImageCaptchaProvider", "Native screenshot capture failed");
    return {};
  }

  // DEBUG: Save captured image to /tmp for debugging
  g_recaptcha_debug_counter++;
  std::string debug_path = "/tmp/recaptcha_grid_" + std::to_string(g_recaptcha_debug_counter) + ".png";
  std::ofstream debug_file(debug_path, std::ios::binary);
  if (debug_file.is_open()) {
    debug_file.write(reinterpret_cast<const char*>(png_data.data()), png_data.size());
    debug_file.close();
    LOG_INFO("RecaptchaImageCaptchaProvider", "DEBUG: Saved grid screenshot to " + debug_path);
  }

  LOG_INFO("RecaptchaImageCaptchaProvider", "Captured reCAPTCHA grid screenshot: " +
           std::to_string(png_data.size()) + " bytes");

  return png_data;
#else
  LOG_ERROR("RecaptchaImageCaptchaProvider", "UI mode screenshot not available in headless build");
  return {};
#endif
}

bool RecaptchaImageCaptchaProvider::ClickGridItem(CefRefPtr<CefBrowser> browser,
                                                  const std::string& context_id,
                                                  int grid_index) {
  LOG_DEBUG("RecaptchaImageCaptchaProvider", "Clicking reCAPTCHA tile " + std::to_string(grid_index));

  CefRefPtr<CefFrame> frame = GetChallengeFrame(browser);
  if (!frame) return false;

#ifdef BUILD_UI
  // USE NATIVE MOUSE EVENTS for more reliable clicking
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (tracker) {
    // Step 1: Get bframe position from main frame context
    int bframe_x = 0, bframe_y = 0;
    std::vector<ElementRenderInfo> main_elements = tracker->GetAllVisibleElements(context_id);
    for (const auto& elem : main_elements) {
      std::string tag_upper = elem.tag;
      std::transform(tag_upper.begin(), tag_upper.end(), tag_upper.begin(), ::toupper);
      if (tag_upper == "IFRAME" && elem.width > 350 && elem.height > 400) {
        bframe_x = elem.x;
        bframe_y = elem.y;
        LOG_DEBUG("RecaptchaImageCaptchaProvider", "Found bframe at: " +
                  std::to_string(bframe_x) + "," + std::to_string(bframe_y));
        break;
      }
    }

    // Step 2: Get tile positions from challenge frame context
    std::string challenge_context_id = context_id + "_recaptcha_challenge";
    std::vector<ElementRenderInfo> challenge_elements = tracker->GetAllVisibleElements(challenge_context_id);

    int tile_count = 0;
    for (const auto& elem : challenge_elements) {
      if (elem.className.find("rc-imageselect-tile") != std::string::npos) {
        if (tile_count == grid_index) {
          // Calculate absolute position = bframe + tile position within iframe
          int abs_x = bframe_x + elem.x;
          int abs_y = bframe_y + elem.y;
          int center_x = abs_x + (elem.width / 2);
          int center_y = abs_y + (elem.height / 2);

          LOG_DEBUG("RecaptchaImageCaptchaProvider", "Native click on tile " + std::to_string(grid_index) +
                    " at absolute (" + std::to_string(center_x) + "," + std::to_string(center_y) + ")");

          // Send native mouse events
          auto host = browser->GetHost();
          if (host) {
            host->SetFocus(true);
            CefMouseEvent mouse_event;
            mouse_event.x = center_x;
            mouse_event.y = center_y;
            mouse_event.modifiers = 0;

            host->SendMouseMoveEvent(mouse_event, false);
            Wait(50);
            host->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 1);  // Mouse down
            host->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 1);   // Mouse up
            return true;
          }
        }
        tile_count++;
      }
    }
    LOG_WARN("RecaptchaImageCaptchaProvider", "Tile " + std::to_string(grid_index) +
             " not found in tracker (found " + std::to_string(tile_count) + " tiles)");
  }
#endif

  // Fallback to JavaScript click (less reliable but works in headless)
  std::string click_script = R"(
    (function() {
      var tiles = document.querySelectorAll('.rc-imageselect-tile');
      if (tiles.length > )" + std::to_string(grid_index) + R"() {
        tiles[)" + std::to_string(grid_index) + R"(].click();
        return true;
      }
      return false;
    })();
  )";

  frame->ExecuteJavaScript(click_script, frame->GetURL(), 0);
  return true;
}

bool RecaptchaImageCaptchaProvider::SubmitVerification(CefRefPtr<CefBrowser> browser,
                                                        const std::string& context_id) {
  LOG_DEBUG("RecaptchaImageCaptchaProvider", "Submitting reCAPTCHA verification");

  CefRefPtr<CefFrame> frame = GetChallengeFrame(browser);
  if (!frame) return false;

#ifdef BUILD_UI
  // USE NATIVE MOUSE EVENTS for more reliable clicking
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (tracker) {
    // Step 1: Get bframe position from main frame context
    int bframe_x = 0, bframe_y = 0;
    std::vector<ElementRenderInfo> main_elements = tracker->GetAllVisibleElements(context_id);
    for (const auto& elem : main_elements) {
      std::string tag_upper = elem.tag;
      std::transform(tag_upper.begin(), tag_upper.end(), tag_upper.begin(), ::toupper);
      if (tag_upper == "IFRAME" && elem.width > 350 && elem.height > 400) {
        bframe_x = elem.x;
        bframe_y = elem.y;
        LOG_DEBUG("RecaptchaImageCaptchaProvider", "Found bframe at: " +
                  std::to_string(bframe_x) + "," + std::to_string(bframe_y));
        break;
      }
    }

    // Step 2: Get verify button position from challenge frame context
    std::string challenge_context_id = context_id + "_recaptcha_challenge";
    std::vector<ElementRenderInfo> challenge_elements = tracker->GetAllVisibleElements(challenge_context_id);

    for (const auto& elem : challenge_elements) {
      // Look for verify button by ID (selector contains "recaptcha-verify-button")
      if (elem.selector.find("recaptcha-verify-button") != std::string::npos ||
          elem.id == "recaptcha-verify-button") {
        // Calculate absolute position = bframe + button position within iframe
        int abs_x = bframe_x + elem.x;
        int abs_y = bframe_y + elem.y;
        int center_x = abs_x + (elem.width / 2);
        int center_y = abs_y + (elem.height / 2);

        LOG_DEBUG("RecaptchaImageCaptchaProvider", "Native click on verify button at absolute (" +
                  std::to_string(center_x) + "," + std::to_string(center_y) + ")");

        // Send native mouse events
        auto host = browser->GetHost();
        if (host) {
          host->SetFocus(true);
          CefMouseEvent mouse_event;
          mouse_event.x = center_x;
          mouse_event.y = center_y;
          mouse_event.modifiers = 0;

          host->SendMouseMoveEvent(mouse_event, false);
          Wait(50);
          host->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 1);  // Mouse down
          host->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 1);   // Mouse up
          return true;
        }
      }
    }
    LOG_WARN("RecaptchaImageCaptchaProvider", "Verify button not found in tracker");
  }
#endif

  // Fallback to JavaScript click
  std::string click_script = R"(
    (function() {
      var btn = document.querySelector('#recaptcha-verify-button');
      if (btn) {
        btn.click();
        return true;
      }
      return false;
    })();
  )";

  frame->ExecuteJavaScript(click_script, frame->GetURL(), 0);
  return true;
}

bool RecaptchaImageCaptchaProvider::SkipChallenge(CefRefPtr<CefBrowser> browser,
                                                   const std::string& context_id) {
  LOG_DEBUG("RecaptchaImageCaptchaProvider", "Refreshing/skipping reCAPTCHA challenge");

  CefRefPtr<CefFrame> frame = GetChallengeFrame(browser);
  if (!frame) return false;

#ifdef BUILD_UI
  // USE NATIVE MOUSE EVENTS for more reliable clicking
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (tracker) {
    // Step 1: Get bframe position from main frame context
    int bframe_x = 0, bframe_y = 0;
    std::vector<ElementRenderInfo> main_elements = tracker->GetAllVisibleElements(context_id);
    for (const auto& elem : main_elements) {
      std::string tag_upper = elem.tag;
      std::transform(tag_upper.begin(), tag_upper.end(), tag_upper.begin(), ::toupper);
      if (tag_upper == "IFRAME" && elem.width > 350 && elem.height > 400) {
        bframe_x = elem.x;
        bframe_y = elem.y;
        LOG_DEBUG("RecaptchaImageCaptchaProvider", "Found bframe at: " +
                  std::to_string(bframe_x) + "," + std::to_string(bframe_y));
        break;
      }
    }

    // Step 2: Get reload button position from challenge frame context
    std::string challenge_context_id = context_id + "_recaptcha_challenge";
    RescanChallengeFrame(browser, context_id);

    std::vector<ElementRenderInfo> challenge_elements = tracker->GetAllVisibleElements(challenge_context_id);

    for (const auto& elem : challenge_elements) {
      // Look for reload/skip button
      if (elem.selector.find("recaptcha-reload-button") != std::string::npos ||
          elem.id == "recaptcha-reload-button" ||
          elem.className.find("rc-imageselect-refresh") != std::string::npos) {
        // Calculate absolute position = bframe + button position within iframe
        int abs_x = bframe_x + elem.x;
        int abs_y = bframe_y + elem.y;
        int center_x = abs_x + (elem.width / 2);
        int center_y = abs_y + (elem.height / 2);

        LOG_DEBUG("RecaptchaImageCaptchaProvider", "Native click on reload button at absolute (" +
                  std::to_string(center_x) + "," + std::to_string(center_y) + ")");

        // Send native mouse events
        auto host = browser->GetHost();
        if (host) {
          host->SetFocus(true);
          CefMouseEvent mouse_event;
          mouse_event.x = center_x;
          mouse_event.y = center_y;
          mouse_event.modifiers = 0;

          host->SendMouseMoveEvent(mouse_event, false);
          Wait(50);
          host->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 1);  // Mouse down
          host->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 1);   // Mouse up
          return true;
        }
      }
    }
    LOG_WARN("RecaptchaImageCaptchaProvider", "Reload button not found in tracker");
  }
#endif

  // Fallback to JavaScript click
  std::string click_script = R"(
    (function() {
      var btn = document.querySelector('#recaptcha-reload-button');
      if (btn) {
        btn.click();
        return true;
      }
      // Also try skip button if available
      var skipBtn = document.querySelector('.rc-imageselect-skip');
      if (skipBtn) {
        skipBtn.click();
        return true;
      }
      return false;
    })();
  )";

  frame->ExecuteJavaScript(click_script, frame->GetURL(), 0);
  return true;
}

bool RecaptchaImageCaptchaProvider::CheckVerificationSuccess(const std::string& context_id,
                                                              CefRefPtr<CefBrowser> browser) {
  LOG_DEBUG("RecaptchaImageCaptchaProvider", "Checking reCAPTCHA verification status");

  // Poll for success indicators
  const int max_polls = 30;
  const int poll_interval_ms = 200;

  for (int i = 0; i < max_polls; i++) {
    // Check if challenge frame is gone (success)
    CefRefPtr<CefFrame> frame = GetChallengeFrame(browser);
    if (!frame) {
      LOG_INFO("RecaptchaImageCaptchaProvider", "Challenge frame gone - likely success");
      return true;
    }

    // Check for success checkmark in anchor frame
    std::vector<CefString> frame_ids;
    browser->GetFrameIdentifiers(frame_ids);

    for (const CefString& frame_id : frame_ids) {
      CefRefPtr<CefFrame> anchor = browser->GetFrameByIdentifier(frame_id);
      if (anchor) {
        std::string url = anchor->GetURL().ToString();
        if (url.find("recaptcha") != std::string::npos && url.find("anchor") != std::string::npos) {
          // Check for checked state - would need async JS result
          // For now, rely on challenge frame disappearing
          break;
        }
      }
    }

    Wait(poll_interval_ms);
  }

  // Check one more time
  CefRefPtr<CefFrame> frame = GetChallengeFrame(browser);
  if (!frame) {
    return true;
  }

  // Check if checkbox is verified
  if (IsCheckboxVerified(browser)) {
    LOG_INFO("RecaptchaImageCaptchaProvider", "Checkbox verified - success");
    return true;
  }

  return false;
}

int RecaptchaImageCaptchaProvider::DetectGridSize(CefRefPtr<CefBrowser> browser,
                                                   const std::string& context_id) {
  CefRefPtr<CefFrame> challenge_frame = GetChallengeFrame(browser);
  if (!challenge_frame) {
    LOG_WARN("RecaptchaImageCaptchaProvider", "Challenge frame not found for grid detection");
    return 9;  // Default to 3x3
  }

  std::string challenge_context_id = context_id + "_recaptcha_challenge";
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (!tracker) return 9;

  std::vector<ElementRenderInfo> elements = tracker->GetAllVisibleElements(challenge_context_id);

  int tile_count = 0;
  bool has_4x4_class = false;

  for (const auto& elem : elements) {
    if (elem.className.find("rc-imageselect-tile") != std::string::npos) {
      tile_count++;
    }
    if (elem.className.find("rc-imageselect-table-44") != std::string::npos) {
      has_4x4_class = true;
    }
  }

  if (has_4x4_class || tile_count >= 16) {
    current_grid_size_ = 16;
    LOG_INFO("RecaptchaImageCaptchaProvider", "Detected 4x4 grid (16 tiles)");
    return 16;
  }

  current_grid_size_ = 9;
  LOG_INFO("RecaptchaImageCaptchaProvider", "Detected 3x3 grid (" + std::to_string(tile_count) + " tiles)");
  return 9;
}

bool RecaptchaImageCaptchaProvider::IsTileSelected(CefRefPtr<CefBrowser> browser,
                                                    const std::string& context_id,
                                                    int tile_index) {
  CefRefPtr<CefFrame> frame = GetChallengeFrame(browser);
  if (!frame) return false;

  std::string challenge_context_id = context_id + "_recaptcha_challenge";
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (!tracker) return false;

  std::vector<ElementRenderInfo> elements = tracker->GetAllVisibleElements(challenge_context_id);

  int tile_count = 0;
  for (const auto& elem : elements) {
    if (elem.className.find("rc-imageselect-tile") != std::string::npos) {
      if (tile_count == tile_index) {
        // Check for selection indicators
        // Selected tiles have "rc-imageselect-tileselected" class or checkmark overlay
        if (elem.className.find("tileselected") != std::string::npos ||
            elem.className.find("selected") != std::string::npos) {
          return true;
        }
        return false;
      }
      tile_count++;
    }
  }

  return false;
}

std::set<int> RecaptchaImageCaptchaProvider::GetSelectedTiles(CefRefPtr<CefBrowser> browser,
                                                               const std::string& context_id) {
  std::set<int> selected;
  CefRefPtr<CefFrame> frame = GetChallengeFrame(browser);
  if (!frame) return selected;

  std::string challenge_context_id = context_id + "_recaptcha_challenge";
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (!tracker) return selected;

  std::vector<ElementRenderInfo> elements = tracker->GetAllVisibleElements(challenge_context_id);

  int tile_index = 0;
  for (const auto& elem : elements) {
    if (elem.className.find("rc-imageselect-tile") != std::string::npos) {
      // Check for selection indicators
      if (elem.className.find("tileselected") != std::string::npos ||
          elem.className.find("selected") != std::string::npos) {
        selected.insert(tile_index);
      }
      tile_index++;
    }
  }

  LOG_DEBUG("RecaptchaImageCaptchaProvider", "Found " + std::to_string(selected.size()) +
            " selected tiles");
  return selected;
}

std::string RecaptchaImageCaptchaProvider::CheckForErrorMessage(CefRefPtr<CefBrowser> browser,
                                                                 const std::string& context_id) {
  CefRefPtr<CefFrame> frame = GetChallengeFrame(browser);
  if (!frame) return "none";

  std::string challenge_context_id = context_id + "_recaptcha_challenge";
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (!tracker) return "none";

  // Rescan to get latest state
  RescanChallengeFrame(browser, context_id);

  std::vector<ElementRenderInfo> elements = tracker->GetAllVisibleElements(challenge_context_id);

  for (const auto& elem : elements) {
    // Error message classes
    if (elem.className.find("rc-imageselect-incorrect") != std::string::npos ||
        elem.className.find("rc-imageselect-error") != std::string::npos) {

      std::string text_lower = elem.text;
      std::transform(text_lower.begin(), text_lower.end(), text_lower.begin(), ::tolower);

      // "Please try again" - wrong selections
      if (text_lower.find("try again") != std::string::npos ||
          text_lower.find("incorrect") != std::string::npos) {
        LOG_WARN("RecaptchaImageCaptchaProvider", "Error: Please try again - wrong selections");
        return "retry";
      }

      // "Please select all matching images" - missed some
      if (text_lower.find("select all") != std::string::npos ||
          text_lower.find("also include") != std::string::npos ||
          text_lower.find("more") != std::string::npos) {
        LOG_WARN("RecaptchaImageCaptchaProvider", "Error: Select more images");
        return "select_more";
      }

      // Generic error
      LOG_WARN("RecaptchaImageCaptchaProvider", "Error detected: " + elem.text);
      return "error";
    }
  }

  return "none";
}

bool RecaptchaImageCaptchaProvider::WaitForChallengeTransition(CefRefPtr<CefBrowser> browser,
                                                                const std::string& context_id,
                                                                int timeout_ms) {
  LOG_DEBUG("RecaptchaImageCaptchaProvider", "Waiting for challenge transition");

  // Wait for the challenge to finish any animations/transitions
  // New challenges take ~500-1000ms to fully load

  const int check_interval = 100;
  int elapsed = 0;

  while (elapsed < timeout_ms) {
    Wait(check_interval);
    elapsed += check_interval;

    // Check if we have a valid challenge frame with tiles
    CefRefPtr<CefFrame> frame = GetChallengeFrame(browser);
    if (!frame) {
      // Challenge frame gone - might be success
      return true;
    }

    // Rescan and check for tiles
    RescanChallengeFrame(browser, context_id);

    std::string challenge_context_id = context_id + "_recaptcha_challenge";
    OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
    if (tracker) {
      std::vector<ElementRenderInfo> elements = tracker->GetAllVisibleElements(challenge_context_id);

      int tile_count = 0;
      for (const auto& elem : elements) {
        if (elem.className.find("rc-imageselect-tile") != std::string::npos) {
          tile_count++;
        }
      }

      if (tile_count >= 9) {
        LOG_DEBUG("RecaptchaImageCaptchaProvider", "Challenge ready with " +
                  std::to_string(tile_count) + " tiles");
        return true;
      }
    }
  }

  return false;
}

bool RecaptchaImageCaptchaProvider::IsCheckboxVerified(CefRefPtr<CefBrowser> browser) {
  // Find the anchor iframe and check for verified state
  std::vector<CefString> frame_ids;
  browser->GetFrameIdentifiers(frame_ids);

  for (const CefString& frame_id : frame_ids) {
    CefRefPtr<CefFrame> anchor = browser->GetFrameByIdentifier(frame_id);
    if (anchor) {
      std::string url = anchor->GetURL().ToString();
      if (url.find("recaptcha") != std::string::npos && url.find("anchor") != std::string::npos) {
        // Scan the anchor frame for verified state
        OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
        if (tracker) {
          std::string anchor_context_id = "recaptcha_anchor_verify_check";
          tracker->ClearContext(anchor_context_id);

          CefRefPtr<CefProcessMessage> scan_msg = CefProcessMessage::Create("scan_element");
          CefRefPtr<CefListValue> args = scan_msg->GetArgumentList();
          args->SetString(0, anchor_context_id);
          args->SetString(1, "*");
          anchor->SendProcessMessage(PID_RENDERER, scan_msg);

          // Wait for scan
          for (int i = 0; i < 15; i++) {
            CefDoMessageLoopWork();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
          }

          std::vector<ElementRenderInfo> elements = tracker->GetAllVisibleElements(anchor_context_id);
          for (const auto& elem : elements) {
            // Look for checkbox-checked class or aria-checked="true"
            if (elem.className.find("checkbox-checked") != std::string::npos ||
                elem.className.find("recaptcha-checkbox-checked") != std::string::npos) {
              LOG_INFO("RecaptchaImageCaptchaProvider", "Checkbox is verified (checked state)");
              return true;
            }
          }
        }
        break;
      }
    }
  }

  return false;
}

void RecaptchaImageCaptchaProvider::RescanChallengeFrame(CefRefPtr<CefBrowser> browser,
                                                          const std::string& context_id) {
  CefRefPtr<CefFrame> challenge_frame = GetChallengeFrame(browser);
  if (!challenge_frame) return;

  std::string challenge_context_id = context_id + "_recaptcha_challenge";
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (!tracker) return;

  // Clear previous data
  tracker->ClearContext(challenge_context_id);

  // Request fresh scan
  CefRefPtr<CefProcessMessage> scan_msg = CefProcessMessage::Create("scan_element");
  CefRefPtr<CefListValue> args = scan_msg->GetArgumentList();
  args->SetString(0, challenge_context_id);
  args->SetString(1, "*");
  challenge_frame->SendProcessMessage(PID_RENDERER, scan_msg);

  // Wait for scan completion
  for (int i = 0; i < 20; i++) {
    CefDoMessageLoopWork();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

bool RecaptchaImageCaptchaProvider::ClickSkipButton(CefRefPtr<CefBrowser> browser,
                                                      const std::string& context_id) {
  LOG_DEBUG("RecaptchaImageCaptchaProvider", "Looking for Skip button (for 4x4 no-match case)");

  CefRefPtr<CefFrame> frame = GetChallengeFrame(browser);
  if (!frame) {
    LOG_WARN("RecaptchaImageCaptchaProvider", "Challenge frame not found for Skip button");
    return false;
  }

#ifdef BUILD_UI
  // USE NATIVE MOUSE EVENTS for more reliable clicking
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (tracker) {
    // Step 1: Get bframe position from main frame context
    int bframe_x = 0, bframe_y = 0;
    std::vector<ElementRenderInfo> main_elements = tracker->GetAllVisibleElements(context_id);
    for (const auto& elem : main_elements) {
      std::string tag_upper = elem.tag;
      std::transform(tag_upper.begin(), tag_upper.end(), tag_upper.begin(), ::toupper);
      if (tag_upper == "IFRAME" && elem.width > 350 && elem.height > 400) {
        bframe_x = elem.x;
        bframe_y = elem.y;
        LOG_DEBUG("RecaptchaImageCaptchaProvider", "Found bframe at: " +
                  std::to_string(bframe_x) + "," + std::to_string(bframe_y));
        break;
      }
    }

    // Step 2: Get Skip button position from challenge frame context
    std::string challenge_context_id = context_id + "_recaptcha_challenge";
    RescanChallengeFrame(browser, context_id);

    std::vector<ElementRenderInfo> challenge_elements = tracker->GetAllVisibleElements(challenge_context_id);

    // Look for Skip button - it appears as text "Skip" or class containing "skip"
    for (const auto& elem : challenge_elements) {
      bool is_skip_button = false;

      // Check by class name
      if (elem.className.find("rc-imageselect-skip") != std::string::npos ||
          elem.className.find("skip") != std::string::npos) {
        is_skip_button = true;
      }

      // Check by element text if available
      // reCAPTCHA uses "Skip" button text when there are no matches in 4x4

      if (is_skip_button && elem.width > 20 && elem.height > 10) {
        // Calculate absolute position = bframe + button position within iframe
        int abs_x = bframe_x + elem.x;
        int abs_y = bframe_y + elem.y;
        int center_x = abs_x + (elem.width / 2);
        int center_y = abs_y + (elem.height / 2);

        LOG_INFO("RecaptchaImageCaptchaProvider", "Native click on Skip button at absolute (" +
                  std::to_string(center_x) + "," + std::to_string(center_y) + ")");

        // Send native mouse events
        auto host = browser->GetHost();
        if (host) {
          host->SetFocus(true);
          CefMouseEvent mouse_event;
          mouse_event.x = center_x;
          mouse_event.y = center_y;
          mouse_event.modifiers = 0;

          host->SendMouseMoveEvent(mouse_event, false);
          Wait(50);
          host->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 1);  // Mouse down
          host->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 1);   // Mouse up
          return true;
        }
      }
    }
    LOG_WARN("RecaptchaImageCaptchaProvider", "Skip button not found in tracker");
  }
#endif

  // Fallback to JavaScript click
  std::string click_script = R"(
    (function() {
      // Look for Skip button specifically (not reload)
      var skipBtn = document.querySelector('.rc-imageselect-skip-button');
      if (skipBtn) {
        skipBtn.click();
        return true;
      }
      // Also try alternative selectors
      skipBtn = document.querySelector('button.rc-button-default:not(#recaptcha-reload-button)');
      if (skipBtn && skipBtn.textContent.toLowerCase().includes('skip')) {
        skipBtn.click();
        return true;
      }
      // Try by text content
      var buttons = document.querySelectorAll('button');
      for (var i = 0; i < buttons.length; i++) {
        if (buttons[i].textContent.toLowerCase().includes('skip')) {
          buttons[i].click();
          return true;
        }
      }
      return false;
    })();
  )";

  frame->ExecuteJavaScript(click_script, frame->GetURL(), 0);
  LOG_INFO("RecaptchaImageCaptchaProvider", "Skip button click attempted via JavaScript");
  return true;
}

bool RecaptchaImageCaptchaProvider::WaitForTilesToLoad(CefRefPtr<CefBrowser> browser,
                                                         int timeout_ms) {
  LOG_DEBUG("RecaptchaImageCaptchaProvider", "Waiting for tiles to load (timeout: " +
            std::to_string(timeout_ms) + "ms)");

  CefRefPtr<CefFrame> frame = GetChallengeFrame(browser);
  if (!frame) return false;

  // Poll for loading spinners to disappear
  int elapsed = 0;
  const int poll_interval = 100;

  while (elapsed < timeout_ms) {
    // Check via JavaScript if any tiles are still loading
    // reCAPTCHA shows a loading spinner overlay on tiles being loaded
    // The class "rc-imageselect-dynamic-selected" indicates a tile that was clicked
    // and is showing the fade animation

    // For now, just use a simple time-based wait
    // A more robust approach would check for specific loading indicators

    // Check if tiles have background images set (indicates loaded)
    // This is difficult to do without async JS results

    Wait(poll_interval);
    elapsed += poll_interval;

    // After minimum wait, check if images appear stable
    if (elapsed >= 500) {
      // Assume loaded after 500ms minimum + any additional wait
      break;
    }
  }

  LOG_DEBUG("RecaptchaImageCaptchaProvider", "Tiles assumed loaded after " +
            std::to_string(elapsed) + "ms");
  return true;
}

RecaptchaGridType RecaptchaImageCaptchaProvider::DetectGridType(
    CefRefPtr<CefBrowser> browser,
    const std::string& context_id) {

  // 4x4 grids are ALWAYS sliced images (one photo divided into 16 squares)
  if (current_grid_size_ == 16) {
    return RecaptchaGridType::SLICED_IMAGE;
  }

  // Dynamic mode (tiles replace after clicking) = ALWAYS separate images
  // Each tile shows a different photo that gets replaced
  if (is_dynamic_mode_) {
    return RecaptchaGridType::SEPARATE_IMAGES;
  }

  // For static 3x3 grids, we need to detect from instruction text:
  // - "Select all images with [X]" = 9 separate photos (SEPARATE_IMAGES)
  // - "Select all squares with [X]" = one photo divided (SLICED_IMAGE)

  CefRefPtr<CefFrame> frame = GetChallengeFrame(browser);
  if (!frame) {
    // Default to SLICED_IMAGE for 3x3 - this is safer because
    // the vision model will look for parts of objects spanning squares
    LOG_WARN("RecaptchaImageCaptchaProvider", "Could not get frame for grid type detection, defaulting to SLICED");
    return RecaptchaGridType::SLICED_IMAGE;
  }

  // Check the challenge context for instruction text clues
  std::string challenge_context_id = context_id + "_recaptcha_challenge";
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();

  if (tracker) {
    std::vector<ElementRenderInfo> elements = tracker->GetAllVisibleElements(challenge_context_id);

    for (const auto& elem : elements) {
      // Check text content for "images" vs "squares" keywords
      std::string text_lower = elem.text;
      std::transform(text_lower.begin(), text_lower.end(), text_lower.begin(), ::tolower);

      // "Select all images with" = separate photos
      if (text_lower.find("select all images") != std::string::npos) {
        LOG_INFO("RecaptchaImageCaptchaProvider", "Detected SEPARATE_IMAGES from instruction: 'select all images'");
        return RecaptchaGridType::SEPARATE_IMAGES;
      }

      // "Select all squares with" = sliced image (object detection)
      if (text_lower.find("select all squares") != std::string::npos) {
        LOG_INFO("RecaptchaImageCaptchaProvider", "Detected SLICED_IMAGE from instruction: 'select all squares'");
        return RecaptchaGridType::SLICED_IMAGE;
      }

      // Check class for table type hints
      if (elem.className.find("rc-imageselect-table-33") != std::string::npos) {
        // 3x3 table found - check for other indicators
        // If there's a single background image set on table, it's sliced
        // This is a CSS indicator that's hard to detect from tracker
      }
    }
  }

  // If we couldn't determine from text, default based on common patterns:
  // - Most 3x3 non-dynamic challenges are sliced images in modern reCAPTCHA
  LOG_INFO("RecaptchaImageCaptchaProvider", "Could not definitively determine grid type, defaulting to SLICED_IMAGE");
  return RecaptchaGridType::SLICED_IMAGE;
}

std::string RecaptchaImageCaptchaProvider::BuildRecaptchaVisionPrompt(
    const std::string& target_description,
    int grid_size,
    RecaptchaGridType grid_type) {

  std::stringstream prompt;

  // Get object-specific hints from base class helper
  auto get_hints = [&target_description]() -> std::string {
    std::string lower_target = target_description;
    std::transform(lower_target.begin(), lower_target.end(), lower_target.begin(), ::tolower);

    if (lower_target.find("traffic light") != std::string::npos) {
      return "Traffic lights are vertical signal poles with red/yellow/green lights. "
             "Look for the pole AND the light housing - both parts count.";
    }
    if (lower_target.find("crosswalk") != std::string::npos) {
      return "Crosswalks are white painted stripes on roads for pedestrians. "
             "Look for parallel white lines on pavement.";
    }
    if (lower_target.find("bus") != std::string::npos) {
      return "Buses are large rectangular vehicles, taller and longer than cars. "
             "Include school buses (yellow), city buses, tour buses.";
    }
    if (lower_target.find("car") != std::string::npos) {
      return "Cars include sedans, SUVs, trucks, vans. They have 4 wheels and windows. "
             "Include partial views showing wheel, hood, or body.";
    }
    if (lower_target.find("bicycle") != std::string::npos || lower_target.find("bike") != std::string::npos) {
      return "Bicycles have 2 wheels, pedals, handlebars. Include parked or ridden bikes. "
             "Exclude motorcycles (have engines).";
    }
    if (lower_target.find("motorcycle") != std::string::npos) {
      return "Motorcycles have 2 wheels with an engine. Include sport bikes, cruisers, scooters.";
    }
    if (lower_target.find("fire hydrant") != std::string::npos || lower_target.find("hydrant") != std::string::npos) {
      return "Fire hydrants are short metal posts, usually red or yellow, on sidewalks. "
             "They have a rounded top with outlet caps.";
    }
    if (lower_target.find("stair") != std::string::npos) {
      return "Stairs are series of horizontal steps going up or down. "
             "Include indoor stairs, outdoor stairs, building entrances with steps.";
    }
    if (lower_target.find("bridge") != std::string::npos) {
      return "Bridges span over water, roads, or valleys. Look for railings and support structures.";
    }
    if (lower_target.find("palm") != std::string::npos) {
      return "Palm trees have tall trunks with fan-shaped or feather-shaped leaves at the top.";
    }
    if (lower_target.find("parking meter") != std::string::npos) {
      return "Parking meters are tall poles with a display/coin slot near the top on sidewalks.";
    }
    if (lower_target.find("chimney") != std::string::npos) {
      return "Chimneys are vertical structures on rooftops for smoke, usually brick or metal.";
    }
    if (lower_target.find("boat") != std::string::npos) {
      return "Boats float on water. Include sailboats, motorboats, kayaks, fishing boats.";
    }
    if (lower_target.find("taxi") != std::string::npos) {
      return "Taxis are marked cars, often yellow in US, black in UK. Look for roof signs.";
    }
    if (lower_target.find("tractor") != std::string::npos) {
      return "Tractors are large agricultural vehicles with big rear wheels, used for farming.";
    }

    return "Look carefully for \"" + target_description + "\" in the image.";
  };

  std::string hints = get_hints();
  int rows = (grid_size == 16) ? 4 : 3;
  int cols = rows;

  if (grid_type == RecaptchaGridType::SLICED_IMAGE) {
    // ONE large image divided into squares - objects SPAN multiple adjacent squares
    prompt << "Visual challenge: Find squares containing \"" << target_description << "\".\n\n";

    prompt << "IMAGE STRUCTURE:\n";
    prompt << "- This is ONE photo divided into " << grid_size << " squares (" << rows << " rows x " << cols << " columns)\n";
    prompt << "- Each square has a RED NUMBER (0-" << (grid_size-1) << ") in the TOP-LEFT corner\n";
    prompt << "- The numbers go left-to-right, top-to-bottom:\n";
    if (rows == 4) {
      prompt << "    0  1  2  3   (top row)\n";
      prompt << "    4  5  6  7\n";
      prompt << "    8  9  10 11\n";
      prompt << "    12 13 14 15  (bottom row)\n\n";
    } else {
      prompt << "    0  1  2   (top row)\n";
      prompt << "    3  4  5\n";
      prompt << "    6  7  8   (bottom row)\n\n";
    }

    prompt << "TARGET: " << target_description << "\n";
    prompt << hints << "\n\n";

    prompt << "INSTRUCTIONS:\n";
    prompt << "1. Look at the RED NUMBERS in each square's top-left corner\n";
    prompt << "2. Find ALL squares that contain ANY PART of \"" << target_description << "\"\n";
    prompt << "3. The object usually spans multiple ADJACENT squares (e.g., a vertical pole spans squares in a column)\n";
    prompt << "4. Include squares with even a small part of the target\n\n";

    prompt << "OUTPUT: Only the numbers separated by commas. Example: 1,2,5,6,9,10\n";
    prompt << "If no " << target_description << " visible: none";

  } else {
    // SEPARATE_IMAGES - 9 different photos, each independently has or doesn't have target
    prompt << "Visual challenge: Find photos containing \"" << target_description << "\".\n\n";

    prompt << "IMAGE STRUCTURE:\n";
    prompt << "- This shows " << grid_size << " SEPARATE photos arranged in a " << rows << "x" << cols << " grid\n";
    prompt << "- Each photo is DIFFERENT and INDEPENDENT from the others\n";
    prompt << "- Each photo has a RED NUMBER (0-" << (grid_size-1) << ") in the TOP-LEFT corner\n";
    prompt << "- The numbers go left-to-right, top-to-bottom:\n";
    prompt << "    0  1  2   (top row)\n";
    prompt << "    3  4  5\n";
    prompt << "    6  7  8   (bottom row)\n\n";

    prompt << "TARGET: " << target_description << "\n";
    prompt << hints << "\n\n";

    prompt << "INSTRUCTIONS:\n";
    prompt << "1. Look at the RED NUMBER in each photo's top-left corner\n";
    prompt << "2. Check each photo INDEPENDENTLY - they show different scenes\n";
    prompt << "3. Select photos where \"" << target_description << "\" is clearly visible\n";
    prompt << "4. A photo counts if it contains the target object anywhere in it\n\n";

    prompt << "OUTPUT: Only the numbers separated by commas. Example: 0,3,7\n";
    prompt << "If no photos contain " << target_description << ": none";
  }

  return prompt.str();
}

std::vector<int> RecaptchaImageCaptchaProvider::IdentifyMatchingImages(
    const std::vector<uint8_t>& grid_screenshot,
    const std::string& target_description,
    int grid_size,
    OwlLLMClient* llm_client) {

  LOG_DEBUG("RecaptchaImageCaptchaProvider", "Identifying images matching: '" +
            target_description + "' (grid size: " + std::to_string(grid_size) +
            ", type: " + (current_grid_type_ == RecaptchaGridType::SLICED_IMAGE ? "SLICED" : "SEPARATE") + ")");

  if (!llm_client) {
    LOG_ERROR("RecaptchaImageCaptchaProvider", "LLM client not available");
    return {};
  }

  // Enhance image for better vision model accuracy
  // Upscale to at least 800px minimum dimension with contrast and sharpening
  int enhanced_width, enhanced_height;
  std::vector<uint8_t> enhanced_screenshot = OwlImageEnhancer::EnhancePNGForVision(
      grid_screenshot, 800, enhanced_width, enhanced_height);

  // Use enhanced image if available, otherwise fall back to original
  const std::vector<uint8_t>& image_to_use = enhanced_screenshot.empty() ? grid_screenshot : enhanced_screenshot;

  // DEBUG: Save enhanced image
  if (!enhanced_screenshot.empty()) {
    std::string debug_path = "/tmp/recaptcha_enhanced_" + std::to_string(g_recaptcha_debug_counter) + ".png";
    std::ofstream debug_file(debug_path, std::ios::binary);
    if (debug_file.is_open()) {
      debug_file.write(reinterpret_cast<const char*>(enhanced_screenshot.data()), enhanced_screenshot.size());
      debug_file.close();
      LOG_INFO("RecaptchaImageCaptchaProvider", "DEBUG: Saved enhanced screenshot to " + debug_path);
    }
  }

  // Convert image to base64
  std::string image_base64 = Base64Encode(image_to_use);

  // Build reCAPTCHA-specific prompt based on grid type
  std::string prompt = BuildRecaptchaVisionPrompt(target_description, grid_size, current_grid_type_);

  // System prompt - use neutral wording to avoid safety filters
  std::string system_prompt;
  if (current_grid_type_ == RecaptchaGridType::SLICED_IMAGE) {
    system_prompt = "You are helping solve a visual challenge. RESPOND WITH ONLY NUMBERS. "
                    "The image shows ONE photo divided into numbered squares. "
                    "Output format: comma-separated numbers (e.g., 4,5,7,8) or 'none'. "
                    "DO NOT explain. DO NOT describe. ONLY OUTPUT THE NUMBERS.";
  } else {
    system_prompt = "You are helping solve a visual challenge. RESPOND WITH ONLY NUMBERS. "
                    "The image shows 9 separate photos in a grid, each numbered 0-8. "
                    "Output format: comma-separated numbers (e.g., 0,3,7) or 'none'. "
                    "DO NOT explain. DO NOT describe. ONLY OUTPUT THE NUMBERS.";
  }

  // Append strict output instruction to prompt
  prompt += "\n\nCRITICAL: Your response must be ONLY the numbers separated by commas (e.g., 0,2,5) or 'none'. "
            "Do not write any other text. Do not explain. Just the numbers.";

  // Call vision model
  LOG_DEBUG("RecaptchaImageCaptchaProvider", "Calling vision model...");
  LOG_DEBUG("RecaptchaImageCaptchaProvider", "Prompt length: " + std::to_string(prompt.length()));
  LOG_DEBUG("RecaptchaImageCaptchaProvider", "Full prompt:\n" + prompt);

  auto response = llm_client->CompleteWithImage(
      prompt,
      image_base64,
      system_prompt,
      100,   // max_tokens - increased for safety
      0.1    // low temperature for consistent output
  );

  if (!response.success) {
    LOG_ERROR("RecaptchaImageCaptchaProvider", "Vision model error: " + response.error);
    return {};
  }

  LOG_INFO("RecaptchaImageCaptchaProvider", "Vision model raw response: '" + response.content + "'");

  // Parse response using base class parser
  std::vector<int> indices = ParseVisionResponse(response.content, grid_size);

  LOG_INFO("RecaptchaImageCaptchaProvider", "Parsed " + std::to_string(indices.size()) +
           " matching indices: " + [&indices]() {
             std::string s;
             for (size_t i = 0; i < indices.size(); i++) {
               if (i > 0) s += ",";
               s += std::to_string(indices[i]);
             }
             return s.empty() ? "(none)" : s;
           }());

  return indices;
}
