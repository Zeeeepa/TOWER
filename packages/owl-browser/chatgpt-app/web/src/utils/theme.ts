/**
 * Owl Browser ChatGPT App - Theme System
 *
 * Brand color: #6BA894 (Owl Teal)
 *
 * @author Olib AI
 * @license MIT
 */

// =============================================================================
// COLOR PALETTE
// =============================================================================

export const COLORS = {
  // Brand colors
  primary: '#6BA894',
  primaryDark: '#5A9683',
  primaryLight: '#7CBAA5',
  primaryLighter: '#E8F4F0',
  primaryTransparent: 'rgba(107, 168, 148, 0.15)',

  // Secondary
  secondary: '#4A5568',
  secondaryDark: '#2D3748',
  secondaryLight: '#718096',

  // Semantic colors
  success: '#48BB78',
  successLight: '#9AE6B4',
  successDark: '#25855A',

  warning: '#ECC94B',
  warningLight: '#FAF089',
  warningDark: '#B7791F',

  error: '#F56565',
  errorLight: '#FEB2B2',
  errorDark: '#C53030',

  info: '#4299E1',
  infoLight: '#90CDF4',
  infoDark: '#2B6CB0',

  // Neutral grays
  gray50: '#F7FAFC',
  gray100: '#EDF2F7',
  gray200: '#E2E8F0',
  gray300: '#CBD5E0',
  gray400: '#A0AEC0',
  gray500: '#718096',
  gray600: '#4A5568',
  gray700: '#2D3748',
  gray800: '#1A202C',
  gray900: '#171923'
};

// =============================================================================
// THEME DEFINITIONS
// =============================================================================

export interface ThemeColors {
  bg: string;
  bgSecondary: string;
  bgTertiary: string;
  bgHover: string;
  text: string;
  textSecondary: string;
  textMuted: string;
  border: string;
  borderLight: string;
  shadow: string;
  shadowMedium: string;
  overlay: string;
}

export const lightTheme: ThemeColors = {
  bg: '#FFFFFF',
  bgSecondary: '#F7FAFC',
  bgTertiary: '#EDF2F7',
  bgHover: '#E2E8F0',
  text: '#1A202C',
  textSecondary: '#4A5568',
  textMuted: '#A0AEC0',
  border: '#E2E8F0',
  borderLight: '#EDF2F7',
  shadow: 'rgba(0, 0, 0, 0.1)',
  shadowMedium: 'rgba(0, 0, 0, 0.15)',
  overlay: 'rgba(0, 0, 0, 0.5)'
};

export const darkTheme: ThemeColors = {
  bg: '#1A202C',
  bgSecondary: '#2D3748',
  bgTertiary: '#4A5568',
  bgHover: '#4A5568',
  text: '#F7FAFC',
  textSecondary: '#CBD5E0',
  textMuted: '#718096',
  border: '#4A5568',
  borderLight: '#2D3748',
  shadow: 'rgba(0, 0, 0, 0.3)',
  shadowMedium: 'rgba(0, 0, 0, 0.4)',
  overlay: 'rgba(0, 0, 0, 0.7)'
};

export function getTheme(isDark: boolean): ThemeColors {
  return isDark ? darkTheme : lightTheme;
}

// =============================================================================
// SPACING SYSTEM
// =============================================================================

export const SPACING = {
  xs: '4px',
  sm: '8px',
  md: '12px',
  lg: '16px',
  xl: '20px',
  xxl: '24px',
  xxxl: '32px'
};

// =============================================================================
// TYPOGRAPHY
// =============================================================================

export const TYPOGRAPHY = {
  fontFamily: '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Oxygen, Ubuntu, Cantarell, sans-serif',
  fontFamilyMono: '"SF Mono", "Monaco", "Inconsolata", "Fira Code", monospace',

  // Font sizes
  sizeXs: '11px',
  sizeSm: '12px',
  sizeMd: '14px',
  sizeLg: '16px',
  sizeXl: '18px',
  sizeXxl: '20px',
  sizeXxxl: '24px',
  sizeDisplay: '32px',

  // Font weights
  weightNormal: 400,
  weightMedium: 500,
  weightSemibold: 600,
  weightBold: 700,

  // Line heights
  lineHeightTight: 1.25,
  lineHeightNormal: 1.5,
  lineHeightRelaxed: 1.75
};

// =============================================================================
// BORDER RADIUS
// =============================================================================

export const RADII = {
  sm: '4px',
  md: '8px',
  lg: '12px',
  xl: '16px',
  full: '9999px'
};

// =============================================================================
// SHADOWS
// =============================================================================

export const SHADOWS = {
  sm: '0 1px 2px rgba(0, 0, 0, 0.05)',
  md: '0 4px 6px rgba(0, 0, 0, 0.1)',
  lg: '0 10px 15px rgba(0, 0, 0, 0.1)',
  xl: '0 20px 25px rgba(0, 0, 0, 0.15)',
  inner: 'inset 0 2px 4px rgba(0, 0, 0, 0.06)'
};

// =============================================================================
// TRANSITIONS
// =============================================================================

export const TRANSITIONS = {
  fast: '0.1s ease',
  normal: '0.2s ease',
  slow: '0.3s ease',
  spring: '0.3s cubic-bezier(0.68, -0.55, 0.265, 1.55)'
};

// =============================================================================
// Z-INDEX
// =============================================================================

export const Z_INDEX = {
  base: 0,
  dropdown: 1000,
  sticky: 1100,
  modal: 1200,
  popover: 1300,
  tooltip: 1400,
  toast: 1500
};

// =============================================================================
// BREAKPOINTS
// =============================================================================

export const BREAKPOINTS = {
  sm: '640px',
  md: '768px',
  lg: '1024px',
  xl: '1280px'
};

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

/**
 * Create a color with alpha transparency
 */
export function withAlpha(color: string, alpha: number): string {
  // Handle hex colors
  if (color.startsWith('#')) {
    const r = parseInt(color.slice(1, 3), 16);
    const g = parseInt(color.slice(3, 5), 16);
    const b = parseInt(color.slice(5, 7), 16);
    return `rgba(${r}, ${g}, ${b}, ${alpha})`;
  }
  return color;
}

/**
 * Lighten a hex color
 */
export function lighten(color: string, amount: number): string {
  const num = parseInt(color.slice(1), 16);
  const r = Math.min(255, (num >> 16) + Math.round(255 * amount));
  const g = Math.min(255, ((num >> 8) & 0x00FF) + Math.round(255 * amount));
  const b = Math.min(255, (num & 0x0000FF) + Math.round(255 * amount));
  return `#${(1 << 24 | r << 16 | g << 8 | b).toString(16).slice(1)}`;
}

/**
 * Darken a hex color
 */
export function darken(color: string, amount: number): string {
  const num = parseInt(color.slice(1), 16);
  const r = Math.max(0, (num >> 16) - Math.round(255 * amount));
  const g = Math.max(0, ((num >> 8) & 0x00FF) - Math.round(255 * amount));
  const b = Math.max(0, (num & 0x0000FF) - Math.round(255 * amount));
  return `#${(1 << 24 | r << 16 | g << 8 | b).toString(16).slice(1)}`;
}

export default {
  COLORS,
  lightTheme,
  darkTheme,
  getTheme,
  SPACING,
  TYPOGRAPHY,
  RADII,
  SHADOWS,
  TRANSITIONS,
  Z_INDEX,
  BREAKPOINTS,
  withAlpha,
  lighten,
  darken
};
