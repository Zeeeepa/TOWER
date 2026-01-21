import type { ApiResponse, IpcResponse, Tool, ToolParam } from '../types/browser'

const API_BASE = '/api'

function getToken(): string | null {
  return localStorage.getItem('owl_token')
}

export function setToken(token: string): void {
  localStorage.setItem('owl_token', token)
  // Also set as cookie for video stream requests (img tags can't set headers)
  document.cookie = `owl_token=${token}; path=/; SameSite=Strict`
}

export function clearToken(): void {
  localStorage.removeItem('owl_token')
  // Also clear the cookie
  document.cookie = 'owl_token=; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT'
}

export function hasToken(): boolean {
  const token = getToken()
  if (token) {
    // Ensure cookie is synced with localStorage (for existing sessions)
    document.cookie = `owl_token=${token}; path=/; SameSite=Strict`
  }
  return !!token
}

async function fetchWithAuth<T>(url: string, options: RequestInit = {}): Promise<ApiResponse<T>> {
  const token = getToken()
  const headers: Record<string, string> = {
    'Content-Type': 'application/json',
    ...(options.headers as Record<string, string>),
  }

  if (token) {
    headers['Authorization'] = `Bearer ${token}`
  }

  try {
    const response = await fetch(url, {
      ...options,
      headers,
    })

    const data = await response.json()
    return data as ApiResponse<T>
  } catch (error) {
    return {
      success: false,
      error: error instanceof Error ? error.message : 'Unknown error',
    }
  }
}

// Auth endpoints
export async function login(password: string): Promise<ApiResponse<{ token: string }>> {
  return fetchWithAuth<{ token: string }>(`${API_BASE}/auth`, {
    method: 'POST',
    body: JSON.stringify({ password }),
  })
}

export async function verifyToken(): Promise<ApiResponse<{ valid: boolean }>> {
  return fetchWithAuth<{ valid: boolean }>(`${API_BASE}/auth/verify`)
}

// Health check
export async function checkHealth(): Promise<ApiResponse<{ status: string; browser_ready: boolean; browser_state: string }>> {
  try {
    const response = await fetch(`${API_BASE}/health`)
    return await response.json()
  } catch {
    return { success: false, error: 'Failed to connect to server' }
  }
}

// Tools endpoints
export async function listTools(): Promise<ApiResponse<{ tools: Tool[] }>> {
  return fetchWithAuth<{ tools: Tool[] }>(`${API_BASE}/tools`)
}

export async function getToolInfo(toolName: string): Promise<ApiResponse<Tool & { parameters: ToolParam[] }>> {
  return fetchWithAuth<Tool & { parameters: ToolParam[] }>(`${API_BASE}/tools/${toolName}`)
}

// Execute tool
export async function executeTool<T = unknown>(toolName: string, params: Record<string, unknown> = {}): Promise<ApiResponse<T>> {
  return fetchWithAuth<T>(`${API_BASE}/execute/${toolName}`, {
    method: 'POST',
    body: JSON.stringify(params),
  })
}

// Video stream URL
export function getVideoStreamUrl(contextId: string, fps = 15): string {
  // Auth is handled via cookie (owl_token) which browser sends automatically
  return `/video/stream/${contextId}?fps=${fps}`
}

// Video frame URL
export function getVideoFrameUrl(contextId: string): string {
  return `/video/frame/${contextId}`
}

// Helper to unwrap IPC response from API response
// Old format: {success: true, result: {id: N, result: <actual_data>}}
// New format: {success: true, result: <actual_data>}
// This handles both formats and extracts the actual data
function unwrapIpcResponse<T>(response: ApiResponse<IpcResponse<T> | T>): ApiResponse<T> {
  if (response.success && response.result !== undefined) {
    // Check if result is an object with nested 'result' property (old IPC format)
    if (typeof response.result === 'object' && response.result !== null && 'result' in response.result) {
      return {
        success: true,
        result: (response.result as IpcResponse<T>).result,
      }
    }
    // Otherwise, result is already the final value (new flat format)
    return {
      success: true,
      result: response.result as T,
    }
  }
  return {
    success: response.success,
    error: response.error,
  }
}

