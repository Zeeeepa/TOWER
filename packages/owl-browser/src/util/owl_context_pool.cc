#include "owl_context_pool.h"
#include "owl_browser_manager.h"
#include "logger.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace owl {

// Static members
std::unique_ptr<ContextPool> ContextPool::instance_;
std::mutex ContextPool::instance_mutex_;

std::unique_ptr<VideoTimerManager> VideoTimerManager::instance_;
std::mutex VideoTimerManager::instance_mutex_;

// ============================================================
// ContextPool Implementation
// ============================================================

ContextPool::ContextPool(const ContextPoolConfig& config)
    : config_(config) {
  // Start cleanup thread
  cleanup_running_.store(true, std::memory_order_release);
  cleanup_thread_ = std::thread(&ContextPool::CleanupLoop, this);

  LOG_DEBUG("ContextPool", "Initialized with max_contexts=" + std::to_string(config_.max_contexts) +
           ", max_memory=" + std::to_string(config_.max_memory_mb) + "MB");
}

ContextPool::~ContextPool() {
  // Stop cleanup thread
  cleanup_running_.store(false, std::memory_order_release);
  cleanup_cv_.notify_all();

  if (cleanup_thread_.joinable()) {
    cleanup_thread_.join();
  }

  // Destroy all contexts
  WriteGuard guard(contexts_rwlock_);
  contexts_.clear();
}

ContextPool* ContextPool::GetInstance() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  return instance_.get();
}

void ContextPool::Initialize(const ContextPoolConfig& config) {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (!instance_) {
    instance_ = std::make_unique<ContextPool>(config);
  }
}

void ContextPool::Destroy() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  instance_.reset();
}

std::string ContextPool::GenerateContextId() {
  uint64_t id = next_context_id_.fetch_add(1, std::memory_order_relaxed);
  std::stringstream ss;
  ss << "ctx_" << std::setfill('0') << std::setw(6) << id;
  return ss.str();
}

std::string ContextPool::CreateContext() {
  // Check capacity
  size_t current = memory_stats_.context_count.load(std::memory_order_relaxed);
  if (current >= config_.max_contexts) {
    LOG_WARN("ContextPool", "At capacity (" + std::to_string(current) + "/" +
             std::to_string(config_.max_contexts) + "), triggering cleanup");
    TriggerCleanup();

    // Re-check after cleanup
    current = memory_stats_.context_count.load(std::memory_order_relaxed);
    if (current >= config_.max_contexts) {
      LOG_ERROR("ContextPool", "Still at capacity after cleanup, cannot create context");
      return "";
    }
  }

  // Try to reuse prewarmed context
  {
    std::lock_guard<std::mutex> lock(prewarm_mutex_);
    if (!prewarmed_contexts_.empty()) {
      std::string ctx_id = std::move(prewarmed_contexts_.back());
      prewarmed_contexts_.pop_back();

      // Mark as in use
      ReadGuard rg(contexts_rwlock_);
      auto it = contexts_.find(ctx_id);
      if (it != contexts_.end()) {
        it->second->in_use.store(true, std::memory_order_release);
        it->second->last_used = std::chrono::steady_clock::now();
        LOG_DEBUG("ContextPool", "Reused prewarmed context: " + ctx_id);
        return ctx_id;
      }
    }
  }

  // Create new context
  std::string context_id = GenerateContextId();

  auto entry = std::make_unique<ContextEntry>();
  entry->id = context_id;
  entry->lock = std::make_unique<ContextLock>();
  entry->created = std::chrono::steady_clock::now();
  entry->last_used = std::chrono::steady_clock::now();
  entry->in_use.store(true, std::memory_order_release);

  {
    WriteGuard guard(contexts_rwlock_);
    contexts_[context_id] = std::move(entry);
  }

  memory_stats_.context_count.fetch_add(1, std::memory_order_relaxed);
  memory_stats_.estimated_rss.fetch_add(
      config_.per_context_memory_mb * 1024 * 1024, std::memory_order_relaxed);

  LOG_DEBUG("ContextPool", "Created context: " + context_id +
            " (total: " + std::to_string(GetTotalCount()) + ")");

  return context_id;
}

