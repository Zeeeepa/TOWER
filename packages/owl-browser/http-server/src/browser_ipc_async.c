/**
 * Owl Browser HTTP Server - Async Browser IPC Implementation
 *
 * High-performance async IPC supporting concurrent commands.
 * Uses a dedicated I/O thread for non-blocking browser communication.
 */

#include "browser_ipc_async.h"
#include "json.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <uuid/uuid.h>

// Multi-IPC support (Linux and macOS - uses Unix Domain Sockets)
#if defined(__linux__) || defined(__APPLE__)
#include <sys/socket.h>
#include <sys/un.h>
#define MULTI_IPC_SUPPORTED 1
#define MULTI_IPC_POOL_SIZE 64  // Number of parallel socket connections
#else
#define MULTI_IPC_SUPPORTED 0
#endif

// Configuration
#define MAX_PENDING_REQUESTS 1024
#define RESPONSE_BUFFER_SIZE (8 * 1024 * 1024)  // 8MB (screenshots can be large)
#define WRITE_BUFFER_SIZE (256 * 1024)           // 256KB
#define IO_POLL_TIMEOUT_MS 10                    // Fast polling for low latency

// Get current time in milliseconds
static uint64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// Write queue entry
typedef struct WriteQueueEntry {
    char* data;
    size_t size;
    size_t offset;
    struct WriteQueueEntry* next;
} WriteQueueEntry;

// Global async IPC state
static struct {
    // Browser process
    pid_t pid;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
    AsyncBrowserState state;
    AsyncLicenseError license_error;
    char instance_id[64];
    char browser_path[1024];
    int default_timeout_ms;

    // I/O thread
    pthread_t io_thread;
    pthread_t stderr_thread;
    volatile bool io_running;
    volatile bool stderr_running;

    // Request ID generator (atomic)
    volatile int next_request_id;

    // Pending requests (protected by mutex)
    pthread_mutex_t pending_mutex;
    PendingRequest* pending_head;
    PendingRequest* pending_tail;
    int pending_count;

    // Write queue (protected by mutex)
    pthread_mutex_t write_mutex;
    WriteQueueEntry* write_head;
    WriteQueueEntry* write_tail;
    pthread_cond_t write_cond;

    // Response buffer
    char* response_buffer;
    size_t response_len;

    // Statistics
    AsyncIPCStats stats;
    pthread_mutex_t stats_mutex;

#if MULTI_IPC_SUPPORTED
    // Multi-IPC (Linux only)
    bool multi_ipc_available;
    char multi_ipc_socket_path[256];
    int multi_ipc_sockets[MULTI_IPC_POOL_SIZE];
    bool multi_ipc_socket_in_use[MULTI_IPC_POOL_SIZE];
    pthread_mutex_t multi_ipc_mutex;
    pthread_cond_t multi_ipc_cond;
    int multi_ipc_connected_count;
#endif

} g_async = {
    .pid = -1,
    .stdin_fd = -1,
    .stdout_fd = -1,
    .stderr_fd = -1,
    .state = ASYNC_BROWSER_STOPPED,
    .io_running = false,
    .next_request_id = 1,
    .pending_count = 0
};

// ============================================================================
// Internal Helpers
// ============================================================================

static void generate_instance_id(char* buf, size_t size) {
    uuid_t uuid;
    uuid_generate(uuid);
    char uuid_str[37];  // UUID string is 36 chars + null terminator
    uuid_unparse_lower(uuid, uuid_str);
    snprintf(buf, size, "http_async_%s", uuid_str);
}

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Find and remove pending request by ID
static PendingRequest* find_and_remove_pending(int request_id) {
    pthread_mutex_lock(&g_async.pending_mutex);

    PendingRequest* prev = NULL;
    PendingRequest* curr = g_async.pending_head;

    while (curr) {
        if (curr->request_id == request_id) {
            // Remove from list
            if (prev) {
                prev->next = curr->next;
            } else {
                g_async.pending_head = curr->next;
            }
            if (curr == g_async.pending_tail) {
                g_async.pending_tail = prev;
            }
            g_async.pending_count--;

            pthread_mutex_unlock(&g_async.pending_mutex);
            return curr;
        }
        prev = curr;
        curr = curr->next;
    }

    pthread_mutex_unlock(&g_async.pending_mutex);
    return NULL;
}

// Add pending request
static void add_pending(PendingRequest* req) {
    pthread_mutex_lock(&g_async.pending_mutex);

    req->next = NULL;
    if (g_async.pending_tail) {
        g_async.pending_tail->next = req;
    } else {
        g_async.pending_head = req;
    }
    g_async.pending_tail = req;
    g_async.pending_count++;

    if (g_async.pending_count > g_async.stats.max_pending) {
        g_async.stats.max_pending = g_async.pending_count;
    }

    pthread_mutex_unlock(&g_async.pending_mutex);
}

// Enqueue data for writing
static int enqueue_write(const char* data, size_t size) {
    WriteQueueEntry* entry = malloc(sizeof(WriteQueueEntry));
    if (!entry) return -1;

    entry->data = malloc(size);
    if (!entry->data) {
        free(entry);
        return -1;
    }

    memcpy(entry->data, data, size);
    entry->size = size;
    entry->offset = 0;
    entry->next = NULL;

    pthread_mutex_lock(&g_async.write_mutex);
    if (g_async.write_tail) {
        g_async.write_tail->next = entry;
    } else {
        g_async.write_head = entry;
    }
    g_async.write_tail = entry;
    pthread_cond_signal(&g_async.write_cond);
    pthread_mutex_unlock(&g_async.write_mutex);

    return 0;
}

