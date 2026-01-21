#include "owl_semantic_matcher.h"
#include "owl_llm_client.h"
#include "owl_browser_manager.h"
#include "owl_composite_scorer.h"
#include "logger.h"
#include "include/cef_parser.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <cmath>
#include <map>
#include <regex>

OwlSemanticMatcher* OwlSemanticMatcher::instance_ = nullptr;

OwlSemanticMatcher::OwlSemanticMatcher()
    : use_enhanced_scoring_(true),
      viewport_width_(1920),
      viewport_height_(1080) {
  LOG_DEBUG("SemanticMatcher", "Initialized with enhanced scoring enabled");
}

OwlSemanticMatcher* OwlSemanticMatcher::GetInstance() {
  if (!instance_) {
    instance_ = new OwlSemanticMatcher();
  }
  return instance_;
}

void OwlSemanticMatcher::SetUseEnhancedScoring(bool enabled) {
  use_enhanced_scoring_ = enabled;
  LOG_DEBUG("SemanticMatcher", "Enhanced scoring " + std::string(enabled ? "enabled" : "disabled"));
}

bool OwlSemanticMatcher::GetUseEnhancedScoring() const {
  return use_enhanced_scoring_;
}

void OwlSemanticMatcher::SetViewportDimensions(int width, int height) {
  viewport_width_ = width;
  viewport_height_ = height;
  CompositeScorer::GetInstance()->SetViewportDimensions(width, height);
}

// ============================================================
// Search Result Caching
// ============================================================

void OwlSemanticMatcher::SetCacheEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  cache_enabled_ = enabled;
  if (!enabled) {
    search_cache_.clear();
  }
  LOG_DEBUG("SemanticMatcher", "Search cache " + std::string(enabled ? "enabled" : "disabled"));
}

void OwlSemanticMatcher::SetCacheTTL(int milliseconds) {
  cache_ttl_ms_ = milliseconds;
}

void OwlSemanticMatcher::ClearCache() {
  std::lock_guard<std::mutex> lock(mutex_);
  search_cache_.clear();
  LOG_DEBUG("SemanticMatcher", "Search cache cleared");
}

void OwlSemanticMatcher::InvalidateCacheForContext(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  // Remove all cache entries for this context
  auto it = search_cache_.begin();
  while (it != search_cache_.end()) {
    if (it->first.find(context_id + "|") == 0) {
      it = search_cache_.erase(it);
    } else {
      ++it;
    }
  }
  LOG_DEBUG("SemanticMatcher", "Cache invalidated for context: " + context_id);
}

std::string OwlSemanticMatcher::MakeCacheKey(const std::string& context_id, const std::string& description) const {
  return context_id + "|" + description;
}

void OwlSemanticMatcher::RegisterElement(const std::string& context_id, const ElementSemantics& elem) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Create a copy with inferred role
  ElementSemantics enriched = elem;
  enriched.inferred_role = InferRole(elem);

  elements_by_context_[context_id].push_back(enriched);

  LOG_DEBUG("SemanticMatcher", "Registered: " + elem.tag + " role=" + enriched.inferred_role +
            " text='" + elem.text.substr(0, 30) + "'");
}

void OwlSemanticMatcher::ClearContext(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  elements_by_context_.erase(context_id);

  // Also clear search cache for this context
  auto it = search_cache_.begin();
  while (it != search_cache_.end()) {
    if (it->first.find(context_id + "|") == 0) {
      it = search_cache_.erase(it);
    } else {
      ++it;
    }
  }

  LOG_DEBUG("SemanticMatcher", "Cleared context: " + context_id);
}

std::vector<ElementSemantics> OwlSemanticMatcher::GetAllElements(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (elements_by_context_.count(context_id)) {
    return elements_by_context_[context_id];
  }
  return {};
}

std::string OwlSemanticMatcher::NormalizeText(const std::string& text) {
  std::string normalized;
  normalized.reserve(text.length());

  for (char c : text) {
    normalized += std::tolower(c);
  }

  // Remove extra whitespace
  std::istringstream iss(normalized);
  std::string word, result;
  while (iss >> word) {
    if (!result.empty()) result += " ";
    result += word;
  }

  return result;
}

std::vector<std::string> OwlSemanticMatcher::ExtractKeywords(const std::string& text) {
  std::vector<std::string> keywords;
  std::string normalized = NormalizeText(text);
  std::istringstream iss(normalized);
  std::string word;

  // Skip common stop words
  std::vector<std::string> stop_words = {"the", "a", "an", "and", "or", "but", "in", "on", "at", "to", "for"};

  while (iss >> word) {
    if (word.length() > 2 &&
        std::find(stop_words.begin(), stop_words.end(), word) == stop_words.end()) {
      keywords.push_back(word);
    }
  }

  return keywords;
}

bool OwlSemanticMatcher::FuzzyMatch(const std::string& text, const std::string& pattern) {
  std::string norm_text = NormalizeText(text);
  std::string norm_pattern = NormalizeText(pattern);

  // Exact match
  if (norm_text.find(norm_pattern) != std::string::npos) {
    return true;
  }

  // Word-level match (any word in pattern matches any word in text)
  auto text_words = ExtractKeywords(text);
  auto pattern_words = ExtractKeywords(pattern);

  for (const auto& pw : pattern_words) {
    for (const auto& tw : text_words) {
      if (tw.find(pw) != std::string::npos || pw.find(tw) != std::string::npos) {
        return true;
      }
    }
  }

  // Try synonym matching
  return MatchWithSynonyms(text, pattern);
}

