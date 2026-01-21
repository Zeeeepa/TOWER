#include "stealth/spoofs/canvas_spoof.h"
#include "util/logger.h"
#include <sstream>
#include <iomanip>

namespace owl {
namespace spoofs {

bool CanvasSpoof::Inject(CefRefPtr<CefFrame> frame, const Config& config) {
    if (!frame) {
        LOG_DEBUG("CanvasSpoof", "Inject: null frame");
        return false;
    }
    
    std::string script = GenerateScript(config);
    frame->ExecuteJavaScript(script, frame->GetURL(), 0);
    LOG_DEBUG("CanvasSpoof", "Injected canvas spoofs into frame");
    return true;
}

std::string CanvasSpoof::GenerateScript(const Config& config) {
    std::stringstream ss;
    
    ss << "(function() {\n";
    ss << "  'use strict';\n";
    ss << "  const _owl = Symbol.for('owl');\n";
    ss << "  const _seedSym = Symbol.for('__owl_canvas_seed__');\n\n";
    
    // Guard check
    ss << "  // Guard: Skip if already patched\n";
    ss << "  if (!window[_owl]?.checkGuard('canvas')) return;\n\n";
    
    // Get createNativeProxy from utilities
    ss << "  const createNativeProxy = window[_owl]?.createNativeProxy;\n";
    ss << "  if (!createNativeProxy) return;\n\n";
    
    // Set canvas seed on window
    ss << "  // Set canvas seed for this context\n";
    ss << "  const __canvasSeed = 0x" << std::hex << config.seed << std::dec << "n;\n";
    ss << "  window[_seedSym] = __canvasSeed;\n\n";
    
    // Store fingerprint hashes
    ss << "  // Store fingerprint hashes\n";
    ss << "  window[_owl].canvas = {\n";
    ss << "    geometry: '" << config.geometry_hash << "',\n";
    ss << "    text: '" << config.text_hash << "'\n";
    ss << "  };\n\n";
    
    // Noise application function
    ss << GenerateNoiseFunction();
    
    // Canvas element hooks
    ss << GenerateCanvasElementHooks();
    
    // Context2D hooks
    ss << GenerateContext2DHooks();
    
    // OffscreenCanvas hooks
    ss << GenerateOffscreenCanvasHooks();
    
    // WebGL canvas handling
    ss << GenerateWebGLCanvasHooks();
    
    ss << "})();\n";
    
    return ss.str();
}

std::string CanvasSpoof::GenerateNoiseFunction() {
    std::stringstream ss;
    
    ss << R"JS(
  // ============================================================
  // NOISE APPLICATION
  // Deterministic noise based on seed for consistent fingerprints
  // ============================================================
  
  // Get seed from canvas's owning window (context-aware for iframes)
  const getSeed = (canvas) => {
    try {
      const win = canvas?.ownerDocument?.defaultView || window;
      return win[_seedSym] || __canvasSeed || 0n;
    } catch (e) {
      return __canvasSeed || 0n;
    }
  };
  
  // Get seed from 2D context's canvas
  const getSeedFromCtx = (ctx) => {
    try {
      return getSeed(ctx.canvas);
    } catch (e) {
      return __canvasSeed || 0n;
    }
  };
  
  // Deterministic noise application
  // Uses a simple xorshift-based PRNG seeded from the BigInt seed
  const applyNoise = (data, width, seed) => {
    if (!seed || seed === 0n) return;
    
    // Convert BigInt seed to number for PRNG
    let state = Number(seed & 0xFFFFFFFFn) || 0x12345678;
    
    // Simple xorshift32 PRNG
    const nextRand = () => {
      state ^= state << 13;
      state ^= state >>> 17;
      state ^= state << 5;
      return (state >>> 0) / 0xFFFFFFFF;
    };
    
    // Skip first few values for better distribution
    for (let i = 0; i < 10; i++) nextRand();
    
    // Apply subtle noise to ALL visible pixels
    // This creates unique but consistent fingerprints
    const len = data.length;
    for (let i = 0; i < len; i += 4) {
      const alpha = data[i + 3];
      if (alpha === 0) continue; // Skip fully transparent
      
      // Very subtle noise: +/- 1 to RGB channels
      const noise = (nextRand() < 0.1) ? (nextRand() < 0.5 ? -1 : 1) : 0;
      if (noise !== 0) {
        data[i] = Math.max(0, Math.min(255, data[i] + noise));     // R
        data[i+1] = Math.max(0, Math.min(255, data[i+1] + noise)); // G
        data[i+2] = Math.max(0, Math.min(255, data[i+2] + noise)); // B
      }
    }
  };
)JS";
    
