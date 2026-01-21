/**
 * Owl Browser HTTP Server - Tool Definitions
 *
 * Defines all available browser tools with their parameters,
 * validation rules, and documentation.
 */

#ifndef OWL_HTTP_TOOLS_H
#define OWL_HTTP_TOOLS_H

#include "types.h"
#include "json.h"
#include <stdbool.h>

// ============================================================================
// Tool Registry
// ============================================================================

/**
 * Initialize the tools registry.
 */
void tools_init(void);

/**
 * Get tool definition by name.
 * Returns NULL if tool not found.
 */
const ToolDef* tools_get(const char* tool_name);

/**
 * Get all tool definitions.
 * Returns count of tools.
 */
int tools_get_all(const ToolDef** tools);

/**
 * Check if a tool exists.
 */
bool tools_exists(const char* tool_name);

// ============================================================================
// Validation
// ============================================================================

/**
 * Validation result with detailed errors.
 */
typedef struct {
    bool valid;
    ValidationError errors[32];
    int error_count;
    char missing_fields[1024];      // Comma-separated list of missing required fields
    char unknown_fields[1024];      // Comma-separated list of unknown fields
    char supported_fields[2048];    // Documentation of supported fields
} ValidationResult;

/**
 * Validate request parameters against tool definition.
 *
 * @param tool_name Name of the tool
 * @param params JSON object with parameters
 * @param result Output validation result
 * @return true if valid, false otherwise
 */
bool tools_validate(const char* tool_name, const JsonValue* params,
                    ValidationResult* result);

/**
 * Generate error response JSON for validation failure.
 * Caller must free the result.
 */
char* tools_validation_error_json(const char* tool_name,
                                   const ValidationResult* result);

/**
 * Generate documentation JSON for a tool.
 * Caller must free the result.
 */
char* tools_get_documentation(const char* tool_name);

/**
 * Generate documentation JSON for all tools.
 * Caller must free the result.
 */
char* tools_get_all_documentation(void);

// ============================================================================
// Tool Name Constants
// ============================================================================

// Context Management
#define TOOL_CREATE_CONTEXT        "browser_create_context"
#define TOOL_CLOSE_CONTEXT         "browser_close_context"
#define TOOL_LIST_CONTEXTS         "browser_list_contexts"

// Navigation
#define TOOL_NAVIGATE              "browser_navigate"
#define TOOL_RELOAD                "browser_reload"
#define TOOL_GO_BACK               "browser_go_back"
#define TOOL_GO_FORWARD            "browser_go_forward"
#define TOOL_CAN_GO_BACK           "browser_can_go_back"
#define TOOL_CAN_GO_FORWARD        "browser_can_go_forward"

// Interaction
#define TOOL_CLICK                 "browser_click"
#define TOOL_TYPE                  "browser_type"
#define TOOL_PICK                  "browser_pick"
#define TOOL_PRESS_KEY             "browser_press_key"
#define TOOL_SUBMIT_FORM           "browser_submit_form"
#define TOOL_DRAG_DROP             "browser_drag_drop"
#define TOOL_HTML5_DRAG_DROP       "browser_html5_drag_drop"
#define TOOL_MOUSE_MOVE            "browser_mouse_move"
#define TOOL_HOVER                 "browser_hover"
#define TOOL_DOUBLE_CLICK          "browser_double_click"
#define TOOL_RIGHT_CLICK           "browser_right_click"
#define TOOL_CLEAR_INPUT           "browser_clear_input"
#define TOOL_FOCUS                 "browser_focus"
#define TOOL_BLUR                  "browser_blur"
#define TOOL_SELECT_ALL            "browser_select_all"
#define TOOL_KEYBOARD_COMBO        "browser_keyboard_combo"
#define TOOL_UPLOAD_FILE           "browser_upload_file"

// Element State Checks
#define TOOL_IS_VISIBLE            "browser_is_visible"
#define TOOL_IS_ENABLED            "browser_is_enabled"
#define TOOL_IS_CHECKED            "browser_is_checked"
#define TOOL_GET_ATTRIBUTE         "browser_get_attribute"
#define TOOL_GET_BOUNDING_BOX      "browser_get_bounding_box"

// Element Picker (for UI overlays)
#define TOOL_GET_ELEMENT_AT_POSITION     "browser_get_element_at_position"
#define TOOL_GET_INTERACTIVE_ELEMENTS    "browser_get_interactive_elements"
#define TOOL_GET_BLOCKER_STATS           "browser_get_blocker_stats"

// JavaScript Evaluation
#define TOOL_EVALUATE              "browser_evaluate"

