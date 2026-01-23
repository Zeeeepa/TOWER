/**
 * Variable resolution for flow execution.
 *
 * Supports ${prev} and ${prev.field.subfield} and ${prev[0].id} syntax
 * for referencing previous step results.
 */
/**
 * Get value at a dot-notation path (e.g., 'data.items.0.name').
 * Also supports bracket notation for array indices (e.g., '[0].id').
 */
export declare function getValueAtPath(obj: unknown, path: string): unknown;
/**
 * Recursively resolve variable references in params object.
 *
 * Variable syntax:
 *   ${prev} - reference the entire previous result
 *   ${prev.field} - reference a field in the previous result
 *   ${prev[0]} - reference an array element
 *   ${prev[0].id} - reference a field in an array element
 *
 * @example
 * ```typescript
 * const previous = { users: [{ id: 123, name: 'Alice' }] };
 * const params = { user_id: '${prev.users[0].id}' };
 * const resolved = resolveVariables(params, previous);
 * // resolved = { user_id: 123 }
 * ```
 */
export declare function resolveVariables(params: Record<string, unknown>, previousResult: unknown): Record<string, unknown>;
//# sourceMappingURL=variables.d.ts.map