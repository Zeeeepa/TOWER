/**
 * Main OwlBrowser client class.
 *
 * Provides the primary interface for interacting with the Owl Browser HTTP server,
 * with dynamic method generation from OpenAPI schema.
 */
import type { RemoteConfig, ToolDefinition, HealthResponse } from './types.js';
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
export declare class OwlBrowser {
    private readonly _config;
    private readonly _transport;
    private readonly _openapi;
    private readonly _tools;
    private readonly _dynamicMethods;
    private _connected;
    [key: string]: unknown;
    constructor(config: RemoteConfig);
    private _normalizeApiPrefix;
    /**
     * Connect to the browser server.
     */
    connect(): Promise<void>;
    /**
     * Close the connection and release resources.
     */
    close(): Promise<void>;
    /**
     * Check if connected to the server.
     */
    get connected(): boolean;
    private _setupDynamicMethods;
    private _createToolMethod;
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
    execute(toolName: string, params?: Record<string, unknown>): Promise<unknown>;
    /**
     * Check server health status.
     */
    healthCheck(): Promise<HealthResponse>;
    /**
     * Get tool definition by name.
     */
    getTool(name: string): ToolDefinition | undefined;
    /**
     * List all available tool names.
     */
    listTools(): string[];
    /**
     * List all available dynamic method names.
     */
    listMethods(): string[];
    /**
     * Check if a dynamic method exists.
     */
    hasMethod(name: string): boolean;
    /**
     * Create a new browser context.
     */
    createContext(params?: Record<string, unknown>): Promise<{
        context_id: string;
        [key: string]: unknown;
    }>;
    /**
     * Close a browser context.
     */
    closeContext(params: {
        context_id: string;
    }): Promise<unknown>;
    /**
     * Navigate to a URL.
     */
    navigate(params: {
        context_id: string;
        url: string;
        [key: string]: unknown;
    }): Promise<unknown>;
    /**
     * Click an element.
     */
    click(params: {
        context_id: string;
        selector: string;
        [key: string]: unknown;
    }): Promise<unknown>;
    /**
     * Type text into an element.
     */
    type(params: {
        context_id: string;
        selector: string;
        text: string;
        [key: string]: unknown;
    }): Promise<unknown>;
    /**
     * Take a screenshot.
     */
    screenshot(params: {
        context_id: string;
        [key: string]: unknown;
    }): Promise<unknown>;
    /**
     * Wait for a selector.
     */
    waitForSelector(params: {
        context_id: string;
        selector: string;
        [key: string]: unknown;
    }): Promise<unknown>;
    /**
     * Get page HTML.
     */
    getHtml(params: {
        context_id: string;
        [key: string]: unknown;
    }): Promise<unknown>;
    /**
     * Get page markdown.
     */
    getMarkdown(params: {
        context_id: string;
        [key: string]: unknown;
    }): Promise<unknown>;
    /**
     * Evaluate JavaScript in the page.
     */
    evaluate(params: {
        context_id: string;
        script: string;
        [key: string]: unknown;
    }): Promise<unknown>;
}
//# sourceMappingURL=client.d.ts.map