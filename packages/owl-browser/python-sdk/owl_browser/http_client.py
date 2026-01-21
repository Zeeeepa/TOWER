"""
HTTP client for connecting to remote Owl Browser HTTP server.

This module provides the HTTP transport layer for remote browser connections,
enabling the SDK to work with a remote browser server via REST API.

Features:
- HTTP connection pooling with keep-alive
- Retry with exponential backoff and jitter
- Concurrency limiting via semaphore
- Comprehensive error handling
"""

import json
import urllib.request
import urllib.error
import ssl
import socket
import time
import random
import threading
import http.client
from typing import Any, Dict, Optional, Callable
from dataclasses import dataclass, field
from urllib.parse import urlparse

from .types import RemoteConfig, AuthMode, RetryConfig, ConcurrencyConfig
from .exceptions import (
    OwlBrowserError,
    LicenseError,
    BrowserInitializationError,
    CommandTimeoutError,
    AuthenticationError,
    RateLimitError,
    IPBlockedError,
)
from .jwt import JWTManager


def calculate_retry_delay(config: RetryConfig, attempt: int) -> float:
    """Calculate delay in seconds with exponential backoff and jitter."""
    delay_ms = config.initial_delay_ms * (config.backoff_multiplier ** attempt)
    delay_ms = min(delay_ms, config.max_delay_ms)
    # Add jitter to prevent thundering herd
    jitter = delay_ms * config.jitter_factor * (random.random() * 2 - 1)
    return max(0, (delay_ms + jitter) / 1000.0)


class Semaphore:
    """Thread-safe semaphore for concurrency limiting."""

    def __init__(self, permits: int):
        self._permits = permits
        self._lock = threading.Lock()
        self._condition = threading.Condition(self._lock)

    def acquire(self, timeout: Optional[float] = None) -> bool:
        """Acquire a permit, blocking if necessary."""
        with self._condition:
            end_time = None if timeout is None else time.time() + timeout
            while self._permits <= 0:
                if timeout is not None:
                    remaining = end_time - time.time()
                    if remaining <= 0:
                        return False
                    self._condition.wait(timeout=remaining)
                else:
                    self._condition.wait()
            self._permits -= 1
            return True

    def release(self):
        """Release a permit."""
        with self._condition:
            self._permits += 1
            self._condition.notify()

    def __enter__(self):
        self.acquire()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.release()


class ConnectionPool:
    """
    HTTP connection pool with keep-alive support.

    Maintains a pool of persistent HTTP connections to reduce
    connection overhead for high-frequency operations.
    """

    def __init__(
        self,
        host: str,
        port: int,
        use_ssl: bool = False,
        max_connections: int = 10,
        ssl_context: Optional[ssl.SSLContext] = None,
        timeout: float = 30.0
    ):
        self._host = host
        self._port = port
        self._use_ssl = use_ssl
        self._max_connections = max_connections
        self._ssl_context = ssl_context
        self._timeout = timeout

        self._pool: list = []
        self._lock = threading.Lock()
        self._created = 0

    def _create_connection(self) -> http.client.HTTPConnection:
        """Create a new HTTP connection."""
        if self._use_ssl:
            conn = http.client.HTTPSConnection(
                self._host,
                self._port,
                timeout=self._timeout,
                context=self._ssl_context
            )
        else:
            conn = http.client.HTTPConnection(
                self._host,
                self._port,
                timeout=self._timeout
            )
        return conn

    def get_connection(self) -> http.client.HTTPConnection:
        """Get a connection from the pool or create a new one."""
        with self._lock:
            # Try to get an existing connection
            while self._pool:
                conn = self._pool.pop()
                try:
                    # Check if connection is still alive
                    conn.sock.getpeername()
                    return conn
                except (AttributeError, OSError):
                    # Connection is dead, discard it
                    self._created -= 1
                    continue

            # Create new connection if under limit
            if self._created < self._max_connections:
                self._created += 1
                return self._create_connection()

        # At limit, create a new connection anyway (will be discarded after use)
        return self._create_connection()

    def return_connection(self, conn: http.client.HTTPConnection):
        """Return a connection to the pool."""
        with self._lock:
            if len(self._pool) < self._max_connections:
                self._pool.append(conn)
            else:
                # Pool is full, close the connection
                try:
                    conn.close()
                except:
                    pass

    def close_connection(self, conn: http.client.HTTPConnection):
        """Close a connection without returning it to the pool."""
        with self._lock:
            self._created = max(0, self._created - 1)
        try:
            conn.close()
        except:
            pass

    def close_all(self):
        """Close all connections in the pool."""
        with self._lock:
            for conn in self._pool:
                try:
                    conn.close()
                except:
                    pass
            self._pool.clear()
            self._created = 0


