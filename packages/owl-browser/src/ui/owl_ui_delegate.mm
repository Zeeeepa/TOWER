// macOS-specific implementation using Objective-C++
#include "owl_ui_delegate.h"
#include "owl_ui_browser.h"
#include "owl_ui_toolbar.h"
#include "owl_agent_controller.h"
#include "owl_task_state.h"
#include "owl_proxy_manager.h"
#include "owl_demographics.h"
#include "owl_stealth.h"
#include "logger.h"
#include "../resources/icons/icons.h"

#if defined(OS_MACOS)

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "include/cef_application_mac.h"
#include "include/cef_values.h"
#include "include/cef_request_context.h"

// Helper: Create NSImage from SVG string (static to avoid duplicate symbol with toolbar)
static NSImage* CreateImageFromSVG(const std::string& svgString, NSSize size) {
  @autoreleasepool {
    NSString* svgNS = [NSString stringWithUTF8String:svgString.c_str()];
    NSData* svgData = [svgNS dataUsingEncoding:NSUTF8StringEncoding];

    // Use WebKit to render SVG to image
    NSImage* image = [[NSImage alloc] initWithSize:size];
    [image lockFocus];

    // Create a simple rendering context
    NSRect bounds = NSMakeRect(0, 0, size.width, size.height);

    // Parse and render SVG (simplified - for production, use proper SVG rendering)
    // For now, we'll use NSImage from data
    NSImage* svgImage = [[NSImage alloc] initWithData:svgData];
    if (svgImage) {
      [svgImage drawInRect:bounds];
    }

    [image unlockFocus];

    return image;
  }
}

// Forward declarations
@interface OwlUIWindowDelegate : NSObject<NSWindowDelegate> {
  BOOL _isClosing;
}
@property (nonatomic, assign) CefRefPtr<CefBrowser> browser;
@property (nonatomic, assign) OwlUIDelegate* uiDelegate;
@end

@implementation OwlUIWindowDelegate

- (instancetype)init {
  self = [super init];
  if (self) {
    _isClosing = NO;
  }
  return self;
}

- (BOOL)windowShouldClose:(NSWindow*)window {
  LOG_DEBUG("UIDelegate", "windowShouldClose called on main window, _isClosing=" + std::string(_isClosing ? "YES" : "NO"));

  // If AI prompt overlay is visible, hide it instead of closing browser
  if (_uiDelegate && _uiDelegate->IsAgentPromptVisible()) {
    LOG_DEBUG("UIDelegate", "Agent prompt is visible, hiding it instead of closing");
    _uiDelegate->HideAgentPrompt();
    return NO;
  }

  // If we're already closing, allow the window to close
  if (_isClosing) {
    LOG_DEBUG("UIDelegate", "Already closing, returning YES to close window");
    return YES;
  }

  // Request browser close with force=true to bypass any beforeunload handlers
  if (_browser) {
    LOG_DEBUG("UIDelegate", "Browser reference exists, calling CloseBrowser(true) with force");
    _isClosing = YES;
    _browser->GetHost()->CloseBrowser(true);  // Force close
  } else {
    LOG_ERROR("UIDelegate", "Browser reference is NULL! Cannot close browser.");
    return YES;  // Allow window to close if browser is gone
  }

  LOG_DEBUG("UIDelegate", "Returning NO to let CEF handle the close");
  return NO;  // Let CEF handle the close first
}

- (void)windowWillClose:(NSNotification*)notification {
  LOG_DEBUG("UIDelegate", "windowWillClose notification received");

  // Forcefully clean up any overlays
  if (_uiDelegate) {
    _uiDelegate->CleanupOverlays();
  }

  // Check remaining main browser count
  int remainingBrowsers = OwlUIBrowser::GetMainBrowserCount();
  LOG_DEBUG("UIDelegate", "Remaining main browsers after this close: " + std::to_string(remainingBrowsers));

  // Only quit if this is the last main browser window
  if (remainingBrowsers == 0) {
    LOG_DEBUG("UIDelegate", "Last main browser closing - quitting application");
    // Last main browser closing - quit application after a brief delay
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.1 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
      CefQuitMessageLoop();
    });
  } else {
    LOG_DEBUG("UIDelegate", "Other main browsers still open - not quitting application");
  }
}

- (void)windowDidResize:(NSNotification*)notification {
  // Reposition overlay and tasks panel when window resizes
  if (_uiDelegate) {
    _uiDelegate->RepositionOverlaysForResize();
  }
}

@end

// Objective-C helper for prompt callbacks (forward declaration)
@interface OlibPromptHelper : NSObject
@property (nonatomic, assign) OwlUIDelegate* delegate;
@property (nonatomic, assign) NSTextField* promptInput;
- (void)sendPrompt:(id)sender;
- (void)closeAgentPrompt:(id)sender;
- (void)toggleTasksList:(id)sender;
@end

// Objective-C helper for proxy overlay callbacks (forward declaration)
@interface OwlProxyHelper : NSObject
@property (nonatomic, assign) OwlUIDelegate* delegate;
@property (nonatomic, retain) NSTextField* caPathLabel;
- (void)saveProxySettings:(id)sender;
- (void)connectProxy:(id)sender;
- (void)closeProxyOverlay:(id)sender;
- (void)browseCACert:(id)sender;
- (void)clearCACert:(id)sender;
@end

// Singleton instance
OwlUIDelegate* OwlUIDelegate::instance_ = nullptr;

OwlUIDelegate* OwlUIDelegate::GetInstance() {
  if (!instance_) {
    instance_ = new OwlUIDelegate();
  }
  return instance_;
}

OwlUIDelegate::OwlUIDelegate()
    : main_window_(nullptr),
      content_view_(nullptr),
      window_delegate_(nullptr),
      toolbar_(nullptr),
      prompt_overlay_(nullptr),
      prompt_input_(nullptr),
      prompt_send_button_(nullptr),
      tasks_button_(nullptr),
      status_dot_(nullptr),
      tasks_panel_(nullptr),
      progress_border_(nullptr),
      prompt_helper_(nullptr),
      response_area_(nullptr),
      response_text_field_(nullptr),
      proxy_overlay_(nullptr),
      proxy_type_popup_(nullptr),
      proxy_host_input_(nullptr),
      proxy_port_input_(nullptr),
      proxy_username_input_(nullptr),
      proxy_password_input_(nullptr),
      proxy_timezone_input_(nullptr),
      proxy_stealth_checkbox_(nullptr),
      proxy_ca_checkbox_(nullptr),
      proxy_ca_path_label_(nullptr),
      proxy_ca_browse_button_(nullptr),
      proxy_tor_checkbox_(nullptr),
      proxy_tor_port_input_(nullptr),
      proxy_tor_password_input_(nullptr),
      proxy_save_button_(nullptr),
      proxy_connect_button_(nullptr),
      proxy_status_label_(nullptr),
      proxy_settings_saved_(false),
      proxy_helper_(nullptr),
      sidebar_visible_(true),
      agent_prompt_visible_(false),
      task_executing_(false),
      tasks_list_visible_(false),
      proxy_overlay_visible_(false),
      browser_handler_(nullptr) {
}

OwlUIDelegate::~OwlUIDelegate() {
  if (toolbar_) {
    delete toolbar_;
  }
  if (main_window_) {
    [main_window_ close];
    [main_window_ release];
  }
  if (window_delegate_) {
    [window_delegate_ release];
  }
}

void* OwlUIDelegate::CreateWindowWithToolbar(OwlUIBrowser* browser_handler, int width, int height) {
  @autoreleasepool {
    // Store browser handler reference
    browser_handler_ = browser_handler;

    // Create window
    NSRect frame = NSMakeRect(0, 0, width, height);
    NSUInteger styleMask = NSWindowStyleMaskTitled |
                          NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable |
                          NSWindowStyleMaskResizable;

    main_window_ = [[NSWindow alloc] initWithContentRect:frame
                                               styleMask:styleMask
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];

    [main_window_ setTitle:@"Owl Browser"];
    [main_window_ center];
    [main_window_ setMinSize:NSMakeSize(800, 600)];

    // Enable macOS native tabbing
    if (@available(macOS 10.12, *)) {
      [main_window_ setTabbingMode:NSWindowTabbingModePreferred];
      [main_window_ setTabbingIdentifier:@"OwlBrowserWindow"];
      LOG_DEBUG("UI", "Native macOS tabbing enabled");
    }

    // Create delegate
    window_delegate_ = [[OwlUIWindowDelegate alloc] init];
    window_delegate_.uiDelegate = this;
    [main_window_ setDelegate:window_delegate_];

    // Create native toolbar
    toolbar_ = new OwlUIToolbar();
    int toolbarHeight = toolbar_->GetHeight();

    // Set up toolbar callbacks
    toolbar_->SetBackCallback([browser_handler]() {
      browser_handler->GoBack();
    });

    toolbar_->SetForwardCallback([browser_handler]() {
      browser_handler->GoForward();
    });

    toolbar_->SetReloadCallback([browser_handler]() {
      browser_handler->Reload();
    });

    toolbar_->SetStopLoadingCallback([browser_handler]() {
      browser_handler->StopLoading();
    });

    toolbar_->SetHomeCallback([browser_handler]() {
      browser_handler->Navigate("owl://homepage.html");
    });

    toolbar_->SetNavigateCallback([browser_handler](const std::string& url) {
      browser_handler->Navigate(url);
    });

    toolbar_->SetAgentToggleCallback([browser_handler]() {
      browser_handler->ToggleAgentMode();
    });

    toolbar_->SetNewTabCallback([]() {
      OwlUIDelegate* delegate = OwlUIDelegate::GetInstance();
      if (delegate) {
        delegate->NewTab();
      }
    });

    toolbar_->SetProxyToggleCallback([]() {
      OwlUIDelegate* delegate = OwlUIDelegate::GetInstance();
      if (delegate) {
        delegate->ToggleProxyOverlay();
      }
    });

    // Create toolbar view
    NSView* toolbarView = (__bridge NSView*)toolbar_->CreateToolbarView(width, toolbarHeight);

    // Create content view for browser (below toolbar)
    content_view_ = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, width, height - toolbarHeight)];
    [content_view_ setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    // Add views to window's content view
    NSView* windowContentView = [main_window_ contentView];
    [windowContentView addSubview:content_view_];
    [windowContentView addSubview:toolbarView];

    // Position toolbar at top
    [toolbarView setFrame:NSMakeRect(0, height - toolbarHeight, width, toolbarHeight)];
    [toolbarView setAutoresizingMask:(NSViewWidthSizable | NSViewMinYMargin)];

    // Position content view below toolbar
    [content_view_ setFrame:NSMakeRect(0, 0, width, height - toolbarHeight)];

    LOG_DEBUG("UI", "Window with native toolbar created successfully");

    // Return content view handle for CEF to use as parent
    return (__bridge void*)content_view_;
  }
}

