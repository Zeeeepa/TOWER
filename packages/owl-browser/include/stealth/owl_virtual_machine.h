#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <functional>

namespace owl {

/**
 * Complete Virtual Machine Profile
 *
 * This represents a complete, consistent virtual machine that can be
 * presented to websites. ALL fingerprinting vectors are defined here
 * and MUST be consistent with each other.
 *
 * When a profile says "Ubuntu 22.04 + Intel UHD 620 + Chrome 131",
 * EVERY fingerprint must match that exact configuration.
 */
struct VirtualMachine {
    // ========== IDENTITY ==========
    std::string id;                     // Unique profile ID (e.g., "ubuntu-intel-chrome-001")
    std::string name;                   // Human-readable name
    std::string description;            // Description of this VM

    // ========== OPERATING SYSTEM ==========
    struct OS {
        std::string name;               // "Windows", "Linux", "macOS"
        std::string version;            // "10.0", "22.04", "14.0"
        std::string platform;           // navigator.platform: "Win32", "Linux x86_64", "MacIntel"
        std::string oscpu;              // navigator.oscpu (Firefox): "Windows NT 10.0; Win64; x64"
        std::string app_version;        // navigator.appVersion base
        int max_touch_points;           // 0 for desktop, 5+ for touch devices
    } os;

    // ========== BROWSER ==========
    struct Browser {
        std::string name;               // "Chrome", "Firefox", "Safari", "Edge"
        std::string version;            // "131.0.0.0"
        std::string vendor;             // navigator.vendor: "Google Inc.", "", "Apple Computer, Inc."
        std::string user_agent;         // Full User-Agent string
        std::string app_name;           // navigator.appName: "Netscape"
        std::string app_code_name;      // navigator.appCodeName: "Mozilla"
        std::string product;            // navigator.product: "Gecko"
        std::string product_sub;        // navigator.productSub: "20030107" or "20100101"
        std::string build_id;           // navigator.buildID (Firefox only)
        bool webdriver;                 // Always false
        bool pdf_viewer_enabled;        // navigator.pdfViewerEnabled
        bool cookies_enabled;           // navigator.cookieEnabled
        bool java_enabled;              // Always false
        int max_parallel_streams;       // For HTTP/2
    } browser;

    // ========== CPU ==========
    struct CPU {
        int hardware_concurrency;       // navigator.hardwareConcurrency (logical cores)
        int device_memory;              // navigator.deviceMemory (GB, power of 2)
        std::string architecture;       // "x86_64", "arm64"
    } cpu;

    // ========== GPU (WebGL) ==========
    struct GPU {
        // Basic info
        std::string vendor;             // VENDOR (e.g., "Google Inc. (Intel)")
        std::string renderer;           // RENDERER (full ANGLE string)
        std::string unmasked_vendor;    // UNMASKED_VENDOR_WEBGL (e.g., "Intel Inc.")
        std::string unmasked_renderer;  // UNMASKED_RENDERER_WEBGL (e.g., "Intel(R) UHD Graphics 620")

        // Version strings
        std::string webgl_version;      // VERSION
        std::string webgl2_version;     // WebGL2 VERSION
        std::string shading_language;   // SHADING_LANGUAGE_VERSION

        // Capabilities
        int max_texture_size;
        int max_cube_map_texture_size;
        int max_render_buffer_size;
        int max_vertex_attribs;
        int max_vertex_uniform_vectors;
        int max_vertex_texture_units;
        int max_varying_vectors;
        int max_fragment_uniform_vectors;
        int max_texture_units;
        int max_combined_texture_units;
        int max_viewport_dims_w;
        int max_viewport_dims_h;
        float aliased_line_width_min;
        float aliased_line_width_max;
        float aliased_point_size_min;
        float aliased_point_size_max;
        // Multisampling parameters (critical for VM detection!)
        int max_samples;                // MAX_SAMPLES - max supported samples
        int samples;                    // GL_SAMPLES - actual samples in framebuffer (e.g., 4)
        int sample_buffers;             // GL_SAMPLE_BUFFERS - 1 if multisampling enabled
        float max_anisotropy;           // EXT_texture_filter_anisotropic

