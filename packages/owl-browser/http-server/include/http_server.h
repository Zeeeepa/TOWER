/**
 * Owl Browser HTTP Server - HTTP Server Core
 *
 * High-performance HTTP server using non-blocking I/O.
 * Supports concurrent connections with proper error handling.
 */

#ifndef OWL_HTTP_SERVER_H
#define OWL_HTTP_SERVER_H

#include "types.h"
#include "config.h"
#include <stdbool.h>

/**
 * Request handler callback type.
 */
typedef int (*RequestHandler)(const HttpRequest* request, HttpResponse* response);

/**
 * Initialize the HTTP server.
 *
 * @param config Server configuration
 * @param handler Request handler callback
 * @return 0 on success, -1 on error
 */
int http_server_init(const ServerConfig* config, RequestHandler handler);

/**
 * Start the HTTP server (blocking).
 * This will run until http_server_stop() is called.
 *
 * @return 0 on graceful shutdown, -1 on error
 */
int http_server_run(void);

/**
 * Signal the server to stop.
 * Can be called from a signal handler.
 */
void http_server_stop(void);

/**
 * Check if server is running.
 */
bool http_server_is_running(void);

/**
 * Get server statistics.
 */
typedef struct {
    // Basic counters
    uint64_t requests_total;
    uint64_t requests_success;
    uint64_t requests_error;
    uint64_t bytes_received;
    uint64_t bytes_sent;
    int active_connections;
    int64_t uptime_seconds;

    // Concurrency metrics
    uint64_t requests_concurrent_peak;   // Peak concurrent requests ever
    uint64_t requests_concurrent_current; // Current concurrent requests
    uint64_t thread_pool_tasks_submitted;
    uint64_t thread_pool_tasks_completed;
    int thread_pool_active_workers;
    int thread_pool_pending_tasks;
    int thread_pool_num_threads;

    // Latency metrics (microseconds)
    uint64_t latency_total_us;       // Sum of all request latencies
    uint64_t latency_min_us;         // Minimum latency
    uint64_t latency_max_us;         // Maximum latency
    uint64_t latency_count;          // Number of measured requests

    // Per-second metrics (computed from last window)
    double requests_per_second;
    double bytes_per_second_in;
    double bytes_per_second_out;
} ServerStats;

void http_server_get_stats(ServerStats* stats);

/**
 * Shutdown the HTTP server and free resources.
 */
void http_server_shutdown(void);

// ============================================================================
// HTTP Utilities
// ============================================================================

/**
 * Parse HTTP method string.
 */
HttpMethod http_parse_method(const char* method_str);

/**
 * Get HTTP status text.
 */
const char* http_status_text(HttpStatus status);

/**
 * URL decode a string.
 * Caller must free the result.
 */
char* http_url_decode(const char* str);

/**
 * Parse query string into key-value pairs.
 * Returns number of pairs parsed.
 */
typedef struct {
    char key[256];
    char value[1024];
} QueryParam;

int http_parse_query_string(const char* query, QueryParam* params, int max_params);

#endif // OWL_HTTP_SERVER_H
