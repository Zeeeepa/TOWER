/**
 * Owl Browser HTTP Server - Main Entry Point
 *
 * A standalone HTTP server for REST API access to the Owl Browser.
 *
 * Usage:
 *   owl_http_server [OPTIONS]
 *
 * Options:
 *   -c, --config <file>         Path to JSON or YAML config file
 *   -h, --help                  Show this help message
 *   -v, --version               Show version information
 *   --generate-config <file>    Generate example config file
 *
 * Environment Variables (override config file):
 *   OWL_HTTP_HOST     - Server host (default: 127.0.0.1)
 *   OWL_HTTP_PORT     - Server port (default: 8080)
 *   OWL_HTTP_TOKEN    - Authorization bearer token (required)
 *   OWL_BROWSER_PATH  - Path to browser binary (required)
 *   OWL_HTTP_VERBOSE  - Enable verbose logging (default: false)
 *   ... and many more (see documentation)
 *
 * API Endpoints:
 *   GET  /health              - Health check (no auth required)
 *   GET  /tools               - List all available tools
 *   GET  /tools/{name}        - Get tool documentation
 *   POST /execute/{tool_name} - Execute a browser tool
 *   POST /command             - Execute raw browser command (advanced)
 */

#include "config.h"
#include "config_file.h"
#include "log.h"
#include "auth.h"
#include "json.h"
#include "tools.h"
#include "browser_ipc.h"
#include "browser_ipc_async.h"
#include "router.h"
#include "http_server.h"
#include "websocket.h"
#include "rate_limit.h"
#include "ip_filter.h"
#include "video_stream.h"
#include "license_manager.h"
#include "ipc_tests.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define VERSION "1.1.0"

static ServerConfig g_config;
static char g_config_file[4096] = {0};

// ============================================================================
// WebSocket Message Handler
// ============================================================================

