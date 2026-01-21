/**
 * Owl Browser HTTP Server - HTTP Server Core Implementation
 *
 * High-performance HTTP server using poll-based I/O.
 * Includes WebSocket support for two-way communication.
 */

#include "http_server.h"
#include "websocket.h"
#include "thread_pool.h"
#include "video_stream.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>

// ============================================================================
// Server State
// ============================================================================

// Connection processing state for thread pool
typedef enum {
    CONN_STATE_IDLE,           // Waiting for data
    CONN_STATE_READING,        // Reading request
    CONN_STATE_PROCESSING,     // Being processed by worker thread
    CONN_STATE_WRITING,        // Writing response
    CONN_STATE_CLOSED          // Connection closed
} ConnState;

typedef struct {
    int fd;
    char* recv_buf;
    size_t recv_size;
    size_t recv_capacity;
    char* send_buf;
    size_t send_size;
    size_t send_offset;
    time_t last_activity;
    bool headers_complete;
    size_t content_length;
    char client_ip[64];  // Store client IP for rate limiting/filtering
    bool is_websocket;   // Set to true after WebSocket upgrade
    char ws_key[128];    // Sec-WebSocket-Key for upgrade
    char upgrade_header[32];  // Upgrade header value
    char connection_header[64]; // Connection header value

    // Thread pool support
    atomic_int state;          // ConnState - atomic for thread safety
    pthread_mutex_t mutex;     // Protects send_buf and other fields during async processing
    HttpRequest pending_request;  // Request to be processed
    bool request_valid;        // Whether pending_request is valid

    // Latency tracking
    struct timeval request_start_time;  // When request was received
} Connection;

// Video stream thread arguments
typedef struct {
    int fd;
    char path[4096];
    char client_ip[64];
    char auth_header[512];
    char cookie_header[1024];
} VideoStreamArgs;

static struct {
    int listen_fd;
    ServerConfig config;
    RequestHandler handler;
    volatile bool running;
    pthread_mutex_t stats_mutex;
    ServerStats stats;
    time_t start_time;
    Connection* connections;
    struct pollfd* poll_fds;
    int max_connections;

    // Thread pool for concurrent request processing
    ThreadPool* thread_pool;
    bool use_thread_pool;

    // Atomic counters for lock-free stats updates (high-frequency)
    atomic_uint_least64_t bytes_received_atomic;
    atomic_uint_least64_t bytes_sent_atomic;
    atomic_uint_least64_t requests_total_atomic;
    atomic_uint_least64_t requests_success_atomic;
    atomic_uint_least64_t requests_error_atomic;
    atomic_int active_connections_atomic;

    // Extended statistics
    atomic_uint_least64_t concurrent_requests;  // Current concurrent requests being processed
    atomic_uint_least64_t concurrent_peak;      // Peak concurrent requests
    atomic_uint_least64_t latency_total_us;     // Total latency in microseconds
    atomic_uint_least64_t latency_min_us;       // Minimum latency
    atomic_uint_least64_t latency_max_us;       // Maximum latency
    atomic_uint_least64_t latency_count;        // Number of latency samples

    // Rate tracking (for requests per second)
    struct timeval last_rate_check;
    uint64_t last_requests_count;
    uint64_t last_bytes_in;
    uint64_t last_bytes_out;
    double current_rps;
    double current_bps_in;
    double current_bps_out;
} g_server = {
    .listen_fd = -1,
    .running = false,
    .thread_pool = NULL,
    .use_thread_pool = true
};

// ============================================================================
// HTTP Parsing
// ============================================================================

HttpMethod http_parse_method(const char* method_str) {
    if (!method_str) return HTTP_UNKNOWN;
    if (strcmp(method_str, "GET") == 0) return HTTP_GET;
    if (strcmp(method_str, "POST") == 0) return HTTP_POST;
    if (strcmp(method_str, "PUT") == 0) return HTTP_PUT;
    if (strcmp(method_str, "DELETE") == 0) return HTTP_DELETE;
    if (strcmp(method_str, "OPTIONS") == 0) return HTTP_OPTIONS;
    return HTTP_UNKNOWN;
}

const char* http_status_text(HttpStatus status) {
    switch (status) {
        case HTTP_101_SWITCHING_PROTOCOLS: return "Switching Protocols";
        case HTTP_200_OK: return "OK";
        case HTTP_201_CREATED: return "Created";
        case HTTP_204_NO_CONTENT: return "No Content";
        case HTTP_400_BAD_REQUEST: return "Bad Request";
        case HTTP_401_UNAUTHORIZED: return "Unauthorized";
        case HTTP_403_FORBIDDEN: return "Forbidden";
        case HTTP_404_NOT_FOUND: return "Not Found";
        case HTTP_405_METHOD_NOT_ALLOWED: return "Method Not Allowed";
        case HTTP_408_REQUEST_TIMEOUT: return "Request Timeout";
        case HTTP_413_PAYLOAD_TOO_LARGE: return "Payload Too Large";
        case HTTP_422_UNPROCESSABLE_ENTITY: return "Unprocessable Entity";
        case HTTP_429_TOO_MANY_REQUESTS: return "Too Many Requests";
        case HTTP_500_INTERNAL_ERROR: return "Internal Server Error";
        case HTTP_502_BAD_GATEWAY: return "Bad Gateway";
        case HTTP_503_SERVICE_UNAVAILABLE: return "Service Unavailable";
        default: return "Unknown";
    }
}

