# Owl Browser HTTP Server

A standalone HTTP server providing REST API access to the Owl Browser for automation tasks.

## Overview

This server acts as a bridge between HTTP clients and the Owl Browser, allowing you to control browser automation through simple REST API calls. It's designed for:

- Docker deployments where other services need browser automation
- Microservices architectures
- Language-agnostic browser automation
- High-availability scenarios with 100% uptime requirements

## Building

The HTTP server is built as part of the main project:

```bash
cd /path/to/olib-browser
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make owl_http_server
```

The binary will be at `build/Release/owl_http_server`.

## Configuration

Configuration can be provided via:
1. **Command-line arguments** (highest priority)
2. **Environment variables**
3. **Configuration file** (JSON or YAML)
4. **Default values** (lowest priority)

### Using a Configuration File

```bash
# Using YAML config
./owl_http_server -c /path/to/config.yaml

# Using JSON config
./owl_http_server --config /path/to/config.json

# Generate example config file
./owl_http_server --generate-config config.yaml
./owl_http_server --generate-config config.json
```

### Example YAML Configuration

```yaml
# Server Settings
host: 127.0.0.1
port: 8080
token: your-secret-token-here
browser_path: /path/to/owl_browser

# Connection Settings
max_connections: 100
request_timeout_ms: 30000
browser_timeout_ms: 60000

# Logging
verbose: false
log_requests: false

# Rate Limiting
rate_limit:
  enabled: true
  requests_per_window: 100
  window_seconds: 60
  burst_size: 20

# IP Whitelist
ip_whitelist:
  enabled: true
  ips:
    - 127.0.0.1
    - 192.168.1.0/24
    - 10.0.0.0/8

# SSL/TLS
ssl:
  enabled: false
  cert_path: /path/to/cert.pem
  key_path: /path/to/key.pem
  ca_path: ""
  verify_client: false

# CORS Settings
cors:
  enabled: true
  allowed_origins: "*"
  allowed_methods: GET,POST,PUT,DELETE,OPTIONS
  allowed_headers: Content-Type,Authorization
  max_age_seconds: 86400
```

### Environment Variables

#### Required

| Variable | Description |
|----------|-------------|
| `OWL_BROWSER_PATH` | Path to the `owl_browser` binary |
| `OWL_HTTP_TOKEN` | Bearer token (required if `OWL_AUTH_MODE=token`) |
| `OWL_JWT_PUBLIC_KEY` | Path to RSA public key (required if `OWL_AUTH_MODE=jwt`) |

#### Authentication

The server supports two authentication modes:

| Variable | Default | Description |
|----------|---------|-------------|
| `OWL_AUTH_MODE` | `token` | Authentication mode: `token` or `jwt` |

#### JWT Authentication

When `OWL_AUTH_MODE=jwt`, the server uses RSA-signed JWT tokens for authentication.

| Variable | Default | Description |
|----------|---------|-------------|
| `OWL_JWT_PUBLIC_KEY` | - | Path to RSA public key (.pem) for verification (required) |
| `OWL_JWT_PRIVATE_KEY` | - | Path to RSA private key (.pem) for signing (optional) |
| `OWL_JWT_ALGORITHM` | `RS256` | Signing algorithm: `RS256`, `RS384`, or `RS512` |
| `OWL_JWT_ISSUER` | - | Expected token issuer (optional validation) |
| `OWL_JWT_AUDIENCE` | - | Expected token audience (optional validation) |
| `OWL_JWT_CLOCK_SKEW` | `60` | Allowed clock skew in seconds |
| `OWL_JWT_REQUIRE_EXP` | `true` | Require expiration claim in tokens |

**Generating RSA Key Pair for JWT:**

```bash
# Generate 2048-bit RSA private key
openssl genrsa -out jwt_private.pem 2048

# Extract public key from private key
openssl rsa -in jwt_private.pem -pubout -out jwt_public.pem

# For stronger security, use 4096-bit key
openssl genrsa -out jwt_private.pem 4096
openssl rsa -in jwt_private.pem -pubout -out jwt_public.pem
```

**JWT Token Structure:**

The server expects tokens with the following standard claims:
- `iss` - Issuer (validated if `OWL_JWT_ISSUER` is set)
- `sub` - Subject (user identifier)
- `aud` - Audience (validated if `OWL_JWT_AUDIENCE` is set)
- `exp` - Expiration time (Unix timestamp)
- `iat` - Issued at (Unix timestamp)
- `nbf` - Not before (Unix timestamp)
- `jti` - JWT ID (unique identifier)

