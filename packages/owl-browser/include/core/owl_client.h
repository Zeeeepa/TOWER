#pragma once

#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_request_handler.h"
#include "include/cef_resource_request_handler.h"
#include "include/cef_render_handler.h"
#include "include/cef_display_handler.h"
#include "include/cef_permission_handler.h"
#include "include/cef_x509_certificate.h"
#include "owl_proxy_manager.h"
#include <string>
#include <chrono>
#include <atomic>
#include <mutex>
#include <memory>

// Forward declarations
class OwlVideoRecorder;

// Navigation state tracking
enum class NavigationState {
  IDLE,              // No navigation in progress
  STARTING,          // Navigation initiated
  LOADING,           // Page loading (resources being fetched)
  DOM_LOADED,        // DOM content loaded (DOMContentLoaded event)
  COMPLETE,          // Page fully loaded (all resources, onLoad event)
  INTERACTIVE,       // DOM ready but async scripts/resources still loading
  NETWORK_IDLE,      // No network activity for 500ms
  STABLE,            // DOM hasn't changed for 1000ms (best for automation)
  FAILED             // Navigation failed
};

struct NavigationInfo {
  NavigationState state;
  std::string url;
  std::string target_url;  // URL we're navigating to
  std::string title;       // Page title from <title> tag
  int http_status;
  std::string error_message;
  std::chrono::steady_clock::time_point start_time;
  std::chrono::steady_clock::time_point end_time;
  std::chrono::steady_clock::time_point last_network_activity;
  std::chrono::steady_clock::time_point last_dom_mutation;
  bool is_main_frame;
  int pending_requests;      // Count of active network requests
  int dom_mutation_count;    // Total DOM mutations since load

  NavigationInfo() : state(NavigationState::IDLE), http_status(0), is_main_frame(false),
                     pending_requests(0), dom_mutation_count(0) {}
};

class OwlClient : public CefClient,
                   public CefLifeSpanHandler,
                   public CefLoadHandler,
                   public CefRequestHandler,
                   public CefRenderHandler,
                   public CefDisplayHandler,
                   public CefPermissionHandler {
public:
  OwlClient();
  explicit OwlClient(const ProxyConfig& proxy_config);

  // Set proxy config for CA certificate validation
  void SetProxyConfig(const ProxyConfig& config);

  // Set context ID for Tor circuit isolation (different contexts get different exit nodes)
  void SetContextId(const std::string& context_id);

  // Control resource blocking (ads, trackers, analytics)
  void SetResourceBlocking(bool enabled);
  bool IsResourceBlockingEnabled() const { return resource_blocking_enabled_; }

  // CefClient methods
  virtual CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
    return this;
  }

  virtual CefRefPtr<CefLoadHandler> GetLoadHandler() override {
    return this;
  }

  virtual CefRefPtr<CefRequestHandler> GetRequestHandler() override {
    return this;
  }

  virtual CefRefPtr<CefRenderHandler> GetRenderHandler() override {
    return this;
  }

  virtual CefRefPtr<CefDisplayHandler> GetDisplayHandler() override {
    return this;
  }

  virtual CefRefPtr<CefPermissionHandler> GetPermissionHandler() override {
    return this;
  }

  // CefClient message handler
  virtual bool OnProcessMessageReceived(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefProcessId source_process,
      CefRefPtr<CefProcessMessage> message) override;

  // CefLifeSpanHandler methods
  virtual void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  virtual void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  // CefLoadHandler methods
  virtual void OnLoadStart(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          TransitionType transition_type) override;

  virtual void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        int httpStatusCode) override;

  virtual void OnLoadError(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          ErrorCode errorCode,
                          const CefString& errorText,
                          const CefString& failedUrl) override;

  virtual void OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                    bool isLoading,
                                    bool canGoBack,
                                    bool canGoForward) override;

  // CefDisplayHandler methods
  virtual bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                cef_log_severity_t level,
                                const CefString& message,
                                const CefString& source,
                                int line) override;

  virtual void OnTitleChange(CefRefPtr<CefBrowser> browser,
                             const CefString& title) override;

  // CefRequestHandler methods
  virtual CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      bool is_navigation,
      bool is_download,
      const CefString& request_initiator,
      bool& disable_default_handling) override;

  // Handle SSL certificate errors - allows custom CA certificates for SSL interception proxies
  virtual bool OnCertificateError(
      CefRefPtr<CefBrowser> browser,
      cef_errorcode_t cert_error,
      const CefString& request_url,
      CefRefPtr<CefSSLInfo> ssl_info,
      CefRefPtr<CefCallback> callback) override;

  // Handle proxy authentication - critical for SOCKS5 proxies and Tor circuit isolation
  // When using Tor, different username:password combinations create separate circuits (exit nodes)
  virtual bool GetAuthCredentials(
      CefRefPtr<CefBrowser> browser,
      const CefString& origin_url,
      bool isProxy,
      const CefString& host,
      int port,
      const CefString& realm,
      const CefString& scheme,
      CefRefPtr<CefAuthCallback> callback) override;

  // CefPermissionHandler methods - auto-grant camera/mic for virtual camera
  virtual bool OnRequestMediaAccessPermission(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      const CefString& requesting_origin,
      uint32_t requested_permissions,
      CefRefPtr<CefMediaAccessCallback> callback) override;

  // CefRenderHandler methods (for off-screen rendering)
  virtual void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;

  virtual void OnPaint(CefRefPtr<CefBrowser> browser,
                      PaintElementType type,
                      const RectList& dirtyRects,
                      const void* buffer,
                      int width,
                      int height) override;

  virtual bool GetScreenInfo(CefRefPtr<CefBrowser> browser,
                             CefScreenInfo& screen_info) override;

  // Screenshot support
  void SetScreenshotBuffer(std::vector<uint8_t>* buffer);
  void SetScreenshotCropBounds(int x, int y, int width, int height);  // Crop to element bounds
  void ClearScreenshotCropBounds();  // Use full screenshot
  bool IsScreenshotReady() const { return screenshot_ready_; }
  void ResetScreenshot();

  // Frame caching - capture from current rendered frame without re-rendering
  bool GetCroppedScreenshotFromCache(std::vector<uint8_t>* out_buffer,
                                      int x, int y, int width, int height);
  bool GetCroppedBGRAFromCache(std::vector<uint8_t>* out_buffer,
                               int x, int y, int width, int height);  // Returns raw BGRA pixels
  void EncodePNGFromBGRA(std::vector<uint8_t>& output, const uint8_t* bgra_data, int width, int height);
  void FreezeFrameCache();   // Stop updating cache (preserves current frame for CAPTCHA solving)
  void UnfreezeFrameCache(); // Resume normal cache updates
  void GetCachedFrameDimensions(int& width, int& height) const;  // Get current cached frame size

  // Viewport control - AI can request different sizes for mobile/tablet/desktop testing
  void SetViewport(int width, int height);
  int GetViewportWidth() const { return viewport_width_; }
  int GetViewportHeight() const { return viewport_height_; }

  // Navigation state management
  NavigationInfo GetNavigationInfo() const;
  bool IsNavigationComplete() const;
  bool IsNavigationInProgress() const;
  void WaitForNavigation(int timeout_ms);
  bool CheckNavigationComplete();  // Non-blocking check for parallel waiting
  void WaitForStable(int timeout_ms);  // Wait for DOM to stabilize (best for automation)
  void WaitForNetworkIdle(int timeout_ms);  // Wait for network to be idle
  void ResetNavigation();

  // Track network and DOM activity
  void NotifyRequestStarted();
  void NotifyRequestCompleted();
  void NotifyDOMMutation();

  // Element scan tracking
  void NotifyElementScanComplete(const std::string& context_id);
  bool WaitForElementScan(CefRefPtr<CefBrowser> browser, const std::string& context_id, int timeout_ms);

  // Text extraction tracking
  void SetExtractedText(const std::string& context_id, const std::string& text);
  std::string GetExtractedText(const std::string& context_id);
  bool WaitForTextExtraction(const std::string& context_id, int timeout_ms);

  // Tool action verification tracking
  // Used to verify that actions (Type, Focus, ClearInput, etc.) actually succeeded
  struct VerificationResult {
    bool success;
    std::string actual_value;  // What was actually found
    std::string expected_value; // What we expected
    std::string error_message;
    std::string element_tag;   // Tag of the element
    std::string active_element_selector; // For focus verification
  };
  void SetVerificationResult(const std::string& context_id, const VerificationResult& result);
  VerificationResult GetVerificationResult(const std::string& context_id);
  bool WaitForVerification(const std::string& context_id, int timeout_ms);
  void ResetVerification(const std::string& context_id);

  // Pick (dropdown selection) result tracking
  void SetPickResult(const std::string& context_id, bool success);
  bool GetPickResult(const std::string& context_id);
  bool WaitForPickResult(const std::string& context_id, int timeout_ms);
  void ResetPickResult(const std::string& context_id);

  // Video recording support
  void SetVideoRecorder(OwlVideoRecorder* recorder);
  OwlVideoRecorder* GetVideoRecorder() const { return video_recorder_; }