std::vector<std::string> OwlSemanticMatcher::ExpandWithSynonyms(const std::string& text) {
  std::vector<std::string> result;
  result.push_back(text);

  std::string norm = NormalizeText(text);

  // Common UI interaction synonyms
  static const std::map<std::string, std::vector<std::string>> synonym_map = {
    // Actions
    {"click", {"tap", "press", "select", "activate"}},
    {"tap", {"click", "press", "touch"}},
    {"press", {"click", "tap", "push"}},
    {"submit", {"send", "go", "apply", "confirm"}},
    {"search", {"find", "query", "look"}},
    {"login", {"signin", "sign-in", "log-in", "authenticate"}},
    {"logout", {"signout", "sign-out", "log-out"}},
    {"register", {"signup", "sign-up", "join"}},

    // Elements
    {"button", {"btn", "control"}},
    {"input", {"field", "textbox", "text-box"}},
    {"box", {"field", "input", "textbox"}},
    {"field", {"input", "box", "textbox"}},
    {"link", {"anchor", "hyperlink", "url"}},
    {"dropdown", {"select", "picker", "menu"}},
    {"select", {"dropdown", "picker", "choose"}},
    {"checkbox", {"check", "tick", "toggle", "checkmark"}},
    {"check", {"checkbox", "tick", "mark"}},

    // Common words
    {"email", {"mail", "e-mail"}},
    {"password", {"pass", "pwd", "passcode"}},
    {"username", {"user", "login", "account"}},
    {"phone", {"telephone", "mobile", "tel"}},
    {"address", {"location", "addr"}},
    {"close", {"dismiss", "cancel", "exit"}},
    {"next", {"continue", "forward", "proceed"}},
    {"previous", {"back", "prev"}},
    {"home", {"main", "dashboard", "start"}},
  };

  // Check each word in the normalized text
  auto words = ExtractKeywords(norm);
  for (const auto& word : words) {
    auto it = synonym_map.find(word);
    if (it != synonym_map.end()) {
      result.insert(result.end(), it->second.begin(), it->second.end());
    }
  }

  return result;
}

bool OwlSemanticMatcher::MatchWithSynonyms(const std::string& text, const std::string& pattern) {
  // Expand both text and pattern with synonyms
  auto text_variations = ExpandWithSynonyms(text);
  auto pattern_variations = ExpandWithSynonyms(pattern);

  // Check if any variation of pattern matches any variation of text
  for (const auto& pv : pattern_variations) {
    std::string norm_pv = NormalizeText(pv);
    for (const auto& tv : text_variations) {
      std::string norm_tv = NormalizeText(tv);
      if (norm_tv.find(norm_pv) != std::string::npos || norm_pv.find(norm_tv) != std::string::npos) {
        return true;
      }
    }
  }

  return false;
}

std::string OwlSemanticMatcher::InferRole(const ElementSemantics& elem) {
  // PRIORITY 1: Type-based detection (most reliable)
  // Check explicit type attributes first before fuzzy text matching
  if (elem.type == "checkbox") {
    return "checkbox_input";
  }

  if (elem.type == "radio") {
    return "radio_input";
  }

  if (elem.type == "email") {
    return "email_input";
  }

  if (elem.type == "password") {
    return "password_input";
  }

  if (elem.type == "submit") {
    return "submit_button";
  }

  // Normalize tag for comparison
  std::string tag_lower = elem.tag;
  std::transform(tag_lower.begin(), tag_lower.end(), tag_lower.begin(), ::tolower);

  // Check if this is an interactive element that can have a meaningful role
  bool is_interactive = (tag_lower == "input" || tag_lower == "button" ||
                         tag_lower == "select" || tag_lower == "textarea" ||
                         tag_lower == "a" || tag_lower == "label");

  // PRIORITY 2: Fuzzy text matching (only for interactive elements)
  std::string combined = NormalizeText(
    elem.text + " " + elem.placeholder + " " + elem.aria_label + " " +
    elem.title + " " + elem.nearby_text
  );

  // Search-related
  if (FuzzyMatch(combined, "search")) {
    if (tag_lower == "input" || tag_lower == "textarea") return "search_input";
    if (tag_lower == "button") return "search_button";
  }

  // Login/Auth
  if (FuzzyMatch(combined, "login") || FuzzyMatch(combined, "sign in")) {
    if (tag_lower == "button") return "login_button";
    if (tag_lower == "a") return "login_link";
  }

  // Email (text-based fallback) - ONLY for interactive elements
  if (is_interactive && FuzzyMatch(combined, "email")) {
    if (tag_lower == "input" || tag_lower == "textarea") return "email_input";
    if (tag_lower == "label") return "email_label";
  }

  // Password (text-based fallback) - ONLY for interactive elements
  if (is_interactive && FuzzyMatch(combined, "password")) {
    if (tag_lower == "input") return "password_input";
    if (tag_lower == "label") return "password_label";
  }

  // Submit (text-based fallback) - ONLY for buttons
  if ((tag_lower == "button" || tag_lower == "input") &&
      (FuzzyMatch(combined, "submit") || FuzzyMatch(combined, "send") || FuzzyMatch(combined, "create"))) {
    return "submit_button";
  }

  // Navigation
  if (tag_lower == "a") {
    if (FuzzyMatch(combined, "home")) return "home_link";
    if (FuzzyMatch(combined, "about")) return "about_link";
    if (FuzzyMatch(combined, "contact")) return "contact_link";
    return "navigation_link";
  }

  // Generic by tag (only for interactive elements)
  if (tag_lower == "button") return "button";
  if (tag_lower == "input") return "input";
  if (tag_lower == "textarea") return "textarea";
  if (tag_lower == "select") return "select";
  if (tag_lower == "label") return "label";

  return "unknown";
}

