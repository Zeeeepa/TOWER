/**
 * Owl Browser ChatGPT App - OpenAI Hooks
 *
 * React hooks for interacting with the ChatGPT window.openai API.
 *
 * @author Olib AI
 * @license MIT
 */

import { useState, useEffect, useCallback, useRef } from 'react';

// =============================================================================
// TYPE DEFINITIONS
// =============================================================================

export interface OpenAI {
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

export interface OpenAIHookResult {
  openai: OpenAI | null;
  isReady: boolean;
  theme: 'light' | 'dark';
  locale: string;
  displayMode: 'inline' | 'fullscreen' | 'pip';
  toolInput: any;
  toolOutput: any;
}

// =============================================================================
// MAIN HOOK
// =============================================================================

/**
 * Hook to access the ChatGPT OpenAI globals.
 * Reactively updates when theme, locale, or displayMode change.
 */
export function useOpenAI(): OpenAIHookResult {
  const [openai, setOpenai] = useState<OpenAI | null>(null);
  const [isReady, setIsReady] = useState(false);
  const [theme, setTheme] = useState<'light' | 'dark'>('light');
  const [locale, setLocale] = useState('en-US');
  const [displayMode, setDisplayMode] = useState<'inline' | 'fullscreen' | 'pip'>('inline');
  const [toolInput, setToolInput] = useState<any>(null);
  const [toolOutput, setToolOutput] = useState<any>(null);

  useEffect(() => {
    // Check if running in ChatGPT context
    if (typeof window !== 'undefined' && window.openai) {
      const api = window.openai;
      setOpenai(api);
      setIsReady(true);
      setTheme(api.theme || 'light');
      setLocale(api.locale || 'en-US');
      setDisplayMode(api.displayMode || 'inline');
      setToolInput(api.toolInput);
      setToolOutput(api.toolOutput);

      // Set up observer for changes
      const checkForUpdates = () => {
        if (api.theme !== theme) setTheme(api.theme);
        if (api.locale !== locale) setLocale(api.locale);
        if (api.displayMode !== displayMode) setDisplayMode(api.displayMode);
        if (api.toolInput !== toolInput) setToolInput(api.toolInput);
        if (api.toolOutput !== toolOutput) setToolOutput(api.toolOutput);
      };

      // Poll for changes (ChatGPT doesn't provide event listeners)
      const interval = setInterval(checkForUpdates, 100);

      return () => clearInterval(interval);
    } else {
      // Running outside ChatGPT - use defaults
      setIsReady(false);
    }
  }, []);

  return {
    openai,
    isReady,
    theme,
    locale,
    displayMode,
    toolInput,
    toolOutput
  };
}

// =============================================================================
// WIDGET STATE HOOK
// =============================================================================

/**
 * Hook for managing widget state that persists across message interactions.
 * State is automatically synced with window.openai.widgetState.
 */
export function useWidgetState<T extends object>(initialState: T): [T, (updates: Partial<T>) => void] {
  const [state, setStateInternal] = useState<T>(() => {
    // Initialize from widgetState if available
    if (typeof window !== 'undefined' && window.openai?.widgetState) {
      return { ...initialState, ...window.openai.widgetState };
    }
    return initialState;
  });

  const setState = useCallback((updates: Partial<T>) => {
    setStateInternal(current => {
      const newState = { ...current, ...updates };

      // Sync to ChatGPT
      if (typeof window !== 'undefined' && window.openai?.setWidgetState) {
        window.openai.setWidgetState(newState);
      }

      return newState;
    });
  }, []);

  return [state, setState];
}

// =============================================================================
// TOOL CALLER HOOK
// =============================================================================

export interface UseToolResult {
  callTool: (name: string, params?: any) => Promise<any>;
  loading: boolean;
  error: Error | null;
  lastResult: any;
}

/**
 * Hook for calling MCP tools with loading and error state management.
 */
export function useTool(): UseToolResult {
  const { openai } = useOpenAI();
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<Error | null>(null);
  const [lastResult, setLastResult] = useState<any>(null);

  const callTool = useCallback(async (name: string, params: any = {}) => {
    if (!openai?.callTool) {
      // Mock mode for development
      console.log('Mock tool call:', name, params);
      return { success: true, message: `Mock: ${name}` };
    }

    setLoading(true);
    setError(null);

    try {
      const result = await openai.callTool(name, params);
      setLastResult(result);
      return result;
    } catch (err) {
      const error = err instanceof Error ? err : new Error(String(err));
      setError(error);
      throw error;
    } finally {
      setLoading(false);
    }
  }, [openai]);

  return { callTool, loading, error, lastResult };
}

// =============================================================================
// DISPLAY MODE HOOK
// =============================================================================

export interface UseDisplayModeResult {
  displayMode: 'inline' | 'fullscreen' | 'pip';
  requestInline: () => void;
  requestFullscreen: () => void;
  requestPiP: () => void;
  toggle: () => void;
}

/**
 * Hook for managing display mode (inline, fullscreen, PiP).
 */
export function useDisplayMode(): UseDisplayModeResult {
  const { openai, displayMode } = useOpenAI();

  const requestMode = useCallback((mode: 'inline' | 'fullscreen' | 'pip') => {
    if (openai?.requestDisplayMode) {
      openai.requestDisplayMode(mode);
    }
  }, [openai]);

  const toggle = useCallback(() => {
    const nextMode = displayMode === 'inline' ? 'fullscreen' : 'inline';
    requestMode(nextMode);
  }, [displayMode, requestMode]);

  return {
    displayMode,
    requestInline: () => requestMode('inline'),
    requestFullscreen: () => requestMode('fullscreen'),
    requestPiP: () => requestMode('pip'),
    toggle
  };
}

// =============================================================================
// FILE UPLOAD HOOK
// =============================================================================

export interface UseFileUploadResult {
  uploadFile: () => Promise<any>;
  getDownloadUrl: (fileId: string) => Promise<string>;
}

/**
 * Hook for file upload and download operations.
 */
export function useFileUpload(): UseFileUploadResult {
  const { openai } = useOpenAI();

  const uploadFile = useCallback(async () => {
    if (!openai?.uploadFile) {
      throw new Error('File upload not available outside ChatGPT');
    }
    return openai.uploadFile();
  }, [openai]);

  const getDownloadUrl = useCallback(async (fileId: string) => {
    if (!openai?.getFileDownloadUrl) {
      throw new Error('File download not available outside ChatGPT');
    }
    return openai.getFileDownloadUrl(fileId);
  }, [openai]);

  return { uploadFile, getDownloadUrl };
}

// =============================================================================
// FOLLOW-UP MESSAGE HOOK
// =============================================================================

/**
 * Hook for sending follow-up messages in the conversation.
 */
export function useFollowUp() {
  const { openai } = useOpenAI();

  const sendMessage = useCallback((message: string) => {
    if (openai?.sendFollowUpMessage) {
      openai.sendFollowUpMessage(message);
    } else {
      console.log('Follow-up message:', message);
    }
  }, [openai]);

  return { sendMessage };
}

// =============================================================================
// EXTERNAL LINK HOOK
// =============================================================================

/**
 * Hook for opening external links from the widget.
 */
export function useExternalLinks() {
  const { openai } = useOpenAI();

  const openExternal = useCallback((url: string) => {
    if (openai?.openExternal) {
      openai.openExternal(url);
    } else {
      window.open(url, '_blank');
    }
  }, [openai]);

  return { openExternal };
}

export default useOpenAI;
