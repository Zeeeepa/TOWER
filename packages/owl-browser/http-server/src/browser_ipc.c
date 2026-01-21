/**
 * Owl Browser HTTP Server - Browser IPC Implementation
 *
 * Manages browser process lifecycle and JSON command/response protocol.
 */

#include "browser_ipc.h"
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

// Global browser IPC instance
static BrowserIPC g_browser = {
    .pid = -1,
    .stdin_fd = -1,
    .stdout_fd = -1,
    .stderr_fd = -1,
    .state = BROWSER_STATE_STOPPED,
    .command_id = 0,
    .timeout_ms = DEFAULT_BROWSER_TIMEOUT_MS
};

static char g_browser_path[MAX_PATH_LENGTH] = {0};

// ============================================================================
// Internal Helpers
// ============================================================================

static void generate_instance_id(char* buf, size_t size) {
    uuid_t uuid;
    uuid_generate(uuid);
    char uuid_str[37];  // UUID string is 36 chars + null terminator
    uuid_unparse_lower(uuid, uuid_str);
    snprintf(buf, size, "http_server_%s", uuid_str);
}

static void* stderr_reader_thread(void* arg) {
    (void)arg;
    char buffer[4096];

    while (g_browser.stderr_thread_running && g_browser.stderr_fd >= 0) {
        struct pollfd pfd = { .fd = g_browser.stderr_fd, .events = POLLIN };
        int ret = poll(&pfd, 1, 100);  // 100ms timeout

        if (ret > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(g_browser.stderr_fd, buffer, sizeof(buffer) - 1);
            if (n > 0) {
                buffer[n] = '\0';

                // Log browser stderr
                LOG_DEBUG("Browser", "%s", buffer);

                // Check for license errors
                if (strstr(buffer, "LICENSE REQUIRED") ||
                    strstr(buffer, "License validation failed") ||
                    strstr(buffer, "license to run")) {

                    pthread_mutex_lock(&g_browser.mutex);
                    g_browser.state = BROWSER_STATE_LICENSE_ERROR;

                    // Extract status
                    char* status_ptr = strstr(buffer, "Status:");
                    if (status_ptr) {
                        sscanf(status_ptr, "Status: %63s", g_browser.license_error.status);
                    } else {
                        strcpy(g_browser.license_error.status, "not_found");
                    }

                    // Extract fingerprint
                    char* fp_ptr = strstr(buffer, "fingerprint:");
                    if (!fp_ptr) fp_ptr = strstr(buffer, "Fingerprint:");
                    if (fp_ptr) {
                        sscanf(fp_ptr, "%*[^:]: %127s", g_browser.license_error.fingerprint);
                    }

                    strncpy(g_browser.license_error.message,
                            "Browser requires a valid license. See browser logs for details.",
                            sizeof(g_browser.license_error.message) - 1);

                    pthread_mutex_unlock(&g_browser.mutex);
                }

                // Check for READY signal
                if (strstr(buffer, "READY")) {
                    pthread_mutex_lock(&g_browser.mutex);
                    if (g_browser.state == BROWSER_STATE_STARTING) {
                        g_browser.state = BROWSER_STATE_READY;
                        LOG_INFO("Browser", "Browser process is ready");
                    }
                    pthread_mutex_unlock(&g_browser.mutex);
                }
            } else if (n == 0) {
                // EOF
                break;
            }
        } else if (ret < 0 && errno != EINTR) {
            break;
        }
    }

    return NULL;
}

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
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

// ============================================================================
// Public Functions
// ============================================================================

int browser_ipc_init(void) {
    pthread_mutex_init(&g_browser.mutex, NULL);
    g_browser.state = BROWSER_STATE_STOPPED;
    g_browser.pid = -1;
    return 0;
}

void browser_ipc_shutdown(void) {
    browser_ipc_stop();
    pthread_mutex_destroy(&g_browser.mutex);
}

