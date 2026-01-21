/**
 * OWL ANGLE Wrapper
 *
 * This library wraps the original ANGLE (libGLESv2) to intercept GL calls
 * for GPU virtualization. It loads the original ANGLE library dynamically
 * and forwards all calls, intercepting specific functions for spoofing.
 *
 * Intercepted functions:
 * - glGetString: Spoof GL_VENDOR, GL_RENDERER, GL_VERSION
 * - glGetIntegerv: Spoof GPU parameters
 * - glReadPixels: Normalize pixel data to prevent fingerprinting
 * - glGetShaderPrecisionFormat: Spoof shader precision
 */

// Must be defined before any includes for RTLD_DEFAULT, Dl_info, dladdr on Linux
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <ctype.h>
#include <dlfcn.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

// ============================================================================
// File Logging
// ============================================================================

static FILE* g_log_file = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static void owl_log(const char* fmt, ...) {
    pthread_mutex_lock(&g_log_mutex);

    // Open log file if not already open
    if (!g_log_file) {
        g_log_file = fopen("/tmp/owl_angle_wrapper.log", "a");
        if (g_log_file) {
            setbuf(g_log_file, NULL);  // Disable buffering
        }
    }

    if (g_log_file) {
        // Timestamp
        struct timeval tv;
        gettimeofday(&tv, NULL);
        struct tm* tm_info = localtime(&tv.tv_sec);
        fprintf(g_log_file, "[%02d:%02d:%02d.%03d] [PID:%d] ",
                tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
                (int)(tv.tv_usec / 1000), getpid());

        // Message
        va_list args;
        va_start(args, fmt);
        vfprintf(g_log_file, fmt, args);
        va_end(args);

        fprintf(g_log_file, "\n");
        fflush(g_log_file);
    }

    pthread_mutex_unlock(&g_log_mutex);
}

// Forward declaration - owl_debug is defined after GLenum type
static void owl_debug(const char* fmt, ...);

// ============================================================================
// Library Constructor - runs at load time
// ============================================================================

__attribute__((constructor))
static void on_library_load(void) {
    owl_log("Library loaded");
}


// ============================================================================
// Configuration - GPU Spoofing Values
// ============================================================================

// Default values (fallback if no per-context or env var values)
// IMPORTANT: VERSION must be a valid OpenGL ES version string (not WebGL version)
// CEF/Chromium validates this and will crash if it's not in expected format
static const char* DEFAULT_VENDOR = "NVIDIA Corporation";
static const char* DEFAULT_RENDERER = "NVIDIA GeForce RTX 4070";

// Target platform for ANGLE backend selection (from OWL_GPU_PLATFORM env var)
static char g_target_platform[64] = {0};

// Helper function to format renderer into ANGLE format if not already formatted
// Input: raw GPU name like "Apple M1" or "NVIDIA GeForce RTX 4070"
// Output: ANGLE format like "ANGLE (Apple, ANGLE Metal Renderer: Apple M1, Unspecified Version)"
static void format_renderer_to_angle(const char* raw_renderer, const char* vendor, char* output, size_t output_size) {
    if (!raw_renderer || !output || output_size == 0) return;

    // If already in ANGLE format, copy as-is
    if (strstr(raw_renderer, "ANGLE") != NULL) {
        strncpy(output, raw_renderer, output_size - 1);
        output[output_size - 1] = '\0';
        return;
    }

    // Create lowercase copy for detection
    char renderer_lower[256] = {0};
    strncpy(renderer_lower, raw_renderer, sizeof(renderer_lower) - 1);
    for (int i = 0; renderer_lower[i]; i++) {
        renderer_lower[i] = (char)tolower((unsigned char)renderer_lower[i]);
    }

    // Determine ANGLE backend based on TARGET PLATFORM (not GPU vendor!)
    // - macOS: ALL GPUs use Metal (Intel, AMD, Apple Silicon all use Metal on macOS)
    // - Linux: ALL GPUs use OpenGL
    // - Windows: ALL GPUs use Direct3D11
    int is_macos = (strstr(g_target_platform, "Mac") != NULL);
    int is_linux = (strstr(g_target_platform, "Linux") != NULL);

    const char* backend;
    if (is_macos) {
        backend = "ANGLE Metal Renderer";
    } else if (is_linux) {
        backend = "ANGLE OpenGL Renderer";
    } else {
        backend = "ANGLE Direct3D11 Renderer";  // Windows default
    }

    const char* vendor_name;

    // Detect GPU vendor for the vendor name field
    if (strstr(renderer_lower, "apple") != NULL ||
        strstr(renderer_lower, " m1") != NULL ||
        strstr(renderer_lower, " m2") != NULL ||
        strstr(renderer_lower, " m3") != NULL ||
        strstr(renderer_lower, " m4") != NULL) {
        vendor_name = "Apple";
    } else if (strstr(renderer_lower, "nvidia") != NULL ||
               strstr(renderer_lower, "geforce") != NULL ||
               strstr(renderer_lower, "rtx") != NULL ||
               strstr(renderer_lower, "gtx") != NULL) {
        vendor_name = "NVIDIA Corporation";
    } else if (strstr(renderer_lower, "amd") != NULL ||
               strstr(renderer_lower, "radeon") != NULL) {
        vendor_name = "AMD";
    } else if (strstr(renderer_lower, "intel") != NULL ||
               strstr(renderer_lower, "iris") != NULL ||
               strstr(renderer_lower, "uhd") != NULL ||
               strstr(renderer_lower, "hd graphics") != NULL) {
        vendor_name = "Intel Inc.";
    } else {
        // Default fallback
        vendor_name = vendor ? vendor : "Unknown";
    }

    // Format: "ANGLE (Vendor, ANGLE Backend: GPU_Name, Unspecified Version)"
    snprintf(output, output_size, "ANGLE (%s, %s: %s, Unspecified Version)",
             vendor_name, backend, raw_renderer);
}
static const char* DEFAULT_VERSION = "OpenGL ES 3.0 (ANGLE 2.1.23096 git hash: d33b20f2c832)";
static const char* DEFAULT_GLSL_VERSION = "OpenGL ES GLSL ES 3.00 (ANGLE 2.1.23096 git hash: d33b20f2c832)";

// Fixed OpenGL ES version strings for ANGLE (used instead of VM profile's webgl_version)
// These MUST be valid OpenGL ES version strings, not WebGL version strings
static const char* ANGLE_VERSION = "OpenGL ES 3.0 (ANGLE 2.1.23096 git hash: d33b20f2c832)";
static const char* ANGLE_GLSL_VERSION = "OpenGL ES GLSL ES 3.00 (ANGLE 2.1.23096 git hash: d33b20f2c832)";

// Session-wide spoofed values (from env vars, used as fallback)
static char g_session_vendor[256] = {0};
static char g_session_renderer[256] = {0};
static char g_session_version[512] = {0};
static char g_session_glsl_version[512] = {0};

// Session-wide shader precision values (format: [range_min, range_max, precision])
static int g_session_vertex_high_float[3] = {0};
static int g_session_vertex_medium_float[3] = {0};
static int g_session_vertex_low_float[3] = {0};
static int g_session_fragment_high_float[3] = {0};
static int g_session_fragment_medium_float[3] = {0};
static int g_session_fragment_low_float[3] = {0};
static int g_session_vertex_high_int[3] = {0};
static int g_session_vertex_medium_int[3] = {0};
static int g_session_vertex_low_int[3] = {0};
static int g_session_fragment_high_int[3] = {0};
static int g_session_fragment_medium_int[3] = {0};
static int g_session_fragment_low_int[3] = {0};
static int g_session_precision_loaded = 0;

// Session-wide WebGL integer parameters
static int g_session_max_texture_size = 0;
static int g_session_max_cube_map_texture_size = 0;
static int g_session_max_render_buffer_size = 0;
static int g_session_max_vertex_attribs = 0;
static int g_session_max_vertex_uniform_vectors = 0;
static int g_session_max_vertex_texture_units = 0;
static int g_session_max_varying_vectors = 0;
static int g_session_max_fragment_uniform_vectors = 0;
static int g_session_max_texture_units = 0;
static int g_session_max_combined_texture_units = 0;
static int g_session_max_samples = 0;
// Multisampling parameters (critical for VM detection!)
static int g_session_samples = 0;
static int g_session_sample_buffers = 0;
static int g_session_integers_loaded = 0;

// Enable/disable spoofing
static int g_spoofing_enabled = 1;
// DISABLED: Pixel normalization now handled by smart edge-only noise in JavaScript
// Native-level noise was causing 2.5x PNG size overhead
static int g_pixel_normalization_enabled = 0;
static uint64_t g_pixel_seed = 0x12345678DEADBEEFULL;
static int g_pixel_quantization_bits = 0;  // Disabled - JS handles this now