Custom claims supported:
- `scope` - Permissions (e.g., "read write admin")
- `client_id` - Client identifier

**Example: Creating a JWT Token (Python):**

```python
import json
import base64
import time
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding

def base64url_encode(data):
    if isinstance(data, str):
        data = data.encode('utf-8')
    return base64.urlsafe_b64encode(data).rstrip(b'=').decode('utf-8')

def create_jwt(claims, private_key_path):
    # Load private key
    with open(private_key_path, 'rb') as f:
        private_key = serialization.load_pem_private_key(f.read(), password=None)

    # Create header and payload
    header = {'alg': 'RS256', 'typ': 'JWT'}
    header_b64 = base64url_encode(json.dumps(header, separators=(',', ':')))
    payload_b64 = base64url_encode(json.dumps(claims, separators=(',', ':')))

    # Sign
    signing_input = f'{header_b64}.{payload_b64}'
    signature = private_key.sign(
        signing_input.encode('utf-8'),
        padding.PKCS1v15(),
        hashes.SHA256()
    )
    signature_b64 = base64url_encode(signature)

    return f'{signing_input}.{signature_b64}'

# Create token with 1 hour expiration
now = int(time.time())
claims = {
    'iss': 'my-service',
    'sub': 'user-123',
    'exp': now + 3600,
    'iat': now,
    'scope': 'read write'
}
token = create_jwt(claims, '/path/to/jwt_private.pem')
print(f'Bearer {token}')
```

**Example: Using JWT with curl:**

```bash
# Set your JWT token
TOKEN="eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9..."

# Make authenticated request
curl -H "Authorization: Bearer $TOKEN" http://localhost:8080/tools
```

#### Server Settings

| Variable | Default | Description |
|----------|---------|-------------|
| `OWL_HTTP_HOST` | `127.0.0.1` | Server bind address |
| `OWL_HTTP_PORT` | `8080` | Server port |
| `OWL_HTTP_MAX_CONNECTIONS` | `100` | Maximum concurrent connections |
| `OWL_HTTP_TIMEOUT` | `30000` | Request timeout (ms) |
| `OWL_BROWSER_TIMEOUT` | `60000` | Browser command timeout (ms) |
| `OWL_HTTP_VERBOSE` | `false` | Enable verbose logging |
| `OWL_LOG_REQUESTS` | `false` | Log all requests |

#### Rate Limiting

| Variable | Default | Description |
|----------|---------|-------------|
| `OWL_RATE_LIMIT_ENABLED` | `false` | Enable rate limiting |
| `OWL_RATE_LIMIT_REQUESTS` | `100` | Max requests per window |
| `OWL_RATE_LIMIT_WINDOW` | `60` | Window size in seconds |
| `OWL_RATE_LIMIT_BURST` | `20` | Burst allowance above limit |

#### IP Whitelist

| Variable | Default | Description |
|----------|---------|-------------|
| `OWL_IP_WHITELIST_ENABLED` | `false` | Enable IP whitelist |
| `OWL_IP_WHITELIST` | - | Comma-separated IPs/CIDRs |

Example: `OWL_IP_WHITELIST=127.0.0.1,192.168.1.0/24,10.0.0.0/8`

#### SSL/TLS

| Variable | Default | Description |
|----------|---------|-------------|
| `OWL_SSL_ENABLED` | `false` | Enable HTTPS |
| `OWL_SSL_CERT` | - | Path to certificate file (.pem or .crt) |
| `OWL_SSL_KEY` | - | Path to private key file (.pem or .key) |
| `OWL_SSL_CA` | - | Path to CA bundle (optional) |
| `OWL_SSL_VERIFY_CLIENT` | `false` | Require client certificates |

#### CORS

| Variable | Default | Description |
|----------|---------|-------------|
| `OWL_CORS_ENABLED` | `true` | Enable CORS headers |
| `OWL_CORS_ORIGINS` | `*` | Allowed origins |
| `OWL_CORS_METHODS` | `GET,POST,PUT,DELETE,OPTIONS` | Allowed methods |
| `OWL_CORS_HEADERS` | `Content-Type,Authorization` | Allowed headers |
| `OWL_CORS_MAX_AGE` | `86400` | Preflight cache duration (seconds) |

