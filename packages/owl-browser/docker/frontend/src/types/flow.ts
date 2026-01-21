// Flow Designer Types
// Matches browser playground format from src/ui/owl_playground.cc

// Condition operators for comparing values
export type ConditionOperator =
  | 'equals'
  | 'not_equals'
  | 'contains'
  | 'not_contains'
  | 'starts_with'
  | 'ends_with'
  | 'greater_than'
  | 'less_than'
  | 'is_truthy'
  | 'is_falsy'
  | 'is_empty'
  | 'is_not_empty'
  | 'regex_match'

// Condition configuration
export interface FlowCondition {
  // What to check: 'previous' checks last step result, 'step' checks specific step
  source: 'previous' | 'step'
  // Step ID to check (when source is 'step')
  sourceStepId?: string
  // Field path in result to check (e.g., 'success', 'message', 'data.count')
  // Empty means check the entire result
  field?: string
  // Comparison operator
  operator: ConditionOperator
  // Value to compare against (not needed for is_truthy, is_falsy, is_empty, is_not_empty)
  value?: unknown
}

export interface FlowStep {
  id: string
  type: string
  enabled: boolean
  params: Record<string, unknown>
  // Condition: if present, step only runs if condition is true
  // For branching, use type='condition' with onTrue/onFalse
  condition?: FlowCondition
  // For type='condition': branches to execute
  onTrue?: FlowStep[]
  onFalse?: FlowStep[]
  // Execution state
  status?: 'pending' | 'running' | 'success' | 'warning' | 'error' | 'skipped'
  result?: unknown
  error?: string
  duration?: number
  // For condition steps: which branch was taken
  branchTaken?: 'true' | 'false' | null
}

export interface Flow {
  id: string
  name: string
  description: string
  steps: FlowStep[]
  createdAt: number
  updatedAt: number
}

export interface FlowExecution {
  flowId: string
  status: 'idle' | 'running' | 'completed' | 'error'
  currentStepIndex: number
  startedAt?: number
  completedAt?: number
  results: FlowStepResult[]
}

export interface FlowStepResult {
  stepId: string
  stepIndex: number
  status: 'success' | 'warning' | 'error'
  result?: unknown
  error?: string
  duration: number
  timestamp: number
}

// Condition operators with labels for UI
export const CONDITION_OPERATORS: { value: ConditionOperator; label: string; needsValue: boolean }[] = [
  { value: 'equals', label: 'Equals', needsValue: true },
  { value: 'not_equals', label: 'Not Equals', needsValue: true },
  { value: 'contains', label: 'Contains', needsValue: true },
  { value: 'not_contains', label: 'Does Not Contain', needsValue: true },
  { value: 'starts_with', label: 'Starts With', needsValue: true },
  { value: 'ends_with', label: 'Ends With', needsValue: true },
  { value: 'greater_than', label: 'Greater Than', needsValue: true },
  { value: 'less_than', label: 'Less Than', needsValue: true },
  { value: 'is_truthy', label: 'Is Truthy', needsValue: false },
  { value: 'is_falsy', label: 'Is Falsy', needsValue: false },
  { value: 'is_empty', label: 'Is Empty', needsValue: false },
  { value: 'is_not_empty', label: 'Is Not Empty', needsValue: false },
  { value: 'regex_match', label: 'Matches Regex', needsValue: true },
]

