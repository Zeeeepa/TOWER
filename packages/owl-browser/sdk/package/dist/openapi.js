/**
 * OpenAPI schema loading and dynamic method generation.
 *
 * This module loads the bundled OpenAPI schema and generates tool definitions
 * that can be used to create dynamic methods.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { fileURLToPath } from 'node:url';
import { OpenAPISchemaError } from './errors.js';
// Get the directory of this module
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
// Path to the bundled OpenAPI schema file
const SCHEMA_PATH = path.join(__dirname, '..', 'openapi.json');
// Cached schema
let _cachedSchema = null;
/**
 * Load the bundled OpenAPI schema from file.
 */
function loadBundledSchema() {
    if (_cachedSchema) {
        return _cachedSchema;
    }
    try {
        const content = fs.readFileSync(SCHEMA_PATH, 'utf-8');
        _cachedSchema = JSON.parse(content);
        return _cachedSchema;
    }
    catch (e) {
        if (e.code === 'ENOENT') {
            throw new OpenAPISchemaError('Bundled OpenAPI schema not found at ' + SCHEMA_PATH + '. The SDK package may be corrupted.', e instanceof Error ? e : undefined);
        }
        if (e instanceof SyntaxError) {
            throw new OpenAPISchemaError('Invalid JSON in bundled OpenAPI schema: ' + e.message, e);
        }
        throw new OpenAPISchemaError('Failed to load bundled OpenAPI schema: ' + (e instanceof Error ? e.message : String(e)), e instanceof Error ? e : undefined);
    }
}
/**
 * Get the bundled OpenAPI schema.
 *
 * This is the primary way to access the bundled schema. The schema
 * is loaded lazily on first access and cached thereafter.
 */
export function getBundledSchema() {
    return loadBundledSchema();
}
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
export class OpenAPILoader {
    _schema;
    _tools = new Map();
    constructor(schema) {
        this._schema = schema;
        this._parseSchema();
    }
    /**
     * Load OpenAPI schema from a JSON file.
     */
    static fromFile(filePath) {
        try {
            const content = fs.readFileSync(filePath, 'utf-8');
            const schema = JSON.parse(content);
            return new OpenAPILoader(schema);
        }
        catch (e) {
            if (e.code === 'ENOENT') {
                throw new OpenAPISchemaError('OpenAPI schema file not found: ' + filePath, e instanceof Error ? e : undefined);
            }
            if (e instanceof SyntaxError) {
                throw new OpenAPISchemaError('Invalid JSON in OpenAPI schema: ' + e.message, e);
            }
            throw new OpenAPISchemaError('Failed to load OpenAPI schema: ' + (e instanceof Error ? e.message : String(e)), e instanceof Error ? e : undefined);
        }
    }
    /**
     * Load OpenAPI schema from a JSON string.
     */
    static fromJSON(jsonStr) {
        try {
            const schema = JSON.parse(jsonStr);
            return new OpenAPILoader(schema);
        }
        catch (e) {
            if (e instanceof SyntaxError) {
                throw new OpenAPISchemaError('Invalid JSON in OpenAPI schema: ' + e.message, e);
            }
            throw e;
        }
    }
    /**
     * Get all parsed tool definitions.
     */
    get tools() {
        return this._tools;
    }
    /**
     * Get a specific tool definition by name.
     */
    getTool(name) {
        return this._tools.get(name);
    }
    _parseSchema() {
        const paths = this._schema['paths'];
        if (!paths) {
            return;
        }
        for (const [pathKey, pathItem] of Object.entries(paths)) {
            // Include all execute paths
            if (!pathKey.startsWith('/api/execute/')) {
                continue;
            }
            const postOp = pathItem['post'];
            if (!postOp) {
                continue;
            }
            const toolName = pathKey.split('/').pop();
            this._tools.set(toolName, this._parseTool(toolName, postOp));
        }
    }
    _parseTool(name, operation) {
        const description = String(operation['description'] ?? operation['summary'] ?? 'Execute ' + name);
        const requestBody = (operation['requestBody'] ?? {});
        const content = (requestBody['content'] ?? {});
        const jsonContent = (content['application/json'] ?? {});
        const schema = (jsonContent['schema'] ?? {});
        const parameters = {};
        const requiredParams = (schema['required'] ?? []);
        const integerFields = new Set();
        const properties = (schema['properties'] ?? {});
        for (const [propName, propDef] of Object.entries(properties)) {
            const prop = propDef;
            let paramType = String(prop['type'] ?? 'string');
            if (paramType === 'integer') {
                integerFields.add(propName);
                paramType = 'number';
            }
            const enumValues = prop['enum'];
            const defaultValue = prop['default'];
            parameters[propName] = {
                name: propName,
                type: paramType,
                required: requiredParams.includes(propName),
                description: String(prop['description'] ?? ''),
                enumValues,
                default: defaultValue,
            };
        }
        return {
            name,
            description,
            parameters,
            requiredParams,
            integerFields,
        };
    }
}
/**
 * Coerce numeric values to integers for fields that require it.
 *
 * The HTTP API expects integers but JSON may send floats like 200.0.
 * This function ensures such fields are properly converted.
 */
export function coerceIntegerFields(tool, params) {
    if (tool.integerFields.size === 0) {
        return params;
    }
    const coerced = { ...params };
    for (const field of tool.integerFields) {
        if (field in coerced && typeof coerced[field] === 'number') {
            coerced[field] = Math.floor(coerced[field]);
        }
    }
    return coerced;
}
//# sourceMappingURL=openapi.js.map