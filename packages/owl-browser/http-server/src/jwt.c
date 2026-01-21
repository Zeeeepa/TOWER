/**
 * Owl Browser HTTP Server - JWT Authentication Implementation
 *
 * Implements JWT with RSA signing using OpenSSL.
 */

#include "jwt.h"
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/err.h>
#include <openssl/bio.h>

// ============================================================================
// Global state
// ============================================================================

static struct {
    bool enabled;
    bool initialized;
    EVP_PKEY* public_key;
    EVP_PKEY* private_key;
    JwtAlgorithm algorithm;
    char expected_issuer[256];
    char expected_audience[256];
    int clock_skew_seconds;
    bool require_exp;
} g_jwt = {0};

// ============================================================================
// Base64URL encoding/decoding
// ============================================================================

static const char base64url_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

int jwt_base64url_encode(const unsigned char* input, size_t input_len,
                         char* output, size_t output_size) {
    if (!input || !output || output_size == 0) return -1;

    size_t encoded_len = ((input_len + 2) / 3) * 4;
    if (output_size < encoded_len + 1) return -1;

    size_t i, j;
    for (i = 0, j = 0; i < input_len; i += 3, j += 4) {
        uint32_t n = ((uint32_t)input[i]) << 16;
        if (i + 1 < input_len) n |= ((uint32_t)input[i + 1]) << 8;
        if (i + 2 < input_len) n |= (uint32_t)input[i + 2];

        output[j] = base64url_chars[(n >> 18) & 0x3F];
        output[j + 1] = base64url_chars[(n >> 12) & 0x3F];
        output[j + 2] = (i + 1 < input_len) ? base64url_chars[(n >> 6) & 0x3F] : '\0';
        output[j + 3] = (i + 2 < input_len) ? base64url_chars[n & 0x3F] : '\0';
    }

    // Remove padding (base64url doesn't use padding)
    while (j > 0 && output[j - 1] == '\0') j--;
    output[j] = '\0';

    return (int)j;
}

static int base64url_char_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

int jwt_base64url_decode(const char* input, unsigned char* output, size_t output_size) {
    if (!input || !output || output_size == 0) return -1;

    size_t input_len = strlen(input);
    size_t decoded_len = (input_len * 3) / 4;

    if (output_size < decoded_len) return -1;

    size_t i, j;
    for (i = 0, j = 0; i < input_len; ) {
        int v[4] = {0, 0, 0, 0};
        int count = 0;

        for (int k = 0; k < 4 && i < input_len; k++, i++) {
            v[k] = base64url_char_value(input[i]);
            if (v[k] >= 0) count++;
        }

        if (count >= 2) {
            output[j++] = (unsigned char)((v[0] << 2) | (v[1] >> 4));
            if (count >= 3 && j < output_size) {
                output[j++] = (unsigned char)((v[1] << 4) | (v[2] >> 2));
                if (count >= 4 && j < output_size) {
                    output[j++] = (unsigned char)((v[2] << 6) | v[3]);
                }
            }
        }
    }

    return (int)j;
}

// ============================================================================
// Key loading
// ============================================================================

static EVP_PKEY* load_public_key(const char* path) {
    FILE* fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "JWT: Cannot open public key file: %s\n", path);
        return NULL;
    }

    EVP_PKEY* key = PEM_read_PUBKEY(fp, NULL, NULL, NULL);
    fclose(fp);

    if (!key) {
        // Try reading as certificate
        fp = fopen(path, "r");
        if (fp) {
            X509* cert = PEM_read_X509(fp, NULL, NULL, NULL);
            fclose(fp);
            if (cert) {
                key = X509_get_pubkey(cert);
                X509_free(cert);
            }
        }
    }

    if (!key) {
        fprintf(stderr, "JWT: Failed to load public key from: %s\n", path);
        ERR_print_errors_fp(stderr);
    }

    return key;
}

