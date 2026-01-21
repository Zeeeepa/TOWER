/**
 * OWL Timing Normalizer Implementation
 *
 * Normalizes GPU operation timing to defeat DrawnApart and similar
 * timing-based fingerprinting attacks.
 */

#include "gpu/owl_timing_normalizer.h"
#include <thread>
#include <cmath>
#include <algorithm>

namespace owl {
namespace gpu {

// ============================================================================
// Static member initialization
// ============================================================================

std::atomic<uint32_t> TimerProtection::precision_us_{100};
std::atomic<bool> TimerProtection::jitter_enabled_{true};
std::mutex TimerProtection::jitter_mutex_;
std::mt19937_64 TimerProtection::jitter_rng_;

// ============================================================================
// TimingNormalizer Implementation
// ============================================================================

TimingNormalizer::TimingNormalizer() {
    // Initialize with current time as default seed
    auto now = std::chrono::high_resolution_clock::now();
    auto seed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    jitter_rng_.seed(static_cast<uint64_t>(seed));
    jitter_seeded_ = true;
}

TimingNormalizer::~TimingNormalizer() = default;

void TimingNormalizer::SetConfig(const TimingNormalizationConfig& config) {
    config_ = config;
}

uint64_t TimingNormalizer::NextOperationId() {
    return next_operation_id_.fetch_add(1);
}

uint64_t TimingNormalizer::BeginOperation(TimingOperation op, const char* context) {
    if (!config_.enabled) return 0;

    uint64_t id = NextOperationId();

    ActiveOperation active;
    active.type = op;
    active.start_time = std::chrono::high_resolution_clock::now();
    active.context = context ? context : "";

    {
        std::lock_guard<std::mutex> lock(operations_mutex_);
        active_operations_[id] = std::move(active);
    }

    return id;
}

uint64_t TimingNormalizer::EndOperation(uint64_t operation_id) {
    if (operation_id == 0 || !config_.enabled) return 0;

    auto end_time = std::chrono::high_resolution_clock::now();

    ActiveOperation active;
    {
        std::lock_guard<std::mutex> lock(operations_mutex_);
        auto it = active_operations_.find(operation_id);
        if (it == active_operations_.end()) return 0;
        active = std::move(it->second);
        active_operations_.erase(it);
    }

    uint64_t raw_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        end_time - active.start_time).count();

    uint64_t normalized = ApplyNormalization(raw_time_ns, active.type);

    // Calculate and apply delay if needed
    if (normalized > raw_time_ns) {
        AddDelay(normalized, raw_time_ns);
    }

    // Record sample
    {
        std::lock_guard<std::mutex> lock(samples_mutex_);
        if (recent_samples_.size() >= MAX_SAMPLES) {
            recent_samples_.erase(recent_samples_.begin());
        }
        TimingSample sample;
        sample.operation = active.type;
        sample.start_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            active.start_time.time_since_epoch()).count();
        sample.end_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end_time.time_since_epoch()).count();
        sample.normalized_duration_ns = normalized;
        sample.delay_added_ns = (normalized > raw_time_ns) ? (normalized - raw_time_ns) : 0;
        sample.context = active.context;
        recent_samples_.push_back(sample);
    }

    // Update statistics
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_operations++;
        stats_.operation_counts[active.type]++;
        stats_.operation_total_time_ns[active.type] += raw_time_ns;
        if (normalized > raw_time_ns) {
            uint64_t delay = normalized - raw_time_ns;
            stats_.total_delay_added_ns += delay;
            stats_.operation_total_delay_ns[active.type] += delay;
        }
    }

    return normalized;
}

uint64_t TimingNormalizer::NormalizeTiming(uint64_t raw_time_ns, TimingOperation op) {
    return ApplyNormalization(raw_time_ns, op);
}

uint64_t TimingNormalizer::ApplyNormalization(uint64_t raw_time_ns, TimingOperation op) {
    uint64_t result = raw_time_ns;

    // Apply minimum time floor
    if (config_.enable_min_time) {
        uint32_t min_us = 0;
        switch (op) {
            case TimingOperation::DrawCall:
                min_us = config_.min_draw_call_us;
                break;
            case TimingOperation::ShaderCompile:
            case TimingOperation::ShaderLink:
                min_us = config_.min_shader_compile_us;
                break;
            case TimingOperation::TextureUpload:
            case TimingOperation::BufferUpload:
                min_us = config_.min_texture_upload_us;
                break;
            default:
                min_us = 10;  // Default minimum
                break;
        }
        uint64_t min_ns = static_cast<uint64_t>(min_us) * 1000;
        result = std::max(result, min_ns);
    }

    // Apply profile-based timing if available
    if (config_.emulate_profile_timing && profile_) {
        uint64_t expected = GetExpectedTiming(op, *profile_, 0);
        // Blend toward expected timing
        result = (result + expected) / 2;
    }

    // Apply quantization
    if (config_.enable_quantization) {
        result = QuantizeTiming(result, config_.quantum_us);
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_quantizations++;
    }

    // Apply jitter
    if (config_.enable_jitter) {
        result = result + GenerateJitter(result, op);
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_jitter_applications++;
    }

    // Enforce maximum delay ceiling
    if (result - raw_time_ns > static_cast<uint64_t>(config_.max_delay_us) * 1000) {
        result = raw_time_ns + static_cast<uint64_t>(config_.max_delay_us) * 1000;
    }

    return result;
}

