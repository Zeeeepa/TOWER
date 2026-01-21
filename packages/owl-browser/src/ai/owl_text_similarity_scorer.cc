#include "owl_text_similarity_scorer.h"
#include "logger.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

TextSimilarityScorer* TextSimilarityScorer::instance_ = nullptr;

TextSimilarityScorer* TextSimilarityScorer::GetInstance() {
  if (!instance_) {
    instance_ = new TextSimilarityScorer();
  }
  return instance_;
}

std::string TextSimilarityScorer::Normalize(const std::string& text) {
  std::string result;
  result.reserve(text.length());

  for (char c : text) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      result += std::tolower(static_cast<unsigned char>(c));
    } else if (!result.empty() && result.back() != ' ') {
      result += ' ';
    }
  }

  // Trim trailing space
  while (!result.empty() && result.back() == ' ') {
    result.pop_back();
  }

  return result;
}

std::vector<std::string> TextSimilarityScorer::Tokenize(const std::string& text) {
  std::vector<std::string> tokens;
  std::string normalized = Normalize(text);
  std::istringstream iss(normalized);
  std::string word;

  while (iss >> word) {
    if (word.length() >= 2) {  // Skip single-char tokens
      tokens.push_back(word);
    }
  }

  return tokens;
}

std::unordered_set<std::string> TextSimilarityScorer::GetNgrams(const std::string& text, int n) {
  std::unordered_set<std::string> ngrams;
  std::string normalized = Normalize(text);

  if (normalized.length() < static_cast<size_t>(n)) {
    if (!normalized.empty()) {
      ngrams.insert(normalized);
    }
    return ngrams;
  }

  for (size_t i = 0; i <= normalized.length() - n; i++) {
    ngrams.insert(normalized.substr(i, n));
  }

  return ngrams;
}

int TextSimilarityScorer::LevenshteinDistance(const std::string& s1, const std::string& s2) {
  const size_t m = s1.length();
  const size_t n = s2.length();

  if (m == 0) return static_cast<int>(n);
  if (n == 0) return static_cast<int>(m);

  // Optimize: use single row instead of full matrix
  std::vector<int> prev_row(n + 1);
  std::vector<int> curr_row(n + 1);

  // Initialize first row
  for (size_t j = 0; j <= n; j++) {
    prev_row[j] = static_cast<int>(j);
  }

  for (size_t i = 1; i <= m; i++) {
    curr_row[0] = static_cast<int>(i);

    for (size_t j = 1; j <= n; j++) {
      int cost = (s1[i-1] == s2[j-1]) ? 0 : 1;

      curr_row[j] = std::min({
        prev_row[j] + 1,      // deletion
        curr_row[j-1] + 1,    // insertion
        prev_row[j-1] + cost  // substitution
      });
    }

    std::swap(prev_row, curr_row);
  }

  return prev_row[n];
}

float TextSimilarityScorer::LevenshteinSimilarity(const std::string& s1, const std::string& s2) {
  std::string norm1 = Normalize(s1);
  std::string norm2 = Normalize(s2);

  if (norm1.empty() && norm2.empty()) return 1.0f;
  if (norm1.empty() || norm2.empty()) return 0.0f;

  int distance = LevenshteinDistance(norm1, norm2);
  int maxLen = static_cast<int>(std::max(norm1.length(), norm2.length()));

  return 1.0f - (static_cast<float>(distance) / maxLen);
}

float TextSimilarityScorer::JaroSimilarity(const std::string& s1, const std::string& s2) {
  if (s1.empty() && s2.empty()) return 1.0f;
  if (s1.empty() || s2.empty()) return 0.0f;

  int len1 = static_cast<int>(s1.length());
  int len2 = static_cast<int>(s2.length());

  // Maximum distance for matching
  int matchWindow = std::max(0, std::max(len1, len2) / 2 - 1);

  std::vector<bool> s1Matched(len1, false);
  std::vector<bool> s2Matched(len2, false);

  int matches = 0;
  int transpositions = 0;

  // Find matches
  for (int i = 0; i < len1; i++) {
    int start = std::max(0, i - matchWindow);
    int end = std::min(len2 - 1, i + matchWindow);

    for (int j = start; j <= end; j++) {
      if (s2Matched[j] || s1[i] != s2[j]) continue;

      s1Matched[i] = true;
      s2Matched[j] = true;
      matches++;
      break;
    }
  }

  if (matches == 0) return 0.0f;

  // Count transpositions
  int j = 0;
  for (int i = 0; i < len1; i++) {
    if (!s1Matched[i]) continue;
    while (!s2Matched[j]) j++;
    if (s1[i] != s2[j]) transpositions++;
    j++;
  }

  float m = static_cast<float>(matches);
  return (m / len1 + m / len2 + (m - transpositions / 2.0f) / m) / 3.0f;
}

float TextSimilarityScorer::JaroWinklerSimilarity(const std::string& s1, const std::string& s2) {
  std::string norm1 = Normalize(s1);
  std::string norm2 = Normalize(s2);

  float jaro = JaroSimilarity(norm1, norm2);

  // Find common prefix (up to 4 chars)
  int prefixLen = 0;
  int maxPrefix = std::min({4, static_cast<int>(norm1.length()), static_cast<int>(norm2.length())});

  for (int i = 0; i < maxPrefix; i++) {
    if (norm1[i] == norm2[i]) {
      prefixLen++;
    } else {
      break;
    }
  }

  // Winkler modification: boost for matching prefix
  const float scalingFactor = 0.1f;
  return jaro + prefixLen * scalingFactor * (1.0f - jaro);
}

