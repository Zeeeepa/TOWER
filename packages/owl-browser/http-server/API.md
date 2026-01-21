# Owl Browser HTTP Server - API Documentation

**Version:** 1.3.0
**Base URL:** `http://{host}:{port}`
**Default:** `http://127.0.0.1:8080`
**WebSocket:** `ws://{host}:{port}/ws`

## Table of Contents

- [Authentication](#authentication)
- [Response Format](#response-format)
- [Error Handling](#error-handling)
- [Endpoints](#endpoints)
  - [Health Check](#health-check)
  - [Tools](#tools)
  - [Execute Tool](#execute-tool)
  - [Raw Command](#raw-command)
- [WebSocket API](#websocket-api)
- [Video Streaming Endpoints](#video-streaming-endpoints)
- [Tools Reference](#tools-reference)
  - [Context Management](#context-management)
  - [Navigation](#navigation)
  - [Interaction](#interaction)
  - [Content Extraction](#content-extraction)
  - [AI/LLM Features](#aillm-features)
  - [Scroll Control](#scroll-control)
  - [Wait Utilities](#wait-utilities)
  - [Page Info](#page-info)
  - [Video Recording](#video-recording)
  - [Live Video Streaming](#live-video-streaming)
  - [Demographics](#demographics)
  - [CAPTCHA Handling](#captcha-handling)
  - [Cookies](#cookies)
  - [Proxy](#proxy)
  - [Browser Profiles](#browser-profiles)
  - [Network Interception](#network-interception)
  - [File Downloads](#file-downloads)
  - [Dialog Handling](#dialog-handling)
  - [Tab/Window Management](#tabwindow-management)
  - [License Management](#license-management)

---

## Authentication

All endpoints except `/health` and `/auth` require Bearer token authentication.

```
Authorization: Bearer <your-token>
```

**Example:**
```bash
curl -H "Authorization: Bearer your-secret-token" \
  http://localhost:8080/tools
```

**Error Response (401):**
```json
{
  "success": false,
  "error": "Invalid or missing authorization token"
}
```

### Panel Login

For the web control panel, use password-based authentication via the `/auth` endpoint. The password is configured via the `OWL_PANEL_PASSWORD` environment variable.

#### POST /auth

Authenticate with a password and receive the Bearer token.

**Request:**
```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"password": "your-panel-password"}' \
  http://localhost:8080/auth
```

**Success Response (200):**
```json
{
  "success": true,
  "token": "your-bearer-token"
}
```

**Error Response (401):**
```json
{
  "success": false,
  "error": "Invalid password"
}
```

#### GET /auth/verify

Verify if a token is valid.

**Request:**
```bash
curl -H "Authorization: Bearer your-token" \
  http://localhost:8080/auth/verify
```

**Response:**
```json
{
  "valid": true
}
```

---

## Response Format

### Success Response
```json
{
  "success": true,
  "result": <value>
}
```

The `result` field type varies by tool:
- **String:** `"ctx_000001"`, `"https://example.com"`
- **Boolean:** `true`, `false`
- **Object:** `{"url": "...", "title": "..."}`
- **Base64:** Screenshot data (PNG)

### Error Response
```json
{
  "success": false,
  "error": "Error message describing what went wrong"
}
```

---

## Error Handling

### HTTP Status Codes

| Code | Description |
|------|-------------|
| 200 | Success |
| 400 | Bad Request - Invalid JSON |
| 401 | Unauthorized - Invalid or missing token |
| 404 | Not Found - Unknown endpoint or tool |
| 405 | Method Not Allowed |
| 422 | Unprocessable Entity - Validation failed |
| 500 | Internal Server Error |
| 502 | Bad Gateway - Browser command failed |
| 503 | Service Unavailable - Browser not ready |

### Validation Error (422)

When tool parameters are invalid:

```json
{
  "success": false,
  "error": "Validation failed",
  "tool": "browser_navigate",
  "missing_fields": "context_id",
  "unknown_fields": "invalid_param",
  "supported_fields": "Supported fields for browser_navigate: context_id (required), url (required)",
  "errors": [
    {
      "field": "context_id",
      "message": "Missing required field: context_id - The context ID"
    },
    {
      "field": "invalid_param",
      "message": "Unknown field: invalid_param is not a valid parameter for browser_navigate"
    }
  ]
}
```

### License Error (503)

When browser license is invalid:

```json
{
  "success": false,
  "error": "License error",
  "license_status": "not_found",
  "license_message": "Browser requires a valid license",
  "hardware_fingerprint": "abc123..."
}
```

---

## Endpoints

### Health Check

Check server and browser status.

```
GET /health
```

**Authentication:** Not required

**Response:**
```json
{
  "status": "healthy",
  "browser_ready": true,
  "browser_state": "ready"
}
```

**Browser States:**
- `stopped` - Browser process not running
- `starting` - Browser is initializing
- `ready` - Browser ready for commands
- `error` - Browser encountered an error
- `license_error` - Invalid or missing license

---

### Tools

#### List All Tools

```
GET /tools
```

**Response:**
```json
{
  "tools": [
    {
      "name": "browser_create_context",
      "description": "Create a new browser context (tab/window)",
      "param_count": 14
    },
    {
      "name": "browser_navigate",
      "description": "Navigate to a URL",
      "param_count": 2
    }
    // ... more tools
  ]
}
```

#### Get Tool Documentation

```
GET /tools/{tool_name}
```

**Example:**
```bash
curl -H "Authorization: Bearer token" \
  http://localhost:8080/tools/browser_navigate
```

**Response:**
```json
{
  "name": "browser_navigate",
  "description": "Navigate to a URL",
  "parameters": [
    {
      "name": "context_id",
      "type": "string",
      "required": true,
      "description": "The context ID"
    },
    {
      "name": "url",
      "type": "string",
      "required": true,
      "description": "The URL to navigate to"
    }
  ]
}
```

---

### Execute Tool

Execute a browser tool with parameters.

```
POST /execute/{tool_name}
Content-Type: application/json

{...parameters}
```

**Example:**
```bash
curl -X POST \
  -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_000001", "url": "https://example.com"}' \
  http://localhost:8080/execute/browser_navigate
```

**Response:**
```json
{
  "success": true,
  "result": true
}
```

---

### Raw Command

Send a raw command to the browser (advanced usage).

```
POST /command
Content-Type: application/json

{"method": "methodName", ...params}
```

**Example:**
```bash
curl -X POST \
  -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{"method": "navigate", "context_id": "ctx_000001", "url": "https://example.com"}' \
  http://localhost:8080/command
```

---

## Video Streaming Endpoints

Real-time video streaming endpoints for watching browser sessions. These endpoints are separate from the tool-based API and provide direct video access.

### Get Single Frame

Get the latest frame from a browser context as a JPEG image.

```
GET /video/frame/{context_id}
Authorization: Bearer <token>
```

**Response:**
- Content-Type: `image/jpeg`
- Body: Raw JPEG image data

**Example:**
```bash
curl -H "Authorization: Bearer token" \
  http://localhost:8080/video/frame/ctx_000001 \
  --output frame.jpg
```

---

### MJPEG Stream

Get a continuous MJPEG video stream from a browser context. The stream automatically starts live streaming for the context if not already active.

```
GET /video/stream/{context_id}[?fps=N]
Authorization: Bearer <token>
```

**Query Parameters:**

| Name | Type | Default | Description |
|------|------|---------|-------------|
| fps | integer | 15 | Target frames per second (1-60) |

**Response:**
- Content-Type: `multipart/x-mixed-replace; boundary=owlboundary`
- Body: Continuous MJPEG stream

**Example:**
```bash
# Stream to file for 5 seconds
curl -H "Authorization: Bearer token" \
  "http://localhost:8080/video/stream/ctx_000001?fps=15" \
  --max-time 5 \
  --output stream.mjpeg

# View in browser or media player that supports MJPEG
# Most browsers can display MJPEG directly in an <img> tag
```

**HTML Usage:**
```html
<img src="http://localhost:8080/video/stream/ctx_000001?fps=15"
     alt="Browser Stream" />
```

---

### List Active Streams

List all active video streams.

```
GET /video/list
Authorization: Bearer <token>
```

**Response:**
```json
{
  "success": true,
  "streams": [
    {
      "context_id": "ctx_000001",
      "fps": 15,
      "quality": 75,
      "subscribers": 1
    }
  ]
}
```

---

### Stream Statistics

Get video streaming statistics for all active streams.

```
GET /video/stats
Authorization: Bearer <token>
```

**Response:**
```json
{
  "success": true,
  "stats": {
    "active_streams": 1,
    "total_frames_sent": 450,
    "total_bytes_sent": 2500000
  }
}
```

---

## Tools Reference

### Context Management

#### browser_create_context

Create a new browser context (isolated tab/window).

```
POST /execute/browser_create_context
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| llm_enabled | boolean | No | Enable LLM features (default: true) |
| llm_use_builtin | boolean | No | Use built-in llama-server (default: true) |
| llm_endpoint | string | No | External LLM API endpoint |
| llm_model | string | No | External LLM model name |
| llm_api_key | string | No | External LLM API key |
| profile_path | string | No | Path to browser profile JSON |
| proxy_type | enum | No | `http`, `https`, `socks4`, `socks5`, `socks5h` |
| proxy_host | string | No | Proxy server hostname |
| proxy_port | integer | No | Proxy server port |
| proxy_username | string | No | Proxy auth username |
| proxy_password | string | No | Proxy auth password |
| proxy_stealth | boolean | No | Enable stealth mode (default: true when proxy used) |
| proxy_ca_cert_path | string | No | Path to custom CA certificate |
| proxy_trust_custom_ca | boolean | No | Trust custom CA certificate |

**Example:**
```bash
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  http://localhost:8080/execute/browser_create_context
```

**Response:**
```json
{
  "success": true,
  "result": "ctx_000001"
}
```

---

#### browser_close_context

Close a browser context.

```
POST /execute/browser_close_context
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID to close |

**Example:**
```bash
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_000001"}' \
  http://localhost:8080/execute/browser_close_context
```

---

#### browser_list_contexts

List all active browser contexts.

```
POST /execute/browser_list_contexts
```

**Parameters:** None

---

### Navigation

#### browser_navigate

Navigate to a URL.

```
POST /execute/browser_navigate
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| url | string | Yes | The URL to navigate to |

**Example:**
```bash
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_000001", "url": "https://example.com"}' \
  http://localhost:8080/execute/browser_navigate
```

**Response:**
```json
{
  "success": true,
  "result": true
}
```

---

#### browser_reload

Reload the current page.

```
POST /execute/browser_reload
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| ignore_cache | boolean | No | Bypass cache (hard reload, default: false) |

---

#### browser_go_back

Navigate back in browser history.

```
POST /execute/browser_go_back
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

---

#### browser_go_forward

Navigate forward in browser history.

```
POST /execute/browser_go_forward
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

---

### Interaction

#### browser_click

Click an element on the page.

```
POST /execute/browser_click
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| selector | string | Yes | CSS selector, position (e.g., "100x200"), or semantic description (e.g., "login button") |

**Examples:**
```bash
# By CSS selector
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_000001", "selector": "#submit-btn"}' \
  http://localhost:8080/execute/browser_click

# By position
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_000001", "selector": "100x200"}' \
  http://localhost:8080/execute/browser_click

# By semantic description (AI-powered)
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_000001", "selector": "login button"}' \
  http://localhost:8080/execute/browser_click
```

---

#### browser_type

Type text into an input field.

```
POST /execute/browser_type
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| selector | string | Yes | CSS selector, position, or semantic description |
| text | string | Yes | Text to type |

**Example:**
```bash
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_000001", "selector": "#email", "text": "user@example.com"}' \
  http://localhost:8080/execute/browser_type
```

---

#### browser_pick

Select an option from a dropdown/select element.

```
POST /execute/browser_pick
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| selector | string | Yes | CSS selector or semantic description |
| value | string | Yes | Value or visible text to select |

**Example:**
```bash
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_000001", "selector": "#country", "value": "United States"}' \
  http://localhost:8080/execute/browser_pick
```

---

#### browser_press_key

Press a special key.

```
POST /execute/browser_press_key
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| key | enum | Yes | Key name (see below) |

**Valid Keys:**
`Enter`, `Return`, `Tab`, `Escape`, `Esc`, `Backspace`, `Delete`, `Del`,
`ArrowUp`, `Up`, `ArrowDown`, `Down`, `ArrowLeft`, `Left`, `ArrowRight`, `Right`,
`Space`, `Home`, `End`, `PageUp`, `PageDown`

**Example:**
```bash
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_000001", "key": "Enter"}' \
  http://localhost:8080/execute/browser_press_key
```

---

#### browser_submit_form

Submit the currently focused form by pressing Enter.

```
POST /execute/browser_submit_form
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

---

### Content Extraction

#### browser_extract_text

Extract text content from the page.

```
POST /execute/browser_extract_text
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| selector | string | No | CSS selector or semantic description (extracts whole page if not provided) |

**Response:**
```json
{
  "success": true,
  "result": "Example Domain\n\nThis domain is for use in illustrative examples..."
}
```

---

#### browser_screenshot

Take a screenshot of the current page.

```
POST /execute/browser_screenshot
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

**Response:**
```json
{
  "success": true,
  "result": "iVBORw0KGgoAAAANSUhEUgAAB4AAAAQ4CAY..."
}
```

The `result` is a **base64-encoded PNG image**.

**Decoding the Screenshot:**
```bash
# Using jq and base64
curl -s -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_000001"}' \
  http://localhost:8080/execute/browser_screenshot \
  | jq -r '.result' | base64 -d > screenshot.png

# Image info
# Format: PNG
# Resolution: 1920 x 1080 (default viewport)
# Color: 8-bit RGBA
```

---

#### browser_highlight

Highlight an element with colored border for debugging.

```
POST /execute/browser_highlight
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| selector | string | Yes | CSS selector or semantic description |
| border_color | string | No | CSS color (default: "#FF0000") |
| background_color | string | No | CSS color with alpha (default: "rgba(255, 0, 0, 0.2)") |

---

#### browser_get_html

Extract clean HTML from the page.

```
POST /execute/browser_get_html
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| clean_level | enum | No | `minimal`, `basic`, `aggressive` (default: basic) |

---

#### browser_get_markdown

Extract page content as clean Markdown.

```
POST /execute/browser_get_markdown
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| include_links | boolean | No | Include links (default: true) |
| include_images | boolean | No | Include images (default: true) |
| max_length | integer | No | Max length in characters (-1 for no limit) |

---

#### browser_extract_json

Extract structured JSON data using templates.

```
POST /execute/browser_extract_json
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| template | string | No | Template name or empty for auto-detect |

**Available Templates:**
- `google_search`
- `wikipedia`
- `amazon_product`
- `github_repo`
- `twitter_feed`
- `reddit_thread`

---

#### browser_detect_site

Detect website type for template matching.

```
POST /execute/browser_detect_site
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

---

#### browser_list_templates

List available extraction templates.

```
POST /execute/browser_list_templates
```

**Parameters:** None

---

### AI/LLM Features

#### browser_summarize_page

Create an intelligent, structured summary using LLM.

```
POST /execute/browser_summarize_page
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| force_refresh | boolean | No | Ignore cache (default: false) |

---

#### browser_query_page

Ask a natural language question about the page.

```
POST /execute/browser_query_page
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| query | string | Yes | Question about the page |

**Example:**
```bash
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_000001", "query": "What is the main topic of this page?"}' \
  http://localhost:8080/execute/browser_query_page
```

---

#### browser_llm_status

Check if the on-device LLM is ready.

```
POST /execute/browser_llm_status
```

**Parameters:** None

**Response:**
```json
{
  "success": true,
  "result": "ready"
}
```

**Status Values:** `ready`, `loading`, `unavailable`

---

#### browser_nla

Execute natural language browser commands.

```
POST /execute/browser_nla
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| command | string | Yes | Natural language command |

**Example:**
```bash
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_000001", "command": "go to google and search for weather"}' \
  http://localhost:8080/execute/browser_nla
```

---

### Scroll Control

#### browser_scroll_by

Scroll the page by specified pixels.

```
POST /execute/browser_scroll_by
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| y | integer | Yes | Vertical scroll (pixels, positive = down) |
| x | integer | No | Horizontal scroll (pixels, default: 0) |

---

#### browser_scroll_to_element

Scroll element into view.

```
POST /execute/browser_scroll_to_element
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| selector | string | Yes | CSS selector or semantic description |

---

#### browser_scroll_to_top

Scroll to top of page.

```
POST /execute/browser_scroll_to_top
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

---

#### browser_scroll_to_bottom

Scroll to bottom of page.

```
POST /execute/browser_scroll_to_bottom
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

---

### Wait Utilities

#### browser_wait_for_selector

Wait for an element to appear on the page.

```
POST /execute/browser_wait_for_selector
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| selector | string | Yes | CSS selector or semantic description |
| timeout | integer | No | Timeout in milliseconds (default: 5000) |

---

#### browser_wait

Wait for specified milliseconds.

```
POST /execute/browser_wait
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| timeout | integer | Yes | Time to wait in milliseconds |

---

### Page Info

#### browser_get_page_info

Get current page information.

```
POST /execute/browser_get_page_info
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

**Response:**
```json
{
  "success": true,
  "result": {
    "url": "https://example.com/",
    "title": "Example Domain",
    "can_go_back": true,
    "can_go_forward": false
  }
}
```

---

#### browser_set_viewport

Change browser viewport size.

```
POST /execute/browser_set_viewport
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| width | integer | Yes | Viewport width in pixels |
| height | integer | Yes | Viewport height in pixels |

---

### Video Recording

#### browser_start_video_recording

Start video recording of the browser session.

```
POST /execute/browser_start_video_recording
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| fps | integer | No | Frames per second (default: 30) |
| codec | string | No | Video codec (default: "libx264") |

---

#### browser_pause_video_recording

Pause video recording.

```
POST /execute/browser_pause_video_recording
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

---

#### browser_resume_video_recording

Resume paused recording.

```
POST /execute/browser_resume_video_recording
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

---

#### browser_stop_video_recording

Stop recording and save the video file.

```
POST /execute/browser_stop_video_recording
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

**Response:**
```json
{
  "success": true,
  "result": "/tmp/owl_video_123456.mp4"
}
```

---

#### browser_get_video_recording_stats

Get recording statistics.

```
POST /execute/browser_get_video_recording_stats
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

---

### Live Video Streaming

Tools for managing real-time MJPEG video streams from browser contexts. These streams can be consumed via the `/video/stream/{context_id}` endpoint.

#### browser_start_live_stream

Start live video streaming for a browser context. Frames are encoded as JPEG and made available for MJPEG streaming.

```
POST /execute/browser_start_live_stream
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| fps | integer | No | Target frames per second (default: 15, range: 1-60) |
| quality | integer | No | JPEG quality (default: 75, range: 10-100) |

**Example:**
```bash
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_000001", "fps": 15, "quality": 75}' \
  http://localhost:8080/execute/browser_start_live_stream
```

**Response:**
```json
{
  "success": true,
  "result": true
}
```

---

#### browser_stop_live_stream

Stop live video streaming for a browser context.

```
POST /execute/browser_stop_live_stream
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

---

#### browser_get_live_stream_stats

Get statistics for a live video stream.

```
POST /execute/browser_get_live_stream_stats
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

**Response:**
```json
{
  "success": true,
  "result": {
    "target_fps": 15,
    "actual_fps": 14,
    "width": 1920,
    "height": 1080,
    "frames_received": 1500,
    "frames_encoded": 1400,
    "frames_sent": 1400,
    "frames_dropped": 100,
    "subscriber_count": 1,
    "is_active": true
  }
}
```

---

#### browser_list_live_streams

List all active live video streams.

```
POST /execute/browser_list_live_streams
```

**Parameters:** None

**Response:**
```json
{
  "success": true,
  "result": ["ctx_000001", "ctx_000002"]
}
```

---

### Demographics

#### browser_get_demographics

Get complete user demographics and context.

```
POST /execute/browser_get_demographics
```

**Parameters:** None

---

#### browser_get_location

Get user's current location based on IP.

```
POST /execute/browser_get_location
```

**Parameters:** None

**Response:**
```json
{
  "success": true,
  "result": {
    "city": "San Francisco",
    "country": "United States",
    "latitude": 37.7749,
    "longitude": -122.4194
  }
}
```

---

#### browser_get_datetime

Get current date and time information.

```
POST /execute/browser_get_datetime
```

**Parameters:** None

---

#### browser_get_weather

Get current weather for user's location.

```
POST /execute/browser_get_weather
```

**Parameters:** None

---

### CAPTCHA Handling

#### browser_detect_captcha

Detect if the current page has a CAPTCHA.

```
POST /execute/browser_detect_captcha
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

---

#### browser_classify_captcha

Classify the type of CAPTCHA on the page.

```
POST /execute/browser_classify_captcha
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

**CAPTCHA Types:**
- `text-based`
- `image-selection`
- `checkbox`
- `puzzle`
- `audio`
- `custom`

---

#### browser_solve_text_captcha

Solve a text-based CAPTCHA using vision model.

```
POST /execute/browser_solve_text_captcha
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| max_attempts | integer | No | Maximum attempts (default: 3) |

---

#### browser_solve_image_captcha

Solve an image-selection CAPTCHA.

```
POST /execute/browser_solve_image_captcha
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| max_attempts | integer | No | Maximum attempts (default: 3) |

---

#### browser_solve_captcha

Auto-detect and solve any supported CAPTCHA type.

```
POST /execute/browser_solve_captcha
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| max_attempts | integer | No | Maximum attempts (default: 3) |

---

### Cookies

#### browser_get_cookies

Get cookies from the browser context.

```
POST /execute/browser_get_cookies
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| url | string | No | Filter cookies by URL |

**Response:**
```json
{
  "success": true,
  "result": [
    {
      "name": "session_id",
      "value": "abc123",
      "domain": ".example.com",
      "path": "/",
      "secure": true,
      "httpOnly": true,
      "sameSite": "lax",
      "expires": 1735689600
    }
  ]
}
```

---

#### browser_set_cookie

Set a cookie in the browser context.

```
POST /execute/browser_set_cookie
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| url | string | Yes | URL to associate with cookie |
| name | string | Yes | Cookie name |
| value | string | Yes | Cookie value |
| domain | string | No | Cookie domain |
| path | string | No | Cookie path (default: "/") |
| secure | boolean | No | Secure flag (default: false) |
| httpOnly | boolean | No | HttpOnly flag (default: false) |
| sameSite | enum | No | `none`, `lax`, `strict` (default: "lax") |
| expires | integer | No | Unix timestamp (-1 for session cookie) |

---

#### browser_delete_cookies

Delete cookies from the browser context.

```
POST /execute/browser_delete_cookies
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| url | string | No | Filter by URL |
| cookie_name | string | No | Specific cookie to delete |

---

### Proxy

#### browser_set_proxy

Configure proxy settings for a context.

```
POST /execute/browser_set_proxy
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| type | enum | Yes | `http`, `https`, `socks4`, `socks5`, `socks5h` |
| host | string | Yes | Proxy server hostname |
| port | integer | Yes | Proxy server port |
| username | string | No | Auth username |
| password | string | No | Auth password |
| stealth | boolean | No | Enable stealth mode (default: true) |
| block_webrtc | boolean | No | Block WebRTC leaks (default: true) |
| spoof_timezone | boolean | No | Spoof timezone (default: false) |
| spoof_language | boolean | No | Spoof language (default: false) |
| timezone_override | string | No | Manual timezone (e.g., "America/New_York") |
| language_override | string | No | Manual language (e.g., "en-US") |

---

#### browser_get_proxy_status

Get current proxy configuration and status.

```
POST /execute/browser_get_proxy_status
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

---

#### browser_connect_proxy

Enable/connect the configured proxy.

```
POST /execute/browser_connect_proxy
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

---

#### browser_disconnect_proxy

Disable/disconnect the proxy.

```
POST /execute/browser_disconnect_proxy
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

---

### Browser Profiles

#### browser_create_profile

Create a new browser profile with randomized fingerprints.

```
POST /execute/browser_create_profile
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| name | string | No | Human-readable profile name |

---

#### browser_load_profile

Load a browser profile from a JSON file.

```
POST /execute/browser_load_profile
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| profile_path | string | Yes | Path to profile JSON file |

---

#### browser_save_profile

Save the current context state to a profile file.

```
POST /execute/browser_save_profile
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| profile_path | string | No | Path to save (optional if previously loaded) |

---

#### browser_get_profile

Get the current profile state as JSON.

```
POST /execute/browser_get_profile
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

---

#### browser_update_profile_cookies

Update the profile file with current cookies.

```
POST /execute/browser_update_profile_cookies
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

---

### Network Interception

#### browser_add_network_rule

Add a network interception rule (block, mock, redirect).

```
POST /execute/browser_add_network_rule
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| url_pattern | string | Yes | URL pattern to match (glob or regex) |
| action | enum | Yes | `allow`, `block`, `mock`, `redirect` |
| is_regex | boolean | No | Whether url_pattern is a regex (default: false for glob) |
| redirect_url | string | No | URL to redirect to (for redirect action) |
| mock_body | string | No | Response body to return (for mock action) |
| mock_status | integer | No | HTTP status code for mock response (default: 200) |
| mock_content_type | string | No | Content-Type header for mock response |

**Example - Block ads:**
```bash
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{
    "context_id": "ctx_000001",
    "url_pattern": "*://ads.example.com/*",
    "action": "block"
  }' \
  http://localhost:8080/execute/browser_add_network_rule
```

**Example - Mock API response:**
```bash
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{
    "context_id": "ctx_000001",
    "url_pattern": "*/api/data",
    "action": "mock",
    "mock_body": "{\"status\": \"ok\"}",
    "mock_status": 200,
    "mock_content_type": "application/json"
  }' \
  http://localhost:8080/execute/browser_add_network_rule
```

**Response:**
```json
{
  "success": true,
  "result": "rule_000001"
}
```

---

#### browser_remove_network_rule

Remove a network interception rule by ID.

```
POST /execute/browser_remove_network_rule
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| rule_id | string | Yes | The rule ID to remove |

---

#### browser_enable_network_interception

Enable or disable network interception for a context.

```
POST /execute/browser_enable_network_interception
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| enabled | boolean | Yes | Enable or disable interception |

---

#### browser_get_network_log

Get the network request log for a context.

```
POST /execute/browser_get_network_log
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| limit | integer | No | Maximum number of entries to return |

**Response:**
```json
{
  "success": true,
  "result": [
    {
      "url": "https://example.com/api",
      "method": "GET",
      "status": 200,
      "timestamp": 1702000000000,
      "intercepted": false
    }
  ]
}
```

---

#### browser_clear_network_log

Clear the network request log for a context.

```
POST /execute/browser_clear_network_log
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

---

### File Downloads

#### browser_set_download_path

Set the download directory for file downloads.

```
POST /execute/browser_set_download_path
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| path | string | Yes | Directory path for downloads |

**Example:**
```bash
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{
    "context_id": "ctx_000001",
    "path": "/tmp/downloads"
  }' \
  http://localhost:8080/execute/browser_set_download_path
```

---

#### browser_get_downloads

Get list of downloads for a context.

```
POST /execute/browser_get_downloads
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

**Response:**
```json
{
  "success": true,
  "result": [
    {
      "id": "dl_000001",
      "url": "https://example.com/file.pdf",
      "filename": "file.pdf",
      "path": "/tmp/downloads/file.pdf",
      "status": "completed",
      "bytes_received": 1048576,
      "total_bytes": 1048576
    }
  ]
}
```

**Status Values:** `pending`, `in_progress`, `completed`, `cancelled`, `failed`

---

#### browser_wait_for_download

Wait for a download to complete.

```
POST /execute/browser_wait_for_download
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| download_id | string | No | Specific download ID to wait for |
| timeout | integer | No | Timeout in milliseconds (default: 30000) |

---

#### browser_cancel_download

Cancel a download in progress.

```
POST /execute/browser_cancel_download
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| download_id | string | Yes | The download ID to cancel |

---

### Dialog Handling

#### browser_set_dialog_action

Configure auto-handling policy for JavaScript dialogs.

```
POST /execute/browser_set_dialog_action
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| dialog_type | enum | Yes | `alert`, `confirm`, `prompt`, `beforeunload` |
| action | enum | Yes | `accept`, `dismiss`, `accept_with_text` |
| prompt_text | string | No | Text to enter for prompt dialogs (when action is accept_with_text) |

**Example - Auto-accept all alerts:**
```bash
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{
    "context_id": "ctx_000001",
    "dialog_type": "alert",
    "action": "accept"
  }' \
  http://localhost:8080/execute/browser_set_dialog_action
```

**Example - Auto-respond to prompts:**
```bash
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{
    "context_id": "ctx_000001",
    "dialog_type": "prompt",
    "action": "accept_with_text",
    "prompt_text": "Automated response"
  }' \
  http://localhost:8080/execute/browser_set_dialog_action
```

---

#### browser_get_pending_dialog

Get information about a pending dialog.

```
POST /execute/browser_get_pending_dialog
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

**Response (when dialog is pending):**
```json
{
  "success": true,
  "result": {
    "id": "dialog_000001",
    "type": "confirm",
    "message": "Are you sure you want to leave?",
    "default_value": ""
  }
}
```

**Response (no dialog):**
```json
{
  "success": true,
  "result": null
}
```

---

#### browser_handle_dialog

Accept or dismiss a specific pending dialog.

```
POST /execute/browser_handle_dialog
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| dialog_id | string | Yes | The dialog ID from get_pending_dialog |
| accept | boolean | Yes | True to accept, false to dismiss |
| response_text | string | No | Text response for prompt dialogs |

**Example:**
```bash
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{
    "dialog_id": "dialog_000001",
    "accept": true,
    "response_text": "My answer"
  }' \
  http://localhost:8080/execute/browser_handle_dialog
```

---

#### browser_wait_for_dialog

Wait for a dialog to appear.

```
POST /execute/browser_wait_for_dialog
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| timeout | integer | No | Timeout in milliseconds (default: 5000) |

**Response:**
```json
{
  "success": true,
  "result": {
    "id": "dialog_000001",
    "type": "alert",
    "message": "Hello World!"
  }
}
```

---

### Tab/Window Management

#### browser_new_tab

Create a new tab within a context.

```
POST /execute/browser_new_tab
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| url | string | No | URL to navigate to in the new tab |

**Example:**
```bash
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{
    "context_id": "ctx_000001",
    "url": "https://google.com"
  }' \
  http://localhost:8080/execute/browser_new_tab
```

**Response:**
```json
{
  "success": true,
  "result": {
    "tab_id": "tab_000002",
    "url": "https://google.com"
  }
}
```

---

#### browser_get_tabs

List all tabs in a context.

```
POST /execute/browser_get_tabs
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

**Response:**
```json
{
  "success": true,
  "result": [
    {
      "tab_id": "tab_000001",
      "url": "https://example.com",
      "title": "Example Domain",
      "active": false
    },
    {
      "tab_id": "tab_000002",
      "url": "https://google.com",
      "title": "Google",
      "active": true
    }
  ]
}
```

---

#### browser_switch_tab

Switch to a specific tab.

```
POST /execute/browser_switch_tab
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| tab_id | string | Yes | The tab ID to switch to |

---

#### browser_get_active_tab

Get the currently active tab.

```
POST /execute/browser_get_active_tab
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

**Response:**
```json
{
  "success": true,
  "result": {
    "tab_id": "tab_000002",
    "url": "https://google.com",
    "title": "Google"
  }
}
```

---

#### browser_close_tab

Close a tab.

```
POST /execute/browser_close_tab
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| tab_id | string | Yes | The tab ID to close |

**Note:** Cannot close the last remaining tab in a context.

---

#### browser_get_tab_count

Get the number of tabs in a context.

```
POST /execute/browser_get_tab_count
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

**Response:**
```json
{
  "success": true,
  "result": 2
}
```

---

#### browser_set_popup_policy

Configure how popups are handled.

```
POST /execute/browser_set_popup_policy
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |
| policy | enum | Yes | `allow`, `block`, `new_tab`, `background` |

**Policy Values:**
- `allow` - Allow popups to open in new windows
- `block` - Block all popups
- `new_tab` - Open popups as new tabs in the same context
- `background` - Open popups in background tabs

**Example:**
```bash
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{
    "context_id": "ctx_000001",
    "policy": "block"
  }' \
  http://localhost:8080/execute/browser_set_popup_policy
```

---

#### browser_get_blocked_popups

Get list of blocked popup URLs.

```
POST /execute/browser_get_blocked_popups
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| context_id | string | Yes | The context ID |

**Response:**
```json
{
  "success": true,
  "result": [
    "https://ads.example.com/popup",
    "https://spam.example.com/offer"
  ]
}
```

---

### License Management

Tools for managing the browser license.

#### browser_get_license_status

Get the current license status.

```
POST /execute/browser_get_license_status
```

**Parameters:** None

**Response:**
```json
{
  "success": true,
  "result": {
    "status": "valid",
    "valid": true
  }
}
```

**Status Values:**
- `valid` - License is valid and active
- `not_found` - No license file found
- `expired` - License has expired
- `corrupted` - License file is invalid
- `hardware_mismatch` - Hardware-bound license on wrong machine

---

#### browser_get_license_info

Get detailed license information.

```
POST /execute/browser_get_license_info
```

**Parameters:** None

**Response:**
```json
{
  "success": true,
  "result": {
    "license_id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
    "name": "Organization Name",
    "type": "professional",
    "expires": "2025-12-31",
    "features": ["video_recording", "captcha_solving", "live_streaming"]
  }
}
```

---

#### browser_get_hardware_fingerprint

Get the hardware fingerprint for license activation. This fingerprint is required when requesting a hardware-bound license.

```
POST /execute/browser_get_hardware_fingerprint
```

**Parameters:** None

**Response:**
```json
{
  "success": true,
  "result": {
    "fingerprint": "44d1d03688c0b246410fb81afbffb4c7bba52d94edfef5a10ca035813199af9d"
  }
}
```

---

#### browser_add_license

Add/activate a license. Provide either a file path or base64-encoded license content.

```
POST /execute/browser_add_license
```

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| license_path | string | No* | Path to the license file (.olic) |
| license_content | string | No* | Base64-encoded license file content |

*One of `license_path` or `license_content` must be provided.

**Example (Base64):**
```bash
# Encode license file to base64
LICENSE_CONTENT=$(base64 -i /path/to/license.olic)

# Add license via API
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d "{\"license_content\": \"$LICENSE_CONTENT\"}" \
  http://localhost:8080/execute/browser_add_license
```

**Example (File Path):**
```bash
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{"license_path": "/app/license.olic"}' \
  http://localhost:8080/execute/browser_add_license
```

**Response:**
```json
{
  "success": true,
  "result": {
    "success": true,
    "status": "valid",
    "license": {
      "license_id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
      "name": "Your Name",
      "type": "professional",
      "expires": "2025-12-31"
    }
  }
}
```

---

#### browser_remove_license

Remove the current license.

```
POST /execute/browser_remove_license
```

**Parameters:** None

**Response:**
```json
{
  "success": true,
  "result": true
}
```

---

## Quick Start Example

Complete workflow example:

```bash
# 1. Check server health
curl http://localhost:8080/health

# 2. Create a browser context
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  http://localhost:8080/execute/browser_create_context
# Response: {"success":true,"result":"ctx_000001"}

# 3. Navigate to a website
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_000001", "url": "https://example.com"}' \
  http://localhost:8080/execute/browser_navigate

# 4. Wait for page to load
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_000001", "timeout": 2000}' \
  http://localhost:8080/execute/browser_wait

# 5. Take a screenshot
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_000001"}' \
  http://localhost:8080/execute/browser_screenshot \
  | jq -r '.result' | base64 -d > screenshot.png

# 6. Extract page text
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_000001"}' \
  http://localhost:8080/execute/browser_extract_text

# 7. Close the context
curl -X POST -H "Authorization: Bearer token" \
  -H "Content-Type: application/json" \
  -d '{"context_id": "ctx_000001"}' \
  http://localhost:8080/execute/browser_close_context
```

---

## Rate Limits

The server does not impose rate limits. However, browser operations are sequential within a context. For parallel operations, create multiple contexts.

---

## WebSocket API

The server supports two-way WebSocket communication for real-time browser control. The WebSocket endpoint uses the same authentication as the REST API.

### Connection

**Endpoint:** `ws://{host}:{port}/ws` (or `wss://` for SSL)

**Authentication:** Pass your Bearer token in the `Authorization` header during the WebSocket handshake.

```javascript
// Browser JavaScript
const ws = new WebSocket('ws://localhost:8080/ws', [], {
  headers: {
    'Authorization': 'Bearer your-token'
  }
});

// Or using query parameter for environments that don't support headers
const ws = new WebSocket('ws://localhost:8080/ws?token=your-token');
```

### Message Format

#### Request
```json
{
  "id": 1,
  "method": "browser_navigate",
  "params": {
    "context_id": "ctx_000001",
    "url": "https://example.com"
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| id | integer | No | Request ID for correlating responses |
| method | string | Yes | Tool name (e.g., `browser_navigate`) |
| params | object | No | Tool parameters |

#### Success Response
```json
{
  "id": 1,
  "success": true,
  "result": true
}
```

#### Error Response
```json
{
  "id": 1,
  "success": false,
  "error": "Browser not ready"
}
```

### Example: JavaScript WebSocket Client

```javascript
class OwlBrowserWS {
  constructor(url, token) {
    this.url = url;
    this.token = token;
    this.ws = null;
    this.requestId = 0;
    this.pending = new Map();
  }

  connect() {
    return new Promise((resolve, reject) => {
      // Note: In browsers, you may need to pass token via query string
      // since custom headers aren't supported in WebSocket constructor
      this.ws = new WebSocket(this.url);

      this.ws.onopen = () => {
        console.log('Connected to Owl Browser');
        resolve();
      };

      this.ws.onmessage = (event) => {
        const response = JSON.parse(event.data);
        if (response.id !== undefined && this.pending.has(response.id)) {
          const { resolve, reject } = this.pending.get(response.id);
          this.pending.delete(response.id);
          if (response.success) {
            resolve(response.result);
          } else {
            reject(new Error(response.error));
          }
        }
      };

      this.ws.onerror = (error) => {
        reject(error);
      };

      this.ws.onclose = () => {
        console.log('Disconnected from Owl Browser');
      };
    });
  }

  send(method, params = {}) {
    return new Promise((resolve, reject) => {
      const id = ++this.requestId;
      this.pending.set(id, { resolve, reject });

      this.ws.send(JSON.stringify({
        id,
        method,
        params
      }));

      // Timeout after 60 seconds
      setTimeout(() => {
        if (this.pending.has(id)) {
          this.pending.delete(id);
          reject(new Error('Request timeout'));
        }
      }, 60000);
    });
  }

  async createContext() {
    return this.send('browser_create_context');
  }

  async navigate(contextId, url) {
    return this.send('browser_navigate', { context_id: contextId, url });
  }

  async click(contextId, selector) {
    return this.send('browser_click', { context_id: contextId, selector });
  }

  async screenshot(contextId) {
    return this.send('browser_screenshot', { context_id: contextId });
  }

  close() {
    if (this.ws) {
      this.ws.close();
    }
  }
}

// Usage
(async () => {
  const browser = new OwlBrowserWS('ws://localhost:8080/ws', 'your-token');
  await browser.connect();

  const ctx = await browser.createContext();
  console.log('Created context:', ctx);

  await browser.navigate(ctx, 'https://example.com');
  console.log('Navigated to example.com');

  const screenshot = await browser.screenshot(ctx);
  console.log('Screenshot taken:', screenshot.substring(0, 50) + '...');

  browser.close();
})();
```

### Example: Python WebSocket Client

```python
import asyncio
import json
import websockets

class OwlBrowserWS:
    def __init__(self, url, token):
        self.url = url
        self.token = token
        self.ws = None
        self.request_id = 0
        self.pending = {}

    async def connect(self):
        headers = {"Authorization": f"Bearer {self.token}"}
        self.ws = await websockets.connect(self.url, extra_headers=headers)
        asyncio.create_task(self._receive_loop())

    async def _receive_loop(self):
        try:
            async for message in self.ws:
                response = json.loads(message)
                if "id" in response and response["id"] in self.pending:
                    future = self.pending.pop(response["id"])
                    if response.get("success"):
                        future.set_result(response.get("result"))
                    else:
                        future.set_exception(Exception(response.get("error")))
        except websockets.exceptions.ConnectionClosed:
            pass

    async def send(self, method, params=None):
        self.request_id += 1
        request_id = self.request_id

        future = asyncio.get_event_loop().create_future()
        self.pending[request_id] = future

        await self.ws.send(json.dumps({
            "id": request_id,
            "method": method,
            "params": params or {}
        }))

        return await asyncio.wait_for(future, timeout=60)

    async def create_context(self):
        return await self.send("browser_create_context")

    async def navigate(self, context_id, url):
        return await self.send("browser_navigate", {
            "context_id": context_id,
            "url": url
        })

    async def screenshot(self, context_id):
        return await self.send("browser_screenshot", {
            "context_id": context_id
        })

    async def close(self):
        if self.ws:
            await self.ws.close()

# Usage
async def main():
    browser = OwlBrowserWS("ws://localhost:8080/ws", "your-token")
    await browser.connect()

    ctx = await browser.create_context()
    print(f"Created context: {ctx}")

    await browser.navigate(ctx, "https://example.com")
    print("Navigated to example.com")

    screenshot = await browser.screenshot(ctx)
    print(f"Screenshot taken: {len(screenshot)} bytes")

    await browser.close()

asyncio.run(main())
```

### WebSocket Configuration

WebSocket behavior can be configured via environment variables or config file:

| Variable | Default | Description |
|----------|---------|-------------|
| OWL_WS_ENABLED | true | Enable WebSocket support |
| OWL_WS_MAX_CONNECTIONS | 50 | Max concurrent WebSocket connections |
| OWL_WS_MESSAGE_MAX_SIZE | 16777216 | Max message size (16MB) |
| OWL_WS_PING_INTERVAL | 30 | Ping interval in seconds |
| OWL_WS_PONG_TIMEOUT | 10 | Pong timeout in seconds |

### Benefits of WebSocket over REST

1. **Lower Latency:** No HTTP overhead for each request
2. **Real-time:** Instant response delivery
3. **Efficient:** Single connection for multiple commands
4. **Bidirectional:** Server can push events (future feature)

### When to Use WebSocket vs REST

| Use Case | Recommended |
|----------|-------------|
| Simple automation scripts | REST API |
| Interactive applications | WebSocket |
| High-frequency operations | WebSocket |
| Single one-off commands | REST API |
| Long-running sessions | WebSocket |

---

## SDK Examples

### Python

```python
import requests
import base64

class OwlBrowser:
    def __init__(self, base_url, token):
        self.base_url = base_url
        self.headers = {
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json"
        }

    def execute(self, tool, params=None):
        response = requests.post(
            f"{self.base_url}/execute/{tool}",
            headers=self.headers,
            json=params or {}
        )
        return response.json()

    def create_context(self):
        return self.execute("browser_create_context")["result"]

    def navigate(self, context_id, url):
        return self.execute("browser_navigate", {
            "context_id": context_id,
            "url": url
        })

    def screenshot(self, context_id, filename):
        result = self.execute("browser_screenshot", {
            "context_id": context_id
        })
        with open(filename, "wb") as f:
            f.write(base64.b64decode(result["result"]))

# Usage
browser = OwlBrowser("http://localhost:8080", "your-token")
ctx = browser.create_context()
browser.navigate(ctx, "https://example.com")
browser.screenshot(ctx, "screenshot.png")
```

### JavaScript/Node.js

```javascript
const axios = require('axios');
const fs = require('fs');

class OwlBrowser {
  constructor(baseUrl, token) {
    this.client = axios.create({
      baseURL: baseUrl,
      headers: {
        'Authorization': `Bearer ${token}`,
        'Content-Type': 'application/json'
      }
    });
  }

  async execute(tool, params = {}) {
    const response = await this.client.post(`/execute/${tool}`, params);
    return response.data;
  }

  async createContext() {
    const result = await this.execute('browser_create_context');
    return result.result;
  }

  async navigate(contextId, url) {
    return this.execute('browser_navigate', { context_id: contextId, url });
  }

  async screenshot(contextId, filename) {
    const result = await this.execute('browser_screenshot', { context_id: contextId });
    const buffer = Buffer.from(result.result, 'base64');
    fs.writeFileSync(filename, buffer);
  }
}

// Usage
(async () => {
  const browser = new OwlBrowser('http://localhost:8080', 'your-token');
  const ctx = await browser.createContext();
  await browser.navigate(ctx, 'https://example.com');
  await browser.screenshot(ctx, 'screenshot.png');
})();
```

---

*Generated for Owl Browser HTTP Server v1.3.0*
