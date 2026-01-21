#pragma once

#include "include/cef_scheme.h"
#include "include/cef_resource_handler.h"
#include "include/cef_response.h"
#include "include/cef_request.h"
#include "include/cef_callback.h"
#include <string>
#include <vector>

// Custom resource handler for test:// scheme
// Maps test://filename.html to statics/filename/ directory
// Example: test://user_form.html -> statics/user_form/index.html
class OwlTestSchemeHandler : public CefResourceHandler {
public:
  OwlTestSchemeHandler();

  // CefResourceHandler methods
  virtual bool Open(CefRefPtr<CefRequest> request,
                   bool& handle_request,
                   CefRefPtr<CefCallback> callback) override;

  virtual void GetResponseHeaders(CefRefPtr<CefResponse> response,
                                  int64_t& response_length,
                                  CefString& redirectUrl) override;

  virtual bool Read(void* data_out,
                   int bytes_to_read,
                   int& bytes_read,
                   CefRefPtr<CefResourceReadCallback> callback) override;

  virtual void Cancel() override;

private:
  // Helper methods
  std::string GetMimeType(const std::string& extension);
  bool LoadFile(const std::string& file_path);
  std::string MapTestUrlToFilePath(const std::string& url);

  // Response data
  std::vector<uint8_t> data_;
  size_t offset_;
  std::string mime_type_;
  int status_code_;

  IMPLEMENT_REFCOUNTING(OwlTestSchemeHandler);
};

// Factory for creating test scheme handlers
class OwlTestSchemeHandlerFactory : public CefSchemeHandlerFactory {
public:
  OwlTestSchemeHandlerFactory();

  virtual CefRefPtr<CefResourceHandler> Create(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      const CefString& scheme_name,
      CefRefPtr<CefRequest> request) override;

  IMPLEMENT_REFCOUNTING(OwlTestSchemeHandlerFactory);
};

// Resource handler for HTTPS *.owl domain requests
// Maps URLs like https://lie-detector.owl/ to statics/lie_detector/index.html
// This enables ServiceWorker testing with proper HTTPS secure context
class OwlHttpsResourceHandler : public CefResourceHandler {
public:
  OwlHttpsResourceHandler(const std::string& domain, const std::string& url, int browser_id);

  // CefResourceHandler methods
  virtual bool Open(CefRefPtr<CefRequest> request,
                   bool& handle_request,
                   CefRefPtr<CefCallback> callback) override;

  virtual void GetResponseHeaders(CefRefPtr<CefResponse> response,
                                  int64_t& response_length,
                                  CefString& redirectUrl) override;

  virtual bool Read(void* data_out,
                   int bytes_to_read,
                   int& bytes_read,
                   CefRefPtr<CefResourceReadCallback> callback) override;

  virtual void Cancel() override;

private:
  // Helper methods
  std::string MapHttpsUrlToFilePath(const std::string& url);
  bool LoadFile(const std::string& file_path);
  void PrependWorkerPatches();  // Prepend spoofing patches to worker scripts

  // Instance data
  std::string domain_;
  std::string url_;
  std::vector<uint8_t> data_;
  size_t offset_;
  std::string mime_type_;
  int status_code_;
  int browser_id_;

  IMPLEMENT_REFCOUNTING(OwlHttpsResourceHandler);
};

// Factory for creating HTTPS .owl domain handlers
class OwlHttpsSchemeHandlerFactory : public CefSchemeHandlerFactory {
public:
  OwlHttpsSchemeHandlerFactory();

  virtual CefRefPtr<CefResourceHandler> Create(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      const CefString& scheme_name,
      CefRefPtr<CefRequest> request) override;

  IMPLEMENT_REFCOUNTING(OwlHttpsSchemeHandlerFactory);
};
