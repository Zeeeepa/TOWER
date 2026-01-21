#include "owl_client.h"
#include "owl_resource_blocker.h"
#include "owl_render_tracker.h"
#include "owl_semantic_matcher.h"
#include "owl_video_recorder.h"
#include "owl_live_streamer.h"
#include "owl_test_scheme_handler.h"
#include "owl_browser_manager.h"
#include "owl_stealth.h"
#include "owl_network_interceptor.h"
#include "owl_console_logger.h"
#include "stealth/owl_virtual_machine.h"
#include "stealth/owl_spoof_manager.h"
#include "stealth/workers/owl_worker_patcher.h"
#include "logger.h"
#include "include/cef_app.h"
#include "include/cef_process_message.h"
#include "include/cef_urlrequest.h"
#include "include/wrapper/cef_helpers.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <thread>
#include <iomanip>
#include <sstream>
#include <zlib.h>
#include <algorithm>
#include <random>

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#endif

// Static counter for Tor circuit isolation - each context gets a unique ID
std::atomic<uint64_t> OwlClient::circuit_counter_{0};

// Weak stub for dev console (overridden in UI process)
#ifdef OS_MACOS
extern "C" {
  // Default stub implementation for helper processes (does nothing)
  // This will be overridden by the real implementation in owl_dev_console.mm for UI process
  __attribute__((weak))
  void OwlDevConsole_AddMessage(const char* level, const char* message, const char* source, int line) {
    // No-op for helper processes
  }

  __attribute__((weak))
  void OwlDevConsole_AddNetworkRequest(const char* url, const char* method,
                                        const char* type, int status_code,
                                        const char* status_text, size_t size,
                                        int duration_ms) {
    // No-op for helper processes
  }

  __attribute__((weak))
  void OwlDevConsole_AddNetworkRequestExtended(
      const char* url, const char* method, const char* type,
      int status_code, const char* status_text, size_t size, int duration_ms,
      const char* request_headers, const char* response_headers,
      const char* url_params, const char* post_data) {
    // No-op for helper processes
  }
}
#endif

// ============================================================================
// SERVICE WORKER SCRIPT RESPONSE FILTER
// Uses CefResponseFilter to prepend navigator spoofing code to SW scripts
// This runs on IO thread and doesn't have threading issues like CefURLRequest
//
// Uses the unified WorkerPatcher classes from stealth/workers/
// ============================================================================

#include "include/cef_response_filter.h"

// Response filter that prepends patch code to Service Worker scripts
// Uses ServiceWorkerPatcher for generating the patch code
class ServiceWorkerResponseFilter : public CefResponseFilter {
 public:
  ServiceWorkerResponseFilter(int browser_id) : browser_id_(browser_id) {
    LOG_INFO("SWDEBUG", "ServiceWorkerResponseFilter created for browser_id=" + std::to_string(browser_id_));
  }

  bool InitFilter() override {
    return true;
  }

  FilterStatus Filter(void* data_in, size_t data_in_size, size_t& data_in_read,
                      void* data_out, size_t data_out_size, size_t& data_out_written) override {
    data_in_read = 0;
    data_out_written = 0;

    // Log when Filter is called
    LOG_DEBUG("SWDEBUG", "Filter() called: data_in_size=" + std::to_string(data_in_size) +
             " data_out_size=" + std::to_string(data_out_size) +
             " patch_generated=" + std::string(patch_generated_ ? "true" : "false") +
             " prefix_written=" + std::string(prefix_written_ ? "true" : "false") +
             " buffered=" + std::to_string(buffered_data_.size()));

    // CRITICAL: Always consume ALL incoming data into our buffer first!
    // CEF won't re-present data we don't consume, so we must buffer it.
    if (data_in_size > 0) {
      buffered_data_.insert(buffered_data_.end(),
                            static_cast<const char*>(data_in),
                            static_cast<const char*>(data_in) + data_in_size);
      data_in_read = data_in_size;  // Mark ALL input as consumed
    }

    // STEP 1: Generate patch code once we have enough data or end of stream
    // We wait for some data to detect ES module type (for logging purposes only)
    if (!patch_generated_) {
      const size_t MIN_DETECT_SIZE = 512;

      // Check if we have enough data OR end of stream
      if (buffered_data_.size() >= MIN_DETECT_SIZE || data_in_size == 0) {
        patch_generated_ = true;

        // Detect ES module for logging
        std::string content(buffered_data_.begin(), buffered_data_.end());
        is_es_module_ = owl::workers::WorkerPatcher::IsESModule(content);

        // Use ServiceWorkerPatcher to generate patch code
        owl::workers::ServiceWorkerPatcher patcher;
        patch_code_ = patcher.GetPatchCode(browser_id_);

        LOG_INFO("SWDEBUG", "Patch generated via ServiceWorkerPatcher: is_es_module=" +
                 std::string(is_es_module_ ? "TRUE" : "FALSE") +
                 " buffered=" + std::to_string(buffered_data_.size()) +
                 " patch_size=" + std::to_string(patch_code_.size()));
      }
      // Early detection for logging
      else if (buffered_data_.size() >= 64) {
        std::string preview(buffered_data_.begin(),
                           buffered_data_.begin() + std::min(buffered_data_.size(), size_t(256)));
        // Check for clear ES module markers
        if (preview.find("import ") != std::string::npos ||
            preview.find("import'") != std::string::npos ||
            preview.find("import\"") != std::string::npos ||
            preview.find("export ") != std::string::npos) {
          patch_generated_ = true;
          is_es_module_ = true;

          // Use ServiceWorkerPatcher
          owl::workers::ServiceWorkerPatcher patcher;
          patch_code_ = patcher.GetPatchCode(browser_id_);

          LOG_INFO("SWDEBUG", "EARLY ES module detection: found import/export in first " +
                   std::to_string(preview.size()) + " bytes");
        }
      }

      // If still not generated, wait for more data
      if (!patch_generated_) {
        LOG_DEBUG("SWDEBUG", "Waiting for more data: " +
                 std::to_string(buffered_data_.size()) + "/" + std::to_string(MIN_DETECT_SIZE));
        return RESPONSE_FILTER_NEED_MORE_DATA;
      }
    }

    // STEP 2: Write the patch code prefix
    if (!prefix_written_) {
      size_t prefix_remaining = patch_code_.size() - prefix_offset_;
      if (prefix_remaining > 0) {
        size_t to_write = std::min(prefix_remaining, data_out_size);
        memcpy(data_out, patch_code_.data() + prefix_offset_, to_write);
        prefix_offset_ += to_write;
        data_out_written = to_write;

        if (prefix_offset_ < patch_code_.size()) {
          return RESPONSE_FILTER_NEED_MORE_DATA;
        }
      }
      prefix_written_ = true;
      LOG_INFO("SWDEBUG", "Prefix fully written, buffered data size=" + std::to_string(buffered_data_.size()));
    }

    // STEP 3: Output buffered data
    if (buffer_offset_ < buffered_data_.size()) {
      size_t remaining_out = data_out_size - data_out_written;
      size_t remaining_buf = buffered_data_.size() - buffer_offset_;
      size_t to_copy = std::min(remaining_buf, remaining_out);
      if (to_copy > 0) {
        memcpy(static_cast<char*>(data_out) + data_out_written,
               buffered_data_.data() + buffer_offset_, to_copy);
        buffer_offset_ += to_copy;
        data_out_written += to_copy;
      }

      if (buffer_offset_ < buffered_data_.size()) {
        return RESPONSE_FILTER_NEED_MORE_DATA;
      }
    }

    // Check if we're done: prefix written, all buffered data output, and no more input
    if (data_in_size == 0 && prefix_written_ && buffer_offset_ >= buffered_data_.size()) {
      LOG_INFO("SWDEBUG", "Filter complete. Total output: " +
               std::to_string(patch_code_.size() + buffered_data_.size()) + " bytes" +
               " is_es_module=" + std::string(is_es_module_ ? "true" : "false"));
      return RESPONSE_FILTER_DONE;
    }

    return RESPONSE_FILTER_NEED_MORE_DATA;
  }

 private:
  int browser_id_;
  std::string patch_code_;
  size_t prefix_offset_ = 0;
  bool prefix_written_ = false;
  bool patch_generated_ = false;
  bool is_es_module_ = false;

  // Buffer for incoming data
  std::vector<char> buffered_data_;
  size_t buffer_offset_ = 0;

  IMPLEMENT_REFCOUNTING(ServiceWorkerResponseFilter);
};

// ResourceRequestHandler that provides ResponseFilter for SW scripts
class ServiceWorkerResourceRequestHandler : public CefResourceRequestHandler {
 public:
  ServiceWorkerResourceRequestHandler(int browser_id) : browser_id_(browser_id) {}

  CefRefPtr<CefResponseFilter> GetResourceResponseFilter(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      CefRefPtr<CefResponse> response) override {
    std::string url = request ? request->GetURL().ToString() : "null";
    std::string mime = response ? response->GetMimeType().ToString() : "null";
    int status = response ? response->GetStatus() : 0;

    LOG_INFO("SWDEBUG", ">>> GetResourceResponseFilter CALLED: " + url +
             " MIME=" + mime + " Status=" + std::to_string(status));

    // Only filter JavaScript responses
    if (mime.find("javascript") != std::string::npos ||
        mime.find("ecmascript") != std::string::npos ||
        url.find(".js") != std::string::npos) {
      LOG_INFO("SWDEBUG", ">>> APPLYING SW ResponseFilter for: " + url);
      return new ServiceWorkerResponseFilter(browser_id_);
    }

    LOG_INFO("SWDEBUG", ">>> NOT applying filter - MIME doesn't match: " + mime);
    return nullptr;
  }

 private:
  int browser_id_;
  IMPLEMENT_REFCOUNTING(ServiceWorkerResourceRequestHandler);
};

// Simple PNG encoder
namespace {
  void WritePNGChunk(std::vector<uint8_t>& output, const char* type, const uint8_t* data, size_t len) {
    // Length
    uint32_t length = static_cast<uint32_t>(len);
    output.push_back((length >> 24) & 0xFF);
    output.push_back((length >> 16) & 0xFF);
    output.push_back((length >> 8) & 0xFF);
    output.push_back(length & 0xFF);

    // Type
    output.insert(output.end(), type, type + 4);

    // Data
    if (data && len > 0) {
      output.insert(output.end(), data, data + len);
    }

    // CRC
    uint32_t crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const uint8_t*>(type), 4);
    if (data && len > 0) {
      crc = crc32(crc, data, len);
    }
    output.push_back((crc >> 24) & 0xFF);
    output.push_back((crc >> 16) & 0xFF);
    output.push_back((crc >> 8) & 0xFF);
    output.push_back(crc & 0xFF);
  }

  void EncodePNG(std::vector<uint8_t>& output, const uint8_t* bgra_data, int width, int height) {
    // PNG signature
    const uint8_t png_sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
    output.insert(output.end(), png_sig, png_sig + 8);

    // IHDR chunk
    uint8_t ihdr[13];
    ihdr[0] = (width >> 24) & 0xFF;
    ihdr[1] = (width >> 16) & 0xFF;
    ihdr[2] = (width >> 8) & 0xFF;
    ihdr[3] = width & 0xFF;
    ihdr[4] = (height >> 24) & 0xFF;
    ihdr[5] = (height >> 16) & 0xFF;
    ihdr[6] = (height >> 8) & 0xFF;
    ihdr[7] = height & 0xFF;
    ihdr[8] = 8;  // bit depth
    ihdr[9] = 6;  // color type (RGBA)
    ihdr[10] = 0; // compression
    ihdr[11] = 0; // filter
    ihdr[12] = 0; // interlace
    WritePNGChunk(output, "IHDR", ihdr, 13);

    // Convert BGRA to RGBA and add filter bytes
    std::vector<uint8_t> rgba_data;
    rgba_data.reserve(height * (1 + width * 4));

    for (int y = 0; y < height; y++) {
      rgba_data.push_back(0); // filter type (none)
      for (int x = 0; x < width; x++) {
        int idx = (y * width + x) * 4;
        rgba_data.push_back(bgra_data[idx + 2]); // R
        rgba_data.push_back(bgra_data[idx + 1]); // G
        rgba_data.push_back(bgra_data[idx + 0]); // B
        rgba_data.push_back(bgra_data[idx + 3]); // A
      }
    }

    // Compress with zlib
    uLongf compressed_size = compressBound(rgba_data.size());
    std::vector<uint8_t> compressed(compressed_size);

    int result = compress(compressed.data(), &compressed_size, rgba_data.data(), rgba_data.size());
    if (result == Z_OK) {
      compressed.resize(compressed_size);
      WritePNGChunk(output, "IDAT", compressed.data(), compressed.size());
    }

    // IEND chunk
    WritePNGChunk(output, "IEND", nullptr, 0);
  }

}

