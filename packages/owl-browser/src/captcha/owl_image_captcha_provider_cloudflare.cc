#include "owl_image_captcha_provider_cloudflare.h"
#include "owl_captcha_utils.h"
#include "owl_llm_client.h"
#include "owl_semantic_matcher.h"
#include "owl_client.h"
#include "owl_render_tracker.h"
#include "owl_browser_manager.h"
#include "logger.h"
#include <sstream>
#include <algorithm>
#include <random>
#include <thread>
#include <chrono>
#include <set>

CloudflareImageCaptchaProvider::CloudflareImageCaptchaProvider() {
  InitializeConfig();
  LOG_DEBUG("CloudflareImageCaptchaProvider", "Initialized");
}

void CloudflareImageCaptchaProvider::InitializeConfig() {
  // Cloudflare uses Turnstile for simple challenges and hCaptcha for image challenges
  // hCaptcha has similar structure to reCAPTCHA

  // Turnstile iframe
  config_.iframe_selector = "iframe[src*='challenges.cloudflare.com'], iframe[src*='turnstile']";

  // hCaptcha iframe (when image challenge is shown)
  config_.challenge_iframe_selector = "iframe[src*='hcaptcha.com'][src*='challenge']";

  config_.uses_iframe = true;

  // hCaptcha grid structure
  config_.grid_container_selector = ".challenge-container";
  config_.grid_item_selector = ".task-image";
  config_.grid_item_class = "task-image";
  config_.default_grid_size = 9;  // hCaptcha typically uses 3x3

  config_.challenge_container_selector = ".challenge";
  config_.challenge_title_selector = ".prompt-text";
  config_.target_text_selector = ".prompt-text";

  config_.checkbox_selector = ".cf-turnstile-wrapper, #cf-turnstile";
  config_.submit_button_selector = ".submit-button";
  config_.skip_button_selector = ".refresh-button";
  config_.refresh_button_selector = ".refresh-button";

  // Timing - Cloudflare may have bot detection
  config_.click_delay_min_ms = 400;
  config_.click_delay_max_ms = 700;
  config_.post_checkbox_wait_ms = 3000;  // Cloudflare may take time to load challenge
  config_.post_submit_wait_ms = 4000;    // Verification can be slow
  config_.grid_load_timeout_ms = 15000;  // Cloudflare can be slow
}

