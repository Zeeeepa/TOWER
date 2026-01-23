"""
Service configuration generation.

Synthesizes discovery results into service configuration JSON.
"""

from __future__ import annotations

from datetime import UTC, datetime
from typing import Any

import structlog

logger = structlog.get_logger(__name__)


class ConfigGenerator:
    """Generates service configuration from discovery results."""

    def __init__(self) -> None:
        """Initialize config generator."""
        self._log = logger.bind(component="config_generator")

    async def generate_service_config(
        self,
        service_id: str,
        url: str,
        auth_config: dict[str, Any],
        features: dict[str, Any],
        operations: list[dict[str, Any]],
    ) -> dict[str, Any]:
        """
        Generate complete service configuration.

        Args:
            service_id: Service identifier
            url: Service URL
            auth_config: Auth configuration
            features: Detected capabilities
            operations: List of operation definitions

        Returns:
            Complete service configuration JSON
        """
        service_name = self._extract_service_name(url)
        service_type = self._determine_service_type(features)

        config = {
            "service_id": service_id,
            "name": service_name,
            "url": url,
            "type": service_type,
            "auth": {
                "type": auth_config.get("type", "unknown"),
                "config": auth_config,
            },
            "capabilities": {
                "primary_operation": features.get("primary_operation", "generic"),
                "available_models": features.get("available_models", []),
                "features": features.get("features", []),
                "has_file_upload": features.get("has_file_upload", False),
            },
            "operations": operations,
            "ui_selectors": {
                "input": features.get("input_selector"),
                "submit": features.get("submit_selector"),
                "output": features.get("output_selector"),
            },
            "discovery_metadata": {
                "discovered_at": datetime.now(UTC).isoformat(),
                "version": "1.0",
                "web2api_version": "2.0",
            },
        }

        self._log.info(
            "Service configuration generated",
            service_id=service_id,
            name=service_name,
            type=service_type,
            operations_count=len(operations),
        )

        return config

    def _extract_service_name(self, url: str) -> str:
        """Extract service name from URL."""
        try:
            from urllib.parse import urlparse

            parsed = urlparse(url)
            domain = parsed.netloc

            # Remove www. and TLD
            name = domain.replace("www.", "").split(".")[0]

            # Capitalize
            return name.capitalize()
        except Exception:
            return "Unknown Service"

    def _determine_service_type(self, features: dict[str, Any]) -> str:
        """Determine service type from features."""
        primary = features.get("primary_operation", "").lower()

        type_mapping = {
            "chat": "chat",
            "image_generation": "image_generation",
            "code_execution": "code_execution",
            "search": "search",
            "embedding": "embedding",
            "completion": "completion",
        }

        return type_mapping.get(primary, "generic")
