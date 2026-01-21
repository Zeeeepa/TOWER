/**
 * OWL GL Interceptor Implementation
 *
 * Intercepts OpenGL ES calls at the ANGLE boundary for GPU virtualization.
 */

#include "gpu/owl_gl_interceptor.h"
#include "gpu/owl_gpu_context.h"
#include "gpu/owl_render_normalizer.h"
#include <sstream>
#include <cstring>

namespace owl {
namespace gpu {

// GL constant definitions (only those currently used)
namespace gl {
    // Data types used in InterceptReadPixels
    constexpr uint32_t UNSIGNED_BYTE = 0x1401;
    constexpr uint32_t RGBA = 0x1908;
}

// ============================================================================
// GLInterceptor Implementation
// ============================================================================

GLInterceptor::GLInterceptor() = default;
GLInterceptor::~GLInterceptor() {
    Shutdown();
}

bool GLInterceptor::Initialize() {
    if (active_) return true;

    // Register default handlers
    RegisterDefaultHandlers();

    // Install hooks into ANGLE (this would be done at compile time in practice)
    if (!InstallHooks()) {
        return false;
    }

    active_ = true;
    return true;
}

void GLInterceptor::Shutdown() {
    if (!active_) return;

    RemoveHooks();

    {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        handlers_.clear();
    }

    active_ = false;
}

bool GLInterceptor::InstallHooks() {
    // In practice, this would hook into ANGLE's GL dispatch table
    // For now, we assume hooks are installed via compile-time patching
    return true;
}

void GLInterceptor::RemoveHooks() {
    // Remove hooks from ANGLE's dispatch table
}

void GLInterceptor::RegisterHandler(GLCallId call_id, GLInterceptHandler handler) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    handlers_[call_id] = std::move(handler);
}

void GLInterceptor::UnregisterHandler(GLCallId call_id) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    handlers_.erase(call_id);
}

void GLInterceptor::RegisterDefaultHandlers() {
    // Parameter query handlers
    RegisterHandler(GLCallId::GetString, [this](GLCallInfo& info) {
        return HandleParameterQuery(info);
    });

    RegisterHandler(GLCallId::GetIntegerv, [this](GLCallInfo& info) {
        return HandleParameterQuery(info);
    });

    RegisterHandler(GLCallId::GetFloatv, [this](GLCallInfo& info) {
        return HandleParameterQuery(info);
    });

    RegisterHandler(GLCallId::GetShaderPrecisionFormat, [this](GLCallInfo& info) {
        return HandleParameterQuery(info);
    });

    // Shader handlers
    RegisterHandler(GLCallId::ShaderSource, [this](GLCallInfo& info) {
        return HandleShaderSource(info);
    });

    // ReadPixels handler
    RegisterHandler(GLCallId::ReadPixels, [this](GLCallInfo& info) {
        return HandleReadPixels(info);
    });

    // Draw call handlers
    RegisterHandler(GLCallId::DrawArrays, [this](GLCallInfo& info) {
        return HandleDrawCall(info);
    });

    RegisterHandler(GLCallId::DrawElements, [this](GLCallInfo& info) {
        return HandleDrawCall(info);
    });

    // Extension handlers
    RegisterHandler(GLCallId::GetSupportedExtensions, [this](GLCallInfo& info) {
        return HandleExtensionQuery(info);
    });

    RegisterHandler(GLCallId::GetExtension, [this](GLCallInfo& info) {
        return HandleExtensionQuery(info);
    });
}

GLCallResult GLInterceptor::HandleParameterQuery(GLCallInfo& info) {
    auto* context = GPUContextManager::Instance().GetCurrentContext();
    if (!context) return GLCallResult::Continue;

    // This would be called from the actual GL intercept point
    // For now, return Continue to let the real GL call proceed
    // The actual spoofing happens in GPUContext

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_calls++;
    }

    return GLCallResult::Continue;
}

GLCallResult GLInterceptor::HandleShaderSource(GLCallInfo& info) {
    auto* context = GPUContextManager::Instance().GetCurrentContext();
    if (!context) return GLCallResult::Continue;

    // Shader translation would happen here
    // For now, let the shader through unchanged

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_calls++;
    }

    return GLCallResult::Continue;
}

GLCallResult GLInterceptor::HandleReadPixels(GLCallInfo& info) {
    // ReadPixels interception for render normalization
    // The actual normalization happens after the GL call completes

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_calls++;
    }

    return GLCallResult::Continue;
}

GLCallResult GLInterceptor::HandleDrawCall(GLCallInfo& info) {
    // Draw call timing would be normalized here

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_calls++;
    }

    return GLCallResult::Continue;
}

GLCallResult GLInterceptor::HandleExtensionQuery(GLCallInfo& info) {
    auto* context = GPUContextManager::Instance().GetCurrentContext();
    if (!context) return GLCallResult::Continue;

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_calls++;
    }

    return GLCallResult::Continue;
}

// ==================== Interception Points ====================

const char* GLInterceptor::InterceptGetString(uint32_t name) {
    auto* context = GPUContextManager::Instance().GetCurrentContext();
    if (!context) return nullptr;

    const char* spoofed = context->GetSpoofedString(name);
    if (spoofed) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.handled_calls++;
        return spoofed;
    }

    return nullptr;  // Let real GL call proceed
}

