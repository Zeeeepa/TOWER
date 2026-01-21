#pragma once

#include <string>
#include <vector>
#include "include/cef_frame.h"
#include "stealth/owl_virtual_machine.h"

namespace owl {
namespace spoofs {

/**
 * Navigator Property Spoofing
 * 
 * Handles ALL navigator-related property spoofing in ONE place:
 * - webdriver (must return false, not undefined)
 * - platform, userAgent, appVersion, appName, appCodeName
 * - deviceMemory, hardwareConcurrency
 * - languages, language
 * - plugins, mimeTypes
 * - permissions
 * - userAgentData (Client Hints)
 * - vendor, product, productSub, buildID
 * - maxTouchPoints, pdfViewerEnabled, cookieEnabled
 * - gpu (WebGPU Navigator.gpu stub)
 * 
 * DEPENDENCIES: Requires SpoofUtils to be injected first.
 * GUARD: Uses window[Symbol.for('owl')].guards.navigator
 */
class NavigatorSpoof {
public:
    /**
     * Configuration for navigator spoofing
     */
    struct Config {
        // Core properties
        std::string user_agent;
        std::string platform;           // "Win32", "Linux x86_64", "MacIntel"
        std::string app_version;        // navigator.appVersion
        std::string vendor;             // "Google Inc.", "", "Apple Computer, Inc."
        std::string app_name = "Netscape";
        std::string app_code_name = "Mozilla";
        std::string product = "Gecko";
        std::string product_sub = "20030107";
        std::string build_id;           // Firefox only
        
        // Hardware
        int hardware_concurrency = 8;
        int device_memory = 8;
        int max_touch_points = 0;
        
        // Languages
        std::vector<std::string> languages;
        std::string primary_language = "en-US";
        
        // Flags
        bool webdriver = false;         // MUST be false, not undefined
        bool pdf_viewer_enabled = true;
        bool cookies_enabled = true;
        bool java_enabled = false;
        
        // Client Hints (userAgentData)
        bool enable_client_hints = true;
        std::string sec_ch_ua;
        std::string sec_ch_ua_platform;
        std::string sec_ch_ua_mobile = "?0";
        std::string sec_ch_ua_full_version;
        std::string sec_ch_ua_arch;
        std::string sec_ch_ua_bitness;
        std::string sec_ch_ua_model;
        
        // Browser info for userAgentData.brands
        std::string browser_name = "Google Chrome";
        std::string browser_version = "143";
        
        // Default constructor
        Config() {
            languages = {"en-US", "en"};
        }
        
        // Build from VirtualMachine
        static Config FromVM(const VirtualMachine& vm);
    };
    
    /**
     * Inject navigator spoofing into the frame.
     * 
     * @param frame The CEF frame to inject into
     * @param config The configuration for spoofing
     * @return true if injection succeeded
     */
    static bool Inject(CefRefPtr<CefFrame> frame, const Config& config);
    
    /**
     * Generate the JavaScript for navigator spoofing.
     * 
     * @param config The configuration for spoofing
     * @return JavaScript code string
     */
    static std::string GenerateScript(const Config& config);
    
private:
    // Generate webdriver spoofing (false, not undefined!)
    static std::string GenerateWebdriverScript();
    
    // Generate core navigator properties
    static std::string GenerateCorePropsScript(const Config& config);
    
    // Generate plugins and mimeTypes
    static std::string GeneratePluginsScript(bool pdf_enabled);
    
    // Generate userAgentData (Client Hints)
    static std::string GenerateUserAgentDataScript(const Config& config);
    
    // Generate navigator.gpu stub
    static std::string GenerateGPUStubScript();
    
    // Escape JavaScript string
    static std::string EscapeJS(const std::string& str);
    
    // Convert vector to JS array string
    static std::string VectorToJSArray(const std::vector<std::string>& vec);
};

} // namespace spoofs
} // namespace owl
