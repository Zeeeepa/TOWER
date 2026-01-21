#include "owl_agent_controller.h"
#include "owl_nla.h"
#include "owl_query_router.h"
#include "owl_ui_delegate.h"
#include "owl_ai_intelligence.h"
#include "owl_demographics.h"
#include "logger.h"
#include <thread>

OwlAgentController* OwlAgentController::instance_ = nullptr;

OwlAgentController* OwlAgentController::GetInstance() {
  if (!instance_) {
    instance_ = new OwlAgentController();
  }
  return instance_;
}

OwlAgentController::OwlAgentController()
    : executing_(false),
      paused_(false) {
  current_status_.state = AgentState::IDLE;
  current_status_.message = "Ready";
  current_status_.progress = 0.0f;
}

OwlAgentController::~OwlAgentController() {
}

void OwlAgentController::ExecuteCommand(CefRefPtr<CefBrowser> browser, const std::string& prompt, StatusCallback callback) {
  if (executing_) {
    LOG_WARN("Agent", "Agent already executing a command");
    return;
  }

  executing_ = true;
  status_callback_ = callback;

  // Start a polling thread to update UI in real-time as tasks change
  std::thread([this]() {
    while (executing_) {
      // Trigger status update to refresh UI with latest task states
      UpdateStatus(current_status_.state, current_status_.message,
                   current_status_.action_index, current_status_.total_actions);
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }).detach();

  // Execute in background thread to avoid blocking UI
  std::thread([this, browser, prompt]() {
    UpdateStatus(AgentState::PLANNING, "Analyzing your request...", 0, 0);

    // Use smart query router to determine the best tool(s)
    QueryResponse response = OwlQueryRouter::RouteAndExecute(browser, prompt);

    if (!response.success) {
      UpdateStatus(AgentState::ERROR, response.error, 0, 0);
      LOG_ERROR("Agent", "Agent execution error: " + response.error);
    } else {
      UpdateStatus(AgentState::COMPLETED, "Task completed successfully", 0, 0);
      LOG_DEBUG("Agent", "Agent execution completed");

      // If there's a text response (not just actions), show it to the user
      if (!response.response_text.empty() && !response.has_actions) {
        // Pure informational query - show response
        OwlUIDelegate* delegate = OwlUIDelegate::GetInstance();
        if (delegate) {
          delegate->ShowResponseArea(response.response_text);
          LOG_DEBUG("Agent", "Showing response area with text: " + response.response_text.substr(0, std::min((size_t)100, response.response_text.length())));
        }
      } else if (response.has_actions && response.should_summarize_result) {
        // Action-based query that should show page summary after completion
        // Wait a bit for page to load, then query for short summary
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));

        OwlUIDelegate* delegate = OwlUIDelegate::GetInstance();
        if (delegate && browser && browser->GetMainFrame()) {
          // Get location for context
          OwlDemographics* demo = OwlDemographics::GetInstance();
          std::string location_context = "";
          if (demo && demo->IsReady()) {
            GeoLocationInfo location = demo->GetGeoLocation();
            if (location.success && !location.city.empty()) {
              location_context = " (searching near " + location.city + ", " + location.region + ")";
            }
          }

          // Ask for short summary of results
          std::string query = "Give me a brief summary of the top 3-5 results shown on this page" + location_context + ". Keep it concise, just the key information.";
          std::string summary = OwlAIIntelligence::QueryPage(browser->GetMainFrame(), query);

          if (!summary.empty()) {
            delegate->ShowResponseArea(summary);
            LOG_DEBUG("Agent", "Showing short results summary after action completion");
          }
        }
      }
      // If only actions without summary flag, don't show response area
      // Just let the task list show the actions
    }

    executing_ = false;
    paused_ = false;

  }).detach();
}

void OwlAgentController::PauseExecution() {
  if (executing_ && !paused_) {
    paused_ = true;
    UpdateStatus(AgentState::WAITING_FOR_USER, "Execution paused", 0, 0);
  }
}

void OwlAgentController::ResumeExecution() {
  if (executing_ && paused_) {
    paused_ = false;
    UpdateStatus(AgentState::EXECUTING, "Resuming execution...", 0, 0);
  }
}

void OwlAgentController::StopExecution() {
  if (executing_) {
    executing_ = false;
    paused_ = false;
    UpdateStatus(AgentState::IDLE, "Execution stopped", 0, 0);
  }
}

OwlAgentController::AgentStatus OwlAgentController::GetStatus() const {
  return current_status_;
}

void OwlAgentController::UpdateStatus(AgentState state, const std::string& message, int action_idx, int total) {
  current_status_.state = state;
  current_status_.message = message;
  current_status_.action_index = action_idx;
  current_status_.total_actions = total;

  if (total > 0) {
    current_status_.progress = static_cast<float>(action_idx) / static_cast<float>(total);
  } else {
    current_status_.progress = 0.0f;
  }

  // Notify callback
  if (status_callback_) {
    status_callback_(current_status_);
  }
}