OwlClient::OwlClient()
  : screenshot_buffer_(nullptr), screenshot_ready_(false),
    screenshot_width_(0), screenshot_height_(0),
    use_crop_bounds_(false), crop_x_(0), crop_y_(0), crop_width_(0), crop_height_(0),
    cached_frame_width_(0), cached_frame_height_(0), frame_cache_frozen_(false),
    viewport_width_(1920), viewport_height_(1080),
    scan_complete_(false), text_extraction_complete_(false),
    verification_complete_(false), pick_complete_(false),
    video_recorder_(nullptr) {
  nav_info_.state = NavigationState::IDLE;
}

OwlClient::OwlClient(const ProxyConfig& proxy_config)
  : screenshot_buffer_(nullptr), screenshot_ready_(false),
    screenshot_width_(0), screenshot_height_(0),
    use_crop_bounds_(false), crop_x_(0), crop_y_(0), crop_width_(0), crop_height_(0),
    cached_frame_width_(0), cached_frame_height_(0), frame_cache_frozen_(false),
    viewport_width_(1920), viewport_height_(1080),
    scan_complete_(false), text_extraction_complete_(false),
    verification_complete_(false), pick_complete_(false),
    video_recorder_(nullptr),
    proxy_config_(proxy_config) {
  nav_info_.state = NavigationState::IDLE;
  if (proxy_config_.trust_custom_ca && !proxy_config_.ca_cert_path.empty()) {
    LOG_DEBUG("OwlClient", "Client initialized with custom CA certificate: " + proxy_config_.ca_cert_path);
  }
}

void OwlClient::SetProxyConfig(const ProxyConfig& config) {
  proxy_config_ = config;
  if (proxy_config_.trust_custom_ca && !proxy_config_.ca_cert_path.empty()) {
    LOG_DEBUG("OwlClient", "Proxy config updated with custom CA certificate: " + proxy_config_.ca_cert_path);
  }
}

void OwlClient::SetContextId(const std::string& context_id) {
  context_id_ = context_id;
  LOG_DEBUG("OwlClient", "Context ID set for circuit isolation: " + context_id);
}

void OwlClient::SetResourceBlocking(bool enabled) {
  resource_blocking_enabled_ = enabled;
  LOG_DEBUG("OwlClient", "Resource blocking " + std::string(enabled ? "enabled" : "disabled"));
}

void OwlClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  // Browser created successfully
}

void OwlClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  // Cleanup
}

void OwlClient::OnLoadStart(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            TransitionType transition_type) {
  CEF_REQUIRE_UI_THREAD();

  if (frame->IsMain()) {
    std::string current_url = frame->GetURL().ToString();

    // Ignore about:blank loads
    if (current_url == "about:blank" || current_url.empty()) {
      LOG_DEBUG("LoadHandler", "Ignoring about:blank load start");
      return;
    }

    // Unfreeze frame cache on new navigation (allows normal rendering)
    UnfreezeFrameCache();

    std::lock_guard<std::mutex> lock(nav_mutex_);
    nav_info_.state = NavigationState::LOADING;
    nav_info_.url = current_url;
    nav_info_.is_main_frame = true;
    nav_info_.start_time = std::chrono::steady_clock::now();

    std::ostringstream msg;
    msg << "Navigation started - URL: " << nav_info_.url << " State: LOADING";
    LOG_DEBUG("LoadHandler", msg.str());
  }
}

void OwlClient::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          int httpStatusCode) {
  CEF_REQUIRE_UI_THREAD();

  if (frame->IsMain()) {
    std::string current_url = frame->GetURL().ToString();

    // Ignore about:blank loads - wait for real URL
    if (current_url == "about:blank" || current_url.empty()) {
      LOG_DEBUG("LoadHandler", "Ignoring about:blank load");
      return;
    }

    // NOTE: webdriver spoofing is handled ONLY in owl_virtual_machine.cc
    // to avoid conflicts between multiple implementations that cause
    // "null conversion error", "toString error", and "too much recursion"
    // detection failures from fingerprint.com/creepjs.
    // DO NOT add webdriver spoofing here.

    // TIMEZONE SPOOFING: Inject timezone override if configured in proxy settings
    // This is critical for stealth when using proxies - browserscan detects timezone mismatch
    if (proxy_config_.enabled && proxy_config_.spoof_timezone &&
        !proxy_config_.timezone_override.empty()) {
      LOG_DEBUG("LoadHandler", "Injecting timezone spoofing: " + proxy_config_.timezone_override);
      OwlStealth::SpoofTimezone(frame, proxy_config_.timezone_override);
      LOG_DEBUG("LoadHandler", "Timezone spoofing injected for: " + proxy_config_.timezone_override);
    }

    std::lock_guard<std::mutex> lock(nav_mutex_);
    nav_info_.state = NavigationState::COMPLETE;
    nav_info_.url = current_url;
    nav_info_.http_status = httpStatusCode;
    nav_info_.end_time = std::chrono::steady_clock::now();

    // Initialize DOM mutation tracking (assume page will change after load)
    nav_info_.last_dom_mutation = std::chrono::steady_clock::now();
    nav_info_.last_network_activity = std::chrono::steady_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        nav_info_.end_time - nav_info_.start_time).count();

    std::ostringstream msg;
    msg << "Navigation complete - URL: " << nav_info_.url
        << " Status: " << httpStatusCode
        << " Duration: " << duration << "ms State: COMPLETE";
    LOG_DEBUG("LoadHandler", msg.str());

    // Trigger DOM element scan after page loads
    // Format browser ID as context_id (ctx_000001, ctx_000002, etc)
    std::ostringstream ctx_stream;
    ctx_stream << "ctx_" << std::setfill('0') << std::setw(6) << browser->GetIdentifier();
    std::string browser_context = ctx_stream.str();

    CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("scan_element");
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    args->SetString(0, browser_context);  // context_id
    args->SetString(1, "*");               // selector (scan all)

    frame->SendProcessMessage(PID_RENDERER, message);
    LOG_DEBUG("LoadHandler", "Triggered DOM scan for context: " + browser_context);
  }
}

void OwlClient::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                      bool isLoading,
                                      bool canGoBack,
                                      bool canGoForward) {
  CEF_REQUIRE_UI_THREAD();

  std::ostringstream msg;
  msg << "Loading state changed - isLoading: " << (isLoading ? "true" : "false")
      << " URL: " << browser->GetMainFrame()->GetURL().ToString();
  LOG_DEBUG("LoadHandler", msg.str());
}

void OwlClient::OnLoadError(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            ErrorCode errorCode,
                            const CefString& errorText,
                            const CefString& failedUrl) {
  CEF_REQUIRE_UI_THREAD();

  // Don't display an error for downloaded files
  if (errorCode == ERR_ABORTED) {
    return;
  }

  if (frame->IsMain()) {
    std::lock_guard<std::mutex> lock(nav_mutex_);
    nav_info_.state = NavigationState::FAILED;
    nav_info_.error_message = errorText.ToString();
    nav_info_.end_time = std::chrono::steady_clock::now();
  }

  std::ostringstream msg;
  msg << "Failed to load URL: " << failedUrl.ToString()
      << " Error code: " << errorCode
      << " Error: " << errorText.ToString() << " State: FAILED";
  LOG_ERROR("LoadHandler", msg.str());
}

// CefDisplayHandler method - capture console messages
bool OwlClient::OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                  cef_log_severity_t level,
                                  const CefString& message,
                                  const CefString& source,
                                  int line) {
  CEF_REQUIRE_UI_THREAD();

  // Log that we received a console message
  std::ostringstream log_msg;
  log_msg << "Received console message: [" << level << "] "
          << message.ToString() << " from " << source.ToString() << ":" << line;
  LOG_DEBUG("ConsoleMessage", log_msg.str());

  // Convert CEF log level to our console level
  std::string console_level;
  switch (level) {
    case LOGSEVERITY_DEBUG:
      console_level = "debug";
      break;
    case LOGSEVERITY_INFO:
      console_level = "info";
      break;
    case LOGSEVERITY_WARNING:
      console_level = "warn";
      break;
    case LOGSEVERITY_ERROR:
      console_level = "error";
      break;
    default:
      console_level = "log";
      break;
  }

  // Store in console logger for retrieval via IPC
  if (!context_id_.empty()) {
    OwlConsoleLogger::GetInstance()->LogMessage(
        context_id_,
        console_level,
        message.ToString(),
        source.ToString(),
        line
    );
  }

  // Forward to dev console (stub in helpers, real implementation in UI process)
  #ifdef OS_MACOS
    LOG_DEBUG("ConsoleMessage", "Forwarding to dev console as: " + console_level);
    OwlDevConsole_AddMessage(
        console_level.c_str(),
        message.ToString().c_str(),
        source.ToString().c_str(),
        line
    );
  #endif

  // Return false to allow default handling (console output to terminal)
  return false;
}

// CefDisplayHandler method - track page title changes
void OwlClient::OnTitleChange(CefRefPtr<CefBrowser> browser,
                               const CefString& title) {
  CEF_REQUIRE_UI_THREAD();

  std::string title_str = title.ToString();
  LOG_DEBUG("DisplayHandler", "Page title changed: " + title_str);

  // Update navigation info with new title
  std::lock_guard<std::mutex> lock(nav_mutex_);
  nav_info_.title = title_str;
}

