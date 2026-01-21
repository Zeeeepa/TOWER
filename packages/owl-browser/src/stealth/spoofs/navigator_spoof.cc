#include "stealth/spoofs/navigator_spoof.h"
#include "util/logger.h"
#include <sstream>
#include <iomanip>

namespace owl {
namespace spoofs {

NavigatorSpoof::Config NavigatorSpoof::Config::FromVM(const VirtualMachine& vm) {
    Config config;
    config.user_agent = vm.browser.user_agent;
    config.platform = vm.os.platform;
    config.app_version = vm.os.app_version;
    config.vendor = vm.browser.vendor;
    config.app_name = vm.browser.app_name;
    config.app_code_name = vm.browser.app_code_name;
    config.product = vm.browser.product;
    config.product_sub = vm.browser.product_sub;
    config.build_id = vm.browser.build_id;
    config.hardware_concurrency = vm.cpu.hardware_concurrency;
    config.device_memory = vm.cpu.device_memory;
    config.max_touch_points = vm.os.max_touch_points;
    config.languages = vm.language.languages;
    config.primary_language = vm.language.primary;
    config.webdriver = false; // Always false
    config.pdf_viewer_enabled = vm.browser.pdf_viewer_enabled;
    config.cookies_enabled = vm.browser.cookies_enabled;
    config.java_enabled = vm.browser.java_enabled;
    config.enable_client_hints = vm.client_hints.enabled;
    config.sec_ch_ua = vm.client_hints.sec_ch_ua;
    config.sec_ch_ua_platform = vm.client_hints.sec_ch_ua_platform;
    config.sec_ch_ua_mobile = vm.client_hints.sec_ch_ua_mobile;
    config.sec_ch_ua_full_version = vm.client_hints.sec_ch_ua_full_version;
    config.sec_ch_ua_arch = vm.client_hints.sec_ch_ua_arch;
    config.sec_ch_ua_bitness = vm.client_hints.sec_ch_ua_bitness;
    config.sec_ch_ua_model = vm.client_hints.sec_ch_ua_model;
    config.browser_name = vm.browser.name;
    config.browser_version = vm.browser.version;
    return config;
}

bool NavigatorSpoof::Inject(CefRefPtr<CefFrame> frame, const Config& config) {
    if (!frame) {
        LOG_DEBUG("NavigatorSpoof", "Inject: null frame");
        return false;
    }
    
    std::string script = GenerateScript(config);
    frame->ExecuteJavaScript(script, frame->GetURL(), 0);
    LOG_DEBUG("NavigatorSpoof", "Injected navigator spoofs into frame");
    return true;
}

std::string NavigatorSpoof::GenerateScript(const Config& config) {
    std::stringstream ss;
    
    ss << "(function() {\n";
    ss << "  'use strict';\n";
    ss << "  const _owl = Symbol.for('owl');\n\n";
    
    // Guard check
    ss << "  // Guard: Skip if already patched\n";
    ss << "  if (!window[_owl]?.checkGuard('navigator')) return;\n\n";
    
    // Get createNativeProxy from utilities
    ss << "  const createNativeProxy = window[_owl]?.createNativeProxy;\n";
    ss << "  if (!createNativeProxy) return;\n\n";
    
    // Configuration object
    ss << "  // Navigator configuration\n";
    ss << "  const __navConfig = {\n";
    ss << "    userAgent: \"" << EscapeJS(config.user_agent) << "\",\n";
    ss << "    platform: \"" << EscapeJS(config.platform) << "\",\n";
    ss << "    appVersion: \"" << EscapeJS(config.app_version) << "\",\n";
    ss << "    vendor: \"" << EscapeJS(config.vendor) << "\",\n";
    ss << "    appName: \"" << EscapeJS(config.app_name) << "\",\n";
    ss << "    appCodeName: \"" << EscapeJS(config.app_code_name) << "\",\n";
    ss << "    product: \"" << EscapeJS(config.product) << "\",\n";
    ss << "    productSub: \"" << EscapeJS(config.product_sub) << "\",\n";
    if (!config.build_id.empty()) {
        ss << "    buildID: \"" << EscapeJS(config.build_id) << "\",\n";
    }
    ss << "    hardwareConcurrency: " << config.hardware_concurrency << ",\n";
    ss << "    deviceMemory: " << config.device_memory << ",\n";
    ss << "    maxTouchPoints: " << config.max_touch_points << ",\n";
    ss << "    languages: " << VectorToJSArray(config.languages) << ",\n";
    ss << "    language: \"" << EscapeJS(config.primary_language) << "\",\n";
    ss << "    webdriver: false,\n";
    ss << "    pdfViewerEnabled: " << (config.pdf_viewer_enabled ? "true" : "false") << ",\n";
    ss << "    cookieEnabled: " << (config.cookies_enabled ? "true" : "false") << ",\n";
    ss << "    javaEnabled: " << (config.java_enabled ? "true" : "false") << "\n";
    ss << "  };\n\n";
    
    // Core properties spoofing
    ss << GenerateCorePropsScript(config);
    
    // Webdriver spoofing - CRITICAL: must be false, not undefined
    ss << GenerateWebdriverScript();
    
    // Plugins spoofing
    ss << GeneratePluginsScript(config.pdf_viewer_enabled);
    
    // UserAgentData (Client Hints)
    if (config.enable_client_hints) {
        ss << GenerateUserAgentDataScript(config);
    }
    
    // GPU stub
    ss << GenerateGPUStubScript();
    
    ss << "})();\n";
    
    return ss.str();
}

std::string NavigatorSpoof::GenerateCorePropsScript(const Config& config) {
    std::stringstream ss;

    ss << R"JS(
  // ============================================================
  // CORE NAVIGATOR PROPERTIES
  // Use createNativeProxy to pass all introspection tests
  // ============================================================

  const navProto = Object.getPrototypeOf(navigator);

  // CRITICAL: Use native method to detect real Navigator vs Object.create(Navigator.prototype)
  // Native methods like sendBeacon have internal slot checking that throws "Illegal invocation"
  // on fake objects. Object.prototype.toString doesn't work because Symbol.toStringTag
  // makes both real and fake return "[object Navigator]"
  const _origSendBeacon = Navigator.prototype.sendBeacon;
  const isRealNavigator = (obj) => {
    try {
      // sendBeacon throws "Illegal invocation" on fake objects (Object.create)
      // but throws different error on real navigator (protocol error, etc)
      _origSendBeacon.call(obj, '', null);
      return true;
    } catch (e) {
      // "Illegal invocation" means fake object, any other error means real navigator
      return !e.message.includes('Illegal invocation');
    }
  };

  // Get a template getter for creating new getters (for props without native getter)
  // Define these BEFORE the guard so they're available for webdriver script
  const templateDesc = Object.getOwnPropertyDescriptor(navProto, 'userAgent');
  const templateGetter = templateDesc?.get;

  // CRITICAL: Prototype-level guard to prevent double-patching across iframes
  // Navigator.prototype is shared across all same-origin windows, so we must
  // use a marker ON THE PROTOTYPE ITSELF, not per-window guards.
  const _navPatched = Symbol.for('owl_navigator_patched');
  if (!navProto[_navPatched]) {
    // Mark as patched BEFORE we start (prevents race conditions)
    try {
      Object.defineProperty(navProto, _navPatched, {
        value: true,
        writable: false,
        enumerable: false,
        configurable: false
      });
    } catch (e) {
      // If we can't set the marker, another context might have just set it
      if (navProto[_navPatched]) {
        // Skip - already patched
      }
    }

    // Only run if we successfully claimed the guard (or first time)
    if (navProto[_navPatched]) {
      // Properties to spoof with their config keys
      const propsToSpoof = [
    'userAgent', 'platform', 'appVersion', 'vendor', 'appName',
    'appCodeName', 'product', 'productSub', 'hardwareConcurrency',
    'deviceMemory', 'maxTouchPoints', 'pdfViewerEnabled', 'cookieEnabled'
  ];

  for (const prop of propsToSpoof) {
    if (__navConfig[prop] === undefined) continue;
    
    try {
      const origDesc = Object.getOwnPropertyDescriptor(navProto, prop);
      const value = __navConfig[prop];
      
      if (origDesc && origDesc.get) {
        // Native getter exists - wrap it with createNativeProxy
        const proxyGetter = createNativeProxy(origDesc.get, (target, thisArg, args) => {
          if (!isRealNavigator(thisArg)) {
            throw new TypeError("Illegal invocation");
          }
          return value;
        }, `get ${prop}`);
        
        Object.defineProperty(navProto, prop, {
          get: proxyGetter,
          configurable: true,
          enumerable: true
        });
      } else if (templateGetter) {
        // No native getter - use template
        const proxyGetter = createNativeProxy(templateGetter, (target, thisArg, args) => {
          if (!isRealNavigator(thisArg)) {
            throw new TypeError("Illegal invocation");
          }
          return value;
        }, `get ${prop}`);
        
        Object.defineProperty(navProto, prop, {
          get: proxyGetter,
          configurable: true,
          enumerable: true
        });
      }
    } catch (e) {
      // Property might not exist on this browser
    }
  }
  
  // Override languages array - needs special handling for freeze
  try {
    const origLangDesc = Object.getOwnPropertyDescriptor(navProto, 'languages');
    if (origLangDesc && origLangDesc.get) {
      const proxyLangGetter = createNativeProxy(origLangDesc.get, (target, thisArg, args) => {
        if (!isRealNavigator(thisArg)) {
          throw new TypeError("Illegal invocation");
        }
        return Object.freeze([...__navConfig.languages]);
      }, 'get languages');
      
      Object.defineProperty(navProto, 'languages', {
        get: proxyLangGetter,
        configurable: true,
        enumerable: true
      });
    }
  } catch (e) {}
  
  // Override language
  try {
    const origLangDesc = Object.getOwnPropertyDescriptor(navProto, 'language');
    if (origLangDesc && origLangDesc.get) {
      const proxyGetter = createNativeProxy(origLangDesc.get, (target, thisArg, args) => {
        if (!isRealNavigator(thisArg)) {
          throw new TypeError("Illegal invocation");
        }
        return __navConfig.language;
      }, 'get language');
      
      Object.defineProperty(navProto, 'language', {
        get: proxyGetter,
        configurable: true,
        enumerable: true
      });
    }
  } catch (e) {}
  
  // Override javaEnabled method
  try {
    const origJavaEnabled = navProto.javaEnabled;
    if (origJavaEnabled) {
      const proxyJavaEnabled = createNativeProxy(origJavaEnabled, (target, thisArg, args) => {
        if (!isRealNavigator(thisArg)) {
          throw new TypeError("Illegal invocation");
        }
        return __navConfig.javaEnabled;
      }, 'javaEnabled');
      
      Object.defineProperty(navProto, 'javaEnabled', {
        value: proxyJavaEnabled,
        writable: true,
        configurable: true,
        enumerable: true
      });
    }
  } catch (e) {}

    } // End of inner if block (navProto[_navPatched] check)
  } // End of outer if block (!navProto[_navPatched] check)
)JS";
    