char* http_url_decode(const char* str) {
    if (!str) return NULL;

    size_t len = strlen(str);
    char* result = malloc(len + 1);
    if (!result) return NULL;

    char* out = result;
    const char* in = str;

    while (*in) {
        if (*in == '%' && in[1] && in[2]) {
            char hex[3] = {in[1], in[2], '\0'};
            *out++ = (char)strtol(hex, NULL, 16);
            in += 3;
        } else if (*in == '+') {
            *out++ = ' ';
            in++;
        } else {
            *out++ = *in++;
        }
    }
    *out = '\0';

    return result;
}

int http_parse_query_string(const char* query, QueryParam* params, int max_params) {
    if (!query || !params || max_params <= 0) return 0;

    int count = 0;
    char* copy = strdup(query);
    if (!copy) return 0;

    char* token = strtok(copy, "&");
    while (token && count < max_params) {
        char* eq = strchr(token, '=');
        if (eq) {
            *eq = '\0';
            char* key = http_url_decode(token);
            char* value = http_url_decode(eq + 1);
            if (key && value) {
                strncpy(params[count].key, key, sizeof(params[count].key) - 1);
                strncpy(params[count].value, value, sizeof(params[count].value) - 1);
                count++;
            }
            free(key);
            free(value);
        }
        token = strtok(NULL, "&");
    }

    free(copy);
    return count;
}

// Parse result codes
#define PARSE_OK 0
#define PARSE_NEED_MORE -1
#define PARSE_ERROR -2
#define PARSE_TOO_LARGE -3

static int parse_request(Connection* conn, HttpRequest* request) {
    http_request_init(request);

    char* buf = conn->recv_buf;
    char* header_end = strstr(buf, "\r\n\r\n");
    if (!header_end) return PARSE_NEED_MORE;  // Headers not complete

    // Parse request line
    char* line_end = strstr(buf, "\r\n");
    if (!line_end || line_end > header_end) return PARSE_ERROR;

    char method[16] = {0};
    char path[4096] = {0};
    char version[16] = {0};

    if (sscanf(buf, "%15s %4095s %15s", method, path, version) != 3) {
        return PARSE_ERROR;
    }

    request->method = http_parse_method(method);

    // Split path and query string
    char* query = strchr(path, '?');
    if (query) {
        *query = '\0';
        strncpy(request->query_string, query + 1, sizeof(request->query_string) - 1);
    }
    strncpy(request->path, path, sizeof(request->path) - 1);

    // Parse headers
    char* line = line_end + 2;
    while (line < header_end) {
        char* next_line = strstr(line, "\r\n");
        if (!next_line) break;

        // Parse header
        char* colon = strchr(line, ':');
        if (colon && colon < next_line) {
            *colon = '\0';
            char* value = colon + 1;
            while (*value == ' ') value++;

            // Trim value
            char* value_end = next_line;
            while (value_end > value && (value_end[-1] == ' ' || value_end[-1] == '\r')) {
                value_end--;
            }
            *value_end = '\0';

            // Store known headers
            if (strcasecmp(line, "Content-Type") == 0) {
                strncpy(request->content_type, value, sizeof(request->content_type) - 1);
            } else if (strcasecmp(line, "Content-Length") == 0) {
                request->content_length = strtoul(value, NULL, 10);
            } else if (strcasecmp(line, "Authorization") == 0) {
                strncpy(request->authorization, value, sizeof(request->authorization) - 1);
            } else if (strcasecmp(line, "Cookie") == 0) {
                strncpy(request->cookie, value, sizeof(request->cookie) - 1);
            } else if (strcasecmp(line, "Upgrade") == 0) {
                strncpy(conn->upgrade_header, value, sizeof(conn->upgrade_header) - 1);
            } else if (strcasecmp(line, "Connection") == 0) {
                strncpy(conn->connection_header, value, sizeof(conn->connection_header) - 1);
            } else if (strcasecmp(line, "Sec-WebSocket-Key") == 0) {
                strncpy(conn->ws_key, value, sizeof(conn->ws_key) - 1);
            }
        }

        line = next_line + 2;
    }

    // Early validation: check Content-Length before reading body
    // This prevents DoS attacks that send huge Content-Length values
    if (request->content_length > MAX_BODY_SIZE) {
        LOG_WARN("Server", "Request body too large: %zu > %d bytes",
                 request->content_length, MAX_BODY_SIZE);
        return PARSE_TOO_LARGE;
    }

    // Check for body
    size_t header_len = (header_end + 4) - buf;
    size_t body_received = conn->recv_size - header_len;

    if (request->content_length > 0) {
        if (body_received < request->content_length) {
            conn->content_length = request->content_length;
            return PARSE_NEED_MORE;  // Need more data
        }

        request->body = malloc(request->content_length + 1);
        if (request->body) {
            memcpy(request->body, header_end + 4, request->content_length);
            request->body[request->content_length] = '\0';
            request->body_size = request->content_length;
        }
    }

    return PARSE_OK;
}

// ============================================================================
// Connection Management
// ============================================================================

static void connection_init(Connection* conn) {
    memset(conn, 0, sizeof(*conn));
    conn->fd = -1;
    conn->recv_capacity = 4096;
    conn->recv_buf = malloc(conn->recv_capacity);
    conn->last_activity = time(NULL);
    conn->is_websocket = false;
    atomic_init(&conn->state, CONN_STATE_IDLE);
    pthread_mutex_init(&conn->mutex, NULL);
    conn->request_valid = false;
    http_request_init(&conn->pending_request);
}

