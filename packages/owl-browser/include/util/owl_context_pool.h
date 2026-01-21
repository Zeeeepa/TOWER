#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <condition_variable>
#include "owl_thread_pool.h"

// Forward declarations - BrowserContext is defined in owl_browser_manager.h
// We only need the declaration here for ContextGuard
struct BrowserContext;

namespace owl {

// Memory tracking for actual resource measurement
struct MemoryStats {
  std::atomic<size_t> estimated_rss{0};          // Resident set size estimate
  std::atomic<size_t> context_count{0};          // Number of active contexts
  std::atomic<size_t> total_allocated{0};        // Total memory allocated
  std::atomic<size_t> video_recording_count{0};  // Contexts with active recording
  std::atomic<size_t> llm_client_count{0};       // Contexts with LLM clients
};

// Per-context lock for fine-grained synchronization
class ContextLock {
public:
  ContextLock() = default;

  // Acquire exclusive lock for this context
  void lock() { mutex_.lock(); }
  void unlock() { mutex_.unlock(); }
  bool try_lock() { return mutex_.try_lock(); }

  // Track operation count for this context
  void begin_operation() { active_ops_.fetch_add(1, std::memory_order_relaxed); }
  void end_operation() { active_ops_.fetch_sub(1, std::memory_order_relaxed); }
  uint32_t active_operations() const { return active_ops_.load(std::memory_order_relaxed); }

private:
  std::mutex mutex_;
  std::atomic<uint32_t> active_ops_{0};
};

// Context pool configuration
// OPTIMIZED for high-performance automation scenarios
struct ContextPoolConfig {
  size_t max_contexts = 1000;           // Maximum concurrent contexts
  size_t soft_limit = 750;              // Soft limit - start cleanup above this (75% of max)
  size_t max_memory_mb = 32000;         // 32GB max memory
  size_t per_context_memory_mb = 150;   // Estimated per-context memory (reduced from 200MB via CEF flags)
  size_t idle_timeout_sec = 120;        // 2 minutes idle timeout (faster cleanup)
  size_t cleanup_interval_sec = 30;     // Run cleanup every 30 seconds (more responsive)
  bool enable_prewarming = true;        // Pre-warm contexts for faster creation
  size_t prewarm_count = 10;            // Number of contexts to prewarm (doubled)

  // Memory pressure thresholds (as percentage of max_memory)
  static constexpr double kCriticalPressure = 1.0;    // 100% - aggressive cleanup
  static constexpr double kHighPressure = 0.9;        // 90% - moderate cleanup
  static constexpr double kModeratePressure = 0.75;   // 75% - light cleanup
};

// Context pool manages browser contexts with optimized concurrency
class ContextPool {
public:
  explicit ContextPool(const ContextPoolConfig& config = ContextPoolConfig());
  ~ContextPool();

  // Singleton access
  static ContextPool* GetInstance();
  static void Initialize(const ContextPoolConfig& config = ContextPoolConfig());
  static void Destroy();

  // Context lifecycle
  std::string CreateContext();  // Create new context, returns context_id
  void ReleaseContext(const std::string& context_id);  // Mark as available for reuse
  bool DestroyContext(const std::string& context_id);  // Force destroy

  // Context access with fine-grained locking
  // Returns locked context, caller must call UnlockContext when done
  BrowserContext* GetAndLockContext(const std::string& context_id);
  void UnlockContext(const std::string& context_id);

  // Batch operations for efficiency
  std::vector<std::string> CreateContexts(size_t count);
  void DestroyContexts(const std::vector<std::string>& context_ids);

  // Memory management
  MemoryStats& GetMemoryStats() { return memory_stats_; }
  const MemoryStats& GetMemoryStats() const { return memory_stats_; }
  void TriggerCleanup();  // Force cleanup of idle contexts
  void SetMaxMemory(size_t mb) { config_.max_memory_mb = mb; }
  void SetMaxContexts(size_t count) { config_.max_contexts = count; }

