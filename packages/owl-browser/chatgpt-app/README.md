# Owl Browser ChatGPT App

**Control your local Owl Browser directly from ChatGPT**

This is a ChatGPT App that allows you to use Owl Browser's 100+ automation tools directly within ChatGPT conversations. Unlike cloud browsers, this connects to your **locally installed** Owl Browser, giving you full control and privacy.

## Features

- **100+ Browser Automation Tools** - All Owl Browser capabilities exposed as ChatGPT tools
- **Natural Language Control** - Click, type, scroll using plain English
- **AI-Powered Automation** - NLA (Natural Language Actions) for multi-step tasks
- **Screenshot & Recording** - Visual feedback with session recording
- **Profile Management** - Persistent browser identities
- **CAPTCHA Solving** - Built-in AI-powered CAPTCHA handling
- **Dark/Light Theme** - Matches ChatGPT's theme automatically
- **Fullscreen Mode** - Immersive experience for complex workflows

## Prerequisites

1. **Owl Browser** must be installed and built on your machine
2. **Node.js** 18.0.0 or higher
3. **ngrok** (for exposing local server to ChatGPT)

## Quick Start

### 1. Install Dependencies

```bash
cd chatgpt-app
npm install
```

### 2. Build the UI Component

```bash
npm run build:web
```

### 3. Start the MCP Server

```bash
npm run start
```

### 4. Expose with ngrok

```bash
npm run tunnel
# or manually: ngrok http 3847
```

### 5. Add to ChatGPT

1. Go to ChatGPT Settings > Apps
2. Click "Add App"
3. Enter your ngrok URL with `/mcp` endpoint (e.g., `https://abc123.ngrok.io/mcp`)
4. Save and start using!

## Development

### Run in Development Mode

```bash
npm run dev
```

This starts both the server and watches for web component changes.

### Project Structure

```
chatgpt-app/
├── server/                    # MCP Server
│   └── index.js              # Express server with MCP endpoints
├── web/                       # React UI Component
│   ├── src/
│   │   ├── App.tsx           # Main React component
│   │   ├── components/       # UI components
│   │   │   └── Icons.tsx     # SVG icon components
│   │   ├── hooks/            # React hooks
│   │   │   └── useOpenAI.ts  # ChatGPT API hooks
│   │   ├── utils/            # Utilities
│   │   │   └── theme.ts      # Theme system
│   │   └── styles/           # CSS styles
│   ├── dist/                  # Built output
│   │   ├── component.js      # Bundled component
│   │   └── index.html        # Preview page
│   └── tsconfig.json         # TypeScript config
├── package.json
└── README.md
```

## Available Tools

The ChatGPT App exposes these Owl Browser tools:

### Context Management
- `browser_create_context` - Create new browser session
- `browser_close_context` - Close session
- `browser_list_contexts` - List active sessions

### Navigation
- `browser_navigate` - Go to URL
- `browser_reload` - Refresh page
- `browser_go_back` / `browser_go_forward` - History navigation

### Interaction
- `browser_click` - Click element (CSS, position, or natural language)
- `browser_type` - Type text into input
- `browser_pick` - Select dropdown option
- `browser_press_key` - Press special keys
- `browser_hover` - Hover over element
- `browser_scroll_by` / `browser_scroll_to_element` - Scroll page

### Content Extraction
- `browser_screenshot` - Capture page (viewport/element/fullpage)
- `browser_extract_text` - Extract text content
- `browser_get_markdown` - Get page as Markdown
- `browser_get_page_info` - Get URL, title, etc.

### AI Features
- `browser_nla` - Execute natural language commands
- `browser_summarize_page` - AI-powered page summary
- `browser_query_page` - Ask questions about page

### Session Recording
- `browser_start_video_recording` - Start recording
- `browser_stop_video_recording` - Stop and save video

### Storage & Cookies
- `browser_get_cookies` - Get cookies
- `browser_set_cookie` - Set cookie
- `browser_create_profile` / `browser_save_profile` - Profile management

### CAPTCHA
- `browser_solve_captcha` - Auto-detect and solve CAPTCHAs

### Wait & Sync
- `browser_wait` - Wait milliseconds
- `browser_wait_for_selector` - Wait for element
- `browser_wait_for_network_idle` - Wait for network

## UI Components

The app includes a rich UI component library:

### Display Modes
- **Inline** - Default, appears in conversation
- **Fullscreen** - Immersive mode for complex workflows
- **PiP** - Picture-in-Picture for parallel activities

### Views
- **Dashboard** - Quick actions and session overview
- **Context View** - Active browser session controls
- **Screenshot Viewer** - Full-size screenshot display
- **Recording Panel** - Video recording controls

## Theme System

Brand color: **#6BA894** (Owl Teal)

The app automatically matches ChatGPT's theme (light/dark) and uses a consistent design system with:
- Primary: `#6BA894`
- Success: `#48BB78`
- Warning: `#ECC94B`
- Error: `#F56565`
- Info: `#4299E1`

## API Endpoints

### MCP Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/mcp` | GET | Server info |
| `/mcp/tools` | GET | List available tools |
| `/mcp/tools/:name` | POST | Execute a tool |
| `/mcp/resources` | GET | List UI resources |
| `/mcp/state` | GET/POST | Widget state |

### OAuth Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/.well-known/oauth-protected-resource` | GET | OAuth metadata |
| `/.well-known/oauth-authorization-server` | GET | Auth server metadata |
| `/oauth/register` | POST | Dynamic client registration |
| `/oauth/authorize` | GET | Authorization endpoint |
| `/oauth/token` | POST | Token endpoint |

### Health

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/health` | GET | Server health check |

## Configuration

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `PORT` | 3847 | Server port |
| `OWL_BROWSER_PATH` | auto-detect | Path to browser binary |

### Browser Detection

The server automatically looks for the browser in:
1. `../../build/Release/owl_browser.app/Contents/MacOS/owl_browser` (macOS)
2. `../../build/owl_browser` (Linux)
3. `OWL_BROWSER_PATH` environment variable

## Hooks Reference

### useOpenAI()
Access ChatGPT globals reactively.

```tsx
const { openai, theme, displayMode, toolInput, toolOutput } = useOpenAI();
```

### useWidgetState(initialState)
Persistent state across message interactions.

```tsx
const [state, setState] = useWidgetState({ count: 0 });
```

### useTool()
Call MCP tools with loading/error handling.

```tsx
const { callTool, loading, error } = useTool();
await callTool('browser_navigate', { context_id, url });
```

### useDisplayMode()
Control display mode.

```tsx
const { displayMode, requestFullscreen, toggle } = useDisplayMode();
```

## Troubleshooting

### Browser Not Found
Ensure Owl Browser is built:
```bash
cd ..
npm run build
```

### Connection Issues
Check that the MCP server is running and accessible via ngrok.

### OAuth Errors
The app uses a simplified OAuth flow for local browser connection. Ensure your ngrok URL is correctly configured in ChatGPT.

## Security

- This app connects to your **local** Owl Browser only
- No data is sent to cloud browsers
- OAuth tokens are for local authentication only
- All browser actions happen on your machine

## License

MIT

## Links

- **Owl Browser**: [www.owlbrowser.net](https://www.owlbrowser.net)
- **Olib AI**: [www.olib.ai](https://www.olib.ai)
- **Documentation**: [GitHub](https://github.com/Olib-AI/owl-browser)
