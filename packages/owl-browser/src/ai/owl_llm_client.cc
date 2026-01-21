#include "owl_llm_client.h"
#include "logger.h"
#include "include/cef_parser.h"
#include <curl/curl.h>
#include <regex>
#include <chrono>

// Callback for curl to write response data
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  ((std::string*)userp)->append((char*)contents, size * nmemb);
  return size * nmemb;
}

OwlLLMClient::OwlLLMClient(const std::string& server_url, bool is_third_party)
    : server_url_(server_url),
      is_external_api_(is_third_party) {

  // Auto-detect external API if URL is not localhost
  // External APIs have different parameter requirements (no top_p with temperature, no top_k, etc.)
  if (!is_external_api_) {
    // Check if URL is NOT localhost - if so, it's an external API
    if (server_url.find("localhost") == std::string::npos &&
        server_url.find("127.0.0.1") == std::string::npos &&
        server_url.find("0.0.0.0") == std::string::npos) {
      is_external_api_ = true;
      LOG_DEBUG("LLMClient", "Auto-detected external API from URL: " + server_url);
    }
  }

  // Initialize PII scrubber ONLY for third-party APIs
  if (is_external_api_) {
    pii_scrubber_ = std::make_unique<OwlPII::OwlPIIScrubber>();
    LOG_DEBUG("LLMClient", "🛡️  PII SCRUBBER ENABLED for third-party API: " + server_url);
    LOG_DEBUG("LLMClient", "All content will be scrubbed for PII/HIPAA data before sending");
  } else {
    LOG_DEBUG("LLMClient", "Local/Private LLM detected - PII scrubbing disabled (data stays on trusted network)");
  }

  LOG_DEBUG("LLMClient", "Initialized for server: " + server_url);
}

OwlLLMClient::~OwlLLMClient() {
  if (pii_scrubber_ && is_external_api_) {
    auto stats = pii_scrubber_->GetStats();
    if (stats.total_items_found > 0) {
      LOG_DEBUG("LLMClient", "PII Scrubbing Session Summary: " + stats.ToString());
    }
  }
}

OwlLLMClient::Message OwlLLMClient::ScrubMessage(const Message& message) {
  if (!is_external_api_ || !pii_scrubber_) {
    // No scrubbing for built-in LLM
    return message;
  }

  Message scrubbed = message;

  // Scrub simple text content
  if (!message.content.empty()) {
    std::string original = message.content;
    scrubbed.content = pii_scrubber_->ScrubText(message.content);

    // Log if PII was scrubbed
    if (original != scrubbed.content) {
      LOG_DEBUG("LLMClient", "🛡️  PII DETECTED AND SCRUBBED - Original length: " +
               std::to_string(original.length()) + ", Scrubbed length: " +
               std::to_string(scrubbed.content.length()));
      LOG_DEBUG("LLMClient", "Original: " + original.substr(0, 100));
      LOG_DEBUG("LLMClient", "Scrubbed: " + scrubbed.content.substr(0, 100));
    }
  }

  // Scrub multimodal content parts
  if (message.is_multimodal && !message.content_parts.empty()) {
    scrubbed.content_parts.clear();
    for (const auto& part : message.content_parts) {
      ContentPart scrubbed_part = part;

      // Scrub text parts
      if (part.type == "text" && !part.text.empty()) {
        std::string original_text = part.text;
        scrubbed_part.text = pii_scrubber_->ScrubText(part.text);

        // Log if PII was scrubbed in multimodal content
        if (original_text != scrubbed_part.text) {
          LOG_DEBUG("LLMClient", "🛡️  PII DETECTED AND SCRUBBED in multimodal content - Original length: " +
                   std::to_string(original_text.length()) + ", Scrubbed length: " +
                   std::to_string(scrubbed_part.text.length()));
        }
      }

      // Note: Image URLs are NOT scrubbed (they're base64 data, not PII)
      // If we wanted to scrub image metadata, we could do it here

      scrubbed.content_parts.push_back(scrubbed_part);
    }
  }

  return scrubbed;
}

