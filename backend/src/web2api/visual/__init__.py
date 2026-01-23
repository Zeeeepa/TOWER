"""
Visual regression testing module.

Provides semantic and pixel-based image comparison capabilities.
"""

from web2api.visual.regression_engine import (
    ComparisonMode,
    ComparisonResult,
    VisualRegressionEngine,
)

__all__ = [
    "ComparisonMode",
    "ComparisonResult",
    "VisualRegressionEngine",
]