// Tool name to browser method mapping (same as router.c)
static const char* get_browser_method_for_tool(const char* tool_name) {
    // This is a subset for WebSocket - full mapping is in router.c
    static const struct { const char* tool; const char* method; } mappings[] = {
        {"browser_create_context", "createContext"},
        {"browser_close_context", "closeContext"},
        {"browser_list_contexts", "listContexts"},
        {"browser_navigate", "navigate"},
        {"browser_reload", "reload"},
        {"browser_go_back", "goBack"},
        {"browser_go_forward", "goForward"},
        {"browser_can_go_back", "canGoBack"},
        {"browser_can_go_forward", "canGoForward"},
        // Interaction
        {"browser_click", "click"},
        {"browser_type", "type"},
        {"browser_pick", "pick"},
        {"browser_press_key", "pressKey"},
        {"browser_submit_form", "submitForm"},
        {"browser_drag_drop", "dragDrop"},
        {"browser_html5_drag_drop", "html5DragDrop"},
        {"browser_mouse_move", "mouseMove"},
        {"browser_hover", "hover"},
        {"browser_double_click", "doubleClick"},
        {"browser_right_click", "rightClick"},
        {"browser_clear_input", "clearInput"},
        {"browser_focus", "focus"},
        {"browser_blur", "blur"},
        {"browser_select_all", "selectAll"},
        {"browser_keyboard_combo", "keyboardCombo"},
        {"browser_upload_file", "uploadFile"},
        // Element State Checks
        {"browser_is_visible", "isVisible"},
        {"browser_is_enabled", "isEnabled"},
        {"browser_is_checked", "isChecked"},
        {"browser_get_attribute", "getAttribute"},
        {"browser_get_bounding_box", "getBoundingBox"},
        // Element Picker
        {"browser_get_element_at_position", "getElementAtPosition"},
        {"browser_get_interactive_elements", "getInteractiveElements"},
        {"browser_get_blocker_stats", "getBlockerStats"},
        // JavaScript Evaluation
        {"browser_evaluate", "evaluate"},
        // Frame Handling
        {"browser_list_frames", "listFrames"},
        {"browser_switch_to_frame", "switchToFrame"},
        {"browser_switch_to_main_frame", "switchToMainFrame"},
        // Content Extraction
        {"browser_extract_text", "extractText"},
        {"browser_screenshot", "screenshot"},
        {"browser_highlight", "highlight"},
        {"browser_show_grid_overlay", "showGridOverlay"},
        {"browser_get_html", "getHTML"},
        {"browser_get_markdown", "getMarkdown"},
        {"browser_extract_json", "extractJSON"},
        {"browser_detect_site", "detectWebsiteType"},
        {"browser_list_templates", "listTemplates"},
        // AI/LLM Features
        {"browser_summarize_page", "summarizePage"},
        {"browser_query_page", "queryPage"},
        {"browser_llm_status", "getLLMStatus"},
        {"browser_nla", "executeNLA"},
        {"browser_ai_click", "aiClick"},
        {"browser_ai_type", "aiType"},
        {"browser_ai_extract", "aiExtract"},
        {"browser_ai_query", "aiQuery"},
        {"browser_ai_analyze", "aiAnalyze"},
        {"browser_find_element", "findElement"},
        {"browser_scroll_by", "scrollBy"},
        {"browser_scroll_to_element", "scrollToElement"},
        {"browser_scroll_to_top", "scrollToTop"},
        {"browser_scroll_to_bottom", "scrollToBottom"},
        {"browser_wait_for_selector", "waitForSelector"},
        {"browser_wait", "waitForTimeout"},
        {"browser_wait_for_network_idle", "waitForNetworkIdle"},
        {"browser_wait_for_function", "waitForFunction"},
        {"browser_wait_for_url", "waitForURL"},
        {"browser_get_page_info", "getPageInfo"},
        {"browser_set_viewport", "setViewport"},
        // DOM Zoom
        {"browser_zoom_in", "zoomIn"},
        {"browser_zoom_out", "zoomOut"},
        {"browser_zoom_reset", "zoomReset"},
        // Console Logs
        {"browser_get_console_log", "getConsoleLogs"},
        {"browser_clear_console_log", "clearConsoleLogs"},
        {"browser_start_video_recording", "startVideoRecording"},
        {"browser_pause_video_recording", "pauseVideoRecording"},
        {"browser_resume_video_recording", "resumeVideoRecording"},
        {"browser_stop_video_recording", "stopVideoRecording"},
        {"browser_get_video_recording_stats", "getVideoRecordingStats"},
        // Live video streaming
        {"browser_start_live_stream", "startLiveStream"},
        {"browser_stop_live_stream", "stopLiveStream"},
        {"browser_get_live_stream_stats", "getLiveStreamStats"},
        {"browser_list_live_streams", "listLiveStreams"},
        {"browser_get_live_frame", "getLiveFrame"},
        // Demographics
        {"browser_get_demographics", "getDemographics"},
        {"browser_get_location", "getLocation"},
        {"browser_get_datetime", "getDateTime"},
        {"browser_get_weather", "getWeather"},
        {"browser_detect_captcha", "detectCaptcha"},
        {"browser_classify_captcha", "classifyCaptcha"},
        {"browser_solve_text_captcha", "solveTextCaptcha"},
        {"browser_solve_image_captcha", "solveImageCaptcha"},
        {"browser_solve_captcha", "solveCaptcha"},
        {"browser_get_cookies", "getCookies"},
        {"browser_set_cookie", "setCookie"},
        {"browser_delete_cookies", "deleteCookies"},
        {"browser_set_proxy", "setProxy"},
        {"browser_get_proxy_status", "getProxyStatus"},
        {"browser_connect_proxy", "connectProxy"},
        {"browser_disconnect_proxy", "disconnectProxy"},
        {"browser_create_profile", "createProfile"},
        {"browser_load_profile", "loadProfile"},
        {"browser_save_profile", "saveProfile"},
        {"browser_get_profile", "getProfile"},
        {"browser_update_profile_cookies", "updateProfileCookies"},
        {"browser_get_context_info", "getContextInfo"},
        // Clipboard
        {"browser_clipboard_read", "clipboardRead"},
        {"browser_clipboard_write", "clipboardWrite"},
        {"browser_clipboard_clear", "clipboardClear"},
        // License management
        {"browser_get_license_status", "getLicenseStatus"},
        {"browser_get_license_info", "getLicenseInfo"},
        {"browser_get_hardware_fingerprint", "getHardwareFingerprint"},
        {"browser_add_license", "addLicense"},
        {"browser_remove_license", "removeLicense"},
        // Network Interception
        {"browser_add_network_rule", "addNetworkRule"},
        {"browser_remove_network_rule", "removeNetworkRule"},
        {"browser_enable_network_interception", "enableNetworkInterception"},
        {"browser_enable_network_logging", "enableNetworkLogging"},
        {"browser_get_network_log", "getNetworkLog"},
        {"browser_clear_network_log", "clearNetworkLog"},
        // File Downloads
        {"browser_set_download_path", "setDownloadPath"},
        {"browser_get_downloads", "getDownloads"},
        {"browser_get_active_downloads", "getActiveDownloads"},
        {"browser_wait_for_download", "waitForDownload"},
        {"browser_cancel_download", "cancelDownload"},
        // Dialog Handling
        {"browser_set_dialog_action", "setDialogAction"},
        {"browser_get_pending_dialog", "getPendingDialog"},
        {"browser_get_dialogs", "getDialogs"},
        {"browser_handle_dialog", "handleDialog"},
        {"browser_wait_for_dialog", "waitForDialog"},
        // Tab/Window Management
        {"browser_set_popup_policy", "setPopupPolicy"},
        {"browser_get_tabs", "getTabs"},
        {"browser_switch_tab", "switchTab"},
        {"browser_close_tab", "closeTab"},
        {"browser_new_tab", "newTab"},
        {"browser_get_active_tab", "getActiveTab"},
        {"browser_get_tab_count", "getTabCount"},
        {"browser_get_blocked_popups", "getBlockedPopups"},
        {NULL, NULL}
    };

    for (int i = 0; mappings[i].tool; i++) {
        if (strcmp(mappings[i].tool, tool_name) == 0) {
            return mappings[i].method;
        }
    }
    return NULL;
}

