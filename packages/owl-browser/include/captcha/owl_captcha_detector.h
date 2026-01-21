#pragma once

#include <string>
#include <vector>
#include "include/cef_browser.h"

/**
 * CaptchaDetector - Detects if a page contains CAPTCHA elements
 *
 * Uses heuristic-based detection without requiring the vision model.
 * Analyzes DOM structure, common CAPTCHA patterns, keywords, and element attributes.
 *
 * Detection strategies:
 * 1. Keyword matching in IDs, classes, text content (captcha, recaptcha, verify, etc.)
 * 2. Image elements with suspicious patterns (distorted text, grids)
 * 3. Canvas elements (often used for CAPTCHA rendering)
 * 4. IFrames from known CAPTCHA providers (Google reCAPTCHA, hCaptcha, etc.)
 * 5. Form elements with verification inputs
 * 6. Specific ARIA labels and roles
 */

struct CaptchaDetectionResult {
  bool has_captcha;                    // True if CAPTCHA detected
  double confidence;                    // Confidence score (0.0 - 1.0)
  std::vector<std::string> indicators; // List of detection indicators found
  std::vector<std::string> selectors;  // CSS selectors of potential CAPTCHA elements
  std::string detection_method;        // How it was detected
};

class OwlCaptchaDetector {
public:
  OwlCaptchaDetector();
  ~OwlCaptchaDetector();

  /**
   * Detect if the current page contains a CAPTCHA
   * @param browser The CEF browser instance
   * @return Detection result with confidence and selectors
   */
  CaptchaDetectionResult Detect(CefRefPtr<CefBrowser> browser);

  /**
   * Check if element is visible on the page
   * @param browser The CEF browser instance
   * @param selector CSS selector to check
   * @return True if element is visible
   */
  bool IsElementVisible(CefRefPtr<CefBrowser> browser, const std::string& selector);

private:
  /**
   * Execute JavaScript and get result as string
   */
  std::string ExecuteJavaScriptAndGetResult(CefRefPtr<CefBrowser> browser,
                                            const std::string& script);

  /**
   * Check for text-based CAPTCHA indicators
   */
  bool DetectTextCaptchaPatterns(CefRefPtr<CefBrowser> browser,
                                 CaptchaDetectionResult& result);

  /**
   * Check for image-based CAPTCHA indicators (reCAPTCHA style)
   */
  bool DetectImageCaptchaPatterns(CefRefPtr<CefBrowser> browser,
                                  CaptchaDetectionResult& result);

  /**
   * Check for known CAPTCHA provider IFrames
   */
  bool DetectCaptchaIFrames(CefRefPtr<CefBrowser> browser,
                            CaptchaDetectionResult& result);

  /**
   * Check for canvas-based CAPTCHAs
   */
  bool DetectCanvasCaptcha(CefRefPtr<CefBrowser> browser,
                           CaptchaDetectionResult& result);

  /**
   * Keyword patterns for CAPTCHA detection
   */
  std::vector<std::string> captcha_keywords_;

  /**
   * Known CAPTCHA provider domains
   */
  std::vector<std::string> captcha_providers_;
};
