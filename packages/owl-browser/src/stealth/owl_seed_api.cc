/**
 * OWL Seed API - Implementation
 *
 * Provides per-context fingerprint seed management following the GPU API pattern.
 * Uses atomic context switching for thread-safe access from renderer process.
 */

#include "stealth/owl_seed_api.h"
#include "stealth/owl_fingerprint_generator.h"
#include "util/logger.h"

#include <atomic>
#include <mutex>
#include <map>
#include <string>
#include <cstring>

namespace {

/* ============================================================================
 * Per-Context Seed Storage
 * ============================================================================ */

struct SeedContext {
    std::string context_id;           // String ID for fingerprint generator
    owl::FingerprintSeeds seeds;      // Cached seeds from generator
    bool enabled;                      // Whether seeding is enabled
};

// Process-wide context registry (browser_id -> SeedContext)
std::map<int, SeedContext> g_contexts;
std::mutex g_contexts_mutex;

// Current context for this thread/process (atomic for thread-safe reads)
std::atomic<int> g_current_browser_id{-1};

/* ============================================================================
 * Thread-Local String Storage
 * For returning stable const char* pointers from accessor functions.
 * Each thread gets its own copy to avoid race conditions.
 * ============================================================================ */

thread_local std::string g_tls_canvas_geometry_hash;
thread_local std::string g_tls_canvas_text_hash;
thread_local std::string g_tls_webgl_params_hash;
thread_local std::string g_tls_webgl_extensions_hash;
thread_local std::string g_tls_webgl_context_hash;
thread_local std::string g_tls_webgl_ext_params_hash;
thread_local std::string g_tls_shader_precisions_hash;
thread_local std::string g_tls_fonts_hash;
thread_local std::string g_tls_plugins_hash;
thread_local std::string g_tls_generic_hash;

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * Get seeds for current context (thread-safe copy).
 * Returns false if no context set.
 */
bool GetCurrentSeeds(owl::FingerprintSeeds& out_seeds) {
    int browser_id = g_current_browser_id.load(std::memory_order_acquire);
    if (browser_id < 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_contexts_mutex);
    auto it = g_contexts.find(browser_id);
    if (it != g_contexts.end() && it->second.enabled) {
        out_seeds = it->second.seeds;
        return true;
    }
    return false;
}

} // anonymous namespace

/* ============================================================================
 * C API Implementation
 * ============================================================================ */