// Extract raw JSON value from a line starting at a given position
// Returns newly allocated string containing the raw JSON value, or NULL on error
static char* extract_raw_json_value(const char* json, const char* key) {
    // Find "key":
    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\":", key);
    const char* key_start = strstr(json, search_key);
    if (!key_start) return NULL;

    const char* value_start = key_start + strlen(search_key);
    // Skip whitespace
    while (*value_start == ' ' || *value_start == '\t' || *value_start == '\n' || *value_start == '\r') {
        value_start++;
    }

    if (!*value_start) return NULL;

    // Determine value type and find end
    const char* value_end = value_start;
    int depth = 0;
    bool in_string = false;
    bool escape_next = false;

    if (*value_start == '{' || *value_start == '[') {
        // Object or array - find matching closing bracket
        char open_char = *value_start;
        char close_char = (open_char == '{') ? '}' : ']';
        depth = 1;
        value_end++;

        while (*value_end && depth > 0) {
            if (escape_next) {
                escape_next = false;
            } else if (*value_end == '\\' && in_string) {
                escape_next = true;
            } else if (*value_end == '"') {
                in_string = !in_string;
            } else if (!in_string) {
                if (*value_end == open_char) depth++;
                else if (*value_end == close_char) depth--;
            }
            if (depth > 0) value_end++;
        }
        if (depth == 0) value_end++; // Include closing bracket
    } else if (*value_start == '"') {
        // String - find closing quote
        value_end++;
        while (*value_end && !(*value_end == '"' && !escape_next)) {
            if (*value_end == '\\' && !escape_next) {
                escape_next = true;
            } else {
                escape_next = false;
            }
            value_end++;
        }
        if (*value_end == '"') value_end++; // Include closing quote
    } else {
        // Number, boolean, or null - find end (comma, }, or ])
        while (*value_end && *value_end != ',' && *value_end != '}' && *value_end != ']' &&
               *value_end != '\n' && *value_end != '\r') {
            value_end++;
        }
        // Trim trailing whitespace
        while (value_end > value_start &&
               (value_end[-1] == ' ' || value_end[-1] == '\t')) {
            value_end--;
        }
    }

    size_t len = value_end - value_start;
    if (len == 0) return NULL;

    char* result = malloc(len + 1);
    if (!result) return NULL;
    memcpy(result, value_start, len);
    result[len] = '\0';
    return result;
}

// Forward declaration for multi-IPC (defined later, but called from process_response)
#if MULTI_IPC_SUPPORTED
static void multi_ipc_init_connections(void);
#endif

// Process a complete response line
static void process_response(const char* line) {
    // Check for special control messages on stdout (browser sends these to stdout, not stderr)
#if MULTI_IPC_SUPPORTED
    // Check for Multi-IPC ready signal: "MULTI_IPC_READY /path/to/socket.sock"
    if (strncmp(line, "MULTI_IPC_READY ", 16) == 0) {
        const char* path_start = line + 16;
        size_t path_len = strlen(path_start);

        // Trim trailing whitespace
        while (path_len > 0 && (path_start[path_len-1] == ' ' ||
               path_start[path_len-1] == '\r' || path_start[path_len-1] == '\n')) {
            path_len--;
        }

        if (path_len > 0 && path_len < sizeof(g_async.multi_ipc_socket_path)) {
            pthread_mutex_lock(&g_async.multi_ipc_mutex);
            if (!g_async.multi_ipc_available) {
                strncpy(g_async.multi_ipc_socket_path, path_start, path_len);
                g_async.multi_ipc_socket_path[path_len] = '\0';
                pthread_mutex_unlock(&g_async.multi_ipc_mutex);

                LOG_INFO("AsyncIPC", "Multi-IPC socket detected on stdout: %s", g_async.multi_ipc_socket_path);

                // Initialize socket connections
                multi_ipc_init_connections();
            } else {
                pthread_mutex_unlock(&g_async.multi_ipc_mutex);
            }
        }
        return;
    }
#endif

    // Check for READY signal (standalone, not part of MULTI_IPC_READY)
    if (strcmp(line, "READY") == 0) {
        if (g_async.state == ASYNC_BROWSER_STARTING) {
            g_async.state = ASYNC_BROWSER_READY;
            LOG_INFO("AsyncIPC", "Browser ready signal detected on stdout");
        }
        return;
    }

    JsonValue* resp = json_parse(line);
    if (!resp) {
        LOG_WARN("AsyncIPC", "Failed to parse response: %.100s", line);
        return;
    }

    int request_id = (int)json_object_get_int(resp, "id", -1);
    if (request_id <= 0) {
        json_free(resp);
        return;
    }

    PendingRequest* pending = find_and_remove_pending(request_id);
    if (!pending) {
        LOG_DEBUG("AsyncIPC", "No pending request for ID %d", request_id);
        json_free(resp);
        return;
    }

    // Calculate latency
    uint64_t latency = get_time_ms() - pending->submit_time_ms;

    pthread_mutex_lock(&g_async.stats_mutex);
    g_async.stats.commands_completed++;
    g_async.stats.total_latency_ms += latency;
    g_async.stats.pending_count = g_async.pending_count;
    pthread_mutex_unlock(&g_async.stats_mutex);

    // Check for error
    const char* error = json_object_get_string(resp, "error");
    if (error) {
        if (pending->callback) {
            pending->callback(request_id, false, NULL, error, pending->user_data);
        }
        pthread_mutex_lock(&g_async.stats_mutex);
        g_async.stats.commands_failed++;
        pthread_mutex_unlock(&g_async.stats_mutex);
    } else {
        // Extract raw result JSON without re-serialization
        // This is more efficient than parsing and re-building
        char* result_json = extract_raw_json_value(line, "result");

        if (pending->callback) {
            pending->callback(request_id, true, result_json, NULL, pending->user_data);
        }

        free(result_json);
    }

    json_free(resp);
    free(pending);
}

// Check for timed out requests
static void check_timeouts(void) {
    uint64_t now = get_time_ms();

    pthread_mutex_lock(&g_async.pending_mutex);

    PendingRequest* prev = NULL;
    PendingRequest* curr = g_async.pending_head;

    while (curr) {
        PendingRequest* next = curr->next;

        if (now - curr->submit_time_ms > curr->timeout_ms) {
            // Remove from list
            if (prev) {
                prev->next = next;
            } else {
                g_async.pending_head = next;
            }
            if (curr == g_async.pending_tail) {
                g_async.pending_tail = prev;
            }
            g_async.pending_count--;

            pthread_mutex_unlock(&g_async.pending_mutex);

            // Invoke callback with timeout error
            if (curr->callback) {
                curr->callback(curr->request_id, false, NULL,
                              "Command timeout", curr->user_data);
            }

            pthread_mutex_lock(&g_async.stats_mutex);
            g_async.stats.commands_timeout++;
            g_async.stats.pending_count = g_async.pending_count;
            pthread_mutex_unlock(&g_async.stats_mutex);

            free(curr);

            pthread_mutex_lock(&g_async.pending_mutex);
            // Don't update prev since we removed curr
        } else {
            prev = curr;
        }

        curr = next;
    }

    pthread_mutex_unlock(&g_async.pending_mutex);
}

