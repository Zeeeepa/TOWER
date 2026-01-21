#ifndef OWL_UI_DELEGATE_H_
#define OWL_UI_DELEGATE_H_

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "owl_ui_toolbar.h"

#if defined(OS_MACOS)
#ifdef __OBJC__
@class NSWindow;
@class OwlUIWindowDelegate;
@class NSView;
@class NSTextField;
@class NSButton;
@class NSPopUpButton;
#else
class NSWindow;
class OwlUIWindowDelegate;
class NSView;
class NSTextField;
class NSButton;
class NSPopUpButton;
#endif
#endif

// Forward declaration
class OwlUIBrowser;

// UI window delegate for handling native window events
class OwlUIDelegate {
public:
  static OwlUIDelegate* GetInstance();

  // Create native window with toolbar (returns content view handle for CEF)
  void* CreateWindowWithToolbar(OwlUIBrowser* browser_handler, int width = 1400, int height = 900);

  // Create native window WITHOUT toolbar (returns content view handle for CEF)
  void* CreateWindow(OwlUIBrowser* browser_handler, int width = 1400, int height = 900);

  // Focus/activate an existing window
  void FocusWindow();

  // Set the browser reference after browser is created
  void SetBrowser(CefRefPtr<CefBrowser> browser);

  // Get the toolbar
  OwlUIToolbar* GetToolbar() { return toolbar_; }

  // Window management
  void ShowWindow();
  void HideWindow();
  void CloseWindow();
  void SetWindowTitle(const std::string& title);

  // Tab management (macOS native tabs)
  void NewTab(const std::string& url = "owl://homepage.html");
  void* GetMainWindow() {
#if defined(OS_MACOS)
    return main_window_;
#elif defined(OS_LINUX)
    return gtk_window_;
#elif defined(OS_WINDOWS)
    return hwnd_;
#else
    return nullptr;
#endif
  }

  // Sidebar management
  void ShowSidebar();
  void HideSidebar();
  void ToggleSidebar();
  bool IsSidebarVisible() const { return sidebar_visible_; }

  // Agent mode UI
  void ShowAgentPrompt();
  void HideAgentPrompt();
  void UpdateAgentStatus(const std::string& status);
  void ExecutePrompt(const std::string& prompt);
  void StopExecution();  // Stop current task execution
  void SetTaskExecuting(bool executing);  // Update UI state when task starts/stops
  void UpdateTaskStatusDot();  // Update status dot color based on current task state
  void UpdateTasksList();  // Update task list content from OwlTaskState
  void ShowTasksList();  // Show tasks list panel
  void HideTasksList();  // Hide tasks list panel
  void ToggleTasksList();  // Toggle tasks list visibility
  void RepositionOverlaysForResize();  // Reposition overlays when window is resized
  void CleanupOverlays();  // Force cleanup of all overlays (called on window close)
  bool IsAgentPromptVisible() const { return agent_prompt_visible_; }

  // Response display
  void ShowResponseArea(const std::string& response_text);  // Show response area with text
  void HideResponseArea();  // Hide response area
  void UpdateResponseText(const std::string& text);  // Update response text

  // Proxy configuration UI
  void ShowProxyOverlay();  // Show proxy configuration overlay
  void HideProxyOverlay();  // Hide proxy configuration overlay
  void ToggleProxyOverlay();  // Toggle proxy overlay visibility
  bool IsProxyOverlayVisible() const { return proxy_overlay_visible_; }
  void SaveProxySettings();  // Save proxy settings from UI
  void ConnectProxy();  // Apply saved proxy to browser
  void DisconnectProxy();  // Remove proxy, use direct connection
  void UpdateProxyStatus();  // Update proxy status display

private:
  OwlUIDelegate();
  ~OwlUIDelegate();

  // Private helper methods for creating UI components
  void CreateAgentPromptOverlay();
  void CreateTasksPanel();
  void CreateResponseArea();
  void CreateProxyOverlay();

