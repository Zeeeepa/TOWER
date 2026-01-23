/**
 * JWT (JSON Web Token) authentication with RS256 signing.
 *
 * Provides JWT generation and automatic refresh capabilities using RSA-SHA256
 * algorithm for secure authentication with the Owl Browser HTTP server.
 */
import * as fs from 'node:fs';
import * as crypto from 'node:crypto';
/**
 * Base64URL encode bytes to string (no padding).
 */
function base64urlEncode(data) {
    return data.toString('base64url');
}
/**
 * Base64URL decode string to bytes.
 */
function base64urlDecode(data) {
    return Buffer.from(data, 'base64url');
}
/**
 * Load RSA private key from PEM string or file path.
 */
function loadPrivateKey(keyOrPath) {
    let keyPem;
    if (keyOrPath.includes('-----BEGIN') || keyOrPath.includes('PRIVATE KEY')) {
        keyPem = keyOrPath;
    }
    else {
        if (!fs.existsSync(keyOrPath)) {
            throw new Error('Private key file not found: ' + keyOrPath);
        }
        keyPem = fs.readFileSync(keyOrPath, 'utf-8');
    }
    return crypto.createPrivateKey(keyPem);
}
/**
 * Sign data using RS256 (RSA-SHA256).
 */
function signRS256(data, privateKey) {
    const sign = crypto.createSign('RSA-SHA256');
    sign.update(data, 'utf-8');
    return sign.sign(privateKey);
}
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
export class JWTAuth {
    _privateKey;
    _expiresIn;
    _refreshThreshold;
    _issuer;
    _subject;
    _audience;
    _claims;
    _currentToken = null;
    _tokenExpiresAt = 0;
    constructor(config) {
        this._privateKey = loadPrivateKey(config.privateKeyPath);
        this._expiresIn = config.expiresIn ?? 3600;
        this._refreshThreshold = config.refreshThreshold ?? 300;
        this._issuer = config.issuer;
        this._subject = config.subject;
        this._audience = config.audience;
        this._claims = config.claims ?? {};
    }
    /**
     * Get authentication headers for HTTP requests.
     */
    getHeaders() {
        return { Authorization: 'Bearer ' + this.getToken() };
    }
    /**
     * Get a valid JWT token, generating a new one if needed.
     */
    getToken() {
        if (this._currentToken && !this._needsRefresh()) {
            return this._currentToken;
        }
        this._currentToken = this._generateToken();
        return this._currentToken;
    }
    /**
     * Force refresh the token regardless of expiration status.
     */
    refreshToken() {
        this._currentToken = this._generateToken();
        return this._currentToken;
    }
    /**
     * Get the remaining validity time of the current token in seconds.
     */
    getRemainingTime() {
        if (!this._currentToken) {
            return -1;
        }
        const remaining = this._tokenExpiresAt - Math.floor(Date.now() / 1000);
        return remaining > 0 ? remaining : -1;
    }
    /**
     * Clear the current token for forced re-authentication.
     */
    clearToken() {
        this._currentToken = null;
        this._tokenExpiresAt = 0;
    }
    _needsRefresh() {
        if (!this._currentToken) {
            return true;
        }
        const remaining = this._tokenExpiresAt - Math.floor(Date.now() / 1000);
        return remaining < this._refreshThreshold;
    }
    _generateToken() {
        const now = Math.floor(Date.now() / 1000);
        const expiresAt = now + this._expiresIn;
        this._tokenExpiresAt = expiresAt;
        const header = { alg: 'RS256', typ: 'JWT' };
        const payload = {
            iat: now,
            exp: expiresAt,
        };
        if (this._issuer) {
            payload['iss'] = this._issuer;
        }
        if (this._subject) {
            payload['sub'] = this._subject;
        }
        if (this._audience) {
            payload['aud'] = this._audience;
        }
        Object.assign(payload, this._claims);
        const headerB64 = base64urlEncode(Buffer.from(JSON.stringify(header)));
        const payloadB64 = base64urlEncode(Buffer.from(JSON.stringify(payload)));
        const signingInput = headerB64 + '.' + payloadB64;
        const signature = signRS256(signingInput, this._privateKey);
        const signatureB64 = base64urlEncode(signature);
        return signingInput + '.' + signatureB64;
    }
}
/**
 * Decode a JWT token without verifying the signature.
 */
export function decodeJWT(token) {
    const parts = token.split('.');
    if (parts.length !== 3) {
        throw new Error('Invalid JWT format: expected 3 parts separated by dots');
    }
    const [headerB64, payloadB64] = parts;
    try {
        const header = JSON.parse(base64urlDecode(headerB64).toString('utf-8'));
        const payload = JSON.parse(base64urlDecode(payloadB64).toString('utf-8'));
        return { header, payload };
    }
    catch (e) {
        throw new Error('Failed to decode JWT: ' + (e instanceof Error ? e.message : String(e)));
    }
}
/**
 * Check if a JWT token is expired.
 */
export function isJWTExpired(token, clockSkew = 60) {
    try {
        const { payload } = decodeJWT(token);
        const exp = payload['exp'];
        if (typeof exp !== 'number') {
            return false;
        }
        const now = Math.floor(Date.now() / 1000);
        return exp < now - clockSkew;
    }
    catch {
        return true;
    }
}
//# sourceMappingURL=jwt.js.map