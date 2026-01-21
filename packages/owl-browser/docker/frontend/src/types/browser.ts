export interface ToolParam {
  name: string
  type: 'string' | 'integer' | 'number' | 'boolean' | 'enum'
  required: boolean
  description: string
  enum_values?: string[]
}

export interface Tool {
  name: string
  description: string
  param_count: number
  parameters?: ToolParam[]
}

export interface ToolCategory {
  name: string
  icon: string
  tools: string[]
}

export interface ApiResponse<T = unknown> {
  success: boolean
  result?: T
  error?: string
}

// IPC response from browser has format: {id: number, result: T}
export interface IpcResponse<T = unknown> {
  id: number
  result: T
}

export interface BrowserContext {
  context_id: string
}

export interface PageInfo {
  url: string
  title: string
  can_go_back: boolean
  can_go_forward: boolean
}

export interface ElementInfo {
  tagName: string
  id: string
  className: string
  textContent: string
  selector: string
  x: number
  y: number
  width: number
  height: number
}

export interface StreamStats {
  target_fps: number
  actual_fps: number
  width: number
  height: number
  frames_received: number
  frames_encoded: number
  frames_sent: number
  frames_dropped: number
  subscriber_count: number
  is_active: boolean
}

export interface ToolExecution {
  tool: string
  params: Record<string, unknown>
  timestamp: number
  result?: unknown
  error?: string
  loading?: boolean
}

