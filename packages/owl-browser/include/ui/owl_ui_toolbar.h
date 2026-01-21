#ifndef OWL_UI_TOOLBAR_H_
#define OWL_UI_TOOLBAR_H_

#include "include/cef_browser.h"
#include <string>
#include <functional>

#if defined(OS_MACOS)
#ifdef __OBJC__
@class NSView;
@class NSTextField;
@class NSButton;
@class NSImageView;
@class TLDAutocompleteHelper;
#else
class NSView;
class NSTextField;
class NSButton;
class NSImageView;
class TLDAutocompleteHelper;
#endif
#endif

// Callback types for toolbar actions
typedef std::function<void()> ToolbarCallback;
typedef std::function<void(const std::string&)> NavigateCallback;

// Native Cocoa toolbar for browser UI
class OwlUIToolbar {
public:
  OwlUIToolbar();
  ~OwlUIToolbar();

  // Create and return the native toolbar view (to be added to window)
  void* CreateToolbarView(int width, int height);

  // Set callbacks for toolbar actions
  void SetBackCallback(ToolbarCallback callback);
  void SetForwardCallback(ToolbarCallback callback);
  void SetReloadCallback(ToolbarCallback callback);
  void SetHomeCallback(ToolbarCallback callback);
  void SetNavigateCallback(NavigateCallback callback);
  void SetAgentToggleCallback(ToolbarCallback callback);
  void SetStopLoadingCallback(ToolbarCallback callback);
  void SetNewTabCallback(ToolbarCallback callback);
  void SetProxyToggleCallback(ToolbarCallback callback);

  // Execute callbacks (called by Objective-C delegate)
  void ExecuteBackCallback() { if (back_callback_) back_callback_(); }
  void ExecuteForwardCallback() { if (forward_callback_) forward_callback_(); }
  void ExecuteReloadCallback() { if (reload_callback_) reload_callback_(); }
  void ExecuteHomeCallback() { if (home_callback_) home_callback_(); }
  void ExecuteNavigateCallback(const std::string& url) { if (navigate_callback_) navigate_callback_(url); }
  void ExecuteAgentToggleCallback() { if (agent_toggle_callback_) agent_toggle_callback_(); }
  void ExecuteStopLoadingCallback() { if (stop_loading_callback_) stop_loading_callback_(); }
  void ExecuteNewTabCallback() { if (new_tab_callback_) new_tab_callback_(); }
  void ExecuteProxyToggleCallback() { if (proxy_toggle_callback_) proxy_toggle_callback_(); }

  // Update toolbar state
  void UpdateNavigationButtons(bool can_go_back, bool can_go_forward);
  void UpdateAddressBar(const std::string& url);
  void SetAgentModeActive(bool active);
  void SetLoadingState(bool is_loading);  // Show/hide loading indicator
  void SetProxyConnected(bool connected);  // Update proxy button state

  // Get toolbar view
  void* GetView() const { return toolbar_view_; }
  int GetHeight() const { return 56; }  // Toolbar height in pixels

private:
  // Native UI callbacks
  ToolbarCallback back_callback_;
  ToolbarCallback forward_callback_;
  ToolbarCallback reload_callback_;
  ToolbarCallback home_callback_;
  NavigateCallback navigate_callback_;
  ToolbarCallback agent_toggle_callback_;
  ToolbarCallback stop_loading_callback_;
  ToolbarCallback new_tab_callback_;
  ToolbarCallback proxy_toggle_callback_;

#if defined(OS_MACOS)
  NSView* toolbar_view_;
  NSButton* back_button_;
  NSButton* forward_button_;
  NSButton* reload_button_;
  NSButton* stop_button_;  // New: Stop loading button
  NSButton* home_button_;
  NSTextField* address_bar_;
  NSButton* go_button_;  // New: Go button for address bar
  NSButton* agent_button_;
  NSButton* proxy_button_;  // Proxy toggle button
  NSImageView* loading_indicator_;  // New: Loading spinner
  TLDAutocompleteHelper* tld_autocomplete_helper_;  // TLD autocomplete for address bar
  bool agent_mode_active_;
  bool is_loading_;
  bool proxy_connected_;
#elif defined(OS_LINUX)
  void* toolbar_view_;  // GtkWidget* (horizontal box)
  void* back_button_;   // GtkWidget* (GtkButton)
  void* forward_button_;  // GtkWidget* (GtkButton)
  void* reload_button_;   // GtkWidget* (GtkButton)
  void* stop_button_;     // GtkWidget* (GtkButton)
  void* home_button_;     // GtkWidget* (GtkButton)
  void* address_bar_;     // GtkWidget* (GtkEntry)
  void* go_button_;       // GtkWidget* (GtkButton)
  void* agent_button_;    // GtkWidget* (GtkButton)
  void* proxy_button_;    // GtkWidget* (GtkButton) - Proxy toggle
  void* loading_indicator_;  // GtkWidget* (GtkSpinner)
  void* tld_autocomplete_helper_;  // Reserved for future TLD autocomplete
  bool agent_mode_active_;
  bool is_loading_;
  bool proxy_connected_;
#endif
};

#endif  // OWL_UI_TOOLBAR_H_
