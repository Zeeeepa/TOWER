#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

// Template-based extraction system
// Optimized selectors for popular websites
// Built BY AI FOR AI

// Transform types for field values
enum class Transform {
  NONE,
  TRIM,
  LOWERCASE,
  UPPERCASE,
  PARSE_INT,
  PARSE_FLOAT,
  PARSE_PRICE,
  PARSE_DATE,
  ABSOLUTE_URL,
  CLEAN_WHITESPACE,
  STRIP_HTML,
  DECODE_ENTITIES
};

// What to extract from an element
enum class ExtractType {
  TEXT,           // innerText
  HTML,           // innerHTML
  ATTRIBUTE,      // specific attribute
  VALUE           // input value
};

// Field extraction rule
struct FieldRule {
  std::string selector;                      // CSS selector
  ExtractType extract = ExtractType::TEXT;   // What to extract
  std::string attribute = "";                // For ATTRIBUTE type
  Transform transform = Transform::NONE;     // Post-processing
  bool multiple = false;                     // Extract array
  std::map<std::string, FieldRule> fields;   // Nested fields
};

// Detection methods for template matching
struct DetectionRules {
  std::vector<std::string> url_patterns;     // Regex patterns
  std::map<std::string, std::string> meta_tags;  // meta[property=X] content=Y
  std::string dom_selector;                  // Must exist
  double confidence_threshold = 0.8;
};

// Extraction template definition
struct ExtractionTemplate {
  std::string name;                          // "google_search"
  std::string version;                       // "1.0.0"
  std::string description;
  DetectionRules detection;
  std::map<std::string, FieldRule> extraction;  // Field name → rule

  // Load from JSON
  static std::unique_ptr<ExtractionTemplate> FromJSON(const std::string& json);

  // Save to JSON
  std::string ToJSON() const;
};

class OlibTemplateManager {
public:
  static OlibTemplateManager* GetInstance();

  // Load templates
  void LoadBuiltinTemplates();
  void LoadTemplate(const std::string& json);
  void LoadTemplateFromFile(const std::string& path);

  // Get template
  ExtractionTemplate* GetTemplate(const std::string& name);
  ExtractionTemplate* DetectTemplate(const std::string& url,
                                     const std::string& html,
                                     const std::map<std::string, std::string>& meta_tags);

  // List templates
  std::vector<std::string> ListTemplates() const;

private:
  OlibTemplateManager();
  ~OlibTemplateManager();

  static OlibTemplateManager* instance_;
  std::map<std::string, std::unique_ptr<ExtractionTemplate>> templates_;

  // Built-in templates
  void RegisterGoogleSearchTemplate();
  void RegisterWikipediaTemplate();
  void RegisterAmazonProductTemplate();
  void RegisterGitHubRepoTemplate();
  void RegisterTwitterFeedTemplate();
  void RegisterRedditThreadTemplate();
};
