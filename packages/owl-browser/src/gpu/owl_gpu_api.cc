/**
 * OWL GPU API - C Interface Implementation
 *
 * This file implements the C interface that the ANGLE wrapper uses to get
 * per-context GPU spoofing values.
 *
 * IMPORTANT: The current context ID is process-wide (not thread-local) because:
 * - Browser thread calls owl_gpu_set_current_context() during navigation
 * - GPU/renderer thread calls owl_gpu_get_vendor() etc during GL rendering
 * - These are different threads, so thread-local storage doesn't work
 * - In practice, only one context renders at a time, so process-wide is safe
 */

#include "gpu/owl_gpu_api.h"
#include "util/logger.h"
#include <map>
#include <mutex>
#include <string>
#include <cstring>
#include <atomic>

namespace {

// OpenGL constants for parameter lookups
constexpr unsigned int GL_MAX_TEXTURE_SIZE = 0x0D33;
constexpr unsigned int GL_MAX_CUBE_MAP_TEXTURE_SIZE = 0x851C;
constexpr unsigned int GL_MAX_RENDERBUFFER_SIZE = 0x84E8;
constexpr unsigned int GL_MAX_VERTEX_ATTRIBS = 0x8869;
constexpr unsigned int GL_MAX_VERTEX_UNIFORM_VECTORS = 0x8DFB;
constexpr unsigned int GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS = 0x8B4C;
constexpr unsigned int GL_MAX_VARYING_VECTORS = 0x8DFC;
constexpr unsigned int GL_MAX_FRAGMENT_UNIFORM_VECTORS = 0x8DFD;
constexpr unsigned int GL_MAX_TEXTURE_IMAGE_UNITS = 0x8872;
constexpr unsigned int GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS = 0x8B4D;
constexpr unsigned int GL_MAX_SAMPLES = 0x8D57;
constexpr unsigned int GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT = 0x84FF;
// Multisampling parameters (critical for VM detection!)
constexpr unsigned int GL_SAMPLES = 0x80A9;
constexpr unsigned int GL_SAMPLE_BUFFERS = 0x80A8;

// WebGL2 parameters
constexpr unsigned int GL_MAX_3D_TEXTURE_SIZE = 0x8073;
constexpr unsigned int GL_MAX_ARRAY_TEXTURE_LAYERS = 0x88FF;
constexpr unsigned int GL_MAX_COLOR_ATTACHMENTS = 0x8CDF;
constexpr unsigned int GL_MAX_DRAW_BUFFERS = 0x8824;
constexpr unsigned int GL_MAX_UNIFORM_BUFFER_BINDINGS = 0x8A2F;
constexpr unsigned int GL_MAX_UNIFORM_BLOCK_SIZE = 0x8A30;
constexpr unsigned int GL_MAX_COMBINED_UNIFORM_BLOCKS = 0x8A2E;
constexpr unsigned int GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS = 0x8C8B;
// Note: GL_MAX_VIEWPORT_DIMS, GL_ALIASED_LINE_WIDTH_RANGE, GL_ALIASED_POINT_SIZE_RANGE
// return arrays and are handled by the ANGLE wrapper directly or via glGetFloatv

// Shader types
constexpr unsigned int GL_VERTEX_SHADER = 0x8B31;
constexpr unsigned int GL_FRAGMENT_SHADER = 0x8B30;

// Precision types (FLOAT)
constexpr unsigned int GL_LOW_FLOAT = 0x8DF0;
constexpr unsigned int GL_MEDIUM_FLOAT = 0x8DF1;
constexpr unsigned int GL_HIGH_FLOAT = 0x8DF2;
// Precision types (INT)
constexpr unsigned int GL_LOW_INT = 0x8DF3;
constexpr unsigned int GL_MEDIUM_INT = 0x8DF4;
constexpr unsigned int GL_HIGH_INT = 0x8DF5;

// Per-context GPU configuration
struct GPUContextConfig {
    std::string vendor;
    std::string renderer;
    std::string version;
    std::string glsl_version;
    bool spoofing_enabled = true;
    bool params_registered = false;
    OWLGPUParams params;
};

// Global registry of context GPU configurations
std::mutex g_contexts_mutex;
std::map<int, GPUContextConfig> g_contexts;

// Process-wide current context ID (-1 = no context)
// Using atomic for thread-safe reads/writes across browser and GPU threads
std::atomic<int> g_current_context_id{-1};

// Thread-local cached strings (for returning const char*)
// These remain thread-local for safe string lifetime management
thread_local std::string g_tls_vendor;
thread_local std::string g_tls_renderer;
thread_local std::string g_tls_version;
thread_local std::string g_tls_glsl_version;

// Get config for current context (returns nullptr if not found)
const GPUContextConfig* GetCurrentConfig() {
    int ctx_id = g_current_context_id.load(std::memory_order_acquire);
    if (ctx_id < 0) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(g_contexts_mutex);
    auto it = g_contexts.find(ctx_id);
    if (it != g_contexts.end()) {
        return &it->second;
    }
    return nullptr;
}

} // anonymous namespace

