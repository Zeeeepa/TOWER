#pragma once

/**
 * OWL GPU Virtualization System
 *
 * This module provides hardware-level GPU virtualization to create undetectable
 * browser fingerprints. Unlike JavaScript-level spoofing, this operates at the
 * GPU command level within the Chromium GPU process.
 *
 * Key Features:
 * - Complete GPU identity spoofing (vendor, renderer, capabilities)
 * - Render output normalization (deterministic pixel transforms)
 * - Shader precision emulation (match target GPU behavior)
 * - Timing attack mitigation (DrawnApart defense)
 *
 * Architecture:
 *
 *   WebGL API → GPU Process → [OWL GPU Virtualization] → ANGLE → Real GPU
 *                                      ↓
 *                              GPU Profile Manager
 *                                      ↓
 *                              Render Normalizer
 *
 * The system intercepts GPU commands at the ANGLE boundary, applies profile-based
 * transformations, and produces consistent fingerprints regardless of the actual
 * hardware.
 */

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <atomic>
#include <map>

namespace owl {
namespace gpu {

// Forward declarations
class GPUProfile;
class GPUContext;
class GLInterceptor;
class ShaderTranslator;
class RenderNormalizer;
class TimingNormalizer;

/**
 * GPU Vendor identification
 */
enum class GPUVendor : uint8_t {
    Unknown = 0,
    Intel,
    NVIDIA,
    AMD,
    Apple,
    Qualcomm,
    ARM,
    Google,  // SwiftShader
    Mesa     // Software renderer
};

/**
 * GPU Architecture generation
 */
enum class GPUArchitecture : uint8_t {
    Unknown = 0,
    // Intel
    Intel_Gen9,      // Skylake, Kaby Lake (UHD 620, etc.)
    Intel_Gen11,     // Ice Lake
    Intel_Gen12,     // Tiger Lake, Xe
    Intel_Arc,       // Alchemist, Battlemage
    // NVIDIA
    NVIDIA_Pascal,   // GTX 10xx
    NVIDIA_Turing,   // RTX 20xx, GTX 16xx
    NVIDIA_Ampere,   // RTX 30xx
    NVIDIA_Ada,      // RTX 40xx
    NVIDIA_Blackwell,// RTX 50xx
    // AMD
    AMD_GCN,         // RX 400/500
    AMD_RDNA,        // RX 5000
    AMD_RDNA2,       // RX 6000
    AMD_RDNA3,       // RX 7000
    AMD_RDNA4,       // RX 9000
    // Apple
    Apple_M1,
    Apple_M2,
    Apple_M3,
    Apple_M4
};

/**
 * Floating-point precision mode
 */
enum class PrecisionMode : uint8_t {
    HighP,      // highp - 32-bit float
    MediumP,    // mediump - typically 16-bit float
    LowP        // lowp - typically 10-bit float
};

/**
 * Anti-aliasing mode for normalization
 */
enum class AAMode : uint8_t {
    None,
    MSAA_2x,
    MSAA_4x,
    MSAA_8x,
    FXAA,
    TAA
};

/**
 * GPU Virtualization configuration
 */
struct GPUVirtualizationConfig {
    // Enable/disable components
    bool enable_parameter_spoofing = true;    // Spoof getParameter results
    bool enable_shader_translation = true;    // Translate shaders for precision
    bool enable_render_normalization = true;  // Normalize pixel output
    bool enable_timing_normalization = true;  // Mask timing characteristics

    // Render normalization settings
    bool apply_deterministic_noise = true;    // Apply seed-based noise
    double noise_intensity = 0.02;            // Noise strength (0-1)
    bool normalize_antialiasing = true;       // Normalize AA differences
    bool normalize_color_space = true;        // Normalize color space conversions

    // Timing normalization settings
    uint32_t timing_quantum_us = 100;         // Quantize to 100μs
    bool add_timing_jitter = true;            // Add random jitter
    double max_jitter_ratio = 0.05;           // Max 5% jitter

    // Shader translation settings
    bool normalize_precision = true;          // Normalize float precision
    bool emulate_gpu_quirks = true;           // Emulate specific GPU behavior

    // Debug settings
    bool log_intercepted_calls = false;       // Log all intercepted GL calls
    bool log_shader_translations = false;     // Log shader modifications
};

/**
 * Shader precision format (matches WebGL getShaderPrecisionFormat)
 */
struct ShaderPrecisionFormat {
    int32_t range_min;      // Minimum representable value (log2)
    int32_t range_max;      // Maximum representable value (log2)
    int32_t precision;      // Number of bits of precision

