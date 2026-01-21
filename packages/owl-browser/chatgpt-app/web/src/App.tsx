/**
 * Owl Browser ChatGPT App - Main UI Component
 *
 * A beautiful, full-featured interface for browser automation in ChatGPT.
 * Brand color: #6BA894 (Owl Teal)
 *
 * @author Olib AI
 * @license MIT
 */

import React, { useState, useEffect, useCallback, useMemo, createContext, useContext } from 'react';
import { createRoot } from 'react-dom/client';

// =============================================================================
// TYPE DEFINITIONS
// =============================================================================

interface OpenAI {
  toolInput: any;
  toolOutput: any;
  widgetState: any;
  theme: 'light' | 'dark';
  locale: string;
  displayMode: 'inline' | 'fullscreen' | 'pip';
  callTool: (name: string, params: any) => Promise<any>;
  sendFollowUpMessage: (message: string) => void;
  requestDisplayMode: (mode: 'inline' | 'fullscreen' | 'pip') => void;
  requestModal: (options: any) => void;
  setWidgetState: (state: any) => void;
  uploadFile: () => Promise<any>;
  getFileDownloadUrl: (fileId: string) => Promise<string>;
  openExternal: (url: string) => void;
}

declare global {
  interface Window {
    openai: OpenAI;
  }
}

interface BrowserContext {
  id: string;
  url: string | null;
  title: string | null;
  created: string;
  status: 'active' | 'loading' | 'error';
  screenshot?: string;
}

interface ToolResult {
  success: boolean;
  message?: string;
  error?: string;
  data?: any;
  screenshot?: string;
  _ui?: {
    displayMode?: string;
    component?: string;
    props?: any;
  };
}

interface AppState {
  connected: boolean;
  contexts: BrowserContext[];
  activeContextId: string | null;
  loading: boolean;
  error: string | null;
  displayMode: 'inline' | 'fullscreen' | 'pip';
  view: 'dashboard' | 'context' | 'screenshot' | 'recording' | 'settings';
}

// =============================================================================
// THEME & COLORS
// =============================================================================

const COLORS = {
  primary: '#6BA894',
  primaryDark: '#5A9683',
  primaryLight: '#7CBAA5',
  primaryLighter: '#E8F4F0',
  secondary: '#4A5568',
  success: '#48BB78',
  warning: '#ECC94B',
  error: '#F56565',
  info: '#4299E1',

  // Light theme
  light: {
    bg: '#FFFFFF',
    bgSecondary: '#F7FAFC',
    bgTertiary: '#EDF2F7',
    text: '#1A202C',
    textSecondary: '#4A5568',
    textMuted: '#A0AEC0',
    border: '#E2E8F0',
    shadow: 'rgba(0, 0, 0, 0.1)'
  },

  // Dark theme
  dark: {
    bg: '#1A202C',
    bgSecondary: '#2D3748',
    bgTertiary: '#4A5568',
    text: '#F7FAFC',
    textSecondary: '#CBD5E0',
    textMuted: '#718096',
    border: '#4A5568',
    shadow: 'rgba(0, 0, 0, 0.3)'
  }
};

// =============================================================================
// CONTEXT
// =============================================================================

interface ThemeContextValue {
  isDark: boolean;
  colors: typeof COLORS.light;
}

const ThemeContext = createContext<ThemeContextValue>({
  isDark: false,
  colors: COLORS.light
});

const useTheme = () => useContext(ThemeContext);

// =============================================================================
// HOOKS
// =============================================================================

function useOpenAI() {
  const [openai, setOpenai] = useState<OpenAI | null>(null);
  const [theme, setTheme] = useState<'light' | 'dark'>('light');
  const [displayMode, setDisplayMode] = useState<'inline' | 'fullscreen' | 'pip'>('inline');

  useEffect(() => {
    if (typeof window !== 'undefined' && window.openai) {
      setOpenai(window.openai);
      setTheme(window.openai.theme || 'light');
      setDisplayMode(window.openai.displayMode || 'inline');

      // Listen for theme changes
      const observer = new MutationObserver(() => {
        if (window.openai.theme !== theme) {
          setTheme(window.openai.theme);
        }
      });

      observer.observe(document.body, { attributes: true });

      return () => observer.disconnect();
    }
  }, []);

  return { openai, theme, displayMode };
}

function useWidgetState<T>(initialState: T): [T, (state: T) => void] {
  const [state, setState] = useState<T>(() => {
    if (typeof window !== 'undefined' && window.openai?.widgetState) {
      return { ...initialState, ...window.openai.widgetState };
    }
    return initialState;
  });

  const updateState = useCallback((newState: T) => {
    setState(newState);
    if (typeof window !== 'undefined' && window.openai?.setWidgetState) {
      window.openai.setWidgetState(newState);
    }
  }, []);

  return [state, updateState];
}

