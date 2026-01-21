#include "owl_native_screenshot.h"
#include <AppKit/AppKit.h>
#include <CoreGraphics/CoreGraphics.h>
#include "logger.h"

std::vector<uint8_t> CaptureNativeScreenshot(CefRefPtr<CefBrowser> browser,
                                              int x, int y, int width, int height,
                                              const std::vector<ElementRenderInfo>& grid_items,
                                              int base_x, int base_y) {
  std::vector<uint8_t> result;

  @autoreleasepool {
    // Get the browser's NSView
    NSView* parentView = (__bridge NSView*)browser->GetHost()->GetWindowHandle();
    if (!parentView) {
      LOG_ERROR("NativeScreenshot", "Failed to get browser NSView");
      return result;
    }

    // CRITICAL: GetWindowHandle returns the parent content_view_
    // The actual CEF browser view is a subview of this parent
    // We need to find the CEF browser view which has the rendered content
    NSView* browserView = nil;

    // Look for the CEF browser view in subviews
    for (NSView* subview in [parentView subviews]) {
      // CEF's browser view class name contains "CefBrowserView" or "BrowserView"
      NSString* className = NSStringFromClass([subview class]);
      (void)className;  // Used in LOG_DEBUG below
      LOG_DEBUG("NativeScreenshot", "Found subview: " + std::string([className UTF8String]));

      // The first subview is usually the CEF browser view
      // Or we can check for specific CEF view classes
      if (browserView == nil) {
        browserView = subview;
        LOG_DEBUG("NativeScreenshot", "Using first subview as browser view: " + std::string([className UTF8String]));
      }
    }

    // Fallback to parent if no subviews found
    if (!browserView) {
      LOG_WARN("NativeScreenshot", "No subviews found, using parent view");
      browserView = parentView;
    }

    // Get view bounds
    NSRect viewBounds = [browserView bounds];
    (void)viewBounds;  // Used in LOG_DEBUG below

    LOG_DEBUG("NativeScreenshot", "View bounds: " + std::to_string(viewBounds.size.width) + "x" +
              std::to_string(viewBounds.size.height) +
              ", Web coords: (" + std::to_string(x) + "," + std::to_string(y) +
              "), Size: " + std::to_string(width) + "x" + std::to_string(height));

    // Get the window that contains this view
    NSWindow* window = [browserView window];
    if (!window) {
      LOG_ERROR("NativeScreenshot", "Failed to get window from view");
      return result;
    }

    // Get window number for CGWindowListCreateImage
    CGWindowID windowID = (CGWindowID)[window windowNumber];
    LOG_DEBUG("NativeScreenshot", "Window ID: " + std::to_string(windowID));

    // Convert browser view coordinates to window coordinates
    NSRect browserViewFrameInWindow = [browserView convertRect:[browserView bounds] toView:nil];

    LOG_DEBUG("NativeScreenshot", "Browser view in window: x=" + std::to_string(browserViewFrameInWindow.origin.x) +
              " y=" + std::to_string(browserViewFrameInWindow.origin.y) +
              " w=" + std::to_string(browserViewFrameInWindow.size.width) +
              " h=" + std::to_string(browserViewFrameInWindow.size.height));

    // Capture the entire window using CGWindowListCreateImage
    // This captures GPU-accelerated content that cacheDisplayInRect cannot
    CGImageRef windowImage = CGWindowListCreateImage(
      CGRectNull,  // Capture entire window
      kCGWindowListOptionIncludingWindow,
      windowID,
      kCGWindowImageBoundsIgnoreFraming | kCGWindowImageNominalResolution
    );

    if (!windowImage) {
      LOG_ERROR("NativeScreenshot", "Failed to capture window image");
      return result;
    }

    // Get window image dimensions
    CGFloat windowBitmapWidth = CGImageGetWidth(windowImage);
    CGFloat windowBitmapHeight = CGImageGetHeight(windowImage);
    (void)windowBitmapWidth;  // Used in LOG_DEBUG below

    LOG_DEBUG("NativeScreenshot", "Full window image: " +
              std::to_string(windowBitmapWidth) + "x" +
              std::to_string(windowBitmapHeight));

    // CRITICAL: Coordinate conversion
    // browserViewFrameInWindow: origin at BOTTOM-LEFT (NSView coordinates)
    // windowBitmap (CGImage): origin at TOP-LEFT (standard bitmap coordinates)
    // x, y parameters: relative to browserView in web coordinates (top-left of browser view)

    // Calculate browser view's position in the window bitmap
    // NSView: browserViewFrameInWindow.origin.y = distance from BOTTOM of window
    // Bitmap: need distance from TOP of window
    CGFloat browserViewBottomInWindow = browserViewFrameInWindow.origin.y;
    CGFloat browserViewTopInBitmap = windowBitmapHeight - browserViewBottomInWindow - browserViewFrameInWindow.size.height;
    CGFloat browserViewLeftInBitmap = browserViewFrameInWindow.origin.x;

    LOG_DEBUG("NativeScreenshot", "Browser view top in bitmap: " + std::to_string(browserViewTopInBitmap) +
              " left: " + std::to_string(browserViewLeftInBitmap));

    // Now calculate the crop region
    // x, y are already in top-left coordinates relative to the browser view
    CGFloat cropX = browserViewLeftInBitmap + x;
    CGFloat cropY = browserViewTopInBitmap + y;

    NSRect cropRect = NSMakeRect(cropX, cropY, width, height);
    (void)cropRect;  // Used in LOG_DEBUG below

    LOG_DEBUG("NativeScreenshot", "Final crop rect: x=" + std::to_string(cropRect.origin.x) +
              " y=" + std::to_string(cropRect.origin.y) +
              " w=" + std::to_string(cropRect.size.width) +
              " h=" + std::to_string(cropRect.size.height));

    // Crop directly from the CGImage to avoid coordinate system issues
    CGImageRef croppedImage = CGImageCreateWithImageInRect(windowImage,
                                                           CGRectMake(cropX, cropY, width, height));

    // Release the full window image now that we've cropped from it
    CGImageRelease(windowImage);

    if (!croppedImage) {
      LOG_ERROR("NativeScreenshot", "Failed to crop window image");
      return result;
    }

    // Create bitmap from cropped CGImage (maintains top-left origin)
    NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc] initWithCGImage:croppedImage];
    CGImageRelease(croppedImage);

    if (!bitmap) {
      LOG_ERROR("NativeScreenshot", "Failed to create bitmap from cropped image");
      return result;
    }

    LOG_DEBUG("NativeScreenshot", "Cropped bitmap created: " +
              std::to_string([bitmap pixelsWide]) + "x" +
              std::to_string([bitmap pixelsHigh]));

    // Now set up graphics context for drawing overlays
    [NSGraphicsContext saveGraphicsState];
    NSGraphicsContext* context = [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
    [NSGraphicsContext setCurrentContext:context];

    // CRITICAL: NSBitmapImageRep's graphics context uses BOTTOM-LEFT origin (AppKit convention)
    // But our bitmap data from CGImage has correct orientation (top-left)
    // We need to flip Y coordinates when drawing, but NOT flip the entire context (which would flip text)

    // Draw numbered overlays on the bitmap

    // Determine grid dimensions from the number of items
    int grid_size = (grid_items.size() == 9) ? 3 : 4;  // 3x3 or 4x4 grid
    CGFloat tile_width = static_cast<CGFloat>(width) / grid_size;
    CGFloat tile_height = static_cast<CGFloat>(height) / grid_size;

    for (size_t i = 0; i < grid_items.size() && i < 16; i++) {
      // Calculate row and column from index
      int row = static_cast<int>(i) / grid_size;
      int col = static_cast<int>(i) % grid_size;

      // Calculate item position directly from grid cell (no gaps in captured image)
      CGFloat itemX = col * tile_width;
      CGFloat itemY = row * tile_height;

      // CRITICAL: Convert from top-left web coordinates to bottom-left AppKit coordinates
      // for drawing in NSGraphicsContext
      // In web coords: itemY is distance from TOP to item's TOP
      // In AppKit coords: we need distance from BOTTOM to overlay's BOTTOM
      //
      // Item's top in AppKit = height - itemY
      // We want overlay at item's top-left corner with 2px margin
      // So overlay's top = height - itemY - 2 (margin from item's top)
      // Overlay's bottom = overlay's top - overlaySize
      CGFloat overlaySize = 16;
      CGFloat overlayX = itemX + 2;
      // Calculate overlay's bottom edge in AppKit coordinates (bottom-left origin)
      CGFloat overlayY = height - itemY - 2 - overlaySize;

      // Draw red background
      [[NSColor colorWithRed:1.0 green:0.0 blue:0.0 alpha:0.85] setFill];
      NSRect overlayRect = NSMakeRect(overlayX, overlayY, overlaySize, overlaySize);
      NSBezierPath* path = [NSBezierPath bezierPathWithRect:overlayRect];
      [path fill];

      // Draw white number
      // Font size 10 because image gets upscaled ~2x for vision model (will appear as ~20)
      NSString* number = [NSString stringWithFormat:@"%zu", i];
      NSDictionary* attributes = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:10],
        NSForegroundColorAttributeName: [NSColor whiteColor]
      };

      NSSize textSize = [number sizeWithAttributes:attributes];
      CGFloat textX = overlayX + (overlaySize - textSize.width) / 2;
      CGFloat textY = overlayY + (overlaySize - textSize.height) / 2;

      [number drawAtPoint:NSMakePoint(textX, textY) withAttributes:attributes];
    }

    [NSGraphicsContext restoreGraphicsState];

    // Convert to PNG
    NSData* pngData = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];

    if (!pngData) {
      LOG_ERROR("NativeScreenshot", "Failed to convert to PNG");
      return result;
    }

    // Copy to vector
    const uint8_t* bytes = (const uint8_t*)[pngData bytes];
    size_t length = [pngData length];
    result.assign(bytes, bytes + length);

    LOG_DEBUG("NativeScreenshot", "Captured " + std::to_string(width) + "x" + std::to_string(height) +
             " screenshot, PNG size: " + std::to_string(result.size()) + " bytes");
  }

  return result;
}
