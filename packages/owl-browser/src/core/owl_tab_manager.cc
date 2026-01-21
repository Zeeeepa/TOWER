#include "owl_tab_manager.h"
#include "logger.h"
#include <sstream>
#include <chrono>
#include <algorithm>

OwlTabManager* OwlTabManager::GetInstance() {
  static OwlTabManager instance;
  return &instance;
}

OwlTabManager::OwlTabManager() {
  LOG_DEBUG("TabManager", "Tab manager initialized");
}

OwlTabManager::~OwlTabManager() {}

void OwlTabManager::SetPopupPolicy(const std::string& context_id,
                                    PopupPolicy policy) {
  std::lock_guard<std::mutex> lock(mutex_);
  popup_policies_[context_id] = policy;

  std::string policy_str;
  switch (policy) {
    case PopupPolicy::ALLOW:
      policy_str = "allow";
      break;
    case PopupPolicy::BLOCK:
      policy_str = "block";
      break;
    case PopupPolicy::OPEN_IN_NEW_TAB:
      policy_str = "new_tab";
      break;
    case PopupPolicy::OPEN_IN_BACKGROUND:
      policy_str = "background";
      break;
  }

  LOG_DEBUG("TabManager",
           "Set popup policy for " + context_id + ": " + policy_str);
}

PopupPolicy OwlTabManager::GetPopupPolicy(const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = popup_policies_.find(context_id);
  if (it != popup_policies_.end()) {
    return it->second;
  }
  return PopupPolicy::OPEN_IN_NEW_TAB;  // Default
}

std::string OwlTabManager::GenerateTabId() {
  return "tab_" + std::to_string(++tab_counter_);
}

void OwlTabManager::RegisterTab(const TabInfo& info) {
  std::lock_guard<std::mutex> lock(mutex_);

  tabs_[info.tab_id] = info;
  context_tabs_[info.context_id].push_back(info.tab_id);

  // Set as active if it's the first tab or if it's not a background tab
  auto active_it = active_tabs_.find(info.context_id);
  if (active_it == active_tabs_.end() || !info.is_popup) {
    active_tabs_[info.context_id] = info.tab_id;
  }

  LOG_DEBUG("TabManager", "Registered tab: " + info.tab_id + " context=" +
                             info.context_id + " url=" + info.url);
}

void OwlTabManager::UnregisterTab(const std::string& tab_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = tabs_.find(tab_id);
  if (it == tabs_.end()) {
    return;
  }

  std::string context_id = it->second.context_id;

  // Remove from context tabs
  auto ctx_it = context_tabs_.find(context_id);
  if (ctx_it != context_tabs_.end()) {
    auto& tabs = ctx_it->second;
    tabs.erase(std::remove(tabs.begin(), tabs.end(), tab_id), tabs.end());

    // If this was active tab, switch to another
    auto active_it = active_tabs_.find(context_id);
    if (active_it != active_tabs_.end() && active_it->second == tab_id) {
      if (!tabs.empty()) {
        active_tabs_[context_id] = tabs.back();
      } else {
        active_tabs_.erase(context_id);
      }
    }
  }

  tabs_.erase(it);

  LOG_DEBUG("TabManager", "Unregistered tab: " + tab_id);
}

void OwlTabManager::UpdateTab(const std::string& tab_id, const std::string& url,
                               const std::string& title) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = tabs_.find(tab_id);
  if (it != tabs_.end()) {
    it->second.url = url;
    it->second.title = title;
  }
}

TabInfo OwlTabManager::GetTab(const std::string& tab_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = tabs_.find(tab_id);
  if (it != tabs_.end()) {
    return it->second;
  }
  return TabInfo();
}

std::vector<TabInfo> OwlTabManager::GetTabs(
    const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<TabInfo> result;
  auto ctx_it = context_tabs_.find(context_id);
  if (ctx_it != context_tabs_.end()) {
    for (const auto& tab_id : ctx_it->second) {
      auto tab_it = tabs_.find(tab_id);
      if (tab_it != tabs_.end()) {
        result.push_back(tab_it->second);
      }
    }
  }
  return result;
}

std::string OwlTabManager::GetActiveTab(const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = active_tabs_.find(context_id);
  if (it != active_tabs_.end()) {
    return it->second;
  }
  return "";
}

int OwlTabManager::GetTabCount(const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = context_tabs_.find(context_id);
  if (it != context_tabs_.end()) {
    return static_cast<int>(it->second.size());
  }
  return 0;
}

void OwlTabManager::SetActiveTab(const std::string& context_id,
                                  const std::string& tab_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Verify tab exists and belongs to context
  auto tab_it = tabs_.find(tab_id);
  if (tab_it != tabs_.end() && tab_it->second.context_id == context_id) {
    // Update is_active flag
    auto ctx_it = context_tabs_.find(context_id);
    if (ctx_it != context_tabs_.end()) {
      for (const auto& tid : ctx_it->second) {
        auto t_it = tabs_.find(tid);
        if (t_it != tabs_.end()) {
          t_it->second.is_active = (tid == tab_id);
        }
      }
    }

    active_tabs_[context_id] = tab_id;
    LOG_DEBUG("TabManager",
             "Set active tab for " + context_id + ": " + tab_id);
  }
}

void OwlTabManager::RecordPopupAttempt(const std::string& context_id,
                                        const std::string& source_tab_id,
                                        const std::string& url) {
  std::lock_guard<std::mutex> lock(mutex_);

  blocked_popups_[context_id].push_back(url);

  LOG_DEBUG("TabManager", "Recorded popup attempt from " + source_tab_id +
                             " url=" + url);
}

std::vector<std::string> OwlTabManager::GetBlockedPopups(
    const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = blocked_popups_.find(context_id);
  if (it != blocked_popups_.end()) {
    return it->second;
  }
  return {};
}

void OwlTabManager::ClearBlockedPopups(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  blocked_popups_.erase(context_id);
}

std::string OwlTabManager::GetTabsJSON(const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::stringstream ss;
  ss << "[";

  auto ctx_it = context_tabs_.find(context_id);
  if (ctx_it != context_tabs_.end()) {
    bool first = true;
    for (const auto& tab_id : ctx_it->second) {
      auto tab_it = tabs_.find(tab_id);
      if (tab_it != tabs_.end()) {
        if (!first) ss << ",";
        first = false;

        const auto& tab = tab_it->second;
        ss << "{\"id\":\"" << tab.tab_id << "\""
           << ",\"url\":\"" << tab.url << "\""
           << ",\"title\":\"" << tab.title << "\""
           << ",\"isMain\":" << (tab.is_main ? "true" : "false")
           << ",\"isPopup\":" << (tab.is_popup ? "true" : "false")
           << ",\"isActive\":" << (tab.is_active ? "true" : "false") << "}";
      }
    }
  }

  ss << "]";
  return ss.str();
}

void OwlTabManager::ClearContext(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto ctx_it = context_tabs_.find(context_id);
  if (ctx_it != context_tabs_.end()) {
    for (const auto& tab_id : ctx_it->second) {
      tabs_.erase(tab_id);
    }
    context_tabs_.erase(ctx_it);
  }

  active_tabs_.erase(context_id);
  popup_policies_.erase(context_id);
  blocked_popups_.erase(context_id);

  LOG_DEBUG("TabManager", "Cleared context: " + context_id);
}
