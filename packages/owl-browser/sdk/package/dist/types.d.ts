/**
 * Type definitions for Owl Browser SDK v2.
 *
 * This module contains all type definitions, interfaces, and enums
 * using strict TypeScript types.
 */
/**
 * Authentication mode for remote HTTP server.
 */
export declare enum AuthMode {
    TOKEN = "token",
    JWT = "jwt"
}
/**
 * Transport mode for remote connections.
 */
export declare enum TransportMode {
    HTTP = "http",
    WEBSOCKET = "websocket"
}
/**
 * Condition operators for comparing values in flow conditions.
 */
export declare enum ConditionOperator {
    EQUALS = "equals",
    NOT_EQUALS = "not_equals",
    CONTAINS = "contains",
    NOT_CONTAINS = "not_contains",
    STARTS_WITH = "starts_with",
    ENDS_WITH = "ends_with",
    GREATER_THAN = "greater_than",
    LESS_THAN = "less_than",
    IS_TRUTHY = "is_truthy",
    IS_FALSY = "is_falsy",
    IS_EMPTY = "is_empty",
    IS_NOT_EMPTY = "is_not_empty",
    REGEX_MATCH = "regex_match"
}
/**
 * Flow step execution status.
 */
export declare enum FlowStepStatus {
    PENDING = "pending",
    RUNNING = "running",
    SUCCESS = "success",
    WARNING = "warning",
    ERROR = "error",
    SKIPPED = "skipped"
}
/**
 * Action status codes returned by the browser.
 */
export declare enum ActionStatus {
    OK = "ok",
    BROWSER_NOT_FOUND = "browser_not_found",
    BROWSER_NOT_READY = "browser_not_ready",
    CONTEXT_NOT_FOUND = "context_not_found",
    NAVIGATION_FAILED = "navigation_failed",
    NAVIGATION_TIMEOUT = "navigation_timeout",
    PAGE_LOAD_ERROR = "page_load_error",
    REDIRECT_DETECTED = "redirect_detected",
    CAPTCHA_DETECTED = "captcha_detected",
    FIREWALL_DETECTED = "firewall_detected",
    ELEMENT_NOT_FOUND = "element_not_found",
    ELEMENT_NOT_VISIBLE = "element_not_visible",
    ELEMENT_NOT_INTERACTABLE = "element_not_interactable",
    ELEMENT_STALE = "element_stale",
    MULTIPLE_ELEMENTS = "multiple_elements",
    CLICK_FAILED = "click_failed",
    CLICK_INTERCEPTED = "click_intercepted",
    TYPE_FAILED = "type_failed",
    TYPE_PARTIAL = "type_partial",
    SCROLL_FAILED = "scroll_failed",
    FOCUS_FAILED = "focus_failed",
    BLUR_FAILED = "blur_failed",
    CLEAR_FAILED = "clear_failed",
    PICK_FAILED = "pick_failed",
    OPTION_NOT_FOUND = "option_not_found",
    UPLOAD_FAILED = "upload_failed",
    FRAME_SWITCH_FAILED = "frame_switch_failed",
    TAB_SWITCH_FAILED = "tab_switch_failed",
    DIALOG_NOT_HANDLED = "dialog_not_handled",
    INVALID_SELECTOR = "invalid_selector",
    INVALID_URL = "invalid_url",
    INVALID_PARAMETER = "invalid_parameter",
    INTERNAL_ERROR = "internal_error",
    TIMEOUT = "timeout",
    NETWORK_TIMEOUT = "network_timeout",
    WAIT_TIMEOUT = "wait_timeout",
    VERIFICATION_TIMEOUT = "verification_timeout",
    UNKNOWN = "unknown"
}
/**
 * Configuration for JWT authentication with automatic token generation.
 */
export interface JWTConfig {
    /** Path to RSA private key file (PEM format) or PEM string. */
    readonly privateKeyPath: string;
    /** Token validity duration in seconds (default: 3600 = 1 hour). */
    readonly expiresIn?: number;
    /** Seconds before expiry to auto-refresh (default: 300). */
    readonly refreshThreshold?: number;
    /** Issuer claim (iss). */
    readonly issuer?: string;
    /** Subject claim (sub). */
    readonly subject?: string;
    /** Audience claim (aud). */
    readonly audience?: string;
    /** Additional custom claims. */
    readonly claims?: Record<string, unknown>;
}
/**
 * Configuration for retry behavior with exponential backoff.
 */
export interface RetryConfig {
    /** Maximum number of retry attempts. */
    readonly maxRetries?: number;
    /** Initial delay in milliseconds. */
    readonly initialDelayMs?: number;
    /** Maximum delay cap in milliseconds. */
    readonly maxDelayMs?: number;
    /** Multiplier for exponential backoff. */
    readonly backoffMultiplier?: number;
    /** Random jitter factor (0-1). */
    readonly jitterFactor?: number;
}
/**
 * Configuration for connecting to a remote Owl Browser HTTP server.
 *
 * Supports two authentication modes:
 * - TOKEN (default): Simple bearer token authentication.
 * - JWT: JSON Web Token authentication with RSA signing.
 *
 * @example
 * ```typescript
 * import { OwlBrowser, RemoteConfig, AuthMode } from '@olib-ai/owl-browser-sdk';
 *
 * // Simple token authentication
 * const browser = new OwlBrowser({
 *   url: 'http://localhost:8080',
 *   token: 'your-secret-token'
 * });
 *
 * // JWT authentication
 * const browser2 = new OwlBrowser({
 *   url: 'http://localhost:8080',
 *   authMode: AuthMode.JWT,
 *   jwt: { privateKeyPath: '/path/to/private.pem' }
 * });
 * ```
 */
