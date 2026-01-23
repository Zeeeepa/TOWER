/**
 * Condition evaluation for flow execution.
 *
 * Provides condition evaluation logic for conditional branching
 * in flows.
 */
import { ConditionOperator } from '../types.js';
import { getValueAtPath } from './variables.js';
/**
 * Evaluate a condition against a value.
 *
 * @example
 * ```typescript
 * const result = { success: true, count: 5 };
 *
 * // Check if success is truthy
 * const cond = {
 *   source: 'previous',
 *   operator: ConditionOperator.IS_TRUTHY,
 *   field: 'success'
 * };
 * evaluateCondition(cond, result); // true
 *
 * // Check if count > 3
 * const cond2 = {
 *   source: 'previous',
 *   operator: ConditionOperator.GREATER_THAN,
 *   field: 'count',
 *   value: 3
 * };
 * evaluateCondition(cond2, result); // true
 * ```
 */
export function evaluateCondition(condition, value) {
    let checkValue = value;
    if (condition.field) {
        checkValue = getValueAtPath(value, condition.field);
    }
    switch (condition.operator) {
        case ConditionOperator.EQUALS:
            return checkValue === condition.value;
        case ConditionOperator.NOT_EQUALS:
            return checkValue !== condition.value;
        case ConditionOperator.CONTAINS:
            if (typeof checkValue === 'string' && typeof condition.value === 'string') {
                return checkValue.includes(condition.value);
            }
            if (Array.isArray(checkValue)) {
                return checkValue.includes(condition.value);
            }
            return false;
        case ConditionOperator.NOT_CONTAINS:
            if (typeof checkValue === 'string' && typeof condition.value === 'string') {
                return !checkValue.includes(condition.value);
            }
            if (Array.isArray(checkValue)) {
                return !checkValue.includes(condition.value);
            }
            return true;
        case ConditionOperator.STARTS_WITH:
            if (typeof checkValue === 'string' && typeof condition.value === 'string') {
                return checkValue.startsWith(condition.value);
            }
            return false;
        case ConditionOperator.ENDS_WITH:
            if (typeof checkValue === 'string' && typeof condition.value === 'string') {
                return checkValue.endsWith(condition.value);
            }
            return false;
        case ConditionOperator.GREATER_THAN:
            if (typeof checkValue === 'number' && typeof condition.value === 'number') {
                return checkValue > condition.value;
            }
            return false;
        case ConditionOperator.LESS_THAN:
            if (typeof checkValue === 'number' && typeof condition.value === 'number') {
                return checkValue < condition.value;
            }
            return false;
        case ConditionOperator.IS_TRUTHY:
            return Boolean(checkValue);
        case ConditionOperator.IS_FALSY:
            return !checkValue;
        case ConditionOperator.IS_EMPTY:
            if (checkValue === null || checkValue === undefined) {
                return true;
            }
            if (typeof checkValue === 'string') {
                return checkValue.length === 0;
            }
            if (Array.isArray(checkValue) || typeof checkValue === 'object') {
                return Object.keys(checkValue).length === 0;
            }
            return false;
        case ConditionOperator.IS_NOT_EMPTY:
            if (checkValue === null || checkValue === undefined) {
                return false;
            }
            if (typeof checkValue === 'string') {
                return checkValue.length > 0;
            }
            if (Array.isArray(checkValue) || typeof checkValue === 'object') {
                return Object.keys(checkValue).length > 0;
            }
            return true;
        case ConditionOperator.REGEX_MATCH:
            if (typeof checkValue === 'string' && typeof condition.value === 'string') {
                try {
                    const pattern = new RegExp(condition.value);
                    return pattern.test(checkValue);
                }
                catch {
                    return false;
                }
            }
            return false;
        default:
            return false;
    }
}
//# sourceMappingURL=conditions.js.map