static EVP_PKEY* load_private_key(const char* path) {
    FILE* fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "JWT: Cannot open private key file: %s\n", path);
        return NULL;
    }

    EVP_PKEY* key = PEM_read_PrivateKey(fp, NULL, NULL, NULL);
    fclose(fp);

    if (!key) {
        fprintf(stderr, "JWT: Failed to load private key from: %s\n", path);
        ERR_print_errors_fp(stderr);
    }

    return key;
}

// ============================================================================
// Algorithm helpers
// ============================================================================

JwtAlgorithm jwt_parse_algorithm(const char* alg_str) {
    if (!alg_str) return JWT_ALG_RS256;
    if (strcmp(alg_str, "RS256") == 0) return JWT_ALG_RS256;
    if (strcmp(alg_str, "RS384") == 0) return JWT_ALG_RS384;
    if (strcmp(alg_str, "RS512") == 0) return JWT_ALG_RS512;
    if (strcmp(alg_str, "none") == 0) return JWT_ALG_NONE;
    return JWT_ALG_RS256;
}

const char* jwt_algorithm_string(JwtAlgorithm alg) {
    switch (alg) {
        case JWT_ALG_RS256: return "RS256";
        case JWT_ALG_RS384: return "RS384";
        case JWT_ALG_RS512: return "RS512";
        case JWT_ALG_NONE:  return "none";
        default: return "unknown";
    }
}

static const EVP_MD* get_evp_md(JwtAlgorithm alg) {
    switch (alg) {
        case JWT_ALG_RS256: return EVP_sha256();
        case JWT_ALG_RS384: return EVP_sha384();
        case JWT_ALG_RS512: return EVP_sha512();
        default: return NULL;
    }
}

// ============================================================================
// JWT parsing
// ============================================================================

static int parse_jwt_parts(const char* token, char** header, char** payload,
                           char** signature, char** signing_input) {
    if (!token || !header || !payload || !signature || !signing_input) return -1;

    *header = NULL;
    *payload = NULL;
    *signature = NULL;
    *signing_input = NULL;

    // Find first dot
    const char* dot1 = strchr(token, '.');
    if (!dot1) return -1;

    // Find second dot
    const char* dot2 = strchr(dot1 + 1, '.');
    if (!dot2) return -1;

    // Extract parts
    size_t header_len = dot1 - token;
    size_t payload_len = dot2 - dot1 - 1;
    size_t sig_len = strlen(dot2 + 1);

    *header = malloc(header_len + 1);
    *payload = malloc(payload_len + 1);
    *signature = malloc(sig_len + 1);
    *signing_input = malloc(dot2 - token + 1);

    if (!*header || !*payload || !*signature || !*signing_input) {
        free(*header);
        free(*payload);
        free(*signature);
        free(*signing_input);
        return -1;
    }

    memcpy(*header, token, header_len);
    (*header)[header_len] = '\0';

    memcpy(*payload, dot1 + 1, payload_len);
    (*payload)[payload_len] = '\0';

    memcpy(*signature, dot2 + 1, sig_len);
    (*signature)[sig_len] = '\0';

    memcpy(*signing_input, token, dot2 - token);
    (*signing_input)[dot2 - token] = '\0';

    return 0;
}

static int verify_signature(const char* signing_input, const char* signature_b64,
                           JwtAlgorithm alg) {
    if (!g_jwt.public_key) {
        fprintf(stderr, "JWT: No public key loaded for verification\n");
        return -1;
    }

    const EVP_MD* md = get_evp_md(alg);
    if (!md) {
        fprintf(stderr, "JWT: Unsupported algorithm\n");
        return -1;
    }

    // Decode signature
    unsigned char sig_decoded[1024];
    int sig_len = jwt_base64url_decode(signature_b64, sig_decoded, sizeof(sig_decoded));
    if (sig_len < 0) {
        return -1;
    }

    // Verify signature
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return -1;

    int result = -1;
    if (EVP_DigestVerifyInit(ctx, NULL, md, NULL, g_jwt.public_key) == 1) {
        if (EVP_DigestVerifyUpdate(ctx, signing_input, strlen(signing_input)) == 1) {
            if (EVP_DigestVerifyFinal(ctx, sig_decoded, (size_t)sig_len) == 1) {
                result = 0;
            }
        }
    }

    EVP_MD_CTX_free(ctx);
    return result;
}

