/**
 * Owl Browser HTTP Server - JWT Authentication
 *
 * JSON Web Token authentication with RSA (RS256) signing.
 */

#ifndef OWL_HTTP_JWT_H
#define OWL_HTTP_JWT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

// JWT algorithm
typedef enum {
    JWT_ALG_NONE,
    JWT_ALG_RS256,    // RSA + SHA256 (recommended)
    JWT_ALG_RS384,    // RSA + SHA384
    JWT_ALG_RS512     // RSA + SHA512
} JwtAlgorithm;

// JWT validation result
typedef enum {
    JWT_VALID,
    JWT_INVALID_FORMAT,
    JWT_INVALID_SIGNATURE,
    JWT_EXPIRED,
    JWT_NOT_YET_VALID,
    JWT_MISSING_CLAIM,
    JWT_INVALID_ISSUER,
    JWT_INVALID_AUDIENCE,
    JWT_ERROR
} JwtValidationResult;

// JWT claims (standard claims from RFC 7519)
typedef struct {
    char issuer[256];          // iss - who issued the token
    char subject[256];         // sub - subject of the token
    char audience[256];        // aud - intended recipient
    int64_t expires_at;        // exp - expiration time (unix timestamp)
    int64_t not_before;        // nbf - not valid before (unix timestamp)
    int64_t issued_at;         // iat - when issued (unix timestamp)
    char jwt_id[128];          // jti - unique identifier

    // Custom claims for Owl Browser
    char scope[512];           // permissions (e.g., "read write admin")
    char client_id[128];       // client identifier
} JwtClaims;

// JWT internal configuration (for jwt module)
typedef struct {
    bool enabled;
    char public_key_path[4096];     // path to public key (.pem) for verification
    char private_key_path[4096];    // path to private key (.pem) for signing (optional)
    JwtAlgorithm algorithm;
    char expected_issuer[256];      // expected issuer (optional validation)
    char expected_audience[256];    // expected audience (optional validation)
    int clock_skew_seconds;         // allowed clock skew for exp/nbf (default: 60)
    bool require_exp;               // require expiration claim (default: true)
} JwtModuleConfig;

/**
 * Initialize JWT authentication.
 *
 * @param config JWT configuration with key paths
 * @return 0 on success, -1 on error
 */
int jwt_init(const JwtModuleConfig* config);

/**
 * Validate a JWT token.
 *
 * @param token The JWT token string (without "Bearer " prefix)
 * @param claims Output: parsed claims if valid (can be NULL)
 * @return JWT_VALID if valid, error code otherwise
 */
JwtValidationResult jwt_validate(const char* token, JwtClaims* claims);

/**
 * Validate Authorization header.
 *
 * @param authorization Full Authorization header value ("Bearer <token>")
 * @param claims Output: parsed claims if valid (can be NULL)
 * @return JWT_VALID if valid, error code otherwise
 */
JwtValidationResult jwt_validate_header(const char* authorization, JwtClaims* claims);

/**
 * Create a new JWT token.
 *
 * @param claims The claims to include in the token
 * @param token_out Output buffer for the token
 * @param token_size Size of output buffer
 * @return 0 on success, -1 on error
 */
int jwt_create(const JwtClaims* claims, char* token_out, size_t token_size);

/**
 * Get human-readable error message for validation result.
 */
const char* jwt_error_string(JwtValidationResult result);

/**
 * Check if JWT authentication is enabled.
 */
bool jwt_is_enabled(void);

/**
 * Get the configured algorithm.
 */
JwtAlgorithm jwt_get_algorithm(void);

/**
 * Parse algorithm string (e.g., "RS256") to enum.
 */
JwtAlgorithm jwt_parse_algorithm(const char* alg_str);

/**
 * Get algorithm string from enum.
 */
const char* jwt_algorithm_string(JwtAlgorithm alg);

/**
 * Shutdown JWT module and free resources.
 */
void jwt_shutdown(void);

// ============================================================================
// Utility functions
// ============================================================================

/**
 * Base64URL encode data.
 *
 * @param input Input data
 * @param input_len Length of input data
 * @param output Output buffer
 * @param output_size Size of output buffer
 * @return Length of encoded string, or -1 on error
 */
int jwt_base64url_encode(const unsigned char* input, size_t input_len,
                         char* output, size_t output_size);

/**
 * Base64URL decode data.
 *
 * @param input Input string
 * @param output Output buffer
 * @param output_size Size of output buffer
 * @return Length of decoded data, or -1 on error
 */
int jwt_base64url_decode(const char* input,
                         unsigned char* output, size_t output_size);

/**
 * Initialize JWT claims with defaults.
 */
void jwt_claims_init(JwtClaims* claims);

/**
 * Set standard claims for a new token.
 *
 * @param claims Claims structure to populate
 * @param issuer Token issuer
 * @param subject Token subject
 * @param audience Token audience
 * @param expires_in_seconds Token lifetime in seconds
 */
void jwt_claims_set(JwtClaims* claims, const char* issuer, const char* subject,
                    const char* audience, int expires_in_seconds);

#endif // OWL_HTTP_JWT_H
