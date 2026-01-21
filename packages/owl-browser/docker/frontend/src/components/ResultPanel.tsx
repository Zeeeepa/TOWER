import { useState } from 'react'
import { CheckCircle, XCircle, Clock, Copy, Check, ChevronDown, ChevronRight, Image, Trash2, Plus } from 'lucide-react'
import type { ToolExecution } from '../types/browser'

interface ResultPanelProps {
  executions: ToolExecution[]
  onClear?: () => void
  onAddToFlow?: (toolName: string, params: Record<string, unknown>) => void
}

export default function ResultPanel({ executions, onClear, onAddToFlow }: ResultPanelProps) {
  const [expandedId, setExpandedId] = useState<number | null>(null)
  const [copiedId, setCopiedId] = useState<number | null>(null)
  const [addedToFlowId, setAddedToFlowId] = useState<number | null>(null)

  const handleAddToFlow = (exec: ToolExecution, index: number) => {
    if (onAddToFlow) {
      onAddToFlow(exec.tool, exec.params)
      setAddedToFlowId(index)
      setTimeout(() => setAddedToFlowId(null), 2000)
    }
  }

  const formatToolName = (name: string): string => {
    return name
      .replace('browser_', '')
      .split('_')
      .map((word) => word.charAt(0).toUpperCase() + word.slice(1))
      .join(' ')
  }

  const formatTime = (timestamp: number): string => {
    return new Date(timestamp).toLocaleTimeString()
  }

  const copyToClipboard = async (text: string, id: number) => {
    try {
      await navigator.clipboard.writeText(text)
      setCopiedId(id)
      setTimeout(() => setCopiedId(null), 2000)
    } catch {
      // Fallback for older browsers
      console.error('Failed to copy')
    }
  }

  const formatResult = (result: unknown): string => {
    if (result === undefined || result === null) return 'null'
    if (typeof result === 'string') {
      // Check if it's base64 image data
      if (result.length > 1000 && /^[A-Za-z0-9+/=]+$/.test(result)) {
        return '[Base64 Image Data]'
      }
      return result
    }
    return JSON.stringify(result, null, 2)
  }

  const isBase64Image = (result: unknown): boolean => {
    if (typeof result !== 'string') return false
    return result.length > 1000 && /^[A-Za-z0-9+/=]+$/.test(result)
  }

  if (executions.length === 0) {
    return (
      <div className="h-full flex items-center justify-center text-text-muted">
        <div className="text-center">
          <Clock className="w-8 h-8 mx-auto mb-2 opacity-50" />
          <p className="text-sm">Tool execution results will appear here</p>
        </div>
      </div>
    )
  }

  return (
    <div className="h-full flex flex-col">
      {/* Header */}
      <div className="px-4 py-2 border-b border-white/10 flex items-center justify-between">
        <h4 className="text-sm font-medium text-text-secondary">Execution Results</h4>
        <div className="flex items-center gap-3">
          <span className="text-xs text-text-muted">{executions.length} executions</span>
          {onClear && executions.length > 0 && (
            <button
              onClick={onClear}
              className="text-xs text-text-muted hover:text-red-400 flex items-center gap-1 transition-colors"
              title="Clear all results"
            >
              <Trash2 className="w-3 h-3" />
              Clear
            </button>
          )}
        </div>
      </div>

      {/* Results list */}
      <div className="flex-1 overflow-y-auto">
        {executions.map((exec, index) => {
          const isExpanded = expandedId === index
          const isSuccess = !exec.error
          const resultStr = formatResult(exec.result)

          return (
            <div
              key={`${exec.timestamp}-${index}`}
              className="border-b border-white/5"
            >
              {/* Result header */}
              <button
                onClick={() => setExpandedId(isExpanded ? null : index)}
                className="w-full px-4 py-2 flex items-center gap-3 hover:bg-bg-tertiary/50 transition-colors"
              >
                {isSuccess ? (
                  <CheckCircle className="w-4 h-4 text-green-400 flex-shrink-0" />
                ) : (
                  <XCircle className="w-4 h-4 text-red-400 flex-shrink-0" />
                )}

                <span className="text-sm font-medium text-text-primary">
                  {formatToolName(exec.tool)}
                </span>

                <span className="text-xs text-text-muted flex-shrink-0">
                  {formatTime(exec.timestamp)}
                </span>

                <div className="flex-1" />

                {isExpanded ? (
                  <ChevronDown className="w-4 h-4 text-text-muted" />
                ) : (
                  <ChevronRight className="w-4 h-4 text-text-muted" />
                )}
              </button>

              {/* Expanded content */}
              {isExpanded && (
                <div className="px-4 pb-3 space-y-3">
                  {/* Add to Flow button */}
                  {onAddToFlow && (
                    <button
                      onClick={() => handleAddToFlow(exec, index)}
                      className={`w-full flex items-center justify-center gap-2 px-3 py-2 rounded-lg text-sm font-medium transition-colors ${
                        addedToFlowId === index
                          ? 'bg-green-500/20 text-green-400 border border-green-500/30'
                          : 'bg-primary/10 text-primary hover:bg-primary/20 border border-primary/30'
                      }`}
                    >
                      {addedToFlowId === index ? (
                        <>
                          <Check className="w-4 h-4" />
                          Added to Flow
                        </>
                      ) : (
                        <>
                          <Plus className="w-4 h-4" />
                          Add to Flow
                        </>
                      )}
                    </button>
                  )}

                  {/* Parameters */}
                  {Object.keys(exec.params).length > 0 && (
                    <div>
                      <span className="text-xs text-text-muted">Parameters</span>
                      <div className="code-block mt-1 text-xs">
                        <pre className="text-text-secondary">
                          {JSON.stringify(exec.params, null, 2)}
                        </pre>
                      </div>
                    </div>
                  )}

                  {/* Result or Error */}
                  <div>
                    <div className="flex items-center justify-between mb-1">
                      <span className="text-xs text-text-muted">
                        {isSuccess ? 'Result' : 'Error'}
                      </span>
                      {!isBase64Image(exec.result) && (
                        <button
                          onClick={() =>
                            copyToClipboard(
                              isSuccess ? resultStr : (exec.error || ''),
                              index
                            )
                          }
                          className="text-xs text-text-muted hover:text-text-primary flex items-center gap-1"
                        >
                          {copiedId === index ? (
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

                    {isSuccess ? (
                      isBase64Image(exec.result) ? (
                        <div className="mt-1">
                          <div className="flex items-center gap-2 text-xs text-text-muted mb-2">
                            <Image className="w-4 h-4" />
                            <span>Screenshot (click to view)</span>
                          </div>
                          <img
                            src={`data:image/png;base64,${exec.result}`}
                            alt="Screenshot"
                            className="max-w-full rounded-lg border border-white/10 cursor-pointer hover:border-primary/50 transition-colors"
                            onClick={() => {
                              const win = window.open()
                              if (win) {
                                win.document.write(`<img src="data:image/png;base64,${exec.result}" />`)
                              }
                            }}
                          />
                        </div>
                      ) : (
                        <div className="code-block mt-1 text-xs">
                          <pre className={isSuccess ? 'text-green-300' : 'text-red-300'}>
                            {resultStr}
                          </pre>
                        </div>
                      )
                    ) : (
                      <div className="code-block mt-1 text-xs bg-red-900/20 border-red-500/20">
                        <pre className="text-red-300">{exec.error}</pre>
                      </div>
                    )}
                  </div>
                </div>
              )}
            </div>
          )
        })}
      </div>
    </div>
  )
}
