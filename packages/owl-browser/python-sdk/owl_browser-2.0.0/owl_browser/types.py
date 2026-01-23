"""
Type definitions for Owl Browser SDK v2.

This module contains all type definitions, dataclasses, and enums
using modern Python 3.12+ syntax with strict typing.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import StrEnum
from typing import Any, Literal


# ==================== ENUMS ====================


class AuthMode(StrEnum):
    """Authentication mode for remote HTTP server."""

    TOKEN = "token"
    JWT = "jwt"


class TransportMode(StrEnum):
    """Transport mode for remote connections."""

    HTTP = "http"
    WEBSOCKET = "websocket"


class ConditionOperator(StrEnum):
    """Condition operators for comparing values in flow conditions."""

    EQUALS = "equals"
    NOT_EQUALS = "not_equals"
    CONTAINS = "contains"
    NOT_CONTAINS = "not_contains"
    STARTS_WITH = "starts_with"
    ENDS_WITH = "ends_with"
    GREATER_THAN = "greater_than"
    LESS_THAN = "less_than"
    IS_TRUTHY = "is_truthy"
    IS_FALSY = "is_falsy"
    IS_EMPTY = "is_empty"
    IS_NOT_EMPTY = "is_not_empty"
    REGEX_MATCH = "regex_match"


class FlowStepStatus(StrEnum):
    """Flow step execution status."""

    PENDING = "pending"
    RUNNING = "running"
    SUCCESS = "success"
    WARNING = "warning"
    ERROR = "error"
    SKIPPED = "skipped"


# ==================== CONFIGURATION CLASSES ====================


@dataclass(frozen=True, slots=True)
class JWTConfig:
    """
    Configuration for JWT authentication with automatic token generation.

    Attributes:
        private_key_path: Path to RSA private key file (PEM format).
        expires_in: Token validity duration in seconds (default: 3600 = 1 hour).
        refresh_threshold: Seconds before expiry to auto-refresh (default: 300).
        issuer: Issuer claim (iss).
        subject: Subject claim (sub).
        audience: Audience claim (aud).
        claims: Additional custom claims.
    """

    private_key_path: str
    expires_in: int = 3600
    refresh_threshold: int = 300
    issuer: str | None = None
    subject: str | None = None
    audience: str | None = None
    claims: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True, slots=True)
class RetryConfig:
    """
    Configuration for retry behavior with exponential backoff.

    Attributes:
        max_retries: Maximum number of retry attempts.
        initial_delay_ms: Initial delay in milliseconds.
        max_delay_ms: Maximum delay cap in milliseconds.
        backoff_multiplier: Multiplier for exponential backoff.
        jitter_factor: Random jitter factor (0-1).
    """

    max_retries: int = 3
    initial_delay_ms: int = 100
    max_delay_ms: int = 10000
    backoff_multiplier: float = 2.0
    jitter_factor: float = 0.1


@dataclass(slots=True)
class RemoteConfig:
    """
    Configuration for connecting to a remote Owl Browser HTTP server.

    Supports two authentication modes:
    - TOKEN (default): Simple bearer token authentication.
    - JWT: JSON Web Token authentication with RSA signing.

    Example:
        ```python
        from owl_browser import OwlBrowser, RemoteConfig

        # Simple token authentication
        browser = OwlBrowser(RemoteConfig(
            url="http://localhost:8080",
            token="your-secret-token"
        ))

        # JWT authentication
        browser = OwlBrowser(RemoteConfig(
            url="http://localhost:8080",
            auth_mode=AuthMode.JWT,
            jwt=JWTConfig(private_key_path="/path/to/private.pem")
        ))
        ```
    """

    url: str
    token: str | None = None
    auth_mode: AuthMode = AuthMode.TOKEN
    jwt: JWTConfig | None = None
    transport: TransportMode = TransportMode.HTTP
    timeout: float = 30.0
    max_concurrent: int = 10
    retry: RetryConfig = field(default_factory=RetryConfig)
    verify_ssl: bool = True
    api_prefix: str = "/api"

    def __post_init__(self) -> None:
        """Validate configuration after initialization."""
        self.url = self.url.rstrip("/")
        if self.api_prefix:
            if not self.api_prefix.startswith("/"):
                self.api_prefix = "/" + self.api_prefix
            self.api_prefix = self.api_prefix.rstrip("/")

        if self.auth_mode == AuthMode.TOKEN and not self.token:
            raise ValueError("Token is required for TOKEN authentication mode")
        if self.auth_mode == AuthMode.JWT and not self.jwt:
            raise ValueError("JWTConfig is required for JWT authentication mode")


# ==================== OPENAPI TYPES ====================


@dataclass(frozen=True, slots=True)
class ParameterDef:
    """Definition of a tool parameter from OpenAPI schema."""

    name: str
    type: str
    required: bool
    description: str
    enum_values: list[str] | None = None
    default: Any = None


@dataclass(frozen=True, slots=True)
class ToolDefinition:
    """Definition of a browser tool from OpenAPI schema."""

    name: str
    description: str
    parameters: dict[str, ParameterDef]
    required_params: list[str]
    integer_fields: frozenset[str]


# ==================== FLOW TYPES ====================


@dataclass(slots=True)
class StepExpectation:
    """
    Expectation types for validating tool results.

    Attributes:
        equals: Exact match value.
        contains: String must contain this substring.
        length: Array length check.
        greater_than: Value must be greater than this number.
        less_than: Value must be less than this number.
        not_empty: Value must not be null/undefined/empty.
        field: Nested field path to check (e.g., "data.count").
        matches: Regex pattern to match.
    """

    equals: Any = None
    contains: str | None = None
    length: int | None = None
    greater_than: float | None = None
    less_than: float | None = None
    not_empty: bool | None = None
    field: str | None = None
    matches: str | None = None


@dataclass(slots=True)
class ExpectationResult:
    """Result of an expectation check."""

    passed: bool
    message: str
    expected: Any = None
    actual: Any = None


@dataclass(slots=True)
class FlowCondition:
    """
    Condition configuration for conditional flow steps.

    Attributes:
        source: What to check - 'previous' for last step result, 'step' for specific step.
        operator: Comparison operator.
        source_step_id: Step ID to check when source is 'step'.
        field: Field path in result to check.
        value: Value to compare against (not needed for is_truthy, is_falsy, etc.).
    """

    source: Literal["previous", "step"]
    operator: ConditionOperator
    source_step_id: str | None = None
    field: str | None = None
    value: Any = None


@dataclass(slots=True)
class FlowStep:
    """
    A step in a flow.

    This represents a single action in a flow, which can be a tool execution
    or a conditional branch.

    Attributes:
        id: Unique identifier for the step.
        type: Tool type (e.g., 'browser_navigate', 'browser_click', 'condition').
        enabled: Whether this step is enabled (default: True).
        params: Tool parameters.
        description: Human-readable description of what this step does.
        expected: Expectation for result validation.
        condition: Condition for conditional steps.
        on_true: Steps to execute if condition is true.
        on_false: Steps to execute if condition is false.
    """

    id: str
    type: str
    enabled: bool = True
    params: dict[str, Any] = field(default_factory=dict)
    description: str | None = None
    expected: StepExpectation | None = None
    condition: FlowCondition | None = None
    on_true: list[FlowStep] | None = None
    on_false: list[FlowStep] | None = None


@dataclass(slots=True)
class Flow:
    """
    Flow definition containing a sequence of steps.

    Attributes:
        name: Flow name.
        description: Optional description.
        steps: List of steps in the flow.
    """

    name: str
    steps: list[FlowStep]
    description: str | None = None


@dataclass(slots=True)
class StepResult:
    """
    Result of executing a single flow step.

    Attributes:
        step_index: Index of the step in the flow.
        step_id: Unique identifier of the step.
        tool_name: Name of the tool executed.
        success: Whether the step succeeded.
        result: The result data from the tool.
        error: Error message if the step failed.
        duration_ms: Execution time in milliseconds.
        expectation_result: Result of expectation validation (if applicable).
        branch_taken: Which branch was taken for condition steps.
    """

    step_index: int
    step_id: str
    tool_name: str
    success: bool
    result: Any = None
    error: str | None = None
    duration_ms: float = 0
    expectation_result: ExpectationResult | None = None
    branch_taken: Literal["true", "false"] | None = None


@dataclass(slots=True)
class FlowResult:
    """
    Result of executing a complete flow.

    Attributes:
        success: Whether the entire flow succeeded.
        steps: List of step results.
        total_duration_ms: Total execution time in milliseconds.
        error: Error message if the flow failed.
    """

    success: bool
    steps: list[StepResult]
    total_duration_ms: float
    error: str | None = None