CefRefPtr<CefResourceRequestHandler> OwlClient::GetResourceRequestHandler(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    bool is_navigation,
    bool is_download,
    const CefString& request_initiator,
    bool& disable_default_handling) {

  std::string url = request->GetURL().ToString();
  CefRequest::ResourceType early_res_type = request->GetResourceType();

  // VERBOSE: Log ALL .js requests with resource type to debug ServiceWorker interception
  if (url.find(".js") != std::string::npos) {
    LOG_INFO("SWDEBUG", ">>> JS REQUEST: " + url +
             " ResourceType=" + std::to_string(early_res_type) +
             " is_navigation=" + std::string(is_navigation ? "true" : "false") +
             " is_download=" + std::string(is_download ? "true" : "false"));

    // Log headers to detect Service-Worker header
    CefRequest::HeaderMap early_headers;
    request->GetHeaderMap(early_headers);
    for (const auto& h : early_headers) {
      if (h.first.ToString().find("Service") != std::string::npos ||
          h.first.ToString().find("Worker") != std::string::npos) {
        LOG_INFO("SWDEBUG", ">>> SW Header: " + h.first.ToString() + " = " + h.second.ToString());
      }
    }
  }

  LOG_DEBUG("RequestHandler", "GetResourceRequestHandler CALLED for URL: " + url);
  LOG_DEBUG("RequestHandler", "is_navigation: " + std::string(is_navigation ? "TRUE" : "FALSE"));

  // FORCE handling of owl:// URLs with validated inline handler
  // This bypasses CEF's routing issues while maintaining safety
  if (url.find("owl://") == 0) {
    LOG_DEBUG("RequestHandler", "Detected owl:// URL - creating validated handler");

    // Safety: Validate URL format before creating handler
    if (url.length() < 8 || url.find("..") != std::string::npos) {
      LOG_ERROR("RequestHandler", "Invalid or suspicious owl:// URL: " + url);
      return nullptr;
    }

    // Create inline handler with safety checks
    // Store browser_id for ResponseFilter
    int browser_id = browser ? browser->GetIdentifier() : 0;

    class SafeOlibHandler : public CefResourceRequestHandler {
     public:
      SafeOlibHandler(int browser_id) : browser_id_(browser_id) {}

      CefRefPtr<CefResourceHandler> GetResourceHandler(
          CefRefPtr<CefBrowser> browser,
          CefRefPtr<CefFrame> frame,
          CefRefPtr<CefRequest> request) override {

        if (!request) return nullptr;

        std::string url = request->GetURL().ToString();

        // Safety: Validate allowed file extensions
        const std::string allowed[] = {
          ".html", ".css", ".js", ".png", ".apng", ".jpg", ".jpeg", ".gif", ".svg", ".json", ".webp",
          ".mp4", ".webm", ".ogg"
        };

        bool allowed_ext = false;
        for (const auto& ext : allowed) {
          if (url.find(ext) != std::string::npos) {
            allowed_ext = true;
            break;
          }
        }

        if (!allowed_ext) {
          LOG_ERROR("SafeOlibHandler", "Disallowed file type: " + url);
          return nullptr;
        }

        LOG_DEBUG("SafeOlibHandler", "Creating resource handler for: " + url);
        return new OwlTestSchemeHandler();
      }

      // CRITICAL: Apply ResponseFilter for worker scripts to inject spoofing
      // IMPORTANT: Only apply for ACTUAL worker resource types, NOT RT_SCRIPT!
      // creep.js is loaded both as main frame script (RT_SCRIPT) and worker script (RT_WORKER)
      // Worker spoofing code uses WorkerNavigator which doesn't exist in main frame
      CefRefPtr<CefResponseFilter> GetResourceResponseFilter(
          CefRefPtr<CefBrowser> browser,
          CefRefPtr<CefFrame> frame,
          CefRefPtr<CefRequest> request,
          CefRefPtr<CefResponse> response) override {

        if (!request) return nullptr;

        std::string url = request->GetURL().ToString();
        CefRequest::ResourceType res_type = request->GetResourceType();

        // DEBUG: Log ALL .js requests to understand resource types
        if (url.find(".js") != std::string::npos || url.find("creep") != std::string::npos) {
          LOG_INFO("SafeOlibHandler", "GetResourceResponseFilter called: URL=" + url +
                   " ResourceType=" + std::to_string(res_type) +
                   " MIME=" + response->GetMimeType().ToString());
        }

        // ONLY apply filter for actual worker resource types
        // RT_WORKER (9) = dedicated worker, RT_SHARED_WORKER (10), RT_SERVICE_WORKER (15)
        // Do NOT use RT_SCRIPT - that would break main frame scripts!
        if (res_type == RT_WORKER || res_type == RT_SHARED_WORKER || res_type == RT_SERVICE_WORKER) {
          // Only filter JavaScript responses
          std::string mime = response->GetMimeType().ToString();
          if (mime.find("javascript") != std::string::npos ||
              mime.find("ecmascript") != std::string::npos ||
              url.find(".js") != std::string::npos) {
            LOG_INFO("SafeOlibHandler", "Applying worker spoof filter for ResourceType=" +
                     std::to_string(res_type) + ": " + url);
            return new ServiceWorkerResponseFilter(browser_id_);
          }
        }

        return nullptr;
      }

     private:
      int browser_id_;
      IMPLEMENT_REFCOUNTING(SafeOlibHandler);
    };

    return new SafeOlibHandler(browser_id);
  }

  // ========== HTTPS .owl TLD HANDLER ==========
  // Intercept https://*.owl requests and serve static content
  // This enables ServiceWorker testing with proper HTTPS secure context
  // URL mapping: https://lie-detector.owl/ -> statics/lie_detector/index.html
  if (url.find("https://") == 0 && url.find(".owl") != std::string::npos) {
    // Extract domain from URL (strip port if present)
    size_t protocol_end = 8;  // "https://"
    size_t domain_end = url.find('/', protocol_end);
    if (domain_end == std::string::npos) domain_end = url.length();

    std::string domain_with_port = url.substr(protocol_end, domain_end - protocol_end);

    // Strip port number if present (e.g., "lie-detector.owl:8443" -> "lie-detector.owl")
    size_t port_pos = domain_with_port.find(':');
    std::string domain = (port_pos != std::string::npos)
        ? domain_with_port.substr(0, port_pos)
        : domain_with_port;

    // Check if it's a .owl domain
    if (domain.length() > 4 && domain.substr(domain.length() - 4) == ".owl") {
      LOG_DEBUG("OwlTLD", "Detected .owl domain request: " + url);

      int browser_id = browser ? browser->GetIdentifier() : 0;

      // Create inline handler for .owl HTTPS requests
      class OwlHttpsHandler : public CefResourceRequestHandler {
       public:
        OwlHttpsHandler(int browser_id, const std::string& domain, const std::string& url)
            : browser_id_(browser_id), domain_(domain), original_url_(url) {}

        CefRefPtr<CefResourceHandler> GetResourceHandler(
            CefRefPtr<CefBrowser> browser,
            CefRefPtr<CefFrame> frame,
            CefRefPtr<CefRequest> request) override {

          if (!request) return nullptr;

          std::string url = request->GetURL().ToString();
          LOG_DEBUG("OwlHttpsHandler", "GetResourceHandler for: " + url);

          // Create a specialized resource handler for .owl domains
          // Pass browser_id for worker script patching (critical for SW spoofing)
          return new OwlHttpsResourceHandler(domain_, url, browser_id_);
        }

        // IMPORTANT: OwlHttpsResourceHandler::PrependWorkerPatches() already patches
        // worker scripts. Do NOT add a response filter here or the script gets
        // double-patched which causes JavaScript syntax errors.
        CefRefPtr<CefResponseFilter> GetResourceResponseFilter(
            CefRefPtr<CefBrowser> browser,
            CefRefPtr<CefFrame> frame,
            CefRefPtr<CefRequest> request,
            CefRefPtr<CefResponse> response) override {
          // No response filter needed - OwlHttpsResourceHandler already patches
          return nullptr;
        }

       private:
        int browser_id_;
        std::string domain_;
        std::string original_url_;
        IMPLEMENT_REFCOUNTING(OwlHttpsHandler);
      };

      return new OwlHttpsHandler(browser_id, domain, url);
    }
  }

  // ========== SERVICE WORKER SCRIPT INTERCEPTION ==========
  // Intercept Service Worker script requests to inject navigator spoofing
  // Service Worker scripts are identified by "Service-Worker: script" header
  // (RT_SERVICE_WORKER means requests FROM SW, not FOR SW scripts)
  CefRequest::HeaderMap headers;
  request->GetHeaderMap(headers);

  // SWDEBUG: Log creep.js requests with resource type and headers
  CefRequest::ResourceType res_type = request->GetResourceType();
  if (url.find("creep.js") != std::string::npos) {
    LOG_INFO("SWDEBUG", "creep.js request: " + url + " ResourceType=" + std::to_string(res_type) + " headers=" + std::to_string(headers.size()));
    for (const auto& h : headers) {
      LOG_INFO("SWDEBUG", "  Header: " + h.first.ToString() + " = " + h.second.ToString());
    }
  }

  // Detect potential Worker scripts by URL pattern OR resource type
  // CEF doesn't expose "Service-Worker: script" header, so we match by:
  // 1. RT_WORKER (9) - dedicated worker script request
  // 2. RT_SHARED_WORKER (10) - shared worker script request
  // 3. RT_SERVICE_WORKER (15) - service worker script request
  // 4. RT_SCRIPT (3) with worker-like URL patterns - for preloading/registration
  // Uses CefResponseFilter approach which doesn't have threading issues
  bool is_potential_sw = false;

  // RT_WORKER (9) = dedicated worker script fetch requests
  if (res_type == RT_WORKER) {
    is_potential_sw = true;
    LOG_INFO("SWDEBUG", "Detected RT_WORKER (DedicatedWorker script) request: " + url);
  }
  // RT_SHARED_WORKER (10) = shared worker script fetch requests
  else if (res_type == RT_SHARED_WORKER) {
    is_potential_sw = true;
    LOG_INFO("SWDEBUG", "Detected RT_SHARED_WORKER (SharedWorker script) request: " + url);
  }
  // RT_SERVICE_WORKER (15) = service worker script fetch requests
  else if (res_type == RT_SERVICE_WORKER) {
    is_potential_sw = true;
    LOG_INFO("SWDEBUG", "Detected RT_SERVICE_WORKER (ServiceWorker script) request: " + url);
  }
  // Also check RT_SCRIPT with worker-like URL patterns
  else if (res_type == RT_SCRIPT) {
    std::string lower_url = url;
    std::transform(lower_url.begin(), lower_url.end(), lower_url.begin(), ::tolower);
    // Match common worker script patterns
    if (lower_url.find("creep.js") != std::string::npos ||
        lower_url.find("/sw.js") != std::string::npos ||
        lower_url.find("service-worker") != std::string::npos ||
        lower_url.find("serviceworker") != std::string::npos ||
        lower_url.find("/worker.js") != std::string::npos) {
      is_potential_sw = true;
    }
  }

  // CRITICAL: Also check URL patterns for ANY resource type that could be a worker script
  // ServiceWorker script fetches might have unexpected resource types (RT_SUB_RESOURCE, etc.)
  if (!is_potential_sw && !is_navigation) {
    std::string lower_url = url;
    std::transform(lower_url.begin(), lower_url.end(), lower_url.begin(), ::tolower);
    if ((lower_url.find("creep.js") != std::string::npos ||
         lower_url.find("/sw.js") != std::string::npos ||
         lower_url.find("service-worker") != std::string::npos ||
         lower_url.find("serviceworker") != std::string::npos) &&
        lower_url.find(".js") != std::string::npos) {
      LOG_INFO("SWDEBUG", "URL pattern match for potential SW (res_type=" + std::to_string(res_type) + "): " + url);
      is_potential_sw = true;
    }
  }

  if (is_potential_sw) {
    int browser_id = browser ? browser->GetIdentifier() : 0;
    LOG_INFO("SWDEBUG", ">>> RETURNING ServiceWorkerResourceRequestHandler for: " + url + " browser_id=" + std::to_string(browser_id));
    return new ServiceWorkerResourceRequestHandler(browser_id);
  } else if (url.find(".js") != std::string::npos) {
    // Log why this .js file was NOT detected as potential SW
    LOG_INFO("SWDEBUG", ">>> NOT a potential SW: " + url +
             " res_type=" + std::to_string(res_type) +
             " is_navigation=" + std::string(is_navigation ? "true" : "false"));
  }

  // Phase 1: Determine resource type for smart filtering
  // Allow images and stylesheets to load (per user request)
  std::string resource_type = "unknown";

  if (is_navigation) {
    resource_type = "document";
  } else {
    // Determine type from URL extension or Content-Type (best effort)
    if (url.find(".jpg") != std::string::npos ||
        url.find(".jpeg") != std::string::npos ||
        url.find(".png") != std::string::npos ||
        url.find(".gif") != std::string::npos ||
        url.find(".webp") != std::string::npos ||
        url.find(".svg") != std::string::npos ||
        url.find(".ico") != std::string::npos ||
        url.find("/image") != std::string::npos) {
      resource_type = "image";
    } else if (url.find(".css") != std::string::npos ||
               url.find("stylesheet") != std::string::npos) {
      resource_type = "stylesheet";
    } else if (url.find(".woff") != std::string::npos ||
               url.find(".woff2") != std::string::npos ||
               url.find(".ttf") != std::string::npos ||
               url.find(".otf") != std::string::npos ||
               url.find(".eot") != std::string::npos) {
      resource_type = "font";
    } else if (url.find(".js") != std::string::npos) {
      resource_type = "script";
    } else if (url.find("/xhr") != std::string::npos ||
               url.find("/api/") != std::string::npos ||
               url.find("/collect") != std::string::npos ||
               url.find("/beacon") != std::string::npos) {
      resource_type = "xhr";
    } else {
      resource_type = "other";
    }
  }

  // Early exit: ALWAYS allow images and stylesheets (per user request)
  if (resource_type == "image" || resource_type == "stylesheet" || resource_type == "font") {
    return nullptr;  // Allow through without checking blocklist
  }

  if (is_navigation) {
    std::ostringstream msg;
    msg << "Resource request (navigation) - URL: " << url;
    LOG_DEBUG("RequestHandler", msg.str());
  }

  // Check if resource should be blocked (only non-image/stylesheet resources)
  // Only check blocker if resource blocking is enabled for this context
  if (resource_blocking_enabled_) {
    auto* blocker = OwlResourceBlocker::GetInstance();
    if (blocker->ShouldBlockRequest(url, resource_type)) {
      std::ostringstream msg;
      msg << "BLOCKED request - URL: " << url << " Type: " << resource_type;
      LOG_DEBUG("RequestHandler", msg.str());
      disable_default_handling = true;
      return nullptr;  // Block the request
    }
  }

  // Response body capture filter - captures response content for network logging
  // Only captures text-based content types up to a size limit (256KB)
  class NetworkLogResponseFilter : public CefResponseFilter {
   public:
    NetworkLogResponseFilter(const std::string& content_type)
        : content_type_(content_type), body_truncated_(false) {}

    bool InitFilter() override {
      return true;
    }

    FilterStatus Filter(void* data_in, size_t data_in_size, size_t& data_in_read,
                        void* data_out, size_t data_out_size, size_t& data_out_written) override {
      // Pass through all data unchanged while capturing it
      data_in_read = 0;
      data_out_written = 0;

      // Max body size: 256KB
      const size_t max_body_size = 256 * 1024;

      if (data_in_size > 0) {
        // Copy input to output (pass-through)
        size_t to_copy = std::min(data_in_size, data_out_size);
        memcpy(data_out, data_in, to_copy);
        data_in_read = to_copy;
        data_out_written = to_copy;

        // Capture body content up to limit
        if (captured_body_.size() < max_body_size) {
          size_t remaining = max_body_size - captured_body_.size();
          size_t capture_size = std::min(to_copy, remaining);
          captured_body_.append(static_cast<const char*>(data_in), capture_size);
          if (to_copy > remaining) {
            body_truncated_ = true;
          }
        }

        // Need more data if we couldn't copy everything
        if (data_in_read < data_in_size) {
          return RESPONSE_FILTER_NEED_MORE_DATA;
        }
      }

      return (data_in_size == 0) ? RESPONSE_FILTER_DONE : RESPONSE_FILTER_NEED_MORE_DATA;
    }

    const std::string& GetCapturedBody() const { return captured_body_; }
    bool IsBodyTruncated() const { return body_truncated_; }
    const std::string& GetContentType() const { return content_type_; }

    IMPLEMENT_REFCOUNTING(NetworkLogResponseFilter);

   private:
    std::string content_type_;
    std::string captured_body_;
    bool body_truncated_;
  };

  // Return a request handler to intercept and modify headers for ALL requests
  // CRITICAL: Remove Sec-CH-UA headers that leak real Chrome version
  class HeaderModifierHandler : public CefResourceRequestHandler {
   public:
    explicit HeaderModifierHandler(const std::string& context_id)
        : context_id_(context_id), start_time_(std::chrono::steady_clock::now()) {
      // Generate unique request ID for correlation
      static std::atomic<uint64_t> request_counter{0};
      request_id_ = "req_" + std::to_string(request_counter.fetch_add(1));
    }

    cef_return_value_t OnBeforeResourceLoad(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefRequest> request,
        CefRefPtr<CefCallback> callback) override {

      // Store request info for network tracking
      request_url_ = request->GetURL().ToString();
      request_method_ = request->GetMethod().ToString();
      request_type_ = request->GetResourceType();
      start_time_ = std::chrono::steady_clock::now();

      // Log request to network interceptor if logging is enabled
      OwlNetworkInterceptor* interceptor = OwlNetworkInterceptor::GetInstance();
      if (interceptor->IsLoggingEnabled(context_id_)) {
        CapturedRequest captured_req;
        captured_req.request_id = request_id_;
        captured_req.url = request_url_;
        captured_req.method = request_method_;
        captured_req.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Get resource type string
        switch (request_type_) {
          case RT_MAIN_FRAME: captured_req.resource_type = "document"; break;
          case RT_SUB_FRAME: captured_req.resource_type = "document"; break;
          case RT_STYLESHEET: captured_req.resource_type = "stylesheet"; break;
          case RT_SCRIPT: captured_req.resource_type = "script"; break;
          case RT_IMAGE: captured_req.resource_type = "image"; break;
          case RT_FONT_RESOURCE: captured_req.resource_type = "font"; break;
          case RT_SUB_RESOURCE: captured_req.resource_type = "fetch"; break;
          case RT_OBJECT: captured_req.resource_type = "object"; break;
          case RT_MEDIA: captured_req.resource_type = "media"; break;
          case RT_XHR: captured_req.resource_type = "xhr"; break;
          default: captured_req.resource_type = "other"; break;
        }

        // Capture headers
        CefRequest::HeaderMap headers;
        request->GetHeaderMap(headers);
        for (const auto& h : headers) {
          captured_req.headers[h.first.ToString()] = h.second.ToString();
        }

        // Capture POST data
        CefRefPtr<CefPostData> postData = request->GetPostData();
        if (postData) {
          CefPostData::ElementVector elements;
          postData->GetElements(elements);
          for (size_t i = 0; i < elements.size(); i++) {
            if (elements[i]->GetType() == PDE_TYPE_BYTES) {
              size_t size = elements[i]->GetBytesCount();
              if (size > 0 && size < 10000) {
                char* bytes = new char[size];
                elements[i]->GetBytes(size, bytes);
                captured_req.post_data = std::string(bytes, size);
                delete[] bytes;
              }
            }
          }
        }

        interceptor->LogRequest(context_id_, captured_req);
      }

      // Get header map
      CefRequest::HeaderMap headers;
      request->GetHeaderMap(headers);

      // Log User-Agent header for debugging
      std::string url = request->GetURL().ToString();
      bool is_browserscan = url.find("browserscan.net") != std::string::npos;

      for (const auto& header : headers) {
        std::string header_name = header.first.ToString();
        std::transform(header_name.begin(), header_name.end(), header_name.begin(), ::tolower);

        if (header_name == "user-agent" && is_browserscan) {
          LOG_DEBUG("HeaderModifier", "User-Agent header for " + url + " = " + header.second.ToString());
        }
      }

      // REPLACE Sec-CH-UA headers and User-Agent with VM profile values
      // Removing them is suspicious - real browsers always send these
      auto it = headers.begin();
      while (it != headers.end()) {
        std::string header_name = it->first.ToString();
        std::transform(header_name.begin(), header_name.end(), header_name.begin(), ::tolower);

        if (header_name.find("sec-ch-ua") == 0 || header_name == "user-agent") {
          LOG_DEBUG("HeaderModifier", "Removing header: " + it->first.ToString() + " = " + it->second.ToString());
          it = headers.erase(it);
        } else {
          ++it;
        }
      }

      // Add Client Hints headers from the VM profile for this browser context
      // Each context has its own randomized profile (Windows, Ubuntu, macOS)
      int browser_id = browser ? browser->GetIdentifier() : -1;
      const owl::VirtualMachine* vm = OwlStealth::GetContextVM(browser_id);

      if (vm) {
        // Use the VM's User-Agent and client hints
        headers.insert(std::make_pair("User-Agent", vm->browser.user_agent));
        headers.insert(std::make_pair("Sec-CH-UA", vm->client_hints.sec_ch_ua));
        headers.insert(std::make_pair("Sec-CH-UA-Mobile", vm->client_hints.sec_ch_ua_mobile));
        headers.insert(std::make_pair("Sec-CH-UA-Platform", vm->client_hints.sec_ch_ua_platform));

        if (is_browserscan) {
          LOG_DEBUG("HeaderModifier", "Using VM profile for browser_id=" + std::to_string(browser_id) +
                   " UA=" + vm->browser.user_agent.substr(0, 50) + "..." +
                   " platform=" + vm->client_hints.sec_ch_ua_platform);
        }
      } else {
        // CRITICAL FIX: Use vm_id from StealthConfig to get the SAME VM
        // DO NOT call SelectRandomVM - it may pick a DIFFERENT VM!
        auto& db = owl::VirtualMachineDB::Instance();
        StealthConfig config = OwlStealth::GetContextFingerprint(browser_id);
        const owl::VirtualMachine* fallback_vm = nullptr;

        if (!config.vm_id.empty()) {
          fallback_vm = db.GetVM(config.vm_id);
          if (fallback_vm) {
            LOG_INFO("HeaderModifier", "[VM_SYNC] Using vm_id=" + config.vm_id +
                     " from StealthConfig for browser_id=" + std::to_string(browser_id));
          } else {
            LOG_ERROR("HeaderModifier", "[VM_SYNC] GetVM failed for config.vm_id=" + config.vm_id);
          }
        } else {
          LOG_ERROR("HeaderModifier", "[VM_SYNC] No vm_id in StealthConfig for browser_id=" +
                    std::to_string(browser_id) + " - fingerprint not properly registered!");
        }

        if (fallback_vm) {
          headers.insert(std::make_pair("User-Agent", fallback_vm->browser.user_agent));
          headers.insert(std::make_pair("Sec-CH-UA", fallback_vm->client_hints.sec_ch_ua));
          headers.insert(std::make_pair("Sec-CH-UA-Mobile", fallback_vm->client_hints.sec_ch_ua_mobile));
          headers.insert(std::make_pair("Sec-CH-UA-Platform", fallback_vm->client_hints.sec_ch_ua_platform));

          if (is_browserscan) {
            LOG_DEBUG("HeaderModifier", "Using VM from config.vm_id for browser_id=" + std::to_string(browser_id) +
                     " vm=" + fallback_vm->id);
          }
        } else {
          // Last resort - use dynamic browser version from config (should rarely happen)
          auto& vm_db = owl::VirtualMachineDB::Instance();
          std::string version = vm_db.GetBrowserVersion();
          headers.insert(std::make_pair("User-Agent", vm_db.GetDefaultUserAgent()));
          headers.insert(std::make_pair("Sec-CH-UA", "\"Google Chrome\";v=\"" + version + "\", \"Chromium\";v=\"" + version + "\", \"Not_A Brand\";v=\"24\""));
          headers.insert(std::make_pair("Sec-CH-UA-Mobile", "?0"));
          headers.insert(std::make_pair("Sec-CH-UA-Platform", "\"Windows\""));

          if (is_browserscan) {
            LOG_WARN("HeaderModifier", "No VM profile for browser_id=" + std::to_string(browser_id) +
                     ", using last resort fallback (v" + version + ")");
          }
        }
      }

      // Add browser_id header for HttpsServer to use correct VM profile
      // This allows the embedded HTTPS server to patch scripts with the same VM
      bool is_owl_server = url.find(".owl:8443") != std::string::npos;
      LOG_INFO("HeaderModifier", "URL check: " + url + " is_owl_server=" + std::string(is_owl_server ? "YES" : "NO") + " browser_id=" + std::to_string(browser_id));
      if (is_owl_server && browser_id > 0) {
        headers.insert(std::make_pair("X-Owl-Browser-Id", std::to_string(browser_id)));
        LOG_INFO("HeaderModifier", "Added X-Owl-Browser-Id=" + std::to_string(browser_id) + " for " + url);
      }

      // Set modified headers back
      request->SetHeaderMap(headers);

      return RV_CONTINUE;
    }

    CefRefPtr<CefResponseFilter> GetResourceResponseFilter(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefRequest> request,
        CefRefPtr<CefResponse> response) override {

      // Only capture body if logging is enabled
      OwlNetworkInterceptor* interceptor = OwlNetworkInterceptor::GetInstance();
      if (!interceptor->IsLoggingEnabled(context_id_)) {
        return nullptr;
      }

      // Get content type to determine if we should capture body
      std::string content_type = response->GetMimeType().ToString();

      // Only capture text-based content types (not images, videos, binary files)
      bool should_capture = false;
      if (content_type.find("text/") == 0 ||
          content_type.find("application/json") != std::string::npos ||
          content_type.find("application/javascript") != std::string::npos ||
          content_type.find("application/xml") != std::string::npos ||
          content_type.find("application/xhtml") != std::string::npos ||
          content_type.find("+json") != std::string::npos ||
          content_type.find("+xml") != std::string::npos) {
        should_capture = true;
      }

      if (should_capture) {
        response_filter_ = new NetworkLogResponseFilter(content_type);
        return response_filter_;
      }

      return nullptr;
    }

    void OnResourceLoadComplete(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefRequest> request,
        CefRefPtr<CefResponse> response,
        URLRequestStatus status,
        int64_t received_content_length) override {

      // Calculate duration
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time_);

      // Get resource type as string
      std::string type;
      switch (request_type_) {
        case RT_MAIN_FRAME: type = "document"; break;
        case RT_SUB_FRAME: type = "document"; break;
        case RT_STYLESHEET: type = "stylesheet"; break;
        case RT_SCRIPT: type = "script"; break;
        case RT_IMAGE: type = "image"; break;
        case RT_FONT_RESOURCE: type = "font"; break;
        case RT_SUB_RESOURCE: type = "fetch"; break;
        case RT_OBJECT: type = "object"; break;
        case RT_MEDIA: type = "media"; break;
        case RT_XHR: type = "xhr"; break;
        default: type = "other"; break;
      }

      // Get status (used for dev console on macOS)
      int status_code = response ? response->GetStatus() : 0;
      std::string status_text = response ? response->GetStatusText().ToString() : "";
      (void)status_code;  // May be unused on Linux
      (void)status_text;  // May be unused on Linux

      // Extract request headers as JSON
      std::string request_headers_json = "{";
      CefRequest::HeaderMap req_headers;
      request->GetHeaderMap(req_headers);
      bool first = true;
      for (const auto& header : req_headers) {
        if (!first) request_headers_json += ",";
        request_headers_json += "\"" + header.first.ToString() + "\":\"" + header.second.ToString() + "\"";
        first = false;
      }
      request_headers_json += "}";

      // Extract response headers as JSON
      std::string response_headers_json = "{";
      if (response) {
        CefRequest::HeaderMap resp_headers;
        response->GetHeaderMap(resp_headers);
        first = true;
        for (const auto& header : resp_headers) {
          if (!first) response_headers_json += ",";
          response_headers_json += "\"" + header.first.ToString() + "\":\"" + header.second.ToString() + "\"";
          first = false;
        }
      }
      response_headers_json += "}";

      // Parse URL parameters
      std::string url_params = "";
      size_t query_pos = request_url_.find('?');
      if (query_pos != std::string::npos) {
        url_params = request_url_.substr(query_pos + 1);
      }

      // Get POST data if available
      std::string post_data = "";
      CefRefPtr<CefPostData> postData = request->GetPostData();
      if (postData) {
        CefPostData::ElementVector elements;
        postData->GetElements(elements);
        for (size_t i = 0; i < elements.size(); i++) {
          if (elements[i]->GetType() == PDE_TYPE_BYTES) {
            size_t size = elements[i]->GetBytesCount();
            if (size > 0 && size < 10000) {  // Limit to 10KB
              char* bytes = new char[size];
              elements[i]->GetBytes(size, bytes);
              post_data = std::string(bytes, size);
              delete[] bytes;
            }
          }
        }
      }

      // Forward to dev console (if in UI process)
#ifdef OS_MACOS
      // Only try to access dev console in UI process
      // The OwlDevConsole_AddNetworkRequest weak symbol will be available in UI process
      extern void OwlDevConsole_AddNetworkRequestExtended(
          const char* url, const char* method, const char* type,
          int status_code, const char* status_text, size_t size, int duration_ms,
          const char* request_headers, const char* response_headers,
          const char* url_params, const char* post_data);

      // Try the extended version first
      if ((void*)OwlDevConsole_AddNetworkRequestExtended != nullptr) {
        OwlDevConsole_AddNetworkRequestExtended(
            request_url_.c_str(),
            request_method_.c_str(),
            type.c_str(),
            status_code,
            status_text.c_str(),
            static_cast<size_t>(received_content_length),
            static_cast<int>(duration.count()),
            request_headers_json.c_str(),
            response_headers_json.c_str(),
            url_params.c_str(),
            post_data.c_str()
        );
      } else if (OwlDevConsole_AddNetworkRequest) {
        // Fallback to basic version
        OwlDevConsole_AddNetworkRequest(
            request_url_.c_str(),
            request_method_.c_str(),
            type.c_str(),
            status_code,
            status_text.c_str(),
            static_cast<size_t>(received_content_length),
            static_cast<int>(duration.count())
        );
      }
#endif

      // Log response to network interceptor if logging is enabled
      OwlNetworkInterceptor* interceptor = OwlNetworkInterceptor::GetInstance();
      if (interceptor->IsLoggingEnabled(context_id_)) {
        CapturedResponse captured_resp;
        captured_resp.request_id = request_id_;
        captured_resp.url = request_url_;
        captured_resp.status_code = status_code;
        captured_resp.response_length = received_content_length;
        captured_resp.duration_ms = static_cast<int64_t>(duration.count());

        // Capture response headers
        if (response) {
          CefRequest::HeaderMap resp_headers;
          response->GetHeaderMap(resp_headers);
          for (const auto& h : resp_headers) {
            captured_resp.headers[h.first.ToString()] = h.second.ToString();
          }
        }

        // Set error if request failed
        if (status != UR_SUCCESS) {
          captured_resp.error = "Request failed with status: " + std::to_string(static_cast<int>(status));
        }

        // Capture response body from filter if available
        if (response_filter_.get()) {
          captured_resp.response_body = response_filter_->GetCapturedBody();
          captured_resp.body_truncated = response_filter_->IsBodyTruncated();
          captured_resp.content_type = response_filter_->GetContentType();
        } else if (response) {
          // Even if no filter, capture content type from response
          captured_resp.content_type = response->GetMimeType().ToString();
        }

        interceptor->LogResponse(context_id_, captured_resp);
      }
    }

    IMPLEMENT_REFCOUNTING(HeaderModifierHandler);

   private:
    std::string context_id_;
    std::string request_id_;
    std::string request_url_;
    std::string request_method_;
    cef_resource_type_t request_type_;
    std::chrono::steady_clock::time_point start_time_;
    CefRefPtr<NetworkLogResponseFilter> response_filter_;
  };

  return new HeaderModifierHandler(context_id_);
}

