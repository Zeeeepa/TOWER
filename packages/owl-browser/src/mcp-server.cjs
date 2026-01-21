#!/usr/bin/env node

const { Server } = require('@modelcontextprotocol/sdk/server/index.js');
const { StdioServerTransport } = require('@modelcontextprotocol/sdk/server/stdio.js');
const {
  CallToolRequestSchema,
  ListToolsRequestSchema,
} = require('@modelcontextprotocol/sdk/types.js');
const { BrowserWrapper } = require('./browser-wrapper.cjs');

let browserWrapper = null;
// Map of context_id -> metadata for tracking active contexts
const activeContexts = new Map();

/**
 * Format an ActionResult response for MCP tools.
 * Includes status code for better debugging and error tracking.
 * @param {Object} result - ActionResult from browser wrapper
 * @param {string} [customMessage] - Optional custom message to use instead of result.message
 * @returns {Object} MCP response object
 */
function formatActionResult(result, customMessage = null) {
  const message = customMessage || result.message || (result.success ? 'Success' : 'Failed');

  // For simple successes, just return the message
  if (result.success && result.status === 'ok') {
    return {
      content: [{ type: 'text', text: message }],
    };
  }

  // For non-ok statuses (even on success, like verification_timeout), include structured info
  const responseData = {
    success: result.success,
    status: result.status || 'unknown',
    message: message,
  };

  // Include additional fields if present
  if (result.selector) responseData.selector = result.selector;
  if (result.url) responseData.url = result.url;
  if (result.error_code) responseData.error_code = result.error_code;
  if (result.http_status) responseData.http_status = result.http_status;
  if (result.element_count) responseData.element_count = result.element_count;

  return {
    content: [{ type: 'text', text: JSON.stringify(responseData, null, 2) }],
  };
}

// Create MCP server
const server = new Server(
  {
    name: 'owl-browser',
    version: '1.0.0',
  },
  {
    capabilities: {
      tools: {},
    },
  }
);