    return ss.str();
}

std::string CanvasSpoof::GenerateCanvasElementHooks() {
    std::stringstream ss;
    
    ss << R"JS(
  // ============================================================
  // HTMLCanvasElement HOOKS
  // ============================================================
  
  const _origToDataURL = HTMLCanvasElement.prototype.toDataURL;
  const _origToBlob = HTMLCanvasElement.prototype.toBlob;
  
  // Check if canvas has WebGL context
  const isWebGLCanvas = (canvas) => {
    try {
      const ctx = canvas.getContext('webgl') || canvas.getContext('webgl2') ||
                  canvas.getContext('experimental-webgl');
      return ctx !== null;
    } catch (e) {
      return false;
    }
  };
  
  // toDataURL - handles both 2D and WebGL canvases
  const toDataURLProxy = createNativeProxy(_origToDataURL, (target, thisArg, args) => {
    try {
      const seed = getSeed(thisArg);
      if (seed && thisArg.width > 0 && thisArg.height > 0) {
        // Create offscreen canvas for noise application
        const offscreen = document.createElement('canvas');
        offscreen.width = thisArg.width;
        offscreen.height = thisArg.height;
        const ctx = offscreen.getContext('2d');
        
        if (ctx) {
          ctx.drawImage(thisArg, 0, 0);
          const imageData = ctx.getImageData(0, 0, offscreen.width, offscreen.height);
          applyNoise(imageData.data, imageData.width, seed);
          ctx.putImageData(imageData, 0, 0);
          return _origToDataURL.apply(offscreen, args);
        }
      }
    } catch (e) {}
    return _origToDataURL.apply(thisArg, args);
  }, 'toDataURL');
  
  // toBlob - handles both 2D and WebGL canvases
  const toBlobProxy = createNativeProxy(_origToBlob, (target, thisArg, args) => {
    const [callback, ...rest] = args;
    try {
      const seed = getSeed(thisArg);
      if (seed && thisArg.width > 0 && thisArg.height > 0) {
        const offscreen = document.createElement('canvas');
        offscreen.width = thisArg.width;
        offscreen.height = thisArg.height;
        const ctx = offscreen.getContext('2d');
        
        if (ctx) {
          ctx.drawImage(thisArg, 0, 0);
          const imageData = ctx.getImageData(0, 0, offscreen.width, offscreen.height);
          applyNoise(imageData.data, imageData.width, seed);
          ctx.putImageData(imageData, 0, 0);
          return _origToBlob.call(offscreen, callback, ...rest);
        }
      }
    } catch (e) {}
    return _origToBlob.apply(thisArg, args);
  }, 'toBlob');
  
  Object.defineProperty(HTMLCanvasElement.prototype, 'toDataURL', {
    value: toDataURLProxy, writable: true, configurable: true
  });
  Object.defineProperty(HTMLCanvasElement.prototype, 'toBlob', {
    value: toBlobProxy, writable: true, configurable: true
  });
)JS";
    
    return ss.str();
}

std::string CanvasSpoof::GenerateContext2DHooks() {
    std::stringstream ss;
    
    ss << R"JS(
  // ============================================================
  // CanvasRenderingContext2D HOOKS
  // ============================================================
  
  const _origGetImageData = CanvasRenderingContext2D.prototype.getImageData;
  
  // getImageData - apply noise to returned data
  // Note: We apply noise on OUTPUT (getImageData) not input (putImageData)
  // This ensures fingerprints are unique but drawing operations work normally
  const getImageDataProxy = createNativeProxy(_origGetImageData, (target, thisArg, args) => {
    const result = _origGetImageData.apply(thisArg, args);
    try {
      const seed = getSeedFromCtx(thisArg);
      if (seed) {
        applyNoise(result.data, result.width, seed);
      }
    } catch (e) {}
    return result;
  }, 'getImageData');
  
  Object.defineProperty(CanvasRenderingContext2D.prototype, 'getImageData', {
    value: getImageDataProxy, writable: true, configurable: true
  });

  // ============================================================
  // measureText - Fix font bounding box values to integers
  // CEF's font metrics can return fractional values which CreepJS
  // detects as "metric noise" on empty string measurements
  // IMPORTANT: TextMetrics instances have OWN properties that shadow
  // prototype getters, so we must hook measureText and fix the result
  // ============================================================

  const _origMeasureText = CanvasRenderingContext2D.prototype.measureText;
  const propsToRound = [
    'actualBoundingBoxAscent', 'actualBoundingBoxDescent',
    'actualBoundingBoxLeft', 'actualBoundingBoxRight',
    'fontBoundingBoxAscent', 'fontBoundingBoxDescent'
  ];

  const measureTextProxy = createNativeProxy(_origMeasureText, function(target, thisArg, args) {
    const result = _origMeasureText.apply(thisArg, args);

    // Round any fractional bounding box values to avoid "metric noise detected"
    // TextMetrics has getter-only properties, so use Object.defineProperty
    for (let i = 0; i < propsToRound.length; i++) {
      const prop = propsToRound[i];
      if (prop in result) {
        const value = result[prop];
        if (typeof value === 'number' && value % 1 !== 0) {
          Object.defineProperty(result, prop, {
            value: Math.round(value),
            writable: false,
            configurable: true,
            enumerable: true
          });
        }
      }
    }

    return result;
  }, 'measureText');

  Object.defineProperty(CanvasRenderingContext2D.prototype, 'measureText', {
    value: measureTextProxy, writable: true, configurable: true
  });
)JS";

    return ss.str();
}