bool OwlClient::OnCertificateError(
    CefRefPtr<CefBrowser> browser,
    cef_errorcode_t cert_error,
    const CefString& request_url,
    CefRefPtr<CefSSLInfo> ssl_info,
    CefRefPtr<CefCallback> callback) {

  std::string url = request_url.ToString();

  // ========== .OWL DOMAIN TRUST ==========
  // Trust self-signed certificates for .owl domains (embedded HTTPS server)
  // This enables ServiceWorker support on https://*.owl URLs
  if (url.find(".owl") != std::string::npos) {
    // Extract domain from URL
    size_t protocol_end = url.find("://");
    if (protocol_end != std::string::npos) {
      size_t domain_start = protocol_end + 3;
      size_t domain_end = url.find_first_of(":/", domain_start);
      std::string domain = url.substr(domain_start, domain_end - domain_start);

      // Check if it's a .owl domain
      if (domain.length() > 4 && domain.substr(domain.length() - 4) == ".owl") {
        LOG_DEBUG("CertHandler", "Trusting .owl domain certificate for: " + url);
        callback->Continue();
        return true;  // We handled it - certificate accepted
      }
    }
  }

  // Check if we should trust custom CA certificates for SSL interception proxies
  // Two modes:
  // 1. trust_custom_ca with ca_cert_path: Trust specific CA certificate
  // 2. trust_custom_ca without ca_cert_path: Trust ALL certificates (for remote proxies)
  if (proxy_config_.trust_custom_ca) {
    LOG_DEBUG("CertHandler", "Certificate error for URL: " + url +
             ", error code: " + std::to_string(cert_error));

    // For SSL interception proxies (Charles, mitmproxy, remote proxies, etc.)
    // Common cert errors when using interception proxies:
    // - ERR_CERT_AUTHORITY_INVALID (the CA is not in system trust store)
    // - ERR_CERT_COMMON_NAME_INVALID (proxy re-signs with different CN)

    // Accept certificate errors when trust_custom_ca is enabled
    // If ca_cert_path is provided, we're trusting a specific CA
    // If ca_cert_path is empty, we're trusting ALL certificates (remote proxy mode)
    if (cert_error == ERR_CERT_AUTHORITY_INVALID ||
        cert_error == ERR_CERT_COMMON_NAME_INVALID ||
        cert_error == ERR_CERT_DATE_INVALID ||
        cert_error == ERR_CERT_NAME_CONSTRAINT_VIOLATION) {

      std::string trust_mode = proxy_config_.ca_cert_path.empty()
          ? "trust ALL certificates (remote proxy mode)"
          : "custom CA: " + proxy_config_.ca_cert_path;
      LOG_WARN("CertHandler", "Accepting certificate for " + url + " (" + trust_mode + ")");

      // Continue with the request (accept the certificate)
      callback->Continue();
      return true;  // We handled it
    }
  }

  // Log the error for debugging
  LOG_WARN("CertHandler", "Certificate error not handled for URL: " + url +
           ", error code: " + std::to_string(cert_error) +
           ", trust_custom_ca: " + std::string(proxy_config_.trust_custom_ca ? "true" : "false"));

  // Return false to use default behavior (show error / reject)
  return false;
}