// I/O thread - handles all browser communication
static void* io_thread_func(void* arg) {
    (void)arg;

    LOG_INFO("AsyncIPC", "I/O thread started");

    while (g_async.io_running) {
        struct pollfd fds[2];
        int nfds = 0;

        // Always poll stdout for reading
        if (g_async.stdout_fd >= 0) {
            fds[nfds].fd = g_async.stdout_fd;
            fds[nfds].events = POLLIN;
            nfds++;
        }

        // Poll stdin for writing if we have data
        pthread_mutex_lock(&g_async.write_mutex);
        bool has_write_data = (g_async.write_head != NULL);
        pthread_mutex_unlock(&g_async.write_mutex);

        if (has_write_data && g_async.stdin_fd >= 0) {
            fds[nfds].fd = g_async.stdin_fd;
            fds[nfds].events = POLLOUT;
            nfds++;
        }

        if (nfds == 0) {
            usleep(1000);  // 1ms sleep if no fds
            check_timeouts();
            continue;
        }

        int ret = poll(fds, nfds, IO_POLL_TIMEOUT_MS);

        if (ret < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("AsyncIPC", "Poll error: %s", strerror(errno));
            break;
        }

        // Process poll results
        for (int i = 0; i < nfds; i++) {
            // Handle readable (responses from browser)
            if ((fds[i].revents & POLLIN) && fds[i].fd == g_async.stdout_fd) {
                // Read into response buffer
                size_t space = RESPONSE_BUFFER_SIZE - g_async.response_len - 1;
                if (space > 0) {
                    ssize_t n = read(g_async.stdout_fd,
                                    g_async.response_buffer + g_async.response_len,
                                    space);
                    if (n > 0) {
                        g_async.response_len += n;
                        g_async.response_buffer[g_async.response_len] = '\0';

                        // Process complete lines
                        char* line_start = g_async.response_buffer;
                        char* newline;

                        while ((newline = strchr(line_start, '\n')) != NULL) {
                            *newline = '\0';
                            if (newline > line_start) {
                                process_response(line_start);
                            }
                            line_start = newline + 1;
                        }

                        // Move remaining data to start
                        size_t remaining = g_async.response_len - (line_start - g_async.response_buffer);
                        if (remaining > 0 && line_start != g_async.response_buffer) {
                            memmove(g_async.response_buffer, line_start, remaining);
                        }
                        g_async.response_len = remaining;
                    } else if (n == 0) {
                        LOG_WARN("AsyncIPC", "Browser stdout closed");
                        // Close the fd to stop polling on it
                        close(g_async.stdout_fd);
                        g_async.stdout_fd = -1;
                        // Check if browser exited due to license error
                        if (g_async.state != ASYNC_BROWSER_LICENSE_ERROR) {
                            g_async.state = ASYNC_BROWSER_ERROR;
                        }
                        // Stop the I/O thread
                        g_async.io_running = false;
                        break;
                    }
                }
            }

            // Handle writable (send commands to browser)
            if ((fds[i].revents & POLLOUT) && fds[i].fd == g_async.stdin_fd) {
                pthread_mutex_lock(&g_async.write_mutex);

                while (g_async.write_head) {
                    WriteQueueEntry* entry = g_async.write_head;
                    size_t to_write = entry->size - entry->offset;

                    ssize_t n = write(g_async.stdin_fd,
                                     entry->data + entry->offset,
                                     to_write);

                    if (n > 0) {
                        entry->offset += n;
                        if (entry->offset >= entry->size) {
                            // Entry complete, remove it
                            g_async.write_head = entry->next;
                            if (!g_async.write_head) {
                                g_async.write_tail = NULL;
                            }
                            free(entry->data);
                            free(entry);
                        }
                    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                        LOG_ERROR("AsyncIPC", "Write error: %s", strerror(errno));
                        break;
                    } else {
                        // Would block, try again later
                        break;
                    }
                }

                pthread_mutex_unlock(&g_async.write_mutex);
            }

            // Handle errors
            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                LOG_WARN("AsyncIPC", "Poll event on fd %d (revents=0x%x)", fds[i].fd, fds[i].revents);
                // Close the problematic fd
                if (fds[i].fd == g_async.stdout_fd) {
                    close(g_async.stdout_fd);
                    g_async.stdout_fd = -1;
                } else if (fds[i].fd == g_async.stdin_fd) {
                    close(g_async.stdin_fd);
                    g_async.stdin_fd = -1;
                }
                // If both fds are closed, browser is gone
                if (g_async.stdout_fd < 0 && g_async.stdin_fd < 0) {
                    if (g_async.state != ASYNC_BROWSER_LICENSE_ERROR) {
                        g_async.state = ASYNC_BROWSER_ERROR;
                    }
                    g_async.io_running = false;
                    break;
                }
            }
        }

        // Check for timeouts periodically
        check_timeouts();

        // Break outer loop if io_running was set to false
        if (!g_async.io_running) {
            break;
        }
    }

    LOG_INFO("AsyncIPC", "I/O thread exiting");
    return NULL;
}

// ============================================================================
// Multi-IPC Support (Linux only)
// ============================================================================

#if MULTI_IPC_SUPPORTED

// Initialize multi-IPC connection pool
static void multi_ipc_init_connections(void) {
    pthread_mutex_lock(&g_async.multi_ipc_mutex);

    if (g_async.multi_ipc_available) {
        pthread_mutex_unlock(&g_async.multi_ipc_mutex);
        return;  // Already initialized
    }

    // Initialize all sockets to -1
    for (int i = 0; i < MULTI_IPC_POOL_SIZE; i++) {
        g_async.multi_ipc_sockets[i] = -1;
        g_async.multi_ipc_socket_in_use[i] = false;
    }

    // Connect sockets
    int connected = 0;
    for (int i = 0; i < MULTI_IPC_POOL_SIZE; i++) {
        int sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock < 0) {
            LOG_WARN("MultiIPC", "Failed to create socket %d: %s", i, strerror(errno));
            continue;
        }

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, g_async.multi_ipc_socket_path, sizeof(addr.sun_path) - 1);

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            LOG_WARN("MultiIPC", "Failed to connect socket %d: %s", i, strerror(errno));
            close(sock);
            continue;
        }

        g_async.multi_ipc_sockets[i] = sock;
        connected++;
        LOG_DEBUG("MultiIPC", "Connected socket %d (fd=%d)", i, sock);
    }

    g_async.multi_ipc_connected_count = connected;

    if (connected > 0) {
        g_async.multi_ipc_available = true;
        LOG_INFO("MultiIPC", "Multi-IPC enabled with %d/%d connections", connected, MULTI_IPC_POOL_SIZE);
    } else {
        LOG_WARN("MultiIPC", "Failed to establish any connections, using pipe IPC");
    }

    pthread_mutex_unlock(&g_async.multi_ipc_mutex);
}

