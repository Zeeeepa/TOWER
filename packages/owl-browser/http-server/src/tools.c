/**
 * Owl Browser HTTP Server - Tool Definitions Implementation
 *
 * Defines all available browser tools with their parameters,
 * validation rules, and documentation.
 */

#include "tools.h"
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Enum Values
// ============================================================================

static const char* PROXY_TYPES[] = {"http", "https", "socks4", "socks5", "socks5h", NULL};
static const char* SAME_SITE_VALUES[] = {"none", "lax", "strict", NULL};
static const char* CLEAN_LEVELS[] = {"minimal", "basic", "aggressive", NULL};
static const char* CAPTCHA_PROVIDERS[] = {"auto", "owl", "recaptcha", "cloudflare", "hcaptcha", NULL};
static const char* KEY_NAMES[] = {
    "Enter", "Return", "Tab", "Escape", "Esc", "Backspace", "Delete", "Del",
    "ArrowUp", "Up", "ArrowDown", "Down", "ArrowLeft", "Left", "ArrowRight", "Right",
    "Space", "Home", "End", "PageUp", "PageDown", NULL
};

// Network interception actions
static const char* NETWORK_ACTIONS[] = {"allow", "block", "mock", "redirect", NULL};

// Dialog types
static const char* DIALOG_TYPES[] = {"alert", "confirm", "prompt", "beforeunload", NULL};

// Dialog actions
static const char* DIALOG_ACTIONS[] = {"accept", "dismiss", "accept_with_text", NULL};

// Popup policies
static const char* POPUP_POLICIES[] = {"allow", "block", "new_tab", "background", NULL};

// IPC test modes
static const char* IPC_TEST_MODES[] = {"smoke", "full", "benchmark", "stress", "leak-check", "parallel", NULL};

// Navigation wait conditions
static const char* WAIT_UNTIL_VALUES[] = {"", "load", "domcontentloaded", "networkidle", NULL};

// ============================================================================
// Parameter Definitions
// ============================================================================

// Common parameters
static const ToolParam PARAM_CONTEXT_ID = {"context_id", PARAM_STRING, true, "The browser context ID", NULL, 0};
static const ToolParam PARAM_URL = {"url", PARAM_STRING, true, "The URL to navigate to", NULL, 0};
static const ToolParam PARAM_URL_OPT = {"url", PARAM_STRING, false, "Optional URL filter", NULL, 0};
static const ToolParam PARAM_SELECTOR = {"selector", PARAM_STRING, true, "CSS selector, coordinates, or semantic description", NULL, 0};
static const ToolParam PARAM_SELECTOR_OPT = {"selector", PARAM_STRING, false, "Optional CSS selector", NULL, 0};
static const ToolParam PARAM_TEXT = {"text", PARAM_STRING, true, "Text to type", NULL, 0};
static const ToolParam PARAM_VALUE = {"value", PARAM_STRING, true, "Value to select", NULL, 0};
static const ToolParam PARAM_TIMEOUT = {"timeout", PARAM_INT, false, "Timeout in milliseconds", NULL, 0};
static const ToolParam PARAM_QUERY = {"query", PARAM_STRING, true, "Query or command string", NULL, 0};

// ============================================================================
// Tool Parameter Arrays
// ============================================================================

// browser_create_context
static const ToolParam CREATE_CONTEXT_PARAMS[] = {
    {"llm_enabled", PARAM_BOOL, false,
        "Enable or disable LLM features for this context. When enabled, allows using AI-powered tools like "
        "browser_query_page, browser_summarize_page, and browser_nla. Default: true", NULL, 0},
    {"llm_use_builtin", PARAM_BOOL, false,
        "Use the built-in llama-server for LLM inference. When true, uses the bundled local model. "
        "Set to false to use an external LLM provider. Default: true", NULL, 0},
    {"llm_endpoint", PARAM_STRING, false,
        "External LLM API endpoint URL (e.g., 'https://api.openai.com/v1' for OpenAI). "
        "Only used when llm_use_builtin is false", NULL, 0},
    {"llm_model", PARAM_STRING, false,
        "External LLM model name (e.g., 'gpt-4-vision-preview' for OpenAI). "
        "Only used when llm_use_builtin is false", NULL, 0},
    {"llm_api_key", PARAM_STRING, false,
        "API key for the external LLM provider. Required when using external LLM endpoint", NULL, 0},
    {"profile_path", PARAM_STRING, false,
        "Path to a browser profile JSON file. If the file exists, loads fingerprints, cookies, and settings. "
        "If not, creates a new profile and saves it to this path. Useful for persistent browser sessions", NULL, 0},
    {"proxy_type", PARAM_ENUM, false,
        "Type of proxy server to use. Options: 'http', 'https', 'socks4', 'socks5', 'socks5h'. "
        "Use 'socks5h' for remote DNS resolution (recommended for privacy)", PROXY_TYPES, 5},
    {"proxy_host", PARAM_STRING, false,
        "Proxy server hostname or IP address (e.g., '127.0.0.1' or 'proxy.example.com')", NULL, 0},
    {"proxy_port", PARAM_INT, false,
        "Proxy server port number (e.g., 8080 for HTTP proxy, 9050 for Tor)", NULL, 0},
    {"proxy_username", PARAM_STRING, false,
        "Username for proxy authentication. Only required if the proxy server requires credentials", NULL, 0},
    {"proxy_password", PARAM_STRING, false,
        "Password for proxy authentication. Only required if the proxy server requires credentials", NULL, 0},
    {"proxy_stealth", PARAM_BOOL, false,
        "Enable stealth mode to prevent proxy/VPN detection. Blocks WebRTC leaks and other detection vectors. "
        "Default: true when proxy is configured", NULL, 0},
    {"proxy_ca_cert_path", PARAM_STRING, false,
        "Path to custom CA certificate file (.pem, .crt, .cer) for SSL interception proxies. "
        "Required when using Charles Proxy, mitmproxy, or similar HTTPS inspection tools", NULL, 0},
    {"proxy_trust_custom_ca", PARAM_BOOL, false,
        "Trust the custom CA certificate for SSL interception. Enable when using Charles Proxy, "
        "mitmproxy, or similar tools that intercept HTTPS traffic. Default: false", NULL, 0},
    {"is_tor", PARAM_BOOL, false,
        "Explicitly mark this proxy as a Tor connection. Enables circuit isolation so each context gets "
        "a unique exit node IP. Auto-detected if proxy is localhost:9050 or localhost:9150 with socks5/socks5h", NULL, 0},
    {"tor_control_port", PARAM_INT, false,
        "Tor control port for circuit isolation. Used to send SIGNAL NEWNYM to get a new exit node. "
        "Default: auto-detect (tries 9051 then 9151). Set to -1 to disable circuit isolation", NULL, 0},
    {"tor_control_password", PARAM_STRING, false,
        "Password for Tor control port authentication. Leave empty to use cookie authentication (default) or no auth", NULL, 0},
    {"resource_blocking", PARAM_BOOL, false,
        "Enable or disable resource blocking (ads, trackers, analytics). When enabled, blocks requests to known "
        "ad networks, trackers, and analytics services. Default: true (enabled)", NULL, 0},
    {"os", PARAM_ENUM, false,
        "Filter profiles by operating system. If set, only profiles matching this OS will be used. "
        "Options: 'windows', 'macos', 'linux'", (const char*[]){"windows", "macos", "linux"}, 3},
    {"gpu", PARAM_STRING, false,
        "Filter profiles by GPU vendor/model. If set, only profiles with matching GPU will be used. "
        "Examples: 'nvidia', 'amd', 'intel'", NULL, 0},
};

// browser_close_context
static const ToolParam CLOSE_CONTEXT_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context to close (e.g., 'ctx_000001'). "
        "Obtained from browser_create_context or browser_list_contexts", NULL, 0},
};

// browser_navigate
static const ToolParam NAVIGATE_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context to navigate (e.g., 'ctx_000001')", NULL, 0},
    {"url", PARAM_STRING, true,
        "The full URL to navigate to, including protocol (e.g., 'https://example.com'). "
        "Supports http://, https://, file://, and data: URLs", NULL, 0},
    {"wait_until", PARAM_ENUM, false,
        "When to consider navigation complete: '' (return immediately, default), 'load' (wait for load event), "
        "'domcontentloaded' (wait for DOMContentLoaded), 'networkidle' (wait for network to be idle)", WAIT_UNTIL_VALUES, 4},
    {"timeout", PARAM_INT, false,
        "Maximum time to wait for navigation in milliseconds. Only used when wait_until is set. "
        "Default: 30000 (30 seconds)", NULL, 0},
};

// browser_click
static const ToolParam CLICK_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"selector", PARAM_STRING, true,
        "Target element to click. Accepts: CSS selector (e.g., '#submit-btn', '.nav-link'), "
        "XY coordinates as 'Xx Y' (e.g., '100x200'), or natural language description (e.g., 'login button', 'search icon'). "
        "Semantic descriptions use AI to find the element", NULL, 0},
};

// browser_type
static const ToolParam TYPE_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"selector", PARAM_STRING, true,
        "Target input field. Accepts: CSS selector (e.g., '#email', 'input[name=\"username\"]'), "
        "XY coordinates (e.g., '100x200'), or natural language description (e.g., 'email field', 'search box')", NULL, 0},
    {"text", PARAM_STRING, true,
        "The text to type into the input field. Simulates real keystrokes with human-like timing. "
        "Existing content is NOT cleared - use browser_clear_input first if needed", NULL, 0},
};

// browser_pick
static const ToolParam PICK_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"selector", PARAM_STRING, true,
        "Target dropdown/select element. Accepts CSS selector (e.g., '#country', 'select[name=\"size\"]') "
        "or natural language description (e.g., 'country dropdown', 'size selector')", NULL, 0},
    {"value", PARAM_STRING, true,
        "The option to select. Can be the option's value attribute OR the visible display text. "
        "Works with both native <select> elements and dynamic dropdowns (select2, etc.)", NULL, 0},
};

// browser_press_key
static const ToolParam PRESS_KEY_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"key", PARAM_ENUM, true,
        "Special key to press. Common uses: 'Enter' to submit forms, 'Tab' to move focus, "
        "'Escape' to close modals, 'ArrowDown/Up' for navigation. The key is sent to the currently focused element", KEY_NAMES, 21},
};

// browser_submit_form
static const ToolParam SUBMIT_FORM_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// browser_drag_drop
static const ToolParam DRAG_DROP_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"start_x", PARAM_NUMBER, true,
        "X coordinate (in pixels from left edge) where the drag starts. Use browser_get_bounding_box to find element positions", NULL, 0},
    {"start_y", PARAM_NUMBER, true,
        "Y coordinate (in pixels from top edge) where the drag starts", NULL, 0},
    {"end_x", PARAM_NUMBER, true,
        "X coordinate (in pixels from left edge) where the drag ends (drop location)", NULL, 0},
    {"end_y", PARAM_NUMBER, true,
        "Y coordinate (in pixels from top edge) where the drag ends (drop location)", NULL, 0},
};

// browser_html5_drag_drop
static const ToolParam HTML5_DRAG_DROP_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"source_selector", PARAM_STRING, true,
        "CSS selector for the draggable source element (must have draggable='true' attribute). "
        "Example: '.drag-item', '[data-id=\"item-1\"]'", NULL, 0},
    {"target_selector", PARAM_STRING, true,
        "CSS selector for the drop target element. The element that will receive the dropped item. "
        "Example: '.drop-zone', '#target-container'", NULL, 0},
};

// browser_mouse_move
static const ToolParam MOUSE_MOVE_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"start_x", PARAM_NUMBER, true,
        "Starting X coordinate (in pixels) - typically the current mouse position", NULL, 0},
    {"start_y", PARAM_NUMBER, true,
        "Starting Y coordinate (in pixels) - typically the current mouse position", NULL, 0},
    {"end_x", PARAM_NUMBER, true,
        "Target X coordinate (in pixels) where the mouse should move to", NULL, 0},
    {"end_y", PARAM_NUMBER, true,
        "Target Y coordinate (in pixels) where the mouse should move to", NULL, 0},
    {"steps", PARAM_NUMBER, false,
        "Number of intermediate points along the path. More steps = smoother movement. "
        "Default: auto-calculated based on distance. Recommended: 0 for auto, or 10-50 for custom", NULL, 0},
};

// browser_extract_text
static const ToolParam EXTRACT_TEXT_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"selector", PARAM_STRING, false,
        "Optional CSS selector or natural language description to extract text from a specific element. "
        "If omitted, extracts all visible text from the entire page. Examples: '#main-content', 'article', 'the product description'", NULL, 0},
};

