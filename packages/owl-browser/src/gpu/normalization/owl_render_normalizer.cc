/**
 * OWL Render Normalizer Implementation
 *
 * Normalizes GPU render output to produce consistent fingerprints.
 */

#include "gpu/owl_render_normalizer.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace owl {
namespace gpu {

// ============================================================================
// PixelData Implementation
// ============================================================================

PixelData::PixelData(void* d, size_t w, size_t h, PixelFormat f)
    : data(d), width(w), height(h), format(f), owns_data(false) {
    stride = width * GetPixelSize();
}

PixelData::PixelData(size_t w, size_t h, PixelFormat f)
    : width(w), height(h), format(f), owns_data(true) {
    size_t pixel_size = GetPixelSize();
    stride = width * pixel_size;
    data = new uint8_t[height * stride];
    std::memset(data, 0, height * stride);
}

PixelData::~PixelData() {
    if (owns_data && data) {
        delete[] static_cast<uint8_t*>(data);
    }
}

PixelData::PixelData(PixelData&& other) noexcept
    : data(other.data)
    , width(other.width)
    , height(other.height)
    , stride(other.stride)
    , format(other.format)
    , owns_data(other.owns_data) {
    other.data = nullptr;
    other.owns_data = false;
}

PixelData& PixelData::operator=(PixelData&& other) noexcept {
    if (this != &other) {
        if (owns_data && data) {
            delete[] static_cast<uint8_t*>(data);
        }
        data = other.data;
        width = other.width;
        height = other.height;
        stride = other.stride;
        format = other.format;
        owns_data = other.owns_data;
        other.data = nullptr;
        other.owns_data = false;
    }
    return *this;
}

size_t PixelData::GetPixelSize() const {
    switch (format) {
        case PixelFormat::RGBA8: return 4;
        case PixelFormat::RGB8: return 3;
        case PixelFormat::RGBA16F: return 8;
        case PixelFormat::RGBA32F: return 16;
        case PixelFormat::RG8: return 2;
        case PixelFormat::R8: return 1;
        case PixelFormat::Depth16: return 2;
        case PixelFormat::Depth24: return 3;
        case PixelFormat::Depth32F: return 4;
    }
    return 4;
}

size_t PixelData::GetTotalSize() const {
    return height * stride;
}

// ============================================================================
// RenderNormalizer Implementation
// ============================================================================

RenderNormalizer::RenderNormalizer() = default;
RenderNormalizer::~RenderNormalizer() = default;

// Deterministic hash function for noise generation
uint64_t RenderNormalizer::HashPixel(uint64_t seed, size_t x, size_t y, size_t channel) {
    uint64_t h = seed;
    h ^= x * 0x517cc1b727220a95ULL;
    h ^= y * 0x5851f42d4c957f2dULL;
    h ^= channel * 0x2545f4914f6cdd1dULL;

    // Mix
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;

    return h;
}

uint64_t RenderNormalizer::HashCombine(uint64_t h1, uint64_t h2) {
    return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
}

int RenderNormalizer::DeterministicRandom(uint64_t seed, int min, int max) {
    uint64_t hash = HashPixel(seed, 0, 0, 0);
    int range = max - min + 1;
    return min + static_cast<int>(hash % range);
}

float RenderNormalizer::DeterministicRandomFloat(uint64_t seed) {
    uint64_t hash = HashPixel(seed, 0, 0, 0);
    return static_cast<float>(hash & 0xFFFFFFFF) / static_cast<float>(0xFFFFFFFF);
}

void RenderNormalizer::Normalize(PixelData& pixels, const GPUProfile& profile) {
    if (!pixels.data || pixels.width == 0 || pixels.height == 0) return;
    if (!config_.enable_pixel_normalization) return;

    uint64_t seed = profile.GetRenderSeed();

    switch (config_.mode) {
        case NormalizationMode::Deterministic:
            ApplyDeterministicNoise(pixels, seed, config_.intensity);
            break;
        case NormalizationMode::Uniform:
            ApplyUniformNoise(pixels, config_.max_delta);
            break;
        case NormalizationMode::Gradient:
            ApplyGradientNoise(pixels, seed);
            break;
        case NormalizationMode::None:
        default:
            break;
    }

    if (config_.enable_aa_normalization) {
        NormalizeAntiAliasing(pixels, config_.target_aa);
    }

    if (config_.enable_color_normalization) {
        NormalizeColorSpace(pixels, config_.target_gamma);
    }
}

void RenderNormalizer::Normalize(void* pixels, size_t width, size_t height,
                                  PixelFormat format, uint64_t seed) {
    if (!pixels) return;

    PixelData pd(pixels, width, height, format);
    ApplyDeterministicNoise(pd, seed, config_.intensity);
}

void RenderNormalizer::NormalizeReadPixels(void* pixels, int32_t x, int32_t y,
                                            int32_t width, int32_t height,
                                            uint32_t gl_format, uint32_t gl_type,
                                            const GPUProfile& profile) {
    if (!pixels || width <= 0 || height <= 0) return;

    PixelFormat format = GLToPixelFormat(gl_format, gl_type);
    PixelData pd(pixels, static_cast<size_t>(width), static_cast<size_t>(height), format);
    Normalize(pd, profile);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.pixels_normalized += width * height;
        stats_.bytes_processed += pd.GetTotalSize();
    }
}

