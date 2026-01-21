#pragma once

#include <string>
#include "include/cef_frame.h"

namespace owl {
namespace spoofs {

/**
 * Spoof Utilities
 * 
 * Provides shared utilities for all spoof implementations:
 * - createNativeProxy: Creates Proxy wrappers that pass all introspection tests
 * - registerNative: Registers functions for toString interception
 * - Guard mechanisms to prevent re-patching
 * 
 * IMPORTANT: This must be injected FIRST before any other spoofs.
 * All spoof files depend on window[Symbol.for('owl')].createNativeProxy
 */
class SpoofUtils {
public:
    /**
     * Inject the core utility functions into the frame.
     * This establishes:
     * - window[Symbol.for('owl')].createNativeProxy
     * - window[Symbol.for('owl')].registerProxy
     * - window[Symbol.for('owl')].registerNative (toString masking)
     * - Guard flags for each spoof category
     * 
     * @param frame The CEF frame to inject into
     * @return true if injection succeeded
     */
    static bool InjectUtilities(CefRefPtr<CefFrame> frame);
    
    /**
     * Check if utilities are already injected in the frame.
     * Uses a flag check via JavaScript execution.
     * 
     * @param frame The CEF frame to check
     * @return true if already injected
     */
    static bool IsInjected(CefRefPtr<CefFrame> frame);
    
    /**
     * Get the JavaScript code for utility injection.
     * This is the raw JS that establishes all shared utilities.
     * 
     * @return JavaScript code string
     */
    static std::string GenerateUtilitiesScript();
    
private:
    // Stack trace fix for toString detection (Error.prepareStackTrace)
    static std::string GenerateStackTraceFixScript();

    // The createNativeProxy implementation
    static std::string GenerateCreateNativeProxyScript();

    // The toString masking registry
    static std::string GenerateToStringMaskingScript();

    // Guard flag management
    static std::string GenerateGuardFlagsScript();
};

} // namespace spoofs
} // namespace owl