// browser_screenshot
static const char* SCREENSHOT_MODE_ENUM[] = {"viewport", "element", "fullpage"};
static const ToolParam SCREENSHOT_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"mode", PARAM_ENUM, false,
        "Screenshot mode: 'viewport' (default, current visible view), 'element' (specific element by selector), 'fullpage' (entire scrollable page)",
        SCREENSHOT_MODE_ENUM, 3},
    {"selector", PARAM_STRING, false,
        "CSS selector or natural language description for the element to capture. Required when mode is 'element'. "
        "Examples: 'div.profile', '#submit-btn', 'the login form'", NULL, 0},
};

// browser_highlight
static const ToolParam HIGHLIGHT_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"selector", PARAM_STRING, true,
        "CSS selector or natural language description of the element to highlight. "
        "Useful for debugging element selection before clicking. Examples: '#submit', '.nav-item', 'the login button'", NULL, 0},
    {"border_color", PARAM_STRING, false,
        "CSS color for the highlight border. Default: '#FF0000' (red). "
        "Examples: 'blue', '#00FF00', 'rgb(255, 128, 0)'", NULL, 0},
    {"background_color", PARAM_STRING, false,
        "CSS color for the highlight background (use alpha for transparency). Default: 'rgba(255, 0, 0, 0.2)'. "
        "Examples: 'rgba(0, 255, 0, 0.3)', 'transparent'", NULL, 0},
};

// browser_show_grid_overlay
static const ToolParam SHOW_GRID_OVERLAY_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"horizontal_lines", PARAM_NUMBER, false,
        "Number of horizontal lines to display from top to bottom. Default: 25. "
        "More lines = more precise coordinate reading but more visual clutter", NULL, 0},
    {"vertical_lines", PARAM_NUMBER, false,
        "Number of vertical lines to display from left to right. Default: 25", NULL, 0},
    {"line_color", PARAM_STRING, false,
        "CSS color for grid lines (use low alpha for visibility). Default: 'rgba(255, 0, 0, 0.15)'", NULL, 0},
    {"text_color", PARAM_STRING, false,
        "CSS color for coordinate labels at intersections. Default: 'rgba(255, 0, 0, 0.4)'", NULL, 0},
};

// browser_get_html
static const ToolParam GET_HTML_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"clean_level", PARAM_ENUM, false,
        "Level of HTML cleaning to apply. 'minimal': preserves most structure, 'basic': removes scripts/styles (default), "
        "'aggressive': strips to essential content only. Choose based on how much structure you need", CLEAN_LEVELS, 3},
};

// browser_get_markdown
static const ToolParam GET_MARKDOWN_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"include_links", PARAM_BOOL, false,
        "Include hyperlinks in markdown output as [text](url). Default: true. "
        "Set to false for cleaner text without links", NULL, 0},
    {"include_images", PARAM_BOOL, false,
        "Include images in markdown output as ![alt](src). Default: true. "
        "Set to false to exclude image references", NULL, 0},
    {"max_length", PARAM_INT, false,
        "Maximum length of the output in characters. Default: -1 (no limit). "
        "Useful for limiting output size when processing many pages", NULL, 0},
};

// browser_extract_json
static const ToolParam EXTRACT_JSON_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"template", PARAM_STRING, false,
        "Extraction template name for known site types. Available: 'google_search', 'wikipedia', 'amazon_product', "
        "'github_repo', 'twitter_feed', 'reddit_thread'. Leave empty for auto-detection based on URL", NULL, 0},
};

// browser_detect_site
static const ToolParam DETECT_SITE_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// browser_summarize_page
static const ToolParam SUMMARIZE_PAGE_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"force_refresh", PARAM_BOOL, false,
        "Force regeneration of the page summary even if cached. Summaries are cached per URL for performance. "
        "Set to true after page content has changed. Default: false (use cached summary if available)", NULL, 0},
};

// browser_query_page
static const ToolParam QUERY_PAGE_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"query", PARAM_STRING, true,
        "Natural language question to ask about the page content. Examples: 'What is the main topic?', "
        "'Does this page have pricing information?', 'What products are shown?', 'Extract all email addresses'", NULL, 0},
};

// browser_nla
static const ToolParam NLA_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"command", PARAM_STRING, true,
        "Natural language instruction for browser automation. The LLM will plan and execute multiple steps automatically. "
        "Examples: 'go to google.com and search for weather', 'click the first result', 'fill out the contact form with test data'", NULL, 0},
};

// browser_reload
static const ToolParam RELOAD_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"ignore_cache", PARAM_BOOL, false,
        "When true, performs a hard reload that bypasses the browser cache and fetches all resources from the server. "
        "Equivalent to Ctrl+Shift+R. Default: false (normal reload using cache)", NULL, 0},
    {"wait_until", PARAM_ENUM, false,
        "When to consider reload complete: '' (return immediately), 'load' (wait for load event, default), "
        "'domcontentloaded' (wait for DOMContentLoaded), 'networkidle' (wait for network to be idle)", WAIT_UNTIL_VALUES, 4},
    {"timeout", PARAM_INT, false,
        "Maximum time to wait for reload in milliseconds. Only used when wait_until is set. "
        "Default: 30000 (30 seconds)", NULL, 0},
};

// browser_go_back / browser_go_forward
static const ToolParam GO_BACK_FORWARD_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"wait_until", PARAM_ENUM, false,
        "When to consider navigation complete: '' (return immediately), 'load' (wait for load event, default), "
        "'domcontentloaded' (wait for DOMContentLoaded), 'networkidle' (wait for network to be idle)", WAIT_UNTIL_VALUES, 4},
    {"timeout", PARAM_INT, false,
        "Maximum time to wait for navigation in milliseconds. Only used when wait_until is set. "
        "Default: 30000 (30 seconds)", NULL, 0},
};

// browser_can_go_back / browser_can_go_forward
static const ToolParam CAN_GO_BACK_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// AI-powered tools
static const ToolParam AI_CLICK_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"description", PARAM_STRING, true,
        "Natural language description of the element to click (e.g., 'search button', 'the login link at the top')", NULL, 0},
};

static const ToolParam AI_TYPE_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"description", PARAM_STRING, true,
        "Natural language description of the input element (e.g., 'search box', 'email input')", NULL, 0},
    {"text", PARAM_STRING, true,
        "Text to type into the input element", NULL, 0},
};

static const ToolParam AI_EXTRACT_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"what", PARAM_STRING, true,
        "Description of what to extract (e.g., 'all product prices', 'main headline')", NULL, 0},
};

static const ToolParam AI_QUERY_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"query", PARAM_STRING, true,
        "Natural language question about the page content", NULL, 0},
};

static const ToolParam AI_ANALYZE_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

static const ToolParam FIND_ELEMENT_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"description", PARAM_STRING, true,
        "Natural language description of elements to find (e.g., 'all buttons', 'form inputs')", NULL, 0},
    {"max_results", PARAM_INT, false,
        "Maximum number of results to return (default: 10)", NULL, 0},
};

// browser_get_blocker_stats
static const ToolParam GET_BLOCKER_STATS_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// browser_get_active_downloads
static const ToolParam GET_ACTIVE_DOWNLOADS_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// browser_get_dialogs
static const ToolParam GET_DIALOGS_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// browser_enable_network_logging
static const ToolParam ENABLE_NETWORK_LOGGING_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"enable", PARAM_BOOL, true,
        "Enable (true) or disable (false) network logging for this context", NULL, 0},
};

// browser_get_live_frame
static const ToolParam GET_LIVE_FRAME_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// browser_scroll_by
static const ToolParam SCROLL_BY_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"y", PARAM_INT, true,
        "Vertical scroll amount in pixels. Positive values scroll down, negative values scroll up. "
        "Typical values: 300-500 for one viewport scroll", NULL, 0},
    {"x", PARAM_INT, false,
        "Horizontal scroll amount in pixels. Positive values scroll right, negative scroll left. "
        "Default: 0 (no horizontal scroll)", NULL, 0},
    {"verification_level", PARAM_STRING, false,
        "Verification level for scroll action. Options: 'none' (no verification), 'basic', 'standard', 'strict'. "
        "Higher levels verify scroll position after scrolling. Default: 'none'", NULL, 0},
};

// browser_scroll_to_element
static const ToolParam SCROLL_TO_ELEMENT_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"selector", PARAM_STRING, true,
        "CSS selector or natural language description of the element to scroll into view. "
        "Examples: '#footer', '.product-reviews', 'the comments section'", NULL, 0},
};

// browser_scroll_to_top / browser_scroll_to_bottom
static const ToolParam SCROLL_TO_TOP_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// browser_wait_for_selector
static const ToolParam WAIT_FOR_SELECTOR_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"selector", PARAM_STRING, true,
        "CSS selector or natural language description of the element to wait for. "
        "Waits until the element exists in the DOM and is visible. Examples: '#login-form', '.results', 'the search results'", NULL, 0},
    {"timeout", PARAM_INT, false,
        "Maximum time to wait in milliseconds. Returns error if element doesn't appear within this time. "
        "Default: 5000 (5 seconds)", NULL, 0},
};

// browser_wait
static const ToolParam WAIT_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"timeout", PARAM_INT, true,
        "Time to wait in milliseconds before continuing. Use for fixed delays when other wait methods aren't suitable. "
        "Examples: 1000 for 1 second, 3000 for 3 seconds", NULL, 0},
};

// browser_wait_for_network_idle
static const ToolParam WAIT_FOR_NETWORK_IDLE_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"idle_time", PARAM_INT, false,
        "Duration of network inactivity (no pending requests) required to consider the page 'idle'. "
        "Default: 500ms. Increase for pages with delayed/lazy loading", NULL, 0},
    {"timeout", PARAM_INT, false,
        "Maximum time to wait for network idle state. Returns error if network doesn't become idle within this time. "
        "Default: 30000 (30 seconds)", NULL, 0},
};

// browser_wait_for_function
static const ToolParam WAIT_FOR_FUNCTION_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"js_function", PARAM_STRING, true,
        "JavaScript code that returns truthy value when condition is met. Executed repeatedly until true or timeout. "
        "Examples: 'return document.querySelector(\".loaded\") !== null', 'return window.dataReady === true'", NULL, 0},
    {"polling", PARAM_INT, false,
        "Interval between function evaluations in milliseconds. Lower = more responsive but more CPU. "
        "Default: 100ms", NULL, 0},
    {"timeout", PARAM_INT, false,
        "Maximum time to wait for function to return true. Default: 30000 (30 seconds)", NULL, 0},
};

// browser_wait_for_url
static const ToolParam WAIT_FOR_URL_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"url_pattern", PARAM_STRING, true,
        "URL pattern to match. By default uses substring matching. With is_regex=true, supports glob patterns "
        "with * (any chars) and ? (single char). Examples: '/dashboard', 'example.com/success', '*/order/*'", NULL, 0},
    {"is_regex", PARAM_BOOL, false,
        "Enable glob-style pattern matching with * and ? wildcards. Default: false (simple substring match)", NULL, 0},
    {"timeout", PARAM_INT, false,
        "Maximum time to wait for URL to match. Default: 30000 (30 seconds)", NULL, 0},
};

// browser_get_page_info
static const ToolParam GET_PAGE_INFO_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// browser_set_viewport
static const ToolParam SET_VIEWPORT_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"width", PARAM_INT, true,
        "Viewport width in pixels. Common sizes: 1920 (desktop), 1366 (laptop), 768 (tablet), 375 (mobile)", NULL, 0},
    {"height", PARAM_INT, true,
        "Viewport height in pixels. Common sizes: 1080 (desktop), 768 (laptop), 1024 (tablet), 812 (mobile)", NULL, 0},
};

// browser_zoom_in, browser_zoom_out, browser_zoom_reset
static const ToolParam ZOOM_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// browser_get_console_log
static const ToolParam GET_CONSOLE_LOG_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"level", PARAM_STRING, false,
        "Filter by log level: 'debug', 'info', 'warn', 'error', 'verbose'. "
        "If not specified, returns all logs", NULL, 0},
    {"filter", PARAM_STRING, false,
        "Filter logs containing specific text (case-sensitive substring match)", NULL, 0},
    {"limit", PARAM_INT, false,
        "Maximum number of log entries to return. If not specified, returns all logs", NULL, 0},
};

// browser_clear_console_log
static const ToolParam CLEAR_CONSOLE_LOG_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// browser_start_video_recording
static const ToolParam START_VIDEO_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"fps", PARAM_INT, false,
        "Frames per second for the recording. Higher = smoother but larger file. Default: 30. "
        "Recommended: 15-30 for most use cases", NULL, 0},
    {"codec", PARAM_STRING, false,
        "Video codec for encoding. Default: 'libx264' (H.264, widely compatible). "
        "Options depend on system FFmpeg installation", NULL, 0},
};

// browser_pause/resume/stop_video_recording, browser_get_video_recording_stats
static const ToolParam VIDEO_CONTEXT_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// browser_start_live_stream
static const ToolParam START_LIVE_STREAM_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"fps", PARAM_INT, false,
        "Target frames per second for the stream. Higher values give smoother video but more bandwidth. "
        "Default: 15. Recommended range: 5-30", NULL, 0},
    {"quality", PARAM_INT, false,
        "JPEG compression quality from 1 (lowest/smallest) to 100 (highest/largest). "
        "Default: 75. Lower values reduce bandwidth but decrease image clarity", NULL, 0},
};

