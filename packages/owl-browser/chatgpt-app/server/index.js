/**
 * Owl Browser ChatGPT App - MCP Server
 *
 * This server exposes Owl Browser capabilities to ChatGPT via MCP protocol.
 * It uses the actual browser-wrapper.cjs for full feature parity with the main MCP server.
 *
 * Users must have Owl Browser installed locally - this is NOT a cloud browser.
 *
 * @author Olib AI
 * @license MIT
 */

import express from 'express';
import cors from 'cors';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';
import { readFileSync, existsSync } from 'fs';
import { v4 as uuidv4 } from 'uuid';
import { createRequire } from 'module';

const require = createRequire(import.meta.url);
const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

// Import the actual browser wrapper from the main codebase
let BrowserWrapper;
try {
  BrowserWrapper = require('../../src/browser-wrapper.cjs').BrowserWrapper;
} catch (e) {
  console.warn('Could not load browser-wrapper.cjs, running in mock mode');
  BrowserWrapper = null;
}

const app = express();
const PORT = process.env.PORT || 3847;

// Brand colors
const BRAND_COLOR = '#6BA894';

// Trust proxy (required for ngrok/reverse proxies to get correct protocol)
app.set('trust proxy', true);

// Enable CORS for ChatGPT
app.use(cors({
  origin: ['https://chat.openai.com', 'https://chatgpt.com', 'http://localhost:3000', '*'],
  credentials: true,
  methods: ['GET', 'POST', 'OPTIONS'],
  allowedHeaders: ['Content-Type', 'Authorization', 'Accept']
}));

// Handle preflight requests
app.options('*', cors());

// Parse both JSON and URL-encoded bodies (OAuth uses form data)
app.use(express.json({ limit: '50mb' }));
app.use(express.urlencoded({ extended: true }));

// Serve static files for web component
app.use('/static', express.static(join(__dirname, '../web/dist')));
app.use('/assets', express.static(join(__dirname, '../web/assets')));

// =============================================================================
// BROWSER CONNECTION STATE
// =============================================================================

let browserWrapper = null;
const activeContexts = new Map();

/**
 * Initialize browser wrapper lazily on first use
 */
async function ensureBrowser() {
  if (browserWrapper) return browserWrapper;

  if (!BrowserWrapper) {
    throw new Error('Browser wrapper not available. Ensure Owl Browser is built.');
  }

  browserWrapper = new BrowserWrapper();
  await browserWrapper.initialize();
  return browserWrapper;
}

/**
 * Format an ActionResult response for MCP tools
 */
function formatActionResult(result, customMessage = null) {
  const message = customMessage || result.message || (result.success ? 'Success' : 'Failed');

  if (result.success && result.status === 'ok') {
    return { content: [{ type: 'text', text: message }] };
  }

  const responseData = {
    success: result.success,
    status: result.status || 'unknown',
    message: message,
  };

  if (result.selector) responseData.selector = result.selector;
  if (result.url) responseData.url = result.url;
  if (result.error_code) responseData.error_code = result.error_code;

  return { content: [{ type: 'text', text: JSON.stringify(responseData, null, 2) }] };
}

// =============================================================================
// COMPLETE TOOL DEFINITIONS (100+ tools from actual MCP server)
// =============================================================================

