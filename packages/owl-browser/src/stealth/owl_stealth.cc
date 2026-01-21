#include "owl_stealth.h"
#include "owl_app.h"
#include "stealth/owl_virtual_machine.h"
#include "stealth/owl_spoof_manager.h"
#include "network/owl_demographics.h"
#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "util/logger.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <cstdlib>

std::string OwlStealth::session_noise_ = "";
std::map<int, StealthConfig> OwlStealth::browser_configs_;
std::map<int, owl::VirtualMachine> OwlStealth::browser_vms_;
std::mutex OwlStealth::configs_mutex_;

void OwlStealth::InjectStealthPatchesWithConfig(CefRefPtr<CefFrame> frame, const StealthConfig& config) {
  // Inject stealth patches with per-context fingerprint configuration
  RemoveWebDriver(frame);
  RemoveCDPArtifacts(frame);
  BlockMacOSFonts(frame);
  BlockClientHints(frame);
  BlockWebRTC(frame);
  BlockGeolocation(frame);
  BlockWebGPU(frame);

  // Use VirtualMachine for COMPLETE, CONSISTENT hardware fingerprinting
  // This handles: Navigator, Screen, WebGL, Audio, Canvas - ALL in one place
  // DO NOT call SpoofNavigatorProps - it conflicts with VirtualMachine!
  InjectHardwareSimulation(frame, config);

  // Inject timezone separately
  // Priority: 1. config.timezone (set from proxy detection), 2. GeoIP, 3. default
  std::string effective_timezone;
  if (!config.timezone.empty()) {
    // Use timezone from stealth config (already set from proxy GeoIP detection in BrowserManager)
    effective_timezone = config.timezone;
    LOG_DEBUG("OwlStealth", "Using config timezone: " + effective_timezone);
  } else {
    // Fallback to GeoIP detection
    effective_timezone = GetEffectiveTimezone();
  }
  SpoofTimezone(frame, effective_timezone);

  InjectVirtualCamera(frame);  // Virtual camera support for reCAPTCHA hand gestures
  PatchIframeCreationWithConfig(frame, config);
}

void OwlStealth::SetContextFingerprint(int browser_id, const StealthConfig& config) {
  std::lock_guard<std::mutex> lock(configs_mutex_);
  browser_configs_[browser_id] = config;
}

StealthConfig OwlStealth::GetContextFingerprint(int browser_id) {
  std::lock_guard<std::mutex> lock(configs_mutex_);
  auto it = browser_configs_.find(browser_id);
  if (it != browser_configs_.end()) {
    return it->second;
  }
  return StealthConfig();  // Return default config if not found
}

void OwlStealth::SetContextVM(int browser_id, const owl::VirtualMachine& vm) {
  std::lock_guard<std::mutex> lock(configs_mutex_);
  browser_vms_[browser_id] = vm;
}

const owl::VirtualMachine* OwlStealth::GetContextVM(int browser_id) {
  std::lock_guard<std::mutex> lock(configs_mutex_);
  auto it = browser_vms_.find(browser_id);
  if (it != browser_vms_.end()) {
    return &it->second;
  }
  return nullptr;
}

std::string OwlStealth::GetEffectiveTimezone(const std::string& proxy_timezone_override) {
  // Priority: 1. Proxy override, 2. GeoIP from public IP, 3. Default

  // 1. If proxy timezone override is set, use it
  if (!proxy_timezone_override.empty()) {
    LOG_DEBUG("OwlStealth", "Using proxy timezone override: " + proxy_timezone_override);
    return proxy_timezone_override;
  }

  // 2. Try to get timezone from GeoIP (based on public IP)
  OwlDemographics* demographics = OwlDemographics::GetInstance();
  if (demographics) {
    GeoLocationInfo location = demographics->GetGeoLocation();
    if (location.success && !location.timezone.empty()) {
      LOG_DEBUG("OwlStealth", "Using GeoIP timezone: " + location.timezone +
               " (IP: " + location.ip_address + ", " + location.city + ", " + location.country_code + ")");
      return location.timezone;
    } else if (!location.success) {
      LOG_DEBUG("OwlStealth", "GeoIP lookup failed: " + location.error);
    }
  }

  // 3. Fallback to America/New_York (common default)
  LOG_DEBUG("OwlStealth", "Using default timezone: America/New_York");
  return "America/New_York";
}

void OwlStealth::InjectHardwareSimulation(CefRefPtr<CefFrame> frame, const StealthConfig& config) {
  const owl::VirtualMachine* vm = nullptr;

  // First, try to look up VM by ID from config (set by browser process)
  // This is the cross-process way to ensure headers and JS use the SAME profile
  if (!config.vm_id.empty()) {
    vm = owl::VirtualMachineDB::Instance().GetVM(config.vm_id);
    if (vm) {
      LOG_DEBUG("OwlStealth", "Using VM from config.vm_id: " + config.vm_id);
    } else {
      LOG_DEBUG("OwlStealth", "VM not found for id: " + config.vm_id + ", will select new");
    }
  }

  // Fallback: try to get from in-process cache (works if same process)
  CefRefPtr<CefBrowser> browser = frame->GetBrowser();
  if (!vm && browser) {
    int browser_id = browser->GetIdentifier();
    vm = GetContextVM(browser_id);
    if (vm) {
      LOG_DEBUG("OwlStealth", "Using pre-stored VM profile for browser_id=" + std::to_string(browser_id) +
               " (vm: " + vm->id + ")");
    }
  }

  // If still no VM, get vm_id from OwlApp's storage (set by OnBrowserCreated)
  // CRITICAL: Use OwlApp::GetBrowserVMId to get the SAME vm_id that browser process selected
  // DO NOT call SelectRandomVM - it may pick a DIFFERENT VM!
  if (!vm && browser) {
    int browser_id = browser->GetIdentifier();

    // Get vm_id from OwlApp's per-browser storage (set via extra_info in OnBrowserCreated)
    std::string vm_id = OwlApp::GetBrowserVMId(browser_id);

    if (!vm_id.empty()) {
      vm = owl::VirtualMachineDB::Instance().GetVM(vm_id);
      if (vm) {
        LOG_INFO("OwlStealth", "[VM_SYNC] Using vm_id=" + vm_id +
                 " from OwlApp::GetBrowserVMId for browser_id=" + std::to_string(browser_id));
      } else {
        LOG_ERROR("OwlStealth", "[VM_SYNC] GetVM failed for vm_id=" + vm_id +
                  " from OwlApp::GetBrowserVMId (not found in VirtualMachineDB)");
      }
    } else {
      LOG_ERROR("OwlStealth", "[VM_SYNC] No vm_id from OwlApp::GetBrowserVMId for browser_id=" +
                std::to_string(browser_id) + " - OnBrowserCreated may not have been called yet!");
    }
  }

  if (!vm) {
    LOG_WARN("OwlStealth", "No VirtualMachine profiles available!");
    return;
  }

  LOG_DEBUG("OwlStealth", "Using VM profile: " + vm->id + " - " + vm->name);
  LOG_DEBUG("OwlStealth", "VM User-Agent: " + vm->browser.user_agent);

  // Block macOS fonts ONLY when spoofing to non-macOS platforms
  // When spoofing to macOS, we want macOS fonts to be available for consistency
  bool shouldBlockMacOSFonts = (vm->os.name != "macOS");
  BlockMacOSFontsConditional(frame, shouldBlockMacOSFonts);

  // Generate context_id for fingerprint seed generation
  // CRITICAL: In multi-process CEF, config.context_id is empty in renderer process
  // because SetContextFingerprint is called in browser process (different memory space).
  // We MUST generate a deterministic context_id here using available data.
  std::string effective_context_id = config.context_id;
  if (effective_context_id.empty() && browser) {
    // Generate deterministic context_id from session_vm_seed + browser_id
    // This matches the formula used in CreateContext for VM selection
    int browser_id = browser->GetIdentifier();
    uint64_t session_vm_seed = 0;
    CefRefPtr<CefCommandLine> cmd_line = CefCommandLine::GetGlobalCommandLine();
    if (cmd_line && cmd_line->HasSwitch("owl-vm-seed")) {
      std::string seed_str = cmd_line->GetSwitchValue("owl-vm-seed").ToString();
      if (!seed_str.empty()) {
        char* end = nullptr;
        session_vm_seed = strtoull(seed_str.c_str(), &end, 10);
      }
    }
    // Create unique context_id: "ctx_<session_seed>_<browser_id>"
    effective_context_id = "ctx_" + std::to_string(session_vm_seed) + "_" + std::to_string(browser_id);
    LOG_DEBUG("OwlStealth", "Generated renderer context_id: " + effective_context_id +
             " (browser_id=" + std::to_string(browser_id) + ")");
  }

  // Use SpoofManager for centralized, modular injection
  // This replaces the old VirtualMachineInjector::GenerateScript approach
  // SpoofManager handles: Navigator, Screen, Canvas, WebGL, Audio, Timezone
  // all using the modular spoof system at src/stealth/spoofs/
  LOG_DEBUG("OwlStealth", "Using SpoofManager for modular spoof injection");
  owl::spoofs::SpoofManager::Instance().InjectForFrame(frame, *vm, effective_context_id);
}

void OwlStealth::PatchIframeCreationWithConfig(CefRefPtr<CefFrame> frame, const StealthConfig& config) {
  // NOTE: Do NOT call PatchIframeCreation() here!
  // The old PatchIframeCreation has conflicting WebGL patches that interfere with
  // the VirtualMachine system. Since we patch at the prototype level in
  // InjectHardwareSimulation, those patches automatically apply to all iframes
  // that share the same JavaScript context (same origin).
  //
  // Cross-origin iframes get their own context and will be patched by
  // OnContextCreated when they load.
}

void OwlStealth::RemoveWebDriver(CefRefPtr<CefFrame> frame) {
  // NOTE: navigator.webdriver = false is now set at C++ V8 level in owl_app.cc
  // This is more reliable than JavaScript because it runs before any page JS
  // and cannot be blocked by JavaScript Object.defineProperty overrides.
  //
  // This function now only handles supplementary cleanup tasks.

  std::string script = R"(
    // WEBDRIVER SUPPLEMENTARY CLEANUP
    // Main webdriver=false is set in C++ (owl_app.cc OnContextCreated)
    // This script handles additional cleanup that can't be done in C++

    (function() {
      // 1. Remove documentElement.webdriver attribute (alternate detection vector)
      try {
        if (document.documentElement && document.documentElement.hasAttribute('webdriver')) {
          document.documentElement.removeAttribute('webdriver');
        }
      } catch(e) {}

      // 7. Remove Selenium/WebDriver globals - COMPREHENSIVE LIST from fingerprint.js
      const automationGlobals = [
        // Selenium/WebDriver
        '__webdriver_script_fn', '__driver_evaluate', '__webdriver_evaluate',
        '__selenium_evaluate', '__fxdriver_evaluate', '__driver_unwrapped',
        '__webdriver_unwrapped', '__selenium_unwrapped', '__fxdriver_unwrapped',
        '_Selenium_IDE_Recorder', '_selenium', 'calledSelenium',
        '__webdriver_script_func', '__webdriver_script_function',
        // Playwright/Puppeteer/Automation
        '__playwright', '__puppeteer',
        // NightmareJS - BOTH variants (critical!)
        '__nightmare', 'nightmare',
        // PhantomJS
        '__phantomas', 'callPhantom', '_phantom',
        // WebDriverIO
        'wdioElectron',
        // Watir (Ruby)
        '__lastWatirAlert', '__lastWatirConfirm', '__lastWatirPrompt',
        // CEF-specific (detected as "cef" bot type)
        'RunPerfTest', 'CefSharp', 'awesomium',
        // Other automation
        '__webdriverFunc', '_WEBDRIVER_ELEM_CACHE', 'ChromeDriverw',
        '__$webdriverAsyncExecutor', '$chrome_asyncScriptInfo',
        'fmget_targets', 'geb', 'spawn',
        // CoachJS detection - window.emit (EventEmitter pattern)
        'emit',
        // Headless Chrome
        'domAutomation', 'domAutomationController'
      ];

      automationGlobals.forEach(prop => {
        try { delete window[prop]; } catch(e) {}
        try { delete document[prop]; } catch(e) {}
      });

      // 8. Remove CDP detection variables from document
      const cdpDocVars = [
        '$cdc_asdjflasutopfhvcZLmcfl_',
        '$cdc_asdjflasutopfhvcZLmcfl',
        '__selenium_evaluate',
        'selenium-evaluate',
        '__selenium_unwrapped',
        '__webdriver_script_fn',
        '__driver_evaluate',
        '__webdriver_evaluate',
        '__fxdriver_evaluate',
        '__driver_unwrapped',
        '__webdriver_unwrapped',
        '__fxdriver_unwrapped',
        '__webdriver_script_func',
        '__webdriver_script_function'
      ];

      cdpDocVars.forEach(varName => {
        if (document && document[varName]) {
          try { delete document[varName]; } catch(e) {}
        }
      });

      // 9. NOTE: We intentionally do NOT use Object.defineProperty to block
      // future assignment of automation globals. Using defineProperty would
      // CREATE the property on window, and fingerprint.com's bot detection
      // uses Object.getOwnPropertyNames(window) to find them - even if they
      // return undefined, their mere existence triggers detection.
      // Just deleting them is sufficient.
    })();
  )";

  frame->ExecuteJavaScript(script, frame->GetURL(), 0);
}

