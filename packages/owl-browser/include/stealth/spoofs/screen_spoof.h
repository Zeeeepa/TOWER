#pragma once

#include <string>
#include "include/cef_frame.h"
#include "stealth/owl_virtual_machine.h"

namespace owl {
namespace spoofs {

/**
 * Screen Dimensions Spoofing
 * 
 * Handles screen dimension spoofing:
 * - screen.width, screen.height
 * - screen.availWidth, screen.availHeight
 * - screen.colorDepth, screen.pixelDepth
 * - window.outerWidth, window.outerHeight
 * - window.devicePixelRatio
 * - screen.orientation
 * 
 * DEPENDENCIES: Requires SpoofUtils to be injected first.
 * GUARD: Uses window[Symbol.for('owl')].guards.screen
 */
class ScreenSpoof {
public:
    /**
     * Configuration for screen spoofing
     */
    struct Config {
        int width = 1920;
        int height = 1080;
        int avail_width = 1920;
        int avail_height = 1040;  // Typically less than height (taskbar)
        int color_depth = 24;
        int pixel_depth = 24;
        float device_pixel_ratio = 1.0f;
        std::string orientation_type = "landscape-primary";
        int orientation_angle = 0;
        
        // Window chrome size (for outerWidth/Height)
        int chrome_width = 0;
        int chrome_height = 79;  // Typical browser toolbar height
        
        // Build from VirtualMachine
        static Config FromVM(const VirtualMachine& vm);
    };
    
    /**
     * Inject screen spoofing into the frame.
     * 
     * @param frame The CEF frame to inject into
     * @param config The configuration for spoofing
     * @return true if injection succeeded
     */
    static bool Inject(CefRefPtr<CefFrame> frame, const Config& config);
    
    /**
     * Generate the JavaScript for screen spoofing.
     * 
     * @param config The configuration for spoofing
     * @return JavaScript code string
     */
    static std::string GenerateScript(const Config& config);
    
private:
    // Generate screen object property hooks
    static std::string GenerateScreenPropsHooks(const Config& config);
    
    // Generate window dimension hooks
    static std::string GenerateWindowDimensionHooks(const Config& config);
    
    // Generate screen orientation hooks
    static std::string GenerateOrientationHooks(const Config& config);
    
    // Escape JavaScript string
    static std::string EscapeJS(const std::string& str);
};

} // namespace spoofs
} // namespace owl
