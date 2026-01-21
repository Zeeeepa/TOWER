# Owl Browser SDK

AI-first browser automation SDK for Node.js/TypeScript. Built on top of the Owl Browser - a custom Chromium-based browser with built-in AI intelligence.

## Features

- 🧠 **Natural Language Selectors**: Use "search button" instead of complex CSS selectors
- 🤖 **On-Device LLM**: Built-in Qwen3-1.7B for intelligent page understanding
- 🌍 **Context Awareness**: Auto-detect location, time, and weather for smart automation
- ⚡ **Lightning Fast**: Native C++ browser with <1s cold start
- 🛡️ **Maximum Stealth**: No WebDriver detection, built-in ad blocking
- 📸 **Full HD Screenshots**: 1920x1080 high-quality captures
- 🎥 **Video Recording**: Record browser sessions as video
- 🏠 **Custom Homepage**: Beautiful branded homepage with real-time demographics
- 🌐 **Dual Mode**: Connect to local browser binary OR remote HTTP server

## Installation

```bash
npm install @olib-ai/owl-browser-sdk
```

**Prerequisites:**
- Node.js 18+
- **Local mode**: The Owl Browser binary must be built (see main README)
- **HTTP mode**: A running Owl Browser HTTP server

## Connection Modes

The SDK supports two connection modes:

### Local Mode (Default)

Uses the local browser binary via IPC (stdin/stdout). Best for development and single-machine deployments.

```typescript
import { Browser } from '@olib-ai/owl-browser-sdk';

// Local mode (default)
const browser = new Browser();
await browser.launch();
```

### HTTP Mode

Connects to a remote browser server via REST API. Best for Docker deployments, microservices, and distributed systems.

```typescript
import { Browser } from '@olib-ai/owl-browser-sdk';

// HTTP mode - connect to remote server (simple token auth)
const browser = new Browser({
  mode: 'http',
  http: {
    baseUrl: 'http://localhost:8080',
    token: 'your-secret-token',
    timeout: 30000  // optional, default 30s
  }
});
await browser.launch();
```

#### WebSocket Transport

For lower latency and persistent connections, use WebSocket transport instead of HTTP:

```typescript
import { Browser } from '@olib-ai/owl-browser-sdk';

// WebSocket mode - real-time communication
const browser = new Browser({
  mode: 'http',
  http: {
    baseUrl: 'http://localhost:8080',
    token: 'your-secret-token',
    transport: 'websocket',  // Use WebSocket instead of HTTP
    reconnect: {
      enabled: true,
      maxAttempts: 5,
      initialDelayMs: 1000,
      maxDelayMs: 30000
    }
  }
});
await browser.launch();
```

#### High-Performance Configuration

For high-concurrency workloads, configure retry, connection pooling, and concurrency limits:

```typescript
import { Browser } from '@olib-ai/owl-browser-sdk';

// High-performance HTTP configuration
const browser = new Browser({
  mode: 'http',
  http: {
    baseUrl: 'http://localhost:8080',
    token: 'your-secret-token',
    timeout: 30000,
    // Retry configuration with exponential backoff
    retry: {
      maxRetries: 5,
      initialDelayMs: 100,
      maxDelayMs: 10000,
      backoffMultiplier: 2.0,
      jitterFactor: 0.1
    },
    // Concurrency limiting
    concurrency: {
      maxConcurrent: 50
    },
    // Connection pooling
    pool: {
      maxConnections: 20,
      idleTimeoutMs: 60000
    }
  }
});
await browser.launch();
```

#### JWT Authentication

For enhanced security, the server supports JWT (JSON Web Token) authentication with RSA signing. The SDK can automatically generate and refresh JWT tokens using your private key:

```typescript
import { Browser } from '@olib-ai/owl-browser-sdk';

// HTTP mode with JWT authentication (auto-generated tokens)
const browser = new Browser({
  mode: 'http',
  http: {
    baseUrl: 'http://localhost:8080',
    authMode: 'jwt',
    jwt: {
      privateKey: '/path/to/private.pem',  // RSA private key
      expiresIn: 3600,                      // Token validity (1 hour)
      refreshThreshold: 300,                // Refresh 5 min before expiry
      issuer: 'my-app',                     // Optional claims
      subject: 'user-123'
    }
  }
});
await browser.launch();
```

