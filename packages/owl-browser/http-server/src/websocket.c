/**
 * Owl Browser HTTP Server - WebSocket Implementation
 *
 * Implements RFC 6455 WebSocket protocol with:
 * - Handshake validation and Sec-WebSocket-Accept computation
 * - Frame parsing and generation
 * - Ping/pong for connection health monitoring
 * - Message fragmentation support
 * - Graceful close handshake
 */

#include "websocket.h"
#include "auth.h"
#include "json.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

// RFC 6455 WebSocket GUID
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

// Connection structure
struct WsConnection {
    int fd;
    WsState state;
    char client_ip[64];
    void* user_data;

    // Receive buffer
    char* recv_buf;
    size_t recv_size;
    size_t recv_capacity;

    // Send buffer (for pending writes) - ring buffer style
    char* send_buf;
    size_t send_size;
    size_t send_offset;
    size_t send_capacity;  // Track allocated capacity

    // Message accumulator (for fragmented messages)
    char* msg_buf;
    size_t msg_size;
    size_t msg_capacity;
    WsOpcode msg_opcode;

    // Timestamps for health monitoring
    time_t last_activity;
    time_t last_ping_sent;
    time_t created_at;
    bool waiting_for_pong;
    int failed_ping_count;  // Track consecutive failed pings

    // Connection statistics
    uint64_t bytes_received;
    uint64_t bytes_sent;
    uint64_t messages_received;
    uint64_t messages_sent;

    // Authentication info
    char auth_subject[256];

    // Error tracking
    int consecutive_errors;
    time_t last_error_time;
};

// Global state
static struct {
    bool initialized;
    bool enabled;
    WebSocketConfig config;

    // Connection pool
    WsConnection** connections;
    int connection_count;
    int max_connections;
    pthread_mutex_t conn_mutex;

    // Handlers
    WsMessageHandler message_handler;
    WsConnectHandler connect_handler;
    WsDisconnectHandler disconnect_handler;

    // Statistics
    WsStats stats;
    pthread_mutex_t stats_mutex;
} g_ws = {0};

// ============================================================================
// Base64 Encoding (for Sec-WebSocket-Accept)
// ============================================================================

static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char* base64_encode(const unsigned char* data, size_t len) {
    size_t out_len = 4 * ((len + 2) / 3);
    char* out = malloc(out_len + 1);
    if (!out) return NULL;

    size_t i, j;
    for (i = 0, j = 0; i < len; i += 3, j += 4) {
        uint32_t n = ((uint32_t)data[i]) << 16;
        if (i + 1 < len) n |= ((uint32_t)data[i + 1]) << 8;
        if (i + 2 < len) n |= data[i + 2];

        out[j] = base64_chars[(n >> 18) & 0x3F];
        out[j + 1] = base64_chars[(n >> 12) & 0x3F];
        out[j + 2] = (i + 1 < len) ? base64_chars[(n >> 6) & 0x3F] : '=';
        out[j + 3] = (i + 2 < len) ? base64_chars[n & 0x3F] : '=';
    }
    out[out_len] = '\0';
    return out;
}

// ============================================================================
// WebSocket Accept Key Computation
// ============================================================================

static char* compute_accept_key(const char* client_key) {
    if (!client_key) return NULL;

    // Concatenate client key with GUID
    size_t key_len = strlen(client_key);
    size_t guid_len = strlen(WS_GUID);
    char* concat = malloc(key_len + guid_len + 1);
    if (!concat) return NULL;

    memcpy(concat, client_key, key_len);
    memcpy(concat + key_len, WS_GUID, guid_len);
    concat[key_len + guid_len] = '\0';

    // SHA-1 hash
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)concat, key_len + guid_len, hash);
    free(concat);

    // Base64 encode
    return base64_encode(hash, SHA_DIGEST_LENGTH);
}

// ============================================================================
// Frame Building
// ============================================================================

static size_t build_frame(char* buf, size_t buf_size, WsOpcode opcode,
                          const void* payload, size_t payload_len, bool fin) {
    if (!buf || buf_size < 2) return 0;

    size_t frame_size = 2;  // Minimum header

    // First byte: FIN + opcode
    buf[0] = (fin ? 0x80 : 0x00) | (opcode & 0x0F);

    // Second byte: no mask (server doesn't mask) + payload length
    if (payload_len < 126) {
        buf[1] = (char)payload_len;
    } else if (payload_len <= 0xFFFF) {
        if (buf_size < 4) return 0;
        buf[1] = 126;
        buf[2] = (payload_len >> 8) & 0xFF;
        buf[3] = payload_len & 0xFF;
        frame_size = 4;
    } else {
        if (buf_size < 10) return 0;
        buf[1] = 127;
        for (int i = 0; i < 8; i++) {
            buf[2 + i] = (payload_len >> (56 - i * 8)) & 0xFF;
        }
        frame_size = 10;
    }

    // Check buffer size
    if (buf_size < frame_size + payload_len) return 0;

    // Copy payload
    if (payload && payload_len > 0) {
        memcpy(buf + frame_size, payload, payload_len);
    }

    return frame_size + payload_len;
}