// Debug/verbose logging (enable with OWL_GPU_DEBUG=1)
static int g_debug_enabled = 0;
static int g_call_count = 0;  // Track number of GL calls for context analysis

// ============================================================================
// Per-Context GPU API (dynamically loaded from main binary)
// ============================================================================

typedef const char* (*owl_gpu_get_string_fn)(void);
typedef int (*owl_gpu_is_spoofing_enabled_fn)(void);
typedef int (*owl_gpu_get_integer_fn)(unsigned int pname, int* value);
typedef int (*owl_gpu_get_float_fn)(unsigned int pname, float* value);
typedef int (*owl_gpu_get_shader_precision_fn)(unsigned int shader_type, unsigned int precision_type,
                                                int* range, int* precision);

static owl_gpu_get_string_fn fn_owl_gpu_get_vendor = NULL;
static owl_gpu_get_string_fn fn_owl_gpu_get_renderer = NULL;
static owl_gpu_get_string_fn fn_owl_gpu_get_version = NULL;
static owl_gpu_get_string_fn fn_owl_gpu_get_glsl_version = NULL;
static owl_gpu_is_spoofing_enabled_fn fn_owl_gpu_is_spoofing_enabled = NULL;
static owl_gpu_get_integer_fn fn_owl_gpu_get_integer = NULL;
static owl_gpu_get_float_fn fn_owl_gpu_get_float = NULL;
static owl_gpu_get_shader_precision_fn fn_owl_gpu_get_shader_precision = NULL;
static int g_api_available = 0;

// ============================================================================
// GL Constants
// ============================================================================

#define GL_VENDOR                     0x1F00
#define GL_RENDERER                   0x1F01
#define GL_VERSION                    0x1F02
#define GL_SHADING_LANGUAGE_VERSION   0x8B8C
#define GL_EXTENSIONS                 0x1F03
#define GL_REQUESTABLE_EXTENSIONS_ANGLE 0x93A8  // ANGLE-specific extension query

#define GL_RGBA                       0x1908
#define GL_UNSIGNED_BYTE              0x1401

#define GL_VERTEX_SHADER              0x8B31
#define GL_FRAGMENT_SHADER            0x8B30
#define GL_LOW_FLOAT                  0x8DF0
#define GL_MEDIUM_FLOAT               0x8DF1
#define GL_HIGH_FLOAT                 0x8DF2
#define GL_LOW_INT                    0x8DF3
#define GL_MEDIUM_INT                 0x8DF4
#define GL_HIGH_INT                   0x8DF5

// WebGL integer parameter constants
#define GL_MAX_TEXTURE_SIZE           0x0D33
#define GL_MAX_CUBE_MAP_TEXTURE_SIZE  0x851C
#define GL_MAX_RENDERBUFFER_SIZE      0x84E8
#define GL_MAX_VERTEX_ATTRIBS_GL      0x8869
#define GL_MAX_VERTEX_UNIFORM_VECTORS 0x8DFB
#define GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS 0x8B4C
#define GL_MAX_VARYING_VECTORS        0x8DFC
#define GL_MAX_FRAGMENT_UNIFORM_VECTORS 0x8DFD
#define GL_MAX_TEXTURE_IMAGE_UNITS    0x8872
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D
#define GL_MAX_SAMPLES_GL             0x8D57
// Multisampling parameters (critical for VM detection!)
#define GL_SAMPLES_GL                 0x80A9
#define GL_SAMPLE_BUFFERS_GL          0x80A8

// GL types
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef unsigned char GLubyte;
typedef unsigned char GLboolean;
typedef char GLchar;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef double GLdouble;
typedef void GLvoid;
typedef signed long int GLsizeiptr;
typedef signed long int GLintptr;

// ============================================================================
// Debug Logging Implementation (after type definitions)
// ============================================================================

// Debug logging - only logs when OWL_GPU_DEBUG=1
static void owl_debug(const char* fmt, ...) {
    if (!g_debug_enabled) return;

    pthread_mutex_lock(&g_log_mutex);

    if (!g_log_file) {
        g_log_file = fopen("/tmp/owl_angle_wrapper.log", "a");
        if (g_log_file) {
            setbuf(g_log_file, NULL);
        }
    }

    if (g_log_file) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        struct tm* tm_info = localtime(&tv.tv_sec);
        fprintf(g_log_file, "[%02d:%02d:%02d.%03d] [PID:%d] [DEBUG] ",
                tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
                (int)(tv.tv_usec / 1000), getpid());

        va_list args;
        va_start(args, fmt);
        vfprintf(g_log_file, fmt, args);
        va_end(args);

        fprintf(g_log_file, "\n");
        fflush(g_log_file);
    }

    pthread_mutex_unlock(&g_log_mutex);
}

// Helper to convert GL enum to string for debugging
static const char* gl_enum_to_string(GLenum val) {
    switch (val) {
        case 0x1F00: return "GL_VENDOR";
        case 0x1F01: return "GL_RENDERER";
        case 0x1F02: return "GL_VERSION";
        case 0x8B8C: return "GL_SHADING_LANGUAGE_VERSION";
        case 0x1F03: return "GL_EXTENSIONS";
        case 0x93A8: return "GL_REQUESTABLE_EXTENSIONS_ANGLE";
        // Common glGetIntegerv parameters
        case 0x0D33: return "GL_MAX_TEXTURE_SIZE";
        case 0x8869: return "GL_MAX_VERTEX_ATTRIBS";
        case 0x8872: return "GL_MAX_TEXTURE_IMAGE_UNITS";
        case 0x8B4A: return "GL_MAX_VERTEX_UNIFORM_VECTORS";
        case 0x8DFD: return "GL_MAX_FRAGMENT_UNIFORM_VECTORS";
        case 0x8B4B: return "GL_MAX_VARYING_VECTORS";
        case 0x0D3A: return "GL_MAX_VIEWPORT_DIMS";
        case 0x80A9: return "GL_SAMPLES";
        case 0x8D57: return "GL_MAX_SAMPLES";
        case 0x84E8: return "GL_MAX_RENDERBUFFER_SIZE";
        case 0x8824: return "GL_MAX_DRAW_BUFFERS";
        case 0x88FF: return "GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS";
        case 0x8CDF: return "GL_MAX_COLOR_ATTACHMENTS";
        default: {
            static char buf[32];
            snprintf(buf, sizeof(buf), "0x%04X", val);
            return buf;
        }
    }
}

// ============================================================================
// Original Function Pointers
// ============================================================================

static void* g_original_lib = NULL;
static pthread_once_t g_init_once = PTHREAD_ONCE_INIT;

// Function pointer types
typedef const GLubyte* (*glGetString_t)(GLenum name);
typedef void (*glGetIntegerv_t)(GLenum pname, GLint* params);
typedef void (*glGetFloatv_t)(GLenum pname, float* params);
typedef void (*glReadPixels_t)(GLint x, GLint y, GLsizei width, GLsizei height,
                                GLenum format, GLenum type, void* pixels);
typedef void (*glGetShaderPrecisionFormat_t)(GLenum shadertype, GLenum precisiontype,
                                              GLint* range, GLint* precision);

// Original function pointers
static glGetString_t orig_glGetString = NULL;
static glGetIntegerv_t orig_glGetIntegerv = NULL;
static glGetFloatv_t orig_glGetFloatv = NULL;
static glReadPixels_t orig_glReadPixels = NULL;
static glGetShaderPrecisionFormat_t orig_glGetShaderPrecisionFormat = NULL;

// ============================================================================
// Initialization
// ============================================================================

static void load_per_context_api(void) {
    // Try to load per-context GPU API from main binary
    // These functions are exported by the browser and allow per-context GPU spoofing
    fn_owl_gpu_get_vendor = (owl_gpu_get_string_fn)dlsym(RTLD_DEFAULT, "owl_gpu_get_vendor");
    fn_owl_gpu_get_renderer = (owl_gpu_get_string_fn)dlsym(RTLD_DEFAULT, "owl_gpu_get_renderer");
    fn_owl_gpu_get_version = (owl_gpu_get_string_fn)dlsym(RTLD_DEFAULT, "owl_gpu_get_version");
    fn_owl_gpu_get_glsl_version = (owl_gpu_get_string_fn)dlsym(RTLD_DEFAULT, "owl_gpu_get_glsl_version");
    fn_owl_gpu_is_spoofing_enabled = (owl_gpu_is_spoofing_enabled_fn)dlsym(RTLD_DEFAULT, "owl_gpu_is_spoofing_enabled");

    // Load extended parameter API functions
    fn_owl_gpu_get_integer = (owl_gpu_get_integer_fn)dlsym(RTLD_DEFAULT, "owl_gpu_get_integer");
    fn_owl_gpu_get_float = (owl_gpu_get_float_fn)dlsym(RTLD_DEFAULT, "owl_gpu_get_float");
    fn_owl_gpu_get_shader_precision = (owl_gpu_get_shader_precision_fn)dlsym(RTLD_DEFAULT, "owl_gpu_get_shader_precision");

    // API is available if we found at least the getter functions
    g_api_available = (fn_owl_gpu_get_vendor != NULL && fn_owl_gpu_get_renderer != NULL);
}