// Define tools
const tools = [
  {
    name: 'browser_create_context',
    description: 'Create a new browser context (tab/window). Returns a context_id to use with other tools. Each context is isolated. Optionally configure LLM (Language Model) settings for this context.',
    inputSchema: {
      type: 'object',
      properties: {
        llm_enabled: {
          type: 'boolean',
          description: 'Enable or disable LLM features for this context (default: true)',
        },
        llm_use_builtin: {
          type: 'boolean',
          description: 'Use built-in llama-server if available (default: true)',
        },
        llm_endpoint: {
          type: 'string',
          description: 'External LLM API endpoint (e.g., "https://api.openai.com" for OpenAI)',
        },
        llm_model: {
          type: 'string',
          description: 'External LLM model name (e.g., "gpt-4-vision-preview" for OpenAI)',
        },
        llm_api_key: {
          type: 'string',
          description: 'API key for external LLM provider',
        },
        // Proxy configuration
        proxy_type: {
          type: 'string',
          enum: ['http', 'https', 'socks4', 'socks5', 'socks5h'],
          description: 'Proxy type. socks5h uses remote DNS resolution for maximum stealth.',
        },
        proxy_host: {
          type: 'string',
          description: 'Proxy server hostname or IP address',
        },
        proxy_port: {
          type: 'number',
          description: 'Proxy server port',
        },
        proxy_username: {
          type: 'string',
          description: 'Proxy authentication username (optional)',
        },
        proxy_password: {
          type: 'string',
          description: 'Proxy authentication password (optional)',
        },
        proxy_stealth: {
          type: 'boolean',
          description: 'Enable stealth mode - blocks WebRTC leaks and other detection vectors (default: true when proxy is used)',
        },
        proxy_ca_cert_path: {
          type: 'string',
          description: 'Path to custom CA certificate file (.pem, .crt, .cer) for SSL interception proxies like Charles Proxy or mitmproxy',
        },
        proxy_trust_custom_ca: {
          type: 'boolean',
          description: 'Trust the custom CA certificate for SSL interception (default: false). Enable this when using Charles Proxy, mitmproxy, or similar tools.',
        },
        // Tor-specific configuration for circuit isolation
        is_tor: {
          type: 'boolean',
          description: 'Explicitly mark this proxy as Tor. Auto-detected if proxy is localhost:9050/9150 with socks5/socks5h.',
        },
        tor_control_port: {
          type: 'number',
          description: 'Tor control port for circuit isolation (default: auto-detect 9051 or 9151). Set to -1 to disable. Each context gets a new exit node IP.',
        },
        tor_control_password: {
          type: 'string',
          description: 'Password for Tor control port authentication. Leave empty to use cookie auth or no auth.',
        },
        // Profile configuration
        profile_path: {
          type: 'string',
          description: 'Path to a browser profile JSON file. If the file exists, it will load the profile (fingerprints, cookies, settings). If not, a new profile will be created and saved to this path.',
        },
        // Resource blocking configuration
        resource_blocking: {
          type: 'boolean',
          description: 'Enable or disable resource blocking (ads, trackers, analytics). Default: true (enabled).',
        },
        // Profile filtering options
        os: {
          type: 'string',
          enum: ['windows', 'macos', 'linux'],
          description: 'Filter profiles by operating system. If set, only profiles matching this OS will be used.',
        },
        gpu: {
          type: 'string',
          description: 'Filter profiles by GPU vendor/model. If set, only profiles with matching GPU will be used. Examples: "nvidia", "amd", "intel".',
        },
      },
    },
  },
  {
    name: 'browser_close_context',
    description: 'Close a browser context and free its resources',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID to close',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_list_contexts',
    description: 'List all active browser contexts',
    inputSchema: {
      type: 'object',
      properties: {},
    },
  },
  {
    name: 'browser_navigate',
    description: 'Navigate to a URL in the browser. By default returns immediately after starting navigation. Use wait_until to wait for page load.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID (from browser_create_context)',
        },
        url: {
          type: 'string',
          description: 'The URL to navigate to',
        },
        wait_until: {
          type: 'string',
          enum: ['', 'load', 'domcontentloaded', 'networkidle'],
          description: 'When to consider navigation complete. Empty string (default) returns immediately. "load" waits for load event. "domcontentloaded" waits for DOMContentLoaded. "networkidle" waits for network to be idle.',
        },
        timeout: {
          type: 'number',
          description: 'Maximum time to wait in milliseconds (default: 30000). Only used when wait_until is specified.',
        },
      },
      required: ['context_id', 'url'],
    },
  },
  {
    name: 'browser_click',
    description: 'Click an element on the page using CSS selector, position coordinates, or semantic description',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        selector: {
          type: 'string',
          description: 'CSS selector for the element to click, position coordinates (e.g., "100x200"), or natural language description (e.g., "search button", "login link")',
        },
      },
      required: ['context_id', 'selector'],
    },
  },
  {
    name: 'browser_drag_drop',
    description: 'Drag from a start position to an end position, optionally passing through waypoints. Useful for slider CAPTCHAs, puzzle solving, and drawing interactions.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        start_x: {
          type: 'number',
          description: 'Start X coordinate for the drag',
        },
        start_y: {
          type: 'number',
          description: 'Start Y coordinate for the drag',
        },
        end_x: {
          type: 'number',
          description: 'End X coordinate for the drop',
        },
        end_y: {
          type: 'number',
          description: 'End Y coordinate for the drop',
        },
        mid_points: {
          type: 'array',
          items: {
            type: 'array',
            items: { type: 'number' },
            minItems: 2,
            maxItems: 2,
          },
          description: 'Optional array of [x, y] waypoints to pass through during drag. Example: [[100, 200], [150, 250], [200, 300]]',
        },
      },
      required: ['context_id', 'start_x', 'start_y', 'end_x', 'end_y'],
    },
  },
  {
    name: 'browser_html5_drag_drop',
    description: 'Drag and drop for HTML5 draggable elements (elements with draggable="true"). Dispatches proper HTML5 DragEvent objects (dragstart, dragover, drop, dragend). Use this for reordering lists, sortable interfaces, and any elements using the HTML5 Drag and Drop API.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        source_selector: {
          type: 'string',
          description: 'CSS selector for the source element to drag (e.g., ".reorder-item[data-value=\\"3\\"]")',
        },
        target_selector: {
          type: 'string',
          description: 'CSS selector for the target element to drop onto (e.g., ".reorder-item[data-value=\\"1\\"]")',
        },
      },
      required: ['context_id', 'source_selector', 'target_selector'],
    },
  },
  {
    name: 'browser_mouse_move',
    description: 'Move the mouse cursor along a natural curved path from start to end position. Uses bezier curves with random variation, micro-jitter, and easing for human-like movement. Essential for avoiding bot detection. Optionally specify stop points where the cursor pauses briefly.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        start_x: {
          type: 'number',
          description: 'Start X coordinate (current cursor position)',
        },
        start_y: {
          type: 'number',
          description: 'Start Y coordinate (current cursor position)',
        },
        end_x: {
          type: 'number',
          description: 'End X coordinate (target position)',
        },
        end_y: {
          type: 'number',
          description: 'End Y coordinate (target position)',
        },
        steps: {
          type: 'number',
          description: 'Number of intermediate points (0 = auto-calculate based on distance). More steps = smoother movement.',
        },
        stop_points: {
          type: 'array',
          items: {
            type: 'array',
            items: { type: 'number' },
            minItems: 2,
            maxItems: 2,
          },
          description: 'Optional array of [x, y] coordinates where cursor should pause briefly (50-150ms). Example: [[200, 300], [400, 350]]',
        },
      },
      required: ['context_id', 'start_x', 'start_y', 'end_x', 'end_y'],
    },
  },
  {
    name: 'browser_type',
    description: 'Type text into an input field using CSS selector, position coordinates, or semantic description',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        selector: {
          type: 'string',
          description: 'CSS selector for the input element, position coordinates (e.g., "100x200"), or natural language description (e.g., "search box", "email input")',
        },
        text: {
          type: 'string',
          description: 'Text to type into the field',
        },
      },
      required: ['context_id', 'selector', 'text'],
    },
  },
  {
    name: 'browser_pick',
    description: 'Select an option from a dropdown/select element using CSS selector or semantic description. Supports both static selects and dynamic ones (like select2) by typing the value if needed.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        selector: {
          type: 'string',
          description: 'CSS selector for the select element, or natural language description (e.g., "country list", "category dropdown")',
        },
        value: {
          type: 'string',
          description: 'Value or visible text of the option to select (e.g., "Morocco", "Technology")',
        },
      },
      required: ['context_id', 'selector', 'value'],
    },
  },
  {
    name: 'browser_press_key',
    description: 'Press a special key (Enter, Tab, Escape, etc.)',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        key: {
          type: 'string',
          description: 'Key name: Enter, Return, Tab, Escape, Esc, Backspace, Delete, Del, ArrowUp, Up, ArrowDown, Down, ArrowLeft, Left, ArrowRight, Right, Space, Home, End, PageUp, PageDown',
          enum: ['Enter', 'Return', 'Tab', 'Escape', 'Esc', 'Backspace', 'Delete', 'Del', 'ArrowUp', 'Up', 'ArrowDown', 'Down', 'ArrowLeft', 'Left', 'ArrowRight', 'Right', 'Space', 'Home', 'End', 'PageUp', 'PageDown'],
        },
      },
      required: ['context_id', 'key'],
    },
  },
  {
    name: 'browser_submit_form',
    description: 'Submit the currently focused form by pressing Enter. Useful for search boxes and forms that submit on Enter.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_extract_text',
    description: 'Extract text content from the page',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        selector: {
          type: 'string',
          description: 'CSS selector for the element or natural language description (optional, extracts whole page if not provided)',
          default: 'body',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_screenshot',
    description: 'Take a screenshot of the current page',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        mode: {
          type: 'string',
          description: 'Screenshot mode: "viewport" (default, current visible view), "element" (specific element by selector), "fullpage" (entire scrollable page)',
          enum: ['viewport', 'element', 'fullpage'],
          default: 'viewport',
        },
        selector: {
          type: 'string',
          description: 'CSS selector or natural language description for the element to capture (required when mode is "element")',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_highlight',
    description: 'Highlight an element with colored border and background for debugging element selection. Useful for verifying which element will be clicked before actual interaction.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        selector: {
          type: 'string',
          description: 'CSS selector for the element or natural language description (e.g., "next button", "search box")',
        },
        border_color: {
          type: 'string',
          description: 'Border color (CSS color, default: "#FF0000")',
          default: '#FF0000',
        },
        background_color: {
          type: 'string',
          description: 'Background color (CSS color with alpha, default: "rgba(255, 0, 0, 0.2)")',
          default: 'rgba(255, 0, 0, 0.2)',
        },
      },
      required: ['context_id', 'selector'],
    },
  },
  {
    name: 'browser_show_grid_overlay',
    description: 'Show a grid overlay on top of the web page with XY position coordinates at intersections. Useful for debugging and understanding element positions. Creates 25 horizontal and 25 vertical lines by default with low opacity so content remains visible.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        horizontal_lines: {
          type: 'number',
          description: 'Number of horizontal lines (top to bottom). Default: 25',
          default: 25,
        },
        vertical_lines: {
          type: 'number',
          description: 'Number of vertical lines (left to right). Default: 25',
          default: 25,
        },
        line_color: {
          type: 'string',
          description: 'Line color with opacity (CSS color, default: "rgba(255, 0, 0, 0.15)")',
          default: 'rgba(255, 0, 0, 0.15)',
        },
        text_color: {
          type: 'string',
          description: 'Coordinate label text color (CSS color, default: "rgba(255, 0, 0, 0.4)")',
          default: 'rgba(255, 0, 0, 0.4)',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_get_html',
    description: 'Extract clean HTML from the page with configurable cleaning levels',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        clean_level: {
          type: 'string',
          description: 'Cleaning level: minimal, basic, or aggressive',
          enum: ['minimal', 'basic', 'aggressive'],
          default: 'basic',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_get_markdown',
    description: 'Extract page content as clean Markdown',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        include_links: {
          type: 'boolean',
          description: 'Include links in markdown',
          default: true,
        },
        include_images: {
          type: 'boolean',
          description: 'Include images in markdown',
          default: true,
        },
        max_length: {
          type: 'number',
          description: 'Maximum length in characters (-1 for no limit)',
          default: -1,
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_extract_json',
    description: 'Extract structured JSON data from the page using templates or auto-detection',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        template: {
          type: 'string',
          description: 'Template name (google_search, wikipedia, amazon_product, github_repo, twitter_feed, reddit_thread) or empty for auto-detect',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_detect_site',
    description: 'Detect website type for template matching',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_list_templates',
    description: 'List all available extraction templates',
    inputSchema: {
      type: 'object',
      properties: {},
    },
  },
  // ============================================================
  // AI Intelligence Tools (On-Device LLM)
  // ============================================================
  {
    name: 'browser_summarize_page',
    description: 'NEW: Create an intelligent, structured summary of the current page using LLM. The summary is cached per URL for fast repeat access. Much better than raw text extraction - provides structured information about page topic, key facts, interactive elements, and content structure.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        force_refresh: {
          type: 'boolean',
          description: 'Force refresh the summary (ignore cache). Default: false',
          default: false,
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_query_page',
    description: 'Ask a natural language question about the current page using on-device LLM. NOW IMPROVED: Uses intelligent page summarization for better answers! Examples: "What is the main topic?", "Does this page have pricing?", "What products are shown?", "Extract all email addresses"',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        query: {
          type: 'string',
          description: 'The question to ask about the page content',
        },
      },
      required: ['context_id', 'query'],
    },
  },
  {
    name: 'browser_llm_status',
    description: 'Check if the on-device LLM is ready to use. Returns status: "ready", "loading", or "unavailable"',
    inputSchema: {
      type: 'object',
      properties: {},
    },
  },
  {
    name: 'browser_nla',
    description: 'POWERFUL: Execute natural language commands using on-device LLM. Examples: "go to google.com and search for banana", "click the first result", "visit reddit.com, find the top post, and take a screenshot". The LLM will plan and execute multi-step browser actions automatically.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        command: {
          type: 'string',
          description: 'Natural language command describing what to do',
        },
      },
      required: ['context_id', 'command'],
    },
  },
  // ============================================================
  // Browser Navigation & Control Tools
  // ============================================================
  {
    name: 'browser_reload',
    description: 'Reload/refresh the current page. By default waits for page load.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        ignore_cache: {
          type: 'boolean',
          description: 'If true, bypass cache (hard reload)',
          default: false,
        },
        wait_until: {
          type: 'string',
          description: 'When to consider reload complete. Options: "" (return immediately), "load" (wait for load event), "domcontentloaded", "networkidle"',
          enum: ['', 'load', 'domcontentloaded', 'networkidle'],
          default: 'load',
        },
        timeout: {
          type: 'number',
          description: 'Maximum time to wait for reload in milliseconds (default: 30000)',
          default: 30000,
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_go_back',
    description: 'Navigate back in browser history. By default waits for page load.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        wait_until: {
          type: 'string',
          description: 'When to consider navigation complete. Options: "" (return immediately), "load" (wait for load event), "domcontentloaded", "networkidle"',
          enum: ['', 'load', 'domcontentloaded', 'networkidle'],
          default: 'load',
        },
        timeout: {
          type: 'number',
          description: 'Maximum time to wait for navigation in milliseconds (default: 30000)',
          default: 30000,
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_go_forward',
    description: 'Navigate forward in browser history. By default waits for page load.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        wait_until: {
          type: 'string',
          description: 'When to consider navigation complete. Options: "" (return immediately), "load" (wait for load event), "domcontentloaded", "networkidle"',
          enum: ['', 'load', 'domcontentloaded', 'networkidle'],
          default: 'load',
        },
        timeout: {
          type: 'number',
          description: 'Maximum time to wait for navigation in milliseconds (default: 30000)',
          default: 30000,
        },
      },
      required: ['context_id'],
    },
  },
  // ============================================================
  // Scroll Control Tools
  // ============================================================
  {
    name: 'browser_scroll_by',
    description: 'Scroll the page by specified pixels',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        x: {
          type: 'number',
          description: 'Horizontal scroll amount (pixels)',
          default: 0,
        },
        y: {
          type: 'number',
          description: 'Vertical scroll amount (pixels)',
        },
      },
      required: ['context_id', 'y'],
    },
  },
  {
    name: 'browser_scroll_to_element',
    description: 'Scroll element into view',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        selector: {
          type: 'string',
          description: 'CSS selector or semantic description',
        },
      },
      required: ['context_id', 'selector'],
    },
  },
  {
    name: 'browser_scroll_to_top',
    description: 'Scroll to top of page',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_scroll_to_bottom',
    description: 'Scroll to bottom of page',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  // ============================================================
  // Wait Utilities Tools
  // ============================================================
  {
    name: 'browser_wait_for_selector',
    description: 'Wait for an element to appear on the page',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        selector: {
          type: 'string',
          description: 'CSS selector or semantic description',
        },
        timeout: {
          type: 'number',
          description: 'Timeout in milliseconds',
          default: 5000,
        },
      },
      required: ['context_id', 'selector'],
    },
  },
  {
    name: 'browser_wait',
    description: 'Wait for specified milliseconds',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        timeout: {
          type: 'number',
          description: 'Time to wait in milliseconds',
        },
      },
      required: ['context_id', 'timeout'],
    },
  },
  {
    name: 'browser_wait_for_network_idle',
    description: 'Wait for network activity to become idle (no pending requests)',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        idle_time: {
          type: 'number',
          description: 'Network idle duration in milliseconds (default: 500)',
          default: 500,
        },
        timeout: {
          type: 'number',
          description: 'Timeout in milliseconds (default: 30000)',
          default: 30000,
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_wait_for_function',
    description: 'Wait for a JavaScript function to return a truthy value',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        js_function: {
          type: 'string',
          description: 'JavaScript function body that returns truthy when condition is met (e.g., "return document.querySelector(\'.loaded\') !== null")',
        },
        polling: {
          type: 'number',
          description: 'Polling interval in milliseconds (default: 100)',
          default: 100,
        },
        timeout: {
          type: 'number',
          description: 'Timeout in milliseconds (default: 30000)',
          default: 30000,
        },
      },
      required: ['context_id', 'js_function'],
    },
  },
  {
    name: 'browser_wait_for_url',
    description: 'Wait for the page URL to match a pattern',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        url_pattern: {
          type: 'string',
          description: 'URL pattern to match (substring or glob pattern with * and ?)',
        },
        is_regex: {
          type: 'boolean',
          description: 'Use glob-style pattern matching with wildcards (default: false, uses substring match)',
          default: false,
        },
        timeout: {
          type: 'number',
          description: 'Timeout in milliseconds (default: 30000)',
          default: 30000,
        },
      },
      required: ['context_id', 'url_pattern'],
    },
  },
  // ============================================================
  // Page State Query Tools
  // ============================================================
  {
    name: 'browser_get_page_info',
    description: 'Get current page information (URL, title, navigation state)',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  // ============================================================
  // Viewport Manipulation Tools
  // ============================================================
  {
    name: 'browser_set_viewport',
    description: 'Change browser viewport size for responsive testing',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        width: {
          type: 'number',
          description: 'Viewport width in pixels',
        },
        height: {
          type: 'number',
          description: 'Viewport height in pixels',
        },
      },
      required: ['context_id', 'width', 'height'],
    },
  },
  // ============================================================
  // Console Log Tools
  // ============================================================
  {
    name: 'browser_get_console_log',
    description: 'Read console logs from the browser. Returns logs with timestamp, level, message, and source information.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        level: {
          type: 'string',
          enum: ['debug', 'info', 'warn', 'error', 'verbose'],
          description: 'Filter by log level',
        },
        filter: {
          type: 'string',
          description: 'Filter logs containing specific text',
        },
        limit: {
          type: 'number',
          description: 'Maximum number of log entries to return (default: 100)',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_clear_console_log',
    description: 'Clear all console logs from the browser context.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  // ============================================================
  // DOM Zoom Tools
  // ============================================================
  {
    name: 'browser_zoom_in',
    description: 'Zoom in the page content by 10%. Uses CSS zoom property.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_zoom_out',
    description: 'Zoom out the page content by 10%. Uses CSS zoom property.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_zoom_reset',
    description: 'Reset page zoom to 100% (default).',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  // ============================================================
  // Video Recording Tools - NEW!
  // ============================================================
  {
    name: 'browser_start_video_recording',
    description: 'Start video recording of the browser session. Records all frames from the viewport to a video file.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        fps: {
          type: 'number',
          description: 'Frames per second (default: 30)',
          default: 30,
        },
        codec: {
          type: 'string',
          description: 'Video codec (default: "libx264")',
          default: 'libx264',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_pause_video_recording',
    description: 'Pause video recording without stopping it. Can be resumed later.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_resume_video_recording',
    description: 'Resume a paused video recording.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_stop_video_recording',
    description: 'Stop video recording and save the video file. Returns the path to the saved video in /tmp.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_get_video_recording_stats',
    description: 'Get statistics about the current video recording (frames captured, duration, etc.).',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  // ============================================================
  // Demographics and Context Tools
  // ============================================================
  {
    name: 'browser_get_demographics',
    description: 'Get user demographics and context (location, time, weather). Useful for location-aware searches like "find me a restaurant" or "book a hotel".',
    inputSchema: {
      type: 'object',
      properties: {},
    },
  },
  {
    name: 'browser_get_location',
    description: 'Get user\'s current location (city, country, coordinates) based on IP address.',
    inputSchema: {
      type: 'object',
      properties: {},
    },
  },
  {
    name: 'browser_get_datetime',
    description: 'Get current date and time information (date, time, day of week, timezone).',
    inputSchema: {
      type: 'object',
      properties: {},
    },
  },
  {
    name: 'browser_get_weather',
    description: 'Get current weather for the user\'s location.',
    inputSchema: {
      type: 'object',
      properties: {},
    },
  },
  {
    name: 'browser_detect_captcha',
    description: 'Detect if the current page has a CAPTCHA using heuristic analysis (no vision model). Returns detection result with confidence score.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_classify_captcha',
    description: 'Classify the type of CAPTCHA on the page (text-based, image-selection, checkbox, puzzle, audio, custom). Returns classification result with element selectors.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_solve_text_captcha',
    description: 'Solve a text-based CAPTCHA using the vision model to extract distorted text. Automatically enters the text and optionally submits.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        max_attempts: {
          type: 'number',
          description: 'Maximum number of attempts (default: 3)',
          default: 3,
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_solve_image_captcha',
    description: 'Solve an image-selection CAPTCHA (e.g., "select all images with traffic lights") using the vision model with numbered overlays for one-shot analysis.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        max_attempts: {
          type: 'number',
          description: 'Maximum number of attempts (default: 3)',
          default: 3,
        },
        provider: {
          type: 'string',
          description: 'CAPTCHA provider to use. Options: "auto" (auto-detect), "owl" (Owl internal test), "recaptcha" (Google reCAPTCHA), "cloudflare" (Cloudflare Turnstile/hCaptcha). Default: "auto"',
          enum: ['auto', 'owl', 'recaptcha', 'cloudflare', 'hcaptcha'],
          default: 'auto',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_solve_captcha',
    description: 'Auto-detect and solve any supported CAPTCHA type (text-based or image-selection). Automatically detects, classifies, and solves the CAPTCHA.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        max_attempts: {
          type: 'number',
          description: 'Maximum number of attempts (default: 3)',
          default: 3,
        },
        provider: {
          type: 'string',
          description: 'CAPTCHA provider to use for image CAPTCHAs. Options: "auto" (auto-detect), "owl" (Owl internal test), "recaptcha" (Google reCAPTCHA), "cloudflare" (Cloudflare Turnstile/hCaptcha). Default: "auto"',
          enum: ['auto', 'owl', 'recaptcha', 'cloudflare', 'hcaptcha'],
          default: 'auto',
        },
      },
      required: ['context_id'],
    },
  },
  // Cookie Management Tools
  {
    name: 'browser_get_cookies',
    description: 'Get all cookies from the browser context. Returns a JSON array of cookies with all attributes including httpOnly, secure, sameSite, and expiration. Can filter by URL to get only relevant cookies.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        url: {
          type: 'string',
          description: 'Optional URL to filter cookies. If empty, returns all cookies.',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_set_cookie',
    description: 'Set a cookie in the browser context with full control over all cookie attributes including domain, path, secure, httpOnly, sameSite, and expiration.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        url: {
          type: 'string',
          description: 'The URL to associate with the cookie (used for domain validation)',
        },
        name: {
          type: 'string',
          description: 'The cookie name',
        },
        value: {
          type: 'string',
          description: 'The cookie value',
        },
        domain: {
          type: 'string',
          description: 'The cookie domain. If empty, a host cookie will be created. Domain cookies (with leading ".") are visible to subdomains.',
        },
        path: {
          type: 'string',
          description: 'The cookie path (default: "/")',
          default: '/',
        },
        secure: {
          type: 'boolean',
          description: 'If true, cookie will only be sent over HTTPS (default: false)',
          default: false,
        },
        httpOnly: {
          type: 'boolean',
          description: 'If true, cookie is not accessible via JavaScript (default: false)',
          default: false,
        },
        sameSite: {
          type: 'string',
          description: 'SameSite attribute: "none", "lax", or "strict" (default: "lax")',
          enum: ['none', 'lax', 'strict'],
          default: 'lax',
        },
        expires: {
          type: 'number',
          description: 'Unix timestamp for cookie expiration. If -1 or not provided, creates a session cookie.',
          default: -1,
        },
      },
      required: ['context_id', 'url', 'name', 'value'],
    },
  },
  {
    name: 'browser_delete_cookies',
    description: 'Delete cookies from the browser context. Can delete all cookies, cookies for a specific URL, or a specific cookie by name.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        url: {
          type: 'string',
          description: 'Optional URL to filter which cookies to delete. If empty with empty cookie_name, deletes all cookies.',
        },
        cookie_name: {
          type: 'string',
          description: 'Optional specific cookie name to delete. If provided with url, deletes only that cookie.',
        },
      },
      required: ['context_id'],
    },
  },
  // ==================== PROXY MANAGEMENT TOOLS ====================
  {
    name: 'browser_set_proxy',
    description: 'Configure proxy settings for a browser context. Includes stealth features to prevent proxy/VPN detection.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        type: {
          type: 'string',
          enum: ['http', 'https', 'socks4', 'socks5', 'socks5h'],
          description: 'Proxy type. "socks5h" uses remote DNS resolution for maximum stealth.',
        },
        host: {
          type: 'string',
          description: 'Proxy server hostname or IP address',
        },
        port: {
          type: 'number',
          description: 'Proxy server port',
        },
        username: {
          type: 'string',
          description: 'Authentication username (optional)',
        },
        password: {
          type: 'string',
          description: 'Authentication password (optional)',
        },
        stealth: {
          type: 'boolean',
          description: 'Enable stealth mode - blocks WebRTC IP leaks and other detection vectors (default: true)',
        },
        block_webrtc: {
          type: 'boolean',
          description: 'Block WebRTC to prevent real IP exposure (default: true)',
        },
        spoof_timezone: {
          type: 'boolean',
          description: 'Spoof timezone to match proxy location (default: false)',
        },
        spoof_language: {
          type: 'boolean',
          description: 'Spoof browser language to match proxy location (default: false)',
        },
        timezone_override: {
          type: 'string',
          description: 'Manual timezone override (e.g., "America/New_York")',
        },
        language_override: {
          type: 'string',
          description: 'Manual language override (e.g., "en-US")',
        },
      },
      required: ['context_id', 'type', 'host', 'port'],
    },
  },
  {
    name: 'browser_get_proxy_status',
    description: 'Get current proxy configuration and connection status for a browser context.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_connect_proxy',
    description: 'Enable/connect the configured proxy for a browser context.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_disconnect_proxy',
    description: 'Disable/disconnect the proxy for a browser context, reverting to direct connection.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  // Profile Management Tools
  {
    name: 'browser_create_profile',
    description: 'Create a new browser profile with randomized fingerprints. Returns the profile JSON. The profile can be saved to a file using browser_save_profile.',
    inputSchema: {
      type: 'object',
      properties: {
        name: {
          type: 'string',
          description: 'A human-readable name for the profile (optional)',
        },
      },
    },
  },
  {
    name: 'browser_load_profile',
    description: 'Load a browser profile from a JSON file into an existing context. This applies the fingerprints, cookies, and settings from the profile.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID to load the profile into',
        },
        profile_path: {
          type: 'string',
          description: 'Path to the profile JSON file',
        },
      },
      required: ['context_id', 'profile_path'],
    },
  },
  {
    name: 'browser_save_profile',
    description: 'Save the current context state (fingerprints, cookies, settings) to a profile JSON file. This captures the current cookies for session persistence.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID to save',
        },
        profile_path: {
          type: 'string',
          description: 'Path to save the profile JSON file (optional if profile was previously loaded)',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_get_profile',
    description: 'Get the current profile state for a context as JSON. Includes fingerprints, current cookies, and settings.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_update_profile_cookies',
    description: 'Update the profile file with the current cookies from the browser. Useful for saving session state after login.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_get_context_info',
    description: 'Get context information including the VM profile and fingerprint hashes (canvas, audio, GPU) currently in use. Returns the stealth configuration for the browser context.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  // ==================== ADVANCED MOUSE INTERACTIONS ====================
  {
    name: 'browser_hover',
    description: 'Hover over an element without clicking. Useful for revealing tooltips, dropdown menus, or triggering hover effects.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        selector: {
          type: 'string',
          description: 'CSS selector, position coordinates (e.g., "100x200"), or natural language description',
        },
      },
      required: ['context_id', 'selector'],
    },
  },
  {
    name: 'browser_double_click',
    description: 'Double-click an element. Useful for selecting text, opening items, or editing interfaces.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        selector: {
          type: 'string',
          description: 'CSS selector, position coordinates (e.g., "100x200"), or natural language description',
        },
      },
      required: ['context_id', 'selector'],
    },
  },
  {
    name: 'browser_right_click',
    description: 'Right-click an element to open context menu.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        selector: {
          type: 'string',
          description: 'CSS selector, position coordinates (e.g., "100x200"), or natural language description',
        },
      },
      required: ['context_id', 'selector'],
    },
  },
  // ==================== INPUT CONTROL ====================
  {
    name: 'browser_clear_input',
    description: 'Clear the text content of an input field.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        selector: {
          type: 'string',
          description: 'CSS selector or natural language description for the input element',
        },
      },
      required: ['context_id', 'selector'],
    },
  },
  {
    name: 'browser_focus',
    description: 'Focus on an element without clicking.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        selector: {
          type: 'string',
          description: 'CSS selector or natural language description',
        },
      },
      required: ['context_id', 'selector'],
    },
  },
  {
    name: 'browser_blur',
    description: 'Remove focus from an element (blur). Useful for triggering validation.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        selector: {
          type: 'string',
          description: 'CSS selector or natural language description',
        },
      },
      required: ['context_id', 'selector'],
    },
  },
  {
    name: 'browser_select_all',
    description: 'Select all text in an input element.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        selector: {
          type: 'string',
          description: 'CSS selector or natural language description',
        },
      },
      required: ['context_id', 'selector'],
    },
  },
  // ==================== KEYBOARD COMBINATIONS ====================
  {
    name: 'browser_keyboard_combo',
    description: 'Press a keyboard combination. Supports Ctrl, Shift, Alt, Meta/Cmd modifiers. Examples: "Ctrl+A", "Ctrl+Shift+N", "Meta+V".',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        combo: {
          type: 'string',
          description: 'Key combination string (e.g., "Ctrl+A", "Shift+Enter", "Ctrl+Shift+N")',
        },
      },
      required: ['context_id', 'combo'],
    },
  },
  // ==================== JAVASCRIPT EVALUATION ====================
  {
    name: 'browser_evaluate',
    description: 'Execute JavaScript code in the page context. Use return_value=true when you need to get the result of an expression (e.g., document.title, element.value). Use return_value=false (default) when executing statements that don\'t return values. Alternatively, use the "expression" parameter for a shorthand that automatically returns the value.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        script: {
          type: 'string',
          description: 'JavaScript code to execute (optional if expression is provided)',
        },
        expression: {
          type: 'string',
          description: 'JavaScript expression to evaluate and return (shorthand for script with return_value=true). If provided, script is optional and return_value is automatically true.',
        },
        return_value: {
          type: 'boolean',
          description: 'If true, treats the script as an expression and returns its value (e.g., "document.title" returns the title). If false (default), executes the script as statements. Automatically set to true when using expression parameter.',
          default: false,
        },
      },
      required: ['context_id'],
    },
  },
  // ==================== ELEMENT STATE CHECKS ====================
  {
    name: 'browser_is_visible',
    description: 'Check if an element is visible on the page.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        selector: {
          type: 'string',
          description: 'CSS selector or natural language description',
        },
      },
      required: ['context_id', 'selector'],
    },
  },
  {
    name: 'browser_is_enabled',
    description: 'Check if an element is enabled (not disabled).',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        selector: {
          type: 'string',
          description: 'CSS selector or natural language description',
        },
      },
      required: ['context_id', 'selector'],
    },
  },
  {
    name: 'browser_is_checked',
    description: 'Check if a checkbox or radio button is checked.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        selector: {
          type: 'string',
          description: 'CSS selector or natural language description',
        },
      },
      required: ['context_id', 'selector'],
    },
  },
  {
    name: 'browser_get_attribute',
    description: 'Get the value of an attribute from an element.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        selector: {
          type: 'string',
          description: 'CSS selector or natural language description',
        },
        attribute: {
          type: 'string',
          description: 'Attribute name (e.g., "href", "src", "value", "data-id")',
        },
      },
      required: ['context_id', 'selector', 'attribute'],
    },
  },
  {
    name: 'browser_get_bounding_box',
    description: 'Get the position and size of an element. Returns {x, y, width, height}.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        selector: {
          type: 'string',
          description: 'CSS selector or natural language description',
        },
      },
      required: ['context_id', 'selector'],
    },
  },
  // ==================== FILE OPERATIONS ====================
  {
    name: 'browser_upload_file',
    description: 'Upload files to a file input element.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        selector: {
          type: 'string',
          description: 'CSS selector for the file input element',
        },
        file_paths: {
          type: 'array',
          items: { type: 'string' },
          description: 'Array of absolute file paths to upload',
        },
      },
      required: ['context_id', 'selector', 'file_paths'],
    },
  },
  // ==================== FRAME/IFRAME HANDLING ====================
  {
    name: 'browser_list_frames',
    description: 'List all frames (including iframes) in the page.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_switch_to_frame',
    description: 'Switch context to an iframe for interaction.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        frame_selector: {
          type: 'string',
          description: 'Frame name, frame index (e.g., "0", "1"), or CSS selector for iframe',
        },
      },
      required: ['context_id', 'frame_selector'],
    },
  },
  {
    name: 'browser_switch_to_main_frame',
    description: 'Switch back to the main frame from an iframe.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  // ==================== NETWORK INTERCEPTION ====================
  {
    name: 'browser_add_network_rule',
    description: 'Add a network interception rule to block, mock, or redirect requests. Supports glob patterns or regex.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        url_pattern: {
          type: 'string',
          description: 'URL pattern to match (glob or regex)',
        },
        action: {
          type: 'string',
          enum: ['allow', 'block', 'mock', 'redirect'],
          description: 'Action to take: allow, block, mock, or redirect',
        },
        is_regex: {
          type: 'boolean',
          description: 'Whether url_pattern is a regex (default: false for glob)',
          default: false,
        },
        redirect_url: {
          type: 'string',
          description: 'URL to redirect to (for redirect action)',
        },
        mock_body: {
          type: 'string',
          description: 'Response body to return (for mock action)',
        },
        mock_status: {
          type: 'number',
          description: 'HTTP status code for mock response (default: 200)',
          default: 200,
        },
        mock_content_type: {
          type: 'string',
          description: 'Content-Type header for mock response',
          default: 'text/plain',
        },
      },
      required: ['context_id', 'url_pattern', 'action'],
    },
  },
  {
    name: 'browser_remove_network_rule',
    description: 'Remove a previously added network interception rule.',
    inputSchema: {
      type: 'object',
      properties: {
        rule_id: {
          type: 'string',
          description: 'The rule ID to remove',
        },
      },
      required: ['rule_id'],
    },
  },
  {
    name: 'browser_enable_network_interception',
    description: 'Enable or disable network interception for a context.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        enable: {
          type: 'boolean',
          description: 'Enable or disable interception',
        },
      },
      required: ['context_id', 'enable'],
    },
  },
  {
    name: 'browser_get_network_log',
    description: 'Get captured network requests and responses for a context.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_clear_network_log',
    description: 'Clear the captured network log for a context.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  // ==================== DOWNLOAD MANAGEMENT ====================
  {
    name: 'browser_set_download_path',
    description: 'Set the download directory for automatic file downloads.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        path: {
          type: 'string',
          description: 'Directory path for downloads',
        },
      },
      required: ['context_id', 'path'],
    },
  },
  {
    name: 'browser_get_downloads',
    description: 'Get all downloads for a context with their status.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_wait_for_download',
    description: 'Wait for a download to complete.',
    inputSchema: {
      type: 'object',
      properties: {
        download_id: {
          type: 'string',
          description: 'The download ID to wait for',
        },
        timeout: {
          type: 'number',
          description: 'Timeout in milliseconds (default: 30000)',
          default: 30000,
        },
      },
      required: ['download_id'],
    },
  },
  {
    name: 'browser_cancel_download',
    description: 'Cancel an in-progress download.',
    inputSchema: {
      type: 'object',
      properties: {
        download_id: {
          type: 'string',
          description: 'The download ID to cancel',
        },
      },
      required: ['download_id'],
    },
  },
  // ==================== DIALOG HANDLING ====================
  {
    name: 'browser_set_dialog_action',
    description: 'Set automatic action for JavaScript dialogs (alert, confirm, prompt, beforeunload).',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        dialog_type: {
          type: 'string',
          enum: ['alert', 'confirm', 'prompt', 'beforeunload'],
          description: 'Type of dialog to configure',
        },
        action: {
          type: 'string',
          enum: ['accept', 'dismiss', 'accept_with_text'],
          description: 'Action to take when dialog appears',
        },
        prompt_text: {
          type: 'string',
          description: 'Default text for prompt dialogs',
        },
      },
      required: ['context_id', 'dialog_type', 'action'],
    },
  },
  {
    name: 'browser_get_pending_dialog',
    description: 'Get information about the current pending dialog.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_handle_dialog',
    description: 'Handle a specific dialog by accepting or dismissing it.',
    inputSchema: {
      type: 'object',
      properties: {
        dialog_id: {
          type: 'string',
          description: 'The dialog ID to handle',
        },
        accept: {
          type: 'boolean',
          description: 'Accept (true) or dismiss (false) the dialog',
        },
        response_text: {
          type: 'string',
          description: 'Response text for prompt dialogs',
        },
      },
      required: ['dialog_id', 'accept'],
    },
  },
  {
    name: 'browser_wait_for_dialog',
    description: 'Wait for a dialog to appear.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        timeout: {
          type: 'number',
          description: 'Timeout in milliseconds (default: 5000)',
          default: 5000,
        },
      },
      required: ['context_id'],
    },
  },
  // ==================== TAB/WINDOW MANAGEMENT ====================
  {
    name: 'browser_set_popup_policy',
    description: 'Set how popups are handled for a context.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        policy: {
          type: 'string',
          enum: ['allow', 'block', 'new_tab', 'background'],
          description: 'Popup policy: allow (new window), block, new_tab (foreground), or background',
        },
      },
      required: ['context_id', 'policy'],
    },
  },
  {
    name: 'browser_get_tabs',
    description: 'Get all tabs/windows for a context.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_switch_tab',
    description: 'Switch to a specific tab.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        tab_id: {
          type: 'string',
          description: 'The tab ID to switch to',
        },
      },
      required: ['context_id', 'tab_id'],
    },
  },
  {
    name: 'browser_close_tab',
    description: 'Close a specific tab.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        tab_id: {
          type: 'string',
          description: 'The tab ID to close',
        },
      },
      required: ['context_id', 'tab_id'],
    },
  },
  {
    name: 'browser_new_tab',
    description: 'Open a new tab, optionally navigating to a URL.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        url: {
          type: 'string',
          description: 'Optional URL to navigate to',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_get_active_tab',
    description: 'Get the currently active tab.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_get_tab_count',
    description: 'Get the number of open tabs.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_get_blocked_popups',
    description: 'Get list of blocked popup URLs.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  // ==================== CLIPBOARD MANAGEMENT ====================
  {
    name: 'browser_clipboard_read',
    description: 'Read text content from the system clipboard. Useful for getting content that was copied from the browser.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
  {
    name: 'browser_clipboard_write',
    description: 'Write text content to the system clipboard. Useful for preparing content to paste into the browser.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
        text: {
          type: 'string',
          description: 'Text content to write to the clipboard',
        },
      },
      required: ['context_id', 'text'],
    },
  },
  {
    name: 'browser_clipboard_clear',
    description: 'Clear the system clipboard content.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: {
          type: 'string',
          description: 'The context ID',
        },
      },
      required: ['context_id'],
    },
  },
];