void OwlStealth::RemoveCDPArtifacts(CefRefPtr<CefFrame> frame) {
  std::string script = R"(
    // COMPREHENSIVE CDP (Chrome DevTools Protocol) & CEF ARTIFACT REMOVAL
    // Fixes detection by fingerprint.com and similar services

    (function() {
      'use strict';

      // ============================================================
      // 1. REMOVE CEF/AUTOMATION ARTIFACTS (Critical for bot detection)
      // fingerprint.js checks multiple automation tools
      // ============================================================
      const cefArtifacts = [
        // CEF-specific
        'RunPerfTest',           // CEF-specific performance test function
        'CefSharp',              // CefSharp .NET wrapper
        'cefQuery',              // CEF query function
        'cefQueryCancel',        // CEF query cancel
        '__cef',                 // CEF internal
        '__gCrWeb',              // Chrome iOS webview
        '__crWeb',               // Chrome webview
        // NightmareJS (Yo="nightmarejs") - CRITICAL
        'nightmare',             // NightmareJS global
        '__nightmare',           // NightmareJS internal
        // CoachJS (Uo="coachjs")
        'emit',                  // EventEmitter pattern
        // Other automation
        'awesomium',             // Awesomium browser
        '__phantomas',           // Phantomas
        'callPhantom',           // PhantomJS
        '_phantom'               // PhantomJS
      ];

      cefArtifacts.forEach(prop => {
        try { delete window[prop]; } catch(e) {}
      });

      // ============================================================
      // 2. REMOVE window.process (Electron/Node.js detection)
      // fingerprint.js checks: wi=function(){const{process:n}=window
      // NOTE: We only delete, we do NOT use defineProperty because that
      // would create the property and be detectable via getOwnPropertyNames
      // ============================================================
      try {
        if (window.process !== undefined) {
          delete window.process;
        }
      } catch(e) {}

      // ============================================================
      // 3. FIX window.chrome object - must match real Chrome
      // Real Chrome has: chrome.app, chrome.csi(), chrome.loadTimes()
      // CEF may be missing some, so we add them
      // ============================================================
      if (typeof window.chrome === 'undefined') {
        window.chrome = {};
      }

      // CRITICAL: Real Chrome does NOT expose chrome.runtime to web pages!
      // chrome.runtime is ONLY available in Chrome extensions, not on regular pages.
      // Owl Browser was incorrectly exposing it, causing "nodriver" detection.
      // DELETE any existing chrome.runtime that CEF might expose.
      if (window.chrome && window.chrome.runtime) {
        delete window.chrome.runtime;
      }

      // chrome.app - Real Chrome has this with specific structure
      if (!window.chrome.app) {
        window.chrome.app = {
          isInstalled: false,
          InstallState: {
            DISABLED: 'disabled',
            INSTALLED: 'installed',
            NOT_INSTALLED: 'not_installed'
          },
          RunningState: {
            CANNOT_RUN: 'cannot_run',
            READY_TO_RUN: 'ready_to_run',
            RUNNING: 'running'
          },
          getDetails: function() { return null; },
          getIsInstalled: function() { return false; },
          installState: function(callback) { callback && callback('not_installed'); },
          runningState: function() { return 'cannot_run'; }
        };
      }

      // chrome.csi - Client-Side Instrumentation (deprecated but present)
      if (!window.chrome.csi) {
        window.chrome.csi = function() {
          return {
            pageT: performance.now(),
            startE: performance.timeOrigin,
            onloadT: performance.timeOrigin + 500,
            tran: 15
          };
        };
      }

      // chrome.loadTimes - Deprecated but still present in real Chrome
      if (!window.chrome.loadTimes) {
        window.chrome.loadTimes = function() {
          const perf = performance.timing || {};
          const nav = performance.getEntriesByType && performance.getEntriesByType('navigation')[0] || {};
          return {
            commitLoadTime: (perf.responseStart || nav.responseStart || Date.now()) / 1000,
            connectionInfo: 'h2',
            finishDocumentLoadTime: (perf.domContentLoadedEventEnd || Date.now()) / 1000,
            finishLoadTime: (perf.loadEventEnd || Date.now()) / 1000,
            firstPaintAfterLoadTime: 0,
            firstPaintTime: (perf.responseEnd || Date.now()) / 1000,
            navigationType: 'Other',
            npnNegotiatedProtocol: 'h2',
            requestTime: (perf.requestStart || nav.requestStart || Date.now()) / 1000,
            startLoadTime: (perf.navigationStart || performance.timeOrigin) / 1000,
            wasAlternateProtocolAvailable: false,
            wasFetchedViaSpdy: true,
            wasNpnNegotiated: true
          };
        };
      }

      // Make chrome.csi and chrome.loadTimes look native
      // CRITICAL: Real Chrome shows 'function csi() { [native code] }' with the function name
      // The toString wrapper itself must also look native (toString.toString() must return native code)

      // Helper: Create a native-looking toString wrapper
      // Register function with global registry for native toString (NO direct toString property!)
      // Native functions inherit toString from Function.prototype - they don't have own toString
      function registerAsNative(fn, name) {
        var nativeString = 'function ' + name + '() { [native code] }';
        var _owl = Symbol.for('owl');
        if (window[_owl] && window[_owl].fn) {
          window[_owl].fn.set(fn, nativeString);
        }
      }

      try {
        registerAsNative(window.chrome.csi, 'csi');
        registerAsNative(window.chrome.loadTimes, 'loadTimes');
      } catch(e) {}

      // ============================================================
      // 3.5. STUB MISSING NAVIGATOR APIs - Real Chrome has these
      // CEF/headless may be missing some of these APIs
      // ============================================================
      try {
        // navigator.mediaDevices - Real Chrome has this
        if (!navigator.mediaDevices) {
          Object.defineProperty(navigator, 'mediaDevices', {
            get: function() {
              return {
                enumerateDevices: function() { return Promise.resolve([]); },
                getUserMedia: function() { return Promise.reject(new DOMException('Not allowed', 'NotAllowedError')); },
                getDisplayMedia: function() { return Promise.reject(new DOMException('Not allowed', 'NotAllowedError')); },
                getSupportedConstraints: function() { return {}; }
              };
            },
            configurable: true, enumerable: true
          });
        }

        // navigator.credentials - Web Credentials API
        if (!navigator.credentials) {
          Object.defineProperty(navigator, 'credentials', {
            get: function() {
              return {
                get: function() { return Promise.resolve(null); },
                store: function() { return Promise.resolve(); },
                create: function() { return Promise.resolve(null); },
                preventSilentAccess: function() { return Promise.resolve(); }
              };
            },
            configurable: true, enumerable: true
          });
        }

        // navigator.bluetooth - Web Bluetooth API (object, not full implementation)
        if (!navigator.bluetooth) {
          Object.defineProperty(navigator, 'bluetooth', {
            get: function() {
              return {
                getAvailability: function() { return Promise.resolve(false); },
                requestDevice: function() { return Promise.reject(new DOMException('Not allowed', 'NotAllowedError')); }
              };
            },
            configurable: true, enumerable: true
          });
        }

        // navigator.usb - WebUSB API
        if (!navigator.usb) {
          Object.defineProperty(navigator, 'usb', {
            get: function() {
              return {
                getDevices: function() { return Promise.resolve([]); },
                requestDevice: function() { return Promise.reject(new DOMException('Not allowed', 'NotAllowedError')); }
              };
            },
            configurable: true, enumerable: true
          });
        }

        // navigator.serial - Web Serial API
        if (!navigator.serial) {
          Object.defineProperty(navigator, 'serial', {
            get: function() {
              return {
                getPorts: function() { return Promise.resolve([]); },
                requestPort: function() { return Promise.reject(new DOMException('Not allowed', 'NotAllowedError')); }
              };
            },
            configurable: true, enumerable: true
          });
        }

        // navigator.hid - WebHID API
        if (!navigator.hid) {
          Object.defineProperty(navigator, 'hid', {
            get: function() {
              return {
                getDevices: function() { return Promise.resolve([]); },
                requestDevice: function() { return Promise.reject(new DOMException('Not allowed', 'NotAllowedError')); }
              };
            },
            configurable: true, enumerable: true
          });
        }

        // window.caches - Service Worker Cache API (missing = headless/automation detection)
        if (typeof caches === 'undefined') {
          const cacheStorage = {
            match: function() { return Promise.resolve(undefined); },
            has: function() { return Promise.resolve(false); },
            open: function(name) {
              return Promise.resolve({
                match: function() { return Promise.resolve(undefined); },
                matchAll: function() { return Promise.resolve([]); },
                add: function() { return Promise.resolve(); },
                addAll: function() { return Promise.resolve(); },
                put: function() { return Promise.resolve(); },
                delete: function() { return Promise.resolve(false); },
                keys: function() { return Promise.resolve([]); }
              });
            },
            delete: function() { return Promise.resolve(false); },
            keys: function() { return Promise.resolve([]); }
          };
          Object.defineProperty(window, 'caches', {
            get: function() { return cacheStorage; },
            configurable: true, enumerable: true
          });
        }

        // navigator.storage - StorageManager API (missing = headless/automation detection)
        if (typeof navigator.storage === 'undefined') {
          const storageManager = {
            estimate: function() {
              return Promise.resolve({
                quota: 2147483648,
                usage: 0,
                usageDetails: {}
              });
            },
            persist: function() { return Promise.resolve(true); },
            persisted: function() { return Promise.resolve(true); },
            getDirectory: function() {
              return Promise.reject(new DOMException('Not supported', 'NotSupportedError'));
            }
          };
          Object.defineProperty(navigator, 'storage', {
            get: function() { return storageManager; },
            configurable: true, enumerable: true
          });
        }

        // navigator.serviceWorker - ServiceWorker API (missing = headless/automation detection)
        if (typeof navigator.serviceWorker === 'undefined') {
          const serviceWorkerContainer = {
            controller: null,
            ready: Promise.resolve({
              active: null,
              installing: null,
              waiting: null,
              scope: '/',
              updateViaCache: 'imports',
              addEventListener: function() {},
              removeEventListener: function() {},
              dispatchEvent: function() { return true; },
              update: function() { return Promise.resolve(); },
              unregister: function() { return Promise.resolve(false); },
              getNotifications: function() { return Promise.resolve([]); },
              showNotification: function() { return Promise.resolve(); }
            }),
            oncontrollerchange: null,
            onmessage: null,
            onmessageerror: null,
            register: function() {
              return Promise.reject(new DOMException('Service workers are not supported', 'SecurityError'));
            },
            getRegistration: function() { return Promise.resolve(undefined); },
            getRegistrations: function() { return Promise.resolve([]); },
            startMessages: function() {},
            addEventListener: function() {},
            removeEventListener: function() {},
            dispatchEvent: function() { return true; }
          };
          Object.defineProperty(navigator, 'serviceWorker', {
            get: function() { return serviceWorkerContainer; },
            configurable: true, enumerable: true
          });
        }

        // navigator.getBattery - Battery Status API (missing = VM/automation detection)
        if (!navigator.getBattery) {
          Object.defineProperty(navigator, 'getBattery', {
            value: function() {
              return Promise.resolve({
                charging: true,
                chargingTime: 0,
                dischargingTime: Infinity,
                level: 1.0,
                onchargingchange: null,
                onchargingtimechange: null,
                ondischargingtimechange: null,
                onlevelchange: null,
                addEventListener: function() {},
                removeEventListener: function() {},
                dispatchEvent: function() { return true; }
              });
            },
            writable: true,
            enumerable: true,
            configurable: true
          });
        }

        // Notification API - Real Chrome has this
        // CRITICAL: Must appear native to avoid bot detection
        if (typeof Notification === 'undefined' || !Notification.requestPermission) {
          // Native function masking utility - uses global registry only (NO direct toString!)
          // Native functions inherit toString from Function.prototype - they don't have own toString
          const maskAsNative = (fn, name) => {
            const nativeStr = 'function ' + name + '() { [native code] }';
            try {
              Object.defineProperty(fn, 'name', { value: name, writable: false, enumerable: false, configurable: true });
              Object.defineProperty(fn, 'length', { value: fn.length || 0, writable: false, enumerable: false, configurable: true });
              // Make prototype configurable first, then delete (native functions don't have prototype)
              try { Object.defineProperty(fn, 'prototype', { configurable: true }); } catch(e) {}
              delete fn.prototype;
              // CRITICAL: Do NOT set direct toString property - register with global registry only!
              const _owl = Symbol.for('owl');
              if (window[_owl] && window[_owl].fn) {
                window[_owl].fn.set(fn, nativeStr);
              }
            } catch(e) {}
            return fn;
          };

          // Create proper Notification constructor that appears native
          const NotificationConstructor = function Notification(title, options) {
            if (!(this instanceof Notification)) {
              throw new TypeError("Failed to construct 'Notification': Please use the 'new' operator");
            }
            this.title = title || '';
            this.options = options || {};
            this.body = options?.body || '';
            this.icon = options?.icon || '';
            this.tag = options?.tag || '';
            this.data = options?.data || null;
            this.requireInteraction = options?.requireInteraction || false;
            this.silent = options?.silent || false;
            this.onclick = null;
            this.onclose = null;
            this.onerror = null;
            this.onshow = null;
          };

          // Static properties
          // CRITICAL: Real Chrome defaults to 'default' (not prompted), not 'denied'
          // 'denied' is a bot detection signature!
          Object.defineProperty(NotificationConstructor, 'permission', {
            get: function() { return 'default'; },
            enumerable: true, configurable: true
          });
          Object.defineProperty(NotificationConstructor, 'maxActions', {
            get: function() { return 2; },
            enumerable: true, configurable: true
          });

          // requestPermission - must appear native
          // Returns 'default' to simulate un-prompted state (not 'denied' which is a bot signature)
          const requestPermissionFn = function requestPermission(callback) {
            const result = Promise.resolve('default');
            if (callback) callback('default');
            return result;
          };
          maskAsNative(requestPermissionFn, 'requestPermission');
          NotificationConstructor.requestPermission = requestPermissionFn;

          // Mask the constructor itself
          maskAsNative(NotificationConstructor, 'Notification');

          // Set up prototype - CRITICAL: Must have all 20 properties to match Chrome
          // Real Chrome Notification.prototype has: actions, badge, body, data, dir, icon, image,
          // lang, onclick, onclose, onerror, onshow, renotify, requireInteraction, silent, tag,
          // timestamp, title, vibrate, close
          const proto = NotificationConstructor.prototype;

          // Methods
          proto.close = function close() {};
          maskAsNative(proto.close, 'close');
          proto.addEventListener = function addEventListener() {};
          proto.removeEventListener = function removeEventListener() {};
          proto.dispatchEvent = function dispatchEvent() { return true; };

          // Event handlers - must be on prototype as getters/setters
          const eventProps = ['onclick', 'onclose', 'onerror', 'onshow'];
          eventProps.forEach(prop => {
            Object.defineProperty(proto, prop, {
              get: function() { return this['_' + prop] || null; },
              set: function(v) { this['_' + prop] = v; },
              enumerable: true, configurable: true
            });
          });

          // Read-only properties - must be getters on prototype that read from instance
          const readOnlyProps = ['actions', 'badge', 'body', 'data', 'dir', 'icon', 'image',
                                  'lang', 'renotify', 'requireInteraction', 'silent', 'tag',
                                  'timestamp', 'title', 'vibrate'];
          readOnlyProps.forEach(prop => {
            Object.defineProperty(proto, prop, {
              get: function() {
                // Return instance property or default value
                if (this.options && this.options[prop] !== undefined) return this.options[prop];
                if (prop === 'actions' || prop === 'vibrate') return [];
                if (prop === 'renotify' || prop === 'requireInteraction' || prop === 'silent') return false;
                if (prop === 'dir') return 'auto';
                if (prop === 'timestamp') return Date.now();
                return '';
              },
              enumerable: true, configurable: true
            });
          });

          window.Notification = NotificationConstructor;

          // CRITICAL: Set constructor length to 1 (title is required)
          Object.defineProperty(NotificationConstructor, 'length', { value: 1, writable: false, configurable: true });
        }

      } catch(e) {}

      // ============================================================
      // 3.6. REMOVED: chrome.runtime stub - was causing "nodriver" detection!
      // Real Chrome does NOT expose chrome.runtime on web pages.
      // chrome.runtime is ONLY available in Chrome extensions.
      // The earlier code (line 383) already deletes any chrome.runtime that CEF exposes.
      // ============================================================

      // ============================================================
      // WebRTC API Stubs - CRITICAL for avoiding bot detection
      // Real Chrome always has WebRTC APIs, missing = automation detected
      // Chrome's RTCPeerConnection.prototype has 46 properties, we must match
      // ============================================================
      try {
        // RTCPeerConnection - Essential WebRTC API
        // CRITICAL: CEF removes WebRTC after injection - use defineProperty to lock
        (function() {
          // Private storage for instance data
          const _rtcData = new WeakMap();

          const RTCPeerConnectionStub = function RTCPeerConnection(config) {
            // Store instance data privately
            _rtcData.set(this, {
              localDescription: null,
              remoteDescription: null,
              signalingState: 'stable',
              iceGatheringState: 'new',
              iceConnectionState: 'new',
              connectionState: 'new',
              canTrickleIceCandidates: null,
              pendingLocalDescription: null,
              pendingRemoteDescription: null,
              currentLocalDescription: null,
              currentRemoteDescription: null,
              sctp: null,
              onconnectionstatechange: null,
              ondatachannel: null,
              onicecandidate: null,
              onicecandidateerror: null,
              oniceconnectionstatechange: null,
              onicegatheringstatechange: null,
              onnegotiationneeded: null,
              onsignalingstatechange: null,
              ontrack: null,
              config: config || {}
            });
          };

          // Create prototype with methods (27 methods to match Chrome)
          const proto = RTCPeerConnectionStub.prototype;

          // Core methods
          proto.createOffer = function createOffer() { return Promise.resolve({ type: 'offer', sdp: '' }); };
          proto.createAnswer = function createAnswer() { return Promise.resolve({ type: 'answer', sdp: '' }); };
          proto.setLocalDescription = function setLocalDescription() { return Promise.resolve(); };
          proto.setRemoteDescription = function setRemoteDescription() { return Promise.resolve(); };
          proto.addIceCandidate = function addIceCandidate() { return Promise.resolve(); };
          proto.getConfiguration = function getConfiguration() { return _rtcData.get(this)?.config || {}; };
          proto.setConfiguration = function setConfiguration(config) { if (_rtcData.get(this)) _rtcData.get(this).config = config; };
          proto.close = function close() { if (_rtcData.get(this)) _rtcData.get(this).signalingState = 'closed'; };
          proto.restartIce = function restartIce() {}; // Added

          // Data channel
          proto.createDataChannel = function createDataChannel(label) { return {}; };

          // Track management
          proto.addTrack = function addTrack() { return {}; };
          proto.removeTrack = function removeTrack() {};
          proto.addTransceiver = function addTransceiver() { return {}; };
          proto.getTransceivers = function getTransceivers() { return []; };
          proto.getSenders = function getSenders() { return []; };
          proto.getReceivers = function getReceivers() { return []; };

          // Stats
          proto.getStats = function getStats() { return Promise.resolve(new Map()); };

          // Deprecated stream methods (still present in Chrome)
          proto.addStream = function addStream() {}; // Added - deprecated but present
          proto.removeStream = function removeStream() {}; // Added - deprecated but present
          proto.getLocalStreams = function getLocalStreams() { return []; }; // Added - deprecated but present
          proto.getRemoteStreams = function getRemoteStreams() { return []; }; // Added - deprecated but present

          // Identity methods (deprecated but present in Chrome)
          proto.setIdentityProvider = function setIdentityProvider() {}; // Added
          proto.getIdentityAssertion = function getIdentityAssertion() { return Promise.resolve(''); }; // Added

          // EventTarget methods
          proto.addEventListener = function addEventListener() {};
          proto.removeEventListener = function removeEventListener() {};
          proto.dispatchEvent = function dispatchEvent() { return true; };

          // Define getters/setters on prototype for all state properties (19 properties)
          // CRITICAL: These must be on prototype, not instance, to match Chrome's 46 properties
          const stateProps = [
            'localDescription', 'remoteDescription', 'currentLocalDescription', 'currentRemoteDescription',
            'pendingLocalDescription', 'pendingRemoteDescription', 'signalingState', 'iceGatheringState',
            'iceConnectionState', 'connectionState', 'canTrickleIceCandidates', 'sctp'
          ];
          stateProps.forEach(prop => {
            Object.defineProperty(proto, prop, {
              get: function() { return _rtcData.get(this)?.[prop] || null; },
              set: function(v) { if (_rtcData.get(this)) _rtcData.get(this)[prop] = v; },
              enumerable: true, configurable: true
            });
          });

          // Event handler properties
          const eventProps = [
            'onconnectionstatechange', 'ondatachannel', 'onicecandidate', 'onicecandidateerror',
            'oniceconnectionstatechange', 'onicegatheringstatechange', 'onnegotiationneeded',
            'onsignalingstatechange', 'ontrack'
          ];
          eventProps.forEach(prop => {
            Object.defineProperty(proto, prop, {
              get: function() { return _rtcData.get(this)?.[prop] || null; },
              set: function(v) { if (_rtcData.get(this)) _rtcData.get(this)[prop] = v; },
              enumerable: true, configurable: true
            });
          });

          // Static methods
          RTCPeerConnectionStub.generateCertificate = function generateCertificate() {
            return Promise.resolve({ expires: Date.now() + 2592000000, getFingerprints: function() { return []; } });
          };

          Object.defineProperty(RTCPeerConnectionStub, 'name', { value: 'RTCPeerConnection' });
          // CRITICAL: Chrome's RTCPeerConnection.length is 0 (no required parameters)
          Object.defineProperty(RTCPeerConnectionStub, 'length', { value: 0 });
          // Register with global registry (NO direct toString property!)
          const _owl_rtc1 = Symbol.for('owl');
          if (window[_owl_rtc1] && window[_owl_rtc1].fn) {
            window[_owl_rtc1].fn.set(RTCPeerConnectionStub, 'function RTCPeerConnection() { [native code] }');
          }

          // Use defineProperty with writable:false to lock against CEF removal
          Object.defineProperty(window, 'RTCPeerConnection', {
            value: RTCPeerConnectionStub, writable: false, configurable: false, enumerable: true
          });
          Object.defineProperty(window, 'webkitRTCPeerConnection', {
            value: RTCPeerConnectionStub, writable: false, configurable: false, enumerable: true
          });
        })();

        // RTCSessionDescription - lock with defineProperty
        (function() {
          const RTCSessionDescriptionStub = function RTCSessionDescription(init) {
            this.type = init && init.type ? init.type : '';
            this.sdp = init && init.sdp ? init.sdp : '';
          };
          RTCSessionDescriptionStub.prototype.toJSON = function() {
            return { type: this.type, sdp: this.sdp };
          };
          Object.defineProperty(RTCSessionDescriptionStub, 'name', { value: 'RTCSessionDescription' });
          // Register with global registry (NO direct toString property!)
          const _owl_rtc2 = Symbol.for('owl');
          if (window[_owl_rtc2] && window[_owl_rtc2].fn) {
            window[_owl_rtc2].fn.set(RTCSessionDescriptionStub, 'function RTCSessionDescription() { [native code] }');
          }
          Object.defineProperty(window, 'RTCSessionDescription', {
            value: RTCSessionDescriptionStub, writable: false, configurable: false, enumerable: true
          });
        })();

        // RTCIceCandidate - lock with defineProperty
        (function() {
          const RTCIceCandidateStub = function RTCIceCandidate(init) {
            this.candidate = init && init.candidate ? init.candidate : '';
            this.sdpMid = init && init.sdpMid ? init.sdpMid : null;
            this.sdpMLineIndex = init && init.sdpMLineIndex ? init.sdpMLineIndex : null;
            this.foundation = '';
            this.component = null;
            this.priority = null;
            this.address = null;
            this.protocol = null;
            this.port = null;
            this.type = null;
            this.tcpType = null;
            this.relatedAddress = null;
            this.relatedPort = null;
            this.usernameFragment = init && init.usernameFragment ? init.usernameFragment : null;
          };
          RTCIceCandidateStub.prototype.toJSON = function() {
            return { candidate: this.candidate, sdpMid: this.sdpMid, sdpMLineIndex: this.sdpMLineIndex };
          };
          Object.defineProperty(RTCIceCandidateStub, 'name', { value: 'RTCIceCandidate' });
          // Register with global registry (NO direct toString property!)
          const _owl_rtc3 = Symbol.for('owl');
          if (window[_owl_rtc3] && window[_owl_rtc3].fn) {
            window[_owl_rtc3].fn.set(RTCIceCandidateStub, 'function RTCIceCandidate() { [native code] }');
          }
          Object.defineProperty(window, 'RTCIceCandidate', {
            value: RTCIceCandidateStub, writable: false, configurable: false, enumerable: true
          });
        })();

        // PushManager
        if (typeof PushManager === 'undefined') {
          const PushManagerStub = function PushManager() {};
          PushManagerStub.prototype = {
            subscribe: function() { return Promise.reject(new DOMException('Push not supported', 'NotSupportedError')); },
            getSubscription: function() { return Promise.resolve(null); },
            // 'prompt' = not asked yet (equivalent to Notification 'default'), 'denied' is a bot signature
            permissionState: function() { return Promise.resolve('prompt'); }
          };
          Object.defineProperty(PushManagerStub, 'name', { value: 'PushManager' });
          // Register with global registry (NO direct toString property!)
          const _owl_push = Symbol.for('owl');
          if (window[_owl_push] && window[_owl_push].fn) {
            window[_owl_push].fn.set(PushManagerStub, 'function PushManager() { [native code] }');
          }
          window.PushManager = PushManagerStub;
        }

        // ServiceWorker global (not just navigator.serviceWorker)
        if (typeof ServiceWorker === 'undefined') {
          const ServiceWorkerStub = function ServiceWorker() {};
          ServiceWorkerStub.prototype = {
            scriptURL: '',
            state: 'activated',
            onstatechange: null,
            onerror: null,
            postMessage: function() {},
            addEventListener: function() {},
            removeEventListener: function() {},
            dispatchEvent: function() { return true; }
          };
          Object.defineProperty(ServiceWorkerStub, 'name', { value: 'ServiceWorker' });
          // Register with global registry (NO direct toString property!)
          const _owl_sw = Symbol.for('owl');
          if (window[_owl_sw] && window[_owl_sw].fn) {
            window[_owl_sw].fn.set(ServiceWorkerStub, 'function ServiceWorker() { [native code] }');
          }
          window.ServiceWorker = ServiceWorkerStub;
        }

        // Cache global
        if (typeof Cache === 'undefined') {
          const CacheStub = function Cache() {};
          CacheStub.prototype = {
            match: function() { return Promise.resolve(undefined); },
            matchAll: function() { return Promise.resolve([]); },
            add: function() { return Promise.resolve(); },
            addAll: function() { return Promise.resolve(); },
            put: function() { return Promise.resolve(); },
            delete: function() { return Promise.resolve(false); },
            keys: function() { return Promise.resolve([]); }
          };
          Object.defineProperty(CacheStub, 'name', { value: 'Cache' });
          // Register with global registry (NO direct toString property!)
          const _owl_cache = Symbol.for('owl');
          if (window[_owl_cache] && window[_owl_cache].fn) {
            window[_owl_cache].fn.set(CacheStub, 'function Cache() { [native code] }');
          }
          window.Cache = CacheStub;
        }

        // CacheStorage global
        if (typeof CacheStorage === 'undefined') {
          const CacheStorageStub = function CacheStorage() {};
          CacheStorageStub.prototype = {
            match: function() { return Promise.resolve(undefined); },
            has: function() { return Promise.resolve(false); },
            open: function() { return Promise.resolve(new (window.Cache || function(){})()); },
            delete: function() { return Promise.resolve(false); },
            keys: function() { return Promise.resolve([]); }
          };
          Object.defineProperty(CacheStorageStub, 'name', { value: 'CacheStorage' });
          // Register with global registry (NO direct toString property!)
          const _owl_cs = Symbol.for('owl');
          if (window[_owl_cs] && window[_owl_cs].fn) {
            window[_owl_cs].fn.set(CacheStorageStub, 'function CacheStorage() { [native code] }');
          }
          window.CacheStorage = CacheStorageStub;
        }

        // NOTE: Clipboard hooks moved to owl_app.cc - they are browser tool functionality,
        // not stealth/anti-detection. See InjectClipboardHooks() in owl_app.cc.
      } catch(e) {}

      // ============================================================
      // 4. REMOVE CDP VARIABLES BY PREFIX
      // ============================================================
      const cdpPrefixes = ['cdc_', '$cdc_', '$chrome_', '__chrome__', 'chrome_'];

      // Scan window properties for CDP variables
      Object.keys(window).forEach(prop => {
        if (cdpPrefixes.some(prefix => prop.startsWith(prefix))) {
          try { delete window[prop]; } catch(e) {}
        }
      });

      // Also scan document
      if (document) {
        Object.keys(document).forEach(prop => {
          if (cdpPrefixes.some(prefix => prop.startsWith(prefix))) {
            try { delete document[prop]; } catch(e) {}
          }
        });
      }

      // ============================================================
      // 5. REMOVE CDP VARIABLES BY REGEX PATTERN
      // fingerprint.js uses: /^([a-z]){3}_.*_(Array|Promise|Symbol)$/
      // This catches: cdc_adoQpoasnfa76pfcZLmcfl_Array, etc.
      // ============================================================
      const cdpRegex = /^[a-z]{3}_.*_(Array|Promise|Symbol)$/;
      const cdpRegex2 = /^\$?cdc_/;  // Also catch $cdc_ variants

      Object.keys(window).forEach(prop => {
        if (cdpRegex.test(prop) || cdpRegex2.test(prop)) {
          try { delete window[prop]; } catch(e) {}
        }
      });

      if (document) {
        Object.keys(document).forEach(prop => {
          if (cdpRegex.test(prop) || cdpRegex2.test(prop)) {
            try { delete document[prop]; } catch(e) {}
          }
        });
      }

      // ============================================================
      // 6. REMOVE SPECIFIC CDP DETECTION VARIABLES
      // ============================================================
      const cdpVars = [
        // ChromeDriver specific
        'cdc_adoQpoasnfa76pfcZLmcfl_Array',
        'cdc_adoQpoasnfa76pfcZLmcfl_Promise',
        'cdc_adoQpoasnfa76pfcZLmcfl_Symbol',
        '$cdc_asdjflasutopfhvcZLmcfl_',
        '$cdc_asdjflasutopfhvcZLmcfl',
        '$chrome_asyncScriptInfo',
        '__$webdriverAsyncExecutor',
        // Watir (Ruby WebDriver)
        '__lastWatirAlert',
        '__lastWatirConfirm',
        '__lastWatirPrompt',
        // WebDriver script functions
        '__webdriver_script_fn',
        '__webdriver_script_func',
        '__webdriver_script_function',
        // WebDriverIO (Electron)
        'wdioElectron',
        // Chrome-specific automation
        'ChromeDriverw',
        '_WEBDRIVER_ELEM_CACHE',
        '__webdriverFunc',
        // Selenium internals
        '__driver_evaluate',
        '__webdriver_evaluate',
        '__fxdriver_evaluate',
        '__driver_unwrapped',
        '__webdriver_unwrapped',
        '__fxdriver_unwrapped',
        // Headless Chrome
        'domAutomation',
        'domAutomationController'
      ];

      cdpVars.forEach(varName => {
        try { delete window[varName]; } catch(e) {}
        if (document) {
          try { delete document[varName]; } catch(e) {}
        }
      });

      // ============================================================
      // 7. NOTE: We intentionally do NOT use Object.defineProperty to block
      // CDP/automation variables. Using defineProperty would CREATE the
      // property on window, and fingerprint.com's bot detection uses
      // Object.getOwnPropertyNames(window) to find them - even if they
      // return undefined, their mere existence triggers detection.
      // Just deleting them (done above) is sufficient.
      // ============================================================

      // ============================================================
      // 8. REMOVE AUTOMATION CONTROLLER FLAGS
      // ============================================================
      // ============================================================
      // 9. POLLING CLEANUP - Remove any variables that appear later
      // Some automation tools inject variables after page load
      // ============================================================
      const cleanupInterval = setInterval(() => {
        try {
          // Check for CDP pattern variables
          Object.keys(window).forEach(prop => {
            if (cdpRegex.test(prop) || cdpRegex2.test(prop) ||
                cdpPrefixes.some(prefix => prop.startsWith(prefix))) {
              try { delete window[prop]; } catch(e) {}
            }
          });

          // Check for CEF artifacts
          cefArtifacts.forEach(prop => {
            if (window[prop] !== undefined) {
              try { delete window[prop]; } catch(e) {}
            }
          });
        } catch(e) {}
      }, 500);

      // Stop cleanup after 10 seconds to reduce overhead
      setTimeout(() => clearInterval(cleanupInterval), 10000);

      // ============================================================
      // 11. GLOBAL FUNCTION.PROTOTYPE.TOSTRING INTERCEPTION
      // fingerprint.com uses Function.prototype.toString.call(fn) to detect
      // if functions have been patched. We intercept this globally and
      // provide a registration mechanism for all patched functions.
      //
      // This MUST run before any other function patching!
      // ============================================================
      try {
        // Use Symbol to hide the patched functions registry
        const _owl = Symbol.for('owl');
        if (!window[_owl]) {
          Object.defineProperty(window, _owl, {
            value: { fn: new Map() },
            writable: false, enumerable: false, configurable: false
          });
        }
        if (!window[_owl].fn) {
          window[_owl].fn = new Map();
        }
        const _patchedFunctions = window[_owl].fn;

        // Global helper to register a function as "native"
        window[_owl].registerNative = (fn, name) => {
          _patchedFunctions.set(fn, 'function ' + name + '() { [native code] }');
        };

        // Override Function.prototype.toString using Proxy
        // CRITICAL: Proxy inherits from the original, so it has:
        // - No 'prototype' property (native toString doesn't have one)
        // - Correct 'length' (0) and 'name' ('toString') from original
        // - Proper 'this' handling via apply trap
        const _origFnToString = Function.prototype.toString;

        // Store reference to proxy for cycle detection (will be set after creation)
        let _toStringProxyRef = null;

        const _toStringProxy = new Proxy(_origFnToString, {
          apply(target, thisArg, args) {
            // Check Map registry for patched functions
            if (_patchedFunctions.has(thisArg)) {
              return _patchedFunctions.get(thisArg);
            }
            // Use original toString for unpatched functions
            return _origFnToString.call(thisArg);
          },
          // CRITICAL: Detect cyclic prototype assignments
          // CreepJS tests: fn.__proto__ = fn should throw "Cyclic __proto__ value"
          setPrototypeOf(target, proto) {
            // If trying to set prototype to self (the proxy), throw cyclic error
            if (proto === _toStringProxyRef) {
              throw new TypeError('Cyclic __proto__ value');
            }
            // Otherwise forward to the original function
            return Reflect.setPrototypeOf(target, proto);
          }
        });

        // Store reference for cycle detection
        _toStringProxyRef = _toStringProxy;

        // Install the proxy as the new toString
        Function.prototype.toString = _toStringProxy;

        // Register the proxy for self-reference (toString.toString() must return [native code])
        _patchedFunctions.set(_toStringProxy, 'function toString() { [native code] }');

        // Mark as patched so owl_virtual_machine.cc doesn't double-patch
        window[_owl].__toStringPatched = true;

        // ============================================================
        // 11b. ANTI-PROXY DETECTION - Intercept prototype manipulation
        // CreepJS detects Proxies using these tests:
        // 1. Object.create(fn).toString() - behaves differently for Proxies
        // 2. Object.setPrototypeOf(fn, Object.create(fn)).toString() - circular prototype
        // 3. Reflect.setPrototypeOf(fn, Object.create(fn)) - reflection test
        //
        // Solution: Track Proxy -> original mappings and intercept these methods
        // to use the original native function for prototype operations
        // ============================================================

        // Map to track Proxy -> original native function
        if (!window[_owl].originals) {
          window[_owl].originals = new Map();
        }
        const _proxyOriginals = window[_owl].originals;

        // Register the toString proxy's original
        _proxyOriginals.set(_toStringProxy, _origFnToString);

        // Helper to get original if it's a tracked proxy, otherwise return as-is
        // CRITICAL: Follow the chain recursively in case of double/triple patching
        // NOTE: Must check BOTH registries:
        // - _proxyOriginals: local Map for proxies created in owl_stealth.cc
        // - window[_owl]._proxyOriginals: WeakMap from spoof_utils.cc (createNativeProxy)
        const getOriginal = (fn) => {
          let current = fn;
          let depth = 0;
          const spoofUtilsRegistry = window[_owl]?._proxyOriginals;
          while (depth < 10) {
            if (_proxyOriginals.has(current)) {
              current = _proxyOriginals.get(current);
            } else if (spoofUtilsRegistry?.has(current)) {
              current = spoofUtilsRegistry.get(current);
            } else {
              break;
            }
            depth++;
          }
          return current;
        };

        // 1. Intercept Object.create
        const _origObjectCreate = Object.create;
        Object.create = function create(proto, propertiesObject) {
          // Use original function for prototype if it's a tracked proxy
          const actualProto = getOriginal(proto);
          return _origObjectCreate.call(Object, actualProto, propertiesObject);
        };
        _patchedFunctions.set(Object.create, 'function create() { [native code] }');
        Object.defineProperty(Object.create, 'name', { value: 'create', writable: false, configurable: true });
        Object.defineProperty(Object.create, 'length', { value: 2, writable: false, configurable: true });

        // 2. Intercept Object.setPrototypeOf
        const _origSetProto = Object.setPrototypeOf;
        Object.setPrototypeOf = function setPrototypeOf(obj, proto) {
          // Use originals for both arguments if they're tracked proxies
          const actualObj = getOriginal(obj);
          const actualProto = getOriginal(proto);
          return _origSetProto.call(Object, actualObj, actualProto);
        };
        _patchedFunctions.set(Object.setPrototypeOf, 'function setPrototypeOf() { [native code] }');
        Object.defineProperty(Object.setPrototypeOf, 'name', { value: 'setPrototypeOf', writable: false, configurable: true });
        Object.defineProperty(Object.setPrototypeOf, 'length', { value: 2, writable: false, configurable: true });

        // 3. Intercept Reflect.setPrototypeOf
        if (typeof Reflect !== 'undefined' && Reflect.setPrototypeOf) {
          const _origReflectSetProto = Reflect.setPrototypeOf;
          Reflect.setPrototypeOf = function setPrototypeOf(target, proto) {
            // Use originals if they're tracked proxies
            const actualTarget = getOriginal(target);
            const actualProto = getOriginal(proto);
            return _origReflectSetProto.call(Reflect, actualTarget, actualProto);
          };
          _patchedFunctions.set(Reflect.setPrototypeOf, 'function setPrototypeOf() { [native code] }');
        }

        // Export helper for other patching code to register their proxies
        window[_owl].registerProxy = (proxy, original) => {
          _proxyOriginals.set(proxy, original);
        };

        // ============================================================
        // CRITICAL: createNativeProxy helper for all function patching
        // Creates a Proxy that looks exactly like a native function:
        // - 'prototype' in proxy → false
        // - Object.getOwnPropertyNames(proxy) → ['length', 'name']
        // - Reflect.ownKeys(proxy) → ['length', 'name']
        // - class X extends proxy {} → same TypeError as native
        // - Function.prototype.toString.call(proxy) → '[native code]'
        // NOTE: This is a fallback - SpoofManager injects spoof_utils.cc first
        // which defines createNativeProxy. This only runs if that didn't happen.
        // ============================================================
        if (!window[_owl].createNativeProxy) {
        window[_owl].createNativeProxy = (original, applyHandler, nameOverride) => {
          // Create a clean wrapper function without 'prototype' property
          // Using arrow function as base - arrow functions don't have 'prototype'
          // Then wrap in Proxy for full control
          const wrapper = (() => {}).bind();
          const effectiveName = nameOverride || original.name;

          const proxy = new Proxy(wrapper, {
            apply(target, thisArg, args) {
              // CRITICAL: Pass 'original' to handler, not 'target' (which is empty wrapper)
              return applyHandler(original, thisArg, args);
            },
            // NO construct trap! Native methods don't have [[Construct]].
            // The wrapper `(() => {}).bind()` also has no [[Construct]], so:
            // - `new proxy()` throws "proxy is not a constructor"
            // - `class X extends proxy {}` throws "Class extends value is not a constructor or null"
            // Both match native non-constructor function behavior.

            // Hide 'prototype' property - native functions don't have it
            has(target, prop) {
              if (prop === 'prototype') return false;
              if (prop === 'length' || prop === 'name') return true;
              return Reflect.has(original, prop);
            },
            // Only expose 'length' and 'name' as own properties
            ownKeys(target) {
              return ['length', 'name'];
            },
            get(target, prop, receiver) {
              if (prop === 'prototype') return undefined;
              if (prop === 'length') return original.length;
              if (prop === 'name') return effectiveName;
              // Return original's prototype chain for proper inheritance
              if (prop === '__proto__' || prop === 'constructor') {
                return Reflect.get(original, prop, original);
              }
              return Reflect.get(original, prop, receiver);
            },
            set(target, prop, value, receiver) {
              // Native functions allow adding properties
              if (prop === 'prototype' || prop === 'length' || prop === 'name') {
                return false; // Can't set these on native methods
              }
              return Reflect.set(original, prop, value, original);
            },
            getOwnPropertyDescriptor(target, prop) {
              if (prop === 'prototype') return undefined;
              if (prop === 'length') {
                return { value: original.length, writable: false, enumerable: false, configurable: true };
              }
              if (prop === 'name') {
                return { value: effectiveName, writable: false, enumerable: false, configurable: true };
              }
              return undefined;
            },
            defineProperty(target, prop, descriptor) {
              // Native functions are extensible - allow defining new properties
              if (prop === 'prototype' || prop === 'length' || prop === 'name') {
                return false; // Can't redefine these
              }
              return Reflect.defineProperty(original, prop, descriptor);
            },
            deleteProperty(target, prop) {
              // Can't delete length/name, but can delete other added properties
              if (prop === 'length' || prop === 'name') return false;
              return Reflect.deleteProperty(original, prop);
            },
            getPrototypeOf(target) {
              // Return Function.prototype for proper prototype chain
              return Function.prototype;
            },
            setPrototypeOf(target, proto) {
              // Allow changing prototype like native functions
              return Reflect.setPrototypeOf(original, proto);
            },
            isExtensible(target) {
              // Native functions are extensible
              return true;
            },
            preventExtensions(target) {
              // Allow freezing
              return Reflect.preventExtensions(original);
            }
          });
          // Register for toString
          _patchedFunctions.set(proxy, 'function ' + effectiveName + '() { [native code] }');
          // Register proxy -> original mapping for prototype detection bypass
          _proxyOriginals.set(proxy, original);
          return proxy;
        };
        } // end if (!window[_owl].createNativeProxy)

      } catch(e) {}

      // ============================================================
      // 12. HIDE CEF INTERNAL BINDINGS FROM Object.getOwnPropertyNames
      // Our CEF V8 bindings (_0, _1, _2) are non-enumerable but still
      // visible via Object.getOwnPropertyNames. We must intercept this
      // to filter them out when scanning window object.
      // ============================================================
      try {
        const _owl = Symbol.for('owl');
        const registerNative = window[_owl].registerNative;

        const _origGetOwnPropNames = Object.getOwnPropertyNames;
        const hiddenBindings = new Set(['_0', '_1', '_2', 'owl', '__vmSpeechSynthesisSpoofed']);

        Object.getOwnPropertyNames = function getOwnPropertyNames(obj) {
          const result = _origGetOwnPropNames.call(Object, obj);
          // Only filter when scanning window or globalThis
          if (obj === window || obj === globalThis) {
            return result.filter(name => !hiddenBindings.has(name));
          }
          return result;
        };

        // Register as native so Function.prototype.toString.call() returns [native code]
        registerNative(Object.getOwnPropertyNames, 'getOwnPropertyNames');

        Object.defineProperty(Object.getOwnPropertyNames, 'name', {
          value: 'getOwnPropertyNames',
          writable: false,
          configurable: true
        });
        Object.defineProperty(Object.getOwnPropertyNames, 'length', {
          value: 1,
          writable: false,
          configurable: true
        });

      } catch(e) {}

      // ============================================================
      // 13. HIDE OWL SYMBOLS FROM Object.getOwnPropertySymbols
      // Our internal Symbol(owl) and Symbol(__vmSyms__) are visible via
      // Object.getOwnPropertySymbols(window). We must filter them out.
      // ============================================================
      try {
        const _owl = Symbol.for('owl');
        const registerNative = window[_owl].registerNative;

        const _origGetOwnPropSymbols = Object.getOwnPropertySymbols;
        const hiddenSymbols = new Set([
          Symbol.for('owl'),
          Symbol.for('__vmSyms__'),
          Symbol.for('__native__'),
          Symbol.for('__patched__')
        ]);

        Object.getOwnPropertySymbols = function getOwnPropertySymbols(obj) {
          const result = _origGetOwnPropSymbols.call(Object, obj);
          // Only filter when scanning window or globalThis
          if (obj === window || obj === globalThis) {
            return result.filter(sym => !hiddenSymbols.has(sym));
          }
          return result;
        };

        // Register as native so Function.prototype.toString.call() returns [native code]
        registerNative(Object.getOwnPropertySymbols, 'getOwnPropertySymbols');

        Object.defineProperty(Object.getOwnPropertySymbols, 'name', {
          value: 'getOwnPropertySymbols',
          writable: false,
          configurable: true
        });
        Object.defineProperty(Object.getOwnPropertySymbols, 'length', {
          value: 1,
          writable: false,
          configurable: true
        });

      } catch(e) {}

    })();
  )";

  frame->ExecuteJavaScript(script, frame->GetURL(), 0);
}

