#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "include/cef_frame.h"
#include "stealth/owl_virtual_machine.h"

namespace owl {
namespace spoofs {

/**
 * WebGL Parameter Spoofing
 * 
 * Handles WebGL/WebGL2 fingerprint spoofing:
 * - UNMASKED_VENDOR_WEBGL, UNMASKED_RENDERER_WEBGL
 * - VENDOR, RENDERER, VERSION, SHADING_LANGUAGE_VERSION
 * - All MAX_* parameters (texture size, uniform vectors, etc.)
 * - getShaderPrecisionFormat
 * - getSupportedExtensions, getExtension
 * - getContextAttributes
 * - readPixels (noise injection)
 * 
 * DEPENDENCIES: Requires SpoofUtils to be injected first.
 * GUARD: Uses window[Symbol.for('owl')].guards.webgl
 */
class WebGLSpoof {
public:
    /**
     * Configuration for WebGL spoofing
     */
    struct Config {
        // Vendor/Renderer strings
        std::string vendor = "WebKit";
        std::string renderer = "WebKit WebGL";
        std::string unmasked_vendor;     // UNMASKED_VENDOR_WEBGL
        std::string unmasked_renderer;   // UNMASKED_RENDERER_WEBGL
        
        // Version strings
        std::string version = "WebGL 1.0 (OpenGL ES 2.0 Chromium)";
        std::string version2 = "WebGL 2.0 (OpenGL ES 3.0 Chromium)";
        std::string shading_language = "WebGL GLSL ES 1.0 (OpenGL ES GLSL ES 1.0 Chromium)";
        std::string shading_language_v2 = "WebGL GLSL ES 3.00 (OpenGL ES GLSL ES 3.0 Chromium)";
        
        // Capabilities
        int max_texture_size = 16384;
        int max_cube_map_texture_size = 16384;
        int max_render_buffer_size = 16384;
        int max_vertex_attribs = 16;
        int max_vertex_uniform_vectors = 4096;
        int max_vertex_texture_units = 16;
        int max_varying_vectors = 30;
        int max_fragment_uniform_vectors = 1024;
        int max_texture_units = 16;
        int max_combined_texture_units = 80;
        int max_viewport_dims_w = 32767;
        int max_viewport_dims_h = 32767;
        float aliased_line_width_min = 1.0f;
        float aliased_line_width_max = 1.0f;
        float aliased_point_size_min = 1.0f;
        float aliased_point_size_max = 1024.0f;
        
        // Multisampling
        int max_samples = 16;
        int samples = 4;
        int sample_buffers = 1;
        float max_anisotropy = 16.0f;
        
        // WebGL2-specific
        int max_3d_texture_size = 2048;
        int max_array_texture_layers = 2048;
        int max_color_attachments = 8;
        int max_draw_buffers = 8;
        int max_uniform_buffer_bindings = 84;
        int max_uniform_block_size = 65536;
        int max_combined_uniform_blocks = 84;
        
        // Extensions
        std::vector<std::string> extensions;
        std::vector<std::string> extensions2;
        
        // Precision formats
        struct PrecisionFormat {
            int range_min = 127;
            int range_max = 127;
            int precision = 23;
        };
        PrecisionFormat vertex_high_float;
        PrecisionFormat vertex_medium_float;
        PrecisionFormat vertex_low_float;
        PrecisionFormat vertex_high_int = {31, 30, 0};
        PrecisionFormat vertex_medium_int = {31, 30, 0};
        PrecisionFormat vertex_low_int = {31, 30, 0};
        PrecisionFormat fragment_high_float;
        PrecisionFormat fragment_medium_float;
        PrecisionFormat fragment_low_float;
        PrecisionFormat fragment_high_int = {31, 30, 0};
        PrecisionFormat fragment_medium_int = {31, 30, 0};
        PrecisionFormat fragment_low_int = {31, 30, 0};
        
        // Context attributes
        bool antialias = true;
        bool desynchronized = false;
        std::string power_preference = "default";
        
        // Noise seed for readPixels
        uint64_t seed = 0;
        
        // Build from VirtualMachine
        static Config FromVM(const VirtualMachine& vm);
    };
    
    /**
     * Inject WebGL spoofing into the frame.
     * 
     * @param frame The CEF frame to inject into
     * @param config The configuration for spoofing
     * @return true if injection succeeded
     */
    static bool Inject(CefRefPtr<CefFrame> frame, const Config& config);
    
    /**
     * Generate the JavaScript for WebGL spoofing.
     * 
     * @param config The configuration for spoofing
     * @return JavaScript code string
     */
    static std::string GenerateScript(const Config& config);
    
private:
    // Generate getParameter hook
    static std::string GenerateGetParameterHook(const Config& config);
    
    // Generate getShaderPrecisionFormat hook
    static std::string GenerateShaderPrecisionHook(const Config& config);
    
    // Generate getSupportedExtensions hook
    static std::string GenerateExtensionsHook(const Config& config);
    
    // Generate getContextAttributes hook
    static std::string GenerateContextAttributesHook(const Config& config);
    
    // Generate readPixels hook (noise injection)
    static std::string GenerateReadPixelsHook();
    
    // Generate getContext wrapper for mock WebGL
    static std::string GenerateGetContextHook(const Config& config);
    
    // Escape JavaScript string
    static std::string EscapeJS(const std::string& str);
    
    // Convert vector to JS array
    static std::string VectorToJSArray(const std::vector<std::string>& vec);
};

} // namespace spoofs
} // namespace owl