static int parse_claims(const char* payload_b64, JwtClaims* claims) {
    if (!claims) return 0;

    jwt_claims_init(claims);

    // Decode payload
    unsigned char payload_json[8192];
    int len = jwt_base64url_decode(payload_b64, payload_json, sizeof(payload_json) - 1);
    if (len < 0) return -1;
    payload_json[len] = '\0';

    // Parse JSON
    JsonValue* json = json_parse((const char*)payload_json);
    if (!json || json->type != JSON_OBJECT) {
        json_free(json);
        return -1;
    }

    // Extract standard claims
    const char* iss = json_object_get_string(json, "iss");
    if (iss) strncpy(claims->issuer, iss, sizeof(claims->issuer) - 1);

    const char* sub = json_object_get_string(json, "sub");
    if (sub) strncpy(claims->subject, sub, sizeof(claims->subject) - 1);

    const char* aud = json_object_get_string(json, "aud");
    if (aud) strncpy(claims->audience, aud, sizeof(claims->audience) - 1);

    claims->expires_at = json_object_get_int(json, "exp", 0);
    claims->not_before = json_object_get_int(json, "nbf", 0);
    claims->issued_at = json_object_get_int(json, "iat", 0);

    const char* jti = json_object_get_string(json, "jti");
    if (jti) strncpy(claims->jwt_id, jti, sizeof(claims->jwt_id) - 1);

    // Custom claims
    const char* scope = json_object_get_string(json, "scope");
    if (scope) strncpy(claims->scope, scope, sizeof(claims->scope) - 1);

    const char* client_id = json_object_get_string(json, "client_id");
    if (client_id) strncpy(claims->client_id, client_id, sizeof(claims->client_id) - 1);

    json_free(json);
    return 0;
}

static JwtValidationResult validate_claims(const JwtClaims* claims) {
    time_t now = time(NULL);

    // Check expiration
    if (g_jwt.require_exp && claims->expires_at == 0) {
        return JWT_MISSING_CLAIM;
    }

    if (claims->expires_at > 0) {
        if (now > claims->expires_at + g_jwt.clock_skew_seconds) {
            return JWT_EXPIRED;
        }
    }

    // Check not-before
    if (claims->not_before > 0) {
        if (now < claims->not_before - g_jwt.clock_skew_seconds) {
            return JWT_NOT_YET_VALID;
        }
    }

    // Check issuer if configured
    if (g_jwt.expected_issuer[0] != '\0') {
        if (strcmp(claims->issuer, g_jwt.expected_issuer) != 0) {
            return JWT_INVALID_ISSUER;
        }
    }

    // Check audience if configured
    if (g_jwt.expected_audience[0] != '\0') {
        if (strcmp(claims->audience, g_jwt.expected_audience) != 0) {
            return JWT_INVALID_AUDIENCE;
        }
    }

    return JWT_VALID;
}

// ============================================================================
// Public API
// ============================================================================

int jwt_init(const JwtModuleConfig* config) {
    if (!config) return -1;

    memset(&g_jwt, 0, sizeof(g_jwt));

    if (!config->enabled) {
        g_jwt.enabled = false;
        return 0;
    }

    // Load public key (required for verification)
    if (config->public_key_path[0] != '\0') {
        g_jwt.public_key = load_public_key(config->public_key_path);
        if (!g_jwt.public_key) {
            return -1;
        }
    } else {
        fprintf(stderr, "JWT: Public key path is required\n");
        return -1;
    }

    // Load private key (optional, for token creation)
    if (config->private_key_path[0] != '\0') {
        g_jwt.private_key = load_private_key(config->private_key_path);
        // Not fatal if this fails - we just can't create tokens
    }

    g_jwt.algorithm = config->algorithm;
    g_jwt.clock_skew_seconds = config->clock_skew_seconds > 0 ? config->clock_skew_seconds : 60;
    g_jwt.require_exp = config->require_exp;

    if (config->expected_issuer[0] != '\0') {
        strncpy(g_jwt.expected_issuer, config->expected_issuer, sizeof(g_jwt.expected_issuer) - 1);
    }
    if (config->expected_audience[0] != '\0') {
        strncpy(g_jwt.expected_audience, config->expected_audience, sizeof(g_jwt.expected_audience) - 1);
    }

    g_jwt.enabled = true;
    g_jwt.initialized = true;

    return 0;
}

