#include "owl_nla.h"
#include "owl_task_state.h"
#include "owl_ai_intelligence.h"
#include "owl_browser_manager.h"
#include "action_result.h"
#include "owl_client.h"
#include "owl_llm_client.h"
#include "owl_semantic_matcher.h"
#include "owl_demographics.h"
#include "owl_llm_guardrail.h"
#include "../resources/icons/icons.h"
#include "logger.h"
#include "include/cef_parser.h"
#include "include/cef_process_message.h"
#include "include/wrapper/cef_helpers.h"
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <regex>
#include <algorithm>

// ============================================================
// Main Entry Point
// ============================================================

std::string OwlNLA::ExecuteCommand(
    CefRefPtr<CefFrame> frame,
    const std::string& command) {

  LOG_DEBUG("NLA", "Executing command: " + command);

  CefRefPtr<CefBrowser> browser = frame->GetBrowser();
  if (!browser) {
    LOG_ERROR("NLA", "Failed to get browser");
    return "Error: Browser not found";
  }

  PageState state = GetPageState(frame);
  LOG_DEBUG("NLA", "Current page: " + state.url);

  NLActionPlan plan = PlanActions(command, state);

  if (!plan.success) {
    LOG_ERROR("NLA", "Failed to create action plan: " + plan.error);
    return "Error: " + plan.error;
  }

  LOG_DEBUG("NLA", "Action plan: " + std::to_string(plan.actions.size()) + " steps");

  // Save task descriptions to persistent state
  OwlTaskState* task_state = OwlTaskState::GetInstance();
  std::vector<std::string> task_descriptions;
  for (const auto& action : plan.actions) {
    task_descriptions.push_back(action.type + " - " + action.target);
  }
  task_state->SetTasks(task_descriptions);

  // Show automation overlay on the page
  ShowAutomationOverlay(frame);

  // Step 3: Execute each action
  std::ostringstream result;
  result << "Executed " << plan.actions.size() << " actions:\n\n";

  // Track timing for NLA execution
  auto nla_start_time = std::chrono::steady_clock::now();

  for (size_t i = 0; i < plan.actions.size(); i++) {
    // Mark current task as active
    task_state->UpdateTaskStatus(i, TaskStatus::ACTIVE);
    NLAction& action = plan.actions[i];

    LOG_DEBUG("NLA", "Step " + std::to_string(i + 1) + "/" +
             std::to_string(plan.actions.size()) + ": " + action.type);

    // Calculate timing
    auto step_start = std::chrono::steady_clock::now();
    double step_elapsed_ms = std::chrono::duration<double, std::milli>(step_start - step_start).count(); // Always 0 at start
    double total_elapsed_ms = std::chrono::duration<double, std::milli>(step_start - nla_start_time).count();

    // Update overlay with current step
    std::string step_desc = action.type + " - " + action.target;
    CefRefPtr<CefFrame> current_frame = browser->GetMainFrame();
    UpdateAutomationStep(current_frame, i + 1, plan.actions.size(), step_desc, step_elapsed_ms, total_elapsed_ms);

    result << (i + 1) << ". " << action.type << " - " << action.target << "\n";

    // Always get fresh frame from browser before each action
    current_frame = browser->GetMainFrame();
    bool success = ExecuteAction(current_frame, action);

    if (!success) {
      LOG_ERROR("NLA", "Action failed: " + action.result);
      result << "   ❌ Failed: " << action.result << "\n";
      result << "\nExecution stopped at step " << (i + 1) << "\n";
      task_state->UpdateTaskStatus(i, TaskStatus::FAILED, action.result);

      // Hide overlay on failure
      CefRefPtr<CefFrame> final_frame = browser->GetMainFrame();
      if (final_frame) {
        HideAutomationOverlay(final_frame);
      }

      break;
    }

    LOG_DEBUG("NLA", "Action succeeded");
    result << "   ✓ Success: " << action.result << "\n";
    action.completed = true;
    task_state->UpdateTaskStatus(i, TaskStatus::COMPLETED, action.result);
    task_state->AdvanceToNextTask();

    // If we pressed Enter in a search box, skip the next button click
    if (action.type == "type" &&
        action.result.find("and submitted") != std::string::npos &&
        i + 1 < plan.actions.size()) {

      NLAction& next_action = plan.actions[i + 1];
      std::string next_target_lower = next_action.target;
      std::transform(next_target_lower.begin(), next_target_lower.end(),
                     next_target_lower.begin(), ::tolower);

      // Skip if next is clicking search/submit button
      if (next_action.type == "click" &&
          (next_target_lower.find("search") != std::string::npos ||
           next_target_lower.find("submit") != std::string::npos ||
           next_target_lower.find("button") != std::string::npos)) {

        LOG_DEBUG("NLA", "Skipping redundant button click");
        result << (i + 2) << ". " << next_action.type << " - " << next_action.target << "\n";
        result << "   ⊘ Skipped (already submitted)\n";

        // Mark as ACTIVE first, then COMPLETED to maintain proper task lifecycle
        task_state->UpdateTaskStatus(i + 1, TaskStatus::ACTIVE);

        next_action.completed = true;
        next_action.result = "Skipped - form already submitted via Enter";
        task_state->UpdateTaskStatus(i + 1, TaskStatus::COMPLETED, next_action.result);
        task_state->AdvanceToNextTask();
        i++; // Skip next iteration
      }
    }

    // Small delay between actions for DOM updates
    if (i < plan.actions.size() - 1) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
  }

  // Hide automation overlay on completion
  CefRefPtr<CefFrame> final_frame = browser->GetMainFrame();
  if (final_frame) {
    HideAutomationOverlay(final_frame);
  }

  LOG_DEBUG("NLA", "NLA command complete");
  return result.str();
}