static void ws_message_handler(WsConnection* conn, const char* message, size_t len) {
    (void)len;

    LOG_DEBUG("WebSocket", "Received message: %.100s%s",
              message, len > 100 ? "..." : "");

    // Parse JSON message
    // Expected format: {"id": 1, "method": "tool_name", "params": {...}}
    JsonValue* root = json_parse(message);
    if (!root || root->type != JSON_OBJECT) {
        // Invalid JSON
        const char* err = "{\"success\":false,\"error\":\"Invalid JSON\"}";
        ws_send_text(conn, err, 0);
        json_free(root);
        return;
    }

    // Get message ID (for request/response correlation)
    int64_t msg_id = json_object_get_int(root, "id", -1);

    // Get method name
    const char* method = json_object_get_string(root, "method");
    if (!method) {
        char err[256];
        snprintf(err, sizeof(err),
                 "{\"id\":%lld,\"success\":false,\"error\":\"Missing method\"}",
                 (long long)msg_id);
        ws_send_text(conn, err, 0);
        json_free(root);
        return;
    }

    // Get params
    JsonValue* params = json_object_get(root, "params");

    // Check browser is ready
    if (!browser_ipc_async_is_ready()) {
        char err[256];
        snprintf(err, sizeof(err),
                 "{\"id\":%lld,\"success\":false,\"error\":\"Browser not ready\"}",
                 (long long)msg_id);
        ws_send_text(conn, err, 0);
        json_free(root);
        return;
    }

    // Get browser method name from tool name
    const char* browser_method = get_browser_method_for_tool(method);
    if (!browser_method) {
        // Try using method name directly (for advanced use)
        browser_method = method;
    }

    // Build params JSON string
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

    // Send command to browser using async IPC (with sync wrapper for simplicity)
    OperationResult result;
    int ret = browser_ipc_async_send_sync(browser_method, params_json, &result);
    free(params_json);

    // Build response
    JsonBuilder resp;
    json_builder_init(&resp);
    json_builder_object_start(&resp);

    if (msg_id >= 0) {
        json_builder_key(&resp, "id");
        json_builder_int(&resp, msg_id);
        json_builder_comma(&resp);
    }

    json_builder_key(&resp, "success");
    json_builder_bool(&resp, ret >= 0 && result.success);

    if (ret >= 0 && result.success && result.data) {
        json_builder_comma(&resp);
        json_builder_key(&resp, "result");
        // Try to parse result.data as JSON
        JsonValue* res_val = json_parse(result.data);
        if (res_val) {
            json_builder_raw(&resp, result.data);
            json_free(res_val);
        } else {
            json_builder_string(&resp, result.data);
        }
    } else {
        json_builder_comma(&resp);
        json_builder_key(&resp, "error");
        if (ret < 0) {
            json_builder_string(&resp, result.error[0] ? result.error : "Command failed");
        } else {
            json_builder_string(&resp, result.error);
        }
    }

    json_builder_object_end(&resp);
    char* response_json = json_builder_finish(&resp);

    ws_send_text(conn, response_json, 0);
    free(response_json);
    free(result.data);
    json_free(root);
}

