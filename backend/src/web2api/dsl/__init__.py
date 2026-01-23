"""
DSL module for YAML test definitions.

Provides parsing, validation, and transformation of test specs.
No AI/LLM dependency - uses deterministic strategies.
"""

from web2api.dsl.models import (
    AssertionConfig,
    AssertionOperator,
    CustomAssertionConfig,
    ElementAssertionConfig,
    EnvironmentConfig,
    NetworkAssertionConfig,
    SecretReference,
    StepAction,
    TestMetadata,
    TestSpec,
    TestStep,
    TestSuite,
    VisualAssertionConfig,
    VisualComparisonMode,
)
from web2api.dsl.parser import DSLParser
from web2api.dsl.transformer import StepTransformer
from web2api.dsl.validator import DSLValidator

__all__ = [
    # Models
    "AssertionConfig",
    "AssertionOperator",
    "CustomAssertionConfig",
    "ElementAssertionConfig",
    "EnvironmentConfig",
    "NetworkAssertionConfig",
    "SecretReference",
    "StepAction",
    "TestMetadata",
    "TestSpec",
    "TestStep",
    "TestSuite",
    "VisualAssertionConfig",
    "VisualComparisonMode",
    # Parser & Transformer
    "DSLParser",
    "DSLValidator",
    "StepTransformer",
]