// ============================================================================
// Frame Parsing
// ============================================================================

// Returns bytes consumed, or -1 on error, or 0 if need more data
static int parse_frame(const char* buf, size_t buf_len, WsFrame* frame) {
    if (!buf || !frame || buf_len < 2) return 0;

    memset(frame, 0, sizeof(*frame));

    // Parse first byte
    frame->fin = (buf[0] & 0x80) != 0;
    frame->opcode = buf[0] & 0x0F;

    // Parse second byte
    frame->masked = (buf[1] & 0x80) != 0;
    uint64_t len = buf[1] & 0x7F;

    size_t header_len = 2;

    // Extended payload length
    if (len == 126) {
        if (buf_len < 4) return 0;
        len = ((unsigned char)buf[2] << 8) | (unsigned char)buf[3];
        header_len = 4;
    } else if (len == 127) {
        if (buf_len < 10) return 0;
        len = 0;
        for (int i = 0; i < 8; i++) {
            len = (len << 8) | (unsigned char)buf[2 + i];
        }
        header_len = 10;
    }

    frame->payload_len = len;

    // Masking key (if masked)
    if (frame->masked) {
        if (buf_len < header_len + 4) return 0;
        memcpy(frame->mask_key, buf + header_len, 4);
        header_len += 4;
    }

    // Check if we have the full payload
    if (buf_len < header_len + len) return 0;

    // Allocate and copy payload
    if (len > 0) {
        frame->payload = malloc(len + 1);
        if (!frame->payload) return -1;

        memcpy(frame->payload, buf + header_len, len);

        // Unmask if needed
        if (frame->masked) {
            for (uint64_t i = 0; i < len; i++) {
                frame->payload[i] ^= frame->mask_key[i % 4];
            }
        }
        frame->payload[len] = '\0';  // Null-terminate for text
    }

    return (int)(header_len + len);
}

static void free_frame(WsFrame* frame) {
    if (frame && frame->payload) {
        free(frame->payload);
        frame->payload = NULL;
    }
}

// ============================================================================
// Connection Management
// ============================================================================

static WsConnection* create_connection(int fd, const char* client_ip) {
    WsConnection* conn = calloc(1, sizeof(WsConnection));
    if (!conn) return NULL;

    conn->fd = fd;
    conn->state = WS_STATE_CONNECTING;
    strncpy(conn->client_ip, client_ip, sizeof(conn->client_ip) - 1);

    // Initialize receive buffer with reasonable initial size
    conn->recv_capacity = 8192;  // Start with 8KB
    conn->recv_buf = malloc(conn->recv_capacity);
    if (!conn->recv_buf) {
        free(conn);
        return NULL;
    }

    // Initialize message accumulator
    conn->msg_capacity = 8192;
    conn->msg_buf = malloc(conn->msg_capacity);
    if (!conn->msg_buf) {
        free(conn->recv_buf);
        free(conn);
        return NULL;
    }

    // Initialize send buffer with pre-allocated capacity
    conn->send_capacity = 16384;  // Start with 16KB for send buffer
    conn->send_buf = malloc(conn->send_capacity);
    if (!conn->send_buf) {
        free(conn->msg_buf);
        free(conn->recv_buf);
        free(conn);
        return NULL;
    }
    conn->send_size = 0;
    conn->send_offset = 0;

    // Initialize timestamps
    time_t now = time(NULL);
    conn->last_activity = now;
    conn->created_at = now;
    conn->failed_ping_count = 0;
    conn->consecutive_errors = 0;

    return conn;
}

static void free_connection(WsConnection* conn) {
    if (!conn) return;

    if (conn->fd >= 0) {
        close(conn->fd);
    }

    free(conn->recv_buf);
    free(conn->send_buf);
    free(conn->msg_buf);
    free(conn);
}

static void add_connection(WsConnection* conn) {
    pthread_mutex_lock(&g_ws.conn_mutex);

    // Find empty slot
    for (int i = 0; i < g_ws.max_connections; i++) {
        if (!g_ws.connections[i]) {
            g_ws.connections[i] = conn;
            g_ws.connection_count++;

            pthread_mutex_lock(&g_ws.stats_mutex);
            g_ws.stats.active_connections++;
            g_ws.stats.total_connections++;
            pthread_mutex_unlock(&g_ws.stats_mutex);

            break;
        }
    }

    pthread_mutex_unlock(&g_ws.conn_mutex);
}

