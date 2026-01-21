/**
 * Owl Browser MCP Server
 *
 * An MCP server that connects to the Owl Browser HTTP API.
 * Tools are dynamically generated from the embedded OpenAPI schema.
 *
 * Configuration (environment variables):
 *   OWL_API_ENDPOINT - HTTP API endpoint (default: http://127.0.0.1:8080)
 *   OWL_API_TOKEN    - Bearer token for authentication
 *
 * Protocol version: 2025-11-25
 */

import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
  Tool,
} from '@modelcontextprotocol/sdk/types.js';

// Import the OpenAPI schema (embedded at build time)
import openApiSchema from '../openapi.json' with { type: 'json' };

// Configuration from environment
const API_ENDPOINT = process.env.OWL_API_ENDPOINT || 'http://127.0.0.1:8080';
const API_TOKEN = process.env.OWL_API_TOKEN || '';

// Track active contexts for this MCP session
const activeContexts = new Map<string, { createdAt: Date; url?: string }>();

/**
 * OpenAPI schema types
 */
interface OpenAPISchema {
  openapi: string;
  info: { title: string; version: string };
  paths: Record<string, PathItem>;
}

interface PathItem {
  post?: OperationObject;
}

interface OperationObject {
  summary?: string;
  description?: string;
  requestBody?: {
    required?: boolean;
    content?: {
      'application/json'?: {
        schema?: SchemaObject;
      };
    };
  };
}

interface SchemaObject {
  type?: string;
  properties?: Record<string, PropertyObject>;
  required?: string[];
}

interface PropertyObject {
  type?: string;
  description?: string;
  enum?: string[];
  items?: PropertyObject;
  default?: unknown;
}

/**
 * Convert OpenAPI schema to MCP tool format
 */
function convertOpenAPIToMCPTools(schema: OpenAPISchema): Tool[] {
  const tools: Tool[] = [];

  for (const [path, pathItem] of Object.entries(schema.paths)) {
    if (!pathItem.post) continue;

    // Extract tool name from path: /api/execute/browser_navigate -> browser_navigate
    const toolName = path.replace('/api/execute/', '');

    const operation = pathItem.post;
    const requestSchema = operation.requestBody?.content?.['application/json']?.schema;

    // Convert OpenAPI properties to JSON Schema for MCP
    const inputSchema: Record<string, unknown> = {
      type: 'object',
      properties: {} as Record<string, unknown>,
    };

    if (requestSchema?.properties) {
      const properties: Record<string, unknown> = {};

      for (const [propName, propDef] of Object.entries(requestSchema.properties)) {
        const prop: Record<string, unknown> = {};

        // Map OpenAPI types to JSON Schema types
        if (propDef.type === 'string') {
          prop.type = 'string';
          if (propDef.enum) {
            prop.enum = propDef.enum;
          }
        } else if (propDef.type === 'boolean') {
          prop.type = 'boolean';
        } else if (propDef.type === 'number' || propDef.type === 'integer') {
          prop.type = 'number';
        } else if (propDef.type === 'array') {
          prop.type = 'array';
          if (propDef.items) {
            prop.items = { type: propDef.items.type || 'string' };
          }
        } else {
          prop.type = 'string'; // Default to string for unknown types
        }

        if (propDef.description) {
          prop.description = propDef.description;
        }

        if (propDef.default !== undefined) {
          prop.default = propDef.default;
        }

        properties[propName] = prop;
      }

      inputSchema.properties = properties;
    }

    if (requestSchema?.required && requestSchema.required.length > 0) {
      inputSchema.required = requestSchema.required;
    }

    tools.push({
      name: toolName,
      description: operation.description || operation.summary || `Execute ${toolName}`,
      inputSchema,
    });
  }

  return tools;
}

/**
 * Make HTTP request to the Owl Browser API
 */
async function callBrowserAPI(
  toolName: string,
  args: Record<string, unknown>
): Promise<{ success: boolean; data?: unknown; error?: string }> {
  const url = `${API_ENDPOINT}/api/execute/${toolName}`;

  const headers: Record<string, string> = {
    'Content-Type': 'application/json',
  };

  if (API_TOKEN) {
    headers['Authorization'] = `Bearer ${API_TOKEN}`;
  }

  try {
    const response = await fetch(url, {
      method: 'POST',
      headers,
      body: JSON.stringify(args),
    });

    const contentType = response.headers.get('content-type') || '';
    let data: unknown;

    if (contentType.includes('application/json')) {
      data = await response.json();
    } else if (contentType.includes('image/')) {
      // Handle screenshot responses - return as base64
      const buffer = await response.arrayBuffer();
      const base64 = Buffer.from(buffer).toString('base64');
      const mimeType = contentType.split(';')[0];
      data = { image: `data:${mimeType};base64,${base64}` };
    } else {
      data = await response.text();
    }

    if (!response.ok) {
      return {
        success: false,
        error: `HTTP ${response.status}: ${typeof data === 'string' ? data : JSON.stringify(data)}`,
      };
    }

    return { success: true, data };
  } catch (error) {
    return {
      success: false,
      error: error instanceof Error ? error.message : String(error),
    };
  }
}

