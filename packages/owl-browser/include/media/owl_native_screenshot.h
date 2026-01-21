#pragma once

#include <vector>
#include <cstdint>
#include "include/cef_browser.h"
#include "owl_render_tracker.h"

// Capture screenshot using native platform APIs with numbered overlays
// macOS: NSView/CoreGraphics, Linux: X11/Cairo
std::vector<uint8_t> CaptureNativeScreenshot(CefRefPtr<CefBrowser> browser,
                                              int x, int y, int width, int height,
                                              const std::vector<ElementRenderInfo>& grid_items,
                                              int base_x, int base_y);
