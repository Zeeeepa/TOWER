#ifndef OWL_AGENT_CONTROLLER_H_
#define OWL_AGENT_CONTROLLER_H_

#include "include/cef_browser.h"
#include "include/cef_frame.h"
#include <string>
#include <functional>
#include <memory>

// Agent mode controller for UI version
// Coordinates LLM-powered browser automation with user feedback
class OwlAgentController {
public:
  enum class AgentState {
    IDLE,
    PLANNING,
    EXECUTING,
    WAITING_FOR_USER,
    COMPLETED,
    ERROR
  };

  struct AgentStatus {
    AgentState state;
    std::string message;
    std::string current_action;
    int action_index;
    int total_actions;
    float progress;  // 0.0 to 1.0
  };

  using StatusCallback = std::function<void(const AgentStatus&)>;

  static OwlAgentController* GetInstance();

  // Execute agent command with user feedback
  void ExecuteCommand(CefRefPtr<CefBrowser> browser, const std::string& prompt, StatusCallback callback);

  // Control execution
  void PauseExecution();
  void ResumeExecution();
  void StopExecution();

  // Get current status
  AgentStatus GetStatus() const;
  bool IsExecuting() const { return executing_; }

  // Update status and notify callback
  void UpdateStatus(AgentState state, const std::string& message, int action_idx = 0, int total = 0);

private:
  OwlAgentController();
  ~OwlAgentController();

  static OwlAgentController* instance_;

  bool executing_;
  bool paused_;
  AgentStatus current_status_;
  StatusCallback status_callback_;
};

#endif  // OWL_AGENT_CONTROLLER_H_
