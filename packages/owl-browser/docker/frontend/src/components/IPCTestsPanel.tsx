import { useState, useEffect, useCallback } from 'react'
import {
  Play,
  Square,
  RefreshCw,
  Trash2,
  CheckCircle,
  XCircle,
  Clock,
  Loader,
  ChevronDown,
  ChevronRight,
  FileText,
  Eye,
  Download,
} from 'lucide-react'
import {
  runIpcTest,
  getIpcTestStatus,
  abortIpcTest,
  listIpcTestReports,
  deleteIpcTestReport,
  cleanAllIpcTestReports,
  getIpcTestReport,
  downloadIpcTestHtmlReport,
  type IpcTestRunConfig,
  type IpcTestStatus,
  type IpcTestReportInfo,
  type IpcTestFullReport,
} from '../api/browserApi'

type TestMode = 'smoke' | 'full' | 'benchmark' | 'stress' | 'leak-check' | 'parallel'

const TEST_MODES: { value: TestMode; label: string; description: string }[] = [
  { value: 'smoke', label: 'Smoke', description: 'Quick validation (5 tests)' },
  { value: 'full', label: 'Full', description: 'Comprehensive (138+ tests)' },
  { value: 'benchmark', label: 'Benchmark', description: 'Performance testing' },
  { value: 'stress', label: 'Stress', description: 'Load testing' },
  { value: 'leak-check', label: 'Leak Check', description: 'Memory leak detection' },
  { value: 'parallel', label: 'Parallel', description: 'Concurrent testing' },
]

export interface IPCTestState {
  status: IpcTestStatus | null
  report: IpcTestFullReport | null
  isLoadingReport: boolean
  selectedRunId: string | null
}

interface IPCTestsPanelProps {
  onStateChange?: (state: IPCTestState) => void
}

