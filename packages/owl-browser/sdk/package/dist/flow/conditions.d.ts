/**
 * Condition evaluation for flow execution.
 *
 * Provides condition evaluation logic for conditional branching
 * in flows.
 */
import type { FlowCondition } from '../types.js';
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
export declare function evaluateCondition(condition: FlowCondition, value: unknown): boolean;
//# sourceMappingURL=conditions.d.ts.map