// ==================== Noise Application ====================

void RenderNormalizer::ApplyDeterministicNoise(PixelData& pixels, uint64_t seed, double intensity) {
    if (pixels.format == PixelFormat::RGBA8) {
        ApplyNoiseRGBA8(static_cast<uint8_t*>(pixels.data),
                        pixels.width, pixels.height, pixels.stride,
                        seed, intensity, config_.max_delta);
    } else if (pixels.format == PixelFormat::RGBA16F) {
        ApplyNoiseRGBA16F(static_cast<uint16_t*>(pixels.data),
                         pixels.width, pixels.height, pixels.stride,
                         seed, intensity);
    } else if (pixels.format == PixelFormat::RGBA32F) {
        ApplyNoiseRGBA32F(static_cast<float*>(pixels.data),
                         pixels.width, pixels.height, pixels.stride,
                         seed, intensity);
    }

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.noise_applications++;
    }
}

void RenderNormalizer::ApplyNoiseRGBA8(uint8_t* pixels, size_t width, size_t height,
                                        size_t stride, uint64_t seed, double intensity,
                                        int max_delta) {
    for (size_t y = 0; y < height; ++y) {
        uint8_t* row = pixels + y * stride;
        for (size_t x = 0; x < width; ++x) {
            uint8_t* pixel = row + x * 4;

            // Skip fully transparent pixels
            if (pixel[3] == 0) continue;

            // Generate deterministic noise for each channel
            int noise_r = DeterministicRandom(HashPixel(seed, x, y, 0), -max_delta, max_delta);
            int noise_g = DeterministicRandom(HashPixel(seed, x, y, 1), -max_delta, max_delta);
            int noise_b = DeterministicRandom(HashPixel(seed, x, y, 2), -max_delta, max_delta);

            // Apply noise with clamping
            pixel[0] = static_cast<uint8_t>(std::clamp(pixel[0] + noise_r, 0, 255));
            pixel[1] = static_cast<uint8_t>(std::clamp(pixel[1] + noise_g, 0, 255));
            pixel[2] = static_cast<uint8_t>(std::clamp(pixel[2] + noise_b, 0, 255));
        }
    }
}