std::string CanvasSpoof::GenerateOffscreenCanvasHooks() {
    std::stringstream ss;

    ss << R"JS(
  // ============================================================
  // OffscreenCanvas HOOKS
  // Note: OffscreenCanvas doesn't have ownerDocument, uses current window's seed
  // ============================================================

  if (typeof OffscreenCanvas !== 'undefined') {
    const _origConvertToBlob = OffscreenCanvas.prototype.convertToBlob;

    if (_origConvertToBlob) {
      const convertToBlobProxy = createNativeProxy(_origConvertToBlob, (target, thisArg, args) => {
        // Early validation - let native function throw correct error for invalid this
        // CRITICAL: Must check null/undefined FIRST for CreepJS "null conversion" test
        if (thisArg === null || thisArg === undefined) {
          return _origConvertToBlob.call(thisArg);
        }
        // Validate 'this' is actually an OffscreenCanvas
        if (!(thisArg instanceof OffscreenCanvas)) {
          return _origConvertToBlob.call(thisArg); // Let native throw correct error
        }

        try {
          const seed = (typeof window !== 'undefined' ? window[_seedSym] : self[_seedSym]) || __canvasSeed;
          if (seed && thisArg.width > 0 && thisArg.height > 0) {
            const ctx = thisArg.getContext('2d');
            if (ctx) {
              const imageData = ctx.getImageData(0, 0, thisArg.width, thisArg.height);
              applyNoise(imageData.data, imageData.width, seed);
              ctx.putImageData(imageData, 0, 0);
            }
          }
        } catch (e) {}
        return _origConvertToBlob.apply(thisArg, args);
      }, 'convertToBlob');

      Object.defineProperty(OffscreenCanvas.prototype, 'convertToBlob', {
        value: convertToBlobProxy, writable: true, configurable: true
      });
    }
  }

  // OffscreenCanvasRenderingContext2D.getImageData
  if (typeof OffscreenCanvasRenderingContext2D !== 'undefined') {
    const _origOffscreenGetImageData = OffscreenCanvasRenderingContext2D.prototype.getImageData;

    if (_origOffscreenGetImageData) {
      const offscreenGetImageDataProxy = createNativeProxy(_origOffscreenGetImageData, (target, thisArg, args) => {
        // Early validation for null/undefined
        if (thisArg === null || thisArg === undefined) {
          return _origOffscreenGetImageData.call(thisArg);
        }
        const result = _origOffscreenGetImageData.apply(thisArg, args);
        try {
          const seed = (typeof window !== 'undefined' ? window[_seedSym] : self[_seedSym]) || __canvasSeed;
          if (seed) {
            applyNoise(result.data, result.width, seed);
          }
        } catch (e) {}
        return result;
      }, 'getImageData');

      Object.defineProperty(OffscreenCanvasRenderingContext2D.prototype, 'getImageData', {
        value: offscreenGetImageDataProxy, writable: true, configurable: true
      });
    }
  }
)JS";
    
    return ss.str();
}

std::string CanvasSpoof::GenerateWebGLCanvasHooks() {
    std::stringstream ss;
    
    ss << R"JS(
  // ============================================================
  // WebGL CANVAS HANDLING
  // Hook drawImage to apply noise when source is a WebGL canvas
  // ============================================================
  
  const _origDrawImage = CanvasRenderingContext2D.prototype.drawImage;
  
  const drawImageProxy = createNativeProxy(_origDrawImage, (target, thisArg, args) => {
    const [source, ...rest] = args;
    
    // If source is a WebGL canvas, we need special handling
    // The WebGL readPixels hook already applies noise, but if we're
    // drawing to a 2D canvas, we may need to re-apply
    if (source instanceof HTMLCanvasElement && source.width > 0 && source.height > 0) {
      if (isWebGLCanvas(source)) {
        try {
          // Create temp canvas with WebGL content
          const temp = document.createElement('canvas');
          temp.width = source.width;
          temp.height = source.height;
          const tempCtx = temp.getContext('2d');
          if (tempCtx) {
            tempCtx.drawImage(source, 0, 0);
            // Draw from temp instead of WebGL canvas
            return _origDrawImage.call(thisArg, temp, ...rest);
          }
        } catch (e) {}
      }
    }
    return _origDrawImage.apply(thisArg, args);
  }, 'drawImage');
  
  Object.defineProperty(CanvasRenderingContext2D.prototype, 'drawImage', {
    value: drawImageProxy, writable: true, configurable: true
  });
)JS";
    
    return ss.str();
}

} // namespace spoofs
} // namespace owl
