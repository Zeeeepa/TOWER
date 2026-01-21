import { useState, useEffect, useCallback } from 'react'
import { Plus, X, Crosshair, MapPin, Check, GitBranch } from 'lucide-react'
import { getToolInfo } from '../api/browserApi'
import { STEP_CATEGORIES, createStep, getStepInfo, CONDITION_OPERATORS, createDefaultCondition } from '../types/flow'
import type { FlowStep, FlowCondition, ConditionOperator } from '../types/flow'
import type { ToolParam } from '../types/browser'

interface FlowStepFormProps {
  onAddStep: (step: FlowStep) => void
  onStartPicker: (mode: 'element' | 'position', callback: (value: string) => void) => void
  contextId: string | null
  editingStep?: FlowStep | null
  onCancelEdit?: () => void
}

export default function FlowStepForm({ onAddStep, onStartPicker, contextId, editingStep, onCancelEdit }: FlowStepFormProps) {
  const [selectedType, setSelectedType] = useState<string | null>(null)
  const [parameters, setParameters] = useState<ToolParam[]>([])
  const [values, setValues] = useState<Record<string, string | number | boolean>>({})
  const [loading, setLoading] = useState(false)
  // Condition state for condition steps
  const [condition, setCondition] = useState<FlowCondition>(createDefaultCondition())

  // Handle editing step - auto-select type and populate values
  useEffect(() => {
    if (editingStep) {
      setSelectedType(editingStep.type)
      // If editing a condition step, populate the condition
      if (editingStep.type === 'condition' && editingStep.condition) {
        setCondition(editingStep.condition)
      }
    }
  }, [editingStep])

  // Load tool parameters when type changes
  useEffect(() => {
    if (!selectedType) {
      setParameters([])
      setValues({})
      return
    }

    setLoading(true)
    getToolInfo(selectedType).then((result) => {
      const toolInfo = result.result || (result as unknown as { parameters?: ToolParam[] })
      const params = toolInfo.parameters || []

      // Filter out context_id as we'll add it automatically
      const filteredParams = params.filter(p => p.name !== 'context_id')
      setParameters(filteredParams)

      // Initialize values - if editing, use existing values
      const initialValues: Record<string, string | number | boolean> = {}
      for (const param of filteredParams) {
        if (editingStep && editingStep.params[param.name] !== undefined) {
          // Use existing value from editing step
          initialValues[param.name] = editingStep.params[param.name] as string | number | boolean
        } else if (param.type === 'boolean') {
          initialValues[param.name] = false
        } else {
          initialValues[param.name] = ''
        }
      }
      setValues(initialValues)
      setLoading(false)
    })
  }, [selectedType, editingStep])

  const handleChange = useCallback((name: string, value: string | number | boolean) => {
    setValues(prev => ({ ...prev, [name]: value }))
  }, [])

  const handlePickerValue = useCallback(
    (paramName: string) => (value: string) => {
      handleChange(paramName, value)
    },
    [handleChange]
  )

  const handleAddStep = () => {
    if (!selectedType) return

    const step = createStep(selectedType)

    // Handle condition steps specially
    if (selectedType === 'condition') {
      step.condition = { ...condition }
      step.onTrue = editingStep?.onTrue || []
      step.onFalse = editingStep?.onFalse || []
    } else {
      // Build params object, filtering empty values
      const params: Record<string, unknown> = {}
      for (const param of parameters) {
        const value = values[param.name]
        if (value === '' || value === undefined) continue

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
      step.params = params
    }

    onAddStep(step)

    // Reset form
    setSelectedType(null)
    setValues({})
    setCondition(createDefaultCondition())
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

  const stepInfo = selectedType ? getStepInfo(selectedType) : null

  return (
    <div className="border border-white/10 rounded-lg overflow-hidden">
      {/* Step Type Selector */}
      {!selectedType ? (
        <div className="p-3">
          <h4 className="text-sm font-medium text-text-primary mb-3">Add Step</h4>
          <div className="space-y-3 max-h-64 overflow-y-auto">
            {Object.entries(STEP_CATEGORIES).map(([key, category]) => (
              <div key={key}>
                <h5 className="text-xs text-text-muted mb-1.5">{category.label}</h5>
                <div className="flex flex-wrap gap-1.5">
                  {category.steps.map((step) => (
                    <button
                      key={step.type}
                      onClick={() => setSelectedType(step.type)}
                      className="text-xs px-2 py-1 rounded bg-bg-tertiary hover:bg-primary/20 text-text-secondary hover:text-primary transition-colors"
                    >
                      {step.label}
                    </button>
                  ))}
                </div>
              </div>
            ))}
          </div>
        </div>
      ) : (
        <div className="p-3">
          {/* Header */}
          <div className="flex items-center justify-between mb-3">
            <h4 className="text-sm font-medium text-text-primary">
              {editingStep ? 'Edit Step: ' : ''}{stepInfo?.label || selectedType}
            </h4>
            <button
              onClick={() => {
                setSelectedType(null)
                if (editingStep && onCancelEdit) onCancelEdit()
              }}
              className="text-text-muted hover:text-text-primary"
            >
              <X className="w-4 h-4" />
            </button>
          </div>

          {/* Condition Builder for condition steps */}
          {selectedType === 'condition' ? (
            <div className="space-y-3 mb-3">
              <div className="flex items-center gap-2 mb-2">
                <GitBranch className="w-4 h-4 text-primary" />
                <span className="text-xs text-text-secondary">Configure condition</span>
              </div>

              {/* Source */}
              <div>
                <label className="text-xs text-text-secondary mb-1 block">Check</label>
                <select
                  value={condition.source}
                  onChange={(e) => setCondition(prev => ({ ...prev, source: e.target.value as 'previous' | 'step' }))}
                  className="input-field text-xs py-1.5"
                >
                  <option value="previous">Previous Step Result</option>
                  <option value="step">Specific Step Result</option>
                </select>
              </div>

              {/* Field path (optional) */}
              <div>
                <label className="text-xs text-text-secondary mb-1 block">Field Path (optional)</label>
                <input
                  type="text"
                  value={condition.field || ''}
                  onChange={(e) => setCondition(prev => ({ ...prev, field: e.target.value || undefined }))}
                  placeholder="e.g., success, data.count, message"
                  className="input-field text-xs py-1.5"
                />
              </div>

              {/* Operator */}
              <div>
                <label className="text-xs text-text-secondary mb-1 block">Operator</label>
                <select
                  value={condition.operator}
                  onChange={(e) => setCondition(prev => ({ ...prev, operator: e.target.value as ConditionOperator }))}
                  className="input-field text-xs py-1.5"
                >
                  {CONDITION_OPERATORS.map(op => (
                    <option key={op.value} value={op.value}>{op.label}</option>
                  ))}
                </select>
              </div>

              {/* Value (only for operators that need it) */}
              {CONDITION_OPERATORS.find(op => op.value === condition.operator)?.needsValue && (
                <div>
                  <label className="text-xs text-text-secondary mb-1 block">Compare Value</label>
                  <input
                    type="text"
                    value={condition.value !== undefined ? String(condition.value) : ''}
                    onChange={(e) => {
                      const val = e.target.value
                      // Try to parse as number or boolean
                      let parsed: unknown = val
                      if (val === 'true') parsed = true
                      else if (val === 'false') parsed = false
                      else if (!isNaN(Number(val)) && val !== '') parsed = Number(val)
                      setCondition(prev => ({ ...prev, value: parsed }))
                    }}
                    placeholder="Value to compare against"
                    className="input-field text-xs py-1.5"
                  />
                </div>
              )}

              <p className="text-xs text-text-muted mt-2">
                Branches will be configured after adding the condition step.
              </p>
            </div>
          ) : /* Parameters */
          loading ? (
            <div className="py-4 text-center text-text-muted text-sm">Loading...</div>
          ) : parameters.length === 0 ? (
            <p className="text-text-muted text-xs mb-3">No parameters required</p>
          ) : (
            <div className="space-y-3 mb-3">
              {parameters.map((param) => (
                <div key={param.name}>
                  <label className="text-xs text-text-secondary mb-1 block">
                    {param.name.replace(/_/g, ' ')}
                    {param.required && <span className="text-red-400 ml-1">*</span>}
                  </label>

                  {param.type === 'boolean' ? (
                    <label className="flex items-center gap-2 cursor-pointer">
                      <input
                        type="checkbox"
                        checked={Boolean(values[param.name])}
                        onChange={(e) => handleChange(param.name, e.target.checked)}
                        className="w-3.5 h-3.5 rounded border-white/20 bg-bg-tertiary text-primary"
                      />
                      <span className="text-xs text-text-muted">{param.description}</span>
                    </label>
                  ) : param.type === 'enum' ? (
                    <select
                      value={String(values[param.name] || '')}
                      onChange={(e) => handleChange(param.name, e.target.value)}
                      className="input-field text-xs py-1.5"
                    >
                      <option value="">Select...</option>
                      {param.enum_values?.map((val) => (
                        <option key={val} value={val}>{val}</option>
                      ))}
                    </select>
                  ) : (
                    <div className="flex gap-1.5">
                      <input
                        type={param.type === 'integer' || param.type === 'number' ? 'number' : 'text'}
                        value={String(values[param.name] ?? '')}
                        onChange={(e) => handleChange(param.name, e.target.value)}
                        placeholder={param.description}
                        className="input-field text-xs py-1.5 flex-1"
                      />

                      {shouldShowElementPicker(param.name) && (
                        <button
                          onClick={() => onStartPicker('element', handlePickerValue(param.name))}
                          disabled={!contextId}
                          className="btn-icon p-1.5 flex-shrink-0"
                          title="Pick element"
                        >
                          <Crosshair className="w-3.5 h-3.5 text-primary" />
                        </button>
                      )}

                      {shouldShowPositionPicker(param.name) && (
                        <button
                          onClick={() => onStartPicker('position', handlePickerValue(param.name))}
                          disabled={!contextId}
                          className="btn-icon p-1.5 flex-shrink-0"
                          title="Pick position"
                        >
                          <MapPin className="w-3.5 h-3.5 text-text-secondary" />
                        </button>
                      )}
                    </div>
                  )}
                </div>
              ))}
            </div>
          )}

          {/* Add/Update Button */}
          <div className="flex gap-2">
            {editingStep && onCancelEdit && (
              <button
                onClick={() => {
                  setSelectedType(null)
                  onCancelEdit()
                }}
                className="flex-1 btn-secondary text-xs py-1.5 flex items-center justify-center gap-1.5"
              >
                <X className="w-3.5 h-3.5" />
                Cancel
              </button>
            )}
            <button
              onClick={handleAddStep}
              className="flex-1 btn-primary text-xs py-1.5 flex items-center justify-center gap-1.5"
            >
              {editingStep ? (
                <>
                  <Check className="w-3.5 h-3.5" />
                  Update Step
                </>
              ) : (
                <>
                  <Plus className="w-3.5 h-3.5" />
                  Add Step
                </>
              )}
            </button>
          </div>
        </div>
      )}
    </div>
  )
}