    return ss.str();
}

std::string NavigatorSpoof::GenerateWebdriverScript() {
    std::stringstream ss;

    ss << R"JS(
  // ============================================================
  // WEBDRIVER SPOOFING - CRITICAL
  // Must return FALSE, not undefined!
  // fingerprint.com: undefined = "nodriver" status, false = normal browser
  // ============================================================

  // Check prototype-level guard (webdriver is on shared prototype)
  const _wdPatched = Symbol.for('owl_webdriver_patched');
  if (!navProto[_wdPatched]) {
    try {
      Object.defineProperty(navProto, _wdPatched, {
        value: true,
        writable: false,
        enumerable: false,
        configurable: false
      });
    } catch (e) {}

    if (navProto[_wdPatched]) {
      try {
        // Remove any instance property first
        delete navigator.webdriver;

        const origWdDesc = Object.getOwnPropertyDescriptor(navProto, 'webdriver');

        if (origWdDesc && origWdDesc.get) {
          // Native getter exists - wrap it
          const proxyWdGetter = createNativeProxy(origWdDesc.get, (target, thisArg, args) => {
            if (!isRealNavigator(thisArg)) {
              throw new TypeError("Illegal invocation");
            }
            return false; // MUST be false, not undefined
          }, 'get webdriver');

          Object.defineProperty(navProto, 'webdriver', {
            get: proxyWdGetter,
            configurable: true,
            enumerable: true
          });
        } else if (templateGetter) {
          // No native getter - use template
          const proxyWdGetter = createNativeProxy(templateGetter, (target, thisArg, args) => {
            if (!isRealNavigator(thisArg)) {
              throw new TypeError("Illegal invocation");
            }
            return false;
          }, 'get webdriver');

          Object.defineProperty(navProto, 'webdriver', {
            get: proxyWdGetter,
            configurable: true,
            enumerable: true
          });
        } else {
          // Fallback: simple getter (less stealthy but functional)
          Object.defineProperty(navProto, 'webdriver', {
            get: function() {
              if (!isRealNavigator(this)) {
                throw new TypeError("Illegal invocation");
              }
              return false;
            },
            configurable: true,
            enumerable: true
          });
        }
      } catch (e) {
        // webdriver patch failed
      }
    }
  }
)JS";
    