You can also use the JWT utilities directly:

```typescript
import { generateJWT, decodeJWT, JWTManager } from '@olib-ai/owl-browser-sdk';

// Generate a single token
const token = generateJWT('/path/to/private.pem', {
  expiresIn: 7200,
  issuer: 'my-app'
});

// Or use JWTManager for auto-refresh
const jwtManager = new JWTManager('/path/to/private.pem', {
  expiresIn: 3600,
  refreshThreshold: 300
});
const token = jwtManager.getToken();  // Auto-refreshes when needed
```

**Setting up the HTTP server:**

```bash
# Build the HTTP server
cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make owl_http_server

# Run with simple token authentication
OWL_HTTP_TOKEN=your-secret-token \
OWL_BROWSER_PATH=/path/to/owl_browser \
./build/Release/owl_http_server

# Run with JWT authentication
OWL_AUTH_MODE=jwt \
OWL_JWT_PUBLIC_KEY=/path/to/public.pem \
OWL_BROWSER_PATH=/path/to/owl_browser \
./build/Release/owl_http_server
```

See [HTTP Server Documentation](../http-server/README.md) for more details on JWT configuration and key generation.

## Quick Start

```typescript
import { Browser } from '@olib-ai/owl-browser-sdk';

// Create and launch browser
const browser = new Browser();
await browser.launch();

// Create a new page
const page = await browser.newPage();

// Navigate to a URL
await page.goto('https://example.com');

// Use natural language selectors!
await page.click('search button');
await page.type('email input', 'user@example.com');
await page.pressKey('Enter');

// Take a screenshot
const screenshot = await page.screenshot();

// Close browser
await browser.close();
```

## API Reference

### Browser

```typescript
const browser = new Browser(config?: BrowserConfig);
```

**Methods:**
- `launch()` - Start the browser process
- `newPage()` - Create a new browser context (page/tab)
- `pages()` - Get all active pages
- `getLLMStatus()` - Check on-device LLM status
- `listTemplates()` - List available extraction templates
- `getDemographics()` - Get complete demographics (location, time, weather)
- `getLocation()` - Get geographic location based on IP
- `getDateTime()` - Get current date and time
- `getWeather()` - Get current weather for user's location
- `getHomepage()` - Get custom browser homepage HTML
- `close()` - Shutdown browser and cleanup
- `isRunning()` - Check if browser is running

### BrowserContext (Page)

All page interactions happen through a BrowserContext instance.

#### Navigation

```typescript
await page.goto('https://example.com');
await page.reload();
await page.goBack();
await page.goForward();
```

#### Interactions

```typescript
// Natural language selectors work!
await page.click('search button');
await page.type('email input', 'user@example.com');
await page.pressKey('Enter');

// Or use CSS selectors
await page.click('#submit-btn');
await page.type('input[name="email"]', 'user@example.com');

// Or use position coordinates (x,y)
await page.click('500x300');  // Click at x=500, y=300
await page.type('640x400', 'text here');  // Type at position

// Highlight elements for debugging
await page.highlight('login button');
```

#### Advanced Interactions

```typescript
// Hover over elements (for tooltips, dropdowns)
await page.hover('menu item');
await page.hover('.dropdown-trigger', 500);  // hover for 500ms

// Double-click
await page.doubleClick('editable cell');

// Right-click (context menu)
await page.rightClick('file item');

// Clear input before typing
await page.clearInput('search box');
await page.type('search box', 'new search');

// Focus and blur (trigger validation)
await page.focus('email input');
await page.blur('email input');

// Select all text in input
await page.selectAll('text area');

// Keyboard combinations
await page.keyboardCombo('a', ['ctrl']);       // Ctrl+A (select all)
await page.keyboardCombo('c', ['ctrl']);       // Ctrl+C (copy)
await page.keyboardCombo('v', ['ctrl']);       // Ctrl+V (paste)
await page.keyboardCombo('z', ['ctrl']);       // Ctrl+Z (undo)
await page.keyboardCombo('s', ['ctrl', 'shift']); // Ctrl+Shift+S

// Human-like mouse movement (anti-bot detection)
await page.mouseMove(100, 100, 500, 300);  // Move from (100,100) to (500,300)
await page.mouseMove(100, 100, 500, 300, 50);  // With 50 intermediate steps
await page.mouseMove(100, 100, 500, 300, 0, [  // With stop points
  [200, 150],
  [350, 250]
]);
```