        // WebGL2-specific parameters
        int max_3d_texture_size;        // MAX_3D_TEXTURE_SIZE (WebGL2)
        int max_array_texture_layers;   // MAX_ARRAY_TEXTURE_LAYERS (WebGL2)
        int max_color_attachments;      // MAX_COLOR_ATTACHMENTS (WebGL2)
        int max_draw_buffers;           // MAX_DRAW_BUFFERS (WebGL2)
        int max_uniform_buffer_bindings; // MAX_UNIFORM_BUFFER_BINDINGS (WebGL2)
        int max_uniform_block_size;     // MAX_UNIFORM_BLOCK_SIZE (WebGL2)
        int max_combined_uniform_blocks; // MAX_COMBINED_UNIFORM_BLOCKS (WebGL2)

        // Precision formats (for getShaderPrecisionFormat)
        struct PrecisionFormat {
            int range_min;
            int range_max;
            int precision;
        };
        // Float precision (vertex shader)
        PrecisionFormat vertex_high_float;
        PrecisionFormat vertex_medium_float;
        PrecisionFormat vertex_low_float;
        // Int precision (vertex shader)
        PrecisionFormat vertex_high_int;
        PrecisionFormat vertex_medium_int;
        PrecisionFormat vertex_low_int;
        // Float precision (fragment shader)
        PrecisionFormat fragment_high_float;
        PrecisionFormat fragment_medium_float;
        PrecisionFormat fragment_low_float;
        // Int precision (fragment shader)
        PrecisionFormat fragment_high_int;
        PrecisionFormat fragment_medium_int;
        PrecisionFormat fragment_low_int;

        // Extensions (WebGL1 and WebGL2)
        std::vector<std::string> webgl_extensions;
        std::vector<std::string> webgl2_extensions;

        // Renderer hash seed (for consistent WebGL fingerprints)
        uint64_t renderer_hash_seed;
    } gpu;

    // ========== SCREEN ==========
    struct Screen {
        int width;                      // screen.width
        int height;                     // screen.height
        int avail_width;                // screen.availWidth
        int avail_height;               // screen.availHeight
        int color_depth;                // screen.colorDepth
        int pixel_depth;                // screen.pixelDepth
        float device_pixel_ratio;       // window.devicePixelRatio
        std::string orientation_type;   // "landscape-primary", "portrait-primary"
        int orientation_angle;          // 0, 90, 180, 270
    } screen;

    // ========== AUDIO ==========
    struct Audio {
        float sample_rate;              // AudioContext.sampleRate (44100 or 48000)
        int max_channel_count;          // destination.maxChannelCount
        int number_of_inputs;           // destination.numberOfInputs
        int number_of_outputs;          // destination.numberOfOutputs
        int channel_count;              // destination.channelCount
        std::string channel_count_mode; // "explicit"
        std::string channel_interpretation; // "speakers"
        float base_latency;             // AudioContext.baseLatency
        float output_latency;           // AudioContext.outputLatency
        uint64_t audio_hash_seed;       // For consistent audio fingerprints
    } audio;

    // ========== CANVAS ==========
    struct Canvas {
        // We do NOT add artificial noise - we let the GPU render naturally
        // But we need a consistent seed for any hash-based operations
        uint64_t hash_seed;
        bool apply_noise;               // Should be FALSE for undetectable mode
        double noise_intensity;         // If noise is applied (should be 0)
    } canvas;

    // ========== FONTS ==========
    struct Fonts {
        std::vector<std::string> installed;  // Platform-specific font list
        std::string default_serif;
        std::string default_sans_serif;
        std::string default_monospace;
    } fonts;

    // ========== TIMEZONE ==========
    struct Timezone {
        std::string iana_name;          // "America/New_York", "Europe/London"
        int offset_minutes;             // Offset from UTC in minutes
        bool has_dst;                   // Has daylight saving time
    } timezone;

    // ========== LANGUAGE ==========
    struct Language {
        std::vector<std::string> languages;  // navigator.languages
        std::string primary;                  // navigator.language
    } language;

