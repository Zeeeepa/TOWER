#include "owl_dev_console.h"
#include "owl_dev_elements.h"
#include "owl_dev_network.h"
#include "owl_client.h"
#include "../resources/icons/icons.h"
#include "logger.h"
#include "include/cef_browser.h"
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include <Cocoa/Cocoa.h>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <set>
#include <algorithm>
#include <vector>

// Window delegate to handle close events
@interface DevConsoleWindowDelegate : NSObject <NSWindowDelegate>
@property (assign) OwlDevConsole* console;
@end

@implementation DevConsoleWindowDelegate
- (BOOL)windowShouldClose:(NSWindow*)window {
  // Instead of closing, just hide the window
  if (_console) {
    _console->Hide();
  }
  return NO;  // Don't actually close the window
}
@end

// Simple client for the dev console - just handles browser lifecycle
class DevConsoleClient : public CefClient,
                         public CefLifeSpanHandler,
                         public CefDisplayHandler,
                         public CefKeyboardHandler {
public:
  DevConsoleClient(OwlDevConsole* console) : console_(console) {}

  // CefClient methods
  virtual CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
    return this;
  }

  virtual CefRefPtr<CefDisplayHandler> GetDisplayHandler() override {
    return this;
  }

  virtual CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override {
    return this;
  }

  // CefLifeSpanHandler methods
  virtual void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    if (console_) {
      console_->SetBrowser(browser);
      LOG_DEBUG("DevConsole", "Browser created and registered with dev console");

      // Load any existing messages that were captured before console was opened
      console_->RefreshConsoleUI();
    }
  }

  virtual void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    if (console_) {
      console_->SetBrowser(nullptr);
    }
  }

  // CefDisplayHandler methods - capture console messages from dev console page itself
  virtual bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                cef_log_severity_t level,
                                const CefString& message,
                                const CefString& source,
                                int line) override {
    if (console_) {
      std::string console_level;
      switch (level) {
        case LOGSEVERITY_DEBUG: console_level = "debug"; break;
        case LOGSEVERITY_INFO: console_level = "info"; break;
        case LOGSEVERITY_WARNING: console_level = "warn"; break;
        case LOGSEVERITY_ERROR: console_level = "error"; break;
        default: console_level = "log"; break;
      }

      console_->AddConsoleMessage(
          console_level,
          message.ToString(),
          source.ToString(),
          line
      );
    }
    return false;  // Allow default console output
  }

  // CefKeyboardHandler methods - handle keyboard shortcuts
  virtual bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                             const CefKeyEvent& event,
                             CefEventHandle os_event,
                             bool* is_keyboard_shortcut) override {
    // Handle keyboard shortcuts
    if (event.type == KEYEVENT_RAWKEYDOWN) {
      // Cmd+C or Ctrl+C: Copy
      if ((event.modifiers & EVENTFLAG_COMMAND_DOWN || event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
          event.windows_key_code == 'C') {
        browser->GetFocusedFrame()->Copy();
        LOG_DEBUG("DevConsole", "Copy shortcut triggered");
        return true;
      }

      // Cmd+V or Ctrl+V: Paste
      if ((event.modifiers & EVENTFLAG_COMMAND_DOWN || event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
          event.windows_key_code == 'V') {
        browser->GetFocusedFrame()->Paste();
        LOG_DEBUG("DevConsole", "Paste shortcut triggered");
        return true;
      }

      // Cmd+X or Ctrl+X: Cut
      if ((event.modifiers & EVENTFLAG_COMMAND_DOWN || event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
          event.windows_key_code == 'X') {
        browser->GetFocusedFrame()->Cut();
        LOG_DEBUG("DevConsole", "Cut shortcut triggered");
        return true;
      }

      // Cmd+A or Ctrl+A: Select All
      if ((event.modifiers & EVENTFLAG_COMMAND_DOWN || event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
          event.windows_key_code == 'A') {
        browser->GetFocusedFrame()->SelectAll();
        LOG_DEBUG("DevConsole", "Select All shortcut triggered");
        return true;
      }

      // Cmd+Z or Ctrl+Z: Undo
      if ((event.modifiers & EVENTFLAG_COMMAND_DOWN || event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
          event.windows_key_code == 'Z') {
        browser->GetFocusedFrame()->Undo();
        LOG_DEBUG("DevConsole", "Undo shortcut triggered");
        return true;
      }

      // Cmd+Shift+Z or Ctrl+Y: Redo
      if (((event.modifiers & EVENTFLAG_COMMAND_DOWN) && (event.modifiers & EVENTFLAG_SHIFT_DOWN) &&
           event.windows_key_code == 'Z') ||
          ((event.modifiers & EVENTFLAG_CONTROL_DOWN) && event.windows_key_code == 'Y')) {
        browser->GetFocusedFrame()->Redo();
        LOG_DEBUG("DevConsole", "Redo shortcut triggered");
        return true;
      }
    }

    return false;  // Let other events pass through
  }

private:
  OwlDevConsole* console_;
  IMPLEMENT_REFCOUNTING(DevConsoleClient);
};

OwlDevConsole* OwlDevConsole::instance_ = nullptr;

// C wrapper for weak linking from helper processes
extern "C" void OwlDevConsole_AddMessage(const char* level, const char* message, const char* source, int line) {
  OwlDevConsole* console = OwlDevConsole::GetInstance();
  if (console) {
    console->AddConsoleMessage(
        std::string(level),
        std::string(message),
        std::string(source),
        line
    );
  }
}

extern "C" void OwlDevConsole_AddNetworkRequest(
    const char* url, const char* method, const char* type,
    int status_code, const char* status_text, size_t size, int duration_ms) {
  OwlDevConsole* console = OwlDevConsole::GetInstance();
  if (console) {
    console->AddNetworkRequest(
        std::string(url),
        std::string(method),
        std::string(type),
        status_code,
        std::string(status_text),
        size,
        duration_ms
    );
  }
}

extern "C" void OwlDevConsole_AddNetworkRequestExtended(
    const char* url, const char* method, const char* type,
    int status_code, const char* status_text, size_t size, int duration_ms,
    const char* request_headers, const char* response_headers,
    const char* url_params, const char* post_data) {
  OwlDevConsole* console = OwlDevConsole::GetInstance();
  if (console) {
    console->AddNetworkRequestExtended(
        std::string(url),
        std::string(method),
        std::string(type),
        status_code,
        std::string(status_text),
        size,
        duration_ms,
        std::string(request_headers),
        std::string(response_headers),
        std::string(url_params),
        std::string(post_data)
    );
  }
}

OwlDevConsole::OwlDevConsole()
    : window_(nullptr),
      browser_(nullptr),
      main_browser_(nullptr),
      is_visible_(false) {
  LOG_DEBUG("DevConsole", "Developer Console initialized");
}

OwlDevConsole::~OwlDevConsole() {
  if (window_) {
    // Don't close on Hide(), only on destructor
    if (browser_) {
      browser_->GetHost()->CloseBrowser(true);
    }
    [window_ close];
    [window_ release];
    window_ = nullptr;
  }
}

OwlDevConsole* OwlDevConsole::GetInstance() {
  if (!instance_) {
    instance_ = new OwlDevConsole();
  }
  return instance_;
}

void OwlDevConsole::Show() {
  if (is_visible_) {
    // Already visible, just bring to front
    if (window_) {
      [window_ makeKeyAndOrderFront:nil];
    }
    return;
  }

  LOG_DEBUG("DevConsole", "Creating developer console window");

  // Create window
  NSRect frame = NSMakeRect(100, 100, 1000, 600);
  NSWindow* nsWindow = [[NSWindow alloc]
      initWithContentRect:frame
      styleMask:(NSWindowStyleMaskTitled |
                 NSWindowStyleMaskClosable |
                 NSWindowStyleMaskMiniaturizable |
                 NSWindowStyleMaskResizable)
      backing:NSBackingStoreBuffered
      defer:NO];

  [nsWindow setTitle:@"Developer Console"];
  [nsWindow setBackgroundColor:[NSColor colorWithRed:0.15 green:0.15 blue:0.15 alpha:1.0]];

  // Set window delegate to handle close events (hide instead of close)
  DevConsoleWindowDelegate* delegate = [[DevConsoleWindowDelegate alloc] init];
  delegate.console = this;
  [nsWindow setDelegate:delegate];

  // Retain the window (manual memory management, no ARC)
  [nsWindow retain];
  window_ = nsWindow;

  // Get the content view for CEF browser
  NSView* contentView = [nsWindow contentView];

  // Create CEF browser as child of window
  CefWindowInfo window_info;
  NSRect contentRect = [contentView bounds];
  window_info.SetAsChild(contentView, CefRect(0, 0, contentRect.size.width, contentRect.size.height));

  // Create a simple client for the dev console
  CefRefPtr<DevConsoleClient> client = new DevConsoleClient(this);

  // Browser settings - enable console messages
  CefBrowserSettings browser_settings;
  browser_settings.javascript_close_windows = STATE_DISABLED;  // Prevent closing

  // Add test messages to show the console is working
  AddConsoleMessage("info", "Developer Console initialized and ready", "DevConsole", 0);
  AddConsoleMessage("log", "Console messages from web pages will appear here", "DevConsole", 0);

  // Use owl:// scheme instead of data URI (data URIs don't trigger CEF load events)
  std::string url = "owl://devconsole.html";

  // Create browser asynchronously - it will call back when created
  CefBrowserHost::CreateBrowser(window_info, client.get(), url, browser_settings, nullptr, nullptr);

  // Show the window
  [nsWindow makeKeyAndOrderFront:nil];
  is_visible_ = true;

  LOG_DEBUG("DevConsole", "Developer console window and browser created");
}

void OwlDevConsole::Hide() {
  if (window_) {
    [window_ orderOut:nil];
    is_visible_ = false;
  }
}

void OwlDevConsole::Toggle() {
  if (is_visible_) {
    Hide();
  } else {
    Show();
  }
}

bool OwlDevConsole::IsVisible() const {
  return is_visible_;
}

void OwlDevConsole::AddConsoleMessage(const std::string& level,
                                      const std::string& message,
                                      const std::string& source,
                                      int line) {
  // Check for special clear console command
  if (message == "__OLIB_CLEAR_CONSOLE__") {
    LOG_DEBUG("DevConsole", "Clear console command received");
    ClearConsole();
    return;  // Don't add this message to the console
  }

  // Check for special execute command
  if (message.substr(0, 13) == "__OLIB_EXEC__") {
    std::string code = message.substr(13);  // Remove prefix
    LOG_DEBUG("DevConsole", "Execute command received: " + code);
    ExecuteInMainBrowser(code);
    return;  // Don't add this internal command to the console
  }

  // Check for special refresh elements command
  if (message == "__OLIB_REFRESH_ELEMENTS__") {
    LOG_DEBUG("DevConsole", "Refresh elements command received");
    RefreshElementsTab();
    return;  // Don't add this internal command to the console
  }

  // Lock scope for adding message
  {
    std::lock_guard<std::mutex> lock(messages_mutex_);

    ConsoleMessage msg;
    msg.level = level;
    msg.message = message;
    msg.source = source;
    msg.line = line;
    msg.timestamp = GetTimestamp();

    messages_.push_back(msg);

    // Limit to last 1000 messages
    if (messages_.size() > 1000) {
      messages_.erase(messages_.begin());
    }

    LOG_DEBUG("DevConsole", "Console message received [" + level + "]: " + message +
             " (total messages: " + std::to_string(messages_.size()) +
             ", visible: " + std::to_string(is_visible_) +
             ", browser: " + std::to_string(browser_ != nullptr) + ")");
  }  // Unlock mutex before calling UpdateConsoleUI

  // Update UI if console is open (call AFTER releasing the lock to avoid deadlock)
  if (is_visible_ && browser_) {
    LOG_DEBUG("DevConsole", "Calling UpdateConsoleUI");
    UpdateConsoleUI();
  } else {
    LOG_WARN("DevConsole", "Not updating UI - visible=" + std::to_string(is_visible_) +
             " browser=" + std::to_string(browser_ != nullptr));
  }
}

void OwlDevConsole::ClearConsole() {
  {
    std::lock_guard<std::mutex> lock(messages_mutex_);
    messages_.clear();
  }  // Unlock before calling UpdateConsoleUI

  if (is_visible_ && browser_) {
    UpdateConsoleUI();
  }
}

void OwlDevConsole::RefreshElementsTab() {
  if (!browser_ || !main_browser_) {
    LOG_ERROR("DevConsole", "Cannot refresh elements - browser not available");
    return;
  }

  LOG_DEBUG("DevConsole", "Refreshing Elements tab - using chunked extraction");

  auto frame = main_browser_->GetMainFrame();
  if (!frame) {
    LOG_ERROR("DevConsole", "Main browser has no frame");
    return;
  }

  // JavaScript that sends DOM data in chunks via process messages
  std::string extraction_script = R"(
    (function() {
      const CHUNK_SIZE = 100;  // Send 100 elements at a time
      let elementCount = 0;
      const MAX_ELEMENTS = 50000;  // Increased limit for larger pages

      // Void elements
      const voidElements = new Set([
        'area', 'base', 'br', 'col', 'embed', 'hr', 'img', 'input',
        'link', 'meta', 'param', 'source', 'track', 'wbr'
      ]);

      function extractElement(element, depth) {
        if (elementCount >= MAX_ELEMENTS) return null;
        elementCount++;

        // Get className safely
        let classStr = '';
        if (element.className) {
          if (typeof element.className === 'string') {
            classStr = element.className;
          } else if (element.className.baseVal !== undefined) {
            classStr = element.className.baseVal;
          } else if (element.classList) {
            classStr = Array.from(element.classList).join(' ');
          }
        }

        // Get text content (increased limit)
        let textPreview = '';
        for (let node of element.childNodes) {
          if (node.nodeType === 3) {
            let text = node.textContent.trim();
            if (text) {
              textPreview += text;
              if (textPreview.length > 1000) {
                textPreview = textPreview.substring(0, 1000) + '...';
                break;
              }
            }
          }
        }

        const tagName = element.tagName ? element.tagName.toLowerCase() : 'unknown';

        // Get all attributes (exclude id and class)
        const attrs = [];
        if (element.attributes) {
          for (let attr of element.attributes) {
            if (attr.name !== 'id' && attr.name !== 'class') {
              attrs.push(attr.name + '="' + attr.value + '"');
            }
          }
        }

        return {
          tag: tagName,
          id: element.id || '',
          classes: classStr,
          text: textPreview,
          depth: depth,
          isVoid: voidElements.has(tagName),
          childCount: element.children.length,
          attrs: attrs
        };
      }

      function traverseDOM(element, depth = 0, maxDepth = 150) {
        if (depth > maxDepth || elementCount >= MAX_ELEMENTS) return [];

        const elements = [];
        if (element.nodeType === 1) {
          const extracted = extractElement(element, depth);
          if (extracted) {
            elements.push(extracted);
            for (let child of element.children) {
              if (elementCount >= MAX_ELEMENTS) break;
              elements.push(...traverseDOM(child, depth + 1, maxDepth));
            }
          }
        }
        return elements;
      }

      try {
        console.log('[DOM Extraction] Starting...');
        const allElements = traverseDOM(document.documentElement);
        console.log('[DOM Extraction] Extracted ' + allElements.length + ' elements, sending in chunks...');

        // Send in chunks
        for (let i = 0; i < allElements.length; i += CHUNK_SIZE) {
          const chunk = allElements.slice(i, i + CHUNK_SIZE);
          const isLast = (i + CHUNK_SIZE) >= allElements.length;

          // Encode chunk as base64 to avoid JSON escaping issues
          // Use proper UTF-8 to base64 encoding for all Unicode characters
          const chunkJson = JSON.stringify(chunk);

          // Convert UTF-8 string to base64 properly
          const utf8Bytes = new TextEncoder().encode(chunkJson);
          let binary = '';
          for (let i = 0; i < utf8Bytes.length; i++) {
            binary += String.fromCharCode(utf8Bytes[i]);
          }
          const chunkBase64 = btoa(binary);

          const message = {
            index: i,
            total: allElements.length,
            isLast: isLast,
            chunkBase64: chunkBase64
          };

          if (typeof _2 !== 'undefined') {
            _2('dom_elements_chunk', JSON.stringify(message));
          }
        }

        console.log('[DOM Extraction] Complete, sent ' + allElements.length + ' elements');
      } catch (e) {
        console.error('[DOM Extraction] Failed:', e);
        if (typeof _2 !== 'undefined') {
          _2('dom_elements_error', e.toString());
        }
      }
    })();
  )";

  frame->ExecuteJavaScript(extraction_script, frame->GetURL(), 0);
  LOG_DEBUG("DevConsole", "DOM extraction script injected");
}

void OwlDevConsole::UpdateElementsTree(const std::string& dom_json) {
  if (!browser_) {
    LOG_ERROR("DevConsole", "Cannot update elements tree - dev console browser not available");
    return;
  }

  LOG_DEBUG("DevConsole", "Updating Elements tree with " + std::to_string(dom_json.length()) + " bytes of data");

  auto frame = browser_->GetMainFrame();
  if (!frame) {
    LOG_ERROR("DevConsole", "Dev console browser has no frame");
    return;
  }

  // Encode as base64 to avoid escaping issues
  CefRefPtr<CefBinaryValue> binary = CefBinaryValue::Create(dom_json.data(), dom_json.size());
  CefString base64_str = CefBase64Encode(binary->GetRawData(), binary->GetSize());
  std::string base64 = base64_str.ToString();

  // Pass base64 encoded data to JavaScript and decode it there
  std::string update_script = R"(
    if (typeof updateDOMTree === 'function') {
      try {
        const base64 = ')" + base64 + R"(';
        const binary = atob(base64);
        const bytes = new Uint8Array(binary.length);
        for (let i = 0; i < binary.length; i++) {
          bytes[i] = binary.charCodeAt(i);
        }
        const jsonStr = new TextDecoder().decode(bytes);
        updateDOMTree(jsonStr);
      } catch (e) {
        console.error('Failed to decode DOM tree:', e);
      }
    }
  )";

  frame->ExecuteJavaScript(update_script, frame->GetURL(), 0);
  LOG_DEBUG("DevConsole", "Elements tree update script executed");
}

void OwlDevConsole::AddNetworkRequest(const std::string& url,
                                       const std::string& method,
                                       const std::string& type,
                                       int status_code,
                                       const std::string& status_text,
                                       size_t size,
                                       int duration_ms) {
  if (!browser_) {
    return;  // Silently ignore if dev console not open
  }

  auto frame = browser_->GetMainFrame();
  if (!frame) {
    return;
  }

  // Build JSON for network request
  std::ostringstream json;
  json << "{";
  json << "\"url\":\"" << url << "\",";
  json << "\"method\":\"" << method << "\",";
  json << "\"type\":\"" << type << "\",";
  json << "\"status\":" << status_code << ",";
  json << "\"statusText\":\"" << status_text << "\",";
  json << "\"size\":" << size << ",";
  json << "\"duration\":" << duration_ms;
  json << "}";

  std::string request_json = json.str();

  // Escape for JavaScript
  std::string escaped_json = request_json;
  size_t pos = 0;
  while ((pos = escaped_json.find("\\", pos)) != std::string::npos) {
    escaped_json.replace(pos, 1, "\\\\");
    pos += 2;
  }
  pos = 0;
  while ((pos = escaped_json.find("\"", pos)) != std::string::npos) {
    escaped_json.replace(pos, 1, "\\\"");
    pos += 2;
  }

  std::string update_script = "if (typeof addNetworkRequest === 'function') { addNetworkRequest(\"" + escaped_json + "\"); }";

  frame->ExecuteJavaScript(update_script, frame->GetURL(), 0);
}

void OwlDevConsole::AddNetworkRequestExtended(const std::string& url,
                                               const std::string& method,
                                               const std::string& type,
                                               int status_code,
                                               const std::string& status_text,
                                               size_t size,
                                               int duration_ms,
                                               const std::string& request_headers,
                                               const std::string& response_headers,
                                               const std::string& url_params,
                                               const std::string& post_data) {
  if (!browser_) {
    return;  // Silently ignore if dev console not open
  }

  auto frame = browser_->GetMainFrame();
  if (!frame) {
    return;
  }

  // Helper function to escape JSON strings
  auto escapeJson = [](const std::string& s) -> std::string {
    std::string result;
    result.reserve(s.length());
    for (char c : s) {
      switch (c) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
          if (c < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
            result += buf;
          } else {
            result += c;
          }
      }
    }
    return result;
  };

  // Build JSON for network request with extended data
  std::ostringstream json;
  json << "{";
  json << "\"url\":\"" << escapeJson(url) << "\",";
  json << "\"method\":\"" << escapeJson(method) << "\",";
  json << "\"type\":\"" << escapeJson(type) << "\",";
  json << "\"status\":" << status_code << ",";
  json << "\"statusText\":\"" << escapeJson(status_text) << "\",";
  json << "\"size\":" << size << ",";
  json << "\"duration\":" << duration_ms << ",";
  json << "\"requestHeaders\":" << request_headers << ",";  // Already JSON
  json << "\"responseHeaders\":" << response_headers << ",";  // Already JSON
  json << "\"urlParams\":\"" << escapeJson(url_params) << "\",";
  json << "\"postData\":\"" << escapeJson(post_data) << "\"";
  json << "}";

  std::string request_json = json.str();

  // Escape for JavaScript
  std::string escaped_json = escapeJson(request_json);

  std::string update_script = "if (typeof addNetworkRequest === 'function') { addNetworkRequest(\"" + escaped_json + "\"); }";

  frame->ExecuteJavaScript(update_script, frame->GetURL(), 0);
}

void OwlDevConsole::RefreshConsoleUI() {
  LOG_DEBUG("DevConsole", "RefreshConsoleUI called - will update UI after page loads");

  // Wait a bit for the page to load, then update UI
  // Use a timer to delay the update
  std::thread([this]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    LOG_DEBUG("DevConsole", "RefreshConsoleUI: Updating UI with existing messages");
    if (browser_) {
      UpdateConsoleUI();
    } else {
      LOG_ERROR("DevConsole", "RefreshConsoleUI: Browser became null!");
    }
  }).detach();
}

