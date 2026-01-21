#include "owl_visual_proximity_scorer.h"
#include "logger.h"
#include <algorithm>
#include <cmath>

VisualProximityScorer* VisualProximityScorer::instance_ = nullptr;

VisualProximityScorer* VisualProximityScorer::GetInstance() {
  if (!instance_) {
    instance_ = new VisualProximityScorer();
  }
  return instance_;
}

bool VisualProximityScorer::IsAboveTheFold(const ElementSemantics& elem, int viewportHeight) {
  // Element center should be within top 75% of viewport
  float elemCenterY = elem.y + elem.height / 2.0f;
  float foldLine = viewportHeight * kFoldThreshold;
  return elemCenterY < foldLine;
}

bool VisualProximityScorer::IsInPrimaryContentArea(const ElementSemantics& elem, int viewportWidth) {
  // Primary content is typically in the center, avoiding sidebars
  float leftMargin = viewportWidth * kSidebarLeftMargin;
  float rightMargin = viewportWidth * (1.0f - kSidebarRightMargin);

  float elemCenterX = elem.x + elem.width / 2.0f;

  // Check if element is in the central content area
  bool inHorizontalCenter = (elemCenterX >= leftMargin && elemCenterX <= rightMargin);

  // Also check it's not in header/footer regions (unless it's a prominent element)
  // This is less strict - small elements in header/footer are okay

  return inHorizontalCenter;
}

float VisualProximityScorer::GetProminenceScore(const ElementSemantics& elem, int viewportWidth, int viewportHeight) {
  // Calculate element area relative to viewport
  int area = elem.width * elem.height;
  int viewportArea = viewportWidth * viewportHeight;

  // Filter out elements that are too small or suspiciously large
  if (elem.width < kMinProminentWidth || elem.height < kMinProminentHeight) {
    return 0.2f;  // Low prominence for tiny elements
  }

  if (elem.width > kMaxReasonableWidth || elem.height > kMaxReasonableHeight) {
    // Very large elements are likely containers, not interactive elements
    return 0.3f;
  }

  // Optimal size range for interactive elements (buttons, inputs)
  // ~100-400px wide, 30-60px tall
  bool isOptimalWidth = (elem.width >= 80 && elem.width <= 400);
  bool isOptimalHeight = (elem.height >= 25 && elem.height <= 80);

  if (isOptimalWidth && isOptimalHeight) {
    return 1.0f;  // Perfect size for interactive element
  }

  // Scale by relative area (larger = more prominent, up to a point)
  float relativeArea = static_cast<float>(area) / viewportArea;

  // Normalize to 0-1 range (assuming 0.001-0.01 is typical for buttons)
  float normalized = std::min(1.0f, relativeArea / 0.01f);

  return 0.4f + 0.6f * normalized;
}

float VisualProximityScorer::GetCenterBiasScore(const ElementSemantics& elem, int viewportWidth) {
  // Calculate distance from horizontal center
  float centerX = viewportWidth / 2.0f;
  float elemCenterX = elem.x + elem.width / 2.0f;
  float distanceFromCenter = std::abs(elemCenterX - centerX);

  // Normalize: elements at center = 1.0, at edges = 0.0
  float maxDistance = viewportWidth / 2.0f;
  float normalizedDistance = distanceFromCenter / maxDistance;

  // Use quadratic falloff for smoother scoring
  return 1.0f - (normalizedDistance * normalizedDistance);
}

float VisualProximityScorer::GetVerticalPositionScore(const ElementSemantics& elem, int viewportHeight) {
  // Prefer elements higher on the page (but not in header)
  float headerLine = viewportHeight * kHeaderHeight;
  float footerLine = viewportHeight * (1.0f - kFooterHeight);

  float elemCenterY = elem.y + elem.height / 2.0f;

  // In header region: slight penalty
  if (elemCenterY < headerLine) {
    return 0.7f;
  }

  // In footer region: moderate penalty
  if (elemCenterY > footerLine) {
    return 0.4f;
  }

  // In main content area: score based on vertical position
  // Higher = better (inverse of normalized position)
  float normalizedY = (elemCenterY - headerLine) / (footerLine - headerLine);
  return 1.0f - (normalizedY * 0.5f);  // Range: 0.5 to 1.0
}

