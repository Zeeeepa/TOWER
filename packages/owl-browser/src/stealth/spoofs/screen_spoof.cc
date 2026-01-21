#include "stealth/spoofs/screen_spoof.h"
#include "util/logger.h"
#include <sstream>

namespace owl {
namespace spoofs {

ScreenSpoof::Config ScreenSpoof::Config::FromVM(const VirtualMachine& vm) {
    Config config;
    config.width = vm.screen.width;
    config.height = vm.screen.height;
    config.avail_width = vm.screen.avail_width;
    config.avail_height = vm.screen.avail_height;
    config.color_depth = vm.screen.color_depth;
    config.pixel_depth = vm.screen.pixel_depth;
    config.device_pixel_ratio = vm.screen.device_pixel_ratio;
    config.orientation_type = vm.screen.orientation_type;
    config.orientation_angle = vm.screen.orientation_angle;
    return config;
}

bool ScreenSpoof::Inject(CefRefPtr<CefFrame> frame, const Config& config) {
    if (!frame) {
        LOG_DEBUG("ScreenSpoof", "Inject: null frame");
        return false;
    }
    
    std::string script = GenerateScript(config);
    frame->ExecuteJavaScript(script, frame->GetURL(), 0);
    LOG_DEBUG("ScreenSpoof", "Injected screen spoofs into frame");
    return true;
}

std::string ScreenSpoof::GenerateScript(const Config& config) {
    std::stringstream ss;
    
    ss << "(function() {\n";
    ss << "  'use strict';\n";
    ss << "  const _owl = Symbol.for('owl');\n\n";
    
    // Guard check
    ss << "  // Guard: Skip if already patched\n";
    ss << "  if (!window[_owl]?.checkGuard('screen')) return;\n\n";
    
    // Get createNativeProxy from utilities
    ss << "  const createNativeProxy = window[_owl]?.createNativeProxy;\n";
    ss << "  if (!createNativeProxy) return;\n\n";
    
    // Configuration
    ss << "  // Screen configuration\n";
    ss << "  const __screenConfig = {\n";
    ss << "    width: " << config.width << ",\n";
    ss << "    height: " << config.height << ",\n";
    ss << "    availWidth: " << config.avail_width << ",\n";
    ss << "    availHeight: " << config.avail_height << ",\n";
    ss << "    colorDepth: " << config.color_depth << ",\n";
    ss << "    pixelDepth: " << config.pixel_depth << ",\n";
    ss << "    devicePixelRatio: " << config.device_pixel_ratio << ",\n";
    ss << "    orientationType: \"" << EscapeJS(config.orientation_type) << "\",\n";
    ss << "    orientationAngle: " << config.orientation_angle << ",\n";
    ss << "    chromeWidth: " << config.chrome_width << ",\n";
    ss << "    chromeHeight: " << config.chrome_height << "\n";
    ss << "  };\n\n";
    
    ss << GenerateScreenPropsHooks(config);
    ss << GenerateWindowDimensionHooks(config);
    ss << GenerateOrientationHooks(config);
    
    ss << "})();\n";
    
    return ss.str();
}

std::string ScreenSpoof::GenerateScreenPropsHooks(const Config& config) {
    std::stringstream ss;

    ss << R"JS(
  // ============================================================
  // SCREEN OBJECT PROPERTIES
  // Note: screen.width/height return CSS pixels, not device pixels!
  // ============================================================

  const screenProto = Object.getPrototypeOf(screen);

  // CRITICAL: Use native getter to detect real Screen vs Object.create(Screen.prototype)
  // Native getters like 'orientation' have internal slot checking that throws "Illegal invocation"
  // on fake objects. Object.prototype.toString doesn't work because Symbol.toStringTag
  // makes both real and fake return "[object Screen]"
  const _origOrientationGetter = Object.getOwnPropertyDescriptor(Screen.prototype, 'orientation')?.get;
  const isRealScreen = (obj) => {
    if (!_origOrientationGetter) return true; // Fallback if orientation doesn't exist
    try {
      // orientation getter throws "Illegal invocation" on fake objects (Object.create)
      _origOrientationGetter.call(obj);
      return true;
    } catch (e) {
      // "Illegal invocation" means fake object
      return !e.message.includes('Illegal invocation');
    }
  };

  // CRITICAL: Prototype-level guard to prevent double-patching across iframes
  // Screen.prototype is shared across all same-origin windows, so we must
  // use a marker ON THE PROTOTYPE ITSELF, not per-window guards.
  const _screenPatched = Symbol.for('owl_screen_patched');
  if (screenProto[_screenPatched]) {
    // Already patched by another window/iframe - skip to avoid proxy-of-proxy
    return;
  }

  // Mark as patched BEFORE we start (prevents race conditions)
  try {
    Object.defineProperty(screenProto, _screenPatched, {
      value: true,
      writable: false,
      enumerable: false,
      configurable: false
    });
  } catch (e) {
    // If we can't set the marker, another context might have just set it
    if (screenProto[_screenPatched]) return;
  }

  // Calculate CSS pixel dimensions
  const dpr = __screenConfig.devicePixelRatio || 1;
  const cssWidth = Math.round(__screenConfig.width / dpr);
  const cssHeight = Math.round(__screenConfig.height / dpr);
  const cssAvailWidth = Math.round(__screenConfig.availWidth / dpr);
  const cssAvailHeight = Math.round(__screenConfig.availHeight / dpr);

  // Properties to spoof
  const screenProps = {
    width: cssWidth,
    height: cssHeight,
    availWidth: cssAvailWidth,
    availHeight: cssAvailHeight,
    colorDepth: __screenConfig.colorDepth,
    pixelDepth: __screenConfig.pixelDepth
  };

  for (const [prop, value] of Object.entries(screenProps)) {
    try {
      const origDesc = Object.getOwnPropertyDescriptor(screenProto, prop);
      if (origDesc && origDesc.get) {
        const proxyGetter = createNativeProxy(origDesc.get, (target, thisArg, args) => {
          // CRITICAL: Must throw TypeError when accessed on prototype (not instance)
          // This is how native getters behave and what CreepJS tests for
          if (!isRealScreen(thisArg)) {
            throw new TypeError("Illegal invocation");
          }
          return value;
        }, `get ${prop}`);

        Object.defineProperty(screenProto, prop, {
          get: proxyGetter,
          configurable: true,
          enumerable: true
        });
      }
    } catch (e) {}
  }

  // availTop and availLeft (usually 0)
  try {
    const availTopDesc = Object.getOwnPropertyDescriptor(screenProto, 'availTop');
    if (availTopDesc && availTopDesc.get) {
      const proxyAvailTop = createNativeProxy(availTopDesc.get, (target, thisArg, args) => {
        if (!isRealScreen(thisArg)) {
          throw new TypeError("Illegal invocation");
        }
        return 0;
      }, 'get availTop');
      Object.defineProperty(screenProto, 'availTop', {
        get: proxyAvailTop, configurable: true, enumerable: true
      });
    }
  } catch (e) {}

  try {
    const availLeftDesc = Object.getOwnPropertyDescriptor(screenProto, 'availLeft');
    if (availLeftDesc && availLeftDesc.get) {
      const proxyAvailLeft = createNativeProxy(availLeftDesc.get, (target, thisArg, args) => {
        if (!isRealScreen(thisArg)) {
          throw new TypeError("Illegal invocation");
        }
        return 0;
      }, 'get availLeft');
      Object.defineProperty(screenProto, 'availLeft', {
        get: proxyAvailLeft, configurable: true, enumerable: true
      });
    }
  } catch (e) {}
)JS";
    
