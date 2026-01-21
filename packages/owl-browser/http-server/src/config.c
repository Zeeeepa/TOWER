/**
 * Owl Browser HTTP Server - Configuration Implementation
 */

#include "config.h"
#include "config_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Helper to parse boolean environment variables
static bool parse_env_bool(const char* env_val) {
    if (!env_val) return false;
    return (strcmp(env_val, "1") == 0 ||
            strcmp(env_val, "true") == 0 ||
            strcmp(env_val, "yes") == 0 ||
            strcmp(env_val, "on") == 0);
}

int config_load(ServerConfig* config) {
    if (!config) return -1;

    // Set all defaults
    memset(config, 0, sizeof(ServerConfig));

    // Basic server settings
    strncpy(config->host, DEFAULT_HOST, sizeof(config->host) - 1);
    config->port = DEFAULT_PORT;
    config->max_connections = DEFAULT_MAX_CONNECTIONS;
    config->request_timeout_ms = DEFAULT_REQUEST_TIMEOUT_MS;
    config->browser_timeout_ms = DEFAULT_BROWSER_TIMEOUT_MS;
    config->verbose = false;

    // Rate limiting defaults
    config->rate_limit.enabled = DEFAULT_RATE_LIMIT_ENABLED;
    config->rate_limit.requests_per_window = DEFAULT_RATE_LIMIT_REQUESTS;
    config->rate_limit.window_seconds = DEFAULT_RATE_LIMIT_WINDOW_SEC;
    config->rate_limit.burst_size = DEFAULT_RATE_LIMIT_BURST;

    // IP whitelist defaults
    config->ip_whitelist.enabled = DEFAULT_IP_WHITELIST_ENABLED;
    config->ip_whitelist.count = 0;

    // SSL defaults
    config->ssl.enabled = DEFAULT_SSL_ENABLED;
    config->ssl.verify_client = false;

    // CORS defaults
    config->cors.enabled = DEFAULT_CORS_ENABLED;
    strncpy(config->cors.allowed_origins, "*", sizeof(config->cors.allowed_origins) - 1);
    strncpy(config->cors.allowed_methods, "GET,POST,PUT,DELETE,OPTIONS",
            sizeof(config->cors.allowed_methods) - 1);
    strncpy(config->cors.allowed_headers, "Content-Type,Authorization",
            sizeof(config->cors.allowed_headers) - 1);
    config->cors.max_age_seconds = 86400;

    // Authentication defaults
    config->auth_mode = AUTH_MODE_TOKEN;
    config->jwt.enabled = DEFAULT_JWT_ENABLED;
    strncpy(config->jwt.algorithm, "RS256", sizeof(config->jwt.algorithm) - 1);
    config->jwt.clock_skew_seconds = DEFAULT_JWT_CLOCK_SKEW;
    config->jwt.require_exp = DEFAULT_JWT_REQUIRE_EXP;

    // WebSocket defaults
    config->websocket.enabled = DEFAULT_WS_ENABLED;
    config->websocket.max_connections = DEFAULT_WS_MAX_CONNECTIONS;
    config->websocket.message_max_size = DEFAULT_WS_MESSAGE_MAX_SIZE;
    config->websocket.ping_interval_sec = DEFAULT_WS_PING_INTERVAL_SEC;
    config->websocket.pong_timeout_sec = DEFAULT_WS_PONG_TIMEOUT_SEC;

    // Additional defaults
    config->graceful_shutdown = true;
    config->shutdown_timeout_sec = 30;
    config->keep_alive_timeout_sec = 60;
    config->log_requests = false;

    // Load from environment variables
    const char* env_val;

    // Basic server settings
    env_val = getenv("OWL_HTTP_HOST");
    if (env_val && strlen(env_val) > 0) {
        strncpy(config->host, env_val, sizeof(config->host) - 1);
    }

    env_val = getenv("OWL_HTTP_PORT");
    if (env_val && strlen(env_val) > 0) {
        int port = atoi(env_val);
        if (port > 0 && port <= 65535) {
            config->port = (uint16_t)port;
        }
    }

    env_val = getenv("OWL_HTTP_TOKEN");
    if (env_val && strlen(env_val) > 0) {
        strncpy(config->auth_token, env_val, sizeof(config->auth_token) - 1);
    }

    env_val = getenv("OWL_BROWSER_PATH");
    if (env_val && strlen(env_val) > 0) {
        strncpy(config->browser_path, env_val, sizeof(config->browser_path) - 1);
    }

    env_val = getenv("OWL_HTTP_MAX_CONNECTIONS");
    if (env_val && strlen(env_val) > 0) {
        int max_conn = atoi(env_val);
        if (max_conn > 0) {
            config->max_connections = max_conn;
        }
    }

    env_val = getenv("OWL_HTTP_TIMEOUT");
    if (env_val && strlen(env_val) > 0) {
        int timeout = atoi(env_val);
        if (timeout > 0) {
            config->request_timeout_ms = timeout;
        }
    }

    env_val = getenv("OWL_BROWSER_TIMEOUT");
    if (env_val && strlen(env_val) > 0) {
        int timeout = atoi(env_val);
        if (timeout > 0) {
            config->browser_timeout_ms = timeout;
        }
    }

    env_val = getenv("OWL_HTTP_VERBOSE");
    if (env_val) {
        config->verbose = parse_env_bool(env_val);
    }

    env_val = getenv("OWL_LOG_REQUESTS");
    if (env_val) {
        config->log_requests = parse_env_bool(env_val);
    }

    // Rate limiting environment variables
    env_val = getenv("OWL_RATE_LIMIT_ENABLED");
    if (env_val) {
        config->rate_limit.enabled = parse_env_bool(env_val);
    }

    env_val = getenv("OWL_RATE_LIMIT_REQUESTS");
    if (env_val && strlen(env_val) > 0) {
        int val = atoi(env_val);
        if (val > 0) {
            config->rate_limit.requests_per_window = val;
        }
    }

    env_val = getenv("OWL_RATE_LIMIT_WINDOW");
    if (env_val && strlen(env_val) > 0) {
        int val = atoi(env_val);
        if (val > 0) {
            config->rate_limit.window_seconds = val;
        }
    }

    env_val = getenv("OWL_RATE_LIMIT_BURST");
    if (env_val && strlen(env_val) > 0) {
        int val = atoi(env_val);
        if (val >= 0) {
            config->rate_limit.burst_size = val;
        }
    }

    // IP whitelist environment variables
    env_val = getenv("OWL_IP_WHITELIST_ENABLED");
    if (env_val) {
        config->ip_whitelist.enabled = parse_env_bool(env_val);
    }

    env_val = getenv("OWL_IP_WHITELIST");
    if (env_val && strlen(env_val) > 0) {
        config_parse_ip_whitelist(config, env_val);
    }

    // SSL environment variables
    env_val = getenv("OWL_SSL_ENABLED");
    if (env_val) {
        config->ssl.enabled = parse_env_bool(env_val);
    }

    env_val = getenv("OWL_SSL_CERT");
    if (env_val && strlen(env_val) > 0) {
        strncpy(config->ssl.cert_path, env_val, sizeof(config->ssl.cert_path) - 1);
    }

    env_val = getenv("OWL_SSL_KEY");
    if (env_val && strlen(env_val) > 0) {
        strncpy(config->ssl.key_path, env_val, sizeof(config->ssl.key_path) - 1);
    }

    env_val = getenv("OWL_SSL_CA");
    if (env_val && strlen(env_val) > 0) {
        strncpy(config->ssl.ca_path, env_val, sizeof(config->ssl.ca_path) - 1);
    }

    env_val = getenv("OWL_SSL_VERIFY_CLIENT");
    if (env_val) {
        config->ssl.verify_client = parse_env_bool(env_val);
    }

    // CORS environment variables
    env_val = getenv("OWL_CORS_ENABLED");
    if (env_val) {
        config->cors.enabled = parse_env_bool(env_val);
    }

    env_val = getenv("OWL_CORS_ORIGINS");
    if (env_val && strlen(env_val) > 0) {
        strncpy(config->cors.allowed_origins, env_val,
                sizeof(config->cors.allowed_origins) - 1);
    }

    env_val = getenv("OWL_CORS_METHODS");
    if (env_val && strlen(env_val) > 0) {
        strncpy(config->cors.allowed_methods, env_val,
                sizeof(config->cors.allowed_methods) - 1);
    }

    env_val = getenv("OWL_CORS_HEADERS");
    if (env_val && strlen(env_val) > 0) {
        strncpy(config->cors.allowed_headers, env_val,
                sizeof(config->cors.allowed_headers) - 1);
    }

    env_val = getenv("OWL_CORS_MAX_AGE");
    if (env_val && strlen(env_val) > 0) {
        int val = atoi(env_val);
        if (val >= 0) {
            config->cors.max_age_seconds = val;
        }
    }

    // Additional environment variables
    env_val = getenv("OWL_GRACEFUL_SHUTDOWN");
    if (env_val) {
        config->graceful_shutdown = parse_env_bool(env_val);
    }

    env_val = getenv("OWL_SHUTDOWN_TIMEOUT");
    if (env_val && strlen(env_val) > 0) {
        int val = atoi(env_val);
        if (val > 0) {
            config->shutdown_timeout_sec = val;
        }
    }

    env_val = getenv("OWL_KEEP_ALIVE_TIMEOUT");
    if (env_val && strlen(env_val) > 0) {
        int val = atoi(env_val);
        if (val > 0) {
            config->keep_alive_timeout_sec = val;
        }
    }

    // Authentication mode
    env_val = getenv("OWL_AUTH_MODE");
    if (env_val && strlen(env_val) > 0) {
        if (strcmp(env_val, "jwt") == 0 || strcmp(env_val, "JWT") == 0) {
            config->auth_mode = AUTH_MODE_JWT;
            config->jwt.enabled = true;
        } else {
            config->auth_mode = AUTH_MODE_TOKEN;
            config->jwt.enabled = false;
        }
    }

    // JWT environment variables
    env_val = getenv("OWL_JWT_PUBLIC_KEY");
    if (env_val && strlen(env_val) > 0) {
        strncpy(config->jwt.public_key_path, env_val, sizeof(config->jwt.public_key_path) - 1);
    }

    env_val = getenv("OWL_JWT_PRIVATE_KEY");
    if (env_val && strlen(env_val) > 0) {
        strncpy(config->jwt.private_key_path, env_val, sizeof(config->jwt.private_key_path) - 1);
    }

    env_val = getenv("OWL_JWT_ALGORITHM");
    if (env_val && strlen(env_val) > 0) {
        strncpy(config->jwt.algorithm, env_val, sizeof(config->jwt.algorithm) - 1);
    }

    env_val = getenv("OWL_JWT_ISSUER");
    if (env_val && strlen(env_val) > 0) {
        strncpy(config->jwt.expected_issuer, env_val, sizeof(config->jwt.expected_issuer) - 1);
    }

    env_val = getenv("OWL_JWT_AUDIENCE");
    if (env_val && strlen(env_val) > 0) {
        strncpy(config->jwt.expected_audience, env_val, sizeof(config->jwt.expected_audience) - 1);
    }

    env_val = getenv("OWL_JWT_CLOCK_SKEW");
    if (env_val && strlen(env_val) > 0) {
        int val = atoi(env_val);
        if (val >= 0) {
            config->jwt.clock_skew_seconds = val;
        }
    }

    env_val = getenv("OWL_JWT_REQUIRE_EXP");
    if (env_val) {
        config->jwt.require_exp = parse_env_bool(env_val);
    }

    // WebSocket environment variables
    env_val = getenv("OWL_WS_ENABLED");
    if (env_val) {
        config->websocket.enabled = parse_env_bool(env_val);
    }

    env_val = getenv("OWL_WS_MAX_CONNECTIONS");
    if (env_val && strlen(env_val) > 0) {
        int val = atoi(env_val);
        if (val > 0) {
            config->websocket.max_connections = val;
        }
    }

    env_val = getenv("OWL_WS_MESSAGE_MAX_SIZE");
    if (env_val && strlen(env_val) > 0) {
        int val = atoi(env_val);
        if (val > 0) {
            config->websocket.message_max_size = val;
        }
    }

    env_val = getenv("OWL_WS_PING_INTERVAL");
    if (env_val && strlen(env_val) > 0) {
        int val = atoi(env_val);
        if (val > 0) {
            config->websocket.ping_interval_sec = val;
        }
    }

    env_val = getenv("OWL_WS_PONG_TIMEOUT");
    if (env_val && strlen(env_val) > 0) {
        int val = atoi(env_val);
        if (val > 0) {
            config->websocket.pong_timeout_sec = val;
        }
    }

    // IPC Tests defaults
    config->ipc_tests.enabled = DEFAULT_IPC_TESTS_ENABLED;

    // IPC Tests environment variables
    env_val = getenv("OWL_IPC_TESTS_ENABLED");
    if (env_val) {
        config->ipc_tests.enabled = parse_env_bool(env_val);
    }

    env_val = getenv("OWL_IPC_TEST_CLIENT_PATH");
    if (env_val && strlen(env_val) > 0) {
        strncpy(config->ipc_tests.test_client_path, env_val,
                sizeof(config->ipc_tests.test_client_path) - 1);
    }

    env_val = getenv("OWL_IPC_TEST_REPORTS_DIR");
    if (env_val && strlen(env_val) > 0) {
        strncpy(config->ipc_tests.reports_dir, env_val,
                sizeof(config->ipc_tests.reports_dir) - 1);
    }

    return 0;
}

