#pragma once

#include "include/cef_app.h"
#include "include/cef_client.h"
#include "include/cef_render_process_handler.h"
#include <map>
#include <mutex>
#include <string>

class OwlApp : public CefApp,
                public CefBrowserProcessHandler,
                public CefRenderProcessHandler {
public:
  OwlApp();

  // CefApp methods
  virtual CefRefPtr<CefBrowserProcessHandler>
    GetBrowserProcessHandler() override { return this; }

  virtual CefRefPtr<CefRenderProcessHandler>
    GetRenderProcessHandler() override { return this; }

  virtual void OnRegisterCustomSchemes(
    CefRawPtr<CefSchemeRegistrar> registrar) override;

  // Browser process callbacks
  virtual void OnContextInitialized() override;

  virtual void OnBeforeCommandLineProcessing(
    const CefString& process_type,
    CefRefPtr<CefCommandLine> command_line) override;

  // Called before child process is launched - used to pass switches to renderer
  virtual void OnBeforeChildProcessLaunch(
    CefRefPtr<CefCommandLine> command_line) override;

  // Renderer process callbacks
  // Called after browser created in renderer - receives vm_id via extra_info
  virtual void OnBrowserCreated(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefDictionaryValue> extra_info) override;

  virtual void OnContextCreated(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefV8Context> context) override;

  virtual void OnWebKitInitialized() override;

  virtual bool OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) override;

  // Get vm_id for a browser (used by OnContextCreated and other renderer code)
  static std::string GetBrowserVMId(int browser_id);

private:
  // Dynamic user agent rotation for better stealth
  static std::vector<std::string> user_agents_;
  static std::string GetRandomUserAgent();

  // Session-wide seeds for consistent VM selection across processes
  static int session_gpu_profile_;
  static uint64_t session_vm_seed_;

  // CRITICAL: Per-browser vm_id storage in renderer process
  // Set by OnBrowserCreated, used by OnContextCreated
  static std::map<int, std::string> browser_vm_ids_;
  static std::mutex browser_vm_ids_mutex_;

  // Default VM ID for worker contexts (ServiceWorker, SharedWorker, DedicatedWorker)
  // Workers don't have a browser reference (CEF returns nullptr), so we need a fallback.
  // This is set by OnBrowserCreated and used when browser is null in OnContextCreated.
  static std::string default_worker_vm_id_;

  IMPLEMENT_REFCOUNTING(OwlApp);
};
