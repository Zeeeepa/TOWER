#include "owl_markdown_converter.h"
#include "logger.h"
#include <regex>
#include <sstream>
#include <algorithm>

std::string OwlMarkdownConverter::StripHTML(const std::string& html) {
  std::regex tag_regex("<[^>]+>");
  return std::regex_replace(html, tag_regex, "");
}

std::string OwlMarkdownConverter::CleanWhitespace(const std::string& text) {
  // Replace multiple spaces with single space
  std::regex multi_space(R"(\s+)");
  std::string result = std::regex_replace(text, multi_space, " ");

  // Trim leading/trailing whitespace
  size_t start = result.find_first_not_of(" \t\n\r");
  if (start == std::string::npos) return "";

  size_t end = result.find_last_not_of(" \t\n\r");
  return result.substr(start, end - start + 1);
}

std::string OwlMarkdownConverter::EscapeMarkdown(const std::string& text) {
  std::string result = text;
  // Escape special Markdown characters
  std::vector<std::pair<std::string, std::string>> replacements = {
    {"\\", "\\\\"},
    {"*", "\\*"},
    {"_", "\\_"},
    {"[", "\\["},
    {"]", "\\]"},
    {"`", "\\`"},
  };

  for (const auto& [from, to] : replacements) {
    size_t pos = 0;
    while ((pos = result.find(from, pos)) != std::string::npos) {
      result.replace(pos, from.length(), to);
      pos += to.length();
    }
  }

  return result;
}

std::string OwlMarkdownConverter::DecodeHTMLEntities(const std::string& text) {
  std::string result = text;

  // Common HTML entities
  std::vector<std::pair<std::string, std::string>> entities = {
    {"&nbsp;", " "},
    {"&lt;", "<"},
    {"&gt;", ">"},
    {"&amp;", "&"},
    {"&quot;", "\""},
    {"&apos;", "'"},
    {"&#39;", "'"},
    {"&mdash;", "—"},
    {"&ndash;", "–"},
    {"&hellip;", "..."},
  };

  for (const auto& [entity, replacement] : entities) {
    size_t pos = 0;
    while ((pos = result.find(entity, pos)) != std::string::npos) {
      result.replace(pos, entity.length(), replacement);
      pos += replacement.length();
    }
  }

  return result;
}

std::string OwlMarkdownConverter::ConvertHeadings(const std::string& html) {
  std::string result = html;

  // Convert h1-h6 tags
  for (int level = 1; level <= 6; level++) {
    std::string tag = "h" + std::to_string(level);
    std::string pattern = "<" + tag + R"(\b[^>]*>)(.*?)<\/)" + tag + ">";
    std::regex heading_regex(pattern, std::regex::icase);

    std::string prefix(level, '#');
    std::string replacement = "\n\n" + prefix + " $1\n\n";
    result = std::regex_replace(result, heading_regex, replacement);
  }

  return result;
}

std::string OwlMarkdownConverter::ConvertParagraphs(const std::string& html) {
  std::string result = html;

  // Convert <p> tags
  std::regex p_regex(R"(<p\b[^>]*>(.*?)<\/p>)", std::regex::icase);
  result = std::regex_replace(result, p_regex, "\n\n$1\n\n");

  // Convert <br> tags
  std::regex br_regex(R"(<br\s*\/?>)", std::regex::icase);
  result = std::regex_replace(result, br_regex, "\n");

  return result;
}

std::string OwlMarkdownConverter::ConvertLists(const std::string& html) {
  std::string result = html;

  // Convert unordered list items
  std::regex ul_regex(R"(<ul\b[^>]*>(.*?)<\/ul>)", std::regex::icase);
  std::regex li_regex(R"(<li\b[^>]*>(.*?)<\/li>)", std::regex::icase);

  // Process nested lists
  result = std::regex_replace(result, li_regex, "- $1\n");
  result = std::regex_replace(result, ul_regex, "\n$1\n");

  // Convert ordered list items (simplified)
  std::regex ol_regex(R"(<ol\b[^>]*>(.*?)<\/ol>)", std::regex::icase);
  result = std::regex_replace(result, ol_regex, "\n$1\n");

  return result;
}

std::string OwlMarkdownConverter::ConvertLinks(const std::string& html) {
  std::string result = html;

  // Convert <a> tags to [text](url)
  std::regex link_regex(R"(<a\b[^>]*href\s*=\s*["\']([^"\']+)["\'][^>]*>(.*?)<\/a>)",
                       std::regex::icase);

  result = std::regex_replace(result, link_regex, "[$2]($1)");

  return result;
}