int browser_ipc_start(const char* browser_path, int timeout_ms) {
    if (!browser_path) {
        LOG_ERROR("Browser", "Browser path is NULL");
        return -1;
    }

    pthread_mutex_lock(&g_browser.mutex);

    if (g_browser.state != BROWSER_STATE_STOPPED) {
        pthread_mutex_unlock(&g_browser.mutex);
        LOG_WARN("Browser", "Browser already running");
        return 0;
    }

    strncpy(g_browser_path, browser_path, sizeof(g_browser_path) - 1);
    g_browser.timeout_ms = timeout_ms > 0 ? timeout_ms : DEFAULT_BROWSER_TIMEOUT_MS;
    g_browser.state = BROWSER_STATE_STARTING;

    // Generate instance ID
    generate_instance_id(g_browser.instance_id, sizeof(g_browser.instance_id));

    // Create pipes for stdin, stdout, stderr
    int stdin_pipe[2], stdout_pipe[2], stderr_pipe[2];

    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        LOG_ERROR("Browser", "Failed to create pipes: %s", strerror(errno));
        g_browser.state = BROWSER_STATE_ERROR;
        pthread_mutex_unlock(&g_browser.mutex);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("Browser", "Fork failed: %s", strerror(errno));
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        g_browser.state = BROWSER_STATE_ERROR;
        pthread_mutex_unlock(&g_browser.mutex);
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

        // Set instance ID environment variable
        setenv("OLIB_INSTANCE_ID", g_browser.instance_id, 1);

        // Execute browser
        execl(browser_path, browser_path, "--instance-id", g_browser.instance_id, NULL);

        // If exec fails
        fprintf(stderr, "Failed to execute browser: %s\n", strerror(errno));
        _exit(1);
    }

    // Parent process
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    g_browser.pid = pid;
    g_browser.stdin_fd = stdin_pipe[1];
    g_browser.stdout_fd = stdout_pipe[0];
    g_browser.stderr_fd = stderr_pipe[0];

    // Set stdout to non-blocking for reading
    set_nonblocking(g_browser.stdout_fd);

    // Start stderr reader thread
    g_browser.stderr_thread_running = true;
    if (pthread_create(&g_browser.stderr_thread, NULL, stderr_reader_thread, NULL) != 0) {
        LOG_WARN("Browser", "Failed to create stderr reader thread");
    }

    LOG_INFO("Browser", "Started browser process (PID: %d, Instance: %s)",
             g_browser.pid, g_browser.instance_id);

    pthread_mutex_unlock(&g_browser.mutex);

    // Wait for READY signal with timeout
    int wait_ms = 0;
    int init_timeout = 30000;  // 30 second init timeout

    while (wait_ms < init_timeout) {
        pthread_mutex_lock(&g_browser.mutex);
        BrowserState state = g_browser.state;
        pthread_mutex_unlock(&g_browser.mutex);

        if (state == BROWSER_STATE_READY) {
            return 0;
        }

        if (state == BROWSER_STATE_LICENSE_ERROR) {
            LOG_ERROR("Browser", "License error detected");
            return -1;
        }

        if (state == BROWSER_STATE_ERROR) {
            LOG_ERROR("Browser", "Browser failed to start");
            return -1;
        }

        // Check if process is still running
        int status;
        pid_t result = waitpid(g_browser.pid, &status, WNOHANG);
        if (result > 0) {
            pthread_mutex_lock(&g_browser.mutex);
            g_browser.state = BROWSER_STATE_ERROR;
            pthread_mutex_unlock(&g_browser.mutex);
            LOG_ERROR("Browser", "Browser process exited unexpectedly (status: %d)", status);
            return -1;
        }

        usleep(100000);  // 100ms
        wait_ms += 100;
    }

    LOG_ERROR("Browser", "Timeout waiting for browser to start");
    browser_ipc_stop();
    return -1;
}

