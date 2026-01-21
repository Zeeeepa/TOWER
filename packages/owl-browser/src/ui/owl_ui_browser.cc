#include "owl_ui_browser.h"
#include "owl_agent_controller.h"
#include "owl_nla.h"
#include "owl_browser_manager.h"
#include "action_result.h"
#include "owl_ui_delegate.h"
#include "owl_playground_window.h"
#include "owl_task_state.h"
#include "owl_dev_console.h"
#include "owl_captcha_utils.h"
#include "owl_proxy_manager.h"
#include "logger.h"
#include "json.hpp"  // nlohmann/json for proper JSON parsing
#include "include/cef_browser.h"
#include "include/cef_task.h"
#include "include/cef_parser.h"
#include "include/cef_request_context.h"
#include "include/wrapper/cef_helpers.h"
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <map>
#include <curl/curl.h>
#include <regex>
#include <set>

using json = nlohmann::json;

#if defined(OS_MACOS)
// Forward declare helper function (implemented in owl_ui_delegate.mm)
extern void SetBrowserWindowTitle(CefRefPtr<CefBrowser> browser, const std::string& title);
#endif

// Initialize static playground instance
CefRefPtr<OwlUIBrowser> OwlUIBrowser::playground_instance_ = nullptr;

// Initialize static main browser counter
int OwlUIBrowser::main_browser_count_ = 0;

// C function to clear playground instance (callable from Objective-C)
// Only define on macOS - Linux has its own definition in owl_playground_window_linux.cc
#if defined(OS_MACOS)
extern "C" void ClearPlaygroundInstance() {
  LOG_DEBUG("UIBrowser", "ClearPlaygroundInstance called from windowWillClose");
  OwlUIBrowser::SetPlaygroundInstance(nullptr);
}
#endif

// Global map for pending element picker operations
// Maps browser ID -> (playground browser, input ID)
std::map<int, std::pair<CefRefPtr<CefBrowser>, std::string>> g_pending_pickers;

// Forward declaration of LLMTestResultTask (used by test_llm_connection handler)
class LLMTestResultTask : public CefTask {
 public:
  LLMTestResultTask(CefRefPtr<CefFrame> frame,
                    const std::string& status,
                    const std::string& message)
      : frame_(frame),
        status_(status),
        message_(message) {}

  void Execute() override {
    if (!frame_ || !frame_->IsValid()) return;

    // Escape quotes in message for JavaScript
    std::string escaped_message = message_;
    size_t pos = 0;
    while ((pos = escaped_message.find("'", pos)) != std::string::npos) {
      escaped_message.replace(pos, 1, "\\'");
      pos += 2;
    }
    pos = 0;
    while ((pos = escaped_message.find("\n", pos)) != std::string::npos) {
      escaped_message.replace(pos, 1, " ");
      pos += 1;
    }

    std::string js = "if (window.onLLMTestResult) { window.onLLMTestResult('" + status_ + "', '" + escaped_message + "'); }";
    frame_->ExecuteJavaScript(js, frame_->GetURL(), 0);
  }

 private:
  CefRefPtr<CefFrame> frame_;
  std::string status_;
  std::string message_;

  IMPLEMENT_REFCOUNTING(LLMTestResultTask);
};

OwlUIBrowser::OwlUIBrowser()
    : agent_mode_(false),
      sidebar_visible_(true),
      current_url_("owl://homepage.html"),
      current_title_("Owl Browser"),
      is_playground_(false),
      main_browser_(nullptr),
      playground_window_handle_(nullptr),
      accumulated_dom_elements_(""),
      expected_dom_total_(0) {
}

OwlUIBrowser::~OwlUIBrowser() {
}

bool OwlUIBrowser::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {

  const std::string& message_name = message->GetName();
  LOG_DEBUG("UIBrowser", "OnProcessMessageReceived: " + message_name);

  // Handle execute_nla message from homepage
  if (message_name == "execute_nla" && source_process == PID_RENDERER) {
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    if (args && args->GetSize() > 0) {
      std::string command = args->GetString(0).ToString();
      LOG_DEBUG("UIBrowser", "Received NLA command from homepage: " + command);
      ExecuteAgentPrompt(command);
      return true;
    }
  }

  // Handle open_playground message from homepage
  if (message_name == "open_playground" && source_process == PID_RENDERER) {
    LOG_DEBUG("UIBrowser", "Opening Developer Playground window");

    // Check if playground already exists and is still valid
    CefRefPtr<OwlUIBrowser> existing_playground = GetPlaygroundInstance();
    LOG_DEBUG("UIBrowser", "Checking existing playground instance: " + std::string(existing_playground ? "EXISTS" : "NULL"));

    if (existing_playground) {
      CefRefPtr<CefBrowser> browser_ref = existing_playground->GetBrowser();
      LOG_DEBUG("UIBrowser", "Existing playground browser: " + std::string(browser_ref ? "VALID" : "NULL"));

      if (browser_ref) {
        // Focus existing playground window
        LOG_DEBUG("UIBrowser", "Playground already open, focusing window");
        existing_playground->FocusWindow();
        return true;
      }

      // Clear stale instance if browser is invalid
      LOG_DEBUG("UIBrowser", "Clearing stale playground instance (browser is NULL)");
      SetPlaygroundInstance(nullptr);
    }

    // Create a new browser window for the playground
    CefRefPtr<OwlUIBrowser> playground_browser(new OwlUIBrowser);
    playground_browser->SetAsPlayground();
    playground_browser->SetMainBrowser(browser_);
    playground_browser->CreateBrowserWindow("owl://playground.html");

    // Store playground instance
    SetPlaygroundInstance(playground_browser);

    return true;
  }

  // Handle execute_test message from playground
  if (message_name == "execute_test" && source_process == PID_RENDERER) {
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    if (args && args->GetSize() > 0) {
      std::string test_json = args->GetString(0).ToString();
      LOG_DEBUG("UIBrowser", "Received test execution request");

      // Execute test asynchronously
      ExecuteTest(test_json, browser);
      return true;
    }
  }

  // Handle start_element_picker message from playground
  if (message_name == "start_element_picker" && source_process == PID_RENDERER) {
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    if (args && args->GetSize() > 0) {
      std::string input_id = args->GetString(0).ToString();
      LOG_DEBUG("UIBrowser", "Starting element picker for input: " + input_id);

      // Bring main browser window to front
      if (main_browser_) {
        BringBrowserToFront(main_browser_);

        // Inject element picker overlay into main browser
        CefRefPtr<CefFrame> main_frame = main_browser_->GetMainFrame();
        if (main_frame) {
          InjectElementPickerOverlay(main_frame, browser, input_id);
        }
      }
      return true;
    }
  }

  // Handle start_position_picker message from playground
  if (message_name == "start_position_picker" && source_process == PID_RENDERER) {
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    if (args && args->GetSize() > 0) {
      std::string input_id = args->GetString(0).ToString();
      LOG_DEBUG("UIBrowser", "Starting position picker for input: " + input_id);

      // Bring main browser window to front
      if (main_browser_) {
        BringBrowserToFront(main_browser_);

        // Inject position picker overlay into main browser
        CefRefPtr<CefFrame> main_frame = main_browser_->GetMainFrame();
        if (main_frame) {
          InjectPositionPickerOverlay(main_frame, browser, input_id);
        }
      }
      return true;
    }
  }

  // Handle element_picker_result message from main browser (via IPC)
  if (message_name == "element_picker_result" && source_process == PID_RENDERER) {
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    if (args && args->GetSize() > 0) {
      std::string result_json = args->GetString(0).ToString();
      LOG_DEBUG("UIBrowser", "Received element picker result via IPC: " + result_json);

      // Get the pending picker info from the global map
      extern std::map<int, std::pair<CefRefPtr<CefBrowser>, std::string>> g_pending_pickers;
      int browser_id = browser->GetIdentifier();
      auto it = g_pending_pickers.find(browser_id);

      if (it != g_pending_pickers.end()) {
        CefRefPtr<CefBrowser> playground_browser = it->second.first;
        std::string input_id = it->second.second;

        bool is_confirmed = result_json.find("\"action\":\"confirmed\"") != std::string::npos;
        bool is_canceled = result_json.find("\"action\":\"canceled\"") != std::string::npos;

        if (is_confirmed) {
          // Extract selector from JSON
          size_t selector_start = result_json.find("\"selector\":\"");
          if (selector_start != std::string::npos) {
            selector_start += 12;
            size_t selector_end = selector_start;
            bool in_escape = false;
            while (selector_end < result_json.length()) {
              if (in_escape) {
                in_escape = false;
              } else if (result_json[selector_end] == '\\') {
                in_escape = true;
              } else if (result_json[selector_end] == '"') {
                break;
              }
              selector_end++;
            }

            if (selector_end < result_json.length()) {
              std::string selector = result_json.substr(selector_start, selector_end - selector_start);

              // Unescape JSON string
              std::string unescaped_selector;
              for (size_t i = 0; i < selector.length(); i++) {
                if (selector[i] == '\\' && i + 1 < selector.length()) {
                  char next = selector[i + 1];
                  if (next == '"' || next == '\\' || next == '/') {
                    unescaped_selector += next;
                    i++;
                    continue;
                  }
                }
                unescaped_selector += selector[i];
              }

              // Escape for JavaScript
              std::string escaped_selector;
              for (char c : unescaped_selector) {
                if (c == '\\' || c == '\'') {
                  escaped_selector += '\\';
                }
                escaped_selector += c;
              }

              // Forward to playground
              if (playground_browser && playground_browser->GetMainFrame()) {
                std::ostringstream forward_js;
                forward_js << "if (typeof onElementPicked === 'function') { onElementPicked('"
                           << escaped_selector << "'); }";
                playground_browser->GetMainFrame()->ExecuteJavaScript(forward_js.str(),
                                                                       playground_browser->GetMainFrame()->GetURL(), 0);
                LOG_DEBUG("UIBrowser", "Forwarded element picker result to playground: " + unescaped_selector);
                BringBrowserToFront(playground_browser);
              }
            }
          }
        } else if (is_canceled) {
          if (playground_browser && playground_browser->GetMainFrame()) {
            std::string cancel_js = "if (typeof onElementPickerCanceled === 'function') { onElementPickerCanceled(); }";
            playground_browser->GetMainFrame()->ExecuteJavaScript(cancel_js,
                                                                   playground_browser->GetMainFrame()->GetURL(), 0);
            LOG_DEBUG("UIBrowser", "Forwarded element picker cancel to playground");
            BringBrowserToFront(playground_browser);
          }
        }

        g_pending_pickers.erase(it);
      }
      return true;
    }
  }

  // Handle position_picker_result message from main browser (via IPC)
  if (message_name == "position_picker_result" && source_process == PID_RENDERER) {
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    if (args && args->GetSize() > 0) {
      std::string result_json = args->GetString(0).ToString();
      LOG_DEBUG("UIBrowser", "Received position picker result via IPC: " + result_json);

      // Get the pending picker info from the global map
      extern std::map<int, std::pair<CefRefPtr<CefBrowser>, std::string>> g_pending_pickers;
      int browser_id = browser->GetIdentifier();
      auto it = g_pending_pickers.find(browser_id);

      if (it != g_pending_pickers.end()) {
        CefRefPtr<CefBrowser> playground_browser = it->second.first;

        bool is_confirmed = result_json.find("\"action\":\"confirmed\"") != std::string::npos;
        bool is_canceled = result_json.find("\"action\":\"canceled\"") != std::string::npos;

        if (is_confirmed) {
          // Extract position from JSON
          size_t position_start = result_json.find("\"position\":\"");
          if (position_start != std::string::npos) {
            position_start += 12;
            size_t position_end = result_json.find("\"", position_start);
            if (position_end != std::string::npos) {
              std::string position = result_json.substr(position_start, position_end - position_start);

              // Forward to playground
              if (playground_browser && playground_browser->GetMainFrame()) {
                std::ostringstream forward_js;
                forward_js << "if (typeof onPositionPicked === 'function') { onPositionPicked('"
                           << position << "'); }";
                playground_browser->GetMainFrame()->ExecuteJavaScript(forward_js.str(),
                                                                       playground_browser->GetMainFrame()->GetURL(), 0);
                LOG_DEBUG("UIBrowser", "Forwarded position picker result to playground: " + position);
                BringBrowserToFront(playground_browser);
              }
            }
          }
        } else if (is_canceled) {
          if (playground_browser && playground_browser->GetMainFrame()) {
            std::string cancel_js = "if (typeof onPositionPickerCanceled === 'function') { onPositionPickerCanceled(); }";
            playground_browser->GetMainFrame()->ExecuteJavaScript(cancel_js,
                                                                   playground_browser->GetMainFrame()->GetURL(), 0);
            LOG_DEBUG("UIBrowser", "Forwarded position picker cancel to playground");
            BringBrowserToFront(playground_browser);
          }
        }

        g_pending_pickers.erase(it);
      }
      return true;
    }
  }

  // Handle dev_console_result message (JavaScript execution result from dev console)
  if (message_name == "dev_console_result" && source_process == PID_RENDERER) {
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    if (args && args->GetSize() > 0) {
      std::string result_json = args->GetString(0).ToString();
      LOG_DEBUG("UIBrowser", "Received dev console result: " + result_json);

      // Helper lambda to unescape JSON string
      auto unescapeJson = [](const std::string& str) -> std::string {
        std::string result;
        result.reserve(str.size());
        for (size_t i = 0; i < str.size(); ++i) {
          if (str[i] == '\\' && i + 1 < str.size()) {
            switch (str[i + 1]) {
              case 'n':  result += '\n'; ++i; break;
              case 't':  result += '\t'; ++i; break;
              case 'r':  result += '\r'; ++i; break;
              case '"':  result += '"';  ++i; break;
              case '\\': result += '\\'; ++i; break;
              default:   result += str[i]; break;
            }
          } else {
            result += str[i];
          }
        }
        return result;
      };

      // Parse JSON to extract result or error
      size_t result_pos = result_json.find("\"result\":");
      size_t error_pos = result_json.find("\"error\":");

      if (result_pos != std::string::npos) {
        // Extract result value - need to handle nested JSON properly
        size_t start = result_json.find("\"", result_pos + 9);
        if (start != std::string::npos) {
          // Find matching end quote, handling escaped quotes
          size_t end = start + 1;
          while (end < result_json.size()) {
            if (result_json[end] == '"' && result_json[end - 1] != '\\') {
              break;
            }
            end++;
          }
          if (end < result_json.size()) {
            std::string result_value = result_json.substr(start + 1, end - start - 1);
            result_value = unescapeJson(result_value);
            OwlDevConsole::GetInstance()->AddConsoleMessage("info", "← " + result_value, "DevConsole", 0);
          }
        }
      } else if (error_pos != std::string::npos) {
        // Extract error message
        size_t start = result_json.find("\"", error_pos + 8);
        if (start != std::string::npos) {
          size_t end = start + 1;
          while (end < result_json.size()) {
            if (result_json[end] == '"' && result_json[end - 1] != '\\') {
              break;
            }
            end++;
          }
          if (end < result_json.size()) {
            std::string error_msg = result_json.substr(start + 1, end - start - 1);
            error_msg = unescapeJson(error_msg);
            OwlDevConsole::GetInstance()->AddConsoleMessage("error", "✗ " + error_msg, "DevConsole", 0);
          }
        }
      }
      return true;
    }
  }

  // Handle save_llm_config message from settings UI
  if (message_name == "save_llm_config" && source_process == PID_RENDERER) {
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    if (args && args->GetSize() >= 1) {
      std::string config_json = args->GetString(0).ToString();
      LOG_DEBUG("UIBrowser", "Received LLM config JSON: " + config_json);

      // Parse JSON manually (simple format)
      auto extract_bool = [&config_json](const std::string& key) -> bool {
        size_t pos = config_json.find("\"" + key + "\"");
        if (pos == std::string::npos) return false;
        pos = config_json.find(":", pos);
        if (pos == std::string::npos) return false;
        pos = config_json.find_first_not_of(" \t\n\r", pos + 1);
        return (pos != std::string::npos && config_json.substr(pos, 4) == "true");
      };

      auto extract_string = [&config_json](const std::string& key) -> std::string {
        size_t pos = config_json.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        pos = config_json.find(":", pos);
        if (pos == std::string::npos) return "";
        pos = config_json.find("\"", pos + 1);
        if (pos == std::string::npos) return "";
        size_t end_pos = config_json.find("\"", pos + 1);
        if (end_pos == std::string::npos) return "";
        return config_json.substr(pos + 1, end_pos - pos - 1);
      };

      LLMConfig config;
      config.enabled = extract_bool("enabled");
      config.use_builtin = extract_bool("use_builtin");
      config.provider_name = extract_string("provider_name");
      config.external_endpoint = extract_string("external_endpoint");
      config.external_model = extract_string("external_model");
      config.external_api_key = extract_string("external_api_key");
      config.is_third_party = extract_bool("is_third_party");

      LOG_DEBUG("UIBrowser", "Parsed LLM config:");
      LOG_DEBUG("UIBrowser", "  enabled: " + std::string(config.enabled ? "true" : "false"));
      LOG_DEBUG("UIBrowser", "  use_builtin: " + std::string(config.use_builtin ? "true" : "false"));
      LOG_DEBUG("UIBrowser", "  provider_name: " + config.provider_name);
      LOG_DEBUG("UIBrowser", "  endpoint: " + config.external_endpoint);
      LOG_DEBUG("UIBrowser", "  model: " + config.external_model);

      // Save config to file
      bool success = OwlBrowserManager::SaveLLMConfigToFile(config);

      if (success) {
        LOG_DEBUG("UIBrowser", "LLM config saved successfully. Restart required for changes to take effect.");

        // Send response back to UI
        CefRefPtr<CefProcessMessage> response = CefProcessMessage::Create("llm_config_saved");
        CefRefPtr<CefListValue> response_args = response->GetArgumentList();
        response_args->SetString(0, "success");
        frame->SendProcessMessage(PID_RENDERER, response);
      } else {
        LOG_ERROR("UIBrowser", "Failed to save LLM config to file");

        // Send error response back to UI
        CefRefPtr<CefProcessMessage> response = CefProcessMessage::Create("llm_config_saved");
        CefRefPtr<CefListValue> response_args = response->GetArgumentList();
        response_args->SetString(0, "error");
        frame->SendProcessMessage(PID_RENDERER, response);
      }

      return true;
    }
  }

  // Handle load_llm_config message - load saved config and send to UI
  if (message_name == "load_llm_config" && source_process == PID_RENDERER) {
    LOG_DEBUG("UIBrowser", "Loading saved LLM config for homepage");

    // Load config from file
    LLMConfig config = OwlBrowserManager::LoadLLMConfigFromFile();

    // Build JSON response
    std::stringstream json;
    json << "{";
    json << "\"enabled\":" << (config.enabled ? "true" : "false") << ",";
    json << "\"use_builtin\":" << (config.use_builtin ? "true" : "false") << ",";
    json << "\"provider_name\":\"" << config.provider_name << "\",";
    json << "\"external_endpoint\":\"" << config.external_endpoint << "\",";
    json << "\"external_model\":\"" << config.external_model << "\",";
    json << "\"external_api_key\":\"" << config.external_api_key << "\",";
    json << "\"is_third_party\":" << (config.is_third_party ? "true" : "false");
    json << "}";

    std::string config_json = json.str();
    LOG_DEBUG("UIBrowser", "Sending LLM config to homepage: " + config_json);

    // Execute JavaScript callback to load the config
    std::string js = "if (window.onLLMConfigLoaded) { window.onLLMConfigLoaded(" + config_json + "); }";
    frame->ExecuteJavaScript(js, frame->GetURL(), 0);

    return true;
  }

  // Handle test_llm_connection message from settings UI
  if (message_name == "test_llm_connection" && source_process == PID_RENDERER) {
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    if (args && args->GetSize() >= 3) {
      std::string endpoint = args->GetString(0).ToString();
      std::string model = args->GetString(1).ToString();
      std::string api_key = args->GetString(2).ToString();

      LOG_DEBUG("UIBrowser", "Testing LLM connection to: " + endpoint + " with model: " + model);

      // Run test in background thread to avoid blocking UI
      CefRefPtr<CefFrame> response_frame = frame;
      std::thread([endpoint, model, api_key, response_frame]() {
        std::string result_status = "error";
        std::string result_message = "";

        // Build full API URL
        std::string api_url = endpoint;
        if (api_url.find("/v1/chat/completions") == std::string::npos) {
          if (api_url.back() == '/') api_url.pop_back();
          api_url += "/v1/chat/completions";
        }

        LOG_DEBUG("UIBrowser", "Testing LLM at URL: " + api_url);

        // Use curl to test connection
        CURL* curl = curl_easy_init();
        if (curl) {
          std::string response_data;
          struct curl_slist* headers = nullptr;
          headers = curl_slist_append(headers, "Content-Type: application/json");

          if (!api_key.empty()) {
            std::string auth_header = "Authorization: Bearer " + api_key;
            headers = curl_slist_append(headers, auth_header.c_str());
          }

          // Simple test request
          std::string request_body = R"({
            "model": ")" + model + R"(",
            "messages": [{"role": "user", "content": "Say OK"}],
            "max_tokens": 10,
            "temperature": 0.1
          })";

          curl_easy_setopt(curl, CURLOPT_URL, api_url.c_str());
          curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
          curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
          curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);  // 10 second timeout
          curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](void* contents, size_t size, size_t nmemb, void* userp) -> size_t {
            ((std::string*)userp)->append((char*)contents, size * nmemb);
            return size * nmemb;
          });
          curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

          CURLcode res = curl_easy_perform(curl);

          if (res == CURLE_OK) {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

            if (http_code == 200) {
              // Check if response has expected structure
              if (response_data.find("\"choices\"") != std::string::npos) {
                result_status = "success";
                result_message = "Connection successful! Model responded.";
                LOG_DEBUG("UIBrowser", "LLM test successful: " + response_data.substr(0, 200));
              } else {
                result_status = "error";
                result_message = "Unexpected response format from API";
                LOG_WARN("UIBrowser", "LLM test unexpected response: " + response_data.substr(0, 200));
              }
            } else {
              result_status = "error";
              result_message = "HTTP " + std::to_string(http_code) + ": " + response_data.substr(0, 200);
              LOG_WARN("UIBrowser", "LLM test HTTP error: " + std::to_string(http_code));
            }
          } else {
            result_status = "error";
            result_message = "Connection failed: " + std::string(curl_easy_strerror(res));
            LOG_ERROR("UIBrowser", "LLM test curl error: " + std::string(curl_easy_strerror(res)));
          }

          curl_slist_free_all(headers);
          curl_easy_cleanup(curl);
        } else {
          result_status = "error";
          result_message = "Failed to initialize HTTP client";
        }

        // Send response back to UI on CEF thread
        CefPostTask(TID_UI, new LLMTestResultTask(response_frame, result_status, result_message));
      }).detach();

      return true;
    }
  }

  // Handle dom_elements_chunk message - chunked DOM data transfer
  if (message_name == "dom_elements_chunk" && source_process == PID_RENDERER) {
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    if (args && args->GetSize() >= 1) {
      std::string message_json = args->GetString(0).ToString();

      // Parse message JSON: { index: N, total: N, isLast: bool, chunkBase64: "..." }
      size_t total_pos = message_json.find("\"total\":");
      size_t is_last_pos = message_json.find("\"isLast\":");
      size_t chunk_pos = message_json.find("\"chunkBase64\":\"");

      if (total_pos != std::string::npos && is_last_pos != std::string::npos && chunk_pos != std::string::npos) {
        // Extract total
        size_t total_start = total_pos + 8;
        size_t total_end = message_json.find(",", total_start);
        if (total_end == std::string::npos) total_end = message_json.find("}", total_start);
        std::string total_str = message_json.substr(total_start, total_end - total_start);
        int total = 0;
        for (char c : total_str) {
          if (c >= '0' && c <= '9') {
            total = total * 10 + (c - '0');
          }
        }

        // Extract isLast
        size_t is_last_start = is_last_pos + 9;
        bool is_last = (message_json.substr(is_last_start, 4) == "true");

        // Extract base64 chunk
        size_t chunk_start = chunk_pos + 15; // After "chunkBase64":"
        size_t chunk_end = message_json.find("\"", chunk_start);
        std::string chunk_base64 = message_json.substr(chunk_start, chunk_end - chunk_start);

        // Decode base64
        CefRefPtr<CefBinaryValue> decoded = CefBase64Decode(chunk_base64);
        if (decoded) {
          size_t decoded_size = decoded->GetSize();
          std::vector<char> buffer(decoded_size + 1);
          decoded->GetData(buffer.data(), decoded_size, 0);
          buffer[decoded_size] = '\0';
          std::string chunk_json(buffer.data(), decoded_size);

          // Check if this is the first chunk
          if (accumulated_dom_elements_.empty()) {
            accumulated_dom_elements_ = "[";
            expected_dom_total_ = total;
            LOG_DEBUG("UIBrowser", "Starting DOM chunk accumulation, expecting " + std::to_string(total) + " elements");
          }

          // Strip array brackets from chunk JSON and append
          if (chunk_json.length() >= 2 && chunk_json[0] == '[' && chunk_json[chunk_json.length()-1] == ']') {
            std::string chunk_elements = chunk_json.substr(1, chunk_json.length() - 2);

            // Append chunk (add comma if not first chunk)
            if (accumulated_dom_elements_.length() > 1) {
              accumulated_dom_elements_ += ",";
            }
            accumulated_dom_elements_ += chunk_elements;

            if (is_last) {
              accumulated_dom_elements_ += "]";
              LOG_DEBUG("UIBrowser", "Received final DOM chunk, total size: " + std::to_string(accumulated_dom_elements_.length()) + " bytes");

              // Forward complete data to dev console
              OwlDevConsole* dev_console = OwlDevConsole::GetInstance();
              if (dev_console) {
                dev_console->UpdateElementsTree(accumulated_dom_elements_);
              }

              // Reset accumulator
              accumulated_dom_elements_ = "";
              expected_dom_total_ = 0;
            } else {
              LOG_DEBUG("UIBrowser", "Received DOM chunk, accumulated size: " + std::to_string(accumulated_dom_elements_.length()) + " bytes");
            }
          } else {
            LOG_ERROR("UIBrowser", "Invalid chunk JSON format after base64 decode");
            accumulated_dom_elements_ = "";
            expected_dom_total_ = 0;
          }
        } else {
          LOG_ERROR("UIBrowser", "Failed to decode base64 chunk");
          accumulated_dom_elements_ = "";
          expected_dom_total_ = 0;
        }
      } else {
        LOG_ERROR("UIBrowser", "Failed to parse DOM chunk message");
        accumulated_dom_elements_ = "";
        expected_dom_total_ = 0;
      }

      return true;
    }
  }

  // Handle dom_elements_error message
  if (message_name == "dom_elements_error" && source_process == PID_RENDERER) {
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    if (args && args->GetSize() > 0) {
      std::string error = args->GetString(0).ToString();
      LOG_ERROR("UIBrowser", "DOM extraction error: " + error);
      accumulated_dom_elements_ = "";
      expected_dom_total_ = 0;
      return true;
    }
  }

  // Let parent handle other messages
  return OwlClient::OnProcessMessageReceived(browser, frame, source_process, message);
}

