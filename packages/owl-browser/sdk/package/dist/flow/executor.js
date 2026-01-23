/**
 * Flow execution engine for Owl Browser SDK v2.
 *
 * Provides the FlowExecutor class that executes flows with variable resolution,
 * expectation validation, and conditional branching support.
 */
import * as fs from 'node:fs';
import { ConditionOperator } from '../types.js';
import { resolveVariables } from './variables.js';
import { checkExpectation } from './expectations.js';
import { evaluateCondition } from './conditions.js';
/**
 * Parameter aliases for flow JSON compatibility.
 * Maps tool_name -> {alias_name -> canonical_name}
 */
const PARAMETER_ALIASES = {
    browser_wait: {
        ms: 'timeout',
    },
    browser_set_proxy: {
        proxy_type: 'type',
    },
};
/**
 * Tools where 'description' is a tool parameter, not just a step comment.
 */
const TOOLS_WITH_DESCRIPTION_PARAM = new Set([
    'browser_find_element',
    'browser_ai_click',
    'browser_ai_type',
]);
/**
 * Apply parameter aliases to convert flow shorthand names to API names.
 */
function applyParameterAliases(toolName, params) {
    const aliases = PARAMETER_ALIASES[toolName];
    if (!aliases) {
        return params;
    }
    const result = { ...params };
    for (const [alias, canonical] of Object.entries(aliases)) {
        if (alias in result && !(canonical in result)) {
            result[canonical] = result[alias];
            delete result[alias];
        }
    }
    return result;
}
/**
 * Generate a unique step ID.
 */
function generateStepId() {
    return 'step_' + Math.random().toString(36).substring(2, 10);
}
/**
 * Sleep for a given number of milliseconds.
 */
function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}
/**
 * Flow execution engine that runs a series of browser tool steps.
 *
 * Supports:
 * - Variable resolution using ${prev} syntax
 * - Expectation validation for result checking
 * - Conditional branching with if/else logic
 * - Automatic context_id injection
 *
 * @example
 * ```typescript
 * import { OwlBrowser, RemoteConfig } from '@olib-ai/owl-browser-sdk';
 * import { FlowExecutor } from '@olib-ai/owl-browser-sdk/flow';
 *
 * const browser = new OwlBrowser({ url: '...', token: '...' });
 * await browser.connect();
 *
 * const ctx = await browser.createContext();
 * const executor = new FlowExecutor(browser, ctx.context_id);
 *
 * const flow = FlowExecutor.loadFlow('test-flows/navigation.json');
 * const result = await executor.execute(flow);
 *
 * if (result.success) {
 *   console.log('Flow completed successfully!');
 * } else {
 *   console.error('Flow failed:', result.error);
 * }
 * ```
 */