#### Element State Checks

```typescript
// Check visibility
if (await page.isVisible('error message')) {
  console.log('Error displayed');
}

// Check if enabled/disabled
if (await page.isEnabled('submit button')) {
  await page.click('submit button');
}

// Check checkbox/radio state
const isChecked = await page.isChecked('remember me checkbox');

// Get element attribute
const href = await page.getAttribute('a.link', 'href');
const placeholder = await page.getAttribute('input', 'placeholder');

// Get element position and size
const box = await page.getBoundingBox('header');
console.log(`Position: ${box.x}, ${box.y}`);
console.log(`Size: ${box.width} x ${box.height}`);
```

#### File Upload

```typescript
// Upload single file
await page.uploadFile('input[type="file"]', ['/path/to/document.pdf']);

// Upload multiple files
await page.uploadFile('file input', [
  '/path/to/image1.jpg',
  '/path/to/image2.jpg'
]);
```

#### Frame/Iframe Handling

```typescript
// List all frames on the page
const frames = await page.listFrames();
for (const frame of frames) {
  console.log(`Frame: ${frame.name || frame.id} - ${frame.url}`);
}

// Switch to iframe by selector
await page.switchToFrame('#payment-iframe');

// Or by frame name/id
await page.switchToFrame('checkout-frame');

// Interact with elements inside iframe
await page.type('card number input', '4111111111111111');
await page.click('submit payment');

// Switch back to main frame
await page.switchToMainFrame();
```

#### JavaScript Evaluation

```typescript
// Execute JavaScript in page context
const title = await page.evaluate<string>('document.title');

// With arguments
const text = await page.evaluate<string>(
  '(selector) => document.querySelector(selector)?.textContent',
  ['h1']
);

// Return complex objects
const metrics = await page.evaluate<{width: number, height: number}>(
  '() => ({ width: window.innerWidth, height: window.innerHeight })'
);
```

#### Content Extraction

```typescript
// Extract text
const text = await page.extractText('body');

// Get HTML (with cleaning)
const html = await page.getHTML('basic'); // 'minimal' | 'basic' | 'aggressive'

// Get Markdown
const markdown = await page.getMarkdown({
  includeLinks: true,
  includeImages: true,
  maxLength: 10000
});

// Extract structured JSON
const data = await page.extractJSON('google_search');
const siteType = await page.detectWebsiteType();
```

#### AI Features

```typescript
// Ask questions about the page
const answer = await page.queryPage('What is the main topic of this page?');

// Execute natural language commands
await page.executeNLA('go to google.com and search for banana');
```

#### Screenshots & Video

```typescript
// Screenshot
const screenshot = await page.screenshot();
fs.writeFileSync('screenshot.png', screenshot);

// Video recording
await page.startVideoRecording({ fps: 30 });
// ... perform actions ...
const videoPath = await page.stopVideoRecording();
console.log('Video saved to:', videoPath);
```

#### Scrolling

```typescript
await page.scrollBy(0, 500);
await page.scrollTo(0, 0);
await page.scrollToTop();
await page.scrollToBottom();
await page.scrollToElement('footer');
```

#### Waiting

```typescript
// Wait for element
await page.waitForSelector('submit button', { timeout: 5000 });

// Wait for time
await page.wait(1000); // 1 second
```

#### Test Execution

