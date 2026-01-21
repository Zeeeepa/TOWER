// owl_macos_init.mm
// Minimal NSApplication initialization for headless mode on macOS
// This is needed because CEF expects NSApplication to implement CefAppProtocol

#ifdef OS_MACOS

#import <Cocoa/Cocoa.h>
#include "include/cef_application_mac.h"

// Minimal NSApplication subclass that implements CefAppProtocol
// This is required for CEF to work correctly on macOS, even in headless mode
@interface OwlHeadlessApplication : NSApplication <CefAppProtocol> {
 @private
  BOOL handlingSendEvent_;
}
@end

@implementation OwlHeadlessApplication

- (BOOL)isHandlingSendEvent {
  return handlingSendEvent_;
}

- (void)setHandlingSendEvent:(BOOL)handlingSendEvent {
  handlingSendEvent_ = handlingSendEvent;
}

- (void)sendEvent:(NSEvent*)event {
  CefScopedSendingEvent sendingEventScoper;
  [super sendEvent:event];
}

@end

// C++ interface for initializing the headless NSApplication
extern "C" void InitializeHeadlessNSApplication() {
  @autoreleasepool {
    // Create and initialize the custom NSApplication
    // This must be done before CefInitialize
    [OwlHeadlessApplication sharedApplication];

    // We don't run the application or set up a delegate
    // since we're in headless mode - we just need the CefAppProtocol implementation
  }
}

#endif // OS_MACOS
