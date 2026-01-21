"""
Discovery pipeline orchestrator with live viewport streaming.

Coordinates the complete auto-discovery process for services.
"""

from __future__ import annotations

import uuid
from typing import Any

import structlog

from autoqa.discovery.auth_detector import AuthDetector
from autoqa.discovery.feature_mapper import FeatureMapper
from autoqa.discovery.operation_builder import OperationBuilder
from autoqa.discovery.config_generator import ConfigGenerator

if TYPE_CHECKING:
    from owl_browser import Browser

logger = structlog.get_logger(__name__)


class DiscoveryOrchestrator:
    """
    Orchestrates the complete service discovery pipeline.

    Pipeline:
    1. Auth detection
    2. Login execution
    3. Feature mapping
    4. Operation building
    5. Configuration generation

    Supports live viewport streaming during discovery.
    """

    def __init__(self) -> None:
        """Initialize discovery orchestrator."""
        self.auth_detector = AuthDetector()
        self.feature_mapper = FeatureMapper()
        self.operation_builder = OperationBuilder()
        self.config_generator = ConfigGenerator()
        self._log = logger.bind(component="discovery_orchestrator")

    async def discover_service(
        self,
        service_id: str,
        url: str,
        credentials: dict[str, str],
        browser: Browser,
        context_id: str,
        websocket_handler: Any = None,
    ) -> dict[str, Any]:
        """
        Run full discovery pipeline.

        Args:
            service_id: Service identifier
            url: Service URL
            credentials: Login credentials
            browser: Owl-Browser instance
            context_id: Browser context ID
            websocket_handler: Optional WebSocket handler for live updates

        Returns:
            Complete service configuration
        """
        discovery_id = str(uuid.uuid4())

        self._log.info(
            "Starting service discovery",
            discovery_id=discovery_id,
            service_id=service_id,
            url=url,
        )

        try:
            # Step 1: Detect authentication mechanism
            await self._send_progress(
                websocket_handler,
                service_id,
                "scanning",
                10,
                "Detecting authentication mechanism...",
            )

            auth_config = await self.auth_detector.detect_auth_mechanism(
                browser,
                context_id,
                url,
            )

            auth_config["url"] = url

            self._log.info(
                "Auth mechanism detected",
                auth_type=auth_config.get("type"),
            )

            # Step 2: Execute login
            await self._send_progress(
                websocket_handler,
                service_id,
                "scanning",
                30,
                "Executing login...",
            )

            # Import here to avoid circular dependency
            from autoqa.auth.form_filler import FormFiller

            form_filler = FormFiller()

            # Detect and fill login form
            login_success = await form_filler.complete_login_flow(
                browser,
                context_id,
                credentials,
            )

            if not login_success:
                raise Exception("Login failed - cannot continue discovery")

            self._log.info("Login successful")

            # Step 3: Detect service capabilities
            await self._send_progress(
                websocket_handler,
                service_id,
                "scanning",
                50,
                "Discovering service capabilities...",
            )

            features = await self.feature_mapper.detect_service_capabilities(
                browser,
                context_id,
            )

            self._log.info(
                "Capabilities detected",
                primary_operation=features.get("primary_operation"),
                models_count=len(features.get("available_models", [])),
            )

            # Step 4: Build operations
            await self._send_progress(
                websocket_handler,
                service_id,
                "scanning",
                70,
                "Building operation definitions...",
            )

            operations = []

            # Build chat completion operation
            chat_op = await self.operation_builder.build_chat_completion_operation(
                service_id,
                features,
                auth_config,
            )
            operations.append(chat_op)

            # Build additional operations if applicable
            if features.get("primary_operation") == "image_generation":
                img_op = await self.operation_builder.build_image_generation_operation(
                    service_id,
                    features,
                )
                if img_op:
                    operations.append(img_op)

            self._log.info(
                "Operations built",
                count=len(operations),
            )

            # Step 5: Generate configuration
            await self._send_progress(
                websocket_handler,
                service_id,
                "scanning",
                90,
                "Generating service configuration...",
            )

            config = await self.config_generator.generate_service_config(
                service_id,
                url,
                auth_config,
                features,
                operations,
            )

            # Step 6: Complete
            await self._send_progress(
                websocket_handler,
                service_id,
                "complete",
                100,
                "Discovery complete!",
            )

            self._log.info(
                "Discovery completed successfully",
                discovery_id=discovery_id,
                service_id=service_id,
            )

            return config

        except Exception as e:
            self._log.error(
                "Discovery failed",
                discovery_id=discovery_id,
                service_id=service_id,
                error=str(e),
            )

            await self._send_progress(
                websocket_handler,
                service_id,
                "failed",
                0,
                f"Discovery failed: {str(e)}",
            )

            raise

    async def _send_progress(
        self,
        websocket_handler: Any,
        service_id: str,
        status: str,
        progress: int,
        message: str,
    ) -> None:
        """Send progress update via WebSocket if handler available."""
        if websocket_handler:
            try:
                await websocket_handler.send_discovery_update(
                    service_id,
                    status,
                    progress,
                    message,
                )
            except Exception as e:
                self._log.warning(
                    "Failed to send progress update",
                    error=str(e),
                )
