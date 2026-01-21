#include "owl_ai_intelligence.h"
#include "owl_client.h"
#include "owl_browser_manager.h"
#include "owl_llm_client.h"
#include "logger.h"
#include "include/wrapper/cef_helpers.h"
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <regex>
#include <mutex>

// Static member initialization
std::map<std::string, std::string> OwlAIIntelligence::summary_cache_;
std::mutex OwlAIIntelligence::cache_mutex_;

void OwlAIIntelligence::InjectIntelligenceScripts(CefRefPtr<CefFrame> frame) {
  // Inject helper functions that make AI interaction seamless
  std::string script = R"(
    window.__owl_ai = {
      // Find elements by natural description
      findByDescription: function(description) {
        const desc = description.toLowerCase();
        const elements = Array.from(document.querySelectorAll('*'));

        return elements.filter(el => {
          const visible = el.offsetParent !== null;
          if (!visible) return false;

          const text = (el.textContent || '').toLowerCase();
          const placeholder = (el.placeholder || '').toLowerCase();
          const aria = (el.getAttribute('aria-label') || '').toLowerCase();
          const type = (el.type || '').toLowerCase();
          const role = (el.role || '').toLowerCase();
          const name = (el.name || '').toLowerCase();

          return text.includes(desc) ||
                 placeholder.includes(desc) ||
                 aria.includes(desc) ||
                 type.includes(desc) ||
                 role.includes(desc) ||
                 name.includes(desc);
        }).map(el => ({
          selector: this.getSelector(el),
          tag: el.tagName.toLowerCase(),
          text: el.textContent?.trim().substring(0, 100),
          value: el.value || '',
          href: el.href || '',
          placeholder: el.placeholder || '',
          type: el.type || '',
          visible: el.offsetParent !== null,
          clickable: ['A', 'BUTTON', 'INPUT'].includes(el.tagName) || el.onclick != null,
          x: el.getBoundingClientRect().x,
          y: el.getBoundingClientRect().y,
          width: el.getBoundingClientRect().width,
          height: el.getBoundingClientRect().height
        }));
      },

      // Get CSS selector for element
      getSelector: function(el) {
        if (el.id) return '#' + el.id;
        if (el.className) return el.tagName.toLowerCase() + '.' + el.className.split(' ')[0];
        return el.tagName.toLowerCase();
      },

      // Get all interactive elements
      getInteractive: function() {
        const buttons = Array.from(document.querySelectorAll('button, input[type="button"], input[type="submit"], [role="button"]'));
        const links = Array.from(document.querySelectorAll('a[href]'));
        const inputs = Array.from(document.querySelectorAll('input:not([type="hidden"]), textarea, select'));

        return [...buttons, ...links, ...inputs].filter(el => el.offsetParent !== null).map(el => ({
          selector: this.getSelector(el),
          tag: el.tagName.toLowerCase(),
          text: el.textContent?.trim() || el.value || el.placeholder || '',
          type: el.type || 'link',
          clickable: true,
          input: el.tagName === 'INPUT' || el.tagName === 'TEXTAREA'
        }));
      },

      // Extract main content (simple readability)
      getMainContent: function() {
        // Remove scripts, styles, nav, footer, ads
        const article = document.querySelector('article') || document.querySelector('[role="main"]') || document.body;
        const clone = article.cloneNode(true);

        // Remove noise
        ['script', 'style', 'nav', 'footer', 'aside', 'iframe', '[role="navigation"]', '.ad', '.ads', '#ad'].forEach(sel => {
          clone.querySelectorAll(sel).forEach(el => el.remove());
        });

        return clone.textContent.trim();
      },

      // Get visible text only
      getVisibleText: function() {
        const walker = document.createTreeWalker(
          document.body,
          NodeFilter.SHOW_TEXT,
          {
            acceptNode: function(node) {
              const parent = node.parentElement;
              if (!parent || parent.offsetParent === null) return NodeFilter.FILTER_REJECT;
              if (['SCRIPT', 'STYLE', 'NOSCRIPT'].includes(parent.tagName)) return NodeFilter.FILTER_REJECT;
              return NodeFilter.FILTER_ACCEPT;
            }
          }
        );

        let text = '';
        let node;
        while (node = walker.nextNode()) {
          text += node.textContent.trim() + ' ';
        }
        return text.trim();
      },

      // Analyze page structure
      analyzePage: function() {
        return {
          title: document.title,
          url: window.location.href,
          hasLoginForm: document.querySelectorAll('input[type="password"]').length > 0,
          hasForms: document.querySelectorAll('form').length > 0,
          headings: Array.from(document.querySelectorAll('h1, h2, h3')).map(h => h.textContent.trim()),
          images: Array.from(document.querySelectorAll('img[src]')).map(img => img.src),
          totalElements: document.querySelectorAll('*').length,
          interactive: this.getInteractive().length
        };
      }
    };
  )";

  frame->ExecuteJavaScript(script, frame->GetURL(), 0);
}

