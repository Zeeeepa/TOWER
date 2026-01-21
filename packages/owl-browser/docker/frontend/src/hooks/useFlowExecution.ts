import { useState, useCallback, useRef } from 'react'
import { executeTool } from '../api/browserApi'
import { evaluateCondition } from '../types/flow'
import type { FlowStep, FlowExecution, FlowStepResult } from '../types/flow'

interface UseFlowExecutionProps {
  contextId: string | null
  onStepStart?: (stepIndex: number, step: FlowStep) => void
  onStepComplete?: (stepIndex: number, result: FlowStepResult) => void
  onFlowComplete?: (results: FlowStepResult[]) => void
}

export function useFlowExecution({
  contextId,
  onStepStart,
  onStepComplete,
  onFlowComplete,
}: UseFlowExecutionProps) {
  const [execution, setExecution] = useState<FlowExecution>({
    flowId: '',
    status: 'idle',
    currentStepIndex: -1,
    results: [],
  })

  const abortRef = useRef(false)

  const executeStep = useCallback(
    async (step: FlowStep, stepIndex: number): Promise<FlowStepResult> => {
      const startTime = Date.now()

      // Add context_id to params if not present
      const params = {
        ...step.params,
        context_id: contextId,
      }

      try {
        const result = await executeTool(step.type, params)

        // Check if API call succeeded
        if (result.success) {
          // Check if the action result itself indicates failure
          // ActionResult format: { success: false, status: "...", message: "..." }
          const actionResult = result.result
          const isActionFailure =
            actionResult !== null &&
            typeof actionResult === 'object' &&
            'success' in actionResult &&
            (actionResult as { success: boolean }).success === false

          return {
            stepId: step.id,
            stepIndex,
            status: isActionFailure ? 'warning' : 'success',
            result: result.result,
            error: isActionFailure && actionResult && typeof actionResult === 'object' && 'message' in actionResult
              ? String((actionResult as unknown as { message: string }).message)
              : undefined,
            duration: Date.now() - startTime,
            timestamp: Date.now(),
          }
        }

        return {
          stepId: step.id,
          stepIndex,
          status: 'error',
          result: undefined,
          error: result.error,
          duration: Date.now() - startTime,
          timestamp: Date.now(),
        }
      } catch (error) {
        return {
          stepId: step.id,
          stepIndex,
          status: 'error',
          error: error instanceof Error ? error.message : 'Unknown error',
          duration: Date.now() - startTime,
          timestamp: Date.now(),
        }
      }
    },
    [contextId]
  )

  // Execute a list of steps recursively (handles nested branches)
  const executeStepList = useCallback(
    async (
      steps: FlowStep[],
      results: FlowStepResult[],
      previousResult: unknown,
      onStepStartInternal: (step: FlowStep) => void,
      onStepCompleteInternal: (result: FlowStepResult, branchTaken?: 'true' | 'false') => void
    ): Promise<{ success: boolean; lastResult: unknown }> => {
      const enabledSteps = steps.filter(s => s.enabled)
      let lastResult = previousResult

      for (let i = 0; i < enabledSteps.length; i++) {
        if (abortRef.current) {
          return { success: false, lastResult }
        }

        const step = enabledSteps[i]
        onStepStartInternal(step)

        // Handle condition steps
        if (step.type === 'condition' && step.condition) {
          const startTime = Date.now()
          const conditionResult = evaluateCondition(step.condition, lastResult)
          const branchTaken: 'true' | 'false' = conditionResult ? 'true' : 'false'

          // Create result for the condition step itself
          const conditionStepResult: FlowStepResult = {
            stepId: step.id,
            stepIndex: i,
            status: 'success',
            result: { conditionResult, branchTaken },
            duration: Date.now() - startTime,
            timestamp: Date.now(),
          }
          results.push(conditionStepResult)
          onStepCompleteInternal(conditionStepResult, branchTaken)

          // Execute the appropriate branch
          const branchSteps = conditionResult ? step.onTrue : step.onFalse
          if (branchSteps && branchSteps.length > 0) {
            const branchResult = await executeStepList(
              branchSteps,
              results,
              lastResult,
              onStepStartInternal,
              onStepCompleteInternal
            )
            if (!branchResult.success) {
              return branchResult
            }
            lastResult = branchResult.lastResult
          }
        } else {
          // Regular step execution
          const result = await executeStep(step, i)
          results.push(result)
          onStepCompleteInternal(result)

          if (result.status === 'error') {
            return { success: false, lastResult }
          }

          lastResult = result.result
        }

        // Small delay between steps for stability
        if (i < enabledSteps.length - 1) {
          await new Promise(resolve => setTimeout(resolve, 300))
        }
      }

      return { success: true, lastResult }
    },
    [executeStep]
  )

  const executeFlow = useCallback(
    async (steps: FlowStep[], flowId: string = 'flow_' + Date.now()) => {
      if (!contextId) {
        console.error('No context ID available')
        return
      }

      // Filter enabled steps
      const enabledSteps = steps.filter(s => s.enabled)
      if (enabledSteps.length === 0) {
        console.error('No enabled steps to execute')
        return
      }

      abortRef.current = false
      const results: FlowStepResult[] = []
      let currentIndex = 0

      setExecution({
        flowId,
        status: 'running',
        currentStepIndex: 0,
        startedAt: Date.now(),
        results: [],
      })

      const onStepStartInternal = (step: FlowStep) => {
        setExecution(prev => ({
          ...prev,
          currentStepIndex: currentIndex,
        }))
        onStepStart?.(currentIndex, step)
        currentIndex++
      }

      const onStepCompleteInternal = (result: FlowStepResult, branchTaken?: 'true' | 'false') => {
        setExecution(prev => ({
          ...prev,
          results: [...prev.results, result],
        }))
        // Include branchTaken in the result callback if present
        const enrichedResult = branchTaken ? { ...result, branchTaken } : result
        onStepComplete?.(result.stepIndex, enrichedResult as FlowStepResult)
      }

      const { success } = await executeStepList(
        steps,
        results,
        null,
        onStepStartInternal,
        onStepCompleteInternal
      )

      if (abortRef.current) {
        setExecution(prev => ({
          ...prev,
          status: 'error',
          completedAt: Date.now(),
        }))
        return
      }

      setExecution(prev => ({
        ...prev,
        status: success ? 'completed' : 'error',
        completedAt: Date.now(),
      }))

      onFlowComplete?.(results)
    },
    [contextId, executeStepList, onStepStart, onStepComplete, onFlowComplete]
  )

  const abortFlow = useCallback(() => {
    abortRef.current = true
  }, [])

  const resetExecution = useCallback(() => {
    abortRef.current = false
    setExecution({
      flowId: '',
      status: 'idle',
      currentStepIndex: -1,
      results: [],
    })
  }, [])

  return {
    execution,
    executeFlow,
    abortFlow,
    resetExecution,
    isRunning: execution.status === 'running',
  }
}