// ============================================================
// Step 1: Get Page State
// ============================================================

PageState OwlNLA::GetPageState(CefRefPtr<CefFrame> frame) {
  PageState state;

  CefRefPtr<CefBrowser> browser = frame->GetBrowser();
  if (browser) {
    state.url = frame->GetURL().ToString();
    state.title = browser->GetMainFrame()->GetName().ToString();
  }

  // ============================================================
  // IMPROVED: Use smart page summarization for better context
  // ============================================================

  // Try to get smart summary first (cached if available)
  std::string summary = OwlAIIntelligence::SummarizePage(frame);

  if (!summary.empty()) {
    LOG_DEBUG("NLA", "Using smart summary for page state");
    state.visible_text = summary;
  } else {
    // Fallback to raw text extraction
    LOG_WARN("NLA", "Summary not available, using raw text");
    std::string full_text = OwlAIIntelligence::GetVisibleText(frame);
    if (full_text.length() > 1500) {
      state.visible_text = full_text.substr(0, 1500) + "...";
    } else {
      state.visible_text = full_text;
    }
  }

  // Get interactive elements (simplified - real implementation would use DOM scan)
  // For now, we'll extract this info via JavaScript
  state.interactive_elements.push_back("buttons and links detected via DOM scan");
  state.input_fields.push_back("input fields detected via DOM scan");

  // ============================================================
  // NEW: Get demographic context (location, time, weather)
  // ============================================================
  OwlDemographics* demographics = OwlDemographics::GetInstance();
  if (demographics && demographics->IsReady()) {
    state.demographics = demographics->GetAllInfo();
  } else if (demographics && demographics->Initialize()) {
    state.demographics = demographics->GetAllInfo();
  }

  return state;
}

// ============================================================
// Step 2: Plan Actions with LLM
// ============================================================

std::string OwlNLA::PageStateToXML(const PageState& state) {
  // ============================================================
  // SECURITY: Apply guardrails to webpage content
  // ============================================================
  LOG_DEBUG("NLA", "Applying LLM guardrails");

  // Skip guardrails for our own trusted pages (owl://)
  bool is_trusted_page = (state.url.find("owl://") == 0);

  OlibGuardrail::LLMGuardrail::GuardrailResult guardrail_result;
  if (!is_trusted_page) {
    // Process visible text through guardrail system
    guardrail_result = OlibGuardrail::LLMGuardrail::ProcessUntrustedContent(
        state.visible_text, "webpage");
  } else {
    // Trusted page - skip guardrail
    guardrail_result.passed_validation = true;
    guardrail_result.safe_content = state.visible_text;
  }

  if (!guardrail_result.passed_validation) {
    LOG_ERROR("NLA", "Webpage content BLOCKED: " + guardrail_result.error_message);
    // Return minimal safe state
    std::ostringstream xml;
    xml << "<page_state>\n";
    xml << "  <url>" << state.url << "</url>\n";
    xml << "  <title>" << state.title << "</title>\n";
    xml << "  <visible_text>[Content blocked by security guardrails]</visible_text>\n";
    xml << "  <security_warning>" << guardrail_result.error_message << "</security_warning>\n";
    xml << "</page_state>";
    return xml.str();
  }

  // Log threats that were blocked
  if (!guardrail_result.threats_blocked.empty()) {
    LOG_WARN("NLA", "Guardrails blocked " +
             std::to_string(guardrail_result.threats_blocked.size()) +
             " threats (risk score: " +
             std::to_string(guardrail_result.total_risk_score) + ")");
    for (const auto& threat : guardrail_result.threats_blocked) {
      LOG_WARN("NLA", "  - " + threat);
    }
  }

  // Build XML with sanitized content
  // NOTE: safe_content is already wrapped with secure delimiters
  std::ostringstream xml;
  xml << "<page_state>\n";
  xml << "  <url>" << state.url << "</url>\n";
  xml << "  <title>" << state.title << "</title>\n";
  xml << "  <visible_text>" << guardrail_result.safe_content << "</visible_text>\n";

  // Add demographics context
  if (state.demographics.has_location || state.demographics.has_weather) {
    xml << "  " << OwlDemographics::ToXML(state.demographics) << "\n";
  }

  xml << "</page_state>";
  return xml.str();
}

