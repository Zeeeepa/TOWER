/**
 * Main OwlBrowser client class.
 *
 * Provides the primary interface for interacting with the Owl Browser HTTP server,
 * with dynamic method generation from OpenAPI schema.
 */
import { TransportMode } from './types.js';
import { OwlBrowserError } from './errors.js';
import { HTTPTransport } from './transport/http.js';
import { WebSocketTransport } from './transport/websocket.js';
import { OpenAPILoader, getBundledSchema, coerceIntegerFields } from './openapi.js';
/**
 * Main client for Owl Browser automation.
 *
 * This class provides the primary interface for interacting with the
 * Owl Browser HTTP server. It dynamically generates methods for all
 * available browser tools based on the OpenAPI schema.
 *
 * Features:
 * - Dynamic method generation from OpenAPI schema
 * - Async-first design
 * - Automatic type coercion for integer fields
 * - Connection pooling and retry logic
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
 * // Navigate to a page
 * await browser.navigate({ context_id: contextId, url: 'https://example.com' });
 *
 * // Take a screenshot
 * const screenshot = await browser.screenshot({ context_id: contextId });
 *
 * // Close the context
 * await browser.closeContext({ context_id: contextId });
 *
 * await browser.close();
 * ```
 */
export class OwlBrowser {
    _config;
    _transport;
    _openapi;
    _tools;
    _dynamicMethods = new Map();
    _connected = false;
    constructor(config) {
        // Normalize config
        this._config = {
            ...config,
            url: config.url.replace(/\/+$/, ''),
            apiPrefix: this._normalizeApiPrefix(config.apiPrefix),
        };
        // Create transport
        if (config.transport === TransportMode.WEBSOCKET) {
            this._transport = new WebSocketTransport(this._config);
        }
        else {
            this._transport = new HTTPTransport(this._config);
        }
        // Load OpenAPI schema
        this._openapi = new OpenAPILoader(getBundledSchema());
        this._tools = this._openapi.tools;
        // Setup dynamic methods
        this._setupDynamicMethods();
    }
    _normalizeApiPrefix(prefix) {
        if (!prefix && prefix !== '') {
            return '/api';
        }
        if (prefix === '') {
            return '';
        }
        let normalized = prefix;
        if (!normalized.startsWith('/')) {
            normalized = '/' + normalized;
        }
        return normalized.replace(/\/+$/, '');
    }
    /**
     * Connect to the browser server.
     */
    async connect() {
        if (this._connected) {
            return;
        }
        await this._transport.connect();
        this._connected = true;
    }
    /**
     * Close the connection and release resources.
     */
    async close() {
        if (!this._connected) {
            return;
        }
        await this._transport.close();
        this._connected = false;
    }
    /**
     * Check if connected to the server.
     */
    get connected() {
        return this._connected;
    }
    _setupDynamicMethods() {
        for (const [toolName, toolDef] of this._tools) {
            const methodName = toolName.replace('browser_', '');
            // Convert snake_case to camelCase
            const camelMethodName = methodName.replace(/_([a-z])/g, (_, letter) => letter.toUpperCase());
            if (!this._dynamicMethods.has(camelMethodName)) {
                const method = this._createToolMethod(toolName, toolDef);
                this._dynamicMethods.set(camelMethodName, method);
                // Also keep snake_case version
                this._dynamicMethods.set(methodName, method);
                // Make methods accessible on the instance
                this[camelMethodName] = method;
                this[methodName] = method;
            }
        }
    }
    _createToolMethod(toolName, toolDef) {
        const method = async (params = {}) => {
            return this.execute(toolName, params);
        };
        // Add documentation to the method
        Object.defineProperty(method, 'name', { value: toolName.replace('browser_', '') });
        return method;
    }
    /**
     * Execute any browser tool by name.
     *
     * @example
     * ```typescript
     * // Using execute() directly
     * const result = await browser.execute('browser_navigate', {
     *   context_id: 'ctx_1',
     *   url: 'https://example.com'
     * });
     *
     * // Or using the dynamic method
     * const result = await browser.navigate({
     *   context_id: 'ctx_1',
     *   url: 'https://example.com'
     * });
     * ```
     */
    async execute(toolName, params = {}) {
        const toolDef = this._tools.get(toolName);
        let coercedParams = params;
        if (toolDef) {
            coercedParams = coerceIntegerFields(toolDef, params);
        }
        return this._transport.execute(toolName, coercedParams);
    }
    /**
     * Check server health status.
     */
    async healthCheck() {
        if (this._transport instanceof HTTPTransport) {
            return this._transport.healthCheck();
        }
        throw new OwlBrowserError('Health check not supported over WebSocket');
    }
    /**
     * Get tool definition by name.
     */
    getTool(name) {
        return this._tools.get(name);
    }
    /**
     * List all available tool names.
     */
    listTools() {
        return Array.from(this._tools.keys());
    }
    /**
     * List all available dynamic method names.
     */
    listMethods() {
        return Array.from(this._dynamicMethods.keys());
    }
    /**
     * Check if a dynamic method exists.
     */
    hasMethod(name) {
        return this._dynamicMethods.has(name);
    }
    // ==================== Commonly Used Methods ====================
    // These are typed versions of the dynamic methods for better IDE support
    /**
     * Create a new browser context.
     */
    async createContext(params) {
        return await this.execute('browser_create_context', params ?? {});
    }
    /**
     * Close a browser context.
     */
    async closeContext(params) {
        return await this.execute('browser_close_context', params);
    }
    /**
     * Navigate to a URL.
     */
    async navigate(params) {
        return await this.execute('browser_navigate', params);
    }
    /**
     * Click an element.
     */
    async click(params) {
        return await this.execute('browser_click', params);
    }
    /**
     * Type text into an element.
     */
    async type(params) {
        return await this.execute('browser_type', params);
    }
    /**
     * Take a screenshot.
     */
    async screenshot(params) {
        return await this.execute('browser_screenshot', params);
    }
    /**
     * Wait for a selector.
     */
    async waitForSelector(params) {
        return await this.execute('browser_wait_for_selector', params);
    }
    /**
     * Get page HTML.
     */
    async getHtml(params) {
        return await this.execute('browser_get_html', params);
    }
    /**
     * Get page markdown.
     */
    async getMarkdown(params) {
        return await this.execute('browser_get_markdown', params);
    }
    /**
     * Evaluate JavaScript in the page.
     */
    async evaluate(params) {
        return await this.execute('browser_evaluate', params);
    }
}
//# sourceMappingURL=client.js.map