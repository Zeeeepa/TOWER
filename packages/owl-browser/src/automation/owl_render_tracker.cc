#include "owl_render_tracker.h"
#include "logger.h"
#include <sstream>
#include <algorithm>
#include <cctype>

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#endif

namespace {
// Get actual system DPR (device pixel ratio)
// CRITICAL: This must return the real system DPR, not spoofed value
// CEF needs to know the actual display scaling to render fonts correctly
// Without this, fontPreferences will be 2x on Retina Macs
float GetActualSystemDPR() {
#ifdef __APPLE__
    // macOS: Get the main display's backing scale factor
    CGDirectDisplayID mainDisplay = CGMainDisplayID();
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(mainDisplay);
    if (mode) {
        size_t pixelWidth = CGDisplayModeGetPixelWidth(mode);
        size_t pointWidth = CGDisplayModeGetWidth(mode);
        CGDisplayModeRelease(mode);
        if (pointWidth > 0) {
            float dpr = static_cast<float>(pixelWidth) / static_cast<float>(pointWidth);
            // Clamp to reasonable values (1.0 to 3.0)
            if (dpr >= 1.0f && dpr <= 3.0f) {
                return dpr;
            }
        }
    }
    return 1.0f;  // Fallback for headless/no display
#else
    // Linux/Windows headless: No display, use 1.0
    return 1.0f;
#endif
}
} // namespace

OwlRenderTracker* OwlRenderTracker::instance_ = nullptr;

OwlRenderTracker* OwlRenderTracker::GetInstance() {
  if (!instance_) {
    instance_ = new OwlRenderTracker();
  }
  return instance_;
}

void OwlRenderTracker::UpdateElementPosition(const std::string& element_id, int x, int y, int width, int height) {
  std::lock_guard<std::mutex> lock(mutex_);
  // Implementation will be filled by renderer process
}

bool OwlRenderTracker::GetElementBounds(const std::string& context_id, const std::string& selector, ElementRenderInfo& info) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto context_it = element_map_.find(context_id);
  if (context_it == element_map_.end()) {
    LOG_ERROR("RenderTracker", "Context not found: " + context_id);
    return false;
  }

  // Parse the selector once and cache it
  SelectorComponent parsed = ParseSelector(selector);

  // Search through elements for matching selector
  for (const auto& entry : context_it->second) {
    const ElementRenderInfo& elem = entry.second;

    // Try fast paths first for common patterns

    // Fast path 1: Match by ID (most specific, fastest)
    if (!elem.id.empty() && !parsed.id.empty()) {
      if (elem.id == parsed.id) {
        // If tag is specified, verify it matches
        if (parsed.tag.empty() || parsed.tag == elem.tag) {
          info = elem;
          LOG_DEBUG("RenderTracker", "Found element by ID: " + elem.id +
                   " at (" + std::to_string(elem.x) + "," + std::to_string(elem.y) + ")");
          return true;
        }
      }
      continue;  // ID didn't match, skip other checks
    }

    // Fast path 2: Simple ID selector like "#my-element"
    if (!elem.id.empty() && selector.size() > 1 && selector[0] == '#') {
      if (selector.substr(1) == elem.id) {
        info = elem;
        LOG_DEBUG("RenderTracker", "Found element by ID selector: " + elem.id);
        return true;
      }
    }

    // Fast path 3: Simple tag name selector
    if (parsed.valid && !parsed.tag.empty() && parsed.id.empty() &&
        parsed.classes.empty() && parsed.attributes.empty()) {
      if (selector == elem.tag) {
        info = elem;
        return true;
      }
    }

    // Use parsed selector matching for complex selectors
    if (parsed.valid && ElementMatchesSelector(elem, parsed)) {
      info = elem;
      LOG_DEBUG("RenderTracker", "Found element by parsed selector: " + selector +
               " at (" + std::to_string(elem.x) + "," + std::to_string(elem.y) + ")");
      return true;
    }

    // Fallback: Exact match on stored selector (strip position info for comparison)
    std::string elem_selector_base = elem.selector;
    size_t elem_at_pos = elem_selector_base.find('@');
    if (elem_at_pos != std::string::npos) {
      elem_selector_base = elem_selector_base.substr(0, elem_at_pos);
    }

    std::string search_selector_base = selector;
    size_t search_at_pos = search_selector_base.find('@');
    if (search_at_pos != std::string::npos) {
      search_selector_base = search_selector_base.substr(0, search_at_pos);
    }

    if (elem_selector_base == search_selector_base || elem.selector == selector) {
      info = elem;
      return true;
    }
  }

  LOG_ERROR("RenderTracker", "Element not found: " + selector +
            " (tracked elements: " + std::to_string(context_it->second.size()) + ")");
  return false;
}

