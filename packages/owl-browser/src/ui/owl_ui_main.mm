#include "owl_app.h"
#include "owl_ui_browser.h"
#include "owl_browser_manager.h"
#include "owl_license.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/wrapper/cef_library_loader.h"
#include "logger.h"
#include <iostream>

#if defined(OS_MACOS)
#include "include/cef_application_mac.h"
#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

// ============================================================================
// Custom Link Button with Hand Cursor
// ============================================================================

@interface OwlLinkButton : NSButton
@end

@implementation OwlLinkButton

- (void)resetCursorRects {
    [self addCursorRect:[self bounds] cursor:[NSCursor pointingHandCursor]];
}

@end

// ============================================================================
// License Activation Window for UI Version
// ============================================================================

@interface OwlLicenseWindowController : NSObject <NSWindowDelegate>
@property (nonatomic, strong) NSWindow* window;
@property (nonatomic, strong) NSTextField* fingerprintField;
@property (nonatomic, strong) NSTextField* statusLabel;
@property (nonatomic, copy) NSString* fingerprint;
@property (nonatomic, assign) olib::license::LicenseStatus initialStatus;
@property (nonatomic, assign) BOOL licenseActivated;
@end

@implementation OwlLicenseWindowController

- (instancetype)initWithStatus:(olib::license::LicenseStatus)status fingerprint:(NSString*)fingerprint {
    self = [super init];
    if (self) {
        _initialStatus = status;
        _fingerprint = [fingerprint copy];
        _licenseActivated = NO;
        [self createWindow];
    }
    return self;
}