bool OwlClient::GetAuthCredentials(
    CefRefPtr<CefBrowser> browser,
    const CefString& origin_url,
    bool isProxy,
    const CefString& host,
    int port,
    const CefString& realm,
    const CefString& scheme,
    CefRefPtr<CefAuthCallback> callback) {

  // Only handle proxy authentication
  if (!isProxy) {
    return false;  // Let CEF handle web server auth
  }

  std::string proxy_host = host.ToString();
  LOG_DEBUG("ProxyAuth", "Proxy authentication requested for " + proxy_host + ":" + std::to_string(port));

  // Check if this is a Tor proxy (localhost:9050 or 127.0.0.1:9050)
  bool is_tor_proxy = (port == 9050 || port == 9150) &&
                      (proxy_host == "127.0.0.1" || proxy_host == "localhost" || proxy_host == "::1");

  // Check if proxy type is SOCKS5H (typically used for Tor)
  bool is_socks5h = (proxy_config_.type == ProxyType::SOCKS5H);

  if (is_tor_proxy || is_socks5h) {
    // TOR CIRCUIT ISOLATION
    // Tor uses SOCKS5 authentication to isolate circuits
    // Different username:password combinations create separate circuits (different exit nodes)
    // This is the "IsolateSOCKSAuth" feature in Tor

    // Generate unique credentials for this context
    // Format: owl_ctx_<context_id>_<counter>:<random_password>
    // The random part ensures each new connection attempt gets a fresh circuit

    std::string username;
    std::string password;

    if (!context_id_.empty()) {
      // Use context ID for consistent circuit per context
      username = "owl_" + context_id_;
    } else {
      // Generate unique username if no context ID
      uint64_t counter = circuit_counter_.fetch_add(1);
      username = "owl_circuit_" + std::to_string(counter);
    }

    // Generate random password component to ensure circuit freshness
    // Each auth attempt with different credentials = new circuit
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint64_t> dis(0, UINT64_MAX);
    password = std::to_string(dis(gen));

    LOG_DEBUG("ProxyAuth", "Tor circuit isolation - using credentials: " + username + ":****");
    LOG_DEBUG("ProxyAuth", "Different credentials = different Tor exit node");

    callback->Continue(username, password);
    return true;
  }

  // For non-Tor proxies, use configured credentials if available
  if (!proxy_config_.username.empty()) {
    LOG_DEBUG("ProxyAuth", "Using configured proxy credentials for " + proxy_host);
    callback->Continue(proxy_config_.username, proxy_config_.password);
    return true;
  }

  // No credentials available
  LOG_WARN("ProxyAuth", "No proxy credentials available for " + proxy_host);
  return false;  // Let CEF handle (will likely show auth dialog or fail)
}

