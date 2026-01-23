"""
HTTP transport for Owl Browser SDK v2.

Provides async HTTP transport with connection pooling, retry logic,
and concurrency limiting.
"""

from __future__ import annotations

import asyncio
import random
from typing import Any

import aiohttp

from ..auth.jwt import JWTAuth
from ..auth.token import TokenAuth
from ..exceptions import (
    AuthenticationError,
    ConnectionError,
    IPBlockedError,
    OwlBrowserError,
    RateLimitError,
    TimeoutError,
    ToolExecutionError,
)
from ..types import AuthMode, RemoteConfig, RetryConfig


# Tools that may take longer due to network operations or AI processing
LONG_RUNNING_TOOLS: frozenset[str] = frozenset({
    "browser_navigate",
    "browser_reload",
    "browser_wait",
    "browser_wait_for_selector",
    "browser_wait_for_network_idle",
    "browser_wait_for_function",
    "browser_wait_for_url",
    "browser_query_page",
    "browser_summarize_page",
    "browser_nla",
    "browser_solve_captcha",
    "browser_solve_text_captcha",
    "browser_solve_image_captcha",
    "browser_detect_captcha",
    "browser_extract_site",
    "browser_extract_site_progress",
    "browser_extract_site_result",
    "browser_get_markdown",
    "browser_get_html",
    "browser_extract_text",
    "browser_extract_json",
    "browser_screenshot",
    "browser_ai_click",
    "browser_ai_type",
    "browser_ai_extract",
    "browser_ai_query",
    "browser_ai_analyze",
    "browser_find_element",
    "browser_wait_for_download",
    "browser_wait_for_dialog",
})


def _calculate_retry_delay(config: RetryConfig, attempt: int) -> float:
    """Calculate delay in seconds with exponential backoff and jitter."""
    delay_ms = config.initial_delay_ms * (config.backoff_multiplier ** attempt)
    delay_ms = min(delay_ms, config.max_delay_ms)
    jitter = delay_ms * config.jitter_factor * (random.random() * 2 - 1)
    return max(0, (delay_ms + jitter) / 1000.0)