// =============================================================================
// ICONS (SVG Components)
// =============================================================================

const Icons = {
  Owl: () => (
    <svg viewBox="0 0 24 24" fill="currentColor" width="24" height="24">
      <circle cx="8" cy="10" r="3" fill="none" stroke="currentColor" strokeWidth="2"/>
      <circle cx="16" cy="10" r="3" fill="none" stroke="currentColor" strokeWidth="2"/>
      <circle cx="8" cy="10" r="1"/>
      <circle cx="16" cy="10" r="1"/>
      <path d="M12 14 L10 18 L12 17 L14 18 Z" fill="currentColor"/>
      <path d="M5 8 Q2 4 6 3" stroke="currentColor" strokeWidth="2" fill="none"/>
      <path d="M19 8 Q22 4 18 3" stroke="currentColor" strokeWidth="2" fill="none"/>
    </svg>
  ),

  Browser: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" width="20" height="20">
      <rect x="3" y="4" width="18" height="16" rx="2"/>
      <line x1="3" y1="9" x2="21" y2="9"/>
      <circle cx="6" cy="6.5" r="1" fill="currentColor"/>
      <circle cx="9" cy="6.5" r="1" fill="currentColor"/>
    </svg>
  ),

  Navigate: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" width="20" height="20">
      <circle cx="12" cy="12" r="10"/>
      <path d="M12 2 L12 6"/>
      <path d="M12 18 L12 22"/>
      <path d="M2 12 L6 12"/>
      <path d="M18 12 L22 12"/>
      <polygon points="12,8 15,14 12,13 9,14" fill="currentColor"/>
    </svg>
  ),

  Click: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" width="20" height="20">
      <path d="M6 3 L6 14 L10 10 L14 18 L16 17 L12 9 L17 9 Z"/>
    </svg>
  ),

  Camera: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" width="20" height="20">
      <rect x="2" y="6" width="20" height="14" rx="2"/>
      <circle cx="12" cy="13" r="4"/>
      <path d="M7 6 L9 3 L15 3 L17 6"/>
    </svg>
  ),

  Magic: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" width="20" height="20">
      <path d="M12 3 L13.5 8 L19 8 L14.5 11.5 L16 17 L12 14 L8 17 L9.5 11.5 L5 8 L10.5 8 Z"/>
    </svg>
  ),

  Record: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" width="20" height="20">
      <circle cx="12" cy="12" r="10"/>
      <circle cx="12" cy="12" r="4" fill="currentColor"/>
    </svg>
  ),

  Play: () => (
    <svg viewBox="0 0 24 24" fill="currentColor" width="20" height="20">
      <polygon points="5,3 19,12 5,21"/>
    </svg>
  ),

  Stop: () => (
    <svg viewBox="0 0 24 24" fill="currentColor" width="20" height="20">
      <rect x="5" y="5" width="14" height="14" rx="2"/>
    </svg>
  ),

  Settings: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" width="20" height="20">
      <circle cx="12" cy="12" r="3"/>
      <path d="M12 1 L12 4 M12 20 L12 23 M4.22 4.22 L6.34 6.34 M17.66 17.66 L19.78 19.78 M1 12 L4 12 M20 12 L23 12 M4.22 19.78 L6.34 17.66 M17.66 6.34 L19.78 4.22"/>
    </svg>
  ),

  Plus: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" width="20" height="20">
      <line x1="12" y1="5" x2="12" y2="19"/>
      <line x1="5" y1="12" x2="19" y2="12"/>
    </svg>
  ),

  Close: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" width="20" height="20">
      <line x1="18" y1="6" x2="6" y2="18"/>
      <line x1="6" y1="6" x2="18" y2="18"/>
    </svg>
  ),

  Expand: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" width="20" height="20">
      <polyline points="15 3 21 3 21 9"/>
      <polyline points="9 21 3 21 3 15"/>
      <line x1="21" y1="3" x2="14" y2="10"/>
      <line x1="3" y1="21" x2="10" y2="14"/>
    </svg>
  ),

  Minimize: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" width="20" height="20">
      <polyline points="4 14 10 14 10 20"/>
      <polyline points="20 10 14 10 14 4"/>
      <line x1="14" y1="10" x2="21" y2="3"/>
      <line x1="3" y1="21" x2="10" y2="14"/>
    </svg>
  ),

  Refresh: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" width="20" height="20">
      <polyline points="23 4 23 10 17 10"/>
      <path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10"/>
    </svg>
  ),

  Link: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" width="20" height="20">
      <path d="M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71"/>
      <path d="M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71"/>
    </svg>
  ),

  Check: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" width="20" height="20">
      <polyline points="20 6 9 17 4 12"/>
    </svg>
  ),

  AlertCircle: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" width="20" height="20">
      <circle cx="12" cy="12" r="10"/>
      <line x1="12" y1="8" x2="12" y2="12"/>
      <line x1="12" y1="16" x2="12.01" y2="16"/>
    </svg>
  ),

  Loader: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" width="20" height="20" style={{ animation: 'spin 1s linear infinite' }}>
      <circle cx="12" cy="12" r="10" strokeOpacity="0.25"/>
      <path d="M12 2 A10 10 0 0 1 22 12" strokeLinecap="round"/>
    </svg>
  )
};

