import { useState, useCallback } from 'react'
import { Wrench, Workflow, AlertTriangle, X, FlaskConical } from 'lucide-react'
import { useBrowserContext } from '../hooks/useBrowserContext'
import Header from './Header'
import ToolsSidebar from './ToolsSidebar'
import VideoViewer from './VideoViewer'
import ResultPanel from './ResultPanel'
import ToolForm from './ToolForm'
import FlowDesigner, { type FlowExecutionState } from './FlowDesigner'
import FlowExecutionPanel from './FlowExecutionPanel'
import IPCTestsPanel, { type IPCTestState } from './IPCTestsPanel'
import IPCTestResultsView from './IPCTestResultsView'
import type { ToolExecution } from '../types/browser'
import type { FlowStep } from '../types/flow'
import { generateStepId } from '../types/flow'

type SidebarTab = 'tools' | 'flow' | 'ipc-tests'

export default function ControlPanel() {
  const browserContext = useBrowserContext()
  const [sidebarTab, setSidebarTab] = useState<SidebarTab>('tools')
  const [selectedTool, setSelectedTool] = useState<string | null>(null)
  const [executions, setExecutions] = useState<ToolExecution[]>([])
  const [pickerMode, setPickerMode] = useState<'element' | 'position' | null>(null)
  const [pickerCallback, setPickerCallback] = useState<((value: string) => void) | null>(null)
  const [flowExecutionState, setFlowExecutionState] = useState<FlowExecutionState | null>(null)

  // Flow state - lifted up to persist across tab switches
  const [flowSteps, setFlowSteps] = useState<FlowStep[]>([])
  const [flowName, setFlowName] = useState('My Flow')

  // IPC test state - lifted up to persist across tab switches
  const [ipcTestState, setIpcTestState] = useState<IPCTestState>({
    status: null,
    report: null,
    isLoadingReport: false,
    selectedRunId: null,
  })

  const handleFlowExecutionStateChange = useCallback((state: FlowExecutionState) => {
    setFlowExecutionState(state)
  }, [])

  const handleIpcTestStateChange = useCallback((state: IPCTestState) => {
    setIpcTestState(state)
  }, [])

  const handleToolSelect = (toolName: string) => {
    setSelectedTool(toolName)
  }

  const handleToolExecuted = async (execution: ToolExecution) => {
    setExecutions((prev) => [execution, ...prev].slice(0, 50)) // Keep last 50

    // If browser_create_context succeeded, switch to the new context
    if (execution.tool === 'browser_create_context' && execution.result && !execution.error) {
      // The result from ToolForm is the raw IPC response
      // Old format: {id: N, result: "ctx_xxx"}
      // New format: {id: N, result: {context_id: "ctx_xxx", vm_profile: {...}, ...}}
      const rawResult = execution.result as { id?: number; result?: string | { context_id?: string } } | string
      let newContextId: string | undefined
      if (typeof rawResult === 'string') {
        newContextId = rawResult
      } else if (typeof rawResult?.result === 'string') {
        newContextId = rawResult.result
      } else if (typeof rawResult?.result === 'object' && rawResult.result?.context_id) {
        newContextId = rawResult.result.context_id
      }
      if (newContextId && typeof newContextId === 'string') {
        await browserContext.switchContext(newContextId)
      }
    }

    // If browser_close_context succeeded, refresh contexts
    if (execution.tool === 'browser_close_context' && !execution.error) {
      browserContext.refreshContexts()
    }
  }

  const handleStartPicker = (mode: 'element' | 'position', callback: (value: string) => void) => {
    setPickerMode(mode)
    setPickerCallback(() => callback)
  }

  const handlePickerComplete = (value: string) => {
    if (pickerCallback) {
      pickerCallback(value)
    }
    setPickerMode(null)
    setPickerCallback(null)
  }

  const handlePickerCancel = () => {
    setPickerMode(null)
    setPickerCallback(null)
  }

  const handleAddExecutionToFlow = useCallback((toolName: string, params: Record<string, unknown>) => {
    const newStep: FlowStep = {
      id: generateStepId(),
      type: toolName,
      enabled: true,
      params: { ...params },
    }
    // Remove context_id from params since it's injected at execution time
    delete newStep.params.context_id

    // For Navigate, also add "Wait for Network Idle" after it
    if (toolName === 'browser_navigate') {
      const waitStep: FlowStep = {
        id: generateStepId(),
        type: 'browser_wait_for_network_idle',
        enabled: true,
        params: {},
      }
      setFlowSteps(prev => [...prev, newStep, waitStep])
    } else {
      setFlowSteps(prev => [...prev, newStep])
    }
  }, [])

  return (
    <div className="h-screen flex flex-col bg-bg-primary overflow-hidden">
      {/* Header */}
      <Header browserContext={browserContext} />

      {/* Session expired notification */}
      {browserContext.sessionExpired && (
        <div className="flex-shrink-0 bg-amber-500/20 border-b border-amber-500/30 px-4 py-2 flex items-center justify-between">
          <div className="flex items-center gap-2 text-amber-200">
            <AlertTriangle className="w-4 h-4" />
            <span className="text-sm">
              Your previous browser session was lost due to a server restart. Please create a new context.
            </span>
          </div>
          <button
            onClick={() => browserContext.dismissSessionExpired()}
            className="p-1 hover:bg-amber-500/20 rounded transition-colors"
          >
            <X className="w-4 h-4 text-amber-200" />
          </button>
        </div>
      )}

      {/* Main content */}
      <div className="flex-1 flex overflow-hidden">
        {/* Left sidebar */}
        <div className="w-80 flex-shrink-0 border-r border-white/10 flex flex-col overflow-hidden">
          {/* Sidebar Tabs */}
          <div className="flex border-b border-white/10">
            <button
              onClick={() => setSidebarTab('tools')}
              className={`flex-1 flex items-center justify-center gap-2 px-4 py-2.5 text-sm font-medium transition-colors ${
                sidebarTab === 'tools'
                  ? 'text-primary border-b-2 border-primary bg-primary/5'
                  : 'text-text-secondary hover:text-text-primary'
              }`}
            >
              <Wrench className="w-4 h-4" />
              Tools
            </button>
            <button
              onClick={() => setSidebarTab('flow')}
              className={`flex-1 flex items-center justify-center gap-2 px-4 py-2.5 text-sm font-medium transition-colors ${
                sidebarTab === 'flow'
                  ? 'text-primary border-b-2 border-primary bg-primary/5'
                  : 'text-text-secondary hover:text-text-primary'
              }`}
            >
              <Workflow className="w-4 h-4" />
              Flow
            </button>
            <button
              onClick={() => setSidebarTab('ipc-tests')}
              className={`flex-1 flex items-center justify-center gap-2 px-4 py-2.5 text-sm font-medium transition-colors ${
                sidebarTab === 'ipc-tests'
                  ? 'text-primary border-b-2 border-primary bg-primary/5'
                  : 'text-text-secondary hover:text-text-primary'
              }`}
            >
              <FlaskConical className="w-4 h-4" />
              Tests
            </button>
          </div>

          {/* Sidebar Content */}
          <div className="flex-1 overflow-hidden">
            {sidebarTab === 'tools' && (
              <ToolsSidebar
                selectedTool={selectedTool}
                onSelectTool={handleToolSelect}
              />
            )}
            {sidebarTab === 'flow' && (
              <FlowDesigner
                contextId={browserContext.contextId}
                onStartPicker={handleStartPicker}
                onExecutionStateChange={handleFlowExecutionStateChange}
                steps={flowSteps}
                onStepsChange={setFlowSteps}
                flowName={flowName}
                onFlowNameChange={setFlowName}
              />
            )}
            {sidebarTab === 'ipc-tests' && (
              <IPCTestsPanel onStateChange={handleIpcTestStateChange} />
            )}
          </div>
        </div>

        {/* Main area */}
        <div className="flex-1 flex flex-col overflow-hidden">
          {/* Top - Video/IPC Results + Tool Form */}
          <div className="flex-1 flex overflow-hidden min-h-0">
            {/* Main viewer area - show IPC results when on tests tab, otherwise show video */}
            <div className="flex-1 p-4 overflow-hidden">
              {sidebarTab === 'ipc-tests' ? (
                <IPCTestResultsView
                  status={ipcTestState.status}
                  report={ipcTestState.report}
                  isLoading={ipcTestState.isLoadingReport}
                />
              ) : (
                <VideoViewer
                  contextId={browserContext.contextId}
                  streamActive={browserContext.streamActive}
                  isCreatingContext={browserContext.isLoading}
                  pageInfo={browserContext.pageInfo}
                  pickerMode={pickerMode}
                  onPickerComplete={handlePickerComplete}
                  onPickerCancel={handlePickerCancel}
                />
              )}
            </div>

            {/* Tool form panel - hide when on IPC tests tab */}
            {selectedTool && sidebarTab !== 'ipc-tests' && (
              <div className="w-96 flex-shrink-0 border-l border-white/10 overflow-auto">
                <ToolForm
                  toolName={selectedTool}
                  contextId={browserContext.contextId}
                  onExecuted={handleToolExecuted}
                  onStartPicker={handleStartPicker}
                  onClose={() => setSelectedTool(null)}
                />
              </div>
            )}
          </div>

          {/* Bottom - Results or Flow Execution (hidden on IPC tests tab) */}
          {sidebarTab !== 'ipc-tests' && (
            <div className="h-64 flex-shrink-0 border-t border-white/10 overflow-hidden">
              {sidebarTab === 'flow' && flowExecutionState ? (
                <FlowExecutionPanel
                  steps={flowExecutionState.steps}
                  execution={flowExecutionState.execution}
                  isRunning={flowExecutionState.isRunning}
                  onClear={flowExecutionState.onClearExecution}
                />
              ) : (
                <ResultPanel
                  executions={executions}
                  onClear={() => setExecutions([])}
                  onAddToFlow={handleAddExecutionToFlow}
                />
              )}
            </div>
          )}
        </div>
      </div>
    </div>
  )
}