// browser_stop_live_stream, browser_get_live_stream_stats
static const ToolParam LIVE_STREAM_CONTEXT_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// browser_detect_captcha, browser_classify_captcha
static const ToolParam CAPTCHA_DETECT_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// browser_solve_text_captcha (no provider param)
static const ToolParam CAPTCHA_SOLVE_TEXT_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"max_attempts", PARAM_INT, false,
        "Maximum number of attempts to solve the CAPTCHA before giving up. Each attempt re-analyzes the image "
        "and tries a new solution. Default: 3", NULL, 0},
};

// browser_solve_image_captcha, browser_solve_captcha (with provider param)
static const ToolParam CAPTCHA_SOLVE_IMAGE_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"max_attempts", PARAM_INT, false,
        "Maximum number of attempts to solve the CAPTCHA. For image selection, each attempt may select "
        "different images. Default: 3", NULL, 0},
    {"provider", PARAM_ENUM, false,
        "CAPTCHA provider hint for optimized solving: 'auto' (detect automatically), 'owl' (Owl test CAPTCHAs), "
        "'recaptcha' (Google reCAPTCHA), 'cloudflare' (Turnstile), 'hcaptcha'. Default: 'auto'", CAPTCHA_PROVIDERS, 5},
};

// browser_get_cookies
static const ToolParam GET_COOKIES_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"url", PARAM_STRING, false,
        "Filter cookies to only those applicable to this URL. If not provided, returns all cookies in the context. "
        "Example: 'https://example.com' returns cookies for example.com domain", NULL, 0},
};

// browser_set_cookie
static const ToolParam SET_COOKIE_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"url", PARAM_STRING, true,
        "URL to associate with the cookie for domain validation. The cookie's domain must match or be a parent of "
        "this URL's domain. Example: 'https://example.com'", NULL, 0},
    {"name", PARAM_STRING, true,
        "The cookie name. Case-sensitive identifier for the cookie. Example: 'session_id', 'auth_token'", NULL, 0},
    {"value", PARAM_STRING, true,
        "The cookie value. Can be any string data. For security tokens, use the exact value from authentication", NULL, 0},
    {"domain", PARAM_STRING, false,
        "Cookie domain scope. If empty, creates a host-only cookie. With leading dot (e.g., '.example.com'), "
        "the cookie is visible to all subdomains", NULL, 0},
    {"path", PARAM_STRING, false,
        "URL path that must exist in the request URL for the cookie to be sent. Default: '/' (all paths). "
        "Example: '/api' restricts cookie to /api/* paths", NULL, 0},
    {"secure", PARAM_BOOL, false,
        "If true, cookie is only sent over HTTPS connections. Set to true for sensitive cookies. Default: false", NULL, 0},
    {"httpOnly", PARAM_BOOL, false,
        "If true, cookie cannot be accessed via JavaScript (document.cookie). Protects against XSS. Default: false", NULL, 0},
    {"sameSite", PARAM_ENUM, false,
        "Cross-site request restriction: 'strict' (only same-site), 'lax' (same-site + top-level navigation), "
        "'none' (allow cross-site, requires secure=true). Default: 'lax'", SAME_SITE_VALUES, 3},
    {"expires", PARAM_INT, false,
        "Unix timestamp (seconds since epoch) when the cookie expires. Use -1 or omit for session cookie "
        "(deleted when browser closes). Example: 1735689600 for a future date", NULL, 0},
};

// browser_delete_cookies
static const ToolParam DELETE_COOKIES_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"url", PARAM_STRING, false,
        "Delete only cookies applicable to this URL. If empty with empty cookie_name, deletes ALL cookies in context", NULL, 0},
    {"cookie_name", PARAM_STRING, false,
        "Delete only the cookie with this specific name. Can be combined with url parameter. "
        "If empty, deletes all cookies (or all for URL if url is provided)", NULL, 0},
};

// browser_set_proxy
static const ToolParam SET_PROXY_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"type", PARAM_ENUM, true,
        "Proxy protocol type: 'http' (HTTP proxy), 'https' (HTTPS proxy), 'socks4' (SOCKS4), "
        "'socks5' (SOCKS5 with local DNS), 'socks5h' (SOCKS5 with remote DNS - most private)", PROXY_TYPES, 5},
    {"host", PARAM_STRING, true,
        "Proxy server hostname or IP address. Examples: '127.0.0.1', 'proxy.example.com', 'localhost'", NULL, 0},
    {"port", PARAM_INT, true,
        "Proxy server port number. Common ports: 8080 (HTTP), 3128 (Squid), 9050/9150 (Tor), 1080 (SOCKS)", NULL, 0},
    {"username", PARAM_STRING, false,
        "Username for proxy authentication. Only required if the proxy server requires credentials", NULL, 0},
    {"password", PARAM_STRING, false,
        "Password for proxy authentication. Only required if the proxy server requires credentials", NULL, 0},
    {"stealth", PARAM_BOOL, false,
        "Enable stealth mode to prevent proxy/VPN detection. Blocks WebRTC leaks, DNS leaks, and other "
        "fingerprinting vectors. Default: true", NULL, 0},
    {"block_webrtc", PARAM_BOOL, false,
        "Block WebRTC to prevent real IP address leaking through STUN/TURN requests. Essential for privacy. "
        "Default: true (enabled when stealth mode is on)", NULL, 0},
    {"spoof_timezone", PARAM_BOOL, false,
        "Automatically spoof browser timezone to match proxy location (detected via IP geolocation). Default: false", NULL, 0},
    {"spoof_language", PARAM_BOOL, false,
        "Automatically spoof browser language preferences to match proxy location. Default: false", NULL, 0},
    {"timezone_override", PARAM_STRING, false,
        "Manually set timezone instead of auto-detection. IANA format like 'America/New_York', 'Europe/London'. "
        "Takes precedence over spoof_timezone", NULL, 0},
    {"language_override", PARAM_STRING, false,
        "Manually set browser language instead of auto-detection. BCP47 format like 'en-US', 'de-DE'. "
        "Takes precedence over spoof_language", NULL, 0},
};

// browser_get_proxy_status, browser_connect_proxy, browser_disconnect_proxy
static const ToolParam PROXY_CONTEXT_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// browser_create_profile
static const ToolParam CREATE_PROFILE_PARAMS[] = {
    {"name", PARAM_STRING, false,
        "Human-readable name for the profile to help identify it. Examples: 'Shopping Account', 'Test User 1'. "
        "Optional but recommended for managing multiple profiles", NULL, 0},
};

// browser_load_profile
static const ToolParam LOAD_PROFILE_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context to load the profile into (e.g., 'ctx_000001')", NULL, 0},
    {"profile_path", PARAM_STRING, true,
        "Absolute path to the profile JSON file to load. The file must exist and contain valid profile data. "
        "Example: '/home/user/profiles/shopping.json'", NULL, 0},
};

// browser_save_profile
static const ToolParam SAVE_PROFILE_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context to save (e.g., 'ctx_000001')", NULL, 0},
    {"profile_path", PARAM_STRING, false,
        "Absolute path where the profile JSON file will be saved. If the context was loaded from a profile, "
        "this can be omitted to save to the original path. Example: '/home/user/profiles/new_profile.json'", NULL, 0},
};

// browser_get_profile, browser_update_profile_cookies
static const ToolParam PROFILE_CONTEXT_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// ============================================================================
// Network Interception Parameters
// ============================================================================

// browser_add_network_rule
static const ToolParam ADD_NETWORK_RULE_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"url_pattern", PARAM_STRING, true,
        "URL pattern to match requests against. Supports glob patterns (e.g., '*://api.example.com/*', '*.js') "
        "or regex if is_regex=true. Glob uses * for any characters, ? for single character", NULL, 0},
    {"action", PARAM_ENUM, true,
        "What to do with matching requests: 'allow' lets them through (useful with blocking enabled), "
        "'block' drops the request, 'mock' returns a fake response, 'redirect' sends to different URL", NETWORK_ACTIONS, 4},
    {"is_regex", PARAM_BOOL, false,
        "Treat url_pattern as a regular expression instead of glob pattern. Default: false (uses glob). "
        "Example regex: '^https://api\\\\.example\\\\.com/v[0-9]+/'", NULL, 0},
    {"redirect_url", PARAM_STRING, false,
        "The URL to redirect matching requests to. Only used when action='redirect'. "
        "Example: redirect tracking scripts to empty response", NULL, 0},
    {"mock_body", PARAM_STRING, false,
        "Response body to return for mocked requests. Only used when action='mock'. "
        "Can be JSON, HTML, or any text content. Example: '{\"success\": true}'", NULL, 0},
    {"mock_status", PARAM_INT, false,
        "HTTP status code for mocked responses (e.g., 200, 404, 500). Only used when action='mock'. "
        "Default: 200", NULL, 0},
    {"mock_content_type", PARAM_STRING, false,
        "Content-Type header for mocked responses. Only used when action='mock'. "
        "Examples: 'application/json', 'text/html', 'text/plain'. Default: 'text/plain'", NULL, 0},
};

// browser_remove_network_rule
static const ToolParam REMOVE_NETWORK_RULE_PARAMS[] = {
    {"rule_id", PARAM_STRING, true,
        "The unique identifier of the rule to remove, returned by browser_add_network_rule when the rule was created", NULL, 0},
};

// browser_enable_network_interception
static const ToolParam ENABLE_NETWORK_INTERCEPTION_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"enable", PARAM_BOOL, true,
        "Set to true to enable network interception (rules start applying), false to disable. "
        "When disabled, all requests pass through normally regardless of rules", NULL, 0},
};

// browser_get_network_log, browser_clear_network_log
static const ToolParam NETWORK_LOG_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// ============================================================================
// File Download Parameters
// ============================================================================

// browser_set_download_path
static const ToolParam SET_DOWNLOAD_PATH_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"path", PARAM_STRING, true,
        "Absolute path to the directory where downloaded files will be saved. "
        "The directory must exist and be writable. Example: '/tmp/downloads' or '/home/user/Downloads'", NULL, 0},
};

// browser_get_downloads
static const ToolParam GET_DOWNLOADS_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// browser_wait_for_download
static const ToolParam WAIT_FOR_DOWNLOAD_PARAMS[] = {
    {"download_id", PARAM_STRING, true,
        "The unique identifier of the download to wait for, obtained from browser_get_downloads or triggered download events", NULL, 0},
    {"timeout", PARAM_INT, false,
        "Maximum time to wait for download completion in milliseconds. Returns error if exceeded. "
        "Default: 30000 (30 seconds). Set higher for large files", NULL, 0},
};

// browser_cancel_download
static const ToolParam CANCEL_DOWNLOAD_PARAMS[] = {
    {"download_id", PARAM_STRING, true,
        "The unique identifier of the in-progress download to cancel. Cancellation stops the download "
        "and may delete partially downloaded content", NULL, 0},
};

// ============================================================================
// Dialog Handling Parameters
// ============================================================================

// browser_set_dialog_action
static const ToolParam SET_DIALOG_ACTION_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"dialog_type", PARAM_ENUM, true,
        "Type of JavaScript dialog to configure: 'alert' (message display), 'confirm' (yes/no), "
        "'prompt' (text input), 'beforeunload' (page leave warning)", DIALOG_TYPES, 4},
    {"action", PARAM_ENUM, true,
        "How to automatically handle this dialog type: 'accept' clicks OK/Yes, 'dismiss' clicks Cancel/No, "
        "'accept_with_text' accepts and provides text input (for prompts)", DIALOG_ACTIONS, 3},
    {"prompt_text", PARAM_STRING, false,
        "Default text to enter when accepting prompt dialogs. Only used when dialog_type='prompt' and "
        "action='accept_with_text'. Leave empty for blank input", NULL, 0},
};

// browser_get_pending_dialog
static const ToolParam GET_PENDING_DIALOG_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// browser_handle_dialog
static const ToolParam HANDLE_DIALOG_PARAMS[] = {
    {"dialog_id", PARAM_STRING, true,
        "The unique identifier of the dialog to handle, obtained from browser_get_pending_dialog or browser_wait_for_dialog", NULL, 0},
    {"accept", PARAM_BOOL, true,
        "Set to true to accept (click OK/Yes) or false to dismiss (click Cancel/No) the dialog", NULL, 0},
    {"response_text", PARAM_STRING, false,
        "Text to enter in prompt dialogs before accepting. Only applicable for prompt dialogs "
        "when accept=true. Ignored for alert and confirm dialogs", NULL, 0},
};

// browser_wait_for_dialog
static const ToolParam WAIT_FOR_DIALOG_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"timeout", PARAM_INT, false,
        "Maximum time to wait for a dialog to appear in milliseconds. Returns error if no dialog appears. "
        "Default: 5000 (5 seconds)", NULL, 0},
};

