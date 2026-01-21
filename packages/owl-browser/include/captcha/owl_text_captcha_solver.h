#pragma once

#include <string>
#include <vector>
#include "include/cef_browser.h"
#include "owl_captcha_classifier.h"

// Forward declaration
class OwlLLMClient;

/**
 * TextCaptchaSolver - Solves text-based CAPTCHAs using vision model
 *
 * Uses the Qwen3-VL-2B vision model to:
 * 1. Capture the CAPTCHA image
 * 2. Send to vision model for OCR
 * 3. Extract the text from distorted image
 * 4. Enter the text into the input field
 * 5. Submit the form
 *
 * Handles:
 * - Distorted text images
 * - Various fonts and styles
 * - Wave distortions
 * - Rotation and noise
 * - Multiple attempt retries
 */

struct TextCaptchaSolveResult {
  bool success;                      // True if CAPTCHA was solved
  std::string extracted_text;        // Text extracted by vision model
  double confidence;                 // Vision model confidence (0.0 - 1.0)
  int attempts;                      // Number of attempts made
  std::string error_message;         // Error message if failed
  bool needs_refresh;                // True if should refresh and try again
};

class OlibTextCaptchaSolver {
public:
  /**
   * Constructor
   * @param llm_client Pointer to LLM client for vision model access
   */
  explicit OlibTextCaptchaSolver(OwlLLMClient* llm_client);
  ~OlibTextCaptchaSolver();

  /**
   * Solve a text-based CAPTCHA
   * @param context_id The browser context ID
   * @param browser The CEF browser instance
   * @param classification CAPTCHA classification result
   * @param max_attempts Maximum number of attempts
   * @return Solve result
   */
  TextCaptchaSolveResult Solve(const std::string& context_id,
                               CefRefPtr<CefBrowser> browser,
                               const CaptchaClassificationResult& classification,
                               int max_attempts = 3);

  /**
   * Set whether to auto-submit after solving
   * @param auto_submit True to auto-submit
   */
  void SetAutoSubmit(bool auto_submit) { auto_submit_ = auto_submit; }

private:
  /**
   * Capture screenshot of CAPTCHA image element
   */
  std::vector<uint8_t> CaptureImageElement(CefRefPtr<CefBrowser> browser,
                                          const std::string& context_id,
                                          const std::string& image_selector);

  /**
   * Use vision model to extract text from CAPTCHA image
   */
  std::string ExtractTextWithVision(const std::vector<uint8_t>& image_data);

  /**
   * Enter extracted text into input field
   */
  bool EnterText(CefRefPtr<CefBrowser> browser,
                const std::string& context_id,
                const std::string& input_selector,
                const std::string& text);

  /**
   * Click the submit button
   */
  bool SubmitCaptcha(CefRefPtr<CefBrowser> browser,
                    const std::string& context_id,
                    const std::string& submit_selector);

  /**
   * Refresh the CAPTCHA to get a new challenge
   */
  bool RefreshCaptcha(CefRefPtr<CefBrowser> browser,
                     const std::string& context_id,
                     const std::string& refresh_selector);

  /**
   * Wait for CAPTCHA verification result
   * @param context_id The browser context ID
   * @param browser The CEF browser instance
   * @param challenge_selector Selector for the challenge element to check visibility
   * @param timeout_ms Maximum time to wait in milliseconds
   * @return True if verification successful
   */
  bool WaitForVerification(const std::string& context_id,
                          CefRefPtr<CefBrowser> browser,
                          const std::string& challenge_selector,
                          int timeout_ms = 5000);

  /**
   * Execute JavaScript and get result as string
   */
  std::string ExecuteJavaScriptAndGetResult(CefRefPtr<CefBrowser> browser,
                                            const std::string& script);

  OwlLLMClient* llm_client_;
  bool auto_submit_;
};
