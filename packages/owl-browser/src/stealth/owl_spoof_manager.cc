#include "stealth/owl_spoof_manager.h"
#include "stealth/owl_virtual_machine.h"
#include "stealth/owl_stealth.h"
#include "stealth/owl_fingerprint_generator.h"
#include "stealth/spoofs/spoof_utils.h"
#include "stealth/spoofs/navigator_spoof.h"
#include "stealth/spoofs/screen_spoof.h"
#include "stealth/spoofs/canvas_spoof.h"
#include "stealth/spoofs/webgl_spoof.h"
#include "stealth/spoofs/audio_spoof.h"
#include "stealth/spoofs/timezone_spoof.h"
#include "stealth/owl_font_spoofer.h"
#include "util/logger.h"
#include <sstream>
#include <iomanip>
#include <fstream>

namespace owl {
namespace spoofs {

// =============================================================================
// SINGLETON
// =============================================================================

SpoofManager& SpoofManager::Instance() {
    static SpoofManager instance;
    return instance;
}

// =============================================================================
// CONTEXT TYPE HELPERS
// =============================================================================

const char* SpoofManager::ContextTypeName(ContextType type) {
    switch (type) {
        case ContextType::MAIN_FRAME: return "MAIN_FRAME";
        case ContextType::IFRAME: return "IFRAME";
        case ContextType::DEDICATED_WORKER: return "DEDICATED_WORKER";
        case ContextType::SHARED_WORKER: return "SHARED_WORKER";
        case ContextType::SERVICE_WORKER: return "SERVICE_WORKER";
        case ContextType::WORKLET: return "WORKLET";
        default: return "UNKNOWN";
    }
}

LayerConfig SpoofManager::GetLayerConfig(ContextType type) {
    LayerConfig config;
    
    switch (type) {
        case ContextType::MAIN_FRAME:
        case ContextType::IFRAME:
            // Full access to all APIs
            config.navigator = true;
            config.screen = true;
            config.canvas = true;
            config.webgl = true;
            config.audio = true;
            config.timezone = true;
            config.fonts = true;
            config.is_worker = false;
            config.has_dom = true;
            break;
            
        case ContextType::DEDICATED_WORKER:
        case ContextType::SHARED_WORKER:
        case ContextType::SERVICE_WORKER:
            // Workers have limited API access
            config.navigator = true;   // WorkerNavigator available
            config.screen = false;     // No screen object
            config.canvas = true;      // OffscreenCanvas available
            config.webgl = true;       // WebGL on OffscreenCanvas
            config.audio = true;       // AudioContext available in workers
            config.timezone = true;    // Date/Intl available
            config.fonts = true;       // FontFace API
            config.is_worker = true;
            config.has_dom = false;
            break;
            
        case ContextType::WORKLET:
            // Worklets have very limited access
            config.navigator = false;
            config.screen = false;
            config.canvas = false;
            config.webgl = false;
            config.audio = true;  // AudioWorklet needs this
            config.timezone = true;
            config.fonts = false;
            config.is_worker = true;
            config.has_dom = false;
            break;
            
        default:
            // Conservative defaults
            config.navigator = true;
            config.screen = false;
            config.canvas = false;
            config.webgl = false;
            config.audio = false;
            config.timezone = true;
            config.is_worker = true;
            config.has_dom = false;
            break;
    }
    
    return config;
}

// =============================================================================
// CONTEXT DETECTION
// =============================================================================

std::string SpoofManager::GenerateContextDetector() {
    return R"JS(
// ============================================================
// CONTEXT DETECTION
// Automatically detects which execution context we're in
// ============================================================
(function() {
    'use strict';
    const _owl = Symbol.for('owl');
    const _guards = Symbol.for('owl_guards');
    
    // Initialize owl namespace if not exists
    if (typeof self !== 'undefined' && !self[_owl]) {
        Object.defineProperty(self, _owl, {
            value: {},
            writable: true,
            configurable: true,
            enumerable: false
        });
    }
    
    // Initialize guard storage (per-context, prevents double-patching)
    if (typeof self !== 'undefined' && !self[_guards]) {
        Object.defineProperty(self, _guards, {
            value: new Set(),
            writable: false,
            configurable: false,
            enumerable: false
        });
    }
    
    // Detect context type
    const detectContext = () => {
        // Check for worker contexts first
        if (typeof WorkerGlobalScope !== 'undefined') {
            if (typeof ServiceWorkerGlobalScope !== 'undefined' && 
                self instanceof ServiceWorkerGlobalScope) {
                return 'SERVICE_WORKER';
            }
            if (typeof SharedWorkerGlobalScope !== 'undefined' && 
                self instanceof SharedWorkerGlobalScope) {
                return 'SHARED_WORKER';
            }
            if (typeof DedicatedWorkerGlobalScope !== 'undefined' && 
                self instanceof DedicatedWorkerGlobalScope) {
                return 'DEDICATED_WORKER';
            }
            // AudioWorklet, PaintWorklet, etc.
            if (typeof AudioWorkletGlobalScope !== 'undefined' ||
                typeof PaintWorkletGlobalScope !== 'undefined') {
                return 'WORKLET';
            }
            return 'WORKER_UNKNOWN';
        }
        
        // Window context
        if (typeof window !== 'undefined') {
            try {
                if (window.parent !== window && window.top !== window) {
                    return 'IFRAME';
                }
            } catch (e) {
                // Cross-origin iframe - can't access parent
                return 'IFRAME';
            }
            return 'MAIN_FRAME';
        }
        
        return 'UNKNOWN';
    };
    
    // Generate unique context ID
    const generateContextId = () => {
        if (typeof crypto !== 'undefined' && crypto.randomUUID) {
            return crypto.randomUUID();
        }
        return 'ctx_' + Math.random().toString(36).substr(2, 9) + 
               '_' + Date.now().toString(36);
    };
    
    // Store context info
    if (self[_owl]) {
        self[_owl].contextType = detectContext();
        self[_owl].contextId = generateContextId();
        self[_owl].contextDetected = true;
        
        // Guard checking function
        self[_owl].checkGuard = (name) => {
            if (self[_guards].has(name)) {
                return false; // Already patched
            }
            self[_guards].add(name);
            return true; // OK to patch
        };
        
        // Check if patched without setting
        self[_owl].isPatched = (name) => {
            return self[_guards].has(name);
        };
        
        // Reset guard (for debugging)
        self[_owl].resetGuard = (name) => {
            self[_guards].delete(name);
        };
    }
})();
)JS";
}

// =============================================================================
// GUARD SYSTEM
// =============================================================================

std::string SpoofManager::GenerateGuardScript() {
    return R"JS(
// ============================================================
// GUARD SYSTEM
// Prevents double-patching across script re-injections
// ============================================================
(function() {
    'use strict';
    const _owl = Symbol.for('owl');
    const _guards = Symbol.for('owl_guards');
    
    if (!self[_owl]) return;
    
    // Extended guard categories
    const guardCategories = [
        'utils', 'navigator', 'canvas', 'webgl', 'audio', 
        'screen', 'timezone', 'fonts', 'permissions', 'network',
        'storage', 'battery', 'media', 'worker_intercept', 
        'iframe_intercept', 'vm_profile'
    ];
    
    // Initialize guards object for quick access
    if (!self[_owl].guards) {
        self[_owl].guards = {};
        for (const cat of guardCategories) {
            Object.defineProperty(self[_owl].guards, cat, {
                get: () => self[_guards].has(cat),
                enumerable: true
            });
        }
    }
})();
)JS";
}

// =============================================================================
// UTILITIES SCRIPT
// =============================================================================

std::string SpoofManager::GenerateUtilitiesScript() {
    // Delegate to SpoofUtils for the core utilities
    // But wrap with our guard system
    std::stringstream ss;
    
    ss << R"JS(
// ============================================================
// SPOOF UTILITIES (SpoofManager Layer)
// ============================================================
(function() {
    'use strict';
    const _owl = Symbol.for('owl');

    // Guard: Skip if already initialized
    // Note: checkGuard may not exist yet on first run, so check utilsReady directly
    if (self[_owl]?.utilsReady) return;

)JS";

    // Include the core spoof utilities (toString masking, createNativeProxy, etc.)
    ss << SpoofUtils::GenerateUtilitiesScript();

    ss << R"JS(

    // Mark utilities as initialized
    self[_owl].utilsReady = true;
})();
)JS";
    
    return ss.str();
}

// =============================================================================
// VM PROFILE EMBEDDING
// =============================================================================

std::string SpoofManager::GenerateVMProfileScript(const VirtualMachine& vm, 
                                                   const std::string& context_id) {
    std::stringstream ss;
    
    ss << R"JS(
// ============================================================
// VM PROFILE EMBEDDING
// Stores the VirtualMachine profile for all spoof modules
// ============================================================
(function() {
    'use strict';
    const _owl = Symbol.for('owl');
    const _vm = Symbol.for('owl_vm');
    
    // Guard: Skip if already embedded
    if (!self[_owl]?.checkGuard('vm_profile')) return;
    if (self[_vm]) return;
    
)JS";
    
    // Embed the VM profile as frozen object
    ss << "    // VirtualMachine Profile: " << vm.id << "\n";
    ss << "    const __vmProfile = Object.freeze({\n";
    ss << "        id: \"" << EscapeJS(vm.id) << "\",\n";
    
    // OS
    ss << "        os: Object.freeze({\n";
    ss << "            name: \"" << EscapeJS(vm.os.name) << "\",\n";
    ss << "            platform: \"" << EscapeJS(vm.os.platform) << "\",\n";
    ss << "            version: \"" << EscapeJS(vm.os.version) << "\"\n";
    ss << "        }),\n";
    
    // Browser
    ss << "        browser: Object.freeze({\n";
    ss << "            name: \"" << EscapeJS(vm.browser.name) << "\",\n";
    ss << "            version: \"" << EscapeJS(vm.browser.version) << "\",\n";
    ss << "            userAgent: \"" << EscapeJS(vm.browser.user_agent) << "\",\n";
    ss << "            vendor: \"" << EscapeJS(vm.browser.vendor) << "\"\n";
    ss << "        }),\n";
    
    // CPU
    ss << "        cpu: Object.freeze({\n";
    ss << "            hardwareConcurrency: " << vm.cpu.hardware_concurrency << ",\n";
    ss << "            deviceMemory: " << vm.cpu.device_memory << "\n";
    ss << "        }),\n";
    
    // GPU
    ss << "        gpu: Object.freeze({\n";
    ss << "            vendor: \"" << EscapeJS(vm.gpu.vendor) << "\",\n";
    ss << "            renderer: \"" << EscapeJS(vm.gpu.renderer) << "\",\n";
    ss << "            unmaskedVendor: \"" << EscapeJS(vm.gpu.unmasked_vendor) << "\",\n";
    ss << "            unmaskedRenderer: \"" << EscapeJS(vm.gpu.unmasked_renderer) << "\"\n";
    ss << "        }),\n";
    
    // Screen
    ss << "        screen: Object.freeze({\n";
    ss << "            width: " << vm.screen.width << ",\n";
    ss << "            height: " << vm.screen.height << ",\n";
    ss << "            devicePixelRatio: " << vm.screen.device_pixel_ratio << "\n";
    ss << "        }),\n";
    
    // Audio
    ss << "        audio: Object.freeze({\n";
    ss << "            sampleRate: " << vm.audio.sample_rate << ",\n";
    ss << "            baseLatency: " << vm.audio.base_latency << "\n";
    ss << "        }),\n";
    
    // Timezone
    ss << "        timezone: Object.freeze({\n";
    ss << "            name: \"" << EscapeJS(vm.timezone.iana_name) << "\",\n";
    ss << "            offset: " << vm.timezone.offset_minutes << "\n";
    ss << "        }),\n";
    
    // Languages
    ss << "        languages: Object.freeze([";
    for (size_t i = 0; i < vm.language.languages.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << "\"" << EscapeJS(vm.language.languages[i]) << "\"";
    }
    ss << "]),\n";
    
    // Get per-context seeds from OwlFingerprintGenerator
    // This ensures unique fingerprints per context, not just per VM profile
    FingerprintSeeds seeds;
    if (!context_id.empty()) {
        seeds = OwlFingerprintGenerator::Instance().GetSeeds(context_id);
    } else {
        // Fallback to VM profile seeds
        seeds.canvas_seed = vm.canvas.hash_seed;
        seeds.webgl_seed = vm.gpu.renderer_hash_seed;
        seeds.audio_seed = vm.audio.audio_hash_seed;
        seeds.fonts_seed = 0;  // No variation without context_id
    }

    // Canvas/WebGL/Audio/Fonts seeds (per-context)
    ss << "        seeds: Object.freeze({\n";
    ss << "            canvas: 0x" << std::hex << seeds.canvas_seed << std::dec << "n,\n";
    ss << "            webgl: 0x" << std::hex << seeds.webgl_seed << std::dec << "n,\n";
    ss << "            audio: 0x" << std::hex << seeds.audio_seed << std::dec << "n,\n";
    ss << "            fonts: 0x" << std::hex << seeds.fonts_seed << std::dec << "n\n";
    ss << "        })\n";
    ss << "    });\n\n";

    // Context ID for per-context fingerprint variation
    ss << "    const __contextId = \"" << EscapeJS(context_id) << "\";\n\n";

    // Define __vmFontsSeed globally for FontSpoofer to use
    ss << "    const __vmFontsSeed = 0x" << std::hex << seeds.fonts_seed << std::dec << "n;\n";
    ss << "    self[Symbol.for('__owl_fonts_seed__')] = __vmFontsSeed;\n\n";
    
    ss << R"JS(
    // Store in symbol-keyed properties (hidden from enumeration)
    Object.defineProperty(self, _vm, {
        value: __vmProfile,
        writable: false,
        configurable: false,
        enumerable: false
    });
    
    // Also expose via owl namespace for convenience
    self[_owl].vm = __vmProfile;
    self[_owl].contextId = __contextId;
    
    // Store the complete script for child context injection
    // This allows Worker interception to pass VM to spawned workers
    self[_owl].vmEmbedded = true;
})();
)JS";
    
    return ss.str();
}

// =============================================================================
// INDIVIDUAL SPOOF GENERATORS
// =============================================================================

std::string SpoofManager::GenerateNavigatorScript(const VirtualMachine& vm, ContextType ctx) {
    NavigatorSpoof::Config config = NavigatorSpoof::Config::FromVM(vm);
    return NavigatorSpoof::GenerateScript(config);
}