    return ss.str();
}

std::string ScreenSpoof::GenerateWindowDimensionHooks(const Config& config) {
    std::stringstream ss;
    
    ss << R"JS(
  // ============================================================
  // WINDOW DIMENSION HOOKS
  // outerWidth/Height must be larger than innerWidth/Height
  // ============================================================
  
  // devicePixelRatio
  try {
    const origDprDesc = Object.getOwnPropertyDescriptor(window, 'devicePixelRatio');
    if (origDprDesc && origDprDesc.get) {
      const proxyDpr = createNativeProxy(origDprDesc.get, (target, thisArg, args) => {
        return __screenConfig.devicePixelRatio;
      }, 'get devicePixelRatio');
      
      Object.defineProperty(window, 'devicePixelRatio', {
        get: proxyDpr,
        configurable: true,
        enumerable: true
      });
    } else {
      Object.defineProperty(window, 'devicePixelRatio', {
        get: function() { return __screenConfig.devicePixelRatio; },
        configurable: true,
        enumerable: true
      });
    }
  } catch (e) {}
  
  // outerWidth = innerWidth + chrome
  // outerHeight = innerHeight + chrome
  const _origInnerWidth = Object.getOwnPropertyDescriptor(window, 'innerWidth')?.get;
  const _origInnerHeight = Object.getOwnPropertyDescriptor(window, 'innerHeight')?.get;
  
  if (_origInnerWidth) {
    try {
      const outerWidthDesc = Object.getOwnPropertyDescriptor(window, 'outerWidth');
      if (outerWidthDesc && outerWidthDesc.get) {
        const proxyOuterWidth = createNativeProxy(outerWidthDesc.get, (target, thisArg, args) => {
          return _origInnerWidth.call(window) + __screenConfig.chromeWidth;
        }, 'get outerWidth');
        
        Object.defineProperty(window, 'outerWidth', {
          get: proxyOuterWidth, configurable: true, enumerable: true
        });
      }
    } catch (e) {}
  }
  
  if (_origInnerHeight) {
    try {
      const outerHeightDesc = Object.getOwnPropertyDescriptor(window, 'outerHeight');
      if (outerHeightDesc && outerHeightDesc.get) {
        const proxyOuterHeight = createNativeProxy(outerHeightDesc.get, (target, thisArg, args) => {
          return _origInnerHeight.call(window) + __screenConfig.chromeHeight;
        }, 'get outerHeight');
        
        Object.defineProperty(window, 'outerHeight', {
          get: proxyOuterHeight, configurable: true, enumerable: true
        });
      }
    } catch (e) {}
  }
)JS";
    
