/**
 * Owl Browser Node.js SDK v2.
 *
 * Async-first SDK for browser automation with dynamic OpenAPI method generation.
 *
 * @example
 * ```typescript
 * import { OwlBrowser, RemoteConfig } from '@olib-ai/owl-browser-sdk';
 *
 * const browser = new OwlBrowser({
 *   url: 'http://localhost:8080',
 *   token: 'your-secret-token'
 * });
 *
 * await browser.connect();
 *
 * // Create a context
 * const ctx = await browser.createContext();
 * const contextId = ctx.context_id;
 *
 * // Navigate and interact
 * await browser.navigate({ context_id: contextId, url: 'https://example.com' });
 * await browser.click({ context_id: contextId, selector: 'button#submit' });
 *
 * // Clean up
 * await browser.closeContext({ context_id: contextId });
 * await browser.close();
 * ```
 */
// Main client
export { OwlBrowser } from './client.js';
// Types
export { 
// Enums
AuthMode, TransportMode, ConditionOperator, FlowStepStatus, ActionStatus, } from './types.js';
// Errors
export { OwlBrowserError, ConnectionError, AuthenticationError, ToolExecutionError, TimeoutError, RateLimitError, IPBlockedError, ContextLimitError, ElementNotFoundError, NavigationError, FlowExecutionError, ExpectationError, OpenAPISchemaError, isActionResult, raiseForActionResult, } from './errors.js';
// OpenAPI
export { OpenAPILoader, getBundledSchema, coerceIntegerFields } from './openapi.js';
// Flow execution
export { FlowExecutor, resolveVariables, getValueAtPath, checkExpectation, evaluateCondition, } from './flow/index.js';
// Transport (for advanced use)
export { HTTPTransport } from './transport/http.js';
export { WebSocketTransport } from './transport/websocket.js';
// Auth (for advanced use)
export { TokenAuth } from './auth/token.js';
export { JWTAuth, decodeJWT, isJWTExpired } from './auth/jwt.js';
// Version
export const VERSION = '2.0.0';
//# sourceMappingURL=index.js.map