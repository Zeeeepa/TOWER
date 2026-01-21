#include "owl_app.h"
#include "owl_automation_handler.h"
#include "owl_stealth.h"
#include "owl_render_tracker.h"
#include "owl_element_scanner.h"
#include "owl_semantic_matcher.h"
#include "owl_test_scheme_handler.h"
#include "owl_https_server.h"
#include "owl_platform_utils.h"
#include "stealth/owl_virtual_machine.h"
#include "stealth/owl_fingerprint_generator.h"
#include "stealth/owl_spoof_manager.h"
#include "gpu/owl_gl_hooks.h"
#include "gpu/owl_gpu_api.h"
#include "logger.h"
#include "include/wrapper/cef_helpers.h"
#include "include/cef_scheme.h"
#include <algorithm>
#include <random>
#include <vector>
#include <cstdlib>
#include <mutex>
#include <unordered_map>

// =============================================================================
// CRITICAL FIX: WorkerNavigator C++ V8 Interceptor
// =============================================================================
// This interceptor patches navigator properties at the V8 engine level,
// BEFORE any JavaScript runs. This fixes the timing bug where CreepJS
// destructures navigator at module level (during parse phase).
//
// The problem: OnContextCreated fires after V8 context is created, but the
// worker script has already started parsing. Module-level code like:
//   const { hardwareConcurrency } = navigator
// executes DURING parsing, capturing real values before context->Eval() runs.
//
// The solution: Use CefV8Interceptor on the navigator object itself to
// intercept property access at the V8 level, returning spoofed values
// regardless of when the access occurs.
// =============================================================================

class WorkerNavigatorInterceptor : public CefV8Interceptor {
public:
  WorkerNavigatorInterceptor(const owl::VirtualMachine& vm) {
    // Cache VM values for fast access
    platform_ = vm.os.platform;
    user_agent_ = vm.browser.user_agent;
    app_version_ = vm.browser.user_agent.length() > 8 ?
                   vm.browser.user_agent.substr(8) : vm.browser.user_agent;
    vendor_ = vm.browser.vendor;
    hardware_concurrency_ = vm.cpu.hardware_concurrency;
    device_memory_ = vm.cpu.device_memory;
    language_ = vm.language.primary;
    languages_ = vm.language.languages;
    os_name_ = vm.os.name;
    os_version_ = vm.os.version;
    browser_version_ = vm.browser.version;

    LOG_INFO("WorkerNavigatorInterceptor", "Created with platform=" + platform_ +
             " hardwareConcurrency=" + std::to_string(hardware_concurrency_));
  }

  // Intercept property access by name
  bool Get(const CefString& name,
           const CefRefPtr<CefV8Value> object,
           CefRefPtr<CefV8Value>& retval,
           CefString& exception) override {

    std::string prop = name.ToString();

    // Only intercept known navigator properties
    if (prop == "platform") {
      retval = CefV8Value::CreateString(platform_);
      return true;
    }
    if (prop == "userAgent") {
      retval = CefV8Value::CreateString(user_agent_);
      return true;
    }
    if (prop == "appVersion") {
      retval = CefV8Value::CreateString(app_version_);
      return true;
    }
    if (prop == "vendor") {
      retval = CefV8Value::CreateString(vendor_);
      return true;
    }
    if (prop == "hardwareConcurrency") {
      retval = CefV8Value::CreateInt(hardware_concurrency_);
      return true;
    }
    if (prop == "deviceMemory") {
      retval = CefV8Value::CreateDouble(static_cast<double>(device_memory_));
      return true;
    }
    if (prop == "language") {
      retval = CefV8Value::CreateString(language_);
      return true;
    }
    if (prop == "languages") {
      CefRefPtr<CefV8Value> arr = CefV8Value::CreateArray(static_cast<int>(languages_.size()));
      for (size_t i = 0; i < languages_.size(); ++i) {
        arr->SetValue(static_cast<int>(i), CefV8Value::CreateString(languages_[i]));
      }
      retval = arr;
      return true;
    }
    if (prop == "webdriver") {
      retval = CefV8Value::CreateBool(false);
      return true;
    }

    // Don't intercept - let V8 handle it normally
    return false;
  }

  // Intercept property access by index (not used for navigator)
  bool Get(int index,
           const CefRefPtr<CefV8Value> object,
           CefRefPtr<CefV8Value>& retval,
           CefString& exception) override {
    return false;
  }

  // Intercept property assignment by name
  bool Set(const CefString& name,
           const CefRefPtr<CefV8Value> object,
           const CefRefPtr<CefV8Value> value,
           CefString& exception) override {
    // Block writes to spoofed properties
    std::string prop = name.ToString();
    if (prop == "platform" || prop == "userAgent" || prop == "appVersion" ||
        prop == "vendor" || prop == "hardwareConcurrency" || prop == "deviceMemory" ||
        prop == "language" || prop == "languages" || prop == "webdriver") {
      // Silently ignore writes (or could throw an exception)
      return true;
    }
    return false;
  }

  // Intercept property assignment by index (not used for navigator)
  bool Set(int index,
           const CefRefPtr<CefV8Value> object,
           const CefRefPtr<CefV8Value> value,
           CefString& exception) override {
    return false;
  }

private:
  std::string platform_;
  std::string user_agent_;
  std::string app_version_;
  std::string vendor_;
  int hardware_concurrency_;
  int device_memory_;
  std::string language_;
  std::vector<std::string> languages_;
  std::string os_name_;
  std::string os_version_;
  std::string browser_version_;

  IMPLEMENT_REFCOUNTING(WorkerNavigatorInterceptor);
};

// Global storage for script evaluation results
namespace {
  std::mutex g_eval_mutex;
  std::unordered_map<std::string, std::string> g_eval_results;  // context_id -> result
  std::unordered_map<std::string, bool> g_eval_ready;           // context_id -> ready flag
}

// Public functions to access eval results
void SetEvalResult(const std::string& context_id, const std::string& result) {
  std::lock_guard<std::mutex> lock(g_eval_mutex);
  g_eval_results[context_id] = result;
  g_eval_ready[context_id] = true;
}

bool GetEvalResult(const std::string& context_id, std::string& result) {
  std::lock_guard<std::mutex> lock(g_eval_mutex);
  auto it = g_eval_ready.find(context_id);
  if (it != g_eval_ready.end() && it->second) {
    result = g_eval_results[context_id];
    return true;
  }
  return false;
}

void ClearEvalResult(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(g_eval_mutex);
  g_eval_ready[context_id] = false;
  g_eval_results.erase(context_id);
}

// Use consistent User-Agent across all contexts to avoid mismatch detection
// CRITICAL: Must match the hardcoded UA in owl_main.cc and owl_subprocess.cc
// User-Agent is now loaded dynamically from VirtualMachineDB config
std::vector<std::string> OwlApp::user_agents_ = {};  // Populated dynamically

// Session-wide seeds - initialized in OnBeforeCommandLineProcessing (browser process)
int OwlApp::session_gpu_profile_ = -1;
uint64_t OwlApp::session_vm_seed_ = 0;

// CRITICAL: Per-browser vm_id storage in renderer process
// This is the SINGLE SOURCE OF TRUTH for vm_id in the renderer
// Set by OnBrowserCreated (from browser process via extra_info)
// Used by OnContextCreated and all other renderer-side code
std::map<int, std::string> OwlApp::browser_vm_ids_;
std::mutex OwlApp::browser_vm_ids_mutex_;

// Default VM ID for worker contexts that don't have a browser reference.
// CEF's CefBrowser is nullptr for ServiceWorker, SharedWorker, DedicatedWorker contexts.
// This fallback is set when any browser is created and used when browser is null.
std::string OwlApp::default_worker_vm_id_;

OwlApp::OwlApp() {}

// Get vm_id for a browser - used by OnContextCreated and owl_stealth.cc
std::string OwlApp::GetBrowserVMId(int browser_id) {
  std::lock_guard<std::mutex> lock(browser_vm_ids_mutex_);
  auto it = browser_vm_ids_.find(browser_id);
  if (it != browser_vm_ids_.end()) {
    return it->second;
  }
  return "";  // Not found - caller must handle this case
}

// Called after browser is created in renderer process
// CRITICAL: This receives vm_id from browser process via extra_info
// OnBrowserCreated is called BEFORE OnContextCreated
void OwlApp::OnBrowserCreated(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefDictionaryValue> extra_info) {
  int browser_id = browser->GetIdentifier();

  if (extra_info && extra_info->HasKey("vm_id")) {
    std::string vm_id = extra_info->GetString("vm_id").ToString();
    if (!vm_id.empty()) {
      std::lock_guard<std::mutex> lock(browser_vm_ids_mutex_);
      browser_vm_ids_[browser_id] = vm_id;
      // Also set as default for worker contexts (ServiceWorker, SharedWorker, etc.)
      // Workers don't have browser reference (CEF returns nullptr), so they use this fallback
      default_worker_vm_id_ = vm_id;
      LOG_INFO("OwlApp", "[VM_SYNC] OnBrowserCreated: Stored vm_id=" + vm_id +
               " for browser_id=" + std::to_string(browser_id) +
               " (also set as default_worker_vm_id)");
    }
  } else {
    LOG_WARN("OwlApp", "[VM_SYNC] OnBrowserCreated: No vm_id in extra_info for browser_id=" +
             std::to_string(browser_id));
  }
}

std::string OwlApp::GetRandomUserAgent() {
  // Use browser version from VirtualMachineDB config
  return owl::VirtualMachineDB::Instance().GetDefaultUserAgent();
}

void OwlApp::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
  // Register custom "owl" scheme with all necessary flags
  // NOTE: Do NOT use LOCAL - it causes origin to be "null" which breaks ServiceWorkers
  // SECURE enables HTTPS-like security, allowing ServiceWorker registration
  registrar->AddCustomScheme(
    "owl",
    CEF_SCHEME_OPTION_STANDARD |
    CEF_SCHEME_OPTION_SECURE |
    CEF_SCHEME_OPTION_CORS_ENABLED |
    CEF_SCHEME_OPTION_FETCH_ENABLED
  );
  LOG_DEBUG("OwlApp", "Registered owl:// scheme with STANDARD | SECURE | CORS_ENABLED | FETCH_ENABLED");

  // Register "owl-nla://" as a custom scheme for triggering NLA commands
  registrar->AddCustomScheme(
    "owl-nla",
    CEF_SCHEME_OPTION_STANDARD |
    CEF_SCHEME_OPTION_CORS_ENABLED
  );
  LOG_DEBUG("OwlApp", "Registered owl-nla:// scheme for NLA command execution");
}