void* OwlUIDelegate::CreateWindow(OwlUIBrowser* browser_handler, int width, int height) {
  @autoreleasepool {
    // Store browser handler reference
    browser_handler_ = browser_handler;

    // Create window
    NSRect frame = NSMakeRect(0, 0, width, height);
    NSUInteger styleMask = NSWindowStyleMaskTitled |
                          NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable |
                          NSWindowStyleMaskResizable;

    main_window_ = [[NSWindow alloc] initWithContentRect:frame
                                               styleMask:styleMask
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];

    [main_window_ setTitle:@"Developer Playground"];
    [main_window_ center];
    [main_window_ setMinSize:NSMakeSize(800, 600)];

    // Create delegate
    window_delegate_ = [[OwlUIWindowDelegate alloc] init];
    window_delegate_.uiDelegate = this;
    [main_window_ setDelegate:window_delegate_];

    // No toolbar for playground windows
    toolbar_ = nullptr;

    // Create content view (full window, no toolbar)
    content_view_ = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
    [content_view_ setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    // Add view to window's content view
    NSView* windowContentView = [main_window_ contentView];
    [windowContentView addSubview:content_view_];

    LOG_DEBUG("UI", "Window without toolbar created successfully");

    // Return content view handle for CEF to use as parent
    return (__bridge void*)content_view_;
  }
}

void OwlUIDelegate::FocusWindow() {
  @autoreleasepool {
    if (main_window_) {
      [main_window_ makeKeyAndOrderFront:nil];
      [NSApp activateIgnoringOtherApps:YES];
      LOG_DEBUG("UI", "Window focused and brought to front");
    }
  }
}

void OwlUIDelegate::SetBrowser(CefRefPtr<CefBrowser> browser) {
  @autoreleasepool {
    if (window_delegate_) {
      [window_delegate_ setBrowser:browser];
      LOG_DEBUG("UI", "Browser reference set in window delegate");
    }
  }
}

void OwlUIDelegate::ShowWindow() {
  if (main_window_) {
    [main_window_ makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
  }
}

void OwlUIDelegate::HideWindow() {
  if (main_window_) {
    [main_window_ orderOut:nil];
  }
}

void OwlUIDelegate::CloseWindow() {
  if (main_window_) {
    [main_window_ close];
  }
}

void OwlUIDelegate::SetWindowTitle(const std::string& title) {
  if (main_window_) {
    [main_window_ setTitle:[NSString stringWithUTF8String:title.c_str()]];
  }
}

void OwlUIDelegate::NewTab(const std::string& url) {
  @autoreleasepool {
    LOG_DEBUG("UI", "Creating new tab with URL: " + url);

    // Create a new browser instance
    CefRefPtr<OwlUIBrowser> new_browser(new OwlUIBrowser);

    // Create a new window (will automatically become a tab due to tabbingIdentifier)
    new_browser->CreateBrowserWindow(url);

    LOG_DEBUG("UI", "New tab created");
  }
}

void OwlUIDelegate::ShowSidebar() {
  sidebar_visible_ = true;
  // Sidebar is implemented via injected HTML/CSS in the browser content
  LOG_DEBUG("UI", "Sidebar shown");
}

void OwlUIDelegate::HideSidebar() {
  sidebar_visible_ = false;
  LOG_DEBUG("UI", "Sidebar hidden");
}

void OwlUIDelegate::ToggleSidebar() {
  if (sidebar_visible_) {
    HideSidebar();
  } else {
    ShowSidebar();
  }
}

void OwlUIDelegate::ShowAgentPrompt() {
  @autoreleasepool {
    agent_prompt_visible_ = true;

    if (!prompt_overlay_ && main_window_) {
      LOG_DEBUG("UI", "Creating AI prompt overlay matching homepage design");

      // Create overlay panel with homepage dimensions
      NSRect windowFrame = [main_window_ contentView].frame;
      CGFloat overlayHeight = 80;  // Compact height matching homepage
      CGFloat overlayWidth = 700;   // Max width from homepage
      CGFloat overlayX = (windowFrame.size.width - overlayWidth) / 2;
      CGFloat overlayY = 40;  // Lower position, closer to bottom

      prompt_overlay_ = [[NSView alloc] initWithFrame:NSMakeRect(overlayX, overlayY, overlayWidth, overlayHeight)];
      [prompt_overlay_ setWantsLayer:YES];
      [prompt_overlay_ retain];  // Retain it

      // White background matching homepage .ai-command-box
      prompt_overlay_.layer.backgroundColor = [[NSColor whiteColor] CGColor];
      prompt_overlay_.layer.cornerRadius = 28.0;  // Match homepage border-radius: 28px
      prompt_overlay_.layer.borderWidth = 2.0;
      prompt_overlay_.layer.borderColor = [[NSColor colorWithRed:0.2 green:0.2 blue:0.2 alpha:0.15] CGColor];  // Darker subtle border

      // Shadow matching homepage: box-shadow: 0 4px 24px rgba(66, 133, 244, 0.2)
      prompt_overlay_.layer.shadowColor = [[NSColor colorWithRed:0.259 green:0.522 blue:0.957 alpha:1.0] CGColor];  // #4285f4
      prompt_overlay_.layer.shadowOpacity = 0.2;
      prompt_overlay_.layer.shadowOffset = NSMakeSize(0, -4);
      prompt_overlay_.layer.shadowRadius = 24.0;

      // No icon - removed to match clean design

      // Create and retain callback helper
      if (!prompt_helper_) {
        OlibPromptHelper* helper = [[OlibPromptHelper alloc] init];
        helper.delegate = this;
        [helper retain];  // Manual retain since not using ARC
        prompt_helper_ = (__bridge void*)helper;
        LOG_DEBUG("UI", "Created and retained prompt helper");
      }

      OlibPromptHelper* helper = (__bridge OlibPromptHelper*)prompt_helper_;

      // Tasks button with status dot on the left side
      CGFloat tasksButtonSize = 40;
      CGFloat tasksButtonX = 20;
      CGFloat tasksButtonY = (overlayHeight - tasksButtonSize) / 2;

      tasks_button_ = [[NSButton alloc] initWithFrame:NSMakeRect(tasksButtonX, tasksButtonY, tasksButtonSize, tasksButtonSize)];
      [tasks_button_ setButtonType:NSButtonTypeMomentaryChange];
      [tasks_button_ setBordered:NO];
      [tasks_button_ setWantsLayer:YES];

      // Light gray background
      tasks_button_.layer.backgroundColor = [[NSColor colorWithRed:0.95 green:0.95 blue:0.95 alpha:1.0] CGColor];
      tasks_button_.layer.cornerRadius = tasksButtonSize / 2;  // Circular

      // Use FA BARS icon instead of text hamburger menu
      NSImage* barsIcon = CreateImageFromSVG(OlibIcons::BARS, NSMakeSize(18, 18));
      if (barsIcon) {
        [barsIcon setTemplate:YES];  // Use template mode for tinting
        [tasks_button_ setImage:barsIcon];
        [tasks_button_ setContentTintColor:[NSColor colorWithRed:0.4 green:0.4 blue:0.4 alpha:1.0]];
      }

      [tasks_button_ setTarget:helper];
      [tasks_button_ setAction:@selector(toggleTasksList:)];
      [prompt_overlay_ addSubview:tasks_button_];

      // Status dot - small colored circle in top-right of tasks button
      CGFloat dotSize = 10;
      CGFloat dotX = tasksButtonX + tasksButtonSize - dotSize - 2;
      CGFloat dotY = tasksButtonY + tasksButtonSize - dotSize - 2;

      status_dot_ = [[NSView alloc] initWithFrame:NSMakeRect(dotX, dotY, dotSize, dotSize)];
      [status_dot_ setWantsLayer:YES];
      status_dot_.layer.cornerRadius = dotSize / 2;  // Circular
      status_dot_.layer.backgroundColor = [[NSColor grayColor] CGColor];  // Default gray (IDLE)
      status_dot_.layer.borderWidth = 2.0;
      status_dot_.layer.borderColor = [[NSColor whiteColor] CGColor];  // White border to stand out
      [prompt_overlay_ addSubview:status_dot_];

      LOG_DEBUG("UI", "Created tasks button with status dot");

      // Optional: Close button removed to match homepage (homepage doesn't have close button in the input box)
      // User can press ESC to close instead

      // Input field matching homepage #ai-command-input (no container, directly in overlay)
      CGFloat inputX = tasksButtonX + tasksButtonSize + 12;  // After tasks button + spacing
      CGFloat buttonWidth = 100;
      CGFloat buttonHeight = 40;
      CGFloat inputHeight = 36;  // Slightly taller for better visual alignment
      CGFloat inputWidth = overlayWidth - inputX - buttonWidth - 12 - 24;  // Leave space for button + spacing + right margin

      // Align input field center with button center for perfect vertical alignment
      CGFloat buttonY = (overlayHeight - buttonHeight) / 2;
      CGFloat inputY = buttonY + (buttonHeight - inputHeight) / 2 - 6;  // Center input within button's vertical space, -6px offset for text baseline

      prompt_input_ = [[NSTextField alloc] initWithFrame:NSMakeRect(inputX, inputY, inputWidth, inputHeight)];
      [prompt_input_ setBezeled:NO];
      [prompt_input_ setDrawsBackground:NO];
      [prompt_input_ setFocusRingType:NSFocusRingTypeNone];
      [prompt_input_ setFont:[NSFont systemFontOfSize:16]];  // Match homepage font-size: 16px
      [prompt_input_ setTextColor:[NSColor colorWithRed:0.125 green:0.129 blue:0.141 alpha:1.0]];  // #202124
      [prompt_input_ setEditable:YES];
      [prompt_input_ setSelectable:YES];
      [prompt_input_ setAlignment:NSTextAlignmentLeft];  // Explicitly set left alignment
      [[prompt_input_ cell] setLineBreakMode:NSLineBreakByTruncatingTail];
      [[prompt_input_ cell] setScrollable:YES];
      [[prompt_input_ cell] setUsesSingleLineMode:YES];  // Ensure single line mode

      // Placeholder matching homepage (color: #9aa0a6)
      NSMutableAttributedString* placeholder = [[NSMutableAttributedString alloc] initWithString:@"Tell me what to do... (e.g., 'go to google.com and search for banana')"];
      [placeholder addAttribute:NSForegroundColorAttributeName value:[NSColor colorWithRed:0.604 green:0.627 blue:0.651 alpha:1.0] range:NSMakeRange(0, placeholder.length)];  // #9aa0a6
      [placeholder addAttribute:NSFontAttributeName value:[NSFont systemFontOfSize:16] range:NSMakeRange(0, placeholder.length)];
      [[prompt_input_ cell] setPlaceholderAttributedString:placeholder];

      [prompt_input_ retain];  // Retain it
      helper.promptInput = prompt_input_;
      [prompt_input_ setTarget:helper];
      [prompt_input_ setAction:@selector(sendPrompt:)];
      [prompt_overlay_ addSubview:prompt_input_];

      LOG_DEBUG("UI", "Created input field matching homepage");

      // "Go" button matching homepage .go-button (buttonHeight already defined above)
      prompt_send_button_ = [[NSButton alloc] initWithFrame:NSMakeRect(overlayWidth - buttonWidth - 24, buttonY, buttonWidth, buttonHeight)];
      [prompt_send_button_ setButtonType:NSButtonTypeMomentaryChange];
      [prompt_send_button_ setBordered:NO];
      [prompt_send_button_ setWantsLayer:YES];

      // Solid blue background matching homepage (background: #4285f4)
      prompt_send_button_.layer.backgroundColor = [[NSColor colorWithRed:0.259 green:0.522 blue:0.957 alpha:1.0] CGColor];  // #4285f4
      prompt_send_button_.layer.cornerRadius = 20.0;  // Match homepage border-radius: 20px

      // Button title "Go" matching homepage
      NSMutableAttributedString* btnTitle = [[NSMutableAttributedString alloc] initWithString:@"Go"];
      [btnTitle addAttribute:NSFontAttributeName value:[NSFont systemFontOfSize:15 weight:NSFontWeightSemibold] range:NSMakeRange(0, btnTitle.length)];  // font-weight: 600
      [btnTitle addAttribute:NSForegroundColorAttributeName value:[NSColor whiteColor] range:NSMakeRange(0, btnTitle.length)];
      [prompt_send_button_ setAttributedTitle:btnTitle];

      [prompt_send_button_ setTarget:helper];
      [prompt_send_button_ setAction:@selector(sendPrompt:)];
      [prompt_overlay_ addSubview:prompt_send_button_];

      // Create animated progress border at the bottom (hidden by default)
      CGFloat borderHeight = 3;
      progress_border_ = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, overlayWidth, borderHeight)];
      [progress_border_ setWantsLayer:YES];
      progress_border_.layer.backgroundColor = [[NSColor clearColor] CGColor];

      // Create dashed line using CAShapeLayer
      CAShapeLayer* dashLayer = [CAShapeLayer layer];
      CGMutablePathRef path = CGPathCreateMutable();
      CGPathMoveToPoint(path, NULL, 0, borderHeight/2);
      CGPathAddLineToPoint(path, NULL, overlayWidth, borderHeight/2);
      dashLayer.path = path;
      CGPathRelease(path);

      dashLayer.strokeColor = [[NSColor colorWithRed:0.259 green:0.522 blue:0.957 alpha:1.0] CGColor];  // Blue matching button
      dashLayer.lineWidth = borderHeight;
      dashLayer.lineCap = kCALineCapRound;
      dashLayer.lineDashPattern = @[@8, @8];  // Dash pattern: 8px dash, 8px gap
      dashLayer.name = @"dashLayer";

      [progress_border_.layer addSublayer:dashLayer];
      [progress_border_ setHidden:YES];  // Hidden by default
      [prompt_overlay_ addSubview:progress_border_];

      LOG_DEBUG("UI", "Adding overlay to window");

      // Add to window with smooth fade-in animation
      [prompt_overlay_ setAlphaValue:0.0];
      [[main_window_ contentView] addSubview:prompt_overlay_ positioned:NSWindowAbove relativeTo:nil];

      [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = 0.25;
        context.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseOut];
        [prompt_overlay_ setAlphaValue:1.0];
      } completionHandler:^{
        [main_window_ makeFirstResponder:prompt_input_];
        UpdateTaskStatusDot();  // Update status dot on first show
        LOG_DEBUG("UI", "Overlay shown with animation");
      }];

    } else if (prompt_overlay_) {
      // Already exists, reset alpha and re-show with animation
      [prompt_overlay_ setAlphaValue:0.0];
      [[main_window_ contentView] addSubview:prompt_overlay_ positioned:NSWindowAbove relativeTo:nil];

      [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = 0.25;
        context.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseOut];
        [prompt_overlay_ setAlphaValue:1.0];
      } completionHandler:^{
        // Clear the input field AFTER animation completes, when view is stable
        if (prompt_input_) {
          NSMutableAttributedString* emptyStr = [[NSMutableAttributedString alloc] initWithString:@""];
          [prompt_input_ setAttributedStringValue:emptyStr];
        }
        [main_window_ makeFirstResponder:prompt_input_];
        UpdateTaskStatusDot();  // Update status dot on re-show
        LOG_DEBUG("UI", "Showed existing overlay with animation");
      }];
    }

    LOG_DEBUG("UI", "Agent prompt shown");
  }
}

void OwlUIDelegate::HideAgentPrompt() {
  @autoreleasepool {
    agent_prompt_visible_ = false;

    if (prompt_overlay_) {
      // CRITICAL: Remove focus from text field FIRST to stop cursor blinking animation
      if (main_window_) {
        [main_window_ makeFirstResponder:nil];
      }

      // Cancel any pending animations and remove immediately
      [NSAnimationContext beginGrouping];
      [[NSAnimationContext currentContext] setDuration:0];
      [prompt_overlay_ setAlphaValue:0.0];
      [NSAnimationContext endGrouping];

      // Remove from superview immediately
      [prompt_overlay_ removeFromSuperview];

      LOG_DEBUG("UI", "Agent prompt hidden and removed");
    }
  }
}