class HttpTransport:
    """
    HTTP transport for communicating with remote Owl Browser HTTP server.

    This class handles all HTTP communication with the remote server,
    including authentication, request/response handling, and error mapping.

    Features:
    - Connection pooling with keep-alive for reduced latency
    - Retry with exponential backoff and jitter
    - Concurrency limiting to prevent server overload
    """

    # Tools that may take longer due to network operations
    LONG_RUNNING_TOOLS = {
        "browser_navigate",
        "browser_reload",
        "browser_wait",
        "browser_wait_for_selector",
        "browser_query_page",
        "browser_summarize_page",
        "browser_nla",
        "browser_solve_captcha",
        "browser_solve_text_captcha",
        "browser_solve_image_captcha",
    }

    # Errors that are safe to retry
    RETRYABLE_ERRORS = (
        ConnectionResetError,
        BrokenPipeError,
        ConnectionRefusedError,
        ConnectionAbortedError,
        http.client.RemoteDisconnected,
        http.client.CannotSendRequest,
        http.client.BadStatusLine,
        OSError,
    )

    def __init__(
        self,
        config: RemoteConfig,
        retry_config: Optional[RetryConfig] = None,
        max_concurrent: int = 10,
        max_pool_connections: int = 10
    ):
        """
        Initialize HTTP transport.

        Args:
            config: Remote server configuration
            retry_config: Retry configuration (uses defaults if not provided)
            max_concurrent: Maximum concurrent requests (semaphore permits)
            max_pool_connections: Maximum connections in the pool
        """
        self._config = config
        self._base_url = config.url
        self._static_token = config.token
        self._jwt_manager: Optional[JWTManager] = None
        self._timeout = config.timeout / 1000.0  # Convert to seconds
        self._long_timeout = max(120.0, self._timeout * 4)  # 2 minutes or 4x base timeout
        self._ssl_context = self._create_ssl_context()

        # Retry configuration
        self._retry_config = retry_config or RetryConfig()

        # Concurrency limiter
        self._semaphore = Semaphore(max_concurrent)

        # Parse URL for connection pool
        parsed = urlparse(self._base_url)
        use_ssl = parsed.scheme == "https"
        host = parsed.hostname or "localhost"
        port = parsed.port or (443 if use_ssl else 80)

        # Connection pool
        self._pool = ConnectionPool(
            host=host,
            port=port,
            use_ssl=use_ssl,
            max_connections=max_pool_connections,
            ssl_context=self._ssl_context,
            timeout=self._timeout
        )

        # Setup JWT manager if using JWT authentication
        if config.auth_mode == AuthMode.JWT and config.jwt:
            self._jwt_manager = JWTManager(
                private_key=config.jwt.private_key,
                expires_in=config.jwt.expires_in,
                refresh_threshold=config.jwt.refresh_threshold,
                issuer=config.jwt.issuer,
                subject=config.jwt.subject,
                audience=config.jwt.audience,
                claims=config.jwt.claims
            )

    def _get_auth_token(self) -> str:
        """Get the current authentication token (static or JWT)."""
        if self._jwt_manager:
            return self._jwt_manager.get_token()
        if self._static_token:
            return self._static_token
        raise OwlBrowserError("No authentication token available")

    def _create_ssl_context(self) -> ssl.SSLContext:
        """Create SSL context based on configuration."""
        if self._config.verify_ssl:
            return ssl.create_default_context()
        else:
            # Disable SSL verification (not recommended for production)
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            return ctx

    def _make_request(
        self,
        method: str,
        path: str,
        data: Optional[Dict[str, Any]] = None,
        require_auth: bool = True,
        long_running: bool = False
    ) -> Dict[str, Any]:
        """
        Make an HTTP request to the server using connection pooling.

        Args:
            method: HTTP method (GET, POST)
            path: URL path
            data: Request body data (JSON)
            require_auth: Whether to include authorization header
            long_running: Use extended timeout for long-running operations

        Returns:
            Response data as dictionary

        Raises:
            OwlBrowserError: On request failure
        """
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json",
            "Connection": "keep-alive",  # Use keep-alive for connection reuse
        }

        if require_auth:
            headers["Authorization"] = f"Bearer {self._get_auth_token()}"

        body = None
        if data is not None:
            body = json.dumps(data).encode('utf-8')

        # Use longer timeout for operations that may take a while
        timeout = self._long_timeout if long_running else self._timeout

        last_error = None
        max_retries = self._retry_config.max_retries

        # Acquire semaphore to limit concurrent requests
        with self._semaphore:
            for attempt in range(max_retries):
                conn = None
                try:
                    # Get connection from pool
                    conn = self._pool.get_connection()
                    conn.timeout = timeout

                    # Make request
                    conn.request(method, path, body=body, headers=headers)
                    response = conn.getresponse()

                    # Read response
                    response_data = response.read().decode('utf-8')

                    # Check for HTTP errors
                    if response.status >= 400:
                        # Return connection to pool before handling error
                        self._pool.return_connection(conn)
                        conn = None
                        return self._handle_http_error_status(
                            response.status, response_data, path
                        )

                    # Success - return connection to pool
                    self._pool.return_connection(conn)
                    conn = None

                    if response_data:
                        return json.loads(response_data)
                    return {}

                except self.RETRYABLE_ERRORS as e:
                    # Close failed connection
                    if conn:
                        self._pool.close_connection(conn)
                        conn = None

                    last_error = e
                    if attempt < max_retries - 1:
                        delay = calculate_retry_delay(self._retry_config, attempt)
                        time.sleep(delay)
                        continue
                    raise OwlBrowserError(
                        f"Connection failed after {max_retries} retries: {e}"
                    )

                except socket.timeout:
                    if conn:
                        self._pool.close_connection(conn)
                    raise CommandTimeoutError(f"Request timed out: {path}")

                except Exception as e:
                    if conn:
                        self._pool.close_connection(conn)

                    # Check if it's a retryable error type by name
                    error_name = type(e).__name__
                    if any(name in error_name for name in
                           ["RemoteDisconnected", "ConnectionReset", "BrokenPipe"]):
                        last_error = e
                        if attempt < max_retries - 1:
                            delay = calculate_retry_delay(self._retry_config, attempt)
                            time.sleep(delay)
                            continue
                        raise OwlBrowserError(
                            f"Connection failed after {max_retries} retries: {e}"
                        )
                    raise

        if last_error:
            raise OwlBrowserError(f"Request failed: {last_error}")

    def _handle_http_error_status(
        self,
        status: int,
        response_body: str,
        path: str
    ) -> Dict[str, Any]:
        """
        Handle HTTP error responses by status code.

        Args:
            status: HTTP status code
            response_body: Response body text
            path: Request path

        Raises:
            Appropriate exception based on error type
        """
        try:
            response_data = json.loads(response_body) if response_body else {}
        except json.JSONDecodeError:
            response_data = {"error": response_body}

        error_message = response_data.get("error", f"HTTP {status}")

        if status == 401:
            raise AuthenticationError(
                message=error_message or "Invalid or missing authorization token",
                reason=response_data.get("reason")
            )

        if status == 403:
            raise IPBlockedError(
                message=error_message or "Access forbidden - IP not allowed",
                ip_address=response_data.get("client_ip")
            )

        if status == 429:
            raise RateLimitError(
                message=error_message or "Rate limit exceeded",
                retry_after=response_data.get("retry_after", 60),
                limit=response_data.get("limit"),
                remaining=response_data.get("remaining")
            )

        if status == 503:
            if response_data.get("license_status"):
                raise LicenseError(
                    message=response_data.get("license_message", "License error"),
                    status=response_data.get("license_status"),
                    fingerprint=response_data.get("hardware_fingerprint")
                )
            raise BrowserInitializationError(f"Browser not ready: {error_message}")

        if status == 404:
            raise OwlBrowserError(f"Endpoint not found: {path}")

        if status == 422:
            missing = response_data.get("missing_fields", "")
            unknown = response_data.get("unknown_fields", "")
            raise OwlBrowserError(
                f"Validation error: {error_message}. "
                f"Missing: {missing}. Unknown: {unknown}"
            )

        if status == 502:
            raise OwlBrowserError(f"Browser command failed: {error_message}")

        raise OwlBrowserError(f"HTTP {status}: {error_message}")

    def close(self):
        """Close the transport and all pooled connections."""
        self._pool.close_all()

    def health_check(self) -> Dict[str, Any]:
        """
        Check server health status.

        Returns:
            Health status dict with 'status', 'browser_ready', 'browser_state'
        """
        return self._make_request("GET", "/health", require_auth=False)

    def list_tools(self) -> Dict[str, Any]:
        """
        List available browser tools.

        Returns:
            Dict with 'tools' list
        """
        return self._make_request("GET", "/tools")

    def get_tool_info(self, tool_name: str) -> Dict[str, Any]:
        """
        Get documentation for a specific tool.

        Args:
            tool_name: Name of the tool

        Returns:
            Tool documentation
        """
        return self._make_request("GET", f"/tools/{tool_name}")

    def execute_tool(
        self,
        tool_name: str,
        params: Optional[Dict[str, Any]] = None
    ) -> Any:
        """
        Execute a browser tool.

        Args:
            tool_name: Name of the tool to execute
            params: Tool parameters

        Returns:
            Tool execution result

        Raises:
            OwlBrowserError: On execution failure
        """
        # Check if this is a long-running operation
        long_running = tool_name in self.LONG_RUNNING_TOOLS

        response = self._make_request(
            "POST",
            f"/execute/{tool_name}",
            data=params or {},
            long_running=long_running
        )

        if not response.get("success", False):
            error_msg = response.get("error", "Unknown error")
            raise OwlBrowserError(f"Tool execution failed: {error_msg}")

        result = response.get("result")

        # Handle nested response format from browser IPC
        # Some commands return {"id": N, "result": {...}} structure
        if isinstance(result, dict) and "id" in result and "result" in result:
            result = result["result"]

        return result

    def send_raw_command(self, command: Dict[str, Any]) -> Any:
        """
        Send a raw command to the browser (advanced usage).

        Args:
            command: Raw command dict with 'method' and parameters

        Returns:
            Command result
        """
        response = self._make_request("POST", "/command", data=command)

        if not response.get("success", False):
            error_msg = response.get("error", "Unknown error")
            raise OwlBrowserError(f"Command failed: {error_msg}")

        return response.get("result")

    def is_browser_ready(self) -> bool:
        """
        Check if browser is ready to accept commands.

        Returns:
            True if browser is ready
        """
        try:
            health = self.health_check()
            return health.get("browser_ready", False)
        except Exception:
            return False