const TOOLS = [
  // ---------------------------------------------------------------------------
  // ChatGPT Required Tools (search and fetch - REQUIRED for connectors)
  // These MUST follow OpenAI's exact specification with JSON Schema format
  // ---------------------------------------------------------------------------
  {
    name: 'search',
    description: 'Search the web for information. Returns a list of result IDs that can be fetched for full content.',
    inputSchema: {
      $schema: 'https://json-schema.org/draft/2020-12/schema',
      type: 'object',
      properties: {
        query: { type: 'string', description: 'Natural language search query' }
      },
      required: ['query'],
      additionalProperties: false
    },
    category: 'chatgpt'
  },
  {
    name: 'fetch',
    description: 'Fetch the full content of a search result by its ID (URL).',
    inputSchema: {
      $schema: 'https://json-schema.org/draft/2020-12/schema',
      type: 'object',
      properties: {
        id: { type: 'string', description: 'The ID (URL) of the result to fetch' }
      },
      required: ['id'],
      additionalProperties: false
    },
    category: 'chatgpt'
  },

  // ---------------------------------------------------------------------------
  // Context Management (3 tools)
  // ---------------------------------------------------------------------------
  {
    name: 'browser_create_context',
    description: 'Create a new browser context (tab/window). Returns a context_id to use with other tools. Each context is isolated. Optionally configure LLM, proxy, and profile settings.',
    inputSchema: {
      type: 'object',
      properties: {
        llm_enabled: { type: 'boolean', description: 'Enable LLM features (default: true)' },
        llm_use_builtin: { type: 'boolean', description: 'Use built-in llama-server (default: true)' },
        llm_endpoint: { type: 'string', description: 'External LLM API endpoint' },
        llm_model: { type: 'string', description: 'External LLM model name' },
        llm_api_key: { type: 'string', description: 'API key for external LLM' },
        proxy_type: { type: 'string', enum: ['http', 'https', 'socks4', 'socks5', 'socks5h'], description: 'Proxy type' },
        proxy_host: { type: 'string', description: 'Proxy hostname' },
        proxy_port: { type: 'number', description: 'Proxy port' },
        proxy_username: { type: 'string', description: 'Proxy username' },
        proxy_password: { type: 'string', description: 'Proxy password' },
        proxy_stealth: { type: 'boolean', description: 'Enable stealth mode' },
        is_tor: { type: 'boolean', description: 'Mark as Tor proxy' },
        tor_control_port: { type: 'number', description: 'Tor control port' },
        profile_path: { type: 'string', description: 'Path to browser profile JSON' },
        resource_blocking: { type: 'boolean', description: 'Block ads/trackers (default: true)' },
        os: { type: 'string', enum: ['windows', 'macos', 'linux'], description: 'Filter profiles by OS' },
        gpu: { type: 'string', description: 'Filter profiles by GPU' }
      }
    },
    category: 'context'
  },
  {
    name: 'browser_close_context',
    description: 'Close a browser context and free its resources',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID to close' } },
      required: ['context_id']
    },
    category: 'context'
  },
  {
    name: 'browser_list_contexts',
    description: 'List all active browser contexts',
    inputSchema: { type: 'object', properties: {} },
    category: 'context'
  },

  // ---------------------------------------------------------------------------
  // Navigation (4 tools)
  // ---------------------------------------------------------------------------
  {
    name: 'browser_navigate',
    description: 'Navigate to a URL. Use wait_until to wait for page load.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        url: { type: 'string', description: 'The URL to navigate to' },
        wait_until: { type: 'string', enum: ['', 'load', 'domcontentloaded', 'networkidle'], description: 'When to consider navigation complete' },
        timeout: { type: 'number', description: 'Timeout in ms (default: 30000)' }
      },
      required: ['context_id', 'url']
    },
    category: 'navigation'
  },
  {
    name: 'browser_reload',
    description: 'Reload the current page',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        ignore_cache: { type: 'boolean', default: false, description: 'Hard reload' },
        wait_until: { type: 'string', enum: ['', 'load', 'domcontentloaded', 'networkidle'], default: 'load' },
        timeout: { type: 'number', default: 30000 }
      },
      required: ['context_id']
    },
    category: 'navigation'
  },
  {
    name: 'browser_go_back',
    description: 'Navigate back in browser history',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        wait_until: { type: 'string', enum: ['', 'load', 'domcontentloaded', 'networkidle'], default: 'load' },
        timeout: { type: 'number', default: 30000 }
      },
      required: ['context_id']
    },
    category: 'navigation'
  },
  {
    name: 'browser_go_forward',
    description: 'Navigate forward in browser history',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        wait_until: { type: 'string', enum: ['', 'load', 'domcontentloaded', 'networkidle'], default: 'load' },
        timeout: { type: 'number', default: 30000 }
      },
      required: ['context_id']
    },
    category: 'navigation'
  },

  // ---------------------------------------------------------------------------
  // Interaction (15 tools)
  // ---------------------------------------------------------------------------
  {
    name: 'browser_click',
    description: 'Click an element using CSS selector, coordinates (e.g., "100x200"), or natural language description',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        selector: { type: 'string', description: 'CSS selector, position, or natural language' }
      },
      required: ['context_id', 'selector']
    },
    category: 'interaction'
  },
  {
    name: 'browser_type',
    description: 'Type text into an input field',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        selector: { type: 'string', description: 'CSS selector or natural language' },
        text: { type: 'string', description: 'Text to type' }
      },
      required: ['context_id', 'selector', 'text']
    },
    category: 'interaction'
  },
  {
    name: 'browser_pick',
    description: 'Select an option from a dropdown/select element',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        selector: { type: 'string', description: 'CSS selector or natural language' },
        value: { type: 'string', description: 'Option value or text to select' }
      },
      required: ['context_id', 'selector', 'value']
    },
    category: 'interaction'
  },
  {
    name: 'browser_press_key',
    description: 'Press a special key (Enter, Tab, Escape, Arrow keys, etc.)',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        key: { type: 'string', enum: ['Enter', 'Return', 'Tab', 'Escape', 'Esc', 'Backspace', 'Delete', 'Del', 'ArrowUp', 'Up', 'ArrowDown', 'Down', 'ArrowLeft', 'Left', 'ArrowRight', 'Right', 'Space', 'Home', 'End', 'PageUp', 'PageDown'] }
      },
      required: ['context_id', 'key']
    },
    category: 'interaction'
  },
  {
    name: 'browser_submit_form',
    description: 'Submit the currently focused form by pressing Enter',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'interaction'
  },
  {
    name: 'browser_hover',
    description: 'Hover over an element to reveal tooltips or menus',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        selector: { type: 'string', description: 'CSS selector or natural language' }
      },
      required: ['context_id', 'selector']
    },
    category: 'interaction'
  },
  {
    name: 'browser_double_click',
    description: 'Double-click an element',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        selector: { type: 'string', description: 'CSS selector or natural language' }
      },
      required: ['context_id', 'selector']
    },
    category: 'interaction'
  },
  {
    name: 'browser_right_click',
    description: 'Right-click an element to open context menu',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        selector: { type: 'string', description: 'CSS selector or natural language' }
      },
      required: ['context_id', 'selector']
    },
    category: 'interaction'
  },
  {
    name: 'browser_clear_input',
    description: 'Clear the text content of an input field',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        selector: { type: 'string', description: 'CSS selector or natural language' }
      },
      required: ['context_id', 'selector']
    },
    category: 'interaction'
  },
  {
    name: 'browser_focus',
    description: 'Focus on an element without clicking',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        selector: { type: 'string', description: 'CSS selector or natural language' }
      },
      required: ['context_id', 'selector']
    },
    category: 'interaction'
  },
  {
    name: 'browser_blur',
    description: 'Remove focus from an element (blur)',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        selector: { type: 'string', description: 'CSS selector or natural language' }
      },
      required: ['context_id', 'selector']
    },
    category: 'interaction'
  },
  {
    name: 'browser_select_all',
    description: 'Select all text in an input element',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        selector: { type: 'string', description: 'CSS selector or natural language' }
      },
      required: ['context_id', 'selector']
    },
    category: 'interaction'
  },
  {
    name: 'browser_keyboard_combo',
    description: 'Press a keyboard combination (Ctrl+A, Shift+Enter, Meta+V, etc.)',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        combo: { type: 'string', description: 'Key combination (e.g., "Ctrl+A", "Shift+Enter")' }
      },
      required: ['context_id', 'combo']
    },
    category: 'interaction'
  },
  {
    name: 'browser_drag_drop',
    description: 'Drag from start to end position with optional waypoints. For slider CAPTCHAs and puzzle solving.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        start_x: { type: 'number', description: 'Start X coordinate' },
        start_y: { type: 'number', description: 'Start Y coordinate' },
        end_x: { type: 'number', description: 'End X coordinate' },
        end_y: { type: 'number', description: 'End Y coordinate' },
        mid_points: { type: 'array', items: { type: 'array', items: { type: 'number' } }, description: 'Optional waypoints [[x,y], ...]' }
      },
      required: ['context_id', 'start_x', 'start_y', 'end_x', 'end_y']
    },
    category: 'interaction'
  },
  {
    name: 'browser_html5_drag_drop',
    description: 'HTML5 Drag and Drop for draggable="true" elements. For reordering lists and sortable interfaces.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        source_selector: { type: 'string', description: 'Source element CSS selector' },
        target_selector: { type: 'string', description: 'Target element CSS selector' }
      },
      required: ['context_id', 'source_selector', 'target_selector']
    },
    category: 'interaction'
  },
  {
    name: 'browser_mouse_move',
    description: 'Move mouse along a natural curved path. Uses bezier curves for human-like movement.',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        start_x: { type: 'number', description: 'Start X' },
        start_y: { type: 'number', description: 'Start Y' },
        end_x: { type: 'number', description: 'End X' },
        end_y: { type: 'number', description: 'End Y' },
        steps: { type: 'number', description: 'Intermediate points (0 = auto)' },
        stop_points: { type: 'array', items: { type: 'array', items: { type: 'number' } }, description: 'Pause points [[x,y], ...]' }
      },
      required: ['context_id', 'start_x', 'start_y', 'end_x', 'end_y']
    },
    category: 'interaction'
  },
  {
    name: 'browser_upload_file',
    description: 'Upload files to a file input element',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        selector: { type: 'string', description: 'CSS selector for file input' },
        file_paths: { type: 'array', items: { type: 'string' }, description: 'Array of file paths' }
      },
      required: ['context_id', 'selector', 'file_paths']
    },
    category: 'interaction'
  },

  // ---------------------------------------------------------------------------
  // Scrolling (4 tools)
  // ---------------------------------------------------------------------------
  {
    name: 'browser_scroll_by',
    description: 'Scroll the page by specified pixels',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        x: { type: 'number', default: 0, description: 'Horizontal pixels' },
        y: { type: 'number', description: 'Vertical pixels (positive = down)' }
      },
      required: ['context_id', 'y']
    },
    category: 'scroll'
  },
  {
    name: 'browser_scroll_to_element',
    description: 'Scroll an element into view',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        selector: { type: 'string', description: 'CSS selector or natural language' }
      },
      required: ['context_id', 'selector']
    },
    category: 'scroll'
  },
  {
    name: 'browser_scroll_to_top',
    description: 'Scroll to top of page',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'scroll'
  },
  {
    name: 'browser_scroll_to_bottom',
    description: 'Scroll to bottom of page',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'scroll'
  },

  // ---------------------------------------------------------------------------
  // Content Extraction (8 tools)
  // ---------------------------------------------------------------------------
  {
    name: 'browser_screenshot',
    description: 'Take a screenshot of the current page',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        mode: { type: 'string', enum: ['viewport', 'element', 'fullpage'], default: 'viewport' },
        selector: { type: 'string', description: 'Element selector (for element mode)' }
      },
      required: ['context_id']
    },
    category: 'extraction'
  },
  {
    name: 'browser_extract_text',
    description: 'Extract text content from the page or element',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        selector: { type: 'string', default: 'body', description: 'Element selector (optional)' }
      },
      required: ['context_id']
    },
    category: 'extraction'
  },
  {
    name: 'browser_get_html',
    description: 'Extract clean HTML from the page',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        clean_level: { type: 'string', enum: ['minimal', 'basic', 'aggressive'], default: 'basic' }
      },
      required: ['context_id']
    },
    category: 'extraction'
  },
  {
    name: 'browser_get_markdown',
    description: 'Extract page content as clean Markdown',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        include_links: { type: 'boolean', default: true },
        include_images: { type: 'boolean', default: true },
        max_length: { type: 'number', default: -1, description: '-1 for no limit' }
      },
      required: ['context_id']
    },
    category: 'extraction'
  },
  {
    name: 'browser_extract_json',
    description: 'Extract structured JSON data using templates or auto-detection',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        template: { type: 'string', description: 'Template name or empty for auto-detect' }
      },
      required: ['context_id']
    },
    category: 'extraction'
  },
  {
    name: 'browser_detect_site',
    description: 'Detect website type for template matching',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'extraction'
  },
  {
    name: 'browser_list_templates',
    description: 'List all available extraction templates',
    inputSchema: { type: 'object', properties: {} },
    category: 'extraction'
  },
  {
    name: 'browser_get_page_info',
    description: 'Get current page info (URL, title, navigation state)',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'extraction'
  },
  {
    name: 'browser_highlight',
    description: 'Highlight an element with colored border for debugging',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        selector: { type: 'string', description: 'CSS selector or natural language' },
        border_color: { type: 'string', default: '#FF0000' },
        background_color: { type: 'string', default: 'rgba(255, 0, 0, 0.2)' }
      },
      required: ['context_id', 'selector']
    },
    category: 'extraction'
  },
  {
    name: 'browser_show_grid_overlay',
    description: 'Show a grid overlay with XY coordinates for debugging',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        horizontal_lines: { type: 'number', default: 25 },
        vertical_lines: { type: 'number', default: 25 },
        line_color: { type: 'string', default: 'rgba(255, 0, 0, 0.15)' },
        text_color: { type: 'string', default: 'rgba(255, 0, 0, 0.4)' }
      },
      required: ['context_id']
    },
    category: 'extraction'
  },

  // ---------------------------------------------------------------------------
  // Element State (5 tools)
  // ---------------------------------------------------------------------------
  {
    name: 'browser_is_visible',
    description: 'Check if an element is visible on the page',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        selector: { type: 'string', description: 'CSS selector or natural language' }
      },
      required: ['context_id', 'selector']
    },
    category: 'state'
  },
  {
    name: 'browser_is_enabled',
    description: 'Check if an element is enabled (not disabled)',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        selector: { type: 'string', description: 'CSS selector or natural language' }
      },
      required: ['context_id', 'selector']
    },
    category: 'state'
  },
  {
    name: 'browser_is_checked',
    description: 'Check if a checkbox or radio button is checked',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        selector: { type: 'string', description: 'CSS selector or natural language' }
      },
      required: ['context_id', 'selector']
    },
    category: 'state'
  },
  {
    name: 'browser_get_attribute',
    description: 'Get the value of an attribute from an element',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        selector: { type: 'string', description: 'CSS selector or natural language' },
        attribute: { type: 'string', description: 'Attribute name (e.g., "href", "src", "value")' }
      },
      required: ['context_id', 'selector', 'attribute']
    },
    category: 'state'
  },
  {
    name: 'browser_get_bounding_box',
    description: 'Get position and size of an element {x, y, width, height}',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        selector: { type: 'string', description: 'CSS selector or natural language' }
      },
      required: ['context_id', 'selector']
    },
    category: 'state'
  },

  // ---------------------------------------------------------------------------
  // AI-Powered Features (4 tools)
  // ---------------------------------------------------------------------------
  {
    name: 'browser_nla',
    description: 'Execute multi-step tasks using Natural Language Actions. Example: "go to google.com and search for banana"',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        command: { type: 'string', description: 'Natural language command' }
      },
      required: ['context_id', 'command']
    },
    category: 'ai'
  },
  {
    name: 'browser_summarize_page',
    description: 'Create an intelligent summary of the current page using LLM',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        force_refresh: { type: 'boolean', default: false, description: 'Ignore cache' }
      },
      required: ['context_id']
    },
    category: 'ai'
  },
  {
    name: 'browser_query_page',
    description: 'Ask a natural language question about the current page',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        query: { type: 'string', description: 'Question about the page' }
      },
      required: ['context_id', 'query']
    },
    category: 'ai'
  },
  {
    name: 'browser_llm_status',
    description: 'Check if the on-device LLM is ready',
    inputSchema: { type: 'object', properties: {} },
    category: 'ai'
  },

  // ---------------------------------------------------------------------------
  // Wait & Synchronization (5 tools)
  // ---------------------------------------------------------------------------
  {
    name: 'browser_wait',
    description: 'Wait for specified milliseconds',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        timeout: { type: 'number', description: 'Milliseconds to wait' }
      },
      required: ['context_id', 'timeout']
    },
    category: 'wait'
  },
  {
    name: 'browser_wait_for_selector',
    description: 'Wait for an element to appear on the page',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        selector: { type: 'string', description: 'CSS selector or natural language' },
        timeout: { type: 'number', default: 5000 }
      },
      required: ['context_id', 'selector']
    },
    category: 'wait'
  },
  {
    name: 'browser_wait_for_network_idle',
    description: 'Wait for network activity to become idle',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        idle_time: { type: 'number', default: 500 },
        timeout: { type: 'number', default: 30000 }
      },
      required: ['context_id']
    },
    category: 'wait'
  },
  {
    name: 'browser_wait_for_function',
    description: 'Wait for a JavaScript function to return truthy',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        js_function: { type: 'string', description: 'JS function body' },
        polling: { type: 'number', default: 100 },
        timeout: { type: 'number', default: 30000 }
      },
      required: ['context_id', 'js_function']
    },
    category: 'wait'
  },
  {
    name: 'browser_wait_for_url',
    description: 'Wait for the page URL to match a pattern',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        url_pattern: { type: 'string', description: 'URL pattern (substring or glob)' },
        is_regex: { type: 'boolean', default: false },
        timeout: { type: 'number', default: 30000 }
      },
      required: ['context_id', 'url_pattern']
    },
    category: 'wait'
  },

  // ---------------------------------------------------------------------------
  // Page Control (6 tools)
  // ---------------------------------------------------------------------------
  {
    name: 'browser_set_viewport',
    description: 'Change browser viewport size',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        width: { type: 'number', description: 'Viewport width' },
        height: { type: 'number', description: 'Viewport height' }
      },
      required: ['context_id', 'width', 'height']
    },
    category: 'page'
  },
  {
    name: 'browser_evaluate',
    description: 'Execute JavaScript code in the page context',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        script: { type: 'string', description: 'JavaScript code' },
        expression: { type: 'string', description: 'JS expression (shorthand)' },
        return_value: { type: 'boolean', default: false }
      },
      required: ['context_id']
    },
    category: 'page'
  },
  {
    name: 'browser_zoom_in',
    description: 'Zoom in the page by 10%',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'page'
  },
  {
    name: 'browser_zoom_out',
    description: 'Zoom out the page by 10%',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'page'
  },
  {
    name: 'browser_zoom_reset',
    description: 'Reset page zoom to 100%',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'page'
  },
  {
    name: 'browser_get_console_log',
    description: 'Read console logs from the browser',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        level: { type: 'string', enum: ['debug', 'info', 'warn', 'error', 'verbose'] },
        filter: { type: 'string', description: 'Filter by text' },
        limit: { type: 'number', description: 'Max entries (default: 100)' }
      },
      required: ['context_id']
    },
    category: 'page'
  },
  {
    name: 'browser_clear_console_log',
    description: 'Clear all console logs',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'page'
  },

  // ---------------------------------------------------------------------------
  // Video Recording (5 tools)
  // ---------------------------------------------------------------------------
  {
    name: 'browser_start_video_recording',
    description: 'Start recording the browser session as video',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        fps: { type: 'number', default: 30 },
        codec: { type: 'string', default: 'libx264' }
      },
      required: ['context_id']
    },
    category: 'video'
  },
  {
    name: 'browser_pause_video_recording',
    description: 'Pause video recording',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'video'
  },
  {
    name: 'browser_resume_video_recording',
    description: 'Resume paused video recording',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'video'
  },
  {
    name: 'browser_stop_video_recording',
    description: 'Stop recording and save the video file',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'video'
  },
  {
    name: 'browser_get_video_recording_stats',
    description: 'Get recording statistics (frames, duration)',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'video'
  },

  // ---------------------------------------------------------------------------
  // Demographics & Context (4 tools)
  // ---------------------------------------------------------------------------
  {
    name: 'browser_get_demographics',
    description: 'Get user demographics (location, time, weather)',
    inputSchema: { type: 'object', properties: {} },
    category: 'demographics'
  },
  {
    name: 'browser_get_location',
    description: 'Get user location based on IP',
    inputSchema: { type: 'object', properties: {} },
    category: 'demographics'
  },
  {
    name: 'browser_get_datetime',
    description: 'Get current date and time',
    inputSchema: { type: 'object', properties: {} },
    category: 'demographics'
  },
  {
    name: 'browser_get_weather',
    description: 'Get current weather for user location',
    inputSchema: { type: 'object', properties: {} },
    category: 'demographics'
  },

  // ---------------------------------------------------------------------------
  // CAPTCHA Solving (5 tools)
  // ---------------------------------------------------------------------------
  {
    name: 'browser_detect_captcha',
    description: 'Detect if the current page has a CAPTCHA',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'captcha'
  },
  {
    name: 'browser_classify_captcha',
    description: 'Classify the type of CAPTCHA (text, image, checkbox, puzzle, audio)',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'captcha'
  },
  {
    name: 'browser_solve_text_captcha',
    description: 'Solve a text-based CAPTCHA using vision OCR',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        max_attempts: { type: 'number', default: 3 }
      },
      required: ['context_id']
    },
    category: 'captcha'
  },
  {
    name: 'browser_solve_image_captcha',
    description: 'Solve an image-selection CAPTCHA (reCAPTCHA, Cloudflare)',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        max_attempts: { type: 'number', default: 3 },
        provider: { type: 'string', enum: ['auto', 'owl', 'recaptcha', 'cloudflare', 'hcaptcha'], default: 'auto' }
      },
      required: ['context_id']
    },
    category: 'captcha'
  },
  {
    name: 'browser_solve_captcha',
    description: 'Auto-detect and solve any supported CAPTCHA',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        max_attempts: { type: 'number', default: 3 },
        provider: { type: 'string', enum: ['auto', 'owl', 'recaptcha', 'cloudflare', 'hcaptcha'], default: 'auto' }
      },
      required: ['context_id']
    },
    category: 'captcha'
  },

  // ---------------------------------------------------------------------------
  // Cookie Management (3 tools)
  // ---------------------------------------------------------------------------
  {
    name: 'browser_get_cookies',
    description: 'Get all cookies from the browser context',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        url: { type: 'string', description: 'Optional URL filter' }
      },
      required: ['context_id']
    },
    category: 'cookies'
  },
  {
    name: 'browser_set_cookie',
    description: 'Set a cookie with full attribute control',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        url: { type: 'string', description: 'URL for cookie' },
        name: { type: 'string', description: 'Cookie name' },
        value: { type: 'string', description: 'Cookie value' },
        domain: { type: 'string' },
        path: { type: 'string', default: '/' },
        secure: { type: 'boolean', default: false },
        httpOnly: { type: 'boolean', default: false },
        sameSite: { type: 'string', enum: ['none', 'lax', 'strict'], default: 'lax' },
        expires: { type: 'number', default: -1 }
      },
      required: ['context_id', 'url', 'name', 'value']
    },
    category: 'cookies'
  },
  {
    name: 'browser_delete_cookies',
    description: 'Delete cookies (all, by URL, or specific)',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        url: { type: 'string', description: 'Optional URL filter' },
        cookie_name: { type: 'string', description: 'Optional specific cookie name' }
      },
      required: ['context_id']
    },
    category: 'cookies'
  },

  // ---------------------------------------------------------------------------
  // Proxy Management (4 tools)
  // ---------------------------------------------------------------------------
  {
    name: 'browser_set_proxy',
    description: 'Configure proxy settings with stealth features',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        type: { type: 'string', enum: ['http', 'https', 'socks4', 'socks5', 'socks5h'] },
        host: { type: 'string', description: 'Proxy hostname' },
        port: { type: 'number', description: 'Proxy port' },
        username: { type: 'string' },
        password: { type: 'string' },
        stealth: { type: 'boolean', description: 'Enable stealth mode' },
        block_webrtc: { type: 'boolean', description: 'Block WebRTC leaks' },
        spoof_timezone: { type: 'boolean' },
        timezone_override: { type: 'string' },
        spoof_language: { type: 'boolean' },
        language_override: { type: 'string' }
      },
      required: ['context_id', 'type', 'host', 'port']
    },
    category: 'proxy'
  },
  {
    name: 'browser_get_proxy_status',
    description: 'Get proxy configuration and connection status',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'proxy'
  },
  {
    name: 'browser_connect_proxy',
    description: 'Enable/connect the configured proxy',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'proxy'
  },
  {
    name: 'browser_disconnect_proxy',
    description: 'Disable/disconnect the proxy',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'proxy'
  },

  // ---------------------------------------------------------------------------
  // Profile Management (6 tools)
  // ---------------------------------------------------------------------------
  {
    name: 'browser_create_profile',
    description: 'Create a new profile with randomized fingerprints',
    inputSchema: {
      type: 'object',
      properties: { name: { type: 'string', description: 'Profile name' } }
    },
    category: 'profile'
  },
  {
    name: 'browser_load_profile',
    description: 'Load a profile from JSON file into context',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        profile_path: { type: 'string', description: 'Path to profile JSON' }
      },
      required: ['context_id', 'profile_path']
    },
    category: 'profile'
  },
  {
    name: 'browser_save_profile',
    description: 'Save current context state to profile JSON',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        profile_path: { type: 'string', description: 'Path to save' }
      },
      required: ['context_id']
    },
    category: 'profile'
  },
  {
    name: 'browser_get_profile',
    description: 'Get current profile state as JSON',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'profile'
  },
  {
    name: 'browser_update_profile_cookies',
    description: 'Update profile with current cookies',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'profile'
  },
  {
    name: 'browser_get_context_info',
    description: 'Get context info including fingerprint hashes',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'profile'
  },

  // ---------------------------------------------------------------------------
  // Frame/Iframe Handling (3 tools)
  // ---------------------------------------------------------------------------
  {
    name: 'browser_list_frames',
    description: 'List all frames (including iframes) in the page',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'frames'
  },
  {
    name: 'browser_switch_to_frame',
    description: 'Switch context to an iframe',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        frame_selector: { type: 'string', description: 'Frame name, index, or CSS selector' }
      },
      required: ['context_id', 'frame_selector']
    },
    category: 'frames'
  },
  {
    name: 'browser_switch_to_main_frame',
    description: 'Switch back to main frame from iframe',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'frames'
  },

  // ---------------------------------------------------------------------------
  // Network Interception (5 tools)
  // ---------------------------------------------------------------------------
  {
    name: 'browser_add_network_rule',
    description: 'Add a network interception rule (block, mock, redirect)',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        url_pattern: { type: 'string', description: 'URL pattern (glob or regex)' },
        action: { type: 'string', enum: ['allow', 'block', 'mock', 'redirect'] },
        is_regex: { type: 'boolean', default: false },
        mock_status: { type: 'number', default: 200 },
        mock_body: { type: 'string' },
        mock_content_type: { type: 'string', default: 'text/plain' },
        redirect_url: { type: 'string' }
      },
      required: ['context_id', 'url_pattern', 'action']
    },
    category: 'network'
  },
  {
    name: 'browser_remove_network_rule',
    description: 'Remove a network interception rule',
    inputSchema: {
      type: 'object',
      properties: { rule_id: { type: 'string', description: 'Rule ID to remove' } },
      required: ['rule_id']
    },
    category: 'network'
  },
  {
    name: 'browser_enable_network_interception',
    description: 'Enable or disable network interception',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        enable: { type: 'boolean' }
      },
      required: ['context_id', 'enable']
    },
    category: 'network'
  },
  {
    name: 'browser_get_network_log',
    description: 'Get captured network requests/responses',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'network'
  },
  {
    name: 'browser_clear_network_log',
    description: 'Clear captured network log',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'network'
  },

  // ---------------------------------------------------------------------------
  // File Downloads (4 tools)
  // ---------------------------------------------------------------------------
  {
    name: 'browser_set_download_path',
    description: 'Set the download directory for file downloads',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        path: { type: 'string', description: 'Download directory path' }
      },
      required: ['context_id', 'path']
    },
    category: 'downloads'
  },
  {
    name: 'browser_get_downloads',
    description: 'Get all downloads with their status',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'downloads'
  },
  {
    name: 'browser_wait_for_download',
    description: 'Wait for a download to complete',
    inputSchema: {
      type: 'object',
      properties: {
        download_id: { type: 'string', description: 'Download ID' },
        timeout: { type: 'number', default: 30000 }
      },
      required: ['download_id']
    },
    category: 'downloads'
  },
  {
    name: 'browser_cancel_download',
    description: 'Cancel an in-progress download',
    inputSchema: {
      type: 'object',
      properties: { download_id: { type: 'string', description: 'Download ID' } },
      required: ['download_id']
    },
    category: 'downloads'
  },

  // ---------------------------------------------------------------------------
  // Dialog Handling (4 tools)
  // ---------------------------------------------------------------------------
  {
    name: 'browser_set_dialog_action',
    description: 'Set automatic action for JS dialogs (alert, confirm, prompt)',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        dialog_type: { type: 'string', enum: ['alert', 'confirm', 'prompt', 'beforeunload'] },
        action: { type: 'string', enum: ['accept', 'dismiss', 'accept_with_text'] },
        prompt_text: { type: 'string', description: 'Default text for prompt' }
      },
      required: ['context_id', 'dialog_type', 'action']
    },
    category: 'dialogs'
  },
  {
    name: 'browser_get_pending_dialog',
    description: 'Get info about current pending dialog',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'dialogs'
  },
  {
    name: 'browser_handle_dialog',
    description: 'Accept or dismiss a specific dialog',
    inputSchema: {
      type: 'object',
      properties: {
        dialog_id: { type: 'string', description: 'Dialog ID' },
        accept: { type: 'boolean', description: 'Accept (true) or dismiss (false)' },
        response_text: { type: 'string', description: 'Response for prompt dialogs' }
      },
      required: ['dialog_id', 'accept']
    },
    category: 'dialogs'
  },
  {
    name: 'browser_wait_for_dialog',
    description: 'Wait for a dialog to appear',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        timeout: { type: 'number', default: 5000 }
      },
      required: ['context_id']
    },
    category: 'dialogs'
  },

  // ---------------------------------------------------------------------------
  // Tab/Window Management (8 tools)
  // ---------------------------------------------------------------------------
  {
    name: 'browser_set_popup_policy',
    description: 'Set how popups are handled (allow, block, new_tab, background)',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        policy: { type: 'string', enum: ['allow', 'block', 'new_tab', 'background'] }
      },
      required: ['context_id', 'policy']
    },
    category: 'tabs'
  },
  {
    name: 'browser_get_tabs',
    description: 'Get all tabs/windows for a context',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'tabs'
  },
  {
    name: 'browser_switch_tab',
    description: 'Switch to a specific tab',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        tab_id: { type: 'string', description: 'Tab ID' }
      },
      required: ['context_id', 'tab_id']
    },
    category: 'tabs'
  },
  {
    name: 'browser_close_tab',
    description: 'Close a specific tab',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        tab_id: { type: 'string', description: 'Tab ID' }
      },
      required: ['context_id', 'tab_id']
    },
    category: 'tabs'
  },
  {
    name: 'browser_new_tab',
    description: 'Open a new tab, optionally navigating to URL',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        url: { type: 'string', description: 'Optional URL' }
      },
      required: ['context_id']
    },
    category: 'tabs'
  },
  {
    name: 'browser_get_active_tab',
    description: 'Get the currently active tab',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'tabs'
  },
  {
    name: 'browser_get_tab_count',
    description: 'Get the number of open tabs',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'tabs'
  },
  {
    name: 'browser_get_blocked_popups',
    description: 'Get list of blocked popup URLs',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'tabs'
  },

  // ---------------------------------------------------------------------------
  // Clipboard (3 tools)
  // ---------------------------------------------------------------------------
  {
    name: 'browser_clipboard_read',
    description: 'Read text from system clipboard',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'clipboard'
  },
  {
    name: 'browser_clipboard_write',
    description: 'Write text to system clipboard',
    inputSchema: {
      type: 'object',
      properties: {
        context_id: { type: 'string', description: 'The context ID' },
        text: { type: 'string', description: 'Text to write' }
      },
      required: ['context_id', 'text']
    },
    category: 'clipboard'
  },
  {
    name: 'browser_clipboard_clear',
    description: 'Clear system clipboard',
    inputSchema: {
      type: 'object',
      properties: { context_id: { type: 'string', description: 'The context ID' } },
      required: ['context_id']
    },
    category: 'clipboard'
  }
];

