#pragma once

#include "owl_semantic_matcher.h"
#include <string>
#include <vector>

/**
 * VisualProximityScorer - Layout-aware scoring for element selection.
 *
 * Evaluates elements based on their visual position and context:
 * - Primary content area detection (main content vs sidebars/footers)
 * - Above-the-fold preference (visible without scrolling)
 * - Z-index priority (topmost elements preferred)
 * - Form grouping (elements within same form/fieldset)
 * - Visual prominence (size-based importance)
 * - Horizontal center bias (main content typically centered)
 */
class VisualProximityScorer {
public:
  static VisualProximityScorer* GetInstance();

  /**
   * Calculate visual proximity score for an element.
   * Returns normalized score 0.0-1.0
   *
   * @param elem The element to score
   * @param viewportWidth Current viewport width (default 1920)
   * @param viewportHeight Current viewport height (default 1080)
   */
  float Score(const ElementSemantics& elem, int viewportWidth = 1920, int viewportHeight = 1080);

  /**
   * Calculate relative score between two elements.
   * Returns positive if elem1 should be preferred over elem2.
   */
  float CompareElements(const ElementSemantics& elem1, const ElementSemantics& elem2,
                        int viewportWidth = 1920, int viewportHeight = 1080);

  /**
   * Check if element is in the primary content area.
   */
  bool IsInPrimaryContentArea(const ElementSemantics& elem, int viewportWidth = 1920);

  /**
   * Check if element is above the fold (visible without scrolling).
   */
  bool IsAboveTheFold(const ElementSemantics& elem, int viewportHeight = 1080);

  /**
   * Get prominence score based on element size.
   */
  float GetProminenceScore(const ElementSemantics& elem, int viewportWidth = 1920, int viewportHeight = 1080);

  /**
   * Get center bias score (elements closer to horizontal center preferred).
   */
  float GetCenterBiasScore(const ElementSemantics& elem, int viewportWidth = 1920);

  /**
   * Get vertical position score (higher elements preferred for ties).
   */
  float GetVerticalPositionScore(const ElementSemantics& elem, int viewportHeight = 1080);

  /**
   * Get z-index priority score.
   */
  float GetZIndexScore(const ElementSemantics& elem);

  /**
   * Get opacity score (elements with higher opacity preferred).
   * Uses cascaded opacity from ElementSemantics.
   */
  float GetOpacityScore(const ElementSemantics& elem);

private:
  VisualProximityScorer() = default;
  static VisualProximityScorer* instance_;

  // Scoring weights (sum = 1.0)
  static constexpr float kAboveTheFoldWeight = 0.20f;
  static constexpr float kPrimaryAreaWeight = 0.18f;
  static constexpr float kProminenceWeight = 0.18f;
  static constexpr float kCenterBiasWeight = 0.12f;
  static constexpr float kVerticalPosWeight = 0.08f;
  static constexpr float kZIndexWeight = 0.12f;
  static constexpr float kOpacityWeight = 0.12f;

  // Layout thresholds
  static constexpr float kFoldThreshold = 0.75f;  // 75% of viewport height
  static constexpr float kSidebarLeftMargin = 0.15f;   // 15% from left edge
  static constexpr float kSidebarRightMargin = 0.15f;  // 15% from right edge
  static constexpr float kHeaderHeight = 0.12f;        // Top 12% is header
  static constexpr float kFooterHeight = 0.15f;        // Bottom 15% is footer

  // Size thresholds for prominence
  static constexpr int kMinProminentWidth = 50;
  static constexpr int kMinProminentHeight = 25;
  static constexpr int kMaxReasonableWidth = 800;   // Buttons/inputs shouldn't be wider
  static constexpr int kMaxReasonableHeight = 200;  // Or taller

  // Z-index normalization
  static constexpr int kNormalZIndex = 0;
  static constexpr int kModalZIndex = 1000;
  static constexpr int kTooltipZIndex = 10000;
};