static void load_config(void) {
    // Check for debug mode via environment variable
    const char* env_debug = getenv("OWL_GPU_DEBUG");
    if (env_debug && strcmp(env_debug, "1") == 0) {
        g_debug_enabled = 1;
        owl_log("DEBUG MODE ENABLED - verbose logging active");
    }

    // First, try to load the per-context API
    load_per_context_api();
    owl_debug("Per-context API available: %s", g_api_available ? "YES" : "NO");

    // Load session-wide spoofed values from environment (fallback for per-context)
    const char* env_vendor = getenv("OWL_GPU_VENDOR");
    const char* env_renderer = getenv("OWL_GPU_RENDERER");
    const char* env_version = getenv("OWL_GPU_VERSION");
    const char* env_glsl = getenv("OWL_GPU_GLSL_VERSION");
    const char* env_enabled = getenv("OWL_GPU_SPOOF_ENABLED");
    const char* env_pixel_norm = getenv("OWL_GPU_PIXEL_NORM");
    const char* env_seed = getenv("OWL_GPU_PIXEL_SEED");

    strncpy(g_session_vendor, env_vendor ? env_vendor : DEFAULT_VENDOR, sizeof(g_session_vendor) - 1);

    // Load target platform for ANGLE backend selection (Linux uses OpenGL, Windows uses D3D11)
    const char* env_platform = getenv("OWL_GPU_PLATFORM");
    if (env_platform) {
        strncpy(g_target_platform, env_platform, sizeof(g_target_platform) - 1);
    }

    // Format renderer into ANGLE format if not already (fixes first-context WebGL for Apple profiles)
    const char* raw_renderer = env_renderer ? env_renderer : DEFAULT_RENDERER;
    format_renderer_to_angle(raw_renderer, g_session_vendor, g_session_renderer, sizeof(g_session_renderer));

    strncpy(g_session_version, env_version ? env_version : DEFAULT_VERSION, sizeof(g_session_version) - 1);
    strncpy(g_session_glsl_version, env_glsl ? env_glsl : DEFAULT_GLSL_VERSION, sizeof(g_session_glsl_version) - 1);

    if (env_enabled && strcmp(env_enabled, "0") == 0) {
        g_spoofing_enabled = 0;
        owl_log("GPU spoofing DISABLED via OWL_GPU_SPOOF_ENABLED=0");
    }

    if (env_pixel_norm && strcmp(env_pixel_norm, "0") == 0) {
        g_pixel_normalization_enabled = 0;
    }

    if (env_seed) {
        g_pixel_seed = strtoull(env_seed, NULL, 0);
    }

    // Load pixel quantization setting (OWL_GPU_PIXEL_QUANT=0-6, default 2)
    const char* env_quant = getenv("OWL_GPU_PIXEL_QUANT");
    if (env_quant) {
        int quant = atoi(env_quant);
        if (quant >= 0 && quant <= 6) {
            g_pixel_quantization_bits = quant;
        }
    }

    // Load shader precision values from environment (format: "range_min,range_max,precision")
    // Helper macro to parse precision format
    #define PARSE_PRECISION(env_name, target) do { \
        const char* env = getenv(env_name); \
        if (env) { \
            if (sscanf(env, "%d,%d,%d", &target[0], &target[1], &target[2]) == 3) { \
                g_session_precision_loaded = 1; \
            } \
        } \
    } while(0)

    PARSE_PRECISION("OWL_GPU_VERTEX_HIGH_FLOAT", g_session_vertex_high_float);
    PARSE_PRECISION("OWL_GPU_VERTEX_MEDIUM_FLOAT", g_session_vertex_medium_float);
    PARSE_PRECISION("OWL_GPU_VERTEX_LOW_FLOAT", g_session_vertex_low_float);
    PARSE_PRECISION("OWL_GPU_FRAGMENT_HIGH_FLOAT", g_session_fragment_high_float);
    PARSE_PRECISION("OWL_GPU_FRAGMENT_MEDIUM_FLOAT", g_session_fragment_medium_float);
    PARSE_PRECISION("OWL_GPU_FRAGMENT_LOW_FLOAT", g_session_fragment_low_float);
    PARSE_PRECISION("OWL_GPU_VERTEX_HIGH_INT", g_session_vertex_high_int);
    PARSE_PRECISION("OWL_GPU_VERTEX_MEDIUM_INT", g_session_vertex_medium_int);
    PARSE_PRECISION("OWL_GPU_VERTEX_LOW_INT", g_session_vertex_low_int);
    PARSE_PRECISION("OWL_GPU_FRAGMENT_HIGH_INT", g_session_fragment_high_int);
    PARSE_PRECISION("OWL_GPU_FRAGMENT_MEDIUM_INT", g_session_fragment_medium_int);
    PARSE_PRECISION("OWL_GPU_FRAGMENT_LOW_INT", g_session_fragment_low_int);

    #undef PARSE_PRECISION

    // Load WebGL integer parameters from environment
    #define PARSE_INT(env_name, target) do { \
        const char* env = getenv(env_name); \
        if (env) { \
            target = atoi(env); \
            g_session_integers_loaded = 1; \
        } \
    } while(0)

    PARSE_INT("OWL_GPU_MAX_TEXTURE_SIZE", g_session_max_texture_size);
    PARSE_INT("OWL_GPU_MAX_CUBE_MAP_TEXTURE_SIZE", g_session_max_cube_map_texture_size);
    PARSE_INT("OWL_GPU_MAX_RENDER_BUFFER_SIZE", g_session_max_render_buffer_size);
    PARSE_INT("OWL_GPU_MAX_VERTEX_ATTRIBS", g_session_max_vertex_attribs);
    PARSE_INT("OWL_GPU_MAX_VERTEX_UNIFORM_VECTORS", g_session_max_vertex_uniform_vectors);
    PARSE_INT("OWL_GPU_MAX_VERTEX_TEXTURE_UNITS", g_session_max_vertex_texture_units);
    PARSE_INT("OWL_GPU_MAX_VARYING_VECTORS", g_session_max_varying_vectors);
    PARSE_INT("OWL_GPU_MAX_FRAGMENT_UNIFORM_VECTORS", g_session_max_fragment_uniform_vectors);
    PARSE_INT("OWL_GPU_MAX_TEXTURE_UNITS", g_session_max_texture_units);
    PARSE_INT("OWL_GPU_MAX_COMBINED_TEXTURE_UNITS", g_session_max_combined_texture_units);
    PARSE_INT("OWL_GPU_MAX_SAMPLES", g_session_max_samples);
    // Multisampling parameters (critical for VM detection!)
    PARSE_INT("OWL_GPU_SAMPLES", g_session_samples);
    PARSE_INT("OWL_GPU_SAMPLE_BUFFERS", g_session_sample_buffers);

    #undef PARSE_INT

    if (g_session_precision_loaded) {
        owl_debug("  Session shader precision loaded:");
        owl_debug("    vertex_high_float=[%d,%d,%d]",
                  g_session_vertex_high_float[0], g_session_vertex_high_float[1], g_session_vertex_high_float[2]);
        owl_debug("    vertex_high_int=[%d,%d,%d]",
                  g_session_vertex_high_int[0], g_session_vertex_high_int[1], g_session_vertex_high_int[2]);
        owl_debug("    fragment_high_int=[%d,%d,%d]",
                  g_session_fragment_high_int[0], g_session_fragment_high_int[1], g_session_fragment_high_int[2]);
    } else {
        owl_debug("  Session shader precision NOT loaded (env vars missing)");
        // Debug: check what env vars are set
        const char* vhf = getenv("OWL_GPU_VERTEX_HIGH_FLOAT");
        const char* vhi = getenv("OWL_GPU_VERTEX_HIGH_INT");
        owl_debug("    OWL_GPU_VERTEX_HIGH_FLOAT = %s", vhf ? vhf : "(null)");
        owl_debug("    OWL_GPU_VERTEX_HIGH_INT = %s", vhi ? vhi : "(null)");
    }
    if (g_session_integers_loaded) {
        owl_debug("  Session integers loaded: max_texture_size=%d, max_varying_vectors=%d",
                  g_session_max_texture_size, g_session_max_varying_vectors);
    }
    // Always log multisampling params for debugging VM detection
    owl_debug("  MULTISAMPLING DEBUG: samples=%d, sample_buffers=%d, max_samples=%d",
              g_session_samples, g_session_sample_buffers, g_session_max_samples);
    const char* env_samples = getenv("OWL_GPU_SAMPLES");
    const char* env_sample_buffers = getenv("OWL_GPU_SAMPLE_BUFFERS");
    owl_debug("  ENV: OWL_GPU_SAMPLES=%s, OWL_GPU_SAMPLE_BUFFERS=%s",
              env_samples ? env_samples : "(null)",
              env_sample_buffers ? env_sample_buffers : "(null)");

    // Log configuration
    owl_debug("Configuration loaded:");
    owl_debug("  Spoofing enabled: %s", g_spoofing_enabled ? "YES" : "NO");
    owl_debug("  Pixel normalization: %s", g_pixel_normalization_enabled ? "YES" : "NO");
    owl_debug("  Pixel quantization bits: %d", g_pixel_quantization_bits);
    owl_debug("  Session vendor: %s", g_session_vendor);
    owl_debug("  Session renderer: %s", g_session_renderer);
    owl_debug("  ANGLE version: %s", ANGLE_VERSION);
    owl_debug("  ANGLE GLSL version: %s", ANGLE_GLSL_VERSION);
}

