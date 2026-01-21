# Owl Browser Build Guide

## Two Build Versions

Owl Browser now supports **two distinct build versions**:

### 1. Developer Version (Headless)
**Target**: `owl_browser`
- Headless browser for automation and MCP integration
- No UI, no Dock icon (background process)
- Communicates via JSON IPC (stdin/stdout)
- Perfect for AI agents, automation scripts, and MCP servers

### 2. User-Facing Version (with UI)
**Target**: `owl_browser_ui`
- Full browser UI with address bar and navigation controls
- Visible in Dock and app switcher
- Beautiful LLM-powered assistant sidebar
- Agent mode for natural language automation
- Perfect for end users who want an AI-enhanced browser

---

## Platform Support

### macOS
- ✅ Developer Version (Headless) - Full support
- ✅ UI Version - Full support with Cocoa/AppKit

### Ubuntu/Linux
- ✅ Developer Version (Headless) - Full support
- ✅ UI Version - Full support with GTK3 **(NEW!)**

### Windows
- ⚠️ Coming soon

---

## Build Commands

### Build Both Versions
```bash
npm run build
```
This builds both the developer and UI versions, plus MCP server and SDK.

### Build Developer Version Only
```bash
npm run build:dev
```
Builds headless browser + MCP server + SDK

### Build UI Version Only
```bash
npm run build:ui
```
Builds the user-facing browser with UI

### Quick Rebuild (After Code Changes)
```bash
# Rebuild everything
npm run rebuild

# Rebuild developer version only
npm run rebuild:dev

# Rebuild UI version only
npm run rebuild:ui
```

---

## Running

### Developer Version
```bash
# Via MCP server
npm start

# Direct execution (for testing)
./build/Release/owl_browser.app/Contents/MacOS/owl_browser
```

### UI Version
```bash
# Launch the app
npm run start:ui

# Or double-click in Finder
open build/Release/owl_browser_ui.app
```

---

## Features Comparison

| Feature | Developer Version | UI Version |
|---------|------------------|------------|
| **Dock Icon** | ❌ Hidden | ✅ Visible |
| **Browser Chrome** | ❌ No UI | ✅ Full UI |
| **Address Bar** | ❌ | ✅ |
| **Agent Mode UI** | ❌ | ✅ |
| **LLM Sidebar** | ❌ | ✅ |
| **JSON IPC** | ✅ | ❌ |
| **MCP Integration** | ✅ | ❌ |
| **Built-in LLM** | ✅ | ✅ |
| **NLA (Natural Language)** | ✅ | ✅ |
| **Stealth Features** | ✅ | ✅ |
| **Ad Blocker** | ✅ | ✅ |

---

## UI Version Features

### 1. Beautiful Browser Chrome
- Modern, clean address bar with smart search
- Back/Forward/Reload navigation buttons
- Keyboard shortcuts:
  - `Cmd+L` / `Ctrl+L`: Focus address bar
  - `Cmd+R` / `Ctrl+R`: Reload
  - `Cmd+[` / `Alt+←`: Back
  - `Cmd+]` / `Alt+→`: Forward
  - `Cmd+Shift+A`: Toggle agent mode
  - `Cmd+Shift+S`: Toggle sidebar

### 2. Agent Mode
Toggle agent mode to enable AI-powered automation:
- Click the **🤖 Agent Mode** button
- Enter natural language prompts
- Watch as the browser executes complex tasks automatically
- Examples:
  - "Search for restaurants near me"
  - "Find the latest AI news and summarize"
  - "Navigate to GitHub and find Python projects"

### 3. LLM Integration
The UI version has full access to the built-in Qwen3-VL-2B vision model:
- On-device processing (privacy-first)
- Metal GPU acceleration (fast)
- No cloud API required
- Context-aware actions using your location and time

### 4. Smart Features
- **Intelligent Search**: Type partial URLs or search queries in address bar
- **Context Menus**: Right-click for quick actions
- **DevTools**: Access via context menu → "Inspect Element"
- **Real-time Status**: Agent execution progress shown in UI

---

## Architecture

### Developer Version
```
owl_browser (subprocess)
├── Headless CEF (off-screen rendering)
├── JSON IPC (stdin/stdout)
├── Built-in LLM (llama.cpp + Qwen3)
├── NLA engine
└── MCP server wrapper
```

### UI Version
```
owl_browser_ui (native app)
├── Windowed CEF (native rendering)
├── UI overlay (address bar, controls)
├── Agent controller
├── Built-in LLM (llama.cpp + Qwen3)
└── Native window (Cocoa/macOS)
```

---

## Development Workflow

### 1. Make Changes to Shared Code
Most browser features are shared between versions:
- `src/owl_browser_manager.cc` - Browser management
- `src/owl_nla.cc` - Natural language actions
- `src/owl_llm_client.cc` - LLM integration
- `src/owl_stealth.cc` - Anti-detection