// Get an available socket from the pool (with timeout)
// Returns socket fd, or -1 if no socket available within timeout
static int multi_ipc_acquire_socket_timeout(int timeout_ms) {
    pthread_mutex_lock(&g_async.multi_ipc_mutex);

    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }

    while (true) {
        // Find an available socket
        for (int i = 0; i < MULTI_IPC_POOL_SIZE; i++) {
            if (g_async.multi_ipc_sockets[i] >= 0 && !g_async.multi_ipc_socket_in_use[i]) {
                g_async.multi_ipc_socket_in_use[i] = true;
                int sock = g_async.multi_ipc_sockets[i];
                pthread_mutex_unlock(&g_async.multi_ipc_mutex);
                return sock;
            }
        }

        // No socket available, wait with timeout
        int wait_result = pthread_cond_timedwait(&g_async.multi_ipc_cond,
                                                  &g_async.multi_ipc_mutex,
                                                  &deadline);

        if (wait_result == ETIMEDOUT) {
            // Count how many sockets are actually in use for better diagnostics
            int in_use_count = 0;
            for (int i = 0; i < MULTI_IPC_POOL_SIZE; i++) {
                if (g_async.multi_ipc_socket_in_use[i]) in_use_count++;
            }
            LOG_WARN("MultiIPC", "Socket acquisition timeout (%dms) - %d/%d sockets in use. "
                     "Consider increasing MULTI_IPC_POOL_SIZE or reducing command concurrency.",
                     timeout_ms, in_use_count, MULTI_IPC_POOL_SIZE);
            pthread_mutex_unlock(&g_async.multi_ipc_mutex);
            return -1;
        }

        // Check if we should give up (browser stopped)
        if (g_async.state != ASYNC_BROWSER_READY) {
            pthread_mutex_unlock(&g_async.multi_ipc_mutex);
            return -1;
        }
    }
}

// Get an available socket from the pool (blocking - for backward compatibility)
static int multi_ipc_acquire_socket(void) {
    // Use 30 second timeout - with 64 sockets this should rarely be hit
    // Each socket processes commands in ~100ms, so 64 sockets can handle ~640 commands per second
    // If we still hit this timeout, the system is overloaded
    return multi_ipc_acquire_socket_timeout(30000);
}

// Release a socket back to the pool
static void multi_ipc_release_socket(int sock) {
    pthread_mutex_lock(&g_async.multi_ipc_mutex);

    for (int i = 0; i < MULTI_IPC_POOL_SIZE; i++) {
        if (g_async.multi_ipc_sockets[i] == sock) {
            g_async.multi_ipc_socket_in_use[i] = false;
            pthread_cond_signal(&g_async.multi_ipc_cond);
            break;
        }
    }

    pthread_mutex_unlock(&g_async.multi_ipc_mutex);
}

// Send command via socket and get response (blocking)
// Returns response JSON string (caller must free) or NULL on error
static char* multi_ipc_send_command(int sock, const char* command) {
    // Send command with newline
    size_t cmd_len = strlen(command);
    char* cmd_buf = malloc(cmd_len + 2);
    if (!cmd_buf) return NULL;

    memcpy(cmd_buf, command, cmd_len);
    cmd_buf[cmd_len] = '\n';
    cmd_buf[cmd_len + 1] = '\0';

    size_t total_sent = 0;
    while (total_sent < cmd_len + 1) {
        ssize_t n = write(sock, cmd_buf + total_sent, cmd_len + 1 - total_sent);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                usleep(1000);
                continue;
            }
            LOG_ERROR("MultiIPC", "Write error: %s", strerror(errno));
            free(cmd_buf);
            return NULL;
        }
        total_sent += n;
    }
    free(cmd_buf);

    // Read response - use larger read buffer for efficiency
    char* response = malloc(RESPONSE_BUFFER_SIZE);
    if (!response) return NULL;

    size_t response_len = 0;
    char buf[65536];  // 64KB read buffer for large responses like screenshots

    while (response_len < RESPONSE_BUFFER_SIZE - 1) {
        struct pollfd pfd = { .fd = sock, .events = POLLIN };
        int ret = poll(&pfd, 1, g_async.default_timeout_ms);

        if (ret < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("MultiIPC", "Poll error: %s", strerror(errno));
            free(response);
            return NULL;
        }

        if (ret == 0) {
            LOG_ERROR("MultiIPC", "Response timeout (read %zu bytes)", response_len);
            free(response);
            return NULL;
        }

        if (pfd.revents & POLLIN) {
            size_t space_left = RESPONSE_BUFFER_SIZE - 1 - response_len;
            size_t to_read = space_left < sizeof(buf) ? space_left : sizeof(buf);

            ssize_t n = read(sock, buf, to_read);
            if (n <= 0) {
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
                LOG_ERROR("MultiIPC", "Read error: %zd (total read: %zu)", n, response_len);
                free(response);
                return NULL;
            }

            memcpy(response + response_len, buf, n);
            response_len += n;
            response[response_len] = '\0';

            // Check for complete response (ends with newline)
            if (response_len > 0 && response[response_len - 1] == '\n') {
                response[response_len - 1] = '\0';  // Remove newline
                return response;
            }
        }
    }

    LOG_ERROR("MultiIPC", "Response buffer overflow at %zu bytes", response_len);
    free(response);
    return NULL;
}

// Close all multi-IPC connections
static void multi_ipc_close_connections(void) {
    pthread_mutex_lock(&g_async.multi_ipc_mutex);

    for (int i = 0; i < MULTI_IPC_POOL_SIZE; i++) {
        if (g_async.multi_ipc_sockets[i] >= 0) {
            close(g_async.multi_ipc_sockets[i]);
            g_async.multi_ipc_sockets[i] = -1;
        }
        g_async.multi_ipc_socket_in_use[i] = false;
    }

    g_async.multi_ipc_available = false;
    g_async.multi_ipc_connected_count = 0;
    g_async.multi_ipc_socket_path[0] = '\0';

    pthread_cond_broadcast(&g_async.multi_ipc_cond);
    pthread_mutex_unlock(&g_async.multi_ipc_mutex);

    LOG_INFO("MultiIPC", "All connections closed");
}

#endif // MULTI_IPC_SUPPORTED