extern "C" {

void owl_seed_register_context(int browser_id, const char* context_id) {
    if (!context_id) {
        LOG_WARN("SeedApi", "owl_seed_register_context called with null context_id");
        return;
    }

    std::string ctx_id(context_id);

    // Generate seeds using the fingerprint generator
    // This will create new seeds or return cached ones if context_id was used before
    owl::FingerprintSeeds seeds = owl::OwlFingerprintGenerator::Instance().GetSeeds(ctx_id);

    // Store in our registry
    {
        std::lock_guard<std::mutex> lock(g_contexts_mutex);
        SeedContext ctx;
        ctx.context_id = ctx_id;
        ctx.seeds = seeds;
        ctx.enabled = true;
        g_contexts[browser_id] = std::move(ctx);
    }

    LOG_INFO("SeedApi", "Registered seed context browser_id=" + std::to_string(browser_id) +
             " context_id=" + ctx_id +
             " canvas=0x" + seeds.canvas_hex +
             " audio=" + std::to_string(seeds.audio_fingerprint));
}

void owl_seed_unregister_context(int browser_id) {
    std::string context_id;

    {
        std::lock_guard<std::mutex> lock(g_contexts_mutex);
        auto it = g_contexts.find(browser_id);
        if (it != g_contexts.end()) {
            context_id = it->second.context_id;
            g_contexts.erase(it);
        }
    }

    // Also clear from the fingerprint generator
    if (!context_id.empty()) {
        owl::OwlFingerprintGenerator::Instance().ClearContext(context_id);
        LOG_DEBUG("SeedApi", "Unregistered seed context browser_id=" + std::to_string(browser_id));
    }

    // Clear current context if it was this one
    int expected = browser_id;
    g_current_browser_id.compare_exchange_strong(expected, -1, std::memory_order_release);
}

int owl_seed_set_current_context(int browser_id) {
    // Store the new current context atomically
    g_current_browser_id.store(browser_id, std::memory_order_release);

    // Verify context exists
    if (browser_id < 0) {
        return 1; // Setting to no context is valid
    }

    std::lock_guard<std::mutex> lock(g_contexts_mutex);
    bool exists = g_contexts.count(browser_id) > 0;

    if (!exists) {
        LOG_WARN("SeedApi", "owl_seed_set_current_context: browser_id=" +
                std::to_string(browser_id) + " not registered");
    }

    return exists ? 1 : 0;
}

void owl_seed_clear_current_context(void) {
    g_current_browser_id.store(-1, std::memory_order_release);
}

int owl_seed_is_enabled(void) {
    owl::FingerprintSeeds seeds;
    return GetCurrentSeeds(seeds) ? 1 : 0;
}

/* ============================================================================
 * 64-bit Seed Accessors
 * ============================================================================ */

uint64_t owl_seed_get_canvas(void) {
    owl::FingerprintSeeds seeds;
    if (!GetCurrentSeeds(seeds)) {
        return 0;
    }
    return seeds.canvas_seed;
}

uint64_t owl_seed_get_webgl(void) {
    owl::FingerprintSeeds seeds;
    if (!GetCurrentSeeds(seeds)) {
        return 0;
    }
    return seeds.webgl_seed;
}

uint64_t owl_seed_get_audio(void) {
    owl::FingerprintSeeds seeds;
    if (!GetCurrentSeeds(seeds)) {
        return 0;
    }
    return seeds.audio_seed;
}

uint64_t owl_seed_get_fonts(void) {
    owl::FingerprintSeeds seeds;
    if (!GetCurrentSeeds(seeds)) {
        return 0;
    }
    // Use a derived seed from canvas_seed for fonts
    // This ensures different but deterministic value
    return seeds.canvas_seed ^ 0x466F6E7473536565ULL; // "FontsSee" in hex
}

uint64_t owl_seed_get_client_rects(void) {
    owl::FingerprintSeeds seeds;
    if (!GetCurrentSeeds(seeds)) {
        return 0;
    }
    // Use a derived seed from webgl_seed for client rects
    return seeds.webgl_seed ^ 0x52656374734865ULL; // "RectsHe" in hex
}

/* ============================================================================
 * Realistic Value Accessors
 * ============================================================================ */

double owl_seed_get_audio_fingerprint(void) {
    owl::FingerprintSeeds seeds;
    if (!GetCurrentSeeds(seeds)) {
        return 0.0;
    }
    return seeds.audio_fingerprint;
}

const char* owl_seed_get_hash(int hash_type) {
    owl::FingerprintSeeds seeds;
    if (!GetCurrentSeeds(seeds)) {
        return nullptr;
    }

    switch (hash_type) {
        case OWL_HASH_CANVAS_GEOMETRY:
            g_tls_generic_hash = seeds.canvas_geometry_hash;
            break;
        case OWL_HASH_CANVAS_TEXT:
            g_tls_generic_hash = seeds.canvas_text_hash;
            break;
        case OWL_HASH_WEBGL_PARAMS:
            g_tls_generic_hash = seeds.webgl_params_hash;
            break;
        case OWL_HASH_WEBGL_EXTENSIONS:
            g_tls_generic_hash = seeds.webgl_extensions_hash;
            break;
        case OWL_HASH_WEBGL_CONTEXT:
            g_tls_generic_hash = seeds.webgl_context_hash;
            break;
        case OWL_HASH_WEBGL_EXT_PARAMS:
            g_tls_generic_hash = seeds.webgl_ext_params_hash;
            break;
        case OWL_HASH_SHADER_PRECISIONS:
            g_tls_generic_hash = seeds.shader_precisions_hash;
            break;
        case OWL_HASH_FONTS:
            // Generate fonts hash from canvas seed
            g_tls_generic_hash = seeds.canvas_geometry_hash.substr(0, 16) +
                                 seeds.canvas_text_hash.substr(16, 16);
            break;
        case OWL_HASH_PLUGINS:
            // Generate plugins hash from webgl seed
            g_tls_generic_hash = seeds.webgl_params_hash.substr(0, 16) +
                                 seeds.webgl_extensions_hash.substr(16, 16);
            break;
        default:
            return nullptr;
    }

    return g_tls_generic_hash.c_str();
}

/* ============================================================================
 * Convenience Hash Accessors
 * ============================================================================ */

const char* owl_seed_get_canvas_geometry_hash(void) {
    owl::FingerprintSeeds seeds;
    if (!GetCurrentSeeds(seeds)) {
        return nullptr;
    }
    g_tls_canvas_geometry_hash = seeds.canvas_geometry_hash;
    return g_tls_canvas_geometry_hash.c_str();
}

const char* owl_seed_get_canvas_text_hash(void) {
    owl::FingerprintSeeds seeds;
    if (!GetCurrentSeeds(seeds)) {
        return nullptr;
    }
    g_tls_canvas_text_hash = seeds.canvas_text_hash;
    return g_tls_canvas_text_hash.c_str();
}

const char* owl_seed_get_webgl_params_hash(void) {
    owl::FingerprintSeeds seeds;
    if (!GetCurrentSeeds(seeds)) {
        return nullptr;
    }
    g_tls_webgl_params_hash = seeds.webgl_params_hash;
    return g_tls_webgl_params_hash.c_str();
}

const char* owl_seed_get_webgl_extensions_hash(void) {
    owl::FingerprintSeeds seeds;
    if (!GetCurrentSeeds(seeds)) {
        return nullptr;
    }
    g_tls_webgl_extensions_hash = seeds.webgl_extensions_hash;
    return g_tls_webgl_extensions_hash.c_str();
}

const char* owl_seed_get_shader_precisions_hash(void) {
    owl::FingerprintSeeds seeds;
    if (!GetCurrentSeeds(seeds)) {
        return nullptr;
    }
    g_tls_shader_precisions_hash = seeds.shader_precisions_hash;
    return g_tls_shader_precisions_hash.c_str();
}

/* ============================================================================
 * Batch Accessor
 * ============================================================================ */

int owl_seed_get_all(OWLSeedData* out_data) {
    if (!out_data) {
        return 0;
    }

    owl::FingerprintSeeds seeds;
    if (!GetCurrentSeeds(seeds)) {
        return 0;
    }

    // Copy 64-bit seeds
    out_data->canvas_seed = seeds.canvas_seed;
    out_data->webgl_seed = seeds.webgl_seed;
    out_data->audio_seed = seeds.audio_seed;
    out_data->fonts_seed = seeds.canvas_seed ^ 0x466F6E7473536565ULL;
    out_data->client_rects_seed = seeds.webgl_seed ^ 0x52656374734865ULL;

    // Copy audio fingerprint
    out_data->audio_fingerprint = seeds.audio_fingerprint;

    // Copy 32-char hashes (ensure null termination)
    auto copy_hash = [](char* dest, const std::string& src) {
        std::strncpy(dest, src.c_str(), 32);
        dest[32] = '\0';
    };

    copy_hash(out_data->canvas_geometry_hash, seeds.canvas_geometry_hash);
    copy_hash(out_data->canvas_text_hash, seeds.canvas_text_hash);
    copy_hash(out_data->webgl_params_hash, seeds.webgl_params_hash);
    copy_hash(out_data->webgl_extensions_hash, seeds.webgl_extensions_hash);
    copy_hash(out_data->webgl_context_hash, seeds.webgl_context_hash);
    copy_hash(out_data->webgl_ext_params_hash, seeds.webgl_ext_params_hash);
    copy_hash(out_data->shader_precisions_hash, seeds.shader_precisions_hash);

    // Generate derived hashes for fonts and plugins
    std::string fonts_hash = seeds.canvas_geometry_hash.substr(0, 16) +
                             seeds.canvas_text_hash.substr(16, 16);
    std::string plugins_hash = seeds.webgl_params_hash.substr(0, 16) +
                               seeds.webgl_extensions_hash.substr(16, 16);

    copy_hash(out_data->fonts_hash, fonts_hash);
    copy_hash(out_data->plugins_hash, plugins_hash);

    return 1;
}

/* ============================================================================
 * Debug/Monitoring Functions
 * ============================================================================ */

int owl_seed_get_context_count(void) {
    std::lock_guard<std::mutex> lock(g_contexts_mutex);
    return static_cast<int>(g_contexts.size());
}

int owl_seed_get_current_browser_id(void) {
    return g_current_browser_id.load(std::memory_order_acquire);
}

} // extern "C"