static void ws_connect_handler(WsConnection* conn) {
    LOG_INFO("WebSocket", "Client connected from %s", ws_get_client_ip(conn));
}

static void ws_disconnect_handler(WsConnection* conn, WsCloseCode code, const char* reason) {
    LOG_INFO("WebSocket", "Client disconnected from %s: %d %s",
             ws_get_client_ip(conn), code, reason ? reason : "");
}

// ============================================================================
// Signal Handling
// ============================================================================

static void signal_handler(int sig) {
    (void)sig;
    LOG_INFO("Main", "Received shutdown signal");
    http_server_stop();
}

static void setup_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Ignore SIGPIPE (broken pipe)
    signal(SIGPIPE, SIG_IGN);
}

// ============================================================================
// Main
// ============================================================================

static void print_banner(void) {
    fprintf(stderr, "\n");
    fprintf(stderr, "  ╔═══════════════════════════════════════════╗\n");
    fprintf(stderr, "  ║     Owl Browser HTTP Server v%s        ║\n", VERSION);
    fprintf(stderr, "  ║     REST API for Browser Automation       ║\n");
    fprintf(stderr, "  ╚═══════════════════════════════════════════╝\n");
    fprintf(stderr, "\n");
}

static void print_version(void) {
    fprintf(stderr, "Owl Browser HTTP Server v%s\n", VERSION);
}