void OwlApp::OnBeforeCommandLineProcessing(
    const CefString& process_type,
    CefRefPtr<CefCommandLine> command_line) {

  LOG_DEBUG("OwlApp", "========================================");
  LOG_DEBUG("OwlApp", "OnBeforeCommandLineProcessing called");
  std::string proc_type = process_type.ToString();
  LOG_DEBUG("OwlApp", "Process type: '" + (proc_type.empty() ? "BROWSER" : proc_type) + "'");

  // CRITICAL: Generate session-wide seeds in BROWSER process
  // These will be passed to renderer via OnBeforeChildProcessLaunch
  // Also add to browser's own command line so it can read them consistently
  if (proc_type.empty()) {  // Browser process only
    if (session_gpu_profile_ == -1) {
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<> dis(0, 2);
      session_gpu_profile_ = dis(gen);
      LOG_DEBUG("OwlApp", "Generated session-wide GPU profile index: " + std::to_string(session_gpu_profile_));
    }
    // Add to browser's command line too for consistency
    command_line->AppendSwitchWithValue("owl-gpu-profile", std::to_string(session_gpu_profile_));

    // Generate session-wide VM seed for per-context VM selection
    // This ensures each context gets a DIFFERENT VM, and both browser and renderer
    // can compute the same VM by using: session_vm_seed XOR (browser_id * golden_ratio)
    if (session_vm_seed_ == 0) {
      std::random_device rd;
      std::mt19937_64 gen64(rd());
      session_vm_seed_ = gen64();
      LOG_DEBUG("OwlApp", "Generated session-wide VM seed: " + std::to_string(session_vm_seed_));
    }
    // Add to browser's command line too for consistency
    command_line->AppendSwitchWithValue("owl-vm-seed", std::to_string(session_vm_seed_));

    // CRITICAL: Set OWL_GPU_* environment variables from VM profile
    // These are read by the ANGLE wrapper (libGLESv2.dylib) to spoof GPU identity
    // Must be set BEFORE any GL context is created
    // IMPORTANT: Use same seed calculation as browser_manager for browser_id=1
    // so the first context gets the same VM as the session VM
    uint64_t first_context_seed = session_vm_seed_ ^ (1ULL * 0x9E3779B97F4A7C15ULL);
    const owl::VirtualMachine* session_vm = owl::VirtualMachineDB::Instance().SelectRandomVM(
        "",  // Any OS
        "",  // Any browser
        "",  // Any GPU
        first_context_seed  // Use same seed as first browser context will use
    );
    if (session_vm) {
      LOG_DEBUG("OwlApp", "Selected session VM: " + session_vm->id);
      LOG_DEBUG("OwlApp", "  GPU (masked): " + session_vm->gpu.vendor + " / " + session_vm->gpu.renderer);

      // Set environment variables for ANGLE wrapper
      // IMPORTANT: Use MASKED values (gpu.vendor, gpu.renderer) NOT unmasked!
      // glGetString(GL_VENDOR/GL_RENDERER) should return masked browser values,
      // not raw GPU names. Unmasked values are only for WEBGL_debug_renderer_info.
      setenv("OWL_GPU_VENDOR", session_vm->gpu.vendor.c_str(), 1);
      setenv("OWL_GPU_RENDERER", session_vm->gpu.renderer.c_str(), 1);
      setenv("OWL_GPU_VERSION", session_vm->gpu.webgl_version.c_str(), 1);
      setenv("OWL_GPU_GLSL_VERSION", session_vm->gpu.shading_language.c_str(), 1);
      setenv("OWL_GPU_PLATFORM", session_vm->os.platform.c_str(), 1);  // For ANGLE backend selection (Linux=OpenGL, Windows=D3D11)
      setenv("OWL_GPU_SPOOF_ENABLED", "1", 1);  // Enable spoofing

      // Set shader precision env vars for cross-process GPU spoofing
      // Format: "range_min,range_max,precision" (e.g., "127,127,23")
      auto fmt_precision = [](const auto& pf) {
        return std::to_string(pf.range_min) + "," +
               std::to_string(pf.range_max) + "," +
               std::to_string(pf.precision);
      };
      setenv("OWL_GPU_VERTEX_HIGH_FLOAT", fmt_precision(session_vm->gpu.vertex_high_float).c_str(), 1);
      setenv("OWL_GPU_VERTEX_MEDIUM_FLOAT", fmt_precision(session_vm->gpu.vertex_medium_float).c_str(), 1);
      setenv("OWL_GPU_VERTEX_LOW_FLOAT", fmt_precision(session_vm->gpu.vertex_low_float).c_str(), 1);
      setenv("OWL_GPU_FRAGMENT_HIGH_FLOAT", fmt_precision(session_vm->gpu.fragment_high_float).c_str(), 1);
      setenv("OWL_GPU_FRAGMENT_MEDIUM_FLOAT", fmt_precision(session_vm->gpu.fragment_medium_float).c_str(), 1);
      setenv("OWL_GPU_FRAGMENT_LOW_FLOAT", fmt_precision(session_vm->gpu.fragment_low_float).c_str(), 1);
      setenv("OWL_GPU_VERTEX_HIGH_INT", fmt_precision(session_vm->gpu.vertex_high_int).c_str(), 1);
      setenv("OWL_GPU_VERTEX_MEDIUM_INT", fmt_precision(session_vm->gpu.vertex_medium_int).c_str(), 1);
      setenv("OWL_GPU_VERTEX_LOW_INT", fmt_precision(session_vm->gpu.vertex_low_int).c_str(), 1);
      setenv("OWL_GPU_FRAGMENT_HIGH_INT", fmt_precision(session_vm->gpu.fragment_high_int).c_str(), 1);
      setenv("OWL_GPU_FRAGMENT_MEDIUM_INT", fmt_precision(session_vm->gpu.fragment_medium_int).c_str(), 1);
      setenv("OWL_GPU_FRAGMENT_LOW_INT", fmt_precision(session_vm->gpu.fragment_low_int).c_str(), 1);

      // Set WebGL integer parameters for cross-process GPU spoofing
      setenv("OWL_GPU_MAX_TEXTURE_SIZE", std::to_string(session_vm->gpu.max_texture_size).c_str(), 1);
      setenv("OWL_GPU_MAX_CUBE_MAP_TEXTURE_SIZE", std::to_string(session_vm->gpu.max_cube_map_texture_size).c_str(), 1);
      setenv("OWL_GPU_MAX_RENDER_BUFFER_SIZE", std::to_string(session_vm->gpu.max_render_buffer_size).c_str(), 1);
      setenv("OWL_GPU_MAX_VERTEX_ATTRIBS", std::to_string(session_vm->gpu.max_vertex_attribs).c_str(), 1);
      setenv("OWL_GPU_MAX_VERTEX_UNIFORM_VECTORS", std::to_string(session_vm->gpu.max_vertex_uniform_vectors).c_str(), 1);
      setenv("OWL_GPU_MAX_VERTEX_TEXTURE_UNITS", std::to_string(session_vm->gpu.max_vertex_texture_units).c_str(), 1);
      setenv("OWL_GPU_MAX_VARYING_VECTORS", std::to_string(session_vm->gpu.max_varying_vectors).c_str(), 1);
      setenv("OWL_GPU_MAX_FRAGMENT_UNIFORM_VECTORS", std::to_string(session_vm->gpu.max_fragment_uniform_vectors).c_str(), 1);
      setenv("OWL_GPU_MAX_TEXTURE_UNITS", std::to_string(session_vm->gpu.max_texture_units).c_str(), 1);
      setenv("OWL_GPU_MAX_COMBINED_TEXTURE_UNITS", std::to_string(session_vm->gpu.max_combined_texture_units).c_str(), 1);
      setenv("OWL_GPU_MAX_SAMPLES", std::to_string(session_vm->gpu.max_samples).c_str(), 1);
      // Multisampling parameters (critical for VM detection!)
      // Default to 4/1 if not set in profile (typical for real Chrome on desktop)
      setenv("OWL_GPU_SAMPLES", std::to_string(session_vm->gpu.samples > 0 ? session_vm->gpu.samples : 4).c_str(), 1);
      setenv("OWL_GPU_SAMPLE_BUFFERS", std::to_string(session_vm->gpu.sample_buffers > 0 ? session_vm->gpu.sample_buffers : 1).c_str(), 1);

      // Also pre-register as context ID 1 (first browser will get this ID)
      // This ensures ANGLE wrapper can use per-context API for early GL calls
      owl_gpu_register_context(1,
          session_vm->gpu.vendor.c_str(),
          session_vm->gpu.renderer.c_str(),
          session_vm->gpu.webgl_version.c_str(),
          session_vm->gpu.shading_language.c_str());
      owl_gpu_set_current_context(1);

      LOG_DEBUG("OwlApp", "Set OWL_GPU_* environment variables for ANGLE wrapper (masked values)");
      LOG_DEBUG("OwlApp", "Pre-registered GPU context 1 for ANGLE wrapper");
    } else {
      LOG_WARN("OwlApp", "No VM profile available - ANGLE wrapper will use defaults");
    }

    // CRITICAL: Install GL hooks for GPU virtualization
    // This must be done early, before any GL calls are made
    // The hooks intercept glGetString, glReadPixels, etc. to spoof GPU identity
#ifdef __APPLE__
    if (owl::gpu::hooks::InstallGLHooks()) {
      LOG_DEBUG("OwlApp", "GPU virtualization: GL hooks installed successfully");
    } else {
      LOG_WARN("OwlApp", "GPU virtualization: Failed to install GL hooks");
    }
#endif
  }

  // CRITICAL: Apply stealth flags to ALL processes (browser AND renderer)
  // The renderer process is where navigator.webdriver gets set!

  // CRITICAL: Disable macOS Keychain password prompt
  command_line->AppendSwitch("use-mock-keychain");
  command_line->AppendSwitchWithValue("password-store", "basic");

  // ============================================================
  // CRITICAL STEALTH FLAGS - Bot Detection Prevention
  // MUST be applied to RENDERER process (where JS runs)
  // ============================================================

  // 1. Disable Blink automation flag (prevents navigator.webdriver at source)
  LOG_DEBUG("OwlApp", "Adding flag: disable-blink-features=AutomationControlled");
  command_line->AppendSwitchWithValue("disable-blink-features", "AutomationControlled");

  // 2. Disable automation-related features
  LOG_DEBUG("OwlApp", "Adding flag: disable-automation");
  command_line->AppendSwitch("disable-automation");

  // 3. Remove CDP (Chrome DevTools Protocol) detection
  // This prevents window.cdc_* variables from being created
  command_line->AppendSwitch("disable-dev-shm-usage");

  // 4. Prevent notification prompts (automation detection)
  command_line->AppendSwitch("disable-notifications");

  // 5. Disable infobars (another automation indicator)
  command_line->AppendSwitch("disable-infobars");

  // 6. Enable features that real browsers have
  // MEMORY OPTIMIZATION: Disable NetworkServiceInProcess to reduce per-context memory
  // NetworkServiceInProcess embeds network stack in each renderer process (~60-100 MB overhead)
  // With this disabled, network service runs in a single shared process
  command_line->AppendSwitch("enable-features=NetworkService");
  command_line->AppendSwitch("disable-features=NetworkServiceInProcess");

  // 7. Disable User-Agent Client Hints (prevents OS leak via Sec-CH-UA-Platform header)
  // CRITICAL: Client Hints send "macOS" even when User-Agent says "Windows"!
  command_line->AppendSwitch("disable-features=IsolateOrigins,site-per-process,UserAgentClientHint");

  // Performance optimizations
  // NOTE: DO NOT disable GPU - it breaks WebGL fingerprinting!
  // command_line->AppendSwitch("disable-gpu");  // DISABLED - breaks WebGL
  // command_line->AppendSwitch("disable-software-rasterizer");  // DISABLED

  // GPU VIRTUALIZATION: Use native GPU with JavaScript spoofing
  // We rely on JS injection to spoof GPU identity (vendor/renderer strings)
  // The actual GPU (Apple MPS, NVIDIA, AMD, Intel) is used for rendering
  // This ensures WebGL works properly while identity is spoofed via JS
  LOG_DEBUG("OwlApp", "GPU Virtualization: Using native GPU with JS spoofing");

  // CRITICAL: Ignore GPU blocklist to prevent WebGL being disabled for spoofed GPUs
  // Chromium checks GPU vendor/renderer against a blocklist and may disable WebGL
  // for certain GPU/driver combinations. Since we spoof the GPU identity, we must
  // ignore this blocklist to ensure WebGL1 and WebGL2 work properly.
  // These flags apply to ALL platforms (macOS, Windows, Linux)
  command_line->AppendSwitch("ignore-gpu-blocklist");
  command_line->AppendSwitch("ignore-gpu-blacklist");  // Legacy name for older Chromium versions
  command_line->AppendSwitch("disable-gpu-driver-bug-workarounds");  // Disable driver-specific workarounds

  // Force enable WebGL and WebGL2 (all platforms)
  command_line->AppendSwitch("enable-webgl");
  command_line->AppendSwitch("enable-webgl2-compute-context");

  // Disable GPU process crash limit (prevents GPU process from being killed)
  command_line->AppendSwitch("disable-gpu-process-crash-limit");

#ifdef OS_LINUX
  // Linux-specific: Enable headless rendering without display server
  // Use SwiftShader (software GL) to avoid ANGLE/EGL crashes in containers
  command_line->AppendSwitch("disable-gpu");
  command_line->AppendSwitch("disable-gpu-compositing");
  command_line->AppendSwitchWithValue("use-gl", "swiftshader");
  command_line->AppendSwitchWithValue("use-angle", "swiftshader");
  command_line->AppendSwitch("ozone-platform=headless");
  command_line->AppendSwitch("in-process-gpu");
  // SwiftShader supports both WebGL1 and WebGL2 in software rendering
  LOG_DEBUG("OwlApp", "Linux headless mode enabled with SwiftShader (WebGL1+WebGL2 via software)");
#else
  // macOS/Windows: Let CEF use default GPU settings
  // CEF should automatically use ANGLE with Metal on macOS
  // Only set minimal flags to avoid conflicts
  command_line->AppendSwitch("in-process-gpu");  // Single GPU process

  LOG_DEBUG("OwlApp", "macOS/Windows headless: Using default CEF GPU settings");
#endif

  // WebGL context settings (all platforms)
  // NOTE: Don't set webgl-antialiasing-mode=none as it causes SAMPLES=0, SAMPLE_BUFFERS=0
  // which triggers VM detection on fingerprinting sites. Let Chrome use default multisampling.

  // Resource optimizations
  command_line->AppendSwitch("disable-extensions");
  command_line->AppendSwitch("disable-plugins");
  command_line->AppendSwitch("disable-default-apps");
  command_line->AppendSwitch("disable-sync");
  command_line->AppendSwitch("no-first-run");
  command_line->AppendSwitch("no-default-browser-check");

  // ============================================================
  // MEMORY OPTIMIZATIONS - Reduce per-context memory usage
  // Target: <150 MB per context for 100+ concurrent contexts
  // ============================================================

  // V8 heap size limits - constrain JavaScript memory per renderer
  // Reduced from 128MB to 96MB for tighter memory control
  // --optimize-for-size enables aggressive code compaction
  command_line->AppendSwitchWithValue("js-flags",
    "--max-old-space-size=96 --max-semi-space-size=2 --optimize-for-size --lite-mode");

  // GPU process pooling - share GPU process across all contexts
  // Without this, each context may spawn its own GPU process
  command_line->AppendSwitch("in-process-gpu");  // Single GPU process for all

  // Reduce renderer process memory footprint
  command_line->AppendSwitch("renderer-process-limit=4");  // Max 4 renderer processes shared

  // Disable font subpixel rendering to reduce memory
  command_line->AppendSwitch("disable-lcd-text");

  // Limit image decoding memory (reduced from 64MB to 32MB)
  command_line->AppendSwitchWithValue("image-decode-memory-limit", "32");  // 32MB max

  // Enable memory pressure handling for aggressive GC
  command_line->AppendSwitch("enable-features=MemoryPressureBasedSourceBufferGC");

  // ============================================================
  // ADDITIONAL HIGH-PERFORMANCE MEMORY OPTIMIZATIONS
  // ============================================================

  // Limit disk cache size per context (50MB max)
  command_line->AppendSwitchWithValue("disk-cache-size", "52428800");

  // Limit media cache size (32MB max)
  command_line->AppendSwitchWithValue("media-cache-size", "33554432");

  // Disable background video track optimization (reduces memory for video pages)
  command_line->AppendSwitch("disable-background-video-track");

  // Disable partial raster for memory savings
  command_line->AppendSwitch("disable-partial-raster");

  // Reduce tile memory for compositor
  command_line->AppendSwitchWithValue("num-raster-threads", "2");

  // Enable aggressive tab discarding under memory pressure
  command_line->AppendSwitch("enable-aggressive-domstorage-flushing");

  // Disable speculative resource prefetching to save memory
  command_line->AppendSwitch("disable-features=SpeculativeResourcePrefetching");

  // Disable V8 lazy parsing for more predictable memory usage
  command_line->AppendSwitch("disable-lazy-frame-loading");

  // Reduce memory used by back-forward cache
  command_line->AppendSwitch("disable-features=BackForwardCache");

  // ============================================================
  // PRIVACY: Disable ALL Google services and telemetry
  // ============================================================

  // Disable Safe Browsing (contacts Google servers)
  command_line->AppendSwitch("disable-client-side-phishing-detection");
  command_line->AppendSwitch("safebrowsing-disable-auto-update");
  command_line->AppendSwitch("safebrowsing-disable-download-protection");

  // Disable component updates (contacts Google)
  command_line->AppendSwitch("disable-component-update");
  command_line->AppendSwitch("disable-component-extensions-with-background-pages");

  // Disable various Google services
  command_line->AppendSwitch("disable-breakpad");  // Crash reporting
  command_line->AppendSwitch("disable-domain-reliability");  // Domain telemetry

  // Disable network prediction/prefetch (contacts Google)
  command_line->AppendSwitch("disable-preconnect");
  command_line->AppendSwitch("dns-prefetch-disable");

  // ============================================================
  // CRITICAL: Disable DNS-over-HTTPS (Secure DNS) completely
  // DoH causes 10+ second navigation delays when DoH servers are slow
  // ============================================================

  // Method 1: Direct mode flag
  command_line->AppendSwitchWithValue("dns-over-https-mode", "off");

  // Method 2: Force system DNS (no built-in async DNS client)
  command_line->AppendSwitch("disable-async-dns");

  // Method 3: Allow policy-like configuration for managed devices
  command_line->AppendSwitch("allow-config-for-managed-device");

  // Method 4: Block DoH servers via host-resolver-rules
  // This redirects DoH DNS queries to localhost (making them fail fast)
  // Also maps .owl domains to localhost for embedded HTTPS server
  command_line->AppendSwitchWithValue("host-resolver-rules",
    "MAP dns.google 127.0.0.1,"
    "MAP dns.google.com 127.0.0.1,"
    "MAP chrome.cloudflare-dns.com 127.0.0.1,"
    "MAP cloudflare-dns.com 127.0.0.1,"
    "MAP 1.1.1.1 127.0.0.1:1,"          // Block Cloudflare DoH
    "MAP 8.8.8.8 127.0.0.1:1,"          // Block Google DoH
    "MAP doh.opendns.com 127.0.0.1,"
    "MAP dns.quad9.net 127.0.0.1,"
    "MAP *.owl 127.0.0.1"               // .owl domains -> localhost for HTTPS server
  );

  LOG_DEBUG("OwlApp", "DoH servers blocked via host-resolver-rules");

  // Method 5: Combine ALL disabled features into a single flag
  // Multiple separate disable-features flags don't work correctly in Chromium
  command_line->AppendSwitchWithValue("disable-features",
    "DnsOverHttps,"              // Disable DoH completely
    "DnsOverHttpsUpgrade,"       // Disable automatic DoH upgrade
    "AsyncDns,"                  // Disable async DNS
    "OptimizationHints,"         // Google optimization
    "MediaRouter,"               // Google Cast discovery
    "Translate,"                 // Google Translate
    "TranslateUI,"               // Google Translate UI
    "SpellcheckService,"         // Spell checking (contacts Google)
    "UseDnsHttpsSvcb,"           // DNS HTTPS SVCB records
    "DnsHttpsSvc,"               // DNS over HTTPS service
    "SecureDns,"                 // Secure DNS feature
    "Temporal"                   // Disable Temporal API (prevents shutdown crash)
  );

  LOG_DEBUG("OwlApp", "DNS-over-HTTPS disabled with combined disable-features flag");

  // Disable metrics and reporting to Google
  command_line->AppendSwitch("disable-metrics");
  command_line->AppendSwitch("disable-metrics-reporting");
  command_line->AppendSwitch("metrics-recording-only");

  // Disable WebRTC IP leak (privacy)
  command_line->AppendSwitch("enforce-webrtc-ip-permission-check");
  command_line->AppendSwitch("force-webrtc-ip-handling-policy=disable_non_proxied_udp");

  // Performance
  command_line->AppendSwitch("disable-background-networking");
  command_line->AppendSwitch("disable-background-timer-throttling");
  command_line->AppendSwitch("disable-renderer-backgrounding");
  command_line->AppendSwitch("disable-backgrounding-occluded-windows");

  // ============================================================
  // VIRTUAL CAMERA: Enable fake media devices for camera bypass
  // ============================================================

  // Enable media stream support (required for getUserMedia)
  command_line->AppendSwitch("enable-media-stream");

  // Use fake/virtual devices instead of real hardware
  // This enables Chromium's built-in fake device support
  command_line->AppendSwitch("use-fake-device-for-media-stream");

  // Auto-grant media permissions (skip permission prompts)
  command_line->AppendSwitch("use-fake-ui-for-media-stream");

  // Disable gesture requirement for media playback
  command_line->AppendSwitch("disable-gesture-requirement-for-media-playback");

  // Allow autoplay without user gesture (for video elements)
  command_line->AppendSwitch("autoplay-policy=no-user-gesture-required");

  LOG_DEBUG("OwlApp", "Virtual camera flags enabled");

  // ============================================================
  // SERVICEWORKER SUPPORT: Treat .owl domains as secure origins
  // This allows ServiceWorker registration on HTTPS .owl TLD
  // ============================================================
  command_line->AppendSwitchWithValue("unsafely-treat-insecure-origin-as-secure",
    "https://lie-detector.owl:8443,"
    "https://user-form.owl:8443,"
    "https://signin-form.owl:8443,"
    "https://test.owl:8443"
  );
  LOG_DEBUG("OwlApp", ".owl domains added to secure origins for ServiceWorker support");

  // ============================================================
  // EMBEDDED HTTPS SERVER: Accept self-signed certificates
  // Our HTTPS server uses a self-signed certificate for .owl domains
  // ============================================================
  command_line->AppendSwitch("allow-insecure-localhost");
  command_line->AppendSwitch("ignore-certificate-errors");
  LOG_DEBUG("OwlApp", "Certificate error handling enabled for .owl HTTPS server");

  // User agent - rotate for stealth (different UA per session)
  std::string ua = GetRandomUserAgent();
  LOG_DEBUG("OwlApp", "Setting User-Agent: " + ua);
  command_line->AppendSwitchWithValue("user-agent", ua);

  // Log final command line
  LOG_DEBUG("OwlApp", "Command line flags set successfully");
  LOG_DEBUG("OwlApp", "Has disable-automation: " + std::string(command_line->HasSwitch("disable-automation") ? "YES" : "NO"));
  LOG_DEBUG("OwlApp", "Has disable-blink-features: " + std::string(command_line->HasSwitch("disable-blink-features") ? "YES" : "NO"));
  LOG_DEBUG("OwlApp", "========================================");
}

void OwlApp::OnBeforeChildProcessLaunch(CefRefPtr<CefCommandLine> command_line) {
  // Pass session-wide seeds to child processes (renderer)
  // This is critical for ensuring browser and renderer select the SAME VM
  if (session_gpu_profile_ >= 0) {
    command_line->AppendSwitchWithValue("owl-gpu-profile", std::to_string(session_gpu_profile_));
  }
  if (session_vm_seed_ != 0) {
    command_line->AppendSwitchWithValue("owl-vm-seed", std::to_string(session_vm_seed_));
    LOG_DEBUG("OwlApp", "Passing VM seed to child process: " + std::to_string(session_vm_seed_));
  }
}

void OwlApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();

  LOG_DEBUG("OwlApp", "OnContextInitialized called - registering owl:// scheme handler");

  // Register handler for owl:// scheme
  CefRegisterSchemeHandlerFactory(
    "owl",
    "",  // Empty domain = wildcard
    new OwlTestSchemeHandlerFactory()
  );
  LOG_DEBUG("OwlApp", "Registered owl:// scheme handler factory");

  // Register HTTPS scheme handlers for .owl TLD domains
  // This enables ServiceWorker testing with proper HTTPS secure context
  // Each .owl domain needs explicit registration for ServiceWorker to work
  const std::vector<std::string> owl_domains = {
    "lie-detector.owl",
    "user-form.owl",
    "signin-form.owl",
    "test.owl"
  };

  for (const auto& domain : owl_domains) {
    CefRegisterSchemeHandlerFactory(
      "https",
      domain,
      new OwlHttpsSchemeHandlerFactory()
    );
    LOG_DEBUG("OwlApp", "Registered HTTPS scheme handler for: " + domain);
  }

  // ========== START EMBEDDED HTTPS SERVER FOR .owl DOMAINS ==========
  // This enables real HTTPS connections for ServiceWorker support
  // Domains are mapped to 127.0.0.1:8443 via host-resolver-rules
  std::string resources_path = OlibPlatform::GetResourcesDir();
  if (resources_path.empty()) {
    resources_path = ".";
  }
  std::string statics_path = resources_path + "/statics";

  if (OwlHttpsServer::Instance().Start(8443, statics_path)) {
    LOG_INFO("OwlApp", "HTTPS server started for .owl domains on port 8443");
  } else {
    LOG_ERROR("OwlApp", "Failed to start HTTPS server for .owl domains");
  }

  // Browser process initialization
  // The actual browser creation will be handled by OwlBrowserManager
}

void OwlApp::OnWebKitInitialized() {
  // Register JavaScript extensions for automation
  // NOTE: CefRegisterExtension creates globals with configurable:false
  // We use a name that blends with common JS patterns (looks like private module binding)
  std::string extensionCode =
    "var _bp;"
    "if (!_bp) _bp = {};"
    "(function() {"
    "  _bp.navigate = function(url) {"
    "    native function NativeNavigate();"
    "    return NativeNavigate(url);"
    "  };"
    "  _bp.click = function(selector) {"
    "    native function NativeClick();"
    "    return NativeClick(selector);"
    "  };"
    "  _bp.type = function(selector, text) {"
    "    native function NativeType();"
    "    return NativeType(selector, text);"
    "  };"
    "  _bp.extractText = function(selector) {"
    "    native function NativeExtractText();"
    "    return NativeExtractText(selector);"
    "  };"
    "  _bp.waitForSelector = function(selector, timeout) {"
    "    native function NativeWaitForSelector();"
    "    return NativeWaitForSelector(selector, timeout || 5000);"
    "  };"
    "})();";

  CefRegisterExtension(
    "v8/owl_automation",
    extensionCode,
    new OwlAutomationHandler()
  );
}