export interface RemoteConfig {
    /** Server URL (e.g., 'http://localhost:8080'). */
    readonly url: string;
    /** Bearer token for TOKEN authentication. */
    readonly token?: string;
    /** Authentication mode (TOKEN or JWT). */
    readonly authMode?: AuthMode;
    /** JWT configuration for JWT authentication. */
    readonly jwt?: JWTConfig;
    /** Transport mode (HTTP or WEBSOCKET). */
    readonly transport?: TransportMode;
    /** Request timeout in seconds (default: 30). */
    readonly timeout?: number;
    /** Maximum concurrent requests (default: 10). */
    readonly maxConcurrent?: number;
    /** Retry configuration. */
    readonly retry?: RetryConfig;
    /** Whether to verify SSL certificates (default: true). */
    readonly verifySsl?: boolean;
    /** API prefix (default: '/api', use '' for direct connection). */
    readonly apiPrefix?: string;
}
/**
 * Definition of a tool parameter from OpenAPI schema.
 */
export interface ParameterDef {
    readonly name: string;
    readonly type: string;
    readonly required: boolean;
    readonly description: string;
    readonly enumValues?: readonly string[];
    readonly default?: unknown;
}
/**
 * Definition of a browser tool from OpenAPI schema.
 */
export interface ToolDefinition {
    readonly name: string;
    readonly description: string;
    readonly parameters: Readonly<Record<string, ParameterDef>>;
    readonly requiredParams: readonly string[];
    readonly integerFields: ReadonlySet<string>;
}
/**
 * Expectation types for validating tool results.
 */
export interface StepExpectation {
    /** Exact match value. */
    equals?: unknown;
    /** String must contain this substring. */
    contains?: string;
    /** Array length check. */
    length?: number;
    /** Value must be greater than this number. */
    greaterThan?: number;
    /** Value must be less than this number. */
    lessThan?: number;
    /** Value must not be null/undefined/empty. */
    notEmpty?: boolean;
    /** Nested field path to check (e.g., 'data.count'). */
    field?: string;
    /** Regex pattern to match. */
    matches?: string;
}
/**
 * Result of an expectation check.
 */
export interface ExpectationResult {
    readonly passed: boolean;
    readonly message: string;
    readonly expected?: unknown;
    readonly actual?: unknown;
}
/**
 * Condition configuration for conditional flow steps.
 */
export interface FlowCondition {
    /** What to check - 'previous' for last step result, 'step' for specific step. */
    source: 'previous' | 'step';
    /** Comparison operator. */
    operator: ConditionOperator;
    /** Step ID to check when source is 'step'. */
    sourceStepId?: string;
    /** Field path in result to check. */
    field?: string;
    /** Value to compare against. */
    value?: unknown;
}
/**
 * A step in a flow.
 */
export interface FlowStep {
    /** Unique identifier for the step. */
    id: string;
    /** Tool type (e.g., 'browser_navigate', 'browser_click', 'condition'). */
    type: string;
    /** Whether this step is enabled (default: true). */
    enabled: boolean;
    /** Tool parameters. */
    params: Record<string, unknown>;
    /** Human-readable description of what this step does. */
    description?: string;
    /** Expectation for result validation. */
    expected?: StepExpectation;
    /** Condition for conditional steps. */
    condition?: FlowCondition;
    /** Steps to execute if condition is true. */
    onTrue?: FlowStep[];
    /** Steps to execute if condition is false. */
    onFalse?: FlowStep[];
}
/**
 * Flow definition containing a sequence of steps.
 */
export interface Flow {
    /** Flow name. */
    name: string;
    /** Optional description. */
    description?: string;
    /** List of steps in the flow. */
    steps: FlowStep[];
}
/**
 * Result of executing a single flow step.
 */
export interface StepResult {
    /** Index of the step in the flow. */
    readonly stepIndex: number;
    /** Unique identifier of the step. */
    readonly stepId: string;
    /** Name of the tool executed. */
    readonly toolName: string;
    /** Whether the step succeeded. */
    readonly success: boolean;
    /** The result data from the tool. */
    readonly result?: unknown;
    /** Error message if the step failed. */
    readonly error?: string;
    /** Execution time in milliseconds. */
    readonly durationMs: number;
    /** Result of expectation validation (if applicable). */
    readonly expectationResult?: ExpectationResult;
    /** Which branch was taken for condition steps. */
    readonly branchTaken?: 'true' | 'false';
}
/**
 * Result of executing a complete flow.
 */
export interface FlowResult {
    /** Whether the entire flow succeeded. */
    readonly success: boolean;
    /** List of step results. */
    readonly steps: readonly StepResult[];
    /** Total execution time in milliseconds. */
    readonly totalDurationMs: number;
    /** Error message if the flow failed. */
    readonly error?: string;
}
/**
 * ActionResult returned by browser for validated actions.
 */
export interface ActionResult {
    readonly success: boolean;
    readonly status: string;
    readonly message: string;
    readonly selector?: string;
    readonly url?: string;
    readonly errorCode?: string;
    readonly httpStatus?: number;
    readonly elementCount?: number;
}
/**
 * Health check response.
 */
export interface HealthResponse {
    status: string;
    browserReady?: boolean;
    version?: string;
    uptime?: number;
}
/**
 * Generic tool execution result.
 */
export type ToolResult = Record<string, unknown>;
//# sourceMappingURL=types.d.ts.map