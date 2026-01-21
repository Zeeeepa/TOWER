#pragma once

/**
 * OWL GPU Profile System
 *
 * Defines complete GPU profiles that can be used to virtualize GPU identity.
 * Each profile contains all parameters needed to convincingly emulate a specific
 * GPU model, including rendering characteristics and timing behavior.
 */

#include "gpu/owl_gpu_virtualization.h"
#include <string>
#include <map>
#include <memory>

namespace owl {

// Forward declaration for VirtualMachine (defined in stealth/owl_virtual_machine.h)
struct VirtualMachine;

namespace gpu {

/**
 * Complete GPU Profile
 *
 * Contains all information needed to emulate a specific GPU:
 * - Identity (vendor, model, driver)
 * - Capabilities (limits, extensions)
 * - Render behavior (precision, quirks)
 * - Timing characteristics (for DrawnApart defense)
 */
class GPUProfile {
public:
    GPUProfile() = default;
    explicit GPUProfile(const std::string& id);
    GPUProfile(const std::string& id, const std::string& name);
    GPUProfile(const GPUProfile&) = default;
    GPUProfile& operator=(const GPUProfile&) = default;
    GPUProfile(GPUProfile&&) = default;
    GPUProfile& operator=(GPUProfile&&) = default;

    // ==================== Identity ====================

    const std::string& GetId() const { return id_; }
    const std::string& GetName() const { return name_; }
    const std::string& GetDescription() const { return description_; }

    GPUVendor GetVendor() const { return vendor_; }
    GPUArchitecture GetArchitecture() const { return architecture_; }

    void SetId(const std::string& id) { id_ = id; }
    void SetName(const std::string& name) { name_ = name; }
    void SetDescription(const std::string& desc) { description_ = desc; }
    void SetVendor(GPUVendor vendor) { vendor_ = vendor; }
    void SetArchitecture(GPUArchitecture arch) { architecture_ = arch; }

    // ==================== Capabilities ====================

    const GPUCapabilities& GetCapabilities() const { return capabilities_; }
    GPUCapabilities& GetCapabilitiesMutable() { return capabilities_; }
    void SetCapabilities(const GPUCapabilities& caps) { capabilities_ = caps; }

    // ==================== Render Behavior ====================

    const GPURenderBehavior& GetRenderBehavior() const { return render_behavior_; }
    GPURenderBehavior& GetRenderBehaviorMutable() { return render_behavior_; }
    void SetRenderBehavior(const GPURenderBehavior& behavior) { render_behavior_ = behavior; }

    // ==================== Timing Profile ====================

    /**
     * Timing characteristics for different operations
     * Used to mask the real GPU's timing signature
     */
    struct TimingProfile {
        // Base operation times (microseconds)
        uint32_t draw_call_base_us = 50;
        uint32_t texture_upload_per_kb_us = 2;
        uint32_t shader_compile_base_us = 1000;
        uint32_t buffer_map_us = 10;

        // Variance factors (0-1)
        float draw_call_variance = 0.1f;
        float texture_variance = 0.05f;
        float shader_variance = 0.2f;

        // GPU-specific timing quirks
        bool has_async_compute = false;
        bool has_hardware_vsync = true;
        uint32_t min_frame_time_us = 1000;  // 1ms = 1000fps max
    };

    const TimingProfile& GetTimingProfile() const { return timing_profile_; }
    TimingProfile& GetTimingProfileMutable() { return timing_profile_; }
    void SetTimingProfile(const TimingProfile& profile) { timing_profile_ = profile; }

    // ==================== Fingerprint Seeds ====================

    /**
     * Deterministic seeds for consistent fingerprinting
     */
    uint64_t GetRenderSeed() const { return render_seed_; }
    uint64_t GetCanvasSeed() const { return canvas_seed_; }
    uint64_t GetAudioSeed() const { return audio_seed_; }

    void SetRenderSeed(uint64_t seed) { render_seed_ = seed; }
    void SetCanvasSeed(uint64_t seed) { canvas_seed_ = seed; }
    void SetAudioSeed(uint64_t seed) { audio_seed_ = seed; }

    // ==================== Validation ====================

    /**
     * Validate that all profile parameters are consistent
     */
    bool Validate() const;

    /**
     * Get validation errors (if any)
     */
    std::vector<std::string> GetValidationErrors() const;

    // ==================== Serialization ====================

    /**
     * Serialize to JSON
     */
    std::string ToJSON() const;

    /**
     * Deserialize from JSON
     */
    static std::shared_ptr<GPUProfile> FromJSON(const std::string& json);

private:
    // Identity
    std::string id_;
    std::string name_;
    std::string description_;
    GPUVendor vendor_ = GPUVendor::Unknown;
    GPUArchitecture architecture_ = GPUArchitecture::Unknown;