void TimingNormalizer::AddDelay(uint64_t target_time_ns, uint64_t actual_time_ns) {
    if (target_time_ns <= actual_time_ns) return;

    uint64_t delay_ns = target_time_ns - actual_time_ns;
    PreciseDelay(delay_ns);
}

void TimingNormalizer::PreciseDelay(uint64_t delay_ns) {
    if (delay_ns < 1000) return;  // Skip delays less than 1μs

    auto start = std::chrono::high_resolution_clock::now();
    auto target = start + std::chrono::nanoseconds(delay_ns);

    // For longer delays, use sleep
    if (delay_ns > 100000) {  // > 100μs
        std::this_thread::sleep_for(std::chrono::nanoseconds(delay_ns - 50000));
    }

    // Busy-wait for the remainder for precision
    while (std::chrono::high_resolution_clock::now() < target) {
        // Spin
    }
}

// ==================== Timer API Protection ====================

uint64_t TimingNormalizer::GetProtectedTime() {
    auto now = std::chrono::high_resolution_clock::now();
    uint64_t time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();

    if (config_.reduce_timer_precision) {
        time_ns = ReducePrecision(time_ns, config_.timer_precision_us);
    }

    return time_ns;
}

double TimingNormalizer::ProtectPerformanceNow(double raw_value) {
    if (!config_.reduce_timer_precision) return raw_value;

    // raw_value is in milliseconds
    double precision_ms = config_.timer_precision_us / 1000.0;
    return std::floor(raw_value / precision_ms) * precision_ms;
}

int64_t TimingNormalizer::ProtectDateNow(int64_t raw_value) {
    if (!config_.reduce_timer_precision) return raw_value;

    // raw_value is in milliseconds
    int64_t precision_ms = std::max(1, static_cast<int>(config_.timer_precision_us / 1000));
    return (raw_value / precision_ms) * precision_ms;
}

// ==================== Profile-Based Timing ====================

uint64_t TimingNormalizer::GetExpectedTiming(TimingOperation op,
                                              const GPUProfile& profile,
                                              size_t data_size) {
    const auto& timing = profile.GetTimingProfile();

    uint64_t base_us = 0;
    switch (op) {
        case TimingOperation::DrawCall:
            base_us = timing.draw_call_base_us;
            break;
        case TimingOperation::ShaderCompile:
        case TimingOperation::ShaderLink:
            base_us = timing.shader_compile_base_us;
            break;
        case TimingOperation::TextureUpload:
        case TimingOperation::BufferUpload:
            base_us = timing.texture_upload_per_kb_us * (data_size / 1024 + 1);
            break;
        case TimingOperation::ReadPixels:
            base_us = timing.buffer_map_us + (data_size / 1024) * 2;
            break;
        default:
            base_us = 50;
            break;
    }

    return base_us * 1000;  // Convert to nanoseconds
}

uint64_t TimingNormalizer::CalculateDelayForProfile(uint64_t actual_time_ns,
                                                     TimingOperation op,
                                                     const GPUProfile& profile) {
    uint64_t expected = GetExpectedTiming(op, profile, 0);
    if (actual_time_ns >= expected) return 0;
    return expected - actual_time_ns;
}

// ==================== Jitter Generation ====================

uint64_t TimingNormalizer::GenerateJitter(uint64_t base_time_ns, TimingOperation op) {
    std::lock_guard<std::mutex> lock(rng_mutex_);

    // Calculate jitter range
    uint64_t max_jitter_ns = static_cast<uint64_t>(base_time_ns * config_.jitter_ratio);

    // Apply min/max bounds
    max_jitter_ns = std::max(max_jitter_ns,
        static_cast<uint64_t>(config_.min_jitter_us) * 1000);
    max_jitter_ns = std::min(max_jitter_ns,
        static_cast<uint64_t>(config_.max_jitter_us) * 1000);

    // Generate random jitter (both positive and negative)
    std::uniform_int_distribution<int64_t> dist(
        -static_cast<int64_t>(max_jitter_ns / 2),
        static_cast<int64_t>(max_jitter_ns / 2));

    int64_t jitter = dist(jitter_rng_);

    // Ensure we don't go negative overall
    if (jitter < 0 && static_cast<uint64_t>(-jitter) > base_time_ns / 2) {
        jitter = -static_cast<int64_t>(base_time_ns / 4);
    }

    return static_cast<uint64_t>(jitter > 0 ? jitter : 0);
}

