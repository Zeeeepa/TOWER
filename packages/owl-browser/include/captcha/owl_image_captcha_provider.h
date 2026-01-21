#pragma once

#include <string>
#include <vector>
#include <memory>
#include <set>
#include "include/cef_browser.h"
#include "owl_captcha_classifier.h"
#include "owl_image_captcha_solver.h"  // For ImageCaptchaSolveResult and ImageCaptchaProviderType

// Forward declaration
class OwlLLMClient;

/**
 * ImageCaptchaProviderConfig - Configuration for a specific provider
 */
struct ImageCaptchaProviderConfig {
  // Grid configuration
  std::string grid_container_selector;
  std::string grid_item_selector;
  std::string grid_item_class;
  int default_grid_size;

  // Challenge elements
  std::string challenge_container_selector;
  std::string challenge_title_selector;
  std::string target_text_selector;

  // Interaction elements
  std::string checkbox_selector;
  std::string submit_button_selector;
  std::string skip_button_selector;
  std::string refresh_button_selector;
  std::string audio_button_selector;

  // Iframe handling (for reCAPTCHA/hCaptcha)
  bool uses_iframe;
  std::string iframe_selector;
  std::string challenge_iframe_selector;

  // Timing configuration
  int click_delay_min_ms;
  int click_delay_max_ms;
  int post_checkbox_wait_ms;
  int post_submit_wait_ms;
  int grid_load_timeout_ms;

  // Vision prompt customization
  std::string vision_prompt_template;

  ImageCaptchaProviderConfig()
    : default_grid_size(9)
    , uses_iframe(false)
    , click_delay_min_ms(200)
    , click_delay_max_ms(450)
    , post_checkbox_wait_ms(1000)
    , post_submit_wait_ms(2000)
    , grid_load_timeout_ms(6000)
  {}
};

/**
 * IImageCaptchaProvider - Abstract base class for image CAPTCHA providers
 *
 * This interface allows implementing different CAPTCHA providers (reCAPTCHA,
 * Cloudflare, hCaptcha, etc.) with provider-specific logic while sharing
 * common functionality through the base class.
 */
class IImageCaptchaProvider {
public:
  virtual ~IImageCaptchaProvider() = default;

  /**
   * Get the provider type
   */
  virtual ImageCaptchaProviderType GetType() const = 0;

  /**
   * Get the provider name for logging
   */
  virtual std::string GetName() const = 0;

  /**
   * Get provider-specific configuration
   */
  virtual ImageCaptchaProviderConfig GetConfig() const = 0;

  /**
   * Detect if this provider is present on the page
   * @param browser The CEF browser instance
   * @param classification CAPTCHA classification result
   * @return Confidence score (0.0 - 1.0) that this provider is present
   */
  virtual double DetectProvider(CefRefPtr<CefBrowser> browser,
                                const CaptchaClassificationResult& classification) = 0;

  /**
   * Solve the image CAPTCHA
   * @param context_id The browser context ID
   * @param browser The CEF browser instance
   * @param classification CAPTCHA classification result
   * @param llm_client LLM client for vision analysis
   * @param max_attempts Maximum number of attempts
   * @return Solve result
   */
  virtual ImageCaptchaSolveResult Solve(const std::string& context_id,
                                        CefRefPtr<CefBrowser> browser,
                                        const CaptchaClassificationResult& classification,
                                        OwlLLMClient* llm_client,
                                        int max_attempts = 3) = 0;

  /**
   * Check if CAPTCHA was already auto-verified (e.g., reCAPTCHA checkbox only)
   * @param browser The CEF browser instance
   * @param classification CAPTCHA classification result
   * @return True if already verified
   */
  virtual bool IsAutoVerified(CefRefPtr<CefBrowser> browser,
                              const CaptchaClassificationResult& classification) = 0;

  /**
   * Click the checkbox to trigger the challenge (if applicable)
   * @param browser The CEF browser instance
   * @param context_id The browser context ID
   * @param classification CAPTCHA classification result
   * @return True if checkbox was clicked successfully
   */
  virtual bool ClickCheckbox(CefRefPtr<CefBrowser> browser,
                             const std::string& context_id,
                             const CaptchaClassificationResult& classification) = 0;

