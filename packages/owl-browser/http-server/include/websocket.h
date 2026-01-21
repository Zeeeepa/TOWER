/**
 * Owl Browser HTTP Server - WebSocket Support
 *
 * Two-way WebSocket communication for real-time browser control.
 * Implements RFC 6455 WebSocket protocol.
 *
 * WebSocket endpoint: /ws
 * Uses same authentication as HTTP API (Bearer token or JWT).
 *
 * Message Format (JSON):
 *   Request:  {"id": 1, "method": "navigate", "params": {"context_id": "...", "url": "..."}}
 *   Response: {"id": 1, "success": true, "result": {...}}
 *   Error:    {"id": 1, "success": false, "error": "..."}
 *   Event:    {"event": "browser_event", "data": {...}}
 */

#ifndef OWL_HTTP_WEBSOCKET_H
#define OWL_HTTP_WEBSOCKET_H

#include "config.h"
#include "types.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

// WebSocket opcodes (RFC 6455)
typedef enum {
    WS_OPCODE_CONTINUATION = 0x0,
    WS_OPCODE_TEXT = 0x1,
    WS_OPCODE_BINARY = 0x2,
    WS_OPCODE_CLOSE = 0x8,
    WS_OPCODE_PING = 0x9,
    WS_OPCODE_PONG = 0xA
} WsOpcode;

// WebSocket close status codes (RFC 6455)
typedef enum {
    WS_CLOSE_NORMAL = 1000,
    WS_CLOSE_GOING_AWAY = 1001,
    WS_CLOSE_PROTOCOL_ERROR = 1002,
    WS_CLOSE_UNSUPPORTED_DATA = 1003,
    WS_CLOSE_NO_STATUS = 1005,
    WS_CLOSE_ABNORMAL = 1006,
    WS_CLOSE_INVALID_DATA = 1007,
    WS_CLOSE_POLICY_VIOLATION = 1008,
    WS_CLOSE_MESSAGE_TOO_BIG = 1009,
    WS_CLOSE_EXTENSION_REQUIRED = 1010,
    WS_CLOSE_INTERNAL_ERROR = 1011
} WsCloseCode;

// WebSocket connection state
typedef enum {
    WS_STATE_CONNECTING,
    WS_STATE_OPEN,
    WS_STATE_CLOSING,
    WS_STATE_CLOSED
} WsState;

// WebSocket frame (internal)
typedef struct {
    bool fin;
    WsOpcode opcode;
    bool masked;
    uint64_t payload_len;
    uint8_t mask_key[4];
    char* payload;
} WsFrame;

// WebSocket connection handle
typedef struct WsConnection WsConnection;

// WebSocket message handler callback
// Called when a complete message is received from a client
// handler should process the message and optionally send a response via ws_send_*
typedef void (*WsMessageHandler)(WsConnection* conn, const char* message, size_t len);

// WebSocket connection event handlers
typedef void (*WsConnectHandler)(WsConnection* conn);
typedef void (*WsDisconnectHandler)(WsConnection* conn, WsCloseCode code, const char* reason);

// ============================================================================
// Initialization / Shutdown
// ============================================================================

/**
 * Initialize WebSocket subsystem.
 *
 * @param config Server configuration
 * @return 0 on success, -1 on error
 */
int ws_init(const ServerConfig* config);

/**
 * Shutdown WebSocket subsystem.
 * Closes all active connections gracefully.
 */
void ws_shutdown(void);

/**
 * Check if WebSocket is enabled.
 */
bool ws_is_enabled(void);

// ============================================================================
// Message Handlers
// ============================================================================

/**
 * Set the message handler callback.
 * Called when a complete text message is received from a client.
 */
void ws_set_message_handler(WsMessageHandler handler);

/**
 * Set the connect handler callback.
 * Called when a new WebSocket connection is established.
 */
void ws_set_connect_handler(WsConnectHandler handler);

/**
 * Set the disconnect handler callback.
 * Called when a WebSocket connection is closed.
 */
void ws_set_disconnect_handler(WsDisconnectHandler handler);

// ============================================================================
// HTTP Upgrade Handling
// ============================================================================

/**
 * Check if an HTTP request is a WebSocket upgrade request.
 *
 * @param request The HTTP request
 * @return true if this is a WebSocket upgrade request
 */
bool ws_is_upgrade_request(const HttpRequest* request);

/**
 * Handle WebSocket upgrade handshake.
 * This validates the request, performs authentication, and completes the handshake.
 *
 * @param request The HTTP upgrade request
 * @param response Output: The HTTP response (101 Switching Protocols or error)
 * @param client_fd The client socket file descriptor
 * @param client_ip The client IP address
 * @return The new WebSocket connection on success, NULL on error
 */
WsConnection* ws_handle_upgrade(const HttpRequest* request, HttpResponse* response,
                                 int client_fd, const char* client_ip);

// ============================================================================
// Connection Management
// ============================================================================

