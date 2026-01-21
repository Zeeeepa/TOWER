#pragma once

/**
 * OWL GPU Context
 *
 * Manages a virtualized GPU context. Each browser context (tab) can have its own
 * GPU context with a specific profile, allowing different fingerprints per context.
 */

#include "gpu/owl_gpu_virtualization.h"
#include "gpu/owl_gpu_profile.h"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <atomic>

namespace owl {
namespace gpu {

/**
 * GL State tracking for accurate emulation
 */
struct GLState {
    // Current bound objects
    uint32_t current_program = 0;
    uint32_t current_vao = 0;
    uint32_t current_fbo = 0;
    uint32_t current_texture_2d = 0;
    uint32_t current_texture_cube = 0;
    uint32_t current_array_buffer = 0;
    uint32_t current_element_buffer = 0;

    // Viewport state
    int32_t viewport[4] = {0, 0, 800, 600};
    int32_t scissor[4] = {0, 0, 800, 600};
    bool scissor_test = false;

    // Blend state
    bool blend_enabled = false;
    uint32_t blend_src_rgb = 1;      // GL_ONE
    uint32_t blend_dst_rgb = 0;      // GL_ZERO
    uint32_t blend_src_alpha = 1;
    uint32_t blend_dst_alpha = 0;
    uint32_t blend_equation_rgb = 0x8006;   // GL_FUNC_ADD
    uint32_t blend_equation_alpha = 0x8006;

    // Depth state
    bool depth_test = false;
    bool depth_write = true;
    uint32_t depth_func = 0x0201;    // GL_LESS

    // Stencil state
    bool stencil_test = false;

    // Culling state
    bool cull_face = false;
    uint32_t cull_mode = 0x0405;     // GL_BACK
    uint32_t front_face = 0x0901;    // GL_CCW

    // Clear values
    float clear_color[4] = {0, 0, 0, 0};
    float clear_depth = 1.0f;
    int32_t clear_stencil = 0;
};

/**
 * Shader object tracking
 */
struct ShaderObject {
    uint32_t id;
    uint32_t type;  // GL_VERTEX_SHADER or GL_FRAGMENT_SHADER
    std::string original_source;
    std::string translated_source;
    bool is_compiled = false;
};

/**
 * Program object tracking
 */
struct ProgramObject {
    uint32_t id;
    std::vector<uint32_t> attached_shaders;
    bool is_linked = false;
    std::unordered_map<std::string, int32_t> uniform_locations;
    std::unordered_map<std::string, int32_t> attrib_locations;
};

/**
 * Texture object tracking
 */
struct TextureObject {
    uint32_t id;
    uint32_t target;  // GL_TEXTURE_2D, GL_TEXTURE_CUBE_MAP, etc.
    int32_t width = 0;
    int32_t height = 0;
    uint32_t internal_format = 0;
    bool is_renderbuffer = false;
};

/**
 * Framebuffer object tracking
 */
struct FramebufferObject {
    uint32_t id;
    std::unordered_map<uint32_t, uint32_t> attachments;  // attachment point -> texture/rb id
    int32_t width = 0;
    int32_t height = 0;
};

/**
 * GPU Context
 *
 * Represents a virtualized GPU context with a specific profile.
 * Tracks all GL state and provides the interface for GL call interception.
 */
class GPUContext : public std::enable_shared_from_this<GPUContext> {
public:
    /**
     * Create a new GPU context with the given profile
     */
    explicit GPUContext(std::shared_ptr<GPUProfile> profile);
    ~GPUContext();

    // Non-copyable
    GPUContext(const GPUContext&) = delete;
    GPUContext& operator=(const GPUContext&) = delete;

    // ==================== Profile Access ====================

    /**
     * Get the GPU profile for this context
     */
    const GPUProfile& GetProfile() const { return *profile_; }
    std::shared_ptr<GPUProfile> GetProfilePtr() const { return profile_; }

    /**
     * Get capabilities (convenience)
     */
    const GPUCapabilities& GetCapabilities() const { return profile_->GetCapabilities(); }

    /**
     * Get render behavior (convenience)
     */
    const GPURenderBehavior& GetRenderBehavior() const { return profile_->GetRenderBehavior(); }

    // ==================== Context ID ====================

    /**
     * Get unique context ID
     */
    uint64_t GetContextId() const { return context_id_; }

    // ==================== GL State ====================

    /**
     * Get current GL state
     */
    const GLState& GetState() const { return state_; }
    GLState& GetStateMutable() { return state_; }

    // ==================== Object Tracking ====================