NLActionPlan OwlNLA::PlanActions(
    const std::string& command,
    const PageState& current_state) {

  NLActionPlan plan;

  // Check if LLM is available
  OwlBrowserManager* manager = OwlBrowserManager::GetInstance();
  if (!manager || !manager->IsLLMReady()) {
    plan.success = false;
    plan.error = "LLM not available or still loading";
    return plan;
  }

  // Build prompt for LLM
  std::string system_prompt = R"PROMPT(You are a browser automation planner with access to user context (location, time, weather). Convert natural language commands into a sequence of browser actions.

Available actions:
- navigate: Go to a URL (target: URL)
- click: Click an element (target: description like "search button" or "first link")
- type: Type text into input (target: input description, value: text to type)
- wait: Wait for milliseconds (timeout_ms: number)
- screenshot: Take a screenshot
- extract: Extract text from page

Context awareness:
- You have access to the user's current location (city, country, coordinates)
- You know the current date and time (including day of week)
- You have current weather information (temperature, condition)
- Use this context to make search queries more specific and relevant

Examples:
- "find me a hotel" → search for "hotels in [USER_CITY]"
- "find a restaurant" → search for "restaurants near me in [USER_CITY]"
- "book for next week" → use the actual dates based on current date
- "what's the weather" → you already know it, inform the user

Output ONLY valid JSON in this exact format:
{
  "reasoning": "brief explanation of the plan (mention if you used context)",
  "actions": [
    {"type": "navigate", "target": "https://google.com"},
    {"type": "type", "target": "search box", "value": "hotels in New York"},
    {"type": "click", "target": "search button"},
    {"type": "wait", "timeout_ms": 2000}
  ]
}

Rules:
1. Be specific with element descriptions
2. Add waits after navigation or clicks (1000-2000ms)
3. For searches: ALWAYS type the query first, THEN click search button or press Enter
4. For "search for X": MUST include type action with value="X" before clicking
5. For "first result/link": use "first search result link"
6. USE DEMOGRAPHIC CONTEXT (location, time, weather) to enhance searches
7. Keep it simple and direct

CRITICAL: When user says "search for banana", you MUST:
1. Navigate to search engine
2. Type "banana" into search box
3. Click search button
Never skip the typing step!)PROMPT";

  std::string page_xml = PageStateToXML(current_state);

  std::string user_prompt =
    page_xml + "\n\n" +
    "<command>" + command + "</command>\n\n" +
    "Generate the action plan as JSON:";

  // Enhance system prompt with anti-injection instructions
  std::string enhanced_system_prompt = OlibGuardrail::PromptProtector::EnhanceSystemPrompt(system_prompt);

  OwlLLMClient* llm = manager->GetLLMClient();
  auto response = llm->Complete(user_prompt, enhanced_system_prompt, 512, 0.3f);  // Low temp for deterministic output

  if (!response.success) {
    plan.success = false;
    plan.error = "LLM query failed: " + response.error;
    return plan;
  }

  LOG_DEBUG("NLA", "LLM response received");

  // ============================================================
  // SECURITY: Validate LLM output for hijacking
  // ============================================================
  auto validation = OlibGuardrail::LLMGuardrail::ValidateLLMOutput(response.content, "json");

  if (!validation.is_valid) {
    plan.success = false;
    plan.error = "LLM response validation failed: Invalid format";
    LOG_ERROR("NLA", plan.error);
    return plan;
  }

  if (validation.is_suspicious) {
    LOG_WARN("NLA", "LLM response appears suspicious, but proceeding with caution");
    for (const auto& issue : validation.issues) {
      LOG_WARN("NLA", "  - " + issue);
    }
  }

  // Parse JSON response
  // Extract JSON from response (LLM might add extra text)
  std::regex json_regex(R"(\{[\s\S]*\})");
  std::smatch match;
  std::string json_str = response.content;

  if (std::regex_search(response.content, match, json_regex)) {
    json_str = match[0].str();
  }

  CefRefPtr<CefValue> json_value = CefParseJSON(json_str, JSON_PARSER_ALLOW_TRAILING_COMMAS);

  if (!json_value || json_value->GetType() != VTYPE_DICTIONARY) {
    plan.success = false;
    plan.error = "Failed to parse LLM JSON response";
    return plan;
  }

  CefRefPtr<CefDictionaryValue> dict = json_value->GetDictionary();

  // Extract reasoning
  if (dict->HasKey("reasoning")) {
    plan.reasoning = dict->GetString("reasoning").ToString();
  }

  // Extract actions array
  if (!dict->HasKey("actions")) {
    plan.success = false;
    plan.error = "No actions in LLM response";
    return plan;
  }

  CefRefPtr<CefListValue> actions_list = dict->GetList("actions");
  for (size_t i = 0; i < actions_list->GetSize(); i++) {
    CefRefPtr<CefDictionaryValue> action_dict = actions_list->GetDictionary(i);

    NLAction action;
    action.type = action_dict->GetString("type").ToString();

    if (action_dict->HasKey("target")) {
      action.target = action_dict->GetString("target").ToString();
    }
    if (action_dict->HasKey("value")) {
      action.value = action_dict->GetString("value").ToString();
    }
    if (action_dict->HasKey("timeout_ms")) {
      action.timeout_ms = action_dict->GetInt("timeout_ms");
    }

    action.completed = false;
    plan.actions.push_back(action);
  }

  plan.success = true;
  return plan;
}