// Step type categories for the UI - matches browser playground exactly
export const STEP_CATEGORIES = {
  controlFlow: {
    label: 'Control Flow',
    steps: [
      { type: 'condition', label: 'If Condition', icon: 'GitBranch' },
    ],
  },
  navigation: {
    label: 'Navigation',
    steps: [
      { type: 'browser_navigate', label: 'Navigate', icon: 'Globe' },
      { type: 'browser_reload', label: 'Reload Page', icon: 'RefreshCw' },
      { type: 'browser_go_back', label: 'Go Back', icon: 'ArrowLeft' },
      { type: 'browser_go_forward', label: 'Go Forward', icon: 'ArrowRight' },
    ],
  },
  interaction: {
    label: 'Interaction',
    steps: [
      { type: 'browser_click', label: 'Click', icon: 'MousePointer' },
      { type: 'browser_type', label: 'Type', icon: 'Type' },
      { type: 'browser_pick', label: 'Pick from Dropdown', icon: 'List' },
      { type: 'browser_submit_form', label: 'Submit Form', icon: 'Send' },
      { type: 'browser_press_key', label: 'Press Key', icon: 'Keyboard' },
      { type: 'browser_drag_drop', label: 'Drag & Drop (Coordinates)', icon: 'Move' },
      { type: 'browser_html5_drag_drop', label: 'HTML5 Drag & Drop (Selectors)', icon: 'Move' },
      { type: 'browser_mouse_move', label: 'Mouse Move (Human-like)', icon: 'MousePointer2' },
      { type: 'browser_hover', label: 'Hover', icon: 'MousePointer2' },
      { type: 'browser_double_click', label: 'Double Click', icon: 'MousePointer' },
      { type: 'browser_right_click', label: 'Right Click', icon: 'MousePointer' },
      { type: 'browser_clear_input', label: 'Clear Input', icon: 'Eraser' },
      { type: 'browser_focus', label: 'Focus Element', icon: 'Target' },
      { type: 'browser_blur', label: 'Blur Element', icon: 'Circle' },
      { type: 'browser_select_all', label: 'Select All Text', icon: 'CheckSquare' },
      { type: 'browser_keyboard_combo', label: 'Keyboard Combo', icon: 'Keyboard' },
      { type: 'browser_upload_file', label: 'Upload File', icon: 'Upload' },
    ],
  },
  elementState: {
    label: 'Element State',
    steps: [
      { type: 'browser_is_visible', label: 'Is Visible', icon: 'Eye' },
      { type: 'browser_is_enabled', label: 'Is Enabled', icon: 'ToggleRight' },
      { type: 'browser_is_checked', label: 'Is Checked', icon: 'CheckSquare' },
      { type: 'browser_get_attribute', label: 'Get Attribute', icon: 'Tag' },
      { type: 'browser_get_bounding_box', label: 'Get Bounding Box', icon: 'Square' },
    ],
  },
  javascript: {
    label: 'JavaScript',
    steps: [
      { type: 'browser_evaluate', label: 'Evaluate JavaScript', icon: 'Code' },
    ],
  },
  frameHandling: {
    label: 'Frame Handling',
    steps: [
      { type: 'browser_list_frames', label: 'List Frames', icon: 'Layers' },
      { type: 'browser_switch_to_frame', label: 'Switch to Frame', icon: 'ArrowRightSquare' },
      { type: 'browser_switch_to_main_frame', label: 'Switch to Main Frame', icon: 'Home' },
    ],
  },
  scrolling: {
    label: 'Scrolling',
    steps: [
      { type: 'browser_scroll_to_top', label: 'Scroll to Top', icon: 'ChevronsUp' },
      { type: 'browser_scroll_to_bottom', label: 'Scroll to Bottom', icon: 'ChevronsDown' },
      { type: 'browser_scroll_by', label: 'Scroll By Pixels', icon: 'ChevronsUpDown' },
      { type: 'browser_scroll_to_element', label: 'Scroll to Element', icon: 'Target' },
    ],
  },
  waiting: {
    label: 'Waiting',
    steps: [
      { type: 'browser_wait', label: 'Wait (ms)', icon: 'Clock' },
      { type: 'browser_wait_for_selector', label: 'Wait for Selector', icon: 'Hourglass' },
      { type: 'browser_wait_for_network_idle', label: 'Wait for Network Idle', icon: 'Wifi' },
      { type: 'browser_wait_for_function', label: 'Wait for Function', icon: 'Code' },
      { type: 'browser_wait_for_url', label: 'Wait for URL', icon: 'Link' },
    ],
  },
  extraction: {
    label: 'Extraction',
    steps: [
      { type: 'browser_extract_text', label: 'Extract Text', icon: 'FileText' },
      { type: 'browser_get_html', label: 'Get HTML', icon: 'Code' },
      { type: 'browser_get_markdown', label: 'Get Markdown', icon: 'FileCode' },
      { type: 'browser_extract_json', label: 'Extract JSON', icon: 'Braces' },
      { type: 'browser_get_page_info', label: 'Get Page Info', icon: 'Info' },
    ],
  },
  ai: {
    label: 'AI Features',
    steps: [
      { type: 'browser_query_page', label: 'Query Page (LLM)', icon: 'MessageSquare' },
      { type: 'browser_summarize_page', label: 'Summarize Page (LLM)', icon: 'FileText' },
      { type: 'browser_nla', label: 'Natural Language Action', icon: 'Wand2' },
    ],
  },
  captcha: {
    label: 'CAPTCHA',
    steps: [
      { type: 'browser_detect_captcha', label: 'Detect CAPTCHA', icon: 'Shield' },
      { type: 'browser_classify_captcha', label: 'Classify CAPTCHA', icon: 'Shield' },
      { type: 'browser_solve_captcha', label: 'Solve CAPTCHA', icon: 'ShieldCheck' },
    ],
  },
  cookies: {
    label: 'Cookies',
    steps: [
      { type: 'browser_get_cookies', label: 'Get Cookies', icon: 'Cookie' },
      { type: 'browser_set_cookie', label: 'Set Cookie', icon: 'Cookie' },
      { type: 'browser_delete_cookies', label: 'Delete Cookies', icon: 'Trash2' },
    ],
  },
  visual: {
    label: 'Visual',
    steps: [
      { type: 'browser_screenshot', label: 'Screenshot', icon: 'Camera' },
      { type: 'browser_highlight', label: 'Highlight Element', icon: 'Highlighter' },
      { type: 'browser_set_viewport', label: 'Set Viewport Size', icon: 'Monitor' },
    ],
  },
  videoRecording: {
    label: 'Video Recording',
    steps: [
      { type: 'browser_start_video_recording', label: 'Start Video Recording', icon: 'Video' },
      { type: 'browser_stop_video_recording', label: 'Stop Video Recording', icon: 'VideoOff' },
    ],
  },
  networkInterception: {
    label: 'Network Interception',
    steps: [
      { type: 'browser_add_network_rule', label: 'Add Network Rule', icon: 'Filter' },
      { type: 'browser_remove_network_rule', label: 'Remove Network Rule', icon: 'FilterX' },
      { type: 'browser_enable_network_interception', label: 'Enable/Disable Interception', icon: 'ToggleRight' },
      { type: 'browser_get_network_log', label: 'Get Network Log', icon: 'List' },
      { type: 'browser_clear_network_log', label: 'Clear Network Log', icon: 'Trash2' },
    ],
  },
  fileDownloads: {
    label: 'File Downloads',
    steps: [
      { type: 'browser_set_download_path', label: 'Set Download Path', icon: 'FolderDown' },
      { type: 'browser_get_downloads', label: 'Get Downloads', icon: 'Download' },
      { type: 'browser_wait_for_download', label: 'Wait for Download', icon: 'Hourglass' },
      { type: 'browser_cancel_download', label: 'Cancel Download', icon: 'XCircle' },
    ],
  },
  dialogHandling: {
    label: 'Dialog Handling',
    steps: [
      { type: 'browser_set_dialog_action', label: 'Set Dialog Action', icon: 'MessageSquare' },
      { type: 'browser_get_pending_dialog', label: 'Get Pending Dialog', icon: 'MessageSquare' },
      { type: 'browser_handle_dialog', label: 'Handle Dialog', icon: 'Check' },
      { type: 'browser_wait_for_dialog', label: 'Wait for Dialog', icon: 'Hourglass' },
    ],
  },
  tabManagement: {
    label: 'Tab Management',
    steps: [
      { type: 'browser_new_tab', label: 'New Tab', icon: 'Plus' },
      { type: 'browser_get_tabs', label: 'Get Tabs', icon: 'Layers' },
      { type: 'browser_switch_tab', label: 'Switch Tab', icon: 'ArrowRightLeft' },
      { type: 'browser_get_active_tab', label: 'Get Active Tab', icon: 'CheckCircle' },
      { type: 'browser_close_tab', label: 'Close Tab', icon: 'X' },
      { type: 'browser_get_tab_count', label: 'Get Tab Count', icon: 'Hash' },
      { type: 'browser_set_popup_policy', label: 'Set Popup Policy', icon: 'Shield' },
      { type: 'browser_get_blocked_popups', label: 'Get Blocked Popups', icon: 'Ban' },
    ],
  },
} as const

