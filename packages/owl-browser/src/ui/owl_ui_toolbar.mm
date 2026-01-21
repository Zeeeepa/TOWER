#include "owl_ui_toolbar.h"
#include "logger.h"
#include "../resources/icons/icons.h"

#if defined(OS_MACOS)

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <QuartzCore/QuartzCore.h>

// Helper: Create NSImage from SVG string
NSImage* CreateImageFromSVG(const std::string& svgString, NSSize size) {
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

// Custom button class with hover effects
@interface OlibToolbarButton : NSButton
@property (nonatomic, assign) BOOL isHovering;
@end

@implementation OlibToolbarButton

- (instancetype)initWithFrame:(NSRect)frameRect {
  self = [super initWithFrame:frameRect];
  if (self) {
    _isHovering = NO;
    [self setButtonType:NSButtonTypeMomentaryChange];
    [self setBordered:NO];
    [self setWantsLayer:YES];

    // Add tracking area for hover
    NSTrackingArea* trackingArea = [[NSTrackingArea alloc]
      initWithRect:self.bounds
      options:(NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect)
      owner:self
      userInfo:nil];
    [self addTrackingArea:trackingArea];
  }
  return self;
}

- (void)mouseEntered:(NSEvent *)event {
  _isHovering = YES;
  self.layer.backgroundColor = [[NSColor colorWithWhite:0.0 alpha:0.05] CGColor];
  self.layer.cornerRadius = 8.0;
}

- (void)mouseExited:(NSEvent *)event {
  _isHovering = NO;
  self.layer.backgroundColor = [[NSColor clearColor] CGColor];
}

- (void)mouseDown:(NSEvent *)event {
  self.layer.backgroundColor = [[NSColor colorWithWhite:0.0 alpha:0.08] CGColor];
  [super mouseDown:event];
}

- (void)mouseUp:(NSEvent *)event {
  if (_isHovering) {
    self.layer.backgroundColor = [[NSColor colorWithWhite:0.0 alpha:0.05] CGColor];
  } else {
    self.layer.backgroundColor = [[NSColor clearColor] CGColor];
  }
  [super mouseUp:event];
}

@end

// Toolbar delegate to handle callbacks
@interface OlibToolbarDelegate : NSObject
@property (nonatomic, assign) OwlUIToolbar* toolbar;
@end

@implementation OlibToolbarDelegate

- (void)backClicked:(id)sender {
  LOG_DEBUG("UI", "Back button clicked");
  if (_toolbar) {
    _toolbar->ExecuteBackCallback();
  }
}

- (void)forwardClicked:(id)sender {
  LOG_DEBUG("UI", "Forward button clicked");
  if (_toolbar) {
    _toolbar->ExecuteForwardCallback();
  }
}

- (void)reloadClicked:(id)sender {
  LOG_DEBUG("UI", "Reload button clicked");
  if (_toolbar) {
    _toolbar->ExecuteReloadCallback();
  }
}

- (void)stopClicked:(id)sender {
  LOG_DEBUG("UI", "Stop button clicked");
  if (_toolbar) {
    _toolbar->ExecuteStopLoadingCallback();
  }
}

- (void)homeClicked:(id)sender {
  LOG_DEBUG("UI", "Home button clicked");
  if (_toolbar) {
    _toolbar->ExecuteHomeCallback();
  }
}

- (void)goClicked:(id)sender {
  (void)sender; // Unused parameter
  if (_toolbar) {
    // Get the address bar's parent view and find the text field
    NSView* toolbarView = (NSView*)_toolbar->GetView();
    for (NSView* subview in [toolbarView subviews]) {
      if ([subview isKindOfClass:[NSView class]]) {
        for (NSView* child in [subview subviews]) {
          if ([child isKindOfClass:[NSTextField class]]) {
            NSTextField* addressBar = (NSTextField*)child;
            std::string url = std::string([[addressBar stringValue] UTF8String]);
            LOG_DEBUG("UI", "Go button clicked: " + url);
            _toolbar->ExecuteNavigateCallback(url);
            return;
          }
        }
      }
    }
  }
}

- (void)agentClicked:(id)sender {
  LOG_DEBUG("UI", "AI Agent button clicked");
  if (_toolbar) {
    _toolbar->ExecuteAgentToggleCallback();
  }
}

- (void)newTabClicked:(id)sender {
  LOG_DEBUG("UI", "New Tab button clicked");
  if (_toolbar) {
    _toolbar->ExecuteNewTabCallback();
  }
}

- (void)proxyClicked:(id)sender {
  LOG_DEBUG("UI", "Proxy button clicked");
  if (_toolbar) {
    _toolbar->ExecuteProxyToggleCallback();
  }
}

- (void)addressBarAction:(id)sender {
  if (_toolbar) {
    NSTextField* textField = (NSTextField*)sender;
    std::string url = std::string([[textField stringValue] UTF8String]);

    // Auto-add https:// if no protocol
    if (!url.empty() && url.find("://") == std::string::npos) {
      url = "https://" + url;
      [textField setStringValue:[NSString stringWithUTF8String:url.c_str()]];
    }

    LOG_DEBUG("UI", "Address bar action: " + url);
    _toolbar->ExecuteNavigateCallback(url);
  }
}

@end

// TLD Autocomplete Helper
@interface TLDAutocompleteHelper : NSObject <NSTextFieldDelegate, NSTableViewDelegate, NSTableViewDataSource>
@property (nonatomic, strong) NSWindow* popupWindow;
@property (nonatomic, strong) NSTableView* tableView;
@property (nonatomic, strong) NSScrollView* scrollView;
@property (nonatomic, strong) NSMutableArray* suggestions;
@property (nonatomic, strong) NSMutableArray* allTLDs;
@property (nonatomic, strong) NSMutableArray* allOlibPages;
@property (nonatomic, strong) NSTextField* textField;
@property (nonatomic, assign) OwlUIToolbar* toolbar;
@property (nonatomic, assign) NSInteger selectedIndex;

- (instancetype)initWithTextField:(NSTextField*)textField toolbar:(OwlUIToolbar*)toolbar;
- (void)showSuggestionsForDomain:(NSString*)domain withFilter:(NSString*)filter;
- (void)showOlibSuggestionsForPath:(NSString*)path;
- (void)hideSuggestions;
- (void)selectSuggestionAtIndex:(NSInteger)index;
@end

@implementation TLDAutocompleteHelper

- (instancetype)initWithTextField:(NSTextField*)textField toolbar:(OwlUIToolbar*)toolbar {
  self = [super init];
  if (self) {
    _textField = textField;
    _toolbar = toolbar;
    _selectedIndex = -1;
    _suggestions = [[NSMutableArray alloc] init];

    // Initialize TLD list with explicit retain
    _allTLDs = [[NSMutableArray alloc] initWithArray:@[
      @{@"tld": @".com", @"desc": @"Commercial"},
      @{@"tld": @".org", @"desc": @"Organization"},
      @{@"tld": @".net", @"desc": @"Network"},
      @{@"tld": @".io", @"desc": @"Tech startups"},
      @{@"tld": @".co", @"desc": @"Company"},
      @{@"tld": @".ai", @"desc": @"Artificial Intelligence"},
      @{@"tld": @".dev", @"desc": @"Developers"},
      @{@"tld": @".app", @"desc": @"Applications"},
      @{@"tld": @".tech", @"desc": @"Technology"},
      @{@"tld": @".me", @"desc": @"Personal"},
      @{@"tld": @".info", @"desc": @"Information"},
      @{@"tld": @".biz", @"desc": @"Business"},
      @{@"tld": @".ca", @"desc": @"Canada"},
      @{@"tld": @".uk", @"desc": @"United Kingdom"},
      @{@"tld": @".de", @"desc": @"Germany"},
      @{@"tld": @".fr", @"desc": @"France"},
      @{@"tld": @".jp", @"desc": @"Japan"},
      @{@"tld": @".cn", @"desc": @"China"},
      @{@"tld": @".in", @"desc": @"India"},
      @{@"tld": @".br", @"desc": @"Brazil"}
    ]];

    // Initialize owl:// pages list
    _allOlibPages = [[NSMutableArray alloc] initWithArray:@[
      @{@"page": @"homepage.html", @"desc": @"Browser Homepage"},
      @{@"page": @"signin_form.html", @"desc": @"Sign In Form Test Page"},
      @{@"page": @"user_form.html", @"desc": @"User Form Test Page"}
    ]];

    [textField setDelegate:self];
  }
  return self;
}

- (void)dealloc {
  if (_suggestions) {
    [_suggestions release];
  }
  if (_allTLDs) {
    [_allTLDs release];
  }
  if (_allOlibPages) {
    [_allOlibPages release];
  }
  if (_popupWindow) {
    [_popupWindow release];
  }
  if (_tableView) {
    [_tableView release];
  }
  if (_scrollView) {
    [_scrollView release];
  }
  [super dealloc];
}

- (void)controlTextDidChange:(NSNotification *)notification {
  // Safety check
  if (!_textField) {
    return;
  }

  NSTextField* field = [notification object];
  NSString* value = [field stringValue];

  // Check for owl:// schema first
  if ([value hasPrefix:@"owl://"] && value.length >= 7) {
    NSString* afterSchema = [value substringFromIndex:7]; // Skip "owl://"
    [self showOlibSuggestionsForPath:afterSchema];
    return;
  }

  // Find last dot for TLD autocomplete
  NSRange lastDotRange = [value rangeOfString:@"." options:NSBackwardsSearch];

  if (lastDotRange.location == NSNotFound) {
    [self hideSuggestions];
    return;
  }

  // If dot is at the end, show all suggestions
  if (lastDotRange.location == value.length - 1) {
    NSString* domain = [value substringToIndex:lastDotRange.location];
    [self showSuggestionsForDomain:domain withFilter:@""];
    return;
  }

  // Get the part after the dot
  NSString* afterDot = [value substringFromIndex:lastDotRange.location + 1];
  NSString* domain = [value substringToIndex:lastDotRange.location];

  // Only show if we have characters after the dot
  if (afterDot.length > 0) {
    [self showSuggestionsForDomain:domain withFilter:afterDot];
  } else {
    [self hideSuggestions];
  }
}

- (void)showSuggestionsForDomain:(NSString*)domain withFilter:(NSString*)filter {
  // Safety checks
  if (!_textField || !_suggestions || !_allTLDs) {
    return;
  }

  // First, filter all TLDs that match the search term
  NSMutableArray* allMatches = [[NSMutableArray alloc] init];
  NSMutableArray* exactMatches = [[NSMutableArray alloc] init];
  NSMutableArray* partialMatches = [[NSMutableArray alloc] init];

  NSString* filterLower = [filter lowercaseString];

  for (NSDictionary* tld in _allTLDs) {
    NSString* tldStr = tld[@"tld"];
    NSString* tldWithoutDot = [tldStr substringFromIndex:1]; // Remove leading dot

    if (filter.length == 0 || [tldWithoutDot hasPrefix:filterLower]) {
      NSMutableDictionary* match = [tld mutableCopy];
      match[@"domain"] = domain;
      [allMatches addObject:match];

      // Check if it's an exact match or partial match
      if ([[tldWithoutDot lowercaseString] isEqualToString:filterLower]) {
        [exactMatches addObject:match];
      } else {
        [partialMatches addObject:match];
      }
    }
  }

  // If there's an exact match and no partial matches, hide dropdown
  if (exactMatches.count > 0 && partialMatches.count == 0) {
    [exactMatches release];
    [partialMatches release];
    [allMatches release];
    [self hideSuggestions];
    return;
  }

  // Use only partial matches (exclude exact matches from suggestions)
  [_suggestions removeAllObjects];
  [_suggestions addObjectsFromArray:partialMatches];

  // Limit to 5 suggestions
  if (_suggestions.count > 5) {
    [_suggestions removeObjectsInRange:NSMakeRange(5, _suggestions.count - 5)];
  }

  [exactMatches release];
  [partialMatches release];
  [allMatches release];

  if (_suggestions.count == 0) {
    [self hideSuggestions];
    return;
  }

  // Create or update popup window
  if (!_popupWindow) {
    NSRect frame = NSMakeRect(0, 0, 400, 150);
    _popupWindow = [[NSWindow alloc] initWithContentRect:frame
                                                styleMask:NSWindowStyleMaskBorderless
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    [_popupWindow setBackgroundColor:[NSColor colorWithWhite:0.12 alpha:0.98]];
    [_popupWindow setHasShadow:YES];
    [_popupWindow setLevel:NSPopUpMenuWindowLevel];
    [_popupWindow setOpaque:NO];

    // Create table view
    _scrollView = [[NSScrollView alloc] initWithFrame:frame];
    [_scrollView setHasVerticalScroller:YES];
    [_scrollView setAutohidesScrollers:YES];
    [_scrollView setBorderType:NSNoBorder];

    _tableView = [[NSTableView alloc] initWithFrame:frame];
    [_tableView setHeaderView:nil];
    [_tableView setBackgroundColor:[NSColor clearColor]];
    [_tableView setRowHeight:30];
    [_tableView setIntercellSpacing:NSMakeSize(0, 0)];
    [_tableView setSelectionHighlightStyle:NSTableViewSelectionHighlightStyleRegular];

    NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:@"tld"];
    [column setWidth:400];
    [_tableView addTableColumn:column];

    [_tableView setDelegate:self];
    [_tableView setDataSource:self];

    [_scrollView setDocumentView:_tableView];
    [_popupWindow setContentView:_scrollView];
  }

  // Position popup below text field
  NSRect fieldFrame = [_textField convertRect:_textField.bounds toView:nil];
  NSRect windowFrame = [[_textField window] convertRectToScreen:fieldFrame];

  CGFloat popupHeight = MIN(_suggestions.count * 30 + 4, 150);
  NSRect popupFrame = NSMakeRect(windowFrame.origin.x,
                                 windowFrame.origin.y - popupHeight - 2,
                                 windowFrame.size.width,
                                 popupHeight);
  [_popupWindow setFrame:popupFrame display:YES];

  [_tableView reloadData];

  // Make popup window a child of the browser window so it follows minimize/close
  NSWindow* parentWindow = [_textField window];
  if (parentWindow && ![parentWindow.childWindows containsObject:_popupWindow]) {
    [parentWindow addChildWindow:_popupWindow ordered:NSWindowAbove];
  }

  [_popupWindow orderFront:nil];
  _selectedIndex = -1;
}

- (void)hideSuggestions {
  if (_popupWindow) {
    // Remove from parent window before hiding
    NSWindow* parentWindow = [_popupWindow parentWindow];
    if (parentWindow) {
      [parentWindow removeChildWindow:_popupWindow];
    }
    [_popupWindow orderOut:nil];
  }
  _selectedIndex = -1;
}

- (void)showOlibSuggestionsForPath:(NSString*)path {
  // Safety checks
  if (!_textField || !_suggestions || !_allOlibPages) {
    return;
  }

  // First, filter all pages that match the search term
  NSMutableArray* allMatches = [[NSMutableArray alloc] init];
  NSMutableArray* exactMatches = [[NSMutableArray alloc] init];
  NSMutableArray* partialMatches = [[NSMutableArray alloc] init];

  NSString* pathLower = [path lowercaseString];

  for (NSDictionary* page in _allOlibPages) {
    NSString* pageStr = page[@"page"];

    if (path.length == 0 || [pageStr hasPrefix:pathLower]) {
      NSMutableDictionary* match = [page mutableCopy];
      [allMatches addObject:match];

      // Check if it's an exact match or partial match
      if ([[pageStr lowercaseString] isEqualToString:pathLower]) {
        [exactMatches addObject:match];
      } else {
        [partialMatches addObject:match];
      }
    }
  }

  // If there's an exact match and no partial matches, hide dropdown
  if (exactMatches.count > 0 && partialMatches.count == 0) {
    [exactMatches release];
    [partialMatches release];
    [allMatches release];
    [self hideSuggestions];
    return;
  }

  // Use only partial matches (exclude exact matches from suggestions)
  [_suggestions removeAllObjects];
  [_suggestions addObjectsFromArray:partialMatches];

  [exactMatches release];
  [partialMatches release];
  [allMatches release];

  if (_suggestions.count == 0) {
    [self hideSuggestions];
    return;
  }

  // Create or update popup window (reuse existing popup code)
  if (!_popupWindow) {
    NSRect frame = NSMakeRect(0, 0, 400, 150);
    _popupWindow = [[NSWindow alloc] initWithContentRect:frame
                                                styleMask:NSWindowStyleMaskBorderless
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    [_popupWindow setBackgroundColor:[NSColor colorWithWhite:0.12 alpha:0.98]];
    [_popupWindow setHasShadow:YES];
    [_popupWindow setLevel:NSPopUpMenuWindowLevel];
    [_popupWindow setOpaque:NO];

    // Create table view
    _scrollView = [[NSScrollView alloc] initWithFrame:frame];
    [_scrollView setHasVerticalScroller:YES];
    [_scrollView setAutohidesScrollers:YES];
    [_scrollView setBorderType:NSNoBorder];

    _tableView = [[NSTableView alloc] initWithFrame:frame];
    [_tableView setHeaderView:nil];
    [_tableView setBackgroundColor:[NSColor clearColor]];
    [_tableView setRowHeight:30];
    [_tableView setIntercellSpacing:NSMakeSize(0, 0)];
    [_tableView setSelectionHighlightStyle:NSTableViewSelectionHighlightStyleRegular];

    NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:@"owl"];
    [column setWidth:400];
    [_tableView addTableColumn:column];

    [_tableView setDelegate:self];
    [_tableView setDataSource:self];

    [_scrollView setDocumentView:_tableView];
    [_popupWindow setContentView:_scrollView];
  }

  // Position popup below text field
  NSRect fieldFrame = [_textField convertRect:_textField.bounds toView:nil];
  NSRect windowFrame = [[_textField window] convertRectToScreen:fieldFrame];

  CGFloat popupHeight = MIN(_suggestions.count * 30 + 4, 150);
  NSRect popupFrame = NSMakeRect(windowFrame.origin.x,
                                 windowFrame.origin.y - popupHeight - 2,
                                 windowFrame.size.width,
                                 popupHeight);
  [_popupWindow setFrame:popupFrame display:YES];

  [_tableView reloadData];

  // Make popup window a child of the browser window so it follows minimize/close
  NSWindow* parentWindow = [_textField window];
  if (parentWindow && ![parentWindow.childWindows containsObject:_popupWindow]) {
    [parentWindow addChildWindow:_popupWindow ordered:NSWindowAbove];
  }

  [_popupWindow orderFront:nil];
  _selectedIndex = -1;
}

- (void)selectSuggestionAtIndex:(NSInteger)index {
  if (index < 0 || index >= (NSInteger)_suggestions.count) return;

  // Safety check
  if (!_textField || !_toolbar) return;

  NSDictionary* suggestion = _suggestions[index];
  NSString* fullURL = nil;

  // Check if this is a TLD suggestion or owl:// page suggestion
  if (suggestion[@"tld"]) {
    // TLD suggestion
    NSString* domain = suggestion[@"domain"];
    NSString* tld = suggestion[@"tld"];
    fullURL = [NSString stringWithFormat:@"%@%@", domain, tld];
  } else if (suggestion[@"page"]) {
    // owl:// page suggestion
    NSString* page = suggestion[@"page"];
    fullURL = [NSString stringWithFormat:@"owl://%@", page];
  }

  if (fullURL) {
    [_textField setStringValue:fullURL];
    [self hideSuggestions];

    // Auto-navigate to the selected URL
    std::string url = [fullURL UTF8String];
    _toolbar->ExecuteNavigateCallback(url);
  }
}

- (BOOL)control:(NSControl *)control textView:(NSTextView *)textView doCommandBySelector:(SEL)commandSelector {
  if (!_popupWindow || ![_popupWindow isVisible]) {
    return NO;
  }

  if (commandSelector == @selector(moveDown:)) {
    _selectedIndex = MIN(_selectedIndex + 1, (NSInteger)_suggestions.count - 1);
    [_tableView selectRowIndexes:[NSIndexSet indexSetWithIndex:_selectedIndex] byExtendingSelection:NO];
    [_tableView scrollRowToVisible:_selectedIndex];
    return YES;
  } else if (commandSelector == @selector(moveUp:)) {
    _selectedIndex = MAX(_selectedIndex - 1, 0);
    [_tableView selectRowIndexes:[NSIndexSet indexSetWithIndex:_selectedIndex] byExtendingSelection:NO];
    [_tableView scrollRowToVisible:_selectedIndex];
    return YES;
  } else if (commandSelector == @selector(insertNewline:)) {
    if (_selectedIndex >= 0) {
      [self selectSuggestionAtIndex:_selectedIndex];
      return YES;
    }
  } else if (commandSelector == @selector(cancelOperation:)) {
    [self hideSuggestions];
    return YES;
  }

  return NO;
}

// NSTableViewDataSource methods
- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
  return _suggestions.count;
}

