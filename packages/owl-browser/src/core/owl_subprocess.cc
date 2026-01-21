#include "owl_app.h"
#include "owl_browser_manager.h"
#include "owl_client.h"
#include "owl_proxy_manager.h"
#include "owl_license.h"
#include "owl_ipc_server.h"
#include "owl_firewall_detector.h"
#include "owl_live_streamer.h"
#include "stealth/owl_virtual_machine.h"
#include "action_result.h"
#include "logger.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"
#ifdef OS_MACOS
#include "include/wrapper/cef_library_loader.h"
// Initialize headless NSApplication with CefAppProtocol - required for SendKeyEvent
extern "C" void InitializeHeadlessNSApplication();
#endif
#if defined(OS_LINUX) || defined(OS_MACOS)
#include <unistd.h>  // For readlink() (Linux) / POSIX APIs
#include <cstring>   // For strstr()
#endif
#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <chrono>
#include <condition_variable>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <future>

std::atomic<bool> should_quit(false);
std::queue<std::string> command_queue;
std::mutex queue_mutex;
std::condition_variable queue_cv;

// Thread-safe response output
std::mutex response_mutex;

// Batch mode flag - when true, operations skip internal CefDoMessageLoopWork
// Events queue up and get processed by single pump at end of batch
std::atomic<bool> g_batch_mode(false);

// Check if batch mode is active
bool IsBatchMode() {
  return g_batch_mode.load();
}

// Multi-IPC support (Linux and macOS)
#if defined(OS_LINUX) || defined(OS_MACOS)
// Thread-local response storage for multi-IPC mode
thread_local std::string tls_response;
thread_local bool tls_use_direct_response = false;

// Global multi-IPC server
std::unique_ptr<owl::IPCServer> g_ipc_server;

// Per-context command processing threads
std::unordered_map<std::string, std::thread> context_workers;
std::mutex context_workers_mutex;

// IPC command queue with response promise - commands must be processed on main thread
struct IPCCommand {
  std::string command;
  std::promise<std::string> response_promise;
};
std::queue<std::unique_ptr<IPCCommand>> ipc_command_queue;
std::mutex ipc_queue_mutex;
std::condition_variable ipc_queue_cv;
#endif

// Check if we're in multi-IPC mode
bool IsMultiIPCMode() {
#if defined(OS_LINUX) || defined(OS_MACOS)
    return g_ipc_server != nullptr;
#else
    return false;
#endif
}

// Simple JSON parser for commands
struct Command {
  int id;
  std::string method;
  std::string context_id;
  std::string url;
  std::string selector;
  std::string text;
  std::string key;           // For pressKey method
  std::string description;  // For AI methods and findElement
  std::string what;          // For AI extract
  std::string query;         // For AI query
  int max_results;           // For findElement
  std::string border_color;  // For highlight method
  std::string background_color;  // For highlight method
  // Content extraction fields
  std::string clean_level;    // For getHTML
  bool include_links;         // For getMarkdown
  bool include_images;        // For getMarkdown
  int max_length;             // For getMarkdown
  std::string template_name;  // For extractJSON
  std::string custom_schema;  // For extractJSON
  // Browser navigation & control fields
  bool ignore_cache;          // For reload method
  bool force_refresh;         // For summarizePage method
  int x;                      // For scroll methods
  int y;                      // For scroll methods
  int timeout;                // For wait methods
  int width;                  // For setViewport method
  int height;                 // For setViewport method
  // Video recording fields
  int fps;                    // For video recording
  std::string codec;          // For video recording codec
  int quality;                // For live streaming JPEG quality
  // CAPTCHA fields
  int max_attempts;           // For CAPTCHA solving methods
  std::string provider;       // For image CAPTCHA provider: auto, owl, recaptcha, cloudflare
  // LLM configuration fields (for createContext)
  bool llm_enabled;           // Enable/disable LLM features
  bool llm_use_builtin;       // Use built-in llama-server
  std::string llm_endpoint;   // External API endpoint
  std::string llm_model;      // External model name
  std::string llm_api_key;    // External API key
  bool llm_is_third_party;    // Is this a third-party LLM (requires PII scrubbing)?
  // Cookie management fields
  std::string name;           // Cookie name (for setCookie/deleteCookies)
  std::string value;          // Cookie value (for setCookie)
  std::string domain;         // Cookie domain (for setCookie)
  std::string path;           // Cookie path (for setCookie)
  bool secure;                // Cookie secure flag (for setCookie)
  bool http_only;             // Cookie httpOnly flag (for setCookie)
  std::string same_site;      // Cookie sameSite (for setCookie): "none", "lax", "strict"
  int64_t expires;            // Cookie expiration timestamp (for setCookie)
  std::string cookie_name;    // Cookie name for deletion (for deleteCookies)
  // Proxy configuration fields (for createContext and setProxy)
  std::string proxy_type;     // "http", "https", "socks4", "socks5", "socks5h"
  std::string proxy_host;     // Proxy server host
  int proxy_port;             // Proxy server port
  std::string proxy_username; // Proxy authentication username
  std::string proxy_password; // Proxy authentication password
  bool proxy_enabled;         // Enable proxy for this context
  bool proxy_stealth;         // Enable stealth mode (WebRTC blocking, etc.)
  bool proxy_block_webrtc;    // Block WebRTC to prevent IP leaks
  bool proxy_spoof_timezone;  // Spoof timezone based on proxy location
  bool proxy_spoof_language;  // Spoof language based on proxy location
  std::string proxy_timezone_override;  // Manual timezone override
  std::string proxy_language_override;  // Manual language override
  // CA certificate for SSL interception proxies (Charles, mitmproxy, etc.)
  std::string proxy_ca_cert_path;   // Path to custom CA certificate file
  bool proxy_trust_custom_ca;       // Trust the custom CA certificate
  // Tor-specific settings for circuit isolation
  bool is_tor;                      // Explicitly mark as Tor proxy
  int tor_control_port;             // Tor control port (0=auto, -1=disabled)
  std::string tor_control_password; // Password for Tor control port
  // Profile configuration fields (for createContext and profile management)
  std::string profile_path;         // Path to browser profile JSON file
  // Resource blocking configuration (for createContext)
  bool resource_blocking;           // Enable/disable resource blocking (ads, trackers, analytics)
  // Profile filtering options (for createContext)
  std::string os_filter;            // Filter VMs by OS: "windows", "macos", "linux"
  std::string gpu_filter;           // Filter VMs by GPU vendor: "nvidia", "amd", "intel"
  // Drag and drop fields
  int start_x;                      // Start X coordinate for drag
  int start_y;                      // Start Y coordinate for drag
  int end_x;                        // End X coordinate for drop
  int end_y;                        // End Y coordinate for drop
  std::string mid_points;           // JSON array of waypoints: "[[x1,y1],[x2,y2],...]"
  // Mouse move fields
  int steps;                        // Number of intermediate steps (0 = auto)
  std::string stop_points;          // JSON array of stop points: "[[x1,y1],[x2,y2],...]"
  // HTML5 drag and drop fields
  std::string source_selector;      // Source element selector for HTML5 drag
  std::string target_selector;      // Target element selector for HTML5 drop
  // Grid overlay fields
  int horizontal_lines;             // Number of horizontal lines for grid overlay
  int vertical_lines;               // Number of vertical lines for grid overlay
  std::string line_color;           // Line color for grid overlay
  std::string text_color;           // Text color for coordinate labels
  // Advanced feature fields
  std::string combo;                // For keyboardCombo (e.g., "Ctrl+A")
  std::string script;               // For evaluate (JavaScript code)
  bool return_value;                // For evaluate (true = return expression result)
  std::string attribute;            // For getAttribute
  std::string file_paths;           // For uploadFile (JSON array of paths)
  std::string frame_selector;       // For switchToFrame
  // Network interception fields
  std::string rule_json;            // For addNetworkRule (full rule JSON)
  std::string rule_id;              // For removeNetworkRule
  bool enable;                      // For enableNetworkInterception, enableNetworkLogging
  // Console log fields
  std::string level_filter;         // For getConsoleLogs (filter by level: debug, info, warn, error)
  std::string text_filter;          // For getConsoleLogs (filter by text content)
  int limit;                        // For getConsoleLogs (max entries to return)
  // Download management fields
  std::string download_path;        // For setDownloadPath
  std::string download_id;          // For waitForDownload, cancelDownload
  // Dialog handling fields
  std::string dialog_type;          // For setDialogAction (alert, confirm, prompt, beforeunload)
  std::string action;               // For setDialogAction (accept, dismiss, accept_with_text)
  std::string prompt_text;          // For setDialogAction (prompt default text)
  std::string dialog_id;            // For handleDialog
  bool accept;                      // For handleDialog
  std::string response_text;        // For handleDialog response
  // Tab/window management fields
  std::string popup_policy;         // For setPopupPolicy (allow, block, new_tab, background)
  std::string tab_id;               // For switchTab, closeTab
  // License management fields
  std::string license_path;         // For addLicense (file path)
  std::string license_data;         // For addLicense (base64 encoded data)
  // Wait tool fields
  int idle_time;                    // For waitForNetworkIdle (network idle duration in ms)
  std::string js_function;          // For waitForFunction (JavaScript function body)
  std::string url_pattern;          // For waitForURL (URL pattern to match)
  bool is_regex;                    // For waitForURL (whether pattern is regex)
  int polling;                      // For waitForFunction (polling interval in ms)
  // Screenshot mode fields
  std::string mode;                 // For screenshot: "viewport" (default), "element", "fullpage"
  // Action verification fields
  std::string verification_level;   // For click/type: "none", "basic", "standard", "strict"
  // Navigation wait fields
  std::string wait_until;           // For navigate: "" (no wait), "load", "domcontentloaded", "networkidle"
  // Clipboard fields
  std::string clipboard_text;       // For clipboardWrite (text to write)
};

std::string ExtractJsonString(const std::string& json, const std::string& field) {
  // Search for both "field":"value" and "field": "value" (with optional space)
  std::string searchFor = "\"" + field + "\":";
  size_t pos = json.find(searchFor);
  if (pos == std::string::npos) {
    return "";
  }

  pos += searchFor.length();

  // Skip optional whitespace
  while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
    pos++;
  }

  // Check for opening quote
  if (pos >= json.length() || json[pos] != '"') {
    return "";
  }
  pos++; // Skip the opening quote

  // Find the closing quote, handling escaped quotes and unescaping
  std::string result;
  for (size_t i = pos; i < json.length(); i++) {
    if (json[i] == '\\' && i + 1 < json.length()) {
      // Escaped character - unescape common JSON escapes
      char next = json[i + 1];
      if (next == '"' || next == '\\' || next == '/') {
        result += next;
      } else if (next == 'n') {
        result += '\n';
      } else if (next == 'r') {
        result += '\r';
      } else if (next == 't') {
        result += '\t';
      } else {
        // Unknown escape - keep as is
        result += json[i];
        result += next;
      }
      i++; // Skip next char
    } else if (json[i] == '"') {
      // Unescaped quote - end of string
      return result;
    } else {
      result += json[i];
    }
  }
  return "";
}

int ExtractJsonInt(const std::string& json, const std::string& field) {
  std::string searchFor = "\"" + field + "\":";
  size_t pos = json.find(searchFor);
  if (pos != std::string::npos) {
    pos += searchFor.length();
    // Skip optional whitespace
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
      pos++;
    }
    size_t end = json.find_first_of(",}", pos);
    if (end != std::string::npos) {
      std::string numStr = json.substr(pos, end - pos);
      // Validate it's actually a number
      if (!numStr.empty() && (numStr[0] == '-' || (numStr[0] >= '0' && numStr[0] <= '9'))) {
        return std::stoi(numStr);
      }
    }
  }
  return -1;
}

int64_t ExtractJsonInt64(const std::string& json, const std::string& field) {
  std::string searchFor = "\"" + field + "\":";
  size_t pos = json.find(searchFor);
  if (pos != std::string::npos) {
    pos += searchFor.length();
    // Skip optional whitespace
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
      pos++;
    }
    size_t end = json.find_first_of(",}", pos);
    if (end != std::string::npos) {
      std::string numStr = json.substr(pos, end - pos);
      // Validate it's actually a number
      if (!numStr.empty() && (numStr[0] == '-' || (numStr[0] >= '0' && numStr[0] <= '9'))) {
        return std::stoll(numStr);
      }
    }
  }
  return -1;
}