#### Additional Options

| Variable | Default | Description |
|----------|---------|-------------|
| `OWL_GRACEFUL_SHUTDOWN` | `true` | Wait for active connections on shutdown |
| `OWL_SHUTDOWN_TIMEOUT` | `30` | Max wait time for graceful shutdown (seconds) |
| `OWL_KEEP_ALIVE_TIMEOUT` | `60` | HTTP keep-alive timeout (seconds) |

## Running

```bash
# Basic usage with environment variables
OWL_HTTP_TOKEN=your-secret-token \
OWL_BROWSER_PATH=/path/to/owl_browser \
./owl_http_server

# With config file
./owl_http_server -c config.yaml

# With rate limiting and IP whitelist
OWL_HTTP_TOKEN=your-secret-token \
OWL_BROWSER_PATH=/path/to/owl_browser \
OWL_RATE_LIMIT_ENABLED=true \
OWL_RATE_LIMIT_REQUESTS=50 \
OWL_IP_WHITELIST_ENABLED=true \
OWL_IP_WHITELIST=127.0.0.1,192.168.1.0/24 \
./owl_http_server

# Production setup with SSL
OWL_HTTP_HOST=0.0.0.0 \
OWL_HTTP_PORT=443 \
OWL_HTTP_TOKEN=your-secret-token \
OWL_BROWSER_PATH=/path/to/owl_browser \
OWL_SSL_ENABLED=true \
OWL_SSL_CERT=/etc/ssl/certs/server.crt \
OWL_SSL_KEY=/etc/ssl/private/server.key \
OWL_RATE_LIMIT_ENABLED=true \
./owl_http_server
```

## API Endpoints

### Health Check

```
GET /health
```

No authentication required. Returns server and browser status.

**Response:**
```json
{
  "status": "healthy",
  "browser_ready": true,
  "browser_state": "ready"
}
```

### List Tools

```
GET /tools
Authorization: Bearer <token>
```

Returns list of all available browser tools.

### Tool Documentation

```
GET /tools/{tool_name}
Authorization: Bearer <token>
```

Returns detailed documentation for a specific tool including parameters.

**Example:**
```bash
curl -H "Authorization: Bearer your-token" \
  http://localhost:8080/tools/browser_navigate
```

### Execute Tool

```
POST /execute/{tool_name}
Authorization: Bearer <token>
Content-Type: application/json

{...parameters...}
```

Execute a browser tool with the given parameters.

**Example - Create Context:**
```bash
curl -X POST \
  -H "Authorization: Bearer your-token" \
  -H "Content-Type: application/json" \
  http://localhost:8080/execute/browser_create_context
```

**Example - Navigate:**
```bash
curl -X POST \
  -H "Authorization: Bearer your-token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_123", "url": "https://example.com"}' \
  http://localhost:8080/execute/browser_navigate
```

**Example - Take Screenshot:**
```bash
curl -X POST \
  -H "Authorization: Bearer your-token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_123"}' \
  http://localhost:8080/execute/browser_screenshot
```

### Raw Command (Advanced)

```
POST /command
Authorization: Bearer <token>
Content-Type: application/json

{"method": "...", ...params}
```

Send a raw command directly to the browser (for advanced use cases).

### Server Statistics

```
GET /stats
Authorization: Bearer <token>
```

Returns detailed server statistics and performance metrics.

**Response:**
```json
{
  "success": true,
  "stats": {
    "uptime_seconds": 3600,
    "requests_total": 10000,
    "requests_success": 9850,
    "requests_error": 150,
    "active_connections": 15,
    "bytes_received": 2500000,
    "bytes_sent": 15000000,
    "requests_per_second": 25.5,
    "bytes_per_second_in": 1200.0,
    "bytes_per_second_out": 8500.0,
    "requests_concurrent_current": 3,
    "requests_concurrent_peak": 12,
    "latency_avg_us": 45000,
    "latency_min_us": 1200,
    "latency_max_us": 250000,
    "thread_pool": {
      "num_threads": 8,
      "active_workers": 3,
      "pending_tasks": 2,
      "tasks_completed": 9998
    }
  }
}
```

### WebSocket API

WebSocket endpoint for bidirectional communication:

```
WS /ws
Authorization: Bearer <token> (via query param or header)
```