/**
 * Process incoming data on a WebSocket connection.
 * Should be called when poll() indicates data is available.
 *
 * @param conn The WebSocket connection
 * @return 0 on success, -1 on error (connection should be closed)
 */
int ws_process_read(WsConnection* conn);

/**
 * Process outgoing data on a WebSocket connection.
 * Should be called when poll() indicates socket is writable and there's pending data.
 *
 * @param conn The WebSocket connection
 * @return 0 on success, -1 on error (connection should be closed)
 */
int ws_process_write(WsConnection* conn);

/**
 * Check if connection has pending data to write.
 *
 * @param conn The WebSocket connection
 * @return true if there's data waiting to be sent
 */
bool ws_has_pending_write(WsConnection* conn);

/**
 * Get the file descriptor for a connection.
 *
 * @param conn The WebSocket connection
 * @return The socket file descriptor, or -1 if invalid
 */
int ws_get_fd(WsConnection* conn);

/**
 * Get the connection state.
 *
 * @param conn The WebSocket connection
 * @return The current state
 */
WsState ws_get_state(WsConnection* conn);

/**
 * Get the client IP address for a connection.
 *
 * @param conn The WebSocket connection
 * @return The client IP address string
 */
const char* ws_get_client_ip(WsConnection* conn);

/**
 * Get user data associated with a connection.
 *
 * @param conn The WebSocket connection
 * @return The user data pointer, or NULL if not set
 */
void* ws_get_user_data(WsConnection* conn);

/**
 * Set user data associated with a connection.
 *
 * @param conn The WebSocket connection
 * @param data The user data pointer
 */
void ws_set_user_data(WsConnection* conn, void* data);

/**
 * Close a WebSocket connection gracefully.
 *
 * @param conn The WebSocket connection
 * @param code Close status code
 * @param reason Close reason (optional, can be NULL)
 */
void ws_close(WsConnection* conn, WsCloseCode code, const char* reason);

// ============================================================================
// Sending Messages
// ============================================================================

/**
 * Send a text message to a client.
 *
 * @param conn The WebSocket connection
 * @param message The message to send
 * @param len Message length (or 0 to use strlen)
 * @return 0 on success, -1 on error
 */
int ws_send_text(WsConnection* conn, const char* message, size_t len);

/**
 * Send a binary message to a client.
 *
 * @param conn The WebSocket connection
 * @param data The data to send
 * @param len Data length
 * @return 0 on success, -1 on error
 */
int ws_send_binary(WsConnection* conn, const void* data, size_t len);

/**
 * Send a ping frame to a client.
 *
 * @param conn The WebSocket connection
 * @return 0 on success, -1 on error
 */
int ws_send_ping(WsConnection* conn);

/**
 * Broadcast a text message to all connected clients.
 *
 * @param message The message to send
 * @param len Message length (or 0 to use strlen)
 */
void ws_broadcast_text(const char* message, size_t len);

// ============================================================================
// Periodic Tasks
// ============================================================================

/**
 * Perform periodic maintenance tasks.
 * Should be called from the main event loop.
 * - Sends ping frames to check connection health
 * - Closes connections that haven't responded to ping
 * - Cleans up closed connections
 */
void ws_periodic_tasks(void);

// ============================================================================
// Statistics
// ============================================================================

/**
 * Get WebSocket statistics.
 */
typedef struct {
    int active_connections;
    uint64_t total_connections;
    uint64_t messages_received;
    uint64_t messages_sent;
    uint64_t bytes_received;
    uint64_t bytes_sent;
} WsStats;

void ws_get_stats(WsStats* stats);

/**
 * Per-connection statistics.
 */
typedef struct {
    const char* client_ip;
    time_t connected_at;
    time_t last_activity;
    uint64_t bytes_received;
    uint64_t bytes_sent;
    uint64_t messages_received;
    uint64_t messages_sent;
    int consecutive_errors;
    int failed_ping_count;
    WsState state;
} WsConnectionStats;

/**
 * Get statistics for a specific connection.
 *
 * @param conn The WebSocket connection
 * @param stats Output: Connection statistics
 * @return 0 on success, -1 on error
 */
int ws_get_connection_stats(WsConnection* conn, WsConnectionStats* stats);

/**
 * Check if the server is under connection pressure.
 * Returns true if more than 80% of max connections are in use.
 *
 * @return true if connection pool is nearly full
 */
bool ws_is_connection_pressure(void);

/**
 * Get the number of available connection slots.
 *
 * @return Number of available slots
 */
int ws_get_available_slots(void);

// ============================================================================
// Connection Iteration
// ============================================================================

/**
 * Get all active WebSocket connections.
 * Returns an array of connection pointers.
 *
 * @param count Output: number of connections returned
 * @return Array of WsConnection pointers (do not free)
 */
WsConnection** ws_get_connections(int* count);

#endif // OWL_HTTP_WEBSOCKET_H
