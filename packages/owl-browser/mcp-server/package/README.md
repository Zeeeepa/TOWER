# Owl Browser MCP Server

MCP (Model Context Protocol) server for Owl Browser HTTP API. This server dynamically generates 157 browser automation tools from the embedded OpenAPI schema.

## Features

- **157 Browser Tools**: Full browser automation including navigation, clicks, typing, screenshots, CAPTCHA solving, and more
- **Multiple Contexts**: Create and manage multiple isolated browser contexts (sessions)
- **Anti-Detection**: Built-in fingerprint management, proxy support, and stealth features
- **HTTP API Backend**: Connects to Owl Browser's HTTP server (Docker or standalone)

## Installation

### Via npx (recommended)

No installation needed - just configure Claude Desktop to use:
```json
"command": "npx",
"args": ["-y", "@olib-ai/owl-browser-mcp"]
```

### Global install

```bash
npm install -g @olib-ai/owl-browser-mcp
```

Then use in Claude Desktop config:
```json
"command": "owl-browser-mcp"
```

### From source

```bash
git clone https://github.com/Olib-AI/owl-browser-mcp
cd owl-browser-mcp
npm install
npm run build
```

## Architecture: Understanding Ports

The Owl Browser exposes two ports with different purposes:

| Port | Service | Description |
|------|---------|-------------|
| **80** | Nginx Proxy | Web control panel + API gateway. **Recommended for Docker deployments.** Routes `/api/*`, `/execute/*`, `/ws` to the HTTP server. |
| **8080** | HTTP Server | Direct REST API access. Use for standalone (non-Docker) deployments or debugging. |

**Important:** When using Docker, connect to **port 80** (nginx), not port 8080. The nginx proxy handles routing, authentication, and serves the web control panel.

```
┌─────────────────┐         ┌─────────────────────────────────────┐
│   MCP Server    │         │           Docker Container          │
│                 │  HTTP   │  ┌─────────┐      ┌──────────────┐  │
│ OWL_API_ENDPOINT├────────►│  │  Nginx  │─────►│ HTTP Server  │  │
│ = localhost:80  │         │  │ (port 80)│      │ (port 8080)  │  │
└─────────────────┘         │  └─────────┘      └──────────────┘  │
                            └─────────────────────────────────────┘
```

## Configuration

Set environment variables before running:

| Variable | Description | Default |
|----------|-------------|---------|
| `OWL_API_ENDPOINT` | HTTP API endpoint URL | `http://127.0.0.1:80` |
| `OWL_API_TOKEN` | Bearer token for authentication (same as `OWL_HTTP_TOKEN` in Docker) | (none) |

## Usage with Docker (Recommended)

When using the Owl Browser Docker image, connect to **port 80** (nginx proxy):

```json
{
  "mcpServers": {
    "owl-browser": {
      "command": "npx",
      "args": ["-y", "@olib-ai/owl-browser-mcp"],
      "env": {
        "OWL_API_ENDPOINT": "http://localhost:80",
        "OWL_API_TOKEN": "your-api-token"
      }
    }
  }
}
```

Or if Docker is running on a remote host:

```json
{
  "mcpServers": {
    "owl-browser": {
      "command": "npx",
      "args": ["-y", "@olib-ai/owl-browser-mcp"],
      "env": {
        "OWL_API_ENDPOINT": "http://your-docker-host:80",
        "OWL_API_TOKEN": "your-api-token"
      }
    }
  }
}
```

**Note:** The `OWL_API_TOKEN` must match the `OWL_HTTP_TOKEN` environment variable set when starting the Docker container.

## Usage with Standalone HTTP Server

If running the HTTP server directly (without Docker/nginx), connect to **port 8080**:

Add to your Claude Desktop config (`~/Library/Application Support/Claude/claude_desktop_config.json`):

```json
{
  "mcpServers": {
    "owl-browser": {
      "command": "npx",
      "args": ["-y", "@olib-ai/owl-browser-mcp"],
      "env": {
        "OWL_API_ENDPOINT": "http://127.0.0.1:8080",
        "OWL_API_TOKEN": "your-api-token"
      }
    }
  }
}
```

## Local Development

If running from source instead of npm:

```json
{
  "mcpServers": {
    "owl-browser": {
      "command": "node",
      "args": ["/path/to/mcp-server/dist/index.js"],
      "env": {
        "OWL_API_ENDPOINT": "http://127.0.0.1:80",
        "OWL_API_TOKEN": "your-api-token"
      }
    }
  }
}
```

## Available Tools

The server exposes 157 tools organized by category:

### Context Management
- `browser_create_context` - Create isolated browser session
- `browser_close_context` - Close browser session
- `browser_list_contexts` - List active sessions

### Navigation
- `browser_navigate` - Navigate to URL
- `browser_reload` - Reload page
- `browser_go_back` / `browser_go_forward` - History navigation

### Interaction
- `browser_click` - Click elements (CSS, coordinates, or natural language)
- `browser_type` - Type text into inputs
- `browser_press_key` - Press keyboard keys
- `browser_drag_drop` - Mouse drag operations

### Content Extraction
- `browser_screenshot` - Capture screenshots
- `browser_extract_text` - Extract page text
- `browser_get_html` - Get page HTML
- `browser_get_markdown` - Get page as Markdown

### CAPTCHA Solving
- `browser_detect_captcha` - Detect CAPTCHA presence
- `browser_solve_captcha` - Auto-solve CAPTCHAs
- `browser_solve_image_captcha` - Solve image-based CAPTCHAs

### And 120+ more...

## Protocol Version

This server implements MCP protocol version 2025-11-25.

## Testing with MCP Inspector

Use the official MCP Inspector to test the server interactively.

**Run from the `mcp-server` directory:**

```bash
cd mcp-server

# With Docker container (connect to nginx on port 80)
npx @modelcontextprotocol/inspector \
  -e OWL_API_ENDPOINT=http://localhost:80 \
  -e OWL_API_TOKEN=your-docker-token \
  node dist/index.js

# With standalone HTTP server (connect directly to port 8080)
npx @modelcontextprotocol/inspector \
  -e OWL_API_ENDPOINT=http://127.0.0.1:8080 \
  -e OWL_API_TOKEN=your-token \
  node dist/index.js

# With remote Docker server
npx @modelcontextprotocol/inspector \
  -e OWL_API_ENDPOINT=http://your-server.com:80 \
  -e OWL_API_TOKEN=your-api-token \
  node dist/index.js
```

The Inspector will open a web UI (default: http://localhost:6274) where you can:
- Browse all 157 browser tools
- Execute tools with custom parameters
- View JSON responses in real-time

## Development

```bash
# Install dependencies
npm install

# Build ESM version
npm run build

# Build CommonJS version
npm run build:cjs

# Run in development mode
npm run dev
```

## Output Files

- `dist/index.js` - ESM bundle (~887KB, single file with embedded schema)
- `dist/index.cjs` - CommonJS bundle (~887KB, single file with embedded schema)

Both bundles are self-contained and include the OpenAPI schema embedded at build time.
