#include "stealth/owl_virtual_machine.h"
#include "stealth/owl_fingerprint_generator.h"
#include "stealth/owl_font_spoofer.h"
#include "gpu/owl_gpu_virtualization.h"
#include "gpu/owl_gpu_profile.h"
#include "gpu/owl_gpu_context.h"
#include <sstream>
#include <iomanip>
#include <random>
#include <algorithm>
#include <ctime>
#include <fstream>
#include <cstring>

#ifdef OWL_USE_SQLCIPHER
// SQLITE_HAS_CODEC enables the sqlite3_key() function declaration in SQLCipher
#define SQLITE_HAS_CODEC 1
#include <sqlcipher/sqlite3.h>
#include "stealth/owl_vm_db_key.h"
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <libgen.h>
#elif defined(__linux__)
#include <unistd.h>
#include <linux/limits.h>
#endif

#include "util/logger.h"

namespace owl {

// ============================================================================
// VirtualMachine Implementation
// ============================================================================

bool VirtualMachine::Validate() const {
    // Basic validation - ensure critical fields are consistent

    // Platform must match OS
    if (os.name == "Windows" && os.platform != "Win32") return false;
    if (os.name == "Linux" && os.platform.find("Linux") == std::string::npos) return false;
    if (os.name == "macOS" && os.platform != "MacIntel") return false;

    // User-Agent must contain OS indicators
    if (os.name == "Windows" && browser.user_agent.find("Windows") == std::string::npos) return false;
    if (os.name == "Linux" && browser.user_agent.find("Linux") == std::string::npos) return false;
    if (os.name == "macOS" && browser.user_agent.find("Mac") == std::string::npos) return false;

    // Browser vendor must match browser type
    if (browser.name == "Chrome" && browser.vendor != "Google Inc.") return false;
    if (browser.name == "Firefox" && !browser.vendor.empty()) return false;
    if (browser.name == "Safari" && browser.vendor != "Apple Computer, Inc.") return false;

    return true;
}

std::string VirtualMachine::GetFingerprintHash() const {
    std::stringstream ss;
    ss << os.platform << "|" << browser.user_agent << "|"
       << gpu.unmasked_vendor << "|" << gpu.unmasked_renderer << "|"
       << cpu.hardware_concurrency << "|" << screen.width << "x" << screen.height;
    return ss.str();
}

// ============================================================================
// GPU Virtualization Integration
// ============================================================================

namespace {
    // Track GPU context per VM profile
    std::mutex g_gpu_context_mutex;
    std::map<std::string, std::shared_ptr<gpu::GPUContext>> g_gpu_contexts;
}

std::shared_ptr<gpu::GPUContext> ApplyGPUVirtualization(const VirtualMachine& vm) {
    // Initialize GPU virtualization system if not already done
    auto& gpu_system = gpu::GPUVirtualizationSystem::Instance();
    if (!gpu_system.IsInitialized()) {
        gpu::GPUVirtualizationConfig config;
        config.enable_render_normalization = true;
        config.apply_deterministic_noise = true;
        config.noise_intensity = 0.02;
        config.normalize_antialiasing = true;
        config.normalize_color_space = true;
        config.enable_timing_normalization = true;
        config.timing_quantum_us = 100;
        config.add_timing_jitter = true;
        config.max_jitter_ratio = 0.05;

        if (!gpu_system.Initialize(config)) {
            LOG_ERROR("VirtualMachine", "Failed to initialize GPU virtualization system");
            return nullptr;
        }
        LOG_DEBUG("VirtualMachine", "GPU virtualization system initialized");
    }

    // Create GPU profile from VirtualMachine
    auto gpu_profile = gpu::GPUProfileFactory::CreateFromVirtualMachine(vm);
    if (!gpu_profile) {
        LOG_ERROR("VirtualMachine", "Failed to create GPU profile for VM: " + vm.id);
        return nullptr;
    }

    // Create GPU context
    auto context = gpu_system.CreateContext(*gpu_profile);
    if (!context) {
        LOG_ERROR("VirtualMachine", "Failed to create GPU context for VM: " + vm.id);
        return nullptr;
    }

    // Store context for this VM
    {
        std::lock_guard<std::mutex> lock(g_gpu_context_mutex);
        g_gpu_contexts[vm.id] = context;
    }

    // Make this context current for the calling thread
    gpu_system.MakeContextCurrent(context.get());

    LOG_DEBUG("VirtualMachine", "GPU virtualization applied for VM: " + vm.id +
             " (GPU: " + vm.gpu.unmasked_renderer + ")");

    return context;
}

std::shared_ptr<gpu::GPUContext> GetGPUContext(const std::string& vm_id) {
    std::lock_guard<std::mutex> lock(g_gpu_context_mutex);
    auto it = g_gpu_contexts.find(vm_id);
    if (it != g_gpu_contexts.end()) {
        return it->second;
    }
    return nullptr;
}

void ClearGPUContext(const std::string& vm_id) {
    std::lock_guard<std::mutex> lock(g_gpu_context_mutex);
    g_gpu_contexts.erase(vm_id);
}

void MakeGPUContextCurrent(const std::string& vm_id) {
    auto context = GetGPUContext(vm_id);
    if (context) {
        gpu::GPUVirtualizationSystem::Instance().MakeContextCurrent(context.get());
    } else {
        gpu::GPUVirtualizationSystem::Instance().MakeContextCurrent(nullptr);
    }
}

// ============================================================================
// VirtualMachineDB Implementation
// ============================================================================

VirtualMachineDB& VirtualMachineDB::Instance() {
    static VirtualMachineDB instance;
    return instance;
}

VirtualMachineDB::VirtualMachineDB() {
    InitializeVMs();
}

void VirtualMachineDB::InitializeVMs() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) return;

    // Try to load from encrypted database first
    if (LoadFromDatabase()) {
        loaded_from_db_ = true;
        LOG_INFO("VirtualMachineDB", "Loaded " + std::to_string(vms_.size()) + " profiles from database");
    } else {
        // Fallback: No profiles available without database
        LOG_INFO("VirtualMachineDB", "Failed to load profiles from database. Run: npm run build:profiles");
    }

    // Build index
    for (size_t i = 0; i < vms_.size(); ++i) {
        vm_index_[vms_[i].id] = i;
    }

    initialized_ = true;
}

std::string VirtualMachineDB::GetDatabasePath() const {
#ifdef __APPLE__
    // macOS: Look in app bundle Resources
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        std::string exe_path(path);

        // Check if we're in a helper app (inside Frameworks folder)
        // Helper path: .../owl_browser.app/Contents/Frameworks/owl_browser Helper (Renderer).app/Contents/MacOS/...
        // Main app path: .../owl_browser.app/Contents/MacOS/owl_browser
        size_t frameworks_pos = exe_path.find("/Contents/Frameworks/");
        if (frameworks_pos != std::string::npos) {
            // We're in a helper app - navigate to main app's Resources
            return exe_path.substr(0, frameworks_pos) + "/Contents/Resources/data/profiles/vm_profiles.db";
        }

        // We're in the main app - navigate from MacOS to Resources
        size_t pos = exe_path.rfind("/Contents/MacOS/");
        if (pos != std::string::npos) {
            return exe_path.substr(0, pos) + "/Contents/Resources/data/profiles/vm_profiles.db";
        }
    }
    // Fallback for development
    return "data/profiles/vm_profiles.db";
#elif defined(__linux__)
    // Linux: Look relative to executable
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        std::string exe_path(path);
        size_t pos = exe_path.rfind('/');
        if (pos != std::string::npos) {
            return exe_path.substr(0, pos) + "/data/profiles/vm_profiles.db";
        }
    }
    // Fallback for development
    return "data/profiles/vm_profiles.db";
#else
    return "data/profiles/vm_profiles.db";