  // Pool status
  size_t GetActiveCount() const;
  size_t GetIdleCount() const;
  size_t GetTotalCount() const;
  bool IsAtCapacity() const;

  // Context prewarming for faster creation
  void PrewarmContexts(size_t count);

  // Callback for context cleanup
  using CleanupCallback = std::function<void(const std::string&)>;
  void SetCleanupCallback(CleanupCallback callback) { cleanup_callback_ = callback; }

  // Configuration
  ContextPoolConfig& GetConfig() { return config_; }
  const ContextPoolConfig& GetConfig() const { return config_; }

private:
  struct ContextEntry {
    std::string id;
    std::unique_ptr<ContextLock> lock;
    std::chrono::steady_clock::time_point created;
    std::chrono::steady_clock::time_point last_used;
    std::atomic<bool> in_use{false};
    std::atomic<bool> marked_for_deletion{false};
  };

  // Map from context_id to entry
  std::unordered_map<std::string, std::unique_ptr<ContextEntry>> contexts_;
  RWLock contexts_rwlock_;  // Reader-writer lock for the map

  // ID generation
  std::atomic<uint64_t> next_context_id_{1};
  std::string GenerateContextId();

  // Pre-warmed contexts ready for immediate use
  std::vector<std::string> prewarmed_contexts_;
  std::mutex prewarm_mutex_;

  // Configuration and stats
  ContextPoolConfig config_;
  MemoryStats memory_stats_;

  // Cleanup thread
  std::atomic<bool> cleanup_running_{false};
  std::thread cleanup_thread_;
  std::condition_variable cleanup_cv_;
  std::mutex cleanup_mutex_;
  void CleanupLoop();

  // Callback
  CleanupCallback cleanup_callback_;

  // Singleton
  static std::unique_ptr<ContextPool> instance_;
  static std::mutex instance_mutex_;
};

// RAII context lock guard
class ContextGuard {
public:
  ContextGuard(ContextPool& pool, const std::string& context_id)
      : pool_(pool), context_id_(context_id) {
    context_ = pool_.GetAndLockContext(context_id_);
  }

  ~ContextGuard() {
    if (context_) {
      pool_.UnlockContext(context_id_);
    }
  }

  BrowserContext* get() { return context_; }
  BrowserContext* operator->() { return context_; }
  explicit operator bool() const { return context_ != nullptr; }

  // Non-copyable
  ContextGuard(const ContextGuard&) = delete;
  ContextGuard& operator=(const ContextGuard&) = delete;

  // Movable
  ContextGuard(ContextGuard&& other) noexcept
      : pool_(other.pool_), context_id_(std::move(other.context_id_)), context_(other.context_) {
    other.context_ = nullptr;
  }

private:
  ContextPool& pool_;
  std::string context_id_;
  BrowserContext* context_;
};

// Video recording timer manager - shared timer thread for all recordings
class VideoTimerManager {
public:
  VideoTimerManager();
  ~VideoTimerManager();

  static VideoTimerManager* GetInstance();

  // Register a context for video recording at specified FPS
  void StartTimer(const std::string& context_id, int fps,
                  std::function<void()> invalidate_callback);
  void StopTimer(const std::string& context_id);
  void PauseTimer(const std::string& context_id);
  void ResumeTimer(const std::string& context_id);

  bool IsRecording(const std::string& context_id) const;

private:

  void TimerLoop();

  struct TimerEntry {
    std::string context_id;
    int fps;
    std::chrono::steady_clock::time_point next_frame;
    std::function<void()> invalidate_callback;
    std::atomic<bool> paused{false};
  };

  std::unordered_map<std::string, std::unique_ptr<TimerEntry>> timers_;
  mutable std::mutex timers_mutex_;

  std::atomic<bool> running_{false};
  std::thread timer_thread_;
  std::condition_variable timer_cv_;

  static std::unique_ptr<VideoTimerManager> instance_;
  static std::mutex instance_mutex_;
};

}  // namespace owl