std::string SpoofManager::GenerateScreenScript(const VirtualMachine& vm) {
    ScreenSpoof::Config config = ScreenSpoof::Config::FromVM(vm);
    return ScreenSpoof::GenerateScript(config);
}

std::string SpoofManager::GenerateCanvasScript(const VirtualMachine& vm, ContextType ctx) {
    CanvasSpoof::Config config;
    config.seed = vm.canvas.hash_seed;
    config.apply_noise = vm.canvas.apply_noise;
    config.noise_intensity = vm.canvas.noise_intensity;
    return CanvasSpoof::GenerateScript(config);
}

std::string SpoofManager::GenerateWebGLScript(const VirtualMachine& vm, ContextType ctx) {
    WebGLSpoof::Config config = WebGLSpoof::Config::FromVM(vm);
    return WebGLSpoof::GenerateScript(config);
}

std::string SpoofManager::GenerateAudioScript(const VirtualMachine& vm, ContextType ctx) {
    AudioSpoof::Config config = AudioSpoof::Config::FromVM(vm);
    return AudioSpoof::GenerateScript(config);
}

std::string SpoofManager::GenerateTimezoneScript(const VirtualMachine& vm) {
    return TimezoneSpoof::GenerateScript(vm.timezone.iana_name);
}

// =============================================================================
// WORKER INTERCEPTION
// =============================================================================

std::string SpoofManager::GenerateWorkerInterceptor(const VirtualMachine& vm) {
    std::stringstream ss;
    
    ss << R"JS(
// ============================================================
// WORKER CONSTRUCTOR INTERCEPTION
// Intercepts Worker/SharedWorker creation to propagate VM profile
// ============================================================
(function() {
    'use strict';
    const _owl = Symbol.for('owl');
    
    // Guard: Skip if already patched
    if (!self[_owl]?.checkGuard('worker_intercept')) return;
    if (typeof Worker === 'undefined') return;
    
    const vm = self[_owl]?.vm;
    if (!vm) {
        // VM profile not available for worker interception
        return;
    }
    
    // Store original Worker constructor
    const OriginalWorker = Worker;
    
    // Create intercepted Worker constructor
    function WorkerProxy(scriptURL, options) {
        // Create the actual worker
        const worker = new OriginalWorker(scriptURL, options);
        
        // For same-origin workers, we can use postMessage to send VM config
        // The worker script should check for __owl_init__ message
        const origPostMessage = worker.postMessage.bind(worker);
        let initSent = false;
        
        // Override postMessage to inject init on first call
        worker.postMessage = function(message, transfer) {
            if (!initSent) {
                initSent = true;
                // Send VM profile initialization
                try {
                    origPostMessage({
                        __owl_init__: true,
                        vm: {
                            id: vm.id,
                            os: vm.os,
                            browser: vm.browser,
                            cpu: vm.cpu,
                            gpu: vm.gpu,
                            audio: vm.audio,
                            timezone: vm.timezone,
                            languages: vm.languages,
                            seeds: {
                                canvas: vm.seeds?.canvas?.toString() || '0',
                                webgl: vm.seeds?.webgl?.toString() || '0',
                                audio: vm.seeds?.audio?.toString() || '0'
                            }
                        }
                    }, []);
                } catch (e) {
                    // Cross-origin worker, can't send init
                }
            }
            return origPostMessage(message, transfer);
        };
        
        return worker;
    }
    
    // Make it look native
    Object.defineProperty(WorkerProxy, 'name', { value: 'Worker', configurable: true });
    Object.defineProperty(WorkerProxy, 'length', { value: 1, configurable: true });
    WorkerProxy.prototype = OriginalWorker.prototype;

    // CRITICAL FIX: Update prototype.constructor to point to our intercepted Worker
    Object.defineProperty(WorkerProxy.prototype, 'constructor', {
        value: WorkerProxy,
        writable: true,
        configurable: true,
        enumerable: false
    });

    // Register for toString masking
    if (self[_owl]?.registerNative) {
        self[_owl].registerNative(WorkerProxy, 'function Worker() { [native code] }');
    }

    // Replace global Worker
    self.Worker = WorkerProxy;
    
    // Same for SharedWorker if available
    if (typeof SharedWorker !== 'undefined') {
        const OriginalSharedWorker = SharedWorker;

        function SharedWorkerProxy(scriptURL, options) {
            const worker = new OriginalSharedWorker(scriptURL, options);
            // SharedWorker uses port.postMessage, handled by response filter
            return worker;
        }

        Object.defineProperty(SharedWorkerProxy, 'name', { value: 'SharedWorker', configurable: true });
        Object.defineProperty(SharedWorkerProxy, 'length', { value: 1, configurable: true });
        SharedWorkerProxy.prototype = OriginalSharedWorker.prototype;

        if (self[_owl]?.registerNative) {
            self[_owl].registerNative(SharedWorkerProxy, 'function SharedWorker() { [native code] }');
        }

        // CRITICAL FIX: Update prototype.constructor to point to our intercepted SharedWorker
        Object.defineProperty(SharedWorkerProxy.prototype, 'constructor', {
            value: SharedWorkerProxy,
            writable: true,
            configurable: true,
            enumerable: false
        });

        self.SharedWorker = SharedWorkerProxy;
    }
})();
)JS";

    return ss.str();
}

// =============================================================================
// BLOB URL INTERCEPTION - CRITICAL FOR WORKER SPOOFING
// =============================================================================

std::string SpoofManager::GenerateBlobURLInterceptor(const VirtualMachine& vm) {
    std::stringstream ss;

    // First, generate the worker spoofing script that will be prepended
    std::string workerSpoofScript = GenerateWorkerSpoofScript(vm);

    // Escape the script for embedding in JavaScript string
    std::string escapedScript;
    for (char c : workerSpoofScript) {
        switch (c) {
            case '\\': escapedScript += "\\\\"; break;
            case '\'': escapedScript += "\\'"; break;
            case '"': escapedScript += "\\\""; break;
            case '\n': escapedScript += "\\n"; break;
            case '\r': escapedScript += "\\r"; break;
            case '\t': escapedScript += "\\t"; break;
            default: escapedScript += c; break;
        }
    }

    ss << R"JS(
// ============================================================
// BLOB URL INTERCEPTION
// Intercepts URL.createObjectURL to prepend spoofs to worker scripts
// ============================================================
(function() {
    'use strict';
    const _owl = Symbol.for('owl');

    // Guard: Skip if already patched via normal mechanism (checkGuard handles this)
    if (!self[_owl]?.checkGuard('blob_intercept')) return;
    if (typeof URL === 'undefined' || typeof URL.createObjectURL !== 'function') return;

    // BroadcastChannel for SharedWorker communication
    try {
        const bc = new BroadcastChannel('owl_debug');
        bc.onmessage = (e) => {};
    } catch(e) {}

    // The worker spoofing script to prepend
    const workerSpoofScript = ')JS";

    ss << escapedScript;

    ss << R"JS(';

    const OriginalCreateObjectURL = URL.createObjectURL.bind(URL);
    const OriginalRevokeObjectURL = URL.revokeObjectURL.bind(URL);
    const OriginalBlob = Blob;
    const OriginalWorker = Worker;

    // Map to track blob URLs we've modified
    const blobURLMap = new Map();

    // Helper: Convert blob part to string - handle string, ArrayBuffer, ArrayBufferView, and Blob
    // CRITICAL: Must handle Blob objects (creepJS passes Blob as blob parts)
    const blobPartToString = (part) => {
        if (typeof part === 'string') return part;
        if (part instanceof ArrayBuffer) {
            return new TextDecoder().decode(part);
        }
        if (ArrayBuffer.isView(part)) {
            return new TextDecoder().decode(part);
        }
        // CRITICAL FIX: Handle Blob objects using FileReaderSync or sync XHR
        if (part instanceof OriginalBlob) {
            try {
                // Method 1: FileReaderSync (available in worker contexts)
                if (typeof FileReaderSync !== 'undefined') {
                    const reader = new FileReaderSync();
                    return reader.readAsText(part);
                }
                // Method 2: Synchronous XHR with blob URL (works in main thread)
                const blobUrl = OriginalCreateObjectURL(part);
                try {
                    const xhr = new XMLHttpRequest();
                    xhr.open('GET', blobUrl, false);  // Synchronous
                    xhr.send();
                    if (xhr.status === 200) {
                        return xhr.responseText;
                    }
                } finally {
                    URL.revokeObjectURL(blobUrl);
                }
            } catch (e) {
                // Failed to convert Blob part
            }
        }
        // Reject Promises, functions, and other objects
        return null;
    };

    // Helper: Check if content looks like JavaScript
    const looksLikeJS = (content) => {
        if (!content || typeof content !== 'string') return false;
        // Check for common JS patterns
        return /\b(function|const|let|var|class|import|export|self\.|this\.)\b/.test(content.slice(0, 2000));
    };

    // Intercept Blob constructor - prepend spoofing to all JS-like blobs
    self.Blob = function Blob(blobParts, options) {
        try {
            const type = (options?.type || '').toLowerCase();
            const isJSType = type === '' ||
                             type === 'application/javascript' ||
                             type === 'text/javascript' ||
                             type === 'application/x-javascript' ||
                             type.includes('javascript');

            if (isJSType && Array.isArray(blobParts) && blobParts.length > 0) {
                // Try to detect if this is worker script content
                let shouldPrepend = true;  // Default to prepend for JS blobs
                let hasStringifiedObject = false;
                const convertedParts = [];  // Will hold converted strings OR original parts
                let firstPartContent = null;  // For checking if already patched

                for (let i = 0; i < blobParts.length; i++) {
                    const part = blobParts[i];
                    const str = blobPartToString(part);
                    if (str !== null) {
                        // Successfully converted to string
                        // Check for stringified objects at start (error case)
                        if (/^\[object (Promise|Object|Null|Undefined)\]/.test(str.trim())) {
                            hasStringifiedObject = true;
                        }
                        convertedParts.push(str);
                        if (i === 0) firstPartContent = str;
                    } else {
                        // CRITICAL FIX: For ServiceWorker where FileReaderSync/XHR unavailable,
                        // use the ORIGINAL blob part directly - Blob constructor accepts Blob parts!
                        convertedParts.push(part);
                    }
                }

                // Skip if any part contains stringified object (e.g., Promise)
                if (hasStringifiedObject) {
                    shouldPrepend = false;
                }

                // Check if OUR spoofing IIFE is at the START of content (we prepend, so it should be first)
                if (firstPartContent && firstPartContent.indexOf("(function(){'use strict';if(self.__owlWorkerPatched") === 0) {
                    shouldPrepend = false;
                }

                if (shouldPrepend) {
                    const modifiedParts = [workerSpoofScript + '\n;\n', ...convertedParts];
                    const newBlob = new OriginalBlob(modifiedParts, options || { type: 'application/javascript' });
                    return newBlob;
                }
            }
        } catch (e) {
            // Error in Blob intercept - use original
        }

        return new OriginalBlob(blobParts, options);
    };

    // Preserve Blob prototype and static properties
    self.Blob.prototype = OriginalBlob.prototype;
    Object.defineProperty(self.Blob, 'name', { value: 'Blob', configurable: true });
    Object.defineProperty(self.Blob, 'length', { value: 0, configurable: true });
    if (self[_owl]?.registerNative) {
        self[_owl].registerNative(self.Blob, 'function Blob() { [native code] }');
    }

    // CRITICAL FIX: Update prototype.constructor to point to our intercepted Blob
    // This prevents bypass via Blob.prototype.constructor or instance.constructor
    // CreepJS and similar fingerprinters use this technique to get the original constructor
    Object.defineProperty(self.Blob.prototype, 'constructor', {
        value: self.Blob,
        writable: true,
        configurable: true,
        enumerable: false
    });

    // CRITICAL: Intercept Worker constructor to handle blob URLs
    // This catches cases where blobs were created before our interception
    self.Worker = function Worker(scriptURL, options) {
        let finalURL = scriptURL;

        // Check if this is a blob URL
        if (typeof scriptURL === 'string' && scriptURL.startsWith('blob:')) {
            try {
                // Try to fetch the blob content synchronously using XHR
                // NOTE: Cannot set responseType on synchronous XHR in document context
                // Default responseType returns text via responseText anyway
                const xhr = new XMLHttpRequest();
                xhr.open('GET', scriptURL, false); // Synchronous
                xhr.send();

                if (xhr.status === 200) {
                    const originalContent = xhr.responseText;

                    // Check if already patched or if content starts with stringified object
                    // Only skip for [object Promise], [object Object], etc. at the start - not [object in code
                    const hasStringifiedObj = /^\[object (Promise|Object|Blob|Null|Undefined)\]/.test(originalContent.trim());
                    if (!originalContent.includes('__owlWorkerPatched') && !hasStringifiedObj) {
                        // Create new blob with spoofing prepended
                        const modifiedContent = workerSpoofScript + '\n;\n' + originalContent;
                        const newBlob = new OriginalBlob([modifiedContent], { type: 'application/javascript' });
                        finalURL = OriginalCreateObjectURL(newBlob);

                        // Revoke original blob URL after a delay
                        setTimeout(() => {
                            try { OriginalRevokeObjectURL(scriptURL); } catch(e) {}
                        }, 100);
                    }
                }
            } catch (e) {
                // XHR failed - use original URL
            }
        } else if (typeof scriptURL === 'string' && !scriptURL.startsWith('blob:') && !scriptURL.startsWith('data:')) {
            // Handle file URLs (e.g., './creep.js', '/worker.js', 'https://example.com/worker.js')
            // CEF doesn't call GetResourceResponseFilter for DedicatedWorker requests from custom schemes
            // Solution: Create wrapper blob that runs spoofing code then uses importScripts to load original
            try {
                // For custom schemes like owl://, the URL API may not work correctly
                // Use a simpler approach: if relative path, prepend base URL manually
                let absURL;
                if (scriptURL.startsWith('./') || scriptURL.startsWith('../') || !scriptURL.includes('://')) {
                    // Relative URL - construct absolute URL manually for custom schemes
                    const base = location.href;
                    const lastSlash = base.lastIndexOf('/');
                    const basePath = lastSlash > base.indexOf('://') + 2 ? base.substring(0, lastSlash + 1) : base + '/';
                    absURL = basePath + (scriptURL.startsWith('./') ? scriptURL.substring(2) : scriptURL);
                } else {
                    absURL = scriptURL; // Already absolute
                }

                // Create wrapper that runs spoofing first, then imports the original script
                const wrapperContent = workerSpoofScript + '\n;\ntry{importScripts("' + absURL + '")}catch(e){}';
                const wrapperBlob = new OriginalBlob([wrapperContent], { type: 'application/javascript' });
                finalURL = OriginalCreateObjectURL(wrapperBlob);
            } catch (e) {
                // File URL wrapper failed - use original URL
            }
        }

        return new OriginalWorker(finalURL, options);
    };

    // Preserve Worker prototype and static properties
    self.Worker.prototype = OriginalWorker.prototype;
    Object.defineProperty(self.Worker, 'name', { value: 'Worker', configurable: true });
    Object.defineProperty(self.Worker, 'length', { value: 1, configurable: true });
    if (self[_owl]?.registerNative) {
        self[_owl].registerNative(self.Worker, 'function Worker() { [native code] }');
    }

    // CRITICAL FIX: Update prototype.constructor to point to our intercepted Worker
    Object.defineProperty(self.Worker.prototype, 'constructor', {
        value: self.Worker,
        writable: true,
        configurable: true,
        enumerable: false
    });

    // INTENTIONALLY DISABLE ServiceWorker registration for remote hosts
    // ====================================================================
    // Problem: CEF doesn't call GetResourceRequestHandler for SW script fetches,
    // and Chrome blocks blob: URLs for SW registration. This means we CANNOT
    // patch ServiceWorker scripts for remote hosts.
    //
    // Solution: Disable SW registration so sites fall back to SharedWorker/DedicatedWorker
    // which ARE correctly spoofed via blob URL interception above.
    //
    // TODO: Implement CDP (Chrome DevTools Protocol) Fetch domain interception
    // to patch SW scripts at browser level without proxy conflicts.
    // ====================================================================
    if (typeof navigator !== 'undefined' && navigator.serviceWorker) {
        navigator.serviceWorker.register = function() {
            return Promise.reject(new DOMException(
                'ServiceWorker registration is disabled',
                'NotSupportedError'
            ));
        };
    }

    // Mark as patched - IMPORTANT: Mark worker_intercept guard so GenerateWorkerInterceptor skips
    self[_owl].blobIntercepted = true;
    self[_owl].checkGuard('worker_intercept'); // Claim the guard so later Worker interception is skipped
})();
)JS";

    return ss.str();
}

