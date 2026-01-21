/**
 * OWL GPU Context Implementation
 *
 * Manages virtualized GPU contexts with profile-based parameter spoofing.
 */

#include "gpu/owl_gpu_context.h"
#include <cstring>
#include <chrono>
#include <algorithm>

namespace owl {
namespace gpu {

// GL constant definitions
namespace gl {
    // String queries
    constexpr uint32_t VENDOR = 0x1F00;
    constexpr uint32_t RENDERER = 0x1F01;
    // NOTE: VERSION (0x1F02) and SHADING_LANGUAGE_VERSION (0x8B8C) are intentionally
    // NOT defined here - we do NOT spoof these values at the native GL level.
    // ANGLE must return the correct OpenGL ES version for each context type.
    constexpr uint32_t EXTENSIONS = 0x1F03;

    // Debug info extension
    constexpr uint32_t UNMASKED_VENDOR_WEBGL = 0x9245;
    constexpr uint32_t UNMASKED_RENDERER_WEBGL = 0x9246;

    // Integer parameters (WebGL1)
    constexpr uint32_t MAX_TEXTURE_SIZE = 0x0D33;
    constexpr uint32_t MAX_CUBE_MAP_TEXTURE_SIZE = 0x851C;
    constexpr uint32_t MAX_RENDERBUFFER_SIZE = 0x84E8;
    constexpr uint32_t MAX_VERTEX_ATTRIBS = 0x8869;
    constexpr uint32_t MAX_VERTEX_UNIFORM_VECTORS = 0x8DFB;
    constexpr uint32_t MAX_VERTEX_TEXTURE_IMAGE_UNITS = 0x8B4C;
    constexpr uint32_t MAX_VARYING_VECTORS = 0x8DFC;
    constexpr uint32_t MAX_FRAGMENT_UNIFORM_VECTORS = 0x8DFD;
    constexpr uint32_t MAX_TEXTURE_IMAGE_UNITS = 0x8872;
    constexpr uint32_t MAX_COMBINED_TEXTURE_IMAGE_UNITS = 0x8B4D;
    constexpr uint32_t MAX_VIEWPORT_DIMS = 0x0D3A;
    constexpr uint32_t MAX_SAMPLES = 0x8D57;

    // Integer parameters (WebGL2 specific)
    constexpr uint32_t MAX_3D_TEXTURE_SIZE = 0x8073;
    constexpr uint32_t MAX_ARRAY_TEXTURE_LAYERS = 0x88FF;
    constexpr uint32_t MAX_COLOR_ATTACHMENTS = 0x8CDF;
    constexpr uint32_t MAX_DRAW_BUFFERS = 0x8824;
    constexpr uint32_t MAX_UNIFORM_BUFFER_BINDINGS = 0x8A2F;
    constexpr uint32_t MAX_UNIFORM_BLOCK_SIZE = 0x8A30;
    constexpr uint32_t MAX_COMBINED_UNIFORM_BLOCKS = 0x8A2E;
    constexpr uint32_t MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS = 0x8C8B;

    // Float parameters
    constexpr uint32_t ALIASED_LINE_WIDTH_RANGE = 0x846E;
    constexpr uint32_t ALIASED_POINT_SIZE_RANGE = 0x8460;
    constexpr uint32_t MAX_TEXTURE_MAX_ANISOTROPY = 0x84FF;

    // Shader types
    constexpr uint32_t VERTEX_SHADER = 0x8B31;
    constexpr uint32_t FRAGMENT_SHADER = 0x8B30;

