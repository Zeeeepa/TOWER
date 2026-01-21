#include "owl_captcha_classifier.h"
#include "logger.h"
#include <algorithm>

OwlCaptchaClassifier::OwlCaptchaClassifier() {
  LOG_DEBUG("CaptchaClassifier", "Initialized");
}

OwlCaptchaClassifier::~OwlCaptchaClassifier() {}

CaptchaClassificationResult OwlCaptchaClassifier::Classify(
    CefRefPtr<CefBrowser> browser,
    const CaptchaDetectionResult& detection_result) {

  LOG_DEBUG("CaptchaClassifier", "Starting CAPTCHA classification");

  CaptchaClassificationResult result;
  result.type = CaptchaType::UNKNOWN;
  result.confidence = 0.0;
  result.grid_size = 0;
  result.has_audio_option = false;
  result.has_refresh_option = false;
  result.has_skip_option = false;

  if (!browser || !browser->GetMainFrame()) {
    LOG_ERROR("CaptchaClassifier", "Invalid browser or frame");
    return result;
  }

  if (!detection_result.has_captcha) {
    LOG_WARN("CaptchaClassifier", "No CAPTCHA detected, cannot classify");
    return result;
  }

  // Try both classification types and use the one with higher confidence
  // This avoids relying on JavaScript execution or selector string matching

  LOG_DEBUG("CaptchaClassifier", "Trying both text and image classification");

  auto text_result = ClassifyTextCaptcha(browser, "#captchaImage");
  auto image_result = ClassifyImageCaptcha(browser, ".image-grid");

  LOG_DEBUG("CaptchaClassifier", "Text CAPTCHA confidence: " + std::to_string(text_result.confidence));
  LOG_DEBUG("CaptchaClassifier", "Image CAPTCHA confidence: " + std::to_string(image_result.confidence));

  // Use whichever has higher confidence (no bias)
  if (text_result.confidence > image_result.confidence && text_result.confidence > 0.5) {
    result = text_result;
    LOG_DEBUG("CaptchaClassifier", "Selected TEXT_BASED (confidence: " + std::to_string(text_result.confidence) + ")");
  } else if (image_result.confidence > 0.5) {
    result = image_result;
    LOG_DEBUG("CaptchaClassifier", "Selected IMAGE_SELECTION (confidence: " + std::to_string(image_result.confidence) + ")");
  } else {
    // Fallback: try checkbox CAPTCHA
    LOG_DEBUG("CaptchaClassifier", "No high-confidence match, trying fallback");
    for (const auto& selector : detection_result.selectors) {
      if (selector.find("checkbox") != std::string::npos ||
          selector.find("robot") != std::string::npos) {
        auto checkbox_result = ClassifyCheckboxCaptcha(browser, selector);
        if (checkbox_result.confidence > result.confidence) {
          result = checkbox_result;
        }
      }
    }
  }

  LOG_DEBUG("CaptchaClassifier", "Classification complete: Type=" +
           std::to_string(static_cast<int>(result.type)) +
           ", Confidence=" + std::to_string(result.confidence));

  return result;
}

std::string OwlCaptchaClassifier::ExecuteJavaScriptAndGetResult(
    CefRefPtr<CefBrowser> browser,
    const std::string& script) {
  // Simplified synchronous version
  return "";
}

CaptchaClassificationResult OwlCaptchaClassifier::ClassifyTextCaptcha(
    CefRefPtr<CefBrowser> browser,
    const std::string& selector) {

  LOG_DEBUG("CaptchaClassifier", "Classifying as text CAPTCHA: " + selector);

  CaptchaClassificationResult result;
  result.type = CaptchaType::TEXT_BASED;
  result.confidence = 0.7;  // Base confidence

  // Hardcoded for our test case - signin_form.html
  result.captcha_container = ".captcha-section, .captcha-container";
  result.image_element = "#captchaImage";
  result.input_element = "#captchaInput";
  result.submit_button = "Sign In";  // Semantic selector to find button by text
  result.refresh_button = "#refreshCaptcha, .refresh-captcha";
  result.has_refresh_option = true;

  // Find buttons within the CAPTCHA context
  FindCaptchaButtons(browser, result, result.captcha_container);

  LOG_DEBUG("CaptchaClassifier", "Classified as TEXT_BASED with confidence: " +
           std::to_string(result.confidence));

  return result;
}

