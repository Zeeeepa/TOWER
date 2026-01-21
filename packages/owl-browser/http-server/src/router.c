/**
 * Owl Browser HTTP Server - Router Implementation
 *
 * Routes HTTP requests to appropriate handlers and maps
 * REST endpoints to browser commands.
 */

#include "router.h"
#include "tools.h"
#include "json.h"
#include "auth.h"
#include "browser_ipc.h"
#include "browser_ipc_async.h"
#include "http_server.h"
#include "rate_limit.h"
#include "ip_filter.h"
#include "video_stream.h"
#include "license_manager.h"
#include "ipc_tests.h"
#include "playground.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

// Use async IPC if available (set via router_init)
static bool g_use_async_ipc = false;

static const ServerConfig* g_config = NULL;

// ============================================================================
// Tool to Browser Method Mapping (with hash table for O(1) lookup)
// ============================================================================

typedef struct {
    const char* tool_name;
    const char* browser_method;
} ToolMethodMapping;

// Hash table for fast tool lookup
#define TOOL_HASH_SIZE 256
static const ToolMethodMapping* g_tool_hash[TOOL_HASH_SIZE];
static bool g_tool_hash_initialized = false;

// Simple djb2 hash function
static unsigned int tool_hash(const char* str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % TOOL_HASH_SIZE;
}

static const ToolMethodMapping TOOL_MAPPINGS[] = {
    // Context Management
    {TOOL_CREATE_CONTEXT, "createContext"},
    {TOOL_CLOSE_CONTEXT, "closeContext"},
    {TOOL_LIST_CONTEXTS, "listContexts"},

    // Navigation
    {TOOL_NAVIGATE, "navigate"},
    {TOOL_RELOAD, "reload"},
    {TOOL_GO_BACK, "goBack"},
    {TOOL_GO_FORWARD, "goForward"},
    {TOOL_CAN_GO_BACK, "canGoBack"},
    {TOOL_CAN_GO_FORWARD, "canGoForward"},

    // Interaction
    {TOOL_CLICK, "click"},
    {TOOL_TYPE, "type"},
    {TOOL_PICK, "pick"},
    {TOOL_PRESS_KEY, "pressKey"},
    {TOOL_SUBMIT_FORM, "submitForm"},
    {TOOL_DRAG_DROP, "dragDrop"},
    {TOOL_HTML5_DRAG_DROP, "html5DragDrop"},
    {TOOL_MOUSE_MOVE, "mouseMove"},
    {TOOL_HOVER, "hover"},
    {TOOL_DOUBLE_CLICK, "doubleClick"},
    {TOOL_RIGHT_CLICK, "rightClick"},
    {TOOL_CLEAR_INPUT, "clearInput"},
    {TOOL_FOCUS, "focus"},
    {TOOL_BLUR, "blur"},
    {TOOL_SELECT_ALL, "selectAll"},
    {TOOL_KEYBOARD_COMBO, "keyboardCombo"},
    {TOOL_UPLOAD_FILE, "uploadFile"},

    // Element State Checks
    {TOOL_IS_VISIBLE, "isVisible"},
    {TOOL_IS_ENABLED, "isEnabled"},
    {TOOL_IS_CHECKED, "isChecked"},
    {TOOL_GET_ATTRIBUTE, "getAttribute"},
    {TOOL_GET_BOUNDING_BOX, "getBoundingBox"},

    // JavaScript Evaluation
    {TOOL_EVALUATE, "evaluate"},

    // Frame Handling
    {TOOL_LIST_FRAMES, "listFrames"},
    {TOOL_SWITCH_TO_FRAME, "switchToFrame"},
    {TOOL_SWITCH_TO_MAIN_FRAME, "switchToMainFrame"},

    // Content Extraction
    {TOOL_EXTRACT_TEXT, "extractText"},
    {TOOL_SCREENSHOT, "screenshot"},
    {TOOL_HIGHLIGHT, "highlight"},
    {TOOL_SHOW_GRID_OVERLAY, "showGridOverlay"},
    {TOOL_GET_HTML, "getHTML"},
    {TOOL_GET_MARKDOWN, "getMarkdown"},
    {TOOL_EXTRACT_JSON, "extractJSON"},
    {TOOL_DETECT_SITE, "detectWebsiteType"},
    {TOOL_LIST_TEMPLATES, "listTemplates"},

    // AI/LLM Features
    {TOOL_SUMMARIZE_PAGE, "summarizePage"},
    {TOOL_QUERY_PAGE, "queryPage"},
    {TOOL_LLM_STATUS, "getLLMStatus"},
    {TOOL_NLA, "executeNLA"},
    {TOOL_AI_CLICK, "aiClick"},
    {TOOL_AI_TYPE, "aiType"},
    {TOOL_AI_EXTRACT, "aiExtract"},
    {TOOL_AI_QUERY, "aiQuery"},
    {TOOL_AI_ANALYZE, "aiAnalyze"},
    {TOOL_FIND_ELEMENT, "findElement"},

    // Scroll Control
    {TOOL_SCROLL_BY, "scrollBy"},
    {TOOL_SCROLL_TO_ELEMENT, "scrollToElement"},
    {TOOL_SCROLL_TO_TOP, "scrollToTop"},
    {TOOL_SCROLL_TO_BOTTOM, "scrollToBottom"},

    // Wait Utilities
    {TOOL_WAIT_FOR_SELECTOR, "waitForSelector"},
    {TOOL_WAIT, "waitForTimeout"},
    {TOOL_WAIT_FOR_NETWORK_IDLE, "waitForNetworkIdle"},
    {TOOL_WAIT_FOR_FUNCTION, "waitForFunction"},
    {TOOL_WAIT_FOR_URL, "waitForURL"},

    // Page Info
    {TOOL_GET_PAGE_INFO, "getPageInfo"},
    {TOOL_SET_VIEWPORT, "setViewport"},

    // DOM Zoom
    {TOOL_ZOOM_IN, "zoomIn"},
    {TOOL_ZOOM_OUT, "zoomOut"},
    {TOOL_ZOOM_RESET, "zoomReset"},

    // Console Logs
    {TOOL_GET_CONSOLE_LOG, "getConsoleLogs"},
    {TOOL_CLEAR_CONSOLE_LOG, "clearConsoleLogs"},

    // Video Recording
    {TOOL_START_VIDEO, "startVideoRecording"},
    {TOOL_PAUSE_VIDEO, "pauseVideoRecording"},
    {TOOL_RESUME_VIDEO, "resumeVideoRecording"},
    {TOOL_STOP_VIDEO, "stopVideoRecording"},
    {TOOL_GET_VIDEO_STATS, "getVideoRecordingStats"},

    // Live Video Streaming
    {TOOL_START_LIVE_STREAM, "startLiveStream"},
    {TOOL_STOP_LIVE_STREAM, "stopLiveStream"},
    {TOOL_GET_LIVE_STREAM_STATS, "getLiveStreamStats"},
    {TOOL_LIST_LIVE_STREAMS, "listLiveStreams"},
    {TOOL_GET_LIVE_FRAME, "getLiveFrame"},

    // Demographics
    {TOOL_GET_DEMOGRAPHICS, "getDemographics"},
    {TOOL_GET_LOCATION, "getLocation"},
    {TOOL_GET_DATETIME, "getDateTime"},
    {TOOL_GET_WEATHER, "getWeather"},

    // CAPTCHA
    {TOOL_DETECT_CAPTCHA, "detectCaptcha"},
    {TOOL_CLASSIFY_CAPTCHA, "classifyCaptcha"},
    {TOOL_SOLVE_TEXT_CAPTCHA, "solveTextCaptcha"},
    {TOOL_SOLVE_IMAGE_CAPTCHA, "solveImageCaptcha"},
    {TOOL_SOLVE_CAPTCHA, "solveCaptcha"},

    // Cookies
    {TOOL_GET_COOKIES, "getCookies"},
    {TOOL_SET_COOKIE, "setCookie"},
    {TOOL_DELETE_COOKIES, "deleteCookies"},

    // Proxy
    {TOOL_SET_PROXY, "setProxy"},
    {TOOL_GET_PROXY_STATUS, "getProxyStatus"},
    {TOOL_CONNECT_PROXY, "connectProxy"},
    {TOOL_DISCONNECT_PROXY, "disconnectProxy"},

    // Profile
    {TOOL_CREATE_PROFILE, "createProfile"},
    {TOOL_LOAD_PROFILE, "loadProfile"},
    {TOOL_SAVE_PROFILE, "saveProfile"},
    {TOOL_GET_PROFILE, "getProfile"},
    {TOOL_UPDATE_PROFILE_COOKIES, "updateProfileCookies"},
    {TOOL_GET_CONTEXT_INFO, "getContextInfo"},

    // Clipboard
    {TOOL_CLIPBOARD_READ, "clipboardRead"},
    {TOOL_CLIPBOARD_WRITE, "clipboardWrite"},
    {TOOL_CLIPBOARD_CLEAR, "clipboardClear"},

    // License Management
    {TOOL_GET_LICENSE_STATUS, "getLicenseStatus"},
    {TOOL_GET_LICENSE_INFO, "getLicenseInfo"},
    {TOOL_GET_HARDWARE_FINGERPRINT, "getHardwareFingerprint"},
    {TOOL_ADD_LICENSE, "addLicense"},
    {TOOL_REMOVE_LICENSE, "removeLicense"},

    // Element Picker (for UI overlays)
    {TOOL_GET_ELEMENT_AT_POSITION, "getElementAtPosition"},
    {TOOL_GET_INTERACTIVE_ELEMENTS, "getInteractiveElements"},
    {TOOL_GET_BLOCKER_STATS, "getBlockerStats"},

    // Network Interception
    {TOOL_ADD_NETWORK_RULE, "addNetworkRule"},
    {TOOL_REMOVE_NETWORK_RULE, "removeNetworkRule"},
    {TOOL_ENABLE_NETWORK_INTERCEPTION, "enableNetworkInterception"},
    {TOOL_ENABLE_NETWORK_LOGGING, "enableNetworkLogging"},
    {TOOL_GET_NETWORK_LOG, "getNetworkLog"},
    {TOOL_CLEAR_NETWORK_LOG, "clearNetworkLog"},

    // File Downloads
    {TOOL_SET_DOWNLOAD_PATH, "setDownloadPath"},
    {TOOL_GET_DOWNLOADS, "getDownloads"},
    {TOOL_GET_ACTIVE_DOWNLOADS, "getActiveDownloads"},
    {TOOL_WAIT_FOR_DOWNLOAD, "waitForDownload"},
    {TOOL_CANCEL_DOWNLOAD, "cancelDownload"},

    // Dialog Handling
    {TOOL_SET_DIALOG_ACTION, "setDialogAction"},
    {TOOL_GET_PENDING_DIALOG, "getPendingDialog"},
    {TOOL_GET_DIALOGS, "getDialogs"},
    {TOOL_HANDLE_DIALOG, "handleDialog"},
    {TOOL_WAIT_FOR_DIALOG, "waitForDialog"},

    // Tab/Window Management
    {TOOL_SET_POPUP_POLICY, "setPopupPolicy"},
    {TOOL_GET_TABS, "getTabs"},
    {TOOL_SWITCH_TAB, "switchTab"},
    {TOOL_CLOSE_TAB, "closeTab"},
    {TOOL_NEW_TAB, "newTab"},
    {TOOL_GET_ACTIVE_TAB, "getActiveTab"},
    {TOOL_GET_TAB_COUNT, "getTabCount"},
    {TOOL_GET_BLOCKED_POPUPS, "getBlockedPopups"},

    {NULL, NULL}
};

