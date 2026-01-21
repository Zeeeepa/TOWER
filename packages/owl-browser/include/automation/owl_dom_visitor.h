#pragma once

#include "include/cef_dom.h"
#include "include/cef_v8.h"
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>

// Struct to hold element position info
struct ElementBounds {
  bool found;
  int x;
  int y;
  int width;
  int height;
  std::string error;
  std::string selector;  // The selector used to find this element
};

// Batch result for multiple bounds lookups
struct BatchBoundsResult {
  std::vector<ElementBounds> results;
  int found_count;
  int total_count;
  double duration_ms;  // Time taken for batch lookup
};

// Cached bounds with timestamp
struct CachedBounds {
  ElementBounds bounds;
  std::chrono::steady_clock::time_point timestamp;
};

// DOM visitor that finds an element by CSS selector and gets its bounds
class OlibDOMVisitor : public CefDOMVisitor {
public:
  using BoundsCallback = std::function<void(const ElementBounds&)>;
  using BatchCallback = std::function<void(const BatchBoundsResult&)>;

  // Single element lookup
  OlibDOMVisitor(const std::string& selector, BoundsCallback callback);

  // Batch element lookup - get bounds for multiple selectors at once
  OlibDOMVisitor(const std::vector<std::string>& selectors, BatchCallback callback);

  void Visit(CefRefPtr<CefDOMDocument> document) override;

  // Static utility methods for bounds lookup without visitor pattern
  static ElementBounds GetBoundsForSelector(CefRefPtr<CefV8Context> context,
                                             const std::string& selector);

  static BatchBoundsResult GetBoundsForSelectors(CefRefPtr<CefV8Context> context,
                                                  const std::vector<std::string>& selectors);

  // Cache management
  static void SetCacheEnabled(bool enabled);
  static void SetCacheTTL(int milliseconds);
  static void ClearCache();
  static void InvalidateCache(const std::string& url);

private:
  std::string selector_;
  std::vector<std::string> selectors_;
  BoundsCallback callback_;
  BatchCallback batch_callback_;
  bool is_batch_;

  // Cache for bounds lookups (shared across instances)
  static std::unordered_map<std::string, CachedBounds> bounds_cache_;
  static std::mutex cache_mutex_;
  static bool cache_enabled_;
  static int cache_ttl_ms_;
  static std::string current_url_;

  // Helper to find element matching selector
  CefRefPtr<CefDOMNode> FindElementBySelector(CefRefPtr<CefDOMDocument> document, const std::string& selector);

  // Helper to get element bounds using CEF V8 (getBoundingClientRect)
  ElementBounds GetElementBounds(CefRefPtr<CefDOMNode> node);

  // Check if cached bounds are still valid
  static bool IsCacheValid(const CachedBounds& cached);

  // Generate cache key
  static std::string GetCacheKey(const std::string& selector);

  IMPLEMENT_REFCOUNTING(OlibDOMVisitor);
};
