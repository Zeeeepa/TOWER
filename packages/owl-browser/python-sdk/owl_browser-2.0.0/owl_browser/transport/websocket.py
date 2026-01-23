"""
WebSocket transport for Owl Browser SDK v2.

Provides async WebSocket transport for real-time communication
with the Owl Browser HTTP server.

Note: WebSocket support is optional and primarily used for
real-time streaming operations. Most operations work fine
over HTTP transport.
"""

from __future__ import annotations

import asyncio
import itertools
import json
from typing import Any

import aiohttp

from ..auth.jwt import JWTAuth
from ..auth.token import TokenAuth
from ..exceptions import (
    AuthenticationError,
    ConnectionError,
    OwlBrowserError,
    TimeoutError,
    ToolExecutionError,
)
from ..types import AuthMode, RemoteConfig


class WebSocketTransport:
    """
    Async WebSocket transport for real-time communication.

    Provides a WebSocket connection to the Owl Browser server
    for operations that benefit from real-time streaming.

    Example:
        ```python
        from owl_browser.transport import WebSocketTransport
        from owl_browser.types import RemoteConfig, TransportMode

        config = RemoteConfig(
            url="http://localhost:8080",
            token="secret",
            transport=TransportMode.WEBSOCKET
        )
        transport = WebSocketTransport(config)

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
        "_auth",
        "_session",
        "_ws",
        "_pending_requests",
        "_receive_task",
        "_connected",
        "_request_id_counter",
    )

    def __init__(self, config: RemoteConfig) -> None:
        """
        Initialize WebSocket transport.

        Args:
            config: Remote server configuration.
        """
        self._config = config
        ws_scheme = "wss" if config.url.startswith("https") else "ws"
        base = config.url.replace("https://", "").replace("http://", "")
        self._base_url = f"{ws_scheme}://{base}{config.api_prefix}/ws"

        # Setup authentication
        if config.auth_mode == AuthMode.JWT and config.jwt:
            self._auth: TokenAuth | JWTAuth = JWTAuth(config.jwt)
        else:
            if not config.token:
                raise ValueError("Token is required for TOKEN authentication")
            self._auth = TokenAuth(config.token)

        self._session: aiohttp.ClientSession | None = None
        self._ws: aiohttp.ClientWebSocketResponse | None = None
        self._pending_requests: dict[int, asyncio.Future[Any]] = {}
        self._receive_task: asyncio.Task[None] | None = None
        self._connected = False
        # Server requires integer IDs for request/response correlation
        self._request_id_counter = itertools.count(1)

    async def __aenter__(self) -> WebSocketTransport:
        """Async context manager entry."""
        await self.connect()
        return self

    async def __aexit__(
        self,
        exc_type: type[BaseException] | None,
        exc_val: BaseException | None,
        exc_tb: Any,
    ) -> None:
        """Async context manager exit."""
        await self.close()

    async def connect(self) -> None:
        """
        Establish WebSocket connection.

        Raises:
            ConnectionError: If connection fails.
            AuthenticationError: If authentication fails.
        """
        if self._connected:
            return

        try:
            self._session = aiohttp.ClientSession()
            token = self._auth.get_token()

            self._ws = await self._session.ws_connect(
                self._base_url,
                headers={"Authorization": f"Bearer {token}"},
                timeout=aiohttp.ClientTimeout(total=self._config.timeout),
            )

            self._connected = True
            self._receive_task = asyncio.create_task(self._receive_loop())

        except aiohttp.WSServerHandshakeError as e:
            await self._cleanup()
            if e.status == 401:
                raise AuthenticationError("WebSocket authentication failed") from e
            raise ConnectionError(f"WebSocket handshake failed: {e}") from e

        except Exception as e:
            await self._cleanup()
            raise ConnectionError(f"WebSocket connection failed: {e}") from e

    async def _cleanup(self) -> None:
        """Clean up resources."""
        if self._receive_task:
            self._receive_task.cancel()
            try:
                await self._receive_task
            except asyncio.CancelledError:
                pass
            self._receive_task = None

        if self._ws:
            await self._ws.close()
            self._ws = None

        if self._session:
            await self._session.close()
            self._session = None

        self._connected = False

        for future in self._pending_requests.values():
            if not future.done():
                future.set_exception(ConnectionError("WebSocket connection closed"))
        self._pending_requests.clear()

    async def close(self) -> None:
        """Close the WebSocket connection and release resources."""
        await self._cleanup()

    async def _receive_loop(self) -> None:
        """Background task to receive and dispatch WebSocket messages."""
        if not self._ws:
            return

        try:
            async for msg in self._ws:
                if msg.type == aiohttp.WSMsgType.TEXT:
                    try:
                        data = json.loads(msg.data)
                        # Server returns integer ID for request/response correlation
                        request_id = data.get("id")
                        if request_id is not None and request_id in self._pending_requests:
                            future = self._pending_requests.pop(request_id)
                            # Check for error response (success=false with error field)
                            if not data.get("success", True) and "error" in data:
                                future.set_exception(
                                    ToolExecutionError(
                                        tool_name="unknown",
                                        message=data["error"],
                                    )
                                )
                            else:
                                future.set_result(data.get("result"))
                    except json.JSONDecodeError:
                        pass

                elif msg.type == aiohttp.WSMsgType.ERROR:
                    # WebSocket error - cancel pending requests
                    for future in self._pending_requests.values():
                        if not future.done():
                            future.set_exception(
                                ConnectionError("WebSocket error occurred")
                            )
                    self._pending_requests.clear()
                    break

                elif msg.type in {aiohttp.WSMsgType.CLOSE, aiohttp.WSMsgType.CLOSED}:
                    # Server closed connection - cancel pending requests
                    for future in self._pending_requests.values():
                        if not future.done():
                            future.set_exception(
                                ConnectionError("WebSocket connection closed by server")
                            )
                    self._pending_requests.clear()
                    break

        except asyncio.CancelledError:
            # Normal cancellation during cleanup
            raise
        except Exception as e:
            # Unexpected error - cancel pending requests
            for future in self._pending_requests.values():
                if not future.done():
                    future.set_exception(
                        ConnectionError(f"WebSocket receive error: {e}")
                    )
            self._pending_requests.clear()
        finally:
            self._connected = False

    async def execute(self, tool_name: str, params: dict[str, Any]) -> Any:
        """
        Execute a browser tool over WebSocket.

        Args:
            tool_name: Name of the tool to execute.
            params: Tool parameters.

        Returns:
            Tool execution result.

        Raises:
            ConnectionError: If not connected.
            ToolExecutionError: If tool execution fails.
            TimeoutError: If request times out.
        """
        if not self._connected or not self._ws:
            raise ConnectionError("WebSocket not connected")

        # Server requires integer IDs for request/response correlation
        request_id = next(self._request_id_counter)
        future: asyncio.Future[Any] = asyncio.Future()
        self._pending_requests[request_id] = future

        message = {
            "id": request_id,
            "method": tool_name,
            "params": params,
        }

        try:
            await self._ws.send_json(message)
            result = await asyncio.wait_for(future, timeout=self._config.timeout)
            return result

        except asyncio.TimeoutError as e:
            self._pending_requests.pop(request_id, None)
            raise TimeoutError(
                f"WebSocket request to {tool_name} timed out",
                timeout_ms=int(self._config.timeout * 1000),
            ) from e

        except Exception as e:
            self._pending_requests.pop(request_id, None)
            if isinstance(e, OwlBrowserError):
                raise
            raise ToolExecutionError(
                tool_name=tool_name,
                message=str(e),
            ) from e

    @property
    def connected(self) -> bool:
        """Check if WebSocket is connected."""
        return self._connected