    // ========== NETWORK ==========
    struct Network {
        std::string connection_type;    // NetworkInformation.type: "wifi", "ethernet"
        float downlink;                 // NetworkInformation.downlink (Mbps)
        float rtt;                      // NetworkInformation.rtt (ms)
        std::string effective_type;     // "4g", "3g"
        bool save_data;                 // NetworkInformation.saveData
    } network;

    // ========== MEDIA ==========
    struct Media {
        std::vector<std::string> audio_codecs;   // Supported audio MIME types
        std::vector<std::string> video_codecs;   // Supported video MIME types
        bool has_microphone;
        bool has_camera;
        bool has_speakers;
    } media;

    // ========== PERMISSIONS ==========
    struct Permissions {
        std::string geolocation;        // "prompt", "granted", "denied"
        std::string notifications;
        std::string camera;
        std::string microphone;
        std::string midi;
        std::string clipboard_read;
        std::string clipboard_write;
    } permissions;

    // ========== CLIENT HINTS ==========
    struct ClientHints {
        bool enabled;                   // Whether to respond to client hints
        std::string sec_ch_ua;          // Sec-CH-UA header value
        std::string sec_ch_ua_platform;
        std::string sec_ch_ua_mobile;
        std::string sec_ch_ua_full_version;
        std::string sec_ch_ua_arch;
        std::string sec_ch_ua_bitness;
        std::string sec_ch_ua_model;
    } client_hints;

    // ========== STORAGE ==========
    struct Storage {
        uint64_t quota;                 // StorageManager.estimate().quota
        uint64_t usage;                 // StorageManager.estimate().usage
    } storage;

    // ========== BATTERY ==========
    struct Battery {
        bool enabled;                   // Whether to expose Battery API
        float level;                    // 0.0 - 1.0
        bool charging;
        float charging_time;            // seconds or Infinity
        float discharging_time;         // seconds or Infinity
    } battery;

    // Validate that all fields are consistent with each other
    bool Validate() const;

    // Generate a unique fingerprint hash for this VM
    std::string GetFingerprintHash() const;
};

/**
 * Virtual Machine Database
 * Contains pre-built, validated virtual machine profiles.
 */
class VirtualMachineDB {
public:
    static VirtualMachineDB& Instance();

    // Get a VM by ID
    const VirtualMachine* GetVM(const std::string& id) const;

    // Get all available VM IDs
    std::vector<std::string> GetVMIds() const;

    // Get VMs by criteria
    std::vector<const VirtualMachine*> GetVMsByOS(const std::string& os) const;
    std::vector<const VirtualMachine*> GetVMsByBrowser(const std::string& browser) const;
    std::vector<const VirtualMachine*> GetVMsByGPU(const std::string& gpu_vendor) const;

    // Select a random VM matching criteria
    const VirtualMachine* SelectRandomVM(
        const std::string& target_os = "",
        const std::string& target_browser = "",
        const std::string& target_gpu = "",
        uint64_t seed = 0
    ) const;

    // Create a randomized VM based on a template
    VirtualMachine CreateRandomizedVM(const VirtualMachine& base, uint64_t seed) const;

    // Get count
    size_t GetVMCount() const;

    // Get browser version from config table
    std::string GetBrowserVersion() const;      // e.g., "143"
    std::string GetBrowserVersionFull() const;  // e.g., "143.0.0.0"

    // Get default user agent with current browser version
    std::string GetDefaultUserAgent() const;

private:
    VirtualMachineDB();
    ~VirtualMachineDB() = default;

    VirtualMachineDB(const VirtualMachineDB&) = delete;
    VirtualMachineDB& operator=(const VirtualMachineDB&) = delete;

    void InitializeVMs();

    // Load profiles from encrypted SQLite database
    bool LoadFromDatabase();

    // Helper to find the database path
    std::string GetDatabasePath() const;

    // Profile builders (fallback for development)
    void AddWindowsProfiles();
    void AddUbuntuProfiles();
    void AddMacOSProfiles();

    mutable std::mutex mutex_;
    std::vector<VirtualMachine> vms_;
    std::map<std::string, size_t> vm_index_;
    bool initialized_ = false;
    bool loaded_from_db_ = false;