float VisualProximityScorer::GetZIndexScore(const ElementSemantics& elem) {
  // Higher z-index = more likely to be important overlay/modal
  // But extremely high z-index might be tooltip/popup which is less relevant

  int zIndex = elem.z_index;  // Now we have actual z-index from ElementSemantics

  if (zIndex <= kNormalZIndex) {
    return 0.5f;  // Normal stacking
  }

  if (zIndex >= kTooltipZIndex) {
    return 0.3f;  // Likely tooltip/popup, less relevant for main interaction
  }

  if (zIndex >= kModalZIndex) {
    return 0.9f;  // Modal/dialog, very relevant
  }

  // Scale linearly for moderate z-index values
  return 0.5f + (static_cast<float>(zIndex) / kModalZIndex) * 0.4f;
}

float VisualProximityScorer::GetOpacityScore(const ElementSemantics& elem) {
  // Opacity from ElementSemantics now includes cumulative opacity (parent cascade)
  // Elements with higher opacity are more likely to be the intended target

  float opacity = elem.opacity;

  // Very low opacity elements are likely decorative or transitioning
  if (opacity < 0.1f) {
    return 0.0f;  // Essentially invisible
  }

  if (opacity < 0.3f) {
    return 0.2f;  // Very faded, probably not primary element
  }

  if (opacity < 0.5f) {
    return 0.4f;  // Semi-transparent
  }

  if (opacity < 0.8f) {
    return 0.7f;  // Slightly transparent
  }

  return 1.0f;  // Fully opaque - most reliable
}

float VisualProximityScorer::Score(const ElementSemantics& elem, int viewportWidth, int viewportHeight) {
  // Visibility check
  if (!elem.visible) {
    return 0.0f;
  }

  // Zero-size elements are not interactable
  if (elem.width <= 0 || elem.height <= 0) {
    return 0.0f;
  }

  // Calculate component scores
  float aboveTheFold = IsAboveTheFold(elem, viewportHeight) ? 1.0f : 0.4f;
  float primaryArea = IsInPrimaryContentArea(elem, viewportWidth) ? 1.0f : 0.5f;
  float prominence = GetProminenceScore(elem, viewportWidth, viewportHeight);
  float centerBias = GetCenterBiasScore(elem, viewportWidth);
  float verticalPos = GetVerticalPositionScore(elem, viewportHeight);
  float zIndexScore = GetZIndexScore(elem);
  float opacityScore = GetOpacityScore(elem);

  // Debug logging for visibility scoring (only for interactive elements with IDs)
  if (!elem.id.empty()) {
    LOG_DEBUG("VisualProximity", "Scoring id='" + elem.id + "': " +
              "z_index=" + std::to_string(elem.z_index) + " " +
              "opacity=" + std::to_string(elem.opacity) + " " +
              "zScore=" + std::to_string(zIndexScore) + " " +
              "opacityScore=" + std::to_string(opacityScore));
  }

  // Weighted combination
  float score = aboveTheFold * kAboveTheFoldWeight +
                primaryArea * kPrimaryAreaWeight +
                prominence * kProminenceWeight +
                centerBias * kCenterBiasWeight +
                verticalPos * kVerticalPosWeight +
                zIndexScore * kZIndexWeight +
                opacityScore * kOpacityWeight;

  // Special case: very prominent elements in primary area get bonus
  if (primaryArea > 0.9f && prominence > 0.8f && aboveTheFold > 0.9f) {
    score = std::min(1.0f, score + 0.1f);
  }

  return score;
}

float VisualProximityScorer::CompareElements(const ElementSemantics& elem1, const ElementSemantics& elem2,
                                              int viewportWidth, int viewportHeight) {
  float score1 = Score(elem1, viewportWidth, viewportHeight);
  float score2 = Score(elem2, viewportWidth, viewportHeight);

  return score1 - score2;  // Positive = elem1 preferred
}