void RenderNormalizer::ApplyNoiseRGBA16F(uint16_t* pixels, size_t width, size_t height,
                                          size_t stride, uint64_t seed, double intensity) {
    size_t pixel_stride = stride / sizeof(uint16_t);
    for (size_t y = 0; y < height; ++y) {
        uint16_t* row = pixels + y * pixel_stride;
        for (size_t x = 0; x < width; ++x) {
            uint16_t* pixel = row + x * 4;

            // Generate float noise
            float noise_r = (DeterministicRandomFloat(HashPixel(seed, x, y, 0)) - 0.5f) * intensity * 2.0f;
            float noise_g = (DeterministicRandomFloat(HashPixel(seed, x, y, 1)) - 0.5f) * intensity * 2.0f;
            float noise_b = (DeterministicRandomFloat(HashPixel(seed, x, y, 2)) - 0.5f) * intensity * 2.0f;

            // Convert half float to float, apply noise, convert back
            // Simplified: just apply to raw bits for demonstration
            // In production, use proper half-float conversion
            pixel[0] = static_cast<uint16_t>(std::clamp(static_cast<int>(pixel[0]) +
                static_cast<int>(noise_r * 1000), 0, 65535));
            pixel[1] = static_cast<uint16_t>(std::clamp(static_cast<int>(pixel[1]) +
                static_cast<int>(noise_g * 1000), 0, 65535));
            pixel[2] = static_cast<uint16_t>(std::clamp(static_cast<int>(pixel[2]) +
                static_cast<int>(noise_b * 1000), 0, 65535));
        }
    }
}

void RenderNormalizer::ApplyNoiseRGBA32F(float* pixels, size_t width, size_t height,
                                          size_t stride, uint64_t seed, double intensity) {
    size_t pixel_stride = stride / sizeof(float);
    for (size_t y = 0; y < height; ++y) {
        float* row = pixels + y * pixel_stride;
        for (size_t x = 0; x < width; ++x) {
            float* pixel = row + x * 4;

            // Generate float noise
            float noise_r = (DeterministicRandomFloat(HashPixel(seed, x, y, 0)) - 0.5f) * static_cast<float>(intensity * 2.0);
            float noise_g = (DeterministicRandomFloat(HashPixel(seed, x, y, 1)) - 0.5f) * static_cast<float>(intensity * 2.0);
            float noise_b = (DeterministicRandomFloat(HashPixel(seed, x, y, 2)) - 0.5f) * static_cast<float>(intensity * 2.0);

            pixel[0] = std::clamp(pixel[0] + noise_r, 0.0f, 1.0f);
            pixel[1] = std::clamp(pixel[1] + noise_g, 0.0f, 1.0f);
            pixel[2] = std::clamp(pixel[2] + noise_b, 0.0f, 1.0f);
        }
    }
}

void RenderNormalizer::ApplyUniformNoise(PixelData& pixels, int delta) {
    if (pixels.format != PixelFormat::RGBA8) return;

    uint8_t* data = static_cast<uint8_t*>(pixels.data);
    for (size_t y = 0; y < pixels.height; ++y) {
        uint8_t* row = data + y * pixels.stride;
        for (size_t x = 0; x < pixels.width; ++x) {
            uint8_t* pixel = row + x * 4;
            if (pixel[3] == 0) continue;

            pixel[0] = static_cast<uint8_t>(std::clamp(static_cast<int>(pixel[0]) + delta, 0, 255));
            pixel[1] = static_cast<uint8_t>(std::clamp(static_cast<int>(pixel[1]) + delta, 0, 255));
            pixel[2] = static_cast<uint8_t>(std::clamp(static_cast<int>(pixel[2]) + delta, 0, 255));
        }
    }
}

void RenderNormalizer::ApplyGradientNoise(PixelData& pixels, uint64_t seed) {
    if (pixels.format != PixelFormat::RGBA8) return;

    uint8_t* data = static_cast<uint8_t*>(pixels.data);
    for (size_t y = 0; y < pixels.height; ++y) {
        uint8_t* row = data + y * pixels.stride;
        float y_factor = static_cast<float>(y) / static_cast<float>(pixels.height);

        for (size_t x = 0; x < pixels.width; ++x) {
            uint8_t* pixel = row + x * 4;
            if (pixel[3] == 0) continue;

            float x_factor = static_cast<float>(x) / static_cast<float>(pixels.width);
            float gradient = (x_factor + y_factor) * 0.5f;

            int delta = DeterministicRandom(seed, -config_.max_delta, config_.max_delta);
            delta = static_cast<int>(delta * gradient);

            pixel[0] = static_cast<uint8_t>(std::clamp(static_cast<int>(pixel[0]) + delta, 0, 255));
            pixel[1] = static_cast<uint8_t>(std::clamp(static_cast<int>(pixel[1]) + delta, 0, 255));
            pixel[2] = static_cast<uint8_t>(std::clamp(static_cast<int>(pixel[2]) + delta, 0, 255));
        }
    }
}

