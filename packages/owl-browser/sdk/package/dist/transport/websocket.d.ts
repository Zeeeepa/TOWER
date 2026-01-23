/**
 * WebSocket transport for Owl Browser SDK v2.
 *
 * Provides async WebSocket transport for real-time communication
 * with the Owl Browser HTTP server.
 */
import type { RemoteConfig } from '../types.js';
/**
 * Async WebSocket transport for real-time communication.
 *
 * @example
 * ```typescript
 * const transport = new WebSocketTransport({
 *   url: 'http://localhost:8080',
 *   token: 'secret',
 *   transport: TransportMode.WEBSOCKET
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
export declare class WebSocketTransport {
    private readonly _config;
    private readonly _baseUrl;
    private readonly _auth;
    private _ws;
    private _pendingRequests;
    private _connected;
    private _requestIdCounter;
    constructor(config: RemoteConfig);
    /**
     * Establish WebSocket connection.
     */
    connect(): Promise<void>;
    private _setupMessageHandler;
    private _cleanup;
    /**
     * Close the WebSocket connection and release resources.
     */
    close(): Promise<void>;
    /**
     * Execute a browser tool over WebSocket.
     */
    execute(toolName: string, params: Record<string, unknown>): Promise<unknown>;
    /**
     * Check if WebSocket is connected.
     */
    get connected(): boolean;
}
//# sourceMappingURL=websocket.d.ts.map