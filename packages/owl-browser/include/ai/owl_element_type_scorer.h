#pragma once

#include "owl_semantic_matcher.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

/**
 * ElementTypeScorer - Intelligent element type inference and matching.
 *
 * Evaluates elements based on:
 * - HTML semantic structure (button, input, anchor, etc.)
 * - ARIA role understanding
 * - Input type specificity (email, password, checkbox, etc.)
 * - Custom component detection (web components, React, Vue patterns)
 * - Interactive element prioritization
 * - Form control grouping
 */
class ElementTypeScorer {
public:
  static ElementTypeScorer* GetInstance();

  /**
   * Calculate element type match score for a given query.
   * Returns normalized score 0.0-1.0
   */
  float Score(const ElementSemantics& elem, const std::string& query);

  /**
   * Get element interactivity score (how likely it is to be clickable/typeable)
   */
  float GetInteractivityScore(const ElementSemantics& elem);

  /**
   * Determine if element is a form control
   */
  bool IsFormControl(const ElementSemantics& elem);

  /**
   * Get element semantic type (normalized)
   */
  std::string GetSemanticType(const ElementSemantics& elem);

  /**
   * Score how well element type matches query expectations
   */
  float ScoreTypeMatch(const ElementSemantics& elem, const std::string& query);

  /**
   * Score ARIA role match
   */
  float ScoreARIARoleMatch(const ElementSemantics& elem, const std::string& query);

  /**
   * Check if element is a custom component (web component, React, Vue)
   */
  bool IsCustomComponent(const ElementSemantics& elem);

  /**
   * Get priority score for interactive elements
   */
  float GetInteractivePriority(const ElementSemantics& elem);

private:
  ElementTypeScorer();
  static ElementTypeScorer* instance_;

  // Initialize databases
  void InitializeElementPriorities();
  void InitializeInputTypes();
  void InitializeARIARoles();

  // Scoring weights
  static constexpr float kTypeMatchWeight = 0.35f;
  static constexpr float kInteractivityWeight = 0.25f;
  static constexpr float kARIARoleWeight = 0.20f;
  static constexpr float kSpecificityWeight = 0.20f;

  // Element type priorities (higher = more likely to be target)
  std::unordered_map<std::string, float> elementPriorities_;

  // Input type -> keywords mapping
  std::unordered_map<std::string, std::vector<std::string>> inputTypeKeywords_;

  // ARIA role -> element behavior mapping
  std::unordered_map<std::string, std::vector<std::string>> ariaRoleBehaviors_;

  // Interactive tag set
  std::unordered_set<std::string> interactiveTags_;

  // Form control tags
  std::unordered_set<std::string> formControlTags_;

  // Helper: Extract query element type hints
  std::string ExtractTypeHint(const std::string& query);

  // Helper: Normalize tag name
  std::string NormalizeTag(const std::string& tag);
};