// Helper to get step info
export function getStepInfo(type: string): { label: string; icon: string; category: string } | null {
  for (const [category, data] of Object.entries(STEP_CATEGORIES)) {
    const step = data.steps.find(s => s.type === type)
    if (step) {
      return { ...step, category }
    }
  }
  return null
}

// Helper to get all step types as flat array
export function getAllStepTypes(): Array<{ type: string; label: string; icon: string; category: string }> {
  const result: Array<{ type: string; label: string; icon: string; category: string }> = []
  for (const [category, data] of Object.entries(STEP_CATEGORIES)) {
    for (const step of data.steps) {
      result.push({ ...step, category })
    }
  }
  return result
}

// Generate unique ID
export function generateStepId(): string {
  return `step_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`
}

// Create a new step with defaults
export function createStep(type: string): FlowStep {
  const step: FlowStep = {
    id: generateStepId(),
    type,
    enabled: true,
    params: {},
  }
  // Initialize condition step with default branches
  if (type === 'condition') {
    step.condition = {
      source: 'previous',
      operator: 'is_truthy',
    }
    step.onTrue = []
    step.onFalse = []
  }
  return step
}

// Get value at a dot-notation path (e.g., 'data.items.0.name')
export function getValueAtPath(obj: unknown, path: string): unknown {
  if (!path) return obj
  const parts = path.split('.')
  let current: unknown = obj
  for (const part of parts) {
    if (current === null || current === undefined) return undefined
    if (typeof current === 'object') {
      current = (current as Record<string, unknown>)[part]
    } else {
      return undefined
    }
  }
  return current
}

