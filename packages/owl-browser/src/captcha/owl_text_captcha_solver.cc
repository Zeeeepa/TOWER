#include "owl_text_captcha_solver.h"
#include "owl_captcha_utils.h"
#include "owl_llm_client.h"
#include "owl_client.h"
#include "owl_semantic_matcher.h"
#include "logger.h"
#include <fstream>
#include <algorithm>

OlibTextCaptchaSolver::OlibTextCaptchaSolver(OwlLLMClient* llm_client)
    : llm_client_(llm_client), auto_submit_(false) {  // Disabled: test should click submit explicitly
  LOG_DEBUG("TextCaptchaSolver", "Initialized");
}

OlibTextCaptchaSolver::~OlibTextCaptchaSolver() {}

TextCaptchaSolveResult OlibTextCaptchaSolver::Solve(
    const std::string& context_id,
    CefRefPtr<CefBrowser> browser,
    const CaptchaClassificationResult& classification,
    int max_attempts) {

  LOG_DEBUG("TextCaptchaSolver", "Starting text CAPTCHA solve (context: " + context_id +
           ", max attempts: " + std::to_string(max_attempts) + ")");

  TextCaptchaSolveResult result;
  result.success = false;
  result.confidence = 0.0;
  result.attempts = 0;
  result.needs_refresh = false;

  if (!llm_client_) {
    result.error_message = "LLM client not available";
    LOG_ERROR("TextCaptchaSolver", result.error_message);
    return result;
  }

  if (classification.type != CaptchaType::TEXT_BASED) {
    result.error_message = "Not a text-based CAPTCHA";
    LOG_ERROR("TextCaptchaSolver", result.error_message);
    return result;
  }

  if (classification.image_element.empty() || classification.input_element.empty()) {
    result.error_message = "Missing image or input element selectors";
    LOG_ERROR("TextCaptchaSolver", result.error_message);
    return result;
  }

  // Attempt to solve with retries
  for (int attempt = 0; attempt < max_attempts; attempt++) {
    result.attempts++;
    LOG_DEBUG("TextCaptchaSolver", "Attempt " + std::to_string(attempt + 1) + "/" +
             std::to_string(max_attempts));

    // Unfreeze and trigger element scan to ensure CAPTCHA image is tracked
    CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
    OwlClient* client = nullptr;
    if (client_base) {
      client = static_cast<OwlClient*>(client_base.get());
      client->UnfreezeFrameCache();
      LOG_DEBUG("TextCaptchaSolver", "Unfroze cache for element scanning");
    }

    // Scroll CAPTCHA into view and capture immediately to avoid DOM changes
    OwlCaptchaUtils::ScrollIntoView(browser, classification.image_element, "center");
    OwlCaptchaUtils::Wait(500);  // Wait for scroll and visibility

    // Trigger element scan to ensure image element is tracked
    if (client) {
      client->WaitForElementScan(browser, context_id, 2000);
      LOG_DEBUG("TextCaptchaSolver", "Element scan complete");
    }

    // Capture screenshot of CAPTCHA image immediately
    LOG_DEBUG("TextCaptchaSolver", "Capturing CAPTCHA image: " +
              classification.image_element);

    std::vector<uint8_t> image_data = CaptureImageElement(browser, context_id,
                                                           classification.image_element);

    if (image_data.empty()) {
      LOG_ERROR("TextCaptchaSolver", "Failed to capture CAPTCHA image");
      result.error_message = "Failed to capture image";
      continue;
    }

    LOG_DEBUG("TextCaptchaSolver", "Image captured: " +
              std::to_string(image_data.size()) + " bytes");

    // Save captured image for debugging (only in debug builds)
#ifdef OWL_DEBUG_BUILD
    std::string debug_path = "/tmp/captcha_crop_attempt_" + std::to_string(attempt + 1) + ".png";
    std::ofstream debug_file(debug_path, std::ios::binary);
    if (debug_file.is_open()) {
      debug_file.write(reinterpret_cast<const char*>(image_data.data()), image_data.size());
      debug_file.close();
      LOG_DEBUG("TextCaptchaSolver", "Saved cropped image to " + debug_path);
    }
#endif

    // Extract text using vision model
    std::string extracted = ExtractTextWithVision(image_data);

    if (extracted.empty()) {
      LOG_ERROR("TextCaptchaSolver", "Failed to extract text from image");
      result.error_message = "Vision model failed to extract text";

      // Try refreshing and retry
      if (attempt < max_attempts - 1 && !classification.refresh_button.empty()) {
        LOG_DEBUG("TextCaptchaSolver", "Refreshing CAPTCHA for retry");
        RefreshCaptcha(browser, context_id, classification.refresh_button);
        OwlCaptchaUtils::Wait(1000);  // Wait for new CAPTCHA to load
      }
      continue;
    }

    result.extracted_text = extracted;
    LOG_DEBUG("TextCaptchaSolver", "Extracted text: '" + extracted + "'");

    // Unfreeze cache before entering text (it was frozen after image capture)
    if (client) {
      client->UnfreezeFrameCache();
      LOG_DEBUG("TextCaptchaSolver", "Unfroze cache before text entry");
    }

    // Enter the extracted text
    bool entered = EnterText(browser, context_id, classification.input_element, extracted);

    if (!entered) {
      LOG_ERROR("TextCaptchaSolver", "Failed to enter text into input field");
      result.error_message = "Failed to enter text";
      continue;
    }

    // Wait for DOM to update and allow a paint to capture the text entry
    OwlCaptchaUtils::Wait(200);

    LOG_DEBUG("TextCaptchaSolver", "Text entered successfully");

    // Submit if auto-submit is enabled
    if (auto_submit_ && !classification.submit_button.empty()) {
      LOG_DEBUG("TextCaptchaSolver", "Submitting CAPTCHA");

      bool submitted = SubmitCaptcha(browser, context_id, classification.submit_button);

      if (!submitted) {
        LOG_ERROR("TextCaptchaSolver", "Failed to submit");
        result.error_message = "Failed to submit";
        continue;
      }

      // Wait for verification
      // Use input_element or captcha_container as challenge selector to check if hidden after success
      std::string verification_selector = !classification.input_element.empty() ?
                                          classification.input_element :
                                          classification.captcha_container;
      bool verified = WaitForVerification(context_id, browser, verification_selector, 5000);

      // For text CAPTCHAs, we submit once and accept the result
      // No retries after submission - the text was entered and submitted
      result.success = verified;
      result.confidence = verified ? 0.9 : 0.5;

      if (verified) {
        LOG_DEBUG("TextCaptchaSolver", "CAPTCHA solved successfully!");
      } else {
        LOG_DEBUG("TextCaptchaSolver", "CAPTCHA submitted but verification unclear");
      }

      return result;
    } else {
      // Not auto-submitting, consider it success if text was entered
      LOG_DEBUG("TextCaptchaSolver", "Text entered (auto-submit disabled)");
      result.success = true;
      result.confidence = 0.7;  // Moderate confidence without verification
      return result;
    }
  }

  // All attempts failed
  result.error_message = "All attempts failed";
  result.needs_refresh = true;
  LOG_ERROR("TextCaptchaSolver", "Failed to solve CAPTCHA after " +
            std::to_string(max_attempts) + " attempts");

  return result;
}