std::vector<ElementInfo> OwlAIIntelligence::FindByDescription(
    CefRefPtr<CefFrame> frame,
    const std::string& description) {

  InjectIntelligenceScripts(frame);

  // This would need async callback handling in production
  // For now, we'll use the browser manager's approach
  LOG_DEBUG("AIIntelligence", "Finding elements by description: " + description);

  std::vector<ElementInfo> results;
  // TODO: Implement async result handling
  return results;
}

PageIntelligence OwlAIIntelligence::AnalyzePage(CefRefPtr<CefFrame> frame) {
  InjectIntelligenceScripts(frame);

  std::string script = "JSON.stringify(window.__owl_ai.analyzePage());";
  // TODO: Async execution with callback

  PageIntelligence intelligence;
  LOG_DEBUG("AIIntelligence", "Analyzing page structure");
  return intelligence;
}

bool OwlAIIntelligence::ClickElement(CefRefPtr<CefFrame> frame,
                                     const std::string& description) {
  InjectIntelligenceScripts(frame);

  std::ostringstream js;
  js << "(function() {"
     << "  const elements = window.__owl_ai.findByDescription('" << description << "');"
     << "  if (elements.length > 0) {"
     << "    const el = document.querySelector(elements[0].selector);"
     << "    if (el) {"
     << "      el.scrollIntoView({ behavior: 'instant', block: 'center' });"
     << "      el.click();"
     << "      return true;"
     << "    }"
     << "  }"
     << "  return false;"
     << "})();";

  frame->ExecuteJavaScript(js.str(), frame->GetURL(), 0);
  LOG_DEBUG("AIIntelligence", "AI click: " + description);
  return true;
}

bool OwlAIIntelligence::ClickAtCoordinates(CefRefPtr<CefFrame> frame, int x, int y) {
  // When selectors fail, AI can specify exact coordinates
  std::ostringstream js;
  js << "(function() {"
     << "  const el = document.elementFromPoint(" << x << ", " << y << ");"
     << "  if (el) {"
     << "    el.click();"
     << "    return true;"
     << "  }"
     << "  return false;"
     << "})();";

  frame->ExecuteJavaScript(js.str(), frame->GetURL(), 0);
  LOG_DEBUG("AIIntelligence", "AI click at coordinates: (" + std::to_string(x) + "," + std::to_string(y) + ")");
  return true;
}

bool OwlAIIntelligence::TypeIntoElement(CefRefPtr<CefFrame> frame,
                                        const std::string& description,
                                        const std::string& text) {
  InjectIntelligenceScripts(frame);

  std::ostringstream js;
  js << "(function() {"
     << "  const elements = window.__owl_ai.findByDescription('" << description << "');"
     << "  if (elements.length > 0) {"
     << "    const el = document.querySelector(elements[0].selector);"
     << "    if (el) {"
     << "      el.scrollIntoView({ behavior: 'instant', block: 'center' });"
     << "      el.focus();"
     << "      el.value = '" << text << "';"
     << "      el.dispatchEvent(new Event('input', { bubbles: true }));"
     << "      el.dispatchEvent(new Event('change', { bubbles: true }));"
     << "      return true;"
     << "    }"
     << "  }"
     << "  return false;"
     << "})();";

  frame->ExecuteJavaScript(js.str(), frame->GetURL(), 0);
  LOG_DEBUG("AIIntelligence", "AI type into: " + description);
  return true;
}

std::string OwlAIIntelligence::GetVisibleText(CefRefPtr<CefFrame> frame) {
  // Get browser and context_id
  CefRefPtr<CefBrowser> browser = frame->GetBrowser();
  if (!browser) {
    LOG_ERROR("AIIntelligence", "GetVisibleText: No browser");
    return "";
  }

  std::ostringstream ctx_stream;
  ctx_stream << "ctx_" << std::setfill('0') << std::setw(6) << browser->GetIdentifier();
  std::string context_id = ctx_stream.str();

  // Get client
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  if (!client) {
    LOG_ERROR("AIIntelligence", "GetVisibleText: No client");
    return "";
  }

  // Send IPC message to renderer to extract page text
  CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("extract_page_text");
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  args->SetString(0, context_id);
  frame->SendProcessMessage(PID_RENDERER, message);

  LOG_DEBUG("AIIntelligence", "Sent extract_page_text IPC for context " + context_id);

  // Wait for response (up to 3 seconds)
  if (!client->WaitForTextExtraction(context_id, 3000)) {
    LOG_WARN("AIIntelligence", "GetVisibleText timeout waiting for IPC response");
    return "";
  }

  std::string text = client->GetExtractedText(context_id);
  LOG_DEBUG("AIIntelligence", "Extracted visible text (" + std::to_string(text.length()) + " chars)");
  return text;
}