void GLInterceptor::InterceptGetIntegerv(uint32_t pname, int32_t* params) {
    auto* context = GPUContextManager::Instance().GetCurrentContext();
    if (!context || !params) return;

    if (context->GetSpoofedParameter(pname, params, sizeof(int32_t))) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.handled_calls++;
    }
}

void GLInterceptor::InterceptGetFloatv(uint32_t pname, float* params) {
    auto* context = GPUContextManager::Instance().GetCurrentContext();
    if (!context || !params) return;

    if (context->GetSpoofedParameter(pname, params, sizeof(float))) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.handled_calls++;
    }
}

void GLInterceptor::InterceptGetBooleanv(uint32_t pname, uint8_t* params) {
    auto* context = GPUContextManager::Instance().GetCurrentContext();
    if (!context || !params) return;

    if (context->GetSpoofedParameter(pname, params, sizeof(uint8_t))) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.handled_calls++;
    }
}

void GLInterceptor::InterceptGetShaderPrecisionFormat(uint32_t shader_type,
                                                       uint32_t precision_type,
                                                       int32_t* range,
                                                       int32_t* precision) {
    auto* context = GPUContextManager::Instance().GetCurrentContext();
    if (!context || !range || !precision) return;

    if (context->GetSpoofedShaderPrecision(shader_type, precision_type, range, precision)) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.handled_calls++;
    }
}

void GLInterceptor::InterceptShaderSource(uint32_t shader, int32_t count,
                                          const char** strings, const int32_t* lengths,
                                          const char*** modified_strings,
                                          int32_t** modified_lengths) {
    // Shader translation would happen here
    // For now, pass through unchanged
    *modified_strings = nullptr;
    *modified_lengths = nullptr;

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_calls++;
    }
}

void GLInterceptor::InterceptReadPixels(int32_t x, int32_t y,
                                         int32_t width, int32_t height,
                                         uint32_t format, uint32_t type,
                                         void* pixels) {
    // This is called AFTER the real glReadPixels
    // Apply render normalization to the pixel data

    auto* context = GPUContextManager::Instance().GetCurrentContext();
    if (!context || !pixels) return;

    if (format == gl::RGBA && type == gl::UNSIGNED_BYTE) {
        context->NormalizePixels(pixels, static_cast<size_t>(width),
                                 static_cast<size_t>(height), format, type);

        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.handled_calls++;
    }
}

void GLInterceptor::InterceptDrawArrays(uint32_t mode, int32_t first, int32_t count) {
    // Timing normalization for draw calls

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_calls++;
    }
}

void GLInterceptor::InterceptDrawElements(uint32_t mode, int32_t count,
                                           uint32_t type, const void* indices) {
    // Timing normalization for draw calls

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_calls++;
    }
}

void GLInterceptor::InterceptFinish() {
    // Timing normalization for glFinish

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_calls++;
    }
}

const char* GLInterceptor::InterceptGetExtensions() {
    if (!extensions_cached_) {
        auto* context = GPUContextManager::Instance().GetCurrentContext();
        if (context) {
            const auto& extensions = context->GetSpoofedExtensions();
            std::stringstream ss;
            for (size_t i = 0; i < extensions.size(); ++i) {
                if (i > 0) ss << " ";
                ss << extensions[i];
            }
            cached_extensions_ = ss.str();
            extensions_cached_ = true;
        }
    }

    return extensions_cached_ ? cached_extensions_.c_str() : nullptr;
}

void* GLInterceptor::InterceptGetExtension(const char* name) {
    if (!name) return nullptr;

    auto* context = GPUContextManager::Instance().GetCurrentContext();
    if (!context) return nullptr;

    // Check if extension is in our filtered list
    const auto& extensions = context->GetSpoofedExtensions();
    std::string ext_name(name);

    for (const auto& ext : extensions) {
        if (ext == ext_name) {
            // Extension is available - return real extension pointer
            return reinterpret_cast<void*>(1);  // Non-null indicates available
        }
    }

    return nullptr;  // Extension not available
}

const std::vector<std::string>& GLInterceptor::GetFilteredExtensions() {
    return filtered_extensions_;
}

GLInterceptor::InterceptorStats GLInterceptor::GetStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void GLInterceptor::ResetStats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = InterceptorStats{};
}

// ============================================================================
// ANGLE Integration Hooks
// ============================================================================

namespace angle_hooks {

static GLInterceptor* g_interceptor = nullptr;

bool InitializeHooks() {
    // Get reference to the interceptor
    // In practice, this would set up function pointers in ANGLE's dispatch table
    return true;
}

void ShutdownHooks() {
    g_interceptor = nullptr;
}

bool ShouldIntercept(uint32_t call_id) {
    return g_interceptor && g_interceptor->IsActive();
}

GLCallResult RouteCall(GLCallId call_id, void* args) {
    if (!g_interceptor || !g_interceptor->IsActive()) {
        return GLCallResult::Continue;
    }

    // Route to appropriate handler
    // This would be called from ANGLE's GL command buffer

    return GLCallResult::Continue;
}

} // namespace angle_hooks

} // namespace gpu
} // namespace owl
