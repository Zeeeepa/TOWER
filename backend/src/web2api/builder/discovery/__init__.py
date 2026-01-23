"""
Discovery module for detecting application patterns and flows.

Provides:
- User flow detection (login, registration, checkout)
- API/XHR endpoint detection
- Deep form analysis with validation inference
"""

from web2api.builder.discovery.flow_detector import (
    FlowDetector,
    FlowConfig,
    UserFlow,
    FlowStep,
    FlowType,
)
from web2api.builder.discovery.api_detector import (
    APIDetector,
    APIConfig,
    APIEndpoint,
    APIType,
    RequestMethod,
)
from web2api.builder.discovery.form_analyzer import (
    FormAnalyzer,
    FormConfig,
    FormAnalysis,
    FieldAnalysis,
    FieldType,
    ValidationRule,
    TestCase,
)

__all__ = [
    # Flow Detector
    "FlowDetector",
    "FlowConfig",
    "UserFlow",
    "FlowStep",
    "FlowType",
    # API Detector
    "APIDetector",
    "APIConfig",
    "APIEndpoint",
    "APIType",
    "RequestMethod",
    # Form Analyzer
    "FormAnalyzer",
    "FormConfig",
    "FormAnalysis",
    "FieldAnalysis",
    "FieldType",
    "ValidationRule",
    "TestCase",
]
