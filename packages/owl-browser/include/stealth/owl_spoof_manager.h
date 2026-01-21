#pragma once

/**
 * OwlSpoofManager: Layer-Based Spoofing Management System
 * 
 * This system manages spoofing across ALL browser execution contexts:
 * - Main Frame (top-level document)
 * - IFrames (same-origin and cross-origin)
 * - Web Workers (Dedicated, Shared, Service)
 * - Worklets (Audio, Paint, Animation)
 * 
 * DESIGN PRINCIPLES:
 * 1. Single VM Profile: One VirtualMachine shared across ALL layers
 * 2. No Conflicts: Guard system prevents re-patching
 * 3. Nested Support: Child contexts inherit parent's profile
 * 4. Context Detection: Automatic layer detection
 * 5. Modular: Uses existing spoof modules from src/stealth/spoofs/
 * 
 * ARCHITECTURE:
 * ┌─────────────────────────────────────────────────────────────┐
 * │  MAIN FRAME (Top-Level Document)                            │
 * │  ├── IFRAME (Same-Origin) - shares prototype patches        │
 * │  │   └── Nested IFRAME                                      │
 * │  ├── IFRAME (Cross-Origin) - separate context, re-inject    │
 * │  ├── DEDICATED WORKER - inject via script interception      │
 * │  │   └── Nested WORKER (spawned from worker)                │
 * │  ├── SHARED WORKER - inject via response filter             │
 * │  ├── SERVICE WORKER - inject via response filter            │
 * │  └── WORKLET - limited API, separate handling               │
 * └─────────────────────────────────────────────────────────────┘
 */

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "include/cef_frame.h"

namespace owl {

// Forward declarations
struct VirtualMachine;
struct StealthConfig;

namespace spoofs {

/**
 * Execution context types for layer-aware spoofing
 */
enum class ContextType {
    MAIN_FRAME,         // Top-level document window
    IFRAME,             // Embedded frame (same or cross-origin)
    DEDICATED_WORKER,   // new Worker()
    SHARED_WORKER,      // new SharedWorker()
    SERVICE_WORKER,     // navigator.serviceWorker.register()
    WORKLET,            // CSS.paintWorklet, Worklet.addModule()
    UNKNOWN
};

/**
 * Layer configuration - defines which spoofs apply in each context
 * Workers don't have access to DOM APIs like screen, document, etc.
 */
struct LayerConfig {
    bool navigator = true;      // Available in all contexts
    bool screen = true;         // NOT available in workers
    bool canvas = true;         // OffscreenCanvas only in workers
    bool webgl = true;          // WebGL on OffscreenCanvas in workers
    bool audio = true;          // AudioContext/OfflineAudioContext
    bool timezone = true;       // Date/Intl available everywhere
    bool fonts = true;          // FontFace API (workers support it)
    
    // Context-specific options
    bool is_worker = false;     // True if worker context
    bool has_dom = true;        // True if DOM available
};

/**
 * SpoofManager: Central manager for all spoofing operations
 * 
 * This singleton provides:
 * - Unified injection for all context types
 * - Guard-based double-patch prevention
 * - VM profile embedding and inheritance
 * - Worker/iframe interception script generation
 */
class SpoofManager {
public:
    // Singleton access
    static SpoofManager& Instance();
    
    /**
     * Main entry point for frame-based contexts (main frame, iframes)
     * Called from OnContextCreated or frame load event.
     * 
     * @param frame The CEF frame to inject into
     * @param vm The VirtualMachine profile to apply
     * @param context_id Unique context identifier for seed generation
     */
    void InjectForFrame(CefRefPtr<CefFrame> frame,
                        const VirtualMachine& vm,
                        const std::string& context_id = "");

    /**
     * Generate complete JavaScript for frame contexts without executing.
     * Use this with context->Eval() for synchronous injection.
     *
     * @param is_main_frame Whether this is the main frame
     * @param vm The VirtualMachine profile to apply
     * @param context_id Unique context identifier for seed generation
     * @return Complete JavaScript string for injection
     */
    std::string GenerateFrameScript(bool is_main_frame,
                                    const VirtualMachine& vm,
                                    const std::string& context_id = "");

