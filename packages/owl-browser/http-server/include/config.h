/**
 * Owl Browser HTTP Server - Configuration
 *
 * Environment variables and compile-time configuration.
 */

#ifndef OWL_HTTP_CONFIG_H
#define OWL_HTTP_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// Default configuration values
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 8080
#define DEFAULT_MAX_CONNECTIONS 100
#define DEFAULT_BUFFER_SIZE 65536
#define DEFAULT_REQUEST_TIMEOUT_MS 30000
#define DEFAULT_BROWSER_TIMEOUT_MS 60000

// Rate limiting defaults
#define DEFAULT_RATE_LIMIT_ENABLED false
#define DEFAULT_RATE_LIMIT_REQUESTS 100      // requests per window
#define DEFAULT_RATE_LIMIT_WINDOW_SEC 60     // window in seconds
#define DEFAULT_RATE_LIMIT_BURST 20          // burst allowance

// IP filtering defaults
#define DEFAULT_IP_WHITELIST_ENABLED false
#define MAX_WHITELIST_IPS 256

// SSL defaults
#define DEFAULT_SSL_ENABLED false

// CORS defaults
#define DEFAULT_CORS_ENABLED true

// JWT defaults
#define DEFAULT_JWT_ENABLED false
#define DEFAULT_JWT_CLOCK_SKEW 60
#define DEFAULT_JWT_REQUIRE_EXP true

// WebSocket defaults
#define DEFAULT_WS_ENABLED true
#define DEFAULT_WS_MAX_CONNECTIONS 50
#define DEFAULT_WS_MESSAGE_MAX_SIZE (16 * 1024 * 1024)  // 16MB max message
#define DEFAULT_WS_PING_INTERVAL_SEC 30
#define DEFAULT_WS_PONG_TIMEOUT_SEC 10

// IPC Tests defaults
#define DEFAULT_IPC_TESTS_ENABLED false

// Auth mode
typedef enum {
    AUTH_MODE_TOKEN,    // Simple bearer token (default)
    AUTH_MODE_JWT       // JWT with RSA signing
} AuthMode;

// Limits
#define MAX_CONTEXTS 256
#define MAX_HEADER_SIZE 8192
#define MAX_BODY_SIZE (16 * 1024 * 1024)  // 16MB max body
#define MAX_URL_LENGTH 4096
#define MAX_TOKEN_LENGTH 256
#define MAX_PATH_LENGTH 4096

// Rate limiting configuration
typedef struct {
    bool enabled;
    int requests_per_window;    // max requests per time window
    int window_seconds;         // time window in seconds
    int burst_size;             // allowed burst above limit
} RateLimitConfig;

// IP whitelist configuration
typedef struct {
    bool enabled;
    char ips[MAX_WHITELIST_IPS][64];  // IP addresses or CIDR ranges
    int count;
} IpWhitelistConfig;

// SSL/TLS configuration
typedef struct {
    bool enabled;
    char cert_path[MAX_PATH_LENGTH];   // path to certificate file (.pem or .crt)
    char key_path[MAX_PATH_LENGTH];    // path to private key file (.pem or .key)
    char ca_path[MAX_PATH_LENGTH];     // optional: path to CA bundle for client cert verification
    bool verify_client;                // require client certificates
} SslConfig;

// CORS configuration
typedef struct {
    bool enabled;
    char allowed_origins[1024];        // comma-separated origins or "*"
    char allowed_methods[256];         // comma-separated methods
    char allowed_headers[512];         // comma-separated headers
    int max_age_seconds;               // preflight cache duration
} CorsConfig;

// JWT configuration
typedef struct {
    bool enabled;
    char public_key_path[MAX_PATH_LENGTH];   // path to public key (.pem) for verification
    char private_key_path[MAX_PATH_LENGTH];  // path to private key (.pem) for signing
    char algorithm[16];                       // RS256, RS384, RS512
    char expected_issuer[256];               // expected token issuer (optional)
    char expected_audience[256];             // expected token audience (optional)
    int clock_skew_seconds;                  // allowed clock skew (default: 60)
    bool require_exp;                        // require expiration claim (default: true)
} JwtConfig;

// WebSocket configuration
typedef struct {
    bool enabled;                      // enable WebSocket support (default: true)
    int max_connections;               // max concurrent WebSocket connections (default: 50)
    int message_max_size;              // max message size in bytes (default: 16MB)
    int ping_interval_sec;             // send ping every N seconds (default: 30)
    int pong_timeout_sec;              // close connection if no pong after N seconds (default: 10)
} WebSocketConfig;

// IPC Tests configuration
typedef struct {
    bool enabled;                              // enable IPC tests feature (default: false)
    char test_client_path[MAX_PATH_LENGTH];    // path to ipc_test_client binary
    char reports_dir[MAX_PATH_LENGTH];         // directory for test reports
} IpcTestsConfig;