- (void)createWindow {
    // Create the main window
    NSRect windowRect = NSMakeRect(0, 0, 520, 420);
    _window = [[NSWindow alloc] initWithContentRect:windowRect
                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    [_window setTitle:@"Owl Browser - License Activation"];
    [_window setDelegate:self];
    [_window center];

    NSView* contentView = [_window contentView];
    CGFloat padding = 24;
    CGFloat y = windowRect.size.height - padding;

    // App icon and title
    NSImageView* iconView = [[NSImageView alloc] initWithFrame:NSMakeRect(padding, y - 64, 64, 64)];
    [iconView setImage:[NSApp applicationIconImage]];
    [iconView setImageScaling:NSImageScaleProportionallyUpOrDown];
    [contentView addSubview:iconView];

    NSTextField* titleLabel = [NSTextField labelWithString:@"Welcome to Owl Browser"];
    [titleLabel setFont:[NSFont boldSystemFontOfSize:20]];
    [titleLabel setFrame:NSMakeRect(padding + 74, y - 30, 400, 28)];
    [contentView addSubview:titleLabel];

    NSTextField* subtitleLabel = [NSTextField labelWithString:@"Activate your license to get started"];
    [subtitleLabel setFont:[NSFont systemFontOfSize:13]];
    [subtitleLabel setTextColor:[NSColor secondaryLabelColor]];
    [subtitleLabel setFrame:NSMakeRect(padding + 74, y - 54, 400, 20)];
    [contentView addSubview:subtitleLabel];

    y -= 90;

    // Separator
    NSBox* separator1 = [[NSBox alloc] initWithFrame:NSMakeRect(padding, y, windowRect.size.width - padding * 2, 1)];
    [separator1 setBoxType:NSBoxSeparator];
    [contentView addSubview:separator1];

    y -= 24;

    // Status message based on license status
    NSString* statusMessage = [self getStatusMessage];
    _statusLabel = [NSTextField wrappingLabelWithString:statusMessage];
    [_statusLabel setFont:[NSFont systemFontOfSize:12]];
    [_statusLabel setFrame:NSMakeRect(padding, y - 48, windowRect.size.width - padding * 2, 48)];
    [contentView addSubview:_statusLabel];

    y -= 70;

    // License file selection section
    NSTextField* licenseLabel = [NSTextField labelWithString:@"License File (.olic):"];
    [licenseLabel setFont:[NSFont systemFontOfSize:12 weight:NSFontWeightMedium]];
    [licenseLabel setFrame:NSMakeRect(padding, y, 200, 18)];
    [contentView addSubview:licenseLabel];

    y -= 30;

    // Browse button - prominent
    NSButton* browseButton = [[NSButton alloc] initWithFrame:NSMakeRect(padding, y, windowRect.size.width - padding * 2, 36)];
    [browseButton setTitle:@"Select License File..."];
    [browseButton setBezelStyle:NSBezelStyleRounded];
    [browseButton setTarget:self];
    [browseButton setAction:@selector(browseLicenseFile:)];
    [contentView addSubview:browseButton];

    y -= 50;

    // Hardware fingerprint section
    NSTextField* fpLabel = [NSTextField labelWithString:@"Hardware Fingerprint (for license request):"];
    [fpLabel setFont:[NSFont systemFontOfSize:12 weight:NSFontWeightMedium]];
    [fpLabel setFrame:NSMakeRect(padding, y, 300, 18)];
    [contentView addSubview:fpLabel];

    y -= 28;

    // Fingerprint display field - use smaller font to fit long fingerprints
    _fingerprintField = [[NSTextField alloc] initWithFrame:NSMakeRect(padding, y, windowRect.size.width - padding * 2 - 80, 24)];
    [_fingerprintField setStringValue:_fingerprint];
    [_fingerprintField setEditable:NO];
    [_fingerprintField setSelectable:YES];
    [_fingerprintField setFont:[NSFont monospacedSystemFontOfSize:9 weight:NSFontWeightRegular]];
    [_fingerprintField setBackgroundColor:[NSColor controlBackgroundColor]];
    [[_fingerprintField cell] setLineBreakMode:NSLineBreakByTruncatingTail];
    [[_fingerprintField cell] setScrollable:YES];
    [contentView addSubview:_fingerprintField];

    // Copy button
    NSButton* copyButton = [[NSButton alloc] initWithFrame:NSMakeRect(windowRect.size.width - padding - 70, y, 70, 24)];
    [copyButton setTitle:@"Copy"];
    [copyButton setBezelStyle:NSBezelStyleRounded];
    [copyButton setTarget:self];
    [copyButton setAction:@selector(copyFingerprint:)];
    [contentView addSubview:copyButton];

    y -= 40;

    // Separator
    NSBox* separator2 = [[NSBox alloc] initWithFrame:NSMakeRect(padding, y, windowRect.size.width - padding * 2, 1)];
    [separator2 setBoxType:NSBoxSeparator];
    [contentView addSubview:separator2];

    y -= 30;

    // Get license section
    NSTextField* getLabel = [NSTextField labelWithString:@"Don't have a license?"];
    [getLabel setFont:[NSFont systemFontOfSize:12]];
    [getLabel setFrame:NSMakeRect(padding, y, 150, 18)];
    [contentView addSubview:getLabel];

    // Website link - use custom OwlLinkButton with hand cursor
    OwlLinkButton* websiteButton = [[OwlLinkButton alloc] initWithFrame:NSMakeRect(padding + 150, y - 2, 200, 22)];
    NSMutableAttributedString* linkText = [[NSMutableAttributedString alloc] initWithString:@"Get one at www.owlbrowser.net"];
    [linkText addAttribute:NSForegroundColorAttributeName value:[NSColor linkColor] range:NSMakeRange(0, [linkText length])];
    [linkText addAttribute:NSUnderlineStyleAttributeName value:@(NSUnderlineStyleSingle) range:NSMakeRange(11, 18)];
    [linkText addAttribute:NSFontAttributeName value:[NSFont systemFontOfSize:12] range:NSMakeRange(0, [linkText length])];
    [websiteButton setAttributedTitle:linkText];
    [websiteButton setBordered:NO];
    [websiteButton setTarget:self];
    [websiteButton setAction:@selector(openWebsite:)];
    [contentView addSubview:websiteButton];

    y -= 50;

    // Bottom buttons
    NSButton* quitButton = [[NSButton alloc] initWithFrame:NSMakeRect(windowRect.size.width - padding - 80, 20, 80, 32)];
    [quitButton setTitle:@"Quit"];
    [quitButton setBezelStyle:NSBezelStyleRounded];
    [quitButton setTarget:self];
    [quitButton setAction:@selector(quitApp:)];
    [quitButton setKeyEquivalent:@"\033"]; // Escape key
    [contentView addSubview:quitButton];
}

- (NSString*)getStatusMessage {
    switch (_initialStatus) {
        case olib::license::LicenseStatus::NOT_FOUND:
            return @"No license file found. Please select your license file (.olic) to activate Owl Browser.";
        case olib::license::LicenseStatus::EXPIRED:
            return @"Your license has expired. Please renew your license at www.owlbrowser.net or select a new license file.";
        case olib::license::LicenseStatus::INVALID_SIGNATURE:
            return @"The license file signature is invalid. Please download a valid license file from www.owlbrowser.net.";
        case olib::license::LicenseStatus::CORRUPTED:
            return @"The license file is corrupted. Please re-download your license file from www.owlbrowser.net.";
        case olib::license::LicenseStatus::HARDWARE_MISMATCH:
            return @"This license is bound to different hardware. Contact support@olib.ai to transfer your license.";
        default:
            return @"License validation failed. Please select a valid license file or visit www.owlbrowser.net for assistance.";
    }
}

- (void)browseLicenseFile:(id)sender {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    [panel setTitle:@"Select License File"];
    [panel setMessage:@"Choose your Owl Browser license file (.olic)"];

    // Use UTType for content types (macOS 11+)
    if (@available(macOS 11.0, *)) {
        UTType* olicType = [UTType typeWithFilenameExtension:@"olic"];
        if (olicType) {
            [panel setAllowedContentTypes:@[olicType]];
        }
    } else {
        // Fallback for older macOS
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [panel setAllowedFileTypes:@[@"olic"]];
        #pragma clang diagnostic pop
    }

    [panel beginSheetModalForWindow:_window completionHandler:^(NSModalResponse result) {
        if (result == NSModalResponseOK) {
            NSURL* url = [[panel URLs] firstObject];
            if (url) {
                [self activateLicenseFromPath:[url path]];
            }
        }
    }];
}

- (void)activateLicenseFromPath:(NSString*)path {
    auto* manager = olib::license::LicenseManager::GetInstance();
    olib::license::LicenseStatus status = manager->AddLicense([path UTF8String]);

    if (status == olib::license::LicenseStatus::VALID) {
        // Success - show confirmation and close
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:@"License Activated"];
        [alert setInformativeText:@"Your license has been activated successfully!\n\nOwl Browser will now start."];
        [alert setAlertStyle:NSAlertStyleInformational];
        [alert addButtonWithTitle:@"Continue"];
        [alert beginSheetModalForWindow:_window completionHandler:^(NSModalResponse response) {
            self.licenseActivated = YES;
            [self.window close];
            [NSApp stopModal];
        }];
    } else {
        // Failed - show error
        NSString* errorMessage;
        switch (status) {
            case olib::license::LicenseStatus::EXPIRED:
                errorMessage = @"This license has expired. Please obtain a new license from www.owlbrowser.net.";
                break;
            case olib::license::LicenseStatus::INVALID_SIGNATURE:
                errorMessage = @"This license file is invalid. Please ensure you have the correct license file.";
                break;
            case olib::license::LicenseStatus::CORRUPTED:
                errorMessage = @"This license file is corrupted. Please re-download it from your account.";
                break;
            case olib::license::LicenseStatus::HARDWARE_MISMATCH:
                errorMessage = @"This license is bound to different hardware. Contact support@olib.ai to transfer it.";
                break;
            default:
                errorMessage = @"Failed to activate the license. Please try again or contact support@olib.ai.";
                break;
        }

        [_statusLabel setStringValue:errorMessage];
        [_statusLabel setTextColor:[NSColor systemRedColor]];

        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:@"Activation Failed"];
        [alert setInformativeText:errorMessage];
        [alert setAlertStyle:NSAlertStyleWarning];
        [alert addButtonWithTitle:@"OK"];
        [alert beginSheetModalForWindow:_window completionHandler:nil];
    }
}