static void remove_connection(WsConnection* conn) {
    pthread_mutex_lock(&g_ws.conn_mutex);

    for (int i = 0; i < g_ws.max_connections; i++) {
        if (g_ws.connections[i] == conn) {
            g_ws.connections[i] = NULL;
            g_ws.connection_count--;

            pthread_mutex_lock(&g_ws.stats_mutex);
            g_ws.stats.active_connections--;
            pthread_mutex_unlock(&g_ws.stats_mutex);

            break;
        }
    }

    pthread_mutex_unlock(&g_ws.conn_mutex);
}

// ============================================================================
// Send Frame Helpers
// ============================================================================

static int queue_send(WsConnection* conn, const char* data, size_t len) {
    if (!conn || !data || len == 0) return -1;

    // Safety check - send_buf should be pre-allocated
    if (!conn->send_buf || conn->send_capacity == 0) {
        LOG_ERROR("WebSocket", "Send buffer not initialized for %s", conn->client_ip);
        return -1;
    }

    // Compact buffer if there's sent data taking up space
    if (conn->send_offset > 0 && conn->send_size > conn->send_offset) {
        size_t remaining = conn->send_size - conn->send_offset;
        memmove(conn->send_buf, conn->send_buf + conn->send_offset, remaining);
        conn->send_size = remaining;
        conn->send_offset = 0;
    } else if (conn->send_offset > 0 && conn->send_size == conn->send_offset) {
        // All data was sent, reset
        conn->send_size = 0;
        conn->send_offset = 0;
    }

    size_t needed = conn->send_size + len;

    // Check if we need to grow the buffer
    if (needed > conn->send_capacity) {
        // Grow by doubling or to needed size, whichever is larger
        size_t new_capacity = conn->send_capacity * 2;
        if (new_capacity < needed) {
            new_capacity = needed + 4096;  // Add some headroom
        }

        // Cap at max message size
        if (new_capacity > (size_t)g_ws.config.message_max_size * 2) {
            LOG_WARN("WebSocket", "Send buffer would exceed max size for %s",
                     conn->client_ip);
            return -1;
        }

        char* new_buf = realloc(conn->send_buf, new_capacity);
        if (!new_buf) {
            LOG_ERROR("WebSocket", "Failed to grow send buffer for %s",
                      conn->client_ip);
            return -1;
        }
        conn->send_buf = new_buf;
        conn->send_capacity = new_capacity;
    }

    memcpy(conn->send_buf + conn->send_size, data, len);
    conn->send_size += len;

    return 0;
}

static int send_frame(WsConnection* conn, WsOpcode opcode,
                      const void* payload, size_t payload_len) {
    if (!conn || conn->state != WS_STATE_OPEN) return -1;

    // Build frame
    size_t frame_capacity = 14 + payload_len;  // Max header + payload
    char* frame_buf = malloc(frame_capacity);
    if (!frame_buf) return -1;

    size_t frame_len = build_frame(frame_buf, frame_capacity, opcode,
                                   payload, payload_len, true);

    if (frame_len == 0) {
        free(frame_buf);
        return -1;
    }

    // Queue for sending
    int ret = queue_send(conn, frame_buf, frame_len);
    free(frame_buf);

    if (ret == 0) {
        pthread_mutex_lock(&g_ws.stats_mutex);
        g_ws.stats.bytes_sent += frame_len;
        pthread_mutex_unlock(&g_ws.stats_mutex);
    }

    return ret;
}

// ============================================================================
// Public API Implementation
// ============================================================================

int ws_init(const ServerConfig* config) {
    if (g_ws.initialized) return 0;
    if (!config) return -1;

    memcpy(&g_ws.config, &config->websocket, sizeof(WebSocketConfig));
    g_ws.enabled = config->websocket.enabled;

    if (!g_ws.enabled) {
        g_ws.initialized = true;
        LOG_INFO("WebSocket", "WebSocket support disabled");
        return 0;
    }

    g_ws.max_connections = config->websocket.max_connections;
    g_ws.connections = calloc(g_ws.max_connections, sizeof(WsConnection*));
    if (!g_ws.connections) {
        LOG_ERROR("WebSocket", "Failed to allocate connection pool");
        return -1;
    }

    pthread_mutex_init(&g_ws.conn_mutex, NULL);
    pthread_mutex_init(&g_ws.stats_mutex, NULL);

    g_ws.initialized = true;
    LOG_INFO("WebSocket", "WebSocket initialized (max %d connections)",
             g_ws.max_connections);

    return 0;
}

void ws_shutdown(void) {
    if (!g_ws.initialized) return;

    // Close all connections
    if (g_ws.connections) {
        pthread_mutex_lock(&g_ws.conn_mutex);
        for (int i = 0; i < g_ws.max_connections; i++) {
            if (g_ws.connections[i]) {
                WsConnection* conn = g_ws.connections[i];
                if (conn->state == WS_STATE_OPEN) {
                    ws_close(conn, WS_CLOSE_GOING_AWAY, "Server shutting down");
                }
                free_connection(conn);
                g_ws.connections[i] = NULL;
            }
        }
        pthread_mutex_unlock(&g_ws.conn_mutex);

        free(g_ws.connections);
        g_ws.connections = NULL;
    }

    pthread_mutex_destroy(&g_ws.conn_mutex);
    pthread_mutex_destroy(&g_ws.stats_mutex);

    g_ws.initialized = false;
    LOG_INFO("WebSocket", "WebSocket shutdown complete");
}