float OwlSemanticMatcher::ScoreTextMatch(const std::string& elem_text, const std::string& query) {
  if (elem_text.empty()) return 0.0f;

  std::string norm_elem = NormalizeText(elem_text);
  std::string norm_query = NormalizeText(query);

  // Exact match = 1.0
  if (norm_elem == norm_query) return 1.0f;

  // Contains full query = 0.9
  if (norm_elem.find(norm_query) != std::string::npos) return 0.9f;

  // Keyword matching
  auto elem_keywords = ExtractKeywords(elem_text);
  auto query_keywords = ExtractKeywords(query);

  if (query_keywords.empty()) return 0.0f;

  int matches = 0;
  for (const auto& qw : query_keywords) {
    for (const auto& ew : elem_keywords) {
      if (ew.find(qw) != std::string::npos || qw.find(ew) != std::string::npos) {
        matches++;
        break;
      }
    }
  }

  return 0.7f * (static_cast<float>(matches) / query_keywords.size());
}

float OwlSemanticMatcher::ScoreRoleMatch(const std::string& elem_role, const std::string& query) {
  std::string norm_query = NormalizeText(query);

  // Direct role mentions
  if (FuzzyMatch(norm_query, "button") && elem_role.find("button") != std::string::npos) return 0.8f;
  if (FuzzyMatch(norm_query, "input") && elem_role.find("input") != std::string::npos) return 0.8f;
  if (FuzzyMatch(norm_query, "link") && elem_role.find("link") != std::string::npos) return 0.8f;
  if (FuzzyMatch(norm_query, "search") && elem_role.find("search") != std::string::npos) return 0.9f;
  if (FuzzyMatch(norm_query, "login") && elem_role.find("login") != std::string::npos) return 0.9f;
  if (MatchWithSynonyms(norm_query, "checkbox") && elem_role.find("checkbox") != std::string::npos) return 0.9f;

  return 0.0f;
}

