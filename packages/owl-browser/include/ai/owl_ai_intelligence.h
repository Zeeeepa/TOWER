#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include "include/cef_browser.h"
#include "include/cef_frame.h"
#include "owl_llm_client.h"  // For LLM client parameter

// AI-First intelligent DOM interaction
// No more querySelector hell - the AI speaks, we execute
struct ElementInfo {
  std::string selector;
  std::string tag_name;
  std::string text_content;
  std::string value;
  std::string href;
  std::string src;
  std::string type;
  std::string placeholder;
  std::string aria_label;
  std::string role;
  bool is_visible;
  bool is_clickable;
  bool is_input;
  int x, y, width, height;
};

struct PageIntelligence {
  std::string title;
  std::string main_content;  // Extracted main content (readability)
  std::vector<ElementInfo> clickable_elements;  // All buttons, links
  std::vector<ElementInfo> input_elements;      // All inputs, textareas
  std::vector<ElementInfo> headings;            // All h1-h6
  std::vector<std::string> images;              // All image URLs
  std::string page_structure;                   // JSON representation
  bool has_forms;
  bool has_login_form;
  int total_elements;
};

class OwlAIIntelligence {
public:
  // Smart element finding - AI describes what it wants, we find it
  static std::vector<ElementInfo> FindByDescription(
      CefRefPtr<CefFrame> frame,
      const std::string& description);  // "submit button", "email input", etc.

  // Extract complete page intelligence for AI
  static PageIntelligence AnalyzePage(CefRefPtr<CefFrame> frame);

  // Smart interaction - AI-friendly
  static bool ClickElement(CefRefPtr<CefFrame> frame, const std::string& description);
  static bool ClickAtCoordinates(CefRefPtr<CefFrame> frame, int x, int y);
  static bool TypeIntoElement(CefRefPtr<CefFrame> frame,
                              const std::string& description,
                              const std::string& text);
  static std::string ExtractContent(CefRefPtr<CefFrame> frame, const std::string& description);

  // Extract visible text (clean, AI-ready)
  static std::string GetVisibleText(CefRefPtr<CefFrame> frame);

  // Get main content using readability algorithm
  static std::string GetMainContent(CefRefPtr<CefFrame> frame);

  // Find all interactive elements
  static std::vector<ElementInfo> GetInteractiveElements(CefRefPtr<CefFrame> frame);

  // Smart waiting - wait for something to appear/change
  static bool WaitForCondition(CefRefPtr<CefFrame> frame,
                               const std::string& condition,
                               int timeout_ms);

  // Execute any arbitrary query and return structured data
  // llm_client: Optional LLM client to use (nullptr = use global client)
  static std::string QueryPage(CefRefPtr<CefFrame> frame, const std::string& query, OwlLLMClient* llm_client = nullptr);

  // Smart page summarization (NEW!)
  // Creates an intelligent, structured summary of the page using LLM
  // Caches results per URL for fast repeat queries
  // llm_client: Optional LLM client to use (nullptr = use global client)
  static std::string SummarizePage(CefRefPtr<CefFrame> frame, bool force_refresh = false, OwlLLMClient* llm_client = nullptr);

  // Get cached summary or empty string if not available
  static std::string GetCachedSummary(const std::string& url);

  // Clear summary cache (useful for dynamic pages)
  static void ClearSummaryCache(const std::string& url = "");

private:
  // Helper to inject intelligence scripts
  static void InjectIntelligenceScripts(CefRefPtr<CefFrame> frame);

  // Helper to parse element from JavaScript result
  static ElementInfo ParseElementInfo(const std::string& json);

  // Summary cache: URL -> summary
  static std::map<std::string, std::string> summary_cache_;
  static std::mutex cache_mutex_;
};
