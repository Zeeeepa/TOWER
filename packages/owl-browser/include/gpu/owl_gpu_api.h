/**
 * OWL GPU API - C Interface for ANGLE Wrapper
 *
 * This header defines the C interface that the ANGLE wrapper uses to get
 * per-context GPU spoofing values. The browser exports these functions,
 * and the wrapper calls them to get the current context's GPU identity.
 *
 * This enables per-context GPU spoofing where each browser context can
 * have its own unique GPU identity that matches between JS and native levels.
 */

#ifndef OWL_GPU_API_H
#define OWL_GPU_API_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the current context's GPU vendor string.
 * Returns NULL if no context is active or spoofing is disabled.
 * The returned string is valid until the next call or context switch.
 */
__attribute__((visibility("default")))
const char* owl_gpu_get_vendor(void);

/**
 * Get the current context's GPU renderer string.
 * Returns NULL if no context is active or spoofing is disabled.
 */
__attribute__((visibility("default")))
const char* owl_gpu_get_renderer(void);

/**
 * Get the current context's GL version string.
 * Returns NULL if no context is active or spoofing is disabled.
 */
__attribute__((visibility("default")))
const char* owl_gpu_get_version(void);

/**
 * Get the current context's GLSL version string.
 * Returns NULL if no context is active or spoofing is disabled.
 */
__attribute__((visibility("default")))
const char* owl_gpu_get_glsl_version(void);

/**
 * Check if GPU spoofing is enabled for the current context.
 * Returns 1 if enabled, 0 if disabled.
 */
__attribute__((visibility("default")))
int owl_gpu_is_spoofing_enabled(void);

/**
 * Set the current thread's GPU context by context ID.
 * Called by the browser before rendering a context.
 * Returns 1 on success, 0 if context not found.
 */
__attribute__((visibility("default")))
int owl_gpu_set_current_context(int context_id);

/**
 * Register GPU values for a context.
 * Called when a new browser context is created.
 */
__attribute__((visibility("default")))
void owl_gpu_register_context(int context_id,
                               const char* vendor,
                               const char* renderer,
                               const char* version,
                               const char* glsl_version);

/**
 * Unregister a context's GPU values.
 * Called when a browser context is destroyed.
 */
__attribute__((visibility("default")))
void owl_gpu_unregister_context(int context_id);

/**
 * Extended GPU parameters for complete spoofing.
 * These are used by glGetIntegerv and glGetShaderPrecisionFormat.
 */
typedef struct {
    // Integer capabilities (WebGL1)
    int max_texture_size;
    int max_cube_map_texture_size;
    int max_render_buffer_size;
    int max_vertex_attribs;
    int max_vertex_uniform_vectors;
    int max_vertex_texture_units;
    int max_varying_vectors;
    int max_fragment_uniform_vectors;
    int max_texture_units;
    int max_combined_texture_units;
    int max_viewport_dims[2];
    int max_samples;

    // Multisampling parameters (critical for VM detection!)
    int samples;         // GL_SAMPLES - actual samples in current framebuffer (e.g., 4)
    int sample_buffers;  // GL_SAMPLE_BUFFERS - 1 if multisampling enabled, 0 otherwise

    // Integer capabilities (WebGL2)
    int max_3d_texture_size;
    int max_array_texture_layers;
    int max_color_attachments;
    int max_draw_buffers;
    int max_uniform_buffer_bindings;
    int max_uniform_block_size;
    int max_combined_uniform_blocks;
    int max_transform_feedback_separate_attribs;

    // Float capabilities
    float aliased_line_width_range[2];
    float aliased_point_size_range[2];
    float max_anisotropy;

    // Shader precision formats: [range_min, range_max, precision]
    // FLOAT precision
    int vertex_high_float[3];
    int vertex_medium_float[3];
    int vertex_low_float[3];
    int fragment_high_float[3];
    int fragment_medium_float[3];
    int fragment_low_float[3];
    // INT precision
    int vertex_high_int[3];
    int vertex_medium_int[3];
    int vertex_low_int[3];
    int fragment_high_int[3];
    int fragment_medium_int[3];
    int fragment_low_int[3];
} OWLGPUParams;

/**
 * Register extended GPU parameters for a context.
 * Called after owl_gpu_register_context to set integer/precision values.
 */
__attribute__((visibility("default")))
void owl_gpu_register_params(int context_id, const OWLGPUParams* params);

/**
 * Get an integer parameter for the current context.
 * Returns 1 if spoofed value was set, 0 if should use original.
 */
__attribute__((visibility("default")))
int owl_gpu_get_integer(unsigned int pname, int* value);

/**
 * Get a float parameter for the current context.
 * Returns 1 if spoofed value was set, 0 if should use original.
 */
__attribute__((visibility("default")))
int owl_gpu_get_float(unsigned int pname, float* value);

/**
 * Get shader precision format for the current context.
 * Returns 1 if spoofed value was set, 0 if should use original.
 * shader_type: GL_VERTEX_SHADER (0x8B31) or GL_FRAGMENT_SHADER (0x8B30)
 * precision_type: GL_LOW_FLOAT, GL_MEDIUM_FLOAT, GL_HIGH_FLOAT
 */
__attribute__((visibility("default")))
int owl_gpu_get_shader_precision(unsigned int shader_type, unsigned int precision_type,
                                  int* range, int* precision);

#ifdef __cplusplus
}
#endif

#endif // OWL_GPU_API_H