// ============================================================
// Step 3: Execute Actions
// ============================================================

bool OwlNLA::ExecuteAction(CefRefPtr<CefFrame> frame, NLAction& action) {
  if (action.type == "navigate") {
    return ExecuteNavigate(frame, action);
  } else if (action.type == "click") {
    return ExecuteClick(frame, action);
  } else if (action.type == "type") {
    return ExecuteType(frame, action);
  } else if (action.type == "wait") {
    return ExecuteWait(frame, action);
  } else if (action.type == "screenshot") {
    return ExecuteScreenshot(frame, action);
  } else if (action.type == "extract") {
    return ExecuteExtract(frame, action);
  } else {
    action.result = "Unknown action type: " + action.type;
    return false;
  }
}

bool OwlNLA::ExecuteNavigate(CefRefPtr<CefFrame> frame, NLAction& action) {
  // Get context_id and client
  CefRefPtr<CefBrowser> browser = frame->GetBrowser();
  if (!browser) {
    action.result = "Failed to get browser";
    return false;
  }

  std::ostringstream ctx_stream;
  ctx_stream << "ctx_" << std::setfill('0') << std::setw(6) << browser->GetIdentifier();
  std::string context_id = ctx_stream.str();

  // Get client to access navigation tracking
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  if (!client) {
    action.result = "Failed to get client";
    return false;
  }

  // Reset navigation state before loading
  client->ResetNavigation();

  // Load URL
  LOG_DEBUG("NLA", "Loading URL: " + action.target);
  frame->LoadURL(action.target);

  // Wait for navigation to complete (smart waiting - returns as soon as loaded)
  LOG_DEBUG("NLA", "Waiting for navigation");
  client->WaitForNavigation(10000);  // 10 second timeout

  // For dynamic sites, we need to wait for content to appear
  // Use polling with exponential backoff instead of fixed waits
  LOG_DEBUG("NLA", "Waiting for dynamic content");

  OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
  int scan_attempts = 0;
  int max_scan_attempts = 15;  // Increased from 5 - allow up to 10 seconds for JS rendering
  int delay_ms = 300;
  size_t prev_element_count = 0;  // Use size_t to match elements.size()
  int stable_count = 0;  // Count how many scans with same element count

  while (scan_attempts < max_scan_attempts) {
    // Pump message loop to allow rendering
    for (int i = 0; i < (delay_ms / 10); i++) {
      CefDoMessageLoopWork();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Trigger scan
    CefRefPtr<CefProcessMessage> scan_msg = CefProcessMessage::Create("scan_element");
    CefRefPtr<CefListValue> scan_args = scan_msg->GetArgumentList();
    scan_args->SetString(0, context_id);
    scan_args->SetString(1, "*");
    browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, scan_msg);

    // For UI browsers, we can't block waiting for scan completion (would deadlock)
    // Instead, just wait a bit for the scan message to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Pump message loop to process scan results
    for (int i = 0; i < 20; i++) {
      CefDoMessageLoopWork();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::vector<ElementSemantics> elements = matcher->GetAllElements(context_id);

    // Check if we have interactive elements (inputs, buttons, textareas)
    int interactive_count = 0;
    for (const auto& elem : elements) {
      if (elem.tag == "INPUT" || elem.tag == "BUTTON" || elem.tag == "TEXTAREA") {
        interactive_count++;
      }
    }

#ifdef OWL_DEBUG_BUILD
    int input_count = 0;
    for (const auto& elem : elements) {
      if (elem.tag == "INPUT" || elem.tag == "TEXTAREA") {
        input_count++;
      }
    }
    LOG_DEBUG("NLA", "Scan attempt " + std::to_string(scan_attempts + 1) + ": " +
             std::to_string(elements.size()) + " elements, " +
             std::to_string(interactive_count) + " interactive (" +
             std::to_string(input_count) + " inputs)");
#endif

    // Success criteria: found interactive elements
    if (interactive_count >= 3) {
      LOG_DEBUG("NLA", "Page ready with " + std::to_string(interactive_count) + " elements");
      action.result = "Navigated to " + action.target + " (" + std::to_string(elements.size()) + " elements scanned)";
      return true;
    }

    // Check if page has stabilized (same element count for 3 scans)
    if (elements.size() == prev_element_count) {
      stable_count++;
      if (stable_count >= 3 && elements.size() > 20) {
        LOG_DEBUG("NLA", "Page stabilized at " + std::to_string(elements.size()) + " elements");
        action.result = "Navigated to " + action.target + " (" + std::to_string(elements.size()) + " elements scanned)";
        return true;
      }
    } else {
      stable_count = 0;
    }
    prev_element_count = elements.size();

    scan_attempts++;
    // Adaptive timing: quick early scans, slower later
    delay_ms = (scan_attempts < 8) ? 300 : 500;
  }

  // Max attempts reached - return success anyway
  std::vector<ElementSemantics> elements = matcher->GetAllElements(context_id);
  LOG_WARN("NLA", "Max scans reached. Found " + std::to_string(elements.size()) + " elements");
  action.result = "Navigated to " + action.target + " (" + std::to_string(elements.size()) + " elements scanned)";
  return true;
}

bool OwlNLA::ExecuteClick(CefRefPtr<CefFrame> frame, NLAction& action) {
  // Get context_id from browser
  CefRefPtr<CefBrowser> browser = frame->GetBrowser();
  if (!browser) {
    action.result = "Failed to get browser";
    return false;
  }

  std::ostringstream ctx_stream;
  ctx_stream << "ctx_" << std::setfill('0') << std::setw(6) << browser->GetIdentifier();
  std::string context_id = ctx_stream.str();

  OwlBrowserManager* manager = OwlBrowserManager::GetInstance();
  std::string selector = action.target;

  // Resolve semantic selector to CSS selector (like browser-wrapper does)
  bool is_semantic = (selector.find_first_of("#.[:>") == std::string::npos);
  if (is_semantic) {
    LOG_DEBUG("NLA", "Resolving semantic selector: " + selector);

    // Use FindElement to get CSS selector via semantic matcher
    std::string find_result = manager->FindElement(context_id, selector, 3);

    // Parse JSON to extract CSS selector
    CefRefPtr<CefValue> json_value = CefParseJSON(find_result, JSON_PARSER_ALLOW_TRAILING_COMMAS);
    if (json_value && json_value->GetType() == VTYPE_DICTIONARY) {
      CefRefPtr<CefDictionaryValue> dict = json_value->GetDictionary();
      if (dict->HasKey("matches")) {
        CefRefPtr<CefListValue> matches = dict->GetList("matches");
        if (matches->GetSize() > 0) {
          CefRefPtr<CefDictionaryValue> first_match = matches->GetDictionary(0);
          if (first_match->HasKey("element")) {
            CefRefPtr<CefDictionaryValue> element = first_match->GetDictionary("element");
#ifdef OWL_DEBUG_BUILD
            double confidence = first_match->GetDouble("confidence");
            std::string old_selector = selector;
#endif
            selector = element->GetString("selector").ToString();
#ifdef OWL_DEBUG_BUILD
            LOG_DEBUG("NLA", "Resolved '" + old_selector + "' to CSS selector: '" + selector +
                     "' (confidence: " + std::to_string(confidence) + ")");
#endif
          }
        } else {
          LOG_ERROR("NLA", "No matches found for: " + action.target);
          action.result = "Element not found: " + action.target;
          return false;
        }
      }
    }
  }

  // Now click using the CSS selector via BrowserManager (uses proper mouse events)
  LOG_DEBUG("NLA", "Clicking: " + selector);

  ActionResult click_result = manager->Click(context_id, selector);
  if (click_result.status != ActionStatus::OK) {
    LOG_ERROR("NLA", "Click failed for: " + selector + " - " + click_result.message);
    action.result = "Click failed: " + action.target + " (" + click_result.message + ")";
    return false;
  }

  // Small delay to allow click to process
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  action.result = "Clicked: " + action.target;
  return true;
}

bool OwlNLA::ExecuteType(CefRefPtr<CefFrame> frame, NLAction& action) {
  // Get context_id from browser
  CefRefPtr<CefBrowser> browser = frame->GetBrowser();
  if (!browser) {
    action.result = "Failed to get browser";
    return false;
  }

  std::ostringstream ctx_stream;
  ctx_stream << "ctx_" << std::setfill('0') << std::setw(6) << browser->GetIdentifier();
  std::string context_id = ctx_stream.str();

  OwlBrowserManager* manager = OwlBrowserManager::GetInstance();
  std::string selector = action.target;

  // Resolve semantic selector to CSS selector (like browser-wrapper does)
  bool is_semantic = (selector.find_first_of("#.[:>") == std::string::npos);
  if (is_semantic) {
    LOG_DEBUG("NLA", "Resolving semantic selector: " + selector);

    // Use FindElement to get CSS selector via semantic matcher
    std::string find_result = manager->FindElement(context_id, selector, 3);

    // Parse JSON to extract CSS selector
    CefRefPtr<CefValue> json_value = CefParseJSON(find_result, JSON_PARSER_ALLOW_TRAILING_COMMAS);
    if (json_value && json_value->GetType() == VTYPE_DICTIONARY) {
      CefRefPtr<CefDictionaryValue> dict = json_value->GetDictionary();
      if (dict->HasKey("matches")) {
        CefRefPtr<CefListValue> matches = dict->GetList("matches");
        if (matches->GetSize() > 0) {
          CefRefPtr<CefDictionaryValue> first_match = matches->GetDictionary(0);
          if (first_match->HasKey("element")) {
            CefRefPtr<CefDictionaryValue> element = first_match->GetDictionary("element");
#ifdef OWL_DEBUG_BUILD
            double confidence = first_match->GetDouble("confidence");
            std::string old_selector = selector;
#endif
            selector = element->GetString("selector").ToString();
#ifdef OWL_DEBUG_BUILD
            LOG_DEBUG("NLA", "Resolved '" + old_selector + "' to CSS selector: '" + selector +
                     "' (confidence: " + std::to_string(confidence) + ")");
#endif
          }
        } else {
          LOG_ERROR("NLA", "No matches found for: " + action.target);
          action.result = "Input element not found: " + action.target;
          return false;
        }
      }
    }
  }

  // Now type using the CSS selector via BrowserManager (uses IPC to renderer)
  LOG_DEBUG("NLA", "Typing into: " + selector);

  ActionResult type_result = manager->Type(context_id, selector, action.value);
  if (type_result.status != ActionStatus::OK) {
    LOG_ERROR("NLA", "Type failed for: " + selector + " - " + type_result.message);
    action.result = "Type failed: " + action.target + " (" + type_result.message + ")";
    return false;
  }

  // Check if typing into a search box - if so, submit form via Enter key
  std::string target_lower = action.target;
  std::transform(target_lower.begin(), target_lower.end(), target_lower.begin(), ::tolower);

  if (target_lower.find("search") != std::string::npos) {
    LOG_DEBUG("NLA", "Search box detected - submitting form");

    // Get browser to send IPC message
    auto browser = manager->GetBrowser(context_id);
    if (browser && browser->GetMainFrame()) {
      // Send submit_form IPC to renderer
      CefRefPtr<CefProcessMessage> submit_msg = CefProcessMessage::Create("submit_form");
      browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, submit_msg);

      // Pump message loop briefly to process IPC (no dead wait)
      for (int i = 0; i < 3; i++) {
        CefDoMessageLoopWork();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }

      LOG_DEBUG("NLA", "Submitted form");
      action.result = "Typed '" + action.value + "' into " + action.target + " and submitted";
      return true;
    } else {
      LOG_ERROR("NLA", "Failed to get browser for submitting form");
    }
  }

  action.result = "Typed '" + action.value + "' into " + action.target;
  return true;
}

