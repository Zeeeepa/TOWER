/**
 * OWL Seed API - C Interface for Per-Context Fingerprint Seeds
 *
 * This header defines the C interface for accessing per-context fingerprint
 * seeds and hashes. Each browser context gets unique, realistic fingerprint
 * values that remain consistent for the context's lifetime.
 *
 * Key features:
 * - 100% isolation between browser contexts
 * - Realistic values that pass bot detection
 * - Consistent seeds for main frame, iframes, and workers within same context
 * - Thread-safe access from renderer process
 *
 * Usage pattern (similar to GPU API):
 * 1. owl_seed_register_context() - Called when browser context is created
 * 2. owl_seed_set_current_context() - Called before rendering/script injection
 * 3. owl_seed_get_*() - Called to retrieve current context's seeds
 * 4. owl_seed_unregister_context() - Called when context is destroyed
 */

#ifndef OWL_SEED_API_H
#define OWL_SEED_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Hash Type Constants
 * Used with owl_seed_get_hash() to retrieve specific 32-char MD5-style hashes
 * ============================================================================ */

#define OWL_HASH_CANVAS_GEOMETRY       0   /* Canvas geometry fingerprint */
#define OWL_HASH_CANVAS_TEXT           1   /* Canvas text rendering fingerprint */
#define OWL_HASH_WEBGL_PARAMS          2   /* WebGL parameters hash */
#define OWL_HASH_WEBGL_EXTENSIONS      3   /* WebGL extensions list hash */
#define OWL_HASH_WEBGL_CONTEXT         4   /* WebGL context attributes hash */
#define OWL_HASH_WEBGL_EXT_PARAMS      5   /* WebGL extension parameters hash */
#define OWL_HASH_SHADER_PRECISIONS     6   /* Shader precision formats hash */
#define OWL_HASH_FONTS                 7   /* Font enumeration hash */
#define OWL_HASH_PLUGINS               8   /* Plugin enumeration hash */
#define OWL_HASH_COUNT                 9   /* Total number of hash types */

/* ============================================================================
 * Context Lifecycle Functions
 * ============================================================================ */

/**
 * Register a new browser context for seed generation.
 * Generates unique, realistic fingerprint seeds for this context.
 * Seeds are cached and remain consistent for the context's lifetime.
 *
 * @param browser_id   Numeric browser identifier (used by renderer)
 * @param context_id   String context identifier (unique per context)
 */
__attribute__((visibility("default")))
void owl_seed_register_context(int browser_id, const char* context_id);

/**
 * Unregister a browser context and clean up its seeds.
 * Called when a browser context is destroyed.
 *
 * @param browser_id   Numeric browser identifier
 */
__attribute__((visibility("default")))
void owl_seed_unregister_context(int browser_id);

/**
 * Set the current thread's seed context by browser ID.
 * Must be called before any seed accessor functions.
 * Called by browser before rendering or injecting scripts into a context.
 *
 * @param browser_id   Numeric browser identifier
 * @return 1 on success, 0 if context not found
 */
__attribute__((visibility("default")))
int owl_seed_set_current_context(int browser_id);

/**
 * Clear the current context (set to no context).
 * After this call, seed accessors will return 0/NULL.
 */
__attribute__((visibility("default")))
void owl_seed_clear_current_context(void);

/**
 * Check if seed generation is enabled for the current context.
 * @return 1 if enabled, 0 if disabled or no context set
 */
__attribute__((visibility("default")))
int owl_seed_is_enabled(void);

/* ============================================================================
 * Seed Accessor Functions (64-bit seeds for noise generation)
 * These return the current context's fingerprint seeds.
 * Returns 0 if no context is set.
 * ============================================================================ */

/**
 * Get the canvas fingerprint seed (64-bit).
 * Used for Canvas 2D/WebGL fingerprint noise generation.
 */
__attribute__((visibility("default")))
uint64_t owl_seed_get_canvas(void);

/**
 * Get the WebGL fingerprint seed (64-bit).
 * Used for WebGL parameter/extension noise generation.
 */
__attribute__((visibility("default")))
uint64_t owl_seed_get_webgl(void);

/**
 * Get the audio fingerprint seed (64-bit).
 * Used for AudioContext fingerprint noise generation.
 */
