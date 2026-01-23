/**
 * Flow execution engine for Owl Browser SDK v2.
 *
 * Provides the FlowExecutor class that executes flows with variable resolution,
 * expectation validation, and conditional branching support.
 */
import type { Flow, FlowStep, FlowResult } from '../types.js';
import type { OwlBrowser } from '../client.js';
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
export declare class FlowExecutor {
    private readonly _client;
    private readonly _contextId;
    private _abortFlag;
    constructor(client: OwlBrowser, contextId: string);
    /**
     * Signal to abort the current flow execution.
     */
    abort(): void;
    /**
     * Reset the abort flag for a new execution.
     */
    reset(): void;
    /**
     * Execute a flow and return the results.
     */
    execute(flow: Flow): Promise<FlowResult>;
    private _executeSteps;
    private _executeConditionStep;
    private _executeToolStep;
    /**
     * Load a flow from a JSON file.
     */
    static loadFlow(path: string): Flow;
    /**
     * Parse flow data from a dictionary.
     */
    static parseFlow(data: Record<string, unknown>): Flow;
    /**
     * Parse a single step from a dictionary.
     */
    static parseStep(data: Record<string, unknown>): FlowStep;
}
//# sourceMappingURL=executor.d.ts.map