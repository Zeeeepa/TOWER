/**
 * Owl Browser SDK v2 Exceptions.
 *
 * Custom exception classes for better error handling and reporting.
 * Provides detailed error information for debugging and recovery.
 */
import { ActionResult } from './types.js';
/**
 * Base exception for all Owl Browser SDK errors.
 */
export declare class OwlBrowserError extends Error {
    constructor(message: string);
}
/**
 * Raised when connection to the browser server fails.
 */
export declare class ConnectionError extends OwlBrowserError {
    readonly cause?: Error;
    constructor(message: string, cause?: Error);
}
/**
 * Raised when authentication fails (401 Unauthorized).
 */
export declare class AuthenticationError extends OwlBrowserError {
    readonly reason?: string;
    readonly statusCode = 401;
    constructor(message: string, reason?: string);
    private static formatMessage;
}
/**
 * Raised when a browser tool execution fails.
 */
export declare class ToolExecutionError extends OwlBrowserError {
    readonly toolName: string;
    readonly status?: string;
    readonly result?: unknown;
    constructor(toolName: string, message: string, status?: string, result?: unknown);
}
/**
 * Raised when an operation times out.
 */
export declare class TimeoutError extends OwlBrowserError {
    readonly timeoutMs?: number;
    constructor(message: string, timeoutMs?: number);
}
/**
 * Raised when the client is rate limited (429 Too Many Requests).
 */
export declare class RateLimitError extends OwlBrowserError {
    readonly retryAfter: number;
    readonly limit?: number;
    readonly remaining?: number;
    readonly statusCode = 429;
    constructor(message: string, retryAfter?: number, limit?: number, remaining?: number);
}
/**
 * Raised when the client IP is blocked (403 Forbidden).
 */
export declare class IPBlockedError extends OwlBrowserError {
    readonly ipAddress?: string;
    readonly statusCode = 403;
    constructor(message: string, ipAddress?: string);
}
/**
 * Raised when context limit is exceeded for the license.
 */
export declare class ContextLimitError extends OwlBrowserError {
    readonly currentContexts?: number;
    readonly maxContexts?: number;
    constructor(message: string, currentContexts?: number, maxContexts?: number);
}
/**
 * Raised when an element cannot be found on the page.
 */
export declare class ElementNotFoundError extends OwlBrowserError {
    readonly selector: string;
    constructor(selector: string, message?: string);
}
/**
 * Raised when page navigation fails.
 */
export declare class NavigationError extends OwlBrowserError {
    readonly url: string;
    readonly statusCode?: number;
    constructor(url: string, message: string, statusCode?: number);
}
/**
 * Raised when flow execution fails.
 */
export declare class FlowExecutionError extends OwlBrowserError {
    readonly stepIndex: number;
    readonly toolName: string;
    readonly result?: unknown;
    constructor(stepIndex: number, toolName: string, message: string, result?: unknown);
}
/**
 * Raised when an expectation validation fails.
 */
export declare class ExpectationError extends OwlBrowserError {
    readonly expected: unknown;
    readonly actual: unknown;
    readonly field?: string;
    constructor(message: string, expected: unknown, actual: unknown, field?: string);
}
/**
 * Raised when OpenAPI schema loading or parsing fails.
 */
export declare class OpenAPISchemaError extends OwlBrowserError {
    readonly cause?: Error;
    constructor(message: string, cause?: Error);
}
/**
 * Check if a result is an ActionResult object or dict.
 */
export declare function isActionResult(result: unknown): result is ActionResult;
/**
 * Check if result is a failed ActionResult and raise appropriate exception.
 */
export declare function raiseForActionResult(result: unknown): void;
//# sourceMappingURL=errors.d.ts.map