"""
Dynamic operation generation from discovered features.

Builds executable operation definitions from detected capabilities.
"""

from __future__ import annotations

import uuid
from typing import Any

import structlog

logger = structlog.get_logger(__name__)


class OperationBuilder:
    """
    Builds executable operations from discovered features.

    Creates operation definitions with:
    - Parameters schema
    - Execution steps
    - UI selectors
    - Response extraction config
    """

    def __init__(self) -> None:
        """Initialize operation builder."""
        self._log = logger.bind(component="operation_builder")

    async def build_chat_completion_operation(
        self,
        service_id: str,
        features: dict[str, Any],
        auth_config: dict[str, Any],
    ) -> dict[str, Any]:
        """
        Build chat completion operation from detected features.

        This is the primary operation for most AI chat services.

        Args:
            service_id: Service identifier
            features: Detected capabilities from FeatureMapper
            auth_config: Auth configuration

        Returns:
            Operation definition
        """
        execution_steps = []

        # Step 1: Navigate to service (if needed)
        execution_steps.append({
            "action": "navigate",
            "url": auth_config.get("url", ""),
            "description": "Navigate to service",
        })

        # Step 2: Wait for input field
        execution_steps.append({
            "action": "wait_for_selector",
            "selector": features.get("input_selector"),
            "timeout": 5000,
            "description": "Wait for input field to be available",
        })

        # Step 3: Type user message
        execution_steps.append({
            "action": "type",
            "selector": features.get("input_selector"),
            "value": "{{message}}",
            "description": "Type user message into input field",
        })

        # Step 4: Select model (if applicable)
        if features.get("available_models"):
            execution_steps.append({
                "action": "select_option",
                "selector": features.get("model_selector"),
                "value": "{{model}}",
                "description": "Select AI model",
                "optional": True,
            })

        # Step 5: Upload file if present
        if features.get("has_file_upload"):
            execution_steps.append({
                "action": "upload_file_if_present",
                "description": "Upload file if provided in request",
                "optional": True,
            })

        # Step 6: Click submit button
        execution_steps.append({
            "action": "click",
            "selector": features.get("submit_selector"),
            "description": "Submit request",
        })

        # Step 7: Wait for response using browser_is_enabled
        # CRITICAL: This is the key command that detects when response is complete
        execution_steps.append({
            "action": "wait_for_response_complete",
            "submit_selector": features.get("submit_selector"),
            "timeout": 60000,
            "description": "Wait for response to complete (monitors submit button state)",
            "implementation": "browser_is_enabled polling",
        })

        # Step 8: Extract response
        execution_steps.append({
            "action": "extract_response",
            "selector": features.get("output_selector"),
            "description": "Extract AI assistant's response",
            "implementation": "browser_ai_extract with browser_extract_text fallback",
        })

        # Build operation definition
        operation = {
            "id": "chat_completion",
            "name": "Chat Completion",
            "description": "Send a message and get an AI response",
            "parameters": {
                "message": {
                    "type": "string",
                    "required": True,
                    "description": "User message to send",
                },
                "model": {
                    "type": "string",
                    "required": False,
                    "enum": features.get("available_models", []),
                    "description": "AI model to use",
                },
            },
            "execution_steps": execution_steps,
            "response_extraction": {
                "method": "ai_extract",
                "fallback": "text_extract",
                "selector": features.get("output_selector"),
            },
            # DSL features for advanced integration
            "dsl_config": {
                "enable_assertions": True,
                "enable_self_healing": True,
                "enable_variable_extraction": True,
                "enable_conditional_logic": True,
            },
        }
        
        # Add advanced features based on detected capabilities
        if features.get("features"):
            for feature in features["features"]:
                if feature["type"] == "model_selector":
                    # Add model selection step
                    model_step = {
                        "action": "select_option",
                        "selector": feature["selector"],
                        "value": "{{model}}",
                        "description": f"Select model: {feature.get('label', 'model')}",
                        "optional": True,
                    }
                    operation["execution_steps"].insert(3, model_step)  # Insert after type step
                elif feature["type"] == "toggle":
                    # Add toggle control step
                    toggle_step = {
                        "action": "click",
                        "selector": feature["selector"],
                        "description": f"Toggle feature: {feature.get('label', 'setting')}"
                    }
                    # Add conditional logic based on parameter
                    toggle_step["condition"] = f"{{params.{feature.get('label', 'setting').lower().replace(' ', '_')}}}"  # Only click if parameter is true
                    operation["execution_steps"].append(toggle_step)

        self._log.info(
            "Chat completion operation built",
            steps_count=len(execution_steps),
        )

        return operation

    async def build_image_generation_operation(
        self,
        service_id: str,
        features: dict[str, Any],
    ) -> dict[str, Any] | None:
        """
        Build image generation operation (if supported).

        Args:
            service_id: Service identifier
            features: Detected capabilities

        Returns:
            Operation definition or None if not supported
        """
        if features.get("primary_operation") != "image_generation":
            return None

        # Similar structure to chat completion but with image-specific parameters
        execution_steps = [
            {
                "action": "navigate",
                "url": features.get("url"),
            },
            {
                "action": "type",
                "selector": features.get("prompt_selector"),
                "value": "{{prompt}}",
            },
            {
                "action": "click",
                "selector": features.get("generate_selector"),
            },
            {
                "action": "wait_for_response_complete",
                "timeout": 120000,  # Image generation takes longer
            },
            {
                "action": "extract_image",
                "selector": features.get("output_selector"),
            },
        ]

        return {
            "id": "image_generation",
            "name": "Image Generation",
            "description": "Generate images from text prompts",
            "parameters": {
                "prompt": {
                    "type": "string",
                    "required": True,
                },
                "size": {
                    "type": "string",
                    "required": False,
                    "enum": ["256x256", "512x512", "1024x1024"],
                },
            },
            "execution_steps": execution_steps,
        }