// Tool categories
const CATEGORIES = {
  chatgpt: { name: 'ChatGPT', color: '#10A37F' },  // Required tools for ChatGPT connectors
  context: { name: 'Context', color: '#6BA894' },
  navigation: { name: 'Navigation', color: '#4A90A4' },
  interaction: { name: 'Interaction', color: '#7B68EE' },
  scroll: { name: 'Scrolling', color: '#20B2AA' },
  extraction: { name: 'Extraction', color: '#F5A623' },
  state: { name: 'Element State', color: '#8B4513' },
  ai: { name: 'AI Features', color: '#E74C3C' },
  wait: { name: 'Wait & Sync', color: '#9B59B6' },
  page: { name: 'Page Control', color: '#3498DB' },
  video: { name: 'Video Recording', color: '#E91E63' },
  demographics: { name: 'Demographics', color: '#00BCD4' },
  captcha: { name: 'CAPTCHA', color: '#FF9800' },
  cookies: { name: 'Cookies', color: '#795548' },
  proxy: { name: 'Proxy', color: '#607D8B' },
  profile: { name: 'Profiles', color: '#2ECC71' },
  frames: { name: 'Frames', color: '#FF5722' },
  network: { name: 'Network', color: '#673AB7' },
  downloads: { name: 'Downloads', color: '#009688' },
  dialogs: { name: 'Dialogs', color: '#FFC107' },
  tabs: { name: 'Tabs', color: '#03A9F4' },
  clipboard: { name: 'Clipboard', color: '#CDDC39' }
};

