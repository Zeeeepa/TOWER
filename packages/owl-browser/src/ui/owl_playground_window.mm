#import "owl_playground_window.h"
#import "owl_ui_browser.h"
#import "logger.h"
#import <Cocoa/Cocoa.h>

// Forward declare C function to clear playground instance
extern "C" void ClearPlaygroundInstance();

// Simple Objective-C delegate for playground windows (no toolbar, no prompts)
@interface OwlPlaygroundWindowDelegate : NSObject <NSWindowDelegate> {
  @public
  CefRefPtr<CefBrowser> _browser;
  BOOL _isClosing;
  BOOL _cefReady;  // Set to YES by DoClose when CEF is ready to close
}
@property (nonatomic, assign) CefRefPtr<CefBrowser> browser;
@property (nonatomic, assign) NSWindow* window;
@end

@implementation OwlPlaygroundWindowDelegate

- (instancetype)init {
  self = [super init];
  if (self) {
    _isClosing = NO;
    _cefReady = NO;
  }
  return self;
}

- (BOOL)windowShouldClose:(NSWindow*)window {
  LOG_DEBUG("PlaygroundWindow", "windowShouldClose called, _isClosing=" + std::string(_isClosing ? "YES" : "NO") + ", _cefReady=" + std::string(_cefReady ? "YES" : "NO"));

  // Request browser close first time through
  if (_browser && !_isClosing) {
    LOG_DEBUG("PlaygroundWindow", "Browser reference exists, calling CloseBrowser(true)");
    _isClosing = YES;
    _browser->GetHost()->CloseBrowser(true);  // Force close
    LOG_DEBUG("PlaygroundWindow", "Returning NO, waiting for DoClose to set _cefReady");
    return NO;  // Don't close yet, wait for DoClose to set _cefReady
  }

  // If CEF is ready (DoClose was called), allow close
  if (_cefReady) {
    LOG_DEBUG("PlaygroundWindow", "CEF is ready, allowing window to close");
    return YES;
  }

  // Keep waiting
  LOG_DEBUG("PlaygroundWindow", "Waiting for CEF to be ready");
  return NO;
}

- (void)windowWillClose:(NSNotification*)notification {
  // Playground window closing - just clean up, don't quit the app
  LOG_DEBUG("PlaygroundWindow", "windowWillClose notification received");

  // IMPORTANT: Manually clear the static playground instance since OnBeforeClose is not being called
  LOG_DEBUG("PlaygroundWindow", "Manually clearing playground state in windowWillClose");
  ClearPlaygroundInstance();

  // Clear browser reference
  _browser = nullptr;
}

@end

void* OwlPlaygroundWindow::CreateWindow(OwlUIBrowser* browser_handler, int width, int height) {
  @autoreleasepool {
    // Create window
    NSRect frame = NSMakeRect(0, 0, width, height);
    NSUInteger styleMask = NSWindowStyleMaskTitled |
                          NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable |
                          NSWindowStyleMaskResizable;

    NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                   styleMask:styleMask
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    [window setTitle:@"Developer Playground"];
    [window center];
    [window setMinSize:NSMakeSize(800, 600)];

    // Create delegate
    OwlPlaygroundWindowDelegate* delegate = [[OwlPlaygroundWindowDelegate alloc] init];
    delegate.window = window;
    [window setDelegate:delegate];

    // Create content view (full window, no toolbar)
    NSView* contentView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
    [contentView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    // Add view to window's content view
    [[window contentView] addSubview:contentView];

    // Show window
    [window makeKeyAndOrderFront:nil];

    LOG_DEBUG("PlaygroundWindow", "Standalone playground window created");

    // Return content view handle for CEF to use as parent
    return (__bridge void*)contentView;
  }
}

void OwlPlaygroundWindow::SetBrowser(void* window_handle, CefRefPtr<CefBrowser> browser) {
  @autoreleasepool {
    LOG_DEBUG("PlaygroundWindow", "SetBrowser called");

    if (!window_handle) {
      LOG_ERROR("PlaygroundWindow", "SetBrowser: window_handle is NULL");
      return;
    }

    if (!browser) {
      LOG_ERROR("PlaygroundWindow", "SetBrowser: browser is NULL");
      return;
    }

    NSView* view = (__bridge NSView*)window_handle;
    LOG_DEBUG("PlaygroundWindow", "Got view from window_handle");

    NSWindow* window = [view window];
    if (!window) {
      LOG_ERROR("PlaygroundWindow", "SetBrowser: Could not get window from view");
      return;
    }

    LOG_DEBUG("PlaygroundWindow", "Got window from view");

    OwlPlaygroundWindowDelegate* delegate = (OwlPlaygroundWindowDelegate*)[window delegate];
    if (!delegate) {
      LOG_ERROR("PlaygroundWindow", "SetBrowser: Window delegate is NULL");
      return;
    }

    LOG_DEBUG("PlaygroundWindow", "Got delegate from window");

    if (![delegate isKindOfClass:[OwlPlaygroundWindowDelegate class]]) {
      LOG_ERROR("PlaygroundWindow", "SetBrowser: Delegate is not OwlPlaygroundWindowDelegate class");
      return;
    }

    delegate->_browser = browser;
    LOG_DEBUG("PlaygroundWindow", "Browser reference successfully set in playground delegate - browser ID: " + std::to_string(browser->GetIdentifier()));
  }
}

void OwlPlaygroundWindow::FocusWindow(void* window_handle) {
  @autoreleasepool {
    if (window_handle) {
      NSView* view = (__bridge NSView*)window_handle;
      NSWindow* window = [view window];
      if (window) {
        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        LOG_DEBUG("PlaygroundWindow", "Playground window focused");
      }
    }
  }
}

void OwlPlaygroundWindow::SignalCefReady(void* window_handle) {
  @autoreleasepool {
    if (window_handle) {
      NSView* view = (__bridge NSView*)window_handle;
      NSWindow* window = [view window];
      if (window) {
        // Get the delegate and set cefReady flag
        OwlPlaygroundWindowDelegate* delegate = (OwlPlaygroundWindowDelegate*)[window delegate];
        if (delegate && [delegate isKindOfClass:[OwlPlaygroundWindowDelegate class]]) {
          delegate->_cefReady = YES;
          LOG_DEBUG("PlaygroundWindow", "Set _cefReady flag");
          // Just close the window directly - CEF will call OnBeforeClose after this
          dispatch_async(dispatch_get_main_queue(), ^{
            LOG_DEBUG("PlaygroundWindow", "Closing window on next run loop");
            [window close];
          });
        }
      }
    }
  }
}

// Legacy function name for compatibility
void OwlPlaygroundWindow::CloseWindow(void* window_handle) {
  OwlPlaygroundWindow::SignalCefReady(window_handle);
}

// Set playground window title (used by OnTitleChange)
void SetPlaygroundWindowTitle(void* window_handle, const std::string& title) {
  @autoreleasepool {
    if (window_handle) {
      NSView* view = (__bridge NSView*)window_handle;
      NSWindow* window = [view window];
      if (window) {
        [window setTitle:[NSString stringWithUTF8String:title.c_str()]];
        LOG_DEBUG("PlaygroundWindow", "Updated window title: " + title);
      }
    }
  }
}