// Stderr reader thread
static void* stderr_thread_func(void* arg) {
    (void)arg;

    // Use a larger accumulated buffer to handle messages split across reads
    static char accum_buffer[16384];
    static size_t accum_len = 0;
    char read_buffer[4096];

    // Keep searching for multi-IPC even after browser is ready
    // The MULTI_IPC_READY message might arrive slightly after or before READY
    int64_t ready_time = 0;
    bool multi_ipc_search_done = false;

#if MULTI_IPC_SUPPORTED
    // On Linux/macOS, ALWAYS expect multi-IPC - the browser will always output MULTI_IPC_READY
    bool expect_multi_ipc = true;
#else
    bool expect_multi_ipc = false;
#endif

    while (g_async.stderr_running && g_async.stderr_fd >= 0) {
        // If browser is ready and we've been searching for multi-IPC for a while, stop searching
        if (ready_time > 0 && !multi_ipc_search_done) {
            int64_t elapsed = get_time_ms() - ready_time;
            if (elapsed > 2000) {  // Wait 2 seconds after READY for MULTI_IPC_READY
                multi_ipc_search_done = true;
#if MULTI_IPC_SUPPORTED
                if (!g_async.multi_ipc_available && expect_multi_ipc) {
                    LOG_WARN("AsyncIPC", "Multi-IPC not detected within timeout, using pipe IPC");
                }
#endif
            }
        }

        struct pollfd pfd = { .fd = g_async.stderr_fd, .events = POLLIN };
        int ret = poll(&pfd, 1, 10);  // Reduced from 100ms for faster response detection

        if (ret > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(g_async.stderr_fd, read_buffer, sizeof(read_buffer) - 1);
            if (n > 0) {
                read_buffer[n] = '\0';

                // Append to accumulated buffer (with overflow protection)
                size_t space_left = sizeof(accum_buffer) - accum_len - 1;
                size_t to_copy = (size_t)n < space_left ? (size_t)n : space_left;
                if (to_copy > 0) {
                    memcpy(accum_buffer + accum_len, read_buffer, to_copy);
                    accum_len += to_copy;
                    accum_buffer[accum_len] = '\0';
                }

                LOG_DEBUG("Browser", "%s", read_buffer);

                // Check for license errors
                if (strstr(accum_buffer, "LICENSE REQUIRED") ||
                    strstr(accum_buffer, "License validation failed")) {
                    g_async.state = ASYNC_BROWSER_LICENSE_ERROR;
                    strncpy(g_async.license_error.message, accum_buffer,
                           sizeof(g_async.license_error.message) - 1);
                }

#if MULTI_IPC_SUPPORTED
                // Check for Multi-IPC ready signal (Linux only)
                // Format: "MULTI_IPC_READY /path/to/socket.sock\n"
                // Use accumulated buffer to handle split messages
                if (!g_async.multi_ipc_available) {
                    char* multi_ipc_pos = strstr(accum_buffer, "MULTI_IPC_READY ");
                    if (multi_ipc_pos) {
                        char* path_start = multi_ipc_pos + strlen("MULTI_IPC_READY ");
                        char* path_end = strchr(path_start, '\n');

                        // Only process if we have the complete line (ending with newline)
                        if (path_end) {
                            size_t path_len = path_end - path_start;
                            if (path_len > 0 && path_len < sizeof(g_async.multi_ipc_socket_path)) {
                                strncpy(g_async.multi_ipc_socket_path, path_start, path_len);
                                g_async.multi_ipc_socket_path[path_len] = '\0';

                                // Trim any trailing whitespace
                                while (path_len > 0 && (g_async.multi_ipc_socket_path[path_len-1] == ' ' ||
                                       g_async.multi_ipc_socket_path[path_len-1] == '\r' ||
                                       g_async.multi_ipc_socket_path[path_len-1] == '\n')) {
                                    g_async.multi_ipc_socket_path[--path_len] = '\0';
                                }

                                LOG_INFO("AsyncIPC", "Multi-IPC socket detected: %s", g_async.multi_ipc_socket_path);

                                // Initialize socket connections
                                multi_ipc_init_connections();

                                if (g_async.multi_ipc_available) {
                                    LOG_INFO("AsyncIPC", "Multi-IPC enabled successfully");
                                    multi_ipc_search_done = true;
                                }
                            }
                        }
                    }
                }
#endif

                // Check for READY signal in accumulated buffer
                // Look for standalone "READY" (not part of "MULTI_IPC_READY")
                if (g_async.state == ASYNC_BROWSER_STARTING) {
                    // Find all occurrences of "READY" and check each
                    char* search_pos = accum_buffer;
                    while ((search_pos = strstr(search_pos, "READY")) != NULL) {
                        // Check if this READY is at a line boundary (not part of MULTI_IPC_READY)
                        bool at_line_start = (search_pos == accum_buffer ||
                                             *(search_pos - 1) == '\n');

                        // Check this is not MULTI_IPC_READY
                        bool is_multi_ipc_ready = false;
                        if (search_pos > accum_buffer) {
                            // Look backwards for "MULTI_IPC_"
                            const char* prefix = "MULTI_IPC_";
                            size_t prefix_len = strlen(prefix);
                            if ((size_t)(search_pos - accum_buffer) >= prefix_len) {
                                if (strncmp(search_pos - prefix_len, prefix, prefix_len) == 0) {
                                    is_multi_ipc_ready = true;
                                }
                            }
                        }

                        if (at_line_start && !is_multi_ipc_ready) {
                            g_async.state = ASYNC_BROWSER_READY;
                            ready_time = get_time_ms();
                            LOG_INFO("AsyncIPC", "Browser ready signal detected");
                            break;
                        }
                        search_pos++;
                    }
                }

                // Trim processed data from accumulated buffer to prevent overflow
                // Keep the last 1KB to handle split messages
                if (accum_len > 8192) {
                    size_t keep = 1024;
                    memmove(accum_buffer, accum_buffer + accum_len - keep, keep);
                    accum_len = keep;
                    accum_buffer[accum_len] = '\0';
                }

            } else if (n == 0) {
                break;
            }
        } else if (ret < 0 && errno != EINTR) {
            break;
        }
    }

    // Clear accumulated buffer on thread exit
    accum_len = 0;
    accum_buffer[0] = '\0';

    return NULL;
}

// ============================================================================
// Public API
// ============================================================================