void OwlStealth::BlockMacOSFonts(CefRefPtr<CefFrame> frame) {
  // NOTE: This function is now a no-op for early injection.
  // macOS font blocking is now handled conditionally by BlockMacOSFontsConditional
  // which is called from InjectHardwareSimulation based on the target OS.
  // When spoofing to macOS, we DON'T block macOS fonts.
  // When spoofing to Windows/Linux, we DO block macOS fonts.
  (void)frame;  // Suppress unused parameter warning
}

void OwlStealth::BlockMacOSFontsConditional(CefRefPtr<CefFrame> frame, bool block) {
  // NOTE: Font blocking is now handled by GenerateFontsScript() in owl_virtual_machine.cc
  // That function provides OS-aware font allowlists (Windows fonts for Windows profile, etc.)
  // This function is kept as a no-op to avoid duplicate/conflicting hooks.
  // DO NOT add font blocking code here - it will conflict with VirtualMachine injection.
  (void)frame;
  (void)block;
}

void OwlStealth::BlockWebRTC(CefRefPtr<CefFrame> frame) {
  std::string script = R"(
    // BLOCK WEBRTC TO PREVENT IP LEAKS
    // WebRTC can expose your real IP address even when using VPN/proxy
    // This is a major privacy/VPN detection vector

    (function() {
      'use strict';

      const DEBUG = false;
      const log = (...args) => DEBUG && console.log('[WEBRTC BLOCK]', ...args);

      // Block RTCPeerConnection (main WebRTC API)
      // FIXED: Use try/catch and Object.defineProperty to handle non-writable properties
      const blockProperty = (obj, prop) => {
        try {
          // First try direct assignment
          const desc = Object.getOwnPropertyDescriptor(obj, prop);
          if (desc && !desc.writable && !desc.configurable) {
            // Property is locked, skip it (it's already our stub)
            log('Property ' + prop + ' is locked, skipping');
            return;
          }
          if (desc && desc.configurable) {
            Object.defineProperty(obj, prop, { value: undefined, writable: true, configurable: true });
          } else {
            obj[prop] = undefined;
          }
          log('Blocked ' + prop);
        } catch (e) {
          log('Could not block ' + prop + ': ' + e.message);
        }
      };

      blockProperty(window, 'RTCPeerConnection');
      blockProperty(window, 'webkitRTCPeerConnection');
      blockProperty(window, 'mozRTCPeerConnection');

      // Block related WebRTC APIs
      blockProperty(window, 'RTCSessionDescription');
      blockProperty(window, 'RTCIceCandidate');

      if (window.MediaStreamTrack) {
        // Keep MediaStreamTrack for getUserMedia to work, but block IP-related methods
        const original = window.MediaStreamTrack;
        window.MediaStreamTrack = function() {
          return original.apply(this, arguments);
        };
      }

      // Block getUserMedia IP enumeration (used by WebRTC)
      if (navigator.mediaDevices && navigator.mediaDevices.enumerateDevices) {
        const originalEnumerate = navigator.mediaDevices.enumerateDevices;
        navigator.mediaDevices.enumerateDevices = function() {
          log('Blocking enumerateDevices to prevent device fingerprinting');
          return Promise.resolve([]);
        };
      }

      log('✅ WebRTC blocked - no IP leaks');
    })();
  )";

  frame->ExecuteJavaScript(script, frame->GetURL(), 0);
}