void OwlUIDelegate::CleanupOverlays() {
  @autoreleasepool {
    LOG_DEBUG("UI", "Cleaning up all overlays");

    // Hide agent prompt overlay
    if (prompt_overlay_) {
      [prompt_overlay_ removeFromSuperview];
      prompt_overlay_ = nullptr;
    }

    // Hide tasks panel
    if (tasks_panel_) {
      [tasks_panel_ removeFromSuperview];
      tasks_panel_ = nullptr;
    }

    // Hide response area
    if (response_area_) {
      [response_area_ removeFromSuperview];
      response_area_ = nullptr;
    }

    // Hide progress border
    if (progress_border_) {
      [progress_border_ removeFromSuperview];
      progress_border_ = nullptr;
    }

    // Hide proxy overlay
    if (proxy_overlay_) {
      [proxy_overlay_ removeFromSuperview];
      proxy_overlay_ = nullptr;
    }

    agent_prompt_visible_ = false;
    tasks_list_visible_ = false;
    proxy_overlay_visible_ = false;

    LOG_DEBUG("UI", "All overlays cleaned up");
  }
}

// Implementation of prompt callbacks helper
@implementation OlibPromptHelper

- (void)sendPrompt:(id)sender {
  LOG_DEBUG("UI", "sendPrompt called");

  if (!_delegate) {
    LOG_ERROR("UI", "_delegate is null!");
    return;
  }

  // Check if task is executing - if so, this is a stop request
  if (OwlAgentController::GetInstance()->IsExecuting()) {
    LOG_DEBUG("UI", "Stop button clicked - stopping execution");
    _delegate->StopExecution();
    return;
  }

  // Otherwise, this is a Go request
  if (!_promptInput) {
    LOG_ERROR("UI", "_promptInput is null!");
    return;
  }

  NSString* promptText = [_promptInput stringValue];
  LOG_DEBUG("UI", "Got prompt text");

  if (!promptText || [promptText length] == 0) {
    LOG_ERROR("UI", "Prompt text is empty!");
    return;
  }

  std::string prompt = std::string([promptText UTF8String]);
  LOG_DEBUG("UI", "Sending prompt to NLA: " + prompt);

  // DO NOT hide overlay - keep it visible to show task progress
  // Clear the input field
  [_promptInput setStringValue:@""];

  // Set executing state BEFORE executing
  _delegate->SetTaskExecuting(true);

  // Execute the prompt via browser handler
  LOG_DEBUG("UI", "Calling ExecutePrompt...");
  _delegate->ExecutePrompt(prompt);
  LOG_DEBUG("UI", "ExecutePrompt returned");
}

- (void)closeAgentPrompt:(id)sender {
  if (_delegate) {
    _delegate->HideAgentPrompt();
  }
}

- (void)toggleTasksList:(id)sender {
  LOG_DEBUG("UI", "toggleTasksList called");
  if (_delegate) {
    _delegate->ToggleTasksList();
  }
}

- (void)closeResponseArea:(id)sender {
  LOG_DEBUG("UI", "closeResponseArea called");
  if (_delegate) {
    _delegate->HideResponseArea();
  }
}

@end

// Implementation of proxy helper callbacks
@implementation OwlProxyHelper

- (void)saveProxySettings:(id)sender {
  LOG_DEBUG("UI", "saveProxySettings called");
  if (_delegate) {
    _delegate->SaveProxySettings();
  }
}

- (void)connectProxy:(id)sender {
  LOG_DEBUG("UI", "connectProxy called");
  if (_delegate) {
    // Check if we're connected - if so, disconnect; otherwise connect
    OwlProxyManager* proxy_manager = OwlProxyManager::GetInstance();
    if (proxy_manager->GetStatus() == ProxyStatus::CONNECTED) {
      _delegate->DisconnectProxy();
    } else {
      _delegate->ConnectProxy();
    }
  }
}

- (void)closeProxyOverlay:(id)sender {
  LOG_DEBUG("UI", "closeProxyOverlay called");
  if (_delegate) {
    _delegate->HideProxyOverlay();
  }
}

- (void)browseCACert:(id)sender {
  LOG_DEBUG("UI", "browseCACert called");

  NSOpenPanel* panel = [NSOpenPanel openPanel];
  [panel setCanChooseFiles:YES];
  [panel setCanChooseDirectories:NO];
  [panel setAllowsMultipleSelection:NO];
  [panel setTitle:@"Select CA Certificate"];
  [panel setMessage:@"Choose a CA certificate file (.pem, .crt, .cer)"];

  // Use modern allowedContentTypes API (macOS 11.0+)
  if (@available(macOS 11.0, *)) {
    // Create UTTypes for certificate file extensions
    UTType* pemType = [UTType typeWithFilenameExtension:@"pem"];
    UTType* crtType = [UTType typeWithFilenameExtension:@"crt"];
    UTType* cerType = [UTType typeWithFilenameExtension:@"cer"];
    UTType* derType = [UTType typeWithFilenameExtension:@"der"];

    NSMutableArray<UTType*>* types = [NSMutableArray array];
    if (pemType) [types addObject:pemType];
    if (crtType) [types addObject:crtType];
    if (cerType) [types addObject:cerType];
    if (derType) [types addObject:derType];

    [panel setAllowedContentTypes:types];
  }

  if ([panel runModal] == NSModalResponseOK) {
    NSURL* url = [[panel URLs] firstObject];
    if (url) {
      NSString* path = [url path];
      LOG_DEBUG("UI", "Selected CA cert: " + std::string([path UTF8String]));

      // Update the path label
      if (_caPathLabel) {
        // Truncate path for display if too long
        NSString* displayPath = path;
        if ([path length] > 40) {
          displayPath = [NSString stringWithFormat:@"...%@", [path substringFromIndex:[path length] - 37]];
        }
        [_caPathLabel setStringValue:displayPath];
        [_caPathLabel setToolTip:path];  // Full path on hover
        [_caPathLabel setTextColor:[NSColor colorWithWhite:0.2 alpha:1.0]];
      }
    }
  }
}

- (void)clearCACert:(id)sender {
  LOG_DEBUG("UI", "clearCACert called");
  if (_caPathLabel) {
    [_caPathLabel setStringValue:@"No certificate selected"];
    [_caPathLabel setToolTip:@""];
    [_caPathLabel setTextColor:[NSColor colorWithWhite:0.5 alpha:1.0]];
  }
}

@end

void OwlUIDelegate::ExecutePrompt(const std::string& prompt) {
  LOG_DEBUG("UI", "ExecutePrompt called with prompt: " + prompt);

  if (!browser_handler_) {
    LOG_ERROR("UI", "browser_handler_ is null!");
    return;
  }

  if (prompt.empty()) {
    LOG_ERROR("UI", "prompt is empty!");
    return;
  }

  LOG_DEBUG("UI", "Calling browser_handler_->ExecuteAgentPrompt...");
  browser_handler_->ExecuteAgentPrompt(prompt);
  LOG_DEBUG("UI", "browser_handler_->ExecuteAgentPrompt returned");
}

void OwlUIDelegate::UpdateAgentStatus(const std::string& status) {
  LOG_DEBUG("UI", "Agent status: " + status);
}

void OwlUIDelegate::SetTaskExecuting(bool executing) {
  @autoreleasepool {
    task_executing_ = executing;

    if (!progress_border_ || !prompt_send_button_ || !prompt_input_) return;

    if (executing) {
      // Show the progress border with marching ants animation
      [progress_border_ setHidden:NO];

      // Find the dash layer and animate it
      for (CALayer* sublayer in progress_border_.layer.sublayers) {
        if ([sublayer.name isEqualToString:@"dashLayer"]) {
          CAShapeLayer* dashLayer = (CAShapeLayer*)sublayer;

          // Animate the dash phase to create marching ants effect
          CABasicAnimation* dashAnimation = [CABasicAnimation animationWithKeyPath:@"lineDashPhase"];
          dashAnimation.fromValue = @0;
          dashAnimation.toValue = @16;  // Sum of dash pattern (8 + 8)
          dashAnimation.duration = 0.5;
          dashAnimation.repeatCount = HUGE_VALF;
          [dashLayer addAnimation:dashAnimation forKey:@"dashAnimation"];
          break;
        }
      }

      // Change button to red "Stop"
      prompt_send_button_.layer.backgroundColor = [[NSColor colorWithRed:0.9 green:0.2 blue:0.2 alpha:1.0] CGColor];
      NSMutableAttributedString* btnTitle = [[NSMutableAttributedString alloc] initWithString:@"Stop"];
      [btnTitle addAttribute:NSFontAttributeName value:[NSFont systemFontOfSize:15 weight:NSFontWeightSemibold] range:NSMakeRange(0, btnTitle.length)];
      [btnTitle addAttribute:NSForegroundColorAttributeName value:[NSColor whiteColor] range:NSMakeRange(0, btnTitle.length)];
      [prompt_send_button_ setAttributedTitle:btnTitle];

      // Disable input
      [prompt_input_ setEnabled:NO];
      [prompt_input_ setTextColor:[NSColor grayColor]];
    } else {
      // Hide the progress border and stop animation
      for (CALayer* sublayer in progress_border_.layer.sublayers) {
        if ([sublayer.name isEqualToString:@"dashLayer"]) {
          [sublayer removeAllAnimations];
          break;
        }
      }
      [progress_border_ setHidden:YES];

      // Change button back to blue "Go"
      prompt_send_button_.layer.backgroundColor = [[NSColor colorWithRed:0.259 green:0.522 blue:0.957 alpha:1.0] CGColor];
      NSMutableAttributedString* btnTitle = [[NSMutableAttributedString alloc] initWithString:@"Go"];
      [btnTitle addAttribute:NSFontAttributeName value:[NSFont systemFontOfSize:15 weight:NSFontWeightSemibold] range:NSMakeRange(0, btnTitle.length)];
      [btnTitle addAttribute:NSForegroundColorAttributeName value:[NSColor whiteColor] range:NSMakeRange(0, btnTitle.length)];
      [prompt_send_button_ setAttributedTitle:btnTitle];

      // Enable input
      [prompt_input_ setEnabled:YES];
      [prompt_input_ setTextColor:[NSColor colorWithRed:0.125 green:0.129 blue:0.141 alpha:1.0]];
    }

    // Update status dot color to reflect current state
    UpdateTaskStatusDot();

    LOG_DEBUG("UI", executing ? "Task executing - button set to Stop (red), progress border shown" : "Task finished - button set to Go (blue), progress border hidden");
  }
}

void OwlUIDelegate::StopExecution() {
  LOG_DEBUG("UI", "StopExecution called");

  // Call agent controller to stop execution
  OwlAgentController::GetInstance()->StopExecution();

  // Clear all tasks from task manager
  OwlTaskState::GetInstance()->Clear();

  // Update tasks list display
  UpdateTasksList();

  // NOTE: Do NOT call SetTaskExecuting(false) here
  // The status callback in owl_ui_browser.cc will call it when the agent
  // controller's state changes to IDLE, ensuring the UI updates only after
  // execution has fully stopped.

  LOG_DEBUG("UI", "Execution stop requested, tasks cleared");
}

void OwlUIDelegate::UpdateTaskStatusDot() {
  @autoreleasepool {
    if (!status_dot_) return;

    // Always show tasks button - don't hide it
    // (Removed visibility toggling based on task count)

    // Get current task state from agent controller
    auto status = OwlAgentController::GetInstance()->GetStatus();

    // Map agent state to color
    NSColor* dotColor = nil;
    switch (status.state) {
      case OwlAgentController::AgentState::IDLE:
        dotColor = [NSColor colorWithRed:0.6 green:0.6 blue:0.6 alpha:1.0];  // Gray
        break;
      case OwlAgentController::AgentState::PLANNING:
        dotColor = [NSColor colorWithRed:1.0 green:0.8 blue:0.0 alpha:1.0];  // Yellow/Orange
        break;
      case OwlAgentController::AgentState::EXECUTING:
        dotColor = [NSColor colorWithRed:0.259 green:0.522 blue:0.957 alpha:1.0];  // Blue (#4285f4)
        break;
      case OwlAgentController::AgentState::WAITING_FOR_USER:
        dotColor = [NSColor colorWithRed:0.8 green:0.4 blue:1.0 alpha:1.0];  // Purple
        break;
      case OwlAgentController::AgentState::COMPLETED:
        dotColor = [NSColor colorWithRed:0.2 green:0.8 blue:0.2 alpha:1.0];  // Green
        break;
      case OwlAgentController::AgentState::ERROR:
        dotColor = [NSColor colorWithRed:1.0 green:0.2 blue:0.2 alpha:1.0];  // Red
        break;
    }

    if (dotColor) {
      status_dot_.layer.backgroundColor = [dotColor CGColor];
    }

    // Add pulsing/spinning animation for active states (PLANNING, EXECUTING)
    [status_dot_.layer removeAllAnimations];  // Remove any existing animations first

    if (status.state == OwlAgentController::AgentState::PLANNING ||
        status.state == OwlAgentController::AgentState::EXECUTING) {
      // Create a pulsing scale animation for visual feedback
      CABasicAnimation* pulseAnimation = [CABasicAnimation animationWithKeyPath:@"transform.scale"];
      pulseAnimation.fromValue = @1.0;
      pulseAnimation.toValue = @1.3;
      pulseAnimation.duration = 0.8;
      pulseAnimation.autoreverses = YES;
      pulseAnimation.repeatCount = HUGE_VALF;  // Repeat forever
      pulseAnimation.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];

      [status_dot_.layer addAnimation:pulseAnimation forKey:@"pulse"];

      LOG_DEBUG("UI", "Started pulsing animation for status dot");
    } else {
      // For non-active states, ensure dot is at normal scale
      status_dot_.layer.transform = CATransform3DIdentity;
    }

    LOG_DEBUG("UI", "Updated status dot color based on agent state");
  }
}

