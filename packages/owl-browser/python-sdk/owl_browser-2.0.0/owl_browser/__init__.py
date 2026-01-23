"""
Owl Browser Python SDK v2.

Async-first SDK for browser automation with dynamic OpenAPI method generation.

Example:
    ```python
    from owl_browser import OwlBrowser, RemoteConfig

    async def main():
        config = RemoteConfig(
            url="http://localhost:8080",
            token="your-secret-token"
        )

        async with OwlBrowser(config) as browser:
            # Create a context
            ctx = await browser.create_context()
            context_id = ctx["context_id"]

            # Navigate and interact
            await browser.navigate(context_id=context_id, url="https://example.com")
            await browser.click(context_id=context_id, selector="button#submit")

            # Clean up
            await browser.close_context(context_id=context_id)

    import asyncio
    asyncio.run(main())
    ```
"""

from .client import OwlBrowser
from .types import (
    AuthMode,
    ConditionOperator,
    ExpectationResult,
    Flow,
    FlowCondition,
    FlowResult,
    FlowStep,
    FlowStepStatus,
    JWTConfig,
    ParameterDef,
    RemoteConfig,
    RetryConfig,
    StepExpectation,
    StepResult,
    ToolDefinition,
    TransportMode,
)
from .exceptions import (
    AuthenticationError,
    ConnectionError,
    ContextLimitError,
    ElementNotFoundError,
    ExpectationError,
    FlowExecutionError,
    IPBlockedError,
    NavigationError,
    OpenAPISchemaError,
    OwlBrowserError,
    RateLimitError,
    TimeoutError,
    ToolExecutionError,
)
from .flow import FlowExecutor, check_expectation, evaluate_condition, resolve_variables
from .openapi import OpenAPILoader


__version__ = "2.0.0"

__all__ = [
    # Main client
    "OwlBrowser",
    # Configuration
    "RemoteConfig",
    "AuthMode",
    "TransportMode",
    "JWTConfig",
    "RetryConfig",
    # OpenAPI
    "OpenAPILoader",
    "ToolDefinition",
    "ParameterDef",
    # Flow execution
    "FlowExecutor",
    "Flow",
    "FlowStep",
    "FlowCondition",
    "FlowResult",
    "StepResult",
    "StepExpectation",
    "ExpectationResult",
    "FlowStepStatus",
    "ConditionOperator",
    # Flow utilities
    "check_expectation",
    "evaluate_condition",
    "resolve_variables",
    # Exceptions
    "OwlBrowserError",
    "ConnectionError",
    "AuthenticationError",
    "ToolExecutionError",
    "TimeoutError",
    "RateLimitError",
    "IPBlockedError",
    "ContextLimitError",
    "ElementNotFoundError",
    "NavigationError",
    "FlowExecutionError",
    "ExpectationError",
    "OpenAPISchemaError",
]
