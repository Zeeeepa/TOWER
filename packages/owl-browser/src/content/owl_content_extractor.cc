#include "owl_content_extractor.h"
#include "owl_markdown_converter.h"
#include "owl_extraction_template.h"
#include "owl_json_extractor.h"
#include "owl_ai_intelligence.h"
#include "owl_client.h"
#include "logger.h"
#include <sstream>
#include <iomanip>
#include <regex>
#include <thread>
#include <chrono>

// Get page HTML using JavaScript
std::string OwlContentExtractor::GetPageHTML(CefRefPtr<CefFrame> frame) {
  // Create a hidden element to store the result
  std::string script = R"(
    (function() {
      try {
        const el = document.getElementById('__owl_html_result') || document.createElement('div');
        el.id = '__owl_html_result';
        el.textContent = document.documentElement.outerHTML;
        el.style.display = 'none';
        if (!el.parentNode) document.body.appendChild(el);
        return true;
      } catch(e) {
        console.error('GetPageHTML error:', e);
        return false;
      }
    })();
  )";

  frame->ExecuteJavaScript(script, frame->GetURL(), 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // For now, return empty - proper implementation needs CefStringVisitor
  // This is a synchronous workaround limitation
  LOG_DEBUG("ContentExtractor", "HTML extraction requested");
  return "";  // TODO: Implement async result handling
}

std::string OwlContentExtractor::GetPageURL(CefRefPtr<CefFrame> frame) {
  return frame->GetURL().ToString();
}