  static OwlUIDelegate* instance_;

#if defined(OS_MACOS)
  NSWindow* main_window_;
  NSView* content_view_;  // Browser content view (below toolbar)
  OwlUIWindowDelegate* window_delegate_;
  OwlUIToolbar* toolbar_;
  NSView* prompt_overlay_;  // AI prompt overlay panel
  NSTextField* prompt_input_;  // Prompt input field
  NSButton* prompt_send_button_;  // Send button
  NSButton* tasks_button_;  // Tasks button with status dot
  NSView* status_dot_;  // Status indicator dot
  NSView* tasks_panel_;  // Tasks list panel
  NSView* progress_border_;  // Animated dotted border for task execution feedback
  void* prompt_helper_;  // Helper object for callbacks (retained)
  NSView* response_area_;  // Response display area
  NSTextField* response_text_field_;  // Text field for displaying response
  // Proxy overlay UI elements
  NSView* proxy_overlay_;  // Proxy configuration overlay
  NSPopUpButton* proxy_type_popup_;  // Proxy type dropdown (HTTP, SOCKS4, SOCKS5)
  NSTextField* proxy_host_input_;  // Proxy host input field
  NSTextField* proxy_port_input_;  // Proxy port input field
  NSTextField* proxy_username_input_;  // Proxy username (optional)
  NSTextField* proxy_password_input_;  // Proxy password (optional)
  NSTextField* proxy_timezone_input_;  // Timezone override (e.g., "America/New_York")
  NSButton* proxy_stealth_checkbox_;  // Stealth mode checkbox
  NSButton* proxy_ca_checkbox_;  // Trust custom CA checkbox
  NSTextField* proxy_ca_path_label_;  // CA certificate path display
  NSButton* proxy_ca_browse_button_;  // Browse for CA certificate
  // Tor-specific UI elements for circuit isolation
  NSButton* proxy_tor_checkbox_;  // Mark as Tor proxy checkbox
  NSTextField* proxy_tor_port_input_;  // Tor control port input
  NSTextField* proxy_tor_password_input_;  // Tor control password input
  NSButton* proxy_save_button_;  // Save settings button
  NSButton* proxy_connect_button_;  // Connect/Disconnect button
  NSTextField* proxy_status_label_;  // Connection status display
  bool proxy_settings_saved_;  // Whether settings have been saved
  void* proxy_helper_;  // Helper object for proxy callbacks (retained)
#elif defined(OS_LINUX)
  void* gtk_window_;        // GtkWidget* (GtkWindow)
  void* overlay_;           // GtkWidget* (GtkOverlay)
  void* content_view_;      // GtkWidget* (content area for CEF browser)
  OwlUIToolbar* toolbar_;
  void* prompt_overlay_;    // GtkWidget* (AI prompt overlay box)
  void* prompt_input_;      // GtkWidget* (GtkEntry)
  void* prompt_send_button_;  // GtkWidget* (GtkButton)
  void* prompt_stop_button_;  // GtkWidget* (GtkButton)
  void* tasks_button_;      // GtkWidget* (GtkButton)
  void* status_dot_;        // GtkWidget* (status indicator)
  void* tasks_panel_;       // GtkWidget* (tasks list panel)
  void* tasks_scroll_;      // GtkWidget* (GtkScrolledWindow for tasks)
  void* tasks_container_;   // GtkWidget* (GtkBox for task items)
  void* progress_border_;   // Reserved for future animated border
  void* response_area_;     // GtkWidget* (response display area)
  void* response_scroll_;   // GtkWidget* (GtkScrolledWindow for response)
  void* response_text_view_;  // GtkWidget* (GtkTextView)
  void* response_text_buffer_;  // GtkTextBuffer*
  CefRefPtr<CefBrowser> browser_;  // CEF browser reference
#elif defined(OS_WINDOWS)
  HWND hwnd_;
#endif

  bool sidebar_visible_;
  bool agent_prompt_visible_;
  bool task_executing_;  // Track if task is currently executing
  bool tasks_list_visible_;  // Track if tasks list is visible
  bool proxy_overlay_visible_;  // Track if proxy overlay is visible
  OwlUIBrowser* browser_handler_;  // Reference to browser for executing prompts
};

#endif  // OWL_UI_DELEGATE_H_