```typescript
// Load test from Developer Playground JSON export
const test = JSON.parse(fs.readFileSync('test.json', 'utf-8'));
const result = await page.runTest(test, {
  verbose: true,
  continueOnError: false,
  screenshotOnError: true
});

console.log(`Success: ${result.successfulSteps}/${result.totalSteps}`);
console.log(`Time: ${result.executionTime}ms`);

// Or define test inline
const inlineTest = {
  name: "Login Test",
  steps: [
    { type: "navigate", url: "https://example.com/login" },
    { type: "type", selector: "#email", text: "user@example.com" },
    { type: "click", selector: "500x300" },  // Position-based click
    { type: "screenshot", filename: "result.png" }
  ]
};
const result = await page.runTest(inlineTest);
```

#### Page Information

```typescript
const url = await page.getCurrentURL();
const title = await page.getTitle();
const info = await page.getPageInfo(); // { url, title, canGoBack, canGoForward }
```

#### Viewport

```typescript
await page.setViewport({ width: 1920, height: 1080 });
const viewport = await page.getViewport();
```

## Configuration

### Local Mode Configuration

```typescript
const browser = new Browser({
  // Connection mode (default: 'local')
  mode: 'local',

  // Path to browser binary (auto-detected if not provided)
  browserPath: '/path/to/owl_browser',

  // Enable headless mode (default: true)
  headless: true,

  // Enable verbose logging (default: false)
  verbose: true,

  // Initialization timeout in ms (default: 30000)
  initTimeout: 30000
});
```

### HTTP Mode Configuration

```typescript
const browser = new Browser({
  // Use HTTP mode to connect to remote server
  mode: 'http',

  // HTTP server connection settings (required for HTTP mode)
  http: {
    // Base URL of the HTTP server
    baseUrl: 'http://localhost:8080',

    // Bearer token for authentication (must match server's OWL_HTTP_TOKEN)
    token: 'your-secret-token',

    // Request timeout in ms (default: 30000)
    timeout: 30000
  },

  // Enable verbose logging (default: false)
  verbose: true
});
```

### Configuration from Environment Variables

```typescript
// HTTP mode with environment variables
const browser = new Browser({
  mode: 'http',
  http: {
    baseUrl: process.env.OWL_HTTP_URL || 'http://localhost:8080',
    token: process.env.OWL_HTTP_TOKEN || ''
  }
});
```

## Examples

### Demographics & Context Awareness

```typescript
import { Browser } from '@olib-ai/owl-browser-sdk';

const browser = new Browser();
await browser.launch();

// Get location based on IP address
const location = await browser.getLocation();
console.log(`Location: ${location.city}, ${location.country}`);
console.log(`Coordinates: ${location.latitude}, ${location.longitude}`);

// Get current weather
const weather = await browser.getWeather();
console.log(`Weather: ${weather.temperature_c}°C, ${weather.condition}`);

// Get date and time
const datetime = await browser.getDateTime();
console.log(`Date: ${datetime.date} (${datetime.day_of_week})`);
console.log(`Time: ${datetime.time} ${datetime.timezone}`);

// Get everything at once
const demographics = await browser.getDemographics();
console.log('Complete context:', demographics);

// Get custom homepage
const homepage = await browser.getHomepage();
fs.writeFileSync('homepage.html', homepage);

await browser.close();
```

### Simple Google Search

```typescript
import { Browser } from '@olib-ai/owl-browser-sdk';
import fs from 'fs';

async function googleSearch() {
  const browser = new Browser();
  await browser.launch();

  const page = await browser.newPage();
  await page.goto('https://www.google.com');

  // Natural language selectors!
  await page.type('search box', 'Owl Browser');
  await page.pressKey('Enter');

  await page.wait(2000);
  const screenshot = await page.screenshot();
  fs.writeFileSync('search-results.png', screenshot);

  await browser.close();
}

googleSearch();
```

### Web Scraping with AI

```typescript
import { Browser } from '@olib-ai/owl-browser-sdk';

async function scrapeWithAI() {
  const browser = new Browser();
  await browser.launch();

  const page = await browser.newPage();
  await page.goto('https://news.ycombinator.com');

  // Ask AI about the page
  const answer = await page.queryPage('What are the top 3 story titles?');
  console.log(answer);

  // Extract as Markdown
  const markdown = await page.getMarkdown();
  console.log(markdown);

  await browser.close();
}

scrapeWithAI();
```

