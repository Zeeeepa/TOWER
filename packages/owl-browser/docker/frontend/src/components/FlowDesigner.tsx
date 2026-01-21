import { useState, useCallback, useEffect, useRef } from 'react'
import {
  Play,
  Square,
  Trash2,
  Download,
  Upload,
  RotateCcw,
} from 'lucide-react'
import FlowStepForm from './FlowStepForm'
import FlowStepList from './FlowStepList'
import { useFlowExecution } from '../hooks/useFlowExecution'
import type { FlowStep } from '../types/flow'

// Browser flow format types (for import/export compatibility)
interface BrowserFlowStep {
  type: string
  selected?: boolean
  [key: string]: unknown // flat params like url, selector, text, value, etc.
}

interface BrowserFlow {
  name: string
  description?: string
  steps: BrowserFlowStep[]
}

// Type mappings between browser format and internal format
// Browser type -> Internal type (API tool name)
const BROWSER_TO_INTERNAL_TYPE: Record<string, string> = {
  'extract': 'browser_extract_text',
  'query': 'browser_query_page',
  'scroll_up': 'browser_scroll_to_top',
  'scroll_down': 'browser_scroll_to_bottom',
  'record_video': 'browser_start_video_recording',
  'stop_video': 'browser_stop_video_recording',
}

// Internal type -> Browser type (for export)
const INTERNAL_TO_BROWSER_TYPE: Record<string, string> = {
  'browser_extract_text': 'extract',
  'browser_query_page': 'query',
  'browser_scroll_to_top': 'scroll_up',
  'browser_scroll_to_bottom': 'scroll_down',
  'browser_start_video_recording': 'record_video',
  'browser_stop_video_recording': 'stop_video',
}

// Parameter name mappings: browser (camelCase) -> API (snake_case)
const BROWSER_TO_API_PARAMS: Record<string, string> = {
  'startX': 'start_x',
  'startY': 'start_y',
  'endX': 'end_x',
  'endY': 'end_y',
  'midPoints': 'mid_points',
  'sourceSelector': 'source_selector',
  'targetSelector': 'target_selector',
  'frameSelector': 'frame_selector',
  'maxAttempts': 'max_attempts',
  'ignoreCache': 'ignore_cache',
  'idleTime': 'idle_time',
  'cleanLevel': 'clean_level',
  'includeImages': 'include_images',
  'includeLinks': 'include_links',
  'matchType': 'match_type',
  'urlPattern': 'url_pattern',
  'isRegex': 'is_regex',
  'mockBody': 'mock_body',
  'mockStatus': 'mock_status',
  'mockContentType': 'mock_content_type',
  'redirectUrl': 'redirect_url',
  'ruleId': 'rule_id',
  'downloadId': 'download_id',
  'dialogType': 'dialog_type',
  'dialogId': 'dialog_id',
  'promptText': 'prompt_text',
  'responseText': 'response_text',
  'tabId': 'tab_id',
  'cookieName': 'cookie_name',
  'filePaths': 'file_paths',
  'forceRefresh': 'force_refresh',
  'jsFunction': 'js_function',
}

// API (snake_case) -> Browser (camelCase) for export
const API_TO_BROWSER_PARAMS: Record<string, string> = Object.fromEntries(
  Object.entries(BROWSER_TO_API_PARAMS).map(([k, v]) => [v, k])
)

// Convert internal type to browser type (remove "browser_" prefix, apply mappings)
function toExternalType(internalType: string): string {
  // Check for special mappings first
  if (INTERNAL_TO_BROWSER_TYPE[internalType]) {
    return INTERNAL_TO_BROWSER_TYPE[internalType]
  }
  // Default: remove browser_ prefix
  return internalType.replace(/^browser_/, '')
}

// Convert browser type to internal type (add "browser_" prefix, apply mappings)
function toInternalType(externalType: string): string {
  // Check for special mappings first
  if (BROWSER_TO_INTERNAL_TYPE[externalType]) {
    return BROWSER_TO_INTERNAL_TYPE[externalType]
  }
  // If already has browser_ prefix, return as-is
  if (externalType.startsWith('browser_')) {
    return externalType
  }
  return `browser_${externalType}`
}

