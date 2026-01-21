import { useState, useCallback, useEffect, useRef } from 'react'
import {
  createContext,
  closeContext,
  listContexts,
  startLiveStream,
  stopLiveStream,
  getPageInfo,
  checkHealth,
  navigate,
} from '../api/browserApi'

const CONTEXT_STORAGE_KEY = 'owl_browser_context_id'

interface BrowserState {
  contextId: string | null
  contexts: string[]
  isLoading: boolean
  error: string | null
  browserReady: boolean
  browserState: string
  pageInfo: {
    url: string
    title: string
    can_go_back: boolean
    can_go_forward: boolean
  } | null
  streamActive: boolean
  sessionExpired: boolean // True when saved context was lost due to server restart
}

// Helper to save context to localStorage
function saveContextToStorage(contextId: string | null) {
  if (contextId) {
    localStorage.setItem(CONTEXT_STORAGE_KEY, contextId)
  } else {
    localStorage.removeItem(CONTEXT_STORAGE_KEY)
  }
}

// Helper to get saved context from localStorage
function getSavedContext(): string | null {
  return localStorage.getItem(CONTEXT_STORAGE_KEY)
}

export function useBrowserContext() {
  const [state, setState] = useState<BrowserState>({
    contextId: null,
    contexts: [],
    isLoading: false,
    error: null,
    browserReady: false,
    browserState: 'unknown',
    pageInfo: null,
    streamActive: false,
    sessionExpired: false,
  })

  const healthCheckInterval = useRef<number | null>(null)
  const pageInfoInterval = useRef<number | null>(null)
  const restoringContext = useRef(false)

  // Check browser health
  const refreshHealth = useCallback(async () => {
    const result = await checkHealth()
    // Health endpoint returns {status, browser_ready, browser_state} directly
    const healthResult = result as unknown as { status?: string; browser_ready?: boolean; browser_state?: string }
    if (healthResult.status === 'healthy' || result.success) {
      setState((prev) => ({
        ...prev,
        browserReady: healthResult.browser_ready || false,
        browserState: healthResult.browser_state || 'unknown',
      }))
    }
  }, [])

  // Refresh page info
  const refreshPageInfo = useCallback(async () => {
    if (!state.contextId) return

    try {
      const result = await getPageInfo(state.contextId)
      if (result.success && result.result) {
        setState((prev) => ({
          ...prev,
          pageInfo: result.result!,
        }))
      }
      // Silently ignore errors - the context might not be fully ready yet
    } catch (error) {
      console.debug('Failed to refresh page info:', error)
    }
  }, [state.contextId])

  // Refresh contexts list and handle if current context was closed
  const refreshContexts = useCallback(async () => {
    const result = await listContexts()
    if (result.success && result.result) {
      const serverContexts = Array.isArray(result.result) ? result.result : []

      // Check if current context is still valid
      if (state.contextId && !serverContexts.includes(state.contextId)) {
        // Current context was closed externally (via tool)
        console.log('[refreshContexts] Current context no longer exists:', state.contextId)

        if (serverContexts.length > 0) {
          // Switch to first available context
          const nextContext = serverContexts[0]
          saveContextToStorage(nextContext)
          setState((prev) => ({
            ...prev,
            contextId: nextContext,
            contexts: serverContexts,
            pageInfo: null,
            streamActive: false,
          }))

          // Start stream on the new context
          const streamResult = await startLiveStream(nextContext, 15, 75)
          if (streamResult.success) {
            setState((prev) => ({ ...prev, streamActive: true }))
          }
        } else {
          // No contexts left
          saveContextToStorage(null)
          setState((prev) => ({
            ...prev,
            contextId: null,
            contexts: [],
            pageInfo: null,
            streamActive: false,
          }))
        }
      } else {
        // Current context still valid, just update list
        setState((prev) => ({
          ...prev,
          contexts: serverContexts,
        }))
      }
    }
  }, [state.contextId])

  // Create a new context
  const create = useCallback(async (options: Record<string, unknown> = {}) => {
    setState((prev) => ({ ...prev, isLoading: true, error: null, sessionExpired: false }))

    const result = await createContext(options)
    if (result.success && result.result) {
      // Result is now a ContextCreationResult object with context_id property
      const contextId = result.result.context_id
      // Keep isLoading true - we're not done yet (still need to navigate and start stream)
      setState((prev) => ({
        ...prev,
        contextId,
        contexts: [...prev.contexts, contextId],
        error: null,
      }))

      // Small delay to ensure browser context is ready
      // The createContext API waits for browser initialization, so this is just a safety buffer
      await new Promise(resolve => setTimeout(resolve, 100))

      // Navigate to homepage
      await navigate(contextId, 'owl://homepage.html/')

      // Start live stream automatically
      const streamResult = await startLiveStream(contextId, 15, 75)
      if (streamResult.success) {
        // NOW we're done - set isLoading to false and streamActive to true
        setState((prev) => ({ ...prev, isLoading: false, streamActive: true }))
        // Save context to localStorage for persistence across page reloads
        saveContextToStorage(contextId)
      } else {
        console.error('Failed to start live stream:', streamResult.error)
        // Still set isLoading to false even on stream error
        setState((prev) => ({ ...prev, isLoading: false }))
      }

      return contextId
    } else {
      setState((prev) => ({
        ...prev,
        isLoading: false,
        error: result.error || 'Failed to create context',
      }))
      return null
    }
  }, [])

  // Close current context
  const close = useCallback(async () => {
    console.log('[close] Called, state.contextId:', state.contextId)
    if (!state.contextId) {
      console.log('[close] No contextId, returning early')
      return
    }

    const contextToClose = state.contextId
    console.log('[close] Closing context:', contextToClose)

    // IMPORTANT: Set streamActive to false FIRST to trigger img unmount
    // This closes the HTTP connection from the client side
    setState((prev) => ({
      ...prev,
      isLoading: true,
      error: null,
      streamActive: false,
    }))

    // Small delay to allow React to unmount the stream img
    await new Promise(resolve => setTimeout(resolve, 100))

    // Stop stream (server will signal streaming thread to stop)
    try {
      await stopLiveStream(contextToClose)
    } catch (err) {
      console.warn('Error stopping stream before close:', err)
    }

    // Small delay for stream signal (reduced since stop is non-blocking)
    await new Promise(resolve => setTimeout(resolve, 50))

    console.log('[close] Calling closeContext API for:', contextToClose)
    const result = await closeContext(contextToClose)
    console.log('[close] closeContext result:', result)
    if (result.success) {
      // Get remaining contexts
      const remainingContexts = state.contexts.filter((c) => c !== contextToClose)

      if (remainingContexts.length > 0) {
        // Switch to another context
        const nextContext = remainingContexts[0]
        saveContextToStorage(nextContext)
        setState((prev) => ({
          ...prev,
          contextId: nextContext,
          contexts: remainingContexts,
          isLoading: false,
          pageInfo: null,
          streamActive: false,
        }))

        // Start stream on the new context
        const streamResult = await startLiveStream(nextContext, 15, 75)
        if (streamResult.success) {
          setState((prev) => ({ ...prev, streamActive: true }))
        }
      } else {
        // No remaining contexts
        saveContextToStorage(null)
        setState((prev) => ({
          ...prev,
          contextId: null,
          contexts: [],
          isLoading: false,
          pageInfo: null,
          streamActive: false,
        }))
      }
    } else {
      setState((prev) => ({
        ...prev,
        isLoading: false,
        error: result.error || 'Failed to close context',
      }))
    }
  }, [state.contextId])

  // Dismiss session expired notification
  const dismissSessionExpired = useCallback(() => {
    setState((prev) => ({ ...prev, sessionExpired: false }))
  }, [])

  // Switch to a different context
  const switchContext = useCallback(async (contextId: string) => {
    if (state.contextId === contextId) return

    const oldContextId = state.contextId

    // IMPORTANT: Set streamActive to false FIRST to trigger img unmount
    // This closes the HTTP connection from the client side
    setState((prev) => ({
      ...prev,
      streamActive: false,
    }))

    // Small delay to allow React to unmount the stream img
    await new Promise(resolve => setTimeout(resolve, 100))

    // Stop stream on current context (server will signal streaming thread to stop)
    if (oldContextId) {
      try {
        await stopLiveStream(oldContextId)
      } catch (err) {
        console.warn('Error stopping old stream:', err)
      }
    }

    // Update state with new context
    setState((prev) => ({
      ...prev,
      contextId,
      // Add context to list if not already present (e.g., created via ToolForm)
      contexts: prev.contexts.includes(contextId) ? prev.contexts : [...prev.contexts, contextId],
      pageInfo: null,
      streamActive: false,
    }))

    // Small delay to allow state to settle (reduced since stop is now non-blocking)
    await new Promise(resolve => setTimeout(resolve, 100))

    // Start stream on new context
    const streamResult = await startLiveStream(contextId, 15, 75)
    if (streamResult.success) {
      setState((prev) => ({ ...prev, streamActive: true }))
      // Save context to localStorage
      saveContextToStorage(contextId)
    } else {
      console.error('Failed to start live stream:', streamResult.error)
    }
  }, [state.contextId])

  // Set up health check interval
  useEffect(() => {
    refreshHealth()
    healthCheckInterval.current = window.setInterval(refreshHealth, 5000)

    return () => {
      if (healthCheckInterval.current) {
        clearInterval(healthCheckInterval.current)
      }
    }
  }, [refreshHealth])

  // Set up page info polling when context is active
  useEffect(() => {
    if (state.contextId) {
      refreshPageInfo()
      pageInfoInterval.current = window.setInterval(refreshPageInfo, 2000)
    }

    return () => {
      if (pageInfoInterval.current) {
        clearInterval(pageInfoInterval.current)
      }
    }
  }, [state.contextId, refreshPageInfo])

  // Load initial contexts and restore saved context
  useEffect(() => {
    const initializeContexts = async () => {
      // Prevent multiple restore attempts
      if (restoringContext.current) return

      // First, fetch the current contexts from the server
      const result = await listContexts()
      const serverContexts = result.success && result.result && Array.isArray(result.result)
        ? result.result
        : []

      setState((prev) => ({
        ...prev,
        contexts: serverContexts,
      }))

      // Check if we have a saved context to restore
      const savedContextId = getSavedContext()
      if (savedContextId && serverContexts.includes(savedContextId)) {
        // Context still exists on server - restore it
        restoringContext.current = true
        console.log('Restoring saved context:', savedContextId)

        setState((prev) => ({
          ...prev,
          contextId: savedContextId,
          isLoading: true,
        }))

        // Small delay to allow state to settle
        await new Promise(resolve => setTimeout(resolve, 100))

        // Start the stream for the saved context
        const streamResult = await startLiveStream(savedContextId, 15, 75)
        if (streamResult.success) {
          setState((prev) => ({
            ...prev,
            isLoading: false,
            streamActive: true
          }))
          console.log('Successfully restored context stream')
        } else {
          console.error('Failed to restore stream for saved context:', streamResult.error)
          // Clear the saved context if we can't restore the stream
          saveContextToStorage(null)
          setState((prev) => ({
            ...prev,
            contextId: null,
            isLoading: false,
            streamActive: false
          }))
        }

        restoringContext.current = false
      } else if (savedContextId) {
        // Saved context no longer exists on server (likely server was restarted)
        console.log('Saved context no longer exists, clearing:', savedContextId)
        saveContextToStorage(null)
        setState((prev) => ({
          ...prev,
          sessionExpired: true,
        }))
      }
    }

    initializeContexts()
  }, []) // Only run on mount

  return {
    ...state,
    create,
    close,
    switchContext,
    refreshHealth,
    refreshPageInfo,
    refreshContexts,
    dismissSessionExpired,
  }
}