void OwlUIDelegate::UpdateTasksList() {
  // If panel is visible, update it
  if (tasks_list_visible_ && tasks_panel_) {
    @autoreleasepool {
      // Get tasks from OwlTaskState
      OwlTaskState* task_state = OwlTaskState::GetInstance();
      std::vector<TaskInfo> tasks = task_state->GetTasks();

      // Remove all subviews
      NSArray* subviews = [tasks_panel_ subviews];
      for (NSView* view in subviews) {
        [view removeFromSuperview];
      }

      // Recreate content with all tasks
      CGFloat panelWidth = [tasks_panel_ frame].size.width;
      CGFloat panelHeight = [tasks_panel_ frame].size.height;

      // Title
      NSTextField* title = [[NSTextField alloc] initWithFrame:NSMakeRect(20, panelHeight - 50, panelWidth - 40, 30)];
      [title setBezeled:NO];
      [title setDrawsBackground:NO];
      [title setEditable:NO];
      [title setSelectable:NO];
      [title setFont:[NSFont systemFontOfSize:16 weight:NSFontWeightSemibold]];
      [title setTextColor:[NSColor colorWithRed:0.125 green:0.129 blue:0.141 alpha:1.0]];
      [title setStringValue:@"Tasks"];
      [tasks_panel_ addSubview:title];

      // Create scrollable content area for tasks
      CGFloat yOffset = panelHeight - 70;
      CGFloat taskItemHeight = 40;
      CGFloat leftMargin = 20;
      CGFloat dotSize = 12;

      if (tasks.empty()) {
        // Show "No tasks" message
        NSTextField* noTasksField = [[NSTextField alloc] initWithFrame:NSMakeRect(leftMargin, yOffset - 30, panelWidth - 40, 30)];
        [noTasksField setBezeled:NO];
        [noTasksField setDrawsBackground:NO];
        [noTasksField setEditable:NO];
        [noTasksField setSelectable:NO];
        [noTasksField setFont:[NSFont systemFontOfSize:14]];
        [noTasksField setTextColor:[NSColor colorWithRed:0.6 green:0.6 blue:0.6 alpha:1.0]];
        [noTasksField setStringValue:@"No tasks"];
        [tasks_panel_ addSubview:noTasksField];
      } else {
        // Display each task
        for (size_t i = 0; i < tasks.size(); i++) {
          const TaskInfo& task = tasks[i];

          // Log task status for debugging with raw enum value
          std::string statusStr;
          int statusValue = static_cast<int>(task.status);
          (void)statusValue;  // Used in LOG_DEBUG below
          switch (task.status) {
            case TaskStatus::PENDING: statusStr = "PENDING"; break;
            case TaskStatus::ACTIVE: statusStr = "ACTIVE"; break;
            case TaskStatus::COMPLETED: statusStr = "COMPLETED"; break;
            case TaskStatus::FAILED: statusStr = "FAILED"; break;
            default: statusStr = "UNKNOWN"; break;
          }
          LOG_DEBUG("UI", "Task[" + std::to_string(i) + "] '" + task.description + "' - Status: " + statusStr + " (value=" + std::to_string(statusValue) + ")");

          // Status icon based on task status - using FontAwesome icons
          std::string iconSVG;
          NSColor* iconColor = nil;
          std::string iconName;
          switch (task.status) {
            case TaskStatus::PENDING:
              iconSVG = OlibIcons::CIRCLE;
              iconColor = [NSColor colorWithRed:0.6 green:0.6 blue:0.6 alpha:1.0];  // Gray
              iconName = "CIRCLE (Gray)";
              break;
            case TaskStatus::ACTIVE:
              iconSVG = OlibIcons::ARROWS_ROTATE;
              iconColor = [NSColor colorWithRed:0.259 green:0.522 blue:0.957 alpha:1.0];  // Blue (#4285f4)
              iconName = "ARROWS_ROTATE (Blue)";
              break;
            case TaskStatus::COMPLETED:
              iconSVG = OlibIcons::CHECK;
              iconColor = [NSColor colorWithRed:0.2 green:0.8 blue:0.2 alpha:1.0];  // Green
              iconName = "CHECK (Green)";
              break;
            case TaskStatus::FAILED:
              iconSVG = OlibIcons::XMARK;
              iconColor = [NSColor colorWithRed:1.0 green:0.2 blue:0.2 alpha:1.0];  // Red
              iconName = "XMARK (Red)";
              break;
            default:
              iconSVG = OlibIcons::CIRCLE;
              iconColor = [NSColor grayColor];
              iconName = "CIRCLE (DefaultGray)";
              break;
          }
          LOG_DEBUG("UI", "  -> Setting icon to: " + iconName);

          // Capitalize first letter of task description
          std::string capitalizedDesc = task.description;
          if (!capitalizedDesc.empty()) {
            capitalizedDesc[0] = std::toupper(capitalizedDesc[0]);
          }

          // Create a container view for this task item
          NSView* taskRow = [[NSView alloc] initWithFrame:NSMakeRect(leftMargin, yOffset - taskItemHeight, panelWidth - 2*leftMargin, taskItemHeight)];

          // Task description - positioned to the right of the dot
          NSTextField* taskField = [[NSTextField alloc] initWithFrame:NSMakeRect(dotSize + 10, 0, panelWidth - 2*leftMargin - dotSize - 10, taskItemHeight)];
          [taskField setBezeled:NO];
          [taskField setDrawsBackground:NO];
          [taskField setEditable:NO];
          [taskField setSelectable:NO];
          [taskField setFont:[NSFont systemFontOfSize:13]];
          [taskField setTextColor:[NSColor colorWithRed:0.2 green:0.2 blue:0.2 alpha:1.0]];
          [taskField setStringValue:[NSString stringWithUTF8String:capitalizedDesc.c_str()]];
          [taskField setLineBreakMode:NSLineBreakByTruncatingTail];
          [taskRow addSubview:taskField];

          // Get the actual baseline of the text to align the icon properly
          // Position icon to align with text baseline (text renders at ~2/3 height from bottom)
          CGFloat textCenterY = taskItemHeight * 0.75;  // Adjusted higher by 2-3px more
          CGFloat iconYInRow = textCenterY - (dotSize / 2);

          // Create icon from SVG with color tint
          NSImage* taskIcon = CreateImageFromSVG(iconSVG, NSMakeSize(dotSize, dotSize));
          if (taskIcon) {
            [taskIcon setTemplate:YES];  // Use template mode for tinting
          }

          NSImageView* iconView = [[NSImageView alloc] initWithFrame:NSMakeRect(0, iconYInRow, dotSize, dotSize)];
          [iconView setImage:taskIcon];
          [iconView setContentTintColor:iconColor];  // Apply color tint
          [taskRow addSubview:iconView];

          // Add the row to the panel
          [tasks_panel_ addSubview:taskRow];

          yOffset -= taskItemHeight;
        }
      }

      LOG_DEBUG("UI", "Updated tasks list with " + std::to_string(tasks.size()) + " tasks");
    }
  }
}

void OwlUIDelegate::ShowTasksList() {
  @autoreleasepool {
    tasks_list_visible_ = true;

    if (!tasks_panel_ && main_window_) {
      LOG_DEBUG("UI", "Creating tasks list panel");

      // Create tasks panel below the overlay
      NSRect windowFrame = [main_window_ contentView].frame;
      CGFloat panelWidth = 350;
      CGFloat panelHeight = 400;
      CGFloat panelX = (windowFrame.size.width - panelWidth) / 2;
      CGFloat panelY = 130;  // Just below the overlay (overlay is at y=40, height=80)

      tasks_panel_ = [[NSView alloc] initWithFrame:NSMakeRect(panelX, panelY, panelWidth, panelHeight)];
      [tasks_panel_ setWantsLayer:YES];
      [tasks_panel_ retain];

      // White background with border
      tasks_panel_.layer.backgroundColor = [[NSColor whiteColor] CGColor];
      tasks_panel_.layer.cornerRadius = 16.0;
      tasks_panel_.layer.borderWidth = 2.0;
      tasks_panel_.layer.borderColor = [[NSColor colorWithRed:0.2 green:0.2 blue:0.2 alpha:0.15] CGColor];

      // Shadow
      tasks_panel_.layer.shadowColor = [[NSColor colorWithRed:0.259 green:0.522 blue:0.957 alpha:1.0] CGColor];
      tasks_panel_.layer.shadowOpacity = 0.15;
      tasks_panel_.layer.shadowOffset = NSMakeSize(0, -4);
      tasks_panel_.layer.shadowRadius = 16.0;

      // Add to window with animation
      [tasks_panel_ setAlphaValue:0.0];
      [[main_window_ contentView] addSubview:tasks_panel_ positioned:NSWindowAbove relativeTo:nil];

      [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = 0.25;
        context.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseOut];
        [tasks_panel_ setAlphaValue:1.0];
      } completionHandler:nil];

      LOG_DEBUG("UI", "Tasks list panel shown");
    } else if (tasks_panel_) {
      // Re-show existing panel
      [tasks_panel_ setAlphaValue:0.0];
      [[main_window_ contentView] addSubview:tasks_panel_ positioned:NSWindowAbove relativeTo:nil];

      [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = 0.25;
        [tasks_panel_ setAlphaValue:1.0];
      } completionHandler:nil];
    }

    // Update task list content whenever panel is shown
    UpdateTasksList();
  }
}

void OwlUIDelegate::HideTasksList() {
  @autoreleasepool {
    tasks_list_visible_ = false;

    if (tasks_panel_ && main_window_) {
      [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = 0.2;
        [tasks_panel_ setAlphaValue:0.0];
      } completionHandler:^{
        [tasks_panel_ removeFromSuperview];
      }];

      LOG_DEBUG("UI", "Tasks list panel hidden");
    }
  }
}

void OwlUIDelegate::ToggleTasksList() {
  if (tasks_list_visible_) {
    HideTasksList();
  } else {
    ShowTasksList();
  }
}

void OwlUIDelegate::RepositionOverlaysForResize() {
  @autoreleasepool {
    if (!main_window_) return;

    NSRect windowFrame = [main_window_ contentView].frame;

    // Reposition prompt overlay if it exists
    if (prompt_overlay_) {
      CGFloat overlayWidth = [prompt_overlay_ frame].size.width;
      CGFloat overlayHeight = [prompt_overlay_ frame].size.height;
      CGFloat overlayX = (windowFrame.size.width - overlayWidth) / 2;
      CGFloat overlayY = 40;  // Keep same distance from bottom

      [prompt_overlay_ setFrame:NSMakeRect(overlayX, overlayY, overlayWidth, overlayHeight)];
    }

    // Reposition tasks panel if it exists
    if (tasks_panel_) {
      CGFloat panelWidth = [tasks_panel_ frame].size.width;
      CGFloat panelHeight = [tasks_panel_ frame].size.height;
      CGFloat panelX = (windowFrame.size.width - panelWidth) / 2;
      CGFloat panelY = 130;  // Just below the overlay

      [tasks_panel_ setFrame:NSMakeRect(panelX, panelY, panelWidth, panelHeight)];
    }
  }
}

