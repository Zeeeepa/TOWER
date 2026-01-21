#pragma once

#include <string>
#include <vector>

namespace owl {

// Forward declaration
struct VirtualMachine;

/**
 * FontSpoofer - Single source of truth for all font fingerprint spoofing
 *
 * This class handles font fingerprint spoofing by:
 * 1. Hiding host OS fonts (e.g., macOS fonts when spoofing to Windows/Linux)
 * 2. Presenting target OS fonts as installed
 * 3. Fixing fontPreferences detection (apple must equal sans on non-macOS)
 * 4. Normalizing measurements for devicePixelRatio differences
 *
 * Detection methods handled:
 * - offsetWidth/offsetHeight - DOM element measurement
 * - getBoundingClientRect - Element dimensions
 * - scrollWidth/clientWidth - Alternative measurements
 * - CanvasRenderingContext2D.measureText - Canvas text measurement
 * - document.fonts.check()/load() - Font loading API
 * - queryLocalFonts() - Chrome's native font enumeration
 * - style.fontFamily / style.font / style.cssText setters
 * - element.setAttribute("style", ...) - Style attribute
 * - getComputedStyle() - Computed style queries
 */
class FontSpoofer {
public:
    /**
     * Configuration for font spoofing
     */
    struct Config {
        std::string target_os;              // Target OS: "Windows", "Linux", "macOS"
        float actual_dpr = 1.0f;            // Actual device pixel ratio (e.g., 2.0 for Retina)
        float spoofed_dpr = 1.0f;           // Spoofed device pixel ratio
        std::vector<std::string> allowed_fonts;  // Explicit list of allowed fonts (optional)
        bool block_mac_fonts = true;        // Block macOS fonts on non-macOS profiles
        bool normalize_measurements = true; // Apply DPR normalization to measurements
        uint64_t fonts_seed = 0;            // Seed for per-context font variation
    };

    /**
     * Generate complete font spoofing JavaScript for the given VM profile
     *
     * @param vm The VirtualMachine profile containing OS and font configuration
     * @return JavaScript code to inject for font spoofing
     */
    static std::string GenerateScript(const VirtualMachine& vm);

    /**
     * Generate complete font spoofing JavaScript with per-context seed
     *
     * @param vm The VirtualMachine profile containing OS and font configuration
     * @param fonts_seed The per-context seed for font variation
     * @return JavaScript code to inject for font spoofing
     */
    static std::string GenerateScript(const VirtualMachine& vm, uint64_t fonts_seed);

    /**
     * Generate font spoofing JavaScript with explicit configuration
     *
     * @param config Configuration specifying target OS, DPR values, etc.
     * @return JavaScript code to inject for font spoofing
     */
    static std::string GenerateScript(const Config& config);

    /**
     * Get the list of fonts that should appear installed for the target OS
     *
     * @param target_os Target OS: "Windows", "Linux", or "macOS"
     * @return Vector of font family names
     */
    static std::vector<std::string> GetTargetOSFonts(const std::string& target_os);

    /**
     * Get the list of macOS-exclusive fonts that must be hidden on other platforms
     *
     * @return Vector of macOS-exclusive font identifiers (lowercase)
     */
    static std::vector<std::string> GetMacOSExclusiveFonts();

private:
    /**
     * Generate the allowed fonts list JavaScript
     */
    static std::string GenerateFontLists(const Config& config);

    /**
     * Generate measureText hook that properly handles font fallback
     */
    static std::string GenerateMeasureTextHook(const Config& config);

    /**
     * Generate DOM measurement hooks (offsetWidth/Height, getBoundingClientRect)
     */
    static std::string GenerateDOMMeasurementHooks(const Config& config);

    /**
     * Generate Font API hooks (document.fonts, queryLocalFonts)
     */
    static std::string GenerateFontAPIHooks(const Config& config);

    /**
     * Generate CSS style property hooks (fontFamily, font, cssText)
     */
    static std::string GenerateCSSStyleHooks(const Config& config);

    /**
     * Generate getComputedStyle proxy wrapper
     */
    static std::string GenerateComputedStyleHook(const Config& config);

    /**
     * Generate element.setAttribute hook for style attributes
     */
    static std::string GenerateSetAttributeHook(const Config& config);

    /**
     * Escape string for JavaScript
     */
    static std::string EscapeJS(const std::string& str);
};

} // namespace owl