class HTTPTransport:
    """
    Async HTTP transport for communicating with Owl Browser HTTP server.

    Features:
    - Connection pooling with aiohttp for efficient connection reuse.
    - Retry with exponential backoff and jitter.
    - Concurrency limiting via asyncio.Semaphore.
    - Extended timeouts for long-running operations.

    Example:
        ```python
        from owl_browser.transport import HTTPTransport
        from owl_browser.types import RemoteConfig

        config = RemoteConfig(url="http://localhost:8080", token="secret")
        transport = HTTPTransport(config)

        async with transport:
            result = await transport.execute("browser_navigate", {
                "context_id": "ctx_1",
                "url": "https://example.com"
            })
        ```
    """

    __slots__ = (
        "_config",
        "_base_url",
        "_api_prefix",
        "_timeout",
        "_long_timeout",
        "_auth",
        "_session",
        "_semaphore",
        "_retry_config",
    )

    def __init__(self, config: RemoteConfig) -> None:
        """
        Initialize HTTP transport.

        Args:
            config: Remote server configuration.
        """
        self._config = config
        self._base_url = config.url
        self._api_prefix = config.api_prefix
        self._timeout = config.timeout
        self._long_timeout = max(120.0, config.timeout * 4)
        self._retry_config = config.retry
        self._semaphore = asyncio.Semaphore(config.max_concurrent)
        self._session: aiohttp.ClientSession | None = None

        # Setup authentication
        if config.auth_mode == AuthMode.JWT and config.jwt:
            self._auth: TokenAuth | JWTAuth = JWTAuth(config.jwt)
        else:
            if not config.token:
                raise ValueError("Token is required for TOKEN authentication")
            self._auth = TokenAuth(config.token)

    async def __aenter__(self) -> HTTPTransport:
        """Async context manager entry."""
        await self._ensure_session()
        return self

    async def __aexit__(
        self,
        exc_type: type[BaseException] | None,
        exc_val: BaseException | None,
        exc_tb: Any,
    ) -> None:
        """Async context manager exit."""
        await self.close()

    async def _ensure_session(self) -> aiohttp.ClientSession:
        """Ensure aiohttp session exists and return it."""
        if self._session is None or self._session.closed:
            connector = aiohttp.TCPConnector(
                limit=self._config.max_concurrent,
                enable_cleanup_closed=True,
                ssl=self._config.verify_ssl,
            )
            self._session = aiohttp.ClientSession(connector=connector)
        return self._session

    async def close(self) -> None:
        """Close the transport and release all resources."""
        if self._session and not self._session.closed:
            await self._session.close()
            self._session = None

    def _get_headers(self) -> dict[str, str]:
        """Get request headers including authentication."""
        headers = self._auth.get_headers()
        headers["Content-Type"] = "application/json"
        headers["Accept"] = "application/json"
        return headers

    def _prefix_path(self, path: str) -> str:
        """Apply API prefix to a path."""
        return f"{self._api_prefix}{path}"

    async def execute(self, tool_name: str, params: dict[str, Any]) -> Any:
        """
        Execute a browser tool.

        Args:
            tool_name: Name of the tool to execute (e.g., 'browser_navigate').
            params: Tool parameters.

        Returns:
            Tool execution result.

        Raises:
            ToolExecutionError: If the tool execution fails.
            AuthenticationError: If authentication fails.
            RateLimitError: If rate limited.
            TimeoutError: If the request times out.
            ConnectionError: If connection fails after retries.
        """
        is_long_running = tool_name in LONG_RUNNING_TOOLS
        timeout = self._long_timeout if is_long_running else self._timeout

        url = f"{self._base_url}{self._prefix_path(f'/execute/{tool_name}')}"
        last_error: Exception | None = None

        async with self._semaphore:
            for attempt in range(self._retry_config.max_retries):
                try:
                    session = await self._ensure_session()
                    client_timeout = aiohttp.ClientTimeout(total=timeout)

                    async with session.post(
                        url,
                        json=params,
                        headers=self._get_headers(),
                        timeout=client_timeout,
                    ) as response:
                        return await self._handle_response(response, tool_name)

                except aiohttp.ClientResponseError as e:
                    last_error = e
                    if e.status in {401, 403, 429}:
                        raise
                    if attempt < self._retry_config.max_retries - 1:
                        await asyncio.sleep(
                            _calculate_retry_delay(self._retry_config, attempt)
                        )
                        continue
                    raise ConnectionError(
                        f"Request failed after {self._retry_config.max_retries} retries",
                        cause=e,
                    ) from e

                except asyncio.TimeoutError as e:
                    raise TimeoutError(
                        f"Request to {tool_name} timed out after {timeout}s",
                        timeout_ms=int(timeout * 1000),
                    ) from e

                except aiohttp.ClientError as e:
                    last_error = e
                    if attempt < self._retry_config.max_retries - 1:
                        await asyncio.sleep(
                            _calculate_retry_delay(self._retry_config, attempt)
                        )
                        continue
                    raise ConnectionError(
                        f"Connection failed after {self._retry_config.max_retries} retries: {e}",
                        cause=e,
                    ) from e

        if last_error:
            raise ConnectionError(f"Request failed: {last_error}", cause=last_error)
        raise ConnectionError("Request failed with unknown error")

    async def _handle_response(
        self, response: aiohttp.ClientResponse, tool_name: str
    ) -> Any:
        """Handle HTTP response and raise appropriate exceptions."""
        if response.status == 401:
            text = await response.text()
            raise AuthenticationError(text or "Invalid or missing authorization token")

        if response.status == 403:
            data = await response.json()
            raise IPBlockedError(
                message=data.get("error", "Access forbidden"),
                ip_address=data.get("client_ip"),
            )

        if response.status == 429:
            data = await response.json()
            raise RateLimitError(
                message=data.get("error", "Rate limit exceeded"),
                retry_after=data.get("retry_after", 60),
                limit=data.get("limit"),
                remaining=data.get("remaining"),
            )

        if response.status >= 400:
            text = await response.text()
            raise ToolExecutionError(
                tool_name=tool_name,
                message=f"HTTP {response.status}: {text}",
                status=str(response.status),
            )

        content_type = response.headers.get("content-type", "")
        if "application/json" in content_type:
            data = await response.json()
            if not data.get("success", False):
                raise ToolExecutionError(
                    tool_name=tool_name,
                    message=data.get("error", "Unknown error"),
                    result=data,
                )
            return data.get("result")
        else:
            return await response.text()

    async def health_check(self) -> dict[str, Any]:
        """
        Check server health status.

        Returns:
            Health status dict with 'status', 'browser_ready', etc.
        """
        session = await self._ensure_session()
        url = f"{self._base_url}{self._prefix_path('/health')}"

        async with session.get(url, timeout=aiohttp.ClientTimeout(total=10)) as resp:
            if resp.status != 200:
                raise OwlBrowserError(f"Health check failed: HTTP {resp.status}")
            return await resp.json()

    async def list_tools(self) -> list[dict[str, Any]]:
        """
        List available browser tools.

        Returns:
            List of tool definitions.
        """
        session = await self._ensure_session()
        url = f"{self._base_url}{self._prefix_path('/tools')}"

        async with session.get(
            url,
            headers=self._get_headers(),
            timeout=aiohttp.ClientTimeout(total=30),
        ) as resp:
            if resp.status != 200:
                raise OwlBrowserError(f"Failed to list tools: HTTP {resp.status}")
            data = await resp.json()
            return data.get("tools", [])

    async def fetch_openapi_schema(self) -> dict[str, Any]:
        """
        Fetch the OpenAPI schema from the server.

        Returns:
            OpenAPI schema dictionary.
        """
        session = await self._ensure_session()
        url = f"{self._base_url}{self._prefix_path('/openapi.json')}"

        async with session.get(
            url,
            headers=self._get_headers(),
            timeout=aiohttp.ClientTimeout(total=30),
        ) as resp:
            if resp.status != 200:
                raise OwlBrowserError(f"Failed to fetch OpenAPI schema: HTTP {resp.status}")
            return await resp.json()