    return ss.str();
}

std::string NavigatorSpoof::GeneratePluginsScript(bool pdf_enabled) {
    std::stringstream ss;

    ss << R"JS(
  // ============================================================
  // PLUGINS AND MIMETYPES SPOOFING
  // Real Chrome 143+ includes ALL 5 PDF plugins
  // MimeType properties must use proper getters like native Chrome
  // ============================================================

  try {
    const chromePluginNames = [
      'PDF Viewer',
      'Chrome PDF Viewer',
      'Chromium PDF Viewer',
      'Microsoft Edge PDF Viewer',
      'WebKit built-in PDF'
    ];

    // Helper to create a MimeType-like object with proper getters
    // Native MimeType has getters for: type, suffixes, description, enabledPlugin
    const createFakeMimeType = (typeValue, suffixesValue, descValue) => {
      // Create object with MimeType prototype
      const mime = Object.create(MimeType.prototype);

      // Store values in a private slot using Symbol
      const _values = Symbol('values');
      mime[_values] = { type: typeValue, suffixes: suffixesValue, description: descValue, enabledPlugin: null };

      // Get the native getters from MimeType.prototype and wrap them
      // If they don't exist (unlikely), create our own
      const typeDesc = Object.getOwnPropertyDescriptor(MimeType.prototype, 'type');
      const suffixesDesc = Object.getOwnPropertyDescriptor(MimeType.prototype, 'suffixes');
      const descriptionDesc = Object.getOwnPropertyDescriptor(MimeType.prototype, 'description');
      const enabledPluginDesc = Object.getOwnPropertyDescriptor(MimeType.prototype, 'enabledPlugin');

      // Define getters that return our values but look native
      if (typeDesc && typeDesc.get) {
        const typeGetter = createNativeProxy(typeDesc.get, (target, thisArg, args) => {
          return thisArg[_values]?.type || '';
        }, 'get type');
        Object.defineProperty(mime, 'type', { get: typeGetter, configurable: true, enumerable: true });
      } else {
        Object.defineProperty(mime, 'type', {
          get: function() { return this[_values]?.type || ''; },
          configurable: true, enumerable: true
        });
      }

      if (suffixesDesc && suffixesDesc.get) {
        const suffixesGetter = createNativeProxy(suffixesDesc.get, (target, thisArg, args) => {
          return thisArg[_values]?.suffixes || '';
        }, 'get suffixes');
        Object.defineProperty(mime, 'suffixes', { get: suffixesGetter, configurable: true, enumerable: true });
      } else {
        Object.defineProperty(mime, 'suffixes', {
          get: function() { return this[_values]?.suffixes || ''; },
          configurable: true, enumerable: true
        });
      }

      if (descriptionDesc && descriptionDesc.get) {
        const descGetter = createNativeProxy(descriptionDesc.get, (target, thisArg, args) => {
          return thisArg[_values]?.description || '';
        }, 'get description');
        Object.defineProperty(mime, 'description', { get: descGetter, configurable: true, enumerable: true });
      } else {
        Object.defineProperty(mime, 'description', {
          get: function() { return this[_values]?.description || ''; },
          configurable: true, enumerable: true
        });
      }

      if (enabledPluginDesc && enabledPluginDesc.get) {
        const pluginGetter = createNativeProxy(enabledPluginDesc.get, (target, thisArg, args) => {
          return thisArg[_values]?.enabledPlugin || null;
        }, 'get enabledPlugin');
        Object.defineProperty(mime, 'enabledPlugin', { get: pluginGetter, configurable: true, enumerable: true });
      } else {
        Object.defineProperty(mime, 'enabledPlugin', {
          get: function() { return this[_values]?.enabledPlugin || null; },
          configurable: true, enumerable: true
        });
      }

      // Method to set enabledPlugin (internal use)
      mime._setEnabledPlugin = (plugin) => { mime[_values].enabledPlugin = plugin; };

      return mime;
    };

    // Create fake Plugin objects with proper structure
    const createFakePlugin = (name) => {
      const mimeType1 = createFakeMimeType('application/pdf', 'pdf', 'Portable Document Format');
      const mimeType2 = createFakeMimeType('text/pdf', 'pdf', 'Portable Document Format');

      // Create plugin object with Plugin prototype
      const plugin = Object.create(Plugin.prototype);

      // Store values
      const _values = Symbol('values');
      plugin[_values] = { name, filename: 'internal-pdf-viewer', description: 'Portable Document Format' };

      // Define getters for Plugin properties
      const nameDesc = Object.getOwnPropertyDescriptor(Plugin.prototype, 'name');
      const filenameDesc = Object.getOwnPropertyDescriptor(Plugin.prototype, 'filename');
      const descriptionDesc = Object.getOwnPropertyDescriptor(Plugin.prototype, 'description');
      const lengthDesc = Object.getOwnPropertyDescriptor(Plugin.prototype, 'length');

      // CRITICAL: name, filename, description, length must be NON-enumerable
      // so Object.values(plugin) only returns MimeType objects (indices 0, 1)
      // CreepJS checks: Object.values(plugin).map(m => m.type) - fails if non-MimeTypes included
      if (nameDesc && nameDesc.get) {
        const nameGetter = createNativeProxy(nameDesc.get, (target, thisArg, args) => {
          return thisArg[_values]?.name || '';
        }, 'get name');
        Object.defineProperty(plugin, 'name', { get: nameGetter, configurable: true, enumerable: false });
      } else {
        Object.defineProperty(plugin, 'name', {
          get: function() { return this[_values]?.name || ''; },
          configurable: true, enumerable: false
        });
      }

      if (filenameDesc && filenameDesc.get) {
        const filenameGetter = createNativeProxy(filenameDesc.get, (target, thisArg, args) => {
          return thisArg[_values]?.filename || '';
        }, 'get filename');
        Object.defineProperty(plugin, 'filename', { get: filenameGetter, configurable: true, enumerable: false });
      } else {
        Object.defineProperty(plugin, 'filename', {
          get: function() { return this[_values]?.filename || ''; },
          configurable: true, enumerable: false
        });
      }

      if (descriptionDesc && descriptionDesc.get) {
        const descGetter = createNativeProxy(descriptionDesc.get, (target, thisArg, args) => {
          return thisArg[_values]?.description || '';
        }, 'get description');
        Object.defineProperty(plugin, 'description', { get: descGetter, configurable: true, enumerable: false });
      } else {
        Object.defineProperty(plugin, 'description', {
          get: function() { return this[_values]?.description || ''; },
          configurable: true, enumerable: false
        });
      }

      if (lengthDesc && lengthDesc.get) {
        const lengthGetter = createNativeProxy(lengthDesc.get, (target, thisArg, args) => {
          return 2;
        }, 'get length');
        Object.defineProperty(plugin, 'length', { get: lengthGetter, configurable: true, enumerable: false });
      } else {
        Object.defineProperty(plugin, 'length', {
          get: function() { return 2; },
          configurable: true, enumerable: false
        });
      }

      // Add indexed access to mimetypes
      plugin[0] = mimeType1;
      plugin[1] = mimeType2;

      // Set enabledPlugin reference on mimetypes
      mimeType1._setEnabledPlugin(plugin);
      mimeType2._setEnabledPlugin(plugin);

      // Add item() and namedItem() methods
      const origItem = Plugin.prototype.item;
      const origNamedItem = Plugin.prototype.namedItem;

      if (origItem) {
        const itemProxy = createNativeProxy(origItem, (target, thisArg, args) => {
          const index = args[0];
          return thisArg[index] || null;
        }, 'item');
        Object.defineProperty(plugin, 'item', { value: itemProxy, writable: true, configurable: true });
      }

      if (origNamedItem) {
        const namedItemProxy = createNativeProxy(origNamedItem, (target, thisArg, args) => {
          const name = args[0];
          if (mimeType1.type === name) return mimeType1;
          if (mimeType2.type === name) return mimeType2;
          return null;
        }, 'namedItem');
        Object.defineProperty(plugin, 'namedItem', { value: namedItemProxy, writable: true, configurable: true });
      }

      return plugin;
    };

    const fakePlugins = chromePluginNames.map(createFakePlugin);

    // Create cached PluginArray
    let _pluginArrayCache = null;
    const getPluginArray = () => {
      if (_pluginArrayCache) return _pluginArrayCache;

      const arr = Object.create(PluginArray.prototype);
      fakePlugins.forEach((p, i) => {
        arr[i] = p;
        const name = p.name || chromePluginNames[i];
        arr[name] = p;
      });

      // Define length as getter
      const lengthDesc = Object.getOwnPropertyDescriptor(PluginArray.prototype, 'length');
      if (lengthDesc && lengthDesc.get) {
        const lengthGetter = createNativeProxy(lengthDesc.get, (target, thisArg, args) => {
          return fakePlugins.length;
        }, 'get length');
        Object.defineProperty(arr, 'length', { get: lengthGetter, configurable: true, enumerable: true });
      } else {
        Object.defineProperty(arr, 'length', { value: fakePlugins.length, configurable: true });
      }

      // Add item() and namedItem() methods
      const origItem = PluginArray.prototype.item;
      const origNamedItem = PluginArray.prototype.namedItem;
      const origRefresh = PluginArray.prototype.refresh;

      if (origItem) {
        const itemProxy = createNativeProxy(origItem, (target, thisArg, args) => {
          return fakePlugins[args[0]] || null;
        }, 'item');
        arr.item = itemProxy;
      }

      if (origNamedItem) {
        const namedItemProxy = createNativeProxy(origNamedItem, (target, thisArg, args) => {
          return fakePlugins.find(p => p.name === args[0]) || null;
        }, 'namedItem');
        arr.namedItem = namedItemProxy;
      }

      if (origRefresh) {
        const refreshProxy = createNativeProxy(origRefresh, (target, thisArg, args) => {}, 'refresh');
        arr.refresh = refreshProxy;
      }

      // Make iterable
      arr[Symbol.iterator] = function*() { for (const p of fakePlugins) yield p; };

      _pluginArrayCache = arr;
      return arr;
    };

    // Create cached MimeTypeArray
    // Real Chrome has only 2 unique mimeTypes, not 10 (one per plugin)
    let _mimeTypeArrayCache = null;
    const getMimeTypeArray = () => {
      if (_mimeTypeArrayCache) return _mimeTypeArrayCache;

      // Only use the first plugin's mimeTypes (2 entries, not 10)
      const allMimes = [fakePlugins[0][0], fakePlugins[0][1]];

      const arr = Object.create(MimeTypeArray.prototype);
      allMimes.forEach((m, i) => {
        arr[i] = m;
        arr[m.type] = m;
      });

      // Define length as getter
      const lengthDesc = Object.getOwnPropertyDescriptor(MimeTypeArray.prototype, 'length');
      if (lengthDesc && lengthDesc.get) {
        const lengthGetter = createNativeProxy(lengthDesc.get, (target, thisArg, args) => {
          return allMimes.length;
        }, 'get length');
        Object.defineProperty(arr, 'length', { get: lengthGetter, configurable: true, enumerable: true });
      } else {
        Object.defineProperty(arr, 'length', { value: allMimes.length, configurable: true });
      }

      // Add item() and namedItem() methods
      const origItem = MimeTypeArray.prototype.item;
      const origNamedItem = MimeTypeArray.prototype.namedItem;

      if (origItem) {
        const itemProxy = createNativeProxy(origItem, (target, thisArg, args) => {
          return allMimes[args[0]] || null;
        }, 'item');
        arr.item = itemProxy;
      }

      if (origNamedItem) {
        const namedItemProxy = createNativeProxy(origNamedItem, (target, thisArg, args) => {
          return allMimes.find(m => m.type === args[0]) || null;
        }, 'namedItem');
        arr.namedItem = namedItemProxy;
      }

      // Make iterable
      arr[Symbol.iterator] = function*() { for (const m of allMimes) yield m; };

      _mimeTypeArrayCache = arr;
      return arr;
    };

    // Override navigator.plugins
    const origPluginsDesc = Object.getOwnPropertyDescriptor(navProto, 'plugins');
    if (origPluginsDesc && origPluginsDesc.get) {
      const proxyPlugins = createNativeProxy(origPluginsDesc.get, (target, thisArg, args) => {
        if (!isRealNavigator(thisArg)) {
          throw new TypeError("Illegal invocation");
        }
        return getPluginArray();
      }, 'get plugins');

      Object.defineProperty(navProto, 'plugins', {
        get: proxyPlugins,
        configurable: true,
        enumerable: true
      });
    }

    // Override navigator.mimeTypes
    const origMimeDesc = Object.getOwnPropertyDescriptor(navProto, 'mimeTypes');
    if (origMimeDesc && origMimeDesc.get) {
      const proxyMimes = createNativeProxy(origMimeDesc.get, (target, thisArg, args) => {
        if (!isRealNavigator(thisArg)) {
          throw new TypeError("Illegal invocation");
        }
        return getMimeTypeArray();
      }, 'get mimeTypes');

      Object.defineProperty(navProto, 'mimeTypes', {
        get: proxyMimes,
        configurable: true,
        enumerable: true
      });
    }
  } catch (e) {
    // plugins patch failed
  }
)JS";
    
    return ss.str();
}

