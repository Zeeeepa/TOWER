/**
 * Owl Browser HTTP Server - IPC Tests Implementation
 *
 * Runs the ipc_test_client binary and manages test reports.
 */

#include "ipc_tests.h"
#include "log.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>

// Module state
static struct {
    bool initialized;
    bool enabled;
    char test_client_path[IPC_TEST_MAX_PATH];
    char browser_path[IPC_TEST_MAX_PATH];
    char reports_dir[IPC_TEST_MAX_PATH];

    // Current/last run state
    pthread_mutex_t mutex;
    IpcTestResult current_result;
    pid_t test_pid;
    pthread_t monitor_thread;
    bool monitor_running;
} g_ipc_tests = {0};

// ============================================================================
// Helper Functions
// ============================================================================

static void generate_run_id(char* buf, size_t size) {
    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    snprintf(buf, size, "run_%04d%02d%02d_%02d%02d%02d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
}

static bool file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static bool ensure_dir_exists(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return mkdir(path, 0755) == 0;
}

static long get_file_size(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return st.st_size;
}

static char* read_file_content(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(content, 1, size, f);
    content[read] = '\0';
    fclose(f);

    return content;
}

// Parse summary from JSON report
static void parse_json_report_summary(const char* json_path, IpcTestResult* result) {
    char* content = read_file_content(json_path);
    if (!content) return;

    JsonValue* root = json_parse(content);
    free(content);
    if (!root) return;

    // Get summary section
    JsonValue* summary = json_object_get(root, "summary");
    if (summary && summary->type == JSON_OBJECT) {
        result->total_tests = (int)json_object_get_int(summary, "total_tests", 0);
        result->passed_tests = (int)json_object_get_int(summary, "passed", 0);
        result->failed_tests = (int)json_object_get_int(summary, "failed", 0);
        result->skipped_tests = (int)json_object_get_int(summary, "skipped", 0);
        result->duration_seconds = json_object_get_number(summary, "total_duration_sec", 0.0);
        result->commands_per_second = json_object_get_number(summary, "commands_per_second", 0.0);
    }

    json_free(root);
}

// ============================================================================
// Monitor Thread
// ============================================================================

static void* monitor_thread_func(void* arg) {
    (void)arg;

    LOG_DEBUG("IpcTests", "Monitor thread started for run %s", g_ipc_tests.current_result.run_id);

    int status;
    pid_t result;

    // Wait for the test process to complete
    do {
        result = waitpid(g_ipc_tests.test_pid, &status, 0);
    } while (result == -1 && errno == EINTR);

    pthread_mutex_lock(&g_ipc_tests.mutex);

    if (result == g_ipc_tests.test_pid) {
        if (WIFEXITED(status)) {
            g_ipc_tests.current_result.exit_code = WEXITSTATUS(status);
            if (g_ipc_tests.current_result.exit_code == 0) {
                g_ipc_tests.current_result.status = IPC_TEST_STATUS_COMPLETED;
                LOG_INFO("IpcTests", "Test run %s completed successfully",
                         g_ipc_tests.current_result.run_id);
            } else {
                g_ipc_tests.current_result.status = IPC_TEST_STATUS_FAILED;
                snprintf(g_ipc_tests.current_result.error_message,
                         sizeof(g_ipc_tests.current_result.error_message),
                         "Test exited with code %d", g_ipc_tests.current_result.exit_code);
                LOG_WARN("IpcTests", "Test run %s failed with exit code %d",
                         g_ipc_tests.current_result.run_id,
                         g_ipc_tests.current_result.exit_code);
            }
        } else if (WIFSIGNALED(status)) {
            g_ipc_tests.current_result.exit_code = -1;
            g_ipc_tests.current_result.status = IPC_TEST_STATUS_ABORTED;
            snprintf(g_ipc_tests.current_result.error_message,
                     sizeof(g_ipc_tests.current_result.error_message),
                     "Test killed by signal %d", WTERMSIG(status));
            LOG_WARN("IpcTests", "Test run %s killed by signal %d",
                     g_ipc_tests.current_result.run_id, WTERMSIG(status));
        }

        // Parse summary from JSON report if it exists
        if (file_exists(g_ipc_tests.current_result.json_report_path)) {
            parse_json_report_summary(g_ipc_tests.current_result.json_report_path,
                                     &g_ipc_tests.current_result);
        }
    } else {
        g_ipc_tests.current_result.status = IPC_TEST_STATUS_FAILED;
        snprintf(g_ipc_tests.current_result.error_message,
                 sizeof(g_ipc_tests.current_result.error_message),
                 "waitpid failed: %s", strerror(errno));
        LOG_ERROR("IpcTests", "waitpid failed for run %s: %s",
                  g_ipc_tests.current_result.run_id, strerror(errno));
    }

    g_ipc_tests.test_pid = 0;
    g_ipc_tests.monitor_running = false;

    pthread_mutex_unlock(&g_ipc_tests.mutex);

    LOG_DEBUG("IpcTests", "Monitor thread finished for run %s", g_ipc_tests.current_result.run_id);
    return NULL;
}

// ============================================================================
// Public API
// ============================================================================

int ipc_tests_init(const char* test_client_path, const char* browser_path,
                   const char* reports_dir) {
    if (g_ipc_tests.initialized) {
        LOG_WARN("IpcTests", "Already initialized");
        return 0;
    }

    // Check if test client exists
    if (!test_client_path || !file_exists(test_client_path)) {
        LOG_INFO("IpcTests", "IPC test client not found at %s, feature disabled",
                 test_client_path ? test_client_path : "(null)");
        g_ipc_tests.enabled = false;
        g_ipc_tests.initialized = true;
        return 0;
    }

    // Copy paths
    strncpy(g_ipc_tests.test_client_path, test_client_path,
            sizeof(g_ipc_tests.test_client_path) - 1);
    strncpy(g_ipc_tests.browser_path, browser_path,
            sizeof(g_ipc_tests.browser_path) - 1);
    strncpy(g_ipc_tests.reports_dir, reports_dir,
            sizeof(g_ipc_tests.reports_dir) - 1);

    // Ensure reports directory exists
    if (!ensure_dir_exists(reports_dir)) {
        LOG_ERROR("IpcTests", "Failed to create reports directory: %s", reports_dir);
        return -1;
    }

    // Initialize mutex
    pthread_mutex_init(&g_ipc_tests.mutex, NULL);

    g_ipc_tests.enabled = true;
    g_ipc_tests.initialized = true;

    LOG_INFO("IpcTests", "Initialized with client=%s, browser=%s, reports=%s",
             test_client_path, browser_path, reports_dir);

    return 0;
}

void ipc_tests_shutdown(void) {
    if (!g_ipc_tests.initialized) return;

    pthread_mutex_lock(&g_ipc_tests.mutex);

    // Kill any running test
    if (g_ipc_tests.test_pid > 0) {
        LOG_INFO("IpcTests", "Killing running test process %d", g_ipc_tests.test_pid);
        kill(g_ipc_tests.test_pid, SIGTERM);
        // Wait a bit then force kill
        usleep(100000); // 100ms
        kill(g_ipc_tests.test_pid, SIGKILL);
    }

    pthread_mutex_unlock(&g_ipc_tests.mutex);

    // Wait for monitor thread if running
    if (g_ipc_tests.monitor_running) {
        pthread_join(g_ipc_tests.monitor_thread, NULL);
    }

    pthread_mutex_destroy(&g_ipc_tests.mutex);

    g_ipc_tests.initialized = false;
    g_ipc_tests.enabled = false;

    LOG_INFO("IpcTests", "Shutdown complete");
}

bool ipc_tests_is_enabled(void) {
    return g_ipc_tests.enabled;
}

int ipc_tests_start(const IpcTestConfig* config, IpcTestResult* result) {
    if (!g_ipc_tests.enabled) {
        snprintf(result->error_message, sizeof(result->error_message),
                 "IPC tests feature is not enabled");
        return -1;
    }

    pthread_mutex_lock(&g_ipc_tests.mutex);

    // Check if a test is already running
    if (g_ipc_tests.test_pid > 0) {
        pthread_mutex_unlock(&g_ipc_tests.mutex);
        snprintf(result->error_message, sizeof(result->error_message),
                 "A test is already running (run_id: %s)", g_ipc_tests.current_result.run_id);
        return -1;
    }

    // Generate run ID
    generate_run_id(result->run_id, sizeof(result->run_id));

    // Build report paths
    snprintf(result->json_report_path, sizeof(result->json_report_path),
             "%s/%s.json", g_ipc_tests.reports_dir, result->run_id);
    snprintf(result->html_report_path, sizeof(result->html_report_path),
             "%s/%s.html", g_ipc_tests.reports_dir, result->run_id);

    // Build command line arguments
    char* argv[32];
    int argc = 0;

    argv[argc++] = g_ipc_tests.test_client_path;
    argv[argc++] = "--browser-path";
    argv[argc++] = g_ipc_tests.browser_path;

    // Mode
    argv[argc++] = "--mode";
    argv[argc++] = (char*)ipc_test_mode_to_string(config->mode);

    // Verbose
    if (config->verbose) {
        argv[argc++] = "--verbose";
    }

    // Mode-specific options
    char iterations_buf[32];
    char contexts_buf[32];
    char duration_buf[32];
    char concurrency_buf[32];

    switch (config->mode) {
        case IPC_TEST_MODE_BENCHMARK:
            if (config->iterations > 0) {
                snprintf(iterations_buf, sizeof(iterations_buf), "%d", config->iterations);
                argv[argc++] = "--iterations";
                argv[argc++] = iterations_buf;
            }
            break;

        case IPC_TEST_MODE_STRESS:
            if (config->contexts > 0) {
                snprintf(contexts_buf, sizeof(contexts_buf), "%d", config->contexts);
                argv[argc++] = "--contexts";
                argv[argc++] = contexts_buf;
            }
            if (config->duration_seconds > 0) {
                snprintf(duration_buf, sizeof(duration_buf), "%d", config->duration_seconds);
                argv[argc++] = "--duration";
                argv[argc++] = duration_buf;
            }
            break;

        case IPC_TEST_MODE_LEAK_CHECK:
            if (config->duration_seconds > 0) {
                snprintf(duration_buf, sizeof(duration_buf), "%d", config->duration_seconds);
                argv[argc++] = "--duration";
                argv[argc++] = duration_buf;
            }
            break;

        case IPC_TEST_MODE_PARALLEL:
            if (config->concurrency > 0) {
                snprintf(concurrency_buf, sizeof(concurrency_buf), "%d", config->concurrency);
                argv[argc++] = "--concurrency";
                argv[argc++] = concurrency_buf;
            }
            break;

        default:
            break;
    }

    // Output reports
    argv[argc++] = "--json-report";
    argv[argc++] = result->json_report_path;
    argv[argc++] = "--html-report";
    argv[argc++] = result->html_report_path;

    argv[argc] = NULL;

    // Log the command
    char cmd_log[2048] = {0};
    for (int i = 0; i < argc; i++) {
        if (i > 0) strcat(cmd_log, " ");
        strcat(cmd_log, argv[i]);
    }
    LOG_INFO("IpcTests", "Starting test: %s", cmd_log);

    // Fork and exec
    pid_t pid = fork();
    if (pid < 0) {
        pthread_mutex_unlock(&g_ipc_tests.mutex);
        snprintf(result->error_message, sizeof(result->error_message),
                 "fork() failed: %s", strerror(errno));
        LOG_ERROR("IpcTests", "fork() failed: %s", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        // Child process
        // Redirect stdout/stderr to /dev/null to avoid blocking
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execv(g_ipc_tests.test_client_path, argv);

        // If exec fails
        _exit(127);
    }

    // Parent process
    g_ipc_tests.test_pid = pid;
    result->status = IPC_TEST_STATUS_RUNNING;
    result->exit_code = -1;
    result->total_tests = 0;
    result->passed_tests = 0;
    result->failed_tests = 0;
    result->skipped_tests = 0;
    result->duration_seconds = 0;
    result->commands_per_second = 0;

    // Copy to current result
    memcpy(&g_ipc_tests.current_result, result, sizeof(IpcTestResult));

    // Start monitor thread
    g_ipc_tests.monitor_running = true;
    if (pthread_create(&g_ipc_tests.monitor_thread, NULL, monitor_thread_func, NULL) != 0) {
        LOG_ERROR("IpcTests", "Failed to create monitor thread");
        g_ipc_tests.monitor_running = false;
        // Continue anyway, status won't be updated automatically
    }

    pthread_mutex_unlock(&g_ipc_tests.mutex);

    LOG_INFO("IpcTests", "Test started with run_id=%s, pid=%d", result->run_id, pid);
    return 0;
}

int ipc_tests_get_status(const char* run_id, IpcTestResult* result) {
    pthread_mutex_lock(&g_ipc_tests.mutex);

    if (strcmp(g_ipc_tests.current_result.run_id, run_id) == 0) {
        memcpy(result, &g_ipc_tests.current_result, sizeof(IpcTestResult));
        pthread_mutex_unlock(&g_ipc_tests.mutex);
        return 0;
    }

    pthread_mutex_unlock(&g_ipc_tests.mutex);

    // Try to find the report on disk
    char json_path[IPC_TEST_MAX_PATH];
    snprintf(json_path, sizeof(json_path), "%s/%s.json", g_ipc_tests.reports_dir, run_id);

    if (!file_exists(json_path)) {
        return -1;
    }

    // Parse the report
    strncpy(result->run_id, run_id, sizeof(result->run_id) - 1);
    strncpy(result->json_report_path, json_path, sizeof(result->json_report_path) - 1);
    snprintf(result->html_report_path, sizeof(result->html_report_path),
             "%s/%s.html", g_ipc_tests.reports_dir, run_id);
    result->status = IPC_TEST_STATUS_COMPLETED;
    result->exit_code = 0;

    parse_json_report_summary(json_path, result);

    return 0;
}

int ipc_tests_get_current_status(IpcTestResult* result) {
    pthread_mutex_lock(&g_ipc_tests.mutex);

    if (g_ipc_tests.current_result.run_id[0] == '\0') {
        pthread_mutex_unlock(&g_ipc_tests.mutex);
        return -1;
    }

    memcpy(result, &g_ipc_tests.current_result, sizeof(IpcTestResult));
    pthread_mutex_unlock(&g_ipc_tests.mutex);
    return 0;
}

int ipc_tests_abort(const char* run_id) {
    pthread_mutex_lock(&g_ipc_tests.mutex);

    if (strcmp(g_ipc_tests.current_result.run_id, run_id) != 0) {
        pthread_mutex_unlock(&g_ipc_tests.mutex);
        return -1;
    }

    if (g_ipc_tests.test_pid <= 0) {
        pthread_mutex_unlock(&g_ipc_tests.mutex);
        return -1;
    }

    LOG_INFO("IpcTests", "Aborting test run %s (pid=%d)", run_id, g_ipc_tests.test_pid);

    // Send SIGTERM first
    kill(g_ipc_tests.test_pid, SIGTERM);

    pthread_mutex_unlock(&g_ipc_tests.mutex);

    // Wait a bit then force kill if still running
    usleep(500000); // 500ms

    pthread_mutex_lock(&g_ipc_tests.mutex);
    if (g_ipc_tests.test_pid > 0 && strcmp(g_ipc_tests.current_result.run_id, run_id) == 0) {
        kill(g_ipc_tests.test_pid, SIGKILL);
    }
    pthread_mutex_unlock(&g_ipc_tests.mutex);

    return 0;
}

int ipc_tests_list_reports(IpcTestReportInfo* reports, int max_reports) {
    if (!g_ipc_tests.enabled) return -1;

    DIR* dir = opendir(g_ipc_tests.reports_dir);
    if (!dir) return -1;

    int count = 0;
    struct dirent* entry;

    while ((entry = readdir(dir)) != NULL && count < max_reports) {
        // Look for .json files
        char* ext = strrchr(entry->d_name, '.');
        if (!ext || strcmp(ext, ".json") != 0) continue;

        // Extract run_id (filename without extension)
        char run_id[64];
        size_t name_len = ext - entry->d_name;
        if (name_len >= sizeof(run_id)) continue;
        strncpy(run_id, entry->d_name, name_len);
        run_id[name_len] = '\0';

        // Build paths
        char json_path[IPC_TEST_MAX_PATH];
        snprintf(json_path, sizeof(json_path), "%s/%s", g_ipc_tests.reports_dir, entry->d_name);

        // Parse the JSON to get metadata
        char* content = read_file_content(json_path);
        if (!content) continue;

        JsonValue* root = json_parse(content);
        free(content);
        if (!root) continue;

        // Fill report info
        IpcTestReportInfo* info = &reports[count];
        strncpy(info->run_id, run_id, sizeof(info->run_id) - 1);
        strncpy(info->json_report_path, json_path, sizeof(info->json_report_path) - 1);
        snprintf(info->html_report_path, sizeof(info->html_report_path),
                 "%s/%s.html", g_ipc_tests.reports_dir, run_id);

        // Get metadata
        JsonValue* metadata = json_object_get(root, "metadata");
        if (metadata && metadata->type == JSON_OBJECT) {
            const char* ts = json_object_get_string(metadata, "timestamp");
            if (ts) strncpy(info->timestamp, ts, sizeof(info->timestamp) - 1);

            const char* test_mode = json_object_get_string(metadata, "test_mode");
            if (test_mode) {
                strncpy(info->mode, test_mode, sizeof(info->mode) - 1);
            } else {
                strcpy(info->mode, "full"); // Default for older reports
            }
        } else {
            strcpy(info->mode, "full"); // Default
        }

        // Get summary
        JsonValue* summary = json_object_get(root, "summary");
        if (summary && summary->type == JSON_OBJECT) {
            info->total_tests = (int)json_object_get_int(summary, "total_tests", 0);
            info->passed_tests = (int)json_object_get_int(summary, "passed", 0);
            info->failed_tests = (int)json_object_get_int(summary, "failed", 0);
            info->duration_seconds = json_object_get_number(summary, "total_duration_sec", 0.0);
        }

        json_free(root);
        count++;
    }

    closedir(dir);
    return count;
}

int ipc_tests_get_json_report(const char* run_id, char** content) {
    char path[IPC_TEST_MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s.json", g_ipc_tests.reports_dir, run_id);

    *content = read_file_content(path);
    return *content ? 0 : -1;
}

int ipc_tests_get_html_report(const char* run_id, char** content) {
    char path[IPC_TEST_MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s.html", g_ipc_tests.reports_dir, run_id);

    *content = read_file_content(path);
    return *content ? 0 : -1;
}

int ipc_tests_delete_report(const char* run_id) {
    char json_path[IPC_TEST_MAX_PATH];
    char html_path[IPC_TEST_MAX_PATH];

    snprintf(json_path, sizeof(json_path), "%s/%s.json", g_ipc_tests.reports_dir, run_id);
    snprintf(html_path, sizeof(html_path), "%s/%s.html", g_ipc_tests.reports_dir, run_id);

    int result = 0;
    if (file_exists(json_path)) {
        if (unlink(json_path) != 0) result = -1;
    }
    if (file_exists(html_path)) {
        if (unlink(html_path) != 0) result = -1;
    }

    return result;
}

int ipc_tests_clean_all_reports(void) {
    if (!g_ipc_tests.enabled) return -1;

    DIR* dir = opendir(g_ipc_tests.reports_dir);
    if (!dir) return -1;

    int deleted_count = 0;
    struct dirent* entry;

    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Delete .json and .html files
        char* ext = strrchr(entry->d_name, '.');
        if (ext && (strcmp(ext, ".json") == 0 || strcmp(ext, ".html") == 0)) {
            char filepath[IPC_TEST_MAX_PATH];
            snprintf(filepath, sizeof(filepath), "%s/%s", g_ipc_tests.reports_dir, entry->d_name);
            if (unlink(filepath) == 0) {
                deleted_count++;
            }
        }
    }

    closedir(dir);

    LOG_INFO("IpcTests", "Cleaned %d report files", deleted_count);
    return deleted_count;
}

const char* ipc_test_mode_to_string(IpcTestMode mode) {
    switch (mode) {
        case IPC_TEST_MODE_SMOKE: return "smoke";
        case IPC_TEST_MODE_FULL: return "full";
        case IPC_TEST_MODE_BENCHMARK: return "benchmark";
        case IPC_TEST_MODE_STRESS: return "stress";
        case IPC_TEST_MODE_LEAK_CHECK: return "leak-check";
        case IPC_TEST_MODE_PARALLEL: return "parallel";
        default: return "full";
    }
}

IpcTestMode ipc_test_mode_from_string(const char* str) {
    if (!str) return IPC_TEST_MODE_FULL;
    if (strcmp(str, "smoke") == 0) return IPC_TEST_MODE_SMOKE;
    if (strcmp(str, "full") == 0) return IPC_TEST_MODE_FULL;
    if (strcmp(str, "benchmark") == 0) return IPC_TEST_MODE_BENCHMARK;
    if (strcmp(str, "stress") == 0) return IPC_TEST_MODE_STRESS;
    if (strcmp(str, "leak-check") == 0) return IPC_TEST_MODE_LEAK_CHECK;
    if (strcmp(str, "parallel") == 0) return IPC_TEST_MODE_PARALLEL;
    return IPC_TEST_MODE_FULL;
}

const char* ipc_test_status_to_string(IpcTestStatus status) {
    switch (status) {
        case IPC_TEST_STATUS_IDLE: return "idle";
        case IPC_TEST_STATUS_RUNNING: return "running";
        case IPC_TEST_STATUS_COMPLETED: return "completed";
        case IPC_TEST_STATUS_FAILED: return "failed";
        case IPC_TEST_STATUS_ABORTED: return "aborted";
        default: return "unknown";
    }
}
