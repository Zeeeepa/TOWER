#pragma once

#include "owl_semantic_matcher.h"
#include <string>
#include <vector>
#include <functional>

/**
 * CompositeScorer - Weighted ensemble scoring manager for semantic matching.
 *
 * Orchestrates multiple specialized scorers to achieve 90%+ accuracy:
 * - TextSimilarityScorer: Fuzzy string matching
 * - VisualProximityScorer: Layout-aware scoring
 * - ContextualRelevanceScorer: Semantic context analysis
 * - ElementTypeScorer: Element type inference
 *
 * Features:
 * - Weighted combination of scorer outputs
 * - Confidence calibration
 * - Dynamic weight adjustment based on query type
 * - Threshold-based filtering
 * - Score normalization to 0.0-1.0 range
 */

// Detailed score breakdown for debugging/analysis
struct ScoreBreakdown {
  float textSimilarity;
  float visualProximity;
  float contextualRelevance;
  float elementType;
  float combined;
  float calibrated;

  std::string ToString() const;
};

class CompositeScorer {
public:
  static CompositeScorer* GetInstance();

  /**
   * Calculate composite score for an element given a query.
   * Returns calibrated score 0.0-1.0
   */
  float Score(const ElementSemantics& elem, const std::string& query);

  /**
   * Calculate score with detailed breakdown.
   */
  ScoreBreakdown ScoreWithBreakdown(const ElementSemantics& elem, const std::string& query);

  /**
   * Score and rank multiple elements, returning them sorted by score.
   * Filters out elements below threshold.
   */
  std::vector<ElementMatch> ScoreAndRank(const std::vector<ElementSemantics>& elements,
                                         const std::string& query,
                                         float threshold = 0.3f,
                                         int maxResults = 10);

  /**
   * Configure scoring weights (for tuning/testing)
   */
  void SetWeights(float textSimilarity, float visualProximity,
                  float contextualRelevance, float elementType);

  /**
   * Get current weights
   */
  void GetWeights(float& textSimilarity, float& visualProximity,
                  float& contextualRelevance, float& elementType) const;

  /**
   * Set confidence calibration parameters
   */
  void SetCalibrationParams(float slope, float offset);

  /**
   * Automatically adjust weights based on query characteristics
   */
  void AutoAdjustWeights(const std::string& query);

  /**
   * Check if an element is a "strong match" (high confidence, can skip LLM)
   */
  bool IsStrongMatch(const ElementSemantics& elem, const std::string& query, float threshold = 0.85f);

  /**
   * Check if results are ambiguous (multiple high-scoring elements)
   */
  bool IsAmbiguous(const std::vector<ElementMatch>& matches, float threshold = 0.1f);

  /**
   * Get viewport dimensions for visual scoring
   */
  void SetViewportDimensions(int width, int height);

private:
  CompositeScorer();
  static CompositeScorer* instance_;

  // Default weights (tuned for general use cases)
  float textSimilarityWeight_ = 0.35f;
  float visualProximityWeight_ = 0.15f;
  float contextualRelevanceWeight_ = 0.30f;
  float elementTypeWeight_ = 0.20f;

  // Calibration parameters (sigmoid-based)
  float calibrationSlope_ = 4.0f;    // Controls steepness
  float calibrationOffset_ = 0.5f;   // Center point

  // Viewport dimensions for visual scoring
  int viewportWidth_ = 1920;
  int viewportHeight_ = 1080;

  // Thresholds
  static constexpr float kMinimumThreshold = 0.25f;
  static constexpr float kStrongMatchThreshold = 0.85f;
  static constexpr float kAmbiguityGap = 0.10f;

  // Score calibration: maps raw score to calibrated confidence
  float CalibrateScore(float rawScore);

  // Detect query type for weight adjustment
  enum class QueryType {
    GENERAL,         // Generic query
    TEXT_HEAVY,      // Query with specific text to match
    TYPE_SPECIFIC,   // Query specifying element type
    POSITIONAL,      // Query with position hints
    ACTION_BASED     // Query describing action to perform
  };

  QueryType DetectQueryType(const std::string& query);

  // Adjust weights based on query type
  void ApplyQueryTypeWeights(QueryType type);
};