bool ExtractJsonBool(const std::string& json, const std::string& field) {
  std::string searchFor = "\"" + field + "\":";
  size_t pos = json.find(searchFor);
  if (pos != std::string::npos) {
    pos += searchFor.length();
    // Skip optional whitespace
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
      pos++;
    }
    size_t end = json.find_first_of(",}", pos);
    if (end != std::string::npos) {
      std::string boolStr = json.substr(pos, end - pos);
      // Trim whitespace from the result as well
      while (!boolStr.empty() && (boolStr.back() == ' ' || boolStr.back() == '\t')) {
        boolStr.pop_back();
      }
      return (boolStr == "true");
    }
  }
  return false;
}

Command ParseCommand(const std::string& json) {
  Command cmd;
  cmd.id = ExtractJsonInt(json, "id");
  cmd.method = ExtractJsonString(json, "method");
  cmd.context_id = ExtractJsonString(json, "context_id");
  cmd.url = ExtractJsonString(json, "url");
  cmd.selector = ExtractJsonString(json, "selector");
  cmd.text = ExtractJsonString(json, "text");
  cmd.key = ExtractJsonString(json, "key");
  cmd.description = ExtractJsonString(json, "description");
  cmd.what = ExtractJsonString(json, "what");
  cmd.query = ExtractJsonString(json, "query");
  cmd.max_results = ExtractJsonInt(json, "max_results");
  if (cmd.max_results <= 0) cmd.max_results = 5;  // Default
  cmd.border_color = ExtractJsonString(json, "border_color");
  cmd.background_color = ExtractJsonString(json, "background_color");
  // Content extraction fields
  cmd.clean_level = ExtractJsonString(json, "clean_level");
  cmd.include_links = ExtractJsonBool(json, "include_links");
  cmd.include_images = ExtractJsonBool(json, "include_images");
  cmd.max_length = ExtractJsonInt(json, "max_length");
  cmd.template_name = ExtractJsonString(json, "template_name");
  cmd.custom_schema = ExtractJsonString(json, "custom_schema");
  // Browser navigation & control fields
  cmd.ignore_cache = ExtractJsonBool(json, "ignore_cache");
  cmd.force_refresh = ExtractJsonBool(json, "force_refresh");
  cmd.x = ExtractJsonInt(json, "x");
  cmd.y = ExtractJsonInt(json, "y");
  cmd.timeout = ExtractJsonInt(json, "timeout");
  cmd.width = ExtractJsonInt(json, "width");
  cmd.height = ExtractJsonInt(json, "height");
  // Video recording fields
  cmd.fps = ExtractJsonInt(json, "fps");
  if (cmd.fps <= 0) cmd.fps = 30;  // Default 30fps
  cmd.codec = ExtractJsonString(json, "codec");
  if (cmd.codec.empty()) cmd.codec = "libx264";  // Default codec
  // Live streaming fields
  cmd.quality = ExtractJsonInt(json, "quality");
  if (cmd.quality <= 0) cmd.quality = 75;  // Default 75% JPEG quality
  // CAPTCHA fields
  cmd.max_attempts = ExtractJsonInt(json, "max_attempts");
  if (cmd.max_attempts <= 0) cmd.max_attempts = 3;  // Default 3 attempts
  cmd.provider = ExtractJsonString(json, "provider");
  if (cmd.provider.empty()) cmd.provider = "auto";  // Default auto-detect provider
  // LLM configuration fields
  cmd.llm_enabled = ExtractJsonBool(json, "llm_enabled");
  cmd.llm_use_builtin = ExtractJsonBool(json, "llm_use_builtin");
  cmd.llm_endpoint = ExtractJsonString(json, "llm_endpoint");
  cmd.llm_model = ExtractJsonString(json, "llm_model");
  cmd.llm_api_key = ExtractJsonString(json, "llm_api_key");
  cmd.llm_is_third_party = ExtractJsonBool(json, "llm_is_third_party");
  // Cookie management fields
  cmd.name = ExtractJsonString(json, "name");
  cmd.value = ExtractJsonString(json, "value");
  cmd.domain = ExtractJsonString(json, "domain");
  cmd.path = ExtractJsonString(json, "path");
  cmd.secure = ExtractJsonBool(json, "secure");
  cmd.http_only = ExtractJsonBool(json, "http_only");
  cmd.same_site = ExtractJsonString(json, "same_site");
  if (cmd.same_site.empty()) cmd.same_site = "lax";  // Default sameSite
  cmd.expires = ExtractJsonInt64(json, "expires");
  cmd.cookie_name = ExtractJsonString(json, "cookie_name");
  // Proxy configuration fields
  cmd.proxy_type = ExtractJsonString(json, "proxy_type");
  cmd.proxy_host = ExtractJsonString(json, "proxy_host");
  cmd.proxy_port = ExtractJsonInt(json, "proxy_port");
  cmd.proxy_username = ExtractJsonString(json, "proxy_username");
  cmd.proxy_password = ExtractJsonString(json, "proxy_password");
  cmd.proxy_enabled = ExtractJsonBool(json, "proxy_enabled");
  // Auto-enable proxy if host and port are provided (even without explicit proxy_enabled)
  if (!cmd.proxy_host.empty() && cmd.proxy_port > 0) {
    cmd.proxy_enabled = true;
  }
  cmd.proxy_stealth = ExtractJsonBool(json, "proxy_stealth");
  // Default stealth mode to true if not specified but proxy is enabled
  if (cmd.proxy_enabled && !ExtractJsonBool(json, "proxy_stealth")) {
    cmd.proxy_stealth = true;
  }
  cmd.proxy_block_webrtc = ExtractJsonBool(json, "proxy_block_webrtc") ||
                           ExtractJsonBool(json, "block_webrtc");
  // Accept both "proxy_spoof_timezone" and "spoof_timezone" (HTTP server uses the latter)
  cmd.proxy_spoof_timezone = ExtractJsonBool(json, "proxy_spoof_timezone") ||
                              ExtractJsonBool(json, "spoof_timezone");
  // Default spoof_timezone to true if not specified but proxy is enabled
  // This ensures context timezone matches proxy location automatically
  if (cmd.proxy_enabled &&
      json.find("\"proxy_spoof_timezone\"") == std::string::npos &&
      json.find("\"spoof_timezone\"") == std::string::npos) {
    cmd.proxy_spoof_timezone = true;
  }
  // Accept both "proxy_spoof_language" and "spoof_language"
  cmd.proxy_spoof_language = ExtractJsonBool(json, "proxy_spoof_language") ||
                              ExtractJsonBool(json, "spoof_language");
  // Accept both "proxy_timezone_override" and "timezone_override"
  cmd.proxy_timezone_override = ExtractJsonString(json, "proxy_timezone_override");
  if (cmd.proxy_timezone_override.empty()) {
    cmd.proxy_timezone_override = ExtractJsonString(json, "timezone_override");
  }
  // Accept both "proxy_language_override" and "language_override"
  cmd.proxy_language_override = ExtractJsonString(json, "proxy_language_override");
  if (cmd.proxy_language_override.empty()) {
    cmd.proxy_language_override = ExtractJsonString(json, "language_override");
  }
  // CA certificate for SSL interception proxies
  cmd.proxy_ca_cert_path = ExtractJsonString(json, "proxy_ca_cert_path");
  cmd.proxy_trust_custom_ca = ExtractJsonBool(json, "proxy_trust_custom_ca");
  // Tor-specific settings for circuit isolation
  cmd.is_tor = ExtractJsonBool(json, "is_tor");
  // Note: ExtractJsonInt returns -1 when field not found, but -1 means "disabled" for tor_control_port
  // When not specified, we want 0 (auto-detect), so check if field exists first
  int tor_port = ExtractJsonInt(json, "tor_control_port");
  cmd.tor_control_port = (tor_port == -1 && json.find("\"tor_control_port\"") == std::string::npos) ? 0 : tor_port;
  cmd.tor_control_password = ExtractJsonString(json, "tor_control_password");
  // Profile configuration
  cmd.profile_path = ExtractJsonString(json, "profile_path");
  // Resource blocking - default to true (enabled)
  // Only disable if explicitly set to false in the JSON
  std::string resource_blocking_str = ExtractJsonString(json, "resource_blocking");
  cmd.resource_blocking = (resource_blocking_str != "false");  // Default true
  // Profile filtering options
  cmd.os_filter = ExtractJsonString(json, "os");
  cmd.gpu_filter = ExtractJsonString(json, "gpu");
  // Drag and drop fields
  cmd.start_x = ExtractJsonInt(json, "start_x");
  cmd.start_y = ExtractJsonInt(json, "start_y");
  cmd.end_x = ExtractJsonInt(json, "end_x");
  cmd.end_y = ExtractJsonInt(json, "end_y");
  cmd.mid_points = ExtractJsonString(json, "mid_points");  // Will be parsed separately as JSON array
  // Mouse move fields
  cmd.steps = ExtractJsonInt(json, "steps");
  cmd.stop_points = ExtractJsonString(json, "stop_points");  // Will be parsed separately as JSON array
  // HTML5 drag and drop fields
  cmd.source_selector = ExtractJsonString(json, "source_selector");
  cmd.target_selector = ExtractJsonString(json, "target_selector");
  // Grid overlay fields
  cmd.horizontal_lines = ExtractJsonInt(json, "horizontal_lines");
  cmd.vertical_lines = ExtractJsonInt(json, "vertical_lines");
  cmd.line_color = ExtractJsonString(json, "line_color");
  cmd.text_color = ExtractJsonString(json, "text_color");
  // New fields for advanced features
  cmd.combo = ExtractJsonString(json, "combo");  // For keyboardCombo
  cmd.script = ExtractJsonString(json, "script");  // For evaluate
  cmd.return_value = ExtractJsonBool(json, "return_value");  // For evaluate (true = return expression result)
  // Support "expression" parameter as shorthand for script + return_value=true
  std::string expression = ExtractJsonString(json, "expression");
  if (!expression.empty()) {
    cmd.script = expression;
    cmd.return_value = true;
  }
  cmd.attribute = ExtractJsonString(json, "attribute");  // For getAttribute
  cmd.file_paths = ExtractJsonString(json, "file_paths");  // For uploadFile (JSON array)
  cmd.frame_selector = ExtractJsonString(json, "frame_selector");  // For switchToFrame
  // Network interception fields
  cmd.rule_json = ExtractJsonString(json, "rule_json");  // For addNetworkRule
  cmd.rule_id = ExtractJsonString(json, "rule_id");  // For removeNetworkRule
  cmd.enable = ExtractJsonBool(json, "enable");  // For enable features
  // Console log fields
  cmd.level_filter = ExtractJsonString(json, "level");  // For getConsoleLogs (filter by level)
  cmd.text_filter = ExtractJsonString(json, "filter");  // For getConsoleLogs (filter by text)
  cmd.limit = ExtractJsonInt(json, "limit");  // For getConsoleLogs (max entries)
  // Download management fields
  cmd.download_path = ExtractJsonString(json, "download_path");  // For setDownloadPath
  cmd.download_id = ExtractJsonString(json, "download_id");  // For waitForDownload
  // Dialog handling fields
  cmd.dialog_type = ExtractJsonString(json, "dialog_type");  // For setDialogAction
  cmd.action = ExtractJsonString(json, "action");  // For setDialogAction
  cmd.prompt_text = ExtractJsonString(json, "prompt_text");  // For setDialogAction
  cmd.dialog_id = ExtractJsonString(json, "dialog_id");  // For handleDialog
  cmd.accept = ExtractJsonBool(json, "accept");  // For handleDialog
  cmd.response_text = ExtractJsonString(json, "response_text");  // For handleDialog
  // Tab/window management fields
  cmd.popup_policy = ExtractJsonString(json, "popup_policy");  // For setPopupPolicy
  cmd.tab_id = ExtractJsonString(json, "tab_id");  // For switchTab, closeTab
  // License management fields
  cmd.license_path = ExtractJsonString(json, "license_path");
  // Accept both license_data and license_content for backwards compatibility
  cmd.license_data = ExtractJsonString(json, "license_data");
  if (cmd.license_data.empty()) {
    cmd.license_data = ExtractJsonString(json, "license_content");
  }
  // Wait tool fields
  cmd.idle_time = ExtractJsonInt(json, "idle_time");  // For waitForNetworkIdle
  cmd.js_function = ExtractJsonString(json, "js_function");  // For waitForFunction
  cmd.url_pattern = ExtractJsonString(json, "url_pattern");  // For waitForURL
  cmd.is_regex = ExtractJsonBool(json, "is_regex");  // For waitForURL
  cmd.polling = ExtractJsonInt(json, "polling");  // For waitForFunction
  // Screenshot mode fields
  cmd.mode = ExtractJsonString(json, "mode");  // For screenshot: "viewport", "element", "fullpage"
  // Action verification fields
  cmd.verification_level = ExtractJsonString(json, "verification_level");  // For click/type
  // Navigation wait fields
  cmd.wait_until = ExtractJsonString(json, "wait_until");  // For navigate: "", "load", "domcontentloaded", "networkidle"
  // Clipboard fields
  cmd.clipboard_text = ExtractJsonString(json, "text");  // For clipboardWrite
  return cmd;
}