    /**
     * Generate complete JavaScript for worker contexts.
     * Called by ServiceWorkerResponseFilter or Worker script injection.
     * 
     * @param vm The VirtualMachine profile
     * @param worker_type Type of worker context
     * @return Complete JavaScript to prepend to worker script
     */
    std::string GenerateWorkerScript(const VirtualMachine& vm,
                                     ContextType worker_type);
    
    /**
     * Generate JavaScript for intercepting nested worker creation.
     * This wraps Worker/SharedWorker constructors to propagate VM profile.
     * 
     * @param vm The VirtualMachine profile
     * @return JavaScript to inject for worker interception
     */
    std::string GenerateWorkerInterceptor(const VirtualMachine& vm);
    
    /**
     * Generate JavaScript for intercepting iframe creation.
     * This hooks document.createElement and iframe.contentWindow access.
     *
     * @param vm The VirtualMachine profile
     * @return JavaScript for iframe interception
     */
    std::string GenerateIframeInterceptor(const VirtualMachine& vm);

    /**
     * Generate JavaScript for intercepting Blob URL creation.
     * This is CRITICAL for worker spoofing - intercepts Blob constructor
     * to prepend spoofing code to JavaScript blobs used for workers.
     *
     * @param vm The VirtualMachine profile
     * @return JavaScript for blob URL interception
     */
    std::string GenerateBlobURLInterceptor(const VirtualMachine& vm);

    /**
     * Generate EARLY blob/worker interception script.
     * This is a STANDALONE script that must run BEFORE any page JavaScript.
     * Unlike GenerateBlobURLInterceptor, this does NOT depend on the owl
     * namespace or guard system - it must be the first script injected.
     *
     * CRITICAL: This fixes the timing bug where page scripts create blob
     * workers before our interception is in place.
     *
     * @param vm The VirtualMachine profile
     * @return Standalone JavaScript for early blob/worker interception
     */
    std::string GenerateEarlyBlobInterceptor(const VirtualMachine& vm);

    /**
     * Generate minimal self-contained worker spoofing script.
     * This is prepended to blob URL workers to spoof Navigator properties.
     *
     * @param vm The VirtualMachine profile
     * @return Minimal JavaScript for worker navigator spoofing
     */
    std::string GenerateWorkerSpoofScript(const VirtualMachine& vm);

    /**
     * Generate ES module early patch import statement.
     * This creates an import statement with a data URL that patches Navigator.prototype
     * BEFORE any other ES module imports are evaluated.
     *
     * CRITICAL: ES modules are hoisted - imports are evaluated before any other code.
     * By prepending an import with our patches, we ensure they run first.
     *
     * @param vm The VirtualMachine profile
     * @return Import statement with data URL, e.g., "import 'data:text/javascript,...';\n"
     */
    std::string GenerateESModuleEarlyPatch(const VirtualMachine& vm);

    /**
     * Get the appropriate layer config for a context type.
     * 
     * @param type The context type
     * @return Configuration for what to spoof in that context
     */
    static LayerConfig GetLayerConfig(ContextType type);
    
    /**
     * Generate context detection JavaScript.
     * This detects which layer we're running in.
     * 
     * @return JavaScript that sets up context detection
     */
    static std::string GenerateContextDetector();
    
    /**
     * Get string name for context type (for logging)
     */
    static const char* ContextTypeName(ContextType type);

private:
    SpoofManager() = default;
    ~SpoofManager() = default;
    
    SpoofManager(const SpoofManager&) = delete;
    SpoofManager& operator=(const SpoofManager&) = delete;
    
    // Script generation helpers
    std::string GenerateUtilitiesScript();
    std::string GenerateGuardScript();
    std::string GenerateVMProfileScript(const VirtualMachine& vm, const std::string& context_id);
    
    // Individual spoof generators (delegates to spoof modules)
    std::string GenerateNavigatorScript(const VirtualMachine& vm, ContextType ctx);
    std::string GenerateScreenScript(const VirtualMachine& vm);
    std::string GenerateCanvasScript(const VirtualMachine& vm, ContextType ctx);
    std::string GenerateWebGLScript(const VirtualMachine& vm, ContextType ctx);
    std::string GenerateAudioScript(const VirtualMachine& vm, ContextType ctx);
    std::string GenerateTimezoneScript(const VirtualMachine& vm);
    
    // Utility
    static std::string EscapeJS(const std::string& str);
    static std::string VectorToJSArray(const std::vector<std::string>& vec);
};

} // namespace spoofs
} // namespace owl