SelectorComponent OwlRenderTracker::ParseSelector(const std::string& selector) {
  // Check cache first
  {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = selector_cache_.find(selector);
    if (it != selector_cache_.end()) {
      return it->second;
    }
  }

  SelectorComponent result;
  result.valid = true;

  // Strip position metadata (@x,y) if present
  std::string cleanSelector = selector;
  size_t atPos = cleanSelector.find('@');
  if (atPos != std::string::npos) {
    cleanSelector = cleanSelector.substr(0, atPos);
  }

  if (cleanSelector.empty()) {
    result.valid = false;
    return result;
  }

  size_t pos = 0;
  size_t len = cleanSelector.length();

  // Parse tag name (must be at start if present)
  if (pos < len && cleanSelector[pos] != '#' && cleanSelector[pos] != '.' && cleanSelector[pos] != '[') {
    size_t tagEnd = cleanSelector.find_first_of("#.[", pos);
    if (tagEnd == std::string::npos) tagEnd = len;
    result.tag = cleanSelector.substr(pos, tagEnd - pos);
    // Convert tag to lowercase for comparison
    std::transform(result.tag.begin(), result.tag.end(), result.tag.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    pos = tagEnd;
  }

  // Parse remaining components
  while (pos < len) {
    char c = cleanSelector[pos];

    if (c == '#') {
      // ID selector
      pos++;
      size_t idEnd = cleanSelector.find_first_of("#.[", pos);
      if (idEnd == std::string::npos) idEnd = len;
      result.id = cleanSelector.substr(pos, idEnd - pos);
      pos = idEnd;
    }
    else if (c == '.') {
      // Class selector
      pos++;
      size_t classEnd = cleanSelector.find_first_of("#.[", pos);
      if (classEnd == std::string::npos) classEnd = len;
      result.classes.push_back(cleanSelector.substr(pos, classEnd - pos));
      pos = classEnd;
    }
    else if (c == '[') {
      // Attribute selector
      pos++;
      size_t bracketEnd = cleanSelector.find(']', pos);
      if (bracketEnd == std::string::npos) {
        result.valid = false;
        break;
      }

      std::string attrContent = cleanSelector.substr(pos, bracketEnd - pos);
      size_t eqPos = attrContent.find('=');

      if (eqPos != std::string::npos) {
        std::string attrName = attrContent.substr(0, eqPos);
        std::string attrValue = attrContent.substr(eqPos + 1);
        attrValue = ParseAttributeValue(attrValue);
        result.attributes.push_back({attrName, attrValue});
      } else {
        // Attribute without value (presence check)
        result.attributes.push_back({attrContent, ""});
      }

      pos = bracketEnd + 1;
    }
    else {
      // Unknown character, skip
      pos++;
    }
  }

  // Cache the result
  {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    // Limit cache size to prevent memory bloat
    if (selector_cache_.size() > 10000) {
      selector_cache_.clear();
    }
    selector_cache_[selector] = result;
  }

  return result;
}

std::string OwlRenderTracker::ParseAttributeValue(const std::string& raw) const {
  std::string value = raw;

  // Remove surrounding quotes
  if (value.size() >= 2) {
    char first = value.front();
    char last = value.back();
    if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
      value = value.substr(1, value.size() - 2);
    }
  }

  // Handle escaped quotes within the value
  std::string result;
  result.reserve(value.size());
  for (size_t i = 0; i < value.size(); i++) {
    if (value[i] == '\\' && i + 1 < value.size()) {
      char next = value[i + 1];
      if (next == '"' || next == '\'' || next == '\\') {
        result += next;
        i++;  // Skip the escaped character
        continue;
      }
    }
    result += value[i];
  }

  return result;
}

std::string OwlRenderTracker::GetElementAttribute(const ElementRenderInfo& elem, const std::string& attrName) const {
  // Map attribute names to ElementRenderInfo fields
  if (attrName == "aria-label") return elem.aria_label;
  if (attrName == "role") return elem.role;
  if (attrName == "name") return elem.name;
  if (attrName == "type") return elem.type;
  if (attrName == "placeholder") return elem.placeholder;
  if (attrName == "title") return elem.title;
  if (attrName == "id") return elem.id;
  if (attrName == "value") return elem.value;
  if (attrName == "class") return elem.className;
  if (attrName == "alt") return elem.alt;
  if (attrName == "src") return elem.src;

  // Check data-* attributes that might be stored in nearby_text
  // (This is a simplification - in reality we'd need to track these separately)
  if (attrName.substr(0, 5) == "data-") {
    // For now, check if the selector string contains this attribute
    std::string pattern = "[" + attrName + "=\"";
    size_t start = elem.selector.find(pattern);
    if (start != std::string::npos) {
      start += pattern.length();
      size_t end = elem.selector.find("\"", start);
      if (end != std::string::npos) {
        return elem.selector.substr(start, end - start);
      }
    }
  }

  return "";
}