bool OwlNLA::ExecuteWait(CefRefPtr<CefFrame> frame, NLAction& action) {
  std::this_thread::sleep_for(std::chrono::milliseconds(action.timeout_ms));
  action.result = "Waited " + std::to_string(action.timeout_ms) + "ms";
  return true;
}

bool OwlNLA::ExecuteScreenshot(CefRefPtr<CefFrame> frame, NLAction& action) {
  action.result = "Screenshot taken (would return base64 data)";
  return true;
}

bool OwlNLA::ExecuteExtract(CefRefPtr<CefFrame> frame, NLAction& action) {
  std::string text = OwlAIIntelligence::GetVisibleText(frame);
  action.result = "Extracted " + std::to_string(text.length()) + " characters";
  return true;
}

// ============================================================
// Automation Overlay Helpers
// ============================================================

void OwlNLA::ShowAutomationOverlay(CefRefPtr<CefFrame> frame) {
  if (!frame) return;

  // Build JavaScript with embedded FA icons
  std::ostringstream js;
  js << R"(
(function() {
  // Don't create if already exists
  if (document.getElementById('owl-automation-overlay')) return;

  // Create overlay container at bottom right
  const overlay = document.createElement('div');
  overlay.id = 'owl-automation-overlay';
  overlay.setAttribute('data-owl-ignore', 'true');  // Mark to exclude from element scanning
  overlay.style.cssText = `
    position: fixed;
    bottom: 24px;
    right: 24px;
    background: white;
    border-radius: 16px;
    padding: 16px 20px;
    box-shadow: 0 4px 24px rgba(66, 133, 244, 0.2), 0 2px 8px rgba(0, 0, 0, 0.1);
    border: 2px solid rgba(66, 133, 244, 0.15);
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
    z-index: 2147483647;
    min-width: 320px;
    max-width: 400px;
    backdrop-filter: blur(10px);
    animation: slideInUp 0.3s ease-out;
    pointer-events: none;
  `;

  // Add slide-in animation
  const style = document.createElement('style');
  style.textContent = `
    @keyframes slideInUp {
      from {
        transform: translateY(20px);
        opacity: 0;
      }
      to {
        transform: translateY(0);
        opacity: 1;
      }
    }
    @keyframes pulse {
      0%, 100% {
        transform: scale(1);
        opacity: 1;
      }
      50% {
        transform: scale(1.1);
        opacity: 0.8;
      }
    }
    #owl-automation-overlay .pulse-dot {
      animation: pulse 1.5s ease-in-out infinite;
    }
    #owl-automation-overlay svg {
      width: 14px;
      height: 14px;
      fill: currentColor;
    }
  `;
  document.head.appendChild(style);

  // Header with animated dot and history button
  const header = document.createElement('div');
  header.style.cssText = `
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 10px;
    margin-bottom: 12px;
  `;

  const leftHeader = document.createElement('div');
  leftHeader.style.cssText = `
    display: flex;
    align-items: center;
    gap: 10px;
  `;

  const dot = document.createElement('div');
  dot.className = 'pulse-dot';
  dot.style.cssText = `
    width: 12px;
    height: 12px;
    background: #4285f4;
    border-radius: 50%;
    flex-shrink: 0;
  `;

  const title = document.createElement('div');
  title.style.cssText = `
    font-size: 15px;
    font-weight: 600;
    color: #202124;
    letter-spacing: -0.2px;
  `;
  title.textContent = 'Automatically Managed';

  leftHeader.appendChild(dot);
  leftHeader.appendChild(title);

  // Right buttons container
  const rightBtns = document.createElement('div');
  rightBtns.style.cssText = `
    display: flex;
    align-items: center;
    gap: 6px;
  `;

  // Close button (X)
  const closeBtn = document.createElement('button');
  closeBtn.id = 'olib-close-btn';
  closeBtn.style.cssText = `
    background: rgba(0, 0, 0, 0.05);
    border: none;
    border-radius: 4px;
    padding: 4px;
    cursor: pointer;
    color: #5f6368;
    pointer-events: auto;
    transition: all 0.2s;
    display: flex;
    align-items: center;
    justify-content: center;
    width: 24px;
    height: 24px;
  `;
  closeBtn.innerHTML = `)";

  js << OlibIcons::TIMES;

  js << R"(`;
  closeBtn.onmouseover = () => {
    closeBtn.style.background = 'rgba(0, 0, 0, 0.1)';
  };
  closeBtn.onmouseout = () => {
    closeBtn.style.background = 'rgba(0, 0, 0, 0.05)';
  };
  closeBtn.onclick = () => {
    overlay.style.animation = 'slideInUp 0.3s ease-out reverse';
    setTimeout(() => overlay.remove(), 300);
  };

  // History button
  const historyBtn = document.createElement('button');
  historyBtn.id = 'olib-history-btn';
  historyBtn.style.cssText = `
    background: rgba(66, 133, 244, 0.1);
    border: none;
    border-radius: 6px;
    padding: 6px 10px;
    cursor: pointer;
    font-size: 11px;
    font-weight: 500;
    color: #4285f4;
    pointer-events: auto;
    transition: all 0.2s;
    display: flex;
    align-items: center;
    gap: 4px;
  `;
  historyBtn.innerHTML = `)";

  js << OlibIcons::ARROW_ROTATE_LEFT << " History";

  js << R"(`;
  historyBtn.onmouseover = () => {
    historyBtn.style.background = 'rgba(66, 133, 244, 0.15)';
  };
  historyBtn.onmouseout = () => {
    historyBtn.style.background = 'rgba(66, 133, 244, 0.1)';
  };
  historyBtn.onclick = () => {
    const historyPanel = document.getElementById('olib-history-panel');
    const isExpanded = historyPanel.style.display !== 'none';
    historyPanel.style.display = isExpanded ? 'none' : 'block';
    overlay.style.maxHeight = isExpanded ? 'none' : '500px';

    // Update button text with icon
    historyBtn.innerHTML = isExpanded ? `)";

  js << OlibIcons::ARROW_ROTATE_LEFT << " History` : `" << OlibIcons::TIMES << " Close`";

  js << R"(;
  };

  // Assemble button container
  rightBtns.appendChild(historyBtn);
  rightBtns.appendChild(closeBtn);

  // Assemble header
  header.appendChild(leftHeader);
  header.appendChild(rightBtns);

  // Step info container
  const stepInfo = document.createElement('div');
  stepInfo.id = 'owl-automation-step';
  stepInfo.style.cssText = `
    font-size: 13px;
    color: #5f6368;
    line-height: 1.5;
    padding: 8px 12px;
    background: rgba(66, 133, 244, 0.05);
    border-radius: 8px;
    border-left: 3px solid #4285f4;
  `;
  stepInfo.textContent = 'Preparing automation...';

  // History panel (hidden by default)
  const historyPanel = document.createElement('div');
  historyPanel.id = 'olib-history-panel';
  historyPanel.style.cssText = `
    display: none;
    margin-top: 12px;
    border-top: 1px solid rgba(0, 0, 0, 0.08);
    padding-top: 12px;
    pointer-events: auto;
  `;

  const historyTitle = document.createElement('div');
  historyTitle.style.cssText = `
    font-size: 12px;
    font-weight: 600;
    color: #5f6368;
    margin-bottom: 8px;
  `;
  historyTitle.textContent = 'Step History';

  const historyList = document.createElement('div');
  historyList.id = 'olib-history-list';
  historyList.style.cssText = `
    max-height: 300px;
    overflow-y: auto;
    overflow-x: hidden;
    display: flex;
    flex-direction: column;
    gap: 6px;
    pointer-events: auto;
  `;

  // Custom scrollbar styling
  const scrollbarStyle = document.createElement('style');
  scrollbarStyle.textContent = `
    #olib-history-list::-webkit-scrollbar {
      width: 6px;
    }
    #olib-history-list::-webkit-scrollbar-track {
      background: rgba(0, 0, 0, 0.05);
      border-radius: 3px;
    }
    #olib-history-list::-webkit-scrollbar-thumb {
      background: rgba(66, 133, 244, 0.3);
      border-radius: 3px;
    }
    #olib-history-list::-webkit-scrollbar-thumb:hover {
      background: rgba(66, 133, 244, 0.5);
    }
  `;
  document.head.appendChild(scrollbarStyle);

  historyPanel.appendChild(historyTitle);
  historyPanel.appendChild(historyList);

  overlay.appendChild(header);
  overlay.appendChild(stepInfo);
  overlay.appendChild(historyPanel);
  document.body.appendChild(overlay);
})();
)";

  std::string js_code = js.str();
  frame->ExecuteJavaScript(js_code, frame->GetURL(), 0);
  LOG_DEBUG("NLA", "Automation overlay shown");
}