// Clipboard Management
#define TOOL_CLIPBOARD_READ        "browser_clipboard_read"
#define TOOL_CLIPBOARD_WRITE       "browser_clipboard_write"
#define TOOL_CLIPBOARD_CLEAR       "browser_clipboard_clear"

// Frame Handling
#define TOOL_LIST_FRAMES           "browser_list_frames"
#define TOOL_SWITCH_TO_FRAME       "browser_switch_to_frame"
#define TOOL_SWITCH_TO_MAIN_FRAME  "browser_switch_to_main_frame"

// Content Extraction
#define TOOL_EXTRACT_TEXT          "browser_extract_text"
#define TOOL_SCREENSHOT            "browser_screenshot"
#define TOOL_HIGHLIGHT             "browser_highlight"
#define TOOL_SHOW_GRID_OVERLAY     "browser_show_grid_overlay"
#define TOOL_GET_HTML              "browser_get_html"
#define TOOL_GET_MARKDOWN          "browser_get_markdown"
#define TOOL_EXTRACT_JSON          "browser_extract_json"
#define TOOL_DETECT_SITE           "browser_detect_site"
#define TOOL_LIST_TEMPLATES        "browser_list_templates"

// AI/LLM Features
#define TOOL_SUMMARIZE_PAGE        "browser_summarize_page"
#define TOOL_QUERY_PAGE            "browser_query_page"
#define TOOL_LLM_STATUS            "browser_llm_status"
#define TOOL_NLA                   "browser_nla"
#define TOOL_AI_CLICK              "browser_ai_click"
#define TOOL_AI_TYPE               "browser_ai_type"
#define TOOL_AI_EXTRACT            "browser_ai_extract"
#define TOOL_AI_QUERY              "browser_ai_query"
#define TOOL_AI_ANALYZE            "browser_ai_analyze"
#define TOOL_FIND_ELEMENT          "browser_find_element"

// Scroll Control
#define TOOL_SCROLL_BY             "browser_scroll_by"
#define TOOL_SCROLL_TO_ELEMENT     "browser_scroll_to_element"
#define TOOL_SCROLL_TO_TOP         "browser_scroll_to_top"
#define TOOL_SCROLL_TO_BOTTOM      "browser_scroll_to_bottom"

// Wait Utilities
#define TOOL_WAIT_FOR_SELECTOR     "browser_wait_for_selector"
#define TOOL_WAIT                  "browser_wait"
#define TOOL_WAIT_FOR_NETWORK_IDLE "browser_wait_for_network_idle"
#define TOOL_WAIT_FOR_FUNCTION     "browser_wait_for_function"
#define TOOL_WAIT_FOR_URL          "browser_wait_for_url"

// Page Info
#define TOOL_GET_PAGE_INFO         "browser_get_page_info"
#define TOOL_SET_VIEWPORT          "browser_set_viewport"

// DOM Zoom
#define TOOL_ZOOM_IN               "browser_zoom_in"
#define TOOL_ZOOM_OUT              "browser_zoom_out"
#define TOOL_ZOOM_RESET            "browser_zoom_reset"

// Console Logs
#define TOOL_GET_CONSOLE_LOG       "browser_get_console_log"
#define TOOL_CLEAR_CONSOLE_LOG     "browser_clear_console_log"

// Video Recording
#define TOOL_START_VIDEO           "browser_start_video_recording"
#define TOOL_PAUSE_VIDEO           "browser_pause_video_recording"
#define TOOL_RESUME_VIDEO          "browser_resume_video_recording"
#define TOOL_STOP_VIDEO            "browser_stop_video_recording"
#define TOOL_GET_VIDEO_STATS       "browser_get_video_recording_stats"

// Live Video Streaming
#define TOOL_START_LIVE_STREAM     "browser_start_live_stream"
#define TOOL_STOP_LIVE_STREAM      "browser_stop_live_stream"
#define TOOL_GET_LIVE_STREAM_STATS "browser_get_live_stream_stats"
#define TOOL_LIST_LIVE_STREAMS     "browser_list_live_streams"
#define TOOL_GET_LIVE_FRAME        "browser_get_live_frame"

// Demographics
#define TOOL_GET_DEMOGRAPHICS      "browser_get_demographics"
#define TOOL_GET_LOCATION          "browser_get_location"
#define TOOL_GET_DATETIME          "browser_get_datetime"
#define TOOL_GET_WEATHER           "browser_get_weather"