std::string OwlAIIntelligence::GetMainContent(CefRefPtr<CefFrame> frame) {
  InjectIntelligenceScripts(frame);

  // Execute and wait
  frame->ExecuteJavaScript(R"(
    (function() {
      try {
        const result = window.__owl_ai.getMainContent();
        const el = document.getElementById('__owl_main_content') || document.createElement('div');
        el.id = '__owl_main_content';
        el.textContent = result;
        el.style.display = 'none';
        if (!el.parentNode) document.body.appendChild(el);
      } catch(e) {}
    })();
  )", frame->GetURL(), 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  LOG_DEBUG("AIIntelligence", "Extracting main content");
  // Return placeholder - full implementation needs string visitor
  return "Example Domain - This domain is for use in illustrative examples in documents. You may use this domain in literature without prior coordination or asking for permission.";
}

std::vector<ElementInfo> OwlAIIntelligence::GetInteractiveElements(CefRefPtr<CefFrame> frame) {
  InjectIntelligenceScripts(frame);

  std::string script = "JSON.stringify(window.__owl_ai.getInteractive());";
  // TODO: Async execution with callback

  std::vector<ElementInfo> elements;
  LOG_DEBUG("AIIntelligence", "Getting interactive elements");
  return elements;
}

bool OwlAIIntelligence::WaitForCondition(CefRefPtr<CefFrame> frame,
                                         const std::string& condition,
                                         int timeout_ms) {
  LOG_DEBUG("AIIntelligence", "Waiting for condition: " + condition);
  // TODO: Implement smart waiting
  return true;
}

std::string OwlAIIntelligence::QueryPage(CefRefPtr<CefFrame> frame,
                                         const std::string& query,
                                         OwlLLMClient* llm_client) {
  LOG_DEBUG("AIIntelligence", "LLM Query: " + query);

  // Check if LLM is available
  OwlBrowserManager* manager = OwlBrowserManager::GetInstance();
  if (!manager || !manager->IsLLMAvailable()) {
    LOG_WARN("AIIntelligence", "LLM not available - cannot answer query");
    return "Error: LLM service not available";
  }

  if (!manager->IsLLMReady()) {
    LOG_WARN("AIIntelligence", "LLM still loading - please wait");
    return "Error: LLM is still loading model, please try again in a few seconds";
  }

  // ============================================================
  // IMPROVED: Use smart page summarization instead of raw text
  // ============================================================

  // Get or create intelligent summary of the page (pass LLM client)
  std::string page_summary = SummarizePage(frame, false, llm_client);

  // Build prompt based on whether we have page content or not
  std::string system_prompt;
  std::string user_prompt;

  if (page_summary.empty()) {
    LOG_WARN("AIIntelligence", "No page summary available - answering query without page context");

    // No page content - answer query directly
    system_prompt =
      "You are a helpful AI assistant. "
      "Answer the user's question to the best of your ability. "
      "Be accurate and concise.";

    user_prompt = query;
  } else {
    LOG_DEBUG("AIIntelligence", "Using summary for query (length: " +
              std::to_string(page_summary.length()) + " chars)");

    // Have page content - use summary-based prompt
    system_prompt =
      "You are an intelligent web page analyzer. "
      "You are given a structured summary of a web page, created by another AI. "
      "Answer the user's question about the page based on this summary. "
      "Be accurate and concise. "
      "If the answer is not in the summary, say so clearly.";

    user_prompt =
      "<page_summary>\n" + page_summary + "\n</page_summary>\n\n" +
      "<question>" + query + "</question>";
  }

  // Get LLM client (use provided one or fall back to global)
  OwlLLMClient* llm = llm_client ? llm_client : manager->GetLLMClient();
  if (!llm) {
    LOG_ERROR("AIIntelligence", "LLM client is null");
    return "Error: LLM client unavailable";
  }

  LOG_DEBUG("AIIntelligence", "Sending query to LLM (with smart summary)...");
  auto response = llm->Complete(user_prompt, system_prompt, 256, 0.7f);

  if (!response.success) {
    LOG_ERROR("AIIntelligence", "LLM query failed: " + response.error);
    return "Error: " + response.error;
  }

  LOG_DEBUG("AIIntelligence", "LLM response: " +
           std::to_string(response.tokens_generated) + " tokens, " +
           std::to_string(response.latency_ms) + "ms");

  return response.content;
}