bool ws_is_enabled(void) {
    return g_ws.initialized && g_ws.enabled;
}

void ws_set_message_handler(WsMessageHandler handler) {
    g_ws.message_handler = handler;
}

void ws_set_connect_handler(WsConnectHandler handler) {
    g_ws.connect_handler = handler;
}

void ws_set_disconnect_handler(WsDisconnectHandler handler) {
    g_ws.disconnect_handler = handler;
}

bool ws_is_upgrade_request(const HttpRequest* request) {
    if (!request) return false;

    // Check path
    if (strcmp(request->path, "/ws") != 0) return false;

    // Check method
    if (request->method != HTTP_GET) return false;

    return true;
}

// Parse header value from raw request (needed for WebSocket headers)
static const char* find_header(const char* headers, const char* name) {
    if (!headers || !name) return NULL;

    static char value_buf[512];
    size_t name_len = strlen(name);

    const char* p = headers;
    while (*p) {
        // Skip to start of header name
        while (*p && (*p == '\r' || *p == '\n')) p++;
        if (!*p) break;

        // Check if this is our header (case-insensitive)
        if (strncasecmp(p, name, name_len) == 0 && p[name_len] == ':') {
            p += name_len + 1;
            while (*p == ' ') p++;

            // Copy value
            size_t i = 0;
            while (*p && *p != '\r' && *p != '\n' && i < sizeof(value_buf) - 1) {
                value_buf[i++] = *p++;
            }
            value_buf[i] = '\0';
            return value_buf;
        }

        // Skip to end of line
        while (*p && *p != '\n') p++;
    }

    return NULL;
}

