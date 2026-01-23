"""
Service capability detection using Owl-Browser AI commands.

Detects features, models, and operational capabilities of web services.
Optimized for Web2API chat interface detection.
"""

from __future__ import annotations

from typing import Any, TYPE_CHECKING

import structlog

if TYPE_CHECKING:
    from owl_browser import Browser

logger = structlog.get_logger(__name__)


# Common selectors for chat interface detection
CHAT_INPUT_SELECTORS = [
    "textarea[placeholder*='message' i]",
    "textarea[placeholder*='ask' i]",
    "textarea[placeholder*='type' i]",
    "textarea[placeholder*='chat' i]",
    "textarea[placeholder*='send' i]",
    "textarea[data-testid*='input']",
    "textarea[aria-label*='message' i]",
    "div[contenteditable='true'][role='textbox']",
    "div[contenteditable='true'][aria-label*='message' i]",
    "input[type='text'][placeholder*='message' i]",
    "#prompt-textarea",
    "[data-testid='prompt-textarea']",
    ".chat-input textarea",
    ".message-input textarea",
    ".prompt-input",
]

SEND_BUTTON_SELECTORS = [
    "button[data-testid='send-button']",
    "button[aria-label*='send' i]",
    "button[aria-label*='submit' i]",
    "button:has(svg[data-icon='paper-plane'])",
    "button:has(svg[data-icon='send'])",
    "button[type='submit']",
    "[data-testid='send-message-button']",
    ".send-button",
    "#send-button",
]