std::string EscapeJsonString(const std::string& str) {
  std::string escaped;
  escaped.reserve(str.length());

  for (char c : str) {
    switch (c) {
      case '"':  escaped += "\\\""; break;
      case '\\': escaped += "\\\\"; break;
      case '\b': escaped += "\\b"; break;
      case '\f': escaped += "\\f"; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default:
        if (c < 32) {
          // Escape other control characters
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
          escaped += buf;
        } else {
          escaped += c;
        }
    }
  }
  return escaped;
}

// Format response as JSON string
std::string FormatResponse(int id, const std::string& result) {
  return "{\"id\":" + std::to_string(id) + ",\"result\":\"" + EscapeJsonString(result) + "\"}";
}

std::string FormatBoolResponse(int id, bool result) {
  return "{\"id\":" + std::to_string(id) + ",\"result\":" + (result ? "true" : "false") + "}";
}

std::string FormatErrorResponse(int id, const std::string& error) {
  return "{\"id\":" + std::to_string(id) + ",\"error\":\"" + EscapeJsonString(error) + "\"}";
}

void SendResponse(int id, const std::string& result) {
#if defined(OS_LINUX) || defined(OS_MACOS)
  if (tls_use_direct_response) {
    tls_response = FormatResponse(id, result);
    return;
  }
#endif
  std::lock_guard<std::mutex> lock(response_mutex);
  std::cout << FormatResponse(id, result) << std::endl;
  std::cout.flush();
}

void SendBoolResponse(int id, bool result) {
#if defined(OS_LINUX) || defined(OS_MACOS)
  if (tls_use_direct_response) {
    tls_response = FormatBoolResponse(id, result);
    return;
  }
#endif
  std::lock_guard<std::mutex> lock(response_mutex);
  std::cout << FormatBoolResponse(id, result) << std::endl;
  std::cout.flush();
}

void SendError(int id, const std::string& error) {
#if defined(OS_LINUX) || defined(OS_MACOS)
  if (tls_use_direct_response) {
    tls_response = FormatErrorResponse(id, error);
    return;
  }
#endif
  std::lock_guard<std::mutex> lock(response_mutex);
  std::cout << FormatErrorResponse(id, error) << std::endl;
  std::cout.flush();
}

// Send a raw JSON response string (for pre-formatted responses like screenshot)
void SendRawJsonResponse(const std::string& json_response) {
#if defined(OS_LINUX) || defined(OS_MACOS)
  if (tls_use_direct_response) {
    tls_response = json_response;
    return;
  }
#endif
  std::lock_guard<std::mutex> lock(response_mutex);
  std::cout << json_response << std::endl;
  std::cout.flush();
}

// Send an ActionResult as a JSON response
// Format: {"id": N, "result": {"success": bool, "status": "code", "message": "...", ...}}
// For backwards compatibility, if success is true with no extra info, clients
// can treat result.success as the boolean result
void SendActionResult(int id, const ActionResult& result) {
  std::string json_result = result.ToJSON();
  std::string response = "{\"id\":" + std::to_string(id) + ",\"result\":" + json_result + "}";
#if defined(OS_LINUX) || defined(OS_MACOS)
  if (tls_use_direct_response) {
    tls_response = response;
    return;
  }
#endif
  std::lock_guard<std::mutex> lock(response_mutex);
  std::cout << response << std::endl;
  std::cout.flush();
}

// Process command and return response string (for multi-IPC mode)
// Commands are posted to the main thread queue because CEF browser operations
// (like CreateBrowserSync) must run on the CEF UI thread.
#if defined(OS_LINUX) || defined(OS_MACOS)
std::string ProcessCommandAndGetResponse(const std::string& line) {
  // Create a command with a promise for the response
  auto ipc_cmd = std::make_unique<IPCCommand>();
  ipc_cmd->command = line;
  std::future<std::string> response_future = ipc_cmd->response_promise.get_future();

  // Post to main thread queue
  {
    std::lock_guard<std::mutex> lock(ipc_queue_mutex);
    ipc_command_queue.push(std::move(ipc_cmd));
  }
  ipc_queue_cv.notify_one();

  // Also wake up the main command loop
  queue_cv.notify_one();

  // Wait for response from main thread
  return response_future.get();
}
#endif