std::string OwlLLMClient::CleanThinkingTags(const std::string& text) {
  // Qwen3 models ALWAYS output <think></think> tags, even with /no_think
  // We MUST remove them from all responses for clean output

  if (text.empty()) {
    return text;
  }

  std::string result = text;

  // Remove <think>...</think> blocks (including newlines and content)
  // Use non-greedy matching to handle multiple thinking blocks
  std::regex think_regex(R"(<think>[\s\S]*?</think>)");
  result = std::regex_replace(result, think_regex, "");

  // Remove empty <think></think> tags (edge case)
  std::regex empty_think_regex(R"(<think>\s*</think>)");
  result = std::regex_replace(result, empty_think_regex, "");

  // Trim leading/trailing whitespace
  size_t start = result.find_first_not_of(" \t\n\r");
  size_t end = result.find_last_not_of(" \t\n\r");

  if (start == std::string::npos) {
    return "";
  }

  return result.substr(start, end - start + 1);
}

std::string OwlLLMClient::BuildOpenAIPayload(const CompletionRequest& request) {
  // Build OpenAI-compatible JSON payload using CEF's JSON utilities
  // Supports both simple text and multimodal (vision) messages
  CefRefPtr<CefDictionaryValue> payload = CefDictionaryValue::Create();

  // Add messages array
  CefRefPtr<CefListValue> messages_list = CefListValue::Create();
  for (size_t i = 0; i < request.messages.size(); i++) {
    // 🛡️ SECURITY: Scrub PII from message before sending to external APIs
    Message scrubbed_message = ScrubMessage(request.messages[i]);

    CefRefPtr<CefDictionaryValue> msg = CefDictionaryValue::Create();
    msg->SetString("role", scrubbed_message.role);

    // Check if this is a multimodal message (vision)
    if (scrubbed_message.is_multimodal && !scrubbed_message.content_parts.empty()) {
      // Build multimodal content array
      CefRefPtr<CefListValue> content_list = CefListValue::Create();

      for (size_t j = 0; j < scrubbed_message.content_parts.size(); j++) {
        const auto& part = scrubbed_message.content_parts[j];
        CefRefPtr<CefDictionaryValue> part_dict = CefDictionaryValue::Create();
        part_dict->SetString("type", part.type);

        if (part.type == "text") {
          part_dict->SetString("text", part.text);
        } else if (part.type == "image_url") {
          CefRefPtr<CefDictionaryValue> image_url_dict = CefDictionaryValue::Create();
          image_url_dict->SetString("url", part.image_url.url);

          CefRefPtr<CefValue> image_url_value = CefValue::Create();
          image_url_value->SetDictionary(image_url_dict);
          part_dict->SetValue("image_url", image_url_value);
        }

        CefRefPtr<CefValue> part_value = CefValue::Create();
        part_value->SetDictionary(part_dict);
        content_list->SetValue(j, part_value);
      }

      CefRefPtr<CefValue> content_value = CefValue::Create();
      content_value->SetList(content_list);
      msg->SetValue("content", content_value);
    } else {
      // Simple text message (already scrubbed)
      msg->SetString("content", scrubbed_message.content);
    }

    CefRefPtr<CefValue> msg_value = CefValue::Create();
    msg_value->SetDictionary(msg);
    messages_list->SetValue(i, msg_value);
  }

  CefRefPtr<CefValue> messages_value = CefValue::Create();
  messages_value->SetList(messages_list);
  payload->SetValue("messages", messages_value);

  // Add model name if set (required for external APIs like OpenAI)
  if (!model_name_.empty()) {
    payload->SetString("model", model_name_);
    LOG_DEBUG("LLMClient", "Using model: " + model_name_);
  }

  // Add generation parameters for high-quality output
  payload->SetInt("max_tokens", request.max_tokens);
  payload->SetDouble("temperature", request.temperature);

  // Only add these parameters for local models (not OpenAI)
  // OpenAI doesn't support top_k and repeat_penalty
  if (!is_external_api_) {
    payload->SetDouble("top_p", request.top_p);
    payload->SetDouble("top_k", request.top_k);
    payload->SetDouble("repeat_penalty", request.repeat_penalty);
  }
  payload->SetBool("stream", request.stream);

  // Convert to JSON string
  CefRefPtr<CefValue> root = CefValue::Create();
  root->SetDictionary(payload);
  CefString json_str = CefWriteJSON(root, JSON_WRITER_DEFAULT);

  return json_str.ToString();
}