void OwlUIBrowser::CreateBrowserWindow(const std::string& url) {
  CEF_REQUIRE_UI_THREAD();

  current_url_ = url;
  void* parent_handle = nullptr;

  if (is_playground_) {
    // Playground window: use standalone window helper (NOT the singleton delegate)
    parent_handle = OwlPlaygroundWindow::CreateWindow(this, 1200, 800);
    playground_window_handle_ = parent_handle;  // Store for focusing later
    LOG_DEBUG("UI", "Standalone playground window created");
  } else {
    // Main browser window: use singleton delegate with toolbar
    OwlUIDelegate* delegate = OwlUIDelegate::GetInstance();
    if (delegate) {
      parent_handle = delegate->CreateWindowWithToolbar(this, 1400, 900);
      LOG_DEBUG("UI", "Main browser window with toolbar created");
    } else {
      LOG_ERROR("UI", "Failed to get UI delegate instance");
      return;
    }
  }

  if (!parent_handle) {
    LOG_ERROR("UI", "Failed to create window");
    return;
  }

  // Create CEF browser as child of the content view
  CefWindowInfo window_info;

#if defined(OS_MACOS)
  if (is_playground_) {
    window_info.SetAsChild(parent_handle, CefRect(0, 0, 1200, 800));
  } else {
    window_info.SetAsChild(parent_handle, CefRect(0, 0, 1400, 844));  // 900 - 56 (toolbar height)
  }
#elif defined(OS_LINUX)
  // On Linux, CefWindowHandle is an unsigned long (X11 window ID)
  if (is_playground_) {
    window_info.SetAsChild(reinterpret_cast<cef_window_handle_t>(parent_handle), CefRect(0, 0, 1200, 800));
  } else {
    window_info.SetAsChild(reinterpret_cast<cef_window_handle_t>(parent_handle), CefRect(0, 0, 1400, 844));
  }
#elif defined(OS_WINDOWS)
  if (is_playground_) {
    window_info.SetAsChild((HWND)parent_handle, CefRect(0, 0, 1200, 800));
  } else {
    window_info.SetAsChild((HWND)parent_handle, CefRect(0, 0, 1400, 844));
  }
#endif

  // Browser settings
  CefBrowserSettings browser_settings;

  // Create a dedicated request context for UI browser
  // This isolates UI browser from global/system proxy settings
  CefRequestContextSettings context_settings;
  // Use non-empty cache path to avoid off-the-record mode (incognito detection)
  CefString(&context_settings.cache_path) = "/tmp/owl_browser_ui_cache";
  context_settings.persist_session_cookies = true;

  CefRefPtr<CefRequestContext> request_context =
    CefRequestContext::CreateContext(context_settings, nullptr);

  // Check if there's a proxy configured via OwlProxyManager
  // Only apply proxy if explicitly configured, otherwise use direct connection
  if (request_context) {
    OwlProxyManager* proxy_manager = OwlProxyManager::GetInstance();
    ProxyConfig proxy_config = proxy_manager->GetProxyConfig();

    CefRefPtr<CefValue> proxy_value = CefValue::Create();
    CefRefPtr<CefDictionaryValue> proxy_dict = CefDictionaryValue::Create();

    if (proxy_config.IsValid() && proxy_config.enabled) {
      // Apply the configured proxy
      std::string proxy_server = proxy_config.GetCEFProxyString();
      proxy_dict->SetString("mode", "fixed_servers");
      proxy_dict->SetString("server", proxy_server);
      proxy_value->SetDictionary(proxy_dict);

      CefString error;
      if (!request_context->SetPreference("proxy", proxy_value, error)) {
        LOG_ERROR("UIBrowser", "Failed to set proxy: " + error.ToString());
      } else {
        LOG_DEBUG("UIBrowser", "UI browser using proxy: " + proxy_server);
      }
    } else {
      // No proxy configured - use direct connection
      proxy_dict->SetString("mode", "direct");
      proxy_value->SetDictionary(proxy_dict);

      CefString error;
      if (!request_context->SetPreference("proxy", proxy_value, error)) {
        LOG_ERROR("UIBrowser", "Failed to set direct connection: " + error.ToString());
      } else {
        LOG_DEBUG("UIBrowser", "UI browser using direct connection (no proxy)");
      }
    }
  }

  // Create browser asynchronously with dedicated request context
  CefBrowserHost::CreateBrowser(window_info, this, url, browser_settings, nullptr, request_context);
}

void OwlUIBrowser::FocusWindow() {
  if (is_playground_) {
    if (playground_window_handle_) {
      // Use playground window helper to focus
      OwlPlaygroundWindow::FocusWindow(playground_window_handle_);
    } else {
      LOG_WARN("UIBrowser", "Cannot focus playground: window handle is null");
    }
  } else {
    // Use the singleton delegate to focus main window
    OwlUIDelegate* delegate = OwlUIDelegate::GetInstance();
    if (delegate) {
      delegate->FocusWindow();
    }
  }
}

void OwlUIBrowser::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();

  browser_ = browser;
  LOG_DEBUG("UI", "UI Browser created successfully");

  // Track main browser instances (not playground)
  if (!is_playground_) {
    main_browser_count_++;
    LOG_DEBUG("UIBrowser", "Main browser created. Total count: " + std::to_string(main_browser_count_));
  }

  // Register UI browser with BrowserManager so Click/Type methods work
  std::ostringstream ctx_stream;
  ctx_stream << "ctx_" << std::setfill('0') << std::setw(6) << browser->GetIdentifier();
  std::string context_id = ctx_stream.str();

  // Load LLM config from file for UI browser
  LLMConfig llm_config = OwlBrowserManager::LoadLLMConfigFromFile();

  OwlBrowserManager* manager = OwlBrowserManager::GetInstance();
  manager->RegisterUIBrowser(context_id, browser, &llm_config);
  LOG_DEBUG("UI", "Registered UI browser as context: " + context_id);

  // Set browser reference in window delegate and show window
  if (is_playground_ && playground_window_handle_) {
    // Playground window - set browser reference in its delegate
    OwlPlaygroundWindow::SetBrowser(playground_window_handle_, browser);
  } else {
    // Main window - use singleton delegate
    OwlUIDelegate* delegate = OwlUIDelegate::GetInstance();
    if (delegate) {
      delegate->SetBrowser(browser);
      delegate->ShowWindow();

      // Update toolbar with initial state
      if (delegate->GetToolbar()) {
        delegate->GetToolbar()->UpdateNavigationButtons(false, false);
        delegate->GetToolbar()->UpdateAddressBar(current_url_);
      }
    } else {
      LOG_ERROR("UI", "Failed to get UI delegate instance");
    }
  }
}