### Video Recording

```typescript
import { Browser } from '@olib-ai/owl-browser-sdk';

async function recordSession() {
  const browser = new Browser();
  await browser.launch();

  const page = await browser.newPage();

  // Start recording
  await page.startVideoRecording({ fps: 30 });

  await page.goto('https://example.com');
  await page.click('some button');
  await page.scrollToBottom();

  // Stop and get video path
  const videoPath = await page.stopVideoRecording();
  console.log('Video saved to:', videoPath);

  await browser.close();
}

recordSession();
```

## TypeScript Support

This SDK is written in TypeScript and includes full type definitions.

```typescript
import { Browser, BrowserContext, Viewport } from '@olib-ai/owl-browser-sdk';

const browser: Browser = new Browser();
const page: BrowserContext = await browser.newPage();
const viewport: Viewport = await page.getViewport();
```

## Why Olib SDK vs Puppeteer/Playwright?

| Feature | Olib SDK | Puppeteer/Playwright |
|---------|----------|----------------------|
| Natural Language Selectors | ✅ Built-in | ❌ Not supported |
| On-Device LLM | ✅ Qwen3-1.7B | ❌ Not supported |
| Cold Start | <1s | 2-5s |
| WebDriver Detection | ✅ None | ⚠️ Detectable |
| Built-in Ad Blocking | ✅ 72 domains | ❌ Manual setup |
| Maintenance Overhead | ✅ Low (AI handles changes) | ⚠️ High (selectors break) |

### Network Interception

Block, mock, or redirect network requests:

```typescript
// Block ads
const ruleId = await page.addNetworkRule({
  urlPattern: 'https://ads.example.com/*',
  action: 'block'
});

// Mock API response
await page.addNetworkRule({
  urlPattern: 'https://api.example.com/data',
  action: 'mock',
  mockBody: '{"status": "ok"}',
  mockStatus: 200,
  mockContentType: 'application/json'
});

// Redirect requests
await page.addNetworkRule({
  urlPattern: 'https://old-api.example.com/*',
  action: 'redirect',
  redirectUrl: 'https://new-api.example.com/'
});

// Enable interception
await page.setNetworkInterception(true);

// Get network log
const log = await page.getNetworkLog();

// Remove rule
await page.removeNetworkRule(ruleId);
```

### File Downloads

Handle file downloads:

```typescript
// Set download directory
await page.setDownloadPath('/tmp/downloads');

// Click a download link
await page.click('a[download]');

// Wait for download to complete
const download = await page.waitForDownload();
console.log(`Downloaded: ${download.filename} to ${download.path}`);

// Or get all downloads
const downloads = await page.getDownloads();

// Cancel a download
await page.cancelDownload(download.id);
```

### Dialog Handling

Handle JavaScript dialogs (alert, confirm, prompt):

```typescript
// Auto-accept alerts
await page.setDialogAction('alert', 'accept');

// Auto-respond to prompts
await page.setDialogAction('prompt', 'accept_with_text', 'My answer');

// Dismiss confirms
await page.setDialogAction('confirm', 'dismiss');

// Or handle manually
const dialog = await page.waitForDialog();
console.log(`Dialog message: ${dialog.message}`);
await page.handleDialog(dialog.id, true, 'response text');
```

### Multi-Tab Management

Work with multiple tabs:

```typescript
// Create new tab
const tab = await page.newTab('https://google.com');
console.log(`Tab ID: ${tab.tabId}`);

// List all tabs
const tabs = await page.getTabs();

// Switch to a tab
await page.switchTab(tabs[0].tabId);

// Get active tab
const active = await page.getActiveTab();

// Get tab count
const count = await page.getTabCount();

// Close a tab
await page.closeTab(tab.tabId);

// Configure popup handling
await page.setPopupPolicy('block');  // 'allow', 'block', 'new_tab', 'background'

// Get blocked popups
const blocked = await page.getBlockedPopups();
```