static void connection_reset(Connection* conn) {
    pthread_mutex_lock(&conn->mutex);
    if (conn->fd >= 0) {
        close(conn->fd);
        conn->fd = -1;
    }
    conn->recv_size = 0;
    if (conn->send_buf) {
        free(conn->send_buf);
        conn->send_buf = NULL;
    }
    conn->send_size = 0;
    conn->send_offset = 0;
    conn->headers_complete = false;
    conn->content_length = 0;
    conn->is_websocket = false;
    conn->ws_key[0] = '\0';
    conn->upgrade_header[0] = '\0';
    conn->connection_header[0] = '\0';
    if (conn->request_valid) {
        http_request_free(&conn->pending_request);
        conn->request_valid = false;
    }
    atomic_store(&conn->state, CONN_STATE_IDLE);
    pthread_mutex_unlock(&conn->mutex);
}

static void connection_free(Connection* conn) {
    connection_reset(conn);
    free(conn->recv_buf);
    conn->recv_buf = NULL;
    pthread_mutex_destroy(&conn->mutex);
}

static int build_response(Connection* conn, HttpResponse* response) {
    // Build HTTP response
    size_t body_len = response->body_size;
    size_t header_size = 512 + (response->content_type[0] ? strlen(response->content_type) : 0);

    conn->send_buf = malloc(header_size + body_len);
    if (!conn->send_buf) return -1;

    // Write status line and headers
    int n = snprintf(conn->send_buf, header_size,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        "\r\n",
        response->status,
        http_status_text(response->status),
        response->content_type[0] ? response->content_type : "application/json",
        body_len
    );

    if (body_len > 0 && response->body) {
        memcpy(conn->send_buf + n, response->body, body_len);
    }

    conn->send_size = n + body_len;
    conn->send_offset = 0;

    return 0;
}

// ============================================================================
// Latency Tracking Helpers
// ============================================================================

static inline uint64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

static inline uint64_t timeval_diff_us(struct timeval* start, struct timeval* end) {
    return ((uint64_t)end->tv_sec - start->tv_sec) * 1000000 +
           ((int64_t)end->tv_usec - start->tv_usec);
}

static void record_latency(uint64_t latency_us) {
    // Increment total latency
    atomic_fetch_add(&g_server.latency_total_us, latency_us);
    atomic_fetch_add(&g_server.latency_count, 1);

    // Update min latency (atomically)
    uint64_t current_min = atomic_load(&g_server.latency_min_us);
    while (latency_us < current_min) {
        if (atomic_compare_exchange_weak(&g_server.latency_min_us, &current_min, latency_us)) {
            break;
        }
    }

    // Update max latency (atomically)
    uint64_t current_max = atomic_load(&g_server.latency_max_us);
    while (latency_us > current_max) {
        if (atomic_compare_exchange_weak(&g_server.latency_max_us, &current_max, latency_us)) {
            break;
        }
    }
}

static void increment_concurrent_requests(void) {
    uint64_t current = atomic_fetch_add(&g_server.concurrent_requests, 1) + 1;
    uint64_t peak = atomic_load(&g_server.concurrent_peak);
    while (current > peak) {
        if (atomic_compare_exchange_weak(&g_server.concurrent_peak, &peak, current)) {
            break;
        }
    }
}

static void decrement_concurrent_requests(void) {
    atomic_fetch_sub(&g_server.concurrent_requests, 1);
}

// ============================================================================
// Video Stream Thread Worker
// ============================================================================

// Worker function for video streaming - runs in dedicated thread
static void* video_stream_worker(void* arg) {
    VideoStreamArgs* args = (VideoStreamArgs*)arg;

    LOG_DEBUG("Server", "Video stream thread started for %s", args->path);

    video_stream_handle_request(args->fd, args->path, args->client_ip,
                                 args->auth_header, args->cookie_header);

    // Close the socket when done
    close(args->fd);

    LOG_DEBUG("Server", "Video stream thread ended for %s", args->path);

    free(args);
    return NULL;
}

// ============================================================================
// Thread Pool Worker
// ============================================================================

// Worker function for thread pool - processes a single HTTP request
static void request_worker(void* arg) {
    Connection* conn = (Connection*)arg;

    // Verify connection is still valid and in processing state
    if (atomic_load(&conn->state) != CONN_STATE_PROCESSING) {
        return;
    }

    // Track concurrent requests
    increment_concurrent_requests();

    pthread_mutex_lock(&conn->mutex);

    if (!conn->request_valid || conn->fd < 0) {
        pthread_mutex_unlock(&conn->mutex);
        atomic_store(&conn->state, CONN_STATE_IDLE);
        decrement_concurrent_requests();
        return;
    }

    // Get a copy of the request (handler owns request memory)
    HttpRequest* request = &conn->pending_request;

    LOG_DEBUG("ThreadPool", "Processing %s %s from %s",
             request->method == HTTP_GET ? "GET" :
             request->method == HTTP_POST ? "POST" :
             request->method == HTTP_OPTIONS ? "OPTIONS" : "OTHER",
             request->path, request->client_ip);

    // Process the request
    HttpResponse response;
    http_response_init(&response);

    g_server.handler(request, &response);

    // Build response into connection's send buffer
    build_response(conn, &response);
    http_response_free(&response);

    // Calculate latency
    struct timeval now;
    gettimeofday(&now, NULL);
    uint64_t latency_us = timeval_diff_us(&conn->request_start_time, &now);
    record_latency(latency_us);

    // Clean up request
    http_request_free(request);
    conn->request_valid = false;

    pthread_mutex_unlock(&conn->mutex);

    // Transition to writing state
    atomic_store(&conn->state, CONN_STATE_WRITING);

    // Update stats (lock-free atomic operations)
    atomic_fetch_add(&g_server.requests_total_atomic, 1);
    if (response.status < 400) {
        atomic_fetch_add(&g_server.requests_success_atomic, 1);
    } else {
        atomic_fetch_add(&g_server.requests_error_atomic, 1);
    }

    // Decrement concurrent requests
    decrement_concurrent_requests();
}