// =============================================================================
// MCP ENDPOINTS
// =============================================================================

/**
 * OAuth Protected Resource Metadata (RFC 9728)
 */
app.get('/.well-known/oauth-protected-resource', (req, res) => {
  const baseUrl = `${req.protocol}://${req.get('host')}`;
  res.json({
    resource: baseUrl,
    authorization_servers: [baseUrl],
    scopes_supported: ['browser:read', 'browser:write', 'browser:admin'],
    bearer_methods_supported: ['header'],
    resource_documentation: 'https://www.owlbrowser.net/docs/chatgpt-app'
  });
});

/**
 * OAuth Authorization Server Metadata (RFC 8414 + RFC 7591)
 * Generic endpoint
 */
app.get('/.well-known/oauth-authorization-server', (req, res) => {
  const baseUrl = `${req.protocol}://${req.get('host')}`;
  console.log('OAuth discovery (generic):', baseUrl);
  res.json({
    issuer: baseUrl,
    authorization_endpoint: `${baseUrl}/oauth/authorize`,
    token_endpoint: `${baseUrl}/oauth/token`,
    registration_endpoint: `${baseUrl}/oauth/register`,
    introspection_endpoint: `${baseUrl}/oauth/introspect`,
    scopes_supported: ['browser:read', 'browser:write', 'browser:admin'],
    response_types_supported: ['code'],
    response_modes_supported: ['query'],
    grant_types_supported: ['authorization_code', 'refresh_token'],
    token_endpoint_auth_methods_supported: ['client_secret_post', 'client_secret_basic', 'none'],
    code_challenge_methods_supported: ['S256'],
    service_documentation: 'https://www.owlbrowser.net/docs/chatgpt-app'
  });
});

