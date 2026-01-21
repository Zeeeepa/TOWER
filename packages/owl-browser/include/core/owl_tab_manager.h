#ifndef OWL_TAB_MANAGER_H_
#define OWL_TAB_MANAGER_H_

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>

// Tab information
struct TabInfo {
  std::string tab_id;
  std::string context_id;         // Parent context
  std::string url;
  std::string title;
  bool is_main;                   // Is main tab (original context)
  bool is_popup;                  // Was opened as popup
  std::string opener_tab_id;      // Tab that opened this one
  int64_t created_time;
  bool is_active;                 // Currently active tab in context
};

// Popup handling policy
enum class PopupPolicy {
  ALLOW,              // Allow popup to open normally
  BLOCK,              // Block all popups
  OPEN_IN_NEW_TAB,    // Convert popup to new tab in same context
  OPEN_IN_BACKGROUND  // Open popup but don't switch to it
};

// Tab manager singleton class
class OwlTabManager {
 public:
  static OwlTabManager* GetInstance();

  // Popup policy
  void SetPopupPolicy(const std::string& context_id, PopupPolicy policy);
  PopupPolicy GetPopupPolicy(const std::string& context_id) const;

  // Tab registration (called by browser)
  void RegisterTab(const TabInfo& info);
  void UnregisterTab(const std::string& tab_id);
  void UpdateTab(const std::string& tab_id, const std::string& url,
                 const std::string& title);

  // Tab queries
  TabInfo GetTab(const std::string& tab_id) const;
  std::vector<TabInfo> GetTabs(const std::string& context_id) const;
  std::string GetActiveTab(const std::string& context_id) const;
  int GetTabCount(const std::string& context_id) const;

  // Tab operations
  void SetActiveTab(const std::string& context_id, const std::string& tab_id);

  // Popup handling
  void RecordPopupAttempt(const std::string& context_id,
                          const std::string& source_tab_id,
                          const std::string& url);
  std::vector<std::string> GetBlockedPopups(const std::string& context_id) const;
  void ClearBlockedPopups(const std::string& context_id);

  // Get tabs as JSON
  std::string GetTabsJSON(const std::string& context_id) const;

  // Generate unique tab ID
  std::string GenerateTabId();

  // Clean up context
  void ClearContext(const std::string& context_id);

 private:
  OwlTabManager();
  ~OwlTabManager();

  mutable std::mutex mutex_;

  // Tab ID -> Tab Info
  std::map<std::string, TabInfo> tabs_;

  // Context ID -> list of tab IDs
  std::map<std::string, std::vector<std::string>> context_tabs_;

  // Context ID -> active tab ID
  std::map<std::string, std::string> active_tabs_;

  // Context ID -> popup policy
  std::map<std::string, PopupPolicy> popup_policies_;

  // Context ID -> blocked popup URLs
  std::map<std::string, std::vector<std::string>> blocked_popups_;

  std::atomic<uint64_t> tab_counter_{0};
};

#endif  // OWL_TAB_MANAGER_H_
