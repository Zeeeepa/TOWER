#pragma once

/**
 * OWL GL Interceptor
 *
 * Intercepts OpenGL ES calls at the ANGLE boundary to enable GPU virtualization.
 * This operates at a lower level than JavaScript API interception, allowing us to
 * control actual rendering behavior.
 *
 * Interception Points:
 * - Parameter queries (glGetParameter, glGetString, etc.)
 * - Shader operations (glShaderSource, glCompileShader, etc.)
 * - Rendering operations (glDrawArrays, glDrawElements, etc.)
 * - Framebuffer reads (glReadPixels)
 */

#include "gpu/owl_gpu_virtualization.h"
#include <functional>
#include <unordered_map>
#include <mutex>

namespace owl {
namespace gpu {

// Forward declarations
class GPUContext;

/**
 * GL Call IDs for interception
 */
enum class GLCallId : uint32_t {
    // Parameter queries
    GetString = 0x1000,
    GetIntegerv,
    GetFloatv,
    GetBooleanv,
    GetParameter,
    GetShaderPrecisionFormat,
    GetSupportedExtensions,
    GetExtension,

    // Shader operations
    CreateShader = 0x2000,
    DeleteShader,
    ShaderSource,
    CompileShader,
    GetShaderiv,
    GetShaderInfoLog,
    GetShaderSource,

    // Program operations
    CreateProgram = 0x3000,
    DeleteProgram,
    AttachShader,
    DetachShader,
    LinkProgram,
    UseProgram,
    GetProgramiv,
    GetProgramInfoLog,
    GetUniformLocation,
    GetAttribLocation,

    // Texture operations
    GenTextures = 0x4000,
    DeleteTextures,
    BindTexture,
    TexImage2D,
    TexSubImage2D,
    TexParameteri,
    TexParameterf,
    GenerateMipmap,

    // Framebuffer operations
    GenFramebuffers = 0x5000,
    DeleteFramebuffers,
    BindFramebuffer,
    FramebufferTexture2D,
    FramebufferRenderbuffer,
    CheckFramebufferStatus,
    ReadPixels,

    // Drawing operations
    DrawArrays = 0x6000,
    DrawElements,
    DrawArraysInstanced,
    DrawElementsInstanced,
    Clear,

    // State operations
    Enable = 0x7000,
    Disable,
    BlendFunc,
    BlendFuncSeparate,
    BlendEquation,
    BlendEquationSeparate,
    DepthFunc,
    DepthMask,
    CullFace,
    FrontFace,
    Viewport,
    Scissor,

    // Buffer operations
    GenBuffers = 0x8000,
    DeleteBuffers,
    BindBuffer,
    BufferData,
    BufferSubData,
    MapBuffer,
    UnmapBuffer,

    // Other
    Flush = 0x9000,
    Finish,
    GetError,
};

/**
 * GL Call Result - indicates how the interceptor handled the call
 */
enum class GLCallResult {
    Continue,       // Continue to real GL call
    Handled,        // Interceptor handled the call, skip real GL
    Modified,       // Interceptor modified arguments, continue to real GL
    Error           // Interceptor encountered an error
};

/**
 * GL Call Info - passed to interceptor handlers
 */
struct GLCallInfo {
    GLCallId call_id;
    GPUContext* context;
    void* args;
    void* return_value;
    bool before_call;   // true = before real GL call, false = after
};

/**
 * GL Interceptor Handler signature
 */
using GLInterceptHandler = std::function<GLCallResult(GLCallInfo& info)>;

/**
 * GL Interceptor
 *
 * Main class for intercepting GL calls. Hooks are installed at the ANGLE level
 * to intercept all OpenGL ES calls before they reach the GPU driver.
 */
class GLInterceptor {
public:
    GLInterceptor();
    ~GLInterceptor();

    // Non-copyable
    GLInterceptor(const GLInterceptor&) = delete;
    GLInterceptor& operator=(const GLInterceptor&) = delete;

    // ==================== Initialization ====================

    /**
     * Initialize the interceptor
     * Must be called before any GL calls are made
     */
    bool Initialize();

    /**
     * Shutdown the interceptor
     */
    void Shutdown();

    /**
     * Check if interceptor is active
     */
    bool IsActive() const { return active_; }

