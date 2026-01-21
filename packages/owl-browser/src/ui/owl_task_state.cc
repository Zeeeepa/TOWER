#include "owl_task_state.h"
#include "logger.h"
#include <algorithm>

OwlTaskState* OwlTaskState::instance_ = nullptr;

OwlTaskState* OwlTaskState::GetInstance() {
  if (!instance_) {
    instance_ = new OwlTaskState();
  }
  return instance_;
}

void OwlTaskState::SetTasks(const std::vector<std::string>& task_descriptions) {
  std::lock_guard<std::mutex> lock(mutex_);

  tasks_.clear();
  for (const auto& desc : task_descriptions) {
    tasks_.emplace_back(desc);
  }

  current_task_index_ = 0;
  LOG_DEBUG("TaskState", "Set " + std::to_string(tasks_.size()) + " tasks");
}

void OwlTaskState::UpdateTaskStatus(size_t index, TaskStatus status, const std::string& result) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (index >= tasks_.size()) {
    LOG_ERROR("TaskState", "Invalid task index: " + std::to_string(index));
    return;
  }

  // Get old status for logging
  std::string old_status_str;
  switch (tasks_[index].status) {
    case TaskStatus::PENDING: old_status_str = "PENDING"; break;
    case TaskStatus::ACTIVE: old_status_str = "ACTIVE"; break;
    case TaskStatus::COMPLETED: old_status_str = "COMPLETED"; break;
    case TaskStatus::FAILED: old_status_str = "FAILED"; break;
  }

  tasks_[index].status = status;
  tasks_[index].result = result;

  std::string new_status_str;
  switch (status) {
    case TaskStatus::PENDING: new_status_str = "PENDING"; break;
    case TaskStatus::ACTIVE: new_status_str = "ACTIVE"; break;
    case TaskStatus::COMPLETED: new_status_str = "COMPLETED"; break;
    case TaskStatus::FAILED: new_status_str = "FAILED"; break;
  }

  LOG_DEBUG("TaskState", "Task " + std::to_string(index) + " '" + tasks_[index].description +
           "' changed from " + old_status_str + " -> " + new_status_str +
           " | Result: " + result);

  // Log all task states after this update
  LOG_DEBUG("TaskState", "=== ALL TASK STATES AFTER UPDATE ===");
  for (size_t i = 0; i < tasks_.size(); i++) {
    std::string state;
    switch (tasks_[i].status) {
      case TaskStatus::PENDING: state = "PENDING"; break;
      case TaskStatus::ACTIVE: state = "ACTIVE"; break;
      case TaskStatus::COMPLETED: state = "COMPLETED"; break;
      case TaskStatus::FAILED: state = "FAILED"; break;
    }
    LOG_DEBUG("TaskState", "  [" + std::to_string(i) + "] " + state + ": " + tasks_[i].description);
  }
  LOG_DEBUG("TaskState", "====================================");
}

void OwlTaskState::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  tasks_.clear();
  current_task_index_ = 0;
  LOG_DEBUG("TaskState", "Cleared all tasks");
}

std::vector<TaskInfo> OwlTaskState::GetTasks() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return tasks_;
}

void OwlTaskState::AdvanceToNextTask() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (current_task_index_ < tasks_.size()) {
    current_task_index_++;
    LOG_DEBUG("TaskState", "Advanced to task " + std::to_string(current_task_index_));
  }
}
