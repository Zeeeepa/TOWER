#pragma once

#include <string>
#include <vector>
#include "include/cef_browser.h"

/**
 * CaptchaUtils - Utility functions for CAPTCHA handling
 *
 * Provides common functionality needed by CAPTCHA solvers:
 * - Scroll to CAPTCHA element
 * - Isolated element screenshots
 * - Element boundary detection
 * - Wait for element visibility
 * - Skip/reload/refresh actions
 */

class OwlCaptchaUtils {
public:
  /**
   * Scroll an element into view, ensuring it's fully visible
   * @param browser The CEF browser instance
   * @param selector CSS selector of element to scroll to
   * @param align Alignment option: "top", "center", "bottom"
   * @return True if successful
   */
  static bool ScrollIntoView(CefRefPtr<CefBrowser> browser,
                             const std::string& selector,
                             const std::string& align = "center");

  /**
   * Capture screenshot of only a specific element (isolated)
   * @param browser The CEF browser instance
   * @param context_id Browser context ID
   * @param selector CSS selector of element to capture
   * @return PNG image data
   */
  static std::vector<uint8_t> CaptureElementScreenshot(CefRefPtr<CefBrowser> browser,
                                                       const std::string& context_id,
                                                       const std::string& selector);

  /**
   * Get element boundaries (position and size)
   * @param browser The CEF browser instance
   * @param context_id Browser context ID
   * @param selector CSS selector
   * @return JSON string with {x, y, width, height}
   */
  static std::string GetElementBounds(CefRefPtr<CefBrowser> browser,
                                     const std::string& context_id,
                                     const std::string& selector);

  /**
   * Wait for element to be visible on page
   * @param browser The CEF browser instance
   * @param selector CSS selector
   * @param timeout_ms Timeout in milliseconds
   * @return True if element became visible
   */
  static bool WaitForVisible(CefRefPtr<CefBrowser> browser,
                            const std::string& selector,
                            int timeout_ms = 5000);

  /**
   * Check if element is currently visible
   * @param browser The CEF browser instance
   * @param selector CSS selector
   * @return True if visible
   */
  static bool IsVisible(CefRefPtr<CefBrowser> browser,
                       const std::string& selector);

  /**
   * Check if element exists in DOM
   * @param browser The CEF browser instance
   * @param selector CSS selector
   * @return True if exists
   */
  static bool ElementExists(CefRefPtr<CefBrowser> browser,
                           const std::string& selector);

  /**
   * Click an element using C++ native CEF mouse events (with retry logic)
   * @param browser The CEF browser instance
   * @param context_id Context ID for render tracker
   * @param selector CSS selector
   * @param max_retries Maximum retry attempts
   * @return True if clicked successfully
   */
  static bool ClickElement(CefRefPtr<CefBrowser> browser,
                          const std::string& context_id,
                          const std::string& selector,
                          int max_retries = 3);

  /**
   * Type text into an input field
   * @param browser The CEF browser instance
   * @param context_id Browser context ID (needed for render tracker)
   * @param selector CSS selector
   * @param text Text to type
   * @return True if successful
   */
  static bool TypeIntoElement(CefRefPtr<CefBrowser> browser,
                             const std::string& context_id,
                             const std::string& selector,
                             const std::string& text);

  /**
   * Execute JavaScript and get result as string
   * @param browser The CEF browser instance
   * @param script JavaScript code to execute
   * @return Result as string
   */
  static std::string ExecuteJavaScriptAndGetResult(CefRefPtr<CefBrowser> browser,
                                                   const std::string& script);

  /**
   * Wait for a specified duration
   * @param milliseconds Duration to wait
   */
  static void Wait(int milliseconds);

  /**
   * Highlight an element with colored border and background
   * @param browser The CEF browser instance
   * @param selector CSS selector
   * @param border_color Border color (CSS format)
   * @param background_color Background color (CSS format)
   * @param duration_ms Duration in milliseconds (0 = permanent)
   * @return True if successful
   */
  static bool HighlightElement(CefRefPtr<CefBrowser> browser,
                               const std::string& selector,
                               const std::string& border_color = "#FF0000",
                               const std::string& background_color = "rgba(255, 0, 0, 0.2)",
                               int duration_ms = 0);

  /**
   * Remove highlight from an element
   * @param browser The CEF browser instance
   * @param selector CSS selector
   * @return True if successful
   */
  static bool RemoveHighlight(CefRefPtr<CefBrowser> browser,
                             const std::string& selector);

  /**
   * Get all elements matching a selector
   * @param browser The CEF browser instance
   * @param selector CSS selector
   * @return Array of element indices or IDs
   */
  static std::vector<std::string> GetAllElements(CefRefPtr<CefBrowser> browser,
                                                 const std::string& selector);

  /**
   * Extract text content from element
   * @param browser The CEF browser instance
   * @param selector CSS selector
   * @return Text content
   */
  static std::string GetTextContent(CefRefPtr<CefBrowser> browser,
                                   const std::string& selector);

  /**
   * Get element attribute value
   * @param browser The CEF browser instance
   * @param selector CSS selector
   * @param attribute Attribute name
   * @return Attribute value
   */
  static std::string GetAttribute(CefRefPtr<CefBrowser> browser,
                                 const std::string& selector,
                                 const std::string& attribute);

  /**
   * Base64 encode binary data
   * @param data Binary data
   * @return Base64 encoded string
   */
  static std::string Base64Encode(const std::vector<uint8_t>& data);

  /**
   * Base64 decode string to binary
   * @param encoded Base64 encoded string
   * @return Binary data
   */
  static std::vector<uint8_t> Base64Decode(const std::string& encoded);
};