// NSTableViewDelegate methods
- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row {
  NSTextField* cellView = [tableView makeViewWithIdentifier:@"TLDCell" owner:self];

  if (!cellView) {
    cellView = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 400, 30)];
    [cellView setIdentifier:@"TLDCell"];
    [cellView setBezeled:NO];
    [cellView setDrawsBackground:NO];
    [cellView setEditable:NO];
    [cellView setSelectable:NO];
  }

  NSDictionary* suggestion = _suggestions[row];
  NSMutableAttributedString* attrStr = [[NSMutableAttributedString alloc] init];

  NSString* mainText = nil;
  NSString* desc = nil;

  // Check if this is a TLD suggestion or owl:// page suggestion
  if (suggestion[@"tld"]) {
    // TLD suggestion
    NSString* domain = suggestion[@"domain"];
    NSString* tld = suggestion[@"tld"];
    mainText = [NSString stringWithFormat:@"%@%@", domain, tld];
    desc = suggestion[@"desc"];
  } else if (suggestion[@"page"]) {
    // owl:// page suggestion
    NSString* page = suggestion[@"page"];
    mainText = [NSString stringWithFormat:@"owl://%@", page];
    desc = suggestion[@"desc"];
  }

  if (mainText && desc) {
    // Main text in blue
    NSAttributedString* mainPart = [[NSAttributedString alloc] initWithString:mainText
                                                                      attributes:@{
      NSForegroundColorAttributeName: [NSColor colorWithRed:0.26 green:0.52 blue:0.96 alpha:1.0],
      NSFontAttributeName: [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold]
    }];
    [attrStr appendAttributedString:mainPart];

    // Description in gray
    NSAttributedString* descPart = [[NSAttributedString alloc] initWithString:[NSString stringWithFormat:@"  %@", desc]
                                                                    attributes:@{
      NSForegroundColorAttributeName: [NSColor colorWithWhite:0.6 alpha:1.0],
      NSFontAttributeName: [NSFont systemFontOfSize:11]
    }];
    [attrStr appendAttributedString:descPart];
  }

  [cellView setAttributedStringValue:attrStr];

  return cellView;
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification {
  _selectedIndex = [_tableView selectedRow];
}