#endif
}

bool VirtualMachineDB::LoadFromDatabase() {
#ifndef OWL_USE_SQLCIPHER
    LOG_WARN("VirtualMachineDB", "SQLCipher not available, cannot load encrypted database");
    return false;
#else
    std::string db_path = GetDatabasePath();

    // Check if file exists
    std::ifstream f(db_path);
    if (!f.good()) {
        LOG_WARN("VirtualMachineDB", "Database not found at: " + db_path);
        return false;
    }
    f.close();

    // Get decrypted password
    std::string password = vm_db::GetDatabasePassword();
    if (password.empty()) {
        LOG_ERROR("VirtualMachineDB", "Failed to get database password");
        return false;
    }

    sqlite3* db = nullptr;
    int rc = sqlite3_open(db_path.c_str(), &db);
    if (rc != SQLITE_OK) {
        LOG_ERROR("VirtualMachineDB", "Failed to open database: " + std::string(sqlite3_errmsg(db)));
        sqlite3_close(db);
        return false;
    }

    // Apply encryption key
    rc = sqlite3_key(db, password.c_str(), static_cast<int>(password.size()));
    if (rc != SQLITE_OK) {
        LOG_ERROR("VirtualMachineDB", "Failed to set encryption key");
        sqlite3_close(db);
        return false;
    }

    // Apply SQLCipher cipher settings - MUST match settings used in build_vm_profiles.py
    // These are required because different SQLCipher builds (Homebrew vs apt) may have
    // different default settings. The database was created with these explicit settings.
    sqlite3_exec(db, "PRAGMA cipher_page_size = 4096;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA kdf_iter = 256000;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA cipher_hmac_algorithm = HMAC_SHA512;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA cipher_kdf_algorithm = PBKDF2_HMAC_SHA512;", nullptr, nullptr, nullptr);

    // Test that we can read (validates the key)
    const char* test_sql = "SELECT COUNT(*) FROM vm_profiles;";
    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db, test_sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("VirtualMachineDB", "Failed to decrypt database (wrong key?)");
        sqlite3_close(db);
        return false;
    }
    sqlite3_finalize(stmt);

    // Load all profiles
    const char* select_sql = R"(
        SELECT
            id, name, description,
            os_name, os_version, os_platform, os_oscpu, os_app_version, os_max_touch_points,
            browser_name, browser_version, browser_vendor, browser_user_agent,
            browser_app_name, browser_app_code_name, browser_product, browser_product_sub,
            browser_build_id, browser_webdriver, browser_pdf_viewer_enabled,
            browser_cookies_enabled, browser_java_enabled,
            cpu_hardware_concurrency, cpu_device_memory, cpu_architecture,
            gpu_vendor, gpu_renderer, gpu_unmasked_vendor, gpu_unmasked_renderer,
            gpu_webgl_version, gpu_webgl2_version, gpu_shading_language,
            gpu_max_texture_size, gpu_max_cube_map_texture_size, gpu_max_render_buffer_size,
            gpu_max_vertex_attribs, gpu_max_vertex_uniform_vectors, gpu_max_vertex_texture_units,
            gpu_max_varying_vectors, gpu_max_fragment_uniform_vectors, gpu_max_texture_units,
            gpu_max_combined_texture_units, gpu_max_viewport_dims_w, gpu_max_viewport_dims_h,
            gpu_aliased_line_width_min, gpu_aliased_line_width_max,
            gpu_aliased_point_size_min, gpu_aliased_point_size_max,
            gpu_max_samples, gpu_max_anisotropy, gpu_renderer_hash_seed,
            gpu_shader_precision, gpu_webgl_extensions,
            screen_width, screen_height, screen_avail_width, screen_avail_height,
            screen_color_depth, screen_pixel_depth, screen_device_pixel_ratio,
            screen_orientation_type, screen_orientation_angle,
            audio_sample_rate, audio_max_channel_count, audio_number_of_inputs,
            audio_number_of_outputs, audio_channel_count, audio_channel_count_mode,
            audio_channel_interpretation, audio_base_latency, audio_output_latency,
            audio_hash_seed,
            canvas_hash_seed, canvas_apply_noise, canvas_noise_intensity,
            fonts_installed, fonts_default_serif, fonts_default_sans_serif, fonts_default_monospace,
            timezone_iana_name, timezone_offset_minutes, timezone_has_dst,
            language_primary, language_list,
            network_connection_type, network_downlink, network_rtt, network_effective_type, network_save_data,
            media_audio_codecs, media_video_codecs, media_has_microphone, media_has_camera, media_has_speakers,
            permissions_geolocation, permissions_notifications, permissions_camera,
            permissions_microphone, permissions_midi, permissions_clipboard_read, permissions_clipboard_write,
            client_hints_enabled, client_hints_sec_ch_ua, client_hints_sec_ch_ua_platform,
            client_hints_sec_ch_ua_mobile, client_hints_sec_ch_ua_full_version,
            client_hints_sec_ch_ua_arch, client_hints_sec_ch_ua_bitness, client_hints_sec_ch_ua_model,
            storage_quota, storage_usage,
            battery_enabled, battery_level, battery_charging, battery_charging_time, battery_discharging_time
        FROM vm_profiles;
    )";

    rc = sqlite3_prepare_v2(db, select_sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("VirtualMachineDB", "Failed to prepare SELECT: " + std::string(sqlite3_errmsg(db)));
        sqlite3_close(db);
        return false;
    }

    // Helper to get string column
    auto get_text = [&stmt](int col) -> std::string {
        const unsigned char* text = sqlite3_column_text(stmt, col);
        return text ? reinterpret_cast<const char*>(text) : "";
    };

    // Helper to parse comma-separated list
    auto parse_list = [](const std::string& str) -> std::vector<std::string> {
        std::vector<std::string> result;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, ',')) {
            // Trim whitespace
            size_t start = item.find_first_not_of(" \t");
            size_t end = item.find_last_not_of(" \t");
            if (start != std::string::npos && end != std::string::npos) {
                result.push_back(item.substr(start, end - start + 1));
            }
        }
        return result;
    };

    // Helper to parse hex string to uint64
    auto parse_hex = [](const std::string& str) -> uint64_t {
        if (str.empty()) return 0;
        return std::stoull(str, nullptr, 16);
    };

    int col = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        VirtualMachine vm;
        col = 0;

        // Identity
        vm.id = get_text(col++);
        vm.name = get_text(col++);
        vm.description = get_text(col++);

        // OS
        vm.os.name = get_text(col++);
        vm.os.version = get_text(col++);
        vm.os.platform = get_text(col++);
        vm.os.oscpu = get_text(col++);
        vm.os.app_version = get_text(col++);
        vm.os.max_touch_points = sqlite3_column_int(stmt, col++);

        // Browser
        vm.browser.name = get_text(col++);
        vm.browser.version = get_text(col++);
        vm.browser.vendor = get_text(col++);
        vm.browser.user_agent = get_text(col++);
        vm.browser.app_name = get_text(col++);
        vm.browser.app_code_name = get_text(col++);
        vm.browser.product = get_text(col++);
        vm.browser.product_sub = get_text(col++);
        vm.browser.build_id = get_text(col++);
        vm.browser.webdriver = sqlite3_column_int(stmt, col++) != 0;
        vm.browser.pdf_viewer_enabled = sqlite3_column_int(stmt, col++) != 0;
        vm.browser.cookies_enabled = sqlite3_column_int(stmt, col++) != 0;
        vm.browser.java_enabled = sqlite3_column_int(stmt, col++) != 0;

        // CPU
        vm.cpu.hardware_concurrency = sqlite3_column_int(stmt, col++);
        vm.cpu.device_memory = sqlite3_column_int(stmt, col++);
        vm.cpu.architecture = get_text(col++);

        // GPU
        vm.gpu.vendor = get_text(col++);
        vm.gpu.renderer = get_text(col++);
        vm.gpu.unmasked_vendor = get_text(col++);
        vm.gpu.unmasked_renderer = get_text(col++);
        vm.gpu.webgl_version = get_text(col++);
        vm.gpu.webgl2_version = get_text(col++);
        vm.gpu.shading_language = get_text(col++);
        vm.gpu.max_texture_size = sqlite3_column_int(stmt, col++);
        vm.gpu.max_cube_map_texture_size = sqlite3_column_int(stmt, col++);
        vm.gpu.max_render_buffer_size = sqlite3_column_int(stmt, col++);
        vm.gpu.max_vertex_attribs = sqlite3_column_int(stmt, col++);
        vm.gpu.max_vertex_uniform_vectors = sqlite3_column_int(stmt, col++);
        vm.gpu.max_vertex_texture_units = sqlite3_column_int(stmt, col++);
        vm.gpu.max_varying_vectors = sqlite3_column_int(stmt, col++);
        vm.gpu.max_fragment_uniform_vectors = sqlite3_column_int(stmt, col++);
        vm.gpu.max_texture_units = sqlite3_column_int(stmt, col++);
        vm.gpu.max_combined_texture_units = sqlite3_column_int(stmt, col++);
        vm.gpu.max_viewport_dims_w = sqlite3_column_int(stmt, col++);
        vm.gpu.max_viewport_dims_h = sqlite3_column_int(stmt, col++);
        vm.gpu.aliased_line_width_min = static_cast<float>(sqlite3_column_double(stmt, col++));
        vm.gpu.aliased_line_width_max = static_cast<float>(sqlite3_column_double(stmt, col++));
        vm.gpu.aliased_point_size_min = static_cast<float>(sqlite3_column_double(stmt, col++));
        vm.gpu.aliased_point_size_max = static_cast<float>(sqlite3_column_double(stmt, col++));
        vm.gpu.max_samples = sqlite3_column_int(stmt, col++);
        // Multisampling parameters (critical for VM detection!)
        // Default to 4/1 which is typical for real Chrome on desktop
        vm.gpu.samples = 4;
        vm.gpu.sample_buffers = 1;
        vm.gpu.max_anisotropy = static_cast<float>(sqlite3_column_double(stmt, col++));
        vm.gpu.renderer_hash_seed = parse_hex(get_text(col++));

        // Shader precision (JSON) - parse later if needed
        std::string precision_json = get_text(col++);

        // Set shader precision values based on GPU vendor/type
        // Different GPUs have different precision characteristics
        // This affects the webGlExtensions.shaderPrecisions hash
        std::string renderer_lower = vm.gpu.unmasked_renderer;
        std::transform(renderer_lower.begin(), renderer_lower.end(), renderer_lower.begin(), ::tolower);

        if (renderer_lower.find("nvidia") != std::string::npos) {
            // NVIDIA desktop GPUs - vary precision slightly to distinguish from Apple
            vm.gpu.vertex_high_float = {127, 127, 23};
            vm.gpu.vertex_medium_float = {127, 127, 23};
            vm.gpu.vertex_low_float = {127, 127, 23};
            vm.gpu.vertex_high_int = {31, 30, 0};
            vm.gpu.vertex_medium_int = {30, 29, 0}; // Unique to NVIDIA
            vm.gpu.vertex_low_int = {31, 30, 0};
            vm.gpu.fragment_high_float = {127, 127, 23};
            vm.gpu.fragment_medium_float = {127, 127, 23};
            vm.gpu.fragment_low_float = {127, 127, 23};
            vm.gpu.fragment_high_int = {31, 30, 0};
            vm.gpu.fragment_medium_int = {30, 29, 0}; // Unique to NVIDIA
            vm.gpu.fragment_low_int = {31, 30, 0};
        } else if (renderer_lower.find("amd") != std::string::npos ||
                   renderer_lower.find("radeon") != std::string::npos) {
            // AMD GPUs - vary precision slightly to distinguish from NVIDIA
            vm.gpu.vertex_high_float = {127, 127, 23};
            vm.gpu.vertex_medium_float = {126, 126, 23}; // Unique to AMD
            vm.gpu.vertex_low_float = {127, 127, 23};
            vm.gpu.vertex_high_int = {31, 30, 0};
            vm.gpu.vertex_medium_int = {31, 30, 0};
            vm.gpu.vertex_low_int = {31, 30, 0};
            vm.gpu.fragment_high_float = {127, 127, 23};
            vm.gpu.fragment_medium_float = {126, 126, 23}; // Unique to AMD
            vm.gpu.fragment_low_float = {127, 127, 23};
            vm.gpu.fragment_high_int = {31, 30, 0};
            vm.gpu.fragment_medium_int = {31, 30, 0};
            vm.gpu.fragment_low_int = {31, 30, 0};
        } else if (renderer_lower.find("intel") != std::string::npos) {
            // Intel integrated GPUs - vary float and int precision
            vm.gpu.vertex_high_float = {127, 127, 23};
            vm.gpu.vertex_medium_float = {125, 125, 23}; // Unique to Intel
            vm.gpu.vertex_low_float = {127, 127, 23};
            vm.gpu.vertex_high_int = {31, 30, 0};
            vm.gpu.vertex_medium_int = {30, 30, 0}; // Unique to Intel
            vm.gpu.vertex_low_int = {31, 30, 0};
            vm.gpu.fragment_high_float = {127, 127, 23};
            vm.gpu.fragment_medium_float = {125, 125, 23}; // Unique to Intel
            vm.gpu.fragment_low_float = {127, 127, 23};
            vm.gpu.fragment_high_int = {31, 30, 0};
            vm.gpu.fragment_medium_int = {30, 30, 0}; // Unique to Intel
            vm.gpu.fragment_low_int = {31, 30, 0};
        } else if (renderer_lower.find("apple") != std::string::npos ||
                   renderer_lower.find("m1") != std::string::npos ||
                   renderer_lower.find("m2") != std::string::npos ||
                   renderer_lower.find("m3") != std::string::npos ||
                   renderer_lower.find("m4") != std::string::npos) {
            // Apple Silicon (M1/M2/M3/M4) - uses Metal backend with UNIFORM precision
            // All float precision types return the same values (no tiered precision)
            // All int precision types return the same values
            // This is a defining characteristic of Apple Silicon GPUs!
            vm.gpu.vertex_high_float = {127, 127, 23};
            vm.gpu.vertex_medium_float = {127, 127, 23};  // Same as high (uniform precision)
            vm.gpu.vertex_low_float = {127, 127, 23};     // Same as high (uniform precision)
            vm.gpu.vertex_high_int = {31, 30, 0};
            vm.gpu.vertex_medium_int = {31, 30, 0};       // Same as high (uniform precision)
            vm.gpu.vertex_low_int = {31, 30, 0};          // Same as high (uniform precision)
            vm.gpu.fragment_high_float = {127, 127, 23};
            vm.gpu.fragment_medium_float = {127, 127, 23};  // Same as high (uniform precision)
            vm.gpu.fragment_low_float = {127, 127, 23};     // Same as high (uniform precision)
            vm.gpu.fragment_high_int = {31, 30, 0};
            vm.gpu.fragment_medium_int = {31, 30, 0};       // Same as high (uniform precision)
            vm.gpu.fragment_low_int = {31, 30, 0};          // Same as high (uniform precision)
        } else if (renderer_lower.find("mali") != std::string::npos) {
            // ARM Mali GPUs (mobile) - tiered precision
            vm.gpu.vertex_high_float = {127, 127, 23};
            vm.gpu.vertex_medium_float = {15, 15, 10};
            vm.gpu.vertex_low_float = {8, 8, 8};
            vm.gpu.vertex_high_int = {31, 30, 0};
            vm.gpu.vertex_medium_int = {15, 15, 0};
            vm.gpu.vertex_low_int = {8, 8, 0};
            vm.gpu.fragment_high_float = {127, 127, 23};
            vm.gpu.fragment_medium_float = {15, 15, 10};
            vm.gpu.fragment_low_float = {8, 8, 8};
            vm.gpu.fragment_high_int = {31, 30, 0};
            vm.gpu.fragment_medium_int = {15, 15, 0};
            vm.gpu.fragment_low_int = {8, 8, 0};
        } else if (renderer_lower.find("adreno") != std::string::npos) {
            // Qualcomm Adreno GPUs (mobile) - tiered precision
            vm.gpu.vertex_high_float = {127, 127, 23};
            vm.gpu.vertex_medium_float = {15, 15, 10};
            vm.gpu.vertex_low_float = {15, 15, 10};
            vm.gpu.vertex_high_int = {31, 30, 0};
            vm.gpu.vertex_medium_int = {15, 15, 0};
            vm.gpu.vertex_low_int = {15, 15, 0};
            vm.gpu.fragment_high_float = {127, 127, 23};
            vm.gpu.fragment_medium_float = {15, 15, 10};
            vm.gpu.fragment_low_float = {15, 15, 10};
            vm.gpu.fragment_high_int = {31, 30, 0};
            vm.gpu.fragment_medium_int = {15, 15, 0};
            vm.gpu.fragment_low_int = {15, 15, 0};
        } else if (renderer_lower.find("mesa") != std::string::npos) {
            // Mesa (Linux software/hardware rendering) - uniform high precision
            vm.gpu.vertex_high_float = {127, 127, 23};
            vm.gpu.vertex_medium_float = {127, 127, 23};
            vm.gpu.vertex_low_float = {127, 127, 23};
            vm.gpu.vertex_high_int = {31, 30, 0};
            vm.gpu.vertex_medium_int = {31, 30, 0};
            vm.gpu.vertex_low_int = {31, 30, 0};
            vm.gpu.fragment_high_float = {127, 127, 23};
            vm.gpu.fragment_medium_float = {127, 127, 23};
            vm.gpu.fragment_low_float = {127, 127, 23};
            vm.gpu.fragment_high_int = {31, 30, 0};
            vm.gpu.fragment_medium_int = {31, 30, 0};
            vm.gpu.fragment_low_int = {31, 30, 0};
        } else {
            // Default fallback (generic desktop) - uniform high precision
            vm.gpu.vertex_high_float = {127, 127, 23};
            vm.gpu.vertex_medium_float = {127, 127, 23};
            vm.gpu.vertex_low_float = {127, 127, 23};
            vm.gpu.vertex_high_int = {31, 30, 0};
            vm.gpu.vertex_medium_int = {31, 30, 0};
            vm.gpu.vertex_low_int = {31, 30, 0};
            vm.gpu.fragment_high_float = {127, 127, 23};
            vm.gpu.fragment_medium_float = {127, 127, 23};
            vm.gpu.fragment_low_float = {127, 127, 23};
            vm.gpu.fragment_high_int = {31, 30, 0};
            vm.gpu.fragment_medium_int = {31, 30, 0};
            vm.gpu.fragment_low_int = {31, 30, 0};
        }

        // WebGL parameters - override with GPU-specific values
        // These MUST match the claimed GPU or detection will fail!
        if (renderer_lower.find("apple") != std::string::npos ||
            renderer_lower.find("m1") != std::string::npos ||
            renderer_lower.find("m2") != std::string::npos ||
            renderer_lower.find("m3") != std::string::npos ||
            renderer_lower.find("m4") != std::string::npos) {
            // Apple Silicon (M1/M2/M3/M4) - Metal backend values
            // Override WebGL1 params to match real Apple Silicon Chrome values
            vm.gpu.max_varying_vectors = 30;           // Real value (was 31)
            vm.gpu.max_vertex_uniform_vectors = 1024;  // Real value (was 4096)
            vm.gpu.max_combined_texture_units = 32;    // Real value (was 80)
            // WebGL2-specific parameters
            vm.gpu.max_3d_texture_size = 2048;
            vm.gpu.max_array_texture_layers = 2048;
            vm.gpu.max_color_attachments = 8;
            vm.gpu.max_draw_buffers = 8;
            vm.gpu.max_uniform_buffer_bindings = 24;
            vm.gpu.max_uniform_block_size = 16384;
            vm.gpu.max_combined_uniform_blocks = 24;
        } else if (renderer_lower.find("nvidia") != std::string::npos) {
            // NVIDIA GPUs - typically higher limits
            vm.gpu.max_3d_texture_size = 2048;
            vm.gpu.max_array_texture_layers = 2048;
            vm.gpu.max_color_attachments = 8;
            vm.gpu.max_draw_buffers = 8;
            vm.gpu.max_uniform_buffer_bindings = 84;
            vm.gpu.max_uniform_block_size = 65536;
            vm.gpu.max_combined_uniform_blocks = 70;
        } else if (renderer_lower.find("amd") != std::string::npos ||
                   renderer_lower.find("radeon") != std::string::npos) {
            // AMD GPUs
            vm.gpu.max_3d_texture_size = 2048;
            vm.gpu.max_array_texture_layers = 2048;
            vm.gpu.max_color_attachments = 8;
            vm.gpu.max_draw_buffers = 8;
            vm.gpu.max_uniform_buffer_bindings = 75;
            vm.gpu.max_uniform_block_size = 65536;
            vm.gpu.max_combined_uniform_blocks = 60;
        } else if (renderer_lower.find("intel") != std::string::npos) {
            // Intel GPUs
            vm.gpu.max_3d_texture_size = 2048;
            vm.gpu.max_array_texture_layers = 2048;
            vm.gpu.max_color_attachments = 8;
            vm.gpu.max_draw_buffers = 8;
            vm.gpu.max_uniform_buffer_bindings = 72;
            vm.gpu.max_uniform_block_size = 65536;
            vm.gpu.max_combined_uniform_blocks = 60;
        } else if (renderer_lower.find("mali") != std::string::npos ||
                   renderer_lower.find("adreno") != std::string::npos) {
            // Mobile GPUs - lower limits
            vm.gpu.max_3d_texture_size = 2048;
            vm.gpu.max_array_texture_layers = 256;
            vm.gpu.max_color_attachments = 4;
            vm.gpu.max_draw_buffers = 4;
            vm.gpu.max_uniform_buffer_bindings = 24;
            vm.gpu.max_uniform_block_size = 16384;
            vm.gpu.max_combined_uniform_blocks = 24;
        } else {
            // Default fallback - conservative desktop values
            vm.gpu.max_3d_texture_size = 2048;
            vm.gpu.max_array_texture_layers = 2048;
            vm.gpu.max_color_attachments = 8;
            vm.gpu.max_draw_buffers = 8;
            vm.gpu.max_uniform_buffer_bindings = 24;
            vm.gpu.max_uniform_block_size = 16384;
            vm.gpu.max_combined_uniform_blocks = 24;
        }

        // WebGL extensions
        vm.gpu.webgl_extensions = parse_list(get_text(col++));

        // Screen
        vm.screen.width = sqlite3_column_int(stmt, col++);
        vm.screen.height = sqlite3_column_int(stmt, col++);
        vm.screen.avail_width = sqlite3_column_int(stmt, col++);
        vm.screen.avail_height = sqlite3_column_int(stmt, col++);
        vm.screen.color_depth = sqlite3_column_int(stmt, col++);
        vm.screen.pixel_depth = sqlite3_column_int(stmt, col++);
        vm.screen.device_pixel_ratio = static_cast<float>(sqlite3_column_double(stmt, col++));
        vm.screen.orientation_type = get_text(col++);
        vm.screen.orientation_angle = sqlite3_column_int(stmt, col++);

        // Audio
        vm.audio.sample_rate = static_cast<float>(sqlite3_column_int(stmt, col++));
        vm.audio.max_channel_count = sqlite3_column_int(stmt, col++);
        vm.audio.number_of_inputs = sqlite3_column_int(stmt, col++);
        vm.audio.number_of_outputs = sqlite3_column_int(stmt, col++);
        vm.audio.channel_count = sqlite3_column_int(stmt, col++);
        vm.audio.channel_count_mode = get_text(col++);
        vm.audio.channel_interpretation = get_text(col++);
        vm.audio.base_latency = static_cast<float>(sqlite3_column_double(stmt, col++));
        vm.audio.output_latency = static_cast<float>(sqlite3_column_double(stmt, col++));
        vm.audio.audio_hash_seed = parse_hex(get_text(col++));

        // Canvas
        vm.canvas.hash_seed = parse_hex(get_text(col++));
        vm.canvas.apply_noise = sqlite3_column_int(stmt, col++) != 0;
        vm.canvas.noise_intensity = sqlite3_column_double(stmt, col++);

        // Fonts
        vm.fonts.installed = parse_list(get_text(col++));
        vm.fonts.default_serif = get_text(col++);
        vm.fonts.default_sans_serif = get_text(col++);
        vm.fonts.default_monospace = get_text(col++);

        // Timezone
        vm.timezone.iana_name = get_text(col++);
        vm.timezone.offset_minutes = sqlite3_column_int(stmt, col++);
        vm.timezone.has_dst = sqlite3_column_int(stmt, col++) != 0;

        // Language
        vm.language.primary = get_text(col++);
        vm.language.languages = parse_list(get_text(col++));

        // Network
        vm.network.connection_type = get_text(col++);
        vm.network.downlink = static_cast<float>(sqlite3_column_double(stmt, col++));
        vm.network.rtt = static_cast<float>(sqlite3_column_int(stmt, col++));
        vm.network.effective_type = get_text(col++);
        vm.network.save_data = sqlite3_column_int(stmt, col++) != 0;

        // Media
        vm.media.audio_codecs = parse_list(get_text(col++));
        vm.media.video_codecs = parse_list(get_text(col++));
        vm.media.has_microphone = sqlite3_column_int(stmt, col++) != 0;
        vm.media.has_camera = sqlite3_column_int(stmt, col++) != 0;
        vm.media.has_speakers = sqlite3_column_int(stmt, col++) != 0;

        // Permissions
        vm.permissions.geolocation = get_text(col++);
        vm.permissions.notifications = get_text(col++);
        vm.permissions.camera = get_text(col++);
        vm.permissions.microphone = get_text(col++);
        vm.permissions.midi = get_text(col++);
        vm.permissions.clipboard_read = get_text(col++);
        vm.permissions.clipboard_write = get_text(col++);

        // Client hints
        vm.client_hints.enabled = sqlite3_column_int(stmt, col++) != 0;
        vm.client_hints.sec_ch_ua = get_text(col++);
        vm.client_hints.sec_ch_ua_platform = get_text(col++);
        vm.client_hints.sec_ch_ua_mobile = get_text(col++);
        vm.client_hints.sec_ch_ua_full_version = get_text(col++);
        vm.client_hints.sec_ch_ua_arch = get_text(col++);
        vm.client_hints.sec_ch_ua_bitness = get_text(col++);
        vm.client_hints.sec_ch_ua_model = get_text(col++);

        // Storage
        vm.storage.quota = static_cast<uint64_t>(sqlite3_column_int64(stmt, col++));
        vm.storage.usage = static_cast<uint64_t>(sqlite3_column_int64(stmt, col++));

        // Battery
        vm.battery.enabled = sqlite3_column_int(stmt, col++) != 0;
        vm.battery.level = static_cast<float>(sqlite3_column_double(stmt, col++));
        vm.battery.charging = sqlite3_column_int(stmt, col++) != 0;
        vm.battery.charging_time = static_cast<float>(sqlite3_column_double(stmt, col++));
        vm.battery.discharging_time = static_cast<float>(sqlite3_column_double(stmt, col++));

        vms_.push_back(vm);
    }

    sqlite3_finalize(stmt);

    // Load config values (browser_version, browser_version_full)
    const char* config_sql = "SELECT key, value FROM config WHERE key IN ('browser_version', 'browser_version_full');";
    rc = sqlite3_prepare_v2(db, config_sql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            if (key && value) {
                if (strcmp(key, "browser_version") == 0) {
                    browser_version_ = value;
                    LOG_DEBUG("VirtualMachineDB", "Loaded browser_version: " + browser_version_);
                } else if (strcmp(key, "browser_version_full") == 0) {
                    browser_version_full_ = value;
                    LOG_DEBUG("VirtualMachineDB", "Loaded browser_version_full: " + browser_version_full_);
                }
            }
        }
        sqlite3_finalize(stmt);
    } else {
        LOG_WARN("VirtualMachineDB", "Could not load config table, using defaults");
    }

    sqlite3_close(db);

    LOG_DEBUG("VirtualMachineDB", "Loaded " + std::to_string(vms_.size()) + " profiles from " + db_path);
    return !vms_.empty();