std::vector<uint8_t> OlibTextCaptchaSolver::CaptureImageElement(
    CefRefPtr<CefBrowser> browser,
    const std::string& context_id,
    const std::string& image_selector) {

  LOG_DEBUG("TextCaptchaSolver", "Capturing image element: " + image_selector + " (context: " + context_id + ")");

  // Use utility function to capture element screenshot (with DOM rescan)
  return OwlCaptchaUtils::CaptureElementScreenshot(browser, context_id, image_selector);
}

std::string OlibTextCaptchaSolver::ExtractTextWithVision(
    const std::vector<uint8_t>& image_data) {

  LOG_DEBUG("TextCaptchaSolver", "Extracting text with vision model");

  if (!llm_client_) {
    LOG_ERROR("TextCaptchaSolver", "LLM client not available");
    return "";
  }

  // Convert image to base64
  std::string image_base64 = OwlCaptchaUtils::Base64Encode(image_data);

  // Create vision prompt - optimized for direct character output
  std::string prompt = R"(You are analyzing a CAPTCHA image. The image contains exactly 5 distorted alphanumeric characters (mix of uppercase letters A-Z and numbers 0-9).

Your task is to identify these 5 characters. Respond with ONLY those 5 characters, no other text.

For example, if you see the characters A, B, 3, K, 9 then respond with exactly: AB3K9

Do NOT include:
- Explanations
- Punctuation
- Quotes
- Spaces (unless they are in the CAPTCHA itself)
- Any other commentary

Just the 5 characters.)";

  std::string system_prompt = "You are a precise CAPTCHA OCR system. Output only the raw characters.";

  // Call vision model
  LOG_DEBUG("TextCaptchaSolver", "Calling vision model...");

  auto response = llm_client_->CompleteWithImage(prompt, image_base64, system_prompt, 50, 0.1);

  if (!response.success) {
    LOG_ERROR("TextCaptchaSolver", "Vision model error: " + response.error);
    return "";
  }

  // Extract and clean the response using multi-strategy approach
  std::string vision_response = response.content;
  std::string extracted = "";

  LOG_DEBUG("TextCaptchaSolver", "Raw vision model response: '" + vision_response + "'");

  // Strategy 1: Look for text after colon (e.g., "characters: AB3K9")
  size_t colon_pos = vision_response.find(':');
  if (colon_pos != std::string::npos) {
    std::string after_colon = vision_response.substr(colon_pos + 1);
    // Trim whitespace
    after_colon.erase(0, after_colon.find_first_not_of(" \t\n\r"));
    after_colon.erase(after_colon.find_last_not_of(" \t\n\r") + 1);

    // Extract alphanumeric sequence
    std::string temp = "";
    for (char c : after_colon) {
      if (std::isalnum(c)) {
        temp += std::toupper(c);
      } else if (!temp.empty()) {
        break; // Stop at first non-alphanumeric after we have content
      }
    }

    if (temp.length() >= 4 && temp.length() <= 6) {
      extracted = temp;
      LOG_DEBUG("TextCaptchaSolver", "Found via colon pattern: '" + extracted + "'");
    }
  }

  // Strategy 2: Look for space-separated characters (e.g., "A B 3 K 9")
  if (extracted.empty()) {
    std::vector<char> chars;
    bool prev_was_space = false;

    for (char c : vision_response) {
      if (std::isspace(c)) {
        prev_was_space = true;
      } else if (std::isalnum(c)) {
        if (chars.empty() || prev_was_space) {
          chars.push_back(std::toupper(c));
        }
        prev_was_space = false;
      }
    }

    if (chars.size() >= 4 && chars.size() <= 6) {
      extracted = std::string(chars.begin(), chars.end());
      LOG_DEBUG("TextCaptchaSolver", "Found via space-separated pattern: '" + extracted + "'");
    }
  }

  // Strategy 3: Filter out common words and extract alphanumeric
  if (extracted.empty()) {
    // Convert to uppercase and extract all alphanumeric
    std::string cleaned = "";
    for (char c : vision_response) {
      if (std::isalnum(c)) {
        cleaned += std::toupper(c);
      }
    }

    // Remove common English words and CAPTCHA-related terms
    std::vector<std::string> words_to_remove = {
      "CAPTCHA", "IMAGE", "SHOWS", "FOLLOWING", "CHARACTERS", "PAGE",
      "TEXT", "THE", "ARE", "EXACT", "SHOWN", "YOU", "SEE", "ANALYZING",
      "CONTAINS", "MIX", "UPPERCASE", "LETTERS", "NUMBERS", "DISTORTED",
      "ALPHANUMERIC", "IDENTIFY", "TASK", "RESPOND", "WITH", "ONLY"
    };

    std::string filtered = cleaned;
    for (const auto& word : words_to_remove) {
      size_t pos = filtered.find(word);
      while (pos != std::string::npos) {
        filtered.erase(pos, word.length());
        pos = filtered.find(word);
      }
    }

    if (filtered.length() >= 4 && filtered.length() <= 6) {
      extracted = filtered;
      LOG_DEBUG("TextCaptchaSolver", "Found via filtering: '" + extracted + "'");
    } else if (filtered.length() > 6) {
      // Take first 5 chars (most likely to be the CAPTCHA)
      extracted = filtered.substr(0, 5);
      LOG_DEBUG("TextCaptchaSolver", "Found via taking first 5 chars: '" + extracted + "'");
    }
  }

  // Fallback: if still empty, try to extract any alphanumeric sequence
  if (extracted.empty()) {
    std::string temp = "";
    for (char c : vision_response) {
      if (std::isalnum(c)) {
        temp += std::toupper(c);
      }
    }
    if (temp.length() >= 4) {
      extracted = temp.substr(0, 5);
      LOG_WARN("TextCaptchaSolver", "Using fallback extraction: '" + extracted + "'");
    }
  }

  // Final validation
  if (extracted.length() < 4 || extracted.length() > 6) {
    LOG_WARN("TextCaptchaSolver", "Extracted text has unusual length: " +
             std::to_string(extracted.length()));
  }

  LOG_DEBUG("TextCaptchaSolver", "Final extracted text: '" + extracted + "'");

  return extracted;
}

