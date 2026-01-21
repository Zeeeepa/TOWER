/**
 * Owl Browser HTTP Server - API Playground
 *
 * Beautiful API playground UI for exploring and testing browser tools.
 * Self-contained HTML/CSS/JS with tool schema translation layer.
 */

#include "playground.h"
#include "tools.h"
#include "json.h"
#include "auth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Tool Categories for UI Organization
// ============================================================================

typedef struct {
    const char* id;
    const char* name;
    const char* icon;
    const char** tools;
    int tool_count;
} ToolCategory;

// Context Management
static const char* CONTEXT_TOOLS[] = {
    TOOL_CREATE_CONTEXT, TOOL_CLOSE_CONTEXT, TOOL_LIST_CONTEXTS, TOOL_GET_CONTEXT_INFO, NULL
};

// Navigation
static const char* NAV_TOOLS[] = {
    TOOL_NAVIGATE, TOOL_RELOAD, TOOL_GO_BACK, TOOL_GO_FORWARD,
    TOOL_CAN_GO_BACK, TOOL_CAN_GO_FORWARD, NULL
};

// Interaction
static const char* INTERACTION_TOOLS[] = {
    TOOL_CLICK, TOOL_TYPE, TOOL_PICK, TOOL_PRESS_KEY, TOOL_SUBMIT_FORM,
    TOOL_DRAG_DROP, TOOL_HTML5_DRAG_DROP, TOOL_MOUSE_MOVE, NULL
};

// Content Extraction
static const char* CONTENT_TOOLS[] = {
    TOOL_EXTRACT_TEXT, TOOL_SCREENSHOT, TOOL_HIGHLIGHT, TOOL_SHOW_GRID_OVERLAY,
    TOOL_GET_HTML, TOOL_GET_MARKDOWN, TOOL_EXTRACT_JSON, TOOL_DETECT_SITE, TOOL_LIST_TEMPLATES, NULL
};

// AI/LLM Features
static const char* AI_TOOLS[] = {
    TOOL_SUMMARIZE_PAGE, TOOL_QUERY_PAGE, TOOL_LLM_STATUS, TOOL_NLA,
    TOOL_AI_CLICK, TOOL_AI_TYPE, TOOL_AI_EXTRACT, TOOL_AI_QUERY,
    TOOL_AI_ANALYZE, TOOL_FIND_ELEMENT, NULL
};

// Scroll Control
static const char* SCROLL_TOOLS[] = {
    TOOL_SCROLL_BY, TOOL_SCROLL_TO_ELEMENT, TOOL_SCROLL_TO_TOP, TOOL_SCROLL_TO_BOTTOM, NULL
};

// Wait Utilities
static const char* WAIT_TOOLS[] = {
    TOOL_WAIT_FOR_SELECTOR, TOOL_WAIT, TOOL_WAIT_FOR_NETWORK_IDLE, TOOL_WAIT_FOR_FUNCTION, TOOL_WAIT_FOR_URL, NULL
};

// Page Info
static const char* PAGE_TOOLS[] = {
    TOOL_GET_PAGE_INFO, TOOL_SET_VIEWPORT, NULL
};

// Video Recording
static const char* VIDEO_TOOLS[] = {
    TOOL_START_VIDEO, TOOL_PAUSE_VIDEO, TOOL_RESUME_VIDEO, TOOL_STOP_VIDEO, TOOL_GET_VIDEO_STATS,
    TOOL_START_LIVE_STREAM, TOOL_STOP_LIVE_STREAM, TOOL_GET_LIVE_STREAM_STATS, TOOL_LIST_LIVE_STREAMS,
    TOOL_GET_LIVE_FRAME, NULL
};

// Demographics
static const char* DEMOGRAPHICS_TOOLS[] = {
    TOOL_GET_DEMOGRAPHICS, TOOL_GET_LOCATION, TOOL_GET_DATETIME, TOOL_GET_WEATHER, NULL
};

// CAPTCHA
static const char* CAPTCHA_TOOLS[] = {
    TOOL_DETECT_CAPTCHA, TOOL_CLASSIFY_CAPTCHA, TOOL_SOLVE_TEXT_CAPTCHA,
    TOOL_SOLVE_IMAGE_CAPTCHA, TOOL_SOLVE_CAPTCHA, NULL
};

// Cookies
static const char* COOKIE_TOOLS[] = {
    TOOL_GET_COOKIES, TOOL_SET_COOKIE, TOOL_DELETE_COOKIES, NULL
};

// Proxy
static const char* PROXY_TOOLS[] = {
    TOOL_SET_PROXY, TOOL_GET_PROXY_STATUS, TOOL_CONNECT_PROXY, TOOL_DISCONNECT_PROXY, NULL
};

// Profile
static const char* PROFILE_TOOLS[] = {
    TOOL_CREATE_PROFILE, TOOL_LOAD_PROFILE, TOOL_SAVE_PROFILE, TOOL_GET_PROFILE,
    TOOL_UPDATE_PROFILE_COOKIES, NULL
};

// Network Interception
static const char* NETWORK_TOOLS[] = {
    TOOL_ADD_NETWORK_RULE, TOOL_REMOVE_NETWORK_RULE, TOOL_ENABLE_NETWORK_INTERCEPTION,
    TOOL_GET_NETWORK_LOG, TOOL_CLEAR_NETWORK_LOG, TOOL_ENABLE_NETWORK_LOGGING, NULL
};

// File Downloads
static const char* DOWNLOAD_TOOLS[] = {
    TOOL_SET_DOWNLOAD_PATH, TOOL_GET_DOWNLOADS, TOOL_GET_ACTIVE_DOWNLOADS,
    TOOL_WAIT_FOR_DOWNLOAD, TOOL_CANCEL_DOWNLOAD, NULL
};

// Dialog Handling
static const char* DIALOG_TOOLS[] = {
    TOOL_SET_DIALOG_ACTION, TOOL_GET_PENDING_DIALOG, TOOL_GET_DIALOGS,
    TOOL_HANDLE_DIALOG, TOOL_WAIT_FOR_DIALOG, NULL
};

// Tab/Window Management
static const char* TAB_TOOLS[] = {
    TOOL_SET_POPUP_POLICY, TOOL_GET_TABS, TOOL_SWITCH_TAB, TOOL_CLOSE_TAB,
    TOOL_NEW_TAB, TOOL_GET_ACTIVE_TAB, TOOL_GET_TAB_COUNT, TOOL_GET_BLOCKED_POPUPS, NULL
};

// Element Picker (for UI overlays)
static const char* ELEMENT_PICKER_TOOLS[] = {
    TOOL_GET_ELEMENT_AT_POSITION, TOOL_GET_INTERACTIVE_ELEMENTS, TOOL_GET_BLOCKER_STATS, NULL
};

// License Management
static const char* LICENSE_TOOLS[] = {
    TOOL_GET_LICENSE_STATUS, TOOL_GET_LICENSE_INFO, TOOL_GET_HARDWARE_FINGERPRINT,
    TOOL_ADD_LICENSE, TOOL_REMOVE_LICENSE, NULL
};

// JavaScript Evaluation
static const char* JAVASCRIPT_TOOLS[] = {
    TOOL_EVALUATE, NULL
};

// Clipboard Management
static const char* CLIPBOARD_TOOLS[] = {
    TOOL_CLIPBOARD_READ, TOOL_CLIPBOARD_WRITE, TOOL_CLIPBOARD_CLEAR, NULL
};

// DOM Zoom
static const char* ZOOM_TOOLS[] = {
    TOOL_ZOOM_IN, TOOL_ZOOM_OUT, TOOL_ZOOM_RESET, NULL
};

// Console Logs
static const char* CONSOLE_TOOLS[] = {
    TOOL_GET_CONSOLE_LOG, TOOL_CLEAR_CONSOLE_LOG, NULL
};

static int count_tools(const char** tools) {
    int count = 0;
    while (tools[count]) count++;
    return count;
}

static const ToolCategory CATEGORIES[] = {
    {"context", "Context", "window", CONTEXT_TOOLS, 0},
    {"navigation", "Navigation", "compass", NAV_TOOLS, 0},
    {"interaction", "Interaction", "pointer", INTERACTION_TOOLS, 0},
    {"content", "Content", "file-text", CONTENT_TOOLS, 0},
    {"ai", "AI & LLM", "brain", AI_TOOLS, 0},
    {"scroll", "Scroll", "arrow-down", SCROLL_TOOLS, 0},
    {"wait", "Wait", "clock", WAIT_TOOLS, 0},
    {"page", "Page Info", "layout", PAGE_TOOLS, 0},
    {"video", "Video", "video", VIDEO_TOOLS, 0},
    {"demographics", "Demographics", "map-pin", DEMOGRAPHICS_TOOLS, 0},
    {"captcha", "CAPTCHA", "shield", CAPTCHA_TOOLS, 0},
    {"cookies", "Cookies", "database", COOKIE_TOOLS, 0},
    {"proxy", "Proxy", "globe", PROXY_TOOLS, 0},
    {"profile", "Profile", "user", PROFILE_TOOLS, 0},
    {"network", "Network", "wifi", NETWORK_TOOLS, 0},
    {"downloads", "Downloads", "download", DOWNLOAD_TOOLS, 0},
    {"dialogs", "Dialogs", "message-square", DIALOG_TOOLS, 0},
    {"tabs", "Tabs", "layers", TAB_TOOLS, 0},
    {"element_picker", "Element Picker", "crosshair", ELEMENT_PICKER_TOOLS, 0},
    {"license", "License", "key", LICENSE_TOOLS, 0},
    {"javascript", "JavaScript", "code", JAVASCRIPT_TOOLS, 0},
    {"clipboard", "Clipboard", "clipboard", CLIPBOARD_TOOLS, 0},
    {"zoom", "DOM Zoom", "zoom-in", ZOOM_TOOLS, 0},
    {"console", "Console Logs", "terminal", CONSOLE_TOOLS, 0},
};

static const int CATEGORY_COUNT = sizeof(CATEGORIES) / sizeof(CATEGORIES[0]);

// ============================================================================
// Tool Schema Generation (Translation Layer)
// ============================================================================

char* playground_get_tool_schema(void) {
    JsonBuilder builder;
    json_builder_init(&builder);

    json_builder_object_start(&builder);

    // Add auth configuration info
    json_builder_key(&builder, "auth");
    json_builder_object_start(&builder);
    json_builder_key(&builder, "enabled");
    json_builder_bool(&builder, auth_is_enabled());
    json_builder_comma(&builder);
    json_builder_key(&builder, "mode");
    json_builder_string(&builder, auth_get_mode() == AUTH_MODE_JWT ? "jwt" : "token");
    json_builder_object_end(&builder);
    json_builder_comma(&builder);

    // Add categories with their tools
    json_builder_key(&builder, "categories");
    json_builder_array_start(&builder);

    for (int i = 0; i < CATEGORY_COUNT; i++) {
        if (i > 0) json_builder_comma(&builder);

        json_builder_object_start(&builder);
        json_builder_key(&builder, "id");
        json_builder_string(&builder, CATEGORIES[i].id);
        json_builder_comma(&builder);
        json_builder_key(&builder, "name");
        json_builder_string(&builder, CATEGORIES[i].name);
        json_builder_comma(&builder);
        json_builder_key(&builder, "icon");
        json_builder_string(&builder, CATEGORIES[i].icon);
        json_builder_comma(&builder);
        json_builder_key(&builder, "tools");
        json_builder_array_start(&builder);

        bool first_tool = true;
        for (int j = 0; CATEGORIES[i].tools[j]; j++) {
            if (!first_tool) json_builder_comma(&builder);
            first_tool = false;
            json_builder_string(&builder, CATEGORIES[i].tools[j]);
        }

        json_builder_array_end(&builder);
        json_builder_object_end(&builder);
    }

    json_builder_array_end(&builder);

    // Add full tool definitions
    json_builder_comma(&builder);
    json_builder_key(&builder, "tools");
    json_builder_object_start(&builder);

    const ToolDef* tools;
    int tool_count = tools_get_all(&tools);

    for (int i = 0; i < tool_count; i++) {
        if (i > 0) json_builder_comma(&builder);

        json_builder_key(&builder, tools[i].name);
        json_builder_object_start(&builder);

        json_builder_key(&builder, "name");
        json_builder_string(&builder, tools[i].name);
        json_builder_comma(&builder);
        json_builder_key(&builder, "description");
        json_builder_string(&builder, tools[i].description);
        json_builder_comma(&builder);
        json_builder_key(&builder, "endpoint");

        char endpoint[256];
        snprintf(endpoint, sizeof(endpoint), "/execute/%s", tools[i].name);
        json_builder_string(&builder, endpoint);

        json_builder_comma(&builder);
        json_builder_key(&builder, "method");
        json_builder_string(&builder, "POST");

        json_builder_comma(&builder);
        json_builder_key(&builder, "parameters");
        json_builder_array_start(&builder);

        for (int p = 0; p < tools[i].param_count; p++) {
            if (p > 0) json_builder_comma(&builder);

            json_builder_object_start(&builder);
            json_builder_key(&builder, "name");
            json_builder_string(&builder, tools[i].params[p].name);
            json_builder_comma(&builder);

            json_builder_key(&builder, "type");
            const char* type_str;
            switch (tools[i].params[p].type) {
                case PARAM_STRING: type_str = "string"; break;
                case PARAM_INT: type_str = "integer"; break;
                case PARAM_NUMBER: type_str = "number"; break;
                case PARAM_BOOL: type_str = "boolean"; break;
                case PARAM_ENUM: type_str = "enum"; break;
                default: type_str = "string";
            }
            json_builder_string(&builder, type_str);
            json_builder_comma(&builder);

            json_builder_key(&builder, "required");
            json_builder_bool(&builder, tools[i].params[p].required);
            json_builder_comma(&builder);

            json_builder_key(&builder, "description");
            json_builder_string(&builder, tools[i].params[p].description ? tools[i].params[p].description : "");

            // Add enum values if applicable
            if (tools[i].params[p].type == PARAM_ENUM && tools[i].params[p].enum_values) {
                json_builder_comma(&builder);
                json_builder_key(&builder, "enum");
                json_builder_array_start(&builder);
                for (int e = 0; e < tools[i].params[p].enum_count && tools[i].params[p].enum_values[e]; e++) {
                    if (e > 0) json_builder_comma(&builder);
                    json_builder_string(&builder, tools[i].params[p].enum_values[e]);
                }
                json_builder_array_end(&builder);
            }

            json_builder_object_end(&builder);
        }

        json_builder_array_end(&builder);
        json_builder_object_end(&builder);
    }

    json_builder_object_end(&builder);
    json_builder_object_end(&builder);

    return json_builder_finish(&builder);
}

// ============================================================================
// Embedded HTML/CSS/JS Playground UI
// ============================================================================