// ==================== Anti-Aliasing Normalization ====================

void RenderNormalizer::NormalizeAntiAliasing(PixelData& pixels, AAMode target_mode) {
    // Simplified AA normalization: apply slight blur to edges
    // This helps mask differences in AA implementation between GPUs

    if (pixels.format != PixelFormat::RGBA8) return;
    if (!config_.smooth_edges) return;

    PixelData edges = DetectEdges(pixels);
    SmoothEdges(pixels, edges);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.aa_normalizations++;
    }
}

PixelData RenderNormalizer::DetectEdges(const PixelData& pixels) {
    // Simple Sobel edge detection
    PixelData edges(pixels.width, pixels.height, PixelFormat::R8);

    if (pixels.format != PixelFormat::RGBA8) return edges;

    const uint8_t* src = static_cast<const uint8_t*>(pixels.data);
    uint8_t* dst = static_cast<uint8_t*>(edges.data);

    // Sobel kernels
    const int gx[3][3] = {{-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1}};
    const int gy[3][3] = {{-1, -2, -1}, {0, 0, 0}, {1, 2, 1}};

    for (size_t y = 1; y < pixels.height - 1; ++y) {
        for (size_t x = 1; x < pixels.width - 1; ++x) {
            int sum_x = 0, sum_y = 0;

            for (int ky = -1; ky <= 1; ++ky) {
                for (int kx = -1; kx <= 1; ++kx) {
                    const uint8_t* p = src + (y + ky) * pixels.stride + (x + kx) * 4;
                    int gray = (p[0] + p[1] + p[2]) / 3;
                    sum_x += gray * gx[ky + 1][kx + 1];
                    sum_y += gray * gy[ky + 1][kx + 1];
                }
            }

            int magnitude = static_cast<int>(std::sqrt(sum_x * sum_x + sum_y * sum_y));
            dst[y * edges.stride + x] = static_cast<uint8_t>(std::min(magnitude, 255));
        }
    }

    return edges;
}

void RenderNormalizer::SmoothEdges(PixelData& pixels, const PixelData& edge_mask) {
    if (pixels.format != PixelFormat::RGBA8) return;

    uint8_t* data = static_cast<uint8_t*>(pixels.data);
    const uint8_t* edges = static_cast<const uint8_t*>(edge_mask.data);

    // Apply slight blur only where edges are detected
    for (size_t y = 1; y < pixels.height - 1; ++y) {
        for (size_t x = 1; x < pixels.width - 1; ++x) {
            uint8_t edge_strength = edges[y * edge_mask.stride + x];
            if (edge_strength < 30) continue;  // Only smooth significant edges

            float blend = std::min(edge_strength / 255.0f * 0.3f, 0.3f);

            // Simple 3x3 box blur
            int sum[3] = {0, 0, 0};
            for (int ky = -1; ky <= 1; ++ky) {
                for (int kx = -1; kx <= 1; ++kx) {
                    const uint8_t* p = data + (y + ky) * pixels.stride + (x + kx) * 4;
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                }
            }

            uint8_t* pixel = data + y * pixels.stride + x * 4;
            pixel[0] = static_cast<uint8_t>(pixel[0] * (1 - blend) + (sum[0] / 9) * blend);
            pixel[1] = static_cast<uint8_t>(pixel[1] * (1 - blend) + (sum[1] / 9) * blend);
            pixel[2] = static_cast<uint8_t>(pixel[2] * (1 - blend) + (sum[2] / 9) * blend);
        }
    }
}

// ==================== Color Space Normalization ====================