// ============================================================================
// Server Core
// ============================================================================

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int http_server_init(const ServerConfig* config, RequestHandler handler) {
    if (!config || !handler) return -1;

    memcpy(&g_server.config, config, sizeof(ServerConfig));
    g_server.handler = handler;
    g_server.max_connections = config->max_connections;

    // Initialize connections array
    g_server.connections = calloc(g_server.max_connections, sizeof(Connection));
    g_server.poll_fds = calloc(g_server.max_connections + 1, sizeof(struct pollfd));

    if (!g_server.connections || !g_server.poll_fds) {
        LOG_ERROR("Server", "Failed to allocate connection arrays");
        return -1;
    }

    for (int i = 0; i < g_server.max_connections; i++) {
        connection_init(&g_server.connections[i]);
        g_server.poll_fds[i + 1].fd = -1;
    }

    // Create listen socket
    g_server.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server.listen_fd < 0) {
        LOG_ERROR("Server", "Failed to create socket: %s", strerror(errno));
        return -1;
    }

    // Set socket options
    int opt = 1;
    setsockopt(g_server.listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(g_server.listen_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    // Bind
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config->port);

    if (inet_pton(AF_INET, config->host, &addr.sin_addr) <= 0) {
        LOG_ERROR("Server", "Invalid host address: %s", config->host);
        close(g_server.listen_fd);
        return -1;
    }

    if (bind(g_server.listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("Server", "Failed to bind: %s", strerror(errno));
        close(g_server.listen_fd);
        return -1;
    }

    // Listen
    if (listen(g_server.listen_fd, 128) < 0) {
        LOG_ERROR("Server", "Failed to listen: %s", strerror(errno));
        close(g_server.listen_fd);
        return -1;
    }

    set_nonblocking(g_server.listen_fd);

    pthread_mutex_init(&g_server.stats_mutex, NULL);
    g_server.start_time = time(NULL);

    // Initialize extended statistics
    atomic_init(&g_server.concurrent_requests, 0);
    atomic_init(&g_server.concurrent_peak, 0);
    atomic_init(&g_server.latency_total_us, 0);
    atomic_init(&g_server.latency_min_us, UINT64_MAX);  // Start at max so first value is recorded
    atomic_init(&g_server.latency_max_us, 0);
    atomic_init(&g_server.latency_count, 0);
    gettimeofday(&g_server.last_rate_check, NULL);
    g_server.last_requests_count = 0;
    g_server.last_bytes_in = 0;
    g_server.last_bytes_out = 0;
    g_server.current_rps = 0.0;
    g_server.current_bps_in = 0.0;
    g_server.current_bps_out = 0.0;

    // Initialize thread pool for concurrent request processing
    if (g_server.use_thread_pool) {
        ThreadPoolConfig pool_config = {
            .num_threads = 0,  // Auto-detect CPU cores
            .queue_size = g_server.max_connections * 2,
            .start_immediately = true
        };
        g_server.thread_pool = thread_pool_create(&pool_config);
        if (g_server.thread_pool) {
            ThreadPoolStats stats;
            thread_pool_stats(g_server.thread_pool, &stats);
            LOG_INFO("Server", "Thread pool initialized with %d workers", stats.num_threads);
        } else {
            LOG_WARN("Server", "Failed to create thread pool, falling back to single-threaded mode");
            g_server.use_thread_pool = false;
        }
    }

    LOG_INFO("Server", "Listening on %s:%d", config->host, config->port);

    return 0;
}

int http_server_run(void) {
    g_server.running = true;

    g_server.poll_fds[0].fd = g_server.listen_fd;
    g_server.poll_fds[0].events = POLLIN;

    while (g_server.running) {
        // Build poll fd array
        int nfds = 1;
        for (int i = 0; i < g_server.max_connections; i++) {
            Connection* conn = &g_server.connections[i];
            if (conn->fd >= 0) {
                ConnState state = atomic_load(&conn->state);

                // Skip connections being processed by worker threads
                if (state == CONN_STATE_PROCESSING) {
                    continue;
                }

                g_server.poll_fds[nfds].fd = conn->fd;
                g_server.poll_fds[nfds].events = 0;

                // Only accept reads if idle or reading
                if (state == CONN_STATE_IDLE || state == CONN_STATE_READING) {
                    g_server.poll_fds[nfds].events |= POLLIN;
                }

                // Accept writes if writing state or has pending data
                if (state == CONN_STATE_WRITING || conn->send_size > conn->send_offset) {
                    g_server.poll_fds[nfds].events |= POLLOUT;
                }

                nfds++;
            }
        }

        int ret = poll(g_server.poll_fds, nfds, 10);  // 10ms timeout for low latency

        if (ret < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("Server", "Poll error: %s", strerror(errno));
            break;
        }

        if (ret == 0) {
            // Timeout - check for stale connections
            time_t now = time(NULL);
            for (int i = 0; i < g_server.max_connections; i++) {
                Connection* conn = &g_server.connections[i];
                if (conn->fd >= 0) {
                    int timeout_sec = g_server.config.request_timeout_ms / 1000;
                    if (now - conn->last_activity > timeout_sec) {
                        LOG_DEBUG("Server", "Connection timeout on fd %d", conn->fd);
                        connection_reset(conn);
                    }
                }
            }

            // Run WebSocket periodic tasks (ping/pong, cleanup)
            if (ws_is_enabled()) {
                ws_periodic_tasks();
            }

            // NOTE: Don't continue here - fall through to process WebSocket connections
            // This ensures WebSocket messages are processed even when no HTTP events occur
        }

        // Periodic maintenance tasks (run every poll cycle when idle)
        // Rate limiter cleanup - removes expired entries to prevent memory growth
        extern void rate_limit_cleanup(void);
        rate_limit_cleanup();

        // Process WebSocket connections - only if there was poll activity
        // Note: We still check all WS connections because they're not in the main poll array
        // A future optimization would add WS fds to the poll array directly
        if (ws_is_enabled()) {
            int ws_count = 0;
            WsConnection** ws_conns = ws_get_connections(&ws_count);

            for (int i = 0; i < ws_count && ws_conns; i++) {
                WsConnection* ws_conn = ws_conns[i];
                if (!ws_conn) continue;

                int ws_fd = ws_get_fd(ws_conn);
                if (ws_fd < 0) continue;

                WsState ws_state = ws_get_state(ws_conn);
                if (ws_state != WS_STATE_OPEN) continue;

                // Process any available data (non-blocking read)
                int read_result = ws_process_read(ws_conn);
                if (read_result < 0) {
                    // Connection error or close - WebSocket module handles cleanup
                    LOG_DEBUG("Server", "WebSocket connection closed/error on fd %d",
                              ws_fd);
                }

                // Write any pending data
                if (ws_has_pending_write(ws_conn)) {
                    int write_result = ws_process_write(ws_conn);
                    if (write_result < 0) {
                        LOG_DEBUG("Server", "WebSocket write error on fd %d", ws_fd);
                    }
                }
            }
        }

        // Handle listen socket
        if (g_server.poll_fds[0].revents & POLLIN) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(g_server.listen_fd,
                                  (struct sockaddr*)&client_addr, &client_len);

            if (client_fd >= 0) {
                set_nonblocking(client_fd);

                int opt = 1;
                setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

                // Find free connection slot
                int slot = -1;
                for (int i = 0; i < g_server.max_connections; i++) {
                    if (g_server.connections[i].fd < 0) {
                        slot = i;
                        break;
                    }
                }

                if (slot >= 0) {
                    Connection* conn = &g_server.connections[slot];
                    conn->fd = client_fd;
                    conn->last_activity = time(NULL);
                    conn->recv_size = 0;
                    conn->headers_complete = false;

                    // Store client IP address
                    strncpy(conn->client_ip, inet_ntoa(client_addr.sin_addr),
                            sizeof(conn->client_ip) - 1);
                    conn->client_ip[sizeof(conn->client_ip) - 1] = '\0';

                    atomic_fetch_add(&g_server.active_connections_atomic, 1);

                    LOG_DEBUG("Server", "New connection from %s:%d (slot %d)",
                             conn->client_ip,
                             ntohs(client_addr.sin_port), slot);
                } else {
                    LOG_WARN("Server", "Max connections reached, rejecting");
                    close(client_fd);
                }
            }
        }

        // Handle client connections
        int poll_idx = 1;
        for (int i = 0; i < g_server.max_connections && poll_idx < nfds; i++) {
            Connection* conn = &g_server.connections[i];
            if (conn->fd < 0) continue;

            struct pollfd* pfd = &g_server.poll_fds[poll_idx++];
            if (pfd->fd != conn->fd) continue;

            // Handle read
            if (pfd->revents & POLLIN) {
                // Ensure buffer has space
                if (conn->recv_size >= conn->recv_capacity - 1) {
                    size_t new_cap = conn->recv_capacity * 2;
                    if (new_cap > MAX_BODY_SIZE) {
                        LOG_WARN("Server", "Request too large");
                        connection_reset(conn);
                        atomic_fetch_sub(&g_server.active_connections_atomic, 1);
                        atomic_fetch_add(&g_server.requests_error_atomic, 1);
                        continue;
                    }
                    char* new_buf = realloc(conn->recv_buf, new_cap);
                    if (!new_buf) {
                        connection_reset(conn);
                        atomic_fetch_sub(&g_server.active_connections_atomic, 1);
                        continue;
                    }
                    conn->recv_buf = new_buf;
                    conn->recv_capacity = new_cap;
                }

                ssize_t n = read(conn->fd, conn->recv_buf + conn->recv_size,
                                conn->recv_capacity - conn->recv_size - 1);

                if (n > 0) {
                    conn->recv_size += n;
                    conn->recv_buf[conn->recv_size] = '\0';
                    conn->last_activity = time(NULL);

                    atomic_fetch_add(&g_server.bytes_received_atomic, n);

                    // Try to parse request
                    HttpRequest request;
                    int parse_result = parse_request(conn, &request);

                    if (parse_result == PARSE_TOO_LARGE) {
                        // Send 413 Payload Too Large response
                        HttpResponse response;
                        http_response_init(&response);
                        response.status = HTTP_413_PAYLOAD_TOO_LARGE;
                        strcpy(response.content_type, "application/json");
                        const char* error_body = "{\"success\":false,\"error\":\"Request body too large\"}";
                        response.body = strdup(error_body);
                        response.body_size = strlen(error_body);
                        response.owns_body = true;
                        build_response(conn, &response);
                        http_response_free(&response);
                        conn->recv_size = 0;
                        atomic_fetch_add(&g_server.requests_total_atomic, 1);
                        atomic_fetch_add(&g_server.requests_error_atomic, 1);
                        continue;
                    }

                    if (parse_result == PARSE_ERROR) {
                        // Send 400 Bad Request response
                        HttpResponse response;
                        http_response_init(&response);
                        response.status = HTTP_400_BAD_REQUEST;
                        strcpy(response.content_type, "application/json");
                        const char* error_body = "{\"success\":false,\"error\":\"Malformed request\"}";
                        response.body = strdup(error_body);
                        response.body_size = strlen(error_body);
                        response.owns_body = true;
                        build_response(conn, &response);
                        http_response_free(&response);
                        conn->recv_size = 0;
                        atomic_fetch_add(&g_server.requests_total_atomic, 1);
                        atomic_fetch_add(&g_server.requests_error_atomic, 1);
                        continue;
                    }

                    if (parse_result == PARSE_OK) {
                        // Record request start time for latency tracking
                        gettimeofday(&conn->request_start_time, NULL);

                        // Copy client IP to request for rate limiting/filtering
                        strncpy(request.client_ip, conn->client_ip,
                                sizeof(request.client_ip) - 1);
                        request.client_ip[sizeof(request.client_ip) - 1] = '\0';

                        // Request complete, handle it
                        LOG_DEBUG("Server", "%s %s from %s",
                                 request.method == HTTP_GET ? "GET" :
                                 request.method == HTTP_POST ? "POST" :
                                 request.method == HTTP_OPTIONS ? "OPTIONS" : "OTHER",
                                 request.path,
                                 request.client_ip);

                        // Check for WebSocket upgrade request
                        bool is_ws_upgrade = (
                            ws_is_enabled() &&
                            strcmp(request.path, "/ws") == 0 &&
                            request.method == HTTP_GET &&
                            strcasecmp(conn->upgrade_header, "websocket") == 0 &&
                            conn->ws_key[0] != '\0'
                        );

                        HttpResponse response;
                        http_response_init(&response);

                        if (is_ws_upgrade) {
                            // Handle WebSocket upgrade synchronously (must be on main thread)
                            // Put the ws_key in the query string so ws_handle_upgrade can access it
                            snprintf(request.query_string, sizeof(request.query_string),
                                     "key=%s", conn->ws_key);

                            WsConnection* ws_conn = ws_handle_upgrade(&request, &response,
                                                                       conn->fd, conn->client_ip);

                            if (ws_conn && response.status == HTTP_101_SWITCHING_PROTOCOLS) {
                                // Successfully upgraded - mark connection as WebSocket
                                // The ws_handle_upgrade already sent the upgrade response
                                // and took ownership of the fd. Don't close it.
                                conn->is_websocket = true;
                                conn->fd = -1;  // Transfer ownership to WebSocket
                                conn->recv_size = 0;

                                LOG_INFO("Server", "WebSocket upgrade successful from %s",
                                         conn->client_ip);

                                http_request_free(&request);
                                // Don't free response body - it's empty for 101

                                atomic_fetch_add(&g_server.requests_total_atomic, 1);
                                atomic_fetch_add(&g_server.requests_success_atomic, 1);
                                atomic_fetch_sub(&g_server.active_connections_atomic, 1);  // No longer HTTP

                                continue;  // Skip normal response handling
                            } else {
                                // Upgrade failed - send error response
                                LOG_WARN("Server", "WebSocket upgrade failed from %s",
                                         conn->client_ip);
                                // Fall through to build response
                                g_server.handler(&request, &response);
                            }

                            build_response(conn, &response);
                            http_response_free(&response);
                            http_request_free(&request);
                            conn->recv_size = 0;

                            atomic_fetch_add(&g_server.requests_total_atomic, 1);
                            if (response.status < 400) {
                                atomic_fetch_add(&g_server.requests_success_atomic, 1);
                            } else {
                                atomic_fetch_add(&g_server.requests_error_atomic, 1);
                            }

                        } else if (strncmp(request.path, "/video/frame/", 13) == 0 ||
                                   strncmp(request.path, "/video/stream/", 14) == 0) {
                            // Handle video streaming in a dedicated thread
                            // MJPEG streaming blocks until client disconnects
                            LOG_DEBUG("Server", "Video streaming request: %s from %s",
                                     request.path, conn->client_ip);

                            // Allocate args for the thread (thread will free)
                            VideoStreamArgs* args = malloc(sizeof(VideoStreamArgs));
                            if (args) {
                                args->fd = conn->fd;
                                strncpy(args->path, request.path, sizeof(args->path) - 1);
                                args->path[sizeof(args->path) - 1] = '\0';
                                strncpy(args->client_ip, conn->client_ip, sizeof(args->client_ip) - 1);
                                args->client_ip[sizeof(args->client_ip) - 1] = '\0';
                                strncpy(args->auth_header, request.authorization, sizeof(args->auth_header) - 1);
                                args->auth_header[sizeof(args->auth_header) - 1] = '\0';
                                strncpy(args->cookie_header, request.cookie, sizeof(args->cookie_header) - 1);
                                args->cookie_header[sizeof(args->cookie_header) - 1] = '\0';

                                pthread_t stream_thread;
                                pthread_attr_t attr;
                                pthread_attr_init(&attr);
                                pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

                                if (pthread_create(&stream_thread, &attr, video_stream_worker, args) == 0) {
                                    // Thread created - it owns the fd now
                                    conn->fd = -1;  // Prevent double-close
                                    conn->recv_size = 0;
                                    http_request_free(&request);

                                    atomic_fetch_add(&g_server.requests_total_atomic, 1);
                                    atomic_fetch_add(&g_server.requests_success_atomic, 1);
                                    atomic_fetch_sub(&g_server.active_connections_atomic, 1);

                                    pthread_attr_destroy(&attr);
                                    continue;  // Skip normal response handling
                                } else {
                                    // Thread creation failed
                                    LOG_ERROR("Server", "Failed to create video stream thread");
                                    free(args);
                                }
                                pthread_attr_destroy(&attr);
                            }

                            // Fall through to error response
                            http_response_init(&response);
                            response.status = HTTP_500_INTERNAL_ERROR;
                            const char* error_msg = "{\"error\":\"Failed to start video stream\"}";
                            response.body = strdup(error_msg);
                            response.body_size = strlen(error_msg);
                            response.owns_body = true;
                            strcpy(response.content_type, "application/json");
                            build_response(conn, &response);
                            http_response_free(&response);
                            http_request_free(&request);
                            conn->recv_size = 0;

                            atomic_fetch_add(&g_server.requests_total_atomic, 1);
                            atomic_fetch_add(&g_server.requests_error_atomic, 1);

                        } else if (g_server.use_thread_pool && g_server.thread_pool) {
                            // Dispatch to thread pool for concurrent processing
                            pthread_mutex_lock(&conn->mutex);

                            // Copy request to connection's pending request
                            memcpy(&conn->pending_request, &request, sizeof(HttpRequest));
                            conn->request_valid = true;
                            conn->recv_size = 0;

                            pthread_mutex_unlock(&conn->mutex);

                            // Transition to processing state
                            atomic_store(&conn->state, CONN_STATE_PROCESSING);

                            // Submit to thread pool
                            if (thread_pool_submit(g_server.thread_pool, request_worker, conn) < 0) {
                                // Thread pool queue full - process synchronously
                                LOG_WARN("Server", "Thread pool full, processing synchronously");
                                atomic_store(&conn->state, CONN_STATE_IDLE);

                                pthread_mutex_lock(&conn->mutex);
                                g_server.handler(&conn->pending_request, &response);
                                build_response(conn, &response);
                                http_response_free(&response);
                                http_request_free(&conn->pending_request);
                                conn->request_valid = false;
                                pthread_mutex_unlock(&conn->mutex);

                                atomic_fetch_add(&g_server.requests_total_atomic, 1);
                                if (response.status < 400) {
                                    atomic_fetch_add(&g_server.requests_success_atomic, 1);
                                } else {
                                    atomic_fetch_add(&g_server.requests_error_atomic, 1);
                                }
                            }
                            // Don't free request - ownership transferred to pending_request

                        } else {
                            // No thread pool - process synchronously (original behavior)
                            increment_concurrent_requests();
                            g_server.handler(&request, &response);
                            build_response(conn, &response);
                            http_response_free(&response);
                            http_request_free(&request);
                            conn->recv_size = 0;

                            // Record latency
                            struct timeval now;
                            gettimeofday(&now, NULL);
                            uint64_t latency_us = timeval_diff_us(&conn->request_start_time, &now);
                            record_latency(latency_us);

                            atomic_fetch_add(&g_server.requests_total_atomic, 1);
                            if (response.status < 400) {
                                atomic_fetch_add(&g_server.requests_success_atomic, 1);
                            } else {
                                atomic_fetch_add(&g_server.requests_error_atomic, 1);
                            }
                            decrement_concurrent_requests();
                        }
                    }
                } else if (n == 0) {
                    // Client closed connection
                    connection_reset(conn);
                    atomic_fetch_sub(&g_server.active_connections_atomic, 1);
                } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    connection_reset(conn);
                    atomic_fetch_sub(&g_server.active_connections_atomic, 1);
                }
            }

            // Handle write
            ConnState write_state = atomic_load(&conn->state);
            bool has_data = conn->send_size > conn->send_offset;

            if ((pfd->revents & POLLOUT) && has_data) {
                pthread_mutex_lock(&conn->mutex);
                ssize_t n = write(conn->fd, conn->send_buf + conn->send_offset,
                                 conn->send_size - conn->send_offset);
                if (n > 0) {
                    conn->send_offset += n;

                    atomic_fetch_add(&g_server.bytes_sent_atomic, n);

                    if (conn->send_offset >= conn->send_size) {
                        // Response sent - free buffer and reset state
                        free(conn->send_buf);
                        conn->send_buf = NULL;
                        conn->send_size = 0;
                        conn->send_offset = 0;

                        // Transition back to idle state if we were writing
                        if (write_state == CONN_STATE_WRITING) {
                            atomic_store(&conn->state, CONN_STATE_IDLE);
                        }
                    }
                } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    pthread_mutex_unlock(&conn->mutex);
                    connection_reset(conn);
                    atomic_fetch_sub(&g_server.active_connections_atomic, 1);
                    continue;
                }
                pthread_mutex_unlock(&conn->mutex);
            }

            // Handle errors
            if (pfd->revents & (POLLERR | POLLHUP | POLLNVAL)) {
                connection_reset(conn);
                atomic_fetch_sub(&g_server.active_connections_atomic, 1);
            }
        }
    }

    return 0;
}

