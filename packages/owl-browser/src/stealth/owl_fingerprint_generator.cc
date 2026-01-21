#include "stealth/owl_fingerprint_generator.h"
#include "util/logger.h"

#include <random>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace owl {

OwlFingerprintGenerator& OwlFingerprintGenerator::Instance() {
    static OwlFingerprintGenerator instance;
    return instance;
}

FingerprintSeeds OwlFingerprintGenerator::GetSeeds(const std::string& context_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if we already have seeds for this context
    auto it = context_seeds_.find(context_id);
    if (it != context_seeds_.end()) {
        return it->second;
    }

    // Generate new seeds for this context
    FingerprintSeeds seeds = GenerateSeeds();

    // Cache for the context's lifetime
    context_seeds_[context_id] = seeds;

    LOG_DEBUG("FingerprintGenerator",
        "Generated fingerprint seeds for context " + context_id +
        " (Canvas: " + seeds.canvas_hex +
        ", WebGL: " + seeds.webgl_hex +
        ", Audio: " + std::to_string(seeds.audio_fingerprint) + ")");

    return seeds;
}

uint64_t OwlFingerprintGenerator::GetCanvasSeed(const std::string& context_id) {
    return GetSeeds(context_id).canvas_seed;
}

uint64_t OwlFingerprintGenerator::GetWebGLSeed(const std::string& context_id) {
    return GetSeeds(context_id).webgl_seed;
}

uint64_t OwlFingerprintGenerator::GetAudioSeed(const std::string& context_id) {
    return GetSeeds(context_id).audio_seed;
}

double OwlFingerprintGenerator::GetAudioFingerprint(const std::string& context_id) {
    return GetSeeds(context_id).audio_fingerprint;
}

void OwlFingerprintGenerator::ClearContext(const std::string& context_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    context_seeds_.erase(context_id);
    LOG_DEBUG("FingerprintGenerator", "Cleared seeds for context " + context_id);
}

void OwlFingerprintGenerator::SetSeeds(const std::string& context_id, const FingerprintSeeds& seeds) {
    std::lock_guard<std::mutex> lock(mutex_);
    context_seeds_[context_id] = seeds;
    LOG_DEBUG("FingerprintGenerator",
        "Set seeds for context " + context_id +
        " (Canvas: " + seeds.canvas_hex +
        ", Audio: " + std::to_string(seeds.audio_fingerprint) + ")");
}

bool OwlFingerprintGenerator::HasSeeds(const std::string& context_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return context_seeds_.find(context_id) != context_seeds_.end();
}

void OwlFingerprintGenerator::ClearAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    context_seeds_.clear();
    LOG_DEBUG("FingerprintGenerator", "Cleared all cached fingerprint seeds");
}

size_t OwlFingerprintGenerator::GetCachedContextCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return context_seeds_.size();
}

FingerprintSeeds OwlFingerprintGenerator::GenerateSeeds() {
    FingerprintSeeds seeds;

    // Generate core 64-bit seeds - each completely independent
    seeds.canvas_seed = GenerateRealisticHash();
    seeds.webgl_seed = GenerateRealisticHash();
    seeds.audio_seed = GenerateRealisticHash();
    seeds.fonts_seed = GenerateRealisticHash();
    seeds.client_rects_seed = GenerateRealisticHash();
    seeds.navigator_seed = GenerateRealisticHash();
    seeds.screen_seed = GenerateRealisticHash();

    // Generate audio fingerprint value (realistic range: ~124.0 - 124.1)
    // This is the most critical value for bot detection - must be EXACTLY in range
    seeds.audio_fingerprint = GenerateAudioFingerprint(seeds.audio_seed);

    // Generate 32-char MD5-style hashes for fingerprint.com compatibility
    // Each hash should be completely independent with realistic distribution
    seeds.canvas_geometry_hash = GenerateMD5StyleHash();
    seeds.canvas_text_hash = GenerateMD5StyleHash();
    seeds.webgl_params_hash = GenerateMD5StyleHash();
    seeds.webgl_extensions_hash = GenerateMD5StyleHash();
    seeds.webgl_context_hash = GenerateMD5StyleHash();
    seeds.webgl_ext_params_hash = GenerateMD5StyleHash();
    seeds.shader_precisions_hash = GenerateMD5StyleHash();
    seeds.fonts_hash = GenerateMD5StyleHash();
    seeds.plugins_hash = GenerateMD5StyleHash();

    // Legacy hex strings for logging
    seeds.canvas_hex = ToHexString(seeds.canvas_seed);
    seeds.webgl_hex = ToHexString(seeds.webgl_seed);
    seeds.audio_hex = ToHexString(seeds.audio_seed);

    return seeds;
}

