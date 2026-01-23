/**
 * Expectation validation for flow execution.
 *
 * Provides validation of step results against expected values,
 * supporting various comparison types like equals, contains,
 * length, greater_than, etc.
 */
import type { StepExpectation, ExpectationResult } from '../types.js';
/**
 * Check if a result meets the expectation.
 *
 * @example
 * ```typescript
 * const result = { count: 5, items: [1, 2, 3] };
 *
 * // Check exact equality
 * const exp = { equals: 5, field: 'count' };
 * const check = checkExpectation(result, exp);
 * // check.passed = true
 *
 * // Check array length
 * const exp2 = { length: 3, field: 'items' };
 * const check2 = checkExpectation(result, exp2);
 * // check2.passed = true
 * ```
 */
export declare function checkExpectation(result: unknown, expected: StepExpectation): ExpectationResult;
//# sourceMappingURL=expectations.d.ts.map