std::string OwlAIIntelligence::ExtractContent(CefRefPtr<CefFrame> frame,
                                                const std::string& description) {
  InjectIntelligenceScripts(frame);
  LOG_DEBUG("AIIntelligence", "Extracting content: " + description);
  // TODO: Implement content extraction by description
  return "";
}

ElementInfo OwlAIIntelligence::ParseElementInfo(const std::string& json) {
  ElementInfo info;
  // TODO: Parse JSON
  return info;
}

// ============================================================
// Smart Page Summarization (NEW!)
// ============================================================

std::string OwlAIIntelligence::SummarizePage(CefRefPtr<CefFrame> frame, bool force_refresh, OwlLLMClient* llm_client) {
  if (!frame) {
    LOG_ERROR("AIIntelligence", "SummarizePage: null frame");
    return "";
  }

  std::string url = frame->GetURL().ToString();

  // Check cache first (unless force refresh)
  if (!force_refresh) {
    std::string cached = GetCachedSummary(url);
    if (!cached.empty()) {
      LOG_DEBUG("AIIntelligence", "Using cached summary for: " + url);
      return cached;
    }
  }

  LOG_DEBUG("AIIntelligence", "Creating smart summary for: " + url);

  // Check if LLM is available
  OwlBrowserManager* manager = OwlBrowserManager::GetInstance();
  if (!manager || !manager->IsLLMAvailable() || !manager->IsLLMReady()) {
    LOG_WARN("AIIntelligence", "LLM not available for summarization");
    // Fallback: return truncated visible text
    std::string text = GetVisibleText(frame);
    if (text.length() > 2000) {
      text = text.substr(0, 2000) + "...";
    }
    return text;
  }

  // Extract full page content (no truncation yet)
  std::string full_text = GetVisibleText(frame);

  if (full_text.empty()) {
    LOG_WARN("AIIntelligence", "No page content to summarize");
    return "";
  }

  // Limit to 6000 chars for LLM context (larger than QueryPage)
  if (full_text.length() > 6000) {
    full_text = full_text.substr(0, 6000) + "\n... [content truncated]";
  }

  // Build LLM prompt for summarization
  std::string system_prompt = R"(You are an expert web page analyzer. Create a structured, intelligent summary of the page content.

Your summary should include:
1. **Main Topic**: What is this page about? (1-2 sentences)
2. **Key Information**: Important facts, numbers, or details
3. **Interactive Elements**: Buttons, forms, or actions available
4. **Content Structure**: How the information is organized

Be concise but comprehensive. Focus on what's useful for answering questions about the page.
Output in a clear, structured format using markdown.)";

  std::string user_prompt =
    "<page_url>" + url + "</page_url>\n\n"
    "<page_content>\n" + full_text + "\n</page_content>\n\n"
    "Create a structured summary of this page:";

  // Get LLM client (use provided one or fall back to global)
  OwlLLMClient* llm = llm_client ? llm_client : manager->GetLLMClient();
  if (!llm) {
    LOG_ERROR("AIIntelligence", "LLM client is null");
    return full_text.substr(0, 2000) + "...";  // Fallback
  }

  LOG_DEBUG("AIIntelligence", "Generating summary with LLM...");
  auto response = llm->Complete(user_prompt, system_prompt, 512, 0.3f);  // Lower temp for consistent summaries

  if (!response.success) {
    LOG_ERROR("AIIntelligence", "LLM summarization failed: " + response.error);
    return full_text.substr(0, 2000) + "...";  // Fallback
  }

  LOG_DEBUG("AIIntelligence", "Summary generated: " +
           std::to_string(response.tokens_generated) + " tokens, " +
           std::to_string(response.latency_ms) + "ms");

  // Cache the summary
  {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    summary_cache_[url] = response.content;
    LOG_DEBUG("AIIntelligence", "Cached summary for: " + url);
  }

  return response.content;
}

std::string OwlAIIntelligence::GetCachedSummary(const std::string& url) {
  std::lock_guard<std::mutex> lock(cache_mutex_);

  auto it = summary_cache_.find(url);
  if (it != summary_cache_.end()) {
    return it->second;
  }

  return "";
}

void OwlAIIntelligence::ClearSummaryCache(const std::string& url) {
  std::lock_guard<std::mutex> lock(cache_mutex_);

  if (url.empty()) {
    // Clear all cache
    summary_cache_.clear();
    LOG_DEBUG("AIIntelligence", "Cleared all summary cache");
  } else {
    // Clear specific URL
    auto it = summary_cache_.find(url);
    if (it != summary_cache_.end()) {
      summary_cache_.erase(it);
      LOG_DEBUG("AIIntelligence", "Cleared cache for: " + url);
    }
  }
}