- (void)copyFingerprint:(id)sender {
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard clearContents];
    [pasteboard setString:_fingerprint forType:NSPasteboardTypeString];

    // Visual feedback
    NSButton* button = (NSButton*)sender;
    NSString* originalTitle = [button title];
    [button setTitle:@"Copied!"];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [button setTitle:originalTitle];
    });
}

- (void)openWebsite:(id)sender {
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"https://www.owlbrowser.net"]];
}

- (void)quitApp:(id)sender {
    _licenseActivated = NO;
    [_window close];
    [NSApp stopModal];
}

- (void)windowWillClose:(NSNotification*)notification {
    [NSApp stopModal];
}

- (void)showModal {
    [_window makeKeyAndOrderFront:nil];
    [NSApp runModalForWindow:_window];
}

@end

// Show license activation window and return YES if license was activated
static BOOL ShowLicenseActivationWindow(olib::license::LicenseStatus status, const std::string& fingerprint) {
    @autoreleasepool {
        NSString* fpString = [NSString stringWithUTF8String:fingerprint.c_str()];
        OwlLicenseWindowController* controller = [[OwlLicenseWindowController alloc] initWithStatus:status fingerprint:fpString];
        [controller showModal];
        return controller.licenseActivated;
    }
}

// ============================================================================
// Provide the custom NSApplication implementation on macOS.
@interface OlibNSApplication : NSApplication <CefAppProtocol, NSApplicationDelegate> {
 @private
  BOOL handlingSendEvent_;
}
@end