void OwlApp::OnContextCreated(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefV8Context> context) {

  // SWDEBUG: Log all context creations to detect SW contexts
  bool has_browser = browser.get() != nullptr;
  bool has_frame = frame.get() != nullptr;
  std::string frame_url = has_frame ? frame->GetURL().ToString() : "NO_FRAME";
  bool is_main = has_frame && frame->IsMain();
  LOG_INFO("SWDEBUG", "OnContextCreated: browser=" + std::string(has_browser ? "YES" : "NO") +
           " frame=" + std::string(has_frame ? "YES" : "NO") +
           " isMain=" + std::string(is_main ? "YES" : "NO") +
           " url=" + frame_url);

  // FORCE navigator.webdriver = false at C++ V8 level (like real Chrome)
  // fingerprint.com: undefined = "nodriver" detection, false = normal browser
  // This CANNOT be bypassed by JavaScript

  if (!context->Enter()) {
    return;
  }

  // =====================================================================
  // CRITICAL TIMING FIX: Inject early blob/worker interception FIRST
  // This MUST run before ANY other JavaScript, including page scripts.
  // The issue: Page scripts with <script defer> can create blob workers
  // before our normal spoof injection runs.
  // Solution: Inject blob/worker interception immediately after context->Enter()
  // =====================================================================
  {
    // CRITICAL: For worker contexts (ServiceWorker, SharedWorker, DedicatedWorker),
    // browser is nullptr according to CEF docs. We must check has_browser FIRST
    // before calling any method on browser.
    int browser_id = 0;
    std::string vm_id;

    if (has_browser) {
      browser_id = browser->GetIdentifier();
      vm_id = GetBrowserVMId(browser_id);
    } else {
      // Worker context - use fallback VM ID stored when browser was created
      // This is set in OnBrowserCreated and inherited by all workers in the renderer
      vm_id = default_worker_vm_id_;
      LOG_INFO("OwlApp", "[WORKER-CONTEXT] No browser reference (expected for workers), "
               "using default_worker_vm_id: " + vm_id);
    }

    const owl::VirtualMachine* vm = nullptr;

    if (!vm_id.empty()) {
      vm = owl::VirtualMachineDB::Instance().GetVM(vm_id);
    }

    if (vm && has_frame) {
      // =====================================================================
      // CRITICAL FIX: Inject early blob interceptor into BOTH main frame AND iframes
      // CreepJS creates iframes and runs worker detection from them. If iframes
      // don't have our early blob interceptor, workers created there will leak
      // native navigator values.
      // =====================================================================
      std::string early_blob_script =
          owl::spoofs::SpoofManager::Instance().GenerateEarlyBlobInterceptor(*vm);

      CefRefPtr<CefV8Value> retval;
      CefRefPtr<CefV8Exception> exception;
      bool success = context->Eval(early_blob_script, frame->GetURL(), 0, retval, exception);

      if (!success && exception) {
        LOG_ERROR("OwlApp", "[EARLY-BLOB] JavaScript error: " + exception->GetMessage().ToString() +
                  " at line " + std::to_string(exception->GetLineNumber()));
      } else {
        std::string frame_type = frame->IsMain() ? "main" : "iframe";
        LOG_INFO("OwlApp", "[EARLY-BLOB] Injected early blob/worker interception into " + frame_type +
                 " for browser_id=" + std::to_string(browser_id));
      }
    }
    // =====================================================================
    // CRITICAL: Inject worker spoofs directly into worker contexts
    // This handles ServiceWorker, DedicatedWorker, SharedWorker contexts
    // where frame is null. The V8 context is created BEFORE
    // the worker script runs, so we can inject spoofs here.
    // =====================================================================
    if (vm && !has_frame) {
      // Detect worker context type using V8 global scope inspection
      CefRefPtr<CefV8Value> global = context->GetGlobal();
      bool is_worker_context = false;
      std::string worker_type = "unknown";

      // IMPROVED DETECTION: Check if Window is undefined (workers don't have Window)
      CefRefPtr<CefV8Value> windowCheck = global->GetValue("Window");
      bool hasWindow = windowCheck.get() && !windowCheck->IsUndefined();

      // Also check for document (workers don't have document)
      CefRefPtr<CefV8Value> documentCheck = global->GetValue("document");
      bool hasDocument = documentCheck.get() && !documentCheck->IsUndefined();

      // If no Window and no document, this is likely a worker context
      if (!hasWindow && !hasDocument) {
        is_worker_context = true;

        // Now determine the specific worker type
        CefRefPtr<CefV8Value> swgs = global->GetValue("ServiceWorkerGlobalScope");
        if (swgs.get() && !swgs->IsUndefined() && swgs->IsFunction()) {
          worker_type = "ServiceWorker";
        } else {
          CefRefPtr<CefV8Value> shwgs = global->GetValue("SharedWorkerGlobalScope");
          if (shwgs.get() && !shwgs->IsUndefined() && shwgs->IsFunction()) {
            worker_type = "SharedWorker";
          } else {
            CefRefPtr<CefV8Value> dwgs = global->GetValue("DedicatedWorkerGlobalScope");
            if (dwgs.get() && !dwgs->IsUndefined() && dwgs->IsFunction()) {
              worker_type = "DedicatedWorker";
            } else {
              worker_type = "Worker";
            }
          }
        }

        LOG_INFO("OwlApp", "[WORKER-DETECT] No Window/document - detected as " + worker_type);
      } else {
        // Original detection as fallback
        CefRefPtr<CefV8Value> swgs = global->GetValue("ServiceWorkerGlobalScope");
        if (swgs.get() && !swgs->IsUndefined()) {
          is_worker_context = true;
          worker_type = "ServiceWorker";
        } else {
          CefRefPtr<CefV8Value> shwgs = global->GetValue("SharedWorkerGlobalScope");
          if (shwgs.get() && !shwgs->IsUndefined()) {
            is_worker_context = true;
            worker_type = "SharedWorker";
          } else {
            CefRefPtr<CefV8Value> dwgs = global->GetValue("DedicatedWorkerGlobalScope");
            if (dwgs.get() && !dwgs->IsUndefined()) {
              is_worker_context = true;
              worker_type = "DedicatedWorker";
            } else {
              CefRefPtr<CefV8Value> wgs = global->GetValue("WorkerGlobalScope");
              if (wgs.get() && !wgs->IsUndefined()) {
                is_worker_context = true;
                worker_type = "Worker";
              }
            }
          }
        }
      }

      if (is_worker_context) {
        LOG_INFO("OwlApp", "[WORKER-SPOOF] Detected " + worker_type + " context, injecting spoofs");

        // =====================================================================
        // CRITICAL C++ LEVEL NAVIGATOR PATCHING FOR WORKERS
        // =====================================================================
        // OnContextCreated fires AFTER the worker script has started parsing.
        // This means JavaScript-based patching via context->Eval() is TOO LATE -
        // module-level code like "const { hardwareConcurrency } = navigator"
        // has already captured the real values.
        //
        // SOLUTION: Patch navigator properties at C++ V8 level IMMEDIATELY.
        // This is done by getting the WorkerNavigator prototype and overriding
        // property descriptors with spoofed values. This works because:
        // 1. We patch the PROTOTYPE, not the navigator object itself
        // 2. Property access goes through prototype chain
        // 3. Even already-captured values will use our patched getters
        //
        // NOTE: This is a FALLBACK. Primary patching should happen via
        // ResponseFilter (for HTTP workers) or Blob interception (for blob workers).
        // This C++ patch catches cases where those mechanisms fail.
        // =====================================================================
        {
          CefRefPtr<CefV8Value> navigator = global->GetValue("navigator");
          if (navigator.get() && navigator->IsObject()) {
            // Direct property patching - works immediately
            navigator->SetValue("platform",
                CefV8Value::CreateString(vm->os.platform),
                V8_PROPERTY_ATTRIBUTE_READONLY);
            navigator->SetValue("hardwareConcurrency",
                CefV8Value::CreateInt(vm->cpu.hardware_concurrency),
                V8_PROPERTY_ATTRIBUTE_READONLY);
            navigator->SetValue("deviceMemory",
                CefV8Value::CreateDouble(static_cast<double>(vm->cpu.device_memory)),
                V8_PROPERTY_ATTRIBUTE_READONLY);
            navigator->SetValue("userAgent",
                CefV8Value::CreateString(vm->browser.user_agent),
                V8_PROPERTY_ATTRIBUTE_READONLY);
            navigator->SetValue("vendor",
                CefV8Value::CreateString(vm->browser.vendor),
                V8_PROPERTY_ATTRIBUTE_READONLY);
            navigator->SetValue("language",
                CefV8Value::CreateString(vm->language.primary),
                V8_PROPERTY_ATTRIBUTE_READONLY);
            navigator->SetValue("webdriver",
                CefV8Value::CreateBool(false),
                V8_PROPERTY_ATTRIBUTE_READONLY);

            // Languages array
            CefRefPtr<CefV8Value> languages = CefV8Value::CreateArray(
                static_cast<int>(vm->language.languages.size()));
            for (size_t i = 0; i < vm->language.languages.size(); ++i) {
              languages->SetValue(static_cast<int>(i),
                  CefV8Value::CreateString(vm->language.languages[i]));
            }
            navigator->SetValue("languages", languages, V8_PROPERTY_ATTRIBUTE_READONLY);

            LOG_INFO("OwlApp", "[WORKER-CPP-PATCH] Patched navigator at C++ level for " + worker_type +
                     ": platform=" + vm->os.platform +
                     " hardwareConcurrency=" + std::to_string(vm->cpu.hardware_concurrency));
          }
        }

        // CRITICAL FIX: Inject early blob interceptor into worker contexts FIRST
        // This ensures that if the worker (e.g., ServiceWorker) creates nested blob workers,
        // those blobs will also be intercepted and spoofed.
        // Without this, creep.js running in ServiceWorker creates unpatched blob workers.
        std::string early_blob_script =
            owl::spoofs::SpoofManager::Instance().GenerateEarlyBlobInterceptor(*vm);

        CefRefPtr<CefV8Value> retval_early;
        CefRefPtr<CefV8Exception> exception_early;
        bool early_success = context->Eval(early_blob_script, "", 0, retval_early, exception_early);

        if (!early_success && exception_early) {
          LOG_ERROR("OwlApp", "[WORKER-EARLY-BLOB] JavaScript error in " + worker_type + ": " +
                    exception_early->GetMessage().ToString() +
                    " at line " + std::to_string(exception_early->GetLineNumber()));
        } else {
          LOG_INFO("OwlApp", "[WORKER-EARLY-BLOB] Injected early blob interceptor into " + worker_type);
        }

        // Generate worker spoof script - this is the same script that gets
        // prepended to blob workers, but we inject it directly here
        std::string worker_spoof_script =
            owl::spoofs::SpoofManager::Instance().GenerateWorkerSpoofScript(*vm);

        CefRefPtr<CefV8Value> retval;
        CefRefPtr<CefV8Exception> exception;
        // Use empty URL for workers (they don't have a frame URL)
        bool success = context->Eval(worker_spoof_script, "", 0, retval, exception);

        if (!success && exception) {
          LOG_ERROR("OwlApp", "[WORKER-SPOOF] JavaScript error in " + worker_type + ": " +
                    exception->GetMessage().ToString() +
                    " at line " + std::to_string(exception->GetLineNumber()));
        } else {
          LOG_INFO("OwlApp", "[WORKER-SPOOF] Successfully injected spoofs into " + worker_type +
                   " (vm_id=" + vm_id + ", browser_id=" + std::to_string(browser_id) + ")");
        }
      }
    }
  }

  CefRefPtr<CefV8Value> global = context->GetGlobal();

  // CRITICAL: Remove automation artifacts FIRST, before any other checks
  // These properties are detected by fingerprint.js bot detection
  // Must run unconditionally before any page JS can capture them
  const char* automation_artifacts[] = {
    // NOTE: "owl" removed - now uses Symbol-keyed storage (no string key)
    // NightmareJS detection (Yo="nightmarejs") - CRITICAL
    "nightmare", "__nightmare",
    // CEF-specific (Go="cef")
    "RunPerfTest", "CefSharp", "cefQuery", "cefQueryCancel",
    // PhantomJS (Jo="phantomjs")
    "callPhantom", "_phantom", "__phantomas",
    // Electron/Node.js
    "process",
    // CoachJS (Uo="coachjs")
    "emit",
    // Other automation
    "awesomium", "domAutomation", "domAutomationController",
    // Selenium
    "_Selenium_IDE_Recorder", "_selenium", "calledSelenium",
    // Additional CEF
    "__cef", "__crWeb", "__gCrWeb"
  };

  for (const char* prop : automation_artifacts) {
    global->DeleteValue(prop);
  }

  // NOTE: We intentionally do NOT use Object.defineProperty to block these
  // properties from future assignment. Using defineProperty would CREATE the
  // property on window, and fingerprint.com's bot detection uses
  // Object.getOwnPropertyNames(window) to find them - even if they return
  // undefined, their mere existence triggers detection (e.g., nightmarejs).
  // The C++ DeleteValue above is sufficient to remove them.

  LOG_DEBUG("OwlApp", "Automation artifacts removed at C++ level (early)");

  // =====================================================================
  // CRITICAL: Try to delete prototype from Function.prototype.toString
  // This is needed to pass CreepJS "'prototype' in fn" check
  // Native functions should NOT have a prototype property
  // =====================================================================
  {
    CefRefPtr<CefV8Value> funcConstructor = global->GetValue("Function");
    if (funcConstructor.get() && funcConstructor->IsFunction()) {
      CefRefPtr<CefV8Value> funcProto = funcConstructor->GetValue("prototype");
      if (funcProto.get() && funcProto->IsObject()) {
        CefRefPtr<CefV8Value> toString = funcProto->GetValue("toString");
        if (toString.get() && toString->IsFunction()) {
          // Try to delete the prototype property from toString
          bool deleted = toString->DeleteValue("prototype");
          LOG_INFO("OwlApp", "Function.prototype.toString.prototype DeleteValue result: " +
                   std::string(deleted ? "SUCCESS" : "FAILED (non-configurable)"));
        }
      }
    }
  }

  // NOTE: webdriver spoofing is handled ONLY in owl_virtual_machine.cc
  // to avoid conflicts between C++ V8 value and JS Proxy getter.
  // The VirtualMachine implementation uses createNativeProxy which passes
  // all introspection tests from fingerprint.com/creepjs.
  // DO NOT add webdriver spoofing here - it causes "null conversion error",
  // "toString error", and "too much recursion" detection failures.

  // === ADDITIONAL C++ STEALTH FIXES ===

  CefRefPtr<CefV8Value> navigator = global->GetValue("navigator");

  // FIX 1: Ensure window.chrome object exists AND has proper runtime property
  CefRefPtr<CefV8Value> chrome = global->GetValue("chrome");
  if (!chrome.get() || chrome->IsUndefined()) {
    // Create chrome object if it doesn't exist
    CefRefPtr<CefV8Value> chromeObj = CefV8Value::CreateObject(nullptr, nullptr);

    // Add app property (common in real Chrome)
    CefRefPtr<CefV8Value> appObj = CefV8Value::CreateObject(nullptr, nullptr);
    chromeObj->SetValue("app", appObj, V8_PROPERTY_ATTRIBUTE_NONE);

    // Add loadTimes function
    chromeObj->SetValue("loadTimes",
      CefV8Value::CreateFunction("loadTimes", nullptr),
      V8_PROPERTY_ATTRIBUTE_NONE);

    // Add csi function
    chromeObj->SetValue("csi",
      CefV8Value::CreateFunction("csi", nullptr),
      V8_PROPERTY_ATTRIBUTE_NONE);

    global->SetValue("chrome", chromeObj, V8_PROPERTY_ATTRIBUTE_NONE);
    chrome = chromeObj;
    LOG_DEBUG("OwlApp", "window.chrome object created");
  } else {
    LOG_DEBUG("OwlApp", "window.chrome already exists");
  }

  // CRITICAL: Remove chrome.runtime.id if it exists (extension detection)
  if (chrome.get() && chrome->IsObject()) {
    CefRefPtr<CefV8Value> runtime = chrome->GetValue("runtime");
    if (runtime.get() && runtime->IsObject()) {
      // Delete runtime.id to avoid extension detection
      runtime->DeleteValue("id");
      LOG_DEBUG("OwlApp", "chrome.runtime.id removed");
    }
  }

  // NOTE: Automation artifacts now removed earlier (before navigator check)

  // FIX 2: Fix plugins array (real browsers have plugins)
  // Real Chrome 143+ has 5 PDF plugins (verified via fingerprint.com)
  if (navigator.get() && navigator->IsObject()) {
    CefRefPtr<CefV8Value> plugins = navigator->GetValue("plugins");
    if (plugins.get() && plugins->IsObject()) {
      // Spoof plugins count to match real Chrome (5 PDF plugins)
      CefRefPtr<CefV8Value> length = CefV8Value::CreateInt(5);
      plugins->SetValue("length", length, V8_PROPERTY_ATTRIBUTE_READONLY);
      LOG_DEBUG("OwlApp", "navigator.plugins.length spoofed to 5");
    }

    // FIX 3: Ensure languages array is properly set
    CefRefPtr<CefV8Value> languages = navigator->GetValue("languages");
    if (!languages.get() || languages->IsUndefined() || !languages->IsArray()) {
      CefRefPtr<CefV8Value> langArray = CefV8Value::CreateArray(2);
      langArray->SetValue(0, CefV8Value::CreateString("en-US"));
      langArray->SetValue(1, CefV8Value::CreateString("en"));
      navigator->SetValue("languages", langArray, V8_PROPERTY_ATTRIBUTE_READONLY);
      LOG_DEBUG("OwlApp", "navigator.languages fixed");
    }

    // NOTE: Navigator properties (platform, vendor, hardwareConcurrency, deviceMemory)
    // are now handled by VirtualMachineInjector in owl_stealth.cc for complete consistency.
    // Do NOT set READONLY properties here - they would prevent VirtualMachine from overriding.

    // FIX 7: Fix permissions API (common detection vector)
    CefRefPtr<CefV8Value> permissions = navigator->GetValue("permissions");
    if (permissions.get() && permissions->IsObject()) {
      // Permissions query behaves differently in automation - we'll fix this with JS since it needs Promise
      LOG_DEBUG("OwlApp", "navigator.permissions detected (will fix with JS)");
    }
  }

  // NOTE: Screen resolution is now handled by VirtualMachineInjector in owl_stealth.cc
  // for complete consistency with the VM profile.

  // =====================================================================
  // CRITICAL: Inject WebGL spoofing SYNCHRONOUSLY before context exits
  // This MUST happen before any page script can access WebGL
  // We use V8 Eval here, not ExecuteJavaScript, for synchronous execution
  // NOTE: Skip for worker contexts (they don't have WebGL and no browser reference)
  // =====================================================================
  if (has_browser && has_frame) {
    // Get VM profile for this browser using vm_id from OnBrowserCreated
    // CRITICAL: Use GetBrowserVMId to get the SAME vm_id that browser process selected
    // This ensures consistency across browser/renderer process boundary
    int browser_id = browser->GetIdentifier();

    // Get vm_id that was stored by OnBrowserCreated (received via extra_info)
    std::string vm_id = GetBrowserVMId(browser_id);
    const owl::VirtualMachine* vm = nullptr;

    if (!vm_id.empty()) {
      vm = owl::VirtualMachineDB::Instance().GetVM(vm_id);
      if (vm) {
        LOG_INFO("OwlApp", "[VM_SYNC] OnContextCreated: Using vm_id=" + vm_id +
                 " for browser_id=" + std::to_string(browser_id));
      } else {
        LOG_ERROR("OwlApp", "[VM_SYNC] OnContextCreated: GetVM failed for vm_id=" + vm_id +
                  " (not found in VirtualMachineDB)");
      }
    } else {
      LOG_ERROR("OwlApp", "[VM_SYNC] OnContextCreated: No vm_id stored for browser_id=" +
                std::to_string(browser_id) + " - OnBrowserCreated may not have been called yet!");
    }

    if (vm) {
      // =====================================================================
      // CRITICAL: Capture actual DPR BEFORE any spoofing
      // This must be the VERY FIRST script to run
      // =====================================================================
      {
        std::string dpr_capture_script = R"(
(function() {
  'use strict';
  // Capture ACTUAL rendering DPR through measurement
  // CEF headless reports devicePixelRatio=1 but renders at host DPR!
  // We detect by measuring text and comparing to expected values
  const _owl = Symbol.for('owl');
  if (!window[_owl]) {
    Object.defineProperty(window, _owl, {
      value: { font: {}, webgl: {}, camera: {} },
      writable: false, enumerable: false, configurable: false
    });
  }
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
      // Use reported if it matches detected, otherwise use detected (CEF headless bug)
      window[_owl].font.actualDPR = (Math.abs(reported - closest) < 0.1) ? reported : closest;
    } catch (e) {
      // Fallback to reported DPR if measurement fails (e.g., no document yet)
      window[_owl].font.actualDPR = window.devicePixelRatio || 1;
    }
  }
})();
)";
        CefRefPtr<CefV8Value> dpr_retval;
        CefRefPtr<CefV8Exception> dpr_exception;
        context->Eval(dpr_capture_script, frame->GetURL(), 0, dpr_retval, dpr_exception);
      }

      // =====================================================================
      // TODO: WebGL spoofing moved to src/stealth/spoofs/webgl_spoof.cc
      // Will be called via WebGLSpoof::InjectEarly(context, vm) from here
      // The modular spoof handles: getParameter, getShaderPrecisionFormat, getExtension
      // =====================================================================

      // =====================================================================
      // TODO: AudioContext spoofing moved to src/stealth/spoofs/audio_spoof.cc
      // Will be called via AudioSpoof::InjectEarly(context, vm) from here
      // The modular spoof handles: baseLatency, outputLatency, sampleRate, OfflineAudioContext
      // =====================================================================

      // =====================================================================
      // TODO: Canvas seed injection moved to src/stealth/spoofs/canvas_spoof.cc
      // Will be called via CanvasSpoof::InjectEarly(context, vm, context_id) from here
      // The modular spoof handles: canvas seed Symbol, toDataURL/toBlob noise
      // =====================================================================

      // Generate effective_context_id for fingerprint seed generation
      std::string effective_context_id = "ctx_" + std::to_string(session_vm_seed_) + "_" + std::to_string(browser_id);

      // =====================================================================
      // SYNCHRONOUS SPOOF INJECTION - For BOTH main frames AND iframes
      // Must run BEFORE context->Exit() to execute synchronously before any
      // page JavaScript. This ensures navigator/screen/etc are spoofed.
      // =====================================================================
      {
        bool is_main = frame->IsMain();
        LOG_DEBUG("OwlApp", "[SpoofManager] Injecting spoofs for " +
                  std::string(is_main ? "main frame" : "iframe") + ", vm=" + vm->id);

        // Generate spoofing script appropriate for frame type
        std::string spoof_script = owl::spoofs::SpoofManager::Instance().GenerateFrameScript(
            is_main,
            *vm,
            effective_context_id);

        // Execute synchronously using context->Eval()
        CefRefPtr<CefV8Value> retval;
        CefRefPtr<CefV8Exception> exception;
        bool success = context->Eval(spoof_script, frame->GetURL(), 0, retval, exception);

        if (!success && exception) {
          LOG_ERROR("OwlApp", "[SpoofManager] JavaScript error: " + exception->GetMessage().ToString() +
                    " at line " + std::to_string(exception->GetLineNumber()));
        }
      }

    } else {
      LOG_WARN("OwlApp", "No VM profile available for early spoofing");
    }
  }

  context->Exit();

  // Fallback for non-main frames or when VM is not available
  // IMPORTANT: Only inject into MAIN frame - iframes are handled by iframe interception
  // code in GenerateIframeInterceptionScript. Injecting into both causes double canvas noise.
  // NOTE: Skip for worker contexts (no frame or browser reference)
  if (has_frame && has_browser && frame->IsMain()) {
    LOG_INFO("OwlApp", "[SpoofManager DEBUG] frame->IsMain()=true browser_id=" +
             std::to_string(browser->GetIdentifier()));
    int browser_id = browser->GetIdentifier();

    // Check if we already injected synchronously (VM was available above)
    std::string vm_id = GetBrowserVMId(browser_id);
    const owl::VirtualMachine* vm = nullptr;
    if (!vm_id.empty()) {
      vm = owl::VirtualMachineDB::Instance().GetVM(vm_id);
    }

    // Only use fallback if VM was NOT available (synchronous injection didn't happen)
    if (!vm) {
      LOG_WARN("OwlApp", "SpoofManager: VM not found for browser_id=" + std::to_string(browser_id) +
               " vm_id=" + vm_id + ", using fallback old stealth injection");
      StealthConfig stealth_config = OwlStealth::GetContextFingerprint(browser_id);
      OwlStealth::InjectStealthPatchesWithConfig(frame, stealth_config);
    }
  }

  // =====================================================================
  // CLIPBOARD HOOKS - Browser Tool Functionality (NOT stealth)
  // Captures clipboard writes from JavaScript for browser_clipboard_read
  // Hooks: navigator.clipboard.writeText/write, document.execCommand('copy')
  // =====================================================================
  if (has_frame) {
    std::string clipboard_script = R"(
      (function() {
        'use strict';
        try {
          // Use a Symbol key to store clipboard text in a way that's hidden from enumeration
          const __owlClipboardKey = Symbol.for('__owl_clipboard__');
          if (window[__owlClipboardKey] === undefined) {
            window[__owlClipboardKey] = '';
          }

          // Hook navigator.clipboard API
          if (typeof navigator.clipboard === 'undefined') {
            // Create clipboard stub if not available
            const clipboardStub = {
              read: function() { return Promise.reject(new DOMException('Not allowed', 'NotAllowedError')); },
              readText: function() { return Promise.resolve(window[__owlClipboardKey] || ''); },
              write: function(data) {
                // Try to extract text from ClipboardItem
                if (data && data.length > 0) {
                  const item = data[0];
                  if (item && typeof item.getType === 'function') {
                    item.getType('text/plain').then(function(blob) {
                      blob.text().then(function(text) {
                        window[__owlClipboardKey] = text;
                      });
                    }).catch(function() {});
                  }
                }
                return Promise.resolve();
              },
              writeText: function(text) {
                window[__owlClipboardKey] = text;
                return Promise.resolve();
              }
            };
            Object.defineProperty(navigator, 'clipboard', {
              get: function() { return clipboardStub; },
              configurable: true,
              enumerable: true
            });
          } else {
            // Hook existing clipboard API to capture writes
            const origWriteText = navigator.clipboard.writeText.bind(navigator.clipboard);
            const origReadText = navigator.clipboard.readText.bind(navigator.clipboard);
            const origWrite = navigator.clipboard.write ? navigator.clipboard.write.bind(navigator.clipboard) : null;

            navigator.clipboard.writeText = function(text) {
              window[__owlClipboardKey] = text;
              return origWriteText(text);
            };

            navigator.clipboard.readText = function() {
              // Try native first, fall back to stored value
              return origReadText().catch(() => Promise.resolve(window[__owlClipboardKey] || ''));
            };

            // Hook write() for ClipboardItem support
            if (origWrite) {
              navigator.clipboard.write = function(data) {
                // Try to extract text from ClipboardItem array
                if (data && data.length > 0) {
                  const item = data[0];
                  if (item && typeof item.getType === 'function') {
                    item.getType('text/plain').then(function(blob) {
                      blob.text().then(function(text) {
                        window[__owlClipboardKey] = text;
                      });
                    }).catch(function() {});
                  }
                }
                return origWrite(data);
              };
            }
          }

          // Hook document.execCommand to capture 'copy' commands
          // This is used by many "click to copy" buttons for browser compatibility
          if (document.execCommand && !document.execCommand.__owlHooked) {
            const origExecCommand = document.execCommand.bind(document);
            document.execCommand = function(command, showUI, value) {
              if (command.toLowerCase() === 'copy') {
                // Capture the current selection before the copy
                try {
                  const selection = window.getSelection();
                  if (selection && selection.toString()) {
                    window[__owlClipboardKey] = selection.toString();
                  }
                } catch(e) {}
              }
              return origExecCommand(command, showUI, value);
            };
            document.execCommand.__owlHooked = true;
          }
        } catch(e) {}
      })();
    )";
    frame->ExecuteJavaScript(clipboard_script, frame->GetURL(), 0);
  }

  // Inject MutationObserver to track DOM changes for stability detection
  // NOTE: Skip for worker contexts (no frame)
  if (has_frame && frame->IsMain() && context->Enter()) {
    CefRefPtr<CefV8Value> global = context->GetGlobal();

    // Create callback function that sends IPC message
    class MutationCallback : public CefV8Handler {
    public:
      MutationCallback(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame)
        : browser_(browser), frame_(frame) {}

      virtual bool Execute(const CefString& name,
                          CefRefPtr<CefV8Value> object,
                          const CefV8ValueList& arguments,
                          CefRefPtr<CefV8Value>& retval,
                          CefString& exception) override {
        // Send DOM mutation notification to browser process
        CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("dom_mutation");
        frame_->SendProcessMessage(PID_BROWSER, msg);
        return true;
      }

    private:
      CefRefPtr<CefBrowser> browser_;
      CefRefPtr<CefFrame> frame_;
      IMPLEMENT_REFCOUNTING(MutationCallback);
    };

    // Register callback - use temporary name, will be moved to Symbol namespace
    CefRefPtr<CefV8Value> func = CefV8Value::CreateFunction("_0",
                                                             new MutationCallback(browser, frame));
    global->SetValue("_0", func, V8_PROPERTY_ATTRIBUTE_DONTENUM);

    // Register NLA execution callback for homepage
    class NLACallback : public CefV8Handler {
    public:
      NLACallback(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame)
        : browser_(browser), frame_(frame) {}

      virtual bool Execute(const CefString& name,
                          CefRefPtr<CefV8Value> object,
                          const CefV8ValueList& arguments,
                          CefRefPtr<CefV8Value>& retval,
                          CefString& exception) override {
        if (arguments.size() > 0 && arguments[0]->IsString()) {
          std::string command = arguments[0]->GetStringValue().ToString();
          // Send execute_nla message to browser process
          CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("execute_nla");
          CefRefPtr<CefListValue> args = msg->GetArgumentList();
          args->SetString(0, command);
          frame_->SendProcessMessage(PID_BROWSER, msg);
          return true;
        }
        return false;
      }

    private:
      CefRefPtr<CefBrowser> browser_;
      CefRefPtr<CefFrame> frame_;
      IMPLEMENT_REFCOUNTING(NLACallback);
    };

    CefRefPtr<CefV8Value> nlaFunc = CefV8Value::CreateFunction("_1",
                                                                new NLACallback(browser, frame));
    global->SetValue("_1", nlaFunc, V8_PROPERTY_ATTRIBUTE_DONTENUM);

    // Register generic message sending callback for homepage/playground
    class SendMessageCallback : public CefV8Handler {
    public:
      SendMessageCallback(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame)
        : browser_(browser), frame_(frame) {}

      virtual bool Execute(const CefString& name,
                          CefRefPtr<CefV8Value> object,
                          const CefV8ValueList& arguments,
                          CefRefPtr<CefV8Value>& retval,
                          CefString& exception) override {
        if (arguments.size() >= 1 && arguments[0]->IsString()) {
          std::string message_name = arguments[0]->GetStringValue().ToString();

          // Create process message
          CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create(message_name);

          // Add all string arguments (starting from index 1)
          if (arguments.size() >= 2) {
            CefRefPtr<CefListValue> args = msg->GetArgumentList();
            for (size_t i = 1; i < arguments.size(); i++) {
              if (arguments[i]->IsString()) {
                args->SetString(i - 1, arguments[i]->GetStringValue());
              } else if (arguments[i]->IsNull() || arguments[i]->IsUndefined()) {
                args->SetString(i - 1, "");
              }
            }
          }

          frame_->SendProcessMessage(PID_BROWSER, msg);
          return true;
        }
        return false;
      }

    private:
      CefRefPtr<CefBrowser> browser_;
      CefRefPtr<CefFrame> frame_;
      IMPLEMENT_REFCOUNTING(SendMessageCallback);
    };

    CefRefPtr<CefV8Value> sendMsgFunc = CefV8Value::CreateFunction("_2",
                                                                    new SendMessageCallback(browser, frame));
    global->SetValue("_2", sendMsgFunc, V8_PROPERTY_ATTRIBUTE_DONTENUM);

    // CRITICAL: Relocate CEF bindings to Symbol keys to avoid bot detection
    // fingerprint.com and similar services check for window._0, _1, _2 as automation signatures
    // Also copy native automation functions from _bp to owl Symbol namespace
    std::string relocate_bindings_script = R"(
      (function() {
        'use strict';
        // Use well-known Symbols for IPC bindings (invisible to detection scripts)
        const _owl = Symbol.for('owl');

        // Create owl Symbol namespace if it doesn't exist
        if (!window[_owl]) {
          Object.defineProperty(window, _owl, {
            value: { ipc: {}, automation: {} },
            writable: false, enumerable: false, configurable: false
          });
        }
        if (!window[_owl].ipc) window[_owl].ipc = {};
        if (!window[_owl].automation) window[_owl].automation = {};

        // Move IPC bindings to Symbol namespace
        if (typeof window._0 === 'function') {
          window[_owl].ipc.mutation = window._0;
          delete window._0;
        }
        if (typeof window._1 === 'function') {
          window[_owl].ipc.nla = window._1;
          delete window._1;
        }
        if (typeof window._2 === 'function') {
          window[_owl].ipc.sendMessage = window._2;
          delete window._2;
        }

        // Copy native automation functions from _bp to owl Symbol namespace
        if (typeof window._bp === 'object' && window._bp) {
          const n = window._bp;
          if (n.navigate) window[_owl].automation.navigate = n.navigate;
          if (n.click) window[_owl].automation.click = n.click;
          if (n.type) window[_owl].automation.type = n.type;
          if (n.extractText) window[_owl].automation.extractText = n.extractText;
          if (n.waitForSelector) window[_owl].automation.waitForSelector = n.waitForSelector;
          // Clear the object (can't delete because CEF makes it configurable:false)
          // but at least make it look empty/undefined-like
          try {
            delete window._bp.navigate;
            delete window._bp.click;
            delete window._bp.type;
            delete window._bp.extractText;
            delete window._bp.waitForSelector;
            // Try to replace with empty frozen object
            window._bp = Object.freeze({});
          } catch(e) {}
        }
      })();
    )";
    frame->ExecuteJavaScript(relocate_bindings_script, frame->GetURL(), 0);

    // Inject MutationObserver script - access binding via Symbol namespace
    std::string mutation_script = R"(
      (function() {
        const _owl = Symbol.for('owl');
        const ipc = window[_owl] && window[_owl].ipc;

        const observer = new MutationObserver(function(mutations) {
          if (ipc && ipc.mutation) {
            ipc.mutation();
          }
        });

        function startObserving() {
          if (document.body) {
            observer.observe(document.body, {
              childList: true,
              subtree: true,
              attributes: true,
              characterData: true
            });
          }
        }

        if (document.body) {
          startObserving();
        } else {
          document.addEventListener('DOMContentLoaded', startObserving);
        }
      })();
    )";
    frame->ExecuteJavaScript(mutation_script, frame->GetURL(), 0);
    context->Exit();
  }

  // DOM scan is now done on-demand via scan_element message
}