void ContextPool::ReleaseContext(const std::string& context_id) {
  ReadGuard guard(contexts_rwlock_);

  auto it = contexts_.find(context_id);
  if (it != contexts_.end()) {
    it->second->in_use.store(false, std::memory_order_release);
    it->second->last_used = std::chrono::steady_clock::now();
    LOG_DEBUG("ContextPool", "Released context: " + context_id);
  }
}

bool ContextPool::DestroyContext(const std::string& context_id) {
  std::unique_ptr<ContextEntry> entry_to_destroy;

  {
    WriteGuard guard(contexts_rwlock_);

    auto it = contexts_.find(context_id);
    if (it == contexts_.end()) {
      return false;
    }

    // Mark for deletion first
    it->second->marked_for_deletion.store(true, std::memory_order_release);

    // Wait for any active operations to complete
    while (it->second->lock->active_operations() > 0) {
      // Brief sleep to avoid busy waiting
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Now safe to remove
    entry_to_destroy = std::move(it->second);
    contexts_.erase(it);
  }

  // Call cleanup callback outside the lock
  if (cleanup_callback_) {
    cleanup_callback_(context_id);
  }

  memory_stats_.context_count.fetch_sub(1, std::memory_order_relaxed);
  memory_stats_.estimated_rss.fetch_sub(
      config_.per_context_memory_mb * 1024 * 1024, std::memory_order_relaxed);

  LOG_DEBUG("ContextPool", "Destroyed context: " + context_id);
  return true;
}

BrowserContext* ContextPool::GetAndLockContext(const std::string& context_id) {
  ReadGuard guard(contexts_rwlock_);

  auto it = contexts_.find(context_id);
  if (it == contexts_.end() || it->second->marked_for_deletion.load(std::memory_order_acquire)) {
    return nullptr;
  }

  // Lock the context
  it->second->lock->lock();
  it->second->lock->begin_operation();
  it->second->last_used = std::chrono::steady_clock::now();

  // Get the actual BrowserContext from OwlBrowserManager
  // This is a bridge to the existing system
  return nullptr;  // Caller should use OwlBrowserManager::GetBrowser()
}

void ContextPool::UnlockContext(const std::string& context_id) {
  ReadGuard guard(contexts_rwlock_);

  auto it = contexts_.find(context_id);
  if (it != contexts_.end()) {
    it->second->lock->end_operation();
    it->second->lock->unlock();
  }
}

std::vector<std::string> ContextPool::CreateContexts(size_t count) {
  std::vector<std::string> result;
  result.reserve(count);

  for (size_t i = 0; i < count; ++i) {
    std::string ctx_id = CreateContext();
    if (!ctx_id.empty()) {
      result.push_back(std::move(ctx_id));
    } else {
      break;  // Stop if we can't create more
    }
  }

  return result;
}

void ContextPool::DestroyContexts(const std::vector<std::string>& context_ids) {
  // Use thread pool for parallel destruction
  auto* pool = ThreadPool::GetInstance();
  if (!pool) {
    // Fall back to sequential
    for (const auto& ctx_id : context_ids) {
      DestroyContext(ctx_id);
    }
    return;
  }

  std::vector<std::future<bool>> futures;
  futures.reserve(context_ids.size());

  for (const auto& ctx_id : context_ids) {
    futures.push_back(pool->Submit(TaskPriority::LOW, [this, ctx_id]() {
      return DestroyContext(ctx_id);
    }));
  }

  // Wait for all to complete
  for (auto& f : futures) {
    f.wait();
  }
}

void ContextPool::TriggerCleanup() {
  cleanup_cv_.notify_one();
}

size_t ContextPool::GetActiveCount() const {
  ReadGuard guard(const_cast<RWLock&>(contexts_rwlock_));

  size_t count = 0;
  for (const auto& pair : contexts_) {
    if (pair.second->in_use.load(std::memory_order_relaxed)) {
      ++count;
    }
  }
  return count;
}

size_t ContextPool::GetIdleCount() const {
  return GetTotalCount() - GetActiveCount();
}

size_t ContextPool::GetTotalCount() const {
  return memory_stats_.context_count.load(std::memory_order_relaxed);
}

bool ContextPool::IsAtCapacity() const {
  return GetTotalCount() >= config_.max_contexts;
}

void ContextPool::PrewarmContexts(size_t count) {
  auto* pool = ThreadPool::GetInstance();
  if (!pool) {
    return;
  }

  pool->Submit(TaskPriority::LOW, [this, count]() {
    for (size_t i = 0; i < count; ++i) {
      std::string ctx_id = CreateContext();
      if (ctx_id.empty()) {
        break;
      }

      // Mark as not in use and add to prewarm list
      {
        ReadGuard guard(contexts_rwlock_);
        auto it = contexts_.find(ctx_id);
        if (it != contexts_.end()) {
          it->second->in_use.store(false, std::memory_order_release);
        }
      }

      {
        std::lock_guard<std::mutex> lock(prewarm_mutex_);
        prewarmed_contexts_.push_back(std::move(ctx_id));
      }
    }

    LOG_DEBUG("ContextPool", "Prewarmed " + std::to_string(count) + " contexts");
  });
}

void ContextPool::CleanupLoop() {
  while (cleanup_running_.load(std::memory_order_acquire)) {
    std::unique_lock<std::mutex> lock(cleanup_mutex_);
    cleanup_cv_.wait_for(lock, std::chrono::seconds(config_.cleanup_interval_sec));

    if (!cleanup_running_.load(std::memory_order_acquire)) {
      break;
    }

    // Find contexts to clean up
    std::vector<std::string> to_cleanup;
    auto now = std::chrono::steady_clock::now();

    {
      ReadGuard guard(contexts_rwlock_);

      for (const auto& pair : contexts_) {
        // Skip if in use or already marked for deletion
        if (pair.second->in_use.load(std::memory_order_relaxed) ||
            pair.second->marked_for_deletion.load(std::memory_order_relaxed)) {
          continue;
        }

        // Check idle timeout
        auto idle_time = std::chrono::duration_cast<std::chrono::seconds>(
            now - pair.second->last_used).count();

        if (idle_time > static_cast<int64_t>(config_.idle_timeout_sec)) {
          to_cleanup.push_back(pair.first);
        }
      }
    }

    // Also cleanup if above soft limit
    size_t current = GetTotalCount();
    if (current > config_.soft_limit) {
      size_t excess = current - config_.soft_limit;
      LOG_DEBUG("ContextPool", "Above soft limit (" + std::to_string(current) +
               "/" + std::to_string(config_.soft_limit) + "), cleaning " +
               std::to_string(excess) + " extra contexts");

      ReadGuard guard(contexts_rwlock_);

      // Find oldest idle contexts
      std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> idle_contexts;
      for (const auto& pair : contexts_) {
        if (!pair.second->in_use.load(std::memory_order_relaxed) &&
            !pair.second->marked_for_deletion.load(std::memory_order_relaxed)) {
          idle_contexts.emplace_back(pair.first, pair.second->last_used);
        }
      }

      // Sort by last used time
      std::sort(idle_contexts.begin(), idle_contexts.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; });

      // Add oldest to cleanup list
      for (size_t i = 0; i < std::min(excess, idle_contexts.size()); ++i) {
        if (std::find(to_cleanup.begin(), to_cleanup.end(), idle_contexts[i].first) == to_cleanup.end()) {
          to_cleanup.push_back(idle_contexts[i].first);
        }
      }
    }

    // Perform cleanup
    if (!to_cleanup.empty()) {
      LOG_DEBUG("ContextPool", "Cleaning up " + std::to_string(to_cleanup.size()) + " contexts");
      DestroyContexts(to_cleanup);
    }
  }
}

