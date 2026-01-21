import { useState, useEffect, useCallback } from 'react'
import { X, Play, Crosshair, MapPin, Loader2 } from 'lucide-react'
import { getToolInfo, executeTool } from '../api/browserApi'
import type { ToolParam, ToolExecution } from '../types/browser'

interface ToolFormProps {
  toolName: string
  contextId: string | null
  onExecuted: (execution: ToolExecution) => void
  onStartPicker: (mode: 'element' | 'position', callback: (value: string) => void) => void
  onClose: () => void
}

export default function ToolForm({
  toolName,
  contextId,
  onExecuted,
  onStartPicker,
  onClose,
}: ToolFormProps) {
  const [parameters, setParameters] = useState<ToolParam[]>([])
  const [values, setValues] = useState<Record<string, string | number | boolean>>({})
  const [loading, setLoading] = useState(true)
  const [executing, setExecuting] = useState(false)
  const [description, setDescription] = useState('')

  // Load tool info
  useEffect(() => {
    setLoading(true)
    getToolInfo(toolName).then((result) => {
      // Tool info returns directly: {name, description, parameters} without wrapper
      // Or wrapped: {success: true, result: {name, description, parameters}}
      const toolInfo = result.result || (result as unknown as { parameters?: ToolParam[]; description?: string })
      const params = toolInfo.parameters || (result as unknown as { parameters?: ToolParam[] }).parameters || []
      const desc = toolInfo.description || (result as unknown as { description?: string }).description || ''

      if (params.length > 0 || desc) {
        setParameters(params)
        setDescription(desc)

        // Initialize values with defaults
        const initialValues: Record<string, string | number | boolean> = {}
        for (const param of params) {
          if (param.name === 'context_id' && contextId) {
            initialValues[param.name] = contextId
          } else if (param.type === 'boolean') {
            initialValues[param.name] = false
          } else if (param.type === 'integer' || param.type === 'number') {
            initialValues[param.name] = ''
          } else {
            initialValues[param.name] = ''
          }
        }
        setValues(initialValues)
      }
      setLoading(false)
    })
  }, [toolName, contextId])

  // Update context_id when it changes
  useEffect(() => {
    if (contextId && parameters.some((p) => p.name === 'context_id')) {
      setValues((prev) => ({ ...prev, context_id: contextId }))
    }
  }, [contextId, parameters])

  const handleChange = useCallback((name: string, value: string | number | boolean) => {
    setValues((prev) => ({ ...prev, [name]: value }))
  }, [])

  const handlePickerValue = useCallback(
    (paramName: string) => (value: string) => {
      handleChange(paramName, value)
    },
    [handleChange]
  )

  const handleExecute = async () => {
    setExecuting(true)
    const startTime = Date.now()

    // Build params object, filtering out empty values
    const params: Record<string, unknown> = {}
    for (const param of parameters) {
      const value = values[param.name]
      if (value === '' || value === undefined) {
        if (param.required) {
          onExecuted({
            tool: toolName,
            params: {},
            timestamp: startTime,
            error: `Missing required parameter: ${param.name}`,
          })
          setExecuting(false)
          return
        }
        continue
      }

      // Convert types
      if (param.type === 'integer') {
        params[param.name] = parseInt(String(value), 10)
      } else if (param.type === 'number') {
        params[param.name] = parseFloat(String(value))
      } else if (param.type === 'boolean') {
        params[param.name] = Boolean(value)
      } else {
        params[param.name] = value
      }
    }

    try {
      const result = await executeTool(toolName, params)
      onExecuted({
        tool: toolName,
        params,
        timestamp: startTime,
        result: result.success ? result.result : undefined,
        error: result.success ? undefined : result.error,
      })
    } catch (error) {
      onExecuted({
        tool: toolName,
        params,
        timestamp: startTime,
        error: error instanceof Error ? error.message : 'Unknown error',
      })
    } finally {
      setExecuting(false)
    }
  }

  const formatToolName = (name: string): string => {
    return name
      .replace('browser_', '')
      .split('_')
      .map((word) => word.charAt(0).toUpperCase() + word.slice(1))
      .join(' ')
  }

  // Check if a parameter should have element picker button
  const shouldShowElementPicker = (paramName: string): boolean => {
    return ['selector', 'source_selector', 'target_selector'].includes(paramName)
  }

  // Check if a parameter should have position picker button
  // Only show for selector fields and explicit coordinate parameters
  const shouldShowPositionPicker = (paramName: string): boolean => {
    const selectorParams = ['selector', 'source_selector', 'target_selector', 'position']
    const coordinateParams = ['x', 'y', 'start_x', 'start_y', 'end_x', 'end_y', 'mid_x', 'mid_y']
    return selectorParams.includes(paramName) || coordinateParams.includes(paramName)
  }

  if (loading) {
    return (
      <div className="p-4 flex items-center justify-center">
        <Loader2 className="w-6 h-6 text-primary animate-spin" />
      </div>
    )
  }

  return (
    <div className="flex flex-col h-full">
      {/* Header */}
      <div className="p-4 border-b border-white/10 flex items-center justify-between">
        <div>
          <h3 className="font-semibold text-text-primary">{formatToolName(toolName)}</h3>
          {description && (
            <p className="text-xs text-text-secondary mt-1">{description}</p>
          )}
        </div>
        <button onClick={onClose} className="btn-icon p-1.5">
          <X className="w-4 h-4" />
        </button>
      </div>

      {/* Parameters */}
      <div className="flex-1 overflow-y-auto p-4 space-y-4">
        {parameters.length === 0 ? (
          <p className="text-text-secondary text-sm">No parameters required</p>
        ) : (
          parameters.map((param) => (
            <div key={param.name}>
              <label className="label">
                {param.name.replace(/_/g, ' ')}
                {param.required && <span className="text-red-400 ml-1">*</span>}
              </label>

              {param.type === 'boolean' ? (
                <label className="flex items-center gap-2 cursor-pointer">
                  <input
                    type="checkbox"
                    checked={Boolean(values[param.name])}
                    onChange={(e) => handleChange(param.name, e.target.checked)}
                    className="w-4 h-4 rounded border-white/20 bg-bg-tertiary text-primary focus:ring-primary/30"
                  />
                  <span className="text-sm text-text-secondary">{param.description}</span>
                </label>
              ) : param.type === 'enum' ? (
                <select
                  value={String(values[param.name] || '')}
                  onChange={(e) => handleChange(param.name, e.target.value)}
                  className="input-field"
                >
                  <option value="">Select...</option>
                  {param.enum_values?.map((val) => (
                    <option key={val} value={val}>
                      {val}
                    </option>
                  ))}
                </select>
              ) : (
                <div className="flex gap-2">
                  <input
                    type={param.type === 'integer' || param.type === 'number' ? 'number' : 'text'}
                    value={String(values[param.name] ?? '')}
                    onChange={(e) => handleChange(param.name, e.target.value)}
                    placeholder={param.description}
                    className="input-field flex-1"
                    disabled={param.name === 'context_id'}
                  />

                  {/* Element picker button for selector fields */}
                  {shouldShowElementPicker(param.name) && (
                    <button
                      onClick={() =>
                        onStartPicker('element', handlePickerValue(param.name))
                      }
                      disabled={!contextId}
                      className="btn-icon flex-shrink-0"
                      title="Pick element from page"
                    >
                      <Crosshair className="w-4 h-4 text-primary" />
                    </button>
                  )}

                  {shouldShowPositionPicker(param.name) && (
                    <button
                      onClick={() =>
                        onStartPicker('position', handlePickerValue(param.name))
                      }
                      disabled={!contextId}
                      className="btn-icon flex-shrink-0"
                      title="Pick position from page"
                    >
                      <MapPin className="w-4 h-4 text-text-secondary" />
                    </button>
                  )}
                </div>
              )}

              {param.type !== 'boolean' && (
                <p className="text-xs text-text-muted mt-1">{param.description}</p>
              )}
            </div>
          ))
        )}
      </div>

      {/* Execute button */}
      <div className="p-4 border-t border-white/10">
        <button
          onClick={handleExecute}
          disabled={executing || (!contextId && parameters.some((p) => p.name === 'context_id' && p.required))}
          className="w-full btn-primary flex items-center justify-center gap-2"
        >
          {executing ? (
            <>
              <Loader2 className="w-4 h-4 animate-spin" />
              <span>Executing...</span>
            </>
          ) : (
            <>
              <Play className="w-4 h-4" />
              <span>Execute</span>
            </>
          )}
        </button>

        {!contextId && parameters.some((p) => p.name === 'context_id' && p.required) && (
          <p className="text-xs text-yellow-400 mt-2 text-center">
            Create a browser context first
          </p>
        )}
      </div>
    </div>
  )
}
