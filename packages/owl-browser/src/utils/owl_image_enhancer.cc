#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image_resize2.h"
#include "stb_image.h"
#include "owl_image_enhancer.h"
#include "logger.h"
#include <cmath>
#include <algorithm>
#include <zlib.h>

namespace OwlImageEnhancer {

// PNG encoding helper functions (copied from owl_client.cc approach)
static void WritePNGChunk(std::vector<uint8_t>& output, const char* type, const uint8_t* data, size_t len) {
  // Write length (big-endian)
  uint32_t length = static_cast<uint32_t>(len);
  output.push_back((length >> 24) & 0xFF);
  output.push_back((length >> 16) & 0xFF);
  output.push_back((length >> 8) & 0xFF);
  output.push_back(length & 0xFF);

  // Write type
  for (int i = 0; i < 4; i++) {
    output.push_back(type[i]);
  }

  // Write data
  if (data && len > 0) {
    output.insert(output.end(), data, data + len);
  }

  // Calculate and write CRC
  uint32_t crc = crc32(0L, Z_NULL, 0);
  crc = crc32(crc, reinterpret_cast<const Bytef*>(type), 4);
  if (data && len > 0) {
    crc = crc32(crc, data, static_cast<uInt>(len));
  }
  output.push_back((crc >> 24) & 0xFF);
  output.push_back((crc >> 16) & 0xFF);
  output.push_back((crc >> 8) & 0xFF);
  output.push_back(crc & 0xFF);
}

static std::vector<uint8_t> EncodeRGBAToPNG(const uint8_t* rgba_data, int width, int height) {
  std::vector<uint8_t> output;

  // PNG signature
  const uint8_t signature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  output.insert(output.end(), signature, signature + 8);

  // IHDR chunk
  uint8_t ihdr[13];
  ihdr[0] = (width >> 24) & 0xFF;
  ihdr[1] = (width >> 16) & 0xFF;
  ihdr[2] = (width >> 8) & 0xFF;
  ihdr[3] = width & 0xFF;
  ihdr[4] = (height >> 24) & 0xFF;
  ihdr[5] = (height >> 16) & 0xFF;
  ihdr[6] = (height >> 8) & 0xFF;
  ihdr[7] = height & 0xFF;
  ihdr[8] = 8;   // bit depth
  ihdr[9] = 6;   // color type (RGBA)
  ihdr[10] = 0;  // compression
  ihdr[11] = 0;  // filter
  ihdr[12] = 0;  // interlace
  WritePNGChunk(output, "IHDR", ihdr, 13);

  // Prepare raw image data with filter bytes
  std::vector<uint8_t> raw_data;
  raw_data.reserve(height * (1 + width * 4));
  for (int y = 0; y < height; y++) {
    raw_data.push_back(0);  // filter type: none
    for (int x = 0; x < width; x++) {
      int idx = (y * width + x) * 4;
      raw_data.push_back(rgba_data[idx + 0]);  // R
      raw_data.push_back(rgba_data[idx + 1]);  // G
      raw_data.push_back(rgba_data[idx + 2]);  // B
      raw_data.push_back(rgba_data[idx + 3]);  // A
    }
  }

  // Compress with zlib
  uLongf compressed_size = compressBound(static_cast<uLong>(raw_data.size()));
  std::vector<uint8_t> compressed(compressed_size);
  if (compress2(compressed.data(), &compressed_size, raw_data.data(),
                static_cast<uLong>(raw_data.size()), Z_BEST_SPEED) == Z_OK) {
    compressed.resize(compressed_size);
    WritePNGChunk(output, "IDAT", compressed.data(), compressed.size());
  }

  // IEND chunk
  WritePNGChunk(output, "IEND", nullptr, 0);

  return output;
}

// Decode PNG data to RGBA pixels
std::vector<uint8_t> DecodePNG(const std::vector<uint8_t>& png_data, int& width, int& height) {
  if (png_data.empty()) {
    LOG_ERROR("ImageEnhancer", "Empty PNG data");
    return {};
  }

  int channels;
  unsigned char* decoded = stbi_load_from_memory(
      png_data.data(), static_cast<int>(png_data.size()),
      &width, &height, &channels, 4);  // Force RGBA output

  if (!decoded) {
    LOG_ERROR("ImageEnhancer", "Failed to decode PNG: " + std::string(stbi_failure_reason()));
    return {};
  }

  std::vector<uint8_t> result(decoded, decoded + (width * height * 4));
  stbi_image_free(decoded);

  LOG_DEBUG("ImageEnhancer", "Decoded PNG: " + std::to_string(width) + "x" +
            std::to_string(height) + " (" + std::to_string(channels) + " channels -> RGBA)");

  return result;
}

// Encode RGBA pixels to PNG data
std::vector<uint8_t> EncodePNG(const std::vector<uint8_t>& rgba_data, int width, int height) {
  if (rgba_data.empty() || width <= 0 || height <= 0) {
    LOG_ERROR("ImageEnhancer", "Invalid input for PNG encoding");
    return {};
  }

  return EncodeRGBAToPNG(rgba_data.data(), width, height);
}

// Enhanced version that works with PNG data directly
std::vector<uint8_t> EnhancePNGForVision(
    const std::vector<uint8_t>& png_data,
    int target_min_dimension,
    int& output_width,
    int& output_height) {

  // Decode PNG
  int width, height;
  std::vector<uint8_t> rgba = DecodePNG(png_data, width, height);
  if (rgba.empty()) {
    return png_data;  // Return original on failure
  }

  // Calculate scale factor
  int min_dim = std::min(width, height);
  float scale_factor = static_cast<float>(target_min_dimension) / min_dim;

  // Don't downscale, only upscale
  if (scale_factor < 1.0f) {
    scale_factor = 1.0f;
  }

  // Cap at 3x to avoid excessive memory usage
  if (scale_factor > 3.0f) {
    scale_factor = 3.0f;
  }

  LOG_DEBUG("ImageEnhancer", "Enhancing PNG for vision: " +
           std::to_string(width) + "x" + std::to_string(height) +
           " -> scale " + std::to_string(scale_factor) + "x");

  // Upscale using high-quality filter
  std::vector<uint8_t> upscaled = UpscaleImage(rgba, width, height, scale_factor,
                                                output_width, output_height);
  if (upscaled.empty()) {
    return png_data;
  }

  // Light contrast boost (subtle - don't overdo it)
  std::vector<uint8_t> contrasted = AdjustContrast(upscaled, output_width, output_height, 1.08f);

  // Very light sharpening (0.2 is subtle, 0.5 is strong)
  std::vector<uint8_t> sharpened = SharpenImage(contrasted, output_width, output_height, 0.25f);

  // Encode back to PNG
  std::vector<uint8_t> enhanced_png = EncodePNG(sharpened, output_width, output_height);

  LOG_DEBUG("ImageEnhancer", "PNG enhancement complete: " +
           std::to_string(output_width) + "x" + std::to_string(output_height) +
           " (" + std::to_string(enhanced_png.size()) + " bytes)");

  return enhanced_png;
}

std::vector<uint8_t> UpscaleImage(
    const std::vector<uint8_t>& input_data,
    int input_width,
    int input_height,
    float scale_factor,
    int& output_width,
    int& output_height) {

  if (input_data.empty() || input_width <= 0 || input_height <= 0 || scale_factor <= 0) {
    LOG_ERROR("ImageEnhancer", "Invalid input parameters for upscaling");
    return {};
  }

  output_width = static_cast<int>(input_width * scale_factor);
  output_height = static_cast<int>(input_height * scale_factor);

  // Allocate output buffer (RGBA = 4 channels)
  std::vector<uint8_t> output_data(output_width * output_height * 4);

  // Use Mitchell filter for high-quality upscaling (better than default for photos)
  // STBIR_FILTER_MITCHELL provides good balance between sharpness and smoothness
  STBIR_RESIZE resize;
  stbir_resize_init(&resize,
                    input_data.data(), input_width, input_height, input_width * 4,
                    output_data.data(), output_width, output_height, output_width * 4,
                    STBIR_RGBA, STBIR_TYPE_UINT8_SRGB);

  // Use Mitchell-Netravali filter for better quality upscaling
  stbir_set_filters(&resize, STBIR_FILTER_MITCHELL, STBIR_FILTER_MITCHELL);

  // Execute resize
  stbir_resize_extended(&resize);

  LOG_DEBUG("ImageEnhancer", "Upscaled image from " + std::to_string(input_width) + "x" +
           std::to_string(input_height) + " to " + std::to_string(output_width) + "x" +
           std::to_string(output_height) + " (Mitchell filter, " + std::to_string(scale_factor) + "x)");

  return output_data;
}

std::vector<uint8_t> UpscaleImageToSize(
    const std::vector<uint8_t>& input_data,
    int input_width,
    int input_height,
    int target_width,
    int target_height) {

  if (input_data.empty() || input_width <= 0 || input_height <= 0 ||
      target_width <= 0 || target_height <= 0) {
    LOG_ERROR("ImageEnhancer", "Invalid input parameters for upscaling to size");
    return {};
  }

  // Allocate output buffer (RGBA = 4 channels)
  std::vector<uint8_t> output_data(target_width * target_height * 4);

  // Use high-quality Catmull-Rom filter for upscaling
  stbir_resize_uint8_srgb(
      input_data.data(), input_width, input_height, input_width * 4,
      output_data.data(), target_width, target_height, target_width * 4,
      STBIR_RGBA);

  LOG_DEBUG("ImageEnhancer", "Resized image from " + std::to_string(input_width) + "x" +
           std::to_string(input_height) + " to " + std::to_string(target_width) + "x" +
           std::to_string(target_height));

  return output_data;
}

std::vector<uint8_t> SharpenImage(
    const std::vector<uint8_t>& input_data,
    int width,
    int height,
    float strength) {

  if (input_data.empty() || width <= 0 || height <= 0) {
    return input_data;
  }

  std::vector<uint8_t> output_data = input_data;  // Copy input

  // Simple 3x3 unsharp mask kernel
  // We apply sharpening by: output = input + strength * (input - blurred)
  // Using a simplified approach: enhance edges

  for (int y = 1; y < height - 1; y++) {
    for (int x = 1; x < width - 1; x++) {
      for (int c = 0; c < 3; c++) {  // Only RGB, not alpha
        int idx = (y * width + x) * 4 + c;

        // Get surrounding pixels for Laplacian-like sharpening
        int center = input_data[idx];
        int top = input_data[((y - 1) * width + x) * 4 + c];
        int bottom = input_data[((y + 1) * width + x) * 4 + c];
        int left = input_data[(y * width + (x - 1)) * 4 + c];
        int right = input_data[(y * width + (x + 1)) * 4 + c];

        // Laplacian sharpening: center * 5 - neighbors
        float sharpened = center + strength * (4.0f * center - top - bottom - left - right);

        // Clamp to valid range
        output_data[idx] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, sharpened)));
      }
    }
  }

  LOG_DEBUG("ImageEnhancer", "Applied sharpening with strength " + std::to_string(strength));
  return output_data;
}