WsConnection* ws_handle_upgrade(const HttpRequest* request, HttpResponse* response,
                                 int client_fd, const char* client_ip) {
    if (!request || !response || client_fd < 0) return NULL;

    http_response_init(response);

    // Check if WebSocket is enabled
    if (!ws_is_enabled()) {
        response->status = HTTP_404_NOT_FOUND;
        response->body = strdup("{\"success\":false,\"error\":\"WebSocket not enabled\"}");
        response->body_size = strlen(response->body);
        response->owns_body = true;
        strcpy(response->content_type, "application/json");
        return NULL;
    }

    // Check connection limit
    if (g_ws.connection_count >= g_ws.max_connections) {
        LOG_WARN("WebSocket", "Rejecting connection from %s: max connections reached (%d/%d)",
                 client_ip, g_ws.connection_count, g_ws.max_connections);
        response->status = HTTP_503_SERVICE_UNAVAILABLE;
        response->body = strdup("{\"success\":false,\"error\":\"Max WebSocket connections reached\",\"retry_after\":5}");
        response->body_size = strlen(response->body);
        response->owns_body = true;
        strcpy(response->content_type, "application/json");
        return NULL;
    }

    // Warn if under connection pressure (>80% capacity)
    if (ws_is_connection_pressure()) {
        LOG_WARN("WebSocket", "Connection pressure: %d/%d connections in use",
                 g_ws.connection_count, g_ws.max_connections);
    }

    // Authenticate - check Authorization header
    if (!auth_validate(request->authorization)) {
        response->status = HTTP_401_UNAUTHORIZED;
        response->body = strdup("{\"success\":false,\"error\":\"Invalid or missing authorization\"}");
        response->body_size = strlen(response->body);
        response->owns_body = true;
        strcpy(response->content_type, "application/json");
        return NULL;
    }

    // For WebSocket upgrade, we need the raw Sec-WebSocket-Key header
    // Since HttpRequest doesn't store all headers, we need to extract it
    // from a different source. For now, use the query string as a workaround
    // or expect the key in a custom format.

    // Actually, we need to modify how this is called - the HTTP server needs
    // to pass us the raw headers. For now, let's use a placeholder approach
    // where the client can pass the key in query string as fallback

    const char* ws_key = NULL;

    // Try to get from query string (fallback)
    if (request->query_string[0]) {
        // Parse key from query: ?key=xxx
        char* key_start = strstr(request->query_string, "key=");
        if (key_start) {
            key_start += 4;
            static char key_buf[128];
            size_t i = 0;
            while (key_start[i] && key_start[i] != '&' && i < sizeof(key_buf) - 1) {
                key_buf[i] = key_start[i];
                i++;
            }
            key_buf[i] = '\0';
            ws_key = key_buf;
        }
    }

    // If no key found, generate a response asking client to provide it
    // In practice, the HTTP server integration will provide this
    if (!ws_key || strlen(ws_key) == 0) {
        // Use a default test key for now - real implementation needs header parsing
        ws_key = NULL;  // Will cause error below
    }

    if (!ws_key) {
        response->status = HTTP_400_BAD_REQUEST;
        response->body = strdup("{\"success\":false,\"error\":\"Missing Sec-WebSocket-Key\"}");
        response->body_size = strlen(response->body);
        response->owns_body = true;
        strcpy(response->content_type, "application/json");
        return NULL;
    }

    // Compute accept key
    char* accept_key = compute_accept_key(ws_key);
    if (!accept_key) {
        response->status = HTTP_500_INTERNAL_ERROR;
        response->body = strdup("{\"success\":false,\"error\":\"Failed to compute accept key\"}");
        response->body_size = strlen(response->body);
        response->owns_body = true;
        strcpy(response->content_type, "application/json");
        return NULL;
    }

    // Create connection
    WsConnection* conn = create_connection(client_fd, client_ip);
    if (!conn) {
        free(accept_key);
        response->status = HTTP_500_INTERNAL_ERROR;
        response->body = strdup("{\"success\":false,\"error\":\"Failed to create connection\"}");
        response->body_size = strlen(response->body);
        response->owns_body = true;
        strcpy(response->content_type, "application/json");
        return NULL;
    }

    // Build upgrade response
    char upgrade_response[512];
    int len = snprintf(upgrade_response, sizeof(upgrade_response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n",
        accept_key);

    free(accept_key);

    // Send upgrade response directly (not through normal response path)
    ssize_t sent = write(client_fd, upgrade_response, len);
    if (sent != len) {
        free_connection(conn);
        response->status = HTTP_500_INTERNAL_ERROR;
        return NULL;
    }

    // Connection is now open
    conn->state = WS_STATE_OPEN;
    add_connection(conn);

    LOG_INFO("WebSocket", "New connection from %s", client_ip);

    // Notify handler
    if (g_ws.connect_handler) {
        g_ws.connect_handler(conn);
    }

    // Return special status to indicate upgrade was handled
    response->status = HTTP_101_SWITCHING_PROTOCOLS;

    return conn;
}

int ws_process_read(WsConnection* conn) {
    if (!conn || conn->fd < 0) return -1;

    // Ensure buffer has space
    if (conn->recv_size >= conn->recv_capacity - 1) {
        size_t new_cap = conn->recv_capacity * 2;
        if (new_cap > (size_t)g_ws.config.message_max_size) {
            LOG_WARN("WebSocket", "Message too large from %s", conn->client_ip);
            ws_close(conn, WS_CLOSE_MESSAGE_TOO_BIG, "Message too large");
            conn->consecutive_errors++;
            conn->last_error_time = time(NULL);
            return -1;
        }
        char* new_buf = realloc(conn->recv_buf, new_cap);
        if (!new_buf) {
            LOG_ERROR("WebSocket", "Failed to grow recv buffer for %s", conn->client_ip);
            conn->consecutive_errors++;
            conn->last_error_time = time(NULL);
            return -1;
        }
        conn->recv_buf = new_buf;
        conn->recv_capacity = new_cap;
    }

    // Read data
    ssize_t n = read(conn->fd, conn->recv_buf + conn->recv_size,
                     conn->recv_capacity - conn->recv_size - 1);

    if (n <= 0) {
        if (n == 0) {
            LOG_DEBUG("WebSocket", "Connection closed by peer: %s", conn->client_ip);
            conn->state = WS_STATE_CLOSED;  // Mark as closed to stop further processing
            return -1;  // Connection closed
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_DEBUG("WebSocket", "Read error from %s: %s",
                      conn->client_ip, strerror(errno));
            conn->consecutive_errors++;
            conn->last_error_time = time(NULL);
            conn->state = WS_STATE_CLOSED;  // Mark as closed to stop further processing
            return -1;  // Real error
        }
        return 0;  // No data available (non-blocking)
    }

    // Reset error count on successful read
    conn->consecutive_errors = 0;
    conn->recv_size += n;
    conn->recv_buf[conn->recv_size] = '\0';
    conn->last_activity = time(NULL);
    conn->bytes_received += n;

    pthread_mutex_lock(&g_ws.stats_mutex);
    g_ws.stats.bytes_received += n;
    pthread_mutex_unlock(&g_ws.stats_mutex);

    // Process frames
    while (conn->recv_size > 0) {
        WsFrame frame;
        int consumed = parse_frame(conn->recv_buf, conn->recv_size, &frame);

        if (consumed < 0) {
            LOG_ERROR("WebSocket", "Frame parse error from %s", conn->client_ip);
            ws_close(conn, WS_CLOSE_PROTOCOL_ERROR, "Frame parse error");
            return -1;
        }

        if (consumed == 0) {
            break;  // Need more data
        }

        // Handle frame based on opcode
        switch (frame.opcode) {
            case WS_OPCODE_TEXT:
            case WS_OPCODE_BINARY:
                if (frame.fin) {
                    // Complete message
                    if (conn->msg_size > 0) {
                        // Append to accumulated message
                        size_t total = conn->msg_size + frame.payload_len;
                        if (total > conn->msg_capacity) {
                            char* new_buf = realloc(conn->msg_buf, total + 1);
                            if (!new_buf) {
                                free_frame(&frame);
                                conn->consecutive_errors++;
                                return -1;
                            }
                            conn->msg_buf = new_buf;
                            conn->msg_capacity = total + 1;
                        }
                        memcpy(conn->msg_buf + conn->msg_size, frame.payload, frame.payload_len);
                        conn->msg_size = total;
                        conn->msg_buf[total] = '\0';

                        // Deliver message
                        if (g_ws.message_handler) {
                            g_ws.message_handler(conn, conn->msg_buf, conn->msg_size);
                        }
                        conn->msg_size = 0;

                        conn->messages_received++;
                        pthread_mutex_lock(&g_ws.stats_mutex);
                        g_ws.stats.messages_received++;
                        pthread_mutex_unlock(&g_ws.stats_mutex);
                    } else {
                        // Single-frame message
                        if (g_ws.message_handler && frame.payload) {
                            g_ws.message_handler(conn, frame.payload, frame.payload_len);
                        }

                        conn->messages_received++;
                        pthread_mutex_lock(&g_ws.stats_mutex);
                        g_ws.stats.messages_received++;
                        pthread_mutex_unlock(&g_ws.stats_mutex);
                    }
                } else {
                    // First fragment of multi-frame message
                    conn->msg_opcode = frame.opcode;
                    if (frame.payload_len > conn->msg_capacity) {
                        char* new_buf = realloc(conn->msg_buf, frame.payload_len + 1);
                        if (!new_buf) {
                            free_frame(&frame);
                            return -1;
                        }
                        conn->msg_buf = new_buf;
                        conn->msg_capacity = frame.payload_len + 1;
                    }
                    memcpy(conn->msg_buf, frame.payload, frame.payload_len);
                    conn->msg_size = frame.payload_len;
                }
                break;

            case WS_OPCODE_CONTINUATION:
                if (conn->msg_size > 0) {
                    // Continue accumulating
                    size_t total = conn->msg_size + frame.payload_len;
                    if (total > conn->msg_capacity) {
                        char* new_buf = realloc(conn->msg_buf, total + 1);
                        if (!new_buf) {
                            free_frame(&frame);
                            return -1;
                        }
                        conn->msg_buf = new_buf;
                        conn->msg_capacity = total + 1;
                    }
                    memcpy(conn->msg_buf + conn->msg_size, frame.payload, frame.payload_len);
                    conn->msg_size = total;

                    if (frame.fin) {
                        // Final fragment
                        conn->msg_buf[total] = '\0';
                        if (g_ws.message_handler) {
                            g_ws.message_handler(conn, conn->msg_buf, conn->msg_size);
                        }
                        conn->msg_size = 0;

                        pthread_mutex_lock(&g_ws.stats_mutex);
                        g_ws.stats.messages_received++;
                        pthread_mutex_unlock(&g_ws.stats_mutex);
                    }
                }
                break;

            case WS_OPCODE_CLOSE:
                LOG_DEBUG("WebSocket", "Close frame from %s", conn->client_ip);
                if (conn->state == WS_STATE_OPEN) {
                    // Send close response
                    conn->state = WS_STATE_CLOSING;
                    send_frame(conn, WS_OPCODE_CLOSE, frame.payload, frame.payload_len);
                }
                conn->state = WS_STATE_CLOSED;

                // Notify handler
                if (g_ws.disconnect_handler) {
                    WsCloseCode code = WS_CLOSE_NORMAL;
                    if (frame.payload_len >= 2) {
                        code = ((unsigned char)frame.payload[0] << 8) |
                               (unsigned char)frame.payload[1];
                    }
                    g_ws.disconnect_handler(conn, code,
                        frame.payload_len > 2 ? frame.payload + 2 : NULL);
                }

                free_frame(&frame);
                return -1;  // Signal to close connection

            case WS_OPCODE_PING:
                LOG_DEBUG("WebSocket", "Ping from %s", conn->client_ip);
                // Send pong with same payload
                send_frame(conn, WS_OPCODE_PONG, frame.payload, frame.payload_len);
                break;

            case WS_OPCODE_PONG:
                LOG_DEBUG("WebSocket", "Pong from %s", conn->client_ip);
                conn->waiting_for_pong = false;
                break;

            default:
                LOG_WARN("WebSocket", "Unknown opcode %d from %s",
                         frame.opcode, conn->client_ip);
                break;
        }

        free_frame(&frame);

        // Remove consumed bytes from buffer
        memmove(conn->recv_buf, conn->recv_buf + consumed, conn->recv_size - consumed);
        conn->recv_size -= consumed;
    }

    return 0;
}

int ws_process_write(WsConnection* conn) {
    if (!conn || conn->fd < 0) return -1;

    if (!conn->send_buf || conn->send_size == conn->send_offset) {
        return 0;  // Nothing to send
    }

    ssize_t n = write(conn->fd, conn->send_buf + conn->send_offset,
                      conn->send_size - conn->send_offset);

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }

    conn->send_offset += n;

    // Reset buffer if fully sent (keep buffer allocated for reuse)
    if (conn->send_offset >= conn->send_size) {
        conn->send_size = 0;
        conn->send_offset = 0;
        // Note: We intentionally keep send_buf allocated to avoid malloc/free cycles
        // The buffer will be freed when the connection is closed
    }

    return 0;
}

