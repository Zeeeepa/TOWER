#ifndef OWL_LLM_CLIENT_H_
#define OWL_LLM_CLIENT_H_

#include <string>
#include <vector>
#include <memory>
#include "include/cef_base.h"
#include "owl_pii_scrubber.h"

// High-performance OpenAI-compatible client for llama-server
// Uses /v1/chat/completions endpoint for maximum compatibility
// Supports vision models with multimodal messages (text + images)
//
// PII PROTECTION: When using third-party APIs (not localhost:8095),
// all content is automatically scrubbed for PII/HIPAA data before sending.
// This protects sensitive information from being sent to external services.
class OwlLLMClient {
 public:
  // Content part for multimodal messages (vision support)
  struct ContentPart {
    std::string type;  // "text" or "image_url"
    std::string text;  // For type="text"
    struct ImageURL {
      std::string url;  // Base64 data URL or HTTP(S) URL
    } image_url;  // For type="image_url"
  };

  struct Message {
    std::string role;     // "system", "user", "assistant"
    std::string content;  // Simple text content (for backward compatibility)
    std::vector<ContentPart> content_parts;  // Multimodal content (vision)
    bool is_multimodal = false;  // Set to true when using content_parts
  };

  struct CompletionRequest {
    std::vector<Message> messages;
    int max_tokens = 512;
    float temperature = 0.7f;
    float top_p = 0.9f;
    float top_k = 40.0f;  // Top-K sampling for better quality
    float repeat_penalty = 1.1f;  // Reduce repetition
    bool stream = false;
  };

  struct CompletionResponse {
    std::string content;
    int tokens_generated = 0;
    int tokens_prompt = 0;
    bool success = false;
    std::string error;
    double latency_ms = 0.0;  // Response time in milliseconds
  };

  OwlLLMClient(const std::string& server_url, bool is_third_party = false);
  ~OwlLLMClient();

  // Set API key for external APIs (OpenAI, etc.)
  void SetApiKey(const std::string& api_key) { api_key_ = api_key; }

  // Set model name (e.g., "gpt-4o", "gpt-4o-mini")
  void SetModel(const std::string& model) { model_name_ = model; }

  // Make chat completion request (OpenAI-compatible)
  CompletionResponse ChatComplete(const CompletionRequest& request);

  // Helper: Simple completion with system + user prompt
  // This is the most common use case - handles /no_think automatically
  CompletionResponse Complete(const std::string& prompt,
                              const std::string& system_prompt = "",
                              int max_tokens = 512,
                              float temperature = 0.7f);

  // Helper: Vision completion with image (screenshot analysis)
  // Sends text prompt + base64 image to vision model
  CompletionResponse CompleteWithImage(const std::string& prompt,
                                       const std::string& image_base64,
                                       const std::string& system_prompt = "",
                                       int max_tokens = 512,
                                       float temperature = 0.7f);

  // Get server URL
  std::string GetServerURL() const { return server_url_; }

  // Check if this is using an external (third-party) API
  bool IsExternalAPI() const { return is_external_api_; }

  // Get PII scrubbing statistics
  OwlPII::ScrubStats GetPIIStats() const {
    return pii_scrubber_ ? pii_scrubber_->GetStats() : OwlPII::ScrubStats();
  }

 private:
  std::string server_url_;
  std::string api_key_;      // API key for external services
  std::string model_name_;   // Model name (e.g., "gpt-4o-mini")
  bool is_external_api_;  // True if third-party API (requires PII scrubbing)
  std::unique_ptr<OwlPII::OwlPIIScrubber> pii_scrubber_;  // PII scrubber for third-party APIs

  // Scrub PII from message content (only for external APIs)
  Message ScrubMessage(const Message& message);

  // Clean <think></think> tags from Qwen3 responses
  // CRITICAL: Qwen3 models ALWAYS output thinking tags
  std::string CleanThinkingTags(const std::string& text);

  // Build OpenAI-compatible JSON payload
  std::string BuildOpenAIPayload(const CompletionRequest& request);

  // Parse OpenAI-compatible JSON response
  bool ParseOpenAIResponse(const std::string& json_str, CompletionResponse& response);
};

#endif  // OWL_LLM_CLIENT_H_
