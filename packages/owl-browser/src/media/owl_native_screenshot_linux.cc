#include "owl_native_screenshot.h"
#include "logger.h"

#if defined(OS_LINUX)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cairo/cairo.h>
#include <vector>
#include <cstring>
#include <cmath>

std::vector<uint8_t> CaptureNativeScreenshot(CefRefPtr<CefBrowser> browser,
                                              int x, int y, int width, int height,
                                              const std::vector<ElementRenderInfo>& grid_items,
                                              int base_x, int base_y) {
  std::vector<uint8_t> result;

  // Get X11 window handle from CEF
  unsigned long window_handle = (unsigned long)browser->GetHost()->GetWindowHandle();
  if (!window_handle) {
    LOG_ERROR("NativeScreenshot", "Failed to get window handle from browser");
    return result;
  }

  // Open X11 display
  Display* display = XOpenDisplay(nullptr);
  if (!display) {
    LOG_ERROR("NativeScreenshot", "Failed to open X11 display");
    return result;
  }

  Window window = (Window)window_handle;

  // Get window attributes to verify it exists
  XWindowAttributes window_attrs;
  if (!XGetWindowAttributes(display, window, &window_attrs)) {
    LOG_ERROR("NativeScreenshot", "Failed to get window attributes");
    XCloseDisplay(display);
    return result;
  }

  LOG_DEBUG("NativeScreenshot", "Window size: " + std::to_string(window_attrs.width) + "x" +
            std::to_string(window_attrs.height));
  LOG_DEBUG("NativeScreenshot", "Capture region: x=" + std::to_string(x) + " y=" + std::to_string(y) +
            " w=" + std::to_string(width) + " h=" + std::to_string(height));

  // Clamp coordinates to window bounds
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x + width > window_attrs.width) width = window_attrs.width - x;
  if (y + height > window_attrs.height) height = window_attrs.height - y;

  if (width <= 0 || height <= 0) {
    LOG_ERROR("NativeScreenshot", "Invalid capture dimensions after clamping");
    XCloseDisplay(display);
    return result;
  }

  // Capture the window region using XGetImage
  XImage* ximage = XGetImage(display, window, x, y, width, height, AllPlanes, ZPixmap);
  if (!ximage) {
    LOG_ERROR("NativeScreenshot", "XGetImage failed");
    XCloseDisplay(display);
    return result;
  }

  LOG_DEBUG("NativeScreenshot", "XImage captured: " + std::to_string(ximage->width) + "x" +
            std::to_string(ximage->height) + " depth=" + std::to_string(ximage->depth));

  // Create Cairo surface from XImage data
  // We need to convert XImage data to Cairo's ARGB32 format
  cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
    LOG_ERROR("NativeScreenshot", "Failed to create Cairo surface");
    XDestroyImage(ximage);
    XCloseDisplay(display);
    return result;
  }

  unsigned char* cairo_data = cairo_image_surface_get_data(surface);
  int cairo_stride = cairo_image_surface_get_stride(surface);

  // Copy and convert XImage data to Cairo ARGB32 format
  for (int row = 0; row < height; row++) {
    for (int col = 0; col < width; col++) {
      unsigned long pixel = XGetPixel(ximage, col, row);

      // Extract RGB components (assuming 24-bit or 32-bit display)
      unsigned char r = (pixel >> 16) & 0xFF;
      unsigned char g = (pixel >> 8) & 0xFF;
      unsigned char b = pixel & 0xFF;

      // Cairo ARGB32 format: B G R A (little-endian)
      int offset = row * cairo_stride + col * 4;
      cairo_data[offset + 0] = b;  // Blue
      cairo_data[offset + 1] = g;  // Green
      cairo_data[offset + 2] = r;  // Red
      cairo_data[offset + 3] = 0xFF;  // Alpha (fully opaque)
    }
  }

  cairo_surface_mark_dirty(surface);

  // Free XImage (we've copied the data)
  XDestroyImage(ximage);
  XCloseDisplay(display);

  // Create Cairo context for drawing overlays
  cairo_t* cr = cairo_create(surface);

  // Draw numbered overlays for grid items (max 9)
  for (size_t i = 0; i < grid_items.size() && i < 9; i++) {
    const auto& item = grid_items[i];

    // Convert item position to relative coordinates within the capture rect
    double itemX = item.x - base_x;
    double itemY = item.y - base_y;

    // Skip if item is outside capture area
    if (itemX < 0 || itemY < 0 || itemX >= width || itemY >= height) {
      continue;
    }

    // Overlay position (top-left corner + 3px margin)
    double overlayX = itemX + 3;
    double overlayY = itemY + 3;
    double overlaySize = 28;

    // Draw red background rectangle
    cairo_set_source_rgba(cr, 1.0, 0.0, 0.0, 0.85);  // Red with 85% opacity
    cairo_rectangle(cr, overlayX, overlayY, overlaySize, overlaySize);
    cairo_fill(cr);

    // Draw white number
    char number_str[2];
    snprintf(number_str, sizeof(number_str), "%zu", i);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 18);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);  // White

    // Get text extents for centering
    cairo_text_extents_t extents;
    cairo_text_extents(cr, number_str, &extents);

    double textX = overlayX + (overlaySize - extents.width) / 2 - extents.x_bearing;
    double textY = overlayY + (overlaySize - extents.height) / 2 - extents.y_bearing;

    cairo_move_to(cr, textX, textY);
    cairo_show_text(cr, number_str);
  }

  cairo_destroy(cr);

  // Write Cairo surface to PNG in memory
  // Cairo provides a convenient function for this
  struct PngWriteData {
    std::vector<uint8_t>* buffer;
  };

  PngWriteData write_data;
  write_data.buffer = &result;

  auto png_write_func = [](void* closure, const unsigned char* data, unsigned int length) -> cairo_status_t {
    PngWriteData* wd = static_cast<PngWriteData*>(closure);
    wd->buffer->insert(wd->buffer->end(), data, data + length);
    return CAIRO_STATUS_SUCCESS;
  };

  cairo_status_t status = cairo_surface_write_to_png_stream(surface, png_write_func, &write_data);

  if (status != CAIRO_STATUS_SUCCESS) {
    LOG_ERROR("NativeScreenshot", "Failed to encode PNG: " + std::string(cairo_status_to_string(status)));
    result.clear();
  } else {
    LOG_DEBUG("NativeScreenshot", "Captured " + std::to_string(width) + "x" + std::to_string(height) +
             " screenshot with " + std::to_string(grid_items.size()) + " overlays, PNG size: " +
             std::to_string(result.size()) + " bytes");
  }

  cairo_surface_destroy(surface);

  return result;
}

#endif  // OS_LINUX
