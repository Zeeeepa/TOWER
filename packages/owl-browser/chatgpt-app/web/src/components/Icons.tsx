/**
 * Owl Browser ChatGPT App - Icon Components
 *
 * SVG icon components matching ChatGPT's design language.
 *
 * @author Olib AI
 * @license MIT
 */

import React from 'react';

interface IconProps {
  size?: number;
  color?: string;
  className?: string;
  style?: React.CSSProperties;
}

const createIcon = (paths: React.ReactNode, viewBox = '0 0 24 24') => {
  const Icon: React.FC<IconProps> = ({ size = 20, color = 'currentColor', className, style }) => (
    <svg
      viewBox={viewBox}
      width={size}
      height={size}
      fill="none"
      stroke={color}
      strokeWidth="2"
      strokeLinecap="round"
      strokeLinejoin="round"
      className={className}
      style={style}
    >
      {paths}
    </svg>
  );
  return Icon;
};

// =============================================================================
// BRAND ICON
// =============================================================================

export const OwlLogo: React.FC<IconProps> = ({ size = 24, color = 'currentColor', style }) => (
  <svg viewBox="0 0 24 24" width={size} height={size} fill={color} style={style}>
    {/* Eyes */}
    <circle cx="8" cy="10" r="3" fill="none" stroke={color} strokeWidth="2"/>
    <circle cx="16" cy="10" r="3" fill="none" stroke={color} strokeWidth="2"/>
    <circle cx="8" cy="10" r="1"/>
    <circle cx="16" cy="10" r="1"/>
    {/* Beak */}
    <path d="M12 14 L10 18 L12 17 L14 18 Z"/>
    {/* Ears/Tufts */}
    <path d="M5 8 Q2 4 6 3" stroke={color} strokeWidth="2" fill="none"/>
    <path d="M19 8 Q22 4 18 3" stroke={color} strokeWidth="2" fill="none"/>
  </svg>
);

// =============================================================================
// NAVIGATION ICONS
// =============================================================================

export const Browser = createIcon(
  <>
    <rect x="3" y="4" width="18" height="16" rx="2"/>
    <line x1="3" y1="9" x2="21" y2="9"/>
    <circle cx="6" cy="6.5" r="0.5" fill="currentColor"/>
    <circle cx="8.5" cy="6.5" r="0.5" fill="currentColor"/>
    <circle cx="11" cy="6.5" r="0.5" fill="currentColor"/>
  </>
);

export const Navigate = createIcon(
  <>
    <circle cx="12" cy="12" r="10"/>
    <polygon points="12,8 15,14 12,12 9,14" fill="currentColor" stroke="none"/>
  </>
);

export const ArrowLeft = createIcon(
  <>
    <line x1="19" y1="12" x2="5" y2="12"/>
    <polyline points="12 19 5 12 12 5"/>
  </>
);

export const ArrowRight = createIcon(
  <>
    <line x1="5" y1="12" x2="19" y2="12"/>
    <polyline points="12 5 19 12 12 19"/>
  </>
);

export const Refresh = createIcon(
  <>
    <polyline points="23 4 23 10 17 10"/>
    <path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10"/>
  </>
);

export const Home = createIcon(
  <>
    <path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>
    <polyline points="9 22 9 12 15 12 15 22"/>
  </>
);

// =============================================================================
// ACTION ICONS
// =============================================================================

export const Click = createIcon(
  <path d="M6 3v12l4-4 3 6 2-1-3-6h5z" fill="currentColor" stroke="none"/>
);

export const Type = createIcon(
  <>
    <polyline points="4 7 4 4 20 4 20 7"/>
    <line x1="9" y1="20" x2="15" y2="20"/>
    <line x1="12" y1="4" x2="12" y2="20"/>
  </>
);

export const Scroll = createIcon(
  <>
    <rect x="9" y="3" width="6" height="18" rx="3"/>
    <line x1="12" y1="7" x2="12" y2="12"/>
    <path d="M12 17v.01"/>
  </>
);

export const Hover = createIcon(
  <>
    <path d="M4 4h6v6H4z"/>
    <path d="M14 4h6v6h-6z"/>
    <path d="M4 14h6v6H4z"/>
    <path d="M17 17v.01"/>
    <path d="M20 17v.01"/>
    <path d="M17 20v.01"/>
    <path d="M20 20v.01"/>
  </>
);

// =============================================================================
// EXTRACTION ICONS
// =============================================================================

export const Camera = createIcon(
  <>
    <rect x="2" y="6" width="20" height="14" rx="2"/>
    <circle cx="12" cy="13" r="4"/>
    <path d="M7 6l2-3h6l2 3"/>
  </>
);

export const Text = createIcon(
  <>
    <line x1="17" y1="10" x2="3" y2="10"/>
    <line x1="21" y1="6" x2="3" y2="6"/>
    <line x1="21" y1="14" x2="3" y2="14"/>
    <line x1="17" y1="18" x2="3" y2="18"/>
  </>
);

export const Code = createIcon(
  <>
    <polyline points="16 18 22 12 16 6"/>
    <polyline points="8 6 2 12 8 18"/>
  </>
);