    // Precision types
    constexpr uint32_t LOW_FLOAT = 0x8DF0;
    constexpr uint32_t MEDIUM_FLOAT = 0x8DF1;
    constexpr uint32_t HIGH_FLOAT = 0x8DF2;
    constexpr uint32_t LOW_INT = 0x8DF3;
    constexpr uint32_t MEDIUM_INT = 0x8DF4;
    constexpr uint32_t HIGH_INT = 0x8DF5;
}

// Static member initialization
std::atomic<uint64_t> GPUContext::next_context_id_{1};
thread_local GPUContext* GPUContextManager::current_context_ = nullptr;

// ============================================================================
// GPUContext Implementation
// ============================================================================

GPUContext::GPUContext(std::shared_ptr<GPUProfile> profile)
    : profile_(std::move(profile))
    , context_id_(next_context_id_.fetch_add(1)) {
    InitializeParameterTables();
}

GPUContext::~GPUContext() {
    // Cleanup any remaining tracked objects
    std::lock_guard<std::mutex> lock(objects_mutex_);
    shaders_.clear();
    programs_.clear();
    textures_.clear();
    framebuffers_.clear();
}

void GPUContext::InitializeParameterTables() {
    const auto& caps = profile_->GetCapabilities();

    // Integer parameters
    parameter_handlers_[gl::MAX_TEXTURE_SIZE] = [&caps](void* p, size_t) {
        *static_cast<int32_t*>(p) = caps.max_texture_size;
    };
    parameter_handlers_[gl::MAX_CUBE_MAP_TEXTURE_SIZE] = [&caps](void* p, size_t) {
        *static_cast<int32_t*>(p) = caps.max_cube_map_texture_size;
    };
    parameter_handlers_[gl::MAX_RENDERBUFFER_SIZE] = [&caps](void* p, size_t) {
        *static_cast<int32_t*>(p) = caps.max_render_buffer_size;
    };
    parameter_handlers_[gl::MAX_VERTEX_ATTRIBS] = [&caps](void* p, size_t) {
        *static_cast<int32_t*>(p) = caps.max_vertex_attribs;
    };
    parameter_handlers_[gl::MAX_VERTEX_UNIFORM_VECTORS] = [&caps](void* p, size_t) {
        *static_cast<int32_t*>(p) = caps.max_vertex_uniform_vectors;
    };
    parameter_handlers_[gl::MAX_VERTEX_TEXTURE_IMAGE_UNITS] = [&caps](void* p, size_t) {
        *static_cast<int32_t*>(p) = caps.max_vertex_texture_image_units;
    };
    parameter_handlers_[gl::MAX_VARYING_VECTORS] = [&caps](void* p, size_t) {
        *static_cast<int32_t*>(p) = caps.max_varying_vectors;
    };
    parameter_handlers_[gl::MAX_FRAGMENT_UNIFORM_VECTORS] = [&caps](void* p, size_t) {
        *static_cast<int32_t*>(p) = caps.max_fragment_uniform_vectors;
    };
    parameter_handlers_[gl::MAX_TEXTURE_IMAGE_UNITS] = [&caps](void* p, size_t) {
        *static_cast<int32_t*>(p) = caps.max_texture_image_units;
    };
    parameter_handlers_[gl::MAX_COMBINED_TEXTURE_IMAGE_UNITS] = [&caps](void* p, size_t) {
        *static_cast<int32_t*>(p) = caps.max_combined_texture_image_units;
    };
    parameter_handlers_[gl::MAX_VIEWPORT_DIMS] = [&caps](void* p, size_t) {
        auto* arr = static_cast<int32_t*>(p);
        arr[0] = caps.max_viewport_dims[0];
        arr[1] = caps.max_viewport_dims[1];
    };
    parameter_handlers_[gl::MAX_SAMPLES] = [&caps](void* p, size_t) {
        *static_cast<int32_t*>(p) = caps.max_samples;
    };

    // WebGL2 specific integer parameters
    parameter_handlers_[gl::MAX_3D_TEXTURE_SIZE] = [&caps](void* p, size_t) {
        *static_cast<int32_t*>(p) = caps.max_3d_texture_size;
    };
    parameter_handlers_[gl::MAX_ARRAY_TEXTURE_LAYERS] = [&caps](void* p, size_t) {
        *static_cast<int32_t*>(p) = caps.max_array_texture_layers;
    };
    parameter_handlers_[gl::MAX_COLOR_ATTACHMENTS] = [&caps](void* p, size_t) {
        *static_cast<int32_t*>(p) = caps.max_color_attachments;
    };
    parameter_handlers_[gl::MAX_DRAW_BUFFERS] = [&caps](void* p, size_t) {
        *static_cast<int32_t*>(p) = caps.max_draw_buffers;
    };
    parameter_handlers_[gl::MAX_UNIFORM_BUFFER_BINDINGS] = [&caps](void* p, size_t) {
        *static_cast<int32_t*>(p) = caps.max_uniform_buffer_bindings;
    };
    parameter_handlers_[gl::MAX_UNIFORM_BLOCK_SIZE] = [&caps](void* p, size_t) {
        *static_cast<int32_t*>(p) = caps.max_uniform_block_size;
    };
    parameter_handlers_[gl::MAX_COMBINED_UNIFORM_BLOCKS] = [](void* p, size_t) {
        // Apple M4 has 24 combined uniform blocks, same value for most modern GPUs
        *static_cast<int32_t*>(p) = 24;
    };
    parameter_handlers_[gl::MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS] = [&caps](void* p, size_t) {
        *static_cast<int32_t*>(p) = caps.max_transform_feedback_separate_attribs;
    };

    // Float parameters
    parameter_handlers_[gl::ALIASED_LINE_WIDTH_RANGE] = [&caps](void* p, size_t) {
        auto* arr = static_cast<float*>(p);
        arr[0] = caps.aliased_line_width_range[0];
        arr[1] = caps.aliased_line_width_range[1];
    };
    parameter_handlers_[gl::ALIASED_POINT_SIZE_RANGE] = [&caps](void* p, size_t) {
        auto* arr = static_cast<float*>(p);
        arr[0] = caps.aliased_point_size_range[0];
        arr[1] = caps.aliased_point_size_range[1];
    };
    parameter_handlers_[gl::MAX_TEXTURE_MAX_ANISOTROPY] = [&caps](void* p, size_t) {
        *static_cast<float*>(p) = caps.max_texture_max_anisotropy;
    };

    // Cache string values
    // NOTE: We do NOT cache GL_VERSION or GL_SHADING_LANGUAGE_VERSION!
    // ANGLE returns different versions for ES 2.0 (WebGL1) vs ES 3.0 (WebGL2) contexts.
    // If we always return a fixed version, WebGL1 context creation fails because
    // Chromium validates that the version matches the requested context type.
    // Let ANGLE return the correct version for each context type.
    string_cache_[gl::VENDOR] = caps.vendor;
    string_cache_[gl::RENDERER] = caps.renderer;
    // string_cache_[gl::VERSION] - NOT CACHED, let ANGLE return correct version
    // string_cache_[gl::SHADING_LANGUAGE_VERSION] - NOT CACHED, let ANGLE return correct version
    string_cache_[gl::UNMASKED_VENDOR_WEBGL] = caps.unmasked_vendor;
    string_cache_[gl::UNMASKED_RENDERER_WEBGL] = caps.unmasked_renderer;

    // Build extensions string
    std::string ext_str;
    for (size_t i = 0; i < caps.extensions.size(); ++i) {
        if (i > 0) ext_str += " ";
        ext_str += caps.extensions[i];
    }
    string_cache_[gl::EXTENSIONS] = ext_str;
}

bool GPUContext::GetSpoofedParameter(uint32_t pname, void* params, size_t param_size) {
    auto it = parameter_handlers_.find(pname);
    if (it != parameter_handlers_.end()) {
        it->second(params, param_size);
        stats_.parameter_queries++;
        return true;
    }
    return false;
}

const char* GPUContext::GetSpoofedString(uint32_t name) {
    auto it = string_cache_.find(name);
    if (it != string_cache_.end()) {
        stats_.parameter_queries++;
        return it->second.c_str();
    }
    return nullptr;
}

bool GPUContext::GetSpoofedShaderPrecision(uint32_t shader_type, uint32_t precision_type,
                                            int32_t* range, int32_t* precision) {
    const auto& caps = profile_->GetCapabilities();
    const ShaderPrecisionFormat* format = nullptr;

    if (shader_type == gl::VERTEX_SHADER) {
        switch (precision_type) {
            case gl::HIGH_FLOAT: format = &caps.vs_high_float; break;
            case gl::MEDIUM_FLOAT: format = &caps.vs_medium_float; break;
            case gl::LOW_FLOAT: format = &caps.vs_low_float; break;
            case gl::HIGH_INT: format = &caps.vs_high_int; break;
            case gl::MEDIUM_INT: format = &caps.vs_medium_int; break;
            case gl::LOW_INT: format = &caps.vs_low_int; break;
        }
    } else if (shader_type == gl::FRAGMENT_SHADER) {
        switch (precision_type) {
            case gl::HIGH_FLOAT: format = &caps.fs_high_float; break;
            case gl::MEDIUM_FLOAT: format = &caps.fs_medium_float; break;
            case gl::LOW_FLOAT: format = &caps.fs_low_float; break;
            case gl::HIGH_INT: format = &caps.fs_high_int; break;
            case gl::MEDIUM_INT: format = &caps.fs_medium_int; break;
            case gl::LOW_INT: format = &caps.fs_low_int; break;
        }
    }

    if (format) {
        range[0] = format->range_min;
        range[1] = format->range_max;
        *precision = format->precision;
        stats_.parameter_queries++;
        return true;
    }

    return false;
}

const std::vector<std::string>& GPUContext::GetSpoofedExtensions(bool webgl2) {
    const auto& caps = profile_->GetCapabilities();
    return webgl2 ? caps.webgl2_extensions : caps.extensions;
}

// ==================== Object Tracking ====================

void GPUContext::TrackShader(uint32_t id, uint32_t type) {
    std::lock_guard<std::mutex> lock(objects_mutex_);
    ShaderObject shader;
    shader.id = id;
    shader.type = type;
    shaders_[id] = shader;
}

ShaderObject* GPUContext::GetShader(uint32_t id) {
    std::lock_guard<std::mutex> lock(objects_mutex_);
    auto it = shaders_.find(id);
    return (it != shaders_.end()) ? &it->second : nullptr;
}

void GPUContext::RemoveShader(uint32_t id) {
    std::lock_guard<std::mutex> lock(objects_mutex_);
    shaders_.erase(id);
}

void GPUContext::TrackProgram(uint32_t id) {
    std::lock_guard<std::mutex> lock(objects_mutex_);
    ProgramObject program;
    program.id = id;
    programs_[id] = program;
}

ProgramObject* GPUContext::GetProgram(uint32_t id) {
    std::lock_guard<std::mutex> lock(objects_mutex_);
    auto it = programs_.find(id);
    return (it != programs_.end()) ? &it->second : nullptr;
}

void GPUContext::RemoveProgram(uint32_t id) {
    std::lock_guard<std::mutex> lock(objects_mutex_);
    programs_.erase(id);
}

void GPUContext::TrackTexture(uint32_t id, uint32_t target) {
    std::lock_guard<std::mutex> lock(objects_mutex_);
    TextureObject texture;
    texture.id = id;
    texture.target = target;
    textures_[id] = texture;
}

TextureObject* GPUContext::GetTexture(uint32_t id) {
    std::lock_guard<std::mutex> lock(objects_mutex_);
    auto it = textures_.find(id);
    return (it != textures_.end()) ? &it->second : nullptr;
}

void GPUContext::RemoveTexture(uint32_t id) {
    std::lock_guard<std::mutex> lock(objects_mutex_);
    textures_.erase(id);
}

void GPUContext::TrackFramebuffer(uint32_t id) {
    std::lock_guard<std::mutex> lock(objects_mutex_);
    FramebufferObject fbo;
    fbo.id = id;
    framebuffers_[id] = fbo;
}

FramebufferObject* GPUContext::GetFramebuffer(uint32_t id) {
    std::lock_guard<std::mutex> lock(objects_mutex_);
    auto it = framebuffers_.find(id);
    return (it != framebuffers_.end()) ? &it->second : nullptr;
}

void GPUContext::RemoveFramebuffer(uint32_t id) {
    std::lock_guard<std::mutex> lock(objects_mutex_);
    framebuffers_.erase(id);
}

// ==================== Render Normalization ====================

void GPUContext::NormalizePixels(void* pixels, size_t width, size_t height,
                                  uint32_t format, uint32_t type) {
    if (!pixels || width == 0 || height == 0) return;

    // Only handle RGBA8 for now (most common)
    if (format == 0x1908 && type == 0x1401) {  // GL_RGBA, GL_UNSIGNED_BYTE
        uint8_t* data = static_cast<uint8_t*>(pixels);
        uint64_t seed = profile_->GetRenderSeed();

        // Apply deterministic noise
        for (size_t i = 0; i < width * height * 4; i += 4) {
            if (data[i + 3] == 0) continue;  // Skip transparent pixels

            // Deterministic noise based on seed and position
            uint64_t pixel_hash = seed ^ (i * 2654435761ULL);
            int noise_r = static_cast<int>((pixel_hash % 9)) - 4;
            int noise_g = static_cast<int>(((pixel_hash >> 8) % 9)) - 4;
            int noise_b = static_cast<int>(((pixel_hash >> 16) % 9)) - 4;

            data[i] = static_cast<uint8_t>(std::max(0, std::min(255, data[i] + noise_r)));
            data[i + 1] = static_cast<uint8_t>(std::max(0, std::min(255, data[i + 1] + noise_g)));
            data[i + 2] = static_cast<uint8_t>(std::max(0, std::min(255, data[i + 2] + noise_b)));
        }

        stats_.pixels_normalized += width * height;
    }
}

uint64_t GPUContext::GetRenderHash(const void* pixels, size_t size) {
    if (!pixels || size == 0) return 0;

    // Simple hash combining pixel data with profile seed
    const uint8_t* data = static_cast<const uint8_t*>(pixels);
    uint64_t hash = profile_->GetRenderSeed();

    for (size_t i = 0; i < size; i += 4) {
        hash = (hash ^ data[i]) * 0xff51afd7ed558ccdULL;
    }

    return hash ^ (hash >> 33);
}

uint64_t GPUContext::GetNormalizationSeed() const {
    // Use profile's render seed combined with context ID for deterministic noise
    return profile_->GetRenderSeed() ^ (context_id_ * 0x9E3779B97F4A7C15ULL);
}

// ==================== Timing ====================

uint64_t GPUContext::NormalizeTiming(uint64_t real_time_ns, const char* operation) {
    const auto& timing = profile_->GetTimingProfile();

    // Quantize to timing quantum
    uint64_t quantum_ns = timing.min_frame_time_us * 1000ULL;
    uint64_t normalized = (real_time_ns / quantum_ns) * quantum_ns;

    // Add small jitter based on seed
    uint64_t jitter_seed = profile_->GetRenderSeed() ^ real_time_ns;
    uint64_t jitter = (jitter_seed % (quantum_ns / 10));
    normalized += jitter;

    return normalized;
}

void GPUContext::BeginTimedOperation(const char* operation) {
    std::lock_guard<std::mutex> lock(timing_mutex_);
    TimingEntry entry;
    entry.start_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    entry.operation = operation ? operation : "unknown";
    active_timings_[entry.operation] = entry;
}

uint64_t GPUContext::EndTimedOperation(const char* operation) {
    std::lock_guard<std::mutex> lock(timing_mutex_);
    std::string op_key = operation ? operation : "unknown";

    auto it = active_timings_.find(op_key);
    if (it == active_timings_.end()) {
        return 0;
    }

    uint64_t end_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    uint64_t real_duration = end_time - it->second.start_time_ns;
    active_timings_.erase(it);

    return NormalizeTiming(real_duration, operation);
}

// ============================================================================
// GPUContextManager Implementation
// ============================================================================

GPUContextManager& GPUContextManager::Instance() {
    static GPUContextManager instance;
    return instance;
}

void GPUContextManager::SetCurrentContext(GPUContext* context) {
    current_context_ = context;
}

GPUContext* GPUContextManager::GetCurrentContext() {
    return current_context_;
}

void GPUContextManager::ClearCurrentContext() {
    current_context_ = nullptr;
}

} // namespace gpu
} // namespace owl
