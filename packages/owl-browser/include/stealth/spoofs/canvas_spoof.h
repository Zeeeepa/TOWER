#pragma once

#include <string>
#include <cstdint>
#include "include/cef_frame.h"

namespace owl {
namespace spoofs {

/**
 * Canvas Fingerprint Spoofing
 * 
 * Handles canvas fingerprint noise injection:
 * - HTMLCanvasElement.toDataURL
 * - HTMLCanvasElement.toBlob
 * - CanvasRenderingContext2D.getImageData
 * - CanvasRenderingContext2D.drawImage (for WebGL canvas sources)
 * - OffscreenCanvas.convertToBlob
 * - OffscreenCanvasRenderingContext2D.getImageData
 * 
 * Context-aware: Uses per-context seed symbol for consistent noise
 * across main frame, iframes, and workers.
 * 
 * DEPENDENCIES: Requires SpoofUtils to be injected first.
 * GUARD: Uses window[Symbol.for('owl')].guards.canvas
 */
class CanvasSpoof {
public:
    /**
     * Configuration for canvas spoofing
     */
    struct Config {
        uint64_t seed = 0;              // Canvas noise seed (BigInt in JS)
        bool apply_noise = true;        // Whether to apply noise
        double noise_intensity = 1.0;   // Noise intensity multiplier
        
        // Pre-computed hashes for fingerprint consistency
        std::string geometry_hash;      // For geometry-based fingerprints
        std::string text_hash;          // For text-based fingerprints
    };
    
    /**
     * Inject canvas spoofing into the frame.
     * 
     * @param frame The CEF frame to inject into
     * @param config The configuration for spoofing
     * @return true if injection succeeded
     */
    static bool Inject(CefRefPtr<CefFrame> frame, const Config& config);
    
    /**
     * Generate the JavaScript for canvas spoofing.
     * 
     * @param config The configuration for spoofing
     * @return JavaScript code string
     */
    static std::string GenerateScript(const Config& config);
    
private:
    // Generate noise application function
    static std::string GenerateNoiseFunction();
    
    // Generate HTMLCanvasElement hooks
    static std::string GenerateCanvasElementHooks();
    
    // Generate CanvasRenderingContext2D hooks
    static std::string GenerateContext2DHooks();
    
    // Generate OffscreenCanvas hooks
    static std::string GenerateOffscreenCanvasHooks();
    
    // Generate WebGL canvas handling
    static std::string GenerateWebGLCanvasHooks();
};

} // namespace spoofs
} // namespace owl