// CAPTCHA
#define TOOL_DETECT_CAPTCHA        "browser_detect_captcha"
#define TOOL_CLASSIFY_CAPTCHA      "browser_classify_captcha"
#define TOOL_SOLVE_TEXT_CAPTCHA    "browser_solve_text_captcha"
#define TOOL_SOLVE_IMAGE_CAPTCHA   "browser_solve_image_captcha"
#define TOOL_SOLVE_CAPTCHA         "browser_solve_captcha"

// Cookies
#define TOOL_GET_COOKIES           "browser_get_cookies"
#define TOOL_SET_COOKIE            "browser_set_cookie"
#define TOOL_DELETE_COOKIES        "browser_delete_cookies"

// Proxy
#define TOOL_SET_PROXY             "browser_set_proxy"
#define TOOL_GET_PROXY_STATUS      "browser_get_proxy_status"
#define TOOL_CONNECT_PROXY         "browser_connect_proxy"
#define TOOL_DISCONNECT_PROXY      "browser_disconnect_proxy"

// Profile
#define TOOL_CREATE_PROFILE        "browser_create_profile"
#define TOOL_LOAD_PROFILE          "browser_load_profile"
#define TOOL_SAVE_PROFILE          "browser_save_profile"
#define TOOL_GET_PROFILE           "browser_get_profile"
#define TOOL_UPDATE_PROFILE_COOKIES "browser_update_profile_cookies"
#define TOOL_GET_CONTEXT_INFO      "browser_get_context_info"

// Network Interception
#define TOOL_ADD_NETWORK_RULE      "browser_add_network_rule"
#define TOOL_REMOVE_NETWORK_RULE   "browser_remove_network_rule"
#define TOOL_ENABLE_NETWORK_INTERCEPTION "browser_enable_network_interception"
#define TOOL_ENABLE_NETWORK_LOGGING "browser_enable_network_logging"
#define TOOL_GET_NETWORK_LOG       "browser_get_network_log"
#define TOOL_CLEAR_NETWORK_LOG     "browser_clear_network_log"

// File Downloads
#define TOOL_SET_DOWNLOAD_PATH     "browser_set_download_path"
#define TOOL_GET_DOWNLOADS         "browser_get_downloads"
#define TOOL_GET_ACTIVE_DOWNLOADS  "browser_get_active_downloads"
#define TOOL_WAIT_FOR_DOWNLOAD     "browser_wait_for_download"
#define TOOL_CANCEL_DOWNLOAD       "browser_cancel_download"

// Dialog Handling
#define TOOL_SET_DIALOG_ACTION     "browser_set_dialog_action"
#define TOOL_GET_PENDING_DIALOG    "browser_get_pending_dialog"
#define TOOL_GET_DIALOGS           "browser_get_dialogs"
#define TOOL_HANDLE_DIALOG         "browser_handle_dialog"
#define TOOL_WAIT_FOR_DIALOG       "browser_wait_for_dialog"

// Tab/Window Management
#define TOOL_SET_POPUP_POLICY      "browser_set_popup_policy"
#define TOOL_GET_TABS              "browser_get_tabs"
#define TOOL_SWITCH_TAB            "browser_switch_tab"
#define TOOL_CLOSE_TAB             "browser_close_tab"
#define TOOL_NEW_TAB               "browser_new_tab"
#define TOOL_GET_ACTIVE_TAB        "browser_get_active_tab"
#define TOOL_GET_TAB_COUNT         "browser_get_tab_count"
#define TOOL_GET_BLOCKED_POPUPS    "browser_get_blocked_popups"

// License Management
#define TOOL_GET_LICENSE_STATUS    "browser_get_license_status"
#define TOOL_GET_LICENSE_INFO      "browser_get_license_info"
#define TOOL_GET_HARDWARE_FINGERPRINT "browser_get_hardware_fingerprint"
#define TOOL_ADD_LICENSE           "browser_add_license"
#define TOOL_REMOVE_LICENSE        "browser_remove_license"

// IPC Tests
#define TOOL_IPC_TESTS_RUN         "ipc_tests_run"
#define TOOL_IPC_TESTS_STATUS      "ipc_tests_status"
#define TOOL_IPC_TESTS_ABORT       "ipc_tests_abort"
#define TOOL_IPC_TESTS_LIST_REPORTS "ipc_tests_list_reports"
#define TOOL_IPC_TESTS_GET_REPORT  "ipc_tests_get_report"
#define TOOL_IPC_TESTS_DELETE_REPORT "ipc_tests_delete_report"
#define TOOL_IPC_TESTS_CLEAN_ALL   "ipc_tests_clean_all"

#endif // OWL_HTTP_TOOLS_H