JwtValidationResult jwt_validate(const char* token, JwtClaims* claims) {
    if (!g_jwt.enabled || !g_jwt.initialized) {
        return JWT_ERROR;
    }

    if (!token || strlen(token) == 0) {
        return JWT_INVALID_FORMAT;
    }

    char* header = NULL;
    char* payload = NULL;
    char* signature = NULL;
    char* signing_input = NULL;

    if (parse_jwt_parts(token, &header, &payload, &signature, &signing_input) != 0) {
        return JWT_INVALID_FORMAT;
    }

    JwtValidationResult result = JWT_ERROR;

    // Verify signature
    if (verify_signature(signing_input, signature, g_jwt.algorithm) != 0) {
        result = JWT_INVALID_SIGNATURE;
        goto cleanup;
    }

    // Parse and validate claims
    JwtClaims temp_claims;
    if (parse_claims(payload, &temp_claims) != 0) {
        result = JWT_INVALID_FORMAT;
        goto cleanup;
    }

    result = validate_claims(&temp_claims);

    if (result == JWT_VALID && claims) {
        *claims = temp_claims;
    }

cleanup:
    free(header);
    free(payload);
    free(signature);
    free(signing_input);

    return result;
}

JwtValidationResult jwt_validate_header(const char* authorization, JwtClaims* claims) {
    if (!authorization || strlen(authorization) == 0) {
        return JWT_INVALID_FORMAT;
    }

    // Check for "Bearer " prefix
    const char* prefix = "Bearer ";
    size_t prefix_len = strlen(prefix);

    if (strncmp(authorization, prefix, prefix_len) != 0) {
        return JWT_INVALID_FORMAT;
    }

    return jwt_validate(authorization + prefix_len, claims);
}

