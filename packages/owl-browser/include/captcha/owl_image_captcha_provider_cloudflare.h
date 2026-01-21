#pragma once

#include "owl_image_captcha_provider_base.h"

/**
 * CloudflareImageCaptchaProvider - Provider for Cloudflare Turnstile and hCaptcha
 *
 * Handles Cloudflare's CAPTCHA challenges, which include:
 * - Turnstile (invisible/checkbox style)
 * - hCaptcha integration (when Cloudflare uses hCaptcha for image challenges)
 * - Challenge page ("Just a moment..." interstitial)
 */
class CloudflareImageCaptchaProvider : public ImageCaptchaProviderBase {
public:
  CloudflareImageCaptchaProvider();
  ~CloudflareImageCaptchaProvider() override = default;

  // IImageCaptchaProvider interface
  ImageCaptchaProviderType GetType() const override { return ImageCaptchaProviderType::CLOUDFLARE; }
  std::string GetName() const override { return "Cloudflare"; }
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
   * Detect if this is a Turnstile (non-image) challenge
   */
  bool IsTurnstileOnly(CefRefPtr<CefBrowser> browser);

  /**
   * Detect if this is an hCaptcha image challenge
   */
  bool IsHCaptchaChallenge(CefRefPtr<CefBrowser> browser);

  /**
   * Get the hCaptcha challenge frame
   */
  CefRefPtr<CefFrame> GetHCaptchaFrame(CefRefPtr<CefBrowser> browser);

  /**
   * Handle the Cloudflare interstitial page
   */
  bool HandleInterstitialPage(CefRefPtr<CefBrowser> browser);
};