bool OlibTextCaptchaSolver::EnterText(CefRefPtr<CefBrowser> browser,
                                      const std::string& context_id,
                                      const std::string& input_selector,
                                      const std::string& text) {
  LOG_DEBUG("TextCaptchaSolver", "Entering text: '" + text + "' into " + input_selector);

  return OwlCaptchaUtils::TypeIntoElement(browser, context_id, input_selector, text);
}

bool OlibTextCaptchaSolver::SubmitCaptcha(CefRefPtr<CefBrowser> browser,
                                          const std::string& context_id,
                                          const std::string& submit_selector) {
  LOG_DEBUG("TextCaptchaSolver", "Submitting CAPTCHA: " + submit_selector);

  return OwlCaptchaUtils::ClickElement(browser, context_id, submit_selector);
}

bool OlibTextCaptchaSolver::RefreshCaptcha(CefRefPtr<CefBrowser> browser,
                                           const std::string& context_id,
                                           const std::string& refresh_selector) {
  LOG_DEBUG("TextCaptchaSolver", "Refreshing CAPTCHA: " + refresh_selector);

  bool clicked = OwlCaptchaUtils::ClickElement(browser, context_id, refresh_selector);

  if (clicked) {
    // Wait for new CAPTCHA to load
    OwlCaptchaUtils::Wait(1500);
  }

  return clicked;
}