#endif
}

// Fallback stubs - profiles are loaded from database
void VirtualMachineDB::AddWindowsProfiles() {}
void VirtualMachineDB::AddUbuntuProfiles() {}
void VirtualMachineDB::AddMacOSProfiles() {}

const VirtualMachine* VirtualMachineDB::GetVM(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = vm_index_.find(id);
    if (it != vm_index_.end()) {
        return &vms_[it->second];
    }
    return nullptr;
}

std::vector<std::string> VirtualMachineDB::GetVMIds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> ids;
    for (const auto& vm : vms_) {
        ids.push_back(vm.id);
    }
    return ids;
}

std::vector<const VirtualMachine*> VirtualMachineDB::GetVMsByOS(const std::string& os) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<const VirtualMachine*> result;
    for (const auto& vm : vms_) {
        if (vm.os.name == os) {
            result.push_back(&vm);
        }
    }
    return result;
}

std::vector<const VirtualMachine*> VirtualMachineDB::GetVMsByBrowser(const std::string& browser) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<const VirtualMachine*> result;
    for (const auto& vm : vms_) {
        if (vm.browser.name == browser) {
            result.push_back(&vm);
        }
    }
    return result;
}

std::vector<const VirtualMachine*> VirtualMachineDB::GetVMsByGPU(const std::string& gpu_vendor) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<const VirtualMachine*> result;
    for (const auto& vm : vms_) {
        if (vm.gpu.unmasked_vendor.find(gpu_vendor) != std::string::npos) {
            result.push_back(&vm);
        }
    }
    return result;
}