After changes:
```bash
npm run rebuild
```

### 2. Make Changes to UI Code
UI-specific files:
- `src/owl_ui_browser.cc` - UI browser window
- `src/owl_ui_delegate.mm` - Native window (macOS)
- `src/owl_agent_controller.cc` - Agent mode

After changes:
```bash
npm run rebuild:ui
```

### 3. Make Changes to Developer Version
Developer-specific files:
- `src/owl_subprocess.cc` - IPC subprocess
- `src/owl_main.cc` - Entry point

After changes:
```bash
npm run rebuild:dev
```

---

## Troubleshooting

### Build Errors

**Problem**: `owl_ui_browser.h` not found
**Solution**: Make sure you ran `cmake ..` first to generate build files
```bash
rm -rf build
npm run build
```

**Problem**: Cocoa framework not linked
**Solution**: The UI version requires macOS. On Linux/Windows, only build the developer version:
```bash
npm run build:dev
```

### Runtime Errors

**Problem**: UI browser crashes immediately on launch (SIGSEGV)
**Solution**: This was caused by missing CEF framework loading. Fixed in `src/owl_ui_main.cc` by adding `CefScopedLibraryLoader` before any CEF API calls. On macOS, the CEF framework MUST be dynamically loaded at runtime using `CefScopedLibraryLoader::LoadInMain()` before calling any CEF functions. Without this, all CEF function pointers are null, causing crashes.

**Problem**: UI version doesn't show in Dock
**Solution**: Check Info.plist was copied correctly:
```bash
cat build/Release/owl_browser_ui.app/Contents/Info.plist | grep LSUIElement
# Should show: <false/>
```

**Problem**: Agent mode not working
**Solution**: Check LLM server is running:
```bash
ps aux | grep llama-server
# Should show llama-server process
```

**Problem**: No address bar visible
**Solution**: Check browser console (DevTools) for JavaScript errors in the injected UI overlay

**Problem**: "Failed to load CEF framework" error
**Solution**: Verify the CEF framework is in the correct location:
```bash
ls -la build/Release/owl_browser_ui.app/Contents/Frameworks/Chromium\ Embedded\ Framework.framework/
# Framework should exist at this path
```

---

## Building on Ubuntu/Linux

### Prerequisites

Install required dependencies:
```bash
# Update package list
sudo apt-get update

# Install build tools
sudo apt-get install -y \
  build-essential \
  cmake \
  git \
  curl \
  pkg-config

# Install GTK3 and dependencies for UI version
sudo apt-get install -y \
  libgtk-3-dev \
  libcairo2-dev \
  libx11-dev \
  libxi-dev \
  libxrandr-dev

# Install other dependencies
sudo apt-get install -y \
  libmaxminddb-dev \
  libcurl4-openssl-dev \
  zlib1g-dev
```

### Build Steps

1. **Clone and setup:**
```bash
cd owl-browser
npm install
```

2. **Download CEF and llama.cpp binaries:**
```bash
npm run download:cef
npm run download:llama
```

3. **Build developer version (headless):**
```bash
npm run build:dev
```

4. **Build UI version (GTK3):**
```bash
npm run build:ui
```

5. **Build both versions:**
```bash
npm run build
```

### Running on Ubuntu

#### Developer Version (Headless)
```bash
# Via MCP server
npm start

# Direct execution
./build/owl_browser
```

#### UI Version (GTK3)
```bash
# Launch the UI
./build/owl_browser_ui

# Or via npm
npm run start:ui
```

### UI Version Features on Ubuntu

The GTK3 UI provides the same features as macOS:
- **Toolbar**: Back, Forward, Reload, Stop, Home, Address Bar, Go
- **Agent Mode**: AI button to toggle agent mode with prompt overlay
- **Developer Console**: F12 or Ctrl+Shift+I to open
- **Developer Playground**: Standalone window for testing
- **Tasks Panel**: View and manage AI tasks
- **Response Area**: Display AI responses

### Keyboard Shortcuts (Ubuntu)

- `Ctrl+L`: Focus address bar
- `Ctrl+R`: Reload page
- `Alt+Left`: Back
- `Alt+Right`: Forward
- `Ctrl+Shift+A`: Toggle agent mode
- `Ctrl+Shift+I`: Open developer console
- `F12`: Open developer console
- `Ctrl+Q`: Quit

### Architecture (Linux UI)