// ============================================================================
// Tab/Window Management Parameters
// ============================================================================

// browser_set_popup_policy
static const ToolParam SET_POPUP_POLICY_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"policy", PARAM_ENUM, true,
        "How to handle popup windows opened by window.open() or target='_blank' links: "
        "'allow' opens in new window, 'block' prevents popup, 'new_tab' opens in foreground tab, "
        "'background' opens in background tab", POPUP_POLICIES, 4},
};

// browser_get_tabs
static const ToolParam GET_TABS_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// browser_switch_tab
static const ToolParam SWITCH_TAB_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"tab_id", PARAM_STRING, true,
        "The unique identifier of the tab to activate/switch to, obtained from browser_get_tabs", NULL, 0},
};

// browser_close_tab
static const ToolParam CLOSE_TAB_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"tab_id", PARAM_STRING, true,
        "The unique identifier of the tab to close, obtained from browser_get_tabs. "
        "Cannot close the last remaining tab in a context", NULL, 0},
};

// browser_new_tab
static const ToolParam NEW_TAB_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"url", PARAM_STRING, false,
        "URL to navigate to in the new tab. If not provided, opens a blank tab (about:blank). "
        "Include full URL with protocol (e.g., 'https://example.com')", NULL, 0},
};

// browser_get_active_tab, browser_get_tab_count, browser_get_blocked_popups
static const ToolParam TAB_CONTEXT_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// ============================================================================
// License Management Parameters
// ============================================================================

// browser_get_license_status, browser_get_license_info - no params
static const ToolParam LICENSE_STATUS_PARAMS[] = {
    {NULL, PARAM_STRING, false, NULL, NULL, 0},  // Empty params - no parameters required
};

// browser_get_hardware_fingerprint - no params
static const ToolParam FINGERPRINT_PARAMS[] = {
    {NULL, PARAM_STRING, false, NULL, NULL, 0},  // Empty params - no parameters required
};

// browser_add_license
static const ToolParam ADD_LICENSE_PARAMS[] = {
    {"license_path", PARAM_STRING, false,
        "Absolute path to the license file (.olic extension). Provide either this OR license_content, not both. "
        "Example: '/path/to/license.olic'", NULL, 0},
    {"license_content", PARAM_STRING, false,
        "Base64-encoded license file content. Alternative to license_path for passing license data directly "
        "without writing to disk. Provide either this OR license_path, not both", NULL, 0},
};

// browser_remove_license - no params
static const ToolParam REMOVE_LICENSE_PARAMS[] = {
    {NULL, PARAM_STRING, false, NULL, NULL, 0},  // Empty params - no parameters required
};

// ============================================================================
// Element Picker Parameters (for UI overlays)
// ============================================================================

// browser_get_element_at_position
static const ToolParam GET_ELEMENT_AT_POSITION_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"x", PARAM_NUMBER, true,
        "X coordinate in pixels from the left edge of the viewport. Use browser_show_grid_overlay "
        "to visualize coordinates on the page", NULL, 0},
    {"y", PARAM_NUMBER, true,
        "Y coordinate in pixels from the top edge of the viewport. Use browser_show_grid_overlay "
        "to visualize coordinates on the page", NULL, 0},
};

// browser_get_interactive_elements
static const ToolParam GET_INTERACTIVE_ELEMENTS_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// ============================================================================
// Additional Interaction Parameters
// ============================================================================

// browser_hover
static const ToolParam HOVER_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"selector", PARAM_STRING, true,
        "CSS selector, position coordinates (e.g., '100x200'), or natural language description of the element to hover over", NULL, 0},
};

// browser_double_click
static const ToolParam DOUBLE_CLICK_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"selector", PARAM_STRING, true,
        "CSS selector, position coordinates (e.g., '100x200'), or natural language description of the element to double-click", NULL, 0},
};

// browser_right_click
static const ToolParam RIGHT_CLICK_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"selector", PARAM_STRING, true,
        "CSS selector, position coordinates (e.g., '100x200'), or natural language description of the element to right-click", NULL, 0},
};

// browser_clear_input
static const ToolParam CLEAR_INPUT_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"selector", PARAM_STRING, true,
        "CSS selector or natural language description of the input element to clear", NULL, 0},
};

// browser_focus
static const ToolParam FOCUS_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"selector", PARAM_STRING, true,
        "CSS selector or natural language description of the element to focus", NULL, 0},
};

// browser_blur
static const ToolParam BLUR_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"selector", PARAM_STRING, true,
        "CSS selector or natural language description of the element to remove focus from", NULL, 0},
};

// browser_select_all
static const ToolParam SELECT_ALL_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"selector", PARAM_STRING, true,
        "CSS selector or natural language description of the input element to select all text in", NULL, 0},
};

// browser_keyboard_combo
static const ToolParam KEYBOARD_COMBO_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"combo", PARAM_STRING, true,
        "Key combination string using modifiers and keys. Supports Ctrl, Shift, Alt, Meta/Cmd. "
        "Examples: 'Ctrl+A', 'Ctrl+Shift+N', 'Meta+V', 'Shift+Enter'", NULL, 0},
};

// browser_upload_file
static const ToolParam UPLOAD_FILE_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"selector", PARAM_STRING, true,
        "CSS selector for the file input element (e.g., 'input[type=\"file\"]', '#file-upload')", NULL, 0},
    {"file_paths", PARAM_STRING, true,
        "Array of absolute file paths to upload, as JSON array string (e.g., '[\"/path/to/file1.pdf\", \"/path/to/file2.jpg\"]')", NULL, 0},
};

// ============================================================================
// Element State Check Parameters
// ============================================================================

// browser_is_visible
static const ToolParam IS_VISIBLE_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"selector", PARAM_STRING, true,
        "CSS selector or natural language description of the element to check visibility for", NULL, 0},
};

// browser_is_enabled
static const ToolParam IS_ENABLED_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"selector", PARAM_STRING, true,
        "CSS selector or natural language description of the element to check enabled state for", NULL, 0},
};

// browser_is_checked
static const ToolParam IS_CHECKED_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"selector", PARAM_STRING, true,
        "CSS selector or natural language description of the checkbox or radio button to check", NULL, 0},
};

// browser_get_attribute
static const ToolParam GET_ATTRIBUTE_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"selector", PARAM_STRING, true,
        "CSS selector or natural language description of the element", NULL, 0},
    {"attribute", PARAM_STRING, true,
        "The attribute name to retrieve (e.g., 'href', 'src', 'value', 'data-id', 'class')", NULL, 0},
};

// browser_get_bounding_box
static const ToolParam GET_BOUNDING_BOX_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"selector", PARAM_STRING, true,
        "CSS selector or natural language description of the element to get position and size for", NULL, 0},
};

// ============================================================================
// JavaScript Evaluation Parameters
// ============================================================================

// browser_evaluate
static const ToolParam EVALUATE_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"script", PARAM_STRING, false,
        "JavaScript code to execute in the page context. Can access DOM, window object, etc. "
        "Example: 'document.title' or 'window.scrollY'. Optional if expression is provided.", NULL, 0},
    {"expression", PARAM_STRING, false,
        "JavaScript expression to evaluate and return (shorthand for script with return_value=true). "
        "If provided, script is optional and return_value is automatically true.", NULL, 0},
    {"return_value", PARAM_BOOL, false,
        "If true, treats the script as an expression and returns its value. "
        "If false (default), executes the script as statements. Automatically set to true when using expression.", NULL, 0},
};

// ============================================================================
// Clipboard Management Parameters
// ============================================================================

// browser_clipboard_read
static const ToolParam CLIPBOARD_READ_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// browser_clipboard_write
static const ToolParam CLIPBOARD_WRITE_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"text", PARAM_STRING, true,
        "Text content to write to the system clipboard", NULL, 0},
};

// browser_clipboard_clear
static const ToolParam CLIPBOARD_CLEAR_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// ============================================================================
// Frame Handling Parameters
// ============================================================================

// browser_list_frames
static const ToolParam LIST_FRAMES_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// browser_switch_to_frame
static const ToolParam SWITCH_TO_FRAME_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
    {"frame_selector", PARAM_STRING, true,
        "Frame identifier: name attribute, frame index as string (e.g., '0', '1'), or CSS selector for iframe element", NULL, 0},
};

// browser_switch_to_main_frame
static const ToolParam SWITCH_TO_MAIN_FRAME_PARAMS[] = {
    {"context_id", PARAM_STRING, true,
        "The unique identifier of the browser context (e.g., 'ctx_000001')", NULL, 0},
};

// ipc_tests_run
static const ToolParam IPC_TESTS_RUN_PARAMS[] = {
    {"mode", PARAM_ENUM, false,
        "Test mode to run. Options: 'smoke' (quick validation, 5 tests), 'full' (comprehensive, 138+ tests), "
        "'benchmark' (performance testing), 'stress' (load testing), 'leak-check' (memory leak detection), "
        "'parallel' (concurrent context testing). Default: 'full'", IPC_TEST_MODES, 6},
    {"verbose", PARAM_BOOL, false,
        "Enable verbose output during test execution. Default: false", NULL, 0},
    {"iterations", PARAM_INT, false,
        "Number of iterations for benchmark mode. Default: 1", NULL, 0},
    {"contexts", PARAM_INT, false,
        "Number of browser contexts for stress mode. Default: 10", NULL, 0},
    {"duration", PARAM_INT, false,
        "Duration in seconds for stress and leak-check modes. Default: 60", NULL, 0},
    {"concurrency", PARAM_INT, false,
        "Number of concurrent threads for parallel mode. Default: 1", NULL, 0},
};

// ipc_tests_status
static const ToolParam IPC_TESTS_STATUS_PARAMS[] = {
    {"run_id", PARAM_STRING, false,
        "The unique run ID to query. If not provided, returns status of current/last run.", NULL, 0},
};

// ipc_tests_abort
static const ToolParam IPC_TESTS_ABORT_PARAMS[] = {
    {"run_id", PARAM_STRING, true,
        "The unique run ID of the test to abort.", NULL, 0},
};

// ipc_tests_get_report
static const ToolParam IPC_TESTS_GET_REPORT_PARAMS[] = {
    {"run_id", PARAM_STRING, true,
        "The unique run ID of the report to retrieve.", NULL, 0},
    {"format", PARAM_ENUM, false,
        "Report format to return. Options: 'json' (raw test data), 'html' (interactive dashboard). Default: 'json'",
        (const char*[]){"json", "html", NULL}, 2},
};

// ipc_tests_delete_report
static const ToolParam IPC_TESTS_DELETE_REPORT_PARAMS[] = {
    {"run_id", PARAM_STRING, true,
        "The unique run ID of the report to delete.", NULL, 0},
};

// ============================================================================
// Tool Definitions
// ============================================================================

#define TOOL_DEF(name, desc, params) {name, desc, params, sizeof(params)/sizeof(params[0])}
#define TOOL_DEF_NO_PARAMS(name, desc) {name, desc, NULL, 0}