const VirtualMachine* VirtualMachineDB::SelectRandomVM(
    const std::string& target_os,
    const std::string& target_browser,
    const std::string& target_gpu,
    uint64_t seed
) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<const VirtualMachine*> candidates;
    for (const auto& vm : vms_) {
        bool os_match = target_os.empty() || vm.os.name == target_os;
        bool browser_match = target_browser.empty() || vm.browser.name == target_browser;
        bool gpu_match = target_gpu.empty() ||
                         vm.gpu.unmasked_vendor.find(target_gpu) != std::string::npos;

        if (os_match && browser_match && gpu_match) {
            candidates.push_back(&vm);
        }
    }

    if (candidates.empty()) {
        return vms_.empty() ? nullptr : &vms_[0];
    }

    if (seed == 0) {
        seed = std::chrono::system_clock::now().time_since_epoch().count();
    }

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
    return candidates[dist(rng)];
}

size_t VirtualMachineDB::GetVMCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return vms_.size();
}

std::string VirtualMachineDB::GetBrowserVersion() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return browser_version_;
}

std::string VirtualMachineDB::GetBrowserVersionFull() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return browser_version_full_;
}

std::string VirtualMachineDB::GetDefaultUserAgent() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/" +
           browser_version_full_ + " Safari/537.36";
}