    bool operator==(const ShaderPrecisionFormat& other) const {
        return range_min == other.range_min &&
               range_max == other.range_max &&
               precision == other.precision;
    }
};

/**
 * GPU Capabilities - all queryable WebGL parameters
 *
 * NOTE ON VERSION STRINGS:
 * The 'version' and 'shading_language' fields are for JavaScript WebGL API spoofing ONLY!
 * They contain WebGL version strings like "WebGL 1.0 (OpenGL ES 2.0 Chromium)".
 *
 * DO NOT use these for native glGetString(GL_VERSION) calls!
 * ANGLE must return the correct OpenGL ES version for each context type:
 * - ES 2.0 for WebGL1 contexts
 * - ES 3.0 for WebGL2 contexts
 *
 * Spoofing native GL_VERSION breaks WebGL1 context creation because Chromium
 * validates that the version matches the requested context type.
 */
struct GPUCapabilities {
    // Basic info - used for JavaScript WebGL API spoofing
    std::string vendor;              // GL_VENDOR (masked browser value, e.g., "Google Inc. (NVIDIA)")
    std::string renderer;            // GL_RENDERER (masked browser value, e.g., "ANGLE (...)")
    // IMPORTANT: version/shading_language are for JS WebGL API only, NOT native GL!
    std::string version;             // WebGL version for JS API (e.g., "WebGL 1.0 (...)")
    std::string shading_language;    // WebGL GLSL for JS API (e.g., "WebGL GLSL ES 1.0 (...)")
    std::string unmasked_vendor;     // UNMASKED_VENDOR_WEBGL (raw GPU vendor name)
    std::string unmasked_renderer;   // UNMASKED_RENDERER_WEBGL (raw GPU model name)

    // Texture limits
    int32_t max_texture_size = 16384;
    int32_t max_cube_map_texture_size = 16384;
    int32_t max_render_buffer_size = 16384;
    int32_t max_texture_image_units = 16;
    int32_t max_combined_texture_image_units = 32;
    int32_t max_vertex_texture_image_units = 16;

    // Shader limits
    int32_t max_vertex_attribs = 16;
    int32_t max_vertex_uniform_vectors = 4096;
    int32_t max_varying_vectors = 32;
    int32_t max_fragment_uniform_vectors = 1024;

    // Viewport limits
    int32_t max_viewport_dims[2] = {32768, 32768};
    float aliased_line_width_range[2] = {1.0f, 1.0f};
    float aliased_point_size_range[2] = {1.0f, 1024.0f};

    // Antialiasing / Multisampling
    int32_t max_samples = 8;
    int32_t samples = 4;        // GL_SAMPLES - actual samples in current FB (critical for VM detection!)
    int32_t sample_buffers = 1; // GL_SAMPLE_BUFFERS - 1 if multisampling enabled (critical for VM detection!)
    float max_texture_max_anisotropy = 16.0f;

    // Shader precision (vertex shader)
    ShaderPrecisionFormat vs_high_float = {127, 127, 23};
    ShaderPrecisionFormat vs_medium_float = {127, 127, 23};
    ShaderPrecisionFormat vs_low_float = {127, 127, 23};
    ShaderPrecisionFormat vs_high_int = {31, 30, 0};
    ShaderPrecisionFormat vs_medium_int = {31, 30, 0};
    ShaderPrecisionFormat vs_low_int = {31, 30, 0};

    // Shader precision (fragment shader)
    ShaderPrecisionFormat fs_high_float = {127, 127, 23};
    ShaderPrecisionFormat fs_medium_float = {127, 127, 23};
    ShaderPrecisionFormat fs_low_float = {127, 127, 23};
    ShaderPrecisionFormat fs_high_int = {31, 30, 0};
    ShaderPrecisionFormat fs_medium_int = {31, 30, 0};
    ShaderPrecisionFormat fs_low_int = {31, 30, 0};

    // Extensions
    std::vector<std::string> extensions;
    std::vector<std::string> webgl2_extensions;

    // WebGL2 specific
    int32_t max_3d_texture_size = 2048;
    int32_t max_array_texture_layers = 2048;
    int32_t max_color_attachments = 8;
    int32_t max_draw_buffers = 8;
    int32_t max_uniform_buffer_bindings = 72;
    int32_t max_uniform_block_size = 65536;
    int32_t max_transform_feedback_separate_attribs = 4;

    // Shader precision mode (for shader translation)
    PrecisionMode vertex_shader_precision = PrecisionMode::HighP;
    PrecisionMode fragment_shader_precision = PrecisionMode::HighP;
};

/**
 * GPU rendering behavior profile
 */
struct GPURenderBehavior {
    // Floating-point behavior
    bool flush_denormals = false;           // Treat denormals as zero
    bool precise_sqrt = true;               // Use precise sqrt
    bool precise_divide = true;             // Use precise division

    // Rounding mode
    enum class RoundingMode {
        Nearest,
        TowardZero,
        TowardPositive,
        TowardNegative
    } rounding_mode = RoundingMode::Nearest;

    // Color space handling
    bool srgb_decode_accurate = true;       // Accurate sRGB decode
    bool linear_blending = true;            // Linear color blending

