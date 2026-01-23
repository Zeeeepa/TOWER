/**
 * HTTP transport for Owl Browser SDK v2.
 *
 * Provides async HTTP transport with connection pooling, retry logic,
 * and concurrency limiting.
 */
import { AuthMode } from '../types.js';
import { TokenAuth } from '../auth/token.js';
import { JWTAuth } from '../auth/jwt.js';
import { AuthenticationError, ConnectionError, IPBlockedError, OwlBrowserError, RateLimitError, TimeoutError, ToolExecutionError, } from '../errors.js';
// Tools that may take longer due to network operations or AI processing
const LONG_RUNNING_TOOLS = new Set([
    'browser_navigate',
    'browser_reload',
    'browser_wait',
    'browser_wait_for_selector',
    'browser_wait_for_network_idle',
    'browser_wait_for_function',
    'browser_wait_for_url',
    'browser_query_page',
    'browser_summarize_page',
    'browser_nla',
    'browser_solve_captcha',
    'browser_solve_text_captcha',
    'browser_solve_image_captcha',
    'browser_detect_captcha',
    'browser_extract_site',
    'browser_extract_site_progress',
    'browser_extract_site_result',
    'browser_get_markdown',
    'browser_get_html',
    'browser_extract_text',
    'browser_extract_json',
    'browser_screenshot',
    'browser_ai_click',
    'browser_ai_type',
    'browser_ai_extract',
    'browser_ai_query',
    'browser_ai_analyze',
    'browser_find_element',
    'browser_wait_for_download',
    'browser_wait_for_dialog',
]);
function normalizeRetryConfig(config) {
    return {
        maxRetries: config?.maxRetries ?? 3,
        initialDelayMs: config?.initialDelayMs ?? 100,
        maxDelayMs: config?.maxDelayMs ?? 10000,
        backoffMultiplier: config?.backoffMultiplier ?? 2.0,
        jitterFactor: config?.jitterFactor ?? 0.1,
    };
}
function calculateRetryDelay(config, attempt) {
    let delayMs = config.initialDelayMs * Math.pow(config.backoffMultiplier, attempt);
    delayMs = Math.min(delayMs, config.maxDelayMs);
    const jitter = delayMs * config.jitterFactor * (Math.random() * 2 - 1);
    return Math.max(0, delayMs + jitter);
}
function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}
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
export class HTTPTransport {
    _config;
    _baseUrl;
    _apiPrefix;
    _timeout;
    _longTimeout;
    _auth;
    _retryConfig;
    _maxConcurrent;
    _activeConcurrent = 0;
    _waitQueue = [];
    _connected = false;
    constructor(config) {
        this._config = config;
        this._baseUrl = config.url.replace(/\/+$/, '');
        let apiPrefix = config.apiPrefix ?? '/api';
        if (apiPrefix && !apiPrefix.startsWith('/')) {
            apiPrefix = '/' + apiPrefix;
        }
        this._apiPrefix = apiPrefix.replace(/\/+$/, '');
        this._timeout = (config.timeout ?? 30) * 1000;
        this._longTimeout = Math.max(120000, this._timeout * 4);
        this._retryConfig = normalizeRetryConfig(config.retry);
        this._maxConcurrent = config.maxConcurrent ?? 10;
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
     * Connect to the server (marks transport as ready).
     */
    async connect() {
        this._connected = true;
    }
    /**
     * Close the transport and release resources.
     */
    async close() {
        this._connected = false;
        // Clear wait queue
        for (const resolve of this._waitQueue) {
            resolve();
        }
        this._waitQueue = [];
    }
    /**
     * Check if transport is connected.
     */
    get connected() {
        return this._connected;
    }
    _getHeaders() {
        const headers = this._auth.getHeaders();
        headers['Content-Type'] = 'application/json';
        headers['Accept'] = 'application/json';
        return headers;
    }
    _prefixPath(path) {
        return this._apiPrefix + path;
    }
    async _acquireSemaphore() {
        if (this._activeConcurrent < this._maxConcurrent) {
            this._activeConcurrent++;
            return;
        }
        return new Promise(resolve => {
            this._waitQueue.push(resolve);
        });
    }
    _releaseSemaphore() {
        const next = this._waitQueue.shift();
        if (next) {
            next();
        }
        else {
            this._activeConcurrent--;
        }
    }
    /**
     * Execute a browser tool.
     */
    async execute(toolName, params) {
        const isLongRunning = LONG_RUNNING_TOOLS.has(toolName);
        const timeout = isLongRunning ? this._longTimeout : this._timeout;
        const url = this._baseUrl + this._prefixPath('/execute/' + toolName);
        let lastError = null;
        await this._acquireSemaphore();
        try {
            for (let attempt = 0; attempt < this._retryConfig.maxRetries; attempt++) {
                const controller = new AbortController();
                const timeoutId = setTimeout(() => controller.abort(), timeout);
                try {
                    const response = await fetch(url, {
                        method: 'POST',
                        headers: this._getHeaders(),
                        body: JSON.stringify(params),
                        signal: controller.signal,
                    });
                    clearTimeout(timeoutId);
                    return await this._handleResponse(response, toolName);
                }
                catch (e) {
                    clearTimeout(timeoutId);
                    if (e instanceof Error && e.name === 'AbortError') {
                        throw new TimeoutError('Request to ' + toolName + ' timed out after ' + (timeout / 1000) + 's', timeout);
                    }
                    if (e instanceof AuthenticationError ||
                        e instanceof RateLimitError ||
                        e instanceof IPBlockedError) {
                        throw e;
                    }
                    lastError = e instanceof Error ? e : new Error(String(e));
                    if (attempt < this._retryConfig.maxRetries - 1) {
                        await sleep(calculateRetryDelay(this._retryConfig, attempt));
                        continue;
                    }
                    throw new ConnectionError('Request failed after ' + this._retryConfig.maxRetries + ' retries: ' + lastError.message, lastError);
                }
            }
            if (lastError) {
                throw new ConnectionError('Request failed: ' + lastError.message, lastError);
            }
            throw new ConnectionError('Request failed with unknown error');
        }
        finally {
            this._releaseSemaphore();
        }
    }
    async _handleResponse(response, toolName) {
        if (response.status === 401) {
            const text = await response.text();
            throw new AuthenticationError(text || 'Invalid or missing authorization token');
        }
        if (response.status === 403) {
            const data = await response.json();
            throw new IPBlockedError(String(data['error'] ?? 'Access forbidden'), data['client_ip']);
        }
        if (response.status === 429) {
            const data = await response.json();
            throw new RateLimitError(String(data['error'] ?? 'Rate limit exceeded'), data['retry_after'] ?? 60, data['limit'], data['remaining']);
        }
        if (response.status >= 400) {
            const text = await response.text();
            throw new ToolExecutionError(toolName, 'HTTP ' + response.status + ': ' + text, String(response.status));
        }
        const contentType = response.headers.get('content-type') ?? '';
        if (contentType.includes('application/json')) {
            const data = await response.json();
            if (data['success'] === false) {
                throw new ToolExecutionError(toolName, String(data['error'] ?? 'Unknown error'), undefined, data);
            }
            return data['result'];
        }
        return await response.text();
    }
    /**
     * Check server health status.
     */
    async healthCheck() {
        const url = this._baseUrl + this._prefixPath('/health');
        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), 10000);
        try {
            const response = await fetch(url, {
                method: 'GET',
                signal: controller.signal,
            });
            clearTimeout(timeoutId);
            if (response.status !== 200) {
                throw new OwlBrowserError('Health check failed: HTTP ' + response.status);
            }
            return await response.json();
        }
        catch (e) {
            clearTimeout(timeoutId);
            if (e instanceof OwlBrowserError) {
                throw e;
            }
            throw new ConnectionError('Health check failed: ' + (e instanceof Error ? e.message : String(e)), e instanceof Error ? e : undefined);
        }
    }
    /**
     * List available browser tools.
     */
    async listTools() {
        const url = this._baseUrl + this._prefixPath('/tools');
        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), 30000);
        try {
            const response = await fetch(url, {
                method: 'GET',
                headers: this._getHeaders(),
                signal: controller.signal,
            });
            clearTimeout(timeoutId);
            if (response.status !== 200) {
                throw new OwlBrowserError('Failed to list tools: HTTP ' + response.status);
            }
            const data = await response.json();
            return data['tools'] ?? [];
        }
        catch (e) {
            clearTimeout(timeoutId);
            if (e instanceof OwlBrowserError) {
                throw e;
            }
            throw new ConnectionError('Failed to list tools: ' + (e instanceof Error ? e.message : String(e)), e instanceof Error ? e : undefined);
        }
    }
    /**
     * Fetch the OpenAPI schema from the server.
     */
    async fetchOpenAPISchema() {
        const url = this._baseUrl + this._prefixPath('/openapi.json');
        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), 30000);
        try {
            const response = await fetch(url, {
                method: 'GET',
                headers: this._getHeaders(),
                signal: controller.signal,
            });
            clearTimeout(timeoutId);
            if (response.status !== 200) {
                throw new OwlBrowserError('Failed to fetch OpenAPI schema: HTTP ' + response.status);
            }
            return await response.json();
        }
        catch (e) {
            clearTimeout(timeoutId);
            if (e instanceof OwlBrowserError) {
                throw e;
            }
            throw new ConnectionError('Failed to fetch OpenAPI schema: ' + (e instanceof Error ? e.message : String(e)), e instanceof Error ? e : undefined);
        }
    }
}
//# sourceMappingURL=http.js.map