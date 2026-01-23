"""
ChatExecutor - Core Web2API chat interface interaction engine.

Handles the complete flow:
1. Navigate to chat URL
2. Restore cookies (if available) or perform login
3. Type message into input field
4. Click send button
5. Track send button state to detect response completion
6. Extract and return response text
"""

from __future__ import annotations

import asyncio
import time
from dataclasses import dataclass
from typing import TYPE_CHECKING, Any

import structlog

if TYPE_CHECKING:
    from owl_browser import Browser

logger = structlog.get_logger(__name__)


@dataclass
class ChatUISelectors:
    """Selectors for chat interface elements."""
    
    input_selector: str
    """Selector for the text input/textarea field."""
    
    send_button_selector: str
    """Selector for the send/submit button."""
    
    output_selector: str
    """Selector for the response/output area."""
    
    # Optional selectors for enhanced detection
    new_chat_button: str | None = None
    """Selector for 'New Chat' button if available."""
    
    stop_button: str | None = None
    """Selector for 'Stop' button (appears during generation)."""
    
    message_container: str | None = None
    """Selector for individual message containers."""


@dataclass
class ChatConfig:
    """Configuration for chat execution."""
    
    url: str
    """Chat interface URL."""
    
    selectors: ChatUISelectors
    """UI element selectors."""
    
    timeout_ms: int = 120000
    """Maximum time to wait for response (ms)."""
    
    poll_interval_ms: int = 500
    """Interval between button state checks (ms)."""
    
    typing_delay_ms: int = 0
    """Delay between keystrokes (0 for instant)."""
    
    wait_after_type_ms: int = 100
    """Wait time after typing before clicking send."""
    
    wait_after_click_ms: int = 500
    """Wait time after clicking send before checking button state."""
    
    initial_button_check_delay_ms: int = 1000
    """Initial delay before starting button state checks."""


@dataclass 
class ChatExecutionResult:
    """Result of a chat execution."""
    
    success: bool
    """Whether execution succeeded."""
    
    response: str
    """The extracted response text."""
    
    duration_ms: int
    """Total execution time in milliseconds."""
    
    error: str | None = None
    """Error message if failed."""
    
    metadata: dict[str, Any] | None = None
    """Additional metadata about the execution."""


