import { createContext, useContext, useState, useEffect, useCallback, ReactNode } from 'react'
import { login as apiLogin, verifyToken, hasToken, setToken, clearToken } from '../api/browserApi'

interface AuthState {
  isAuthenticated: boolean
  isLoading: boolean
  error: string | null
}

interface AuthContextType extends AuthState {
  login: (password: string) => Promise<boolean>
  logout: () => void
  checkAuth: () => Promise<void>
}

const AuthContext = createContext<AuthContextType | null>(null)

export function AuthProvider({ children }: { children: ReactNode }) {
  const [state, setState] = useState<AuthState>({
    isAuthenticated: false,
    isLoading: true,
    error: null,
  })

  const checkAuth = useCallback(async () => {
    if (!hasToken()) {
      setState({ isAuthenticated: false, isLoading: false, error: null })
      return
    }

    const result = await verifyToken()
    // Auth verify returns {"valid": true} directly, not wrapped in result
    const valid = (result as { valid?: boolean }).valid || result.result?.valid
    if (valid) {
      setState({ isAuthenticated: true, isLoading: false, error: null })
    } else {
      clearToken()
      setState({ isAuthenticated: false, isLoading: false, error: null })
    }
  }, [])

  useEffect(() => {
    checkAuth()
  }, [checkAuth])

  const login = async (password: string): Promise<boolean> => {
    setState((prev) => ({ ...prev, isLoading: true, error: null }))

    const result = await apiLogin(password)
    // Token can be in result.token (auth endpoint) or result.result.token (standard format)
    const token = (result as { token?: string }).token || result.result?.token
    if (result.success && token) {
      setToken(token)
      setState({ isAuthenticated: true, isLoading: false, error: null })
      return true
    } else {
      setState({
        isAuthenticated: false,
        isLoading: false,
        error: result.error || 'Invalid password',
      })
      return false
    }
  }

  const logout = () => {
    clearToken()
    setState({ isAuthenticated: false, isLoading: false, error: null })
  }

  return (
    <AuthContext.Provider
      value={{
        ...state,
        login,
        logout,
        checkAuth,
      }}
    >
      {children}
    </AuthContext.Provider>
  )
}

export function useAuth() {
  const context = useContext(AuthContext)
  if (!context) {
    throw new Error('useAuth must be used within an AuthProvider')
  }
  return context
}
