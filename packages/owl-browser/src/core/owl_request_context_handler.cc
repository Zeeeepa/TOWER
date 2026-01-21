/**
 * OwlRequestContextHandler Implementation
 *
 * This handler intercepts ServiceWorker script requests at the request context level,
 * solving the critical issue where CefClient::GetResourceRequestHandler is not called
 * for ServiceWorker fetches (browser and frame are NULL).
 *
 * The key insight: CEF's CefRequestHandler only intercepts requests with a browser
 * reference. ServiceWorker script fetches happen outside any browser context, so
 * we need CefRequestContextHandler which receives ALL requests.
 */

#include "owl_request_context_handler.h"
#include "stealth/workers/owl_worker_patcher.h"
#include "stealth/owl_virtual_machine.h"
#include "stealth/owl_spoof_manager.h"
#include "logger.h"

#include <algorithm>

// ============================================================================
// ServiceWorkerContextFilter - Response filter for patching SW scripts
// Uses VM ID lookup since browser_id is not available for SW requests
// ============================================================================

class ServiceWorkerContextFilter : public CefResponseFilter {
 public:
  ServiceWorkerContextFilter(const std::string& vm_id, const std::string& url)
      : vm_id_(vm_id), url_(url) {
    LOG_INFO("SW-CONTEXT", "ServiceWorkerContextFilter created for vm_id=" + vm_id_ +
             " url=" + url_);
  }

  bool InitFilter() override {
    return true;
  }

  FilterStatus Filter(void* data_in, size_t data_in_size, size_t& data_in_read,
                      void* data_out, size_t data_out_size, size_t& data_out_written) override {
    data_in_read = 0;
    data_out_written = 0;

    // Buffer all incoming data
    if (data_in_size > 0) {
      buffered_data_.insert(buffered_data_.end(),
                            static_cast<const char*>(data_in),
                            static_cast<const char*>(data_in) + data_in_size);
      data_in_read = data_in_size;
    }

    // Generate patch code once we have enough data or end of stream
    if (!patch_generated_) {
      const size_t MIN_DETECT_SIZE = 512;

      if (buffered_data_.size() >= MIN_DETECT_SIZE || data_in_size == 0) {
        patch_generated_ = true;

        // Get VM by ID
        const owl::VirtualMachine* vm = nullptr;
        if (!vm_id_.empty()) {
          vm = owl::VirtualMachineDB::Instance().GetVM(vm_id_);
        }

        if (!vm) {
          // Fallback to random VM if ID lookup fails
          vm = owl::VirtualMachineDB::Instance().SelectRandomVM();
          LOG_WARN("SW-CONTEXT", "VM ID '" + vm_id_ + "' not found, using random VM: " +
                   (vm ? vm->id : "NONE"));
        }

        if (vm) {
          // Generate ServiceWorker patch code
          patch_code_ = owl::spoofs::SpoofManager::Instance().GenerateWorkerScript(
              *vm,
              owl::spoofs::ContextType::SERVICE_WORKER);
          patch_code_ += "\n";

          LOG_INFO("SW-CONTEXT", "Generated patch for " + url_ +
                   " using VM '" + vm->id + "': " + std::to_string(patch_code_.size()) + " bytes");
        } else {
          LOG_ERROR("SW-CONTEXT", "No VM available for patching " + url_);
        }
      } else {
        return RESPONSE_FILTER_NEED_MORE_DATA;
      }
    }

    // Write patch code prefix
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
    }

    // Output buffered data
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

    // Check if complete
    if (data_in_size == 0 && prefix_written_ && buffer_offset_ >= buffered_data_.size()) {
      LOG_INFO("SW-CONTEXT", "Filter complete for " + url_ + ". Total output: " +
               std::to_string(patch_code_.size() + buffered_data_.size()) + " bytes");
      return RESPONSE_FILTER_DONE;
    }

    return RESPONSE_FILTER_NEED_MORE_DATA;
  }

 private:
  std::string vm_id_;
  std::string url_;
  std::string patch_code_;
  size_t prefix_offset_ = 0;
  bool prefix_written_ = false;
  bool patch_generated_ = false;
  std::vector<char> buffered_data_;
  size_t buffer_offset_ = 0;

  IMPLEMENT_REFCOUNTING(ServiceWorkerContextFilter);
};

// ============================================================================
// ServiceWorkerContextRequestHandler - Resource handler for SW scripts
// ============================================================================

class ServiceWorkerContextRequestHandler : public CefResourceRequestHandler {
 public:
  ServiceWorkerContextRequestHandler(const std::string& vm_id)
      : vm_id_(vm_id) {}