std::vector<uint8_t> AdjustContrast(
    const std::vector<uint8_t>& input_data,
    int width,
    int height,
    float factor) {

  if (input_data.empty() || width <= 0 || height <= 0) {
    return input_data;
  }

  std::vector<uint8_t> output_data(input_data.size());

  for (size_t i = 0; i < input_data.size(); i += 4) {
    for (int c = 0; c < 3; c++) {  // Only RGB, not alpha
      float pixel = input_data[i + c];
      // Contrast adjustment: (pixel - 128) * factor + 128
      float adjusted = (pixel - 128.0f) * factor + 128.0f;
      output_data[i + c] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, adjusted)));
    }
    output_data[i + 3] = input_data[i + 3];  // Keep alpha unchanged
  }

  LOG_DEBUG("ImageEnhancer", "Adjusted contrast with factor " + std::to_string(factor));
  return output_data;
}

std::vector<uint8_t> NormalizeBrightness(
    const std::vector<uint8_t>& input_data,
    int width,
    int height,
    int target_mean) {

  if (input_data.empty() || width <= 0 || height <= 0) {
    return input_data;
  }

  // Calculate current mean brightness
  long long total_brightness = 0;
  size_t pixel_count = (input_data.size() / 4);

  for (size_t i = 0; i < input_data.size(); i += 4) {
    // Use luminance formula: 0.299*R + 0.587*G + 0.114*B
    int lum = static_cast<int>(0.299f * input_data[i] + 0.587f * input_data[i+1] + 0.114f * input_data[i+2]);
    total_brightness += lum;
  }

  float current_mean = static_cast<float>(total_brightness) / pixel_count;

  // Calculate adjustment needed
  float adjustment = static_cast<float>(target_mean) - current_mean;

  // Limit adjustment to avoid extreme changes
  if (adjustment > 50.0f) adjustment = 50.0f;
  if (adjustment < -50.0f) adjustment = -50.0f;

  // Skip if image is already well-balanced
  if (std::abs(adjustment) < 10.0f) {
    LOG_DEBUG("ImageEnhancer", "Brightness already normalized (mean: " + std::to_string(current_mean) + ")");
    return input_data;
  }

  std::vector<uint8_t> output_data(input_data.size());

  for (size_t i = 0; i < input_data.size(); i += 4) {
    for (int c = 0; c < 3; c++) {
      float adjusted = input_data[i + c] + adjustment;
      output_data[i + c] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, adjusted)));
    }
    output_data[i + 3] = input_data[i + 3];  // Keep alpha
  }

  LOG_DEBUG("ImageEnhancer", "Normalized brightness from " + std::to_string(current_mean) +
            " to ~" + std::to_string(target_mean) + " (adjustment: " + std::to_string(adjustment) + ")");
  return output_data;
}