std::string NavigatorSpoof::GenerateUserAgentDataScript(const Config& config) {
    std::stringstream ss;
    
    // Build brands array
    std::string version = config.browser_version.empty() ? "143" : config.browser_version;
    std::string major_version = version.substr(0, version.find('.'));
    if (major_version.empty()) major_version = version;
    
    ss << "\n  // ============================================================\n";
    ss << "  // USER-AGENT DATA (Client Hints)\n";
    ss << "  // ============================================================\n\n";
    
    ss << "  try {\n";
    ss << "    const __uaDataBrands = [\n";
    ss << "      { brand: 'Chromium', version: '" << EscapeJS(major_version) << "' },\n";
    ss << "      { brand: 'Not)A;Brand', version: '99' },\n";
    ss << "      { brand: 'Google Chrome', version: '" << EscapeJS(major_version) << "' }\n";
    ss << "    ];\n\n";
    
    std::string ua_platform = config.platform == "Win32" ? "Windows" :
                              config.platform == "MacIntel" ? "macOS" : "Linux";
    ss << "    const __uaDataConfig = {\n";
    ss << "      brands: Object.freeze(__uaDataBrands),\n";
    ss << "      mobile: false,\n";
    ss << "      platform: '" << ua_platform << "'\n";
    ss << "    };\n\n";
    
    ss << R"JS(
    // Check if userAgentData exists
    if ('userAgentData' in navigator) {
      const origUAData = navigator.userAgentData;
      
      // Create spoofed userAgentData object
      const __spoofedUAData = {
        brands: __uaDataConfig.brands,
        mobile: __uaDataConfig.mobile,
        platform: __uaDataConfig.platform,
        
        getHighEntropyValues: function getHighEntropyValues(hints) {
          return Promise.resolve({
            brands: __uaDataConfig.brands,
            mobile: __uaDataConfig.mobile,
            platform: __uaDataConfig.platform,
)JS";
    
    ss << "            platformVersion: '15.0.0',\n";
    ss << "            architecture: 'x86',\n";
    ss << "            bitness: '64',\n";
    ss << "            model: '',\n";
    ss << "            uaFullVersion: '" << version << "',\n";
    ss << "            fullVersionList: [\n";
    ss << "              { brand: 'Chromium', version: '" << version << "' },\n";
    ss << "              { brand: 'Not)A;Brand', version: '99.0.0.0' },\n";
    ss << "              { brand: 'Google Chrome', version: '" << version << "' }\n";
    ss << "            ]\n";

    ss << R"JS(
          });
        },
        
        toJSON: function toJSON() {
          return {
            brands: __uaDataConfig.brands,
            mobile: __uaDataConfig.mobile,
            platform: __uaDataConfig.platform
          };
        }
      };
      
      // Set prototype to match real NavigatorUAData
      if (origUAData) {
        Object.setPrototypeOf(__spoofedUAData, Object.getPrototypeOf(origUAData));
      }
      
      // Override navigator.userAgentData
      const origUADataDesc = Object.getOwnPropertyDescriptor(navProto, 'userAgentData');
      if (origUADataDesc && origUADataDesc.get) {
        const proxyUAData = createNativeProxy(origUADataDesc.get, (target, thisArg, args) => {
          if (!isRealNavigator(thisArg)) {
            throw new TypeError("Illegal invocation");
          }
          return __spoofedUAData;
        }, 'get userAgentData');
        
        Object.defineProperty(navProto, 'userAgentData', {
          get: proxyUAData,
          configurable: true,
          enumerable: true
        });
      }
    }
  } catch (e) {
    // userAgentData patch failed
  }
)JS";
    
    return ss.str();
}

