/**
 * OWL GPU Virtualization System Implementation
 *
 * Main orchestration layer for GPU virtualization that ties together
 * GL interception, shader translation, render normalization, and timing control.
 */

#include "gpu/owl_gpu_virtualization.h"
#include "gpu/owl_gpu_profile.h"
#include "gpu/owl_gpu_context.h"
#include "gpu/owl_gl_interceptor.h"
#include "gpu/owl_shader_translator.h"
#include "gpu/owl_render_normalizer.h"
#include "gpu/owl_timing_normalizer.h"
#include <algorithm>

namespace owl {
namespace gpu {

// ============================================================================
// Thread-local storage for current context
// ============================================================================

thread_local GPUContext* GPUVirtualizationSystem::current_context_ = nullptr;

// ============================================================================
// Singleton Implementation
// ============================================================================

GPUVirtualizationSystem& GPUVirtualizationSystem::Instance() {
    static GPUVirtualizationSystem instance;
    return instance;
}

GPUVirtualizationSystem::GPUVirtualizationSystem() = default;

GPUVirtualizationSystem::~GPUVirtualizationSystem() {
    Shutdown();
}

// ============================================================================
// Initialization and Shutdown
// ============================================================================

bool GPUVirtualizationSystem::Initialize(const GPUVirtualizationConfig& config) {
    if (initialized_.load()) {
        return true;  // Already initialized
    }

    config_ = config;

    // Initialize GL interceptor
    interceptor_ = std::make_unique<GLInterceptor>();
    if (!interceptor_->Initialize()) {
        return false;
    }

    // Initialize shader translator
    shader_translator_ = std::make_unique<ShaderTranslator>();

    // Initialize render normalizer
    render_normalizer_ = std::make_unique<RenderNormalizer>();
    RenderNormalizationConfig render_config;
    render_config.enable_pixel_normalization = config.enable_render_normalization;
    render_config.intensity = config.noise_intensity;
    render_config.mode = config.apply_deterministic_noise ?
        NormalizationMode::Deterministic : NormalizationMode::None;
    render_config.enable_aa_normalization = config.normalize_antialiasing;
    render_config.normalize_color_primaries = config.normalize_color_space;
    render_normalizer_->SetConfig(render_config);

    // Initialize timing normalizer
    timing_normalizer_ = std::make_unique<TimingNormalizer>();
    TimingNormalizationConfig timing_config;
    timing_config.enabled = config.enable_timing_normalization;
    timing_config.quantum_us = config.timing_quantum_us;
    timing_config.enable_jitter = config.add_timing_jitter;
    timing_config.jitter_ratio = config.max_jitter_ratio;
    timing_normalizer_->SetConfig(timing_config);

    // Load GPU profiles
    LoadProfiles();

    initialized_.store(true);
    return true;
}

void GPUVirtualizationSystem::Shutdown() {
    if (!initialized_.load()) {
        return;
    }

    // Clear all contexts
    {
        std::lock_guard<std::mutex> lock(contexts_mutex_);
        contexts_.clear();
    }

    // Shutdown components
    if (interceptor_) {
        interceptor_->Shutdown();
    }

    timing_normalizer_.reset();
    render_normalizer_.reset();
    shader_translator_.reset();
    interceptor_.reset();

    // Clear profiles
    {
        std::lock_guard<std::mutex> lock(profiles_mutex_);
        profiles_.clear();
    }

    initialized_.store(false);
}

void GPUVirtualizationSystem::UpdateConfig(const GPUVirtualizationConfig& config) {
    config_ = config;

    // Update component configs
    if (render_normalizer_) {
        RenderNormalizationConfig render_config;
        render_config.enable_pixel_normalization = config.enable_render_normalization;
        render_config.intensity = config.noise_intensity;
        render_config.mode = config.apply_deterministic_noise ?
            NormalizationMode::Deterministic : NormalizationMode::None;
        render_config.enable_aa_normalization = config.normalize_antialiasing;
        render_config.normalize_color_primaries = config.normalize_color_space;
        render_normalizer_->SetConfig(render_config);
    }

    if (timing_normalizer_) {
        TimingNormalizationConfig timing_config;
        timing_config.enabled = config.enable_timing_normalization;
        timing_config.quantum_us = config.timing_quantum_us;
        timing_config.enable_jitter = config.add_timing_jitter;
        timing_config.jitter_ratio = config.max_jitter_ratio;
        timing_normalizer_->SetConfig(timing_config);
    }
}

// ============================================================================
// Context Management
// ============================================================================

std::shared_ptr<GPUContext> GPUVirtualizationSystem::CreateContext(const GPUProfile& profile) {
    if (!initialized_.load()) {
        return nullptr;
    }

    // Create a copy of the profile as shared_ptr
    auto profile_ptr = std::make_shared<GPUProfile>(profile);
    auto context = std::make_shared<GPUContext>(profile_ptr);

    // Track context
    {
        std::lock_guard<std::mutex> lock(contexts_mutex_);
        // Clean up expired contexts
        contexts_.erase(
            std::remove_if(contexts_.begin(), contexts_.end(),
                [](const std::weak_ptr<GPUContext>& wp) { return wp.expired(); }),
            contexts_.end());
        contexts_.push_back(context);
    }

    return context;
}

std::shared_ptr<GPUContext> GPUVirtualizationSystem::CreateContext(const std::string& profile_id) {
    std::lock_guard<std::mutex> lock(profiles_mutex_);
    auto it = profiles_.find(profile_id);
    if (it == profiles_.end() || !it->second) {
        return nullptr;
    }
    // Create context directly with shared_ptr
    auto context = std::make_shared<GPUContext>(it->second);

    // Track context
    {
        std::lock_guard<std::mutex> lock(contexts_mutex_);
        contexts_.erase(
            std::remove_if(contexts_.begin(), contexts_.end(),
                [](const std::weak_ptr<GPUContext>& wp) { return wp.expired(); }),
            contexts_.end());
        contexts_.push_back(context);
    }

    return context;
}

GPUContext* GPUVirtualizationSystem::GetCurrentContext() {
    return current_context_;
}

void GPUVirtualizationSystem::MakeContextCurrent(GPUContext* context) {
    current_context_ = context;

    // Also register with context manager for GL interception
    if (context) {
        GPUContextManager::Instance().SetCurrentContext(context);
    } else {
        GPUContextManager::Instance().SetCurrentContext(nullptr);
    }
}

// ============================================================================
// GL Handler Registration
// ============================================================================

void GPUVirtualizationSystem::RegisterGLHandler(uint32_t call_id, GLCallHandler handler) {
    if (!interceptor_) return;

    interceptor_->RegisterHandler(static_cast<GLCallId>(call_id),
        [handler](GLCallInfo& info) -> GLCallResult {
            if (handler(static_cast<uint32_t>(info.call_id), info.args)) {
                return GLCallResult::Handled;
            }
            return GLCallResult::Continue;
        });
}

// ============================================================================
// Profile Management
// ============================================================================

void GPUVirtualizationSystem::LoadProfiles() {
    std::lock_guard<std::mutex> lock(profiles_mutex_);

    // Register factory profiles
    auto& registry = GPUProfileRegistry::Instance();
    registry.RegisterFactoryProfiles();

    // Get all profile IDs from registry
    auto ids = registry.GetAllProfileIds();
    for (const auto& id : ids) {
        if (auto profile = registry.GetProfile(id)) {
            profiles_[id] = profile;
        }
    }
}

const GPUProfile* GPUVirtualizationSystem::GetProfile(const std::string& id) const {
    std::lock_guard<std::mutex> lock(profiles_mutex_);
    auto it = profiles_.find(id);
    if (it != profiles_.end()) {
        return it->second.get();
    }
    return nullptr;
}

std::vector<std::string> GPUVirtualizationSystem::GetProfileIds() const {
    std::lock_guard<std::mutex> lock(profiles_mutex_);
    std::vector<std::string> ids;
    ids.reserve(profiles_.size());
    for (const auto& [id, profile] : profiles_) {
        ids.push_back(id);
    }
    return ids;
}

std::shared_ptr<GPUProfile> GPUVirtualizationSystem::CreateProfile(
    const std::string& id,
    const GPUCapabilities& caps,
    const GPURenderBehavior& behavior) {

    auto profile = std::make_shared<GPUProfile>(id);
    profile->SetCapabilities(caps);
    profile->SetRenderBehavior(behavior);

    // Determine vendor and architecture from renderer string
    GPUVendor vendor = GPUVendor::Unknown;
    GPUArchitecture arch = GPUArchitecture::Unknown;

    std::string renderer_lower = caps.unmasked_renderer;
    std::transform(renderer_lower.begin(), renderer_lower.end(),
                   renderer_lower.begin(), ::tolower);

    // Detect vendor
    if (renderer_lower.find("nvidia") != std::string::npos ||
        renderer_lower.find("geforce") != std::string::npos ||
        renderer_lower.find("quadro") != std::string::npos) {
        vendor = GPUVendor::NVIDIA;
    } else if (renderer_lower.find("amd") != std::string::npos ||
               renderer_lower.find("radeon") != std::string::npos) {
        vendor = GPUVendor::AMD;
    } else if (renderer_lower.find("intel") != std::string::npos) {
        vendor = GPUVendor::Intel;
    } else if (renderer_lower.find("apple") != std::string::npos) {
        vendor = GPUVendor::Apple;
    }

    // Detect architecture
    if (vendor == GPUVendor::NVIDIA) {
        if (renderer_lower.find("rtx 50") != std::string::npos ||
            renderer_lower.find("rtx50") != std::string::npos) {
            arch = GPUArchitecture::NVIDIA_Blackwell;
        } else if (renderer_lower.find("rtx 40") != std::string::npos ||
                   renderer_lower.find("rtx40") != std::string::npos) {
            arch = GPUArchitecture::NVIDIA_Ada;
        } else if (renderer_lower.find("rtx 30") != std::string::npos ||
                   renderer_lower.find("rtx30") != std::string::npos) {
            arch = GPUArchitecture::NVIDIA_Ampere;
        } else if (renderer_lower.find("rtx 20") != std::string::npos ||
                   renderer_lower.find("gtx 16") != std::string::npos) {
            arch = GPUArchitecture::NVIDIA_Turing;
        }
    } else if (vendor == GPUVendor::AMD) {
        if (renderer_lower.find("rx 9") != std::string::npos ||
            renderer_lower.find("rx9") != std::string::npos) {
            arch = GPUArchitecture::AMD_RDNA4;
        } else if (renderer_lower.find("rx 7") != std::string::npos ||
                   renderer_lower.find("rx7") != std::string::npos) {
            arch = GPUArchitecture::AMD_RDNA3;
        } else if (renderer_lower.find("rx 6") != std::string::npos ||
                   renderer_lower.find("rx6") != std::string::npos) {
            arch = GPUArchitecture::AMD_RDNA2;
        }
    } else if (vendor == GPUVendor::Apple) {
        if (renderer_lower.find("m4") != std::string::npos) {
            arch = GPUArchitecture::Apple_M4;
        } else if (renderer_lower.find("m3") != std::string::npos) {
            arch = GPUArchitecture::Apple_M3;
        } else if (renderer_lower.find("m2") != std::string::npos) {
            arch = GPUArchitecture::Apple_M2;
        } else if (renderer_lower.find("m1") != std::string::npos) {
            arch = GPUArchitecture::Apple_M1;
        }
    } else if (vendor == GPUVendor::Intel) {
        if (renderer_lower.find("arc") != std::string::npos) {
            arch = GPUArchitecture::Intel_Arc;
        } else if (renderer_lower.find("xe") != std::string::npos ||
                   renderer_lower.find("iris xe") != std::string::npos) {
            arch = GPUArchitecture::Intel_Gen12;
        } else if (renderer_lower.find("iris plus") != std::string::npos) {
            arch = GPUArchitecture::Intel_Gen11;
        } else if (renderer_lower.find("uhd") != std::string::npos) {
            arch = GPUArchitecture::Intel_Gen9;
        }
    }

    profile->SetVendor(vendor);
    profile->SetArchitecture(arch);

    // Register the profile
    {
        std::lock_guard<std::mutex> lock(profiles_mutex_);
        profiles_[id] = profile;
    }

    GPUProfileRegistry::Instance().RegisterProfile(profile);

    return profile;
}

// ============================================================================
// Statistics
// ============================================================================

GPUVirtualizationSystem::Statistics GPUVirtualizationSystem::GetStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    Statistics combined = stats_;