void OwlUIDelegate::ShowResponseArea(const std::string& response_text) {
  @autoreleasepool {
    if (!main_window_) return;

    // If response area doesn't exist, create it
    if (!response_area_) {
      NSRect windowFrame = [main_window_ contentView].frame;
      CGFloat overlayWidth = 700;
      CGFloat overlayY = 40;
      CGFloat responseHeight = 340;
      CGFloat responseX = (windowFrame.size.width - overlayWidth) / 2;

      // Position response ABOVE input overlay (higher Y)
      // Input will cover bottom part of response (layered paper effect)
      // Response bottom hidden behind input, only top visible with rounded corners
      CGFloat responseY = overlayY + 20;  // Start slightly above input bottom

      response_area_ = [[NSView alloc] initWithFrame:NSMakeRect(responseX, responseY, overlayWidth, responseHeight)];
      [response_area_ setWantsLayer:YES];
      [response_area_ retain];

      // Layered paper page 2 styling - only TOP corners rounded (bottom hidden by input)
      response_area_.layer.backgroundColor = [[NSColor colorWithWhite:0.98 alpha:1.0] CGColor];
      response_area_.layer.borderWidth = 2.0;
      response_area_.layer.borderColor = [[NSColor colorWithRed:0.2 green:0.2 blue:0.2 alpha:0.12] CGColor];

      // Only round top corners (bottom hidden behind input page 1)
      response_area_.layer.maskedCorners = kCALayerMinXMaxYCorner | kCALayerMaxXMaxYCorner;  // Top corners only
      response_area_.layer.cornerRadius = 28.0;

      // Deeper shadow for page 2 (behind page 1)
      response_area_.layer.shadowColor = [[NSColor colorWithRed:0.259 green:0.522 blue:0.957 alpha:1.0] CGColor];
      response_area_.layer.shadowOpacity = 0.25;
      response_area_.layer.shadowOffset = NSMakeSize(0, -6);
      response_area_.layer.shadowRadius = 20.0;

      // Close button in top-right corner
      CGFloat closeButtonSize = 32;
      NSButton* closeButton = [[NSButton alloc] initWithFrame:NSMakeRect(overlayWidth - closeButtonSize - 16, responseHeight - closeButtonSize - 16, closeButtonSize, closeButtonSize)];
      [closeButton setButtonType:NSButtonTypeMomentaryChange];
      [closeButton setBordered:NO];
      [closeButton setWantsLayer:YES];
      closeButton.layer.cornerRadius = closeButtonSize / 2;
      closeButton.layer.backgroundColor = [[NSColor colorWithWhite:0.95 alpha:1.0] CGColor];

      // Add cursor change on hover
      NSTrackingArea* trackingArea = [[NSTrackingArea alloc]
        initWithRect:closeButton.bounds
        options:(NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow | NSTrackingCursorUpdate)
        owner:closeButton
        userInfo:nil];
      [closeButton addTrackingArea:trackingArea];
      [closeButton addCursorRect:closeButton.bounds cursor:[NSCursor pointingHandCursor]];

      // X icon for close
      NSImage* closeIcon = CreateImageFromSVG(OlibIcons::XMARK, NSMakeSize(14, 14));
      if (closeIcon) {
        [closeIcon setTemplate:YES];
        [closeButton setImage:closeIcon];
        [closeButton setContentTintColor:[NSColor colorWithWhite:0.4 alpha:1.0]];
      }

      // Create helper if not exists
      if (!prompt_helper_) {
        OlibPromptHelper* helper = [[OlibPromptHelper alloc] init];
        helper.delegate = this;
        [helper retain];
        prompt_helper_ = (__bridge void*)helper;
      }

      OlibPromptHelper* helper = (__bridge OlibPromptHelper*)prompt_helper_;
      [closeButton setTarget:helper];
      [closeButton setAction:@selector(closeResponseArea:)];
      [response_area_ addSubview:closeButton];

      // Create scrollable text view for response
      CGFloat padding = 24;
      CGFloat bottomPadding = 100;  // Extra space - bottom is behind input overlay
      CGFloat topPadding = 20;  // Space for close button at top
      NSRect textFrame = NSMakeRect(padding, bottomPadding, overlayWidth - 2*padding, responseHeight - bottomPadding - topPadding - closeButtonSize);

      // Create scroll view
      NSScrollView* scrollView = [[NSScrollView alloc] initWithFrame:textFrame];
      [scrollView setHasVerticalScroller:YES];
      [scrollView setHasHorizontalScroller:NO];
      [scrollView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
      [scrollView setBorderType:NSNoBorder];
      [scrollView setBackgroundColor:[NSColor clearColor]];
      [scrollView setDrawsBackground:NO];

      // Create text view with better typography
      NSTextView* textView = [[NSTextView alloc] initWithFrame:textFrame];
      [textView setEditable:NO];
      [textView setSelectable:YES];

      // Use SF Pro or fallback to system font
      NSFont* textFont = [NSFont fontWithName:@"SF Pro Text" size:15];
      if (!textFont) {
        textFont = [NSFont systemFontOfSize:15];
      }
      [textView setFont:textFont];

      [textView setTextColor:[NSColor colorWithRed:0.15 green:0.15 blue:0.15 alpha:1.0]];
      [textView setBackgroundColor:[NSColor clearColor]];
      [textView setDrawsBackground:NO];
      [textView setMaxSize:NSMakeSize(FLT_MAX, FLT_MAX)];
      [textView setVerticallyResizable:YES];
      [textView setHorizontallyResizable:NO];
      [textView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
      [[textView textContainer] setContainerSize:NSMakeSize(overlayWidth - 2*padding - 15, FLT_MAX)];
      [[textView textContainer] setWidthTracksTextView:YES];
      [[textView textContainer] setLineFragmentPadding:8];  // Better text spacing

      [scrollView setDocumentView:textView];
      [response_area_ addSubview:scrollView];

      response_text_field_ = (NSTextField*)textView;

      LOG_DEBUG("UI", "Created layered paper response area");
    }

    // Update text
    UpdateResponseText(response_text);

    // Show with animation - slide down from above
    [response_area_ setAlphaValue:0.0];
    CGRect finalFrame = [response_area_ frame];
    CGRect startFrame = finalFrame;
    startFrame.origin.y += 30;  // Start higher
    [response_area_ setFrame:startFrame];

    // Add BELOW input overlay so input covers response bottom
    [[main_window_ contentView] addSubview:response_area_ positioned:NSWindowBelow relativeTo:prompt_overlay_];

    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
      context.duration = 0.3;
      context.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseOut];
      [[response_area_ animator] setAlphaValue:1.0];
      [[response_area_ animator] setFrame:finalFrame];
    } completionHandler:^{
      LOG_DEBUG("UI", "Response area shown with slide animation");
    }];
  }
}

void OwlUIDelegate::HideResponseArea() {
  @autoreleasepool {
    if (response_area_ && main_window_) {
      [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = 0.2;
        [response_area_ setAlphaValue:0.0];
      } completionHandler:^{
        [response_area_ removeFromSuperview];
      }];

      LOG_DEBUG("UI", "Response area hidden");
    }
  }
}

// Helper function to convert simple markdown to attributed string
static NSAttributedString* MarkdownToAttributedString(const std::string& markdown) {
  NSString* text = [NSString stringWithUTF8String:markdown.c_str()];
  NSMutableAttributedString* attributedString = [[NSMutableAttributedString alloc] initWithString:text];

  // Base font
  NSFont* baseFont = [NSFont systemFontOfSize:15];
  NSFont* boldFont = [NSFont boldSystemFontOfSize:15];
  NSFont* codeFont = [NSFont fontWithName:@"Menlo" size:14] ?: [NSFont monospacedSystemFontOfSize:14 weight:NSFontWeightRegular];

  NSRange fullRange = NSMakeRange(0, [text length]);
  [attributedString addAttribute:NSFontAttributeName value:baseFont range:fullRange];

  // Parse markdown - simple patterns
  NSString* pattern = nil;
  NSRegularExpression* regex = nil;

  // Bold **text**
  pattern = @"\\*\\*([^*]+)\\*\\*";
  regex = [NSRegularExpression regularExpressionWithPattern:pattern options:0 error:nil];
  NSArray* matches = [regex matchesInString:text options:0 range:fullRange];
  for (NSTextCheckingResult* match in [matches reverseObjectEnumerator]) {
    NSRange matchRange = [match range];
    NSRange contentRange = [match rangeAtIndex:1];
    NSString* content = [text substringWithRange:contentRange];
    [attributedString replaceCharactersInRange:matchRange withString:content];
    [attributedString addAttribute:NSFontAttributeName value:boldFont range:NSMakeRange(matchRange.location, [content length])];
  }

  // Update text after replacements
  text = [attributedString string];
  fullRange = NSMakeRange(0, [text length]);

  // Inline code `text`
  pattern = @"`([^`]+)`";
  regex = [NSRegularExpression regularExpressionWithPattern:pattern options:0 error:nil];
  matches = [regex matchesInString:text options:0 range:fullRange];
  for (NSTextCheckingResult* match in [matches reverseObjectEnumerator]) {
    NSRange matchRange = [match range];
    NSRange contentRange = [match rangeAtIndex:1];
    NSString* content = [text substringWithRange:contentRange];
    [attributedString replaceCharactersInRange:matchRange withString:content];
    NSRange newRange = NSMakeRange(matchRange.location, [content length]);
    [attributedString addAttribute:NSFontAttributeName value:codeFont range:newRange];
    [attributedString addAttribute:NSBackgroundColorAttributeName value:[NSColor colorWithWhite:0.95 alpha:1.0] range:newRange];
  }

  // Update text again
  text = [attributedString string];
  fullRange = NSMakeRange(0, [text length]);

  // Headings
  pattern = @"^#{1,3}\\s+(.+)$";
  regex = [NSRegularExpression regularExpressionWithPattern:pattern options:NSRegularExpressionAnchorsMatchLines error:nil];
  matches = [regex matchesInString:text options:0 range:fullRange];
  for (NSTextCheckingResult* match in matches) {
    NSRange matchRange = [match range];
    NSFont* headingFont = [NSFont boldSystemFontOfSize:17];
    [attributedString addAttribute:NSFontAttributeName value:headingFont range:matchRange];
  }

  // Bullets - replace markdown bullets with native bullets
  pattern = @"^[\\s]*[-*]\\s+";
  regex = [NSRegularExpression regularExpressionWithPattern:pattern options:NSRegularExpressionAnchorsMatchLines error:nil];
  matches = [regex matchesInString:text options:0 range:fullRange];
  for (NSTextCheckingResult* match in [matches reverseObjectEnumerator]) {
    [attributedString replaceCharactersInRange:[match range] withString:@"• "];
  }

  return attributedString;
}

void OwlUIDelegate::UpdateResponseText(const std::string& text) {
  @autoreleasepool {
    if (!response_area_) return;

    // Find the scroll view and text view
    for (NSView* subview in [response_area_ subviews]) {
      if ([subview isKindOfClass:[NSScrollView class]]) {
        NSScrollView* scrollView = (NSScrollView*)subview;
        NSTextView* textView = (NSTextView*)[scrollView documentView];
        if (textView) {
          // Convert markdown to attributed string for nice formatting
          NSAttributedString* attributedText = MarkdownToAttributedString(text);
          [[textView textStorage] setAttributedString:attributedText];
          LOG_DEBUG("UI", "Updated response text with markdown formatting (" + std::to_string(text.length()) + " chars)");
          break;
        }
      }
    }
  }
}

// Bring browser window to front (used by element picker)
void BringBrowserWindowToFront(CefRefPtr<CefBrowser> browser) {
  if (!browser) return;

  @autoreleasepool {
    // Get the NSView from the browser
    NSView* contentView = (__bridge NSView*)browser->GetHost()->GetWindowHandle();
    if (contentView) {
      NSWindow* window = [contentView window];
      if (window) {
        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        LOG_DEBUG("UIBrowser", "Brought browser window to front");
      }
    }
  }
}

// Helper: Set window title for a browser (works with tabs)
void SetBrowserWindowTitle(CefRefPtr<CefBrowser> browser, const std::string& title) {
  @autoreleasepool {
    if (!browser) {
      LOG_ERROR("UIBrowser", "SetBrowserWindowTitle: browser is null");
      return;
    }

    CefRefPtr<CefBrowserHost> host = browser->GetHost();
    if (!host) {
      LOG_ERROR("UIBrowser", "SetBrowserWindowTitle: browser host is null");
      return;
    }

    NSView* contentView = (NSView*)host->GetWindowHandle();
    if (!contentView) {
      LOG_ERROR("UIBrowser", "SetBrowserWindowTitle: content view is null");
      return;
    }

    NSWindow* window = [contentView window];
    if (!window) {
      LOG_ERROR("UIBrowser", "SetBrowserWindowTitle: window is null");
      return;
    }

    [window setTitle:[NSString stringWithUTF8String:title.c_str()]];
    LOG_DEBUG("UIBrowser", "Set window title: " + title);
  }
}