@implementation OlibNSApplication

- (BOOL)isHandlingSendEvent {
  return handlingSendEvent_;
}

- (void)setHandlingSendEvent:(BOOL)handlingSendEvent {
  handlingSendEvent_ = handlingSendEvent;
}

- (NSMenu*)applicationDockMenu:(NSApplication*)sender {
  @autoreleasepool {
    LOG_DEBUG("DockMenu", "applicationDockMenu called");

    // Create custom dock menu with list of open windows
    NSMenu* dockMenu = [[NSMenu alloc] init];

    // Add menu item for each open window
    NSArray* windows = [NSApp windows];
    LOG_DEBUG("DockMenu", "Total windows: " + std::to_string([windows count]));
    int windowCount = 0;

    for (NSWindow* window in windows) {
      std::string title_str = [[window title] UTF8String] ?: "(no title)";
      LOG_DEBUG("DockMenu", "Window: title='" + title_str + "' visible=" + std::to_string([window isVisible]));

      // Skip invisible windows and those without titles
      if (![window isVisible] || [[window title] length] == 0) {
        continue;
      }

      NSString* title = [window title];
      NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
                                                    action:@selector(focusWindow:)
                                             keyEquivalent:@""];
      [item setTarget:self];
      [item setRepresentedObject:window];
      [dockMenu addItem:item];
      windowCount++;
      LOG_DEBUG("DockMenu", "Added window to menu: " + std::string([title UTF8String]));
    }

    LOG_DEBUG("DockMenu", "Total menu items added: " + std::to_string(windowCount));

    // If no windows, add a placeholder
    if (windowCount == 0) {
      NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:@"No Windows Open"
                                                    action:nil
                                             keyEquivalent:@""];
      [item setEnabled:NO];
      [dockMenu addItem:item];
      LOG_DEBUG("DockMenu", "Added 'No Windows Open' placeholder");
    }

    return dockMenu;
  }
}

- (void)focusWindow:(NSMenuItem*)sender {
  @autoreleasepool {
    NSWindow* window = [sender representedObject];
    if (window) {
      [window makeKeyAndOrderFront:nil];
      [NSApp activateIgnoringOtherApps:YES];
    }
  }
}