    // Aggregate from components
    if (interceptor_) {
        auto interc_stats = interceptor_->GetStats();
        combined.gl_calls_intercepted = interc_stats.total_calls;
        combined.parameters_spoofed = interc_stats.handled_calls;
    }

    if (shader_translator_) {
        auto shader_stats = shader_translator_->GetStats();
        combined.shaders_translated = shader_stats.shaders_translated;
    }

    if (render_normalizer_) {
        auto render_stats = render_normalizer_->GetStats();
        combined.pixels_normalized = render_stats.pixels_normalized;
    }

    if (timing_normalizer_) {
        auto timing_stats = timing_normalizer_->GetStatistics();
        combined.timing_normalizations = timing_stats.total_operations;
    }

    return combined;
}

void GPUVirtualizationSystem::ResetStatistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = Statistics{};

    if (interceptor_) interceptor_->ResetStats();
    if (shader_translator_) shader_translator_->ResetStats();
    if (render_normalizer_) render_normalizer_->ResetStats();
    if (timing_normalizer_) timing_normalizer_->ResetStatistics();
}

// ============================================================================
// Helper Functions for ANGLE Integration
// ============================================================================

namespace angle_integration {

/**
 * Hook into ANGLE's GL dispatch table
 * This function would be called during CEF/Chromium initialization
 */
bool InstallANGLEHooks() {
    auto& system = GPUVirtualizationSystem::Instance();
    if (!system.IsInitialized()) {
        if (!system.Initialize()) {
            return false;
        }
    }
    return true;
}

/**
 * Remove ANGLE hooks during shutdown
 */
void RemoveANGLEHooks() {
    GPUVirtualizationSystem::Instance().Shutdown();
}

/**
 * Called by ANGLE before processing a GL command
 * Returns true if the command was handled and should not be passed to the GPU
 */
bool PreProcessGLCommand(uint32_t command_id, void* args) {
    auto& system = GPUVirtualizationSystem::Instance();
    if (!system.IsInitialized()) return false;

    // Get current context
    auto* context = system.GetCurrentContext();
    if (!context) return false;

    // Route through interceptor
    // This is where we would intercept getParameter, getShaderPrecisionFormat, etc.

    return false;  // Continue to real GPU
}

/**
 * Called by ANGLE after processing a GL command
 * Used for post-processing (e.g., normalizing readPixels output)
 */
void PostProcessGLCommand(uint32_t command_id, void* args, void* result) {
    auto& system = GPUVirtualizationSystem::Instance();
    if (!system.IsInitialized()) return;

    auto* context = system.GetCurrentContext();
    if (!context) return;

    // Post-process based on command
    // readPixels normalization, timing measurement, etc.
}

/**
 * Translate shader source before compilation
 */
std::string TranslateShader(const std::string& source, bool is_vertex_shader) {
    auto& system = GPUVirtualizationSystem::Instance();
    if (!system.IsInitialized()) return source;

    auto* context = system.GetCurrentContext();
    if (!context) return source;

    auto& translator = system.GetShaderTranslator();
    auto result = translator.Translate(
        source,
        is_vertex_shader ? ShaderType::Vertex : ShaderType::Fragment,
        context);

    return result.success ? result.translated_source : source;
}

/**
 * Normalize pixel data from readPixels
 */
void NormalizePixels(void* pixels, size_t width, size_t height,
                      uint32_t format, uint32_t type) {
    auto& system = GPUVirtualizationSystem::Instance();
    if (!system.IsInitialized()) return;

    auto* context = system.GetCurrentContext();
    if (!context) return;

    context->NormalizePixels(pixels, width, height, format, type);
}

/**
 * Get spoofed GL string (vendor, renderer, version, etc.)
 */
const char* GetSpoofedString(uint32_t name) {
    auto& system = GPUVirtualizationSystem::Instance();
    if (!system.IsInitialized()) return nullptr;

    auto* context = system.GetCurrentContext();
    if (!context) return nullptr;

    return context->GetSpoofedString(name);
}

/**
 * Get spoofed GL parameter (texture size, uniform vectors, etc.)
 */
bool GetSpoofedParameter(uint32_t pname, void* params, size_t param_size) {
    auto& system = GPUVirtualizationSystem::Instance();
    if (!system.IsInitialized()) return false;

    auto* context = system.GetCurrentContext();
    if (!context) return false;

    return context->GetSpoofedParameter(pname, params, param_size);
}

/**
 * Get spoofed shader precision format
 */
bool GetSpoofedShaderPrecision(uint32_t shader_type, uint32_t precision_type,
                                int32_t* range, int32_t* precision) {
    auto& system = GPUVirtualizationSystem::Instance();
    if (!system.IsInitialized()) return false;

    auto* context = system.GetCurrentContext();
    if (!context) return false;

    return context->GetSpoofedShaderPrecision(shader_type, precision_type,
                                               range, precision);
}

/**
 * Begin timing measurement for GPU operation
 */
uint64_t BeginGPUOperation(TimingOperation op, const char* context_name) {
    auto& system = GPUVirtualizationSystem::Instance();
    if (!system.IsInitialized()) return 0;

    return system.GetTimingNormalizer().BeginOperation(op, context_name);
}

/**
 * End timing measurement and apply normalization
 */
uint64_t EndGPUOperation(uint64_t operation_id) {
    auto& system = GPUVirtualizationSystem::Instance();
    if (!system.IsInitialized()) return 0;

    return system.GetTimingNormalizer().EndOperation(operation_id);
}

/**
 * Get protected performance.now() value
 */
double GetProtectedPerformanceNow(double raw_value) {
    auto& system = GPUVirtualizationSystem::Instance();
    if (!system.IsInitialized()) return raw_value;

    return system.GetTimingNormalizer().ProtectPerformanceNow(raw_value);
}

/**
 * Get protected Date.now() value
 */
int64_t GetProtectedDateNow(int64_t raw_value) {
    auto& system = GPUVirtualizationSystem::Instance();
    if (!system.IsInitialized()) return raw_value;

    return system.GetTimingNormalizer().ProtectDateNow(raw_value);
}

} // namespace angle_integration

} // namespace gpu
} // namespace owl