```
owl_browser_ui (GTK3 app)
├── GtkWindow (main window)
├── GtkOverlay (overlay container)
│   ├── GtkBox (main layout)
│   │   ├── Toolbar (GtkBox with buttons/entry)
│   │   └── Content (GtkFixed for CEF browser)
│   ├── Agent Prompt Overlay (GtkBox)
│   ├── Tasks Panel (GtkScrolledWindow)
│   └── Response Area (GtkTextView)
├── CEF Browser (embedded)
├── Built-in LLM (llama.cpp + Qwen3)
└── GTK event loop + CefDoMessageLoopWork()
```

### Troubleshooting (Ubuntu)

**Problem**: GTK3 not found
**Solution**:
```bash
sudo apt-get install libgtk-3-dev
```

**Problem**: Cairo not found
**Solution**:
```bash
sudo apt-get install libcairo2-dev
```

**Problem**: X11 not found
**Solution**:
```bash
sudo apt-get install libx11-dev
```

**Problem**: `libcef.so` not found when running
**Solution**: Make sure you're in the build directory or the library is in your path:
```bash
cd build
./owl_browser_ui
```

**Problem**: GTK warnings about themes
**Solution**: Install a GTK3 theme:
```bash
sudo apt-get install gnome-themes-standard
```

**Problem**: Window doesn't appear
**Solution**: Check if X11 display is set:
```bash
echo $DISPLAY
# Should show :0 or :1

# If not set:
export DISPLAY=:0
```

**Problem**: Agent mode not working
**Solution**: Ensure llama-server binary was downloaded:
```bash
ls build/llama-server
npm run download:llama
```

---

## File Structure

```
owl-browser/
├── src/
│   # Platform-independent core
│   ├── owl_browser_manager.cc   # Browser management
│   ├── owl_nla.cc               # Natural language actions
│   ├── owl_llm_client.cc        # LLM integration
│   ├── owl_stealth.cc           # Anti-detection
│   ├── owl_ui_browser.cc        # UI browser window (shared)
│   ├── owl_agent_controller.cc  # Agent mode controller (shared)
│   │
│   # macOS-specific
│   ├── owl_ui_main.mm           # macOS UI entry point
│   ├── owl_ui_delegate.mm       # macOS window manager (Cocoa)
│   ├── owl_ui_toolbar.mm        # macOS toolbar (AppKit)
│   ├── owl_playground_window.mm # macOS playground window
│   ├── owl_dev_console.mm       # macOS dev console
│   ├── owl_native_screenshot.mm # macOS screenshot (CoreGraphics)
│   │
│   # Linux-specific (NEW!)
│   ├── owl_ui_main_linux.cc          # Linux UI entry point
│   ├── owl_ui_delegate_linux.cc      # Linux window manager (GTK3)
│   ├── owl_ui_toolbar_linux.cc       # Linux toolbar (GTK3)
│   ├── owl_playground_window_linux.cc# Linux playground window
│   ├── owl_dev_console_linux.cc      # Linux dev console
│   ├── owl_native_screenshot_linux.cc# Linux screenshot (X11/Cairo)
│   ├── owl_dev_elements_linux.cc     # Dev console elements tab
│   └── owl_dev_network_linux.cc      # Dev console network tab
│
├── include/
│   ├── owl_ui_browser.h         # Cross-platform
│   ├── owl_ui_delegate.h        # Cross-platform (platform guards)
│   ├── owl_ui_toolbar.h         # Cross-platform (platform guards)
│   ├── owl_playground_window.h  # Cross-platform
│   ├── owl_dev_console.h        # Cross-platform (platform guards)
│   └── ... (shared headers)
│
├── resources/
│   ├── Info.plist.template       # macOS: Developer version (hidden)
│   ├── Info-UI.plist.template    # macOS: UI version (visible)
│   └── icon.icns                 # macOS app icon
│
├── build/                        # Build output
│   # macOS
│   └── Release/
│       ├── owl_browser.app      # macOS: Developer version
│       └── owl_browser_ui.app   # macOS: UI version
│   # Linux
│   ├── owl_browser              # Linux: Developer version
│   └── owl_browser_ui           # Linux: UI version
│
└── CMakeLists.txt                # Cross-platform build configuration
```

---

## Tips

1. **Use the right version for your task:**
   - Automation/AI agents → Developer version
   - End users → UI version

2. **Test both versions:**
   - Developer version: Use test scripts (`npm test`)
   - UI version: Launch and test manually (`npm run start:ui`)

3. **Debugging:**
   - Developer version: Check logs in terminal
   - UI version: Use DevTools (Cmd+Shift+I or context menu)

4. **Performance:**
   - Both versions use the same LLM and NLA engine
   - UI version has slightly higher memory usage due to rendering

---

## Next Steps

- Read [README.md](README.md) for general browser features
- Read [SDK-QUICKSTART.md](SDK-QUICKSTART.md) for SDK usage
- Try the UI version: `npm run start:ui`
- Build your own automation: Check `tests/` for examples

**Enjoy building with Owl Browser!** 🚀