**Connecting:**
```javascript
const ws = new WebSocket('ws://localhost:8080/ws?token=your-token');

ws.onopen = () => {
  console.log('Connected');
  // Send a command
  ws.send(JSON.stringify({
    id: 1,
    method: 'browser_create_context',
    params: {}
  }));
};

ws.onmessage = (event) => {
  const response = JSON.parse(event.data);
  console.log('Response:', response);
  // { id: 1, success: true, result: { context_id: "ctx_123" } }
};
```

**Message Format:**
```json
{
  "id": 1,
  "method": "browser_navigate",
  "params": {
    "context_id": "ctx_123",
    "url": "https://example.com"
  }
}
```

**Response Format:**
```json
{
  "id": 1,
  "success": true,
  "result": {...}
}
```

WebSocket provides lower latency for high-frequency commands compared to REST API.

## Available Tools

### Context Management
- `browser_create_context` - Create a new browser context (tab/window)
- `browser_close_context` - Close a browser context
- `browser_list_contexts` - List all active contexts

### Navigation
- `browser_navigate` - Navigate to a URL
- `browser_reload` - Reload the current page
- `browser_go_back` - Navigate back
- `browser_go_forward` - Navigate forward

### Interaction
- `browser_click` - Click an element
- `browser_type` - Type text into an input
- `browser_pick` - Select from a dropdown
- `browser_press_key` - Press a key (Enter, Tab, etc.)
- `browser_submit_form` - Submit the current form
- `browser_hover` - Hover over an element
- `browser_double_click` - Double-click an element
- `browser_right_click` - Right-click (context menu)
- `browser_clear_input` - Clear an input field
- `browser_focus` - Focus on an element
- `browser_blur` - Remove focus from an element
- `browser_select_all` - Select all text in an input
- `browser_keyboard_combo` - Press key combinations (Ctrl+A, etc.)
- `browser_drag_drop` - Drag and drop with coordinates
- `browser_html5_drag_drop` - HTML5 drag and drop for sortable elements
- `browser_mouse_move` - Human-like mouse movement
- `browser_upload_file` - Upload files to input elements
- `browser_highlight` - Highlight an element for debugging

### Element State
- `browser_is_visible` - Check if element is visible
- `browser_is_enabled` - Check if element is enabled
- `browser_is_checked` - Check if checkbox/radio is checked
- `browser_get_attribute` - Get element attribute value
- `browser_get_bounding_box` - Get element position and size

### JavaScript Evaluation
- `browser_evaluate` - Execute JavaScript in page context (use return_value=true for expressions)

### Clipboard Management
- `browser_clipboard_read` - Read text from system clipboard
- `browser_clipboard_write` - Write text to system clipboard
- `browser_clipboard_clear` - Clear system clipboard

### Frame/Iframe Handling
- `browser_list_frames` - List all frames on the page
- `browser_switch_to_frame` - Switch to an iframe
- `browser_switch_to_main_frame` - Switch back to main frame

### Content Extraction
- `browser_extract_text` - Extract text content
- `browser_screenshot` - Take a screenshot (base64)
- `browser_show_grid_overlay` - Show XY coordinate grid overlay
- `browser_get_html` - Get page HTML
- `browser_get_markdown` - Get page as Markdown
- `browser_extract_json` - Extract structured JSON data

### AI/LLM Features
- `browser_summarize_page` - AI-powered page summary
- `browser_query_page` - Ask questions about the page
- `browser_nla` - Natural language browser commands
- `browser_llm_status` - Check LLM status

### Scroll Control
- `browser_scroll_by` - Scroll by pixels
- `browser_scroll_to_element` - Scroll element into view
- `browser_scroll_to_top` - Scroll to top
- `browser_scroll_to_bottom` - Scroll to bottom

### Wait Utilities
- `browser_wait` - Wait for milliseconds
- `browser_wait_for_selector` - Wait for element
- `browser_wait_for_network_idle` - Wait for network activity to stop
- `browser_wait_for_function` - Wait for JS function to return truthy
- `browser_wait_for_url` - Wait for URL to match pattern

### Page Info
- `browser_get_page_info` - Get current page info
- `browser_set_viewport` - Set viewport size

### Video Recording
- `browser_start_video_recording` - Start recording
- `browser_pause_video_recording` - Pause recording
- `browser_resume_video_recording` - Resume recording
- `browser_stop_video_recording` - Stop and save
- `browser_get_video_recording_stats` - Get recording stats

