#include "owl_cookie_manager.h"
#include "logger.h"
#include "include/cef_app.h"
#include "include/cef_request_context.h"
#include "include/wrapper/cef_helpers.h"
#include <sstream>
#include <thread>
#include <chrono>

// Cookie visitor that collects cookies into a vector
class OwlCookieVisitor : public CefCookieVisitor {
public:
  OwlCookieVisitor(std::vector<CefCookie>* cookies, bool* done)
    : cookies_(cookies), done_(done) {}

  bool Visit(const CefCookie& cookie, int count, int total, bool& deleteCookie) override {
    cookies_->push_back(cookie);
    if (count == total - 1) {
      *done_ = true;
    }
    return true;  // Continue visiting
  }

private:
  std::vector<CefCookie>* cookies_;
  bool* done_;
  IMPLEMENT_REFCOUNTING(OwlCookieVisitor);
};

// Callback for cookie set operations
class OlibSetCookieCallback : public CefSetCookieCallback {
public:
  OlibSetCookieCallback(bool* success, bool* done)
    : success_(success), done_(done) {}

  void OnComplete(bool success) override {
    *success_ = success;
    *done_ = true;
  }

private:
  bool* success_;
  bool* done_;
  IMPLEMENT_REFCOUNTING(OlibSetCookieCallback);
};

// Callback for cookie delete operations
class OlibDeleteCookiesCallback : public CefDeleteCookiesCallback {
public:
  OlibDeleteCookiesCallback(int* num_deleted, bool* done)
    : num_deleted_(num_deleted), done_(done) {}

  void OnComplete(int num_deleted) override {
    *num_deleted_ = num_deleted;
    *done_ = true;
  }

private:
  int* num_deleted_;
  bool* done_;
  IMPLEMENT_REFCOUNTING(OlibDeleteCookiesCallback);
};

// Helper to convert SameSite enum to string
std::string OwlCookieManager::SameSiteToString(cef_cookie_same_site_t same_site) {
  switch (same_site) {
    case CEF_COOKIE_SAME_SITE_NO_RESTRICTION: return "none";
    case CEF_COOKIE_SAME_SITE_LAX_MODE: return "lax";
    case CEF_COOKIE_SAME_SITE_STRICT_MODE: return "strict";
    default: return "unspecified";
  }
}

// Helper to convert string to SameSite enum
cef_cookie_same_site_t OwlCookieManager::StringToSameSite(const std::string& same_site) {
  if (same_site == "none") return CEF_COOKIE_SAME_SITE_NO_RESTRICTION;
  if (same_site == "strict") return CEF_COOKIE_SAME_SITE_STRICT_MODE;
  if (same_site == "lax") return CEF_COOKIE_SAME_SITE_LAX_MODE;
  return CEF_COOKIE_SAME_SITE_UNSPECIFIED;
}

// Helper to convert Priority enum to string
std::string OwlCookieManager::PriorityToString(cef_cookie_priority_t priority) {
  switch (priority) {
    case CEF_COOKIE_PRIORITY_LOW: return "low";
    case CEF_COOKIE_PRIORITY_HIGH: return "high";
    default: return "medium";
  }
}