// Initialize the hash table on first use
static void init_tool_hash(void) {
    if (g_tool_hash_initialized) return;

    // Clear the hash table
    for (int i = 0; i < TOOL_HASH_SIZE; i++) {
        g_tool_hash[i] = NULL;
    }

    // Insert all tools (using linear probing for collisions)
    for (int i = 0; TOOL_MAPPINGS[i].tool_name; i++) {
        unsigned int idx = tool_hash(TOOL_MAPPINGS[i].tool_name);
        // Linear probing to find empty slot
        while (g_tool_hash[idx] != NULL) {
            idx = (idx + 1) % TOOL_HASH_SIZE;
        }
        g_tool_hash[idx] = &TOOL_MAPPINGS[i];
    }

    g_tool_hash_initialized = true;
}

static const char* get_browser_method(const char* tool_name) {
    if (!g_tool_hash_initialized) {
        init_tool_hash();
    }

    unsigned int idx = tool_hash(tool_name);
    unsigned int start = idx;

    // Linear probing to find the tool
    while (g_tool_hash[idx] != NULL) {
        if (strcmp(g_tool_hash[idx]->tool_name, tool_name) == 0) {
            return g_tool_hash[idx]->browser_method;
        }
        idx = (idx + 1) % TOOL_HASH_SIZE;
        if (idx == start) break;  // Full loop, not found
    }

    return NULL;
}

// ============================================================================
// Response Helpers
// ============================================================================

static void set_json_response(HttpResponse* response, HttpStatus status, char* json) {
    response->status = status;
    strcpy(response->content_type, "application/json");
    response->body = json;
    response->body_size = strlen(json);
    response->owns_body = true;
}

static void set_error_response(HttpResponse* response, HttpStatus status,
                               const char* error_msg) {
    char* json = json_error_response(error_msg);
    set_json_response(response, status, json);
}

// ============================================================================
// Route Handlers
// ============================================================================