/**
 * MCP-specific OAuth Authorization Server Metadata
 * ChatGPT specifically requests this path for MCP connectors
 */
app.get('/.well-known/oauth-authorization-server/mcp', (req, res) => {
  const baseUrl = `${req.protocol}://${req.get('host')}`;
  console.log('OAuth discovery (MCP-specific):', baseUrl);
  res.json({
    issuer: baseUrl,
    authorization_endpoint: `${baseUrl}/oauth/authorize`,
    token_endpoint: `${baseUrl}/oauth/token`,
    registration_endpoint: `${baseUrl}/oauth/register`,
    introspection_endpoint: `${baseUrl}/oauth/introspect`,
    scopes_supported: ['browser:read', 'browser:write', 'browser:admin'],
    response_types_supported: ['code'],
    response_modes_supported: ['query'],
    grant_types_supported: ['authorization_code', 'refresh_token'],
    token_endpoint_auth_methods_supported: ['client_secret_post', 'client_secret_basic', 'none'],
    code_challenge_methods_supported: ['S256'],
    service_documentation: 'https://www.owlbrowser.net/docs/chatgpt-app'
  });
});

/**
 * OpenID Connect Discovery (some clients check this)
 */
app.get('/.well-known/openid-configuration', (req, res) => {
  const baseUrl = `${req.protocol}://${req.get('host')}`;
  res.json({
    issuer: baseUrl,
    authorization_endpoint: `${baseUrl}/oauth/authorize`,
    token_endpoint: `${baseUrl}/oauth/token`,
    registration_endpoint: `${baseUrl}/oauth/register`,
    scopes_supported: ['browser:read', 'browser:write', 'browser:admin'],
    response_types_supported: ['code'],
    grant_types_supported: ['authorization_code', 'refresh_token'],
    token_endpoint_auth_methods_supported: ['client_secret_post', 'client_secret_basic', 'none'],
    code_challenge_methods_supported: ['S256']
  });
});

/**
 * MCP Server Info
 */
app.get('/mcp', (req, res) => {
  res.json({
    name: 'owl-browser',
    version: '1.0.0',
    description: 'Owl Browser - AI-First Browser Automation for ChatGPT',
    vendor: 'Olib AI',
    homepage: 'https://www.owlbrowser.net',
    capabilities: {
      tools: TOOLS.length,
      categories: Object.keys(CATEGORIES).length,
      resources: ['component.js']
    },
    branding: { primaryColor: BRAND_COLOR, logo: '/assets/owl-logo.svg' }
  });
});

/**
 * List available tools
 */
app.get('/mcp/tools', (req, res) => {
  const tools = TOOLS.map(t => ({
    name: t.name,
    description: t.description,
    inputSchema: t.inputSchema
  }));
  res.json({ tools });
});

/**
 * Get tool categories
 */
app.get('/mcp/tools/categories', (req, res) => {
  const categories = Object.entries(CATEGORIES).map(([id, cat]) => ({
    id,
    ...cat,
    tools: TOOLS.filter(t => t.category === id).map(t => t.name)
  }));
  res.json({ categories });
});

/**
 * Execute a tool
 */
app.post('/mcp/tools/:toolName', async (req, res) => {
  const { toolName } = req.params;
  const params = req.body;

  const tool = TOOLS.find(t => t.name === toolName);
  if (!tool) {
    return res.status(404).json({ error: 'Tool not found', message: `Unknown tool: ${toolName}` });
  }

  try {
    const browser = await ensureBrowser();
    const result = await executeTool(browser, toolName, params);

    // Handle screenshot responses
    if (result.screenshot) {
      return res.json({
        content: [{ type: 'image', data: result.screenshot, mimeType: 'image/png' }]
      });
    }

    res.json(formatActionResult(result));
  } catch (error) {
    // Browser not available - return helpful error
    if (error.message.includes('not available') || error.message.includes('not found')) {
      return res.json({
        content: [{
          type: 'text',
          text: JSON.stringify({
            success: false,
            status: 'browser_not_found',
            message: 'Owl Browser is not running. Please start the browser first.',
            help: 'Run: cd .. && npm run build && node src/mcp-server.cjs'
          }, null, 2)
        }]
      });
    }

    res.status(500).json({
      error: 'Tool execution failed',
      message: error.message,
      tool: toolName
    });
  }
});

/**
 * Execute search tool - required by ChatGPT connectors
 * MUST return format: { results: [{id, title, url}, ...] }
 * Each result must have: id (unique), title (human-readable), url (for citations)
 */
async function executeSearch(browser, params) {
  const { query } = params;
  const max_results = 10;

  console.log('Executing search:', query);

  try {
    // Create a temporary context for search
    const contextResult = await browser.createContext({});
    const contextId = contextResult.context_id;

    // Navigate to DuckDuckGo (privacy-focused, no CAPTCHAs)
    const searchUrl = `https://duckduckgo.com/?q=${encodeURIComponent(query)}`;
    await browser.navigate(contextId, searchUrl, 'networkidle', 30000);

    // Wait for results
    await browser.wait(contextId, 2000);

    // Extract search results with id, title, url
    const resultsJson = await browser.evaluate(contextId, `
      (function() {
        const results = [];
        const items = document.querySelectorAll('[data-testid="result"]');
        items.forEach((item, index) => {
          if (index >= ${max_results}) return;
          const titleEl = item.querySelector('h2 a');
          const snippetEl = item.querySelector('[data-result="snippet"]');
          if (titleEl && titleEl.href) {
            results.push({
              id: titleEl.href,
              title: titleEl.textContent?.trim() || 'Untitled',
              url: titleEl.href,
              snippet: snippetEl?.textContent?.trim() || ''
            });
          }
        });
        return JSON.stringify(results);
      })()
    `, true);

    // Clean up context
    await browser.releaseContext(contextId);

    const results = JSON.parse(resultsJson || '[]');
    console.log('Search found', results.length, 'results');

    // ChatGPT expects exactly this format: { results: [...] }
    return { results };
  } catch (error) {
    console.error('Search error:', error.message);
    return { results: [] };
  }
}

