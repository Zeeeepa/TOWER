import { useMemo } from 'react'
import {
  CheckCircle,
  XCircle,
  Clock,
  Cpu,
  HardDrive,
  Zap,
  BarChart3,
  AlertTriangle,
  Activity,
  Loader,
} from 'lucide-react'
import type {
  IpcTestFullReport,
  IpcTestStatus,
} from '../api/browserApi'

interface IPCTestResultsViewProps {
  status: IpcTestStatus | null
  report: IpcTestFullReport | null
  isLoading: boolean
}

// Simple SVG Bar Chart component
function BarChart({
  data,
  labels,
  title,
  unit = 'ms',
  color = '#22c55e',
}: {
  data: number[]
  labels: string[]
  title: string
  unit?: string
  color?: string
}) {
  const maxValue = Math.max(...data, 1)

  return (
    <div className="bg-bg-secondary/50 rounded-lg p-4">
      <h4 className="text-sm font-medium text-text-secondary mb-3">{title}</h4>
      <div className="flex items-end gap-2 h-32">
        {data.map((value, i) => (
          <div key={labels[i]} className="flex-1 flex flex-col items-center">
            <div className="text-xs text-text-muted mb-1">
              {value.toFixed(1)}{unit}
            </div>
            <div
              className="w-full rounded-t transition-all"
              style={{
                height: `${(value / maxValue) * 100}%`,
                backgroundColor: color,
                minHeight: value > 0 ? '4px' : '0',
              }}
            />
            <div className="text-xs text-text-muted mt-1 truncate w-full text-center">
              {labels[i]}
            </div>
          </div>
        ))}
      </div>
    </div>
  )
}

// Simple Line Chart for timeline data
function TimelineChart({
  data,
  title,
  color = '#22c55e',
  unit = 'MB',
}: {
  data: { x: number; y: number }[]
  title: string
  color?: string
  unit?: string
}) {
  if (data.length === 0) return null

  const maxY = Math.max(...data.map((d) => d.y), 1)
  const minX = Math.min(...data.map((d) => d.x))
  const maxX = Math.max(...data.map((d) => d.x))
  const rangeX = maxX - minX || 1

  const points = data
    .map((d) => {
      const x = ((d.x - minX) / rangeX) * 100
      const y = 100 - (d.y / maxY) * 100
      return `${x},${y}`
    })
    .join(' ')

  return (
    <div className="bg-bg-secondary/50 rounded-lg p-4">
      <div className="flex justify-between items-center mb-3">
        <h4 className="text-sm font-medium text-text-secondary">{title}</h4>
        <span className="text-xs text-text-muted">
          Peak: {maxY.toFixed(1)} {unit}
        </span>
      </div>
      <svg viewBox="0 0 100 60" className="w-full h-24" preserveAspectRatio="none">
        <polyline
          points={points}
          fill="none"
          stroke={color}
          strokeWidth="1"
          vectorEffect="non-scaling-stroke"
        />
        <polyline
          points={`0,100 ${points} 100,100`}
          fill={`${color}20`}
          stroke="none"
        />
      </svg>
    </div>
  )
}

// Category progress bar
function CategoryBar({
  name,
  passed,
  failed,
  total,
  avgLatency,
}: {
  name: string
  passed: number
  failed: number
  total: number
  avgLatency: number
}) {
  return (
    <div className="flex items-center gap-3 py-2">
      <div className="w-32 text-sm text-text-secondary truncate" title={name}>
        {name}
      </div>
      <div className="flex-1 h-4 bg-bg-tertiary rounded overflow-hidden flex">
        <div
          className="bg-green-500 h-full transition-all"
          style={{ width: `${(passed / total) * 100}%` }}
        />
        <div
          className="bg-red-500 h-full transition-all"
          style={{ width: `${(failed / total) * 100}%` }}
        />
      </div>
      <div className="w-16 text-xs text-right">
        <span className="text-green-400">{passed}</span>
        <span className="text-text-muted">/</span>
        <span className={failed > 0 ? 'text-red-400' : 'text-text-muted'}>{total}</span>
      </div>
      <div className="w-20 text-xs text-text-muted text-right">
        {avgLatency.toFixed(1)}ms
      </div>
    </div>
  )
}

// Stat card component
function StatCard({
  icon: Icon,
  label,
  value,
  subValue,
  color = 'text-primary',
}: {
  icon: React.ElementType
  label: string
  value: string | number
  subValue?: string
  color?: string
}) {
  return (
    <div className="bg-bg-secondary/50 rounded-lg p-4 flex items-start gap-3">
      <div className={`p-2 rounded-lg bg-bg-tertiary ${color}`}>
        <Icon className="w-5 h-5" />
      </div>
      <div>
        <div className="text-xs text-text-muted">{label}</div>
        <div className="text-xl font-semibold text-text-primary">{value}</div>
        {subValue && <div className="text-xs text-text-muted">{subValue}</div>}
      </div>
    </div>
  )
}

