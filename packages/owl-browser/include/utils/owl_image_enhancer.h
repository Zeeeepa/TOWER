#ifndef OWL_IMAGE_ENHANCER_H_
#define OWL_IMAGE_ENHANCER_H_

#include <vector>
#include <cstdint>

// Image enhancement utilities for improving vision model accuracy
// Uses stb_image_resize2 for high-quality upscaling
// Uses stb_image for PNG decoding
namespace OwlImageEnhancer {

// Decode PNG data to RGBA pixels
// @param png_data PNG file data
// @param width Output parameter for image width
// @param height Output parameter for image height
// @return RGBA pixel data
std::vector<uint8_t> DecodePNG(const std::vector<uint8_t>& png_data, int& width, int& height);

// Encode RGBA pixels to PNG data
// @param rgba_data Raw RGBA pixel data
// @param width Image width
// @param height Image height
// @return PNG file data
std::vector<uint8_t> EncodePNG(const std::vector<uint8_t>& rgba_data, int width, int height);

// Enhanced version that works with PNG data directly
// Decodes PNG, enhances, and re-encodes to PNG
// @param png_data Input PNG file data
// @param target_min_dimension Minimum dimension after upscaling (default 800)
// @param output_width Output parameter for new width
// @param output_height Output parameter for new height
// @return Enhanced PNG file data
std::vector<uint8_t> EnhancePNGForVision(
    const std::vector<uint8_t>& png_data,
    int target_min_dimension,
    int& output_width,
    int& output_height);

// Upscale an RGBA image using high-quality bicubic interpolation
// @param input_data Raw RGBA pixel data
// @param input_width Original width
// @param input_height Original height
// @param scale_factor Upscale factor (e.g., 2.0 = double size)
// @param output_width Output parameter for new width
// @param output_height Output parameter for new height
// @return Upscaled RGBA pixel data
std::vector<uint8_t> UpscaleImage(
    const std::vector<uint8_t>& input_data,
    int input_width,
    int input_height,
    float scale_factor,
    int& output_width,
    int& output_height);

// Upscale an RGBA image to specific dimensions
std::vector<uint8_t> UpscaleImageToSize(
    const std::vector<uint8_t>& input_data,
    int input_width,
    int input_height,
    int target_width,
    int target_height);

// Sharpen an RGBA image using unsharp mask
// @param input_data Raw RGBA pixel data
// @param width Image width
// @param height Image height
// @param strength Sharpening strength (0.0 - 2.0, default 0.5)
// @return Sharpened RGBA pixel data
std::vector<uint8_t> SharpenImage(
    const std::vector<uint8_t>& input_data,
    int width,
    int height,
    float strength = 0.5f);

// Enhance image for better vision model processing
// Combines upscaling and sharpening
// @param input_data Raw RGBA pixel data (or PNG data)
// @param input_width Original width (0 if PNG data)
// @param input_height Original height (0 if PNG data)
// @param target_min_dimension Minimum dimension after upscaling (default 800)
// @return Enhanced RGBA pixel data
std::vector<uint8_t> EnhanceForVision(
    const std::vector<uint8_t>& input_data,
    int input_width,
    int input_height,
    int target_min_dimension = 800);

// Increase contrast of an RGBA image
// @param input_data Raw RGBA pixel data
// @param width Image width
// @param height Image height
// @param factor Contrast factor (1.0 = no change, 1.5 = 50% more contrast)
// @return Contrast-adjusted RGBA pixel data
std::vector<uint8_t> AdjustContrast(
    const std::vector<uint8_t>& input_data,
    int width,
    int height,
    float factor = 1.2f);

// Normalize brightness to target mean value
// @param input_data Raw RGBA pixel data
// @param width Image width
// @param height Image height
// @param target_mean Target mean brightness (0-255, default 128)
// @return Brightness-normalized RGBA pixel data
std::vector<uint8_t> NormalizeBrightness(
    const std::vector<uint8_t>& input_data,
    int width,
    int height,
    int target_mean = 128);

// Apply auto-levels to stretch color range to full 0-255
// Improves contrast in washed-out or dark images
// @param input_data Raw RGBA pixel data
// @param width Image width
// @param height Image height
// @return Auto-leveled RGBA pixel data
std::vector<uint8_t> AutoLevels(
    const std::vector<uint8_t>& input_data,
    int width,
    int height);

// Enhanced tile processing for vision model classification
// Combines: upscaling -> auto-levels -> brightness normalization -> contrast -> sharpening
// Optimized for captcha tile analysis
// @param input_data Raw RGBA pixel data
// @param input_width Original width
// @param input_height Original height
// @param target_size Minimum dimension after upscaling (default 384)
// @return Enhanced RGBA pixel data
std::vector<uint8_t> EnhanceTileForVision(
    const std::vector<uint8_t>& input_data,
    int input_width,
    int input_height,
    int target_size = 384);

}  // namespace OwlImageEnhancer

#endif  // OWL_IMAGE_ENHANCER_H_
