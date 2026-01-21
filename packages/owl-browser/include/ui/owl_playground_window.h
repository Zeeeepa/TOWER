#ifndef OWL_PLAYGROUND_WINDOW_H_
#define OWL_PLAYGROUND_WINDOW_H_

#include <string>
#include "include/cef_browser.h"

// Forward declarations
class OwlUIBrowser;

// Helper class for creating standalone playground windows without using the singleton delegate
class OwlPlaygroundWindow {
public:
  // Create a standalone playground window and return the content view handle for CEF
  static void* CreateWindow(OwlUIBrowser* browser_handler, int width, int height);

  // Set the browser reference in the window delegate (call after browser is created)
  static void SetBrowser(void* window_handle, CefRefPtr<CefBrowser> browser);

  // Focus a specific playground window
  static void FocusWindow(void* window_handle);

  // Close a specific playground window
  static void CloseWindow(void* window_handle);

  // Signal that CEF is ready to close (called from DoClose)
  static void SignalCefReady(void* window_handle);
};

#endif  // OWL_PLAYGROUND_WINDOW_H_