float OwlSemanticMatcher::ScoreContextMatch(const ElementSemantics& elem, const std::string& query) {
  float best_score = 0.0f;

  // Check all text sources with priority weights
  // aria-label is highest priority (1.3x) - most reliable for accessibility
  best_score = std::max(best_score, ScoreTextMatch(elem.aria_label, query) * 1.3f);
  best_score = std::max(best_score, ScoreTextMatch(elem.placeholder, query) * 1.2f);  // placeholder also high priority
  best_score = std::max(best_score, ScoreTextMatch(elem.nearby_text, query) * 1.1f);  // label text is very reliable
  best_score = std::max(best_score, ScoreTextMatch(elem.text, query));
  best_score = std::max(best_score, ScoreTextMatch(elem.title, query) * 0.9f);
  best_score = std::max(best_score, ScoreTextMatch(elem.name, query) * 0.8f);
  best_score = std::max(best_score, ScoreTextMatch(elem.value, query) * 0.7f);

  // Boost with role match (both inferred and explicit)
  best_score += ScoreRoleMatch(elem.inferred_role, query);

  // Keyword-based element type boosting with synonym support
  std::string norm_query = NormalizeText(query);
  std::string lower_tag = elem.tag;
  std::transform(lower_tag.begin(), lower_tag.end(), lower_tag.begin(), ::tolower);

  // Input-related keywords boost input/textarea elements (with synonyms)
  if (MatchWithSynonyms(norm_query, "box") || MatchWithSynonyms(norm_query, "field") ||
      MatchWithSynonyms(norm_query, "input") || FuzzyMatch(norm_query, "text") ||
      FuzzyMatch(norm_query, "type") || FuzzyMatch(norm_query, "enter")) {
    if (lower_tag == "input" || lower_tag == "textarea") {
      best_score += 0.35f;
    }
  }

  // Button-related keywords boost button elements (with synonyms)
  if (MatchWithSynonyms(norm_query, "button") || MatchWithSynonyms(norm_query, "click") ||
      MatchWithSynonyms(norm_query, "submit") || MatchWithSynonyms(norm_query, "press")) {
    if (lower_tag == "button" || (lower_tag == "input" &&
        (elem.type == "submit" || elem.type == "button" ||
         elem.inferred_role.find("button") != std::string::npos))) {
      best_score += 0.35f;
    }
  }

  // Link/navigation keywords boost anchor elements (with synonyms)
  if (MatchWithSynonyms(norm_query, "link") || FuzzyMatch(norm_query, "tab") ||
      FuzzyMatch(norm_query, "menu") || FuzzyMatch(norm_query, "nav")) {
    if (lower_tag == "a") {
      best_score += 0.35f;
    }
  }

  // Select/dropdown keywords boost select elements (with synonyms)
  if (MatchWithSynonyms(norm_query, "select") || MatchWithSynonyms(norm_query, "dropdown") ||
      MatchWithSynonyms(norm_query, "choose") || FuzzyMatch(norm_query, "option")) {
    if (lower_tag == "select") {
      best_score += 0.35f;
    }
  }

  // Boost SELECT elements when nearby_text (label) matches the query
  // This ensures we prefer SELECT over LABEL when searching by label text
  if (lower_tag == "select" && !elem.nearby_text.empty()) {
    float nearby_score = ScoreTextMatch(elem.nearby_text, query);
    if (nearby_score > 0.5f) {
      best_score += 0.5f;  // Strong boost for SELECT with matching label
    }
  }

  // Penalize LABEL elements for select-related queries
  // This prevents labels from outranking their associated select elements
  if (lower_tag == "label" && !elem.label_for.empty()) {
    // Check if this might be a label for a select element
    if (FuzzyMatch(norm_query, "country") || FuzzyMatch(norm_query, "state") ||
        FuzzyMatch(norm_query, "city") || MatchWithSynonyms(norm_query, "select") ||
        MatchWithSynonyms(norm_query, "dropdown") || MatchWithSynonyms(norm_query, "choose")) {
      best_score *= 0.5f;  // Reduce label score for dropdown-like queries
    }
  }

  // Boost INPUT checkboxes when nearby_text (label) matches the query
  // This ensures we prefer INPUT over LABEL when searching by label text
  if (lower_tag == "input" && elem.type == "checkbox" && !elem.nearby_text.empty()) {
    float nearby_score = ScoreTextMatch(elem.nearby_text, query);

    // CRITICAL: For checkbox queries like "I agree checkbox", we need to match
    // the checkbox whose label contains "I agree", not just any checkbox
    // Extract the meaningful part of the query (remove generic checkbox-related keywords)
    std::string query_without_checkbox = norm_query;

    // Remove common checkbox-related suffixes/prefixes
    std::vector<std::string> checkbox_keywords = {
      "checkbox", "check box", "check", "tick", "toggle", "input", "box"
    };

    for (const auto& kw : checkbox_keywords) {
      size_t pos = query_without_checkbox.find(kw);
      if (pos != std::string::npos) {
        // Remove the keyword and surrounding spaces
        query_without_checkbox.erase(pos, kw.length());
      }
    }

    // Trim whitespace
    query_without_checkbox = NormalizeText(query_without_checkbox);

    // If we have specific keywords (like "I agree", "newsletter", "terms", etc.),
    // require STRONG match with nearby_text
    if (!query_without_checkbox.empty() && query_without_checkbox.length() > 1) {
      // Check if nearby_text contains the specific keywords
      std::string norm_nearby = NormalizeText(elem.nearby_text);

      // Try multiple matching strategies for better checkbox identification
      float specific_match = ScoreTextMatch(norm_nearby, query_without_checkbox);

      // Also try matching individual keywords from the query
      auto query_keywords = ExtractKeywords(query_without_checkbox);
      float keyword_match_score = 0.0f;
      int matched_keywords = 0;

      for (const auto& kw : query_keywords) {
        if (norm_nearby.find(kw) != std::string::npos) {
          matched_keywords++;
        }
      }

      if (!query_keywords.empty()) {
        keyword_match_score = static_cast<float>(matched_keywords) / query_keywords.size();
      }

      // Use the better of the two match scores
      float best_match = std::max(specific_match, keyword_match_score);

      // Log checkbox scoring for debugging
      LOG_DEBUG("SemanticMatcher", "Checkbox scoring: id=" + elem.id +
                " nearby_text='" + elem.nearby_text.substr(0, 50) + "'" +
                " query_stripped='" + query_without_checkbox + "'" +
                " specific_match=" + std::to_string(specific_match) +
                " keyword_match=" + std::to_string(keyword_match_score) +
                " best_match=" + std::to_string(best_match));

      if (best_match > 0.6f) {
        // Very strong specific match - this is definitely the right checkbox
        best_score += 3.0f;  // Strong boost for correct checkbox
      } else if (best_match > 0.4f) {
        // Good match
        best_score += 1.5f;
      } else if (best_match > 0.2f) {
        // Weak match - probably not the right checkbox
        best_score += 0.4f;
      } else {
        // No match - this is likely the wrong checkbox, penalize it
        best_score -= 0.8f;  // Stronger penalty for checkbox with non-matching label
      }
    } else {
      // Generic "checkbox" query - use original scoring
      if (nearby_score > 0.5f) {
        best_score += 1.5f;
      } else if (nearby_score > 0.3f) {
        best_score += 0.8f;
      }
    }
  }

  // Penalize LABEL elements for checkbox-related queries
  // This prevents labels from outranking their associated checkbox inputs
  if (lower_tag == "label" && !elem.label_for.empty()) {
    // Check if the label text matches the query (like "I agree", "accept terms", etc.)
    float text_score = ScoreTextMatch(elem.text, query);
    if (text_score > 0.5f) {
      // This is likely a label for a checkbox/input, penalize it heavily
      best_score *= 0.2f;  // Strong penalty to prefer the actual input
    }
  }

  // Checkbox keywords boost checkbox input elements (with synonyms)
  // BUT: Only apply generic boost if there's no specific text match via nearby_text
  if (MatchWithSynonyms(norm_query, "checkbox") || MatchWithSynonyms(norm_query, "check") ||
      FuzzyMatch(norm_query, "tick") || FuzzyMatch(norm_query, "toggle")) {
    if (lower_tag == "input" && elem.type == "checkbox") {
      // Only give a small generic boost - nearby_text match is much more important
      best_score += 0.3f;  // Reduced from 0.6f to reduce ambiguity
    }
    // Also boost LABEL elements for checkbox queries (custom checkboxes use labels)
    if (lower_tag == "label") {
      best_score += 0.4f;  // Reduced from 0.7f and less than INPUT
    }
  }

  // Boost for specific input types matching query
  if (!elem.type.empty()) {
    std::string lower_type = elem.type;
    std::transform(lower_type.begin(), lower_type.end(), lower_type.begin(), ::tolower);

    if (MatchWithSynonyms(norm_query, "email") && lower_type == "email") {
      best_score += 0.4f;
    }
    if (MatchWithSynonyms(norm_query, "password") && lower_type == "password") {
      best_score += 0.4f;
    }
    if (FuzzyMatch(norm_query, "search") && lower_type == "search") {
      best_score += 0.4f;
    }
    if (FuzzyMatch(norm_query, "number") && lower_type == "number") {
      best_score += 0.4f;
    }
    if (MatchWithSynonyms(norm_query, "phone") && (lower_type == "tel" || lower_type == "phone")) {
      best_score += 0.4f;
    }
  }

  return std::min(2.0f, best_score);  // Increased max to 2.0 to allow strong matches
}

