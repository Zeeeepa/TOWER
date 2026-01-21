import { useState, useEffect } from 'react'
import {
  CheckCircle,
  XCircle,
  AlertTriangle,
  Clock,
  Loader2,
  ChevronDown,
  ChevronRight,
  Timer,
  Activity,
  Copy,
  Check,
  Image,
  Trash2,
} from 'lucide-react'
import type { FlowStep } from '../types/flow'
import { getStepInfo } from '../types/flow'

interface ExecutionState {
  flowId: string
  status: 'idle' | 'running' | 'completed' | 'error'
  currentStepIndex: number
  startedAt?: number
  completedAt?: number
}

interface FlowExecutionPanelProps {
  steps: FlowStep[]
  execution: ExecutionState
  isRunning: boolean
  onClear?: () => void
}

function formatDuration(ms: number): string {
  if (ms < 1000) return `${ms}ms`
  if (ms < 60000) return `${(ms / 1000).toFixed(1)}s`
  const mins = Math.floor(ms / 60000)
  const secs = Math.round((ms % 60000) / 1000)
  return `${mins}m ${secs}s`
}

export default function FlowExecutionPanel({
  steps,
  execution,
  isRunning,
  onClear,
}: FlowExecutionPanelProps) {
  const [expandedStep, setExpandedStep] = useState<string | null>(null)
  const [elapsedTime, setElapsedTime] = useState<number>(0)
  const [copiedId, setCopiedId] = useState<string | null>(null)

  // Update elapsed time while flow is running
  useEffect(() => {
    if (!isRunning || !execution.startedAt) {
      return
    }

    const interval = setInterval(() => {
      setElapsedTime(Date.now() - execution.startedAt!)
    }, 100)

    return () => clearInterval(interval)
  }, [isRunning, execution.startedAt])

  const totalDuration =
    execution.completedAt && execution.startedAt
      ? execution.completedAt - execution.startedAt
      : isRunning
      ? elapsedTime
      : 0

  const copyToClipboard = async (text: string, id: string) => {
    try {
      await navigator.clipboard.writeText(text)
      setCopiedId(id)
      setTimeout(() => setCopiedId(null), 2000)
    } catch {
      console.error('Failed to copy')
    }
  }

  const isBase64Image = (result: unknown): boolean => {
    if (typeof result !== 'string') return false
    return result.length > 1000 && /^[A-Za-z0-9+/=]+$/.test(result)
  }

  const formatResult = (result: unknown): string => {
    if (result === undefined || result === null) return 'null'
    if (typeof result === 'string') {
      if (isBase64Image(result)) return '[Base64 Image Data]'
      return result
    }
    return JSON.stringify(result, null, 2)
  }

  const enabledSteps = steps.filter((s) => s.enabled)

  // Detect if we just started a new run (running but at beginning)
  const justStarted = isRunning && execution.currentStepIndex <= 0

  // Calculate stats - reset to 0 when just started to avoid showing stale data
  const completedCount = justStarted ? 0 : enabledSteps.filter((s) => s.status === 'success').length
  const warningCount = justStarted ? 0 : enabledSteps.filter((s) => s.status === 'warning').length
  const errorCount = justStarted ? 0 : enabledSteps.filter((s) => s.status === 'error').length
  const executedSteps = justStarted ? [] : enabledSteps.filter((s) => s.status === 'success' || s.status === 'warning' || s.status === 'error')
  const progressPercent =
    enabledSteps.length > 0 ? (executedSteps.length / enabledSteps.length) * 100 : 0

  // Show idle state if no execution has started
  if (execution.status === 'idle' && executedSteps.length === 0) {
    return (
      <div className="h-full flex items-center justify-center text-text-muted">
        <div className="text-center">
          <Activity className="w-8 h-8 mx-auto mb-2 opacity-50" />
          <p className="text-sm">Flow execution details will appear here</p>
          <p className="text-xs mt-1 opacity-75">Add steps and run the flow to see results</p>
        </div>
      </div>
    )
  }

  return (
    <div className="h-full flex flex-col">
      {/* Header with progress */}
      <div className="px-4 py-2 border-b border-white/10 flex-shrink-0">
        <div className="flex items-center justify-between mb-2">
          <div className="flex items-center gap-3">
            <h4 className="text-sm font-medium text-text-secondary">Flow Execution</h4>
            {isRunning && (
              <span className="flex items-center gap-1 text-xs text-primary">
                <Loader2 className="w-3 h-3 animate-spin" />
                Running
              </span>
            )}
            {execution.status === 'completed' && (
              <span className="flex items-center gap-1 text-xs text-green-400">
                <CheckCircle className="w-3 h-3" />
                Completed
              </span>
            )}
            {execution.status === 'error' && (
              <span className="flex items-center gap-1 text-xs text-red-400">
                <XCircle className="w-3 h-3" />
                Failed
              </span>
            )}
          </div>
          <div className="flex items-center gap-4 text-xs">
            {completedCount > 0 && (
              <span className="text-green-400">{completedCount} passed</span>
            )}
            {warningCount > 0 && (
              <span className="text-yellow-400">{warningCount} warning</span>
            )}
            {errorCount > 0 && <span className="text-red-400">{errorCount} failed</span>}
            {totalDuration > 0 && (
              <span className="text-text-muted flex items-center gap-1">
                <Clock className="w-3 h-3" />
                {formatDuration(totalDuration)}
              </span>
            )}
            {onClear && executedSteps.length > 0 && !isRunning && (
              <button
                onClick={onClear}
                className="text-text-muted hover:text-red-400 flex items-center gap-1 transition-colors"
                title="Clear execution history"
              >
                <Trash2 className="w-3 h-3" />
                Clear
              </button>
            )}
          </div>
        </div>

        {/* Progress bar */}
        <div className="h-1.5 bg-bg-tertiary rounded-full overflow-hidden">
          <div
            className={`h-full transition-all duration-300 ${
              errorCount > 0 ? 'bg-red-400' : warningCount > 0 ? 'bg-yellow-400' : 'bg-green-400'
            }`}
            style={{ width: `${progressPercent}%` }}
          />
        </div>
      </div>

      {/* Steps timeline */}
      <div className="flex-1 overflow-y-auto">
        {enabledSteps.map((step, index) => {
          const stepInfo = getStepInfo(step.type)
          const isExpanded = expandedStep === step.id
          const isCurrentStep = isRunning && index === execution.currentStepIndex
          const hasResult = step.status === 'success' || step.status === 'warning' || step.status === 'error'

          return (
            <div
              key={step.id}
              className={`border-b border-white/5 ${
                isCurrentStep ? 'bg-primary/10' : ''
              }`}
            >
              {/* Step header */}
              <button
                onClick={() => setExpandedStep(isExpanded ? null : step.id)}
                disabled={!hasResult}
                className={`w-full px-4 py-2 flex items-center gap-3 transition-colors ${
                  hasResult ? 'hover:bg-bg-tertiary/50' : ''
                }`}
              >
                {/* Step number */}
                <span className="text-xs text-text-muted w-5 flex-shrink-0">
                  {index + 1}.
                </span>

                {/* Status icon */}
                <span className="flex-shrink-0">
                  {isCurrentStep ? (
                    <Loader2 className="w-4 h-4 text-primary animate-spin" />
                  ) : step.status === 'success' ? (
                    <CheckCircle className="w-4 h-4 text-green-400" />
                  ) : step.status === 'warning' ? (
                    <AlertTriangle className="w-4 h-4 text-yellow-400" />
                  ) : step.status === 'error' ? (
                    <XCircle className="w-4 h-4 text-red-400" />
                  ) : (
                    <div className="w-4 h-4 rounded-full border border-white/20" />
                  )}
                </span>

                {/* Step name */}
                <span
                  className={`text-sm ${
                    hasResult || isCurrentStep
                      ? 'text-text-primary'
                      : 'text-text-muted'
                  }`}
                >
                  {stepInfo?.label || step.type}
                </span>

                {/* Duration */}
                {step.duration !== undefined && (
                  <span className="flex items-center gap-1 text-xs text-text-muted">
                    <Timer className="w-3 h-3" />
                    {formatDuration(step.duration)}
                  </span>
                )}

                <div className="flex-1" />

                {/* Expand icon */}
                {hasResult && (
                  <span className="flex-shrink-0">
                    {isExpanded ? (
                      <ChevronDown className="w-4 h-4 text-text-muted" />
                    ) : (
                      <ChevronRight className="w-4 h-4 text-text-muted" />
                    )}
                  </span>
                )}
              </button>

              {/* Expanded result */}
              {isExpanded && hasResult && (
                <div className="px-4 pb-3 space-y-2">
                  {/* Error/Warning message */}
                  {step.error && (
                    <div className={`rounded p-2 ${
                      step.status === 'warning'
                        ? 'bg-yellow-900/20 border border-yellow-500/20'
                        : 'bg-red-900/20 border border-red-500/20'
                    }`}>
                      <p className={`text-xs ${
                        step.status === 'warning' ? 'text-yellow-300' : 'text-red-300'
                      }`}>{step.error}</p>
                    </div>
                  )}

                  {/* Result */}
                  {step.result !== undefined && (step.status === 'success' || step.status === 'warning') && (
                    <div>
                      <div className="flex items-center justify-between mb-1">
                        <span className="text-xs text-text-muted">Result</span>
                        {!isBase64Image(step.result) && (
                          <button
                            onClick={() =>
                              copyToClipboard(formatResult(step.result), step.id)
                            }
                            className="text-xs text-text-muted hover:text-text-primary flex items-center gap-1"
                          >
                            {copiedId === step.id ? (
                              <>
                                <Check className="w-3 h-3" />
                                Copied
                              </>
                            ) : (
                              <>
                                <Copy className="w-3 h-3" />
                                Copy
                              </>
                            )}
                          </button>
                        )}
                      </div>
                      {isBase64Image(step.result) ? (
                        <div>
                          <div className="flex items-center gap-2 text-xs text-text-muted mb-2">
                            <Image className="w-4 h-4" />
                            <span>Screenshot (click to view)</span>
                          </div>
                          <img
                            src={`data:image/png;base64,${step.result}`}
                            alt="Screenshot"
                            className="max-w-full max-h-32 rounded border border-white/10 cursor-pointer hover:border-primary/50 transition-colors"
                            onClick={() => {
                              const win = window.open()
                              if (win) {
                                win.document.write(
                                  `<img src="data:image/png;base64,${step.result}" />`
                                )
                              }
                            }}
                          />
                        </div>
                      ) : (
                        <pre className="text-xs text-green-300 bg-bg-primary/50 p-2 rounded overflow-auto max-h-24">
                          {formatResult(step.result)}
                        </pre>
                      )}
                    </div>
                  )}
                </div>
              )}
            </div>
          )
        })}
      </div>
    </div>
  )
}
