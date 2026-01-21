#include "owl_image_captcha_factory.h"
#include "owl_image_captcha_provider_owl.h"
#include "owl_image_captcha_provider_recaptcha.h"
#include "owl_image_captcha_provider_cloudflare.h"
#include "logger.h"

ImageCaptchaProviderFactory* ImageCaptchaProviderFactory::instance_ = nullptr;

ImageCaptchaProviderFactory* ImageCaptchaProviderFactory::GetInstance() {
  if (!instance_) {
    instance_ = new ImageCaptchaProviderFactory();
  }
  return instance_;
}

ImageCaptchaProviderFactory::ImageCaptchaProviderFactory() {
  LOG_DEBUG("ImageCaptchaProviderFactory", "Initialized");
}

ImageCaptchaProviderFactory::~ImageCaptchaProviderFactory() {
  provider_cache_.clear();
}

std::shared_ptr<IImageCaptchaProvider> ImageCaptchaProviderFactory::CreateProvider(
    ImageCaptchaProviderType type) {

  // Check cache first
  auto it = provider_cache_.find(type);
  if (it != provider_cache_.end()) {
    LOG_DEBUG("ImageCaptchaProviderFactory", "Returning cached provider: " +
              ImageCaptchaProviderTypeToString(type));
    return it->second;
  }

  // Create new provider
  std::shared_ptr<IImageCaptchaProvider> provider;

  switch (type) {
    case ImageCaptchaProviderType::OWL:
      LOG_DEBUG("ImageCaptchaProviderFactory", "Creating Owl provider");
      provider = std::make_shared<OwlImageCaptchaProvider>();
      break;

    case ImageCaptchaProviderType::RECAPTCHA:
      LOG_DEBUG("ImageCaptchaProviderFactory", "Creating reCAPTCHA provider");
      provider = std::make_shared<RecaptchaImageCaptchaProvider>();
      break;

    case ImageCaptchaProviderType::CLOUDFLARE:
      LOG_DEBUG("ImageCaptchaProviderFactory", "Creating Cloudflare provider");
      provider = std::make_shared<CloudflareImageCaptchaProvider>();
      break;

    case ImageCaptchaProviderType::HCAPTCHA:
      // hCaptcha is handled by Cloudflare provider (they use hCaptcha)
      LOG_DEBUG("ImageCaptchaProviderFactory", "Creating Cloudflare/hCaptcha provider");
      provider = std::make_shared<CloudflareImageCaptchaProvider>();
      break;

    case ImageCaptchaProviderType::AUTO:
    case ImageCaptchaProviderType::UNKNOWN:
      LOG_WARN("ImageCaptchaProviderFactory", "Cannot create AUTO/UNKNOWN provider directly");
      return nullptr;

    default:
      LOG_ERROR("ImageCaptchaProviderFactory", "Unknown provider type: " +
                std::to_string(static_cast<int>(type)));
      return nullptr;
  }

  // Cache the provider
  if (provider) {
    provider_cache_[type] = provider;
  }

  return provider;
}

std::shared_ptr<IImageCaptchaProvider> ImageCaptchaProviderFactory::DetectAndCreateProvider(
    CefRefPtr<CefBrowser> browser,
    const CaptchaClassificationResult& classification) {

  LOG_DEBUG("ImageCaptchaProviderFactory", "Auto-detecting CAPTCHA provider...");

  // Get detection scores from each provider
  std::vector<std::pair<ImageCaptchaProviderType, double>> scores;

  // Create temporary providers for detection
  auto owl_provider = CreateProvider(ImageCaptchaProviderType::OWL);
  auto recaptcha_provider = CreateProvider(ImageCaptchaProviderType::RECAPTCHA);
  auto cloudflare_provider = CreateProvider(ImageCaptchaProviderType::CLOUDFLARE);

  if (owl_provider) {
    double score = owl_provider->DetectProvider(browser, classification);
    scores.push_back({ImageCaptchaProviderType::OWL, score});
    LOG_DEBUG("ImageCaptchaProviderFactory", "Owl detection score: " + std::to_string(score));
  }

  if (recaptcha_provider) {
    double score = recaptcha_provider->DetectProvider(browser, classification);
    scores.push_back({ImageCaptchaProviderType::RECAPTCHA, score});
    LOG_DEBUG("ImageCaptchaProviderFactory", "reCAPTCHA detection score: " + std::to_string(score));
  }

  if (cloudflare_provider) {
    double score = cloudflare_provider->DetectProvider(browser, classification);
    scores.push_back({ImageCaptchaProviderType::CLOUDFLARE, score});
    LOG_DEBUG("ImageCaptchaProviderFactory", "Cloudflare detection score: " + std::to_string(score));
  }

  // Find the highest scoring provider
  // Priority order for tie-breaking: OWL > RECAPTCHA > CLOUDFLARE
  // OWL is our generic provider, so if it matches, prefer it
  ImageCaptchaProviderType best_type = ImageCaptchaProviderType::UNKNOWN;
  double best_score = 0.0;

  for (const auto& [type, score] : scores) {
    if (score > best_score) {
      best_score = score;
      best_type = type;
    } else if (score == best_score && score > 0.0) {
      // On tie, prefer OWL provider (our generic/internal provider)
      if (type == ImageCaptchaProviderType::OWL) {
        best_type = type;
      }
    }
  }

  // Require minimum confidence
  const double MIN_CONFIDENCE = 0.3;

  if (best_score < MIN_CONFIDENCE) {
    LOG_WARN("ImageCaptchaProviderFactory", "No provider detected with sufficient confidence (best: " +
             std::to_string(best_score) + ")");

    // Fallback to Owl provider (works for generic image CAPTCHA)
    LOG_DEBUG("ImageCaptchaProviderFactory", "Falling back to Owl provider");
    return CreateProvider(ImageCaptchaProviderType::OWL);
  }

  LOG_DEBUG("ImageCaptchaProviderFactory", "Detected provider: " +
           ImageCaptchaProviderTypeToString(best_type) +
           " (confidence: " + std::to_string(best_score) + ")");

  return CreateProvider(best_type);
}

std::vector<ImageCaptchaProviderType> ImageCaptchaProviderFactory::GetAvailableProviders() const {
  return {
    ImageCaptchaProviderType::OWL,
    ImageCaptchaProviderType::RECAPTCHA,
    ImageCaptchaProviderType::CLOUDFLARE
  };
}

bool ImageCaptchaProviderFactory::IsProviderSupported(ImageCaptchaProviderType type) const {
  switch (type) {
    case ImageCaptchaProviderType::OWL:
    case ImageCaptchaProviderType::RECAPTCHA:
    case ImageCaptchaProviderType::CLOUDFLARE:
    case ImageCaptchaProviderType::HCAPTCHA:
      return true;

    case ImageCaptchaProviderType::AUTO:
      return true;  // AUTO triggers detection

    default:
      return false;
  }
}

std::string ImageCaptchaProviderFactory::GetProviderName(ImageCaptchaProviderType type) const {
  return ImageCaptchaProviderTypeToString(type);
}