  CefRefPtr<CefResponseFilter> GetResourceResponseFilter(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      CefRefPtr<CefResponse> response) override {

    std::string url = request ? request->GetURL().ToString() : "null";
    std::string mime = response ? response->GetMimeType().ToString() : "null";
    int status = response ? response->GetStatus() : 0;

    LOG_INFO("SW-CONTEXT", ">>> GetResourceResponseFilter CALLED: " + url +
             " MIME=" + mime + " Status=" + std::to_string(status) +
             " vm_id=" + vm_id_);

    // Only filter JavaScript responses
    if (mime.find("javascript") != std::string::npos ||
        mime.find("ecmascript") != std::string::npos ||
        url.find(".js") != std::string::npos) {
      LOG_INFO("SW-CONTEXT", ">>> APPLYING ServiceWorkerContextFilter for: " + url);
      return new ServiceWorkerContextFilter(vm_id_, url);
    }

    LOG_DEBUG("SW-CONTEXT", ">>> NOT applying filter - MIME doesn't match: " + mime);
    return nullptr;
  }

 private:
  std::string vm_id_;
  IMPLEMENT_REFCOUNTING(ServiceWorkerContextRequestHandler);
};

// ============================================================================
// OwlRequestContextHandler Implementation
// ============================================================================

OwlRequestContextHandler::OwlRequestContextHandler(const std::string& vm_id,
                                                     const std::string& context_id)
    : vm_id_(vm_id), context_id_(context_id) {
  LOG_INFO("SW-CONTEXT", "OwlRequestContextHandler created: vm_id=" + vm_id_ +
           " context_id=" + context_id_);
}

void OwlRequestContextHandler::SetVMId(const std::string& vm_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  vm_id_ = vm_id;
  LOG_INFO("SW-CONTEXT", "VM ID updated to: " + vm_id_);
}

std::string OwlRequestContextHandler::GetVMId() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return vm_id_;
}

void OwlRequestContextHandler::OnRequestContextInitialized(
    CefRefPtr<CefRequestContext> request_context) {
  LOG_INFO("SW-CONTEXT", "Request context initialized for: " + context_id_);
}

CefRefPtr<CefResourceRequestHandler> OwlRequestContextHandler::GetResourceRequestHandler(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    bool is_navigation,
    bool is_download,
    const CefString& request_initiator,
    bool& disable_default_handling) {

  if (!request) {
    return nullptr;
  }

  std::string url = request->GetURL().ToString();
  CefRequest::ResourceType res_type = request->GetResourceType();

  // Only intercept when browser is NULL (ServiceWorker context)
  // When browser is non-NULL, CefClient::GetResourceRequestHandler handles it
  bool browser_is_null = !browser.get();

  // Log ServiceWorker-related requests for debugging
  if (url.find(".js") != std::string::npos) {
    LOG_DEBUG("SW-CONTEXT", "GetResourceRequestHandler: url=" + url +
             " res_type=" + std::to_string(res_type) +
             " browser_null=" + std::string(browser_is_null ? "YES" : "NO") +
             " is_nav=" + std::string(is_navigation ? "YES" : "NO"));
  }

  // CRITICAL: Intercept ServiceWorker script fetches (browser is NULL)
  if (browser_is_null) {
    bool is_sw_script = false;

    // Check resource type
    if (res_type == RT_SERVICE_WORKER) {
      is_sw_script = true;
      LOG_INFO("SW-CONTEXT", "Detected RT_SERVICE_WORKER request: " + url);
    }
    // Also check worker types
    else if (res_type == RT_WORKER || res_type == RT_SHARED_WORKER) {
      is_sw_script = true;
      LOG_INFO("SW-CONTEXT", "Detected RT_WORKER/RT_SHARED_WORKER request: " + url);
    }
    // Check URL patterns for potential worker scripts
    else if (!is_navigation && url.find(".js") != std::string::npos) {
      std::string lower_url = url;
      std::transform(lower_url.begin(), lower_url.end(), lower_url.begin(), ::tolower);

      if (lower_url.find("creep.js") != std::string::npos ||
          lower_url.find("/sw.js") != std::string::npos ||
          lower_url.find("service-worker") != std::string::npos ||
          lower_url.find("serviceworker") != std::string::npos ||
          lower_url.find("/worker.js") != std::string::npos) {
        is_sw_script = true;
        LOG_INFO("SW-CONTEXT", "Detected worker script by URL pattern: " + url);
      }
    }

    if (is_sw_script) {
      std::string current_vm_id;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        current_vm_id = vm_id_;
      }

      LOG_INFO("SW-CONTEXT", ">>> RETURNING ServiceWorkerContextRequestHandler for: " + url +
               " vm_id=" + current_vm_id);
      return new ServiceWorkerContextRequestHandler(current_vm_id);
    }
  }

  // Let CefClient handle requests with browser reference
  return nullptr;
}
