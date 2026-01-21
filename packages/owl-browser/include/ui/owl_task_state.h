#ifndef OWL_TASK_STATE_H_
#define OWL_TASK_STATE_H_

#include <string>
#include <vector>
#include <mutex>

// Task status
enum class TaskStatus {
  PENDING,
  ACTIVE,
  COMPLETED,
  FAILED
};

// Individual task info
struct TaskInfo {
  std::string id;
  std::string description;
  TaskStatus status;
  std::string result;

  TaskInfo(const std::string& desc)
    : description(desc), status(TaskStatus::PENDING) {}
};

// Singleton task state manager - persists across page navigations
class OwlTaskState {
 public:
  static OwlTaskState* GetInstance();

  // Task management
  void SetTasks(const std::vector<std::string>& task_descriptions);
  void UpdateTaskStatus(size_t index, TaskStatus status, const std::string& result = "");
  void Clear();

  // Getters
  std::vector<TaskInfo> GetTasks() const;
  size_t GetCurrentTaskIndex() const { return current_task_index_; }
  bool HasActiveTasks() const { return !tasks_.empty(); }

  // Move to next task
  void AdvanceToNextTask();

 private:
  OwlTaskState() : current_task_index_(0) {}
  static OwlTaskState* instance_;

  std::vector<TaskInfo> tasks_;
  size_t current_task_index_;
  mutable std::mutex mutex_;
};

#endif  // OWL_TASK_STATE_H_
