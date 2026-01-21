#pragma once

#include "owl_semantic_matcher.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

/**
 * ContextualRelevanceScorer - Semantic context analysis for element selection.
 *
 * Evaluates elements based on semantic understanding:
 * - Expanded synonym/related term database
 * - Action verb understanding (click->button, type->input, etc.)
 * - Label-input relationship strength
 * - Nearby element context
 * - Domain-specific vocabulary (e-commerce, social media, etc.)
 * - Multi-word phrase understanding
 */
class ContextualRelevanceScorer {
public:
  static ContextualRelevanceScorer* GetInstance();

  /**
   * Calculate contextual relevance score for an element given a query.
   * Returns normalized score 0.0-1.0
   */
  float Score(const ElementSemantics& elem, const std::string& query);

  /**
   * Get action type implied by query (e.g., "click", "type", "select", "check")
   */
  std::string InferActionType(const std::string& query);

  /**
   * Get expected element types for an action
   */
  std::vector<std::string> GetExpectedElementTypes(const std::string& action);

  /**
   * Expand query with synonyms and related terms
   */
  std::vector<std::string> ExpandQuery(const std::string& query);

  /**
   * Check if element role matches query context
   */
  float ScoreRoleMatch(const std::string& elemRole, const std::string& query);

  /**
   * Score label-input relationship strength
   */
  float ScoreLabelRelationship(const ElementSemantics& elem, const std::string& query);

  /**
   * Score based on nearby text context
   */
  float ScoreNearbyContext(const ElementSemantics& elem, const std::string& query);

  /**
   * Get domain-specific relevance (e-commerce, auth, search, etc.)
   */
  float ScoreDomainRelevance(const ElementSemantics& elem, const std::string& query);

private:
  ContextualRelevanceScorer();
  static ContextualRelevanceScorer* instance_;

  // Initialize synonym and concept databases
  void InitializeSynonymDatabase();
  void InitializeActionVerbDatabase();
  void InitializeDomainVocabulary();

  // Scoring weights
  static constexpr float kSynonymMatchWeight = 0.30f;
  static constexpr float kActionMatchWeight = 0.20f;
  static constexpr float kLabelRelationWeight = 0.20f;
  static constexpr float kNearbyContextWeight = 0.15f;
  static constexpr float kDomainRelevanceWeight = 0.15f;

  // Synonym database: word -> list of synonyms/related terms
  std::unordered_map<std::string, std::vector<std::string>> synonyms_;

  // Action verb -> expected element types
  std::unordered_map<std::string, std::vector<std::string>> actionToElements_;

  // Domain vocabulary clusters
  std::unordered_map<std::string, std::unordered_set<std::string>> domainClusters_;

  // Common query patterns
  std::vector<std::pair<std::string, std::string>> queryPatterns_;  // pattern -> element type

  // Helper: Check word similarity
  bool WordsRelated(const std::string& word1, const std::string& word2);

  // Helper: Extract semantic keywords
  std::vector<std::string> ExtractSemanticKeywords(const std::string& text);

  // Helper: Calculate synonym overlap
  float CalculateSynonymOverlap(const std::vector<std::string>& queryWords,
                                 const std::vector<std::string>& elemWords);
};