### Live Video Streaming
- `browser_start_live_stream` - Start MJPEG live stream
- `browser_stop_live_stream` - Stop live stream
- `browser_get_live_stream_stats` - Get stream statistics
- `browser_list_live_streams` - List active streams

### Demographics
- `browser_get_demographics` - Get user demographics
- `browser_get_location` - Get location info
- `browser_get_datetime` - Get date/time
- `browser_get_weather` - Get weather info

### CAPTCHA Handling
- `browser_detect_captcha` - Detect if page has CAPTCHA
- `browser_classify_captcha` - Classify CAPTCHA type
- `browser_solve_captcha` - Auto-solve CAPTCHA
- `browser_solve_text_captcha` - Solve text CAPTCHA
- `browser_solve_image_captcha` - Solve image CAPTCHA

### Cookies
- `browser_get_cookies` - Get cookies
- `browser_set_cookie` - Set a cookie
- `browser_delete_cookies` - Delete cookies

### Proxy
- `browser_set_proxy` - Configure proxy
- `browser_get_proxy_status` - Get proxy status
- `browser_connect_proxy` - Connect proxy
- `browser_disconnect_proxy` - Disconnect proxy

### Browser Profiles
- `browser_create_profile` - Create new profile
- `browser_load_profile` - Load profile from file
- `browser_save_profile` - Save profile to file
- `browser_get_profile` - Get current profile
- `browser_update_profile_cookies` - Update profile cookies

### Network Interception
- `browser_add_network_rule` - Add interception rule (block, mock, redirect)
- `browser_remove_network_rule` - Remove a rule by ID
- `browser_enable_network_interception` - Enable/disable interception
- `browser_get_network_log` - Get captured requests/responses
- `browser_clear_network_log` - Clear the network log

### File Downloads
- `browser_set_download_path` - Set download directory
- `browser_get_downloads` - List all downloads
- `browser_wait_for_download` - Wait for download to complete
- `browser_cancel_download` - Cancel an active download

### Dialog Handling
- `browser_set_dialog_action` - Configure auto-handling policy
- `browser_handle_dialog` - Accept/dismiss a dialog
- `browser_get_pending_dialog` - Get pending dialog info
- `browser_wait_for_dialog` - Wait for a dialog to appear

### Multi-Tab Management
- `browser_new_tab` - Create new tab
- `browser_get_tabs` - List all tabs
- `browser_switch_tab` - Switch to a tab
- `browser_close_tab` - Close a tab
- `browser_get_active_tab` - Get active tab
- `browser_get_tab_count` - Get tab count
- `browser_set_popup_policy` - Configure popup handling
- `browser_get_blocked_popups` - Get blocked popup URLs

### License Management
- `browser_get_license_status` - Get current license status
- `browser_get_license_info` - Get detailed license information
- `browser_get_hardware_fingerprint` - Get hardware fingerprint for licensing
- `browser_add_license` - Add/activate a license (via base64 data or file path)
- `browser_remove_license` - Remove the current license

## Error Handling

All responses follow this format:

**Success:**
```json
{
  "success": true,
  "result": "..."
}
```

**Error:**
```json
{
  "success": false,
  "error": "Error message"
}
```

**Validation Error (422):**
```json
{
  "success": false,
  "error": "Validation failed",
  "tool": "browser_type",
  "missing_fields": "context_id, selector",
  "unknown_fields": "element_position",
  "supported_fields": "Supported fields for browser_type: context_id (required), selector (required), text (required)",
  "errors": [
    {"field": "context_id", "message": "Missing required field: context_id - The context ID"},
    {"field": "element_position", "message": "Unknown field: element_position is not a valid parameter for browser_type"}
  ]
}
```

### ActionResult Response Format

Browser actions (navigation, click, type, etc.) return structured `ActionResult` responses with detailed status information:

**Successful Action:**
```json
{
  "success": true,
  "status": "ok",
  "message": "Navigation completed",
  "url": "https://example.com"
}
```

**Failed Action - Element Not Found:**
```json
{
  "success": false,
  "status": "element_not_found",
  "message": "Element not found: login button",
  "selector": "login button"
}
```

**Failed Action - Navigation Error:**
```json
{
  "success": false,
  "status": "navigation_failed",
  "message": "Navigation failed: net::ERR_NAME_NOT_RESOLVED",
  "url": "https://invalid-domain.example",
  "error_code": "ERR_NAME_NOT_RESOLVED"
}
```