std::vector<uint8_t> AutoLevels(
    const std::vector<uint8_t>& input_data,
    int width,
    int height) {

  if (input_data.empty() || width <= 0 || height <= 0) {
    return input_data;
  }

  // Find min and max values for each channel
  uint8_t min_r = 255, max_r = 0;
  uint8_t min_g = 255, max_g = 0;
  uint8_t min_b = 255, max_b = 0;

  for (size_t i = 0; i < input_data.size(); i += 4) {
    min_r = std::min(min_r, input_data[i]);
    max_r = std::max(max_r, input_data[i]);
    min_g = std::min(min_g, input_data[i + 1]);
    max_g = std::max(max_g, input_data[i + 1]);
    min_b = std::min(min_b, input_data[i + 2]);
    max_b = std::max(max_b, input_data[i + 2]);
  }

  // Skip if already using full range (within tolerance)
  int range_r = max_r - min_r;
  int range_g = max_g - min_g;
  int range_b = max_b - min_b;

  if (range_r > 230 && range_g > 230 && range_b > 230) {
    LOG_DEBUG("ImageEnhancer", "Auto-levels skipped - already using full range");
    return input_data;
  }

  // Avoid division by zero
  if (range_r < 10) range_r = 255;
  if (range_g < 10) range_g = 255;
  if (range_b < 10) range_b = 255;

  std::vector<uint8_t> output_data(input_data.size());

  for (size_t i = 0; i < input_data.size(); i += 4) {
    // Stretch each channel to full 0-255 range
    output_data[i] = static_cast<uint8_t>(255.0f * (input_data[i] - min_r) / range_r);
    output_data[i + 1] = static_cast<uint8_t>(255.0f * (input_data[i + 1] - min_g) / range_g);
    output_data[i + 2] = static_cast<uint8_t>(255.0f * (input_data[i + 2] - min_b) / range_b);
    output_data[i + 3] = input_data[i + 3];  // Keep alpha
  }

  LOG_DEBUG("ImageEnhancer", "Applied auto-levels: R[" + std::to_string(min_r) + "-" + std::to_string(max_r) +
            "] G[" + std::to_string(min_g) + "-" + std::to_string(max_g) +
            "] B[" + std::to_string(min_b) + "-" + std::to_string(max_b) + "]");
  return output_data;
}

