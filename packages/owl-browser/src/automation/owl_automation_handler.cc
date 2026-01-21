#include "owl_automation_handler.h"
#include "include/cef_process_message.h"
#include "include/wrapper/cef_helpers.h"
#include <sstream>

OwlAutomationHandler::OwlAutomationHandler() {}

bool OwlAutomationHandler::Execute(
    const CefString& name,
    CefRefPtr<CefV8Value> object,
    const CefV8ValueList& arguments,
    CefRefPtr<CefV8Value>& retval,
    CefString& exception) {

  CefRefPtr<CefBrowser> browser = CefV8Context::GetCurrentContext()->GetBrowser();
  CefRefPtr<CefFrame> frame = CefV8Context::GetCurrentContext()->GetFrame();

  if (name == "NativeNavigate") {
    if (arguments.size() == 1 && arguments[0]->IsString()) {
      Navigate(browser, arguments[0]->GetStringValue());
      retval = CefV8Value::CreateBool(true);
      return true;
    }
  }
  else if (name == "NativeClick") {
    if (arguments.size() == 1 && arguments[0]->IsString()) {
      Click(frame, arguments[0]->GetStringValue());
      retval = CefV8Value::CreateBool(true);
      return true;
    }
  }
  else if (name == "NativeType") {
    if (arguments.size() == 2 &&
        arguments[0]->IsString() &&
        arguments[1]->IsString()) {
      Type(frame, arguments[0]->GetStringValue(),
           arguments[1]->GetStringValue());
      retval = CefV8Value::CreateBool(true);
      return true;
    }
  }
  else if (name == "NativeExtractText") {
    if (arguments.size() == 1 && arguments[0]->IsString()) {
      CefString text = ExtractText(frame, arguments[0]->GetStringValue());
      retval = CefV8Value::CreateString(text);
      return true;
    }
  }
  else if (name == "NativeWaitForSelector") {
    if (arguments.size() >= 1 && arguments[0]->IsString()) {
      int timeout = 5000;
      if (arguments.size() == 2 && arguments[1]->IsInt()) {
        timeout = arguments[1]->GetIntValue();
      }
      WaitForSelector(frame, arguments[0]->GetStringValue(), timeout);
      retval = CefV8Value::CreateBool(true);
      return true;
    }
  }

  return false;
}

void OwlAutomationHandler::Navigate(CefRefPtr<CefBrowser> browser, const CefString& url) {
  if (CefCurrentlyOn(TID_RENDERER)) {
    // Send IPC message to browser process
    CefRefPtr<CefProcessMessage> message =
      CefProcessMessage::Create("navigate");
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    args->SetString(0, url);
    browser->GetMainFrame()->SendProcessMessage(PID_BROWSER, message);
  } else {
    browser->GetMainFrame()->LoadURL(url);
  }
}

void OwlAutomationHandler::Click(CefRefPtr<CefFrame> frame, const CefString& selector) {
  std::stringstream js;
  js << "(function() {"
     << "  try {"
     << "    const el = document.querySelector('" << selector.ToString() << "');"
     << "    if (el) {"
     << "      el.scrollIntoView({ behavior: 'instant', block: 'center' });"
     << "      el.click();"
     << "      return true;"
     << "    }"
     << "    return false;"
     << "  } catch(e) {"
     << "    console.error('Click error:', e);"
     << "    return false;"
     << "  }"
     << "})();";

  frame->ExecuteJavaScript(js.str(), frame->GetURL(), 0);
}

void OwlAutomationHandler::Type(
    CefRefPtr<CefFrame> frame,
    const CefString& selector,
    const CefString& text) {

  std::stringstream js;
  js << "(function() {"
     << "  try {"
     << "    const el = document.querySelector('" << selector.ToString() << "');"
     << "    if (el) {"
     << "      el.scrollIntoView({ behavior: 'instant', block: 'center' });"
     << "      el.focus();"
     << "      el.value = '" << text.ToString() << "';"
     << "      el.dispatchEvent(new Event('input', { bubbles: true }));"
     << "      el.dispatchEvent(new Event('change', { bubbles: true }));"
     << "      return true;"
     << "    }"
     << "    return false;"
     << "  } catch(e) {"
     << "    console.error('Type error:', e);"
     << "    return false;"
     << "  }"
     << "})();";

  frame->ExecuteJavaScript(js.str(), frame->GetURL(), 0);
}

CefString OwlAutomationHandler::ExtractText(
    CefRefPtr<CefFrame> frame,
    const CefString& selector) {

  // Note: This is synchronous JS execution, which is limited in CEF
  // For production, should use IPC with callbacks
  std::stringstream js;
  js << "(function() {"
     << "  try {"
     << "    const el = document.querySelector('" << selector.ToString() << "');"
     << "    return el ? el.innerText : '';"
     << "  } catch(e) {"
     << "    return '';"
     << "  }"
     << "})();";

  // This is a simplified version - proper implementation needs V8 promise handling
  return CefString("");
}

void OwlAutomationHandler::WaitForSelector(
    CefRefPtr<CefFrame> frame,
    const CefString& selector,
    int timeout) {

  std::stringstream js;
  js << "(function() {"
     << "  return new Promise((resolve, reject) => {"
     << "    const checkElement = () => {"
     << "      const el = document.querySelector('" << selector.ToString() << "');"
     << "      if (el) {"
     << "        resolve(true);"
     << "        return;"
     << "      }"
     << "      if (Date.now() - startTime > " << timeout << ") {"
     << "        reject(new Error('Timeout waiting for selector'));"
     << "        return;"
     << "      }"
     << "      setTimeout(checkElement, 100);"
     << "    };"
     << "    const startTime = Date.now();"
     << "    checkElement();"
     << "  });"
     << "})();";

  frame->ExecuteJavaScript(js.str(), frame->GetURL(), 0);
}
