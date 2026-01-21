#pragma once

#include "owl_image_captcha_provider_base.h"

/**
 * OwlImageCaptchaProvider - Provider for Owl Browser's internal test CAPTCHA
 *
 * This provider handles the internal test CAPTCHA system used for development
 * and testing of the image CAPTCHA solving functionality.
 */
class OwlImageCaptchaProvider : public ImageCaptchaProviderBase {
public:
  OwlImageCaptchaProvider();
  ~OwlImageCaptchaProvider() override = default;

  // IImageCaptchaProvider interface
  ImageCaptchaProviderType GetType() const override { return ImageCaptchaProviderType::OWL; }
  std::string GetName() const override { return "Owl"; }
  ImageCaptchaProviderConfig GetConfig() const override { return config_; }

  double DetectProvider(CefRefPtr<CefBrowser> browser,
                        const CaptchaClassificationResult& classification) override;

  ImageCaptchaSolveResult Solve(const std::string& context_id,
                                CefRefPtr<CefBrowser> browser,
                                const CaptchaClassificationResult& classification,
                                OwlLLMClient* llm_client,
                                int max_attempts = 3) override;

  bool IsAutoVerified(CefRefPtr<CefBrowser> browser,
                      const CaptchaClassificationResult& classification) override;

  bool ClickCheckbox(CefRefPtr<CefBrowser> browser,
                     const std::string& context_id,
                     const CaptchaClassificationResult& classification) override;

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

  bool CheckVerificationSuccess(const std::string& context_id,
                                CefRefPtr<CefBrowser> browser) override;

private:
  /**
   * Initialize provider-specific configuration
   */
  void InitializeConfig();

  /**
   * Get grid item positions from the page
   * Returns vector of (x, y, width, height) tuples
   */
  std::vector<std::tuple<int, int, int, int>> GetGridItemPositions(CefRefPtr<CefBrowser> browser);
};
