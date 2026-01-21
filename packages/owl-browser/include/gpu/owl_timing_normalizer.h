#pragma once

/**
 * OWL Timing Normalizer
 *
 * Normalizes GPU operation timing to defeat timing-based fingerprinting attacks
 * like DrawnApart. By masking the real GPU's timing characteristics, we prevent
 * identification through shader execution timing analysis.
 *
 * Key Functions:
 * - Draw call timing normalization
 * - Shader compilation timing masking
 * - Operation jitter injection
 * - Timing quantization
 */

#include "gpu/owl_gpu_virtualization.h"
#include "gpu/owl_gpu_profile.h"
#include <chrono>
#include <random>
#include <mutex>
#include <unordered_map>
#include <atomic>

namespace owl {
namespace gpu {

/**
 * Operation type for timing
 */
enum class TimingOperation : uint8_t {
    DrawCall,
    ShaderCompile,
    ShaderLink,
    TextureUpload,
    BufferUpload,
    ReadPixels,
    Finish,
    Flush,
    Other
};

/**
 * Timing normalization configuration
 */
struct TimingNormalizationConfig {
    // Enable/disable
    bool enabled = true;

    // Quantization (rounds timing to multiples)
    bool enable_quantization = true;
    uint32_t quantum_us = 100;            // Quantize to 100μs

    // Jitter injection
    bool enable_jitter = true;
    double jitter_ratio = 0.05;           // ±5% jitter
    uint32_t min_jitter_us = 10;          // Minimum 10μs jitter
    uint32_t max_jitter_us = 1000;        // Maximum 1ms jitter

    // Minimum operation time (floor)
    bool enable_min_time = true;
    uint32_t min_draw_call_us = 50;
    uint32_t min_shader_compile_us = 1000;
    uint32_t min_texture_upload_us = 100;

    // Maximum timing delay (ceiling)
    uint32_t max_delay_us = 5000;         // Max 5ms added delay

    // Profile-based timing emulation
    bool emulate_profile_timing = true;

    // High-resolution timer protection
    bool reduce_timer_precision = true;
    uint32_t timer_precision_us = 100;    // Round to 100μs

    // Performance monitoring protection
    bool mask_performance_api = true;
};

/**
 * Timing sample for analysis
 */
struct TimingSample {
    TimingOperation operation;
    uint64_t start_time_ns;
    uint64_t end_time_ns;
    uint64_t normalized_duration_ns;
    uint64_t delay_added_ns;
    std::string context;
};

/**
 * Timing statistics
 */
struct TimingStatistics {
    uint64_t total_operations = 0;
    uint64_t total_delay_added_ns = 0;
    uint64_t total_quantizations = 0;
    uint64_t total_jitter_applications = 0;

    std::unordered_map<TimingOperation, uint64_t> operation_counts;
    std::unordered_map<TimingOperation, uint64_t> operation_total_time_ns;
    std::unordered_map<TimingOperation, uint64_t> operation_total_delay_ns;
};

/**
 * Timing Normalizer
 *
 * Main class for normalizing GPU operation timing
 */
class TimingNormalizer {
public:
    TimingNormalizer();
    ~TimingNormalizer();

    // Non-copyable
    TimingNormalizer(const TimingNormalizer&) = delete;
    TimingNormalizer& operator=(const TimingNormalizer&) = delete;

    // ==================== Configuration ====================

    void SetConfig(const TimingNormalizationConfig& config);
    const TimingNormalizationConfig& GetConfig() const { return config_; }

    /**
     * Set profile for timing emulation
     */
    void SetProfile(const GPUProfile* profile) { profile_ = profile; }

    // ==================== Operation Timing ====================

    /**
     * Begin timing an operation
     * Returns an operation ID for EndOperation
     */
    uint64_t BeginOperation(TimingOperation op, const char* context = nullptr);

    /**
     * End timing and apply normalization
     * Returns the normalized duration in nanoseconds
     */
    uint64_t EndOperation(uint64_t operation_id);

    /**
     * Normalize a raw timing value
     */
    uint64_t NormalizeTiming(uint64_t raw_time_ns, TimingOperation op);

    /**
     * Add delay to match target timing
     */
    void AddDelay(uint64_t target_time_ns, uint64_t actual_time_ns);

    // ==================== Timer API Protection ====================

    /**
     * Get protected high-resolution time
     * Returns a time value with reduced precision
     */
    uint64_t GetProtectedTime();

    /**
     * Protect performance.now() result
     */
    double ProtectPerformanceNow(double raw_value);

    /**
     * Protect Date.now() result
     */
    int64_t ProtectDateNow(int64_t raw_value);

    // ==================== Profile-Based Timing ====================

    /**
     * Get expected timing for operation on target GPU
     */
    uint64_t GetExpectedTiming(TimingOperation op,
                               const GPUProfile& profile,
                               size_t data_size = 0);

