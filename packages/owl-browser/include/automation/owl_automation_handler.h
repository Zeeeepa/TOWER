#pragma once

#include "include/cef_v8.h"
#include "include/cef_browser.h"

class OwlAutomationHandler : public CefV8Handler {
public:
  OwlAutomationHandler();

  virtual bool Execute(
    const CefString& name,
    CefRefPtr<CefV8Value> object,
    const CefV8ValueList& arguments,
    CefRefPtr<CefV8Value>& retval,
    CefString& exception) override;

private:
  // Native implementations
  void Navigate(CefRefPtr<CefBrowser> browser, const CefString& url);
  void Click(CefRefPtr<CefFrame> frame, const CefString& selector);
  void Type(CefRefPtr<CefFrame> frame, const CefString& selector, const CefString& text);
  CefString ExtractText(CefRefPtr<CefFrame> frame, const CefString& selector);
  void WaitForSelector(CefRefPtr<CefFrame> frame, const CefString& selector, int timeout);

  IMPLEMENT_REFCOUNTING(OwlAutomationHandler);
};