**Failed Action - Web Firewall Detected:**
```json
{
  "success": false,
  "status": "firewall_detected",
  "message": "Web firewall detected: Cloudflare (JS Challenge) on https://protected-site.com",
  "url": "https://protected-site.com",
  "error_code": "Cloudflare"
}
```

The browser automatically detects web firewalls and bot protection services including:
- **Cloudflare** - JS Challenge, Turnstile, Managed Challenge
- **Akamai** - Bot Manager
- **Imperva/Incapsula** - Security Challenge
- **PerimeterX** - Human Challenge
- **DataDome** - Bot Detection
- **AWS WAF** - Web Application Firewall

### ActionResult Status Codes

| Status | Description |
|--------|-------------|
| `ok` | Action completed successfully |
| `element_not_found` | Element not found on page |
| `element_not_visible` | Element exists but not visible |
| `element_not_interactable` | Element not interactable |
| `navigation_failed` | Navigation failed |
| `navigation_timeout` | Navigation timed out |
| `page_load_error` | Page failed to load |
| `firewall_detected` | Web firewall/bot protection detected (Cloudflare, Akamai, etc.) |
| `captcha_detected` | CAPTCHA challenge detected |
| `click_failed` | Click action failed |
| `type_failed` | Type action failed |
| `scroll_failed` | Scroll action failed |
| `context_not_found` | Browser context not found |
| `invalid_selector` | Invalid selector provided |
| `invalid_url` | Invalid URL provided |
| `timeout` | Operation timed out |

### ActionResult Fields

| Field | Type | Description |
|-------|------|-------------|
| `success` | boolean | Whether the action succeeded |
| `status` | string | Status code (see table above) |
| `message` | string | Human-readable message |
| `selector` | string? | The selector that was used (for element errors) |
| `url` | string? | URL involved (for navigation) |
| `error_code` | string? | Browser error code |
| `http_status` | number? | HTTP status code (for navigation) |
| `element_count` | number? | Number of elements found (for multiple_elements) |

## Docker Usage

Example Dockerfile:

```dockerfile
FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    libcurl4 \
    libmaxminddb0 \
    libuuid1 \
    && rm -rf /var/lib/apt/lists/*

# Copy browser and server
COPY build/Release/owl_browser /app/owl_browser
COPY build/Release/owl_http_server /app/owl_http_server

# Copy required resources
COPY build/Release/models /app/models
COPY build/Release/third_party /app/third_party

WORKDIR /app

ENV OWL_BROWSER_PATH=/app/owl_browser
ENV OWL_HTTP_HOST=0.0.0.0
ENV OWL_HTTP_PORT=8080

EXPOSE 8080

CMD ["./owl_http_server"]
```

## License Requirements

The Owl Browser requires a valid license to run. If no license is found, the server will display a license error with the hardware fingerprint needed for activation.

To activate a license:
```bash
/path/to/owl_browser --license add /path/to/license.olic
```

## Architecture

```
┌─────────────────┐     HTTP/WS    ┌─────────────────────────────────────────┐     IPC       ┌─────────────────┐
│   HTTP Client   │ ─────────────► │           HTTP Server                   │ ────────────► │  Owl Browser    │
│ (curl, browser) │ ◄───────────── │  ┌──────────┐  ┌──────────────────────┐ │ ◄──────────── │  (CEF-based)    │
└─────────────────┘     JSON       │  │ Router   │  │    Thread Pool       │ │    JSON       └─────────────────┘
                                   │  │ + Auth   │  │  (N worker threads)  │ │
                                   │  └──────────┘  └──────────────────────┘ │
                                   │  ┌──────────┐  ┌──────────────────────┐ │
                                   │  │   Rate   │  │   Async IPC          │ │
                                   │  │  Limiter │  │ (dedicated I/O thread)│ │
                                   │  └──────────┘  └──────────────────────┘ │
                                   └─────────────────────────────────────────┘
```

### Key Architecture Components

- **Thread Pool**: Automatically sizes to CPU cores, handles concurrent HTTP requests
- **Async IPC**: Dedicated I/O thread for non-blocking browser communication
- **Rate Limiter**: Token bucket algorithm with per-IP tracking and automatic cleanup
- **Graceful Shutdown**: Drains active connections before terminating

