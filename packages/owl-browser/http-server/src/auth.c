/**
 * Owl Browser HTTP Server - Authentication Implementation
 *
 * Supports both Bearer token and JWT authentication.
 */

#include "auth.h"
#include "jwt.h"
#include <string.h>
#include <stdio.h>

static char g_token[256] = {0};
static bool g_enabled = false;
static AuthMode g_mode = AUTH_MODE_TOKEN;

void auth_init(const char* token) {
    if (token && strlen(token) > 0) {
        strncpy(g_token, token, sizeof(g_token) - 1);
        g_token[sizeof(g_token) - 1] = '\0';
        g_enabled = true;
        g_mode = AUTH_MODE_TOKEN;
    } else {
        g_token[0] = '\0';
        g_enabled = false;
    }
}

int auth_init_config(const ServerConfig* config) {
    if (!config) return -1;

    g_mode = config->auth_mode;

    if (g_mode == AUTH_MODE_JWT) {
        // Initialize JWT authentication
        JwtModuleConfig jwt_config = {0};
        jwt_config.enabled = true;
        strncpy(jwt_config.public_key_path, config->jwt.public_key_path,
                sizeof(jwt_config.public_key_path) - 1);
        strncpy(jwt_config.private_key_path, config->jwt.private_key_path,
                sizeof(jwt_config.private_key_path) - 1);
        jwt_config.algorithm = jwt_parse_algorithm(config->jwt.algorithm);
        strncpy(jwt_config.expected_issuer, config->jwt.expected_issuer,
                sizeof(jwt_config.expected_issuer) - 1);
        strncpy(jwt_config.expected_audience, config->jwt.expected_audience,
                sizeof(jwt_config.expected_audience) - 1);
        jwt_config.clock_skew_seconds = config->jwt.clock_skew_seconds;
        jwt_config.require_exp = config->jwt.require_exp;

        if (jwt_init(&jwt_config) != 0) {
            fprintf(stderr, "Error: Failed to initialize JWT authentication\n");
            return -1;
        }

        g_enabled = true;
        g_token[0] = '\0';
    } else {
        // Token mode
        auth_init(config->auth_token);
    }

    return 0;
}

static bool validate_token(const char* authorization) {
    if (!authorization || strlen(authorization) == 0) {
        return false;
    }

    // Expect "Bearer <token>" format
    const char* prefix = "Bearer ";
    size_t prefix_len = strlen(prefix);

    if (strncmp(authorization, prefix, prefix_len) != 0) {
        return false;
    }

    const char* provided_token = authorization + prefix_len;

    // Constant-time comparison to prevent timing attacks
    size_t expected_len = strlen(g_token);
    size_t provided_len = strlen(provided_token);

    // Always compare full length to prevent timing attack
    volatile size_t diff = expected_len ^ provided_len;

    for (size_t i = 0; i < expected_len && i < provided_len; i++) {
        diff |= (unsigned char)g_token[i] ^ (unsigned char)provided_token[i];
    }

    return diff == 0;
}

bool auth_validate(const char* authorization) {
    if (!g_enabled) {
        return true;  // No authentication required
    }

    if (g_mode == AUTH_MODE_JWT) {
        JwtValidationResult result = jwt_validate_header(authorization, NULL);
        return result == JWT_VALID;
    } else {
        return validate_token(authorization);
    }
}

bool auth_validate_with_result(const char* authorization, AuthResult* result) {
    if (result) {
        memset(result, 0, sizeof(AuthResult));
    }

    if (!g_enabled) {
        if (result) {
            result->valid = true;
        }
        return true;  // No authentication required
    }

    if (g_mode == AUTH_MODE_JWT) {
        JwtClaims claims;
        JwtValidationResult jwt_result = jwt_validate_header(authorization, &claims);

        if (result) {
            result->valid = (jwt_result == JWT_VALID);
            if (jwt_result != JWT_VALID) {
                strncpy(result->error, jwt_error_string(jwt_result), sizeof(result->error) - 1);
            } else {
                strncpy(result->subject, claims.subject, sizeof(result->subject) - 1);
                strncpy(result->scope, claims.scope, sizeof(result->scope) - 1);
                strncpy(result->client_id, claims.client_id, sizeof(result->client_id) - 1);
            }
        }

        return jwt_result == JWT_VALID;
    } else {
        bool valid = validate_token(authorization);
        if (result) {
            result->valid = valid;
            if (!valid) {
                strncpy(result->error, "Invalid bearer token", sizeof(result->error) - 1);
            }
        }
        return valid;
    }
}

bool auth_is_enabled(void) {
    return g_enabled;
}

AuthMode auth_get_mode(void) {
    return g_mode;
}

const char* auth_get_token(void) {
    if (!g_enabled || g_mode == AUTH_MODE_JWT) {
        return NULL;
    }
    return g_token;
}

bool auth_validate_panel_password(const char* password) {
    if (!password || strlen(password) == 0) {
        return false;
    }

    const char* panel_password = getenv("OWL_PANEL_PASSWORD");
    if (!panel_password || strlen(panel_password) == 0) {
        return false;  // Panel password not configured
    }

    // Constant-time comparison to prevent timing attacks
    size_t expected_len = strlen(panel_password);
    size_t provided_len = strlen(password);

    volatile size_t diff = expected_len ^ provided_len;

    for (size_t i = 0; i < expected_len && i < provided_len; i++) {
        diff |= (unsigned char)panel_password[i] ^ (unsigned char)password[i];
    }

    return diff == 0;
}

void auth_shutdown(void) {
    // Clear token from memory
    memset(g_token, 0, sizeof(g_token));
    g_enabled = false;

    // Shutdown JWT if enabled
    if (g_mode == AUTH_MODE_JWT) {
        jwt_shutdown();
    }

    g_mode = AUTH_MODE_TOKEN;
}