std::vector<uint8_t> EnhanceTileForVision(
    const std::vector<uint8_t>& input_data,
    int input_width,
    int input_height,
    int target_size) {

  if (input_data.empty() || input_width <= 0 || input_height <= 0) {
    LOG_ERROR("ImageEnhancer", "Invalid input for tile enhancement");
    return {};
  }

  // Calculate scale factor to reach target size
  int min_dim = std::min(input_width, input_height);
  float scale_factor = static_cast<float>(target_size) / min_dim;

  // Ensure minimum upscale
  if (scale_factor < 1.5f) scale_factor = 1.5f;
  // Cap to avoid memory issues
  if (scale_factor > 4.0f) scale_factor = 4.0f;

  LOG_DEBUG("ImageEnhancer", "Enhancing tile: " + std::to_string(input_width) + "x" +
           std::to_string(input_height) + " -> scale " + std::to_string(scale_factor) + "x");

  // Step 1: Upscale with high-quality filter
  int output_width, output_height;
  std::vector<uint8_t> upscaled = UpscaleImage(
      input_data, input_width, input_height, scale_factor,
      output_width, output_height);

  if (upscaled.empty()) {
    return input_data;
  }

  // Step 2: Auto-levels to maximize dynamic range
  std::vector<uint8_t> leveled = AutoLevels(upscaled, output_width, output_height);

  // Step 3: Normalize brightness to target mean (~128 for balanced)
  std::vector<uint8_t> normalized = NormalizeBrightness(leveled, output_width, output_height, 128);

  // Step 4: Contrast boost for edge definition
  std::vector<uint8_t> contrasted = AdjustContrast(normalized, output_width, output_height, 1.12f);

  // Step 5: Sharpening for detail enhancement
  std::vector<uint8_t> sharpened = SharpenImage(contrasted, output_width, output_height, 0.3f);

  LOG_DEBUG("ImageEnhancer", "Tile enhancement complete: " +
           std::to_string(output_width) + "x" + std::to_string(output_height));

  return sharpened;
}