private:
  std::vector<uint8_t>* screenshot_buffer_;
  bool screenshot_ready_;
  int screenshot_width_;
  int screenshot_height_;

  // Screenshot crop bounds (for capturing specific elements)
  bool use_crop_bounds_;
  int crop_x_, crop_y_, crop_width_, crop_height_;

  // Cached frame buffer (stores last rendered frame to avoid re-rendering)
  mutable std::mutex frame_cache_mutex_;
  std::vector<uint8_t> cached_frame_;  // BGRA format
  int cached_frame_width_;
  int cached_frame_height_;
  bool frame_cache_frozen_;  // If true, don't update cache

  // Dynamic viewport sizing
  int viewport_width_;
  int viewport_height_;

  // Navigation state
  mutable std::mutex nav_mutex_;
  NavigationInfo nav_info_;

  // Element scan tracking
  mutable std::mutex scan_mutex_;
  std::string last_scan_context_;
  bool scan_complete_;

  // Text extraction tracking
  mutable std::mutex text_mutex_;
  std::map<std::string, std::string> extracted_texts_;  // context_id -> extracted text
  std::string text_extraction_context_;
  bool text_extraction_complete_;

  // Verification tracking for tool actions (Type, Focus, ClearInput, etc.)
  mutable std::mutex verification_mutex_;
  std::map<std::string, VerificationResult> verification_results_;  // context_id -> result
  std::string verification_context_;
  bool verification_complete_;

  // Pick (dropdown selection) result tracking
  mutable std::mutex pick_mutex_;
  std::map<std::string, bool> pick_results_;  // context_id -> success
  std::string pick_context_;
  bool pick_complete_;

  // Video recording
  OwlVideoRecorder* video_recorder_;  // Not owned by this class

  // Proxy configuration for CA certificate validation
  ProxyConfig proxy_config_;

  // Unique context identifier for Tor circuit isolation
  // Different contexts get different credentials -> different Tor circuits -> different exit nodes
  std::string context_id_;

  // Resource blocking enabled (ads, trackers, analytics)
  bool resource_blocking_enabled_ = true;

  // Static counter for generating unique circuit isolation credentials
  static std::atomic<uint64_t> circuit_counter_;

  IMPLEMENT_REFCOUNTING(OwlClient);
};