// =============================================================================
// STYLES
// =============================================================================

const createStyles = (isDark: boolean, colors: typeof COLORS.light) => ({
  container: {
    fontFamily: '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif',
    backgroundColor: colors.bg,
    color: colors.text,
    borderRadius: '12px',
    overflow: 'hidden',
    border: `1px solid ${colors.border}`,
    boxShadow: `0 4px 12px ${colors.shadow}`
  },

  header: {
    background: `linear-gradient(135deg, ${COLORS.primary} 0%, ${COLORS.primaryDark} 100%)`,
    color: 'white',
    padding: '16px 20px',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between'
  },

  headerTitle: {
    display: 'flex',
    alignItems: 'center',
    gap: '12px',
    fontSize: '18px',
    fontWeight: 600
  },

  headerActions: {
    display: 'flex',
    alignItems: 'center',
    gap: '8px'
  },

  content: {
    padding: '20px'
  },

  card: {
    backgroundColor: colors.bgSecondary,
    borderRadius: '10px',
    padding: '16px',
    marginBottom: '16px',
    border: `1px solid ${colors.border}`
  },

  button: {
    display: 'inline-flex',
    alignItems: 'center',
    justifyContent: 'center',
    gap: '8px',
    padding: '10px 20px',
    borderRadius: '8px',
    border: 'none',
    cursor: 'pointer',
    fontWeight: 500,
    fontSize: '14px',
    transition: 'all 0.2s ease'
  },

  buttonPrimary: {
    backgroundColor: COLORS.primary,
    color: 'white'
  },

  buttonSecondary: {
    backgroundColor: colors.bgTertiary,
    color: colors.text
  },

  buttonIcon: {
    padding: '8px',
    borderRadius: '8px',
    backgroundColor: 'rgba(255, 255, 255, 0.15)',
    border: 'none',
    cursor: 'pointer',
    color: 'white',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center'
  },

  input: {
    width: '100%',
    padding: '12px 16px',
    borderRadius: '8px',
    border: `1px solid ${colors.border}`,
    backgroundColor: colors.bg,
    color: colors.text,
    fontSize: '14px',
    outline: 'none'
  },

  contextCard: {
    backgroundColor: colors.bg,
    borderRadius: '10px',
    padding: '16px',
    marginBottom: '12px',
    border: `1px solid ${colors.border}`,
    cursor: 'pointer',
    transition: 'all 0.2s ease'
  },

  contextCardActive: {
    borderColor: COLORS.primary,
    boxShadow: `0 0 0 2px ${COLORS.primaryLighter}`
  },

  badge: {
    display: 'inline-flex',
    alignItems: 'center',
    padding: '4px 10px',
    borderRadius: '12px',
    fontSize: '12px',
    fontWeight: 500
  },

  badgeSuccess: {
    backgroundColor: `${COLORS.success}20`,
    color: COLORS.success
  },

  badgeWarning: {
    backgroundColor: `${COLORS.warning}20`,
    color: COLORS.warning
  },

  badgeError: {
    backgroundColor: `${COLORS.error}20`,
    color: COLORS.error
  },

  grid: {
    display: 'grid',
    gridTemplateColumns: 'repeat(auto-fill, minmax(280px, 1fr))',
    gap: '16px'
  },

  toolButton: {
    display: 'flex',
    flexDirection: 'column' as const,
    alignItems: 'center',
    gap: '8px',
    padding: '20px',
    borderRadius: '12px',
    backgroundColor: colors.bgSecondary,
    border: `1px solid ${colors.border}`,
    cursor: 'pointer',
    transition: 'all 0.2s ease',
    textAlign: 'center' as const
  },

  toolIcon: {
    width: '48px',
    height: '48px',
    borderRadius: '12px',
    backgroundColor: `${COLORS.primary}15`,
    color: COLORS.primary,
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center'
  },

  screenshot: {
    width: '100%',
    borderRadius: '8px',
    border: `1px solid ${colors.border}`,
    boxShadow: `0 4px 12px ${colors.shadow}`
  },

  urlBar: {
    display: 'flex',
    alignItems: 'center',
    gap: '8px',
    padding: '8px 12px',
    backgroundColor: colors.bgSecondary,
    borderRadius: '8px',
    marginBottom: '16px'
  },

  statusDot: {
    width: '8px',
    height: '8px',
    borderRadius: '50%'
  },

  emptyState: {
    textAlign: 'center' as const,
    padding: '48px 24px',
    color: colors.textMuted
  }
});