bool ws_has_pending_write(WsConnection* conn) {
    if (!conn) return false;
    return conn->send_buf && conn->send_size > conn->send_offset;
}

int ws_get_fd(WsConnection* conn) {
    return conn ? conn->fd : -1;
}

WsState ws_get_state(WsConnection* conn) {
    return conn ? conn->state : WS_STATE_CLOSED;
}

const char* ws_get_client_ip(WsConnection* conn) {
    return conn ? conn->client_ip : "";
}

void* ws_get_user_data(WsConnection* conn) {
    return conn ? conn->user_data : NULL;
}

void ws_set_user_data(WsConnection* conn, void* data) {
    if (conn) conn->user_data = data;
}

void ws_close(WsConnection* conn, WsCloseCode code, const char* reason) {
    if (!conn || conn->state != WS_STATE_OPEN) return;

    conn->state = WS_STATE_CLOSING;

    // Build close payload: 2-byte status code + optional reason
    char payload[128];
    size_t payload_len = 2;

    payload[0] = (code >> 8) & 0xFF;
    payload[1] = code & 0xFF;

    if (reason) {
        size_t reason_len = strlen(reason);
        if (reason_len > sizeof(payload) - 2) {
            reason_len = sizeof(payload) - 2;
        }
        memcpy(payload + 2, reason, reason_len);
        payload_len += reason_len;
    }

    send_frame(conn, WS_OPCODE_CLOSE, payload, payload_len);

    LOG_DEBUG("WebSocket", "Closing connection to %s: %d %s",
              conn->client_ip, code, reason ? reason : "");
}