int browser_ipc_async_init(void) {
    pthread_mutex_init(&g_async.pending_mutex, NULL);
    pthread_mutex_init(&g_async.write_mutex, NULL);
    pthread_mutex_init(&g_async.stats_mutex, NULL);
    pthread_cond_init(&g_async.write_cond, NULL);

#if MULTI_IPC_SUPPORTED
    pthread_mutex_init(&g_async.multi_ipc_mutex, NULL);
    pthread_cond_init(&g_async.multi_ipc_cond, NULL);
    g_async.multi_ipc_available = false;
    g_async.multi_ipc_connected_count = 0;
    g_async.multi_ipc_socket_path[0] = '\0';
    for (int i = 0; i < MULTI_IPC_POOL_SIZE; i++) {
        g_async.multi_ipc_sockets[i] = -1;
        g_async.multi_ipc_socket_in_use[i] = false;
    }
#endif

    g_async.response_buffer = malloc(RESPONSE_BUFFER_SIZE);
    if (!g_async.response_buffer) {
        LOG_ERROR("AsyncIPC", "Failed to allocate response buffer");
        return -1;
    }
    g_async.response_len = 0;

    g_async.state = ASYNC_BROWSER_STOPPED;
    memset(&g_async.stats, 0, sizeof(g_async.stats));

    LOG_INFO("AsyncIPC", "Async IPC initialized");
    return 0;
}

void browser_ipc_async_shutdown(void) {
    browser_ipc_async_stop();

#if MULTI_IPC_SUPPORTED
    multi_ipc_close_connections();
    pthread_mutex_destroy(&g_async.multi_ipc_mutex);
    pthread_cond_destroy(&g_async.multi_ipc_cond);
#endif

    free(g_async.response_buffer);
    g_async.response_buffer = NULL;

    // Free any remaining write queue entries
    while (g_async.write_head) {
        WriteQueueEntry* entry = g_async.write_head;
        g_async.write_head = entry->next;
        free(entry->data);
        free(entry);
    }

    // Free any remaining pending requests
    while (g_async.pending_head) {
        PendingRequest* req = g_async.pending_head;
        g_async.pending_head = req->next;
        free(req);
    }

    pthread_mutex_destroy(&g_async.pending_mutex);
    pthread_mutex_destroy(&g_async.write_mutex);
    pthread_mutex_destroy(&g_async.stats_mutex);
    pthread_cond_destroy(&g_async.write_cond);

    LOG_INFO("AsyncIPC", "Async IPC shutdown complete");
}

int browser_ipc_async_start(const char* browser_path, int timeout_ms) {
    if (!browser_path) {
        LOG_ERROR("AsyncIPC", "Browser path is NULL");
        return -1;
    }

    if (g_async.state != ASYNC_BROWSER_STOPPED) {
        LOG_WARN("AsyncIPC", "Browser already running");
        return 0;
    }

    strncpy(g_async.browser_path, browser_path, sizeof(g_async.browser_path) - 1);
    // Default to 10 seconds (reduced from 30s) - most IPC calls complete in <1 second
    g_async.default_timeout_ms = timeout_ms > 0 ? timeout_ms : 10000;
    g_async.state = ASYNC_BROWSER_STARTING;

    generate_instance_id(g_async.instance_id, sizeof(g_async.instance_id));

    // Create pipes
    int stdin_pipe[2], stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        LOG_ERROR("AsyncIPC", "Failed to create pipes");
        g_async.state = ASYNC_BROWSER_ERROR;
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("AsyncIPC", "Fork failed");
        g_async.state = ASYNC_BROWSER_ERROR;
        return -1;
    }

    if (pid == 0) {
        // Child process
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        setenv("OLIB_INSTANCE_ID", g_async.instance_id, 1);
        execl(browser_path, browser_path, "--instance-id", g_async.instance_id, NULL);
        _exit(1);
    }

    // Parent
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    g_async.pid = pid;
    g_async.stdin_fd = stdin_pipe[1];
    g_async.stdout_fd = stdout_pipe[0];
    g_async.stderr_fd = stderr_pipe[0];

    set_nonblocking(g_async.stdin_fd);
    set_nonblocking(g_async.stdout_fd);

    // Start I/O thread
    g_async.io_running = true;
    if (pthread_create(&g_async.io_thread, NULL, io_thread_func, NULL) != 0) {
        LOG_ERROR("AsyncIPC", "Failed to create I/O thread");
        browser_ipc_async_stop();
        return -1;
    }

    // Start stderr thread
    g_async.stderr_running = true;
    if (pthread_create(&g_async.stderr_thread, NULL, stderr_thread_func, NULL) != 0) {
        LOG_WARN("AsyncIPC", "Failed to create stderr thread");
    }

    LOG_INFO("AsyncIPC", "Started browser (PID: %d, Instance: %s)",
             g_async.pid, g_async.instance_id);

    // Wait for ready
    int wait_ms = 0;
    while (wait_ms < 30000) {
        if (g_async.state == ASYNC_BROWSER_READY) {
            return 0;
        }
        if (g_async.state == ASYNC_BROWSER_LICENSE_ERROR ||
            g_async.state == ASYNC_BROWSER_ERROR) {
            return -1;
        }
        usleep(100000);
        wait_ms += 100;
    }

    LOG_ERROR("AsyncIPC", "Timeout waiting for browser");
    browser_ipc_async_stop();
    return -1;
}

void browser_ipc_async_stop(void) {
    if (g_async.state == ASYNC_BROWSER_STOPPED) {
        return;
    }

    LOG_INFO("AsyncIPC", "Stopping browser (PID: %d)", g_async.pid);

    // Stop threads
    g_async.io_running = false;
    g_async.stderr_running = false;

    // Send shutdown command
    if (g_async.stdin_fd >= 0) {
        const char* cmd = "{\"id\":0,\"method\":\"shutdown\"}\n";
        write(g_async.stdin_fd, cmd, strlen(cmd));
    }

    // Close FDs
    if (g_async.stdin_fd >= 0) { close(g_async.stdin_fd); g_async.stdin_fd = -1; }
    if (g_async.stdout_fd >= 0) { close(g_async.stdout_fd); g_async.stdout_fd = -1; }
    if (g_async.stderr_fd >= 0) { close(g_async.stderr_fd); g_async.stderr_fd = -1; }

    // Wait for process
    if (g_async.pid > 0) {
        int status;
        int wait_count = 0;
        while (waitpid(g_async.pid, &status, WNOHANG) == 0 && wait_count < 30) {
            usleep(100000);
            wait_count++;
        }
        if (wait_count >= 30) {
            kill(g_async.pid, SIGKILL);
            waitpid(g_async.pid, &status, 0);
        }
        g_async.pid = -1;
    }

    // Join threads
    if (g_async.io_thread) {
        pthread_join(g_async.io_thread, NULL);
    }
    if (g_async.stderr_thread) {
        pthread_join(g_async.stderr_thread, NULL);
    }

    // Fail all pending requests
    pthread_mutex_lock(&g_async.pending_mutex);
    while (g_async.pending_head) {
        PendingRequest* req = g_async.pending_head;
        g_async.pending_head = req->next;
        g_async.pending_count--;

        if (req->callback) {
            req->callback(req->request_id, false, NULL,
                         "Browser stopped", req->user_data);
        }
        free(req);
    }
    g_async.pending_tail = NULL;
    pthread_mutex_unlock(&g_async.pending_mutex);

    g_async.state = ASYNC_BROWSER_STOPPED;
    g_async.response_len = 0;

    LOG_INFO("AsyncIPC", "Browser stopped");
}