- (BOOL)applicationShouldHandleReopen:(NSApplication*)sender hasVisibleWindows:(BOOL)flag {
  @autoreleasepool {
    LOG_DEBUG("AppDelegate", "applicationShouldHandleReopen called, hasVisibleWindows=" + std::string(flag ? "YES" : "NO"));

    // If no visible windows, show the most recent window
    if (!flag) {
      NSArray* windows = [NSApp windows];
      LOG_DEBUG("AppDelegate", "Total windows: " + std::to_string([windows count]));

      for (NSWindow* window in windows) {
        // Find the first non-panel, non-menu window
        if ([window canBecomeKeyWindow] && ![window isKindOfClass:[NSPanel class]]) {
          LOG_DEBUG("AppDelegate", "Showing window: " + std::string([[window title] UTF8String] ?: "(no title)"));
          [window makeKeyAndOrderFront:nil];
          [NSApp activateIgnoringOtherApps:YES];
          return YES;
        }
      }

      // If no existing windows, create a new one
      LOG_DEBUG("AppDelegate", "No windows found, creating new browser window");
      CefRefPtr<OwlUIBrowser> ui_browser(new OwlUIBrowser);
      ui_browser->CreateBrowserWindow("owl://homepage.html");
    } else {
      // If there are visible windows, just activate the app
      [NSApp activateIgnoringOtherApps:YES];
    }

    return YES;
  }
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
  LOG_DEBUG("AppDelegate", "applicationShouldTerminateAfterLastWindowClosed called");
  // Terminate the app when the last window closes
  return YES;
}

- (void)sendEvent:(NSEvent*)event {
  CefScopedSendingEvent sendingEventScoper;

  // Handle keyboard shortcuts for native controls (NSTextField) since we don't have an Edit menu
  if (event.type == NSEventTypeKeyDown) {
    NSResponder* firstResponder = [[self keyWindow] firstResponder];

    // Check if the first responder is a text field or text view (native control)
    BOOL isTextControl = [firstResponder isKindOfClass:[NSTextField class]] ||
                         [firstResponder isKindOfClass:[NSTextView class]] ||
                         [firstResponder isKindOfClass:[NSText class]];

    if (isTextControl && (event.modifierFlags & NSEventModifierFlagCommand)) {
      NSString* chars = [event charactersIgnoringModifiers];
      if ([chars length] == 1) {
        unichar character = [chars characterAtIndex:0];

        // Cmd+C: Copy
        if (character == 'c') {
          if ([firstResponder respondsToSelector:@selector(copy:)]) {
            [firstResponder performSelector:@selector(copy:) withObject:nil];
            return;  // Event handled, don't pass to super
          }
        }
        // Cmd+V: Paste
        else if (character == 'v') {
          if ([firstResponder respondsToSelector:@selector(paste:)]) {
            [firstResponder performSelector:@selector(paste:) withObject:nil];
            return;
          }
        }
        // Cmd+X: Cut
        else if (character == 'x') {
          if ([firstResponder respondsToSelector:@selector(cut:)]) {
            [firstResponder performSelector:@selector(cut:) withObject:nil];
            return;
          }
        }
        // Cmd+A: Select All
        else if (character == 'a') {
          if ([firstResponder respondsToSelector:@selector(selectAll:)]) {
            [firstResponder performSelector:@selector(selectAll:) withObject:nil];
            return;
          }
        }
        // Cmd+Z: Undo
        else if (character == 'z' && !(event.modifierFlags & NSEventModifierFlagShift)) {
          if ([firstResponder respondsToSelector:@selector(undo:)]) {
            [firstResponder performSelector:@selector(undo:) withObject:nil];
            return;
          }
        }
        // Cmd+Shift+Z: Redo
        else if (character == 'z' && (event.modifierFlags & NSEventModifierFlagShift)) {
          if ([firstResponder respondsToSelector:@selector(redo:)]) {
            [firstResponder performSelector:@selector(redo:) withObject:nil];
            return;
          }
        }
      }
    }
  }

  [super sendEvent:event];
}