export const Markdown = createIcon(
  <>
    <rect x="2" y="4" width="20" height="16" rx="2"/>
    <path d="M6 8v8"/>
    <path d="M6 12l3-4 3 4"/>
    <path d="M15 8v8l3-3 3 3v-8"/>
  </>
);

// =============================================================================
// AI ICONS
// =============================================================================

export const Magic = createIcon(
  <>
    <path d="M12 3l1.5 5L19 8l-4.5 3.5 1.5 5.5-4-3-4 3 1.5-5.5L5 8l5.5 0z" fill="currentColor" stroke="none"/>
  </>
);

export const Brain = createIcon(
  <>
    <path d="M9.5 2A2.5 2.5 0 0 1 12 4.5v15a2.5 2.5 0 0 1-4.96.44 2.5 2.5 0 0 1-2.96-3.08 3 3 0 0 1-.34-5.58 2.5 2.5 0 0 1 1.32-4.24 2.5 2.5 0 0 1 4.44-2z"/>
    <path d="M14.5 2A2.5 2.5 0 0 0 12 4.5v15a2.5 2.5 0 0 0 4.96.44 2.5 2.5 0 0 0 2.96-3.08 3 3 0 0 0 .34-5.58 2.5 2.5 0 0 0-1.32-4.24 2.5 2.5 0 0 0-4.44-2z"/>
  </>
);

export const Sparkles = createIcon(
  <>
    <path d="M12 3l1.5 4.5L18 9l-4.5 1.5L12 15l-1.5-4.5L6 9l4.5-1.5z" fill="currentColor"/>
    <path d="M19 10l.5 1.5L21 12l-1.5.5-.5 1.5-.5-1.5L17 12l1.5-.5z" fill="currentColor"/>
    <path d="M5 17l.5 1.5L7 19l-1.5.5L5 21l-.5-1.5L3 19l1.5-.5z" fill="currentColor"/>
  </>
);

// =============================================================================
// MEDIA ICONS
// =============================================================================

export const Record = createIcon(
  <>
    <circle cx="12" cy="12" r="10"/>
    <circle cx="12" cy="12" r="4" fill="currentColor" stroke="none"/>
  </>
);

export const Play = createIcon(
  <polygon points="5,3 19,12 5,21" fill="currentColor" stroke="none"/>
);

export const Pause = createIcon(
  <>
    <rect x="6" y="4" width="4" height="16" fill="currentColor" stroke="none"/>
    <rect x="14" y="4" width="4" height="16" fill="currentColor" stroke="none"/>
  </>
);

export const Stop = createIcon(
  <rect x="5" y="5" width="14" height="14" rx="2" fill="currentColor" stroke="none"/>
);

export const Video = createIcon(
  <>
    <rect x="2" y="6" width="14" height="12" rx="2"/>
    <polygon points="22,8 16,12 22,16"/>
  </>
);

// =============================================================================
// UI ICONS
// =============================================================================

export const Plus = createIcon(
  <>
    <line x1="12" y1="5" x2="12" y2="19"/>
    <line x1="5" y1="12" x2="19" y2="12"/>
  </>
);

export const Minus = createIcon(
  <line x1="5" y1="12" x2="19" y2="12"/>
);

export const Close = createIcon(
  <>
    <line x1="18" y1="6" x2="6" y2="18"/>
    <line x1="6" y1="6" x2="18" y2="18"/>
  </>
);

export const Check = createIcon(
  <polyline points="20 6 9 17 4 12"/>
);

export const ChevronUp = createIcon(
  <polyline points="18 15 12 9 6 15"/>
);

export const ChevronDown = createIcon(
  <polyline points="6 9 12 15 18 9"/>
);

export const ChevronLeft = createIcon(
  <polyline points="15 18 9 12 15 6"/>
);

export const ChevronRight = createIcon(
  <polyline points="9 6 15 12 9 18"/>
);

export const Expand = createIcon(
  <>
    <polyline points="15 3 21 3 21 9"/>
    <polyline points="9 21 3 21 3 15"/>
    <line x1="21" y1="3" x2="14" y2="10"/>
    <line x1="3" y1="21" x2="10" y2="14"/>
  </>
);

export const Minimize = createIcon(
  <>
    <polyline points="4 14 10 14 10 20"/>
    <polyline points="20 10 14 10 14 4"/>
    <line x1="14" y1="10" x2="21" y2="3"/>
    <line x1="3" y1="21" x2="10" y2="14"/>
  </>
);

export const Menu = createIcon(
  <>
    <line x1="3" y1="6" x2="21" y2="6"/>
    <line x1="3" y1="12" x2="21" y2="12"/>
    <line x1="3" y1="18" x2="21" y2="18"/>
  </>
);

export const MoreHorizontal = createIcon(
  <>
    <circle cx="12" cy="12" r="1" fill="currentColor"/>
    <circle cx="6" cy="12" r="1" fill="currentColor"/>
    <circle cx="18" cy="12" r="1" fill="currentColor"/>
  </>
);

export const MoreVertical = createIcon(
  <>
    <circle cx="12" cy="12" r="1" fill="currentColor"/>
    <circle cx="12" cy="6" r="1" fill="currentColor"/>
    <circle cx="12" cy="18" r="1" fill="currentColor"/>
  </>
);