static void init_wrapper(void) {
    // Load configuration
    load_config();

    owl_debug("Initializing ANGLE wrapper...");

    // Find and load the original ANGLE library
#ifdef __APPLE__
    // On macOS, the original is renamed to libGLESv2_original.dylib
    // It should be in the same directory as our wrapper

    // Get the path to our wrapper library
    Dl_info info;
    if (dladdr((void*)init_wrapper, &info) && info.dli_fname) {
        char original_path[1024];
        strncpy(original_path, info.dli_fname, sizeof(original_path) - 1);
        owl_debug("Wrapper loaded from: %s", info.dli_fname);

        // Replace libGLESv2.dylib with libGLESv2_original.dylib
        char* last_slash = strrchr(original_path, '/');
        if (last_slash) {
            strcpy(last_slash + 1, "libGLESv2_original.dylib");
        }

        owl_debug("Loading original ANGLE from: %s", original_path);
        g_original_lib = dlopen(original_path, RTLD_NOW | RTLD_LOCAL);
        if (!g_original_lib) {
            owl_log("FATAL: dlopen failed: %s", dlerror());
        }
    } else {
        owl_log("FATAL: Could not get wrapper library path");
    }
#else
    // On Linux, the original is renamed to libGLESv2_original.so
    Dl_info info;
    if (dladdr((void*)init_wrapper, &info) && info.dli_fname) {
        char original_path[1024];
        strncpy(original_path, info.dli_fname, sizeof(original_path) - 1);
        owl_debug("Wrapper loaded from: %s", info.dli_fname);

        char* last_slash = strrchr(original_path, '/');
        if (last_slash) {
            strcpy(last_slash + 1, "libGLESv2_original.so");
        }

        owl_debug("Loading original ANGLE from: %s", original_path);
        g_original_lib = dlopen(original_path, RTLD_NOW | RTLD_LOCAL);
        if (!g_original_lib) {
            owl_log("FATAL: dlopen failed: %s", dlerror());
        }
    }
#endif

    if (!g_original_lib) {
        owl_log("FATAL: Could not load original ANGLE library");
        return;
    }

    // Load original function pointers
    orig_glGetString = (glGetString_t)dlsym(g_original_lib, "glGetString");
    orig_glGetIntegerv = (glGetIntegerv_t)dlsym(g_original_lib, "glGetIntegerv");
    orig_glGetFloatv = (glGetFloatv_t)dlsym(g_original_lib, "glGetFloatv");
    orig_glReadPixels = (glReadPixels_t)dlsym(g_original_lib, "glReadPixels");
    orig_glGetShaderPrecisionFormat = (glGetShaderPrecisionFormat_t)dlsym(g_original_lib, "glGetShaderPrecisionFormat");

    owl_debug("Function pointers loaded:");
    owl_debug("  orig_glGetString: %p", (void*)orig_glGetString);
    owl_debug("  orig_glGetIntegerv: %p", (void*)orig_glGetIntegerv);
    owl_debug("  orig_glGetFloatv: %p", (void*)orig_glGetFloatv);
    owl_debug("  orig_glReadPixels: %p", (void*)orig_glReadPixels);
    owl_debug("  orig_glGetShaderPrecisionFormat: %p", (void*)orig_glGetShaderPrecisionFormat);

    owl_log("Initialized successfully");
}

static void ensure_initialized(void) {
    pthread_once(&g_init_once, init_wrapper);
}

// ============================================================================
// Extension Filtering (for fingerprint resistance)
// ============================================================================

// Buffer for filtered extensions string
static char g_filtered_extensions[16384] = {0};
static int g_extensions_filtered = 0;

// Apple-specific extension prefixes to filter out when not spoofing as Apple
static const char* APPLE_EXTENSION_PREFIXES[] = {
    "GL_APPLE_",
    NULL
};

// Check if an extension should be filtered based on current spoofed vendor
static int should_filter_extension(const char* ext, const char* vendor) {
    if (!ext || !vendor) return 0;

    // If we're spoofing as Apple, don't filter Apple extensions
    if (strstr(vendor, "Apple") != NULL) {
        return 0;
    }

    // Filter Apple extensions when not spoofing as Apple
    for (int i = 0; APPLE_EXTENSION_PREFIXES[i] != NULL; i++) {
        if (strncmp(ext, APPLE_EXTENSION_PREFIXES[i], strlen(APPLE_EXTENSION_PREFIXES[i])) == 0) {
            return 1;
        }
    }

    return 0;
}

// Filter extensions string to remove vendor-specific extensions
static const char* filter_extensions(const char* original, const char* vendor) {
    if (!original || !vendor) return original;

    // Clear the buffer
    g_filtered_extensions[0] = '\0';
    size_t out_pos = 0;
    size_t max_len = sizeof(g_filtered_extensions) - 1;

    // Parse and filter extensions (space-separated)
    const char* start = original;
    const char* p = original;
    int filtered_count = 0;
    int total_count = 0;

    while (*p) {
        // Find end of current extension (space or null)
        while (*p && *p != ' ') p++;

        size_t ext_len = p - start;
        if (ext_len > 0) {
            total_count++;

            // Create temporary null-terminated string for the extension
            char ext[256];
            if (ext_len < sizeof(ext)) {
                memcpy(ext, start, ext_len);
                ext[ext_len] = '\0';

                if (!should_filter_extension(ext, vendor)) {
                    // Add to output (with space separator if not first)
                    if (out_pos > 0 && out_pos < max_len) {
                        g_filtered_extensions[out_pos++] = ' ';
                    }

                    // Copy extension
                    size_t copy_len = ext_len;
                    if (out_pos + copy_len > max_len) {
                        copy_len = max_len - out_pos;
                    }
                    memcpy(g_filtered_extensions + out_pos, start, copy_len);
                    out_pos += copy_len;
                } else {
                    filtered_count++;
                }
            }
        }

        // Skip spaces
        while (*p == ' ') p++;
        start = p;
    }

    g_filtered_extensions[out_pos] = '\0';
    g_extensions_filtered = 1;

    return g_filtered_extensions;
}

// ============================================================================
// Pixel Normalization (for fingerprint resistance)
// ============================================================================

