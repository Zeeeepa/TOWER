#pragma once

#include <string>
#include <vector>
#include <set>
#include "include/cef_browser.h"
#include "owl_captcha_classifier.h"

// Forward declaration
class OwlLLMClient;

/**
 * ImageCaptchaSolver - Solves image-selection CAPTCHAs using vision model + DOM highlighting
 *
 * Uses the Qwen3-VL-2B vision model to:
 * 1. Analyze the full CAPTCHA grid screenshot
 * 2. Identify which images match the target (e.g., "traffic lights")
 * 3. Highlight each grid item using DOM highlighter
 * 4. Use vision model in one-shot mode to determine correct images
 * 5. Click the correct grid items
 * 6. Submit verification
 *
 * Handles:
 * - 3x3 and 4x4 image grids
 * - Various challenge types (traffic lights, crosswalks, bicycles, etc.)
 * - Dynamic image loading
 * - Multiple verification rounds
 * - Skip option if unsure
 */

/**
 * ImageCaptchaProviderType - Enumeration of supported CAPTCHA providers
 * Defined here to be available to ImageCaptchaSolveResult
 */
enum class ImageCaptchaProviderType {
  AUTO,       // Auto-detect provider based on page analysis
  OWL,        // Owl Browser's internal test CAPTCHA
  RECAPTCHA,  // Google reCAPTCHA v2 image challenges
  CLOUDFLARE, // Cloudflare Turnstile/hCaptcha
  HCAPTCHA,   // hCaptcha standalone
  UNKNOWN     // Unknown provider
};

struct ImageCaptchaSolveResult {
  bool success;                          // True if CAPTCHA was solved
  std::vector<int> selected_indices;     // Indices of selected grid items
  double confidence;                      // Overall confidence (0.0 - 1.0)
  int attempts;                          // Number of attempts made
  std::string error_message;             // Error message if failed
  bool needs_skip;                       // True if should skip this challenge
  std::string target_detected;           // What the model detected as target
  ImageCaptchaProviderType provider;     // Provider that was used (for modular CAPTCHA solving)
};

class OlibImageCaptchaSolver {
public:
  /**
   * Constructor
   * @param llm_client Pointer to LLM client for vision model access
   */
  explicit OlibImageCaptchaSolver(OwlLLMClient* llm_client);
  ~OlibImageCaptchaSolver();

  /**
   * Solve an image-selection CAPTCHA
   * @param context_id The browser context ID
   * @param browser The CEF browser instance
   * @param classification CAPTCHA classification result
   * @param max_attempts Maximum number of attempts
   * @return Solve result
   */
  ImageCaptchaSolveResult Solve(const std::string& context_id,
                                CefRefPtr<CefBrowser> browser,
                                const CaptchaClassificationResult& classification,
                                int max_attempts = 3);

  /**
   * Set whether to auto-submit after solving
   * @param auto_submit True to auto-submit
   */
  void SetAutoSubmit(bool auto_submit) { auto_submit_ = auto_submit; }

  /**
   * Set whether to use skip option if unsure
   * @param allow_skip True to allow skipping
   */
  void SetAllowSkip(bool allow_skip) { allow_skip_ = allow_skip; }

private:
  /**
   * Scroll the CAPTCHA challenge into view
   */
  bool ScrollChallengeIntoView(CefRefPtr<CefBrowser> browser,
                               const std::string& challenge_selector);

  /**
   * Capture screenshot of the entire CAPTCHA grid
   */
  std::vector<uint8_t> CaptureGridScreenshot(CefRefPtr<CefBrowser> browser,
                                             const std::string& context_id,
                                             const std::string& grid_selector);

  /**
   * Use vision model to identify which grid items match the target
   * Uses one-shot analysis with numbered grid items
   */
  std::vector<int> IdentifyMatchingImages(const std::vector<uint8_t>& grid_screenshot,
                                          const std::string& target_description,
                                          int grid_size);

  /**
   * Highlight a specific grid item for visual feedback
   */
  bool HighlightGridItem(CefRefPtr<CefBrowser> browser,
                        int grid_index,
                        const std::vector<std::string>& grid_selectors);

  /**
   * Click a grid item to select it
   */
  bool ClickGridItem(CefRefPtr<CefBrowser> browser,
                    const std::string& context_id,
                    int grid_index,
                    const std::vector<std::string>& grid_selectors);

  /**
   * Submit the CAPTCHA verification
   */
  bool SubmitVerification(CefRefPtr<CefBrowser> browser,
                         const std::string& context_id,
                         const std::string& submit_selector);

  /**
   * Skip the current challenge and get a new one
   */
  bool SkipChallenge(CefRefPtr<CefBrowser> browser,
                    const std::string& context_id,
                    const std::string& skip_selector);

  /**
   * Wait for new images to load after submission
   */
  bool WaitForImageLoad(CefRefPtr<CefBrowser> browser,
                       const std::vector<std::string>& grid_selectors,
                       int timeout_ms = 5000);

  /**
   * Check if verification was successful
   */
  bool CheckVerificationSuccess(const std::string& context_id,
                                CefRefPtr<CefBrowser> browser,
                                const std::string& challenge_selector);

  /**
   * Execute JavaScript and get result as string
   */
  std::string ExecuteJavaScriptAndGetResult(CefRefPtr<CefBrowser> browser,
                                            const std::string& script);

  /**
   * Isolate and capture only the CAPTCHA area (not full page)
   */
  std::vector<uint8_t> IsolatedScreenshot(CefRefPtr<CefBrowser> browser,
                                          const std::string& context_id,
                                          const std::string& element_selector);

  /**
   * Add numbered overlays to grid items for vision model
   * This helps the vision model identify specific grid positions
   */
  bool AddNumberedOverlays(CefRefPtr<CefBrowser> browser,
                          const std::vector<std::string>& grid_selectors);

  /**
   * Remove numbered overlays after analysis
   */
  bool RemoveNumberedOverlays(CefRefPtr<CefBrowser> browser);

  /**
   * Extract target description from the challenge element on the page
   * Parses text like "Select all squares with bicycles" -> "bicycles"
   */
  std::string ExtractTargetFromChallenge(const std::string& context_id,
                                         CefRefPtr<CefBrowser> browser,
                                         const std::string& challenge_selector);

  OwlLLMClient* llm_client_;
  bool auto_submit_;
  bool allow_skip_;
};