// Evaluate a condition against a value
export function evaluateCondition(condition: FlowCondition, value: unknown): boolean {
  const checkValue = condition.field ? getValueAtPath(value, condition.field) : value

  switch (condition.operator) {
    case 'equals':
      return checkValue === condition.value
    case 'not_equals':
      return checkValue !== condition.value
    case 'contains':
      if (typeof checkValue === 'string' && typeof condition.value === 'string') {
        return checkValue.includes(condition.value)
      }
      if (Array.isArray(checkValue)) {
        return checkValue.includes(condition.value)
      }
      return false
    case 'not_contains':
      if (typeof checkValue === 'string' && typeof condition.value === 'string') {
        return !checkValue.includes(condition.value)
      }
      if (Array.isArray(checkValue)) {
        return !checkValue.includes(condition.value)
      }
      return true
    case 'starts_with':
      return typeof checkValue === 'string' && typeof condition.value === 'string'
        ? checkValue.startsWith(condition.value)
        : false
    case 'ends_with':
      return typeof checkValue === 'string' && typeof condition.value === 'string'
        ? checkValue.endsWith(condition.value)
        : false
    case 'greater_than':
      return typeof checkValue === 'number' && typeof condition.value === 'number'
        ? checkValue > condition.value
        : false
    case 'less_than':
      return typeof checkValue === 'number' && typeof condition.value === 'number'
        ? checkValue < condition.value
        : false
    case 'is_truthy':
      return !!checkValue
    case 'is_falsy':
      return !checkValue
    case 'is_empty':
      if (checkValue === null || checkValue === undefined) return true
      if (typeof checkValue === 'string') return checkValue.length === 0
      if (Array.isArray(checkValue)) return checkValue.length === 0
      if (typeof checkValue === 'object') return Object.keys(checkValue).length === 0
      return false
    case 'is_not_empty':
      if (checkValue === null || checkValue === undefined) return false
      if (typeof checkValue === 'string') return checkValue.length > 0
      if (Array.isArray(checkValue)) return checkValue.length > 0
      if (typeof checkValue === 'object') return Object.keys(checkValue).length > 0
      return true
    case 'regex_match':
      if (typeof checkValue === 'string' && typeof condition.value === 'string') {
        try {
          const regex = new RegExp(condition.value)
          return regex.test(checkValue)
        } catch {
          return false
        }
      }
      return false
    default:
      return false
  }
}

// Create a default condition
export function createDefaultCondition(): FlowCondition {
  return {
    source: 'previous',
    operator: 'is_truthy',
  }
}