// ============================================================================
// VirtualMachineInjector Implementation
// ============================================================================

std::string VirtualMachineInjector::EscapeJS(const std::string& str) {
    std::stringstream ss;
    for (char c : str) {
        switch (c) {
            case '\\': ss << "\\\\"; break;
            case '"': ss << "\\\""; break;
            case '\'': ss << "\\'"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default: ss << c; break;
        }
    }
    return ss.str();
}

std::string VirtualMachineInjector::VectorToJSArray(const std::vector<std::string>& vec) {
    std::stringstream ss;
    ss << "[";
    for (size_t i = 0; i < vec.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << "\"" << EscapeJS(vec[i]) << "\"";
    }
    ss << "]";
    return ss.str();
}

std::string VirtualMachineInjector::GenerateMaskingUtility() {
    return R"(
// Native function masking utility - makes patched functions appear native
// Uses Proxy to wrap the original toString - this preserves native-like properties
(function() {
    const _owl = Symbol.for('owl');

    // Skip if already patched by owl_stealth.cc
    if (window[_owl] && window[_owl].__toStringPatched) {
        return;
    }

    // Initialize registry if not present
    if (!window[_owl]) {
        Object.defineProperty(window, _owl, {
            value: { fn: new Map() },
            writable: false, enumerable: false, configurable: false
        });
    }
    if (!window[_owl].fn) {
        window[_owl].fn = new Map();
    }

    const _nativeToString = Function.prototype.toString;
    const _toStringStr = 'function toString() { [native code] }';
    const _registry = window[_owl].fn;

    // Use Proxy to intercept toString calls
    // CRITICAL: Proxy inherits properties from target, so:
    // - No 'prototype' property (native toString doesn't have one)
    // - Correct 'length' and 'name' from original
    const _toStringProxy = new Proxy(_nativeToString, {
        apply(target, thisArg, args) {
            // Check the global registry
            if (_registry.has(thisArg)) {
                return _registry.get(thisArg);
            }
            // Fallback registry
            if (window.__vmFallbackRegistry && window.__vmFallbackRegistry.has(thisArg)) {
                return window.__vmFallbackRegistry.get(thisArg);
            }
            // Use original toString
            return _nativeToString.call(thisArg);
        }
    });

    // Install the proxy
    Function.prototype.toString = _toStringProxy;

    // Register proxy for self-reference
    _registry.set(_toStringProxy, _toStringStr);

    // Mark as patched
    window[_owl].__toStringPatched = true;
})();

// Access global registry created by owl_stealth.cc
// This Map is stored at window[Symbol.for('owl')].fn
const __vmGetRegistry = () => {
    const _owl = Symbol.for('owl');
    if (window[_owl] && window[_owl].fn) {
        return window[_owl].fn;
    }
    // Fallback: create local registry if global not available
    if (!window.__vmFallbackRegistry) {
        window.__vmFallbackRegistry = new Map();
    }
    return window.__vmFallbackRegistry;
};

const __vmMask = (fn, name, originalFn) => {
    const _owl = Symbol.for('owl');

    // Use createNativeProxy from global registry for proper introspection bypass
    const createNativeProxy = window[_owl]?.createNativeProxy;

    // If we have createNativeProxy and original function, use it
    if (createNativeProxy && originalFn) {
        return createNativeProxy(originalFn, (target, thisArg, args) => {
            return fn.apply(thisArg, args);
        });
    }

    // Fallback: create Proxy manually
    if (originalFn) {
        const nativeStr = `function ${name}() { [native code] }`;
        const registry = __vmGetRegistry();
        const proxy = new Proxy(originalFn, {
            apply(target, thisArg, args) {
                return fn.apply(thisArg, args);
            }
        });
        registry.set(proxy, nativeStr);
        if (window[_owl] && window[_owl].registerProxy) {
            window[_owl].registerProxy(proxy, originalFn);
        }
        return proxy;
    }

    // Last resort fallback for functions without original
    const nativeStr = `function ${name}() { [native code] }`;
    const registry = __vmGetRegistry();
    try {
        const ownKeys = Reflect.ownKeys(fn);
        for (const key of ownKeys) {
            if (key !== 'length' && key !== 'name') {
                try {
                    const desc = Object.getOwnPropertyDescriptor(fn, key);
                    if (desc && !desc.configurable) {
                        Object.defineProperty(fn, key, { configurable: true });
                    }
                    delete fn[key];
                } catch(e) {}
            }
        }
        Object.defineProperty(fn, 'name', {
            value: name, writable: false, enumerable: false, configurable: true
        });
        Object.defineProperty(fn, 'length', {
            value: 0, writable: false, enumerable: false, configurable: true
        });
        registry.set(fn, nativeStr);
    } catch (e) {}

    return fn;
};

// Helper to make a getter/setter look like a native one
// Native getters have: no prototype, name='get X', length=0, only 'length' and 'name' properties
const __vmMakeNativeGetter = (fn, propName) => {
    // Just delegate to __vmMask with proper getter name format
    return __vmMask(fn, `get ${propName}`, null);
};
)";
}