class ChatExecutor:
    """
    Executes chat interactions on web interfaces.
    
    This is the core engine for Web2API - it takes a message,
    interacts with a web chat interface, and returns the response.
    
    Key features:
    - Send button state tracking for response completion detection
    - Multiple fallback strategies for element detection
    - Support for different chat UI patterns
    - Robust error handling and retry logic
    """
    
    def __init__(self) -> None:
        """Initialize chat executor."""
        self._log = logger.bind(component="chat_executor")
    
    async def execute_chat(
        self,
        browser: Browser,
        context_id: str,
        config: ChatConfig,
        message: str,
        websocket_handler: Any = None,
    ) -> ChatExecutionResult:
        """
        Execute a chat message and return the response.
        
        Args:
            browser: Owl-Browser instance
            context_id: Browser context ID
            config: Chat configuration with selectors
            message: Message to send
            websocket_handler: Optional WebSocket for progress updates
            
        Returns:
            ChatExecutionResult with response or error
        """
        start_time = time.time()
        
        self._log.info(
            "Starting chat execution",
            url=config.url,
            message_length=len(message),
        )
        
        try:
            # Step 1: Navigate to chat URL if not already there
            await self._ensure_on_chat_page(browser, context_id, config.url)
            
            if websocket_handler:
                await websocket_handler.send_execution_log(
                    None, "info", "Navigated to chat interface"
                )
            
            # Step 2: Wait for input field to be ready
            await self._wait_for_input_ready(
                browser, context_id, config.selectors.input_selector
            )
            
            if websocket_handler:
                await websocket_handler.send_execution_log(
                    None, "info", "Input field ready"
                )
            
            # Step 3: Record initial button state for later comparison
            initial_button_state = await self._get_button_state(
                browser, context_id, config.selectors.send_button_selector
            )
            
            self._log.debug(
                "Initial button state recorded",
                enabled=initial_button_state.get("enabled"),
            )
            
            # Step 4: Clear any existing text and type the message
            await self._type_message(
                browser, context_id, config, message
            )
            
            if websocket_handler:
                await websocket_handler.send_execution_log(
                    None, "info", "Message typed"
                )
            
            # Step 5: Wait a moment for UI to update after typing
            await asyncio.sleep(config.wait_after_type_ms / 1000)
            
            # Step 6: Click the send button
            await self._click_send_button(
                browser, context_id, config.selectors.send_button_selector
            )
            
            if websocket_handler:
                await websocket_handler.send_execution_log(
                    None, "info", "Message sent, waiting for response..."
                )
            
            # Step 7: Wait for response to complete (button re-enabled)
            await self._wait_for_response_complete(
                browser, context_id, config, initial_button_state, websocket_handler
            )
            
            if websocket_handler:
                await websocket_handler.send_execution_log(
                    None, "info", "Response complete, extracting text..."
                )
            
            # Step 8: Extract the response
            response = await self._extract_response(
                browser, context_id, config.selectors.output_selector
            )
            
            duration_ms = int((time.time() - start_time) * 1000)
            
            self._log.info(
                "Chat execution successful",
                response_length=len(response),
                duration_ms=duration_ms,
            )
            
            return ChatExecutionResult(
                success=True,
                response=response,
                duration_ms=duration_ms,
                metadata={
                    "message_length": len(message),
                    "response_length": len(response),
                },
            )
            
        except Exception as e:
            duration_ms = int((time.time() - start_time) * 1000)
            error_msg = str(e)
            
            self._log.error(
                "Chat execution failed",
                error=error_msg,
                duration_ms=duration_ms,
            )
            
            return ChatExecutionResult(
                success=False,
                response="",
                duration_ms=duration_ms,
                error=error_msg,
            )
    
    async def _ensure_on_chat_page(
        self,
        browser: Browser,
        context_id: str,
        url: str,
    ) -> None:
        """Ensure browser is on the chat page."""
        try:
            # Get current URL
            result = await browser.browser_get_current_url({
                "context_id": context_id,
            })
            current_url = result.get("url", "")
            
            # If not on the chat page, navigate there
            if not current_url.startswith(url.rstrip("/")):
                self._log.info("Navigating to chat URL", url=url)
                await browser.browser_navigate({
                    "context_id": context_id,
                    "url": url,
                })
                
                # Wait for page to load
                await browser.browser_wait_for_load({
                    "context_id": context_id,
                    "state": "domcontentloaded",
                    "timeout": 30000,
                })
                
        except Exception as e:
            self._log.warning(
                "Error checking current URL, navigating anyway",
                error=str(e),
            )
            await browser.browser_navigate({
                "context_id": context_id,
                "url": url,
            })
    
    async def _wait_for_input_ready(
        self,
        browser: Browser,
        context_id: str,
        input_selector: str,
        timeout_ms: int = 30000,
    ) -> None:
        """Wait for input field to be visible and enabled."""
        await browser.browser_wait_for_selector({
            "context_id": context_id,
            "selector": input_selector,
            "state": "visible",
            "timeout": timeout_ms,
        })
        
        self._log.debug("Input field visible and ready", selector=input_selector)
    
    async def _get_button_state(
        self,
        browser: Browser,
        context_id: str,
        button_selector: str,
    ) -> dict[str, Any]:
        """Get the current state of the send button."""
        try:
            # Check if enabled
            enabled_result = await browser.browser_is_enabled({
                "context_id": context_id,
                "selector": button_selector,
            })
            
            # Try to get button text or aria-label for state comparison
            text_result = await browser.browser_extract_text({
                "context_id": context_id,
                "selector": button_selector,
            })
            
            return {
                "enabled": enabled_result.get("enabled", True),
                "text": text_result.get("text", ""),
                "timestamp": time.time(),
            }
            
        except Exception as e:
            self._log.debug("Error getting button state", error=str(e))
            return {"enabled": True, "text": "", "timestamp": time.time()}
    
    async def _type_message(
        self,
        browser: Browser,
        context_id: str,
        config: ChatConfig,
        message: str,
    ) -> None:
        """Clear input and type the message."""
        selector = config.selectors.input_selector
        
        # First, clear existing content
        try:
            await browser.browser_fill({
                "context_id": context_id,
                "selector": selector,
                "text": "",
            })
        except Exception:
            # Try triple-click to select all, then delete
            try:
                await browser.browser_click({
                    "context_id": context_id,
                    "selector": selector,
                    "click_count": 3,
                })
                await browser.browser_press({
                    "context_id": context_id,
                    "key": "Backspace",
                })
            except Exception as e:
                self._log.debug("Could not clear input field", error=str(e))
        
        # Type the message
        if config.typing_delay_ms > 0:
            # Type with delay (more human-like)
            await browser.browser_type({
                "context_id": context_id,
                "selector": selector,
                "text": message,
                "delay": config.typing_delay_ms,
            })
        else:
            # Instant fill (faster)
            await browser.browser_fill({
                "context_id": context_id,
                "selector": selector,
                "text": message,
            })
        
        self._log.debug("Message typed", message_length=len(message))
    
    async def _click_send_button(
        self,
        browser: Browser,
        context_id: str,
        button_selector: str,
    ) -> None:
        """Click the send button."""
        await browser.browser_click({
            "context_id": context_id,
            "selector": button_selector,
        })
        
        self._log.debug("Send button clicked", selector=button_selector)
    
    async def _wait_for_response_complete(
        self,
        browser: Browser,
        context_id: str,
        config: ChatConfig,
        initial_button_state: dict[str, Any],
        websocket_handler: Any = None,
    ) -> None:
        """
        Wait for the response to complete.
        
        Detection strategy:
        1. Wait for button to become disabled (indicates processing started)
        2. Wait for button to become enabled again (indicates processing complete)
        
        Some chat interfaces use different patterns:
        - Button disabled during generation
        - Button text changes (e.g., "Send" → "Stop" → "Send")
        - Stop button appears during generation
        """
        timeout_seconds = config.timeout_ms / 1000
        poll_interval = config.poll_interval_ms / 1000
        start_time = time.time()
        
        # Initial delay to let UI update after clicking send
        await asyncio.sleep(config.initial_button_check_delay_ms / 1000)
        
        button_selector = config.selectors.send_button_selector
        stop_button = config.selectors.stop_button
        
        # Track if we've seen the button disabled (processing started)
        seen_disabled = False
        last_log_time = start_time
        
        while (time.time() - start_time) < timeout_seconds:
            # Check stop button if configured (some UIs show stop instead of send)
            if stop_button:
                try:
                    stop_visible = await browser.browser_is_visible({
                        "context_id": context_id,
                        "selector": stop_button,
                    })
                    if stop_visible.get("visible"):
                        seen_disabled = True
                        if time.time() - last_log_time > 5:
                            self._log.debug("Stop button visible, still generating...")
                            last_log_time = time.time()
                        await asyncio.sleep(poll_interval)
                        continue
                except Exception:
                    pass
            
            # Check send button state
            current_state = await self._get_button_state(
                browser, context_id, button_selector
            )
            
            is_enabled = current_state.get("enabled", True)
            
            if not is_enabled:
                # Button is disabled - generation in progress
                seen_disabled = True
                if time.time() - last_log_time > 5:
                    self._log.debug("Button disabled, generation in progress...")
                    last_log_time = time.time()
            elif seen_disabled and is_enabled:
                # Button was disabled and is now enabled - generation complete!
                self._log.info("Response complete - button re-enabled")
                return
            elif not seen_disabled:
                # Button never got disabled - might be fast response or different UI
                # Give it more time on first iterations
                elapsed = time.time() - start_time
                if elapsed > 3:
                    # After 3 seconds, check if there's new content
                    # This handles fast responses or UIs that don't disable button
                    self._log.debug(
                        "Button never disabled, checking for content",
                        elapsed_seconds=int(elapsed),
                    )
            
            await asyncio.sleep(poll_interval)
            
            # Progress update every 10 seconds
            if websocket_handler and int(time.time() - start_time) % 10 == 0:
                await websocket_handler.send_execution_log(
                    None, "info", f"Still waiting for response... ({int(time.time() - start_time)}s)"
                )
        
        # Timeout reached
        if seen_disabled:
            self._log.warning("Timeout while waiting for response to complete")
            raise TimeoutError("Response generation timeout")
        else:
            # Button was never disabled - assume fast response
            self._log.info("Button never disabled, assuming fast response completed")
    
    async def _extract_response(
        self,
        browser: Browser,
        context_id: str,
        output_selector: str,
    ) -> str:
        """
        Extract the response text from the output area.
        
        Strategies:
        1. Try AI extraction for clean text
        2. Fallback to text extraction
        3. Handle multiple message containers (get last one)
        """
        # Small delay to ensure DOM is fully updated
        await asyncio.sleep(0.5)
        
        # Strategy 1: AI extraction for best quality
        try:
            result = await browser.browser_ai_extract({
                "context_id": context_id,
                "selector": output_selector,
                "prompt": "Extract the complete AI assistant response text. Return only the response content, no UI elements or metadata.",
            })
            
            if result and result.get("content"):
                content = result["content"].strip()
                if content:
                    self._log.debug("Response extracted via AI", length=len(content))
                    return content
                    
        except Exception as e:
            self._log.debug("AI extraction failed", error=str(e))
        
        # Strategy 2: Direct text extraction
        try:
            result = await browser.browser_extract_text({
                "context_id": context_id,
                "selector": output_selector,
            })
            
            if result and result.get("text"):
                text = result["text"].strip()
                if text:
                    self._log.debug("Response extracted via text", length=len(text))
                    return text
                    
        except Exception as e:
            self._log.debug("Text extraction failed", error=str(e))
        
        # Strategy 3: Get inner HTML and parse
        try:
            result = await browser.browser_get_html({
                "context_id": context_id,
                "selector": output_selector,
            })
            
            if result and result.get("html"):
                # Simple HTML to text conversion
                from html import unescape
                import re
                
                html = result["html"]
                # Remove script and style tags
                html = re.sub(r'<(script|style)[^>]*>.*?</\1>', '', html, flags=re.DOTALL | re.IGNORECASE)
                # Replace br and p with newlines
                html = re.sub(r'<br\s*/?>', '\n', html, flags=re.IGNORECASE)
                html = re.sub(r'</p>', '\n', html, flags=re.IGNORECASE)
                # Remove remaining tags
                html = re.sub(r'<[^>]+>', '', html)
                # Decode entities
                text = unescape(html).strip()
                
                if text:
                    self._log.debug("Response extracted via HTML", length=len(text))
                    return text
                    
        except Exception as e:
            self._log.debug("HTML extraction failed", error=str(e))
        
        raise Exception("Failed to extract response from chat interface")