/**
 * Execute fetch tool - required by ChatGPT connectors
 * Accepts 'id' parameter (which is the URL from search results)
 * MUST return: { id, title, text (full content), url, metadata }
 */
async function executeFetch(browser, params) {
  // ChatGPT passes the URL as 'id'
  const url = params.id || params.url;

  console.log('Executing fetch:', url);

  try {
    // Create a temporary context
    const contextResult = await browser.createContext({});
    const contextId = contextResult.context_id;

    // Navigate to the URL
    await browser.navigate(contextId, url, 'networkidle', 30000);

    // Wait for content to load
    await browser.wait(contextId, 1000);

    // Get page info
    const pageInfo = await browser.getPageInfo(contextId);

    // Extract text content
    const content = await browser.extractText(contextId, 'body');

    // Clean up context
    await browser.releaseContext(contextId);

    const textContent = typeof content === 'string' ? content : content?.text || '';

    console.log('Fetch complete, content length:', textContent.length);

    // Return format ChatGPT expects: id, title, text, url, metadata
    return {
      id: url,
      title: pageInfo?.title || 'Untitled',
      text: textContent.slice(0, 50000), // 'text' is the field name ChatGPT expects
      url: url,
      metadata: {
        fetched_at: new Date().toISOString()
      }
    };
  } catch (error) {
    console.error('Fetch error:', error.message);
    return {
      id: url,
      title: 'Error',
      text: `Failed to fetch: ${error.message}`,
      url: url,
      metadata: {
        error: error.message,
        fetched_at: new Date().toISOString()
      }
    };
  }
}

/**
 * Execute tool using browser wrapper
 */
async function executeTool(browser, toolName, params) {
  const { context_id } = params;

  // Handle ChatGPT required tools (search and fetch)
  if (toolName === 'search') {
    return await executeSearch(browser, params);
  }
  if (toolName === 'fetch') {
    return await executeFetch(browser, params);
  }

  // Map tool names to browser wrapper methods
  const methodMap = {
    browser_create_context: () => browser.createContext(params),
    browser_close_context: () => browser.releaseContext(context_id),
    browser_list_contexts: () => browser.listContexts(),
    browser_navigate: () => browser.navigate(context_id, params.url, params.wait_until, params.timeout),
    browser_reload: () => browser.reload(context_id, params.ignore_cache, params.wait_until, params.timeout),
    browser_go_back: () => browser.goBack(context_id, params.wait_until, params.timeout),
    browser_go_forward: () => browser.goForward(context_id, params.wait_until, params.timeout),
    browser_click: () => browser.click(context_id, params.selector),
    browser_type: () => browser.type(context_id, params.selector, params.text),
    browser_pick: () => browser.pick(context_id, params.selector, params.value),
    browser_press_key: () => browser.pressKey(context_id, params.key),
    browser_submit_form: () => browser.submitForm(context_id),
    browser_hover: () => browser.hover(context_id, params.selector),
    browser_double_click: () => browser.doubleClick(context_id, params.selector),
    browser_right_click: () => browser.rightClick(context_id, params.selector),
    browser_clear_input: () => browser.clearInput(context_id, params.selector),
    browser_focus: () => browser.focus(context_id, params.selector),
    browser_blur: () => browser.blur(context_id, params.selector),
    browser_select_all: () => browser.selectAll(context_id, params.selector),
    browser_keyboard_combo: () => browser.keyboardCombo(context_id, params.combo),
    browser_drag_drop: () => browser.dragDrop(context_id, params.start_x, params.start_y, params.end_x, params.end_y, params.mid_points),
    browser_html5_drag_drop: () => browser.html5DragDrop(context_id, params.source_selector, params.target_selector),
    browser_mouse_move: () => browser.mouseMove(context_id, params.start_x, params.start_y, params.end_x, params.end_y, params.steps, params.stop_points),
    browser_upload_file: () => browser.uploadFile(context_id, params.selector, params.file_paths),
    browser_scroll_by: () => browser.scrollBy(context_id, params.x || 0, params.y),
    browser_scroll_to_element: () => browser.scrollToElement(context_id, params.selector),
    browser_scroll_to_top: () => browser.scrollToTop(context_id),
    browser_scroll_to_bottom: () => browser.scrollToBottom(context_id),
    browser_screenshot: () => browser.screenshot(context_id, params.mode, params.selector),
    browser_extract_text: () => browser.extractText(context_id, params.selector),
    browser_get_html: () => browser.getHTML(context_id, params.clean_level),
    browser_get_markdown: () => browser.getMarkdown(context_id, params.include_links, params.include_images, params.max_length),
    browser_extract_json: () => browser.extractJSON(context_id, params.template),
    browser_detect_site: () => browser.detectSite(context_id),
    browser_list_templates: () => browser.listTemplates(),
    browser_get_page_info: () => browser.getPageInfo(context_id),
    browser_highlight: () => browser.highlight(context_id, params.selector, params.border_color, params.background_color),
    browser_show_grid_overlay: () => browser.showGridOverlay(context_id, params.horizontal_lines, params.vertical_lines, params.line_color, params.text_color),
    browser_is_visible: () => browser.isVisible(context_id, params.selector),
    browser_is_enabled: () => browser.isEnabled(context_id, params.selector),
    browser_is_checked: () => browser.isChecked(context_id, params.selector),
    browser_get_attribute: () => browser.getAttribute(context_id, params.selector, params.attribute),
    browser_get_bounding_box: () => browser.getBoundingBox(context_id, params.selector),
    browser_nla: () => browser.executeNLA(context_id, params.command),
    browser_summarize_page: () => browser.summarizePage(context_id, params.force_refresh),
    browser_query_page: () => browser.queryPage(context_id, params.query),
    browser_llm_status: () => browser.getLLMStatus(),
    browser_wait: () => browser.wait(context_id, params.timeout),
    browser_wait_for_selector: () => browser.waitForSelector(context_id, params.selector, params.timeout),
    browser_wait_for_network_idle: () => browser.waitForNetworkIdle(context_id, params.idle_time, params.timeout),
    browser_wait_for_function: () => browser.waitForFunction(context_id, params.js_function, params.polling, params.timeout),
    browser_wait_for_url: () => browser.waitForURL(context_id, params.url_pattern, params.is_regex, params.timeout),
    browser_set_viewport: () => browser.setViewport(context_id, params.width, params.height),
    browser_evaluate: () => browser.evaluate(context_id, params.script || params.expression, params.return_value),
    browser_zoom_in: () => browser.zoomIn(context_id),
    browser_zoom_out: () => browser.zoomOut(context_id),
    browser_zoom_reset: () => browser.zoomReset(context_id),
    browser_get_console_log: () => browser.getConsoleLogs(context_id, params.level, params.filter, params.limit),
    browser_clear_console_log: () => browser.clearConsoleLogs(context_id),
    browser_start_video_recording: () => browser.startVideoRecording(context_id, params.fps, params.codec),
    browser_pause_video_recording: () => browser.pauseVideoRecording(context_id),
    browser_resume_video_recording: () => browser.resumeVideoRecording(context_id),
    browser_stop_video_recording: () => browser.stopVideoRecording(context_id),
    browser_get_video_recording_stats: () => browser.getVideoRecordingStats(context_id),
    browser_get_demographics: () => browser.getDemographics(),
    browser_get_location: () => browser.getLocation(),
    browser_get_datetime: () => browser.getDateTime(),
    browser_get_weather: () => browser.getWeather(),
    browser_detect_captcha: () => browser.detectCaptcha(context_id),
    browser_classify_captcha: () => browser.classifyCaptcha(context_id),
    browser_solve_text_captcha: () => browser.solveTextCaptcha(context_id, params.max_attempts),
    browser_solve_image_captcha: () => browser.solveImageCaptcha(context_id, params.max_attempts, params.provider),
    browser_solve_captcha: () => browser.solveCaptcha(context_id, params.max_attempts, params.provider),
    browser_get_cookies: () => browser.getCookies(context_id, params.url),
    browser_set_cookie: () => browser.setCookie(context_id, params.url, params.name, params.value, params),
    browser_delete_cookies: () => browser.deleteCookies(context_id, params.url, params.cookie_name),
    browser_set_proxy: () => browser.setProxy(context_id, params),
    browser_get_proxy_status: () => browser.getProxyStatus(context_id),
    browser_connect_proxy: () => browser.connectProxy(context_id),
    browser_disconnect_proxy: () => browser.disconnectProxy(context_id),
    browser_create_profile: () => browser.createProfile(params.name),
    browser_load_profile: () => browser.loadProfile(context_id, params.profile_path),
    browser_save_profile: () => browser.saveProfile(context_id, params.profile_path),
    browser_get_profile: () => browser.getProfile(context_id),
    browser_update_profile_cookies: () => browser.updateProfileCookies(context_id),
    browser_get_context_info: () => browser.getContextInfo(context_id),
    browser_list_frames: () => browser.listFrames(context_id),
    browser_switch_to_frame: () => browser.switchToFrame(context_id, params.frame_selector),
    browser_switch_to_main_frame: () => browser.switchToMainFrame(context_id),
    browser_add_network_rule: () => browser.addNetworkRule(context_id, params),
    browser_remove_network_rule: () => browser.removeNetworkRule(params.rule_id),
    browser_enable_network_interception: () => browser.enableNetworkInterception(context_id, params.enable),
    browser_get_network_log: () => browser.getNetworkLog(context_id),
    browser_clear_network_log: () => browser.clearNetworkLog(context_id),
    browser_set_download_path: () => browser.setDownloadPath(context_id, params.path),
    browser_get_downloads: () => browser.getDownloads(context_id),
    browser_wait_for_download: () => browser.waitForDownload(params.download_id, params.timeout),
    browser_cancel_download: () => browser.cancelDownload(params.download_id),
    browser_set_dialog_action: () => browser.setDialogAction(context_id, params.dialog_type, params.action, params.prompt_text),
    browser_get_pending_dialog: () => browser.getPendingDialog(context_id),
    browser_handle_dialog: () => browser.handleDialog(params.dialog_id, params.accept, params.response_text),
    browser_wait_for_dialog: () => browser.waitForDialog(context_id, params.timeout),
    browser_set_popup_policy: () => browser.setPopupPolicy(context_id, params.policy),
    browser_get_tabs: () => browser.getTabs(context_id),
    browser_switch_tab: () => browser.switchTab(context_id, params.tab_id),
    browser_close_tab: () => browser.closeTab(context_id, params.tab_id),
    browser_new_tab: () => browser.newTab(context_id, params.url),
    browser_get_active_tab: () => browser.getActiveTab(context_id),
    browser_get_tab_count: () => browser.getTabCount(context_id),
    browser_get_blocked_popups: () => browser.getBlockedPopups(context_id),
    browser_clipboard_read: () => browser.clipboardRead(context_id),
    browser_clipboard_write: () => browser.clipboardWrite(context_id, params.text),
    browser_clipboard_clear: () => browser.clipboardClear(context_id)
  };

  const method = methodMap[toolName];
  if (!method) {
    throw new Error(`Tool ${toolName} not implemented`);
  }

  return await method();
}