std::string NavigatorSpoof::GenerateGPUStubScript() {
    std::stringstream ss;
    
    ss << R"JS(
  // ============================================================
  // NAVIGATOR.GPU STUB
  // Must be an object, not undefined!
  // Detection: ('gpu' in navigator) && typeof navigator.gpu === 'undefined'
  // ============================================================
  
  try {
    delete navigator.gpu; // Remove any instance property
    
    // Recursion guard for property introspection
    let __gpuAccessInProgress = false;
    
    // Create GPU stub that looks real but returns null adapter
    const __gpuStubBase = Object.create(null);
    __gpuStubBase.requestAdapter = function requestAdapter(options) {
      return Promise.resolve(null);
    };
    __gpuStubBase.getPreferredCanvasFormat = function getPreferredCanvasFormat() {
      return 'bgra8unorm';
    };
    
    // Register methods for toString masking
    window[_owl].registerNative(__gpuStubBase.requestAdapter, 'function requestAdapter() { [native code] }');
    window[_owl].registerNative(__gpuStubBase.getPreferredCanvasFormat, 'function getPreferredCanvasFormat() { [native code] }');
    
    // Create Proxy for safe property access
    const __gpuStub = new Proxy(__gpuStubBase, {
      get(target, prop, receiver) {
        if (__gpuAccessInProgress && prop !== Symbol.toStringTag) {
          return target[prop];
        }
        __gpuAccessInProgress = true;
        try {
          if (prop === 'requestAdapter') return target.requestAdapter;
          if (prop === 'getPreferredCanvasFormat') return target.getPreferredCanvasFormat;
          if (prop === 'wgslLanguageFeatures') return new Set();
          if (prop === Symbol.toStringTag) return 'GPU';
          return target[prop];
        } finally {
          __gpuAccessInProgress = false;
        }
      },
      has(target, prop) {
        return ['requestAdapter', 'getPreferredCanvasFormat', 'wgslLanguageFeatures'].includes(prop);
      },
      ownKeys(target) {
        return ['requestAdapter', 'getPreferredCanvasFormat', 'wgslLanguageFeatures'];
      }
    });
    
    // Override navigator.gpu
    const origGpuDesc = Object.getOwnPropertyDescriptor(navProto, 'gpu');
    const origGpuGetter = origGpuDesc?.get || templateGetter;
    
    if (origGpuGetter) {
      const proxyGpu = createNativeProxy(origGpuGetter, (target, thisArg, args) => {
        if (!isRealNavigator(thisArg)) {
          throw new TypeError("Illegal invocation");
        }
        return __gpuStub;
      }, 'get gpu');
      
      Object.defineProperty(navProto, 'gpu', {
        get: proxyGpu,
        configurable: true,
        enumerable: true
      });
    }
  } catch (e) {
    // gpu stub failed
  }
)JS";
    
    return ss.str();
}

std::string NavigatorSpoof::EscapeJS(const std::string& str) {
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

std::string NavigatorSpoof::VectorToJSArray(const std::vector<std::string>& vec) {
    std::stringstream ss;
    ss << "[";
    for (size_t i = 0; i < vec.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << "\"" << EscapeJS(vec[i]) << "\"";
    }
    ss << "]";
    return ss.str();
}

} // namespace spoofs
} // namespace owl