bool OwlLLMClient::ParseOpenAIResponse(const std::string& json_str, CompletionResponse& response) {
  // Parse OpenAI-compatible JSON response
  // Response format: {"choices": [{"message": {"content": "..."}}], "usage": {...}}

  CefRefPtr<CefValue> json_value = CefParseJSON(
      json_str, JSON_PARSER_ALLOW_TRAILING_COMMAS);

  if (!json_value || json_value->GetType() != VTYPE_DICTIONARY) {
    response.error = "Failed to parse JSON response";
    LOG_ERROR("LLMClient", "Invalid JSON response");
    return false;
  }

  CefRefPtr<CefDictionaryValue> dict = json_value->GetDictionary();

  // Parse choices array
  if (dict->HasKey("choices")) {
    CefRefPtr<CefListValue> choices = dict->GetList("choices");
    if (choices && choices->GetSize() > 0) {
      CefRefPtr<CefDictionaryValue> choice = choices->GetDictionary(0);
      if (choice->HasKey("message")) {
        CefRefPtr<CefDictionaryValue> message = choice->GetDictionary("message");
        if (message->HasKey("content")) {
          std::string content = message->GetString("content").ToString();

          // CRITICAL: Clean thinking tags for Qwen3-1.7B
          response.content = CleanThinkingTags(content);
          response.success = true;
        }
      }
    }
  }

  // Parse token usage
  if (dict->HasKey("usage")) {
    CefRefPtr<CefDictionaryValue> usage = dict->GetDictionary("usage");
    if (usage->HasKey("completion_tokens")) {
      response.tokens_generated = usage->GetInt("completion_tokens");
    }
    if (usage->HasKey("prompt_tokens")) {
      response.tokens_prompt = usage->GetInt("prompt_tokens");
    }
  }

  if (!response.success) {
    response.error = "No content in response";
    return false;
  }

  return true;
}

OwlLLMClient::CompletionResponse OwlLLMClient::ChatComplete(
    const CompletionRequest& request) {
  CompletionResponse response;
  auto start_time = std::chrono::high_resolution_clock::now();

  CURL* curl = curl_easy_init();
  if (!curl) {
    response.error = "Failed to initialize CURL";
    LOG_ERROR("LLMClient", response.error);
    return response;
  }

  // Build OpenAI-compatible payload
  std::string payload_json = BuildOpenAIPayload(request);

  std::string response_str;

  // Build full API URL - only append /v1/chat/completions if not already present
  std::string url = server_url_;
  if (url.find("/v1/chat/completions") == std::string::npos) {
    // Remove trailing slash if present
    if (!url.empty() && url.back() == '/') {
      url.pop_back();
    }
    url += "/v1/chat/completions";
  }

  LOG_DEBUG("LLMClient", "Request URL: " + url);
  LOG_DEBUG("LLMClient", "Model: " + model_name_);

  // Set up HTTP headers
  struct curl_slist* headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  // Add Authorization header if API key is set
  if (!api_key_.empty()) {
    std::string auth_header = "Authorization: Bearer " + api_key_;
    headers = curl_slist_append(headers, auth_header.c_str());
    LOG_DEBUG("LLMClient", "Using API key authentication");
  }

  // Configure curl for high performance
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload_json.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);  // 60 second timeout
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);  // 5 second connect timeout
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);  // Thread-safe
  curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);  // Disable Nagle's algorithm for lower latency

  // Perform HTTP request
  CURLcode res = curl_easy_perform(curl);

  // Calculate latency
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  response.latency_ms = duration.count();

  // Check for HTTP errors
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    response.error = "HTTP request failed: " + std::string(curl_easy_strerror(res));
    LOG_ERROR("LLMClient", response.error);
    return response;
  }

  if (http_code != 200) {
    response.error = "HTTP error " + std::to_string(http_code);
    LOG_ERROR("LLMClient", response.error);
    LOG_ERROR("LLMClient", "Response: " + response_str.substr(0, 200));
    return response;
  }

  // Parse JSON response
  if (!ParseOpenAIResponse(response_str, response)) {
    LOG_ERROR("LLMClient", "Failed to parse response");
    return response;
  }

  LOG_DEBUG("LLMClient", "Completion successful - " +
            std::to_string(response.tokens_generated) + " tokens in " +
            std::to_string(response.latency_ms) + "ms");

  return response;
}

