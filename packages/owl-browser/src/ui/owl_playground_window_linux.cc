// Linux-specific implementation for playground window using GTK3
#include "owl_playground_window.h"

#if defined(OS_LINUX)

#include "logger.h"
#include "owl_ui_browser.h"
#include <gtk/gtk.h>
#include <map>
#include <mutex>

// Store playground window data
struct PlaygroundWindowData {
  GtkWidget* window;
  GtkWidget* container;
  CefRefPtr<CefBrowser> browser;
  bool is_closing;
  bool cef_ready;
};

// Global storage for playground windows (thread-safe)
static std::map<void*, PlaygroundWindowData*> g_playground_windows;
static std::mutex g_playground_mutex;

// Forward declaration of delete event handler
static gboolean OnPlaygroundWindowDelete(GtkWidget* widget, GdkEvent* event, gpointer data);
static void OnPlaygroundWindowDestroy(GtkWidget* widget, gpointer data);

// C wrapper for clearing playground instance (called from owl_ui_browser.cc)
extern "C" void ClearPlaygroundInstance() {
  LOG_DEBUG("PlaygroundWindow", "ClearPlaygroundInstance called (Linux implementation)");
  OwlUIBrowser::SetPlaygroundInstance(nullptr);
}

void* OwlPlaygroundWindow::CreateWindow(OwlUIBrowser* browser_handler, int width, int height) {
  LOG_DEBUG("PlaygroundWindow", "Creating playground window: " + std::to_string(width) + "x" + std::to_string(height));

  // Create GTK window
  GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(window), "Developer Playground");
  gtk_window_set_default_size(GTK_WINDOW(window), width, height);
  gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

  // Create container for CEF browser
  GtkWidget* container = gtk_fixed_new();
  gtk_widget_set_size_request(container, width, height);
  gtk_container_add(GTK_CONTAINER(window), container);

  // Create window data structure
  PlaygroundWindowData* window_data = new PlaygroundWindowData();
  window_data->window = window;
  window_data->container = container;
  window_data->browser = nullptr;
  window_data->is_closing = false;
  window_data->cef_ready = false;

  // Store window data
  {
    std::lock_guard<std::mutex> lock(g_playground_mutex);
    g_playground_windows[container] = window_data;
  }

  // Connect window signals
  g_signal_connect(G_OBJECT(window), "delete-event",
                   G_CALLBACK(OnPlaygroundWindowDelete), window_data);
  g_signal_connect(G_OBJECT(window), "destroy",
                   G_CALLBACK(OnPlaygroundWindowDestroy), window_data);

  // Show window
  gtk_widget_show_all(window);

  LOG_DEBUG("PlaygroundWindow", "Playground window created successfully");

  // Return container widget for CEF to use as parent
  return container;
}

void OwlPlaygroundWindow::SetBrowser(void* window_handle, CefRefPtr<CefBrowser> browser) {
  LOG_DEBUG("PlaygroundWindow", "SetBrowser called");

  if (!window_handle) {
    LOG_ERROR("PlaygroundWindow", "window_handle is NULL");
    return;
  }

  if (!browser) {
    LOG_ERROR("PlaygroundWindow", "browser is NULL");
    return;
  }

  std::lock_guard<std::mutex> lock(g_playground_mutex);

  auto it = g_playground_windows.find(window_handle);
  if (it == g_playground_windows.end()) {
    LOG_ERROR("PlaygroundWindow", "Window handle not found in playground windows map");
    return;
  }

  PlaygroundWindowData* window_data = it->second;
  window_data->browser = browser;

  LOG_DEBUG("PlaygroundWindow", "Browser reference successfully set - browser ID: " +
           std::to_string(browser->GetIdentifier()));
}

void OwlPlaygroundWindow::FocusWindow(void* window_handle) {
  if (!window_handle) {
    return;
  }

  std::lock_guard<std::mutex> lock(g_playground_mutex);

  auto it = g_playground_windows.find(window_handle);
  if (it == g_playground_windows.end()) {
    return;
  }

  PlaygroundWindowData* window_data = it->second;
  if (window_data->window) {
    gtk_window_present(GTK_WINDOW(window_data->window));
    LOG_DEBUG("PlaygroundWindow", "Playground window focused");
  }
}

void OwlPlaygroundWindow::SignalCefReady(void* window_handle) {
  if (!window_handle) {
    return;
  }

  std::lock_guard<std::mutex> lock(g_playground_mutex);

  auto it = g_playground_windows.find(window_handle);
  if (it == g_playground_windows.end()) {
    return;
  }

  PlaygroundWindowData* window_data = it->second;
  window_data->cef_ready = true;

  LOG_DEBUG("PlaygroundWindow", "CEF ready signal received, closing window");

  // Close the window on the main thread
  if (window_data->window) {
    gtk_widget_destroy(window_data->window);
  }
}

void OwlPlaygroundWindow::CloseWindow(void* window_handle) {
  // Alias for SignalCefReady for compatibility
  SignalCefReady(window_handle);
}

// GTK callbacks (must be outside class)
static gboolean OnPlaygroundWindowDelete(GtkWidget* widget, GdkEvent* event, gpointer data) {
  PlaygroundWindowData* window_data = static_cast<PlaygroundWindowData*>(data);

  LOG_DEBUG("PlaygroundWindow", "Delete event received, is_closing=" +
           std::string(window_data->is_closing ? "true" : "false") +
           ", cef_ready=" + std::string(window_data->cef_ready ? "true" : "false"));

  // Request browser close first time through
  if (window_data->browser && !window_data->is_closing) {
    LOG_DEBUG("PlaygroundWindow", "Browser exists, calling CloseBrowser(true)");
    window_data->is_closing = true;
    window_data->browser->GetHost()->CloseBrowser(true);  // Force close
    return TRUE;  // Don't close yet, wait for DoClose
  }

  // If CEF is ready, allow close
  if (window_data->cef_ready) {
    LOG_DEBUG("PlaygroundWindow", "CEF is ready, allowing window to close");
    return FALSE;  // Allow window to close
  }

  // Keep waiting
  LOG_DEBUG("PlaygroundWindow", "Waiting for CEF to be ready");
  return TRUE;  // Don't close yet
}

static void OnPlaygroundWindowDestroy(GtkWidget* widget, gpointer data) {
  PlaygroundWindowData* window_data = static_cast<PlaygroundWindowData*>(data);

  LOG_DEBUG("PlaygroundWindow", "Window destroy event received");

  // Manually clear playground instance
  ClearPlaygroundInstance();

  // Clear browser reference
  window_data->browser = nullptr;

  // Remove from global map
  {
    std::lock_guard<std::mutex> lock(g_playground_mutex);

    for (auto it = g_playground_windows.begin(); it != g_playground_windows.end(); ++it) {
      if (it->second == window_data) {
        g_playground_windows.erase(it);
        break;
      }
    }
  }

  // Free window data
  delete window_data;
}

// Set playground window title (used by OnTitleChange)
void SetPlaygroundWindowTitle(void* window_handle, const std::string& title) {
  if (!window_handle) {
    return;
  }

  std::lock_guard<std::mutex> lock(g_playground_mutex);

  auto it = g_playground_windows.find(window_handle);
  if (it == g_playground_windows.end()) {
    return;
  }

  PlaygroundWindowData* window_data = it->second;
  if (window_data->window) {
    gtk_window_set_title(GTK_WINDOW(window_data->window), title.c_str());
    LOG_DEBUG("PlaygroundWindow", "Updated window title: " + title);
  }
}

#endif  // OS_LINUX
