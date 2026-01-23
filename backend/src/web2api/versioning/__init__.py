"""
Versioned test tracking module.

Provides tools for tracking test runs over time and comparing changes between versions.
"""

from web2api.versioning.diff_analyzer import (
    DiffAnalyzerError,
    VersionDiffAnalyzer,
)
from web2api.versioning.history_tracker import (
    HistoryStorageError,
    SnapshotNotFoundError,
    TestRunHistory,
)
from web2api.versioning.models import (
    BoundingBox,
    ChangeSeverity,
    ChangeType,
    ElementChange,
    ElementState,
    LayoutChange,
    NetworkChange,
    NetworkRequest,
    TestSnapshot,
    TextChange,
    VersionDiff,
    VersioningConfig,
    VisualChange,
)

__all__ = [
    "BoundingBox",
    "ChangeSeverity",
    "ChangeType",
    "DiffAnalyzerError",
    "ElementChange",
    "ElementState",
    "HistoryStorageError",
    "LayoutChange",
    "NetworkChange",
    "NetworkRequest",
    "SnapshotNotFoundError",
    "TestRunHistory",
    "TestSnapshot",
    "TextChange",
    "VersionDiff",
    "VersionDiffAnalyzer",
    "VersioningConfig",
    "VisualChange",
]