@end

#endif  // OS_MACOS

#if defined(OS_WIN)
int APIENTRY wWinMain(HINSTANCE hInstance) {
  CefMainArgs main_args(hInstance);
#else
int main(int argc, char* argv[]) {
#if defined(OS_MACOS)
  // Initialize the custom NSApplication before CEF initialization.
  @autoreleasepool {
    OlibNSApplication* app = [OlibNSApplication sharedApplication];
    [app setDelegate:app];  // Set as its own delegate to handle dock menu
  }

  // =========================================================================
  // License CLI Commands for UI Version (--license add/remove/info/status)
  // =========================================================================
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--license" && i + 1 < argc) {
      std::string license_cmd = argv[i + 1];

      if (license_cmd == "add" && i + 2 < argc) {
        // Add license: owl_browser_ui --license add /path/to/license.olic
        std::string license_path = argv[i + 2];
        auto* manager = olib::license::LicenseManager::GetInstance();
        olib::license::LicenseStatus status = manager->AddLicense(license_path);

        if (status == olib::license::LicenseStatus::VALID) {
          std::cout << "License activated successfully!" << std::endl;
          const auto* data = manager->GetLicenseData();
          if (data) {
            std::cout << "{\"status\":\"valid\",\"valid\":true,"
                      << "\"license_id\":\"" << data->license_id << "\","
                      << "\"name\":\"" << data->name << "\","
                      << "\"organization\":\"" << data->organization << "\","
                      << "\"email\":\"" << data->email << "\","
                      << "\"type\":" << static_cast<int>(data->type) << ","
                      << "\"max_seats\":" << data->max_seats << ","
                      << "\"issue_date\":" << data->issue_timestamp << ","
                      << "\"expiry_date\":" << data->expiry_timestamp << ","
                      << "\"hardware_bound\":" << (data->hardware_bound ? "true" : "false")
                      << "}" << std::endl;
          }
          return 0;
        } else {
          std::cerr << "Failed to activate license: "
                    << olib::license::LicenseStatusToString(status) << std::endl;
          return 1;
        }
      }
      else if (license_cmd == "remove") {
        auto* manager = olib::license::LicenseManager::GetInstance();
        olib::license::LicenseStatus status = manager->RemoveLicense();
        if (status == olib::license::LicenseStatus::NOT_FOUND ||
            status == olib::license::LicenseStatus::VALID) {
          std::cout << "License removed successfully." << std::endl;
          return 0;
        } else {
          std::cerr << "Failed to remove license." << std::endl;
          return 1;
        }
      }
      else if (license_cmd == "info") {
        auto* manager = olib::license::LicenseManager::GetInstance();
        olib::license::LicenseStatus status = manager->Validate();
        if (status == olib::license::LicenseStatus::VALID) {
          const auto* data = manager->GetLicenseData();
          if (data) {
            std::cout << "License Information:" << std::endl;
            std::cout << "  ID: " << data->license_id << std::endl;
            std::cout << "  Name: " << data->name << std::endl;
            std::cout << "  Organization: " << data->organization << std::endl;
            std::cout << "  Email: " << data->email << std::endl;
            std::cout << "  Type: " << static_cast<int>(data->type) << std::endl;
            std::cout << "  Max Seats: " << data->max_seats << std::endl;
            std::cout << "  Hardware Bound: " << (data->hardware_bound ? "Yes" : "No") << std::endl;
          }
          return 0;
        } else {
          std::cerr << "No valid license found: "
                    << olib::license::LicenseStatusToString(status) << std::endl;
          return 1;
        }
      }
      else if (license_cmd == "status") {
        auto* manager = olib::license::LicenseManager::GetInstance();
        olib::license::LicenseStatus status = manager->Validate();
        std::cout << "License Status: "
                  << olib::license::LicenseStatusToString(status) << std::endl;
        return (status == olib::license::LicenseStatus::VALID) ? 0 : 1;
      }
      else if (license_cmd == "fingerprint") {
        std::string fp = olib::license::HardwareFingerprint::Generate();
        std::cout << "Hardware Fingerprint: " << fp << std::endl;
        return 0;
      }
    }
  }

  // =========================================================================
  // License Validation - UI will not start without valid license
  // =========================================================================
  @autoreleasepool {
    auto* license_manager = olib::license::LicenseManager::GetInstance();
    olib::license::LicenseStatus license_status = license_manager->Validate();

    if (license_status != olib::license::LicenseStatus::VALID) {
      std::string fingerprint = olib::license::HardwareFingerprint::Generate();
      LOG_WARN("UI", "License validation failed: " + std::string(olib::license::LicenseStatusToString(license_status)));

      // Show license activation window - allows user to select and activate license
      BOOL activated = ShowLicenseActivationWindow(license_status, fingerprint);
      if (!activated) {
        // User quit without activating
        return 1;
      }
      // License was activated successfully, continue with app startup
      LOG_DEBUG("UI", "License activated via activation window");
    } else {
      LOG_DEBUG("UI", "License validated successfully");
    }
  }

  // Load the CEF framework library at runtime instead of linking directly.
  // This is required on macOS for proper CEF initialization.
  CefScopedLibraryLoader library_loader;
  if (!library_loader.LoadInMain()) {
    fprintf(stderr, "Failed to load CEF framework\n");
    return 1;
  }