// =============================================================================
// COMPONENTS
// =============================================================================

// Button Component
interface ButtonProps {
  children: React.ReactNode;
  onClick?: () => void;
  variant?: 'primary' | 'secondary' | 'icon';
  disabled?: boolean;
  loading?: boolean;
  style?: React.CSSProperties;
}

const Button: React.FC<ButtonProps> = ({
  children,
  onClick,
  variant = 'primary',
  disabled,
  loading,
  style
}) => {
  const { colors, isDark } = useTheme();
  const styles = createStyles(isDark, colors);

  const buttonStyle = {
    ...styles.button,
    ...(variant === 'primary' ? styles.buttonPrimary : {}),
    ...(variant === 'secondary' ? styles.buttonSecondary : {}),
    ...(variant === 'icon' ? styles.buttonIcon : {}),
    opacity: disabled || loading ? 0.6 : 1,
    cursor: disabled || loading ? 'not-allowed' : 'pointer',
    ...style
  };

  return (
    <button style={buttonStyle} onClick={onClick} disabled={disabled || loading}>
      {loading ? <Icons.Loader /> : children}
    </button>
  );
};

// Card Component
interface CardProps {
  children: React.ReactNode;
  title?: string;
  style?: React.CSSProperties;
}

const Card: React.FC<CardProps> = ({ children, title, style }) => {
  const { colors, isDark } = useTheme();
  const styles = createStyles(isDark, colors);

  return (
    <div style={{ ...styles.card, ...style }}>
      {title && (
        <h3 style={{ margin: '0 0 12px 0', fontSize: '16px', fontWeight: 600 }}>
          {title}
        </h3>
      )}
      {children}
    </div>
  );
};

// Context Card Component
interface ContextCardProps {
  context: BrowserContext;
  isActive: boolean;
  onClick: () => void;
  onClose: () => void;
}

const ContextCard: React.FC<ContextCardProps> = ({ context, isActive, onClick, onClose }) => {
  const { colors, isDark } = useTheme();
  const styles = createStyles(isDark, colors);

  return (
    <div
      style={{
        ...styles.contextCard,
        ...(isActive ? styles.contextCardActive : {})
      }}
      onClick={onClick}
    >
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
        <div style={{ flex: 1 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '8px' }}>
            <span style={{
              ...styles.statusDot,
              backgroundColor: context.status === 'active' ? COLORS.success :
                             context.status === 'loading' ? COLORS.warning : COLORS.error
            }} />
            <span style={{ fontWeight: 600 }}>{context.id}</span>
          </div>
          {context.url && (
            <div style={{
              fontSize: '13px',
              color: colors.textSecondary,
              overflow: 'hidden',
              textOverflow: 'ellipsis',
              whiteSpace: 'nowrap'
            }}>
              {context.url}
            </div>
          )}
          {context.title && (
            <div style={{
              fontSize: '12px',
              color: colors.textMuted,
              marginTop: '4px'
            }}>
              {context.title}
            </div>
          )}
        </div>
        <button
          style={{
            ...styles.buttonIcon,
            backgroundColor: 'transparent',
            color: colors.textMuted
          }}
          onClick={(e) => { e.stopPropagation(); onClose(); }}
        >
          <Icons.Close />
        </button>
      </div>
    </div>
  );
};

// Quick Action Button Component
interface QuickActionProps {
  icon: React.ReactNode;
  label: string;
  description: string;
  onClick: () => void;
  color?: string;
}

