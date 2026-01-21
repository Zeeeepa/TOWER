#pragma once

#include <string>
#include <cstdint>
#include "include/cef_frame.h"
#include "stealth/owl_virtual_machine.h"

namespace owl {
namespace spoofs {

/**
 * Audio Fingerprint Spoofing
 * 
 * Handles audio fingerprint spoofing:
 * - AudioContext properties (sampleRate, baseLatency, outputLatency)
 * - AudioContext/BaseAudioContext getters
 * - OfflineAudioContext sample rate normalization
 * - AnalyserNode frequency data noise (optional)
 * 
 * DEPENDENCIES: Requires SpoofUtils to be injected first.
 * GUARD: Uses window[Symbol.for('owl')].guards.audio
 */
class AudioSpoof {
public:
    /**
     * Configuration for audio spoofing
     */
    struct Config {
        float sample_rate = 48000.0f;
        float base_latency = 0.005;
        float output_latency = 0.02;
        int max_channel_count = 2;
        int number_of_inputs = 0;
        int number_of_outputs = 1;
        int channel_count = 2;
        std::string channel_count_mode = "explicit";
        std::string channel_interpretation = "speakers";
        
        // Seed for audio fingerprint noise
        uint64_t seed = 0;
        
        // Build from VirtualMachine
        static Config FromVM(const VirtualMachine& vm);
    };
    
    /**
     * Inject audio spoofing into the frame.
     * 
     * @param frame The CEF frame to inject into
     * @param config The configuration for spoofing
     * @return true if injection succeeded
     */
    static bool Inject(CefRefPtr<CefFrame> frame, const Config& config);
    
    /**
     * Generate the JavaScript for audio spoofing.
     * 
     * @param config The configuration for spoofing
     * @return JavaScript code string
     */
    static std::string GenerateScript(const Config& config);
    
private:
    // Generate AudioContext property hooks
    static std::string GenerateAudioContextHooks(const Config& config);

    // Generate OfflineAudioContext hooks
    static std::string GenerateOfflineAudioContextHooks(const Config& config);

    // Generate destination node hooks
    static std::string GenerateDestinationHooks(const Config& config);

    // Generate audio fingerprint hooks (getChannelData noise injection)
    static std::string GenerateAudioFingerprintHooks(const Config& config);
};

} // namespace spoofs
} // namespace owl
