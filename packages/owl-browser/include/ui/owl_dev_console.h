#ifndef OWL_DEV_CONSOLE_H
#define OWL_DEV_CONSOLE_H

#include "include/cef_base.h"
#include "include/cef_browser.h"
#include <string>
#include <vector>
#include <mutex>

#if defined(OS_MACOS)
#ifdef __OBJC__
@class NSWindow;
#else
// Forward declare NSWindow as an opaque pointer for C++
class NSWindow;
#endif
#endif

// Console message structure
struct ConsoleMessage {
  std::string level;      // "log", "warn", "error", "info", "debug"
  std::string message;
  std::string source;     // File/URL
  int line;
  std::string timestamp;
};

// Developer Console window for debugging
// This provides a Chromium DevTools-like interface for debugging the browser
// Currently implements Console tab, with structure for future tabs
class OwlDevConsole {
public:
  OwlDevConsole();
  ~OwlDevConsole();

  // Singleton instance
  static OwlDevConsole* GetInstance();

  // Create and show the console window
  void Show();

  // Hide the console window
  void Hide();

  // Toggle visibility
  void Toggle();

  // Check if console is visible
  bool IsVisible() const;

  // Add a console message (called from browser's console message handler)
  void AddConsoleMessage(const std::string& level,
                        const std::string& message,
                        const std::string& source,
                        int line);

  // Clear all console messages
  void ClearConsole();

  // Elements tab: Extract and display DOM tree
  void RefreshElementsTab();
  void UpdateElementsTree(const std::string& dom_json);

  // Network tab: Add network request (basic version)
  void AddNetworkRequest(const std::string& url,
                        const std::string& method,
                        const std::string& type,
                        int status_code,
                        const std::string& status_text,
                        size_t size,
                        int duration_ms);

  // Network tab: Add network request (extended version with headers and payloads)
  void AddNetworkRequestExtended(const std::string& url,
                                 const std::string& method,
                                 const std::string& type,
                                 int status_code,
                                 const std::string& status_text,
                                 size_t size,
                                 int duration_ms,
                                 const std::string& request_headers,
                                 const std::string& response_headers,
                                 const std::string& url_params,
                                 const std::string& post_data);

  // Get browser instance for the dev console window
  CefRefPtr<CefBrowser> GetBrowser() const { return browser_; }

  // Set the browser instance (called after creation)
  void SetBrowser(CefRefPtr<CefBrowser> browser) { browser_ = browser; }

  // Set the main browser being debugged
  void SetMainBrowser(CefRefPtr<CefBrowser> main_browser) { main_browser_ = main_browser; }

  // Get the main browser being debugged
  CefRefPtr<CefBrowser> GetMainBrowser() const { return main_browser_; }

  // Execute JavaScript in the main browser
  void ExecuteInMainBrowser(const std::string& code);

  // Refresh the console UI with current messages (called when console opens)
  void RefreshConsoleUI();

  // Generate the HTML content for the console
  std::string GenerateHTML();

  // NOTE: Future tabs can be added here
  // Example structure for adding tabs:
  //
  // 1. Add tab enum:
  //    enum class Tab { CONSOLE, NETWORK, ELEMENTS, SOURCES };
  //
  // 2. Add tab-specific methods:
  //    void AddNetworkRequest(const NetworkRequest& req);
  //    void UpdateElementsTree(const DOMTree& tree);
  //
  // 3. Add tab content generation:
  //    std::string GenerateNetworkTab();
  //    std::string GenerateElementsTab();
  //
  // 4. Update GenerateHTML() to include tab switching UI
  //
  // 5. Add message handlers in the HTML/JS for each tab

private:
  static OwlDevConsole* instance_;

#if defined(OS_MACOS)
  NSWindow* window_;
#elif defined(OS_LINUX)
  void* window_;  // GtkWidget* (GtkWindow)
#endif

  CefRefPtr<CefBrowser> browser_;           // The dev console browser
  CefRefPtr<CefBrowser> main_browser_;      // The main browser being debugged

  std::vector<ConsoleMessage> messages_;
  std::mutex messages_mutex_;

  bool is_visible_;

  // Helper to format timestamp
  std::string GetTimestamp();

  // Helper to send messages to the console UI
  void UpdateConsoleUI();
};

#endif // OWL_DEV_CONSOLE_H