    // Config values from database
    std::string browser_version_ = "143";           // Default fallback
    std::string browser_version_full_ = "143.0.0.0"; // Default fallback
};

/**
 * Overrides for VirtualMachine values (from browser profile)
 * These take precedence over the VM database values
 */
struct VMOverrides {
    std::string webgl_renderer;  // Full ANGLE format string from profile
    std::string webgl_vendor;    // WebGL vendor from profile

    bool HasWebGLOverrides() const {
        return !webgl_renderer.empty() || !webgl_vendor.empty();
    }
};

/**
 * Virtual Machine Injector
 * Generates JavaScript code to make the browser appear as the specified VM.
 */
class VirtualMachineInjector {
public:
    // Generate complete injection script for a VM
    // If context_id is provided, uses OwlFingerprintGenerator for dynamic seeds
    // Otherwise falls back to static seeds from the VirtualMachine profile
    // If overrides are provided, they take precedence over VM database values
    static std::string GenerateScript(const VirtualMachine& vm, const std::string& context_id = "",
                                      const VMOverrides& overrides = VMOverrides());

    // Generate individual component scripts
    static std::string GenerateNavigatorScript(const VirtualMachine& vm);
    static std::string GenerateScreenScript(const VirtualMachine& vm);
    static std::string GenerateWebGLScript(const VirtualMachine& vm, const VMOverrides& overrides = VMOverrides());
    static std::string GenerateCanvasScript(const VirtualMachine& vm);
    static std::string GenerateAudioScript(const VirtualMachine& vm);
    static std::string GenerateFontsScript(const VirtualMachine& vm);
    static std::string GenerateTimezoneScript(const VirtualMachine& vm);
    static std::string GenerateMediaScript(const VirtualMachine& vm);
    static std::string GeneratePermissionsScript(const VirtualMachine& vm);
    static std::string GenerateStorageScript(const VirtualMachine& vm);
    static std::string GenerateBatteryScript(const VirtualMachine& vm);
    static std::string GenerateNetworkScript(const VirtualMachine& vm);
    static std::string GenerateIframeInterceptionScript(const VirtualMachine& vm);

    // Generate User-Agent header (for HTTP requests)
    static std::string GetUserAgent(const VirtualMachine& vm);

    // Generate Client Hints headers
    static std::map<std::string, std::string> GetClientHintHeaders(const VirtualMachine& vm);

private:
    static std::string EscapeJS(const std::string& str);
    static std::string GenerateMaskingUtility();
    static std::string VectorToJSArray(const std::vector<std::string>& vec);
};

/**
 * Integration with existing profile system
 */
struct VMProfile {
    std::string profile_id;             // ID in the profile database
    VirtualMachine vm;                  // The virtual machine configuration

    // Additional profile data
    std::string proxy_url;
    std::vector<std::pair<std::string, std::string>> cookies;
    std::map<std::string, std::string> local_storage;

    // Serialization
    std::string ToJSON() const;
    static std::optional<VMProfile> FromJSON(const std::string& json);
};

// ============================================================================
// GPU Virtualization Functions
// ============================================================================

// Forward declarations
namespace gpu {
    class GPUContext;
}

/**
 * Apply GPU virtualization for a VirtualMachine profile.
 * This initializes the GPU virtualization system and creates a context
 * that will intercept GL calls at the native level.
 *
 * @param vm The VirtualMachine profile to apply
 * @return Shared pointer to the GPU context, or nullptr on failure
 */
std::shared_ptr<gpu::GPUContext> ApplyGPUVirtualization(const VirtualMachine& vm);

/**
 * Get the GPU context for a specific VM profile ID.
 *
 * @param vm_id The VirtualMachine profile ID
 * @return Shared pointer to the GPU context, or nullptr if not found
 */
std::shared_ptr<gpu::GPUContext> GetGPUContext(const std::string& vm_id);

/**
 * Clear the GPU context for a specific VM profile ID.
 * Call this when a browser context is destroyed.
 *
 * @param vm_id The VirtualMachine profile ID
 */
void ClearGPUContext(const std::string& vm_id);

/**
 * Make the GPU context for a VM profile current on this thread.
 * This should be called before any GL operations for the context.
 *
 * @param vm_id The VirtualMachine profile ID
 */
void MakeGPUContextCurrent(const std::string& vm_id);

} // namespace owl