int browser_ipc_async_restart(void) {
    LOG_INFO("AsyncIPC", "Restarting browser...");

    // Save current settings
    char browser_path[1024];
    int timeout_ms = g_async.default_timeout_ms;
    strncpy(browser_path, g_async.browser_path, sizeof(browser_path) - 1);
    browser_path[sizeof(browser_path) - 1] = '\0';

    // Stop current browser
    browser_ipc_async_stop();

    // Small delay to ensure clean shutdown
    usleep(500000);  // 500ms

    // Start new browser
    int ret = browser_ipc_async_start(browser_path, timeout_ms);
    if (ret == 0) {
        LOG_INFO("AsyncIPC", "Browser restarted successfully");
    } else {
        LOG_ERROR("AsyncIPC", "Failed to restart browser");
    }
    return ret;
}

bool browser_ipc_async_is_ready(void) {
    return g_async.state == ASYNC_BROWSER_READY;
}

AsyncBrowserState browser_ipc_async_get_state(void) {
    return g_async.state;
}

const AsyncLicenseError* browser_ipc_async_get_license_error(void) {
    return &g_async.license_error;
}

int browser_ipc_async_send(const char* method, const char* params_json,
                           AsyncCommandCallback callback, void* user_data,
                           int timeout_ms) {
    if (!method) return -1;

    if (g_async.state != ASYNC_BROWSER_READY) {
        LOG_WARN("AsyncIPC", "Browser not ready, cannot send command");
        return -1;
    }

    // Generate request ID atomically
    int request_id = __sync_fetch_and_add(&g_async.next_request_id, 1);

    // Build command JSON
    JsonBuilder builder;
    json_builder_init(&builder);

    json_builder_object_start(&builder);
    json_builder_key(&builder, "id");
    json_builder_int(&builder, request_id);
    json_builder_comma(&builder);
    json_builder_key(&builder, "method");
    json_builder_string(&builder, method);

    // Merge params
    if (params_json && strlen(params_json) > 2) {
        JsonValue* params = json_parse(params_json);
        if (params && params->type == JSON_OBJECT && params->object_val) {
            JsonPair* pair = params->object_val->pairs;
            while (pair) {
                json_builder_comma(&builder);
                json_builder_key(&builder, pair->key);

                switch (pair->value->type) {
                    case JSON_STRING:
                        json_builder_string(&builder, pair->value->string_val);
                        break;
                    case JSON_NUMBER:
                        json_builder_number(&builder, pair->value->number_val);
                        break;
                    case JSON_BOOL:
                        json_builder_bool(&builder, pair->value->bool_val);
                        break;
                    case JSON_NULL:
                        json_builder_null(&builder);
                        break;
                    default:
                        json_builder_null(&builder);
                        break;
                }
                pair = pair->next;
            }
            json_free(params);
        }
    }

    json_builder_object_end(&builder);

    char* cmd = json_builder_finish(&builder);
    size_t cmd_len = strlen(cmd);

    // Add newline
    char* cmd_nl = malloc(cmd_len + 2);
    memcpy(cmd_nl, cmd, cmd_len);
    cmd_nl[cmd_len] = '\n';
    cmd_nl[cmd_len + 1] = '\0';
    free(cmd);

    // Create pending request
    PendingRequest* pending = malloc(sizeof(PendingRequest));
    if (!pending) {
        free(cmd_nl);
        return -1;
    }

    pending->request_id = request_id;
    pending->callback = callback;
    pending->user_data = user_data;
    pending->submit_time_ms = get_time_ms();
    pending->timeout_ms = timeout_ms > 0 ? timeout_ms : g_async.default_timeout_ms;
    pending->next = NULL;

    // Add to pending list first (before sending)
    add_pending(pending);

    // Enqueue for writing
    if (enqueue_write(cmd_nl, cmd_len + 1) < 0) {
        free(cmd_nl);
        find_and_remove_pending(request_id);
        free(pending);
        return -1;
    }

    free(cmd_nl);

    pthread_mutex_lock(&g_async.stats_mutex);
    g_async.stats.commands_sent++;
    g_async.stats.pending_count = g_async.pending_count;
    pthread_mutex_unlock(&g_async.stats_mutex);

    LOG_DEBUG("AsyncIPC", "Sent command %d: %s", request_id, method);

    return request_id;
}

// Sync wrapper state
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool completed;
    bool success;
    char* result_json;
    char error[512];
} SyncCallState;

static void sync_callback(int request_id, bool success,
                          const char* result_json, const char* error,
                          void* user_data) {
    (void)request_id;
    SyncCallState* state = (SyncCallState*)user_data;

    pthread_mutex_lock(&state->mutex);
    state->success = success;
    if (result_json) {
        state->result_json = strdup(result_json);
    }
    if (error) {
        strncpy(state->error, error, sizeof(state->error) - 1);
    }
    state->completed = true;
    pthread_cond_signal(&state->cond);
    pthread_mutex_unlock(&state->mutex);
}