__attribute__((visibility("default")))
uint64_t owl_seed_get_audio(void);

/**
 * Get the font enumeration seed (64-bit).
 * Used for font fingerprint noise generation.
 */
__attribute__((visibility("default")))
uint64_t owl_seed_get_fonts(void);

/**
 * Get the client rects seed (64-bit).
 * Used for getBoundingClientRect noise generation.
 */
__attribute__((visibility("default")))
uint64_t owl_seed_get_client_rects(void);

/* ============================================================================
 * Realistic Value Accessor Functions
 * These return pre-computed realistic fingerprint values.
 * ============================================================================ */

/**
 * Get the audio fingerprint value (double, ~124.04).
 * Real Chrome audio fingerprints are in range 124.00-124.10.
 * This value passes fingerprint.com audio detection.
 */
__attribute__((visibility("default")))
double owl_seed_get_audio_fingerprint(void);

/**
 * Get a 32-character MD5-style hash for fingerprint.com compatibility.
 * Returns pointer to thread-local string (valid until next call on same thread).
 *
 * @param hash_type   One of OWL_HASH_* constants
 * @return 32-char lowercase hex string, or NULL if invalid type or no context
 */
__attribute__((visibility("default")))
const char* owl_seed_get_hash(int hash_type);

/* ============================================================================
 * Convenience Hash Accessor Functions
 * Direct accessors for common hash types.
 * ============================================================================ */

/**
 * Get the canvas geometry hash (32-char lowercase hex).
 * Used for canvas.Geometry in fingerprint.com format.
 */
__attribute__((visibility("default")))
const char* owl_seed_get_canvas_geometry_hash(void);

/**
 * Get the canvas text hash (32-char lowercase hex).
 * Used for canvas.Text in fingerprint.com format.
 */
__attribute__((visibility("default")))
const char* owl_seed_get_canvas_text_hash(void);

/**
 * Get the WebGL parameters hash (32-char lowercase hex).
 */
__attribute__((visibility("default")))
const char* owl_seed_get_webgl_params_hash(void);

/**
 * Get the WebGL extensions hash (32-char lowercase hex).
 */
__attribute__((visibility("default")))
const char* owl_seed_get_webgl_extensions_hash(void);

/**
 * Get the shader precisions hash (32-char lowercase hex).
 */
__attribute__((visibility("default")))
const char* owl_seed_get_shader_precisions_hash(void);

/* ============================================================================
 * All Seeds Structure (for batch retrieval)
 * ============================================================================ */

/**
 * Structure containing all fingerprint seeds for a context.
 * Used for batch retrieval to minimize API calls.
 */
typedef struct {
    /* 64-bit seeds for noise generation */
    uint64_t canvas_seed;
    uint64_t webgl_seed;
    uint64_t audio_seed;
    uint64_t fonts_seed;
    uint64_t client_rects_seed;

    /* Realistic audio fingerprint value (~124.04) */
    double audio_fingerprint;

    /* 32-char MD5-style hashes (null-terminated) */
    char canvas_geometry_hash[33];
    char canvas_text_hash[33];
    char webgl_params_hash[33];
    char webgl_extensions_hash[33];
    char webgl_context_hash[33];
    char webgl_ext_params_hash[33];
    char shader_precisions_hash[33];
    char fonts_hash[33];
    char plugins_hash[33];
} OWLSeedData;

/**
 * Get all seeds for the current context in one call.
 * More efficient than calling individual accessors.
 *
 * @param out_data   Pointer to OWLSeedData structure to fill
 * @return 1 on success, 0 if no context set or out_data is NULL
 */
__attribute__((visibility("default")))
int owl_seed_get_all(OWLSeedData* out_data);

/* ============================================================================
 * Debug/Monitoring Functions
 * ============================================================================ */

/**
 * Get the number of registered contexts.
 * Useful for debugging and monitoring memory usage.
 */
__attribute__((visibility("default")))
int owl_seed_get_context_count(void);

/**
 * Get the current context's browser ID.
 * Returns -1 if no context is set.
 */
__attribute__((visibility("default")))
int owl_seed_get_current_browser_id(void);

#ifdef __cplusplus
}
#endif

#endif /* OWL_SEED_API_H */