# Tool name mapping from SDK method names to HTTP API tool names
TOOL_NAME_MAP = {
    # Context management
    "createContext": "browser_create_context",
    "releaseContext": "browser_close_context",
    "listContexts": "browser_list_contexts",

    # Navigation
    "navigate": "browser_navigate",
    "reload": "browser_reload",
    "goBack": "browser_go_back",
    "goForward": "browser_go_forward",

    # Interaction
    "click": "browser_click",
    "type": "browser_type",
    "pick": "browser_pick",
    "pressKey": "browser_press_key",
    "submitForm": "browser_submit_form",
    "highlight": "browser_highlight",

    # Content extraction
    "extractText": "browser_extract_text",
    "screenshot": "browser_screenshot",
    "getHTML": "browser_get_html",
    "getMarkdown": "browser_get_markdown",
    "extractJSON": "browser_extract_json",
    "detectWebsiteType": "browser_detect_site",
    "listTemplates": "browser_list_templates",

    # AI features
    "summarizePage": "browser_summarize_page",
    "queryPage": "browser_query_page",
    "llmStatus": "browser_llm_status",
    "executeNLA": "browser_nla",
    "getLLMStatus": "browser_llm_status",

    # Scrolling
    "scrollBy": "browser_scroll_by",
    "scrollToElement": "browser_scroll_to_element",
    "scrollToTop": "browser_scroll_to_top",
    "scrollToBottom": "browser_scroll_to_bottom",

    # Waiting
    "waitForSelector": "browser_wait_for_selector",
    "waitForTimeout": "browser_wait",

    # Page info
    "getPageInfo": "browser_get_page_info",
    "getCurrentURL": "browser_get_page_info",
    "getPageTitle": "browser_get_page_info",
    "setViewport": "browser_set_viewport",

    # Video recording
    "startVideoRecording": "browser_start_video_recording",
    "pauseVideoRecording": "browser_pause_video_recording",
    "resumeVideoRecording": "browser_resume_video_recording",
    "stopVideoRecording": "browser_stop_video_recording",
    "getVideoRecordingStats": "browser_get_video_recording_stats",

    # Demographics
    "getDemographics": "browser_get_demographics",
    "getLocation": "browser_get_location",
    "getDateTime": "browser_get_datetime",
    "getWeather": "browser_get_weather",

    # CAPTCHA
    "detectCaptcha": "browser_detect_captcha",
    "classifyCaptcha": "browser_classify_captcha",
    "solveTextCaptcha": "browser_solve_text_captcha",
    "solveImageCaptcha": "browser_solve_image_captcha",
    "solveCaptcha": "browser_solve_captcha",

    # Cookies
    "getCookies": "browser_get_cookies",
    "setCookie": "browser_set_cookie",
    "deleteCookies": "browser_delete_cookies",

    # Proxy
    "setProxy": "browser_set_proxy",
    "getProxyStatus": "browser_get_proxy_status",
    "connectProxy": "browser_connect_proxy",
    "disconnectProxy": "browser_disconnect_proxy",

    # Profiles
    "createProfile": "browser_create_profile",
    "loadProfile": "browser_load_profile",
    "saveProfile": "browser_save_profile",
    "getProfile": "browser_get_profile",
    "updateProfileCookies": "browser_update_profile_cookies",
}