/**
 * List resources
 */
app.get('/mcp/resources', (req, res) => {
  res.json({
    resources: [{
      uri: '/static/component.js',
      name: 'Owl Browser UI Component',
      mimeType: 'application/javascript'
    }]
  });
});

/**
 * Get state
 */
app.get('/mcp/state', (req, res) => {
  res.json({
    connected: browserWrapper !== null,
    contexts: Array.from(activeContexts.values()),
    toolCount: TOOLS.length,
    categories: Object.keys(CATEGORIES).length
  });
});

// =============================================================================
// MCP STREAMABLE HTTP TRANSPORT (New transport for ChatGPT - March 2025)
// =============================================================================

const mcpSessions = new Map(); // Store session data

/**
 * MCP Endpoint - Streamable HTTP Transport
 * Single endpoint handling all MCP communication via POST
 * URL: /mcp
 */
app.post('/mcp', async (req, res) => {
  const sessionId = req.get('mcp-session-id');
  const acceptHeader = req.get('accept') || '';
  const contentType = req.get('content-type') || '';
  const message = req.body;

  // Log full details for debugging
  console.log('MCP POST request:', {
    sessionId: sessionId?.slice(0, 8),
    method: message?.method,
    id: message?.id,
    accept: acceptHeader.slice(0, 50),
    contentType: contentType,
    bodyType: typeof message,
    hasBody: !!message && Object.keys(message).length > 0
  });

  // If body is empty or not a proper JSON-RPC request, return error
  if (!message || typeof message !== 'object' || !message.method) {
    console.log('Invalid or empty request body:', JSON.stringify(message));
    // Return a proper JSON-RPC error for invalid requests
    res.setHeader('Content-Type', 'application/json');
    return res.status(400).json({
      jsonrpc: '2.0',
      id: message?.id || null,
      error: { code: -32600, message: 'Invalid Request: missing method' }
    });
  }

  try {
    const response = await handleMcpMessage(message, sessionId, res);

    if (response) {
      // Set session ID header if this is initialize response
      let responseSessionId = sessionId;
      if (message.method === 'initialize' && response.result) {
        responseSessionId = uuidv4();
        mcpSessions.set(responseSessionId, {
          clientInfo: message.params?.clientInfo || {},
          created: Date.now()
        });
        console.log('New session created:', responseSessionId.slice(0, 8));
      }

      // Always include session ID in response header if we have one
      if (responseSessionId) {
        res.setHeader('Mcp-Session-Id', responseSessionId);
      }

      // Return JSON response
      res.setHeader('Content-Type', 'application/json');
      res.json(response);
    } else {
      // Notification - no response needed
      res.status(202).end();
    }
  } catch (error) {
    console.error('MCP error:', error);
    res.status(500).json({
      jsonrpc: '2.0',
      id: message.id,
      error: { code: -32603, message: error.message }
    });
  }
});

/**
 * MCP GET endpoint - for SSE streaming (optional)
 */
app.get('/mcp', (req, res) => {
  const sessionId = req.get('mcp-session-id');
  console.log('MCP GET request (SSE stream):', { sessionId: sessionId?.slice(0, 8) });

  // Return SSE stream for server-initiated messages
  res.setHeader('Content-Type', 'text/event-stream');
  res.setHeader('Cache-Control', 'no-cache');
  res.setHeader('Connection', 'keep-alive');
  res.setHeader('X-Accel-Buffering', 'no');
  res.flushHeaders();

  // Keep alive
  const heartbeat = setInterval(() => res.write(':heartbeat\n\n'), 30000);

  req.on('close', () => {
    clearInterval(heartbeat);
    console.log('MCP SSE stream closed');
  });
});

/**
 * Handle MCP JSON-RPC message
 */
async function handleMcpMessage(message, sessionId, res) {
  // Handle notifications (no id = no response)
  if (message.method?.startsWith('notifications/') || message.id === undefined) {
    console.log('Notification received:', message.method);
    return null;
  }

  // Get session data
  const session = mcpSessions.get(sessionId) || {};

  // Handle methods
  if (message.method === 'initialize') {
    const clientVersion = message.params?.protocolVersion || '2024-11-05';
    const clientInfo = message.params?.clientInfo || {};
    console.log('Initialize:', clientInfo.name, 'protocol:', clientVersion);

    const initResponse = {
      jsonrpc: '2.0',
      id: message.id,
      result: {
        protocolVersion: clientVersion,
        serverInfo: {
          name: 'owl-browser',
          version: '1.0.0'
        },
        capabilities: {
          tools: { listChanged: true },
          resources: { subscribe: false, listChanged: false }
        }
      }
    };

    console.log('Initialize response:', JSON.stringify(initResponse, null, 2));
    return initResponse;
  }

  if (message.method === 'tools/list') {
    const clientInfo = session.clientInfo || {};
    const clientName = clientInfo.name || '';
    console.log('tools/list client:', clientName, 'session:', sessionId?.slice(0, 8));

    // Return ALL tools - ChatGPT connectors should see everything
    // search and fetch are REQUIRED for Deep Research, but we include all tools
    const toolsResponse = {
      jsonrpc: '2.0',
      id: message.id,
      result: {
        tools: TOOLS.map(t => ({
          name: t.name,
          description: t.description,
          inputSchema: t.inputSchema
        }))
      }
    };

    console.log('Tools list: returning', TOOLS.length, 'tools');

    return toolsResponse;
  }

  if (message.method === 'tools/call') {
    const { name, arguments: args } = message.params;
    console.log('Tool call:', name);

    const tool = TOOLS.find(t => t.name === name);
    if (!tool) {
      return {
        jsonrpc: '2.0',
        id: message.id,
        error: { code: -32601, message: `Unknown tool: ${name}` }
      };
    }

    try {
      const browser = await ensureBrowser();
      const result = await executeTool(browser, name, args || {});

      let content;
      if (result.screenshot) {
        content = [{ type: 'image', data: result.screenshot, mimeType: 'image/png' }];
      } else {
        content = [{ type: 'text', text: JSON.stringify(result, null, 2) }];
      }

      return {
        jsonrpc: '2.0',
        id: message.id,
        result: { content }
      };
    } catch (error) {
      return {
        jsonrpc: '2.0',
        id: message.id,
        result: {
          content: [{ type: 'text', text: `Error: ${error.message}` }],
          isError: true
        }
      };
    }
  }

  if (message.method === 'resources/list') {
    return {
      jsonrpc: '2.0',
      id: message.id,
      result: { resources: [] }
    };
  }

  if (message.method === 'ping') {
    return { jsonrpc: '2.0', id: message.id, result: {} };
  }

  // Unknown method
  return {
    jsonrpc: '2.0',
    id: message.id,
    error: { code: -32601, message: `Unknown method: ${message.method}` }
  };
}

// =============================================================================
// LEGACY SSE TRANSPORT (Keep for backwards compatibility)
// =============================================================================