// ============================================================
// VideoTimerManager Implementation
// ============================================================

VideoTimerManager::VideoTimerManager() {
  running_.store(true, std::memory_order_release);
  timer_thread_ = std::thread(&VideoTimerManager::TimerLoop, this);
}

VideoTimerManager::~VideoTimerManager() {
  running_.store(false, std::memory_order_release);
  timer_cv_.notify_all();

  if (timer_thread_.joinable()) {
    timer_thread_.join();
  }
}

VideoTimerManager* VideoTimerManager::GetInstance() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (!instance_) {
    instance_ = std::make_unique<VideoTimerManager>();
  }
  return instance_.get();
}

void VideoTimerManager::StartTimer(const std::string& context_id, int fps,
                                    std::function<void()> invalidate_callback) {
  std::lock_guard<std::mutex> lock(timers_mutex_);

  auto entry = std::make_unique<TimerEntry>();
  entry->context_id = context_id;
  entry->fps = fps;
  entry->next_frame = std::chrono::steady_clock::now();
  entry->invalidate_callback = std::move(invalidate_callback);
  entry->paused.store(false, std::memory_order_relaxed);

  timers_[context_id] = std::move(entry);
  timer_cv_.notify_one();

  LOG_DEBUG("VideoTimerManager", "Started timer for context " + context_id +
            " at " + std::to_string(fps) + " FPS");
}