std::string OwlDevConsole::GetTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch()) % 1000;
  auto timer = std::chrono::system_clock::to_time_t(now);
  std::tm bt = *std::localtime(&timer);

  std::ostringstream oss;
  oss << std::put_time(&bt, "%H:%M:%S");
  oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
  return oss.str();
}

// Helper function to escape strings for JavaScript
std::string EscapeJavaScriptString(const std::string& str) {
  std::ostringstream escaped;
  for (char c : str) {
    switch (c) {
      case '\'': escaped << "\\'"; break;
      case '\"': escaped << "\\\""; break;
      case '\\': escaped << "\\\\"; break;
      case '\n': escaped << "\\n"; break;
      case '\r': escaped << "\\r"; break;
      case '\t': escaped << "\\t"; break;
      default: escaped << c; break;
    }
  }
  return escaped.str();
}

void OwlDevConsole::UpdateConsoleUI() {
  if (!browser_) {
    LOG_ERROR("DevConsole", "UpdateConsoleUI: browser is null!");
    return;
  }

  CefRefPtr<CefFrame> frame = browser_->GetMainFrame();
  if (!frame) {
    LOG_ERROR("DevConsole", "UpdateConsoleUI: frame is null!");
    return;
  }

  std::lock_guard<std::mutex> lock(messages_mutex_);
  LOG_DEBUG("DevConsole", "UpdateConsoleUI: Building JavaScript for " +
           std::to_string(messages_.size()) + " messages");

  // Build JavaScript to update the console
  std::ostringstream js;
  js << "if (typeof updateConsoleMessages === 'function') { updateConsoleMessages([";

  for (size_t i = 0; i < messages_.size(); i++) {
    const auto& msg = messages_[i];

    if (i > 0) js << ",";
    js << "{";
    js << "level:'" << EscapeJavaScriptString(msg.level) << "',";
    js << "message:'" << EscapeJavaScriptString(msg.message) << "',";
    js << "source:'" << EscapeJavaScriptString(msg.source) << "',";
    js << "line:" << msg.line << ",";
    js << "timestamp:'" << EscapeJavaScriptString(msg.timestamp) << "'";
    js << "}";
  }

  js << "]); } else { console.log('ERROR: updateConsoleMessages function not found!'); }";

  std::string js_str = js.str();
  LOG_DEBUG("DevConsole", "Executing JavaScript (first 300 chars): " +
           js_str.substr(0, std::min((size_t)300, js_str.length())));
  frame->ExecuteJavaScript(js_str, frame->GetURL(), 0);
}