std::vector<ElementMatch> OwlSemanticMatcher::FindByDescription(
    const std::string& context_id,
    const std::string& description,
    int max_results) {

  LOG_DEBUG("SemanticMatcher", "FindByDescription START: context=" + context_id + " description='" + description + "' max=" + std::to_string(max_results));

  // Use enhanced scoring if enabled (default: enabled for 90%+ accuracy)
  if (use_enhanced_scoring_) {
    LOG_DEBUG("SemanticMatcher", "Using enhanced CompositeScorer for improved accuracy");
    return FindByDescriptionEnhanced(context_id, description, max_results);
  }

  // Legacy scoring path (kept for backward compatibility)
  LOG_DEBUG("SemanticMatcher", "Using legacy scoring path");

  std::lock_guard<std::mutex> lock(mutex_);

  LOG_DEBUG("SemanticMatcher", "Acquired lock, searching for: '" + description + "'");

  std::vector<ElementMatch> matches;

  if (!elements_by_context_.count(context_id)) {
    LOG_WARN("SemanticMatcher", "No elements for context: " + context_id);
    return matches;
  }

#ifdef OWL_DEBUG_BUILD
  size_t element_count = elements_by_context_[context_id].size();
  LOG_DEBUG("SemanticMatcher", "Found context " + context_id + " with " + std::to_string(element_count) + " elements");
#endif

  // Score all elements
  for (const auto& elem : elements_by_context_[context_id]) {
    if (!elem.visible) continue;  // Skip hidden elements

    float score = ScoreContextMatch(elem, description);

    if (score > 0.3f) {  // Threshold for relevance
      ElementMatch match;
      match.element = elem;
      match.confidence = score;
      match.match_reason = "Matched '" + description + "' with role=" + elem.inferred_role;
      matches.push_back(match);
    }
  }

  // Sort by confidence (descending), with intelligent tie-breaking for equal scores
  std::sort(matches.begin(), matches.end(),
    [](const ElementMatch& a, const ElementMatch& b) {
      // Primary sort by confidence
      if (std::abs(a.confidence - b.confidence) > 0.01f) {
        return a.confidence > b.confidence;
      }

      // When confidence is equal, use multiple tie-breakers:

      // 1. Prefer larger elements (more prominent/primary buttons are usually bigger)
      int area_a = a.element.width * a.element.height;
      int area_b = b.element.width * b.element.height;
      if (area_a > area_b * 1.5) return true;  // A is 50% larger
      if (area_b > area_a * 1.5) return false; // B is 50% larger

      // 2. Prefer elements higher on page (main content vs dropdowns/footer)
      // Elements in top 60% of viewport are preferred
      bool a_is_primary = a.element.y < 650;  // Assuming typical viewport
      bool b_is_primary = b.element.y < 650;
      if (a_is_primary && !b_is_primary) return true;
      if (b_is_primary && !a_is_primary) return false;

      // 3. Prefer elements more to the left/center (not in sidebars)
      bool a_is_centered = a.element.x > 200 && a.element.x < 1200;
      bool b_is_centered = b.element.x > 200 && b.element.x < 1200;
      if (a_is_centered && !b_is_centered) return true;
      if (b_is_centered && !a_is_centered) return false;

      // 4. Tag priority - prefer semantic HTML elements
      auto tag_priority = [](const std::string& tag) -> int {
        std::string lower_tag = tag;
        std::transform(lower_tag.begin(), lower_tag.end(), lower_tag.begin(), ::tolower);
        if (lower_tag == "a") return 10;           // Links highest priority
        if (lower_tag == "button") return 9;       // Buttons second
        if (lower_tag == "input") return 8;        // Inputs third
        if (lower_tag == "textarea") return 7;     // Textareas fourth
        if (lower_tag == "select") return 6;       // Selects fifth
        return 0;                                  // Generic tags lowest
      };

      return tag_priority(a.element.tag) > tag_priority(b.element.tag);
    });

  // ============================================================
  // LLM ENHANCEMENT: Disambiguate if needed
  // ============================================================

  // Check if we should use LLM to improve the selection
  if (ShouldUseLLMDisambiguation(matches)) {
    // Try to get LLM client from BrowserManager
    OwlBrowserManager* manager = OwlBrowserManager::GetInstance();
    if (manager && manager->IsLLMReady()) {
      OwlLLMClient* llm = manager->GetLLMClient();
      if (llm) {
        LOG_DEBUG("SemanticMatcher", "Invoking LLM disambiguation");
        matches = DisambiguateWithLLM(matches, description, llm);
        LOG_DEBUG("SemanticMatcher", "LLM disambiguation complete, top match confidence: " +
                 std::to_string(matches[0].confidence));
      }
    } else {
      LOG_DEBUG("SemanticMatcher", "LLM not available for disambiguation, using fast matcher results");
    }
  }

  // Limit results
  if (matches.size() > static_cast<size_t>(max_results)) {
    matches.resize(max_results);
  }

  return matches;
}