// Simple xorshift64 PRNG for deterministic noise
static uint64_t xorshift64(uint64_t* state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static void normalize_pixels(void* pixels, GLsizei width, GLsizei height,
                             GLenum format, GLenum type) {
    if (!pixels || width <= 0 || height <= 0) return;
    if (!g_pixel_normalization_enabled) return;

    // Only handle RGBA/UNSIGNED_BYTE (most common WebGL readback)
    if (format != GL_RGBA || type != GL_UNSIGNED_BYTE) return;

    uint8_t* data = (uint8_t*)pixels;
    size_t pixel_count = (size_t)width * (size_t)height;

    // Use seed for deterministic noise
    uint64_t rng_state = g_pixel_seed;

    // Calculate quantization parameters
    // quantization_bits=0: no quantization (256 levels)
    // quantization_bits=2: 64 levels (shift right 2, then left 2)
    // quantization_bits=4: 16 levels (more aggressive, may be visible)
    int shift = g_pixel_quantization_bits;
    int half_step = (shift > 0) ? (1 << (shift - 1)) : 0;  // For rounding

    for (size_t i = 0; i < pixel_count * 4; i++) {
        int val = (int)data[i];

        // Step 1: Quantization - reduces precision to hide subtle OS-specific
        // rendering differences (anti-aliasing patterns, sub-pixel rendering)
        // This makes different OS renderings converge to the same quantized values
        if (shift > 0) {
            // Add half-step for rounding, then quantize
            val = ((val + half_step) >> shift) << shift;
        }

        // Step 2: Add small noise to change the hash while preserving
        // the quantized structure. Noise range is smaller than quantization step
        // so it doesn't undo the convergence effect
        int noise_range = (shift > 0) ? (1 << shift) : 5;  // Noise within quantization step
        int noise = (int)(xorshift64(&rng_state) % noise_range) - (noise_range / 2);
        val += noise;

        // Clamp to valid range
        if (val < 0) val = 0;
        if (val > 255) val = 255;
        data[i] = (uint8_t)val;
    }
}

// ============================================================================
// Intercepted GL Functions
// ============================================================================

// Helper to get spoofed string with per-context -> session -> default fallback
// Static buffer for formatted per-context renderer
static char g_formatted_ctx_renderer[512] = {0};

static const char* get_spoofed_string(GLenum name) {
    const char* result = NULL;

    // NOTE: We do NOT spoof GL_VERSION or GL_SHADING_LANGUAGE_VERSION!
    // ANGLE returns different versions for ES 2.0 (WebGL1) vs ES 3.0 (WebGL2) contexts.
    // If we always return "OpenGL ES 3.0", WebGL1 context creation fails because
    // Chromium validates that the version matches the requested context type.
    // Let ANGLE return the correct version for each context type.

    // For VENDOR and RENDERER, use per-context -> session -> default fallback
    // Priority 1: Per-context API (if available and returns non-NULL)
    if (g_api_available) {
        switch (name) {
            case GL_VENDOR:
                if (fn_owl_gpu_get_vendor) result = fn_owl_gpu_get_vendor();
                break;
            case GL_RENDERER:
                if (fn_owl_gpu_get_renderer) {
                    const char* raw_renderer = fn_owl_gpu_get_renderer();
                    if (raw_renderer) {
                        // Format to ANGLE format if not already
                        const char* vendor = fn_owl_gpu_get_vendor ? fn_owl_gpu_get_vendor() : g_session_vendor;
                        format_renderer_to_angle(raw_renderer, vendor, g_formatted_ctx_renderer, sizeof(g_formatted_ctx_renderer));
                        result = g_formatted_ctx_renderer;
                    }
                }
                break;
        }
        if (result) return result;
    }

    // Priority 2: Session-wide values (from env vars)
    switch (name) {
        case GL_VENDOR:
            return g_session_vendor;
        case GL_RENDERER:
            return g_session_renderer;
    }

    return NULL;
}

// Our hooked glGetString implementation
static const GLubyte* hooked_glGetString(GLenum name) {
    ensure_initialized();
    g_call_count++;

    owl_debug("glGetString(%s) called [call #%d]", gl_enum_to_string(name), g_call_count);

    if (g_spoofing_enabled) {
        const char* spoofed = get_spoofed_string(name);
        if (spoofed) {
            owl_debug("  -> SPOOFED: %s", spoofed);
            return (const GLubyte*)spoofed;
        }

        // Handle GL_EXTENSIONS and GL_REQUESTABLE_EXTENSIONS_ANGLE - filter out vendor-specific extensions
        if ((name == GL_EXTENSIONS || name == GL_REQUESTABLE_EXTENSIONS_ANGLE) && orig_glGetString) {
            const GLubyte* original = orig_glGetString(name);
            if (original) {
                // Get current vendor for filtering decision
                const char* vendor = g_session_vendor;
                if (g_api_available && fn_owl_gpu_get_vendor) {
                    const char* ctx_vendor = fn_owl_gpu_get_vendor();
                    if (ctx_vendor) vendor = ctx_vendor;
                }

                const char* filtered = filter_extensions((const char*)original, vendor);
                owl_debug("  -> FILTERED extensions (vendor=%s)", vendor);
                return (const GLubyte*)filtered;
            }
        }
    }

    if (orig_glGetString) {
        const GLubyte* result = orig_glGetString(name);
        owl_debug("  -> ORIGINAL: %s", result ? (const char*)result : "(null)");
        return result;
    }
    owl_debug("  -> ERROR: orig_glGetString is NULL!");
    return NULL;
}

// Export our function with the standard name for direct linking
// Use visibility("default") to ensure it's exported and can override the original
__attribute__((visibility("default")))
const GLubyte* glGetString(GLenum name) {
    return hooked_glGetString(name);
}

// Helper to get session-level integer value (env var fallback)
static int get_session_integer(GLenum pname, int* value) {
    if (!g_session_integers_loaded || !value) return 0;

    switch (pname) {
        case GL_MAX_TEXTURE_SIZE:
            if (g_session_max_texture_size > 0) { *value = g_session_max_texture_size; return 1; }
            break;
        case GL_MAX_CUBE_MAP_TEXTURE_SIZE:
            if (g_session_max_cube_map_texture_size > 0) { *value = g_session_max_cube_map_texture_size; return 1; }
            break;
        case GL_MAX_RENDERBUFFER_SIZE:
            if (g_session_max_render_buffer_size > 0) { *value = g_session_max_render_buffer_size; return 1; }
            break;
        case GL_MAX_VERTEX_ATTRIBS_GL:
            if (g_session_max_vertex_attribs > 0) { *value = g_session_max_vertex_attribs; return 1; }
            break;
        case GL_MAX_VERTEX_UNIFORM_VECTORS:
            if (g_session_max_vertex_uniform_vectors > 0) { *value = g_session_max_vertex_uniform_vectors; return 1; }
            break;
        case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:
            if (g_session_max_vertex_texture_units > 0) { *value = g_session_max_vertex_texture_units; return 1; }
            break;
        case GL_MAX_VARYING_VECTORS:
            if (g_session_max_varying_vectors > 0) { *value = g_session_max_varying_vectors; return 1; }
            break;
        case GL_MAX_FRAGMENT_UNIFORM_VECTORS:
            if (g_session_max_fragment_uniform_vectors > 0) { *value = g_session_max_fragment_uniform_vectors; return 1; }
            break;
        case GL_MAX_TEXTURE_IMAGE_UNITS:
            if (g_session_max_texture_units > 0) { *value = g_session_max_texture_units; return 1; }
            break;
        case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
            if (g_session_max_combined_texture_units > 0) { *value = g_session_max_combined_texture_units; return 1; }
            break;
        case GL_MAX_SAMPLES_GL:
            if (g_session_max_samples > 0) { *value = g_session_max_samples; return 1; }
            break;
        // Multisampling parameters (critical for VM detection!)
        case GL_SAMPLES_GL:
            if (g_session_samples > 0) { *value = g_session_samples; return 1; }
            break;
        case GL_SAMPLE_BUFFERS_GL:
            if (g_session_sample_buffers > 0) { *value = g_session_sample_buffers; return 1; }
            break;
    }
    return 0;
}

__attribute__((visibility("default")))
void glGetIntegerv(GLenum pname, GLint* params) {
    ensure_initialized();
    g_call_count++;

    if (!params) {
        owl_debug("glGetIntegerv(%s) called with NULL params [call #%d]", gl_enum_to_string(pname), g_call_count);
        return;
    }

    // Try to get spoofed value from per-context API first
    if (g_spoofing_enabled && fn_owl_gpu_get_integer) {
        int spoofed_value;
        if (fn_owl_gpu_get_integer(pname, &spoofed_value)) {
            *params = spoofed_value;
            owl_debug("glGetIntegerv(%s) -> SPOOFED (per-ctx): %d [call #%d]", gl_enum_to_string(pname), spoofed_value, g_call_count);
            return;
        }
    }

    // Try session-level fallback (from env vars, for multi-process GPU spoofing)
    if (g_spoofing_enabled) {
        int session_value;
        if (get_session_integer(pname, &session_value)) {
            *params = session_value;
            owl_debug("glGetIntegerv(%s) -> SPOOFED (session): %d [call #%d]", gl_enum_to_string(pname), session_value, g_call_count);
            return;
        }
    }

    // Fall back to original implementation
    if (orig_glGetIntegerv) {
        orig_glGetIntegerv(pname, params);
        owl_debug("glGetIntegerv(%s) -> ORIGINAL: %d [call #%d]", gl_enum_to_string(pname), *params, g_call_count);
    } else {
        owl_debug("glGetIntegerv(%s) -> ERROR: orig_glGetIntegerv is NULL! [call #%d]", gl_enum_to_string(pname), g_call_count);
    }
}

__attribute__((visibility("default")))
void glGetFloatv(GLenum pname, float* params) {
    ensure_initialized();

    if (!params) return;

    // Try to get spoofed value from per-context API first
    if (g_spoofing_enabled && fn_owl_gpu_get_float) {
        float spoofed_value;
        if (fn_owl_gpu_get_float(pname, &spoofed_value)) {
            *params = spoofed_value;
            return;
        }
    }

    // Fall back to original implementation
    if (orig_glGetFloatv) {
        orig_glGetFloatv(pname, params);
    }
}

__attribute__((visibility("default")))
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, void* pixels) {
    ensure_initialized();
    g_call_count++;

    owl_debug("glReadPixels(x=%d, y=%d, w=%d, h=%d, format=0x%04X, type=0x%04X) [call #%d]",
              x, y, width, height, format, type, g_call_count);

    if (orig_glReadPixels) {
        orig_glReadPixels(x, y, width, height, format, type, pixels);
    }

    // Apply pixel normalization for fingerprint resistance
    if (g_pixel_normalization_enabled && format == GL_RGBA && type == GL_UNSIGNED_BYTE) {
        owl_debug("  -> Applying pixel normalization (quant_bits=%d) to %dx%d pixels",
                  g_pixel_quantization_bits, width, height);
    }
    normalize_pixels(pixels, width, height, format, type);
}