int browser_ipc_async_send_sync(const char* method, const char* params_json,
                                 OperationResult* result) {
    if (!result) return -1;
    memset(result, 0, sizeof(*result));

#if MULTI_IPC_SUPPORTED
    // Use multi-IPC if available for true parallel processing
    if (g_async.multi_ipc_available) {
        // Acquire a socket from the pool
        int sock = multi_ipc_acquire_socket();
        if (sock < 0) {
            snprintf(result->error, sizeof(result->error), "No socket available");
            return -1;
        }

        // Build command JSON (same format as pipe IPC)
        int request_id = __sync_fetch_and_add(&g_async.next_request_id, 1);

        JsonBuilder builder;
        json_builder_init(&builder);
        json_builder_object_start(&builder);
        json_builder_key(&builder, "id");
        json_builder_int(&builder, request_id);
        json_builder_comma(&builder);
        json_builder_key(&builder, "method");
        json_builder_string(&builder, method);

        // Merge params
        if (params_json && strlen(params_json) > 2) {
            JsonValue* params = json_parse(params_json);
            if (params && params->type == JSON_OBJECT && params->object_val) {
                JsonPair* pair = params->object_val->pairs;
                while (pair) {
                    json_builder_comma(&builder);
                    json_builder_key(&builder, pair->key);
                    switch (pair->value->type) {
                        case JSON_STRING:
                            json_builder_string(&builder, pair->value->string_val);
                            break;
                        case JSON_NUMBER:
                            json_builder_number(&builder, pair->value->number_val);
                            break;
                        case JSON_BOOL:
                            json_builder_bool(&builder, pair->value->bool_val);
                            break;
                        case JSON_NULL:
                        default:
                            json_builder_null(&builder);
                            break;
                    }
                    pair = pair->next;
                }
                json_free(params);
            }
        }
        json_builder_object_end(&builder);

        char* cmd = json_builder_finish(&builder);

        // Update stats
        pthread_mutex_lock(&g_async.stats_mutex);
        g_async.stats.commands_sent++;
        pthread_mutex_unlock(&g_async.stats_mutex);

        // Send via socket and get response
        char* response = multi_ipc_send_command(sock, cmd);
        free(cmd);

        // Release socket back to pool
        multi_ipc_release_socket(sock);

        if (!response) {
            snprintf(result->error, sizeof(result->error), "Multi-IPC command failed");
            pthread_mutex_lock(&g_async.stats_mutex);
            g_async.stats.commands_failed++;
            pthread_mutex_unlock(&g_async.stats_mutex);
            return -1;
        }

        // Parse response
        JsonValue* resp = json_parse(response);
        if (resp) {
            const char* error_str = json_object_get_string(resp, "error");
            if (error_str) {
                result->success = false;
                strncpy(result->error, error_str, sizeof(result->error) - 1);
                pthread_mutex_lock(&g_async.stats_mutex);
                g_async.stats.commands_failed++;
                pthread_mutex_unlock(&g_async.stats_mutex);
            } else {
                result->success = true;
                // Extract just the "result" value, not the full response
                char* result_value = extract_raw_json_value(response, "result");
                if (result_value) {
                    result->data = result_value;
                    result->data_size = strlen(result_value);
                } else {
                    // Fallback to full response if extraction fails
                    result->data = response;
                    result->data_size = strlen(response);
                    response = NULL;  // Prevent free below
                }
                pthread_mutex_lock(&g_async.stats_mutex);
                g_async.stats.commands_completed++;
                pthread_mutex_unlock(&g_async.stats_mutex);
            }
            json_free(resp);
        } else {
            result->success = false;
            // Log the actual response that failed to parse for debugging
            int response_len = response ? (int)strlen(response) : 0;
            if (response_len > 0) {
                // Truncate response preview to first 200 chars
                int preview_len = response_len > 200 ? 200 : response_len;
                snprintf(result->error, sizeof(result->error),
                    "Invalid JSON response (len=%d): %.200s%s",
                    response_len, response, response_len > 200 ? "..." : "");
            } else {
                snprintf(result->error, sizeof(result->error), "Invalid JSON response (empty)");
            }
            pthread_mutex_lock(&g_async.stats_mutex);
            g_async.stats.commands_failed++;
            pthread_mutex_unlock(&g_async.stats_mutex);
        }

        if (response) free(response);
        return result->success ? 0 : -1;
    }
#endif

    // Fall back to pipe-based async IPC
    SyncCallState state = {
        .completed = false,
        .success = false,
        .result_json = NULL,
        .error = {0}
    };
    pthread_mutex_init(&state.mutex, NULL);
    pthread_cond_init(&state.cond, NULL);

    int req_id = browser_ipc_async_send(method, params_json,
                                         sync_callback, &state, 0);
    if (req_id < 0) {
        snprintf(result->error, sizeof(result->error), "Failed to send command");
        pthread_mutex_destroy(&state.mutex);
        pthread_cond_destroy(&state.cond);
        return -1;
    }

    // Wait for completion
    pthread_mutex_lock(&state.mutex);
    while (!state.completed) {
        pthread_cond_wait(&state.cond, &state.mutex);
    }
    pthread_mutex_unlock(&state.mutex);

    result->success = state.success;
    if (state.result_json) {
        result->data = state.result_json;
        result->data_size = strlen(state.result_json);
    }
    if (state.error[0]) {
        strncpy(result->error, state.error, sizeof(result->error) - 1);
    }

    pthread_mutex_destroy(&state.mutex);
    pthread_cond_destroy(&state.cond);

    return state.success ? 0 : -1;
}

void browser_ipc_async_get_stats(AsyncIPCStats* stats) {
    if (!stats) return;

    pthread_mutex_lock(&g_async.stats_mutex);
    memcpy(stats, &g_async.stats, sizeof(AsyncIPCStats));
    pthread_mutex_unlock(&g_async.stats_mutex);
}

bool browser_ipc_async_cancel(int request_id) {
    PendingRequest* req = find_and_remove_pending(request_id);
    if (req) {
        free(req);
        return true;
    }
    return false;
}

int browser_ipc_async_pending_count(void) {
    pthread_mutex_lock(&g_async.pending_mutex);
    int count = g_async.pending_count;
    pthread_mutex_unlock(&g_async.pending_mutex);
    return count;
}

// ============================================================================
// Multi-IPC API (Linux only)
// ============================================================================

bool browser_ipc_async_is_multi_ipc(void) {
#if MULTI_IPC_SUPPORTED
    return g_async.multi_ipc_available;
#else
    return false;
#endif
}

const char* browser_ipc_async_get_socket_path(void) {
#if MULTI_IPC_SUPPORTED
    return g_async.multi_ipc_available ? g_async.multi_ipc_socket_path : NULL;
#else
    return NULL;
#endif
}

int browser_ipc_async_get_connection_count(void) {
#if MULTI_IPC_SUPPORTED
    return g_async.multi_ipc_connected_count;
#else
    return 0;
#endif
}
