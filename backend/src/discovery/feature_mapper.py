"""
Service capability detection using Owl-Browser AI commands.

Detects features, models, and operational capabilities of web services.
"""

from __future__ import annotations

from typing import Any

import structlog

if TYPE_CHECKING:
    from owl_browser import Browser

logger = structlog.get_logger(__name__)


class FeatureMapper:
    """
    Maps web service features using AI-powered analysis.

    Uses Owl-Browser AI commands:
    - browser_ai_analyze: Comprehensive page analysis
    - browser_query_page: Query specific page elements
    - browser_screenshot: Visual analysis
    - browser_get_html: DOM structure extraction
    - browser_extract_text: Text content extraction
    """

    def __init__(self) -> None:
        """Initialize feature mapper."""
        self._log = logger.bind(component="feature_mapper")

    async def detect_service_capabilities(
        self,
        browser: Browser,
        context_id: str,
    ) -> dict[str, Any]:
        """
        Detect what the service can do.

        Args:
            browser: Owl-Browser instance
            context_id: Browser context ID

        Returns:
            Dictionary with detected capabilities and UI selectors
        """
        try:
            # Step 1: AI-powered comprehensive analysis
            ai_analysis = await browser.browser_ai_analyze({
                "context_id": context_id,
                "question": """
                Analyze this web interface and identify:
                1. What is the primary service type? (chat, image generation, search, code execution, etc.)
                2. What AI models or engines are available?
                3. What features are configurable? (temperature, max tokens, web browsing, etc.)
                4. Where is the main input area for user prompts?
                5. Where do responses or outputs appear?
                6. Are there file upload capabilities?
                7. What buttons or controls exist for submitting requests?

                Be very specific about element locations and selectors.
                """,
            })

            self._log.info("AI analysis completed", analysis_summary=ai_analysis.get("summary")[:200] if ai_analysis else None)

            # Step 2: Query for specific UI elements
            input_elements = await browser.browser_query_page({
                "context_id": context_id,
                "question": "Find the main input area (textarea, input field) where users type their prompts or questions",
            })

            submit_button = await browser.browser_query_page({
                "context_id": context_id,
                "question": "Find the submit, send, generate, or run button that executes the request",
            })

            output_area = await browser.browser_query_page({
                "context_id": context_id,
                "question": "Find where the AI response, result, or output appears after execution",
            })

            # Step 3: Detect available models
            models = await self._extract_models(browser, context_id)

            # Step 4: Detect configurable features
            features = await self._extract_features(browser, context_id)

            # Step 5: Check for file upload
            has_file_upload = await self._check_file_upload(browser, context_id)

            # Step 6: Compile capabilities
            capabilities = {
                "primary_operation": self._extract_primary_operation(ai_analysis),
                "available_models": models,
                "features": features,
                "input_selector": input_elements.get("selector"),
                "submit_selector": submit_button.get("selector"),
                "output_selector": output_area.get("selector"),
                "has_file_upload": has_file_upload,
                "ai_analysis": ai_analysis,
            }

            self._log.info(
                "Service capabilities detected",
                primary_operation=capabilities["primary_operation"],
                models_count=len(models),
                features_count=len(features),
            )

            return capabilities

        except Exception as e:
            self._log.error("Feature detection failed", error=str(e))
            raise

    async def _extract_models(
        self,
        browser: Browser,
        context_id: str,
    ) -> list[str]:
        """Extract available model options."""
        try:
            models_result = await browser.browser_query_page({
                "context_id": context_id,
                "question": "List all available AI models, engines, or options shown in dropdowns, selectors, or options",
            })

            models = models_result.get("models", [])

            if not models:
                # Try alternative query
                models_result = await browser.browser_query_page({
                    "context_id": context_id,
                    "question": "What models can be selected? (e.g., GPT-4, Claude, Gemini, etc.)",
                })
                models = models_result.get("models", [])

            self._log.debug("Models extracted", count=len(models), models=models[:5])
            return models

        except Exception as e:
            self._log.error("Model extraction failed", error=str(e))
            return []

    async def _extract_features(
        self,
        browser: Browser,
        context_id: str,
    ) -> list[dict[str, Any]]:
        """Extract configurable features."""
        try:
            features_result = await browser.browser_query_page({
                "context_id": context_id,
                "question": "Find all toggles, sliders, dropdowns, checkboxes, or other controls for configuring behavior",
            })

            features = features_result.get("features", [])

            self._log.debug("Features extracted", count=len(features))
            return features

        except Exception as e:
            self._log.error("Feature extraction failed", error=str(e))
            return []

    async def _check_file_upload(
        self,
        browser: Browser,
        context_id: str,
    ) -> bool:
        """Check if service supports file uploads."""
        try:
            upload_button = await browser.browser_find_element({
                "context_id": context_id,
                "description": "file upload button, attachment button, or upload icon",
            })

            has_upload = upload_button.get("found", False)

            self._log.debug("File upload check", has_upload=has_upload)
            return has_upload

        except Exception as e:
            self._log.error("File upload check failed", error=str(e))
            return False

    def _extract_primary_operation(self, ai_analysis: dict[str, Any] | None) -> str:
        """Extract primary operation type from AI analysis."""
        if not ai_analysis:
            return "generic"

        analysis_text = str(ai_analysis).lower()

        if "chat" in analysis_text:
            return "chat"
        elif "image generation" in analysis_text or "generate image" in analysis_text:
            return "image_generation"
        elif "code execution" in analysis_text or "run code" in analysis_text:
            return "code_execution"
        elif "search" in analysis_text:
            return "search"
        elif "embedding" in analysis_text:
            return "embedding"
        elif "completion" in analysis_text:
            return "completion"
        else:
            return "generic"