export default function IPCTestResultsView({
  status,
  report,
  isLoading,
}: IPCTestResultsViewProps) {
  // Prepare chart data
  const latencyData = useMemo(() => {
    if (!report?.latency_stats) return { data: [], labels: [] }
    const stats = report.latency_stats
    return {
      data: [stats.min_ms, stats.avg_ms, stats.median_ms, stats.p95_ms, stats.p99_ms, stats.max_ms],
      labels: ['Min', 'Avg', 'Median', 'P95', 'P99', 'Max'],
    }
  }, [report])

  const memoryTimeline = useMemo(() => {
    if (!report?.resource_timeline) return []
    return report.resource_timeline.map((s) => ({
      x: s.timestamp_ms,
      y: s.memory_mb,
    }))
  }, [report])

  const cpuTimeline = useMemo(() => {
    if (!report?.resource_timeline) return []
    return report.resource_timeline.map((s) => ({
      x: s.timestamp_ms,
      y: s.cpu_percent,
    }))
  }, [report])

  const categories = useMemo(() => {
    if (!report?.by_category) return []
    return Object.entries(report.by_category).map(([name, stats]) => ({
      name,
      ...stats,
    }))
  }, [report])

  // Show loading state
  if (isLoading) {
    return (
      <div className="h-full flex items-center justify-center">
        <div className="flex flex-col items-center gap-4">
          <Loader className="w-12 h-12 text-primary animate-spin" />
          <p className="text-text-secondary">Loading test results...</p>
        </div>
      </div>
    )
  }

  // Show running state
  if (status?.status === 'running') {
    return (
      <div className="h-full flex items-center justify-center">
        <div className="flex flex-col items-center gap-4 p-8">
          <div className="relative">
            <Loader className="w-16 h-16 text-primary animate-spin" />
            <Activity className="w-8 h-8 text-primary absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2" />
          </div>
          <div className="text-center">
            <p className="text-lg font-medium text-text-primary">Running IPC Tests</p>
            <p className="text-sm text-text-muted mt-1">{status.run_id}</p>
          </div>
          {status.total_tests > 0 && (
            <div className="grid grid-cols-3 gap-6 mt-4">
              <div className="text-center">
                <div className="text-2xl font-bold text-text-primary">{status.total_tests}</div>
                <div className="text-xs text-text-muted">Total</div>
              </div>
              <div className="text-center">
                <div className="text-2xl font-bold text-green-400">{status.passed_tests}</div>
                <div className="text-xs text-text-muted">Passed</div>
              </div>
              <div className="text-center">
                <div className="text-2xl font-bold text-red-400">{status.failed_tests}</div>
                <div className="text-xs text-text-muted">Failed</div>
              </div>
            </div>
          )}
        </div>
      </div>
    )
  }

  // Show empty state
  if (!report && !status) {
    return (
      <div className="h-full flex items-center justify-center">
        <div className="flex flex-col items-center gap-4 text-center p-8">
          <BarChart3 className="w-16 h-16 text-text-muted opacity-50" />
          <div>
            <p className="text-lg font-medium text-text-secondary">No Test Results</p>
            <p className="text-sm text-text-muted mt-1">
              Run an IPC test to see results and charts here
            </p>
          </div>
        </div>
      </div>
    )
  }

  // Show results
  const passRate = report?.summary
    ? ((report.summary.passed / report.summary.total_tests) * 100).toFixed(1)
    : status
    ? ((status.passed_tests / Math.max(status.total_tests, 1)) * 100).toFixed(1)
    : '0'

  const allPassed = report
    ? report.summary.failed === 0
    : status
    ? status.failed_tests === 0
    : false

  return (
    <div className="h-full overflow-auto p-4 space-y-4">
      {/* Header with status */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-3">
          {allPassed ? (
            <CheckCircle className="w-6 h-6 text-green-500" />
          ) : (
            <XCircle className="w-6 h-6 text-red-500" />
          )}
          <div>
            <h2 className="text-lg font-semibold text-text-primary">
              {report?.metadata?.test_run_id || status?.run_id || 'Test Results'}
            </h2>
            <p className="text-xs text-text-muted">
              {report?.metadata?.timestamp || status?.status}
            </p>
          </div>
        </div>
        <div className={`text-3xl font-bold ${allPassed ? 'text-green-400' : 'text-yellow-400'}`}>
          {passRate}%
        </div>
      </div>

      {/* Summary stats */}
      <div className="grid grid-cols-2 lg:grid-cols-4 gap-3">
        <StatCard
          icon={CheckCircle}
          label="Tests Passed"
          value={report?.summary.passed ?? status?.passed_tests ?? 0}
          subValue={`of ${report?.summary.total_tests ?? status?.total_tests ?? 0}`}
          color="text-green-400"
        />
        <StatCard
          icon={XCircle}
          label="Tests Failed"
          value={report?.summary.failed ?? status?.failed_tests ?? 0}
          color={
            (report?.summary.failed ?? status?.failed_tests ?? 0) > 0
              ? 'text-red-400'
              : 'text-text-muted'
          }
        />
        <StatCard
          icon={Clock}
          label="Duration"
          value={`${(report?.summary.total_duration_sec ?? status?.duration_seconds ?? 0).toFixed(2)}s`}
        />
        <StatCard
          icon={Zap}
          label="Throughput"
          value={`${(report?.summary.commands_per_second ?? status?.commands_per_second ?? 0).toFixed(1)}`}
          subValue="cmd/sec"
          color="text-primary"
        />
      </div>

      {/* Charts row */}
      {report && (
        <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
          {/* Latency distribution */}
          <BarChart
            data={latencyData.data}
            labels={latencyData.labels}
            title="Latency Distribution"
            unit="ms"
            color="#3b82f6"
          />

          {/* Resource stats */}
          <div className="bg-bg-secondary/50 rounded-lg p-4">
            <h4 className="text-sm font-medium text-text-secondary mb-3">Resource Usage</h4>
            <div className="grid grid-cols-2 gap-4">
              <div className="flex items-center gap-2">
                <HardDrive className="w-4 h-4 text-blue-400" />
                <div>
                  <div className="text-xs text-text-muted">Peak Memory</div>
                  <div className="text-sm font-medium text-text-primary">
                    {report.resource_stats?.peak_memory_mb?.toFixed(1) ?? 'N/A'} MB
                  </div>
                </div>
              </div>
              <div className="flex items-center gap-2">
                <HardDrive className="w-4 h-4 text-green-400" />
                <div>
                  <div className="text-xs text-text-muted">Avg Memory</div>
                  <div className="text-sm font-medium text-text-primary">
                    {report.resource_stats?.avg_memory_mb?.toFixed(1) ?? 'N/A'} MB
                  </div>
                </div>
              </div>
              <div className="flex items-center gap-2">
                <Cpu className="w-4 h-4 text-orange-400" />
                <div>
                  <div className="text-xs text-text-muted">Peak CPU</div>
                  <div className="text-sm font-medium text-text-primary">
                    {report.resource_stats?.peak_cpu_percent?.toFixed(1) ?? 'N/A'}%
                  </div>
                </div>
              </div>
              <div className="flex items-center gap-2">
                <Cpu className="w-4 h-4 text-yellow-400" />
                <div>
                  <div className="text-xs text-text-muted">Avg CPU</div>
                  <div className="text-sm font-medium text-text-primary">
                    {report.resource_stats?.avg_cpu_percent?.toFixed(1) ?? 'N/A'}%
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>
      )}

      {/* Timeline charts */}
      {report && memoryTimeline.length > 0 && (
        <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
          <TimelineChart
            data={memoryTimeline}
            title="Memory Over Time"
            color="#3b82f6"
            unit="MB"
          />
          <TimelineChart
            data={cpuTimeline}
            title="CPU Over Time"
            color="#f59e0b"
            unit="%"
          />
        </div>
      )}

      {/* Category breakdown */}
      {categories.length > 0 && (
        <div className="bg-bg-secondary/50 rounded-lg p-4">
          <h4 className="text-sm font-medium text-text-secondary mb-3">Results by Category</h4>
          <div className="space-y-1">
            {categories.map((cat) => (
              <CategoryBar
                key={cat.name}
                name={cat.name}
                passed={cat.passed}
                failed={cat.failed}
                total={cat.total}
                avgLatency={cat.avg_latency_ms}
              />
            ))}
          </div>
        </div>
      )}

      {/* Failures */}
      {report && report.failures.length > 0 && (
        <div className="bg-red-500/10 border border-red-500/30 rounded-lg p-4">
          <div className="flex items-center gap-2 mb-3">
            <AlertTriangle className="w-4 h-4 text-red-400" />
            <h4 className="text-sm font-medium text-red-300">
              Failed Tests ({report.failures.length})
            </h4>
          </div>
          <div className="space-y-2 max-h-48 overflow-auto">
            {report.failures.map((failure, i) => (
              <div
                key={i}
                className="bg-bg-primary/50 rounded p-2 text-sm"
              >
                <div className="font-mono text-red-300">{failure.method}</div>
                <div className="text-text-muted text-xs mt-1">{failure.message}</div>
                <div className="text-xs text-text-muted mt-1">
                  Expected: <span className="text-green-400">{failure.expected}</span>
                  {' | '}
                  Actual: <span className="text-red-400">{failure.actual}</span>
                </div>
              </div>
            ))}
          </div>
        </div>
      )}

      {/* Metadata */}
      {report?.metadata && (
        <div className="bg-bg-secondary/50 rounded-lg p-4 text-xs text-text-muted">
          <div className="grid grid-cols-2 lg:grid-cols-4 gap-2">
            <div>
              <span className="text-text-secondary">Platform:</span>{' '}
              {report.metadata.platform} {report.metadata.platform_version}
            </div>
            <div>
              <span className="text-text-secondary">Browser:</span>{' '}
              {report.metadata.browser_version}
            </div>
            <div>
              <span className="text-text-secondary">CPU:</span>{' '}
              {report.metadata.cpu_model}
            </div>
            <div>
              <span className="text-text-secondary">Memory:</span>{' '}
              {report.metadata.total_memory_gb?.toFixed(1)} GB
            </div>
          </div>
        </div>
      )}
    </div>
  )
}
