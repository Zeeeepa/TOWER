#pragma once

#include <string>
#include <vector>
#include <map>
#include "include/cef_browser.h"
#include "include/cef_frame.h"

// Structured content extraction system
// Built BY AI FOR AI - extract clean, structured data from any website

// Cleaning levels
enum class CleanLevel {
  MINIMAL,     // Remove only scripts/styles
  BASIC,       // + Remove ads, trackers, navigation
  AGGRESSIVE   // + Remove all non-content elements
};

// Extraction options for HTML
struct HTMLExtractionOptions {
  CleanLevel clean_level = CleanLevel::BASIC;
  bool include_metadata = true;           // Keep meta tags
  bool convert_relative_urls = true;      // Relative → absolute
  bool preserve_structure = true;         // Keep semantic HTML
  bool remove_hidden_elements = true;     // Remove display:none
};

// Extraction options for Markdown
struct MarkdownExtractionOptions {
  bool include_links = true;
  bool include_images = true;
  bool include_code_blocks = true;
  bool include_tables = true;
  int max_length = -1;                    // -1 = no limit
  bool preserve_formatting = true;
};

class OwlContentExtractor {
public:
  // Extract clean HTML
  static std::string ExtractHTML(
      CefRefPtr<CefFrame> frame,
      const HTMLExtractionOptions& options = HTMLExtractionOptions());

  // Extract as Markdown
  static std::string ExtractMarkdown(
      CefRefPtr<CefFrame> frame,
      const MarkdownExtractionOptions& options = MarkdownExtractionOptions());

  // Extract structured JSON (generic)
  static std::string ExtractJSON(
      CefRefPtr<CefFrame> frame,
      const std::string& schema = "");

  // Extract with template
  static std::string ExtractWithTemplate(
      CefRefPtr<CefFrame> frame,
      const std::string& template_name);

  // Detect website type for template matching
  static std::string DetectWebsiteType(CefRefPtr<CefFrame> frame);

  // List available templates
  static std::vector<std::string> ListAvailableTemplates();

private:
  // HTML cleaning helpers
  static std::string RemoveScriptsAndStyles(const std::string& html);
  static std::string RemoveAdsAndTrackers(const std::string& html);
  static std::string RemoveNavigationElements(const std::string& html);
  static std::string RemoveHiddenElements(const std::string& html);
  static std::string ConvertRelativeURLs(const std::string& html, const std::string& base_url);
  static std::string ExtractMainContent(const std::string& html);

  // Get page HTML via JavaScript
  static std::string GetPageHTML(CefRefPtr<CefFrame> frame);
  static std::string GetPageURL(CefRefPtr<CefFrame> frame);
};
