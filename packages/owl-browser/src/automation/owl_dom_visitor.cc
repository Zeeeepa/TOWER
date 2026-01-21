#include "owl_dom_visitor.h"
#include "logger.h"
#include "include/wrapper/cef_helpers.h"

// Static member initialization
std::unordered_map<std::string, CachedBounds> OlibDOMVisitor::bounds_cache_;
std::mutex OlibDOMVisitor::cache_mutex_;
bool OlibDOMVisitor::cache_enabled_ = true;
int OlibDOMVisitor::cache_ttl_ms_ = 100;  // Default 100ms TTL for bounds cache
std::string OlibDOMVisitor::current_url_;

// Single element constructor
OlibDOMVisitor::OlibDOMVisitor(const std::string& selector, BoundsCallback callback)
  : selector_(selector), callback_(callback), is_batch_(false) {
}

// Batch element constructor
OlibDOMVisitor::OlibDOMVisitor(const std::vector<std::string>& selectors, BatchCallback callback)
  : selectors_(selectors), batch_callback_(callback), is_batch_(true) {
}

void OlibDOMVisitor::Visit(CefRefPtr<CefDOMDocument> document) {
  CEF_REQUIRE_RENDERER_THREAD();

  if (!document) {
    if (is_batch_) {
      BatchBoundsResult result;
      result.found_count = 0;
      result.total_count = static_cast<int>(selectors_.size());
      result.duration_ms = 0;
      for (const auto& sel : selectors_) {
        ElementBounds bounds;
        bounds.found = false;
        bounds.selector = sel;
        bounds.error = "No document";
        result.results.push_back(bounds);
      }
      batch_callback_(result);
    } else {
      ElementBounds bounds;
      bounds.found = false;
      bounds.selector = selector_;
      bounds.error = "No document";
      callback_(bounds);
    }
    return;
  }

  CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
  if (!context || !context->Enter()) {
    if (is_batch_) {
      BatchBoundsResult result;
      result.found_count = 0;
      result.total_count = static_cast<int>(selectors_.size());
      result.duration_ms = 0;
      for (const auto& sel : selectors_) {
        ElementBounds bounds;
        bounds.found = false;
        bounds.selector = sel;
        bounds.error = "No V8 context";
        result.results.push_back(bounds);
      }
      batch_callback_(result);
    } else {
      ElementBounds bounds;
      bounds.found = false;
      bounds.selector = selector_;
      bounds.error = "No V8 context";
      callback_(bounds);
    }
    return;
  }

  if (is_batch_) {
    // Batch lookup
    BatchBoundsResult result = GetBoundsForSelectors(context, selectors_);
    context->Exit();
    batch_callback_(result);
  } else {
    // Single lookup
    LOG_DEBUG("DOMVisitor", "Searching for element: " + selector_);
    ElementBounds bounds = GetBoundsForSelector(context, selector_);
    context->Exit();
    callback_(bounds);
  }
}

