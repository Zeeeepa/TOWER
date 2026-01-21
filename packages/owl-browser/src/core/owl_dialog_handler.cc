#include "owl_dialog_handler.h"
#include "logger.h"
#include <sstream>
#include <chrono>

OwlDialogManager* OwlDialogManager::GetInstance() {
  static OwlDialogManager instance;
  return &instance;
}

OwlDialogManager::OwlDialogManager() {
  LOG_DEBUG("DialogManager", "Dialog manager initialized");
}

OwlDialogManager::~OwlDialogManager() {}

void OwlDialogManager::SetDialogConfig(const std::string& context_id,
                                        const DialogConfig& config) {
  std::lock_guard<std::mutex> lock(mutex_);
  configs_[context_id] = config;
  LOG_DEBUG("DialogManager",
           "Set dialog config for context: " + context_id);
}

DialogConfig OwlDialogManager::GetDialogConfig(
    const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = configs_.find(context_id);
  if (it != configs_.end()) {
    return it->second;
  }
  return DialogConfig();  // Default config
}

void OwlDialogManager::SetAlertAction(const std::string& context_id,
                                       DialogAction action) {
  std::lock_guard<std::mutex> lock(mutex_);
  configs_[context_id].auto_dismiss_alerts = (action == DialogAction::DISMISS);
}

void OwlDialogManager::SetConfirmAction(const std::string& context_id,
                                         DialogAction action) {
  std::lock_guard<std::mutex> lock(mutex_);
  configs_[context_id].auto_accept_confirms = (action == DialogAction::ACCEPT);
}

void OwlDialogManager::SetPromptAction(const std::string& context_id,
                                        DialogAction action,
                                        const std::string& text) {
  std::lock_guard<std::mutex> lock(mutex_);
  configs_[context_id].default_action = action;
  configs_[context_id].default_prompt_text = text;
}

void OwlDialogManager::SetBeforeUnloadAction(const std::string& context_id,
                                              DialogAction action) {
  std::lock_guard<std::mutex> lock(mutex_);
  configs_[context_id].auto_accept_beforeunload =
      (action == DialogAction::ACCEPT);
}

std::string OwlDialogManager::GenerateDialogId() {
  return "dialog_" + std::to_string(++dialog_counter_);
}

std::string OwlDialogManager::RecordDialog(const std::string& context_id,
                                            DialogType type,
                                            const std::string& message,
                                            const std::string& default_value,
                                            const std::string& origin_url) {
  std::lock_guard<std::mutex> lock(mutex_);

  PendingDialog dialog;
  dialog.dialog_id = GenerateDialogId();
  dialog.context_id = context_id;
  dialog.type = type;
  dialog.message = message;
  dialog.default_value = default_value;
  dialog.origin_url = origin_url;
  dialog.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
  dialog.handled = false;
  dialog.accepted = false;

  dialogs_[dialog.dialog_id] = dialog;
  context_dialogs_[context_id].push_back(dialog.dialog_id);

  std::string type_str;
  switch (type) {
    case DialogType::ALERT:
      type_str = "alert";
      break;
    case DialogType::CONFIRM:
      type_str = "confirm";
      break;
    case DialogType::PROMPT:
      type_str = "prompt";
      break;
    case DialogType::BEFOREUNLOAD:
      type_str = "beforeunload";
      break;
  }

  LOG_DEBUG("DialogManager", "Recorded " + type_str + " dialog: " +
                                dialog.dialog_id + " message=\"" + message +
                                "\"");

  dialog_cv_.notify_all();

  return dialog.dialog_id;
}

DialogAction OwlDialogManager::GetAction(const std::string& context_id,
                                          DialogType type,
                                          std::string* prompt_text) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = configs_.find(context_id);
  if (it == configs_.end()) {
    // Default behavior
    if (type == DialogType::ALERT) {
      return DialogAction::ACCEPT;
    }
    return DialogAction::DISMISS;
  }

  const auto& config = it->second;

  switch (type) {
    case DialogType::ALERT:
      return config.auto_dismiss_alerts ? DialogAction::DISMISS
                                        : DialogAction::ACCEPT;

    case DialogType::CONFIRM:
      return config.auto_accept_confirms ? DialogAction::ACCEPT
                                         : DialogAction::DISMISS;

    case DialogType::PROMPT:
      if (prompt_text) {
        *prompt_text = config.default_prompt_text;
      }
      return config.default_action;

    case DialogType::BEFOREUNLOAD:
      return config.auto_accept_beforeunload ? DialogAction::ACCEPT
                                             : DialogAction::DISMISS;
  }

  return DialogAction::DISMISS;
}

bool OwlDialogManager::HandleDialog(const std::string& dialog_id, bool accept,
                                     const std::string& response_text) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = dialogs_.find(dialog_id);
  if (it == dialogs_.end()) {
    LOG_WARN("DialogManager", "Dialog not found: " + dialog_id);
    return false;
  }

  it->second.handled = true;
  it->second.accepted = accept;
  it->second.response_text = response_text;

  LOG_DEBUG("DialogManager", "Handled dialog " + dialog_id +
                                " accept=" + (accept ? "true" : "false"));

  return true;
}

