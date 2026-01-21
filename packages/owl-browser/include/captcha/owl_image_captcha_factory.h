#pragma once

#include "owl_image_captcha_provider.h"
#include <memory>
#include <map>
#include <vector>

/**
 * ImageCaptchaProviderFactory - Factory for creating and managing CAPTCHA providers
 *
 * Handles:
 * - Provider registration and creation
 * - Auto-detection of the correct provider for a page
 * - Provider caching for performance
 */
class ImageCaptchaProviderFactory {
public:
  /**
   * Get the singleton instance
   */
  static ImageCaptchaProviderFactory* GetInstance();

  /**
   * Create a provider by type
   * @param type The provider type to create
   * @return Shared pointer to the provider, or nullptr if type not supported
   */
  std::shared_ptr<IImageCaptchaProvider> CreateProvider(ImageCaptchaProviderType type);

  /**
   * Auto-detect and create the appropriate provider for a page
   * @param browser The CEF browser instance
   * @param classification CAPTCHA classification result
   * @return Shared pointer to the best matching provider
   */
  std::shared_ptr<IImageCaptchaProvider> DetectAndCreateProvider(
      CefRefPtr<CefBrowser> browser,
      const CaptchaClassificationResult& classification);

  /**
   * Get all available provider types
   */
  std::vector<ImageCaptchaProviderType> GetAvailableProviders() const;

  /**
   * Check if a provider type is supported
   */
  bool IsProviderSupported(ImageCaptchaProviderType type) const;

  /**
   * Get the provider name for a type
   */
  std::string GetProviderName(ImageCaptchaProviderType type) const;

private:
  ImageCaptchaProviderFactory();
  ~ImageCaptchaProviderFactory();

  // Prevent copying
  ImageCaptchaProviderFactory(const ImageCaptchaProviderFactory&) = delete;
  ImageCaptchaProviderFactory& operator=(const ImageCaptchaProviderFactory&) = delete;

  static ImageCaptchaProviderFactory* instance_;

  // Cache created providers for reuse
  std::map<ImageCaptchaProviderType, std::shared_ptr<IImageCaptchaProvider>> provider_cache_;
};
