#pragma once

/**
 * OWL Render Normalizer
 *
 * Normalizes GPU rendering output to produce consistent fingerprints regardless
 * of the actual hardware. This is the key component for defeating render-based
 * GPU fingerprinting techniques.
 *
 * Key Functions:
 * - Pixel normalization (apply deterministic transforms)
 * - Anti-aliasing normalization
 * - Color space normalization
 * - Floating-point precision normalization
 * - Consistent hash generation
 */

#include "gpu/owl_gpu_virtualization.h"
#include "gpu/owl_gpu_profile.h"
#include <cstdint>
#include <memory>
#include <functional>

namespace owl {
namespace gpu {

/**
 * Pixel format enumeration
 */
enum class PixelFormat : uint8_t {
    RGBA8,      // 8-bit RGBA
    RGB8,       // 8-bit RGB
    RGBA16F,    // 16-bit float RGBA
    RGBA32F,    // 32-bit float RGBA
    RG8,        // 8-bit RG
    R8,         // 8-bit R
    Depth16,    // 16-bit depth
    Depth24,    // 24-bit depth
    Depth32F    // 32-bit float depth
};

/**
 * Normalization mode
 */
enum class NormalizationMode : uint8_t {
    None,           // No normalization
    Deterministic,  // Deterministic seed-based modification
    Uniform,        // Uniform modification across all pixels
    Gradient        // Gradient-based modification
};

/**
 * Normalization configuration
 */
struct RenderNormalizationConfig {
    // Enable/disable features
    bool enable_pixel_normalization = true;
    bool enable_aa_normalization = true;
    bool enable_color_normalization = true;
    bool enable_precision_normalization = true;

    // Normalization parameters
    NormalizationMode mode = NormalizationMode::Deterministic;
    uint64_t seed = 0;                    // Deterministic seed
    double intensity = 0.02;              // Noise intensity (0-1)
    int max_delta = 4;                    // Max pixel value change

    // Anti-aliasing normalization
    AAMode target_aa = AAMode::MSAA_4x;
    bool smooth_edges = true;

    // Color space
    bool normalize_gamma = true;
    float target_gamma = 2.2f;
    bool normalize_color_primaries = true;

    // Precision
    bool round_to_8bit = true;            // Round float to 8-bit
    bool clamp_values = true;             // Clamp to valid range
};

/**
 * Pixel data structure for processing
 */
struct PixelData {
    void* data = nullptr;
    size_t width = 0;
    size_t height = 0;
    size_t stride = 0;  // bytes per row
    PixelFormat format = PixelFormat::RGBA8;
    bool owns_data = false;

    PixelData() = default;
    PixelData(void* d, size_t w, size_t h, PixelFormat f);
    PixelData(size_t w, size_t h, PixelFormat f);  // Allocates
    ~PixelData();

    // Move only
    PixelData(PixelData&& other) noexcept;
    PixelData& operator=(PixelData&& other) noexcept;
    PixelData(const PixelData&) = delete;
    PixelData& operator=(const PixelData&) = delete;

    size_t GetPixelSize() const;
    size_t GetTotalSize() const;
};

/**
 * Render Normalizer
 *
 * Main class for normalizing render output
 */
class RenderNormalizer {
public:
    RenderNormalizer();
    ~RenderNormalizer();

    // Non-copyable
    RenderNormalizer(const RenderNormalizer&) = delete;
    RenderNormalizer& operator=(const RenderNormalizer&) = delete;

    // ==================== Configuration ====================

    /**
     * Set normalization configuration
     */
    void SetConfig(const RenderNormalizationConfig& config) { config_ = config; }
    const RenderNormalizationConfig& GetConfig() const { return config_; }

    // ==================== Main Normalization ====================

    /**
     * Normalize pixel data in-place
     */
    void Normalize(PixelData& pixels, const GPUProfile& profile);

    /**
     * Normalize pixel data in-place using raw parameters
     */
    void Normalize(void* pixels, size_t width, size_t height,
                   PixelFormat format, uint64_t seed);

    /**
     * Normalize glReadPixels result
     */
    void NormalizeReadPixels(void* pixels, int32_t x, int32_t y,
                             int32_t width, int32_t height,
                             uint32_t gl_format, uint32_t gl_type,
                             const GPUProfile& profile);

    // ==================== Individual Transformations ====================

    /**
     * Apply deterministic noise based on seed
     */
    void ApplyDeterministicNoise(PixelData& pixels, uint64_t seed, double intensity);

    /**
     * Apply uniform noise (same delta to all pixels)
     */
    void ApplyUniformNoise(PixelData& pixels, int delta);

    /**
     * Apply gradient-based noise
     */
    void ApplyGradientNoise(PixelData& pixels, uint64_t seed);

    /**
     * Normalize anti-aliasing artifacts
     */
    void NormalizeAntiAliasing(PixelData& pixels, AAMode target_mode);