// List tools handler
server.setRequestHandler(ListToolsRequestSchema, async () => {
  return { tools };
});

// Call tool handler
server.setRequestHandler(CallToolRequestSchema, async (request) => {
  const { name, arguments: args } = request.params;

  try {
    // Ensure browser is initialized for all operations
    if (!browserWrapper) {
      browserWrapper = new BrowserWrapper();
      await browserWrapper.initialize();
      console.error('Browser initialized');
    }

    switch (name) {
      case 'browser_create_context': {
        // Extract LLM configuration if provided
        const config = {
          llm_enabled: args.llm_enabled,
          llm_use_builtin: args.llm_use_builtin,
          llm_endpoint: args.llm_endpoint,
          llm_model: args.llm_model,
          llm_api_key: args.llm_api_key,
        };

        // Extract proxy configuration if provided
        if (args.proxy_host && args.proxy_port) {
          config.proxy = {
            type: args.proxy_type || 'http',
            host: args.proxy_host,
            port: args.proxy_port,
            username: args.proxy_username,
            password: args.proxy_password,
            enabled: true,
            stealth: args.proxy_stealth !== false, // Default to true
            blockWebrtc: true, // Always block WebRTC when using proxy
            // CA certificate for SSL interception proxies (Charles, mitmproxy, etc.)
            caCertPath: args.proxy_ca_cert_path || '',
            trustCustomCa: args.proxy_trust_custom_ca || false,
            // Tor-specific settings for circuit isolation
            // Each context gets a new Tor circuit (exit node) automatically
            isTor: args.is_tor || false,
            torControlPort: args.tor_control_port !== undefined ? args.tor_control_port : 0, // 0 = auto-detect
            torControlPassword: args.tor_control_password || '',
          };
        }

        // Profile configuration
        if (args.profile_path) {
          config.profile_path = args.profile_path;
        }

        // Resource blocking configuration (default: true)
        config.resource_blocking = args.resource_blocking !== false;

        // Profile filtering options
        if (args.os) {
          config.os = args.os;
        }
        if (args.gpu) {
          config.gpu = args.gpu;
        }

        // Browser now returns full context info directly (with context_id included)
        const result = await browserWrapper.createContext(config);
        const contextInfo = typeof result === 'string' ? JSON.parse(result) : result;
        const context_id = contextInfo.context_id;

        activeContexts.set(context_id, { created: Date.now(), url: null, proxy: config.proxy, profile_path: config.profile_path });

        let statusMsg = `Context created: ${context_id}`;
        if (config.llm_endpoint) {
          statusMsg += ` (with external LLM: ${config.llm_endpoint})`;
        } else if (config.llm_enabled === false) {
          statusMsg += ` (LLM disabled)`;
        }
        if (config.proxy) {
          statusMsg += ` (proxy: ${config.proxy.type}://${config.proxy.host}:${config.proxy.port})`;
        }
        if (config.profile_path) {
          statusMsg += ` (profile: ${config.profile_path})`;
        }
        console.error(statusMsg);

        return {
          content: [
            {
              type: 'text',
              text: JSON.stringify(contextInfo, null, 2),
            },
          ],
        };
      }

      case 'browser_close_context': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.releaseContext(context_id);
        activeContexts.delete(context_id);
        console.error(`Context closed: ${context_id}`);
        return {
          content: [
            {
              type: 'text',
              text: `Context closed: ${context_id}`,
            },
          ],
        };
      }

      case 'browser_list_contexts': {
        // Get contexts from browser (authoritative source)
        const browserContexts = await browserWrapper.listContexts();

        // Merge with local metadata where available
        const contexts = browserContexts.map(id => {
          const meta = activeContexts.get(id);
          return {
            context_id: id,
            url: meta?.url || '(unknown)',
            created: meta?.created ? new Date(meta.created).toISOString() : '(unknown)',
            created_timestamp: meta?.created || 0,
          };
        });

        // Sort by creation time (oldest first), falling back to context_id
        contexts.sort((a, b) => {
          if (a.created_timestamp !== b.created_timestamp) {
            return a.created_timestamp - b.created_timestamp;
          }
          return a.context_id.localeCompare(b.context_id);
        });

        // Remove internal timestamp before returning
        contexts.forEach(c => delete c.created_timestamp);

        // Sync local cache with browser state
        for (const id of browserContexts) {
          if (!activeContexts.has(id)) {
            activeContexts.set(id, { created: Date.now(), url: null });
          }
        }
        // Remove stale entries from local cache
        for (const id of activeContexts.keys()) {
          if (!browserContexts.includes(id)) {
            activeContexts.delete(id);
          }
        }

        return {
          content: [
            {
              type: 'text',
              text: contexts.length > 0
                ? `Active contexts:\n${contexts.map(c => `  ${c.context_id}: ${c.url}`).join('\n')}`
                : 'No active contexts',
            },
          ],
        };
      }

      case 'browser_navigate': {
        const { context_id, url, wait_until = '', timeout = 30000 } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}. Use browser_create_context first.`);
        }
        await browserWrapper.navigate(context_id, url, wait_until, timeout);
        activeContexts.get(context_id).url = url;
        const waitMsg = wait_until ? ` (waited for ${wait_until})` : '';
        return {
          content: [
            {
              type: 'text',
              text: `Successfully navigated to ${url}${waitMsg}`,
            },
          ],
        };
      }

      case 'browser_click': {
        const { context_id, selector } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.click(context_id, selector);
        return formatActionResult(result, `Clicked element: ${selector}`);
      }

      case 'browser_drag_drop': {
        const { context_id, start_x, start_y, end_x, end_y, mid_points } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.dragDrop(context_id, start_x, start_y, end_x, end_y, mid_points || []);
        const waypointCount = mid_points ? mid_points.length : 0;
        return {
          content: [
            {
              type: 'text',
              text: `Successfully dragged from (${start_x}, ${start_y}) to (${end_x}, ${end_y})${waypointCount > 0 ? ` through ${waypointCount} waypoints` : ''}`,
            },
          ],
        };
      }

      case 'browser_html5_drag_drop': {
        const { context_id, source_selector, target_selector } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.html5DragDrop(context_id, source_selector, target_selector);
        return {
          content: [
            {
              type: 'text',
              text: `Successfully performed HTML5 drag from "${source_selector}" to "${target_selector}"`,
            },
          ],
        };
      }

      case 'browser_mouse_move': {
        const { context_id, start_x, start_y, end_x, end_y, steps, stop_points } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.mouseMove(context_id, start_x, start_y, end_x, end_y, steps || 0, stop_points || []);
        const stopCount = stop_points ? stop_points.length : 0;
        return {
          content: [
            {
              type: 'text',
              text: `Successfully moved mouse from (${start_x}, ${start_y}) to (${end_x}, ${end_y}) along curved path${stopCount > 0 ? ` with ${stopCount} stop points` : ''}`,
            },
          ],
        };
      }

      case 'browser_type': {
        const { context_id, selector, text } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.type(context_id, selector, text);
        return formatActionResult(result, `Typed "${text}" into ${selector}`);
      }

      case 'browser_pick': {
        const { context_id, selector, value } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.pick(context_id, selector, value);
        return formatActionResult(result, `Selected "${value}" from ${selector}`);
      }

      case 'browser_press_key': {
        const { context_id, key } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.pressKey(context_id, key);
        return {
          content: [
            {
              type: 'text',
              text: `Successfully pressed key: ${key}`,
            },
          ],
        };
      }

      case 'browser_submit_form': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.submitForm(context_id);
        return {
          content: [
            {
              type: 'text',
              text: 'Successfully submitted form',
            },
          ],
        };
      }

      case 'browser_extract_text': {
        const { context_id, selector } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const text = await browserWrapper.extractText(context_id, selector || 'body');
        return {
          content: [
            {
              type: 'text',
              text: text || '(empty)',
            },
          ],
        };
      }

      case 'browser_screenshot': {
        const { context_id, mode, selector } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        // Validate element mode requires selector
        if (mode === 'element' && !selector) {
          throw new Error('Element screenshot mode requires a selector');
        }
        const png = await browserWrapper.screenshot(context_id, mode, selector);
        return {
          content: [
            {
              type: 'image',
              data: png.toString('base64'),
              mimeType: 'image/png',
            },
          ],
        };
      }

      case 'browser_highlight': {
        const { context_id, selector, border_color, background_color } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.highlight(
          context_id,
          selector,
          border_color || '#FF0000',
          background_color || 'rgba(255, 0, 0, 0.2)'
        );
        return {
          content: [
            {
              type: 'text',
              text: `Successfully highlighted element: ${selector}`,
            },
          ],
        };
      }

      case 'browser_show_grid_overlay': {
        const { context_id, horizontal_lines, vertical_lines, line_color, text_color } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.showGridOverlay(
          context_id,
          horizontal_lines || 25,
          vertical_lines || 25,
          line_color || 'rgba(255, 0, 0, 0.15)',
          text_color || 'rgba(255, 0, 0, 0.4)'
        );
        return {
          content: [
            {
              type: 'text',
              text: `Successfully showed grid overlay with ${horizontal_lines || 25}x${vertical_lines || 25} lines`,
            },
          ],
        };
      }

      case 'browser_get_html': {
        const { context_id, clean_level } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const html = await browserWrapper.getHTML(context_id, clean_level || 'basic');
        return {
          content: [
            {
              type: 'text',
              text: html,
            },
          ],
        };
      }

      case 'browser_get_markdown': {
        const { context_id, include_links, include_images, max_length } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const markdown = await browserWrapper.getMarkdown(
          context_id,
          include_links !== false,
          include_images !== false,
          max_length || -1
        );
        return {
          content: [
            {
              type: 'text',
              text: markdown,
            },
          ],
        };
      }

      case 'browser_extract_json': {
        const { context_id, template } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const jsonData = await browserWrapper.extractJSON(context_id, template || '');
        return {
          content: [
            {
              type: 'text',
              text: JSON.stringify(jsonData, null, 2),
            },
          ],
        };
      }

      case 'browser_summarize_page': {
        const { context_id, force_refresh = false } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const summary = await browserWrapper.summarizePage(context_id, force_refresh);
        return {
          content: [
            {
              type: 'text',
              text: summary,
            },
          ],
        };
      }

      case 'browser_query_page': {
        const { context_id, query } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const answer = await browserWrapper.queryPage(context_id, query);
        return {
          content: [
            {
              type: 'text',
              text: answer,
            },
          ],
        };
      }

      case 'browser_llm_status': {
        const status = await browserWrapper.getLLMStatus();
        return {
          content: [
            {
              type: 'text',
              text: status,
            },
          ],
        };
      }

      case 'browser_nla': {
        const { context_id, command } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.executeNLA(context_id, command);
        return {
          content: [
            {
              type: 'text',
              text: result,
            },
          ],
        };
      }

      case 'browser_detect_site': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const siteType = await browserWrapper.detectWebsiteType(context_id);
        return {
          content: [
            {
              type: 'text',
              text: siteType,
            },
          ],
        };
      }

      case 'browser_list_templates': {
        const templates = await browserWrapper.listTemplates();
        return {
          content: [
            {
              type: 'text',
              text: JSON.stringify(templates, null, 2),
            },
          ],
        };
      }

      // ============================================================
      // Browser Navigation & Control Cases
      // ============================================================

      case 'browser_reload': {
        const { context_id, ignore_cache = false, wait_until = 'load', timeout = 30000 } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.reload(context_id, ignore_cache, wait_until, timeout);
        return {
          content: [
            {
              type: 'text',
              text: `Successfully reloaded page${ignore_cache ? ' (cache bypassed)' : ''}`,
            },
          ],
        };
      }

      case 'browser_go_back': {
        const { context_id, wait_until = 'load', timeout = 30000 } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.goBack(context_id, wait_until, timeout);
        return {
          content: [
            {
              type: 'text',
              text: 'Successfully navigated back',
            },
          ],
        };
      }

      case 'browser_go_forward': {
        const { context_id, wait_until = 'load', timeout = 30000 } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.goForward(context_id, wait_until, timeout);
        return {
          content: [
            {
              type: 'text',
              text: 'Successfully navigated forward',
            },
          ],
        };
      }

      // ============================================================
      // Scroll Control Cases
      // ============================================================

      case 'browser_scroll_by': {
        const { context_id, x = 0, y } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.scrollBy(context_id, x, y);
        return {
          content: [
            {
              type: 'text',
              text: `Successfully scrolled by x=${x}, y=${y}`,
            },
          ],
        };
      }

      case 'browser_scroll_to_element': {
        const { context_id, selector } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.scrollToElement(context_id, selector);
        return {
          content: [
            {
              type: 'text',
              text: `Successfully scrolled to element: ${selector}`,
            },
          ],
        };
      }

      case 'browser_scroll_to_top': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.scrollToTop(context_id);
        return {
          content: [
            {
              type: 'text',
              text: 'Successfully scrolled to top',
            },
          ],
        };
      }

      case 'browser_scroll_to_bottom': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.scrollToBottom(context_id);
        return {
          content: [
            {
              type: 'text',
              text: 'Successfully scrolled to bottom',
            },
          ],
        };
      }

      // ============================================================
      // Wait Utilities Cases
      // ============================================================

      case 'browser_wait_for_selector': {
        const { context_id, selector, timeout = 5000 } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.waitForSelector(context_id, selector, timeout);
        return {
          content: [
            {
              type: 'text',
              text: `Element found: ${selector}`,
            },
          ],
        };
      }

      case 'browser_wait': {
        const { context_id, timeout = 0 } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        if (timeout <= 0) {
          throw new Error('timeout must be a positive number');
        }
        await browserWrapper.waitForTimeout(context_id, timeout);
        return {
          content: [
            {
              type: 'text',
              text: `Waited for ${timeout}ms`,
            },
          ],
        };
      }

      case 'browser_wait_for_network_idle': {
        const { context_id, idle_time = 500, timeout = 30000 } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.waitForNetworkIdle(context_id, idle_time, timeout);
        return {
          content: [
            {
              type: 'text',
              text: `Network idle for ${idle_time}ms`,
            },
          ],
        };
      }

      case 'browser_wait_for_function': {
        const { context_id, js_function, polling = 100, timeout = 30000 } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.waitForFunction(context_id, js_function, polling, timeout);
        return {
          content: [
            {
              type: 'text',
              text: `Function returned truthy value`,
            },
          ],
        };
      }

      case 'browser_wait_for_url': {
        const { context_id, url_pattern, is_regex = false, timeout = 30000 } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.waitForURL(context_id, url_pattern, is_regex, timeout);
        const currentUrl = await browserWrapper.getCurrentURL(context_id);
        return {
          content: [
            {
              type: 'text',
              text: `URL matched pattern: ${url_pattern}\nCurrent URL: ${currentUrl}`,
            },
          ],
        };
      }

      // ============================================================
      // Page State Query Cases
      // ============================================================

      case 'browser_get_page_info': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const pageInfo = await browserWrapper.getPageInfo(context_id);
        return {
          content: [
            {
              type: 'text',
              text: JSON.stringify(pageInfo, null, 2),
            },
          ],
        };
      }

      // ============================================================
      // Viewport Manipulation Cases
      // ============================================================

      case 'browser_set_viewport': {
        const { context_id, width, height } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.setViewport(context_id, width, height);
        return {
          content: [
            {
              type: 'text',
              text: `Successfully set viewport to ${width}x${height}`,
            },
          ],
        };
      }

      // ============================================================
      // DOM Zoom Cases
      // ============================================================

      case 'browser_zoom_in': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        // Get current zoom and increase by 10%
        const zoomScript = `
          (function() {
            const currentZoom = parseFloat(document.body.style.zoom) || 1;
            const newZoom = Math.min(currentZoom + 0.1, 3); // Max 300%
            document.body.style.zoom = newZoom;
            return Math.round(newZoom * 100);
          })()
        `;
        const result = await browserWrapper.evaluate(context_id, zoomScript, true);
        return {
          content: [
            {
              type: 'text',
              text: `Zoomed in to ${result}%`,
            },
          ],
        };
      }

      case 'browser_zoom_out': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        // Get current zoom and decrease by 10%
        const zoomScript = `
          (function() {
            const currentZoom = parseFloat(document.body.style.zoom) || 1;
            const newZoom = Math.max(currentZoom - 0.1, 0.1); // Min 10%
            document.body.style.zoom = newZoom;
            return Math.round(newZoom * 100);
          })()
        `;
        const result = await browserWrapper.evaluate(context_id, zoomScript, true);
        return {
          content: [
            {
              type: 'text',
              text: `Zoomed out to ${result}%`,
            },
          ],
        };
      }

      case 'browser_zoom_reset': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        // Reset zoom to 100%
        const zoomScript = `document.body.style.zoom = 1; return 100;`;
        await browserWrapper.evaluate(context_id, zoomScript, true);
        return {
          content: [
            {
              type: 'text',
              text: `Zoom reset to 100%`,
            },
          ],
        };
      }

      // ============================================================
      // Console Log Cases
      // ============================================================

      case 'browser_get_console_log': {
        const { context_id, level, filter, limit } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const logs = await browserWrapper.getConsoleLogs(context_id, level, filter, limit);
        return {
          content: [
            {
              type: 'text',
              text: JSON.stringify(logs, null, 2),
            },
          ],
        };
      }

      case 'browser_clear_console_log': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.clearConsoleLogs(context_id);
        return {
          content: [
            {
              type: 'text',
              text: `Console logs cleared for context ${context_id}`,
            },
          ],
        };
      }

      // ============================================================
      // Video Recording Cases - NEW!
      // ============================================================

      case 'browser_start_video_recording': {
        const { context_id, fps = 30, codec = 'libx264' } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.startVideoRecording(context_id, fps, codec);
        if (result) {
          return {
            content: [
              {
                type: 'text',
                text: `Video recording started for context ${context_id} at ${fps}fps using ${codec}`,
              },
            ],
          };
        } else {
          throw new Error('Failed to start video recording');
        }
      }

      case 'browser_pause_video_recording': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.pauseVideoRecording(context_id);
        if (result) {
          return {
            content: [
              {
                type: 'text',
                text: `Video recording paused for context ${context_id}`,
              },
            ],
          };
        } else {
          throw new Error('Failed to pause video recording');
        }
      }

      case 'browser_resume_video_recording': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.resumeVideoRecording(context_id);
        if (result) {
          return {
            content: [
              {
                type: 'text',
                text: `Video recording resumed for context ${context_id}`,
              },
            ],
          };
        } else {
          throw new Error('Failed to resume video recording');
        }
      }

      case 'browser_stop_video_recording': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const videoPath = await browserWrapper.stopVideoRecording(context_id);
        if (videoPath) {
          return {
            content: [
              {
                type: 'text',
                text: `Video recording stopped. Video saved to: ${videoPath}`,
              },
            ],
          };
        } else {
          throw new Error('Failed to stop video recording or no recording in progress');
        }
      }

      case 'browser_get_video_recording_stats': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const stats = await browserWrapper.getVideoRecordingStats(context_id);
        return {
          content: [
            {
              type: 'text',
              text: `Video recording stats:\n${stats}`,
            },
          ],
        };
      }

      // ============================================================
      // Demographics and Context Tools
      // ============================================================

      case 'browser_get_demographics': {
        const demographics = await browserWrapper.getDemographics();
        return {
          content: [
            {
              type: 'text',
              text: demographics,
            },
          ],
        };
      }

      case 'browser_get_location': {
        const location = await browserWrapper.getLocation();
        return {
          content: [
            {
              type: 'text',
              text: location,
            },
          ],
        };
      }

      case 'browser_get_datetime': {
        const datetime = await browserWrapper.getDateTime();
        return {
          content: [
            {
              type: 'text',
              text: datetime,
            },
          ],
        };
      }

      case 'browser_get_weather': {
        const weather = await browserWrapper.getWeather();
        return {
          content: [
            {
              type: 'text',
              text: weather,
            },
          ],
        };
      }

      // CAPTCHA Handling
      case 'browser_detect_captcha': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.detectCaptcha(context_id);
        return {
          content: [
            {
              type: 'text',
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      }

      case 'browser_classify_captcha': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.classifyCaptcha(context_id);
        return {
          content: [
            {
              type: 'text',
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      }

      case 'browser_solve_text_captcha': {
        const { context_id, max_attempts = 3 } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        console.error(`Solving text CAPTCHA (max_attempts: ${max_attempts})`);
        const result = await browserWrapper.solveTextCaptcha(context_id, max_attempts);
        return {
          content: [
            {
              type: 'text',
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      }

      case 'browser_solve_image_captcha': {
        const { context_id, max_attempts = 3, provider = 'auto' } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        console.error(`Solving image CAPTCHA (max_attempts: ${max_attempts}, provider: ${provider})`);
        const result = await browserWrapper.solveImageCaptcha(context_id, max_attempts, provider);
        return {
          content: [
            {
              type: 'text',
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      }

      case 'browser_solve_captcha': {
        const { context_id, max_attempts = 3, provider = 'auto' } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        console.error(`Auto-solving CAPTCHA (max_attempts: ${max_attempts}, provider: ${provider})`);
        const result = await browserWrapper.solveCaptcha(context_id, max_attempts, provider);
        return {
          content: [
            {
              type: 'text',
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      }

      // Cookie Management Tools
      case 'browser_get_cookies': {
        const { context_id, url = '' } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const cookies = await browserWrapper.getCookies(context_id, url);
        return {
          content: [
            {
              type: 'text',
              text: JSON.stringify(cookies, null, 2),
            },
          ],
        };
      }

      case 'browser_set_cookie': {
        const { context_id, url, name, value, domain, path, secure, httpOnly, sameSite, expires } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.setCookie(context_id, url, name, value, {
          domain: domain || '',
          path: path || '/',
          secure: secure || false,
          httpOnly: httpOnly || false,
          sameSite: sameSite || 'lax',
          expires: expires || -1,
        });
        // result is ActionResult: {success: bool, message: string, ...}
        return {
          content: [
            {
              type: 'text',
              text: result.message || (result.success ? `Cookie "${name}" set successfully` : `Failed to set cookie "${name}"`),
            },
          ],
        };
      }

      case 'browser_delete_cookies': {
        const { context_id, url = '', cookie_name = '' } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.deleteCookies(context_id, url, cookie_name);
        // result is ActionResult: {success: bool, message: string, ...}
        return {
          content: [
            {
              type: 'text',
              text: result.message || (result.success ? 'Cookies deleted successfully' : 'Failed to delete cookies'),
            },
          ],
        };
      }

      // Proxy Management Tools
      case 'browser_set_proxy': {
        const { context_id, type, host, port, username, password, stealth, block_webrtc, spoof_timezone, spoof_language, timezone_override, language_override } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const success = await browserWrapper.setProxy(context_id, {
          type: type || 'http',
          host,
          port,
          username: username || '',
          password: password || '',
          enabled: true,
          stealth: stealth !== false,
          blockWebrtc: block_webrtc !== false,
          spoofTimezone: spoof_timezone || false,
          spoofLanguage: spoof_language || false,
          timezoneOverride: timezone_override || '',
          languageOverride: language_override || '',
        });
        // Update context metadata
        const ctx = activeContexts.get(context_id);
        if (ctx) {
          ctx.proxy = { type, host, port };
        }
        return {
          content: [
            {
              type: 'text',
              text: success ? `Proxy configured: ${type}://${host}:${port}${stealth !== false ? ' (stealth mode)' : ''}` : `Failed to configure proxy`,
            },
          ],
        };
      }

      case 'browser_get_proxy_status': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const status = await browserWrapper.getProxyStatus(context_id);
        return {
          content: [
            {
              type: 'text',
              text: typeof status === 'object' ? JSON.stringify(status, null, 2) : status,
            },
          ],
        };
      }

      case 'browser_connect_proxy': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const success = await browserWrapper.connectProxy(context_id);
        return {
          content: [
            {
              type: 'text',
              text: success ? 'Proxy connected successfully' : 'Failed to connect proxy',
            },
          ],
        };
      }

      case 'browser_disconnect_proxy': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const success = await browserWrapper.disconnectProxy(context_id);
        return {
          content: [
            {
              type: 'text',
              text: success ? 'Proxy disconnected successfully' : 'Failed to disconnect proxy',
            },
          ],
        };
      }

      // Profile Management Handlers
      case 'browser_create_profile': {
        const profileName = args.name || '';
        const profileJson = await browserWrapper.createProfile(profileName);
        return {
          content: [
            {
              type: 'text',
              text: profileJson,
            },
          ],
        };
      }

      case 'browser_load_profile': {
        const { context_id, profile_path } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const profileJson = await browserWrapper.loadProfile(context_id, profile_path);
        // Update tracked profile path
        const contextMeta = activeContexts.get(context_id);
        contextMeta.profile_path = profile_path;
        return {
          content: [
            {
              type: 'text',
              text: profileJson,
            },
          ],
        };
      }

      case 'browser_save_profile': {
        const { context_id, profile_path } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const profileJson = await browserWrapper.saveProfile(context_id, profile_path || '');
        // Update tracked profile path if saved to new location
        if (profile_path) {
          const contextMeta = activeContexts.get(context_id);
          contextMeta.profile_path = profile_path;
        }
        return {
          content: [
            {
              type: 'text',
              text: profileJson,
            },
          ],
        };
      }

      case 'browser_get_profile': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const profileJson = await browserWrapper.getProfile(context_id);
        return {
          content: [
            {
              type: 'text',
              text: profileJson,
            },
          ],
        };
      }

      case 'browser_update_profile_cookies': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const success = await browserWrapper.updateProfileCookies(context_id);
        return {
          content: [
            {
              type: 'text',
              text: success ? 'Profile cookies updated successfully' : 'Failed to update profile cookies (no profile associated)',
            },
          ],
        };
      }

      case 'browser_get_context_info': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const contextInfo = await browserWrapper.getContextInfo(context_id);
        // Result is a JSON object from C++, stringify for text output
        const infoText = typeof contextInfo === 'string'
          ? contextInfo
          : JSON.stringify(contextInfo, null, 2);
        return {
          content: [
            {
              type: 'text',
              text: infoText,
            },
          ],
        };
      }

      // ==================== ADVANCED MOUSE INTERACTIONS ====================
      case 'browser_hover': {
        const { context_id, selector } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.hover(context_id, selector);
        return {
          content: [{ type: 'text', text: `Hovered over element: ${selector}` }],
        };
      }

      case 'browser_double_click': {
        const { context_id, selector } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.doubleClick(context_id, selector);
        return {
          content: [{ type: 'text', text: `Double-clicked element: ${selector}` }],
        };
      }

      case 'browser_right_click': {
        const { context_id, selector } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.rightClick(context_id, selector);
        return {
          content: [{ type: 'text', text: `Right-clicked element: ${selector}` }],
        };
      }

      // ==================== INPUT CONTROL ====================
      case 'browser_clear_input': {
        const { context_id, selector } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.clearInput(context_id, selector);
        return {
          content: [{ type: 'text', text: `Cleared input: ${selector}` }],
        };
      }

      case 'browser_focus': {
        const { context_id, selector } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.focus(context_id, selector);
        return formatActionResult(result, `Focused element: ${selector}`);
      }

      case 'browser_blur': {
        const { context_id, selector } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.blur(context_id, selector);
        return formatActionResult(result, `Blurred element: ${selector}`);
      }

      case 'browser_select_all': {
        const { context_id, selector } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.selectAll(context_id, selector);
        return {
          content: [{ type: 'text', text: `Selected all text in: ${selector}` }],
        };
      }

      // ==================== KEYBOARD COMBINATIONS ====================
      case 'browser_keyboard_combo': {
        const { context_id, combo } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.keyboardCombo(context_id, combo);
        return {
          content: [{ type: 'text', text: `Pressed keyboard combo: ${combo}` }],
        };
      }

      // ==================== JAVASCRIPT EVALUATION ====================
      case 'browser_evaluate': {
        const { context_id, script, expression, return_value = false } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        // If expression is provided, use it as script and force return_value to true
        const actualScript = expression || script;
        const actualReturnValue = expression ? true : return_value;
        if (!actualScript) {
          throw new Error('Either script or expression must be provided');
        }
        const result = await browserWrapper.evaluate(context_id, actualScript, actualReturnValue);
        return {
          content: [{ type: 'text', text: JSON.stringify(result, null, 2) }],
        };
      }

      // ==================== ELEMENT STATE CHECKS ====================
      case 'browser_is_visible': {
        const { context_id, selector } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.isVisible(context_id, selector);
        // result is ActionResult: success=true means visible, message contains state
        const isVisible = result.success && result.message?.includes('visible') && !result.message?.includes('not visible');
        return {
          content: [{ type: 'text', text: `Element "${selector}" is ${isVisible ? 'visible' : 'not visible'}` }],
        };
      }

      case 'browser_is_enabled': {
        const { context_id, selector } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.isEnabled(context_id, selector);
        // result is ActionResult: success=true means enabled
        const isEnabled = result.success && result.message?.includes('enabled');
        return {
          content: [{ type: 'text', text: `Element "${selector}" is ${isEnabled ? 'enabled' : 'disabled'}` }],
        };
      }

      case 'browser_is_checked': {
        const { context_id, selector } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.isChecked(context_id, selector);
        // result is ActionResult: message contains checked state
        const isChecked = result.success && result.message?.includes('checked') && !result.message?.includes('not checked');
        return {
          content: [{ type: 'text', text: `Element "${selector}" is ${isChecked ? 'checked' : 'not checked'}` }],
        };
      }

      case 'browser_get_attribute': {
        const { context_id, selector, attribute } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const value = await browserWrapper.getAttribute(context_id, selector, attribute);
        return {
          content: [{ type: 'text', text: `Attribute "${attribute}" = "${value}"` }],
        };
      }

      case 'browser_get_bounding_box': {
        const { context_id, selector } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const box = await browserWrapper.getBoundingBox(context_id, selector);
        return {
          content: [{ type: 'text', text: JSON.stringify(box, null, 2) }],
        };
      }

      // ==================== FILE OPERATIONS ====================
      case 'browser_upload_file': {
        const { context_id, selector, file_paths } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.uploadFile(context_id, selector, file_paths);
        return formatActionResult(result, `Uploaded ${file_paths.length} file(s) to: ${selector}`);
      }

      // ==================== FRAME/IFRAME HANDLING ====================
      case 'browser_list_frames': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const frames = await browserWrapper.listFrames(context_id);
        return {
          content: [{ type: 'text', text: JSON.stringify(frames, null, 2) }],
        };
      }

      case 'browser_switch_to_frame': {
        const { context_id, frame_selector } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.switchToFrame(context_id, frame_selector);
        return formatActionResult(result, `Switched to frame: ${frame_selector}`);
      }

      case 'browser_switch_to_main_frame': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.switchToMainFrame(context_id);
        return {
          content: [{ type: 'text', text: 'Switched to main frame' }],
        };
      }

      // ==================== NETWORK INTERCEPTION ====================
      case 'browser_add_network_rule': {
        const { context_id, url_pattern, action, is_regex, redirect_url, mock_body, mock_status, mock_content_type } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const rule = {
          url_pattern,
          action,
          is_regex: is_regex || false,
          redirect_url: redirect_url || '',
          mock_body: mock_body || '',
          mock_status: mock_status || 200,
          mock_content_type: mock_content_type || 'text/plain',
        };
        const result = await browserWrapper.addNetworkRule(context_id, rule);
        return {
          content: [{ type: 'text', text: JSON.stringify(result) }],
        };
      }

      case 'browser_remove_network_rule': {
        const { rule_id } = args;
        await browserWrapper.removeNetworkRule(rule_id);
        return {
          content: [{ type: 'text', text: `Removed network rule: ${rule_id}` }],
        };
      }

      case 'browser_enable_network_interception': {
        const { context_id, enable } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.enableNetworkInterception(context_id, enable);
        return {
          content: [{ type: 'text', text: `Network interception ${enable ? 'enabled' : 'disabled'}` }],
        };
      }

      case 'browser_get_network_log': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.getNetworkLog(context_id);
        return {
          content: [{ type: 'text', text: JSON.stringify(result) }],
        };
      }

      case 'browser_clear_network_log': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.clearNetworkLog(context_id);
        return {
          content: [{ type: 'text', text: 'Network log cleared' }],
        };
      }

      // ==================== DOWNLOAD MANAGEMENT ====================
      case 'browser_set_download_path': {
        const { context_id, path } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.setDownloadPath(context_id, path);
        return {
          content: [{ type: 'text', text: `Download path set to: ${path}` }],
        };
      }

      case 'browser_get_downloads': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.getDownloads(context_id);
        return {
          content: [{ type: 'text', text: JSON.stringify(result) }],
        };
      }

      case 'browser_wait_for_download': {
        const { download_id, timeout } = args;
        const success = await browserWrapper.waitForDownload(download_id, timeout || 30000);
        return {
          content: [{ type: 'text', text: success ? 'Download completed' : 'Download failed or timed out' }],
        };
      }

      case 'browser_cancel_download': {
        const { download_id } = args;
        await browserWrapper.cancelDownload(download_id);
        return {
          content: [{ type: 'text', text: `Download cancelled: ${download_id}` }],
        };
      }

      // ==================== DIALOG HANDLING ====================
      case 'browser_set_dialog_action': {
        const { context_id, dialog_type, action, prompt_text } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.setDialogAction(context_id, dialog_type, action, prompt_text || '');
        return {
          content: [{ type: 'text', text: `Dialog action set: ${dialog_type} -> ${action}` }],
        };
      }

      case 'browser_get_pending_dialog': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.getPendingDialog(context_id);
        return {
          content: [{ type: 'text', text: JSON.stringify(result) }],
        };
      }

      case 'browser_handle_dialog': {
        const { dialog_id, accept, response_text } = args;
        await browserWrapper.handleDialog(dialog_id, accept, response_text || '');
        return {
          content: [{ type: 'text', text: `Dialog ${accept ? 'accepted' : 'dismissed'}` }],
        };
      }

      case 'browser_wait_for_dialog': {
        const { context_id, timeout } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const appeared = await browserWrapper.waitForDialog(context_id, timeout || 5000);
        return {
          content: [{ type: 'text', text: appeared ? 'Dialog appeared' : 'No dialog appeared within timeout' }],
        };
      }

      // ==================== TAB/WINDOW MANAGEMENT ====================
      case 'browser_set_popup_policy': {
        const { context_id, policy } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.setPopupPolicy(context_id, policy);
        return {
          content: [{ type: 'text', text: `Popup policy set to: ${policy}` }],
        };
      }

      case 'browser_get_tabs': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.getTabs(context_id);
        return {
          content: [{ type: 'text', text: JSON.stringify(result) }],
        };
      }

      case 'browser_switch_tab': {
        const { context_id, tab_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.switchTab(context_id, tab_id);
        return formatActionResult(result, `Switched to tab: ${tab_id}`);
      }

      case 'browser_close_tab': {
        const { context_id, tab_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.closeTab(context_id, tab_id);
        return formatActionResult(result, `Closed tab: ${tab_id}`);
      }

      case 'browser_new_tab': {
        const { context_id, url } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.newTab(context_id, url || '');
        return {
          content: [{ type: 'text', text: JSON.stringify(result) }],
        };
      }

      case 'browser_get_active_tab': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.getActiveTab(context_id);
        return {
          content: [{ type: 'text', text: JSON.stringify(result) }],
        };
      }

      case 'browser_get_tab_count': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.getTabCount(context_id);
        return {
          content: [{ type: 'text', text: JSON.stringify(result) }],
        };
      }

      case 'browser_get_blocked_popups': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.getBlockedPopups(context_id);
        return {
          content: [{ type: 'text', text: JSON.stringify(result) }],
        };
      }

      // ==================== CLIPBOARD MANAGEMENT ====================
      case 'browser_clipboard_read': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        const result = await browserWrapper.clipboardRead(context_id);
        return {
          content: [{ type: 'text', text: JSON.stringify(result) }],
        };
      }

      case 'browser_clipboard_write': {
        const { context_id, text } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.clipboardWrite(context_id, text);
        return {
          content: [{ type: 'text', text: `Text written to clipboard (${text.length} characters)` }],
        };
      }

      case 'browser_clipboard_clear': {
        const { context_id } = args;
        if (!activeContexts.has(context_id)) {
          throw new Error(`Context not found: ${context_id}`);
        }
        await browserWrapper.clipboardClear(context_id);
        return {
          content: [{ type: 'text', text: 'Clipboard cleared' }],
        };
      }

      default:
        throw new Error(`Unknown tool: ${name}`);
    }
  } catch (error) {
    const errorMessage = error instanceof Error ? error.message : String(error);
    console.error('Tool error:', errorMessage);

    // Build structured error response with status code if available
    const errorInfo = {
      error: errorMessage,
      status: error.status || 'unknown',
    };

    // Include additional context from ActionResult errors
    if (error.selector) errorInfo.selector = error.selector;
    if (error.url) errorInfo.url = error.url;
    if (error.httpStatus) errorInfo.http_status = error.httpStatus;
    if (error.errorCode) errorInfo.error_code = error.errorCode;
    if (error.elementCount) errorInfo.element_count = error.elementCount;

    return {
      content: [
        {
          type: 'text',
          text: JSON.stringify(errorInfo, null, 2),
        },
      ],
      isError: true,
    };
  }
});

// Cleanup on exit
async function cleanup() {
  console.error('Cleaning up browser contexts...');

  // Release all active contexts
  for (const context_id of activeContexts.keys()) {
    try {
      await browserWrapper.releaseContext(context_id);
      console.error(`Released context: ${context_id}`);
    } catch (e) {
      console.error(`Error releasing context ${context_id}:`, e.message);
    }
  }
  activeContexts.clear();

  // Shutdown browser
  if (browserWrapper) {
    await browserWrapper.shutdown();
    console.error('Browser shutdown complete');
  }

  process.exit(0);
}

process.on('SIGINT', cleanup);
process.on('SIGTERM', cleanup);

// Start server
async function main() {
  const transport = new StdioServerTransport();
  await server.connect(transport);
  console.error('Owl Browser MCP Server running on stdio');
}

main().catch((error) => {
  console.error('Fatal error:', error);
  process.exit(1);
});