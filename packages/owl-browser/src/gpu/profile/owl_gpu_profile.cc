/**
 * OWL GPU Profile Implementation
 *
 * Provides GPU profile creation, validation, and management.
 */

#include "gpu/owl_gpu_profile.h"
#include "stealth/owl_virtual_machine.h"
#include <sstream>
#include <algorithm>
#include <random>

namespace owl {
namespace gpu {

// ============================================================================
// GPUProfile Implementation
// ============================================================================

GPUProfile::GPUProfile(const std::string& id)
    : id_(id), name_(id) {
}

GPUProfile::GPUProfile(const std::string& id, const std::string& name)
    : id_(id), name_(name) {
}

bool GPUProfile::Validate() const {
    // Check required fields
    if (id_.empty()) return false;
    if (capabilities_.vendor.empty()) return false;
    if (capabilities_.renderer.empty()) return false;

    // Validate capability ranges
    if (capabilities_.max_texture_size < 1024) return false;
    if (capabilities_.max_vertex_attribs < 8) return false;
    if (capabilities_.max_texture_image_units < 8) return false;

    // Validate precision formats
    auto validate_precision = [](const ShaderPrecisionFormat& p) {
        return p.range_min >= 0 && p.range_max >= p.range_min && p.precision >= 0;
    };

    if (!validate_precision(capabilities_.vs_high_float)) return false;
    if (!validate_precision(capabilities_.fs_high_float)) return false;

    return true;
}

std::vector<std::string> GPUProfile::GetValidationErrors() const {
    std::vector<std::string> errors;

    if (id_.empty()) errors.push_back("Profile ID is empty");
    if (capabilities_.vendor.empty()) errors.push_back("GPU vendor is empty");
    if (capabilities_.renderer.empty()) errors.push_back("GPU renderer is empty");
    if (capabilities_.max_texture_size < 1024)
        errors.push_back("max_texture_size too small (< 1024)");
    if (capabilities_.max_vertex_attribs < 8)
        errors.push_back("max_vertex_attribs too small (< 8)");

    return errors;
}

std::string GPUProfile::ToJSON() const {
    std::stringstream ss;
    ss << "{\n";
    ss << "  \"id\": \"" << id_ << "\",\n";
    ss << "  \"name\": \"" << name_ << "\",\n";
    ss << "  \"vendor\": \"" << capabilities_.vendor << "\",\n";
    ss << "  \"renderer\": \"" << capabilities_.renderer << "\",\n";
    ss << "  \"unmasked_vendor\": \"" << capabilities_.unmasked_vendor << "\",\n";
    ss << "  \"unmasked_renderer\": \"" << capabilities_.unmasked_renderer << "\",\n";
    ss << "  \"max_texture_size\": " << capabilities_.max_texture_size << ",\n";
    ss << "  \"render_seed\": " << render_seed_ << ",\n";
    ss << "  \"canvas_seed\": " << canvas_seed_ << "\n";
    ss << "}";
    return ss.str();
}

// ============================================================================
// GPUProfileFactory Implementation
// ============================================================================

void GPUProfileFactory::SetIntelCommon(GPUCapabilities& caps, GPURenderBehavior& behavior) {
    caps.max_texture_size = 16384;
    caps.max_cube_map_texture_size = 16384;
    caps.max_render_buffer_size = 16384;
    caps.max_vertex_attribs = 16;
    caps.max_vertex_uniform_vectors = 4096;
    caps.max_varying_vectors = 32;
    caps.max_fragment_uniform_vectors = 1024;
    caps.max_texture_image_units = 16;
    caps.max_combined_texture_image_units = 32;
    caps.max_vertex_texture_image_units = 16;
    caps.max_viewport_dims[0] = 16384;
    caps.max_viewport_dims[1] = 16384;
    caps.aliased_line_width_range[0] = 1.0f;
    caps.aliased_line_width_range[1] = 1.0f;
    caps.aliased_point_size_range[0] = 1.0f;
    caps.aliased_point_size_range[1] = 1024.0f;
    // Multisampling parameters
    caps.max_samples = 8;
    caps.samples = 4;
    caps.sample_buffers = 1;
    caps.max_texture_max_anisotropy = 16.0f;

    // Standard precision for desktop GPUs
    caps.vs_high_float = {127, 127, 23};
    caps.vs_medium_float = {127, 127, 23};
    caps.vs_low_float = {127, 127, 23};
    caps.fs_high_float = {127, 127, 23};
    caps.fs_medium_float = {127, 127, 23};
    caps.fs_low_float = {127, 127, 23};

    // Intel render behavior
    behavior.flush_denormals = false;
    behavior.precise_sqrt = true;
    behavior.precise_divide = true;
    behavior.srgb_decode_accurate = true;
}

void GPUProfileFactory::SetNVIDIACommon(GPUCapabilities& caps, GPURenderBehavior& behavior) {
    caps.max_texture_size = 32768;
    caps.max_cube_map_texture_size = 32768;
    caps.max_render_buffer_size = 32768;
    caps.max_vertex_attribs = 16;
    caps.max_vertex_uniform_vectors = 4096;
    caps.max_varying_vectors = 32;
    caps.max_fragment_uniform_vectors = 4096;
    caps.max_texture_image_units = 32;
    // CRITICAL: Chromium's kMaxTextureUnits is hardcoded at 64
    // Spoofing higher values causes WebGL exceptions
    caps.max_combined_texture_image_units = 64;
    caps.max_vertex_texture_image_units = 32;
    caps.max_viewport_dims[0] = 32768;
    caps.max_viewport_dims[1] = 32768;
    caps.aliased_line_width_range[0] = 1.0f;
    caps.aliased_line_width_range[1] = 10.0f;
    caps.aliased_point_size_range[0] = 1.0f;
    caps.aliased_point_size_range[1] = 2048.0f;
    // Multisampling parameters
    caps.max_samples = 32;
    caps.samples = 4;
    caps.sample_buffers = 1;
    caps.max_texture_max_anisotropy = 16.0f;

    // NVIDIA precision
    caps.vs_high_float = {127, 127, 23};
    caps.vs_medium_float = {127, 127, 23};
    caps.vs_low_float = {127, 127, 23};
    caps.fs_high_float = {127, 127, 23};
    caps.fs_medium_float = {127, 127, 23};
    caps.fs_low_float = {127, 127, 23};

    // NVIDIA render behavior
    behavior.flush_denormals = false;
    behavior.precise_sqrt = true;
    behavior.precise_divide = true;
    behavior.has_async_compute = true;
}

void GPUProfileFactory::SetAMDCommon(GPUCapabilities& caps, GPURenderBehavior& behavior) {
    caps.max_texture_size = 16384;
    caps.max_cube_map_texture_size = 16384;
    caps.max_render_buffer_size = 16384;
    caps.max_vertex_attribs = 16;
    caps.max_vertex_uniform_vectors = 4096;
    caps.max_varying_vectors = 32;
    caps.max_fragment_uniform_vectors = 4096;
    caps.max_texture_image_units = 32;
    // CRITICAL: Chromium's kMaxTextureUnits is hardcoded at 64
    // Spoofing higher values causes WebGL exceptions
    caps.max_combined_texture_image_units = 64;
    caps.max_vertex_texture_image_units = 32;
    caps.max_viewport_dims[0] = 16384;
    caps.max_viewport_dims[1] = 16384;
    caps.aliased_line_width_range[0] = 1.0f;
    caps.aliased_line_width_range[1] = 10.0f;
    caps.aliased_point_size_range[0] = 1.0f;
    caps.aliased_point_size_range[1] = 8191.0f;
    // Multisampling parameters
    caps.max_samples = 16;
    caps.samples = 4;
    caps.sample_buffers = 1;
    caps.max_texture_max_anisotropy = 16.0f;

    // AMD precision
    caps.vs_high_float = {127, 127, 23};
    caps.vs_medium_float = {127, 127, 23};
    caps.vs_low_float = {127, 127, 23};
    caps.fs_high_float = {127, 127, 23};
    caps.fs_medium_float = {127, 127, 23};
    caps.fs_low_float = {127, 127, 23};

    // AMD render behavior
    behavior.flush_denormals = true;  // AMD often flushes denormals
    behavior.precise_sqrt = true;
    behavior.precise_divide = true;
}

void GPUProfileFactory::SetAppleCommon(GPUCapabilities& caps, GPURenderBehavior& behavior) {
    caps.max_texture_size = 16384;
    caps.max_cube_map_texture_size = 16384;
    caps.max_render_buffer_size = 16384;
    caps.max_vertex_attribs = 31;
    caps.max_vertex_uniform_vectors = 4096;
    caps.max_varying_vectors = 31;
    caps.max_fragment_uniform_vectors = 1024;
    caps.max_texture_image_units = 16;
    caps.max_combined_texture_image_units = 32;
    caps.max_vertex_texture_image_units = 16;
    caps.max_viewport_dims[0] = 16384;
    caps.max_viewport_dims[1] = 16384;
    caps.aliased_line_width_range[0] = 1.0f;
    caps.aliased_line_width_range[1] = 1.0f;
    caps.aliased_point_size_range[0] = 1.0f;
    caps.aliased_point_size_range[1] = 511.0f;
    // Multisampling parameters (critical for VM detection!)
    // Real Chrome on Apple Silicon: MAX_SAMPLES=4, SAMPLES=4, SAMPLE_BUFFERS=1
    caps.max_samples = 4;
    caps.samples = 4;
    caps.sample_buffers = 1;
    caps.max_texture_max_anisotropy = 16.0f;

    // Apple precision
    caps.vs_high_float = {127, 127, 23};
    caps.vs_medium_float = {127, 127, 23};
    caps.vs_low_float = {127, 127, 23};
    caps.fs_high_float = {127, 127, 23};
    caps.fs_medium_float = {127, 127, 23};
    caps.fs_low_float = {127, 127, 23};

    // Apple render behavior
    behavior.flush_denormals = false;
    behavior.precise_sqrt = true;
    behavior.precise_divide = true;
    behavior.default_aa_mode = AAMode::MSAA_4x;
}

std::shared_ptr<GPUProfile> GPUProfileFactory::CreateIntelUHD620() {
    auto profile = std::make_shared<GPUProfile>("intel-uhd620", "Intel UHD Graphics 620");
    profile->SetDescription("Intel 8th Gen integrated GPU");
    profile->SetVendor(GPUVendor::Intel);
    profile->SetArchitecture(GPUArchitecture::Intel_Gen9);

    GPUCapabilities caps;
    GPURenderBehavior behavior;
    SetIntelCommon(caps, behavior);

    caps.vendor = "Google Inc. (Intel)";
    caps.renderer = "ANGLE (Intel, Intel(R) UHD Graphics 620 Direct3D11 vs_5_0 ps_5_0, D3D11)";
    caps.unmasked_vendor = "Intel Inc.";
    caps.unmasked_renderer = "Intel(R) UHD Graphics 620";
    caps.version = "WebGL 1.0 (OpenGL ES 2.0 Chromium)";
    caps.shading_language = "WebGL GLSL ES 1.0 (OpenGL ES GLSL ES 1.0 Chromium)";

    caps.extensions = {
        "ANGLE_instanced_arrays", "EXT_blend_minmax", "EXT_clip_control",
        "EXT_color_buffer_half_float", "EXT_depth_clamp", "EXT_disjoint_timer_query",
        "EXT_float_blend", "EXT_frag_depth", "EXT_polygon_offset_clamp",
        "EXT_shader_texture_lod", "EXT_sRGB",
        "EXT_texture_compression_bptc", "EXT_texture_compression_rgtc",
        "EXT_texture_filter_anisotropic", "EXT_texture_mirror_clamp_to_edge",
        "KHR_parallel_shader_compile",
        "OES_element_index_uint", "OES_fbo_render_mipmap", "OES_standard_derivatives",
        "OES_texture_float", "OES_texture_float_linear",
        "OES_texture_half_float", "OES_texture_half_float_linear",
        "OES_vertex_array_object",
        "WEBGL_blend_func_extended", "WEBGL_color_buffer_float",
        "WEBGL_compressed_texture_s3tc", "WEBGL_compressed_texture_s3tc_srgb",
        "WEBGL_debug_renderer_info", "WEBGL_debug_shaders",
        "WEBGL_depth_texture", "WEBGL_draw_buffers",
        "WEBGL_lose_context", "WEBGL_multi_draw", "WEBGL_polygon_mode"
    };

    profile->SetCapabilities(caps);
    profile->SetRenderBehavior(behavior);

    // Generate deterministic seeds
    profile->SetRenderSeed(0x49555844363230ULL);  // "IUHD620"
    profile->SetCanvasSeed(0x494E54454C5548ULL);

    return profile;
}

std::shared_ptr<GPUProfile> GPUProfileFactory::CreateNVIDIA_RTX4090() {
    auto profile = std::make_shared<GPUProfile>("nvidia-rtx4090", "NVIDIA GeForce RTX 4090");
    profile->SetDescription("NVIDIA Ada Lovelace flagship GPU");
    profile->SetVendor(GPUVendor::NVIDIA);
    profile->SetArchitecture(GPUArchitecture::NVIDIA_Ada);

    GPUCapabilities caps;
    GPURenderBehavior behavior;
    SetNVIDIACommon(caps, behavior);

    caps.vendor = "Google Inc. (NVIDIA)";
    caps.renderer = "ANGLE (NVIDIA, NVIDIA GeForce RTX 4090 Direct3D11 vs_5_0 ps_5_0, D3D11)";
    caps.unmasked_vendor = "NVIDIA Corporation";
    caps.unmasked_renderer = "NVIDIA GeForce RTX 4090";
    caps.version = "WebGL 1.0 (OpenGL ES 2.0 Chromium)";
    caps.shading_language = "WebGL GLSL ES 1.0 (OpenGL ES GLSL ES 1.0 Chromium)";

    caps.extensions = {
        "ANGLE_instanced_arrays", "EXT_blend_minmax", "EXT_clip_control",
        "EXT_color_buffer_half_float", "EXT_depth_clamp", "EXT_disjoint_timer_query",
        "EXT_float_blend", "EXT_frag_depth", "EXT_polygon_offset_clamp",
        "EXT_shader_texture_lod", "EXT_sRGB",
        "EXT_texture_compression_bptc", "EXT_texture_compression_rgtc",
        "EXT_texture_filter_anisotropic", "EXT_texture_mirror_clamp_to_edge",
        "KHR_parallel_shader_compile",
        "OES_element_index_uint", "OES_fbo_render_mipmap", "OES_standard_derivatives",
        "OES_texture_float", "OES_texture_float_linear",
        "OES_texture_half_float", "OES_texture_half_float_linear",
        "OES_vertex_array_object",
        "WEBGL_blend_func_extended", "WEBGL_color_buffer_float",
        "WEBGL_compressed_texture_astc",
        "WEBGL_compressed_texture_s3tc", "WEBGL_compressed_texture_s3tc_srgb",
        "WEBGL_debug_renderer_info", "WEBGL_debug_shaders",
        "WEBGL_depth_texture", "WEBGL_draw_buffers",
        "WEBGL_lose_context", "WEBGL_multi_draw", "WEBGL_polygon_mode"
    };

    profile->SetCapabilities(caps);
    profile->SetRenderBehavior(behavior);

    // Timing profile for high-end GPU
    GPUProfile::TimingProfile timing;
    timing.draw_call_base_us = 20;
    timing.shader_compile_base_us = 500;
    timing.has_async_compute = true;
    profile->SetTimingProfile(timing);

    profile->SetRenderSeed(0x525458343039304EULL);  // "RTX4090N"
    profile->SetCanvasSeed(0x4E5649444941344BULL);

    return profile;
}

std::shared_ptr<GPUProfile> GPUProfileFactory::CreateAppleM3() {
    auto profile = std::make_shared<GPUProfile>("apple-m3", "Apple M3");
    profile->SetDescription("Apple M3 GPU");
    profile->SetVendor(GPUVendor::Apple);
    profile->SetArchitecture(GPUArchitecture::Apple_M3);

    GPUCapabilities caps;
    GPURenderBehavior behavior;
    SetAppleCommon(caps, behavior);

    caps.vendor = "Apple Inc.";
    caps.renderer = "Apple M3";
    caps.unmasked_vendor = "Apple Inc.";
    caps.unmasked_renderer = "Apple M3";
    caps.version = "WebGL 1.0 (OpenGL ES 2.0 Chromium)";
    caps.shading_language = "WebGL GLSL ES 1.0 (OpenGL ES GLSL ES 1.0 Chromium)";

    // Apple Silicon extensions matching real Chrome on M4 Max
    caps.extensions = {
        "ANGLE_instanced_arrays", "EXT_blend_minmax", "EXT_clip_control",
        "EXT_color_buffer_half_float", "EXT_depth_clamp", "EXT_disjoint_timer_query",
        "EXT_float_blend", "EXT_frag_depth", "EXT_polygon_offset_clamp",
        "EXT_shader_texture_lod", "EXT_sRGB",
        "EXT_texture_compression_bptc", "EXT_texture_compression_rgtc",
        "EXT_texture_filter_anisotropic", "EXT_texture_mirror_clamp_to_edge",
        "KHR_parallel_shader_compile",
        "OES_element_index_uint", "OES_fbo_render_mipmap", "OES_standard_derivatives",
        "OES_texture_float", "OES_texture_float_linear",
        "OES_texture_half_float", "OES_texture_half_float_linear",
        "OES_vertex_array_object",
        "WEBGL_blend_func_extended", "WEBGL_color_buffer_float",
        "WEBGL_compressed_texture_astc", "WEBGL_compressed_texture_etc",
        "WEBGL_compressed_texture_etc1", "WEBGL_compressed_texture_pvrtc",
        "WEBGL_compressed_texture_s3tc", "WEBGL_compressed_texture_s3tc_srgb",
        "WEBGL_debug_renderer_info", "WEBGL_debug_shaders",
        "WEBGL_depth_texture", "WEBGL_draw_buffers",
        "WEBGL_lose_context", "WEBGL_multi_draw", "WEBGL_polygon_mode"
    };

    profile->SetCapabilities(caps);
    profile->SetRenderBehavior(behavior);

    profile->SetRenderSeed(0x4150504C454D3300ULL);  // "APPLEM3"
    profile->SetCanvasSeed(0x4D33475055415050ULL);

    return profile;
}

std::shared_ptr<GPUProfile> GPUProfileFactory::CreateFromVirtualMachine(
    const VirtualMachine& vm) {
    auto profile = std::make_shared<GPUProfile>(vm.id, vm.name);
    profile->SetDescription(vm.description);

    // Determine vendor from unmasked vendor string
    GPUVendor vendor = GPUVendor::Unknown;
    std::string vendor_lower = vm.gpu.unmasked_vendor;
    std::transform(vendor_lower.begin(), vendor_lower.end(), vendor_lower.begin(), ::tolower);

    if (vendor_lower.find("intel") != std::string::npos) {
        vendor = GPUVendor::Intel;
    } else if (vendor_lower.find("nvidia") != std::string::npos) {
        vendor = GPUVendor::NVIDIA;
    } else if (vendor_lower.find("amd") != std::string::npos ||
               vendor_lower.find("ati") != std::string::npos) {
        vendor = GPUVendor::AMD;
    } else if (vendor_lower.find("apple") != std::string::npos) {
        vendor = GPUVendor::Apple;
    }
    profile->SetVendor(vendor);

    // Copy capabilities from VirtualMachine
    GPUCapabilities caps;
    caps.vendor = vm.gpu.vendor;
    caps.renderer = vm.gpu.renderer;
    caps.unmasked_vendor = vm.gpu.unmasked_vendor;
    caps.unmasked_renderer = vm.gpu.unmasked_renderer;
    caps.version = vm.gpu.webgl_version;
    caps.shading_language = vm.gpu.shading_language;
    caps.max_texture_size = vm.gpu.max_texture_size;
    caps.max_cube_map_texture_size = vm.gpu.max_cube_map_texture_size;
    caps.max_render_buffer_size = vm.gpu.max_render_buffer_size;
    caps.max_vertex_attribs = vm.gpu.max_vertex_attribs;
    caps.max_vertex_uniform_vectors = vm.gpu.max_vertex_uniform_vectors;
    caps.max_vertex_texture_image_units = vm.gpu.max_vertex_texture_units;
    caps.max_varying_vectors = vm.gpu.max_varying_vectors;
    caps.max_fragment_uniform_vectors = vm.gpu.max_fragment_uniform_vectors;
    caps.max_texture_image_units = vm.gpu.max_texture_units;
    caps.max_combined_texture_image_units = vm.gpu.max_combined_texture_units;
    caps.max_viewport_dims[0] = vm.gpu.max_viewport_dims_w;
    caps.max_viewport_dims[1] = vm.gpu.max_viewport_dims_h;
    caps.aliased_line_width_range[0] = vm.gpu.aliased_line_width_min;
    caps.aliased_line_width_range[1] = vm.gpu.aliased_line_width_max;
    caps.aliased_point_size_range[0] = vm.gpu.aliased_point_size_min;
    caps.aliased_point_size_range[1] = vm.gpu.aliased_point_size_max;
    caps.max_samples = vm.gpu.max_samples;
    caps.max_texture_max_anisotropy = vm.gpu.max_anisotropy;

    // Copy shader precision (FLOAT - all levels: high, medium, low)
    caps.vs_high_float = {vm.gpu.vertex_high_float.range_min,
                          vm.gpu.vertex_high_float.range_max,
                          vm.gpu.vertex_high_float.precision};
    caps.vs_medium_float = {vm.gpu.vertex_medium_float.range_min,
                            vm.gpu.vertex_medium_float.range_max,
                            vm.gpu.vertex_medium_float.precision};
    caps.vs_low_float = {vm.gpu.vertex_low_float.range_min,
                         vm.gpu.vertex_low_float.range_max,
                         vm.gpu.vertex_low_float.precision};
    caps.fs_high_float = {vm.gpu.fragment_high_float.range_min,
                          vm.gpu.fragment_high_float.range_max,
                          vm.gpu.fragment_high_float.precision};
    caps.fs_medium_float = {vm.gpu.fragment_medium_float.range_min,
                            vm.gpu.fragment_medium_float.range_max,
                            vm.gpu.fragment_medium_float.precision};
    caps.fs_low_float = {vm.gpu.fragment_low_float.range_min,
                         vm.gpu.fragment_low_float.range_max,
                         vm.gpu.fragment_low_float.precision};

    // Copy shader precision (INT - all levels: high, medium, low)
    caps.vs_high_int = {vm.gpu.vertex_high_int.range_min,
                        vm.gpu.vertex_high_int.range_max,
                        vm.gpu.vertex_high_int.precision};
    caps.vs_medium_int = {vm.gpu.vertex_medium_int.range_min,
                          vm.gpu.vertex_medium_int.range_max,
                          vm.gpu.vertex_medium_int.precision};
    caps.vs_low_int = {vm.gpu.vertex_low_int.range_min,
                       vm.gpu.vertex_low_int.range_max,
                       vm.gpu.vertex_low_int.precision};
    caps.fs_high_int = {vm.gpu.fragment_high_int.range_min,
                        vm.gpu.fragment_high_int.range_max,
                        vm.gpu.fragment_high_int.precision};
    caps.fs_medium_int = {vm.gpu.fragment_medium_int.range_min,
                          vm.gpu.fragment_medium_int.range_max,
                          vm.gpu.fragment_medium_int.precision};
    caps.fs_low_int = {vm.gpu.fragment_low_int.range_min,
                       vm.gpu.fragment_low_int.range_max,
                       vm.gpu.fragment_low_int.precision};

    // Copy extensions
    caps.extensions = vm.gpu.webgl_extensions;

    profile->SetCapabilities(caps);

    // Set seeds from VM
    profile->SetRenderSeed(vm.gpu.renderer_hash_seed);
    profile->SetCanvasSeed(vm.canvas.hash_seed);
    profile->SetAudioSeed(vm.audio.audio_hash_seed);

    return profile;
}

// Stub implementations for other factory methods
std::shared_ptr<GPUProfile> GPUProfileFactory::CreateIntelIrisXe() {
    // Similar to UHD620 but with Xe architecture
    auto profile = CreateIntelUHD620();
    profile->SetId("intel-iris-xe");
    profile->SetName("Intel Iris Xe Graphics");
    profile->SetArchitecture(GPUArchitecture::Intel_Gen12);
    auto& caps = profile->GetCapabilitiesMutable();
    caps.unmasked_renderer = "Intel(R) Iris(R) Xe Graphics";
    caps.renderer = "ANGLE (Intel, Intel(R) Iris(R) Xe Graphics Direct3D11 vs_5_0 ps_5_0, D3D11)";
    return profile;
}

std::shared_ptr<GPUProfile> GPUProfileFactory::CreateIntelArcA770() {
    auto profile = CreateIntelUHD620();
    profile->SetId("intel-arc-a770");
    profile->SetName("Intel Arc A770");
    profile->SetArchitecture(GPUArchitecture::Intel_Arc);
    auto& caps = profile->GetCapabilitiesMutable();
    caps.unmasked_renderer = "Intel(R) Arc(TM) A770 Graphics";
    caps.max_texture_size = 32768;
    return profile;
}

std::shared_ptr<GPUProfile> GPUProfileFactory::CreateIntelBattlemageB580() {
    auto profile = CreateIntelArcA770();
    profile->SetId("intel-battlemage-b580");
    profile->SetName("Intel Arc B580");
    auto& caps = profile->GetCapabilitiesMutable();
    caps.unmasked_renderer = "Intel(R) Arc(TM) B580 Graphics";
    return profile;
}

std::shared_ptr<GPUProfile> GPUProfileFactory::CreateNVIDIA_RTX2080() {
    auto profile = CreateNVIDIA_RTX4090();
    profile->SetId("nvidia-rtx2080");
    profile->SetName("NVIDIA GeForce RTX 2080");
    profile->SetArchitecture(GPUArchitecture::NVIDIA_Turing);
    auto& caps = profile->GetCapabilitiesMutable();
    caps.unmasked_renderer = "NVIDIA GeForce RTX 2080";
    caps.renderer = "ANGLE (NVIDIA, NVIDIA GeForce RTX 2080 Direct3D11 vs_5_0 ps_5_0, D3D11)";
    return profile;
}

std::shared_ptr<GPUProfile> GPUProfileFactory::CreateNVIDIA_RTX3060() {
    auto profile = CreateNVIDIA_RTX4090();
    profile->SetId("nvidia-rtx3060");
    profile->SetName("NVIDIA GeForce RTX 3060");
    profile->SetArchitecture(GPUArchitecture::NVIDIA_Ampere);
    auto& caps = profile->GetCapabilitiesMutable();
    caps.unmasked_renderer = "NVIDIA GeForce RTX 3060";
    caps.renderer = "ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 Direct3D11 vs_5_0 ps_5_0, D3D11)";
    return profile;
}

std::shared_ptr<GPUProfile> GPUProfileFactory::CreateNVIDIA_RTX3080() {
    auto profile = CreateNVIDIA_RTX4090();
    profile->SetId("nvidia-rtx3080");
    profile->SetName("NVIDIA GeForce RTX 3080");
    profile->SetArchitecture(GPUArchitecture::NVIDIA_Ampere);
    auto& caps = profile->GetCapabilitiesMutable();
    caps.unmasked_renderer = "NVIDIA GeForce RTX 3080";
    caps.renderer = "ANGLE (NVIDIA, NVIDIA GeForce RTX 3080 Direct3D11 vs_5_0 ps_5_0, D3D11)";
    return profile;
}

std::shared_ptr<GPUProfile> GPUProfileFactory::CreateNVIDIA_RTX4070() {
    auto profile = CreateNVIDIA_RTX4090();
    profile->SetId("nvidia-rtx4070");
    profile->SetName("NVIDIA GeForce RTX 4070");
    auto& caps = profile->GetCapabilitiesMutable();
    caps.unmasked_renderer = "NVIDIA GeForce RTX 4070";
    caps.renderer = "ANGLE (NVIDIA, NVIDIA GeForce RTX 4070 Direct3D11 vs_5_0 ps_5_0, D3D11)";
    return profile;
}

std::shared_ptr<GPUProfile> GPUProfileFactory::CreateNVIDIA_RTX5090() {
    auto profile = CreateNVIDIA_RTX4090();
    profile->SetId("nvidia-rtx5090");
    profile->SetName("NVIDIA GeForce RTX 5090");
    profile->SetArchitecture(GPUArchitecture::NVIDIA_Blackwell);
    auto& caps = profile->GetCapabilitiesMutable();
    caps.unmasked_renderer = "NVIDIA GeForce RTX 5090";
    caps.renderer = "ANGLE (NVIDIA, NVIDIA GeForce RTX 5090 Direct3D11 vs_5_0 ps_5_0, D3D11)";
    return profile;
}

std::shared_ptr<GPUProfile> GPUProfileFactory::CreateAMD_RX6700XT() {
    auto profile = std::make_shared<GPUProfile>("amd-rx6700xt", "AMD Radeon RX 6700 XT");
    profile->SetVendor(GPUVendor::AMD);
    profile->SetArchitecture(GPUArchitecture::AMD_RDNA2);

    GPUCapabilities caps;
    GPURenderBehavior behavior;
    SetAMDCommon(caps, behavior);

    caps.unmasked_vendor = "AMD";
    caps.unmasked_renderer = "AMD Radeon RX 6700 XT";
    caps.vendor = "Google Inc. (AMD)";
    caps.renderer = "ANGLE (AMD, AMD Radeon RX 6700 XT Direct3D11 vs_5_0 ps_5_0, D3D11)";

    profile->SetCapabilities(caps);
    profile->SetRenderBehavior(behavior);
    return profile;
}

std::shared_ptr<GPUProfile> GPUProfileFactory::CreateAMD_RX7800XT() {
    auto profile = CreateAMD_RX6700XT();
    profile->SetId("amd-rx7800xt");
    profile->SetName("AMD Radeon RX 7800 XT");
    profile->SetArchitecture(GPUArchitecture::AMD_RDNA3);
    auto& caps = profile->GetCapabilitiesMutable();
    caps.unmasked_renderer = "AMD Radeon RX 7800 XT";
    return profile;
}

std::shared_ptr<GPUProfile> GPUProfileFactory::CreateAMD_RX7900XTX() {
    auto profile = CreateAMD_RX7800XT();
    profile->SetId("amd-rx7900xtx");
    profile->SetName("AMD Radeon RX 7900 XTX");
    auto& caps = profile->GetCapabilitiesMutable();
    caps.unmasked_renderer = "AMD Radeon RX 7900 XTX";
    return profile;
}

std::shared_ptr<GPUProfile> GPUProfileFactory::CreateAMD_RX9070XT() {
    auto profile = CreateAMD_RX7800XT();
    profile->SetId("amd-rx9070xt");
    profile->SetName("AMD Radeon RX 9070 XT");
    profile->SetArchitecture(GPUArchitecture::AMD_RDNA4);
    auto& caps = profile->GetCapabilitiesMutable();
    caps.unmasked_renderer = "AMD Radeon RX 9070 XT";
    return profile;
}

std::shared_ptr<GPUProfile> GPUProfileFactory::CreateAppleM1() {
    auto profile = CreateAppleM3();
    profile->SetId("apple-m1");
    profile->SetName("Apple M1");
    profile->SetArchitecture(GPUArchitecture::Apple_M1);
    auto& caps = profile->GetCapabilitiesMutable();
    caps.unmasked_renderer = "Apple M1";
    caps.renderer = "Apple M1";
    return profile;
}

std::shared_ptr<GPUProfile> GPUProfileFactory::CreateAppleM2() {
    auto profile = CreateAppleM3();
    profile->SetId("apple-m2");
    profile->SetName("Apple M2");
    profile->SetArchitecture(GPUArchitecture::Apple_M2);
    auto& caps = profile->GetCapabilitiesMutable();
    caps.unmasked_renderer = "Apple M2";
    caps.renderer = "Apple M2";
    return profile;
}

std::shared_ptr<GPUProfile> GPUProfileFactory::CreateAppleM4() {
    auto profile = CreateAppleM3();
    profile->SetId("apple-m4");
    profile->SetName("Apple M4");
    profile->SetArchitecture(GPUArchitecture::Apple_M4);
    auto& caps = profile->GetCapabilitiesMutable();
    caps.unmasked_renderer = "Apple M4";
    caps.renderer = "Apple M4";
    return profile;
}

std::shared_ptr<GPUProfile> GPUProfileFactory::CreateAppleM4Ultra() {
    auto profile = CreateAppleM4();
    profile->SetId("apple-m4ultra");
    profile->SetName("Apple M4 Ultra");
    auto& caps = profile->GetCapabilitiesMutable();
    caps.unmasked_renderer = "Apple M4 Ultra";
    caps.renderer = "Apple M4 Ultra";
    return profile;
}

std::shared_ptr<GPUProfile> GPUProfileFactory::CreateFromRealGPU() {
    // This would query the actual GPU - for now return a default profile
    return CreateAppleM3();
}

// ============================================================================
// GPUProfileRegistry Implementation
// ============================================================================

GPUProfileRegistry& GPUProfileRegistry::Instance() {
    static GPUProfileRegistry instance;
    return instance;
}

GPUProfileRegistry::GPUProfileRegistry() {
    // Register factory profiles on first access
    RegisterFactoryProfiles();
}

void GPUProfileRegistry::Register(std::shared_ptr<GPUProfile> profile) {
    if (!profile) return;
    std::lock_guard<std::mutex> lock(mutex_);
    profiles_[profile->GetId()] = profile;
}

std::shared_ptr<GPUProfile> GPUProfileRegistry::Get(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = profiles_.find(id);
    if (it != profiles_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::string> GPUProfileRegistry::GetAllIds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> ids;
    ids.reserve(profiles_.size());
    for (const auto& pair : profiles_) {
        ids.push_back(pair.first);
    }
    return ids;
}

std::vector<std::shared_ptr<GPUProfile>> GPUProfileRegistry::GetByVendor(GPUVendor vendor) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::shared_ptr<GPUProfile>> result;
    for (const auto& pair : profiles_) {
        if (pair.second->GetVendor() == vendor) {
            result.push_back(pair.second);
        }
    }
    return result;
}

std::vector<std::shared_ptr<GPUProfile>> GPUProfileRegistry::GetByArchitecture(GPUArchitecture arch) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::shared_ptr<GPUProfile>> result;
    for (const auto& pair : profiles_) {
        if (pair.second->GetArchitecture() == arch) {
            result.push_back(pair.second);
        }
    }
    return result;
}

void GPUProfileRegistry::LoadFromVMDatabase() {
    // Load all VMs from the database and create GPU profiles
    auto& vm_db = VirtualMachineDB::Instance();
    auto vm_ids = vm_db.GetVMIds();

    for (const auto& id : vm_ids) {
        const VirtualMachine* vm = vm_db.GetVM(id);
        if (vm) {
            auto profile = GPUProfileFactory::CreateFromVirtualMachine(*vm);
            Register(profile);
        }
    }
}

void GPUProfileRegistry::RegisterFactoryProfiles() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Intel
    profiles_["intel-uhd620"] = GPUProfileFactory::CreateIntelUHD620();
    profiles_["intel-iris-xe"] = GPUProfileFactory::CreateIntelIrisXe();
    profiles_["intel-arc-a770"] = GPUProfileFactory::CreateIntelArcA770();
    profiles_["intel-battlemage-b580"] = GPUProfileFactory::CreateIntelBattlemageB580();