void RenderNormalizer::NormalizeColorSpace(PixelData& pixels, float target_gamma) {
    if (pixels.format != PixelFormat::RGBA8) return;

    uint8_t* data = static_cast<uint8_t*>(pixels.data);

    // Build gamma correction LUT
    float gamma_correction = 1.0f / target_gamma;
    uint8_t lut[256];
    for (int i = 0; i < 256; ++i) {
        float normalized = static_cast<float>(i) / 255.0f;
        float corrected = std::pow(normalized, gamma_correction);
        lut[i] = static_cast<uint8_t>(std::clamp(corrected * 255.0f, 0.0f, 255.0f));
    }

    for (size_t y = 0; y < pixels.height; ++y) {
        uint8_t* row = data + y * pixels.stride;
        for (size_t x = 0; x < pixels.width; ++x) {
            uint8_t* pixel = row + x * 4;
            pixel[0] = lut[pixel[0]];
            pixel[1] = lut[pixel[1]];
            pixel[2] = lut[pixel[2]];
        }
    }

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.color_normalizations++;
    }
}

// ==================== Precision Normalization ====================

void RenderNormalizer::NormalizePrecision(PixelData& pixels, int mantissa_bits) {
    // Round floating point values to match target precision
    if (pixels.format != PixelFormat::RGBA32F) return;

    float* data = static_cast<float*>(pixels.data);
    uint32_t mask = ~((1u << (23 - mantissa_bits)) - 1);

    for (size_t i = 0; i < pixels.height * pixels.width * 4; ++i) {
        uint32_t* bits = reinterpret_cast<uint32_t*>(&data[i]);
        *bits &= mask;
    }
}

void RenderNormalizer::RoundValues(PixelData& pixels, int bits_per_channel) {
    if (pixels.format != PixelFormat::RGBA8) return;
    if (bits_per_channel >= 8) return;

    uint8_t* data = static_cast<uint8_t*>(pixels.data);
    int shift = 8 - bits_per_channel;
    uint8_t mask = static_cast<uint8_t>(0xFF << shift);

    for (size_t i = 0; i < pixels.height * pixels.stride; ++i) {
        data[i] = (data[i] & mask) | (data[i] >> bits_per_channel);
    }
}

// ==================== Hash Generation ====================

uint64_t RenderNormalizer::GenerateHash(const PixelData& pixels, uint64_t seed) {
    if (!pixels.data) return 0;

    const uint8_t* data = static_cast<const uint8_t*>(pixels.data);
    uint64_t hash = seed;

    for (size_t i = 0; i < pixels.GetTotalSize(); i += 16) {
        hash = HashCombine(hash, *reinterpret_cast<const uint64_t*>(data + i));
    }

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.hashes_generated++;
    }

    return hash;
}

uint64_t RenderNormalizer::GenerateProfileHash(const PixelData& pixels, const GPUProfile& profile) {
    return GenerateHash(pixels, profile.GetRenderSeed());
}

std::string RenderNormalizer::GenerateCanvasFingerprint(const PixelData& pixels, uint64_t seed) {
    uint64_t hash = GenerateHash(pixels, seed);

    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return ss.str();
}

// ==================== Utilities ====================

PixelFormat RenderNormalizer::GLToPixelFormat(uint32_t gl_format, uint32_t gl_type) {
    // GL_RGBA = 0x1908, GL_UNSIGNED_BYTE = 0x1401
    if (gl_format == 0x1908 && gl_type == 0x1401) return PixelFormat::RGBA8;
    // GL_RGB = 0x1907
    if (gl_format == 0x1907 && gl_type == 0x1401) return PixelFormat::RGB8;
    // GL_RGBA, GL_FLOAT = 0x1406
    if (gl_format == 0x1908 && gl_type == 0x1406) return PixelFormat::RGBA32F;
    // GL_RGBA, GL_HALF_FLOAT = 0x140B
    if (gl_format == 0x1908 && gl_type == 0x140B) return PixelFormat::RGBA16F;

    return PixelFormat::RGBA8;  // Default
}

