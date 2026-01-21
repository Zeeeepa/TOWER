#ifndef OWL_DIALOG_HANDLER_H_
#define OWL_DIALOG_HANDLER_H_

#include <string>
#include <map>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>

// Dialog types
enum class DialogType {
  ALERT,
  CONFIRM,
  PROMPT,
  BEFOREUNLOAD
};

// Dialog action configuration
enum class DialogAction {
  ACCEPT,           // Accept/OK the dialog
  DISMISS,          // Dismiss/Cancel the dialog
  ACCEPT_WITH_TEXT, // Accept with specific text (for prompt)
  WAIT              // Wait for manual handling via API
};

// Pending dialog information
struct PendingDialog {
  std::string dialog_id;
  std::string context_id;
  DialogType type;
  std::string message;
  std::string default_value;      // For prompt dialogs
  std::string origin_url;
  int64_t timestamp;
  bool handled;
  bool accepted;
  std::string response_text;      // User response for prompt
};

// Dialog handler configuration per context
struct DialogConfig {
  DialogAction default_action;
  std::string default_prompt_text;
  bool auto_dismiss_alerts;
  bool auto_accept_confirms;
  bool auto_accept_beforeunload;

  DialogConfig()
      : default_action(DialogAction::DISMISS),
        auto_dismiss_alerts(true),
        auto_accept_confirms(false),
        auto_accept_beforeunload(true) {}
};

// Dialog manager singleton class
class OwlDialogManager {
 public:
  static OwlDialogManager* GetInstance();

  // Configure dialog handling for a context
  void SetDialogConfig(const std::string& context_id, const DialogConfig& config);
  DialogConfig GetDialogConfig(const std::string& context_id) const;

  // Set specific dialog actions
  void SetAlertAction(const std::string& context_id, DialogAction action);
  void SetConfirmAction(const std::string& context_id, DialogAction action);
  void SetPromptAction(const std::string& context_id, DialogAction action,
                       const std::string& text = "");
  void SetBeforeUnloadAction(const std::string& context_id, DialogAction action);

  // Record a pending dialog (called by CefJSDialogHandler)
  std::string RecordDialog(const std::string& context_id, DialogType type,
                           const std::string& message,
                           const std::string& default_value,
                           const std::string& origin_url);

  // Get action for a dialog type
  DialogAction GetAction(const std::string& context_id, DialogType type,
                         std::string* prompt_text = nullptr) const;

  // Handle pending dialog manually
  bool HandleDialog(const std::string& dialog_id, bool accept,
                    const std::string& response_text = "");

  // Get pending dialogs
  PendingDialog GetPendingDialog(const std::string& context_id) const;
  std::vector<PendingDialog> GetAllDialogs(const std::string& context_id) const;
  bool HasPendingDialog(const std::string& context_id) const;

  // Wait for dialog
  bool WaitForDialog(const std::string& context_id, int timeout_ms);

  // Mark dialog as handled
  void MarkDialogHandled(const std::string& dialog_id, bool accepted,
                         const std::string& response = "");

  // Clear dialogs
  void ClearDialogs(const std::string& context_id);

  // Get dialog info as JSON
  std::string GetDialogsJSON(const std::string& context_id) const;

  // Generate unique dialog ID
  std::string GenerateDialogId();

 private:
  OwlDialogManager();
  ~OwlDialogManager();

  mutable std::mutex mutex_;
  std::condition_variable dialog_cv_;

  // Context ID -> config
  std::map<std::string, DialogConfig> configs_;

  // Dialog ID -> dialog info
  std::map<std::string, PendingDialog> dialogs_;

  // Context ID -> list of dialog IDs (chronological)
  std::map<std::string, std::vector<std::string>> context_dialogs_;

  std::atomic<uint64_t> dialog_counter_{0};
};

#endif  // OWL_DIALOG_HANDLER_H_