export class FlowExecutor {
    _client;
    _contextId;
    _abortFlag = false;
    constructor(client, contextId) {
        this._client = client;
        this._contextId = contextId;
    }
    /**
     * Signal to abort the current flow execution.
     */
    abort() {
        this._abortFlag = true;
    }
    /**
     * Reset the abort flag for a new execution.
     */
    reset() {
        this._abortFlag = false;
    }
    /**
     * Execute a flow and return the results.
     */
    async execute(flow) {
        this._abortFlag = false;
        const startTime = performance.now();
        const results = [];
        const enabledSteps = flow.steps.filter(s => s.enabled);
        if (enabledSteps.length === 0) {
            return {
                success: true,
                steps: [],
                totalDurationMs: 0,
            };
        }
        try {
            const [success] = await this._executeSteps(enabledSteps, results, undefined);
            return {
                success,
                steps: results,
                totalDurationMs: performance.now() - startTime,
            };
        }
        catch (e) {
            return {
                success: false,
                steps: results,
                totalDurationMs: performance.now() - startTime,
                error: e instanceof Error ? e.message : String(e),
            };
        }
    }
    async _executeSteps(steps, results, previousResult) {
        let lastResult = previousResult;
        for (let i = 0; i < steps.length; i++) {
            if (this._abortFlag) {
                return [false, lastResult];
            }
            const step = steps[i];
            if (step.type === 'condition' && step.condition) {
                const result = await this._executeConditionStep(step, i, lastResult, results);
                if (!result.success) {
                    return [false, lastResult];
                }
                lastResult = result.result;
            }
            else {
                const result = await this._executeToolStep(step, i, lastResult);
                results.push(result);
                if (!result.success) {
                    return [false, lastResult];
                }
                lastResult = result.result;
            }
            // Small delay between steps
            if (i < steps.length - 1) {
                await sleep(100);
            }
        }
        return [true, lastResult];
    }
    async _executeConditionStep(step, stepIndex, previousResult, results) {
        const startTime = performance.now();
        if (!step.condition) {
            return {
                stepIndex,
                stepId: step.id,
                toolName: 'condition',
                success: false,
                error: 'Condition step missing condition',
                durationMs: performance.now() - startTime,
            };
        }
        const conditionResult = evaluateCondition(step.condition, previousResult);
        const branchTaken = conditionResult ? 'true' : 'false';
        const conditionStepResult = {
            stepIndex,
            stepId: step.id,
            toolName: 'condition',
            success: true,
            result: { condition_result: conditionResult, branch_taken: branchTaken },
            durationMs: performance.now() - startTime,
            branchTaken,
        };
        results.push(conditionStepResult);
        const branchSteps = conditionResult ? step.onTrue : step.onFalse;
        if (branchSteps && branchSteps.length > 0) {
            const enabledBranch = branchSteps.filter(s => s.enabled);
            const [success, lastResult] = await this._executeSteps(enabledBranch, results, previousResult);
            if (!success) {
                return {
                    stepIndex,
                    stepId: step.id,
                    toolName: 'condition',
                    success: false,
                    result: lastResult,
                    error: 'Branch execution failed',
                    durationMs: performance.now() - startTime,
                    branchTaken,
                };
            }
            return {
                stepIndex,
                stepId: step.id,
                toolName: 'condition',
                success: true,
                result: lastResult,
                durationMs: performance.now() - startTime,
                branchTaken,
            };
        }
        return conditionStepResult;
    }
    async _executeToolStep(step, stepIndex, previousResult) {
        const startTime = performance.now();
        let params = resolveVariables(step.params, previousResult);
        params = applyParameterAliases(step.type, params);
        params['context_id'] = this._contextId;
        try {
            const result = await this._client.execute(step.type, params);
            const durationMs = performance.now() - startTime;
            if (step.expected) {
                const expectationResult = checkExpectation(result, step.expected);
                if (!expectationResult.passed) {
                    return {
                        stepIndex,
                        stepId: step.id,
                        toolName: step.type,
                        success: false,
                        result,
                        error: 'Expectation failed: ' + expectationResult.message,
                        durationMs,
                        expectationResult,
                    };
                }
                return {
                    stepIndex,
                    stepId: step.id,
                    toolName: step.type,
                    success: true,
                    result,
                    durationMs,
                    expectationResult,
                };
            }
            return {
                stepIndex,
                stepId: step.id,
                toolName: step.type,
                success: true,
                result,
                durationMs,
            };
        }
        catch (e) {
            return {
                stepIndex,
                stepId: step.id,
                toolName: step.type,
                success: false,
                error: e instanceof Error ? e.message : String(e),
                durationMs: performance.now() - startTime,
            };
        }
    }
    /**
     * Load a flow from a JSON file.
     */
    static loadFlow(path) {
        const content = fs.readFileSync(path, 'utf-8');
        const data = JSON.parse(content);
        return FlowExecutor.parseFlow(data);
    }
    /**
     * Parse flow data from a dictionary.
     */
    static parseFlow(data) {
        const stepsData = (data['steps'] ?? []);
        const steps = stepsData.map(s => FlowExecutor.parseStep(s));
        return {
            name: String(data['name'] ?? 'Unnamed Flow'),
            description: data['description'],
            steps,
        };
    }
    /**
     * Parse a single step from a dictionary.
     */
    static parseStep(data) {
        const stepId = String(data['id'] ?? generateStepId());
        const stepType = String(data['type'] ?? '');
        // Extract params (everything except metadata)
        const params = {};
        const keysToRemove = new Set([
            'type', 'selected', 'enabled', 'expected',
            'condition', 'onTrue', 'onFalse', 'on_true', 'on_false', 'id',
        ]);
        // Only remove 'description' if this tool doesn't use it as a parameter
        if (!TOOLS_WITH_DESCRIPTION_PARAM.has(stepType)) {
            keysToRemove.add('description');
        }
        for (const [key, value] of Object.entries(data)) {
            if (!keysToRemove.has(key)) {
                params[key] = value;
            }
        }
        // Parse expected
        let expected;
        if ('expected' in data && data['expected']) {
            const expData = data['expected'];
            expected = {
                equals: expData['equals'],
                contains: expData['contains'],
                length: expData['length'],
                greaterThan: (expData['greaterThan'] ?? expData['greater_than']),
                lessThan: (expData['lessThan'] ?? expData['less_than']),
                notEmpty: (expData['notEmpty'] ?? expData['not_empty']),
                field: expData['field'],
                matches: expData['matches'],
            };
        }
        // Parse condition
        let condition;
        if ('condition' in data && data['condition']) {
            const condData = data['condition'];
            condition = {
                source: condData['source'] ?? 'previous',
                operator: condData['operator'] ?? ConditionOperator.IS_TRUTHY,
                sourceStepId: (condData['sourceStepId'] ?? condData['source_step_id']),
                field: condData['field'],
                value: condData['value'],
            };
        }
        // Parse branches
        let onTrue;
        let onFalse;
        const onTrueData = (data['onTrue'] ?? data['on_true']);
        if (onTrueData) {
            onTrue = onTrueData.map(s => FlowExecutor.parseStep(s));
        }
        const onFalseData = (data['onFalse'] ?? data['on_false']);
        if (onFalseData) {
            onFalse = onFalseData.map(s => FlowExecutor.parseStep(s));
        }
        const enabled = (data['enabled'] ?? data['selected'] ?? true);
        return {
            id: stepId,
            type: stepType,
            enabled,
            params,
            description: data['description'],
            expected,
            condition,
            onTrue,
            onFalse,
        };
    }
}
//# sourceMappingURL=executor.js.map