/**
 * Track context lifecycle
 */
function trackContext(toolName: string, args: Record<string, unknown>, result: unknown): void {
  if (toolName === 'browser_create_context' && typeof result === 'object' && result !== null) {
    const contextId = (result as Record<string, unknown>).context_id as string;
    if (contextId) {
      activeContexts.set(contextId, { createdAt: new Date() });
    }
  } else if (toolName === 'browser_close_context') {
    const contextId = args.context_id as string;
    if (contextId) {
      activeContexts.delete(contextId);
    }
  } else if (toolName === 'browser_navigate') {
    const contextId = args.context_id as string;
    const url = args.url as string;
    if (contextId && activeContexts.has(contextId)) {
      activeContexts.get(contextId)!.url = url;
    }
  }
}

/**
 * Format tool response for MCP
 */
function formatResponse(
  toolName: string,
  result: { success: boolean; data?: unknown; error?: string }
): { content: Array<{ type: string; text?: string; data?: string; mimeType?: string }> } {
  if (!result.success) {
    return {
      content: [
        {
          type: 'text',
          text: JSON.stringify({ success: false, error: result.error }, null, 2),
        },
      ],
    };
  }

  const data = result.data;

  // Handle image responses
  if (typeof data === 'object' && data !== null && 'image' in data) {
    const imageData = (data as { image: string }).image;
    if (imageData.startsWith('data:image/')) {
      const [header, base64] = imageData.split(',');
      const mimeType = header.match(/data:(image\/[^;]+)/)?.[1] || 'image/png';
      return {
        content: [
          {
            type: 'image',
            data: base64,
            mimeType,
          },
        ],
      };
    }
  }

  // Handle text/object responses
  const text = typeof data === 'string' ? data : JSON.stringify(data, null, 2);
  return {
    content: [{ type: 'text', text }],
  };
}

/**
 * Main server setup
 */
async function main(): Promise<void> {
  // Convert OpenAPI schema to MCP tools
  const tools = convertOpenAPIToMCPTools(openApiSchema as OpenAPISchema);

  console.error(`[owl-browser-mcp] Loaded ${tools.length} tools from OpenAPI schema`);
  console.error(`[owl-browser-mcp] API endpoint: ${API_ENDPOINT}`);
  console.error(`[owl-browser-mcp] Authentication: ${API_TOKEN ? 'Bearer token configured' : 'No token (anonymous)'}`);

  // Create MCP server
  const server = new Server(
    {
      name: 'owl-browser',
      version: '1.0.0',
    },
    {
      capabilities: {
        tools: {},
      },
    }
  );

  // Handle tool listing
  server.setRequestHandler(ListToolsRequestSchema, async () => {
    return { tools };
  });

  // Handle tool execution
  server.setRequestHandler(CallToolRequestSchema, async (request) => {
    const { name, arguments: args } = request.params;

    // Validate tool exists
    const tool = tools.find((t) => t.name === name);
    if (!tool) {
      return {
        content: [{ type: 'text', text: `Unknown tool: ${name}` }],
        isError: true,
      };
    }

    // Call the HTTP API
    const result = await callBrowserAPI(name, (args as Record<string, unknown>) || {});

    // Track context lifecycle
    if (result.success && result.data) {
      trackContext(name, (args as Record<string, unknown>) || {}, result.data);
    }

    // Format and return response
    const response = formatResponse(name, result);

    return {
      ...response,
      isError: !result.success,
    };
  });

  // Connect via stdio transport
  const transport = new StdioServerTransport();
  await server.connect(transport);

  console.error('[owl-browser-mcp] Server started, waiting for requests...');

  // Handle cleanup on exit
  process.on('SIGINT', async () => {
    console.error('[owl-browser-mcp] Shutting down...');

    // Close any active contexts
    for (const contextId of activeContexts.keys()) {
      try {
        await callBrowserAPI('browser_close_context', { context_id: contextId });
        console.error(`[owl-browser-mcp] Closed context: ${contextId}`);
      } catch {
        // Ignore errors during cleanup
      }
    }

    await server.close();
    process.exit(0);
  });
}

main().catch((error) => {
  console.error('[owl-browser-mcp] Fatal error:', error);
  process.exit(1);
});