bool OwlClient::OnRequestMediaAccessPermission(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    const CefString& requesting_origin,
    uint32_t requested_permissions,
    CefRefPtr<CefMediaAccessCallback> callback) {
  CEF_REQUIRE_UI_THREAD();

  std::string origin = requesting_origin.ToString();

  // Log the permission request
  LOG_DEBUG("PermissionHandler", "Media access permission requested from: " + origin +
           ", permissions: " + std::to_string(requested_permissions));

  // Check what permissions are being requested
  bool wants_audio = (requested_permissions & CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE) != 0;
  bool wants_video = (requested_permissions & CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE) != 0;

  if (wants_audio) {
    LOG_DEBUG("PermissionHandler", "  - Audio capture requested");
  }
  if (wants_video) {
    LOG_DEBUG("PermissionHandler", "  - Video capture requested (virtual camera will be used)");
  }

  // Auto-grant all media permissions for virtual camera support
  // For getUserMedia requests, we must grant exactly what was requested
  callback->Continue(requested_permissions);

  LOG_DEBUG("PermissionHandler", "Media access permission GRANTED for virtual camera");

  return true;  // We handled the request
}

void OwlClient::SetViewport(int width, int height) {
  viewport_width_ = width;
  viewport_height_ = height;
  LOG_DEBUG("OwlClient", "Viewport set to " + std::to_string(width) + "x" + std::to_string(height));
}

void OwlClient::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) {
  // Off-screen rendering viewport - dynamic sizing for AI
  // Common presets: 1920x1080 (desktop), 768x1024 (tablet), 375x667 (mobile)
  rect.x = 0;
  rect.y = 0;
  rect.width = viewport_width_;
  rect.height = viewport_height_;
}

void OwlClient::OnPaint(CefRefPtr<CefBrowser> browser,
                        PaintElementType type,
                        const RectList& dirtyRects,
                        const void* buffer,
                        int width,
                        int height) {

  if (type == PET_VIEW) {
    // Cache the current frame so we can crop from it later without re-rendering
    // But only if cache is not frozen (prevents scroll-triggered paints from changing it)
    {
      std::lock_guard<std::mutex> lock(frame_cache_mutex_);
      if (!frame_cache_frozen_) {
        const uint8_t* src = static_cast<const uint8_t*>(buffer);
        size_t frame_size = width * height * 4;  // BGRA format
        cached_frame_.resize(frame_size);
        std::memcpy(cached_frame_.data(), src, frame_size);
        cached_frame_width_ = width;
        cached_frame_height_ = height;
      }
    }
    // Handle screenshot capture
    if (screenshot_buffer_ != nullptr) {
      int final_width = width;
      int final_height = height;
      const uint8_t* src = static_cast<const uint8_t*>(buffer);

      if (use_crop_bounds_) {
        // Crop to element bounds for faster processing and fewer tokens
        // Validate and clamp crop bounds to image dimensions
        int x = std::max(0, std::min(crop_x_, width - 1));
        int y = std::max(0, std::min(crop_y_, height - 1));
        int w = std::max(1, std::min(crop_width_, width - x));
        int h = std::max(1, std::min(crop_height_, height - y));

        // Create cropped BGRA buffer
        std::vector<uint8_t> cropped_bgra(w * h * 4);

        // Copy row by row from the source image
        for (int row = 0; row < h; ++row) {
          const uint8_t* src_row = src + ((y + row) * width + x) * 4;
          uint8_t* dst_row = cropped_bgra.data() + (row * w * 4);
          std::memcpy(dst_row, src_row, w * 4);
        }

        // Encode cropped image
        screenshot_buffer_->clear();
        EncodePNG(*screenshot_buffer_, cropped_bgra.data(), w, h);

        final_width = w;
        final_height = h;
      } else {
        // Full screenshot - no cropping

        // Copy BGRA data directly (no downscaling for better quality)
        std::vector<uint8_t> bgra_data(width * height * 4);
        std::memcpy(bgra_data.data(), src, width * height * 4);

        // Encode as PNG at full resolution
        screenshot_buffer_->clear();
        EncodePNG(*screenshot_buffer_, bgra_data.data(), width, height);
      }

      screenshot_width_ = final_width;
      screenshot_height_ = final_height;
      screenshot_ready_ = true;
    }

    // Handle video recording - feed frames to video recorder
    if (video_recorder_ != nullptr && video_recorder_->IsRecording()) {
      video_recorder_->AddFrame(buffer, width, height);
    }

    // Handle live streaming - feed frames to live streamer if active
    // Use browser identifier to construct context_id (matches OwlBrowserManager format)
    if (browser) {
      std::ostringstream ctx_stream;
      ctx_stream << "ctx_" << std::setfill('0') << std::setw(6) << browser->GetIdentifier();
      std::string context_id = ctx_stream.str();

      auto* streamer = owl::LiveStreamer::GetInstance();
      if (streamer->IsStreaming(context_id)) {
        streamer->AddFrame(context_id, buffer, width, height);
      }
    }
  }
}

bool OwlClient::GetScreenInfo(CefRefPtr<CefBrowser> browser, CefScreenInfo& screen_info) {
  // Use spoofed DPR (1.0) for consistent rendering across all platforms
  // This ensures the viewport renders at 1x scale matching our 1920x1080 profiles
  // Previously used GetActualSystemDPR() which caused 2x scaling on Retina Macs,
  // resulting in web pages being cut off (rendered at device pixels instead of CSS pixels)
  screen_info.device_scale_factor = 1.0f;
  screen_info.depth = 24;
  screen_info.depth_per_component = 8;
  screen_info.is_monochrome = false;

  screen_info.rect.x = 0;
  screen_info.rect.y = 0;
  screen_info.rect.width = viewport_width_;
  screen_info.rect.height = viewport_height_;

  screen_info.available_rect = screen_info.rect;

  LOG_DEBUG("OwlClient", "GetScreenInfo: DPR=" + std::to_string(screen_info.device_scale_factor) +
            " viewport=" + std::to_string(viewport_width_) + "x" + std::to_string(viewport_height_));

  return true;
}

void OwlClient::SetScreenshotBuffer(std::vector<uint8_t>* buffer) {
  screenshot_buffer_ = buffer;
  screenshot_ready_ = false;
}

void OwlClient::ResetScreenshot() {
  screenshot_buffer_ = nullptr;
  screenshot_ready_ = false;
  use_crop_bounds_ = false;  // Clear crop bounds on reset
}

void OwlClient::SetScreenshotCropBounds(int x, int y, int width, int height) {
  use_crop_bounds_ = true;
  crop_x_ = x;
  crop_y_ = y;
  crop_width_ = width;
  crop_height_ = height;
}

void OwlClient::ClearScreenshotCropBounds() {
  use_crop_bounds_ = false;
}

void OwlClient::FreezeFrameCache() {
  std::lock_guard<std::mutex> lock(frame_cache_mutex_);
  frame_cache_frozen_ = true;
}

void OwlClient::UnfreezeFrameCache() {
  std::lock_guard<std::mutex> lock(frame_cache_mutex_);
  frame_cache_frozen_ = false;
}

void OwlClient::GetCachedFrameDimensions(int& width, int& height) const {
  std::lock_guard<std::mutex> lock(frame_cache_mutex_);
  width = cached_frame_width_;
  height = cached_frame_height_;
}

bool OwlClient::GetCroppedScreenshotFromCache(std::vector<uint8_t>* out_buffer,
                                               int x, int y, int width, int height) {
  if (!out_buffer) {
    return false;
  }

  std::lock_guard<std::mutex> lock(frame_cache_mutex_);

  // Check if we have a cached frame
  if (cached_frame_.empty() || cached_frame_width_ == 0 || cached_frame_height_ == 0) {
    return false;
  }

  // Validate and clamp crop bounds to cached frame dimensions
  int clamped_x = std::max(0, std::min(x, cached_frame_width_ - 1));
  int clamped_y = std::max(0, std::min(y, cached_frame_height_ - 1));
  int clamped_w = std::max(1, std::min(width, cached_frame_width_ - clamped_x));
  int clamped_h = std::max(1, std::min(height, cached_frame_height_ - clamped_y));

  // Create cropped BGRA buffer
  std::vector<uint8_t> cropped_bgra(clamped_w * clamped_h * 4);

  // Copy row by row from cached frame
  for (int row = 0; row < clamped_h; ++row) {
    const uint8_t* src_row = cached_frame_.data() + ((clamped_y + row) * cached_frame_width_ + clamped_x) * 4;
    uint8_t* dst_row = cropped_bgra.data() + (row * clamped_w * 4);
    std::memcpy(dst_row, src_row, clamped_w * 4);
  }

  // Encode to PNG
  out_buffer->clear();
  EncodePNG(*out_buffer, cropped_bgra.data(), clamped_w, clamped_h);

  return true;
}