std::vector<ElementMatch> OwlSemanticMatcher::FindByRole(
    const std::string& context_id,
    const std::string& role,
    const std::string& text_hint) {

  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<ElementMatch> matches;

  if (!elements_by_context_.count(context_id)) {
    return matches;
  }

  for (const auto& elem : elements_by_context_[context_id]) {
    if (!elem.visible) continue;

    if (elem.inferred_role.find(role) != std::string::npos) {
      float score = 0.8f;

      if (!text_hint.empty()) {
        score = ScoreTextMatch(elem.text + " " + elem.placeholder, text_hint);
      }

      if (score > 0.3f) {
        ElementMatch match;
        match.element = elem;
        match.confidence = score;
        match.match_reason = "Role=" + role;
        matches.push_back(match);
      }
    }
  }

  std::sort(matches.begin(), matches.end(),
    [](const ElementMatch& a, const ElementMatch& b) {
      return a.confidence > b.confidence;
    });

  return matches;
}

// ============================================================
// LLM-Enhanced Matching (NEW!)
// ============================================================

bool OwlSemanticMatcher::ShouldUseLLMDisambiguation(const std::vector<ElementMatch>& matches) {
  if (matches.empty()) {
    return false;  // No matches - LLM won't help
  }

  // Criteria for LLM disambiguation:

  // 1. Top match has low confidence (< 0.7)
  if (matches[0].confidence < 0.7f) {
    LOG_DEBUG("SemanticMatcher", "LLM: Top match confidence too low: " +
             std::to_string(matches[0].confidence));
    return true;
  }

  // 2. Multiple matches with similar confidence (ambiguous)
  if (matches.size() >= 2) {
    float top_confidence = matches[0].confidence;
    float second_confidence = matches[1].confidence;

    // If top 2 are within 0.15 of each other, it's ambiguous
    if (std::abs(top_confidence - second_confidence) < 0.15f) {
      LOG_DEBUG("SemanticMatcher", "LLM: Ambiguous - top 2 matches similar: " +
               std::to_string(top_confidence) + " vs " + std::to_string(second_confidence));
      return true;
    }
  }

  // 3. Check if we have 3+ matches all above 0.5 (many plausible options)
  int strong_matches = 0;
  for (const auto& match : matches) {
    if (match.confidence >= 0.5f) {
      strong_matches++;
    }
  }

  if (strong_matches >= 3) {
    LOG_DEBUG("SemanticMatcher", "LLM: Multiple strong candidates (" +
             std::to_string(strong_matches) + " matches above 0.5)");
    return true;
  }

  // Otherwise, trust the fast matcher
  LOG_DEBUG("SemanticMatcher", "LLM: Fast match confident enough: " +
            std::to_string(matches[0].confidence));
  return false;
}

std::string OwlSemanticMatcher::ElementToLLMContext(const ElementSemantics& elem, int index) {
  std::ostringstream ctx;

  ctx << "[" << index << "] ";
  ctx << "<" << elem.tag;

  if (!elem.type.empty()) {
    ctx << " type=\"" << elem.type << "\"";
  }
  if (!elem.id.empty()) {
    ctx << " id=\"" << elem.id << "\"";
  }
  if (!elem.name.empty()) {
    ctx << " name=\"" << elem.name << "\"";
  }
  if (!elem.aria_label.empty()) {
    ctx << " aria-label=\"" << elem.aria_label << "\"";
  }
  if (!elem.placeholder.empty()) {
    ctx << " placeholder=\"" << elem.placeholder << "\"";
  }
  if (!elem.title.empty()) {
    ctx << " title=\"" << elem.title << "\"";
  }

  ctx << ">";

  if (!elem.text.empty()) {
    std::string text = elem.text;
    if (text.length() > 50) {
      text = text.substr(0, 50) + "...";
    }
    ctx << text;
  }

  ctx << "</" << elem.tag << ">";

  // Add visual context
  ctx << " [position: x=" << elem.x << " y=" << elem.y
      << " size=" << elem.width << "x" << elem.height << "]";

  // Add nearby context
  if (!elem.nearby_text.empty()) {
    std::string nearby = elem.nearby_text;
    if (nearby.length() > 50) {
      nearby = nearby.substr(0, 50) + "...";
    }
    ctx << " [label: " << nearby << "]";
  }

  return ctx.str();
}