int config_validate(const ServerConfig* config) {
    if (!config) {
        fprintf(stderr, "Error: NULL config\n");
        return -1;
    }

    // Check authentication configuration
    if (config->auth_mode == AUTH_MODE_JWT) {
        // JWT mode requires public key
        if (strlen(config->jwt.public_key_path) == 0) {
            fprintf(stderr, "Error: JWT mode requires OWL_JWT_PUBLIC_KEY\n");
            return -1;
        }
        // Validate public key file exists
        struct stat st;
        if (stat(config->jwt.public_key_path, &st) != 0) {
            fprintf(stderr, "Error: JWT public key not found: %s\n", config->jwt.public_key_path);
            return -1;
        }
        // Validate private key if specified
        if (strlen(config->jwt.private_key_path) > 0) {
            if (stat(config->jwt.private_key_path, &st) != 0) {
                fprintf(stderr, "Error: JWT private key not found: %s\n", config->jwt.private_key_path);
                return -1;
            }
        }
        // Validate algorithm
        if (strcmp(config->jwt.algorithm, "RS256") != 0 &&
            strcmp(config->jwt.algorithm, "RS384") != 0 &&
            strcmp(config->jwt.algorithm, "RS512") != 0) {
            fprintf(stderr, "Error: Invalid JWT algorithm: %s (use RS256, RS384, or RS512)\n",
                    config->jwt.algorithm);
            return -1;
        }
    } else {
        // Token mode requires auth_token
        if (strlen(config->auth_token) == 0) {
            fprintf(stderr, "Error: OWL_HTTP_TOKEN environment variable is required\n");
            return -1;
        }
    }

    if (strlen(config->browser_path) == 0) {
        fprintf(stderr, "Error: OWL_BROWSER_PATH environment variable is required\n");
        return -1;
    }

    // Check browser binary exists
    struct stat st;
    if (stat(config->browser_path, &st) != 0) {
        fprintf(stderr, "Error: Browser binary not found at: %s\n", config->browser_path);
        return -1;
    }

    // Check it's executable
    if (!(st.st_mode & S_IXUSR)) {
        fprintf(stderr, "Error: Browser binary is not executable: %s\n", config->browser_path);
        return -1;
    }

    // Validate port
    if (config->port == 0) {
        fprintf(stderr, "Error: Invalid port number\n");
        return -1;
    }

    // Validate max connections
    if (config->max_connections <= 0 || config->max_connections > 10000) {
        fprintf(stderr, "Error: max_connections must be between 1 and 10000\n");
        return -1;
    }

    // Validate SSL configuration
    if (config->ssl.enabled) {
        if (strlen(config->ssl.cert_path) == 0) {
            fprintf(stderr, "Error: SSL enabled but OWL_SSL_CERT not set\n");
            return -1;
        }
        if (strlen(config->ssl.key_path) == 0) {
            fprintf(stderr, "Error: SSL enabled but OWL_SSL_KEY not set\n");
            return -1;
        }
        // Check SSL files exist
        if (stat(config->ssl.cert_path, &st) != 0) {
            fprintf(stderr, "Error: SSL certificate not found: %s\n", config->ssl.cert_path);
            return -1;
        }
        if (stat(config->ssl.key_path, &st) != 0) {
            fprintf(stderr, "Error: SSL key not found: %s\n", config->ssl.key_path);
            return -1;
        }
        // Check CA file if specified
        if (strlen(config->ssl.ca_path) > 0 && stat(config->ssl.ca_path, &st) != 0) {
            fprintf(stderr, "Error: SSL CA bundle not found: %s\n", config->ssl.ca_path);
            return -1;
        }
    }

    // Validate rate limit configuration
    if (config->rate_limit.enabled) {
        if (config->rate_limit.requests_per_window <= 0) {
            fprintf(stderr, "Error: rate_limit.requests_per_window must be positive\n");
            return -1;
        }
        if (config->rate_limit.window_seconds <= 0) {
            fprintf(stderr, "Error: rate_limit.window_seconds must be positive\n");
            return -1;
        }
    }

    return 0;
}