bool OwlUIBrowser::DoClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();

  LOG_DEBUG("UIBrowser", "========== DoClose START ==========");
  LOG_DEBUG("UIBrowser", "DoClose called for browser ID " + std::to_string(browser->GetIdentifier()) + " - is_playground=" + std::to_string(is_playground_));

  // For playground windows, signal that CEF is ready to close
  if (is_playground_ && playground_window_handle_) {
    LOG_DEBUG("UIBrowser", "DoClose: Signaling CEF is ready, triggering window close");
    OwlPlaygroundWindow::CloseWindow(playground_window_handle_);
    LOG_DEBUG("UIBrowser", "DoClose: Signal sent, window will close and OnBeforeClose will be called");
  } else if (is_playground_) {
    LOG_ERROR("UIBrowser", "DoClose: playground_window_handle_ is NULL!");
  }

  LOG_DEBUG("UIBrowser", "DoClose: Returning false to allow window to close");
  LOG_DEBUG("UIBrowser", "========== DoClose END ==========");
  return false;
}

void OwlUIBrowser::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();

  LOG_DEBUG("UIBrowser", "========== OnBeforeClose START ==========");
  LOG_DEBUG("UIBrowser", "OnBeforeClose called for browser ID " + std::to_string(browser->GetIdentifier()) + " - is_playground=" + std::to_string(is_playground_));

  // Clear playground instance if this is the playground window
  if (is_playground_) {
    // Always clear the static instance if this is a playground
    CefRefPtr<OwlUIBrowser> current_playground = GetPlaygroundInstance();
    if (current_playground == this || current_playground.get() == this) {
      SetPlaygroundInstance(nullptr);
      LOG_DEBUG("UIBrowser", "Playground instance cleared");
    }

    // Clear the window handle (window is already closing, don't need to close it again)
    playground_window_handle_ = nullptr;
    LOG_DEBUG("UIBrowser", "Playground window handle cleared");
  } else {
    // Decrement main browser count
    main_browser_count_--;
    LOG_DEBUG("UIBrowser", "Main browser closed. Remaining count: " + std::to_string(main_browser_count_));
  }

  browser_ = nullptr;
  LOG_DEBUG("UIBrowser", "Browser reference cleared");
  LOG_DEBUG("UIBrowser", "========== OnBeforeClose END ==========");
}

bool OwlUIBrowser::OnBeforePopup(CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<CefFrame> frame,
                                    int popup_id,
                                    const CefString& target_url,
                                    const CefString& target_frame_name,
                                    CefLifeSpanHandler::WindowOpenDisposition target_disposition,
                                    bool user_gesture,
                                    const CefPopupFeatures& popupFeatures,
                                    CefWindowInfo& windowInfo,
                                    CefRefPtr<CefClient>& client,
                                    CefBrowserSettings& settings,
                                    CefRefPtr<CefDictionaryValue>& extra_info,
                                    bool* no_javascript_access) {
  CEF_REQUIRE_UI_THREAD();

  std::string url = target_url.ToString();
  LOG_DEBUG("UIBrowser", "OnBeforePopup called for URL: " + url);

  // Don't handle popups for playground window
  if (is_playground_) {
    LOG_DEBUG("UIBrowser", "Popup blocked - playground window");
    return true;  // Block popup
  }

  // Create new tab for popup instead of new window
  OwlUIDelegate* delegate = OwlUIDelegate::GetInstance();
  if (delegate) {
    delegate->NewTab(url);
    LOG_DEBUG("UIBrowser", "Popup opened in new tab: " + url);
  }

  return true;  // Block the default popup behavior
}

void OwlUIBrowser::OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, TransitionType transition_type) {
  CEF_REQUIRE_UI_THREAD();

  if (frame->IsMain()) {
    std::string url = frame->GetURL().ToString();
    current_url_ = url;

    // Only update address bar and loading state for main browser, not playground
    if (!is_playground_) {
      UpdateAddressBar(url);

      // Update loading indicator to show spinner
      OwlUIDelegate* delegate = OwlUIDelegate::GetInstance();
      if (delegate && delegate->GetToolbar()) {
        delegate->GetToolbar()->SetLoadingState(true);
      }
    }
  }
}

void OwlUIBrowser::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) {
  CEF_REQUIRE_UI_THREAD();

  // IMPORTANT: Call parent to update navigation state
  OwlClient::OnLoadEnd(browser, frame, httpStatusCode);

  if (frame->IsMain()) {
    // Only update toolbar state for main browser, not playground
    if (!is_playground_) {
      // Update navigation buttons after page load
      UpdateNavigationButtons();

      // Hide loading indicator and check if we should reset task executing state after page load
      OwlUIDelegate* delegate = OwlUIDelegate::GetInstance();
      if (delegate) {
        if (delegate->GetToolbar()) {
          delegate->GetToolbar()->SetLoadingState(false);
        }

        auto controller_status = OwlAgentController::GetInstance()->GetStatus();

        // If agent is done (IDLE/COMPLETED/ERROR) and all tasks are complete, reset UI
        if (controller_status.state == OwlAgentController::AgentState::COMPLETED ||
            controller_status.state == OwlAgentController::AgentState::ERROR ||
            controller_status.state == OwlAgentController::AgentState::IDLE) {

          auto tasks = OwlTaskState::GetInstance()->GetTasks();
          bool has_active_tasks = false;
          for (const auto& task : tasks) {
            if (task.status == TaskStatus::ACTIVE || task.status == TaskStatus::PENDING) {
              has_active_tasks = true;
              break;
            }
          }

          // Now that page is loaded, if no active tasks, reset UI
          if (!has_active_tasks) {
            delegate->SetTaskExecuting(false);
          }
        }
      }
    }
  }
}

void OwlUIBrowser::OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode, const CefString& errorText, const CefString& failedUrl) {
  CEF_REQUIRE_UI_THREAD();

  if (frame->IsMain()) {
    // Only update toolbar state for main browser, not playground
    if (!is_playground_) {
      // Hide loading indicator on error
      OwlUIDelegate* delegate = OwlUIDelegate::GetInstance();
      if (delegate && delegate->GetToolbar()) {
        delegate->GetToolbar()->SetLoadingState(false);
      }
    }

    if (errorCode != ERR_ABORTED) {
      LOG_ERROR("UI", "Navigation error: " + errorText.ToString());
    }
  }
}

void OwlUIBrowser::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) {
  CEF_REQUIRE_UI_THREAD();

  current_title_ = title.ToString();

  if (is_playground_) {
    // Playground window - update the playground window's title directly
    if (playground_window_handle_) {
      #if defined(OS_MACOS)
      extern void SetPlaygroundWindowTitle(void* window_handle, const std::string& title);
      SetPlaygroundWindowTitle(playground_window_handle_, current_title_);
      #endif
    }
  } else {
    // Main browser window/tab - set window title via helper function
    #if defined(OS_MACOS)
    SetBrowserWindowTitle(browser, current_title_);
    #endif
  }
}

void OwlUIBrowser::OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& url) {
  CEF_REQUIRE_UI_THREAD();

  if (frame->IsMain()) {
    current_url_ = url.ToString();
    // Only update address bar for main browser, not playground
    if (!is_playground_) {
      UpdateAddressBar(current_url_);
    }
  }
}

