"""
Main OwlBrowser client class.

Provides the primary interface for interacting with the Owl Browser HTTP server,
with dynamic method generation from OpenAPI schema.
"""

from __future__ import annotations

import asyncio
from functools import partial
from typing import Any, Callable, Coroutine

from .exceptions import OwlBrowserError, ToolExecutionError
from .openapi import OpenAPILoader, ToolDefinition, coerce_integer_fields, get_bundled_schema
from .transport.http import HTTPTransport
from .transport.websocket import WebSocketTransport
from .types import RemoteConfig, TransportMode


class OwlBrowser:
    """
    Main client for Owl Browser automation.

    This class provides the primary interface for interacting with the
    Owl Browser HTTP server. It dynamically generates methods for all
    available browser tools based on the OpenAPI schema.

    Features:
    - Dynamic method generation from OpenAPI schema
    - Async-first with sync wrappers for convenience
    - Automatic type coercion for integer fields
    - Connection pooling and retry logic

    Example:
        ```python
        from owl_browser import OwlBrowser, RemoteConfig

        # Async usage (recommended)
        async def main():
            config = RemoteConfig(
                url="http://localhost:8080",
                token="your-secret-token"
            )
            async with OwlBrowser(config) as browser:
                # Create a context
                ctx = await browser.create_context()
                context_id = ctx["context_id"]

                # Navigate to a page
                await browser.navigate(context_id=context_id, url="https://example.com")

                # Take a screenshot
                screenshot = await browser.screenshot(context_id=context_id)

                # Close the context
                await browser.close_context(context_id=context_id)

        # Sync usage (convenience wrapper)
        browser = OwlBrowser(config)
        browser.connect_sync()
        ctx = browser.execute_sync("browser_create_context")
        browser.close_sync()
        ```
    """

    __slots__ = (
        "_config",
        "_transport",
        "_openapi",
        "_tools",
        "_connected",
        "_dynamic_methods",
    )

    def __init__(self, config: RemoteConfig) -> None:
        """
        Initialize OwlBrowser client.

        Args:
            config: Remote server configuration.
        """
        self._config = config
        self._connected = False

        if config.transport == TransportMode.WEBSOCKET:
            self._transport: HTTPTransport | WebSocketTransport = WebSocketTransport(config)
        else:
            self._transport = HTTPTransport(config)

        self._openapi: OpenAPILoader | None = None
        self._tools: dict[str, ToolDefinition] = {}
        self._dynamic_methods: dict[str, Callable[..., Coroutine[Any, Any, Any]]] = {}

        self._setup_bundled_methods()

    async def __aenter__(self) -> OwlBrowser:
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
        Connect to the browser server.

        This method initializes the transport connection. The OpenAPI schema
        is loaded from the bundled file at initialization, so this method
        does not need to fetch it from the server.

        Raises:
            ConnectionError: If connection fails.
        """
        if self._connected:
            return

        if isinstance(self._transport, WebSocketTransport):
            await self._transport.connect()
        else:
            await self._transport.__aenter__()

        self._connected = True

    async def close(self) -> None:
        """Close the connection and release resources."""
        if not self._connected:
            return

        await self._transport.close()
        self._connected = False

    def _setup_bundled_methods(self) -> None:
        """Setup methods from bundled schema for offline use."""
        self._openapi = OpenAPILoader(get_bundled_schema())
        self._tools = self._openapi.tools
        self._setup_dynamic_methods()

    def _setup_dynamic_methods(self) -> None:
        """Generate dynamic methods for all tools."""
        for tool_name, tool_def in self._tools.items():
            method_name = tool_name.replace("browser_", "")
            if method_name not in self._dynamic_methods:
                method = self._create_tool_method(tool_name, tool_def)
                self._dynamic_methods[method_name] = method

    def __getattr__(self, name: str) -> Callable[..., Coroutine[Any, Any, Any]]:
        """Return dynamic method if it exists."""
        # Access _dynamic_methods via object.__getattribute__ to avoid recursion
        try:
            dynamic_methods = object.__getattribute__(self, "_dynamic_methods")
            if name in dynamic_methods:
                return dynamic_methods[name]
        except AttributeError:
            pass
        raise AttributeError(f"'{type(self).__name__}' object has no attribute '{name}'")

    def _create_tool_method(
        self, tool_name: str, tool_def: ToolDefinition
    ) -> Callable[..., Coroutine[Any, Any, Any]]:
        """Create an async method for a tool."""

        async def method(**kwargs: Any) -> Any:
            return await self.execute(tool_name, **kwargs)

        method.__doc__ = f"""
{tool_def.description}

Args:
{self._format_params_doc(tool_def)}

Returns:
    Tool execution result.
"""
        method.__name__ = tool_name.replace("browser_", "")
        return method

    def _format_params_doc(self, tool_def: ToolDefinition) -> str:
        """Format parameter documentation."""
        lines = []
        for name, param in tool_def.parameters.items():
            required = " (required)" if param.required else ""
            lines.append(f"    {name}: {param.description}{required}")
        return "\n".join(lines) if lines else "    None"

    async def execute(self, tool_name: str, **params: Any) -> Any:
        """
        Execute any browser tool by name.

        This is the low-level method for executing tools. You can also
        use the dynamically generated convenience methods like
        `navigate()`, `click()`, etc.

        Args:
            tool_name: Name of the tool (e.g., 'browser_navigate').
            **params: Tool parameters.

        Returns:
            Tool execution result.

        Raises:
            ToolExecutionError: If tool execution fails.

        Example:
            ```python
            # Using execute() directly
            result = await browser.execute(
                "browser_navigate",
                context_id="ctx_1",
                url="https://example.com"
            )

            # Or using the dynamic method
            result = await browser.navigate(
                context_id="ctx_1",
                url="https://example.com"
            )
            ```
        """
        tool_def = self._tools.get(tool_name)
        if tool_def:
            params = coerce_integer_fields(tool_def, params)

        return await self._transport.execute(tool_name, params)

    async def health_check(self) -> dict[str, Any]:
        """
        Check server health status.

        Returns:
            Health status dictionary.
        """
        if isinstance(self._transport, HTTPTransport):
            return await self._transport.health_check()
        raise OwlBrowserError("Health check not supported over WebSocket")

    def get_tool(self, name: str) -> ToolDefinition | None:
        """
        Get tool definition by name.

        Args:
            name: Tool name.

        Returns:
            ToolDefinition or None if not found.
        """
        return self._tools.get(name)

    def list_tools(self) -> list[str]:
        """
        List all available tool names.

        Returns:
            List of tool names.
        """
        return list(self._tools.keys())

    def list_methods(self) -> list[str]:
        """
        List all available dynamic method names.

        Returns:
            List of method names (without 'browser_' prefix).
        """
        return list(self._dynamic_methods.keys())

    def has_method(self, name: str) -> bool:
        """
        Check if a dynamic method exists.

        Args:
            name: Method name (without 'browser_' prefix).

        Returns:
            True if the method exists.
        """
        return name in self._dynamic_methods

    def connect_sync(self) -> None:
        """
        Synchronous version of connect().

        Convenience method for non-async code.
        """
        asyncio.run(self.connect())

    def close_sync(self) -> None:
        """
        Synchronous version of close().

        Convenience method for non-async code.
        """
        asyncio.run(self.close())

    def execute_sync(self, tool_name: str, **params: Any) -> Any:
        """
        Synchronous version of execute().

        Convenience method for non-async code.

        Args:
            tool_name: Name of the tool.
            **params: Tool parameters.

        Returns:
            Tool execution result.
        """
        return asyncio.run(self.execute(tool_name, **params))