class ChatUIDetector:
    """
    Auto-detects chat UI elements on a web page.
    
    Used during service discovery to find:
    - Input field (textarea, input)
    - Send button
    - Output/response area
    """
    
    def __init__(self) -> None:
        """Initialize UI detector."""
        self._log = logger.bind(component="chat_ui_detector")
    
    async def detect_chat_ui(
        self,
        browser: Browser,
        context_id: str,
    ) -> ChatUISelectors | None:
        """
        Auto-detect chat UI elements on the current page.
        
        Args:
            browser: Owl-Browser instance
            context_id: Browser context ID
            
        Returns:
            ChatUISelectors if detected, None otherwise
        """
        self._log.info("Starting chat UI detection")
        
        try:
            # Detect input field
            input_selector = await self._detect_input_field(browser, context_id)
            
            if not input_selector:
                self._log.warning("Could not detect input field")
                return None
            
            # Detect send button
            send_button = await self._detect_send_button(browser, context_id)
            
            if not send_button:
                self._log.warning("Could not detect send button")
                return None
            
            # Detect output area
            output_selector = await self._detect_output_area(browser, context_id)
            
            if not output_selector:
                self._log.warning("Could not detect output area")
                return None
            
            # Detect optional elements
            stop_button = await self._detect_stop_button(browser, context_id)
            new_chat = await self._detect_new_chat_button(browser, context_id)
            
            selectors = ChatUISelectors(
                input_selector=input_selector,
                send_button_selector=send_button,
                output_selector=output_selector,
                stop_button=stop_button,
                new_chat_button=new_chat,
            )
            
            self._log.info(
                "Chat UI detected",
                input=input_selector,
                send=send_button,
                output=output_selector,
            )
            
            return selectors
            
        except Exception as e:
            self._log.error("Chat UI detection failed", error=str(e))
            return None
    
    async def _detect_input_field(
        self,
        browser: Browser,
        context_id: str,
    ) -> str | None:
        """Detect the main input field."""
        # Common selectors for chat inputs
        candidates = [
            "textarea[placeholder*='message' i]",
            "textarea[placeholder*='ask' i]",
            "textarea[placeholder*='type' i]",
            "textarea[placeholder*='chat' i]",
            "textarea[data-testid*='input']",
            "textarea[aria-label*='message' i]",
            "div[contenteditable='true'][role='textbox']",
            "div[contenteditable='true'][aria-label*='message' i]",
            "input[type='text'][placeholder*='message' i]",
            "#prompt-textarea",
            "[data-testid='prompt-textarea']",
            ".chat-input textarea",
            ".message-input textarea",
        ]
        
        for selector in candidates:
            try:
                result = await browser.browser_is_visible({
                    "context_id": context_id,
                    "selector": selector,
                })
                if result.get("visible"):
                    self._log.debug("Input field found", selector=selector)
                    return selector
            except Exception:
                continue
        
        # Try AI detection as fallback
        try:
            result = await browser.browser_find_element({
                "context_id": context_id,
                "description": "The main text input field or textarea where users type their messages or prompts",
            })
            if result.get("found") and result.get("selector"):
                return result["selector"]
        except Exception as e:
            self._log.debug("AI input detection failed", error=str(e))
        
        return None
    
    async def _detect_send_button(
        self,
        browser: Browser,
        context_id: str,
    ) -> str | None:
        """Detect the send/submit button."""
        candidates = [
            "button[data-testid='send-button']",
            "button[aria-label*='send' i]",
            "button[aria-label*='submit' i]",
            "button:has(svg[data-icon='paper-plane'])",
            "button:has(svg[data-icon='send'])",
            "button[type='submit']",
            "button:has-text('Send')",
            "button:has-text('Submit')",
            "[data-testid='send-message-button']",
            ".send-button",
            "#send-button",
        ]
        
        for selector in candidates:
            try:
                result = await browser.browser_is_visible({
                    "context_id": context_id,
                    "selector": selector,
                })
                if result.get("visible"):
                    self._log.debug("Send button found", selector=selector)
                    return selector
            except Exception:
                continue
        
        # Try AI detection
        try:
            result = await browser.browser_find_element({
                "context_id": context_id,
                "description": "The send or submit button used to send chat messages",
            })
            if result.get("found") and result.get("selector"):
                return result["selector"]
        except Exception as e:
            self._log.debug("AI send button detection failed", error=str(e))
        
        return None
    
    async def _detect_output_area(
        self,
        browser: Browser,
        context_id: str,
    ) -> str | None:
        """Detect the output/response area."""
        candidates = [
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
        
        for selector in candidates:
            try:
                result = await browser.browser_is_visible({
                    "context_id": context_id,
                    "selector": selector,
                })
                if result.get("visible"):
                    self._log.debug("Output area found", selector=selector)
                    return selector
            except Exception:
                continue
        
        # Try AI detection
        try:
            result = await browser.browser_find_element({
                "context_id": context_id,
                "description": "The area where AI assistant responses appear, typically the last message in the conversation",
            })
            if result.get("found") and result.get("selector"):
                return result["selector"]
        except Exception as e:
            self._log.debug("AI output detection failed", error=str(e))
        
        return None
    
    async def _detect_stop_button(
        self,
        browser: Browser,
        context_id: str,
    ) -> str | None:
        """Detect the stop generation button (optional)."""
        candidates = [
            "button[aria-label*='stop' i]",
            "button:has-text('Stop')",
            "[data-testid='stop-button']",
            ".stop-button",
        ]
        
        for selector in candidates:
            try:
                # Just check if it exists, doesn't need to be visible
                result = await browser.browser_query_selector({
                    "context_id": context_id,
                    "selector": selector,
                })
                if result.get("found"):
                    return selector
            except Exception:
                continue
        
        return None
    
    async def _detect_new_chat_button(
        self,
        browser: Browser,
        context_id: str,
    ) -> str | None:
        """Detect new chat button (optional)."""
        candidates = [
            "button[aria-label*='new chat' i]",
            "button:has-text('New chat')",
            "button:has-text('New Chat')",
            "[data-testid='new-chat-button']",
            "a[href='/']",
        ]
        
        for selector in candidates:
            try:
                result = await browser.browser_is_visible({
                    "context_id": context_id,
                    "selector": selector,
                })
                if result.get("visible"):
                    return selector
            except Exception:
                continue
        
        return None