std::vector<ElementMatch> OwlSemanticMatcher::DisambiguateWithLLM(
    const std::vector<ElementMatch>& candidates,
    const std::string& description,
    OwlLLMClient* llm) {

  if (!llm) {
    LOG_WARN("SemanticMatcher", "LLM not available for disambiguation");
    return candidates;  // Return original matches
  }

  LOG_DEBUG("SemanticMatcher", "Using LLM to disambiguate " +
           std::to_string(candidates.size()) + " candidates for: '" + description + "'");

  // Prepare candidate list for LLM (top 5 max for context window)
  std::ostringstream candidates_xml;
  int num_candidates = std::min(5, static_cast<int>(candidates.size()));

  candidates_xml << "<candidates>\n";
  for (int i = 0; i < num_candidates; i++) {
    candidates_xml << "  " << ElementToLLMContext(candidates[i].element, i) << "\n";
  }
  candidates_xml << "</candidates>";

  // Build LLM prompt
  std::string system_prompt = R"(You are an intelligent element selector for browser automation.
Given a user's description and a list of candidate HTML elements, determine which element best matches the description.

Rules:
1. PRIORITIZE TEXT MATCH + ELEMENT TYPE together:
   - For checkbox queries: LABEL elements with matching text (e.g., "I'm not a robot") are BETTER than INPUT checkboxes without matching text
   - LABEL with text "I'm not a robot" is the CORRECT match for "robot checkbox" query
   - INPUT type="checkbox" without relevant text (e.g., name="newsletter", name="terms") is WRONG for "robot checkbox" query
   - Example: "robot checkbox" → LABEL containing "robot" or "I'm not a robot" is BEST match
   - Container elements (DIV, SPAN) are LESS preferred than LABEL or INPUT
2. Candidates are pre-sorted by confidence score (highest first). Prefer lower-index candidates unless there's a clear reason not to.
3. For checkbox/button queries, text content is MORE IMPORTANT than element type alone
4. Prioritize semantic HTML attributes (aria-label, placeholder, alt, title)
5. If multiple elements match equally, prefer the one higher on the page (lower y coordinate)
6. Output ONLY a JSON object with the index of the best match

Output format:
{
  "best_match_index": 0,
  "reasoning": "brief explanation why this element matches"
}

If no element is a good match, return: {"best_match_index": -1, "reasoning": "no good match"})";

  std::string user_prompt =
    "<query>" + description + "</query>\n\n" +
    candidates_xml.str() + "\n\n" +
    "Which element index best matches the query? Output JSON:";

  // Query LLM with low temperature for deterministic selection
  auto response = llm->Complete(user_prompt, system_prompt, 256, 0.2f);

  if (!response.success) {
    LOG_ERROR("SemanticMatcher", "LLM query failed: " + response.error);
    return candidates;  // Fallback to original matches
  }

  LOG_DEBUG("SemanticMatcher", "LLM response: " + response.content);

  // Parse JSON response
  std::regex json_regex(R"(\{[\s\S]*\})");
  std::smatch match;
  std::string json_str = response.content;

  if (std::regex_search(response.content, match, json_regex)) {
    json_str = match[0].str();
  }

  CefRefPtr<CefValue> json_value = CefParseJSON(json_str, JSON_PARSER_ALLOW_TRAILING_COMMAS);

  if (!json_value || json_value->GetType() != VTYPE_DICTIONARY) {
    LOG_ERROR("SemanticMatcher", "Failed to parse LLM JSON response");
    return candidates;  // Fallback
  }

  CefRefPtr<CefDictionaryValue> dict = json_value->GetDictionary();

  if (!dict->HasKey("best_match_index")) {
    LOG_ERROR("SemanticMatcher", "LLM response missing best_match_index");
    return candidates;  // Fallback
  }

  int best_index = dict->GetInt("best_match_index");
  std::string reasoning = dict->HasKey("reasoning") ?
                          dict->GetString("reasoning").ToString() :
                          "LLM selected this element";

  if (best_index < 0 || best_index >= static_cast<int>(candidates.size())) {
    LOG_WARN("SemanticMatcher", "LLM returned no good match (index " +
             std::to_string(best_index) + ")");
    return candidates;  // Return original order
  }

  LOG_DEBUG("SemanticMatcher", "LLM selected candidate " + std::to_string(best_index) +
           ": " + reasoning);

  // IMPORTANT: Trust LLM's judgment - put selected element first
  // The LLM has better semantic understanding than numerical confidence scores
  std::vector<ElementMatch> reordered;

  // Add LLM's selection as first element with boosted confidence
  ElementMatch selected = candidates[best_index];
  selected.confidence = 2.0f;  // Very high confidence for LLM selection
  selected.match_reason = "LLM: " + reasoning;
  reordered.push_back(selected);

  // Add remaining candidates in original order (skip the one we already added)
  for (int i = 0; i < static_cast<int>(candidates.size()); i++) {
    if (i != best_index) {
      reordered.push_back(candidates[i]);
    }
  }

  return reordered;
}

// ============================================================
// ENHANCED SCORING: Multi-Scorer Ensemble for 90%+ Accuracy
// ============================================================

