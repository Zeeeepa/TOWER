#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <memory>
#include <chrono>
#include <unordered_map>
#include <string>

// High-performance thread pool optimized for 100+ concurrent browser contexts
// Features:
// - Work stealing for load balancing
// - Priority queues for urgent tasks
// - Per-thread task affinity for cache locality
// - Dynamic scaling based on load
// - Minimal context switching overhead

namespace owl {

enum class TaskPriority {
  LOW = 0,
  NORMAL = 1,
  HIGH = 2,
  CRITICAL = 3  // For user-facing operations like screenshots
};

struct TaskMetrics {
  std::atomic<uint64_t> tasks_submitted{0};
  std::atomic<uint64_t> tasks_completed{0};
  std::atomic<uint64_t> tasks_failed{0};
  std::atomic<uint64_t> total_wait_time_us{0};
  std::atomic<uint64_t> total_exec_time_us{0};
  std::atomic<uint32_t> active_workers{0};
  std::atomic<uint32_t> idle_workers{0};
  std::atomic<uint32_t> queue_depth{0};
};

class ThreadPool {
public:
  // Create thread pool with specified number of workers
  // If num_threads = 0, uses hardware_concurrency * 2 (for I/O-bound tasks)
  explicit ThreadPool(size_t num_threads = 0);
  ~ThreadPool();

  // Non-copyable, non-movable
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
  ThreadPool(ThreadPool&&) = delete;
  ThreadPool& operator=(ThreadPool&&) = delete;

  // Submit a task with priority, returns future for result
  template<typename F, typename... Args>
  auto Submit(TaskPriority priority, F&& f, Args&&... args)
      -> std::future<typename std::invoke_result<F, Args...>::type>;

  // Submit normal priority task (convenience)
  template<typename F, typename... Args>
  auto Submit(F&& f, Args&&... args)
      -> std::future<typename std::invoke_result<F, Args...>::type> {
    return Submit(TaskPriority::NORMAL, std::forward<F>(f), std::forward<Args>(args)...);
  }

  // Submit task for specific context (enables affinity-based scheduling)
  template<typename F, typename... Args>
  auto SubmitForContext(const std::string& context_id, TaskPriority priority, F&& f, Args&&... args)
      -> std::future<typename std::invoke_result<F, Args...>::type>;

  // Shutdown the pool (waits for all tasks to complete)
  void Shutdown();

  // Get current metrics
  TaskMetrics& GetMetrics() { return metrics_; }
  const TaskMetrics& GetMetrics() const { return metrics_; }

  // Get pool status
  size_t GetWorkerCount() const { return workers_.size(); }
  size_t GetQueueSize() const;
  bool IsShutdown() const { return shutdown_.load(std::memory_order_acquire); }

  // Dynamic scaling
  void SetMinWorkers(size_t min) { min_workers_ = min; }
  void SetMaxWorkers(size_t max) { max_workers_ = max; }
  void ScaleWorkers(size_t target);

  // Singleton access for global pool
  static ThreadPool* GetInstance();
  static void Initialize(size_t num_threads = 0);
  static void Destroy();

private:
  struct Task {
    std::function<void()> func;
    TaskPriority priority;
    std::chrono::steady_clock::time_point submitted;
    std::string context_id;  // For affinity scheduling

    // Priority comparison (higher priority = should be processed first)
    bool operator<(const Task& other) const {
      return static_cast<int>(priority) < static_cast<int>(other.priority);
    }
  };

  void WorkerLoop(size_t worker_id);
  bool TryStealTask(Task& task, size_t thief_id);

  // Workers
  std::vector<std::thread> workers_;
  std::vector<std::unique_ptr<std::atomic<bool>>> worker_active_;  // Per-worker activity tracking (lock-free)
  std::mutex worker_scaling_mutex_;   // Protects resizing of worker_active_ during scale operations

  // Task queues - one per priority level for faster scheduling
  std::array<std::queue<Task>, 4> priority_queues_;  // One per TaskPriority
  mutable std::mutex queue_mutex_;
  std::condition_variable queue_cv_;

  // Context affinity map - maps context_id to preferred worker
  std::unordered_map<std::string, size_t> context_affinity_;
  std::mutex affinity_mutex_;

  // Shutdown flag
  std::atomic<bool> shutdown_{false};

  // Metrics
  TaskMetrics metrics_;

  // Scaling parameters
  size_t min_workers_;
  size_t max_workers_;
  std::mutex scaling_mutex_;

