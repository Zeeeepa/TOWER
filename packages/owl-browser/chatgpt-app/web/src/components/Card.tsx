/**
 * Owl Browser ChatGPT App - Card Component
 *
 * Card components for structured content display.
 *
 * @author Olib AI
 * @license MIT
 */

import React from 'react';
import { COLORS, RADII, SPACING, SHADOWS, TRANSITIONS, getTheme } from '../utils/theme';

// =============================================================================
// TYPES
// =============================================================================

export interface CardProps {
  children: React.ReactNode;
  title?: string;
  subtitle?: string;
  icon?: React.ReactNode;
  actions?: React.ReactNode;
  variant?: 'default' | 'outlined' | 'elevated' | 'filled';
  status?: 'default' | 'success' | 'warning' | 'error' | 'info';
  clickable?: boolean;
  onClick?: () => void;
  isDark?: boolean;
  style?: React.CSSProperties;
}

// =============================================================================
// CARD COMPONENT
// =============================================================================

export const Card: React.FC<CardProps> = ({
  children,
  title,
  subtitle,
  icon,
  actions,
  variant = 'default',
  status = 'default',
  clickable = false,
  onClick,
  isDark = false,
  style
}) => {
  const theme = getTheme(isDark);

  const getVariantStyles = (): React.CSSProperties => {
    const styles: Record<typeof variant, React.CSSProperties> = {
      default: {
        backgroundColor: theme.bgSecondary,
        border: `1px solid ${theme.border}`
      },
      outlined: {
        backgroundColor: 'transparent',
        border: `1px solid ${theme.border}`
      },
      elevated: {
        backgroundColor: theme.bg,
        border: 'none',
        boxShadow: SHADOWS.md
      },
      filled: {
        backgroundColor: theme.bgTertiary,
        border: 'none'
      }
    };
    return styles[variant];
  };

  const getStatusBorderColor = (): string | undefined => {
    if (status === 'default') return undefined;

    const colors = {
      success: COLORS.success,
      warning: COLORS.warning,
      error: COLORS.error,
      info: COLORS.info
    };
    return colors[status];
  };

  const cardStyle: React.CSSProperties = {
    borderRadius: RADII.lg,
    padding: SPACING.lg,
    transition: TRANSITIONS.normal,
    cursor: clickable ? 'pointer' : 'default',
    ...getVariantStyles(),
    ...(getStatusBorderColor() && {
      borderColor: getStatusBorderColor(),
      borderLeftWidth: '3px'
    }),
    ...style
  };

  const headerStyle: React.CSSProperties = {
    display: 'flex',
    alignItems: 'flex-start',
    justifyContent: 'space-between',
    marginBottom: title || subtitle ? SPACING.md : 0
  };

  const titleContainerStyle: React.CSSProperties = {
    display: 'flex',
    alignItems: 'center',
    gap: SPACING.sm
  };

  const titleStyle: React.CSSProperties = {
    margin: 0,
    fontSize: '16px',
    fontWeight: 600,
    color: theme.text
  };

  const subtitleStyle: React.CSSProperties = {
    margin: `${SPACING.xs} 0 0 0`,
    fontSize: '13px',
    color: theme.textSecondary
  };

  return (
    <div style={cardStyle} onClick={clickable ? onClick : undefined}>
      {(title || icon || actions) && (
        <div style={headerStyle}>
          <div style={titleContainerStyle}>
            {icon && <span style={{ color: COLORS.primary }}>{icon}</span>}
            <div>
              {title && <h3 style={titleStyle}>{title}</h3>}
              {subtitle && <p style={subtitleStyle}>{subtitle}</p>}
            </div>
          </div>
          {actions && <div>{actions}</div>}
        </div>
      )}
      {children}
    </div>
  );
};

// =============================================================================
// CARD HEADER
// =============================================================================

export interface CardHeaderProps {
  title: string;
  subtitle?: string;
  icon?: React.ReactNode;
  actions?: React.ReactNode;
  isDark?: boolean;
}

export const CardHeader: React.FC<CardHeaderProps> = ({
  title,
  subtitle,
  icon,
  actions,
  isDark = false
}) => {
  const theme = getTheme(isDark);

  return (
    <div style={{
      display: 'flex',
      alignItems: 'flex-start',
      justifyContent: 'space-between',
      marginBottom: SPACING.md
    }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: SPACING.sm }}>
        {icon && <span style={{ color: COLORS.primary }}>{icon}</span>}
        <div>
          <h3 style={{ margin: 0, fontSize: '16px', fontWeight: 600, color: theme.text }}>
            {title}
          </h3>
          {subtitle && (
            <p style={{ margin: `${SPACING.xs} 0 0 0`, fontSize: '13px', color: theme.textSecondary }}>
              {subtitle}
            </p>
          )}
        </div>
      </div>
      {actions && <div>{actions}</div>}
    </div>
  );
};