    // NVIDIA
    profiles_["nvidia-rtx2080"] = GPUProfileFactory::CreateNVIDIA_RTX2080();
    profiles_["nvidia-rtx3060"] = GPUProfileFactory::CreateNVIDIA_RTX3060();
    profiles_["nvidia-rtx3080"] = GPUProfileFactory::CreateNVIDIA_RTX3080();
    profiles_["nvidia-rtx4070"] = GPUProfileFactory::CreateNVIDIA_RTX4070();
    profiles_["nvidia-rtx4090"] = GPUProfileFactory::CreateNVIDIA_RTX4090();
    profiles_["nvidia-rtx5090"] = GPUProfileFactory::CreateNVIDIA_RTX5090();

    // AMD
    profiles_["amd-rx6700xt"] = GPUProfileFactory::CreateAMD_RX6700XT();
    profiles_["amd-rx7800xt"] = GPUProfileFactory::CreateAMD_RX7800XT();
    profiles_["amd-rx7900xtx"] = GPUProfileFactory::CreateAMD_RX7900XTX();
    profiles_["amd-rx9070xt"] = GPUProfileFactory::CreateAMD_RX9070XT();

    // Apple
    profiles_["apple-m1"] = GPUProfileFactory::CreateAppleM1();
    profiles_["apple-m2"] = GPUProfileFactory::CreateAppleM2();
    profiles_["apple-m3"] = GPUProfileFactory::CreateAppleM3();
    profiles_["apple-m4"] = GPUProfileFactory::CreateAppleM4();
    profiles_["apple-m4ultra"] = GPUProfileFactory::CreateAppleM4Ultra();
}

} // namespace gpu
} // namespace owl
