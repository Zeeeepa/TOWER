#pragma once

#include "owl_image_captcha_provider_base.h"
#include <string>
#include <map>

/**
 * Gesture types for reCAPTCHA liveness challenges
 */
enum class RecaptchaGesture {
  UNKNOWN,
  RAISED_HAND,      // Open hand raised
  CLOSED_FIST,      // Closed fist
  THUMBS_UP,        // Thumbs up
  THUMBS_DOWN,      // Thumbs down
  OPEN_PALM,        // Open palm facing camera
  PEACE_SIGN,       // Two fingers up (V sign)
  POINTING_UP,      // One finger pointing up
  THREE_FINGERS,    // Three fingers up
  FOUR_FINGERS,     // Four fingers up
};

/**
 * RecaptchaImageCaptchaProvider - Provider for Google reCAPTCHA v2
 *
 * Handles both:
 * - Traditional image grid challenges
 * - Liveness (hand gesture) challenges via virtual camera
 */
class RecaptchaImageCaptchaProvider : public ImageCaptchaProviderBase {
public:
  RecaptchaImageCaptchaProvider();
  ~RecaptchaImageCaptchaProvider() override = default;

  // IImageCaptchaProvider interface
  ImageCaptchaProviderType GetType() const override { return ImageCaptchaProviderType::RECAPTCHA; }
  std::string GetName() const override { return "reCAPTCHA"; }
  ImageCaptchaProviderConfig GetConfig() const override { return config_; }

  // Detection
  double DetectProvider(CefRefPtr<CefBrowser> browser,
                        const CaptchaClassificationResult& classification) override;

  // Main solve entry point
  ImageCaptchaSolveResult Solve(const std::string& context_id,
                                CefRefPtr<CefBrowser> browser,
                                const CaptchaClassificationResult& classification,
                                OwlLLMClient* llm_client,
                                int max_attempts = 3) override;

  // Checkbox handling
  bool ClickCheckbox(CefRefPtr<CefBrowser> browser,
                     const std::string& context_id,
                     const CaptchaClassificationResult& classification) override;

  bool IsAutoVerified(CefRefPtr<CefBrowser> browser,
                      const CaptchaClassificationResult& classification) override;

  bool CheckVerificationSuccess(const std::string& context_id,
                                CefRefPtr<CefBrowser> browser) override;

  // Grid challenge methods (minimal implementation for now)
  std::string ExtractTarget(const std::string& context_id,
                            CefRefPtr<CefBrowser> browser,
                            const CaptchaClassificationResult& classification) override;

  std::vector<uint8_t> CaptureGridScreenshot(CefRefPtr<CefBrowser> browser,
                                             const std::string& context_id) override;

  bool ClickGridItem(CefRefPtr<CefBrowser> browser,
                     const std::string& context_id,
                     int grid_index) override;

  bool SubmitVerification(CefRefPtr<CefBrowser> browser,
                          const std::string& context_id) override;

  bool SkipChallenge(CefRefPtr<CefBrowser> browser,
                     const std::string& context_id) override;

  std::vector<int> IdentifyMatchingImages(const std::vector<uint8_t>& grid_screenshot,
                                          const std::string& target_description,
                                          int grid_size,
                                          OwlLLMClient* llm_client) override;

private:
  void InitializeConfig();
  void InitializeGesturePaths();

  // ============================================================================
  // LIVENESS CHALLENGE (Hand Gesture)
  // ============================================================================

  /**
   * Solve liveness challenge using virtual camera
   */
  ImageCaptchaSolveResult SolveLivenessChallenge(
      const std::string& context_id,
      CefRefPtr<CefBrowser> browser,
      OwlLLMClient* llm_client);

  /**
   * Click the liveness button to switch from grid to camera challenge
   */
  bool ClickLivenessButton(CefRefPtr<CefBrowser> browser);

  /**
   * Click "Next" button on the liveness intro screen
   */
  bool ClickLivenessNextButton(CefRefPtr<CefBrowser> browser);

  /**
   * Scan all frames and find IMG with gesture alt text
   */
  RecaptchaGesture GetRequestedGestureFromDOM(CefRefPtr<CefBrowser> browser);

  /**
   * Parse gesture from DOM alt text (e.g., "Open palm", "Victory", "Closed fist")
   */
  RecaptchaGesture ParseGestureFromAltText(const std::string& alt);

  /**
   * Set up virtual camera with background and gesture overlay
   */
  bool SetupVirtualCameraForGesture(RecaptchaGesture gesture);

  /**
   * Get the image path for a specific gesture
   */
  std::string GetGestureImagePath(RecaptchaGesture gesture);

  /**
   * Wait for gesture to be recognized by reCAPTCHA
   */
  bool WaitForGestureRecognition(CefRefPtr<CefBrowser> browser, int timeout_ms = 10000);

  /**
   * Check if liveness challenge is complete
   */
  bool IsLivenessComplete(CefRefPtr<CefBrowser> browser);

  // ============================================================================
  // HELPER METHODS
  // ============================================================================

  /**
   * Get the reCAPTCHA challenge iframe (bframe)
   */
  CefRefPtr<CefFrame> GetChallengeFrame(CefRefPtr<CefBrowser> browser);

  /**
   * Rescan challenge frame DOM via IPC
   */
  void RescanChallengeFrame(CefRefPtr<CefBrowser> browser, const std::string& context_id);

  /**
   * Rescan main frame DOM via IPC
   */
  void RescanMainFrame(CefRefPtr<CefBrowser> browser, const std::string& context_id);

  /**
   * Get bframe position using render tracker
   */
  bool GetBframePosition(CefRefPtr<CefBrowser> browser,
                         const std::string& context_id,
                         int& bframe_x, int& bframe_y,
                         int& bframe_w, int& bframe_h);

  /**
   * Check if checkbox is in verified state
   */
  bool IsCheckboxVerified(CefRefPtr<CefBrowser> browser);

  /**
   * Check if liveness button is available (uses IPC scanning)
   */
  bool HasLivenessButton(CefRefPtr<CefBrowser> browser);

  /**
   * Check if we're on the liveness screen (liveness container is visible)
   */
  bool IsOnLivenessScreen(CefRefPtr<CefBrowser> browser);

  /**
   * Click element inside bframe using native mouse events
   */
  bool ClickElementInBframe(CefRefPtr<CefBrowser> browser,
                            const std::string& context_id,
                            const std::string& element_selector_or_id);

  // State
  std::string gestures_directory_;
  std::map<RecaptchaGesture, std::string> gesture_paths_;
  std::string current_context_id_;  // Store context_id for helper methods

  // Per-instance cursor state (for thread-safe concurrent usage)
  int last_cursor_x_{0};
  int last_cursor_y_{0};
};
