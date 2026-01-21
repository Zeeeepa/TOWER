#include "owl_query_router.h"
#include "owl_ai_intelligence.h"
#include "owl_demographics.h"
#include "owl_nla.h"
#include "owl_browser_manager.h"
#include "logger.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>

// Convert string to lowercase for comparison
static std::string ToLower(const std::string& str) {
  std::string result = str;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

QueryResponse OwlQueryRouter::RouteAndExecute(
    CefRefPtr<CefBrowser> browser,
    const std::string& user_query) {

  LOG_DEBUG("QueryRouter", "Analyzing user query: " + user_query);

  // Step 1: Analyze the query
  QueryAnalysis analysis = AnalyzeQuery(user_query);

  LOG_DEBUG("QueryRouter", "Query type: " + std::to_string(static_cast<int>(analysis.query_type)));
  LOG_DEBUG("QueryRouter", "Tools to use: " + std::to_string(analysis.tools_to_use.size()));
  LOG_DEBUG("QueryRouter", "Reasoning: " + analysis.reasoning);

  // Step 2: Execute the selected tools
  QueryResponse response = ExecuteTools(browser, user_query, analysis);

  return response;
}

QueryAnalysis OwlQueryRouter::AnalyzeQuery(const std::string& query) {
  QueryAnalysis analysis;
  std::string query_lower = ToLower(query);

  // Check for different types of queries
  bool has_weather = ContainsWeatherKeywords(query_lower);
  bool has_location = ContainsLocationKeywords(query_lower);
  bool has_action = ContainsActionKeywords(query_lower);
  bool has_question = ContainsQuestionKeywords(query_lower);

  // Check for CAPTCHA solving
  bool has_captcha = (query_lower.find("solve") != std::string::npos &&
                      query_lower.find("captcha") != std::string::npos);

  // Determine query type and tools
  if (has_captcha) {
    analysis.query_type = QueryType::ACTION;
    analysis.tools_to_use.push_back(ToolSelection::NLA);
    analysis.needs_current_page = true;
    analysis.needs_demographics = false;
    analysis.reasoning = "Query requests CAPTCHA solving";
  }
  else if (has_weather) {
    analysis.query_type = QueryType::INFORMATIONAL;
    analysis.tools_to_use.push_back(ToolSelection::GET_WEATHER);
    analysis.needs_demographics = true;
    analysis.needs_current_page = false;
    analysis.reasoning = "Query asks about weather";
  }
  else if (has_location && !has_action) {
    // Just asking about location (not "find nearby X")
    analysis.query_type = QueryType::INFORMATIONAL;
    analysis.tools_to_use.push_back(ToolSelection::GET_LOCATION);
    analysis.needs_demographics = true;
    analysis.needs_current_page = false;
    analysis.reasoning = "Query asks about location";
  }
  else if (has_question && (query_lower.find("this") != std::string::npos ||
                            query_lower.find("page") != std::string::npos ||
                            query_lower.find("website") != std::string::npos ||
                            query_lower.find("site") != std::string::npos)) {
    // Asking about current page
    analysis.query_type = QueryType::INFORMATIONAL;
    analysis.tools_to_use.push_back(ToolSelection::QUERY_PAGE);
    analysis.needs_current_page = true;
    analysis.needs_demographics = false;
    analysis.reasoning = "Query asks about current page content";
  }
  else if (has_action || query_lower.find("find") != std::string::npos ||
           query_lower.find("search") != std::string::npos ||
           query_lower.find("go to") != std::string::npos ||
           query_lower.find("navigate") != std::string::npos ||
           query_lower.find("click") != std::string::npos) {
    // Action-based query
    analysis.query_type = QueryType::ACTION;
    analysis.tools_to_use.push_back(ToolSelection::NLA);
    analysis.needs_current_page = false;
    analysis.needs_demographics = false;
    analysis.reasoning = "Query requires browser automation";

    // If it's "find nearby X", we also need demographics for context
    if (query_lower.find("nearby") != std::string::npos ||
        query_lower.find("near me") != std::string::npos ||
        query_lower.find("around me") != std::string::npos) {
      analysis.needs_demographics = true;
      analysis.query_type = QueryType::MIXED;
      analysis.reasoning = "Query requires location-aware browser automation";
    }
  }
  else if (query_lower.find("summarize") != std::string::npos ||
           query_lower.find("summary") != std::string::npos) {
    analysis.query_type = QueryType::INFORMATIONAL;
    analysis.tools_to_use.push_back(ToolSelection::SUMMARIZE_PAGE);
    analysis.needs_current_page = true;
    analysis.needs_demographics = false;
    analysis.reasoning = "Query asks for page summary";
  }
  else {
    // Default to NLA for anything else
    analysis.query_type = QueryType::ACTION;
    analysis.tools_to_use.push_back(ToolSelection::NLA);
    analysis.needs_current_page = false;
    analysis.needs_demographics = false;
    analysis.reasoning = "Default to browser automation";
  }

  return analysis;
}

QueryResponse OwlQueryRouter::ExecuteTools(
    CefRefPtr<CefBrowser> browser,
    const std::string& query,
    const QueryAnalysis& analysis) {

  QueryResponse response;
  response.success = true;
  response.has_actions = false;
  response.should_summarize_result = false;

  CefRefPtr<CefFrame> frame = browser->GetMainFrame();

  // Execute each selected tool
  for (const auto& tool : analysis.tools_to_use) {
    switch (tool) {
      case ToolSelection::QUERY_PAGE: {
        std::string result = ExecuteQueryPage(frame, query);
        response.response_text = result;
        LOG_DEBUG("QueryRouter", "QueryPage result: " + result.substr(0, std::min((size_t)200, result.length())));
        break;
      }

      case ToolSelection::SUMMARIZE_PAGE: {
        std::string result = ExecuteSummarizePage(frame);
        response.response_text = result;
        LOG_DEBUG("QueryRouter", "SummarizePage result: " + result.substr(0, std::min((size_t)200, result.length())));
        break;
      }

      case ToolSelection::GET_WEATHER: {
        std::string result = ExecuteGetWeather();
        response.response_text = result;
        LOG_DEBUG("QueryRouter", "GetWeather result: " + result);
        break;
      }

      case ToolSelection::GET_LOCATION: {
        std::string result = ExecuteGetLocation();
        response.response_text = result;
        LOG_DEBUG("QueryRouter", "GetLocation result: " + result);
        break;
      }

      case ToolSelection::GET_DEMOGRAPHICS: {
        std::string result = ExecuteGetDemographics();
        response.response_text = result;
        LOG_DEBUG("QueryRouter", "GetDemographics result: " + result);
        break;
      }

      case ToolSelection::NLA: {
        // NLA returns a different type of response (actions, not text)
        QueryResponse nla_response = ExecuteNLA(browser, query);

        // Preserve the has_actions flag from the NLA response (important for CAPTCHA solving)
        response.has_actions = nla_response.has_actions;
        response.action_descriptions = nla_response.action_descriptions;
        response.should_summarize_result = nla_response.should_summarize_result;

        if (!nla_response.success) {
          response.success = false;
          response.error = nla_response.error;
        }

        // If NLA returned a text response (e.g., CAPTCHA solve result), use it
        if (!nla_response.response_text.empty()) {
          response.response_text = nla_response.response_text;
        }

        // Check if this is a search/find query that should show results summary
        std::string query_lower = ToLower(query);
        if (query_lower.find("find") != std::string::npos ||
            query_lower.find("search") != std::string::npos ||
            query_lower.find("show me") != std::string::npos ||
            query_lower.find("look for") != std::string::npos) {
          response.should_summarize_result = true;
          LOG_DEBUG("QueryRouter", "Will summarize page after NLA completes");
        }

        LOG_DEBUG("QueryRouter", "NLA executed with " + std::to_string(nla_response.action_descriptions.size()) + " actions");
        break;
      }
    }
  }

  return response;
}

std::string OwlQueryRouter::ExecuteQueryPage(CefRefPtr<CefFrame> frame, const std::string& query) {
  return OwlAIIntelligence::QueryPage(frame, query);
}

std::string OwlQueryRouter::ExecuteSummarizePage(CefRefPtr<CefFrame> frame) {
  return OwlAIIntelligence::SummarizePage(frame, false);
}

std::string OwlQueryRouter::ExecuteGetWeather() {
  OwlDemographics* demo = OwlDemographics::GetInstance();
  if (!demo || !demo->IsReady()) {
    return "Weather information is not available (demographics system not initialized).";
  }

  WeatherInfo weather = demo->GetWeather();
  if (!weather.success) {
    return "Could not retrieve weather information: " + weather.error;
  }

  // Format friendly response
  std::ostringstream response;
  response << "Current Weather:\n";
  response << "Temperature: " << std::fixed << std::setprecision(1)
           << weather.temperature_celsius << "°C ("
           << weather.temperature_fahrenheit << "°F)\n";
  response << "Condition: " << weather.condition << "\n";
  if (!weather.description.empty()) {
    response << "Details: " << weather.description << "\n";
  }
  response << "Humidity: " << std::fixed << std::setprecision(0)
           << weather.humidity_percent << "%\n";
  response << "Wind Speed: " << std::fixed << std::setprecision(1)
           << weather.wind_speed_kmh << " km/h ("
           << weather.wind_speed_mph << " mph)";

  return response.str();
}

std::string OwlQueryRouter::ExecuteGetLocation() {
  OwlDemographics* demo = OwlDemographics::GetInstance();
  if (!demo || !demo->IsReady()) {
    return "Location information is not available (demographics system not initialized).";
  }

  GeoLocationInfo location = demo->GetGeoLocation();
  if (!location.success) {
    return "Could not retrieve location information: " + location.error;
  }

  // Format friendly response
  std::ostringstream response;
  response << "Your Location:\n";
  if (!location.city.empty()) {
    response << "City: " << location.city << "\n";
  }
  if (!location.region.empty()) {
    response << "Region: " << location.region << "\n";
  }
  response << "Country: " << location.country_name << " (" << location.country_code << ")\n";
  if (!location.postal_code.empty()) {
    response << "Postal Code: " << location.postal_code << "\n";
  }
  response << "Coordinates: " << std::fixed << std::setprecision(4)
           << location.latitude << ", " << location.longitude << "\n";
  response << "Timezone: " << location.timezone;

  return response.str();
}

std::string OwlQueryRouter::ExecuteGetDemographics() {
  OwlDemographics* demo = OwlDemographics::GetInstance();
  if (!demo || !demo->IsReady()) {
    return "Demographics information is not available (system not initialized).";
  }

  DemographicInfo info = demo->GetAllInfo();

  std::ostringstream response;
  response << "Your Context:\n\n";

  // Date & Time
  response << "Date & Time:\n";
  response << "Current: " << info.datetime.date << " " << info.datetime.time << "\n";
  response << "Day: " << info.datetime.day_of_week << "\n";
  response << "Timezone: " << info.datetime.timezone << "\n\n";

  // Location
  if (info.has_location) {
    response << "Location:\n";
    if (!info.location.city.empty()) {
      response << "City: " << info.location.city << "\n";
    }
    if (!info.location.region.empty()) {
      response << "Region: " << info.location.region << "\n";
    }
    response << "Country: " << info.location.country_name << "\n\n";
  }

  // Weather
  if (info.has_weather) {
    response << "Weather:\n";
    response << "Temperature: " << std::fixed << std::setprecision(1)
             << info.weather.temperature_celsius << "°C\n";
    response << "Condition: " << info.weather.condition << "\n";
    response << "Humidity: " << std::fixed << std::setprecision(0)
             << info.weather.humidity_percent << "%";
  }

  return response.str();
}

QueryResponse OwlQueryRouter::SolveCaptcha(CefRefPtr<CefBrowser> browser) {
  QueryResponse response;
  response.success = true;
  response.has_actions = false;
  response.should_summarize_result = false;

  OwlBrowserManager* manager = OwlBrowserManager::GetInstance();

  // Get context ID from browser
  std::ostringstream ctx_stream;
  ctx_stream << "ctx_" << std::setfill('0') << std::setw(6) << browser->GetIdentifier();
  std::string context_id = ctx_stream.str();

  LOG_DEBUG("QueryRouter", "Solving CAPTCHA for context: " + context_id);

  // Detect CAPTCHA
  std::string detection_result = manager->DetectCaptcha(context_id);
  LOG_DEBUG("QueryRouter", "Detection result: " + detection_result);

  if (detection_result.find("\"detected\":false") != std::string::npos ||
      detection_result.find("\"detected\": false") != std::string::npos) {
    response.success = false;
    response.error = "No CAPTCHA detected on the page";
    response.response_text = "I couldn't find a CAPTCHA on this page.";
    return response;
  }

  // Classify CAPTCHA type
  std::string classification_result = manager->ClassifyCaptcha(context_id);
  LOG_DEBUG("QueryRouter", "Classification result: " + classification_result);

  std::string captcha_type = "unknown";

  if (classification_result.find("\"type\":\"text-based\"") != std::string::npos ||
      classification_result.find("\"type\": \"text-based\"") != std::string::npos) {
    captcha_type = "text";
  } else if (classification_result.find("\"type\":\"image-selection\"") != std::string::npos ||
             classification_result.find("\"type\": \"image-selection\"") != std::string::npos) {
    captcha_type = "image";
  }

#ifdef OWL_DEBUG_BUILD
  bool has_checkbox = (classification_result.find("\"checkbox_selector\"") != std::string::npos);
  LOG_DEBUG("QueryRouter", "CAPTCHA type: " + captcha_type + ", has_checkbox: " + (has_checkbox ? "true" : "false"));
#endif

  // The solvers (text and image) handle scrolling and clicking internally
  // We just need to call the appropriate solver
  // Solve based on type
  std::string solve_result;
  if (captcha_type == "text") {
    solve_result = manager->SolveTextCaptcha(context_id, 3);
  } else if (captcha_type == "image") {
    solve_result = manager->SolveImageCaptcha(context_id, 3);
  } else {
    // Try auto-solve
    solve_result = manager->SolveCaptcha(context_id, 3);
  }

  LOG_DEBUG("QueryRouter", "Solve result: " + solve_result);

  // Check success
  if (solve_result.find("\"success\":true") != std::string::npos ||
      solve_result.find("\"success\": true") != std::string::npos) {
    response.success = true;
    response.response_text = "✓ CAPTCHA solved successfully!";
  } else {
    response.success = false;
    response.error = "Failed to solve CAPTCHA";
    response.response_text = "I tried to solve the CAPTCHA but it failed. Please try manually.";
  }

  return response;
}

QueryResponse OwlQueryRouter::ExecuteNLA(CefRefPtr<CefBrowser> browser, const std::string& command) {
  QueryResponse response;
  response.success = true;
  response.has_actions = true;
  response.should_summarize_result = false;

  // Check if this is a CAPTCHA solve request
  std::string command_lower = ToLower(command);
  if (command_lower.find("solve") != std::string::npos &&
      command_lower.find("captcha") != std::string::npos) {
    // Handle CAPTCHA solving
    return SolveCaptcha(browser);
  }

  // Execute NLA
  std::string result = OwlNLA::ExecuteCommand(browser->GetMainFrame(), command);

  // Check if it succeeded
  if (result.find("Error") != std::string::npos || result.find("error") != std::string::npos) {
    response.success = false;
    response.error = result;
  } else {
    response.success = true;
    // Don't set response_text - actions speak for themselves via task list
  }

  return response;
}

// Helper functions for keyword detection

bool OwlQueryRouter::ContainsWeatherKeywords(const std::string& query) {
  return query.find("weather") != std::string::npos ||
         query.find("temperature") != std::string::npos ||
         query.find("forecast") != std::string::npos ||
         query.find("hot") != std::string::npos ||
         query.find("cold") != std::string::npos ||
         query.find("sunny") != std::string::npos ||
         query.find("rainy") != std::string::npos ||
         query.find("rain") != std::string::npos ||
         query.find("snow") != std::string::npos ||
         query.find("cloudy") != std::string::npos;
}

bool OwlQueryRouter::ContainsLocationKeywords(const std::string& query) {
  return (query.find("where") != std::string::npos &&
          (query.find("am i") != std::string::npos || query.find("i am") != std::string::npos)) ||
         query.find("my location") != std::string::npos ||
         query.find("current location") != std::string::npos ||
         query.find("what city") != std::string::npos ||
         query.find("what country") != std::string::npos;
}

bool OwlQueryRouter::ContainsActionKeywords(const std::string& query) {
  return query.find("click") != std::string::npos ||
         query.find("type") != std::string::npos ||
         query.find("enter") != std::string::npos ||
         query.find("submit") != std::string::npos ||
         query.find("fill") != std::string::npos ||
         query.find("scroll") != std::string::npos ||
         query.find("solve") != std::string::npos;
}

bool OwlQueryRouter::ContainsQuestionKeywords(const std::string& query) {
  return query.find("what") != std::string::npos ||
         query.find("how") != std::string::npos ||
         query.find("why") != std::string::npos ||
         query.find("when") != std::string::npos ||
         query.find("where") != std::string::npos ||
         query.find("who") != std::string::npos ||
         query.find("does") != std::string::npos ||
         query.find("is") != std::string::npos ||
         query.find("can") != std::string::npos ||
         query.find("?") != std::string::npos;
}