bool OwlApp::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {

  const std::string& message_name = message->GetName();

  // Handle renderer->browser messages
  if (source_process == PID_RENDERER) {
    if (message_name == "element_scan_result") {
      // Received element positions from renderer
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string context_id = args->GetString(0).ToString();
      int count = args->GetInt(1);

      LOG_DEBUG("MessageHandler", "=== RECEIVED SCAN RESULT === context=" + context_id +
               " elements=" + std::to_string(count));

      OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
      OwlSemanticMatcher* semantic = OwlSemanticMatcher::GetInstance();

      // Clear previous semantic data
      semantic->ClearContext(context_id);

      for (int i = 0; i < count; i++) {
        CefRefPtr<CefDictionaryValue> dict = args->GetDictionary(i + 2);

        ElementRenderInfo info;
        info.selector = dict->GetString("selector").ToString();
        info.tag = dict->GetString("tag").ToString();
        info.id = dict->GetString("id").ToString();
        info.className = dict->GetString("className").ToString();
        info.text = dict->GetString("text").ToString();
        info.x = dict->GetInt("x");
        info.y = dict->GetInt("y");
        info.width = dict->GetInt("width");
        info.height = dict->GetInt("height");
        info.visible = dict->GetBool("visible");

        tracker->RegisterElement(context_id, info);

        // Register with semantic matcher
        ElementSemantics sem;
        sem.selector = info.selector;
        sem.tag = info.tag;
        sem.id = info.id;
        sem.text = info.text;
        sem.x = info.x;
        sem.y = info.y;
        sem.width = info.width;
        sem.height = info.height;
        sem.visible = info.visible;

        // Extract additional semantic fields that are critical for proper matching
        sem.type = dict->GetString("type").ToString();
        sem.placeholder = dict->GetString("placeholder").ToString();
        sem.value = dict->GetString("value").ToString();
        sem.title = dict->GetString("title").ToString();
        sem.name = dict->GetString("name").ToString();
        sem.nearby_text = dict->GetString("nearby_text").ToString();
        sem.label_for = dict->GetString("label_for").ToString();
        // Note: aria_label would be from role attribute, but keeping it empty for now
        sem.aria_label = dict->GetString("role").ToString();

        // Enhanced visibility fields (from improved scanner)
        sem.z_index = dict->HasKey("z_index") ? dict->GetInt("z_index") : 0;
        sem.opacity = dict->HasKey("opacity") ? static_cast<float>(dict->GetDouble("opacity")) : 1.0f;
        sem.display = dict->HasKey("display") ? dict->GetString("display").ToString() : "";
        sem.visibility_css = dict->HasKey("visibility_css") ? dict->GetString("visibility_css").ToString() : "";
        sem.transform = dict->HasKey("transform") ? dict->GetString("transform").ToString() : "";

        semantic->RegisterElement(context_id, sem);

        if (i < 3) {
          LOG_DEBUG("MessageHandler", "  Registered: " + info.tag +
                    (info.id.empty() ? "" : "#" + info.id) + " text='" + info.text.substr(0, 20) + "'");
        }
      }

      LOG_DEBUG("MessageHandler", "All elements registered (tracker + semantic)");
      return true;
    }

    if (message_name == "evaluate_result") {
      // Received JavaScript evaluation result from renderer
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string context_id = args->GetString(0).ToString();
      std::string result = args->GetString(1).ToString();

      LOG_DEBUG("MessageHandler", "=== RECEIVED EVAL RESULT === context=" + context_id +
               " result=" + result.substr(0, 100));

      // Store the result for retrieval by OwlBrowserManager::Evaluate
      SetEvalResult(context_id, result);
      return true;
    }
  }

  // Handle browser->renderer messages
  if (source_process == PID_BROWSER) {
    LOG_DEBUG("MessageHandler", "Received message from browser: " + message_name);
    if (message_name == "scan_element") {
      // Request from browser process to scan for a specific element
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string context_id = args->GetString(0).ToString();
      std::string selector = args->GetString(1).ToString();
      LOG_DEBUG("MessageHandler", "scan_element request for context " + context_id);

      // Trigger immediate scan
      OwlElementScanner::ScanAndReportElements(browser, frame, context_id);
      return true;
    }

    if (message_name == "evaluate_script") {
      // Request from browser process to evaluate JavaScript and return result
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string context_id = args->GetString(0).ToString();
      std::string script = args->GetString(1).ToString();
      bool return_value = args->GetSize() > 2 ? args->GetBool(2) : false;
      LOG_DEBUG("MessageHandler", "evaluate_script request for context " + context_id + " return_value=" + (return_value ? "true" : "false"));

      // Get V8 context from frame
      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      std::string result_json = "{\"error\":\"V8 context not available\"}";

      if (v8_context && v8_context->Enter()) {
        CefRefPtr<CefV8Value> retval;
        CefRefPtr<CefV8Exception> exception;

        // Wrap user script in an IIFE
        // If return_value is true, wrap as expression: (function() { return (script); })()
        // Otherwise, wrap normally for execution: (function() { script })()
        std::string wrapped_script;
        if (return_value) {
          wrapped_script = "(function() { return (" + script + "); })()";
        } else {
          wrapped_script = "(function() { " + script + " })()";
        }

        bool success = v8_context->Eval(wrapped_script, frame->GetURL(), 0, retval, exception);

        if (success && retval) {
          // Convert V8 value to JSON string
          if (retval->IsNull() || retval->IsUndefined()) {
            result_json = "null";
          } else if (retval->IsBool()) {
            result_json = retval->GetBoolValue() ? "true" : "false";
          } else if (retval->IsInt()) {
            result_json = std::to_string(retval->GetIntValue());
          } else if (retval->IsDouble()) {
            result_json = std::to_string(retval->GetDoubleValue());
          } else if (retval->IsString()) {
            // Escape string for JSON
            std::string str = retval->GetStringValue().ToString();
            std::string escaped;
            escaped.reserve(str.length() + 2);
            escaped += '"';
            for (char c : str) {
              switch (c) {
                case '"': escaped += "\\\""; break;
                case '\\': escaped += "\\\\"; break;
                case '\n': escaped += "\\n"; break;
                case '\r': escaped += "\\r"; break;
                case '\t': escaped += "\\t"; break;
                default: escaped += c;
              }
            }
            escaped += '"';
            result_json = escaped;
          } else if (retval->IsArray()) {
            // For arrays, use JSON.stringify
            CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
            CefRefPtr<CefV8Value> json_obj = global->GetValue("JSON");
            if (json_obj && json_obj->IsObject()) {
              CefRefPtr<CefV8Value> stringify = json_obj->GetValue("stringify");
              if (stringify && stringify->IsFunction()) {
                CefV8ValueList stringify_args;
                stringify_args.push_back(retval);
                CefRefPtr<CefV8Value> json_result = stringify->ExecuteFunction(json_obj, stringify_args);
                if (json_result && json_result->IsString()) {
                  result_json = json_result->GetStringValue().ToString();
                }
              }
            }
          } else if (retval->IsObject()) {
            // For objects, use JSON.stringify
            CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
            CefRefPtr<CefV8Value> json_obj = global->GetValue("JSON");
            if (json_obj && json_obj->IsObject()) {
              CefRefPtr<CefV8Value> stringify = json_obj->GetValue("stringify");
              if (stringify && stringify->IsFunction()) {
                CefV8ValueList stringify_args;
                stringify_args.push_back(retval);
                CefRefPtr<CefV8Value> json_result = stringify->ExecuteFunction(json_obj, stringify_args);
                if (json_result && json_result->IsString()) {
                  result_json = json_result->GetStringValue().ToString();
                } else {
                  result_json = "{}";
                }
              }
            }
          }
        } else if (exception) {
          // Return error with exception message
          std::string error_msg = exception->GetMessage().ToString();
          // Escape for JSON
          std::string escaped;
          for (char c : error_msg) {
            switch (c) {
              case '"': escaped += "\\\""; break;
              case '\\': escaped += "\\\\"; break;
              case '\n': escaped += "\\n"; break;
              case '\r': escaped += "\\r"; break;
              default: escaped += c;
            }
          }
          result_json = "{\"error\":\"" + escaped + "\"}";
        }

        v8_context->Exit();
      }

      // Send result back to browser process
      CefRefPtr<CefProcessMessage> response = CefProcessMessage::Create("evaluate_result");
      CefRefPtr<CefListValue> response_args = response->GetArgumentList();
      response_args->SetString(0, context_id);
      response_args->SetString(1, result_json);
      frame->SendProcessMessage(PID_BROWSER, response);

      LOG_DEBUG("MessageHandler", "evaluate_script complete, result: " + result_json.substr(0, 100));
      return true;
    }

    if (message_name == "highlight_element") {
      // Request from browser process to highlight an element
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string context_id = args->GetString(0).ToString();
      std::string selector = args->GetString(1).ToString();
      int target_x = args->GetInt(2);
      int target_y = args->GetInt(3);
      std::string border_color = args->GetString(4).ToString();
      std::string background_color = args->GetString(5).ToString();

      LOG_DEBUG("MessageHandler", "highlight_element request for " + selector);

      // Get V8 context from frame
      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
        CefRefPtr<CefV8Value> document = global->GetValue("document");

        if (document && document->IsObject()) {
          // Query selector
          CefRefPtr<CefV8Value> querySelectorAll = document->GetValue("querySelectorAll");
          if (querySelectorAll && querySelectorAll->IsFunction()) {
            CefRefPtr<CefV8Value> selectorStr = CefV8Value::CreateString(selector);
            CefV8ValueList args_v8;
            args_v8.push_back(selectorStr);

            CefRefPtr<CefV8Value> elements = querySelectorAll->ExecuteFunction(document, args_v8);

            if (elements && elements->IsObject()) {
              CefRefPtr<CefV8Value> length = elements->GetValue("length");
              if (length && length->IsInt()) {
                int element_count = length->GetIntValue();

                // Find element at target position
                CefRefPtr<CefV8Value> target_element;
                for (int i = 0; i < element_count; i++) {
                  CefRefPtr<CefV8Value> elem = elements->GetValue(i);
                  if (elem && elem->IsObject()) {
                    // Get bounding rect
                    CefRefPtr<CefV8Value> getBoundingClientRect = elem->GetValue("getBoundingClientRect");
                    if (getBoundingClientRect && getBoundingClientRect->IsFunction()) {
                      CefV8ValueList no_args;
                      CefRefPtr<CefV8Value> rect = getBoundingClientRect->ExecuteFunction(elem, no_args);

                      if (rect && rect->IsObject()) {
                        int left = static_cast<int>(rect->GetValue("left")->GetDoubleValue());
                        int top = static_cast<int>(rect->GetValue("top")->GetDoubleValue());

                        if (abs(left - target_x) < 5 && abs(top - target_y) < 5) {
                          target_element = elem;
                          break;
                        }
                      }
                    }
                  }
                }

                // If single element, use it
                if (!target_element && element_count == 1) {
                  target_element = elements->GetValue(0);
                }

                // Apply highlight styles
                if (target_element) {
                  CefRefPtr<CefV8Value> style = target_element->GetValue("style");
                  if (style && style->IsObject()) {
                    style->SetValue("border", CefV8Value::CreateString("3px solid " + border_color), V8_PROPERTY_ATTRIBUTE_NONE);
                    style->SetValue("background", CefV8Value::CreateString(background_color), V8_PROPERTY_ATTRIBUTE_NONE);
                    style->SetValue("outline", CefV8Value::CreateString("2px solid " + border_color), V8_PROPERTY_ATTRIBUTE_NONE);
                    style->SetValue("outlineOffset", CefV8Value::CreateString("2px"), V8_PROPERTY_ATTRIBUTE_NONE);

                    // Scroll into view
                    CefRefPtr<CefV8Value> scrollIntoView = target_element->GetValue("scrollIntoView");
                    if (scrollIntoView && scrollIntoView->IsFunction()) {
                      CefV8ValueList scroll_args;
                      scrollIntoView->ExecuteFunction(target_element, scroll_args);
                    }

                  }
                }
              }
            }
          }
        }

        v8_context->Exit();
      }

      return true;
    }

    if (message_name == "show_grid_overlay") {
      // Request from browser process to show grid overlay with XY coordinates
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string context_id = args->GetString(0).ToString();
      int horizontal_lines = args->GetInt(1);
      int vertical_lines = args->GetInt(2);
      std::string line_color = args->GetString(3).ToString();
      std::string text_color = args->GetString(4).ToString();

      LOG_DEBUG("MessageHandler", "show_grid_overlay request");

      // Get V8 context from frame
      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
        CefRefPtr<CefV8Value> document = global->GetValue("document");

        if (document && document->IsObject()) {
          // First, remove any existing grid overlay
          CefRefPtr<CefV8Value> getElementById = document->GetValue("getElementById");
          if (getElementById && getElementById->IsFunction()) {
            CefV8ValueList id_args;
            id_args.push_back(CefV8Value::CreateString("owl-grid-overlay"));
            CefRefPtr<CefV8Value> existing_overlay = getElementById->ExecuteFunction(document, id_args);
            if (existing_overlay && existing_overlay->IsObject()) {
              CefRefPtr<CefV8Value> remove = existing_overlay->GetValue("remove");
              if (remove && remove->IsFunction()) {
                CefV8ValueList no_args;
                remove->ExecuteFunction(existing_overlay, no_args);
              }
            }
          }

          // Get body element
          CefRefPtr<CefV8Value> body = document->GetValue("body");
          if (body && body->IsObject()) {
            // Create overlay container div
            CefRefPtr<CefV8Value> createElement = document->GetValue("createElement");
            if (createElement && createElement->IsFunction()) {
              CefV8ValueList create_args;
              create_args.push_back(CefV8Value::CreateString("div"));
              CefRefPtr<CefV8Value> overlay = createElement->ExecuteFunction(document, create_args);

              if (overlay && overlay->IsObject()) {
                // Set overlay ID
                overlay->SetValue("id", CefV8Value::CreateString("owl-grid-overlay"), V8_PROPERTY_ATTRIBUTE_NONE);

                // Get style and set overlay positioning
                CefRefPtr<CefV8Value> style = overlay->GetValue("style");
                if (style && style->IsObject()) {
                  style->SetValue("position", CefV8Value::CreateString("fixed"), V8_PROPERTY_ATTRIBUTE_NONE);
                  style->SetValue("top", CefV8Value::CreateString("0"), V8_PROPERTY_ATTRIBUTE_NONE);
                  style->SetValue("left", CefV8Value::CreateString("0"), V8_PROPERTY_ATTRIBUTE_NONE);
                  style->SetValue("width", CefV8Value::CreateString("100vw"), V8_PROPERTY_ATTRIBUTE_NONE);
                  style->SetValue("height", CefV8Value::CreateString("100vh"), V8_PROPERTY_ATTRIBUTE_NONE);
                  style->SetValue("pointerEvents", CefV8Value::CreateString("none"), V8_PROPERTY_ATTRIBUTE_NONE);
                  style->SetValue("zIndex", CefV8Value::CreateString("999999"), V8_PROPERTY_ATTRIBUTE_NONE);
                  style->SetValue("overflow", CefV8Value::CreateString("hidden"), V8_PROPERTY_ATTRIBUTE_NONE);
                }

                // Get viewport dimensions using window.innerWidth/innerHeight
                CefRefPtr<CefV8Value> window = global->GetValue("window");
                int viewport_width = 1920;
                int viewport_height = 1080;
                if (window && window->IsObject()) {
                  CefRefPtr<CefV8Value> innerWidth = window->GetValue("innerWidth");
                  CefRefPtr<CefV8Value> innerHeight = window->GetValue("innerHeight");
                  if (innerWidth && innerWidth->IsInt()) {
                    viewport_width = innerWidth->GetIntValue();
                  }
                  if (innerHeight && innerHeight->IsInt()) {
                    viewport_height = innerHeight->GetIntValue();
                  }
                }


                // Calculate spacing between lines
                int h_spacing = viewport_height / (horizontal_lines + 1);
                int v_spacing = viewport_width / (vertical_lines + 1);

                // Create horizontal lines (top to bottom)
                for (int i = 1; i <= horizontal_lines; i++) {
                  int y_pos = i * h_spacing;

                  // Create line element
                  CefRefPtr<CefV8Value> line = createElement->ExecuteFunction(document, create_args);
                  if (line && line->IsObject()) {
                    CefRefPtr<CefV8Value> line_style = line->GetValue("style");
                    if (line_style && line_style->IsObject()) {
                      line_style->SetValue("position", CefV8Value::CreateString("absolute"), V8_PROPERTY_ATTRIBUTE_NONE);
                      line_style->SetValue("left", CefV8Value::CreateString("0"), V8_PROPERTY_ATTRIBUTE_NONE);
                      line_style->SetValue("top", CefV8Value::CreateString(std::to_string(y_pos) + "px"), V8_PROPERTY_ATTRIBUTE_NONE);
                      line_style->SetValue("width", CefV8Value::CreateString("100%"), V8_PROPERTY_ATTRIBUTE_NONE);
                      line_style->SetValue("height", CefV8Value::CreateString("1px"), V8_PROPERTY_ATTRIBUTE_NONE);
                      line_style->SetValue("backgroundColor", CefV8Value::CreateString(line_color), V8_PROPERTY_ATTRIBUTE_NONE);
                    }
                    // Append line to overlay
                    CefRefPtr<CefV8Value> appendChild = overlay->GetValue("appendChild");
                    if (appendChild && appendChild->IsFunction()) {
                      CefV8ValueList append_args;
                      append_args.push_back(line);
                      appendChild->ExecuteFunction(overlay, append_args);
                    }
                  }
                }

                // Create vertical lines (left to right)
                for (int i = 1; i <= vertical_lines; i++) {
                  int x_pos = i * v_spacing;

                  // Create line element
                  CefRefPtr<CefV8Value> line = createElement->ExecuteFunction(document, create_args);
                  if (line && line->IsObject()) {
                    CefRefPtr<CefV8Value> line_style = line->GetValue("style");
                    if (line_style && line_style->IsObject()) {
                      line_style->SetValue("position", CefV8Value::CreateString("absolute"), V8_PROPERTY_ATTRIBUTE_NONE);
                      line_style->SetValue("left", CefV8Value::CreateString(std::to_string(x_pos) + "px"), V8_PROPERTY_ATTRIBUTE_NONE);
                      line_style->SetValue("top", CefV8Value::CreateString("0"), V8_PROPERTY_ATTRIBUTE_NONE);
                      line_style->SetValue("width", CefV8Value::CreateString("1px"), V8_PROPERTY_ATTRIBUTE_NONE);
                      line_style->SetValue("height", CefV8Value::CreateString("100%"), V8_PROPERTY_ATTRIBUTE_NONE);
                      line_style->SetValue("backgroundColor", CefV8Value::CreateString(line_color), V8_PROPERTY_ATTRIBUTE_NONE);
                    }
                    // Append line to overlay
                    CefRefPtr<CefV8Value> appendChild = overlay->GetValue("appendChild");
                    if (appendChild && appendChild->IsFunction()) {
                      CefV8ValueList append_args;
                      append_args.push_back(line);
                      appendChild->ExecuteFunction(overlay, append_args);
                    }
                  }
                }

                // Create coordinate labels at intersections
                for (int hi = 1; hi <= horizontal_lines; hi++) {
                  for (int vi = 1; vi <= vertical_lines; vi++) {
                    int x_pos = vi * v_spacing;
                    int y_pos = hi * h_spacing;

                    // Create span element for coordinate label
                    CefV8ValueList span_args;
                    span_args.push_back(CefV8Value::CreateString("span"));
                    CefRefPtr<CefV8Value> label = createElement->ExecuteFunction(document, span_args);
                    if (label && label->IsObject()) {
                      // Set the coordinate text
                      std::string coord_text = std::to_string(x_pos) + "," + std::to_string(y_pos);
                      label->SetValue("textContent", CefV8Value::CreateString(coord_text), V8_PROPERTY_ATTRIBUTE_NONE);

                      CefRefPtr<CefV8Value> label_style = label->GetValue("style");
                      if (label_style && label_style->IsObject()) {
                        label_style->SetValue("position", CefV8Value::CreateString("absolute"), V8_PROPERTY_ATTRIBUTE_NONE);
                        label_style->SetValue("left", CefV8Value::CreateString(std::to_string(x_pos + 2) + "px"), V8_PROPERTY_ATTRIBUTE_NONE);
                        label_style->SetValue("top", CefV8Value::CreateString(std::to_string(y_pos + 2) + "px"), V8_PROPERTY_ATTRIBUTE_NONE);
                        label_style->SetValue("fontSize", CefV8Value::CreateString("9px"), V8_PROPERTY_ATTRIBUTE_NONE);
                        label_style->SetValue("fontFamily", CefV8Value::CreateString("monospace"), V8_PROPERTY_ATTRIBUTE_NONE);
                        label_style->SetValue("color", CefV8Value::CreateString(text_color), V8_PROPERTY_ATTRIBUTE_NONE);
                        label_style->SetValue("backgroundColor", CefV8Value::CreateString("rgba(255,255,255,0.7)"), V8_PROPERTY_ATTRIBUTE_NONE);
                        label_style->SetValue("padding", CefV8Value::CreateString("1px 2px"), V8_PROPERTY_ATTRIBUTE_NONE);
                        label_style->SetValue("borderRadius", CefV8Value::CreateString("2px"), V8_PROPERTY_ATTRIBUTE_NONE);
                        label_style->SetValue("whiteSpace", CefV8Value::CreateString("nowrap"), V8_PROPERTY_ATTRIBUTE_NONE);
                      }
                      // Append label to overlay
                      CefRefPtr<CefV8Value> appendChild = overlay->GetValue("appendChild");
                      if (appendChild && appendChild->IsFunction()) {
                        CefV8ValueList append_args;
                        append_args.push_back(label);
                        appendChild->ExecuteFunction(overlay, append_args);
                      }
                    }
                  }
                }

                // Append overlay to body
                CefRefPtr<CefV8Value> appendChild = body->GetValue("appendChild");
                if (appendChild && appendChild->IsFunction()) {
                  CefV8ValueList append_args;
                  append_args.push_back(overlay);
                  appendChild->ExecuteFunction(body, append_args);
                  LOG_DEBUG("MessageHandler", "Successfully created grid overlay");
                }
              }
            }
          }
        }

        v8_context->Exit();
      }

      return true;
    }

    if (message_name == "type_into_element") {
      // Request from browser process to type text into an input element
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string selector = args->GetString(0).ToString();
      std::string text = args->GetString(1).ToString();

      LOG_DEBUG("RendererIPC", "type_into_element request for " + selector + " text='" + text + "'");

      bool success = false;

      // Get V8 context from frame
      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
        CefRefPtr<CefV8Value> document = global->GetValue("document");

        if (document && document->IsObject()) {
          // Query selector
          CefRefPtr<CefV8Value> querySelector = document->GetValue("querySelector");
          if (querySelector && querySelector->IsFunction()) {
            CefV8ValueList args_v8;
            args_v8.push_back(CefV8Value::CreateString(selector));

            CefRefPtr<CefV8Value> element = querySelector->ExecuteFunction(document, args_v8);

            if (element && element->IsObject() && !element->IsNull()) {
              // Check if the element is an input-like element (input, textarea, or contenteditable)
              // If not, look for an input/textarea inside it
              CefRefPtr<CefV8Value> tagName = element->GetValue("tagName");
              std::string tag = tagName && tagName->IsString() ? tagName->GetStringValue().ToString() : "";
              std::transform(tag.begin(), tag.end(), tag.begin(), ::tolower);

              CefRefPtr<CefV8Value> targetElement = element;
              bool isInputLike = (tag == "input" || tag == "textarea");

              // Check for contenteditable
              if (!isInputLike) {
                CefRefPtr<CefV8Value> contentEditable = element->GetValue("contentEditable");
                if (contentEditable && contentEditable->IsString() &&
                    contentEditable->GetStringValue().ToString() == "true") {
                  isInputLike = true;
                }
              }

              // If not an input-like element, look for input/textarea inside
              if (!isInputLike) {
                CefRefPtr<CefV8Value> innerQS = element->GetValue("querySelector");
                if (innerQS && innerQS->IsFunction()) {
                  // Try to find input or textarea inside
                  CefV8ValueList inner_args;
                  inner_args.push_back(CefV8Value::CreateString("input, textarea, [contenteditable='true']"));
                  CefRefPtr<CefV8Value> innerElement = innerQS->ExecuteFunction(element, inner_args);

                  if (innerElement && innerElement->IsObject() && !innerElement->IsNull()) {
                    targetElement = innerElement;
                  } else {
                    // Also try document.activeElement in case click already focused an input
                    CefRefPtr<CefV8Value> activeElement = document->GetValue("activeElement");
                    if (activeElement && activeElement->IsObject() && !activeElement->IsNull()) {
                      CefRefPtr<CefV8Value> activeTag = activeElement->GetValue("tagName");
                      std::string activeTagStr = activeTag && activeTag->IsString() ? activeTag->GetStringValue().ToString() : "";
                      std::transform(activeTagStr.begin(), activeTagStr.end(), activeTagStr.begin(), ::tolower);
                      if (activeTagStr == "input" || activeTagStr == "textarea") {
                        targetElement = activeElement;
                      }
                    }
                  }
                }
              }

              // Focus the target element
              CefRefPtr<CefV8Value> focus = targetElement->GetValue("focus");
              if (focus && focus->IsFunction()) {
                CefV8ValueList no_args;
                focus->ExecuteFunction(targetElement, no_args);
              }

              // Set the value (use textContent for contenteditable, value for input/textarea)
              CefRefPtr<CefV8Value> targetTagName = targetElement->GetValue("tagName");
              std::string targetTag = targetTagName && targetTagName->IsString() ? targetTagName->GetStringValue().ToString() : "";
              std::transform(targetTag.begin(), targetTag.end(), targetTag.begin(), ::tolower);

              CefRefPtr<CefV8Value> targetContentEditable = targetElement->GetValue("contentEditable");
              bool isContentEditable = targetContentEditable && targetContentEditable->IsString() &&
                                       targetContentEditable->GetStringValue().ToString() == "true";

              if (isContentEditable && targetTag != "input" && targetTag != "textarea") {
                // For contenteditable elements, set textContent
                targetElement->SetValue("textContent", CefV8Value::CreateString(text), V8_PROPERTY_ATTRIBUTE_NONE);
              } else {
                // For input/textarea, set value
                targetElement->SetValue("value", CefV8Value::CreateString(text), V8_PROPERTY_ATTRIBUTE_NONE);
              }

              // Use targetElement for events from here
              element = targetElement;

              // Create and dispatch 'input' event
              CefRefPtr<CefV8Value> Event = global->GetValue("Event");
              if (Event && Event->IsFunction()) {
                CefV8ValueList event_args;
                event_args.push_back(CefV8Value::CreateString("input"));
                CefRefPtr<CefV8Value> event_options = CefV8Value::CreateObject(nullptr, nullptr);
                event_options->SetValue("bubbles", CefV8Value::CreateBool(true), V8_PROPERTY_ATTRIBUTE_NONE);
                event_options->SetValue("cancelable", CefV8Value::CreateBool(true), V8_PROPERTY_ATTRIBUTE_NONE);
                event_args.push_back(event_options);

                CefRefPtr<CefV8Value> inputEvent = Event->ExecuteFunction(global, event_args);

                if (inputEvent && inputEvent->IsObject()) {
                  CefRefPtr<CefV8Value> dispatchEvent = element->GetValue("dispatchEvent");
                  if (dispatchEvent && dispatchEvent->IsFunction()) {
                    CefV8ValueList dispatch_args;
                    dispatch_args.push_back(inputEvent);
                    dispatchEvent->ExecuteFunction(element, dispatch_args);
                  }
                }
              }

              // Create and dispatch 'change' event
              if (Event && Event->IsFunction()) {
                CefV8ValueList event_args;
                event_args.push_back(CefV8Value::CreateString("change"));
                CefRefPtr<CefV8Value> event_options = CefV8Value::CreateObject(nullptr, nullptr);
                event_options->SetValue("bubbles", CefV8Value::CreateBool(true), V8_PROPERTY_ATTRIBUTE_NONE);
                event_options->SetValue("cancelable", CefV8Value::CreateBool(true), V8_PROPERTY_ATTRIBUTE_NONE);
                event_args.push_back(event_options);

                CefRefPtr<CefV8Value> changeEvent = Event->ExecuteFunction(global, event_args);

                if (changeEvent && changeEvent->IsObject()) {
                  CefRefPtr<CefV8Value> dispatchEvent = element->GetValue("dispatchEvent");
                  if (dispatchEvent && dispatchEvent->IsFunction()) {
                    CefV8ValueList dispatch_args;
                    dispatch_args.push_back(changeEvent);
                    dispatchEvent->ExecuteFunction(element, dispatch_args);
                  }
                }
              }

              success = true;
            }
          }
        }

        v8_context->Exit();
      }

      // Send response back to browser process
      CefRefPtr<CefProcessMessage> response = CefProcessMessage::Create("type_into_element_response");
      CefRefPtr<CefListValue> response_args = response->GetArgumentList();
      response_args->SetBool(0, success);
      frame->SendProcessMessage(PID_BROWSER, response);

      return true;
    }

    if (message_name == "pick_from_select") {
      // Request from browser process to select an option from a dropdown
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string selector = args->GetString(0).ToString();
      std::string value = args->GetString(1).ToString();
      std::string context_id = args->GetSize() > 2 ? args->GetString(2).ToString() : "";

      LOG_DEBUG("RendererIPC", "pick_from_select request for " + selector + " value='" + value + "' context=" + context_id);

      bool success = false;

      // Get V8 context from frame
      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
        CefRefPtr<CefV8Value> document = global->GetValue("document");

        if (document && document->IsObject()) {
          CefRefPtr<CefV8Value> element;

          // First try querySelector with the CSS selector
          CefRefPtr<CefV8Value> querySelector = document->GetValue("querySelector");
          if (querySelector && querySelector->IsFunction()) {
            CefV8ValueList args_v8;
            args_v8.push_back(CefV8Value::CreateString(selector));
            element = querySelector->ExecuteFunction(document, args_v8);
          }

          // If querySelector failed, try activeElement (the element we just clicked)
          if (!element || element->IsNull() || element->IsUndefined()) {
            element = document->GetValue("activeElement");
            LOG_DEBUG("RendererIPC", "Using activeElement as fallback for pick_from_select");
          }

          if (element && element->IsObject() && !element->IsNull()) {
              // Check if this is a standard select element
              CefRefPtr<CefV8Value> tagName = element->GetValue("tagName");
              std::string tag = "";
              if (tagName && tagName->IsString()) {
                tag = tagName->GetStringValue().ToString();
                std::transform(tag.begin(), tag.end(), tag.begin(), ::tolower);
              }

              if (tag == "select") {
                // Standard <select> element - scan options
                CefRefPtr<CefV8Value> options = element->GetValue("options");
                if (options && options->IsObject()) {
                  CefRefPtr<CefV8Value> length = options->GetValue("length");
                  if (length && length->IsInt()) {
                    int num_options = length->GetIntValue();
                    // Scan through options to find matching value or text
                    for (int i = 0; i < num_options; i++) {
                      CefRefPtr<CefV8Value> option = options->GetValue(i);
                      if (option && option->IsObject()) {
                        // Check value attribute
                        CefRefPtr<CefV8Value> option_value = option->GetValue("value");
                        std::string opt_val = "";
                        if (option_value && option_value->IsString()) {
                          opt_val = option_value->GetStringValue().ToString();
                        }

                        // Check text content
                        CefRefPtr<CefV8Value> option_text = option->GetValue("textContent");
                        std::string opt_text = "";
                        if (option_text && option_text->IsString()) {
                          opt_text = option_text->GetStringValue().ToString();
                          // Trim whitespace
                          opt_text.erase(0, opt_text.find_first_not_of(" \t\n\r"));
                          opt_text.erase(opt_text.find_last_not_of(" \t\n\r") + 1);
                        }

                        // Check if this option matches (case-insensitive)
                        std::string value_lower = value;
                        std::string opt_val_lower = opt_val;
                        std::string opt_text_lower = opt_text;
                        std::transform(value_lower.begin(), value_lower.end(), value_lower.begin(), ::tolower);
                        std::transform(opt_val_lower.begin(), opt_val_lower.end(), opt_val_lower.begin(), ::tolower);
                        std::transform(opt_text_lower.begin(), opt_text_lower.end(), opt_text_lower.begin(), ::tolower);

                        if (opt_val_lower == value_lower || opt_text_lower == value_lower) {
                          // Set selectedIndex on the select element
                          element->SetValue("selectedIndex", CefV8Value::CreateInt(i), V8_PROPERTY_ATTRIBUTE_NONE);

                          // Also set selected property on the option
                          option->SetValue("selected", CefV8Value::CreateBool(true), V8_PROPERTY_ATTRIBUTE_NONE);

                          // Trigger change event
                          CefRefPtr<CefV8Value> Event = global->GetValue("Event");
                          if (Event && Event->IsFunction()) {
                            CefV8ValueList event_args;
                            event_args.push_back(CefV8Value::CreateString("change"));
                            CefRefPtr<CefV8Value> event_options = CefV8Value::CreateObject(nullptr, nullptr);
                            event_options->SetValue("bubbles", CefV8Value::CreateBool(true), V8_PROPERTY_ATTRIBUTE_NONE);
                            event_options->SetValue("cancelable", CefV8Value::CreateBool(true), V8_PROPERTY_ATTRIBUTE_NONE);
                            event_args.push_back(event_options);

                            CefRefPtr<CefV8Value> changeEvent = Event->ExecuteFunction(global, event_args);

                            if (changeEvent && changeEvent->IsObject()) {
                              CefRefPtr<CefV8Value> dispatchEvent = element->GetValue("dispatchEvent");
                              if (dispatchEvent && dispatchEvent->IsFunction()) {
                                CefV8ValueList dispatch_args;
                                dispatch_args.push_back(changeEvent);
                                dispatchEvent->ExecuteFunction(element, dispatch_args);
                              }
                            }
                          }

                          // Blur the select element to close the dropdown
                          CefRefPtr<CefV8Value> blur = element->GetValue("blur");
                          if (blur && blur->IsFunction()) {
                            CefV8ValueList no_args;
                            blur->ExecuteFunction(element, no_args);
                          }

                          success = true;
                          break;
                        }
                      }
                    }
                  }
                }
              } else {
                // Not a standard select - might be select2 or custom dropdown
                // Try to find an input field within it and type the value
                CefRefPtr<CefV8Value> querySelectorAll = element->GetValue("querySelectorAll");
                if (querySelectorAll && querySelectorAll->IsFunction()) {
                  CefV8ValueList search_args;
                  search_args.push_back(CefV8Value::CreateString("input[type='text'], input:not([type])"));
                  CefRefPtr<CefV8Value> inputs = querySelectorAll->ExecuteFunction(element, search_args);

                  if (inputs && inputs->IsObject()) {
                    CefRefPtr<CefV8Value> length = inputs->GetValue("length");
                    if (length && length->IsInt() && length->GetIntValue() > 0) {
                      CefRefPtr<CefV8Value> input = inputs->GetValue(0);
                      if (input && input->IsObject()) {
                        // Focus the input
                        CefRefPtr<CefV8Value> focus = input->GetValue("focus");
                        if (focus && focus->IsFunction()) {
                          CefV8ValueList no_args;
                          focus->ExecuteFunction(input, no_args);
                        }

                        // Set the value
                        input->SetValue("value", CefV8Value::CreateString(value), V8_PROPERTY_ATTRIBUTE_NONE);

                        // Trigger input and change events
                        CefRefPtr<CefV8Value> Event = global->GetValue("Event");
                        if (Event && Event->IsFunction()) {
                          // Input event
                          CefV8ValueList event_args;
                          event_args.push_back(CefV8Value::CreateString("input"));
                          CefRefPtr<CefV8Value> event_options = CefV8Value::CreateObject(nullptr, nullptr);
                          event_options->SetValue("bubbles", CefV8Value::CreateBool(true), V8_PROPERTY_ATTRIBUTE_NONE);
                          event_args.push_back(event_options);
                          CefRefPtr<CefV8Value> inputEvent = Event->ExecuteFunction(global, event_args);
                          if (inputEvent && inputEvent->IsObject()) {
                            CefRefPtr<CefV8Value> dispatchEvent = input->GetValue("dispatchEvent");
                            if (dispatchEvent && dispatchEvent->IsFunction()) {
                              CefV8ValueList dispatch_args;
                              dispatch_args.push_back(inputEvent);
                              dispatchEvent->ExecuteFunction(input, dispatch_args);
                            }
                          }
                        }

                        success = true;
                      }
                    }
                  }
                }
              }

          }
        }

        v8_context->Exit();
      }

      // Send response back to browser process
      CefRefPtr<CefProcessMessage> response = CefProcessMessage::Create("pick_from_select_response");
      CefRefPtr<CefListValue> response_args = response->GetArgumentList();
      response_args->SetString(0, context_id);
      response_args->SetBool(1, success);
      frame->SendProcessMessage(PID_BROWSER, response);

      return true;
    }

    if (message_name == "submit_form") {
      // Request from browser process to submit the form containing the focused element
      LOG_DEBUG("RendererIPC", "submit_form request - finding and submitting form");

      bool success = false;

      // Get V8 context from frame
      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
        CefRefPtr<CefV8Value> document = global->GetValue("document");

        if (document && document->IsObject()) {
          // Get the currently focused element
          CefRefPtr<CefV8Value> activeElement = document->GetValue("activeElement");

          if (activeElement && activeElement->IsObject() && !activeElement->IsNull()) {
            LOG_DEBUG("RendererIPC", "Found activeElement, looking for parent form");

            // Get the closest form element (activeElement.closest('form'))
            CefRefPtr<CefV8Value> closest = activeElement->GetValue("closest");
            if (closest && closest->IsFunction()) {
              CefV8ValueList args;
              args.push_back(CefV8Value::CreateString("form"));
              CefRefPtr<CefV8Value> form = closest->ExecuteFunction(activeElement, args);

              if (form && form->IsObject() && !form->IsNull()) {
                LOG_DEBUG("RendererIPC", "Found parent form, calling submit()");

                // Try requestSubmit() first (triggers validation), fallback to submit()
                CefRefPtr<CefV8Value> requestSubmit = form->GetValue("requestSubmit");
                if (requestSubmit && requestSubmit->IsFunction()) {
                  CefV8ValueList no_args;
                  requestSubmit->ExecuteFunction(form, no_args);
                  success = true;
                  LOG_DEBUG("RendererIPC", "Form submitted via requestSubmit()");
                } else {
                  // Fallback to submit()
                  CefRefPtr<CefV8Value> submit = form->GetValue("submit");
                  if (submit && submit->IsFunction()) {
                    CefV8ValueList no_args;
                    submit->ExecuteFunction(form, no_args);
                    success = true;
                    LOG_DEBUG("RendererIPC", "Form submitted via submit()");
                  }
                }
              } else {
                LOG_ERROR("RendererIPC", "No parent form found - element might not be in a form");
              }
            }
          } else {
            LOG_ERROR("RendererIPC", "No activeElement found");
          }
        }

        v8_context->Exit();
      }

      // Send response back to browser process
      CefRefPtr<CefProcessMessage> response = CefProcessMessage::Create("submit_form_response");
      CefRefPtr<CefListValue> response_args = response->GetArgumentList();
      response_args->SetBool(0, success);
      frame->SendProcessMessage(PID_BROWSER, response);

      return true;
    }

    if (message_name == "count_elements") {
      // Request from browser process to count elements matching a selector
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string selector = args->GetString(0).ToString();

      LOG_DEBUG("RendererIPC", "count_elements request for selector: " + selector);

      int count = 0;

      // Get V8 context from frame
      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
        CefRefPtr<CefV8Value> document = global->GetValue("document");

        if (document && document->IsObject()) {
          // Query selector all
          CefRefPtr<CefV8Value> querySelectorAll = document->GetValue("querySelectorAll");
          if (querySelectorAll && querySelectorAll->IsFunction()) {
            CefV8ValueList query_args;
            query_args.push_back(CefV8Value::CreateString(selector));

            CefRefPtr<CefV8Value> elements = querySelectorAll->ExecuteFunction(document, query_args);

            if (elements && elements->IsObject() && !elements->IsNull()) {
              // Get the length property
              CefRefPtr<CefV8Value> length = elements->GetValue("length");
              if (length && length->IsInt()) {
                count = length->GetIntValue();
              }
            }
          }
        }

        v8_context->Exit();
      }

      LOG_DEBUG("RendererIPC", "count_elements found " + std::to_string(count) + " elements");

      // Send response back to browser process
      CefRefPtr<CefProcessMessage> response = CefProcessMessage::Create("count_elements_response");
      CefRefPtr<CefListValue> response_args = response->GetArgumentList();
      response_args->SetInt(0, count);
      frame->SendProcessMessage(PID_BROWSER, response);

      return true;
    }

    if (message_name == "extract_text") {
      // Request from browser process to extract text from an element
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string context_id = args->GetString(0).ToString();
      std::string selector = args->GetString(1).ToString();
      int target_x = args->GetInt(2);
      int target_y = args->GetInt(3);

      std::string extracted_text = "";

      // Get V8 context from frame
      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
        CefRefPtr<CefV8Value> document = global->GetValue("document");

        if (document && document->IsObject()) {
          // If no selector provided, extract all visible text from body
          if (selector.empty()) {
            CefRefPtr<CefV8Value> body = document->GetValue("body");
            if (body && body->IsObject()) {
              CefRefPtr<CefV8Value> innerText = body->GetValue("innerText");
              if (innerText && innerText->IsString()) {
                extracted_text = innerText->GetStringValue().ToString();
              }
            }
          } else {
            // Query selector
            CefRefPtr<CefV8Value> querySelectorAll = document->GetValue("querySelectorAll");
            if (querySelectorAll && querySelectorAll->IsFunction()) {
              CefRefPtr<CefV8Value> selectorStr = CefV8Value::CreateString(selector);
              CefV8ValueList args_v8;
              args_v8.push_back(selectorStr);

              CefRefPtr<CefV8Value> elements = querySelectorAll->ExecuteFunction(document, args_v8);

              if (elements && elements->IsObject()) {
                CefRefPtr<CefV8Value> length = elements->GetValue("length");
                if (length && length->IsInt()) {
                  int element_count = length->GetIntValue();

                  // Find element at target position (if specified)
                  CefRefPtr<CefV8Value> target_element;
                  if (target_x >= 0 && target_y >= 0) {
                    for (int i = 0; i < element_count; i++) {
                      CefRefPtr<CefV8Value> elem = elements->GetValue(i);
                      if (elem && elem->IsObject()) {
                        CefRefPtr<CefV8Value> getBoundingClientRect = elem->GetValue("getBoundingClientRect");
                        if (getBoundingClientRect && getBoundingClientRect->IsFunction()) {
                          CefV8ValueList no_args;
                          CefRefPtr<CefV8Value> rect = getBoundingClientRect->ExecuteFunction(elem, no_args);

                          if (rect && rect->IsObject()) {
                            int left = static_cast<int>(rect->GetValue("left")->GetDoubleValue());
                            int top = static_cast<int>(rect->GetValue("top")->GetDoubleValue());

                            if (abs(left - target_x) < 5 && abs(top - target_y) < 5) {
                              target_element = elem;
                              break;
                            }
                          }
                        }
                      }
                    }
                  }

                  // If no position specified or single element, use first
                  if (!target_element && element_count > 0) {
                    target_element = elements->GetValue(0);
                  }

                  // Extract text content
                  if (target_element) {
                    CefRefPtr<CefV8Value> textContent = target_element->GetValue("textContent");
                    if (textContent && textContent->IsString()) {
                      extracted_text = textContent->GetStringValue().ToString();
                    }
                  }
                }
              }
            }
          }
        }

        v8_context->Exit();
      }

      // Send text back to browser process
      CefRefPtr<CefProcessMessage> response = CefProcessMessage::Create("extract_text_response");
      CefRefPtr<CefListValue> response_args = response->GetArgumentList();
      response_args->SetString(0, context_id);
      response_args->SetString(1, extracted_text);
      frame->SendProcessMessage(PID_BROWSER, response);

      return true;
    }

    if (message_name == "extract_page_text") {
      // Request from browser process to extract full page visible text
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string context_id = args->GetString(0).ToString();

      std::string page_text = "";

      // Get V8 context from frame
      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
        CefRefPtr<CefV8Value> document = global->GetValue("document");

        if (document && document->IsObject()) {
          // Get document.body
          CefRefPtr<CefV8Value> body = document->GetValue("body");
          if (body && body->IsObject()) {
            // Get innerText property
            CefRefPtr<CefV8Value> innerText = body->GetValue("innerText");
            if (innerText && innerText->IsString()) {
              page_text = innerText->GetStringValue().ToString();
            }
          }
        }

        v8_context->Exit();
      }

      // Send text back to browser process
      CefRefPtr<CefProcessMessage> response = CefProcessMessage::Create("extract_page_text_response");
      CefRefPtr<CefListValue> response_args = response->GetArgumentList();
      response_args->SetString(0, context_id);
      response_args->SetString(1, page_text);
      frame->SendProcessMessage(PID_BROWSER, response);

      return true;
    }

    if (message_name == "extract_page_html") {
      // Request from browser process to extract full page HTML
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string context_id = args->GetString(0).ToString();

      std::string page_html = "";

      // Get V8 context from frame
      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
        CefRefPtr<CefV8Value> document = global->GetValue("document");

        if (document && document->IsObject()) {
          // Get document.documentElement
          CefRefPtr<CefV8Value> documentElement = document->GetValue("documentElement");
          if (documentElement && documentElement->IsObject()) {
            // Get outerHTML property
            CefRefPtr<CefV8Value> outerHTML = documentElement->GetValue("outerHTML");
            if (outerHTML && outerHTML->IsString()) {
              page_html = outerHTML->GetStringValue().ToString();
            }
          }
        }

        v8_context->Exit();
      }

      // Send HTML back to browser process
      CefRefPtr<CefProcessMessage> response = CefProcessMessage::Create("extract_page_html_response");
      CefRefPtr<CefListValue> response_args = response->GetArgumentList();
      response_args->SetString(0, context_id);
      response_args->SetString(1, page_html);
      frame->SendProcessMessage(PID_BROWSER, response);

      return true;
    }

    // ============================================================
    // Scroll Control Message Handlers
    // ============================================================

    if (message_name == "scroll_by") {
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string context_id = args->GetString(0).ToString();
      int x = args->GetInt(1);
      int y = args->GetInt(2);

      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
        CefRefPtr<CefV8Value> window = global->GetValue("window");

        if (window && window->IsObject()) {
          CefRefPtr<CefV8Value> scrollBy = window->GetValue("scrollBy");
          if (scrollBy && scrollBy->IsFunction()) {
            CefV8ValueList scroll_args;
            scroll_args.push_back(CefV8Value::CreateInt(x));
            scroll_args.push_back(CefV8Value::CreateInt(y));
            scrollBy->ExecuteFunction(window, scroll_args);
          }
        }

        v8_context->Exit();
      }

      return true;
    }

    if (message_name == "scroll_to") {
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string context_id = args->GetString(0).ToString();
      int x = args->GetInt(1);
      int y = args->GetInt(2);

      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
        CefRefPtr<CefV8Value> window = global->GetValue("window");

        if (window && window->IsObject()) {
          CefRefPtr<CefV8Value> scrollTo = window->GetValue("scrollTo");
          if (scrollTo && scrollTo->IsFunction()) {
            CefV8ValueList scroll_args;
            scroll_args.push_back(CefV8Value::CreateInt(x));
            scroll_args.push_back(CefV8Value::CreateInt(y));
            scrollTo->ExecuteFunction(window, scroll_args);
          }
        }

        v8_context->Exit();
      }

      return true;
    }

    if (message_name == "scroll_to_element") {
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string context_id = args->GetString(0).ToString();
      std::string selector = args->GetString(1).ToString();


      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
        CefRefPtr<CefV8Value> document = global->GetValue("document");

        if (document && document->IsObject()) {
          // Query selector
          CefRefPtr<CefV8Value> querySelectorAll = document->GetValue("querySelectorAll");
          if (querySelectorAll && querySelectorAll->IsFunction()) {
            CefRefPtr<CefV8Value> selectorStr = CefV8Value::CreateString(selector);
            CefV8ValueList args_v8;
            args_v8.push_back(selectorStr);

            CefRefPtr<CefV8Value> elements = querySelectorAll->ExecuteFunction(document, args_v8);

            if (elements && elements->IsObject()) {
              CefRefPtr<CefV8Value> length = elements->GetValue("length");
              if (length && length->IsInt() && length->GetIntValue() > 0) {
                // Get first element
                CefRefPtr<CefV8Value> element = elements->GetValue(0);
                if (element && element->IsObject()) {
                  // Call scrollIntoView
                  CefRefPtr<CefV8Value> scrollIntoView = element->GetValue("scrollIntoView");
                  if (scrollIntoView && scrollIntoView->IsFunction()) {
                    CefV8ValueList scroll_args;
                    // Pass behavior: 'smooth' option
                    CefRefPtr<CefV8Value> options = CefV8Value::CreateObject(nullptr, nullptr);
                    options->SetValue("behavior", CefV8Value::CreateString("smooth"), V8_PROPERTY_ATTRIBUTE_NONE);
                    options->SetValue("block", CefV8Value::CreateString("center"), V8_PROPERTY_ATTRIBUTE_NONE);
                    scroll_args.push_back(options);
                    scrollIntoView->ExecuteFunction(element, scroll_args);
                  }
                }
              }
            }
          }
        }

        v8_context->Exit();
      }

      return true;
    }

    if (message_name == "dispatch_html5_drag") {
      // Dispatch HTML5 drag events for elements with draggable="true"
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string context_id = args->GetString(0).ToString();
      std::string source_selector = args->GetString(1).ToString();
      std::string target_selector = args->GetString(2).ToString();


      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
        CefRefPtr<CefV8Value> document = global->GetValue("document");

        if (document && document->IsObject()) {
          // Get querySelector function
          CefRefPtr<CefV8Value> querySelector = document->GetValue("querySelector");
          if (querySelector && querySelector->IsFunction()) {
            CefV8ValueList args_v8;

            // Get source element
            args_v8.push_back(CefV8Value::CreateString(source_selector));
            CefRefPtr<CefV8Value> sourceElement = querySelector->ExecuteFunction(document, args_v8);

            // Get target element
            args_v8.clear();
            args_v8.push_back(CefV8Value::CreateString(target_selector));
            CefRefPtr<CefV8Value> targetElement = querySelector->ExecuteFunction(document, args_v8);

            if (sourceElement && sourceElement->IsObject() && targetElement && targetElement->IsObject()) {
              // Create and dispatch drag events using eval (simpler and more reliable)
              // This executes JavaScript to create and dispatch proper DragEvent objects
              std::string js_code = R"(
                (function() {
                  const source = document.querySelector(')" + source_selector + R"(');
                  const target = document.querySelector(')" + target_selector + R"(');

                  if (!source || !target) {
                    return false;
                  }

                  // Create DataTransfer object
                  const dt = new DataTransfer();
                  dt.setData('text/plain', source.dataset.value || source.textContent);

                  // Create dragstart event
                  const dragStartEvent = new DragEvent('dragstart', {
                    bubbles: true,
                    cancelable: true,
                    dataTransfer: dt
                  });
                  source.dispatchEvent(dragStartEvent);

                  // Create dragenter event on target
                  const dragEnterEvent = new DragEvent('dragenter', {
                    bubbles: true,
                    cancelable: true,
                    dataTransfer: dt
                  });
                  target.dispatchEvent(dragEnterEvent);

                  // Create dragover event on target (required for drop to work)
                  const dragOverEvent = new DragEvent('dragover', {
                    bubbles: true,
                    cancelable: true,
                    dataTransfer: dt
                  });
                  target.dispatchEvent(dragOverEvent);

                  // Create drop event on target
                  const dropEvent = new DragEvent('drop', {
                    bubbles: true,
                    cancelable: true,
                    dataTransfer: dt
                  });
                  target.dispatchEvent(dropEvent);

                  // Create dragend event on source
                  const dragEndEvent = new DragEvent('dragend', {
                    bubbles: true,
                    cancelable: true,
                    dataTransfer: dt
                  });
                  source.dispatchEvent(dragEndEvent);

                  return true;
                })();
              )";

              // Execute the JavaScript via eval
              CefRefPtr<CefV8Value> eval_func = global->GetValue("eval");
              if (eval_func && eval_func->IsFunction()) {
                CefV8ValueList eval_args;
                eval_args.push_back(CefV8Value::CreateString(js_code));
                CefRefPtr<CefV8Value> result = eval_func->ExecuteFunction(global, eval_args);

                if (result && result->IsBool() && result->GetBoolValue()) {
                }
              }
            }
          }
        }

        v8_context->Exit();
      }

      return true;
    }

    if (message_name == "scroll_to_bottom") {
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string context_id = args->GetString(0).ToString();


      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
        CefRefPtr<CefV8Value> window = global->GetValue("window");
        CefRefPtr<CefV8Value> document = global->GetValue("document");

        if (window && window->IsObject() && document && document->IsObject()) {
          // Get document.body.scrollHeight
          CefRefPtr<CefV8Value> body = document->GetValue("body");
          if (body && body->IsObject()) {
            CefRefPtr<CefV8Value> scrollHeight = body->GetValue("scrollHeight");
            if (scrollHeight && scrollHeight->IsInt()) {
              int height = scrollHeight->GetIntValue();

              // Call window.scrollTo(0, height)
              CefRefPtr<CefV8Value> scrollTo = window->GetValue("scrollTo");
              if (scrollTo && scrollTo->IsFunction()) {
                CefV8ValueList scroll_args;
                scroll_args.push_back(CefV8Value::CreateInt(0));
                scroll_args.push_back(CefV8Value::CreateInt(height));
                scrollTo->ExecuteFunction(window, scroll_args);
              }
            }
          }
        }

        v8_context->Exit();
      }

      return true;
    }

    // ============================================================================
    // FOCUS/BLUR HANDLERS
    // ============================================================================

    if (message_name == "focus_element") {
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string selector = args->GetString(0).ToString();

      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
        CefRefPtr<CefV8Value> document = global->GetValue("document");

        if (document && document->IsObject()) {
          CefRefPtr<CefV8Value> querySelector = document->GetValue("querySelector");
          if (querySelector && querySelector->IsFunction()) {
            CefV8ValueList args_v8;
            args_v8.push_back(CefV8Value::CreateString(selector));
            CefRefPtr<CefV8Value> element = querySelector->ExecuteFunction(document, args_v8);

            if (element && element->IsObject() && !element->IsNull()) {
              CefRefPtr<CefV8Value> focus = element->GetValue("focus");
              if (focus && focus->IsFunction()) {
                CefV8ValueList no_args;
                focus->ExecuteFunction(element, no_args);
              }
            }
          }
        }

        v8_context->Exit();
      }

      return true;
    }

    if (message_name == "blur_element") {
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string selector = args->GetString(0).ToString();

      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
        CefRefPtr<CefV8Value> document = global->GetValue("document");

        if (document && document->IsObject()) {
          CefRefPtr<CefV8Value> querySelector = document->GetValue("querySelector");
          if (querySelector && querySelector->IsFunction()) {
            CefV8ValueList args_v8;
            args_v8.push_back(CefV8Value::CreateString(selector));
            CefRefPtr<CefV8Value> element = querySelector->ExecuteFunction(document, args_v8);

            if (element && element->IsObject() && !element->IsNull()) {
              CefRefPtr<CefV8Value> blur = element->GetValue("blur");
              if (blur && blur->IsFunction()) {
                CefV8ValueList no_args;
                blur->ExecuteFunction(element, no_args);
              }
            }
          }
        }

        v8_context->Exit();
      }

      return true;
    }

    // ============================================================================
    // FILE UPLOAD HANDLER
    // ============================================================================

    if (message_name == "upload_file") {
      // Handle file upload to a file input element
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string selector = args->GetString(0).ToString();
      std::string paths_json = args->GetString(1).ToString();

      LOG_DEBUG("MessageHandler", "upload_file: selector=" + selector + " paths=" + paths_json);

      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        // Use JavaScript to set files via DataTransfer API
        // This is the only way to programmatically set files on a file input
        std::string js_code = R"(
          (function(selector, pathsJson) {
            try {
              var input = document.querySelector(selector);
              if (!input) {
                return { success: false, error: 'Element not found: ' + selector };
              }
              if (input.type !== 'file') {
                return { success: false, error: 'Element is not a file input' };
              }

              var paths = JSON.parse(pathsJson);
              if (!paths || paths.length === 0) {
                return { success: false, error: 'No files specified' };
              }

              // Create a DataTransfer object to hold the files
              var dataTransfer = new DataTransfer();

              // For each path, create a File object
              // Note: In a real browser, we can't create File objects from arbitrary paths
              // But CEF allows this in the renderer process
              for (var i = 0; i < paths.length; i++) {
                var path = paths[i];
                var fileName = path.split('/').pop() || path.split('\\').pop() || 'file' + i;

                // Create a blob/file - the actual file content will be handled by CEF
                // This sets up the file input's files property
                var file = new File([''], fileName, { type: 'application/octet-stream' });
                file._path = path;  // Store path for CEF to resolve
                dataTransfer.items.add(file);
              }

              // Set the files on the input
              input.files = dataTransfer.files;

              // Dispatch change event
              var event = new Event('change', { bubbles: true });
              input.dispatchEvent(event);

              return {
                success: true,
                fileCount: input.files.length,
                files: Array.from(input.files).map(f => f.name)
              };
            } catch (e) {
              return { success: false, error: e.message };
            }
          })()" + std::string("('") + selector + "', '" + paths_json + "')";

        CefRefPtr<CefV8Value> retval;
        CefRefPtr<CefV8Exception> exception;

        if (v8_context->Eval(js_code, frame->GetURL(), 0, retval, exception)) {
          if (retval && retval->IsObject()) {
            CefRefPtr<CefV8Value> success_val = retval->GetValue("success");
            if (success_val && success_val->IsBool() && success_val->GetBoolValue()) {
              LOG_DEBUG("MessageHandler", "upload_file success");
            } else {
              CefRefPtr<CefV8Value> error_val = retval->GetValue("error");
              std::string error = error_val ? error_val->GetStringValue().ToString() : "Unknown error";
              LOG_ERROR("MessageHandler", "upload_file failed: " + error);
            }
          }
        } else if (exception) {
          LOG_ERROR("MessageHandler", "upload_file JS error: " + exception->GetMessage().ToString());
        }

        v8_context->Exit();
      }

      return true;
    }

    if (message_name == "verify_upload_files") {
      // Verify that a file input has the expected number of files
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string context_id = args->GetString(0).ToString();
      std::string selector = args->GetString(1).ToString();
      int expected_count = args->GetInt(2);

      bool success = false;
      int actual_count = 0;
      std::string error_message = "";

      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        std::string js_code = R"(
          (function(selector) {
            var input = document.querySelector(selector);
            if (!input) return { found: false, count: 0, error: 'Element not found' };
            if (input.type !== 'file') return { found: false, count: 0, error: 'Not a file input' };
            return {
              found: true,
              count: input.files ? input.files.length : 0,
              files: input.files ? Array.from(input.files).map(f => f.name) : []
            };
          })(')" + selector + "')";

        CefRefPtr<CefV8Value> retval;
        CefRefPtr<CefV8Exception> exception;

        if (v8_context->Eval(js_code, frame->GetURL(), 0, retval, exception)) {
          if (retval && retval->IsObject()) {
            CefRefPtr<CefV8Value> found_val = retval->GetValue("found");
            CefRefPtr<CefV8Value> count_val = retval->GetValue("count");

            if (found_val && found_val->IsBool() && found_val->GetBoolValue()) {
              actual_count = count_val ? count_val->GetIntValue() : 0;
              success = (actual_count == expected_count);
              if (!success) {
                error_message = "Expected " + std::to_string(expected_count) +
                               " files but got " + std::to_string(actual_count);
              }
            } else {
              CefRefPtr<CefV8Value> error_val = retval->GetValue("error");
              error_message = error_val ? error_val->GetStringValue().ToString() : "Element not found";
            }
          }
        } else if (exception) {
          error_message = "JS error: " + exception->GetMessage().ToString();
        }

        v8_context->Exit();
      }

      // Send result back to browser process
      CefRefPtr<CefProcessMessage> response = CefProcessMessage::Create("verify_upload_files_response");
      CefRefPtr<CefListValue> resp_args = response->GetArgumentList();
      resp_args->SetString(0, context_id);
      resp_args->SetBool(1, success);
      resp_args->SetInt(2, actual_count);
      resp_args->SetString(3, error_message);
      browser->GetMainFrame()->SendProcessMessage(PID_BROWSER, response);

      return true;
    }

    // ============================================================================
    // VERIFICATION HANDLERS - Used to verify tool actions actually succeeded
    // ============================================================================

    if (message_name == "verify_input_value") {
      // Verify that an input field contains the expected value after typing
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string context_id = args->GetString(0).ToString();
      std::string selector = args->GetString(1).ToString();
      std::string expected_value = args->GetString(2).ToString();

      bool success = false;
      std::string actual_value = "";
      std::string element_tag = "";
      std::string error_message = "";

      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
        CefRefPtr<CefV8Value> document = global->GetValue("document");

        if (document && document->IsObject()) {
          CefRefPtr<CefV8Value> element;

          // IMPORTANT: Check document.activeElement FIRST since we just typed into it.
          // This is more reliable than querySelector with a potentially inferred/wrong CSS selector.
          // The focused element is the one that received our keystrokes.
          element = document->GetValue("activeElement");

          // Verify activeElement is an input/textarea (not body or other non-input element)
          bool isValidActiveElement = false;
          if (element && element->IsObject() && !element->IsNull() && !element->IsUndefined()) {
            CefRefPtr<CefV8Value> tagName = element->GetValue("tagName");
            if (tagName && tagName->IsString()) {
              std::string tag = tagName->GetStringValue().ToString();
              std::transform(tag.begin(), tag.end(), tag.begin(), ::tolower);
              isValidActiveElement = (tag == "input" || tag == "textarea");
              // Also check contentEditable
              if (!isValidActiveElement) {
                CefRefPtr<CefV8Value> contentEditable = element->GetValue("contentEditable");
                if (contentEditable && contentEditable->IsString() &&
                    contentEditable->GetStringValue().ToString() == "true") {
                  isValidActiveElement = true;
                }
              }
            }
          }

          // If activeElement is not a valid input, fall back to querySelector
          if (!isValidActiveElement) {
            CefRefPtr<CefV8Value> querySelector = document->GetValue("querySelector");
            if (querySelector && querySelector->IsFunction()) {
              CefV8ValueList args_v8;
              args_v8.push_back(CefV8Value::CreateString(selector));
              element = querySelector->ExecuteFunction(document, args_v8);
            }
          }

          if (element && element->IsObject() && !element->IsNull() && !element->IsUndefined()) {
            // Get element tag name
            CefRefPtr<CefV8Value> tagName = element->GetValue("tagName");
            if (tagName && tagName->IsString()) {
              element_tag = tagName->GetStringValue().ToString();
              std::transform(element_tag.begin(), element_tag.end(), element_tag.begin(), ::tolower);
            }

            // Get input type for special handling (password, tel, etc.)
            std::string input_type = "text";
            if (element_tag == "input") {
              CefRefPtr<CefV8Value> typeAttr = element->GetValue("type");
              if (typeAttr && typeAttr->IsString()) {
                input_type = typeAttr->GetStringValue().ToString();
                std::transform(input_type.begin(), input_type.end(), input_type.begin(), ::tolower);
              }
            }

            // Check if it's a contenteditable element
            CefRefPtr<CefV8Value> contentEditable = element->GetValue("contentEditable");
            bool isContentEditable = contentEditable && contentEditable->IsString() &&
                                     contentEditable->GetStringValue().ToString() == "true";

            // Get the value
            if (element_tag == "input" || element_tag == "textarea") {
              CefRefPtr<CefV8Value> value = element->GetValue("value");
              if (value && value->IsString()) {
                actual_value = value->GetStringValue().ToString();
              }
            } else if (isContentEditable) {
              CefRefPtr<CefV8Value> textContent = element->GetValue("textContent");
              if (textContent && textContent->IsString()) {
                actual_value = textContent->GetStringValue().ToString();
              }
            } else {
              // Try to find input/textarea inside the element
              CefRefPtr<CefV8Value> innerQS = element->GetValue("querySelector");
              if (innerQS && innerQS->IsFunction()) {
                CefV8ValueList inner_args;
                inner_args.push_back(CefV8Value::CreateString("input, textarea"));
                CefRefPtr<CefV8Value> innerElement = innerQS->ExecuteFunction(element, inner_args);
                if (innerElement && innerElement->IsObject() && !innerElement->IsNull()) {
                  CefRefPtr<CefV8Value> value = innerElement->GetValue("value");
                  if (value && value->IsString()) {
                    actual_value = value->GetStringValue().ToString();
                    CefRefPtr<CefV8Value> innerTag = innerElement->GetValue("tagName");
                    if (innerTag && innerTag->IsString()) {
                      element_tag = innerTag->GetStringValue().ToString();
                      std::transform(element_tag.begin(), element_tag.end(), element_tag.begin(), ::tolower);
                    }
                    // Also get inner element's type
                    CefRefPtr<CefV8Value> innerType = innerElement->GetValue("type");
                    if (innerType && innerType->IsString()) {
                      input_type = innerType->GetStringValue().ToString();
                      std::transform(input_type.begin(), input_type.end(), input_type.begin(), ::tolower);
                    }
                  }
                }
              }
            }

            // ================================================================
            // SMART VERIFICATION based on input type
            // ================================================================

            // Helper lambda to strip non-numeric characters
            auto stripNonNumeric = [](const std::string& s) -> std::string {
              std::string result;
              for (char c : s) {
                if (c >= '0' && c <= '9') {
                  result += c;
                }
              }
              return result;
            };

            if (input_type == "password") {
              // PASSWORD FIELDS: Verify by length only (value may be masked or unreadable)
              // Success if actual has same length as expected, OR if actual is non-empty when expected is non-empty
              if (actual_value.length() == expected_value.length()) {
                success = true;
              } else if (!expected_value.empty() && !actual_value.empty()) {
                // Some browsers may truncate or mask - accept if both are non-empty
                success = true;
              } else if (expected_value.empty() && actual_value.empty()) {
                success = true;
              } else {
                error_message = "Password length mismatch";
              }
            } else if (input_type == "tel" || input_type == "phone") {
              // PHONE FIELDS: Strip formatting before comparison (auto-format adds parentheses, dashes, spaces)
              std::string expected_digits = stripNonNumeric(expected_value);
              std::string actual_digits = stripNonNumeric(actual_value);

              if (actual_digits == expected_digits) {
                success = true;
              } else if (!expected_digits.empty() && actual_digits.find(expected_digits) != std::string::npos) {
                // Actual contains expected (e.g., country code was added)
                success = true;
              } else if (!expected_digits.empty() && expected_digits.find(actual_digits) != std::string::npos) {
                // Expected contains actual (partial match)
                success = true;
              } else if (expected_digits.empty() && actual_digits.empty()) {
                success = true;
              } else {
                error_message = "Phone number mismatch (digits: expected=" + expected_digits + " actual=" + actual_digits + ")";
              }
            } else {
              // STANDARD FIELDS (text, email, etc.): Check exact match or contains
              if (!actual_value.empty() && actual_value.find(expected_value) != std::string::npos) {
                success = true;
              } else if (actual_value == expected_value) {
                success = true;
              } else if (actual_value.empty() && expected_value.empty()) {
                success = true;  // Both empty - success for clear operations
              } else {
                // Try case-insensitive comparison as fallback
                std::string lower_actual = actual_value;
                std::string lower_expected = expected_value;
                std::transform(lower_actual.begin(), lower_actual.end(), lower_actual.begin(), ::tolower);
                std::transform(lower_expected.begin(), lower_expected.end(), lower_expected.begin(), ::tolower);
                if (lower_actual == lower_expected || lower_actual.find(lower_expected) != std::string::npos) {
                  success = true;
                } else {
                  error_message = "Input value mismatch";
                }
              }
            }
          } else {
            error_message = "Element not found";
          }
        } else {
          error_message = "Document not available";
        }

        v8_context->Exit();
      }


      // Send response back to browser process
      CefRefPtr<CefProcessMessage> response = CefProcessMessage::Create("verify_input_value_response");
      CefRefPtr<CefListValue> response_args = response->GetArgumentList();
      response_args->SetString(0, context_id);
      response_args->SetBool(1, success);
      response_args->SetString(2, actual_value);
      response_args->SetString(3, expected_value);
      response_args->SetString(4, element_tag);
      response_args->SetString(5, error_message);
      frame->SendProcessMessage(PID_BROWSER, response);

      return true;
    }

    if (message_name == "verify_focus") {
      // Verify that a specific element has focus (is document.activeElement)
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string context_id = args->GetString(0).ToString();
      std::string expected_selector = args->GetString(1).ToString();


      bool success = false;
      std::string active_element_selector = "";
      std::string element_tag = "";
      std::string error_message = "";

      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
        CefRefPtr<CefV8Value> document = global->GetValue("document");

        if (document && document->IsObject()) {
          // Get document.activeElement
          CefRefPtr<CefV8Value> activeElement = document->GetValue("activeElement");

          if (activeElement && activeElement->IsObject() && !activeElement->IsNull()) {
            // Build selector for active element
            CefRefPtr<CefV8Value> tagName = activeElement->GetValue("tagName");
            CefRefPtr<CefV8Value> id = activeElement->GetValue("id");
            CefRefPtr<CefV8Value> className = activeElement->GetValue("className");

            if (tagName && tagName->IsString()) {
              element_tag = tagName->GetStringValue().ToString();
              std::transform(element_tag.begin(), element_tag.end(), element_tag.begin(), ::tolower);
              active_element_selector = element_tag;
            }

            if (id && id->IsString() && !id->GetStringValue().ToString().empty()) {
              active_element_selector = "#" + id->GetStringValue().ToString();
            } else if (className && className->IsString() && !className->GetStringValue().ToString().empty()) {
              std::string classes = className->GetStringValue().ToString();
              // Replace spaces with dots for CSS selector
              std::string class_selector = element_tag + ".";
              for (char c : classes) {
                if (c == ' ') {
                  class_selector += '.';
                } else {
                  class_selector += c;
                }
              }
              active_element_selector = class_selector;
            }

            // Check if active element matches expected
            // We do this by querying the expected selector and comparing
            CefRefPtr<CefV8Value> querySelector = document->GetValue("querySelector");
            if (querySelector && querySelector->IsFunction()) {
              CefV8ValueList args_v8;
              args_v8.push_back(CefV8Value::CreateString(expected_selector));
              CefRefPtr<CefV8Value> expected_element = querySelector->ExecuteFunction(document, args_v8);

              if (expected_element && expected_element->IsObject() && !expected_element->IsNull()) {
                // Compare the two elements - in V8 we can't directly compare objects
                // So we check if both have same id, or same position
                CefRefPtr<CefV8Value> expected_id = expected_element->GetValue("id");
                CefRefPtr<CefV8Value> active_id = activeElement->GetValue("id");

                if (expected_id && active_id && expected_id->IsString() && active_id->IsString() &&
                    !expected_id->GetStringValue().ToString().empty() &&
                    expected_id->GetStringValue().ToString() == active_id->GetStringValue().ToString()) {
                  success = true;
                } else {
                  // Compare bounding rects
                  CefRefPtr<CefV8Value> getBoundingClientRect1 = expected_element->GetValue("getBoundingClientRect");
                  CefRefPtr<CefV8Value> getBoundingClientRect2 = activeElement->GetValue("getBoundingClientRect");

                  if (getBoundingClientRect1 && getBoundingClientRect1->IsFunction() &&
                      getBoundingClientRect2 && getBoundingClientRect2->IsFunction()) {
                    CefV8ValueList no_args;
                    CefRefPtr<CefV8Value> rect1 = getBoundingClientRect1->ExecuteFunction(expected_element, no_args);
                    CefRefPtr<CefV8Value> rect2 = getBoundingClientRect2->ExecuteFunction(activeElement, no_args);

                    if (rect1 && rect2 && rect1->IsObject() && rect2->IsObject()) {
                      int left1 = static_cast<int>(rect1->GetValue("left")->GetDoubleValue());
                      int top1 = static_cast<int>(rect1->GetValue("top")->GetDoubleValue());
                      int left2 = static_cast<int>(rect2->GetValue("left")->GetDoubleValue());
                      int top2 = static_cast<int>(rect2->GetValue("top")->GetDoubleValue());

                      if (abs(left1 - left2) < 5 && abs(top1 - top2) < 5) {
                        success = true;
                      }
                    }
                  }
                }

                if (!success) {
                  error_message = "Active element does not match expected selector";
                }
              } else {
                error_message = "Expected element not found";
              }
            }
          } else {
            error_message = "No element has focus";
            element_tag = "body";
            active_element_selector = "body";
          }
        } else {
          error_message = "Document not available";
        }

        v8_context->Exit();
      }


      // Send response back to browser process
      CefRefPtr<CefProcessMessage> response = CefProcessMessage::Create("verify_focus_response");
      CefRefPtr<CefListValue> response_args = response->GetArgumentList();
      response_args->SetString(0, context_id);
      response_args->SetBool(1, success);
      response_args->SetString(2, active_element_selector);
      response_args->SetString(3, expected_selector);
      response_args->SetString(4, element_tag);
      response_args->SetString(5, error_message);
      frame->SendProcessMessage(PID_BROWSER, response);

      return true;
    }

    // ============================================================================
    // GET CHECKED STATE - Check if checkbox/radio is checked
    // ============================================================================
    if (message_name == "get_checked_state") {
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string context_id = args->GetString(0).ToString();
      std::string selector = args->GetString(1).ToString();

      bool found = false;
      bool is_checked = false;
      std::string element_tag = "";
      std::string error_message = "";

      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
        CefRefPtr<CefV8Value> document = global->GetValue("document");

        if (document && document->IsObject()) {
          CefRefPtr<CefV8Value> querySelector = document->GetValue("querySelector");
          if (querySelector && querySelector->IsFunction()) {
            CefV8ValueList args_v8;
            args_v8.push_back(CefV8Value::CreateString(selector));
            CefRefPtr<CefV8Value> element = querySelector->ExecuteFunction(document, args_v8);

            if (element && element->IsObject() && !element->IsNull()) {
              found = true;

              // Get element tag
              CefRefPtr<CefV8Value> tagName = element->GetValue("tagName");
              if (tagName && tagName->IsString()) {
                element_tag = tagName->GetStringValue().ToString();
                std::transform(element_tag.begin(), element_tag.end(), element_tag.begin(), ::tolower);
              }

              // Check the 'checked' property
              CefRefPtr<CefV8Value> checked = element->GetValue("checked");
              if (checked && checked->IsBool()) {
                is_checked = checked->GetBoolValue();
              } else {
                // For non-input elements, check aria-checked attribute
                CefRefPtr<CefV8Value> getAttribute = element->GetValue("getAttribute");
                if (getAttribute && getAttribute->IsFunction()) {
                  CefV8ValueList attr_args;
                  attr_args.push_back(CefV8Value::CreateString("aria-checked"));
                  CefRefPtr<CefV8Value> ariaChecked = getAttribute->ExecuteFunction(element, attr_args);
                  if (ariaChecked && ariaChecked->IsString()) {
                    is_checked = (ariaChecked->GetStringValue().ToString() == "true");
                  }
                }
              }
            } else {
              error_message = "Element not found: " + selector;
            }
          }
        } else {
          error_message = "Document not available";
        }

        v8_context->Exit();
      }

      // Send response back to browser process
      CefRefPtr<CefProcessMessage> response = CefProcessMessage::Create("get_checked_state_response");
      CefRefPtr<CefListValue> response_args = response->GetArgumentList();
      response_args->SetString(0, context_id);
      response_args->SetBool(1, found);
      response_args->SetBool(2, is_checked);
      response_args->SetString(3, element_tag);
      response_args->SetString(4, error_message);
      frame->SendProcessMessage(PID_BROWSER, response);

      return true;
    }

    // ============================================================================
    // GET SCROLL POSITION - Get current scroll position for verification
    // ============================================================================
    if (message_name == "get_scroll_position") {
      CefRefPtr<CefListValue> args = message->GetArgumentList();
      std::string context_id = args->GetString(0).ToString();

      int scroll_x = 0;
      int scroll_y = 0;
      int scroll_width = 0;
      int scroll_height = 0;

      CefRefPtr<CefV8Context> v8_context = frame->GetV8Context();
      if (v8_context && v8_context->Enter()) {
        CefRefPtr<CefV8Value> global = v8_context->GetGlobal();
        CefRefPtr<CefV8Value> window = global->GetValue("window");

        if (window && window->IsObject()) {
          CefRefPtr<CefV8Value> scrollX = window->GetValue("scrollX");
          CefRefPtr<CefV8Value> scrollY = window->GetValue("scrollY");
          if (scrollX && scrollX->IsDouble()) {
            scroll_x = static_cast<int>(scrollX->GetDoubleValue());
          } else if (scrollX && scrollX->IsInt()) {
            scroll_x = scrollX->GetIntValue();
          }
          if (scrollY && scrollY->IsDouble()) {
            scroll_y = static_cast<int>(scrollY->GetDoubleValue());
          } else if (scrollY && scrollY->IsInt()) {
            scroll_y = scrollY->GetIntValue();
          }
        }

        CefRefPtr<CefV8Value> document = global->GetValue("document");
        if (document && document->IsObject()) {
          CefRefPtr<CefV8Value> documentElement = document->GetValue("documentElement");
          if (documentElement && documentElement->IsObject()) {
            CefRefPtr<CefV8Value> scrollW = documentElement->GetValue("scrollWidth");
            CefRefPtr<CefV8Value> scrollH = documentElement->GetValue("scrollHeight");
            if (scrollW && scrollW->IsInt()) {
              scroll_width = scrollW->GetIntValue();
            }
            if (scrollH && scrollH->IsInt()) {
              scroll_height = scrollH->GetIntValue();
            }
          }
        }

        v8_context->Exit();
      }

      // Send response back to browser process
      CefRefPtr<CefProcessMessage> response = CefProcessMessage::Create("scroll_position_response");
      CefRefPtr<CefListValue> response_args = response->GetArgumentList();
      response_args->SetString(0, context_id);
      response_args->SetInt(1, scroll_x);
      response_args->SetInt(2, scroll_y);
      response_args->SetInt(3, scroll_width);
      response_args->SetInt(4, scroll_height);
      frame->SendProcessMessage(PID_BROWSER, response);

      return true;
    }
  }

  return false;
}
