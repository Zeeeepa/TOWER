"""
Service discovery and capability detection for Web2API.

Automatically discovers service features, operations, and configuration.
"""

from autoqa.discovery.auth_detector import AuthDetector
from autoqa.discovery.feature_mapper import FeatureMapper
from autoqa.discovery.operation_builder import OperationBuilder
from autoqa.discovery.config_generator import ConfigGenerator
from autoqa.discovery.orchestrator import DiscoveryOrchestrator

__all__ = [
    "AuthDetector",
    "FeatureMapper",
    "OperationBuilder",
    "ConfigGenerator",
    "DiscoveryOrchestrator",
]