void browser_ipc_stop(void) {
    pthread_mutex_lock(&g_browser.mutex);

    if (g_browser.state == BROWSER_STATE_STOPPED) {
        pthread_mutex_unlock(&g_browser.mutex);
        return;
    }

    LOG_INFO("Browser", "Stopping browser process (PID: %d)", g_browser.pid);

    // Stop stderr thread
    g_browser.stderr_thread_running = false;

    // Try graceful shutdown first
    if (g_browser.stdin_fd >= 0) {
        const char* shutdown_cmd = "{\"id\":0,\"method\":\"shutdown\"}\n";
        write(g_browser.stdin_fd, shutdown_cmd, strlen(shutdown_cmd));
    }

    // Close file descriptors
    if (g_browser.stdin_fd >= 0) {
        close(g_browser.stdin_fd);
        g_browser.stdin_fd = -1;
    }
    if (g_browser.stdout_fd >= 0) {
        close(g_browser.stdout_fd);
        g_browser.stdout_fd = -1;
    }
    if (g_browser.stderr_fd >= 0) {
        close(g_browser.stderr_fd);
        g_browser.stderr_fd = -1;
    }

    // Wait for process to exit
    if (g_browser.pid > 0) {
        int status;
        int wait_count = 0;
        while (waitpid(g_browser.pid, &status, WNOHANG) == 0 && wait_count < 30) {
            usleep(100000);  // 100ms
            wait_count++;
        }

        // Force kill if still running
        if (wait_count >= 30) {
            LOG_WARN("Browser", "Browser didn't exit gracefully, killing...");
            kill(g_browser.pid, SIGKILL);
            waitpid(g_browser.pid, &status, 0);
        }
    }

    g_browser.pid = -1;
    g_browser.state = BROWSER_STATE_STOPPED;
    g_browser.command_id = 0;

    // Join stderr thread
    pthread_mutex_unlock(&g_browser.mutex);

    if (g_browser.stderr_thread) {
        pthread_join(g_browser.stderr_thread, NULL);
    }

    LOG_INFO("Browser", "Browser stopped");
}

bool browser_ipc_is_ready(void) {
    pthread_mutex_lock(&g_browser.mutex);
    bool ready = (g_browser.state == BROWSER_STATE_READY);
    pthread_mutex_unlock(&g_browser.mutex);
    return ready;
}

BrowserState browser_ipc_get_state(void) {
    pthread_mutex_lock(&g_browser.mutex);
    BrowserState state = g_browser.state;
    pthread_mutex_unlock(&g_browser.mutex);
    return state;
}

const LicenseError* browser_ipc_get_license_error(void) {
    return &g_browser.license_error;
}

