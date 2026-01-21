/**
 * Owl Browser HTTP Server - Authentication
 *
 * Supports both Bearer token and JWT authentication.
 */

#ifndef OWL_HTTP_AUTH_H
#define OWL_HTTP_AUTH_H

#include "types.h"
#include "config.h"
#include <stdbool.h>

/**
 * Authentication result with detailed error info
 */
typedef struct {
    bool valid;
    char error[256];           // Error message if !valid
    char subject[256];         // JWT subject (user identifier) if valid
    char scope[512];           // JWT scope (permissions) if valid
    char client_id[128];       // JWT client_id if valid
} AuthResult;

/**
 * Initialize authentication with the given configuration.
 *
 * @param config Server configuration containing auth settings
 * @return 0 on success, -1 on error
 */
int auth_init_config(const ServerConfig* config);

/**
 * Initialize authentication with a simple token (legacy API).
 *
 * @param token The bearer token to use for authentication
 */
void auth_init(const char* token);

/**
 * Validate the Authorization header.
 *
 * @param authorization The Authorization header value
 * @return true if valid, false otherwise
 */
bool auth_validate(const char* authorization);

/**
 * Validate the Authorization header with detailed result.
 *
 * @param authorization The Authorization header value
 * @param result Output: detailed authentication result
 * @return true if valid, false otherwise
 */
bool auth_validate_with_result(const char* authorization, AuthResult* result);

/**
 * Check if authentication is enabled.
 * Returns false if no authentication is configured.
 */
bool auth_is_enabled(void);

/**
 * Get the current authentication mode.
 */
AuthMode auth_get_mode(void);

/**
 * Get the current authentication token.
 * Returns NULL if auth is disabled or in JWT mode.
 */
const char* auth_get_token(void);

/**
 * Validate password for panel authentication.
 * Compares against OWL_PANEL_PASSWORD environment variable.
 *
 * @param password The password to validate
 * @return true if password matches, false otherwise
 */
bool auth_validate_panel_password(const char* password);

/**
 * Shutdown authentication module.
 */
void auth_shutdown(void);

#endif // OWL_HTTP_AUTH_H