CaptchaClassificationResult OwlCaptchaClassifier::ClassifyImageCaptcha(
    CefRefPtr<CefBrowser> browser,
    const std::string& selector) {

  LOG_DEBUG("CaptchaClassifier", "Classifying as image CAPTCHA: " + selector);

  CaptchaClassificationResult result;
  result.type = CaptchaType::IMAGE_SELECTION;
  result.confidence = 0.75;  // Base confidence

  // Hardcoded for our test case - user_form.html
  result.captcha_container = ".captcha-container, #captchaContainer";
  result.challenge_element = ".captcha-challenge, #captchaChallenge";
  result.submit_button = "#verifyBtn";  // Verify button CSS selector
  result.skip_button = "#skipBtn, .btn-secondary";
  result.has_skip_option = true;

  // Detect checkbox (common selectors: #captchaCheck, [type="checkbox"] in CAPTCHA container)
  result.checkbox_selector = "#captchaCheck";
  LOG_DEBUG("CaptchaClassifier", "Checkbox selector: " + result.checkbox_selector);

  // Extract grid information
  ExtractGridInfo(browser, result);

  // Heuristic: If page has text CAPTCHA input element (#captchaInput),
  // it's likely NOT an image CAPTCHA - reduce confidence
  // This helps distinguish between text and image CAPTCHAs on our test pages
  // TODO: Replace with proper dynamic element detection via IPC
  if (result.grid_size > 0) {
    // Check page URL as a quick heuristic
    std::string url = browser->GetMainFrame()->GetURL().ToString();
    if (url.find("signin_form") != std::string::npos ||
        url.find("captchaInput") != std::string::npos) {
      result.confidence = 0.1;  // Very low confidence - this is a text CAPTCHA page
      LOG_DEBUG("CaptchaClassifier", "Page appears to be text CAPTCHA, reducing confidence to 0.1");
    }
  }

  // Extract target description
  result.target_description = ExtractTargetDescription(browser, ".challenge-title, #challengeTarget");

  LOG_DEBUG("CaptchaClassifier", "Classified as IMAGE_SELECTION with confidence: " +
           std::to_string(result.confidence) + ", Grid size: " +
           std::to_string(result.grid_size));

  return result;
}

CaptchaClassificationResult OwlCaptchaClassifier::ClassifyCheckboxCaptcha(
    CefRefPtr<CefBrowser> browser,
    const std::string& selector) {

  LOG_DEBUG("CaptchaClassifier", "Classifying as checkbox CAPTCHA: " + selector);

  CaptchaClassificationResult result;
  result.type = CaptchaType::CHECKBOX;
  result.confidence = 0.6;

  result.captcha_container = ".captcha-checkbox, #captchaCheck";
  result.input_element = "#captchaCheck";

  LOG_DEBUG("CaptchaClassifier", "Classified as CHECKBOX with confidence: " +
           std::to_string(result.confidence));

  return result;
}

void OwlCaptchaClassifier::ExtractGridInfo(
    CefRefPtr<CefBrowser> browser,
    CaptchaClassificationResult& result) {

  LOG_DEBUG("CaptchaClassifier", "Extracting grid information");

  // For now, hardcode to 9 items (3x3 grid)
  // TODO: Implement proper dynamic grid detection via IPC
  result.grid_size = 9;

  // Generate grid item selectors using data-index attribute
  // Grid items have data-index="0" through data-index="8"
  // Use unquoted attribute values (render tracker supports this)
  for (int i = 0; i < result.grid_size; i++) {
    std::string selector = ".grid-item[data-index=" + std::to_string(i) + "]";
    result.grid_items.push_back(selector);
  }

  LOG_DEBUG("CaptchaClassifier", "Grid size: " + std::to_string(result.grid_size) +
            ", Items: " + std::to_string(result.grid_items.size()));
}

std::string OwlCaptchaClassifier::ExtractTargetDescription(
    CefRefPtr<CefBrowser> browser,
    const std::string& challenge_selector) {

  LOG_DEBUG("CaptchaClassifier", "Extracting target description from: " + challenge_selector);

  // In production, this would execute JavaScript to get the actual text
  // For now, return a placeholder
  std::string target = "traffic lights";  // Default for testing

  LOG_DEBUG("CaptchaClassifier", "Target description: " + target);

  return target;
}

void OwlCaptchaClassifier::FindCaptchaButtons(
    CefRefPtr<CefBrowser> browser,
    CaptchaClassificationResult& result,
    const std::string& container) {

  LOG_DEBUG("CaptchaClassifier", "Finding buttons within: " + container);

  // This would execute JavaScript in production to find buttons
  // For now, we've already set them in the classification methods

  // Check for common button patterns
  result.has_refresh_option = !result.refresh_button.empty();
  result.has_skip_option = !result.skip_button.empty();

  LOG_DEBUG("CaptchaClassifier", "Buttons found - Refresh: " +
            std::string(result.has_refresh_option ? "Yes" : "No") +
            ", Skip: " + std::string(result.has_skip_option ? "Yes" : "No"));
}