// Context creation result type (now returns full context info)
export interface ContextCreationResult {
  context_id: string
  vm_profile?: {
    vm_id: string
    platform: string
    user_agent: string
    hardware_concurrency: number
    device_memory: number
    screen_width: number
    screen_height: number
    timezone: string
    locale: string
  }
  seeds?: Record<string, number>
  hashes?: Record<string, string>
  canvas?: { hash_seed: number; noise_seed: number }
  audio?: { noise_seed: number }
  gpu?: { profile_index: number; webgl_vendor: string; webgl_renderer: string }
  has_profile?: boolean
}

// Convenience wrappers for common operations
// Note: These unwrap the IPC response to return the actual data directly
export async function createContext(options: Record<string, unknown> = {}): Promise<ApiResponse<ContextCreationResult>> {
  const response = await executeTool<IpcResponse<ContextCreationResult>>('browser_create_context', options)
  return unwrapIpcResponse(response)
}

export async function closeContext(contextId: string): Promise<ApiResponse<boolean>> {
  console.log('[API] closeContext called with:', contextId)
  const response = await executeTool<IpcResponse<boolean>>('browser_close_context', { context_id: contextId })
  console.log('[API] closeContext response:', response)
  return unwrapIpcResponse(response)
}

export async function listContexts(): Promise<ApiResponse<string[]>> {
  const response = await executeTool<IpcResponse<string[]>>('browser_list_contexts')
  return unwrapIpcResponse(response)
}

export async function navigate(contextId: string, url: string): Promise<ApiResponse<boolean>> {
  const response = await executeTool<IpcResponse<boolean>>('browser_navigate', { context_id: contextId, url })
  return unwrapIpcResponse(response)
}

export async function screenshot(contextId: string): Promise<ApiResponse<string>> {
  const response = await executeTool<IpcResponse<string>>('browser_screenshot', { context_id: contextId })
  return unwrapIpcResponse(response)
}

export async function startLiveStream(contextId: string, fps = 15, quality = 75): Promise<ApiResponse<boolean>> {
  const response = await executeTool<IpcResponse<boolean>>('browser_start_live_stream', { context_id: contextId, fps, quality })
  return unwrapIpcResponse(response)
}

export async function stopLiveStream(contextId: string): Promise<ApiResponse<boolean>> {
  const response = await executeTool<IpcResponse<boolean>>('browser_stop_live_stream', { context_id: contextId })
  return unwrapIpcResponse(response)
}

interface ElementAtPositionResult {
  tagName: string
  id: string
  className: string
  textContent: string
  selector: string
}

export async function getElementAtPosition(contextId: string, x: number, y: number): Promise<ApiResponse<ElementAtPositionResult>> {
  const response = await executeTool<IpcResponse<ElementAtPositionResult>>('browser_get_element_at_position', { context_id: contextId, x, y })
  return unwrapIpcResponse(response)
}

interface PageInfoResult {
  url: string
  title: string
  can_go_back: boolean
  can_go_forward: boolean
}

export async function getPageInfo(contextId: string): Promise<ApiResponse<PageInfoResult>> {
  const response = await executeTool<IpcResponse<PageInfoResult>>('browser_get_page_info', { context_id: contextId })
  return unwrapIpcResponse(response)
}

// IPC Tests API
export interface IpcTestRunConfig {
  mode?: 'smoke' | 'full' | 'benchmark' | 'stress' | 'leak-check' | 'parallel'
  verbose?: boolean
  iterations?: number
  contexts?: number
  duration?: number
  concurrency?: number
}

export interface IpcTestRunResult {
  run_id: string
  status: string
  mode: string
}

export interface IpcTestStatus {
  run_id: string
  status: 'idle' | 'running' | 'completed' | 'failed' | 'aborted'
  exit_code: number
  total_tests: number
  passed_tests: number
  failed_tests: number
  skipped_tests: number
  duration_seconds: number
  commands_per_second: number
  error_message?: string
}

export interface IpcTestReportInfo {
  run_id: string
  timestamp: string
  mode: string
  total_tests: number
  passed_tests: number
  failed_tests: number
  duration_seconds: number
}

// Full JSON report types
export interface IpcTestReportMetadata {
  test_run_id: string
  timestamp: string
  browser_version: string
  browser_path: string
  platform: string
  platform_version: string
  cpu_model: string
  total_memory_gb: number
}

export interface IpcTestLatencyStats {
  min_ms: number
  max_ms: number
  avg_ms: number
  median_ms: number
  p95_ms: number
  p99_ms: number
  stddev_ms: number
}