std::string VirtualMachineInjector::GenerateScript(const VirtualMachine& vm, const std::string& context_id,
                                                   const VMOverrides& overrides) {
    // Apply GPU virtualization at native level
    // This sets up the GPU context to intercept GL calls and spoof GPU identity
    auto gpu_context = ApplyGPUVirtualization(vm);
    if (gpu_context) {
        LOG_DEBUG("VirtualMachineInjector", "GPU virtualization active for " + vm.id);
    }

    // Get fingerprint seeds for this context
    // If context_id is provided, use OwlFingerprintGenerator for unique-per-context seeds
    // This ensures unlimited unique fingerprints (not limited by DB profile count)
    FingerprintSeeds seeds;
    bool use_dynamic_seeds = !context_id.empty();

    if (use_dynamic_seeds) {
        seeds = OwlFingerprintGenerator::Instance().GetSeeds(context_id);
        LOG_INFO("VirtualMachineInjector",
            "Using dynamic fingerprint seeds for context " + context_id +
            " (Canvas: " + seeds.canvas_hex + ", Audio: " + std::to_string(seeds.audio_fingerprint) +
            ", ExtParams: " + seeds.webgl_ext_params_hash + ")");
    } else {
        // Generate unique seeds even without context_id
        // Create a temporary context_id based on VM id and canvas hash seed for uniqueness
        std::string temp_context_id = "vm_" + vm.id + "_" + std::to_string(vm.canvas.hash_seed);
        seeds = OwlFingerprintGenerator::Instance().GetSeeds(temp_context_id);
        LOG_DEBUG("VirtualMachineInjector",
            "Generated unique seeds for VM " + vm.id + " (temp_context: " + temp_context_id + ")");
    }

    std::stringstream ss;
    ss << "(function() {\n'use strict';\n\n";

    // =========================================================================
    // CRITICAL: Detect actual RENDERING DPR through measurement
    // CEF headless mode reports devicePixelRatio = 1 but renders at host DPR!
    // We detect the actual scale by measuring text before any hooks interfere.
    // =========================================================================
    ss << R"(
// Detect actual rendering DPR through measurement (CEF headless workaround)
(function() {
    const _owl = Symbol.for('owl');
    if (!window[_owl]) {
        Object.defineProperty(window, _owl, {
            value: { font: {}, webgl: {}, camera: {} },
            writable: false, enumerable: false, configurable: false
        });
    }
    // Ensure sub-objects exist even if window[_owl] was created elsewhere
    if (!window[_owl].font) window[_owl].font = {};
    if (!window[_owl].webgl) window[_owl].webgl = {};
    if (!window[_owl].camera) window[_owl].camera = {};

    if (typeof window[_owl].font.actualDPR === 'undefined') {
        try {
            // Measure text to detect actual rendering DPR
            const canvas = document.createElement('canvas');
            canvas.width = 500;
            canvas.height = 100;
            const ctx = canvas.getContext('2d');
            ctx.font = '72px monospace';
            const measured = ctx.measureText('mmmmmmmmmmlli').width;
            // Reference: ~468px at DPR=1
            const detected = measured / 468;
            const commonDPRs = [1, 1.25, 1.5, 1.75, 2, 2.25, 2.5, 2.75, 3];
            let closest = 1;
            for (const dpr of commonDPRs) {
                if (Math.abs(detected - dpr) < Math.abs(detected - closest)) closest = dpr;
            }
            const reported = window.devicePixelRatio || 1;
            window[_owl].font.actualDPR = (Math.abs(reported - closest) < 0.1) ? reported : closest;
        } catch (e) {
            window[_owl].font.actualDPR = window.devicePixelRatio || 1;
        }
    }
})();
)";

    // Add masking utility
    ss << GenerateMaskingUtility();

    // Inject all fingerprint seeds as global config
    // These are dynamically generated per-context for unique fingerprints
    ss << "\n// Global fingerprint seeds for this context\n";
    ss << "const __vmCanvasSeed = 0x" << std::hex << seeds.canvas_seed << std::dec << "n;\n";
    ss << "const __vmWebGLSeed = 0x" << std::hex << seeds.webgl_seed << std::dec << "n;\n";
    ss << "const __vmAudioSeed = 0x" << std::hex << seeds.audio_seed << std::dec << "n;\n";
    // Store seeds on window using Symbol for context-aware hooks
    // This allows shared prototype hooks to use per-context seeds
    ss << "window[Symbol.for('__owl_canvas_seed__')] = __vmCanvasSeed;\n";
    ss << "window[Symbol.for('__owl_audio_seed__')] = __vmAudioSeed;\n";
    ss << "const __vmFontsSeed = 0x" << std::hex << seeds.fonts_seed << std::dec << "n;\n";
    ss << "window[Symbol.for('__owl_fonts_seed__')] = __vmFontsSeed;\n";
    ss << "const __vmFingerprintSeeds = {\n";
    ss << "    audioFingerprint: " << std::fixed << std::setprecision(14) << seeds.audio_fingerprint << ",\n";
    ss << "    fontsSeed: __vmFontsSeed,\n";
    ss << "    canvas: {\n";
    ss << "        geometry: '" << seeds.canvas_geometry_hash << "',\n";
    ss << "        text: '" << seeds.canvas_text_hash << "'\n";
    ss << "    },\n";
    ss << "    webgl: {\n";
    ss << "        parameters: '" << seeds.webgl_params_hash << "',\n";
    ss << "        extensions: '" << seeds.webgl_extensions_hash << "',\n";
    ss << "        contextAttributes: '" << seeds.webgl_context_hash << "',\n";
    ss << "        extensionParameters: '" << seeds.webgl_ext_params_hash << "',\n";
    ss << "        shaderPrecisions: '" << seeds.shader_precisions_hash << "'\n";
    ss << "    }\n";
    ss << "};\n\n";

    // Add all component scripts
    ss << GenerateNavigatorScript(vm);
    ss << GenerateScreenScript(vm);
    ss << GenerateWebGLScript(vm, overrides);  // Pass overrides for WebGL renderer/vendor from profile
    ss << GenerateAudioScript(vm);
    ss << GenerateFontsScript(vm);  // CRITICAL: OS-aware font fingerprint spoofing
    // Timezone is NOT injected here - it's handled dynamically by OwlStealth
    // based on GeoIP detection (public IP) with proxy override support
    // See: OwlStealth::SpoofTimezone which is called after InjectHardwareSimulation
    ss << GenerateCanvasScript(vm);
    // Canvas script now applies deterministic noise for fingerprint protection

    // CRITICAL: Fix performance.now() precision to match real Chrome
    // CEF in headless mode may return integer milliseconds instead of sub-millisecond precision
    // Real Chrome returns values like 1234.567890123 (microsecond precision)
    ss << R"(
