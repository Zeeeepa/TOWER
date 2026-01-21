#pragma once

#include <string>
#include "include/cef_frame.h"
#include "stealth/owl_virtual_machine.h"

namespace owl {
namespace spoofs {

/**
 * Timezone Spoofing
 * 
 * Handles timezone spoofing:
 * - Intl.DateTimeFormat().resolvedOptions().timeZone
 * - Date.prototype.getTimezoneOffset()
 * - Date.prototype.toString() / toTimeString() / toDateString()
 * - Date.prototype.toLocaleString() family
 * 
 * DEPENDENCIES: Requires SpoofUtils to be injected first.
 * GUARD: Uses window[Symbol.for('owl')].guards.timezone
 */
class TimezoneSpoof {
public:
    /**
     * Configuration for timezone spoofing
     */
    struct Config {
        std::string iana_name = "America/New_York";  // IANA timezone name
        int offset_minutes = -300;                   // Offset from UTC in minutes (negative = west)
        bool has_dst = true;                         // Has daylight saving time
        
        // Build from VirtualMachine
        static Config FromVM(const VirtualMachine& vm);
        
        // Build from IANA name (calculates offset automatically)
        static Config FromIANA(const std::string& iana_name);
    };
    
    /**
     * Inject timezone spoofing into the frame.
     * 
     * @param frame The CEF frame to inject into
     * @param config The configuration for spoofing
     * @return true if injection succeeded
     */
    static bool Inject(CefRefPtr<CefFrame> frame, const Config& config);
    
    /**
     * Inject timezone spoofing with just IANA name.
     * Offset is calculated dynamically in JavaScript.
     * 
     * @param frame The CEF frame to inject into
     * @param iana_name The IANA timezone name (e.g., "America/New_York")
     * @return true if injection succeeded
     */
    static bool Inject(CefRefPtr<CefFrame> frame, const std::string& iana_name);
    
    /**
     * Generate the JavaScript for timezone spoofing.
     * 
     * @param config The configuration for spoofing
     * @return JavaScript code string
     */
    static std::string GenerateScript(const Config& config);
    
    /**
     * Generate the JavaScript for timezone spoofing with dynamic offset calculation.
     * 
     * @param iana_name The IANA timezone name
     * @return JavaScript code string
     */
    static std::string GenerateScript(const std::string& iana_name);
    
private:
    // Generate Date method hooks
    static std::string GenerateDateHooks();
    
    // Generate Intl.DateTimeFormat hooks
    static std::string GenerateIntlHooks();
    
    // Escape JavaScript string
    static std::string EscapeJS(const std::string& str);
};

} // namespace spoofs
} // namespace owl