// Server configuration structure
typedef struct {
    char host[256];
    uint16_t port;
    char auth_token[MAX_TOKEN_LENGTH];
    char browser_path[MAX_PATH_LENGTH];
    int max_connections;
    int request_timeout_ms;
    int browser_timeout_ms;
    bool verbose;

    // Authentication
    AuthMode auth_mode;                // TOKEN or JWT
    JwtConfig jwt;                     // JWT configuration (when auth_mode == AUTH_MODE_JWT)

    // Security features
    RateLimitConfig rate_limit;
    IpWhitelistConfig ip_whitelist;
    SslConfig ssl;
    CorsConfig cors;

    // WebSocket
    WebSocketConfig websocket;

    // IPC Tests
    IpcTestsConfig ipc_tests;

    // Additional options
    bool graceful_shutdown;            // wait for active connections on shutdown
    int shutdown_timeout_sec;          // max wait time for graceful shutdown
    int keep_alive_timeout_sec;        // HTTP keep-alive timeout
    bool log_requests;                 // log all requests (even without verbose)
} ServerConfig;

/**
 * Load configuration from environment variables.
 *
 * Environment variables:
 *   OWL_HTTP_HOST - Server host (default: 127.0.0.1)
 *   OWL_HTTP_PORT - Server port (default: 8080)
 *   OWL_HTTP_TOKEN - Authorization bearer token (required if not using JWT)
 *   OWL_BROWSER_PATH - Path to browser binary (required)
 *   OWL_HTTP_MAX_CONNECTIONS - Max concurrent connections (default: 100)
 *   OWL_HTTP_TIMEOUT - Request timeout in ms (default: 30000)
 *   OWL_BROWSER_TIMEOUT - Browser command timeout in ms (default: 60000)
 *   OWL_HTTP_VERBOSE - Enable verbose logging (default: false)
 *
 * Authentication (choose one mode):
 *   OWL_AUTH_MODE - "token" or "jwt" (default: "token")
 *
 * JWT Authentication (when OWL_AUTH_MODE=jwt):
 *   OWL_JWT_PUBLIC_KEY - Path to RSA public key (.pem) for verification (required)
 *   OWL_JWT_PRIVATE_KEY - Path to RSA private key (.pem) for signing (optional)
 *   OWL_JWT_ALGORITHM - RS256, RS384, or RS512 (default: RS256)
 *   OWL_JWT_ISSUER - Expected token issuer (optional)
 *   OWL_JWT_AUDIENCE - Expected token audience (optional)
 *   OWL_JWT_CLOCK_SKEW - Allowed clock skew in seconds (default: 60)
 *   OWL_JWT_REQUIRE_EXP - Require expiration claim (default: true)
 *
 * Rate Limiting:
 *   OWL_RATE_LIMIT_ENABLED - Enable rate limiting (default: false)
 *   OWL_RATE_LIMIT_REQUESTS - Max requests per window (default: 100)
 *   OWL_RATE_LIMIT_WINDOW - Window size in seconds (default: 60)
 *   OWL_RATE_LIMIT_BURST - Burst allowance (default: 20)
 *
 * IP Whitelist:
 *   OWL_IP_WHITELIST_ENABLED - Enable IP whitelist (default: false)
 *   OWL_IP_WHITELIST - Comma-separated list of IPs or CIDR ranges
 *
 * SSL/TLS:
 *   OWL_SSL_ENABLED - Enable HTTPS (default: false)
 *   OWL_SSL_CERT - Path to SSL certificate (.pem or .crt)
 *   OWL_SSL_KEY - Path to SSL private key (.pem or .key)
 *   OWL_SSL_CA - Path to CA bundle for client cert verification (optional)
 *   OWL_SSL_VERIFY_CLIENT - Require client certificates (default: false)
 *
 * CORS:
 *   OWL_CORS_ENABLED - Enable CORS headers (default: true)
 *   OWL_CORS_ORIGINS - Allowed origins, comma-separated or "*" (default: "*")
 *   OWL_CORS_METHODS - Allowed methods (default: "GET,POST,PUT,DELETE,OPTIONS")
 *   OWL_CORS_HEADERS - Allowed headers (default: "Content-Type,Authorization")
 *   OWL_CORS_MAX_AGE - Preflight cache duration in seconds (default: 86400)
 *
 * WebSocket:
 *   OWL_WS_ENABLED - Enable WebSocket support at /ws (default: true)
 *   OWL_WS_MAX_CONNECTIONS - Max concurrent WebSocket connections (default: 50)
 *   OWL_WS_MESSAGE_MAX_SIZE - Max message size in bytes (default: 16MB)
 *   OWL_WS_PING_INTERVAL - Ping interval in seconds (default: 30)
 *   OWL_WS_PONG_TIMEOUT - Pong timeout in seconds (default: 10)
 *
 * IPC Tests:
 *   OWL_IPC_TESTS_ENABLED - Enable IPC tests feature (default: false)
 *   OWL_IPC_TEST_CLIENT_PATH - Path to ipc_test_client binary
 *   OWL_IPC_TEST_REPORTS_DIR - Directory for test reports
 *
 * Additional:
 *   OWL_GRACEFUL_SHUTDOWN - Wait for active connections on shutdown (default: true)
 *   OWL_SHUTDOWN_TIMEOUT - Max wait time for graceful shutdown in seconds (default: 30)
 *   OWL_KEEP_ALIVE_TIMEOUT - HTTP keep-alive timeout in seconds (default: 60)
 *   OWL_LOG_REQUESTS - Log all requests (default: false)
 */
int config_load(ServerConfig* config);

/**
 * Validate configuration.
 * Returns 0 on success, -1 on error with message printed to stderr.
 */
int config_validate(const ServerConfig* config);

/**
 * Print configuration (for debugging).
 */
void config_print(const ServerConfig* config);

#endif // OWL_HTTP_CONFIG_H
