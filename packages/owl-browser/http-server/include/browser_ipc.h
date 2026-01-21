/**
 * Owl Browser HTTP Server - Browser IPC
 *
 * Inter-process communication with the browser binary.
 * Manages browser process lifecycle and JSON command/response protocol.
 */

#ifndef OWL_HTTP_BROWSER_IPC_H
#define OWL_HTTP_BROWSER_IPC_H

#include "types.h"
#include "config.h"
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/types.h>

// Browser process state
typedef enum {
    BROWSER_STATE_STOPPED,
    BROWSER_STATE_STARTING,
    BROWSER_STATE_READY,
    BROWSER_STATE_ERROR,
    BROWSER_STATE_LICENSE_ERROR
} BrowserState;

// License error information
typedef struct {
    char status[64];
    char message[512];
    char fingerprint[128];
} LicenseError;

// Browser IPC handle
typedef struct {
    pid_t pid;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
    BrowserState state;
    LicenseError license_error;
    char instance_id[64];
    pthread_mutex_t mutex;
    pthread_t stderr_thread;
    bool stderr_thread_running;
    int command_id;
    int timeout_ms;
} BrowserIPC;

/**
 * Initialize browser IPC system.
 * Must be called once at startup.
 */
int browser_ipc_init(void);

/**
 * Shutdown browser IPC system.
 * Terminates browser process if running.
 */
void browser_ipc_shutdown(void);

/**
 * Start the browser process.
 *
 * @param browser_path Path to browser binary
 * @param timeout_ms Command timeout in milliseconds
 * @return 0 on success, -1 on error
 */
int browser_ipc_start(const char* browser_path, int timeout_ms);

/**
 * Stop the browser process gracefully.
 */
void browser_ipc_stop(void);

/**
 * Check if browser is ready.
 */
bool browser_ipc_is_ready(void);

/**
 * Get current browser state.
 */
BrowserState browser_ipc_get_state(void);

/**
 * Get license error information (if state is BROWSER_STATE_LICENSE_ERROR).
 */
const LicenseError* browser_ipc_get_license_error(void);

/**
 * Send a command to the browser and wait for response.
 *
 * @param method Command method name
 * @param params JSON object with parameters (can be NULL)
 * @param result Output: result structure (caller must free result.data)
 * @return 0 on success, -1 on error
 */
int browser_ipc_send_command(const char* method, const char* params_json,
                             OperationResult* result);

/**
 * Send a raw JSON command string.
 *
 * @param json_command Complete JSON command string
 * @param result Output: result structure
 * @return 0 on success, -1 on error
 */
int browser_ipc_send_raw(const char* json_command, OperationResult* result);

/**
 * Restart browser process (e.g., after crash).
 */
int browser_ipc_restart(const char* browser_path, int timeout_ms);

#endif // OWL_HTTP_BROWSER_IPC_H
