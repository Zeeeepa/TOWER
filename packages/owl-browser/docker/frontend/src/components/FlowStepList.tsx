import { useState, useCallback } from 'react'
import {
  GripVertical,
  Trash2,
  Pencil,
  CheckCircle,
  XCircle,
  Loader2,
  ChevronDown,
  ChevronRight,
  Clock,
  GitBranch,
  CornerDownRight,
  Plus,
} from 'lucide-react'
import { getStepInfo, CONDITION_OPERATORS } from '../types/flow'
import type { FlowStep, FlowCondition } from '../types/flow'

interface FlowStepListProps {
  steps: FlowStep[]
  onReorder: (steps: FlowStep[]) => void
  onToggle: (stepId: string) => void
  onDelete: (stepId: string) => void
  onEdit?: (step: FlowStep) => void
  onAddToBranch?: (parentStepId: string, branch: 'onTrue' | 'onFalse') => void
  onUpdateBranch?: (parentStepId: string, branch: 'onTrue' | 'onFalse', steps: FlowStep[]) => void
  onDeleteFromBranch?: (parentStepId: string, branch: 'onTrue' | 'onFalse', stepId: string) => void
  currentStepIndex: number
  isRunning: boolean
  depth?: number // For nested rendering
}

// Helper to format condition for display
function formatCondition(condition: FlowCondition): string {
  const operator = CONDITION_OPERATORS.find(op => op.value === condition.operator)
  const opLabel = operator?.label || condition.operator
  const source = condition.source === 'previous' ? 'Previous result' : `Step result`
  const field = condition.field ? `.${condition.field}` : ''
  const value = condition.value !== undefined ? ` "${condition.value}"` : ''
  return `${source}${field} ${opLabel}${value}`
}