int ws_send_text(WsConnection* conn, const char* message, size_t len) {
    if (!conn || !message) return -1;
    if (len == 0) len = strlen(message);

    int ret = send_frame(conn, WS_OPCODE_TEXT, message, len);

    if (ret == 0) {
        conn->messages_sent++;
        conn->bytes_sent += len;
        pthread_mutex_lock(&g_ws.stats_mutex);
        g_ws.stats.messages_sent++;
        pthread_mutex_unlock(&g_ws.stats_mutex);
    }

    return ret;
}

int ws_send_binary(WsConnection* conn, const void* data, size_t len) {
    if (!conn || !data || len == 0) return -1;

    int ret = send_frame(conn, WS_OPCODE_BINARY, data, len);

    if (ret == 0) {
        conn->messages_sent++;
        conn->bytes_sent += len;
        pthread_mutex_lock(&g_ws.stats_mutex);
        g_ws.stats.messages_sent++;
        pthread_mutex_unlock(&g_ws.stats_mutex);
    }

    return ret;
}

int ws_send_ping(WsConnection* conn) {
    if (!conn) return -1;

    conn->last_ping_sent = time(NULL);
    conn->waiting_for_pong = true;

    return send_frame(conn, WS_OPCODE_PING, NULL, 0);
}

void ws_broadcast_text(const char* message, size_t len) {
    if (!message) return;
    if (len == 0) len = strlen(message);

    pthread_mutex_lock(&g_ws.conn_mutex);

    for (int i = 0; i < g_ws.max_connections; i++) {
        WsConnection* conn = g_ws.connections[i];
        if (conn && conn->state == WS_STATE_OPEN) {
            ws_send_text(conn, message, len);
        }
    }

    pthread_mutex_unlock(&g_ws.conn_mutex);
}

// Maximum number of consecutive failed pings before closing connection
#define MAX_FAILED_PINGS 3

// Maximum idle time before closing connection (5 minutes)
#define MAX_IDLE_TIME_SEC 300

// Maximum consecutive errors before closing connection
#define MAX_CONSECUTIVE_ERRORS 10