// Fix performance.now() precision - real Chrome returns sub-millisecond values
try {
    const _origPerfNow = Performance.prototype.now;
    // Use crypto for high-quality random decimals (if available), fallback to Math.random
    const getRandomDecimal = () => {
        if (typeof crypto !== 'undefined' && crypto.getRandomValues) {
            const arr = new Uint32Array(1);
            crypto.getRandomValues(arr);
            return (arr[0] / 0xFFFFFFFF);
        }
        return Math.random();
    };

    // Cache the decimal offset per call to maintain monotonicity
    let lastValue = 0;
    let lastDecimal = 0;

    Performance.prototype.now = __vmMask(function now() {
        let value = _origPerfNow.call(this);

        // If value is integer (or has low precision), add realistic sub-millisecond component
        // Real Chrome precision is approximately 5 microseconds (0.005ms)
        const intPart = Math.floor(value);
        const fracPart = value - intPart;

        // If fractional part is 0 or very small (< 0.001), add realistic precision
        if (fracPart < 0.001) {
            // Generate sub-millisecond precision (0.000000 to 0.999999)
            // Use a deterministic approach based on time to ensure monotonicity
            let decimal;
            if (intPart === lastValue) {
                // Same millisecond - ensure we don't go backwards
                decimal = lastDecimal + 0.001 + (getRandomDecimal() * 0.001);
                if (decimal >= 1) decimal = 0.999;
            } else {
                // New millisecond - generate fresh random decimal
                decimal = getRandomDecimal() * 0.5 + fracPart;  // Keep in first half to avoid overflow
            }
            lastValue = intPart;
            lastDecimal = decimal;
            value = intPart + decimal;
        }

        return value;
    }, 'now', _origPerfNow);
} catch (e) {}
)";

    // Architecture spoofing for non-macOS profiles (Windows/Linux)
    // fingerprint.com detects architecture via Float32Array NaN byte pattern:
    //   const f = new Float32Array(1); const u8 = new Uint8Array(f.buffer);
    //   f[0] = Infinity; f[0] = f[0] - f[0]; return u8[3];
    // Returns 127 on ARM, 255 on x86_64
    // Windows/Linux x86_64 should always return 255, but ARM Mac returns 127
    // Apply for any non-macOS profile to match worker behavior
    if (vm.os.name != "macOS" && vm.os.platform.find("Mac") == std::string::npos) {
        ss << R"(
// Architecture spoofing - Windows/Linux x86_64 must show 255, not 127 (ARM)
// SIMPLER APPROACH: Patch Uint8Array constructor to fix NaN byte pattern directly
try {
    const __origUint8Array = Uint8Array;
    const __origFloat32Array = Float32Array;

    // Wrap Uint8Array to detect architecture fingerprinting pattern
    window.Uint8Array = function Uint8Array(...args) {
        const instance = new __origUint8Array(...args);

        // Check if this is a view over an ArrayBuffer (not array/length)
        // Architecture detection: new Uint8Array(float32Array.buffer)
        if (args.length === 1 && args[0] instanceof ArrayBuffer && args[0].byteLength === 4) {
            // Check if buffer contains ARM NaN pattern: [0, 0, 192, 127]
            // ARM NaN: 0x7FC00000, x86 NaN: 0xFFC00000
            // The difference is byte 3: 127 (ARM) vs 255 (x86)
            if (instance[0] === 0 && instance[1] === 0 && instance[2] === 192 && instance[3] === 127) {
                // This is an ARM NaN - modify byte 3 to look like x86 NaN
                instance[3] = 255;
            }
        }
        return instance;
    };
    Object.setPrototypeOf(window.Uint8Array, __origUint8Array);
    window.Uint8Array.prototype = __origUint8Array.prototype;
    window.Uint8Array.BYTES_PER_ELEMENT = __origUint8Array.BYTES_PER_ELEMENT;
    Object.defineProperty(window.Uint8Array, 'name', { value: 'Uint8Array', configurable: true });
} catch (e) {}
)";
    }

    // Add network information API spoofing
    ss << GenerateNetworkScript(vm);

    // CRITICAL: Add iframe interception LAST so all patches are defined
    // This intercepts iframe creation and patches their contexts before fingerprint.com can use them
    ss << GenerateIframeInterceptionScript(vm);

    ss << "\n})();\n";
    return ss.str();
}