void VideoTimerManager::StopTimer(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(timers_mutex_);
  timers_.erase(context_id);
  LOG_DEBUG("VideoTimerManager", "Stopped timer for context " + context_id);
}

void VideoTimerManager::PauseTimer(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(timers_mutex_);
  auto it = timers_.find(context_id);
  if (it != timers_.end()) {
    it->second->paused.store(true, std::memory_order_release);
  }
}

void VideoTimerManager::ResumeTimer(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(timers_mutex_);
  auto it = timers_.find(context_id);
  if (it != timers_.end()) {
    it->second->paused.store(false, std::memory_order_release);
    it->second->next_frame = std::chrono::steady_clock::now();
    timer_cv_.notify_one();
  }
}

bool VideoTimerManager::IsRecording(const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(timers_mutex_);
  return timers_.find(context_id) != timers_.end();
}

void VideoTimerManager::TimerLoop() {
  while (running_.load(std::memory_order_acquire)) {
    std::vector<std::function<void()>> callbacks_to_invoke;
    std::chrono::steady_clock::time_point next_wakeup;
    bool has_timers = false;

    {
      std::unique_lock<std::mutex> lock(timers_mutex_);

      if (timers_.empty()) {
        timer_cv_.wait(lock, [this]() {
          return !running_.load(std::memory_order_acquire) || !timers_.empty();
        });
        continue;
      }

      auto now = std::chrono::steady_clock::now();
      next_wakeup = now + std::chrono::seconds(1);  // Default fallback

      for (auto& pair : timers_) {
        auto& entry = pair.second;

        if (entry->paused.load(std::memory_order_acquire)) {
          continue;
        }

        if (now >= entry->next_frame) {
          // Time to fire
          callbacks_to_invoke.push_back(entry->invalidate_callback);

          // Schedule next frame
          int interval_ms = 1000 / entry->fps;
          entry->next_frame = now + std::chrono::milliseconds(interval_ms);
        }

        // Track earliest next wakeup
        if (entry->next_frame < next_wakeup) {
          next_wakeup = entry->next_frame;
        }

        has_timers = true;
      }
    }

    // Invoke callbacks outside the lock - no try/catch since exceptions are disabled
    for (auto& callback : callbacks_to_invoke) {
      callback();
    }

    // Sleep until next frame is needed
    if (has_timers) {
      auto sleep_time = std::chrono::duration_cast<std::chrono::milliseconds>(
          next_wakeup - std::chrono::steady_clock::now());

      if (sleep_time.count() > 0) {
        std::unique_lock<std::mutex> lock(timers_mutex_);
        timer_cv_.wait_for(lock, sleep_time);
      }
    }
  }
}

}  // namespace owl