// =============================================================================
// STATUS ICONS
// =============================================================================

export const AlertCircle = createIcon(
  <>
    <circle cx="12" cy="12" r="10"/>
    <line x1="12" y1="8" x2="12" y2="12"/>
    <line x1="12" y1="16" x2="12.01" y2="16"/>
  </>
);

export const AlertTriangle = createIcon(
  <>
    <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/>
    <line x1="12" y1="9" x2="12" y2="13"/>
    <line x1="12" y1="17" x2="12.01" y2="17"/>
  </>
);

export const Info = createIcon(
  <>
    <circle cx="12" cy="12" r="10"/>
    <line x1="12" y1="16" x2="12" y2="12"/>
    <line x1="12" y1="8" x2="12.01" y2="8"/>
  </>
);

export const CheckCircle = createIcon(
  <>
    <path d="M22 11.08V12a10 10 0 1 1-5.93-9.14"/>
    <polyline points="22 4 12 14.01 9 11.01"/>
  </>
);

export const Loader = createIcon(
  <>
    <circle cx="12" cy="12" r="10" strokeOpacity="0.25"/>
    <path d="M12 2 A10 10 0 0 1 22 12" strokeLinecap="round" style={{ animation: 'spin 1s linear infinite' }}/>
  </>
);

// =============================================================================
// UTILITY ICONS
// =============================================================================

export const Settings = createIcon(
  <>
    <circle cx="12" cy="12" r="3"/>
    <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"/>
  </>
);

export const Search = createIcon(
  <>
    <circle cx="11" cy="11" r="8"/>
    <line x1="21" y1="21" x2="16.65" y2="16.65"/>
  </>
);

export const Link = createIcon(
  <>
    <path d="M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71"/>
    <path d="M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71"/>
  </>
);

export const ExternalLink = createIcon(
  <>
    <path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"/>
    <polyline points="15 3 21 3 21 9"/>
    <line x1="10" y1="14" x2="21" y2="3"/>
  </>
);

export const Copy = createIcon(
  <>
    <rect x="9" y="9" width="13" height="13" rx="2"/>
    <path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/>
  </>
);

export const Download = createIcon(
  <>
    <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>
    <polyline points="7 10 12 15 17 10"/>
    <line x1="12" y1="15" x2="12" y2="3"/>
  </>
);

export const Upload = createIcon(
  <>
    <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>
    <polyline points="17 8 12 3 7 8"/>
    <line x1="12" y1="3" x2="12" y2="15"/>
  </>
);

export const Cookie = createIcon(
  <>
    <circle cx="12" cy="12" r="10"/>
    <circle cx="8" cy="9" r="1" fill="currentColor"/>
    <circle cx="15" cy="9" r="1" fill="currentColor"/>
    <circle cx="11" cy="14" r="1" fill="currentColor"/>
    <circle cx="16" cy="14" r="1" fill="currentColor"/>
    <circle cx="8" cy="16" r="1" fill="currentColor"/>
  </>
);

export const Shield = createIcon(
  <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/>
);

export const User = createIcon(
  <>
    <circle cx="12" cy="7" r="4"/>
    <path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/>
  </>
);

export const Clock = createIcon(
  <>
    <circle cx="12" cy="12" r="10"/>
    <polyline points="12 6 12 12 16 14"/>
  </>
);

export const Globe = createIcon(
  <>
    <circle cx="12" cy="12" r="10"/>
    <line x1="2" y1="12" x2="22" y2="12"/>
    <path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"/>
  </>
);

export const Puzzle = createIcon(
  <>
    <path d="M20 14v-2a2 2 0 0 0-2-2h-4a2 2 0 0 0-2 2v2"/>
    <path d="M17 14a2 2 0 1 1 0-4"/>
    <path d="M4 20h8a2 2 0 0 0 2-2v-8a2 2 0 0 0-2-2H4a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2z"/>
    <path d="M7 10a2 2 0 1 1 0-4"/>
    <path d="M12 17a2 2 0 1 1-4 0"/>
  </>
);

// =============================================================================
// EXPORT ALL
// =============================================================================

const Icons = {
  OwlLogo,
  Browser,
  Navigate,
  ArrowLeft,
  ArrowRight,
  Refresh,
  Home,
  Click,
  Type,
  Scroll,
  Hover,
  Camera,
  Text,
  Code,
  Markdown,
  Magic,
  Brain,
  Sparkles,
  Record,
  Play,
  Pause,
  Stop,
  Video,
  Plus,
  Minus,
  Close,
  Check,
  ChevronUp,
  ChevronDown,
  ChevronLeft,
  ChevronRight,
  Expand,
  Minimize,
  Menu,
  MoreHorizontal,
  MoreVertical,
  AlertCircle,
  AlertTriangle,
  Info,
  CheckCircle,
  Loader,
  Settings,
  Search,
  Link,
  ExternalLink,
  Copy,
  Download,
  Upload,
  Cookie,
  Shield,
  User,
  Clock,
  Globe,
  Puzzle
};

export default Icons;