void OwlStealth::BlockGeolocation(CefRefPtr<CefFrame> frame) {
  std::string script = R"(
    // BLOCK GEOLOCATION API
    // Prevents location tracking and timezone-based VPN detection

    (function() {
      'use strict';

      const DEBUG = false;
      const log = (...args) => DEBUG && console.log('[GEOLOCATION BLOCK]', ...args);

      // Native function masking utility - uses global registry only (NO direct toString!)
      // Native functions inherit toString from Function.prototype - they don't have own toString
      const maskAsNative = (fn, name) => {
        const nativeStr = 'function ' + name + '() { [native code] }';
        try {
          Object.defineProperty(fn, 'name', { value: name, writable: false, enumerable: false, configurable: true });
          Object.defineProperty(fn, 'length', { value: 0, writable: false, enumerable: false, configurable: true });
          // Make prototype configurable first, then delete (native functions don't have prototype)
          try { Object.defineProperty(fn, 'prototype', { configurable: true }); } catch(e) {}
          delete fn.prototype;
          // CRITICAL: Do NOT set direct toString property - register with global registry only!
          const _owl = Symbol.for('owl');
          if (window[_owl] && window[_owl].fn) {
            window[_owl].fn.set(fn, nativeStr);
          }
        } catch(e) {}
        return fn;
      };

      // Block navigator.geolocation
      if (navigator.geolocation) {
        // Override getCurrentPosition to always fail
        const getCurrentPositionFn = function getCurrentPosition(success, error) {
          if (error) {
            error({
              code: 1, // PERMISSION_DENIED
              message: 'User denied geolocation'
            });
          }
        };
        maskAsNative(getCurrentPositionFn, 'getCurrentPosition');
        navigator.geolocation.getCurrentPosition = getCurrentPositionFn;

        // Override watchPosition to always fail
        const watchPositionFn = function watchPosition(success, error) {
          if (error) {
            error({
              code: 1, // PERMISSION_DENIED
              message: 'User denied geolocation'
            });
          }
          return 0;
        };
        maskAsNative(watchPositionFn, 'watchPosition');
        navigator.geolocation.watchPosition = watchPositionFn;

        log('✅ Geolocation blocked - location requests will fail');
      }

      // Also handle permissions query for geolocation using Proxy
      // Use 'prompt' (not-yet-asked) instead of 'denied' to avoid bot signature
      if (navigator.permissions && navigator.permissions.query) {
        const permProto = Object.getPrototypeOf(navigator.permissions);
        const originalQuery = permProto.query;

        // Create Proxy-based query that inherits native properties
        const proxyQuery = new Proxy(originalQuery, {
          apply(target, thisArg, args) {
            const permissionDesc = args[0];
            // CRITICAL: Validate permissionDesc - native throws SYNCHRONOUSLY
            if (permissionDesc === undefined || permissionDesc === null) {
              throw new TypeError("Failed to execute 'query' on 'Permissions': 1 argument required, but only 0 present.");
            }
            if (typeof permissionDesc !== 'object') {
              throw new TypeError("Failed to execute 'query' on 'Permissions': parameter 1 is not of type 'object'.");
            }
            if (permissionDesc.name === 'geolocation') {
              return Promise.resolve({
                state: 'prompt',
                name: 'geolocation',
                onchange: null,
                addEventListener: function() {},
                removeEventListener: function() {},
                dispatchEvent: function() { return true; }
              });
            }
            return originalQuery.call(thisArg, permissionDesc);
          }
        });

        // Register proxy for toString AND anti-proxy detection
        const _owl_perm = Symbol.for('owl');
        if (window[_owl_perm]) {
          if (window[_owl_perm].fn) {
            window[_owl_perm].fn.set(proxyQuery, 'function query() { [native code] }');
          }
          // Register for anti-proxy detection
          if (window[_owl_perm].registerProxy) {
            window[_owl_perm].registerProxy(proxyQuery, originalQuery);
          }
        }

        Object.defineProperty(permProto, 'query', {
          value: proxyQuery,
          writable: true,
          enumerable: true,
          configurable: true
        });
      }
    })();
  )";

  frame->ExecuteJavaScript(script, frame->GetURL(), 0);
}