double CloudflareImageCaptchaProvider::DetectProvider(CefRefPtr<CefBrowser> browser,
                                                      const CaptchaClassificationResult& classification) {
  if (!browser || !browser->GetMainFrame()) {
    return 0.0;
  }

  double confidence = 0.0;

  // Check page URL for Cloudflare indicators
  std::string url = browser->GetMainFrame()->GetURL().ToString();
  if (url.find("challenges.cloudflare.com") != std::string::npos ||
      url.find("hcaptcha.com") != std::string::npos) {
    confidence += 0.8;
  }

  // Use JavaScript to check for Cloudflare/hCaptcha-specific elements and resources
  std::string detect_script = R"(
    (function() {
      var score = 0;

      // Check for Cloudflare Turnstile iframes
      var iframes = document.querySelectorAll('iframe');
      for (var i = 0; i < iframes.length; i++) {
        var src = iframes[i].src || '';
        if (src.includes('challenges.cloudflare.com') || src.includes('cloudflare.com/cdn-cgi')) {
          score += 0.5;
          break;
        }
        if (src.includes('hcaptcha.com')) {
          score += 0.4;
          break;
        }
      }

      // Check for Cloudflare/hCaptcha scripts
      var scripts = document.querySelectorAll('script');
      for (var i = 0; i < scripts.length; i++) {
        var src = scripts[i].src || '';
        if (src.includes('challenges.cloudflare.com') || src.includes('cloudflare.com/turnstile')) {
          score += 0.3;
          break;
        }
        if (src.includes('hcaptcha.com')) {
          score += 0.3;
          break;
        }
      }

      // Check for cf-turnstile or hcaptcha containers
      if (document.querySelector('.cf-turnstile, #cf-turnstile')) {
        score += 0.2;
      }
      if (document.querySelector('.h-captcha, #hcaptcha')) {
        score += 0.2;
      }

      // Check for hcaptcha object
      if (typeof hcaptcha !== 'undefined') {
        score += 0.2;
      }

      // Check for Cloudflare interstitial elements
      if (document.querySelector('#cf-wrapper, .cf-browser-verification')) {
        score += 0.3;
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

  LOG_DEBUG("CloudflareImageCaptchaProvider", "Detection confidence: " + std::to_string(confidence));

  return std::min(confidence, 1.0);
}

bool CloudflareImageCaptchaProvider::IsTurnstileOnly(CefRefPtr<CefBrowser> browser) {
  // Turnstile often auto-verifies without image challenge
  // Check if only Turnstile iframe is present (no hCaptcha)
  bool has_turnstile = OwlCaptchaUtils::IsVisible(browser, "iframe[src*='challenges.cloudflare.com']");
  bool has_hcaptcha = OwlCaptchaUtils::IsVisible(browser, "iframe[src*='hcaptcha.com']");

  return has_turnstile && !has_hcaptcha;
}

bool CloudflareImageCaptchaProvider::IsHCaptchaChallenge(CefRefPtr<CefBrowser> browser) {
  return OwlCaptchaUtils::IsVisible(browser, "iframe[src*='hcaptcha.com'][src*='challenge']");
}

CefRefPtr<CefFrame> CloudflareImageCaptchaProvider::GetHCaptchaFrame(CefRefPtr<CefBrowser> browser) {
  if (!browser) return nullptr;

  std::vector<CefString> frame_ids;
  browser->GetFrameIdentifiers(frame_ids);

  for (const CefString& frame_id : frame_ids) {
    CefRefPtr<CefFrame> frame = browser->GetFrameByIdentifier(frame_id);
    if (frame) {
      std::string url = frame->GetURL().ToString();
      if (url.find("hcaptcha.com") != std::string::npos) {
        LOG_DEBUG("CloudflareImageCaptchaProvider", "Found hCaptcha frame: " + url);
        return frame;
      }
    }
  }

  return nullptr;
}

bool CloudflareImageCaptchaProvider::HandleInterstitialPage(CefRefPtr<CefBrowser> browser) {
  // Cloudflare interstitial pages ("Just a moment...") often auto-solve
  // We might just need to wait for JavaScript to complete

  LOG_DEBUG("CloudflareImageCaptchaProvider", "Handling Cloudflare interstitial");

  // Wait for automatic verification
  const int max_wait_seconds = 10;
  for (int i = 0; i < max_wait_seconds * 2; i++) {
    // Check if we're past the interstitial
    std::string url = browser->GetMainFrame()->GetURL().ToString();
    if (url.find("challenges.cloudflare.com") == std::string::npos) {
      LOG_DEBUG("CloudflareImageCaptchaProvider", "Passed interstitial page");
      return true;
    }

    // Check for any clickable elements that appeared
    bool has_checkbox = OwlCaptchaUtils::IsVisible(browser, ".cf-turnstile");
    if (has_checkbox) {
      break;  // Checkbox appeared, proceed with solving
    }

    Wait(500);
  }

  return false;
}

ImageCaptchaSolveResult CloudflareImageCaptchaProvider::Solve(
    const std::string& context_id,
    CefRefPtr<CefBrowser> browser,
    const CaptchaClassificationResult& classification,
    OwlLLMClient* llm_client,
    int max_attempts) {

  LOG_DEBUG("CloudflareImageCaptchaProvider", "Starting solve (max_attempts=" + std::to_string(max_attempts) + ")");

  ImageCaptchaSolveResult result;
  result.success = false;
  result.confidence = 0.0;
  result.attempts = 0;
  result.needs_skip = false;
  result.provider = ImageCaptchaProviderType::CLOUDFLARE;

  if (!llm_client) {
    result.error_message = "LLM client not available";
    LOG_ERROR("CloudflareImageCaptchaProvider", result.error_message);
    return result;
  }

  // Check for interstitial page
  std::string url = browser->GetMainFrame()->GetURL().ToString();
  if (url.find("challenges.cloudflare.com") != std::string::npos) {
    if (HandleInterstitialPage(browser)) {
      // Interstitial auto-solved
      result.success = true;
      result.confidence = 1.0;
      return result;
    }
  }

  // Cloudflare flow:
  // 1. If Turnstile-only: Just wait for auto-verification
  // 2. If hCaptcha challenge: Handle similar to reCAPTCHA

  if (IsTurnstileOnly(browser)) {
    LOG_DEBUG("CloudflareImageCaptchaProvider", "Turnstile-only challenge, attempting auto-solve");

    ClickCheckbox(browser, context_id, classification);
    Wait(config_.post_checkbox_wait_ms);

    // Wait for auto-verification
    for (int i = 0; i < 30; i++) {
      if (IsAutoVerified(browser, classification)) {
        LOG_DEBUG("CloudflareImageCaptchaProvider", "Turnstile auto-verified");
        result.success = true;
        result.confidence = 1.0;
        return result;
      }

      if (IsHCaptchaChallenge(browser)) {
        LOG_DEBUG("CloudflareImageCaptchaProvider", "hCaptcha challenge appeared");
        break;
      }

      Wait(500);
    }
  }

  // Handle hCaptcha image challenge
  if (IsHCaptchaChallenge(browser)) {
    CefRefPtr<CefFrame> hcaptcha_frame = GetHCaptchaFrame(browser);

    if (!hcaptcha_frame) {
      result.error_message = "hCaptcha frame not found";
      return result;
    }

    for (int attempt = 0; attempt < max_attempts; attempt++) {
      result.attempts++;
      LOG_DEBUG("CloudflareImageCaptchaProvider", "hCaptcha attempt " + std::to_string(attempt + 1));

      Wait(1000);

      // Extract target
      std::string current_target = ExtractTarget(context_id, browser, classification);
      if (current_target.empty()) {
        current_target = "objects";
      }
      result.target_detected = current_target;

      // Capture grid
      std::vector<uint8_t> grid_screenshot = CaptureGridScreenshot(browser, context_id);

      if (grid_screenshot.empty()) {
        SkipChallenge(browser, context_id);
        Wait(2000);
        continue;
      }

      // Identify matches
      std::vector<int> matching_indices = IdentifyMatchingImages(
          grid_screenshot, result.target_detected,
          config_.default_grid_size, llm_client);

      if (matching_indices.empty()) {
        SkipChallenge(browser, context_id);
        Wait(2000);
        continue;
      }

      result.selected_indices = matching_indices;

      // Randomize and click
      static std::random_device rd;
      static std::mt19937 gen(rd());
      std::vector<int> shuffled = matching_indices;
      std::shuffle(shuffled.begin(), shuffled.end(), gen);

      std::uniform_int_distribution<> delay_dist(1000, 2000);
      Wait(delay_dist(gen));

      for (size_t i = 0; i < shuffled.size(); i++) {
        ClickGridItem(browser, context_id, shuffled[i]);

        if (i < shuffled.size() - 1) {
          std::uniform_int_distribution<> interval(
              config_.click_delay_min_ms, config_.click_delay_max_ms);
          Wait(interval(gen));
        }
      }

      Wait(500);

      if (auto_submit_) {
        SubmitVerification(browser, context_id);
        Wait(config_.post_submit_wait_ms);

        if (CheckVerificationSuccess(context_id, browser)) {
          LOG_DEBUG("CloudflareImageCaptchaProvider", "hCaptcha solved");
          result.success = true;
          result.confidence = 0.85;
          result.attempts = attempt + 1;
          return result;
        }
      } else {
        result.success = true;
        result.confidence = 0.7;
        return result;
      }
    }
  }

  // Final check for auto-verification
  if (IsAutoVerified(browser, classification)) {
    result.success = true;
    result.confidence = 0.9;
    return result;
  }

  result.error_message = "All attempts failed";
  result.needs_skip = true;
  return result;
}

bool CloudflareImageCaptchaProvider::IsAutoVerified(CefRefPtr<CefBrowser> browser,
                                                    const CaptchaClassificationResult& classification) {
  // Check if Cloudflare challenge is gone
  bool has_turnstile = OwlCaptchaUtils::IsVisible(browser, "iframe[src*='challenges.cloudflare.com']");
  bool has_hcaptcha = OwlCaptchaUtils::IsVisible(browser, "iframe[src*='hcaptcha.com']");
  bool has_cf_wrapper = OwlCaptchaUtils::IsVisible(browser, "#cf-wrapper, .cf-browser-verification");

  // If no challenge elements visible, likely verified
  if (!has_turnstile && !has_hcaptcha && !has_cf_wrapper) {
    return true;
  }

  // Check for success indicator
  bool has_success = OwlCaptchaUtils::IsVisible(browser, ".cf-turnstile-success, [data-success]");
  if (has_success) {
    return true;
  }

  return false;
}

bool CloudflareImageCaptchaProvider::ClickCheckbox(CefRefPtr<CefBrowser> browser,
                                                   const std::string& context_id,
                                                   const CaptchaClassificationResult& classification) {
  LOG_DEBUG("CloudflareImageCaptchaProvider", "Clicking Cloudflare checkbox");

  // Try clicking Turnstile widget
  std::vector<CefString> frame_ids;
  browser->GetFrameIdentifiers(frame_ids);

  for (const CefString& frame_id : frame_ids) {
    CefRefPtr<CefFrame> frame = browser->GetFrameByIdentifier(frame_id);
    if (frame) {
      std::string url = frame->GetURL().ToString();
      if (url.find("challenges.cloudflare.com") != std::string::npos ||
          url.find("turnstile") != std::string::npos) {
        LOG_DEBUG("CloudflareImageCaptchaProvider", "Found Turnstile frame: " + url);

        // Click the checkbox/widget
        std::string click_script = R"(
          (function() {
            var cb = document.querySelector('input[type="checkbox"], .checkbox, #cf-turnstile-wrapper');
            if (cb) {
              cb.click();
              return true;
            }
            // Try clicking the body (Turnstile sometimes captures whole area)
            document.body.click();
            return true;
          })();
        )";

        frame->ExecuteJavaScript(click_script, frame->GetURL(), 0);
        return true;
      }
    }
  }

  // Fallback to main frame selector
  return OwlCaptchaUtils::ClickElement(browser, context_id, config_.checkbox_selector);
}

std::string CloudflareImageCaptchaProvider::ExtractTarget(const std::string& context_id,
                                                           CefRefPtr<CefBrowser> browser,
                                                           const CaptchaClassificationResult& classification) {
  LOG_DEBUG("CloudflareImageCaptchaProvider", "Extracting target from Cloudflare challenge");

  CefRefPtr<CefFrame> frame = GetHCaptchaFrame(browser);
  if (!frame) return "";

  // hCaptcha prompt text contains the target
  // Would need async JS to get the actual text
  return "";
}

std::vector<uint8_t> CloudflareImageCaptchaProvider::CaptureGridScreenshot(CefRefPtr<CefBrowser> browser,
                                                                            const std::string& context_id) {
  LOG_DEBUG("CloudflareImageCaptchaProvider", "Capturing Cloudflare/hCaptcha grid screenshot");

  // Similar to reCAPTCHA, hCaptcha is in an iframe
  CefRefPtr<CefFrame> frame = GetHCaptchaFrame(browser);
  if (!frame) {
    LOG_ERROR("CloudflareImageCaptchaProvider", "hCaptcha frame not found");
    return {};
  }

  // For production: capture the iframe viewport region
  LOG_WARN("CloudflareImageCaptchaProvider", "Grid screenshot capture not fully implemented for iframe");

  return {};
}

bool CloudflareImageCaptchaProvider::ClickGridItem(CefRefPtr<CefBrowser> browser,
                                                   const std::string& context_id,
                                                   int grid_index) {
  LOG_DEBUG("CloudflareImageCaptchaProvider", "Clicking hCaptcha tile " + std::to_string(grid_index));

  CefRefPtr<CefFrame> frame = GetHCaptchaFrame(browser);
  if (!frame) return false;

  // hCaptcha uses .task-image class for tiles
  std::string click_script = R"(
    (function() {
      var tiles = document.querySelectorAll('.task-image');
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

bool CloudflareImageCaptchaProvider::SubmitVerification(CefRefPtr<CefBrowser> browser,
                                                         const std::string& context_id) {
  LOG_DEBUG("CloudflareImageCaptchaProvider", "Submitting Cloudflare verification");

  CefRefPtr<CefFrame> frame = GetHCaptchaFrame(browser);
  if (!frame) return false;

  std::string click_script = R"(
    (function() {
      var btn = document.querySelector('.submit-button, button[type="submit"]');
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

bool CloudflareImageCaptchaProvider::SkipChallenge(CefRefPtr<CefBrowser> browser,
                                                    const std::string& context_id) {
  LOG_DEBUG("CloudflareImageCaptchaProvider", "Refreshing Cloudflare challenge");

  CefRefPtr<CefFrame> frame = GetHCaptchaFrame(browser);
  if (!frame) return false;

  std::string click_script = R"(
    (function() {
      var btn = document.querySelector('.refresh-button, button.refresh');
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

bool CloudflareImageCaptchaProvider::CheckVerificationSuccess(const std::string& context_id,
                                                               CefRefPtr<CefBrowser> browser) {
  LOG_DEBUG("CloudflareImageCaptchaProvider", "Checking Cloudflare verification status");

  const int max_polls = 40;
  const int poll_interval_ms = 250;

  for (int i = 0; i < max_polls; i++) {
    if (IsAutoVerified(browser, CaptchaClassificationResult())) {
      LOG_DEBUG("CloudflareImageCaptchaProvider", "Verification success");
      return true;
    }

    CefRefPtr<CefFrame> frame = GetHCaptchaFrame(browser);
    if (!frame) {
      LOG_DEBUG("CloudflareImageCaptchaProvider", "hCaptcha frame gone - success");
      return true;
    }

    // Check for page redirect (successful verification often redirects)
    // Would need URL comparison logic

    Wait(poll_interval_ms);
  }

  return false;
}
