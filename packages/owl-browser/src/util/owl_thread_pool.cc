#include "owl_thread_pool.h"
#include <algorithm>
#include <iostream>

namespace owl {

// Static members
std::unique_ptr<ThreadPool> ThreadPool::instance_;
std::mutex ThreadPool::instance_mutex_;

ThreadPool::ThreadPool(size_t num_threads)
    : min_workers_(2),
      max_workers_(std::thread::hardware_concurrency() * 4) {

  // Default: 2x hardware threads for I/O-bound browser operations
  if (num_threads == 0) {
    num_threads = std::max<size_t>(4, std::thread::hardware_concurrency() * 2);
  }

  // Clamp to reasonable limits
  num_threads = std::clamp(num_threads, min_workers_, max_workers_);

  workers_.reserve(num_threads);
  worker_active_.reserve(num_threads);

  // Initialize atomic flags for each worker
  for (size_t i = 0; i < num_threads; ++i) {
    worker_active_.push_back(std::make_unique<std::atomic<bool>>(false));
  }

  for (size_t i = 0; i < num_threads; ++i) {
    workers_.emplace_back(&ThreadPool::WorkerLoop, this, i);
  }

  metrics_.idle_workers.store(static_cast<uint32_t>(num_threads), std::memory_order_relaxed);
}

ThreadPool::~ThreadPool() {
  Shutdown();
}

void ThreadPool::Shutdown() {
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (shutdown_.load(std::memory_order_acquire)) {
      return;  // Already shutdown
    }
    shutdown_.store(true, std::memory_order_release);
  }

  queue_cv_.notify_all();

  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }

  workers_.clear();
}

size_t ThreadPool::GetQueueSize() const {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  size_t total = 0;
  for (const auto& q : priority_queues_) {
    total += q.size();
  }
  return total;
}

void ThreadPool::ScaleWorkers(size_t target) {
  std::lock_guard<std::mutex> lock(scaling_mutex_);

  target = std::clamp(target, min_workers_, max_workers_);

  if (target == workers_.size()) {
    return;
  }

  if (target > workers_.size()) {
    // Add workers
    size_t current = workers_.size();

    // Add new atomic flags under scaling lock
    {
      std::lock_guard<std::mutex> scale_lock(worker_scaling_mutex_);
      for (size_t i = current; i < target; ++i) {
        worker_active_.push_back(std::make_unique<std::atomic<bool>>(false));
      }
    }

    for (size_t i = current; i < target; ++i) {
      workers_.emplace_back(&ThreadPool::WorkerLoop, this, i);
    }

    metrics_.idle_workers.fetch_add(static_cast<uint32_t>(target - current), std::memory_order_relaxed);
  }
  // Note: Scaling down requires more complex logic to safely stop workers
  // For now, we only support scaling up
}

void ThreadPool::WorkerLoop(size_t worker_id) {
  while (true) {
    Task task;
    bool got_task = false;

    {
      std::unique_lock<std::mutex> lock(queue_mutex_);

      // Wait for task or shutdown
      queue_cv_.wait(lock, [this] {
        if (shutdown_.load(std::memory_order_acquire)) {
          return true;
        }
        for (const auto& q : priority_queues_) {
          if (!q.empty()) {
            return true;
          }
        }
        return false;
      });

      if (shutdown_.load(std::memory_order_acquire)) {
        // Check if there are remaining tasks
        bool has_tasks = false;
        for (const auto& q : priority_queues_) {
          if (!q.empty()) {
            has_tasks = true;
            break;
          }
        }
        if (!has_tasks) {
          return;  // Exit worker
        }
      }

      // Get highest priority task
      for (int i = static_cast<int>(TaskPriority::CRITICAL); i >= 0; --i) {
        auto& q = priority_queues_[i];
        if (!q.empty()) {
          task = std::move(q.front());
          q.pop();
          got_task = true;
          metrics_.queue_depth.fetch_sub(1, std::memory_order_relaxed);
          break;
        }
      }
    }

    if (got_task) {
      // Track activity - lock-free using atomic
      if (worker_id < worker_active_.size()) {
        worker_active_[worker_id]->store(true, std::memory_order_release);
      }
      metrics_.active_workers.fetch_add(1, std::memory_order_relaxed);
      metrics_.idle_workers.fetch_sub(1, std::memory_order_relaxed);

      // Calculate wait time
      auto now = std::chrono::steady_clock::now();
      auto wait_us = std::chrono::duration_cast<std::chrono::microseconds>(
          now - task.submitted).count();
      metrics_.total_wait_time_us.fetch_add(wait_us, std::memory_order_relaxed);

      // Execute task - no try/catch since exceptions are disabled
      auto exec_start = std::chrono::steady_clock::now();
      task.func();
      metrics_.tasks_completed.fetch_add(1, std::memory_order_relaxed);

      auto exec_us = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - exec_start).count();
      metrics_.total_exec_time_us.fetch_add(exec_us, std::memory_order_relaxed);

      // Track activity - lock-free using atomic
      if (worker_id < worker_active_.size()) {
        worker_active_[worker_id]->store(false, std::memory_order_release);
      }
      metrics_.active_workers.fetch_sub(1, std::memory_order_relaxed);
      metrics_.idle_workers.fetch_add(1, std::memory_order_relaxed);

      // Update affinity for context-bound tasks
      if (!task.context_id.empty()) {
        std::lock_guard<std::mutex> aff_lock(affinity_mutex_);
        context_affinity_[task.context_id] = worker_id;
      }
    }
  }
}

bool ThreadPool::TryStealTask(Task& task, size_t thief_id) {
  std::lock_guard<std::mutex> lock(queue_mutex_);

  // Try to steal from any non-empty queue
  for (int i = static_cast<int>(TaskPriority::CRITICAL); i >= 0; --i) {
    auto& q = priority_queues_[i];
    if (!q.empty()) {
      task = std::move(q.front());
      q.pop();
      metrics_.queue_depth.fetch_sub(1, std::memory_order_relaxed);
      return true;
    }
  }

  return false;
}

// Singleton implementation
ThreadPool* ThreadPool::GetInstance() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  return instance_.get();
}

void ThreadPool::Initialize(size_t num_threads) {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (!instance_) {
    instance_ = std::make_unique<ThreadPool>(num_threads);
  }
}

void ThreadPool::Destroy() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (instance_) {
    instance_->Shutdown();
    instance_.reset();
  }
}

}  // namespace owl
