/**
 * WebSocket transport for Owl Browser SDK v2.
 *
 * Provides async WebSocket transport for real-time communication
 * with the Owl Browser HTTP server.
 */
import WebSocket from 'ws';
import { AuthMode } from '../types.js';
import { TokenAuth } from '../auth/token.js';
import { JWTAuth } from '../auth/jwt.js';
import { AuthenticationError, ConnectionError, OwlBrowserError, TimeoutError, ToolExecutionError, } from '../errors.js';
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
export class WebSocketTransport {
    _config;
    _baseUrl;
    _auth;
    _ws = null;
    _pendingRequests = new Map();
    _connected = false;
    _requestIdCounter = 0;
    constructor(config) {
        this._config = config;
        const wsScheme = config.url.startsWith('https') ? 'wss' : 'ws';
        const base = config.url.replace(/^https?:\/\//, '').replace(/\/+$/, '');
        const apiPrefix = (config.apiPrefix ?? '/api').replace(/\/+$/, '');
        this._baseUrl = wsScheme + '://' + base + apiPrefix + '/ws';
        // Setup authentication
        if (config.authMode === AuthMode.JWT && config.jwt) {
            this._auth = new JWTAuth(config.jwt);
        }
        else {
            if (!config.token) {
                throw new Error('Token is required for TOKEN authentication');
            }
            this._auth = new TokenAuth(config.token);
        }
    }
    /**
     * Establish WebSocket connection.
     */
    async connect() {
        if (this._connected) {
            return;
        }
        return new Promise((resolve, reject) => {
            try {
                const token = this._auth.getToken();
                this._ws = new WebSocket(this._baseUrl, {
                    headers: {
                        Authorization: 'Bearer ' + token,
                    },
                    handshakeTimeout: (this._config.timeout ?? 30) * 1000,
                });
                this._ws.on('open', () => {
                    this._connected = true;
                    this._setupMessageHandler();
                    resolve();
                });
                this._ws.on('error', (err) => {
                    this._cleanup();
                    if (err.message.includes('401')) {
                        reject(new AuthenticationError('WebSocket authentication failed'));
                    }
                    else {
                        reject(new ConnectionError('WebSocket connection failed: ' + err.message, err));
                    }
                });
                this._ws.on('close', () => {
                    this._cleanup();
                });
            }
            catch (e) {
                this._cleanup();
                reject(new ConnectionError('WebSocket connection failed: ' + (e instanceof Error ? e.message : String(e)), e instanceof Error ? e : undefined));
            }
        });
    }
    _setupMessageHandler() {
        if (!this._ws)
            return;
        this._ws.on('message', (data) => {
            try {
                const message = JSON.parse(data.toString());
                const rawId = message['id'];
                const requestId = typeof rawId === 'number' ? rawId : undefined;
                if (requestId !== undefined && this._pendingRequests.has(requestId)) {
                    const pending = this._pendingRequests.get(requestId);
                    this._pendingRequests.delete(requestId);
                    if ('error' in message) {
                        pending.reject(new ToolExecutionError('unknown', String(message['error'])));
                    }
                    else {
                        pending.resolve(message['result']);
                    }
                }
            }
            catch {
                // Ignore parse errors
            }
        });
    }
    _cleanup() {
        this._connected = false;
        // Reject all pending requests
        for (const [, pending] of this._pendingRequests) {
            pending.reject(new ConnectionError('WebSocket connection closed'));
        }
        this._pendingRequests.clear();
        if (this._ws) {
            this._ws.removeAllListeners();
            try {
                this._ws.close();
            }
            catch {
                // Ignore close errors
            }
            this._ws = null;
        }
    }
    /**
     * Close the WebSocket connection and release resources.
     */
    async close() {
        this._cleanup();
    }
    /**
     * Execute a browser tool over WebSocket.
     */
    async execute(toolName, params) {
        if (!this._connected || !this._ws) {
            throw new ConnectionError('WebSocket not connected');
        }
        const requestId = ++this._requestIdCounter;
        const timeout = (this._config.timeout ?? 30) * 1000;
        return new Promise((resolve, reject) => {
            const timeoutId = setTimeout(() => {
                this._pendingRequests.delete(requestId);
                reject(new TimeoutError('WebSocket request to ' + toolName + ' timed out', timeout));
            }, timeout);
            this._pendingRequests.set(requestId, {
                resolve: (value) => {
                    clearTimeout(timeoutId);
                    resolve(value);
                },
                reject: (error) => {
                    clearTimeout(timeoutId);
                    reject(error);
                },
            });
            const message = {
                id: requestId,
                method: toolName,
                params,
            };
            try {
                this._ws.send(JSON.stringify(message));
            }
            catch (e) {
                this._pendingRequests.delete(requestId);
                clearTimeout(timeoutId);
                if (e instanceof OwlBrowserError) {
                    reject(e);
                }
                else {
                    reject(new ToolExecutionError(toolName, e instanceof Error ? e.message : String(e)));
                }
            }
        });
    }
    /**
     * Check if WebSocket is connected.
     */
    get connected() {
        return this._connected;
    }
}
//# sourceMappingURL=websocket.js.map