// =============================================================================
// EARLY BLOB INTERCEPTION - CRITICAL TIMING FIX
// =============================================================================
// This function generates a STANDALONE script that must run BEFORE any page JS.
// Unlike GenerateBlobURLInterceptor, this does NOT depend on:
// - Symbol.for('owl') namespace
// - Guard system
// - Context detector
// It uses a simple global flag to prevent double-patching.
//
// CRITICAL: This fixes the timing bug where page scripts (like creep.js) create
// blob workers BEFORE our normal interception is in place.
// =============================================================================

std::string SpoofManager::GenerateEarlyBlobInterceptor(const VirtualMachine& vm) {
    std::stringstream ss;

    // Generate the worker spoofing script that will be prepended (IIFE for classic scripts)
    std::string workerSpoofScript = GenerateWorkerSpoofScript(vm);

    // Escape the script for embedding in JavaScript string
    std::string escapedScript;
    for (char c : workerSpoofScript) {
        switch (c) {
            case '\\': escapedScript += "\\\\"; break;
            case '\'': escapedScript += "\\'"; break;
            case '"': escapedScript += "\\\""; break;
            case '\n': escapedScript += "\\n"; break;
            case '\r': escapedScript += "\\r"; break;
            case '\t': escapedScript += "\\t"; break;
            default: escapedScript += c; break;
        }
    }

    // Generate ES module early patch (data URL import for ES module blobs)
    // This patches Navigator.prototype before any other ES module imports run
    std::string esModulePatch = GenerateESModuleEarlyPatch(vm);

    // Escape the ES module patch for embedding
    std::string escapedESMPatch;
    for (char c : esModulePatch) {
        switch (c) {
            case '\\': escapedESMPatch += "\\\\"; break;
            case '\'': escapedESMPatch += "\\'"; break;
            case '"': escapedESMPatch += "\\\""; break;
            case '\n': escapedESMPatch += "\\n"; break;
            case '\r': escapedESMPatch += "\\r"; break;
            case '\t': escapedESMPatch += "\\t"; break;
            default: escapedESMPatch += c; break;
        }
    }

    ss << R"JS(
// ============================================================
// EARLY BLOB/WORKER INTERCEPTION
// MUST RUN BEFORE ANY PAGE JAVASCRIPT
// This is a STANDALONE script - no dependencies on owl namespace
// NOW WITH ES MODULE SUPPORT!
// ============================================================
(function() {
    'use strict';

    // Guard: Skip ONLY if BOTH guard is set AND interception is verified in place
    // This fixes race condition between OnContextCreated and ResponseFilter prepending
    // where the guard might be set but interception failed
    if (self.__owlEarlyBlobPatched && self.__owlBlobIntercepted && self.__owlWorkerIntercepted) {
        return;
    }
    self.__owlEarlyBlobPatched = true;

    // The worker spoofing script to prepend to CLASSIC SCRIPT blobs (IIFE)
    var workerSpoofScript = ')JS";

    ss << escapedScript;

    ss << R"JS(';

    // Store for ServiceWorker registration intercept
    // SW scripts fetched from remote URLs need this to prepend spoofing code
    self._owl_workerScript = workerSpoofScript;

    // ES module early patch - data URL import for ES MODULE blobs
    // This import statement evaluates FIRST due to ES module hoisting
    var esmEarlyPatch = ')JS";

    ss << escapedESMPatch;

    ss << R"JS(';

    // Helper: Detect if content is an ES module (has import/export at module level)
    var isESModule = function(content) {
        if (!content || typeof content !== 'string') return false;
        // Simple heuristic: look for import/export statements not inside strings/comments
        var trimmed = content.slice(0, 4000);
        // Remove single-line comments
        trimmed = trimmed.replace(/\/\/[^\n]*/g, '');
        // Remove multi-line comments
        trimmed = trimmed.replace(/\/\*[\s\S]*?\*\//g, '');
        // Check for import/export keywords at statement positions
        return /(?:^|[\n\r;{}])\s*(import|export)\s/.test(trimmed);
    };

    // Save original constructors
    var OriginalBlob = Blob;
    var OriginalWorker = typeof Worker !== 'undefined' ? Worker : null;
    var OriginalCreateObjectURL = URL.createObjectURL.bind(URL);
    var OriginalRevokeObjectURL = URL.revokeObjectURL.bind(URL);

    // Helper: Convert blob part to string
    // CRITICAL: Must handle Blob objects using FileReaderSync (worker contexts)
    // or fall back to synchronous XHR blob URL technique
    var blobPartToString = function(part) {
        if (typeof part === 'string') return part;
        if (part instanceof ArrayBuffer) {
            return new TextDecoder().decode(part);
        }
        if (ArrayBuffer.isView(part)) {
            return new TextDecoder().decode(part);
        }
        // CRITICAL FIX: Handle Blob objects (creepJS passes Blob as blob parts)
        if (part instanceof OriginalBlob) {
            try {
                // Method 1: Use FileReaderSync (available in worker contexts)
                if (typeof FileReaderSync !== 'undefined') {
                    var reader = new FileReaderSync();
                    return reader.readAsText(part);
                }
                // Method 2: Use synchronous XHR with blob URL (works in main thread too)
                var blobUrl = OriginalCreateObjectURL(part);
                try {
                    var xhr = new XMLHttpRequest();
                    xhr.open('GET', blobUrl, false);  // Synchronous
                    xhr.send();
                    if (xhr.status === 200) {
                        return xhr.responseText;
                    }
                } finally {
                    OriginalRevokeObjectURL(blobUrl);
                }
            } catch (e) {
                // Failed to convert Blob part
            }
        }
        return null;
    };

    // Helper: Check if content looks like JavaScript
    var looksLikeJS = function(content) {
        if (!content || typeof content !== 'string') return false;
        return /\b(function|const|let|var|class|import|export|self\.|this\.)\b/.test(content.slice(0, 2000));
    };

    // Intercept Blob constructor - prepend spoofing to JS blobs
    self.Blob = function Blob(blobParts, options) {
        var type = (options && options.type || '').toLowerCase();
        try {
            var isJSType = type === '' ||
                           type === 'application/javascript' ||
                           type === 'text/javascript' ||
                           type === 'application/x-javascript' ||
                           type.indexOf('javascript') !== -1;

            if (isJSType && Array.isArray(blobParts) && blobParts.length > 0) {
                var shouldPrepend = true;  // Default to prepend for JS blobs
                var hasOurMarker = false;
                var convertedParts = [];  // Will hold converted strings OR original parts
                var firstPartContent = null;  // For checking if already patched

                for (var i = 0; i < blobParts.length; i++) {
                    var str = blobPartToString(blobParts[i]);
                    if (str !== null) {
                        // Successfully converted to string
                        // Check for stringified objects (error case)
                        if (/^\[object (Promise|Object|Null|Undefined)\]/.test(str.trim())) {
                            // Invalid content, use original blob unchanged
                            shouldPrepend = false;
                            break;
                        }
                        convertedParts.push(str);
                        if (i === 0) firstPartContent = str;
                    } else {
                        // CRITICAL FIX: For ServiceWorker where FileReaderSync/XHR unavailable,
                        // use the ORIGINAL blob part directly - Blob constructor accepts Blob parts!
                        convertedParts.push(blobParts[i]);
                    }
                }

                // Check if OUR spoofing code is at the START of content (we prepend, so it should be first)
                // This prevents double-patching - check for both IIFE and ESM import
                if (firstPartContent) {
                    if (firstPartContent.indexOf("(function(){'use strict';if(self.__owlWorkerPatched") === 0) {
                        shouldPrepend = false;
                    } else if (firstPartContent.indexOf("import'data:text/javascript") === 0) {
                        shouldPrepend = false;
                    }
                }

                if (shouldPrepend) {
                    // Check if content is an ES module
                    var contentForDetection = convertedParts.filter(function(p) { return typeof p === 'string'; }).join('');
                    var isESM = isESModule(contentForDetection);

                    if (isESM) {
                        // For ES modules, prepend import statement with data URL
                        var modifiedParts = [esmEarlyPatch].concat(convertedParts);
                        return new OriginalBlob(modifiedParts, options || { type: 'application/javascript' });
                    } else {
                        var modifiedParts = [workerSpoofScript + '\n;\n'].concat(convertedParts);
                        return new OriginalBlob(modifiedParts, options || { type: 'application/javascript' });
                    }
                }
            }
        } catch (e) {
            // Error in Blob intercept - use original
        }
        return new OriginalBlob(blobParts, options);
    };

    // Preserve Blob prototype
    self.Blob.prototype = OriginalBlob.prototype;
    Object.defineProperty(self.Blob, 'name', { value: 'Blob', configurable: true });
    Object.defineProperty(self.Blob, 'length', { value: 0, configurable: true });

    // CRITICAL FIX: Update prototype.constructor to point to our intercepted Blob
    // Prevents bypass via Blob.prototype.constructor or instance.constructor
    Object.defineProperty(self.Blob.prototype, 'constructor', {
        value: self.Blob,
        writable: true,
        configurable: true,
        enumerable: false
    });

    // Mark Blob interception as complete
    self.__owlBlobIntercepted = true;

    // Intercept Worker constructor - catch blobs created before our interception
    // NOTE: Worker is NOT available in ServiceWorker context - skip if undefined
    if (OriginalWorker) {
    self.Worker = function Worker(scriptURL, options) {
        var finalURL = scriptURL;

        if (typeof scriptURL === 'string' && scriptURL.indexOf('blob:') === 0) {
            // Method 1: Try synchronous XHR to fetch and patch blob content
            // NOTE: Cannot set responseType on synchronous XHR in document context
            var patched = false;
            try {
                var xhr = new XMLHttpRequest();
                xhr.open('GET', scriptURL, false);
                xhr.send();

                if (xhr.status === 200) {
                    var content = xhr.responseText;
                    var hasStringifiedObj = /^\[object (Promise|Object|Blob|Null|Undefined)\]/.test(content.trim());
                    // Check for OUR IIFE at position 0, not just any mention of marker string
                    var hasOurPatch = content.indexOf("(function(){'use strict';if(self.__owlWorkerPatched") === 0;
                    // Check for OUR ES module data URL import
                    var hasOurESMPatch = content.indexOf("import'data:text/javascript") === 0;

                    if (!hasOurPatch && !hasOurESMPatch && !hasStringifiedObj) {
                        // Check if content is ES module
                        var isESM = isESModule(content);
                        if (isESM) {
                            var modified = esmEarlyPatch + content;
                            var newBlob = new OriginalBlob([modified], { type: 'application/javascript' });
                            finalURL = OriginalCreateObjectURL(newBlob);
                            patched = true;
                        } else {
                            var modified = workerSpoofScript + '\n;\n' + content;
                            var newBlob = new OriginalBlob([modified], { type: 'application/javascript' });
                            finalURL = OriginalCreateObjectURL(newBlob);
                            patched = true;
                        }
                        setTimeout(function() {
                            try { OriginalRevokeObjectURL(scriptURL); } catch(e) {}
                        }, 100);
                    } else if (hasOurPatch || hasOurESMPatch) {
                        patched = true;
                    }
                }
            } catch (e) {
                // XHR failed - try wrapper method
            }

            // Method 2: If XHR failed but our Blob interceptor is active,
            // the blob was already patched when created - use it directly!
            // CRITICAL: Do NOT wrap with importScripts because:
            // - Wrapper's workerSpoofScript sets guards to true
            // - importScripts loads patched blob
            // - Patched blob's workerSpoofScript sees guards, returns early
            // - Spoofing never runs in this context = native values leak!
            if (!patched && self.__owlBlobIntercepted) {
                patched = true;
                // finalURL stays as scriptURL - use the already-patched blob
            }

            // Method 3: Only use wrapper if blob wasn't created through our interceptor
            // (e.g., blob created before our code ran)
            if (!patched) {
                try {
                    var wrapperCode = workerSpoofScript + '\n;\ntry{importScripts("' + scriptURL + '")}catch(e){}';
                    var wrapperBlob = new OriginalBlob([wrapperCode], { type: 'application/javascript' });
                    finalURL = OriginalCreateObjectURL(wrapperBlob);
                    patched = true;
                } catch (e2) {
                    // Wrapper fallback also failed
                }
            }
        } else if (typeof scriptURL === 'string' && scriptURL.indexOf('blob:') !== 0 && scriptURL.indexOf('data:') !== 0) {
            // File URL - create wrapper blob
            // Check if this is a module worker
            var isModuleWorker = options && options.type === 'module';

            try {
                var absURL;
                if (scriptURL.indexOf('./') === 0 || scriptURL.indexOf('../') === 0 || scriptURL.indexOf('://') === -1) {
                    var base = location.href;
                    var lastSlash = base.lastIndexOf('/');
                    var basePath = lastSlash > base.indexOf('://') + 2 ? base.substring(0, lastSlash + 1) : base + '/';
                    absURL = basePath + (scriptURL.indexOf('./') === 0 ? scriptURL.substring(2) : scriptURL);
                } else {
                    absURL = scriptURL;
                }

                if (isModuleWorker) {
                    // ES module worker - use dynamic import() instead of importScripts
                    var wrapper = esmEarlyPatch + 'import("' + absURL + '").catch(function(e){});';
                    var wrapperBlob = new OriginalBlob([wrapper], { type: 'application/javascript' });
                    finalURL = OriginalCreateObjectURL(wrapperBlob);
                } else {
                    // Classic worker - use importScripts
                    var wrapper = workerSpoofScript + '\n;\ntry{importScripts("' + absURL + '")}catch(e){}';
                    var wrapperBlob = new OriginalBlob([wrapper], { type: 'application/javascript' });
                    finalURL = OriginalCreateObjectURL(wrapperBlob);
                }
            } catch (e) {
                // Wrapper error - use original URL
            }
        }

        return new OriginalWorker(finalURL, options);
    };

    // Preserve Worker prototype
    self.Worker.prototype = OriginalWorker.prototype;
    Object.defineProperty(self.Worker, 'name', { value: 'Worker', configurable: true });
    Object.defineProperty(self.Worker, 'length', { value: 1, configurable: true });

    // CRITICAL FIX: Update prototype.constructor to point to our intercepted Worker
    Object.defineProperty(self.Worker.prototype, 'constructor', {
        value: self.Worker,
        writable: true,
        configurable: true,
        enumerable: false
    });

    // Mark Worker interception as complete
    self.__owlWorkerIntercepted = true;
    } // End of if (OriginalWorker)

    // =========================================================================
    // ServiceWorker Note:
    // CEF does NOT support blob: URLs for ServiceWorker registration.
    // The HTTPS server (owl_https_server.cc) handles ServiceWorker script
    // patching when the browser fetches the script. No JS-level interception
    // needed here - just let the registration pass through to our server.
    // =========================================================================
})();
)JS";

    // =========================================================================
    // CRITICAL CSP FIX: Inject workerSpoofScript DIRECTLY (not via new Function)
    // ServiceWorkers have strict CSP that blocks new Function() / eval()
    // By injecting the raw script here, it runs as inline code with no CSP issues
    // The workerSpoofScript has its own guards to prevent double-execution
    // WRAPPED IN TRY/CATCH: ServiceWorker evaluation fails on uncaught errors
    // =========================================================================
    ss << R"JS(
// DIRECT INLINE NAVIGATOR SPOOFING (CSP-safe, no dynamic execution)
try {
)JS";
    ss << workerSpoofScript;
    ss << R"JS(
// Navigator spoofing complete
} catch (__owl_early_error) {
    // Navigator spoofing error caught
}
)JS";

    return ss.str();
}

