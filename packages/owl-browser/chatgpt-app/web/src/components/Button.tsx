/**
 * Owl Browser ChatGPT App - Button Component
 *
 * A versatile button component with multiple variants and states.
 *
 * @author Olib AI
 * @license MIT
 */

import React, { forwardRef, ButtonHTMLAttributes } from 'react';
import { COLORS, RADII, SPACING, TRANSITIONS, TYPOGRAPHY } from '../utils/theme';
import { Loader } from './Icons';

// =============================================================================
// TYPES
// =============================================================================

export type ButtonVariant = 'primary' | 'secondary' | 'ghost' | 'danger' | 'success' | 'outline';
export type ButtonSize = 'sm' | 'md' | 'lg';

export interface ButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: ButtonVariant;
  size?: ButtonSize;
  loading?: boolean;
  leftIcon?: React.ReactNode;
  rightIcon?: React.ReactNode;
  fullWidth?: boolean;
  isDark?: boolean;
}

// =============================================================================
// STYLES
// =============================================================================

const getVariantStyles = (variant: ButtonVariant, isDark: boolean): React.CSSProperties => {
  const styles: Record<ButtonVariant, React.CSSProperties> = {
    primary: {
      backgroundColor: COLORS.primary,
      color: '#FFFFFF',
      border: 'none'
    },
    secondary: {
      backgroundColor: isDark ? '#4A5568' : '#EDF2F7',
      color: isDark ? '#F7FAFC' : '#1A202C',
      border: 'none'
    },
    ghost: {
      backgroundColor: 'transparent',
      color: isDark ? '#F7FAFC' : '#1A202C',
      border: 'none'
    },
    danger: {
      backgroundColor: COLORS.error,
      color: '#FFFFFF',
      border: 'none'
    },
    success: {
      backgroundColor: COLORS.success,
      color: '#FFFFFF',
      border: 'none'
    },
    outline: {
      backgroundColor: 'transparent',
      color: COLORS.primary,
      border: `2px solid ${COLORS.primary}`
    }
  };

  return styles[variant];
};

const getSizeStyles = (size: ButtonSize): React.CSSProperties => {
  const styles: Record<ButtonSize, React.CSSProperties> = {
    sm: {
      padding: `${SPACING.xs} ${SPACING.md}`,
      fontSize: TYPOGRAPHY.sizeSm,
      borderRadius: RADII.md
    },
    md: {
      padding: `${SPACING.sm} ${SPACING.lg}`,
      fontSize: TYPOGRAPHY.sizeMd,
      borderRadius: RADII.md
    },
    lg: {
      padding: `${SPACING.md} ${SPACING.xl}`,
      fontSize: TYPOGRAPHY.sizeLg,
      borderRadius: RADII.lg
    }
  };

  return styles[size];
};

// =============================================================================
// COMPONENT
// =============================================================================

export const Button = forwardRef<HTMLButtonElement, ButtonProps>(({
  children,
  variant = 'primary',
  size = 'md',
  loading = false,
  leftIcon,
  rightIcon,
  fullWidth = false,
  isDark = false,
  disabled,
  style,
  ...props
}, ref) => {
  const isDisabled = disabled || loading;

  const buttonStyle: React.CSSProperties = {
    display: 'inline-flex',
    alignItems: 'center',
    justifyContent: 'center',
    gap: SPACING.sm,
    fontFamily: TYPOGRAPHY.fontFamily,
    fontWeight: TYPOGRAPHY.weightMedium,
    cursor: isDisabled ? 'not-allowed' : 'pointer',
    opacity: isDisabled ? 0.6 : 1,
    transition: TRANSITIONS.normal,
    width: fullWidth ? '100%' : 'auto',
    ...getVariantStyles(variant, isDark),
    ...getSizeStyles(size),
    ...style
  };

  return (
    <button
      ref={ref}
      style={buttonStyle}
      disabled={isDisabled}
      {...props}
    >
      {loading ? (
        <Loader size={size === 'sm' ? 14 : size === 'md' ? 16 : 18} />
      ) : leftIcon}
      {children}
      {!loading && rightIcon}
    </button>
  );
});

Button.displayName = 'Button';

// =============================================================================
// ICON BUTTON
// =============================================================================

export interface IconButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  icon: React.ReactNode;
  variant?: ButtonVariant;
  size?: ButtonSize;
  loading?: boolean;
  isDark?: boolean;
  label?: string; // for accessibility
}

export const IconButton = forwardRef<HTMLButtonElement, IconButtonProps>(({
  icon,
  variant = 'ghost',
  size = 'md',
  loading = false,
  isDark = false,
  label,
  disabled,
  style,
  ...props
}, ref) => {
  const isDisabled = disabled || loading;

  const sizeMap: Record<ButtonSize, number> = {
    sm: 28,
    md: 36,
    lg: 44
  };

  const buttonStyle: React.CSSProperties = {
    display: 'inline-flex',
    alignItems: 'center',
    justifyContent: 'center',
    width: sizeMap[size],
    height: sizeMap[size],
    borderRadius: RADII.md,
    cursor: isDisabled ? 'not-allowed' : 'pointer',
    opacity: isDisabled ? 0.6 : 1,
    transition: TRANSITIONS.normal,
    ...getVariantStyles(variant, isDark),
    padding: 0,
    ...style
  };

  return (
    <button
      ref={ref}
      style={buttonStyle}
      disabled={isDisabled}
      aria-label={label}
      {...props}
    >
      {loading ? <Loader size={size === 'sm' ? 14 : size === 'md' ? 16 : 18} /> : icon}
    </button>
  );
});

IconButton.displayName = 'IconButton';

// =============================================================================
// BUTTON GROUP
// =============================================================================

export interface ButtonGroupProps {
  children: React.ReactNode;
  attached?: boolean;
  spacing?: string;
}

export const ButtonGroup: React.FC<ButtonGroupProps> = ({
  children,
  attached = false,
  spacing = SPACING.sm
}) => {
  const style: React.CSSProperties = {
    display: 'flex',
    alignItems: 'center',
    gap: attached ? 0 : spacing
  };

  if (attached) {
    return (
      <div style={style}>
        {React.Children.map(children, (child, index) => {
          if (!React.isValidElement(child)) return child;

          const isFirst = index === 0;
          const isLast = index === React.Children.count(children) - 1;

          return React.cloneElement(child as React.ReactElement<any>, {
            style: {
              ...((child as React.ReactElement<any>).props.style || {}),
              borderRadius: isFirst
                ? `${RADII.md} 0 0 ${RADII.md}`
                : isLast
                ? `0 ${RADII.md} ${RADII.md} 0`
                : 0,
              marginLeft: isFirst ? 0 : '-1px'
            }
          });
        })}
      </div>
    );
  }

  return <div style={style}>{children}</div>;
};

export default Button;
