import { useState } from 'react'
import {
  Layers,
  Navigation,
  MousePointer2,
  Move,
  ArrowDownUp,
  FileText,
  Sparkles,
  Clock,
  Info,
  ToggleRight,
  Video,
  Radio,
  ShieldCheck,
  Cookie,
  UserCircle,
  Shield,
  Network,
  Download,
  MessageSquare,
  PanelTop,
  Frame,
  Upload,
  Code,
  Clipboard,
  Globe,
  Key,
  ChevronDown,
  ChevronRight,
  Search,
} from 'lucide-react'
import { TOOL_CATEGORIES } from '../types/browser'

const ICON_MAP: Record<string, React.ComponentType<{ className?: string }>> = {
  Layers,
  Navigation,
  MousePointer2,
  Move,
  ArrowDownUp,
  FileText,
  Sparkles,
  Clock,
  Info,
  ToggleRight,
  Video,
  Radio,
  ShieldCheck,
  Cookie,
  UserCircle,
  Shield,
  Network,
  Download,
  MessageSquare,
  PanelTop,
  Frame,
  Upload,
  Code,
  Clipboard,
  Globe,
  Key,
}

interface ToolsSidebarProps {
  selectedTool: string | null
  onSelectTool: (toolName: string) => void
}

export default function ToolsSidebar({ selectedTool, onSelectTool }: ToolsSidebarProps) {
  const [expandedCategories, setExpandedCategories] = useState<Set<string>>(new Set(['Context', 'Navigation', 'Interaction']))
  const [searchQuery, setSearchQuery] = useState('')

  const toggleCategory = (categoryName: string) => {
    setExpandedCategories((prev) => {
      const next = new Set(prev)
      if (next.has(categoryName)) {
        next.delete(categoryName)
      } else {
        next.add(categoryName)
      }
      return next
    })
  }

  const formatToolName = (name: string): string => {
    return name
      .replace('browser_', '')
      .split('_')
      .map((word) => word.charAt(0).toUpperCase() + word.slice(1))
      .join(' ')
  }

  const filteredCategories = searchQuery
    ? TOOL_CATEGORIES.map((cat) => ({
        ...cat,
        tools: cat.tools.filter(
          (tool) =>
            tool.toLowerCase().includes(searchQuery.toLowerCase()) ||
            formatToolName(tool).toLowerCase().includes(searchQuery.toLowerCase())
        ),
      })).filter((cat) => cat.tools.length > 0)
    : TOOL_CATEGORIES

  return (
    <div className="flex flex-col h-full">
      {/* Search */}
      <div className="p-3 border-b border-white/10">
        <div className="relative">
          <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-text-muted" />
          <input
            type="text"
            placeholder="Search tools..."
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
            className="input-field-sm pl-9"
          />
        </div>
      </div>

      {/* Categories */}
      <div className="flex-1 overflow-y-auto">
        {filteredCategories.map((category) => {
          const Icon = ICON_MAP[category.icon] || Layers
          const isExpanded = expandedCategories.has(category.name) || searchQuery.length > 0

          return (
            <div key={category.name} className="border-b border-white/5">
              {/* Category Header */}
              <button
                onClick={() => toggleCategory(category.name)}
                className="w-full category-header hover:bg-bg-tertiary transition-colors"
              >
                <Icon className="w-4 h-4" />
                <span className="flex-1 text-left">{category.name}</span>
                <span className="text-xs text-text-muted">{category.tools.length}</span>
                {isExpanded ? (
                  <ChevronDown className="w-4 h-4" />
                ) : (
                  <ChevronRight className="w-4 h-4" />
                )}
              </button>

              {/* Tools List */}
              {isExpanded && (
                <div className="py-1">
                  {category.tools.map((tool) => (
                    <button
                      key={tool}
                      onClick={() => onSelectTool(tool)}
                      className={
                        selectedTool === tool
                          ? 'tool-item-active w-full text-left'
                          : 'tool-item w-full text-left'
                      }
                    >
                      <span className="text-sm truncate">{formatToolName(tool)}</span>
                    </button>
                  ))}
                </div>
              )}
            </div>
          )
        })}
      </div>

      {/* Footer */}
      <div className="p-3 border-t border-white/10 text-xs text-text-muted text-center">
        {TOOL_CATEGORIES.reduce((sum, cat) => sum + cat.tools.length, 0)} tools available
      </div>
    </div>
  )
}