int browser_ipc_send_command(const char* method, const char* params_json,
                             OperationResult* result) {
    if (!method || !result) return -1;

    memset(result, 0, sizeof(*result));

    pthread_mutex_lock(&g_browser.mutex);

    if (g_browser.state != BROWSER_STATE_READY) {
        pthread_mutex_unlock(&g_browser.mutex);
        snprintf(result->error, sizeof(result->error), "Browser not ready");
        return -1;
    }

    // Generate command ID
    int cmd_id = ++g_browser.command_id;

    // Build command JSON
    JsonBuilder builder;
    json_builder_init(&builder);

    json_builder_object_start(&builder);
    json_builder_key(&builder, "id");
    json_builder_int(&builder, cmd_id);
    json_builder_comma(&builder);
    json_builder_key(&builder, "method");
    json_builder_string(&builder, method);

    // Merge params if provided
    if (params_json && strlen(params_json) > 2) {  // More than "{}"
        // Parse params and add each key-value pair
        JsonValue* params = json_parse(params_json);
        if (params && params->type == JSON_OBJECT && params->object_val) {
            JsonPair* pair = params->object_val->pairs;
            while (pair) {
                json_builder_comma(&builder);
                json_builder_key(&builder, pair->key);

                // Output value based on type
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
                        // For arrays/objects, this is simplified
                        json_builder_null(&builder);
                        break;
                }
                pair = pair->next;
            }
            json_free(params);
        }
    }

    json_builder_object_end(&builder);

    char* cmd_str = json_builder_finish(&builder);
    size_t cmd_len = strlen(cmd_str);

    // Send command
    LOG_DEBUG("Browser", "Sending command: %s", cmd_str);

    char* cmd_with_newline = malloc(cmd_len + 2);
    snprintf(cmd_with_newline, cmd_len + 2, "%s\n", cmd_str);
    free(cmd_str);

    ssize_t written = write(g_browser.stdin_fd, cmd_with_newline, strlen(cmd_with_newline));
    free(cmd_with_newline);

    if (written < 0) {
        pthread_mutex_unlock(&g_browser.mutex);
        snprintf(result->error, sizeof(result->error), "Failed to send command: %s", strerror(errno));
        return -1;
    }

    int timeout_ms = g_browser.timeout_ms;
    int stdout_fd = g_browser.stdout_fd;

    pthread_mutex_unlock(&g_browser.mutex);

    // Read response with timeout
    char response_buf[65536];
    size_t response_len = 0;
    int elapsed_ms = 0;

    while (elapsed_ms < timeout_ms) {
        struct pollfd pfd = { .fd = stdout_fd, .events = POLLIN };
        int ret = poll(&pfd, 1, 100);  // 100ms poll

        if (ret > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(stdout_fd, response_buf + response_len,
                            sizeof(response_buf) - response_len - 1);
            if (n > 0) {
                response_len += n;
                response_buf[response_len] = '\0';

                // Check if we have a complete line (ends with newline)
                char* newline = strchr(response_buf, '\n');
                if (newline) {
                    *newline = '\0';

                    // Parse response
                    JsonValue* resp = json_parse(response_buf);
                    if (resp) {
                        int resp_id = (int)json_object_get_int(resp, "id", -1);

                        if (resp_id == cmd_id) {
                            // Check for error
                            const char* error = json_object_get_string(resp, "error");
                            if (error) {
                                strncpy(result->error, error, sizeof(result->error) - 1);
                                result->success = false;
                            } else {
                                // Get result
                                JsonValue* res_val = json_object_get(resp, "result");
                                if (res_val) {
                                    // Convert result back to JSON string
                                    JsonBuilder res_builder;
                                    json_builder_init(&res_builder);

                                    switch (res_val->type) {
                                        case JSON_STRING:
                                            json_builder_string(&res_builder, res_val->string_val);
                                            break;
                                        case JSON_NUMBER:
                                            json_builder_number(&res_builder, res_val->number_val);
                                            break;
                                        case JSON_BOOL:
                                            json_builder_bool(&res_builder, res_val->bool_val);
                                            break;
                                        case JSON_NULL:
                                            json_builder_null(&res_builder);
                                            break;
                                        default: {
                                            // For complex types (objects/arrays), extract just the "result" value
                                            char* result_json = extract_raw_json_value(response_buf, "result");
                                            if (result_json) {
                                                json_builder_append(&res_builder, result_json);
                                                free(result_json);
                                            }
                                            break;
                                        }
                                    }

                                    result->data = json_builder_finish(&res_builder);
                                    result->data_size = strlen(result->data);
                                }
                                result->success = true;
                            }

                            json_free(resp);
                            return 0;
                        }
                    }

                    // Response for different command, continue waiting
                    // Move remaining data to start of buffer
                    size_t remaining = response_len - (newline - response_buf) - 1;
                    if (remaining > 0) {
                        memmove(response_buf, newline + 1, remaining);
                        response_len = remaining;
                    } else {
                        response_len = 0;
                    }
                }
            } else if (n == 0) {
                snprintf(result->error, sizeof(result->error), "Browser connection closed");
                return -1;
            }
        } else if (ret < 0 && errno != EINTR) {
            snprintf(result->error, sizeof(result->error), "Poll error: %s", strerror(errno));
            return -1;
        }

        elapsed_ms += 100;
    }

    snprintf(result->error, sizeof(result->error), "Command timeout after %d ms", timeout_ms);
    return -1;
}

int browser_ipc_send_raw(const char* json_command, OperationResult* result) {
    // Parse the raw JSON to extract method and build params
    JsonValue* cmd = json_parse(json_command);
    if (!cmd) {
        snprintf(result->error, sizeof(result->error), "Invalid JSON command");
        return -1;
    }

    const char* method = json_object_get_string(cmd, "method");
    if (!method) {
        json_free(cmd);
        snprintf(result->error, sizeof(result->error), "Missing method in command");
        return -1;
    }

    // Rebuild params JSON (everything except id and method)
    // For simplicity, just pass the original JSON and let browser handle it
    int ret = browser_ipc_send_command(method, json_command, result);

    json_free(cmd);
    return ret;
}

int browser_ipc_restart(const char* browser_path, int timeout_ms) {
    browser_ipc_stop();
    usleep(500000);  // 500ms delay
    return browser_ipc_start(browser_path, timeout_ms);
}