static const ToolDef g_tools[] = {
    // Context Management
    TOOL_DEF(TOOL_CREATE_CONTEXT,
        "Create a new isolated browser context with its own cookies, storage, and optional proxy configuration. "
        "Each context acts as an independent browser session. Use this to create multiple isolated browsing sessions, "
        "configure proxy/Tor connections, load browser profiles with saved fingerprints, and enable/disable LLM features. "
        "Returns a context_id to use with other browser tools.",
        CREATE_CONTEXT_PARAMS),
    TOOL_DEF(TOOL_CLOSE_CONTEXT,
        "Close a browser context and release all associated resources including cookies, storage, and network connections. "
        "Always close contexts when done to free up memory. Any ongoing operations in the context will be terminated.",
        CLOSE_CONTEXT_PARAMS),
    TOOL_DEF_NO_PARAMS(TOOL_LIST_CONTEXTS,
        "List all currently active browser contexts with their IDs, creation time, current URL, and state. "
        "Useful for managing multiple browser sessions and checking which contexts are still open."),

    // Navigation
    TOOL_DEF(TOOL_NAVIGATE,
        "Navigate the browser to a specified URL. This is a non-blocking operation that starts navigation "
        "and returns immediately. Use browser_wait_for_network_idle or browser_wait_for_selector to wait for "
        "the page to fully load. Supports HTTP, HTTPS, file, and data URLs.",
        NAVIGATE_PARAMS),
    TOOL_DEF(TOOL_RELOAD,
        "Reload the current page. Optionally bypass the browser cache for a hard reload that fetches all "
        "resources fresh from the server. Useful when testing changes or clearing stale cached content.",
        RELOAD_PARAMS),
    TOOL_DEF(TOOL_GO_BACK,
        "Navigate back to the previous page in the browser's history stack. Equivalent to clicking the browser's "
        "back button. Does nothing if there is no previous page in history.",
        GO_BACK_FORWARD_PARAMS),
    TOOL_DEF(TOOL_GO_FORWARD,
        "Navigate forward to the next page in the browser's history stack. Equivalent to clicking the browser's "
        "forward button. Only works after using browser_go_back. Does nothing if there is no forward history.",
        GO_BACK_FORWARD_PARAMS),
    TOOL_DEF(TOOL_CAN_GO_BACK,
        "Check if navigation back is possible. Returns true if there is a previous page in the browser history, "
        "false otherwise. Use before calling browser_go_back to avoid no-op calls.",
        CAN_GO_BACK_PARAMS),
    TOOL_DEF(TOOL_CAN_GO_FORWARD,
        "Check if navigation forward is possible. Returns true if there is a next page in the browser history "
        "(after going back), false otherwise. Use before calling browser_go_forward.",
        CAN_GO_BACK_PARAMS),

    // Interaction
    TOOL_DEF(TOOL_CLICK,
        "Click on an element using CSS selector, XY coordinates, or natural language description. "
        "Supports semantic element finding using AI - describe what you want to click (e.g., 'login button', 'search icon') "
        "and the system will locate the right element. Simulates a real mouse click with proper event dispatch.",
        CLICK_PARAMS),
    TOOL_DEF(TOOL_TYPE,
        "Type text into an input field with human-like keystroke simulation. Target the field using CSS selector, "
        "coordinates, or natural language (e.g., 'email field'). Note: Does NOT clear existing content - "
        "use browser_clear_input first if you need to replace text rather than append.",
        TYPE_PARAMS),
    TOOL_DEF(TOOL_PICK,
        "Select an option from a dropdown or select element. Works with native HTML <select> elements and "
        "dynamic/custom dropdowns (like select2, react-select). Specify the option by its value or visible text. "
        "Automatically handles opening the dropdown and selecting the option.",
        PICK_PARAMS),
    TOOL_DEF(TOOL_PRESS_KEY,
        "Press a special keyboard key like Enter, Tab, Escape, or arrow keys. The key is sent to the currently "
        "focused element. Common uses: Enter to submit forms, Tab to move between fields, Escape to close modals, "
        "Arrow keys for navigation in menus or sliders.",
        PRESS_KEY_PARAMS),
    TOOL_DEF(TOOL_SUBMIT_FORM,
        "Submit the currently focused form by simulating an Enter key press. Equivalent to pressing Enter in a form field. "
        "Useful for search boxes and forms that submit on Enter. The form must have a focused input element.",
        SUBMIT_FORM_PARAMS),
    TOOL_DEF(TOOL_DRAG_DROP,
        "Perform a mouse drag operation from start coordinates to end coordinates. Used for slider CAPTCHAs, "
        "puzzle solving, canvas drawing, and custom drag interactions. Uses realistic mouse movement with "
        "bezier curves. For HTML5 draggable elements, use browser_html5_drag_drop instead.",
        DRAG_DROP_PARAMS),
    TOOL_DEF(TOOL_HTML5_DRAG_DROP,
        "Perform HTML5 drag and drop using proper DragEvent dispatch. Use this for elements with draggable='true' "
        "attribute, sortable lists, and interfaces using the HTML5 Drag and Drop API. Dispatches dragstart, "
        "dragover, drop, and dragend events in the correct sequence.",
        HTML5_DRAG_DROP_PARAMS),
    TOOL_DEF(TOOL_MOUSE_MOVE,
        "Move the mouse cursor along a natural curved path from start to end position. Uses bezier curves with "
        "random variation, micro-jitter, and easing for human-like movement. Essential for avoiding bot detection "
        "on sites that track mouse movement patterns.",
        MOUSE_MOVE_PARAMS),
    TOOL_DEF(TOOL_HOVER,
        "Hover over an element without clicking. Triggers hover effects like tooltips, dropdown menus, and "
        "CSS :hover styles. Useful for revealing hidden content or previewing actions before clicking.",
        HOVER_PARAMS),
    TOOL_DEF(TOOL_DOUBLE_CLICK,
        "Double-click an element. Common uses include selecting text, opening items in file managers, "
        "and triggering edit mode in editable interfaces.",
        DOUBLE_CLICK_PARAMS),
    TOOL_DEF(TOOL_RIGHT_CLICK,
        "Right-click (context click) on an element to open the context menu. Useful for accessing "
        "secondary actions like 'Open in new tab', 'Copy link', or custom application menus.",
        RIGHT_CLICK_PARAMS),
    TOOL_DEF(TOOL_CLEAR_INPUT,
        "Clear all text content from an input field. Use this before browser_type to replace text "
        "rather than append to existing content. Selects all text and deletes it.",
        CLEAR_INPUT_PARAMS),
    TOOL_DEF(TOOL_FOCUS,
        "Set focus to an element without clicking it. The element receives keyboard events and becomes "
        "the active element. Useful for preparing to type or when click would trigger unwanted actions.",
        FOCUS_PARAMS),
    TOOL_DEF(TOOL_BLUR,
        "Remove focus from an element. Often triggers validation on form fields. "
        "Useful for forcing validation or removing focus from the current element.",
        BLUR_PARAMS),
    TOOL_DEF(TOOL_SELECT_ALL,
        "Select all text content within an input element. Equivalent to Ctrl+A within the field. "
        "Useful before typing to replace content or for copying the entire field value.",
        SELECT_ALL_PARAMS),
    TOOL_DEF(TOOL_KEYBOARD_COMBO,
        "Press a keyboard combination with modifiers. Supports Ctrl, Shift, Alt, Meta/Cmd modifiers. "
        "Examples: 'Ctrl+A' (select all), 'Ctrl+C' (copy), 'Ctrl+V' (paste), 'Ctrl+Shift+N' (new window).",
        KEYBOARD_COMBO_PARAMS),
    TOOL_DEF(TOOL_UPLOAD_FILE,
        "Upload files to a file input element. Programmatically sets the files on an input[type='file'] element. "
        "Supports multiple files if the input accepts them. File paths must be absolute paths on the server.",
        UPLOAD_FILE_PARAMS),

    // Element State Checks
    TOOL_DEF(TOOL_IS_VISIBLE,
        "Check if an element is visible on the page. Returns true if the element exists, is displayed (not hidden), "
        "and has non-zero dimensions. Useful for conditional logic based on visibility.",
        IS_VISIBLE_PARAMS),
    TOOL_DEF(TOOL_IS_ENABLED,
        "Check if an element is enabled (not disabled). Returns true if the element doesn't have the 'disabled' attribute. "
        "Useful for checking if buttons, inputs, or other controls can be interacted with.",
        IS_ENABLED_PARAMS),
    TOOL_DEF(TOOL_IS_CHECKED,
        "Check if a checkbox or radio button is currently checked/selected. Returns true if the element "
        "has the 'checked' property set. Useful for verifying form state.",
        IS_CHECKED_PARAMS),
    TOOL_DEF(TOOL_GET_ATTRIBUTE,
        "Get the value of an HTML attribute from an element. Can retrieve any attribute including 'href', 'src', "
        "'value', 'data-*' attributes, 'class', 'id', etc. Returns null if attribute doesn't exist.",
        GET_ATTRIBUTE_PARAMS),
    TOOL_DEF(TOOL_GET_BOUNDING_BOX,
        "Get the position and size of an element. Returns {x, y, width, height} representing the element's "
        "bounding rectangle in viewport coordinates. Useful for positioning or calculating drag coordinates.",
        GET_BOUNDING_BOX_PARAMS),

    // JavaScript Evaluation
    TOOL_DEF(TOOL_EVALUATE,
        "Execute arbitrary JavaScript code in the page context. Has full access to the DOM, window object, "
        "and all page JavaScript. Use return_value=true to get the result of an expression.",
        EVALUATE_PARAMS),

    // Clipboard Management
    TOOL_DEF(TOOL_CLIPBOARD_READ,
        "Read text content from the system clipboard. Returns the current clipboard text content. "
        "Useful for getting content that was copied from the browser.",
        CLIPBOARD_READ_PARAMS),
    TOOL_DEF(TOOL_CLIPBOARD_WRITE,
        "Write text content to the system clipboard. Replaces any existing clipboard content. "
        "Useful for preparing content to paste into the browser.",
        CLIPBOARD_WRITE_PARAMS),
    TOOL_DEF(TOOL_CLIPBOARD_CLEAR,
        "Clear the system clipboard content. Removes all text and data from the clipboard.",
        CLIPBOARD_CLEAR_PARAMS),

    // Frame Handling
    TOOL_DEF(TOOL_LIST_FRAMES,
        "List all frames (main frame and iframes) on the current page. Returns frame names, URLs, and indices. "
        "Use to discover iframes before switching context to interact with their content.",
        LIST_FRAMES_PARAMS),
    TOOL_DEF(TOOL_SWITCH_TO_FRAME,
        "Switch the browser context to interact with an iframe. After switching, all subsequent operations "
        "(click, type, etc.) target elements within that frame. Use frame name, index, or CSS selector.",
        SWITCH_TO_FRAME_PARAMS),
    TOOL_DEF(TOOL_SWITCH_TO_MAIN_FRAME,
        "Switch back to the main (top-level) frame after interacting with an iframe. Call this to return to "
        "the main document after browser_switch_to_frame operations.",
        SWITCH_TO_MAIN_FRAME_PARAMS),

    // Content Extraction
    TOOL_DEF(TOOL_EXTRACT_TEXT,
        "Extract visible text content from the page or a specific element. Returns plain text stripped of HTML tags. "
        "Optionally target a specific element using CSS selector or natural language description. "
        "Useful for reading page content, extracting article text, or getting form values.",
        EXTRACT_TEXT_PARAMS),
    TOOL_DEF(TOOL_SCREENSHOT,
        "Capture a PNG screenshot with configurable modes. 'viewport' (default) captures the current visible area, "
        "'element' captures a specific element by CSS selector or natural language description, "
        "'fullpage' captures the entire scrollable page. Returns base64-encoded image data. "
        "Screenshots capture exactly as rendered, including all dynamic content, images, and styling. "
        "Useful for visual verification, debugging, and AI vision analysis.",
        SCREENSHOT_PARAMS),
    TOOL_DEF(TOOL_HIGHLIGHT,
        "Visually highlight an element on the page with a colored border and background overlay. "
        "Useful for debugging element selection - verify which element will be clicked before performing actions. "
        "The highlight persists until the page is navigated or refreshed.",
        HIGHLIGHT_PARAMS),
    TOOL_DEF(TOOL_SHOW_GRID_OVERLAY,
        "Display an XY coordinate grid overlay on the page with position labels at intersections. "
        "Essential for finding exact pixel coordinates when using browser_click with coordinates or browser_drag_drop. "
        "The grid helps identify precise positions for mouse operations.",
        SHOW_GRID_OVERLAY_PARAMS),
    TOOL_DEF(TOOL_GET_HTML,
        "Extract the page's HTML with configurable cleaning levels. 'minimal' preserves most structure, "
        "'basic' removes scripts and styles, 'aggressive' strips to essential content. "
        "Useful for page analysis, content extraction, and feeding HTML to other processing tools.",
        GET_HTML_PARAMS),
    TOOL_DEF(TOOL_GET_MARKDOWN,
        "Convert the page content to clean Markdown format. Preserves headings, links, images, lists, and basic formatting. "
        "Much more readable than HTML for text analysis. Optionally control inclusion of links and images, "
        "and limit output length for large pages.",
        GET_MARKDOWN_PARAMS),
    TOOL_DEF(TOOL_EXTRACT_JSON,
        "Extract structured data from the page using predefined templates or auto-detection. "
        "Templates available for Google Search results, Wikipedia articles, Amazon products, GitHub repos, and more. "
        "Returns consistently structured JSON regardless of page layout changes.",
        EXTRACT_JSON_PARAMS),
    TOOL_DEF(TOOL_DETECT_SITE,
        "Identify the type of website currently loaded (e.g., 'google_search', 'wikipedia', 'amazon_product'). "
        "Use before browser_extract_json to determine which template to apply, or for conditional page handling logic.",
        DETECT_SITE_PARAMS),
    TOOL_DEF_NO_PARAMS(TOOL_LIST_TEMPLATES,
        "List all available JSON extraction templates with their names and descriptions. "
        "Use to discover which structured data extraction templates are available for browser_extract_json."),

    // AI/LLM Features
    TOOL_DEF(TOOL_SUMMARIZE_PAGE,
        "Generate an intelligent, structured summary of the current page using the LLM. Returns key information "
        "about the page topic, main content, and interactive elements. Summaries are cached per URL for performance. "
        "Much better than raw text extraction for understanding page content.",
        SUMMARIZE_PAGE_PARAMS),
    TOOL_DEF(TOOL_QUERY_PAGE,
        "Ask a natural language question about the current page content. The LLM analyzes the page and answers "
        "questions like 'What is the main topic?', 'Are there any prices listed?', 'Extract all email addresses'. "
        "Uses intelligent page summarization for better context understanding.",
        QUERY_PAGE_PARAMS),
    TOOL_DEF_NO_PARAMS(TOOL_LLM_STATUS,
        "Check if the on-device LLM is ready to use. Returns status: 'ready', 'loading', or 'unavailable'. "
        "Call this before using LLM-powered features to ensure the model is loaded."),
    TOOL_DEF(TOOL_NLA,
        "Execute complex browser automation using natural language commands. The LLM interprets your instruction "
        "and automatically plans and executes multiple browser actions. Examples: 'search for shoes on Amazon', "
        "'log in with test credentials', 'add the first item to cart'.",
        NLA_PARAMS),
    TOOL_DEF(TOOL_AI_CLICK,
        "Click an element described in natural language using AI vision. The AI analyzes the page screenshot "
        "to find and click the element matching your description. Example: 'the blue submit button'.",
        AI_CLICK_PARAMS),
    TOOL_DEF(TOOL_AI_TYPE,
        "Type text into an element described in natural language using AI vision. The AI finds the input field "
        "matching your description and enters the text. Example: 'email field', 'search box'.",
        AI_TYPE_PARAMS),
    TOOL_DEF(TOOL_AI_EXTRACT,
        "Extract specific information from the page using AI. Describe what you want to extract and the AI "
        "will analyze the page content and return the relevant data. Example: 'all product prices'.",
        AI_EXTRACT_PARAMS),
    TOOL_DEF(TOOL_AI_QUERY,
        "Ask a natural language question about the current page. The AI analyzes the page and provides "
        "an intelligent answer. Example: 'Is there a login form on this page?'.",
        AI_QUERY_PARAMS),
    TOOL_DEF(TOOL_AI_ANALYZE,
        "Perform comprehensive AI analysis of the current page. Returns structured information about "
        "the page's topic, main content, structure, and interactive elements.",
        AI_ANALYZE_PARAMS),
    TOOL_DEF(TOOL_FIND_ELEMENT,
        "Find an element on the page using natural language description. Returns the CSS selector, confidence "
        "score, and element details. Useful for locating elements before interaction.",
        FIND_ELEMENT_PARAMS),

    // Scroll Control
    TOOL_DEF(TOOL_SCROLL_BY,
        "Scroll the page by a specified number of pixels vertically and/or horizontally. Positive Y scrolls down, "
        "negative scrolls up. Use for incremental scrolling to load lazy content or navigate long pages.",
        SCROLL_BY_PARAMS),
    TOOL_DEF(TOOL_SCROLL_TO_ELEMENT,
        "Scroll the page to bring a specific element into view. Uses smooth scrolling to center the element "
        "in the viewport. Target elements using CSS selector or natural language description.",
        SCROLL_TO_ELEMENT_PARAMS),
    TOOL_DEF(TOOL_SCROLL_TO_TOP,
        "Instantly scroll to the very top of the page. Equivalent to pressing Home key or clicking in the "
        "scrollbar track at the top.",
        SCROLL_TO_TOP_PARAMS),
    TOOL_DEF(TOOL_SCROLL_TO_BOTTOM,
        "Instantly scroll to the very bottom of the page. Useful for loading infinite scroll content "
        "or reaching footer elements.",
        SCROLL_TO_TOP_PARAMS),

    // Wait Utilities
    TOOL_DEF(TOOL_WAIT_FOR_SELECTOR,
        "Wait for an element matching the selector to appear and become visible on the page. Essential for "
        "handling dynamic content that loads after navigation. Times out with error if element doesn't appear.",
        WAIT_FOR_SELECTOR_PARAMS),
    TOOL_DEF(TOOL_WAIT,
        "Pause execution for a fixed number of milliseconds. Use sparingly - prefer browser_wait_for_selector "
        "or browser_wait_for_network_idle for more reliable synchronization.",
        WAIT_PARAMS),
    TOOL_DEF(TOOL_WAIT_FOR_NETWORK_IDLE,
        "Wait until there are no pending network requests for a specified duration. The most reliable way to "
        "wait for a page to fully load, including AJAX requests, images, and dynamic content.",
        WAIT_FOR_NETWORK_IDLE_PARAMS),
    TOOL_DEF(TOOL_WAIT_FOR_FUNCTION,
        "Wait for a custom JavaScript condition to become true. Useful for waiting on application-specific state "
        "like 'window.appReady' or complex DOM conditions that can't be expressed with a selector.",
        WAIT_FOR_FUNCTION_PARAMS),
    TOOL_DEF(TOOL_WAIT_FOR_URL,
        "Wait for the page URL to match a specific pattern. Useful after clicking links or submitting forms "
        "to ensure navigation completed. Supports substring matching or glob patterns with wildcards.",
        WAIT_FOR_URL_PARAMS),

    // Page Info
    TOOL_DEF(TOOL_GET_PAGE_INFO,
        "Get comprehensive information about the current page including URL, title, document state, "
        "can go back/forward status, and viewport dimensions. Useful for verifying navigation state.",
        GET_PAGE_INFO_PARAMS),
    TOOL_DEF(TOOL_SET_VIEWPORT,
        "Set the browser viewport size for responsive testing. Changes the visible area dimensions, "
        "triggering any responsive CSS breakpoints. Common uses: testing mobile layouts, matching specific device sizes.",
        SET_VIEWPORT_PARAMS),

    // DOM Zoom
    TOOL_DEF(TOOL_ZOOM_IN,
        "Zoom in the page content by 10%. Uses CSS zoom property to scale the entire page. "
        "Maximum zoom is 300%. Current zoom level is returned.",
        ZOOM_PARAMS),
    TOOL_DEF(TOOL_ZOOM_OUT,
        "Zoom out the page content by 10%. Uses CSS zoom property to scale the entire page. "
        "Minimum zoom is 10%. Current zoom level is returned.",
        ZOOM_PARAMS),
    TOOL_DEF(TOOL_ZOOM_RESET,
        "Reset page zoom to 100% (default). Restores the page to its original size.",
        ZOOM_PARAMS),

    // Console Logs
    TOOL_DEF(TOOL_GET_CONSOLE_LOG,
        "Read console logs from the browser. Returns JavaScript console messages (console.log, console.warn, etc). "
        "Supports filtering by level and text. Useful for debugging and monitoring page behavior.",
        GET_CONSOLE_LOG_PARAMS),
    TOOL_DEF(TOOL_CLEAR_CONSOLE_LOG,
        "Clear all console logs from the browser context. Useful to reset log state between operations.",
        CLEAR_CONSOLE_LOG_PARAMS),

    // Video Recording
    TOOL_DEF(TOOL_START_VIDEO,
        "Begin recording a video of the browser session. Captures all visual activity in the viewport. "
        "Recording continues until stopped with browser_stop_video_recording. Useful for debugging, demos, and documentation.",
        START_VIDEO_PARAMS),
    TOOL_DEF(TOOL_PAUSE_VIDEO,
        "Temporarily pause video recording without stopping it. The recording can be resumed later with "
        "browser_resume_video_recording. Paused segments are not included in the final video.",
        VIDEO_CONTEXT_PARAMS),
    TOOL_DEF(TOOL_RESUME_VIDEO,
        "Resume a paused video recording. Continues capturing from where it was paused. "
        "Only valid after browser_pause_video_recording.",
        VIDEO_CONTEXT_PARAMS),
    TOOL_DEF(TOOL_STOP_VIDEO,
        "Stop video recording and save the video file. Returns the path to the saved MP4 file in /tmp. "
        "The file can then be downloaded or processed further.",
        VIDEO_CONTEXT_PARAMS),
    TOOL_DEF(TOOL_GET_VIDEO_STATS,
        "Get statistics about the current video recording including frames captured, duration, file size estimate, "
        "and recording state (recording/paused/stopped).",
        VIDEO_CONTEXT_PARAMS),

    // Live Video Streaming
    TOOL_DEF(TOOL_START_LIVE_STREAM,
        "Start a live MJPEG video stream of the browser session accessible via HTTP. Returns a stream URL "
        "that can be viewed in any browser or video player. Useful for real-time monitoring of browser activity.",
        START_LIVE_STREAM_PARAMS),
    TOOL_DEF(TOOL_STOP_LIVE_STREAM,
        "Stop an active live video stream. Terminates the MJPEG stream and frees associated resources.",
        LIVE_STREAM_CONTEXT_PARAMS),
    TOOL_DEF(TOOL_GET_LIVE_STREAM_STATS,
        "Get statistics about an active live stream including connected viewers, frames sent, bandwidth usage, "
        "and stream duration.",
        LIVE_STREAM_CONTEXT_PARAMS),
    TOOL_DEF_NO_PARAMS(TOOL_LIST_LIVE_STREAMS,
        "List all currently active live video streams across all contexts with their URLs and stats."),
    TOOL_DEF(TOOL_GET_LIVE_FRAME,
        "Get the latest frame from a live video stream as a base64-encoded image. Useful for capturing "
        "single frames without recording a full video.",
        GET_LIVE_FRAME_PARAMS),

    // Demographics
    TOOL_DEF_NO_PARAMS(TOOL_GET_DEMOGRAPHICS,
        "Get comprehensive user demographics and context based on IP geolocation. Returns location, timezone, "
        "weather, date/time, and locale information. Useful for location-aware operations and personalizing interactions."),
    TOOL_DEF_NO_PARAMS(TOOL_GET_LOCATION,
        "Get user's current geographic location based on IP address. Returns city, country, coordinates (lat/lon), "
        "and timezone. Uses GeoIP database for lookup."),
    TOOL_DEF_NO_PARAMS(TOOL_GET_DATETIME,
        "Get current date and time information including date, time, day of week, and timezone. "
        "Reflects the server's local time."),
    TOOL_DEF_NO_PARAMS(TOOL_GET_WEATHER,
        "Get current weather conditions for the user's detected location. Returns temperature, conditions, "
        "humidity, and other weather data."),

    // CAPTCHA
    TOOL_DEF(TOOL_DETECT_CAPTCHA,
        "Detect if the current page contains a CAPTCHA challenge using heuristic analysis (no vision model). "
        "Returns detection result with confidence score. Use before attempting CAPTCHA solving.",
        CAPTCHA_DETECT_PARAMS),
    TOOL_DEF(TOOL_CLASSIFY_CAPTCHA,
        "Identify the type of CAPTCHA on the page. Classifies as text-based, image-selection, checkbox (reCAPTCHA v2), "
        "puzzle, audio, or custom. Returns element selectors for the CAPTCHA components.",
        CAPTCHA_DETECT_PARAMS),
    TOOL_DEF(TOOL_SOLVE_TEXT_CAPTCHA,
        "Solve a text-based CAPTCHA by using vision model to read distorted characters. Automatically finds the "
        "CAPTCHA image, extracts the text, enters it in the input field, and optionally submits.",
        CAPTCHA_SOLVE_TEXT_PARAMS),
    TOOL_DEF(TOOL_SOLVE_IMAGE_CAPTCHA,
        "Solve an image-selection CAPTCHA (e.g., 'select all images with traffic lights'). Uses vision model "
        "with numbered overlays for one-shot analysis. Supports reCAPTCHA, Cloudflare Turnstile, and hCaptcha.",
        CAPTCHA_SOLVE_IMAGE_PARAMS),
    TOOL_DEF(TOOL_SOLVE_CAPTCHA,
        "Auto-detect and solve any supported CAPTCHA type. Automatically detects whether it's text-based or "
        "image-selection and applies the appropriate solving strategy. Most convenient option when CAPTCHA type is unknown.",
        CAPTCHA_SOLVE_IMAGE_PARAMS),

    // Cookies
    TOOL_DEF(TOOL_GET_COOKIES,
        "Retrieve all cookies from the browser context with full details including httpOnly, secure, sameSite, "
        "domain, path, and expiration. Optionally filter by URL to get only relevant cookies.",
        GET_COOKIES_PARAMS),
    TOOL_DEF(TOOL_SET_COOKIE,
        "Set a cookie in the browser context with full control over all attributes. Supports domain, path, "
        "secure, httpOnly, sameSite, and expiration settings. Useful for injecting session tokens or test cookies.",
        SET_COOKIE_PARAMS),
    TOOL_DEF(TOOL_DELETE_COOKIES,
        "Delete cookies from the browser context. Can delete all cookies, cookies for a specific URL, "
        "or a specific cookie by name. Useful for testing login flows or clearing session state.",
        DELETE_COOKIES_PARAMS),

    // Proxy
    TOOL_DEF(TOOL_SET_PROXY,
        "Configure proxy settings for the browser context after creation. Supports HTTP, HTTPS, SOCKS4, SOCKS5, "
        "and SOCKS5H proxies with optional authentication. Includes stealth features to prevent proxy detection.",
        SET_PROXY_PARAMS),
    TOOL_DEF(TOOL_GET_PROXY_STATUS,
        "Get the current proxy configuration and connection status. Returns proxy type, host, port, "
        "and whether stealth features are enabled.",
        PROXY_CONTEXT_PARAMS),
    TOOL_DEF(TOOL_CONNECT_PROXY,
        "Enable and connect the configured proxy for the browser context. Traffic will start routing through "
        "the proxy after this call.",
        PROXY_CONTEXT_PARAMS),
    TOOL_DEF(TOOL_DISCONNECT_PROXY,
        "Disable the proxy and revert to direct connection. Useful for comparing behavior with and without proxy, "
        "or troubleshooting connection issues.",
        PROXY_CONTEXT_PARAMS),

    // Profile
    TOOL_DEF(TOOL_CREATE_PROFILE,
        "Create a new browser profile with randomized fingerprints (user agent, screen size, WebGL, Canvas, etc.). "
        "Returns the profile JSON that can be saved for later use. Each profile represents a unique browser identity.",
        CREATE_PROFILE_PARAMS),
    TOOL_DEF(TOOL_LOAD_PROFILE,
        "Load a saved browser profile from a JSON file into an existing context. Applies fingerprints, cookies, "
        "and settings from the profile. Use for maintaining consistent browser identity across sessions.",
        LOAD_PROFILE_PARAMS),
    TOOL_DEF(TOOL_SAVE_PROFILE,
        "Save the current context state (fingerprints, cookies, settings) to a profile JSON file. "
        "Useful for persisting login sessions and browser identity for reuse.",
        SAVE_PROFILE_PARAMS),
    TOOL_DEF(TOOL_GET_PROFILE,
        "Get the current profile state for the context as JSON. Includes fingerprints, current cookies, "
        "and all settings. Useful for inspecting profile details.",
        PROFILE_CONTEXT_PARAMS),
    TOOL_DEF(TOOL_UPDATE_PROFILE_COOKIES,
        "Update the profile file with the current cookies from the browser. Call after login to persist "
        "session cookies. Only updates cookies, preserving other profile settings.",
        PROFILE_CONTEXT_PARAMS),
    TOOL_DEF(TOOL_GET_CONTEXT_INFO,
        "Get context information including the VM profile and fingerprint hashes (canvas, audio, GPU) currently in use. "
        "Returns the stealth configuration for the browser context including vm_id, canvas hash seed, audio noise seed, "
        "and GPU profile details.",
        PROFILE_CONTEXT_PARAMS),

    // Network Interception
    TOOL_DEF(TOOL_ADD_NETWORK_RULE,
        "Add a rule to intercept network requests matching a URL pattern. Rules can block requests (e.g., ads, trackers), "
        "mock responses with custom data (useful for testing), or redirect to different URLs. Supports glob patterns "
        "(*.js, *://api.example.com/*) or regex for flexible matching. Returns a rule_id for later removal.",
        ADD_NETWORK_RULE_PARAMS),
    TOOL_DEF(TOOL_REMOVE_NETWORK_RULE,
        "Remove a previously added network interception rule by its ID. The rule immediately stops applying to new requests. "
        "Use browser_add_network_rule's returned rule_id to remove specific rules.",
        REMOVE_NETWORK_RULE_PARAMS),
    TOOL_DEF(TOOL_ENABLE_NETWORK_INTERCEPTION,
        "Enable or disable network interception for a context. When disabled, all rules are ignored and requests "
        "pass through normally. When enabled, rules start applying. Useful for toggling interception without removing rules.",
        ENABLE_NETWORK_INTERCEPTION_PARAMS),
    TOOL_DEF(TOOL_GET_NETWORK_LOG,
        "Retrieve the log of captured network requests and responses for debugging and analysis. "
        "Returns request URLs, methods, headers, status codes, and response sizes. Enable interception first to start capturing.",
        NETWORK_LOG_PARAMS),
    TOOL_DEF(TOOL_CLEAR_NETWORK_LOG,
        "Clear all captured network log entries for the context. Use periodically to prevent memory buildup "
        "during long sessions with network interception enabled.",
        NETWORK_LOG_PARAMS),
    TOOL_DEF(TOOL_ENABLE_NETWORK_LOGGING,
        "Enable or disable network request/response logging for the context. When enabled, all network "
        "activity is captured and can be retrieved with get_network_log.",
        ENABLE_NETWORK_LOGGING_PARAMS),

    // File Downloads
    TOOL_DEF(TOOL_SET_DOWNLOAD_PATH,
        "Configure the directory where browser downloads will be saved. Must be set before triggering downloads. "
        "The directory must exist and be writable. Downloaded files are saved with their original filenames.",
        SET_DOWNLOAD_PATH_PARAMS),
    TOOL_DEF(TOOL_GET_DOWNLOADS,
        "List all downloads for the context with their current status. Returns download ID, filename, URL, "
        "progress percentage, bytes downloaded, total size, and state (pending, in_progress, completed, failed, cancelled).",
        GET_DOWNLOADS_PARAMS),
    TOOL_DEF(TOOL_GET_ACTIVE_DOWNLOADS,
        "Get only currently active (in-progress) downloads for the context. More efficient than get_downloads "
        "when you only need to monitor ongoing transfers.",
        GET_ACTIVE_DOWNLOADS_PARAMS),
    TOOL_DEF(TOOL_WAIT_FOR_DOWNLOAD,
        "Block until a specific download completes or times out. Returns the final download status and file path "
        "on success. Use after triggering a download to ensure it finishes before proceeding.",
        WAIT_FOR_DOWNLOAD_PARAMS),
    TOOL_DEF(TOOL_CANCEL_DOWNLOAD,
        "Cancel an in-progress download. Stops the download immediately and may delete partially downloaded content. "
        "The download will be marked as 'cancelled' in the downloads list.",
        CANCEL_DOWNLOAD_PARAMS),

    // Dialog Handling
    TOOL_DEF(TOOL_SET_DIALOG_ACTION,
        "Configure automatic handling for JavaScript dialogs (alert, confirm, prompt, beforeunload). "
        "Set once and all future dialogs of that type are handled automatically. Useful for preventing dialog "
        "interruptions during automated browsing. Each dialog type can have a different action.",
        SET_DIALOG_ACTION_PARAMS),
    TOOL_DEF(TOOL_GET_PENDING_DIALOG,
        "Check if there's a JavaScript dialog currently waiting for user action. Returns dialog type, message, "
        "default prompt value, and dialog ID. Returns null/empty if no dialog is pending.",
        GET_PENDING_DIALOG_PARAMS),
    TOOL_DEF(TOOL_GET_DIALOGS,
        "Get all dialog events that have occurred in the context, including handled and pending dialogs. "
        "Useful for reviewing dialog history and debugging dialog handling.",
        GET_DIALOGS_PARAMS),
    TOOL_DEF(TOOL_HANDLE_DIALOG,
        "Manually accept or dismiss a specific dialog by its ID. Use for interactive dialog handling instead of "
        "automatic actions. For prompt dialogs, can provide response text when accepting.",
        HANDLE_DIALOG_PARAMS),
    TOOL_DEF(TOOL_WAIT_FOR_DIALOG,
        "Wait for a JavaScript dialog to appear. Blocks until a dialog shows up or timeout expires. "
        "Useful when you know an action will trigger a dialog and need to handle it specifically.",
        WAIT_FOR_DIALOG_PARAMS),

    // Tab/Window Management
    TOOL_DEF(TOOL_SET_POPUP_POLICY,
        "Configure how popup windows are handled when JavaScript calls window.open() or links have target='_blank'. "
        "Options: 'allow' opens new window, 'block' prevents popup entirely, 'new_tab' opens in foreground tab, "
        "'background' opens in background tab. Default behavior may vary by browser.",
        SET_POPUP_POLICY_PARAMS),
    TOOL_DEF(TOOL_GET_TABS,
        "List all tabs/windows in the browser context. Returns tab ID, URL, title, and active status for each tab. "
        "Use to discover which tabs are open and get tab IDs for switching or closing.",
        GET_TABS_PARAMS),
    TOOL_DEF(TOOL_SWITCH_TAB,
        "Switch focus to a specific tab by its ID. The tab becomes the active tab, and subsequent browser "
        "operations will interact with that tab's content. Other tabs continue running in the background.",
        SWITCH_TAB_PARAMS),
    TOOL_DEF(TOOL_CLOSE_TAB,
        "Close a specific tab by its ID. The tab is destroyed and its resources freed. "
        "If the closed tab was active, another tab becomes active. Cannot close the last remaining tab.",
        CLOSE_TAB_PARAMS),
    TOOL_DEF(TOOL_NEW_TAB,
        "Open a new tab in the browser context. Optionally navigate to a URL immediately. "
        "The new tab becomes active by default. Use for parallel browsing within a single context.",
        NEW_TAB_PARAMS),
    TOOL_DEF(TOOL_GET_ACTIVE_TAB,
        "Get information about the currently active (focused) tab. Returns tab ID, URL, and title. "
        "Browser operations target this tab unless you switch to another.",
        TAB_CONTEXT_PARAMS),
    TOOL_DEF(TOOL_GET_TAB_COUNT,
        "Get the number of open tabs in the context. Quick way to check if multiple tabs exist "
        "without retrieving full tab details.",
        TAB_CONTEXT_PARAMS),
    TOOL_DEF(TOOL_GET_BLOCKED_POPUPS,
        "Get a list of popup URLs that were blocked due to popup policy set to 'block'. "
        "Useful for discovering popups that would have opened and deciding whether to allow them.",
        TAB_CONTEXT_PARAMS),

    // License Management
    TOOL_DEF_NO_PARAMS(TOOL_GET_LICENSE_STATUS,
        "Check if the browser has a valid license. Returns license status (valid, expired, missing, invalid), "
        "expiration date, and license type (trial, standard, enterprise). Quick check for license validity."),
    TOOL_DEF_NO_PARAMS(TOOL_GET_LICENSE_INFO,
        "Get comprehensive license information including licensee name, license key (partially masked), "
        "features enabled, usage limits, support expiration, and full license metadata."),
    TOOL_DEF_NO_PARAMS(TOOL_GET_HARDWARE_FINGERPRINT,
        "Get the machine's hardware fingerprint used for license binding. Provide this to obtain a license "
        "file tied to your hardware. The fingerprint is a unique identifier based on hardware characteristics."),
    TOOL_DEF(TOOL_ADD_LICENSE,
        "Install a license file to activate the browser. Provide either a file path or base64-encoded content. "
        "The license is validated against the hardware fingerprint. Returns success/failure with validation details.",
        ADD_LICENSE_PARAMS),
    TOOL_DEF_NO_PARAMS(TOOL_REMOVE_LICENSE,
        "Remove the currently installed license. The browser reverts to unlicensed/trial mode. "
        "Use before installing a new license or to deactivate the browser on this machine."),

    // Element Picker (for UI overlays)
    TOOL_DEF(TOOL_GET_ELEMENT_AT_POSITION,
        "Get detailed information about the DOM element at specific XY coordinates. Returns element tag, ID, classes, "
        "bounding box, text content, and computed styles. Use with browser_show_grid_overlay to find coordinates. "
        "Useful for debugging and building element picker UIs.",
        GET_ELEMENT_AT_POSITION_PARAMS),
    TOOL_DEF(TOOL_GET_INTERACTIVE_ELEMENTS,
        "Find all interactive elements on the page (buttons, links, inputs, selects, etc.). Returns position, size, "
        "and identifying info for each element. Used for building visual overlays showing clickable/interactable areas, "
        "or for automated element discovery.",
        GET_INTERACTIVE_ELEMENTS_PARAMS),
    TOOL_DEF(TOOL_GET_BLOCKER_STATS,
        "Get statistics about blocked ads, trackers, and analytics. Returns counts for each category "
        "and total blocked/allowed requests. Useful for monitoring ad blocker effectiveness.",
        GET_BLOCKER_STATS_PARAMS),

    // IPC Tests (only available when OWL_IPC_TESTS_ENABLED=true)
    TOOL_DEF(TOOL_IPC_TESTS_RUN,
        "Start a new IPC test run to validate browser IPC communication. Supports multiple test modes: "
        "'smoke' for quick validation (5 tests), 'full' for comprehensive testing (138+ methods), "
        "'benchmark' for performance measurement, 'stress' for load testing, 'leak-check' for memory analysis, "
        "'parallel' for concurrent context testing. Returns a run_id to track progress.",
        IPC_TESTS_RUN_PARAMS),
    TOOL_DEF(TOOL_IPC_TESTS_STATUS,
        "Get the status of an IPC test run. Returns status (running/completed/failed/aborted), "
        "test results summary (total/passed/failed), duration, and commands per second. "
        "If run_id is not provided, returns status of the current or last run.",
        IPC_TESTS_STATUS_PARAMS),
    TOOL_DEF(TOOL_IPC_TESTS_ABORT,
        "Abort a running IPC test. Sends SIGTERM to the test process and waits for graceful shutdown. "
        "Use when a test is taking too long or needs to be cancelled.",
        IPC_TESTS_ABORT_PARAMS),
    TOOL_DEF_NO_PARAMS(TOOL_IPC_TESTS_LIST_REPORTS,
        "List all available IPC test reports. Returns an array of report metadata including "
        "run_id, timestamp, mode, test counts, and duration. Reports are stored in the reports directory."),
    TOOL_DEF(TOOL_IPC_TESTS_GET_REPORT,
        "Get a specific IPC test report by run_id. Returns either JSON data or HTML dashboard "
        "depending on the format parameter. JSON includes detailed test results, latency stats, "
        "resource usage, and failure details. HTML is an interactive dashboard with charts.",
        IPC_TESTS_GET_REPORT_PARAMS),
    TOOL_DEF(TOOL_IPC_TESTS_DELETE_REPORT,
        "Delete an IPC test report by run_id. Removes both JSON and HTML report files from disk. "
        "Use to clean up old test results.",
        IPC_TESTS_DELETE_REPORT_PARAMS),
    TOOL_DEF_NO_PARAMS(TOOL_IPC_TESTS_CLEAN_ALL,
        "Delete all IPC test reports. Removes all JSON and HTML report files from the reports directory. "
        "Use to clean up all test history."),
};