static int handle_health(const HttpRequest* request, HttpResponse* response) {
    (void)request;

    JsonBuilder builder;
    json_builder_init(&builder);

    json_builder_object_start(&builder);
    json_builder_key(&builder, "status");
    json_builder_string(&builder, "healthy");
    json_builder_comma(&builder);
    json_builder_key(&builder, "browser_ready");

    bool browser_ready;
    const char* state_str;

    if (g_use_async_ipc) {
        browser_ready = browser_ipc_async_is_ready();
        json_builder_bool(&builder, browser_ready);
        json_builder_comma(&builder);
        json_builder_key(&builder, "browser_state");

        AsyncBrowserState state = browser_ipc_async_get_state();
        switch (state) {
            case ASYNC_BROWSER_STOPPED: state_str = "stopped"; break;
            case ASYNC_BROWSER_STARTING: state_str = "starting"; break;
            case ASYNC_BROWSER_READY: state_str = "ready"; break;
            case ASYNC_BROWSER_ERROR: state_str = "error"; break;
            case ASYNC_BROWSER_LICENSE_ERROR: state_str = "license_error"; break;
            default: state_str = "unknown";
        }
    } else {
        browser_ready = browser_ipc_is_ready();
        json_builder_bool(&builder, browser_ready);
        json_builder_comma(&builder);
        json_builder_key(&builder, "browser_state");

        BrowserState state = browser_ipc_get_state();
        switch (state) {
            case BROWSER_STATE_STOPPED: state_str = "stopped"; break;
            case BROWSER_STATE_STARTING: state_str = "starting"; break;
            case BROWSER_STATE_READY: state_str = "ready"; break;
            case BROWSER_STATE_ERROR: state_str = "error"; break;
            case BROWSER_STATE_LICENSE_ERROR: state_str = "license_error"; break;
            default: state_str = "unknown";
        }
    }
    json_builder_string(&builder, state_str);

    // Add async IPC stats if enabled
    if (g_use_async_ipc) {
        AsyncIPCStats stats;
        browser_ipc_async_get_stats(&stats);
        json_builder_comma(&builder);
        json_builder_key(&builder, "ipc_stats");
        json_builder_object_start(&builder);
        json_builder_key(&builder, "pending_commands");
        json_builder_number(&builder, stats.pending_count);
        json_builder_comma(&builder);
        json_builder_key(&builder, "commands_completed");
        json_builder_number(&builder, stats.commands_completed);
        json_builder_comma(&builder);
        json_builder_key(&builder, "commands_failed");
        json_builder_number(&builder, stats.commands_failed);
        json_builder_object_end(&builder);
    }

    json_builder_object_end(&builder);

    set_json_response(response, HTTP_200_OK, json_builder_finish(&builder));
    return 0;
}

static int handle_stats(const HttpRequest* request, HttpResponse* response) {
    (void)request;

    ServerStats stats;
    http_server_get_stats(&stats);

    JsonBuilder builder;
    json_builder_init(&builder);

    json_builder_object_start(&builder);

    // Server info
    json_builder_key(&builder, "uptime_seconds");
    json_builder_int(&builder, stats.uptime_seconds);
    json_builder_comma(&builder);

    // Request counters
    json_builder_key(&builder, "requests");
    json_builder_object_start(&builder);
    json_builder_key(&builder, "total");
    json_builder_int(&builder, stats.requests_total);
    json_builder_comma(&builder);
    json_builder_key(&builder, "success");
    json_builder_int(&builder, stats.requests_success);
    json_builder_comma(&builder);
    json_builder_key(&builder, "error");
    json_builder_int(&builder, stats.requests_error);
    json_builder_comma(&builder);
    json_builder_key(&builder, "per_second");
    json_builder_raw(&builder, "");  // Placeholder for proper float formatting
    {
        char rps_buf[64];
        snprintf(rps_buf, sizeof(rps_buf), "%.2f", stats.requests_per_second);
        json_builder_raw(&builder, rps_buf);
    }
    json_builder_object_end(&builder);
    json_builder_comma(&builder);

    // Concurrency metrics
    json_builder_key(&builder, "concurrency");
    json_builder_object_start(&builder);
    json_builder_key(&builder, "current");
    json_builder_int(&builder, stats.requests_concurrent_current);
    json_builder_comma(&builder);
    json_builder_key(&builder, "peak");
    json_builder_int(&builder, stats.requests_concurrent_peak);
    json_builder_comma(&builder);
    json_builder_key(&builder, "active_connections");
    json_builder_number(&builder, stats.active_connections);
    json_builder_object_end(&builder);
    json_builder_comma(&builder);

    // Thread pool metrics
    json_builder_key(&builder, "thread_pool");
    json_builder_object_start(&builder);
    json_builder_key(&builder, "num_threads");
    json_builder_number(&builder, stats.thread_pool_num_threads);
    json_builder_comma(&builder);
    json_builder_key(&builder, "active_workers");
    json_builder_number(&builder, stats.thread_pool_active_workers);
    json_builder_comma(&builder);
    json_builder_key(&builder, "pending_tasks");
    json_builder_number(&builder, stats.thread_pool_pending_tasks);
    json_builder_comma(&builder);
    json_builder_key(&builder, "tasks_completed");
    json_builder_int(&builder, stats.thread_pool_tasks_completed);
    json_builder_object_end(&builder);
    json_builder_comma(&builder);

    // Latency metrics (convert from microseconds to milliseconds for display)
    json_builder_key(&builder, "latency_ms");
    json_builder_object_start(&builder);
    if (stats.latency_count > 0) {
        double avg_latency_ms = (double)stats.latency_total_us / stats.latency_count / 1000.0;
        json_builder_key(&builder, "avg");
        {
            char lat_buf[64];
            snprintf(lat_buf, sizeof(lat_buf), "%.3f", avg_latency_ms);
            json_builder_raw(&builder, lat_buf);
        }
        json_builder_comma(&builder);
        json_builder_key(&builder, "min");
        {
            char lat_buf[64];
            snprintf(lat_buf, sizeof(lat_buf), "%.3f", (double)stats.latency_min_us / 1000.0);
            json_builder_raw(&builder, lat_buf);
        }
        json_builder_comma(&builder);
        json_builder_key(&builder, "max");
        {
            char lat_buf[64];
            snprintf(lat_buf, sizeof(lat_buf), "%.3f", (double)stats.latency_max_us / 1000.0);
            json_builder_raw(&builder, lat_buf);
        }
        json_builder_comma(&builder);
        json_builder_key(&builder, "samples");
        json_builder_int(&builder, stats.latency_count);
    } else {
        json_builder_key(&builder, "avg");
        json_builder_number(&builder, 0);
        json_builder_comma(&builder);
        json_builder_key(&builder, "min");
        json_builder_number(&builder, 0);
        json_builder_comma(&builder);
        json_builder_key(&builder, "max");
        json_builder_number(&builder, 0);
        json_builder_comma(&builder);
        json_builder_key(&builder, "samples");
        json_builder_number(&builder, 0);
    }
    json_builder_object_end(&builder);
    json_builder_comma(&builder);

    // Bandwidth metrics
    json_builder_key(&builder, "bandwidth");
    json_builder_object_start(&builder);
    json_builder_key(&builder, "bytes_received");
    json_builder_int(&builder, stats.bytes_received);
    json_builder_comma(&builder);
    json_builder_key(&builder, "bytes_sent");
    json_builder_int(&builder, stats.bytes_sent);
    json_builder_comma(&builder);
    json_builder_key(&builder, "in_bytes_per_second");
    {
        char bw_buf[64];
        snprintf(bw_buf, sizeof(bw_buf), "%.2f", stats.bytes_per_second_in);
        json_builder_raw(&builder, bw_buf);
    }
    json_builder_comma(&builder);
    json_builder_key(&builder, "out_bytes_per_second");
    {
        char bw_buf[64];
        snprintf(bw_buf, sizeof(bw_buf), "%.2f", stats.bytes_per_second_out);
        json_builder_raw(&builder, bw_buf);
    }
    json_builder_object_end(&builder);

    // Add async IPC stats if enabled
    if (g_use_async_ipc) {
        json_builder_comma(&builder);
        json_builder_key(&builder, "ipc");
        json_builder_object_start(&builder);
        AsyncIPCStats ipc_stats;
        browser_ipc_async_get_stats(&ipc_stats);
        json_builder_key(&builder, "pending_commands");
        json_builder_number(&builder, ipc_stats.pending_count);
        json_builder_comma(&builder);
        json_builder_key(&builder, "commands_sent");
        json_builder_int(&builder, ipc_stats.commands_sent);
        json_builder_comma(&builder);
        json_builder_key(&builder, "commands_completed");
        json_builder_int(&builder, ipc_stats.commands_completed);
        json_builder_comma(&builder);
        json_builder_key(&builder, "commands_failed");
        json_builder_int(&builder, ipc_stats.commands_failed);
        json_builder_comma(&builder);
        json_builder_key(&builder, "commands_timeout");
        json_builder_int(&builder, ipc_stats.commands_timeout);
        json_builder_comma(&builder);
        if (ipc_stats.commands_completed > 0) {
            double avg_ipc_latency = (double)ipc_stats.total_latency_ms / ipc_stats.commands_completed;
            json_builder_key(&builder, "avg_latency_ms");
            {
                char lat_buf[64];
                snprintf(lat_buf, sizeof(lat_buf), "%.2f", avg_ipc_latency);
                json_builder_raw(&builder, lat_buf);
            }
        } else {
            json_builder_key(&builder, "avg_latency_ms");
            json_builder_number(&builder, 0);
        }
        json_builder_object_end(&builder);
    }

    json_builder_object_end(&builder);

    set_json_response(response, HTTP_200_OK, json_builder_finish(&builder));
    return 0;
}