extern "C" {

// Use __attribute__((used)) to prevent the linker from stripping these functions
// as dead code - they are called by the ANGLE wrapper via dlsym at runtime

__attribute__((used, visibility("default")))
const char* owl_gpu_get_vendor(void) {
    const GPUContextConfig* config = GetCurrentConfig();
    if (!config || !config->spoofing_enabled || config->vendor.empty()) {
#ifdef OWL_DEBUG_BUILD
        int ctx_id = g_current_context_id.load(std::memory_order_acquire);
        LOG_DEBUG("GPUApi", "owl_gpu_get_vendor: returning NULL (ctx_id=" +
                  std::to_string(ctx_id) + ", config=" + (config ? "yes" : "no") + ")");
#endif
        return nullptr;
    }
    g_tls_vendor = config->vendor;
#ifdef OWL_DEBUG_BUILD
    int ctx_id = g_current_context_id.load(std::memory_order_acquire);
    LOG_DEBUG("GPUApi", "owl_gpu_get_vendor: returning '" + g_tls_vendor +
              "' for ctx_id=" + std::to_string(ctx_id));
#endif
    return g_tls_vendor.c_str();
}

__attribute__((used, visibility("default")))
const char* owl_gpu_get_renderer(void) {
    const GPUContextConfig* config = GetCurrentConfig();
    if (!config || !config->spoofing_enabled || config->renderer.empty()) {
#ifdef OWL_DEBUG_BUILD
        int ctx_id = g_current_context_id.load(std::memory_order_acquire);
        LOG_DEBUG("GPUApi", "owl_gpu_get_renderer: returning NULL (ctx_id=" +
                  std::to_string(ctx_id) + ", config=" + (config ? "yes" : "no") + ")");
#endif
        return nullptr;
    }
    g_tls_renderer = config->renderer;
#ifdef OWL_DEBUG_BUILD
    int ctx_id = g_current_context_id.load(std::memory_order_acquire);
    LOG_DEBUG("GPUApi", "owl_gpu_get_renderer: returning '" + g_tls_renderer +
              "' for ctx_id=" + std::to_string(ctx_id));
#endif
    return g_tls_renderer.c_str();
}

__attribute__((used, visibility("default")))
const char* owl_gpu_get_version(void) {
    // IMPORTANT: Always return NULL to let ANGLE return the correct native GL version!
    // ANGLE returns different versions for ES 2.0 (WebGL1) vs ES 3.0 (WebGL2) contexts.
    // If we return a spoofed version, WebGL1 context creation fails because Chromium
    // validates that the GL version matches the requested context type.
    // The version stored in config is a WebGL version string (for JS API spoofing),
    // NOT a valid OpenGL ES version string for native GL calls.
    return nullptr;
}

__attribute__((used, visibility("default")))
const char* owl_gpu_get_glsl_version(void) {
    // IMPORTANT: Always return NULL to let ANGLE return the correct native GLSL version!
    // Same reasoning as owl_gpu_get_version() - ANGLE needs to return the correct
    // GLSL version for each context type (ES 2.0 vs ES 3.0).
    return nullptr;
}

__attribute__((used, visibility("default")))
int owl_gpu_is_spoofing_enabled(void) {
    const GPUContextConfig* config = GetCurrentConfig();
    return (config && config->spoofing_enabled) ? 1 : 0;
}

__attribute__((used, visibility("default")))
int owl_gpu_set_current_context(int context_id) {
    g_current_context_id.store(context_id, std::memory_order_release);

    LOG_DEBUG("GPUApi", "Set current context to " + std::to_string(context_id));

    if (context_id < 0) {
        return 1;  // No context is valid
    }

    std::lock_guard<std::mutex> lock(g_contexts_mutex);
    return g_contexts.count(context_id) > 0 ? 1 : 0;
}

__attribute__((used, visibility("default")))
void owl_gpu_register_context(int context_id,
                               const char* vendor,
                               const char* renderer,
                               const char* version,
                               const char* glsl_version) {
    GPUContextConfig config;
    config.vendor = vendor ? vendor : "";
    config.renderer = renderer ? renderer : "";
    config.version = version ? version : "";
    config.glsl_version = glsl_version ? glsl_version : "";
    config.spoofing_enabled = true;

    {
        std::lock_guard<std::mutex> lock(g_contexts_mutex);
        g_contexts[context_id] = std::move(config);
    }

    LOG_DEBUG("GPUApi", "Registered GPU context " + std::to_string(context_id) +
             ": " + (vendor ? vendor : "null") + " / " + (renderer ? renderer : "null"));
}

__attribute__((used, visibility("default")))
void owl_gpu_unregister_context(int context_id) {
    std::lock_guard<std::mutex> lock(g_contexts_mutex);
    g_contexts.erase(context_id);

    LOG_DEBUG("GPUApi", "Unregistered GPU context " + std::to_string(context_id));
}

__attribute__((used, visibility("default")))
void owl_gpu_register_params(int context_id, const OWLGPUParams* params) {
    if (!params) return;

    std::lock_guard<std::mutex> lock(g_contexts_mutex);
    auto it = g_contexts.find(context_id);
    if (it != g_contexts.end()) {
        it->second.params = *params;
        it->second.params_registered = true;
        LOG_DEBUG("GPUApi", "Registered GPU params for context " + std::to_string(context_id) +
                 ": max_texture=" + std::to_string(params->max_texture_size) +
                 ", max_viewport=[" + std::to_string(params->max_viewport_dims[0]) + "," +
                 std::to_string(params->max_viewport_dims[1]) + "]");
    } else {
        LOG_WARN("GPUApi", "Cannot register params for unknown context " + std::to_string(context_id));
    }
}

__attribute__((used, visibility("default")))
int owl_gpu_get_integer(unsigned int pname, int* value) {
    if (!value) return 0;

    int ctx_id = g_current_context_id.load(std::memory_order_acquire);
    if (ctx_id < 0) return 0;

    std::lock_guard<std::mutex> lock(g_contexts_mutex);
    auto it = g_contexts.find(ctx_id);
    if (it == g_contexts.end() || !it->second.spoofing_enabled || !it->second.params_registered) {
        return 0;
    }

    const OWLGPUParams& p = it->second.params;

    switch (pname) {
        case GL_MAX_TEXTURE_SIZE:
            *value = p.max_texture_size;
            return 1;
        case GL_MAX_CUBE_MAP_TEXTURE_SIZE:
            *value = p.max_cube_map_texture_size;
            return 1;
        case GL_MAX_RENDERBUFFER_SIZE:
            *value = p.max_render_buffer_size;
            return 1;
        case GL_MAX_VERTEX_ATTRIBS:
            *value = p.max_vertex_attribs;
            return 1;
        case GL_MAX_VERTEX_UNIFORM_VECTORS:
            *value = p.max_vertex_uniform_vectors;
            return 1;
        case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:
            *value = p.max_vertex_texture_units;
            return 1;
        case GL_MAX_VARYING_VECTORS:
            *value = p.max_varying_vectors;
            return 1;
        case GL_MAX_FRAGMENT_UNIFORM_VECTORS:
            *value = p.max_fragment_uniform_vectors;
            return 1;
        case GL_MAX_TEXTURE_IMAGE_UNITS:
            *value = p.max_texture_units;
            return 1;
        case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
            *value = p.max_combined_texture_units;
            return 1;
        case GL_MAX_SAMPLES:
            *value = p.max_samples;
            return 1;
        // Multisampling parameters (critical for VM detection!)
        case GL_SAMPLES:
            *value = p.samples;
            return 1;
        case GL_SAMPLE_BUFFERS:
            *value = p.sample_buffers;
            return 1;
        // WebGL2 parameters
        case GL_MAX_3D_TEXTURE_SIZE:
            *value = p.max_3d_texture_size;
            return 1;
        case GL_MAX_ARRAY_TEXTURE_LAYERS:
            *value = p.max_array_texture_layers;
            return 1;
        case GL_MAX_COLOR_ATTACHMENTS:
            *value = p.max_color_attachments;
            return 1;
        case GL_MAX_DRAW_BUFFERS:
            *value = p.max_draw_buffers;
            return 1;
        case GL_MAX_UNIFORM_BUFFER_BINDINGS:
            *value = p.max_uniform_buffer_bindings;
            return 1;
        case GL_MAX_UNIFORM_BLOCK_SIZE:
            *value = p.max_uniform_block_size;
            return 1;
        case GL_MAX_COMBINED_UNIFORM_BLOCKS:
            *value = p.max_combined_uniform_blocks;
            return 1;
        case GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS:
            *value = p.max_transform_feedback_separate_attribs;
            return 1;
        default:
            return 0;
    }
}

__attribute__((used, visibility("default")))
int owl_gpu_get_float(unsigned int pname, float* value) {
    if (!value) return 0;

    int ctx_id = g_current_context_id.load(std::memory_order_acquire);
    if (ctx_id < 0) return 0;

    std::lock_guard<std::mutex> lock(g_contexts_mutex);
    auto it = g_contexts.find(ctx_id);
    if (it == g_contexts.end() || !it->second.spoofing_enabled || !it->second.params_registered) {
        return 0;
    }

    const OWLGPUParams& p = it->second.params;

    switch (pname) {
        case GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT:
            *value = p.max_anisotropy;
            return 1;
        default:
            return 0;
    }
}

__attribute__((used, visibility("default")))
int owl_gpu_get_shader_precision(unsigned int shader_type, unsigned int precision_type,
                                  int* range, int* precision) {
    if (!range || !precision) return 0;

    int ctx_id = g_current_context_id.load(std::memory_order_acquire);
    if (ctx_id < 0) return 0;

    std::lock_guard<std::mutex> lock(g_contexts_mutex);
    auto it = g_contexts.find(ctx_id);
    if (it == g_contexts.end() || !it->second.spoofing_enabled || !it->second.params_registered) {
        return 0;
    }

    const OWLGPUParams& p = it->second.params;
    const int* src = nullptr;

    if (shader_type == GL_VERTEX_SHADER) {
        switch (precision_type) {
            // FLOAT precision
            case GL_HIGH_FLOAT:   src = p.vertex_high_float; break;
            case GL_MEDIUM_FLOAT: src = p.vertex_medium_float; break;
            case GL_LOW_FLOAT:    src = p.vertex_low_float; break;
            // INT precision
            case GL_HIGH_INT:     src = p.vertex_high_int; break;
            case GL_MEDIUM_INT:   src = p.vertex_medium_int; break;
            case GL_LOW_INT:      src = p.vertex_low_int; break;
        }
    } else if (shader_type == GL_FRAGMENT_SHADER) {
        switch (precision_type) {
            // FLOAT precision
            case GL_HIGH_FLOAT:   src = p.fragment_high_float; break;
            case GL_MEDIUM_FLOAT: src = p.fragment_medium_float; break;
            case GL_LOW_FLOAT:    src = p.fragment_low_float; break;
            // INT precision
            case GL_HIGH_INT:     src = p.fragment_high_int; break;
            case GL_MEDIUM_INT:   src = p.fragment_medium_int; break;
            case GL_LOW_INT:      src = p.fragment_low_int; break;
        }
    }

    if (src) {
        range[0] = src[0];
        range[1] = src[1];
        *precision = src[2];
        return 1;
    }

    return 0;
}

} // extern "C"