## Advanced Features

### Semantic Element Matching

The browser uses an advanced semantic matcher that understands natural language:

```typescript
// All of these work!
await page.click('search button');
await page.click('search btn');
await page.click('search');
await page.type('email input', 'test@example.com');
await page.type('email field', 'test@example.com');
await page.type('email box', 'test@example.com');
```

The matcher uses:
- Keyword extraction and normalization
- Role inference (search_input, submit_button, etc.)
- Multi-source scoring (aria-label, placeholder, text, title)
- Fuzzy matching with confidence thresholds

### On-Device LLM

Check LLM status:

```typescript
const status = await browser.getLLMStatus();
// Returns: 'ready' | 'loading' | 'unavailable'
```

Query pages with natural language:

```typescript
const answer = await page.queryPage('What products are shown on this page?');
const summary = await page.queryPage('Summarize this article in 2 sentences');
```

## Error Handling

The SDK provides specific exception types for different error scenarios:

```typescript
import {
  Browser,
  AuthenticationError,
  RateLimitError,
  IPBlockedError,
  LicenseError,
  BrowserInitializationError,
  ActionError,
  ActionStatus,
  ElementNotFoundError,
  NavigationError,
  FirewallError
} from '@olib-ai/owl-browser-sdk';

async function withErrorHandling() {
  const browser = new Browser({
    mode: 'http',
    http: {
      baseUrl: 'http://localhost:8080',
      authMode: 'jwt',
      jwt: { privateKey: '/path/to/private.pem' }
    }
  });

  try {
    await browser.launch();
    const page = await browser.newPage();
    await page.goto('https://example.com');

    // Your automation code...
    await page.click('non-existent button');

  } catch (error) {
    if (error instanceof ElementNotFoundError) {
      // Element was not found on the page
      console.error('Element not found:', error.message);
    } else if (error instanceof NavigationError) {
      // Navigation failed (timeout, page load error, etc.)
      console.error('Navigation failed:', error.message);
    } else if (error instanceof ActionError) {
      // Generic action error with detailed status
      console.error('Action failed:', error.message);
      console.error('Status:', error.status);
      console.error('Selector:', error.selector);
      // Check specific error types
      if (error.isElementNotFound()) {
        console.error('Could not find element');
      } else if (error.isNavigationError()) {
        console.error('Navigation issue');
      } else if (error.isTimeout()) {
        console.error('Operation timed out');
      }
    } else if (error instanceof FirewallError) {
      // Web firewall/bot protection detected
      console.error('Firewall detected:', error.provider);
      console.error('Challenge type:', error.challengeType);
      console.error('URL:', error.url);
    } else if (error instanceof AuthenticationError) {
      // 401 - Invalid or expired token
      console.error('Auth failed:', error.message);
      console.error('Reason:', error.reason);
    } else if (error instanceof RateLimitError) {
      // 429 - Too many requests
      console.error('Rate limited. Retry after:', error.retryAfter, 'seconds');
    } else if (error instanceof IPBlockedError) {
      // 403 - IP not whitelisted
      console.error('IP blocked:', error.ipAddress);
    } else if (error instanceof LicenseError) {
      // License validation failed
      console.error('License error:', error.status);
    } else {
      console.error('Automation failed:', error);
    }
  } finally {
    await browser.close();
  }
}
```

### Action Result Validation

The browser returns structured `ActionResult` responses for browser actions, providing detailed information about success or failure:

