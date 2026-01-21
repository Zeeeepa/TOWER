"""
Operation execution engine for discovered services.

Executes operations using Owl-Browser commands with response detection.
"""

from __future__ import annotations

import asyncio
import time
import uuid
from typing import Any

import structlog

if TYPE_CHECKING:
    from owl_browser import Browser

logger = structlog.get_logger(__name__)


class OperationRunner:
    """
    Executes discovered operations on services.

    Key Owl-Browser commands used:
    - browser_is_enabled: CRITICAL for detecting response completion
    - browser_ai_extract: AI-powered response extraction
    - browser_extract_text: Fallback text extraction
    - browser_upload_file: File upload support
    """

    def __init__(self) -> None:
        """Initialize operation runner."""
        self._log = logger.bind(component="operation_runner")

    async def execute_operation(
        self,
        service_id: str,
        service_config: dict[str, Any],
        operation_id: str,
        parameters: dict[str, Any],
        browser: Browser,
        context_id: str,
        websocket_handler: Any = None,
    ) -> str:
        """
        Execute an operation on a service.

        Args:
            service_id: Service identifier
            service_config: Service configuration from discovery
            operation_id: Operation to execute (e.g., "chat_completion")
            parameters: Operation parameters (e.g., {"message": "Hello"})
            browser: Owl-Browser instance
            context_id: Browser context ID
            websocket_handler: Optional WebSocket for progress updates

        Returns:
            Operation result/response
        """
        execution_id = str(uuid.uuid4())
        start_time = time.time()

        self._log.info(
            "Starting operation execution",
            execution_id=execution_id,
            service_id=service_id,
            operation_id=operation_id,
        )

        try:
            # Get operation definition
            operations = service_config.get("operations", [])
            operation = next((op for op in operations if op["id"] == operation_id), None)

            if not operation:
                raise ValueError(f"Operation {operation_id} not found")

            # Execute each step
            for step in operation.get("execution_steps", []):
                await self._execute_step(
                    step,
                    parameters,
                    browser,
                    context_id,
                    websocket_handler,
                )

            # Extract and return result
            result = await self._extract_result(
                operation,
                browser,
                context_id,
            )

            duration_ms = int((time.time() - start_time) * 1000)

            self._log.info(
                "Operation executed successfully",
                execution_id=execution_id,
                duration_ms=duration_ms,
            )

            return result

        except Exception as e:
            self._log.error(
                "Operation execution failed",
                execution_id=execution_id,
                error=str(e),
            )
            raise

    async def _execute_step(
        self,
        step: dict[str, Any],
        parameters: dict[str, Any],
        browser: Browser,
        context_id: str,
        websocket_handler: Any = None,
    ) -> None:
        """Execute a single operation step."""
        action = step.get("action")
        description = step.get("description", "")

        self._log.debug("Executing step", action=action, description=description)

        # Send progress update
        if websocket_handler:
            await websocket_handler.send_execution_log(
                None,  # service_id
                "info",
                f"Executing: {description}",
            )

        # Handle different action types
        if action == "navigate":
            await browser.browser_navigate({
                "context_id": context_id,
                "url": step["url"],
            })

        elif action == "wait_for_selector":
            await browser.browser_wait_for_selector({
                "context_id": context_id,
                "selector": step["selector"],
                "timeout": step.get("timeout", 30000),
            })

        elif action == "type":
            # Replace placeholders with actual values
            value = step["value"]
            if value.startswith("{{") and value.endswith("}}"):
                param_name = value[2:-2]
                value = parameters.get(param_name, value)

            await browser.browser_type({
                "context_id": context_id,
                "selector": step["selector"],
                "text": value,
            })

        elif action == "click":
            await browser.browser_click({
                "context_id": context_id,
                "selector": step["selector"],
            })

        elif action == "select_option":
            if step.get("optional") and parameters.get("model") is None:
                self._log.debug("Skipping optional model selection")
                return

            value = step["value"]
            if value.startswith("{{") and value.endswith("}}"):
                param_name = value[2:-2]
                value = parameters.get(param_name)

            if value:
                await browser.browser_select_option({
                    "context_id": context_id,
                    "selector": step["selector"],
                    "values": [value],
                })

        elif action == "upload_file_if_present":
            file_path = parameters.get("file")
            if file_path:
                await browser.browser_upload_file({
                    "context_id": context_id,
                    "files": [file_path],
                })

        elif action == "wait_for_response_complete":
            # CRITICAL: Use browser_is_enabled to detect when response is ready
            await self._wait_for_response_complete(
                browser,
                context_id,
                step["submit_selector"],
                step.get("timeout", 60000),
            )

        elif action == "extract_response":
            # This is handled separately in _extract_result
            pass

        else:
            self._log.warning("Unknown step action", action=action)

    async def _wait_for_response_complete(
        self,
        browser: Browser,
        context_id: str,
        submit_selector: str,
        timeout: int,
    ) -> None:
        """
        Wait for response to complete using browser_is_enabled.

        CRITICAL: Most chat interfaces disable the "Send" button while generating.
        When it re-enables, the response is complete.

        Args:
            browser: Owl-Browser instance
            context_id: Browser context ID
            submit_selector: Selector for submit button
            timeout: Maximum wait time in milliseconds
        """
        start = time.time()
        timeout_seconds = timeout / 1000

        self._log.info(
            "Waiting for response completion",
            submit_selector=submit_selector,
            timeout_seconds=timeout_seconds,
        )

        while (time.time() - start) < timeout_seconds:
            # Check if submit button is enabled
            result = await browser.browser_is_enabled({
                "context_id": context_id,
                "selector": submit_selector,
            })

            if result.get("enabled"):
                self._log.info("Response completed - button re-enabled")
                return

            # Wait before checking again
            await asyncio.sleep(0.5)

        raise TimeoutError("Response completion timeout")

    async def _extract_result(
        self,
        operation: dict[str, Any],
        browser: Browser,
        context_id: str,
    ) -> str:
        """
        Extract operation result using AI or text extraction.

        Args:
            operation: Operation definition
            browser: Owl-Browser instance
            context_id: Browser context ID

        Returns:
            Extracted result text
        """
        extraction_config = operation.get("response_extraction", {})
        selector = extraction_config.get("selector")

        if not selector:
            # Fallback to ui_selectors
            ui_selectors = operation.get("ui_selectors", {})
            selector = ui_selectors.get("output")

        if not selector:
            raise ValueError("No output selector configured")

        # Try AI extraction first
        try:
            result = await browser.browser_ai_extract({
                "context_id": context_id,
                "selector": selector,
                "prompt": "Extract the AI assistant's response text completely",
            })

            if result and result.get("content"):
                self._log.info("Result extracted via AI", content_length=len(result["content"]))
                return result["content"]

        except Exception as e:
            self._log.debug("AI extraction failed, trying text extraction", error=str(e))

        # Fallback to text extraction
        try:
            result = await browser.browser_extract_text({
                "context_id": context_id,
                "selector": selector,
            })

            if result and result.get("text"):
                self._log.info("Result extracted via text", text_length=len(result["text"]))
                return result["text"]

        except Exception as e:
            self._log.error("Text extraction failed", error=str(e))

        raise Exception("Failed to extract result from service")