    /**
     * Track shader creation
     */
    void TrackShader(uint32_t id, uint32_t type);
    ShaderObject* GetShader(uint32_t id);
    void RemoveShader(uint32_t id);

    /**
     * Track program creation
     */
    void TrackProgram(uint32_t id);
    ProgramObject* GetProgram(uint32_t id);
    void RemoveProgram(uint32_t id);

    /**
     * Track texture creation
     */
    void TrackTexture(uint32_t id, uint32_t target);
    TextureObject* GetTexture(uint32_t id);
    void RemoveTexture(uint32_t id);

    /**
     * Track framebuffer creation
     */
    void TrackFramebuffer(uint32_t id);
    FramebufferObject* GetFramebuffer(uint32_t id);
    void RemoveFramebuffer(uint32_t id);

    // ==================== Parameter Queries ====================

    /**
     * Get a spoofed GL parameter value
     * Returns true if parameter was spoofed, false to use real value
     */
    bool GetSpoofedParameter(uint32_t pname, void* params, size_t param_size);

    /**
     * Get a spoofed GL string value
     */
    const char* GetSpoofedString(uint32_t name);

    /**
     * Get spoofed shader precision format
     */
    bool GetSpoofedShaderPrecision(uint32_t shader_type, uint32_t precision_type,
                                    int32_t* range, int32_t* precision);

    /**
     * Get spoofed extensions list
     */
    const std::vector<std::string>& GetSpoofedExtensions(bool webgl2 = false);

    // ==================== Render Normalization ====================

    /**
     * Apply render normalization to pixel data
     */
    void NormalizePixels(void* pixels, size_t width, size_t height,
                         uint32_t format, uint32_t type);

    /**
     * Get deterministic hash for this context's render output
     */
    uint64_t GetRenderHash(const void* pixels, size_t size);

    /**
     * Get deterministic seed for pixel normalization
     * This ensures consistent noise across renders for the same profile
     */
    uint64_t GetNormalizationSeed() const;

    // ==================== Timing ====================

    /**
     * Apply timing normalization
     * Returns adjusted time that should be reported
     */
    uint64_t NormalizeTiming(uint64_t real_time_ns, const char* operation);

    /**
     * Record operation start time
     */
    void BeginTimedOperation(const char* operation);

    /**
     * Record operation end time and get normalized duration
     */
    uint64_t EndTimedOperation(const char* operation);

    // ==================== Statistics ====================

    struct ContextStats {
        uint64_t draw_calls = 0;
        uint64_t shader_compilations = 0;
        uint64_t texture_uploads = 0;
        uint64_t parameter_queries = 0;
        uint64_t pixels_normalized = 0;
    };

    const ContextStats& GetStats() const { return stats_; }

private:
    // Initialize parameter lookup tables
    void InitializeParameterTables();

    // Generate parameter value from profile
    void GenerateParameterValue(uint32_t pname, void* params, size_t param_size);

    // Profile
    std::shared_ptr<GPUProfile> profile_;

    // Unique ID
    uint64_t context_id_;
    static std::atomic<uint64_t> next_context_id_;

    // GL state
    GLState state_;

    // Object tracking
    mutable std::mutex objects_mutex_;
    std::unordered_map<uint32_t, ShaderObject> shaders_;
    std::unordered_map<uint32_t, ProgramObject> programs_;
    std::unordered_map<uint32_t, TextureObject> textures_;
    std::unordered_map<uint32_t, FramebufferObject> framebuffers_;

    // Timing tracking
    struct TimingEntry {
        uint64_t start_time_ns;
        std::string operation;
    };
    std::unordered_map<std::string, TimingEntry> active_timings_;
    mutable std::mutex timing_mutex_;

    // Statistics
    mutable ContextStats stats_;

    // Parameter lookup table (pname -> lambda to get value)
    std::unordered_map<uint32_t, std::function<void(void*, size_t)>> parameter_handlers_;

    // String lookup table
    std::unordered_map<uint32_t, std::string> string_cache_;
};

/**
 * Context manager for thread-local context tracking
 */
class GPUContextManager {
public:
    static GPUContextManager& Instance();

    /**
     * Set current context for this thread
     */
    void SetCurrentContext(GPUContext* context);

    /**
     * Get current context for this thread
     */
    GPUContext* GetCurrentContext();

    /**
     * Clear current context for this thread
     */
    void ClearCurrentContext();

private:
    GPUContextManager() = default;
    thread_local static GPUContext* current_context_;
};

} // namespace gpu
} // namespace owl
