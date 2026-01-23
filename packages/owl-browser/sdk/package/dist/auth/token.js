/**
 * Simple bearer token authentication.
 *
 * Provides a minimal authentication implementation using static bearer tokens.
 */
/**
 * Simple bearer token authentication.
 *
 * Provides authentication using a static bearer token that is included
 * in the Authorization header of each request.
 *
 * @example
 * ```typescript
 * const auth = new TokenAuth('your-secret-token');
 * const headers = auth.getHeaders();
 * // headers = { Authorization: 'Bearer your-secret-token' }
 * ```
 */
export class TokenAuth {
    _token;
    constructor(token) {
        this._token = token;
    }
    /**
     * Get authentication headers for HTTP requests.
     */
    getHeaders() {
        return { Authorization: `Bearer ${this._token}` };
    }
    /**
     * Get the raw token value.
     */
    getToken() {
        return this._token;
    }
}
//# sourceMappingURL=token.js.map