- (BOOL)tableView:(NSTableView *)tableView shouldSelectRow:(NSInteger)row {
  [self selectSuggestionAtIndex:row];
  return YES;
}

@end

// Implementation
OwlUIToolbar::OwlUIToolbar()
    : toolbar_view_(nullptr),
      back_button_(nullptr),
      forward_button_(nullptr),
      reload_button_(nullptr),
      stop_button_(nullptr),
      home_button_(nullptr),
      address_bar_(nullptr),
      go_button_(nullptr),
      agent_button_(nullptr),
      proxy_button_(nullptr),
      loading_indicator_(nullptr),
      tld_autocomplete_helper_(nullptr),
      agent_mode_active_(false),
      is_loading_(false),
      proxy_connected_(false) {
}

OwlUIToolbar::~OwlUIToolbar() {
  if (tld_autocomplete_helper_) {
    [tld_autocomplete_helper_ release];
  }
  if (toolbar_view_) {
    [toolbar_view_ release];
  }
}

void* OwlUIToolbar::CreateToolbarView(int width, int height) {
  @autoreleasepool {
    // Create main toolbar view
    NSRect frame = NSMakeRect(0, 0, width, height);
    toolbar_view_ = [[NSView alloc] initWithFrame:frame];
    [toolbar_view_ setWantsLayer:YES];
    toolbar_view_.layer.backgroundColor = [[NSColor whiteColor] CGColor];

    // Add bottom border
    CALayer* bottomBorder = [CALayer layer];
    bottomBorder.frame = NSMakeRect(0, 0, width, 1);
    bottomBorder.backgroundColor = [[NSColor colorWithWhite:0.85 alpha:1.0] CGColor];
    [toolbar_view_.layer addSublayer:bottomBorder];

    // Create delegate
    OlibToolbarDelegate* delegate = [[OlibToolbarDelegate alloc] init];
    delegate.toolbar = this;

    // Layout constants
    CGFloat padding = 16;
    CGFloat buttonSize = 36;
    CGFloat buttonSpacing = 4;
    CGFloat xPos = padding;
    CGFloat yPos = (height - buttonSize) / 2;

    // Icon size for SVG rendering
    NSSize iconSize = NSMakeSize(18, 18);

    // Back button
    back_button_ = [[OlibToolbarButton alloc] initWithFrame:NSMakeRect(xPos, yPos, buttonSize, buttonSize)];
    NSImage* backIcon = CreateImageFromSVG(OlibIcons::ANGLE_LEFT, iconSize);
    [back_button_ setImage:backIcon];
    [back_button_ setTarget:delegate];
    [back_button_ setAction:@selector(backClicked:)];
    [back_button_ setToolTip:@"Go Back (⌘[)"];
    [toolbar_view_ addSubview:back_button_];
    xPos += buttonSize + buttonSpacing;

    // Forward button
    forward_button_ = [[OlibToolbarButton alloc] initWithFrame:NSMakeRect(xPos, yPos, buttonSize, buttonSize)];
    NSImage* forwardIcon = CreateImageFromSVG(OlibIcons::ANGLE_RIGHT, iconSize);
    [forward_button_ setImage:forwardIcon];
    [forward_button_ setTarget:delegate];
    [forward_button_ setAction:@selector(forwardClicked:)];
    [forward_button_ setToolTip:@"Go Forward (⌘])"];
    [toolbar_view_ addSubview:forward_button_];
    xPos += buttonSize + buttonSpacing;

    // Reload button
    reload_button_ = [[OlibToolbarButton alloc] initWithFrame:NSMakeRect(xPos, yPos, buttonSize, buttonSize)];
    NSImage* reloadIcon = CreateImageFromSVG(OlibIcons::ARROWS_ROTATE, iconSize);
    [reload_button_ setImage:reloadIcon];
    [reload_button_ setTarget:delegate];
    [reload_button_ setAction:@selector(reloadClicked:)];
    [reload_button_ setToolTip:@"Reload (⌘R)"];
    [toolbar_view_ addSubview:reload_button_];

    // Stop button (same position as reload, will be shown/hidden based on loading state)
    stop_button_ = [[OlibToolbarButton alloc] initWithFrame:NSMakeRect(xPos, yPos, buttonSize, buttonSize)];
    NSImage* stopIcon = CreateImageFromSVG(OlibIcons::XMARK, iconSize);
    [stop_button_ setImage:stopIcon];
    [stop_button_ setTarget:delegate];
    [stop_button_ setAction:@selector(stopClicked:)];
    [stop_button_ setToolTip:@"Stop Loading"];
    [stop_button_ setHidden:YES];  // Hidden by default
    [toolbar_view_ addSubview:stop_button_];
    xPos += buttonSize + buttonSpacing;

    // Home button
    home_button_ = [[OlibToolbarButton alloc] initWithFrame:NSMakeRect(xPos, yPos, buttonSize, buttonSize)];
    NSImage* homeIcon = CreateImageFromSVG(OlibIcons::HOME, iconSize);
    [home_button_ setImage:homeIcon];
    [home_button_ setTarget:delegate];
    [home_button_ setAction:@selector(homeClicked:)];
    [home_button_ setToolTip:@"Home"];
    [toolbar_view_ addSubview:home_button_];
    xPos += buttonSize + padding;

    // Address bar container
    CGFloat addressBarHeight = 38;
    CGFloat agentButtonWidth = 110;
    CGFloat newTabButtonWidth = buttonSize + 4;  // New tab button + spacing
    CGFloat proxyButtonWidth = buttonSize + 10;  // Proxy button + spacing
    CGFloat addressBarWidth = width - xPos - padding - agentButtonWidth - newTabButtonWidth - proxyButtonWidth - 10;
    CGFloat addressBarY = (height - addressBarHeight) / 2;

    // Address bar background
    NSView* addressContainer = [[NSView alloc] initWithFrame:NSMakeRect(xPos, addressBarY, addressBarWidth, addressBarHeight)];
    [addressContainer setWantsLayer:YES];
    addressContainer.layer.backgroundColor = [[NSColor colorWithWhite:0.96 alpha:1.0] CGColor];
    addressContainer.layer.borderColor = [[NSColor colorWithWhite:0.82 alpha:1.0] CGColor];
    addressContainer.layer.borderWidth = 1.0;
    addressContainer.layer.cornerRadius = 10.0;
    [toolbar_view_ addSubview:addressContainer];

    // Search/Loading icon (will switch between search and spinner)
    // Position centered vertically, 14px from left for better spacing
    loading_indicator_ = [[NSImageView alloc] initWithFrame:NSMakeRect(14, (addressBarHeight - 16) / 2, 16, 16)];
    NSImage* searchImage = CreateImageFromSVG(OlibIcons::SEARCH, NSMakeSize(16, 16));
    [loading_indicator_ setImage:searchImage];
    [loading_indicator_ setWantsLayer:YES];  // Enable layer for animations

    [addressContainer addSubview:loading_indicator_];

    // Address bar text field - center vertically in container, make room for Go button
    CGFloat textFieldY = (addressBarHeight - 22) / 2;  // Center 22px text field in 38px container
    CGFloat goButtonWidth = 60;
    address_bar_ = [[NSTextField alloc] initWithFrame:NSMakeRect(40, textFieldY, addressBarWidth - 52 - goButtonWidth, 22)];
    [address_bar_ setPlaceholderString:@"Search or enter address"];
    [address_bar_ setBezeled:NO];
    [address_bar_ setDrawsBackground:NO];
    [address_bar_ setFont:[NSFont systemFontOfSize:14]];
    [address_bar_ setTarget:delegate];
    [address_bar_ setAction:@selector(addressBarAction:)];

    // Configure cell for single line with truncation
    [[address_bar_ cell] setLineBreakMode:NSLineBreakByTruncatingTail];
    [[address_bar_ cell] setUsesSingleLineMode:YES];
    [[address_bar_ cell] setScrollable:YES];
    [address_bar_ setAlignment:NSTextAlignmentLeft];

    [addressContainer addSubview:address_bar_];

    // Initialize TLD autocomplete for address bar
    tld_autocomplete_helper_ = [[TLDAutocompleteHelper alloc] initWithTextField:address_bar_ toolbar:this];

    // Go button - inside address bar container on the right
    CGFloat goButtonHeight = 26;
    CGFloat goButtonY = (addressBarHeight - goButtonHeight) / 2;
    CGFloat goButtonX = addressBarWidth - goButtonWidth - 8;
    go_button_ = [[NSButton alloc] initWithFrame:NSMakeRect(goButtonX, goButtonY, goButtonWidth, goButtonHeight)];
    [go_button_ setButtonType:NSButtonTypeMomentaryPushIn];
    [go_button_ setBezelStyle:NSBezelStyleRounded];
    [go_button_ setTitle:@"Go"];
    [go_button_ setFont:[NSFont systemFontOfSize:12 weight:NSFontWeightMedium]];
    [go_button_ setTarget:delegate];
    [go_button_ setAction:@selector(goClicked:)];
    [go_button_ setWantsLayer:YES];
    go_button_.layer.backgroundColor = [[NSColor colorWithWhite:0.18 alpha:1.0] CGColor];
    go_button_.layer.cornerRadius = 6.0;

    // Style Go button text
    NSMutableAttributedString* goAttrString = [[NSMutableAttributedString alloc] initWithString:@"Go"];
    [goAttrString addAttribute:NSForegroundColorAttributeName
                         value:[NSColor whiteColor]
                         range:NSMakeRange(0, goAttrString.length)];
    [goAttrString addAttribute:NSFontAttributeName
                         value:[NSFont systemFontOfSize:12 weight:NSFontWeightMedium]
                         range:NSMakeRange(0, goAttrString.length)];
    [go_button_ setAttributedTitle:goAttrString];
    [go_button_ setBordered:NO];

    [addressContainer addSubview:go_button_];
    xPos += addressBarWidth + 10;

    // New Tab button - small + icon
    NSButton* newTabButton = [[OlibToolbarButton alloc] initWithFrame:NSMakeRect(xPos, yPos, buttonSize, buttonSize)];
    NSImage* plusIcon = CreateImageFromSVG(OlibIcons::PLUS, NSMakeSize(14, 14));
    if (plusIcon) {
      [newTabButton setImage:plusIcon];
    }
    [newTabButton setTarget:delegate];
    [newTabButton setAction:@selector(newTabClicked:)];
    [newTabButton setToolTip:@"New Tab (⌘T)"];
    [toolbar_view_ addSubview:newTabButton];
    xPos += buttonSize + 4;

    // Proxy button - shield icon for VPN/Proxy toggle
    proxy_button_ = [[OlibToolbarButton alloc] initWithFrame:NSMakeRect(xPos, yPos, buttonSize, buttonSize)];
    NSImage* shieldIcon = CreateImageFromSVG(OlibIcons::SHIELD_BLANK, NSMakeSize(16, 16));
    if (shieldIcon) {
      [shieldIcon setTemplate:YES];  // Template for tinting
      [proxy_button_ setImage:shieldIcon];
    }
    [proxy_button_ setTarget:delegate];
    [proxy_button_ setAction:@selector(proxyClicked:)];
    [proxy_button_ setToolTip:@"Proxy Settings"];
    [toolbar_view_ addSubview:proxy_button_];
    xPos += buttonSize + 10;

    // AI Agent button with icon - elegant styling
    agent_button_ = [[NSButton alloc] initWithFrame:NSMakeRect(xPos, yPos, agentButtonWidth, buttonSize)];
    [agent_button_ setButtonType:NSButtonTypeMomentaryChange];
    [agent_button_ setTarget:delegate];
    [agent_button_ setAction:@selector(agentClicked:)];
    [agent_button_ setBordered:NO];
    [agent_button_ setWantsLayer:YES];

    // Modern rounded button with shadow
    agent_button_.layer.backgroundColor = [[NSColor colorWithWhite:0.18 alpha:1.0] CGColor];
    agent_button_.layer.cornerRadius = 10.0;
    agent_button_.layer.shadowColor = [[NSColor blackColor] CGColor];
    agent_button_.layer.shadowOffset = NSMakeSize(0, -1);
    agent_button_.layer.shadowRadius = 3.0;
    agent_button_.layer.shadowOpacity = 0.15;

    // Create and add icon with proper rendering
    NSImage* magicIcon = CreateImageFromSVG(OlibIcons::MAGIC_WAND_SPARKLES, NSMakeSize(16, 16));
    if (magicIcon) {
      [magicIcon setTemplate:YES];  // Use template mode so it respects tintColor (white on dark background)
      [agent_button_ setImage:magicIcon];
      [agent_button_ setImagePosition:NSImageLeft];
      [agent_button_ setImageHugsTitle:YES];
      [agent_button_ setContentTintColor:[NSColor whiteColor]];  // Set tint color to white
    }

    // Style text with proper attributes
    NSMutableAttributedString* attrString = [[NSMutableAttributedString alloc] initWithString:@"AI Agent"];
    [attrString addAttribute:NSForegroundColorAttributeName
                       value:[NSColor whiteColor]
                       range:NSMakeRange(0, attrString.length)];
    [attrString addAttribute:NSFontAttributeName
                       value:[NSFont systemFontOfSize:13 weight:NSFontWeightSemibold]
                       range:NSMakeRange(0, attrString.length)];
    [agent_button_ setAttributedTitle:attrString];

    [toolbar_view_ addSubview:agent_button_];

    // Set autoresizing - make address bar expand/shrink, keep buttons on right
    [toolbar_view_ setAutoresizingMask:(NSViewWidthSizable)];
    [addressContainer setAutoresizingMask:(NSViewWidthSizable | NSViewMaxXMargin)];
    [address_bar_ setAutoresizingMask:(NSViewWidthSizable)];
    [agent_button_ setAutoresizingMask:(NSViewMinXMargin)];

    LOG_DEBUG("UI", "Native toolbar created successfully");

    return (__bridge void*)toolbar_view_;
  }
}