void TimingNormalizer::SetJitterSeed(uint64_t seed) {
    std::lock_guard<std::mutex> lock(rng_mutex_);
    jitter_rng_.seed(seed);
    jitter_seeded_ = true;
}

// ==================== Quantization ====================

uint64_t TimingNormalizer::QuantizeTiming(uint64_t time_ns, uint32_t quantum_us) {
    uint64_t quantum_ns = static_cast<uint64_t>(quantum_us) * 1000;
    return ((time_ns + quantum_ns / 2) / quantum_ns) * quantum_ns;
}

uint64_t TimingNormalizer::ReducePrecision(uint64_t time_ns, uint32_t precision_us) {
    return QuantizeTiming(time_ns, precision_us);
}

// ==================== Statistics ====================

TimingStatistics TimingNormalizer::GetStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void TimingNormalizer::ResetStatistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = TimingStatistics{};
}

std::vector<TimingSample> TimingNormalizer::GetRecentSamples(size_t count) const {
    std::lock_guard<std::mutex> lock(samples_mutex_);
    size_t start = (recent_samples_.size() > count) ?
        (recent_samples_.size() - count) : 0;
    return std::vector<TimingSample>(
        recent_samples_.begin() + start,
        recent_samples_.end());
}

// ============================================================================
// DrawnApartDefense Implementation
// ============================================================================

bool DrawnApartDefense::DetectFingerprinting(const std::vector<TimingSample>& samples) {
    // DrawnApart uses many rapid, similar draw calls to measure timing variance
    if (samples.size() < static_cast<size_t>(MIN_SUSPICIOUS_DRAWS)) return false;

    // Count recent draw calls
    int draw_count = 0;
    uint64_t total_time = 0;
    uint64_t min_time = UINT64_MAX;
    uint64_t max_time = 0;

    for (const auto& sample : samples) {
        if (sample.operation == TimingOperation::DrawCall) {
            draw_count++;
            uint64_t duration = sample.end_time_ns - sample.start_time_ns;
            total_time += duration;
            min_time = std::min(min_time, duration);
            max_time = std::max(max_time, duration);
        }
    }

    if (draw_count < MIN_SUSPICIOUS_DRAWS) return false;

    // Check for suspiciously low variance (indicates precision timing attack)
    double avg_time = static_cast<double>(total_time) / draw_count;
    double variance = static_cast<double>(max_time - min_time) / avg_time;

    return variance < TIMING_VARIANCE_THRESHOLD;
}

void DrawnApartDefense::ApplyCountermeasures(TimingNormalizer& normalizer) {
    // When fingerprinting is detected, increase jitter and quantization
    auto config = normalizer.GetConfig();
    config.jitter_ratio *= 2.0;
    config.quantum_us *= 2;
    config.max_jitter_us *= 2;
    normalizer.SetConfig(config);
}

void DrawnApartDefense::InjectDecoyPatterns(TimingNormalizer& normalizer) {
    // Inject artificial timing variations to confuse fingerprinting
    auto config = normalizer.GetConfig();
    config.enable_jitter = true;
    config.jitter_ratio = 0.15;  // 15% jitter
    normalizer.SetConfig(config);
}

// ============================================================================
// TimerProtection Implementation
// ============================================================================

double TimerProtection::ProtectedPerformanceNow() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    double ms = std::chrono::duration<double, std::milli>(duration).count();

    uint32_t precision = precision_us_.load();
    double precision_ms = precision / 1000.0;

    double result = std::floor(ms / precision_ms) * precision_ms;

    if (jitter_enabled_.load()) {
        std::lock_guard<std::mutex> lock(jitter_mutex_);
        std::uniform_real_distribution<double> dist(0, precision_ms);
        result += dist(jitter_rng_);
    }

    return result;
}

int64_t TimerProtection::ProtectedDateNow() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

    uint32_t precision = precision_us_.load();
    int64_t precision_ms = std::max(1, static_cast<int>(precision / 1000));

    int64_t result = (ms / precision_ms) * precision_ms;

    if (jitter_enabled_.load()) {
        std::lock_guard<std::mutex> lock(jitter_mutex_);
        std::uniform_int_distribution<int64_t> dist(0, precision_ms);
        result += dist(jitter_rng_);
    }

    return result;
}

double TimerProtection::ProtectedRAFTimestamp() {
    return ProtectedPerformanceNow();
}

void TimerProtection::SetPrecision(uint32_t precision_us) {
    precision_us_.store(precision_us);
}

void TimerProtection::SetJitterEnabled(bool enabled) {
    jitter_enabled_.store(enabled);
}

} // namespace gpu
} // namespace owl