// Helper to escape JSON strings
std::string OwlCookieManager::EscapeJsonString(const std::string& str) {
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

std::string OwlCookieManager::GetCookies(CefRefPtr<CefBrowser> browser, const std::string& url) {
  if (!browser) {
    LOG_ERROR("CookieManager", "GetCookies failed - browser is null");
    return "[]";
  }

  LOG_DEBUG("CookieManager", "=== GET COOKIES === url=" + url);

  // Get the request context's cookie manager
  CefRefPtr<CefRequestContext> request_context = browser->GetHost()->GetRequestContext();
  if (!request_context) {
    LOG_ERROR("CookieManager", "GetCookies failed - no request context");
    return "[]";
  }

  CefRefPtr<CefCookieManager> cookie_manager = request_context->GetCookieManager(nullptr);
  if (!cookie_manager) {
    LOG_ERROR("CookieManager", "GetCookies failed - no cookie manager");
    return "[]";
  }

  // Collect cookies
  std::vector<CefCookie> cookies;
  bool done = false;
  CefRefPtr<OwlCookieVisitor> visitor = new OwlCookieVisitor(&cookies, &done);

  bool started;
  if (url.empty()) {
    // Get all cookies
    started = cookie_manager->VisitAllCookies(visitor);
  } else {
    // Get cookies for specific URL (including HTTP-only)
    started = cookie_manager->VisitUrlCookies(url, true, visitor);
  }

  if (!started) {
    LOG_ERROR("CookieManager", "GetCookies failed - could not start cookie visit");
    return "[]";
  }

  // Wait for cookies to be collected with early exit
  auto start = std::chrono::steady_clock::now();
  while (!done) {
    CefDoMessageLoopWork();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    // If cookies were found and visitor hasn't set done yet, give it a moment
    if (!cookies.empty()) {
      // Cookies exist - wait a bit longer for done flag or timeout at 100ms
      if (elapsed > 100) {
        break;
      }
    } else {
      // No cookies found yet - use shorter timeout (50ms)
      if (elapsed > 50) {
        break;
      }
    }

    // Absolute max timeout
    if (elapsed > 5000) {
      LOG_WARN("CookieManager", "GetCookies timeout waiting for cookies");
      break;
    }
  }

  LOG_DEBUG("CookieManager", "Found " + std::to_string(cookies.size()) + " cookies");

  // Build JSON array
  std::ostringstream json;
  json << "[";

  for (size_t i = 0; i < cookies.size(); i++) {
    const CefCookie& cookie = cookies[i];

    if (i > 0) json << ",";

    json << "{";
    json << "\"name\":\"" << EscapeJsonString(CefString(&cookie.name).ToString()) << "\"";
    json << ",\"value\":\"" << EscapeJsonString(CefString(&cookie.value).ToString()) << "\"";
    json << ",\"domain\":\"" << EscapeJsonString(CefString(&cookie.domain).ToString()) << "\"";
    json << ",\"path\":\"" << EscapeJsonString(CefString(&cookie.path).ToString()) << "\"";
    json << ",\"secure\":" << (cookie.secure ? "true" : "false");
    json << ",\"httpOnly\":" << (cookie.httponly ? "true" : "false");
    json << ",\"sameSite\":\"" << SameSiteToString(cookie.same_site) << "\"";
    json << ",\"priority\":\"" << PriorityToString(cookie.priority) << "\"";

    // Creation time (convert from CefBaseTime to Unix timestamp)
    if (cookie.creation.val != 0) {
      // CefBaseTime is microseconds since Windows epoch (1601-01-01)
      // Convert to Unix timestamp (seconds since 1970-01-01)
      // Windows epoch to Unix epoch difference: 11644473600 seconds
      int64_t unix_timestamp = (cookie.creation.val / 1000000) - 11644473600LL;
      json << ",\"creation\":" << unix_timestamp;
    }

    // Expiration
    json << ",\"hasExpires\":" << (cookie.has_expires ? "true" : "false");
    if (cookie.has_expires && cookie.expires.val != 0) {
      int64_t unix_timestamp = (cookie.expires.val / 1000000) - 11644473600LL;
      json << ",\"expires\":" << unix_timestamp;
    }

    json << "}";
  }

  json << "]";

  return json.str();
}

bool OwlCookieManager::SetCookie(CefRefPtr<CefBrowser> browser,
                                  const std::string& url,
                                  const std::string& name,
                                  const std::string& value,
                                  const std::string& domain,
                                  const std::string& path,
                                  bool secure,
                                  bool http_only,
                                  const std::string& same_site,
                                  int64_t expires) {
  if (!browser) {
    LOG_ERROR("CookieManager", "SetCookie failed - browser is null");
    return false;
  }

  LOG_DEBUG("CookieManager", "=== SET COOKIE === name=" + name + " domain=" + domain + " path=" + path);

  // Get the request context's cookie manager
  CefRefPtr<CefRequestContext> request_context = browser->GetHost()->GetRequestContext();
  if (!request_context) {
    LOG_ERROR("CookieManager", "SetCookie failed - no request context");
    return false;
  }

  CefRefPtr<CefCookieManager> cookie_manager = request_context->GetCookieManager(nullptr);
  if (!cookie_manager) {
    LOG_ERROR("CookieManager", "SetCookie failed - no cookie manager");
    return false;
  }

  // Build the CefCookie
  CefCookie cookie;
  CefString(&cookie.name) = name;
  CefString(&cookie.value) = value;
  CefString(&cookie.domain) = domain;
  CefString(&cookie.path) = path.empty() ? "/" : path;
  cookie.secure = secure ? 1 : 0;
  cookie.httponly = http_only ? 1 : 0;
  cookie.same_site = StringToSameSite(same_site);
  cookie.priority = CEF_COOKIE_PRIORITY_MEDIUM;

  // Set expiration
  if (expires > 0) {
    cookie.has_expires = 1;
    // Convert Unix timestamp to CefBaseTime (microseconds since Windows epoch)
    cookie.expires.val = (expires + 11644473600LL) * 1000000;
  } else {
    cookie.has_expires = 0;
  }

  // Set the cookie with callback
  bool success = false;
  bool done = false;
  CefRefPtr<OlibSetCookieCallback> callback = new OlibSetCookieCallback(&success, &done);

  bool started = cookie_manager->SetCookie(url, cookie, callback);
  if (!started) {
    LOG_ERROR("CookieManager", "SetCookie failed - could not start cookie set");
    return false;
  }

  // Wait for completion with aggressive polling
  auto start = std::chrono::steady_clock::now();
  while (!done) {
    CefDoMessageLoopWork();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    if (elapsed > 5000) {
      LOG_WARN("CookieManager", "SetCookie timeout waiting for completion");
      return false;
    }
  }

  if (success) {
    LOG_DEBUG("CookieManager", "Cookie set successfully: " + name);
  } else {
    LOG_ERROR("CookieManager", "Cookie set failed: " + name);
  }

  return success;
}

bool OwlCookieManager::DeleteCookies(CefRefPtr<CefBrowser> browser,
                                      const std::string& url,
                                      const std::string& cookie_name) {
  if (!browser) {
    LOG_ERROR("CookieManager", "DeleteCookies failed - browser is null");
    return false;
  }

  LOG_DEBUG("CookieManager", "=== DELETE COOKIES === url=" + url + " name=" + cookie_name);

  // Get the request context's cookie manager
  CefRefPtr<CefRequestContext> request_context = browser->GetHost()->GetRequestContext();
  if (!request_context) {
    LOG_ERROR("CookieManager", "DeleteCookies failed - no request context");
    return false;
  }

  CefRefPtr<CefCookieManager> cookie_manager = request_context->GetCookieManager(nullptr);
  if (!cookie_manager) {
    LOG_ERROR("CookieManager", "DeleteCookies failed - no cookie manager");
    return false;
  }

  // Delete cookies with callback
  int num_deleted = 0;
  bool done = false;
  CefRefPtr<OlibDeleteCookiesCallback> callback = new OlibDeleteCookiesCallback(&num_deleted, &done);

  bool started = cookie_manager->DeleteCookies(url, cookie_name, callback);
  if (!started) {
    LOG_ERROR("CookieManager", "DeleteCookies failed - could not start cookie delete");
    return false;
  }

  // Wait for completion with aggressive polling
  auto start = std::chrono::steady_clock::now();
  while (!done) {
    CefDoMessageLoopWork();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    if (elapsed > 5000) {
      LOG_WARN("CookieManager", "DeleteCookies timeout waiting for completion");
      return false;
    }
  }

  LOG_DEBUG("CookieManager", "Deleted " + std::to_string(num_deleted) + " cookies");

  return true;
}

std::vector<CookieData> OwlCookieManager::ParseCookiesJson(const std::string& json) {
  std::vector<CookieData> cookies;

  if (json.empty() || json == "[]") {
    return cookies;
  }

  // Simple JSON array parser for cookies
  // Find each cookie object in the array
  size_t pos = 0;
  while ((pos = json.find('{', pos)) != std::string::npos) {
    // Find the matching closing brace
    int brace_count = 1;
    size_t end_pos = pos + 1;
    while (end_pos < json.size() && brace_count > 0) {
      if (json[end_pos] == '{') brace_count++;
      else if (json[end_pos] == '}') brace_count--;
      end_pos++;
    }

    if (brace_count != 0) break;  // Malformed JSON

    std::string cookie_obj = json.substr(pos, end_pos - pos);
    CookieData cookie;

    // Helper lambda to extract string value
    auto extractString = [&cookie_obj](const std::string& key) -> std::string {
      std::string search = "\"" + key + "\"";
      size_t key_pos = cookie_obj.find(search);
      if (key_pos == std::string::npos) return "";

      size_t colon_pos = cookie_obj.find(":", key_pos);
      if (colon_pos == std::string::npos) return "";

      size_t quote_start = cookie_obj.find("\"", colon_pos + 1);
      if (quote_start == std::string::npos) return "";

      size_t quote_end = quote_start + 1;
      while (quote_end < cookie_obj.size()) {
        if (cookie_obj[quote_end] == '"' && cookie_obj[quote_end - 1] != '\\') break;
        quote_end++;
      }

      std::string value = cookie_obj.substr(quote_start + 1, quote_end - quote_start - 1);

      // Unescape basic JSON escapes
      std::string result;
      for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
          switch (value[i + 1]) {
            case '"': result += '"'; i++; break;
            case '\\': result += '\\'; i++; break;
            case 'n': result += '\n'; i++; break;
            case 'r': result += '\r'; i++; break;
            case 't': result += '\t'; i++; break;
            default: result += value[i]; break;
          }
        } else {
          result += value[i];
        }
      }
      return result;
    };

    // Helper lambda to extract boolean value
    auto extractBool = [&cookie_obj](const std::string& key, bool default_val = false) -> bool {
      std::string search = "\"" + key + "\"";
      size_t key_pos = cookie_obj.find(search);
      if (key_pos == std::string::npos) return default_val;

      size_t colon_pos = cookie_obj.find(":", key_pos);
      if (colon_pos == std::string::npos) return default_val;

      size_t val_start = cookie_obj.find_first_not_of(" \t\n\r", colon_pos + 1);
      if (val_start == std::string::npos) return default_val;

      return cookie_obj.substr(val_start, 4) == "true";
    };

    // Helper lambda to extract int64 value
    auto extractInt64 = [&cookie_obj](const std::string& key, int64_t default_val = 0) -> int64_t {
      std::string search = "\"" + key + "\"";
      size_t key_pos = cookie_obj.find(search);
      if (key_pos == std::string::npos) return default_val;

      size_t colon_pos = cookie_obj.find(":", key_pos);
      if (colon_pos == std::string::npos) return default_val;

      size_t val_start = cookie_obj.find_first_of("-0123456789", colon_pos + 1);
      if (val_start == std::string::npos) return default_val;

      size_t val_end = cookie_obj.find_first_not_of("-0123456789", val_start);
      if (val_end == std::string::npos) val_end = cookie_obj.size();

      std::string num_str = cookie_obj.substr(val_start, val_end - val_start);
      if (num_str.empty()) return default_val;

      // Manual parsing to avoid exceptions
      int64_t result = 0;
      int64_t sign = 1;
      size_t i = 0;
      if (num_str[0] == '-') {
        sign = -1;
        i = 1;
      }
      for (; i < num_str.size(); i++) {
        if (num_str[i] < '0' || num_str[i] > '9') return default_val;
        result = result * 10 + (num_str[i] - '0');
      }
      return result * sign;
    };

    // Parse cookie fields
    cookie.name = extractString("name");
    cookie.value = extractString("value");
    cookie.domain = extractString("domain");
    cookie.path = extractString("path");
    if (cookie.path.empty()) cookie.path = "/";
    cookie.secure = extractBool("secure", false);
    cookie.http_only = extractBool("httpOnly", false);
    cookie.same_site = extractString("sameSite");
    if (cookie.same_site.empty()) cookie.same_site = "lax";
    cookie.priority = extractString("priority");
    if (cookie.priority.empty()) cookie.priority = "medium";
    cookie.has_expires = extractBool("hasExpires", false);
    cookie.expires = extractInt64("expires", -1);
    cookie.creation = extractInt64("creation", 0);

    // Only add valid cookies (must have name)
    if (!cookie.name.empty()) {
      cookies.push_back(cookie);
    }

    pos = end_pos;
  }

  LOG_DEBUG("CookieManager", "Parsed " + std::to_string(cookies.size()) + " cookies from JSON");

  return cookies;
}