# Parameter name mapping from SDK to HTTP API
PARAM_NAME_MAP = {
    # Common renames
    "context_id": "context_id",
    "url": "url",
    "selector": "selector",
    "text": "text",
    "value": "value",
    "query": "query",
    "command": "command",
    "timeout": "timeout",
    "ignore_cache": "ignore_cache",
    "clean_level": "clean_level",
    "include_links": "include_links",
    "include_images": "include_images",
    "max_length": "max_length",
    "template_name": "template",
    "force_refresh": "force_refresh",
    "key": "key",
    "border_color": "border_color",
    "background_color": "background_color",
    "x": "x",
    "y": "y",
    "width": "width",
    "height": "height",
    "fps": "fps",
    "codec": "codec",
    "max_attempts": "max_attempts",

    # Cookie parameters
    "cookie_name": "cookie_name",
    "httpOnly": "httpOnly",
    "sameSite": "sameSite",
    "expires": "expires",
    "secure": "secure",
    "path": "path",
    "domain": "domain",
    "name": "name",

    # Proxy parameters
    "type": "type",
    "host": "host",
    "port": "port",
    "username": "username",
    "password": "password",
    "stealth": "stealth",
    "block_webrtc": "block_webrtc",
    "spoof_timezone": "spoof_timezone",
    "spoof_language": "spoof_language",
    "timezone_override": "timezone_override",
    "language_override": "language_override",

    # Context creation parameters (LLM)
    "llm_enabled": "llm_enabled",
    "llm_use_builtin": "llm_use_builtin",
    "llm_endpoint": "llm_endpoint",
    "llm_model": "llm_model",
    "llm_api_key": "llm_api_key",

    # Context creation parameters (Proxy)
    "proxy_type": "proxy_type",
    "proxy_host": "proxy_host",
    "proxy_port": "proxy_port",
    "proxy_username": "proxy_username",
    "proxy_password": "proxy_password",
    "proxy_stealth": "proxy_stealth",
    "proxy_block_webrtc": "proxy_block_webrtc",
    "proxy_spoof_timezone": "proxy_spoof_timezone",
    "proxy_spoof_language": "proxy_spoof_language",
    "proxy_timezone_override": "proxy_timezone_override",
    "proxy_language_override": "proxy_language_override",
    "proxy_ca_cert_path": "proxy_ca_cert_path",
    "proxy_trust_custom_ca": "proxy_trust_custom_ca",

    # Profile
    "profile_path": "profile_path",
}


def map_method_to_tool(method: str) -> str:
    """
    Map SDK method name to HTTP API tool name.

    Args:
        method: SDK method name (e.g., 'navigate', 'click')

    Returns:
        HTTP API tool name (e.g., 'browser_navigate', 'browser_click')
    """
    return TOOL_NAME_MAP.get(method, f"browser_{method}")


def map_params_for_http(params: Dict[str, Any]) -> Dict[str, Any]:
    """
    Map SDK parameter names to HTTP API parameter names.

    Args:
        params: SDK parameters

    Returns:
        HTTP API parameters
    """
    mapped = {}
    for key, value in params.items():
        # Use mapping if available, otherwise use original key
        mapped_key = PARAM_NAME_MAP.get(key, key)
        mapped[mapped_key] = value
    return mapped