#endif

  CefMainArgs main_args(argc, argv);
#endif

  // Parse command line
  CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
#if defined(OS_WIN)
  command_line->InitFromString(::GetCommandLineW());
#else
  command_line->InitFromArgv(argc, argv);
#endif

  // Create application
  CefRefPtr<OwlApp> app(new OwlApp);

  // Execute sub-process if needed
  int exit_code = CefExecuteProcess(main_args, app.get(), nullptr);
  if (exit_code >= 0) {
    return exit_code;
  }

  // CEF settings for UI version
  CefSettings settings;
  settings.no_sandbox = true;
  settings.remote_debugging_port = 9223;  // Enable DevTools
  settings.log_severity = LOGSEVERITY_WARNING;
  settings.windowless_rendering_enabled = false;  // Use windowed rendering for visible UI

  // DO NOT set custom UserAgent - let CEF use its default
  // This ensures UserAgent matches actual navigator properties
  // Custom UA causes "Different browser name/version" detection

  // Set locale
  CefString(&settings.locale) = "en-US";

  // Set cache path for UI version
  CefString(&settings.cache_path) = "";  // Use temporary cache

  // Initialize logger for main UI process
  std::string log_file = "/tmp/owl_browser_ui_main.log";
  OlibLogger::Logger::Init(log_file);
  LOG_DEBUG("UI", "Logger initialized: " + log_file);

  // Initialize CEF
  if (!CefInitialize(main_args, settings, app.get(), nullptr)) {
    LOG_ERROR("UI", "Failed to initialize CEF");
    return 1;
  }

  // Set message loop mode to CefRunMessageLoop (UI mode) BEFORE Initialize
  OwlBrowserManager::SetUsesRunMessageLoop(true);

  // Initialize browser manager (starts LLM service)
  OwlBrowserManager::GetInstance()->Initialize();

  LOG_DEBUG("UI", "Owl Browser UI initialized successfully");

  // Create UI browser window with custom homepage
  CefRefPtr<OwlUIBrowser> ui_browser(new OwlUIBrowser);
  ui_browser->CreateBrowserWindow("owl://homepage.html");

  // Run message loop
  CefRunMessageLoop();

  // Shutdown
  OwlBrowserManager::GetInstance()->Shutdown();
  CefShutdown();

  LOG_DEBUG("UI", "Owl Browser UI shutdown complete");

  return 0;
}
