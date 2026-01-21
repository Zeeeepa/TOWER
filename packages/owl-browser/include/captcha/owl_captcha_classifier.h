#pragma once

#include <string>
#include <vector>
#include "include/cef_browser.h"
#include "owl_captcha_detector.h"

/**
 * CaptchaTypeClassifier - Classifies the type of CAPTCHA on a page
 *
 * Determines the specific CAPTCHA type after detection:
 * - Text-based (distorted text entry)
 * - Image selection (reCAPTCHA style - select matching images)
 * - Checkbox (simple "I'm not a robot" with possible challenge)
 * - Puzzle (sliding pieces, rotation, etc.)
 * - Audio (audio challenge option)
 * - Custom (site-specific implementations)
 *
 * Uses DOM analysis, element structure, and visual patterns to classify.
 */

enum class CaptchaType {
  UNKNOWN,
  TEXT_BASED,      // Text entry CAPTCHA (distorted text image)
  IMAGE_SELECTION, // Image grid selection (select traffic lights, etc.)
  CHECKBOX,        // Simple checkbox with potential challenge
  PUZZLE,          // Sliding puzzle or rotation
  AUDIO,           // Audio-based CAPTCHA
  CUSTOM           // Custom implementation
};

struct CaptchaClassificationResult {
  CaptchaType type;
  double confidence;                    // Classification confidence (0.0 - 1.0)
  std::string captcha_container;        // Main CAPTCHA container selector
  std::string input_element;            // Input field selector (for text CAPTCHAs)
  std::string image_element;            // Image element selector
  std::string challenge_element;        // Challenge area selector (for image CAPTCHAs)
  std::string submit_button;            // Submit/verify button selector
  std::string refresh_button;           // Refresh/reload button selector
  std::string skip_button;              // Skip button selector (if available)
  std::string checkbox_selector;        // "I'm not a robot" checkbox selector (for image CAPTCHAs)
  std::vector<std::string> grid_items;  // Grid item selectors (for image CAPTCHAs)
  int grid_size;                        // Grid size (e.g., 9 for 3x3, 16 for 4x4)
  std::string target_description;       // What to select (e.g., "traffic lights")
  bool has_audio_option;                // Has audio alternative
  bool has_refresh_option;              // Has refresh/reload option
  bool has_skip_option;                 // Has skip option
};

class OwlCaptchaClassifier {
public:
  OwlCaptchaClassifier();
  ~OwlCaptchaClassifier();

  /**
   * Classify the type of CAPTCHA on the page
   * @param browser The CEF browser instance
   * @param detection_result Result from CaptchaDetector
   * @return Classification result with type and element selectors
   */
  CaptchaClassificationResult Classify(CefRefPtr<CefBrowser> browser,
                                       const CaptchaDetectionResult& detection_result);

private:
  /**
   * Execute JavaScript and get result as string
   */
  std::string ExecuteJavaScriptAndGetResult(CefRefPtr<CefBrowser> browser,
                                            const std::string& script);

  /**
   * Classify text-based CAPTCHA and extract element info
   */
  CaptchaClassificationResult ClassifyTextCaptcha(CefRefPtr<CefBrowser> browser,
                                                  const std::string& selector);

  /**
   * Classify image-selection CAPTCHA and extract grid info
   */
  CaptchaClassificationResult ClassifyImageCaptcha(CefRefPtr<CefBrowser> browser,
                                                   const std::string& selector);

  /**
   * Classify checkbox CAPTCHA
   */
  CaptchaClassificationResult ClassifyCheckboxCaptcha(CefRefPtr<CefBrowser> browser,
                                                      const std::string& selector);

  /**
   * Extract grid information (size, items) from image CAPTCHA
   */
  void ExtractGridInfo(CefRefPtr<CefBrowser> browser,
                      CaptchaClassificationResult& result);

  /**
   * Extract target description from challenge text
   * (e.g., "Select all squares with traffic lights")
   */
  std::string ExtractTargetDescription(CefRefPtr<CefBrowser> browser,
                                       const std::string& challenge_selector);

  /**
   * Find interactive elements (buttons) for the CAPTCHA
   */
  void FindCaptchaButtons(CefRefPtr<CefBrowser> browser,
                         CaptchaClassificationResult& result,
                         const std::string& container);
};