// Generate minimal worker spoofing script (to be prepended to blob workers)
// This script is SELF-REPLICATING - it intercepts Worker/Blob creation to propagate itself
// CRITICAL: Must include createNativeProxy and toString masking for CreepJS compatibility
std::string SpoofManager::GenerateWorkerSpoofScript(const VirtualMachine& vm) {
    std::stringstream ss;

    // Start of self-contained worker spoofing IIFE
    ss << "(function(){'use strict';";

    // Guard: Skip if already patched and navigator spoofing is verified complete
    ss << "if(self.__owlWorkerPatched && self.__owlNavSpoofed){return;}self.__owlWorkerPatched=true;";

    // BroadcastChannel for SharedWorker communication
    ss << "var _bc;try{_bc=new BroadcastChannel('owl_debug');}catch(e){}";

    // =========================================================================
    // CRITICAL: Add createNativeProxy and toString masking for worker contexts
    // This ensures spoofed getters pass all CreepJS introspection tests:
    // - toString() returns "[native code]"
    // - Calling with null this throws "Illegal invocation"
    // - Object.prototype.toString.call(fn) returns "[object Function]"
    // - Prototype chain cycle detection works correctly
    //
    // IMPORTANT: Cycle tracking approach - cycles are tracked in a WeakSet
    // and errors are thrown from toString handler (not from setPrototypeOf)
    // so the stack trace shows "at Function.toString" which passes CreepJS
    // =========================================================================
    ss << R"JS(
var _pf=new WeakMap();
var _cycleProxies=new WeakSet();
var _origTS=Function.prototype.toString;
var _origCall=Function.prototype.call;
var _origSetProto=Object.setPrototypeOf;
var _origReflectSetProto=typeof Reflect!=='undefined'?Reflect.setPrototypeOf:null;
var _proxyOriginals=new WeakMap();
var _createNativeProxy=function(orig,handler,name){
var effName=name||orig.name||'anonymous';
var nativeStr='function '+effName+'() { [native code] }';
var wrapper={[effName](...args){return handler(orig,this,args);}}[effName];
_pf.set(wrapper,nativeStr);
return wrapper;
};
var _tsImpl={toString(){if(_cycleProxies.has(this))throw new TypeError('Cyclic __proto__ value');var m=_pf.get(this);if(m!==undefined)return m;return _origCall.call(_origTS,this);}}.toString;
_pf.set(_tsImpl,'function toString() { [native code] }');
Function.prototype.toString=_tsImpl;
var _setProtoProxy=_createNativeProxy(_origSetProto,function(t,thisArg,args){
return _origCall.call(t,Object,args[0],args[1]);
},'setPrototypeOf');
Object.setPrototypeOf=_setProtoProxy;
if(_origReflectSetProto){
var _reflectSetProtoProxy=_createNativeProxy(_origReflectSetProto,function(t,thisArg,args){
return _origCall.call(t,Reflect,args[0],args[1]);
},'setPrototypeOf');
Reflect.setPrototypeOf=_reflectSetProtoProxy;
}
var _origObjectCreate=Object.create;
var _getOriginal=function(fn){
var cur=fn,depth=0;
while(_proxyOriginals.has(cur)&&depth<10){cur=_proxyOriginals.get(cur);depth++;}
return cur;
};
var _objectCreateProxy=_createNativeProxy(_origObjectCreate,function(t,thisArg,args){
var proto=_getOriginal(args[0]);
return _origCall.call(t,Object,proto,args[1]);
},'create');
Object.create=_objectCreateProxy;
)JS";

    // Navigator spoofing - now using createNativeProxy for proper masking
    ss << "var navProto=Object.getPrototypeOf(navigator);";

    // Use prototype chain check - sendBeacon does NOT exist on WorkerNavigator.prototype in blob workers
    // This approach works in all contexts (Window, Worker, SharedWorker, ServiceWorker, blob workers)
    ss << "var isRealWN=function(o){if(o===navProto)return false;try{var p=o;while(p!==null){if(p===navProto)return true;p=Object.getPrototypeOf(p);}return false;}catch(e){return false;}};";

    ss << "var spoofNav={";
    ss << "platform:'" << vm.os.platform << "',";
    ss << "userAgent:'" << EscapeJS(vm.browser.user_agent) << "',";
    ss << "appVersion:'" << EscapeJS(vm.browser.user_agent.substr(8)) << "',";  // Remove "Mozilla/"
    ss << "vendor:'" << EscapeJS(vm.browser.vendor) << "',";
    ss << "hardwareConcurrency:" << vm.cpu.hardware_concurrency << ",";
    ss << "deviceMemory:" << vm.cpu.device_memory << ",";
    ss << "language:'" << EscapeJS(vm.language.primary) << "',";
    ss << "languages:Object.freeze(['" << EscapeJS(vm.language.primary) << "'";
    for (size_t i = 1; i < vm.language.languages.size() && i < 3; i++) {
        ss << ",'" << EscapeJS(vm.language.languages[i]) << "'";
    }
    ss << "])";
    ss << "};";

    // Apply navigator spoofs using createNativeProxy for proper masking
    // CRITICAL: WorkerNavigator properties MUST be accessor properties (with getters)
    // Native getters throw "Illegal invocation" when accessed on prototype directly
    // CreepJS detects data properties as lies because proto[name] returns value without TypeError
    ss << R"JS(
Object.keys(spoofNav).forEach(function(k){
try{
var origDesc=Object.getOwnPropertyDescriptor(navProto,k);
var val=spoofNav[k];
// ALWAYS use accessor property with getter that throws "Illegal invocation"
// This matches native behavior where WorkerNavigator.prototype.platform throws
var origGetter=origDesc&&origDesc.get?origDesc.get:function(){return val;};
var proxyGetter=_createNativeProxy(origGetter,function(t,thisArg,args){
if(!isRealWN(thisArg)){throw new TypeError('Illegal invocation');}
var retVal=k==='languages'?Object.freeze(val.slice?val.slice():val):val;
return retVal;
},'get '+k);
Object.defineProperty(navProto,k,{get:proxyGetter,configurable:true,enumerable:true});
}catch(e){}
});
)JS";

    // Spoof webdriver using createNativeProxy
    // CRITICAL: Must be accessor property with getter that throws "Illegal invocation"
    ss << R"JS(
try{
var wdDesc=Object.getOwnPropertyDescriptor(navProto,'webdriver');
var wdOrigGetter=wdDesc&&wdDesc.get?wdDesc.get:function(){return false;};
var wdProxy=_createNativeProxy(wdOrigGetter,function(t,thisArg,args){
if(!isRealWN(thisArg)){throw new TypeError('Illegal invocation');}
return false;
},'get webdriver');
Object.defineProperty(navProto,'webdriver',{get:wdProxy,configurable:true,enumerable:true});
}catch(e){}
)JS";

    // Spoof userAgentData - ALWAYS create it even if it doesn't exist in workers
    // Use createNativeProxy for the getter to pass introspection tests
    ss << R"JS(
try{
var _uaOsName=')JS" << EscapeJS(vm.os.name == "Windows" ? "Windows" : vm.os.name) << R"JS(';
var _uaBrowserVer=')JS" << EscapeJS(vm.browser.version) << R"JS(';
var _uaOsVer=')JS" << EscapeJS(vm.os.version) << R"JS(';
var uaData={
platform:_uaOsName,mobile:false,
brands:Object.freeze([
Object.freeze({brand:'Chromium',version:_uaBrowserVer}),
Object.freeze({brand:'Not)A;Brand',version:'99'}),
Object.freeze({brand:'Google Chrome',version:_uaBrowserVer})
]),
getHighEntropyValues:function(hints){return Promise.resolve({
platform:_uaOsName,
platformVersion:_uaOsVer,
architecture:'x86',
bitness:'64',
model:'',
uaFullVersion:_uaBrowserVer+'.0.0.0',
fullVersionList:[
{brand:'Chromium',version:_uaBrowserVer+'.0.0.0'},
{brand:'Not)A;Brand',version:'99.0.0.0'},
{brand:'Google Chrome',version:_uaBrowserVer+'.0.0.0'}
]
})},
toJSON:function(){return{brands:this.brands,mobile:this.mobile,platform:this.platform};}
};
if(navigator.userAgentData){Object.setPrototypeOf(uaData,Object.getPrototypeOf(navigator.userAgentData));}
Object.freeze(uaData);
var uaDataDesc=Object.getOwnPropertyDescriptor(navProto,'userAgentData');
if(uaDataDesc&&uaDataDesc.get){
var uaDataProxy=_createNativeProxy(uaDataDesc.get,function(t,thisArg,args){
if(!isRealWN(thisArg)){throw new TypeError('Illegal invocation');}
return uaData;
},'get userAgentData');
Object.defineProperty(navProto,'userAgentData',{get:uaDataProxy,configurable:true,enumerable:true});
}else{
Object.defineProperty(navProto,'userAgentData',{get:function(){return uaData},configurable:true,enumerable:true});
}
}catch(e){}
)JS";

    // Mark navigator spoofing as complete - this is checked by the guard
    ss << R"JS(
self.__owlNavSpoofed = true;
)JS";

    // SELF-REPLICATING: Intercept Worker constructor for nested workers
    // CRITICAL: For nested workers, we pass the FULL script (with createNativeProxy)
    // instead of a simplified version. This ensures introspection tests pass at all levels.
    ss << "if(typeof Worker!=='undefined'){";
    ss << "var OrigW=Worker,OrigB=Blob;";

    // Embed the spoofing values for data propagation
    ss << "var spoofData={p:'" << EscapeJS(vm.os.platform) << "',";
    ss << "ua:'" << EscapeJS(vm.browser.user_agent) << "',";
    ss << "v:'" << EscapeJS(vm.browser.vendor) << "',";
    ss << "hc:" << vm.cpu.hardware_concurrency << ",";
    ss << "dm:" << vm.cpu.device_memory << ",";
    ss << "l:'" << EscapeJS(vm.language.primary) << "',";
    ss << "os:'" << EscapeJS(vm.os.name == "Windows" ? "Windows" : vm.os.name) << "',";
    ss << "bv:'" << EscapeJS(vm.browser.version) << "',";
    ss << "ov:'" << EscapeJS(vm.os.version) << "'};";
    ss << "var SD=JSON.stringify(spoofData);";

    // =========================================================================
    // mkScript: Generate a FULL script with createNativeProxy for nested workers
    // This includes the minified createNativeProxy function to pass introspection tests
    // at all nesting levels (Worker -> Worker -> Worker, etc.)
    // =========================================================================
    ss << R"JS(