std::vector<uint8_t> EnhanceForVision(
    const std::vector<uint8_t>& input_data,
    int input_width,
    int input_height,
    int target_min_dimension) {

  if (input_data.empty() || input_width <= 0 || input_height <= 0) {
    LOG_ERROR("ImageEnhancer", "Invalid input for vision enhancement");
    return {};
  }

  // Calculate scale factor to reach target minimum dimension
  int min_dim = std::min(input_width, input_height);
  float scale_factor = static_cast<float>(target_min_dimension) / min_dim;

  // Don't downscale, only upscale
  if (scale_factor < 1.0f) {
    scale_factor = 1.0f;
  }

  // Cap at 3x to avoid excessive memory usage
  if (scale_factor > 3.0f) {
    scale_factor = 3.0f;
  }

  LOG_DEBUG("ImageEnhancer", "Enhancing image for vision: " +
           std::to_string(input_width) + "x" + std::to_string(input_height) +
           " -> scale " + std::to_string(scale_factor) + "x");

  // Step 1: Upscale
  int output_width, output_height;
  std::vector<uint8_t> upscaled = UpscaleImage(
      input_data, input_width, input_height, scale_factor,
      output_width, output_height);

  if (upscaled.empty()) {
    return input_data;  // Return original on failure
  }

  // Step 2: Slight contrast boost for clarity
  std::vector<uint8_t> contrasted = AdjustContrast(upscaled, output_width, output_height, 1.1f);

  // Step 3: Light sharpening to enhance edges
  std::vector<uint8_t> sharpened = SharpenImage(contrasted, output_width, output_height, 0.3f);

  LOG_DEBUG("ImageEnhancer", "Vision enhancement complete: " +
           std::to_string(output_width) + "x" + std::to_string(output_height));

  return sharpened;
}

}  // namespace OwlImageEnhancer