float TextSimilarityScorer::NgramJaccardSimilarity(const std::string& s1, const std::string& s2, int n) {
  auto ngrams1 = GetNgrams(s1, n);
  auto ngrams2 = GetNgrams(s2, n);

  if (ngrams1.empty() && ngrams2.empty()) return 1.0f;
  if (ngrams1.empty() || ngrams2.empty()) return 0.0f;

  int intersection = 0;
  for (const auto& ng : ngrams1) {
    if (ngrams2.count(ng)) {
      intersection++;
    }
  }

  int unionSize = static_cast<int>(ngrams1.size() + ngrams2.size()) - intersection;
  if (unionSize == 0) return 0.0f;

  return static_cast<float>(intersection) / unionSize;
}

float TextSimilarityScorer::TokenSetRatio(const std::string& s1, const std::string& s2) {
  auto tokens1 = Tokenize(s1);
  auto tokens2 = Tokenize(s2);

  if (tokens1.empty() && tokens2.empty()) return 1.0f;
  if (tokens1.empty() || tokens2.empty()) return 0.0f;

  // Convert to sets
  std::unordered_set<std::string> set1(tokens1.begin(), tokens1.end());
  std::unordered_set<std::string> set2(tokens2.begin(), tokens2.end());

  // Find intersection and differences
  std::unordered_set<std::string> intersection;
  std::unordered_set<std::string> diff1, diff2;

  for (const auto& t : set1) {
    if (set2.count(t)) {
      intersection.insert(t);
    } else {
      diff1.insert(t);
    }
  }

  for (const auto& t : set2) {
    if (!set1.count(t)) {
      diff2.insert(t);
    }
  }

  // Build comparison strings
  auto setToStr = [](const std::unordered_set<std::string>& s) {
    std::vector<std::string> sorted(s.begin(), s.end());
    std::sort(sorted.begin(), sorted.end());
    std::string result;
    for (const auto& t : sorted) {
      if (!result.empty()) result += " ";
      result += t;
    }
    return result;
  };

  std::string intersectionStr = setToStr(intersection);
  std::string combined1 = intersectionStr + " " + setToStr(diff1);
  std::string combined2 = intersectionStr + " " + setToStr(diff2);

  // Normalize and trim
  while (!combined1.empty() && combined1[0] == ' ') combined1 = combined1.substr(1);
  while (!combined2.empty() && combined2[0] == ' ') combined2 = combined2.substr(1);

  // Calculate ratios using Levenshtein similarity
  float ratio1 = 1.0f;  // Intersection with itself
  float ratio2 = intersectionStr.empty() ? 0.0f : LevenshteinSimilarity(intersectionStr, combined1);
  float ratio3 = intersectionStr.empty() ? 0.0f : LevenshteinSimilarity(intersectionStr, combined2);
  float ratio4 = LevenshteinSimilarity(combined1, combined2);

  // Return best ratio
  return std::max({ratio1 * static_cast<float>(intersection.size()) / std::max(set1.size(), set2.size()),
                   ratio2, ratio3, ratio4});
}

bool TextSimilarityScorer::IsPrefixMatch(const std::string& query, const std::string& target) {
  std::string normQuery = Normalize(query);
  std::string normTarget = Normalize(target);

  if (normQuery.empty()) return false;

  return normTarget.find(normQuery) == 0;
}

bool TextSimilarityScorer::ContainsAllWords(const std::string& query, const std::string& target) {
  auto queryTokens = Tokenize(query);
  auto targetTokens = Tokenize(target);

  if (queryTokens.empty()) return true;

  std::unordered_set<std::string> targetSet(targetTokens.begin(), targetTokens.end());

  for (const auto& qt : queryTokens) {
    bool found = false;
    for (const auto& tt : targetSet) {
      // Allow partial matches (query word is prefix of target word)
      if (tt.find(qt) == 0 || qt.find(tt) == 0) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }

  return true;
}

float TextSimilarityScorer::Score(const std::string& query, const std::string& target) {
  if (query.empty() || target.empty()) {
    return 0.0f;
  }

  std::string normQuery = Normalize(query);
  std::string normTarget = Normalize(target);

  // Fast path: exact match
  if (normQuery == normTarget) {
    return 1.0f;
  }

  // Calculate weighted combination of algorithms
  float levenshtein = LevenshteinSimilarity(query, target);
  float jaroWinkler = JaroWinklerSimilarity(query, target);
  float ngram = NgramJaccardSimilarity(query, target, 2);
  float tokenSet = TokenSetRatio(query, target);

  float combined = levenshtein * kLevenshteinWeight +
                   jaroWinkler * kJaroWinklerWeight +
                   ngram * kNgramWeight +
                   tokenSet * kTokenSetWeight;

  // Apply bonuses for special matches
  if (normTarget.find(normQuery) != std::string::npos) {
    // Query is substring of target
    combined = std::min(1.0f, combined + kExactMatchBonus);
  } else if (IsPrefixMatch(query, target)) {
    combined = std::min(1.0f, combined + kPrefixMatchBonus);
  } else if (ContainsAllWords(query, target)) {
    combined = std::min(1.0f, combined + kContainsAllWordsBonus);
  }

  return combined;
}

float TextSimilarityScorer::ScoreBestMatch(const std::string& query,
                                           const std::vector<std::string>& targets) {
  float bestScore = 0.0f;

  for (const auto& target : targets) {
    if (target.empty()) continue;
    float score = Score(query, target);
    bestScore = std::max(bestScore, score);
  }

  return bestScore;
}