var mkScript=function(){
// Minified createNativeProxy infrastructure for nested workers
// CRITICAL: Uses cycle tracking approach - track cycles in WeakSet, throw from toString
var core='var _pf=new WeakMap(),_cp=new WeakSet(),_pO=new WeakMap(),_oTS=Function.prototype.toString,_oC=Function.prototype.call,_oSP=Object.setPrototypeOf,_oRSP=typeof Reflect!=="undefined"?Reflect.setPrototypeOf:null,_oOC=Object.create;';
core+='var _cnp=function(o,h,n){var w=(function(){}).bind(),s="function "+(n||o.name||"anonymous")+"() { [native code] }",cp=null,cc=false;';
core+='var hc=function(){if(cp===null||cc)return false;cc=true;try{var c=cp,v=new WeakSet(),d=0;while(c!==null&&c!==undefined&&d<50){if(c===p)return true;if(v.has(c))return true;v.add(c);try{var np=Reflect.getPrototypeOf(c);if(np===p)return true;c=np;}catch(e){break;}d++;}return false;}finally{cc=false;}};';
core+='var tc=function(pr){var ic=false;if(pr===p)ic=true;else{var c=pr,d=0;while(c!==null&&c!==undefined&&d<50){if(c===p){ic=true;break;}try{c=Reflect.getPrototypeOf(c);}catch(e){break;}d++;}}if(ic)_cp.add(p);else _cp.delete(p);return ic;};';
core+='var p=new Proxy(w,{apply:function(t,a,r){return h(o,a,r);},get:function(t,k,r){if(k==="prototype")return undefined;if(k==="length")return o.length;if(k==="name")return n||o.name;if(k===Symbol.toStringTag)return"Function";if(k==="__proto__")return cp!==null?cp:Function.prototype;if(k==="constructor")return Function;if(hc())throw new TypeError("Cyclic __proto__ value");return Reflect.get(cp!==null?cp:Function.prototype,k,r);},';
core+='set:function(t,k,v,r){if(k==="prototype"||k==="length"||k==="name")return false;if(k==="__proto__"){tc(v);cp=v;return true;}return Reflect.set(w,k,v,w);},';
core+='getPrototypeOf:function(){return cp!==null?cp:Function.prototype;},';
core+='setPrototypeOf:function(t,pr){tc(pr);cp=pr;return true;}});_pf.set(p,s);_pf.set(w,s);_pO.set(p,o);return p;};';
core+='var _tsp=_cnp(_oTS,function(t,a,r){if(_cp.has(a))throw new TypeError("Cyclic __proto__ value");var m=_pf.get(a);if(m!==undefined)return m;return _oC.call(t,a);},"toString");Function.prototype.toString=_tsp;';
core+='var _spp=_cnp(_oSP,function(t,a,r){return _oC.call(t,Object,r[0],r[1]);},"setPrototypeOf");Object.setPrototypeOf=_spp;';
core+='if(_oRSP){var _rspp=_cnp(_oRSP,function(t,a,r){return _oC.call(t,Reflect,r[0],r[1]);},"setPrototypeOf");Reflect.setPrototypeOf=_rspp;}';
core+='var _gO=function(f){var c=f,d=0;while(_pO.has(c)&&d<10){c=_pO.get(c);d++;}return c;};';
core+='var _ocp=_cnp(_oOC,function(t,a,r){return _oC.call(t,Object,_gO(r[0]),r[1]);},"create");Object.create=_ocp;';

var d=spoofData;
var script='(function(){"use strict";if(self.__owlWorkerPatched&&self.__owlNavSpoofed)return;self.__owlWorkerPatched=true;';
script+=core;
script+='var d='+SD+';var np=Object.getPrototypeOf(navigator);';
// Use prototype chain check - sendBeacon does NOT exist on WorkerNavigator.prototype in blob workers
script+='var isRWN=function(o){if(o===np)return false;try{var p=o;while(p!==null){if(p===np)return true;p=Object.getPrototypeOf(p);}return false;}catch(e){return false;}};';
script+='var sn={platform:d.p,userAgent:d.ua,appVersion:d.ua.substr(8),vendor:d.v,hardwareConcurrency:d.hc,deviceMemory:d.dm,language:d.l,languages:Object.freeze([d.l])};';
script+='Object.keys(sn).forEach(function(k){try{var od=Object.getOwnPropertyDescriptor(np,k);var v=sn[k];var og=od&&od.get?od.get:function(){return v;};var pg=_cnp(og,function(t,a,r){if(!isRWN(a))throw new TypeError("Illegal invocation");return k==="languages"?Object.freeze(v.slice?v.slice():v):v;},"get "+k);Object.defineProperty(np,k,{get:pg,configurable:true,enumerable:true});}catch(e){}});';
script+='try{var wd=Object.getOwnPropertyDescriptor(np,"webdriver");var wdg=wd&&wd.get?wd.get:function(){return false;};var wp=_cnp(wdg,function(t,a,r){if(!isRWN(a))throw new TypeError("Illegal invocation");return false;},"get webdriver");Object.defineProperty(np,"webdriver",{get:wp,configurable:true,enumerable:true});}catch(e){}';
script+='try{var ua={platform:d.os,mobile:false,brands:Object.freeze([Object.freeze({brand:"Chromium",version:d.bv}),Object.freeze({brand:"Not)A;Brand",version:"99"}),Object.freeze({brand:"Google Chrome",version:d.bv})]),getHighEntropyValues:function(){return Promise.resolve({platform:d.os,platformVersion:d.ov,architecture:"x86",bitness:"64",model:"",uaFullVersion:d.bv+".0.0.0",fullVersionList:[{brand:"Chromium",version:d.bv+".0.0.0"},{brand:"Not)A;Brand",version:"99.0.0.0"},{brand:"Google Chrome",version:d.bv+".0.0.0"}]})},toJSON:function(){return{brands:this.brands,mobile:this.mobile,platform:this.platform};}};';
script+='Object.freeze(ua);var udd=Object.getOwnPropertyDescriptor(np,"userAgentData");if(udd&&udd.get){var udp=_cnp(udd.get,function(t,a,r){if(!isRWN(a))throw new TypeError("Illegal invocation");return ua;},"get userAgentData");Object.defineProperty(np,"userAgentData",{get:udp,configurable:true,enumerable:true});}else{Object.defineProperty(np,"userAgentData",{get:function(){return ua},configurable:true,enumerable:true});}}catch(e){}';
// Mark navigator spoofing complete
script+='self.__owlNavSpoofed=true;';
// Add Blob/Worker interception for deeper nesting
script+='if(typeof Worker!=="undefined"){var OB=Blob,OW=Worker,SD2=JSON.stringify(d);';
script+='var mk2=function(){return "(function(){\\"use strict\\";if(self.__owlWorkerPatched&&self.__owlNavSpoofed)return;self.__owlWorkerPatched=true;var d="+SD2+";var np=Object.getPrototypeOf(navigator);var sn={platform:d.p,userAgent:d.ua,appVersion:d.ua.substr(8),vendor:d.v,hardwareConcurrency:d.hc,deviceMemory:d.dm,language:d.l,languages:Object.freeze([d.l])};for(var k in sn){try{Object.defineProperty(np,k,{get:(function(v){return function(){return v}})(sn[k]),configurable:true,enumerable:true})}catch(e){}}try{Object.defineProperty(np,\\"webdriver\\",{get:function(){return false},configurable:true,enumerable:true})}catch(e){}self.__owlNavSpoofed=true;})();"};';
// CRITICAL FIX: Handle Blob objects in deeply nested workers using FileReaderSync
script+='self.Blob=function(p,o){var t=(o&&o.type||"").toLowerCase();if((t===""||t.indexOf("javascript")>=0)&&p&&p.length>0){try{var v=true,ss=[];for(var i=0;i<p.length;i++){var pt=p[i],st;if(typeof pt==="string")st=pt;else if(pt instanceof ArrayBuffer||pt instanceof Uint8Array)st=new TextDecoder().decode(pt);else if(pt instanceof OB){try{if(typeof FileReaderSync!=="undefined"){st=new FileReaderSync().readAsText(pt)}else{var bu=URL.createObjectURL(pt);try{var x=new XMLHttpRequest();x.open("GET",bu,false);x.send();st=x.status===200?x.responseText:null}finally{URL.revokeObjectURL(bu)}}}catch(e){st=null}if(st===null){v=false;break;}}else{v=false;break;}if(/^\\[object (Promise|Object|Blob)\\]/.test(st.trim())){v=false;break;}ss.push(st);}if(v&&ss.length>0&&ss[0].indexOf("(function(){"+"\\""+"use strict"+"\\""+";if(self.__owlWorkerPatched")!==0){var m=mk2();return new OB([m+"\\n;\\n"].concat(ss),o||{type:"application/javascript"});}}catch(e){}}return new OB(p,o);};self.Blob.prototype=OB.prototype;Object.defineProperty(self.Blob.prototype,"constructor",{value:self.Blob,writable:true,configurable:true,enumerable:false});';
script+='self.Worker=function(u,o){if(typeof u==="string"&&u.indexOf("blob:")===0){try{var x=new XMLHttpRequest();x.open("GET",u,false);x.send();var rt=x.responseText;var hso=/^\\[object (Promise|Object|Blob)\\]/.test(rt.trim());if(x.status===200&&rt.indexOf("(function(){"+"\\""+"use strict"+"\\""+";if(self.__owlWorkerPatched")!==0&&!hso){var m=mk2();var nb=new OB([m+"\\n;\\n"+rt],{type:"application/javascript"});return new OW(URL.createObjectURL(nb),o);}}catch(e){}}return new OW(u,o);};self.Worker.prototype=OW.prototype;Object.defineProperty(self.Worker.prototype,"constructor",{value:self.Worker,writable:true,configurable:true,enumerable:false});}';
script+='})();';
return script;
};
)JS";

    // NOTE: Removed new Function() validation - CSP blocks it in ServiceWorkers
    // mkScript() output is validated at generation time instead

    // Intercept Blob constructor - only intercept actual string content, not Promises
    // Must check ALL parts for stringified objects like [object Promise]
    // CRITICAL FIX: Handle Blob objects using FileReaderSync
    ss << "self.Blob=function(parts,opts){";
    ss << "var t=(opts&&opts.type||'').toLowerCase();";
    ss << "if((t===''||t.indexOf('javascript')>=0)&&parts&&parts.length>0){";
    ss << "try{";
    ss << "var valid=true,strs=[];";
    ss << "for(var i=0;i<parts.length;i++){";
    ss << "var p=parts[i],s;";
    ss << "if(typeof p==='string')s=p;";
    ss << "else if(p instanceof ArrayBuffer||p instanceof Uint8Array)s=new TextDecoder().decode(p);";
    // CRITICAL FIX: Handle Blob objects (creepJS passes Blob as blob parts)
    ss << "else if(p instanceof OrigB){";
    ss << "try{if(typeof FileReaderSync!=='undefined'){var r=new FileReaderSync();s=r.readAsText(p);}";
    ss << "else{var bu=URL.createObjectURL(p);try{var x=new XMLHttpRequest();x.open('GET',bu,false);x.send();s=x.status===200?x.responseText:null;}finally{URL.revokeObjectURL(bu);}}}";
    ss << "catch(e){s=null;}";
    ss << "if(s===null){valid=false;break;}}";
    ss << "else{valid=false;break;}";
    ss << "if(/^\\[object (Promise|Object|Blob|Null|Undefined)\\]/.test(s.trim())){valid=false;break;}";
    ss << "strs.push(s);";
    ss << "}";
    ss << "if(valid&&strs.length>0&&strs[0].indexOf(\"(function(){'use strict';if(self.__owlWorkerPatched\")!==0){";
    ss << "var ms=mkScript();";
    ss << "return new OrigB([ms+'\\n;\\n'].concat(strs),opts||{type:'application/javascript'});";
    ss << "}}catch(e){}}";
    ss << "return new OrigB(parts,opts);};";
    ss << "self.Blob.prototype=OrigB.prototype;";
    // CRITICAL FIX: Update prototype.constructor to prevent bypass
    ss << "Object.defineProperty(self.Blob.prototype,'constructor',{value:self.Blob,writable:true,configurable:true,enumerable:false});";

    // Intercept Worker constructor - handle BOTH blob: URLs and file URLs
    // CEF doesn't call GetResourceResponseFilter for DedicatedWorker requests from custom schemes
    // Solution: Create wrapper blob that runs spoofing code then imports original script
    // This avoids XHR issues with custom schemes
    ss << "self.Worker=function(url,opts){";
    ss << "if(typeof url==='string'){";
    // For blob URLs, try XHR approach (works for same-origin blobs)
    ss << "if(url.indexOf('blob:')===0){";
    ss << "try{";
    ss << "var x=new XMLHttpRequest();x.open('GET',url,false);x.send();";
    ss << "var rt=x.responseText;";
    ss << "if(x.status===200&&rt.indexOf(\"(function(){'use strict';if(self.__owlWorkerPatched\")!==0){";
    ss << "var ms=mkScript();var nb=new OrigB([ms+'\\n;\\n'+rt],{type:'application/javascript'});";
    ss << "return new OrigW(URL.createObjectURL(nb),opts);";
    ss << "}}catch(e){}";
    ss << "}else{";
    // For non-blob URLs (file URLs), use importScripts wrapper
    // This avoids XHR issues with custom schemes like owl://
    ss << "try{";
    ss << "var absUrl=new URL(url,location.href).href;";
    ss << "var ms=mkScript();";
    // Wrapper: run spoofing, then importScripts the original script
    ss << "var wrapper=ms+'\\n;try{importScripts(\"'+absUrl+'\")}catch(e){}';";
    ss << "var nb=new OrigB([wrapper],{type:'application/javascript'});";
    ss << "return new OrigW(URL.createObjectURL(nb),opts);";
    ss << "}catch(e){}";
    ss << "}";
    ss << "}";
    ss << "return new OrigW(url,opts);};";
    ss << "self.Worker.prototype=OrigW.prototype;";
    // CRITICAL FIX: Update prototype.constructor to prevent bypass
    ss << "Object.defineProperty(self.Worker.prototype,'constructor',{value:self.Worker,writable:true,configurable:true,enumerable:false});";
    ss << "}";

    ss << "})();";

    return ss.str();
}

// =============================================================================
// ES MODULE EARLY PATCH - CRITICAL FOR SERVICE WORKER TIMING
// =============================================================================
// ES modules hoist imports - they're evaluated BEFORE any other code runs.
// For Service Workers using ES modules (like CreepJS), our prepended IIFE runs
// AFTER their import statements have already captured real navigator values.
//
// Solution: Prepend an import statement that imports a data URL module.
// This module evaluates FIRST (before any other imports) and patches
// Navigator.prototype, so subsequent imports see spoofed values.
// =============================================================================