void OwlUIToolbar::SetBackCallback(ToolbarCallback callback) {
  back_callback_ = callback;
}

void OwlUIToolbar::SetForwardCallback(ToolbarCallback callback) {
  forward_callback_ = callback;
}

void OwlUIToolbar::SetReloadCallback(ToolbarCallback callback) {
  reload_callback_ = callback;
}

void OwlUIToolbar::SetHomeCallback(ToolbarCallback callback) {
  home_callback_ = callback;
}

void OwlUIToolbar::SetNavigateCallback(NavigateCallback callback) {
  navigate_callback_ = callback;
}

void OwlUIToolbar::SetAgentToggleCallback(ToolbarCallback callback) {
  agent_toggle_callback_ = callback;
}

void OwlUIToolbar::SetStopLoadingCallback(ToolbarCallback callback) {
  stop_loading_callback_ = callback;
}

void OwlUIToolbar::SetNewTabCallback(ToolbarCallback callback) {
  new_tab_callback_ = callback;
}

void OwlUIToolbar::SetProxyToggleCallback(ToolbarCallback callback) {
  proxy_toggle_callback_ = callback;
}

void OwlUIToolbar::SetLoadingState(bool is_loading) {
  is_loading_ = is_loading;

  @autoreleasepool {
    dispatch_async(dispatch_get_main_queue(), ^{
      if (is_loading) {
        // Show stop button, hide reload button
        [stop_button_ setHidden:NO];
        [reload_button_ setHidden:YES];

        // Change Go button to "Stop"
        [go_button_ setTitle:@"Stop"];
        [go_button_ setAction:@selector(stopClicked:)];
        go_button_.layer.backgroundColor = [[NSColor colorWithRed:0.8 green:0.2 blue:0.2 alpha:1.0] CGColor];

        // Set white text color for Stop button
        NSMutableAttributedString* stopTitle = [[NSMutableAttributedString alloc]
            initWithString:@"Stop"
            attributes:@{
                NSForegroundColorAttributeName: [NSColor whiteColor],
                NSFontAttributeName: [NSFont systemFontOfSize:12 weight:NSFontWeightMedium]
            }];
        [go_button_ setAttributedTitle:stopTitle];

        // Change loading indicator to hourglass icon
        NSImage* loaderIcon = CreateImageFromSVG(OlibIcons::HOURGLASS, NSMakeSize(16, 16));
        [loading_indicator_ setImage:loaderIcon];

        // Add pulsing opacity animation (no position change, no rotation)
        CABasicAnimation* pulse = [CABasicAnimation animationWithKeyPath:@"opacity"];
        pulse.fromValue = [NSNumber numberWithFloat:1.0];
        pulse.toValue = [NSNumber numberWithFloat:0.3];
        pulse.duration = 0.8;
        pulse.autoreverses = YES;
        pulse.repeatCount = HUGE_VALF;
        [loading_indicator_.layer addAnimation:pulse forKey:@"pulseAnimation"];
      } else {
        // Hide stop button, show reload button
        [stop_button_ setHidden:YES];
        [reload_button_ setHidden:NO];

        // Change Go button back to "Go"
        [go_button_ setTitle:@"Go"];
        [go_button_ setAction:@selector(goClicked:)];
        go_button_.layer.backgroundColor = [[NSColor colorWithWhite:0.18 alpha:1.0] CGColor];

        // Set white text color for Go button
        NSMutableAttributedString* goTitle = [[NSMutableAttributedString alloc]
            initWithString:@"Go"
            attributes:@{
                NSForegroundColorAttributeName: [NSColor whiteColor],
                NSFontAttributeName: [NSFont systemFontOfSize:12 weight:NSFontWeightMedium]
            }];
        [go_button_ setAttributedTitle:goTitle];

        // Change loading indicator back to search icon
        NSImage* searchIcon = CreateImageFromSVG(OlibIcons::SEARCH, NSMakeSize(16, 16));
        [loading_indicator_ setImage:searchIcon];

        // Remove pulsing animation
        [loading_indicator_.layer removeAnimationForKey:@"pulseAnimation"];
      }
    });
  }
}