export default function FlowStepList({
  steps,
  onReorder,
  onToggle,
  onDelete,
  onEdit,
  onAddToBranch,
  onUpdateBranch,
  onDeleteFromBranch,
  currentStepIndex,
  isRunning,
  depth = 0,
}: FlowStepListProps) {
  const [dragIndex, setDragIndex] = useState<number | null>(null)
  const [dragOverIndex, setDragOverIndex] = useState<number | null>(null)
  const [expandedSteps, setExpandedSteps] = useState<Set<string>>(new Set())

  const handleDragStart = useCallback((e: React.DragEvent, index: number) => {
    setDragIndex(index)
    e.dataTransfer.effectAllowed = 'move'
  }, [])

  const handleDragOver = useCallback((e: React.DragEvent, index: number) => {
    e.preventDefault()
    e.dataTransfer.dropEffect = 'move'
    setDragOverIndex(index)
  }, [])

  const handleDragEnd = useCallback(() => {
    if (dragIndex !== null && dragOverIndex !== null && dragIndex !== dragOverIndex) {
      const newSteps = [...steps]
      const [removed] = newSteps.splice(dragIndex, 1)
      newSteps.splice(dragOverIndex, 0, removed)
      onReorder(newSteps)
    }
    setDragIndex(null)
    setDragOverIndex(null)
  }, [dragIndex, dragOverIndex, steps, onReorder])

  const toggleExpanded = useCallback((stepId: string) => {
    setExpandedSteps(prev => {
      const next = new Set(prev)
      if (next.has(stepId)) {
        next.delete(stepId)
      } else {
        next.add(stepId)
      }
      return next
    })
  }, [])

  const getStepStatusIcon = (step: FlowStep, index: number) => {
    if (isRunning && index === currentStepIndex) {
      return <Loader2 className="w-4 h-4 text-primary animate-spin" />
    }
    if (step.status === 'success') {
      return <CheckCircle className="w-4 h-4 text-green-400" />
    }
    if (step.status === 'error') {
      return <XCircle className="w-4 h-4 text-red-400" />
    }
    return null
  }

  const formatParamValue = (value: unknown): string => {
    if (typeof value === 'string') {
      return value.length > 30 ? value.substring(0, 30) + '...' : value
    }
    return String(value)
  }

  const formatDuration = (ms: number): string => {
    if (ms < 1000) return `${ms}ms`
    if (ms < 60000) return `${(ms / 1000).toFixed(1)}s`
    const mins = Math.floor(ms / 60000)
    const secs = Math.round((ms % 60000) / 1000)
    return `${mins}m ${secs}s`
  }

  if (steps.length === 0) {
    return (
      <div className="text-center py-8 text-text-muted text-sm">
        No steps added yet. Add steps using the form above.
      </div>
    )
  }

  return (
    <div className="space-y-1.5">
      {steps.map((step, index) => {
        const stepInfo = getStepInfo(step.type)
        const isExpanded = expandedSteps.has(step.id)
        const isDragging = index === dragIndex
        const isDragOver = index === dragOverIndex

        return (
          <div
            key={step.id}
            draggable={!isRunning}
            onDragStart={(e) => handleDragStart(e, index)}
            onDragOver={(e) => handleDragOver(e, index)}
            onDragEnd={handleDragEnd}
            className={`
              border rounded-lg transition-all
              ${isDragging ? 'opacity-50' : ''}
              ${isDragOver ? 'border-primary' : 'border-white/10'}
              ${!step.enabled ? 'opacity-50' : ''}
              ${step.status === 'error' ? 'border-red-400/50 bg-red-400/5' : ''}
              ${step.status === 'success' ? 'border-green-400/50 bg-green-400/5' : ''}
              ${isRunning && index === currentStepIndex ? 'border-primary bg-primary/10' : ''}
            `}
          >
            {/* Step Header */}
            <div className="flex items-center gap-2 p-2">
              {/* Drag Handle */}
              <div
                className="cursor-grab text-text-muted hover:text-text-primary flex-shrink-0"
                onMouseDown={(e) => e.stopPropagation()}
              >
                <GripVertical className="w-4 h-4" />
              </div>

              {/* Checkbox */}
              <input
                type="checkbox"
                checked={step.enabled}
                onChange={() => onToggle(step.id)}
                disabled={isRunning}
                className="w-3.5 h-3.5 rounded border-white/20 bg-bg-tertiary text-primary flex-shrink-0"
              />

              {/* Step Number */}
              <span className="text-xs text-text-muted w-5 flex-shrink-0">{index + 1}.</span>

              {/* Step Info - takes remaining space with overflow hidden */}
              <button
                onClick={() => toggleExpanded(step.id)}
                className="flex-1 flex items-center gap-2 text-left min-w-0 overflow-hidden"
              >
                <span className="flex-shrink-0">
                  {isExpanded ? (
                    <ChevronDown className="w-3.5 h-3.5 text-text-muted" />
                  ) : (
                    <ChevronRight className="w-3.5 h-3.5 text-text-muted" />
                  )}
                </span>
                {step.type === 'condition' ? (
                  <GitBranch className="w-3.5 h-3.5 text-primary flex-shrink-0" />
                ) : null}
                <span className="text-sm text-text-primary flex-shrink-0">
                  {stepInfo?.label || step.type}
                </span>
                {/* Show condition preview or primary param preview */}
                {step.type === 'condition' && step.condition && !isExpanded ? (
                  <span className="text-xs text-text-muted truncate">
                    {formatCondition(step.condition)}
                  </span>
                ) : Object.keys(step.params).length > 0 && !isExpanded && (
                  <span className="text-xs text-text-muted truncate">
                    {formatParamValue(Object.values(step.params)[0])}
                  </span>
                )}
              </button>

              {/* Duration */}
              {step.duration !== undefined && (
                <span className="flex items-center gap-1 text-xs text-text-muted flex-shrink-0">
                  <Clock className="w-3 h-3" />
                  {formatDuration(step.duration)}
                </span>
              )}

              {/* Status Icon */}
              <span className="flex-shrink-0">
                {getStepStatusIcon(step, index)}
              </span>

              {/* Edit Button */}
              {onEdit && (
                <button
                  onClick={() => onEdit(step)}
                  disabled={isRunning}
                  className="text-text-muted hover:text-primary disabled:opacity-50 flex-shrink-0"
                  title="Edit step"
                >
                  <Pencil className="w-4 h-4" />
                </button>
              )}

              {/* Delete Button */}
              <button
                onClick={() => onDelete(step.id)}
                disabled={isRunning}
                className="text-text-muted hover:text-red-400 disabled:opacity-50 flex-shrink-0"
                title="Delete step"
              >
                <Trash2 className="w-4 h-4" />
              </button>
            </div>

            {/* Expanded Parameters */}
            {isExpanded && Object.keys(step.params).length > 0 && (
              <div className="px-3 pb-2 pt-0 border-t border-white/5">
                <div className="space-y-1 mt-2">
                  {Object.entries(step.params).map(([key, value]) => (
                    <div key={key} className="flex gap-2 text-xs">
                      <span className="text-text-muted">{key}:</span>
                      <span className="text-text-secondary break-all">
                        {typeof value === 'object'
                          ? JSON.stringify(value)
                          : String(value ?? '')}
                      </span>
                    </div>
                  ))}
                </div>
              </div>
            )}

            {/* Error Message */}
            {step.error && (
              <div className="px-3 pb-2 pt-0">
                <p className="text-xs text-red-400">{step.error}</p>
              </div>
            )}

            {/* Result Preview */}
            {step.result !== undefined && step.status === 'success' && isExpanded && (
              <div className="px-3 pb-2 pt-0 border-t border-white/5">
                <p className="text-xs text-text-muted mt-2">Result:</p>
                <pre className="text-xs text-green-400 mt-1 overflow-auto max-h-20 bg-bg-primary/50 p-1.5 rounded">
                  {typeof step.result === 'object'
                    ? JSON.stringify(step.result, null, 2)
                    : String(step.result ?? '')}
                </pre>
              </div>
            )}

            {/* Condition Branches */}
            {step.type === 'condition' && isExpanded && (
              <div className="px-3 pb-3 pt-0 border-t border-white/5 mt-2">
                {/* Condition Expression */}
                {step.condition && (
                  <div className="flex items-center gap-2 mb-3 mt-2">
                    <GitBranch className="w-4 h-4 text-primary flex-shrink-0" />
                    <span className="text-xs text-text-secondary">
                      If {formatCondition(step.condition)}
                    </span>
                  </div>
                )}

                {/* True Branch */}
                <div className="mb-3">
                  <div className="flex items-center gap-2 mb-1.5">
                    <div className="flex items-center gap-1 text-green-400">
                      <CornerDownRight className="w-3 h-3" />
                      <span className="text-xs font-medium">Then</span>
                    </div>
                    {step.branchTaken === 'true' && (
                      <span className="text-xs bg-green-500/20 text-green-400 px-1.5 py-0.5 rounded">executed</span>
                    )}
                    {onAddToBranch && (
                      <button
                        onClick={() => onAddToBranch(step.id, 'onTrue')}
                        disabled={isRunning}
                        className="ml-auto text-xs text-text-muted hover:text-primary flex items-center gap-1"
                      >
                        <Plus className="w-3 h-3" />
                      </button>
                    )}
                  </div>
                  <div className={`ml-4 pl-2 border-l-2 ${step.branchTaken === 'true' ? 'border-green-500/50' : 'border-white/10'}`}>
                    {step.onTrue && step.onTrue.length > 0 ? (
                      <FlowStepList
                        steps={step.onTrue}
                        onReorder={(newSteps) => onUpdateBranch?.(step.id, 'onTrue', newSteps)}
                        onToggle={(id) => {
                          const updated = step.onTrue?.map(s => s.id === id ? { ...s, enabled: !s.enabled } : s) || []
                          onUpdateBranch?.(step.id, 'onTrue', updated)
                        }}
                        onDelete={(id) => onDeleteFromBranch?.(step.id, 'onTrue', id)}
                        onEdit={onEdit}
                        onAddToBranch={onAddToBranch}
                        onUpdateBranch={onUpdateBranch}
                        onDeleteFromBranch={onDeleteFromBranch}
                        currentStepIndex={-1}
                        isRunning={isRunning}
                        depth={depth + 1}
                      />
                    ) : (
                      <p className="text-xs text-text-muted py-2">No steps</p>
                    )}
                  </div>
                </div>

                {/* False Branch */}
                <div>
                  <div className="flex items-center gap-2 mb-1.5">
                    <div className="flex items-center gap-1 text-red-400">
                      <CornerDownRight className="w-3 h-3" />
                      <span className="text-xs font-medium">Else</span>
                    </div>
                    {step.branchTaken === 'false' && (
                      <span className="text-xs bg-red-500/20 text-red-400 px-1.5 py-0.5 rounded">executed</span>
                    )}
                    {onAddToBranch && (
                      <button
                        onClick={() => onAddToBranch(step.id, 'onFalse')}
                        disabled={isRunning}
                        className="ml-auto text-xs text-text-muted hover:text-primary flex items-center gap-1"
                      >
                        <Plus className="w-3 h-3" />
                      </button>
                    )}
                  </div>
                  <div className={`ml-4 pl-2 border-l-2 ${step.branchTaken === 'false' ? 'border-red-500/50' : 'border-white/10'}`}>
                    {step.onFalse && step.onFalse.length > 0 ? (
                      <FlowStepList
                        steps={step.onFalse}
                        onReorder={(newSteps) => onUpdateBranch?.(step.id, 'onFalse', newSteps)}
                        onToggle={(id) => {
                          const updated = step.onFalse?.map(s => s.id === id ? { ...s, enabled: !s.enabled } : s) || []
                          onUpdateBranch?.(step.id, 'onFalse', updated)
                        }}
                        onDelete={(id) => onDeleteFromBranch?.(step.id, 'onFalse', id)}
                        onEdit={onEdit}
                        onAddToBranch={onAddToBranch}
                        onUpdateBranch={onUpdateBranch}
                        onDeleteFromBranch={onDeleteFromBranch}
                        currentStepIndex={-1}
                        isRunning={isRunning}
                        depth={depth + 1}
                      />
                    ) : (
                      <p className="text-xs text-text-muted py-2">No steps</p>
                    )}
                  </div>
                </div>
              </div>
            )}
          </div>
        )
      })}
    </div>
  )
}