const QuickAction: React.FC<QuickActionProps> = ({ icon, label, description, onClick, color = COLORS.primary }) => {
  const { colors, isDark } = useTheme();
  const styles = createStyles(isDark, colors);

  return (
    <div
      style={styles.toolButton}
      onClick={onClick}
      onMouseOver={(e) => {
        (e.currentTarget as HTMLDivElement).style.borderColor = color;
        (e.currentTarget as HTMLDivElement).style.transform = 'translateY(-2px)';
      }}
      onMouseOut={(e) => {
        (e.currentTarget as HTMLDivElement).style.borderColor = colors.border;
        (e.currentTarget as HTMLDivElement).style.transform = 'translateY(0)';
      }}
    >
      <div style={{ ...styles.toolIcon, backgroundColor: `${color}15`, color }}>
        {icon}
      </div>
      <div style={{ fontWeight: 600, fontSize: '14px' }}>{label}</div>
      <div style={{ fontSize: '12px', color: colors.textMuted }}>{description}</div>
    </div>
  );
};

// URL Input Component
interface URLInputProps {
  value: string;
  onChange: (value: string) => void;
  onNavigate: () => void;
  loading?: boolean;
}

const URLInput: React.FC<URLInputProps> = ({ value, onChange, onNavigate, loading }) => {
  const { colors, isDark } = useTheme();
  const styles = createStyles(isDark, colors);

  return (
    <div style={styles.urlBar}>
      <Icons.Link />
      <input
        style={{
          ...styles.input,
          border: 'none',
          backgroundColor: 'transparent',
          flex: 1,
          padding: '8px 0'
        }}
        placeholder="Enter URL or command..."
        value={value}
        onChange={(e) => onChange(e.target.value)}
        onKeyDown={(e) => e.key === 'Enter' && onNavigate()}
      />
      <Button onClick={onNavigate} loading={loading}>
        <Icons.Navigate />
        Go
      </Button>
    </div>
  );
};

// Dashboard View
interface DashboardProps {
  state: AppState;
  onAction: (action: string, params?: any) => void;
}

const Dashboard: React.FC<DashboardProps> = ({ state, onAction }) => {
  const { colors, isDark } = useTheme();
  const styles = createStyles(isDark, colors);

  const quickActions = [
    { icon: <Icons.Plus />, label: 'New Session', description: 'Create browser context', action: 'create_context', color: COLORS.primary },
    { icon: <Icons.Navigate />, label: 'Navigate', description: 'Go to a URL', action: 'navigate', color: COLORS.info },
    { icon: <Icons.Camera />, label: 'Screenshot', description: 'Capture the page', action: 'screenshot', color: COLORS.warning },
    { icon: <Icons.Magic />, label: 'AI Action', description: 'Natural language task', action: 'nla', color: '#E74C3C' },
    { icon: <Icons.Record />, label: 'Record', description: 'Record session video', action: 'record', color: COLORS.error },
    { icon: <Icons.Click />, label: 'Interact', description: 'Click, type, scroll', action: 'interact', color: '#9B59B6' }
  ];

  return (
    <div>
      {/* Connection Status */}
      {!state.connected && (
        <Card style={{ backgroundColor: `${COLORS.warning}10`, borderColor: COLORS.warning }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
            <Icons.AlertCircle />
            <div>
              <div style={{ fontWeight: 600 }}>Browser Not Connected</div>
              <div style={{ fontSize: '13px', color: colors.textSecondary }}>
                Start Owl Browser locally to enable automation
              </div>
            </div>
          </div>
        </Card>
      )}

      {/* Active Contexts */}
      {state.contexts.length > 0 && (
        <div style={{ marginBottom: '24px' }}>
          <h3 style={{ fontSize: '14px', fontWeight: 600, marginBottom: '12px', textTransform: 'uppercase', letterSpacing: '0.5px', color: colors.textMuted }}>
            Active Sessions
          </h3>
          {state.contexts.map((ctx) => (
            <ContextCard
              key={ctx.id}
              context={ctx}
              isActive={ctx.id === state.activeContextId}
              onClick={() => onAction('select_context', { contextId: ctx.id })}
              onClose={() => onAction('close_context', { contextId: ctx.id })}
            />
          ))}
        </div>
      )}

      {/* Quick Actions */}
      <div style={{ marginBottom: '24px' }}>
        <h3 style={{ fontSize: '14px', fontWeight: 600, marginBottom: '12px', textTransform: 'uppercase', letterSpacing: '0.5px', color: colors.textMuted }}>
          Quick Actions
        </h3>
        <div style={styles.grid}>
          {quickActions.map((action) => (
            <QuickAction
              key={action.action}
              icon={action.icon}
              label={action.label}
              description={action.description}
              color={action.color}
              onClick={() => onAction(action.action)}
            />
          ))}
        </div>
      </div>

      {/* Empty State */}
      {state.contexts.length === 0 && (
        <div style={styles.emptyState}>
          <div style={{ marginBottom: '16px' }}>
            <Icons.Browser />
          </div>
          <h3 style={{ margin: '0 0 8px 0', color: colors.text }}>No Active Sessions</h3>
          <p style={{ margin: 0 }}>Create a new browser session to start automating</p>
        </div>
      )}
    </div>
  );
};

// Context View (Active Browser Session)
interface ContextViewProps {
  context: BrowserContext;
  onAction: (action: string, params?: any) => void;
  loading: boolean;
}

const ContextView: React.FC<ContextViewProps> = ({ context, onAction, loading }) => {
  const { colors, isDark } = useTheme();
  const styles = createStyles(isDark, colors);
  const [url, setUrl] = useState(context.url || '');
  const [command, setCommand] = useState('');

  return (
    <div>
      {/* URL Bar */}
      <URLInput
        value={url}
        onChange={setUrl}
        onNavigate={() => onAction('navigate', { url })}
        loading={loading}
      />

      {/* Screenshot Preview */}
      {context.screenshot && (
        <Card title="Current View">
          <img
            src={`data:image/png;base64,${context.screenshot}`}
            alt="Browser screenshot"
            style={styles.screenshot}
          />
        </Card>
      )}

      {/* NLA Command */}
      <Card title="AI Command">
        <div style={{ display: 'flex', gap: '8px' }}>
          <input
            style={{ ...styles.input, flex: 1 }}
            placeholder="e.g., 'click the sign in button' or 'fill in the search box'"
            value={command}
            onChange={(e) => setCommand(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter' && command) {
                onAction('nla', { command });
                setCommand('');
              }
            }}
          />
          <Button
            onClick={() => {
              if (command) {
                onAction('nla', { command });
                setCommand('');
              }
            }}
            disabled={!command}
            loading={loading}
          >
            <Icons.Magic />
            Execute
          </Button>
        </div>
      </Card>

      {/* Action Buttons */}
      <div style={{ display: 'flex', gap: '8px', flexWrap: 'wrap' }}>
        <Button variant="secondary" onClick={() => onAction('screenshot')}>
          <Icons.Camera />
          Screenshot
        </Button>
        <Button variant="secondary" onClick={() => onAction('extract_text')}>
          Extract Text
        </Button>
        <Button variant="secondary" onClick={() => onAction('summarize')}>
          <Icons.Magic />
          Summarize
        </Button>
        <Button variant="secondary" onClick={() => onAction('reload')}>
          <Icons.Refresh />
          Reload
        </Button>
      </div>
    </div>
  );
};