// Helper to get session-level shader precision (env var fallback)
static int get_session_shader_precision(GLenum shadertype, GLenum precisiontype,
                                         int* range, int* precision) {
    if (!g_session_precision_loaded || !range || !precision) return 0;

    const int* src = NULL;

    if (shadertype == GL_VERTEX_SHADER) {
        switch (precisiontype) {
            case GL_HIGH_FLOAT:   src = g_session_vertex_high_float; break;
            case GL_MEDIUM_FLOAT: src = g_session_vertex_medium_float; break;
            case GL_LOW_FLOAT:    src = g_session_vertex_low_float; break;
            case GL_HIGH_INT:     src = g_session_vertex_high_int; break;
            case GL_MEDIUM_INT:   src = g_session_vertex_medium_int; break;
            case GL_LOW_INT:      src = g_session_vertex_low_int; break;
        }
    } else if (shadertype == GL_FRAGMENT_SHADER) {
        switch (precisiontype) {
            case GL_HIGH_FLOAT:   src = g_session_fragment_high_float; break;
            case GL_MEDIUM_FLOAT: src = g_session_fragment_medium_float; break;
            case GL_LOW_FLOAT:    src = g_session_fragment_low_float; break;
            case GL_HIGH_INT:     src = g_session_fragment_high_int; break;
            case GL_MEDIUM_INT:   src = g_session_fragment_medium_int; break;
            case GL_LOW_INT:      src = g_session_fragment_low_int; break;
        }
    }

    if (src && (src[0] != 0 || src[1] != 0 || src[2] != 0)) {
        range[0] = src[0];
        range[1] = src[1];
        *precision = src[2];
        return 1;
    }
    return 0;
}

__attribute__((visibility("default")))
void glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype,
                                 GLint* range, GLint* precision) {
    ensure_initialized();

    if (!range || !precision) return;

    // Try to get spoofed value from per-context API first
    if (g_spoofing_enabled && fn_owl_gpu_get_shader_precision) {
        int spoofed_range[2];
        int spoofed_precision;
        if (fn_owl_gpu_get_shader_precision(shadertype, precisiontype, spoofed_range, &spoofed_precision)) {
            range[0] = spoofed_range[0];
            range[1] = spoofed_range[1];
            *precision = spoofed_precision;
            owl_debug("glGetShaderPrecisionFormat(shader=0x%X, prec=0x%X) -> SPOOFED (per-ctx): [%d,%d]/%d",
                      shadertype, precisiontype, range[0], range[1], *precision);
            return;
        }
    }

    // Try session-level fallback (from env vars, for multi-process GPU spoofing)
    if (g_spoofing_enabled) {
        int session_range[2];
        int session_prec;
        if (get_session_shader_precision(shadertype, precisiontype, session_range, &session_prec)) {
            range[0] = session_range[0];
            range[1] = session_range[1];
            *precision = session_prec;
            owl_debug("glGetShaderPrecisionFormat(shader=0x%X, prec=0x%X) -> SPOOFED (session): [%d,%d]/%d",
                      shadertype, precisiontype, range[0], range[1], *precision);
            return;
        }
    }

    // Fall back to original implementation
    if (orig_glGetShaderPrecisionFormat) {
        orig_glGetShaderPrecisionFormat(shadertype, precisiontype, range, precision);
        owl_debug("glGetShaderPrecisionFormat(shader=0x%X, prec=0x%X) -> ORIGINAL: [%d,%d]/%d",
                  shadertype, precisiontype, range[0], range[1], *precision);
    }
}

// ============================================================================
// Forward all other GL functions to the original library
// ============================================================================

