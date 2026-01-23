"""
Owl Browser SDK v2 Exceptions.

Custom exception classes for better error handling and reporting.
Provides detailed error information for debugging and recovery.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum
from typing import Any


class ActionStatus(StrEnum):
    """Action status codes returned by the browser."""

    # Success
    OK = "ok"

    # Browser/context errors
    BROWSER_NOT_FOUND = "browser_not_found"
    BROWSER_NOT_READY = "browser_not_ready"
    CONTEXT_NOT_FOUND = "context_not_found"

    # Navigation errors
    NAVIGATION_FAILED = "navigation_failed"
    NAVIGATION_TIMEOUT = "navigation_timeout"
    PAGE_LOAD_ERROR = "page_load_error"
    REDIRECT_DETECTED = "redirect_detected"
    CAPTCHA_DETECTED = "captcha_detected"
    FIREWALL_DETECTED = "firewall_detected"

    # Element errors
    ELEMENT_NOT_FOUND = "element_not_found"
    ELEMENT_NOT_VISIBLE = "element_not_visible"
    ELEMENT_NOT_INTERACTABLE = "element_not_interactable"
    ELEMENT_STALE = "element_stale"
    MULTIPLE_ELEMENTS = "multiple_elements"

    # Action execution errors
    CLICK_FAILED = "click_failed"
    CLICK_INTERCEPTED = "click_intercepted"
    TYPE_FAILED = "type_failed"
    TYPE_PARTIAL = "type_partial"
    SCROLL_FAILED = "scroll_failed"
    FOCUS_FAILED = "focus_failed"
    BLUR_FAILED = "blur_failed"
    CLEAR_FAILED = "clear_failed"
    PICK_FAILED = "pick_failed"
    OPTION_NOT_FOUND = "option_not_found"
    UPLOAD_FAILED = "upload_failed"
    FRAME_SWITCH_FAILED = "frame_switch_failed"
    TAB_SWITCH_FAILED = "tab_switch_failed"
    DIALOG_NOT_HANDLED = "dialog_not_handled"

    # Validation errors
    INVALID_SELECTOR = "invalid_selector"
    INVALID_URL = "invalid_url"
    INVALID_PARAMETER = "invalid_parameter"

    # System/timeout errors
    INTERNAL_ERROR = "internal_error"
    TIMEOUT = "timeout"
    NETWORK_TIMEOUT = "network_timeout"
    WAIT_TIMEOUT = "wait_timeout"
    VERIFICATION_TIMEOUT = "verification_timeout"

    # Unknown
    UNKNOWN = "unknown"


class OwlBrowserError(Exception):
    """Base exception for all Owl Browser SDK errors."""

    pass


class ConnectionError(OwlBrowserError):
    """Raised when connection to the browser server fails."""

    def __init__(self, message: str, cause: Exception | None = None) -> None:
        self.message = message
        self.cause = cause
        super().__init__(message)


class AuthenticationError(OwlBrowserError):
    """
    Raised when authentication fails (401 Unauthorized).

    This can happen when:
    - The bearer token is invalid or missing
    - The JWT token has expired
    - The JWT token signature is invalid
    """

    def __init__(self, message: str, reason: str | None = None) -> None:
        self.message = message
        self.reason = reason
        self.status_code = 401
        super().__init__(self._format_message())

    def _format_message(self) -> str:
        lines = [f"Authentication Error: {self.message}"]
        if self.reason:
            lines.append(f"Reason: {self.reason}")
        return "\n".join(lines)


class ToolExecutionError(OwlBrowserError):
    """Raised when a browser tool execution fails."""

    def __init__(
        self,
        tool_name: str,
        message: str,
        status: str | None = None,
        result: Any = None,
    ) -> None:
        self.tool_name = tool_name
        self.message = message
        self.status = status
        self.result = result
        super().__init__(f"Tool '{tool_name}' failed: {message}")


class TimeoutError(OwlBrowserError):
    """Raised when an operation times out."""

    def __init__(self, message: str, timeout_ms: int | None = None) -> None:
        self.message = message
        self.timeout_ms = timeout_ms
        super().__init__(message)


class RateLimitError(OwlBrowserError):
    """
    Raised when the client is rate limited (429 Too Many Requests).

    Attributes:
        retry_after: Number of seconds to wait before retrying.
    """

    def __init__(
        self,
        message: str,
        retry_after: int = 60,
        limit: int | None = None,
        remaining: int | None = None,
    ) -> None:
        self.message = message
        self.retry_after = retry_after
        self.limit = limit
        self.remaining = remaining
        self.status_code = 429
        super().__init__(f"Rate limit exceeded: {message}. Retry after {retry_after}s")


class IPBlockedError(OwlBrowserError):
    """Raised when the client IP is blocked (403 Forbidden)."""

    def __init__(self, message: str, ip_address: str | None = None) -> None:
        self.message = message
        self.ip_address = ip_address
        self.status_code = 403
        super().__init__(f"IP blocked: {message}")


class ContextLimitError(OwlBrowserError):
    """Raised when context limit is exceeded for the license."""

    def __init__(
        self,
        message: str,
        current_contexts: int | None = None,
        max_contexts: int | None = None,
    ) -> None:
        self.message = message
        self.current_contexts = current_contexts
        self.max_contexts = max_contexts
        super().__init__(message)


class ElementNotFoundError(OwlBrowserError):
    """Raised when an element cannot be found on the page."""

    def __init__(self, selector: str, message: str | None = None) -> None:
        self.selector = selector
        self.message = message or f"Element not found: {selector}"
        super().__init__(self.message)


class NavigationError(OwlBrowserError):
    """Raised when page navigation fails."""

    def __init__(self, url: str, message: str, status_code: int | None = None) -> None:
        self.url = url
        self.message = message
        self.status_code = status_code
        super().__init__(f"Navigation to {url} failed: {message}")


class FlowExecutionError(OwlBrowserError):
    """Raised when flow execution fails."""

    def __init__(
        self,
        step_index: int,
        tool_name: str,
        message: str,
        result: Any = None,
    ) -> None:
        self.step_index = step_index
        self.tool_name = tool_name
        self.message = message
        self.result = result
        super().__init__(f"Flow failed at step {step_index} ({tool_name}): {message}")


class ExpectationError(OwlBrowserError):
    """Raised when an expectation validation fails."""

    def __init__(
        self,
        message: str,
        expected: Any,
        actual: Any,
        field: str | None = None,
    ) -> None:
        self.message = message
        self.expected = expected
        self.actual = actual
        self.field = field
        super().__init__(message)


class OpenAPISchemaError(OwlBrowserError):
    """Raised when OpenAPI schema loading or parsing fails."""

    def __init__(self, message: str, cause: Exception | None = None) -> None:
        self.message = message
        self.cause = cause
        super().__init__(message)


@dataclass
class ActionResult:
    """
    ActionResult returned by browser for validated actions.
    Contains success status, status code, message, and additional details.
    """

    success: bool
    status: str
    message: str
    selector: str | None = None
    url: str | None = None
    error_code: str | None = None
    http_status: int | None = None
    element_count: int | None = None

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> ActionResult:
        """Create ActionResult from dictionary."""
        return cls(
            success=data.get("success", False),
            status=data.get("status", "unknown"),
            message=data.get("message", ""),
            selector=data.get("selector"),
            url=data.get("url"),
            error_code=data.get("error_code"),
            http_status=data.get("http_status"),
            element_count=data.get("element_count"),
        )


def is_action_result(result: Any) -> bool:
    """Check if a result is an ActionResult object or dict."""
    if isinstance(result, ActionResult):
        return True
    if isinstance(result, dict):
        return (
            "success" in result
            and "status" in result
            and isinstance(result.get("success"), bool)
        )
    return False


def raise_for_action_result(result: Any) -> None:
    """
    Check if result is a failed ActionResult and raise appropriate exception.

    Args:
        result: The result to check (can be dict or ActionResult).

    Raises:
        ElementNotFoundError: If element was not found.
        NavigationError: If navigation failed.
        ToolExecutionError: For other action failures.
    """
    if not is_action_result(result):
        return

    if isinstance(result, dict):
        action_result = ActionResult.from_dict(result)
    else:
        action_result = result

    if action_result.success:
        return

    if action_result.status == ActionStatus.ELEMENT_NOT_FOUND and action_result.selector:
        raise ElementNotFoundError(action_result.selector, action_result.message)

    if action_result.status in {
        ActionStatus.NAVIGATION_FAILED,
        ActionStatus.NAVIGATION_TIMEOUT,
        ActionStatus.PAGE_LOAD_ERROR,
    }:
        raise NavigationError(
            url=action_result.url or "unknown",
            message=action_result.message,
            status_code=action_result.http_status,
        )

    raise ToolExecutionError(
        tool_name="unknown",
        message=action_result.message,
        status=action_result.status,
        result=action_result,
    )