    // Configuration
    GPUCapabilities capabilities_;
    GPURenderBehavior render_behavior_;
    TimingProfile timing_profile_;

    // Fingerprint seeds
    uint64_t render_seed_ = 0;
    uint64_t canvas_seed_ = 0;
    uint64_t audio_seed_ = 0;
};

/**
 * GPU Profile Factory
 *
 * Creates pre-configured profiles for common GPUs
 */
class GPUProfileFactory {
public:
    // ==================== Intel Profiles ====================

    static std::shared_ptr<GPUProfile> CreateIntelUHD620();
    static std::shared_ptr<GPUProfile> CreateIntelIrisXe();
    static std::shared_ptr<GPUProfile> CreateIntelArcA770();
    static std::shared_ptr<GPUProfile> CreateIntelBattlemageB580();

    // ==================== NVIDIA Profiles ====================

    static std::shared_ptr<GPUProfile> CreateNVIDIA_RTX2080();
    static std::shared_ptr<GPUProfile> CreateNVIDIA_RTX3060();
    static std::shared_ptr<GPUProfile> CreateNVIDIA_RTX3080();
    static std::shared_ptr<GPUProfile> CreateNVIDIA_RTX4070();
    static std::shared_ptr<GPUProfile> CreateNVIDIA_RTX4090();
    static std::shared_ptr<GPUProfile> CreateNVIDIA_RTX5090();

    // ==================== AMD Profiles ====================

    static std::shared_ptr<GPUProfile> CreateAMD_RX6700XT();
    static std::shared_ptr<GPUProfile> CreateAMD_RX7800XT();
    static std::shared_ptr<GPUProfile> CreateAMD_RX7900XTX();
    static std::shared_ptr<GPUProfile> CreateAMD_RX9070XT();

    // ==================== Apple Profiles ====================

    static std::shared_ptr<GPUProfile> CreateAppleM1();
    static std::shared_ptr<GPUProfile> CreateAppleM2();
    static std::shared_ptr<GPUProfile> CreateAppleM3();
    static std::shared_ptr<GPUProfile> CreateAppleM4();
    static std::shared_ptr<GPUProfile> CreateAppleM4Ultra();

    // ==================== Generic Creation ====================

    /**
     * Create profile from existing VirtualMachine GPU config
     */
    static std::shared_ptr<GPUProfile> CreateFromVirtualMachine(
        const owl::VirtualMachine& vm);

    /**
     * Create a profile matching the current real GPU
     * (for testing/development)
     */
    static std::shared_ptr<GPUProfile> CreateFromRealGPU();

private:
    // Helper to set common Intel capabilities
    static void SetIntelCommon(GPUCapabilities& caps, GPURenderBehavior& behavior);

    // Helper to set common NVIDIA capabilities
    static void SetNVIDIACommon(GPUCapabilities& caps, GPURenderBehavior& behavior);

    // Helper to set common AMD capabilities
    static void SetAMDCommon(GPUCapabilities& caps, GPURenderBehavior& behavior);

    // Helper to set common Apple capabilities
    static void SetAppleCommon(GPUCapabilities& caps, GPURenderBehavior& behavior);
};

/**
 * GPU Profile Registry
 *
 * Maintains a collection of available GPU profiles
 */
class GPUProfileRegistry {
public:
    static GPUProfileRegistry& Instance();

    /**
     * Register a profile
     */
    void Register(std::shared_ptr<GPUProfile> profile);

    /**
     * Register a profile (alias for Register)
     */
    void RegisterProfile(std::shared_ptr<GPUProfile> profile) { Register(profile); }

    /**
     * Get a profile by ID
     */
    std::shared_ptr<GPUProfile> Get(const std::string& id) const;

    /**
     * Get a profile by ID (alias for Get)
     */
    std::shared_ptr<GPUProfile> GetProfile(const std::string& id) const { return Get(id); }

    /**
     * Get all profile IDs
     */
    std::vector<std::string> GetAllIds() const;

    /**
     * Get all profile IDs (alias for GetAllIds)
     */
    std::vector<std::string> GetAllProfileIds() const { return GetAllIds(); }

    /**
     * Get profiles by vendor
     */
    std::vector<std::shared_ptr<GPUProfile>> GetByVendor(GPUVendor vendor) const;

    /**
     * Get profiles by architecture
     */
    std::vector<std::shared_ptr<GPUProfile>> GetByArchitecture(GPUArchitecture arch) const;

    /**
     * Load profiles from VirtualMachine database
     */
    void LoadFromVMDatabase();

    /**
     * Register all factory profiles
     */
    void RegisterFactoryProfiles();

private:
    GPUProfileRegistry();

    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<GPUProfile>> profiles_;
};

} // namespace gpu
} // namespace owl
