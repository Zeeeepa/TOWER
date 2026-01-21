/**
 * OwlRequestContextHandler
 *
 * CefRequestContextHandler implementation that intercepts ServiceWorker script
 * requests from remote hosts. This solves the critical issue where CEF's
 * CefClient::GetResourceRequestHandler is NOT called for ServiceWorker fetches
 * because browser and frame are NULL.
 *
 * This handler:
 * 1. Intercepts all resource requests at the request context level
 * 2. Detects ServiceWorker script requests (RT_SERVICE_WORKER or URL patterns)
 * 3. Applies ServiceWorkerResponseFilter to patch the scripts with spoofing code
 *
 * CRITICAL: ServiceWorker requests have browser=NULL and frame=NULL, so we cannot
 * use CefClient's GetResourceRequestHandler. This handler is the only way to
 * intercept and modify ServiceWorker scripts fetched from remote URLs.
 */

#ifndef OWL_REQUEST_CONTEXT_HANDLER_H_
#define OWL_REQUEST_CONTEXT_HANDLER_H_

#include "include/cef_request_context_handler.h"
#include "include/cef_resource_request_handler.h"
#include "include/cef_response_filter.h"
#include <string>
#include <atomic>
#include <mutex>

/**
 * Request context handler for ServiceWorker interception.
 * 
 * This handler is passed to CefRequestContext::CreateContext() and receives
 * ALL resource requests, including those without a browser reference.
 */
class OwlRequestContextHandler : public CefRequestContextHandler {
 public:
  /**
   * Create a request context handler with the VM ID for spoofing.
   * 
   * @param vm_id The VirtualMachine ID to use for spoofing ServiceWorker scripts.
   *              This is stored and used by the ResponseFilter when patching scripts.
   * @param context_id The browser context ID for logging/debugging.
   */
  explicit OwlRequestContextHandler(const std::string& vm_id,
                                     const std::string& context_id);

  /**
   * Update the VM ID (used when profile changes or VM is selected later).
   */
  void SetVMId(const std::string& vm_id);

  /**
   * Get the current VM ID.
   */
  std::string GetVMId() const;

  // CefRequestContextHandler methods
  void OnRequestContextInitialized(
      CefRefPtr<CefRequestContext> request_context) override;

  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      bool is_navigation,
      bool is_download,
      const CefString& request_initiator,
      bool& disable_default_handling) override;

 private:
  std::string vm_id_;
  std::string context_id_;
  mutable std::mutex mutex_;

  IMPLEMENT_REFCOUNTING(OwlRequestContextHandler);
  DISALLOW_COPY_AND_ASSIGN(OwlRequestContextHandler);
};

#endif  // OWL_REQUEST_CONTEXT_HANDLER_H_