    /**
     * Calculate delay needed to match target timing
     */
    uint64_t CalculateDelayForProfile(uint64_t actual_time_ns,
                                       TimingOperation op,
                                       const GPUProfile& profile);

    // ==================== Jitter Generation ====================

    /**
     * Generate jitter amount for operation
     */
    uint64_t GenerateJitter(uint64_t base_time_ns, TimingOperation op);

    /**
     * Set jitter seed for deterministic behavior
     */
    void SetJitterSeed(uint64_t seed);

    // ==================== Quantization ====================

    /**
     * Quantize timing to quantum multiples
     */
    uint64_t QuantizeTiming(uint64_t time_ns, uint32_t quantum_us);

    /**
     * Round timing to reduced precision
     */
    uint64_t ReducePrecision(uint64_t time_ns, uint32_t precision_us);

    // ==================== Statistics ====================

    TimingStatistics GetStatistics() const;
    void ResetStatistics();

    /**
     * Get recent timing samples (for debugging)
     */
    std::vector<TimingSample> GetRecentSamples(size_t count = 100) const;

private:
    // Active operation tracking
    struct ActiveOperation {
        TimingOperation type;
        std::chrono::high_resolution_clock::time_point start_time;
        std::string context;
    };

    // Generate next operation ID
    uint64_t NextOperationId();

    // Apply all normalization steps
    uint64_t ApplyNormalization(uint64_t raw_time_ns, TimingOperation op);

    // Busy-wait for precise delays
    void PreciseDelay(uint64_t delay_ns);

    TimingNormalizationConfig config_;
    const GPUProfile* profile_ = nullptr;

    // Operation tracking
    mutable std::mutex operations_mutex_;
    std::unordered_map<uint64_t, ActiveOperation> active_operations_;
    std::atomic<uint64_t> next_operation_id_{1};

    // Jitter RNG
    mutable std::mutex rng_mutex_;
    std::mt19937_64 jitter_rng_;
    bool jitter_seeded_ = false;

    // Statistics
    mutable std::mutex stats_mutex_;
    TimingStatistics stats_;

    // Sample history
    mutable std::mutex samples_mutex_;
    std::vector<TimingSample> recent_samples_;
    static constexpr size_t MAX_SAMPLES = 1000;
};

/**
 * Scoped timing helper
 *
 * RAII helper for timing operations
 */
class ScopedTiming {
public:
    ScopedTiming(TimingNormalizer& normalizer, TimingOperation op, const char* context = nullptr)
        : normalizer_(normalizer)
        , operation_id_(normalizer.BeginOperation(op, context))
    {}

    ~ScopedTiming() {
        if (operation_id_ != 0) {
            duration_ns_ = normalizer_.EndOperation(operation_id_);
        }
    }

    // Get normalized duration (only valid after destruction or Cancel)
    uint64_t GetDuration() const { return duration_ns_; }

    // Cancel timing (don't apply normalization)
    void Cancel() { operation_id_ = 0; }

private:
    TimingNormalizer& normalizer_;
    uint64_t operation_id_;
    uint64_t duration_ns_ = 0;
};

/**
 * DrawnApart Defense
 *
 * Specific countermeasures against the DrawnApart timing attack
 */
class DrawnApartDefense {
public:
    /**
     * Check if an operation pattern looks like DrawnApart fingerprinting
     */
    static bool DetectFingerprinting(const std::vector<TimingSample>& samples);

    /**
     * Apply aggressive countermeasures when fingerprinting is detected
     */
    static void ApplyCountermeasures(TimingNormalizer& normalizer);

    /**
     * Generate decoy timing patterns
     */
    static void InjectDecoyPatterns(TimingNormalizer& normalizer);

private:
    // Pattern detection thresholds
    static constexpr int MIN_SUSPICIOUS_DRAWS = 50;
    static constexpr double TIMING_VARIANCE_THRESHOLD = 0.01;
};

/**
 * High-Resolution Timer Protection
 *
 * Protects against timing attacks through browser timing APIs
 */
class TimerProtection {
public:
    /**
     * Get protected performance.now() value
     */
    static double ProtectedPerformanceNow();

    /**
     * Get protected Date.now() value
     */
    static int64_t ProtectedDateNow();

    /**
     * Get protected requestAnimationFrame timestamp
     */
    static double ProtectedRAFTimestamp();

    /**
     * Set timer precision (affects all protected values)
     */
    static void SetPrecision(uint32_t precision_us);

    /**
     * Enable/disable timer jitter
     */
    static void SetJitterEnabled(bool enabled);

private:
    static std::atomic<uint32_t> precision_us_;
    static std::atomic<bool> jitter_enabled_;
    static std::mutex jitter_mutex_;
    static std::mt19937_64 jitter_rng_;
};

} // namespace gpu
} // namespace owl