static const char PLAYGROUND_HTML[] =
"<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"<meta charset=\"UTF-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"<title>Owl Browser API Playground</title>\n"
"<style>\n"
":root {\n"
"  --bg-primary: #0f0f0f;\n"
"  --bg-secondary: #1a1a1a;\n"
"  --bg-tertiary: #242424;\n"
"  --bg-hover: #2a2a2a;\n"
"  --text-primary: #ffffff;\n"
"  --text-secondary: #a0a0a0;\n"
"  --text-muted: #666666;\n"
"  --accent: #6BA894;\n"
"  --accent-hover: #7dbba6;\n"
"  --success: #22c55e;\n"
"  --error: #ef4444;\n"
"  --warning: #f59e0b;\n"
"  --border: #333333;\n"
"  --border-focus: #6BA894;\n"
"}\n"
"\n"
"* { margin: 0; padding: 0; box-sizing: border-box; }\n"
"body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: var(--bg-primary); color: var(--text-primary); line-height: 1.6; }\n"
"\n"
".container { display: flex; height: 100vh; }\n"
"\n"
"/* Sidebar */\n"
".sidebar { width: 280px; background: var(--bg-secondary); border-right: 1px solid var(--border); display: flex; flex-direction: column; flex-shrink: 0; }\n"
".sidebar-header { padding: 20px; border-bottom: 1px solid var(--border); }\n"
".logo { display: flex; align-items: center; gap: 12px; margin-bottom: 16px; }\n"
".logo svg { width: 32px; height: 32px; color: var(--accent); }\n"
".logo h1 { font-size: 18px; font-weight: 600; }\n"
".search-box { position: relative; }\n"
".search-box input { width: 100%; padding: 10px 12px 10px 36px; background: var(--bg-tertiary); border: 1px solid var(--border); border-radius: 8px; color: var(--text-primary); font-size: 14px; outline: none; transition: border-color 0.2s; }\n"
".search-box input:focus { border-color: var(--border-focus); }\n"
".search-box svg { position: absolute; left: 12px; top: 50%; transform: translateY(-50%); width: 16px; height: 16px; color: var(--text-muted); }\n"
"\n"
".sidebar-content { flex: 1; overflow-y: auto; padding: 12px; }\n"
".category { margin-bottom: 8px; }\n"
".category-header { display: flex; align-items: center; gap: 8px; padding: 8px 12px; border-radius: 6px; cursor: pointer; color: var(--text-secondary); font-size: 13px; font-weight: 500; text-transform: uppercase; letter-spacing: 0.5px; transition: background 0.2s; }\n"
".category-header:hover { background: var(--bg-hover); }\n"
".category-header svg { width: 16px; height: 16px; }\n"
".category-tools { display: none; padding-left: 8px; }\n"
".category.expanded .category-tools { display: block; }\n"
".category-header .chevron { transition: transform 0.2s; margin-right: 8px; }\n"
".category.expanded .chevron { transform: rotate(90deg); }\n"
"\n"
"/* Favorites category */\n"
".favorites-category .category-header { color: #f59e0b; }\n"
".favorites-category .star-icon { width: 14px; height: 14px; color: #f59e0b; }\n"
".favorite-count { font-size: 11px; padding: 1px 6px; background: rgba(245, 158, 11, 0.2); color: #f59e0b; border-radius: 10px; margin-left: auto; }\n"
".favorite-item { color: var(--text-primary) !important; }\n"
".favorite-item .item-star { width: 12px; height: 12px; color: #f59e0b; margin-right: 6px; flex-shrink: 0; }\n"
".tool-item.is-favorite { position: relative; }\n"
".tool-item.is-favorite::after { content: ''; position: absolute; right: 8px; width: 6px; height: 6px; background: #f59e0b; border-radius: 50%; }\n"
"\n"
".tool-item { display: flex; align-items: center; padding: 8px 12px; border-radius: 6px; cursor: pointer; font-size: 13px; color: var(--text-secondary); transition: all 0.2s; margin: 2px 0; }\n"
".tool-item:hover { background: var(--bg-hover); color: var(--text-primary); }\n"
".tool-item.active { background: var(--accent); color: white; }\n"
"\n"
"/* Main Content */\n"
".main { flex: 1; display: flex; flex-direction: column; overflow: hidden; }\n"
".main-header { padding: 16px 24px; border-bottom: 1px solid var(--border); display: flex; align-items: center; justify-content: space-between; background: var(--bg-secondary); }\n"
".header-left { display: flex; align-items: center; gap: 16px; }\n"
".status-badge { display: flex; align-items: center; gap: 6px; padding: 6px 12px; border-radius: 20px; font-size: 12px; font-weight: 500; }\n"
".status-badge.healthy { background: rgba(34, 197, 94, 0.15); color: var(--success); }\n"
".status-badge.unhealthy { background: rgba(239, 68, 68, 0.15); color: var(--error); }\n"
".status-dot { width: 8px; height: 8px; border-radius: 50%; }\n"
".status-badge.healthy .status-dot { background: var(--success); }\n"
".status-badge.unhealthy .status-dot { background: var(--error); }\n"
"\n"
".auth-input { display: flex; align-items: center; gap: 8px; position: relative; }\n"
".auth-input input { width: 280px; padding: 8px 12px; background: var(--bg-tertiary); border: 1px solid var(--border); border-radius: 6px; color: var(--text-primary); font-size: 13px; outline: none; }\n"
".auth-input input:focus { border-color: var(--border-focus); }\n"
".auth-input input:disabled { opacity: 0.5; cursor: not-allowed; }\n"
".auth-input.jwt-mode::before { content: 'JWT'; position: absolute; right: 12px; top: 50%; transform: translateY(-50%); font-size: 10px; font-weight: 600; color: var(--accent); background: rgba(99, 102, 241, 0.15); padding: 2px 6px; border-radius: 4px; pointer-events: none; }\n"
".auth-input.jwt-mode input { padding-right: 48px; }\n"
".download-btn { display: flex; align-items: center; gap: 6px; padding: 8px 14px; background: var(--accent); border: none; border-radius: 6px; color: white; font-size: 13px; font-weight: 500; cursor: pointer; transition: all 0.2s; }\n"
".download-btn:hover { background: var(--accent-hover); }\n"
".download-btn svg { width: 16px; height: 16px; }\n"
"\n"
".main-content { flex: 1; display: flex; overflow: hidden; }\n"
"\n"
"/* Tool Panel */\n"
".tool-panel { flex: 1; display: flex; flex-direction: column; overflow: hidden; }\n"
".tool-header { padding: 24px 24px 20px; border-bottom: 1px solid var(--border); background: var(--bg-secondary); }\n"
".tool-title-row { display: flex; align-items: center; gap: 12px; margin-bottom: 12px; }\n"
".tool-title { font-size: 22px; font-weight: 600; color: var(--text-primary); }\n"
".favorite-btn { padding: 6px; background: transparent; border: 1px solid var(--border); border-radius: 6px; cursor: pointer; color: var(--text-tertiary); transition: all 0.2s; display: flex; align-items: center; justify-content: center; }\n"
".favorite-btn:hover { border-color: #f59e0b; color: #f59e0b; background: rgba(245, 158, 11, 0.1); }\n"
".favorite-btn.active { border-color: #f59e0b; color: #f59e0b; background: rgba(245, 158, 11, 0.15); }\n"
".favorite-btn svg { width: 18px; height: 18px; }\n"
".tool-description { color: var(--text-secondary); font-size: 14px; line-height: 1.6; padding: 12px 16px; background: var(--bg-tertiary); border-radius: 8px; border-left: 3px solid var(--accent); margin-bottom: 12px; }\n"
".tool-endpoint { display: inline-flex; align-items: center; gap: 8px; padding: 8px 12px; background: var(--bg-primary); border-radius: 6px; font-family: 'SF Mono', Monaco, monospace; font-size: 13px; border: 1px solid var(--border); }\n"
".method-badge { padding: 2px 8px; border-radius: 4px; font-size: 11px; font-weight: 600; background: var(--accent); color: white; }\n"
"\n"
".tool-body { flex: 1; display: flex; overflow: hidden; }\n"
"\n"
"/* Request Panel */\n"
".request-panel { flex: 1; display: flex; flex-direction: column; border-right: 1px solid var(--border); }\n"
".panel-header { padding: 12px 20px; border-bottom: 1px solid var(--border); font-size: 13px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.5px; color: var(--text-secondary); background: var(--bg-secondary); }\n"
".request-form { flex: 1; padding: 20px; overflow-y: auto; }\n"
"\n"
".form-group { margin-bottom: 24px; padding-bottom: 16px; border-bottom: 1px solid var(--border); }\n"
".form-group:last-of-type { border-bottom: none; }\n"
".form-label { display: flex; align-items: center; gap: 8px; margin-bottom: 8px; font-size: 14px; font-weight: 600; color: var(--text-primary); }\n"
".required-badge { color: var(--error); font-size: 11px; font-weight: 500; }\n"
".type-badge { padding: 3px 8px; border-radius: 4px; font-size: 10px; background: var(--accent); color: white; font-weight: 600; text-transform: uppercase; }\n"
".form-description { font-size: 13px; color: var(--text-secondary); margin-bottom: 10px; line-height: 1.5; padding: 8px 12px; background: rgba(107, 168, 148, 0.08); border-radius: 6px; border-left: 2px solid var(--accent); }\n"
".form-input { width: 100%; padding: 10px 12px; background: var(--bg-tertiary); border: 1px solid var(--border); border-radius: 6px; color: var(--text-primary); font-size: 14px; outline: none; transition: border-color 0.2s; }\n"
".form-input:focus { border-color: var(--border-focus); }\n"
".form-select { width: 100%; padding: 10px 12px; background: var(--bg-tertiary); border: 1px solid var(--border); border-radius: 6px; color: var(--text-primary); font-size: 14px; outline: none; cursor: pointer; }\n"
".form-checkbox { display: flex; align-items: center; gap: 8px; }\n"
".form-checkbox input { width: 18px; height: 18px; cursor: pointer; accent-color: var(--accent); }\n"
".file-input-wrapper { position: relative; }\n"
".form-file-input { position: absolute; width: 100%; height: 100%; opacity: 0; cursor: pointer; }\n"
".file-input-label { display: flex; align-items: center; gap: 10px; padding: 12px 16px; background: var(--bg-tertiary); border: 2px dashed var(--border); border-radius: 8px; color: var(--text-secondary); cursor: pointer; transition: all 0.2s; }\n"
".file-input-label:hover { border-color: var(--accent); color: var(--text-primary); }\n"
".file-input-label svg { width: 20px; height: 20px; }\n"
".file-input-label.has-file { border-color: var(--accent); border-style: solid; background: var(--accent-bg); }\n"
".file-input-label.has-file span { color: var(--accent); }\n"
"\n"
".execute-btn { width: 100%; padding: 14px; background: var(--accent); color: white; border: none; border-radius: 8px; font-size: 14px; font-weight: 600; cursor: pointer; transition: background 0.2s; margin-top: 10px; display: flex; align-items: center; justify-content: center; gap: 8px; }\n"
".execute-btn:hover { background: var(--accent-hover); }\n"
".execute-btn:disabled { opacity: 0.5; cursor: not-allowed; }\n"
".execute-btn svg { width: 18px; height: 18px; }\n"
"\n"
"/* SDK Snippet Section */\n"
".sdk-snippet-section { margin-top: 16px; border-top: 1px solid var(--border); padding-top: 16px; }\n"
".sdk-snippet-header { display: flex; align-items: center; justify-content: space-between; margin-bottom: 12px; }\n"
".sdk-snippet-title { font-size: 0.85rem; font-weight: 600; color: var(--text-primary); display: flex; align-items: center; gap: 6px; }\n"
".sdk-snippet-title svg { width: 16px; height: 16px; stroke: var(--accent); }\n"
".sdk-tabs { display: flex; gap: 4px; }\n"
".sdk-tab { padding: 6px 12px; border: none; background: transparent; color: var(--text-muted); font-size: 0.8rem; cursor: pointer; border-radius: 6px; transition: all 0.2s; display: flex; align-items: center; gap: 6px; }\n"
".sdk-tab:hover { background: var(--bg-hover); color: var(--text-primary); }\n"
".sdk-tab.active { background: var(--accent); color: white; }\n"
".sdk-tab svg { width: 14px; height: 14px; }\n"
".sdk-code-container { position: relative; background: var(--bg-tertiary); border-radius: 8px; overflow: hidden; }\n"
".sdk-code-content { display: none; padding: 16px; overflow-x: auto; max-height: 300px; }\n"
".sdk-code-content.active { display: block; }\n"
".sdk-code-content pre { margin: 0; font-family: 'SF Mono', 'Consolas', monospace; font-size: 0.8rem; line-height: 1.5; color: var(--text-primary); white-space: pre; }\n"
".sdk-copy-btn { position: absolute; top: 8px; right: 8px; padding: 6px 10px; background: var(--bg-hover); border: none; border-radius: 4px; color: var(--text-muted); cursor: pointer; font-size: 0.75rem; display: flex; align-items: center; gap: 4px; transition: all 0.2s; z-index: 1; }\n"
".sdk-copy-btn:hover { background: var(--accent); color: white; }\n"
".sdk-copy-btn svg { width: 12px; height: 12px; }\n"
".sdk-keyword { color: #c678dd; }\n"
".sdk-string { color: #98c379; }\n"
".sdk-comment { color: #5c6370; font-style: italic; }\n"
".sdk-function { color: #61afef; }\n"
".sdk-variable { color: #e5c07b; }\n"
".sdk-property { color: #e06c75; }\n"
".sdk-number { color: #d19a66; }\n"
"\n"
"/* Response Panel */\n"
".response-panel { flex: 1; display: flex; flex-direction: column; background: var(--bg-secondary); overflow-y: auto; }\n"
".response-header { display: flex; align-items: center; justify-content: space-between; padding: 12px 20px; border-bottom: 1px solid var(--border); background: var(--bg-secondary); }\n"
".response-status { display: flex; align-items: center; gap: 8px; }\n"
".response-time { font-size: 12px; color: var(--text-muted); }\n"
".response-body { padding: 16px 20px; overflow: auto; font-family: 'SF Mono', Monaco, monospace; font-size: 13px; line-height: 1.6; white-space: pre-wrap; word-break: break-word; }\n"
".response-snippet-section { border-top: 1px solid var(--border); padding: 16px 20px; }\n"
"\n"
"/* JSON Syntax Highlighting */\n"
".json-key { color: #9cdcfe; }\n"
".json-string { color: #ce9178; }\n"
".json-number { color: #b5cea8; }\n"
".json-boolean { color: #569cd6; }\n"
".json-null { color: #569cd6; }\n"
"\n"
"/* Welcome Screen */\n"
".welcome { flex: 1; display: flex; flex-direction: column; align-items: center; justify-content: center; padding: 40px; text-align: center; }\n"
".welcome svg { width: 80px; height: 80px; color: var(--accent); margin-bottom: 24px; opacity: 0.8; }\n"
".welcome h2 { font-size: 24px; margin-bottom: 12px; }\n"
".welcome p { color: var(--text-secondary); max-width: 400px; }\n"
"\n"
"/* Loading Spinner */\n"
".spinner { width: 20px; height: 20px; border: 2px solid transparent; border-top-color: white; border-radius: 50%; animation: spin 1s linear infinite; }\n"
"@keyframes spin { to { transform: rotate(360deg); } }\n"
"\n"
"/* Empty State */\n"
".empty-params { padding: 40px 20px; text-align: center; color: var(--text-muted); }\n"
".empty-params svg { width: 48px; height: 48px; margin-bottom: 12px; opacity: 0.5; }\n"
"\n"
"/* Copy Button */\n"
".copy-btn { padding: 6px 12px; background: var(--bg-tertiary); border: 1px solid var(--border); border-radius: 6px; color: var(--text-secondary); font-size: 12px; cursor: pointer; transition: all 0.2s; display: flex; align-items: center; gap: 6px; }\n"
".copy-btn:hover { background: var(--bg-hover); color: var(--text-primary); }\n"
".copy-btn svg { width: 14px; height: 14px; }\n"
"\n"
"/* Context Select Wrapper */\n"
".context-select-wrapper { display: flex; gap: 8px; align-items: center; }\n"
".context-select-wrapper .form-select { flex: 1; }\n"
".refresh-btn { padding: 8px; background: var(--bg-tertiary); border: 1px solid var(--border); border-radius: 6px; color: var(--text-secondary); cursor: pointer; transition: all 0.2s; display: flex; align-items: center; justify-content: center; }\n"
".refresh-btn:hover { background: var(--bg-hover); color: var(--text-primary); }\n"
".refresh-btn:active { transform: scale(0.95); }\n"
".refresh-btn svg { width: 16px; height: 16px; }\n"
".refresh-btn.loading svg { animation: spin 1s linear infinite; }\n"
"\n"
"/* Response Tabs */\n"
".response-tabs { display: flex; gap: 4px; }\n"
".response-tab { padding: 6px 14px; background: transparent; border: 1px solid var(--border); border-radius: 6px; color: var(--text-secondary); font-size: 12px; cursor: pointer; transition: all 0.2s; }\n"
".response-tab:hover { background: var(--bg-hover); color: var(--text-primary); }\n"
".response-tab.active { background: var(--accent); border-color: var(--accent); color: white; }\n"
".response-content { display: none; height: 350px; max-height: 350px; overflow: auto; flex-shrink: 0; }\n"
".response-content.active { display: block; }\n"
".preview-image { max-width: 100%; height: auto; border-radius: 8px; }\n"
"/* Image Zoom Controls */\n"
".image-preview-container { position: relative; }\n"
".zoom-controls { position: absolute; top: 8px; right: 8px; display: flex; gap: 4px; z-index: 10; background: rgba(0,0,0,0.6); border-radius: 8px; padding: 4px; }\n"
".zoom-btn { width: 32px; height: 32px; border: none; background: transparent; color: white; cursor: pointer; border-radius: 6px; display: flex; align-items: center; justify-content: center; transition: background 0.2s; }\n"
".zoom-btn:hover { background: rgba(255,255,255,0.2); }\n"
".zoom-btn.active { background: var(--accent); }\n"
".zoom-btn svg { width: 18px; height: 18px; }\n"
".zoom-level { color: white; font-size: 12px; padding: 0 8px; display: flex; align-items: center; min-width: 45px; justify-content: center; }\n"
".image-viewport { overflow: hidden; border-radius: 8px; position: relative; cursor: default; max-height: 70vh; background: white; }\n"
".image-viewport.zoomed { cursor: grab; }\n"
".image-viewport.dragging { cursor: grabbing; }\n"
".image-viewport img { transition: transform 0.2s ease; transform-origin: 0 0; display: block; }\n"
".preview-text { padding: 16px 20px; font-family: 'SF Mono', Monaco, monospace; font-size: 13px; line-height: 1.6; white-space: pre-wrap; word-break: break-word; }\n"
".preview-html { padding: 16px 20px; background: white; color: #333; border-radius: 8px; margin: 16px; overflow: auto; }\n"
".preview-markdown { padding: 16px 20px; line-height: 1.8; }\n"
".preview-markdown h1, .preview-markdown h2, .preview-markdown h3 { margin: 16px 0 8px; }\n"
".preview-markdown p { margin: 8px 0; }\n"
".preview-markdown code { background: var(--bg-tertiary); padding: 2px 6px; border-radius: 4px; font-family: 'SF Mono', Monaco, monospace; }\n"
".preview-markdown pre { background: var(--bg-tertiary); padding: 12px; border-radius: 6px; overflow-x: auto; }\n"
".preview-markdown a { color: var(--accent); }\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<div class=\"container\">\n"
"  <aside class=\"sidebar\">\n"
"    <div class=\"sidebar-header\">\n"
"      <div class=\"logo\">\n"
"        <img src=\"/logo.svg\" width=\"36\" height=\"36\" alt=\"Owl Browser\">\n"
"        <h1>Owl Browser API</h1>\n"
"      </div>\n"
"      <div class=\"search-box\">\n"
"        <svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><circle cx=\"11\" cy=\"11\" r=\"8\"/><path d=\"m21 21-4.35-4.35\"/></svg>\n"
"        <input type=\"text\" id=\"searchInput\" placeholder=\"Search tools...\">\n"
"      </div>\n"
"    </div>\n"
"    <div class=\"sidebar-content\" id=\"toolList\"></div>\n"
"  </aside>\n"
"\n"
"  <main class=\"main\">\n"
"    <header class=\"main-header\">\n"
"      <div class=\"header-left\">\n"
"        <div class=\"status-badge\" id=\"statusBadge\">\n"
"          <span class=\"status-dot\"></span>\n"
"          <span id=\"statusText\">Checking...</span>\n"
"        </div>\n"
"        <button class=\"download-btn\" onclick=\"downloadOpenAPI()\" title=\"Download OpenAPI spec for Postman\">\n"
"          <svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M21 15v4a2 2 0 01-2 2H5a2 2 0 01-2-2v-4M7 10l5 5 5-5M12 15V3\"/></svg>\n"
"          OpenAPI\n"
"        </button>\n"
"      </div>\n"
"      <div class=\"auth-input\">\n"
"        <input type=\"password\" id=\"authToken\" placeholder=\"Loading...\">\n"
"      </div>\n"
"    </header>\n"
"\n"
"    <div class=\"main-content\">\n"
"      <div class=\"tool-panel\" id=\"toolPanel\">\n"
"        <div class=\"welcome\" id=\"welcomeScreen\">\n"
"          <img src=\"/logo.svg\" width=\"80\" height=\"80\" alt=\"Owl Browser\">\n"
"          <h2>Owl Browser API Playground</h2>\n"
"          <p>Select a tool from the sidebar to explore and test the API. Enter your Bearer token above to execute requests.</p>\n"
"        </div>\n"
"        <div id=\"toolContent\" style=\"display: none; flex: 1; overflow: hidden; display: flex; flex-direction: column;\"></div>\n"
"      </div>\n"
"    </div>\n"
"  </main>\n"
"</div>\n"
"\n"
"<script>\n"
"let schema = null;\n"
"let currentTool = null;\n"
"let contextIds = []; // Track created context IDs\n"
"\n"
"// Fetch and initialize\n"
"async function init() {\n"
"  try {\n"
"    const res = await fetch('/api/schema');\n"
"    schema = await res.json();\n"
"    renderToolList();\n"
"    checkHealth();\n"
"    setInterval(checkHealth, 30000);\n"
"    // Apply auth mode UI hints\n"
"    applyAuthMode();\n"
"    // Load existing contexts\n"
"    await loadExistingContexts();\n"
"  } catch (e) {\n"
"    console.error('Failed to load schema:', e);\n"
"  }\n"
"}\n"
"\n"
"// Apply auth mode UI hints based on server configuration\n"
"function applyAuthMode() {\n"
"  const authInput = document.getElementById('authToken');\n"
"  const authWrapper = document.querySelector('.auth-input');\n"
"  if (!schema || !schema.auth) return;\n"
"  \n"
"  if (!schema.auth.enabled) {\n"
"    authInput.placeholder = 'Authentication disabled';\n"
"    authInput.disabled = true;\n"
"    authWrapper.title = 'Server has authentication disabled';\n"
"  } else if (schema.auth.mode === 'jwt') {\n"
"    authInput.placeholder = 'Enter JWT token';\n"
"    authInput.title = 'Server expects JWT authentication. Paste your JWT token here.';\n"
"    authWrapper.classList.add('jwt-mode');\n"
"  } else {\n"
"    authInput.placeholder = 'Enter API token';\n"
"    authInput.title = 'Server expects Bearer token authentication.';\n"
"  }\n"
"}\n"
"\n"
"// Favorites management (localStorage)\n"
"const FAVORITES_KEY = 'owl_playground_favorites';\n"
"\n"
"function getFavorites() {\n"
"  try {\n"
"    return JSON.parse(localStorage.getItem(FAVORITES_KEY)) || [];\n"
"  } catch (e) {\n"
"    return [];\n"
"  }\n"
"}\n"
"\n"
"function isFavorite(toolName) {\n"
"  return getFavorites().includes(toolName);\n"
"}\n"
"\n"
"function toggleFavorite(toolName) {\n"
"  const favorites = getFavorites();\n"
"  const index = favorites.indexOf(toolName);\n"
"  if (index === -1) {\n"
"    favorites.push(toolName);\n"
"  } else {\n"
"    favorites.splice(index, 1);\n"
"  }\n"
"  localStorage.setItem(FAVORITES_KEY, JSON.stringify(favorites));\n"
"  renderToolList(); // Re-render to update favorites category\n"
"  updateFavoriteButton(); // Update button state\n"
"}\n"
"\n"
"function updateFavoriteButton() {\n"
"  if (!currentTool) return;\n"
"  const btn = document.getElementById('favoriteBtn');\n"
"  if (!btn) return;\n"
"  const isFav = isFavorite(currentTool.name);\n"
"  btn.classList.toggle('active', isFav);\n"
"  btn.title = isFav ? 'Remove from favorites' : 'Add to favorites';\n"
"}\n"
"\n"
"// Load existing browser contexts\n"
"async function loadExistingContexts() {\n"
"  const token = document.getElementById('authToken').value;\n"
"  if (!token) return;\n"
"  try {\n"
"    const res = await fetch('/execute/browser_list_contexts', {\n"
"      method: 'POST',\n"
"      headers: { 'Content-Type': 'application/json', 'Authorization': `Bearer ${token}` },\n"
"      body: '{}'\n"
"    });\n"
"    const data = await res.json();\n"
"    if (data.success && Array.isArray(data.result)) {\n"
"      contextIds = data.result;\n"
"    }\n"
"  } catch (e) { /* ignore */ }\n"
"}\n"
"\n"
"// Refresh context list (called from UI button)\n"
"async function refreshContexts() {\n"
"  const btn = document.querySelector('.refresh-btn');\n"
"  if (btn) btn.classList.add('loading');\n"
"  \n"
"  await loadExistingContexts();\n"
"  \n"
"  // Update the dropdown\n"
"  const select = document.getElementById('param_context_id');\n"
"  if (select) {\n"
"    const currentVal = select.value;\n"
"    const options = contextIds.map(id => `<option value=\"${id}\">${id}</option>`).join('');\n"
"    // Auto-select if only one context, otherwise show placeholder\n"
"    const emptyMsg = contextIds.length === 0 ? '<option value=\"\">No contexts - create one first</option>' : (contextIds.length === 1 ? '' : '<option value=\"\">Select context...</option>');\n"
"    select.innerHTML = emptyMsg + options;\n"
"    // Auto-select single context, or restore previous selection if still valid\n"
"    if (contextIds.length === 1) {\n"
"      select.value = contextIds[0];\n"
"    } else if (currentVal && contextIds.includes(currentVal)) {\n"
"      select.value = currentVal;\n"
"    }\n"
"  }\n"
"  \n"
"  if (btn) btn.classList.remove('loading');\n"
"}\n"
"\n"
"// Handle license file selection\n"
"function handleLicenseFile(input) {\n"
"  const file = input.files[0];\n"
"  const filenameEl = document.getElementById('param_license_content_filename');\n"
"  const hiddenInput = document.getElementById('param_license_content');\n"
"  const labelEl = input.nextElementSibling.nextElementSibling;\n"
"  \n"
"  if (file) {\n"
"    // Validate file extension\n"
"    if (!file.name.endsWith('.olic')) {\n"
"      alert('Please select a .olic license file');\n"
"      input.value = '';\n"
"      return;\n"
"    }\n"
"    \n"
"    // Read file content as base64\n"
"    const reader = new FileReader();\n"
"    reader.onload = function(e) {\n"
"      // Extract base64 data (remove 'data:application/octet-stream;base64,' prefix)\n"
"      const base64Data = e.target.result.split(',')[1] || e.target.result;\n"
"      hiddenInput.value = base64Data;\n"
"      filenameEl.textContent = file.name;\n"
"      labelEl.classList.add('has-file');\n"
"    };\n"
"    reader.readAsDataURL(file);\n"
"  } else {\n"
"    hiddenInput.value = '';\n"
"    filenameEl.textContent = 'Choose .olic license file...';\n"
"    labelEl.classList.remove('has-file');\n"
"  }\n"
"}\n"
"\n"
"// Check server health\n"
"async function checkHealth() {\n"
"  const badge = document.getElementById('statusBadge');\n"
"  const text = document.getElementById('statusText');\n"
"  try {\n"
"    const res = await fetch('/health');\n"
"    const data = await res.json();\n"
"    if (data.browser_ready) {\n"
"      badge.className = 'status-badge healthy';\n"
"      text.textContent = 'Browser Ready';\n"
"    } else {\n"
"      badge.className = 'status-badge unhealthy';\n"
"      text.textContent = data.browser_state || 'Not Ready';\n"
"    }\n"
"  } catch (e) {\n"
"    badge.className = 'status-badge unhealthy';\n"
"    text.textContent = 'Disconnected';\n"
"  }\n"
"}\n"
"\n"
"// Render tool list by category\n"
"function renderToolList() {\n"
"  const container = document.getElementById('toolList');\n"
"  let html = '';\n"
"  \n"
"  // Add Favorites category at top if there are any\n"
"  const favorites = getFavorites();\n"
"  if (favorites.length > 0) {\n"
"    html += `<div class=\"category favorites-category expanded\" data-category=\"favorites\">\n"
"      <div class=\"category-header\" onclick=\"toggleCategory('favorites')\">\n"
"        <svg class=\"chevron\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"m9 18 6-6-6-6\"/></svg>\n"
"        <svg class=\"star-icon\" viewBox=\"0 0 24 24\" fill=\"currentColor\"><path d=\"M12 2l3.09 6.26L22 9.27l-5 4.87 1.18 6.88L12 17.77l-6.18 3.25L7 14.14 2 9.27l6.91-1.01L12 2z\"/></svg>\n"
"        <span>Favorites</span>\n"
"        <span class=\"favorite-count\">${favorites.length}</span>\n"
"      </div>\n"
"      <div class=\"category-tools\">`;\n"
"    \n"
"    for (const toolName of favorites) {\n"
"      const tool = schema.tools[toolName];\n"
"      if (tool) {\n"
"        const shortName = toolName.replace('browser_', '');\n"
"        html += `<div class=\"tool-item favorite-item\" data-tool=\"${toolName}\" onclick=\"selectTool('${toolName}')\"><svg class=\"item-star\" viewBox=\"0 0 24 24\" fill=\"currentColor\"><path d=\"M12 2l3.09 6.26L22 9.27l-5 4.87 1.18 6.88L12 17.77l-6.18 3.25L7 14.14 2 9.27l6.91-1.01L12 2z\"/></svg>${shortName}</div>`;\n"
"      }\n"
"    }\n"
"    \n"
"    html += '</div></div>';\n"
"  }\n"
"  \n"
"  for (const cat of schema.categories) {\n"
"    html += `<div class=\"category\" data-category=\"${cat.id}\">\n"
"      <div class=\"category-header\" onclick=\"toggleCategory('${cat.id}')\">\n"
"        <svg class=\"chevron\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"m9 18 6-6-6-6\"/></svg>\n"
"        <span>${cat.name}</span>\n"
"      </div>\n"
"      <div class=\"category-tools\">`;\n"
"    \n"
"    for (const toolName of cat.tools) {\n"
"      const tool = schema.tools[toolName];\n"
"      if (tool) {\n"
"        const shortName = toolName.replace('browser_', '');\n"
"        const isFav = favorites.includes(toolName);\n"
"        html += `<div class=\"tool-item${isFav ? ' is-favorite' : ''}\" data-tool=\"${toolName}\" onclick=\"selectTool('${toolName}')\">${shortName}</div>`;\n"
"      }\n"
"    }\n"
"    \n"
"    html += '</div></div>';\n"
"  }\n"
"  \n"
"  container.innerHTML = html;\n"
"  \n"
"  // Re-highlight current tool if selected\n"
"  if (currentTool) {\n"
"    document.querySelectorAll('.tool-item').forEach(el => el.classList.remove('active'));\n"
"    document.querySelectorAll(`.tool-item[data-tool=\"${currentTool.name}\"]`).forEach(el => el.classList.add('active'));\n"
"  }\n"
"}\n"
"\n"
"// Toggle category expansion\n"
"function toggleCategory(catId) {\n"
"  const cat = document.querySelector(`.category[data-category=\"${catId}\"]`);\n"
"  cat.classList.toggle('expanded');\n"
"}\n"
"\n"
"// Select a tool\n"
"async function selectTool(toolName) {\n"
"  currentTool = schema.tools[toolName];\n"
"  \n"
"  // Update active state\n"
"  document.querySelectorAll('.tool-item').forEach(el => el.classList.remove('active'));\n"
"  document.querySelector(`.tool-item[data-tool=\"${toolName}\"]`)?.classList.add('active');\n"
"  \n"
"  // Expand parent category\n"
"  const toolItem = document.querySelector(`.tool-item[data-tool=\"${toolName}\"]`);\n"
"  if (toolItem) {\n"
"    toolItem.closest('.category')?.classList.add('expanded');\n"
"  }\n"
"  \n"
"  document.getElementById('welcomeScreen').style.display = 'none';\n"
"  \n"
"  // Check if this tool needs context_id and fetch existing contexts\n"
"  const needsContext = currentTool.parameters.some(p => p.name === 'context_id');\n"
"  if (needsContext) {\n"
"    await loadExistingContexts();\n"
"  }\n"
"  \n"
"  renderToolContent();\n"
"}\n"
"\n"
"// Render tool content\n"
"function renderToolContent() {\n"
"  const container = document.getElementById('toolContent');\n"
"  container.style.display = 'flex';\n"
"  \n"
"  let paramsHtml = '';\n"
"  if (currentTool.parameters.length === 0) {\n"
"    paramsHtml = `<div class=\"empty-params\">\n"
"      <svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M12 8v4m0 4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z\"/></svg>\n"
"      <p>This tool takes no parameters</p>\n"
"    </div>`;\n"
"  } else {\n"
"    for (const param of currentTool.parameters) {\n"
"      const reqBadge = param.required ? '<span class=\"required-badge\">required</span>' : '';\n"
"      const typeBadge = `<span class=\"type-badge\">${param.type}</span>`;\n"
"      \n"
"      let inputHtml = '';\n"
"      if (param.name === 'context_id') {\n"
"        // Show dropdown with tracked context IDs (always as dropdown)\n"
"        const options = contextIds.map(id => `<option value=\"${id}\"${contextIds.length === 1 ? ' selected' : ''}>${id}</option>`).join('');\n"
"        // Auto-select if only one context, otherwise show placeholder\n"
"        const emptyMsg = contextIds.length === 0 ? '<option value=\"\">No contexts - create one first</option>' : (contextIds.length === 1 ? '' : '<option value=\"\">Select context...</option>');\n"
"        inputHtml = `<div class=\"context-select-wrapper\">\\n"
"          <select class=\"form-select\" id=\"param_${param.name}\">${emptyMsg}${options}</select>\\n"
"          <button type=\"button\" class=\"refresh-btn\" onclick=\"refreshContexts()\" title=\"Refresh context list\">\\n"
"            <svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M3 12a9 9 0 0 1 9-9 9.75 9.75 0 0 1 6.74 2.74L21 8\"/><path d=\"M21 3v5h-5\"/><path d=\"M21 12a9 9 0 0 1-9 9 9.75 9.75 0 0 1-6.74-2.74L3 16\"/><path d=\"M8 16H3v5\"/></svg>\\n"
"          </button>\\n"
"        </div>`;\n"
"      } else if (param.name === 'license_content') {\n"
"        // Special file input for license files\n"
"        inputHtml = `<div class=\"file-input-wrapper\">\\n"
"          <input type=\"file\" id=\"param_${param.name}_file\" accept=\".olic\" class=\"form-file-input\" onchange=\"handleLicenseFile(this)\">\\n"
"          <input type=\"hidden\" id=\"param_${param.name}\">\\n"
"          <div class=\"file-input-label\">\\n"
"            <svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4\"/><polyline points=\"17 8 12 3 7 8\"/><line x1=\"12\" y1=\"3\" x2=\"12\" y2=\"15\"/></svg>\\n"
"            <span id=\"param_${param.name}_filename\">Choose .olic license file...</span>\\n"
"          </div>\\n"
"        </div>`;\n"
"      } else if (param.type === 'boolean') {\n"
"        inputHtml = `<div class=\"form-checkbox\"><input type=\"checkbox\" id=\"param_${param.name}\"><label for=\"param_${param.name}\">Enable</label></div>`;\n"
"      } else if (param.type === 'enum' && param.enum) {\n"
"        const options = param.enum.map(v => `<option value=\"${v}\">${v}</option>`).join('');\n"
"        inputHtml = `<select class=\"form-select\" id=\"param_${param.name}\"><option value=\"\">Select...</option>${options}</select>`;\n"
"      } else if (param.type === 'integer' || param.type === 'number') {\n"
"        inputHtml = `<input type=\"number\" class=\"form-input\" id=\"param_${param.name}\" placeholder=\"Enter ${param.name}...\">`;\n"
"      } else {\n"
"        inputHtml = `<input type=\"text\" class=\"form-input\" id=\"param_${param.name}\" placeholder=\"Enter ${param.name}...\">`;\n"
"      }\n"
"      \n"
"      paramsHtml += `<div class=\"form-group\">\n"
"        <label class=\"form-label\">${param.name} ${reqBadge} ${typeBadge}</label>\n"
"        <div class=\"form-description\">${param.description || ''}</div>\n"
"        ${inputHtml}\n"
"      </div>`;\n"
"    }\n"
"  }\n"
"  \n"
"  const isFav = isFavorite(currentTool.name);\n"
"  container.innerHTML = `\n"
"    <div class=\"tool-header\">\n"
"      <div class=\"tool-title-row\">\n"
"        <div class=\"tool-title\">${currentTool.name}</div>\n"
"        <button id=\"favoriteBtn\" class=\"favorite-btn${isFav ? ' active' : ''}\" onclick=\"toggleFavorite('${currentTool.name}')\" title=\"${isFav ? 'Remove from favorites' : 'Add to favorites'}\">\n"
"          <svg viewBox=\"0 0 24 24\" fill=\"${isFav ? 'currentColor' : 'none'}\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M12 2l3.09 6.26L22 9.27l-5 4.87 1.18 6.88L12 17.77l-6.18 3.25L7 14.14 2 9.27l6.91-1.01L12 2z\"/></svg>\n"
"        </button>\n"
"      </div>\n"
"      <div class=\"tool-description\">${currentTool.description}</div>\n"
"      <div class=\"tool-endpoint\">\n"
"        <span class=\"method-badge\">${currentTool.method}</span>\n"
"        <span>${currentTool.endpoint}</span>\n"
"      </div>\n"
"    </div>\n"
"    <div class=\"tool-body\">\n"
"      <div class=\"request-panel\">\n"
"        <div class=\"panel-header\">Request Parameters</div>\n"
"        <div class=\"request-form\">\n"
"          ${paramsHtml}\n"
"          <button class=\"execute-btn\" onclick=\"executeRequest()\">\n"
"            <svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M5 12h14M12 5l7 7-7 7\"/></svg>\n"
"            Execute Request\n"
"          </button>\n"
"        </div>\n"
"      </div>\n"
"      <div class=\"response-panel\">\n"
"        <div class=\"response-header\">\n"
"          <div class=\"response-tabs\">\n"
"            <button class=\"response-tab active\" onclick=\"switchTab('json')\">JSON</button>\n"
"            <button class=\"response-tab\" id=\"previewTab\" onclick=\"switchTab('preview')\" style=\"display:none\">Preview</button>\n"
"          </div>\n"
"          <div class=\"response-status\">\n"
"            <span id=\"responseTime\" class=\"response-time\"></span>\n"
"            <button class=\"copy-btn\" onclick=\"copyResponse()\">\n"
"              <svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><rect x=\"9\" y=\"9\" width=\"13\" height=\"13\" rx=\"2\"/><path d=\"M5 15H4a2 2 0 01-2-2V4a2 2 0 012-2h9a2 2 0 012 2v1\"/></svg>\n"
"              Copy\n"
"            </button>\n"
"          </div>\n"
"        </div>\n"
"        <div class=\"response-content active\" id=\"responseJson\">\n"
"          <div class=\"response-body\" id=\"responseBody\">Execute a request to see the response here...</div>\n"
"        </div>\n"
"        <div class=\"response-content\" id=\"responsePreview\"></div>\n"
"        <div class=\"response-snippet-section\" id=\"sdkSnippetSection\">\n"
"          <div class=\"sdk-snippet-header\">\n"
"            <div class=\"sdk-snippet-title\">\n"
"              <svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><polyline points=\"16 18 22 12 16 6\"></polyline><polyline points=\"8 6 2 12 8 18\"></polyline></svg>\n"
"              Code Snippet\n"
"            </div>\n"
"            <div class=\"sdk-tabs\">\n"
"              <button class=\"sdk-tab active\" onclick=\"switchSdkTab('curl')\" id=\"curlTab\">\n"
"                <svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M7 8l-4 4 4 4\"/><path d=\"M17 8l4 4-4 4\"/></svg>\n"
"                cURL\n"
"              </button>\n"
"              <button class=\"sdk-tab\" onclick=\"switchSdkTab('python')\" id=\"pythonTab\">\n"
"                <svg viewBox=\"0 0 24 24\" fill=\"currentColor\"><path d=\"M12 0C5.4 0 5.8 2.9 5.8 2.9l0 3h6.3v.9H4.1S0 6.2 0 12.1c0 5.9 3.6 5.7 3.6 5.7h2.1v-2.7s-.1-3.6 3.5-3.6h6.1s3.4.1 3.4-3.3V3.6S19.3 0 12 0zM8.7 2.1c.6 0 1.1.5 1.1 1.1s-.5 1.1-1.1 1.1-1.1-.5-1.1-1.1.5-1.1 1.1-1.1z\"/><path d=\"M12 24c6.6 0 6.2-2.9 6.2-2.9l0-3h-6.3v-.9h8S24 17.8 24 11.9c0-5.9-3.6-5.7-3.6-5.7h-2.1v2.7s.1 3.6-3.5 3.6h-6.1s-3.4-.1-3.4 3.3v4.6S4.7 24 12 24zm3.3-2.1c-.6 0-1.1-.5-1.1-1.1s.5-1.1 1.1-1.1 1.1.5 1.1 1.1-.5 1.1-1.1 1.1z\"/></svg>\n"
"                Python\n"
"              </button>\n"
"              <button class=\"sdk-tab\" onclick=\"switchSdkTab('nodejs')\" id=\"nodejsTab\">\n"
"                <svg viewBox=\"0 0 24 24\" fill=\"currentColor\"><path d=\"M12 21.985c-.275 0-.532-.074-.772-.202l-2.439-1.448c-.365-.203-.182-.277-.072-.314.496-.165.588-.201 1.101-.493.056-.037.129-.02.185.017l1.87 1.12c.074.036.166.036.221 0l7.319-4.237c.074-.036.11-.11.11-.202V7.768c0-.091-.036-.165-.11-.201l-7.319-4.219c-.073-.037-.165-.037-.221 0L4.552 7.566c-.073.036-.11.129-.11.201v8.457c0 .073.037.166.11.202l2 1.157c1.082.548 1.762-.095 1.762-.735V8.502c0-.11.091-.221.22-.221h.936c.108 0 .22.092.22.221v8.347c0 1.449-.788 2.294-2.164 2.294-.422 0-.752 0-1.688-.46l-1.925-1.099a1.55 1.55 0 01-.771-1.34V7.786c0-.55.293-1.064.771-1.339l7.316-4.237a1.637 1.637 0 011.544 0l7.317 4.237c.479.274.771.789.771 1.339v8.458c0 .549-.293 1.063-.771 1.34l-7.317 4.236c-.241.11-.516.165-.773.165z\"/></svg>\n"
"                Node.js\n"
"              </button>\n"
"            </div>\n"
"          </div>\n"
"          <div class=\"sdk-code-container\">\n"
"            <button class=\"sdk-copy-btn\" onclick=\"copySdkCode()\">\n"
"              <svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><rect x=\"9\" y=\"9\" width=\"13\" height=\"13\" rx=\"2\"/><path d=\"M5 15H4a2 2 0 01-2-2V4a2 2 0 012-2h9a2 2 0 012 2v1\"/></svg>\n"
"              Copy\n"
"            </button>\n"
"            <div class=\"sdk-code-content active\" id=\"curlCode\"><pre></pre></div>\n"
"            <div class=\"sdk-code-content\" id=\"pythonCode\"><pre></pre></div>\n"
"            <div class=\"sdk-code-content\" id=\"nodejsCode\"><pre></pre></div>\n"
"          </div>\n"
"        </div>\n"
"      </div>\n"
"    </div>\n"
"  `;\n"
"}\n"
"\n"
"// Execute request\n"
"async function executeRequest() {\n"
"  const token = document.getElementById('authToken').value;\n"
"  if (!token) {\n"
"    alert('Please enter your API token');\n"
"    return;\n"
"  }\n"
"  \n"
"  const params = {};\n"
"  for (const param of currentTool.parameters) {\n"
"    const el = document.getElementById(`param_${param.name}`);\n"
"    if (el) {\n"
"      if (param.type === 'boolean') {\n"
"        if (el.checked) params[param.name] = true;\n"
"      } else if (param.type === 'integer') {\n"
"        if (el.value) params[param.name] = parseInt(el.value);\n"
"      } else if (param.type === 'number') {\n"
"        if (el.value) params[param.name] = parseFloat(el.value);\n"
"      } else {\n"
"        if (el.value) params[param.name] = el.value;\n"
"      }\n"
"    }\n"
"  }\n"
"  \n"
"  const btn = document.querySelector('.execute-btn');\n"
"  btn.disabled = true;\n"
"  btn.innerHTML = '<span class=\"spinner\"></span> Executing...';\n"
"  \n"
"  const startTime = performance.now();\n"
"  \n"
"  try {\n"
"    const res = await fetch(currentTool.endpoint, {\n"
"      method: 'POST',\n"
"      headers: {\n"
"        'Content-Type': 'application/json',\n"
"        'Authorization': `Bearer ${token}`\n"
"      },\n"
"      body: JSON.stringify(params)\n"
"    });\n"
"    \n"
"    const elapsed = Math.round(performance.now() - startTime);\n"
"    document.getElementById('responseTime').textContent = `${elapsed}ms`;\n"
"    \n"
"    const data = await res.json();\n"
"    \n"
"    // Track context IDs from create_context responses\n"
"    let contextChanged = false;\n"
"    if (currentTool.name === 'browser_create_context' && data.success && data.result) {\n"
"      const ctxId = typeof data.result === 'string' ? data.result : data.result.context_id;\n"
"      if (ctxId && !contextIds.includes(ctxId)) {\n"
"        contextIds.push(ctxId);\n"
"        contextChanged = true;\n"
"      }\n"
"    }\n"
"    // Remove context ID when closed\n"
"    if (currentTool.name === 'browser_close_context' && data.success && params.context_id) {\n"
"      contextIds = contextIds.filter(id => id !== params.context_id);\n"
"      contextChanged = true;\n"
"    }\n"
"    \n"
"    // Update context_id dropdown in real-time if contexts changed\n"
"    if (contextChanged) {\n"
"      const ctxSelect = document.getElementById('param_context_id');\n"
"      if (ctxSelect) {\n"
"        const options = contextIds.map(id => `<option value=\"${id}\">${id}</option>`).join('');\n"
"        // Auto-select if only one context, otherwise show placeholder\n"
"        const emptyMsg = contextIds.length === 1 ? '' : '<option value=\"\">Select context...</option>';\n"
"        ctxSelect.innerHTML = emptyMsg + options;\n"
"        // Auto-select newly created context, or single remaining context\n"
"        if (currentTool.name === 'browser_create_context' && data.result) {\n"
"          const newCtxId = typeof data.result === 'string' ? data.result : data.result.context_id;\n"
"          if (newCtxId) ctxSelect.value = newCtxId;\n"
"        } else if (contextIds.length === 1) {\n"
"          ctxSelect.value = contextIds[0];\n"
"        }\n"
"      }\n"
"    }\n"
"    \n"
"    document.getElementById('responseBody').innerHTML = syntaxHighlight(JSON.stringify(data, null, 2));\n"
"    renderPreview(data);\n"
"  } catch (e) {\n"
"    document.getElementById('responseBody').textContent = `Error: ${e.message}`;\n"
"    document.getElementById('previewTab').style.display = 'none';\n"
"  } finally {\n"
"    btn.disabled = false;\n"
"    btn.innerHTML = '<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M5 12h14M12 5l7 7-7 7\"/></svg> Execute Request';\n"
"  }\n"
"}\n"
"\n"
"// Escape HTML entities to prevent XSS and DOM corruption\n"
"function escapeHtml(str) {\n"
"  return str\n"
"    .replace(/&/g, '&amp;')\n"
"    .replace(/</g, '&lt;')\n"
"    .replace(/>/g, '&gt;')\n"
"    .replace(/\"/g, '&quot;');\n"
"}\n"
"\n"
"// Syntax highlight JSON (escapes HTML first to prevent injection)\n"
"function syntaxHighlight(json) {\n"
"  // CRITICAL: First escape HTML entities to prevent DOM injection\n"
"  // Without this, HTML in JSON values (like getHTML response) corrupts the page\n"
"  const escaped = escapeHtml(json);\n"
"  // Then apply syntax highlighting on the escaped content\n"
"  return escaped\n"
"    .replace(/(&quot;[^&]*?&quot;)(:)/g, '<span class=\"json-key\">$1</span>$2')\n"
"    .replace(/: (&quot;)/g, ': <span class=\"json-string\">$1')\n"
"    .replace(/(&quot;)([,\\n\\r\\}\\]])/g, '$1</span>$2')\n"
"    .replace(/: (\\d+\\.?\\d*)([,\\n\\r\\}\\]])/g, ': <span class=\"json-number\">$1</span>$2')\n"
"    .replace(/: (true|false)([,\\n\\r\\}\\]])/g, ': <span class=\"json-boolean\">$1</span>$2')\n"
"    .replace(/: (null)([,\\n\\r\\}\\]])/g, ': <span class=\"json-null\">$1</span>$2');\n"
"}\n"
"\n"
"// Copy to clipboard with fallback for non-secure contexts\n"
"function copyToClipboard(text) {\n"
"  if (navigator.clipboard && window.isSecureContext) {\n"
"    return navigator.clipboard.writeText(text);\n"
"  }\n"
"  // Fallback for non-secure contexts (HTTP)\n"
"  const textarea = document.createElement('textarea');\n"
"  textarea.value = text;\n"
"  textarea.style.position = 'fixed';\n"
"  textarea.style.left = '-9999px';\n"
"  document.body.appendChild(textarea);\n"
"  textarea.select();\n"
"  try {\n"
"    document.execCommand('copy');\n"
"  } catch (e) {\n"
"    console.error('Copy failed:', e);\n"
"  }\n"
"  document.body.removeChild(textarea);\n"
"  return Promise.resolve();\n"
"}\n"
"\n"
"// Copy response\n"
"function copyResponse() {\n"
"  const text = document.getElementById('responseBody').textContent;\n"
"  const btn = document.querySelector('.copy-btn');\n"
"  copyToClipboard(text).then(() => {\n"
"    const orig = btn.innerHTML;\n"
"    btn.innerHTML = '<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><polyline points=\"20 6 9 17 4 12\"></polyline></svg> Copied!';\n"
"    btn.style.color = '#22c55e';\n"
"    setTimeout(() => { btn.innerHTML = orig; btn.style.color = ''; }, 2000);\n"
"  });\n"
"}\n"
"\n"
"// Switch response tabs\n"
"function switchTab(tab) {\n"
"  document.querySelectorAll('.response-tab').forEach(t => t.classList.remove('active'));\n"
"  document.querySelectorAll('.response-content').forEach(c => c.classList.remove('active'));\n"
"  if (tab === 'json') {\n"
"    document.querySelector('.response-tab').classList.add('active');\n"
"    document.getElementById('responseJson').classList.add('active');\n"
"  } else {\n"
"    document.getElementById('previewTab').classList.add('active');\n"
"    document.getElementById('responsePreview').classList.add('active');\n"
"  }\n"
"}\n"
"\n"
"// Render preview for special tools\n"
"function renderPreview(data) {\n"
"  const previewTab = document.getElementById('previewTab');\n"
"  const previewPane = document.getElementById('responsePreview');\n"
"  const previewTools = ['browser_screenshot', 'browser_get_html', 'browser_get_markdown', 'browser_extract_text'];\n"
"  \n"
"  if (!previewTools.includes(currentTool.name) || !data.success || !data.result) {\n"
"    previewTab.style.display = 'none';\n"
"    return;\n"
"  }\n"
"  \n"
"  previewTab.style.display = '';\n"
"  previewPane.innerHTML = '';\n"
"  \n"
"  if (currentTool.name === 'browser_screenshot') {\n"
"    const base64 = data.result.screenshot || data.result;\n"
"    const wrapper = document.createElement('div');\n"
"    wrapper.style.padding = '16px';\n"
"    wrapper.className = 'image-preview-container';\n"
"    \n"
"    // Zoom controls\n"
"    const controls = document.createElement('div');\n"
"    controls.className = 'zoom-controls';\n"
"    controls.innerHTML = '\\n"
"      <button class=\\\"zoom-btn\\\" id=\\\"zoomOut\\\" title=\\\"Zoom Out\\\">\\n"
"        <svg viewBox=\\\"0 0 24 24\\\" fill=\\\"none\\\" stroke=\\\"currentColor\\\" stroke-width=\\\"2\\\"><circle cx=\\\"11\\\" cy=\\\"11\\\" r=\\\"8\\\"/><path d=\\\"m21 21-4.35-4.35\\\"/><path d=\\\"M8 11h6\\\"/></svg>\\n"
"      </button>\\n"
"      <span class=\\\"zoom-level\\\" id=\\\"zoomLevel\\\">100%</span>\\n"
"      <button class=\\\"zoom-btn\\\" id=\\\"zoomIn\\\" title=\\\"Zoom In\\\">\\n"
"        <svg viewBox=\\\"0 0 24 24\\\" fill=\\\"none\\\" stroke=\\\"currentColor\\\" stroke-width=\\\"2\\\"><circle cx=\\\"11\\\" cy=\\\"11\\\" r=\\\"8\\\"/><path d=\\\"m21 21-4.35-4.35\\\"/><path d=\\\"M11 8v6\\\"/><path d=\\\"M8 11h6\\\"/></svg>\\n"
"      </button>\\n"
"      <button class=\\\"zoom-btn\\\" id=\\\"zoomReset\\\" title=\\\"Reset Zoom\\\">\\n"
"        <svg viewBox=\\\"0 0 24 24\\\" fill=\\\"none\\\" stroke=\\\"currentColor\\\" stroke-width=\\\"2\\\"><path d=\\\"M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8\\\"/><path d=\\\"M3 3v5h5\\\"/></svg>\\n"
"      </button>\\n"
"    ';\n"
"    wrapper.appendChild(controls);\n"
"    \n"
"    // Image viewport (for pan/zoom)\n"
"    const viewport = document.createElement('div');\n"
"    viewport.className = 'image-viewport';\n"
"    viewport.id = 'imageViewport';\n"
"    \n"
"    const img = document.createElement('img');\n"
"    img.className = 'preview-image';\n"
"    img.id = 'previewImg';\n"
"    img.src = 'data:image/png;base64,' + base64;\n"
"    img.alt = 'Screenshot';\n"
"    img.draggable = false;\n"
"    viewport.appendChild(img);\n"
"    wrapper.appendChild(viewport);\n"
"    previewPane.appendChild(wrapper);\n"
"    \n"
"    // Initialize zoom functionality\n"
"    initImageZoom();\n"
"  } else if (currentTool.name === 'browser_get_html') {\n"
"    const html = data.result.html || data.result;\n"
"    const htmlContent = typeof html === 'string' ? html : JSON.stringify(html, null, 2);\n"
"    \n"
"    // Create container with Shadow DOM for isolated HTML rendering\n"
"    const container = document.createElement('div');\n"
"    container.className = 'preview-html-container';\n"
"    container.style.cssText = 'width:100%;height:100%;overflow:auto;background:#fff;';\n"
"    \n"
"    // Attach Shadow DOM for style isolation\n"
"    const shadow = container.attachShadow({ mode: 'open' });\n"
"    \n"
"    // Add base styles and viewport meta for proper rendering\n"
"    const wrapper = document.createElement('div');\n"
"    wrapper.style.cssText = 'all:initial;font-family:system-ui,-apple-system,sans-serif;';\n"
"    wrapper.innerHTML = htmlContent;\n"
"    shadow.appendChild(wrapper);\n"
"    \n"
"    previewPane.appendChild(container);\n"
"  } else if (currentTool.name === 'browser_get_markdown') {\n"
"    const md = data.result.markdown || data.result;\n"
"    const div = document.createElement('div');\n"
"    div.className = 'preview-markdown';\n"
"    div.innerHTML = renderMarkdown(typeof md === 'string' ? md : String(md));\n"
"    previewPane.appendChild(div);\n"
"  } else if (currentTool.name === 'browser_extract_text') {\n"
"    const text = data.result.text || data.result;\n"
"    const pre = document.createElement('pre');\n"
"    pre.className = 'preview-text';\n"
"    pre.textContent = typeof text === 'string' ? text : JSON.stringify(text, null, 2);\n"
"    previewPane.appendChild(pre);\n"
"  }\n"
"}\n"
"\n"
"\n"
"// Simple markdown renderer\n"
"function renderMarkdown(md) {\n"
"  return md\n"
"    .replace(/^### (.+)$/gm, '<h3>$1</h3>')\n"
"    .replace(/^## (.+)$/gm, '<h2>$1</h2>')\n"
"    .replace(/^# (.+)$/gm, '<h1>$1</h1>')\n"
"    .replace(/\\*\\*(.+?)\\*\\*/g, '<strong>$1</strong>')\n"
"    .replace(/\\*(.+?)\\*/g, '<em>$1</em>')\n"
"    .replace(/`([^`]+)`/g, '<code>$1</code>')\n"
"    .replace(/\\[([^\\]]+)\\]\\(([^)]+)\\)/g, '<a href=\"$2\" target=\"_blank\">$1</a>')\n"
"    .replace(/^- (.+)$/gm, '<li>$1</li>')\n"
"    .replace(/\\n\\n/g, '</p><p>')\n"
"    .replace(/^(.+)$/gm, '<p>$1</p>')\n"
"    .replace(/<p><h/g, '<h').replace(/<\\/h(\\d)><\\/p>/g, '</h$1>')\n"
"    .replace(/<p><li>/g, '<li>').replace(/<\\/li><\\/p>/g, '</li>');\n"
"}\n"
"\n"
"// Search functionality\n"
"document.getElementById('searchInput').addEventListener('input', (e) => {\n"
"  const query = e.target.value.toLowerCase();\n"
"  // Collapse all categories first when search is cleared\n"
"  if (!query) {\n"
"    document.querySelectorAll('.category').forEach(cat => {\n"
"      cat.classList.remove('expanded');\n"
"      cat.style.display = '';\n"
"    });\n"
"    document.querySelectorAll('.tool-item').forEach(el => el.style.display = '');\n"
"    return;\n"
"  }\n"
"  document.querySelectorAll('.tool-item').forEach(el => {\n"
"    const toolName = el.dataset.tool.toLowerCase();\n"
"    const match = toolName.includes(query);\n"
"    el.style.display = match ? '' : 'none';\n"
"    if (match) {\n"
"      el.closest('.category')?.classList.add('expanded');\n"
"    }\n"
"  });\n"
"  // Hide categories with no visible tools\n"
"  document.querySelectorAll('.category').forEach(cat => {\n"
"    const tools = cat.querySelectorAll('.tool-item');\n"
"    const hasVisible = Array.from(tools).some(t => t.style.display !== 'none');\n"
"    cat.style.display = hasVisible ? '' : 'none';\n"
"  });\n"
"});\n"
"\n"
"// Reload contexts when token changes\n"
"document.getElementById('authToken').addEventListener('change', async () => {\n"
"  await loadExistingContexts();\n"
"  if (currentTool) renderToolContent();\n"
"});\n"
"\n"
"// ==================== CODE SNIPPET FUNCTIONS ====================\n"
"\n"
"let currentSdkTab = 'curl';\n"
"\n"
"// Tool name to SDK method mapping\n"
"const toolMethodMap = {\n"
"  'browser_create_context': { node: 'newPage', python: 'new_page', onBrowser: true },\n"
"  'browser_close_context': { node: 'close', python: 'close', onContext: true },\n"
"  'browser_list_contexts': { node: 'pages', python: 'pages', onBrowser: true },\n"
"  'browser_navigate': { node: 'goto', python: 'goto' },\n"
"  'browser_reload': { node: 'reload', python: 'reload' },\n"
"  'browser_go_back': { node: 'goBack', python: 'go_back' },\n"
"  'browser_go_forward': { node: 'goForward', python: 'go_forward' },\n"
"  'browser_can_go_back': { node: 'canGoBack', python: 'can_go_back' },\n"
"  'browser_can_go_forward': { node: 'canGoForward', python: 'can_go_forward' },\n"
"  'browser_click': { node: 'click', python: 'click' },\n"
"  'browser_type': { node: 'type', python: 'type' },\n"
"  'browser_pick': { node: 'pick', python: 'pick' },\n"
"  'browser_press_key': { node: 'pressKey', python: 'press_key' },\n"
"  'browser_submit_form': { node: 'submitForm', python: 'submit_form' },\n"
"  'browser_drag_drop': { node: 'dragDrop', python: 'drag_drop' },\n"
"  'browser_html5_drag_drop': { node: 'html5DragDrop', python: 'html5_drag_drop' },\n"
"  'browser_mouse_move': { node: 'mouseMove', python: 'mouse_move' },\n"
"  'browser_hover': { node: 'hover', python: 'hover' },\n"
"  'browser_double_click': { node: 'doubleClick', python: 'double_click' },\n"
"  'browser_right_click': { node: 'rightClick', python: 'right_click' },\n"
"  'browser_clear_input': { node: 'clearInput', python: 'clear_input' },\n"
"  'browser_focus': { node: 'focus', python: 'focus' },\n"
"  'browser_blur': { node: 'blur', python: 'blur' },\n"
"  'browser_select_all': { node: 'selectAll', python: 'select_all' },\n"
"  'browser_keyboard_combo': { node: 'keyboardCombo', python: 'keyboard_combo' },\n"
"  'browser_upload_file': { node: 'uploadFile', python: 'upload_file' },\n"
"  'browser_extract_text': { node: 'extractText', python: 'extract_text' },\n"
"  'browser_screenshot': { node: 'screenshot', python: 'screenshot' },\n"
"  'browser_highlight': { node: 'highlight', python: 'highlight' },\n"
"  'browser_show_grid_overlay': { node: 'showGridOverlay', python: 'show_grid_overlay' },\n"
"  'browser_get_html': { node: 'getHTML', python: 'get_html' },\n"
"  'browser_get_markdown': { node: 'getMarkdown', python: 'get_markdown' },\n"
"  'browser_extract_json': { node: 'extractJSON', python: 'extract_json' },\n"
"  'browser_detect_site': { node: 'detectWebsiteType', python: 'detect_website_type' },\n"
"  'browser_list_templates': { node: 'listTemplates', python: 'list_templates', onBrowser: true },\n"
"  'browser_summarize_page': { node: 'summarizePage', python: 'summarize_page' },\n"
"  'browser_query_page': { node: 'queryPage', python: 'query_page' },\n"
"  'browser_llm_status': { node: 'getLLMStatus', python: 'get_llm_status', onBrowser: true },\n"
"  'browser_nla': { node: 'executeNLA', python: 'execute_nla' },\n"
"  'browser_ai_click': { node: 'aiClick', python: 'ai_click' },\n"
"  'browser_ai_type': { node: 'aiType', python: 'ai_type' },\n"
"  'browser_ai_extract': { node: 'aiExtract', python: 'ai_extract' },\n"
"  'browser_ai_query': { node: 'aiQuery', python: 'ai_query' },\n"
"  'browser_ai_analyze': { node: 'aiAnalyze', python: 'ai_analyze' },\n"
"  'browser_find_element': { node: 'findElement', python: 'find_element' },\n"
"  'browser_scroll_by': { node: 'scrollBy', python: 'scroll_by' },\n"
"  'browser_scroll_to_element': { node: 'scrollToElement', python: 'scroll_to_element' },\n"
"  'browser_scroll_to_top': { node: 'scrollToTop', python: 'scroll_to_top' },\n"
"  'browser_scroll_to_bottom': { node: 'scrollToBottom', python: 'scroll_to_bottom' },\n"
"  'browser_wait_for_selector': { node: 'waitForSelector', python: 'wait_for_selector' },\n"
"  'browser_wait': { node: 'wait', python: 'wait' },\n"
"  'browser_wait_for_network_idle': { node: 'waitForNetworkIdle', python: 'wait_for_network_idle' },\n"
"  'browser_wait_for_function': { node: 'waitForFunction', python: 'wait_for_function' },\n"
"  'browser_wait_for_url': { node: 'waitForURL', python: 'wait_for_url' },\n"
"  'browser_get_page_info': { node: 'getPageInfo', python: 'get_page_info' },\n"
"  'browser_set_viewport': { node: 'setViewport', python: 'set_viewport' },\n"
"  'browser_zoom_in': { node: 'zoomIn', python: 'zoom_in' },\n"
"  'browser_zoom_out': { node: 'zoomOut', python: 'zoom_out' },\n"
"  'browser_zoom_reset': { node: 'zoomReset', python: 'zoom_reset' },\n"
"  'browser_start_video_recording': { node: 'startVideoRecording', python: 'start_video_recording' },\n"
"  'browser_pause_video_recording': { node: 'pauseVideoRecording', python: 'pause_video_recording' },\n"
"  'browser_resume_video_recording': { node: 'resumeVideoRecording', python: 'resume_video_recording' },\n"
"  'browser_stop_video_recording': { node: 'stopVideoRecording', python: 'stop_video_recording' },\n"
"  'browser_get_video_recording_stats': { node: 'getVideoStats', python: 'get_video_stats' },\n"
"  'browser_get_console_log': { node: 'getConsoleLogs', python: 'get_console_logs' },\n"
"  'browser_clear_console_log': { node: 'clearConsoleLogs', python: 'clear_console_logs' },\n"
"  'browser_start_live_stream': { node: 'startLiveStream', python: 'start_live_stream' },\n"
"  'browser_stop_live_stream': { node: 'stopLiveStream', python: 'stop_live_stream' },\n"
"  'browser_get_live_stream_stats': { node: 'getLiveStreamStats', python: 'get_live_stream_stats' },\n"
"  'browser_list_live_streams': { node: 'listLiveStreams', python: 'list_live_streams', onBrowser: true },\n"
"  'browser_get_live_frame': { node: 'getLiveFrame', python: 'get_live_frame' },\n"
"  'browser_get_demographics': { node: 'getDemographics', python: 'get_demographics', onBrowser: true },\n"
"  'browser_get_location': { node: 'getLocation', python: 'get_location', onBrowser: true },\n"
"  'browser_get_datetime': { node: 'getDateTime', python: 'get_datetime', onBrowser: true },\n"
"  'browser_get_weather': { node: 'getWeather', python: 'get_weather', onBrowser: true },\n"
"  'browser_detect_captcha': { node: 'detectCaptcha', python: 'detect_captcha' },\n"
"  'browser_classify_captcha': { node: 'classifyCaptcha', python: 'classify_captcha' },\n"
"  'browser_solve_text_captcha': { node: 'solveTextCaptcha', python: 'solve_text_captcha' },\n"
"  'browser_solve_image_captcha': { node: 'solveImageCaptcha', python: 'solve_image_captcha' },\n"
"  'browser_solve_captcha': { node: 'solveCaptcha', python: 'solve_captcha' },\n"
"  'browser_get_cookies': { node: 'getCookies', python: 'get_cookies' },\n"
"  'browser_set_cookie': { node: 'setCookie', python: 'set_cookie' },\n"
"  'browser_delete_cookies': { node: 'deleteCookies', python: 'delete_cookies' },\n"
"  'browser_set_proxy': { node: 'setProxy', python: 'set_proxy' },\n"
"  'browser_get_proxy_status': { node: 'getProxyStatus', python: 'get_proxy_status' },\n"
"  'browser_connect_proxy': { node: 'connectProxy', python: 'connect_proxy' },\n"
"  'browser_disconnect_proxy': { node: 'disconnectProxy', python: 'disconnect_proxy' },\n"
"  'browser_create_profile': { node: 'createProfile', python: 'create_profile', onBrowser: true },\n"
"  'browser_load_profile': { node: 'loadProfile', python: 'load_profile' },\n"
"  'browser_save_profile': { node: 'saveProfile', python: 'save_profile' },\n"
"  'browser_get_profile': { node: 'getProfile', python: 'get_profile' },\n"
"  'browser_update_profile_cookies': { node: 'updateProfileCookies', python: 'update_profile_cookies' },\n"
"  'browser_get_context_info': { node: 'getContextInfo', python: 'get_context_info' },\n"
"  'browser_clipboard_read': { node: 'clipboardRead', python: 'clipboard_read' },\n"
"  'browser_clipboard_write': { node: 'clipboardWrite', python: 'clipboard_write' },\n"
"  'browser_clipboard_clear': { node: 'clipboardClear', python: 'clipboard_clear' },\n"
"  'browser_add_network_rule': { node: 'addNetworkRule', python: 'add_network_rule' },\n"
"  'browser_remove_network_rule': { node: 'removeNetworkRule', python: 'remove_network_rule' },\n"
"  'browser_enable_network_interception': { node: 'enableNetworkInterception', python: 'enable_network_interception' },\n"
"  'browser_enable_network_logging': { node: 'enableNetworkLogging', python: 'enable_network_logging' },\n"
"  'browser_get_network_log': { node: 'getNetworkLog', python: 'get_network_log' },\n"
"  'browser_clear_network_log': { node: 'clearNetworkLog', python: 'clear_network_log' },\n"
"  'browser_set_download_path': { node: 'setDownloadPath', python: 'set_download_path' },\n"
"  'browser_get_downloads': { node: 'getDownloads', python: 'get_downloads' },\n"
"  'browser_get_active_downloads': { node: 'getActiveDownloads', python: 'get_active_downloads' },\n"
"  'browser_wait_for_download': { node: 'waitForDownload', python: 'wait_for_download' },\n"
"  'browser_cancel_download': { node: 'cancelDownload', python: 'cancel_download' },\n"
"  'browser_set_dialog_action': { node: 'setDialogAction', python: 'set_dialog_action' },\n"
"  'browser_get_pending_dialog': { node: 'getPendingDialog', python: 'get_pending_dialog' },\n"
"  'browser_get_dialogs': { node: 'getDialogs', python: 'get_dialogs' },\n"
"  'browser_handle_dialog': { node: 'handleDialog', python: 'handle_dialog' },\n"
"  'browser_wait_for_dialog': { node: 'waitForDialog', python: 'wait_for_dialog' },\n"
"  'browser_new_tab': { node: 'newTab', python: 'new_tab' },\n"
"  'browser_get_tabs': { node: 'getTabs', python: 'get_tabs' },\n"
"  'browser_switch_tab': { node: 'switchTab', python: 'switch_tab' },\n"
"  'browser_close_tab': { node: 'closeTab', python: 'close_tab' },\n"
"  'browser_get_active_tab': { node: 'getActiveTab', python: 'get_active_tab' },\n"
"  'browser_get_tab_count': { node: 'getTabCount', python: 'get_tab_count' },\n"
"  'browser_set_popup_policy': { node: 'setPopupPolicy', python: 'set_popup_policy' },\n"
"  'browser_get_blocked_popups': { node: 'getBlockedPopups', python: 'get_blocked_popups' },\n"
"  'browser_is_visible': { node: 'isVisible', python: 'is_visible' },\n"
"  'browser_is_enabled': { node: 'isEnabled', python: 'is_enabled' },\n"
"  'browser_is_checked': { node: 'isChecked', python: 'is_checked' },\n"
"  'browser_get_attribute': { node: 'getAttribute', python: 'get_attribute' },\n"
"  'browser_get_bounding_box': { node: 'getBoundingBox', python: 'get_bounding_box' },\n"
"  'browser_get_element_at_position': { node: 'getElementAtPosition', python: 'get_element_at_position' },\n"
"  'browser_get_interactive_elements': { node: 'getInteractiveElements', python: 'get_interactive_elements' },\n"
"  'browser_get_blocker_stats': { node: 'getBlockerStats', python: 'get_blocker_stats' },\n"
"  'browser_evaluate': { node: 'evaluate', python: 'evaluate' },\n"
"  'browser_list_frames': { node: 'listFrames', python: 'list_frames' },\n"
"  'browser_switch_to_frame': { node: 'switchToFrame', python: 'switch_to_frame' },\n"
"  'browser_switch_to_main_frame': { node: 'switchToMainFrame', python: 'switch_to_main_frame' },\n"
"  'browser_get_license_status': { node: 'getLicenseStatus', python: 'get_license_status', onBrowser: true },\n"
"  'browser_get_license_info': { node: 'getLicenseInfo', python: 'get_license_info', onBrowser: true },\n"
"  'browser_get_hardware_fingerprint': { node: 'getHardwareFingerprint', python: 'get_hardware_fingerprint', onBrowser: true },\n"
"  'browser_add_license': { node: 'addLicense', python: 'add_license', onBrowser: true },\n"
"  'browser_remove_license': { node: 'removeLicense', python: 'remove_license', onBrowser: true },\n"
"};\n"
"\n"
"function switchSdkTab(tab) {\n"
"  currentSdkTab = tab;\n"
"  document.querySelectorAll('.sdk-tab').forEach(t => t.classList.remove('active'));\n"
"  document.querySelectorAll('.sdk-code-content').forEach(c => c.classList.remove('active'));\n"
"  document.getElementById(tab + 'Tab').classList.add('active');\n"
"  document.getElementById(tab + 'Code').classList.add('active');\n"
"}\n"
"\n"
"function copySdkCode() {\n"
"  const codeEl = document.querySelector('.sdk-code-content.active pre');\n"
"  if (codeEl) {\n"
"    const text = codeEl.textContent;\n"
"    const btn = document.querySelector('.sdk-copy-btn');\n"
"    copyToClipboard(text).then(() => {\n"
"      const orig = btn.innerHTML;\n"
"      btn.innerHTML = '<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><polyline points=\"20 6 9 17 4 12\"></polyline></svg> Copied!';\n"
"      btn.style.color = '#22c55e';\n"
"      setTimeout(() => { btn.innerHTML = orig; btn.style.color = ''; }, 2000);\n"
"    });\n"
"  }\n"
"}\n"
"\n"
"function generatePythonCode(toolName, params) {\n"
"  const m = toolMethodMap[toolName];\n"
"  if (!m) return '<span class=\"sdk-comment\"># SDK method not available for this tool</span>';\n"
"  const method = m.python;\n"
"  const isOnBrowser = m.onBrowser;\n"
"  const isOnContext = m.onContext;\n"
"  const contextId = params.context_id || '';\n"
"  const sdkParams = Object.entries(params).filter(([k]) => k !== 'context_id').map(([k, v]) => {\n"
"    const pyKey = k.replace(/([A-Z])/g, '_$1').toLowerCase();\n"
"    if (typeof v === 'string') return `${pyKey}=\\\"${v}\\\"`;\n"
"    if (typeof v === 'boolean') return `${pyKey}=${v ? 'True' : 'False'}`;\n"
"    return `${pyKey}=${v}`;\n"
"  });\n"
"  const paramsStr = sdkParams.join(', ');\n"
"  const target = isOnBrowser ? 'browser' : 'page';\n"
"  let code = '<span class=\"sdk-comment\"># Python SDK</span>\\n';\n"
"  code += '<span class=\"sdk-keyword\">from</span> owl_browser <span class=\"sdk-keyword\">import</span> Browser\\n\\n';\n"
"  if (isOnBrowser && toolName === 'browser_create_context') {\n"
"    code += '<span class=\"sdk-comment\"># Create browser and page (returns context with internal ID)</span>\\n';\n"
"    code += '<span class=\"sdk-keyword\">with</span> <span class=\"sdk-function\">Browser</span>() <span class=\"sdk-keyword\">as</span> browser:\\n';\n"
"    code += '    page = browser.<span class=\"sdk-function\">new_page</span>()\\n';\n"
"    code += '    <span class=\"sdk-comment\"># page.id contains the context_id (e.g., \\\"' + (contextId || 'ctx_abc123') + '\\\")</span>\\n';\n"
"    code += '    <span class=\"sdk-keyword\">print</span>(<span class=\"sdk-string\">f\\\"Context ID: {page.id}\\\"</span>)\\n\\n';\n"
"    code += '    <span class=\"sdk-comment\"># All subsequent calls use this context automatically</span>\\n';\n"
"    code += '    page.<span class=\"sdk-function\">goto</span>(<span class=\"sdk-string\">\\\"https://example.com\\\"</span>)\\n';\n"
"  } else if (isOnBrowser) {\n"
"    code += '<span class=\"sdk-comment\"># Initialize browser</span>\\n';\n"
"    code += '<span class=\"sdk-keyword\">with</span> <span class=\"sdk-function\">Browser</span>() <span class=\"sdk-keyword\">as</span> browser:\\n';\n"
"    code += `    result = ${target}.<span class=\"sdk-function\">${method}</span>(${paramsStr})\\n`;\n"
"    code += '    <span class=\"sdk-keyword\">print</span>(result)\\n';\n"
"  } else {\n"
"    code += '<span class=\"sdk-comment\"># Initialize browser and create page</span>\\n';\n"
"    code += '<span class=\"sdk-keyword\">with</span> <span class=\"sdk-function\">Browser</span>() <span class=\"sdk-keyword\">as</span> browser:\\n';\n"
"    code += '    page = browser.<span class=\"sdk-function\">new_page</span>()\\n';\n"
"    if (contextId) {\n"
"      code += '    <span class=\"sdk-comment\"># Context ID \\\"' + contextId + '\\\" is managed internally by page object</span>\\n\\n';\n"
"    } else {\n"
"      code += '    <span class=\"sdk-comment\"># Context ID is managed internally by page object</span>\\n\\n';\n"
"    }\n"
"    code += '    <span class=\"sdk-comment\"># Execute the action</span>\\n';\n"
"    if (isOnContext) {\n"
"      code += `    ${target}.<span class=\"sdk-function\">${method}</span>()\\n`;\n"
"    } else {\n"
"      code += `    result = ${target}.<span class=\"sdk-function\">${method}</span>(${paramsStr})\\n`;\n"
"      code += '    <span class=\"sdk-keyword\">print</span>(result)\\n';\n"
"    }\n"
"  }\n"
"  return code;\n"
"}\n"
"\n"
"function generateNodeCode(toolName, params) {\n"
"  const m = toolMethodMap[toolName];\n"
"  if (!m) return '<span class=\"sdk-comment\">// SDK method not available for this tool</span>';\n"
"  const method = m.node;\n"
"  const isOnBrowser = m.onBrowser;\n"
"  const isOnContext = m.onContext;\n"
"  const contextId = params.context_id || '';\n"
"  const sdkParams = Object.entries(params).filter(([k]) => k !== 'context_id').map(([k, v]) => {\n"
"    if (typeof v === 'string') return `'${v}'`;\n"
"    return v;\n"
"  });\n"
"  const paramsStr = sdkParams.join(', ');\n"
"  const target = isOnBrowser ? 'browser' : 'page';\n"
"  let code = '<span class=\"sdk-comment\">// Node.js SDK (TypeScript/JavaScript)</span>\\n';\n"
"  code += '<span class=\"sdk-keyword\">import</span> { Browser } <span class=\"sdk-keyword\">from</span> <span class=\"sdk-string\">\"@olib-ai/owl-browser-sdk\"</span>;\\n\\n';\n"
"  if (isOnBrowser && toolName === 'browser_create_context') {\n"
"    code += '<span class=\"sdk-comment\">// Create browser and page (returns context with internal ID)</span>\\n';\n"
"    code += '<span class=\"sdk-keyword\">const</span> browser = <span class=\"sdk-keyword\">new</span> <span class=\"sdk-function\">Browser</span>();\\n';\n"
"    code += '<span class=\"sdk-keyword\">await</span> browser.<span class=\"sdk-function\">launch</span>();\\n\\n';\n"
"    code += '<span class=\"sdk-keyword\">const</span> page = <span class=\"sdk-keyword\">await</span> browser.<span class=\"sdk-function\">newPage</span>();\\n';\n"
"    code += '<span class=\"sdk-comment\">// page.getId() returns the context_id (e.g., \\\"' + (contextId || 'ctx_abc123') + '\\\")</span>\\n';\n"
"    code += '<span class=\"sdk-function\">console</span>.<span class=\"sdk-function\">log</span>(<span class=\"sdk-string\">`Context ID: ${page.getId()}`</span>);\\n\\n';\n"
"    code += '<span class=\"sdk-comment\">// All subsequent calls use this context automatically</span>\\n';\n"
"    code += '<span class=\"sdk-keyword\">await</span> page.<span class=\"sdk-function\">goto</span>(<span class=\"sdk-string\">\\'https://example.com\\'</span>);\\n\\n';\n"
"    code += '<span class=\"sdk-keyword\">await</span> browser.<span class=\"sdk-function\">close</span>();\\n';\n"
"  } else if (isOnBrowser) {\n"
"    code += '<span class=\"sdk-comment\">// Initialize browser</span>\\n';\n"
"    code += '<span class=\"sdk-keyword\">const</span> browser = <span class=\"sdk-keyword\">new</span> <span class=\"sdk-function\">Browser</span>();\\n';\n"
"    code += '<span class=\"sdk-keyword\">await</span> browser.<span class=\"sdk-function\">launch</span>();\\n\\n';\n"
"    code += `<span class=\"sdk-keyword\">const</span> result = <span class=\"sdk-keyword\">await</span> ${target}.<span class=\"sdk-function\">${method}</span>(${paramsStr});\\n`;\n"
"    code += '<span class=\"sdk-function\">console</span>.<span class=\"sdk-function\">log</span>(result);\\n\\n';\n"
"    code += '<span class=\"sdk-keyword\">await</span> browser.<span class=\"sdk-function\">close</span>();\\n';\n"
"  } else {\n"
"    code += '<span class=\"sdk-comment\">// Initialize browser and create page</span>\\n';\n"
"    code += '<span class=\"sdk-keyword\">const</span> browser = <span class=\"sdk-keyword\">new</span> <span class=\"sdk-function\">Browser</span>();\\n';\n"
"    code += '<span class=\"sdk-keyword\">await</span> browser.<span class=\"sdk-function\">launch</span>();\\n\\n';\n"
"    code += '<span class=\"sdk-keyword\">const</span> page = <span class=\"sdk-keyword\">await</span> browser.<span class=\"sdk-function\">newPage</span>();\\n';\n"
"    if (contextId) {\n"
"      code += '<span class=\"sdk-comment\">// Context ID \\\"' + contextId + '\\\" is managed internally by page object</span>\\n\\n';\n"
"    } else {\n"
"      code += '<span class=\"sdk-comment\">// Context ID is managed internally by page object</span>\\n\\n';\n"
"    }\n"
"    code += '<span class=\"sdk-comment\">// Execute the action</span>\\n';\n"
"    if (isOnContext) {\n"
"      code += `<span class=\"sdk-keyword\">await</span> ${target}.<span class=\"sdk-function\">${method}</span>();\\n\\n`;\n"
"    } else {\n"
"      code += `<span class=\"sdk-keyword\">const</span> result = <span class=\"sdk-keyword\">await</span> ${target}.<span class=\"sdk-function\">${method}</span>(${paramsStr});\\n`;\n"
"      code += '<span class=\"sdk-function\">console</span>.<span class=\"sdk-function\">log</span>(result);\\n\\n';\n"
"    }\n"
"    code += '<span class=\"sdk-keyword\">await</span> browser.<span class=\"sdk-function\">close</span>();\\n';\n"
"  }\n"
"  return code;\n"
"}\n"
"\n"
"function generateCurlCode(toolName, params) {\n"
"  const endpoint = `/execute/${toolName}`;\n"
"  const token = document.getElementById('authToken').value || 'YOUR_TOKEN';\n"
"  const baseUrl = window.location.origin;\n"
"  \n"
"  // Filter out empty values for cleaner output\n"
"  const filteredParams = {};\n"
"  for (const [k, v] of Object.entries(params)) {\n"
"    if (v !== '' && v !== 0 && v !== false) {\n"
"      filteredParams[k] = v;\n"
"    }\n"
"  }\n"
"  \n"
"  const jsonBody = JSON.stringify(filteredParams, null, 2);\n"
"  const escapedBody = jsonBody.replace(/'/g, \"'\\\\''\");\n"
"  \n"
"  let code = '<span class=\"sdk-comment\"># HTTP API Request</span>\\n';\n"
"  code += '<span class=\"sdk-comment\"># Endpoint: POST ' + endpoint + '</span>\\n\\n';\n"
"  code += '<span class=\"sdk-function\">curl</span> -X <span class=\"sdk-string\">POST</span> <span class=\"sdk-string\">\\\"' + baseUrl + endpoint + '\\\"</span> <span class=\"sdk-keyword\">\\\\</span>\\n';\n"
"  code += '  -H <span class=\"sdk-string\">\\\"Content-Type: application/json\\\"</span> <span class=\"sdk-keyword\">\\\\</span>\\n';\n"
"  code += '  -H <span class=\"sdk-string\">\\\"Authorization: Bearer ' + token + '\\\"</span> <span class=\"sdk-keyword\">\\\\</span>\\n';\n"
"  \n"
"  // Format JSON body with syntax highlighting\n"
"  const lines = jsonBody.split('\\n');\n"
"  if (lines.length === 1) {\n"
"    code += '  -d <span class=\"sdk-string\">\\'' + jsonBody + '\\'</span>';\n"
"  } else {\n"
"    code += '  -d <span class=\"sdk-string\">\\'' + lines[0] + '</span>\\n';\n"
"    for (let i = 1; i < lines.length - 1; i++) {\n"
"      code += '<span class=\"sdk-string\">' + lines[i] + '</span>\\n';\n"
"    }\n"
"    code += '<span class=\"sdk-string\">' + lines[lines.length - 1] + '\\'</span>';\n"
"  }\n"
"  \n"
"  return code;\n"
"}\n"
"\n"
"function updateSdkSnippets() {\n"
"  if (!currentTool) return;\n"
"  const params = {};\n"
"  for (const param of currentTool.parameters) {\n"
"    const el = document.getElementById(`param_${param.name}`);\n"
"    if (el) {\n"
"      if (param.type === 'boolean') params[param.name] = el.checked;\n"
"      else if (param.type === 'integer') params[param.name] = el.value ? parseInt(el.value) : 0;\n"
"      else if (param.type === 'number') params[param.name] = el.value ? parseFloat(el.value) : 0;\n"
"      else params[param.name] = el.value || '';\n"
"    }\n"
"  }\n"
"  const curlPre = document.querySelector('#curlCode pre');\n"
"  const pythonPre = document.querySelector('#pythonCode pre');\n"
"  const nodePre = document.querySelector('#nodejsCode pre');\n"
"  if (curlPre) curlPre.innerHTML = generateCurlCode(currentTool.name, params);\n"
"  if (pythonPre) pythonPre.innerHTML = generatePythonCode(currentTool.name, params);\n"
"  if (nodePre) nodePre.innerHTML = generateNodeCode(currentTool.name, params);\n"
"}\n"
"\n"
"// Hook into renderToolContent to update SDK snippets\n"
"const originalRenderToolContent = renderToolContent;\n"
"renderToolContent = function() {\n"
"  originalRenderToolContent();\n"
"  setTimeout(() => {\n"
"    updateSdkSnippets();\n"
"    document.querySelectorAll('.form-input, .form-select, .form-checkbox input').forEach(el => {\n"
"      el.addEventListener('input', updateSdkSnippets);\n"
"      el.addEventListener('change', updateSdkSnippets);\n"
"    });\n"
"  }, 0);\n"
"};\n"
"\n"
"// ==================== IMAGE ZOOM FUNCTIONS ====================\n"
"\n"
"let zoomState = { scale: 1, panX: 0, panY: 0, isDragging: false, startX: 0, startY: 0 };\n"
"\n"
"function initImageZoom() {\n"
"  const viewport = document.getElementById('imageViewport');\n"
"  const img = document.getElementById('previewImg');\n"
"  const zoomIn = document.getElementById('zoomIn');\n"
"  const zoomOut = document.getElementById('zoomOut');\n"
"  const zoomReset = document.getElementById('zoomReset');\n"
"  const zoomLevel = document.getElementById('zoomLevel');\n"
"  \n"
"  if (!viewport || !img) return;\n"
"  \n"
"  // Reset state\n"
"  zoomState = { scale: 1, panX: 0, panY: 0, isDragging: false, startX: 0, startY: 0 };\n"
"  updateZoomTransform();\n"
"  \n"
"  // Zoom in button\n"
"  zoomIn.addEventListener('click', () => {\n"
"    zoomState.scale = Math.min(zoomState.scale * 1.25, 5);\n"
"    updateZoomTransform();\n"
"  });\n"
"  \n"
"  // Zoom out button\n"
"  zoomOut.addEventListener('click', () => {\n"
"    zoomState.scale = Math.max(zoomState.scale / 1.25, 0.5);\n"
"    if (zoomState.scale <= 1) {\n"
"      zoomState.panX = 0;\n"
"      zoomState.panY = 0;\n"
"    }\n"
"    updateZoomTransform();\n"
"  });\n"
"  \n"
"  // Reset button\n"
"  zoomReset.addEventListener('click', () => {\n"
"    zoomState.scale = 1;\n"
"    zoomState.panX = 0;\n"
"    zoomState.panY = 0;\n"
"    updateZoomTransform();\n"
"  });\n"
"  \n"
"  // Mouse wheel zoom\n"
"  viewport.addEventListener('wheel', (e) => {\n"
"    e.preventDefault();\n"
"    const delta = e.deltaY > 0 ? 0.9 : 1.1;\n"
"    zoomState.scale = Math.min(Math.max(zoomState.scale * delta, 0.5), 5);\n"
"    if (zoomState.scale <= 1) {\n"
"      zoomState.panX = 0;\n"
"      zoomState.panY = 0;\n"
"    }\n"
"    updateZoomTransform();\n"
"  });\n"
"  \n"
"  // Drag to pan (only when zoomed)\n"
"  viewport.addEventListener('mousedown', (e) => {\n"
"    if (zoomState.scale <= 1) return;\n"
"    zoomState.isDragging = true;\n"
"    zoomState.startX = e.clientX - zoomState.panX;\n"
"    zoomState.startY = e.clientY - zoomState.panY;\n"
"    viewport.classList.add('dragging');\n"
"  });\n"
"  \n"
"  document.addEventListener('mousemove', (e) => {\n"
"    if (!zoomState.isDragging) return;\n"
"    zoomState.panX = e.clientX - zoomState.startX;\n"
"    zoomState.panY = e.clientY - zoomState.startY;\n"
"    updateZoomTransform();\n"
"  });\n"
"  \n"
"  document.addEventListener('mouseup', () => {\n"
"    if (zoomState.isDragging) {\n"
"      zoomState.isDragging = false;\n"
"      const viewport = document.getElementById('imageViewport');\n"
"      if (viewport) viewport.classList.remove('dragging');\n"
"    }\n"
"  });\n"
"}\n"
"\n"
"function updateZoomTransform() {\n"
"  const img = document.getElementById('previewImg');\n"
"  const viewport = document.getElementById('imageViewport');\n"
"  const zoomLevel = document.getElementById('zoomLevel');\n"
"  \n"
"  if (!img || !viewport) return;\n"
"  \n"
"  img.style.transform = `translate(${zoomState.panX}px, ${zoomState.panY}px) scale(${zoomState.scale})`;\n"
"  \n"
"  if (zoomLevel) {\n"
"    zoomLevel.textContent = Math.round(zoomState.scale * 100) + '%';\n"
"  }\n"
"  \n"
"  if (zoomState.scale > 1) {\n"
"    viewport.classList.add('zoomed');\n"
"  } else {\n"
"    viewport.classList.remove('zoomed');\n"
"  }\n"
"}\n"
"\n"
"// Download OpenAPI spec\n"
"async function downloadOpenAPI() {\n"
"  if (!schema) {\n"
"    alert('Schema not loaded yet. Please wait...');\n"
"    return;\n"
"  }\n"
"  \n"
"  const openapi = {\n"
"    openapi: '3.0.3',\n"
"    info: {\n"
"      title: 'Owl Browser API',\n"
"      description: 'REST API for browser automation with anti-detection capabilities',\n"
"      version: '1.1.0'\n"
"    },\n"
"    servers: [{ url: window.location.origin, description: 'Current server' }],\n"
"    security: [{ bearerAuth: [] }],\n"
"    components: {\n"
"      securitySchemes: {\n"
"        bearerAuth: { type: 'http', scheme: 'bearer' }\n"
"      }\n"
"    },\n"
"    paths: {}\n"
"  };\n"
"  \n"
"  // Convert tools to OpenAPI paths\n"
"  for (const [toolName, tool] of Object.entries(schema.tools)) {\n"
"    const path = `/api/execute/${toolName}`;\n"
"    const properties = {};\n"
"    const required = [];\n"
"    \n"
"    if (tool.parameters) {\n"
"      for (const param of tool.parameters) {\n"
"        properties[param.name] = {\n"
"          type: param.type === 'boolean' ? 'boolean' : param.type === 'number' ? 'number' : 'string',\n"
"          description: param.description || ''\n"
"        };\n"
"        if (param.enum && param.enum.length > 0) {\n"
"          properties[param.name].enum = param.enum;\n"
"        }\n"
"        if (param.required) required.push(param.name);\n"
"      }\n"
"    }\n"
"    \n"
"    openapi.paths[path] = {\n"
"      post: {\n"
"        summary: toolName.replace(/_/g, ' ').replace(/\\b\\w/g, c => c.toUpperCase()),\n"
"        description: tool.description || '',\n"
"        tags: [tool.category || 'General'],\n"
"        requestBody: {\n"
"          required: true,\n"
"          content: {\n"
"            'application/json': {\n"
"              schema: {\n"
"                type: 'object',\n"
"                properties,\n"
"                required: required.length > 0 ? required : undefined\n"
"              }\n"
"            }\n"
"          }\n"
"        },\n"
"        responses: {\n"
"          '200': { description: 'Successful response' },\n"
"          '400': { description: 'Bad request' },\n"
"          '401': { description: 'Unauthorized' }\n"
"        }\n"
"      }\n"
"    };\n"
"  }\n"
"  \n"
"  // Download as JSON file\n"
"  const blob = new Blob([JSON.stringify(openapi, null, 2)], { type: 'application/json' });\n"
"  const url = URL.createObjectURL(blob);\n"
"  const a = document.createElement('a');\n"
"  a.href = url;\n"
"  a.download = 'owl-browser-openapi.json';\n"
"  a.click();\n"
"  URL.revokeObjectURL(url);\n"
"}\n"
"\n"
"// Initialize\n"
"init();\n"
"</script>\n"
"</body>\n"
"</html>\n";