export default function IPCTestsPanel({ onStateChange }: IPCTestsPanelProps) {
  const [mode, setMode] = useState<TestMode>('full')
  const [verbose, setVerbose] = useState(false)
  const [iterations, setIterations] = useState(1)
  const [contexts, setContexts] = useState(10)
  const [duration, setDuration] = useState(60)
  const [concurrency, setConcurrency] = useState(4)

  const [currentRunId, setCurrentRunId] = useState<string | null>(null)
  const [currentStatus, setCurrentStatus] = useState<IpcTestStatus | null>(null)
  const [reports, setReports] = useState<IpcTestReportInfo[]>([])
  const [isLoading, setIsLoading] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [expandedReports, setExpandedReports] = useState<Set<string>>(new Set())
  const [selectedReport, setSelectedReport] = useState<IpcTestFullReport | null>(null)
  const [selectedRunId, setSelectedRunId] = useState<string | null>(null)
  const [isLoadingReport, setIsLoadingReport] = useState(false)

  // Define callbacks first before useEffects that use them
  const fetchReports = useCallback(async () => {
    const response = await listIpcTestReports()
    if (response.success && response.result) {
      setReports(response.result)
    }
  }, [])

  const fetchCurrentStatus = useCallback(async () => {
    const response = await getIpcTestStatus()
    if (response.success && response.result) {
      setCurrentStatus(response.result)
      if (response.result.run_id) {
        setCurrentRunId(response.result.run_id)
      }
    }
  }, [])

  const loadReport = useCallback(async (runId: string) => {
    setIsLoadingReport(true)
    setSelectedRunId(runId)
    setSelectedReport(null)

    const response = await getIpcTestReport(runId, 'json')
    setIsLoadingReport(false)

    if (response.success && response.result) {
      setSelectedReport(response.result)
    } else {
      setError(response.error || 'Failed to load report')
    }
  }, [])

  // Fetch reports on mount
  useEffect(() => {
    fetchReports()
    fetchCurrentStatus()
  }, [fetchReports, fetchCurrentStatus])

  // Notify parent of state changes
  useEffect(() => {
    if (onStateChange) {
      onStateChange({
        status: currentStatus,
        report: selectedReport,
        isLoadingReport,
        selectedRunId,
      })
    }
  }, [currentStatus, selectedReport, isLoadingReport, selectedRunId, onStateChange])

  // Poll for status while test is running
  useEffect(() => {
    if (currentStatus?.status === 'running' && currentRunId) {
      const interval = setInterval(() => {
        fetchCurrentStatus()
      }, 2000)
      return () => clearInterval(interval)
    }
  }, [currentStatus?.status, currentRunId, fetchCurrentStatus])

  // Auto-load report when test completes
  // Only auto-load if the status run_id matches currentRunId to avoid race conditions
  useEffect(() => {
    if (
      currentStatus?.status === 'completed' &&
      currentRunId &&
      currentStatus.run_id === currentRunId &&
      !selectedReport
    ) {
      loadReport(currentRunId)
      // Also refresh the reports list to show the new report
      fetchReports()
    }
  }, [currentStatus?.status, currentStatus?.run_id, currentRunId, selectedReport, loadReport, fetchReports])

  const handleRunTest = async () => {
    setIsLoading(true)
    setError(null)
    // Clear previous report state so auto-load will work for new test
    setSelectedReport(null)
    setSelectedRunId(null)

    const config: IpcTestRunConfig = {
      mode,
      verbose,
    }

    // Add mode-specific options
    if (mode === 'benchmark') {
      config.iterations = iterations
    } else if (mode === 'stress') {
      config.contexts = contexts
      config.duration = duration
    } else if (mode === 'leak-check') {
      config.duration = duration
    } else if (mode === 'parallel') {
      config.concurrency = concurrency
    }

    const response = await runIpcTest(config)
    setIsLoading(false)

    if (response.success && response.result) {
      setCurrentRunId(response.result.run_id)
      fetchCurrentStatus()
    } else {
      setError(response.error || 'Failed to start test')
    }
  }

  const handleAbortTest = async () => {
    if (!currentRunId) return
    setIsLoading(true)
    const response = await abortIpcTest(currentRunId)
    setIsLoading(false)

    if (response.success) {
      fetchCurrentStatus()
    } else {
      setError(response.error || 'Failed to abort test')
    }
  }

  const handleDeleteReport = async (runId: string) => {
    if (!confirm('Are you sure you want to delete this report?')) return
    const response = await deleteIpcTestReport(runId)
    if (response.success) {
      fetchReports()
      // Clear selected report if it was deleted
      if (selectedRunId === runId) {
        setSelectedReport(null)
        setSelectedRunId(null)
      }
    } else {
      setError(response.error || 'Failed to delete report')
    }
  }

  const handleCleanAllReports = async () => {
    if (!confirm('Are you sure you want to delete ALL test reports? This cannot be undone.')) return
    const response = await cleanAllIpcTestReports()
    if (response.success) {
      fetchReports()
      // Clear selected report
      setSelectedReport(null)
      setSelectedRunId(null)
    } else {
      setError(response.error || 'Failed to clean reports')
    }
  }

  const toggleReportExpanded = useCallback((runId: string) => {
    setExpandedReports(prev => {
      const next = new Set(prev)
      if (next.has(runId)) {
        next.delete(runId)
      } else {
        next.add(runId)
      }
      return next
    })
  }, [])

  const isRunning = currentStatus?.status === 'running'

  return (
    <div className="h-full flex flex-col overflow-hidden">
      {/* Configuration Section */}
      <div className="flex-shrink-0 p-4 border-b border-white/10">
        <h2 className="text-lg font-semibold text-text-primary mb-4">IPC Tests</h2>

        {/* Test Mode Selection */}
        <div className="mb-4">
          <label className="label mb-2">Test Mode</label>
          <div className="flex flex-col gap-1.5">
            {TEST_MODES.map(m => (
              <button
                key={m.value}
                onClick={() => setMode(m.value)}
                disabled={isRunning}
                className={`px-3 py-2 rounded border text-left transition-colors flex items-center justify-between ${
                  mode === m.value
                    ? 'border-primary bg-primary/10 text-primary'
                    : 'border-white/10 hover:border-white/20 text-text-secondary'
                } ${isRunning ? 'opacity-50 cursor-not-allowed' : ''}`}
              >
                <span className="font-medium text-sm">{m.label}</span>
                <span className="text-xs opacity-70">{m.description}</span>
              </button>
            ))}
          </div>
        </div>

        {/* Mode-specific options */}
        <div className="grid grid-cols-2 gap-4 mb-4">
          {mode === 'benchmark' && (
            <div>
              <label className="label">Iterations</label>
              <input
                type="number"
                value={iterations}
                onChange={(e) => setIterations(parseInt(e.target.value) || 1)}
                disabled={isRunning}
                min={1}
                max={100}
                className="input-field-sm w-full"
              />
            </div>
          )}
          {mode === 'stress' && (
            <>
              <div>
                <label className="label">Contexts</label>
                <input
                  type="number"
                  value={contexts}
                  onChange={(e) => setContexts(parseInt(e.target.value) || 1)}
                  disabled={isRunning}
                  min={1}
                  max={100}
                  className="input-field-sm w-full"
                />
              </div>
              <div>
                <label className="label">Duration (sec)</label>
                <input
                  type="number"
                  value={duration}
                  onChange={(e) => setDuration(parseInt(e.target.value) || 60)}
                  disabled={isRunning}
                  min={10}
                  max={3600}
                  className="input-field-sm w-full"
                />
              </div>
            </>
          )}
          {mode === 'leak-check' && (
            <div>
              <label className="label">Duration (sec)</label>
              <input
                type="number"
                value={duration}
                onChange={(e) => setDuration(parseInt(e.target.value) || 60)}
                disabled={isRunning}
                min={10}
                max={3600}
                className="input-field-sm w-full"
              />
            </div>
          )}
          {mode === 'parallel' && (
            <div>
              <label className="label">Concurrency</label>
              <input
                type="number"
                value={concurrency}
                onChange={(e) => setConcurrency(parseInt(e.target.value) || 1)}
                disabled={isRunning}
                min={1}
                max={32}
                className="input-field-sm w-full"
              />
            </div>
          )}
          <div className="flex items-center">
            <label className="flex items-center gap-2 cursor-pointer">
              <input
                type="checkbox"
                checked={verbose}
                onChange={(e) => setVerbose(e.target.checked)}
                disabled={isRunning}
                className="w-4 h-4"
              />
              <span className="text-text-secondary text-sm">Verbose output</span>
            </label>
          </div>
        </div>

        {/* Action Buttons */}
        <div className="flex gap-2">
          {!isRunning ? (
            <button
              onClick={handleRunTest}
              disabled={isLoading}
              className="btn-primary flex items-center gap-2"
            >
              {isLoading ? (
                <Loader className="w-4 h-4 animate-spin" />
              ) : (
                <Play className="w-4 h-4" />
              )}
              Run Test
            </button>
          ) : (
            <button
              onClick={handleAbortTest}
              disabled={isLoading}
              className="bg-red-600 hover:bg-red-700 text-white px-4 py-2 rounded flex items-center gap-2"
            >
              <Square className="w-4 h-4" />
              Abort
            </button>
          )}
          <button
            onClick={() => {
              fetchReports()
              fetchCurrentStatus()
            }}
            className="btn-secondary flex items-center gap-2"
          >
            <RefreshCw className="w-4 h-4" />
            Refresh
          </button>
        </div>

        {error && (
          <div className="mt-3 p-2 bg-red-500/10 border border-red-500/30 rounded text-red-300 text-sm">
            {error}
          </div>
        )}
      </div>

      {/* Current Status */}
      {currentStatus && (
        <div className="flex-shrink-0 p-4 border-b border-white/10 bg-bg-secondary/50">
          <div className="flex items-center gap-3 mb-3 flex-wrap">
            <div className="flex items-center gap-2 flex-shrink-0">
              {currentStatus.status === 'running' && (
                <Loader className="w-5 h-5 text-primary animate-spin" />
              )}
              {currentStatus.status === 'completed' && (
                <CheckCircle className="w-5 h-5 text-green-500" />
              )}
              {currentStatus.status === 'failed' && (
                <XCircle className="w-5 h-5 text-red-500" />
              )}
              {currentStatus.status === 'aborted' && (
                <Square className="w-5 h-5 text-yellow-500" />
              )}
              {currentStatus.status === 'idle' && (
                <Clock className="w-5 h-5 text-text-muted" />
              )}
              <span className="text-text-primary font-medium whitespace-nowrap">
                {currentStatus.status === 'running' ? 'Running:' : 'Last run:'}
              </span>
            </div>
            <span className="text-text-primary font-mono text-sm truncate max-w-[180px]" title={currentStatus.run_id}>
              {currentStatus.run_id}
            </span>
            <span className="text-text-muted text-sm capitalize flex-shrink-0">({currentStatus.status})</span>
          </div>
          {currentStatus.total_tests > 0 && (
            <div className="flex flex-wrap gap-x-6 gap-y-1 text-sm">
              <div className="whitespace-nowrap">
                <span className="text-text-muted">Total:</span>{' '}
                <span className="text-text-primary font-medium">{currentStatus.total_tests}</span>
              </div>
              <div className="whitespace-nowrap">
                <span className="text-text-muted">Passed:</span>{' '}
                <span className="text-green-400 font-medium">{currentStatus.passed_tests}</span>
              </div>
              <div className="whitespace-nowrap">
                <span className="text-text-muted">Failed:</span>{' '}
                <span className="text-red-400 font-medium">{currentStatus.failed_tests}</span>
              </div>
              <div className="whitespace-nowrap">
                <span className="text-text-muted">Duration:</span>{' '}
                <span className="text-text-primary font-medium">{currentStatus.duration_seconds.toFixed(2)}s</span>
              </div>
            </div>
          )}
          {currentStatus.error_message && (
            <div className="mt-2 text-sm text-red-400">{currentStatus.error_message}</div>
          )}
        </div>
      )}

      {/* Reports List */}
      <div className="flex-1 overflow-auto p-4">
        <div className="flex items-center justify-between mb-3">
          <h3 className="text-sm font-medium text-text-secondary flex items-center gap-2">
            <FileText className="w-4 h-4" />
            Test Reports
          </h3>
          {reports.length > 0 && (
            <button
              onClick={handleCleanAllReports}
              className="text-xs text-red-400 hover:text-red-300 flex items-center gap-1"
            >
              <Trash2 className="w-3 h-3" />
              Clean All
            </button>
          )}
        </div>
        {reports.length === 0 ? (
          <div className="text-text-muted text-sm">No test reports yet</div>
        ) : (
          <div className="space-y-2">
            {reports.map((report) => {
              const isExpanded = expandedReports.has(report.run_id)
              const passRate = report.total_tests > 0
                ? ((report.passed_tests / report.total_tests) * 100).toFixed(1)
                : '0'
              const allPassed = report.failed_tests === 0 && report.total_tests > 0

              return (
                <div
                  key={report.run_id}
                  className="border border-white/10 rounded overflow-hidden"
                >
                  {/* Report Header */}
                  <div
                    className="flex items-center gap-2 p-3 bg-bg-secondary/50 cursor-pointer hover:bg-bg-secondary"
                    onClick={() => toggleReportExpanded(report.run_id)}
                  >
                    <div className="flex-shrink-0">
                      {isExpanded ? (
                        <ChevronDown className="w-4 h-4 text-text-muted" />
                      ) : (
                        <ChevronRight className="w-4 h-4 text-text-muted" />
                      )}
                    </div>
                    <div className="flex-shrink-0">
                      {allPassed ? (
                        <CheckCircle className="w-4 h-4 text-green-500" />
                      ) : (
                        <XCircle className="w-4 h-4 text-red-500" />
                      )}
                    </div>
                    <span className="text-text-primary font-mono text-sm flex-1 truncate min-w-0" title={report.run_id}>
                      {report.run_id}
                    </span>
                    <span className="text-text-muted text-xs flex-shrink-0 px-1">{report.mode}</span>
                    <span
                      className={`text-sm font-medium flex-shrink-0 min-w-[45px] text-right ${allPassed ? 'text-green-400' : 'text-yellow-400'}`}
                    >
                      {passRate}%
                    </span>
                  </div>

                  {/* Report Details */}
                  {isExpanded && (
                    <div className="p-3 border-t border-white/10">
                      <div className="flex flex-wrap gap-x-4 gap-y-2 text-sm mb-3">
                        <div className="whitespace-nowrap">
                          <span className="text-text-muted">Timestamp:</span>{' '}
                          <span className="text-text-primary font-mono text-xs">
                            {report.timestamp ? new Date(report.timestamp).toLocaleString() : 'N/A'}
                          </span>
                        </div>
                        <div className="whitespace-nowrap">
                          <span className="text-text-muted">Duration:</span>{' '}
                          <span className="text-text-primary font-medium">{report.duration_seconds.toFixed(2)}s</span>
                        </div>
                        <div className="whitespace-nowrap">
                          <span className="text-text-muted">Total:</span>{' '}
                          <span className="text-text-primary font-medium">{report.total_tests}</span>
                        </div>
                        <div className="whitespace-nowrap">
                          <span className="text-text-muted">Passed:</span>{' '}
                          <span className="text-green-400 font-medium">{report.passed_tests}</span>
                          <span className="text-text-muted"> / </span>
                          <span className="text-text-muted">Failed:</span>{' '}
                          <span className="text-red-400 font-medium">{report.failed_tests}</span>
                        </div>
                      </div>
                      <div className="flex gap-2">
                        <button
                          onClick={(e) => {
                            e.stopPropagation()
                            loadReport(report.run_id)
                          }}
                          className={`text-xs px-3 py-1.5 rounded flex items-center gap-1.5 font-medium transition-colors ${
                            selectedRunId === report.run_id
                              ? 'bg-primary text-white'
                              : 'bg-bg-tertiary text-text-secondary hover:bg-bg-secondary border border-white/10'
                          }`}
                        >
                          <Eye className="w-3.5 h-3.5" />
                          {selectedRunId === report.run_id ? 'Viewing' : 'View'}
                        </button>
                        <button
                          onClick={async (e) => {
                            e.stopPropagation()
                            const result = await downloadIpcTestHtmlReport(report.run_id)
                            if (!result.success) {
                              setError(result.error || 'Failed to download report')
                            }
                          }}
                          className="text-xs px-3 py-1.5 rounded flex items-center gap-1.5 font-medium bg-bg-tertiary text-text-secondary hover:bg-bg-secondary border border-white/10 transition-colors"
                        >
                          <Download className="w-3.5 h-3.5" />
                          Download
                        </button>
                        <button
                          onClick={(e) => {
                            e.stopPropagation()
                            handleDeleteReport(report.run_id)
                          }}
                          className="text-xs px-3 py-1.5 rounded flex items-center gap-1.5 font-medium text-red-400 hover:text-red-300 hover:bg-red-500/10 transition-colors"
                        >
                          <Trash2 className="w-3.5 h-3.5" />
                          Delete
                        </button>
                      </div>
                    </div>
                  )}
                </div>
              )
            })}
          </div>
        )}
      </div>
    </div>
  )
}
