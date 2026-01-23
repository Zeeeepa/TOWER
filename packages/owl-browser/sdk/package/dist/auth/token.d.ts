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
export declare class TokenAuth {
    private readonly _token;
    constructor(token: string);
    /**
     * Get authentication headers for HTTP requests.
     */
    getHeaders(): Record<string, string>;
    /**
     * Get the raw token value.
     */
    getToken(): string;
}
//# sourceMappingURL=token.d.ts.map