// Screenshot Viewer
interface ScreenshotViewerProps {
  screenshot: string;
  url?: string;
  onClose: () => void;
}

const ScreenshotViewer: React.FC<ScreenshotViewerProps> = ({ screenshot, url, onClose }) => {
  const { colors, isDark } = useTheme();
  const styles = createStyles(isDark, colors);

  return (
    <div>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '16px' }}>
        <h3 style={{ margin: 0 }}>Screenshot</h3>
        <Button variant="icon" onClick={onClose}>
          <Icons.Close />
        </Button>
      </div>
      {url && (
        <div style={{ ...styles.urlBar, marginBottom: '16px' }}>
          <Icons.Link />
          <span style={{ fontSize: '13px', color: colors.textSecondary }}>{url}</span>
        </div>
      )}
      <img
        src={`data:image/png;base64,${screenshot}`}
        alt="Screenshot"
        style={styles.screenshot}
      />
    </div>
  );
};

// Recording Panel
interface RecordingPanelProps {
  contextId: string;
  isRecording: boolean;
  duration: number;
  onStart: () => void;
  onStop: () => void;
  onPause: () => void;
}

const RecordingPanel: React.FC<RecordingPanelProps> = ({
  contextId,
  isRecording,
  duration,
  onStart,
  onStop,
  onPause
}) => {
  const { colors, isDark } = useTheme();
  const styles = createStyles(isDark, colors);

  const formatDuration = (seconds: number) => {
    const mins = Math.floor(seconds / 60);
    const secs = seconds % 60;
    return `${mins.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
  };

  return (
    <Card title="Session Recording">
      <div style={{ display: 'flex', alignItems: 'center', gap: '16px' }}>
        {isRecording ? (
          <>
            <div style={{
              ...styles.statusDot,
              backgroundColor: COLORS.error,
              animation: 'pulse 1s infinite'
            }} />
            <span style={{ fontWeight: 600, fontSize: '24px', fontFamily: 'monospace' }}>
              {formatDuration(duration)}
            </span>
            <div style={{ flex: 1 }} />
            <Button variant="secondary" onClick={onPause}>
              Pause
            </Button>
            <Button onClick={onStop}>
              <Icons.Stop />
              Stop
            </Button>
          </>
        ) : (
          <>
            <span style={{ color: colors.textMuted }}>Not recording</span>
            <div style={{ flex: 1 }} />
            <Button onClick={onStart}>
              <Icons.Record />
              Start Recording
            </Button>
          </>
        )}
      </div>
    </Card>
  );
};

// =============================================================================
// MAIN APP COMPONENT
// =============================================================================

const App: React.FC = () => {
  const { openai, theme, displayMode: apiDisplayMode } = useOpenAI();

  // State management
  const [state, setState] = useWidgetState<AppState>({
    connected: false,
    contexts: [],
    activeContextId: null,
    loading: false,
    error: null,
    displayMode: 'inline',
    view: 'dashboard'
  });

  // Theme
  const isDark = theme === 'dark';
  const colors = isDark ? COLORS.dark : COLORS.light;
  const styles = createStyles(isDark, colors);

  // Recording state
  const [isRecording, setIsRecording] = useState(false);
  const [recordingDuration, setRecordingDuration] = useState(0);

  // Screenshot state
  const [currentScreenshot, setCurrentScreenshot] = useState<string | null>(null);

  // Load initial state from toolOutput
  useEffect(() => {
    if (openai?.toolOutput) {
      const output = openai.toolOutput;
      if (output.contexts) {
        setState({ ...state, contexts: output.contexts, connected: true });
      }
      if (output.screenshot) {
        setCurrentScreenshot(output.screenshot);
        setState({ ...state, view: 'screenshot' });
      }
    }
  }, [openai?.toolOutput]);

  // Call tool helper
  const callTool = useCallback(async (toolName: string, params: any = {}) => {
    if (!openai?.callTool) {
      console.log('Mock tool call:', toolName, params);
      return { success: true, message: `Executed ${toolName}` };
    }

    setState({ ...state, loading: true, error: null });
    try {
      const result = await openai.callTool(toolName, params);
      setState({ ...state, loading: false });
      return result;
    } catch (error: any) {
      setState({ ...state, loading: false, error: error.message });
      throw error;
    }
  }, [openai, state, setState]);

  // Action handlers
  const handleAction = useCallback(async (action: string, params?: any) => {
    try {
      switch (action) {
        case 'create_context': {
          const result = await callTool('browser_create_context', {});
          if (result.context_id) {
            setState({
              ...state,
              connected: true,
              contexts: [...state.contexts, {
                id: result.context_id,
                url: null,
                title: null,
                created: new Date().toISOString(),
                status: 'active'
              }],
              activeContextId: result.context_id
            });
          }
          break;
        }

        case 'select_context': {
          setState({ ...state, activeContextId: params.contextId, view: 'context' });
          break;
        }

        case 'close_context': {
          await callTool('browser_close_context', { context_id: params.contextId });
          setState({
            ...state,
            contexts: state.contexts.filter(c => c.id !== params.contextId),
            activeContextId: state.activeContextId === params.contextId ? null : state.activeContextId,
            view: state.activeContextId === params.contextId ? 'dashboard' : state.view
          });
          break;
        }

        case 'navigate': {
          if (!state.activeContextId) return;
          await callTool('browser_navigate', {
            context_id: state.activeContextId,
            url: params.url,
            wait_until: 'load'
          });
          setState({
            ...state,
            contexts: state.contexts.map(c =>
              c.id === state.activeContextId
                ? { ...c, url: params.url, status: 'active' }
                : c
            )
          });
          break;
        }

        case 'screenshot': {
          if (!state.activeContextId) return;
          const result = await callTool('browser_screenshot', {
            context_id: state.activeContextId,
            mode: 'viewport'
          });
          if (result.screenshot) {
            setCurrentScreenshot(result.screenshot);
            setState({ ...state, view: 'screenshot' });
          }
          break;
        }

        case 'nla': {
          if (!state.activeContextId) return;
          await callTool('browser_nla', {
            context_id: state.activeContextId,
            command: params.command
          });
          break;
        }

        case 'summarize': {
          if (!state.activeContextId) return;
          await callTool('browser_summarize_page', {
            context_id: state.activeContextId
          });
          break;
        }

        case 'extract_text': {
          if (!state.activeContextId) return;
          await callTool('browser_extract_text', {
            context_id: state.activeContextId
          });
          break;
        }

        case 'reload': {
          if (!state.activeContextId) return;
          await callTool('browser_reload', {
            context_id: state.activeContextId
          });
          break;
        }

        case 'record': {
          setState({ ...state, view: 'recording' });
          break;
        }

        case 'start_recording': {
          if (!state.activeContextId) return;
          await callTool('browser_start_video_recording', {
            context_id: state.activeContextId
          });
          setIsRecording(true);
          break;
        }

        case 'stop_recording': {
          if (!state.activeContextId) return;
          await callTool('browser_stop_video_recording', {
            context_id: state.activeContextId
          });
          setIsRecording(false);
          setRecordingDuration(0);
          break;
        }

        case 'expand': {
          if (openai?.requestDisplayMode) {
            openai.requestDisplayMode('fullscreen');
          }
          setState({ ...state, displayMode: 'fullscreen' });
          break;
        }

        case 'minimize': {
          if (openai?.requestDisplayMode) {
            openai.requestDisplayMode('inline');
          }
          setState({ ...state, displayMode: 'inline' });
          break;
        }

        case 'back': {
          setState({ ...state, view: 'dashboard' });
          break;
        }

        default:
          console.log('Unknown action:', action, params);
      }
    } catch (error) {
      console.error('Action failed:', error);
    }
  }, [callTool, openai, state, setState]);

  // Recording timer
  useEffect(() => {
    let interval: NodeJS.Timeout;
    if (isRecording) {
      interval = setInterval(() => {
        setRecordingDuration(d => d + 1);
      }, 1000);
    }
    return () => clearInterval(interval);
  }, [isRecording]);

  // Get active context
  const activeContext = useMemo(() =>
    state.contexts.find(c => c.id === state.activeContextId),
    [state.contexts, state.activeContextId]
  );

  return (
    <ThemeContext.Provider value={{ isDark, colors }}>
      <style>{`
        @keyframes spin {
          from { transform: rotate(0deg); }
          to { transform: rotate(360deg); }
        }
        @keyframes pulse {
          0%, 100% { opacity: 1; }
          50% { opacity: 0.5; }
        }
        * { box-sizing: border-box; }
        input:focus { outline: 2px solid ${COLORS.primary}; outline-offset: -2px; }
        button:hover:not(:disabled) { filter: brightness(1.1); }
      `}</style>

      <div style={styles.container}>
        {/* Header */}
        <div style={styles.header}>
          <div style={styles.headerTitle}>
            <Icons.Owl />
            <span>Owl Browser</span>
            {state.connected && (
              <span style={{
                ...styles.badge,
                ...styles.badgeSuccess,
                fontSize: '11px'
              }}>
                Connected
              </span>
            )}
          </div>
          <div style={styles.headerActions}>
            {state.view !== 'dashboard' && (
              <Button variant="icon" onClick={() => handleAction('back')}>
                <Icons.Close />
              </Button>
            )}
            <Button
              variant="icon"
              onClick={() => handleAction(state.displayMode === 'fullscreen' ? 'minimize' : 'expand')}
            >
              {state.displayMode === 'fullscreen' ? <Icons.Minimize /> : <Icons.Expand />}
            </Button>
          </div>
        </div>

        {/* Content */}
        <div style={styles.content}>
          {/* Error Display */}
          {state.error && (
            <Card style={{ backgroundColor: `${COLORS.error}10`, borderColor: COLORS.error, marginBottom: '16px' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: '12px', color: COLORS.error }}>
                <Icons.AlertCircle />
                <span>{state.error}</span>
              </div>
            </Card>
          )}

          {/* Views */}
          {state.view === 'dashboard' && (
            <Dashboard state={state} onAction={handleAction} />
          )}

          {state.view === 'context' && activeContext && (
            <ContextView
              context={activeContext}
              onAction={handleAction}
              loading={state.loading}
            />
          )}

          {state.view === 'screenshot' && currentScreenshot && (
            <ScreenshotViewer
              screenshot={currentScreenshot}
              url={activeContext?.url || undefined}
              onClose={() => setState({ ...state, view: activeContext ? 'context' : 'dashboard' })}
            />
          )}

          {state.view === 'recording' && state.activeContextId && (
            <RecordingPanel
              contextId={state.activeContextId}
              isRecording={isRecording}
              duration={recordingDuration}
              onStart={() => handleAction('start_recording')}
              onStop={() => handleAction('stop_recording')}
              onPause={() => {}}
            />
          )}
        </div>
      </div>
    </ThemeContext.Provider>
  );
};

// =============================================================================
// MOUNT
// =============================================================================

// Auto-mount when loaded
const root = document.getElementById('owl-browser-root');
if (root) {
  createRoot(root).render(<App />);
} else {
  // Create root element if not present
  const container = document.createElement('div');
  container.id = 'owl-browser-root';
  document.body.appendChild(container);
  createRoot(container).render(<App />);
}

export default App;