// Generic function forwarder macro
#define FORWARD_GL_FUNC(name, ret, args, call_args) \
    typedef ret (*name##_t) args; \
    static name##_t orig_##name = NULL; \
    ret name args { \
        ensure_initialized(); \
        if (!orig_##name) { \
            orig_##name = (name##_t)dlsym(g_original_lib, #name); \
        } \
        if (orig_##name) { \
            return orig_##name call_args; \
        } \
        return (ret)0; \
    }

#define FORWARD_GL_FUNC_VOID(name, args, call_args) \
    typedef void (*name##_t) args; \
    static name##_t orig_##name = NULL; \
    void name args { \
        ensure_initialized(); \
        if (!orig_##name) { \
            orig_##name = (name##_t)dlsym(g_original_lib, #name); \
        } \
        if (orig_##name) { \
            orig_##name call_args; \
        } \
    }

// ============================================================================
// Export additional commonly used GL functions
// These are forwarded directly to the original library
// ============================================================================

// We'll use dlsym for any function not explicitly defined above
// This is handled by the dynamic linker's fallback behavior

// For functions that need to be exported, we define them here
// The list below covers the most commonly used WebGL functions

FORWARD_GL_FUNC_VOID(glActiveTexture, (GLenum texture), (texture))
FORWARD_GL_FUNC_VOID(glAttachShader, (GLuint program, GLuint shader), (program, shader))
FORWARD_GL_FUNC_VOID(glBindAttribLocation, (GLuint program, GLuint index, const char* name), (program, index, name))
FORWARD_GL_FUNC_VOID(glBindBuffer, (GLenum target, GLuint buffer), (target, buffer))
FORWARD_GL_FUNC_VOID(glBindFramebuffer, (GLenum target, GLuint framebuffer), (target, framebuffer))
FORWARD_GL_FUNC_VOID(glBindRenderbuffer, (GLenum target, GLuint renderbuffer), (target, renderbuffer))
FORWARD_GL_FUNC_VOID(glBindTexture, (GLenum target, GLuint texture), (target, texture))
FORWARD_GL_FUNC_VOID(glBlendColor, (float red, float green, float blue, float alpha), (red, green, blue, alpha))
FORWARD_GL_FUNC_VOID(glBlendEquation, (GLenum mode), (mode))
FORWARD_GL_FUNC_VOID(glBlendEquationSeparate, (GLenum modeRGB, GLenum modeAlpha), (modeRGB, modeAlpha))
FORWARD_GL_FUNC_VOID(glBlendFunc, (GLenum sfactor, GLenum dfactor), (sfactor, dfactor))
FORWARD_GL_FUNC_VOID(glBlendFuncSeparate, (GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha), (srcRGB, dstRGB, srcAlpha, dstAlpha))
FORWARD_GL_FUNC_VOID(glBufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage), (target, size, data, usage))
FORWARD_GL_FUNC_VOID(glBufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void* data), (target, offset, size, data))
FORWARD_GL_FUNC(glCheckFramebufferStatus, GLenum, (GLenum target), (target))
FORWARD_GL_FUNC_VOID(glClear, (GLbitfield mask), (mask))
FORWARD_GL_FUNC_VOID(glClearColor, (float red, float green, float blue, float alpha), (red, green, blue, alpha))
FORWARD_GL_FUNC_VOID(glClearDepthf, (float depth), (depth))
FORWARD_GL_FUNC_VOID(glClearStencil, (GLint s), (s))
FORWARD_GL_FUNC_VOID(glColorMask, (GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha), (red, green, blue, alpha))
FORWARD_GL_FUNC_VOID(glCompileShader, (GLuint shader), (shader))
FORWARD_GL_FUNC_VOID(glCompressedTexImage2D, (GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void* data), (target, level, internalformat, width, height, border, imageSize, data))
FORWARD_GL_FUNC_VOID(glCompressedTexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void* data), (target, level, xoffset, yoffset, width, height, format, imageSize, data))
FORWARD_GL_FUNC_VOID(glCopyTexImage2D, (GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border), (target, level, internalformat, x, y, width, height, border))
FORWARD_GL_FUNC_VOID(glCopyTexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height), (target, level, xoffset, yoffset, x, y, width, height))
FORWARD_GL_FUNC(glCreateProgram, GLuint, (void), ())
FORWARD_GL_FUNC(glCreateShader, GLuint, (GLenum type), (type))
FORWARD_GL_FUNC_VOID(glCullFace, (GLenum mode), (mode))
FORWARD_GL_FUNC_VOID(glDeleteBuffers, (GLsizei n, const GLuint* buffers), (n, buffers))
FORWARD_GL_FUNC_VOID(glDeleteFramebuffers, (GLsizei n, const GLuint* framebuffers), (n, framebuffers))
FORWARD_GL_FUNC_VOID(glDeleteProgram, (GLuint program), (program))
FORWARD_GL_FUNC_VOID(glDeleteRenderbuffers, (GLsizei n, const GLuint* renderbuffers), (n, renderbuffers))
FORWARD_GL_FUNC_VOID(glDeleteShader, (GLuint shader), (shader))
FORWARD_GL_FUNC_VOID(glDeleteTextures, (GLsizei n, const GLuint* textures), (n, textures))
FORWARD_GL_FUNC_VOID(glDepthFunc, (GLenum func), (func))
FORWARD_GL_FUNC_VOID(glDepthMask, (GLubyte flag), (flag))
FORWARD_GL_FUNC_VOID(glDepthRangef, (float nearVal, float farVal), (nearVal, farVal))
FORWARD_GL_FUNC_VOID(glDetachShader, (GLuint program, GLuint shader), (program, shader))
FORWARD_GL_FUNC_VOID(glDisable, (GLenum cap), (cap))
FORWARD_GL_FUNC_VOID(glDisableVertexAttribArray, (GLuint index), (index))
FORWARD_GL_FUNC_VOID(glDrawArrays, (GLenum mode, GLint first, GLsizei count), (mode, first, count))
FORWARD_GL_FUNC_VOID(glDrawElements, (GLenum mode, GLsizei count, GLenum type, const void* indices), (mode, count, type, indices))
FORWARD_GL_FUNC_VOID(glEnable, (GLenum cap), (cap))
FORWARD_GL_FUNC_VOID(glEnableVertexAttribArray, (GLuint index), (index))
FORWARD_GL_FUNC_VOID(glFinish, (void), ())
FORWARD_GL_FUNC_VOID(glFlush, (void), ())
FORWARD_GL_FUNC_VOID(glFramebufferRenderbuffer, (GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer), (target, attachment, renderbuffertarget, renderbuffer))
FORWARD_GL_FUNC_VOID(glFramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level), (target, attachment, textarget, texture, level))
FORWARD_GL_FUNC_VOID(glFrontFace, (GLenum mode), (mode))
FORWARD_GL_FUNC_VOID(glGenBuffers, (GLsizei n, GLuint* buffers), (n, buffers))
FORWARD_GL_FUNC_VOID(glGenFramebuffers, (GLsizei n, GLuint* framebuffers), (n, framebuffers))
FORWARD_GL_FUNC_VOID(glGenRenderbuffers, (GLsizei n, GLuint* renderbuffers), (n, renderbuffers))
FORWARD_GL_FUNC_VOID(glGenTextures, (GLsizei n, GLuint* textures), (n, textures))
FORWARD_GL_FUNC_VOID(glGenerateMipmap, (GLenum target), (target))
FORWARD_GL_FUNC_VOID(glGetActiveAttrib, (GLuint program, GLuint index, GLsizei bufSize, GLsizei* length, GLint* size, GLenum* type, char* name), (program, index, bufSize, length, size, type, name))
FORWARD_GL_FUNC_VOID(glGetActiveUniform, (GLuint program, GLuint index, GLsizei bufSize, GLsizei* length, GLint* size, GLenum* type, char* name), (program, index, bufSize, length, size, type, name))
FORWARD_GL_FUNC_VOID(glGetAttachedShaders, (GLuint program, GLsizei maxCount, GLsizei* count, GLuint* shaders), (program, maxCount, count, shaders))
FORWARD_GL_FUNC(glGetAttribLocation, GLint, (GLuint program, const char* name), (program, name))
FORWARD_GL_FUNC_VOID(glGetBooleanv, (GLenum pname, GLubyte* params), (pname, params))
FORWARD_GL_FUNC_VOID(glGetBufferParameteriv, (GLenum target, GLenum pname, GLint* params), (target, pname, params))
FORWARD_GL_FUNC(glGetError, GLenum, (void), ())
FORWARD_GL_FUNC_VOID(glGetFramebufferAttachmentParameteriv, (GLenum target, GLenum attachment, GLenum pname, GLint* params), (target, attachment, pname, params))
FORWARD_GL_FUNC_VOID(glGetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei* length, char* infoLog), (program, bufSize, length, infoLog))
FORWARD_GL_FUNC_VOID(glGetProgramiv, (GLuint program, GLenum pname, GLint* params), (program, pname, params))
FORWARD_GL_FUNC_VOID(glGetRenderbufferParameteriv, (GLenum target, GLenum pname, GLint* params), (target, pname, params))
FORWARD_GL_FUNC_VOID(glGetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei* length, char* infoLog), (shader, bufSize, length, infoLog))
FORWARD_GL_FUNC_VOID(glGetShaderSource, (GLuint shader, GLsizei bufSize, GLsizei* length, char* source), (shader, bufSize, length, source))
FORWARD_GL_FUNC_VOID(glGetShaderiv, (GLuint shader, GLenum pname, GLint* params), (shader, pname, params))
FORWARD_GL_FUNC_VOID(glGetTexParameterfv, (GLenum target, GLenum pname, float* params), (target, pname, params))
FORWARD_GL_FUNC_VOID(glGetTexParameteriv, (GLenum target, GLenum pname, GLint* params), (target, pname, params))
FORWARD_GL_FUNC(glGetUniformLocation, GLint, (GLuint program, const char* name), (program, name))
FORWARD_GL_FUNC_VOID(glGetUniformfv, (GLuint program, GLint location, float* params), (program, location, params))
FORWARD_GL_FUNC_VOID(glGetUniformiv, (GLuint program, GLint location, GLint* params), (program, location, params))
FORWARD_GL_FUNC_VOID(glGetVertexAttribPointerv, (GLuint index, GLenum pname, void** pointer), (index, pname, pointer))
FORWARD_GL_FUNC_VOID(glGetVertexAttribfv, (GLuint index, GLenum pname, float* params), (index, pname, params))
FORWARD_GL_FUNC_VOID(glGetVertexAttribiv, (GLuint index, GLenum pname, GLint* params), (index, pname, params))
FORWARD_GL_FUNC_VOID(glHint, (GLenum target, GLenum mode), (target, mode))
FORWARD_GL_FUNC(glIsBuffer, GLubyte, (GLuint buffer), (buffer))
FORWARD_GL_FUNC(glIsEnabled, GLubyte, (GLenum cap), (cap))
FORWARD_GL_FUNC(glIsFramebuffer, GLubyte, (GLuint framebuffer), (framebuffer))
FORWARD_GL_FUNC(glIsProgram, GLubyte, (GLuint program), (program))
FORWARD_GL_FUNC(glIsRenderbuffer, GLubyte, (GLuint renderbuffer), (renderbuffer))
FORWARD_GL_FUNC(glIsShader, GLubyte, (GLuint shader), (shader))
FORWARD_GL_FUNC(glIsTexture, GLubyte, (GLuint texture), (texture))
FORWARD_GL_FUNC_VOID(glLineWidth, (float width), (width))
FORWARD_GL_FUNC_VOID(glLinkProgram, (GLuint program), (program))
FORWARD_GL_FUNC_VOID(glPixelStorei, (GLenum pname, GLint param), (pname, param))
FORWARD_GL_FUNC_VOID(glPolygonOffset, (float factor, float units), (factor, units))
FORWARD_GL_FUNC_VOID(glRenderbufferStorage, (GLenum target, GLenum internalformat, GLsizei width, GLsizei height), (target, internalformat, width, height))
FORWARD_GL_FUNC_VOID(glSampleCoverage, (float value, GLubyte invert), (value, invert))
FORWARD_GL_FUNC_VOID(glScissor, (GLint x, GLint y, GLsizei width, GLsizei height), (x, y, width, height))
FORWARD_GL_FUNC_VOID(glShaderSource, (GLuint shader, GLsizei count, const char* const* string, const GLint* length), (shader, count, string, length))
FORWARD_GL_FUNC_VOID(glStencilFunc, (GLenum func, GLint ref, GLuint mask), (func, ref, mask))
FORWARD_GL_FUNC_VOID(glStencilFuncSeparate, (GLenum face, GLenum func, GLint ref, GLuint mask), (face, func, ref, mask))
FORWARD_GL_FUNC_VOID(glStencilMask, (GLuint mask), (mask))
FORWARD_GL_FUNC_VOID(glStencilMaskSeparate, (GLenum face, GLuint mask), (face, mask))
FORWARD_GL_FUNC_VOID(glStencilOp, (GLenum fail, GLenum zfail, GLenum zpass), (fail, zfail, zpass))
FORWARD_GL_FUNC_VOID(glStencilOpSeparate, (GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass), (face, sfail, dpfail, dppass))
FORWARD_GL_FUNC_VOID(glTexImage2D, (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels), (target, level, internalformat, width, height, border, format, type, pixels))
FORWARD_GL_FUNC_VOID(glTexParameterf, (GLenum target, GLenum pname, float param), (target, pname, param))
FORWARD_GL_FUNC_VOID(glTexParameterfv, (GLenum target, GLenum pname, const float* params), (target, pname, params))
FORWARD_GL_FUNC_VOID(glTexParameteri, (GLenum target, GLenum pname, GLint param), (target, pname, param))
FORWARD_GL_FUNC_VOID(glTexParameteriv, (GLenum target, GLenum pname, const GLint* params), (target, pname, params))
FORWARD_GL_FUNC_VOID(glTexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* pixels), (target, level, xoffset, yoffset, width, height, format, type, pixels))
FORWARD_GL_FUNC_VOID(glUniform1f, (GLint location, float v0), (location, v0))
FORWARD_GL_FUNC_VOID(glUniform1fv, (GLint location, GLsizei count, const float* value), (location, count, value))
FORWARD_GL_FUNC_VOID(glUniform1i, (GLint location, GLint v0), (location, v0))
FORWARD_GL_FUNC_VOID(glUniform1iv, (GLint location, GLsizei count, const GLint* value), (location, count, value))
FORWARD_GL_FUNC_VOID(glUniform2f, (GLint location, float v0, float v1), (location, v0, v1))
FORWARD_GL_FUNC_VOID(glUniform2fv, (GLint location, GLsizei count, const float* value), (location, count, value))
FORWARD_GL_FUNC_VOID(glUniform2i, (GLint location, GLint v0, GLint v1), (location, v0, v1))
FORWARD_GL_FUNC_VOID(glUniform2iv, (GLint location, GLsizei count, const GLint* value), (location, count, value))
FORWARD_GL_FUNC_VOID(glUniform3f, (GLint location, float v0, float v1, float v2), (location, v0, v1, v2))
FORWARD_GL_FUNC_VOID(glUniform3fv, (GLint location, GLsizei count, const float* value), (location, count, value))
FORWARD_GL_FUNC_VOID(glUniform3i, (GLint location, GLint v0, GLint v1, GLint v2), (location, v0, v1, v2))
FORWARD_GL_FUNC_VOID(glUniform3iv, (GLint location, GLsizei count, const GLint* value), (location, count, value))
FORWARD_GL_FUNC_VOID(glUniform4f, (GLint location, float v0, float v1, float v2, float v3), (location, v0, v1, v2, v3))
FORWARD_GL_FUNC_VOID(glUniform4fv, (GLint location, GLsizei count, const float* value), (location, count, value))
FORWARD_GL_FUNC_VOID(glUniform4i, (GLint location, GLint v0, GLint v1, GLint v2, GLint v3), (location, v0, v1, v2, v3))
FORWARD_GL_FUNC_VOID(glUniform4iv, (GLint location, GLsizei count, const GLint* value), (location, count, value))
FORWARD_GL_FUNC_VOID(glUniformMatrix2fv, (GLint location, GLsizei count, GLubyte transpose, const float* value), (location, count, transpose, value))
FORWARD_GL_FUNC_VOID(glUniformMatrix3fv, (GLint location, GLsizei count, GLubyte transpose, const float* value), (location, count, transpose, value))
FORWARD_GL_FUNC_VOID(glUniformMatrix4fv, (GLint location, GLsizei count, GLubyte transpose, const float* value), (location, count, transpose, value))
FORWARD_GL_FUNC_VOID(glUseProgram, (GLuint program), (program))
FORWARD_GL_FUNC_VOID(glValidateProgram, (GLuint program), (program))
FORWARD_GL_FUNC_VOID(glVertexAttrib1f, (GLuint index, float x), (index, x))
FORWARD_GL_FUNC_VOID(glVertexAttrib1fv, (GLuint index, const float* v), (index, v))
FORWARD_GL_FUNC_VOID(glVertexAttrib2f, (GLuint index, float x, float y), (index, x, y))
FORWARD_GL_FUNC_VOID(glVertexAttrib2fv, (GLuint index, const float* v), (index, v))
FORWARD_GL_FUNC_VOID(glVertexAttrib3f, (GLuint index, float x, float y, float z), (index, x, y, z))
FORWARD_GL_FUNC_VOID(glVertexAttrib3fv, (GLuint index, const float* v), (index, v))
FORWARD_GL_FUNC_VOID(glVertexAttrib4f, (GLuint index, float x, float y, float z, float w), (index, x, y, z, w))
FORWARD_GL_FUNC_VOID(glVertexAttrib4fv, (GLuint index, const float* v), (index, v))
FORWARD_GL_FUNC_VOID(glVertexAttribPointer, (GLuint index, GLint size, GLenum type, GLubyte normalized, GLsizei stride, const void* pointer), (index, size, type, normalized, stride, pointer))
FORWARD_GL_FUNC_VOID(glViewport, (GLint x, GLint y, GLsizei width, GLsizei height), (x, y, width, height))