bool OlibTextCaptchaSolver::WaitForVerification(const std::string& context_id,
                                                CefRefPtr<CefBrowser> browser,
                                                const std::string& challenge_selector,
                                                int timeout_ms) {
  LOG_DEBUG("TextCaptchaSolver", "Waiting for form submission result (timeout: " +
            std::to_string(timeout_ms) + "ms)");

  // For text CAPTCHAs, verification happens server-side after form submission
  // We poll to check if form progresses/navigates or CAPTCHA stays visible
  // Success = input hidden (form progressed)
  // Failure = input still visible (CAPTCHA rejected)

  // Poll with proper element scanning to avoid stale cache issues
  const int poll_interval_ms = 200;
  const int max_polls = timeout_ms / poll_interval_ms;

  CefRefPtr<OwlClient> client = static_cast<OwlClient*>(browser->GetHost()->GetClient().get());

  for (int i = 0; i < max_polls; i++) {
    // Force element scan to update SemanticMatcher cache
    // This ensures we're checking fresh DOM state, not stale data
    if (client) {
      client->WaitForElementScan(browser, context_id, 100);
    }

    // Check if input is still visible
    bool input_visible = OwlCaptchaUtils::IsVisible(browser, challenge_selector);

    if (!input_visible) {
      LOG_DEBUG("TextCaptchaSolver", "CAPTCHA input hidden - form submitted successfully");
      return true;
    }

    // Also check for success indicators (like "verified" text or success message)
    OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
    if (matcher) {
      std::vector<ElementSemantics> elements = matcher->GetAllElements(context_id);
      for (const auto& elem : elements) {
        std::string text_lower = elem.text;
        std::transform(text_lower.begin(), text_lower.end(), text_lower.begin(), ::tolower);

        // Check for common success indicators
        if (text_lower.find("success") != std::string::npos ||
            text_lower.find("verified") != std::string::npos ||
            text_lower.find("correct") != std::string::npos) {
          LOG_DEBUG("TextCaptchaSolver", "Success indicator found (text: '" + elem.text + "')");
          return true;
        }
      }
    }

    OwlCaptchaUtils::Wait(poll_interval_ms);
  }

  LOG_WARN("TextCaptchaSolver", "CAPTCHA input still visible after timeout - verification likely failed");
  return false;
}

std::string OlibTextCaptchaSolver::ExecuteJavaScriptAndGetResult(
    CefRefPtr<CefBrowser> browser,
    const std::string& script) {
  return OwlCaptchaUtils::ExecuteJavaScriptAndGetResult(browser, script);
}
