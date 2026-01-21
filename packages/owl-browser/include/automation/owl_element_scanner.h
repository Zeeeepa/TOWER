#pragma once

#include "include/cef_v8.h"
#include "include/cef_browser.h"
#include "include/cef_frame.h"
#include "owl_render_tracker.h"
#include <string>

// Visibility analysis result
struct VisibilityInfo {
  bool visible;
  float cumulative_opacity;  // Product of all ancestor opacities
  bool clipped_by_overflow;  // True if clipped by parent overflow:hidden
  std::string hidden_reason; // Debug info about why element is hidden
};

// Runs in renderer process to scan DOM and extract element positions
class OwlElementScanner {
public:
  // Scan all elements in the current document and send to browser process
  static void ScanAndReportElements(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const std::string& context_id);

  // Scan specific element by selector
  static bool ScanElement(CefRefPtr<CefV8Context> context, const std::string& selector, ElementRenderInfo& info);

private:
  // Recursively scan DOM tree with parent context
  static void ScanDOMTree(CefRefPtr<CefV8Context> context,
                          CefRefPtr<CefV8Value> element,
                          std::vector<ElementRenderInfo>& elements,
                          float parent_opacity = 1.0f,
                          int depth = 0);

  // Extract info from a single element
  static ElementRenderInfo ExtractElementInfo(CefRefPtr<CefV8Context> context,
                                               CefRefPtr<CefV8Value> element,
                                               float parent_opacity = 1.0f);

  // Analyze visibility including parent cascade effects
  static VisibilityInfo AnalyzeVisibility(CefRefPtr<CefV8Context> context,
                                          CefRefPtr<CefV8Value> element,
                                          const ElementRenderInfo& info,
                                          float parent_opacity);

  // Check if element is clipped by ancestor overflow
  static bool IsClippedByOverflow(CefRefPtr<CefV8Context> context,
                                  CefRefPtr<CefV8Value> element,
                                  const ElementRenderInfo& info);

  // Send element info to browser process via IPC
  static void SendElementsToBrowserProcess(CefRefPtr<CefBrowser> browser,
                                           CefRefPtr<CefFrame> frame,
                                           const std::string& context_id,
                                           const std::vector<ElementRenderInfo>& elements);
};