// Convert parameter name from browser format to API format
function toApiParamName(browserParam: string): string {
  return BROWSER_TO_API_PARAMS[browserParam] || browserParam
}

// Convert parameter name from API format to browser format
function toBrowserParamName(apiParam: string): string {
  return API_TO_BROWSER_PARAMS[apiParam] || apiParam
}

// Convert internal FlowStep to browser format for export
function toExternalStep(step: FlowStep): BrowserFlowStep {
  const externalType = toExternalType(step.type)
  const result: BrowserFlowStep = {
    type: externalType,
    selected: step.enabled,
  }

  // Handle condition steps
  if (step.type === 'condition' && step.condition) {
    result.condition = step.condition
    if (step.onTrue && step.onTrue.length > 0) {
      result.onTrue = step.onTrue.map(toExternalStep)
    }
    if (step.onFalse && step.onFalse.length > 0) {
      result.onFalse = step.onFalse.map(toExternalStep)
    }
  }

  // Flatten params onto the step object with browser naming
  if (step.params) {
    for (const [key, value] of Object.entries(step.params)) {
      if (key !== 'context_id' && value !== undefined && value !== '') {
        const browserKey = toBrowserParamName(key)
        result[browserKey] = value
      }
    }
  }

  return result
}

// Convert browser format step to internal FlowStep for import
function toInternalStep(externalStep: BrowserFlowStep, index: number): FlowStep {
  const internalType = toInternalType(externalStep.type)

  // Extract params from flat structure with API naming
  const params: Record<string, unknown> = {}

  for (const [key, value] of Object.entries(externalStep)) {
    // Skip special fields that are handled separately
    if (['type', 'selected', 'condition', 'onTrue', 'onFalse'].includes(key)) continue

    // Convert parameter name to API format
    const apiKey = toApiParamName(key)
    params[apiKey] = value
  }

  const step: FlowStep = {
    id: `step_${Date.now()}_${index}_${Math.random().toString(36).substr(2, 5)}`,
    type: internalType,
    enabled: externalStep.selected !== false,
    params,
  }

  // Handle condition steps with nested branches
  if (externalStep.condition) {
    step.condition = externalStep.condition as FlowStep['condition']
  }
  if (externalStep.onTrue && Array.isArray(externalStep.onTrue)) {
    step.onTrue = (externalStep.onTrue as BrowserFlowStep[]).map((s, i) => toInternalStep(s, i))
  }
  if (externalStep.onFalse && Array.isArray(externalStep.onFalse)) {
    step.onFalse = (externalStep.onFalse as BrowserFlowStep[]).map((s, i) => toInternalStep(s, i))
  }

  return step
}

export interface FlowExecutionState {
  steps: FlowStep[]
  execution: {
    flowId: string
    status: 'idle' | 'running' | 'completed' | 'error'
    currentStepIndex: number
    startedAt?: number
    completedAt?: number
  }
  isRunning: boolean
  onClearExecution?: () => void
}

interface FlowDesignerProps {
  contextId: string | null
  onStartPicker: (mode: 'element' | 'position', callback: (value: string) => void) => void
  onExecutionStateChange?: (state: FlowExecutionState) => void
  // Lifted state props for persistence across tab switches
  steps: FlowStep[]
  onStepsChange: (steps: FlowStep[]) => void
  flowName: string
  onFlowNameChange: (name: string) => void
}