void config_print(const ServerConfig* config) {
    if (!config) return;

    fprintf(stderr, "=== Server Configuration ===\n");
    fprintf(stderr, "  Host: %s\n", config->host);
    fprintf(stderr, "  Port: %d\n", config->port);
    fprintf(stderr, "  Auth Mode: %s\n", config->auth_mode == AUTH_MODE_JWT ? "JWT" : "Token");
    if (config->auth_mode == AUTH_MODE_TOKEN) {
        fprintf(stderr, "  Token: %s\n", strlen(config->auth_token) > 0 ? "[SET]" : "[NOT SET]");
    }
    fprintf(stderr, "  Browser Path: %s\n", config->browser_path);
    fprintf(stderr, "  Max Connections: %d\n", config->max_connections);
    fprintf(stderr, "  Request Timeout: %d ms\n", config->request_timeout_ms);
    fprintf(stderr, "  Browser Timeout: %d ms\n", config->browser_timeout_ms);
    fprintf(stderr, "  Verbose: %s\n", config->verbose ? "true" : "false");
    fprintf(stderr, "  Log Requests: %s\n", config->log_requests ? "true" : "false");

    if (config->auth_mode == AUTH_MODE_JWT) {
        fprintf(stderr, "\n--- JWT Authentication ---\n");
        fprintf(stderr, "  Algorithm: %s\n", config->jwt.algorithm);
        fprintf(stderr, "  Public Key: %s\n", config->jwt.public_key_path);
        fprintf(stderr, "  Private Key: %s\n",
                strlen(config->jwt.private_key_path) > 0 ? config->jwt.private_key_path : "[NOT SET]");
        if (strlen(config->jwt.expected_issuer) > 0) {
            fprintf(stderr, "  Expected Issuer: %s\n", config->jwt.expected_issuer);
        }
        if (strlen(config->jwt.expected_audience) > 0) {
            fprintf(stderr, "  Expected Audience: %s\n", config->jwt.expected_audience);
        }
        fprintf(stderr, "  Clock Skew: %d sec\n", config->jwt.clock_skew_seconds);
        fprintf(stderr, "  Require Expiration: %s\n", config->jwt.require_exp ? "true" : "false");
    }

    fprintf(stderr, "\n--- Rate Limiting ---\n");
    fprintf(stderr, "  Enabled: %s\n", config->rate_limit.enabled ? "true" : "false");
    if (config->rate_limit.enabled) {
        fprintf(stderr, "  Requests/Window: %d\n", config->rate_limit.requests_per_window);
        fprintf(stderr, "  Window (sec): %d\n", config->rate_limit.window_seconds);
        fprintf(stderr, "  Burst Size: %d\n", config->rate_limit.burst_size);
    }

    fprintf(stderr, "\n--- IP Whitelist ---\n");
    fprintf(stderr, "  Enabled: %s\n", config->ip_whitelist.enabled ? "true" : "false");
    if (config->ip_whitelist.enabled && config->ip_whitelist.count > 0) {
        fprintf(stderr, "  Entries: %d\n", config->ip_whitelist.count);
        for (int i = 0; i < config->ip_whitelist.count && i < 5; i++) {
            fprintf(stderr, "    - %s\n", config->ip_whitelist.ips[i]);
        }
        if (config->ip_whitelist.count > 5) {
            fprintf(stderr, "    ... and %d more\n", config->ip_whitelist.count - 5);
        }
    }

    fprintf(stderr, "\n--- SSL/TLS ---\n");
    fprintf(stderr, "  Enabled: %s\n", config->ssl.enabled ? "true" : "false");
    if (config->ssl.enabled) {
        fprintf(stderr, "  Cert: %s\n", config->ssl.cert_path);
        fprintf(stderr, "  Key: %s\n", config->ssl.key_path);
        if (strlen(config->ssl.ca_path) > 0) {
            fprintf(stderr, "  CA: %s\n", config->ssl.ca_path);
        }
        fprintf(stderr, "  Verify Client: %s\n", config->ssl.verify_client ? "true" : "false");
    }

    fprintf(stderr, "\n--- CORS ---\n");
    fprintf(stderr, "  Enabled: %s\n", config->cors.enabled ? "true" : "false");
    if (config->cors.enabled) {
        fprintf(stderr, "  Origins: %s\n", config->cors.allowed_origins);
        fprintf(stderr, "  Methods: %s\n", config->cors.allowed_methods);
        fprintf(stderr, "  Headers: %s\n", config->cors.allowed_headers);
        fprintf(stderr, "  Max Age: %d sec\n", config->cors.max_age_seconds);
    }

    fprintf(stderr, "\n--- WebSocket ---\n");
    fprintf(stderr, "  Enabled: %s\n", config->websocket.enabled ? "true" : "false");
    if (config->websocket.enabled) {
        fprintf(stderr, "  Max Connections: %d\n", config->websocket.max_connections);
        fprintf(stderr, "  Max Message Size: %d bytes\n", config->websocket.message_max_size);
        fprintf(stderr, "  Ping Interval: %d sec\n", config->websocket.ping_interval_sec);
        fprintf(stderr, "  Pong Timeout: %d sec\n", config->websocket.pong_timeout_sec);
    }

    fprintf(stderr, "\n--- IPC Tests ---\n");
    fprintf(stderr, "  Enabled: %s\n", config->ipc_tests.enabled ? "true" : "false");
    if (config->ipc_tests.enabled) {
        fprintf(stderr, "  Test Client Path: %s\n",
                strlen(config->ipc_tests.test_client_path) > 0 ? config->ipc_tests.test_client_path : "[NOT SET]");
        fprintf(stderr, "  Reports Directory: %s\n",
                strlen(config->ipc_tests.reports_dir) > 0 ? config->ipc_tests.reports_dir : "[NOT SET]");
    }

    fprintf(stderr, "\n--- Additional ---\n");
    fprintf(stderr, "  Graceful Shutdown: %s\n", config->graceful_shutdown ? "true" : "false");
    fprintf(stderr, "  Shutdown Timeout: %d sec\n", config->shutdown_timeout_sec);
    fprintf(stderr, "  Keep-Alive Timeout: %d sec\n", config->keep_alive_timeout_sec);
    fprintf(stderr, "============================\n");
}