static void print_usage(void) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  owl_http_server [OPTIONS]\n\n");

    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -c, --config <file>         Load config from JSON or YAML file\n");
    fprintf(stderr, "  -h, --help                  Show this help message\n");
    fprintf(stderr, "  -v, --version               Show version information\n");
    fprintf(stderr, "  --generate-config <file>    Generate example config file\n\n");

    fprintf(stderr, "Configuration Priority (highest to lowest):\n");
    fprintf(stderr, "  1. Environment variables\n");
    fprintf(stderr, "  2. Config file (if specified with -c)\n");
    fprintf(stderr, "  3. Default values\n\n");

    fprintf(stderr, "Required Configuration:\n");
    fprintf(stderr, "  OWL_BROWSER_PATH  - Path to owl_browser binary\n");
    fprintf(stderr, "  OWL_HTTP_TOKEN    - Bearer token (if auth_mode=token)\n");
    fprintf(stderr, "  OWL_JWT_PUBLIC_KEY - RSA public key (if auth_mode=jwt)\n\n");

    fprintf(stderr, "Authentication:\n");
    fprintf(stderr, "  OWL_AUTH_MODE          - 'token' or 'jwt' (default: token)\n");
    fprintf(stderr, "  OWL_HTTP_TOKEN         - Bearer token (for token mode)\n\n");

    fprintf(stderr, "JWT Authentication (when OWL_AUTH_MODE=jwt):\n");
    fprintf(stderr, "  OWL_JWT_PUBLIC_KEY     - Path to RSA public key (.pem)\n");
    fprintf(stderr, "  OWL_JWT_PRIVATE_KEY    - Path to RSA private key (optional)\n");
    fprintf(stderr, "  OWL_JWT_ALGORITHM      - RS256, RS384, RS512 (default: RS256)\n");
    fprintf(stderr, "  OWL_JWT_ISSUER         - Expected token issuer (optional)\n");
    fprintf(stderr, "  OWL_JWT_AUDIENCE       - Expected audience (optional)\n");
    fprintf(stderr, "  OWL_JWT_CLOCK_SKEW     - Clock skew in seconds (default: 60)\n\n");

    fprintf(stderr, "Server Settings:\n");
    fprintf(stderr, "  OWL_HTTP_HOST          - Server host (default: 127.0.0.1)\n");
    fprintf(stderr, "  OWL_HTTP_PORT          - Server port (default: 8080)\n");
    fprintf(stderr, "  OWL_HTTP_MAX_CONNECTIONS - Max connections (default: 100)\n");
    fprintf(stderr, "  OWL_HTTP_TIMEOUT       - Request timeout ms (default: 30000)\n");
    fprintf(stderr, "  OWL_BROWSER_TIMEOUT    - Browser timeout ms (default: 60000)\n");
    fprintf(stderr, "  OWL_HTTP_VERBOSE       - Verbose logging (default: false)\n\n");

    fprintf(stderr, "Rate Limiting:\n");
    fprintf(stderr, "  OWL_RATE_LIMIT_ENABLED     - Enable rate limiting (default: false)\n");
    fprintf(stderr, "  OWL_RATE_LIMIT_REQUESTS    - Requests per window (default: 100)\n");
    fprintf(stderr, "  OWL_RATE_LIMIT_WINDOW      - Window in seconds (default: 60)\n");
    fprintf(stderr, "  OWL_RATE_LIMIT_BURST       - Burst allowance (default: 20)\n\n");

    fprintf(stderr, "IP Whitelist:\n");
    fprintf(stderr, "  OWL_IP_WHITELIST_ENABLED   - Enable IP whitelist (default: false)\n");
    fprintf(stderr, "  OWL_IP_WHITELIST           - Comma-separated IPs/CIDRs\n\n");

    fprintf(stderr, "SSL/TLS:\n");
    fprintf(stderr, "  OWL_SSL_ENABLED        - Enable HTTPS (default: false)\n");
    fprintf(stderr, "  OWL_SSL_CERT           - Path to certificate file\n");
    fprintf(stderr, "  OWL_SSL_KEY            - Path to private key file\n");
    fprintf(stderr, "  OWL_SSL_CA             - Path to CA bundle (optional)\n");
    fprintf(stderr, "  OWL_SSL_VERIFY_CLIENT  - Require client certs (default: false)\n\n");

    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  # Using environment variables\n");
    fprintf(stderr, "  OWL_HTTP_TOKEN=secret OWL_BROWSER_PATH=./owl_browser ./owl_http_server\n\n");
    fprintf(stderr, "  # Using config file\n");
    fprintf(stderr, "  ./owl_http_server -c /path/to/config.yaml\n\n");
    fprintf(stderr, "  # Generate example config\n");
    fprintf(stderr, "  ./owl_http_server --generate-config config.yaml\n\n");

    fprintf(stderr, "API Endpoints:\n");
    fprintf(stderr, "  GET  /health              - Health check (no auth)\n");
    fprintf(stderr, "  GET  /tools               - List all tools\n");
    fprintf(stderr, "  GET  /tools/{name}        - Tool documentation\n");
    fprintf(stderr, "  POST /execute/{tool_name} - Execute a tool\n");
    fprintf(stderr, "  POST /command             - Raw browser command\n\n");
}

