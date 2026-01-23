/**
 * HTTP transport for Owl Browser SDK v2.
 *
 * Provides async HTTP transport with connection pooling, retry logic,
 * and concurrency limiting.
 */
import type { RemoteConfig, HealthResponse } from '../types.js';
/**
 * Async HTTP transport for communicating with Owl Browser HTTP server.
 *
 * Features:
 * - Connection pooling with fetch for efficient connection reuse.
 * - Retry with exponential backoff and jitter.
 * - Concurrency limiting via semaphore pattern.
 * - Extended timeouts for long-running operations.
 *
 * @example
 * ```typescript
 * const transport = new HTTPTransport({
 *   url: 'http://localhost:8080',
 *   token: 'secret'
 * });
 *
 * await transport.connect();
 * const result = await transport.execute('browser_navigate', {
 *   context_id: 'ctx_1',
 *   url: 'https://example.com'
 * });
 * await transport.close();
 * ```
 */
export declare class HTTPTransport {
    private readonly _config;
    private readonly _baseUrl;
    private readonly _apiPrefix;
    private readonly _timeout;
    private readonly _longTimeout;
    private readonly _auth;
    private readonly _retryConfig;
    private readonly _maxConcurrent;
    private _activeConcurrent;
    private _waitQueue;
    private _connected;
    constructor(config: RemoteConfig);
    /**
     * Connect to the server (marks transport as ready).
     */
    connect(): Promise<void>;
    /**
     * Close the transport and release resources.
     */
    close(): Promise<void>;
    /**
     * Check if transport is connected.
     */
    get connected(): boolean;
    private _getHeaders;
    private _prefixPath;
    private _acquireSemaphore;
    private _releaseSemaphore;
    /**
     * Execute a browser tool.
     */
    execute(toolName: string, params: Record<string, unknown>): Promise<unknown>;
    private _handleResponse;
    /**
     * Check server health status.
     */
    healthCheck(): Promise<HealthResponse>;
    /**
     * List available browser tools.
     */
    listTools(): Promise<Array<Record<string, unknown>>>;
    /**
     * Fetch the OpenAPI schema from the server.
     */
    fetchOpenAPISchema(): Promise<Record<string, unknown>>;
}
//# sourceMappingURL=http.d.ts.map