ElementBounds OlibDOMVisitor::GetBoundsForSelector(CefRefPtr<CefV8Context> context,
                                                    const std::string& selector) {
  ElementBounds bounds;
  bounds.found = false;
  bounds.selector = selector;
  bounds.x = 0;
  bounds.y = 0;
  bounds.width = 0;
  bounds.height = 0;

  // Check cache first
  if (cache_enabled_) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = bounds_cache_.find(GetCacheKey(selector));
    if (it != bounds_cache_.end() && IsCacheValid(it->second)) {
      LOG_DEBUG("DOMVisitor", "Cache hit for: " + selector);
      return it->second.bounds;
    }
  }

  CefRefPtr<CefV8Value> global = context->GetGlobal();
  CefRefPtr<CefV8Value> doc = global->GetValue("document");

  if (!doc || !doc->IsObject()) {
    bounds.error = "No document object";
    return bounds;
  }

  CefRefPtr<CefV8Value> querySelector = doc->GetValue("querySelector");
  if (!querySelector || !querySelector->IsFunction()) {
    bounds.error = "querySelector not available";
    return bounds;
  }

  CefV8ValueList args;
  args.push_back(CefV8Value::CreateString(selector));

  CefRefPtr<CefV8Value> element = querySelector->ExecuteFunctionWithContext(context, doc, args);

  if (!element || element->IsNull() || element->IsUndefined()) {
    bounds.error = "Element not found: " + selector;
    LOG_DEBUG("DOMVisitor", bounds.error);
    return bounds;
  }

  CefRefPtr<CefV8Value> getBoundingClientRect = element->GetValue("getBoundingClientRect");
  if (!getBoundingClientRect || !getBoundingClientRect->IsFunction()) {
    bounds.error = "getBoundingClientRect not available";
    return bounds;
  }

  CefV8ValueList rectArgs;
  CefRefPtr<CefV8Value> rect = getBoundingClientRect->ExecuteFunctionWithContext(context, element, rectArgs);

  if (!rect || !rect->IsObject()) {
    bounds.error = "Failed to get bounding rect";
    return bounds;
  }

  // Extract coordinates - center point
  CefRefPtr<CefV8Value> left = rect->GetValue("left");
  CefRefPtr<CefV8Value> top = rect->GetValue("top");
  CefRefPtr<CefV8Value> width = rect->GetValue("width");
  CefRefPtr<CefV8Value> height = rect->GetValue("height");

  if (left && top && width && height) {
    double w = width->GetDoubleValue();
    double h = height->GetDoubleValue();
    bounds.found = true;
    bounds.x = static_cast<int>(left->GetDoubleValue() + w / 2.0);
    bounds.y = static_cast<int>(top->GetDoubleValue() + h / 2.0);
    bounds.width = static_cast<int>(w);
    bounds.height = static_cast<int>(h);
  }

  LOG_DEBUG("DOMVisitor", "Element bounds: selector=" + selector +
           " x=" + std::to_string(bounds.x) +
           " y=" + std::to_string(bounds.y) +
           " w=" + std::to_string(bounds.width) +
           " h=" + std::to_string(bounds.height));

  // Update cache
  if (cache_enabled_ && bounds.found) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    CachedBounds cached;
    cached.bounds = bounds;
    cached.timestamp = std::chrono::steady_clock::now();
    bounds_cache_[GetCacheKey(selector)] = cached;
  }

  return bounds;
}