std::string SpoofManager::GenerateESModuleEarlyPatch(const VirtualMachine& vm) {
    std::stringstream ss;

    // Build the patch module code that will be embedded in a data URL
    // CRITICAL: In workers, navigator inherits from WorkerNavigator.prototype, NOT Navigator.prototype
    // We use Object.getPrototypeOf(navigator) to get the correct prototype in any context
    //
    // CRITICAL FIX: Must use method shorthand for getters to avoid 'prototype' property!
    // Plain `function(){}` has 'prototype', but native getters don't.
    // Method shorthand `{['name'](){}}['name']` naturally has no 'prototype'.
    //
    // Additionally, we must patch Function.prototype.toString and introspection methods
    // to pass CreepJS lie detection tests.
    std::stringstream patchModule;
    std::string osName = vm.os.name == "Windows" ? "Windows" : vm.os.name;

    patchModule << "/* OWL_WORKER_PATCH: ES Module Early Patch - runs BEFORE all other imports */\n";

    // CRITICAL: Initialize _owl namespace and set guards FIRST to prevent double-patching
    // by GenerateWorkerScript which runs after this ES module import
    patchModule << "var _owl = Symbol.for('owl');\n";
    patchModule << "if (!self[_owl]) {\n";
    patchModule << "  Object.defineProperty(self, _owl, {\n";
    patchModule << "    value: { guards: {}, _cycleProxies: new WeakSet() },\n";
    patchModule << "    writable: true, configurable: true, enumerable: false\n";
    patchModule << "  });\n";
    patchModule << "}\n";
    patchModule << "self[_owl].checkGuard = function(n) {\n";
    patchModule << "  if (self[_owl].guards[n]) return false;\n";
    patchModule << "  self[_owl].guards[n] = true;\n";
    patchModule << "  return true;\n";
    patchModule << "};\n";
    patchModule << "self[_owl].guards.navigator = true;\n";  // Claim navigator guard
    patchModule << "self[_owl].utilsInitialized = true;\n";  // Mark utilities as initialized
    patchModule << "\n";

    // Store original functions for masking
    patchModule << "var _oTS = Function.prototype.toString;\n";
    patchModule << "var _oC = Function.prototype.call;\n";
    patchModule << "var _oGOPN = Object.getOwnPropertyNames;\n";
    patchModule << "var _oGOPD = Object.getOwnPropertyDescriptor;\n";
    patchModule << "var _oGOPDs = Object.getOwnPropertyDescriptors;\n";
    patchModule << "var _oROK = Reflect.ownKeys;\n";
    patchModule << "var _oHOP = Object.prototype.hasOwnProperty;\n";
    patchModule << "var _oSP = Object.setPrototypeOf;\n";
    patchModule << "var _oRSP = Reflect.setPrototypeOf;\n";
    patchModule << "var _oRH = Reflect.has;\n";

    // Registry for patched functions
    patchModule << "var _pf = new WeakMap();\n";
    patchModule << "var _cp = new WeakSet();\n";  // Cycle proxies

    // Get the correct prototype
    patchModule << "var _navProto = Object.getPrototypeOf(navigator);\n\n";

    // Helper to check if prototype-level access (throw Illegal invocation)
    patchModule << "var _isProto = function(t) { return t === _navProto; };\n\n";

    // Create native-like getter using method shorthand
    // Method shorthand {['name'](){}}['name'] has no 'prototype' property
    patchModule << "var _mkGetter = function(name, value, isArray) {\n";
    patchModule << "  var g = {['get ' + name]() {\n";
    patchModule << "    if (_isProto(this)) throw new TypeError('Illegal invocation');\n";
    patchModule << "    return isArray ? Object.freeze(value.slice()) : value;\n";
    patchModule << "  }}['get ' + name];\n";
    patchModule << "  _pf.set(g, 'function get ' + name + '() { [native code] }');\n";
    patchModule << "  return g;\n";
    patchModule << "};\n\n";

    // Patch Function.prototype.toString to mask our getters
    patchModule << "var _newTS = {toString() {\n";
    patchModule << "  if (_cp.has(this)) throw new TypeError('Cyclic __proto__ value');\n";
    patchModule << "  if (this && typeof this === 'function') {\n";
    patchModule << "    var m = _pf.get(this);\n";
    patchModule << "    if (m !== undefined) return m;\n";
    patchModule << "  }\n";
    patchModule << "  return _oC.call(_oTS, this);\n";
    patchModule << "}}.toString;\n";
    patchModule << "_pf.set(_newTS, 'function toString() { [native code] }');\n";
    patchModule << "Function.prototype.toString = _newTS;\n\n";

    // Patch Object.getOwnPropertyNames to hide 'prototype' for our functions
    patchModule << "Object.getOwnPropertyNames = function(obj) {\n";
    patchModule << "  var names = _oC.call(_oGOPN, Object, obj);\n";
    patchModule << "  if (_pf.has(obj)) return names.filter(function(n) { return n !== 'prototype'; });\n";
    patchModule << "  return names;\n";
    patchModule << "};\n";
    patchModule << "_pf.set(Object.getOwnPropertyNames, 'function getOwnPropertyNames() { [native code] }');\n\n";

    // Patch Object.getOwnPropertyDescriptor
    patchModule << "Object.getOwnPropertyDescriptor = function(obj, prop) {\n";
    patchModule << "  if (_pf.has(obj) && prop === 'prototype') return undefined;\n";
    patchModule << "  return _oC.call(_oGOPD, Object, obj, prop);\n";
    patchModule << "};\n";
    patchModule << "_pf.set(Object.getOwnPropertyDescriptor, 'function getOwnPropertyDescriptor() { [native code] }');\n\n";

    // Patch Object.getOwnPropertyDescriptors
    patchModule << "Object.getOwnPropertyDescriptors = function(obj) {\n";
    patchModule << "  var descs = _oC.call(_oGOPDs, Object, obj);\n";
    patchModule << "  if (_pf.has(obj)) delete descs.prototype;\n";
    patchModule << "  return descs;\n";
    patchModule << "};\n";
    patchModule << "_pf.set(Object.getOwnPropertyDescriptors, 'function getOwnPropertyDescriptors() { [native code] }');\n\n";

    // Patch Reflect.ownKeys
    patchModule << "Reflect.ownKeys = function(obj) {\n";
    patchModule << "  var keys = _oC.call(_oROK, Reflect, obj);\n";
    patchModule << "  if (_pf.has(obj)) return keys.filter(function(k) { return k !== 'prototype'; });\n";
    patchModule << "  return keys;\n";
    patchModule << "};\n";
    patchModule << "_pf.set(Reflect.ownKeys, 'function ownKeys() { [native code] }');\n\n";

    // Patch Object.prototype.hasOwnProperty
    patchModule << "Object.prototype.hasOwnProperty = function(prop) {\n";
    patchModule << "  if (_pf.has(this) && prop === 'prototype') return false;\n";
    patchModule << "  return _oC.call(_oHOP, this, prop);\n";
    patchModule << "};\n";
    patchModule << "_pf.set(Object.prototype.hasOwnProperty, 'function hasOwnProperty() { [native code] }');\n\n";

    // Patch Reflect.has
    patchModule << "Reflect.has = function(obj, prop) {\n";
    patchModule << "  if (_pf.has(obj) && prop === 'prototype') return false;\n";
    patchModule << "  return _oC.call(_oRH, Reflect, obj, prop);\n";
    patchModule << "};\n";
    patchModule << "_pf.set(Reflect.has, 'function has() { [native code] }');\n\n";

    // Patch Object.setPrototypeOf for recursion detection
    patchModule << "Object.setPrototypeOf = function(obj, proto) {\n";
    patchModule << "  if (proto === obj) throw new TypeError('Cyclic __proto__ value');\n";
    patchModule << "  var c = proto, d = 0;\n";
    patchModule << "  while (c !== null && c !== undefined && d < 50) {\n";
    patchModule << "    if (c === obj) throw new TypeError('Cyclic __proto__ value');\n";
    patchModule << "    try { c = Reflect.getPrototypeOf(c); } catch(e) { break; }\n";
    patchModule << "    d++;\n";
    patchModule << "  }\n";
    patchModule << "  return _oC.call(_oSP, Object, obj, proto);\n";
    patchModule << "};\n";
    patchModule << "_pf.set(Object.setPrototypeOf, 'function setPrototypeOf() { [native code] }');\n\n";

    // Patch Reflect.setPrototypeOf
    patchModule << "Reflect.setPrototypeOf = function(obj, proto) {\n";
    patchModule << "  if (proto === obj) throw new TypeError('Cyclic __proto__ value');\n";
    patchModule << "  var c = proto, d = 0;\n";
    patchModule << "  while (c !== null && c !== undefined && d < 50) {\n";
    patchModule << "    if (c === obj) throw new TypeError('Cyclic __proto__ value');\n";
    patchModule << "    try { c = Reflect.getPrototypeOf(c); } catch(e) { break; }\n";
    patchModule << "    d++;\n";
    patchModule << "  }\n";
    patchModule << "  return _oC.call(_oRSP, Reflect, obj, proto);\n";
    patchModule << "};\n";
    patchModule << "_pf.set(Reflect.setPrototypeOf, 'function setPrototypeOf() { [native code] }');\n\n";

    // Now patch navigator properties using method shorthand getters
    patchModule << "try {\n";
    patchModule << "  Object.defineProperty(_navProto, 'hardwareConcurrency', {\n";
    patchModule << "    get: _mkGetter('hardwareConcurrency', " << vm.cpu.hardware_concurrency << ", false),\n";
    patchModule << "    configurable: true,\n";
    patchModule << "    enumerable: true\n";
    patchModule << "  });\n";
    patchModule << "} catch(e) {}\n";

    patchModule << "try {\n";
    patchModule << "  Object.defineProperty(_navProto, 'platform', {\n";
    patchModule << "    get: _mkGetter('platform', '" << EscapeJS(vm.os.platform) << "', false),\n";
    patchModule << "    configurable: true,\n";
    patchModule << "    enumerable: true\n";
    patchModule << "  });\n";
    patchModule << "} catch(e) {}\n";

    patchModule << "try {\n";
    patchModule << "  Object.defineProperty(_navProto, 'deviceMemory', {\n";
    patchModule << "    get: _mkGetter('deviceMemory', " << vm.cpu.device_memory << ", false),\n";
    patchModule << "    configurable: true,\n";
    patchModule << "    enumerable: true\n";
    patchModule << "  });\n";
    patchModule << "} catch(e) {}\n";

    patchModule << "try {\n";
    patchModule << "  Object.defineProperty(_navProto, 'userAgent', {\n";
    patchModule << "    get: _mkGetter('userAgent', '" << EscapeJS(vm.browser.user_agent) << "', false),\n";
    patchModule << "    configurable: true,\n";
    patchModule << "    enumerable: true\n";
    patchModule << "  });\n";
    patchModule << "} catch(e) {}\n";

    patchModule << "try {\n";
    patchModule << "  Object.defineProperty(_navProto, 'vendor', {\n";
    patchModule << "    get: _mkGetter('vendor', '" << EscapeJS(vm.browser.vendor) << "', false),\n";
    patchModule << "    configurable: true,\n";
    patchModule << "    enumerable: true\n";
    patchModule << "  });\n";
    patchModule << "} catch(e) {}\n";

    patchModule << "try {\n";
    patchModule << "  Object.defineProperty(_navProto, 'language', {\n";
    patchModule << "    get: _mkGetter('language', '" << EscapeJS(vm.language.primary) << "', false),\n";
    patchModule << "    configurable: true,\n";
    patchModule << "    enumerable: true\n";
    patchModule << "  });\n";
    patchModule << "} catch(e) {}\n";

    // Patch languages (array)
    patchModule << "try {\n";
    patchModule << "  var _langs = ['" << EscapeJS(vm.language.primary) << "'";
    for (size_t i = 1; i < vm.language.languages.size() && i < 3; i++) {
        patchModule << ",'" << EscapeJS(vm.language.languages[i]) << "'";
    }
    patchModule << "];\n";
    patchModule << "  Object.defineProperty(_navProto, 'languages', {\n";
    patchModule << "    get: _mkGetter('languages', _langs, true),\n";
    patchModule << "    configurable: true,\n";
    patchModule << "    enumerable: true\n";
    patchModule << "  });\n";
    patchModule << "} catch(e) {}\n";

    // Patch userAgentData - critical for CreepJS detection
    patchModule << "try {\n";
    patchModule << "  if (navigator.userAgentData || typeof NavigatorUAData !== 'undefined') {\n";
    patchModule << "    var _uad = {\n";
    patchModule << "      platform: '" << EscapeJS(osName) << "',\n";
    patchModule << "      mobile: false,\n";
    patchModule << "      brands: Object.freeze([\n";
    patchModule << "        Object.freeze({brand: 'Chromium', version: '" << EscapeJS(vm.browser.version) << "'}),\n";
    patchModule << "        Object.freeze({brand: 'Not)A;Brand', version: '99'}),\n";
    patchModule << "        Object.freeze({brand: 'Google Chrome', version: '" << EscapeJS(vm.browser.version) << "'})\n";
    patchModule << "      ]),\n";
    patchModule << "      getHighEntropyValues: function(hints) {\n";
    patchModule << "        return Promise.resolve({\n";
    patchModule << "          platform: '" << EscapeJS(osName) << "',\n";
    patchModule << "          platformVersion: '" << EscapeJS(vm.os.version) << "',\n";
    patchModule << "          architecture: 'x86',\n";
    patchModule << "          bitness: '64',\n";
    patchModule << "          model: '',\n";
    patchModule << "          uaFullVersion: '" << EscapeJS(vm.browser.version) << ".0.0.0',\n";
    patchModule << "          fullVersionList: [\n";
    patchModule << "            {brand: 'Chromium', version: '" << EscapeJS(vm.browser.version) << ".0.0.0'},\n";
    patchModule << "            {brand: 'Not)A;Brand', version: '99.0.0.0'},\n";
    patchModule << "            {brand: 'Google Chrome', version: '" << EscapeJS(vm.browser.version) << ".0.0.0'}\n";
    patchModule << "          ]\n";
    patchModule << "        });\n";
    patchModule << "      },\n";
    patchModule << "      toJSON: function() { return {brands: this.brands, mobile: this.mobile, platform: this.platform}; }\n";
    patchModule << "    };\n";
    patchModule << "    Object.freeze(_uad);\n";
    patchModule << "    Object.defineProperty(_navProto, 'userAgentData', {\n";
    patchModule << "      get: _mkGetter('userAgentData', _uad, false),\n";
    patchModule << "      configurable: true,\n";
    patchModule << "      enumerable: true\n";
    patchModule << "    });\n";
    patchModule << "  }\n";
    patchModule << "} catch(e) {}\n";

    // Export _pf and createNativeProxy to _owl namespace for other scripts
    patchModule << "self[_owl]._patchedFunctions = _pf;\n";
    patchModule << "self[_owl].registerNative = function(fn, nativeString) {\n";
    patchModule << "  var str = nativeString || ('function ' + (fn.name || 'anonymous') + '() { [native code] }');\n";
    patchModule << "  _pf.set(fn, str);\n";
    patchModule << "};\n";
    patchModule << "self[_owl].createNativeProxy = function(original, applyHandler, nameOverride) {\n";
    patchModule << "  var effectiveName = nameOverride || original.name;\n";
    patchModule << "  var nativeString = 'function ' + effectiveName + '() { [native code] }';\n";
    patchModule << "  var wrapper = {[effectiveName](...args) {\n";
    patchModule << "    return applyHandler(original, this, args);\n";
    patchModule << "  }}[effectiveName];\n";
    patchModule << "  _pf.set(wrapper, nativeString);\n";
    patchModule << "  return wrapper;\n";
    patchModule << "};\n\n";

    patchModule << "self.__owlEarlyESMPatched = true;\n";

    // Mark as ES module with empty export
    patchModule << "export {};\n";

    // URL-encode the module code for embedding in data URL
    std::string moduleCode = patchModule.str();
    std::string encodedModule;
    for (unsigned char c : moduleCode) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~' || c == ' ' || c == '\n' || c == '\t') {
            if (c == ' ') {
                encodedModule += "%20";
            } else if (c == '\n') {
                encodedModule += "%0A";
            } else if (c == '\t') {
                encodedModule += "%09";
            } else {
                encodedModule += c;
            }
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", c);
            encodedModule += hex;
        }
    }

    // Generate the import statement with data URL
    ss << "import 'data:text/javascript," << encodedModule << "';\n";

    LOG_INFO("SpoofManager", "Generated ES module early patch import, data URL length: " +
             std::to_string(encodedModule.length()));

    return ss.str();
}

