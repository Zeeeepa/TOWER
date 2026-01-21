#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "include/cef_browser.h"
#include "include/cef_cookie.h"

// Cookie data structure for easy manipulation
struct CookieData {
  std::string name;
  std::string value;
  std::string domain;
  std::string path;
  bool secure = false;
  bool http_only = false;
  std::string same_site = "lax";  // "none", "lax", "strict", "unspecified"
  std::string priority = "medium";  // "low", "medium", "high"
  bool has_expires = false;
  int64_t expires = -1;  // Unix timestamp
  int64_t creation = 0;  // Unix timestamp
};

// Cookie manager class for handling all cookie operations
class OwlCookieManager {
public:
  // Get cookies as JSON string
  // If url is empty, gets all cookies
  // If url is provided, gets cookies for that URL (including HTTP-only)
  static std::string GetCookies(CefRefPtr<CefBrowser> browser, const std::string& url = "");

  // Set a cookie with full control over attributes
  // Returns true on success
  static bool SetCookie(CefRefPtr<CefBrowser> browser,
                        const std::string& url,
                        const std::string& name,
                        const std::string& value,
                        const std::string& domain = "",
                        const std::string& path = "/",
                        bool secure = false,
                        bool http_only = false,
                        const std::string& same_site = "lax",
                        int64_t expires = -1);

  // Delete cookies
  // If both url and cookie_name are empty, deletes all cookies
  // If only url is provided, deletes all cookies for that URL
  // If both are provided, deletes the specific cookie
  static bool DeleteCookies(CefRefPtr<CefBrowser> browser,
                            const std::string& url = "",
                            const std::string& cookie_name = "");

  // Parse cookies from JSON string into vector of CookieData
  static std::vector<CookieData> ParseCookiesJson(const std::string& json);

  // Convert CookieData vector to JSON string
  static std::string CookiesToJson(const std::vector<CookieData>& cookies);

private:
  // Helper to convert SameSite enum to string
  static std::string SameSiteToString(cef_cookie_same_site_t same_site);

  // Helper to convert string to SameSite enum
  static cef_cookie_same_site_t StringToSameSite(const std::string& same_site);

  // Helper to convert Priority enum to string
  static std::string PriorityToString(cef_cookie_priority_t priority);

  // Helper to escape JSON strings
  static std::string EscapeJsonString(const std::string& str);
};