bool OwlClient::GetCroppedBGRAFromCache(std::vector<uint8_t>* out_buffer,
                                          int x, int y, int width, int height) {
  if (!out_buffer) {
    return false;
  }

  std::lock_guard<std::mutex> lock(frame_cache_mutex_);

  // Check if we have a cached frame
  if (cached_frame_.empty() || cached_frame_width_ == 0 || cached_frame_height_ == 0) {
    LOG_ERROR("OwlClient", "GetCroppedBGRAFromCache: Frame cache is empty");
    return false;
  }

  // Validate and clamp crop bounds
  int clamped_x = std::max(0, std::min(x, cached_frame_width_ - 1));
  int clamped_y = std::max(0, std::min(y, cached_frame_height_ - 1));
  int clamped_w = std::max(1, std::min(width, cached_frame_width_ - clamped_x));
  int clamped_h = std::max(1, std::min(height, cached_frame_height_ - clamped_y));

  // Create cropped BGRA buffer
  out_buffer->resize(clamped_w * clamped_h * 4);

  // Copy row by row from cached frame
  for (int row = 0; row < clamped_h; ++row) {
    const uint8_t* src_row = cached_frame_.data() + ((clamped_y + row) * cached_frame_width_ + clamped_x) * 4;
    uint8_t* dst_row = out_buffer->data() + (row * clamped_w * 4);
    std::memcpy(dst_row, src_row, clamped_w * 4);
  }

  return true;
}

void OwlClient::EncodePNGFromBGRA(std::vector<uint8_t>& output, const uint8_t* bgra_data, int width, int height) {
  EncodePNG(output, bgra_data, width, height);
}

// Navigation state management
NavigationInfo OwlClient::GetNavigationInfo() const {
  std::lock_guard<std::mutex> lock(nav_mutex_);
  return nav_info_;
}

bool OwlClient::IsNavigationComplete() const {
  std::lock_guard<std::mutex> lock(nav_mutex_);
  return nav_info_.state == NavigationState::COMPLETE ||
         nav_info_.state == NavigationState::FAILED;
}

bool OwlClient::IsNavigationInProgress() const {
  std::lock_guard<std::mutex> lock(nav_mutex_);
  return nav_info_.state == NavigationState::STARTING ||
         nav_info_.state == NavigationState::LOADING ||
         nav_info_.state == NavigationState::DOM_LOADED;
}

void OwlClient::WaitForNavigation(int timeout_ms) {
  LOG_DEBUG("OwlClient", "WaitForNavigation START");

  // Check if navigation already complete (race condition fix for fast pages like about:blank)
  if (IsNavigationComplete()) {
    LOG_DEBUG("OwlClient", "Navigation already complete before wait started");
    return;
  }

  auto start = std::chrono::steady_clock::now();
  while (!IsNavigationComplete()) {
    OwlBrowserManager::PumpMessageLoopIfNeeded();
    // PERFORMANCE: Reduced sleep from 10ms to 2ms for faster response
    // This significantly improves throughput when many navigations are queued
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    if (elapsed >= timeout_ms) {
      LOG_WARN("OwlClient", "WaitForNavigation timeout reached");
      break;
    }
  }
  LOG_DEBUG("OwlClient", "WaitForNavigation END");
}

bool OwlClient::CheckNavigationComplete() {
  // Non-blocking check - does NOT pump CEF, just checks state
  // Used for parallel waiting where main loop pumps CEF
  return IsNavigationComplete();
}

void OwlClient::ResetNavigation() {
  std::lock_guard<std::mutex> lock(nav_mutex_);
  nav_info_.state = NavigationState::IDLE;
  nav_info_.url.clear();
  nav_info_.error_message.clear();
  nav_info_.http_status = 0;
  nav_info_.pending_requests = 0;
  nav_info_.dom_mutation_count = 0;
  LOG_DEBUG("OwlClient", "Navigation state reset");
}

void OwlClient::WaitForStable(int timeout_ms) {
  LOG_DEBUG("OwlClient", "WaitForStable START (waiting for dynamic content to render)");

  // Simple approach: wait a fixed time for JavaScript to execute
  // This gives dynamic sites like Google time to render their content
  auto wait_time = std::min(timeout_ms, 2000);  // Max 2 seconds

  auto start = std::chrono::steady_clock::now();
  while (true) {
    OwlBrowserManager::PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    if (elapsed >= wait_time) {
      std::lock_guard<std::mutex> lock(nav_mutex_);
      nav_info_.state = NavigationState::STABLE;
      LOG_DEBUG("OwlClient", "Page considered STABLE after " + std::to_string(elapsed) + "ms");
      break;
    }
  }

  LOG_DEBUG("OwlClient", "WaitForStable END");
}

void OwlClient::WaitForNetworkIdle(int timeout_ms) {
  LOG_DEBUG("OwlClient", "WaitForNetworkIdle START");
  auto start = std::chrono::steady_clock::now();

  while (true) {
    OwlBrowserManager::PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Check if network has been idle for 500ms
    {
      std::lock_guard<std::mutex> lock(nav_mutex_);
      auto now = std::chrono::steady_clock::now();
      auto since_network = std::chrono::duration_cast<std::chrono::milliseconds>(
          now - nav_info_.last_network_activity).count();

      if (nav_info_.pending_requests == 0 && since_network >= 500) {
        nav_info_.state = NavigationState::NETWORK_IDLE;
        LOG_DEBUG("OwlClient", "Network is IDLE (no requests for " + std::to_string(since_network) + "ms)");
        break;
      }
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    if (elapsed >= timeout_ms) {
      LOG_WARN("OwlClient", "WaitForNetworkIdle timeout reached");
      break;
    }
  }
  LOG_DEBUG("OwlClient", "WaitForNetworkIdle END");
}

void OwlClient::NotifyRequestStarted() {
  std::lock_guard<std::mutex> lock(nav_mutex_);
  nav_info_.pending_requests++;
  nav_info_.last_network_activity = std::chrono::steady_clock::now();
}

void OwlClient::NotifyRequestCompleted() {
  std::lock_guard<std::mutex> lock(nav_mutex_);
  if (nav_info_.pending_requests > 0) {
    nav_info_.pending_requests--;
  }
  nav_info_.last_network_activity = std::chrono::steady_clock::now();
}

void OwlClient::NotifyDOMMutation() {
  std::lock_guard<std::mutex> lock(nav_mutex_);
  nav_info_.dom_mutation_count++;
  nav_info_.last_dom_mutation = std::chrono::steady_clock::now();
  LOG_DEBUG("OwlClient", "DOM mutation detected (total: " + std::to_string(nav_info_.dom_mutation_count) + ")");
}

void OwlClient::NotifyElementScanComplete(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(scan_mutex_);
  last_scan_context_ = context_id;
  scan_complete_ = true;
}

bool OwlClient::WaitForElementScan(CefRefPtr<CefBrowser> browser, const std::string& context_id, int timeout_ms) {
  LOG_DEBUG("OwlClient", "WaitForElementScan START for context: " + context_id);

  // CRITICAL: Reset scan_complete flag BEFORE sending scan message
  // This ensures we wait for the NEW scan, not an old one
  {
    std::lock_guard<std::mutex> lock(scan_mutex_);
    scan_complete_ = false;
    last_scan_context_ = "";
  }

  // CRITICAL: Trigger the scan by sending message to renderer process
  CefRefPtr<CefProcessMessage> scan_msg = CefProcessMessage::Create("scan_element");
  CefRefPtr<CefListValue> scan_args = scan_msg->GetArgumentList();
  scan_args->SetString(0, context_id);
  scan_args->SetString(1, "");  // Empty selector = scan all elements
  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, scan_msg);
  LOG_DEBUG("OwlClient", "Sent scan_element message to renderer");

  auto start = std::chrono::steady_clock::now();
  while (std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start).count() < timeout_ms) {

    {
      std::lock_guard<std::mutex> lock(scan_mutex_);
      if (scan_complete_ && last_scan_context_ == context_id) {
        LOG_DEBUG("OwlClient", "WaitForElementScan END - scan complete");
        return true;
      }
    }

    OwlBrowserManager::PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  LOG_WARN("OwlClient", "WaitForElementScan END - timeout reached");
  return false;
}