void OwlUIToolbar::UpdateNavigationButtons(bool can_go_back, bool can_go_forward) {
  @autoreleasepool {
    dispatch_async(dispatch_get_main_queue(), ^{
      if (back_button_) {
        [back_button_ setEnabled:can_go_back];
      }
      if (forward_button_) {
        [forward_button_ setEnabled:can_go_forward];
      }
    });
  }
}

void OwlUIToolbar::UpdateAddressBar(const std::string& url) {
  @autoreleasepool {
    // Convert to NSString BEFORE dispatch block to avoid accessing dangling reference
    NSString* urlString = [NSString stringWithUTF8String:url.c_str()];
    if (!urlString) {
      urlString = @"";
    }

    dispatch_async(dispatch_get_main_queue(), ^{
      if (address_bar_) {
        // Only add attributes if string is not empty
        if (urlString.length > 0) {
          NSMutableAttributedString* attrString = [[NSMutableAttributedString alloc] initWithString:urlString];
          [attrString addAttribute:NSFontAttributeName
                             value:[NSFont systemFontOfSize:14]
                             range:NSMakeRange(0, attrString.length)];
          [address_bar_ setAttributedStringValue:attrString];
        } else {
          // For empty string, create attributed string without adding attributes
          NSMutableAttributedString* emptyAttr = [[NSMutableAttributedString alloc] initWithString:@""];
          [address_bar_ setAttributedStringValue:emptyAttr];
        }
      }
    });
  }
}