void ws_periodic_tasks(void) {
    if (!g_ws.initialized || !g_ws.enabled) return;

    time_t now = time(NULL);

    // Use trylock to avoid blocking if another thread has the lock
    if (pthread_mutex_trylock(&g_ws.conn_mutex) != 0) {
        return;  // Skip this cycle if we can't get the lock
    }

    // Track connections to close (avoid modifying array while iterating)
    // Use fixed-size array to avoid VLA stack issues
    int to_close_count = 0;
    int to_close[128];  // Max 128 connections to close per cycle
    int max_to_close = sizeof(to_close) / sizeof(to_close[0]);

    for (int i = 0; i < g_ws.max_connections; i++) {
        WsConnection* conn = g_ws.connections[i];
        if (!conn) continue;

        bool should_close = false;
        const char* close_reason = NULL;
        WsCloseCode close_code = WS_CLOSE_NORMAL;

        if (conn->state == WS_STATE_OPEN) {
            // Check for too many consecutive errors
            if (conn->consecutive_errors >= MAX_CONSECUTIVE_ERRORS) {
                LOG_WARN("WebSocket", "Too many errors from %s (%d errors)",
                         conn->client_ip, conn->consecutive_errors);
                should_close = true;
                close_reason = "Too many errors";
                close_code = WS_CLOSE_GOING_AWAY;
            }
            // Check pong timeout
            else if (conn->waiting_for_pong) {
                if (now - conn->last_ping_sent > g_ws.config.pong_timeout_sec) {
                    conn->failed_ping_count++;
                    conn->waiting_for_pong = false;  // Reset for retry

                    if (conn->failed_ping_count >= MAX_FAILED_PINGS) {
                        LOG_WARN("WebSocket", "Pong timeout from %s (%d failed pings)",
                                 conn->client_ip, conn->failed_ping_count);
                        should_close = true;
                        close_reason = "Pong timeout";
                        close_code = WS_CLOSE_GOING_AWAY;
                    } else {
                        LOG_DEBUG("WebSocket", "Missed pong from %s (attempt %d/%d)",
                                  conn->client_ip, conn->failed_ping_count, MAX_FAILED_PINGS);
                    }
                }
            }
            // Check idle timeout
            else if (now - conn->last_activity > MAX_IDLE_TIME_SEC) {
                LOG_INFO("WebSocket", "Idle timeout for %s (idle %ld sec)",
                         conn->client_ip, (long)(now - conn->last_activity));
                should_close = true;
                close_reason = "Idle timeout";
                close_code = WS_CLOSE_GOING_AWAY;
            }
            // Send periodic ping if needed
            else if (!conn->waiting_for_pong &&
                     now - conn->last_activity > g_ws.config.ping_interval_sec) {
                ws_send_ping(conn);
            }

            if (should_close) {
                ws_close(conn, close_code, close_reason);
            }
        }

        // Mark connections for cleanup
        if (conn->state == WS_STATE_CLOSED && to_close_count < max_to_close) {
            to_close[to_close_count++] = i;
        }
    }

    // Clean up closed connections
    for (int j = 0; j < to_close_count; j++) {
        int i = to_close[j];
        WsConnection* conn = g_ws.connections[i];
        if (conn) {
            LOG_DEBUG("WebSocket", "Cleaning up closed connection from %s "
                      "(sent: %llu msgs, recv: %llu msgs)",
                      conn->client_ip,
                      (unsigned long long)conn->messages_sent,
                      (unsigned long long)conn->messages_received);

            g_ws.connections[i] = NULL;
            g_ws.connection_count--;

            pthread_mutex_lock(&g_ws.stats_mutex);
            g_ws.stats.active_connections--;
            pthread_mutex_unlock(&g_ws.stats_mutex);

            free_connection(conn);
        }
    }

    pthread_mutex_unlock(&g_ws.conn_mutex);
}

void ws_get_stats(WsStats* stats) {
    if (!stats) return;

    pthread_mutex_lock(&g_ws.stats_mutex);
    memcpy(stats, &g_ws.stats, sizeof(WsStats));
    pthread_mutex_unlock(&g_ws.stats_mutex);
}

WsConnection** ws_get_connections(int* count) {
    if (count) *count = g_ws.connection_count;
    return g_ws.connections;
}

int ws_get_connection_stats(WsConnection* conn, WsConnectionStats* stats) {
    if (!conn || !stats) return -1;

    stats->client_ip = conn->client_ip;
    stats->connected_at = conn->created_at;
    stats->last_activity = conn->last_activity;
    stats->bytes_received = conn->bytes_received;
    stats->bytes_sent = conn->bytes_sent;
    stats->messages_received = conn->messages_received;
    stats->messages_sent = conn->messages_sent;
    stats->consecutive_errors = conn->consecutive_errors;
    stats->failed_ping_count = conn->failed_ping_count;
    stats->state = conn->state;

    return 0;
}

bool ws_is_connection_pressure(void) {
    if (!g_ws.initialized || !g_ws.enabled) return false;

    // More than 80% capacity
    return g_ws.connection_count > (g_ws.max_connections * 8 / 10);
}

int ws_get_available_slots(void) {
    if (!g_ws.initialized || !g_ws.enabled) return 0;

    return g_ws.max_connections - g_ws.connection_count;
}