OUTPUT_SELECTORS = [
    "[data-testid='conversation-turn']:last-child",
    ".message-content:last-child",
    ".assistant-message:last-child",
    ".chat-message:last-child",
    ".response-container",
    "[role='log'] > div:last-child",
    ".conversation-container > div:last-child",
    "#response-area",
    ".output-area",
]


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
                8. Identify any toggle switches, checkboxes, or radio buttons for feature control.
                9. Look for model selection dropdowns or tabs.
                10. Find any settings or configuration panels.

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

            # Step 3: Enhanced detection for advanced features
            models = await self._extract_models(browser, context_id)
            features = await self._extract_features(browser, context_id)
            
            # Enhanced feature detection for toggle switches and advanced controls
            additional_features = await self._detect_advanced_features(browser, context_id)
            features.extend(additional_features)

            # Step 4: Check for file upload
            has_file_upload = await self._check_file_upload(browser, context_id)

            # Step 5: Fallback mechanisms using vision models and AI reasoning for edge cases
            capabilities = await self._compile_capabilities_with_fallbacks(
                ai_analysis, input_elements, submit_button, output_area,
                models, features, has_file_upload, browser, context_id
            )

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
    
    async def _detect_advanced_features(
        self,
        browser: Browser,
        context_id: str,
    ) -> list[dict[str, Any]]:
        """Detect advanced features like toggle switches, checkboxes, etc."""
        try:
            # Query for toggle switches, checkboxes, and other controls
            toggle_elements = await browser.browser_query_page({
                "context_id": context_id,
                "question": "Find all toggle switches, checkboxes, and radio buttons that control features",
            })
            
            model_elements = await browser.browser_query_page({
                "context_id": context_id,
                "question": "Find model selection dropdowns, tabs, or radio buttons for choosing AI models",
            })
            
            config_elements = await browser.browser_query_page({
                "context_id": context_id,
                "question": "Find all configuration panels, settings buttons, or parameter controls",
            })
            
            features = []
            
            # Process toggle switches
            if toggle_elements and toggle_elements.get("elements"):
                for element in toggle_elements["elements"]:
                    features.append({
                        "type": "toggle",
                        "selector": element.get("selector"),
                        "description": element.get("description", "Toggle switch"),
                        "label": element.get("text", ""),
                    })
            
            # Process model selectors
            if model_elements and model_elements.get("elements"):
                for element in model_elements["elements"]:
                    features.append({
                        "type": "model_selector",
                        "selector": element.get("selector"),
                        "description": element.get("description", "Model selector"),
                        "label": element.get("text", ""),
                    })
            
            # Process configuration elements
            if config_elements and config_elements.get("elements"):
                for element in config_elements["elements"]:
                    features.append({
                        "type": "configuration",
                        "selector": element.get("selector"),
                        "description": element.get("description", "Configuration panel"),
                        "label": element.get("text", ""),
                    })
            
            self._log.debug("Advanced features detected", count=len(features))
            return features
        
        except Exception as e:
            self._log.error("Advanced feature detection failed", error=str(e))
            return []
    
    async def _compile_capabilities_with_fallbacks(
        self,
        ai_analysis: dict[str, Any],
        input_elements: dict[str, Any],
        submit_button: dict[str, Any],
        output_area: dict[str, Any],
        models: list[str],
        features: list[dict[str, Any]],
        has_file_upload: bool,
        browser: Browser,
        context_id: str,
    ) -> dict[str, Any]:
        """Compile capabilities with fallback mechanisms for edge cases."""
        # Primary operation
        primary_operation = self._extract_primary_operation(ai_analysis)
        
        # UI selectors with fallbacks
        input_selector = input_elements.get("selector")
        if not input_selector:
            # Fallback to AI detection
            ai_result = await browser.browser_find_element({
                "context_id": context_id,
                "description": "main text input field or textarea for user prompts",
            })
            if ai_result.get("found") and ai_result.get("selector"):
                input_selector = ai_result["selector"]
        
        submit_selector = submit_button.get("selector")
        if not submit_selector:
            # Fallback to AI detection
            ai_result = await browser.browser_find_element({
                "context_id": context_id,
                "description": "submit button or send button for executing requests",
            })
            if ai_result.get("found") and ai_result.get("selector"):
                submit_selector = ai_result["selector"]
        
        output_selector = output_area.get("selector")
        if not output_selector:
            # Fallback to AI detection
            ai_result = await browser.browser_find_element({
                "context_id": context_id,
                "description": "area where responses or outputs appear",
            })
            if ai_result.get("found") and ai_result.get("selector"):
                output_selector = ai_result["selector"]
        
        # Compile capabilities
        capabilities = {
            "primary_operation": primary_operation,
            "available_models": models,
            "features": features,
            "input_selector": input_selector,
            "submit_selector": submit_selector,
            "output_selector": output_selector,
            "has_file_upload": has_file_upload,
            "ai_analysis": ai_analysis,
        }
        
        return capabilities
    
    async def detect_chat_interface(
        self,
        browser: Browser,
        context_id: str,
    ) -> dict[str, str | None]:
        """
        Fast detection of chat interface elements.
        
        Uses predefined selector patterns instead of AI for speed.
        This is optimized for Web2API chat completion flow.
        
        Args:
            browser: Owl-Browser instance
            context_id: Browser context ID
            
        Returns:
            Dictionary with detected selectors (input, submit, output)
        """
        self._log.info("Starting fast chat interface detection")
        
        result = {
            "input_selector": None,
            "submit_selector": None,
            "output_selector": None,
            "stop_button_selector": None,
        }
        
        # Detect input field
        for selector in CHAT_INPUT_SELECTORS:
            try:
                visible = await browser.browser_is_visible({
                    "context_id": context_id,
                    "selector": selector,
                })
                if visible.get("visible"):
                    result["input_selector"] = selector
                    self._log.debug("Input field found", selector=selector)
                    break
            except Exception:
                continue
        
        # Fallback to AI detection for input
        if not result["input_selector"]:
            try:
                ai_result = await browser.browser_find_element({
                    "context_id": context_id,
                    "description": "main text input field or textarea for chat messages",
                })
                if ai_result.get("found") and ai_result.get("selector"):
                    result["input_selector"] = ai_result["selector"]
                    self._log.debug("Input field found via AI", selector=result["input_selector"])
            except Exception as e:
                self._log.debug("AI input detection failed", error=str(e))
        
        # Detect send button
        for selector in SEND_BUTTON_SELECTORS:
            try:
                visible = await browser.browser_is_visible({
                    "context_id": context_id,
                    "selector": selector,
                })
                if visible.get("visible"):
                    result["submit_selector"] = selector
                    self._log.debug("Send button found", selector=selector)
                    break
            except Exception:
                continue
        
        # Fallback to AI detection for send button
        if not result["submit_selector"]:
            try:
                ai_result = await browser.browser_find_element({
                    "context_id": context_id,
                    "description": "send button or submit button for chat messages",
                })
                if ai_result.get("found") and ai_result.get("selector"):
                    result["submit_selector"] = ai_result["selector"]
                    self._log.debug("Send button found via AI", selector=result["submit_selector"])
            except Exception as e:
                self._log.debug("AI send button detection failed", error=str(e))
        
        # Detect output area
        for selector in OUTPUT_SELECTORS:
            try:
                visible = await browser.browser_is_visible({
                    "context_id": context_id,
                    "selector": selector,
                })
                if visible.get("visible"):
                    result["output_selector"] = selector
                    self._log.debug("Output area found", selector=selector)
                    break
            except Exception:
                continue
        
        # Fallback to AI detection for output
        if not result["output_selector"]:
            try:
                ai_result = await browser.browser_find_element({
                    "context_id": context_id,
                    "description": "area where assistant or AI responses appear in the chat",
                })
                if ai_result.get("found") and ai_result.get("selector"):
                    result["output_selector"] = ai_result["selector"]
                    self._log.debug("Output area found via AI", selector=result["output_selector"])
            except Exception as e:
                self._log.debug("AI output detection failed", error=str(e))
        
        # Detect stop button (optional)
        stop_selectors = [
            "button[aria-label*='stop' i]",
            "button:has-text('Stop')",
            "[data-testid='stop-button']",
            ".stop-button",
        ]
        for selector in stop_selectors:
            try:
                exists = await browser.browser_query_selector({
                    "context_id": context_id,
                    "selector": selector,
                })
                if exists.get("found"):
                    result["stop_button_selector"] = selector
                    break
            except Exception:
                continue
        
        self._log.info(
            "Chat interface detection complete",
            input_found=result["input_selector"] is not None,
            submit_found=result["submit_selector"] is not None,
            output_found=result["output_selector"] is not None,
        )
        
        return result
