#ifndef OWL_LLM_GUARDRAIL_H_
#define OWL_LLM_GUARDRAIL_H_

#include <string>
#include <vector>
#include <set>

// ============================================================
// LLM Guardrail System - Defense Against Prompt Injection
// ============================================================
//
// This system provides multi-layered protection against prompt injection attacks
// that could hijack the LLM, as discovered in AI-assisted browsers.
//
// Attack Vectors Defended:
// 1. Webpage content injection (malicious text in HTML)
// 2. Hidden content attacks (CSS-hidden instructions)
// 3. XML/tag breaking (closing tags + fake system prompts)
// 4. Instruction hijacking (keywords like "IGNORE PREVIOUS")
// 5. Image-based injection (for vision models)
//
// Defense Layers:
// 1. Input Sanitization - Clean untrusted content
// 2. Injection Detection - Detect attack patterns
// 3. Output Validation - Verify LLM responses
// 4. Prompt Structure Protection - Use secure delimiters

namespace OlibGuardrail {

// ============================================================
// Sanitization Results
// ============================================================

struct SanitizationResult {
  std::string sanitized_content;
  bool was_modified;
  std::vector<std::string> threats_detected;
  int risk_score;  // 0-100, higher = more dangerous
};

struct DetectionResult {
  bool is_suspicious;
  std::vector<std::string> threats_detected;
  int risk_score;  // 0-100
  std::string details;
};

struct ValidationResult {
  bool is_valid;
  bool is_suspicious;
  std::vector<std::string> issues;
  std::string sanitized_output;
};

// ============================================================
// 1. Input Sanitizer - Clean Untrusted Content
// ============================================================

class InputSanitizer {
 public:
  // Sanitize webpage content (visible text, HTML, etc.)
  // This is the PRIMARY defense against webpage-based injection
  static SanitizationResult SanitizeWebpageContent(const std::string& content);

  // Sanitize user command (secondary defense)
  static SanitizationResult SanitizeUserInput(const std::string& input);

  // Remove invisible/hidden characters
  static std::string RemoveInvisibleCharacters(const std::string& text);

  // Escape XML/HTML special characters
  static std::string EscapeXMLCharacters(const std::string& text);

  // Truncate to safe length
  static std::string TruncateToSafeLength(const std::string& text, size_t max_length = 10000);

  // Remove repeated characters (obfuscation technique)
  static std::string RemoveExcessiveRepetition(const std::string& text);

  // Normalize whitespace
  static std::string NormalizeWhitespace(const std::string& text);

 private:
  // Detect and remove Unicode tricks
  static std::string RemoveUnicodeTricks(const std::string& text);
};

// ============================================================
// 2. Injection Detector - Detect Attack Patterns
// ============================================================

class InjectionDetector {
 public:
  // Main detection method - check for prompt injection
  static DetectionResult DetectInjection(const std::string& content);

  // Detect instruction keywords in suspicious contexts
  static bool ContainsInstructionKeywords(const std::string& text);

  // Detect tag/delimiter breaking attempts
  static bool ContainsTagBreaking(const std::string& text);

  // Detect role-playing attempts ("you are now...", "act as...")
  static bool ContainsRolePlayingAttempts(const std::string& text);

  // Detect encoded payloads (base64, hex, etc.)
  static bool ContainsEncodedPayload(const std::string& text);

  // Detect excessive special characters (obfuscation)
  static bool HasExcessiveSpecialCharacters(const std::string& text);

 private:
  // Get dangerous instruction keywords
  static const std::set<std::string>& GetInstructionKeywords();

  // Get tag breaking patterns
  static const std::vector<std::string>& GetTagBreakingPatterns();

  // Get role-playing patterns
  static const std::vector<std::string>& GetRolePlayingPatterns();
};

// ============================================================
// 3. Output Validator - Validate LLM Responses
// ============================================================

class OutputValidator {
 public:
  // Validate action plan from NLA
  static ValidationResult ValidateActionPlan(const std::string& json_plan);

  // Detect if LLM was hijacked (unexpected response format)
  static bool IsResponseHijacked(const std::string& response);

  // Validate actions are safe (no dangerous URLs, no suspicious commands)
  static bool AreActionsSafe(const std::vector<std::string>& actions);

  // Check for data exfiltration attempts
  static bool ContainsExfiltrationAttempt(const std::string& response);

 private:
  // Get list of dangerous domains
  static const std::set<std::string>& GetDangerousDomains();

  // Get suspicious action patterns
  static const std::vector<std::string>& GetSuspiciousActions();
};

// ============================================================
// 4. Prompt Structure Protector - Secure Prompt Building
// ============================================================

class PromptProtector {
 public:
  // Build secure prompt with clear boundaries between trusted/untrusted content
  static std::string BuildSecurePrompt(
      const std::string& system_prompt,
      const std::string& untrusted_content,
      const std::string& user_query);

  // Add anti-injection instructions to system prompt
  static std::string EnhanceSystemPrompt(const std::string& system_prompt);

  // Wrap untrusted content with clear delimiters
  static std::string WrapUntrustedContent(
      const std::string& content,
      const std::string& content_type = "webpage");

 private:
  // Generate secure delimiter that's hard to inject
  static std::string GetSecureDelimiter();

  // Get anti-injection instructions
  static std::string GetAntiInjectionInstructions();
};

// ============================================================
// 5. Master Guardrail - Orchestrates All Defenses
// ============================================================

class LLMGuardrail {
 public:
  // Full pipeline: sanitize, detect, protect
  struct GuardrailResult {
    std::string safe_content;
    bool passed_validation;
    std::vector<std::string> threats_blocked;
    int total_risk_score;
    std::string error_message;
  };

  // Process untrusted content through full guardrail pipeline
  static GuardrailResult ProcessUntrustedContent(
      const std::string& content,
      const std::string& content_type = "webpage");

  // Validate LLM output
  static ValidationResult ValidateLLMOutput(
      const std::string& output,
      const std::string& expected_format = "json");

  // Get statistics
  static std::string GetStatistics();

 private:
  static int total_threats_blocked_;
  static int total_requests_processed_;
};

}  // namespace OlibGuardrail

#endif  // OWL_LLM_GUARDRAIL_H_