void ProcessCommand(const std::string& line) {
  LOG_DEBUG("SubProcess", "ProcessCommand called with: " + line.substr(0, 100));
  Command cmd = ParseCommand(line);
  LOG_DEBUG("SubProcess", "Parsed command: method=" + cmd.method + " id=" + std::to_string(cmd.id));
  OwlBrowserManager* mgr = OwlBrowserManager::GetInstance();

  if (cmd.method == "createContext") {
    // Check if LLM config was provided
    LLMConfig* llm_config = nullptr;
    LLMConfig config;
    if (!cmd.llm_endpoint.empty() || !cmd.llm_model.empty()) {
      // External API config provided
      config.enabled = true;
      config.use_builtin = cmd.llm_use_builtin;
      config.external_endpoint = cmd.llm_endpoint;
      config.external_model = cmd.llm_model;
      config.external_api_key = cmd.llm_api_key;
      config.is_third_party = cmd.llm_is_third_party;
      llm_config = &config;
      LOG_DEBUG("SubProcess", "Creating context with LLM config - endpoint: " + cmd.llm_endpoint +
               ", third-party: " + std::string(cmd.llm_is_third_party ? "YES" : "NO"));
    } else if (cmd.llm_enabled && cmd.llm_use_builtin) {
      // Use built-in LLM server
      config.enabled = true;
      config.use_builtin = true;
      llm_config = &config;
      LOG_DEBUG("SubProcess", "Creating context with built-in LLM enabled");
    } else if (!cmd.llm_enabled) {
      // LLM explicitly disabled
      config.enabled = false;
      llm_config = &config;
      LOG_DEBUG("SubProcess", "Creating context with LLM disabled");
    }

    // Check if proxy config was provided
    ProxyConfig* proxy_config = nullptr;
    ProxyConfig proxy;
    if (!cmd.proxy_host.empty() && cmd.proxy_port > 0) {
      proxy.type = OwlProxyManager::StringToProxyType(cmd.proxy_type);
      proxy.host = cmd.proxy_host;
      proxy.port = cmd.proxy_port;
      proxy.username = cmd.proxy_username;
      proxy.password = cmd.proxy_password;
      proxy.enabled = cmd.proxy_enabled;
      proxy.stealth_mode = cmd.proxy_stealth;
      proxy.block_webrtc = cmd.proxy_block_webrtc;
      proxy.spoof_timezone = cmd.proxy_spoof_timezone;
      proxy.spoof_language = cmd.proxy_spoof_language;
      proxy.timezone_override = cmd.proxy_timezone_override;
      proxy.language_override = cmd.proxy_language_override;
      // CA certificate for SSL interception proxies
      proxy.ca_cert_path = cmd.proxy_ca_cert_path;
      proxy.trust_custom_ca = cmd.proxy_trust_custom_ca;
      // Tor-specific settings for circuit isolation
      proxy.is_tor = cmd.is_tor;
      proxy.tor_control_port = cmd.tor_control_port;
      proxy.tor_control_password = cmd.tor_control_password;
      proxy_config = &proxy;
      LOG_DEBUG("SubProcess", "Creating context with proxy config - " +
               OwlProxyManager::ProxyTypeToString(proxy.type) + "://" +
               proxy.host + ":" + std::to_string(proxy.port) +
               ", stealth: " + std::string(proxy.stealth_mode ? "enabled" : "disabled") +
               ", spoof_timezone: " + std::string(proxy.spoof_timezone ? "enabled" : "disabled") +
               ", timezone_override: " + (proxy.timezone_override.empty() ? "(empty)" : proxy.timezone_override) +
               ", trust_custom_ca: " + std::string(proxy.trust_custom_ca ? "enabled" : "disabled"));
    }

    // Check if profile path was provided
    if (!cmd.profile_path.empty()) {
      LOG_DEBUG("SubProcess", "Creating context with profile: " + cmd.profile_path);
    }

    // Log resource blocking setting
    LOG_DEBUG("SubProcess", "Creating context with resource_blocking: " + std::string(cmd.resource_blocking ? "enabled" : "disabled"));

    // Log profile filtering options
    if (!cmd.os_filter.empty()) {
      LOG_DEBUG("SubProcess", "Creating context with OS filter: " + cmd.os_filter);
    }
    if (!cmd.gpu_filter.empty()) {
      LOG_DEBUG("SubProcess", "Creating context with GPU filter: " + cmd.gpu_filter);
    }

    std::string ctx = mgr->CreateContext(llm_config, proxy_config, cmd.profile_path, cmd.resource_blocking, cmd.os_filter, cmd.gpu_filter);

    // Return full context info (vm_profile, seeds, hashes, etc.) instead of just context_id
    std::string context_info = mgr->GetContextInfo(ctx);
    // Remove newlines from JSON result (must be single-line for IPC)
    context_info.erase(std::remove(context_info.begin(), context_info.end(), '\n'), context_info.end());
    context_info.erase(std::remove(context_info.begin(), context_info.end(), '\r'), context_info.end());
    // Add context_id to the response
    // Insert context_id at the beginning of the JSON object
    if (!context_info.empty() && context_info[0] == '{') {
      context_info = "{\"context_id\":\"" + ctx + "\"," + context_info.substr(1);
    }
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + context_info + "}");
  }
  else if (cmd.method == "navigate") {
    // Validate URL is not empty
    if (cmd.url.empty()) {
      SendError(cmd.id, "URL cannot be empty");
      return;
    }
    // Default timeout is 30000ms, use custom if provided
    int timeout = cmd.timeout > 0 ? cmd.timeout : 30000;
    ActionResult result = mgr->Navigate(cmd.context_id, cmd.url, cmd.wait_until, timeout);
    result.url = cmd.url;
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "waitForNavigation") {
    int timeout = cmd.timeout > 0 ? cmd.timeout : 30000;  // Default 30s
    ActionResult result = mgr->WaitForNavigation(cmd.context_id, timeout);

    // If navigation succeeded, check for firewall and add navigation info
    if (result.status == ActionStatus::OK) {
      auto browser = mgr->GetBrowser(cmd.context_id);
      if (browser) {
        CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
        OwlClient* client = static_cast<OwlClient*>(client_base.get());
        NavigationInfo nav_info = client->GetNavigationInfo();

        // Check for web firewall/bot protection challenges
        OwlFirewallDetector firewall_detector;
        FirewallDetectionResult firewall_result = firewall_detector.Detect(browser);

        if (firewall_result.detected && firewall_result.confidence >= 0.5) {
          LOG_DEBUG("Navigation", "Firewall detected: " + firewall_result.provider_name +
                   " (" + firewall_result.challenge_description + ") confidence: " +
                   std::to_string(firewall_result.confidence));

          result = ActionResult::FirewallDetected(
            nav_info.url,
            firewall_result.provider_name,
            firewall_result.challenge_description
          );
        }
        result.url = nav_info.url;
        result.http_status = nav_info.http_status;
      }
    }
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "click") {
    // Validate context exists first
    auto browser = mgr->GetBrowser(cmd.context_id);
    if (!browser) {
      SendError(cmd.id, "Browser not found: " + cmd.context_id);
      return;
    }
    // Validate selector is not empty
    if (cmd.selector.empty()) {
      SendError(cmd.id, "Selector cannot be empty");
      return;
    }
    // Parse verification level (default to STANDARD)
    VerificationLevel level = VerificationLevel::STANDARD;
    if (cmd.verification_level == "none") {
      level = VerificationLevel::NONE;
    } else if (cmd.verification_level == "basic") {
      level = VerificationLevel::BASIC;
    } else if (cmd.verification_level == "strict") {
      level = VerificationLevel::STRICT;
    }
    // Click now returns ActionResult directly
    ActionResult result = mgr->Click(cmd.context_id, cmd.selector, level);
    result.selector = cmd.selector;
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "dragDrop") {
    // Parse mid_points from JSON array string "[[x1,y1],[x2,y2],...]"
    std::vector<std::pair<int, int>> mid_points_vec;
    if (!cmd.mid_points.empty()) {
      // Simple parsing of [[x,y],[x,y],...] format
      std::string points_str = cmd.mid_points;
      size_t pos = 0;
      while ((pos = points_str.find('[', pos)) != std::string::npos) {
        // Skip the outer array bracket
        if (pos == 0) {
          pos++;
          continue;
        }
        // Find the matching ]
        size_t end_pos = points_str.find(']', pos);
        if (end_pos == std::string::npos) break;
        // Extract "x,y" between [ and ]
        std::string coord_str = points_str.substr(pos + 1, end_pos - pos - 1);
        size_t comma_pos = coord_str.find(',');
        if (comma_pos != std::string::npos) {
          int px = std::stoi(coord_str.substr(0, comma_pos));
          int py = std::stoi(coord_str.substr(comma_pos + 1));
          mid_points_vec.push_back({px, py});
        }
        pos = end_pos + 1;
      }
    }
    // Method now returns ActionResult directly
    ActionResult result = mgr->DragDrop(cmd.context_id, cmd.start_x, cmd.start_y,
                                  cmd.end_x, cmd.end_y, mid_points_vec);
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "html5DragDrop") {
    // Method now returns ActionResult directly
    ActionResult result = mgr->HTML5DragDrop(cmd.context_id, cmd.source_selector, cmd.target_selector);
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "mouseMove") {
    // Parse stop_points from JSON array string "[[x1,y1],[x2,y2],...]"
    std::vector<std::pair<int, int>> stop_points_vec;
    if (!cmd.stop_points.empty()) {
      std::string points_str = cmd.stop_points;
      size_t pos = 0;
      while ((pos = points_str.find('[', pos)) != std::string::npos) {
        if (pos == 0) {
          pos++;
          continue;
        }
        size_t end_pos = points_str.find(']', pos);
        if (end_pos == std::string::npos) break;
        std::string coord_str = points_str.substr(pos + 1, end_pos - pos - 1);
        size_t comma_pos = coord_str.find(',');
        if (comma_pos != std::string::npos) {
          int px = std::stoi(coord_str.substr(0, comma_pos));
          int py = std::stoi(coord_str.substr(comma_pos + 1));
          stop_points_vec.push_back({px, py});
        }
        pos = end_pos + 1;
      }
    }
    // Method now returns ActionResult directly
    ActionResult result = mgr->MouseMove(cmd.context_id, cmd.start_x, cmd.start_y,
                                   cmd.end_x, cmd.end_y, cmd.steps, stop_points_vec);
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "type") {
    // Validate context exists first
    auto browser = mgr->GetBrowser(cmd.context_id);
    if (!browser) {
      SendActionResult(cmd.id, ActionResult::BrowserNotFound(cmd.context_id));
      return;
    }
    // Validate selector is not empty
    if (cmd.selector.empty()) {
      SendActionResult(cmd.id, ActionResult::Failure(ActionStatus::INVALID_SELECTOR, "Selector cannot be empty"));
      return;
    }
    // Parse verification level (default to STANDARD)
    VerificationLevel level = VerificationLevel::STANDARD;
    if (cmd.verification_level == "none") {
      level = VerificationLevel::NONE;
    } else if (cmd.verification_level == "basic") {
      level = VerificationLevel::BASIC;
    } else if (cmd.verification_level == "strict") {
      level = VerificationLevel::STRICT;
    }
    // Type now returns ActionResult directly
    ActionResult result = mgr->Type(cmd.context_id, cmd.selector, cmd.text, level);
    result.selector = cmd.selector;
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "pick") {
    // Validate context exists first
    auto browser = mgr->GetBrowser(cmd.context_id);
    if (!browser) {
      SendActionResult(cmd.id, ActionResult::BrowserNotFound(cmd.context_id));
      return;
    }
    // Validate selector is not empty
    if (cmd.selector.empty()) {
      SendActionResult(cmd.id, ActionResult::Failure(ActionStatus::INVALID_SELECTOR, "Selector cannot be empty"));
      return;
    }
    // Parse verification level (default to STANDARD)
    VerificationLevel level = VerificationLevel::STANDARD;
    if (cmd.verification_level == "none") {
      level = VerificationLevel::NONE;
    } else if (cmd.verification_level == "basic") {
      level = VerificationLevel::BASIC;
    } else if (cmd.verification_level == "strict") {
      level = VerificationLevel::STRICT;
    }
    // Pick now returns ActionResult directly
    ActionResult result = mgr->Pick(cmd.context_id, cmd.selector, cmd.value, level);
    result.selector = cmd.selector;
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "pressKey") {
    // Method now returns ActionResult directly
    ActionResult result = mgr->PressKey(cmd.context_id, cmd.key);
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "submitForm") {
    // Method now returns ActionResult directly
    ActionResult result = mgr->SubmitForm(cmd.context_id);
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "releaseContext") {
    mgr->ReleaseContext(cmd.context_id);
    SendBoolResponse(cmd.id, true);
  }
  else if (cmd.method == "closeContext") {
    bool success = mgr->CloseContext(cmd.context_id);
    SendBoolResponse(cmd.id, success);
  }
  else if (cmd.method == "listContexts") {
    std::vector<std::string> contexts = mgr->ListContexts();
    // Build JSON array of context IDs
    std::ostringstream json;
    json << "[";
    for (size_t i = 0; i < contexts.size(); ++i) {
      if (i > 0) json << ",";
      json << "\"" << contexts[i] << "\"";
    }
    json << "]";
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + json.str() + "}");
  }
  else if (cmd.method == "screenshot") {
    std::vector<uint8_t> data;

    // Determine screenshot mode (default: viewport)
    std::string mode = cmd.mode.empty() ? "viewport" : cmd.mode;

    if (mode == "element") {
      // Element mode: capture specific element by selector
      if (cmd.selector.empty()) {
        SendError(cmd.id, "Element screenshot mode requires a selector");
        return;
      }
      data = mgr->ScreenshotElement(cmd.context_id, cmd.selector);
    } else if (mode == "fullpage") {
      // Fullpage mode: capture entire scrollable page
      data = mgr->ScreenshotFullpage(cmd.context_id);
    } else {
      // Viewport mode (default): capture current visible view
      data = mgr->Screenshot(cmd.context_id);
    }

    // Use CEF's optimized base64 encoding (much faster)
    CefRefPtr<CefBinaryValue> binary = CefBinaryValue::Create(data.data(), data.size());
    std::string encoded = CefBase64Encode(data.data(), data.size()).ToString();

    std::string response = "{\"id\":" + std::to_string(cmd.id) + ",\"result\":\"" + encoded + "\"}";
    SendRawJsonResponse(response);
  }
  else if (cmd.method == "shutdown") {
    should_quit = true;
    SendResponse(cmd.id, "shutdown");
  }
  // AI-First Methods
  else if (cmd.method == "aiClick") {
    bool success = mgr->AIClick(cmd.context_id, cmd.description);
    SendBoolResponse(cmd.id, success);
  }
  else if (cmd.method == "aiType") {
    bool success = mgr->AIType(cmd.context_id, cmd.description, cmd.text);
    SendBoolResponse(cmd.id, success);
  }
  else if (cmd.method == "aiExtract") {
    std::string result = mgr->AIExtract(cmd.context_id, cmd.what);
    SendResponse(cmd.id, result);
  }
  else if (cmd.method == "aiAnalyze") {
    std::string result = mgr->AIAnalyze(cmd.context_id);
    std::string response = "{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}";
    SendRawJsonResponse(response);
  }
  else if (cmd.method == "aiQuery") {
    std::string result = mgr->AIQuery(cmd.context_id, cmd.query);
    SendResponse(cmd.id, result);
  }
  else if (cmd.method == "findElement") {
    LOG_DEBUG("SubProcess", "findElement command received - context=" + cmd.context_id + " description='" + cmd.description + "' max=" + std::to_string(cmd.max_results));
    std::string result = mgr->FindElement(cmd.context_id, cmd.description, cmd.max_results);
    LOG_DEBUG("SubProcess", "findElement result: " + result.substr(0, 200));
    LOG_DEBUG("SubProcess", "findElement completed, sending response");
    std::string response = "{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}";
    LOG_DEBUG("SubProcess", "Response: " + response.substr(0, 300));
    SendRawJsonResponse(response);
  }
  else if (cmd.method == "getBlockerStats") {
    std::string result = mgr->GetBlockerStats(cmd.context_id);
    std::string response = "{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}";
    SendRawJsonResponse(response);
  }
  else if (cmd.method == "highlight") {
    // Use default colors if not provided
    std::string border = cmd.border_color.empty() ? "#FF0000" : cmd.border_color;
    std::string background = cmd.background_color.empty() ? "rgba(255, 0, 0, 0.2)" : cmd.background_color;
    ActionResult result = mgr->Highlight(cmd.context_id, cmd.selector, border, background);
    result.selector = cmd.selector;
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "showGridOverlay") {
    // Use default values if not provided
    int h_lines = cmd.horizontal_lines > 0 ? cmd.horizontal_lines : 25;
    int v_lines = cmd.vertical_lines > 0 ? cmd.vertical_lines : 25;
    std::string line_col = cmd.line_color.empty() ? "rgba(255, 0, 0, 0.15)" : cmd.line_color;
    std::string text_col = cmd.text_color.empty() ? "rgba(255, 0, 0, 0.4)" : cmd.text_color;
    ActionResult result = mgr->ShowGridOverlay(cmd.context_id, h_lines, v_lines, line_col, text_col);
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "extractText") {
    std::string text = mgr->ExtractText(cmd.context_id, cmd.selector);
    SendResponse(cmd.id, text);
  }
  // Content Extraction Methods
  else if (cmd.method == "getHTML") {
    std::string html = mgr->GetHTML(cmd.context_id,
                                    cmd.clean_level.empty() ? "basic" : cmd.clean_level);
    SendResponse(cmd.id, html);
  }
  else if (cmd.method == "getMarkdown") {
    std::string markdown = mgr->GetMarkdown(cmd.context_id,
                                            cmd.include_links,
                                            cmd.include_images,
                                            cmd.max_length);
    SendResponse(cmd.id, markdown);
  }
  else if (cmd.method == "extractJSON") {
    std::string json = mgr->ExtractJSON(cmd.context_id,
                                        cmd.template_name,
                                        cmd.custom_schema);
    SendResponse(cmd.id, json);
  }
  else if (cmd.method == "detectWebsiteType") {
    std::string type = mgr->DetectWebsiteType(cmd.context_id);
    SendResponse(cmd.id, type);
  }
  else if (cmd.method == "listTemplates") {
    std::vector<std::string> templates = mgr->ListTemplates();
    std::ostringstream json;
    json << "[";
    for (size_t i = 0; i < templates.size(); i++) {
      if (i > 0) json << ",";
      json << "\"" << templates[i] << "\"";
    }
    json << "]";
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + json.str() + "}");
  }
  // AI Intelligence Methods
  else if (cmd.method == "summarizePage") {
    std::string summary = mgr->SummarizePage(cmd.context_id, cmd.force_refresh);
    SendResponse(cmd.id, summary);
  }
  else if (cmd.method == "queryPage") {
    std::string answer = mgr->QueryPage(cmd.context_id, cmd.query);
    SendResponse(cmd.id, answer);
  }
  else if (cmd.method == "getLLMStatus") {
    std::string status = mgr->GetLLMStatus();
    SendResponse(cmd.id, status);
  }
  else if (cmd.method == "executeNLA") {
    std::string result = mgr->ExecuteNLA(cmd.context_id, cmd.query);
    SendResponse(cmd.id, result);
  }
  // Browser Navigation & Control Methods
  else if (cmd.method == "reload") {
    int timeout = cmd.timeout > 0 ? cmd.timeout : 30000;
    // Default wait_until is "load" for backward compatibility
    std::string wait_until = cmd.wait_until.empty() ? "load" : cmd.wait_until;
    ActionResult result = mgr->Reload(cmd.context_id, cmd.ignore_cache, wait_until, timeout);
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "goBack") {
    int timeout = cmd.timeout > 0 ? cmd.timeout : 30000;
    std::string wait_until = cmd.wait_until.empty() ? "load" : cmd.wait_until;
    ActionResult result = mgr->GoBack(cmd.context_id, wait_until, timeout);
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "goForward") {
    int timeout = cmd.timeout > 0 ? cmd.timeout : 30000;
    std::string wait_until = cmd.wait_until.empty() ? "load" : cmd.wait_until;
    ActionResult result = mgr->GoForward(cmd.context_id, wait_until, timeout);
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "canGoBack") {
    bool can_go_back = mgr->CanGoBack(cmd.context_id);
    SendBoolResponse(cmd.id, can_go_back);
  }
  else if (cmd.method == "canGoForward") {
    bool can_go_forward = mgr->CanGoForward(cmd.context_id);
    SendBoolResponse(cmd.id, can_go_forward);
  }
  // Scroll Control Methods
  else if (cmd.method == "scrollBy") {
    // Parse verification level (default to NONE for scroll - backward compatible)
    VerificationLevel level = VerificationLevel::NONE;
    if (cmd.verification_level == "basic") {
      level = VerificationLevel::BASIC;
    } else if (cmd.verification_level == "standard") {
      level = VerificationLevel::STANDARD;
    } else if (cmd.verification_level == "strict") {
      level = VerificationLevel::STRICT;
    }
    ActionResult result = mgr->ScrollBy(cmd.context_id, cmd.x, cmd.y, level);
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "scrollTo") {
    // Parse verification level (default to NONE for scroll - backward compatible)
    VerificationLevel level = VerificationLevel::NONE;
    if (cmd.verification_level == "basic") {
      level = VerificationLevel::BASIC;
    } else if (cmd.verification_level == "standard") {
      level = VerificationLevel::STANDARD;
    } else if (cmd.verification_level == "strict") {
      level = VerificationLevel::STRICT;
    }
    ActionResult result = mgr->ScrollTo(cmd.context_id, cmd.x, cmd.y, level);
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "scrollToElement") {
    if (cmd.selector.empty()) {
      SendActionResult(cmd.id, ActionResult::Failure(ActionStatus::INVALID_SELECTOR, "Selector cannot be empty"));
      return;
    }
    ActionResult result = mgr->ScrollToElement(cmd.context_id, cmd.selector);
    result.selector = cmd.selector;
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "scrollToTop") {
    ActionResult result = mgr->ScrollToTop(cmd.context_id);
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "scrollToBottom") {
    ActionResult result = mgr->ScrollToBottom(cmd.context_id);
    SendActionResult(cmd.id, result);
  }
  // Wait Utilities Methods
  else if (cmd.method == "waitForSelector") {
    if (cmd.selector.empty()) {
      SendActionResult(cmd.id, ActionResult::Failure(ActionStatus::INVALID_SELECTOR, "Selector cannot be empty"));
      return;
    }
    int timeout = cmd.timeout > 0 ? cmd.timeout : 5000;  // Default 5000ms
    ActionResult result = mgr->WaitForSelector(cmd.context_id, cmd.selector, timeout);
    result.selector = cmd.selector;
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "waitForTimeout") {
    int timeout = cmd.timeout > 0 ? cmd.timeout : 1000;  // Default 1000ms
    ActionResult result = mgr->WaitForTimeout(cmd.context_id, timeout);
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "waitForNetworkIdle") {
    int idle_time = cmd.idle_time > 0 ? cmd.idle_time : 500;  // Default 500ms idle time
    int timeout = cmd.timeout > 0 ? cmd.timeout : 30000;  // Default 30s
    ActionResult result = mgr->WaitForNetworkIdle(cmd.context_id, idle_time, timeout);
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "waitForFunction") {
    if (cmd.js_function.empty()) {
      SendActionResult(cmd.id, ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "JavaScript function cannot be empty"));
      return;
    }
    int polling = cmd.polling > 0 ? cmd.polling : 100;  // Default 100ms polling
    int timeout = cmd.timeout > 0 ? cmd.timeout : 30000;  // Default 30s
    ActionResult result = mgr->WaitForFunction(cmd.context_id, cmd.js_function, polling, timeout);
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "waitForURL") {
    if (cmd.url_pattern.empty()) {
      SendActionResult(cmd.id, ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "URL pattern cannot be empty"));
      return;
    }
    int timeout = cmd.timeout > 0 ? cmd.timeout : 30000;  // Default 30s
    ActionResult result = mgr->WaitForURL(cmd.context_id, cmd.url_pattern, cmd.is_regex, timeout);
    if (result.status == ActionStatus::OK) {
      result.url = mgr->GetCurrentURL(cmd.context_id);
    }
    SendActionResult(cmd.id, result);
  }
  // Page State Query Methods
  else if (cmd.method == "getCurrentURL") {
    std::string url = mgr->GetCurrentURL(cmd.context_id);
    SendResponse(cmd.id, url);
  }
  else if (cmd.method == "getPageTitle") {
    std::string title = mgr->GetPageTitle(cmd.context_id);
    SendResponse(cmd.id, title);
  }
  else if (cmd.method == "getPageInfo") {
    std::string info = mgr->GetPageInfo(cmd.context_id);
    std::string response = "{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + info + "}";
    SendRawJsonResponse(response);
  }
  // Viewport Manipulation Methods
  else if (cmd.method == "setViewport") {
    ActionResult result = mgr->SetViewport(cmd.context_id, cmd.width, cmd.height);
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "getViewport") {
    std::string viewport = mgr->GetViewport(cmd.context_id);
    std::string response = "{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + viewport + "}";
    SendRawJsonResponse(response);
  }
  // Video Recording Methods - NEW!
  else if (cmd.method == "startVideoRecording") {
    bool success = mgr->StartVideoRecording(cmd.context_id, cmd.fps, cmd.codec);
    SendBoolResponse(cmd.id, success);
  }
  else if (cmd.method == "pauseVideoRecording") {
    bool success = mgr->PauseVideoRecording(cmd.context_id);
    SendBoolResponse(cmd.id, success);
  }
  else if (cmd.method == "resumeVideoRecording") {
    bool success = mgr->ResumeVideoRecording(cmd.context_id);
    SendBoolResponse(cmd.id, success);
  }
  else if (cmd.method == "stopVideoRecording") {
    std::string video_path = mgr->StopVideoRecording(cmd.context_id);
    std::string response = "{\"id\":" + std::to_string(cmd.id) + ",\"result\":\"" + video_path + "\"}";
    SendRawJsonResponse(response);
  }
  else if (cmd.method == "getVideoRecordingStats") {
    std::string stats = mgr->GetVideoRecordingStats(cmd.context_id);
    std::string response = "{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + stats + "}";
    SendRawJsonResponse(response);
  }
  // Live video streaming commands
  else if (cmd.method == "startLiveStream") {
#ifdef OWL_DEBUG_BUILD
    auto start_time = std::chrono::steady_clock::now();
    LOG_DEBUG("SubProcess", "[TIMING] startLiveStream command received for context " + cmd.context_id);
#endif

    // Use parsed values from JSON, with appropriate defaults for live streaming
    int fps = (cmd.fps > 0 && cmd.fps <= 60) ? cmd.fps : 15;  // Default 15 fps for live streaming
    int quality = (cmd.quality > 0 && cmd.quality <= 100) ? cmd.quality : 75;  // Default 75% JPEG quality
    bool success = mgr->StartLiveStream(cmd.context_id, fps, quality);

#ifdef OWL_DEBUG_BUILD
    auto after_start = std::chrono::steady_clock::now();
    long start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(after_start - start_time).count();
    LOG_DEBUG("SubProcess", "[TIMING] StartLiveStream() took " + std::to_string(start_ms) + "ms for context " + cmd.context_id);
#endif

    // Get shared memory info for direct frame access
    auto* streamer = owl::LiveStreamer::GetInstance();
    auto shm_info = streamer->GetSharedMemoryInfo(cmd.context_id);

    std::ostringstream response;
    response << "{\"id\":" << cmd.id << ",\"result\":{\"success\":" << (success ? "true" : "false")
             << ",\"context_id\":\"" << cmd.context_id << "\""
             << ",\"fps\":" << fps << ",\"quality\":" << quality;

    // Include shared memory info if available (Linux only)
    if (shm_info.available) {
      response << ",\"shm_name\":\"" << shm_info.shm_name << "\""
               << ",\"shm_available\":true";
    } else {
      response << ",\"shm_available\":false";
    }
    response << "}}";

#ifdef OWL_DEBUG_BUILD
    auto end_time = std::chrono::steady_clock::now();
    long total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    LOG_DEBUG("SubProcess", "[TIMING] startLiveStream total processing: " + std::to_string(total_ms) + "ms, sending response");
#endif
    SendRawJsonResponse(response.str());
  }
  else if (cmd.method == "stopLiveStream") {
    bool success = mgr->StopLiveStream(cmd.context_id);
    SendBoolResponse(cmd.id, success);
  }
  else if (cmd.method == "getLiveStreamStats") {
    std::string stats = mgr->GetLiveStreamStats(cmd.context_id);
    std::string response = "{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + stats + "}";
    SendRawJsonResponse(response);
  }
  else if (cmd.method == "listLiveStreams") {
    std::string list = mgr->ListLiveStreams();
    std::string response = "{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + list + "}";
    SendRawJsonResponse(response);
  }
  else if (cmd.method == "getLiveFrame") {
    // Get the latest JPEG frame from the live streamer
    auto* streamer = owl::LiveStreamer::GetInstance();
    std::vector<uint8_t> jpeg_data;
    int width = 0, height = 0;

    if (streamer->GetLatestFrame(cmd.context_id, jpeg_data, width, height)) {
      // Base64 encode the JPEG data
      std::string encoded = CefBase64Encode(jpeg_data.data(), jpeg_data.size()).ToString();

      // Return object with data, width, and height for IPC fallback on macOS
      std::ostringstream response;
      response << "{\"id\":" << cmd.id << ",\"result\":{\"data\":\"" << encoded
               << "\",\"width\":" << width << ",\"height\":" << height << "}}";
      SendRawJsonResponse(response.str());
    } else {
      SendError(cmd.id, "No frame available");
    }
  }

  // =========================================================================
  // License Management Commands
  // =========================================================================
  else if (cmd.method == "getLicenseStatus") {
    auto* license_mgr = olib::license::LicenseManager::GetInstance();
    auto status = license_mgr->Validate();
    std::string status_str = olib::license::LicenseStatusToString(status);
    bool is_valid = (status == olib::license::LicenseStatus::VALID);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":{\"status\":\"" + status_str + "\",\"valid\":" + (is_valid ? "true" : "false") + "}}");
  }
  else if (cmd.method == "getLicenseInfo") {
    auto* license_mgr = olib::license::LicenseManager::GetInstance();
    license_mgr->Validate();  // Ensure license is loaded
    std::string info = license_mgr->GetLicenseInfo();
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + info + "}");
  }
  else if (cmd.method == "getHardwareFingerprint") {
    std::string fingerprint = olib::license::HardwareFingerprint::Generate();
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":{\"fingerprint\":\"" + fingerprint + "\"}}");
  }
  else if (cmd.method == "addLicense") {
    // Expects license_data as base64-encoded .olic file content or license_path
    if (!cmd.license_path.empty()) {
      // Direct file path approach
      auto* license_mgr = olib::license::LicenseManager::GetInstance();
      auto status = license_mgr->AddLicense(cmd.license_path);
      bool success = (status == olib::license::LicenseStatus::VALID);
      std::string status_str = olib::license::LicenseStatusToString(status);
      if (success) {
        std::string info = license_mgr->GetLicenseInfo();
        SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":{\"success\":true,\"status\":\"" + status_str + "\",\"license\":" + info + "}}");
      } else {
        SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":{\"success\":false,\"status\":\"" + status_str + "\",\"error\":\"Failed to activate license\"}}");
      }
    } else if (!cmd.license_data.empty()) {
      // Base64 license data - decode and save to temp file
      static const std::string b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      std::vector<uint8_t> decoded;
      int val = 0, valb = -8;
      for (char c : cmd.license_data) {
        if (c == '=') break;
        size_t pos = b64chars.find(c);
        if (pos == std::string::npos) continue;
        val = (val << 6) + static_cast<int>(pos);
        valb += 6;
        if (valb >= 0) {
          decoded.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
          valb -= 8;
        }
      }

      if (decoded.empty()) {
        SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":{\"success\":false,\"error\":\"Invalid base64 license data\"}}");
      } else {
        // Write to temp file
        std::string temp_path = "/tmp/owl_license_" + std::to_string(cmd.id) + ".olic";
        std::ofstream ofs(temp_path, std::ios::binary);
        if (ofs) {
          ofs.write(reinterpret_cast<const char*>(decoded.data()), decoded.size());
          ofs.close();

          auto* license_mgr = olib::license::LicenseManager::GetInstance();
          auto status = license_mgr->AddLicense(temp_path);
          bool success = (status == olib::license::LicenseStatus::VALID);
          std::string status_str = olib::license::LicenseStatusToString(status);

          // Clean up temp file
          std::remove(temp_path.c_str());

          if (success) {
            std::string info = license_mgr->GetLicenseInfo();
            SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":{\"success\":true,\"status\":\"" + status_str + "\",\"license\":" + info + "}}");
          } else {
            SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":{\"success\":false,\"status\":\"" + status_str + "\",\"error\":\"Failed to activate license\"}}");
          }
        } else {
          SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":{\"success\":false,\"error\":\"Failed to write temp license file\"}}");
        }
      }
    } else {
      SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":{\"success\":false,\"error\":\"Missing license_data or license_path parameter\"}}");
    }
  }
  else if (cmd.method == "removeLicense") {
    auto* license_mgr = olib::license::LicenseManager::GetInstance();
    license_mgr->RemoveLicense();
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":{\"success\":true,\"message\":\"License removed\"}}");
  }

  // Demographics and context commands
  else if (cmd.method == "getDemographics") {
    std::string demographics = mgr->GetDemographics();
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + demographics + "}");
  }
  else if (cmd.method == "getLocation") {
    std::string location = mgr->GetLocation();
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + location + "}");
  }
  else if (cmd.method == "getDateTime") {
    std::string datetime = mgr->GetDateTime();
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + datetime + "}");
  }
  else if (cmd.method == "getWeather") {
    std::string weather = mgr->GetWeather();
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + weather + "}");
  }
  else if (cmd.method == "getHomepage") {
    std::string html = mgr->GetHomepageHTML();
    // Return the HTML as a JSON string
    std::ostringstream json;
    json << "{\"id\":" << cmd.id << ",\"result\":\"";
    // Escape the HTML for JSON
    for (char c : html) {
      if (c == '"') json << "\\\"";
      else if (c == '\\') json << "\\\\";
      else if (c == '\n') json << "\\n";
      else if (c == '\r') json << "\\r";
      else if (c == '\t') json << "\\t";
      else json << c;
    }
    json << "\"}";
    SendRawJsonResponse(json.str());
  }
  // CAPTCHA Handling Methods
  else if (cmd.method == "detectCaptcha") {
    std::string result = mgr->DetectCaptcha(cmd.context_id);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  else if (cmd.method == "classifyCaptcha") {
    std::string result = mgr->ClassifyCaptcha(cmd.context_id);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  else if (cmd.method == "solveTextCaptcha") {
    int max_attempts = cmd.max_attempts > 0 ? cmd.max_attempts : 3;
    std::string result = mgr->SolveTextCaptcha(cmd.context_id, max_attempts);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  else if (cmd.method == "solveImageCaptcha") {
    int max_attempts = cmd.max_attempts > 0 ? cmd.max_attempts : 3;
    std::string provider = cmd.provider.empty() ? "auto" : cmd.provider;
    std::string result = mgr->SolveImageCaptcha(cmd.context_id, max_attempts, provider);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  else if (cmd.method == "solveCaptcha") {
    int max_attempts = cmd.max_attempts > 0 ? cmd.max_attempts : 3;
    std::string provider = cmd.provider.empty() ? "auto" : cmd.provider;
    std::string result = mgr->SolveCaptcha(cmd.context_id, max_attempts, provider);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  // Cookie Management Methods
  else if (cmd.method == "getCookies") {
    std::string result = mgr->GetCookies(cmd.context_id, cmd.url);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  else if (cmd.method == "setCookie") {
    ActionResult result = mgr->SetCookie(
      cmd.context_id,
      cmd.url,
      cmd.name,
      cmd.value,
      cmd.domain,
      cmd.path,
      cmd.secure,
      cmd.http_only,
      cmd.same_site,
      cmd.expires
    );
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "deleteCookies") {
    ActionResult result = mgr->DeleteCookies(cmd.context_id, cmd.url, cmd.cookie_name);
    SendActionResult(cmd.id, result);
  }
  // ===== Proxy Management =====
  else if (cmd.method == "setProxy") {
    ProxyConfig proxy;
    proxy.type = OwlProxyManager::StringToProxyType(cmd.proxy_type);
    proxy.host = cmd.proxy_host;
    proxy.port = cmd.proxy_port;
    proxy.username = cmd.proxy_username;
    proxy.password = cmd.proxy_password;
    proxy.enabled = cmd.proxy_enabled;
    proxy.stealth_mode = cmd.proxy_stealth;
    proxy.block_webrtc = cmd.proxy_block_webrtc;
    proxy.spoof_timezone = cmd.proxy_spoof_timezone;
    proxy.spoof_language = cmd.proxy_spoof_language;
    proxy.timezone_override = cmd.proxy_timezone_override;
    proxy.language_override = cmd.proxy_language_override;
    // CA certificate for SSL interception proxies
    proxy.ca_cert_path = cmd.proxy_ca_cert_path;
    proxy.trust_custom_ca = cmd.proxy_trust_custom_ca;
    bool success = mgr->SetProxy(cmd.context_id, proxy);
    SendBoolResponse(cmd.id, success);
  }
  else if (cmd.method == "getProxyStatus") {
    std::string result = mgr->GetProxyStatus(cmd.context_id);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  else if (cmd.method == "connectProxy") {
    bool success = mgr->ConnectProxy(cmd.context_id);
    SendBoolResponse(cmd.id, success);
  }
  else if (cmd.method == "disconnectProxy") {
    bool success = mgr->DisconnectProxy(cmd.context_id);
    SendBoolResponse(cmd.id, success);
  }
  // ===== Profile Management =====
  else if (cmd.method == "createProfile") {
    std::string result = mgr->CreateProfile(cmd.name);
    // Remove newlines from JSON result (must be single-line for IPC)
    result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
    result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  else if (cmd.method == "loadProfile") {
    std::string result = mgr->LoadProfile(cmd.context_id, cmd.profile_path);
    // Remove newlines from JSON result (must be single-line for IPC)
    result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
    result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  else if (cmd.method == "saveProfile") {
    std::string result = mgr->SaveProfile(cmd.context_id, cmd.profile_path);
    // Remove newlines from JSON result (must be single-line for IPC)
    result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
    result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  else if (cmd.method == "getProfile") {
    std::string result = mgr->GetProfile(cmd.context_id);
    // Remove newlines from JSON result (must be single-line for IPC)
    result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
    result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  else if (cmd.method == "updateProfileCookies") {
    bool success = mgr->UpdateProfileCookies(cmd.context_id);
    if (!success) {
      SendError(cmd.id, "No profile associated with context or update failed");
    } else {
      SendBoolResponse(cmd.id, success);
    }
  }
  else if (cmd.method == "getContextInfo") {
    std::string result = mgr->GetContextInfo(cmd.context_id);
    // Remove newlines from JSON result (must be single-line for IPC)
    result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
    result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  // ===== Advanced Mouse Interactions =====
  else if (cmd.method == "hover") {
    if (cmd.selector.empty()) {
      SendActionResult(cmd.id, ActionResult::Failure(ActionStatus::INVALID_SELECTOR, "Selector cannot be empty"));
      return;
    }
    // Method now returns ActionResult directly
    ActionResult result = mgr->Hover(cmd.context_id, cmd.selector);
    result.selector = cmd.selector;
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "doubleClick") {
    if (cmd.selector.empty()) {
      SendActionResult(cmd.id, ActionResult::Failure(ActionStatus::INVALID_SELECTOR, "Selector cannot be empty"));
      return;
    }
    // Method now returns ActionResult directly
    ActionResult result = mgr->DoubleClick(cmd.context_id, cmd.selector);
    result.selector = cmd.selector;
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "rightClick") {
    if (cmd.selector.empty()) {
      SendActionResult(cmd.id, ActionResult::Failure(ActionStatus::INVALID_SELECTOR, "Selector cannot be empty"));
      return;
    }
    // Method now returns ActionResult directly
    ActionResult result = mgr->RightClick(cmd.context_id, cmd.selector);
    result.selector = cmd.selector;
    SendActionResult(cmd.id, result);
  }
  // ===== Input Control =====
  else if (cmd.method == "clearInput") {
    if (cmd.selector.empty()) {
      SendActionResult(cmd.id, ActionResult::Failure(ActionStatus::INVALID_SELECTOR, "Selector cannot be empty"));
      return;
    }
    // Method now returns ActionResult directly
    ActionResult result = mgr->ClearInput(cmd.context_id, cmd.selector);
    result.selector = cmd.selector;
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "focus") {
    if (cmd.selector.empty()) {
      SendActionResult(cmd.id, ActionResult::Failure(ActionStatus::INVALID_SELECTOR, "Selector cannot be empty"));
      return;
    }
    // Method now returns ActionResult directly
    ActionResult result = mgr->Focus(cmd.context_id, cmd.selector);
    result.selector = cmd.selector;
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "blur") {
    if (cmd.selector.empty()) {
      SendActionResult(cmd.id, ActionResult::Failure(ActionStatus::INVALID_SELECTOR, "Selector cannot be empty"));
      return;
    }
    // Method now returns ActionResult directly
    ActionResult result = mgr->Blur(cmd.context_id, cmd.selector);
    result.selector = cmd.selector;
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "selectAll") {
    if (cmd.selector.empty()) {
      SendActionResult(cmd.id, ActionResult::Failure(ActionStatus::INVALID_SELECTOR, "Selector cannot be empty"));
      return;
    }
    // Method now returns ActionResult directly
    ActionResult result = mgr->SelectAll(cmd.context_id, cmd.selector);
    result.selector = cmd.selector;
    SendActionResult(cmd.id, result);
  }
  // ===== Keyboard Combinations =====
  else if (cmd.method == "keyboardCombo") {
    // Method now returns ActionResult directly
    ActionResult result = mgr->KeyboardCombo(cmd.context_id, cmd.combo);
    SendActionResult(cmd.id, result);
  }
  // ===== JavaScript Evaluation =====
  else if (cmd.method == "evaluate") {
    std::string result = mgr->Evaluate(cmd.context_id, cmd.script, cmd.return_value);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  // ===== Element State Checks =====
  else if (cmd.method == "isVisible") {
    ActionResult result = mgr->IsVisible(cmd.context_id, cmd.selector);
    result.selector = cmd.selector;
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "isEnabled") {
    ActionResult result = mgr->IsEnabled(cmd.context_id, cmd.selector);
    result.selector = cmd.selector;
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "isChecked") {
    ActionResult result = mgr->IsChecked(cmd.context_id, cmd.selector);
    result.selector = cmd.selector;
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "getAttribute") {
    std::string value = mgr->GetAttribute(cmd.context_id, cmd.selector, cmd.attribute);
    SendResponse(cmd.id, value);
  }
  else if (cmd.method == "getBoundingBox") {
    std::string result = mgr->GetBoundingBox(cmd.context_id, cmd.selector);
    std::string response = "{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}";
    SendRawJsonResponse(response);
  }
  else if (cmd.method == "getElementAtPosition") {
    std::string result = mgr->GetElementAtPosition(cmd.context_id, cmd.x, cmd.y);
    std::string response = "{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}";
    SendRawJsonResponse(response);
  }
  else if (cmd.method == "getInteractiveElements") {
    std::string result = mgr->GetInteractiveElements(cmd.context_id);
    std::string response = "{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}";
    SendRawJsonResponse(response);
  }
  // ===== File Operations =====
  else if (cmd.method == "uploadFile") {
    // Parse file_paths JSON array
    std::vector<std::string> paths;
    if (!cmd.file_paths.empty()) {
      // Simple JSON array parsing: ["path1","path2"]
      std::string paths_str = cmd.file_paths;
      size_t pos = 0;
      while ((pos = paths_str.find('"', pos)) != std::string::npos) {
        size_t start = pos + 1;
        size_t end = paths_str.find('"', start);
        if (end == std::string::npos) break;
        paths.push_back(paths_str.substr(start, end - start));
        pos = end + 1;
      }
    }
    ActionResult result = mgr->UploadFile(cmd.context_id, cmd.selector, paths);
    SendActionResult(cmd.id, result);
  }
  // ===== Frame/Iframe Handling =====
  else if (cmd.method == "listFrames") {
    std::string result = mgr->ListFrames(cmd.context_id);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  else if (cmd.method == "switchToFrame") {
    ActionResult result = mgr->SwitchToFrame(cmd.context_id, cmd.frame_selector);
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "switchToMainFrame") {
    ActionResult result = mgr->SwitchToMainFrame(cmd.context_id);
    SendActionResult(cmd.id, result);
  }
  // ===== Network Interception =====
  else if (cmd.method == "addNetworkRule") {
    std::string rule_id = mgr->AddNetworkRule(cmd.context_id, cmd.rule_json);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":{\"rule_id\":\"" + rule_id + "\"}}");
  }
  else if (cmd.method == "removeNetworkRule") {
    bool success = mgr->RemoveNetworkRule(cmd.rule_id);
    SendBoolResponse(cmd.id, success);
  }
  else if (cmd.method == "enableNetworkInterception") {
    mgr->EnableNetworkInterception(cmd.context_id, cmd.enable);
    SendBoolResponse(cmd.id, true);
  }
  else if (cmd.method == "enableNetworkLogging") {
    mgr->EnableNetworkLogging(cmd.context_id, cmd.enable);
    SendBoolResponse(cmd.id, true);
  }
  else if (cmd.method == "getNetworkLog") {
    std::string result = mgr->GetNetworkLog(cmd.context_id);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  else if (cmd.method == "clearNetworkLog") {
    mgr->ClearNetworkLog(cmd.context_id);
    SendBoolResponse(cmd.id, true);
  }
  // ===== Console Log Management =====
  else if (cmd.method == "enableConsoleLogging") {
    mgr->EnableConsoleLogging(cmd.context_id, cmd.enable);
    SendBoolResponse(cmd.id, true);
  }
  else if (cmd.method == "getConsoleLogs") {
    // limit defaults to 0 (unlimited) if not specified or -1 from ExtractJsonInt
    int limit = cmd.limit > 0 ? cmd.limit : 0;
    std::string result = mgr->GetConsoleLogs(cmd.context_id, cmd.level_filter, cmd.text_filter, limit);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  else if (cmd.method == "clearConsoleLogs") {
    mgr->ClearConsoleLogs(cmd.context_id);
    SendBoolResponse(cmd.id, true);
  }
  // ===== Download Management =====
  else if (cmd.method == "setDownloadPath") {
    mgr->SetDownloadPath(cmd.context_id, cmd.download_path);
    SendBoolResponse(cmd.id, true);
  }
  else if (cmd.method == "getDownloads") {
    std::string result = mgr->GetDownloads(cmd.context_id);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  else if (cmd.method == "getActiveDownloads") {
    std::string result = mgr->GetActiveDownloads(cmd.context_id);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  else if (cmd.method == "waitForDownload") {
    if (cmd.download_id.empty()) {
      SendError(cmd.id, "download_id is required");
    } else {
      bool success = mgr->WaitForDownload(cmd.download_id, cmd.timeout > 0 ? cmd.timeout : 30000);
      if (!success) {
        SendError(cmd.id, "Download not found or timed out: " + cmd.download_id);
      } else {
        SendBoolResponse(cmd.id, success);
      }
    }
  }
  else if (cmd.method == "cancelDownload") {
    if (cmd.download_id.empty()) {
      SendError(cmd.id, "download_id is required");
    } else {
      bool success = mgr->CancelDownload(cmd.download_id);
      if (!success) {
        SendError(cmd.id, "Download not found: " + cmd.download_id);
      } else {
        SendBoolResponse(cmd.id, success);
      }
    }
  }
  // ===== Dialog Handling =====
  else if (cmd.method == "setDialogAction") {
    mgr->SetDialogAction(cmd.context_id, cmd.dialog_type, cmd.action, cmd.prompt_text);
    SendBoolResponse(cmd.id, true);
  }
  else if (cmd.method == "getPendingDialog") {
    std::string result = mgr->GetPendingDialog(cmd.context_id);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  else if (cmd.method == "handleDialog") {
    if (cmd.dialog_id.empty()) {
      SendError(cmd.id, "dialog_id is required");
    } else {
      bool success = mgr->HandleDialog(cmd.dialog_id, cmd.accept, cmd.response_text);
      if (!success) {
        SendError(cmd.id, "Dialog not found: " + cmd.dialog_id);
      } else {
        SendBoolResponse(cmd.id, success);
      }
    }
  }
  else if (cmd.method == "waitForDialog") {
    bool success = mgr->WaitForDialog(cmd.context_id, cmd.timeout > 0 ? cmd.timeout : 5000);
    if (!success) {
      SendError(cmd.id, "No dialog appeared within timeout");
    } else {
      SendBoolResponse(cmd.id, success);
    }
  }
  else if (cmd.method == "getDialogs") {
    std::string result = mgr->GetDialogs(cmd.context_id);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  // ===== Tab/Window Management =====
  else if (cmd.method == "setPopupPolicy") {
    mgr->SetPopupPolicy(cmd.context_id, cmd.popup_policy);
    SendBoolResponse(cmd.id, true);
  }
  else if (cmd.method == "getTabs") {
    std::string result = mgr->GetTabs(cmd.context_id);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  else if (cmd.method == "switchTab") {
    ActionResult result = mgr->SwitchTab(cmd.context_id, cmd.tab_id);
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "closeTab") {
    ActionResult result = mgr->CloseTab(cmd.context_id, cmd.tab_id);
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "newTab") {
    std::string tab_id = mgr->NewTab(cmd.context_id, cmd.url);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":{\"tab_id\":\"" + tab_id + "\"}}");
  }
  else if (cmd.method == "getActiveTab") {
    std::string tab_id = mgr->GetActiveTab(cmd.context_id);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":{\"tab_id\":\"" + tab_id + "\"}}");
  }
  else if (cmd.method == "getTabCount") {
    int count = mgr->GetTabCount(cmd.context_id);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":{\"count\":" + std::to_string(count) + "}}");
  }
  else if (cmd.method == "getBlockedPopups") {
    std::string result = mgr->GetBlockedPopups(cmd.context_id);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  // ===== Clipboard Management =====
  else if (cmd.method == "clipboardRead") {
    std::string result = mgr->ClipboardRead(cmd.context_id);
    SendRawJsonResponse("{\"id\":" + std::to_string(cmd.id) + ",\"result\":" + result + "}");
  }
  else if (cmd.method == "clipboardWrite") {
    ActionResult result = mgr->ClipboardWrite(cmd.context_id, cmd.clipboard_text);
    SendActionResult(cmd.id, result);
  }
  else if (cmd.method == "clipboardClear") {
    ActionResult result = mgr->ClipboardClear(cmd.context_id);
    SendActionResult(cmd.id, result);
  }
  else {
    SendError(cmd.id, "Unknown method: " + cmd.method);
  }
}

int main(int argc, char* argv[]) {
#ifdef OS_LINUX
  // CRITICAL: Change to executable directory FIRST, before ANYTHING else
  // This must happen before logger init, before parsing args, before EVERYTHING
  // CEF will look for resources relative to CWD during initialization
  {
    char exe_path[1024];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
      exe_path[len] = '\0';
      std::string path_str = exe_path;
      size_t last_slash = path_str.find_last_of('/');
      if (last_slash != std::string::npos) {
        std::string exe_dir = path_str.substr(0, last_slash);
        if (chdir(exe_dir.c_str()) != 0) {
          // Log error to stderr only on failure
          std::cerr << "[ERROR] Failed to chdir to: " << exe_dir << std::endl;
        }
      }
    }
  }
#endif

  // Parse instance ID from command line or environment (before initializing logger)
  std::string instance_id = "default";

  // Check command line arguments first
  for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == "--instance-id" && i + 1 < argc) {
      instance_id = argv[i + 1];
      break;
    }
  }

  // Fall back to environment variable if not in args
  if (instance_id == "default") {
    const char* env_id = std::getenv("OLIB_INSTANCE_ID");
    if (env_id != nullptr && strlen(env_id) > 0) {
      instance_id = env_id;
    }
  }

  // Initialize logger with log file path (must be after instance_id is determined)
  std::string log_file = "/tmp/owl_browser_" + instance_id + ".log";
  OlibLogger::Logger::Init(log_file);

  LOG_DEBUG("SubProcess", "Browser instance starting with ID: " + instance_id);

  // =========================================================================
  // License CLI Commands (--license add/remove/info/status)
  // =========================================================================
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--license" && i + 1 < argc) {
      std::string license_cmd = argv[i + 1];

      if (license_cmd == "add" && i + 2 < argc) {
        // Add license: owl_browser --license add /path/to/license.olic
        std::string license_path = argv[i + 2];
        auto* mgr = olib::license::LicenseManager::GetInstance();
        auto status = mgr->AddLicense(license_path);
        if (status == olib::license::LicenseStatus::VALID) {
          std::cout << "License activated successfully!" << std::endl;
          std::cout << mgr->GetLicenseInfo() << std::endl;
          return 0;
        } else {
          std::cerr << "Failed to activate license: "
                    << olib::license::LicenseStatusToString(status) << std::endl;
          return 1;
        }
      }
      else if (license_cmd == "remove") {
        // Remove license: owl_browser --license remove
        auto* mgr = olib::license::LicenseManager::GetInstance();
        mgr->RemoveLicense();
        std::cout << "License removed." << std::endl;
        return 0;
      }
      else if (license_cmd == "info") {
        // Show license info: owl_browser --license info
        auto* mgr = olib::license::LicenseManager::GetInstance();
        mgr->Validate();
        std::cout << mgr->GetLicenseInfo() << std::endl;
        return 0;
      }
      else if (license_cmd == "status") {
        // Check license status: owl_browser --license status
        auto* mgr = olib::license::LicenseManager::GetInstance();
        auto status = mgr->Validate();
        std::cout << "License Status: " << olib::license::LicenseStatusToString(status) << std::endl;
        if (status == olib::license::LicenseStatus::VALID) {
          std::cout << "License is valid." << std::endl;
          return 0;
        } else {
          std::cerr << "License is not valid." << std::endl;
          return 1;
        }
      }
      else if (license_cmd == "fingerprint") {
        // Show hardware fingerprint: owl_browser --license fingerprint
        std::string fingerprint = olib::license::HardwareFingerprint::Generate();
        std::cout << "Hardware Fingerprint: " << fingerprint << std::endl;
        return 0;
      }
      else {
        std::cerr << "Unknown license command: " << license_cmd << std::endl;
        std::cerr << "Usage:" << std::endl;
        std::cerr << "  --license add <path>    Add/activate a license file" << std::endl;
        std::cerr << "  --license remove        Remove the current license" << std::endl;
        std::cerr << "  --license info          Show license information" << std::endl;
        std::cerr << "  --license status        Check license status" << std::endl;
        std::cerr << "  --license fingerprint   Show hardware fingerprint" << std::endl;
        return 1;
      }
    }
  }

  // =========================================================================
  // License Validation (required for browser to function)
  // =========================================================================
  {
    auto* license_mgr = olib::license::LicenseManager::GetInstance();
    auto license_status = license_mgr->Validate();

    if (license_status != olib::license::LicenseStatus::VALID) {
      std::cerr << "LICENSE REQUIRED: " << olib::license::LicenseStatusToString(license_status) << std::endl;
      std::cerr << "Run: owl_browser --license add /path/to/license.olic" << std::endl;
      std::cerr << "Fingerprint: " << olib::license::HardwareFingerprint::Generate() << std::endl;

      LOG_ERROR("License", "Validation: " +
                std::string(olib::license::LicenseStatusToString(license_status)));
      return 1;
    }

    LOG_INFO("License", "OK");
  }

#ifdef OS_MACOS
  // Load the CEF framework library at runtime (macOS only)
  CefScopedLibraryLoader library_loader;
  if (!library_loader.LoadInMain()) {
    LOG_ERROR("Main", "Failed to load CEF library");
    return 1;
  }
#endif
  // On Linux/Windows, CEF is linked at compile time

  // Command line switches are now set in OnBeforeCommandLineProcessing in owl_app.cc

#ifdef OS_LINUX
  // Get executable directory for CefSettings (chdir already done at start of main())
  std::string exe_dir;
  {
    char exe_path[1024];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
      exe_path[len] = '\0';
      exe_dir = exe_path;
      size_t last_slash = exe_dir.find_last_of('/');
      if (last_slash != std::string::npos) {
        exe_dir = exe_dir.substr(0, last_slash);
      }
    }
  }
#endif

#ifdef OS_LINUX
  // CRITICAL: Build argv with all required flags for CEF
  std::vector<char*> new_argv;
  std::vector<std::string> arg_storage;  // Keep strings alive

  // Copy original arguments
  for (int i = 0; i < argc; i++) {
    new_argv.push_back(argv[i]);
  }

  // Add headless Ozone flag if not already present
  bool has_ozone = false;
  for (int i = 0; i < argc; i++) {
    if (strstr(argv[i], "--ozone-platform") != nullptr) {
      has_ozone = true;
      break;
    }
  }

  if (!has_ozone) {
    arg_storage.push_back("--ozone-platform=headless");
    new_argv.push_back(const_cast<char*>(arg_storage.back().c_str()));
  }

  CefMainArgs main_args(new_argv.size(), new_argv.data());
#else
  CefMainArgs main_args(argc, argv);
#endif

  CefRefPtr<OwlApp> app(new OwlApp);

  // Handle CEF helper processes - they should execute and exit here
  int exit_code = CefExecuteProcess(main_args, app.get(), nullptr);
  if (exit_code >= 0) {
    // This is a helper process, exit immediately
    return exit_code;
  }

  // Only the main browser process continues here
  LOG_DEBUG("Main", "owl_browser main process starting");

  // CEF settings
  CefSettings settings;
  settings.no_sandbox = true;
  settings.remote_debugging_port = 0;
  settings.log_severity = LOGSEVERITY_VERBOSE;  // Enable verbose logging for debugging

  // Use instance-specific log file to avoid conflicts (same as our Logger)
  CefString(&settings.log_file) = log_file;

  settings.windowless_rendering_enabled = true;
  settings.multi_threaded_message_loop = false;

  // Smart cache system - use instance-specific cache directories to allow parallel instances
  // Each instance gets its own cache to avoid SingletonLock conflicts
  std::string cache_path = "/tmp/owl_browser_cache_" + instance_id;
  CefString(&settings.cache_path) = cache_path;
  CefString(&settings.root_cache_path) = cache_path;

  LOG_DEBUG("Main", "Using instance-specific cache: " + cache_path);

  // Configure cache behavior for AI efficiency
  // MEMORY OPTIMIZATION: Reduced from 100MB to 50MB per context
  // Cache is persistent across browser sessions

  CefString(&settings.browser_subprocess_path) = "";  // Use the same executable

#ifdef OS_LINUX
  // On Linux, CEF needs explicit resource and framework paths
  // CRITICAL: Reuse exe_dir from earlier (NEVER use getcwd() - it gives wrong path!)
  if (!exe_dir.empty()) {
    // Set all required paths for Linux
    CefString(&settings.resources_dir_path) = exe_dir;
    CefString(&settings.locales_dir_path) = exe_dir + "/locales";
    CefString(&settings.framework_dir_path) = exe_dir;
    LOG_DEBUG("Main", "Linux CEF paths configured from: " + exe_dir);
  }
#endif

  // Set user agent - MUST match actual CEF version to avoid API mismatch detection
  // Browser version is loaded dynamically from VirtualMachineDB config
  CefString(&settings.user_agent) = owl::VirtualMachineDB::Instance().GetDefaultUserAgent();

#ifdef OS_MACOS
  // Initialize headless NSApplication with CefAppProtocol support
  // This is required for SendKeyEvent to work (calls isHandlingSendEvent)
  InitializeHeadlessNSApplication();
#endif

  // Initialize CEF
  if (!CefInitialize(main_args, settings, app.get(), nullptr)) {
    LOG_ERROR("Main", "Failed to initialize CEF");
    return 1;
  }
  LOG_DEBUG("Main", "CEF initialized successfully");

  OwlBrowserManager::GetInstance()->Initialize();

  // Set message loop mode to manual pumping (Headless mode)
  OwlBrowserManager::SetUsesRunMessageLoop(false);

#if defined(OS_LINUX) || defined(OS_MACOS)
  // Initialize Multi-IPC Server for parallel command processing (Linux/macOS)
  // This allows multiple concurrent connections, each processing commands independently
  g_ipc_server = std::make_unique<owl::IPCServer>();
  if (g_ipc_server->Initialize(instance_id, [](const std::string& command) -> std::string {
    // This handler is called from worker threads - commands for different
    // contexts can be processed in parallel
    return ProcessCommandAndGetResponse(command);
  })) {
    g_ipc_server->Start();
    std::cout << "MULTI_IPC_READY " << g_ipc_server->GetSocketPath() << std::endl;
    std::cout.flush();
    LOG_DEBUG("Main", "Multi-IPC server started at " + g_ipc_server->GetSocketPath());
  } else {
    LOG_WARN("Main", "Failed to start multi-IPC server, using single-IPC mode");
    g_ipc_server.reset();
  }
#endif

  // READY signal goes to stdout so IPC clients can detect it
  // (stderr may be redirected to /dev/null in test clients)
  std::cout << "READY" << std::endl;
  std::cout.flush();

  // Start stdin reading thread (always active for backward compatibility)
  std::thread stdin_thread([]() {
    std::string line;
    while (!should_quit) {
      if (std::getline(std::cin, line)) {
        LOG_DEBUG("StdinThread", "Read command: " + line);
        if (!line.empty()) {
          std::lock_guard<std::mutex> lock(queue_mutex);
          command_queue.push(line);
          queue_cv.notify_one();
        }
      } else if (std::cin.eof()) {
        LOG_DEBUG("StdinThread", "Stdin EOF reached");
        should_quit = true;
        queue_cv.notify_one();
        break;
      } else if (std::cin.fail()) {
        std::cin.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }
  });

  // Main thread: process commands and CEF message loop
  // PERFORMANCE OPTIMIZATION: Process ALL pending commands before pumping
  // PARALLEL WAITING: waitForNavigation commands run in parallel threads

  // Track active navigation wait threads
  std::vector<std::thread> active_wait_threads;
  std::atomic<int> active_waits{0};

  while (!should_quit) {
    // Process ALL pending commands in batch (unlimited - drain entire queue)
    std::vector<std::string> commands_batch;
    {
      std::unique_lock<std::mutex> lock(queue_mutex);

      // Wait for commands if none pending and no active waits
      // Also check IPC command queue (Linux/macOS)
#if defined(OS_LINUX) || defined(OS_MACOS)
      bool has_ipc_commands = false;
      {
        std::lock_guard<std::mutex> ipc_lock(ipc_queue_mutex);
        has_ipc_commands = !ipc_command_queue.empty();
      }
      if (command_queue.empty() && active_waits.load() == 0 && !has_ipc_commands) {
        queue_cv.wait_for(lock, std::chrono::milliseconds(10), []() {
          return !command_queue.empty() || should_quit;
        });
      }
#else
      if (command_queue.empty() && active_waits.load() == 0) {
        queue_cv.wait(lock, []() {
          return !command_queue.empty() || should_quit;
        });
      }
#endif

      // Brief delay to let command burst accumulate
      if (!command_queue.empty() && !should_quit) {
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        lock.lock();
      }

      // Drain entire queue
      while (!command_queue.empty()) {
        commands_batch.push_back(command_queue.front());
        command_queue.pop();
      }
    }

    // Commands that MUST run on the UI thread (CEF browser creation/destruction)
    // Commands using CefDoMessageLoopWork or browser input events need UI thread
    static const std::unordered_set<std::string> ui_thread_commands = {
      // Context lifecycle
      "createContext",    // Uses CreateBrowserSync - must be on UI thread
      "releaseContext",   // Mark context as not in use (for pooling)
      "closeContext",     // Actually close and destroy browser - needs UI thread
      "shutdown",         // Coordinated shutdown
      "listContexts",     // Fast, simple query
      // License operations
      "getLicenseStatus", // Simple query
      "getLicenseInfo",   // Simple query
      "addLicense",       // License operations
      "removeLicense",    // License operations
      "getHardwareFingerprint", // Simple query
      // Navigation commands (use CefDoMessageLoopWork or LoadURL)
      "navigate",          // LoadURL must be called on UI thread
      "reload",            // Browser reload on UI thread
      "goBack",            // Browser navigation on UI thread
      "goForward",         // Browser navigation on UI thread
      "waitForNavigation", // Calls CefDoMessageLoopWork - must be on UI thread
      "waitForSelector",   // May call CefDoMessageLoopWork
      "waitForFunction",   // May call CefDoMessageLoopWork
      "waitForURL",        // May call CefDoMessageLoopWork
      "waitForNetworkIdle", // May call CefDoMessageLoopWork
      // Input/interaction commands (use CefDoMessageLoopWork for events)
      "click",            // Uses CefDoMessageLoopWork for mouse events
      "doubleClick",      // Uses CefDoMessageLoopWork for mouse events
      "rightClick",       // Uses CefDoMessageLoopWork for mouse events
      "hover",            // Uses CefDoMessageLoopWork for mouse events
      "mouseMove",        // Uses CefDoMessageLoopWork for mouse events
      "dragDrop",         // Uses CefDoMessageLoopWork for mouse events
      "html5DragDrop",    // Uses CefDoMessageLoopWork for mouse events
      "type",             // Uses CefDoMessageLoopWork for key events
      "pressKey",         // Uses CefDoMessageLoopWork for key events
      "keyboardCombo",    // Uses CefDoMessageLoopWork for key events
      "submitForm",       // Uses CefDoMessageLoopWork
      "pick",             // Uses Click internally
      "clearInput",       // Uses SendKeyEvent - must be on UI thread
      "selectAll",        // Uses SendKeyEvent - must be on UI thread
      "focus",            // Uses SendProcessMessage - must be on UI thread
      "blur",             // Uses SendProcessMessage - must be on UI thread
      // Element operations that may use CefDoMessageLoopWork
      "findElement",      // May block waiting for element scan
      "getElementAtPosition", // May block waiting for element scan
    };

    // Separate commands by processing mode
    std::vector<std::string> ui_thread_cmds;
    std::vector<std::string> parallel_cmds;

    for (const auto& cmd_str : commands_batch) {
      // Parse to get method name for classification
      bool is_ui_thread = false;
      for (const auto& ui_cmd : ui_thread_commands) {
        if (cmd_str.find("\"" + ui_cmd + "\"") != std::string::npos) {
          is_ui_thread = true;
          break;
        }
      }

      if (is_ui_thread) {
        ui_thread_cmds.push_back(cmd_str);
      } else {
        parallel_cmds.push_back(cmd_str);
      }
    }

    // Process UI-thread commands sequentially (they're typically fast)
    for (const auto& command : ui_thread_cmds) {
      LOG_DEBUG("MainLoop", "Processing UI-thread command: " + command.substr(0, 80));
      ProcessCommand(command);
    }

    // Process all other commands in PARALLEL threads
    for (const auto& command : parallel_cmds) {
      active_waits.fetch_add(1);
      active_wait_threads.emplace_back([command, &active_waits]() {
        LOG_DEBUG("ParallelThread", "Processing command: " + command.substr(0, 80));
        ProcessCommand(command);
        active_waits.fetch_sub(1);
      });
    }

#if defined(OS_LINUX) || defined(OS_MACOS)
    // Process IPC commands from multi-IPC socket connections
    // PREDICTIVE PUMPING: Enable batch mode, queue all events, pump once, send responses
    {
      std::vector<std::unique_ptr<IPCCommand>> ipc_batch;
      {
        std::lock_guard<std::mutex> lock(ipc_queue_mutex);
        while (!ipc_command_queue.empty()) {
          ipc_batch.push_back(std::move(ipc_command_queue.front()));
          ipc_command_queue.pop();
        }
      }

      for (auto& ipc_cmd : ipc_batch) {
        LOG_DEBUG("MainLoop", "Processing IPC command: " + ipc_cmd->command.substr(0, 80));

        // Set up thread-local response capture
        tls_use_direct_response = true;
        tls_response.clear();

        // Process the command (this will set tls_response)
        ProcessCommand(ipc_cmd->command);

        // Get the response and send it back via the promise
        std::string response = tls_response;
        tls_use_direct_response = false;

        // Set the response via promise so the waiting IPC worker thread can return it
        ipc_cmd->response_promise.set_value(response);
      }
    }
#endif

    // Pump CEF message loop - this drives rendering/navigation for ALL contexts
    CefDoMessageLoopWork();

    // Clean up finished threads periodically
    if (!active_wait_threads.empty()) {
      auto it = active_wait_threads.begin();
      while (it != active_wait_threads.end()) {
        if (it->joinable()) {
          // Try to join with zero timeout (non-blocking check)
          // Note: C++ doesn't have try_join, so we just leave threads running
          // They will complete on their own
          ++it;
        } else {
          it = active_wait_threads.erase(it);
        }
      }
    }

    // Small sleep if we have active waits but no new commands
    // This prevents busy-spinning while waiting
    if (commands_batch.empty() && active_waits.load() > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  // Wait for all active threads to complete before shutdown
  for (auto& t : active_wait_threads) {
    if (t.joinable()) {
      t.join();
    }
  }

  // Shutdown
  should_quit = true;

#if defined(OS_LINUX) || defined(OS_MACOS)
  // Stop multi-IPC server first
  if (g_ipc_server) {
    LOG_DEBUG("Main", "Stopping multi-IPC server...");
    g_ipc_server->Stop();
    g_ipc_server.reset();
  }
#endif

  stdin_thread.join();

  OwlBrowserManager::GetInstance()->Shutdown();
  CefShutdown();

  return 0;
}