// Helper method for simple completions
OwlLLMClient::CompletionResponse OwlLLMClient::Complete(
    const std::string& prompt,
    const std::string& system_prompt,
    int max_tokens,
    float temperature) {

  CompletionRequest request;

  // Add system message with /no_think instruction for Qwen3
  // This REDUCES thinking output but doesn't eliminate it
  if (!system_prompt.empty()) {
    Message sys_msg;
    sys_msg.role = "system";
    sys_msg.content = system_prompt + "\n/no_think";  // Minimize thinking
    sys_msg.is_multimodal = false;
    request.messages.push_back(sys_msg);
  } else {
    // Default system prompt with /no_think
    Message sys_msg;
    sys_msg.role = "system";
    sys_msg.content = "You are a helpful AI assistant.\n/no_think";
    sys_msg.is_multimodal = false;
    request.messages.push_back(sys_msg);
  }

  // Add user message
  Message user_msg;
  user_msg.role = "user";
  user_msg.content = prompt;
  user_msg.is_multimodal = false;
  request.messages.push_back(user_msg);

  request.max_tokens = max_tokens;
  request.temperature = temperature;

  return ChatComplete(request);
}

// Helper method for vision completions (image analysis)
OwlLLMClient::CompletionResponse OwlLLMClient::CompleteWithImage(
    const std::string& prompt,
    const std::string& image_base64,
    const std::string& system_prompt,
    int max_tokens,
    float temperature) {

  CompletionRequest request;

  // Add system message
  if (!system_prompt.empty()) {
    Message sys_msg;
    sys_msg.role = "system";
    sys_msg.content = system_prompt + "\n/no_think";
    sys_msg.is_multimodal = false;
    request.messages.push_back(sys_msg);
  } else {
    Message sys_msg;
    sys_msg.role = "system";
    sys_msg.content = "You are a helpful AI vision assistant. Analyze images and answer questions about them.\n/no_think";
    sys_msg.is_multimodal = false;
    request.messages.push_back(sys_msg);
  }

  // Add multimodal user message (text + image)
  Message user_msg;
  user_msg.role = "user";
  user_msg.is_multimodal = true;

  // Add text part
  ContentPart text_part;
  text_part.type = "text";
  text_part.text = prompt;
  user_msg.content_parts.push_back(text_part);

  // Add image part
  ContentPart image_part;
  image_part.type = "image_url";
  // Create data URL from base64
  image_part.image_url.url = "data:image/png;base64," + image_base64;
  user_msg.content_parts.push_back(image_part);

  request.messages.push_back(user_msg);

  request.max_tokens = max_tokens;
  request.temperature = temperature;

  LOG_DEBUG("LLMClient", "Sending vision request with image (" +
           std::to_string(image_base64.length()) + " bytes base64)");

  return ChatComplete(request);
}