std::string OwlCookieManager::CookiesToJson(const std::vector<CookieData>& cookies) {
  std::ostringstream json;
  json << "[";

  for (size_t i = 0; i < cookies.size(); i++) {
    const CookieData& cookie = cookies[i];

    if (i > 0) json << ",";

    json << "{";
    json << "\"name\":\"" << EscapeJsonString(cookie.name) << "\"";
    json << ",\"value\":\"" << EscapeJsonString(cookie.value) << "\"";
    json << ",\"domain\":\"" << EscapeJsonString(cookie.domain) << "\"";
    json << ",\"path\":\"" << EscapeJsonString(cookie.path) << "\"";
    json << ",\"secure\":" << (cookie.secure ? "true" : "false");
    json << ",\"httpOnly\":" << (cookie.http_only ? "true" : "false");
    json << ",\"sameSite\":\"" << cookie.same_site << "\"";
    json << ",\"priority\":\"" << cookie.priority << "\"";

    if (cookie.creation != 0) {
      json << ",\"creation\":" << cookie.creation;
    }

    json << ",\"hasExpires\":" << (cookie.has_expires ? "true" : "false");
    if (cookie.has_expires && cookie.expires > 0) {
      json << ",\"expires\":" << cookie.expires;
    }

    json << "}";
  }

  json << "]";

  return json.str();
}