```typescript
import {
  ActionStatus,
  ActionResult,
  ActionError,
  isActionResult,
  throwIfActionFailed
} from '@olib-ai/owl-browser-sdk';

// ActionStatus enum values
ActionStatus.OK                    // Action succeeded
ActionStatus.ELEMENT_NOT_FOUND     // Element not found on page
ActionStatus.ELEMENT_NOT_VISIBLE   // Element exists but not visible
ActionStatus.ELEMENT_NOT_INTERACTABLE // Element not interactable
ActionStatus.NAVIGATION_FAILED     // Navigation failed
ActionStatus.NAVIGATION_TIMEOUT    // Navigation timed out
ActionStatus.PAGE_LOAD_ERROR       // Page failed to load
ActionStatus.FIREWALL_DETECTED     // Web firewall/bot protection detected
ActionStatus.CAPTCHA_DETECTED      // CAPTCHA challenge detected
ActionStatus.CLICK_FAILED          // Click action failed
ActionStatus.TYPE_FAILED           // Type action failed
ActionStatus.CONTEXT_NOT_FOUND     // Browser context not found
ActionStatus.INVALID_SELECTOR      // Invalid selector provided
ActionStatus.TIMEOUT               // Operation timed out

// ActionResult interface
interface ActionResult {
  success: boolean;
  status: ActionStatus | string;
  message: string;
  selector?: string;      // The selector that failed (for element errors)
  url?: string;           // URL involved (for navigation errors)
  http_status?: number;   // HTTP status code (for navigation)
  error_code?: string;    // Browser error code
  element_count?: number; // Elements found (for multiple_elements error)
}

// Helper functions
if (isActionResult(result)) {
  if (!result.success) {
    throwIfActionFailed(result); // Throws appropriate exception
  }
}
```

### Exception Types

| Exception | HTTP Code | Description |
|-----------|-----------|-------------|
| `ActionError` | - | Browser action failed with status code and details |
| `ElementNotFoundError` | - | Element not found on page |
| `NavigationError` | - | Navigation failed (timeout, load error) |
| `FirewallError` | - | Web firewall/bot protection detected (Cloudflare, Akamai, etc.) |
| `ContextError` | - | Browser context not found |
| `AuthenticationError` | 401 | Invalid/expired token or JWT signature mismatch |
| `RateLimitError` | 429 | Too many requests, includes `retryAfter` in seconds |
| `IPBlockedError` | 403 | Client IP not in whitelist |
| `LicenseError` | 503 | Browser license validation failed |
| `BrowserInitializationError` | - | Failed to start/connect to browser |
| `CommandTimeoutError` | - | Operation timed out |

### FirewallError Properties

| Property | Type | Description |
|----------|------|-------------|
| `url` | `string` | The URL where the firewall was detected |
| `provider` | `string` | Firewall provider (Cloudflare, Akamai, Imperva, etc.) |
| `challengeType` | `string?` | Type of challenge (JS Challenge, CAPTCHA, etc.) |

The browser automatically detects web firewalls including Cloudflare, Akamai, Imperva, PerimeterX, DataDome, and AWS WAF.

### ActionError Properties

| Property | Type | Description |
|----------|------|-------------|
| `status` | `ActionStatus` | Status code (e.g., `element_not_found`) |
| `message` | `string` | Human-readable error message |
| `selector` | `string?` | The selector that failed |
| `url` | `string?` | URL involved in navigation errors |
| `httpStatus` | `number?` | HTTP status code |
| `errorCode` | `string?` | Browser error code |
| `elementCount` | `number?` | Number of elements found |

## Performance Tips

1. **Reuse browser instances** - Creating a new browser is expensive
2. **Use multiple pages** - Instead of multiple browsers, create multiple pages
3. **Wait strategically** - Use `waitForSelector()` instead of fixed `wait()`
4. **Close contexts** - Call `page.close()` when done with a page

## Troubleshooting

### Browser binary not found

Make sure you've built the browser first:

```bash
cd /path/to/owl-browser
npm run build
```

Or provide the path explicitly:

```typescript
const browser = new Browser({
  browserPath: '/custom/path/to/owl_browser'
});
```

### Timeout errors

Increase the timeout:

```typescript
const browser = new Browser({
  initTimeout: 60000 // 60 seconds
});
```

### Element not found

Try different selector variations:

```typescript
// Try multiple approaches
await page.click('search button');
await page.click('search');
await page.click('[aria-label="Search"]');
await page.click('#search-btn');
```

## License

MIT

## Credits

- Built with Chromium Embedded Framework (CEF)
- LLM powered by llama.cpp + Qwen3-1.7B
- Designed for AI automation and developer productivity
