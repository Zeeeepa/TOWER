#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <chrono>

// Semantic information about an element
struct ElementSemantics {
  std::string selector;  // CSS selector
  std::string tag;       // HTML tag (button, input, a, etc)
  std::string type;      // input type if applicable

  // Text content from various sources
  std::string text;           // Element's text content
  std::string placeholder;    // input placeholder
  std::string title;          // title attribute
  std::string aria_label;     // aria-label
  std::string name;           // name attribute
  std::string id;             // id attribute
  std::string value;          // value attribute

  // Context
  std::string nearby_text;    // Text from nearby labels/spans
  std::string label_for;      // LABEL's "for" attribute (references INPUT id)

  // Visual/position info
  int x, y, width, height;
  bool visible;

  // Enhanced visibility info (from improved scanner)
  int z_index;               // CSS z-index for stacking order
  float opacity;             // Cumulative opacity (includes parent opacity cascade)
  std::string display;       // CSS display property
  std::string visibility_css; // CSS visibility property
  std::string transform;     // CSS transform (for detecting off-screen positioning)

  // Role categorization
  std::string inferred_role;  // "search_input", "submit_button", etc

  // Default constructor
  ElementSemantics() : x(0), y(0), width(0), height(0), visible(false),
                       z_index(0), opacity(1.0f) {}
};

// Match result with confidence score
struct ElementMatch {
  ElementSemantics element;
  float confidence;  // 0.0 to 1.0
  std::string match_reason;  // Why this matched
};

// Forward declaration
class OwlLLMClient;

// Intelligent element matcher
class OwlSemanticMatcher {
public:
  static OwlSemanticMatcher* GetInstance();

  // Register element semantics from renderer
  void RegisterElement(const std::string& context_id, const ElementSemantics& elem);

  // Clear all elements for a context
  void ClearContext(const std::string& context_id);

  // Find element by natural language description
  // e.g. "search button", "email input", "login link"
  // Now enhanced with LLM fallback for low-confidence matches
  std::vector<ElementMatch> FindByDescription(
    const std::string& context_id,
    const std::string& description,
    int max_results = 5
  );

  // Find element by role and text
  // e.g. role="button", text="Search"
  std::vector<ElementMatch> FindByRole(
    const std::string& context_id,
    const std::string& role,
    const std::string& text_hint = ""
  );

  // Get all elements in context with semantic info
  std::vector<ElementSemantics> GetAllElements(const std::string& context_id);

  // NEW: Enable/disable enhanced scoring with CompositeScorer
  // When enabled, uses the new multi-scorer ensemble for 90%+ accuracy
  void SetUseEnhancedScoring(bool enabled);
  bool GetUseEnhancedScoring() const;

  // NEW: Set viewport dimensions for visual scoring
  void SetViewportDimensions(int width, int height);

  // Search result caching control
  void SetCacheEnabled(bool enabled);
  void SetCacheTTL(int milliseconds);
  void ClearCache();
  void InvalidateCacheForContext(const std::string& context_id);

private:
  OwlSemanticMatcher();
  static OwlSemanticMatcher* instance_;

  std::unordered_map<std::string, std::vector<ElementSemantics>> elements_by_context_;
  std::mutex mutex_;

  // Enhanced scoring flag (default: enabled)
  bool use_enhanced_scoring_ = true;
  int viewport_width_ = 1920;
  int viewport_height_ = 1080;

  // Search result caching to avoid redundant lookups
  struct CachedSearch {
    std::vector<ElementMatch> results;
    std::chrono::steady_clock::time_point timestamp;
    size_t element_count;  // For cache invalidation when elements change
  };
  std::unordered_map<std::string, CachedSearch> search_cache_;  // key: context_id + "|" + description
  bool cache_enabled_ = true;
  int cache_ttl_ms_ = 500;  // Cache TTL in milliseconds (default 500ms)

  // Cache helper
  std::string MakeCacheKey(const std::string& context_id, const std::string& description) const;

  // Scoring functions
  float ScoreTextMatch(const std::string& elem_text, const std::string& query);
  float ScoreRoleMatch(const std::string& elem_role, const std::string& query);
  float ScoreContextMatch(const ElementSemantics& elem, const std::string& query);

  // Text analysis
  std::vector<std::string> ExtractKeywords(const std::string& text);
  std::string NormalizeText(const std::string& text);
  bool FuzzyMatch(const std::string& text, const std::string& pattern);

  // Synonym expansion for better matching
  std::vector<std::string> ExpandWithSynonyms(const std::string& text);
  bool MatchWithSynonyms(const std::string& text, const std::string& pattern);

  // Role inference
  std::string InferRole(const ElementSemantics& elem);

  // LLM-enhanced matching
  // Used when code-based matching has low confidence or ambiguity
  bool ShouldUseLLMDisambiguation(const std::vector<ElementMatch>& matches);
  std::vector<ElementMatch> DisambiguateWithLLM(
    const std::vector<ElementMatch>& candidates,
    const std::string& description,
    OwlLLMClient* llm
  );
  std::string ElementToLLMContext(const ElementSemantics& elem, int index);

  // NEW: Enhanced scoring using CompositeScorer (multi-scorer ensemble)
  std::vector<ElementMatch> FindByDescriptionEnhanced(
    const std::string& context_id,
    const std::string& description,
    int max_results = 5
  );
};