void OwlStealth::BlockWebGPU(CefRefPtr<CefFrame> frame) {
  // NOTE: This function is now a no-op.
  // WebGPU (navigator.gpu) spoofing is now handled by VirtualMachineInjector
  // in owl_virtual_machine.cc with proper Symbol.toStringTag support.
  // Having duplicate GPU stub implementations caused "failed object toString error"
  // and "failed at too much recursion error" detection failures from fingerprint.com.
  // The VirtualMachine implementation includes:
  // - Proper Symbol.toStringTag getter for [object GPU] toString
  // - Recursion guards in Proxy traps
  // - Consistent GPU stub across main frame, iframes, and workers
  (void)frame;  // Suppress unused parameter warning
}

void OwlStealth::BlockClientHints(CefRefPtr<CefFrame> frame) {
  // NOTE: This function is now a no-op.
  // User-Agent Client Hints (navigator.userAgentData) are now properly spoofed
  // by VirtualMachineInjector::GenerateNavigatorScript() with the correct
  // platform from the VM profile. Setting it to undefined was causing
  // "Different operating systems" detection on browserscan.net.
  (void)frame;  // Suppress unused parameter warning
}

std::string OwlStealth::GenerateSessionNoise() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<> dis(0.0001, 0.0005);

  return std::to_string(dis(gen));
}