bool OwlClient::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {

  const std::string& message_name = message->GetName();

  // CEF_REQUIRE_UI_THREAD() can cause issues - messages from renderer arrive on browser UI thread
  LOG_DEBUG("MessageHandler", "OnProcessMessageReceived: " + message_name);

  // Handle renderer->browser messages
  if (source_process == PID_RENDERER) {
    if (message_name == "dom_mutation") {
      // DOM changed - update mutation timestamp
      NotifyDOMMutation();
      return true;
    }

    if (message_name == "element_scan_result") {
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string context_id = args->GetString(0).ToString();
      int count = args->GetInt(1);

      LOG_DEBUG("MessageHandler", "Received scan result: " + std::to_string(count) + " elements for " + context_id);

      OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
      OwlSemanticMatcher* semantic = OwlSemanticMatcher::GetInstance();

      // Clear previous data for this context (CRITICAL: prevents stale positions)
      tracker->ClearContext(context_id);
      semantic->ClearContext(context_id);

      for (int i = 0; i < count; i++) {
        CefRefPtr<CefDictionaryValue> dict = args->GetDictionary(i + 2);
        ElementRenderInfo info;
        info.selector = dict->GetString("selector").ToString();
        info.tag = dict->GetString("tag").ToString();
        info.id = dict->GetString("id").ToString();
        info.className = dict->GetString("className").ToString();
        info.x = dict->GetInt("x");
        info.y = dict->GetInt("y");
        info.width = dict->GetInt("width");
        info.height = dict->GetInt("height");
        info.visible = dict->GetBool("visible");
        info.text = dict->GetString("text").ToString();
        info.aria_label = dict->GetString("aria_label").ToString();

        // Enhanced attributes
        info.role = dict->GetString("role").ToString();
        info.placeholder = dict->GetString("placeholder").ToString();
        info.value = dict->GetString("value").ToString();
        info.type = dict->GetString("type").ToString();
        info.title = dict->GetString("title").ToString();
        info.name = dict->GetString("name").ToString();
        info.nearby_text = dict->GetString("nearby_text").ToString();
        info.label_for = dict->GetString("label_for").ToString();
        info.alt = dict->GetString("alt").ToString();
        info.src = dict->GetString("src").ToString();

        // Enhanced position/visibility info
        info.z_index = dict->GetInt("z_index");
        info.transform = dict->GetString("transform").ToString();
        info.opacity = static_cast<float>(dict->GetDouble("opacity"));
        info.display = dict->GetString("display").ToString();
        info.visibility_css = dict->GetString("visibility_css").ToString();

        tracker->RegisterElement(context_id, info);

        // Also register with semantic matcher
        ElementSemantics sem;
        sem.selector = info.selector;
        sem.tag = info.tag;
        sem.type = info.type;
        sem.id = info.id;
        sem.text = info.text;
        sem.placeholder = info.placeholder;
        sem.title = info.title;
        sem.aria_label = info.aria_label;
        sem.name = info.name;
        sem.value = info.value;
        sem.nearby_text = info.nearby_text;
        sem.label_for = info.label_for;
        sem.x = info.x;
        sem.y = info.y;
        sem.width = info.width;
        sem.height = info.height;
        sem.visible = info.visible;
        // Enhanced visibility fields (from improved scanner)
        sem.z_index = info.z_index;
        sem.opacity = info.opacity;
        sem.display = info.display;
        sem.visibility_css = info.visibility_css;
        sem.transform = info.transform;
        semantic->RegisterElement(context_id, sem);

        if (i < 3) {
          LOG_DEBUG("MessageHandler", "  Registered: " + info.tag +
                    (info.id.empty() ? "" : "#" + info.id) + " text='" + info.text.substr(0, 20) + "'");
        }
      }

      LOG_DEBUG("MessageHandler", "All elements registered (tracker + semantic)");

      // Notify that scan is complete
      NotifyElementScanComplete(context_id);

      return true;
    }

    if (message_name == "evaluate_result") {
      // Received JavaScript evaluation result from renderer
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string context_id = args->GetString(0).ToString();
      std::string result = args->GetString(1).ToString();

      LOG_DEBUG("MessageHandler", "Received eval result for context " + context_id + ": " + result.substr(0, 100));

      // Store the result for retrieval by OwlBrowserManager::Evaluate
      // (SetEvalResult is defined in owl_app.cc)
      extern void SetEvalResult(const std::string& context_id, const std::string& result);
      SetEvalResult(context_id, result);
      return true;
    }

    // Handle text extraction response from renderer
    if (message_name == "extract_text_response") {
      CefRefPtr<CefListValue> response_args = message->GetArgumentList();
      std::string context_id = response_args->GetString(0).ToString();
      std::string extracted_text = response_args->GetString(1).ToString();

      LOG_DEBUG("MessageHandler", "Received extracted text for context " + context_id + ": " + std::to_string(extracted_text.length()) + " chars");

      SetExtractedText(context_id, extracted_text);
      return true;
    }

    if (message_name == "extract_page_text_response") {
      CefRefPtr<CefListValue> response_args = message->GetArgumentList();
      std::string context_id = response_args->GetString(0).ToString();
      std::string page_text = response_args->GetString(1).ToString();

      LOG_DEBUG("MessageHandler", "Received page text for context " + context_id + ": " + std::to_string(page_text.length()) + " chars");

      SetExtractedText(context_id, page_text);
      return true;
    }

    if (message_name == "extract_page_html_response") {
      CefRefPtr<CefListValue> response_args = message->GetArgumentList();
      std::string context_id = response_args->GetString(0).ToString();
      std::string page_html = response_args->GetString(1).ToString();

      LOG_DEBUG("MessageHandler", "Received page HTML for context " + context_id + ": " + std::to_string(page_html.length()) + " chars");

      SetExtractedText(context_id, page_html);
      return true;
    }

    // Handle verification responses from renderer
    if (message_name == "verify_input_value_response") {
      CefRefPtr<CefListValue> response_args = message->GetArgumentList();
      std::string context_id = response_args->GetString(0).ToString();
      bool success = response_args->GetBool(1);
      std::string actual_value = response_args->GetString(2).ToString();
      std::string expected_value = response_args->GetString(3).ToString();
      std::string element_tag = response_args->GetString(4).ToString();
      std::string error_message = response_args->GetString(5).ToString();

      LOG_DEBUG("MessageHandler", "Received verify_input_value_response for context " + context_id +
               " success=" + (success ? "true" : "false") +
               " actual='" + actual_value.substr(0, 30) + "'" +
               " expected='" + expected_value.substr(0, 30) + "'");

      VerificationResult result;
      result.success = success;
      result.actual_value = actual_value;
      result.expected_value = expected_value;
      result.element_tag = element_tag;
      result.error_message = error_message;
      SetVerificationResult(context_id, result);
      return true;
    }

    if (message_name == "verify_focus_response") {
      CefRefPtr<CefListValue> response_args = message->GetArgumentList();
      std::string context_id = response_args->GetString(0).ToString();
      bool success = response_args->GetBool(1);
      std::string active_element_selector = response_args->GetString(2).ToString();
      std::string expected_selector = response_args->GetString(3).ToString();
      std::string element_tag = response_args->GetString(4).ToString();
      std::string error_message = response_args->GetString(5).ToString();

      LOG_DEBUG("MessageHandler", "Received verify_focus_response for context " + context_id +
               " success=" + (success ? "true" : "false") +
               " active='" + active_element_selector + "'" +
               " expected='" + expected_selector + "'");

      VerificationResult result;
      result.success = success;
      result.active_element_selector = active_element_selector;
      result.expected_value = expected_selector;
      result.element_tag = element_tag;
      result.error_message = error_message;
      SetVerificationResult(context_id, result);
      return true;
    }

    // Handle pick (dropdown selection) response from renderer
    if (message_name == "pick_from_select_response") {
      CefRefPtr<CefListValue> response_args = message->GetArgumentList();
      std::string context_id = response_args->GetString(0).ToString();
      bool success = response_args->GetBool(1);

      LOG_DEBUG("MessageHandler", "Received pick_from_select_response for context " + context_id +
               " success=" + (success ? "true" : "false"));

      SetPickResult(context_id, success);
      return true;
    }

    // Handle get_checked_state response from renderer
    if (message_name == "get_checked_state_response") {
      CefRefPtr<CefListValue> response_args = message->GetArgumentList();
      std::string context_id = response_args->GetString(0).ToString();
      bool found = response_args->GetBool(1);
      bool is_checked = response_args->GetBool(2);
      std::string element_tag = response_args->GetString(3).ToString();
      std::string error_message = response_args->GetString(4).ToString();

      LOG_DEBUG("MessageHandler", "Received get_checked_state_response for context " + context_id +
               " found=" + (found ? "true" : "false") +
               " is_checked=" + (is_checked ? "true" : "false"));

      VerificationResult result;
      result.success = found;
      result.actual_value = is_checked ? "true" : "false";
      result.element_tag = element_tag;
      result.error_message = error_message;
      SetVerificationResult(context_id, result);
      return true;
    }

    // Handle scroll_position response from renderer
    if (message_name == "scroll_position_response") {
      CefRefPtr<CefListValue> response_args = message->GetArgumentList();
      std::string context_id = response_args->GetString(0).ToString();
      int scroll_x = response_args->GetInt(1);
      int scroll_y = response_args->GetInt(2);
      int scroll_width = response_args->GetInt(3);
      int scroll_height = response_args->GetInt(4);

      LOG_DEBUG("MessageHandler", "Received scroll_position_response for context " + context_id +
               " x=" + std::to_string(scroll_x) + " y=" + std::to_string(scroll_y));

      // Store as "scroll_x,scroll_y,scroll_width,scroll_height" format
      VerificationResult result;
      result.success = true;
      result.actual_value = std::to_string(scroll_x) + "," + std::to_string(scroll_y) + "," +
                           std::to_string(scroll_width) + "," + std::to_string(scroll_height);
      SetVerificationResult(context_id, result);
      return true;
    }

    // Handle upload file verification response from renderer
    if (message_name == "verify_upload_files_response") {
      CefRefPtr<CefListValue> response_args = message->GetArgumentList();
      std::string context_id = response_args->GetString(0).ToString();
      bool success = response_args->GetBool(1);
      int actual_count = response_args->GetInt(2);
      std::string error_message = response_args->GetString(3).ToString();

      LOG_DEBUG("MessageHandler", "Received verify_upload_files_response for context " + context_id +
               " success=" + (success ? "true" : "false") +
               " file_count=" + std::to_string(actual_count));

      VerificationResult result;
      result.success = success;
      result.actual_value = std::to_string(actual_count);
      result.error_message = error_message;
      SetVerificationResult(context_id, result);
      return true;
    }
  }

  return false;
}

// Text extraction methods
void OwlClient::SetExtractedText(const std::string& context_id, const std::string& text) {
  std::lock_guard<std::mutex> lock(text_mutex_);
  extracted_texts_[context_id] = text;
  text_extraction_context_ = context_id;
  text_extraction_complete_ = true;
  LOG_DEBUG("Client", "Text extraction complete for " + context_id);
}

std::string OwlClient::GetExtractedText(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(text_mutex_);
  auto it = extracted_texts_.find(context_id);
  if (it != extracted_texts_.end()) {
    return it->second;
  }
  return "";
}

bool OwlClient::WaitForTextExtraction(const std::string& context_id, int timeout_ms) {
  auto start = std::chrono::steady_clock::now();

  while (true) {
    {
      std::lock_guard<std::mutex> lock(text_mutex_);
      if (text_extraction_complete_ && text_extraction_context_ == context_id) {
        text_extraction_complete_ = false;  // Reset for next extraction
        return true;
      }
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    if (elapsed >= timeout_ms) {
      LOG_WARN("Client", "Text extraction timeout for " + context_id);
      return false;
    }

    OwlBrowserManager::PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

// Verification methods for tool actions
void OwlClient::SetVerificationResult(const std::string& context_id, const VerificationResult& result) {
  std::lock_guard<std::mutex> lock(verification_mutex_);
  verification_results_[context_id] = result;
  verification_context_ = context_id;
  verification_complete_ = true;
  LOG_DEBUG("Client", "Verification complete for " + context_id +
            " success=" + (result.success ? "true" : "false") +
            " actual='" + result.actual_value.substr(0, 50) + "'");
}

OwlClient::VerificationResult OwlClient::GetVerificationResult(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(verification_mutex_);
  auto it = verification_results_.find(context_id);
  if (it != verification_results_.end()) {
    return it->second;
  }
  return VerificationResult{false, "", "", "No verification result found", "", ""};
}

bool OwlClient::WaitForVerification(const std::string& context_id, int timeout_ms) {
  auto start = std::chrono::steady_clock::now();

  while (true) {
    {
      std::lock_guard<std::mutex> lock(verification_mutex_);
      if (verification_complete_ && verification_context_ == context_id) {
        verification_complete_ = false;  // Reset for next verification
        return true;
      }
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    if (elapsed >= timeout_ms) {
      LOG_WARN("Client", "Verification timeout for " + context_id);
      return false;
    }

    OwlBrowserManager::PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

void OwlClient::ResetVerification(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(verification_mutex_);
  verification_results_.erase(context_id);
  if (verification_context_ == context_id) {
    verification_complete_ = false;
  }
}

// Pick (dropdown selection) result methods
void OwlClient::SetPickResult(const std::string& context_id, bool success) {
  std::lock_guard<std::mutex> lock(pick_mutex_);
  pick_results_[context_id] = success;
  pick_context_ = context_id;
  pick_complete_ = true;
  LOG_DEBUG("Client", "Pick complete for " + context_id + " success=" + (success ? "true" : "false"));
}

bool OwlClient::GetPickResult(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(pick_mutex_);
  auto it = pick_results_.find(context_id);
  if (it != pick_results_.end()) {
    return it->second;
  }
  return false;
}

bool OwlClient::WaitForPickResult(const std::string& context_id, int timeout_ms) {
  auto start = std::chrono::steady_clock::now();

  while (true) {
    {
      std::lock_guard<std::mutex> lock(pick_mutex_);
      if (pick_complete_ && pick_context_ == context_id) {
        pick_complete_ = false;  // Reset for next pick
        return true;
      }
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    if (elapsed >= timeout_ms) {
      LOG_WARN("Client", "Pick result timeout for " + context_id);
      return false;
    }

    OwlBrowserManager::PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

void OwlClient::ResetPickResult(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(pick_mutex_);
  pick_results_.erase(context_id);
  if (pick_context_ == context_id) {
    pick_complete_ = false;
  }
}

void OwlClient::SetVideoRecorder(OwlVideoRecorder* recorder) {
  video_recorder_ = recorder;
}