// =============================================================================
// IFRAME INTERCEPTION
// =============================================================================

std::string SpoofManager::GenerateIframeInterceptor(const VirtualMachine& vm) {
    std::stringstream ss;

    // Generate a minified iframe spoofing script that includes createNativeProxy
    // This will be prepended to srcdoc content
    ss << R"JS(
// ============================================================
// IFRAME CREATION INTERCEPTION
// Intercepts srcdoc setter to prepend spoofing BEFORE iframe script runs
// ============================================================
(function() {
    'use strict';
    const _owl = Symbol.for('owl');

    // Guard: Skip if already patched
    if (!self[_owl]?.checkGuard('iframe_intercept')) return;
    if (typeof document === 'undefined') return;
    if (typeof HTMLIFrameElement === 'undefined') return;

    const vm = self[_owl]?.vm;
    if (!vm) return;

    // Generate iframe spoofing script with createNativeProxy
    const iframeSpoofScript = (function() {
        // Minified createNativeProxy for iframes
        var core = 'var _pf=new WeakMap(),_oTS=Function.prototype.toString,_oC=Function.prototype.call,_oSP=Object.setPrototypeOf,_oRSP=typeof Reflect!=="undefined"?Reflect.setPrototypeOf:null;';
        core += 'var _cnp=function(o,h,n){var w=(function(){}).bind(),s="function "+(n||o.name||"anonymous")+"() { [native code] }",cp=null,cc=false;';
        core += 'var hc=function(){if(cp===null||cc)return false;cc=true;try{var c=cp,v=new WeakSet(),d=0;while(c!==null&&c!==undefined&&d<50){if(c===p)return true;if(v.has(c))return true;v.add(c);try{var np=Reflect.getPrototypeOf(c);if(np===p)return true;c=np;}catch(e){break;}d++;}return false;}finally{cc=false;}};';
        core += 'var p=new Proxy(w,{apply:function(t,a,r){return h(o,a,r);},get:function(t,k,r){if(k==="prototype")return undefined;if(k==="length")return o.length;if(k==="name")return n||o.name;if(k===Symbol.toStringTag)return"Function";if(k==="__proto__"){if(hc())throw new TypeError("Cyclic __proto__ value");return cp!==null?cp:Function.prototype;}if(k==="constructor")return Function;if(hc())throw new TypeError("Cyclic __proto__ value");return Reflect.get(cp!==null?cp:Function.prototype,k,r);},';
        core += 'set:function(t,k,v,r){if(k==="prototype"||k==="length"||k==="name")return false;if(k==="__proto__"){if(v===p)throw new TypeError("Cyclic __proto__ value");var c=v,d=0;while(c!==null&&c!==undefined&&d<50){if(c===p)throw new TypeError("Cyclic __proto__ value");try{c=Reflect.getPrototypeOf(c);}catch(e){break;}d++;}cp=v;return true;}return Reflect.set(w,k,v,w);},';
        core += 'getPrototypeOf:function(){if(cc)return cp!==null?cp:Function.prototype;if(hc())throw new TypeError("Cyclic __proto__ value");return cp!==null?cp:Function.prototype;},';
        core += 'setPrototypeOf:function(t,pr){if(pr===p)throw new TypeError("Cyclic __proto__ value");var c=pr,d=0;while(c!==null&&c!==undefined&&d<50){if(c===p)throw new TypeError("Cyclic __proto__ value");try{c=Reflect.getPrototypeOf(c);}catch(e){break;}d++;}cp=pr;return true;}});_pf.set(p,s);_pf.set(w,s);return p;};';
        core += 'var _tsp=_cnp(_oTS,function(t,a,r){var m=_pf.get(a);if(m!==undefined)return m;return _oC.call(t,a);},"toString");Function.prototype.toString=_tsp;';
        core += 'var _spp=_cnp(_oSP,function(t,a,r){return _oC.call(t,Object,r[0],r[1]);},"setPrototypeOf");Object.setPrototypeOf=_spp;';
        core += 'if(_oRSP){var _rspp=_cnp(_oRSP,function(t,a,r){return _oC.call(t,Reflect,r[0],r[1]);},"setPrototypeOf");Reflect.setPrototypeOf=_rspp;}';

        // Navigator spoofing using the VM values from parent context
        // VM structure: vm.os.platform, vm.browser.userAgent, vm.cpu.hardwareConcurrency, etc.
        // CRITICAL: Check prototype-level guard first - Navigator.prototype is shared across same-origin iframes
        var navSpoof = 'var np=Object.getPrototypeOf(navigator);';
        navSpoof += 'var _navPatched=Symbol.for("owl_navigator_patched");';
        navSpoof += 'if(!np[_navPatched]){';
        // isRealNavigator using sendBeacon - same as main navigator_spoof.cc
        navSpoof += 'var _osb=Navigator.prototype.sendBeacon;';
        navSpoof += 'var isRealNavigator=function(obj){try{_osb.call(obj,"",null);return true;}catch(e){return !e.message.includes("Illegal invocation");}};';
        navSpoof += 'var sn={platform:"' + (vm.os?.platform || vm.platform || "Win32") + '",userAgent:"' + (vm.browser?.userAgent || vm.userAgent || "") + '",appVersion:"' + ((vm.browser?.userAgent || vm.userAgent || "").substring(8)) + '",vendor:"' + (vm.browser?.vendor || vm.vendor || "Google Inc.") + '",hardwareConcurrency:' + (vm.cpu?.hardwareConcurrency || vm.hardwareConcurrency || 8) + ',deviceMemory:' + (vm.cpu?.deviceMemory || vm.deviceMemory || 8) + ',language:"' + (vm.languages?.[0] || vm.language || "en-US") + '",languages:' + JSON.stringify(vm.languages || [vm.language || "en-US"]) + '};';
        navSpoof += 'Object.keys(sn).forEach(function(k){try{var od=Object.getOwnPropertyDescriptor(np,k);if(od&&od.get){var v=sn[k];var pg=_cnp(od.get,function(t,a,r){if(!isRealNavigator(a))throw new TypeError("Illegal invocation");return k==="languages"?Object.freeze(v.slice?v.slice():v):v;},"get "+k);Object.defineProperty(np,k,{get:pg,configurable:true,enumerable:true});}}catch(e){}});';
        navSpoof += 'try{var wd=Object.getOwnPropertyDescriptor(np,"webdriver");if(wd&&wd.get){var wp=_cnp(wd.get,function(t,a,r){if(!isRealNavigator(a))throw new TypeError("Illegal invocation");return false;},"get webdriver");Object.defineProperty(np,"webdriver",{get:wp,configurable:true,enumerable:true});}}catch(e){}';
        navSpoof += '}'; // Close the if block

        // Initialize owl namespace and claim guards so CEF injection doesn't double-patch
        var owlInit = 'var _o=Symbol.for("owl"),_g=Symbol.for("owl_guards");';
        owlInit += 'if(!self[_o])self[_o]={};if(!self[_g])self[_g]=new Set();';
        owlInit += 'self[_o].guards=self[_g];';
        owlInit += 'self[_o].checkGuard=function(n){if(self[_g].has(n))return false;self[_g].add(n);return true;};';
        // Claim all the guards that our script handles
        owlInit += 'self[_g].add("navigator");self[_g].add("utils");self[_g].add("blob_intercept");';
        owlInit += 'self[_g].add("worker_intercept");self[_g].add("iframe_intercept");';
        owlInit += 'self[_o].utilsReady=true;self[_o].vmEmbedded=true;';

        // Skip entirely if CEF already ran (check utilsReady before doing anything)
        return '<script>(function(){"use strict";if(self.__owlIframePatched)return;var _oc=Symbol.for("owl");if(self[_oc]?.utilsReady){self.__owlIframePatched=true;return;}self.__owlIframePatched=true;' + owlInit + core + navSpoof + '})();<\/script>';
    })();

    // Intercept HTMLIFrameElement.srcdoc setter to prepend spoofing script
    const origSrcdocDesc = Object.getOwnPropertyDescriptor(HTMLIFrameElement.prototype, 'srcdoc');
    if (origSrcdocDesc && origSrcdocDesc.set) {
        const origSet = origSrcdocDesc.set;
        Object.defineProperty(HTMLIFrameElement.prototype, 'srcdoc', {
            get: origSrcdocDesc.get,
            set: function(value) {
                if (typeof value === 'string' && value.length > 0) {
                    // Prepend spoofing script right after <html> or at the start
                    if (value.indexOf('__owlIframePatched') < 0) {
                        const insertPoint = value.toLowerCase().indexOf('<head');
                        if (insertPoint >= 0) {
                            const headEnd = value.indexOf('>', insertPoint);
                            if (headEnd >= 0) {
                                value = value.slice(0, headEnd + 1) + iframeSpoofScript + value.slice(headEnd + 1);
                            }
                        } else {
                            const bodyPoint = value.toLowerCase().indexOf('<body');
                            if (bodyPoint >= 0) {
                                const bodyEnd = value.indexOf('>', bodyPoint);
                                if (bodyEnd >= 0) {
                                    value = value.slice(0, bodyEnd + 1) + iframeSpoofScript + value.slice(bodyEnd + 1);
                                }
                            } else {
                                // No head or body tag, prepend to start
                                value = iframeSpoofScript + value;
                            }
                        }
                    }
                }
                return origSet.call(this, value);
            },
            configurable: true,
            enumerable: true
        });
    }

    // Also intercept setAttribute for srcdoc - use createNativeProxy to pass introspection tests
    const origSetAttribute = Element.prototype.setAttribute;
    const createNativeProxy = self[_owl]?.createNativeProxy;

    if (createNativeProxy) {
        Element.prototype.setAttribute = createNativeProxy(origSetAttribute, (target, thisArg, args) => {
            let [name, value] = args;
            if (thisArg instanceof HTMLIFrameElement && name?.toLowerCase?.() === 'srcdoc' && typeof value === 'string') {
                if (value.length > 0 && value.indexOf('__owlIframePatched') < 0) {
                    const insertPoint = value.toLowerCase().indexOf('<head');
                    if (insertPoint >= 0) {
                        const headEnd = value.indexOf('>', insertPoint);
                        if (headEnd >= 0) {
                            value = value.slice(0, headEnd + 1) + iframeSpoofScript + value.slice(headEnd + 1);
                        }
                    } else {
                        const bodyPoint = value.toLowerCase().indexOf('<body');
                        if (bodyPoint >= 0) {
                            const bodyEnd = value.indexOf('>', bodyPoint);
                            if (bodyEnd >= 0) {
                                value = value.slice(0, bodyEnd + 1) + iframeSpoofScript + value.slice(bodyEnd + 1);
                            }
                        } else {
                            value = iframeSpoofScript + value;
                        }
                    }
                }
            }
            return target.call(thisArg, name, value);
        }, 'setAttribute');
    } else {
        // Fallback if createNativeProxy not available
        Element.prototype.setAttribute = function(name, value) {
            if (this instanceof HTMLIFrameElement && name.toLowerCase() === 'srcdoc' && typeof value === 'string') {
                if (value.length > 0 && value.indexOf('__owlIframePatched') < 0) {
                    const insertPoint = value.toLowerCase().indexOf('<head');
                    if (insertPoint >= 0) {
                        const headEnd = value.indexOf('>', insertPoint);
                        if (headEnd >= 0) {
                            value = value.slice(0, headEnd + 1) + iframeSpoofScript + value.slice(headEnd + 1);
                        }
                    } else {
                        const bodyPoint = value.toLowerCase().indexOf('<body');
                        if (bodyPoint >= 0) {
                            const bodyEnd = value.indexOf('>', bodyPoint);
                            if (bodyEnd >= 0) {
                                value = value.slice(0, bodyEnd + 1) + iframeSpoofScript + value.slice(bodyEnd + 1);
                            }
                        } else {
                            value = iframeSpoofScript + value;
                        }
                    }
                }
            }
            return origSetAttribute.call(this, name, value);
        };
        // Register for toString masking
        if (self[_owl]?.registerNative) {
            self[_owl].registerNative(Element.prototype.setAttribute, 'function setAttribute() { [native code] }');
        }
    }
})();
)JS";

    return ss.str();
}

// =============================================================================
// MAIN INJECTION METHODS
// =============================================================================