std::string OwlMarkdownConverter::ConvertImages(const std::string& html) {
  std::string result = html;

  // Convert <img> tags to ![alt](src)
  std::regex img_regex(R"(<img\b[^>]*src\s*=\s*["\']([^"\']+)["\'][^>]*alt\s*=\s*["\']([^"\']*)["\'][^>]*\/?>)",
                      std::regex::icase);

  result = std::regex_replace(result, img_regex, "![$2]($1)");

  // Handle img tags without alt
  std::regex img_no_alt_regex(R"(<img\b[^>]*src\s*=\s*["\']([^"\']+)["\'][^>]*\/?>)",
                             std::regex::icase);
  result = std::regex_replace(result, img_no_alt_regex, "![image]($1)");

  return result;
}

std::string OwlMarkdownConverter::ConvertCodeBlocks(const std::string& html) {
  std::string result = html;

  // Convert <pre><code> blocks
  std::regex code_block_regex(R"(<pre\b[^>]*><code\b[^>]*>(.*?)<\/code><\/pre>)",
                             std::regex::icase);
  result = std::regex_replace(result, code_block_regex, "\n```\n$1\n```\n");

  // Convert inline <code> tags
  std::regex inline_code_regex(R"(<code\b[^>]*>(.*?)<\/code>)", std::regex::icase);
  result = std::regex_replace(result, inline_code_regex, "`$1`");

  return result;
}

std::string OwlMarkdownConverter::ConvertBlockquotes(const std::string& html) {
  std::string result = html;

  // Convert <blockquote> tags
  std::regex blockquote_regex(R"(<blockquote\b[^>]*>(.*?)<\/blockquote>)",
                             std::regex::icase);
  result = std::regex_replace(result, blockquote_regex, "\n> $1\n");

  return result;
}

std::string OwlMarkdownConverter::ConvertInlineFormatting(const std::string& html) {
  std::string result = html;

  // Convert <strong> and <b> to **bold**
  std::regex strong_regex(R"(<(strong|b)\b[^>]*>(.*?)<\/\1>)", std::regex::icase);
  result = std::regex_replace(result, strong_regex, "**$2**");

  // Convert <em> and <i> to *italic*
  std::regex em_regex(R"(<(em|i)\b[^>]*>(.*?)<\/\1>)", std::regex::icase);
  result = std::regex_replace(result, em_regex, "*$2*");

  return result;
}

size_t OwlMarkdownConverter::FindClosingTag(const std::string& html,
                                             const std::string& tag,
                                             size_t start) {
  std::string opening = "<" + tag;
  std::string closing = "</" + tag + ">";

  int depth = 1;
  size_t pos = start;

  while (depth > 0 && pos < html.length()) {
    size_t next_open = html.find(opening, pos);
    size_t next_close = html.find(closing, pos);

    if (next_close == std::string::npos) {
      return std::string::npos;
    }

    if (next_open < next_close) {
      depth++;
      pos = next_open + opening.length();
    } else {
      depth--;
      pos = next_close + closing.length();
    }
  }

  return pos;
}

std::string OwlMarkdownConverter::ConvertTables(const std::string& html) {
  // Basic table conversion (simplified)
  std::string result = html;

  // Remove table tags but keep content for now
  // Full table conversion is complex and would need proper HTML parsing
  std::regex table_regex(R"(<table\b[^>]*>(.*?)<\/table>)", std::regex::icase);
  result = std::regex_replace(result, table_regex, "\n$1\n");

  return result;
}

std::string OwlMarkdownConverter::Convert(const std::string& html,
                                          bool include_links,
                                          bool include_images) {
  LOG_DEBUG("MarkdownConverter", "Converting HTML to Markdown");

  std::string result = html;

  // Step 1: Convert block-level elements
  result = ConvertHeadings(result);
  result = ConvertCodeBlocks(result);
  result = ConvertBlockquotes(result);
  result = ConvertTables(result);
  result = ConvertLists(result);
  result = ConvertParagraphs(result);

  // Step 2: Convert inline elements
  result = ConvertInlineFormatting(result);

  if (include_links) {
    result = ConvertLinks(result);
  }

  if (include_images) {
    result = ConvertImages(result);
  }

  // Step 3: Clean up remaining HTML tags
  result = StripHTML(result);

  // Step 4: Decode HTML entities
  result = DecodeHTMLEntities(result);

  // Step 5: Clean up whitespace
  // Remove multiple consecutive blank lines
  std::regex multi_newline(R"(\n\n\n+)");
  result = std::regex_replace(result, multi_newline, "\n\n");

  // Trim
  result = CleanWhitespace(result);

  LOG_DEBUG("MarkdownConverter", "Markdown conversion complete (" +
           std::to_string(result.length()) + " chars)");

  return result;
}

std::vector<std::vector<std::string>> OwlMarkdownConverter::ParseTable(const std::string& html) {
  // TODO: Implement proper table parsing
  return {};
}

std::string OwlMarkdownConverter::FormatMarkdownTable(
    const std::vector<std::vector<std::string>>& table) {
  // TODO: Implement Markdown table formatting
  return "";
}
