#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

/**
 * TextSimilarityScorer - Advanced text matching for semantic element selection.
 *
 * Implements multiple string similarity algorithms to achieve robust
 * matching even with typos, word order variations, and partial matches.
 *
 * Algorithms used:
 * - Levenshtein Distance (edit distance)
 * - Jaro-Winkler Similarity (prefix-weighted)
 * - N-gram Jaccard Similarity (character-level)
 * - Token Set Ratio (word order independent)
 */
class TextSimilarityScorer {
public:
  static TextSimilarityScorer* GetInstance();

  /**
   * Calculate overall text similarity between query and target text.
   * Returns normalized score 0.0-1.0
   */
  float Score(const std::string& query, const std::string& target);

  /**
   * Calculate best match score from query against multiple text sources.
   * Useful for matching against aria_label, placeholder, text, etc.
   */
  float ScoreBestMatch(const std::string& query, const std::vector<std::string>& targets);

  // Individual algorithm methods (exposed for testing/tuning)
  float LevenshteinSimilarity(const std::string& s1, const std::string& s2);
  float JaroWinklerSimilarity(const std::string& s1, const std::string& s2);
  float NgramJaccardSimilarity(const std::string& s1, const std::string& s2, int n = 2);
  float TokenSetRatio(const std::string& s1, const std::string& s2);

  /**
   * Check if query is a prefix match for target (for autocomplete-style matching)
   */
  bool IsPrefixMatch(const std::string& query, const std::string& target);

  /**
   * Check if all query words appear in target (any order)
   */
  bool ContainsAllWords(const std::string& query, const std::string& target);

private:
  TextSimilarityScorer() = default;
  static TextSimilarityScorer* instance_;

  // Text preprocessing
  std::string Normalize(const std::string& text);
  std::vector<std::string> Tokenize(const std::string& text);
  std::unordered_set<std::string> GetNgrams(const std::string& text, int n);

  // Algorithm weights for combined score
  static constexpr float kLevenshteinWeight = 0.25f;
  static constexpr float kJaroWinklerWeight = 0.30f;
  static constexpr float kNgramWeight = 0.20f;
  static constexpr float kTokenSetWeight = 0.25f;

  // Thresholds
  static constexpr float kExactMatchBonus = 0.2f;
  static constexpr float kPrefixMatchBonus = 0.15f;
  static constexpr float kContainsAllWordsBonus = 0.1f;

  // Helper: Levenshtein distance calculation
  int LevenshteinDistance(const std::string& s1, const std::string& s2);

  // Helper: Jaro similarity (base for Jaro-Winkler)
  float JaroSimilarity(const std::string& s1, const std::string& s2);
};