void SpoofManager::InjectForFrame(CefRefPtr<CefFrame> frame, 
                                   const VirtualMachine& vm,
                                   const std::string& context_id) {
    if (!frame) {
        LOG_DEBUG("SpoofManager", "InjectForFrame: null frame");
        return;
    }
    
    ContextType ctx = frame->IsMain() ? ContextType::MAIN_FRAME : ContextType::IFRAME;
    LayerConfig config = GetLayerConfig(ctx);
    
    LOG_DEBUG("SpoofManager", "InjectForFrame: " + std::string(ContextTypeName(ctx)) +
              " vm=" + vm.id + " frame_id=" + frame->GetIdentifier().ToString());
    
    std::stringstream ss;
    
    // 1. Context detection (sets up owl namespace and guards)
    ss << GenerateContextDetector();
    
    // 2. Guard system extension
    ss << GenerateGuardScript();
    
    // 3. Core utilities (createNativeProxy, toString masking)
    ss << GenerateUtilitiesScript();
    
    // 4. VM profile embedding
    ss << GenerateVMProfileScript(vm, context_id);
    
    // 5. Apply spoofs based on layer config
    if (config.navigator) {
        ss << GenerateNavigatorScript(vm, ctx);
    }
    
    if (config.screen) {
        ss << GenerateScreenScript(vm);
    }
    
    if (config.canvas) {
        ss << GenerateCanvasScript(vm, ctx);
    }
    
    if (config.webgl) {
        ss << GenerateWebGLScript(vm, ctx);
    }
    
    if (config.audio) {
        ss << GenerateAudioScript(vm, ctx);
    }

    if (config.timezone) {
        ss << GenerateTimezoneScript(vm);
    }

    if (config.fonts) {
        ss << FontSpoofer::GenerateScript(vm);
    }

    // 6. Blob URL interception (CRITICAL - must be before worker interception)
    // This intercepts Blob constructor to prepend spoofing code to worker scripts
    if (config.has_dom) {
        ss << GenerateBlobURLInterceptor(vm);
    }

    // 7. Worker interception (for spawning workers from this context)
    ss << GenerateWorkerInterceptor(vm);

    // 8. Iframe interception (for dynamically created iframes)
    if (config.has_dom) {
        ss << GenerateIframeInterceptor(vm);
    }
    
    // Execute the complete script
    std::string script = ss.str();
    LOG_DEBUG("SpoofManager", "Injecting " + std::to_string(script.length()) +
              " bytes of spoof script");

    frame->ExecuteJavaScript(script, frame->GetURL(), 0);
}

std::string SpoofManager::GenerateFrameScript(bool is_main_frame,
                                               const VirtualMachine& vm,
                                               const std::string& context_id) {
    ContextType ctx = is_main_frame ? ContextType::MAIN_FRAME : ContextType::IFRAME;
    LayerConfig config = GetLayerConfig(ctx);

    LOG_DEBUG("SpoofManager", "GenerateFrameScript: " + std::string(ContextTypeName(ctx)) +
              " vm=" + vm.id);

    std::stringstream ss;

    // 1. Context detection (sets up owl namespace and guards)
    ss << GenerateContextDetector();

    // 2. Guard system extension
    ss << GenerateGuardScript();

    // 3. Core utilities (createNativeProxy, toString masking)
    ss << GenerateUtilitiesScript();

    // 4. VM profile embedding
    ss << GenerateVMProfileScript(vm, context_id);

    // 5. Apply spoofs based on layer config
    if (config.navigator) {
        ss << GenerateNavigatorScript(vm, ctx);
    }

    if (config.screen) {
        ss << GenerateScreenScript(vm);
    }

    if (config.canvas) {
        ss << GenerateCanvasScript(vm, ctx);
    }

    if (config.webgl) {
        ss << GenerateWebGLScript(vm, ctx);
    }

    if (config.audio) {
        ss << GenerateAudioScript(vm, ctx);
    }

    if (config.timezone) {
        ss << GenerateTimezoneScript(vm);
    }

    if (config.fonts) {
        ss << FontSpoofer::GenerateScript(vm);
    }

    // 6. Blob URL interception (CRITICAL - must be before worker interception)
    // This intercepts Blob constructor to prepend spoofing code to worker scripts
    if (config.has_dom) {
        ss << GenerateBlobURLInterceptor(vm);
    }

    // 7. Worker interception (for spawning workers from this context)
    ss << GenerateWorkerInterceptor(vm);

    // 8. Iframe interception (for dynamically created iframes)
    if (config.has_dom) {
        ss << GenerateIframeInterceptor(vm);
    }

    std::string script = ss.str();
    LOG_DEBUG("SpoofManager", "Generated " + std::to_string(script.length()) +
              " bytes of spoof script for frame");

    return script;
}

std::string SpoofManager::GenerateWorkerScript(const VirtualMachine& vm,
                                                ContextType worker_type) {
    LayerConfig config = GetLayerConfig(worker_type);

    std::stringstream ss;

    // Worker spoofing script wrapper
    ss << R"JS(
// ============================================================
// OWL WORKER SPOOFING
// ============================================================
try {
)JS";

    // 1. Context detection
    ss << GenerateContextDetector();

    // 2. Guard system
    ss << GenerateGuardScript();

    // 3. Core utilities
    ss << GenerateUtilitiesScript();

    // 4. VM profile
    ss << GenerateVMProfileScript(vm, "");

    // 5. Apply applicable spoofs
    if (config.navigator) {
        // Worker has WorkerNavigator, not Navigator
        ss << R"JS(
// ============================================================
// WORKER NAVIGATOR SPOOFING
// WorkerNavigator has subset of Navigator properties
// ============================================================
(function() {
    'use strict';
    try {
        const _owl = Symbol.for('owl');
        const _vm = Symbol.for('owl_vm');

        if (!self[_owl]?.checkGuard('navigator')) return;

        const vm = self[_vm];
        if (!vm) return;

        const createNativeProxy = self[_owl]?.createNativeProxy;
        if (!createNativeProxy) return;

        const navProto = Object.getPrototypeOf(self.navigator);

        // Detect real navigator vs prototype access
        // Try sendBeacon first (works in SharedWorker/ServiceWorker), fallback to prototype check
        const _origSendBeacon = navProto.sendBeacon;
        const isRealWorkerNavigator = (obj) => {
            // Quick check: if obj IS the prototype, it's definitely not a real navigator
            if (obj === navProto) return false;
            // If sendBeacon exists, use it for robust detection
            if (typeof _origSendBeacon === 'function') {
                try {
                    _origSendBeacon.call(obj, '', null);
                    return true;
                } catch (e) {
                    return !e.message.includes('Illegal invocation');
                }
            }
            // Fallback: check if obj is the actual navigator instance
            return obj === self.navigator;
        };

        // Properties available in WorkerNavigator
        const workerNavProps = {
            userAgent: vm.browser?.userAgent,
            platform: vm.os?.platform,
            hardwareConcurrency: vm.cpu?.hardwareConcurrency,
            deviceMemory: vm.cpu?.deviceMemory,
            language: vm.languages?.[0],
            languages: vm.languages
        };

        for (const [prop, value] of Object.entries(workerNavProps)) {
            if (value === undefined) continue;

            try {
                const origDesc = Object.getOwnPropertyDescriptor(navProto, prop);
                if (origDesc && origDesc.get) {
                    const proxyGetter = createNativeProxy(origDesc.get, (target, thisArg, args) => {
                        // CRITICAL: Must throw "Illegal invocation" for prototype access
                        // Use sendBeacon-based check (same as main frame) to handle all worker types
                        if (!isRealWorkerNavigator(thisArg)) {
                            throw new TypeError('Illegal invocation');
                        }
                        return prop === 'languages' ? Object.freeze([...value]) : value;
                    }, `get ${prop}`);

                    Object.defineProperty(navProto, prop, {
                        get: proxyGetter,
                        configurable: true,
                        enumerable: true
                    });
                }
            } catch (e) {
                // Error patching property
            }
        }

        // Spoof userAgentData in workers
        // Check navigator.userAgentData existence (NavigatorUAData constructor may not exist in workers)
        if (self.navigator.userAgentData) {
            try {
                const osName = vm.os?.name || 'Windows';
                const browserVersion = vm.browser?.version?.toString().split('.')[0] || '143';

                const uaData = {
                    platform: osName === 'Windows' ? 'Windows' : osName,
                    mobile: false,
                    brands: Object.freeze([
                        Object.freeze({brand: 'Chromium', version: browserVersion}),
                        Object.freeze({brand: 'Not)A;Brand', version: '99'}),
                        Object.freeze({brand: 'Google Chrome', version: browserVersion})
                    ]),
                    getHighEntropyValues: function(hints) {
                        return Promise.resolve({
                            platform: osName === 'Windows' ? 'Windows' : osName,
                            platformVersion: vm.os?.version || '10.0.0',
                            architecture: 'x86',
                            bitness: '64',
                            model: '',
                            uaFullVersion: browserVersion + '.0.0.0',
                            fullVersionList: [
                                {brand: 'Chromium', version: browserVersion + '.0.0.0'},
                                {brand: 'Not)A;Brand', version: '99.0.0.0'},
                                {brand: 'Google Chrome', version: browserVersion + '.0.0.0'}
                            ]
                        });
                    },
                    toJSON: function() {
                        return {
                            brands: this.brands,
                            mobile: this.mobile,
                            platform: this.platform
                        };
                    }
                };

                // Use Object.getPrototypeOf to get prototype dynamically (NavigatorUAData.prototype may not exist in workers)
                Object.setPrototypeOf(uaData, Object.getPrototypeOf(self.navigator.userAgentData));
                Object.freeze(uaData);

                Object.defineProperty(navProto, 'userAgentData', {
                    get: createNativeProxy(
                        Object.getOwnPropertyDescriptor(navProto, 'userAgentData')?.get || (() => uaData),
                        (target, thisArg, args) => {
                            // CRITICAL: Must throw "Illegal invocation" for prototype access
                            if (!isRealWorkerNavigator(thisArg)) {
                                throw new TypeError('Illegal invocation');
                            }
                            return uaData;
                        },
                        'get userAgentData'
                    ),
                    configurable: true,
                    enumerable: true
                });
            } catch (e) {
                // Error spoofing userAgentData
            }
        }
    } catch (e) {
        // Navigator spoofing error
    }
})();
)JS";
    }

    if (config.canvas) {
        // OffscreenCanvas spoofing for workers
        ss << R"JS(
// ============================================================
// OFFSCREEN CANVAS SPOOFING (Worker)
// ============================================================
(function() {
    'use strict';
    try {
        const _owl = Symbol.for('owl');
        const _vm = Symbol.for('owl_vm');

        if (!self[_owl]?.checkGuard('canvas')) return;
        if (typeof OffscreenCanvas === 'undefined') return;

        const vm = self[_vm];
        const seed = vm?.seeds?.canvas ? BigInt(vm.seeds.canvas) : 0n;

        const createNativeProxy = self[_owl]?.createNativeProxy;
        if (!createNativeProxy) return;

        // Simple noise application
        const applyNoise = (data, width, seed) => {
            if (!seed || seed === 0n) return;
            let state = Number(seed & 0xFFFFFFFFn) || 0x12345678;
            const nextRand = () => {
                state ^= state << 13;
                state ^= state >>> 17;
                state ^= state << 5;
                return (state >>> 0) / 0xFFFFFFFF;
            };
            for (let i = 0; i < 10; i++) nextRand();

            const len = data.length;
            for (let i = 0; i < len; i += 4) {
                if (data[i + 3] === 0) continue;
                const noise = (nextRand() < 0.1) ? (nextRand() < 0.5 ? -1 : 1) : 0;
                if (noise !== 0) {
                    data[i] = Math.max(0, Math.min(255, data[i] + noise));
                    data[i+1] = Math.max(0, Math.min(255, data[i+1] + noise));
                    data[i+2] = Math.max(0, Math.min(255, data[i+2] + noise));
                }
            }
        };

        // Hook OffscreenCanvas.convertToBlob
        const origConvertToBlob = OffscreenCanvas.prototype.convertToBlob;
        if (origConvertToBlob) {
            const convertToBlobProxy = createNativeProxy(origConvertToBlob, (target, thisArg, args) => {
                try {
                    if (seed && thisArg.width > 0 && thisArg.height > 0) {
                        const ctx = thisArg.getContext('2d');
                        if (ctx) {
                            const imageData = ctx.getImageData(0, 0, thisArg.width, thisArg.height);
                            applyNoise(imageData.data, imageData.width, seed);
                            ctx.putImageData(imageData, 0, 0);
                        }
                    }
                } catch (e) {}
                return origConvertToBlob.apply(thisArg, args);
            }, 'convertToBlob');

            Object.defineProperty(OffscreenCanvas.prototype, 'convertToBlob', {
                value: convertToBlobProxy, writable: true, configurable: true
            });
        }
    } catch (e) {
        // Canvas spoofing error
    }
})();
)JS";
    }
    
    if (config.timezone) {
        ss << GenerateTimezoneScript(vm);
    }
    
    if (config.audio) {
        ss << GenerateAudioScript(vm, worker_type);
    }
    
    // 6. Blob URL interception for nested workers (CRITICAL!)
    // Workers can also create blob workers, so we need to intercept in workers too
    ss << GenerateBlobURLInterceptor(vm);

    // 7. Message handler for receiving VM config from parent
    ss << R"JS(
// ============================================================
// VM CONFIG MESSAGE HANDLER
// Receives VM config from parent context via postMessage
// ============================================================
(function() {
    'use strict';
    const _owl = Symbol.for('owl');
    
    if (typeof self.onmessage !== 'undefined') {
        // Don't override existing handler
        return;
    }
    
    // Listen for __owl_init__ message
    self.addEventListener('message', function owlInitHandler(event) {
        if (event.data && event.data.__owl_init__) {
            // VM config received - but we've already been injected by response filter
            // This is just a backup mechanism
            self.removeEventListener('message', owlInitHandler);
        }
    }, { once: false });
})();
)JS";

    // Close try/catch wrapper
    ss << R"JS(
} catch (__owl_top_error) {
    // Top-level error caught
}
)JS";

    return ss.str();
}

// =============================================================================
// UTILITY METHODS
// =============================================================================

std::string SpoofManager::EscapeJS(const std::string& str) {
    std::stringstream ss;
    for (char c : str) {
        switch (c) {
            case '\\': ss << "\\\\"; break;
            case '"': ss << "\\\""; break;
            case '\'': ss << "\\'"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            case '\0': break; // Skip null bytes
            default: 
                if (static_cast<unsigned char>(c) < 32) {
                    // Escape other control characters
                    ss << "\\x" << std::hex << std::setw(2) << std::setfill('0') 
                       << static_cast<int>(static_cast<unsigned char>(c));
                } else {
                    ss << c;
                }
                break;
        }
    }
    return ss.str();
}

std::string SpoofManager::VectorToJSArray(const std::vector<std::string>& vec) {
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