int main(int argc, char* argv[]) {
    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_banner();
            print_usage();
            return 0;
        }
        else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            print_version();
            return 0;
        }
        else if (strcmp(argv[i], "--generate-config") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --generate-config requires a file path\n");
                return 1;
            }
            const char* file_path = argv[i + 1];
            ConfigFormat format = config_detect_format(file_path);
            if (format == CONFIG_FORMAT_UNKNOWN) {
                // Default to YAML for unknown extensions
                format = CONFIG_FORMAT_YAML;
            }
            return config_generate_example(file_path, format);
        }
        else if (strcmp(argv[i], "--config") == 0 || strcmp(argv[i], "-c") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --config requires a file path\n");
                return 1;
            }
            strncpy(g_config_file, argv[i + 1], sizeof(g_config_file) - 1);
            i++;  // Skip next argument
        }
        else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    print_banner();

    // Load configuration in order: defaults -> config file -> environment vars
    // First, set defaults
    if (config_load(&g_config) != 0) {
        fprintf(stderr, "Error: Failed to set default configuration\n");
        return 1;
    }

    // Then, load from config file if specified (overrides defaults)
    if (strlen(g_config_file) > 0) {
        fprintf(stderr, "Loading config from: %s\n", g_config_file);
        if (config_load_file(&g_config, g_config_file) != 0) {
            fprintf(stderr, "Error: Failed to load configuration file\n");
            return 1;
        }
    }

    // Finally, environment variables override config file
    // Re-apply env vars on top of file config
    if (strlen(g_config_file) > 0) {
        // config_load already loaded env vars, but we need to re-apply
        // them after loading the file. This is a simpler approach:
        // Just load env vars again (they were already loaded in config_load)
        // The current implementation already handles this correctly because
        // config_load() sets defaults first, then applies env vars.
        // When we load a file, it overrides defaults but env vars from
        // config_load() were already applied.
        //
        // To properly support: defaults < file < env, we need to reload env vars
        // after loading the file. Let's call config_load again to reapply env vars.
        ServerConfig env_only;
        config_load(&env_only);

        // Now selectively apply non-default env values
        // For simplicity, we'll re-check key environment variables here
        const char* env_val;

        env_val = getenv("OWL_HTTP_HOST");
        if (env_val && strlen(env_val) > 0) {
            strncpy(g_config.host, env_val, sizeof(g_config.host) - 1);
        }
        env_val = getenv("OWL_HTTP_PORT");
        if (env_val && strlen(env_val) > 0) {
            g_config.port = (uint16_t)atoi(env_val);
        }
        env_val = getenv("OWL_HTTP_TOKEN");
        if (env_val && strlen(env_val) > 0) {
            strncpy(g_config.auth_token, env_val, sizeof(g_config.auth_token) - 1);
        }
        env_val = getenv("OWL_BROWSER_PATH");
        if (env_val && strlen(env_val) > 0) {
            strncpy(g_config.browser_path, env_val, sizeof(g_config.browser_path) - 1);
        }
    }

    // Validate configuration
    if (config_validate(&g_config) != 0) {
        fprintf(stderr, "\n");
        print_usage();
        return 1;
    }

    // Initialize logging
    log_init(g_config.verbose);

    if (g_config.verbose) {
        config_print(&g_config);
    }

    // Initialize modules
    LOG_INFO("Main", "Initializing modules...");

    // Initialize authentication (token or JWT)
    if (auth_init_config(&g_config) != 0) {
        LOG_ERROR("Main", "Failed to initialize authentication");
        return 1;
    }
    tools_init();
    license_manager_init(g_config.browser_path);

    // Initialize IPC tests (if enabled)
    if (g_config.ipc_tests.enabled) {
        const char* test_client_path = g_config.ipc_tests.test_client_path;
        const char* reports_dir = g_config.ipc_tests.reports_dir;

        // Use defaults if not set
        if (strlen(test_client_path) == 0) {
            test_client_path = "/app/ipc_test_client";
        }
        if (strlen(reports_dir) == 0) {
            reports_dir = "/app/reports";
        }

        if (ipc_tests_init(test_client_path, g_config.browser_path, reports_dir) != 0) {
            LOG_WARN("Main", "Failed to initialize IPC tests (feature disabled)");
        } else {
            LOG_INFO("Main", "IPC tests enabled: client=%s, reports=%s",
                     test_client_path, reports_dir);
        }
    }

    // Initialize rate limiter
    if (rate_limit_init(&g_config.rate_limit) != 0) {
        LOG_ERROR("Main", "Failed to initialize rate limiter");
        return 1;
    }
    if (g_config.rate_limit.enabled) {
        LOG_INFO("Main", "Rate limiting enabled: %d requests per %d seconds",
                 g_config.rate_limit.requests_per_window,
                 g_config.rate_limit.window_seconds);
    }

    // Initialize IP filter
    if (ip_filter_init(&g_config.ip_whitelist) != 0) {
        LOG_ERROR("Main", "Failed to initialize IP filter");
        rate_limit_shutdown();
        return 1;
    }
    if (g_config.ip_whitelist.enabled) {
        LOG_INFO("Main", "IP whitelist enabled with %d entries",
                 g_config.ip_whitelist.count);
    }

    // Initialize async browser IPC for concurrent command handling
    if (browser_ipc_async_init() != 0) {
        LOG_ERROR("Main", "Failed to initialize async browser IPC");
        ip_filter_shutdown();
        rate_limit_shutdown();
        return 1;
    }

    // Start browser process with async IPC
    LOG_INFO("Main", "Starting browser process with async IPC...");
    bool browser_started = true;
    if (browser_ipc_async_start(g_config.browser_path, g_config.browser_timeout_ms) != 0) {
        AsyncBrowserState state = browser_ipc_async_get_state();
        browser_started = false;

        if (state == ASYNC_BROWSER_LICENSE_ERROR) {
            const AsyncLicenseError* le = browser_ipc_async_get_license_error();
            fprintf(stderr, "\n");
            fprintf(stderr, "╔════════════════════════════════════════════════╗\n");
            fprintf(stderr, "║         LICENSE ERROR - LIMITED MODE           ║\n");
            fprintf(stderr, "╚════════════════════════════════════════════════╝\n");
            fprintf(stderr, "\n");
            fprintf(stderr, "The browser requires a valid license to run.\n");
            fprintf(stderr, "Server will start in LIMITED MODE for license management.\n");
            fprintf(stderr, "\n");
            fprintf(stderr, "Status: %s\n", le->status);
            if (strlen(le->fingerprint) > 0) {
                fprintf(stderr, "Hardware Fingerprint: %s\n", le->fingerprint);
            }
            fprintf(stderr, "\n");
            fprintf(stderr, "Available endpoints in limited mode:\n");
            fprintf(stderr, "  GET  /health                          - Server health\n");
            fprintf(stderr, "  GET  /stats                           - Server stats\n");
            fprintf(stderr, "  POST /execute/browser_get_license_status     - License status\n");
            fprintf(stderr, "  POST /execute/browser_get_hardware_fingerprint - Hardware ID\n");
            fprintf(stderr, "  POST /execute/browser_add_license     - Add license\n");
            fprintf(stderr, "  POST /execute/browser_remove_license  - Remove license\n");
            fprintf(stderr, "\n");
            fprintf(stderr, "Or activate directly via CLI:\n");
            fprintf(stderr, "  %s --license add /path/to/license.olic\n", g_config.browser_path);
            fprintf(stderr, "\n");
            // Continue to start server in limited mode
        } else {
            LOG_ERROR("Main", "Failed to start browser process");
            browser_ipc_async_shutdown();
            ip_filter_shutdown();
            rate_limit_shutdown();
            return 1;
        }
    }

    // Initialize router
    router_init(&g_config);

    // Initialize video streaming
    VideoStreamConfig vs_config = {
        .max_clients = 50,
        .frame_timeout_ms = 5000,
        .poll_interval_ms = 50
    };
    video_stream_init(&vs_config);

    // Initialize WebSocket
    if (ws_init(&g_config) != 0) {
        LOG_ERROR("Main", "Failed to initialize WebSocket");
        video_stream_shutdown();
        router_shutdown();
        browser_ipc_shutdown();
        ip_filter_shutdown();
        rate_limit_shutdown();
        return 1;
    }

    // Set WebSocket handlers
    if (g_config.websocket.enabled) {
        ws_set_message_handler(ws_message_handler);
        ws_set_connect_handler(ws_connect_handler);
        ws_set_disconnect_handler(ws_disconnect_handler);
        LOG_INFO("Main", "WebSocket enabled at /ws");
    }

    // Setup signal handlers
    setup_signal_handlers();

    // Initialize HTTP server
    LOG_INFO("Main", "Starting HTTP server on %s:%d...", g_config.host, g_config.port);

    if (http_server_init(&g_config, router_handle_request) != 0) {
        LOG_ERROR("Main", "Failed to initialize HTTP server");
        browser_ipc_shutdown();
        ip_filter_shutdown();
        rate_limit_shutdown();
        return 1;
    }

    // Print server ready message
    fprintf(stderr, "\n");
    if (browser_started) {
        fprintf(stderr, "Server ready! Accepting connections at:\n");
    } else {
        fprintf(stderr, "Server ready (LIMITED MODE - license required):\n");
    }
    if (g_config.ssl.enabled) {
        fprintf(stderr, "  REST API: https://%s:%d\n", g_config.host, g_config.port);
        if (g_config.websocket.enabled && browser_started) {
            fprintf(stderr, "  WebSocket: wss://%s:%d/ws\n", g_config.host, g_config.port);
        }
    } else {
        fprintf(stderr, "  REST API: http://%s:%d\n", g_config.host, g_config.port);
        if (g_config.websocket.enabled && browser_started) {
            fprintf(stderr, "  WebSocket: ws://%s:%d/ws\n", g_config.host, g_config.port);
        }
    }

    // Print security status
    fprintf(stderr, "\nSecurity features:\n");
    if (g_config.auth_mode == AUTH_MODE_JWT) {
        fprintf(stderr, "  [*] Auth: JWT (%s)\n", g_config.jwt.algorithm);
    } else {
        fprintf(stderr, "  [*] Auth: Bearer Token\n");
    }
    if (g_config.ssl.enabled) {
        fprintf(stderr, "  [*] SSL/TLS enabled\n");
    }
    if (g_config.rate_limit.enabled) {
        fprintf(stderr, "  [*] Rate limiting: %d req/%ds\n",
                g_config.rate_limit.requests_per_window,
                g_config.rate_limit.window_seconds);
    }
    if (g_config.ip_whitelist.enabled) {
        fprintf(stderr, "  [*] IP whitelist: %d entries\n", g_config.ip_whitelist.count);
    }

    fprintf(stderr, "\nPress Ctrl+C to stop.\n\n");

    // Run server (blocking)
    int ret = http_server_run();

    // Shutdown
    LOG_INFO("Main", "Shutting down...");

    http_server_shutdown();
    ws_shutdown();
    video_stream_shutdown();
    router_shutdown();
    browser_ipc_async_shutdown();
    ip_filter_shutdown();
    rate_limit_shutdown();
    ipc_tests_shutdown();
    license_manager_shutdown();
    auth_shutdown();
    log_shutdown();

    fprintf(stderr, "Server stopped.\n");

    return ret;
}