void http_server_stop(void) {
    g_server.running = false;
}

bool http_server_is_running(void) {
    return g_server.running;
}

void http_server_get_stats(ServerStats* stats) {
    if (!stats) return;

    // All basic stats now use lock-free atomic reads
    stats->requests_total = atomic_load(&g_server.requests_total_atomic);
    stats->requests_success = atomic_load(&g_server.requests_success_atomic);
    stats->requests_error = atomic_load(&g_server.requests_error_atomic);
    stats->bytes_received = atomic_load(&g_server.bytes_received_atomic);
    stats->bytes_sent = atomic_load(&g_server.bytes_sent_atomic);
    stats->active_connections = atomic_load(&g_server.active_connections_atomic);
    stats->uptime_seconds = time(NULL) - g_server.start_time;

    // Concurrency metrics (atomic reads)
    stats->requests_concurrent_current = atomic_load(&g_server.concurrent_requests);
    stats->requests_concurrent_peak = atomic_load(&g_server.concurrent_peak);

    // Thread pool stats
    if (g_server.thread_pool) {
        ThreadPoolStats pool_stats;
        thread_pool_stats(g_server.thread_pool, &pool_stats);
        stats->thread_pool_num_threads = pool_stats.num_threads;
        stats->thread_pool_active_workers = pool_stats.active_threads;
        stats->thread_pool_pending_tasks = pool_stats.pending_tasks;
        stats->thread_pool_tasks_completed = pool_stats.completed_tasks;
        stats->thread_pool_tasks_submitted = pool_stats.completed_tasks + pool_stats.pending_tasks;
    } else {
        stats->thread_pool_num_threads = 0;
        stats->thread_pool_active_workers = 0;
        stats->thread_pool_pending_tasks = 0;
        stats->thread_pool_tasks_completed = 0;
        stats->thread_pool_tasks_submitted = 0;
    }

    // Latency metrics (atomic reads)
    stats->latency_total_us = atomic_load(&g_server.latency_total_us);
    stats->latency_count = atomic_load(&g_server.latency_count);
    stats->latency_min_us = atomic_load(&g_server.latency_min_us);
    stats->latency_max_us = atomic_load(&g_server.latency_max_us);

    // If no samples yet, set min to 0
    if (stats->latency_count == 0) {
        stats->latency_min_us = 0;
    }

    // Calculate rate metrics - uses mutex for rate calculation state only
    pthread_mutex_lock(&g_server.stats_mutex);

    struct timeval now;
    gettimeofday(&now, NULL);
    double elapsed_sec = (now.tv_sec - g_server.last_rate_check.tv_sec) +
                         (now.tv_usec - g_server.last_rate_check.tv_usec) / 1000000.0;

    if (elapsed_sec >= 1.0) {  // Update rates every second
        uint64_t req_delta = stats->requests_total - g_server.last_requests_count;
        uint64_t bytes_in_delta = stats->bytes_received - g_server.last_bytes_in;
        uint64_t bytes_out_delta = stats->bytes_sent - g_server.last_bytes_out;

        g_server.current_rps = req_delta / elapsed_sec;
        g_server.current_bps_in = bytes_in_delta / elapsed_sec;
        g_server.current_bps_out = bytes_out_delta / elapsed_sec;

        g_server.last_rate_check = now;
        g_server.last_requests_count = stats->requests_total;
        g_server.last_bytes_in = stats->bytes_received;
        g_server.last_bytes_out = stats->bytes_sent;
    }

    stats->requests_per_second = g_server.current_rps;
    stats->bytes_per_second_in = g_server.current_bps_in;
    stats->bytes_per_second_out = g_server.current_bps_out;

    pthread_mutex_unlock(&g_server.stats_mutex);
}

