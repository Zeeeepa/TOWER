import { Plus, X, RefreshCw, LogOut, Globe, Layers } from 'lucide-react'
import { useAuth } from '../contexts/AuthContext'

interface HeaderProps {
  browserContext: {
    contextId: string | null
    contexts: string[]
    isLoading: boolean
    browserReady: boolean
    browserState: string
    pageInfo: {
      url: string
      title: string
    } | null
    create: () => Promise<string | null>
    close: () => Promise<void>
    switchContext: (contextId: string) => Promise<void>
    refreshHealth: () => Promise<void>
  }
}

export default function Header({ browserContext }: HeaderProps) {
  const { logout } = useAuth()
  const {
    contextId,
    contexts,
    isLoading,
    browserReady,
    browserState,
    pageInfo,
    create,
    close,
    switchContext,
    refreshHealth,
  } = browserContext

  const getStatusColor = () => {
    if (!browserReady) return 'status-dot-error'
    if (browserState === 'ready') return 'status-dot-success'
    if (browserState === 'starting') return 'status-dot-warning'
    return 'status-dot-neutral'
  }

  const getStatusText = () => {
    if (!browserReady) return 'Not Ready'
    return browserState.charAt(0).toUpperCase() + browserState.slice(1)
  }

  return (
    <header className="h-14 flex-shrink-0 bg-bg-secondary border-b border-white/10 flex items-center px-4 gap-4">
      {/* Logo */}
      <div className="flex items-center gap-2">
        <div className="w-8 h-8 rounded-lg bg-primary/20 flex items-center justify-center">
          <img src="/owl-logo.svg" alt="Owl" className="w-6 h-6" />
        </div>
        <span className="font-semibold text-text-primary">Owl Browser</span>
      </div>

      {/* Divider */}
      <div className="h-6 w-px bg-white/10" />

      {/* Browser Status */}
      <div className="flex items-center gap-2">
        <div className={getStatusColor()} />
        <span className="text-sm text-text-secondary">{getStatusText()}</span>
        <button
          onClick={refreshHealth}
          className="p-1 rounded hover:bg-bg-tertiary transition-colors"
          title="Refresh status"
        >
          <RefreshCw className="w-4 h-4 text-text-muted" />
        </button>
      </div>

      {/* Divider */}
      <div className="h-6 w-px bg-white/10" />

      {/* Context Controls */}
      <div className="flex items-center gap-2">
        <Layers className="w-4 h-4 text-text-muted" />
        {contextId ? (
          <div className="flex items-center gap-2">
            <select
              value={contextId}
              onChange={(e) => switchContext(e.target.value)}
              className="bg-bg-tertiary border border-white/10 rounded px-2 py-1 text-sm text-text-primary"
            >
              {contexts.map((ctx) => (
                <option key={ctx} value={ctx}>
                  {ctx}
                </option>
              ))}
            </select>
            <button
              onClick={close}
              disabled={isLoading}
              className="btn-icon p-1.5 text-red-400 hover:text-red-300"
              title="Close context"
            >
              <X className="w-4 h-4" />
            </button>
          </div>
        ) : (
          <button
            onClick={() => create()}
            disabled={isLoading || !browserReady}
            className="btn-primary py-1 px-3 text-sm flex items-center gap-1"
          >
            <Plus className="w-4 h-4" />
            <span>New Context</span>
          </button>
        )}
      </div>

      {/* Page Info */}
      {pageInfo && (
        <>
          <div className="h-6 w-px bg-white/10" />
          <div className="flex-1 flex items-center gap-2 min-w-0">
            <Globe className="w-4 h-4 text-text-muted flex-shrink-0" />
            <span className="text-sm text-text-secondary truncate" title={pageInfo.url}>
              {pageInfo.title || pageInfo.url}
            </span>
          </div>
        </>
      )}

      {/* Spacer */}
      <div className="flex-1" />

      {/* Logout */}
      <button
        onClick={logout}
        className="btn-icon p-2 text-text-muted hover:text-text-primary"
        title="Logout"
      >
        <LogOut className="w-4 h-4" />
      </button>
    </header>
  )
}