void OwlUIToolbar::SetAgentModeActive(bool active) {
  agent_mode_active_ = active;

  LOG_DEBUG("UI", std::string("SetAgentModeActive called with: ") + (active ? "true" : "false"));

  @autoreleasepool {
    if (!agent_button_) {
      LOG_ERROR("UI", "agent_button_ is null!");
      return;
    }

    NSString* title = active ? @"AI Active" : @"AI Agent";
    NSColor* bgColor = active ?
      [NSColor colorWithRed:0.10 green:0.46 blue:0.90 alpha:1.0] :
      [NSColor colorWithWhite:0.18 alpha:1.0];

    // Update background color directly
    agent_button_.layer.backgroundColor = [bgColor CGColor];

    // Update attributed title with white text color
    NSMutableAttributedString* attrString = [[NSMutableAttributedString alloc] initWithString:title];
    [attrString addAttribute:NSForegroundColorAttributeName
                       value:[NSColor whiteColor]
                       range:NSMakeRange(0, attrString.length)];
    [attrString addAttribute:NSFontAttributeName
                       value:[NSFont systemFontOfSize:13 weight:NSFontWeightSemibold]
                       range:NSMakeRange(0, attrString.length)];
    [agent_button_ setAttributedTitle:attrString];

    LOG_DEBUG("UI", "Button title and color updated successfully");
  }
}

