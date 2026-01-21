# Owl Browser

**The AI-First Browser Built BY AI FOR AI**

A revolutionary Chromium-based browser purpose-built for AI automation and MCP (Model Context Protocol) integration. Unlike traditional automation tools, Owl Browser is a **native browser** with AI-first capabilities built directly into the browser core.

**Website**: [www.owlbrowser.net](https://www.owlbrowser.net)
**By**: [Olib AI](https://www.olib.ai)

## Platform Support

| Platform | Architecture | Headless Browser | UI Browser | Status |
|----------|--------------|------------------|------------|--------|
| macOS    | ARM64 (M1/M2/M3) | Full Support | Full Support | Production Ready |
| macOS    | x64 (Intel) | Full Support | Full Support | Production Ready |
| **Ubuntu 22.04+** | **x64 (amd64)** | **Full Support** | **Stub Only** | **Headless Production Ready** |
| **Ubuntu 22.04+** | **ARM64 (aarch64)** | **Full Support** | **Stub Only** | **Headless Production Ready** |

**Cross-Platform Features:**
- All AI-first capabilities work on both macOS and Ubuntu
- Built-in LLM (Qwen3-VL-2B) with GPU acceleration (Metal on macOS, CPU/CUDA on Ubuntu)
- Third-party LLM support (OpenAI, Claude, local servers) - per-context configuration
- Complete headless automation with MCP server integration
- All 100+ MCP tools available on all platforms
- Comprehensive test suite runs on both platforms

For Ubuntu-specific setup and details, see [UBUNTU-SUPPORT.md](UBUNTU-SUPPORT.md).

## Why Owl Browser?

- **AI-Native API**: Natural language interaction instead of CSS selectors
- **Flexible LLM Integration**: Built-in Qwen3-VL-2B OR any OpenAI-compatible API (GPT-4, Claude, local servers)
- **Lightning Fast**: Native C++ with <1s cold start, no CDP overhead
- **Maximum Stealth**: No WebDriver flags, no detection artifacts
- **Built-in Ad Blocker**: 72 domains blocked (ads/analytics/trackers)
- **Full HD Screenshots**: 1920x1080 high-quality PNG captures
- **Video Recording**: Record browser sessions with off-screen rendering
- **Context-Aware**: Built-in geolocation, datetime, and weather detection
- **High Performance**: Off-screen rendering, session pooling
- **100+ MCP Tools**: Complete browser automation via Model Context Protocol

## MCP Server Integration

The browser includes a full-featured **MCP (Model Context Protocol) server** with 100+ tools for AI agent integration. Perfect for Claude Desktop, AI workflows, and automation scripts.

### MCP Tools (100+ Total)

**Core Browser Control:**
- `browser_create_context` - Create new browser context (supports LLM config: built-in or third-party APIs)
- `browser_close_context`, `browser_list_contexts`
- `browser_navigate`, `browser_reload`, `browser_go_back`, `browser_go_forward`
- `browser_get_page_info`, `browser_set_viewport`

**Interaction:**
- `browser_click`, `browser_type`, `browser_pick`, `browser_press_key`, `browser_submit_form`
- `browser_hover`, `browser_double_click`, `browser_right_click` - Advanced click actions
- `browser_clear_input`, `browser_focus`, `browser_blur`, `browser_select_all` - Input control
- `browser_keyboard_combo` - Press key combinations (Ctrl+A, etc.)
- `browser_drag_drop`, `browser_html5_drag_drop`, `browser_mouse_move` - Human-like mouse movement
- `browser_upload_file` - Upload files to input elements
- `browser_scroll_by`, `browser_scroll_to_element`, `browser_scroll_to_top`, `browser_scroll_to_bottom`
- `browser_wait_for_selector`, `browser_wait`, `browser_wait_for_network_idle`
- `browser_wait_for_function`, `browser_wait_for_url` - Advanced wait conditions

**Element State:**
- `browser_is_visible`, `browser_is_enabled`, `browser_is_checked` - Check element state
- `browser_get_attribute` - Get element attribute value
- `browser_get_bounding_box` - Get element position and size

**JavaScript Evaluation:**
- `browser_evaluate` - Execute JavaScript in page context

**Frame/Iframe Handling:**
- `browser_list_frames` - List all frames on the page
- `browser_switch_to_frame` - Switch to an iframe
- `browser_switch_to_main_frame` - Switch back to main frame

**Content Extraction:**
- `browser_extract_text`, `browser_screenshot`, `browser_highlight`
- `browser_show_grid_overlay` - Show XY coordinate grid overlay
- `browser_get_html`, `browser_get_markdown`, `browser_extract_json`
- `browser_detect_site`, `browser_list_templates`

**AI-Powered Features:**
- `browser_nla` - Natural Language Actions (e.g., "go to google.com and search for banana")
- `browser_summarize_page` - LLM-powered page summarization
- `browser_query_page` - Ask questions about page content
- `browser_llm_status` - Check if LLM is ready

**Video Recording:**
- `browser_start_video_recording`, `browser_pause_video_recording`
- `browser_resume_video_recording`, `browser_stop_video_recording`
- `browser_get_video_recording_stats`

**Demographics & Context:**
- `browser_get_demographics` - Full context (location, datetime, weather)
- `browser_get_location` - IP-based geolocation
- `browser_get_datetime` - Current date/time with timezone
- `browser_get_weather` - Real-time weather conditions

**CAPTCHA Solving (Experimental):**
- `browser_detect_captcha`, `browser_classify_captcha`
- `browser_solve_text_captcha`, `browser_solve_image_captcha`
- `browser_solve_captcha` - Auto-detect and solve

**Cookie Management:**
- `browser_get_cookies` - Get all cookies (optionally filtered by URL)
- `browser_set_cookie` - Set a cookie with full attribute control
- `browser_delete_cookies` - Delete cookies (all, by URL, or specific cookie)

**Profile Management:**
- `browser_create_profile` - Create new profile with randomized fingerprints
- `browser_load_profile` - Load profile into existing context
- `browser_save_profile` - Save context state to profile file
- `browser_get_profile` - Get current profile state as JSON
- `browser_update_profile_cookies` - Update profile with current cookies

**Proxy Management:**
- `browser_set_proxy` - Configure proxy with stealth features
- `browser_get_proxy_status` - Get proxy configuration and status
- `browser_connect_proxy` - Enable/connect proxy
- `browser_disconnect_proxy` - Disable/disconnect proxy

**Network Interception:**
- `browser_add_network_rule` - Add URL interception rule (block, mock, redirect)
- `browser_remove_network_rule` - Remove an interception rule by ID
- `browser_enable_network_interception` - Enable/disable interception
- `browser_get_network_log` - Get captured network requests/responses
- `browser_clear_network_log` - Clear captured network data

**File Downloads:**
- `browser_set_download_path` - Configure download directory per context
- `browser_get_downloads` - List all downloads with status and progress
- `browser_wait_for_download` - Wait for a download to complete
- `browser_cancel_download` - Cancel an active download

**Dialog Handling:**
- `browser_set_dialog_action` - Configure auto-handling policy for dialogs
- `browser_handle_dialog` - Accept/dismiss specific dialog with optional text
- `browser_get_pending_dialog` - Get current pending dialog info
- `browser_wait_for_dialog` - Wait for a dialog to appear

**Multi-Tab/Window Management:**
- `browser_new_tab` - Create new tab within context
- `browser_get_tabs` - List all tabs with URLs and status
- `browser_switch_tab` - Switch to a specific tab
- `browser_close_tab` - Close a tab
- `browser_get_active_tab` - Get currently active tab
- `browser_get_tab_count` - Get number of open tabs
- `browser_set_popup_policy` - Configure popup handling (allow, block, new_tab)
- `browser_get_blocked_popups` - Get list of blocked popup URLs

**License Management:**
- `browser_get_license_status` - Check license validation status
- `browser_get_license_info` - Get detailed license information
- `browser_get_hardware_fingerprint` - Get hardware fingerprint for licensing
- `browser_add_license` - Add/install a license file
- `browser_remove_license` - Remove installed license

### Using the MCP Server

```bash
# Start MCP server (stdio mode for Claude Desktop)
node src/mcp-server.cjs

# Or use standalone build
node dist/mcp-server-standalone.cjs
```

The MCP server provides all browser capabilities via a simple tool-based interface. Claude Desktop can use these tools to browse the web, extract information, interact with pages, and even solve CAPTCHAs.

## Node.js/TypeScript SDK

**NEW!** We now have a fully-featured SDK for Node.js/TypeScript developers!

### Quick Start

```typescript
import { Browser } from '@olib-ai/owl-browser-sdk';

const browser = new Browser();
await browser.launch();

const page = await browser.newPage();
await page.goto('https://example.com');

// Natural language selectors!
await page.click('search button');
await page.type('email input', 'user@example.com');

const screenshot = await page.screenshot();
await browser.close();
```

### SDK Features

- Simple, clean API (3-4 lines of code)
- Full TypeScript support with type definitions
- Natural language selectors built-in
- All browser features available (AI, video, NLA, CAPTCHA, etc.)
- Multiple page/context support
- 6+ ready-to-run examples

### Documentation

- [SDK Quick Start Guide](SDK-QUICKSTART.md)
- [Full SDK Documentation](sdk/README.md)
- [SDK Examples](sdk/examples/)

### Installation

```bash
# Install from npm
npm install @olib-ai/owl-browser-sdk

# Or build SDK from source (from root)
npm run build:sdk
```

## HTTP Server (REST API)

For deployments where you need HTTP/REST access to the browser (Docker, microservices, language-agnostic clients), the project includes a standalone HTTP server.

### Quick Start

```bash
# Build the HTTP server
cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make owl_http_server

# Run with required environment variables
OWL_HTTP_TOKEN=your-secret-token \
OWL_BROWSER_PATH=/path/to/owl_browser \
./build/Release/owl_http_server
```

### Key Features

- **REST API**: All 100+ browser tools available via HTTP endpoints
- **Bearer Authentication**: Secure token-based authentication
- **Docker Ready**: Designed for containerized deployments
- **Language Agnostic**: Use from Python, Node.js, Go, or any HTTP client

### Example Usage

```bash
# Check server health
curl http://localhost:8080/health

# Create a browser context
curl -X POST -H "Authorization: Bearer your-token" \
  http://localhost:8080/execute/browser_create_context

# Navigate to a URL
curl -X POST -H "Authorization: Bearer your-token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_000001", "url": "https://example.com"}' \
  http://localhost:8080/execute/browser_navigate

# Take a screenshot
curl -X POST -H "Authorization: Bearer your-token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_000001"}' \
  http://localhost:8080/execute/browser_screenshot
```

### Documentation

- [HTTP Server README](http-server/README.md) - Setup, configuration, and deployment guide
- [API Reference](http-server/API.md) - Complete API documentation with all endpoints and parameters

## Revolutionary Features

### AI-First Methods

Instead of fighting with CSS selectors, use natural language:

```typescript
// Traditional approach (brittle, breaks easily)
await browser.click('#submit-btn-xyz-123');

// AI-First approach (robust, intelligent)
await browser.aiClick('submit button');
await browser.aiType('email input', 'user@example.com');
await browser.aiExtract('main content');
```

**Semantic Matcher**: Lightning-fast element finding with ~2ms performance:
- **Natural Language**: "search box", "login button", "submit button"
- **Fuzzy Matching**: Keyword extraction and normalization
- **Role Inference**: Automatic element role detection (search_input, submit_button, etc.)
- **Multi-Source Scoring**: aria-label (1.2x priority), placeholder (1.1x), text, title, nearby text
- **Keyword Boosting**: "box"→textarea/input (+0.3), "button"→button/submit (+0.3), "link"→anchor (+0.3)
- **Tag Priority**: Prefers interactive elements (anchor > button > input > div)
- **Confidence Threshold**: 0.3+ for relevance, sorted by confidence score

### Video Recording

**NEW!** Record your browser automation sessions with off-screen rendering:

```javascript
// Via MCP
await browser.startVideoRecording(context_id);
// ... perform actions ...
const videoPath = await browser.stopVideoRecording(context_id);

// Via SDK
await page.startRecording();
await page.goto('https://example.com');
await page.click('search button');
const videoPath = await page.stopRecording();
```

**Features:**
- Off-screen rendering (no visible window required)
- ffmpeg-based H.264/MP4 encoding
- 30fps continuous recording
- Pause/resume support
- Works on both macOS and Ubuntu

### Natural Language Actions (NLA)

**Execute multi-step tasks with a single command:**

```javascript
// Via MCP tool
await browser.nla(context_id, "go to google.com and search for banana");

// Via SDK
await page.nla("find me a restaurant nearby");
await page.nla("fill in the contact form");
```

The browser automatically:
1. Plans the required steps
2. Executes actions sequentially
3. Handles errors and retries
4. Returns detailed execution results

### LLM Integration (On-Device + Third-Party APIs)

**Powered by llama.cpp + Qwen3 Vision 2B (built-in) OR any OpenAI-compatible API**

The browser supports **two LLM modes** for maximum flexibility:

#### Built-in LLM (On-Device Intelligence)

The browser has an integrated **on-device vision-language model** that runs as a subprocess, providing intelligent automation capabilities without cloud APIs:

- **Visual Understanding**: Vision model can analyze screenshots and webpage visuals
- **Element Understanding**: LLM-powered semantic matching for ambiguous queries
- **Content Classification**: Automatically classify page elements by purpose and type
- **Intent Matching**: Match natural language queries to page elements
- **Smart Extraction**: Understand context for better data extraction

**Performance:**
- **Model Size**: 2 billion parameters (Qwen3-VL-2B quantized)
- **Inference Speed**: 50-200ms per completion
- **Memory**: ~2GB additional (model + runtime)
- **Startup**: 15-25s model loading time
- **Acceleration**: Metal (macOS), CPU/CUDA (Ubuntu)
- **Concurrency**: 4 parallel requests

**Technical Details:**
- **Model**: Qwen3-VL-2B (Q4_K_M quantized) - vision-language model with multimodal understanding
- **Capabilities**: Text + Vision (can process screenshots and images)
- **Server**: llama.cpp server (OpenAI-compatible API)
- **API**: `/v1/chat/completions` endpoint (localhost:8095)
- **Thinking Tag Cleanup**: Automatic removal of `<think></think>` tags from responses
- **Security**: Built-in guardrails protect against prompt injection attacks from malicious websites

#### Third-Party LLM APIs (OpenAI, Anthropic, etc.)

The browser also supports **any OpenAI-compatible API** for LLM capabilities, configured on a **per-context basis**:

**Supported Providers:**
- OpenAI (GPT-4, GPT-4 Vision, etc.)
- Anthropic Claude (via OpenAI-compatible proxy)
- Local LLM servers (LM Studio, Ollama, llama.cpp server, etc.)
- Any API implementing OpenAI's `/v1/chat/completions` endpoint

**Configuration:**
```javascript
// Via MCP tool - create context with external LLM
await browser.createContext({
  llm_enabled: true,
  llm_use_builtin: false,
  llm_endpoint: 'http://127.0.0.1:1234',      // Your LLM server
  llm_model: 'gpt-4-vision-preview',           // Model name
  llm_api_key: 'sk-...'                        // API key (optional for local)
});

// Via SDK
const page = await browser.newPage({
  llmConfig: {
    enabled: true,
    useBuiltin: false,
    endpoint: 'https://api.openai.com/v1',
    model: 'gpt-4-vision-preview',
    apiKey: process.env.OPENAI_API_KEY
  }
});
```

**Use Cases:**
- **Use GPT-4 Vision** for better visual understanding and accuracy
- **Use Claude** for complex reasoning and long-context tasks
- **Use local LLM server** for privacy-sensitive automation
- **Mix and match**: Different contexts can use different LLM providers

**UI Version Configuration:**
The UI version reads LLM config from `~/.owl_browser/llm_config.json`:
```json
{
  "enabled": true,
  "use_builtin": false,
  "external_endpoint": "http://127.0.0.1:1234",
  "external_model": "gpt-4-vision-preview",
  "external_api_key": "sk-..."
}
```

### Demographics & Context Awareness

**Powered by MaxMind GeoLite2 + Open-Meteo**

The browser has built-in context awareness with automatic detection of:

- **Geolocation**: IP-based location (city, region, country, coordinates, timezone)
- **Date/Time**: Current date, time, day of week, timezone-aware
- **Weather**: Real-time weather conditions (temperature, humidity, wind speed)

**Use Cases:**
```javascript
// NLA automatically uses location context
await browser.nla('find me a restaurant');
// → Searches for "restaurants near me in [YOUR_CITY]"

// Access demographics directly via MCP tools
const demographics = await browser.getDemographics();
const location = await browser.getLocation();
const weather = await browser.getWeather();
```

**Integration with NLA:**
- Makes location-aware queries work naturally
- "Find me a hotel" → automatically uses your city
- "What's the weather like" → uses your coordinates
- "Book a restaurant nearby" → searches in your area

### CAPTCHA Solver (Experimental)

**Powered by Qwen3 Vision Model + Native C++ DOM Manipulation**

The browser includes experimental CAPTCHA solving capabilities for internal testing:

**Supported CAPTCHA Types:**
- **Text-based CAPTCHAs**: Distorted alphanumeric characters (5 characters)
- **Image Selection CAPTCHAs**: Grid-based image challenges (3x3 grids)

**How It Works:**
1. **Detection**: Automatically detects CAPTCHA presence on page
2. **Classification**: Identifies CAPTCHA type (text vs. image)
3. **Vision Analysis**: Uses on-device Qwen3-VL model to analyze CAPTCHA images
4. **Native Interaction**: Uses IPC messages to renderer process for DOM manipulation
5. **Verification**: Waits for form submission and validates success

**Performance:**
- Detection: <50ms
- Classification: <100ms
- Vision OCR: 50-200ms (GPU accelerated)
- Total solve time: ~1-2 seconds

**Note:** This is an experimental feature for internal testing only.

### UI Version with Agent Interface (macOS Only)

**Native macOS Application with Visual Agent Overlay**

The browser includes a full-featured UI version with real-time task visualization:

**Key Features:**
1. **Homepage with Navigation** - URL bar, AI assistant trigger, developer tools
2. **AI Assistant (Agent Mode)** - Natural language command input with auto-execution
3. **Real-Time Task Visualization** - Color-coded tasks (Gray=Pending, Blue=Active, Green=Completed)
4. **Developer Playground** - Test commands, inspect execution, debug agent behavior

**Usage:**
```bash
# Launch UI version (macOS only)
./build/Release/owl_browser_ui.app/Contents/MacOS/owl_browser_ui

# Launch headless version (all platforms)
./build/Release/owl_browser.app/Contents/MacOS/owl_browser  # macOS
./build/owl_browser  # Ubuntu
```

**Note:** Ubuntu UI is currently a stub. Use headless mode on Ubuntu.

### Built-in Resource Blocker

Automatically blocks:
- **31 ad domains** (doubleclick.net, googlesyndication.com, etc.)
- **20 analytics domains** (google-analytics.com, mixpanel.com, etc.)
- **21 tracker domains** (scorecardresearch.com, facebook pixel, etc.)

**Real-world performance**: 6-8% of requests blocked on major news sites like CNN.

## Architecture

```
┌────────────────────────────────────────────────────────────┐
│  MCP Server (100+ Tools) / SDK / External Clients          │
│  - Claude Desktop, AI Agents, Automation Scripts           │
│  - @modelcontextprotocol/sdk for LLM integration          │
│  - TypeScript/Python/Node.js SDKs                         │
└──────────────────┬─────────────────────────────────────────┘
                   │ JSON IPC (stdin/stdout)
┌──────────────────▼─────────────────────────────────────────┐
│  Owl Browser Native Process (C++ / CEF)                    │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ LLM Layer (Built-in OR Third-Party APIs)             │  │
│  │ ┌────────────────────┐  ┌──────────────────────────┐│  │
│  │ │ Built-in:          │  │ LLM Client (C++)        ││  │
│  │ │ llama-server       │←─│ - OpenAI API compat     ││  │
│  │ │ - Qwen3-VL-2B      │  │ - Thinking tag cleanup  ││  │
│  │ │ - Vision + Text    │  │ - Multimodal support    ││  │
│  │ │ - Metal/CPU/CUDA   │  │ - Element classification││  │
│  │ │ - Port 8095        │  │ - Prompt injection guard││  │
│  │ │ - 4 parallel reqs  │  │                         ││  │
│  │ └────────────────────┘  │ Third-Party APIs:       ││  │
│  │                         │ - OpenAI (GPT-4V)       ││  │
│  │                         │ - Claude (via proxy)    ││  │
│  │                         │ - Local LLM servers     ││  │
│  │                         │ - Per-context config    ││  │
│  │                         └──────────────────────────┘│  │
│  └──────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ Profile Manager                                      │  │
│  │ - Browser fingerprint generation & persistence       │  │
│  │ - Cookie storage & session persistence               │  │
│  │ - Per-profile LLM & proxy configuration              │  │
│  └──────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ AI Intelligence Layer                                │  │
│  │ - Natural language element finding (~2ms)            │  │
│  │ - LLM-powered semantic matching                      │  │
│  │ - Page structure analysis                            │  │
│  │ - Main content extraction (readability)              │  │
│  │ - Natural Language Actions (NLA)                     │  │
│  └──────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ Video Recording Layer                                │  │
│  │ - Off-screen rendering • ffmpeg encoding (H.264)     │  │
│  │ - 30fps recording • Pause/resume support             │  │
│  └──────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ Resource Blocker (72 domains)                        │  │
│  │ - Ad blocking • Analytics blocking • Tracker blocking│  │
│  └──────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ Stealth Layer                                        │  │
│  │ - WebDriver removal • Fingerprint protection         │  │
│  │ - Canvas/WebGL/Audio noise injection                 │  │
│  └──────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ Chromium Embedded Framework (CEF)                    │  │
│  │ - 1920x1080 off-screen rendering                     │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

## Prerequisites

- **Node.js**: 18.0.0 or higher
- **CMake**: 3.21 or higher
- **C++ Compiler**:
  - macOS: Xcode Command Line Tools
  - Ubuntu: GCC 9+ or Clang 10+
- **Python**: 3.6+ (for node-gyp)

### macOS Setup

```bash
xcode-select --install
brew install cmake node libmaxminddb curl
```

### Ubuntu Setup

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake nodejs npm \
    libmaxminddb-dev libcurl4-openssl-dev zlib1g-dev \
    libgtk-3-dev libglib2.0-dev libnspr4-dev libnss3-dev \
    libatk1.0-dev libatk-bridge2.0-dev libcups2-dev \
    libdrm-dev libxkbcommon-dev libxcomposite-dev \
    libxdamage-dev libxrandr-dev libgbm-dev \
    libpango1.0-dev libasound2-dev
```

For complete Ubuntu setup instructions, see [UBUNTU-SUPPORT.md](UBUNTU-SUPPORT.md).

## Installation

### 1. Clone Repository

```bash
git clone https://github.com/Olib-AI/owl-browser.git
cd owl-browser
```

### 2. Download Dependencies

The project includes automated download scripts for CEF and llama.cpp binaries:

```bash
# Download CEF binary (~500MB, auto-detects your platform/architecture)
npm run download:cef

# Download llama.cpp binaries and AI model (~2GB)
npm run download:llama
```

The scripts automatically detect your platform (macOS/Ubuntu) and architecture (x64/ARM64) and download the correct binaries to `third_party/cef_[platform]/` and `third_party/llama_[platform]/`.

### 3. Build Browser

```bash
# Full build (browser + MCP server + SDK)
npm run build

# Or build individually
npm run build      # Build native browser
npm run build:mcp      # Build MCP server
npm run build:sdk      # Build TypeScript SDK
```

**Build outputs:**
- macOS: `build/Release/owl_browser.app`
- Ubuntu: `build/owl_browser`

## Usage

### Via MCP Server (Recommended for AI Agents)

```bash
# Start MCP server
node src/mcp-server.cjs
```

Use all 95+ MCP tools from Claude Desktop or your AI agent. See [MCP Server Integration](#mcp-server-integration) above for the complete tool list.

### Via SDK (Recommended for Developers)

```typescript
import { Browser } from '@olib-ai/owl-browser-sdk';

const browser = new Browser();
await browser.launch();

const page = await browser.newPage();
await page.goto('https://example.com');
await page.click('submit button');  // Natural language!

await browser.close();
```

See [SDK Documentation](sdk/README.md) for complete API reference and examples.

### Via JSON IPC (Advanced)

For direct browser control:

```bash
# macOS
./build/Release/owl_browser.app/Contents/MacOS/owl_browser

# Ubuntu
./build/owl_browser
```

Send JSON commands via stdin:

```javascript
// Create context (uses built-in LLM by default)
{"id": 1, "method": "createContext"}
// Response: {"id": 1, "result": "ctx_000001"}

// Create context with third-party LLM (OpenAI, Claude, etc.)
{
  "id": 2,
  "method": "createContext",
  "llm_enabled": true,
  "llm_use_builtin": false,
  "llm_endpoint": "https://api.openai.com/v1",
  "llm_model": "gpt-4-vision-preview",
  "llm_api_key": "sk-..."
}
// Response: {"id": 2, "result": "ctx_000002"}

// Navigate
{"id": 3, "method": "navigate", "context_id": "ctx_000001", "url": "https://example.com"}

// Natural Language Action (uses context's LLM config)
{"id": 4, "method": "nla", "context_id": "ctx_000001", "command": "search for banana"}

// Screenshot
{"id": 5, "method": "screenshot", "context_id": "ctx_000001"}
// Response: {"id": 5, "result": "base64_png_data..."}
```

## Testing

The project includes a comprehensive test suite with 10 tests:

```bash
# Run default test (NLA Google search)
npm run test

# Run specific tests
npm run test:browserscan      # Browser fingerprinting test
npm run test:demographics     # Location/weather test
npm run test:sdk              # SDK quick test

# Or run individual test files
node tests/test_nla_google.cjs              # Natural Language Actions
node tests/test_video_recording.cjs         # Video recording
node tests/test_captcha_forms.cjs           # CAPTCHA solving
node tests/test_llm_query.cjs               # LLM page analysis
node tests/test_guardrails.cjs              # Prompt injection protection
node tests/test_text_captcha_simple.cjs     # Text CAPTCHA only
node tests/test_image_captcha_simple.cjs    # Image CAPTCHA only
node tests/test_mcp_google_banana.cjs       # MCP server test
```

**All tests are cross-platform** and work on both macOS and Ubuntu!

## Performance Benchmarks

| Metric | Owl Browser | Puppeteer |
|--------|-------------|-----------|
| Cold start | <1s | 2-5s |
| Navigation (example.com) | 12ms | 500ms+ |
| Navigation (CNN) | 9s (full load) | 12s+ |
| Screenshot quality | 1920x1080 PNG | Varies |
| Action latency | <30ms | 50-100ms |
| Memory per session | <100MB | 150-200MB |
| Ad blocking | Built-in (72 domains) | Manual setup |

**Real-world CNN test:**
- Requests: 263 total
- Blocked: 16 requests (6.08%)
- Ads blocked: 5
- Analytics blocked: 11

## Stealth Features

- `navigator.webdriver` completely removed (not just false)
- No CDP detection artifacts
- Canvas fingerprint protection
- WebGL parameter masking
- Realistic plugin enumeration
- Permission API consistency
- No automation controller flags
- Native user agent
- No keychain password prompts on macOS

## Licensing

Owl Browser requires a valid license to run. The licensing system supports both offline and subscription-based licenses.

### License Types

| Type | Description |
|------|-------------|
| `trial` | Time-limited trial license |
| `personal` | Single-user personal license |
| `professional` | Professional license with more features |
| `enterprise` | Enterprise license with unlimited seats |
| `developer` | Developer license for testing |
| `subscription` | Monthly-validated subscription license |

### Quick Start

```bash
# Activate a license
owl_browser --license add /path/to/license.olic

# Check license status
owl_browser --license status

# Get hardware fingerprint (for hardware-bound licenses)
owl_browser --license fingerprint
```

### Subscription Licenses

Subscription licenses are validated monthly with a license server:
- **Monthly checks** occur on the same day/time as activation
- **Grace period** allows offline use if server is temporarily unreachable (default: 7 days)
- **Reactivation** is automatic when a canceled subscription is renewed

For complete licensing documentation, including license generation and server setup, see [docs/LICENSE_SYSTEM.md](docs/LICENSE_SYSTEM.md).

## Proxy Support

**Full proxy support with timezone spoofing for maximum stealth!**

The browser supports configuring proxies for both headless and UI modes with advanced stealth features:

### Supported Proxy Types
- **HTTP/HTTPS** - Standard HTTP(S) proxies
- **SOCKS4** - SOCKS4 proxy protocol
- **SOCKS5** - SOCKS5 proxy protocol
- **SOCKS5H** - SOCKS5 with remote DNS resolution (recommended for stealth)

### Proxy Features
- **Timezone Spoofing**: Override browser timezone to match proxy location (prevents browserscan detection)
- **WebRTC Blocking**: Prevents IP leaks via WebRTC
- **Custom CA Certificates**: Support for SSL interception proxies (Charles, mitmproxy, etc.)
- **Stealth Mode**: Automatic fingerprint randomization when proxy is enabled

### Configuration via MCP

```javascript
// Create context with proxy
await browser.createContext({
  proxy_enabled: true,
  proxy_type: 'socks5h',
  proxy_host: 'proxy.example.com',
  proxy_port: 1080,
  proxy_username: 'user',        // Optional
  proxy_password: 'pass',        // Optional
  proxy_spoof_timezone: true,
  proxy_timezone_override: 'America/New_York',  // Match your proxy location
  proxy_stealth: true,
  proxy_block_webrtc: true
});
```

### Configuration via SDK

```typescript
const page = await browser.newPage({
  proxyConfig: {
    enabled: true,
    type: 'socks5h',
    host: 'proxy.example.com',
    port: 1080,
    timezone: 'America/New_York',  // Spoof timezone to match proxy
    stealth: true
  }
});
```

### UI Browser Proxy Settings

The UI version includes a visual proxy configuration overlay:
1. Click the **Proxy** button in the toolbar
2. Enter proxy details (type, host, port, credentials)
3. Set **Timezone** to match your proxy location (e.g., `America/New_York`, `Europe/London`)
4. Enable **Stealth Mode** for WebRTC blocking and fingerprint protection
5. Optionally configure custom CA certificate for SSL interception proxies
6. Click **Save** then **Connect**

### Timezone Values

Use standard IANA timezone identifiers:
- `America/New_York` - US Eastern
- `America/Los_Angeles` - US Pacific
- `Europe/London` - UK
- `Europe/Paris` - Central Europe
- `Asia/Tokyo` - Japan
- `Asia/Singapore` - Singapore
- `Australia/Sydney` - Australia Eastern

**Note:** Setting the correct timezone to match your proxy location prevents detection by sites like browserscan.net that check for timezone mismatches.

## Browser Profiles

**Persistent browser identities with fingerprint management, cookie storage, and session persistence!**

Browser profiles allow you to create and maintain consistent browser identities across sessions. Each profile includes:

- **Browser Fingerprints**: Randomized but consistent fingerprints (user agent, screen resolution, GPU, timezone, etc.)
- **Cookie Persistence**: Automatically save and restore cookies for login sessions
- **LLM Configuration**: Per-profile LLM settings (built-in or third-party APIs)
- **Proxy Configuration**: Per-profile proxy settings with stealth features
- **Session Storage**: Persist local storage and session data

### Profile Features

**Fingerprint Properties:**
- `user_agent` - Browser user agent string
- `platform` - Navigator platform (Win32, MacIntel, Linux x86_64)
- `vendor` - Browser vendor (Google Inc.)
- `languages` - Preferred languages array
- `hardware_concurrency` - CPU core count (4, 6, 8, 12, 16)
- `device_memory` - Device RAM in GB (4, 8, 16, 32)
- `screen_width`, `screen_height` - Screen resolution
- `color_depth`, `pixel_ratio` - Display properties
- `timezone`, `locale` - Regional settings
- `webgl_vendor`, `webgl_renderer` - GPU fingerprint
- `canvas_noise_seed`, `audio_noise_seed` - Anti-fingerprint noise seeds

### MCP Tools for Profile Management

```javascript
// Create a new profile with randomized fingerprints
const profile = await browser.createProfile({ name: 'My Shopping Profile' });

// Create context with profile (auto-loads if exists, creates if not)
await browser.createContext({
  profile_path: '/path/to/my-profile.json'
});

// Load existing profile into a context
await browser.loadProfile(context_id, '/path/to/profile.json');

// Save current context state to profile
await browser.saveProfile(context_id, '/path/to/profile.json');

// Get current profile state
const state = await browser.getProfile(context_id);

// Update profile with current cookies (after login)
await browser.updateProfileCookies(context_id);
```

### Profile File Structure

Profiles are stored as JSON files:

```json
{
  "profile_id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
  "profile_name": "My Shopping Profile",
  "created_at": "2024-01-15T10:30:00Z",
  "modified_at": "2024-01-15T14:45:00Z",
  "version": 1,
  "fingerprint": {
    "user_agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36...",
    "platform": "Win32",
    "hardware_concurrency": 8,
    "device_memory": 16,
    "screen_width": 1920,
    "screen_height": 1080,
    "timezone": "America/New_York",
    "webgl_vendor": "Google Inc. (NVIDIA)",
    "webgl_renderer": "ANGLE (NVIDIA, NVIDIA GeForce GTX 1660 Ti...)",
    "canvas_noise_seed": 0.0003,
    "audio_noise_seed": 0.0002
  },
  "cookies": [
    {
      "name": "session_id",
      "value": "abc123...",
      "domain": ".example.com",
      "path": "/",
      "secure": true,
      "http_only": true,
      "same_site": "lax",
      "expires": 1705312800
    }
  ],
  "has_llm_config": true,
  "llm_config": {
    "enabled": true,
    "use_builtin": false,
    "external_endpoint": "https://api.openai.com/v1",
    "external_model": "gpt-4-vision-preview"
  },
  "has_proxy_config": true,
  "proxy_config": {
    "type": "socks5h",
    "host": "proxy.example.com",
    "port": 1080,
    "enabled": true,
    "stealth_mode": true,
    "block_webrtc": true,
    "spoof_timezone": true,
    "timezone_override": "America/New_York"
  },
  "auto_save_cookies": true,
  "persist_local_storage": true
}
```

### Use Cases

**E-Commerce Automation:**
```javascript
// Create shopping profile
const ctx = await browser.createContext({
  profile_path: './profiles/amazon-shopper.json'
});

// Profile auto-loads saved cookies (already logged in!)
await browser.navigate(ctx, 'https://amazon.com');

// After session, save updated cookies
await browser.updateProfileCookies(ctx);
await browser.saveProfile(ctx);
```

**Multi-Account Management:**
```javascript
// Different profiles for different accounts
const profile1 = await browser.createContext({ profile_path: './profiles/account1.json' });
const profile2 = await browser.createContext({ profile_path: './profiles/account2.json' });

// Each has unique fingerprint + saved sessions
await browser.navigate(profile1, 'https://twitter.com');  // Logged into Account 1
await browser.navigate(profile2, 'https://twitter.com');  // Logged into Account 2
```

**Anti-Detection Testing:**
```javascript
// Create profile with specific fingerprint for testing
const ctx = await browser.createContext({
  profile_path: './profiles/test-profile.json'
});

// Navigate to fingerprint checker
await browser.navigate(ctx, 'https://browserscan.net');

// Profile maintains consistent fingerprint across sessions
// Same canvas hash, WebGL renderer, timezone, etc.
```

### Profile Storage Location

By default, profiles are stored in:
- **macOS**: `~/Library/Application Support/OwlBrowser/profiles/`
- **Windows**: `%APPDATA%\OwlBrowser\profiles\`
- **Linux**: `~/.config/owl-browser/profiles/`

You can also specify any custom path when creating or loading profiles.

## Use Cases

**For AI Agents (Claude, ChatGPT, etc.):**
- Web research with intelligent content extraction
- Form filling with natural language ("fill in the contact form")
- E-commerce automation (price monitoring, cart management)
- Data collection from dynamic websites
- Screenshot + LLM analysis for visual understanding
- Multi-step task automation with NLA

**For Developers (Selenium/Puppeteer/Playwright Replacement):**
- **Resilient automation**: Scripts don't break when DOM changes
- **No CSS selector hell**: Use natural language instead
- **Built-in intelligence**: Element classification, intent matching
- **Better debugging**: LLM explains what it found and why
- **Faster development**: Write automation scripts 10x faster
- **Simple SDK**: Just install and go - 3-4 lines of code

**For MCP Integration:**
- Expose as MCP tools for Claude Desktop
- Enable Claude to browse the web autonomously
- Combine with other MCP servers (filesystem, database, etc.)
- Build complex multi-tool AI workflows
- 95+ tools ready to use out of the box

## Development

### Build Commands

```bash
# Full rebuild
npm run build

# Quick rebuild (after code changes)
npm run rebuild

# Build UI version (macOS only)
npm run build:ui
```

### Project Structure

The codebase is organized into modular components for better maintainability:

```
owl-browser/
├── src/                            # C++ source files (modular structure)
│   ├── core/                      # Browser fundamentals
│   │   ├── owl_app.cc             # CefApp implementation
│   │   ├── owl_browser_manager.cc # Browser lifecycle + AI methods
│   │   ├── owl_client.cc          # Load handler + paint + navigation
│   │   ├── owl_subprocess.cc      # Main process + IPC handler
│   │   └── owl_main.cc            # Entry point
│   ├── automation/                # Element interaction
│   │   ├── owl_automation_handler.cc
│   │   ├── owl_element_scanner.cc
│   │   ├── owl_dom_visitor.cc
│   │   └── owl_render_tracker.cc
│   ├── ai/                        # LLM integration & intelligence
│   │   ├── owl_ai_intelligence.cc # AI intelligence layer
│   │   ├── owl_semantic_matcher.cc # Semantic element matching
│   │   ├── owl_llama_server.cc    # LLM server integration
│   │   ├── owl_llm_client.cc      # LLM API client
│   │   ├── owl_nla.cc             # Natural Language Actions
│   │   ├── owl_query_router.cc    # Query routing
│   │   ├── owl_llm_guardrail.cc   # Prompt injection protection
│   │   └── owl_pii_scrubber.cc    # PII scrubbing for third-party LLMs
│   ├── captcha/                   # CAPTCHA detection & solving
│   │   ├── owl_captcha_detector.cc
│   │   ├── owl_captcha_classifier.cc
│   │   ├── owl_text_captcha_solver.cc
│   │   ├── owl_image_captcha_solver.cc
│   │   └── owl_image_captcha_provider_*.cc
│   ├── content/                   # Content extraction
│   │   ├── owl_content_extractor.cc
│   │   ├── owl_markdown_converter.cc
│   │   ├── owl_extraction_template.cc
│   │   └── owl_json_extractor.cc
│   ├── stealth/                   # Anti-detection
│   │   ├── owl_stealth.cc         # Anti-detection patches
│   │   └── owl_resource_blocker.cc # Ad/analytics blocker
│   ├── network/                   # Network & connectivity
│   │   ├── owl_proxy_manager.cc   # Proxy configuration & stealth
│   │   ├── owl_cookie_manager.cc  # Cookie persistence
│   │   └── owl_demographics.cc    # Location/weather detection
│   ├── profile/                   # Browser profiles
│   │   └── owl_browser_profile.cc # Profile & fingerprint management
│   ├── media/                     # Media handling
│   │   ├── owl_video_recorder.cc  # Video recording
│   │   └── owl_native_screenshot.mm # Native screenshots (macOS/Linux)
│   ├── ui/                        # UI components
│   │   ├── owl_ui_browser.cc      # UI browser implementation
│   │   ├── owl_ui_main.mm         # UI entry point (macOS)
│   │   ├── owl_ui_delegate.mm     # UI delegate (macOS)
│   │   ├── owl_ui_toolbar.mm      # Toolbar (macOS)
│   │   ├── owl_playground*.cc     # Developer playground
│   │   ├── owl_homepage.cc        # Browser homepage
│   │   ├── owl_agent_controller.cc # AI agent controller
│   │   ├── owl_task_state.cc      # Task state management
│   │   └── owl_dev_*.mm           # Developer tools (console, elements, network)
│   ├── util/                      # Utilities
│   │   ├── owl_platform_utils.cc  # Platform utilities
│   │   ├── owl_license.cc         # Licensing system
│   │   ├── owl_test_scheme_handler.cc
│   │   └── logger.cc              # Logging
│   ├── mcp-server.cjs             # MCP server (58 tools)
│   └── browser-wrapper.cjs        # Native browser wrapper
├── include/                        # C++ headers (mirrors src/ structure)
│   ├── core/                      # Core headers
│   ├── automation/                # Automation headers
│   ├── ai/                        # AI headers
│   ├── captcha/                   # CAPTCHA headers
│   ├── content/                   # Content headers
│   ├── stealth/                   # Stealth headers
│   ├── network/                   # Network headers
│   ├── profile/                   # Profile headers
│   ├── media/                     # Media headers
│   ├── ui/                        # UI headers
│   └── util/                      # Utility headers
├── sdk/                            # TypeScript SDK
│   ├── src/                       # SDK source
│   ├── examples/                  # Usage examples
│   └── README.md                  # SDK documentation
├── tests/                          # Test suite (10 tests)
├── http-server/                    # HTTP REST API server
│   ├── include/                   # Server headers
│   ├── src/                       # Server implementation
│   ├── README.md                  # Setup guide
│   └── API.md                     # API documentation
├── third_party/                    # Dependencies
│   ├── cef_macos/                 # CEF for macOS
│   ├── cef_linux/                 # CEF for Ubuntu
│   ├── llama_macos/               # llama.cpp for macOS
│   ├── llama_linux/               # llama.cpp for Ubuntu
│   └── GeoLite2-City.mmdb         # GeoIP database
├── models/                         # AI models
│   ├── llm-assist.gguf            # Qwen3-VL-2B model
│   └── mmproj-llm-assist.gguf     # Vision projector
├── scripts/                        # Build scripts
│   ├── download-cef.cjs           # Auto-download CEF
│   └── download-llama.cjs         # Auto-download llama.cpp
├── CMakeLists.txt                 # Build configuration
├── README.md                      # This file
├── UBUNTU-SUPPORT.md              # Ubuntu setup guide
└── SDK-QUICKSTART.md              # SDK quick start
```

### Module Overview

| Module | Description |
|--------|-------------|
| **core/** | Browser fundamentals - CEF integration, browser lifecycle, IPC |
| **automation/** | Element interaction, DOM scanning, render tracking |
| **ai/** | LLM integration, semantic matching, NLA, guardrails |
| **captcha/** | CAPTCHA detection, classification, and solving |
| **content/** | HTML/Markdown extraction, JSON templates |
| **stealth/** | Anti-detection patches, ad/tracker blocking |
| **network/** | Proxy management, cookies, geolocation |
| **profile/** | Browser fingerprinting, session persistence |
| **media/** | Video recording, screenshot capture |
| **ui/** | UI components, toolbar, developer tools |
| **util/** | Platform utilities, licensing, logging |

## Troubleshooting

### macOS - Keychain Password Prompt

**Fixed!** The browser now uses `--use-mock-keychain` and `--password-store=basic` flags to prevent the macOS keychain password prompt.

### Ubuntu - Build Errors

**Missing Dependencies:**
```bash
sudo apt-get install -y build-essential cmake libgtk-3-dev libglib2.0-dev \
    libnspr4-dev libnss3-dev libatk1.0-dev libatk-bridge2.0-dev \
    libcups2-dev libdrm-dev libxkbcommon-dev libxcomposite-dev \
    libxdamage-dev libxrandr-dev libgbm-dev libpango1.0-dev libasound2-dev
```

**llama-server Not Found:**
```bash
# Re-download llama.cpp binaries
npm run download:llama
```

See [UBUNTU-SUPPORT.md](UBUNTU-SUPPORT.md) for complete Ubuntu troubleshooting guide.

## Roadmap

- [x] **Phase 1: CEF Browser Core** - Off-screen rendering, navigation, screenshots
- [x] **Phase 2: AI-First Features** - Natural language selectors, content extraction, ad blocking
- [x] **Phase 3: Built-in LLM Integration** - Qwen3-VL-2B, llama.cpp, vision model
- [x] **Phase 4: Advanced Features** - Video recording, NLA, CAPTCHA solving, demographics
- [x] **Phase 5: MCP Integration** - 95+ MCP tools, Claude Desktop integration
- [x] **Phase 6: Cross-Platform Support** - Ubuntu 22.04+ support (headless)
- [x] **Phase 7: Complete Automation** - Network interception, file downloads, dialog handling, multi-tab
- [ ] **Phase 8: Production Ready** - Advanced caching, multi-context optimization, performance tuning

## Credits

- **Built with**: Chromium Embedded Framework (CEF) 140.1.14
- **LLM Engine**: llama.cpp (Metal/CPU/CUDA acceleration)
- **AI Model**: Qwen3-VL-2B vision-language model (Alibaba Cloud)
- **GeoIP**: MaxMind GeoLite2
- **Weather**: Open-Meteo API
- **Designed by**: Claude (Anthropic)
- **Built by**: [Olib AI](https://www.olib.ai)
- **Built for**: AI automation, MCP integration, and developer productivity
- **Inspired by**: The need for a truly AI-native browser

## License

MIT

## Contributing

This browser was built BY AI FOR AI. Contributions welcome!

---

**Built BY AI FOR AI** - Selenium/Puppeteer replacement with built-in intelligence!
