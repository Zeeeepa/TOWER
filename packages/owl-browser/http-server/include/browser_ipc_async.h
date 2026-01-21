/**
 * Owl Browser HTTP Server - Async Browser IPC
 *
 * High-performance async IPC with the browser binary.
 * Supports concurrent commands with request ID matching.
 * Uses a dedicated I/O thread for non-blocking communication.
 */

#ifndef OWL_HTTP_BROWSER_IPC_ASYNC_H
#define OWL_HTTP_BROWSER_IPC_ASYNC_H

#include "types.h"
#include "config.h"
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/types.h>

// Forward declarations
typedef struct AsyncIPCHandle AsyncIPCHandle;

// Callback for async command completion
typedef void (*AsyncCommandCallback)(int request_id, bool success,
                                      const char* result_json,
                                      const char* error,
                                      void* user_data);

// Browser process state
typedef enum {
    ASYNC_BROWSER_STOPPED,
    ASYNC_BROWSER_STARTING,
    ASYNC_BROWSER_READY,
    ASYNC_BROWSER_ERROR,
    ASYNC_BROWSER_LICENSE_ERROR
} AsyncBrowserState;

// License error information
typedef struct {
    char status[64];
    char message[512];
    char fingerprint[128];
} AsyncLicenseError;

// Pending request entry
typedef struct PendingRequest {
    int request_id;
    AsyncCommandCallback callback;
    void* user_data;
    uint64_t submit_time_ms;
    uint64_t timeout_ms;
    struct PendingRequest* next;
} PendingRequest;

// Statistics
typedef struct {
    uint64_t commands_sent;
    uint64_t commands_completed;
    uint64_t commands_failed;
    uint64_t commands_timeout;
    uint64_t total_latency_ms;
    uint32_t pending_count;
    uint32_t max_pending;
} AsyncIPCStats;

/**
 * Initialize async browser IPC system.
 * @return 0 on success, -1 on error
 */
int browser_ipc_async_init(void);

/**
 * Shutdown async browser IPC system.
 */
void browser_ipc_async_shutdown(void);

/**
 * Start the browser process.
 * @param browser_path Path to browser binary
 * @param timeout_ms Default command timeout in milliseconds
 * @return 0 on success, -1 on error
 */
int browser_ipc_async_start(const char* browser_path, int timeout_ms);

/**
 * Stop the browser process.
 */
void browser_ipc_async_stop(void);

/**
 * Restart the browser process.
 * Stops the current browser (if running) and starts a new one.
 * @return 0 on success, -1 on error
 */
int browser_ipc_async_restart(void);

/**
 * Check if browser is ready.
 */
bool browser_ipc_async_is_ready(void);

/**
 * Get current browser state.
 */
AsyncBrowserState browser_ipc_async_get_state(void);

/**
 * Get license error (if state is LICENSE_ERROR).
 */
const AsyncLicenseError* browser_ipc_async_get_license_error(void);

/**
 * Send an async command to the browser.
 * The callback will be invoked from the I/O thread when response arrives.
 *
 * @param method Command method name
 * @param params_json JSON parameters (can be NULL for "{}")
 * @param callback Completion callback (called from I/O thread)
 * @param user_data User data passed to callback
 * @param timeout_ms Command timeout (0 for default)
 * @return request_id on success (>0), -1 on error
 */
int browser_ipc_async_send(const char* method, const char* params_json,
                           AsyncCommandCallback callback, void* user_data,
                           int timeout_ms);

/**
 * Send a synchronous command (blocks until response).
 * This is a convenience wrapper around the async API.
 *
 * @param method Command method name
 * @param params_json JSON parameters
 * @param result Output result structure
 * @return 0 on success, -1 on error
 */
int browser_ipc_async_send_sync(const char* method, const char* params_json,
                                 OperationResult* result);

/**
 * Get IPC statistics.
 */
void browser_ipc_async_get_stats(AsyncIPCStats* stats);

/**
 * Cancel a pending command.
 * @param request_id Request ID returned by browser_ipc_async_send
 * @return true if cancelled, false if not found
 */
bool browser_ipc_async_cancel(int request_id);

/**
 * Get count of pending requests.
 */
int browser_ipc_async_pending_count(void);

/**
 * Check if multi-IPC mode is available (Linux only).
 * Multi-IPC allows parallel command processing through multiple socket connections.
 */
bool browser_ipc_async_is_multi_ipc(void);

/**
 * Get multi-IPC socket path (if available).
 * @return Socket path or NULL if not in multi-IPC mode
 */
const char* browser_ipc_async_get_socket_path(void);

/**
 * Get number of active socket connections (multi-IPC only).
 */
int browser_ipc_async_get_connection_count(void);

#endif // OWL_HTTP_BROWSER_IPC_ASYNC_H
