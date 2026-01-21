#pragma once

#include "owl_image_captcha_provider.h"

/**
 * ImageCaptchaProviderBase - Base implementation with common functionality
 *
 * Provides default implementations for common operations that can be
 * overridden by specific providers as needed.
 */
class ImageCaptchaProviderBase : public IImageCaptchaProvider {
public:
  ImageCaptchaProviderBase();
  virtual ~ImageCaptchaProviderBase() = default;

  // IImageCaptchaProvider interface - common implementations
  void SetAutoSubmit(bool auto_submit) override { auto_submit_ = auto_submit; }
  void SetAllowSkip(bool allow_skip) override { allow_skip_ = allow_skip; }

  // Common vision model analysis (can be overridden for custom prompts)
  std::vector<int> IdentifyMatchingImages(const std::vector<uint8_t>& grid_screenshot,
                                          const std::string& target_description,
                                          int grid_size,
                                          OwlLLMClient* llm_client) override;

protected:
  // Protected helper methods for subclasses

  /**
   * Wait for a specified duration
   */
  void Wait(int ms);

  /**
   * Execute JavaScript and return result
   */
  std::string ExecuteJavaScript(CefRefPtr<CefBrowser> browser,
                                const std::string& script);

  /**
   * Check if an element is visible on the page
   */
  bool IsElementVisible(CefRefPtr<CefBrowser> browser,
                        const std::string& selector);

  /**
   * Click an element using CEF mouse events
   */
  bool ClickElementByPosition(CefRefPtr<CefBrowser> browser,
                              int x, int y, int width, int height);

  /**
   * Scroll element into view
   */
  bool ScrollIntoView(CefRefPtr<CefBrowser> browser,
                      const std::string& selector);

  /**
   * Get random delay between clicks (human-like)
   */
  int GetRandomClickDelay();

  /**
   * Base64 encode binary data
   */
  std::string Base64Encode(const std::vector<uint8_t>& data);

  /**
   * Build vision prompt for grid analysis
   * @param target_description What to look for
   * @param grid_size Number of grid items
   * @param custom_template Optional custom template (uses default if empty)
   */
  std::string BuildVisionPrompt(const std::string& target_description,
                                int grid_size,
                                const std::string& custom_template = "");

  /**
   * Parse vision model response to extract indices
   */
  std::vector<int> ParseVisionResponse(const std::string& response, int grid_size);

  /**
   * Draw numbered overlays on BGRA pixel buffer
   * @param bgra Pixel data
   * @param img_w Image width
   * @param img_h Image height
   * @param grid_items Grid item positions (x, y, width, height)
   * @param crop_x X offset of cropped region
   * @param crop_y Y offset of cropped region
   */
  void DrawNumberedOverlays(uint8_t* bgra, int img_w, int img_h,
                            const std::vector<std::tuple<int, int, int, int>>& grid_items,
                            int crop_x, int crop_y);

  // Configuration
  bool auto_submit_;
  bool allow_skip_;
  ImageCaptchaProviderConfig config_;
};