std::string OwlDevConsole::GenerateHTML() {
  // Escape FA icons for JavaScript
  std::string iconLog = EscapeJavaScriptString(OlibIcons::CIRCLE);
  std::string iconInfo = EscapeJavaScriptString(OlibIcons::CIRCLE_INFO);
  std::string iconWarn = EscapeJavaScriptString(OlibIcons::TRIANGLE_EXCLAMATION);
  std::string iconError = EscapeJavaScriptString(OlibIcons::XMARK);
  std::string iconDebug = EscapeJavaScriptString(OlibIcons::BUG);

  // Build HTML with embedded FA icons
  std::ostringstream html;

  html << R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Developer Console</title>
  <style>
    * {
      margin: 0;
      padding: 0;
      box-sizing: border-box;
    }

    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', 'Monaco', monospace;
      background: #1e1e1e;
      color: #cccccc;
      height: 100vh;
      display: flex;
      flex-direction: column;
      overflow: hidden;
    }

    /* TAB BAR - Structure for future tabs */
    /* To add more tabs:
       1. Add tab button in HTML: <div class="tab" onclick="switchTab('network')">Network</div>
       2. Add tab content div: <div class="tab-content" id="network-tab">...</div>
       3. Implement switchTab() function
       4. Add tab-specific update functions
    */
    .tab-bar {
      background: #252526;
      border-bottom: 1px solid #3c3c3c;
      display: flex;
      padding: 0 8px;
      min-height: 35px;
    }

    .tab {
      padding: 8px 16px;
      cursor: pointer;
      border-bottom: 2px solid transparent;
      color: #cccccc;
      font-size: 13px;
      user-select: none;
    }

    .tab:hover {
      background: #2a2a2a;
    }

    .tab.active {
      border-bottom-color: #007acc;
      color: #ffffff;
    }

    /* TOOLBAR */
    .toolbar {
      background: #252526;
      border-bottom: 1px solid #3c3c3c;
      padding: 8px 12px;
      display: flex;
      align-items: center;
      gap: 8px;
    }

    .btn {
      background: #0e639c;
      color: #fff;
      border: none;
      padding: 6px 12px;
      border-radius: 3px;
      cursor: pointer;
      font-size: 12px;
      display: flex;
      align-items: center;
      gap: 4px;
    }

    .btn:hover {
      background: #1177bb;
    }

    .btn-secondary {
      background: #3c3c3c;
    }

    .btn-secondary:hover {
      background: #505050;
    }

    /* CONSOLE CONTENT */
    .tab-content {
      display: none;
      flex: 1;
      overflow: hidden;
    }

    .tab-content.active {
      display: flex;
      flex-direction: column;
    }

    #console-messages {
      flex: 1;
      overflow-y: auto;
      font-family: 'Monaco', 'Menlo', 'Consolas', monospace;
      font-size: 12px;
      padding: 4px 0;
    }

    .console-message {
      padding: 2px 12px;
      border-bottom: 1px solid #2d2d30;
      display: flex;
      align-items: flex-start;
      gap: 8px;
    }

    .console-message:hover {
      background: #2a2a2a;
    }

    .console-timestamp {
      color: #858585;
      font-size: 11px;
      white-space: nowrap;
      flex-shrink: 0;
    }

    .console-icon {
      flex-shrink: 0;
      width: 16px;
      height: 16px;
      margin-top: 2px;
    }

    .console-content {
      flex: 1;
      word-wrap: break-word;
      line-height: 1.4;
    }

    .console-source {
      color: #858585;
      font-size: 11px;
      margin-top: 2px;
    }

    .console-message.log .console-content { color: #cccccc; }
    .console-message.info .console-content { color: #3794ff; }
    .console-message.warn .console-content { color: #cca700; }
    .console-message.error .console-content { color: #f48771; }
    .console-message.debug .console-content { color: #b267e6; }

    .empty-state {
      display: flex;
      align-items: center;
      justify-content: center;
      height: 100%;
      color: #858585;
      font-size: 13px;
    }

    /* FILTER BAR */
    .filter-bar {
      background: #252526;
      border-bottom: 1px solid #3c3c3c;
      padding: 6px 12px;
      display: flex;
      gap: 12px;
      align-items: center;
    }

    .filter-label {
      color: #cccccc;
      font-size: 12px;
    }

    .filter-checkbox {
      display: flex;
      align-items: center;
      gap: 4px;
      cursor: pointer;
      font-size: 12px;
    }

    .filter-checkbox input {
      cursor: pointer;
    }

    .filter-input {
      background: #3c3c3c;
      border: 1px solid #555555;
      color: #cccccc;
      padding: 4px 8px;
      border-radius: 3px;
      font-size: 12px;
      font-family: 'Monaco', 'Menlo', 'Consolas', monospace;
      outline: none;
      width: 200px;
    }

    .filter-input:focus {
      border-color: #007acc;
      background: #2d2d30;
    }

    .filter-input::placeholder {
      color: #858585;
    }

    /* CONSOLE INPUT */
    .console-input-container {
      background: #1e1e1e;
      border-top: 1px solid #3c3c3c;
      padding: 8px 12px;
      display: flex;
      align-items: flex-start;
      gap: 8px;
    }

    .console-prompt {
      color: #3794ff;
      font-family: 'Monaco', 'Menlo', 'Consolas', monospace;
      font-size: 14px;
      font-weight: bold;
      margin-top: 8px;
    }

    .console-input-wrapper {
      flex: 1;
      display: flex;
      gap: 8px;
      align-items: flex-end;
    }

    .console-input-field {
      flex: 1;
      background: #2d2d30;
      border: 1px solid #3c3c3c;
      color: #cccccc;
      padding: 8px 10px;
      border-radius: 3px;
      font-size: 13px;
      font-family: 'Monaco', 'Menlo', 'Consolas', monospace;
      outline: none;
      resize: vertical;
      min-height: 28px;
      max-height: 150px;
      line-height: 1.4;
    }

    .console-input-field:focus {
      border-color: #007acc;
      background: #252526;
    }

    .console-input-field::placeholder {
      color: #6a6a6a;
    }

    .console-execute-btn {
      background: #0e639c;
      color: #fff;
      border: none;
      padding: 6px 16px;
      border-radius: 3px;
      cursor: pointer;
      font-size: 12px;
      height: 30px;
      white-space: nowrap;
    }

    .console-execute-btn:hover {
      background: #1177bb;
    }

    .console-copy-btn {
      opacity: 0;
      background: #3c3c3c;
      border: none;
      color: #cccccc;
      padding: 2px 8px;
      border-radius: 3px;
      cursor: pointer;
      font-size: 11px;
      margin-left: 8px;
    }

    .console-message:hover .console-copy-btn {
      opacity: 1;
    }

    .console-copy-btn:hover {
      background: #505050;
    }

    .console-object {
      white-space: pre-wrap;
      font-family: 'Monaco', 'Menlo', 'Consolas', monospace;
      background: #252526;
      padding: 8px;
      border-radius: 3px;
      margin-top: 4px;
    }
  </style>
</head>
<body>
  <!-- TAB BAR -->
  <div class="tab-bar">
    <div class="tab active" onclick="switchTab('console')">Console</div>
    <div class="tab" onclick="switchTab('elements')">Elements</div>
    <div class="tab" onclick="switchTab('network')">Network</div>
  </div>

  <!-- TOOLBAR -->
  <div class="toolbar">
    <button class="btn btn-secondary" onclick="clearConsole()">
      )HTML" + std::string(OlibIcons::TRASH) + R"HTML(
      Clear
    </button>
    <input type="text" id="text-filter" class="filter-input" placeholder="Filter text..." oninput="applyTextFilter()">
    <div style="flex: 1;"></div>
    <div class="filter-label">Level:</div>
    <label class="filter-checkbox">
      <input type="checkbox" checked onchange="toggleFilter('log')"> Log
    </label>
    <label class="filter-checkbox">
      <input type="checkbox" checked onchange="toggleFilter('info')"> Info
    </label>
    <label class="filter-checkbox">
      <input type="checkbox" checked onchange="toggleFilter('warn')"> Warn
    </label>
    <label class="filter-checkbox">
      <input type="checkbox" checked onchange="toggleFilter('error')"> Error
    </label>
  </div>

  <!-- CONSOLE TAB -->
  <div class="tab-content active" id="console-tab">
    <div id="console-messages">
      <div class="empty-state">No console messages yet</div>
    </div>
    <div class="console-input-container">
      <span class="console-prompt">&gt;</span>
      <div class="console-input-wrapper">
        <textarea id="console-input" class="console-input-field" placeholder="Execute JavaScript (Shift+Enter for new line, Enter to execute)" rows="1" onkeydown="handleConsoleInput(event)" oninput="autoResize(this)"></textarea>
        <button class="console-execute-btn" onclick="executeConsoleCommand()">Execute</button>
      </div>
    </div>
  </div>

  <!-- ELEMENTS TAB -->
  <div class="tab-content" id="elements-tab">
    )HTML";

  // Add Elements tab content
  OwlDevElements elements_tab;
  html << elements_tab.GenerateHTML();

  html << R"HTML(
  </div>

  <!-- NETWORK TAB -->
  <div class="tab-content" id="network-tab">
    )HTML";

  // Add Network tab content
  OwlDevNetwork network_tab;
  html << network_tab.GenerateHTML();

  html << R"HTML(
  </div>

  <script>
    let messages = [];
    let filters = { log: true, info: true, warn: true, error: true, debug: true };

    function switchTab(tabName) {
      // Remove active class from all tabs
      document.querySelectorAll('.tab').forEach(tab => tab.classList.remove('active'));
      document.querySelectorAll('.tab-content').forEach(content => content.classList.remove('active'));

      // Add active class to selected tab
      event.target.classList.add('active');
      document.getElementById(tabName + '-tab').classList.add('active');
    }

    function clearConsole() {
      // Send signal to C++ backend to clear its stored messages
      console.log('__OLIB_CLEAR_CONSOLE__');

      // Also clear local messages immediately for instant feedback
      messages = [];
      renderMessages();
    }

    function toggleFilter(level) {
      filters[level] = !filters[level];
      renderMessages();
    }

    function applyTextFilter() {
      renderMessages();
    }

    function updateConsoleMessages(newMessages) {
      messages = newMessages;
      renderMessages();
    }

    function renderMessages() {
      const container = document.getElementById('console-messages');

      if (messages.length === 0) {
        container.innerHTML = '<div class="empty-state">No console messages yet</div>';
        return;
      }

      // Filter by level
      let filtered = messages.filter(msg => filters[msg.level]);

      // Filter by text if text filter is not empty
      const textFilter = document.getElementById('text-filter').value.trim().toLowerCase();
      if (textFilter) {
        filtered = filtered.filter(msg => {
          const message = msg.message.toLowerCase();
          const source = (msg.source || '').toLowerCase();
          return message.includes(textFilter) || source.includes(textFilter);
        });
      }

      if (filtered.length === 0) {
        container.innerHTML = '<div class="empty-state">No messages match the current filter</div>';
        return;
      }

      const html = filtered.map((msg, index) => {
        const icon = getIconForLevel(msg.level);
        const sourceText = msg.source ? `${msg.source}:${msg.line}` : '';

        // Check if message looks like an object/array (starts with { or [)
        const isObject = msg.message.trim().startsWith('{') || msg.message.trim().startsWith('[');
        const messageClass = isObject ? 'console-object' : '';

        return `
          <div class="console-message ${msg.level}">
            <span class="console-timestamp">${msg.timestamp}</span>
            <span class="console-icon">${icon}</span>
            <div class="console-content">
              <div class="${messageClass}">${escapeHtml(msg.message)}</div>
              ${sourceText ? `<div class="console-source">${escapeHtml(sourceText)}</div>` : ''}
            </div>
            <button class="console-copy-btn" onclick="copyToClipboard(\`${escapeHtml(msg.message).replace(/`/g, '\\`')}\`)" title="Copy">Copy</button>
          </div>
        `;
      }).join('');

      container.innerHTML = html;

      // Auto-scroll to bottom
      container.scrollTop = container.scrollHeight;
    }

    // FontAwesome icons for console levels
    const ICON_LOG = )HTML";
  html << "'" << iconLog << "'";
  html << R"HTML(;
    const ICON_INFO = )HTML";
  html << "'" << iconInfo << "'";
  html << R"HTML(;
    const ICON_WARN = )HTML";
  html << "'" << iconWarn << "'";
  html << R"HTML(;
    const ICON_ERROR = )HTML";
  html << "'" << iconError << "'";
  html << R"HTML(;
    const ICON_DEBUG = )HTML";
  html << "'" << iconDebug << "'";
  html << R"HTML(;

    function getIconForLevel(level) {
      const icons = {
        log: ICON_LOG,
        info: ICON_INFO,
        warn: ICON_WARN,
        error: ICON_ERROR,
        debug: ICON_DEBUG
      };
      return icons[level] || ICON_LOG;
    }

    function escapeHtml(text) {
      const div = document.createElement('div');
      div.textContent = text;
      return div.innerHTML;
    }

    function autoResize(textarea) {
      textarea.style.height = 'auto';
      textarea.style.height = Math.min(textarea.scrollHeight, 150) + 'px';
    }

    function executeConsoleCommand() {
      const input = document.getElementById('console-input');
      const code = input.value.trim();

      if (code) {
        // Echo the command to console
        console.log('> ' + code);

        // Send command to C++ to execute in main browser
        console.log('__OLIB_EXEC__' + code);

        // Clear input and reset height
        input.value = '';
        input.style.height = 'auto';
      }
    }

    function handleConsoleInput(event) {
      // Shift+Enter: Add new line
      if (event.key === 'Enter' && event.shiftKey) {
        // Allow default behavior (new line)
        return;
      }

      // Enter (without Shift): Execute command
      if (event.key === 'Enter' && !event.shiftKey) {
        event.preventDefault();
        executeConsoleCommand();
      }

      // Cmd/Ctrl+C: Copy (browser default)
      // Cmd/Ctrl+V: Paste (browser default)
      // Cmd/Ctrl+X: Cut (browser default)
      // These work automatically in textarea
    }

    function copyToClipboard(text) {
      // Create temporary textarea
      const textarea = document.createElement('textarea');
      textarea.value = text;
      textarea.style.position = 'fixed';
      textarea.style.opacity = '0';
      document.body.appendChild(textarea);
      textarea.select();
      document.execCommand('copy');
      document.body.removeChild(textarea);
    }

    // Initial render
    renderMessages();
  </script>
