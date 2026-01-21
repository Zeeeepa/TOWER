#include "owl_composite_scorer.h"
#include "owl_text_similarity_scorer.h"
#include "owl_visual_proximity_scorer.h"
#include "owl_contextual_relevance_scorer.h"
#include "owl_element_type_scorer.h"
#include "logger.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <cctype>

CompositeScorer* CompositeScorer::instance_ = nullptr;

std::string ScoreBreakdown::ToString() const {
  std::ostringstream ss;
  ss << "text=" << textSimilarity
     << " visual=" << visualProximity
     << " context=" << contextualRelevance
     << " type=" << elementType
     << " combined=" << combined
     << " calibrated=" << calibrated;
  return ss.str();
}

CompositeScorer* CompositeScorer::GetInstance() {
  if (!instance_) {
    instance_ = new CompositeScorer();
  }
  return instance_;
}

CompositeScorer::CompositeScorer() {
  // Default weights optimized for general element matching
  // These were tuned based on analysis of common failure cases
}

void CompositeScorer::SetWeights(float textSimilarity, float visualProximity,
                                  float contextualRelevance, float elementType) {
  // Normalize weights to sum to 1.0
  float sum = textSimilarity + visualProximity + contextualRelevance + elementType;
  if (sum > 0) {
    textSimilarityWeight_ = textSimilarity / sum;
    visualProximityWeight_ = visualProximity / sum;
    contextualRelevanceWeight_ = contextualRelevance / sum;
    elementTypeWeight_ = elementType / sum;
  }
}

void CompositeScorer::GetWeights(float& textSimilarity, float& visualProximity,
                                  float& contextualRelevance, float& elementType) const {
  textSimilarity = textSimilarityWeight_;
  visualProximity = visualProximityWeight_;
  contextualRelevance = contextualRelevanceWeight_;
  elementType = elementTypeWeight_;
}

void CompositeScorer::SetCalibrationParams(float slope, float offset) {
  calibrationSlope_ = slope;
  calibrationOffset_ = offset;
}

void CompositeScorer::SetViewportDimensions(int width, int height) {
  viewportWidth_ = width;
  viewportHeight_ = height;
}

float CompositeScorer::CalibrateScore(float rawScore) {
  // Use sigmoid function for calibration
  // This maps raw scores to a more calibrated confidence range
  // Scores around 0.5 remain neutral, low scores compress, high scores stretch

  // First, apply a linear transformation to shift the center
  float shifted = (rawScore - calibrationOffset_) * calibrationSlope_;

  // Apply sigmoid
  float calibrated = 1.0f / (1.0f + std::exp(-shifted));

  // Ensure bounds
  return std::max(0.0f, std::min(1.0f, calibrated));
}

CompositeScorer::QueryType CompositeScorer::DetectQueryType(const std::string& query) {
  std::string lower;
  for (char c : query) {
    lower += std::tolower(static_cast<unsigned char>(c));
  }

  // Check for type-specific keywords
  std::vector<std::string> typeKeywords = {
    "button", "input", "link", "checkbox", "dropdown", "select",
    "field", "textbox", "radio", "textarea"
  };

  for (const auto& kw : typeKeywords) {
    if (lower.find(kw) != std::string::npos) {
      return QueryType::TYPE_SPECIFIC;
    }
  }

  // Check for action-based keywords
  std::vector<std::string> actionKeywords = {
    "click", "tap", "press", "type", "enter", "fill", "submit",
    "check", "toggle", "select", "choose"
  };

  for (const auto& kw : actionKeywords) {
    if (lower.find(kw) != std::string::npos) {
      return QueryType::ACTION_BASED;
    }
  }

  // Check for positional keywords
  std::vector<std::string> positionKeywords = {
    "first", "second", "third", "last", "top", "bottom",
    "left", "right", "main", "primary", "header", "footer"
  };

  for (const auto& kw : positionKeywords) {
    if (lower.find(kw) != std::string::npos) {
      return QueryType::POSITIONAL;
    }
  }

  // Check if query contains quoted text or specific words to match
  if (lower.find('"') != std::string::npos ||
      lower.find("'") != std::string::npos) {
    return QueryType::TEXT_HEAVY;
  }

  // Default to general if no specific pattern detected
  // But check word count - longer queries are more text-heavy
  int wordCount = 0;
  bool inWord = false;
  for (char c : lower) {
    if (std::isalpha(static_cast<unsigned char>(c))) {
      if (!inWord) {
        wordCount++;
        inWord = true;
      }
    } else {
      inWord = false;
    }
  }

  if (wordCount >= 3) {
    return QueryType::TEXT_HEAVY;
  }

  return QueryType::GENERAL;
}