static const int g_tools_count = sizeof(g_tools) / sizeof(g_tools[0]);

// ============================================================================
// Public Functions
// ============================================================================

void tools_init(void) {
    // Nothing to initialize currently
}

const ToolDef* tools_get(const char* tool_name) {
    if (!tool_name) return NULL;

    for (int i = 0; i < g_tools_count; i++) {
        if (strcmp(g_tools[i].name, tool_name) == 0) {
            return &g_tools[i];
        }
    }
    return NULL;
}

int tools_get_all(const ToolDef** tools) {
    if (tools) {
        *tools = g_tools;
    }
    return g_tools_count;
}

bool tools_exists(const char* tool_name) {
    return tools_get(tool_name) != NULL;
}

// ============================================================================
// Validation
// ============================================================================

static bool validate_enum(const char* value, const char** valid_values, int count) {
    if (!value || !valid_values) return false;

    for (int i = 0; i < count && valid_values[i]; i++) {
        if (strcmp(value, valid_values[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool tools_validate(const char* tool_name, const JsonValue* params,
                    ValidationResult* result) {
    if (!result) return false;

    memset(result, 0, sizeof(*result));
    result->valid = true;

    const ToolDef* tool = tools_get(tool_name);
    if (!tool) {
        result->valid = false;
        snprintf(result->errors[0].field, sizeof(result->errors[0].field), "tool");
        snprintf(result->errors[0].message, sizeof(result->errors[0].message),
                 "Unknown tool: %s", tool_name);
        result->error_count = 1;
        return false;
    }

    // Build supported fields documentation
    char* supported = result->supported_fields;
    int supported_remaining = sizeof(result->supported_fields);

    if (tool->param_count > 0) {
        int n = snprintf(supported, supported_remaining, "Supported fields for %s: ", tool_name);
        supported += n;
        supported_remaining -= n;

        for (int i = 0; i < tool->param_count && supported_remaining > 0; i++) {
            const char* req = tool->params[i].required ? " (required)" : " (optional)";
            n = snprintf(supported, supported_remaining, "%s%s%s",
                        tool->params[i].name, req,
                        i < tool->param_count - 1 ? ", " : "");
            supported += n;
            supported_remaining -= n;
        }
    } else {
        snprintf(supported, supported_remaining, "%s takes no parameters", tool_name);
    }

    // If tool takes no params, params should be null or empty object
    if (tool->param_count == 0) {
        return true;
    }

    // Check required fields
    char* missing = result->missing_fields;
    int missing_remaining = sizeof(result->missing_fields);

    for (int i = 0; i < tool->param_count; i++) {
        if (!tool->params[i].required) continue;

        if (!params || !json_object_has(params, tool->params[i].name)) {
            result->valid = false;

            if (strlen(result->missing_fields) > 0) {
                int n = snprintf(missing, missing_remaining, ", %s", tool->params[i].name);
                missing += n;
                missing_remaining -= n;
            } else {
                int n = snprintf(missing, missing_remaining, "%s", tool->params[i].name);
                missing += n;
                missing_remaining -= n;
            }

            if (result->error_count < 32) {
                ValidationError* err = &result->errors[result->error_count++];
                strncpy(err->field, tool->params[i].name, sizeof(err->field) - 1);
                snprintf(err->message, sizeof(err->message),
                        "Missing required field: %s - %s",
                        tool->params[i].name, tool->params[i].description);
            }
        }
    }

    if (!params) {
        return result->valid;
    }

    // Check for unknown fields and validate types
    if (params->type == JSON_OBJECT && params->object_val) {
        char* unknown = result->unknown_fields;
        int unknown_remaining = sizeof(result->unknown_fields);

        JsonPair* pair = params->object_val->pairs;
        while (pair) {
            bool found = false;
            const ToolParam* param_def = NULL;

            for (int i = 0; i < tool->param_count; i++) {
                if (strcmp(pair->key, tool->params[i].name) == 0) {
                    found = true;
                    param_def = &tool->params[i];
                    break;
                }
            }

            if (!found) {
                result->valid = false;

                if (strlen(result->unknown_fields) > 0) {
                    int n = snprintf(unknown, unknown_remaining, ", %s", pair->key);
                    unknown += n;
                    unknown_remaining -= n;
                } else {
                    int n = snprintf(unknown, unknown_remaining, "%s", pair->key);
                    unknown += n;
                    unknown_remaining -= n;
                }

                if (result->error_count < 32) {
                    ValidationError* err = &result->errors[result->error_count++];
                    strncpy(err->field, pair->key, sizeof(err->field) - 1);
                    snprintf(err->message, sizeof(err->message),
                            "Unknown field: %s is not a valid parameter for %s",
                            pair->key, tool_name);
                }
            } else if (param_def) {
                // Validate type
                bool type_valid = true;
                const char* expected_type = "";

                switch (param_def->type) {
                    case PARAM_STRING:
                        type_valid = (pair->value->type == JSON_STRING);
                        expected_type = "string";
                        break;
                    case PARAM_INT:
                        type_valid = (pair->value->type == JSON_NUMBER);
                        expected_type = "integer";
                        break;
                    case PARAM_NUMBER:
                        type_valid = (pair->value->type == JSON_NUMBER);
                        expected_type = "number";
                        break;
                    case PARAM_BOOL:
                        type_valid = (pair->value->type == JSON_BOOL);
                        expected_type = "boolean";
                        break;
                    case PARAM_ENUM:
                        if (pair->value->type != JSON_STRING) {
                            type_valid = false;
                            expected_type = "string (enum)";
                        } else {
                            type_valid = validate_enum(pair->value->string_val,
                                                      param_def->enum_values,
                                                      param_def->enum_count);
                            if (!type_valid) {
                                if (result->error_count < 32) {
                                    ValidationError* err = &result->errors[result->error_count++];
                                    strncpy(err->field, pair->key, sizeof(err->field) - 1);

                                    char valid_vals[512] = {0};
                                    for (int i = 0; i < param_def->enum_count && param_def->enum_values[i]; i++) {
                                        if (i > 0) strcat(valid_vals, ", ");
                                        strcat(valid_vals, param_def->enum_values[i]);
                                    }
                                    snprintf(err->message, sizeof(err->message),
                                            "Invalid value '%s' for %s. Valid values: %s",
                                            pair->value->string_val, pair->key, valid_vals);
                                }
                                result->valid = false;
                            }
                        }
                        break;
                }

                if (!type_valid && param_def->type != PARAM_ENUM) {
                    result->valid = false;
                    if (result->error_count < 32) {
                        ValidationError* err = &result->errors[result->error_count++];
                        strncpy(err->field, pair->key, sizeof(err->field) - 1);
                        snprintf(err->message, sizeof(err->message),
                                "Invalid type for %s: expected %s",
                                pair->key, expected_type);
                    }
                }
            }

            pair = pair->next;
        }
    }

    return result->valid;
}

char* tools_validation_error_json(const char* tool_name,
                                   const ValidationResult* result) {
    JsonBuilder builder;
    json_builder_init(&builder);

    json_builder_object_start(&builder);

    json_builder_key(&builder, "success");
    json_builder_bool(&builder, false);

    json_builder_comma(&builder);
    json_builder_key(&builder, "error");
    json_builder_string(&builder, "Validation failed");

    json_builder_comma(&builder);
    json_builder_key(&builder, "tool");
    json_builder_string(&builder, tool_name);

    if (strlen(result->missing_fields) > 0) {
        json_builder_comma(&builder);
        json_builder_key(&builder, "missing_fields");
        json_builder_string(&builder, result->missing_fields);
    }

    if (strlen(result->unknown_fields) > 0) {
        json_builder_comma(&builder);
        json_builder_key(&builder, "unknown_fields");
        json_builder_string(&builder, result->unknown_fields);
    }

    json_builder_comma(&builder);
    json_builder_key(&builder, "supported_fields");
    json_builder_string(&builder, result->supported_fields);

    if (result->error_count > 0) {
        json_builder_comma(&builder);
        json_builder_key(&builder, "errors");
        json_builder_array_start(&builder);

        for (int i = 0; i < result->error_count; i++) {
            if (i > 0) json_builder_comma(&builder);
            json_builder_object_start(&builder);
            json_builder_key(&builder, "field");
            json_builder_string(&builder, result->errors[i].field);
            json_builder_comma(&builder);
            json_builder_key(&builder, "message");
            json_builder_string(&builder, result->errors[i].message);
            json_builder_object_end(&builder);
        }

        json_builder_array_end(&builder);
    }

    json_builder_object_end(&builder);

    return json_builder_finish(&builder);
}

char* tools_get_documentation(const char* tool_name) {
    const ToolDef* tool = tools_get(tool_name);
    if (!tool) {
        return json_error_response("Unknown tool");
    }

    JsonBuilder builder;
    json_builder_init(&builder);

    json_builder_object_start(&builder);

    json_builder_key(&builder, "name");
    json_builder_string(&builder, tool->name);

    json_builder_comma(&builder);
    json_builder_key(&builder, "description");
    json_builder_string(&builder, tool->description);

    json_builder_comma(&builder);
    json_builder_key(&builder, "parameters");
    json_builder_array_start(&builder);

    for (int i = 0; i < tool->param_count; i++) {
        if (i > 0) json_builder_comma(&builder);

        json_builder_object_start(&builder);

        json_builder_key(&builder, "name");
        json_builder_string(&builder, tool->params[i].name);

        json_builder_comma(&builder);
        json_builder_key(&builder, "type");
        const char* type_str;
        switch (tool->params[i].type) {
            case PARAM_STRING: type_str = "string"; break;
            case PARAM_INT: type_str = "integer"; break;
            case PARAM_NUMBER: type_str = "number"; break;
            case PARAM_BOOL: type_str = "boolean"; break;
            case PARAM_ENUM: type_str = "enum"; break;
            default: type_str = "unknown";
        }
        json_builder_string(&builder, type_str);

        json_builder_comma(&builder);
        json_builder_key(&builder, "required");
        json_builder_bool(&builder, tool->params[i].required);

        json_builder_comma(&builder);
        json_builder_key(&builder, "description");
        json_builder_string(&builder, tool->params[i].description);

        if (tool->params[i].type == PARAM_ENUM && tool->params[i].enum_values) {
            json_builder_comma(&builder);
            json_builder_key(&builder, "enum_values");
            json_builder_array_start(&builder);
            for (int j = 0; j < tool->params[i].enum_count && tool->params[i].enum_values[j]; j++) {
                if (j > 0) json_builder_comma(&builder);
                json_builder_string(&builder, tool->params[i].enum_values[j]);
            }
            json_builder_array_end(&builder);
        }

        json_builder_object_end(&builder);
    }

    json_builder_array_end(&builder);
    json_builder_object_end(&builder);

    return json_builder_finish(&builder);
}

char* tools_get_all_documentation(void) {
    JsonBuilder builder;
    json_builder_init(&builder);

    json_builder_object_start(&builder);
    json_builder_key(&builder, "tools");
    json_builder_array_start(&builder);

    for (int i = 0; i < g_tools_count; i++) {
        if (i > 0) json_builder_comma(&builder);

        json_builder_object_start(&builder);
        json_builder_key(&builder, "name");
        json_builder_string(&builder, g_tools[i].name);
        json_builder_comma(&builder);
        json_builder_key(&builder, "description");
        json_builder_string(&builder, g_tools[i].description);
        json_builder_comma(&builder);
        json_builder_key(&builder, "param_count");
        json_builder_int(&builder, g_tools[i].param_count);
        json_builder_object_end(&builder);
    }

    json_builder_array_end(&builder);
    json_builder_object_end(&builder);

    return json_builder_finish(&builder);
}