</body>
</html>
)HTML";

  return html.str();
}

void OwlDevConsole::ExecuteInMainBrowser(const std::string& code) {
  if (!main_browser_) {
    AddConsoleMessage("error", "No main browser connected", "DevConsole", 0);
    LOG_ERROR("DevConsole", "Cannot execute - no main browser set");
    return;
  }

  CefRefPtr<CefFrame> frame = main_browser_->GetMainFrame();
  if (!frame) {
    AddConsoleMessage("error", "Main browser has no frame", "DevConsole", 0);
    LOG_ERROR("DevConsole", "Cannot execute - main browser has no frame");
    return;
  }

  // Use CefProcessMessage via _2 to bypass console.log blocking
  std::ostringstream wrapped;
  wrapped << "(function() {\n"
          << "  try {\n"
          << "    const __result = eval('" << EscapeJavaScriptString(code) << "');\n"
          << "    let __formatted;\n"
          << "    if (typeof __result === 'undefined') {\n"
          << "      __formatted = 'undefined';\n"
          << "    } else if (__result === null) {\n"
          << "      __formatted = 'null';\n"
          << "    } else if (typeof __result === 'object') {\n"
          << "      // Pretty print objects and arrays with indentation\n"
          << "      __formatted = JSON.stringify(__result, null, 2);\n"
          << "    } else {\n"
          << "      __formatted = String(__result);\n"
          << "    }\n"
          << "    if (typeof _2 !== 'undefined') {\n"
          << "      _2('dev_console_result', JSON.stringify({success: true, result: __formatted, isObject: typeof __result === 'object'}));\n"
          << "    } else {\n"
          << "      console.log('← ' + __formatted);\n"
          << "    }\n"
          << "  } catch (e) {\n"
          << "    if (typeof _2 !== 'undefined') {\n"
          << "      _2('dev_console_result', JSON.stringify({success: false, error: e.toString()}));\n"
          << "    } else {\n"
          << "      console.error('✗ ' + e.toString());\n"
          << "    }\n"
          << "  }\n"
          << "})();";

  std::string wrapped_code = wrapped.str();
  frame->ExecuteJavaScript(wrapped_code, frame->GetURL(), 0);
  LOG_DEBUG("DevConsole", "Executed in main browser: " + code);
  LOG_DEBUG("DevConsole", "Wrapped code: " + wrapped_code);
}