BatchBoundsResult OlibDOMVisitor::GetBoundsForSelectors(CefRefPtr<CefV8Context> context,
                                                         const std::vector<std::string>& selectors) {
  BatchBoundsResult result;
  result.found_count = 0;
  result.total_count = static_cast<int>(selectors.size());

  auto start_time = std::chrono::steady_clock::now();

  // Get document and querySelector once for batch
  CefRefPtr<CefV8Value> global = context->GetGlobal();
  CefRefPtr<CefV8Value> doc = global->GetValue("document");

  if (!doc || !doc->IsObject()) {
    for (const auto& sel : selectors) {
      ElementBounds bounds;
      bounds.found = false;
      bounds.selector = sel;
      bounds.error = "No document object";
      result.results.push_back(bounds);
    }
    result.duration_ms = 0;
    return result;
  }

  CefRefPtr<CefV8Value> querySelector = doc->GetValue("querySelector");
  if (!querySelector || !querySelector->IsFunction()) {
    for (const auto& sel : selectors) {
      ElementBounds bounds;
      bounds.found = false;
      bounds.selector = sel;
      bounds.error = "querySelector not available";
      result.results.push_back(bounds);
    }
    result.duration_ms = 0;
    return result;
  }

  // Process each selector
  for (const auto& selector : selectors) {
    ElementBounds bounds;
    bounds.found = false;
    bounds.selector = selector;
    bounds.x = 0;
    bounds.y = 0;
    bounds.width = 0;
    bounds.height = 0;

    // Check cache first
    if (cache_enabled_) {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      auto it = bounds_cache_.find(GetCacheKey(selector));
      if (it != bounds_cache_.end() && IsCacheValid(it->second)) {
        result.results.push_back(it->second.bounds);
        if (it->second.bounds.found) result.found_count++;
        continue;
      }
    }

    // Query the element
    CefV8ValueList args;
    args.push_back(CefV8Value::CreateString(selector));
    CefRefPtr<CefV8Value> element = querySelector->ExecuteFunctionWithContext(context, doc, args);

    if (!element || element->IsNull() || element->IsUndefined()) {
      bounds.error = "Element not found";
      result.results.push_back(bounds);
      continue;
    }

    CefRefPtr<CefV8Value> getBoundingClientRect = element->GetValue("getBoundingClientRect");
    if (!getBoundingClientRect || !getBoundingClientRect->IsFunction()) {
      bounds.error = "getBoundingClientRect not available";
      result.results.push_back(bounds);
      continue;
    }

    CefV8ValueList rectArgs;
    CefRefPtr<CefV8Value> rect = getBoundingClientRect->ExecuteFunctionWithContext(context, element, rectArgs);

    if (!rect || !rect->IsObject()) {
      bounds.error = "Failed to get bounding rect";
      result.results.push_back(bounds);
      continue;
    }

    CefRefPtr<CefV8Value> left = rect->GetValue("left");
    CefRefPtr<CefV8Value> top = rect->GetValue("top");
    CefRefPtr<CefV8Value> width = rect->GetValue("width");
    CefRefPtr<CefV8Value> height = rect->GetValue("height");

    if (left && top && width && height) {
      double w = width->GetDoubleValue();
      double h = height->GetDoubleValue();
      bounds.found = true;
      bounds.x = static_cast<int>(left->GetDoubleValue() + w / 2.0);
      bounds.y = static_cast<int>(top->GetDoubleValue() + h / 2.0);
      bounds.width = static_cast<int>(w);
      bounds.height = static_cast<int>(h);
      result.found_count++;

      // Update cache
      if (cache_enabled_) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        CachedBounds cached;
        cached.bounds = bounds;
        cached.timestamp = std::chrono::steady_clock::now();
        bounds_cache_[GetCacheKey(selector)] = cached;
      }
    }

    result.results.push_back(bounds);
  }

  auto end_time = std::chrono::steady_clock::now();
  result.duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

  LOG_DEBUG("DOMVisitor", "Batch lookup complete: " + std::to_string(result.found_count) + "/" +
           std::to_string(result.total_count) + " found in " +
           std::to_string(result.duration_ms) + "ms");

  return result;
}

// Cache management
void OlibDOMVisitor::SetCacheEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  cache_enabled_ = enabled;
  if (!enabled) {
    bounds_cache_.clear();
  }
}

void OlibDOMVisitor::SetCacheTTL(int milliseconds) {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  cache_ttl_ms_ = milliseconds;
}

void OlibDOMVisitor::ClearCache() {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  bounds_cache_.clear();
}

void OlibDOMVisitor::InvalidateCache(const std::string& url) {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  if (url != current_url_) {
    bounds_cache_.clear();
    current_url_ = url;
  }
}

bool OlibDOMVisitor::IsCacheValid(const CachedBounds& cached) {
  auto now = std::chrono::steady_clock::now();
  auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - cached.timestamp).count();
  return age < cache_ttl_ms_;
}

std::string OlibDOMVisitor::GetCacheKey(const std::string& selector) {
  return current_url_ + "::" + selector;
}

CefRefPtr<CefDOMNode> OlibDOMVisitor::FindElementBySelector(CefRefPtr<CefDOMDocument> document, const std::string& selector) {
  // This would require implementing a CSS selector engine in C++
  // For now, we use V8 in Visit() method instead
  return nullptr;
}

ElementBounds OlibDOMVisitor::GetElementBounds(CefRefPtr<CefDOMNode> node) {
  // Not used - we use V8 getBoundingClientRect instead
  ElementBounds bounds;
  bounds.found = false;
  return bounds;
}