bool OwlUIBrowser::OnPreKeyEvent(CefRefPtr<CefBrowser> browser, const CefKeyEvent& event, CefEventHandle os_event, bool* is_keyboard_shortcut) {
  CEF_REQUIRE_UI_THREAD();

  // Handle keyboard shortcuts
  // NOTE: OnPreKeyEvent is only called when the browser content has focus, not native controls.
  // So these shortcuts work in the browser but won't interfere with the address bar.
  if (event.type == KEYEVENT_RAWKEYDOWN) {
    // Cmd+C or Ctrl+C: Copy
    if ((event.modifiers & EVENTFLAG_COMMAND_DOWN || event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
        event.windows_key_code == 'C') {
      browser->GetFocusedFrame()->Copy();
      return true;
    }

    // Cmd+V or Ctrl+V: Paste
    if ((event.modifiers & EVENTFLAG_COMMAND_DOWN || event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
        event.windows_key_code == 'V') {
      browser->GetFocusedFrame()->Paste();
      return true;
    }

    // Cmd+X or Ctrl+X: Cut
    if ((event.modifiers & EVENTFLAG_COMMAND_DOWN || event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
        event.windows_key_code == 'X') {
      browser->GetFocusedFrame()->Cut();
      return true;
    }

    // Cmd+A or Ctrl+A: Select All
    if ((event.modifiers & EVENTFLAG_COMMAND_DOWN || event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
        event.windows_key_code == 'A') {
      browser->GetFocusedFrame()->SelectAll();
      return true;
    }

    // Cmd+Z or Ctrl+Z: Undo
    if ((event.modifiers & EVENTFLAG_COMMAND_DOWN || event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
        !(event.modifiers & EVENTFLAG_SHIFT_DOWN) && event.windows_key_code == 'Z') {
      browser->GetFocusedFrame()->Undo();
      return true;
    }

    // Cmd+Shift+Z or Ctrl+Shift+Z or Cmd+Y or Ctrl+Y: Redo
    if (((event.modifiers & EVENTFLAG_COMMAND_DOWN || event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
         (event.modifiers & EVENTFLAG_SHIFT_DOWN) && event.windows_key_code == 'Z') ||
        ((event.modifiers & EVENTFLAG_COMMAND_DOWN || event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
         event.windows_key_code == 'Y')) {
      browser->GetFocusedFrame()->Redo();
      return true;
    }

    // Cmd+W or Ctrl+W: Close window
    if ((event.modifiers & EVENTFLAG_COMMAND_DOWN || event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
        event.windows_key_code == 'W') {
      browser->GetHost()->CloseBrowser(false);
      return true;
    }

    // Cmd+L or Ctrl+L: Focus address bar (only for non-playground windows)
    if (!is_playground_ && (event.modifiers & EVENTFLAG_COMMAND_DOWN || event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
        event.windows_key_code == 'L') {
      // Focus address bar via JS
      browser->GetMainFrame()->ExecuteJavaScript(
        "document.getElementById('owl-address-bar')?.focus();",
        browser->GetMainFrame()->GetURL(), 0);
      return true;
    }

    // Cmd+T or Ctrl+T: New tab (navigate to home) - only for non-playground windows
    if (!is_playground_ && (event.modifiers & EVENTFLAG_COMMAND_DOWN || event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
        event.windows_key_code == 'T') {
      Navigate("owl://homepage.html");
      return true;
    }

    // Cmd+R or Ctrl+R: Reload
    if ((event.modifiers & EVENTFLAG_COMMAND_DOWN || event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
         event.windows_key_code == 'R') {
      Reload();
      return true;
    }

    // Cmd+[ : Back (only for non-playground windows)
    if (!is_playground_ && event.modifiers & EVENTFLAG_COMMAND_DOWN && event.windows_key_code == 219) {
      GoBack();
      return true;
    }

    // Cmd+] : Forward (only for non-playground windows)
    if (!is_playground_ && event.modifiers & EVENTFLAG_COMMAND_DOWN && event.windows_key_code == 221) {
      GoForward();
      return true;
    }

    // Cmd+Shift+A or Ctrl+Shift+A: Toggle agent mode (only for non-playground windows)
    if (!is_playground_ && (event.modifiers & EVENTFLAG_COMMAND_DOWN || event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
        (event.modifiers & EVENTFLAG_SHIFT_DOWN) && event.windows_key_code == 'A') {
      ToggleAgentMode();
      return true;
    }

    // Cmd+Shift+S or Ctrl+Shift+S: Toggle sidebar (only for non-playground windows)
    if (!is_playground_ && (event.modifiers & EVENTFLAG_COMMAND_DOWN || event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
        (event.modifiers & EVENTFLAG_SHIFT_DOWN) && event.windows_key_code == 'S') {
      ToggleSidebar();
      return true;
    }
  }

  return false;
}

void OwlUIBrowser::OnBeforeContextMenu(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefContextMenuParams> params, CefRefPtr<CefMenuModel> model) {
  CEF_REQUIRE_UI_THREAD();

  LOG_DEBUG("UI", "OnBeforeContextMenu called - is_playground=" + std::to_string(is_playground_));
  LOG_DEBUG("UI", "TypeFlags: " + std::to_string(params->GetTypeFlags()));

  // Clear default menu
  model->Clear();

  // Add custom context menu items using CEF standard menu IDs
  if (params->GetTypeFlags() & CM_TYPEFLAG_EDITABLE) {
    LOG_DEBUG("UI", "Adding editable context menu items");
    // Context menu for editable content (input fields, textareas)
    model->AddItem(MENU_ID_UNDO, "Undo");
    model->AddItem(MENU_ID_REDO, "Redo");
    model->AddSeparator();
    model->AddItem(MENU_ID_CUT, "Cut");
    model->AddItem(MENU_ID_COPY, "Copy");
    model->AddItem(MENU_ID_PASTE, "Paste");
    model->AddSeparator();
    model->AddItem(MENU_ID_SELECT_ALL, "Select All");
  } else if (params->GetTypeFlags() & CM_TYPEFLAG_SELECTION) {
    LOG_DEBUG("UI", "Adding selection context menu items");
    // Context menu for text selection
    model->AddItem(MENU_ID_COPY, "Copy");
  } else {
    LOG_DEBUG("UI", "Adding general page context menu items");
    if (!is_playground_) {
      // Main browser window - add navigation items
      model->AddItem(MENU_ID_BACK, "Back");
      model->AddItem(MENU_ID_FORWARD, "Forward");
      model->AddItem(MENU_ID_RELOAD, "Reload");
      model->AddSeparator();
      model->AddItem(MENU_ID_USER_FIRST, "Developer Playground");
      model->AddItem(MENU_ID_USER_FIRST + 1, "Inspect");
    } else {
      // Playground window - only show Inspect
      model->AddItem(MENU_ID_USER_FIRST, "Inspect");
    }
  }

  LOG_DEBUG("UI", "Context menu prepared with " + std::to_string(model->GetCount()) + " items");
}

bool OwlUIBrowser::OnContextMenuCommand(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefContextMenuParams> params, int command_id, EventFlags event_flags) {
  CEF_REQUIRE_UI_THREAD();

  LOG_DEBUG("UI", "OnContextMenuCommand called with command_id: " + std::to_string(command_id));

  switch (command_id) {
    case MENU_ID_BACK:
      GoBack();
      return true;
    case MENU_ID_FORWARD:
      GoForward();
      return true;
    case MENU_ID_RELOAD:
      Reload();
      return true;
    case MENU_ID_UNDO:
      frame->Undo();
      return true;
    case MENU_ID_REDO:
      frame->Redo();
      return true;
    case MENU_ID_CUT:
      frame->Cut();
      return true;
    case MENU_ID_COPY:
      frame->Copy();
      return true;
    case MENU_ID_PASTE:
      frame->Paste();
      return true;
    case MENU_ID_SELECT_ALL:
      frame->SelectAll();
      return true;
    case MENU_ID_USER_FIRST:  // Developer Playground (main browser) or Inspect (playground)
      if (!is_playground_) {
        // Main browser - open Developer Playground
        LOG_DEBUG("UIBrowser", "Opening Developer Playground from context menu");

        // Check if playground already exists and is still valid
        CefRefPtr<OwlUIBrowser> existing_playground = GetPlaygroundInstance();
        if (existing_playground && existing_playground->GetBrowser()) {
          // Focus existing playground window
          LOG_DEBUG("UIBrowser", "Playground already open, focusing window");
          existing_playground->FocusWindow();
        } else {
          // Clear stale instance if browser is invalid
          if (existing_playground && !existing_playground->GetBrowser()) {
            LOG_DEBUG("UIBrowser", "Clearing stale playground instance from context menu");
            SetPlaygroundInstance(nullptr);
          }

          // Create a new browser window for the playground
          CefRefPtr<OwlUIBrowser> playground_browser(new OwlUIBrowser);
          playground_browser->SetAsPlayground();
          playground_browser->SetMainBrowser(browser_);
          SetPlaygroundInstance(playground_browser);
          playground_browser->CreateBrowserWindow("owl://playground.html");
          LOG_DEBUG("UIBrowser", "New playground window created");
        }
        return true;
      } else {
        // Playground window - show Inspect
        OwlDevConsole::GetInstance()->SetMainBrowser(browser_);
        OwlDevConsole::GetInstance()->Show();
        LOG_DEBUG("UI", "Developer console opened from context menu (Playground)");
        return true;
      }
    case MENU_ID_USER_FIRST + 1:  // Inspect (main browser only)
      OwlDevConsole::GetInstance()->SetMainBrowser(browser_);
      OwlDevConsole::GetInstance()->Show();
      LOG_DEBUG("UI", "Developer console opened from context menu (Main Browser)");
      return true;
    default:
      return false;
  }
}

void OwlUIBrowser::Navigate(const std::string& url) {
  if (!browser_) return;

  std::string final_url = url;

  // Add https:// if no protocol specified
  if (final_url.find("://") == std::string::npos) {
    // Check if it looks like a domain (has dot and no spaces)
    if (final_url.find('.') != std::string::npos && final_url.find(' ') == std::string::npos) {
      final_url = "https://" + final_url;
    } else {
      // Search query - use Google
      final_url = "https://www.google.com/search?q=" + final_url;
    }
  }

  browser_->GetMainFrame()->LoadURL(final_url);
}

void OwlUIBrowser::GoBack() {
  LOG_DEBUG("UI", "GoBack() called");
  if (browser_) {
    bool can_go_back = browser_->CanGoBack();
    LOG_DEBUG("UI", "Can go back: " + std::string(can_go_back ? "yes" : "no"));
    if (can_go_back) {
      browser_->GoBack();
      LOG_DEBUG("UI", "Navigation back executed");
    }
  } else {
    LOG_ERROR("UI", "Browser is null in GoBack()");
  }
}

void OwlUIBrowser::GoForward() {
  LOG_DEBUG("UI", "GoForward() called");
  if (browser_) {
    bool can_go_forward = browser_->CanGoForward();
    LOG_DEBUG("UI", "Can go forward: " + std::string(can_go_forward ? "yes" : "no"));
    if (can_go_forward) {
      browser_->GoForward();
      LOG_DEBUG("UI", "Navigation forward executed");
    }
  } else {
    LOG_ERROR("UI", "Browser is null in GoForward()");
  }
}

void OwlUIBrowser::Reload() {
  LOG_DEBUG("UI", "Reload() called");
  if (browser_) {
    browser_->Reload();
    LOG_DEBUG("UI", "Page reload executed");
  } else {
    LOG_ERROR("UI", "Browser is null in Reload()");
  }
}

void OwlUIBrowser::StopLoading() {
  LOG_DEBUG("UI", "StopLoading() called");
  if (browser_) {
    browser_->StopLoad();
    LOG_DEBUG("UI", "Page loading stopped");
  } else {
    LOG_ERROR("UI", "Browser is null in StopLoading()");
  }
}

void OwlUIBrowser::ToggleAgentMode() {
  agent_mode_ = !agent_mode_;

  LOG_DEBUG("UI", agent_mode_ ? "Agent mode enabled - toggling button" : "Agent mode disabled - toggling button");

  // Update native toolbar button state
  OwlUIDelegate* delegate = OwlUIDelegate::GetInstance();
  if (delegate && delegate->GetToolbar()) {
    LOG_DEBUG("UI", "Calling SetAgentModeActive");
    delegate->GetToolbar()->SetAgentModeActive(agent_mode_);
    LOG_DEBUG("UI", "SetAgentModeActive completed");
  }

  // Show/hide native agent prompt overlay
  if (delegate) {
    if (agent_mode_) {
      LOG_DEBUG("UI", "Showing agent prompt overlay");
      delegate->ShowAgentPrompt();
    } else {
      LOG_DEBUG("UI", "Hiding agent prompt overlay");
      delegate->HideAgentPrompt();
    }
  }

  LOG_DEBUG("UI", "ToggleAgentMode completed");
}

void OwlUIBrowser::ExecuteAgentPrompt(const std::string& prompt) {
  LOG_DEBUG("UI", "OwlUIBrowser::ExecuteAgentPrompt called");

  if (!browser_) {
    LOG_ERROR("UI", "browser_ is null in ExecuteAgentPrompt!");
    return;
  }

  if (prompt.empty()) {
    LOG_ERROR("UI", "prompt is empty in ExecuteAgentPrompt!");
    return;
  }

  LOG_DEBUG("UI", "Executing agent prompt: " + prompt);

  // Execute via agent controller with status updates
  LOG_DEBUG("UI", "Calling OwlAgentController::GetInstance()->ExecuteCommand...");
  OwlAgentController::GetInstance()->ExecuteCommand(
    browser_,
    prompt,
    [this](const OwlAgentController::AgentStatus& status) {
      if (!browser_) return;

      // Update UI with status
      std::string js = "window.owlUI?.updateAgentStatus(" +
        std::to_string(static_cast<int>(status.state)) + "," +
        "'" + status.message + "'," +
        std::to_string(status.progress) + ");";

      browser_->GetMainFrame()->ExecuteJavaScript(js, browser_->GetMainFrame()->GetURL(), 0);

      // Update UI delegate based on agent state
      OwlUIDelegate* delegate = OwlUIDelegate::GetInstance();
      if (delegate) {
        // Update status dot color
        delegate->UpdateTaskStatusDot();

        // Update task list with current status from OwlTaskState
        delegate->UpdateTasksList();

        // Only reset button when agent is done AND there are no more active/pending tasks AND browser is not loading
        if (status.state == OwlAgentController::AgentState::COMPLETED ||
            status.state == OwlAgentController::AgentState::ERROR ||
            status.state == OwlAgentController::AgentState::IDLE) {

          // Check if there are any active or pending tasks
          auto tasks = OwlTaskState::GetInstance()->GetTasks();
          bool has_active_tasks = false;
          for (const auto& task : tasks) {
            if (task.status == TaskStatus::ACTIVE || task.status == TaskStatus::PENDING) {
              has_active_tasks = true;
              break;
            }
          }

          // Only change UI state if there are NO active/pending tasks AND browser is not loading
          if (!has_active_tasks && !browser_->IsLoading()) {
            delegate->SetTaskExecuting(false);
          }
        }
      }
    }
  );
  LOG_DEBUG("UI", "OwlAgentController::ExecuteCommand returned");
}

void OwlUIBrowser::ToggleSidebar() {
  sidebar_visible_ = !sidebar_visible_;

  OwlUIDelegate* delegate = OwlUIDelegate::GetInstance();
  if (delegate) {
    if (sidebar_visible_) {
      delegate->ShowSidebar();
    } else {
      delegate->HideSidebar();
    }
  }

  // Update UI via JavaScript
  if (browser_) {
    std::string js = sidebar_visible_
      ? "window.owlUI?.showSidebar()"
      : "window.owlUI?.hideSidebar()";

    browser_->GetMainFrame()->ExecuteJavaScript(js, browser_->GetMainFrame()->GetURL(), 0);
  }
}

void OwlUIBrowser::UpdateAddressBar(const std::string& url) {
  // Update native toolbar address bar
  OwlUIDelegate* delegate = OwlUIDelegate::GetInstance();
  if (delegate && delegate->GetToolbar()) {
    delegate->GetToolbar()->UpdateAddressBar(url);
  }
}

void OwlUIBrowser::UpdateNavigationButtons() {
  if (!browser_) return;

  bool can_go_back = browser_->CanGoBack();
  bool can_go_forward = browser_->CanGoForward();

  // Update native toolbar buttons
  OwlUIDelegate* delegate = OwlUIDelegate::GetInstance();
  if (delegate && delegate->GetToolbar()) {
    delegate->GetToolbar()->UpdateNavigationButtons(can_go_back, can_go_forward);
  }
}

bool OwlUIBrowser::OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level, const CefString& message, const CefString& source, int line) {
  std::string msg = message.ToString();

  // Check for position picker result marker
  if (msg.find("[OLIB_POSITION_PICKER_RESULT]") != std::string::npos) {
    // Extract the JSON result after the marker
    size_t json_start = msg.find("{");
    if (json_start != std::string::npos) {
      std::string result_json = msg.substr(json_start);

      // Get the pending picker info from the global map
      extern std::map<int, std::pair<CefRefPtr<CefBrowser>, std::string>> g_pending_pickers;

      int browser_id = browser->GetIdentifier();
      auto it = g_pending_pickers.find(browser_id);
      if (it != g_pending_pickers.end()) {
        CefRefPtr<CefBrowser> playground_browser = it->second.first;
        std::string input_id = it->second.second;

        // Parse result JSON for position picker
        // Looking for: {"action":"confirmed","position":"100x200"}
        // or: {"action":"canceled"}

        bool is_confirmed = result_json.find("\"action\":\"confirmed\"") != std::string::npos;
        bool is_canceled = result_json.find("\"action\":\"canceled\"") != std::string::npos;

        if (is_confirmed) {
          // Extract position from JSON
          size_t position_start = result_json.find("\"position\":\"");
          if (position_start != std::string::npos) {
            position_start += 12;  // Length of "position":""
            size_t position_end = result_json.find("\"", position_start);

            if (position_end != std::string::npos) {
              std::string position = result_json.substr(position_start, position_end - position_start);

              // Forward to playground
              if (playground_browser && playground_browser->GetMainFrame()) {
                std::ostringstream forward_js;
                forward_js << "if (typeof onPositionPicked === 'function') { onPositionPicked('"
                           << position << "'); }";
                playground_browser->GetMainFrame()->ExecuteJavaScript(forward_js.str(),
                                                                       playground_browser->GetMainFrame()->GetURL(), 0);

                LOG_DEBUG("UIBrowser", "Forwarded position picker result to playground: " + position);

                // Bring playground to front
                BringBrowserToFront(playground_browser);
              }
            }
          }
        } else if (is_canceled) {
          // Forward cancel to playground
          if (playground_browser && playground_browser->GetMainFrame()) {
            std::string cancel_js = "if (typeof onPositionPickerCanceled === 'function') { onPositionPickerCanceled(); }";
            playground_browser->GetMainFrame()->ExecuteJavaScript(cancel_js,
                                                                   playground_browser->GetMainFrame()->GetURL(), 0);

            LOG_DEBUG("UIBrowser", "Forwarded position picker cancel to playground");

            // Bring playground to front
            BringBrowserToFront(playground_browser);
          }
        }

        // Remove from pending map
        g_pending_pickers.erase(it);
      }
    }

    // Don't forward picker result messages to dev console
    return true;
  }

  // Check for element picker result marker
  if (msg.find("__OLIB_PICKER_RESULT__") != std::string::npos) {
    // Extract the JSON result after the marker
    size_t json_start = msg.find("{");
    if (json_start != std::string::npos) {
      std::string result_json = msg.substr(json_start);

      // Get the pending picker info from the global map
      extern std::map<int, std::pair<CefRefPtr<CefBrowser>, std::string>> g_pending_pickers;

      int browser_id = browser->GetIdentifier();
      auto it = g_pending_pickers.find(browser_id);
      if (it != g_pending_pickers.end()) {
        CefRefPtr<CefBrowser> playground_browser = it->second.first;
        std::string input_id = it->second.second;

        // Parse result JSON to determine action
        // Looking for: {"action":"confirmed","selector":"...","inputId":"..."}
        // or: {"action":"canceled","inputId":"..."}

        bool is_confirmed = result_json.find("\"action\":\"confirmed\"") != std::string::npos;
        bool is_canceled = result_json.find("\"action\":\"canceled\"") != std::string::npos;

        if (is_confirmed) {
          // Extract selector from JSON - properly handle escaped quotes
          size_t selector_start = result_json.find("\"selector\":\"");
          if (selector_start != std::string::npos) {
            selector_start += 12;  // Length of "selector":""

            // Find the closing quote, skipping escaped quotes
            size_t selector_end = selector_start;
            bool in_escape = false;
            while (selector_end < result_json.length()) {
              if (in_escape) {
                in_escape = false;
              } else if (result_json[selector_end] == '\\') {
                in_escape = true;
              } else if (result_json[selector_end] == '"') {
                break;  // Found unescaped closing quote
              }
              selector_end++;
            }

            if (selector_end < result_json.length()) {
              std::string selector = result_json.substr(selector_start, selector_end - selector_start);

              // Unescape JSON string (convert \" to ", etc.)
              std::string unescaped_selector;
              for (size_t i = 0; i < selector.length(); i++) {
                if (selector[i] == '\\' && i + 1 < selector.length()) {
                  char next = selector[i + 1];
                  if (next == '"' || next == '\\' || next == '/') {
                    unescaped_selector += next;
                    i++;  // Skip the backslash
                    continue;
                  } else if (next == 'n') {
                    unescaped_selector += '\n';
                    i++;
                    continue;
                  } else if (next == 't') {
                    unescaped_selector += '\t';
                    i++;
                    continue;
                  }
                }
                unescaped_selector += selector[i];
              }

              // Escape the selector for JavaScript (use single quotes in JS string)
              std::string escaped_selector;
              for (char c : unescaped_selector) {
                if (c == '\\' || c == '\'') {
                  escaped_selector += '\\';
                }
                escaped_selector += c;
              }

              // Forward to playground
              if (playground_browser && playground_browser->GetMainFrame()) {
                std::ostringstream forward_js;
                forward_js << "if (typeof onElementPicked === 'function') { onElementPicked('"
                           << escaped_selector << "'); }";
                playground_browser->GetMainFrame()->ExecuteJavaScript(forward_js.str(),
                                                                       playground_browser->GetMainFrame()->GetURL(), 0);

                LOG_DEBUG("UIBrowser", "Forwarded picker result to playground: " + unescaped_selector);

                // Bring playground to front
                BringBrowserToFront(playground_browser);
              }
            }
          }
        } else if (is_canceled) {
          // Forward cancel to playground
          if (playground_browser && playground_browser->GetMainFrame()) {
            std::string cancel_js = "if (typeof onElementPickerCanceled === 'function') { onElementPickerCanceled(); }";
            playground_browser->GetMainFrame()->ExecuteJavaScript(cancel_js,
                                                                   playground_browser->GetMainFrame()->GetURL(), 0);

            LOG_DEBUG("UIBrowser", "Forwarded picker cancel to playground");

            // Bring playground to front
            BringBrowserToFront(playground_browser);
          }
        }

        // Remove from pending map
        g_pending_pickers.erase(it);
      }
    }

    // Don't forward picker result messages to dev console
    return true;
  }

  // Forward to parent (OwlClient) to capture console messages in dev console
  return OwlClient::OnConsoleMessage(browser, level, message, source, line);
}

void OwlUIBrowser::SendProgressUpdate(CefRefPtr<CefBrowser> playground_browser,
                                        const std::string& status,
                                        const std::string& message,
                                        int current_step,
                                        int total_steps) {
  CEF_REQUIRE_UI_THREAD();

  if (!playground_browser) return;

  // Build JavaScript call to update status in playground window
  std::ostringstream js;
  js << "if (typeof updateExecutionStatus === 'function') { updateExecutionStatus('"
     << status << "', '" << message << "'";

  if (current_step >= 0 && total_steps > 0) {
    js << ", " << current_step << ", " << total_steps;
  }

  js << "); }";

  // Execute in playground window
  CefRefPtr<CefFrame> frame = playground_browser->GetMainFrame();
  if (frame) {
    frame->ExecuteJavaScript(js.str(), frame->GetURL(), 0);
  }
}

// Custom task for progress updates
class ProgressUpdateTask : public CefTask {
 public:
  ProgressUpdateTask(CefRefPtr<CefBrowser> browser,
                     const std::string& status,
                     const std::string& message,
                     int current_step,
                     int total_steps)
      : browser_(browser),
        status_(status),
        message_(message),
        current_step_(current_step),
        total_steps_(total_steps) {}

  void Execute() override {
    if (!browser_) return;

    std::ostringstream js;
    js << "if (typeof updateExecutionStatus === 'function') { updateExecutionStatus('"
       << status_ << "', '" << message_ << "'";

    if (current_step_ >= 0 && total_steps_ > 0) {
      js << ", " << current_step_ << ", " << total_steps_;
    }

    js << "); }";

    CefRefPtr<CefFrame> frame = browser_->GetMainFrame();
    if (frame) {
      frame->ExecuteJavaScript(js.str(), frame->GetURL(), 0);
    }
  }

 private:
  CefRefPtr<CefBrowser> browser_;
  std::string status_;
  std::string message_;
  int current_step_;
  int total_steps_;

  IMPLEMENT_REFCOUNTING(ProgressUpdateTask);
};

// Custom task for sending output to playground (must run on UI thread)
class OutputTask : public CefTask {
 public:
  OutputTask(CefRefPtr<CefBrowser> browser,
             int step_index,
             const std::string& output_type,
             const std::string& encoded_data)
      : browser_(browser),
        step_index_(step_index),
        output_type_(output_type),
        encoded_data_(encoded_data) {}

  void Execute() override {
    if (!browser_) return;

    CefRefPtr<CefFrame> frame = browser_->GetMainFrame();
    if (frame) {
      std::string js;
      // For screenshots, keep the base64 data as-is (don't decode with atob)
      // The playground will use it directly in img src="data:image/png;base64,..."
      if (output_type_ == "screenshot") {
        js = "if(window.receiveOutput) window.receiveOutput(" +
             std::to_string(step_index_) + ", '" + output_type_ +
             "', '" + encoded_data_ + "');";
      } else {
        // For text-based outputs, decode from base64
        js = "if(window.receiveOutput) window.receiveOutput(" +
             std::to_string(step_index_) + ", '" + output_type_ +
             "', atob('" + encoded_data_ + "'));";
      }
      frame->ExecuteJavaScript(js, frame->GetURL(), 0);
    }
  }

 private:
  CefRefPtr<CefBrowser> browser_;
  int step_index_;
  std::string output_type_;
  std::string encoded_data_;

  IMPLEMENT_REFCOUNTING(OutputTask);
};

// Custom task for sending detailed error info to playground (must run on UI thread)
class ErrorDetailTask : public CefTask {
 public:
  ErrorDetailTask(CefRefPtr<CefBrowser> browser,
                  const std::string& step_type,
                  const std::string& message,
                  int step_index,
                  const std::string& selector = "",
                  const std::string& url = "",
                  const std::string& status = "",
                  const std::string& error_code = "",
                  int http_status = 0)
      : browser_(browser),
        step_type_(step_type),
        message_(message),
        step_index_(step_index),
        selector_(selector),
        url_(url),
        status_(status),
        error_code_(error_code),
        http_status_(http_status) {}

  void Execute() override {
    if (!browser_) return;

    CefRefPtr<CefFrame> frame = browser_->GetMainFrame();
    if (frame) {
      // Escape strings for JavaScript
      auto escapeJS = [](const std::string& s) -> std::string {
        std::string result;
        for (char c : s) {
          if (c == '\\') result += "\\\\";
          else if (c == '\'') result += "\\'";
          else if (c == '"') result += "\\\"";
          else if (c == '\n') result += "\\n";
          else if (c == '\r') result += "\\r";
          else result += c;
        }
        return result;
      };

      std::ostringstream js;
      js << "if(typeof showPersistentErrorToast === 'function') { showPersistentErrorToast({";
      js << "step_type: '" << escapeJS(step_type_) << "',";
      js << "message: '" << escapeJS(message_) << "',";
      js << "step_index: " << step_index_;

      if (!selector_.empty()) {
        js << ", selector: '" << escapeJS(selector_) << "'";
      }
      if (!url_.empty()) {
        js << ", url: '" << escapeJS(url_) << "'";
      }
      if (!status_.empty()) {
        js << ", status: '" << escapeJS(status_) << "'";
      }
      if (!error_code_.empty()) {
        js << ", error_code: '" << escapeJS(error_code_) << "'";
      }
      if (http_status_ > 0) {
        js << ", http_status: " << http_status_;
      }

      js << "}); }";

      frame->ExecuteJavaScript(js.str(), frame->GetURL(), 0);
    }
  }

 private:
  CefRefPtr<CefBrowser> browser_;
  std::string step_type_;
  std::string message_;
  int step_index_;
  std::string selector_;
  std::string url_;
  std::string status_;
  std::string error_code_;
  int http_status_;

  IMPLEMENT_REFCOUNTING(ErrorDetailTask);
};

// ============================================================================
// Flow Condition Execution Support
// ============================================================================

// Condition structure matching frontend/SDK format
struct TestCondition {
  std::string source;      // "previous" or "step"
  std::string sourceStepId; // Step ID/index when source is "step"
  std::string field;       // Dot-notation path (e.g., "data.count")
  std::string op;          // Operator
  json value;              // Compare value
};

// Forward declaration for recursive structure
struct TestStep;

// TestStep structure with condition support
struct TestStep {
  std::string type;
  bool selected = true;

  // Regular step params
  std::string url;
  std::string selector;
  std::string text;
  std::string value;
  int duration = 0;
  std::string filename;
  std::string query;
  std::string command;
  int fps = 30;
  bool ignoreCache = false;
  std::string key;
  int x = 0;
  int y = 0;
  int timeout = 5000;
  std::string cleanLevel;
  bool includeImages = true;
  bool includeLinks = true;
  std::string templateName;
  std::string name;
  std::string domain;
  std::string cookieName;
  int width = 1280;
  int height = 720;
  int startX = 0;
  int startY = 0;
  int endX = 0;
  int endY = 0;
  std::vector<std::pair<int, int>> midPoints;
  int mouseSteps = 0;
  std::string sourceSelector;
  std::string targetSelector;
  int maxAttempts = 3;
  std::string provider;

  // Condition support
  bool hasCondition = false;
  TestCondition condition;
  std::vector<TestStep> onTrue;
  std::vector<TestStep> onFalse;
};

// Get value at dot-notation path from JSON
static json getValueAtPath(const json& obj, const std::string& path) {
  if (path.empty()) return obj;

  json current = obj;
  std::istringstream iss(path);
  std::string part;

  while (std::getline(iss, part, '.')) {
    if (current.is_null()) return json();

    // Try as array index first (check if all digits)
    bool is_number = !part.empty() && std::all_of(part.begin(), part.end(), ::isdigit);
    if (is_number && current.is_array()) {
      size_t idx = std::stoul(part);
      if (idx < current.size()) {
        current = current[idx];
        continue;
      }
    }

    // Try as object key
    if (current.is_object() && current.contains(part)) {
      current = current[part];
    } else {
      return json();
    }
  }

  return current;
}

// Evaluate a condition against a value (returns true/false)
static bool evaluateCondition(const TestCondition& cond, const json& checkValue) {
  json actual = cond.field.empty() ? checkValue : getValueAtPath(checkValue, cond.field);

  // Helper to check if json is "truthy"
  auto isTruthy = [](const json& v) -> bool {
    if (v.is_null()) return false;
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number()) return v.get<double>() != 0;
    if (v.is_string()) return !v.get<std::string>().empty();
    if (v.is_array()) return !v.empty();
    if (v.is_object()) return !v.empty();
    return false;
  };

  // Helper to check if json is "empty"
  auto isEmpty = [](const json& v) -> bool {
    if (v.is_null()) return true;
    if (v.is_string()) return v.get<std::string>().empty();
    if (v.is_array()) return v.empty();
    if (v.is_object()) return v.empty();
    return false;
  };

  const std::string& op = cond.op;

  if (op == "is_truthy") {
    return isTruthy(actual);
  }
  if (op == "is_falsy") {
    return !isTruthy(actual);
  }
  if (op == "is_empty") {
    return isEmpty(actual);
  }
  if (op == "is_not_empty") {
    return !isEmpty(actual);
  }
  if (op == "equals") {
    return actual == cond.value;
  }
  if (op == "not_equals") {
    return actual != cond.value;
  }
  if (op == "contains") {
    if (actual.is_string() && cond.value.is_string()) {
      return actual.get<std::string>().find(cond.value.get<std::string>()) != std::string::npos;
    }
    if (actual.is_array()) {
      for (const auto& item : actual) {
        if (item == cond.value) return true;
      }
      return false;
    }
    return false;
  }
  if (op == "not_contains") {
    if (actual.is_string() && cond.value.is_string()) {
      return actual.get<std::string>().find(cond.value.get<std::string>()) == std::string::npos;
    }
    if (actual.is_array()) {
      for (const auto& item : actual) {
        if (item == cond.value) return false;
      }
      return true;
    }
    return true;
  }
  if (op == "starts_with") {
    if (actual.is_string() && cond.value.is_string()) {
      const std::string& str = actual.get<std::string>();
      const std::string& prefix = cond.value.get<std::string>();
      return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
    }
    return false;
  }
  if (op == "ends_with") {
    if (actual.is_string() && cond.value.is_string()) {
      const std::string& str = actual.get<std::string>();
      const std::string& suffix = cond.value.get<std::string>();
      return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
    return false;
  }
  if (op == "greater_than") {
    if (actual.is_number() && cond.value.is_number()) {
      return actual.get<double>() > cond.value.get<double>();
    }
    return false;
  }
  if (op == "less_than") {
    if (actual.is_number() && cond.value.is_number()) {
      return actual.get<double>() < cond.value.get<double>();
    }
    return false;
  }
  if (op == "regex_match") {
    if (actual.is_string() && cond.value.is_string()) {
      // Note: Invalid regex patterns will cause undefined behavior with exceptions disabled
      std::regex re(cond.value.get<std::string>(), std::regex::ECMAScript | std::regex::nosubs);
      return std::regex_search(actual.get<std::string>(), re);
    }
    return false;
  }

  return false;
}

// Parse a single TestStep from JSON (recursive for conditions)
static TestStep parseTestStep(const json& j) {
  TestStep step;

  step.type = j.value("type", "");
  step.selected = j.value("selected", true);

  // Regular params
  step.url = j.value("url", "");
  step.selector = j.value("selector", "");
  step.text = j.value("text", "");
  step.value = j.value("value", "");
  step.duration = j.value("duration", 0);
  step.filename = j.value("filename", "");
  step.query = j.value("query", "");
  step.command = j.value("command", "");
  step.fps = j.value("fps", 30);
  step.ignoreCache = j.value("ignoreCache", false);
  step.key = j.value("key", "");
  step.x = j.value("x", 0);
  step.y = j.value("y", 0);
  step.timeout = j.value("timeout", 5000);
  step.cleanLevel = j.value("cleanLevel", "");
  step.includeImages = j.value("includeImages", true);
  step.includeLinks = j.value("includeLinks", true);
  step.templateName = j.value("template", "");
  step.name = j.value("name", "");
  step.domain = j.value("domain", "");
  step.cookieName = j.value("cookieName", "");
  step.width = j.value("width", 1280);
  step.height = j.value("height", 720);
  step.startX = j.value("startX", 0);
  step.startY = j.value("startY", 0);
  step.endX = j.value("endX", 0);
  step.endY = j.value("endY", 0);
  step.mouseSteps = j.value("steps", 0);
  step.sourceSelector = j.value("sourceSelector", "");
  step.targetSelector = j.value("targetSelector", "");
  step.maxAttempts = j.value("maxAttempts", 3);
  step.provider = j.value("provider", "");

  // Parse midPoints array
  if (j.contains("midPoints") && j["midPoints"].is_array()) {
    for (const auto& pt : j["midPoints"]) {
      if (pt.is_array() && pt.size() >= 2) {
        step.midPoints.push_back({pt[0].get<int>(), pt[1].get<int>()});
      }
    }
  }

  // Parse condition (for type="condition")
  if (j.contains("condition") && j["condition"].is_object()) {
    step.hasCondition = true;
    const auto& cond = j["condition"];
    step.condition.source = cond.value("source", "previous");
    step.condition.sourceStepId = cond.value("sourceStepId", "");
    step.condition.field = cond.value("field", "");
    step.condition.op = cond.value("operator", "is_truthy");
    if (cond.contains("value")) {
      step.condition.value = cond["value"];
    }
  }

  // Parse onTrue branch (recursive)
  if (j.contains("onTrue") && j["onTrue"].is_array()) {
    for (const auto& s : j["onTrue"]) {
      step.onTrue.push_back(parseTestStep(s));
    }
  }

  // Parse onFalse branch (recursive)
  if (j.contains("onFalse") && j["onFalse"].is_array()) {
    for (const auto& s : j["onFalse"]) {
      step.onFalse.push_back(parseTestStep(s));
    }
  }

  return step;
}

// Parse all test steps from JSON string
static std::vector<TestStep> parseTestSteps(const std::string& test_json) {
  std::vector<TestStep> steps;

  // Use accept() to validate JSON before parsing (no exceptions)
  if (!json::accept(test_json)) {
    LOG_ERROR("UIBrowser", "Failed to parse test JSON: invalid JSON format");
    return steps;
  }

  json j = json::parse(test_json);

  // Handle array of steps directly
  if (j.is_array()) {
    for (const auto& step_json : j) {
      steps.push_back(parseTestStep(step_json));
    }
  }
  // Handle object with steps array (e.g., {name: "...", steps: [...]})
  else if (j.is_object() && j.contains("steps") && j["steps"].is_array()) {
    for (const auto& step_json : j["steps"]) {
      steps.push_back(parseTestStep(step_json));
    }
  }

  return steps;
}

// Format condition for display
static std::string formatCondition(const TestCondition& cond) {
  static const std::map<std::string, std::string> opLabels = {
    {"equals", "=="},
    {"not_equals", "!="},
    {"contains", "contains"},
    {"not_contains", "not contains"},
    {"starts_with", "starts with"},
    {"ends_with", "ends with"},
    {"greater_than", ">"},
    {"less_than", "<"},
    {"is_truthy", "is truthy"},
    {"is_falsy", "is falsy"},
    {"is_empty", "is empty"},
    {"is_not_empty", "is not empty"},
    {"regex_match", "matches regex"}
  };

  std::string source = (cond.source == "step") ? ("Step " + cond.sourceStepId) : "Previous";
  std::string field = cond.field.empty() ? "" : ("." + cond.field);

  auto it = opLabels.find(cond.op);
  std::string opLabel = (it != opLabels.end()) ? it->second : cond.op;

  static const std::set<std::string> noValueOps = {"is_truthy", "is_falsy", "is_empty", "is_not_empty"};
  if (noValueOps.count(cond.op)) {
    return source + field + " " + opLabel;
  }

  std::string valueStr;
  if (cond.value.is_string()) {
    valueStr = "\"" + cond.value.get<std::string>() + "\"";
  } else {
    valueStr = cond.value.dump();
  }

  return source + field + " " + opLabel + " " + valueStr;
}

// ============================================================================
// End of Flow Condition Support
// ============================================================================

void OwlUIBrowser::ExecuteTest(const std::string& test_json, CefRefPtr<CefBrowser> playground_browser) {
  LOG_DEBUG("UIBrowser", "ExecuteTest called with JSON: " + test_json);

  // Validate JSON has array structure
  size_t start = test_json.find('[');
  size_t end = test_json.rfind(']');

  if (start == std::string::npos || end == std::string::npos) {
    SendProgressUpdate(playground_browser, "error", "Invalid test JSON format", -1, -1);
    return;
  }

  // Get the context ID for the main browser (the one that will execute the test)
  // IMPORTANT: Tests must execute on the main browser, not the playground browser!
  if (!main_browser_) {
    SendProgressUpdate(playground_browser, "error", "Main browser not available. Please ensure main window is open.", -1, -1);
    LOG_ERROR("UIBrowser", "Cannot execute test: main_browser_ is NULL");
    return;
  }

  CefRefPtr<CefBrowser> target_browser = main_browser_;

  std::ostringstream ctx_stream;
  ctx_stream << "ctx_" << std::setfill('0') << std::setw(6) << target_browser->GetIdentifier();
  std::string context_id = ctx_stream.str();

  LOG_DEBUG("UIBrowser", "Executing test on context: " + context_id);

  // Focus the main browser window so user can see the test executing
  OwlUIDelegate* delegate = OwlUIDelegate::GetInstance();
  if (delegate) {
    delegate->FocusWindow();
    LOG_DEBUG("UIBrowser", "Focused main browser window for test execution");
  }

  // Execute test in background thread to avoid blocking UI
  std::thread([playground_browser, test_json, context_id]() {
    // Parse test steps using nlohmann/json (supports nested conditions)
    std::vector<TestStep> test_steps = parseTestSteps(test_json);

    if (test_steps.empty()) {
      LOG_ERROR("UIBrowser", "No valid steps parsed from JSON");
      CefPostTask(TID_UI, new ProgressUpdateTask(playground_browser, "error", "No valid steps in test", -1, -1));
      return;
    }

    LOG_DEBUG("UIBrowser", "Parsed " + std::to_string(test_steps.size()) + " test steps (with nlohmann/json)");

    // Helper function for base64 encoding strings
    auto base64EncodeString = [](const std::string& input) -> std::string {
      std::vector<uint8_t> data(input.begin(), input.end());
      return OwlCaptchaUtils::Base64Encode(data);
    };

    // Show automation overlay on the target browser
    OwlBrowserManager* manager = OwlBrowserManager::GetInstance();
    CefRefPtr<CefBrowser> target_browser = manager->GetBrowser(context_id);
    if (target_browser) {
      CefRefPtr<CefFrame> target_frame = target_browser->GetMainFrame();
      if (target_frame) {
        OwlNLA::ShowAutomationOverlay(target_frame);
        LOG_DEBUG("UIBrowser", "Automation overlay shown on target browser");
      }
    }

    // Execute steps sequentially
    int total_steps = test_steps.size();

    // Track timing
    auto test_start_time = std::chrono::steady_clock::now();
    auto step_start_time = test_start_time;

    // Track step results for condition evaluation
    json previousResult = json();  // Result of the last executed step
    std::map<std::string, json> stepResults;  // Results by step index (for source: "step")

    for (size_t i = 0; i < test_steps.size(); i++) {
      const TestStep& step = test_steps[i];
      int current = i + 1;

      // Reset step timer
      step_start_time = std::chrono::steady_clock::now();

      LOG_DEBUG("UIBrowser", "Executing step " + std::to_string(current) + "/" + std::to_string(total_steps) + ": " + step.type);

      // Send progress update to playground
      std::string status_msg;
      if (step.type == "navigate") {
        status_msg = "Navigating to " + step.url;
      } else if (step.type == "click") {
        status_msg = "Clicking " + step.selector;
      } else if (step.type == "type") {
        status_msg = "Typing into " + step.selector;
      } else if (step.type == "pick") {
        status_msg = "Selecting from " + step.selector;
      } else if (step.type == "wait") {
        status_msg = "Waiting " + std::to_string(step.duration) + "ms";
      } else if (step.type == "screenshot") {
        status_msg = "Taking screenshot";
      } else if (step.type == "extract") {
        status_msg = "Extracting text from " + step.selector;
      } else if (step.type == "submit_form") {
        status_msg = "Submitting form";
      } else if (step.type == "query") {
        status_msg = "Querying page: " + step.query;
      } else if (step.type == "nla") {
        status_msg = "Executing: " + step.command;
      } else if (step.type == "solve_captcha") {
        status_msg = "Solving CAPTCHA";
        if (!step.provider.empty() && step.provider != "auto") {
          status_msg += " (" + step.provider + ")";
        }
      } else if (step.type == "highlight") {
        status_msg = "Highlighting " + step.selector;
      } else if (step.type == "scroll_up") {
        status_msg = "Scrolling to top";
      } else if (step.type == "scroll_down") {
        status_msg = "Scrolling to bottom";
      } else if (step.type == "record_video") {
        status_msg = "Starting video recording";
      } else if (step.type == "stop_video") {
        status_msg = "Stopping video recording";
      } else if (step.type == "reload") {
        status_msg = step.ignoreCache ? "Hard reloading page" : "Reloading page";
      } else if (step.type == "go_back") {
        status_msg = "Going back";
      } else if (step.type == "go_forward") {
        status_msg = "Going forward";
      } else if (step.type == "press_key") {
        status_msg = "Pressing key: " + step.key;
      } else if (step.type == "scroll_by") {
        status_msg = "Scrolling by " + std::to_string(step.x) + "x" + std::to_string(step.y);
      } else if (step.type == "scroll_to_element") {
        status_msg = "Scrolling to " + step.selector;
      } else if (step.type == "wait_for_selector") {
        status_msg = "Waiting for " + step.selector;
      } else if (step.type == "get_html") {
        status_msg = "Getting HTML";
      } else if (step.type == "get_markdown") {
        status_msg = "Getting Markdown";
      } else if (step.type == "extract_json") {
        status_msg = "Extracting JSON";
      } else if (step.type == "get_page_info") {
        status_msg = "Getting page info";
      } else if (step.type == "summarize_page") {
        status_msg = "Summarizing page";
      } else if (step.type == "detect_captcha") {
        status_msg = "Detecting CAPTCHA";
      } else if (step.type == "classify_captcha") {
        status_msg = "Classifying CAPTCHA";
      } else if (step.type == "get_cookies") {
        status_msg = "Getting cookies";
      } else if (step.type == "set_cookie") {
        status_msg = "Setting cookie: " + step.name;
      } else if (step.type == "delete_cookies") {
        status_msg = "Deleting cookies";
      } else if (step.type == "set_viewport") {
        status_msg = "Setting viewport: " + std::to_string(step.width) + "x" + std::to_string(step.height);
      } else if (step.type == "drag_drop") {
        status_msg = "Dragging from " + std::to_string(step.startX) + "x" + std::to_string(step.startY) +
                     " to " + std::to_string(step.endX) + "x" + std::to_string(step.endY);
        if (!step.midPoints.empty()) {
          status_msg += " (" + std::to_string(step.midPoints.size()) + " waypoints)";
        }
      } else if (step.type == "html5_drag_drop") {
        status_msg = "HTML5 drag: " + step.sourceSelector + " → " + step.targetSelector;
      } else if (step.type == "mouse_move") {
        status_msg = "Moving mouse from " + std::to_string(step.startX) + "x" + std::to_string(step.startY) +
                     " to " + std::to_string(step.endX) + "x" + std::to_string(step.endY);
      } else if (step.type == "condition") {
        status_msg = "Evaluating: " + formatCondition(step.condition);
      }

      std::string status_msg_copy = status_msg;
      CefPostTask(TID_UI, new ProgressUpdateTask(playground_browser, "running", status_msg_copy, current, total_steps));

      // Execute the step
      bool success = false;
      ActionResult lastActionResult;  // Store last action result for error reporting
      json stepResult;  // To track step result for conditions

      if (step.type == "navigate") {
          lastActionResult = manager->Navigate(context_id, step.url);
          success = (lastActionResult.status == ActionStatus::OK);
          if (success) {
            // Wait for page to load
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));

            // Re-inject overlay after navigation (DOM was replaced)
            CefRefPtr<CefBrowser> nav_browser = manager->GetBrowser(context_id);
            if (nav_browser) {
              CefRefPtr<CefFrame> nav_frame = nav_browser->GetMainFrame();
              if (nav_frame) {
                OwlNLA::ShowAutomationOverlay(nav_frame);
                LOG_DEBUG("UIBrowser", "Re-injected automation overlay after navigation");

                // Update with current timing after re-injection
                auto nav_end_time = std::chrono::steady_clock::now();
                double nav_step_elapsed = std::chrono::duration<double, std::milli>(nav_end_time - step_start_time).count();
                double nav_total_elapsed = std::chrono::duration<double, std::milli>(nav_end_time - test_start_time).count();
                OwlNLA::UpdateAutomationStep(nav_frame, current, total_steps, status_msg, nav_step_elapsed, nav_total_elapsed);
              }
            }
          }
        } else if (step.type == "click") {
          lastActionResult = manager->Click(context_id, step.selector);
          success = (lastActionResult.status == ActionStatus::OK);
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } else if (step.type == "type") {
          lastActionResult = manager->Type(context_id, step.selector, step.text);
          success = (lastActionResult.status == ActionStatus::OK);
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } else if (step.type == "pick") {
          lastActionResult = manager->Pick(context_id, step.selector, step.value);
          success = (lastActionResult.status == ActionStatus::OK);
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
        } else if (step.type == "wait") {
          std::this_thread::sleep_for(std::chrono::milliseconds(step.duration));
          success = true;
        } else if (step.type == "screenshot") {
          auto screenshot = manager->Screenshot(context_id);
          success = !screenshot.empty();
          if (success) {
            std::string screenshot_b64 = OwlCaptchaUtils::Base64Encode(screenshot);
            // Must post to UI thread for JavaScript execution
            CefPostTask(TID_UI, new OutputTask(playground_browser, current, "screenshot", screenshot_b64));
          }
        } else if (step.type == "extract") {
          std::string text = manager->ExtractText(context_id, step.selector);
          success = !text.empty();
          LOG_DEBUG("UIBrowser", "Extracted text: " + text.substr(0, std::min((size_t)100, text.length())));
          if (success) {
            std::string encoded = base64EncodeString(text);
            CefPostTask(TID_UI, new OutputTask(playground_browser, current, "extract", encoded));
          }
        } else if (step.type == "submit_form") {
          lastActionResult = manager->SubmitForm(context_id);
          success = (lastActionResult.status == ActionStatus::OK);
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } else if (step.type == "query") {
          std::string result = manager->QueryPage(context_id, step.query);
          success = !result.empty();
          LOG_DEBUG("UIBrowser", "Query result: " + result.substr(0, std::min((size_t)200, result.length())));
          if (success) {
            std::string encoded = base64EncodeString(result);
            CefPostTask(TID_UI, new OutputTask(playground_browser, current, "query", encoded));
          }
        } else if (step.type == "nla") {
          std::string result = manager->ExecuteNLA(context_id, step.command);
          success = !result.empty();
          LOG_DEBUG("UIBrowser", "NLA result: " + result.substr(0, std::min((size_t)200, result.length())));
        } else if (step.type == "solve_captcha") {
          std::string provider = step.provider.empty() ? "auto" : step.provider;
          std::string result = manager->SolveCaptcha(context_id, step.maxAttempts, provider);
          success = !result.empty() && result.find("error") == std::string::npos;
          LOG_DEBUG("UIBrowser", "CAPTCHA solve result (provider=" + provider + ", maxAttempts=" + std::to_string(step.maxAttempts) + "): " + result);
          std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        } else if (step.type == "highlight") {
          lastActionResult = manager->Highlight(context_id, step.selector, "#FF0000", "rgba(255, 0, 0, 0.2)");
          success = (lastActionResult.status == ActionStatus::OK);
          std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        } else if (step.type == "scroll_up") {
          lastActionResult = manager->ScrollToTop(context_id);
          success = (lastActionResult.status == ActionStatus::OK);
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } else if (step.type == "scroll_down") {
          lastActionResult = manager->ScrollToBottom(context_id);
          success = (lastActionResult.status == ActionStatus::OK);
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } else if (step.type == "record_video") {
          success = manager->StartVideoRecording(context_id, step.fps, "libx264");
          LOG_DEBUG("UIBrowser", "Video recording started with " + std::to_string(step.fps) + " fps");
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
        } else if (step.type == "stop_video") {
          std::string video_path = manager->StopVideoRecording(context_id);
          success = !video_path.empty();
          LOG_DEBUG("UIBrowser", "Video saved to: " + video_path);
          // Send video path to output tab
          if (success) {
            std::string encoded = base64EncodeString(video_path);
            CefPostTask(TID_UI, new OutputTask(playground_browser, current, "video", encoded));
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } else if (step.type == "reload") {
          lastActionResult = manager->Reload(context_id, step.ignoreCache);
          success = (lastActionResult.status == ActionStatus::OK);
          if (success) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            // Re-inject overlay after reload
            CefRefPtr<CefBrowser> reload_browser = manager->GetBrowser(context_id);
            if (reload_browser) {
              CefRefPtr<CefFrame> reload_frame = reload_browser->GetMainFrame();
              if (reload_frame) {
                OwlNLA::ShowAutomationOverlay(reload_frame);
              }
            }
          }
        } else if (step.type == "go_back") {
          lastActionResult = manager->GoBack(context_id);
          success = (lastActionResult.status == ActionStatus::OK);
          if (success) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            CefRefPtr<CefBrowser> back_browser = manager->GetBrowser(context_id);
            if (back_browser) {
              CefRefPtr<CefFrame> back_frame = back_browser->GetMainFrame();
              if (back_frame) {
                OwlNLA::ShowAutomationOverlay(back_frame);
              }
            }
          }
        } else if (step.type == "go_forward") {
          lastActionResult = manager->GoForward(context_id);
          success = (lastActionResult.status == ActionStatus::OK);
          if (success) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            CefRefPtr<CefBrowser> fwd_browser = manager->GetBrowser(context_id);
            if (fwd_browser) {
              CefRefPtr<CefFrame> fwd_frame = fwd_browser->GetMainFrame();
              if (fwd_frame) {
                OwlNLA::ShowAutomationOverlay(fwd_frame);
              }
            }
          }
        } else if (step.type == "press_key") {
          lastActionResult = manager->PressKey(context_id, step.key);
          success = (lastActionResult.status == ActionStatus::OK);
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
        } else if (step.type == "scroll_by") {
          lastActionResult = manager->ScrollBy(context_id, step.x, step.y);
          success = (lastActionResult.status == ActionStatus::OK);
          std::this_thread::sleep_for(std::chrono::milliseconds(300));
        } else if (step.type == "scroll_to_element") {
          lastActionResult = manager->ScrollToElement(context_id, step.selector);
          success = (lastActionResult.status == ActionStatus::OK);
          std::this_thread::sleep_for(std::chrono::milliseconds(300));
        } else if (step.type == "wait_for_selector") {
          lastActionResult = manager->WaitForSelector(context_id, step.selector, step.timeout);
          success = (lastActionResult.status == ActionStatus::OK);
        } else if (step.type == "get_html") {
          std::string html = manager->GetHTML(context_id, step.cleanLevel.empty() ? "basic" : step.cleanLevel);
          success = !html.empty();
          if (success) {
            std::string encoded = base64EncodeString(html);
            CefPostTask(TID_UI, new OutputTask(playground_browser, current, "get_html", encoded));
          }
        } else if (step.type == "get_markdown") {
          std::string markdown = manager->GetMarkdown(context_id, step.includeImages, step.includeLinks, -1);
          success = !markdown.empty();
          if (success) {
            std::string encoded = base64EncodeString(markdown);
            CefPostTask(TID_UI, new OutputTask(playground_browser, current, "get_markdown", encoded));
          }
        } else if (step.type == "extract_json") {
          std::string json = manager->ExtractJSON(context_id, step.templateName);
          success = !json.empty();
          if (success) {
            std::string encoded = base64EncodeString(json);
            CefPostTask(TID_UI, new OutputTask(playground_browser, current, "extract_json", encoded));
          }
        } else if (step.type == "get_page_info") {
          std::string info = manager->GetPageInfo(context_id);
          success = !info.empty();
          if (success) {
            std::string encoded = base64EncodeString(info);
            CefPostTask(TID_UI, new OutputTask(playground_browser, current, "get_page_info", encoded));
          }
        } else if (step.type == "summarize_page") {
          std::string summary = manager->SummarizePage(context_id, false);
          success = !summary.empty();
          if (success) {
            std::string encoded = base64EncodeString(summary);
            CefPostTask(TID_UI, new OutputTask(playground_browser, current, "summarize_page", encoded));
          }
        } else if (step.type == "detect_captcha") {
          std::string result = manager->DetectCaptcha(context_id);
          success = !result.empty();
          if (success) {
            std::string encoded = base64EncodeString(result);
            CefPostTask(TID_UI, new OutputTask(playground_browser, current, "detect_captcha", encoded));
          }
        } else if (step.type == "classify_captcha") {
          std::string result = manager->ClassifyCaptcha(context_id);
          success = !result.empty();
          if (success) {
            std::string encoded = base64EncodeString(result);
            CefPostTask(TID_UI, new OutputTask(playground_browser, current, "classify_captcha", encoded));
          }
        } else if (step.type == "get_cookies") {
          std::string cookies = manager->GetCookies(context_id, step.url);
          success = true; // Even empty cookies is valid
          std::string encoded = base64EncodeString(cookies);
          CefPostTask(TID_UI, new OutputTask(playground_browser, current, "get_cookies", encoded));
        } else if (step.type == "set_cookie") {
          lastActionResult = manager->SetCookie(context_id, step.url, step.name, step.value, step.domain, "/", false, false, "lax", -1);
          success = (lastActionResult.status == ActionStatus::OK);
        } else if (step.type == "delete_cookies") {
          lastActionResult = manager->DeleteCookies(context_id, step.url, step.cookieName);
          success = (lastActionResult.status == ActionStatus::OK);
        } else if (step.type == "set_viewport") {
          lastActionResult = manager->SetViewport(context_id, step.width, step.height);
          success = (lastActionResult.status == ActionStatus::OK);
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
        } else if (step.type == "drag_drop") {
          lastActionResult = manager->DragDrop(context_id, step.startX, step.startY, step.endX, step.endY, step.midPoints);
          success = (lastActionResult.status == ActionStatus::OK);
          std::this_thread::sleep_for(std::chrono::milliseconds(300));
        } else if (step.type == "html5_drag_drop") {
          lastActionResult = manager->HTML5DragDrop(context_id, step.sourceSelector, step.targetSelector);
          success = (lastActionResult.status == ActionStatus::OK);
          std::this_thread::sleep_for(std::chrono::milliseconds(300));
        } else if (step.type == "mouse_move") {
          lastActionResult = manager->MouseMove(context_id, step.startX, step.startY, step.endX, step.endY, step.mouseSteps);
          success = (lastActionResult.status == ActionStatus::OK);
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } else if (step.type == "condition") {
          // Evaluate condition against previous result or specific step result
          json checkValue = previousResult;
          if (step.condition.source == "step" && !step.condition.sourceStepId.empty()) {
            auto it = stepResults.find(step.condition.sourceStepId);
            if (it != stepResults.end()) {
              checkValue = it->second;
            }
          }

          bool conditionResult = evaluateCondition(step.condition, checkValue);
          std::string branchTaken = conditionResult ? "true" : "false";
          LOG_DEBUG("UIBrowser", "Condition evaluated to: " + branchTaken);

          // Store condition result
          json condResult;
          condResult["conditionResult"] = conditionResult;
          condResult["branchTaken"] = branchTaken;
          previousResult = condResult;
          stepResults[std::to_string(i)] = condResult;

          // Update overlay with condition result
          std::string cond_status = "Condition: " + branchTaken + " → executing " +
                                    (conditionResult ? "Then" : "Else") + " branch";
          CefRefPtr<CefBrowser> cond_browser = manager->GetBrowser(context_id);
          if (cond_browser) {
            CefRefPtr<CefFrame> cond_frame = cond_browser->GetMainFrame();
            if (cond_frame) {
              auto cond_end_time = std::chrono::steady_clock::now();
              double cond_step_ms = std::chrono::duration<double, std::milli>(cond_end_time - step_start_time).count();
              double cond_total_ms = std::chrono::duration<double, std::milli>(cond_end_time - test_start_time).count();
              OwlNLA::UpdateAutomationStep(cond_frame, current, total_steps, cond_status, cond_step_ms, cond_total_ms);
            }
          }

          // Execute the appropriate branch
          const std::vector<TestStep>& branchSteps = conditionResult ? step.onTrue : step.onFalse;

          if (!branchSteps.empty()) {
            LOG_DEBUG("UIBrowser", "Executing " + std::to_string(branchSteps.size()) + " steps in " +
                      (conditionResult ? "Then" : "Else") + " branch");

            // Execute branch steps (simplified - doesn't handle nested conditions yet)
            for (size_t bi = 0; bi < branchSteps.size(); bi++) {
              const TestStep& branchStep = branchSteps[bi];

              if (!branchStep.selected) continue;

              // Send branch step status
              std::string branch_msg = (conditionResult ? "[Then] " : "[Else] ") + branchStep.type;
              if (!branchStep.selector.empty()) branch_msg += ": " + branchStep.selector;
              else if (!branchStep.url.empty()) branch_msg += ": " + branchStep.url;

              CefPostTask(TID_UI, new ProgressUpdateTask(playground_browser, "running", branch_msg, current, total_steps));

              // Execute the branch step
              bool branchSuccess = false;
              json branchResult;

              if (branchStep.type == "navigate") {
                ActionResult br_nav = manager->Navigate(context_id, branchStep.url);
                branchSuccess = (br_nav.status == ActionStatus::OK);
                if (branchSuccess) {
                  std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                  CefRefPtr<CefBrowser> nav_br = manager->GetBrowser(context_id);
                  if (nav_br && nav_br->GetMainFrame()) {
                    OwlNLA::ShowAutomationOverlay(nav_br->GetMainFrame());
                  }
                }
                branchResult["success"] = branchSuccess;
              } else if (branchStep.type == "click") {
                ActionResult r = manager->Click(context_id, branchStep.selector);
                branchSuccess = (r.status == ActionStatus::OK);
                branchResult["success"] = branchSuccess;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
              } else if (branchStep.type == "type") {
                ActionResult r = manager->Type(context_id, branchStep.selector, branchStep.text);
                branchSuccess = (r.status == ActionStatus::OK);
                branchResult["success"] = branchSuccess;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
              } else if (branchStep.type == "wait") {
                std::this_thread::sleep_for(std::chrono::milliseconds(branchStep.duration));
                branchSuccess = true;
                branchResult["success"] = true;
              } else if (branchStep.type == "screenshot") {
                auto screenshot = manager->Screenshot(context_id);
                branchSuccess = !screenshot.empty();
                if (branchSuccess) {
                  std::string screenshot_b64 = OwlCaptchaUtils::Base64Encode(screenshot);
                  CefPostTask(TID_UI, new OutputTask(playground_browser, current, "screenshot", screenshot_b64));
                }
                branchResult["success"] = branchSuccess;
              } else if (branchStep.type == "extract") {
                std::string text = manager->ExtractText(context_id, branchStep.selector);
                branchSuccess = !text.empty();
                branchResult["success"] = branchSuccess;
                branchResult["text"] = text;
                if (branchSuccess) {
                  std::string encoded = base64EncodeString(text);
                  CefPostTask(TID_UI, new OutputTask(playground_browser, current, "extract", encoded));
                }
              } else if (branchStep.type == "scroll_up") {
                ActionResult br_su = manager->ScrollToTop(context_id);
                branchSuccess = (br_su.status == ActionStatus::OK);
                branchResult["success"] = branchSuccess;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
              } else if (branchStep.type == "scroll_down") {
                ActionResult br_sd = manager->ScrollToBottom(context_id);
                branchSuccess = (br_sd.status == ActionStatus::OK);
                branchResult["success"] = branchSuccess;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
              } else if (branchStep.type == "press_key") {
                ActionResult br_pk = manager->PressKey(context_id, branchStep.key);
                branchSuccess = (br_pk.status == ActionStatus::OK);
                branchResult["success"] = branchSuccess;
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
              } else if (branchStep.type == "highlight") {
                ActionResult br_hl = manager->Highlight(context_id, branchStep.selector, "#FF0000", "rgba(255, 0, 0, 0.2)");
                branchSuccess = (br_hl.status == ActionStatus::OK);
                branchResult["success"] = branchSuccess;
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
              } else {
                // Unsupported step type in branch - skip but log
                LOG_DEBUG("UIBrowser", "Skipping unsupported branch step type: " + branchStep.type);
                branchSuccess = true;
                branchResult["skipped"] = true;
              }

              previousResult = branchResult;

              if (!branchSuccess) {
                LOG_ERROR("UIBrowser", "Branch step failed: " + branchStep.type);
                CefPostTask(TID_UI, new ProgressUpdateTask(playground_browser, "error",
                  "Branch step failed: " + branchStep.type, current, total_steps));
                // Continue with other branch steps instead of failing entire test
              }

              std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
          } else {
            LOG_DEBUG("UIBrowser", "Branch is empty, continuing");
          }

          success = true;  // Condition evaluation itself succeeded
        }

        // Store step result for condition evaluation (if not already stored by condition step)
        if (step.type != "condition") {
          stepResult["success"] = success;
          stepResult["type"] = step.type;
          previousResult = stepResult;
          stepResults[std::to_string(i)] = stepResult;
        }

        // Update timing after step completes
        auto step_end_time = std::chrono::steady_clock::now();
        double step_elapsed_ms = std::chrono::duration<double, std::milli>(step_end_time - step_start_time).count();
        double total_elapsed_ms = std::chrono::duration<double, std::milli>(step_end_time - test_start_time).count();

        // Update automation overlay with timing
        CefRefPtr<CefBrowser> timing_browser = manager->GetBrowser(context_id);
        if (timing_browser) {
          CefRefPtr<CefFrame> timing_frame = timing_browser->GetMainFrame();
          if (timing_frame) {
            OwlNLA::UpdateAutomationStep(timing_frame, current, total_steps, status_msg, step_elapsed_ms, total_elapsed_ms);
          }
        }

        if (!success) {
          LOG_ERROR("UIBrowser", "Step " + std::to_string(current) + " failed: " + step.type);

          // Use ActionResult for detailed error information
          std::string error_msg = !lastActionResult.message.empty()
            ? lastActionResult.message
            : ActionStatusToMessage(lastActionResult.status);
          std::string error_selector = !lastActionResult.selector.empty()
            ? lastActionResult.selector
            : step.selector;
          std::string error_url = !lastActionResult.url.empty()
            ? lastActionResult.url
            : step.url;
          std::string error_status = ActionStatusToCode(lastActionResult.status);

          // Fallback for actions that don't return ActionResult (screenshot, extract, etc.)
          if (lastActionResult.status == ActionStatus::OK && !success) {
            error_msg = "Action failed: " + step.type;
            error_status = "unknown";
          }

          // Send status update for badge
          CefPostTask(TID_UI, new ProgressUpdateTask(playground_browser, "error", error_msg, current, total_steps));

          // Send detailed error toast with full ActionResult information
          CefPostTask(TID_UI, new ErrorDetailTask(
            playground_browser,
            step.type,
            error_msg,
            static_cast<int>(i),  // step_index (0-based)
            error_selector,
            error_url,
            error_status,
            lastActionResult.error_code,
            lastActionResult.http_status
          ));

          // Show error state on automation overlay before hiding
          CefRefPtr<CefBrowser> target_browser = manager->GetBrowser(context_id);
          if (target_browser) {
            CefRefPtr<CefFrame> target_frame = target_browser->GetMainFrame();
            if (target_frame) {
              // Update overlay to show error state
              std::string error_overlay_js = R"(
(function() {
  const overlay = document.getElementById('owl-automation-overlay');
  const stepEl = document.getElementById('owl-automation-step');
  const dot = overlay.querySelector('.pulse-dot');

  if (overlay) {
    // Stop pulsing and show error state
    if (dot) {
      dot.style.animation = 'none';
      dot.style.background = '#ea4335';
      dot.innerHTML = '<svg width="8" height="8" viewBox="0 0 8 8" fill="white"><path d="M1 1L7 7M7 1L1 7" stroke="white" stroke-width="1.5" stroke-linecap="round"/></svg>';
      dot.style.display = 'flex';
      dot.style.alignItems = 'center';
      dot.style.justifyContent = 'center';
    }

    if (stepEl) {
      stepEl.innerHTML = `
        <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 4px;">
          <span style="font-weight: 600; color: #ea4335;">✕ Step Failed</span>
          <span style="font-size: 11px; color: #ea4335; font-weight: 500;">)" + step.type + R"(</span>
        </div>
        <div style="color: #5f6368; margin-bottom: 8px;">
          )" + error_msg + R"(
        </div>
        <div style="display: flex; align-items: center; gap: 6px; padding-top: 6px; border-top: 1px solid rgba(0,0,0,0.08);">
          <span style="font-size: 11px; color: #ea4335;">Step )" + std::to_string(current) + "/" + std::to_string(total_steps) + R"(</span>
        </div>
      `;
      stepEl.style.borderLeft = '3px solid #ea4335';
      stepEl.style.background = 'rgba(234, 67, 53, 0.05)';
    }

    // Auto-hide after 5 seconds
    setTimeout(() => {
      if (overlay) {
        overlay.style.opacity = '0';
        setTimeout(() => overlay.remove(), 300);
      }
    }, 5000);
  }
})();
)";
              target_frame->ExecuteJavaScript(error_overlay_js, target_frame->GetURL(), 0);
            }
          }

          return;
        }
    }

    // Test completed successfully
    LOG_DEBUG("UIBrowser", "Test execution completed successfully");
    std::string success_msg = "Test completed: " + std::to_string(total_steps) + " steps";
    CefPostTask(TID_UI, new ProgressUpdateTask(playground_browser, "success", success_msg, -1, -1));

    // Keep overlay visible but mark as completed
    CefRefPtr<CefBrowser> success_browser = manager->GetBrowser(context_id);
    if (success_browser) {
      CefRefPtr<CefFrame> success_frame = success_browser->GetMainFrame();
      if (success_frame) {
        // Calculate final total time
        auto final_time = std::chrono::steady_clock::now();
        double final_total_ms = std::chrono::duration<double, std::milli>(final_time - test_start_time).count();

        // Show completion state in overlay
        std::string completion_js = R"(
(function() {
  const overlay = document.getElementById('owl-automation-overlay');
  const stepEl = document.getElementById('owl-automation-step');
  const dot = overlay.querySelector('.pulse-dot');

  // Stop pulsing animation and change to green checkmark
  if (dot) {
    dot.style.animation = 'none';
    dot.style.background = '#34a853';
    dot.innerHTML = '<svg width="8" height="6" viewBox="0 0 8 6" fill="white"><path d="M1 3L3 5L7 1" stroke="white" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/></svg>';
    dot.style.display = 'flex';
    dot.style.alignItems = 'center';
    dot.style.justifyContent = 'center';
  }

  // Update step info to show completion
  if (stepEl) {
    stepEl.innerHTML = `
      <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 4px;">
        <span style="font-weight: 600; color: #34a853;">✓ Test Completed</span>
        <span style="font-size: 11px; color: #34a853; font-weight: 500;">)" + std::to_string(total_steps) + R"( steps</span>
      </div>
      <div style="color: #5f6368; margin-bottom: 8px;">
        All automation steps completed successfully
      </div>
      <div style="display: flex; align-items: center; gap: 6px; padding-top: 6px; border-top: 1px solid rgba(0,0,0,0.08);">
        <svg width="12" height="12" viewBox="0 0 12 12" fill="none">
          <circle cx="6" cy="6" r="5" stroke="#5f6368" stroke-width="1.5"/>
          <path d="M6 3v3l2 2" stroke="#5f6368" stroke-width="1.5" stroke-linecap="round"/>
        </svg>
        <span style="font-size: 11px; color: #5f6368;">Total: )" + std::to_string(static_cast<int>(final_total_ms < 1000 ? final_total_ms : final_total_ms / 1000.0)) + (final_total_ms < 1000 ? "ms" : "s") + R"(</span>
      </div>
    `;
    stepEl.style.borderLeft = '3px solid #34a853';
    stepEl.style.background = 'rgba(52, 168, 83, 0.05)';
  }

  // Add close button functionality
  const historyBtn = document.getElementById('olib-history-btn');
  if (historyBtn && historyBtn.textContent.trim() === 'History') {
    // Emphasize the history button
    historyBtn.style.background = 'rgba(52, 168, 83, 0.15)';
    historyBtn.style.color = '#34a853';
  }
})();
)";
        success_frame->ExecuteJavaScript(completion_js, success_frame->GetURL(), 0);
        LOG_DEBUG("UIBrowser", "Automation overlay marked as completed (kept visible for history review)");
      }
    }
  }).detach();
}

void OwlUIBrowser::BringBrowserToFront(CefRefPtr<CefBrowser> browser) {
  if (!browser) return;

#if defined(OS_MACOSX)
  // Forward to native implementation in .mm file
  extern void BringBrowserWindowToFront(CefRefPtr<CefBrowser> browser);
  BringBrowserWindowToFront(browser);
#endif
}

void OwlUIBrowser::InjectElementPickerOverlay(CefRefPtr<CefFrame> main_frame, CefRefPtr<CefBrowser> playground_browser, const std::string& input_id) {
  if (!main_frame || !playground_browser) return;

  // Escape input_id for JavaScript
  std::string escaped_input_id = input_id;
  size_t pos = 0;
  while ((pos = escaped_input_id.find("'", pos)) != std::string::npos) {
    escaped_input_id.replace(pos, 1, "\\'");
    pos += 2;
  }

  // Build JavaScript with embedded input ID
  std::ostringstream picker_js;
  picker_js << R"(
(function() {
  // Remove existing picker if present
  const existing = document.getElementById('olib-element-picker-overlay');
  if (existing) existing.remove();

  // Create overlay container
  const overlay = document.createElement('div');
  overlay.id = 'olib-element-picker-overlay';
  overlay.setAttribute('data-owl-ignore', 'true');
  overlay.style.cssText = `
    position: fixed;
    top: 0;
    left: 0;
    width: 100%;
    height: 100%;
    z-index: 2147483646;
    cursor: crosshair;
  `;

  // Add styles
  const style = document.createElement('style');
  style.textContent = `
    .olib-picker-highlight {
      outline: 2px solid #4285f4 !important;
      outline-offset: 2px !important;
      background: rgba(66, 133, 244, 0.1) !important;
      cursor: pointer !important;
      position: relative !important;
    }
    .olib-picker-highlight:hover {
      outline: 2px solid #1967d2 !important;
      background: rgba(66, 133, 244, 0.2) !important;
    }
    #olib-element-picker-overlay {
      pointer-events: none !important;
    }
    #olib-element-picker-overlay * {
      pointer-events: auto !important;
    }
    .olib-picker-confirmation {
      position: fixed;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      background: white;
      border-radius: 16px;
      padding: 24px;
      box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);
      border: 2px solid rgba(66, 133, 244, 0.3);
      z-index: 2147483647;
      min-width: 400px;
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
    }
    .olib-picker-title {
      font-size: 18px;
      font-weight: 600;
      color: #202124;
      margin-bottom: 12px;
    }
    .olib-picker-selector {
      background: rgba(66, 133, 244, 0.05);
      padding: 12px;
      border-radius: 8px;
      font-family: 'Monaco', 'Courier New', monospace;
      font-size: 13px;
      color: #1967d2;
      word-break: break-all;
      margin-bottom: 20px;
    }
    .olib-picker-buttons {
      display: flex;
      gap: 12px;
      justify-content: flex-end;
    }
    .olib-picker-btn {
      padding: 10px 20px;
      border-radius: 8px;
      border: none;
      font-size: 14px;
      font-weight: 500;
      cursor: pointer;
      transition: all 0.2s;
    }
    .olib-picker-btn-confirm {
      background: #4285f4;
      color: white;
    }
    .olib-picker-btn-confirm:hover {
      background: #1967d2;
    }
    .olib-picker-btn-close {
      background: #34a853;
      color: white;
    }
    .olib-picker-btn-close:hover {
      background: #2d8e47;
    }
    .olib-picker-btn-cancel {
      background: rgba(0, 0, 0, 0.08);
      color: #5f6368;
    }
    .olib-picker-btn-cancel:hover {
      background: rgba(0, 0, 0, 0.12);
    }
  `;
  document.head.appendChild(style);

  // Generate CSS selector for element with validation and scoring
  function generateSelector(element) {
    // Helper: Escape CSS selector value
    function escapeSelectorValue(value) {
      if (!value) return '';
      // Escape special characters for CSS attribute selectors
      // Must escape: " \ and control characters
      return value
        .replace(/\\/g, '\\\\')    // Escape backslashes first
        .replace(/"/g, '\\\\"')     // Escape quotes
        .replace(/\n/g, '\\n')      // Escape newlines
        .replace(/\r/g, '\\r')      // Escape carriage returns
        .replace(/\t/g, '\\t');     // Escape tabs
    }

    // Helper: Test if selector is valid and returns the target element
    function testSelector(selector, targetElement) {
      try {
        const found = document.querySelectorAll(selector);
        // Selector must return exactly one element AND it must be our target
        return found.length === 1 && found[0] === targetElement;
      } catch (e) {
        // Invalid selector
        return false;
      }
    }

    // Helper: Calculate selector score (higher is better)
    function scoreSelector(selector, selectorType) {
      let score = 0;

      // Base scores by type (prefer more specific/reliable selectors)
      const typeScores = {
        'id': 100,
        'data-testid': 95,
        'data-test': 90,
        'name': 75,
        'type': 65,
        'placeholder': 55,
        'aria-label': 70,  // Increased - usually unique
        'title': 45,
        'alt': 40,
        'role': 15,  // Decreased - rarely unique
        'href': 25,  // Added
        'class': 20,
        'path': 10,
        'multi-attr': 85  // Added - multiple attributes are strong
      };

      score += typeScores[selectorType] || 0;

      // Big bonus for multi-attribute selectors (very specific)
      const attrCount = (selector.match(/\[/g) || []).length;
      if (attrCount >= 2) {
        score += 30;
      }

      // Penalize longer selectors (prefer shorter)
      score -= Math.floor(selector.length / 15);

      // Penalize selectors with many combinators
      const combinators = (selector.match(/[>\+~\s]/g) || []).length;
      score -= combinators * 5;

      // Bonus for simple selectors (but less important than multi-attr)
      if (!selector.includes(' ') && !selector.includes('>')) {
        score += 5;
      }

      return score;
    }

    // Collect all candidate selectors with scores
    const candidates = [];

    // Try ID first (highest priority)
    if (element.id && /^[a-zA-Z][\w-]*$/.test(element.id)) {
      const selector = '#' + element.id;
      if (testSelector(selector, element)) {
        candidates.push({
          selector: selector,
          score: scoreSelector(selector, 'id'),
          type: 'id'
        });
      }
    }

    // Try unique attributes in priority order
    const uniqueAttrs = [
      { attr: 'data-testid', type: 'data-testid' },
      { attr: 'data-test', type: 'data-test' },
      { attr: 'name', type: 'name' },
      { attr: 'type', type: 'type' },
      { attr: 'placeholder', type: 'placeholder' },
      { attr: 'aria-label', type: 'aria-label' },
      { attr: 'title', type: 'title' },
      { attr: 'alt', type: 'alt' },
      { attr: 'role', type: 'role' }
    ];

    for (const { attr, type } of uniqueAttrs) {
      const value = element.getAttribute(attr);
      if (value) {
        const escapedValue = escapeSelectorValue(value);
        const selector = `[${attr}="${escapedValue}"]`;

        if (testSelector(selector, element)) {
          candidates.push({
            selector: selector,
            score: scoreSelector(selector, type),
            type: type
          });
        }
      }
    }

    // Try tag + attribute combinations for better specificity
    const tagName = element.tagName.toLowerCase();
    for (const { attr, type } of uniqueAttrs) {
      const value = element.getAttribute(attr);
      if (value) {
        const escapedValue = escapeSelectorValue(value);
        const selector = `${tagName}[${attr}="${escapedValue}"]`;

        if (testSelector(selector, element)) {
          candidates.push({
            selector: selector,
            score: scoreSelector(selector, type) + 5, // Bonus for tag specificity
            type: type + '+tag'
          });
        }
      }
    }

    // Try multi-attribute combinations (strongest selectors)
    // Combine 2 attributes for high specificity
    const attrValues = [];
    for (const { attr, type } of uniqueAttrs) {
      const value = element.getAttribute(attr);
      if (value) {
        attrValues.push({ attr, value, type });
      }
    }

    // Try all pairs of attributes
    for (let i = 0; i < attrValues.length && i < 5; i++) {
      for (let j = i + 1; j < attrValues.length && j < 5; j++) {
        const attr1 = attrValues[i];
        const attr2 = attrValues[j];
        const escaped1 = escapeSelectorValue(attr1.value);
        const escaped2 = escapeSelectorValue(attr2.value);

        // Without tag
        const selector1 = `[${attr1.attr}="${escaped1}"][${attr2.attr}="${escaped2}"]`;
        if (testSelector(selector1, element)) {
          candidates.push({
            selector: selector1,
            score: scoreSelector(selector1, 'multi-attr'),
            type: 'multi-attr'
          });
        }

        // With tag (even more specific)
        const selector2 = `${tagName}[${attr1.attr}="${escaped1}"][${attr2.attr}="${escaped2}"]`;
        if (testSelector(selector2, element)) {
          candidates.push({
            selector: selector2,
            score: scoreSelector(selector2, 'multi-attr') + 10,
            type: 'multi-attr+tag'
          });
        }
      }
    }

    // Try class-based selectors
    if (element.className && typeof element.className === 'string') {
      const classes = element.className.trim().split(/\s+/)
        .filter(c => c && !c.startsWith('olib-') && !c.startsWith('picker-'));

      if (classes.length > 0) {
        // Try single class
        for (const cls of classes) {
          const selector = '.' + cls;
          if (testSelector(selector, element)) {
            candidates.push({
              selector: selector,
              score: scoreSelector(selector, 'class'),
              type: 'class'
            });
          }
        }

        // Try tag + class
        for (const cls of classes) {
          const selector = tagName + '.' + cls;
          if (testSelector(selector, element)) {
            candidates.push({
              selector: selector,
              score: scoreSelector(selector, 'class') + 5,
              type: 'tag+class'
            });
          }
        }

        // Try multiple classes
        if (classes.length >= 2) {
          const selector = '.' + classes.slice(0, 2).join('.');
          if (testSelector(selector, element)) {
            candidates.push({
              selector: selector,
              score: scoreSelector(selector, 'class') + 3,
              type: 'multi-class'
            });
          }
        }
      }
    }

    // If we have good candidates, return the best one
    if (candidates.length > 0) {
      candidates.sort((a, b) => b.score - a.score);
      return candidates[0].selector;
    }

    // Fallback: Build unique path (most reliable but verbose)
    const path = [];
    let current = element;

    while (current && current !== document.body && path.length < 5) {
      let selector = current.tagName.toLowerCase();

      // Try to make this step unique
      // Add ID if available
      if (current.id && /^[a-zA-Z][\w-]*$/.test(current.id)) {
        selector += '#' + current.id;
        path.unshift(selector);
        break; // ID is unique, stop here
      }

      // Add classes if available
      if (current.className && typeof current.className === 'string') {
        const classes = current.className.trim().split(/\s+/)
          .filter(c => c && !c.startsWith('olib-') && !c.startsWith('picker-'));
        if (classes.length > 0) {
          selector += '.' + classes.slice(0, 2).join('.');
        }
      }

      // Add nth-of-type for uniqueness
      if (current.parentElement) {
        const siblings = Array.from(current.parentElement.children)
          .filter(s => s.tagName === current.tagName);
        if (siblings.length > 1) {
          const index = siblings.indexOf(current) + 1;
          selector += `:nth-of-type(${index})`;
        }
      }

      path.unshift(selector);

      // Test if current path is unique
      const testPath = path.join(' > ');
      if (testSelector(testPath, element)) {
        return testPath;
      }

      current = current.parentElement;
    }

    const finalPath = path.join(' > ');

    // Final validation - if still not unique, add more specificity
    if (!testSelector(finalPath, element)) {
      // Add position-based selector as last resort
      const allElements = Array.from(document.querySelectorAll('*'));
      const index = allElements.indexOf(element);
      if (index >= 0) {
        // Use a combination of tag and position for absolute uniqueness
        return `${tagName}:nth-of-type(${index + 1})`;
      }
    }

    return finalPath;
  }

  // Track if picker is active
  let pickerActive = true;

  // Find all interactive elements
  const interactiveSelectors = [
    'a', 'button', 'input', 'select', 'textarea',
    '[onclick]', '[role="button"]', '[role="link"]',
    '[tabindex]', '[contenteditable]'
  ];

  const elements = new Set();
  interactiveSelectors.forEach(selector => {
    document.querySelectorAll(selector).forEach(el => {
      if (!el.hasAttribute('data-owl-ignore')) {
        elements.add(el);
      }
    });
  });

  // Highlight all elements
  elements.forEach(el => {
    el.classList.add('olib-picker-highlight');

    el.addEventListener('click', (e) => {
      // Ignore clicks if picker is no longer active
      if (!pickerActive) return;

      e.preventDefault();
      e.stopPropagation();

      // Generate selector for clicked element
      const selector = generateSelector(el);

      // Show confirmation UI
      showConfirmation(selector, el);
    }, true);
  });

  function showConfirmation(selector, element) {
    // Remove any existing confirmation dialogs
    const existingDialogs = overlay.querySelectorAll('.olib-picker-confirmation');
    existingDialogs.forEach(d => d.remove());

    // Clear any previous element's green highlight by checking all elements
    document.querySelectorAll('*').forEach(el => {
      if (el.style.outline && el.style.outline.includes('34, 168, 83')) {
        el.style.outline = '';
        el.style.outlineOffset = '';
        el.style.background = '';
      }
    });

    // Remove highlights
    document.querySelectorAll('.olib-picker-highlight').forEach(el => {
      el.classList.remove('olib-picker-highlight');
    });

    // Highlight selected element with friendly green color
    element.style.outline = '3px solid #34a853';
    element.style.outlineOffset = '3px';
    element.style.background = 'rgba(52, 168, 83, 0.1)';

    // Create confirmation dialog
    const dialog = document.createElement('div');
    dialog.className = 'olib-picker-confirmation';
    dialog.innerHTML = `
      <div class="olib-picker-title">Element Selected</div>
      <div class="olib-picker-selector">${selector}</div>
      <div class="olib-picker-buttons">
        <button class="olib-picker-btn olib-picker-btn-cancel">Cancel</button>
        <button class="olib-picker-btn olib-picker-btn-close">Pick Another</button>
        <button class="olib-picker-btn olib-picker-btn-confirm">Confirm</button>
      </div>
    `;

    overlay.appendChild(dialog);

    // Cancel button
    dialog.querySelector('.olib-picker-btn-cancel').addEventListener('click', () => {
      pickerActive = false;
      overlay.remove();

      // Clear the green highlight from selected element
      element.style.outline = '';
      element.style.outlineOffset = '';
      element.style.background = '';

      // Send result via IPC (bypasses any console.log blocking by websites)
      if (typeof _2 !== 'undefined') {
        _2('element_picker_result', JSON.stringify({
          action: 'canceled',
          inputId: ')" << escaped_input_id << R"('
        }));
      }
    });

    // Close (pick another) button
    dialog.querySelector('.olib-picker-btn-close').addEventListener('click', () => {
      dialog.remove();
      element.style.outline = '';
      element.style.outlineOffset = '';
      element.style.background = '';

      // Re-highlight all elements
      document.querySelectorAll('a, button, input, select, textarea, [onclick], [role="button"], [role="link"], [tabindex], [contenteditable]').forEach(el => {
        if (!el.hasAttribute('data-owl-ignore')) {
          el.classList.add('olib-picker-highlight');
        }
      });
    });

    // Confirm button
    dialog.querySelector('.olib-picker-btn-confirm').addEventListener('click', () => {
      pickerActive = false;
      overlay.remove();

      // Clear the green highlight from selected element
      element.style.outline = '';
      element.style.outlineOffset = '';
      element.style.background = '';

      // Send result via IPC (bypasses any console.log blocking by websites)
      if (typeof _2 !== 'undefined') {
        _2('element_picker_result', JSON.stringify({
          action: 'confirmed',
          selector: selector,
          inputId: ')" << escaped_input_id << R"('
        }));
      }
    });
  }

  document.body.appendChild(overlay);
})();
)";

  main_frame->ExecuteJavaScript(picker_js.str(), main_frame->GetURL(), 0);

  // Store the playground browser and input ID so OnConsoleMessage can access them
  extern std::map<int, std::pair<CefRefPtr<CefBrowser>, std::string>> g_pending_pickers;
  int browser_id = main_frame->GetBrowser()->GetIdentifier();
  g_pending_pickers[browser_id] = std::make_pair(playground_browser, escaped_input_id);

  LOG_DEBUG("UIBrowser", "Element picker overlay injected");
}

void OwlUIBrowser::InjectPositionPickerOverlay(CefRefPtr<CefFrame> main_frame, CefRefPtr<CefBrowser> playground_browser, const std::string& input_id) {
  if (!main_frame || !playground_browser) return;

  // Escape input_id for JavaScript
  std::string escaped_input_id = input_id;
  size_t pos = 0;
  while ((pos = escaped_input_id.find("'", pos)) != std::string::npos) {
    escaped_input_id.replace(pos, 1, "\\'");
    pos += 2;
  }

  // Build JavaScript for position picker with embedded input ID
  std::ostringstream picker_js;
  picker_js << R"(
(function() {
  // Remove existing picker if present
  const existing = document.getElementById('olib-position-picker-overlay');
  if (existing) existing.remove();

  // Create overlay container
  const overlay = document.createElement('div');
  overlay.id = 'olib-position-picker-overlay';
  overlay.setAttribute('data-owl-ignore', 'true');
  overlay.style.cssText = `
    position: fixed;
    top: 0;
    left: 0;
    width: 100%;
    height: 100%;
    z-index: 2147483646;
    cursor: crosshair;
    background: rgba(0, 0, 0, 0.05);
  `;

  // Add styles
  const style = document.createElement('style');
  style.textContent = `
    #olib-position-picker-overlay {
      pointer-events: auto !important;
    }
    .olib-position-crosshair {
      position: fixed;
      pointer-events: none;
      z-index: 2147483647;
    }
    .olib-position-crosshair-h {
      width: 100%;
      height: 1px;
      background: #4285f4;
      box-shadow: 0 0 3px rgba(0, 0, 0, 0.5);
    }
    .olib-position-crosshair-v {
      width: 1px;
      height: 100%;
      background: #4285f4;
      box-shadow: 0 0 3px rgba(0, 0, 0, 0.5);
    }
    .olib-position-coordinates {
      position: fixed;
      background: rgba(0, 0, 0, 0.8);
      color: white;
      padding: 6px 12px;
      border-radius: 6px;
      font-family: 'Monaco', 'Courier New', monospace;
      font-size: 12px;
      pointer-events: none;
      z-index: 2147483647;
      margin-left: 15px;
      margin-top: 10px;
    }
    .olib-position-confirmation {
      position: fixed;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      background: white;
      border-radius: 16px;
      padding: 24px;
      box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);
      border: 2px solid rgba(66, 133, 244, 0.3);
      z-index: 2147483647;
      min-width: 400px;
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
    }
    .olib-position-title {
      font-size: 18px;
      font-weight: 600;
      color: #202124;
      margin-bottom: 12px;
    }
    .olib-position-display {
      background: rgba(66, 133, 244, 0.05);
      padding: 12px;
      border-radius: 8px;
      font-family: 'Monaco', 'Courier New', monospace;
      font-size: 15px;
      color: #1967d2;
      text-align: center;
      margin-bottom: 20px;
      font-weight: 600;
    }
    .olib-position-buttons {
      display: flex;
      gap: 12px;
      justify-content: flex-end;
    }
    .olib-position-btn {
      padding: 10px 20px;
      border-radius: 8px;
      border: none;
      font-size: 14px;
      font-weight: 500;
      cursor: pointer;
      transition: all 0.2s;
    }
    .olib-position-btn-confirm {
      background: #4285f4;
      color: white;
    }
    .olib-position-btn-confirm:hover {
      background: #1967d2;
      transform: translateY(-1px);
      box-shadow: 0 2px 8px rgba(66, 133, 244, 0.3);
    }
    .olib-position-btn-cancel {
      background: #f1f3f4;
      color: #5f6368;
    }
    .olib-position-btn-cancel:hover {
      background: #e8eaed;
    }
  `;
  overlay.appendChild(style);

  // Create crosshair elements
  const crosshairH = document.createElement('div');
  crosshairH.className = 'olib-position-crosshair olib-position-crosshair-h';
  overlay.appendChild(crosshairH);

  const crosshairV = document.createElement('div');
  crosshairV.className = 'olib-position-crosshair olib-position-crosshair-v';
  overlay.appendChild(crosshairV);

  // Create coordinates display
  const coordsDisplay = document.createElement('div');
  coordsDisplay.className = 'olib-position-coordinates';
  overlay.appendChild(coordsDisplay);

  // Update crosshair and coordinates on mouse move
  overlay.addEventListener('mousemove', (e) => {
    crosshairH.style.top = e.clientY + 'px';
    crosshairV.style.left = e.clientX + 'px';
    coordsDisplay.style.left = e.clientX + 'px';
    coordsDisplay.style.top = e.clientY + 'px';
    coordsDisplay.textContent = `${e.clientX}x${e.clientY}`;
  });

  // Handle click
  overlay.addEventListener('click', (e) => {
    e.preventDefault();
    e.stopPropagation();

    const x = e.clientX;
    const y = e.clientY;
    const position = `${x}x${y}`;

    // Remove crosshair and coords
    crosshairH.remove();
    crosshairV.remove();
    coordsDisplay.remove();

    // Show confirmation dialog
    const confirmation = document.createElement('div');
    confirmation.className = 'olib-position-confirmation';
    confirmation.innerHTML = `
      <div class="olib-position-title">Confirm Position</div>
      <div class="olib-position-display">${position}</div>
      <div class="olib-position-buttons">
        <button class="olib-position-btn olib-position-btn-cancel" data-action="cancel">Cancel</button>
        <button class="olib-position-btn olib-position-btn-confirm" data-action="confirm">Confirm</button>
      </div>
    `;
    overlay.appendChild(confirmation);

    // Handle button clicks
    confirmation.addEventListener('click', (btnE) => {
      const button = btnE.target.closest('[data-action]');
      if (!button) return;

      const action = button.getAttribute('data-action');

      if (action === 'confirm') {
        // Send result via IPC (bypasses any console.log blocking by websites)
        if (typeof _2 !== 'undefined') {
          _2('position_picker_result', JSON.stringify({
            action: 'confirmed',
            position: position
          }));
        }
        overlay.remove();
      } else if (action === 'cancel') {
        // Send cancel result via IPC
        if (typeof _2 !== 'undefined') {
          _2('position_picker_result', JSON.stringify({
            action: 'canceled'
          }));
        }
        overlay.remove();
      }
    });
  });

  // Handle escape key
  const escapeHandler = (e) => {
    if (e.key === 'Escape') {
      if (typeof _2 !== 'undefined') {
        _2('position_picker_result', JSON.stringify({
          action: 'canceled'
        }));
      }
      overlay.remove();
      document.removeEventListener('keydown', escapeHandler);
    }
  };
  document.addEventListener('keydown', escapeHandler);

  document.body.appendChild(overlay);
})();
)";

  main_frame->ExecuteJavaScript(picker_js.str(), main_frame->GetURL(), 0);

  // Store the playground browser and input ID
  extern std::map<int, std::pair<CefRefPtr<CefBrowser>, std::string>> g_pending_pickers;
  int browser_id = main_frame->GetBrowser()->GetIdentifier();
  g_pending_pickers[browser_id] = std::make_pair(playground_browser, escaped_input_id);

  LOG_DEBUG("UIBrowser", "Position picker overlay injected");
}