    // Texture filtering
    bool anisotropic_filtering_quality = true;
    float texture_lod_bias = 0.0f;

    // Anti-aliasing characteristics
    AAMode default_aa_mode = AAMode::MSAA_4x;
    bool alpha_to_coverage_dithering = false;

    // Depth buffer
    bool reverse_depth = false;
    float depth_bias_constant = 0.0f;
    float depth_bias_slope = 0.0f;

    // Async compute
    bool has_async_compute = false;
};

/**
 * Main GPU Virtualization System
 *
 * This is the primary interface for GPU virtualization. It manages GPU contexts,
 * intercepts GL calls, and applies profile-based transformations.
 */
class GPUVirtualizationSystem {
public:
    /**
     * Get the singleton instance
     */
    static GPUVirtualizationSystem& Instance();

    /**
     * Initialize the virtualization system
     */
    bool Initialize(const GPUVirtualizationConfig& config = {});

    /**
     * Shutdown and cleanup
     */
    void Shutdown();

    /**
     * Check if system is initialized
     */
    bool IsInitialized() const { return initialized_.load(); }

    /**
     * Get current configuration
     */
    const GPUVirtualizationConfig& GetConfig() const { return config_; }

    /**
     * Update configuration (some changes require context recreation)
     */
    void UpdateConfig(const GPUVirtualizationConfig& config);

    // ==================== Context Management ====================

    /**
     * Create a virtualized GPU context with the specified profile
     */
    std::shared_ptr<GPUContext> CreateContext(const GPUProfile& profile);

    /**
     * Create a virtualized GPU context by profile ID
     */
    std::shared_ptr<GPUContext> CreateContext(const std::string& profile_id);

    /**
     * Get the current active context for this thread
     */
    GPUContext* GetCurrentContext();

    /**
     * Make a context current for this thread
     */
    void MakeContextCurrent(GPUContext* context);

    // ==================== GL Interception ====================

    /**
     * Get interceptor for GL call hooking
     */
    GLInterceptor& GetInterceptor() { return *interceptor_; }

    /**
     * Register custom GL call handler
     */
    using GLCallHandler = std::function<bool(uint32_t call_id, void* args)>;
    void RegisterGLHandler(uint32_t call_id, GLCallHandler handler);

    // ==================== Shader Translation ====================

    /**
     * Get shader translator
     */
    ShaderTranslator& GetShaderTranslator() { return *shader_translator_; }

    // ==================== Render Normalization ====================

    /**
     * Get render normalizer
     */
    RenderNormalizer& GetRenderNormalizer() { return *render_normalizer_; }

    // ==================== Timing Normalization ====================

    /**
     * Get timing normalizer
     */
    TimingNormalizer& GetTimingNormalizer() { return *timing_normalizer_; }

    // ==================== Profile Management ====================

    /**
     * Get a GPU profile by ID
     */
    const GPUProfile* GetProfile(const std::string& id) const;

    /**
     * Get all available profile IDs
     */
    std::vector<std::string> GetProfileIds() const;

    /**
     * Create a profile from capabilities
     */
    std::shared_ptr<GPUProfile> CreateProfile(const std::string& id,
                                               const GPUCapabilities& caps,
                                               const GPURenderBehavior& behavior);

    // ==================== Statistics ====================

    struct Statistics {
        uint64_t gl_calls_intercepted = 0;
        uint64_t parameters_spoofed = 0;
        uint64_t shaders_translated = 0;
        uint64_t pixels_normalized = 0;
        uint64_t timing_normalizations = 0;
    };

    Statistics GetStatistics() const;
    void ResetStatistics();

private:
    GPUVirtualizationSystem();
    ~GPUVirtualizationSystem();

    // Non-copyable
    GPUVirtualizationSystem(const GPUVirtualizationSystem&) = delete;
    GPUVirtualizationSystem& operator=(const GPUVirtualizationSystem&) = delete;

    // Load GPU profiles from VM database
    void LoadProfiles();

    std::atomic<bool> initialized_{false};
    GPUVirtualizationConfig config_;

    std::unique_ptr<GLInterceptor> interceptor_;
    std::unique_ptr<ShaderTranslator> shader_translator_;
    std::unique_ptr<RenderNormalizer> render_normalizer_;
    std::unique_ptr<TimingNormalizer> timing_normalizer_;

    mutable std::mutex profiles_mutex_;
    std::map<std::string, std::shared_ptr<GPUProfile>> profiles_;

    mutable std::mutex contexts_mutex_;
    std::vector<std::weak_ptr<GPUContext>> contexts_;

    thread_local static GPUContext* current_context_;

    mutable std::mutex stats_mutex_;
    Statistics stats_;
};

/**
 * Convenience function to get the GPU virtualization system
 */
inline GPUVirtualizationSystem& GetGPUVirtualization() {
    return GPUVirtualizationSystem::Instance();
}

} // namespace gpu
} // namespace owl