static int handle_tools_list(const HttpRequest* request, HttpResponse* response) {
    (void)request;

    char* docs = tools_get_all_documentation();
    set_json_response(response, HTTP_200_OK, docs);
    return 0;
}

static int handle_tool_docs(const char* tool_name, HttpResponse* response) {
    if (!tools_exists(tool_name)) {
        set_error_response(response, HTTP_404_NOT_FOUND, "Tool not found");
        return 0;
    }

    char* docs = tools_get_documentation(tool_name);
    set_json_response(response, HTTP_200_OK, docs);
    return 0;
}

static int handle_tool_execute(const char* tool_name, const JsonValue* params,
                               HttpResponse* response) {
    // Validate tool exists
    if (!tools_exists(tool_name)) {
        set_error_response(response, HTTP_404_NOT_FOUND, "Tool not found");
        return 0;
    }

    // Validate parameters (heap-allocate ValidationResult to avoid stack overflow - it's ~45KB)
    ValidationResult* validation = malloc(sizeof(ValidationResult));
    if (!validation) {
        set_error_response(response, HTTP_500_INTERNAL_ERROR, "Memory allocation failed");
        return 0;
    }
    if (!tools_validate(tool_name, params, validation)) {
        char* error_json = tools_validation_error_json(tool_name, validation);
        free(validation);
        set_json_response(response, HTTP_422_UNPROCESSABLE_ENTITY, error_json);
        return 0;
    }
    free(validation);

    // Handle license endpoints directly via license_manager (always, regardless of browser state)
    // This ensures consistent parameter handling (license_content/license_path)
    // Exception: getLicenseInfo forwards to subprocess when browser is ready (for full license details)
    bool browser_ready_for_license = g_use_async_ipc ? browser_ipc_async_is_ready() : browser_ipc_is_ready();

    bool is_license_endpoint = (
        strcmp(tool_name, TOOL_GET_LICENSE_STATUS) == 0 ||
        strcmp(tool_name, TOOL_GET_HARDWARE_FINGERPRINT) == 0 ||
        strcmp(tool_name, TOOL_ADD_LICENSE) == 0 ||
        strcmp(tool_name, TOOL_REMOVE_LICENSE) == 0 ||
        // getLicenseInfo uses license_manager only when browser not ready
        (strcmp(tool_name, TOOL_GET_LICENSE_INFO) == 0 && !browser_ready_for_license)
    );

    if (is_license_endpoint) {
        char* result_json = NULL;

        if (strcmp(tool_name, TOOL_GET_LICENSE_STATUS) == 0 ||
            strcmp(tool_name, TOOL_GET_LICENSE_INFO) == 0) {
            LicenseInfo info;
            license_manager_get_status(&info);
            result_json = license_manager_status_to_json(&info);
        }
        else if (strcmp(tool_name, TOOL_GET_HARDWARE_FINGERPRINT) == 0) {
            char fingerprint[128];
            if (license_manager_get_fingerprint(fingerprint, sizeof(fingerprint))) {
                JsonBuilder builder;
                json_builder_init(&builder);
                json_builder_object_start(&builder);
                json_builder_key(&builder, "success");
                json_builder_bool(&builder, true);
                json_builder_comma(&builder);
                json_builder_key(&builder, "status");
                json_builder_string(&builder, "ok");
                json_builder_comma(&builder);
                json_builder_key(&builder, "fingerprint");
                json_builder_string(&builder, fingerprint);
                json_builder_object_end(&builder);
                result_json = json_builder_finish(&builder);
            } else {
                result_json = strdup("{\"success\":false,\"error\":\"Failed to get fingerprint\"}");
            }
        }
        else if (strcmp(tool_name, TOOL_ADD_LICENSE) == 0) {
            LicenseOpResult op_result;

            // Check for license_content first (base64 encoded)
            const char* license_content = params ? json_object_get_string(params, "license_content") : NULL;
            if (license_content && license_content[0] != '\0') {
                license_manager_add_license_content(license_content, &op_result);
            } else {
                // Fall back to license_path
                const char* license_path = params ? json_object_get_string(params, "license_path") : NULL;
                if (!license_path) {
                    license_path = params ? json_object_get_string(params, "path") : NULL;
                }
                if (license_path && license_path[0] != '\0') {
                    license_manager_add_license(license_path, &op_result);
                } else {
                    memset(&op_result, 0, sizeof(op_result));
                    strncpy(op_result.error, "Either license_path or license_content is required", sizeof(op_result.error) - 1);
                }
            }

            // If license was successfully added, restart the browser to pick up the new license
            if (op_result.success && g_use_async_ipc) {
                LOG_INFO("Router", "License added successfully, restarting browser...");
                int restart_result = browser_ipc_async_restart();
                if (restart_result == 0) {
                    size_t msg_len = strlen(op_result.message);
                    if (msg_len < sizeof(op_result.message) - 50) {
                        strncat(op_result.message, " Browser restarted.", sizeof(op_result.message) - msg_len - 1);
                    }
                } else {
                    LOG_WARN("Router", "Failed to restart browser after license add");
                }
            }

            result_json = license_manager_result_to_json(&op_result);
        }
        else if (strcmp(tool_name, TOOL_REMOVE_LICENSE) == 0) {
            LicenseOpResult op_result;
            license_manager_remove_license(&op_result);

            // If license was removed, restart the browser
            if (op_result.success && g_use_async_ipc) {
                LOG_INFO("Router", "License removed, restarting browser...");
                browser_ipc_async_restart();
            }

            result_json = license_manager_result_to_json(&op_result);
        }

        if (result_json) {
            set_json_response(response, HTTP_200_OK, result_json);
            return 0;
        }
    }

    // Handle IPC test endpoints (don't require browser to be ready)
    bool is_ipc_test_endpoint = (
        strcmp(tool_name, TOOL_IPC_TESTS_RUN) == 0 ||
        strcmp(tool_name, TOOL_IPC_TESTS_STATUS) == 0 ||
        strcmp(tool_name, TOOL_IPC_TESTS_ABORT) == 0 ||
        strcmp(tool_name, TOOL_IPC_TESTS_LIST_REPORTS) == 0 ||
        strcmp(tool_name, TOOL_IPC_TESTS_GET_REPORT) == 0 ||
        strcmp(tool_name, TOOL_IPC_TESTS_DELETE_REPORT) == 0 ||
        strcmp(tool_name, TOOL_IPC_TESTS_CLEAN_ALL) == 0
    );

    if (is_ipc_test_endpoint) {
        // Check if IPC tests feature is enabled
        if (!ipc_tests_is_enabled()) {
            set_error_response(response, HTTP_503_SERVICE_UNAVAILABLE,
                              "IPC tests feature is not enabled. Set OWL_IPC_TESTS_ENABLED=true");
            return 0;
        }

        JsonBuilder builder;
        json_builder_init(&builder);

        if (strcmp(tool_name, TOOL_IPC_TESTS_RUN) == 0) {
            IpcTestConfig config = {0};
            config.mode = IPC_TEST_MODE_FULL;  // Default

            // Parse parameters
            if (params) {
                const char* mode_str = json_object_get_string(params, "mode");
                if (mode_str) {
                    config.mode = ipc_test_mode_from_string(mode_str);
                }
                config.verbose = json_object_get_bool(params, "verbose", false);
                config.iterations = (int)json_object_get_int(params, "iterations", 1);
                config.contexts = (int)json_object_get_int(params, "contexts", 10);
                config.duration_seconds = (int)json_object_get_int(params, "duration", 60);
                config.concurrency = (int)json_object_get_int(params, "concurrency", 1);
            }

            IpcTestResult result = {0};
            int ret = ipc_tests_start(&config, &result);

            json_builder_object_start(&builder);
            json_builder_key(&builder, "success");
            json_builder_bool(&builder, ret == 0);

            if (ret == 0) {
                json_builder_comma(&builder);
                json_builder_key(&builder, "result");
                json_builder_object_start(&builder);
                json_builder_key(&builder, "run_id");
                json_builder_string(&builder, result.run_id);
                json_builder_comma(&builder);
                json_builder_key(&builder, "status");
                json_builder_string(&builder, ipc_test_status_to_string(result.status));
                json_builder_comma(&builder);
                json_builder_key(&builder, "mode");
                json_builder_string(&builder, ipc_test_mode_to_string(config.mode));
                json_builder_object_end(&builder);
            } else {
                json_builder_comma(&builder);
                json_builder_key(&builder, "error");
                json_builder_string(&builder, result.error_message);
            }
            json_builder_object_end(&builder);
        }
        else if (strcmp(tool_name, TOOL_IPC_TESTS_STATUS) == 0) {
            IpcTestResult result = {0};
            const char* run_id = params ? json_object_get_string(params, "run_id") : NULL;
            int ret;

            if (run_id && strlen(run_id) > 0) {
                ret = ipc_tests_get_status(run_id, &result);
            } else {
                ret = ipc_tests_get_current_status(&result);
            }

            json_builder_object_start(&builder);
            json_builder_key(&builder, "success");
            json_builder_bool(&builder, ret == 0);

            if (ret == 0) {
                json_builder_comma(&builder);
                json_builder_key(&builder, "result");
                json_builder_object_start(&builder);
                json_builder_key(&builder, "run_id");
                json_builder_string(&builder, result.run_id);
                json_builder_comma(&builder);
                json_builder_key(&builder, "status");
                json_builder_string(&builder, ipc_test_status_to_string(result.status));
                json_builder_comma(&builder);
                json_builder_key(&builder, "exit_code");
                json_builder_int(&builder, result.exit_code);
                json_builder_comma(&builder);
                json_builder_key(&builder, "total_tests");
                json_builder_int(&builder, result.total_tests);
                json_builder_comma(&builder);
                json_builder_key(&builder, "passed_tests");
                json_builder_int(&builder, result.passed_tests);
                json_builder_comma(&builder);
                json_builder_key(&builder, "failed_tests");
                json_builder_int(&builder, result.failed_tests);
                json_builder_comma(&builder);
                json_builder_key(&builder, "skipped_tests");
                json_builder_int(&builder, result.skipped_tests);
                json_builder_comma(&builder);
                json_builder_key(&builder, "duration_seconds");
                {
                    char dur_buf[32];
                    snprintf(dur_buf, sizeof(dur_buf), "%.3f", result.duration_seconds);
                    json_builder_raw(&builder, dur_buf);
                }
                json_builder_comma(&builder);
                json_builder_key(&builder, "commands_per_second");
                {
                    char cps_buf[32];
                    snprintf(cps_buf, sizeof(cps_buf), "%.2f", result.commands_per_second);
                    json_builder_raw(&builder, cps_buf);
                }
                if (result.error_message[0]) {
                    json_builder_comma(&builder);
                    json_builder_key(&builder, "error_message");
                    json_builder_string(&builder, result.error_message);
                }
                json_builder_object_end(&builder);
            } else {
                json_builder_comma(&builder);
                json_builder_key(&builder, "error");
                json_builder_string(&builder, "No test run found");
            }
            json_builder_object_end(&builder);
        }
        else if (strcmp(tool_name, TOOL_IPC_TESTS_ABORT) == 0) {
            const char* run_id = params ? json_object_get_string(params, "run_id") : NULL;

            if (!run_id || strlen(run_id) == 0) {
                set_error_response(response, HTTP_400_BAD_REQUEST, "run_id is required");
                return 0;
            }

            int ret = ipc_tests_abort(run_id);

            json_builder_object_start(&builder);
            json_builder_key(&builder, "success");
            json_builder_bool(&builder, ret == 0);
            if (ret != 0) {
                json_builder_comma(&builder);
                json_builder_key(&builder, "error");
                json_builder_string(&builder, "Failed to abort test (not found or not running)");
            }
            json_builder_object_end(&builder);
        }
        else if (strcmp(tool_name, TOOL_IPC_TESTS_LIST_REPORTS) == 0) {
            IpcTestReportInfo reports[100];
            int count = ipc_tests_list_reports(reports, 100);

            json_builder_object_start(&builder);
            json_builder_key(&builder, "success");
            json_builder_bool(&builder, count >= 0);
            json_builder_comma(&builder);
            json_builder_key(&builder, "result");
            json_builder_array_start(&builder);

            for (int i = 0; i < count; i++) {
                if (i > 0) json_builder_comma(&builder);
                json_builder_object_start(&builder);
                json_builder_key(&builder, "run_id");
                json_builder_string(&builder, reports[i].run_id);
                json_builder_comma(&builder);
                json_builder_key(&builder, "timestamp");
                json_builder_string(&builder, reports[i].timestamp);
                json_builder_comma(&builder);
                json_builder_key(&builder, "mode");
                json_builder_string(&builder, reports[i].mode);
                json_builder_comma(&builder);
                json_builder_key(&builder, "total_tests");
                json_builder_int(&builder, reports[i].total_tests);
                json_builder_comma(&builder);
                json_builder_key(&builder, "passed_tests");
                json_builder_int(&builder, reports[i].passed_tests);
                json_builder_comma(&builder);
                json_builder_key(&builder, "failed_tests");
                json_builder_int(&builder, reports[i].failed_tests);
                json_builder_comma(&builder);
                json_builder_key(&builder, "duration_seconds");
                {
                    char dur_buf[32];
                    snprintf(dur_buf, sizeof(dur_buf), "%.3f", reports[i].duration_seconds);
                    json_builder_raw(&builder, dur_buf);
                }
                json_builder_object_end(&builder);
            }

            json_builder_array_end(&builder);
            json_builder_object_end(&builder);
        }
        else if (strcmp(tool_name, TOOL_IPC_TESTS_GET_REPORT) == 0) {
            const char* run_id = params ? json_object_get_string(params, "run_id") : NULL;
            const char* format = params ? json_object_get_string(params, "format") : "json";

            if (!run_id || strlen(run_id) == 0) {
                set_error_response(response, HTTP_400_BAD_REQUEST, "run_id is required");
                return 0;
            }

            if (!format) format = "json";

            char* content = NULL;
            int ret;

            if (strcmp(format, "html") == 0) {
                ret = ipc_tests_get_html_report(run_id, &content);
                if (ret == 0 && content) {
                    response->status = HTTP_200_OK;
                    strcpy(response->content_type, "text/html; charset=utf-8");
                    response->body = content;
                    response->body_size = strlen(content);
                    response->owns_body = true;
                    return 0;
                }
            } else {
                ret = ipc_tests_get_json_report(run_id, &content);
                if (ret == 0 && content) {
                    json_builder_object_start(&builder);
                    json_builder_key(&builder, "success");
                    json_builder_bool(&builder, true);
                    json_builder_comma(&builder);
                    json_builder_key(&builder, "result");
                    json_builder_raw(&builder, content);
                    json_builder_object_end(&builder);
                    free(content);
                    set_json_response(response, HTTP_200_OK, json_builder_finish(&builder));
                    return 0;
                }
            }

            set_error_response(response, HTTP_404_NOT_FOUND, "Report not found");
            return 0;
        }
        else if (strcmp(tool_name, TOOL_IPC_TESTS_DELETE_REPORT) == 0) {
            const char* run_id = params ? json_object_get_string(params, "run_id") : NULL;

            if (!run_id || strlen(run_id) == 0) {
                set_error_response(response, HTTP_400_BAD_REQUEST, "run_id is required");
                return 0;
            }

            int ret = ipc_tests_delete_report(run_id);

            json_builder_object_start(&builder);
            json_builder_key(&builder, "success");
            json_builder_bool(&builder, ret == 0);
            if (ret != 0) {
                json_builder_comma(&builder);
                json_builder_key(&builder, "error");
                json_builder_string(&builder, "Failed to delete report");
            }
            json_builder_object_end(&builder);
        }
        else if (strcmp(tool_name, TOOL_IPC_TESTS_CLEAN_ALL) == 0) {
            int deleted_count = ipc_tests_clean_all_reports();

            json_builder_object_start(&builder);
            json_builder_key(&builder, "success");
            json_builder_bool(&builder, deleted_count >= 0);
            json_builder_comma(&builder);
            json_builder_key(&builder, "result");
            json_builder_object_start(&builder);
            json_builder_key(&builder, "deleted_count");
            json_builder_int(&builder, deleted_count >= 0 ? deleted_count : 0);
            json_builder_object_end(&builder);
            if (deleted_count < 0) {
                json_builder_comma(&builder);
                json_builder_key(&builder, "error");
                json_builder_string(&builder, "Failed to clean reports");
            }
            json_builder_object_end(&builder);
        }

        set_json_response(response, HTTP_200_OK, json_builder_finish(&builder));
        return 0;
    }

    // Check browser is ready
    bool browser_ready = g_use_async_ipc ? browser_ipc_async_is_ready() : browser_ipc_is_ready();
    if (!browser_ready) {
        // License endpoints are handled above, so this is for non-license endpoints
        // Check for license error state
        bool is_license_error = false;
        const char* license_status = NULL;
        const char* license_message = NULL;
        const char* hardware_fingerprint = NULL;

        if (g_use_async_ipc) {
            AsyncBrowserState state = browser_ipc_async_get_state();
            if (state == ASYNC_BROWSER_LICENSE_ERROR) {
                const AsyncLicenseError* le = browser_ipc_async_get_license_error();
                is_license_error = true;
                license_status = le->status;
                license_message = le->message;
                hardware_fingerprint = le->fingerprint;
            }
        } else {
            BrowserState state = browser_ipc_get_state();
            if (state == BROWSER_STATE_LICENSE_ERROR) {
                const LicenseError* le = browser_ipc_get_license_error();
                is_license_error = true;
                license_status = le->status;
                license_message = le->message;
                hardware_fingerprint = le->fingerprint;
            }
        }

        if (is_license_error) {
            JsonBuilder builder;
            json_builder_init(&builder);
            json_builder_object_start(&builder);
            json_builder_key(&builder, "success");
            json_builder_bool(&builder, false);
            json_builder_comma(&builder);
            json_builder_key(&builder, "error");
            json_builder_string(&builder, "License error");
            json_builder_comma(&builder);
            json_builder_key(&builder, "license_status");
            json_builder_string(&builder, license_status);
            json_builder_comma(&builder);
            json_builder_key(&builder, "license_message");
            json_builder_string(&builder, license_message);
            if (hardware_fingerprint && strlen(hardware_fingerprint) > 0) {
                json_builder_comma(&builder);
                json_builder_key(&builder, "hardware_fingerprint");
                json_builder_string(&builder, hardware_fingerprint);
            }
            json_builder_object_end(&builder);

            set_json_response(response, HTTP_503_SERVICE_UNAVAILABLE,
                            json_builder_finish(&builder));
            return 0;
        }

        set_error_response(response, HTTP_503_SERVICE_UNAVAILABLE,
                          "Browser not ready");
        return 0;
    }

    // Special handling for live stream tools to set up SHM reader
    // This prevents redundant IPC calls when /video/stream/ is accessed
    if (strcmp(tool_name, TOOL_START_LIVE_STREAM) == 0) {
        const char* context_id = params ? json_object_get_string(params, "context_id") : NULL;
        int fps = (int)json_object_get_int(params, "fps", 15);
        int quality = (int)json_object_get_int(params, "quality", 75);

        if (!context_id) {
            set_error_response(response, HTTP_400_BAD_REQUEST, "context_id required");
            return 0;
        }

        bool success = video_stream_start(context_id, fps, quality);

        JsonBuilder builder;
        json_builder_init(&builder);
        json_builder_object_start(&builder);
        json_builder_key(&builder, "success");
        json_builder_bool(&builder, success);
        if (success) {
            json_builder_comma(&builder);
            json_builder_key(&builder, "result");
            json_builder_bool(&builder, true);
        } else {
            json_builder_comma(&builder);
            json_builder_key(&builder, "error");
            json_builder_string(&builder, "Failed to start live stream");
        }
        json_builder_object_end(&builder);

        set_json_response(response, HTTP_200_OK, json_builder_finish(&builder));
        return 0;
    }

    if (strcmp(tool_name, TOOL_STOP_LIVE_STREAM) == 0) {
        const char* context_id = params ? json_object_get_string(params, "context_id") : NULL;

        if (!context_id) {
            set_error_response(response, HTTP_400_BAD_REQUEST, "context_id required");
            return 0;
        }

        bool success = video_stream_stop(context_id);

        JsonBuilder builder;
        json_builder_init(&builder);
        json_builder_object_start(&builder);
        json_builder_key(&builder, "success");
        json_builder_bool(&builder, success);
        if (success) {
            json_builder_comma(&builder);
            json_builder_key(&builder, "result");
            json_builder_bool(&builder, true);
        } else {
            json_builder_comma(&builder);
            json_builder_key(&builder, "error");
            json_builder_string(&builder, "Failed to stop live stream");
        }
        json_builder_object_end(&builder);

        set_json_response(response, HTTP_200_OK, json_builder_finish(&builder));
        return 0;
    }

    // Get browser method name
    const char* browser_method = get_browser_method(tool_name);
    if (!browser_method) {
        set_error_response(response, HTTP_500_INTERNAL_ERROR,
                          "No method mapping for tool");
        return 0;
    }

    // Build params JSON
    char* params_json = NULL;
    if (params && params->type == JSON_OBJECT) {
        JsonBuilder builder;
        json_builder_init(&builder);
        json_builder_object_start(&builder);

        bool first = true;
        JsonPair* pair = params->object_val->pairs;
        while (pair) {
            if (!first) json_builder_comma(&builder);
            first = false;

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
            }
            pair = pair->next;
        }

        json_builder_object_end(&builder);
        params_json = json_builder_finish(&builder);
    }

    // Send command to browser (use async IPC for better concurrency)
    OperationResult result;
    int ret;
    if (g_use_async_ipc) {
        ret = browser_ipc_async_send_sync(browser_method, params_json, &result);
    } else {
        ret = browser_ipc_send_command(browser_method, params_json, &result);
    }
    free(params_json);

    if (ret < 0) {
        set_error_response(response, HTTP_502_BAD_GATEWAY, result.error);
        return 0;
    }

    // Always return consistent format: {success: bool, result: <data>} or {success: false, error: string}
    JsonBuilder builder;
    json_builder_init(&builder);
    json_builder_object_start(&builder);
    json_builder_key(&builder, "success");
    json_builder_bool(&builder, result.success);

    if (result.success && result.data) {
        json_builder_comma(&builder);
        json_builder_key(&builder, "result");
        // Try to parse result.data as JSON, if it fails treat as string
        JsonValue* res_val = json_parse(result.data);
        if (res_val) {
            json_builder_raw(&builder, result.data);
            json_free(res_val);
        } else {
            json_builder_string(&builder, result.data);
        }
    } else if (!result.success) {
        json_builder_comma(&builder);
        json_builder_key(&builder, "error");
        json_builder_string(&builder, result.error);
    }

    json_builder_object_end(&builder);

    free(result.data);
    set_json_response(response, HTTP_200_OK, json_builder_finish(&builder));
    return 0;
}