  // Singleton instance
  static std::unique_ptr<ThreadPool> instance_;
  static std::mutex instance_mutex_;
};

// Lightweight spin-lock for very short critical sections
class SpinLock {
public:
  void lock() {
    while (flag_.test_and_set(std::memory_order_acquire)) {
      // Spin with pause instruction for better performance
      #if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_pause();
      #elif defined(__aarch64__)
        asm volatile("yield");
      #endif
    }
  }

  void unlock() {
    flag_.clear(std::memory_order_release);
  }

  bool try_lock() {
    return !flag_.test_and_set(std::memory_order_acquire);
  }

private:
  std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

// Reader-writer lock for contexts map (many readers, few writers)
class RWLock {
public:
  void lock_shared() {
    std::unique_lock<std::mutex> lock(mutex_);
    reader_cv_.wait(lock, [this] { return !writer_active_ && waiting_writers_ == 0; });
    ++readers_;
  }

  void unlock_shared() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (--readers_ == 0 && waiting_writers_ > 0) {
      writer_cv_.notify_one();
    }
  }

  void lock() {
    std::unique_lock<std::mutex> lock(mutex_);
    ++waiting_writers_;
    writer_cv_.wait(lock, [this] { return !writer_active_ && readers_ == 0; });
    --waiting_writers_;
    writer_active_ = true;
  }

  void unlock() {
    std::unique_lock<std::mutex> lock(mutex_);
    writer_active_ = false;
    if (waiting_writers_ > 0) {
      writer_cv_.notify_one();
    } else {
      reader_cv_.notify_all();
    }
  }

private:
  std::mutex mutex_;
  std::condition_variable reader_cv_;
  std::condition_variable writer_cv_;
  int readers_ = 0;
  int waiting_writers_ = 0;
  bool writer_active_ = false;
};

// RAII guards for RWLock
class ReadGuard {
public:
  explicit ReadGuard(RWLock& lock) : lock_(lock) { lock_.lock_shared(); }
  ~ReadGuard() { lock_.unlock_shared(); }
  ReadGuard(const ReadGuard&) = delete;
  ReadGuard& operator=(const ReadGuard&) = delete;
private:
  RWLock& lock_;
};

class WriteGuard {
public:
  explicit WriteGuard(RWLock& lock) : lock_(lock) { lock_.lock(); }
  ~WriteGuard() { lock_.unlock(); }
  WriteGuard(const WriteGuard&) = delete;
  WriteGuard& operator=(const WriteGuard&) = delete;
private:
  RWLock& lock_;
};

// ============================================================
// Template Implementations
// ============================================================

template<typename F, typename... Args>
auto ThreadPool::Submit(TaskPriority priority, F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type> {
  using return_type = typename std::invoke_result<F, Args...>::type;

  auto task = std::make_shared<std::packaged_task<return_type()>>(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...)
  );

  std::future<return_type> result = task->get_future();

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (shutdown_.load(std::memory_order_acquire)) {
      // Cannot throw with exceptions disabled - just log and return invalid future
      // The task won't be executed, and the future will never be ready
      return result;
    }

    Task t;
    t.func = [task]() { (*task)(); };
    t.priority = priority;
    t.submitted = std::chrono::steady_clock::now();

    priority_queues_[static_cast<int>(priority)].push(std::move(t));
    metrics_.tasks_submitted.fetch_add(1, std::memory_order_relaxed);
    metrics_.queue_depth.fetch_add(1, std::memory_order_relaxed);
  }

  queue_cv_.notify_one();
  return result;
}

template<typename F, typename... Args>
auto ThreadPool::SubmitForContext(const std::string& context_id, TaskPriority priority, F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type> {
  using return_type = typename std::invoke_result<F, Args...>::type;

  auto task = std::make_shared<std::packaged_task<return_type()>>(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...)
  );

  std::future<return_type> result = task->get_future();

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (shutdown_.load(std::memory_order_acquire)) {
      // Cannot throw with exceptions disabled - just return invalid future
      return result;
    }

    Task t;
    t.func = [task]() { (*task)(); };
    t.priority = priority;
    t.submitted = std::chrono::steady_clock::now();
    t.context_id = context_id;

    priority_queues_[static_cast<int>(priority)].push(std::move(t));
    metrics_.tasks_submitted.fetch_add(1, std::memory_order_relaxed);
    metrics_.queue_depth.fetch_add(1, std::memory_order_relaxed);
  }

  queue_cv_.notify_one();
  return result;
}

}  // namespace owl