// ============================================================================
// EGL GetProcAddress Interception
// ============================================================================
// This is critical! ANGLE clients use eglGetProcAddress to get function pointers.
// If we just forward to the original, they'll get the original unhooked functions.
// We must return our hooked versions for the functions we intercept.

typedef void* (*eglGetProcAddress_t)(const char* procname);
static eglGetProcAddress_t orig_eglGetProcAddress = NULL;

// Declare our hooked functions for pointer return
extern const GLubyte* glGetString(GLenum name);
extern void glGetIntegerv(GLenum pname, GLint* params);
extern void glGetFloatv(GLenum pname, float* params);
extern void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                         GLenum format, GLenum type, void* pixels);
extern void glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype,
                                        GLint* range, GLint* precision);

// Helper function to handle proc address lookup
static void* get_hooked_proc_address(const char* procname, eglGetProcAddress_t original_fn) {
    // Return our hooked versions for intercepted functions
    if (procname) {
        if (strcmp(procname, "glGetString") == 0) {
            owl_debug("eglGetProcAddress(\"%s\") -> returning HOOKED glGetString", procname);
            return (void*)glGetString;
        }
        if (strcmp(procname, "glGetIntegerv") == 0) {
            owl_debug("eglGetProcAddress(\"%s\") -> returning HOOKED glGetIntegerv", procname);
            return (void*)glGetIntegerv;
        }
        if (strcmp(procname, "glGetFloatv") == 0) {
            owl_debug("eglGetProcAddress(\"%s\") -> returning HOOKED glGetFloatv", procname);
            return (void*)glGetFloatv;
        }
        if (strcmp(procname, "glReadPixels") == 0) {
            owl_debug("eglGetProcAddress(\"%s\") -> returning HOOKED glReadPixels", procname);
            return (void*)glReadPixels;
        }
        if (strcmp(procname, "glGetShaderPrecisionFormat") == 0) {
            owl_debug("eglGetProcAddress(\"%s\") -> returning HOOKED glGetShaderPrecisionFormat", procname);
            return (void*)glGetShaderPrecisionFormat;
        }
    }

    // For all other functions, forward to original
    if (original_fn) {
        void* result = original_fn(procname);
        // Log important EGL/context-related functions
        if (procname && (
            strstr(procname, "eglCreate") != NULL ||
            strstr(procname, "eglMake") != NULL ||
            strstr(procname, "Context") != NULL ||
            strstr(procname, "Surface") != NULL)) {
            owl_debug("eglGetProcAddress(\"%s\") -> %p (ORIGINAL)", procname, result);
        }
        return result;
    }
    owl_debug("eglGetProcAddress(\"%s\") -> NULL (no original fn!)", procname);
    return NULL;
}

// Standard lowercase eglGetProcAddress
void* eglGetProcAddress(const char* procname) {
    ensure_initialized();
    if (!orig_eglGetProcAddress) {
        orig_eglGetProcAddress = (eglGetProcAddress_t)dlsym(g_original_lib, "eglGetProcAddress");
        owl_debug("Loaded orig_eglGetProcAddress: %p", (void*)orig_eglGetProcAddress);
    }
    return get_hooked_proc_address(procname, orig_eglGetProcAddress);
}

// ANGLE uses EGL_GetProcAddress (with capitals) - this is the actual exported symbol
static eglGetProcAddress_t orig_EGL_GetProcAddress = NULL;

void* EGL_GetProcAddress(const char* procname) {
    ensure_initialized();
    if (!orig_EGL_GetProcAddress) {
        orig_EGL_GetProcAddress = (eglGetProcAddress_t)dlsym(g_original_lib, "EGL_GetProcAddress");
        owl_debug("Loaded orig_EGL_GetProcAddress: %p", (void*)orig_EGL_GetProcAddress);
    }
    return get_hooked_proc_address(procname, orig_EGL_GetProcAddress);
}
