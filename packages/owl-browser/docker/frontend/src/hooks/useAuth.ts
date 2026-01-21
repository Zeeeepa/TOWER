import { useState, useEffect, useCallback } from 'react'
import { login as apiLogin, verifyToken, hasToken, setToken, clearToken } from '../api/browserApi'

interface AuthState {
  isAuthenticated: boolean
  isLoading: boolean
  error: string | null
}

export function useAuth() {
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

  return {
    ...state,
    login,
    logout,
    checkAuth,
  }
}