export interface IpcTestResourceStats {
  peak_memory_mb: number
  avg_memory_mb: number
  peak_cpu_percent: number
  avg_cpu_percent: number
}

export interface IpcTestCategoryStats {
  total: number
  passed: number
  failed: number
  avg_latency_ms: number
}

export interface IpcTestCommand {
  method: string
  category: string
  params: Record<string, unknown>
  success: boolean
  latency_ms: number
  response_size_bytes: number
  status: string
  memory_before_mb: number
  memory_after_mb: number
}

export interface IpcTestResourceSample {
  timestamp_ms: number
  memory_mb: number
  vms_mb: number
  cpu_percent: number
  cpu_user_sec: number
  cpu_system_sec: number
}

export interface IpcTestFailure {
  method: string
  params: Record<string, unknown>
  expected: string
  actual: string
  message: string
}

export interface IpcTestSummary {
  total_tests: number
  passed: number
  failed: number
  skipped: number
  total_duration_sec: number
  commands_per_second: number
}

export interface IpcTestFullReport {
  metadata: IpcTestReportMetadata
  summary: IpcTestSummary
  latency_stats: IpcTestLatencyStats
  resource_stats: IpcTestResourceStats
  by_category: Record<string, IpcTestCategoryStats>
  commands: IpcTestCommand[]
  resource_timeline: IpcTestResourceSample[]
  failures: IpcTestFailure[]
}

export async function runIpcTest(config: IpcTestRunConfig = {}): Promise<ApiResponse<IpcTestRunResult>> {
  return executeTool<IpcTestRunResult>('ipc_tests_run', config as Record<string, unknown>)
}

export async function getIpcTestStatus(runId?: string): Promise<ApiResponse<IpcTestStatus>> {
  return executeTool<IpcTestStatus>('ipc_tests_status', runId ? { run_id: runId } : {})
}

export async function abortIpcTest(runId: string): Promise<ApiResponse<void>> {
  return executeTool<void>('ipc_tests_abort', { run_id: runId })
}

export async function listIpcTestReports(): Promise<ApiResponse<IpcTestReportInfo[]>> {
  return executeTool<IpcTestReportInfo[]>('ipc_tests_list_reports')
}

export async function getIpcTestReport(runId: string, format: 'json' | 'html' = 'json'): Promise<ApiResponse<IpcTestFullReport>> {
  return executeTool<IpcTestFullReport>('ipc_tests_get_report', { run_id: runId, format })
}

export async function deleteIpcTestReport(runId: string): Promise<ApiResponse<void>> {
  return executeTool<void>('ipc_tests_delete_report', { run_id: runId })
}

export async function cleanAllIpcTestReports(): Promise<ApiResponse<{ deleted_count: number }>> {
  return executeTool<{ deleted_count: number }>('ipc_tests_clean_all')
}

export function getIpcTestHtmlReportUrl(runId: string): string {
  const token = getToken()
  return `${API_BASE}/execute/ipc_tests_get_report?run_id=${runId}&format=html&token=${token}`
}

/**
 * Download the HTML report for a test run.
 * Fetches via POST and triggers a browser download.
 */
export async function downloadIpcTestHtmlReport(runId: string): Promise<{ success: boolean; error?: string }> {
  const token = getToken()
  const headers: Record<string, string> = {
    'Content-Type': 'application/json',
  }

  if (token) {
    headers['Authorization'] = `Bearer ${token}`
  }

  try {
    const response = await fetch(`${API_BASE}/execute/ipc_tests_get_report`, {
      method: 'POST',
      headers,
      body: JSON.stringify({ run_id: runId, format: 'html' }),
    })

    if (!response.ok) {
      const errorData = await response.json().catch(() => ({}))
      return { success: false, error: errorData.error || `HTTP ${response.status}` }
    }

    // Get the HTML content
    const htmlContent = await response.text()

    // Create a blob and trigger download
    const blob = new Blob([htmlContent], { type: 'text/html' })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = `ipc-test-report-${runId}.html`
    document.body.appendChild(a)
    a.click()
    document.body.removeChild(a)
    URL.revokeObjectURL(url)

    return { success: true }
  } catch (error) {
    return {
      success: false,
      error: error instanceof Error ? error.message : 'Unknown error',
    }
  }
}