export default function FlowDesigner({
  contextId,
  onStartPicker,
  onExecutionStateChange,
  steps,
  onStepsChange,
  flowName,
  onFlowNameChange,
}: FlowDesignerProps) {
  const [editingStep, setEditingStep] = useState<FlowStep | null>(null)

  // Helper to update steps through parent - use ref to avoid stale closure
  const stepsRef = useRef(steps)
  stepsRef.current = steps

  const setSteps = useCallback((updater: FlowStep[] | ((prev: FlowStep[]) => FlowStep[])) => {
    if (typeof updater === 'function') {
      onStepsChange(updater(stepsRef.current))
    } else {
      onStepsChange(updater)
    }
  }, [onStepsChange])

  const handleStepStart = useCallback((_stepIndex: number, step: FlowStep) => {
    // stepIndex is index within enabled steps, use step.id to find correct step
    setSteps(prev =>
      prev.map(s => ({
        ...s,
        status: s.id === step.id ? 'running' : s.status,
      }))
    )
  }, [])

  const handleStepComplete = useCallback((_stepIndex: number, result: { stepId: string; status: string; result?: unknown; error?: string; duration?: number; branchTaken?: 'true' | 'false' }) => {
    // Use stepId to find the correct step (stepIndex is relative to enabled steps only)
    // Also handle nested steps in condition branches
    const updateStepRecursively = (steps: FlowStep[]): FlowStep[] => {
      return steps.map(s => {
        if (s.id === result.stepId) {
          return {
            ...s,
            status: result.status as FlowStep['status'],
            result: result.result,
            error: result.error,
            duration: result.duration,
            branchTaken: result.branchTaken,
          }
        }
        // Check nested branches for condition steps
        if (s.type === 'condition') {
          return {
            ...s,
            onTrue: s.onTrue ? updateStepRecursively(s.onTrue) : s.onTrue,
            onFalse: s.onFalse ? updateStepRecursively(s.onFalse) : s.onFalse,
          }
        }
        return s
      })
    }
    setSteps(prev => updateStepRecursively(prev))
  }, [])

  const { execution, executeFlow, abortFlow, resetExecution, isRunning } = useFlowExecution({
    contextId,
    onStepStart: handleStepStart,
    onStepComplete: handleStepComplete,
  })

  // Reset/clear execution state and step results (recursively for nested steps)
  const handleReset = useCallback(() => {
    const resetStepRecursively = (steps: FlowStep[]): FlowStep[] => {
      return steps.map(s => ({
        ...s,
        status: undefined,
        result: undefined,
        error: undefined,
        duration: undefined,
        branchTaken: undefined,
        onTrue: s.onTrue ? resetStepRecursively(s.onTrue) : s.onTrue,
        onFalse: s.onFalse ? resetStepRecursively(s.onFalse) : s.onFalse,
      }))
    }
    setSteps(prev => resetStepRecursively(prev))
    resetExecution()
  }, [resetExecution])

  // Report execution state changes to parent
  useEffect(() => {
    onExecutionStateChange?.({
      steps,
      execution: {
        flowId: execution.flowId,
        status: execution.status,
        currentStepIndex: execution.currentStepIndex,
        startedAt: execution.startedAt,
        completedAt: execution.completedAt,
      },
      isRunning,
      onClearExecution: handleReset,
    })
  }, [steps, execution, isRunning, onExecutionStateChange, handleReset])

  const handleEditStep = useCallback((step: FlowStep) => {
    setEditingStep(step)
  }, [])

  const handleCancelEdit = useCallback(() => {
    setEditingStep(null)
  }, [])

  const handleReorder = useCallback((newSteps: FlowStep[]) => {
    setSteps(newSteps)
  }, [])

  const handleToggle = useCallback((stepId: string) => {
    setSteps(prev =>
      prev.map(s => (s.id === stepId ? { ...s, enabled: !s.enabled } : s))
    )
  }, [])

  const handleDelete = useCallback((stepId: string) => {
    setSteps(prev => prev.filter(s => s.id !== stepId))
  }, [])

  // State for adding to a specific branch
  const [addingToBranch, setAddingToBranch] = useState<{ parentId: string; branch: 'onTrue' | 'onFalse' } | null>(null)

  // Add step to a condition branch
  const handleAddToBranch = useCallback((parentStepId: string, branch: 'onTrue' | 'onFalse') => {
    setAddingToBranch({ parentId: parentStepId, branch })
    setEditingStep(null) // Clear any editing state
  }, [])

  // Update branch steps
  const handleUpdateBranch = useCallback((parentStepId: string, branch: 'onTrue' | 'onFalse', branchSteps: FlowStep[]) => {
    const updateRecursively = (steps: FlowStep[]): FlowStep[] => {
      return steps.map(s => {
        if (s.id === parentStepId) {
          return { ...s, [branch]: branchSteps }
        }
        if (s.type === 'condition') {
          return {
            ...s,
            onTrue: s.onTrue ? updateRecursively(s.onTrue) : s.onTrue,
            onFalse: s.onFalse ? updateRecursively(s.onFalse) : s.onFalse,
          }
        }
        return s
      })
    }
    setSteps(prev => updateRecursively(prev))
  }, [])

  // Delete step from a condition branch
  const handleDeleteFromBranch = useCallback((parentStepId: string, branch: 'onTrue' | 'onFalse', stepId: string) => {
    const updateRecursively = (steps: FlowStep[]): FlowStep[] => {
      return steps.map(s => {
        if (s.id === parentStepId) {
          const branchSteps = s[branch] as FlowStep[] | undefined
          return { ...s, [branch]: branchSteps?.filter(bs => bs.id !== stepId) || [] }
        }
        if (s.type === 'condition') {
          return {
            ...s,
            onTrue: s.onTrue ? updateRecursively(s.onTrue) : s.onTrue,
            onFalse: s.onFalse ? updateRecursively(s.onFalse) : s.onFalse,
          }
        }
        return s
      })
    }
    setSteps(prev => updateRecursively(prev))
  }, [])

  // Modified handleAddStep to support adding to branches
  const handleAddStepWithBranch = useCallback((step: FlowStep) => {
    if (addingToBranch) {
      // Add to the specific branch
      const { parentId, branch } = addingToBranch
      const updateRecursively = (steps: FlowStep[]): FlowStep[] => {
        return steps.map(s => {
          if (s.id === parentId) {
            const branchSteps = (s[branch] as FlowStep[] | undefined) || []
            return { ...s, [branch]: [...branchSteps, step] }
          }
          if (s.type === 'condition') {
            return {
              ...s,
              onTrue: s.onTrue ? updateRecursively(s.onTrue) : s.onTrue,
              onFalse: s.onFalse ? updateRecursively(s.onFalse) : s.onFalse,
            }
          }
          return s
        })
      }
      setSteps(prev => updateRecursively(prev))
      setAddingToBranch(null) // Clear branch state
    } else if (editingStep) {
      // Update existing step
      setSteps(prev => prev.map(s => s.id === editingStep.id ? { ...step, id: editingStep.id } : s))
      setEditingStep(null)
    } else {
      // Add new step to main list
      setSteps(prev => [...prev, step])
    }
  }, [editingStep, addingToBranch])

  const handleClearAll = useCallback(() => {
    if (confirm('Clear all steps?')) {
      setSteps([])
      resetExecution()
    }
  }, [resetExecution])

  const handleRun = useCallback(() => {
    // Clear previous execution results
    resetExecution()
    // Reset step statuses before running
    const currentSteps = stepsRef.current
    const resetSteps = currentSteps.map(s => ({
      ...s,
      status: undefined,
      result: undefined,
      error: undefined,
      duration: undefined,
    }))
    onStepsChange(resetSteps)
    // Execute with the reset steps
    executeFlow(resetSteps)
  }, [executeFlow, resetExecution, onStepsChange])

  const handleExport = useCallback(() => {
    // Export in browser-compatible format
    const flow: BrowserFlow = {
      name: flowName,
      description: 'Flow created with Owl Browser Control Panel',
      steps: steps.map(toExternalStep),
    }

    const blob = new Blob([JSON.stringify(flow, null, 2)], { type: 'application/json' })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = `${flowName.toLowerCase().replace(/\s+/g, '-')}.json`
    a.click()
    URL.revokeObjectURL(url)
  }, [flowName, steps])

  const handleImport = useCallback(() => {
    const input = document.createElement('input')
    input.type = 'file'
    input.accept = '.json'
    input.onchange = async (e) => {
      const file = (e.target as HTMLInputElement).files?.[0]
      if (!file) return

      try {
        const text = await file.text()
        const flow = JSON.parse(text) as BrowserFlow

        if (flow.name) onFlowNameChange(flow.name)
        if (flow.steps && Array.isArray(flow.steps)) {
          // Convert browser format to internal format
          const importedSteps: FlowStep[] = flow.steps.map((step, index) =>
            toInternalStep(step as BrowserFlowStep, index)
          )
          setSteps(importedSteps)
        }
      } catch (err) {
        alert('Failed to import flow: ' + (err instanceof Error ? err.message : 'Unknown error'))
      }
    }
    input.click()
  }, [])

  const enabledCount = steps.filter(s => s.enabled).length

  return (
    <div className="flex flex-col h-full">
      {/* Header */}
      <div className="p-3 border-b border-white/10">
        <div className="flex items-center gap-2 mb-2">
          <input
            type="text"
            value={flowName}
            onChange={(e) => onFlowNameChange(e.target.value)}
            className="input-field text-sm py-1 flex-1"
            placeholder="Flow name"
          />
        </div>

        {/* Action Buttons */}
        <div className="flex gap-1.5 flex-wrap">
          {!isRunning ? (
            <button
              onClick={handleRun}
              disabled={!contextId || enabledCount === 0}
              className="btn-primary text-xs py-1 px-2 flex items-center gap-1"
            >
              <Play className="w-3.5 h-3.5" />
              Run ({enabledCount})
            </button>
          ) : (
            <button
              onClick={abortFlow}
              className="bg-red-500 hover:bg-red-600 text-white text-xs py-1 px-2 rounded flex items-center gap-1"
            >
              <Square className="w-3.5 h-3.5" />
              Stop
            </button>
          )}

          <button
            onClick={handleReset}
            disabled={isRunning}
            className="btn-secondary text-xs py-1 px-2 flex items-center gap-1"
            title="Reset results"
          >
            <RotateCcw className="w-3.5 h-3.5" />
          </button>

          <button
            onClick={handleClearAll}
            disabled={isRunning || steps.length === 0}
            className="btn-secondary text-xs py-1 px-2 flex items-center gap-1"
            title="Clear all steps"
          >
            <Trash2 className="w-3.5 h-3.5" />
          </button>

          <div className="flex-1" />

          <button
            onClick={handleImport}
            disabled={isRunning}
            className="btn-secondary text-xs py-1 px-2 flex items-center gap-1"
            title="Import flow"
          >
            <Upload className="w-3.5 h-3.5" />
          </button>

          <button
            onClick={handleExport}
            disabled={steps.length === 0}
            className="btn-secondary text-xs py-1 px-2 flex items-center gap-1"
            title="Export flow"
          >
            <Download className="w-3.5 h-3.5" />
          </button>
        </div>
      </div>

      {/* Content */}
      <div className="flex-1 overflow-y-auto p-3 space-y-3">
        {/* Add/Edit Step Form */}
        <div>
          {addingToBranch && (
            <div className="flex items-center gap-2 mb-2 text-xs text-primary">
              <span>Adding to {addingToBranch.branch === 'onTrue' ? 'Then' : 'Else'} branch</span>
              <button
                onClick={() => setAddingToBranch(null)}
                className="text-text-muted hover:text-primary"
              >
                ✕
              </button>
            </div>
          )}
          <FlowStepForm
            onAddStep={handleAddStepWithBranch}
            onStartPicker={onStartPicker}
            contextId={contextId}
            editingStep={editingStep}
            onCancelEdit={() => {
              handleCancelEdit()
              setAddingToBranch(null)
            }}
          />
        </div>

        {/* Steps List */}
        <div>
          <h4 className="text-xs text-text-muted mb-2">
            Steps ({steps.length})
          </h4>
          <FlowStepList
            steps={steps}
            onReorder={handleReorder}
            onToggle={handleToggle}
            onDelete={handleDelete}
            onEdit={handleEditStep}
            onAddToBranch={handleAddToBranch}
            onUpdateBranch={handleUpdateBranch}
            onDeleteFromBranch={handleDeleteFromBranch}
            currentStepIndex={execution.currentStepIndex}
            isRunning={isRunning}
          />
        </div>
      </div>

      {/* Footer - No context warning */}
      {!contextId && (
        <div className="p-3 border-t border-white/10 bg-yellow-500/10">
          <p className="text-xs text-yellow-400 text-center">
            Create a browser context to run flows
          </p>
        </div>
      )}
    </div>
  )
}
