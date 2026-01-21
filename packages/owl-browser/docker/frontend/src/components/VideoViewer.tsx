import { useRef, useState, useEffect, useCallback } from 'react'
import { Play, RefreshCw, AlertCircle, Crosshair, X, Navigation } from 'lucide-react'
import { getVideoStreamUrl, getElementAtPosition } from '../api/browserApi'
import placeholderVideo from '../assets/Olib-owl.mp4'

interface VideoViewerProps {
  contextId: string | null
  streamActive: boolean
  isCreatingContext: boolean
  pageInfo: {
    url: string
    title: string
    can_go_back: boolean
    can_go_forward: boolean
  } | null
  pickerMode: 'element' | 'position' | null
  onPickerComplete: (value: string) => void
  onPickerCancel: () => void
}

// Browser viewport size (default)
const VIEWPORT_WIDTH = 1920
const VIEWPORT_HEIGHT = 1080

export default function VideoViewer({
  contextId,
  streamActive,
  isCreatingContext,
  pageInfo,
  pickerMode,
  onPickerComplete,
  onPickerCancel,
}: VideoViewerProps) {
  const containerRef = useRef<HTMLDivElement>(null)
  const imgRef = useRef<HTMLImageElement>(null)
  const videoRef = useRef<HTMLVideoElement>(null)
  const [streamError, setStreamError] = useState(false)
  const [mousePos, setMousePos] = useState({ x: 0, y: 0 })
  const [browserPos, setBrowserPos] = useState({ x: 0, y: 0 })
  const [pickerLoading, setPickerLoading] = useState(false)
  const [elementInfo, setElementInfo] = useState<{
    selector: string
    tagName: string
    id: string
    className: string
    textContent: string
  } | null>(null)

  // Calculate browser coordinates from mouse position
  const calculateBrowserPosition = useCallback((clientX: number, clientY: number) => {
    if (!imgRef.current) return { x: 0, y: 0 }

    const rect = imgRef.current.getBoundingClientRect()
    const relX = clientX - rect.left
    const relY = clientY - rect.top

    const scaleX = VIEWPORT_WIDTH / rect.width
    const scaleY = VIEWPORT_HEIGHT / rect.height

    return {
      x: Math.round(relX * scaleX),
      y: Math.round(relY * scaleY),
    }
  }, [])

  // Handle mouse move over picker overlay
  const handleMouseMove = useCallback(
    (e: React.MouseEvent) => {
      if (!imgRef.current) return

      const rect = imgRef.current.getBoundingClientRect()
      setMousePos({
        x: e.clientX - rect.left,
        y: e.clientY - rect.top,
      })

      const pos = calculateBrowserPosition(e.clientX, e.clientY)
      setBrowserPos(pos)
    },
    [calculateBrowserPosition]
  )

  // Handle click on picker overlay
  const handlePickerClick = useCallback(
    async (e: React.MouseEvent) => {
      if (!contextId || !pickerMode) return

      const pos = calculateBrowserPosition(e.clientX, e.clientY)

      if (pickerMode === 'position') {
        onPickerComplete(`${pos.x}x${pos.y}`)
        return
      }

      // Element picker - fetch element info
      setPickerLoading(true)
      try {
        const result = await getElementAtPosition(contextId, pos.x, pos.y)
        if (result.success && result.result) {
          setElementInfo(result.result)
        } else {
          setElementInfo(null)
          onPickerComplete(`${pos.x}x${pos.y}`) // Fallback to position
        }
      } catch {
        onPickerComplete(`${pos.x}x${pos.y}`)
      } finally {
        setPickerLoading(false)
      }
    },
    [contextId, pickerMode, calculateBrowserPosition, onPickerComplete]
  )

  // Confirm element selection
  const confirmElement = useCallback((e: React.MouseEvent) => {
    e.stopPropagation() // Prevent click from bubbling to overlay
    if (elementInfo?.selector) {
      onPickerComplete(elementInfo.selector)
    }
    setElementInfo(null)
  }, [elementInfo, onPickerComplete])

  // Cancel and use position instead
  const usePosition = useCallback((e: React.MouseEvent) => {
    e.stopPropagation() // Prevent click from bubbling to overlay
    onPickerComplete(`${browserPos.x}x${browserPos.y}`)
    setElementInfo(null)
  }, [browserPos, onPickerComplete])

  // Cancel picker
  const cancelPicker = useCallback((e: React.MouseEvent) => {
    e.stopPropagation() // Prevent click from bubbling to overlay
    setElementInfo(null)
    onPickerCancel()
  }, [onPickerCancel])

  // Reset stream error on context change
  useEffect(() => {
    setStreamError(false)
  }, [contextId])

  const handleStreamError = () => {
    setStreamError(true)
  }

  const handleStreamLoad = () => {
    setStreamError(false)
  }

  const retryStream = () => {
    setStreamError(false)
  }

  // Show loading state when creating context - CHECK THIS FIRST!
  if (isCreatingContext) {
    return (
      <div className="h-full flex flex-col rounded-lg overflow-hidden bg-white shadow-lg">
        <div className="flex-1 relative flex items-center justify-center bg-gray-100">
          <div className="text-center p-6">
            <div className="w-16 h-16 border-4 border-primary/30 border-t-primary rounded-full animate-spin mx-auto mb-4" />
            <h3 className="text-xl font-semibold text-gray-800 mb-2">Creating Browser Context</h3>
            <p className="text-gray-600">
              Starting browser instance...
            </p>
          </div>
        </div>
      </div>
    )
  }

  // Show placeholder when no context
  if (!contextId || !streamActive) {
    return (
      <div className="h-full flex flex-col rounded-lg overflow-hidden bg-white shadow-lg">
        <div className="flex-1 relative flex items-center justify-center">
          <video
            ref={videoRef}
            src={placeholderVideo}
            autoPlay
            loop
            muted
            playsInline
            className="max-w-full max-h-full object-contain"
          />
          <div className="absolute inset-0 flex items-center justify-center bg-black/40">
            <div className="text-center p-6">
              <Play className="w-16 h-16 text-primary mx-auto mb-4" />
              <h3 className="text-xl font-semibold text-white mb-2">No Active Context</h3>
              <p className="text-white/80">
                Create a browser context to start streaming
              </p>
            </div>
          </div>
        </div>
      </div>
    )
  }

  // Show error state
  if (streamError) {
    return (
      <div className="h-full flex flex-col rounded-lg overflow-hidden bg-white shadow-lg items-center justify-center">
        <div className="text-center p-6">
          <AlertCircle className="w-16 h-16 text-red-400 mx-auto mb-4" />
          <h3 className="text-xl font-semibold text-gray-800 mb-2">Stream Error</h3>
          <p className="text-gray-600 mb-4">Failed to connect to video stream</p>
          <button onClick={retryStream} className="btn-primary flex items-center gap-2 mx-auto">
            <RefreshCw className="w-4 h-4" />
            Retry
          </button>
        </div>
      </div>
    )
  }

  return (
    <div ref={containerRef} className="h-full flex flex-col rounded-lg overflow-hidden bg-white shadow-lg">
      {/* Stream container */}
      <div className="flex-1 relative flex items-center justify-center min-h-0">
        <div className="relative w-full h-full flex items-center justify-center">
          {/* Key prop forces React to recreate the img element when context changes */}
          {/* This ensures the old HTTP connection is closed before the new one opens */}
          <img
            key={`stream-${contextId}`}
            ref={imgRef}
            src={getVideoStreamUrl(contextId, 15)}
            alt="Browser Stream"
            className="max-w-full max-h-full object-contain"
            onError={handleStreamError}
            onLoad={handleStreamLoad}
          />

          {/* Blank page overlay - show when no URL loaded yet */}
          {pageInfo && (!pageInfo.url || pageInfo.url === 'about:blank') && (
            <div className="absolute inset-0 flex items-center justify-center bg-gray-900/80 backdrop-blur-sm">
              <div className="text-center p-8 max-w-md">
                <Navigation className="w-16 h-16 text-primary mx-auto mb-4" />
                <h3 className="text-xl font-semibold text-white mb-3">Context Ready</h3>
                <p className="text-gray-300 mb-4">
                  Your browser context is ready. Use the <span className="text-primary font-medium">browser_navigate</span> tool
                  from the left panel to visit any website.
                </p>
                <div className="text-sm text-gray-400 bg-gray-800/50 rounded-lg p-3">
                  <code className="text-primary">browser_navigate</code>
                  <span className="mx-2">→</span>
                  <span>Enter URL to navigate</span>
                </div>
              </div>
            </div>
          )}

          {/* Picker Overlay */}
          {pickerMode && imgRef.current && (
            <div
              className="picker-overlay"
              style={{
                position: 'absolute',
                top: imgRef.current.offsetTop,
                left: imgRef.current.offsetLeft,
                width: imgRef.current.clientWidth,
                height: imgRef.current.clientHeight,
              }}
              onMouseMove={handleMouseMove}
              onClick={handlePickerClick}
            >
              {/* Crosshairs */}
              <div className="picker-crosshair-h" style={{ top: mousePos.y }} />
              <div className="picker-crosshair-v" style={{ left: mousePos.x }} />

              {/* Coordinates tooltip */}
              <div
                className="picker-coords"
                style={{
                  top: mousePos.y + 15,
                  left: mousePos.x + 15,
                }}
              >
                {browserPos.x} x {browserPos.y}
              </div>

              {/* Loading indicator */}
              {pickerLoading && (
                <div className="absolute inset-0 flex items-center justify-center bg-bg-primary/50">
                  <div className="w-8 h-8 border-4 border-primary/30 border-t-primary rounded-full animate-spin" />
                </div>
              )}

              {/* Element info popup */}
              {elementInfo && (
                <div
                  className="absolute inset-0 flex items-center justify-center bg-bg-primary/70"
                  onClick={(e) => e.stopPropagation()}
                >
                  <div className="glass-card p-6 max-w-md w-full mx-4">
                    <h4 className="text-lg font-semibold text-text-primary mb-4 flex items-center gap-2">
                      <Crosshair className="w-5 h-5 text-primary" />
                      Element Found
                    </h4>

                    <div className="space-y-3 mb-6">
                      <div>
                        <span className="text-xs text-text-muted">Selector</span>
                        <code className="block mt-1 p-2 bg-bg-primary rounded text-sm text-primary font-mono break-all">
                          {elementInfo.selector}
                        </code>
                      </div>

                      <div className="grid grid-cols-2 gap-3">
                        <div>
                          <span className="text-xs text-text-muted">Tag</span>
                          <p className="text-sm text-text-primary">{elementInfo.tagName}</p>
                        </div>
                        {elementInfo.id && (
                          <div>
                            <span className="text-xs text-text-muted">ID</span>
                            <p className="text-sm text-text-primary">#{elementInfo.id}</p>
                          </div>
                        )}
                      </div>

                      {elementInfo.className && (
                        <div>
                          <span className="text-xs text-text-muted">Classes</span>
                          <p className="text-sm text-text-primary truncate">{elementInfo.className}</p>
                        </div>
                      )}

                      {elementInfo.textContent && (
                        <div>
                          <span className="text-xs text-text-muted">Text</span>
                          <p className="text-sm text-text-secondary truncate">{elementInfo.textContent}</p>
                        </div>
                      )}
                    </div>

                    <div className="flex gap-2">
                      <button onClick={confirmElement} className="btn-primary flex-1">
                        Use Selector
                      </button>
                      <button onClick={usePosition} className="btn-secondary">
                        Use Position
                      </button>
                      <button onClick={cancelPicker} className="btn-icon">
                        <X className="w-4 h-4" />
                      </button>
                    </div>
                  </div>
                </div>
              )}
            </div>
          )}
        </div>
      </div>

      {/* Picker mode indicator */}
      {pickerMode && !elementInfo && (
        <div className="flex-shrink-0 px-4 py-2 bg-primary/20 border-t border-primary/30 flex items-center justify-between">
          <div className="flex items-center gap-2 text-primary">
            <Crosshair className="w-4 h-4" />
            <span className="text-sm font-medium">
              {pickerMode === 'element' ? 'Click to select element' : 'Click to select position'}
            </span>
          </div>
          <button onClick={cancelPicker} className="text-sm text-gray-600 hover:text-gray-800">
            Cancel (Esc)
          </button>
        </div>
      )}
    </div>
  )
}
