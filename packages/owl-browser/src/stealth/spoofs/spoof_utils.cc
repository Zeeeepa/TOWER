#include "stealth/spoofs/spoof_utils.h"
#include "util/logger.h"
#include <sstream>

namespace owl {
namespace spoofs {

bool SpoofUtils::InjectUtilities(CefRefPtr<CefFrame> frame) {
    if (!frame) {
        LOG_DEBUG("SpoofUtils", "InjectUtilities: null frame");
        return false;
    }
    
    std::string script = GenerateUtilitiesScript();
    frame->ExecuteJavaScript(script, frame->GetURL(), 0);
    LOG_DEBUG("SpoofUtils", "Injected spoof utilities into frame");
    return true;
}

bool SpoofUtils::IsInjected(CefRefPtr<CefFrame> frame) {
    // Note: This is a synchronous check hint, actual injection guards are in JS
    return false; // Always allow re-check, JS guards handle duplicates
}

std::string SpoofUtils::GenerateUtilitiesScript() {
    std::stringstream ss;
    ss << "(function() {\n";
    ss << "  'use strict';\n\n";
    
    // Symbol key for our namespace
    ss << "  const _owl = Symbol.for('owl');\n\n";
    
    // Guard: Skip if already initialized
    ss << "  // Guard: Skip if already fully initialized\n";
    // Use 'self' which works in both window and worker contexts
    ss << "  const _global = typeof self !== 'undefined' ? self : window;\n";
    ss << "  if (_global[_owl]?.utilsInitialized) return;\n\n";

    // Initialize namespace
    ss << "  // Initialize namespace object\n";
    ss << "  if (!_global[_owl]) {\n";
    ss << "    Object.defineProperty(_global, _owl, {\n";
    ss << "      value: { font: {}, webgl: {}, camera: {}, _cycleProxies: new WeakSet() },\n";
    ss << "      writable: true, configurable: true, enumerable: false\n";
    ss << "    });\n";
    ss << "  }\n";
    ss << "  // Ensure _cycleProxies exists (may be missing if namespace was created elsewhere)\n";
    ss << "  if (!_global[_owl]._cycleProxies) {\n";
    ss << "    _global[_owl]._cycleProxies = new WeakSet();\n";
    ss << "  }\n\n";

    ss << GenerateToStringMaskingScript();
    ss << "\n";
    ss << GenerateCreateNativeProxyScript();
    ss << "\n";
    ss << GenerateGuardFlagsScript();
    
    ss << "\n  // Mark utilities as initialized\n";
    ss << "  _global[_owl].utilsInitialized = true;\n";
    ss << "})();\n";
    
    return ss.str();
}

std::string SpoofUtils::GenerateStackTraceFixScript() {
    std::stringstream ss;

    ss << R"JS(
  // ============================================================
  // STACK TRACE FIX FOR TOSTRING DETECTION
  // CreepJS checks that error stack traces show correct call sites:
  // - Object.create(fn).toString() should show "at Function.toString"
  // - Object.create(new Proxy(fn, {})).toString() should show "at Object.toString"
  // V8 provides getTypeName() and getMethodName() that have the correct values,
  // but our object method shorthand toString would show "Object.toString" for both.
  // This fix uses Error.prepareStackTrace to format stacks correctly.
  // ============================================================

  const _origPrepareStackTrace = Error.prepareStackTrace;

  Error.prepareStackTrace = function(err, stack) {
    // Build stack string using V8's structured stack trace
    let result = err.toString();
    for (let i = 0; i < stack.length; i++) {
      const frame = stack[i];
      const typeName = frame.getTypeName();
      const methodName = frame.getMethodName();
      const functionName = frame.getFunctionName();
      const fileName = frame.getFileName() || '<anonymous>';
      const lineNum = frame.getLineNumber();
      const colNum = frame.getColumnNumber();

      let line;
      if (typeName && methodName) {
        // Method call: "at TypeName.methodName (file:line:col)"
        line = `    at ${typeName}.${methodName} (${fileName}:${lineNum}:${colNum})`;
      } else if (typeName && functionName) {
        // Function with type context: "at TypeName.functionName (file:line:col)"
        line = `    at ${typeName}.${functionName} (${fileName}:${lineNum}:${colNum})`;
      } else if (functionName) {
        // Plain function: "at functionName (file:line:col)"
        line = `    at ${functionName} (${fileName}:${lineNum}:${colNum})`;
      } else {
        // Anonymous: "at file:line:col"
        line = `    at ${fileName}:${lineNum}:${colNum}`;
      }

      result += '\n' + line;
    }
    return result;
  };

  // Register for toString masking
  _global[_owl]._origPrepareStackTrace = _origPrepareStackTrace;
)JS";

