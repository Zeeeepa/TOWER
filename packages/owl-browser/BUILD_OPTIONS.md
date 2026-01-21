# Build Options and LLM Configuration

This document explains the new build options and LLM configuration features added to Owl Browser.

## Table of Contents

- [Build Options](#build-options)
- [LLM Configuration](#llm-configuration)
  - [Headless Browser](#headless-browser-llm-config)
  - [UI Browser](#ui-browser-llm-config)
- [Use Cases](#use-cases)
- [Examples](#examples)

---

## Build Options

### `BUILD_WITH_LLAMA` - Skip llama.cpp and Models

You can now build the browser without bundling llama.cpp and the AI model, saving ~2GB of build output.

#### Default Behavior (WITH llama.cpp)

```bash
# Standard build includes llama-server and models (~2GB)
npm run build          # Headless browser
npm run build:ui       # UI browser
```

#### Build WITHOUT llama.cpp

```bash
# Clean build directory
rm -rf build

# Configure with BUILD_WITH_LLAMA=OFF
mkdir build && cd build
cmake -DBUILD_WITH_LLAMA=OFF ..
make -j$(sysctl -n hw.ncpu || nproc || echo 4)

# Or for UI version
cmake -DBUILD_WITH_LLAMA=OFF ..
make owl_browser_ui -j$(sysctl -n hw.ncpu || nproc || echo 4)
```

**Benefits:**
- Smaller binary size (~2GB saved)
- Faster build time
- Use external APIs (OpenAI, etc.) for better accuracy
- Deploy without bundling large AI models

**Note:** When built with `BUILD_WITH_LLAMA=OFF`, you must configure an external LLM API to use AI features.

---

## LLM Configuration

Owl Browser now supports **two LLM modes**:

1. **Built-in Mode** (default): Uses local llama-server with Qwen3-VL-2B model
2. **External Mode**: Uses third-party OpenAI-compatible APIs (OpenAI, Azure, etc.)

You can configure LLM settings at runtime:
- **Headless Browser**: Pass config when creating context via `browser_create_context`
- **UI Browser**: Configure via Settings page (`test://llm_settings.html`)

---

## Headless Browser LLM Config

### MCP Server (`browser_create_context` Tool)

The `browser_create_context` MCP tool now accepts optional LLM configuration parameters:

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `llm_enabled` | boolean | Enable/disable LLM features (default: true) |
| `llm_use_builtin` | boolean | Use built-in llama-server if available (default: true) |
| `llm_endpoint` | string | External API endpoint (e.g., "https://api.openai.com") |
| `llm_model` | string | External model name (e.g., "gpt-4-vision-preview") |
| `llm_api_key` | string | API key for external provider |

#### Example 1: Use Built-in Model (Default)

```javascript
// No LLM config needed - uses built-in model automatically
const context_id = await browser.createContext();
```

#### Example 2: Use OpenAI GPT-4 Vision

```javascript
const context_id = await browser.createContext({
  llm_endpoint: "https://api.openai.com",
  llm_model: "gpt-4-vision-preview",
  llm_api_key: "sk-your-api-key-here"
});
```

#### Example 3: Use Azure OpenAI

```javascript
const context_id = await browser.createContext({
  llm_endpoint: "https://your-resource.openai.azure.com",
  llm_model: "gpt-4-vision",
  llm_api_key: "your-azure-api-key"
});
```

#### Example 4: Disable LLM Features

```javascript
const context_id = await browser.createContext({
  llm_enabled: false
});
```

### SDK Usage

The TypeScript/JavaScript SDK also supports LLM configuration:

```typescript
import { Browser } from 'owl-browser-sdk';

const browser = new Browser({
  llm: {
    enabled: true,
    use_builtin: false,
    external_endpoint: "https://api.openai.com",
    external_model: "gpt-4-vision-preview",
    external_api_key: process.env.OPENAI_API_KEY
  }
});

await browser.launch();
const page = await browser.newPage();
```

---

## UI Browser LLM Config

### Settings on Homepage

The UI browser includes LLM settings integrated into the homepage:

**Access Settings:**
1. Launch UI browser: `npm run start:ui`
2. On the homepage, click the **"LLM Settings"** action card (gear icon)
3. You'll see the Settings view with:
   - **Active Provider** display showing which LLM is currently in use
   - **Add Provider** button to configure new providers
   - **Providers List** showing all configured providers with Edit/Delete/Activate actions

**Managing Providers:**

1. **Add New Provider**
   - Click "Add LLM Provider" button
   - Fill in provider details:
     - **Provider Name**: Custom name (e.g., "OpenAI GPT-4", "Azure OpenAI")
     - **API Endpoint**: Full URL (e.g., `https://api.openai.com`)
     - **Model Name**: Model identifier (e.g., `gpt-4-vision-preview`)
     - **API Key**: Your API key (optional for local servers)
     - **Set as active**: Check to make this provider active immediately
   - Click "Save Provider"

2. **Edit Existing Provider**
   - In the providers list, click "Edit" on any provider
   - Update the details
   - Click "Save Provider"

3. **Activate Provider**
   - Click "Activate" on any inactive provider
   - Only one provider can be active at a time

4. **Delete Provider**
   - Click "Delete" on any provider
   - Confirm deletion

**Default Provider:**
- Built-in Model (Qwen3-VL-2B) is the default
- Local on-device model - Fast & Private
- Automatically used if no external provider is active

**Settings Storage:**
- All providers are stored in `localStorage`
- Persist across browser restarts
- Active provider is used for all new browser contexts

---

## Use Cases

### Use Case 1: Development with Built-in Model

**Scenario:** Fast iteration during development

```bash
# Build with llama.cpp (default)
npm run build

# Use built-in model
const context_id = await browser.createContext();
```

**Pros:** Fast, private, no API costs
**Cons:** Lower accuracy than GPT-4

### Use Case 2: Production with External API

**Scenario:** Deploy lightweight browser, use GPT-4 for accuracy

```bash
# Build WITHOUT llama.cpp (saves 2GB)
rm -rf build && mkdir build && cd build
cmake -DBUILD_WITH_LLAMA=OFF ..
make -j$(nproc)
```

```javascript
// Use OpenAI GPT-4 for better accuracy
const context_id = await browser.createContext({
  llm_endpoint: "https://api.openai.com",
  llm_model: "gpt-4-vision-preview",
  llm_api_key: process.env.OPENAI_API_KEY
});
```

**Pros:** Smaller binary, better accuracy
**Cons:** API costs, requires internet

### Use Case 3: Hybrid Mode

**Scenario:** Use built-in for simple tasks, GPT-4 for complex ones

```bash
# Build with llama.cpp (default)
npm run build
```

```javascript
// Simple task - use built-in
const ctx1 = await browser.createContext();

// Complex task - use GPT-4
const ctx2 = await browser.createContext({
  llm_endpoint: "https://api.openai.com",
  llm_model: "gpt-4-vision-preview",
  llm_api_key: process.env.OPENAI_API_KEY
});
```

**Pros:** Best of both worlds
**Cons:** Larger binary size

---

## Examples

### Example 1: Headless Browser with OpenAI

```javascript
// MCP Server usage
const { Client } = require('@modelcontextprotocol/sdk/client/index.js');

// Create context with OpenAI
const result = await client.callTool({
  name: 'browser_create_context',
  arguments: {
    llm_endpoint: 'https://api.openai.com',
    llm_model: 'gpt-4-vision-preview',
    llm_api_key: 'sk-your-api-key'
  }
});

const context_id = result.content[0].text.match(/ctx_\d+/)[0];

// Now use AI features with GPT-4
await client.callTool({
  name: 'browser_nla',
  arguments: {
    context_id: context_id,
    command: 'search for the latest AI news'
  }
});
```

### Example 2: UI Browser with Custom Endpoint

1. Build browser with llama.cpp disabled:
   ```bash
   rm -rf build && mkdir build && cd build
   cmake -DBUILD_WITH_LLAMA=OFF ..
   make owl_browser_ui -j$(nproc)
   ```

2. Launch UI browser:
   ```bash
   npm run start:ui
   ```

3. On the homepage, click **"LLM Settings"** card (gear icon)

4. Click **"Add LLM Provider"** and configure:
   - Provider Name: `OpenAI GPT-4`
   - Endpoint: `https://api.openai.com`
   - Model: `gpt-4-vision-preview`
   - API Key: `sk-your-key`
   - Check "Set as active provider"

5. Click "Save Provider"

6. Test AI features:
   - Navigate to any website
   - Use natural language actions in the AI command box
   - AI features now powered by GPT-4!

### Example 3: Environment Variables

```bash
# Set environment variables
export OPENAI_API_KEY="sk-your-api-key"
export LLM_ENDPOINT="https://api.openai.com"
export LLM_MODEL="gpt-4-vision-preview"

# Use in your code
const context_id = await browser.createContext({
  llm_endpoint: process.env.LLM_ENDPOINT,
  llm_model: process.env.LLM_MODEL,
  llm_api_key: process.env.OPENAI_API_KEY
});
```

---

## OpenAI-Compatible APIs

The external LLM configuration works with any OpenAI-compatible API:

- **OpenAI**: `https://api.openai.com`
- **Azure OpenAI**: `https://your-resource.openai.azure.com`
- **Anthropic Claude** (via proxy): Various community proxies
- **Local LLaMA** (via llama-server): `http://localhost:8095`
- **Ollama**: `http://localhost:11434`
- **LM Studio**: `http://localhost:1234`

All use the same `/v1/chat/completions` endpoint format.

---

## Troubleshooting

### Build Issues

**Q: Build fails with "llama-server not found"**
A: This is expected when using `BUILD_WITH_LLAMA=OFF`. The browser will work fine with external APIs.

**Q: How do I check if llama.cpp was included?**
A: Check for `llama-server` binary in your build output:
```bash
# macOS
ls build/Release/owl_browser.app/Contents/MacOS/llama-server

# Linux
ls build/llama-server
```

### Runtime Issues

**Q: LLM features don't work**
A: Check:
1. Is `llm_enabled` set to true? (default)
2. If using external API, are endpoint/model/key correct?
3. Check browser logs for errors

**Q: External API returns errors**
A: Verify:
1. API key is valid
2. Endpoint URL is correct
3. Model name matches your API provider
4. You have sufficient API credits

---

## Summary

**Build Options:**
- `BUILD_WITH_LLAMA=ON` (default): Include llama.cpp and models (~2GB)
- `BUILD_WITH_LLAMA=OFF`: Lightweight build, use external APIs

**LLM Modes:**
- **Built-in**: Local Qwen3-VL-2B (fast, private)
- **External**: OpenAI-compatible APIs (better accuracy)

**Configuration:**
- **Headless**: Pass config in `browser_create_context`
- **UI**: Use settings page at `test://llm_settings.html`

**Use Cases:**
- Development: Use built-in model
- Production: Use external API for accuracy
- Hybrid: Mix and match per context

For more details, see the main [README.md](README.md).