static size_t g_html_size = 0;

void playground_init(void) {
    g_html_size = strlen(PLAYGROUND_HTML);
}

const char* playground_get_html(void) {
    return PLAYGROUND_HTML;
}

size_t playground_get_html_size(void) {
    if (g_html_size == 0) {
        g_html_size = strlen(PLAYGROUND_HTML);
    }
    return g_html_size;
}

void playground_shutdown(void) {
    // Nothing to clean up for static content
}

// ============================================================================
// Logo SVG (embedded from owl-logo.svg)
// ============================================================================

static const char LOGO_SVG[] =
"<svg width=\"100%\" height=\"100%\" viewBox=\"0 0 4267 4267\" version=\"1.1\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" xml:space=\"preserve\" xmlns:serif=\"http://www.serif.com/\" style=\"fill-rule:evenodd;clip-rule:evenodd;stroke-linejoin:round;stroke-miterlimit:2;\"><g><ellipse cx=\"2529.824\" cy=\"2246.008\" rx=\"1154.138\" ry=\"1079.333\" style=\"fill:#ebebeb;\"/><path d=\"M2833.853,2408.763c-42.906,10.099 -110.658,9.938 -149.076,-0.374c-149.61,-40.128 -245.308,-162.808 -245.682,-314.77c-0.107,-30.67 16.03,-111.299 23.777,-119.047c0.962,-1.015 12.931,6.732 26.556,17.098c73.149,55.837 167.083,29.495 203.203,-57.012c13.786,-33.021 10.58,-67.966 -9.832,-107.132l-18.06,-34.624l14.053,-6.626c24.045,-11.381 115.948,-12.877 155.595,-2.565c75.286,19.61 151.534,74.111 192.944,138.016c11.06,17.045 26.876,50.707 35.158,74.805c12.557,36.655 15.014,52.471 14.961,97.193c-0.053,60.165 -6.679,89.072 -33.662,146.939c-35.853,76.943 -124.551,148.007 -209.936,168.098Zm30.51,309.854c-194.494,33.93 -405.605,-34.571 -548.75,-178.09c-96.392,-96.659 -153.938,-205.073 -176.22,-331.922c-9.19,-52.203 -9.244,-168.258 -0.16,-219.072c40.876,-228.049 205.501,-417.306 427.726,-491.791l47.448,-15.869l44.509,27.464c69.195,42.692 76.034,48.089 67.752,53.379c-3.954,2.511 -13.892,4.595 -22.068,4.595c-26.502,0.16 -96.232,16.671 -137.428,32.54c-90.354,34.784 -188.937,112.956 -245.415,194.654c-92.758,134.169 -119.635,306.167 -71.706,458.289c60.218,191.021 209.722,325.029 410.681,368.095c46.112,9.885 167.831,8.442 212.34,-2.511c193.051,-47.501 349.768,-198.18 397.857,-382.629c25.38,-97.3 22.174,-205.928 -9.083,-306.274c-5.61,-18.167 -9.725,-33.449 -9.137,-33.983c3.206,-2.725 118.192,78.385 119.528,84.37c9.938,43.601 12.076,63.852 12.289,117.177c0.481,132.405 -30.937,237.988 -103.926,349.34c-92.919,141.863 -247.285,242.796 -416.238,272.238Zm946.019,160.243l10.875,1.841l-10.875,-1.841Z\" style=\"fill:#21241e;fill-rule:nonzero;\"/><path d=\"M3256.77,1897.042c-2.298,-1.229 -3.74,-1.817 -4.061,-1.55c-0.588,0.534 3.527,15.816 9.137,33.983c31.258,100.346 34.464,208.974 9.083,306.274c-48.089,184.448 -204.806,335.127 -397.857,382.629c-44.509,10.954 -166.228,12.396 -212.34,2.511c-200.959,-43.066 -350.463,-177.075 -410.681,-368.095c-47.929,-152.122 -21.052,-324.12 71.706,-458.289c56.478,-81.698 155.061,-159.869 245.415,-194.654c41.196,-15.869 110.925,-32.38 137.428,-32.54c8.175,0 18.114,-2.084 22.068,-4.595c5.13,-3.259 4.488,-6.572 -13.037,-18.648c7.534,4.862 13.144,8.549 15.602,10.312c6.412,4.542 36.281,23.136 66.416,41.357c79.24,47.929 215.439,135.718 313.915,202.348c39.273,26.556 100.079,67.432 147.206,98.957Zm-422.917,511.721c85.385,-20.091 174.082,-91.156 209.936,-168.098c26.983,-57.867 33.609,-86.774 33.662,-146.939c0.053,-44.723 -2.404,-60.539 -14.961,-97.193c-8.282,-24.098 -24.098,-57.76 -35.158,-74.805c-41.41,-63.905 -117.658,-118.406 -192.944,-138.016c-39.647,-10.312 -131.55,-8.816 -155.595,2.565l-14.053,6.626l13.625,26.128c-43.12,-68.019 -128.184,-80.255 -191.234,-23.564c-29.815,26.77 -43.601,56.905 -43.494,94.949c0.107,39.754 12.717,71.653 37.189,94.896c-9.725,-7 -17.205,-11.541 -17.953,-10.74c-7.748,7.748 -23.884,88.377 -23.777,119.047c0.374,151.961 96.071,274.642 245.682,314.77c38.418,10.312 106.17,10.473 149.076,0.374Zm-135.985,-541.804c5.504,23.777 3.74,46.166 -5.236,67.699c-0.855,1.977 -1.71,3.901 -2.565,5.824c9.778,-23.083 12.663,-48.463 7.801,-73.523Zm-141.596,153.084c20.571,1.55 43.814,-1.443 62.356,-8.282c-19.556,8.122 -40.876,11.007 -62.356,8.282Zm77.049,-526.469l-18.915,-11.702l0.107,-0.053l18.808,11.755Z\" style=\"fill:#62935a;fill-rule:nonzero;\"/><path d=\"M3256.77,1897.042c-47.127,-31.525 -107.933,-72.401 -147.206,-98.957c-98.476,-66.63 -234.675,-154.419 -313.915,-202.348c-30.136,-18.22 -60.004,-36.815 -66.416,-41.357c-2.458,-1.763 -8.068,-5.45 -15.602,-10.312c-10.9,-7.481 -28.212,-18.381 -54.715,-34.731l-25.594,-15.763l-18.808,-11.755l-0.107,0.053l-47.448,15.869c-59.363,19.877 -114.559,47.982 -164.465,82.767c46.753,-32.647 98.209,-59.524 153.458,-79.347c55.944,-20.144 57.386,-21.159 42.425,-30.937c-6.839,-4.435 -38.899,-22.014 -71.225,-39.006c-32.327,-16.991 -77.263,-41.57 -99.865,-54.554c-51.028,-29.334 -177.182,-87.308 -244.773,-112.475l-50.761,-18.862l-123.535,-2.618c-165.32,-3.42 -286.557,7.32 -445.519,39.593c-338.707,68.661 -647.653,214.638 -892.32,421.581c-50.226,42.479 -165.64,159.602 -165.64,168.045c0,2.992 -2.084,5.397 -4.649,5.397c-11.167,0 30.991,-170.556 64.707,-261.818c42.158,-114.238 104.781,-239.964 152.496,-306.167c10.152,-14.106 17.579,-26.289 16.511,-27.037c-1.069,-0.801 -25.006,-12.77 -53.112,-26.502c-102.537,-50.226 -190.54,-138.71 -240.018,-241.407c-23.724,-49.104 -39.967,-93.507 -34.25,-93.507c1.282,0 23.35,10.473 48.997,23.296c52.471,26.182 91.797,39.807 169.541,58.722c101.575,24.686 107.346,23.777 62.462,-9.992c-79.294,-59.577 -183.754,-191.021 -227.301,-285.863c-44.776,-97.567 -71.225,-201.654 -76.088,-299.114l-3.046,-61.447l13.786,26.716c19.182,37.082 43.066,73.843 80.255,123.375c146.618,195.349 371.729,318.083 679.873,370.553c70.424,12.022 222.011,24.739 351.691,29.548c372.37,13.839 520.324,26.93 699.108,62.035c143.359,28.105 249.155,62.516 374.614,121.933c268.497,127.169 541.59,358.851 810.835,687.941c76.836,93.934 101.895,127.81 195.723,264.383c41.089,59.844 80.95,115.788 88.591,124.337c7.587,8.496 13.892,18.114 13.999,21.373c0.107,3.206 25.915,22.709 57.386,43.28c68.874,44.99 183.914,156.022 221.904,214.157c69.783,106.758 102.109,201.76 108.948,320.166c6.145,107.132 -24.632,245.789 -76.729,345.44c-7.481,14.266 -14.32,25.915 -15.228,25.915c-0.908,0 -8.229,-17.419 -16.297,-38.738c-29.174,-77.21 -95.59,-180.334 -116.108,-180.334c-6.465,0 -69.462,14.106 -139.993,31.365c-70.531,17.259 -139.993,32.914 -154.419,34.731c-14.373,1.87 -28.96,5.664 -32.327,8.496c-3.366,2.778 -16.03,6.679 -28.159,8.603l-22.068,3.527l39.219,-88.056c101.521,-228.049 153.084,-433.763 166.709,-665.393c2.885,-49.799 2.244,-55.73 -6.893,-62.89c-24.793,-19.342 -297.671,-200.211 -299.435,-198.448c-1.069,1.069 0.427,14.694 3.42,30.243c3.74,19.77 5.397,52.577 5.13,87.415l0,-1.122c-0.214,-53.325 -2.351,-73.576 -12.289,-117.177c-1.229,-5.397 -94.522,-71.706 -115.467,-82.82Zm-633.707,1462.709c-228.851,30.029 -487.356,-30.884 -680.995,-160.404c-269.139,-180.067 -442.259,-456.472 -484.097,-773.005c-10.366,-78.439 -7.587,-232.965 5.717,-316.212c37.242,-233.179 146.511,-443.007 316.159,-606.991c60.272,-58.241 128.344,-112.261 177.182,-140.58c18.541,-10.74 34.998,-19.556 36.655,-19.556c1.603,0 -19.556,23.671 -47.074,52.577c-59.951,63.104 -99.972,114.719 -140.741,181.456c-74.324,121.559 -127.864,272.932 -150.038,424.146c-13.518,92.064 -8.549,264.757 10.473,364.141c55.356,289.176 230.24,556.07 462.777,706.161c132.726,85.705 253.109,133.901 395.399,158.373c63.264,10.9 231.522,9.404 299.221,-2.672c115.521,-20.625 268.978,-81.645 376.965,-150.038l31.792,-20.091l-21.373,22.388c-11.755,12.343 -39.433,37.029 -61.447,54.928c-150.733,122.2 -332.296,199.944 -526.575,225.377Zm-2081.776,430.077c-1.924,5.504 -262.566,11.541 -325.563,7.534l-35.265,-2.244l41.463,-81.271c53.058,-104.086 110.391,-249.369 147.74,-374.4c3.42,-11.541 9.778,-16.991 25.754,-22.014c137.374,-43.601 172.854,-56.638 223.775,-82.499c92.278,-46.86 175.044,-115.628 257.009,-213.516c30.617,-36.601 37.456,-42.318 41.677,-34.838c8.656,15.495 0.321,201.333 -11.595,259.147c-41.677,202.081 -154.74,380.972 -319.579,505.844c-23.991,18.167 -44.402,35.372 -45.417,38.258Zm707.07,7.694c-21.8,3.633 -370.66,3.259 -389.628,-0.427c-14.747,-2.832 -14.747,-4.435 -0.695,-54.661c21.533,-76.836 77.049,-172.533 136.894,-235.957c32.968,-34.998 34.036,-35.586 53.593,-31.365c149.397,32.38 333.898,-25.594 464.647,-146.137l24.205,-22.281l-4.007,23.991c-14.48,86.721 -61.768,205.982 -111.674,281.749c-30.724,46.593 -106.17,131.818 -136.092,153.725c-12.076,8.816 -20.945,18.755 -19.77,22.068c1.229,3.473 -6.145,7.427 -17.472,9.297Zm763.174,-7.855c0,4.221 -72.454,5.878 -254.231,5.878l-254.231,0l3.633,-17.366c11.488,-54.554 58.241,-145.977 105.315,-205.608c49.104,-62.249 152.603,-146.939 246.911,-201.974c22.976,-13.412 25.808,-14.053 22.014,-4.916c-10.793,26.235 -18.968,59.203 -24.312,98.048c-7.213,52.898 2.885,129.627 22.869,173.174l13.465,29.495l23.243,-2.885c69.355,-8.816 159.709,-53.432 220.515,-109.002c16.35,-14.961 29.762,-25.006 29.762,-22.335c0,9.992 -26.502,72.027 -43.494,101.682c-24.472,42.906 -59.15,91.69 -87.469,123.215c-13.198,14.694 -23.991,29.334 -23.991,32.594Zm-1474.732,-742.335c-19.075,14.213 -53.432,36.868 -76.248,50.387c-37.777,22.281 -41.25,23.457 -38.845,12.503c12.77,-57.921 34.945,-258.666 43.334,-392.353c2.565,-40.929 5.61,-56.425 12.343,-62.516c19.93,-18.007 69.409,-35.052 126.207,-43.44c50.226,-7.427 133.207,-25.06 178.517,-37.99l12.877,-3.687l-3.473,37.403c-15.121,162.434 -115.734,336.089 -254.712,439.694Zm523.637,-1006.558c-22.014,3.954 -62.569,7.267 -90.087,7.374l-50.066,0.16l3.633,-24.098c7.748,-51.562 47.341,-145.122 84.797,-200.318c24.098,-35.479 87.148,-99.331 124.711,-126.261c58.882,-42.212 148.328,-76.943 197.593,-76.675l20.625,0.107l-26.716,16.724c-14.694,9.19 -37.029,25.167 -49.639,35.426c-29.976,24.472 -81.805,93.613 -101.895,135.878c-17.579,36.975 -34.891,97.835 -30.884,108.361c2.992,7.748 56.478,18.327 117.551,23.243l41.998,3.42l-42.479,25.273c-47.127,27.945 -137.588,60.379 -199.142,71.386Zm56.104,1240.698c-57.333,12.503 -65.775,12.824 -68.767,2.618c-17.74,-60.325 -16.938,-184.769 1.656,-257.971c22.976,-90.461 90.835,-204.325 149.343,-250.651l22.388,-17.74l-14.266,31.365c-25.22,55.516 -35.052,83.835 -45.311,130.161c-11.435,51.509 -12.877,135.184 -3.099,181.67l6.145,29.388l32.059,-1.71c73.095,-3.954 153.404,-30.724 223.454,-74.538c18.167,-11.381 36.387,-22.709 40.502,-25.113c4.488,-2.672 2.298,4.916 -5.343,18.862c-20.197,36.708 -108.628,123.108 -159.763,156.022c-47.181,30.456 -132.833,67.538 -178.998,77.637Zm-156.557,-600.633c-24.419,7.694 -45.471,13.999 -46.753,13.999c-6.091,0 -1.603,-141.97 5.664,-180.334c15.228,-80.255 46.7,-155.595 91.049,-218.164c22.709,-32.006 93.507,-102.537 126.795,-126.207l17.098,-12.183l-10.954,18.648c-18.701,31.899 -48.997,112.902 -58.562,156.717c-10.419,47.662 -9.404,164.411 1.55,175.365c9.297,9.297 73.469,-2.939 127.062,-24.258l44.883,-17.793l-9.19,15.976c-14.908,25.861 -83.942,92.064 -122.787,117.765c-50.387,33.288 -113.757,64.012 -165.854,80.469Zm-357.462,-311.724c-5.183,1.069 -9.938,2.084 -14.373,2.992c-30.937,6.465 -44.563,9.351 -49.852,3.954c-4.221,-4.221 -3.206,-13.625 -1.496,-30.35c0.321,-3.046 0.695,-6.358 1.015,-9.885c5.397,-55.516 15.175,-93.56 41.036,-159.442c31.525,-80.362 84.37,-152.763 149.664,-205.233c31.685,-25.434 89.553,-61.768 98.369,-61.768c1.87,0 -5.984,15.014 -17.472,33.395c-43.173,69.141 -69.248,151.855 -73.469,233.285l-2.939,55.57l68.874,-3.313c37.884,-1.87 74.859,-4.809 82.232,-6.572c11.755,-2.832 10.847,-0.908 -8.015,16.243c-26.823,24.419 -81.11,60.379 -123.535,81.805c-43.173,21.8 -88.324,36.601 -150.038,49.318Zm1799.066,1402.117c-42.586,7.908 -96.712,15.709 -120.223,17.312l-42.746,2.885l37.403,-38.471c21.8,-22.442 66.79,-58.402 107.826,-86.24c51.883,-35.265 83.622,-61.768 120.864,-100.88c27.731,-29.174 51.936,-51.562 53.806,-49.745c9.992,9.992 -7.801,130.802 -27.571,187.334c-17.098,48.784 -11.702,45.952 -129.36,67.806Zm1429.581,-894.243c-5.076,-8.282 -17.312,-7.213 -20.785,1.87c-1.71,4.488 0.801,8.549 6.145,9.725c11.274,2.618 19.449,-3.847 14.64,-11.595Zm-1188.869,-162.114c-120.543,-23.564 -235.156,-82.553 -326.685,-174.35c89.285,89.499 205.02,149.824 326.685,174.35Zm688.155,-357.248c-24.312,53.646 -56.104,103.552 -94.522,148.435c16.083,-18.968 31.044,-38.899 44.669,-59.684c19.556,-29.762 36.067,-59.15 49.852,-88.751Zm-216.08,258.025c-18.327,12.717 -37.349,24.525 -57.173,35.479c-42.212,23.296 -86.453,41.143 -131.711,53.593c67.432,-19.022 131.443,-49.425 188.883,-89.072Zm266.093,-437.878c-1.069,10.473 -2.351,20.197 -3.794,28.8c-0.214,1.229 -0.427,2.458 -0.641,3.687c1.763,-10.686 3.206,-21.533 4.435,-32.487Z\" style=\"fill:#85704f;fill-rule:nonzero;\"/><path d=\"M2623.063,3359.751c194.28,-25.434 375.843,-103.178 526.575,-225.377c22.014,-17.9 49.692,-42.586 61.447,-54.928l21.373,-22.388l-31.792,20.091c-27.25,17.259 -57.386,34.036 -89.125,49.852c48.517,-25.167 93.667,-53.379 132.352,-83.194c140.206,-108.254 246.644,-239.003 325.349,-399.727c42.265,-86.293 71.065,-167.617 87.896,-248.46c5.824,-27.892 13.091,-59.203 16.19,-69.462c1.817,-6.038 3.901,-20.785 5.771,-38.632c-17.472,213.302 -68.661,406.78 -163.503,619.708l-39.219,88.056l22.068,-3.527c3.206,-0.481 6.412,-1.176 9.511,-1.87c-3.687,1.122 -7.481,2.191 -11.007,3.046c-16.511,3.847 -22.121,10.206 -48.089,54.875c-148.969,256.101 -336.089,437.29 -585.084,566.703c-116.75,60.699 -236.171,105.262 -369.912,137.214c24.739,-8.549 26.93,-19.342 37.082,-48.303c19.77,-56.531 37.563,-177.342 27.571,-187.334c-1.87,-1.817 -26.075,20.571 -53.806,49.745c-37.242,39.112 -68.981,65.615 -120.864,100.88c-41.036,27.838 -86.026,63.798 -107.826,86.24l-37.403,38.471l42.746,-2.885c23.51,-1.603 77.637,-9.404 120.223,-17.312c5.343,-1.015 10.473,-1.924 15.335,-2.832c-45.097,8.816 -91.85,16.404 -140.741,22.869c-19.503,2.565 -523.637,5.13 -1028.893,6.412c0.427,-0.053 0.801,-0.107 1.069,-0.16c11.328,-1.87 18.701,-5.824 17.472,-9.297c-1.176,-3.313 7.694,-13.251 19.77,-22.068c29.922,-21.907 105.369,-107.132 136.092,-153.725c49.906,-75.767 97.193,-195.028 111.674,-281.749l4.007,-23.991l-24.205,22.281c-130.749,120.543 -315.251,178.517 -464.647,146.137c-19.556,-4.221 -20.625,-3.633 -53.593,31.365c-59.844,63.424 -115.36,159.121 -136.894,235.957c-14.053,50.226 -14.053,51.829 0.695,54.661c2.458,0.481 10.419,0.908 22.495,1.282c-214.37,0.267 -404.803,0.214 -527.377,-0.321c89.766,-1.336 186.265,-4.862 187.441,-8.229c1.015,-2.885 21.426,-20.091 45.417,-38.258c164.839,-124.871 277.901,-303.763 319.579,-505.844c11.915,-57.814 20.251,-243.651 11.595,-259.147c-4.221,-7.481 -11.06,-1.763 -41.677,34.838c-81.965,97.888 -164.732,166.655 -257.009,213.516c-50.921,25.861 -86.4,38.899 -223.775,82.499c-15.976,5.023 -22.335,10.473 -25.754,22.014c-4.809,16.03 -9.938,32.433 -15.335,49.051c18.007,-60.111 37.563,-143.199 70.691,-298.259c2.778,-13.198 8.656,-52.898 12.984,-88.163c0.908,-7.427 1.817,-14.587 2.672,-21.533c-6.839,57.707 -13.732,105.849 -18.968,129.68c-2.404,10.954 1.069,9.778 38.845,-12.503c22.816,-13.518 57.173,-36.174 76.248,-50.387c138.977,-103.605 239.59,-277.26 254.712,-439.694l3.473,-37.403l-12.877,3.687c-45.311,12.931 -128.291,30.563 -178.517,37.99c-56.799,8.389 -106.277,25.434 -126.207,43.44c-6.732,6.091 -9.778,21.587 -12.343,62.516c-1.87,29.708 -4.435,62.783 -7.374,96.819c5.664,-83.354 7.053,-181.937 9.671,-405.712c4.381,-371.568 8.389,-450.969 26.823,-531.651c2.351,-10.419 4.702,-20.518 6.946,-30.296c-3.206,18.701 -3.901,30.296 -1.015,30.296c2.565,0 4.649,-2.404 4.649,-5.397c0,-8.442 115.414,-125.566 165.64,-168.045c244.667,-206.943 553.612,-352.92 892.32,-421.581c158.961,-32.273 280.199,-43.013 445.519,-39.593l123.535,2.618l50.761,18.862c19.77,7.374 44.563,17.526 71.012,29.014c-55.997,-23.617 -104.514,-41.036 -119.047,-41.036c-29.976,0 -148.435,99.705 -227.515,186.853c12.289,-14.053 25.434,-28.426 39.647,-43.44c27.518,-28.907 48.677,-52.577 47.074,-52.577c-1.656,0 -18.114,8.816 -36.655,19.556c-48.837,28.319 -116.91,82.339 -177.182,140.58c-169.648,163.984 -278.917,373.812 -316.159,606.991c-13.305,83.248 -16.083,237.774 -5.717,316.212c41.837,316.533 214.958,592.938 484.097,773.005c193.639,129.52 452.144,190.433 680.995,160.404Zm1065.066,-1185.022c-2.298,-4.488 -7.106,-11.274 -12.663,-17.419c-7.641,-8.549 -47.501,-64.493 -88.591,-124.337c-93.827,-136.573 -118.887,-170.449 -195.723,-264.383c-269.245,-329.09 -542.338,-560.772 -810.835,-687.941c-125.459,-59.417 -231.255,-93.827 -374.614,-121.933c-178.785,-35.105 -326.739,-48.196 -699.108,-62.035c-129.68,-4.809 -281.268,-17.526 -351.691,-29.548c-308.144,-52.471 -533.254,-175.205 -679.873,-370.553c-37.189,-49.532 -61.073,-86.293 -80.255,-123.375l-13.786,-26.716l3.046,61.447c2.298,46.166 9.458,93.774 21.052,141.489c-2.618,-10.366 -4.916,-20.465 -6.893,-30.136c-12.396,-60.646 -24.846,-168.739 -19.984,-173.602c1.924,-1.924 16.19,8.389 31.632,22.976c106.17,100.186 305.74,181.51 541.697,220.729c34.891,5.771 106.758,15.763 159.656,22.228c90.888,11.007 126.902,11.862 657.217,15.549c529.407,3.633 564.833,4.488 627.83,14.801c322.624,52.791 574.451,173.655 806.668,387.224c185.944,170.983 337.585,415.062 410.788,661.118c39.38,132.405 62.836,269.673 69.248,404.483c1.656,34.838 3.687,65.508 5.183,79.935Zm-1676.599,1614.938c0,-3.259 10.793,-17.9 23.991,-32.594c28.319,-31.525 62.997,-80.309 87.469,-123.215c16.991,-29.655 43.494,-91.69 43.494,-101.682c0,-2.672 -13.412,7.374 -29.762,22.335c-60.806,55.57 -151.16,100.186 -220.515,109.002l-23.243,2.885l-13.465,-29.495c-19.984,-43.547 -30.082,-120.276 -22.869,-173.174c5.343,-38.845 13.518,-71.813 24.312,-98.048c3.794,-9.137 0.962,-8.496 -22.014,4.916c-94.308,55.035 -197.806,139.725 -246.911,201.974c-47.074,59.63 -93.827,151.053 -105.315,205.608l-3.633,17.366l254.231,0c181.777,0 254.231,-1.656 254.231,-5.878Zm-951.095,-1748.893c61.554,-11.007 152.015,-43.44 199.142,-71.386l42.479,-25.273l-41.998,-3.42c-61.073,-4.916 -114.559,-15.495 -117.551,-23.243c-4.007,-10.526 13.305,-71.386 30.884,-108.361c20.091,-42.265 71.92,-111.406 101.895,-135.878c12.61,-10.259 34.945,-26.235 49.639,-35.426l26.716,-16.724l-20.625,-0.107c-49.265,-0.267 -138.71,34.464 -197.593,76.675c-37.563,26.93 -100.613,90.781 -124.711,126.261c-37.456,55.196 -77.049,148.756 -84.797,200.318l-3.633,24.098l50.066,-0.16c27.518,-0.107 68.073,-3.42 90.087,-7.374Zm56.104,1240.698c46.166,-10.099 131.818,-47.181 178.998,-77.637c51.135,-32.914 139.565,-119.314 159.763,-156.022c7.641,-13.946 9.832,-21.533 5.343,-18.862c-4.114,2.404 -22.335,13.732 -40.502,25.113c-70.05,43.814 -150.359,70.584 -223.454,74.538l-32.059,1.71l-6.145,-29.388c-9.778,-46.486 -8.335,-130.161 3.099,-181.67c10.259,-46.326 20.091,-74.645 45.311,-130.161l14.266,-31.365l-22.388,17.74c-58.508,46.326 -126.367,160.19 -149.343,250.651c-18.594,73.202 -19.396,197.646 -1.656,257.971c2.992,10.206 11.435,9.885 68.767,-2.618Zm-156.557,-600.633c52.097,-16.457 115.467,-47.181 165.854,-80.469c38.845,-25.701 107.88,-91.904 122.787,-117.765l9.19,-15.976l-44.883,17.793c-53.593,21.319 -117.765,33.555 -127.062,24.258c-10.954,-10.954 -11.969,-127.703 -1.55,-175.365c9.564,-43.814 39.861,-124.818 58.562,-156.717l10.954,-18.648l-17.098,12.183c-33.288,23.671 -104.086,94.201 -126.795,126.207c-44.349,62.569 -75.82,137.909 -91.049,218.164c-7.267,38.364 -11.755,180.334 -5.664,180.334c1.282,0 22.335,-6.305 46.753,-13.999Zm-357.462,-311.724c61.714,-12.717 106.865,-27.518 150.038,-49.318c42.425,-21.426 96.712,-57.386 123.535,-81.805c18.862,-17.152 19.77,-19.075 8.015,-16.243c-7.374,1.763 -44.349,4.702 -82.232,6.572l-68.874,3.313l2.939,-55.57c4.221,-81.431 30.296,-164.144 73.469,-233.285c11.488,-18.381 19.342,-33.395 17.472,-33.395c-8.816,0 -66.684,36.334 -98.369,61.768c-65.294,52.471 -118.139,124.871 -149.664,205.233c-25.861,65.882 -35.639,103.926 -41.036,159.442c-0.321,3.527 -0.695,6.839 -1.015,9.885c-1.71,16.724 -2.725,26.128 1.496,30.35c5.29,5.397 18.915,2.511 49.852,-3.954c4.435,-0.908 9.19,-1.924 14.373,-2.992Zm-46.272,-729.351c17.312,-53.058 35.907,-97.995 61.073,-151.481c7.748,-16.511 16.35,-33.823 25.273,-51.081c-28.159,55.837 -55.783,118.62 -77.797,178.304c-2.832,7.587 -5.664,15.709 -8.549,24.258Zm2006.33,1594.42c-14.854,-1.122 -27.838,-2.565 -38.097,-4.328c-46.7,-8.015 -91.049,-18.594 -134.222,-32.22c54.127,16.724 110.498,28.479 172.319,36.548Zm289.283,-12.663c-9.618,2.137 -19.022,4.007 -28.159,5.664c-28.853,5.13 -76.034,8.389 -125.512,9.511c54.394,-2.351 111.567,-6.946 138.871,-12.183c4.916,-0.962 9.832,-1.924 14.801,-2.992Zm-1042.09,-1650.898c-36.441,59.63 -67.485,123.749 -92.491,190.914c24.419,-65.935 54.287,-128.398 88.591,-184.502c1.282,-2.191 2.618,-4.275 3.901,-6.412Zm2132.964,831.14c-35.479,-42.425 -89.873,-95.804 -138.817,-136.68c49.104,40.822 103.231,94.041 138.817,136.68Zm-3697.73,1264.796c-7.748,16.511 -15.442,32.327 -23.083,47.288l-41.463,81.271l41.41,-81.271c7.641,-14.961 15.389,-30.83 23.136,-47.288Zm3292.499,-687.674c5.771,-1.603 13.091,-3.099 20.411,-4.061c-7,0.962 -14.32,2.458 -20.411,4.061Z\" style=\"fill:#c4a05b;fill-rule:nonzero;\"/><path d=\"M3612.5,2650c-4.798,-17.302 143.348,76.782 241.667,258.333l-375,83.333c0,0 138.131,-324.365 133.333,-341.667Z\" style=\"fill:#574d3c;\"/></g></svg>";

static size_t g_logo_size = 0;

const char* playground_get_logo(void) {
    return LOGO_SVG;
}

size_t playground_get_logo_size(void) {
    if (g_logo_size == 0) {
        g_logo_size = strlen(LOGO_SVG);
    }
    return g_logo_size;
}