double OwlFingerprintGenerator::GenerateAudioFingerprint(uint64_t seed) {
    // Real Chrome audio fingerprints are VERY specific - they come from
    // AudioContext's oscillator + dynamics compressor processing.
    //
    // Observed real Chrome values (different machines):
    // - 124.04344968475198 (common baseline)
    // - 124.04347527516074
    // - 124.04347657808103
    // - 124.08075528279005
    // - 124.08072766105096
    //
    // The value depends on:
    // 1. Audio hardware/drivers
    // 2. Sample rate (usually 44100 or 48000)
    // 3. OS audio processing
    //
    // Key insight: Values cluster around 124.04 and 124.08
    // Values outside 124.00-124.10 are instantly flagged as fake

    std::mt19937_64 gen(seed);

    // Choose a cluster center (most values are around 124.04 or 124.08)
    std::uniform_int_distribution<int> cluster_dist(0, 2);
    int cluster = cluster_dist(gen);

    double base;
    switch (cluster) {
        case 0:
            // Most common cluster: around 124.043xx
            base = 124.043;
            break;
        case 1:
            // Secondary cluster: around 124.080xx
            base = 124.080;
            break;
        default:
            // Less common: around 124.04x
            base = 124.040;
            break;
    }

    // Add realistic micro-variation (4th-5th decimal place)
    // Real values have ~14 decimal places with realistic distribution
    std::uniform_real_distribution<double> micro_dist(0.0, 0.0009);
    double micro = micro_dist(gen);

    // Add sub-micro variation for the remaining decimal places
    std::uniform_real_distribution<double> sub_micro_dist(0.0, 0.00009);
    double sub_micro = sub_micro_dist(gen);

    double result = base + micro + sub_micro;

    // Ensure we're in the valid range (paranoid check)
    if (result < 124.00 || result > 124.10) {
        result = 124.04344968475198; // Fallback to most common value
    }

    return result;
}

std::string OwlFingerprintGenerator::GenerateMD5StyleHash() {
    // Generate two 64-bit values for a full 128-bit hash
    uint64_t high = GenerateRealisticHash();
    uint64_t low = GenerateRealisticHash();

    return ToMD5HexString(high, low);
}

std::string OwlFingerprintGenerator::ToMD5HexString(uint64_t high, uint64_t low) {
    // Convert to 32-char lowercase hex (like MD5 format used by fingerprint.com)
    std::ostringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << high
       << std::setw(16) << std::setfill('0') << low;
    return ss.str();
}

uint64_t OwlFingerprintGenerator::GenerateRealisticHash() {
    // Use multiple entropy sources for high-quality randomness
    std::random_device rd;

    // Combine random_device with high-resolution time for extra entropy
    auto time_seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();

    // Create a seed sequence from multiple sources
    std::seed_seq seed_seq{rd(), rd(), rd(), rd(),
                          static_cast<unsigned int>(time_seed & 0xFFFFFFFF),
                          static_cast<unsigned int>((time_seed >> 32) & 0xFFFFFFFF)};

    std::mt19937_64 gen(seed_seq);

    // Generate base random value
    uint64_t hash = gen();

    // Apply transformations to make it look more like a real fingerprint hash
    // Real browser fingerprint hashes tend to have certain characteristics:
    // - Not all zeros or all ones
    // - Good distribution of bits
    // - Some variation in nibble values

    // Ensure we don't have degenerate values
    if (hash == 0 || hash == ~0ULL) {
        hash = gen();
    }

    // Apply a mixing function to improve distribution
    // Using xxHash-style mixing
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ULL;
    hash ^= hash >> 33;

    // Ensure the hash looks realistic:
    // - Has at least 3 different hex digits in the upper and lower halves
    // - Is not a simple pattern like 0x1234567890ABCDEF

    // Count unique nibbles
    uint64_t temp = hash;
    uint16_t nibble_mask = 0;
    for (int i = 0; i < 16; i++) {
        nibble_mask |= (1 << (temp & 0xF));
        temp >>= 4;
    }

    // If too few unique nibbles (less than 6), remix
    int unique_nibbles = __builtin_popcount(nibble_mask);
    if (unique_nibbles < 6) {
        hash ^= gen();
        hash *= 0x9e3779b97f4a7c15ULL;  // Golden ratio based constant
    }

    return hash;
}

std::string OwlFingerprintGenerator::ToHexString(uint64_t value) {
    std::ostringstream ss;
    ss << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << value;
    return ss.str();
}

} // namespace owl