void CompositeScorer::ApplyQueryTypeWeights(QueryType type) {
  switch (type) {
    case QueryType::TEXT_HEAVY:
      // Emphasize text similarity for text-heavy queries
      textSimilarityWeight_ = 0.45f;
      visualProximityWeight_ = 0.10f;
      contextualRelevanceWeight_ = 0.30f;
      elementTypeWeight_ = 0.15f;
      break;

    case QueryType::TYPE_SPECIFIC:
      // Emphasize element type matching
      textSimilarityWeight_ = 0.25f;
      visualProximityWeight_ = 0.15f;
      contextualRelevanceWeight_ = 0.25f;
      elementTypeWeight_ = 0.35f;
      break;

    case QueryType::POSITIONAL:
      // Emphasize visual proximity
      textSimilarityWeight_ = 0.25f;
      visualProximityWeight_ = 0.35f;
      contextualRelevanceWeight_ = 0.25f;
      elementTypeWeight_ = 0.15f;
      break;

    case QueryType::ACTION_BASED:
      // Balance between context and type
      textSimilarityWeight_ = 0.25f;
      visualProximityWeight_ = 0.15f;
      contextualRelevanceWeight_ = 0.35f;
      elementTypeWeight_ = 0.25f;
      break;

    case QueryType::GENERAL:
    default:
      // Use default balanced weights
      textSimilarityWeight_ = 0.35f;
      visualProximityWeight_ = 0.15f;
      contextualRelevanceWeight_ = 0.30f;
      elementTypeWeight_ = 0.20f;
      break;
  }
}

void CompositeScorer::AutoAdjustWeights(const std::string& query) {
  QueryType type = DetectQueryType(query);
  ApplyQueryTypeWeights(type);

  LOG_DEBUG("CompositeScorer", "Query type: " + std::to_string(static_cast<int>(type)) +
            " weights: text=" + std::to_string(textSimilarityWeight_) +
            " visual=" + std::to_string(visualProximityWeight_) +
            " context=" + std::to_string(contextualRelevanceWeight_) +
            " type=" + std::to_string(elementTypeWeight_));
}

ScoreBreakdown CompositeScorer::ScoreWithBreakdown(const ElementSemantics& elem, const std::string& query) {
  ScoreBreakdown breakdown;

  // Get individual scorer instances
  auto textScorer = TextSimilarityScorer::GetInstance();
  auto visualScorer = VisualProximityScorer::GetInstance();
  auto contextScorer = ContextualRelevanceScorer::GetInstance();
  auto typeScorer = ElementTypeScorer::GetInstance();

  // Collect all text sources from element
  std::vector<std::string> textSources = {
    elem.text, elem.aria_label, elem.placeholder,
    elem.title, elem.name, elem.nearby_text
  };

  // Calculate individual scores
  breakdown.textSimilarity = textScorer->ScoreBestMatch(query, textSources);
  breakdown.visualProximity = visualScorer->Score(elem, viewportWidth_, viewportHeight_);
  breakdown.contextualRelevance = contextScorer->Score(elem, query);
  breakdown.elementType = typeScorer->Score(elem, query);

  // Calculate weighted combination
  breakdown.combined = breakdown.textSimilarity * textSimilarityWeight_ +
                       breakdown.visualProximity * visualProximityWeight_ +
                       breakdown.contextualRelevance * contextualRelevanceWeight_ +
                       breakdown.elementType * elementTypeWeight_;

  // Apply calibration
  breakdown.calibrated = CalibrateScore(breakdown.combined);

  return breakdown;
}

float CompositeScorer::Score(const ElementSemantics& elem, const std::string& query) {
  // Auto-adjust weights based on query type
  AutoAdjustWeights(query);

  // Get score breakdown
  ScoreBreakdown breakdown = ScoreWithBreakdown(elem, query);

  return breakdown.calibrated;
}

std::vector<ElementMatch> CompositeScorer::ScoreAndRank(
    const std::vector<ElementSemantics>& elements,
    const std::string& query,
    float threshold,
    int maxResults) {

  std::vector<ElementMatch> matches;

  // Auto-adjust weights
  AutoAdjustWeights(query);

  // Score all elements
  for (const auto& elem : elements) {
    if (!elem.visible) {
      continue;  // Skip hidden elements
    }

    ScoreBreakdown breakdown = ScoreWithBreakdown(elem, query);

    if (breakdown.calibrated >= threshold) {
      ElementMatch match;
      match.element = elem;
      match.confidence = breakdown.calibrated;
      match.match_reason = breakdown.ToString();
      matches.push_back(match);
    }
  }

  // Sort by score descending
  std::sort(matches.begin(), matches.end(),
    [](const ElementMatch& a, const ElementMatch& b) {
      if (std::abs(a.confidence - b.confidence) > 0.01f) {
        return a.confidence > b.confidence;
      }

      // Tie-breaking: prefer elements with more semantic info
      int aInfo = 0, bInfo = 0;
      if (!a.element.aria_label.empty()) aInfo++;
      if (!a.element.placeholder.empty()) aInfo++;
      if (!a.element.id.empty()) aInfo++;
      if (!b.element.aria_label.empty()) bInfo++;
      if (!b.element.placeholder.empty()) bInfo++;
      if (!b.element.id.empty()) bInfo++;

      if (aInfo != bInfo) {
        return aInfo > bInfo;
      }

      // Further tie-breaking: prefer higher on page
      return a.element.y < b.element.y;
    });

  // Limit results
  if (matches.size() > static_cast<size_t>(maxResults)) {
    matches.resize(maxResults);
  }

  return matches;
}

bool CompositeScorer::IsStrongMatch(const ElementSemantics& elem, const std::string& query, float threshold) {
  float score = Score(elem, query);
  return score >= threshold;
}

bool CompositeScorer::IsAmbiguous(const std::vector<ElementMatch>& matches, float threshold) {
  if (matches.size() < 2) {
    return false;  // Not ambiguous if fewer than 2 matches
  }

  // Check if top 2 scores are too close
  float topScore = matches[0].confidence;
  float secondScore = matches[1].confidence;

  return (topScore - secondScore) < threshold;
}
