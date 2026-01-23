# @olib-ai/owl-browser-sdk

Node.js SDK v2 for [Owl Browser](https://www.owlbrowser.net) - AI-native browser automation with antidetect capabilities.

## Features

- **Async-first design** - All operations are async/await based
- **Dynamic method generation** - 144+ browser tools available as typed methods
- **OpenAPI schema bundled** - Works offline, no need to fetch schema from server
- **Flow execution engine** - Run complex automation flows with conditions and expectations
- **JWT and Token auth** - Flexible authentication options
- **TypeScript support** - Full type definitions included
- **Retry with backoff** - Built-in retry logic with exponential backoff and jitter

## Installation

```bash
npm install @olib-ai/owl-browser-sdk
```

## Quick Start

```typescript
import { OwlBrowser, RemoteConfig } from '@olib-ai/owl-browser-sdk';

const browser = new OwlBrowser({
  url: 'http://localhost:8080',
  token: 'your-secret-token',
  apiPrefix: ''  // Use '' for direct connection, '/api' for nginx proxy
});

await browser.connect();

// Create a context
const ctx = await browser.createContext();
const contextId = ctx.context_id;

// Navigate to a page
await browser.navigate({ context_id: contextId, url: 'https://example.com' });

// Click an element
await browser.click({ context_id: contextId, selector: 'button#submit' });

// Take a screenshot
const screenshot = await browser.screenshot({ context_id: contextId });

// Close the context
await browser.closeContext({ context_id: contextId });

await browser.close();
```

## Configuration

### RemoteConfig Options

```typescript
interface RemoteConfig {
  // Required
  url: string;                    // Server URL (e.g., 'http://localhost:8080')
  
  // Authentication (one required)
  token?: string;                 // Bearer token for TOKEN auth
  authMode?: AuthMode;            // 'token' (default) or 'jwt'
  jwt?: JWTConfig;                // JWT configuration for JWT auth
  
  // Optional
  transport?: TransportMode;      // 'http' (default) or 'websocket'
  timeout?: number;               // Request timeout in seconds (default: 30)
  maxConcurrent?: number;         // Max concurrent requests (default: 10)
  retry?: RetryConfig;            // Retry configuration
  verifySsl?: boolean;            // Verify SSL certificates (default: true)
  apiPrefix?: string;             // API prefix (default: '/api', use '' for direct)
}
```

### JWT Authentication

```typescript
import { OwlBrowser, AuthMode } from '@olib-ai/owl-browser-sdk';

const browser = new OwlBrowser({
  url: 'http://localhost:8080',
  authMode: AuthMode.JWT,
  jwt: {
    privateKeyPath: '/path/to/private.pem',
    expiresIn: 3600,        // Token validity in seconds
    refreshThreshold: 300,   // Refresh when < 300s remaining
    issuer: 'my-app',
    claims: { custom: 'data' }
  }
});
```

## Dynamic Methods

The SDK dynamically generates methods for all 144+ browser tools. Methods are available in both camelCase and snake_case:

```typescript
// These are equivalent
await browser.createContext();
await browser.create_context();

// Navigation
await browser.navigate({ context_id: ctx, url: 'https://example.com' });
await browser.reload({ context_id: ctx });
await browser.goBack({ context_id: ctx });
await browser.goForward({ context_id: ctx });

// Interaction
await browser.click({ context_id: ctx, selector: '#button' });
await browser.type({ context_id: ctx, selector: '#input', text: 'Hello' });
await browser.scroll({ context_id: ctx, direction: 'down' });

// Data extraction
await browser.getHtml({ context_id: ctx });
await browser.getMarkdown({ context_id: ctx });
await browser.screenshot({ context_id: ctx });

// AI-powered tools
await browser.queryPage({ context_id: ctx, question: 'What is the title?' });
await browser.solveCaptcha({ context_id: ctx });
await browser.findElement({ context_id: ctx, description: 'login button' });
```

## Flow Execution

Execute complex automation flows with variable resolution and expectations:

```typescript
import { OwlBrowser, FlowExecutor } from '@olib-ai/owl-browser-sdk';

const browser = new OwlBrowser({ url: '...', token: '...' });
await browser.connect();

const ctx = await browser.createContext();
const executor = new FlowExecutor(browser, ctx.context_id);

// Load flow from JSON file
const flow = FlowExecutor.loadFlow('test-flows/navigation.json');
const result = await executor.execute(flow);

if (result.success) {
  console.log('Flow completed in', result.totalDurationMs, 'ms');
} else {
  console.error('Flow failed:', result.error);
}
```

### Flow JSON Format

```json
{
  "name": "Login Flow",
  "description": "Automates login process",
  "steps": [
    {
      "type": "browser_navigate",
      "url": "https://example.com/login"
    },
    {
      "type": "browser_type",
      "selector": "#email",
      "text": "user@example.com"
    },
    {
      "type": "browser_type",
      "selector": "#password",
      "text": "secret123"
    },
    {
      "type": "browser_click",
      "selector": "#submit",
      "expected": {
        "notEmpty": true
      }
    },
    {
      "type": "browser_wait_for_selector",
      "selector": ".dashboard",
      "expected": {
        "equals": true,
        "field": "found"
      }
    }
  ]
}
```

### Variable Resolution

Use `${prev}` to reference previous step results:

```json
{
  "steps": [
    {
      "type": "browser_evaluate",
      "script": "document.querySelector('.user-id').textContent"
    },
    {
      "type": "browser_navigate",
      "url": "https://example.com/users/${prev}"
    }
  ]
}
```

Supported syntax:
- `${prev}` - Entire previous result
- `${prev.field}` - Field in previous result
- `${prev[0]}` - Array element
- `${prev[0].id}` - Nested access

### Expectations

Validate step results:

```typescript
const expectation = {
  equals: 'expected value',     // Exact match
  contains: 'substring',        // String contains
  length: 5,                    // Array/string length
  greaterThan: 10,              // Numeric comparison
  lessThan: 100,                // Numeric comparison
  notEmpty: true,               // Not null/empty
  matches: '^[A-Z]+$',          // Regex pattern
  field: 'data.items'           // Nested field to check
};
```

### Conditional Branching

```json
{
  "type": "condition",
  "condition": {
    "source": "previous",
    "operator": "equals",
    "field": "success",
    "value": true
  },
  "onTrue": [
    { "type": "browser_click", "selector": "#continue" }
  ],
  "onFalse": [
    { "type": "browser_screenshot" }
  ]
}
```

## Error Handling

```typescript
import {
  OwlBrowserError,
  ConnectionError,
  AuthenticationError,
  ToolExecutionError,
  TimeoutError,
  RateLimitError,
  ElementNotFoundError
} from '@olib-ai/owl-browser-sdk';

try {
  await browser.click({ context_id: ctx, selector: '#nonexistent' });
} catch (e) {
  if (e instanceof ElementNotFoundError) {
    console.log('Element not found:', e.selector);
  } else if (e instanceof TimeoutError) {
    console.log('Operation timed out after', e.timeoutMs, 'ms');
  } else if (e instanceof RateLimitError) {
    console.log('Rate limited. Retry after', e.retryAfter, 'seconds');
  } else if (e instanceof AuthenticationError) {
    console.log('Auth failed:', e.message);
  }
}
```

## Advanced Usage

### Direct Transport Access

```typescript
import { HTTPTransport, WebSocketTransport } from '@olib-ai/owl-browser-sdk';

// Use HTTP transport directly
const transport = new HTTPTransport({
  url: 'http://localhost:8080',
  token: 'secret'
});

await transport.connect();
const result = await transport.execute('browser_navigate', {
  context_id: 'ctx_1',
  url: 'https://example.com'
});
await transport.close();
```

### OpenAPI Schema Access

```typescript
import { OpenAPILoader, getBundledSchema } from '@olib-ai/owl-browser-sdk';

// Get bundled schema
const schema = getBundledSchema();
console.log('API Version:', schema.info.version);

// Load and parse schema
const loader = new OpenAPILoader(schema);
for (const [name, tool] of loader.tools) {
  console.log(name + ':', tool.description);
}
```

## API Reference

### OwlBrowser

- `connect(): Promise<void>` - Connect to server
- `close(): Promise<void>` - Close connection
- `execute(toolName, params): Promise<unknown>` - Execute any tool
- `healthCheck(): Promise<HealthResponse>` - Check server health
- `listTools(): string[]` - List all tool names
- `listMethods(): string[]` - List all method names
- `getTool(name): ToolDefinition | undefined` - Get tool definition

### FlowExecutor

- `execute(flow): Promise<FlowResult>` - Execute a flow
- `abort(): void` - Abort current execution
- `reset(): void` - Reset abort flag
- `static loadFlow(path): Flow` - Load flow from JSON file
- `static parseFlow(data): Flow` - Parse flow from object

## Requirements

- Node.js 18+
- TypeScript 5+ (optional, for type definitions)

## License

MIT - See LICENSE file for details.

## Links

- **Website**: https://www.owlbrowser.net
- **Documentation**: https://docs.owlbrowser.net
- **GitHub**: https://github.com/Olib-AI/olib-browser
- **Support**: support@olib.ai