bool OwlRenderTracker::ElementMatchesSelector(const ElementRenderInfo& elem, const SelectorComponent& parsed) {
  // Check tag match
  if (!parsed.tag.empty()) {
    std::string elemTag = elem.tag;
    std::transform(elemTag.begin(), elemTag.end(), elemTag.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (elemTag != parsed.tag) {
      return false;
    }
  }

  // Check ID match
  if (!parsed.id.empty() && elem.id != parsed.id) {
    return false;
  }

  // Check all classes
  for (const auto& cls : parsed.classes) {
    // Element's className is space-separated
    bool found = false;
    std::istringstream iss(elem.className);
    std::string token;
    while (iss >> token) {
      if (token == cls) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }

  // Check all attributes
  for (const auto& attr : parsed.attributes) {
    std::string elemValue = GetElementAttribute(elem, attr.first);

    if (attr.second.empty()) {
      // Presence check only - attribute must exist
      if (elemValue.empty()) {
        // Also check if it's in the selector string
        std::string pattern = "[" + attr.first + "]";
        if (elem.selector.find(pattern) == std::string::npos) {
          return false;
        }
      }
    } else {
      // Value must match
      if (elemValue != attr.second) {
        // Also try substring match for partial values
        if (elemValue.find(attr.second) == std::string::npos &&
            attr.second.find(elemValue) == std::string::npos) {
          return false;
        }
      }
    }
  }

  return true;
}

std::vector<ElementRenderInfo> OwlRenderTracker::GetAllVisibleElements(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<ElementRenderInfo> result;
  auto context_it = element_map_.find(context_id);
  if (context_it != element_map_.end()) {
    for (const auto& entry : context_it->second) {
      if (entry.second.visible) {
        result.push_back(entry.second);
      }
    }
  }
  return result;
}

std::vector<ElementRenderInfo> OwlRenderTracker::GetAllElements(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<ElementRenderInfo> result;
  auto context_it = element_map_.find(context_id);
  if (context_it != element_map_.end()) {
    for (const auto& entry : context_it->second) {
      result.push_back(entry.second);
    }
  }
  return result;
}

void OwlRenderTracker::ClearContext(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  element_map_.erase(context_id);
  LOG_DEBUG("RenderTracker", "Cleared context: " + context_id);
}

void OwlRenderTracker::RegisterElement(const std::string& context_id, const ElementRenderInfo& info) {
  std::lock_guard<std::mutex> lock(mutex_);

  std::string key = GenerateElementKey(info);
  element_map_[context_id][key] = info;

  LOG_DEBUG("RenderTracker", "Registered element: " + info.tag +
            (info.id.empty() ? "" : "#" + info.id) +
            " at (" + std::to_string(info.x) + "," + std::to_string(info.y) + ")" +
            " size: " + std::to_string(info.width) + "x" + std::to_string(info.height) +
            " selector: '" + info.selector + "'");
}

std::string OwlRenderTracker::GenerateElementKey(const ElementRenderInfo& info) {
  std::stringstream ss;
  ss << info.tag;
  if (!info.id.empty()) {
    ss << "#" << info.id;
  }
  if (!info.className.empty()) {
    ss << "." << info.className;
  }
  ss << "@" << info.x << "," << info.y;
  return ss.str();
}

// ============================================================================
// OlibRenderHandler Implementation
// ============================================================================

OlibRenderHandler::OlibRenderHandler(const std::string& context_id)
  : context_id_(context_id), view_width_(1920), view_height_(1080) {
  LOG_DEBUG("RenderHandler", "Created for context: " + context_id);
}

void OlibRenderHandler::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) {
  rect.x = 0;
  rect.y = 0;
  rect.width = view_width_;
  rect.height = view_height_;
}

void OlibRenderHandler::OnPaint(CefRefPtr<CefBrowser> browser,
                                PaintElementType type,
                                const RectList& dirtyRects,
                                const void* buffer,
                                int width,
                                int height) {
  // Called when page is painted - this is where we know rendering happened
  // We'll trigger element position updates from renderer process via IPC messages
  LOG_DEBUG("RenderHandler", "OnPaint: " + std::to_string(width) + "x" + std::to_string(height) +
            " dirty rects: " + std::to_string(dirtyRects.size()));
}

void OlibRenderHandler::OnAcceleratedPaint(CefRefPtr<CefBrowser> browser,
                                           PaintElementType type,
                                           const RectList& dirtyRects,
                                           const CefAcceleratedPaintInfo& info) {
  LOG_DEBUG("RenderHandler", "OnAcceleratedPaint");
}

bool OlibRenderHandler::GetScreenInfo(CefRefPtr<CefBrowser> browser, CefScreenInfo& screen_info) {
  // CRITICAL: Use actual system DPR for correct font rendering
  // This ensures fontPreferences measurements are correct regardless of host display
  // - On Retina Mac: Returns 2.0 so fonts render at proper scale
  // - On headless Ubuntu: Returns 1.0 (no display)
  // The JavaScript spoofing in owl_app.cc will still override window.devicePixelRatio
  // but CEF needs the real value for internal rendering calculations
  screen_info.device_scale_factor = GetActualSystemDPR();
  screen_info.depth = 24;
  screen_info.depth_per_component = 8;
  screen_info.is_monochrome = false;

  screen_info.rect.x = 0;
  screen_info.rect.y = 0;
  screen_info.rect.width = view_width_;
  screen_info.rect.height = view_height_;

  screen_info.available_rect = screen_info.rect;

  return true;
}