// Tool categories for organization
export const TOOL_CATEGORIES: ToolCategory[] = [
  {
    name: 'Context',
    icon: 'Layers',
    tools: ['browser_create_context', 'browser_close_context', 'browser_list_contexts', 'browser_get_context_info'],
  },
  {
    name: 'Navigation',
    icon: 'Navigation',
    tools: ['browser_navigate', 'browser_reload', 'browser_go_back', 'browser_go_forward'],
  },
  {
    name: 'Interaction',
    icon: 'MousePointer2',
    tools: [
      'browser_click',
      'browser_type',
      'browser_pick',
      'browser_press_key',
      'browser_submit_form',
      'browser_hover',
      'browser_double_click',
      'browser_right_click',
      'browser_clear_input',
      'browser_focus',
      'browser_blur',
      'browser_select_all',
      'browser_keyboard_combo',
    ],
  },
  {
    name: 'Mouse & Drag',
    icon: 'Move',
    tools: ['browser_drag_drop', 'browser_html5_drag_drop', 'browser_mouse_move'],
  },
  {
    name: 'Scrolling',
    icon: 'ArrowDownUp',
    tools: ['browser_scroll_by', 'browser_scroll_to_element', 'browser_scroll_to_top', 'browser_scroll_to_bottom'],
  },
  {
    name: 'Extraction',
    icon: 'FileText',
    tools: [
      'browser_extract_text',
      'browser_screenshot',
      'browser_get_html',
      'browser_get_markdown',
      'browser_extract_json',
      'browser_detect_site',
      'browser_list_templates',
    ],
  },
  {
    name: 'AI / LLM',
    icon: 'Sparkles',
    tools: ['browser_summarize_page', 'browser_query_page', 'browser_nla', 'browser_llm_status'],
  },
  {
    name: 'Waiting',
    icon: 'Clock',
    tools: [
      'browser_wait',
      'browser_wait_for_selector',
      'browser_wait_for_network_idle',
      'browser_wait_for_function',
      'browser_wait_for_url',
    ],
  },
  {
    name: 'Page Info',
    icon: 'Info',
    tools: [
      'browser_get_page_info',
      'browser_set_viewport',
      'browser_highlight',
      'browser_show_grid_overlay',
      'browser_get_element_at_position',
    ],
  },
  {
    name: 'DOM Zoom',
    icon: 'ZoomIn',
    tools: ['browser_zoom_in', 'browser_zoom_out', 'browser_zoom_reset'],
  },
  {
    name: 'Console Logs',
    icon: 'Terminal',
    tools: ['browser_get_console_log', 'browser_clear_console_log'],
  },
  {
    name: 'Element State',
    icon: 'ToggleRight',
    tools: ['browser_is_visible', 'browser_is_enabled', 'browser_is_checked', 'browser_get_attribute', 'browser_get_bounding_box'],
  },
  {
    name: 'Video Recording',
    icon: 'Video',
    tools: [
      'browser_start_video_recording',
      'browser_pause_video_recording',
      'browser_resume_video_recording',
      'browser_stop_video_recording',
      'browser_get_video_recording_stats',
    ],
  },
  {
    name: 'Live Streaming',
    icon: 'Radio',
    tools: ['browser_start_live_stream', 'browser_stop_live_stream', 'browser_get_live_stream_stats', 'browser_list_live_streams'],
  },
  {
    name: 'CAPTCHA',
    icon: 'ShieldCheck',
    tools: [
      'browser_detect_captcha',
      'browser_classify_captcha',
      'browser_solve_text_captcha',
      'browser_solve_image_captcha',
      'browser_solve_captcha',
    ],
  },
  {
    name: 'Cookies',
    icon: 'Cookie',
    tools: ['browser_get_cookies', 'browser_set_cookie', 'browser_delete_cookies'],
  },
  {
    name: 'Profiles',
    icon: 'UserCircle',
    tools: ['browser_create_profile', 'browser_load_profile', 'browser_save_profile', 'browser_get_profile', 'browser_update_profile_cookies'],
  },
  {
    name: 'Proxy',
    icon: 'Shield',
    tools: ['browser_set_proxy', 'browser_get_proxy_status', 'browser_connect_proxy', 'browser_disconnect_proxy'],
  },
  {
    name: 'Network',
    icon: 'Network',
    tools: [
      'browser_add_network_rule',
      'browser_remove_network_rule',
      'browser_enable_network_interception',
      'browser_get_network_log',
      'browser_clear_network_log',
    ],
  },
  {
    name: 'Downloads',
    icon: 'Download',
    tools: ['browser_set_download_path', 'browser_get_downloads', 'browser_wait_for_download', 'browser_cancel_download'],
  },
  {
    name: 'Dialogs',
    icon: 'MessageSquare',
    tools: ['browser_set_dialog_action', 'browser_get_pending_dialog', 'browser_handle_dialog', 'browser_wait_for_dialog'],
  },
  {
    name: 'Tabs',
    icon: 'PanelTop',
    tools: [
      'browser_new_tab',
      'browser_get_tabs',
      'browser_switch_tab',
      'browser_get_active_tab',
      'browser_close_tab',
      'browser_get_tab_count',
      'browser_set_popup_policy',
      'browser_get_blocked_popups',
    ],
  },
  {
    name: 'Frames',
    icon: 'Frame',
    tools: ['browser_list_frames', 'browser_switch_to_frame', 'browser_switch_to_main_frame'],
  },
  {
    name: 'Files',
    icon: 'Upload',
    tools: ['browser_upload_file'],
  },
  {
    name: 'JavaScript',
    icon: 'Code',
    tools: ['browser_evaluate'],
  },
  {
    name: 'Clipboard',
    icon: 'Clipboard',
    tools: ['browser_clipboard_read', 'browser_clipboard_write', 'browser_clipboard_clear'],
  },
  {
    name: 'Demographics',
    icon: 'Globe',
    tools: ['browser_get_demographics', 'browser_get_location', 'browser_get_datetime', 'browser_get_weather'],
  },
  {
    name: 'License',
    icon: 'Key',
    tools: [
      'browser_get_license_status',
      'browser_get_license_info',
      'browser_get_hardware_fingerprint',
      'browser_add_license',
      'browser_remove_license',
    ],
  },
]

// Helper to get category for a tool
export function getToolCategory(toolName: string): ToolCategory | undefined {
  return TOOL_CATEGORIES.find((cat) => cat.tools.includes(toolName))
}
