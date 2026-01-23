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
export function getValueAtPath(obj, path) {
    if (!path) {
        return obj;
    }
    // Normalize path: convert bracket notation to dot notation
    const normalizedPath = path
        .replace(/^\[/, '')
        .replace(/\]\[/g, '.')
        .replace(/\]/g, '')
        .replace(/\[/g, '.')
        .replace(/^\./, '');
    const parts = normalizedPath.split('.');
    let current = obj;
    for (const part of parts) {
        if (current === null || current === undefined) {
            return undefined;
        }
        if (typeof current === 'object' && current !== null) {
            if (Array.isArray(current)) {
                const index = parseInt(part, 10);
                if (!isNaN(index) && index >= 0 && index < current.length) {
                    current = current[index];
                }
                else {
                    return undefined;
                }
            }
            else {
                current = current[part];
            }
        }
        else {
            return undefined;
        }
    }
    return current;
}
/**
 * Resolve variable references in a string value.
 */
function resolveVariableInString(value, previousResult) {
    // Check if entire string is a variable reference
    const fullMatch = value.match(/^\$\{prev([\[\.].*?)?\}$/);
    if (fullMatch) {
        const path = fullMatch[1] ?? '';
        return getValueAtPath(previousResult, path);
    }
    // Replace inline variable references
    return value.replace(/\$\{prev([\[\.].*?)?\}/g, (_, pathMatch) => {
        const path = pathMatch ?? '';
        const resolved = getValueAtPath(previousResult, path);
        if (resolved === null || resolved === undefined) {
            return '';
        }
        if (typeof resolved === 'string') {
            return resolved;
        }
        if (typeof resolved === 'object') {
            return JSON.stringify(resolved);
        }
        return String(resolved);
    });
}
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
export function resolveVariables(params, previousResult) {
    const resolved = {};
    for (const [key, value] of Object.entries(params)) {
        if (typeof value === 'string') {
            resolved[key] = resolveVariableInString(value, previousResult);
        }
        else if (value !== null && typeof value === 'object' && !Array.isArray(value)) {
            resolved[key] = resolveVariables(value, previousResult);
        }
        else if (Array.isArray(value)) {
            resolved[key] = value.map(item => {
                if (typeof item === 'string') {
                    return resolveVariableInString(item, previousResult);
                }
                if (item !== null && typeof item === 'object' && !Array.isArray(item)) {
                    return resolveVariables(item, previousResult);
                }
                return item;
            });
        }
        else {
            resolved[key] = value;
        }
    }
    return resolved;
}
//# sourceMappingURL=variables.js.map