std::vector<ElementMatch> OwlSemanticMatcher::FindByDescriptionEnhanced(
    const std::string& context_id,
    const std::string& description,
    int max_results) {

  LOG_DEBUG("SemanticMatcher", "FindByDescriptionEnhanced: context=" + context_id +
           " description='" + description + "' max=" + std::to_string(max_results));

  std::lock_guard<std::mutex> lock(mutex_);

  if (!elements_by_context_.count(context_id)) {
    LOG_WARN("SemanticMatcher", "No elements for context: " + context_id);
    return {};
  }

  const auto& elements = elements_by_context_[context_id];
  size_t element_count = elements.size();

  // ============================================================
  // CACHE CHECK: Return cached result if valid
  // ============================================================
  if (cache_enabled_) {
    std::string cache_key = MakeCacheKey(context_id, description);
    auto cache_it = search_cache_.find(cache_key);

    if (cache_it != search_cache_.end()) {
      const auto& cached = cache_it->second;

      // Check if cache is still valid (TTL not expired and element count unchanged)
      auto now = std::chrono::steady_clock::now();
      auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - cached.timestamp).count();

      if (age_ms < cache_ttl_ms_ && cached.element_count == element_count) {
        LOG_DEBUG("SemanticMatcher", "Cache HIT for '" + description + "' (age=" +
                  std::to_string(age_ms) + "ms, results=" + std::to_string(cached.results.size()) + ")");
        return cached.results;
      } else {
        // Cache expired or stale, remove it
        search_cache_.erase(cache_it);
        LOG_DEBUG("SemanticMatcher", "Cache STALE for '" + description + "' (age=" +
                  std::to_string(age_ms) + "ms, elements changed: " +
                  std::to_string(cached.element_count) + " -> " + std::to_string(element_count) + ")");
      }
    }
  }

  LOG_DEBUG("SemanticMatcher", "Enhanced scoring " + std::to_string(element_count) + " elements");

  // Use CompositeScorer for multi-scorer ensemble matching
  CompositeScorer* compositor = CompositeScorer::GetInstance();

  // Score and rank all elements
  std::vector<ElementMatch> matches = compositor->ScoreAndRank(
    elements,
    description,
    0.25f,      // Lower threshold to catch more candidates
    max_results * 2  // Get extra candidates for LLM disambiguation
  );

  LOG_DEBUG("SemanticMatcher", "CompositeScorer found " + std::to_string(matches.size()) + " candidates");

  if (matches.empty()) {
    LOG_WARN("SemanticMatcher", "No matches found by enhanced scoring");
    return matches;
  }

#ifdef OWL_DEBUG_BUILD
  // Log top matches for debugging
  int log_count = std::min(3, static_cast<int>(matches.size()));
  for (int i = 0; i < log_count; i++) {
    const auto& m = matches[i];
    LOG_DEBUG("SemanticMatcher", "  #" + std::to_string(i+1) + ": " +
              m.element.tag + " id='" + m.element.id + "' " +
              "text='" + m.element.text.substr(0, 30) + "' " +
              "confidence=" + std::to_string(m.confidence) + " " +
              "reason=" + m.match_reason);
  }
#endif

  // ============================================================
  // LLM FALLBACK STRATEGY: Only use LLM when scores are truly tied
  // This saves 200-300ms per match in most cases
  // ============================================================

  // Case 1: Single match - no ambiguity, use it directly
  if (matches.size() == 1) {
    LOG_DEBUG("SemanticMatcher", "Single match found, no LLM needed");
    return matches;
  }

  // Case 2: Clear winner - top score is significantly higher than second
  // Using 0.05 gap threshold (5% difference is enough to declare winner)
  float topScore = matches[0].confidence;
  float secondScore = matches[1].confidence;
  float scoreGap = topScore - secondScore;

  // Helper lambda to cache and return results
  auto cacheAndReturn = [&](std::vector<ElementMatch>& results) -> std::vector<ElementMatch> {
    if (results.size() > static_cast<size_t>(max_results)) {
      results.resize(max_results);
    }

    // Store in cache
    if (cache_enabled_) {
      std::string cache_key = MakeCacheKey(context_id, description);
      CachedSearch cached;
      cached.results = results;
      cached.timestamp = std::chrono::steady_clock::now();
      cached.element_count = element_count;
      search_cache_[cache_key] = cached;
      LOG_DEBUG("SemanticMatcher", "Cache STORE for '" + description + "' (" +
                std::to_string(results.size()) + " results)");
    }

    return results;
  };

  if (scoreGap >= 0.05f) {
    LOG_DEBUG("SemanticMatcher", "Clear winner with gap=" + std::to_string(scoreGap) +
             " (top=" + std::to_string(topScore) + ", second=" + std::to_string(secondScore) + "), no LLM needed");
    return cacheAndReturn(matches);
  }

  // Case 3: Top score is high enough to be confident (>= 0.6)
  if (topScore >= 0.6f) {
    LOG_DEBUG("SemanticMatcher", "High confidence match=" + std::to_string(topScore) + ", no LLM needed");
    return cacheAndReturn(matches);
  }

  // Case 4: TRUE TIE - scores are very close AND low confidence
  // Only invoke LLM as last resort fallback
  LOG_DEBUG("SemanticMatcher", "Score tie detected (gap=" + std::to_string(scoreGap) +
           ", top=" + std::to_string(topScore) + "). Checking LLM fallback...");

  OwlBrowserManager* manager = OwlBrowserManager::GetInstance();
  if (manager && manager->IsLLMReady()) {
    OwlLLMClient* llm = manager->GetLLMClient();
    if (llm) {
      LOG_DEBUG("SemanticMatcher", "Invoking LLM for tie-breaking disambiguation");
      matches = DisambiguateWithLLM(matches, description, llm);

      if (!matches.empty()) {
        LOG_DEBUG("SemanticMatcher", "LLM tie-break complete, selected: " +
                 matches[0].element.tag + " confidence=" + std::to_string(matches[0].confidence));
      }
    }
  } else {
    LOG_DEBUG("SemanticMatcher", "LLM not available, using best score from tie");
  }

  return cacheAndReturn(matches);
}