const sseClients = new Map();
const sessionClientInfo = new Map();

/**
 * Legacy SSE endpoint
 */
app.get('/sse', (req, res) => {
  console.log('Legacy SSE connection');

  res.setHeader('Content-Type', 'text/event-stream');
  res.setHeader('Cache-Control', 'no-cache, no-transform');
  res.setHeader('Connection', 'keep-alive');
  res.setHeader('X-Accel-Buffering', 'no');
  res.flushHeaders();

  const sessionId = uuidv4();
  sseClients.set(sessionId, res);

  const messageEndpoint = `${req.protocol}://${req.get('host')}/message?sessionId=${sessionId}`;
  res.write(`event: endpoint\ndata: ${messageEndpoint}\n\n`);

  const heartbeat = setInterval(() => res.write(':heartbeat\n\n'), 30000);

  req.on('close', () => {
    clearInterval(heartbeat);
    sseClients.delete(sessionId);
    sessionClientInfo.delete(sessionId);
  });
});

/**
 * Legacy message endpoint
 */
app.post('/message', async (req, res) => {
  const { sessionId } = req.query;
  const message = req.body;

  const response = await handleMcpMessage(message, sessionId, res);

  if (response) {
    // Store client info for this session on initialize
    if (message.method === 'initialize' && message.params?.clientInfo) {
      sessionClientInfo.set(sessionId, message.params.clientInfo);
    }

    // Send via SSE if connected
    const sseClient = sseClients.get(sessionId);
    if (sseClient) {
      sseClient.write(`event: message\ndata: ${JSON.stringify(response)}\n\n`);
    }

    res.json(response);
  } else {
    res.status(202).end();
  }
});

// =============================================================================
// OAUTH 2.1 ENDPOINTS (RFC 7591 Dynamic Client Registration)
// =============================================================================

const oauthClients = new Map();
const authorizationCodes = new Map();
const accessTokens = new Map();

/**
 * RFC 7591 Dynamic Client Registration
 * POST /oauth/register
 */
app.post('/oauth/register', (req, res) => {
  const clientId = `owl_${uuidv4()}`;
  const clientSecret = `owl_secret_${uuidv4()}`;
  const issuedAt = Math.floor(Date.now() / 1000);

  const clientData = {
    client_id: clientId,
    client_secret: clientSecret,
    client_id_issued_at: issuedAt,
    client_secret_expires_at: 0, // Never expires
    redirect_uris: req.body.redirect_uris || [],
    token_endpoint_auth_method: req.body.token_endpoint_auth_method || 'client_secret_post',
    grant_types: req.body.grant_types || ['authorization_code', 'refresh_token'],
    response_types: req.body.response_types || ['code'],
    client_name: req.body.client_name || 'ChatGPT Client',
    scope: req.body.scope || 'browser:read browser:write browser:admin'
  };

  oauthClients.set(clientId, clientData);

  // RFC 7591 requires 201 Created status
  res.status(201).json(clientData);
});

/**
 * Authorization endpoint - shows authorization page
 * GET /oauth/authorize
 */
app.get('/oauth/authorize', (req, res) => {
  const { client_id, redirect_uri, state, code_challenge, code_challenge_method, scope } = req.query;

  console.log('Authorization request:', { client_id, redirect_uri, state, scope });

  // For ChatGPT integration, auto-approve and redirect
  // In production, you might want to show an authorization page
  const code = uuidv4();
  authorizationCodes.set(code, {
    client_id: client_id || 'chatgpt',
    redirect_uri,
    code_challenge,
    code_challenge_method: code_challenge_method || 'S256',
    scope: scope || 'browser:read browser:write',
    created: Date.now(),
    expires: Date.now() + 600000 // 10 minutes
  });

  // If no redirect_uri, show error
  if (!redirect_uri) {
    return res.status(400).send(`
      <!DOCTYPE html>
      <html>
      <head><title>Error - Owl Browser</title></head>
      <body style="font-family: system-ui; padding: 40px; text-align: center;">
        <h1>Missing redirect_uri</h1>
        <p>The authorization request is missing the redirect_uri parameter.</p>
      </body>
      </html>
    `);
  }

  // Build redirect URL with code
  try {
    const redirectUrl = new URL(redirect_uri);
    redirectUrl.searchParams.set('code', code);
    if (state) redirectUrl.searchParams.set('state', state);

    console.log('Redirecting to:', redirectUrl.toString());
    res.redirect(redirectUrl.toString());
  } catch (e) {
    console.error('Invalid redirect_uri:', redirect_uri, e);
    res.status(400).json({ error: 'invalid_request', error_description: 'Invalid redirect_uri' });
  }
});

/**
 * Token endpoint
 * POST /oauth/token
 */
app.post('/oauth/token', (req, res) => {
  console.log('Token request body:', req.body);
  console.log('Token request content-type:', req.get('content-type'));

  const { grant_type, code, redirect_uri, code_verifier, refresh_token, client_id, client_secret } = req.body;

  console.log('Token request parsed:', {
    grant_type,
    code: code ? code.slice(0, 8) + '...' : 'missing',
    redirect_uri,
    client_id: client_id ? client_id.slice(0, 15) + '...' : 'missing'
  });

  if (grant_type === 'authorization_code') {
    const authCode = authorizationCodes.get(code);

    if (!authCode) {
      console.log('Invalid code. Received:', code?.slice(0, 8));
      console.log('Available codes:', [...authorizationCodes.keys()].map(k => k.slice(0, 8)));
      return res.status(400).json({ error: 'invalid_grant', error_description: 'Invalid authorization code' });
    }

    if (authCode.expires < Date.now()) {
      authorizationCodes.delete(code);
      return res.status(400).json({ error: 'invalid_grant', error_description: 'Authorization code expired' });
    }

    // PKCE validation - skip strict check for ChatGPT integration
    console.log('PKCE check:', {
      has_challenge: !!authCode.code_challenge,
      has_verifier: !!code_verifier,
      challenge: authCode.code_challenge?.slice(0, 10) + '...'
    });

    // Generate tokens
    const accessToken = `owl_at_${uuidv4()}`;
    const newRefreshToken = `owl_rt_${uuidv4()}`;

    accessTokens.set(accessToken, {
      client_id: authCode.client_id || client_id,
      scope: authCode.scope,
      created: Date.now(),
      expires: Date.now() + 3600000 // 1 hour
    });

    authorizationCodes.delete(code);

    console.log('Token issued successfully for client:', authCode.client_id || client_id);

    res.json({
      access_token: accessToken,
      token_type: 'Bearer',
      expires_in: 3600,
      refresh_token: newRefreshToken,
      scope: authCode.scope
    });
  } else if (grant_type === 'refresh_token') {
    // Handle refresh token
    const accessToken = `owl_at_${uuidv4()}`;
    const newRefreshToken = `owl_rt_${uuidv4()}`;

    res.json({
      access_token: accessToken,
      token_type: 'Bearer',
      expires_in: 3600,
      refresh_token: newRefreshToken,
      scope: 'browser:read browser:write'
    });
  } else {
    res.status(400).json({ error: 'unsupported_grant_type' });
  }
});

/**
 * Token introspection (optional but useful for debugging)
 * POST /oauth/introspect
 */
app.post('/oauth/introspect', (req, res) => {
  const { token } = req.body;
  const tokenData = accessTokens.get(token);

  if (tokenData && tokenData.expires > Date.now()) {
    res.json({
      active: true,
      client_id: tokenData.client_id,
      scope: tokenData.scope,
      exp: Math.floor(tokenData.expires / 1000)
    });
  } else {
    res.json({ active: false });
  }
});

// =============================================================================
// HEALTH
// =============================================================================

app.get('/health', (req, res) => {
  res.json({
    status: 'healthy',
    service: 'owl-browser-chatgpt-app',
    version: '1.0.0',
    tools: TOOLS.length,
    categories: Object.keys(CATEGORIES).length,
    browserConnected: browserWrapper !== null
  });
});

// =============================================================================
// START SERVER
// =============================================================================

app.listen(PORT, () => {
  console.log(`
  ╔═══════════════════════════════════════════════════════════════╗
  ║                                                               ║
  ║   🦉 Owl Browser ChatGPT App                                 ║
  ║                                                               ║
  ║   Server running at: http://localhost:${PORT}                  ║
  ║   MCP Endpoint:      http://localhost:${PORT}/mcp              ║
  ║   Legacy SSE:        http://localhost:${PORT}/sse              ║
  ║                                                               ║
  ║   Available tools: ${TOOLS.length}                                       ║
  ║   Categories: ${Object.keys(CATEGORIES).length}                                           ║
  ║                                                               ║
  ║   To use with ChatGPT:                                        ║
  ║   1. Run: npm run tunnel (requires ngrok)                     ║
  ║   2. Add URL to ChatGPT: https://your-domain/mcp              ║
  ║                                                               ║
  ╚═══════════════════════════════════════════════════════════════╝
  `);

  // Check for browser wrapper
  if (BrowserWrapper) {
    console.log('  ✓ Browser wrapper loaded');
  } else {
    console.log('  ⚠ Browser wrapper not available (mock mode)');
  }
});

export default app;