PendingDialog OwlDialogManager::GetPendingDialog(
    const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto ctx_it = context_dialogs_.find(context_id);
  if (ctx_it != context_dialogs_.end()) {
    // Return most recent unhandled dialog
    for (auto rit = ctx_it->second.rbegin(); rit != ctx_it->second.rend();
         ++rit) {
      auto dl_it = dialogs_.find(*rit);
      if (dl_it != dialogs_.end() && !dl_it->second.handled) {
        return dl_it->second;
      }
    }
  }

  return PendingDialog();
}

std::vector<PendingDialog> OwlDialogManager::GetAllDialogs(
    const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<PendingDialog> result;
  auto ctx_it = context_dialogs_.find(context_id);
  if (ctx_it != context_dialogs_.end()) {
    for (const auto& dl_id : ctx_it->second) {
      auto dl_it = dialogs_.find(dl_id);
      if (dl_it != dialogs_.end()) {
        result.push_back(dl_it->second);
      }
    }
  }
  return result;
}

bool OwlDialogManager::HasPendingDialog(const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto ctx_it = context_dialogs_.find(context_id);
  if (ctx_it != context_dialogs_.end()) {
    for (const auto& dl_id : ctx_it->second) {
      auto dl_it = dialogs_.find(dl_id);
      if (dl_it != dialogs_.end() && !dl_it->second.handled) {
        return true;
      }
    }
  }
  return false;
}

bool OwlDialogManager::WaitForDialog(const std::string& context_id,
                                      int timeout_ms) {
  std::unique_lock<std::mutex> lock(mutex_);

  // Check if already has pending dialog
  auto ctx_it = context_dialogs_.find(context_id);
  if (ctx_it != context_dialogs_.end()) {
    for (const auto& dl_id : ctx_it->second) {
      auto dl_it = dialogs_.find(dl_id);
      if (dl_it != dialogs_.end() && !dl_it->second.handled) {
        return true;  // Already have pending dialog
      }
    }
  }

  // Wait for new dialog
  auto timeout = std::chrono::milliseconds(timeout_ms);
  return dialog_cv_.wait_for(lock, timeout, [this, &context_id]() {
    auto ctx_it = context_dialogs_.find(context_id);
    if (ctx_it != context_dialogs_.end()) {
      for (const auto& dl_id : ctx_it->second) {
        auto dl_it = dialogs_.find(dl_id);
        if (dl_it != dialogs_.end() && !dl_it->second.handled) {
          return true;
        }
      }
    }
    return false;
  });
}

void OwlDialogManager::MarkDialogHandled(const std::string& dialog_id,
                                          bool accepted,
                                          const std::string& response) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = dialogs_.find(dialog_id);
  if (it != dialogs_.end()) {
    it->second.handled = true;
    it->second.accepted = accepted;
    it->second.response_text = response;
  }
}

void OwlDialogManager::ClearDialogs(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto ctx_it = context_dialogs_.find(context_id);
  if (ctx_it != context_dialogs_.end()) {
    for (const auto& dl_id : ctx_it->second) {
      dialogs_.erase(dl_id);
    }
    context_dialogs_.erase(ctx_it);
  }

  LOG_DEBUG("DialogManager", "Cleared dialogs for context: " + context_id);
}

std::string OwlDialogManager::GetDialogsJSON(
    const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::stringstream ss;
  ss << "[";

  auto ctx_it = context_dialogs_.find(context_id);
  if (ctx_it != context_dialogs_.end()) {
    bool first = true;
    for (const auto& dl_id : ctx_it->second) {
      auto dl_it = dialogs_.find(dl_id);
      if (dl_it != dialogs_.end()) {
        if (!first) ss << ",";
        first = false;

        const auto& dl = dl_it->second;
        std::string type_str;
        switch (dl.type) {
          case DialogType::ALERT:
            type_str = "alert";
            break;
          case DialogType::CONFIRM:
            type_str = "confirm";
            break;
          case DialogType::PROMPT:
            type_str = "prompt";
            break;
          case DialogType::BEFOREUNLOAD:
            type_str = "beforeunload";
            break;
        }

        ss << "{\"id\":\"" << dl.dialog_id << "\""
           << ",\"type\":\"" << type_str << "\""
           << ",\"message\":\"" << dl.message << "\""
           << ",\"defaultValue\":\"" << dl.default_value << "\""
           << ",\"handled\":" << (dl.handled ? "true" : "false")
           << ",\"accepted\":" << (dl.accepted ? "true" : "false")
           << ",\"response\":\"" << dl.response_text << "\"}";
      }
    }
  }

  ss << "]";
  return ss.str();
}
