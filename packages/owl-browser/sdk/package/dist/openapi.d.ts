/**
 * OpenAPI schema loading and dynamic method generation.
 *
 * This module loads the bundled OpenAPI schema and generates tool definitions
 * that can be used to create dynamic methods.
 */
import type { ToolDefinition } from './types.js';
/**
 * Get the bundled OpenAPI schema.
 *
 * This is the primary way to access the bundled schema. The schema
 * is loaded lazily on first access and cached thereafter.
 */
export declare function getBundledSchema(): Record<string, unknown>;
/**
 * Loads and parses OpenAPI schema to extract tool definitions.
 *
 * @example
 * ```typescript
 * // Load from bundled schema
 * const loader = new OpenAPILoader(getBundledSchema());
 *
 * // Or from file
 * const loader = OpenAPILoader.fromFile('openapi.json');
 *
 * // Access tools
 * for (const [name, tool] of loader.tools) {
 *   console.log(name + ': ' + tool.description);
 * }
 * ```
 */
export declare class OpenAPILoader {
    private readonly _schema;
    private readonly _tools;
    constructor(schema: Record<string, unknown>);
    /**
     * Load OpenAPI schema from a JSON file.
     */
    static fromFile(filePath: string): OpenAPILoader;
    /**
     * Load OpenAPI schema from a JSON string.
     */
    static fromJSON(jsonStr: string): OpenAPILoader;
    /**
     * Get all parsed tool definitions.
     */
    get tools(): Map<string, ToolDefinition>;
    /**
     * Get a specific tool definition by name.
     */
    getTool(name: string): ToolDefinition | undefined;
    private _parseSchema;
    private _parseTool;
}
/**
 * Coerce numeric values to integers for fields that require it.
 *
 * The HTTP API expects integers but JSON may send floats like 200.0.
 * This function ensures such fields are properly converted.
 */
export declare function coerceIntegerFields(tool: ToolDefinition, params: Record<string, unknown>): Record<string, unknown>;
//# sourceMappingURL=openapi.d.ts.map