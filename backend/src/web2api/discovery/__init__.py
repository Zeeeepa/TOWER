"""
Service discovery and capability detection for Web2API.

Automatically discovers service features, operations, and configuration.
"""

from web2api.discovery.auth_detector import AuthDetector
from web2api.discovery.feature_mapper import FeatureMapper
from web2api.discovery.operation_builder import OperationBuilder
from web2api.discovery.config_generator import ConfigGenerator
from web2api.discovery.orchestrator import DiscoveryOrchestrator

__all__ = [
    "AuthDetector",
    "FeatureMapper",
    "OperationBuilder",
    "ConfigGenerator",
    "DiscoveryOrchestrator",
]