std::string OwlContentExtractor::RemoveScriptsAndStyles(const std::string& html) {
  std::string result = html;

  // Remove <script> tags and content
  std::regex script_regex(R"(<script\b[^>]*>.*?<\/script>)", std::regex::icase);
  result = std::regex_replace(result, script_regex, "");

  // Remove <style> tags and content
  std::regex style_regex(R"(<style\b[^>]*>.*?<\/style>)", std::regex::icase);
  result = std::regex_replace(result, style_regex, "");

  // Remove <noscript> tags
  std::regex noscript_regex(R"(<noscript\b[^>]*>.*?<\/noscript>)", std::regex::icase);
  result = std::regex_replace(result, noscript_regex, "");

  // Remove inline event handlers
  std::regex onclick_regex(R"(\s+on\w+\s*=\s*["\'][^"\']*["\']))", std::regex::icase);
  result = std::regex_replace(result, onclick_regex, "");

  return result;
}

std::string OwlContentExtractor::RemoveAdsAndTrackers(const std::string& html) {
  std::string result = html;

  // Common ad/tracker class names and IDs
  std::vector<std::string> patterns = {
    // Ad containers
    R"(<[^>]*\s+class\s*=\s*["\'][^"\']*\b(ad|ads|advertisement|advert|banner|sponsored|promo|dfp|doubleclick)[^"\']*["\'][^>]*>.*?<\/[^>]+>)",
    R"(<[^>]*\s+id\s*=\s*["\'][^"\']*\b(ad|ads|advertisement|banner|sponsored)[^"\']*["\'][^>]*>.*?<\/[^>]+>)",

    // Navigation/menus
    R"(<nav\b[^>]*>.*?<\/nav>)",
    R"(<[^>]*\s+role\s*=\s*["\']navigation["\'][^>]*>.*?<\/[^>]+>)",

    // Footers (often contain ads)
    R"(<footer\b[^>]*>.*?<\/footer>)",

    // Sidebars (often contain ads)
    R"(<aside\b[^>]*>.*?<\/aside>)",
    R"(<[^>]*\s+class\s*=\s*["\'][^"\']*\b(sidebar|side-bar)[^"\']*["\'][^>]*>.*?<\/[^>]+>)",
  };

  for (const auto& pattern : patterns) {
    std::regex regex(pattern, std::regex::icase);
    result = std::regex_replace(result, regex, "");
  }

  return result;
}

std::string OwlContentExtractor::RemoveNavigationElements(const std::string& html) {
  std::string result = html;

  // Remove common navigation patterns
  std::vector<std::string> patterns = {
    R"(<header\b[^>]*>.*?<\/header>)",
    R"(<nav\b[^>]*>.*?<\/nav>)",
    R"(<[^>]*\s+class\s*=\s*["\'][^"\']*\b(menu|navbar|navigation|breadcrumb)[^"\']*["\'][^>]*>.*?<\/[^>]+>)",
  };

  for (const auto& pattern : patterns) {
    std::regex regex(pattern, std::regex::icase);
    result = std::regex_replace(result, regex, "");
  }

  return result;
}

std::string OwlContentExtractor::RemoveHiddenElements(const std::string& html) {
  std::string result = html;

  // Remove elements with display:none or visibility:hidden
  std::vector<std::string> patterns = {
    R"(<[^>]*\s+style\s*=\s*["\'][^"\']*display\s*:\s*none[^"\']*["\'][^>]*>.*?<\/[^>]+>)",
    R"(<[^>]*\s+style\s*=\s*["\'][^"\']*visibility\s*:\s*hidden[^"\']*["\'][^>]*>.*?<\/[^>]+>)",
    R"(<[^>]*\s+hidden\s*[^>]*>.*?<\/[^>]+>)",
  };

  for (const auto& pattern : patterns) {
    std::regex regex(pattern, std::regex::icase);
    result = std::regex_replace(result, regex, "");
  }

  return result;
}

std::string OwlContentExtractor::ConvertRelativeURLs(const std::string& html, const std::string& base_url) {
  std::string result = html;

  // Extract base domain
  std::regex base_regex(R"(^(https?://[^/]+))");
  std::smatch base_match;
  std::string base_domain;

  if (std::regex_search(base_url, base_match, base_regex)) {
    base_domain = base_match[1].str();
  }

  if (base_domain.empty()) {
    return result;  // Can't convert without valid base
  }

  // Convert relative href attributes
  std::regex href_regex(R"(href\s*=\s*["\'](?!https?://|#|mailto:|tel:)([^"\']+)["\'])");
  result = std::regex_replace(result, href_regex, "href=\"" + base_domain + "$1\"");

  // Convert relative src attributes
  std::regex src_regex(R"(src\s*=\s*["\'](?!https?://|data:)([^"\']+)["\'])");
  result = std::regex_replace(result, src_regex, "src=\"" + base_domain + "$1\"");

  return result;
}

std::string OwlContentExtractor::ExtractMainContent(const std::string& html) {
  std::string result = html;

  // Try to find main content area
  std::regex article_regex(R"(<article\b[^>]*>(.*?)<\/article>)", std::regex::icase);
  std::regex main_regex(R"(<main\b[^>]*>(.*?)<\/main>)", std::regex::icase);
  std::regex role_main_regex(R"(<[^>]+role\s*=\s*["\']main["\'][^>]*>(.*?)<\/[^>]+>)", std::regex::icase);

  std::smatch match;

  // Try article first
  if (std::regex_search(result, match, article_regex)) {
    return match[1].str();
  }

  // Try main tag
  if (std::regex_search(result, match, main_regex)) {
    return match[1].str();
  }

  // Try role="main"
  if (std::regex_search(result, match, role_main_regex)) {
    return match[1].str();
  }

  // Fall back to body content
  std::regex body_regex(R"(<body\b[^>]*>(.*?)<\/body>)", std::regex::icase);
  if (std::regex_search(result, match, body_regex)) {
    return match[1].str();
  }

  return result;
}

std::string OwlContentExtractor::ExtractHTML(
    CefRefPtr<CefFrame> frame,
    const HTMLExtractionOptions& options) {

  LOG_DEBUG("ContentExtractor", "Extracting HTML with clean level " +
           std::to_string(static_cast<int>(options.clean_level)));

  // Get browser and context_id
  CefRefPtr<CefBrowser> browser = frame->GetBrowser();
  if (!browser) {
    LOG_ERROR("ContentExtractor", "ExtractHTML: No browser");
    return "";
  }

  std::ostringstream ctx_stream;
  ctx_stream << "ctx_" << std::setfill('0') << std::setw(6) << browser->GetIdentifier();
  std::string context_id = ctx_stream.str();

  // Get client
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  if (!client) {
    LOG_ERROR("ContentExtractor", "ExtractHTML: No client");
    return "";
  }

  // Send IPC message to renderer to extract page HTML
  CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("extract_page_html");
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  args->SetString(0, context_id);
  frame->SendProcessMessage(PID_RENDERER, message);

  LOG_DEBUG("ContentExtractor", "Sent extract_page_html IPC for context " + context_id);

  // Wait for response (up to 3 seconds)
  if (!client->WaitForTextExtraction(context_id, 3000)) {
    LOG_WARN("ContentExtractor", "ExtractHTML timeout waiting for IPC response");
    return "";
  }

  std::string html = client->GetExtractedText(context_id);

  if (html.empty()) {
    html = "<html><body><!-- Could not extract HTML --></body></html>";
  }

  LOG_DEBUG("ContentExtractor", "HTML extraction complete (" + std::to_string(html.length()) + " chars)");
  return html;
}

std::string OwlContentExtractor::ExtractMarkdown(
    CefRefPtr<CefFrame> frame,
    const MarkdownExtractionOptions& options) {

  LOG_DEBUG("ContentExtractor", "Extracting Markdown");

  // Get visible text directly (simpler and faster)
  std::string text = OwlAIIntelligence::GetVisibleText(frame);

  if (text.empty()) {
    text = "<!-- Could not extract content -->";
  }

  // For now, return as plain text (proper markdown conversion from HTML needs full HTML extraction working)
  // Apply max length if specified
  if (options.max_length > 0 && text.length() > static_cast<size_t>(options.max_length)) {
    text = text.substr(0, options.max_length) + "...";
  }

  LOG_DEBUG("ContentExtractor", "Markdown extraction complete (" + std::to_string(text.length()) + " chars)");
  return text;
}

std::string OwlContentExtractor::ExtractJSON(
    CefRefPtr<CefFrame> frame,
    const std::string& schema) {

  std::string msg = "Extracting JSON";
  if (schema.empty()) {
    msg += " (generic)";
  } else {
    msg += " with schema";
  }
  LOG_DEBUG("ContentExtractor", msg);

  if (schema.empty()) {
    // Generic extraction
    return OlibJSONExtractor::ExtractGeneric(frame);
  } else {
    // Parse schema and extract
    // TODO: Implement schema parsing
    return "{}";
  }
}

std::string OwlContentExtractor::ExtractWithTemplate(
    CefRefPtr<CefFrame> frame,
    const std::string& template_name) {

  LOG_DEBUG("ContentExtractor", "Extracting with template: " + template_name);

  auto* template_mgr = OlibTemplateManager::GetInstance();
  auto* templ = template_mgr->GetTemplate(template_name);

  if (!templ) {
    LOG_ERROR("ContentExtractor", "Template not found: " + template_name);
    return "{}";
  }

  return OlibJSONExtractor::ExtractWithTemplate(frame, *templ);
}

std::string OwlContentExtractor::DetectWebsiteType(CefRefPtr<CefFrame> frame) {
  LOG_DEBUG("ContentExtractor", "Detecting website type");

  std::string url = frame->GetURL().ToString();

  // TODO: Implement proper detection
  // For now, use simple URL pattern matching
  if (url.find("google.") != std::string::npos && url.find("/search") != std::string::npos) {
    return "google_search";
  } else if (url.find("wikipedia.org") != std::string::npos) {
    return "wikipedia";
  } else if (url.find("amazon.") != std::string::npos && url.find("/dp/") != std::string::npos) {
    return "amazon_product";
  } else if (url.find("github.com") != std::string::npos) {
    return "github_repo";
  } else if (url.find("twitter.com") != std::string::npos || url.find("x.com") != std::string::npos) {
    return "twitter_feed";
  } else if (url.find("reddit.com") != std::string::npos) {
    return "reddit_thread";
  }

  return "generic";
}

std::vector<std::string> OwlContentExtractor::ListAvailableTemplates() {
  auto* template_mgr = OlibTemplateManager::GetInstance();
  return template_mgr->ListTemplates();
}