void RenderNormalizer::PixelFormatToGL(PixelFormat format, uint32_t& gl_format, uint32_t& gl_type) {
    switch (format) {
        case PixelFormat::RGBA8:
            gl_format = 0x1908; gl_type = 0x1401; break;
        case PixelFormat::RGB8:
            gl_format = 0x1907; gl_type = 0x1401; break;
        case PixelFormat::RGBA32F:
            gl_format = 0x1908; gl_type = 0x1406; break;
        case PixelFormat::RGBA16F:
            gl_format = 0x1908; gl_type = 0x140B; break;
        default:
            gl_format = 0x1908; gl_type = 0x1401; break;
    }
}

size_t RenderNormalizer::GetBytesPerPixel(PixelFormat format) {
    switch (format) {
        case PixelFormat::RGBA8: return 4;
        case PixelFormat::RGB8: return 3;
        case PixelFormat::RGBA16F: return 8;
        case PixelFormat::RGBA32F: return 16;
        case PixelFormat::RG8: return 2;
        case PixelFormat::R8: return 1;
        default: return 4;
    }
}

RenderNormalizer::NormalizerStats RenderNormalizer::GetStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void RenderNormalizer::ResetStats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = NormalizerStats{};
}

// ============================================================================
// CanvasFingerprintGenerator Implementation
// ============================================================================

std::string CanvasFingerprintGenerator::Generate(const PixelData& pixels, uint64_t profile_seed) {
    uint64_t hash = HashPixels(static_cast<const uint8_t*>(pixels.data),
                               pixels.GetTotalSize(), profile_seed);
    return HashToString(hash);
}

std::string CanvasFingerprintGenerator::GenerateWebGLHash(const PixelData& pixels,
                                                           const GPUProfile& profile) {
    return Generate(pixels, profile.GetRenderSeed());
}

std::string CanvasFingerprintGenerator::GenerateCanvas2DHash(const PixelData& pixels,
                                                              const GPUProfile& profile) {
    return Generate(pixels, profile.GetCanvasSeed());
}

double CanvasFingerprintGenerator::CompareSimilarity(const std::string& fp1,
                                                      const std::string& fp2) {
    if (fp1.length() != fp2.length()) return 0.0;

    size_t matching = 0;
    for (size_t i = 0; i < fp1.length(); ++i) {
        if (fp1[i] == fp2[i]) matching++;
    }

    return static_cast<double>(matching) / static_cast<double>(fp1.length());
}

uint64_t CanvasFingerprintGenerator::HashPixels(const uint8_t* pixels, size_t size,
                                                 uint64_t seed) {
    uint64_t hash = seed;

    for (size_t i = 0; i < size; i += 4) {
        uint32_t pixel = *reinterpret_cast<const uint32_t*>(pixels + i);
        hash ^= pixel * 0x517cc1b727220a95ULL;
        hash = (hash << 31) | (hash >> 33);
        hash *= 0xff51afd7ed558ccdULL;
    }

    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ULL;
    hash ^= hash >> 33;

    return hash;
}

std::string CanvasFingerprintGenerator::HashToString(uint64_t hash) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return ss.str();
}

// ============================================================================
// FrameBufferProcessor Implementation
// ============================================================================

void FrameBufferProcessor::ProcessFrameBuffer(void* pixels, size_t width, size_t height,
                                               PixelFormat format, const GPUProfile& profile) {
    static RenderNormalizer normalizer;
    PixelData pd(pixels, width, height, format);
    normalizer.Normalize(pd, profile);
}

void FrameBufferProcessor::ProcessReadPixels(void* pixels, int32_t x, int32_t y,
                                              int32_t width, int32_t height,
                                              uint32_t gl_format, uint32_t gl_type,
                                              const GPUProfile& profile) {
    static RenderNormalizer normalizer;
    normalizer.NormalizeReadPixels(pixels, x, y, width, height, gl_format, gl_type, profile);
}

void FrameBufferProcessor::ProcessDataURL(std::string& data_url, const GPUProfile& profile) {
    // TODO: Decode base64, normalize pixels, re-encode
    // For now, this is a placeholder
    (void)data_url;
    (void)profile;
}

} // namespace gpu
} // namespace owl
