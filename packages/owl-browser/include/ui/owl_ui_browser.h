#ifndef OWL_UI_BROWSER_H_
#define OWL_UI_BROWSER_H_

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "owl_client.h"
#include <string>

// Main UI browser window manager for user-facing version
// Provides traditional browser UI with address bar, navigation, and LLM sidebar
// Inherits from OwlClient to support AI intelligence features (text extraction, etc.)
class OwlUIBrowser : public OwlClient,
                      public CefKeyboardHandler,
                      public CefContextMenuHandler {
public:
  OwlUIBrowser();
  ~OwlUIBrowser() override;

  // Create main browser window
  void CreateBrowserWindow(const std::string& url = "https://www.google.com");

  // CefClient methods - override to return this instance for UI-specific handlers
  // Return this to use OwlUIBrowser's OnConsoleMessage for picker result handling
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  // Return our handler to prevent CEF's buggy default context menu on macOS
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return this; }

  // CefRenderProcessHandler - handle messages from renderer
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 CefProcessId source_process,
                                 CefRefPtr<CefProcessMessage> message) override;

  // CefLifeSpanHandler methods
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     int popup_id,
                     const CefString& target_url,
                     const CefString& target_frame_name,
                     CefLifeSpanHandler::WindowOpenDisposition target_disposition,
                     bool user_gesture,
                     const CefPopupFeatures& popupFeatures,
                     CefWindowInfo& windowInfo,
                     CefRefPtr<CefClient>& client,
                     CefBrowserSettings& settings,
                     CefRefPtr<CefDictionaryValue>& extra_info,
                     bool* no_javascript_access) override;

  // CefLoadHandler methods
  void OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, TransitionType transition_type) override;
  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) override;
  void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode, const CefString& errorText, const CefString& failedUrl) override;

  // CefDisplayHandler methods
  void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override;
  void OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& url) override;
  bool OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level, const CefString& message, const CefString& source, int line) override;

  // CefKeyboardHandler methods
  bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser, const CefKeyEvent& event, CefEventHandle os_event, bool* is_keyboard_shortcut) override;

  // CefContextMenuHandler methods
  void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefContextMenuParams> params, CefRefPtr<CefMenuModel> model) override;
  bool OnContextMenuCommand(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefContextMenuParams> params, int command_id, EventFlags event_flags) override;

  // Navigation
  void Navigate(const std::string& url);
  void GoBack();
  void GoForward();
  void Reload();
  void StopLoading();

  // Agent mode
  void ToggleAgentMode();
  void ExecuteAgentPrompt(const std::string& prompt);
  bool IsAgentMode() const { return agent_mode_; }

  // Sidebar
  void ToggleSidebar();
  bool IsSidebarVisible() const { return sidebar_visible_; }

  // Get browser instance
  CefRefPtr<CefBrowser> GetBrowser() const { return browser_; }

  // Set as playground window
  void SetAsPlayground() { is_playground_ = true; }
  bool IsPlayground() const { return is_playground_; }

  // Set main browser for playground to control
  void SetMainBrowser(CefRefPtr<CefBrowser> main_browser) { main_browser_ = main_browser; }

  // Get/Set playground instance (static)
  static CefRefPtr<OwlUIBrowser> GetPlaygroundInstance() { return playground_instance_; }
  static void SetPlaygroundInstance(CefRefPtr<OwlUIBrowser> instance) { playground_instance_ = instance; }

  // Get main browser count (static)
  static int GetMainBrowserCount() { return main_browser_count_; }

  // Focus this window
  void FocusWindow();

  // Bring browser window to front (native)
  static void BringBrowserToFront(CefRefPtr<CefBrowser> browser);

private:
  CefRefPtr<CefBrowser> browser_;
  bool agent_mode_;
  bool sidebar_visible_;
  std::string current_url_;
  std::string current_title_;
  bool is_playground_;
  CefRefPtr<CefBrowser> main_browser_;
  void* playground_window_handle_;  // NSView* for playground window content view

  // Static playground instance to prevent duplicates
  static CefRefPtr<OwlUIBrowser> playground_instance_;

  // Track number of main browser instances (not playground/console)
  static int main_browser_count_;

  // DOM elements accumulator for chunked transfer
  std::string accumulated_dom_elements_;
  int expected_dom_total_;

  // Inject UI overlay HTML/CSS/JS
  void InjectUIOverlay();
  void UpdateAddressBar(const std::string& url);
  void UpdateNavigationButtons();

  // Element picker
  void InjectElementPickerOverlay(CefRefPtr<CefFrame> main_frame, CefRefPtr<CefBrowser> playground_browser, const std::string& input_id);

  // Position picker
  void InjectPositionPickerOverlay(CefRefPtr<CefFrame> main_frame, CefRefPtr<CefBrowser> playground_browser, const std::string& input_id);

  // Test execution
  void ExecuteTest(const std::string& test_json, CefRefPtr<CefBrowser> playground_browser);
  void SendProgressUpdate(CefRefPtr<CefBrowser> playground_browser, const std::string& status,
                          const std::string& message, int current_step, int total_steps);

  IMPLEMENT_REFCOUNTING(OwlUIBrowser);
  DISALLOW_COPY_AND_ASSIGN(OwlUIBrowser);
};

#endif  // OWL_UI_BROWSER_H_
