/**
 * Expectation validation for flow execution.
 *
 * Provides validation of step results against expected values,
 * supporting various comparison types like equals, contains,
 * length, greater_than, etc.
 */
import { getValueAtPath } from './variables.js';
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
export function checkExpectation(result, expected) {
    let valueToCheck = result;
    if (expected.field) {
        valueToCheck = getValueAtPath(result, expected.field);
    }
    // Check equals
    if (expected.equals !== undefined) {
        const actualJson = JSON.stringify(valueToCheck, Object.keys(valueToCheck ?? {}).sort());
        const expectedJson = JSON.stringify(expected.equals, Object.keys(expected.equals ?? {}).sort());
        const passed = actualJson === expectedJson;
        return {
            passed,
            message: passed
                ? 'Value matches expected'
                : 'Expected ' + JSON.stringify(expected.equals) + ', got ' + JSON.stringify(valueToCheck),
            expected: expected.equals,
            actual: valueToCheck,
        };
    }
    // Check contains
    if (expected.contains !== undefined) {
        const strValue = valueToCheck !== null && valueToCheck !== undefined
            ? String(valueToCheck)
            : '';
        const passed = strValue.includes(expected.contains);
        return {
            passed,
            message: passed
                ? 'Value contains "' + expected.contains + '"'
                : 'Expected to contain "' + expected.contains + '", got "' + strValue + '"',
            expected: expected.contains,
            actual: strValue,
        };
    }
    // Check length
    if (expected.length !== undefined) {
        let actualLength;
        if (Array.isArray(valueToCheck)) {
            actualLength = valueToCheck.length;
        }
        else if (typeof valueToCheck === 'string') {
            actualLength = valueToCheck.length;
        }
        else {
            actualLength = 0;
        }
        const passed = actualLength === expected.length;
        return {
            passed,
            message: passed
                ? 'Length is ' + expected.length
                : 'Expected length ' + expected.length + ', got ' + actualLength,
            expected: expected.length,
            actual: actualLength,
        };
    }
    // Check greaterThan
    if (expected.greaterThan !== undefined) {
        let num;
        try {
            num = valueToCheck !== null && valueToCheck !== undefined
                ? Number(valueToCheck)
                : NaN;
        }
        catch {
            num = NaN;
        }
        const passed = !isNaN(num) && num > expected.greaterThan;
        return {
            passed,
            message: passed
                ? 'Value ' + num + ' > ' + expected.greaterThan
                : 'Expected > ' + expected.greaterThan + ', got ' + num,
            expected: '> ' + expected.greaterThan,
            actual: num,
        };
    }
    // Check lessThan
    if (expected.lessThan !== undefined) {
        let num;
        try {
            num = valueToCheck !== null && valueToCheck !== undefined
                ? Number(valueToCheck)
                : NaN;
        }
        catch {
            num = NaN;
        }
        const passed = !isNaN(num) && num < expected.lessThan;
        return {
            passed,
            message: passed
                ? 'Value ' + num + ' < ' + expected.lessThan
                : 'Expected < ' + expected.lessThan + ', got ' + num,
            expected: '< ' + expected.lessThan,
            actual: num,
        };
    }
    // Check notEmpty
    if (expected.notEmpty !== undefined) {
        let isEmpty = false;
        if (valueToCheck === null || valueToCheck === undefined) {
            isEmpty = true;
        }
        else if (typeof valueToCheck === 'string') {
            isEmpty = valueToCheck.length === 0;
        }
        else if (Array.isArray(valueToCheck)) {
            isEmpty = valueToCheck.length === 0;
        }
        else if (typeof valueToCheck === 'object') {
            isEmpty = Object.keys(valueToCheck).length === 0;
        }
        if (expected.notEmpty) {
            // notEmpty=true means value should NOT be empty
            const passed = !isEmpty;
            return {
                passed,
                message: passed
                    ? 'Value is not empty'
                    : 'Expected non-empty value, got empty',
                expected: 'non-empty',
                actual: valueToCheck,
            };
        }
        else {
            // notEmpty=false means value SHOULD be empty
            const passed = isEmpty;
            return {
                passed,
                message: passed
                    ? 'Value is empty'
                    : 'Expected empty value, got non-empty',
                expected: 'empty',
                actual: valueToCheck,
            };
        }
    }
    // Check matches (regex)
    if (expected.matches !== undefined) {
        const strValue = valueToCheck !== null && valueToCheck !== undefined
            ? String(valueToCheck)
            : '';
        let passed = false;
        try {
            const pattern = new RegExp(expected.matches);
            passed = pattern.test(strValue);
        }
        catch {
            passed = false;
        }
        return {
            passed,
            message: passed
                ? 'Value matches pattern /' + expected.matches + '/'
                : 'Expected to match /' + expected.matches + '/, got "' + strValue + '"',
            expected: expected.matches,
            actual: strValue,
        };
    }
    // No expectation specified
    return {
        passed: true,
        message: 'No expectation specified',
    };
}
//# sourceMappingURL=expectations.js.map