int jwt_create(const JwtClaims* claims, char* token_out, size_t token_size) {
    if (!claims || !token_out || token_size == 0) return -1;

    if (!g_jwt.private_key) {
        fprintf(stderr, "JWT: No private key loaded for signing\n");
        return -1;
    }

    // Build header
    char header_json[256];
    snprintf(header_json, sizeof(header_json),
             "{\"alg\":\"%s\",\"typ\":\"JWT\"}",
             jwt_algorithm_string(g_jwt.algorithm));

    // Build payload
    JsonBuilder builder;
    json_builder_init(&builder);
    json_builder_object_start(&builder);

    if (claims->issuer[0]) {
        json_builder_key(&builder, "iss");
        json_builder_string(&builder, claims->issuer);
        json_builder_comma(&builder);
    }
    if (claims->subject[0]) {
        json_builder_key(&builder, "sub");
        json_builder_string(&builder, claims->subject);
        json_builder_comma(&builder);
    }
    if (claims->audience[0]) {
        json_builder_key(&builder, "aud");
        json_builder_string(&builder, claims->audience);
        json_builder_comma(&builder);
    }
    if (claims->expires_at > 0) {
        json_builder_key(&builder, "exp");
        json_builder_int(&builder, claims->expires_at);
        json_builder_comma(&builder);
    }
    if (claims->not_before > 0) {
        json_builder_key(&builder, "nbf");
        json_builder_int(&builder, claims->not_before);
        json_builder_comma(&builder);
    }
    if (claims->issued_at > 0) {
        json_builder_key(&builder, "iat");
        json_builder_int(&builder, claims->issued_at);
        json_builder_comma(&builder);
    }
    if (claims->jwt_id[0]) {
        json_builder_key(&builder, "jti");
        json_builder_string(&builder, claims->jwt_id);
        json_builder_comma(&builder);
    }
    if (claims->scope[0]) {
        json_builder_key(&builder, "scope");
        json_builder_string(&builder, claims->scope);
        json_builder_comma(&builder);
    }
    if (claims->client_id[0]) {
        json_builder_key(&builder, "client_id");
        json_builder_string(&builder, claims->client_id);
    }

    json_builder_object_end(&builder);
    char* payload_json = json_builder_finish(&builder);

    // Base64URL encode header and payload
    char header_b64[512];
    char payload_b64[8192];

    jwt_base64url_encode((unsigned char*)header_json, strlen(header_json),
                         header_b64, sizeof(header_b64));
    jwt_base64url_encode((unsigned char*)payload_json, strlen(payload_json),
                         payload_b64, sizeof(payload_b64));

    free(payload_json);

    // Create signing input
    char signing_input[9000];
    snprintf(signing_input, sizeof(signing_input), "%s.%s", header_b64, payload_b64);

    // Sign
    const EVP_MD* md = get_evp_md(g_jwt.algorithm);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return -1;

    unsigned char signature[512];
    size_t sig_len = sizeof(signature);
    int result = -1;

    if (EVP_DigestSignInit(ctx, NULL, md, NULL, g_jwt.private_key) == 1) {
        if (EVP_DigestSignUpdate(ctx, signing_input, strlen(signing_input)) == 1) {
            if (EVP_DigestSignFinal(ctx, signature, &sig_len) == 1) {
                result = 0;
            }
        }
    }

    EVP_MD_CTX_free(ctx);

    if (result != 0) return -1;

    // Base64URL encode signature
    char sig_b64[1024];
    jwt_base64url_encode(signature, sig_len, sig_b64, sizeof(sig_b64));

    // Combine all parts
    int written = snprintf(token_out, token_size, "%s.%s.%s",
                          header_b64, payload_b64, sig_b64);

    return (written > 0 && (size_t)written < token_size) ? 0 : -1;
}

const char* jwt_error_string(JwtValidationResult result) {
    switch (result) {
        case JWT_VALID:             return "Valid";
        case JWT_INVALID_FORMAT:    return "Invalid token format";
        case JWT_INVALID_SIGNATURE: return "Invalid signature";
        case JWT_EXPIRED:           return "Token expired";
        case JWT_NOT_YET_VALID:     return "Token not yet valid";
        case JWT_MISSING_CLAIM:     return "Missing required claim";
        case JWT_INVALID_ISSUER:    return "Invalid issuer";
        case JWT_INVALID_AUDIENCE:  return "Invalid audience";
        case JWT_ERROR:             return "JWT error";
        default:                    return "Unknown error";
    }
}

bool jwt_is_enabled(void) {
    return g_jwt.enabled && g_jwt.initialized;
}

JwtAlgorithm jwt_get_algorithm(void) {
    return g_jwt.algorithm;
}

void jwt_shutdown(void) {
    if (g_jwt.public_key) {
        EVP_PKEY_free(g_jwt.public_key);
        g_jwt.public_key = NULL;
    }
    if (g_jwt.private_key) {
        EVP_PKEY_free(g_jwt.private_key);
        g_jwt.private_key = NULL;
    }
    memset(&g_jwt, 0, sizeof(g_jwt));
}

void jwt_claims_init(JwtClaims* claims) {
    if (claims) {
        memset(claims, 0, sizeof(JwtClaims));
    }
}

void jwt_claims_set(JwtClaims* claims, const char* issuer, const char* subject,
                    const char* audience, int expires_in_seconds) {
    if (!claims) return;

    jwt_claims_init(claims);

    time_t now = time(NULL);

    if (issuer) strncpy(claims->issuer, issuer, sizeof(claims->issuer) - 1);
    if (subject) strncpy(claims->subject, subject, sizeof(claims->subject) - 1);
    if (audience) strncpy(claims->audience, audience, sizeof(claims->audience) - 1);

    claims->issued_at = now;
    claims->not_before = now;
    claims->expires_at = now + expires_in_seconds;
}