// Proxy overlay implementation
void OwlUIDelegate::ShowProxyOverlay() {
  @autoreleasepool {
    // Safety check - ensure main_window_ is valid
    if (!main_window_) {
      LOG_ERROR("UI", "ShowProxyOverlay: main_window_ is null");
      return;
    }

    NSView* contentView = [main_window_ contentView];
    if (!contentView) {
      LOG_ERROR("UI", "ShowProxyOverlay: contentView is null");
      return;
    }

    proxy_overlay_visible_ = true;

    if (!proxy_overlay_) {
      LOG_DEBUG("UI", "Creating proxy configuration overlay");

      // Create overlay panel
      NSRect windowFrame = contentView.frame;
      CGFloat overlayWidth = 420;
      CGFloat overlayHeight = 680;  // Increased height for timezone, CA certificate, and Tor section
      CGFloat overlayX = (windowFrame.size.width - overlayWidth) / 2;
      CGFloat overlayY = (windowFrame.size.height - overlayHeight) / 2;

      proxy_overlay_ = [[NSView alloc] initWithFrame:NSMakeRect(overlayX, overlayY, overlayWidth, overlayHeight)];
      [proxy_overlay_ setWantsLayer:YES];
      [proxy_overlay_ retain];

      // White background with rounded corners
      proxy_overlay_.layer.backgroundColor = [[NSColor whiteColor] CGColor];
      proxy_overlay_.layer.cornerRadius = 16.0;
      proxy_overlay_.layer.borderWidth = 1.0;
      proxy_overlay_.layer.borderColor = [[NSColor colorWithRed:0.85 green:0.85 blue:0.85 alpha:1.0] CGColor];

      // Shadow
      proxy_overlay_.layer.shadowColor = [[NSColor colorWithRed:0.0 green:0.0 blue:0.0 alpha:1.0] CGColor];
      proxy_overlay_.layer.shadowOpacity = 0.15;
      proxy_overlay_.layer.shadowOffset = NSMakeSize(0, -2);
      proxy_overlay_.layer.shadowRadius = 20.0;

      // Create helper for callbacks
      if (!proxy_helper_) {
        OwlProxyHelper* helper = [[OwlProxyHelper alloc] init];
        helper.delegate = this;
        [helper retain];
        proxy_helper_ = (__bridge void*)helper;
      }
      OwlProxyHelper* helper = (__bridge OwlProxyHelper*)proxy_helper_;

      CGFloat yPos = overlayHeight - 60;
      CGFloat labelWidth = 90;
      CGFloat inputWidth = overlayWidth - labelWidth - 50;
      CGFloat rowHeight = 32;
      CGFloat spacing = 16;
      CGFloat leftMargin = 20;

      // Title label
      NSTextField* titleLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(leftMargin, overlayHeight - 45, overlayWidth - 80, 28)];
      [titleLabel setBezeled:NO];
      [titleLabel setDrawsBackground:NO];
      [titleLabel setEditable:NO];
      [titleLabel setSelectable:NO];
      [titleLabel setFont:[NSFont systemFontOfSize:20 weight:NSFontWeightBold]];
      [titleLabel setTextColor:[NSColor colorWithRed:0.1 green:0.1 blue:0.1 alpha:1.0]];
      [titleLabel setStringValue:@"Proxy Settings"];
      [proxy_overlay_ addSubview:titleLabel];

      // Close button in top-right
      CGFloat closeButtonSize = 26;
      NSButton* closeButton = [[NSButton alloc] initWithFrame:NSMakeRect(overlayWidth - closeButtonSize - 16, overlayHeight - 43, closeButtonSize, closeButtonSize)];
      [closeButton setButtonType:NSButtonTypeMomentaryChange];
      [closeButton setBordered:NO];
      [closeButton setWantsLayer:YES];
      closeButton.layer.cornerRadius = closeButtonSize / 2;
      closeButton.layer.backgroundColor = [[NSColor colorWithWhite:0.92 alpha:1.0] CGColor];
      NSImage* closeIcon = CreateImageFromSVG(OlibIcons::XMARK, NSMakeSize(11, 11));
      if (closeIcon) {
        [closeIcon setTemplate:YES];
        [closeButton setImage:closeIcon];
        [closeButton setContentTintColor:[NSColor colorWithWhite:0.35 alpha:1.0]];
      }
      [closeButton setTarget:helper];
      [closeButton setAction:@selector(closeProxyOverlay:)];
      [proxy_overlay_ addSubview:closeButton];

      yPos = overlayHeight - 80;

      // Proxy Type dropdown
      NSTextField* typeLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(leftMargin, yPos + 6, labelWidth, 20)];
      [typeLabel setBezeled:NO];
      [typeLabel setDrawsBackground:NO];
      [typeLabel setEditable:NO];
      [typeLabel setFont:[NSFont systemFontOfSize:14 weight:NSFontWeightMedium]];
      [typeLabel setTextColor:[NSColor colorWithWhite:0.25 alpha:1.0]];
      [typeLabel setStringValue:@"Type"];
      [proxy_overlay_ addSubview:typeLabel];

      proxy_type_popup_ = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(leftMargin + labelWidth, yPos, inputWidth, rowHeight) pullsDown:NO];
      [proxy_type_popup_ setFont:[NSFont systemFontOfSize:13]];
      [proxy_type_popup_ addItemsWithTitles:@[@"HTTP", @"HTTPS", @"SOCKS4", @"SOCKS5", @"SOCKS5H (Stealth)"]];
      [proxy_type_popup_ selectItemAtIndex:4];  // Default to SOCKS5H for stealth
      [proxy_type_popup_ setWantsLayer:YES];
      proxy_type_popup_.layer.cornerRadius = 6.0;
      [proxy_overlay_ addSubview:proxy_type_popup_];

      yPos -= (rowHeight + spacing);

      // Host input
      NSTextField* hostLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(leftMargin, yPos + 6, labelWidth, 20)];
      [hostLabel setBezeled:NO];
      [hostLabel setDrawsBackground:NO];
      [hostLabel setEditable:NO];
      [hostLabel setFont:[NSFont systemFontOfSize:14 weight:NSFontWeightMedium]];
      [hostLabel setTextColor:[NSColor colorWithWhite:0.25 alpha:1.0]];
      [hostLabel setStringValue:@"Host"];
      [proxy_overlay_ addSubview:hostLabel];

      proxy_host_input_ = [[NSTextField alloc] initWithFrame:NSMakeRect(leftMargin + labelWidth, yPos, inputWidth, rowHeight)];
      [proxy_host_input_ setPlaceholderString:@"proxy.example.com"];
      [proxy_host_input_ setFont:[NSFont systemFontOfSize:13]];
      [proxy_host_input_ setBezeled:YES];
      [proxy_host_input_ setBezelStyle:NSTextFieldRoundedBezel];
      [proxy_host_input_ setWantsLayer:YES];
      proxy_host_input_.layer.cornerRadius = 6.0;
      proxy_host_input_.layer.borderWidth = 1.0;
      proxy_host_input_.layer.borderColor = [[NSColor colorWithWhite:0.82 alpha:1.0] CGColor];
      [proxy_overlay_ addSubview:proxy_host_input_];

      yPos -= (rowHeight + spacing);

      // Port input
      NSTextField* portLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(leftMargin, yPos + 6, labelWidth, 20)];
      [portLabel setBezeled:NO];
      [portLabel setDrawsBackground:NO];
      [portLabel setEditable:NO];
      [portLabel setFont:[NSFont systemFontOfSize:14 weight:NSFontWeightMedium]];
      [portLabel setTextColor:[NSColor colorWithWhite:0.25 alpha:1.0]];
      [portLabel setStringValue:@"Port"];
      [proxy_overlay_ addSubview:portLabel];

      proxy_port_input_ = [[NSTextField alloc] initWithFrame:NSMakeRect(leftMargin + labelWidth, yPos, 100, rowHeight)];
      [proxy_port_input_ setPlaceholderString:@"1080"];
      [proxy_port_input_ setFont:[NSFont systemFontOfSize:13]];
      [proxy_port_input_ setBezeled:YES];
      [proxy_port_input_ setBezelStyle:NSTextFieldRoundedBezel];
      [proxy_port_input_ setWantsLayer:YES];
      proxy_port_input_.layer.cornerRadius = 6.0;
      proxy_port_input_.layer.borderWidth = 1.0;
      proxy_port_input_.layer.borderColor = [[NSColor colorWithWhite:0.82 alpha:1.0] CGColor];
      [proxy_overlay_ addSubview:proxy_port_input_];

      yPos -= (rowHeight + spacing);

      // Username input (optional)
      NSTextField* userLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(leftMargin, yPos + 6, labelWidth, 20)];
      [userLabel setBezeled:NO];
      [userLabel setDrawsBackground:NO];
      [userLabel setEditable:NO];
      [userLabel setFont:[NSFont systemFontOfSize:14 weight:NSFontWeightMedium]];
      [userLabel setTextColor:[NSColor colorWithWhite:0.25 alpha:1.0]];
      [userLabel setStringValue:@"Username"];
      [proxy_overlay_ addSubview:userLabel];

      proxy_username_input_ = [[NSTextField alloc] initWithFrame:NSMakeRect(leftMargin + labelWidth, yPos, inputWidth, rowHeight)];
      [proxy_username_input_ setPlaceholderString:@"Optional"];
      [proxy_username_input_ setFont:[NSFont systemFontOfSize:13]];
      [proxy_username_input_ setBezeled:YES];
      [proxy_username_input_ setBezelStyle:NSTextFieldRoundedBezel];
      [proxy_username_input_ setWantsLayer:YES];
      proxy_username_input_.layer.cornerRadius = 6.0;
      proxy_username_input_.layer.borderWidth = 1.0;
      proxy_username_input_.layer.borderColor = [[NSColor colorWithWhite:0.82 alpha:1.0] CGColor];
      [proxy_overlay_ addSubview:proxy_username_input_];

      yPos -= (rowHeight + spacing);

      // Password input (optional)
      NSTextField* passLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(leftMargin, yPos + 6, labelWidth, 20)];
      [passLabel setBezeled:NO];
      [passLabel setDrawsBackground:NO];
      [passLabel setEditable:NO];
      [passLabel setFont:[NSFont systemFontOfSize:14 weight:NSFontWeightMedium]];
      [passLabel setTextColor:[NSColor colorWithWhite:0.25 alpha:1.0]];
      [passLabel setStringValue:@"Password"];
      [proxy_overlay_ addSubview:passLabel];

      proxy_password_input_ = [[NSSecureTextField alloc] initWithFrame:NSMakeRect(leftMargin + labelWidth, yPos, inputWidth, rowHeight)];
      [proxy_password_input_ setPlaceholderString:@"Optional"];
      [proxy_password_input_ setFont:[NSFont systemFontOfSize:13]];
      [proxy_password_input_ setBezeled:YES];
      [proxy_password_input_ setBezelStyle:NSTextFieldRoundedBezel];
      [proxy_password_input_ setWantsLayer:YES];
      proxy_password_input_.layer.cornerRadius = 6.0;
      proxy_password_input_.layer.borderWidth = 1.0;
      proxy_password_input_.layer.borderColor = [[NSColor colorWithWhite:0.82 alpha:1.0] CGColor];
      [proxy_overlay_ addSubview:proxy_password_input_];

      yPos -= (rowHeight + spacing);

      // Timezone input (for stealth - prevents browserscan timezone mismatch detection)
      NSTextField* tzLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(leftMargin, yPos + 6, labelWidth, 20)];
      [tzLabel setBezeled:NO];
      [tzLabel setDrawsBackground:NO];
      [tzLabel setEditable:NO];
      [tzLabel setFont:[NSFont systemFontOfSize:14 weight:NSFontWeightMedium]];
      [tzLabel setTextColor:[NSColor colorWithWhite:0.25 alpha:1.0]];
      [tzLabel setStringValue:@"Timezone"];
      [proxy_overlay_ addSubview:tzLabel];

      proxy_timezone_input_ = [[NSTextField alloc] initWithFrame:NSMakeRect(leftMargin + labelWidth, yPos, inputWidth, rowHeight)];
      [proxy_timezone_input_ setPlaceholderString:@"e.g., America/New_York"];
      [proxy_timezone_input_ setFont:[NSFont systemFontOfSize:13]];
      [proxy_timezone_input_ setBezeled:YES];
      [proxy_timezone_input_ setBezelStyle:NSTextFieldRoundedBezel];
      [proxy_timezone_input_ setWantsLayer:YES];
      proxy_timezone_input_.layer.cornerRadius = 6.0;
      proxy_timezone_input_.layer.borderWidth = 1.0;
      proxy_timezone_input_.layer.borderColor = [[NSColor colorWithWhite:0.82 alpha:1.0] CGColor];
      [proxy_overlay_ addSubview:proxy_timezone_input_];

      yPos -= (rowHeight + spacing);

      // Stealth mode checkbox
      proxy_stealth_checkbox_ = [[NSButton alloc] initWithFrame:NSMakeRect(leftMargin, yPos, overlayWidth - 40, 22)];
      [proxy_stealth_checkbox_ setButtonType:NSButtonTypeSwitch];
      [proxy_stealth_checkbox_ setTitle:@"Enable Stealth Mode (WebRTC block, fingerprint)"];
      [proxy_stealth_checkbox_ setFont:[NSFont systemFontOfSize:13]];
      [proxy_stealth_checkbox_ setState:NSControlStateValueOn];  // Default enabled
      [proxy_overlay_ addSubview:proxy_stealth_checkbox_];

      yPos -= 28;

      // CA Certificate checkbox
      proxy_ca_checkbox_ = [[NSButton alloc] initWithFrame:NSMakeRect(leftMargin, yPos, overlayWidth - 40, 22)];
      [proxy_ca_checkbox_ setButtonType:NSButtonTypeSwitch];
      [proxy_ca_checkbox_ setTitle:@"Trust Custom CA (for Charles, mitmproxy, etc.)"];
      [proxy_ca_checkbox_ setFont:[NSFont systemFontOfSize:13]];
      [proxy_ca_checkbox_ setState:NSControlStateValueOff];
      [proxy_overlay_ addSubview:proxy_ca_checkbox_];

      yPos -= 32;

      // CA Certificate path label and buttons (Browse + Clear)
      CGFloat buttonWidth = 50;
      CGFloat buttonGapSmall = 6;
      CGFloat pathLabelWidth = overlayWidth - leftMargin - 20 - (buttonWidth * 2) - buttonGapSmall;

      proxy_ca_path_label_ = [[NSTextField alloc] initWithFrame:NSMakeRect(leftMargin, yPos, pathLabelWidth, 28)];
      [proxy_ca_path_label_ setBezeled:YES];
      [proxy_ca_path_label_ setBezelStyle:NSTextFieldRoundedBezel];
      [proxy_ca_path_label_ setEditable:NO];
      [proxy_ca_path_label_ setSelectable:YES];
      [proxy_ca_path_label_ setFont:[NSFont systemFontOfSize:12]];
      [proxy_ca_path_label_ setTextColor:[NSColor colorWithWhite:0.5 alpha:1.0]];
      [proxy_ca_path_label_ setStringValue:@"No certificate selected"];
      [proxy_ca_path_label_ setWantsLayer:YES];
      proxy_ca_path_label_.layer.cornerRadius = 6.0;
      proxy_ca_path_label_.layer.borderWidth = 1.0;
      proxy_ca_path_label_.layer.borderColor = [[NSColor colorWithWhite:0.82 alpha:1.0] CGColor];
      [proxy_overlay_ addSubview:proxy_ca_path_label_];

      // Browse button
      CGFloat browseX = leftMargin + pathLabelWidth + buttonGapSmall;
      proxy_ca_browse_button_ = [[NSButton alloc] initWithFrame:NSMakeRect(browseX, yPos, buttonWidth, 28)];
      [proxy_ca_browse_button_ setButtonType:NSButtonTypeMomentaryPushIn];
      [proxy_ca_browse_button_ setBezelStyle:NSBezelStyleRounded];
      [proxy_ca_browse_button_ setTitle:@"Browse"];
      [proxy_ca_browse_button_ setFont:[NSFont systemFontOfSize:11]];
      [proxy_ca_browse_button_ setTarget:helper];
      [proxy_ca_browse_button_ setAction:@selector(browseCACert:)];
      [proxy_overlay_ addSubview:proxy_ca_browse_button_];

      // Clear button
      CGFloat clearX = browseX + buttonWidth + buttonGapSmall;
      NSButton* clearButton = [[NSButton alloc] initWithFrame:NSMakeRect(clearX, yPos, buttonWidth, 28)];
      [clearButton setButtonType:NSButtonTypeMomentaryPushIn];
      [clearButton setBezelStyle:NSBezelStyleRounded];
      [clearButton setTitle:@"Clear"];
      [clearButton setFont:[NSFont systemFontOfSize:11]];
      [clearButton setTarget:helper];
      [clearButton setAction:@selector(clearCACert:)];
      [proxy_overlay_ addSubview:clearButton];

      // Set CA path label reference in helper
      helper.caPathLabel = proxy_ca_path_label_;

      yPos -= 32;

      // Tor circuit isolation section
      proxy_tor_checkbox_ = [[NSButton alloc] initWithFrame:NSMakeRect(leftMargin, yPos, overlayWidth - 40, 22)];
      [proxy_tor_checkbox_ setButtonType:NSButtonTypeSwitch];
      [proxy_tor_checkbox_ setTitle:@"Tor Proxy (request new circuit per context)"];
      [proxy_tor_checkbox_ setFont:[NSFont systemFontOfSize:13]];
      [proxy_tor_checkbox_ setState:NSControlStateValueOff];
      [proxy_overlay_ addSubview:proxy_tor_checkbox_];

      yPos -= (rowHeight + spacing);

      // Tor control port input
      NSTextField* torPortLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(leftMargin, yPos + 6, labelWidth, 20)];
      [torPortLabel setBezeled:NO];
      [torPortLabel setDrawsBackground:NO];
      [torPortLabel setEditable:NO];
      [torPortLabel setFont:[NSFont systemFontOfSize:14 weight:NSFontWeightMedium]];
      [torPortLabel setTextColor:[NSColor colorWithWhite:0.25 alpha:1.0]];
      [torPortLabel setStringValue:@"Ctrl Port"];
      [proxy_overlay_ addSubview:torPortLabel];

      proxy_tor_port_input_ = [[NSTextField alloc] initWithFrame:NSMakeRect(leftMargin + labelWidth, yPos, 100, rowHeight)];
      [proxy_tor_port_input_ setPlaceholderString:@"9051 (auto)"];
      [proxy_tor_port_input_ setFont:[NSFont systemFontOfSize:13]];
      [proxy_tor_port_input_ setBezeled:YES];
      [proxy_tor_port_input_ setBezelStyle:NSTextFieldRoundedBezel];
      [proxy_tor_port_input_ setWantsLayer:YES];
      proxy_tor_port_input_.layer.cornerRadius = 6.0;
      proxy_tor_port_input_.layer.borderWidth = 1.0;
      proxy_tor_port_input_.layer.borderColor = [[NSColor colorWithWhite:0.82 alpha:1.0] CGColor];
      [proxy_overlay_ addSubview:proxy_tor_port_input_];

      yPos -= (rowHeight + spacing);

      // Tor control password input
      NSTextField* torPassLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(leftMargin, yPos + 6, labelWidth, 20)];
      [torPassLabel setBezeled:NO];
      [torPassLabel setDrawsBackground:NO];
      [torPassLabel setEditable:NO];
      [torPassLabel setFont:[NSFont systemFontOfSize:14 weight:NSFontWeightMedium]];
      [torPassLabel setTextColor:[NSColor colorWithWhite:0.25 alpha:1.0]];
      [torPassLabel setStringValue:@"Ctrl Pass"];
      [proxy_overlay_ addSubview:torPassLabel];

      proxy_tor_password_input_ = [[NSSecureTextField alloc] initWithFrame:NSMakeRect(leftMargin + labelWidth, yPos, inputWidth, rowHeight)];
      [proxy_tor_password_input_ setPlaceholderString:@"Optional (uses cookie auth)"];
      [proxy_tor_password_input_ setFont:[NSFont systemFontOfSize:13]];
      [proxy_tor_password_input_ setBezeled:YES];
      [proxy_tor_password_input_ setBezelStyle:NSTextFieldRoundedBezel];
      [proxy_tor_password_input_ setWantsLayer:YES];
      proxy_tor_password_input_.layer.cornerRadius = 6.0;
      proxy_tor_password_input_.layer.borderWidth = 1.0;
      proxy_tor_password_input_.layer.borderColor = [[NSColor colorWithWhite:0.82 alpha:1.0] CGColor];
      [proxy_overlay_ addSubview:proxy_tor_password_input_];

      yPos -= 32;

      // Status label
      proxy_status_label_ = [[NSTextField alloc] initWithFrame:NSMakeRect(leftMargin, yPos, overlayWidth - 40, 22)];
      [proxy_status_label_ setBezeled:NO];
      [proxy_status_label_ setDrawsBackground:NO];
      [proxy_status_label_ setEditable:NO];
      [proxy_status_label_ setSelectable:NO];
      [proxy_status_label_ setFont:[NSFont systemFontOfSize:13]];
      [proxy_status_label_ setTextColor:[NSColor colorWithWhite:0.5 alpha:1.0]];
      [proxy_status_label_ setStringValue:@"Status: Disconnected"];
      [proxy_overlay_ addSubview:proxy_status_label_];

      // Two buttons at the bottom: Save Settings and Connect/Disconnect
      CGFloat buttonHeight = 44;
      CGFloat buttonY = 24;  // Fixed position from bottom
      CGFloat buttonGap = 12;
      CGFloat halfWidth = (overlayWidth - 40 - buttonGap) / 2;

      // Save Settings button (left)
      proxy_save_button_ = [[NSButton alloc] initWithFrame:NSMakeRect(leftMargin, buttonY, halfWidth, buttonHeight)];
      [proxy_save_button_ setButtonType:NSButtonTypeMomentaryChange];
      [proxy_save_button_ setBordered:NO];
      [proxy_save_button_ setWantsLayer:YES];
      proxy_save_button_.layer.backgroundColor = [[NSColor colorWithWhite:0.85 alpha:1.0] CGColor];
      proxy_save_button_.layer.cornerRadius = 10.0;

      NSMutableAttributedString* saveTitle = [[NSMutableAttributedString alloc] initWithString:@"Save"];
      [saveTitle addAttribute:NSFontAttributeName value:[NSFont systemFontOfSize:15 weight:NSFontWeightMedium] range:NSMakeRange(0, saveTitle.length)];
      [saveTitle addAttribute:NSForegroundColorAttributeName value:[NSColor colorWithWhite:0.2 alpha:1.0] range:NSMakeRange(0, saveTitle.length)];
      [proxy_save_button_ setAttributedTitle:saveTitle];

      [proxy_save_button_ setTarget:helper];
      [proxy_save_button_ setAction:@selector(saveProxySettings:)];
      [proxy_overlay_ addSubview:proxy_save_button_];

      // Connect/Disconnect button (right)
      proxy_connect_button_ = [[NSButton alloc] initWithFrame:NSMakeRect(leftMargin + halfWidth + buttonGap, buttonY, halfWidth, buttonHeight)];
      [proxy_connect_button_ setButtonType:NSButtonTypeMomentaryChange];
      [proxy_connect_button_ setBordered:NO];
      [proxy_connect_button_ setWantsLayer:YES];
      proxy_connect_button_.layer.backgroundColor = [[NSColor colorWithRed:0.2 green:0.5 blue:0.95 alpha:1.0] CGColor];
      proxy_connect_button_.layer.cornerRadius = 10.0;

      NSMutableAttributedString* btnTitle = [[NSMutableAttributedString alloc] initWithString:@"Connect"];
      [btnTitle addAttribute:NSFontAttributeName value:[NSFont systemFontOfSize:15 weight:NSFontWeightMedium] range:NSMakeRange(0, btnTitle.length)];
      [btnTitle addAttribute:NSForegroundColorAttributeName value:[NSColor whiteColor] range:NSMakeRange(0, btnTitle.length)];
      [proxy_connect_button_ setAttributedTitle:btnTitle];

      [proxy_connect_button_ setTarget:helper];
      [proxy_connect_button_ setAction:@selector(connectProxy:)];
      // Initially disabled until settings are saved
      [proxy_connect_button_ setEnabled:NO];
      proxy_connect_button_.layer.backgroundColor = [[NSColor colorWithWhite:0.7 alpha:1.0] CGColor];
      [proxy_overlay_ addSubview:proxy_connect_button_];

      // Add to window with animation
      [proxy_overlay_ setAlphaValue:0.0];
      [contentView addSubview:proxy_overlay_ positioned:NSWindowAbove relativeTo:nil];

      // Capture instance pointer for block
      OwlUIDelegate* delegate = this;
      NSWindow* window = main_window_;
      NSTextField* hostInput = proxy_host_input_;
      NSView* overlay = proxy_overlay_;

      [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = 0.25;
        context.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseOut];
        [overlay setAlphaValue:1.0];
      } completionHandler:^{
        if (window && hostInput) {
          [window makeFirstResponder:hostInput];
        }
        if (delegate) {
          delegate->UpdateProxyStatus();
        }
        LOG_DEBUG("UI", "Proxy overlay shown");
      }];

    } else if (proxy_overlay_) {
      // Already exists, show it
      [proxy_overlay_ setAlphaValue:0.0];
      [contentView addSubview:proxy_overlay_ positioned:NSWindowAbove relativeTo:nil];

      // Capture instance pointer for block
      OwlUIDelegate* delegate = this;
      NSWindow* window = main_window_;
      NSTextField* hostInput = proxy_host_input_;
      NSView* overlay = proxy_overlay_;

      [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = 0.25;
        [overlay setAlphaValue:1.0];
      } completionHandler:^{
        if (window && hostInput) {
          [window makeFirstResponder:hostInput];
        }
        if (delegate) {
          delegate->UpdateProxyStatus();
        }
        LOG_DEBUG("UI", "Proxy overlay re-shown");
      }];
    }
  }
}

