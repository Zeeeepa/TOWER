#pragma once

#include "include/cef_render_handler.h"
#include "include/cef_v8.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <mutex>

// Parsed CSS selector component
struct SelectorComponent {
  std::string tag;               // Tag name (empty if not specified)
  std::string id;                // ID (without #)
  std::vector<std::string> classes;  // Class names (without .)
  std::vector<std::pair<std::string, std::string>> attributes;  // attr=value pairs
  bool valid;                    // Whether selector was parsed successfully

  SelectorComponent() : valid(false) {}
};

// Structure to hold element rendering information
struct ElementRenderInfo {
  std::string selector;
  std::string tag;
  std::string tagName;         // Original tag name (uppercase, e.g., "IMG")
  std::string id;
  std::string className;
  int x;
  int y;
  int width;
  int height;
  bool visible;
  std::string text;
  std::string aria_label;

  // Enhanced attributes for better semantic matching
  std::string role;            // ARIA role or data-role
  std::string placeholder;     // input placeholder
  std::string value;           // input/button value
  std::string type;            // input type
  std::string title;           // title attribute
  std::string name;            // name attribute
  std::string nearby_text;     // text from nearby labels
  std::string label_for;       // for attribute on LABEL elements (references INPUT id)
  std::string alt;             // alt attribute (for IMG elements)
  std::string src;             // src attribute (for IMG, SCRIPT, etc.)

  // Enhanced position/visibility info
  int z_index;                 // CSS z-index for overlap detection
  std::string transform;       // CSS transform if present
  float opacity;               // CSS opacity
  std::string display;         // CSS display property
  std::string visibility_css;  // CSS visibility property
};

// Tracks all rendered elements and their exact screen positions
class OwlRenderTracker {
public:
  static OwlRenderTracker* GetInstance();

  // Called during rendering to update element positions
  void UpdateElementPosition(const std::string& element_id, int x, int y, int width, int height);

  // Query element position by CSS selector
  bool GetElementBounds(const std::string& context_id, const std::string& selector, ElementRenderInfo& info);

  // Get all visible elements on the page
  std::vector<ElementRenderInfo> GetAllVisibleElements(const std::string& context_id);

  // Get ALL elements on the page (including hidden ones)
  std::vector<ElementRenderInfo> GetAllElements(const std::string& context_id);

  // Clear tracking for a context
  void ClearContext(const std::string& context_id);

  // Register element from renderer process
  void RegisterElement(const std::string& context_id, const ElementRenderInfo& info);

private:
  OwlRenderTracker() = default;
  static OwlRenderTracker* instance_;

  std::mutex mutex_;
  // context_id -> element_key -> ElementRenderInfo
  std::unordered_map<std::string, std::unordered_map<std::string, ElementRenderInfo>> element_map_;

  // Selector parsing cache (selector string -> parsed result)
  std::unordered_map<std::string, SelectorComponent> selector_cache_;
  std::mutex cache_mutex_;

  std::string GenerateElementKey(const ElementRenderInfo& info);

  // Parse a CSS selector into components
  SelectorComponent ParseSelector(const std::string& selector);

  // Check if an element matches parsed selector components
  bool ElementMatchesSelector(const ElementRenderInfo& elem, const SelectorComponent& parsed);

  // Get attribute value from element by name
  std::string GetElementAttribute(const ElementRenderInfo& elem, const std::string& attrName) const;

  // Parse attribute value, handling escaped quotes
  std::string ParseAttributeValue(const std::string& raw) const;
};

// Custom render handler to intercept paint events
class OlibRenderHandler : public CefRenderHandler {
public:
  OlibRenderHandler(const std::string& context_id);

  // CefRenderHandler methods
  void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;

  void OnPaint(CefRefPtr<CefBrowser> browser,
               PaintElementType type,
               const RectList& dirtyRects,
               const void* buffer,
               int width,
               int height) override;

  void OnAcceleratedPaint(CefRefPtr<CefBrowser> browser,
                          PaintElementType type,
                          const RectList& dirtyRects,
                          const CefAcceleratedPaintInfo& info) override;

  bool GetScreenInfo(CefRefPtr<CefBrowser> browser, CefScreenInfo& screen_info) override;

private:
  std::string context_id_;
  int view_width_;
  int view_height_;

  IMPLEMENT_REFCOUNTING(OlibRenderHandler);
};
