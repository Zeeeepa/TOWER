/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        primary: {
          DEFAULT: '#4E9179',
          light: '#5CA88A',
          dark: '#3D7862',
        },
        bg: {
          primary: '#0A1628',
          secondary: '#1A2A44',
          tertiary: '#243448',
        },
        text: {
          primary: '#E8E8E8',
          secondary: '#A0A0A0',
          muted: '#6B7280',
        },
      },
      fontFamily: {
        sans: ['Inter', 'system-ui', 'sans-serif'],
        mono: ['JetBrains Mono', 'Fira Code', 'monospace'],
      },
      backdropBlur: {
        xs: '2px',
      },
    },
  },
  plugins: [],
}