std::string VirtualMachineInjector::GenerateNavigatorScript(const VirtualMachine& vm) {
    // STUB: Navigator spoofing moved to src/stealth/spoofs/navigator_spoof.cc
    return "";
}
std::string VirtualMachineInjector::GenerateScreenScript(const VirtualMachine& vm) {
    // STUB: Screen spoofing moved to src/stealth/spoofs/screen_spoof.cc
    return "";
}
std::string VirtualMachineInjector::GenerateWebGLScript(const VirtualMachine& vm, const VMOverrides& overrides) {
    // STUB: WebGL spoofing moved to src/stealth/spoofs/webgl_spoof.cc
    return "";
}
std::string VirtualMachineInjector::GenerateAudioScript(const VirtualMachine& vm) {
    // STUB: Audio spoofing moved to src/stealth/spoofs/audio_spoof.cc
    return "";
}
std::string VirtualMachineInjector::GenerateTimezoneScript(const VirtualMachine& vm) {
    // UNUSED - Timezone spoofing is done by OwlStealth::SpoofTimezone() which has
    // dynamic GeoIP support. This function is kept for API compatibility.
    // See: src/stealth/owl_stealth.cc::SpoofTimezone
    return "";
}

std::string VirtualMachineInjector::GenerateCanvasScript(const VirtualMachine& vm) {
    // STUB: Canvas spoofing moved to src/stealth/spoofs/canvas_spoof.cc
    return "";
}

std::string VirtualMachineInjector::GenerateFontsScript(const VirtualMachine& vm) {
    // Delegate to FontSpoofer - single source of truth for font fingerprint spoofing
    return FontSpoofer::GenerateScript(vm);
}

// Note: All legacy font code has been removed and consolidated into FontSpoofer class
// See: src/stealth/owl_font_spoofer.cc

// REMOVED LEGACY CODE - DO NOT RESTORE
// The old inline font spoofing code was removed because:
// 1. It was duplicated between owl_app.cc and owl_virtual_machine.cc
// 2. It didn't properly handle fontPreferences (apple != sans issue)
// 3. It blocked fonts too aggressively causing empty fonts array
// 4. It didn't account for DPR differences in measurements

#if 0  // DELETED - This marker is just for git diff clarity
// ~500 lines of legacy font code removed
// All font spoofing is now in FontSpoofer::GenerateScript()
#endif

// Legacy font code completely removed - see owl_font_spoofer.cc
// Approximately 500 lines of duplicated font spoofing code was here
// Now consolidated in FontSpoofer class for maintainability

std::string VirtualMachineInjector::GenerateMediaScript(const VirtualMachine& vm) {
    return "// Media: TODO - implement media capabilities spoofing\n";
}

// LEGACY FONT CODE REMOVED - approximately 400 lines deleted
// All font spoofing consolidated in FontSpoofer class
// Keeping this comment as a marker for git history

std::string VirtualMachineInjector::GeneratePermissionsScript(const VirtualMachine& vm) {
    return "// Permissions: TODO - implement permissions API spoofing\n";
}

// All legacy font code removed - consolidated in FontSpoofer class

std::string VirtualMachineInjector::GenerateStorageScript(const VirtualMachine& vm) {
    return "// Storage: TODO - implement storage API spoofing\n";
}

std::string VirtualMachineInjector::GenerateBatteryScript(const VirtualMachine& vm) {
    if (!vm.battery.enabled) {
        return "// Battery API disabled for this profile\n";
    }
    return "// Battery: TODO - implement battery API spoofing\n";
}

std::string VirtualMachineInjector::GenerateNetworkScript(const VirtualMachine& vm) {
    std::stringstream ss;
    ss << "\n// Network Information API spoofing\n";

    // Get network values from VM profile, with sensible defaults
    float downlink = vm.network.downlink > 0 ? vm.network.downlink : 10.0f;
    int rtt = vm.network.rtt > 0 ? vm.network.rtt : 50;
    std::string effective_type = vm.network.effective_type.empty() ? "4g" : vm.network.effective_type;

    ss << R"(
// CRITICAL: Network downlink should be dynamic, not fixed round values
// Real Chrome reports actual network speed with decimals like 1.7, 5.4, etc.
// Fixed values like 10 are easily detectable as fake
try {
    if (typeof navigator !== 'undefined' && navigator.connection) {
        const __vmNetwork = {
)";
    ss << "            downlink: " << std::fixed << std::setprecision(2) << downlink << " + (Math.random() * 0.5 - 0.25),\n";  // Add ±0.25 Mbps jitter
    ss << "            rtt: " << rtt << ",\n";
    ss << "            effectiveType: '" << effective_type << "',\n";
    ss << "            saveData: false,\n";
    ss << "            type: 'wifi'\n";
    ss << R"(        };

        const connProto = Object.getPrototypeOf(navigator.connection);

        // Override with realistic, slightly varying values
        Object.defineProperty(connProto, 'downlink', {
            get: __vmMask(function() {
                // Return slightly different value each time to simulate real network variance
                return __vmNetwork.downlink + (Math.random() * 0.1 - 0.05);
            }, 'get downlink'),
            configurable: true,
            enumerable: true
        });

        Object.defineProperty(connProto, 'rtt', {
            get: __vmMask(function() {
                return __vmNetwork.rtt + Math.floor(Math.random() * 10);  // ±10ms jitter
            }, 'get rtt'),
            configurable: true,
            enumerable: true
        });

        Object.defineProperty(connProto, 'effectiveType', {
            get: __vmMask(function() {
                return __vmNetwork.effectiveType;
            }, 'get effectiveType'),
            configurable: true,
            enumerable: true
        });

        Object.defineProperty(connProto, 'saveData', {
            get: __vmMask(function() {
                return __vmNetwork.saveData;
            }, 'get saveData'),
            configurable: true,
            enumerable: true
        });

        Object.defineProperty(connProto, 'type', {
            get: __vmMask(function() {
                return __vmNetwork.type;
            }, 'get type'),
            configurable: true,
            enumerable: true
        });
    }
} catch (e) {}

)";

    return ss.str();
}

std::string VirtualMachineInjector::GenerateIframeInterceptionScript(const VirtualMachine& vm) {
    // STUB: Iframe interception moved to modular spoof system
    // The iframe patching is now handled by:
    // 1. SpoofUtils::InjectUtilities() - Core utilities shared across contexts
    // 2. Individual spoof modules apply their hooks to prototype level
    // 3. Cross-origin iframes get patched via OnContextCreated in owl_app.cc
    // Worker patching is handled by ServiceWorkerResponseFilter in owl_client.cc
    return "";
}

std::string VirtualMachineInjector::GetUserAgent(const VirtualMachine& vm) {
    return vm.browser.user_agent;
}

std::map<std::string, std::string> VirtualMachineInjector::GetClientHintHeaders(const VirtualMachine& vm) {
    std::map<std::string, std::string> headers;
    if (!vm.client_hints.enabled) {
        return headers;  // Empty - don't send client hints
    }
    // Add client hint headers if enabled
    return headers;
}

} // namespace owl