void http_server_shutdown(void) {
    http_server_stop();

    // Stop accepting new connections immediately
    if (g_server.listen_fd >= 0) {
        close(g_server.listen_fd);
        g_server.listen_fd = -1;
        LOG_INFO("Server", "Stopped accepting new connections");
    }

    // Graceful shutdown: wait for active connections to complete
    if (g_server.config.graceful_shutdown && g_server.connections) {
        int timeout_sec = g_server.config.shutdown_timeout_sec > 0 ?
                          g_server.config.shutdown_timeout_sec : 30;
        time_t deadline = time(NULL) + timeout_sec;

        LOG_INFO("Server", "Graceful shutdown: draining connections (timeout: %ds)...", timeout_sec);

        while (time(NULL) < deadline) {
            int active_count = 0;
            int processing_count = 0;

            for (int i = 0; i < g_server.max_connections; i++) {
                Connection* conn = &g_server.connections[i];
                if (conn->fd >= 0) {
                    active_count++;
                    ConnState state = atomic_load(&conn->state);
                    if (state == CONN_STATE_PROCESSING || state == CONN_STATE_WRITING) {
                        processing_count++;
                    }
                }
            }

            // Also check thread pool for pending work
            int pending_tasks = 0;
            if (g_server.thread_pool) {
                pending_tasks = thread_pool_pending(g_server.thread_pool) +
                               thread_pool_active(g_server.thread_pool);
            }

            if (active_count == 0 && pending_tasks == 0) {
                LOG_INFO("Server", "All connections drained");
                break;
            }

            LOG_DEBUG("Server", "Draining: %d active connections, %d processing, %d pending tasks",
                     active_count, processing_count, pending_tasks);

            // Process remaining writes
            for (int i = 0; i < g_server.max_connections; i++) {
                Connection* conn = &g_server.connections[i];
                if (conn->fd >= 0 && conn->send_size > conn->send_offset) {
                    pthread_mutex_lock(&conn->mutex);
                    ssize_t n = write(conn->fd, conn->send_buf + conn->send_offset,
                                     conn->send_size - conn->send_offset);
                    if (n > 0) {
                        conn->send_offset += n;
                        if (conn->send_offset >= conn->send_size) {
                            free(conn->send_buf);
                            conn->send_buf = NULL;
                            conn->send_size = 0;
                            conn->send_offset = 0;
                        }
                    }
                    pthread_mutex_unlock(&conn->mutex);
                }
            }

            usleep(100000); // 100ms
        }

        if (time(NULL) >= deadline) {
            LOG_WARN("Server", "Graceful shutdown timeout - forcing close");
        }
    }

    // Destroy thread pool - wait for pending work to complete
    if (g_server.thread_pool) {
        LOG_INFO("Server", "Waiting for thread pool to finish...");
        thread_pool_destroy(g_server.thread_pool);
        g_server.thread_pool = NULL;
        LOG_INFO("Server", "Thread pool destroyed");
    }

    if (g_server.connections) {
        for (int i = 0; i < g_server.max_connections; i++) {
            connection_free(&g_server.connections[i]);
        }
        free(g_server.connections);
        g_server.connections = NULL;
    }

    free(g_server.poll_fds);
    g_server.poll_fds = NULL;

    pthread_mutex_destroy(&g_server.stats_mutex);
}
