/**
 * Owl Browser SDK v2 Exceptions.
 *
 * Custom exception classes for better error handling and reporting.
 * Provides detailed error information for debugging and recovery.
 */
import { ActionStatus } from './types.js';
/**
 * Base exception for all Owl Browser SDK errors.
 */
export class OwlBrowserError extends Error {
    constructor(message) {
        super(message);
        this.name = 'OwlBrowserError';
        Object.setPrototypeOf(this, new.target.prototype);
    }
}
/**
 * Raised when connection to the browser server fails.
 */
export class ConnectionError extends OwlBrowserError {
    cause;
    constructor(message, cause) {
        super(message);
        this.name = 'ConnectionError';
        this.cause = cause;
    }
}
/**
 * Raised when authentication fails (401 Unauthorized).
 */
export class AuthenticationError extends OwlBrowserError {
    reason;
    statusCode = 401;
    constructor(message, reason) {
        super(AuthenticationError.formatMessage(message, reason));
        this.name = 'AuthenticationError';
        this.reason = reason;
    }
    static formatMessage(message, reason) {
        const lines = [`Authentication Error: ${message}`];
        if (reason) {
            lines.push(`Reason: ${reason}`);
        }
        return lines.join('\n');
    }
}
/**
 * Raised when a browser tool execution fails.
 */
export class ToolExecutionError extends OwlBrowserError {
    toolName;
    status;
    result;
    constructor(toolName, message, status, result) {
        super(`Tool '${toolName}' failed: ${message}`);
        this.name = 'ToolExecutionError';
        this.toolName = toolName;
        this.status = status;
        this.result = result;
    }
}
/**
 * Raised when an operation times out.
 */
export class TimeoutError extends OwlBrowserError {
    timeoutMs;
    constructor(message, timeoutMs) {
        super(message);
        this.name = 'TimeoutError';
        this.timeoutMs = timeoutMs;
    }
}
/**
 * Raised when the client is rate limited (429 Too Many Requests).
 */
export class RateLimitError extends OwlBrowserError {
    retryAfter;
    limit;
    remaining;
    statusCode = 429;
    constructor(message, retryAfter = 60, limit, remaining) {
        super(`Rate limit exceeded: ${message}. Retry after ${retryAfter}s`);
        this.name = 'RateLimitError';
        this.retryAfter = retryAfter;
        this.limit = limit;
        this.remaining = remaining;
    }
}
/**
 * Raised when the client IP is blocked (403 Forbidden).
 */
export class IPBlockedError extends OwlBrowserError {
    ipAddress;
    statusCode = 403;
    constructor(message, ipAddress) {
        super(`IP blocked: ${message}`);
        this.name = 'IPBlockedError';
        this.ipAddress = ipAddress;
    }
}
/**
 * Raised when context limit is exceeded for the license.
 */
export class ContextLimitError extends OwlBrowserError {
    currentContexts;
    maxContexts;
    constructor(message, currentContexts, maxContexts) {
        super(message);
        this.name = 'ContextLimitError';
        this.currentContexts = currentContexts;
        this.maxContexts = maxContexts;
    }
}
/**
 * Raised when an element cannot be found on the page.
 */
export class ElementNotFoundError extends OwlBrowserError {
    selector;
    constructor(selector, message) {
        super(message ?? `Element not found: ${selector}`);
        this.name = 'ElementNotFoundError';
        this.selector = selector;
    }
}
/**
 * Raised when page navigation fails.
 */
export class NavigationError extends OwlBrowserError {
    url;
    statusCode;
    constructor(url, message, statusCode) {
        super(`Navigation to ${url} failed: ${message}`);
        this.name = 'NavigationError';
        this.url = url;
        this.statusCode = statusCode;
    }
}
/**
 * Raised when flow execution fails.
 */
export class FlowExecutionError extends OwlBrowserError {
    stepIndex;
    toolName;
    result;
    constructor(stepIndex, toolName, message, result) {
        super(`Flow failed at step ${stepIndex} (${toolName}): ${message}`);
        this.name = 'FlowExecutionError';
        this.stepIndex = stepIndex;
        this.toolName = toolName;
        this.result = result;
    }
}
/**
 * Raised when an expectation validation fails.
 */
export class ExpectationError extends OwlBrowserError {
    expected;
    actual;
    field;
    constructor(message, expected, actual, field) {
        super(message);
        this.name = 'ExpectationError';
        this.expected = expected;
        this.actual = actual;
        this.field = field;
    }
}
/**
 * Raised when OpenAPI schema loading or parsing fails.
 */
export class OpenAPISchemaError extends OwlBrowserError {
    cause;
    constructor(message, cause) {
        super(message);
        this.name = 'OpenAPISchemaError';
        this.cause = cause;
    }
}
/**
 * Check if a result is an ActionResult object or dict.
 */
export function isActionResult(result) {
    if (typeof result !== 'object' || result === null) {
        return false;
    }
    const obj = result;
    return ('success' in obj &&
        'status' in obj &&
        typeof obj['success'] === 'boolean');
}
/**
 * Check if result is a failed ActionResult and raise appropriate exception.
 */
export function raiseForActionResult(result) {
    if (!isActionResult(result)) {
        return;
    }
    if (result.success) {
        return;
    }
    if (result.status === ActionStatus.ELEMENT_NOT_FOUND && result.selector) {
        throw new ElementNotFoundError(result.selector, result.message);
    }
    const navigationStatuses = new Set([
        ActionStatus.NAVIGATION_FAILED,
        ActionStatus.NAVIGATION_TIMEOUT,
        ActionStatus.PAGE_LOAD_ERROR,
    ]);
    if (navigationStatuses.has(result.status)) {
        throw new NavigationError(result.url ?? 'unknown', result.message, result.httpStatus);
    }
    throw new ToolExecutionError('unknown', result.message, result.status, result);
}
//# sourceMappingURL=errors.js.map