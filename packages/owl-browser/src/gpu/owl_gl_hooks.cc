/**
 * OWL GL Hooks - Runtime GL Function Interception
 *
 * Uses fishhook to intercept OpenGL ES calls from ANGLE at runtime.
 * This enables GPU virtualization without rebuilding CEF/ANGLE.
 *
 * Key intercepted functions:
 * - glGetString: Spoof vendor/renderer strings
 * - glGetIntegerv: Spoof GPU parameters
 * - glReadPixels: Normalize pixel data to prevent fingerprinting
 * - glGetShaderPrecisionFormat: Spoof precision formats
 */

#include "gpu/owl_gl_hooks.h"
#include "gpu/owl_gpu_virtualization.h"
#include "gpu/owl_gpu_context.h"
#include "util/logger.h"

#ifdef __APPLE__
#include "util/fishhook.h"
#include <OpenGL/gl.h>
#include <dlfcn.h>
#include <cstring>
#include <mutex>
#include <random>
#include <cmath>

namespace owl {
namespace gpu {
namespace hooks {

// ============================================================================
// Original function pointers (saved before hooking)
// ============================================================================

static const GLubyte* (*orig_glGetString)(GLenum name) = nullptr;
static void (*orig_glGetIntegerv)(GLenum pname, GLint* params) = nullptr;
static void (*orig_glGetFloatv)(GLenum pname, GLfloat* params) = nullptr;
static void (*orig_glReadPixels)(GLint x, GLint y, GLsizei width, GLsizei height,
                                  GLenum format, GLenum type, void* pixels) = nullptr;
static void (*orig_glGetShaderPrecisionFormat)(GLenum shadertype, GLenum precisiontype,
                                                GLint* range, GLint* precision) = nullptr;

// ============================================================================
// Hook state
// ============================================================================

static std::mutex g_hooks_mutex;
static bool g_hooks_installed = false;
static bool g_hooks_enabled = true;

// Spoofed strings (must persist for glGetString return)
static std::string g_spoofed_vendor;
static std::string g_spoofed_renderer;
static std::string g_spoofed_version;
static std::string g_spoofed_shading_language_version;

// GL constants
#define GL_VENDOR                           0x1F00
#define GL_RENDERER                         0x1F01
#define GL_VERSION                          0x1F02
#define GL_SHADING_LANGUAGE_VERSION         0x8B8C
#define GL_MAX_TEXTURE_SIZE                 0x0D33
#define GL_MAX_VIEWPORT_DIMS                0x0D3A
#define GL_MAX_VERTEX_ATTRIBS               0x8869
#define GL_MAX_VERTEX_UNIFORM_VECTORS       0x8DFB
#define GL_MAX_VARYING_VECTORS              0x8DFC
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D
#define GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS   0x8B4C
#define GL_MAX_TEXTURE_IMAGE_UNITS          0x8872
#define GL_MAX_FRAGMENT_UNIFORM_VECTORS     0x8DFD
#define GL_MAX_RENDERBUFFER_SIZE            0x84E8

// WebGL extension constants
#define GL_UNMASKED_VENDOR_WEBGL            0x9245
#define GL_UNMASKED_RENDERER_WEBGL          0x9246

// ============================================================================
// Hooked Functions
// ============================================================================

/**
 * Hooked glGetString - returns spoofed vendor/renderer strings
 *
 * NOTE: We do NOT spoof GL_VERSION or GL_SHADING_LANGUAGE_VERSION!
 * ANGLE returns different versions for ES 2.0 (WebGL1) vs ES 3.0 (WebGL2) contexts.
 * If we always return a fixed version, WebGL1 context creation fails because
 * Chromium validates that the version matches the requested context type.
 * Let ANGLE return the correct version for each context type.
 */
static const GLubyte* hooked_glGetString(GLenum name) {
    if (!g_hooks_enabled || !orig_glGetString) {
        return orig_glGetString ? orig_glGetString(name) : nullptr;
    }

    // Get current GPU context for spoofing
    auto* context = GPUVirtualizationSystem::Instance().GetCurrentContext();

    switch (name) {
        case GL_VENDOR:
            if (context) {
                const char* spoofed = context->GetSpoofedString(GL_VENDOR);
                if (spoofed) return reinterpret_cast<const GLubyte*>(spoofed);
            }
            if (!g_spoofed_vendor.empty()) {
                return reinterpret_cast<const GLubyte*>(g_spoofed_vendor.c_str());
            }
            break;

        case GL_RENDERER:
            if (context) {
                const char* spoofed = context->GetSpoofedString(GL_RENDERER);
                if (spoofed) return reinterpret_cast<const GLubyte*>(spoofed);
            }
            if (!g_spoofed_renderer.empty()) {
                return reinterpret_cast<const GLubyte*>(g_spoofed_renderer.c_str());
            }
            break;

        case GL_VERSION:
        case GL_SHADING_LANGUAGE_VERSION:
            // DO NOT SPOOF - let ANGLE return the correct version for each context type
            // WebGL1 contexts need "OpenGL ES 2.0", WebGL2 contexts need "OpenGL ES 3.0"
            // Spoofing these with WebGL version strings breaks context creation
            break;
    }

    return orig_glGetString(name);
}

/**
 * Hooked glGetIntegerv - returns spoofed GPU parameters
 */
static void hooked_glGetIntegerv(GLenum pname, GLint* params) {
    if (!g_hooks_enabled || !orig_glGetIntegerv || !params) {
        if (orig_glGetIntegerv) orig_glGetIntegerv(pname, params);
        return;
    }

    // Get current GPU context for spoofing
    auto* context = GPUVirtualizationSystem::Instance().GetCurrentContext();

    if (context && context->GetSpoofedParameter(pname, params, sizeof(GLint))) {
        return;  // Parameter was spoofed
    }

    // Call original
    orig_glGetIntegerv(pname, params);
}

/**
 * Hooked glGetFloatv - returns spoofed float parameters
 */
static void hooked_glGetFloatv(GLenum pname, GLfloat* params) {
    if (!g_hooks_enabled || !orig_glGetFloatv || !params) {
        if (orig_glGetFloatv) orig_glGetFloatv(pname, params);
        return;
    }

    auto* context = GPUVirtualizationSystem::Instance().GetCurrentContext();

    if (context && context->GetSpoofedParameter(pname, params, sizeof(GLfloat))) {
        return;
    }

    orig_glGetFloatv(pname, params);
}

/**
 * Apply deterministic noise to pixel data for fingerprint protection
 * The noise is seeded by the GPU context so it's consistent per-profile
 */
static void normalizePixels(void* pixels, GLsizei width, GLsizei height,
                            GLenum format, GLenum type, uint64_t seed) {
    if (!pixels || width <= 0 || height <= 0) return;

    // Only handle RGBA/UNSIGNED_BYTE for now (most common WebGL readback)
    if (format != 0x1908 /* GL_RGBA */ || type != 0x1401 /* GL_UNSIGNED_BYTE */) {
        return;
    }

    uint8_t* data = static_cast<uint8_t*>(pixels);
    size_t pixel_count = width * height;

    // Use seed for deterministic noise
    std::mt19937 rng(static_cast<uint32_t>(seed));
    std::uniform_int_distribution<int> noise_dist(-2, 2);

    for (size_t i = 0; i < pixel_count * 4; i++) {
        int val = data[i] + noise_dist(rng);
        data[i] = static_cast<uint8_t>(std::max(0, std::min(255, val)));
    }
}

/**
 * Hooked glReadPixels - normalizes pixel data to prevent fingerprinting
 */
static void hooked_glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                                 GLenum format, GLenum type, void* pixels) {
    if (!orig_glReadPixels) return;

    // Call original first
    orig_glReadPixels(x, y, width, height, format, type, pixels);

    if (!g_hooks_enabled || !pixels) return;

    // Get context for normalization seed
    auto* context = GPUVirtualizationSystem::Instance().GetCurrentContext();
    if (context) {
        // Use profile-based seed for deterministic noise
        uint64_t seed = context->GetNormalizationSeed();
        normalizePixels(pixels, width, height, format, type, seed);
    }
}

/**
 * Hooked glGetShaderPrecisionFormat - returns spoofed precision
 */
static void hooked_glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype,
                                               GLint* range, GLint* precision) {
    if (!g_hooks_enabled || !orig_glGetShaderPrecisionFormat) {
        if (orig_glGetShaderPrecisionFormat) {
            orig_glGetShaderPrecisionFormat(shadertype, precisiontype, range, precision);
        }
        return;
    }

    auto* context = GPUVirtualizationSystem::Instance().GetCurrentContext();

    if (context && context->GetSpoofedShaderPrecision(shadertype, precisiontype, range, precision)) {
        return;  // Precision was spoofed
    }

    orig_glGetShaderPrecisionFormat(shadertype, precisiontype, range, precision);
}

// ============================================================================
// Public API
// ============================================================================

bool InstallGLHooks() {
    std::lock_guard<std::mutex> lock(g_hooks_mutex);

    if (g_hooks_installed) {
        LOG_DEBUG("GLHooks", "GL hooks already installed");
        return true;
    }

    LOG_DEBUG("GLHooks", "Installing GL function hooks via fishhook...");

    // Define rebindings for GL functions
    struct rebinding rebindings[] = {
        {"glGetString", reinterpret_cast<void*>(hooked_glGetString),
         reinterpret_cast<void**>(&orig_glGetString)},
        {"glGetIntegerv", reinterpret_cast<void*>(hooked_glGetIntegerv),
         reinterpret_cast<void**>(&orig_glGetIntegerv)},
        {"glGetFloatv", reinterpret_cast<void*>(hooked_glGetFloatv),
         reinterpret_cast<void**>(&orig_glGetFloatv)},
        {"glReadPixels", reinterpret_cast<void*>(hooked_glReadPixels),
         reinterpret_cast<void**>(&orig_glReadPixels)},
        {"glGetShaderPrecisionFormat", reinterpret_cast<void*>(hooked_glGetShaderPrecisionFormat),
         reinterpret_cast<void**>(&orig_glGetShaderPrecisionFormat)},
    };

    int result = rebind_symbols(rebindings, sizeof(rebindings) / sizeof(rebindings[0]));

    if (result != 0) {
        LOG_ERROR("GLHooks", "Failed to install GL hooks, rebind_symbols returned: " + std::to_string(result));
        return false;
    }

    g_hooks_installed = true;

    LOG_DEBUG("GLHooks", "GL hooks installed successfully");
    LOG_DEBUG("GLHooks", "  - glGetString: " + std::string(orig_glGetString ? "hooked" : "not found"));
    LOG_DEBUG("GLHooks", "  - glGetIntegerv: " + std::string(orig_glGetIntegerv ? "hooked" : "not found"));
    LOG_DEBUG("GLHooks", "  - glGetFloatv: " + std::string(orig_glGetFloatv ? "hooked" : "not found"));
    LOG_DEBUG("GLHooks", "  - glReadPixels: " + std::string(orig_glReadPixels ? "hooked" : "not found"));
    LOG_DEBUG("GLHooks", "  - glGetShaderPrecisionFormat: " +
              std::string(orig_glGetShaderPrecisionFormat ? "hooked" : "not found"));

    return true;
}

void RemoveGLHooks() {
    std::lock_guard<std::mutex> lock(g_hooks_mutex);

    if (!g_hooks_installed) return;

    // Note: fishhook doesn't support unrebinding, so we just disable
    g_hooks_enabled = false;
    g_hooks_installed = false;

    LOG_DEBUG("GLHooks", "GL hooks disabled");
}

void EnableGLHooks(bool enable) {
    std::lock_guard<std::mutex> lock(g_hooks_mutex);
    g_hooks_enabled = enable;
    LOG_DEBUG("GLHooks", std::string("GL hooks ") + (enable ? "enabled" : "disabled"));
}

bool AreGLHooksInstalled() {
    std::lock_guard<std::mutex> lock(g_hooks_mutex);
    return g_hooks_installed;
}

void SetDefaultSpoofedStrings(const std::string& vendor,
                               const std::string& renderer,
                               const std::string& version,
                               const std::string& glsl_version) {
    std::lock_guard<std::mutex> lock(g_hooks_mutex);
    g_spoofed_vendor = vendor;
    g_spoofed_renderer = renderer;
    g_spoofed_version = version;
    g_spoofed_shading_language_version = glsl_version;

    LOG_DEBUG("GLHooks", "Set default spoofed strings:");
    LOG_DEBUG("GLHooks", "  Vendor: " + vendor);
    LOG_DEBUG("GLHooks", "  Renderer: " + renderer);
}

} // namespace hooks
} // namespace gpu
} // namespace owl

#else // !__APPLE__

// Non-Apple platforms: stub implementation
namespace owl {
namespace gpu {
namespace hooks {

bool InstallGLHooks() {
    LOG_WARN("GLHooks", "GL hooks not implemented for this platform");
    return false;
}

void RemoveGLHooks() {}
void EnableGLHooks(bool) {}
bool AreGLHooksInstalled() { return false; }
void SetDefaultSpoofedStrings(const std::string&, const std::string&,
                               const std::string&, const std::string&) {}

} // namespace hooks
} // namespace gpu
} // namespace owl

#endif // __APPLE__