void OwlUIToolbar::SetProxyConnected(bool connected) {
  proxy_connected_ = connected;

  LOG_DEBUG("UI", std::string("SetProxyConnected called with: ") + (connected ? "true" : "false"));

  @autoreleasepool {
    if (!proxy_button_) {
      LOG_ERROR("UI", "proxy_button_ is null!");
      return;
    }

    dispatch_async(dispatch_get_main_queue(), ^{
      // Update icon based on connection state
      NSImage* icon = nil;
      if (connected) {
        icon = CreateImageFromSVG(OlibIcons::SHIELD, NSMakeSize(16, 16));  // Filled shield for connected
        [proxy_button_ setToolTip:@"Proxy Connected - Click to Disconnect"];
        // Add green tint to indicate connected
        [proxy_button_ setContentTintColor:[NSColor colorWithRed:0.2 green:0.8 blue:0.4 alpha:1.0]];
      } else {
        icon = CreateImageFromSVG(OlibIcons::SHIELD_BLANK, NSMakeSize(16, 16));  // Empty shield for disconnected
        [proxy_button_ setToolTip:@"Proxy Settings - Click to Connect"];
        [proxy_button_ setContentTintColor:[NSColor labelColor]];  // Default color
      }

      if (icon) {
        [icon setTemplate:YES];
        [proxy_button_ setImage:icon];
      }

      LOG_DEBUG("UI", "Proxy button updated successfully");
    });
  }
}

#endif // OS_MACOS
