#ifndef OWL_FINGERPRINT_GENERATOR_H_
#define OWL_FINGERPRINT_GENERATOR_H_

#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>

namespace owl {

/**
 * Fingerprint hash seeds for a browser context.
 * These seeds are used to generate deterministic noise for fingerprint protection.
 * The same seeds are used throughout a context's lifetime for consistency.
 */
struct FingerprintSeeds {
    // Core fingerprint seeds (64-bit for internal use)
    uint64_t canvas_seed;         // Seed for Canvas 2D fingerprint noise
    uint64_t webgl_seed;          // Seed for WebGL fingerprint noise
    uint64_t audio_seed;          // Seed for AudioContext fingerprint noise
    uint64_t fonts_seed;          // Seed for font enumeration noise
    uint64_t client_rects_seed;   // Seed for getBoundingClientRect noise
    uint64_t navigator_seed;      // Seed for navigator property noise
    uint64_t screen_seed;         // Seed for screen property noise

    // Audio fingerprint value (deterministically generated from seed)
    // Real Chrome values are typically in range 124.0 - 124.1
    double audio_fingerprint;

    // 32-char lowercase hex hashes (MD5-style format like fingerprint.com uses)
    std::string canvas_geometry_hash;    // For canvas.Geometry
    std::string canvas_text_hash;        // For canvas.Text
    std::string webgl_params_hash;       // For webGlExtensions.parameters
    std::string webgl_extensions_hash;   // For webGlExtensions.extensions
    std::string webgl_context_hash;      // For webGlExtensions.contextAttributes
    std::string webgl_ext_params_hash;   // For webGlExtensions.extensionParameters
    std::string shader_precisions_hash;  // For webGlExtensions.shaderPrecisions
    std::string fonts_hash;              // For font enumeration
    std::string plugins_hash;            // For plugin enumeration

    // Legacy hex strings for logging (16-char uppercase)
    std::string canvas_hex;
    std::string webgl_hex;
    std::string audio_hex;
};

/**
 * OwlFingerprintGenerator - Generates realistic fingerprint hash seeds per context.
 *
 * This class solves the problem of having a limited number of unique fingerprints
 * in the profile database. Instead of using static seeds from the DB, we generate
 * unique, realistic-looking seeds for each browser context.
 *
 * Key features:
 * - Generates seeds that look like real browser fingerprint hashes
 * - Seeds are unique per context_id
 * - Seeds remain consistent for the entire context lifetime
 * - Thread-safe singleton pattern
 *
 * The generated hashes are designed to:
 * - Have realistic entropy distribution (not too uniform, not too patterned)
 * - Look like legitimate browser fingerprint values
 * - Be reproducible given the same context_id (for debugging)
 */
class OwlFingerprintGenerator {
public:
    /**
     * Get the singleton instance.
     */
    static OwlFingerprintGenerator& Instance();

    /**
     * Generate or retrieve fingerprint seeds for a context.
     * If seeds already exist for this context, returns the existing ones.
     * Otherwise, generates new seeds and caches them.
     *
     * @param context_id The browser context identifier
     * @return FingerprintSeeds struct containing all three seed values
     */
    FingerprintSeeds GetSeeds(const std::string& context_id);

    /**
     * Get canvas seed for a context.
     * Convenience method that returns just the canvas seed.
     */
    uint64_t GetCanvasSeed(const std::string& context_id);

    /**
     * Get WebGL seed for a context.
     * Convenience method that returns just the WebGL seed.
     */
    uint64_t GetWebGLSeed(const std::string& context_id);

    /**
     * Get audio seed for a context.
     * Convenience method that returns just the audio seed.
     */
    uint64_t GetAudioSeed(const std::string& context_id);

    /**
     * Get audio fingerprint value for a context.
     * Returns a realistic value in the range ~124.0-124.1
     */
    double GetAudioFingerprint(const std::string& context_id);

    /**
     * Set seeds for a context (used when loading from profile).
     * This allows restoring previously saved seeds instead of generating new ones.
     * If seeds already exist for this context, they will be overwritten.
     *
     * @param context_id The browser context identifier
     * @param seeds The FingerprintSeeds to store for this context
     */
    void SetSeeds(const std::string& context_id, const FingerprintSeeds& seeds);

    /**
     * Check if seeds exist for a context.
     *
     * @param context_id The browser context identifier
     * @return true if seeds are cached for this context
     */
    bool HasSeeds(const std::string& context_id) const;

    /**
     * Clear seeds for a context when it's destroyed.
     * This frees memory and allows a new context with the same ID
     * to get fresh seeds (though context IDs should be unique).
     *
     * @param context_id The browser context identifier
     */
    void ClearContext(const std::string& context_id);

    /**
     * Clear all cached seeds.
     * Useful for testing or when resetting the browser.
     */
    void ClearAll();

    /**
     * Get the number of contexts with cached seeds.
     * Useful for debugging and monitoring.
     */
    size_t GetCachedContextCount() const;

private:
    OwlFingerprintGenerator() = default;
    ~OwlFingerprintGenerator() = default;

    // Non-copyable
    OwlFingerprintGenerator(const OwlFingerprintGenerator&) = delete;
    OwlFingerprintGenerator& operator=(const OwlFingerprintGenerator&) = delete;

    /**
     * Generate new fingerprint seeds.
     * Creates realistic-looking hash values.
     */
    FingerprintSeeds GenerateSeeds();

    /**
     * Generate a single realistic hash seed (64-bit).
     * The hash is designed to look like a real browser fingerprint value.
     */
    uint64_t GenerateRealisticHash();

    /**
     * Generate a 128-bit hash and return as 32-char lowercase hex (MD5-style).
     * This matches the format used by fingerprint.com for various hashes.
     */
    std::string GenerateMD5StyleHash();

    /**
     * Generate a realistic audio fingerprint value.
     * Real Chrome values are typically around 124.04 with slight variations.
     */
    double GenerateAudioFingerprint(uint64_t seed);

    /**
     * Convert a 64-bit value to uppercase hex string (16 chars).
     */
    static std::string ToHexString(uint64_t value);

    /**
     * Convert two 64-bit values to lowercase hex string (32 chars, MD5-style).
     */
    static std::string ToMD5HexString(uint64_t high, uint64_t low);

    // Thread-safe storage
    mutable std::mutex mutex_;
    std::unordered_map<std::string, FingerprintSeeds> context_seeds_;
};

} // namespace owl

#endif // OWL_FINGERPRINT_GENERATOR_H_