// ============================================================================
// Main Router
// ============================================================================

void router_init(const ServerConfig* config) {
    g_config = config;
    // Enable async IPC for better concurrency
    g_use_async_ipc = true;
}

// Helper to add rate limit headers to response
static void add_rate_limit_headers(HttpResponse* response, const RateLimitResult* rl) {
    // Note: In a full implementation, we'd add headers to the response
    // For now, we include rate limit info in the error response body
    (void)response;
    (void)rl;
}

int router_handle_request(const HttpRequest* request, HttpResponse* response) {
    http_response_init(response);

    // Handle CORS preflight (always allow)
    if (request->method == HTTP_OPTIONS) {
        response->status = HTTP_204_NO_CONTENT;
        return 0;
    }

    // IP whitelist check (applies to all requests except health when IP filter is enabled)
    if (ip_filter_is_enabled() && strcmp(request->path, "/health") != 0) {
        IpFilterResult ip_result = ip_filter_check(request->client_ip);
        if (ip_result == IP_FILTER_DENIED) {
            LOG_WARN("Router", "IP denied: %s", request->client_ip);
            set_error_response(response, HTTP_403_FORBIDDEN,
                              "Access denied: IP not in whitelist");
            return 0;
        } else if (ip_result == IP_FILTER_INVALID) {
            LOG_WARN("Router", "Invalid client IP: %s", request->client_ip);
            set_error_response(response, HTTP_400_BAD_REQUEST,
                              "Invalid client IP address");
            return 0;
        }
    }

    // Rate limiting check (applies to all requests except health)
    if (rate_limit_is_enabled() && strcmp(request->path, "/health") != 0) {
        RateLimitResult rl_result;
        if (!rate_limit_check(request->client_ip, &rl_result)) {
            LOG_WARN("Router", "Rate limited: %s (retry after %d sec)",
                     request->client_ip, rl_result.retry_after);

            // Build rate limit error response with retry info
            JsonBuilder builder;
            json_builder_init(&builder);
            json_builder_object_start(&builder);
            json_builder_key(&builder, "success");
            json_builder_bool(&builder, false);
            json_builder_comma(&builder);
            json_builder_key(&builder, "error");
            json_builder_string(&builder, "Rate limit exceeded");
            json_builder_comma(&builder);
            json_builder_key(&builder, "retry_after");
            json_builder_number(&builder, rl_result.retry_after);
            json_builder_comma(&builder);
            json_builder_key(&builder, "limit");
            json_builder_number(&builder, rl_result.limit);
            json_builder_comma(&builder);
            json_builder_key(&builder, "remaining");
            json_builder_number(&builder, 0);
            json_builder_object_end(&builder);

            set_json_response(response, HTTP_429_TOO_MANY_REQUESTS,
                            json_builder_finish(&builder));
            add_rate_limit_headers(response, &rl_result);
            return 0;
        }
        // Record the request for rate limiting
        rate_limit_record(request->client_ip);
    }

    // Health check endpoint (no auth required)
    if (strcmp(request->path, "/health") == 0) {
        if (request->method != HTTP_GET) {
            set_error_response(response, HTTP_405_METHOD_NOT_ALLOWED,
                              "Method not allowed");
            return 0;
        }
        return handle_health(request, response);
    }

    // API Playground UI (no auth required - serves at root)
    if (strcmp(request->path, "/") == 0) {
        if (request->method != HTTP_GET) {
            set_error_response(response, HTTP_405_METHOD_NOT_ALLOWED,
                              "Method not allowed");
            return 0;
        }
        response->status = HTTP_200_OK;
        strcpy(response->content_type, "text/html; charset=utf-8");
        response->body = (char*)playground_get_html();
        response->body_size = playground_get_html_size();
        response->owns_body = false;  // Static content, don't free
        return 0;
    }

    // API Schema endpoint (no auth required - for playground UI)
    if (strcmp(request->path, "/api/schema") == 0) {
        if (request->method != HTTP_GET) {
            set_error_response(response, HTTP_405_METHOD_NOT_ALLOWED,
                              "Method not allowed");
            return 0;
        }
        char* schema = playground_get_tool_schema();
        set_json_response(response, HTTP_200_OK, schema);
        return 0;
    }

    // Logo SVG endpoint (no auth required - for playground UI)
    if (strcmp(request->path, "/logo.svg") == 0) {
        if (request->method != HTTP_GET) {
            set_error_response(response, HTTP_405_METHOD_NOT_ALLOWED,
                              "Method not allowed");
            return 0;
        }
        response->status = HTTP_200_OK;
        strcpy(response->content_type, "image/svg+xml");
        response->body = (char*)playground_get_logo();
        response->body_size = playground_get_logo_size();
        response->owns_body = false;  // Static content, don't free
        return 0;
    }

    // GET /stats - Server statistics (no auth required, like health)
    if (strcmp(request->path, "/stats") == 0) {
        if (request->method != HTTP_GET) {
            set_error_response(response, HTTP_405_METHOD_NOT_ALLOWED,
                              "Method not allowed");
            return 0;
        }
        return handle_stats(request, response);
    }

    // POST /auth - Panel login (validates password, returns auth token)
    if (strcmp(request->path, "/auth") == 0) {
        if (request->method != HTTP_POST) {
            set_error_response(response, HTTP_405_METHOD_NOT_ALLOWED,
                              "Use POST for authentication");
            return 0;
        }

        // Parse request body
        if (!request->body || request->body_size == 0) {
            set_error_response(response, HTTP_400_BAD_REQUEST,
                              "Request body required");
            return 0;
        }

        JsonValue* body = json_parse(request->body);
        if (!body || body->type != JSON_OBJECT) {
            json_free(body);
            set_error_response(response, HTTP_400_BAD_REQUEST,
                              "Invalid JSON in request body");
            return 0;
        }

        const char* password = json_object_get_string(body, "password");
        if (!password || strlen(password) == 0) {
            json_free(body);
            set_error_response(response, HTTP_400_BAD_REQUEST,
                              "Password is required");
            return 0;
        }

        // Validate password against OWL_PANEL_PASSWORD
        if (auth_validate_panel_password(password)) {
            const char* token = auth_get_token();
            JsonBuilder builder;
            json_builder_init(&builder);
            json_builder_object_start(&builder);
            json_builder_key(&builder, "success");
            json_builder_bool(&builder, true);
            if (token) {
                json_builder_comma(&builder);
                json_builder_key(&builder, "token");
                json_builder_string(&builder, token);
            }
            json_builder_object_end(&builder);
            json_free(body);
            set_json_response(response, HTTP_200_OK, json_builder_finish(&builder));
            return 0;
        } else {
            json_free(body);
            set_error_response(response, HTTP_401_UNAUTHORIZED,
                              "Invalid password");
            return 0;
        }
    }

    // GET /auth/verify - Verify if token is valid
    if (strcmp(request->path, "/auth/verify") == 0) {
        if (request->method != HTTP_GET) {
            set_error_response(response, HTTP_405_METHOD_NOT_ALLOWED,
                              "Use GET for verification");
            return 0;
        }

        bool valid = auth_validate(request->authorization);
        JsonBuilder builder;
        json_builder_init(&builder);
        json_builder_object_start(&builder);
        json_builder_key(&builder, "valid");
        json_builder_bool(&builder, valid);
        json_builder_object_end(&builder);
        set_json_response(response, HTTP_200_OK, json_builder_finish(&builder));
        return 0;
    }

    // All other endpoints require authentication
    if (!auth_validate(request->authorization)) {
        set_error_response(response, HTTP_401_UNAUTHORIZED,
                          "Invalid or missing authorization token");
        return 0;
    }

    // GET /video/list - List active video streams
    if (strcmp(request->path, "/video/list") == 0) {
        if (request->method != HTTP_GET) {
            set_error_response(response, HTTP_405_METHOD_NOT_ALLOWED,
                              "Method not allowed");
            return 0;
        }
        char* list = video_stream_list();
        set_json_response(response, HTTP_200_OK, list);
        return 0;
    }

    // GET /video/stats - Video streaming statistics
    if (strcmp(request->path, "/video/stats") == 0) {
        if (request->method != HTTP_GET) {
            set_error_response(response, HTTP_405_METHOD_NOT_ALLOWED,
                              "Method not allowed");
            return 0;
        }
        VideoStreamStats stats;
        video_stream_get_stats(&stats);

        JsonBuilder builder;
        json_builder_init(&builder);
        json_builder_object_start(&builder);
        json_builder_key(&builder, "active_streams");
        json_builder_number(&builder, stats.active_streams);
        json_builder_comma(&builder);
        json_builder_key(&builder, "active_clients");
        json_builder_number(&builder, stats.active_clients);
        json_builder_comma(&builder);
        json_builder_key(&builder, "total_frames_sent");
        json_builder_number(&builder, stats.total_frames_sent);
        json_builder_comma(&builder);
        json_builder_key(&builder, "total_bytes_sent");
        json_builder_number(&builder, stats.total_bytes_sent);
        json_builder_object_end(&builder);

        set_json_response(response, HTTP_200_OK, json_builder_finish(&builder));
        return 0;
    }

    // GET /tools - List all tools
    if (strcmp(request->path, "/tools") == 0) {
        if (request->method != HTTP_GET) {
            set_error_response(response, HTTP_405_METHOD_NOT_ALLOWED,
                              "Method not allowed");
            return 0;
        }
        return handle_tools_list(request, response);
    }

    // GET /tools/{tool_name} - Get tool documentation
    if (strncmp(request->path, "/tools/", 7) == 0 && request->method == HTTP_GET) {
        const char* tool_name = request->path + 7;
        // Convert path format to tool name (browser_click -> browser_click)
        // Path already uses the correct format
        return handle_tool_docs(tool_name, response);
    }

    // POST /execute/{tool_name} - Execute a tool
    if (strncmp(request->path, "/execute/", 9) == 0) {
        if (request->method != HTTP_POST) {
            set_error_response(response, HTTP_405_METHOD_NOT_ALLOWED,
                              "Use POST to execute tools");
            return 0;
        }

        const char* tool_name = request->path + 9;

        // Parse request body as JSON
        JsonValue* params = NULL;
        if (request->body && request->body_size > 0) {
            params = json_parse(request->body);
            if (!params && request->body_size > 2) {  // Not empty "{}"
                set_error_response(response, HTTP_400_BAD_REQUEST,
                                  "Invalid JSON in request body");
                return 0;
            }
        }

        int ret = handle_tool_execute(tool_name, params, response);
        json_free(params);
        return ret;
    }

    // POST /command - Execute raw browser command (advanced)
    if (strcmp(request->path, "/command") == 0) {
        if (request->method != HTTP_POST) {
            set_error_response(response, HTTP_405_METHOD_NOT_ALLOWED,
                              "Use POST for commands");
            return 0;
        }

        if (!request->body || request->body_size == 0) {
            set_error_response(response, HTTP_400_BAD_REQUEST,
                              "Request body required");
            return 0;
        }

        bool cmd_browser_ready = g_use_async_ipc ? browser_ipc_async_is_ready() : browser_ipc_is_ready();
        if (!cmd_browser_ready) {
            set_error_response(response, HTTP_503_SERVICE_UNAVAILABLE,
                              "Browser not ready");
            return 0;
        }

        OperationResult result;
        // Note: browser_ipc_send_raw is not ported to async yet - use sync path
        int ret = browser_ipc_send_raw(request->body, &result);

        if (ret < 0) {
            set_error_response(response, HTTP_502_BAD_GATEWAY, result.error);
            return 0;
        }

        char* resp_json = result.success ?
            json_success_response_raw(result.data) :
            json_error_response(result.error);

        free(result.data);
        set_json_response(response, HTTP_200_OK, resp_json);
        return 0;
    }

    // 404 for unknown routes
    set_error_response(response, HTTP_404_NOT_FOUND, "Endpoint not found");
    return 0;
}

void router_shutdown(void) {
    g_config = NULL;
}