void OwlStealth::SpoofTimezone(CefRefPtr<CefFrame> frame, const std::string& timezone) {
  if (timezone.empty()) {
    return;
  }

  // Inject JavaScript to spoof the timezone
  // This overrides:
  // 1. Intl.DateTimeFormat().resolvedOptions().timeZone
  // 2. Date.prototype.getTimezoneOffset()
  // 3. new Date().toTimeString() which includes timezone name
  // 4. Temporal API if available
  std::stringstream script_stream;
  script_stream << R"JS(
    // TIMEZONE SPOOFING
    // Spoof timezone to match proxy location for stealth
    // This prevents timezone-based VPN/proxy detection

    (function() {
      'use strict';

      const DEBUG = false;
      const log = (...args) => DEBUG && console.log('[TIMEZONE SPOOF]', ...args);

      // Helper to make patched methods look native using Proxy
      // CRITICAL: Wraps the ORIGINAL native method in a Proxy, so it inherits:
      // - No prototype property (native methods don't have it)
      // - Correct name and length from original
      // - Native-like property descriptors
      const proxyAsNative = (originalMethod, handler, name) => {
        const nativeStr = 'function ' + name + '() { [native code] }';
        // Store reference for cycle detection (will be set after creation)
        let proxyRef = null;
        const proxy = new Proxy(originalMethod, {
          apply: handler,
          // CRITICAL: Detect cyclic prototype assignments
          // CreepJS tests: fn.__proto__ = fn should throw "Cyclic __proto__ value"
          setPrototypeOf(target, proto) {
            if (proto === proxyRef) {
              throw new TypeError('Cyclic __proto__ value');
            }
            return Reflect.setPrototypeOf(target, proto);
          }
        });
        proxyRef = proxy;
        // Register proxy for toString interception AND anti-proxy detection
        const _owl = Symbol.for('owl');
        if (window[_owl]) {
          if (window[_owl].fn) {
            window[_owl].fn.set(proxy, nativeStr);
          }
          // Register proxy -> original mapping for anti-proxy detection
          if (window[_owl].registerProxy) {
            window[_owl].registerProxy(proxy, originalMethod);
          }
        }
        return proxy;
      };

      // Helper to mask a function as native (for constructors/non-method functions)
      const maskAsNative = (fn, name, length = 0) => {
        const nativeStr = 'function ' + name + '() { [native code] }';
        try {
          Object.defineProperty(fn, 'name', { value: name, writable: false, enumerable: false, configurable: true });
          Object.defineProperty(fn, 'length', { value: length, writable: false, enumerable: false, configurable: true });
          try { Object.defineProperty(fn, 'prototype', { configurable: true }); } catch(e) {}
          delete fn.prototype;
          const _owl = Symbol.for('owl');
          if (window[_owl] && window[_owl].fn) {
            window[_owl].fn.set(fn, nativeStr);
          }
        } catch(e) {}
        return fn;
      };

      const targetTimezone = ')JS" << timezone << R"JS(';
      log('🌍 Spoofing timezone to:', targetTimezone);

      // Calculate the timezone offset in minutes
      // We need to get the offset for the target timezone
      // Create a date formatter for the target timezone to get the offset
      const getTimezoneOffset = (tz) => {
        try {
          const now = new Date();
          // Get UTC time components
          const utcTime = now.getTime() + (now.getTimezoneOffset() * 60000);

          // Format as ISO string in target timezone to get the offset
          const formatter = new Intl.DateTimeFormat('en-US', {
            timeZone: tz,
            timeZoneName: 'longOffset'
          });
          const parts = formatter.formatToParts(now);
          const tzPart = parts.find(p => p.type === 'timeZoneName');

          if (tzPart && tzPart.value) {
            // Parse offset like "GMT+05:30" or "GMT-08:00"
            const match = tzPart.value.match(/GMT([+-])(\d{1,2}):?(\d{2})?/);
            if (match) {
              const sign = match[1] === '+' ? -1 : 1;  // Reversed because getTimezoneOffset returns inverse
              const hours = parseInt(match[2], 10);
              const minutes = parseInt(match[3] || '0', 10);
              return sign * (hours * 60 + minutes);
            }
          }

          // Fallback: calculate from actual date difference
          const targetFormatter = new Intl.DateTimeFormat('en-US', {
            timeZone: tz,
            year: 'numeric', month: '2-digit', day: '2-digit',
            hour: '2-digit', minute: '2-digit', second: '2-digit',
            hour12: false
          });
          const targetParts = targetFormatter.formatToParts(now);
          const targetDate = new Date(
            parseInt(targetParts.find(p => p.type === 'year').value),
            parseInt(targetParts.find(p => p.type === 'month').value) - 1,
            parseInt(targetParts.find(p => p.type === 'day').value),
            parseInt(targetParts.find(p => p.type === 'hour').value),
            parseInt(targetParts.find(p => p.type === 'minute').value),
            parseInt(targetParts.find(p => p.type === 'second').value)
          );

          // The difference gives us the offset
          const utcParts = new Intl.DateTimeFormat('en-US', {
            timeZone: 'UTC',
            year: 'numeric', month: '2-digit', day: '2-digit',
            hour: '2-digit', minute: '2-digit', second: '2-digit',
            hour12: false
          }).formatToParts(now);
          const utcDate = new Date(
            parseInt(utcParts.find(p => p.type === 'year').value),
            parseInt(utcParts.find(p => p.type === 'month').value) - 1,
            parseInt(utcParts.find(p => p.type === 'day').value),
            parseInt(utcParts.find(p => p.type === 'hour').value),
            parseInt(utcParts.find(p => p.type === 'minute').value),
            parseInt(utcParts.find(p => p.type === 'second').value)
          );

          return Math.round((utcDate - targetDate) / 60000);
        } catch (e) {
          log('⚠️ Error calculating offset for', tz, ':', e.message);
          return 0;  // UTC fallback
        }
      };

      const spoofedOffset = getTimezoneOffset(targetTimezone);
      log('📐 Calculated offset:', spoofedOffset, 'minutes');

      // 1. Override Date.prototype.getTimezoneOffset using Proxy
      const originalGetTimezoneOffset = Date.prototype.getTimezoneOffset;
      Date.prototype.getTimezoneOffset = proxyAsNative(
        originalGetTimezoneOffset,
        (target, thisArg, args) => spoofedOffset,
        'getTimezoneOffset'
      );

      log('✅ Date.prototype.getTimezoneOffset patched');

      // 2. Override Intl.DateTimeFormat to always use target timezone
      const originalDateTimeFormat = Intl.DateTimeFormat;
      const originalResolvedOptions = Intl.DateTimeFormat.prototype.resolvedOptions;

      // Override resolvedOptions to return spoofed timezone using Proxy
      Intl.DateTimeFormat.prototype.resolvedOptions = proxyAsNative(
        originalResolvedOptions,
        (target, thisArg, args) => {
          const options = originalResolvedOptions.call(thisArg);
          options.timeZone = targetTimezone;
          return options;
        },
        'resolvedOptions'
      );

      log('✅ Intl.DateTimeFormat.resolvedOptions patched');

      // 3. Override the DateTimeFormat constructor to inject timezone
      const DateTimeFormatWrapper = function DateTimeFormat(locales, options) {
        // If no options or no timezone specified, inject our timezone
        const newOptions = options ? { ...options } : {};
        if (!newOptions.timeZone) {
          newOptions.timeZone = targetTimezone;
        }
        return new originalDateTimeFormat(locales, newOptions);
      };

      // Copy static methods and properties
      Object.setPrototypeOf(DateTimeFormatWrapper, originalDateTimeFormat);
      DateTimeFormatWrapper.prototype = originalDateTimeFormat.prototype;
      DateTimeFormatWrapper.supportedLocalesOf = originalDateTimeFormat.supportedLocalesOf;

      // Make constructor look native using maskAsNative
      Intl.DateTimeFormat = maskAsNative(DateTimeFormatWrapper, 'DateTimeFormat', 0);

      log('✅ Intl.DateTimeFormat constructor patched');

      // 4. Override toTimeString() to show correct timezone name using Proxy
      const originalToTimeString = Date.prototype.toTimeString;
      Date.prototype.toTimeString = proxyAsNative(
        originalToTimeString,
        (target, thisArg, args) => {
          const formatter = new originalDateTimeFormat('en-US', {
            timeZone: targetTimezone,
            hour: '2-digit',
            minute: '2-digit',
            second: '2-digit',
            hour12: false,
            timeZoneName: 'longOffset'
          });
          const parts = formatter.formatToParts(thisArg);
          const time = parts.filter(p => ['hour', 'minute', 'second'].includes(p.type))
                           .map(p => p.value).join(':');
          const tzPart = parts.find(p => p.type === 'timeZoneName');
          const tzName = tzPart ? tzPart.value : '';

          // Format: "14:30:00 GMT+0530 (India Standard Time)"
          const longFormatter = new originalDateTimeFormat('en-US', {
            timeZone: targetTimezone,
            timeZoneName: 'long'
          });
          const longParts = longFormatter.formatToParts(thisArg);
          const longTzPart = longParts.find(p => p.type === 'timeZoneName');
          const longTzName = longTzPart ? longTzPart.value : targetTimezone;

          const shortTz = tzName.replace(':', '');
          return time + ' ' + shortTz + ' (' + longTzName + ')';
        },
        'toTimeString'
      );

      log('✅ Date.prototype.toTimeString patched');

      // 5. Override toLocaleString and related methods using Proxy
      const dateMethods = ['toLocaleString', 'toLocaleDateString', 'toLocaleTimeString'];
      dateMethods.forEach(method => {
        const original = Date.prototype[method];
        Date.prototype[method] = proxyAsNative(
          original,
          (target, thisArg, args) => {
            const [locales, options] = args;
            const newOptions = options ? { ...options } : {};
            if (!newOptions.timeZone) {
              newOptions.timeZone = targetTimezone;
            }
            return original.call(thisArg, locales, newOptions);
          },
          method
        );
      });

      log('✅ Date locale methods patched');

      // 6. Override toString to use correct timezone using Proxy
      const originalToString = Date.prototype.toString;
      Date.prototype.toString = proxyAsNative(
        originalToString,
        (target, thisArg, args) => {
          try {
            // Get day name
            const dayFormatter = new originalDateTimeFormat('en-US', {
              timeZone: targetTimezone,
              weekday: 'short'
            });
            // Get month name
            const monthFormatter = new originalDateTimeFormat('en-US', {
              timeZone: targetTimezone,
              month: 'short'
            });
            // Get date parts
            const dateFormatter = new originalDateTimeFormat('en-US', {
              timeZone: targetTimezone,
              day: '2-digit',
              year: 'numeric',
              hour: '2-digit',
              minute: '2-digit',
              second: '2-digit',
              hour12: false,
              timeZoneName: 'longOffset'
            });

            const dayParts = dayFormatter.formatToParts(thisArg);
            const monthParts = monthFormatter.formatToParts(thisArg);
            const dateParts = dateFormatter.formatToParts(thisArg);

            const day = dayParts.find(p => p.type === 'weekday').value;
            const month = monthParts.find(p => p.type === 'month').value;
            const date = dateParts.find(p => p.type === 'day').value;
            const year = dateParts.find(p => p.type === 'year').value;
            const hour = dateParts.find(p => p.type === 'hour').value;
            const minute = dateParts.find(p => p.type === 'minute').value;
            const second = dateParts.find(p => p.type === 'second').value;
            const tzPart = dateParts.find(p => p.type === 'timeZoneName');
            const tz = tzPart ? tzPart.value.replace(':', '') : '';

            // Get long timezone name
            const longFormatter = new originalDateTimeFormat('en-US', {
              timeZone: targetTimezone,
              timeZoneName: 'long'
            });
            const longParts = longFormatter.formatToParts(thisArg);
            const longTzPart = longParts.find(p => p.type === 'timeZoneName');
            const longTzName = longTzPart ? longTzPart.value : targetTimezone;

            // Format: "Sat Nov 09 2024 14:30:00 GMT+0530 (India Standard Time)"
            return day + ' ' + month + ' ' + date + ' ' + year + ' ' + hour + ':' + minute + ':' + second + ' ' + tz + ' (' + longTzName + ')';
          } catch (e) {
            return originalToString.call(thisArg);
          }
        },
        'toString'
      );

      log('✅ Date.prototype.toString patched');

      // Final verification
      log('📊 Timezone spoofing complete:');
      log('  - Target timezone:', targetTimezone);
      log('  - getTimezoneOffset():', new Date().getTimezoneOffset(), 'minutes');
      log('  - Intl timezone:', Intl.DateTimeFormat().resolvedOptions().timeZone);
      log('  - Date string:', new Date().toString());
    })();
  )JS";

  std::string script = script_stream.str();
  frame->ExecuteJavaScript(script, frame->GetURL(), 0);
}