    // ==================== Handler Registration ====================

    /**
     * Register a handler for a specific GL call
     * Handler is called both before and after the real GL call
     */
    void RegisterHandler(GLCallId call_id, GLInterceptHandler handler);

    /**
     * Unregister a handler
     */
    void UnregisterHandler(GLCallId call_id);

    /**
     * Register default handlers for GPU virtualization
     */
    void RegisterDefaultHandlers();

    // ==================== Interception Points ====================

    // These are called from ANGLE hooks

    /**
     * Intercept glGetString
     */
    const char* InterceptGetString(uint32_t name);

    /**
     * Intercept glGetIntegerv
     */
    void InterceptGetIntegerv(uint32_t pname, int32_t* params);

    /**
     * Intercept glGetFloatv
     */
    void InterceptGetFloatv(uint32_t pname, float* params);

    /**
     * Intercept glGetBooleanv
     */
    void InterceptGetBooleanv(uint32_t pname, uint8_t* params);

    /**
     * Intercept glGetShaderPrecisionFormat
     */
    void InterceptGetShaderPrecisionFormat(uint32_t shader_type,
                                            uint32_t precision_type,
                                            int32_t* range,
                                            int32_t* precision);

    /**
     * Intercept glShaderSource
     */
    void InterceptShaderSource(uint32_t shader, int32_t count,
                               const char** strings, const int32_t* lengths,
                               const char*** modified_strings,
                               int32_t** modified_lengths);

    /**
     * Intercept glReadPixels
     */
    void InterceptReadPixels(int32_t x, int32_t y,
                             int32_t width, int32_t height,
                             uint32_t format, uint32_t type,
                             void* pixels);

    /**
     * Intercept draw calls for timing normalization
     */
    void InterceptDrawArrays(uint32_t mode, int32_t first, int32_t count);
    void InterceptDrawElements(uint32_t mode, int32_t count,
                               uint32_t type, const void* indices);

    /**
     * Intercept glFinish for timing normalization
     */
    void InterceptFinish();

    // ==================== Extension Queries ====================

    /**
     * Intercept extension string query
     */
    const char* InterceptGetExtensions();

    /**
     * Intercept single extension query
     */
    void* InterceptGetExtension(const char* name);

    /**
     * Get list of supported extensions (filtered by profile)
     */
    const std::vector<std::string>& GetFilteredExtensions();

    // ==================== Statistics ====================

    struct InterceptorStats {
        uint64_t total_calls = 0;
        uint64_t handled_calls = 0;
        uint64_t modified_calls = 0;
        std::unordered_map<GLCallId, uint64_t> call_counts;
    };

    InterceptorStats GetStats() const;
    void ResetStats();

private:
    // Install hooks into ANGLE
    bool InstallHooks();
    void RemoveHooks();

    // Default handlers for different call types
    GLCallResult HandleParameterQuery(GLCallInfo& info);
    GLCallResult HandleShaderSource(GLCallInfo& info);
    GLCallResult HandleReadPixels(GLCallInfo& info);
    GLCallResult HandleDrawCall(GLCallInfo& info);
    GLCallResult HandleExtensionQuery(GLCallInfo& info);

    bool active_ = false;

    mutable std::mutex handlers_mutex_;
    std::unordered_map<GLCallId, GLInterceptHandler> handlers_;

    // Cached extension string
    std::string cached_extensions_;
    std::vector<std::string> filtered_extensions_;
    bool extensions_cached_ = false;

    // Statistics
    mutable std::mutex stats_mutex_;
    InterceptorStats stats_;
};

/**
 * ANGLE Integration Hooks
 *
 * These functions are called from patched ANGLE code to route calls
 * through the interceptor.
 */
namespace angle_hooks {

/**
 * Initialize ANGLE hooks
 * Called during browser initialization
 */
bool InitializeHooks();

/**
 * Shutdown ANGLE hooks
 */
void ShutdownHooks();

/**
 * Check if a GL call should be intercepted
 */
bool ShouldIntercept(uint32_t call_id);

/**
 * Route a GL call through the interceptor
 */
GLCallResult RouteCall(GLCallId call_id, void* args);

} // namespace angle_hooks

} // namespace gpu
} // namespace owl