void OwlUIDelegate::HideProxyOverlay() {
  @autoreleasepool {
    proxy_overlay_visible_ = false;

    if (proxy_overlay_) {
      // Remove focus
      if (main_window_) {
        [main_window_ makeFirstResponder:nil];
      }

      // Capture the overlay pointer for the block
      NSView* overlay = proxy_overlay_;

      [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = 0.2;
        [overlay setAlphaValue:0.0];
      } completionHandler:^{
        [overlay removeFromSuperview];
      }];

      LOG_DEBUG("UI", "Proxy overlay hidden");
    }
  }
}

void OwlUIDelegate::ToggleProxyOverlay() {
  if (proxy_overlay_visible_) {
    HideProxyOverlay();
  } else {
    ShowProxyOverlay();
  }
}

void OwlUIDelegate::SaveProxySettings() {
  @autoreleasepool {
    LOG_DEBUG("UI", "SaveProxySettings called");

    if (!proxy_host_input_ || !proxy_port_input_ || !proxy_type_popup_) {
      LOG_ERROR("UI", "Proxy input fields not initialized");
      return;
    }

    // Get values from UI
    NSString* hostNS = [proxy_host_input_ stringValue];
    NSString* portNS = [proxy_port_input_ stringValue];
    NSInteger typeIndex = [proxy_type_popup_ indexOfSelectedItem];

    if ([hostNS length] == 0) {
      [proxy_status_label_ setTextColor:[NSColor redColor]];
      [proxy_status_label_ setStringValue:@"Error: Host is required"];
      proxy_settings_saved_ = false;
      return;
    }

    if ([portNS length] == 0) {
      [proxy_status_label_ setTextColor:[NSColor redColor]];
      [proxy_status_label_ setStringValue:@"Error: Port is required"];
      proxy_settings_saved_ = false;
      return;
    }

    // Build proxy config
    ProxyConfig config;
    config.enabled = true;
    config.host = std::string([hostNS UTF8String]);
    config.port = [portNS intValue];

    // Map popup index to proxy type
    switch (typeIndex) {
      case 0: config.type = ProxyType::HTTP; break;
      case 1: config.type = ProxyType::HTTPS; break;
      case 2: config.type = ProxyType::SOCKS4; break;
      case 3: config.type = ProxyType::SOCKS5; break;
      case 4: config.type = ProxyType::SOCKS5H; break;
      default: config.type = ProxyType::SOCKS5H; break;
    }

    // Optional username/password
    if (proxy_username_input_) {
      NSString* userNS = [proxy_username_input_ stringValue];
      if ([userNS length] > 0) {
        config.username = std::string([userNS UTF8String]);
      }
    }
    if (proxy_password_input_) {
      NSString* passNS = [proxy_password_input_ stringValue];
      if ([passNS length] > 0) {
        config.password = std::string([passNS UTF8String]);
      }
    }

    // Stealth settings
    if (proxy_stealth_checkbox_) {
      bool stealth = ([proxy_stealth_checkbox_ state] == NSControlStateValueOn);
      config.stealth_mode = stealth;
      config.block_webrtc = stealth;
      config.randomize_fingerprint = stealth;
    }

    // CA Certificate settings
    if (proxy_ca_checkbox_ && proxy_ca_path_label_) {
      config.trust_custom_ca = ([proxy_ca_checkbox_ state] == NSControlStateValueOn);
      NSString* caPath = [proxy_ca_path_label_ toolTip];  // Full path stored in tooltip
      if (caPath && [caPath length] > 0 && ![caPath isEqualToString:@"No certificate selected"]) {
        config.ca_cert_path = std::string([caPath UTF8String]);
      } else {
        // Check stringValue as fallback
        caPath = [proxy_ca_path_label_ stringValue];
        if (caPath && ![caPath isEqualToString:@"No certificate selected"]) {
          config.ca_cert_path = std::string([caPath UTF8String]);
        }
      }
    }

    // Timezone spoofing - critical for stealth when using proxies
    // This prevents browserscan from detecting timezone mismatch between IP and browser
    if (proxy_timezone_input_) {
      NSString* timezoneNS = [proxy_timezone_input_ stringValue];
      if ([timezoneNS length] > 0) {
        config.timezone_override = std::string([timezoneNS UTF8String]);
        config.spoof_timezone = true;  // Enable timezone spoofing when timezone is provided
        LOG_DEBUG("UI", "Timezone override configured: " + config.timezone_override);
      } else {
        config.spoof_timezone = false;
        config.timezone_override = "";
      }
    }

    // Tor circuit isolation settings
    if (proxy_tor_checkbox_) {
      config.is_tor = ([proxy_tor_checkbox_ state] == NSControlStateValueOn);
      if (config.is_tor) {
        LOG_DEBUG("UI", "Tor proxy mode enabled - will request new circuit per context");
      }
    }
    if (proxy_tor_port_input_) {
      NSString* portNS = [proxy_tor_port_input_ stringValue];
      if ([portNS length] > 0) {
        config.tor_control_port = [portNS intValue];
      } else {
        config.tor_control_port = 0;  // Auto-detect
      }
    }
    if (proxy_tor_password_input_) {
      NSString* passNS = [proxy_tor_password_input_ stringValue];
      if ([passNS length] > 0) {
        config.tor_control_password = std::string([passNS UTF8String]);
      }
    }

    // Save config to proxy manager (just store, don't apply yet)
    OwlProxyManager* proxy_manager = OwlProxyManager::GetInstance();
    proxy_manager->SetProxyConfig(config);

    // Mark settings as saved
    proxy_settings_saved_ = true;

    // Update status to show saved
    [proxy_status_label_ setTextColor:[NSColor colorWithRed:0.2 green:0.6 blue:0.2 alpha:1.0]];
    [proxy_status_label_ setStringValue:[NSString stringWithFormat:@"✓ Settings saved: %s:%d",
                                         config.host.c_str(), config.port]];

    // Enable the Connect button now that settings are saved
    if (proxy_connect_button_) {
      [proxy_connect_button_ setEnabled:YES];
      proxy_connect_button_.layer.backgroundColor = [[NSColor colorWithRed:0.2 green:0.5 blue:0.95 alpha:1.0] CGColor];

      NSMutableAttributedString* btnTitle = [[NSMutableAttributedString alloc] initWithString:@"Connect"];
      [btnTitle addAttribute:NSFontAttributeName value:[NSFont systemFontOfSize:15 weight:NSFontWeightMedium] range:NSMakeRange(0, btnTitle.length)];
      [btnTitle addAttribute:NSForegroundColorAttributeName value:[NSColor whiteColor] range:NSMakeRange(0, btnTitle.length)];
      [proxy_connect_button_ setAttributedTitle:btnTitle];
    }

    LOG_DEBUG("UI", "Proxy settings saved: " + config.host + ":" + std::to_string(config.port));
  }
}

