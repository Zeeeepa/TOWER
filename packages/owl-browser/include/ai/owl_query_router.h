#ifndef OWL_QUERY_ROUTER_H_
#define OWL_QUERY_ROUTER_H_

#include <string>
#include <vector>
#include "include/cef_browser.h"
#include "include/cef_frame.h"

// Smart query routing system for AI Agent overlay
// Determines the best tool(s) to use based on user intent

enum class QueryType {
  INFORMATIONAL,    // Asking for information (use QueryPage or Demographics)
  ACTION,           // Performing browser actions (use NLA)
  MIXED             // Combination of both
};

enum class ToolSelection {
  QUERY_PAGE,       // Ask questions about current page
  SUMMARIZE_PAGE,   // Summarize current page
  GET_WEATHER,      // Get weather information
  GET_LOCATION,     // Get location information
  GET_DEMOGRAPHICS, // Get all demographics (location, weather, time)
  NLA               // Execute browser automation
};

struct QueryAnalysis {
  QueryType query_type;
  std::vector<ToolSelection> tools_to_use;
  std::string reasoning;           // Why these tools were chosen
  bool needs_current_page;         // Does it need info from current page?
  bool needs_demographics;         // Does it need location/weather/time?
};

struct QueryResponse {
  bool success;
  std::string response_text;       // Text response to show user
  std::string error;
  bool has_actions;                // True if NLA actions were executed
  bool should_summarize_result;    // True if we should summarize page after actions
  std::vector<std::string> action_descriptions;  // Human-readable action list
};

class OwlQueryRouter {
public:
  // Main entry point: Analyze query and execute appropriate tools
  static QueryResponse RouteAndExecute(
      CefRefPtr<CefBrowser> browser,
      const std::string& user_query);

private:
  // Step 1: Analyze user query to determine intent and tools needed
  static QueryAnalysis AnalyzeQuery(const std::string& query);

  // Step 2: Execute the selected tools
  static QueryResponse ExecuteTools(
      CefRefPtr<CefBrowser> browser,
      const std::string& query,
      const QueryAnalysis& analysis);

  // Tool executors
  static std::string ExecuteQueryPage(CefRefPtr<CefFrame> frame, const std::string& query);
  static std::string ExecuteSummarizePage(CefRefPtr<CefFrame> frame);
  static std::string ExecuteGetWeather();
  static std::string ExecuteGetLocation();
  static std::string ExecuteGetDemographics();
  static QueryResponse ExecuteNLA(CefRefPtr<CefBrowser> browser, const std::string& command);

  // Helpers
  static bool ContainsWeatherKeywords(const std::string& query);
  static bool ContainsLocationKeywords(const std::string& query);
  static bool ContainsActionKeywords(const std::string& query);
  static bool ContainsQuestionKeywords(const std::string& query);
  static std::string FormatWeatherResponse(const std::string& weather_json);
  static std::string FormatLocationResponse(const std::string& location_json);
  static std::string FormatDemographicsResponse(const std::string& demographics_json);

  // CAPTCHA solving
  static QueryResponse SolveCaptcha(CefRefPtr<CefBrowser> browser);
};

#endif  // OWL_QUERY_ROUTER_H_
