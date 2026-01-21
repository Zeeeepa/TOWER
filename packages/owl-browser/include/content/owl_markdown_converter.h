#pragma once

#include <string>
#include <vector>
#include <map>

// HTML → Markdown conversion
// Fast, lightweight, no external dependencies
// Built BY AI FOR AI

struct HTMLElement {
  std::string tag;
  std::string content;
  std::map<std::string, std::string> attributes;
  std::vector<HTMLElement> children;
};

class OwlMarkdownConverter {
public:
  // Convert HTML string to Markdown
  static std::string Convert(const std::string& html,
                            bool include_links = true,
                            bool include_images = true);

  // Convert specific HTML elements
  static std::string ConvertHeadings(const std::string& html);
  static std::string ConvertParagraphs(const std::string& html);
  static std::string ConvertLists(const std::string& html);
  static std::string ConvertLinks(const std::string& html);
  static std::string ConvertImages(const std::string& html);
  static std::string ConvertCodeBlocks(const std::string& html);
  static std::string ConvertTables(const std::string& html);
  static std::string ConvertBlockquotes(const std::string& html);
  static std::string ConvertInlineFormatting(const std::string& html);

private:
  // Helpers
  static std::string ProcessNode(const std::string& html, size_t& pos);
  static std::string GetTagName(const std::string& html, size_t pos);
  static std::string GetTagContent(const std::string& html, size_t start, size_t end);
  static std::string StripHTML(const std::string& html);
  static std::string CleanWhitespace(const std::string& text);
  static std::string EscapeMarkdown(const std::string& text);
  static std::string DecodeHTMLEntities(const std::string& text);

  // Find matching closing tag
  static size_t FindClosingTag(const std::string& html, const std::string& tag, size_t start);

  // Table conversion helpers
  static std::vector<std::vector<std::string>> ParseTable(const std::string& html);
  static std::string FormatMarkdownTable(const std::vector<std::vector<std::string>>& table);
};