// =============================================================================
// CARD CONTENT
// =============================================================================

export interface CardContentProps {
  children: React.ReactNode;
  noPadding?: boolean;
}

export const CardContent: React.FC<CardContentProps> = ({
  children,
  noPadding = false
}) => {
  return (
    <div style={{ padding: noPadding ? 0 : `${SPACING.sm} 0` }}>
      {children}
    </div>
  );
};

// =============================================================================
// CARD FOOTER
// =============================================================================

export interface CardFooterProps {
  children: React.ReactNode;
  justify?: 'start' | 'end' | 'center' | 'between';
  isDark?: boolean;
}

export const CardFooter: React.FC<CardFooterProps> = ({
  children,
  justify = 'end',
  isDark = false
}) => {
  const theme = getTheme(isDark);

  const justifyMap = {
    start: 'flex-start',
    end: 'flex-end',
    center: 'center',
    between: 'space-between'
  };

  return (
    <div style={{
      display: 'flex',
      alignItems: 'center',
      justifyContent: justifyMap[justify],
      gap: SPACING.sm,
      marginTop: SPACING.md,
      paddingTop: SPACING.md,
      borderTop: `1px solid ${theme.border}`
    }}>
      {children}
    </div>
  );
};

// =============================================================================
// STAT CARD
// =============================================================================

export interface StatCardProps {
  label: string;
  value: string | number;
  change?: {
    value: string | number;
    type: 'positive' | 'negative' | 'neutral';
  };
  icon?: React.ReactNode;
  isDark?: boolean;
}

export const StatCard: React.FC<StatCardProps> = ({
  label,
  value,
  change,
  icon,
  isDark = false
}) => {
  const theme = getTheme(isDark);

  const getChangeColor = () => {
    if (!change) return theme.textSecondary;
    return change.type === 'positive' ? COLORS.success
         : change.type === 'negative' ? COLORS.error
         : theme.textSecondary;
  };

  return (
    <Card isDark={isDark}>
      <div style={{ display: 'flex', alignItems: 'flex-start', justifyContent: 'space-between' }}>
        <div>
          <p style={{ margin: 0, fontSize: '13px', color: theme.textMuted, marginBottom: SPACING.xs }}>
            {label}
          </p>
          <p style={{ margin: 0, fontSize: '28px', fontWeight: 700, color: theme.text }}>
            {value}
          </p>
          {change && (
            <p style={{ margin: `${SPACING.xs} 0 0 0`, fontSize: '13px', color: getChangeColor() }}>
              {change.type === 'positive' ? '+' : change.type === 'negative' ? '' : ''}
              {change.value}
            </p>
          )}
        </div>
        {icon && (
          <div style={{
            width: 48,
            height: 48,
            borderRadius: RADII.lg,
            backgroundColor: `${COLORS.primary}15`,
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            color: COLORS.primary
          }}>
            {icon}
          </div>
        )}
      </div>
    </Card>
  );
};

// =============================================================================
// ACTION CARD
// =============================================================================

export interface ActionCardProps {
  icon: React.ReactNode;
  title: string;
  description: string;
  onClick: () => void;
  color?: string;
  isDark?: boolean;
}

export const ActionCard: React.FC<ActionCardProps> = ({
  icon,
  title,
  description,
  onClick,
  color = COLORS.primary,
  isDark = false
}) => {
  const theme = getTheme(isDark);

  return (
    <div
      onClick={onClick}
      style={{
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        gap: SPACING.sm,
        padding: SPACING.xl,
        borderRadius: RADII.lg,
        backgroundColor: theme.bgSecondary,
        border: `1px solid ${theme.border}`,
        cursor: 'pointer',
        transition: TRANSITIONS.normal,
        textAlign: 'center'
      }}
      onMouseOver={(e) => {
        (e.currentTarget as HTMLDivElement).style.borderColor = color;
        (e.currentTarget as HTMLDivElement).style.transform = 'translateY(-2px)';
      }}
      onMouseOut={(e) => {
        (e.currentTarget as HTMLDivElement).style.borderColor = theme.border;
        (e.currentTarget as HTMLDivElement).style.transform = 'translateY(0)';
      }}
    >
      <div style={{
        width: 48,
        height: 48,
        borderRadius: RADII.lg,
        backgroundColor: `${color}15`,
        color: color,
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center'
      }}>
        {icon}
      </div>
      <div style={{ fontWeight: 600, fontSize: '14px', color: theme.text }}>{title}</div>
      <div style={{ fontSize: '12px', color: theme.textMuted }}>{description}</div>
    </div>
  );
};

export default Card;
