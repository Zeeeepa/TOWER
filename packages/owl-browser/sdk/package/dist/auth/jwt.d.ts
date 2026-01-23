/**
 * JWT (JSON Web Token) authentication with RS256 signing.
 *
 * Provides JWT generation and automatic refresh capabilities using RSA-SHA256
 * algorithm for secure authentication with the Owl Browser HTTP server.
 */
import type { JWTConfig } from '../types.js';
/**
 * JWT authentication manager with automatic token generation and refresh.
 *
 * @example
 * ```typescript
 * import { JWTAuth } from './auth/jwt.js';
 *
 * const auth = new JWTAuth({
 *   privateKeyPath: '/path/to/private.pem',
 *   expiresIn: 3600,
 *   refreshThreshold: 300,
 *   issuer: 'my-app'
 * });
 *
 * const headers = auth.getHeaders();
 * // headers = { Authorization: 'Bearer <jwt_token>' }
 * ```
 */
export declare class JWTAuth {
    private readonly _privateKey;
    private readonly _expiresIn;
    private readonly _refreshThreshold;
    private readonly _issuer?;
    private readonly _subject?;
    private readonly _audience?;
    private readonly _claims;
    private _currentToken;
    private _tokenExpiresAt;
    constructor(config: JWTConfig);
    /**
     * Get authentication headers for HTTP requests.
     */
    getHeaders(): Record<string, string>;
    /**
     * Get a valid JWT token, generating a new one if needed.
     */
    getToken(): string;
    /**
     * Force refresh the token regardless of expiration status.
     */
    refreshToken(): string;
    /**
     * Get the remaining validity time of the current token in seconds.
     */
    getRemainingTime(): number;
    /**
     * Clear the current token for forced re-authentication.
     */
    clearToken(): void;
    private _needsRefresh;
    private _generateToken;
}
/**
 * Decode a JWT token without verifying the signature.
 */
export declare function decodeJWT(token: string): {
    header: unknown;
    payload: unknown;
};
/**
 * Check if a JWT token is expired.
 */
export declare function isJWTExpired(token: string, clockSkew?: number): boolean;
//# sourceMappingURL=jwt.d.ts.map