## Performance Optimizations

The HTTP server includes several optimizations for high throughput and stability:

### Thread Pool
- Auto-detects CPU cores for optimal thread count
- Work queue with configurable size
- Handles concurrent HTTP requests without blocking the main event loop

### Async IPC
- Dedicated I/O thread for browser communication
- Non-blocking command submission
- Efficient response parsing without JSON re-serialization

### Memory Management
- Rate limiter with automatic cleanup of expired entries
- Pre-allocated connection buffers with growth on demand
- Efficient JSON string escaping with single-pass encoding

### Request Handling
- Early Content-Length validation prevents DoS attacks
- HTTP keep-alive support for connection reuse
- Poll-based I/O for low-latency event handling

### Graceful Shutdown
- Configurable shutdown timeout
- Drains in-flight requests before terminating
- Completes pending writes to clients

## Security Features

### JWT Authentication

When enabled (`OWL_AUTH_MODE=jwt`), the server uses RSA-signed JWT tokens:

- Supports RS256, RS384, and RS512 algorithms
- Validates token signature, expiration, and optional issuer/audience claims
- Configurable clock skew tolerance
- Constant-time signature verification to prevent timing attacks

**Running with JWT:**
```bash
OWL_AUTH_MODE=jwt \
OWL_JWT_PUBLIC_KEY=/path/to/public.pem \
OWL_BROWSER_PATH=/path/to/owl_browser \
./owl_http_server
```

### Rate Limiting

When enabled, rate limiting tracks requests per IP address using a token bucket algorithm:

- Requests are tracked per IP address
- Configurable requests per time window
- Burst allowance for short spikes
- Returns `429 Too Many Requests` with retry information when limit exceeded

**Response when rate limited:**
```json
{
  "success": false,
  "error": "Rate limit exceeded",
  "retry_after": 45,
  "limit": 100,
  "remaining": 0
}
```

### IP Whitelist

When enabled, only requests from whitelisted IP addresses are allowed:

- Supports individual IPs (e.g., `192.168.1.100`)
- Supports CIDR ranges (e.g., `192.168.1.0/24`)
- Supports IPv4 and IPv6
- Returns `403 Forbidden` for non-whitelisted IPs

### SSL/TLS

When enabled with certificates:

- HTTPS encryption for all traffic
- Optional client certificate verification
- Supports PEM and CRT/KEY certificate formats

## Source Files

```
http-server/
├── include/
│   ├── config.h           # Configuration types and loading
│   ├── config_file.h      # JSON/YAML config file parser
│   ├── types.h            # Common type definitions
│   ├── json.h             # JSON parsing and building (optimized)
│   ├── auth.h             # Authentication (token + JWT)
│   ├── jwt.h              # JWT token handling (RS256/384/512)
│   ├── browser_ipc.h      # Browser process communication (sync)
│   ├── browser_ipc_async.h # Async IPC with dedicated I/O thread
│   ├── tools.h            # Tool definitions and validation
│   ├── router.h           # Request routing
│   ├── http_server.h      # HTTP server core with thread pool
│   ├── websocket.h        # WebSocket support
│   ├── thread_pool.h      # Thread pool for concurrent requests
│   ├── rate_limit.h       # Rate limiting with auto-cleanup
│   ├── ip_filter.h        # IP whitelist/filtering
│   └── log.h              # Logging utilities
├── src/
│   ├── main.c             # Entry point with CLI argument parsing
│   ├── config.c           # Configuration from env vars
│   ├── config_file.c      # JSON/YAML config file loading
│   ├── json.c             # JSON implementation (optimized string escaping)
│   ├── auth.c             # Authentication implementation
│   ├── jwt.c              # JWT implementation using OpenSSL
│   ├── browser_ipc.c      # Sync IPC implementation
│   ├── browser_ipc_async.c # Async IPC with efficient response parsing
│   ├── tools.c            # Tool definitions (100+ tools)
│   ├── router.c           # Router with security checks
│   ├── http_server.c      # HTTP server with graceful shutdown
│   ├── websocket.c        # WebSocket implementation
│   ├── thread_pool.c      # Thread pool implementation
│   ├── rate_limit.c       # Token bucket rate limiter
│   ├── ip_filter.c        # IP whitelist with CIDR support
│   └── log.c              # Logging implementation
├── README.md              # This file
└── API.md                 # API reference documentation
```