void OwlUIDelegate::ConnectProxy() {
  @autoreleasepool {
    LOG_DEBUG("UI", "ConnectProxy called");

    // Check if settings have been saved
    if (!proxy_settings_saved_) {
      [proxy_status_label_ setTextColor:[NSColor redColor]];
      [proxy_status_label_ setStringValue:@"Error: Save settings first"];
      return;
    }

    OwlProxyManager* proxy_manager = OwlProxyManager::GetInstance();
    ProxyConfig config = proxy_manager->GetProxyConfig();

    if (!config.IsValid()) {
      [proxy_status_label_ setTextColor:[NSColor redColor]];
      [proxy_status_label_ setStringValue:@"Error: Invalid proxy configuration"];
      return;
    }

    // Update status
    [proxy_status_label_ setTextColor:[NSColor colorWithRed:0.8 green:0.6 blue:0.0 alpha:1.0]];
    [proxy_status_label_ setStringValue:@"Connecting..."];

    // Mark as connecting in proxy manager
    if (proxy_manager->Connect()) {
      LOG_DEBUG("UI", "Proxy manager connect succeeded");

      // Apply proxy to the browser's request context
      if (browser_handler_) {
        CefRefPtr<CefBrowser> browser = browser_handler_->GetBrowser();
        if (browser) {
          CefRefPtr<CefRequestContext> request_context = browser->GetHost()->GetRequestContext();
          if (request_context) {
            // Set proxy via request context preferences
            CefRefPtr<CefValue> proxy_value = CefValue::Create();
            CefRefPtr<CefDictionaryValue> proxy_dict = CefDictionaryValue::Create();

            // Mode: "fixed_servers" for explicit proxy
            proxy_dict->SetString("mode", "fixed_servers");

            // Build proxy rules string
            std::string proxy_rules = config.GetCEFProxyString();
            proxy_dict->SetString("server", proxy_rules);

            // Bypass localhost
            proxy_dict->SetString("bypass_list", "<local>");

            proxy_value->SetDictionary(proxy_dict);

            // Set the proxy preference
            CefString error;
            if (request_context->SetPreference("proxy", proxy_value, error)) {
              LOG_DEBUG("UI", "Proxy preference set successfully: " + proxy_rules);
            } else {
              LOG_ERROR("UI", "Failed to set proxy preference: " + error.ToString());
            }

            // Also update the client's proxy config for CA certificate handling
            browser_handler_->SetProxyConfig(config);

            // Detect timezone from proxy and update stealth config
            // This ensures timezone spoofing matches the proxy location
            OwlDemographics* demo = OwlDemographics::GetInstance();
            LOG_DEBUG("UI", "Timezone detection check - demo=" + std::string(demo ? "valid" : "null") +
                      " spoof_timezone=" + std::to_string(config.spoof_timezone) +
                      " timezone_override=" + (config.timezone_override.empty() ? "empty" : config.timezone_override));

            // Default spoof_timezone to true for proxy connections if not explicitly set
            if (!config.spoof_timezone) {
              config.spoof_timezone = true;
              LOG_DEBUG("UI", "Enabled spoof_timezone for proxy connection");
            }

            if (demo && config.spoof_timezone && config.timezone_override.empty()) {
              LOG_DEBUG("UI", "Detecting timezone from proxy...");
              demo->SetProxyConfig(config);
              GeoLocationInfo location = demo->GetGeoLocation();
              if (location.success && !location.timezone.empty()) {
                LOG_DEBUG("UI", "Detected proxy timezone: " + location.timezone);
                // Update the config with detected timezone
                config.timezone_override = location.timezone;
                browser_handler_->SetProxyConfig(config);

                // Update stealth config for the browser
                int browser_id = browser->GetIdentifier();
                StealthConfig stealth_config = OwlStealth::GetContextFingerprint(browser_id);
                stealth_config.timezone = location.timezone;
                OwlStealth::SetContextFingerprint(browser_id, stealth_config);
                LOG_DEBUG("UI", "Updated stealth config timezone to: " + location.timezone);

                // Inject timezone spoofing into the current page immediately
                CefRefPtr<CefFrame> main_frame = browser->GetMainFrame();
                if (main_frame) {
                  OwlStealth::SpoofTimezone(main_frame, location.timezone);
                  LOG_DEBUG("UI", "Injected timezone spoofing into current page: " + location.timezone);
                }
              } else {
                LOG_WARN("UI", "Failed to detect proxy timezone: " + location.error);
              }
            }
          } else {
            LOG_ERROR("UI", "Request context is null");
          }
        } else {
          LOG_ERROR("UI", "Browser is null");
        }
      } else {
        LOG_ERROR("UI", "browser_handler_ is null");
      }

      UpdateProxyStatus();

      // Update toolbar button state
      if (toolbar_) {
        toolbar_->SetProxyConnected(true);
      }
    } else {
      [proxy_status_label_ setTextColor:[NSColor redColor]];
      [proxy_status_label_ setStringValue:@"Error: Connection failed"];
      LOG_ERROR("UI", "Proxy connection failed");
    }
  }
}

void OwlUIDelegate::DisconnectProxy() {
  @autoreleasepool {
    LOG_DEBUG("UI", "DisconnectProxy called");

    OwlProxyManager* proxy_manager = OwlProxyManager::GetInstance();
    proxy_manager->Disconnect();

    // Remove proxy from the browser's request context
    if (browser_handler_) {
      CefRefPtr<CefBrowser> browser = browser_handler_->GetBrowser();
      if (browser) {
        CefRefPtr<CefRequestContext> request_context = browser->GetHost()->GetRequestContext();
        if (request_context) {
          // Set proxy mode to "direct" (no proxy)
          CefRefPtr<CefValue> proxy_value = CefValue::Create();
          CefRefPtr<CefDictionaryValue> proxy_dict = CefDictionaryValue::Create();
          proxy_dict->SetString("mode", "direct");
          proxy_value->SetDictionary(proxy_dict);

          CefString error;
          if (request_context->SetPreference("proxy", proxy_value, error)) {
            LOG_DEBUG("UI", "Proxy disabled successfully (direct connection)");
          } else {
            LOG_ERROR("UI", "Failed to disable proxy: " + error.ToString());
          }

          // Clear the client's proxy config
          ProxyConfig empty_config;
          browser_handler_->SetProxyConfig(empty_config);
        }
      }
    }

    UpdateProxyStatus();

    // Update toolbar button state
    if (toolbar_) {
      toolbar_->SetProxyConnected(false);
    }

    LOG_DEBUG("UI", "Proxy disconnected");
  }
}

void OwlUIDelegate::UpdateProxyStatus() {
  @autoreleasepool {
    if (!proxy_status_label_ || !proxy_connect_button_) return;

    OwlProxyManager* proxy_manager = OwlProxyManager::GetInstance();
    ProxyStatus status = proxy_manager->GetStatus();

    NSColor* statusColor = nil;
    NSString* statusText = nil;
    NSString* buttonText = nil;
    NSColor* buttonBgColor = nil;
    BOOL buttonEnabled = YES;

    switch (status) {
      case ProxyStatus::CONNECTED:
        // Connected state - show Disconnect button (red)
        statusColor = [NSColor colorWithRed:0.15 green:0.65 blue:0.15 alpha:1.0];
        statusText = [NSString stringWithFormat:@"✓ Connected: %s:%d",
                      proxy_manager->GetProxyConfig().host.c_str(),
                      proxy_manager->GetProxyConfig().port];
        buttonText = @"Disconnect";
        buttonBgColor = [NSColor colorWithRed:0.85 green:0.25 blue:0.25 alpha:1.0];  // Red
        buttonEnabled = YES;
        break;

      case ProxyStatus::CONNECTING:
        // Connecting state
        statusColor = [NSColor colorWithRed:0.85 green:0.55 blue:0.0 alpha:1.0];
        statusText = @"⏳ Connecting...";
        buttonText = @"Connect";
        buttonBgColor = [NSColor colorWithRed:0.5 green:0.5 blue:0.5 alpha:1.0];  // Gray
        buttonEnabled = NO;
        break;

      case ProxyStatus::ERROR:
        // Error state - allow retry
        statusColor = [NSColor colorWithRed:0.85 green:0.2 blue:0.2 alpha:1.0];
        statusText = [NSString stringWithFormat:@"✗ Error: %s",
                      proxy_manager->GetStatusMessage().c_str()];
        buttonText = @"Connect";
        buttonBgColor = proxy_settings_saved_ ?
          [NSColor colorWithRed:0.2 green:0.5 blue:0.95 alpha:1.0] :  // Blue if saved
          [NSColor colorWithRed:0.5 green:0.5 blue:0.5 alpha:1.0];    // Gray if not
        buttonEnabled = proxy_settings_saved_;
        break;

      case ProxyStatus::DISCONNECTED:
      default:
        // Disconnected state - check if settings are saved
        if (proxy_settings_saved_) {
          statusColor = [NSColor colorWithRed:0.2 green:0.6 blue:0.2 alpha:1.0];
          statusText = [NSString stringWithFormat:@"✓ Saved: %s:%d (not connected)",
                        proxy_manager->GetProxyConfig().host.c_str(),
                        proxy_manager->GetProxyConfig().port];
          buttonText = @"Connect";
          buttonBgColor = [NSColor colorWithRed:0.2 green:0.5 blue:0.95 alpha:1.0];  // Blue
          buttonEnabled = YES;
        } else {
          statusColor = [NSColor colorWithWhite:0.5 alpha:1.0];
          statusText = @"Status: Direct connection";
          buttonText = @"Connect";
          buttonBgColor = [NSColor colorWithRed:0.5 green:0.5 blue:0.5 alpha:1.0];  // Gray
          buttonEnabled = NO;
        }
        break;
    }

    [proxy_status_label_ setTextColor:statusColor];
    [proxy_status_label_ setStringValue:statusText];

    [proxy_connect_button_ setEnabled:buttonEnabled];
    proxy_connect_button_.layer.backgroundColor = [buttonBgColor CGColor];
    NSMutableAttributedString* btnTitle = [[NSMutableAttributedString alloc] initWithString:buttonText];
    [btnTitle addAttribute:NSFontAttributeName value:[NSFont systemFontOfSize:15 weight:NSFontWeightMedium] range:NSMakeRange(0, btnTitle.length)];
    [btnTitle addAttribute:NSForegroundColorAttributeName value:[NSColor whiteColor] range:NSMakeRange(0, btnTitle.length)];
    [proxy_connect_button_ setAttributedTitle:btnTitle];
  }
}

#endif  // OS_MACOS