    return ss.str();
}

std::string SpoofUtils::GenerateToStringMaskingScript() {
    std::stringstream ss;

    ss << R"JS(
  // ============================================================
  // FUNCTION.PROTOTYPE.TOSTRING MASKING
  // All spoofed functions must look like native functions:
  // - 'prototype' in fn -> false
  // - Object.getOwnPropertyNames(fn) -> ['length', 'name']
  // - Reflect.ownKeys(fn) -> ['length', 'name']
  // - class X extends fn {} -> correct error
  // - fn.toString() -> 'function name() { [native code] }'
  //
  // CRITICAL CreepJS tests that must pass:
  // - Object.prototype.toString.call(fn) -> "[object Function]"
  // - Object.setPrototypeOf(fn, Object.create(fn)); fn.toString() -> "too much recursion"
  // - __proto__ chain cycle detection
  // - Reflect.setPrototypeOf tests
  // ============================================================

  // Registry: patched function -> native-looking toString output
  const _patchedFunctions = new WeakMap();

  // CRITICAL: Global Symbol for cross-realm toString masking
  // When iframe's toString is called with main frame's function, Map lookup fails
  // because each realm has its own WeakMap. Symbol.for() creates shared Symbol.
  const _maskedSymbol = Symbol.for('__owlMaskedToString');

  // Store original functions FIRST before any patching
  const _origToString = Function.prototype.toString;
  const _origCall = Function.prototype.call;
  const _origApply = Function.prototype.apply;
  const _origBind = Function.prototype.bind;
  const _origGetOwnPropertyDescriptor = Object.getOwnPropertyDescriptor;
  const _origDefineProperty = Object.defineProperty;
  const _origGetOwnPropertyNames = Object.getOwnPropertyNames;
  const _origKeys = Object.keys;
  const _origSetPrototypeOf = Object.setPrototypeOf;
  const _origReflectSetProto = typeof Reflect !== 'undefined' ? Reflect.setPrototypeOf : null;
  const _origHasOwnProperty = Object.prototype.hasOwnProperty;

  // Map proxies to their originals for prototype chain operations
  const _proxyOriginals = new WeakMap();

  // Map wrappers to their proxies (for toString when called on wrapper)
  const _wrapperToProxy = new WeakMap();

  // Get the original target from a proxy
  const getOriginal = (obj) => _proxyOriginals.get(obj) || obj;

  // ============================================================
  // createNativeProxy: Creates functions that look exactly like native functions
  // Uses METHOD SHORTHAND instead of Proxy so V8 reports "Function" in stack traces.
  // Must pass these creepjs tests:
  // - Object.create(fn).toString() -> proper TypeError with "Function.toString" in stack
  // - Object.create(new Proxy(fn, {})).toString() -> proper TypeError with "Object.toString"
  // - 'prototype' in fn -> false
  // - Reflect.ownKeys(fn) -> ['length', 'name']
  // ============================================================
  const createNativeProxy = (original, applyHandler, nameOverride) => {
    const effectiveName = nameOverride || original.name;
    const nativeString = `function ${effectiveName}() { [native code] }`;

    // Create wrapper using method shorthand with computed property name
    // This gives us:
    // - No prototype property naturally
    // - Correct typeName ("Function") in V8 stack traces
    // - Dynamic 'this' access via .call()/.apply()
    // - ownKeys = ['length', 'name'] only
    const wrapper = {
      [effectiveName](...args) {
        return applyHandler(original, this, args);
      }
    }[effectiveName];

    // Register for toString masking
    _patchedFunctions.set(wrapper, nativeString);

    // Set Symbol for cross-realm toString masking
    try {
      _origDefineProperty(wrapper, _maskedSymbol, {
        value: nativeString, writable: false, enumerable: false, configurable: true
      });
    } catch(e) {}

    return wrapper;
  };

  // ============================================================
  // Patch Function.prototype.toString using OBJECT METHOD SHORTHAND
  // Method shorthand automatically has no prototype property.
  // We use Error.prepareStackTrace to fix stack traces so they show
  // "at Function.toString" instead of "at Object.toString".
  // ============================================================

  // Store original prepareStackTrace
  const _origPrepareStackTrace = Error.prepareStackTrace;

  // Smart prepareStackTrace that fixes stack traces for detection evasion
  // Must handle:
  // 1. toString on Function prototype -> "at Function.toString"
  // 2. toString on Object (via Proxy) -> "at Object.toString"
  // 3. [Symbol.hasInstance] -> NO type prefix (V8 native shows just "at [Symbol.hasInstance]")
  //    This is critical because CreepJS patterns are:
  //    - /at (Function\.)?\[Symbol.hasInstance\]/ - matches "at [Symbol.hasInstance]"
  //    - /at (Object\.)?\[Symbol.hasInstance\]/ - also matches "at [Symbol.hasInstance]"
  Error.prepareStackTrace = function(err, stack) {
    let result = String(err);
    for (let i = 0; i < stack.length; i++) {
      const frame = stack[i];
      const funcName = frame.getFunctionName();
      const typeName = frame.getTypeName();
      const methodName = frame.getMethodName();
      const fileName = frame.getFileName() || '<anonymous>';
      const lineNum = frame.getLineNumber();
      const colNum = frame.getColumnNumber();

      let line;
      const name = methodName || funcName;

      // CRITICAL: [Symbol.hasInstance] must NOT have type prefix to match V8 native format
      // V8 native shows "at [Symbol.hasInstance] (<anonymous>)" without Function. or Object.
      if (name === '[Symbol.hasInstance]') {
        line = '    at [Symbol.hasInstance] (' + fileName + ')';
      }
      // Use typeName from V8's getTypeName() for other methods
      else if (typeName && name) {
        line = '    at ' + typeName + '.' + name + ' (' + fileName + ':' + lineNum + ':' + colNum + ')';
      } else if (funcName) {
        line = '    at ' + funcName + ' (' + fileName + ':' + lineNum + ':' + colNum + ')';
      } else {
        line = '    at ' + fileName + ':' + lineNum + ':' + colNum;
      }
      result += '\n' + line;
    }
    return result;
  };

  // Create toString using object method shorthand - automatically has no prototype
  const toStringImpl = {
    toString() {
      // CRITICAL: Check for cycle proxies FIRST
      if (_global[_owl]._cycleProxies && _global[_owl]._cycleProxies.has(this)) {
        throw new TypeError('Cyclic __proto__ value');
      }

      // CRITICAL: Only check masked string if 'this' is actually a function
      // Object.create(fn).toString() should throw TypeError, not return fn's string
      // The Symbol lookup would otherwise find it via prototype chain
      if (this && typeof this === 'function') {
        // Check Symbol property for cross-realm support (OWN property only matters for functions)
        if (typeof this[_maskedSymbol] === 'string') {
          return this[_maskedSymbol];
        }
        // Check local WeakMap (same realm)
        const maskedString = _patchedFunctions.get(this);
        if (maskedString !== undefined) {
          return maskedString;
        }
      }

      // Fall back to original toString - will throw TypeError for non-functions
      return _origCall.call(_origToString, this);
    }
  }.toString;

  // Register for toString masking
  _patchedFunctions.set(toStringImpl, 'function toString() { [native code] }');
  try {
    _origDefineProperty(toStringImpl, _maskedSymbol, {
      value: 'function toString() { [native code] }',
      writable: false, enumerable: false, configurable: true
    });
  } catch(e) {}

  Function.prototype.toString = toStringImpl;

  // ============================================================
  // FALLBACK: Hide prototype from enumeration for toStringImpl
  // Object method shorthand has no prototype, but we keep these as backup.
  // ============================================================
  const _origGetOwnPropNames = Object.getOwnPropertyNames;
  const _origGetOwnPropDesc = Object.getOwnPropertyDescriptor;
  const _origGetOwnPropDescs = Object.getOwnPropertyDescriptors;
  const _origReflectOwnKeys = Reflect.ownKeys;
  const _origObjectKeys = Object.keys;
  const _origReflectHas = Reflect.has;
  const _origHasOwn = Object.hasOwn;

  // Helper to check if we should hide internal properties for ALL patched functions
  // Native functions don't have prototype or our internal symbols, so hide them
  const isPatchedFunction = (obj) => _patchedFunctions.has(obj);

  // Helper to check if a property/key should be hidden from patched functions
  // Must hide: 'prototype' and our internal _maskedSymbol
  const shouldHideKey = (key) => key === 'prototype' || key === _maskedSymbol;

  Object.getOwnPropertyNames = function(obj) {
    const names = _origCall.call(_origGetOwnPropNames, Object, obj);
    if (isPatchedFunction(obj)) {
      return names.filter(n => n !== 'prototype');
    }
    return names;
  };
  _patchedFunctions.set(Object.getOwnPropertyNames, 'function getOwnPropertyNames() { [native code] }');

  Object.getOwnPropertyDescriptor = function(obj, prop) {
    if (isPatchedFunction(obj) && shouldHideKey(prop)) {
      return undefined;
    }
    return _origCall.call(_origGetOwnPropDesc, Object, obj, prop);
  };
  _patchedFunctions.set(Object.getOwnPropertyDescriptor, 'function getOwnPropertyDescriptor() { [native code] }');

  Object.getOwnPropertyDescriptors = function(obj) {
    const descs = _origCall.call(_origGetOwnPropDescs, Object, obj);
    if (isPatchedFunction(obj)) {
      delete descs.prototype;
      delete descs[_maskedSymbol];
    }
    return descs;
  };
  _patchedFunctions.set(Object.getOwnPropertyDescriptors, 'function getOwnPropertyDescriptors() { [native code] }');

  Reflect.ownKeys = function(obj) {
    const keys = _origCall.call(_origReflectOwnKeys, Reflect, obj);
    if (isPatchedFunction(obj)) {
      return keys.filter(k => !shouldHideKey(k));
    }
    return keys;
  };
  _patchedFunctions.set(Reflect.ownKeys, 'function ownKeys() { [native code] }');

  Object.keys = function(obj) {
    const keys = _origCall.call(_origObjectKeys, Object, obj);
    if (isPatchedFunction(obj)) {
      return keys.filter(k => k !== 'prototype');
    }
    return keys;
  };
  _patchedFunctions.set(Object.keys, 'function keys() { [native code] }');

  // Patch Reflect.has for 'prototype' and _maskedSymbol check
  Reflect.has = function(obj, prop) {
    if (isPatchedFunction(obj) && shouldHideKey(prop)) {
      return false;
    }
    return _origCall.call(_origReflectHas, Reflect, obj, prop);
  };
  _patchedFunctions.set(Reflect.has, 'function has() { [native code] }');

  // Patch Object.hasOwn
  if (_origHasOwn) {
    Object.hasOwn = function(obj, prop) {
      if (isPatchedFunction(obj) && shouldHideKey(prop)) {
        return false;
      }
      return _origCall.call(_origHasOwn, Object, obj, prop);
    };
    _patchedFunctions.set(Object.hasOwn, 'function hasOwn() { [native code] }');
  }

  // Patch Object.prototype.hasOwnProperty for "failed own property" check
  const _origHasOwnProp = Object.prototype.hasOwnProperty;
  Object.prototype.hasOwnProperty = function(prop) {
    if (isPatchedFunction(this) && shouldHideKey(prop)) {
      return false;
    }
    return _origCall.call(_origHasOwnProp, this, prop);
  };
  _patchedFunctions.set(Object.prototype.hasOwnProperty, 'function hasOwnProperty() { [native code] }');

  // ============================================================
  // Patch Object.setPrototypeOf using createNativeProxy
  // CRITICAL: Do NOT unwrap proxies - let them trigger their traps
  // ============================================================
  const setPrototypeOfProxy = createNativeProxy(_origSetPrototypeOf, (target, thisArg, args) => {
    // Pass through directly - proxy targets will trigger their setPrototypeOf trap
    return _origCall.call(target, Object, args[0], args[1]);
  }, 'setPrototypeOf');

  Object.setPrototypeOf = setPrototypeOfProxy;

  // ============================================================
  // Patch Reflect.setPrototypeOf using createNativeProxy
  // CRITICAL: Do NOT unwrap proxies - let them trigger their traps
  // ============================================================
  if (_origReflectSetProto) {
    const reflectSetPrototypeOfProxy = createNativeProxy(_origReflectSetProto, (target, thisArg, args) => {
      // Pass through directly - proxy targets will trigger their setPrototypeOf trap
      return _origCall.call(target, Reflect, args[0], args[1]);
    }, 'setPrototypeOf');

    Reflect.setPrototypeOf = reflectSetPrototypeOfProxy;
  }

  // Export helpers
  _global[_owl].registerProxy = (proxy, original) => {
    _proxyOriginals.set(proxy, original);
  };

  _global[_owl].registerNative = (fn, nativeString) => {
    const str = nativeString || `function ${fn.name || 'anonymous'}() { [native code] }`;
    _patchedFunctions.set(fn, str);
    // Also set Symbol for cross-realm toString masking
    try {
      _origDefineProperty(fn, _maskedSymbol, {
        value: str, writable: false, enumerable: false, configurable: true
      });
    } catch(e) {} // Ignore if property already exists or can't be set
  };

  // Store references for internal use
  _global[_owl]._patchedFunctions = _patchedFunctions;
  _global[_owl]._proxyOriginals = _proxyOriginals;
  _global[_owl]._maskedSymbol = _maskedSymbol;
)JS";