    /**
     * Normalize color space
     */
    void NormalizeColorSpace(PixelData& pixels, float target_gamma);

    /**
     * Normalize floating-point precision
     */
    void NormalizePrecision(PixelData& pixels, int mantissa_bits);

    /**
     * Round values to match target GPU behavior
     */
    void RoundValues(PixelData& pixels, int bits_per_channel);

    // ==================== Hash Generation ====================

    /**
     * Generate deterministic hash from pixel data
     * This produces a consistent hash that matches the target GPU profile
     */
    uint64_t GenerateHash(const PixelData& pixels, uint64_t seed);

    /**
     * Generate hash matching a specific profile
     */
    uint64_t GenerateProfileHash(const PixelData& pixels, const GPUProfile& profile);

    /**
     * Generate WebGL-style canvas fingerprint hash
     */
    std::string GenerateCanvasFingerprint(const PixelData& pixels, uint64_t seed);

    // ==================== Edge Detection ====================

    /**
     * Detect edges in image (for AA normalization)
     */
    PixelData DetectEdges(const PixelData& pixels);

    /**
     * Smooth edges to normalize AA differences
     */
    void SmoothEdges(PixelData& pixels, const PixelData& edge_mask);

    // ==================== Utilities ====================

    /**
     * Convert GL format/type to PixelFormat
     */
    static PixelFormat GLToPixelFormat(uint32_t gl_format, uint32_t gl_type);

    /**
     * Convert PixelFormat to GL format/type
     */
    static void PixelFormatToGL(PixelFormat format, uint32_t& gl_format, uint32_t& gl_type);

    /**
     * Get bytes per pixel for format
     */
    static size_t GetBytesPerPixel(PixelFormat format);

    // ==================== Statistics ====================

    struct NormalizerStats {
        uint64_t pixels_normalized = 0;
        uint64_t bytes_processed = 0;
        uint64_t noise_applications = 0;
        uint64_t aa_normalizations = 0;
        uint64_t color_normalizations = 0;
        uint64_t hashes_generated = 0;
    };

    NormalizerStats GetStats() const;
    void ResetStats();

private:
    // Internal helpers
    void ApplyNoiseRGBA8(uint8_t* pixels, size_t width, size_t height,
                         size_t stride, uint64_t seed, double intensity, int max_delta);
    void ApplyNoiseRGBA16F(uint16_t* pixels, size_t width, size_t height,
                           size_t stride, uint64_t seed, double intensity);
    void ApplyNoiseRGBA32F(float* pixels, size_t width, size_t height,
                           size_t stride, uint64_t seed, double intensity);

    // Hash function (murmur-like)
    static uint64_t HashPixel(uint64_t seed, size_t x, size_t y, size_t channel);
    static uint64_t HashCombine(uint64_t h1, uint64_t h2);

    // Fast deterministic random
    static int DeterministicRandom(uint64_t seed, int min, int max);
    static float DeterministicRandomFloat(uint64_t seed);

    RenderNormalizationConfig config_;

    mutable std::mutex stats_mutex_;
    NormalizerStats stats_;
};

/**
 * Canvas Fingerprint Generator
 *
 * Generates consistent canvas fingerprints matching target profiles
 */
class CanvasFingerprintGenerator {
public:
    /**
     * Generate a canvas fingerprint hash
     */
    static std::string Generate(const PixelData& pixels, uint64_t profile_seed);

    /**
     * Generate a WebGL render hash
     */
    static std::string GenerateWebGLHash(const PixelData& pixels, const GPUProfile& profile);

    /**
     * Generate a 2D canvas hash
     */
    static std::string GenerateCanvas2DHash(const PixelData& pixels, const GPUProfile& profile);

    /**
     * Compare two fingerprints for similarity
     */
    static double CompareSimilarity(const std::string& fp1, const std::string& fp2);

private:
    static uint64_t HashPixels(const uint8_t* pixels, size_t size, uint64_t seed);
    static std::string HashToString(uint64_t hash);
};

/**
 * Frame Buffer Processor
 *
 * Processes entire framebuffers for normalization
 */
class FrameBufferProcessor {
public:
    /**
     * Process a framebuffer before presenting
     */
    static void ProcessFrameBuffer(void* pixels, size_t width, size_t height,
                                    PixelFormat format, const GPUProfile& profile);

    /**
     * Process WebGL readPixels result
     */
    static void ProcessReadPixels(void* pixels, int32_t x, int32_t y,
                                   int32_t width, int32_t height,
                                   uint32_t gl_format, uint32_t gl_type,
                                   const GPUProfile& profile);

    /**
     * Process toDataURL output
     */
    static void ProcessDataURL(std::string& data_url, const GPUProfile& profile);
};

} // namespace gpu
} // namespace owl