  /**
   * Extract target description from the challenge
   * @param context_id The browser context ID
   * @param browser The CEF browser instance
   * @param classification CAPTCHA classification result
   * @return Target description (e.g., "traffic lights", "bicycles")
   */
  virtual std::string ExtractTarget(const std::string& context_id,
                                    CefRefPtr<CefBrowser> browser,
                                    const CaptchaClassificationResult& classification) = 0;

  /**
   * Capture screenshot of the grid with numbered overlays
   * @param browser The CEF browser instance
   * @param context_id The browser context ID
   * @return PNG image data
   */
  virtual std::vector<uint8_t> CaptureGridScreenshot(CefRefPtr<CefBrowser> browser,
                                                     const std::string& context_id) = 0;

  /**
   * Identify matching images using vision model
   * @param grid_screenshot PNG image data of the grid
   * @param target_description What to look for
   * @param grid_size Number of grid items
   * @param llm_client LLM client for vision analysis
   * @return Indices of matching grid items
   */
  virtual std::vector<int> IdentifyMatchingImages(const std::vector<uint8_t>& grid_screenshot,
                                                  const std::string& target_description,
                                                  int grid_size,
                                                  OwlLLMClient* llm_client) = 0;

  /**
   * Click a grid item
   * @param browser The CEF browser instance
   * @param context_id The browser context ID
   * @param grid_index Index of the grid item to click
   * @return True if click succeeded
   */
  virtual bool ClickGridItem(CefRefPtr<CefBrowser> browser,
                             const std::string& context_id,
                             int grid_index) = 0;

  /**
   * Submit the verification
   * @param browser The CEF browser instance
   * @param context_id The browser context ID
   * @return True if submission succeeded
   */
  virtual bool SubmitVerification(CefRefPtr<CefBrowser> browser,
                                  const std::string& context_id) = 0;

  /**
   * Skip the current challenge
   * @param browser The CEF browser instance
   * @param context_id The browser context ID
   * @return True if skip succeeded
   */
  virtual bool SkipChallenge(CefRefPtr<CefBrowser> browser,
                             const std::string& context_id) = 0;

  /**
   * Check if verification was successful
   * @param context_id The browser context ID
   * @param browser The CEF browser instance
   * @return True if verification succeeded
   */
  virtual bool CheckVerificationSuccess(const std::string& context_id,
                                        CefRefPtr<CefBrowser> browser) = 0;

  /**
   * Set whether to auto-submit after solving
   */
  virtual void SetAutoSubmit(bool auto_submit) = 0;

  /**
   * Set whether to allow skipping challenges
   */
  virtual void SetAllowSkip(bool allow_skip) = 0;
};

/**
 * Convert provider type to string
 */
inline std::string ImageCaptchaProviderTypeToString(ImageCaptchaProviderType type) {
  switch (type) {
    case ImageCaptchaProviderType::AUTO: return "auto";
    case ImageCaptchaProviderType::OWL: return "owl";
    case ImageCaptchaProviderType::RECAPTCHA: return "recaptcha";
    case ImageCaptchaProviderType::CLOUDFLARE: return "cloudflare";
    case ImageCaptchaProviderType::HCAPTCHA: return "hcaptcha";
    case ImageCaptchaProviderType::UNKNOWN: return "unknown";
    default: return "unknown";
  }
}

/**
 * Convert string to provider type
 */
inline ImageCaptchaProviderType StringToImageCaptchaProviderType(const std::string& str) {
  if (str == "auto" || str.empty()) return ImageCaptchaProviderType::AUTO;
  if (str == "owl") return ImageCaptchaProviderType::OWL;
  if (str == "recaptcha") return ImageCaptchaProviderType::RECAPTCHA;
  if (str == "cloudflare") return ImageCaptchaProviderType::CLOUDFLARE;
  if (str == "hcaptcha") return ImageCaptchaProviderType::HCAPTCHA;
  return ImageCaptchaProviderType::UNKNOWN;
}