    return ss.str();
}

std::string SpoofUtils::GenerateCreateNativeProxyScript() {
    std::stringstream ss;

    ss << R"JS(
  // ============================================================
  // Export createNativeProxy to _global[_owl] for other spoofs
  // The implementation is already defined above
  // ============================================================
  _global[_owl].createNativeProxy = createNativeProxy;
)JS";

    return ss.str();
}

std::string SpoofUtils::GenerateGuardFlagsScript() {
    std::stringstream ss;
    
    ss << R"JS(
  // ============================================================
  // GUARD FLAGS
  // Each spoof module sets a flag when it patches to prevent double-patching
  // Check these flags before applying any patches
  // ============================================================

  _global[_owl].guards = {
    navigator: false,
    canvas: false,
    webgl: false,
    audio: false,
    screen: false,
    timezone: false,
    fonts: false,
    permissions: false,
    network: false,
    storage: false,
    battery: false,
    media: false,
    cdpRemoved: false
  };

  // Helper to check and set guard
  _global[_owl].checkGuard = (name) => {
    if (_global[_owl].guards[name]) {
      return false; // Already patched
    }
    _global[_owl].guards[name] = true;
    return true; // OK to patch
  };
)JS";
    
    return ss.str();
}

} // namespace spoofs
} // namespace owl