void OwlStealth::InjectVirtualCamera(CefRefPtr<CefFrame> frame) {
  // JavaScript to override navigator.mediaDevices for virtual camera support
  // This makes websites detect our virtual camera as a real webcam
  std::string script = R"JS(
(function() {
  'use strict';

  const DEBUG = false;
  const log = (...args) => DEBUG && console.log('[VirtualCamera]', ...args);

  // Store original functions before overriding
  if (!navigator.mediaDevices) {
    log('No mediaDevices API available');
    return;
  }

  const originalEnumerateDevices = navigator.mediaDevices.enumerateDevices.bind(navigator.mediaDevices);
  const originalGetUserMedia = navigator.mediaDevices.getUserMedia.bind(navigator.mediaDevices);

  // Virtual camera device info - looks like a real USB webcam
  // CRITICAL: Labels must be EMPTY until permission is granted!
  // Exposing labels without permission is impossible in real browsers
  // and is a dead giveaway for automation detection.
  const virtualCameraDevice = {
    deviceId: 'default',
    groupId: 'webcam-group-001',
    kind: 'videoinput',
    label: ''  // CRITICAL: Empty until permission granted
  };

  // Virtual microphone device info
  // CRITICAL: Same rule - labels empty until permission granted
  const virtualMicDevice = {
    deviceId: 'default',
    groupId: 'audio-group-001',
    kind: 'audioinput',
    label: ''  // CRITICAL: Empty until permission granted
  };

  // Override enumerateDevices with privacy-respecting implementation
  // CRITICAL: In real Chrome, device labels are EMPTY until permission granted!
  // Also limit device count to match typical real browser (3 devices typical)
  navigator.mediaDevices.enumerateDevices = async function() {
    log('enumerateDevices called');
    try {
      const realDevices = await originalEnumerateDevices();
      log('Real devices:', realDevices.length);

      // CRITICAL: Strip all labels from real devices (no permission granted)
      // Real browsers only show labels after getUserMedia permission
      const sanitizedDevices = realDevices.map(device => ({
        deviceId: device.deviceId,
        groupId: device.groupId,
        kind: device.kind,
        label: '',  // ALWAYS empty without permission
        toJSON: function() {
          return {
            deviceId: this.deviceId,
            groupId: this.groupId,
            kind: this.kind,
            label: this.label
          };
        }
      }));

      // Check if we have basic device types
      const hasVideoInput = sanitizedDevices.some(d => d.kind === 'videoinput');
      const hasAudioInput = sanitizedDevices.some(d => d.kind === 'audioinput');
      const hasAudioOutput = sanitizedDevices.some(d => d.kind === 'audiooutput');

      const devices = [...sanitizedDevices];

      // Add basic virtual devices if missing (max 3 total to match real Chrome)
      if (!hasVideoInput && devices.length < 3) {
        log('Adding virtual camera');
        devices.push({
          deviceId: virtualCameraDevice.deviceId,
          groupId: virtualCameraDevice.groupId,
          kind: virtualCameraDevice.kind,
          label: '',  // CRITICAL: Empty
          toJSON: function() {
            return {
              deviceId: this.deviceId,
              groupId: this.groupId,
              kind: this.kind,
              label: this.label
            };
          }
        });
      }

      if (!hasAudioInput && devices.length < 3) {
        log('Adding virtual microphone');
        devices.push({
          deviceId: virtualMicDevice.deviceId,
          groupId: virtualMicDevice.groupId,
          kind: virtualMicDevice.kind,
          label: '',  // CRITICAL: Empty
          toJSON: function() {
            return {
              deviceId: this.deviceId,
              groupId: this.groupId,
              kind: this.kind,
              label: this.label
            };
          }
        });
      }

      // Limit to 3 devices max (typical real Chrome without permission)
      return devices.slice(0, 3);
    } catch (e) {
      log('enumerateDevices error:', e);
      // Return minimal virtual devices if enumeration fails
      // CRITICAL: All labels empty
      return [
        {
          deviceId: virtualCameraDevice.deviceId,
          groupId: virtualCameraDevice.groupId,
          kind: virtualCameraDevice.kind,
          label: '',
          toJSON: function() { return this; }
        },
        {
          deviceId: virtualMicDevice.deviceId,
          groupId: virtualMicDevice.groupId,
          kind: virtualMicDevice.kind,
          label: '',
          toJSON: function() { return this; }
        }
      ];
    }
  };

  // Make it look native - use global registry (NO direct toString property!)
  const _owl_ed = Symbol.for('owl');
  if (window[_owl_ed] && window[_owl_ed].fn) {
    window[_owl_ed].fn.set(navigator.mediaDevices.enumerateDevices, 'function enumerateDevices() { [native code] }');
  }

  // Create fake video track from canvas
  function createFakeVideoTrack(width, height, fps) {
    width = width || 640;
    height = height || 480;
    fps = fps || 30;

    const canvas = document.createElement('canvas');
    canvas.width = width;
    canvas.height = height;
    const ctx = canvas.getContext('2d');

    // Draw a realistic "no camera" placeholder
    ctx.fillStyle = '#1a1a2e';
    ctx.fillRect(0, 0, width, height);

    // Draw a camera icon
    ctx.fillStyle = '#4a4a6a';
    const iconSize = Math.min(width, height) * 0.3;
    const iconX = (width - iconSize) / 2;
    const iconY = (height - iconSize) / 2 - 20;

    // Camera body
    ctx.fillRect(iconX, iconY + iconSize * 0.2, iconSize, iconSize * 0.6);
    // Camera lens
    ctx.beginPath();
    ctx.arc(iconX + iconSize / 2, iconY + iconSize / 2, iconSize * 0.2, 0, Math.PI * 2);
    ctx.fill();
    // Flash
    ctx.fillRect(iconX + iconSize * 0.7, iconY, iconSize * 0.15, iconSize * 0.15);

    // Text
    ctx.fillStyle = '#8a8aaa';
    ctx.font = '16px Arial';
    ctx.textAlign = 'center';
    ctx.fillText('Virtual Camera Ready', width / 2, height - 40);

    // Get stream from canvas
    const stream = canvas.captureStream(fps);
    const track = stream.getVideoTracks()[0];

    // Store canvas reference for frame injection (short names to avoid detection)
    track._vCanvas = canvas;
    track._vCtx = ctx;

    log('Created fake video track:', width, 'x', height, '@', fps, 'fps');
    return track;
  }

  // Create fake audio track (silence)
  function createFakeAudioTrack() {
    const audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    const oscillator = audioCtx.createOscillator();
    const dest = audioCtx.createMediaStreamDestination();
    const gainNode = audioCtx.createGain();

    oscillator.connect(gainNode);
    gainNode.connect(dest);
    gainNode.gain.value = 0; // Silent

    oscillator.type = 'sine';
    oscillator.frequency.value = 0;
    oscillator.start();

    log('Created fake audio track (silent)');
    return dest.stream.getAudioTracks()[0];
  }

  // Override getUserMedia - ALWAYS use our controllable fake stream for video
  // This allows us to inject custom frames (background, gestures) from C++
  navigator.mediaDevices.getUserMedia = async function(constraints) {
    log('getUserMedia called with:', JSON.stringify(constraints));

    // Always create our own controllable fake stream for video
    // This allows frame injection from C++ side
    const tracks = [];

    if (constraints.video) {
      let width = 640, height = 480, fps = 30;

      if (typeof constraints.video === 'object') {
        width = constraints.video.width?.ideal || constraints.video.width?.exact || constraints.video.width || 640;
        height = constraints.video.height?.ideal || constraints.video.height?.exact || constraints.video.height || 480;
        fps = constraints.video.frameRate?.ideal || constraints.video.frameRate || 30;
      }

      const videoTrack = createFakeVideoTrack(width, height, fps);
      tracks.push(videoTrack);
      log('Created controllable fake video track:', width, 'x', height);
    }

    if (constraints.audio) {
      // For audio, try to get real/CEF fake audio first
      try {
        const audioStream = await originalGetUserMedia({ audio: constraints.audio });
        const audioTracks = audioStream.getAudioTracks();
        if (audioTracks.length > 0) {
          tracks.push(audioTracks[0]);
          log('Got audio track from browser');
        }
      } catch (audioError) {
        // Fallback to silent fake audio
        const audioTrack = createFakeAudioTrack();
        tracks.push(audioTrack);
        log('Created fake audio track (silent)');
      }
    }

    if (tracks.length === 0) {
      throw new DOMException('No media tracks requested', 'NotFoundError');
    }

    const fakeStream = new MediaStream(tracks);

    // Store stream in Symbol namespace (invisible to getOwnPropertyNames)
    const _owl = Symbol.for('owl');
    if (!window[_owl]) {
      Object.defineProperty(window, _owl, {
        value: { font: {}, webgl: {}, camera: {} },
        writable: false, enumerable: false, configurable: false
      });
    }
    // Ensure camera object exists even if window[_owl] was created elsewhere
    if (!window[_owl].camera) window[_owl].camera = {};
    if (!window[_owl].camera.streams) {
      window[_owl].camera.streams = [];
    }
    window[_owl].camera.streams.push(fakeStream);

    log('Created fake MediaStream with', tracks.length, 'tracks');
    return fakeStream;
  };

  // Make it look native - use global registry (NO direct toString property!)
  const _owl_gum = Symbol.for('owl');
  if (window[_owl_gum] && window[_owl_gum].fn) {
    window[_owl_gum].fn.set(navigator.mediaDevices.getUserMedia, 'function getUserMedia() { [native code] }');
  }

  // Patch legacy navigator.getUserMedia if it exists
  // CRITICAL: Must use createNativeProxy to pass all introspection tests
  if (navigator.getUserMedia || Navigator.prototype.getUserMedia) {
    // First delete any own property on the instance
    delete navigator.getUserMedia;

    // Get createNativeProxy from global registry
    const _owl_gum2 = Symbol.for('owl');
    const createNativeProxy = window[_owl_gum2]?.createNativeProxy;

    if (createNativeProxy) {
      // Get original or create dummy for proxy base
      const _origGetUserMedia = Navigator.prototype.getUserMedia || function getUserMedia() {};

      // Create proxy that passes all introspection tests
      const getUserMediaProxy = createNativeProxy(_origGetUserMedia, (original, thisArg, args) => {
        const [constraints, success, error] = args;
        navigator.mediaDevices.getUserMedia(constraints)
          .then(success)
          .catch(error);
      }, 'getUserMedia');

      // Define on prototype with non-enumerable to match real Chrome
      Object.defineProperty(Navigator.prototype, 'getUserMedia', {
        value: getUserMediaProxy,
        writable: true,
        enumerable: false,  // CRITICAL: must be false
        configurable: true
      });
    } else {
      // Fallback if createNativeProxy not available
      Object.defineProperty(Navigator.prototype, 'getUserMedia', {
        value: function(constraints, success, error) {
          navigator.mediaDevices.getUserMedia(constraints)
            .then(success)
            .catch(error);
        },
        writable: true,
        enumerable: false,
        configurable: true
      });
    }
  }

  // Initialize Symbol namespace for camera streams
  const _owl_cam = Symbol.for('owl');
  if (!window[_owl_cam]) {
    Object.defineProperty(window, _owl_cam, {
      value: { font: {}, webgl: {}, camera: {} },
      writable: false, enumerable: false, configurable: false
    });
  }
  // Ensure camera object exists (may be missing if owl namespace was created elsewhere)
  if (!window[_owl_cam].camera) {
    window[_owl_cam].camera = {};
  }
  if (!window[_owl_cam].camera.streams) {
    window[_owl_cam].camera.streams = [];
  }

  // Helper function to draw image with "cover" mode (maintain aspect ratio, center, crop excess)
  function drawImageCover(ctx, img, canvasWidth, canvasHeight) {
    const imgAspect = img.width / img.height;
    const canvasAspect = canvasWidth / canvasHeight;

    let drawWidth, drawHeight, offsetX, offsetY;

    if (imgAspect > canvasAspect) {
      // Image is wider - scale to match height, crop sides
      drawHeight = canvasHeight;
      drawWidth = canvasHeight * imgAspect;
      offsetX = (canvasWidth - drawWidth) / 2;
      offsetY = 0;
    } else {
      // Image is taller - scale to match width, crop top/bottom
      drawWidth = canvasWidth;
      drawHeight = canvasWidth / imgAspect;
      offsetX = 0;
      offsetY = (canvasHeight - drawHeight) / 2;
    }

    ctx.drawImage(img, offsetX, offsetY, drawWidth, drawHeight);
  }

  // Store inject function in Symbol namespace (called from C++ via wrapper)
  window[_owl_cam].camera.injectFrame = function(base64ImageData) {
    let injected = false;

    // Method 1: Find video elements and update their tracks
    const videos = document.querySelectorAll('video');
    videos.forEach(video => {
      if (video.srcObject) {
        const videoTrack = video.srcObject.getVideoTracks()[0];
        if (videoTrack && videoTrack._vCtx) {
          const img = new Image();
          img.onload = function() {
            const canvas = videoTrack._vCanvas;
            const ctx = videoTrack._vCtx;
            // Clear canvas first
            ctx.fillStyle = '#000000';
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            // Draw with cover mode (maintain aspect ratio, center)
            drawImageCover(ctx, img, canvas.width, canvas.height);
          };
          img.src = 'data:image/jpeg;base64,' + base64ImageData;
          injected = true;
        }
      }
    });

    // Method 2: Also check globally stored streams
    const streams = window[Symbol.for('owl')].camera.streams;
    if (streams) {
      streams.forEach(stream => {
        const videoTrack = stream.getVideoTracks()[0];
        if (videoTrack && videoTrack._vCtx) {
          const img = new Image();
          img.onload = function() {
            const canvas = videoTrack._vCanvas;
            const ctx = videoTrack._vCtx;
            // Clear canvas first
            ctx.fillStyle = '#000000';
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            // Draw with cover mode (maintain aspect ratio, center)
            drawImageCover(ctx, img, canvas.width, canvas.height);
          };
          img.src = 'data:image/jpeg;base64,' + base64ImageData;
          injected = true;
        }
      });
    }
  };

  log('Virtual camera injection complete');
})();
)JS";

  frame->ExecuteJavaScript(script, frame->GetURL(), 0);
}