    return ss.str();
}

std::string ScreenSpoof::GenerateOrientationHooks(const Config& config) {
    std::stringstream ss;
    
    ss << R"JS(
  // ============================================================
  // SCREEN ORIENTATION HOOKS
  // ============================================================
  
  try {
    if (screen.orientation) {
      const origTypeDesc = Object.getOwnPropertyDescriptor(ScreenOrientation.prototype, 'type');
      const origAngleDesc = Object.getOwnPropertyDescriptor(ScreenOrientation.prototype, 'angle');
      
      if (origTypeDesc && origTypeDesc.get) {
        const proxyType = createNativeProxy(origTypeDesc.get, (target, thisArg, args) => {
          return __screenConfig.orientationType;
        }, 'get type');
        Object.defineProperty(ScreenOrientation.prototype, 'type', {
          get: proxyType, configurable: true, enumerable: true
        });
      }
      
      if (origAngleDesc && origAngleDesc.get) {
        const proxyAngle = createNativeProxy(origAngleDesc.get, (target, thisArg, args) => {
          return __screenConfig.orientationAngle;
        }, 'get angle');
        Object.defineProperty(ScreenOrientation.prototype, 'angle', {
          get: proxyAngle, configurable: true, enumerable: true
        });
      }
    }
  } catch (e) {}
)JS";
    
    return ss.str();
}

std::string ScreenSpoof::EscapeJS(const std::string& str) {
    std::stringstream ss;
    for (char c : str) {
        switch (c) {
            case '\\': ss << "\\\\"; break;
            case '"': ss << "\\\""; break;
            case '\'': ss << "\\'"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default: ss << c; break;
        }
    }
    return ss.str();
}

} // namespace spoofs
} // namespace owl