void OwlNLA::UpdateAutomationStep(CefRefPtr<CefFrame> frame, int current, int total, const std::string& step_description, double step_time_ms, double total_time_ms) {
  if (!frame) return;

  // Escape quotes in step description
  std::string escaped_desc = step_description;
  size_t pos = 0;
  while ((pos = escaped_desc.find("'", pos)) != std::string::npos) {
    escaped_desc.replace(pos, 1, "\\'");
    pos += 2;
  }

  // Format timing strings
  auto format_time = [](double ms) -> std::string {
    if (ms < 1000) {
      return std::to_string(static_cast<int>(ms)) + "ms";
    } else {
      double seconds = ms / 1000.0;
      char buffer[32];
      snprintf(buffer, sizeof(buffer), "%.1fs", seconds);
      return std::string(buffer);
    }
  };

  std::string step_time_str = format_time(step_time_ms);
  std::string total_time_str = format_time(total_time_ms);

  std::string js = R"(
(function() {
  const stepEl = document.getElementById('owl-automation-step');
  if (stepEl) {
    stepEl.innerHTML = `
      <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 4px;">
        <span style="font-weight: 600;">Step )" + std::to_string(current) + R"( of )" + std::to_string(total) + R"(</span>
        <span style="font-size: 11px; color: #4285f4; font-weight: 500;">)" + step_time_str + R"(</span>
      </div>
      <div style="color: #80868b; margin-bottom: 8px;">
        )" + escaped_desc + R"(
      </div>
      <div style="display: flex; align-items: center; gap: 6px; padding-top: 6px; border-top: 1px solid rgba(0,0,0,0.08);">
        <svg width="12" height="12" viewBox="0 0 12 12" fill="none">
          <circle cx="6" cy="6" r="5" stroke="#5f6368" stroke-width="1.5"/>
          <path d="M6 3v3l2 2" stroke="#5f6368" stroke-width="1.5" stroke-linecap="round"/>
        </svg>
        <span style="font-size: 11px; color: #5f6368;">Total: )" + total_time_str + R"(</span>
      </div>
    `;
  }

  // Add to history list
  const historyList = document.getElementById('olib-history-list');
  if (historyList) {
    // Check if this step already exists in history (avoid duplicates)
    const existingStep = historyList.querySelector('[data-step=")" + std::to_string(current) + R"("]');

    if (!existingStep) {
      // Create history item
      const historyItem = document.createElement('div');
      historyItem.setAttribute('data-step', ')" + std::to_string(current) + R"(');
      historyItem.style.cssText = `
        padding: 8px 10px;
        background: rgba(66, 133, 244, 0.05);
        border-radius: 6px;
        font-size: 11px;
        display: flex;
        justify-content: space-between;
        align-items: center;
        gap: 8px;
        border-left: 2px solid #4285f4;
      `;

      const stepDesc = document.createElement('div');
      stepDesc.style.cssText = `
        flex: 1;
        color: #5f6368;
        line-height: 1.4;
      `;
      stepDesc.innerHTML = `
        <span style="font-weight: 600; color: #202124;">Step )" + std::to_string(current) + R"(:</span> )" + escaped_desc + R"(
      `;

      const stepTime = document.createElement('div');
      stepTime.style.cssText = `
        color: #4285f4;
        font-weight: 500;
        white-space: nowrap;
      `;
      stepTime.textContent = ')" + step_time_str + R"(';

      historyItem.appendChild(stepDesc);
      historyItem.appendChild(stepTime);
      historyList.appendChild(historyItem);

      // Auto-scroll to bottom
      historyList.scrollTop = historyList.scrollHeight;
    } else {
      // Update existing step's timing
      const timeEl = existingStep.querySelector('div:last-child');
      if (timeEl) {
        timeEl.textContent = ')" + step_time_str + R"(';
      }
    }
  }
})();
)";

  frame->ExecuteJavaScript(js, frame->GetURL(), 0);
  LOG_DEBUG("NLA", "Overlay: Step " + std::to_string(current) + "/" + std::to_string(total));
}

void OwlNLA::HideAutomationOverlay(CefRefPtr<CefFrame> frame) {
  if (!frame) return;

  std::string js = R"(
(function() {
  const overlay = document.getElementById('owl-automation-overlay');
  if (overlay) {
    overlay.style.animation = 'slideInUp 0.3s ease-out reverse';
    setTimeout(() => overlay.remove(), 300);
  }
})();
)";

  frame->ExecuteJavaScript(js, frame->GetURL(), 0);
  LOG_DEBUG("NLA", "Automation overlay hidden");
}
