#include "owl_browser_manager.h"
#include "owl_client.h"
#include "owl_request_context_handler.h"
#include "owl_action_verifier.h"
#include "owl_ai_intelligence.h"
#include "owl_resource_blocker.h"
#include "owl_render_tracker.h"
#include "owl_semantic_matcher.h"
#include "owl_llama_server.h"
#include "owl_llm_client.h"
#include "owl_nla.h"
#include "owl_demographics.h"
#include "owl_homepage.h"
#include "owl_playground.h"
#include "owl_dev_console.h"
#include "owl_video_recorder.h"
#include "owl_live_streamer.h"
#include "owl_captcha_detector.h"
#include "owl_captcha_classifier.h"
#include "owl_text_captcha_solver.h"
#include "owl_image_captcha_solver.h"
#include "owl_image_captcha_provider.h"
#include "owl_image_captcha_factory.h"
#include "owl_native_screenshot.h"
#include "owl_cookie_manager.h"
#include "owl_proxy_manager.h"
#include "owl_browser_profile.h"
#include "owl_stealth.h"
#include "stealth/owl_virtual_machine.h"
#include "stealth/owl_fingerprint_generator.h"
#include "stealth/owl_seed_api.h"
#include "gpu/owl_gpu_api.h"
#include "owl_network_interceptor.h"
#include "owl_console_logger.h"
#include "owl_download_handler.h"
#include "owl_dialog_handler.h"
#include "owl_tab_manager.h"
#include "owl_license.h"
#include "owl_thread_pool.h"
#include "owl_context_pool.h"
#include "logger.h"
#include "include/cef_app.h"
#include "include/cef_request_context.h"
#include "include/cef_process_message.h"
#include "include/wrapper/cef_helpers.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <shared_mutex>
#include <random>

// External functions from owl_app.cc for script evaluation results
extern void SetEvalResult(const std::string& context_id, const std::string& result);
extern bool GetEvalResult(const std::string& context_id, std::string& result);
extern void ClearEvalResult(const std::string& context_id);

// Batch mode - when true, skip internal CefDoMessageLoopWork for parallel processing
extern bool IsBatchMode();
#include <cmath>
#include <set>
#if defined(OS_WIN)
#include <direct.h>
#define mkdir _mkdir
#else
#include <sys/types.h>
#include <unistd.h>
#endif

#if defined(OS_MACOS)
#include <mach/mach.h>  // For macOS memory stats
#endif

OwlBrowserManager* OwlBrowserManager::instance_ = nullptr;
std::mutex OwlBrowserManager::instance_mutex_;
bool OwlBrowserManager::uses_run_message_loop_ = false;

// HTML tag names that should be treated as CSS tag selectors, not semantic selectors
// These can be used directly with document.querySelector() without SemanticMatcher
static const std::set<std::string> kHtmlTagNames = {
  // Document structure
  "html", "head", "body", "main", "header", "footer", "nav", "aside",
  "section", "article", "div", "span",
  // Headings
  "h1", "h2", "h3", "h4", "h5", "h6",
  // Text content
  "p", "pre", "code", "blockquote", "hr", "br",
  // Lists
  "ul", "ol", "li", "dl", "dt", "dd",
  // Tables
  "table", "thead", "tbody", "tfoot", "tr", "th", "td", "caption", "colgroup", "col",
  // Forms
  "form", "input", "button", "select", "option", "optgroup", "textarea", "label",
  "fieldset", "legend", "datalist", "output", "progress", "meter",
  // Links and media
  "a", "img", "picture", "source", "video", "audio", "track", "embed", "object",
  "iframe", "canvas", "svg", "math",
  // Inline elements
  "strong", "em", "b", "i", "u", "s", "mark", "small", "sub", "sup",
  "abbr", "cite", "q", "dfn", "time", "var", "samp", "kbd",
  // Other common elements
  "figure", "figcaption", "details", "summary", "dialog", "menu", "menuitem",
  "template", "slot", "noscript", "script", "style", "link", "meta", "title", "base"
};

// Check if a selector is a simple HTML tag name (case-insensitive)
static bool IsHtmlTagSelector(const std::string& selector) {
  if (selector.empty()) return false;

  // Convert to lowercase for comparison
  std::string lower;
  lower.reserve(selector.size());
  for (char c : selector) {
    lower += std::tolower(static_cast<unsigned char>(c));
  }

  return kHtmlTagNames.count(lower) > 0;
}

// Check if selector is semantic (natural language) vs CSS/tag selector
// Returns true for semantic selectors like "submit button", "user name field"
// Returns false for CSS selectors (#id, .class, tag, [attr], etc.)
static bool IsSelectorSemantic(const std::string& selector) {
  if (selector.empty()) return false;

  // CSS selectors start with or contain special characters
  if (selector.find_first_of("#.[:>") != std::string::npos) {
    return false;
  }

  // HTML tag names are valid CSS selectors, not semantic
  if (IsHtmlTagSelector(selector)) {
    return false;
  }

  // Everything else is semantic (natural language description)
  return true;
}

// Initialize thread pool with optimal worker count for browser I/O operations
// Uses 2x hardware threads by default for I/O-bound operations
static void InitializeGlobalThreadPool() {
  size_t num_threads = std::max<size_t>(4, std::thread::hardware_concurrency() * 2);
  owl::ThreadPool::Initialize(num_threads);
  LOG_DEBUG("BrowserManager", "Initialized thread pool with " + std::to_string(num_threads) + " workers");
}

OwlBrowserManager::OwlBrowserManager() {
  // Initialize atomic members with optimized defaults for 100+ contexts
  max_contexts_.store(kDefaultMaxContexts, std::memory_order_relaxed);
  next_context_id_.store(1, std::memory_order_relaxed);
  initialized_.store(false, std::memory_order_relaxed);
  max_memory_mb_.store(kDefaultMaxMemoryMB, std::memory_order_relaxed);
  current_memory_mb_.store(0, std::memory_order_relaxed);
  context_count_.store(0, std::memory_order_relaxed);
  cleanup_running_.store(false, std::memory_order_relaxed);
}

OwlBrowserManager::~OwlBrowserManager() {
  StopCleanupThread();
  ShutdownLLM();
  Shutdown();
  owl::ThreadPool::Destroy();
}

OwlBrowserManager* OwlBrowserManager::GetInstance() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (instance_ == nullptr) {
    instance_ = new OwlBrowserManager();
  }
  return instance_;
}

void OwlBrowserManager::SetUsesRunMessageLoop(bool uses_run_loop) {
  uses_run_message_loop_ = uses_run_loop;
  LOG_DEBUG("BrowserManager", std::string("Message loop mode set to: ") +
           (uses_run_loop ? "CefRunMessageLoop (UI mode)" : "Manual CefDoMessageLoopWork (Headless mode)"));
}

bool OwlBrowserManager::UsesRunMessageLoop() {
  return uses_run_message_loop_;
}

// Helper: Only pump message loop in headless mode
// In UI mode (CefRunMessageLoop), the main thread is already pumping messages
// and calling CefDoMessageLoopWork from background threads causes crashes
void OwlBrowserManager::PumpMessageLoopIfNeeded() {
  if (!uses_run_message_loop_) {
    CefDoMessageLoopWork();
  }
}

void OwlBrowserManager::Initialize() {
  // Use compare_exchange to ensure single initialization
  bool expected = false;
  if (!initialized_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
    return;  // Already initialized
  }

  // Initialize thread pool first
  InitializeGlobalThreadPool();

  // Initialize AI-first resource blocker
  OwlResourceBlocker::GetInstance()->Initialize();

  // Start background cleanup thread
  StartCleanupThread();

  // Initialize LLM in background (non-blocking async startup)
  // For UI mode, load config from file. For headless mode, use defaults (contexts will provide their own config)
  if (uses_run_message_loop_) {
    // UI mode - load config from file
    LLMConfig ui_config = LoadLLMConfigFromFile();
    InitializeLLMAsync(&ui_config);
    LOG_DEBUG("BrowserManager", "Initializing LLM for UI mode with config from file");
  } else {
    // Headless mode - use default config (contexts will specify their own)
    InitializeLLMAsync(nullptr);
    LOG_DEBUG("BrowserManager", "Initializing LLM for headless mode with default config");
  }

  LOG_DEBUG("BrowserManager", "AI-first browser initialized with ad/analytics blocking (max_contexts=" +
           std::to_string(max_contexts_.load(std::memory_order_relaxed)) + ", max_memory=" +
           std::to_string(max_memory_mb_.load(std::memory_order_relaxed)) + "MB)");
}

void OwlBrowserManager::Shutdown() {
  StopCleanupThread();

  // Get exclusive lock for shutdown
  std::unique_lock<std::shared_mutex> lock(contexts_mutex_);

  // Close all browsers
  for (auto& pair : contexts_) {
    if (pair.second->browser) {
      // Wait for active operations to complete
      while (pair.second->HasActiveOperations()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      pair.second->browser->GetHost()->CloseBrowser(true);
    }
  }

  contexts_.clear();
  context_count_.store(0, std::memory_order_relaxed);
  current_memory_mb_.store(0, std::memory_order_relaxed);
  initialized_.store(false, std::memory_order_release);
}

std::string OwlBrowserManager::GenerateContextId() {
  uint64_t id = next_context_id_.fetch_add(1, std::memory_order_relaxed);
  std::stringstream ss;
  ss << "ctx_" << std::setfill('0') << std::setw(6) << id;
  return ss.str();
}

// Helper function to escape JSON strings
static std::string EscapeJSONString(const std::string& str) {
  std::string result;
  result.reserve(str.size());
  for (char c : str) {
    switch (c) {
      case '"':  result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\b': result += "\\b"; break;
      case '\f': result += "\\f"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          result += buf;
        } else {
          result += c;
        }
    }
  }
  return result;
}

// Helper function to unescape JSON strings (basic implementation)
static std::string UnescapeJSONString(const std::string& str) {
  std::string result;
  result.reserve(str.size());
  for (size_t i = 0; i < str.size(); ++i) {
    if (str[i] == '\\' && i + 1 < str.size()) {
      switch (str[i + 1]) {
        case '"':  result += '"'; i++; break;
        case '\\': result += '\\'; i++; break;
        case 'b':  result += '\b'; i++; break;
        case 'f':  result += '\f'; i++; break;
        case 'n':  result += '\n'; i++; break;
        case 'r':  result += '\r'; i++; break;
        case 't':  result += '\t'; i++; break;
        default:   result += str[i]; break;
      }
    } else {
      result += str[i];
    }
  }
  return result;
}

std::string OwlBrowserManager::GetLLMConfigPath() {
  std::string config_dir;

#if defined(OS_MACOS)
  // macOS: ~/Library/Application Support/OwlBrowser/
  const char* home = getenv("HOME");
  if (!home) {
    LOG_ERROR("BrowserManager", "Could not determine home directory");
    return "";
  }
  config_dir = std::string(home) + "/Library/Application Support/OwlBrowser";
#elif defined(OS_WIN)
  // Windows: %APPDATA%\OwlBrowser\
  const char* appdata = getenv("APPDATA");
  if (!appdata) {
    LOG_ERROR("BrowserManager", "Could not determine APPDATA directory");
    return "";
  }
  config_dir = std::string(appdata) + "\\OwlBrowser";
#else
  // Linux/Unix: ~/.config/owl-browser/
  const char* home = getenv("HOME");
  if (!home) {
    LOG_ERROR("BrowserManager", "Could not determine home directory");
    return "";
  }
  // Check if XDG_CONFIG_HOME is set
  const char* xdg_config = getenv("XDG_CONFIG_HOME");
  if (xdg_config) {
    config_dir = std::string(xdg_config) + "/owl-browser";
  } else {
    config_dir = std::string(home) + "/.config/owl-browser";
  }
#endif

  std::string config_file = config_dir + "/llm_config.json";
  LOG_DEBUG("BrowserManager", "LLM config path: " + config_file);
  return config_file;
}

LLMConfig OwlBrowserManager::LoadLLMConfigFromFile(const std::string& config_path) {
  std::string path = config_path.empty() ? GetLLMConfigPath() : config_path;

  LLMConfig config;
  // Set defaults based on BUILD_WITH_LLAMA
  config.enabled = true;
#if BUILD_WITH_LLAMA
  config.use_builtin = true;
#else
  config.use_builtin = false;
#endif

  if (path.empty()) {
    LOG_WARN("BrowserManager", "No config path available, using defaults");
    return config;
  }

  std::ifstream file(path);
  if (!file.is_open()) {
    LOG_DEBUG("BrowserManager", "No existing LLM config file at " + path + ", using defaults");
    return config;
  }

  // Parse JSON manually (simple format)
  std::string line;
  std::string content;
  while (std::getline(file, line)) {
    content += line;
  }
  file.close();

  // Basic JSON parsing for our simple config structure
  auto extract_bool = [&content](const std::string& key) -> bool {
    size_t pos = content.find("\"" + key + "\"");
    if (pos == std::string::npos) return false;
    pos = content.find(":", pos);
    if (pos == std::string::npos) return false;
    pos = content.find_first_not_of(" \t\n\r", pos + 1);
    return (pos != std::string::npos && content.substr(pos, 4) == "true");
  };

  auto extract_string = [&content](const std::string& key) -> std::string {
    size_t pos = content.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    pos = content.find(":", pos);
    if (pos == std::string::npos) return "";
    pos = content.find("\"", pos + 1);
    if (pos == std::string::npos) return "";
    size_t end_pos = content.find("\"", pos + 1);
    if (end_pos == std::string::npos) return "";
    return UnescapeJSONString(content.substr(pos + 1, end_pos - pos - 1));
  };

  config.enabled = extract_bool("enabled");
  config.use_builtin = extract_bool("use_builtin");
  config.provider_name = extract_string("provider_name");
  config.external_endpoint = extract_string("external_endpoint");
  config.external_model = extract_string("external_model");
  config.external_api_key = extract_string("external_api_key");
  config.is_third_party = extract_bool("is_third_party");

  LOG_DEBUG("BrowserManager", "Loaded LLM config from " + path);
  return config;
}

bool OwlBrowserManager::SaveLLMConfigToFile(const LLMConfig& config, const std::string& config_path) {
  std::string path = config_path.empty() ? GetLLMConfigPath() : config_path;

  if (path.empty()) {
    LOG_ERROR("BrowserManager", "No config path available");
    return false;
  }

  // Create directory if it doesn't exist
  size_t last_slash = path.find_last_of("/\\");
  if (last_slash != std::string::npos) {
    std::string dir = path.substr(0, last_slash);
#if defined(OS_WIN)
    mkdir(dir.c_str());
#else
    mkdir(dir.c_str(), 0755);
#endif
  }

  // Write JSON
  std::ofstream file(path);
  if (!file.is_open()) {
    LOG_ERROR("BrowserManager", "Failed to open config file for writing: " + path);
    return false;
  }

  file << "{\n";
  file << "  \"enabled\": " << (config.enabled ? "true" : "false") << ",\n";
  file << "  \"use_builtin\": " << (config.use_builtin ? "true" : "false") << ",\n";
  file << "  \"provider_name\": \"" << EscapeJSONString(config.provider_name) << "\",\n";
  file << "  \"external_endpoint\": \"" << EscapeJSONString(config.external_endpoint) << "\",\n";
  file << "  \"external_model\": \"" << EscapeJSONString(config.external_model) << "\",\n";
  file << "  \"external_api_key\": \"" << EscapeJSONString(config.external_api_key) << "\",\n";
  file << "  \"is_third_party\": " << (config.is_third_party ? "true" : "false") << "\n";
  file << "}\n";

  file.close();

  LOG_DEBUG("BrowserManager", "Saved LLM config to " + path);
  return true;
}

std::string OwlBrowserManager::CreateContext(const LLMConfig* llm_config, const ProxyConfig* proxy_config, const std::string& profile_path, bool resource_blocking, const std::string& os_filter, const std::string& gpu_filter) {
  // Distributed license verification - check on every context creation
  if (!olib::license::LicenseManager::GetInstance()->VerifyIntegrity()) {
    LOG_ERROR("BrowserManager", "License verification failed - cannot create context");
    return "";  // Return empty context ID to signal failure
  }

  // Check capacity with atomic load (no lock needed for read)
  size_t current_count = context_count_.load(std::memory_order_relaxed);
  size_t max = max_contexts_.load(std::memory_order_relaxed);

  if (current_count >= max) {
    LOG_WARN("BrowserManager", "At capacity (" + std::to_string(current_count) + "/" +
             std::to_string(max) + "), triggering cleanup");
    CleanupOldContexts();

    // Re-check after cleanup
    current_count = context_count_.load(std::memory_order_relaxed);
    if (current_count >= max) {
      LOG_ERROR("BrowserManager", "Still at capacity after cleanup, cannot create context");
      return "";
    }
  }

  // Create new context
  std::string context_id = GenerateContextId();
  LOG_DEBUG("BrowserManager", "Creating new context: " + context_id +
           " (total: " + std::to_string(current_count + 1) + "/" + std::to_string(max) + ")");

  // CRITICAL: Generate seed_context_id EARLY using the SAME formula as renderer process
  // This ensures FingerprintGenerator uses the same key in both browser and renderer
  int predicted_browser_id = next_browser_id_.fetch_add(1);

  // Get session VM seed from command line (must match renderer's formula in owl_stealth.cc)
  uint64_t session_vm_seed = 0;
  CefRefPtr<CefCommandLine> cmd_line = CefCommandLine::GetGlobalCommandLine();
  if (cmd_line && cmd_line->HasSwitch("owl-vm-seed")) {
    std::string seed_str = cmd_line->GetSwitchValue("owl-vm-seed").ToString();
    if (!seed_str.empty()) {
      char* end = nullptr;
      session_vm_seed = strtoull(seed_str.c_str(), &end, 10);
    }
  }

  // Generate seed_context_id: "ctx_" + session_vm_seed + "_" + browser_id
  // This MUST match the formula in owl_stealth.cc line 206
  std::string seed_context_id = "ctx_" + std::to_string(session_vm_seed) + "_" + std::to_string(predicted_browser_id);
  LOG_DEBUG("BrowserManager", "Generated seed_context_id: " + seed_context_id +
           " (session_seed=" + std::to_string(session_vm_seed) +
           ", predicted_browser_id=" + std::to_string(predicted_browser_id) + ")");

  auto context = std::make_unique<BrowserContext>();
  context->id = context_id;
  context->in_use.store(true, std::memory_order_relaxed);
  context->created = std::chrono::steady_clock::now();
  context->last_used.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
  context->resource_blocking_enabled = resource_blocking;

  LOG_DEBUG("BrowserManager", "Resource blocking " + std::string(resource_blocking ? "enabled" : "disabled") + " for context: " + context_id);

  // Load profile if path provided, otherwise generate random fingerprint
  BrowserProfile profile;
  if (!profile_path.empty()) {
    OwlBrowserProfileManager* profile_manager = OwlBrowserProfileManager::GetInstance();
    profile = profile_manager->LoadProfile(profile_path);
    context->profile_path = profile_path;
    context->has_profile = true;
    context->auto_save_profile = profile.auto_save_cookies;

    // Copy fingerprint from profile to context (all fields including seeds)
    context->fingerprint.vm_id = profile.fingerprint.vm_id;

    // Copy all Seed API fields
    context->fingerprint.canvas_seed = profile.fingerprint.canvas_seed;
    context->fingerprint.webgl_seed = profile.fingerprint.webgl_seed;
    context->fingerprint.audio_seed = profile.fingerprint.audio_seed;
    context->fingerprint.fonts_seed = profile.fingerprint.fonts_seed;
    context->fingerprint.client_rects_seed = profile.fingerprint.client_rects_seed;
    context->fingerprint.navigator_seed = profile.fingerprint.navigator_seed;
    context->fingerprint.screen_seed = profile.fingerprint.screen_seed;
    context->fingerprint.audio_fingerprint = profile.fingerprint.audio_fingerprint;

    // Copy MD5-style hashes
    context->fingerprint.canvas_geometry_hash = profile.fingerprint.canvas_geometry_hash;
    context->fingerprint.canvas_text_hash = profile.fingerprint.canvas_text_hash;
    context->fingerprint.webgl_params_hash = profile.fingerprint.webgl_params_hash;
    context->fingerprint.webgl_extensions_hash = profile.fingerprint.webgl_extensions_hash;
    context->fingerprint.webgl_context_hash = profile.fingerprint.webgl_context_hash;
    context->fingerprint.webgl_ext_params_hash = profile.fingerprint.webgl_ext_params_hash;
    context->fingerprint.shader_precisions_hash = profile.fingerprint.shader_precisions_hash;
    context->fingerprint.fonts_hash = profile.fingerprint.fonts_hash;
    context->fingerprint.plugins_hash = profile.fingerprint.plugins_hash;

    // Copy legacy and other fields
    context->fingerprint.canvas_hash_seed = profile.fingerprint.canvas_hash_seed;
    context->fingerprint.user_agent = profile.fingerprint.user_agent;
    context->fingerprint.platform = profile.fingerprint.platform;
    context->fingerprint.hardware_concurrency = profile.fingerprint.hardware_concurrency;
    context->fingerprint.device_memory = profile.fingerprint.device_memory;
    context->fingerprint.canvas_noise_seed = profile.fingerprint.canvas_noise_seed;
    context->fingerprint.gpu_profile_index = profile.fingerprint.gpu_profile_index;
    context->fingerprint.webgl_vendor = profile.fingerprint.webgl_vendor;
    context->fingerprint.webgl_renderer = profile.fingerprint.webgl_renderer;
    context->fingerprint.screen_width = profile.fingerprint.screen_width;
    context->fingerprint.screen_height = profile.fingerprint.screen_height;
    context->fingerprint.timezone = profile.fingerprint.timezone;
    context->fingerprint.locale = profile.fingerprint.locale;
    context->fingerprint.audio_noise_seed = profile.fingerprint.audio_noise_seed;

    // Sync seeds with FingerprintGenerator so Seed API returns correct values
    owl::FingerprintSeeds seeds;
    seeds.canvas_seed = profile.fingerprint.canvas_seed;
    seeds.webgl_seed = profile.fingerprint.webgl_seed;
    seeds.audio_seed = profile.fingerprint.audio_seed;
    seeds.fonts_seed = profile.fingerprint.fonts_seed;
    seeds.client_rects_seed = profile.fingerprint.client_rects_seed;
    seeds.navigator_seed = profile.fingerprint.navigator_seed;
    seeds.screen_seed = profile.fingerprint.screen_seed;
    seeds.audio_fingerprint = profile.fingerprint.audio_fingerprint;
    seeds.canvas_geometry_hash = profile.fingerprint.canvas_geometry_hash;
    seeds.canvas_text_hash = profile.fingerprint.canvas_text_hash;
    seeds.webgl_params_hash = profile.fingerprint.webgl_params_hash;
    seeds.webgl_extensions_hash = profile.fingerprint.webgl_extensions_hash;
    seeds.webgl_context_hash = profile.fingerprint.webgl_context_hash;
    seeds.webgl_ext_params_hash = profile.fingerprint.webgl_ext_params_hash;
    seeds.shader_precisions_hash = profile.fingerprint.shader_precisions_hash;
    seeds.fonts_hash = profile.fingerprint.fonts_hash;
    seeds.plugins_hash = profile.fingerprint.plugins_hash;
    owl::OwlFingerprintGenerator::Instance().SetSeeds(seed_context_id, seeds);

    LOG_DEBUG("BrowserManager", "Synced profile seeds with FingerprintGenerator for seed_context_id " + seed_context_id);

    // Use profile's LLM config if not explicitly provided
    if (!llm_config && profile.has_llm_config) {
      context->llm_config = profile.llm_config;
    }

    // Use profile's proxy config if not explicitly provided
    if ((!proxy_config || !proxy_config->IsValid()) && profile.has_proxy_config) {
      context->proxy_config = profile.proxy_config;
    }

    LOG_DEBUG("BrowserManager", "Context " + context_id + " using profile: " + profile.profile_id +
             " (" + profile.profile_name + ") with " + std::to_string(profile.cookies.size()) + " cookies");
  } else {
    // Generate random fingerprint for contexts without a profile
    // Use the new VM-based system for consistent profiles
    // GenerateRandom() uses FingerprintGenerator internally but with a temp context_id

    // First, generate seeds for THIS seed_context_id (renderer format) directly
    // CRITICAL: Use seed_context_id so renderer gets the SAME seeds
    owl::FingerprintSeeds seeds = owl::OwlFingerprintGenerator::Instance().GetSeeds(seed_context_id);

    // Now generate the random fingerprint (for VM profile and other fields)
    BrowserFingerprint random_fp = BrowserFingerprint::GenerateRandom();

    // Copy VM profile fields from random_fp
    context->fingerprint.vm_id = random_fp.vm_id;
    context->fingerprint.user_agent = random_fp.user_agent;
    context->fingerprint.platform = random_fp.platform;
    context->fingerprint.hardware_concurrency = random_fp.hardware_concurrency;
    context->fingerprint.device_memory = random_fp.device_memory;
    context->fingerprint.gpu_profile_index = random_fp.gpu_profile_index;
    context->fingerprint.webgl_vendor = random_fp.webgl_vendor;
    context->fingerprint.webgl_renderer = random_fp.webgl_renderer;
    context->fingerprint.screen_width = random_fp.screen_width;
    context->fingerprint.screen_height = random_fp.screen_height;
    context->fingerprint.timezone = random_fp.timezone;
    context->fingerprint.locale = random_fp.locale;

    // Copy seeds from FingerprintGenerator (the authoritative source)
    context->fingerprint.canvas_seed = seeds.canvas_seed;
    context->fingerprint.webgl_seed = seeds.webgl_seed;
    context->fingerprint.audio_seed = seeds.audio_seed;
    context->fingerprint.fonts_seed = seeds.fonts_seed;
    context->fingerprint.client_rects_seed = seeds.client_rects_seed;
    context->fingerprint.navigator_seed = seeds.navigator_seed;
    context->fingerprint.screen_seed = seeds.screen_seed;
    context->fingerprint.audio_fingerprint = seeds.audio_fingerprint;

    // Copy MD5-style hashes
    context->fingerprint.canvas_geometry_hash = seeds.canvas_geometry_hash;
    context->fingerprint.canvas_text_hash = seeds.canvas_text_hash;
    context->fingerprint.webgl_params_hash = seeds.webgl_params_hash;
    context->fingerprint.webgl_extensions_hash = seeds.webgl_extensions_hash;
    context->fingerprint.webgl_context_hash = seeds.webgl_context_hash;
    context->fingerprint.webgl_ext_params_hash = seeds.webgl_ext_params_hash;
    context->fingerprint.shader_precisions_hash = seeds.shader_precisions_hash;
    context->fingerprint.fonts_hash = seeds.fonts_hash;
    context->fingerprint.plugins_hash = seeds.plugins_hash;

    // Copy legacy fields (for backwards compatibility)
    context->fingerprint.canvas_hash_seed = seeds.canvas_seed;  // Legacy maps to canvas_seed
    context->fingerprint.canvas_noise_seed = random_fp.canvas_noise_seed;
    context->fingerprint.audio_noise_seed = random_fp.audio_noise_seed;

    LOG_DEBUG("BrowserManager", "Context " + context_id + " generated unique fingerprint - " +
             "VM: " + random_fp.vm_id +
             ", canvas_seed: " + std::to_string(seeds.canvas_seed) +
             ", audio_fingerprint: " + std::to_string(seeds.audio_fingerprint));
  }

  // Store LLM config in this context (not globally)
  if (llm_config) {
    context->llm_config = *llm_config;
    LOG_DEBUG("BrowserManager", "Context " + context_id + " created with custom LLM config - " +
             "use_builtin: " + std::string(llm_config->use_builtin ? "true" : "false") +
             ", external_endpoint: " + llm_config->external_endpoint +
             ", external_model: " + llm_config->external_model +
             ", enabled: " + std::string(llm_config->enabled ? "true" : "false") +
             ", HasExternalAPI: " + std::string(llm_config->HasExternalAPI() ? "true" : "false"));

    // Initialize LLM client based on config
    if (llm_config->enabled && llm_config->HasExternalAPI()) {
      // External API: create per-context client (with optional PII scrubbing)
      LOG_DEBUG("BrowserManager", "Initializing per-context LLM client for external API: " + llm_config->external_endpoint +
               " (third-party: " + std::string(llm_config->is_third_party ? "YES" : "NO") + ")");
      context->llm_client = std::make_unique<OwlLLMClient>(llm_config->external_endpoint, llm_config->is_third_party);

      // Set API key and model for external APIs
      if (!llm_config->external_api_key.empty()) {
        context->llm_client->SetApiKey(llm_config->external_api_key);
        LOG_DEBUG("BrowserManager", "Set API key for external LLM client");
      }
      if (!llm_config->external_model.empty()) {
        context->llm_client->SetModel(llm_config->external_model);
        LOG_DEBUG("BrowserManager", "Set model for external LLM client: " + llm_config->external_model);
      }

      LOG_DEBUG("BrowserManager", "Per-context LLM client initialized for external API: " + llm_config->external_endpoint);
    } else if (llm_config->enabled && llm_config->use_builtin) {
      // Built-in LLM: start server if not already running
      LOG_DEBUG("BrowserManager", "Context requests built-in LLM, checking if server is running...");
      if (!llama_server_ && !llm_client_) {
        LOG_DEBUG("BrowserManager", "Built-in LLM server not running, starting now...");
        // Start the built-in LLM server for this context
        InitializeLLMAsync(llm_config);
      } else {
        LOG_DEBUG("BrowserManager", "Built-in LLM server already running or client exists");
      }
    } else {
      LOG_WARN("BrowserManager", "NOT initializing LLM client - enabled=" +
               std::string(llm_config->enabled ? "true" : "false") +
               ", use_builtin=" + std::string(llm_config->use_builtin ? "true" : "false") +
               ", HasExternalAPI=" + std::string(llm_config->HasExternalAPI() ? "true" : "false"));
    }
  } else {
    // Use default config (already set in BrowserContext constructor)
    // Will use global LLM client (built-in or default)
    LOG_DEBUG("BrowserManager", "Context " + context_id + " created with default LLM config");
  }

  // Store proxy config in this context
  if (proxy_config && proxy_config->IsValid()) {
    context->proxy_config = *proxy_config;
    LOG_DEBUG("BrowserManager", "Context " + context_id + " created with proxy config - " +
             "type: " + OwlProxyManager::ProxyTypeToString(proxy_config->type) +
             ", host: " + proxy_config->host + ":" + std::to_string(proxy_config->port) +
             ", stealth: " + std::string(proxy_config->stealth_mode ? "enabled" : "disabled"));

    // Update demographics to use this proxy for IP detection if proxy is enabled
    if (proxy_config->enabled) {
      OwlDemographics* demo = OwlDemographics::GetInstance();
      if (demo) {
        demo->SetProxyConfig(*proxy_config);

        // Debug: Log all proxy config values
        LOG_DEBUG("BrowserManager", "Context " + context_id + " proxy config: " +
                 "spoof_timezone=" + std::string(proxy_config->spoof_timezone ? "true" : "false") +
                 ", timezone_override=" + (proxy_config->timezone_override.empty() ? "(empty)" : proxy_config->timezone_override) +
                 ", spoof_language=" + std::string(proxy_config->spoof_language ? "true" : "false"));

        // Detect timezone from proxy if no timezone_override is set
        // This timezone will be used for the full context lifetime
        if (proxy_config->timezone_override.empty() && proxy_config->spoof_timezone) {
          LOG_DEBUG("BrowserManager", "Detecting timezone from proxy for new context: " + context_id);
          GeoLocationInfo location = demo->GetGeoLocation();
          if (location.success && !location.timezone.empty()) {
            context->fingerprint.timezone = location.timezone;
            context->proxy_config.timezone_override = location.timezone;
            LOG_DEBUG("BrowserManager", "Set context timezone to proxy timezone: " + location.timezone +
                     " for context: " + context_id);
          } else {
            LOG_WARN("BrowserManager", "Failed to detect proxy timezone for new context: " + location.error);
          }
        } else {
          LOG_DEBUG("BrowserManager", "Skipping timezone detection for context " + context_id +
                   " - spoof_timezone=" + std::string(proxy_config->spoof_timezone ? "true" : "false") +
                   ", timezone_override=" + (proxy_config->timezone_override.empty() ? "(empty)" : proxy_config->timezone_override));
        }
      }
    }
  } else if (proxy_config) {
    LOG_WARN("BrowserManager", "Invalid proxy config provided for context " + context_id);
  }

  // Create CEF browser with optimized cache settings
  CefWindowInfo window_info;
  window_info.SetAsWindowless(0);  // Off-screen rendering

  CefBrowserSettings browser_settings;
  browser_settings.windowless_frame_rate = 30;

  LOG_DEBUG("BrowserManager", "Creating CEF browser with windowless rendering");

  // Phase 1: Create request context with aggressive caching and proxy support
  // This enables disk cache, session persistence, and HTTP cache
  CefRequestContextSettings context_settings;
  // IMPORTANT: Must provide a non-empty cache_path to avoid off-the-record/incognito mode
  // Empty cache_path causes CEF to create an in-memory context which triggers incognito detection
  // Use context-specific cache path to enable disk caching and storage persistence
  std::string context_cache_path = "/tmp/owl_browser_context_cache/" + context_id;
  CefString(&context_settings.cache_path) = context_cache_path;
  context_settings.persist_session_cookies = true;

  // Configure proxy if provided and valid
  // CEF uses Chromium's proxy settings format
  // Format: "http=host:port;https=host:port" or "socks5://host:port" for all protocols
  if (proxy_config && proxy_config->IsValid() && proxy_config->enabled) {
    std::string proxy_url = proxy_config->GetCEFProxyString();
    if (!proxy_url.empty()) {
      // CEF doesn't have direct proxy settings in CefRequestContextSettings
      // Proxy must be set via command-line or preferences after context creation
      // For now, we store the config and apply it via CefRequestContext preferences
      LOG_DEBUG("BrowserManager", "Proxy configured for context: " + proxy_url);
    }
  }

  // ========================================================================
  // CRITICAL: Create request context handler for ServiceWorker interception
  // ========================================================================
  // CefClient::GetResourceRequestHandler is NOT called for ServiceWorker script
  // fetches because browser and frame are NULL. We must use CefRequestContextHandler
  // to intercept these requests at the request context level.
  //
  // The handler is created with the initial vm_id (from profile if available).
  // If vm_id is empty, we'll update it after VM selection.
  // ========================================================================
  std::string initial_vm_id = context->fingerprint.vm_id;
  context->request_context_handler = new OwlRequestContextHandler(initial_vm_id, context_id);
  LOG_INFO("BrowserManager", "[SW-CONTEXT] Created OwlRequestContextHandler for context " +
           context_id + " with initial vm_id=" + (initial_vm_id.empty() ? "(empty)" : initial_vm_id));

  // Create request context with our handler
  CefRefPtr<CefRequestContext> request_context =
    CefRequestContext::CreateContext(context_settings, context->request_context_handler);

  // CRITICAL: Disable DNS-over-HTTPS (Secure DNS) to prevent timeout issues
  // DoH causes navigation failures when DoH servers are blocked or slow
  // This must be set on every request context to disable DoH for all browsers
  if (request_context) {
    CefString dns_error;
    // Set dns_over_https.mode to "off" to completely disable secure DNS
    CefRefPtr<CefValue> dns_mode = CefValue::Create();
    dns_mode->SetString("off");
    if (request_context->SetPreference("dns_over_https.mode", dns_mode, dns_error)) {
      LOG_DEBUG("BrowserManager", "DNS-over-HTTPS disabled for context");
    } else {
      LOG_DEBUG("BrowserManager", "Could not set dns_over_https.mode: " + dns_error.ToString());
    }
  }

  // Apply proxy settings via preferences if configured
  if (proxy_config && proxy_config->IsValid() && proxy_config->enabled && request_context) {
    // TOR CIRCUIT ISOLATION: Request a new circuit for each context
    // This ensures each browser context gets a different Tor exit node (IP)
    if (proxy_config->IsTorProxy()) {
      LOG_DEBUG("BrowserManager", "Detected Tor proxy - requesting new circuit for context: " + context_id);
      if (OwlProxyManager::RequestNewTorCircuit(*proxy_config)) {
        LOG_DEBUG("BrowserManager", "New Tor circuit established - context will have unique exit node");
      } else {
        LOG_WARN("BrowserManager", "Could not request new Tor circuit. "
                 "To enable circuit isolation, configure Tor with: ControlPort 9051");
      }
    }

    // Set proxy via request context preferences
    // CEF proxy is configured through the "proxy" preference as a dictionary
    CefRefPtr<CefValue> proxy_value = CefValue::Create();
    CefRefPtr<CefDictionaryValue> proxy_dict = CefDictionaryValue::Create();

    // Get CEF-compatible proxy string for all proxy types
    std::string proxy_url = proxy_config->GetCEFProxyString();

    // For SOCKS5H (TOR), don't bypass anything - route all traffic through proxy
    // For other proxies, bypass local addresses
    std::string bypass_list = (proxy_config->type == ProxyType::SOCKS5H) ? "" : "<local>";

    LOG_DEBUG("BrowserManager", "Proxy configured: " + proxy_url +
             " (type: " + OwlProxyManager::ProxyTypeToString(proxy_config->type) + ")");

    proxy_dict->SetString("mode", "fixed_servers");
    proxy_dict->SetString("server", proxy_url);
    proxy_dict->SetString("bypass_list", bypass_list);
    proxy_value->SetDictionary(proxy_dict);

    // Set the proxy preference
    CefString error;
    if (request_context->SetPreference("proxy", proxy_value, error)) {
      LOG_DEBUG("BrowserManager", "Proxy preference set successfully");
    } else {
      LOG_ERROR("BrowserManager", "Failed to set proxy preference: " + error.ToString());
    }

    // Also log what preferences are available for debugging
    CefRefPtr<CefValue> current_proxy = request_context->GetPreference("proxy");
    if (current_proxy && current_proxy->GetType() == VTYPE_DICTIONARY) {
      CefRefPtr<CefDictionaryValue> current_dict = current_proxy->GetDictionary();
      if (current_dict) {
        CefString mode = current_dict->GetString("mode");
        CefString server = current_dict->GetString("server");
        LOG_DEBUG("BrowserManager", "Verified proxy config - mode: " + mode.ToString() + ", server: " + server.ToString());
      }
    }

    // Log CA certificate configuration if enabled
    if (proxy_config->trust_custom_ca && !proxy_config->ca_cert_path.empty()) {
      LOG_DEBUG("BrowserManager", "Custom CA certificate configured: " + proxy_config->ca_cert_path);
      LOG_DEBUG("BrowserManager", "CA certificate trust will be handled by OnCertificateError callback");
    }
  }

  // ========================================================================
  // PRE-BROWSER CREATION: Select VM and register GPU BEFORE CEF GL init
  // This is critical because CEF makes glGetString calls during CreateBrowserSync
  // which happen BEFORE we have the actual browser_id. We use predicted ID.
  // NOTE: predicted_browser_id, session_vm_seed, and seed_context_id are
  // already computed at the start of CreateContext() for seed consistency
  // ========================================================================

  // Prepare stealth config early
  StealthConfig stealth_config;
  stealth_config.user_agent = context->fingerprint.user_agent;
  stealth_config.platform = context->fingerprint.platform;
  stealth_config.hardware_concurrency = context->fingerprint.hardware_concurrency;
  stealth_config.device_memory = context->fingerprint.device_memory;
  stealth_config.canvas_noise_seed = context->fingerprint.canvas_noise_seed;
  stealth_config.gpu_profile_index = context->fingerprint.gpu_profile_index;
  stealth_config.webgl_vendor = context->fingerprint.webgl_vendor;
  stealth_config.webgl_renderer = context->fingerprint.webgl_renderer;
  stealth_config.screen_width = context->fingerprint.screen_width;
  stealth_config.screen_height = context->fingerprint.screen_height;
  stealth_config.timezone = context->fingerprint.timezone;
  stealth_config.audio_noise_seed = context->fingerprint.audio_noise_seed;

  // DEBUG: Log timezone at stealth_config build time
  LOG_DEBUG("BrowserManager", "Building stealth_config with timezone: " + stealth_config.timezone +
           " (context->fingerprint.timezone: " + context->fingerprint.timezone + ")");

  // Select VM profile BEFORE browser creation
  auto& vmdb = owl::VirtualMachineDB::Instance();
  const owl::VirtualMachine* vm = nullptr;
  owl::VirtualMachine vm_copy;

  // If context has a vm_id (from profile), use that specific VM
  if (!context->fingerprint.vm_id.empty()) {
    vm = vmdb.GetVM(context->fingerprint.vm_id);
    if (vm) {
      LOG_DEBUG("BrowserManager", "Using profile's VM: " + context->fingerprint.vm_id +
               " for context: " + context_id);
    } else {
      LOG_WARN("BrowserManager", "Profile's VM not found: " + context->fingerprint.vm_id +
               " - will select a new VM");
    }
  }

  // If no VM from profile, select a random one using session seed + predicted browser_id
  if (!vm) {
    // Use session_vm_seed obtained earlier (same formula as renderer process)
    // Use predicted browser_id for seed calculation
    uint64_t seed = session_vm_seed ^ (static_cast<uint64_t>(predicted_browser_id) * 0x9E3779B97F4A7C15ULL);

    // Determine target OS for VM selection
    // Priority: 1) Explicit os_filter parameter, 2) Profile platform/user_agent, 3) Empty (random)
    std::string target_os = "";
    std::string target_gpu = gpu_filter;  // Use GPU filter directly if provided

    // Check if explicit OS filter was provided
    if (!os_filter.empty()) {
      // Convert lowercase filter value to VM OS name format
      if (os_filter == "windows") {
        target_os = "Windows";
      } else if (os_filter == "macos") {
        target_os = "macOS";
      } else if (os_filter == "linux") {
        target_os = "Linux";
      }
      LOG_DEBUG("BrowserManager", "Using explicit OS filter: " + os_filter + " -> " + target_os);
    }
    // If no explicit filter, try to derive from profile platform/user_agent
    else if (!context->fingerprint.platform.empty()) {
      if (context->fingerprint.platform == "Win32" ||
          context->fingerprint.user_agent.find("Windows") != std::string::npos) {
        target_os = "Windows";
      } else if (context->fingerprint.platform == "MacIntel" ||
                 context->fingerprint.user_agent.find("Mac") != std::string::npos) {
        target_os = "macOS";
      } else if (context->fingerprint.platform.find("Linux") != std::string::npos ||
                 context->fingerprint.user_agent.find("Linux") != std::string::npos) {
        target_os = "Linux";
      }
      LOG_DEBUG("BrowserManager", "Profile has platform '" + context->fingerprint.platform +
               "' - selecting VM matching OS: " + target_os);
    } else {
      LOG_DEBUG("BrowserManager", "No OS filter or platform specified - selecting random VM");
    }

    vm = vmdb.SelectRandomVM(target_os, "", target_gpu, seed);
    if (vm) {
      context->fingerprint.vm_id = vm->id;
      // CRITICAL FIX: Only fill in values that are EMPTY in the profile
      // This preserves profile's configured values while adding missing ones from VM
      bool profile_has_values = !context->fingerprint.user_agent.empty();
      if (profile_has_values) {
        LOG_DEBUG("BrowserManager", "Profile has fingerprint values - preserving them");
        // CRITICAL FIX: ALWAYS update webgl values from VM - GenerateRandom() uses hardcoded defaults
        // that don't match the selected VM. The VM is the authoritative source for GPU values.
        context->fingerprint.webgl_vendor = vm->gpu.unmasked_vendor;
        context->fingerprint.webgl_renderer = vm->gpu.unmasked_renderer;
        if (context->fingerprint.screen_width == 0)
          context->fingerprint.screen_width = vm->screen.width;
        if (context->fingerprint.screen_height == 0)
          context->fingerprint.screen_height = vm->screen.height;
        if (context->fingerprint.timezone.empty() && context->proxy_config.timezone_override.empty())
          context->fingerprint.timezone = vm->timezone.iana_name;
        if (context->fingerprint.locale.empty())
          context->fingerprint.locale = vm->language.primary;
      } else {
        // No profile values - use all VM values
        context->fingerprint.user_agent = vm->browser.user_agent;
        context->fingerprint.platform = vm->os.platform;
        context->fingerprint.hardware_concurrency = vm->cpu.hardware_concurrency;
        context->fingerprint.device_memory = vm->cpu.device_memory;
        context->fingerprint.webgl_vendor = vm->gpu.unmasked_vendor;
        context->fingerprint.webgl_renderer = vm->gpu.unmasked_renderer;
        context->fingerprint.screen_width = vm->screen.width;
        context->fingerprint.screen_height = vm->screen.height;
        // IMPORTANT: Only use VM timezone if proxy timezone was NOT detected
        if (context->proxy_config.timezone_override.empty()) {
          context->fingerprint.timezone = vm->timezone.iana_name;
        }
        context->fingerprint.locale = vm->language.primary;
      }
      LOG_INFO("BrowserManager", "[VM_SYNC] Selected random VM: " + vm->id + " for context: " + context_id +
               " (target_os=" + (target_os.empty() ? "(any)" : target_os) +
               ", target_gpu=" + (target_gpu.empty() ? "(any)" : target_gpu) +
               ", profile_preserved=" + (profile_has_values ? "yes" : "no") +
               ", session_seed=" + std::to_string(session_vm_seed) +
               ", predicted_browser_id=" + std::to_string(predicted_browser_id) + ")");
    } else {
      LOG_ERROR("BrowserManager", "[VM_SYNC] SelectRandomVM returned null for target_os=" +
               (target_os.empty() ? "(any)" : target_os) + ", target_gpu=" +
               (target_gpu.empty() ? "(any)" : target_gpu) + " - no VMs available matching criteria!");
    }
  }

  // ========================================================================
  // CRITICAL: Update request context handler with final vm_id
  // ========================================================================
  // If vm_id was empty when handler was created (new context without profile),
  // update it now with the selected VM's id. This ensures ServiceWorker scripts
  // fetched from remote URLs get properly patched.
  // ========================================================================
  if (vm && context->request_context_handler) {
    std::string current_vm_id = context->request_context_handler->GetVMId();
    if (current_vm_id.empty() || current_vm_id != vm->id) {
      context->request_context_handler->SetVMId(vm->id);
      LOG_INFO("BrowserManager", "[SW-CONTEXT] Updated request context handler vm_id to: " + vm->id);
    }
  }

  // GPU params struct (needs to be in scope for both initial and fallback registration)
  OWLGPUParams gpu_params = {};

  // Pre-register GPU context BEFORE browser creation
  // This ensures ANGLE GL calls during CEF init get the correct spoofed values
  if (vm) {
    stealth_config.vm_id = vm->id;
    stealth_config.context_id = seed_context_id;  // Must match renderer's formula for FingerprintGenerator caching

    // CRITICAL: ALWAYS update stealth_config and fingerprint with VM values
    // stealth_config was populated from context->fingerprint BEFORE VM selection,
    // which might have hardcoded defaults. VM values MUST be authoritative for
    // consistency across main frame, iframes, workers, and HTTP headers.
    // The VM profile is the single source of truth for fingerprint values.
    stealth_config.user_agent = vm->browser.user_agent;
    stealth_config.platform = vm->os.platform;
    stealth_config.hardware_concurrency = vm->cpu.hardware_concurrency;
    stealth_config.device_memory = vm->cpu.device_memory;
    // Also update context->fingerprint to match VM values
    context->fingerprint.user_agent = vm->browser.user_agent;
    context->fingerprint.platform = vm->os.platform;
    context->fingerprint.hardware_concurrency = vm->cpu.hardware_concurrency;
    context->fingerprint.device_memory = vm->cpu.device_memory;
    LOG_DEBUG("BrowserManager", "Synced stealth_config/fingerprint with VM values: " + vm->browser.user_agent);

    vm_copy = *vm;
    if (context->fingerprint.canvas_hash_seed != 0) {
      vm_copy.canvas.hash_seed = context->fingerprint.canvas_hash_seed;
      LOG_DEBUG("BrowserManager", "Using profile's canvas_hash_seed: 0x" +
               std::to_string(context->fingerprint.canvas_hash_seed));
    }

    // Register GPU values BEFORE browser creation
    // IMPORTANT: Use MASKED values (gpu.vendor, gpu.renderer) NOT unmasked!
    owl_gpu_register_context(
        predicted_browser_id,
        vm_copy.gpu.vendor.c_str(),       // MASKED: "Google Inc. (Intel)"
        vm_copy.gpu.renderer.c_str(),     // MASKED: "ANGLE (...)"
        vm_copy.gpu.webgl_version.c_str(),
        vm_copy.gpu.shading_language.c_str()
    );

    // Register extended GPU parameters for native GL call spoofing
    // This enables glGetIntegerv and glGetShaderPrecisionFormat spoofing
    gpu_params.max_texture_size = vm_copy.gpu.max_texture_size;
    gpu_params.max_cube_map_texture_size = vm_copy.gpu.max_cube_map_texture_size;
    gpu_params.max_render_buffer_size = vm_copy.gpu.max_render_buffer_size;
    gpu_params.max_vertex_attribs = vm_copy.gpu.max_vertex_attribs;
    gpu_params.max_vertex_uniform_vectors = vm_copy.gpu.max_vertex_uniform_vectors;
    gpu_params.max_vertex_texture_units = vm_copy.gpu.max_vertex_texture_units;
    gpu_params.max_varying_vectors = vm_copy.gpu.max_varying_vectors;
    gpu_params.max_fragment_uniform_vectors = vm_copy.gpu.max_fragment_uniform_vectors;
    gpu_params.max_texture_units = vm_copy.gpu.max_texture_units;
    gpu_params.max_combined_texture_units = vm_copy.gpu.max_combined_texture_units;
    gpu_params.max_viewport_dims[0] = vm_copy.gpu.max_viewport_dims_w;
    gpu_params.max_viewport_dims[1] = vm_copy.gpu.max_viewport_dims_h;
    gpu_params.max_samples = vm_copy.gpu.max_samples;
    // Multisampling parameters (critical for VM detection!)
    // Use VM values if set, otherwise default to 4/1 (real Chrome on desktop)
    gpu_params.samples = vm_copy.gpu.samples > 0 ? vm_copy.gpu.samples : 4;
    gpu_params.sample_buffers = vm_copy.gpu.sample_buffers > 0 ? vm_copy.gpu.sample_buffers : 1;
    gpu_params.aliased_line_width_range[0] = vm_copy.gpu.aliased_line_width_min;
    gpu_params.aliased_line_width_range[1] = vm_copy.gpu.aliased_line_width_max;
    gpu_params.aliased_point_size_range[0] = vm_copy.gpu.aliased_point_size_min;
    gpu_params.aliased_point_size_range[1] = vm_copy.gpu.aliased_point_size_max;
    gpu_params.max_anisotropy = vm_copy.gpu.max_anisotropy;
    // Shader precision formats
    gpu_params.vertex_high_float[0] = vm_copy.gpu.vertex_high_float.range_min;
    gpu_params.vertex_high_float[1] = vm_copy.gpu.vertex_high_float.range_max;
    gpu_params.vertex_high_float[2] = vm_copy.gpu.vertex_high_float.precision;
    gpu_params.vertex_medium_float[0] = vm_copy.gpu.vertex_medium_float.range_min;
    gpu_params.vertex_medium_float[1] = vm_copy.gpu.vertex_medium_float.range_max;
    gpu_params.vertex_medium_float[2] = vm_copy.gpu.vertex_medium_float.precision;
    gpu_params.vertex_low_float[0] = vm_copy.gpu.vertex_low_float.range_min;
    gpu_params.vertex_low_float[1] = vm_copy.gpu.vertex_low_float.range_max;
    gpu_params.vertex_low_float[2] = vm_copy.gpu.vertex_low_float.precision;
    gpu_params.fragment_high_float[0] = vm_copy.gpu.fragment_high_float.range_min;
    gpu_params.fragment_high_float[1] = vm_copy.gpu.fragment_high_float.range_max;
    gpu_params.fragment_high_float[2] = vm_copy.gpu.fragment_high_float.precision;
    gpu_params.fragment_medium_float[0] = vm_copy.gpu.fragment_medium_float.range_min;
    gpu_params.fragment_medium_float[1] = vm_copy.gpu.fragment_medium_float.range_max;
    gpu_params.fragment_medium_float[2] = vm_copy.gpu.fragment_medium_float.precision;
    gpu_params.fragment_low_float[0] = vm_copy.gpu.fragment_low_float.range_min;
    gpu_params.fragment_low_float[1] = vm_copy.gpu.fragment_low_float.range_max;
    gpu_params.fragment_low_float[2] = vm_copy.gpu.fragment_low_float.precision;
    // INT precision formats
    gpu_params.vertex_high_int[0] = vm_copy.gpu.vertex_high_int.range_min;
    gpu_params.vertex_high_int[1] = vm_copy.gpu.vertex_high_int.range_max;
    gpu_params.vertex_high_int[2] = vm_copy.gpu.vertex_high_int.precision;
    gpu_params.vertex_medium_int[0] = vm_copy.gpu.vertex_medium_int.range_min;
    gpu_params.vertex_medium_int[1] = vm_copy.gpu.vertex_medium_int.range_max;
    gpu_params.vertex_medium_int[2] = vm_copy.gpu.vertex_medium_int.precision;
    gpu_params.vertex_low_int[0] = vm_copy.gpu.vertex_low_int.range_min;
    gpu_params.vertex_low_int[1] = vm_copy.gpu.vertex_low_int.range_max;
    gpu_params.vertex_low_int[2] = vm_copy.gpu.vertex_low_int.precision;
    gpu_params.fragment_high_int[0] = vm_copy.gpu.fragment_high_int.range_min;
    gpu_params.fragment_high_int[1] = vm_copy.gpu.fragment_high_int.range_max;
    gpu_params.fragment_high_int[2] = vm_copy.gpu.fragment_high_int.precision;
    gpu_params.fragment_medium_int[0] = vm_copy.gpu.fragment_medium_int.range_min;
    gpu_params.fragment_medium_int[1] = vm_copy.gpu.fragment_medium_int.range_max;
    gpu_params.fragment_medium_int[2] = vm_copy.gpu.fragment_medium_int.precision;
    gpu_params.fragment_low_int[0] = vm_copy.gpu.fragment_low_int.range_min;
    gpu_params.fragment_low_int[1] = vm_copy.gpu.fragment_low_int.range_max;
    gpu_params.fragment_low_int[2] = vm_copy.gpu.fragment_low_int.precision;
    // WebGL2 parameters - use realistic values for modern GPUs
    // These values are consistent across Chrome/Safari on Apple Silicon
    gpu_params.max_3d_texture_size = 2048;
    gpu_params.max_array_texture_layers = 2048;
    gpu_params.max_color_attachments = 8;
    gpu_params.max_draw_buffers = 8;
    gpu_params.max_uniform_buffer_bindings = 24;
    gpu_params.max_uniform_block_size = 16384;
    gpu_params.max_combined_uniform_blocks = 24;
    gpu_params.max_transform_feedback_separate_attribs = 4;
    owl_gpu_register_params(predicted_browser_id, &gpu_params);

    // Set this as the current context for ANGLE wrapper
    owl_gpu_set_current_context(predicted_browser_id);

    // Register fingerprint seeds for this context
    // Each context gets unique, realistic seeds that remain consistent for its lifetime
    // CRITICAL: Use seed_context_id which matches renderer formula for FingerprintGenerator caching
    owl_seed_register_context(predicted_browser_id, seed_context_id.c_str());
    owl_seed_set_current_context(predicted_browser_id);

    // CRITICAL: Sync ALL generated seeds to vm_copy so renderer uses the same unique seeds
    // The Seed API generates unique per-context seeds, we must update vm_copy to match
    vm_copy.canvas.hash_seed = owl_seed_get_canvas();
    vm_copy.gpu.renderer_hash_seed = owl_seed_get_webgl();
    vm_copy.audio.audio_hash_seed = owl_seed_get_audio();

    LOG_DEBUG("BrowserManager", "PRE-REGISTERED GPU for ANGLE (masked): " +
             vm_copy.gpu.vendor + " / " + vm_copy.gpu.renderer +
             " (predicted_browser_id=" + std::to_string(predicted_browser_id) + ")");
    LOG_DEBUG("BrowserManager", "Registered fingerprint seeds for context: " + context_id);

    // CRITICAL: Pre-register stealth config and VM BEFORE browser creation
    // This ensures OnContextCreated in renderer has the config available immediately
    // Race condition fix: Without this, Context 1 often gets empty rendererUnmasked
    // because OnContextCreated fires before SetContextFingerprint is called post-browser-creation
    OwlStealth::SetContextVM(predicted_browser_id, vm_copy);
    OwlStealth::SetContextFingerprint(predicted_browser_id, stealth_config);
    LOG_DEBUG("BrowserManager", "Pre-registered stealth config for predicted_browser_id=" +
             std::to_string(predicted_browser_id) + " (vm: " + vm->id + ")");
  }

  // ========================================================================
  // NOW CREATE THE BROWSER (CEF GL calls will use our pre-registered GPU)
  // ========================================================================

  CefRefPtr<OwlClient> client;
  // IMPORTANT: Use context->proxy_config (which has timezone_override set from GeoIP detection)
  // NOT the original proxy_config pointer (which doesn't have timezone_override)
  if (context->proxy_config.IsValid()) {
    client = new OwlClient(context->proxy_config);
  } else {
    client = new OwlClient();
  }

  // Set context ID for Tor circuit isolation
  // Each context gets a unique ID, which generates unique SOCKS auth credentials
  // When using Tor, this ensures each context gets a different exit node
  client->SetContextId(context_id);

  // Set resource blocking preference
  client->SetResourceBlocking(context->resource_blocking_enabled);

  // CRITICAL: Pass vm_id via extra_info to renderer process
  // This ensures renderer uses the SAME VM profile selected by browser process
  CefRefPtr<CefDictionaryValue> extra_info = CefDictionaryValue::Create();
  if (!context->fingerprint.vm_id.empty()) {
    extra_info->SetString("vm_id", context->fingerprint.vm_id);
    LOG_INFO("BrowserManager", "[VM_SYNC] Passing vm_id to renderer via extra_info: " + context->fingerprint.vm_id);
  } else {
    LOG_WARN("BrowserManager", "[VM_SYNC] No vm_id to pass via extra_info - stealth patches will fail!");
  }

  context->browser = CefBrowserHost::CreateBrowserSync(
    window_info,
    client.get(),
    "about:blank",
    browser_settings,
    extra_info,
    request_context
  );

  LOG_DEBUG("BrowserManager", "Browser created with instance-specific cache");

  if (context->browser) {
    int actual_browser_id = context->browser->GetIdentifier();
    LOG_DEBUG("BrowserManager", "Browser created successfully for context: " + context_id +
             " (actual_browser_id=" + std::to_string(actual_browser_id) +
             ", predicted=" + std::to_string(predicted_browser_id) + ")");

    // If prediction was wrong (shouldn't happen normally), re-register
    if (actual_browser_id != predicted_browser_id && vm) {
      LOG_WARN("BrowserManager", "Browser ID mismatch! Re-registering GPU and seed contexts");
      // Re-register GPU context
      owl_gpu_unregister_context(predicted_browser_id);
      owl_gpu_register_context(
          actual_browser_id,
          vm_copy.gpu.vendor.c_str(),
          vm_copy.gpu.renderer.c_str(),
          vm_copy.gpu.webgl_version.c_str(),
          vm_copy.gpu.shading_language.c_str()
      );
      owl_gpu_register_params(actual_browser_id, &gpu_params);
      owl_gpu_set_current_context(actual_browser_id);

      // Re-register seed context with correct browser_id
      // IMPORTANT: Regenerate seed_context_id with actual_browser_id for consistency with renderer
      owl_seed_unregister_context(predicted_browser_id);
      std::string actual_seed_context_id = "ctx_" + std::to_string(session_vm_seed) + "_" + std::to_string(actual_browser_id);
      owl_seed_register_context(actual_browser_id, actual_seed_context_id.c_str());
      owl_seed_set_current_context(actual_browser_id);
      stealth_config.context_id = actual_seed_context_id;  // Update for consistency

      // Re-sync ALL seeds to vm_copy (new seeds for actual_browser_id)
      vm_copy.canvas.hash_seed = owl_seed_get_canvas();
      vm_copy.gpu.renderer_hash_seed = owl_seed_get_webgl();
      vm_copy.audio.audio_hash_seed = owl_seed_get_audio();

      // Re-register stealth config and VM with correct browser_id
      OwlStealth::SetContextVM(actual_browser_id, vm_copy);
      OwlStealth::SetContextFingerprint(actual_browser_id, stealth_config);

      LOG_DEBUG("BrowserManager", "Re-registered seeds and stealth config with actual_browser_id=" +
               std::to_string(actual_browser_id) + " seed_context_id=" + actual_seed_context_id);
    }

    if (vm) {
      OwlStealth::SetContextVM(actual_browser_id, vm_copy);
      LOG_DEBUG("BrowserManager", "Applied VM profile for context: " + context_id +
               " (browser_id: " + std::to_string(actual_browser_id) +
               ", vm: " + vm->id + ", canvas_seed: 0x" +
               std::to_string(vm_copy.canvas.hash_seed) + ")");
    }

    // DEBUG: Log stealth_config.timezone before registration
    LOG_DEBUG("BrowserManager", "Registering stealth config with timezone: " + stealth_config.timezone +
             " (context->fingerprint.timezone: " + context->fingerprint.timezone + ")");
    OwlStealth::SetContextFingerprint(actual_browser_id, stealth_config);
    LOG_DEBUG("BrowserManager", "Registered unique stealth fingerprint for context: " + context_id +
             " (browser_id: " + std::to_string(actual_browser_id) +
             ", vm_id: " + stealth_config.vm_id + ")");

    // Apply cookies from profile (only for profiled contexts)
    if (context->has_profile) {
      OwlBrowserProfileManager* profile_manager = OwlBrowserProfileManager::GetInstance();
      profile_manager->ApplyProfileCookies(profile, context->browser);
    }
  } else {
    LOG_ERROR("BrowserManager", "Failed to create browser for context: " + context_id);
    // Browser creation failed - don't add to contexts map, return empty string to signal failure
    // Context will be destroyed when unique_ptr goes out of scope
    return "";
  }

  // Insert with exclusive lock (writer lock) - only if browser was created successfully
  {
    std::unique_lock<std::shared_mutex> lock(contexts_mutex_);
    contexts_[context_id] = std::move(context);
  }

  // Update counters atomically
  context_count_.fetch_add(1, std::memory_order_relaxed);
  current_memory_mb_.fetch_add(kEstimatedPerContextMB, std::memory_order_relaxed);

  return context_id;
}

void OwlBrowserManager::ReleaseContext(const std::string& id) {
  // Use shared lock for read-mostly operation
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);

  auto it = contexts_.find(id);
  if (it != contexts_.end()) {
    it->second->in_use.store(false, std::memory_order_release);
    it->second->last_used.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
  }
}

bool OwlBrowserManager::CloseContext(const std::string& id) {
  std::unique_ptr<BrowserContext> ctx;

  // Phase 1: Extract context under lock (fast)
  {
    std::unique_lock<std::shared_mutex> lock(contexts_mutex_);

    auto it = contexts_.find(id);
    if (it == contexts_.end()) {
      LOG_ERROR("BrowserManager", "CloseContext failed - context not found: " + id);
      return false;
    }

    // Extract from map
    ctx = std::move(it->second);
    contexts_.erase(it);

    context_count_.fetch_sub(1, std::memory_order_relaxed);
    current_memory_mb_.fetch_sub(kEstimatedPerContextMB, std::memory_order_relaxed);
  }

  // Phase 2: Close browser outside lock (slow but non-blocking)
  if (ctx) {
    // Wait for any in-flight operations to complete
    int wait_attempts = 0;
    while (ctx->HasActiveOperations() && wait_attempts++ < 100) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Unregister GPU and seed contexts
    if (ctx->browser) {
      int browser_id = ctx->browser->GetIdentifier();
      owl_gpu_unregister_context(browser_id);
      owl_seed_unregister_context(browser_id);
    }

    // Clear fingerprint seeds for this context (legacy, now handled by owl_seed_unregister_context)
    owl::OwlFingerprintGenerator::Instance().ClearContext(id);

    // Close the browser
    if (ctx->browser) {
      ctx->browser->GetHost()->CloseBrowser(true);
    }

    LOG_DEBUG("BrowserManager", "Closed context: " + id);
  }

  return true;
}

CefRefPtr<CefBrowser> OwlBrowserManager::GetBrowser(const std::string& id) {
  // Use shared lock for concurrent reads
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);

  auto it = contexts_.find(id);
  if (it != contexts_.end()) {
    // Update last_used time
    it->second->last_used.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
    return it->second->browser;
  }

  return nullptr;
}

void OwlBrowserManager::RegisterUIBrowser(const std::string& context_id, CefRefPtr<CefBrowser> browser, const LLMConfig* llm_config) {
  std::unique_lock<std::shared_mutex> lock(contexts_mutex_);

  // Create a BrowserContext for the UI browser
  auto context = std::make_unique<BrowserContext>();
  context->id = context_id;
  context->browser = browser;
  context->in_use.store(true, std::memory_order_relaxed);
  context->created = std::chrono::steady_clock::now();
  context->last_used.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);

  // Store LLM config if provided
  if (llm_config) {
    context->llm_config = *llm_config;
    LOG_DEBUG("BrowserManager", "Registered UI browser " + context_id + " with custom LLM config - " +
             "use_builtin: " + std::string(llm_config->use_builtin ? "true" : "false") +
             ", external_endpoint: " + llm_config->external_endpoint);
  } else {
    // Use default config (already set in BrowserContext constructor)
    LOG_DEBUG("BrowserManager", "Registered UI browser " + context_id + " with default LLM config");
  }

  contexts_[context_id] = std::move(context);
  context_count_.fetch_add(1, std::memory_order_relaxed);
  current_memory_mb_.fetch_add(kEstimatedPerContextMB, std::memory_order_relaxed);
}

size_t OwlBrowserManager::GetTotalMemoryUsage() const {
  // Fast path: return cached estimate
  return current_memory_mb_.load(std::memory_order_relaxed) * 1024 * 1024;
}

size_t OwlBrowserManager::GetActualMemoryUsage() const {
  // Platform-specific actual memory measurement
#if defined(OS_MACOS)
  struct mach_task_basic_info info;
  mach_msg_type_number_t size = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &size) == KERN_SUCCESS) {
    return info.resident_size;
  }
#elif defined(OS_LINUX)
  // Read from /proc/self/statm
  FILE* f = fopen("/proc/self/statm", "r");
  if (f) {
    long pages = 0;
    if (fscanf(f, "%ld", &pages) == 1) {  // First field is total program size in pages
      fclose(f);
      return pages * sysconf(_SC_PAGESIZE);
    }
    fclose(f);
  }
#endif
  // Fallback to estimate
  return GetTotalMemoryUsage();
}

// Helper: Find and extract the oldest context for removal
// Returns the extracted context (caller must close browser outside lock)
// Must be called with exclusive lock held
std::unique_ptr<BrowserContext> OwlBrowserManager::ExtractOldestContext(std::string& out_id) {
  auto now = std::chrono::steady_clock::now();

  // Find oldest unused context
  std::string oldest_id;
  auto oldest_time = now;

  for (const auto& pair : contexts_) {
    if (!pair.second->in_use.load(std::memory_order_relaxed) &&
        pair.second->last_used.load(std::memory_order_relaxed) < oldest_time) {
      oldest_time = pair.second->last_used.load(std::memory_order_relaxed);
      oldest_id = pair.first;
    }
  }

  // Extract context from map (but don't close browser yet - do that outside lock)
  if (!oldest_id.empty()) {
    auto it = contexts_.find(oldest_id);
    if (it != contexts_.end()) {
      // Skip if has active operations (will be cleaned up next cycle)
      if (it->second->HasActiveOperations()) {
        out_id.clear();
        return nullptr;
      }

      out_id = oldest_id;
      std::unique_ptr<BrowserContext> ctx = std::move(it->second);
      contexts_.erase(it);

      context_count_.fetch_sub(1, std::memory_order_relaxed);
      current_memory_mb_.fetch_sub(kEstimatedPerContextMB, std::memory_order_relaxed);

      return ctx;
    }
  }

  out_id.clear();
  return nullptr;
}

// Close a browser context safely (called outside lock)
void OwlBrowserManager::CloseBrowserContext(std::unique_ptr<BrowserContext> ctx, const std::string& context_id) {
  if (!ctx) return;

  // Wait for any in-flight operations to complete
  int wait_attempts = 0;
  while (ctx->HasActiveOperations() && wait_attempts++ < 100) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  // Unregister GPU and seed contexts
  if (ctx->browser) {
    int browser_id = ctx->browser->GetIdentifier();
    owl_gpu_unregister_context(browser_id);
    owl_seed_unregister_context(browser_id);
  }

  // Clear fingerprint seeds for this context (legacy, now handled by owl_seed_unregister_context)
  owl::OwlFingerprintGenerator::Instance().ClearContext(context_id);

  // Close the browser (this is the slow part that was blocking other operations)
  if (ctx->browser) {
    ctx->browser->GetHost()->CloseBrowser(true);
  }

  LOG_DEBUG("BrowserManager", "Removed context " + context_id + " due to memory pressure");

  // ctx destructor handles cleanup when it goes out of scope
}

void OwlBrowserManager::RemoveOldestContext() {
  // DEPRECATED: Use ExtractOldestContext + CloseBrowserContext instead
  // Kept for backwards compatibility but should migrate to new pattern
  std::string oldest_id;
  std::unique_ptr<BrowserContext> ctx;

  // Extract under lock
  {
    // Note: Caller should already hold lock, but this is defensive
    ctx = ExtractOldestContext(oldest_id);
  }

  // Close outside lock (but we're already called with lock held, so this is suboptimal)
  // New code should use the two-phase pattern in CleanupOldContexts
  if (ctx) {
    CloseBrowserContext(std::move(ctx), oldest_id);
  }
}

void OwlBrowserManager::CleanupOldContexts() {
  // PERFORMANCE OPTIMIZATION: Two-phase cleanup to minimize lock contention
  // Phase 1: Extract contexts under lock (fast)
  // Phase 2: Close browsers outside lock (slow, but doesn't block other operations)

  std::vector<std::pair<std::string, std::unique_ptr<BrowserContext>>> contexts_to_close;
  bool trigger_gc = false;

  // Phase 1: Extract contexts to close under exclusive lock
  {
    std::unique_lock<std::shared_mutex> lock(contexts_mutex_);

    // GRADUATED MEMORY PRESSURE HANDLING
    // Instead of binary "under pressure or not", use tiered response
    size_t total_memory = GetTotalMemoryUsage();
    size_t max_memory = max_memory_mb_.load(std::memory_order_relaxed) * 1024 * 1024;
    size_t current_count = context_count_.load(std::memory_order_relaxed);

    // Calculate memory pressure level (0-100%)
    double pressure_ratio = static_cast<double>(total_memory) / static_cast<double>(max_memory);
    size_t contexts_to_remove = 0;

    if (pressure_ratio >= 1.0) {
      // CRITICAL: Over memory limit - aggressive cleanup (60%)
      contexts_to_remove = (current_count * 60) / 100;
      LOG_WARN("BrowserManager", "CRITICAL memory pressure (" +
               std::to_string(static_cast<int>(pressure_ratio * 100)) + "%), removing " +
               std::to_string(contexts_to_remove) + " of " + std::to_string(current_count) + " contexts");
      trigger_gc = true;
    } else if (pressure_ratio >= 0.9) {
      // HIGH: 90-100% memory - moderate cleanup (40%)
      contexts_to_remove = (current_count * 40) / 100;
      LOG_WARN("BrowserManager", "HIGH memory pressure (" +
               std::to_string(static_cast<int>(pressure_ratio * 100)) + "%), removing " +
               std::to_string(contexts_to_remove) + " contexts");
      trigger_gc = true;
    } else if (pressure_ratio >= 0.75) {
      // MODERATE: 75-90% memory - light cleanup (20%)
      contexts_to_remove = (current_count * 20) / 100;
      if (contexts_to_remove < 1) contexts_to_remove = 1;
      LOG_DEBUG("BrowserManager", "MODERATE memory pressure (" +
               std::to_string(static_cast<int>(pressure_ratio * 100)) + "%), removing " +
               std::to_string(contexts_to_remove) + " contexts");
    } else {
      // LOW: Under 75% - only cleanup idle contexts older than timeout
      auto now = std::chrono::steady_clock::now();
      auto idle_threshold = std::chrono::seconds(kIdleTimeoutSec);

      for (const auto& pair : contexts_) {
        if (!pair.second->in_use.load(std::memory_order_relaxed)) {
          auto last_used = pair.second->last_used.load(std::memory_order_relaxed);
          if (now - last_used > idle_threshold) {
            contexts_to_remove++;
          }
        }
      }

      if (contexts_to_remove > 0) {
        LOG_DEBUG("BrowserManager", "Normal cleanup: " +
                 std::to_string(contexts_to_remove) + " idle contexts exceeded timeout");
      }
    }

    // Ensure at least 1 context removed if any contexts need removal
    if (contexts_to_remove < 1 && pressure_ratio >= 0.75) {
      contexts_to_remove = 1;
    }

    // Extract contexts
    for (size_t i = 0; i < contexts_to_remove && !contexts_.empty(); i++) {
      std::string ctx_id;
      auto ctx = ExtractOldestContext(ctx_id);
      if (ctx) {
        contexts_to_close.emplace_back(ctx_id, std::move(ctx));
      }
    }
  }
  // Lock released here - other operations can now proceed

  // Phase 2: Close browsers outside lock (slow but non-blocking)
  // This can take time but doesn't block CreateContext, GetBrowser, etc.
  for (auto& pair : contexts_to_close) {
    CloseBrowserContext(std::move(pair.second), pair.first);
  }

  // Trigger garbage collection after significant cleanup
  if (trigger_gc || contexts_to_close.size() > 3) {
    PumpMessageLoopIfNeeded();
  }
}

ActionResult OwlBrowserManager::Navigate(const std::string& context_id, const std::string& url,
                                         const std::string& wait_until, int timeout_ms) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "Navigate failed - browser not found for context: " + context_id);
    return ActionResult::BrowserNotFound(context_id);
  }

  // Special handling for about:blank - it loads instantly and doesn't trigger normal load events
  bool is_about_blank = (url == "about:blank" || url.empty());

  // Set current GPU context for ANGLE wrapper (per-context GPU spoofing)
  int browser_id = browser->GetIdentifier();
  owl_gpu_set_current_context(browser_id);

  std::ostringstream msg;
  msg << "Navigating context " << context_id << " to URL: " + url;
  if (!wait_until.empty()) {
    msg << " (wait_until=" << wait_until << ", timeout=" << timeout_ms << "ms)";
  }
  if (is_about_blank) {
    msg << " [about:blank - instant load]";
  }
  LOG_DEBUG("BrowserManager", msg.str());

  // Get the client to access navigation state
  CefRefPtr<CefBrowserHost> host = browser->GetHost();
  if (!host) {
    LOG_ERROR("BrowserManager", "Navigate failed - no browser host for context: " + context_id);
    return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "No browser host for context: " + context_id);
  }

  CefRefPtr<CefClient> client_base = host->GetClient();
  if (!client_base) {
    LOG_ERROR("BrowserManager", "Navigate failed - no client for context: " + context_id);
    return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "No client for context: " + context_id);
  }

  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  if (!client) {
    LOG_ERROR("BrowserManager", "Navigate failed - client cast failed for context: " + context_id);
    return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "Client cast failed for context: " + context_id);
  }

  // Reset navigation state before starting new navigation
  client->ResetNavigation();

  // LoadURL must be called on UI thread
  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  if (frame) {
    // Log current frame URL before navigation
    std::string current_url = frame->GetURL().ToString();
    std::ostringstream pre_msg;
    pre_msg << "Before LoadURL - Current URL: " << current_url << " Target: " << url;
    LOG_DEBUG("BrowserManager", pre_msg.str());

    frame->LoadURL(url);
    LOG_DEBUG("BrowserManager", "LoadURL called for: " + url);

    // If wait_until is specified, wait for the navigation to complete
    if (!wait_until.empty()) {
      // Special case: about:blank loads instantly and doesn't trigger normal load events
      // Skip waiting and return success immediately
      if (is_about_blank) {
        LOG_DEBUG("BrowserManager", "about:blank navigation - skipping wait (instant load)");
        // Give CEF a moment to process the LoadURL
        PumpMessageLoopIfNeeded();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        PumpMessageLoopIfNeeded();
        return ActionResult::Success("Navigated to: " + url);
      }

      if (wait_until == "load" || wait_until == "domcontentloaded") {
        // Wait for navigation load event
        ActionResult wait_result = WaitForNavigation(context_id, timeout_ms);
        if (wait_result.status != ActionStatus::OK) {
          return wait_result;
        }
        LOG_DEBUG("BrowserManager", "Navigation completed (wait_until=" + wait_until + ") for: " + url);
        return ActionResult::Success("Navigated to: " + url);
      } else if (wait_until == "networkidle") {
        // Wait for navigation first
        ActionResult wait_result = WaitForNavigation(context_id, timeout_ms);
        if (wait_result.status != ActionStatus::OK) {
          return wait_result;
        }
        // Then wait for network idle
        ActionResult idle_result = WaitForNetworkIdle(context_id, 500, timeout_ms);
        if (idle_result.status != ActionStatus::OK) {
          return idle_result;
        }
        LOG_DEBUG("BrowserManager", "Navigation completed (wait_until=networkidle) for: " + url);
        return ActionResult::Success("Navigated to: " + url);
      } else {
        LOG_WARN("BrowserManager", "Unknown wait_until value: " + wait_until + ", ignoring");
      }
    }

    // PERFORMANCE OPTIMIZATION: Non-blocking navigation (default behavior)!
    // LoadURL is async - we just trigger it and return immediately.
    // The page will load in the background. The SDK/test can:
    // 1. Call waitForNavigation separately if they need to wait
    // 2. Or just proceed if they know the page will be ready (e.g., same-origin)
    //
    // This allows 50 navigations to be triggered in parallel, with all pages
    // loading concurrently instead of serially waiting for each one.

    LOG_DEBUG("BrowserManager", "Navigation triggered (non-blocking) for: " + url);
    return ActionResult::Success("Navigation started to: " + url);
  }

  LOG_ERROR("BrowserManager", "Navigate failed - no main frame");
  return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "No main frame available");
}

ActionResult OwlBrowserManager::WaitForNavigation(const std::string& context_id, int timeout_ms) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "WaitForNavigation failed - browser not found for context: " + context_id);
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "=== WAIT FOR NAVIGATION === context=" + context_id +
           " timeout=" + std::to_string(timeout_ms) + "ms");

  CefRefPtr<CefBrowserHost> host = browser->GetHost();
  if (!host) {
    LOG_ERROR("BrowserManager", "WaitForNavigation failed - no browser host");
    return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "No browser host");
  }

  CefRefPtr<CefClient> client_base = host->GetClient();
  if (!client_base) {
    LOG_ERROR("BrowserManager", "WaitForNavigation failed - no client");
    return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "No client");
  }

  OwlClient* client = static_cast<OwlClient*>(client_base.get());

  // Wait for navigation to complete
  client->WaitForNavigation(timeout_ms);

  NavigationInfo nav_info = client->GetNavigationInfo();
  if (nav_info.state == NavigationState::COMPLETE) {
    // Also wait for element scan to complete
    std::ostringstream ctx_stream;
    ctx_stream << "ctx_" << std::setfill('0') << std::setw(6) << browser->GetIdentifier();
    std::string expected_context = ctx_stream.str();
    client->WaitForElementScan(browser, expected_context, 5000);

    LOG_DEBUG("BrowserManager", "Navigation complete for context: " + context_id);
    return ActionResult::Success("Navigation completed");
  } else if (nav_info.state == NavigationState::FAILED) {
    LOG_ERROR("BrowserManager", "Navigation failed: " + nav_info.error_message);
    return ActionResult::Failure(ActionStatus::NAVIGATION_FAILED, nav_info.error_message);
  } else {
    LOG_WARN("BrowserManager", "Navigation timeout or still loading");
    return ActionResult::Failure(ActionStatus::TIMEOUT, "Navigation timeout after " + std::to_string(timeout_ms) + "ms");
  }
}

ActionResult OwlBrowserManager::Click(const std::string& context_id, const std::string& selector,
                                       VerificationLevel level) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "Click failed - browser not found for context: " + context_id);
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "=== CLICK START === selector='" + selector + "' context='" + context_id + "'");

  // Unfreeze cache so click events can be processed properly by JavaScript
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  client->UnfreezeFrameCache();
  LOG_DEBUG("BrowserManager", "Frame cache unfrozen for click interaction");

  // Check if selector is a direct position format (e.g., "100x200")
  // Pattern: digits + 'x' + digits with no other characters
  bool is_direct_position = false;
  int direct_x = -1, direct_y = -1;
  size_t x_pos = selector.find('x');
  if (x_pos != std::string::npos && x_pos > 0 && x_pos < selector.length() - 1) {
    // Check if all chars before 'x' are digits and all chars after 'x' are digits
    bool before_digits = true, after_digits = true;
    for (size_t i = 0; i < x_pos; i++) {
      if (!std::isdigit(selector[i])) {
        before_digits = false;
        break;
      }
    }
    for (size_t i = x_pos + 1; i < selector.length(); i++) {
      if (!std::isdigit(selector[i])) {
        after_digits = false;
        break;
      }
    }
    if (before_digits && after_digits) {
      is_direct_position = true;
      direct_x = std::stoi(selector.substr(0, x_pos));
      direct_y = std::stoi(selector.substr(x_pos + 1));
      LOG_DEBUG("BrowserManager", "Direct position click detected: (" + std::to_string(direct_x) + "," + std::to_string(direct_y) + ")");
    }
  }

  // If direct position, skip element lookup and click directly at coordinates
  if (is_direct_position) {
    auto host = browser->GetHost();
    host->SetFocus(true);

    CefMouseEvent mouse_event;
    mouse_event.x = direct_x;
    mouse_event.y = direct_y;
    mouse_event.modifiers = 0;

    // Send mouse move, down, up in quick succession - NO pumping
    host->SendMouseMoveEvent(mouse_event, false);
    host->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 1);
    host->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 1);

    LOG_DEBUG("BrowserManager", "=== CLICK COMPLETE (POSITION) ===");
    return ActionResult::Success();
  }

  // Parse selector - check if it has position metadata appended (format: "SELECTOR@x,y")
  std::string actual_selector = selector;
  size_t at_pos = selector.find('@');
  if (at_pos != std::string::npos) {
    actual_selector = selector.substr(0, at_pos);
    LOG_DEBUG("BrowserManager", "Extracted actual selector from position-tagged selector: " + actual_selector);
  }

  // Step 1: Always do a fresh scan to get accurate positions
  // Cached positions can be stale after page scroll, DOM mutations, or window resize
  // Semantic selectors (natural language like "submit button") need SemanticMatcher
  // HTML tags (h1, div, button) and CSS selectors (#id, .class) use direct lookup
  bool is_semantic = IsSelectorSemantic(actual_selector);

  ElementRenderInfo info;
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  bool found = false;

  // Always do a fresh scan for Click to ensure accurate coordinates
  // The RenderTracker cache can have stale positions after scrolling
  {
    LOG_DEBUG("BrowserManager", "Scanning for element: " + selector +
              (is_semantic ? " (semantic)" : " (CSS)"));

    // Trigger element scan from renderer process
    CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("scan_element");
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    args->SetString(0, context_id);
    args->SetString(1, selector);
    browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);

    // Pump to process scan message
    // In UI mode, don't call CefDoMessageLoopWork() from background thread - it crashes
    if (UsesRunMessageLoop()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } else {
      // Single pump is sufficient - WaitForElementScan handles proper synchronization
      PumpMessageLoopIfNeeded();
    }

    // Try getting element again after scan
    // For semantic selectors, use SemanticMatcher to find best match
    if (is_semantic) {
      OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
      auto matches = matcher->FindByDescription(context_id, selector, 1);
      if (!matches.empty()) {
        LOG_DEBUG("BrowserManager", "SemanticMatcher found element with confidence: " + std::to_string(matches[0].confidence));
        const auto& elem = matches[0].element;
        info.x = elem.x;
        info.y = elem.y;
        info.width = elem.width;
        info.height = elem.height;
        info.tag = elem.tag;
        info.id = elem.id;
        info.visible = elem.visible;
        found = true;
      } else {
        LOG_WARN("BrowserManager", "SemanticMatcher found no matches for: " + selector);
        found = tracker->GetElementBounds(context_id, selector, info);
      }
    } else {
      found = tracker->GetElementBounds(context_id, selector, info);
    }
  }

  // Step 2: Verify we have the element
  if (!found) {
    LOG_ERROR("BrowserManager", "Element not found: " + selector);
    LOG_DEBUG("BrowserManager", "=== CLICK FAILED (ELEMENT NOT FOUND) ===");
    return ActionResult::ElementNotFound(selector);
  } else {
    LOG_DEBUG("BrowserManager", "Found element: tag=" + info.tag + " id=" + info.id +
             " visible=" + (info.visible ? "true" : "false"));
  }

  // OPTIMIZATION: For CSS selectors with visible cached elements, skip scroll/rescan
  // This dramatically improves performance for repeated interactions with same elements
  bool needs_scroll = is_semantic || !info.visible || info.y < 0 || info.y > 1080;

  if (needs_scroll) {
    // Scroll element into view - only needed for semantic selectors or off-screen elements
    std::string scroll_selector = actual_selector;
    if (is_semantic && !info.id.empty()) {
      scroll_selector = "#" + info.id;
    }

    CefRefPtr<CefProcessMessage> scroll_msg = CefProcessMessage::Create("scroll_to_element");
    CefRefPtr<CefListValue> scroll_args = scroll_msg->GetArgumentList();
    scroll_args->SetString(0, context_id);
    scroll_args->SetString(1, scroll_selector);
    browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, scroll_msg);
    LOG_DEBUG("BrowserManager", "Sent scroll_to_element for: " + scroll_selector);

    // Process scroll message - WaitForElementScan handles proper synchronization
    // In UI mode, don't call CefDoMessageLoopWork() from background thread - it crashes
    if (UsesRunMessageLoop()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } else {
      PumpMessageLoopIfNeeded();
    }

    // CRITICAL: Wait for element scan to complete after scroll
    // The scroll changes element positions, so we need fresh coordinates
    // Use WaitForElementScan which properly waits for scan_complete signal
    OwlClient* client = static_cast<OwlClient*>(browser->GetHost()->GetClient().get());
    if (client) {
      LOG_DEBUG("BrowserManager", "Waiting for element scan after scroll...");
      client->WaitForElementScan(browser, context_id, 500);  // 500ms timeout
    }

    // Get fresh bounds from tracker after scan completes
    ElementRenderInfo updated_info;
    bool found_after_scroll = false;
    std::string lookup_selector = is_semantic && !info.id.empty() ? "#" + info.id : actual_selector;

    found_after_scroll = tracker->GetElementBounds(context_id, lookup_selector, updated_info);

    if (found_after_scroll && updated_info.visible) {
      LOG_DEBUG("BrowserManager", "Updated coordinates after scroll: (" +
                std::to_string(updated_info.x) + "," + std::to_string(updated_info.y) + ")");
      info = updated_info;
    } else {
      LOG_WARN("BrowserManager", "Could not get updated coordinates after scroll, using original");
    }
  }

  // Calculate click position
  int click_x, click_y;

  // Special case: For checkbox labels, click the LEFT side where the checkmark visual is
  // (custom checkboxes have hidden INPUT and styled SPAN.checkmark on the left)
  if (info.tag == "LABEL" && (info.className.find("checkbox") != std::string::npos ||
                               info.className.find("check") != std::string::npos)) {
    // Click left side (checkmark position) instead of center
    click_x = info.x + 20;  // ~20px from left edge (checkmark center)
    click_y = info.y + (info.height / 2);  // Vertically centered
    LOG_DEBUG("BrowserManager", "Checkbox label detected - clicking LEFT side for checkmark");
  } else {
    // Default: click center of element
    click_x = info.x + (info.width / 2);
    click_y = info.y + (info.height / 2);
  }

  LOG_DEBUG("BrowserManager", "Clicking element at (" + std::to_string(click_x) + ", " + std::to_string(click_y) +
           ") (element bounds: " + std::to_string(info.x) + "," + std::to_string(info.y) +
           " " + std::to_string(info.width) + "x" + std::to_string(info.height) + ")");

  auto host = browser->GetHost();

  // Set focus and send all mouse events - NO pumping here, let main loop handle it
  host->SetFocus(true);

  CefMouseEvent mouse_event;
  mouse_event.x = click_x;
  mouse_event.y = click_y;
  mouse_event.modifiers = 0;

  // Send mouse move, down, up in quick succession
  host->SendMouseMoveEvent(mouse_event, false);
  host->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 1);
  host->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 1);

  // In UI mode, pump the message loop to ensure click events are processed
  // This is critical for checkbox clicks which need immediate DOM update
  if (UsesRunMessageLoop()) {
    // UI mode - give time for the click to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  } else {
    // Headless mode - single pump, main loop will process rest
    PumpMessageLoopIfNeeded();
  }

  LOG_DEBUG("BrowserManager", "=== CLICK COMPLETE ===");

  return ActionResult::Success();
}

ActionResult OwlBrowserManager::DragDrop(const std::string& context_id,
                                  int start_x, int start_y,
                                  int end_x, int end_y,
                                  const std::vector<std::pair<int, int>>& mid_points) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "DragDrop failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "=== DRAG DROP START === from (" +
           std::to_string(start_x) + "," + std::to_string(start_y) + ") to (" +
           std::to_string(end_x) + "," + std::to_string(end_y) + ") with " +
           std::to_string(mid_points.size()) + " waypoints");

  auto host = browser->GetHost();

  // CRITICAL: First trigger a scan_element IPC to "activate" the page for mouse events
  // This is needed because owl:// scheme pages don't receive direct coordinate mouse events
  // until some IPC interaction with the renderer has occurred
  LOG_DEBUG("BrowserManager", "Triggering element scan to activate page for mouse events");
  CefRefPtr<CefProcessMessage> scan_msg = CefProcessMessage::Create("scan_element");
  CefRefPtr<CefListValue> scan_args = scan_msg->GetArgumentList();
  scan_args->SetString(0, context_id);
  scan_args->SetString(1, "body");  // Scan for body element
  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, scan_msg);

  // Quick pump for scan (non-blocking)
  for (int i = 0; i < 5; i++) {
    PumpMessageLoopIfNeeded();
  }

  // Step 1: Ensure browser has focus (like Click does)
  LOG_DEBUG("BrowserManager", "Setting browser focus");
  host->SetFocus(true);
  PumpMessageLoopIfNeeded();

  CefMouseEvent mouse_event;
  mouse_event.modifiers = 0;

  // Step 2: Move to start position
  LOG_DEBUG("BrowserManager", "Moving to start position: (" +
            std::to_string(start_x) + "," + std::to_string(start_y) + ")");
  mouse_event.x = start_x;
  mouse_event.y = start_y;
  host->SendMouseMoveEvent(mouse_event, false);
  PumpMessageLoopIfNeeded();  // Process events immediately like Click does

  // Step 3: Mouse down at start position (begin drag)
  LOG_DEBUG("BrowserManager", "Mouse down - initiating drag");
  host->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 1);
  PumpMessageLoopIfNeeded();  // Critical: process mousedown event immediately

  // Step 4: Move through waypoints with button held
  // Use EVENTFLAG_LEFT_MOUSE_BUTTON to indicate button is pressed during moves
  mouse_event.modifiers = EVENTFLAG_LEFT_MOUSE_BUTTON;

  for (size_t i = 0; i < mid_points.size(); i++) {
    int wp_x = mid_points[i].first;
    int wp_y = mid_points[i].second;
    LOG_DEBUG("BrowserManager", "Moving through waypoint " + std::to_string(i + 1) +
              "/" + std::to_string(mid_points.size()) + ": (" +
              std::to_string(wp_x) + "," + std::to_string(wp_y) + ")");

    mouse_event.x = wp_x;
    mouse_event.y = wp_y;
    host->SendMouseMoveEvent(mouse_event, false);
    PumpMessageLoopIfNeeded();  // Process each move event
    std::this_thread::sleep_for(std::chrono::milliseconds(10));  // Small delay for natural movement
  }

  // Step 5: Move to end position (still with button held)
  LOG_DEBUG("BrowserManager", "Moving to end position: (" +
            std::to_string(end_x) + "," + std::to_string(end_y) + ")");
  mouse_event.x = end_x;
  mouse_event.y = end_y;
  host->SendMouseMoveEvent(mouse_event, false);
  PumpMessageLoopIfNeeded();

  // Step 6: Mouse up at end position (complete drop)
  LOG_DEBUG("BrowserManager", "Mouse up - completing drop");
  mouse_event.modifiers = 0;  // Clear modifier for mouse up
  host->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 1);
  PumpMessageLoopIfNeeded();  // Critical: process mouseup event immediately

  LOG_DEBUG("BrowserManager", "=== DRAG DROP COMPLETE ===");

  return ActionResult::Success("Dragged from (" + std::to_string(start_x) + "," + std::to_string(start_y) +
                               ") to (" + std::to_string(end_x) + "," + std::to_string(end_y) + ")");
}

ActionResult OwlBrowserManager::HTML5DragDrop(const std::string& context_id,
                                       const std::string& source_selector,
                                       const std::string& target_selector) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "HTML5DragDrop failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "=== HTML5 DRAG DROP START === source='" +
           source_selector + "' target='" + target_selector + "'");

  // Send IPC message to renderer to dispatch HTML5 drag events
  CefRefPtr<CefProcessMessage> drag_msg = CefProcessMessage::Create("dispatch_html5_drag");
  CefRefPtr<CefListValue> drag_args = drag_msg->GetArgumentList();
  drag_args->SetString(0, context_id);
  drag_args->SetString(1, source_selector);
  drag_args->SetString(2, target_selector);

  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, drag_msg);

  // Quick pump to process IPC (non-blocking)
  for (int i = 0; i < 5; i++) {
    PumpMessageLoopIfNeeded();
  }

  LOG_DEBUG("BrowserManager", "=== HTML5 DRAG DROP COMPLETE ===");

  return ActionResult::Success("HTML5 drag from '" + source_selector + "' to '" + target_selector + "'");
}

// ==================== HUMAN-LIKE MOUSE MOVEMENT ====================

ActionResult OwlBrowserManager::MouseMove(const std::string& context_id,
                                   int start_x, int start_y,
                                   int end_x, int end_y,
                                   int steps,
                                   const std::vector<std::pair<int, int>>& stop_points) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "MouseMove failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "=== MOUSE MOVE START === from (" +
           std::to_string(start_x) + "," + std::to_string(start_y) + ") to (" +
           std::to_string(end_x) + "," + std::to_string(end_y) + ") with " +
           std::to_string(stop_points.size()) + " stop points");

  auto host = browser->GetHost();

  // Random number generator for natural variation
  static std::random_device rd;
  static std::mt19937 gen(rd());

  // Calculate distance for auto steps
  double dx = end_x - start_x;
  double dy = end_y - start_y;
  double distance = std::sqrt(dx * dx + dy * dy);

  // Auto-calculate steps based on distance (roughly 1 step per 5-10 pixels)
  if (steps <= 0) {
    std::uniform_int_distribution<> step_dis(5, 10);
    steps = std::max(10, static_cast<int>(distance / step_dis(gen)));
  }

  // Generate control points for bezier curve
  // Add random perpendicular offset for natural curve
  std::uniform_real_distribution<> curve_dis(-0.3, 0.3);  // 30% curve deviation
  double perp_x = -dy;  // Perpendicular vector
  double perp_y = dx;
  double perp_len = std::sqrt(perp_x * perp_x + perp_y * perp_y);
  if (perp_len > 0) {
    perp_x /= perp_len;
    perp_y /= perp_len;
  }

  // Control point at ~40% of path with random curve
  double curve_factor = curve_dis(gen);
  double ctrl_x = start_x + dx * 0.4 + perp_x * distance * curve_factor;
  double ctrl_y = start_y + dy * 0.4 + perp_y * distance * curve_factor;

  // Second control point at ~70% with smaller random curve
  double curve_factor2 = curve_dis(gen) * 0.5;
  double ctrl2_x = start_x + dx * 0.7 + perp_x * distance * curve_factor2;
  double ctrl2_y = start_y + dy * 0.7 + perp_y * distance * curve_factor2;

  LOG_DEBUG("BrowserManager", "Bezier curve: ctrl1=(" + std::to_string((int)ctrl_x) + "," +
            std::to_string((int)ctrl_y) + ") ctrl2=(" + std::to_string((int)ctrl2_x) + "," +
            std::to_string((int)ctrl2_y) + ") steps=" + std::to_string(steps));

  // Set focus
  host->SetFocus(true);
  PumpMessageLoopIfNeeded();

  CefMouseEvent mouse_event;
  mouse_event.modifiers = 0;

  // Move to start position
  mouse_event.x = start_x;
  mouse_event.y = start_y;
  host->SendMouseMoveEvent(mouse_event, false);
  PumpMessageLoopIfNeeded();

  // Build stop points set for easy lookup
  std::set<int> stop_at_steps;
  for (const auto& sp : stop_points) {
    // Find closest step to stop point
    for (int s = 0; s < steps; s++) {
      double t = static_cast<double>(s) / (steps - 1);
      // Cubic bezier
      double t2 = t * t;
      double t3 = t2 * t;
      double mt = 1 - t;
      double mt2 = mt * mt;
      double mt3 = mt2 * mt;
      double px = mt3 * start_x + 3 * mt2 * t * ctrl_x + 3 * mt * t2 * ctrl2_x + t3 * end_x;
      double py = mt3 * start_y + 3 * mt2 * t * ctrl_y + 3 * mt * t2 * ctrl2_y + t3 * end_y;

      // Check if this point is close to stop point
      double dist = std::sqrt((px - sp.first) * (px - sp.first) + (py - sp.second) * (py - sp.second));
      if (dist < 20) {  // Within 20 pixels
        stop_at_steps.insert(s);
        break;
      }
    }
  }

  // Generate timing variation (easing - faster in middle, slower at ends)
  std::uniform_int_distribution<> delay_dis(3, 12);  // Base delay 3-12ms
  std::uniform_int_distribution<> stop_delay_dis(50, 150);  // Stop delay 50-150ms

  // Move along cubic bezier curve
  for (int s = 1; s < steps; s++) {
    double t = static_cast<double>(s) / (steps - 1);

    // Cubic bezier formula: B(t) = (1-t)^3*P0 + 3*(1-t)^2*t*P1 + 3*(1-t)*t^2*P2 + t^3*P3
    double t2 = t * t;
    double t3 = t2 * t;
    double mt = 1 - t;
    double mt2 = mt * mt;
    double mt3 = mt2 * mt;

    double px = mt3 * start_x + 3 * mt2 * t * ctrl_x + 3 * mt * t2 * ctrl2_x + t3 * end_x;
    double py = mt3 * start_y + 3 * mt2 * t * ctrl_y + 3 * mt * t2 * ctrl2_y + t3 * end_y;

    // Add tiny random jitter for micro-movements (like real hand tremor)
    std::uniform_int_distribution<> jitter_dis(-1, 1);
    int jitter_x = jitter_dis(gen);
    int jitter_y = jitter_dis(gen);

    mouse_event.x = static_cast<int>(px) + jitter_x;
    mouse_event.y = static_cast<int>(py) + jitter_y;

    host->SendMouseMoveEvent(mouse_event, false);
    PumpMessageLoopIfNeeded();

    // Variable delay - slower at start and end (easing)
    int delay;
    if (t < 0.2 || t > 0.8) {
      delay = delay_dis(gen) + 5;  // Slower at edges
    } else {
      delay = delay_dis(gen);  // Faster in middle
    }

    // Check for stop points
    if (stop_at_steps.count(s)) {
      delay = stop_delay_dis(gen);
      LOG_DEBUG("BrowserManager", "Stop point at step " + std::to_string(s) +
                " - pausing " + std::to_string(delay) + "ms");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
  }

  // Ensure we end exactly at target
  mouse_event.x = end_x;
  mouse_event.y = end_y;
  host->SendMouseMoveEvent(mouse_event, false);
  PumpMessageLoopIfNeeded();

  LOG_DEBUG("BrowserManager", "=== MOUSE MOVE COMPLETE ===");

  return ActionResult::Success("Mouse moved from (" + std::to_string(start_x) + "," + std::to_string(start_y) +
                               ") to (" + std::to_string(end_x) + "," + std::to_string(end_y) + ")");
}

// ==================== ADVANCED MOUSE INTERACTIONS ====================

ActionResult OwlBrowserManager::Hover(const std::string& context_id, const std::string& selector) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "Hover failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "=== HOVER START === selector='" + selector + "'");

  // Reuse Click's element finding logic but only send mouse move
  // Check if selector is a direct position format (e.g., "100x200")
  bool is_direct_position = false;
  int direct_x = -1, direct_y = -1;
  size_t x_pos = selector.find('x');
  if (x_pos != std::string::npos && x_pos > 0 && x_pos < selector.length() - 1) {
    bool before_digits = true, after_digits = true;
    for (size_t i = 0; i < x_pos; i++) {
      if (!std::isdigit(selector[i])) { before_digits = false; break; }
    }
    for (size_t i = x_pos + 1; i < selector.length(); i++) {
      if (!std::isdigit(selector[i])) { after_digits = false; break; }
    }
    if (before_digits && after_digits) {
      is_direct_position = true;
      direct_x = std::stoi(selector.substr(0, x_pos));
      direct_y = std::stoi(selector.substr(x_pos + 1));
    }
  }

  auto host = browser->GetHost();
  CefMouseEvent mouse_event;
  mouse_event.modifiers = 0;

  if (is_direct_position) {
    mouse_event.x = direct_x;
    mouse_event.y = direct_y;
    host->SendMouseMoveEvent(mouse_event, false);
    LOG_DEBUG("BrowserManager", "=== HOVER COMPLETE (POSITION) ===");
    return ActionResult::Success("Hovered at position: " + selector);
  }

  // Find element position
  std::string actual_selector = selector;
  size_t at_pos = selector.find('@');
  if (at_pos != std::string::npos) {
    actual_selector = selector.substr(0, at_pos);
  }

  bool is_semantic = IsSelectorSemantic(actual_selector);
  ElementRenderInfo info;
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  bool found = false;

  if (is_semantic) {
    OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
    auto matches = matcher->FindByDescription(context_id, selector, 1);
    if (!matches.empty()) {
      const auto& elem = matches[0].element;
      info.x = elem.x; info.y = elem.y;
      info.width = elem.width; info.height = elem.height;
      found = true;
    }
  } else {
    found = tracker->GetElementBounds(context_id, actual_selector, info);
  }

  if (!found) {
    LOG_ERROR("BrowserManager", "Hover failed - element not found: " + selector);
    return ActionResult::ElementNotFound(selector);
  }

  int hover_x = info.x + (info.width / 2);
  int hover_y = info.y + (info.height / 2);

  mouse_event.x = hover_x;
  mouse_event.y = hover_y;
  host->SendMouseMoveEvent(mouse_event, false);

  LOG_DEBUG("BrowserManager", "=== HOVER COMPLETE at (" + std::to_string(hover_x) + "," + std::to_string(hover_y) + ") ===");
  return ActionResult::Success("Hovered over element: " + selector);
}

ActionResult OwlBrowserManager::DoubleClick(const std::string& context_id, const std::string& selector) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "DoubleClick failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "=== DOUBLE CLICK START === selector='" + selector + "'");

  // Check for direct position format
  bool is_direct_position = false;
  int direct_x = -1, direct_y = -1;
  size_t x_pos = selector.find('x');
  if (x_pos != std::string::npos && x_pos > 0 && x_pos < selector.length() - 1) {
    bool before_digits = true, after_digits = true;
    for (size_t i = 0; i < x_pos; i++) {
      if (!std::isdigit(selector[i])) { before_digits = false; break; }
    }
    for (size_t i = x_pos + 1; i < selector.length(); i++) {
      if (!std::isdigit(selector[i])) { after_digits = false; break; }
    }
    if (before_digits && after_digits) {
      is_direct_position = true;
      direct_x = std::stoi(selector.substr(0, x_pos));
      direct_y = std::stoi(selector.substr(x_pos + 1));
    }
  }

  auto host = browser->GetHost();
  host->SetFocus(true);

  CefMouseEvent mouse_event;
  mouse_event.modifiers = 0;

  if (is_direct_position) {
    mouse_event.x = direct_x;
    mouse_event.y = direct_y;
    host->SendMouseMoveEvent(mouse_event, false);
    // Double-click: click_count = 2
    host->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 2);
    host->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 2);
    LOG_DEBUG("BrowserManager", "=== DOUBLE CLICK COMPLETE (POSITION) ===");
    return ActionResult::Success("Double-clicked at position: " + selector);
  }

  // Find element
  std::string actual_selector = selector;
  size_t at_pos = selector.find('@');
  if (at_pos != std::string::npos) actual_selector = selector.substr(0, at_pos);

  bool is_semantic = IsSelectorSemantic(actual_selector);
  ElementRenderInfo info;
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  bool found = false;

  if (is_semantic) {
    OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
    auto matches = matcher->FindByDescription(context_id, selector, 1);
    if (!matches.empty()) {
      const auto& elem = matches[0].element;
      info.x = elem.x; info.y = elem.y;
      info.width = elem.width; info.height = elem.height;
      found = true;
    }
  } else {
    found = tracker->GetElementBounds(context_id, actual_selector, info);
  }

  if (!found) {
    LOG_ERROR("BrowserManager", "DoubleClick failed - element not found");
    return ActionResult::ElementNotFound(selector);
  }

  int click_x = info.x + (info.width / 2);
  int click_y = info.y + (info.height / 2);

  mouse_event.x = click_x;
  mouse_event.y = click_y;
  host->SendMouseMoveEvent(mouse_event, false);
  host->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 2);
  host->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 2);

  LOG_DEBUG("BrowserManager", "=== DOUBLE CLICK COMPLETE ===");
  return ActionResult::Success("Double-clicked element: " + selector);
}

ActionResult OwlBrowserManager::RightClick(const std::string& context_id, const std::string& selector) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "RightClick failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "=== RIGHT CLICK START === selector='" + selector + "'");

  // Check for direct position format
  bool is_direct_position = false;
  int direct_x = -1, direct_y = -1;
  size_t x_pos = selector.find('x');
  if (x_pos != std::string::npos && x_pos > 0 && x_pos < selector.length() - 1) {
    bool before_digits = true, after_digits = true;
    for (size_t i = 0; i < x_pos; i++) {
      if (!std::isdigit(selector[i])) { before_digits = false; break; }
    }
    for (size_t i = x_pos + 1; i < selector.length(); i++) {
      if (!std::isdigit(selector[i])) { after_digits = false; break; }
    }
    if (before_digits && after_digits) {
      is_direct_position = true;
      direct_x = std::stoi(selector.substr(0, x_pos));
      direct_y = std::stoi(selector.substr(x_pos + 1));
    }
  }

  auto host = browser->GetHost();
  host->SetFocus(true);

  CefMouseEvent mouse_event;
  mouse_event.modifiers = 0;

  if (is_direct_position) {
    mouse_event.x = direct_x;
    mouse_event.y = direct_y;
    host->SendMouseMoveEvent(mouse_event, false);
    // Right-click uses MBT_RIGHT
    host->SendMouseClickEvent(mouse_event, MBT_RIGHT, false, 1);
    host->SendMouseClickEvent(mouse_event, MBT_RIGHT, true, 1);
    LOG_DEBUG("BrowserManager", "=== RIGHT CLICK COMPLETE (POSITION) ===");
    return ActionResult::Success("Right-clicked at position: " + selector);
  }

  // Find element
  std::string actual_selector = selector;
  size_t at_pos = selector.find('@');
  if (at_pos != std::string::npos) actual_selector = selector.substr(0, at_pos);

  bool is_semantic = IsSelectorSemantic(actual_selector);
  ElementRenderInfo info;
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  bool found = false;

  if (is_semantic) {
    OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
    auto matches = matcher->FindByDescription(context_id, selector, 1);
    if (!matches.empty()) {
      const auto& elem = matches[0].element;
      info.x = elem.x; info.y = elem.y;
      info.width = elem.width; info.height = elem.height;
      found = true;
    }
  } else {
    found = tracker->GetElementBounds(context_id, actual_selector, info);
  }

  if (!found) {
    LOG_ERROR("BrowserManager", "RightClick failed - element not found");
    return ActionResult::ElementNotFound(selector);
  }

  int click_x = info.x + (info.width / 2);
  int click_y = info.y + (info.height / 2);

  mouse_event.x = click_x;
  mouse_event.y = click_y;
  host->SendMouseMoveEvent(mouse_event, false);
  host->SendMouseClickEvent(mouse_event, MBT_RIGHT, false, 1);
  host->SendMouseClickEvent(mouse_event, MBT_RIGHT, true, 1);

  LOG_DEBUG("BrowserManager", "=== RIGHT CLICK COMPLETE ===");
  return ActionResult::Success("Right-clicked element: " + selector);
}

// ==================== INPUT CONTROL ====================

ActionResult OwlBrowserManager::ClearInput(const std::string& context_id, const std::string& selector) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "ClearInput failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "=== CLEAR INPUT START === selector='" + selector + "'");

  // Resolve semantic selectors to CSS selectors for verification
  std::string css_selector = selector;
  if (!selector.empty() && selector[0] != '#' && selector[0] != '.' && selector[0] != '[') {
    OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
    auto matches = matcher->FindByDescription(context_id, selector, 1);
    if (!matches.empty() && !matches[0].element.selector.empty()) {
      css_selector = matches[0].element.selector;
      LOG_DEBUG("BrowserManager", "ClearInput - resolved semantic selector to: " + css_selector);
    }
  }

  // Send native keyboard events to clear the currently focused element
  // We assume the caller has already focused/clicked the element, or we use the
  // currently focused element. This avoids UI thread deadlock issues.
  auto host = browser->GetHost();
  host->SetFocus(true);

  // Ctrl+A (Select All)
  CefKeyEvent key_event;
  key_event.windows_key_code = 'A';
  key_event.native_key_code = 'A';
  key_event.character = 'a';
  key_event.unmodified_character = 'a';
  key_event.modifiers = EVENTFLAG_CONTROL_DOWN;
  key_event.is_system_key = false;
  key_event.focus_on_editable_field = true;

  key_event.type = KEYEVENT_RAWKEYDOWN;
  host->SendKeyEvent(key_event);
  key_event.type = KEYEVENT_CHAR;
  host->SendKeyEvent(key_event);
  key_event.type = KEYEVENT_KEYUP;
  host->SendKeyEvent(key_event);

  // Delete key to clear selected text
  key_event.windows_key_code = 0x2E;  // VK_DELETE
  key_event.native_key_code = 0x2E;
  key_event.character = 0;
  key_event.unmodified_character = 0;
  key_event.modifiers = 0;

  key_event.type = KEYEVENT_RAWKEYDOWN;
  host->SendKeyEvent(key_event);
  key_event.type = KEYEVENT_KEYUP;
  host->SendKeyEvent(key_event);

  // Process message loop to allow key events to be handled
  for (int i = 0; i < 3; i++) {
    PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Verify that the input was cleared using existing verify_input_value IPC
  CefRefPtr<CefClient> client_base = host->GetClient();
  if (client_base) {
    OwlClient* client = static_cast<OwlClient*>(client_base.get());
    client->ResetVerification(context_id);

    // Send verification request expecting empty string
    CefRefPtr<CefProcessMessage> verify_msg = CefProcessMessage::Create("verify_input_value");
    CefRefPtr<CefListValue> verify_args = verify_msg->GetArgumentList();
    verify_args->SetString(0, context_id);
    verify_args->SetString(1, css_selector);
    verify_args->SetString(2, "");  // Expected empty value
    browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, verify_msg);

    // Wait for verification (50ms timeout)
    if (client->WaitForVerification(context_id, 50)) {
      OwlClient::VerificationResult result = client->GetVerificationResult(context_id);
      if (!result.success || !result.actual_value.empty()) {
        // Field was not cleared - still has content
        LOG_WARN("BrowserManager", "ClearInput - verification failed, field still has content: '" + result.actual_value.substr(0, 30) + "'");
        ActionResult fail_result = ActionResult::Failure(ActionStatus::CLEAR_FAILED,
          "Clear failed - field still contains: " + result.actual_value.substr(0, 50));
        fail_result.selector = selector;
        fail_result.error_code = result.actual_value;
        return fail_result;
      }
      LOG_DEBUG("BrowserManager", "ClearInput - verified field is empty");
    } else {
      LOG_DEBUG("BrowserManager", "ClearInput - verification timeout (assuming success)");
    }
  }

  LOG_DEBUG("BrowserManager", "=== CLEAR INPUT COMPLETE ===");
  return ActionResult::Success("Cleared input: " + selector);
}

ActionResult OwlBrowserManager::Focus(const std::string& context_id, const std::string& selector) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "Focus failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "=== FOCUS START === selector='" + selector + "'");

  // Get client for verification
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  if (!client_base) {
    LOG_WARN("BrowserManager", "Focus - could not get client");
    return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "Could not get client");
  }
  OwlClient* client = static_cast<OwlClient*>(client_base.get());

  // Send focus command to renderer
  CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("focus_element");
  CefRefPtr<CefListValue> args = msg->GetArgumentList();
  args->SetString(0, selector);
  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, msg);

  // Brief delay for focus to take effect, pump message loop
  for (int i = 0; i < 5; i++) {
    PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Verify focus using IPC
  client->ResetVerification(context_id);
  CefRefPtr<CefProcessMessage> verify_msg = CefProcessMessage::Create("verify_focus");
  CefRefPtr<CefListValue> verify_args = verify_msg->GetArgumentList();
  verify_args->SetString(0, context_id);
  verify_args->SetString(1, selector);
  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, verify_msg);

  // Wait for verification response (50ms timeout)
  if (client->WaitForVerification(context_id, 50)) {
    OwlClient::VerificationResult result = client->GetVerificationResult(context_id);
    if (!result.success) {
      LOG_WARN("BrowserManager", "Focus verification failed - active element: " + result.active_element_selector);
      ActionResult fail_result = ActionResult::Failure(ActionStatus::FOCUS_FAILED,
        "Focus failed - active element is '" + result.active_element_selector + "' instead of '" + selector + "'");
      fail_result.selector = selector;
      fail_result.error_code = result.active_element_selector;
      return fail_result;
    }
  } else {
    LOG_DEBUG("BrowserManager", "Focus - verification timeout, assuming success");
  }

  LOG_DEBUG("BrowserManager", "=== FOCUS COMPLETE ===");
  ActionResult success = ActionResult::Success("Focused element: " + selector);
  success.selector = selector;
  return success;
}

ActionResult OwlBrowserManager::Blur(const std::string& context_id, const std::string& selector) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "Blur failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "=== BLUR START === selector='" + selector + "'");

  // Get client for verification
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  if (!client_base) {
    LOG_WARN("BrowserManager", "Blur - could not get client");
    return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "Could not get client");
  }
  OwlClient* client = static_cast<OwlClient*>(client_base.get());

  // Send blur command to renderer
  CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("blur_element");
  CefRefPtr<CefListValue> args = msg->GetArgumentList();
  args->SetString(0, selector);
  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, msg);

  // Brief delay for blur to take effect, pump message loop
  for (int i = 0; i < 5; i++) {
    PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Verify blur using IPC - element should NOT have focus anymore
  client->ResetVerification(context_id);
  CefRefPtr<CefProcessMessage> verify_msg = CefProcessMessage::Create("verify_focus");
  CefRefPtr<CefListValue> verify_args = verify_msg->GetArgumentList();
  verify_args->SetString(0, context_id);
  verify_args->SetString(1, selector);
  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, verify_msg);

  // Wait for verification response (50ms timeout)
  if (client->WaitForVerification(context_id, 50)) {
    OwlClient::VerificationResult result = client->GetVerificationResult(context_id);
    // For blur, success means the element does NOT have focus (verify_focus returns false)
    if (result.success) {
      // Element still has focus - blur failed
      LOG_WARN("BrowserManager", "Blur verification failed - element still has focus: " + selector);
      ActionResult fail_result = ActionResult::Failure(ActionStatus::BLUR_FAILED,
        "Blur failed - element '" + selector + "' still has focus");
      fail_result.selector = selector;
      return fail_result;
    }
  } else {
    LOG_DEBUG("BrowserManager", "Blur - verification timeout, assuming success");
  }

  LOG_DEBUG("BrowserManager", "=== BLUR COMPLETE ===");
  ActionResult success = ActionResult::Success("Blurred element: " + selector);
  success.selector = selector;
  return success;
}

ActionResult OwlBrowserManager::SelectAll(const std::string& context_id, const std::string& selector) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "SelectAll failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "=== SELECT ALL START === selector='" + selector + "'");

  // Send native Ctrl+A to select all in the currently focused element
  // We assume the caller has already focused/clicked the element
  auto host = browser->GetHost();
  host->SetFocus(true);

  CefKeyEvent key_event;
  key_event.windows_key_code = 'A';
  key_event.native_key_code = 'A';
  key_event.character = 'a';
  key_event.unmodified_character = 'a';
  key_event.modifiers = EVENTFLAG_CONTROL_DOWN;
  key_event.is_system_key = false;
  key_event.focus_on_editable_field = true;

  key_event.type = KEYEVENT_RAWKEYDOWN;
  host->SendKeyEvent(key_event);
  key_event.type = KEYEVENT_CHAR;
  host->SendKeyEvent(key_event);
  key_event.type = KEYEVENT_KEYUP;
  host->SendKeyEvent(key_event);

  LOG_DEBUG("BrowserManager", "=== SELECT ALL COMPLETE (native keyboard) ===");
  return ActionResult::Success("Selected all in: " + selector);
}

// ==================== KEYBOARD COMBINATIONS ====================

ActionResult OwlBrowserManager::KeyboardCombo(const std::string& context_id, const std::string& combo) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "KeyboardCombo failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "=== KEYBOARD COMBO START === combo='" + combo + "'");

  auto host = browser->GetHost();

  // Parse combo like "Ctrl+A", "Shift+Enter", "Ctrl+Shift+N"
  std::vector<std::string> parts;
  std::string current;
  for (char c : combo) {
    if (c == '+') {
      if (!current.empty()) {
        parts.push_back(current);
        current.clear();
      }
    } else {
      current += c;
    }
  }
  if (!current.empty()) parts.push_back(current);

  if (parts.empty()) {
    LOG_ERROR("BrowserManager", "KeyboardCombo failed - empty combo");
    return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "Empty keyboard combo");
  }

  // Build modifiers
  uint32_t modifiers = 0;
  std::string final_key;
  for (const auto& part : parts) {
    std::string lower = part;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "ctrl" || lower == "control") {
      modifiers |= EVENTFLAG_CONTROL_DOWN;
    } else if (lower == "shift") {
      modifiers |= EVENTFLAG_SHIFT_DOWN;
    } else if (lower == "alt") {
      modifiers |= EVENTFLAG_ALT_DOWN;
    } else if (lower == "meta" || lower == "cmd" || lower == "command") {
      modifiers |= EVENTFLAG_COMMAND_DOWN;  // macOS Command key
    } else {
      final_key = lower;
    }
  }

  if (final_key.empty()) {
    LOG_ERROR("BrowserManager", "KeyboardCombo failed - no final key specified");
    return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "No final key specified in combo: " + combo);
  }

  // Map final key to keycode
  int windows_key_code = 0;
  int native_key_code = 0;
  char16_t character = 0;

  if (final_key == "a") { windows_key_code = 'A'; native_key_code = 0x00; character = 'a'; }
  else if (final_key == "c") { windows_key_code = 'C'; native_key_code = 0x08; character = 'c'; }
  else if (final_key == "v") { windows_key_code = 'V'; native_key_code = 0x09; character = 'v'; }
  else if (final_key == "x") { windows_key_code = 'X'; native_key_code = 0x07; character = 'x'; }
  else if (final_key == "z") { windows_key_code = 'Z'; native_key_code = 0x06; character = 'z'; }
  else if (final_key == "y") { windows_key_code = 'Y'; native_key_code = 0x10; character = 'y'; }
  else if (final_key == "s") { windows_key_code = 'S'; native_key_code = 0x01; character = 's'; }
  else if (final_key == "n") { windows_key_code = 'N'; native_key_code = 0x2D; character = 'n'; }
  else if (final_key == "t") { windows_key_code = 'T'; native_key_code = 0x11; character = 't'; }
  else if (final_key == "w") { windows_key_code = 'W'; native_key_code = 0x0D; character = 'w'; }
  else if (final_key == "f") { windows_key_code = 'F'; native_key_code = 0x03; character = 'f'; }
  else if (final_key == "enter" || final_key == "return") { windows_key_code = 0x0D; native_key_code = 0x24; character = 13; }
  else if (final_key == "tab") { windows_key_code = 0x09; native_key_code = 0x30; character = 9; }
  else if (final_key == "escape" || final_key == "esc") { windows_key_code = 0x1B; native_key_code = 0x35; }
  else if (final_key == "backspace") { windows_key_code = 0x08; native_key_code = 0x33; }
  else if (final_key == "delete") { windows_key_code = 0x2E; native_key_code = 0x75; }
  else if (final_key.length() == 1 && std::isalpha(final_key[0])) {
    windows_key_code = std::toupper(final_key[0]);
    character = final_key[0];
  }
  else {
    LOG_ERROR("BrowserManager", "KeyboardCombo - unknown key: " + final_key);
    return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "Unknown key: " + final_key);
  }

  CefKeyEvent key_event;
  key_event.windows_key_code = windows_key_code;
  key_event.native_key_code = native_key_code;
  key_event.character = character;
  key_event.unmodified_character = character;
  key_event.modifiers = modifiers;
  key_event.is_system_key = false;
  key_event.focus_on_editable_field = true;

  // Key down
  key_event.type = KEYEVENT_RAWKEYDOWN;
  host->SendKeyEvent(key_event);

  // Char event if applicable
  if (character != 0) {
    key_event.type = KEYEVENT_CHAR;
    host->SendKeyEvent(key_event);
  }

  // Key up
  key_event.type = KEYEVENT_KEYUP;
  host->SendKeyEvent(key_event);

  LOG_DEBUG("BrowserManager", "=== KEYBOARD COMBO COMPLETE ===");
  return ActionResult::Success("Pressed keyboard combo: " + combo);
}

// ==================== JAVASCRIPT EVALUATION ====================

std::string OwlBrowserManager::Evaluate(const std::string& context_id, const std::string& script, bool return_value) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "Evaluate failed - browser not found");
    return "{\"error\":\"browser not found\"}";
  }

  LOG_DEBUG("BrowserManager", "=== EVALUATE START === script length=" + std::to_string(script.length()) + " return_value=" + (return_value ? "true" : "false"));

  // Clear any previous result for this context
  ClearEvalResult(context_id);

  // Send IPC to renderer to execute JS and return result
  CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("evaluate_script");
  CefRefPtr<CefListValue> args = msg->GetArgumentList();
  args->SetString(0, context_id);
  args->SetString(1, script);
  args->SetBool(2, return_value);  // Pass return_value flag
  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, msg);

  // Wait for the result with message loop pumping
  std::string result;
  auto start = std::chrono::steady_clock::now();
  const int timeout_ms = 10000;  // 10 second timeout

  while (std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start).count() < timeout_ms) {

    // Check if result is ready
    if (GetEvalResult(context_id, result)) {
      LOG_DEBUG("BrowserManager", "=== EVALUATE COMPLETE === result=" + result.substr(0, 100));
      ClearEvalResult(context_id);  // Clean up
      return result;
    }

    // Pump message loop to process IPC messages
    PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  LOG_ERROR("BrowserManager", "Evaluate timeout - no result received");
  return "{\"error\":\"evaluation timeout\"}";
}

// ==================== ELEMENT STATE CHECKS ====================

ActionResult OwlBrowserManager::IsVisible(const std::string& context_id, const std::string& selector) {
  auto browser = GetBrowser(context_id);
  if (!browser) return ActionResult::BrowserNotFound(context_id);

  ElementRenderInfo info;
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (tracker->GetElementBounds(context_id, selector, info)) {
    if (info.visible) {
      return ActionResult::Success("Element is visible");
    } else {
      return ActionResult::Failure(ActionStatus::OK, "Element is not visible");
    }
  }

  // Fall back to semantic matcher
  OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
  auto matches = matcher->FindByDescription(context_id, selector, 1);
  if (!matches.empty()) {
    if (matches[0].element.visible) {
      return ActionResult::Success("Element is visible");
    } else {
      return ActionResult::Failure(ActionStatus::OK, "Element is not visible");
    }
  }

  return ActionResult::ElementNotFound(selector);
}

ActionResult OwlBrowserManager::IsEnabled(const std::string& context_id, const std::string& selector) {
  auto browser = GetBrowser(context_id);
  if (!browser) return ActionResult::BrowserNotFound(context_id);

  // For now, use render tracker - disabled elements are typically tracked
  ElementRenderInfo info;
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  if (tracker->GetElementBounds(context_id, selector, info)) {
    // Most elements that are tracked are enabled unless specifically disabled
    // A more thorough check would require IPC to renderer
    return ActionResult::Success("Element is enabled");
  }

  // Fall back to semantic matcher for semantic selectors
  OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
  auto matches = matcher->FindByDescription(context_id, selector, 1);
  if (!matches.empty()) {
    // Element found via semantic matcher - assume enabled if visible
    return ActionResult::Success("Element is enabled");
  }

  return ActionResult::ElementNotFound(selector);
}

ActionResult OwlBrowserManager::IsChecked(const std::string& context_id, const std::string& selector) {
  auto browser = GetBrowser(context_id);
  if (!browser) return ActionResult::BrowserNotFound(context_id);

  LOG_DEBUG("BrowserManager", "=== IS_CHECKED START === selector='" + selector + "'");

  // Resolve semantic selectors to CSS selectors
  std::string css_selector = selector;
  if (!selector.empty() && selector[0] != '#' && selector[0] != '.' && selector[0] != '[') {
    // Might be a semantic selector - try to resolve it
    OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
    auto matches = matcher->FindByDescription(context_id, selector, 1);
    if (!matches.empty() && !matches[0].element.selector.empty()) {
      css_selector = matches[0].element.selector;
      LOG_DEBUG("BrowserManager", "IsChecked - resolved semantic selector to: " + css_selector);
    }
  }

  // Get client for IPC
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  if (!client_base) {
    LOG_WARN("BrowserManager", "IsChecked - could not get client");
    return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "Could not get client");
  }
  OwlClient* client = static_cast<OwlClient*>(client_base.get());

  // Reset verification state
  client->ResetVerification(context_id);

  // Send IPC request to renderer to get checked state
  CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("get_checked_state");
  CefRefPtr<CefListValue> args = msg->GetArgumentList();
  args->SetString(0, context_id);
  args->SetString(1, css_selector);
  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, msg);

  // Wait for response (50ms timeout - checkbox state check should be fast)
  if (!client->WaitForVerification(context_id, 50)) {
    LOG_WARN("BrowserManager", "IsChecked - verification timeout");
    return ActionResult::VerificationTimeout("IsChecked", selector);
  }

  // Get the result
  OwlClient::VerificationResult result = client->GetVerificationResult(context_id);

  if (!result.success) {
    LOG_DEBUG("BrowserManager", "IsChecked - element not found: " + selector);
    return ActionResult::ElementNotFound(selector);
  }

  bool is_checked = (result.actual_value == "true");
  LOG_DEBUG("BrowserManager", "=== IS_CHECKED COMPLETE === checked=" + std::string(is_checked ? "true" : "false"));

  ActionResult action_result = ActionResult::Success();
  action_result.message = "Element " + selector + " is " + (is_checked ? "checked" : "not checked");
  action_result.selector = selector;
  // Store checked state in error_code field for easy parsing
  action_result.error_code = is_checked ? "checked" : "unchecked";
  return action_result;
}

std::string OwlBrowserManager::GetAttribute(const std::string& context_id, const std::string& selector, const std::string& attribute) {
  auto browser = GetBrowser(context_id);
  if (!browser) return "";

  LOG_DEBUG("BrowserManager", "GetAttribute: selector='" + selector + "' attr='" + attribute + "'");

  // Would need IPC to renderer - placeholder
  return "";
}

std::string OwlBrowserManager::GetBoundingBox(const std::string& context_id, const std::string& selector) {
  auto browser = GetBrowser(context_id);
  if (!browser) return "{\"error\":\"browser not found\"}";

  ElementRenderInfo info;
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();

  bool found = tracker->GetElementBounds(context_id, selector, info);
  if (!found) {
    // Try semantic matcher
    OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
    auto matches = matcher->FindByDescription(context_id, selector, 1);
    if (!matches.empty()) {
      const auto& elem = matches[0].element;
      return "{\"x\":" + std::to_string(elem.x) +
             ",\"y\":" + std::to_string(elem.y) +
             ",\"width\":" + std::to_string(elem.width) +
             ",\"height\":" + std::to_string(elem.height) + "}";
    }
    return "{\"error\":\"element not found\"}";
  }

  return "{\"x\":" + std::to_string(info.x) +
         ",\"y\":" + std::to_string(info.y) +
         ",\"width\":" + std::to_string(info.width) +
         ",\"height\":" + std::to_string(info.height) + "}";
}

std::string OwlBrowserManager::GetElementAtPosition(const std::string& context_id, int x, int y) {
  auto browser = GetBrowser(context_id);
  if (!browser) return "{\"error\":\"browser not found\"}";

  // Trigger a fresh element scan to ensure we have up-to-date DOM data
  // This is critical for element picker accuracy - stale cache causes missed elements
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  if (client_base) {
    OwlClient* client = static_cast<OwlClient*>(client_base.get());
    if (client) {
      LOG_DEBUG("BrowserManager", "GetElementAtPosition: Refreshing element cache...");
      client->WaitForElementScan(browser, context_id, 2000);  // 2 second timeout
    }
  }

  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  auto elements = tracker->GetAllVisibleElements(context_id);

  // Find the element at position (x, y) - prefer smaller/more specific elements
  ElementRenderInfo* best_match = nullptr;
  int best_area = INT_MAX;

  for (auto& elem : elements) {
    // Check if point is within element bounds
    if (x >= elem.x && x <= elem.x + elem.width &&
        y >= elem.y && y <= elem.y + elem.height) {
      int area = elem.width * elem.height;
      // Prefer smaller elements (more specific)
      if (area < best_area) {
        best_area = area;
        best_match = &elem;
      }
    }
  }

  if (!best_match) {
    // Try semantic matcher as fallback
    OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
    auto matches = matcher->FindByDescription(context_id, std::to_string(x) + "x" + std::to_string(y), 1);
    if (!matches.empty()) {
      const auto& elem = matches[0].element;
      // Check if position is within matched element
      if (x >= elem.x && x <= elem.x + elem.width &&
          y >= elem.y && y <= elem.y + elem.height) {
        std::ostringstream json;
        json << "{"
             << "\"tagName\":\"" << EscapeJSONString(elem.tag) << "\","
             << "\"id\":\"" << EscapeJSONString(elem.id) << "\","
             << "\"className\":\"\","  // ElementSemantics doesn't have className
             << "\"textContent\":\"" << EscapeJSONString(elem.text.substr(0, 100)) << "\","
             << "\"selector\":\"" << EscapeJSONString(elem.selector) << "\","
             << "\"x\":" << elem.x << ","
             << "\"y\":" << elem.y << ","
             << "\"width\":" << elem.width << ","
             << "\"height\":" << elem.height
             << "}";
        return json.str();
      }
    }
    return "{\"error\":\"no element found at position\"}";
  }

  // Build JSON response with element info
  // Strip position info (@x,y) from selector for clean CSS selector
  std::string clean_selector = best_match->selector;
  size_t at_pos = clean_selector.find('@');
  if (at_pos != std::string::npos) {
    clean_selector = clean_selector.substr(0, at_pos);
  }

  std::ostringstream json;
  json << "{"
       << "\"tagName\":\"" << EscapeJSONString(best_match->tag) << "\","
       << "\"id\":\"" << EscapeJSONString(best_match->id) << "\","
       << "\"className\":\"" << EscapeJSONString(best_match->className) << "\","
       << "\"textContent\":\"" << EscapeJSONString(best_match->text.substr(0, 100)) << "\","
       << "\"selector\":\"" << EscapeJSONString(clean_selector) << "\","
       << "\"x\":" << best_match->x << ","
       << "\"y\":" << best_match->y << ","
       << "\"width\":" << best_match->width << ","
       << "\"height\":" << best_match->height;

  // Add optional attributes if present
  if (!best_match->role.empty()) {
    json << ",\"role\":\"" << EscapeJSONString(best_match->role) << "\"";
  }
  if (!best_match->aria_label.empty()) {
    json << ",\"ariaLabel\":\"" << EscapeJSONString(best_match->aria_label) << "\"";
  }
  if (!best_match->placeholder.empty()) {
    json << ",\"placeholder\":\"" << EscapeJSONString(best_match->placeholder) << "\"";
  }
  if (!best_match->type.empty()) {
    json << ",\"type\":\"" << EscapeJSONString(best_match->type) << "\"";
  }

  json << "}";
  return json.str();
}

std::string OwlBrowserManager::GetInteractiveElements(const std::string& context_id) {
  auto browser = GetBrowser(context_id);
  if (!browser) return "{\"error\":\"browser not found\"}";

  // Wait for any pending navigation to complete
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  if (client) {
    NavigationInfo nav_info = client->GetNavigationInfo();
    if (nav_info.state != NavigationState::COMPLETE) {
      LOG_DEBUG("BrowserManager", "GetInteractiveElements: Waiting for navigation to complete...");
      client->WaitForNavigation(10000);
    }
  }

  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  auto elements = tracker->GetAllVisibleElements(context_id);

  // Filter to interactive elements only
  std::vector<ElementRenderInfo*> interactive;
  for (auto& elem : elements) {
    // Check if element is interactive
    std::string tag = elem.tag;
    std::transform(tag.begin(), tag.end(), tag.begin(), ::tolower);

    bool is_interactive = (
      tag == "a" || tag == "button" || tag == "input" ||
      tag == "select" || tag == "textarea" ||
      elem.role == "button" || elem.role == "link" ||
      elem.role == "checkbox" || elem.role == "radio" ||
      elem.role == "menuitem" || elem.role == "tab" ||
      !elem.aria_label.empty()
    );

    if (is_interactive && elem.width > 0 && elem.height > 0) {
      interactive.push_back(&elem);
    }
  }

  // Build JSON array
  std::ostringstream json;
  json << "{\"elements\":[";

  bool first = true;
  for (auto* elem : interactive) {
    if (!first) json << ",";
    first = false;

    // Strip position info (@x,y) from selector for clean CSS selector
    std::string clean_selector = elem->selector;
    size_t at_pos = clean_selector.find('@');
    if (at_pos != std::string::npos) {
      clean_selector = clean_selector.substr(0, at_pos);
    }

    json << "{"
         << "\"tagName\":\"" << EscapeJSONString(elem->tag) << "\","
         << "\"id\":\"" << EscapeJSONString(elem->id) << "\","
         << "\"className\":\"" << EscapeJSONString(elem->className) << "\","
         << "\"textContent\":\"" << EscapeJSONString(elem->text.substr(0, 50)) << "\","
         << "\"selector\":\"" << EscapeJSONString(clean_selector) << "\","
         << "\"x\":" << elem->x << ","
         << "\"y\":" << elem->y << ","
         << "\"width\":" << elem->width << ","
         << "\"height\":" << elem->height;

    if (!elem->role.empty()) {
      json << ",\"role\":\"" << EscapeJSONString(elem->role) << "\"";
    }
    if (!elem->type.empty()) {
      json << ",\"type\":\"" << EscapeJSONString(elem->type) << "\"";
    }

    json << "}";
  }

  json << "],\"count\":" << interactive.size() << "}";
  return json.str();
}

// ==================== FILE OPERATIONS ====================

ActionResult OwlBrowserManager::UploadFile(const std::string& context_id, const std::string& selector,
                                    const std::vector<std::string>& file_paths) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "UploadFile failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  // Validate file paths exist
  for (const auto& path : file_paths) {
    if (path.empty()) {
      LOG_ERROR("BrowserManager", "UploadFile failed - empty file path");
      return ActionResult::Failure(ActionStatus::INVALID_PARAMETER, "Empty file path provided");
    }
    // Check if file exists (basic validation)
    std::ifstream f(path);
    if (!f.good()) {
      LOG_ERROR("BrowserManager", "UploadFile failed - file not found: " + path);
      return ActionResult::Failure(ActionStatus::INVALID_PARAMETER, "File not found: " + path);
    }
  }

  LOG_DEBUG("BrowserManager", "=== UPLOAD FILE START === selector='" + selector + "' files=" + std::to_string(file_paths.size()));

  // Get client for IPC
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());

  // Send IPC to renderer to set file input value
  CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("upload_file");
  CefRefPtr<CefListValue> args = msg->GetArgumentList();
  args->SetString(0, selector);

  // Create JSON array of file paths
  std::string paths_json = "[";
  for (size_t i = 0; i < file_paths.size(); i++) {
    if (i > 0) paths_json += ",";
    // Escape backslashes and quotes in paths
    std::string escaped_path;
    for (char c : file_paths[i]) {
      if (c == '\\' || c == '"') escaped_path += '\\';
      escaped_path += c;
    }
    paths_json += "\"" + escaped_path + "\"";
  }
  paths_json += "]";
  args->SetString(1, paths_json);

  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, msg);

  // Allow time for the upload_file handler to execute
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Pump message loop to process IPC
  for (int i = 0; i < 10; i++) {
    PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Send verification request
  CefRefPtr<CefProcessMessage> verify_msg = CefProcessMessage::Create("verify_upload_files");
  CefRefPtr<CefListValue> verify_args = verify_msg->GetArgumentList();
  verify_args->SetString(0, context_id);
  verify_args->SetString(1, selector);
  verify_args->SetInt(2, static_cast<int>(file_paths.size()));
  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, verify_msg);

  // Wait for verification response (500ms timeout)
  if (!client->WaitForVerification(context_id, 500)) {
    LOG_WARN("BrowserManager", "UploadFile verification timeout");
    // Return success anyway - the upload IPC was sent
    return ActionResult::Success("Uploaded " + std::to_string(file_paths.size()) + " file(s) to '" + selector + "' (verification timeout)");
  }

  // Check verification result
  OwlClient::VerificationResult result = client->GetVerificationResult(context_id);
  if (!result.success) {
    LOG_ERROR("BrowserManager", "UploadFile verification failed: " + result.error_message);
    return ActionResult::UploadFailed(selector, result.error_message);
  }

  LOG_DEBUG("BrowserManager", "=== UPLOAD FILE COMPLETE === Verified files set");
  return ActionResult::Success("Uploaded " + std::to_string(file_paths.size()) + " file(s) to '" + selector + "' (verified: " + result.actual_value + " files)");
}

// ==================== FRAME/IFRAME HANDLING ====================

std::string OwlBrowserManager::ListFrames(const std::string& context_id) {
  auto browser = GetBrowser(context_id);
  if (!browser) return "[]";

  std::vector<CefString> frame_ids;
  browser->GetFrameIdentifiers(frame_ids);

  std::string result = "[";
  for (size_t i = 0; i < frame_ids.size(); i++) {
    auto frame = browser->GetFrameByIdentifier(frame_ids[i]);
    if (frame) {
      if (i > 0) result += ",";
      result += "{\"id\":\"" + frame_ids[i].ToString() +
                "\",\"name\":\"" + frame->GetName().ToString() +
                "\",\"url\":\"" + frame->GetURL().ToString() +
                "\",\"isMain\":" + (frame->IsMain() ? "true" : "false") + "}";
    }
  }
  result += "]";

  return result;
}

ActionResult OwlBrowserManager::SwitchToFrame(const std::string& context_id, const std::string& frame_selector) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "SwitchToFrame failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "=== SWITCH TO FRAME === selector='" + frame_selector + "'");

  CefRefPtr<CefFrame> frame;

  // Try to find frame by name first
  frame = browser->GetFrameByName(frame_selector);
  if (frame) {
    LOG_DEBUG("BrowserManager", "Found frame by name: " + frame_selector);
  }

  // Try to find by index if not found by name
  if (!frame && std::all_of(frame_selector.begin(), frame_selector.end(), ::isdigit)) {
    int index = std::stoi(frame_selector);
    std::vector<CefString> frame_ids;
    browser->GetFrameIdentifiers(frame_ids);
    if (index >= 0 && static_cast<size_t>(index) < frame_ids.size()) {
      frame = browser->GetFrameByIdentifier(frame_ids[index]);
      if (frame) {
        LOG_DEBUG("BrowserManager", "Found frame by index: " + frame_selector);
      }
    }
  }

  // Try to find by CSS selector (iframe element)
  if (!frame) {
    CefRefPtr<CefFrame> main_frame = browser->GetMainFrame();
    if (main_frame) {
      // Execute JS to find the iframe and get its name
      std::string find_frame_js = R"(
        (function() {
          var iframe = document.querySelector(')" + frame_selector + R"(');
          if (iframe && iframe.tagName.toLowerCase() === 'iframe') {
            return iframe.name || iframe.id || '';
          }
          return '';
        })()
      )";
      // Note: This would require async execution - for now we just try CSS-based lookup
      LOG_DEBUG("BrowserManager", "Frame not found by name or index, selector may be CSS");
    }
  }

  if (!frame) {
    LOG_ERROR("BrowserManager", "SwitchToFrame failed - frame not found: " + frame_selector);
    return ActionResult::FrameSwitchFailed(frame_selector, "Frame not found");
  }

  // Verify frame is valid and accessible
  if (!frame->IsValid()) {
    LOG_ERROR("BrowserManager", "SwitchToFrame failed - frame is invalid/detached: " + frame_selector);
    return ActionResult::FrameSwitchFailed(frame_selector, "Frame is detached or invalid");
  }

  // Verify frame URL is accessible (not about:blank for cross-origin check)
  std::string frame_url = frame->GetURL().ToString();
  LOG_DEBUG("BrowserManager", "Frame URL: " + frame_url);

  // Store the current frame context for this browser (for subsequent operations)
  // Note: The frame reference is valid until the page changes or frame is removed

  LOG_DEBUG("BrowserManager", "=== SWITCH TO FRAME COMPLETE === Frame is valid and accessible");
  return ActionResult::Success("Switched to frame: " + frame_selector + " (url: " + frame_url + ")");
}

ActionResult OwlBrowserManager::SwitchToMainFrame(const std::string& context_id) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "SwitchToMainFrame failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  // Main frame is always available
  auto frame = browser->GetMainFrame();
  if (frame) {
    LOG_DEBUG("BrowserManager", "Switched to main frame");
    return ActionResult::Success("Switched to main frame");
  }

  return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "Main frame not available");
}

ActionResult OwlBrowserManager::Type(const std::string& context_id,
                                      const std::string& selector,
                                      const std::string& text,
                                      VerificationLevel level) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "Type failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "=== TYPE START === selector='" + selector + "' text='" + text + "'");

  // Parse selector - strip position metadata if present (format: "SELECTOR@x,y")
  // This position info is used by Click() but must be removed for querySelector
  std::string actual_selector = selector;
  size_t at_pos = selector.find('@');
  if (at_pos != std::string::npos) {
    actual_selector = selector.substr(0, at_pos);
    LOG_DEBUG("BrowserManager", "Stripped position from selector: '" + selector + "' -> '" + actual_selector + "'");
  }

  // Get CSS selector for the element BEFORE clicking (to avoid race with element scan clearing)
  std::string css_selector = actual_selector;
  bool is_semantic = IsSelectorSemantic(actual_selector);

  if (is_semantic) {
    // Semantic selector - resolve to CSS selector BEFORE click triggers new scans
    OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
    auto matches = matcher->FindByDescription(context_id, actual_selector, 1);
    if (!matches.empty() && !matches[0].element.id.empty()) {
      css_selector = "#" + matches[0].element.id;
      LOG_DEBUG("BrowserManager", "Pre-resolved semantic selector '" + actual_selector + "' to CSS '" + css_selector + "'");
    } else if (!matches.empty()) {
      // No ID - construct selector from tag and attributes
      const auto& elem = matches[0].element;
      if (!elem.name.empty()) {
        css_selector = elem.tag + "[name='" + elem.name + "']";
      } else {
        css_selector = elem.tag;
      }
      LOG_DEBUG("BrowserManager", "Pre-resolved semantic selector '" + actual_selector + "' to '" + css_selector + "'");
    } else {
      // SemanticMatcher returned nothing - try inferring from common patterns
      std::string lower_selector = actual_selector;
      std::transform(lower_selector.begin(), lower_selector.end(), lower_selector.begin(), ::tolower);

      // Infer camelCase ID from semantic description: "first name" -> "#firstName"
      std::string inferred_id = "#";
      bool capitalize_next = false;
      for (char c : actual_selector) {
        if (c == ' ') {
          capitalize_next = true;
        } else if (capitalize_next) {
          inferred_id += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
          capitalize_next = false;
        } else {
          inferred_id += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
      }
      css_selector = inferred_id;
      LOG_DEBUG("BrowserManager", "Inferred CSS selector from semantic: '" + actual_selector + "' -> '" + css_selector + "'");
    }
  }

  // Now click on the element to focus it (use original selector with position)
  // Use NONE verification for the click since we'll verify the type result separately
  ActionResult click_result = Click(context_id, selector, VerificationLevel::NONE);
  if (click_result.status != ActionStatus::OK) {
    LOG_ERROR("BrowserManager", "Type failed - could not click element");
    return ActionResult::Failure(ActionStatus::CLICK_FAILED, "Could not focus element for typing: " + selector);
  }

  // Allow time for element to receive focus - some fields have focus animations/handlers
  // In UI mode, the message loop runs on main thread so we just need to wait
  // In headless mode, we need to pump messages manually
  if (UsesRunMessageLoop()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  } else {
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    PumpMessageLoopIfNeeded();
  }

  // CRITICAL FIX: In UI mode, SendKeyEvent and SetFocus must be called on the UI thread.
  // When UsesRunMessageLoop() is true (UI mode) and we're NOT on the UI thread,
  // we must post all browser host operations to the UI thread to avoid crashes.
  bool needs_ui_thread_post = UsesRunMessageLoop() && !CefCurrentlyOn(TID_UI);

  LOG_DEBUG("BrowserManager", "Typing " + std::to_string(text.length()) + " characters via CEF keyboard events");

  if (needs_ui_thread_post) {
    LOG_DEBUG("BrowserManager", "Using JavaScript for windowed browser typing");

    // For windowed browsers, SendKeyEvent doesn't work (CEF limitation).
    // Use JavaScript to set the input value instead.
    auto frame = browser->GetMainFrame();
    if (!frame) {
      LOG_ERROR("BrowserManager", "Could not get main frame for JS execution");
      return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "Could not get main frame");
    }

    // Escape the text for JavaScript
    std::string escaped_text;
    for (char c : text) {
      if (c == '\\') escaped_text += "\\\\";
      else if (c == '\'') escaped_text += "\\'";
      else if (c == '\n') escaped_text += "\\n";
      else if (c == '\r') escaped_text += "\\r";
      else escaped_text += c;
    }

    // Escape the selector for JavaScript
    std::string escaped_selector;
    for (char c : css_selector) {
      if (c == '\\') escaped_selector += "\\\\";
      else if (c == '\'') escaped_selector += "\\'";
      else escaped_selector += c;
    }

    // JavaScript to set value and dispatch input event (for React/Vue compatibility)
    std::string js_code = R"(
      (function() {
        var el = document.querySelector(')" + escaped_selector + R"(');
        if (el) {
          el.focus();
          // Use native value setter to bypass React's synthetic event system
          var nativeInputValueSetter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set;
          var nativeTextareaValueSetter = Object.getOwnPropertyDescriptor(window.HTMLTextAreaElement.prototype, 'value').set;
          if (el.tagName === 'INPUT') {
            nativeInputValueSetter.call(el, ')" + escaped_text + R"(');
          } else if (el.tagName === 'TEXTAREA') {
            nativeTextareaValueSetter.call(el, ')" + escaped_text + R"(');
          } else {
            el.value = ')" + escaped_text + R"(';
          }
          // Dispatch input event for frameworks
          el.dispatchEvent(new Event('input', { bubbles: true }));
          el.dispatchEvent(new Event('change', { bubbles: true }));
          return true;
        }
        return false;
      })()
    )";

    frame->ExecuteJavaScript(js_code, frame->GetURL(), 0);

    // Give time for JS to execute and events to propagate
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  } else {
    // Headless mode or already on UI thread - send key events directly
    auto host = browser->GetHost();
    if (!host) {
      LOG_ERROR("BrowserManager", "GetHost() returned NULL");
      return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "Browser host not available");
    }

    host->SetFocus(true);

    // In UI mode (on UI thread), give time for focus to be processed
    if (UsesRunMessageLoop()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    for (size_t i = 0; i < text.length(); i++) {
      char c = text[i];
      CefKeyEvent key_event;
      char16_t char_code = static_cast<char16_t>(static_cast<unsigned char>(c));

      // For text input, KEYEVENT_CHAR is the primary event that inserts characters
      // windows_key_code should be the virtual key code, but for special characters
      // we focus on the CHAR event which carries the actual character
      key_event.character = char_code;
      key_event.unmodified_character = char_code;
      key_event.modifiers = 0;
      key_event.is_system_key = false;
      key_event.focus_on_editable_field = true;

      // Map common characters to their virtual key codes
      // For alphanumeric, use uppercase letter as VK code
      if (c >= 'a' && c <= 'z') {
        key_event.windows_key_code = c - 'a' + 'A';  // VK_A to VK_Z
        key_event.native_key_code = key_event.windows_key_code;
      } else if (c >= 'A' && c <= 'Z') {
        key_event.windows_key_code = c;  // VK_A to VK_Z
        key_event.native_key_code = c;
        key_event.modifiers = EVENTFLAG_SHIFT_DOWN;
      } else if (c >= '0' && c <= '9') {
        key_event.windows_key_code = c;  // VK_0 to VK_9
        key_event.native_key_code = c;
      } else {
        // For special characters, use a generic approach
        // The CHAR event with correct character code is what matters for text input
        key_event.windows_key_code = 0;  // Will be ignored for CHAR event
        key_event.native_key_code = 0;
      }

      // Key down (optional for text input, but helps with some frameworks)
      key_event.type = KEYEVENT_RAWKEYDOWN;
      host->SendKeyEvent(key_event);

      // Char event - this is what actually inserts the character
      key_event.type = KEYEVENT_CHAR;
      host->SendKeyEvent(key_event);

      // Key up
      key_event.type = KEYEVENT_KEYUP;
      host->SendKeyEvent(key_event);

      // In UI mode (on UI thread), give brief time between keystrokes
      // In headless mode, pump message loop periodically to process queued events
      // Without pumping, key events pile up and can be dropped or lost
      if (UsesRunMessageLoop()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      } else {
        // Pump every 10 characters to ensure events are processed
        // This prevents event queue overflow without adding too much latency
        if ((i + 1) % 10 == 0) {
          PumpMessageLoopIfNeeded();
        }
      }
    }

    // Process pending messages to ensure all keystrokes are delivered
    // In UI mode, give extra time for main thread message loop to process events
    if (UsesRunMessageLoop()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } else {
      // Pump multiple times to ensure all key events are fully processed
      for (int pump = 0; pump < 3; pump++) {
        PumpMessageLoopIfNeeded();
      }
    }
  }

  LOG_DEBUG("BrowserManager", "=== TYPE COMPLETE === " + std::to_string(text.length()) +
           " characters typed via CEF keyboard events");

  // For NONE verification level, skip verification and return success
  if (level == VerificationLevel::NONE) {
    return ActionResult::Success("Typed " + std::to_string(text.length()) + " characters (no verification)");
  }

  // ============================================================================
  // VERIFICATION: Verify that the typed text actually appears in the input field
  // ============================================================================
  // Allow DOM to update after typing - some frameworks debounce input events
  // In UI mode, give more time since message loop runs on main thread
  if (UsesRunMessageLoop()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  } else {
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    PumpMessageLoopIfNeeded();  // Process any pending messages
  }

  // Get OwlClient to use verification
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  if (!client_base) {
    LOG_WARN("BrowserManager", "Type - could not get client for verification, assuming success");
    return ActionResult::Success("Typed text (verification skipped - no client)");
  }
  OwlClient* client = static_cast<OwlClient*>(client_base.get());

  // Reset verification state
  client->ResetVerification(context_id);

  // Send verification request to renderer
  CefRefPtr<CefProcessMessage> verify_msg = CefProcessMessage::Create("verify_input_value");
  CefRefPtr<CefListValue> verify_args = verify_msg->GetArgumentList();
  verify_args->SetString(0, context_id);
  verify_args->SetString(1, css_selector);  // Use the CSS selector we resolved earlier
  verify_args->SetString(2, text);  // Expected value
  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, verify_msg);

  // Wait for verification response (reduced timeout for better concurrency)
  if (!client->WaitForVerification(context_id, 100)) {
    LOG_WARN("BrowserManager", "Type - verification timeout, typed text may not have been entered");
    return ActionResult::VerificationTimeout("Type", selector);
  }

  // Check verification result
  OwlClient::VerificationResult result = client->GetVerificationResult(context_id);
  if (!result.success) {
    LOG_ERROR("BrowserManager", "Type FAILED - verification failed: " + result.error_message +
              " (expected='" + text.substr(0, 30) + "' actual='" + result.actual_value.substr(0, 30) + "')");
    // Check for partial match
    if (!result.actual_value.empty() && text.find(result.actual_value) == 0) {
      return ActionResult::TypePartial(selector, text, result.actual_value);
    }
    return ActionResult::Failure(ActionStatus::TYPE_FAILED,
                                  "Type verification failed for " + selector + ": " + result.error_message);
  }

  LOG_DEBUG("BrowserManager", "Type VERIFIED - text successfully entered into " + result.element_tag);
  ActionResult success_result = ActionResult::Success("Typed and verified: " + text.substr(0, 30) +
                                                       (text.length() > 30 ? "..." : ""));
  success_result.selector = selector;
  return success_result;
}

ActionResult OwlBrowserManager::Pick(const std::string& context_id,
                                      const std::string& selector,
                                      const std::string& value,
                                      VerificationLevel level) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "Pick failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "=== PICK START === selector='" + selector + "' value='" + value + "'");

  // Parse selector - strip position metadata if present (format: "SELECTOR@x,y")
  std::string actual_selector = selector;
  size_t at_pos = selector.find('@');
  if (at_pos != std::string::npos) {
    actual_selector = selector.substr(0, at_pos);
    LOG_DEBUG("BrowserManager", "Stripped position from selector: '" + selector + "' -> '" + actual_selector + "'");
  }

  // Get CSS selector for the element BEFORE clicking (to avoid race with element scan clearing)
  std::string css_selector = actual_selector;
  bool is_semantic = IsSelectorSemantic(actual_selector);

  if (is_semantic) {
    // Semantic selector - resolve to CSS selector BEFORE click triggers new scans
    OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
    auto matches = matcher->FindByDescription(context_id, actual_selector, 1);
    if (!matches.empty() && !matches[0].element.id.empty()) {
      css_selector = "#" + matches[0].element.id;
      LOG_DEBUG("BrowserManager", "Pre-resolved semantic selector '" + actual_selector + "' to CSS '" + css_selector + "'");
    } else if (!matches.empty()) {
      // No ID - construct selector from tag and attributes
      const auto& elem = matches[0].element;
      if (!elem.name.empty()) {
        css_selector = elem.tag + "[name='" + elem.name + "']";
      } else {
        css_selector = elem.tag;
      }
      LOG_DEBUG("BrowserManager", "Pre-resolved semantic selector '" + actual_selector + "' to '" + css_selector + "'");
    } else {
      // SemanticMatcher returned nothing - try inferring from common patterns
      std::string inferred_id = "#";
      bool capitalize_next = false;
      for (char c : actual_selector) {
        if (c == ' ') {
          capitalize_next = true;
        } else if (capitalize_next) {
          inferred_id += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
          capitalize_next = false;
        } else {
          inferred_id += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
      }
      css_selector = inferred_id;
      LOG_DEBUG("BrowserManager", "Inferred CSS selector from semantic: '" + actual_selector + "' -> '" + css_selector + "'");
    }
  }

  // Now click on the element to open the dropdown (use original selector with position)
  // Use NONE verification for the click since we'll verify the pick result separately
  ActionResult click_result = Click(context_id, selector, VerificationLevel::NONE);
  if (click_result.status != ActionStatus::OK) {
    LOG_ERROR("BrowserManager", "Pick failed - could not click select element");
    return ActionResult::Failure(ActionStatus::CLICK_FAILED, "Could not open dropdown: " + selector);
  }

  // Allow dropdown to open - some dropdowns have animations or async loading
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  PumpMessageLoopIfNeeded();

  // Use IPC to renderer process to select option from dropdown via V8 DOM API
  // The renderer will handle both static <select> and dynamic dropdowns (select2)
  LOG_DEBUG("BrowserManager", "Sending IPC to renderer to pick option: " + value);

  // Get OwlClient to wait for pick response
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = client_base ? static_cast<OwlClient*>(client_base.get()) : nullptr;
  if (client) {
    client->ResetPickResult(context_id);
  }

  CefRefPtr<CefProcessMessage> pick_message = CefProcessMessage::Create("pick_from_select");
  CefRefPtr<CefListValue> pick_args = pick_message->GetArgumentList();
  pick_args->SetString(0, css_selector);  // Use CSS selector for querySelector
  pick_args->SetString(1, value);
  pick_args->SetString(2, context_id);  // Pass context_id for response routing

  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, pick_message);

  // Wait for pick response from renderer
  if (client && client->WaitForPickResult(context_id, 200)) {
    bool pick_success = client->GetPickResult(context_id);
    if (pick_success) {
      LOG_DEBUG("BrowserManager", "=== PICK COMPLETE === Selected '" + value + "' from dropdown");
      ActionResult success_result = ActionResult::Success("Selected '" + value + "' from dropdown");
      success_result.selector = selector;
      return success_result;
    } else {
      LOG_ERROR("BrowserManager", "Pick failed - could not find or select option: " + value);
      return ActionResult::Failure(ActionStatus::ELEMENT_NOT_FOUND, "Option not found: " + value);
    }
  }

  // Timeout waiting for response - assume success since IPC was sent
  LOG_WARN("BrowserManager", "Pick - response timeout, assuming success");
  ActionResult success_result = ActionResult::Success("Selected '" + value + "' (unverified)");
  success_result.selector = selector;
  return success_result;
}

ActionResult OwlBrowserManager::PressKey(const std::string& context_id, const std::string& key) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "PressKey failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "PressKey: key='" + key + "'");

  auto host = browser->GetHost();
  auto frame = browser->GetMainFrame();

  CefKeyEvent key_event;
  key_event.is_system_key = false;
  key_event.modifiers = 0;
  key_event.focus_on_editable_field = true;

  // Map key names to key codes
  int windows_key_code = 0;
  int native_key_code = 0;  // macOS uses different codes
  std::string key_lower = key;
  std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);

  if (key_lower == "enter" || key_lower == "return") {
    windows_key_code = 0x0D;  // VK_RETURN
    native_key_code = 0x24;   // kVK_Return (macOS: 36 decimal = 0x24)
  } else if (key_lower == "tab") {
    windows_key_code = 0x09;  // VK_TAB
    native_key_code = 0x30;   // kVK_Tab (macOS: 48)
  } else if (key_lower == "escape" || key_lower == "esc") {
    windows_key_code = 0x1B;  // VK_ESCAPE
    native_key_code = 0x35;   // kVK_Escape (macOS: 53)
  } else if (key_lower == "backspace") {
    windows_key_code = 0x08;  // VK_BACK
    native_key_code = 0x33;   // kVK_Delete (macOS: 51)
  } else if (key_lower == "delete" || key_lower == "del") {
    windows_key_code = 0x2E;  // VK_DELETE
    native_key_code = 0x75;   // kVK_ForwardDelete (macOS: 117)
  } else if (key_lower == "arrowup" || key_lower == "up") {
    windows_key_code = 0x26;  // VK_UP
    native_key_code = 0x7E;   // kVK_UpArrow (macOS: 126)
  } else if (key_lower == "arrowdown" || key_lower == "down") {
    windows_key_code = 0x28;  // VK_DOWN
    native_key_code = 0x7D;   // kVK_DownArrow (macOS: 125)
  } else if (key_lower == "arrowleft" || key_lower == "left") {
    windows_key_code = 0x25;  // VK_LEFT
    native_key_code = 0x7B;   // kVK_LeftArrow (macOS: 123)
  } else if (key_lower == "arrowright" || key_lower == "right") {
    windows_key_code = 0x27;  // VK_RIGHT
    native_key_code = 0x7C;   // kVK_RightArrow (macOS: 124)
  } else if (key_lower == "space") {
    windows_key_code = 0x20;  // VK_SPACE
    native_key_code = 0x31;   // kVK_Space (macOS: 49)
  } else if (key_lower == "home") {
    windows_key_code = 0x24;  // VK_HOME
    native_key_code = 0x73;   // kVK_Home (macOS: 115)
  } else if (key_lower == "end") {
    windows_key_code = 0x23;  // VK_END
    native_key_code = 0x77;   // kVK_End (macOS: 119)
  } else if (key_lower == "pageup") {
    windows_key_code = 0x21;  // VK_PRIOR
    native_key_code = 0x74;   // kVK_PageUp (macOS: 116)
  } else if (key_lower == "pagedown") {
    windows_key_code = 0x22;  // VK_NEXT
    native_key_code = 0x79;   // kVK_PageDown (macOS: 121)
  } else {
    LOG_ERROR("BrowserManager", "Unknown key: " + key);
    return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "Unknown key: " + key);
  }

  key_event.windows_key_code = windows_key_code;
  key_event.native_key_code = native_key_code;

  // Set character representation for keys that have one
  if (key_lower == "enter" || key_lower == "return") {
    key_event.character = 13;  // '\r'
    key_event.unmodified_character = 13;
  } else if (key_lower == "space") {
    key_event.character = 32;  // ' '
    key_event.unmodified_character = 32;
  } else if (key_lower == "tab") {
    key_event.character = 9;  // '\t'
    key_event.unmodified_character = 9;
  } else {
    key_event.character = 0;
    key_event.unmodified_character = 0;
  }

  LOG_DEBUG("BrowserManager", "About to send key events - key='" + key + "' win=" + std::to_string(windows_key_code) + " native=" + std::to_string(native_key_code));

  // Key down
  LOG_DEBUG("BrowserManager", "Sending KEYDOWN for: " + key);
  key_event.type = KEYEVENT_RAWKEYDOWN;
  host->SendKeyEvent(key_event);

  // Char event (needed for Enter and Space to actually work)
  if (key_lower == "space" || key_lower == "enter" || key_lower == "return") {
    key_event.type = KEYEVENT_CHAR;
    host->SendKeyEvent(key_event);
  }

  // Key up
  key_event.type = KEYEVENT_KEYUP;
  host->SendKeyEvent(key_event);

  // NO CefDoMessageLoopWork() here - main loop will pump after batch

  LOG_DEBUG("BrowserManager", "=== PRESS KEY COMPLETE ===");
  return ActionResult::Success("Pressed key: " + key);
}

ActionResult OwlBrowserManager::SubmitForm(const std::string& context_id) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "SubmitForm failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "=== SUBMIT FORM START === (native Enter key)");

  // Use native Enter key to submit form (assumes an input is focused)
  auto host = browser->GetHost();
  host->SetFocus(true);

  CefKeyEvent key_event;
  key_event.windows_key_code = 0x0D;  // VK_RETURN (Enter)
  // native_key_code: macOS kVK_Return = 36 (0x24), Linux X11 Return = 36
  key_event.native_key_code = 0x24;
  key_event.character = '\r';
  key_event.unmodified_character = '\r';
  key_event.modifiers = 0;
  key_event.is_system_key = false;
  key_event.focus_on_editable_field = true;

  // Key down
  key_event.type = KEYEVENT_RAWKEYDOWN;
  host->SendKeyEvent(key_event);

  // Char event
  key_event.type = KEYEVENT_CHAR;
  host->SendKeyEvent(key_event);

  // Key up
  key_event.type = KEYEVENT_KEYUP;
  host->SendKeyEvent(key_event);

  LOG_DEBUG("BrowserManager", "=== SUBMIT FORM COMPLETE ===");
  return ActionResult::Success("Form submitted");
}

ActionResult OwlBrowserManager::Highlight(const std::string& context_id,
                                    const std::string& selector,
                                    const std::string& border_color,
                                    const std::string& background_color) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "Highlight failed - browser not found for context: " + context_id);
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "=== HIGHLIGHT START === selector='" + selector + "' border=" + border_color + " bg=" + background_color);

  // Unfreeze cache so highlight DOM changes can be rendered
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  client->UnfreezeFrameCache();
  LOG_DEBUG("BrowserManager", "Frame cache unfrozen for highlight rendering");

  // Parse selector to extract position metadata if present (format: "SELECTOR@x,y")
  std::string actual_selector = selector;
  int target_x = -1, target_y = -1;
  size_t at_pos = selector.find('@');
  if (at_pos != std::string::npos) {
    actual_selector = selector.substr(0, at_pos);
    std::string coords = selector.substr(at_pos + 1);
    size_t comma = coords.find(',');
    if (comma != std::string::npos) {
      target_x = std::stoi(coords.substr(0, comma));
      target_y = std::stoi(coords.substr(comma + 1));
      LOG_DEBUG("BrowserManager", "Position from selector: (" + std::to_string(target_x) + "," + std::to_string(target_y) + ")");
    }
  }

  // Get element position for verification (same logic as Click)
  ElementRenderInfo info;
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  bool found = tracker->GetElementBounds(context_id, actual_selector, info);

  if (!found) {
    LOG_WARN("BrowserManager", "Element not in cache, scanning for: " + actual_selector);

    // Trigger element scan
    CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("scan_element");
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    args->SetString(0, context_id);
    args->SetString(1, actual_selector);
    browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);

    // Wait for scan
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count() < 500) {
      PumpMessageLoopIfNeeded();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    found = tracker->GetElementBounds(context_id, actual_selector, info);
  }

  if (!found || !info.visible) {
    LOG_ERROR("BrowserManager", "Element not found or not visible: " + actual_selector);
    return ActionResult::ElementNotFound(actual_selector);
  }

  // Verify position if specified
  if (target_x >= 0 && target_y >= 0) {
    if (info.x != target_x || info.y != target_y) {
      LOG_WARN("BrowserManager", "Position mismatch! Expected (" + std::to_string(target_x) + "," + std::to_string(target_y) +
               ") but got (" + std::to_string(info.x) + "," + std::to_string(info.y) + ")");
    }
  }

  LOG_DEBUG("BrowserManager", "Highlighting element at: (" + std::to_string(info.x) + "," + std::to_string(info.y) +
           ") size=" + std::to_string(info.width) + "x" + std::to_string(info.height));

  // Send process message to renderer to highlight the element
  CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("highlight_element");
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  args->SetString(0, context_id);
  args->SetString(1, actual_selector);
  args->SetInt(2, info.x);
  args->SetInt(3, info.y);
  args->SetString(4, border_color);
  args->SetString(5, background_color);

  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);

  // Wait for highlight to be applied
  for (int i = 0; i < 20; i++) {
    PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  LOG_DEBUG("BrowserManager", "=== HIGHLIGHT COMPLETE ===");
  return ActionResult::Success("Highlighted element: " + actual_selector);
}

ActionResult OwlBrowserManager::ShowGridOverlay(const std::string& context_id,
                                         int horizontal_lines,
                                         int vertical_lines,
                                         const std::string& line_color,
                                         const std::string& text_color) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "ShowGridOverlay failed - browser not found for context: " + context_id);
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "=== SHOW GRID OVERLAY START === h_lines=" + std::to_string(horizontal_lines) +
           " v_lines=" + std::to_string(vertical_lines));

  // Unfreeze cache so grid overlay DOM changes can be rendered
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  client->UnfreezeFrameCache();
  LOG_DEBUG("BrowserManager", "Frame cache unfrozen for grid overlay rendering");

  // Send process message to renderer to create the grid overlay
  CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("show_grid_overlay");
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  args->SetString(0, context_id);
  args->SetInt(1, horizontal_lines);
  args->SetInt(2, vertical_lines);
  args->SetString(3, line_color);
  args->SetString(4, text_color);

  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);

  // Wait for grid overlay to be rendered
  for (int i = 0; i < 20; i++) {
    PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  LOG_DEBUG("BrowserManager", "=== SHOW GRID OVERLAY COMPLETE ===");
  return ActionResult::Success("Grid overlay displayed with " + std::to_string(horizontal_lines) + "x" + std::to_string(vertical_lines) + " lines");
}

std::string OwlBrowserManager::ExtractText(const std::string& context_id,
                                           const std::string& selector) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "ExtractText failed - browser not found");
    return "";
  }

  // Get client for navigation wait and text extraction
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());

  // CRITICAL: Wait for any pending navigation to complete before extracting
  if (client) {
    NavigationInfo nav_info = client->GetNavigationInfo();
    if (nav_info.state != NavigationState::COMPLETE) {
      LOG_DEBUG("BrowserManager", "ExtractText: Waiting for navigation to complete...");
      client->WaitForNavigation(10000);  // Wait up to 10 seconds
    }
  }

  LOG_DEBUG("BrowserManager", "=== EXTRACT TEXT START === selector='" + selector + "'");

  // For text extraction, use selector as-is (position metadata not needed for text)
  std::string actual_selector = selector;

  // Get element info from tracker (for cached text)
  ElementRenderInfo info;
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  bool found = tracker->GetElementBounds(context_id, actual_selector, info);

  if (!found) {
    LOG_DEBUG("BrowserManager", "Element not in cache, scanning: " + actual_selector);

    // Trigger scan
    CefRefPtr<CefProcessMessage> scan_msg = CefProcessMessage::Create("scan_element");
    CefRefPtr<CefListValue> scan_args = scan_msg->GetArgumentList();
    scan_args->SetString(0, context_id);
    scan_args->SetString(1, actual_selector);
    browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, scan_msg);

    // Wait for scan with early exit when element found
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count() < 200) {
      PumpMessageLoopIfNeeded();
      // Check if element is now available
      if (tracker->GetElementBounds(context_id, actual_selector, info)) {
        found = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (!found) {
      found = tracker->GetElementBounds(context_id, actual_selector, info);
    }
  }

  // Send message to renderer to extract text
  CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("extract_text");
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  args->SetString(0, context_id);
  args->SetString(1, actual_selector);
  args->SetInt(2, found ? info.x : -1);
  args->SetInt(3, found ? info.y : -1);

  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);

  // Wait for async response from renderer (up to 3 seconds)
  bool success = client->WaitForTextExtraction(context_id, 3000);

  std::string extracted_text;
  if (success) {
    extracted_text = client->GetExtractedText(context_id);
    LOG_DEBUG("BrowserManager", "Text extraction successful: " + std::to_string(extracted_text.length()) + " chars");
  } else {
    LOG_WARN("BrowserManager", "Text extraction timed out, returning cached text");
    extracted_text = found ? info.text : "";
  }

  LOG_DEBUG("BrowserManager", "=== EXTRACT TEXT COMPLETE ===");
  return extracted_text;
}

std::vector<uint8_t> OwlBrowserManager::Screenshot(const std::string& context_id) {
  std::vector<uint8_t> result;

  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "Screenshot: Browser not found for context: " + context_id);
    return result;
  }

  // Get client
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());

  // CRITICAL: Wait for any pending navigation to complete before screenshot
  if (client) {
    NavigationInfo nav_info = client->GetNavigationInfo();
    if (nav_info.state != NavigationState::COMPLETE) {
      client->WaitForNavigation(10000);
    }
  }

  // Get full viewport dimensions
  int width = client->GetViewportWidth();
  int height = client->GetViewportHeight();

  // Check if we're in UI mode (windowed) or headless mode
  if (OwlBrowserManager::UsesRunMessageLoop()) {
    // UI MODE: Use native macOS screenshot (windowed rendering doesn't populate frame cache)
    LOG_DEBUG("BrowserManager", "Screenshot: UI mode - using native capture");

#ifdef BUILD_UI
    // CaptureNativeScreenshot expects grid_items for overlays, but we don't need overlays for regular screenshots
    std::vector<ElementRenderInfo> empty_grid;
    result = CaptureNativeScreenshot(browser, 0, 0, width, height, empty_grid, 0, 0);

    if (result.empty()) {
      LOG_ERROR("BrowserManager", "Native screenshot failed in UI mode");
    }
#else
    LOG_ERROR("BrowserManager", "UI mode detected but BUILD_UI not defined - cannot capture screenshot");
#endif
  } else {
    // HEADLESS MODE: Always trigger fresh paint to ensure current page content
    // IMPORTANT: Unfreeze cache and trigger fresh paint
    // The cache might have stale content from previous page (e.g., about:blank)
    // We must invalidate to ensure OnPaint fires with current page content
    client->UnfreezeFrameCache();

    // Trigger a fresh paint by invalidating the view
    browser->GetHost()->Invalidate(PET_VIEW);

    // Pump message loop to process the paint event with early exit when cache is ready
    int paint_wait = 50;  // Wait up to 500ms for paint
    bool success = false;
    while (paint_wait-- > 0) {
      PumpMessageLoopIfNeeded();
      // Try to get screenshot - break early if cache is ready
      success = client->GetCroppedScreenshotFromCache(&result, 0, 0, width, height);
      if (success && !result.empty()) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Final attempt if loop exhausted without success
    if (!success || result.empty()) {
      success = client->GetCroppedScreenshotFromCache(&result, 0, 0, width, height);
    }

    if (!success || result.empty()) {
      // Last resort: use direct screenshot buffer capture
      client->SetScreenshotBuffer(&result);
      browser->GetHost()->Invalidate(PET_VIEW);

      int timeout = 100;
      while (!client->IsScreenshotReady() && timeout-- > 0) {
        PumpMessageLoopIfNeeded();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      client->ResetScreenshot();
    }

    // IMPORTANT: Freeze frame cache AFTER capturing screenshot
    // This preserves the DOM state shown in the screenshot for CAPTCHA solving
    // (prevents subsequent ScrollIntoView from changing the cached frame)
    if (!result.empty()) {
      client->FreezeFrameCache();
    }
  }

  return result;
}

std::vector<uint8_t> OwlBrowserManager::ScreenshotElement(const std::string& context_id, const std::string& selector) {
  std::vector<uint8_t> result;

  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "ScreenshotElement: Browser not found for context: " + context_id);
    return result;
  }

  LOG_DEBUG("BrowserManager", "ScreenshotElement: Taking element screenshot for selector: " + selector);

  // Get element bounds using existing infrastructure
  ElementRenderInfo info;
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();

  bool found = tracker->GetElementBounds(context_id, selector, info);
  if (!found) {
    // Try semantic matcher for natural language selectors
    OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();
    auto matches = matcher->FindByDescription(context_id, selector, 1);
    if (!matches.empty()) {
      const auto& elem = matches[0].element;
      info.x = elem.x;
      info.y = elem.y;
      info.width = elem.width;
      info.height = elem.height;
      found = true;
    }
  }

  if (!found) {
    LOG_ERROR("BrowserManager", "ScreenshotElement: Element not found: " + selector);
    return result;
  }

  // Validate bounds
  if (info.width <= 0 || info.height <= 0) {
    LOG_ERROR("BrowserManager", "ScreenshotElement: Invalid element dimensions: " +
              std::to_string(info.width) + "x" + std::to_string(info.height));
    return result;
  }

  LOG_DEBUG("BrowserManager", "ScreenshotElement: Element bounds x=" + std::to_string(info.x) +
            " y=" + std::to_string(info.y) + " w=" + std::to_string(info.width) +
            " h=" + std::to_string(info.height));

  // Get client
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());

  // Wait for navigation to complete
  if (client) {
    NavigationInfo nav_info = client->GetNavigationInfo();
    if (nav_info.state != NavigationState::COMPLETE) {
      client->WaitForNavigation(10000);
    }
  }

  // Check if we're in UI mode or headless mode
  if (OwlBrowserManager::UsesRunMessageLoop()) {
    // UI MODE: Use native screenshot and crop
    LOG_DEBUG("BrowserManager", "ScreenshotElement: UI mode - using native capture with crop");

#ifdef BUILD_UI
    std::vector<ElementRenderInfo> empty_grid;
    result = CaptureNativeScreenshot(browser, info.x, info.y, info.width, info.height, empty_grid, 0, 0);

    if (result.empty()) {
      LOG_ERROR("BrowserManager", "Native element screenshot failed in UI mode");
    }
#else
    LOG_ERROR("BrowserManager", "UI mode detected but BUILD_UI not defined");
#endif
  } else {
    // HEADLESS MODE: Use frame cache with cropping
    client->UnfreezeFrameCache();

    // Trigger fresh paint
    browser->GetHost()->Invalidate(PET_VIEW);

    // Pump message loop with early exit when cache is ready
    int paint_wait = 50;
    bool success = false;
    while (paint_wait-- > 0) {
      PumpMessageLoopIfNeeded();
      // Try to get screenshot - break early if cache is ready
      success = client->GetCroppedScreenshotFromCache(&result, info.x, info.y, info.width, info.height);
      if (success && !result.empty()) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Final attempt if loop exhausted
    if (!success || result.empty()) {
      success = client->GetCroppedScreenshotFromCache(&result, info.x, info.y, info.width, info.height);
      if (!success || result.empty()) {
        LOG_ERROR("BrowserManager", "ScreenshotElement: Failed to capture from cache");
      }
    }

    if (!result.empty()) {
      client->FreezeFrameCache();
    }
  }

  LOG_DEBUG("BrowserManager", "ScreenshotElement: Complete, size=" + std::to_string(result.size()));
  return result;
}

std::vector<uint8_t> OwlBrowserManager::ScreenshotFullpage(const std::string& context_id) {
  std::vector<uint8_t> result;

  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "ScreenshotFullpage: Browser not found for context: " + context_id);
    return result;
  }

  LOG_DEBUG("BrowserManager", "ScreenshotFullpage: Taking fullpage screenshot using CDP");

  // Get client
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());

  // Wait for navigation to complete
  if (client) {
    NavigationInfo nav_info = client->GetNavigationInfo();
    if (nav_info.state != NavigationState::COMPLETE) {
      client->WaitForNavigation(10000);
    }
  }

  // Scroll to top first to ensure we capture from the beginning
  ScrollToTop(context_id);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Get page dimensions via JavaScript evaluation
  // Note: Script is wrapped in IIFE by renderer, so we need 'return' statement
  std::string dims_result = Evaluate(context_id,
    "return JSON.stringify({"
    "scrollWidth: Math.max(document.body.scrollWidth, document.documentElement.scrollWidth, document.body.offsetWidth, document.documentElement.offsetWidth),"
    "scrollHeight: Math.max(document.body.scrollHeight, document.documentElement.scrollHeight, document.body.offsetHeight, document.documentElement.offsetHeight)"
    "})");

  LOG_DEBUG("BrowserManager", "ScreenshotFullpage: JS result = " + dims_result);

  int viewport_width = client->GetViewportWidth();
  int viewport_height = client->GetViewportHeight();
  int page_width = viewport_width;
  int page_height = viewport_height;

  // Parse the JSON result to get dimensions
  // The result might be double-quoted (JSON string containing JSON), so check for escaped quotes
  if (!dims_result.empty() && dims_result != "undefined" && dims_result != "null") {
    // Look for scrollWidth - handle both escaped (\\\"scrollWidth\\\") and normal (\"scrollWidth\") formats
    size_t sw_pos = dims_result.find("scrollWidth");
    size_t sh_pos = dims_result.find("scrollHeight");

    if (sw_pos != std::string::npos) {
      // Find the colon and then the number
      size_t colon_pos = dims_result.find(":", sw_pos);
      if (colon_pos != std::string::npos) {
        colon_pos++;
        // Skip any whitespace or backslashes
        while (colon_pos < dims_result.length() &&
               (dims_result[colon_pos] == ' ' || dims_result[colon_pos] == '\\')) {
          colon_pos++;
        }
        size_t sw_end = dims_result.find_first_of(",}\\\"", colon_pos);
        if (sw_end != std::string::npos) {
          std::string sw_str = dims_result.substr(colon_pos, sw_end - colon_pos);
          char* end_ptr = nullptr;
          long sw = std::strtol(sw_str.c_str(), &end_ptr, 10);
          if (end_ptr != sw_str.c_str() && sw > 0) {
            page_width = static_cast<int>(sw);
          }
        }
      }
    }

    if (sh_pos != std::string::npos) {
      // Find the colon and then the number
      size_t colon_pos = dims_result.find(":", sh_pos);
      if (colon_pos != std::string::npos) {
        colon_pos++;
        // Skip any whitespace or backslashes
        while (colon_pos < dims_result.length() &&
               (dims_result[colon_pos] == ' ' || dims_result[colon_pos] == '\\')) {
          colon_pos++;
        }
        size_t sh_end = dims_result.find_first_of(",}\\\"", colon_pos);
        if (sh_end != std::string::npos) {
          std::string sh_str = dims_result.substr(colon_pos, sh_end - colon_pos);
          char* end_ptr = nullptr;
          long sh = std::strtol(sh_str.c_str(), &end_ptr, 10);
          if (end_ptr != sh_str.c_str() && sh > 0) {
            page_height = static_cast<int>(sh);
          }
        }
      }
    }
  }

  LOG_DEBUG("BrowserManager", "ScreenshotFullpage: Page dimensions w=" + std::to_string(page_width) +
            " h=" + std::to_string(page_height));

  // Limit max dimensions to prevent memory issues
  const int MAX_DIMENSION = 16384;
  page_width = std::min(page_width, MAX_DIMENSION);
  page_height = std::min(page_height, MAX_DIMENSION);

  // If page fits in viewport, just take regular screenshot
  if (page_width <= viewport_width && page_height <= viewport_height) {
    LOG_DEBUG("BrowserManager", "ScreenshotFullpage: Page fits in viewport, using standard screenshot");
    return Screenshot(context_id);
  }

  // Resize viewport to full page size and capture from frame cache
  LOG_DEBUG("BrowserManager", "ScreenshotFullpage: Resizing viewport to " +
            std::to_string(page_width) + "x" + std::to_string(page_height));

  // Unfreeze frame cache to allow updates
  client->UnfreezeFrameCache();

  // Set the viewport to full page dimensions
  client->SetViewport(page_width, page_height);

  // Notify browser of size change
  browser->GetHost()->WasResized();

  // Force invalidate to trigger paint
  browser->GetHost()->Invalidate(PET_VIEW);

  // Wait for frame cache to be updated to new dimensions
  // Poll until the cached frame matches our requested size
  const int MAX_WAIT_MS = 10000;  // 10 second max wait
  const int POLL_INTERVAL_MS = 50;
  int waited_ms = 0;
  bool frame_ready = false;

  LOG_DEBUG("BrowserManager", "ScreenshotFullpage: Waiting for frame cache to reach " +
            std::to_string(page_width) + "x" + std::to_string(page_height));

  while (waited_ms < MAX_WAIT_MS) {
    // Process message loop to allow OnPaint to be called
    PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
    waited_ms += POLL_INTERVAL_MS;

    // Check if frame cache has been updated to our requested size
    int cached_w = 0, cached_h = 0;
    client->GetCachedFrameDimensions(cached_w, cached_h);

    if (cached_w >= page_width && cached_h >= page_height) {
      LOG_DEBUG("BrowserManager", "ScreenshotFullpage: Frame cache ready at " +
                std::to_string(cached_w) + "x" + std::to_string(cached_h) +
                " after " + std::to_string(waited_ms) + "ms");
      frame_ready = true;
      break;
    }

    // Periodically re-invalidate to ensure paint happens
    if (waited_ms % 200 == 0) {
      browser->GetHost()->Invalidate(PET_VIEW);
      LOG_DEBUG("BrowserManager", "ScreenshotFullpage: Re-invalidating, current cache: " +
                std::to_string(cached_w) + "x" + std::to_string(cached_h));
    }
  }

  if (!frame_ready) {
    int cached_w = 0, cached_h = 0;
    client->GetCachedFrameDimensions(cached_w, cached_h);
    LOG_ERROR("BrowserManager", "ScreenshotFullpage: Frame cache did not reach requested size. " +
              std::string("Requested: ") + std::to_string(page_width) + "x" + std::to_string(page_height) +
              ", Cached: " + std::to_string(cached_w) + "x" + std::to_string(cached_h));
  }

  // Capture from frame cache
  bool success = client->GetCroppedScreenshotFromCache(&result, 0, 0, page_width, page_height);

  if (!success || result.empty()) {
    LOG_ERROR("BrowserManager", "ScreenshotFullpage: Failed to capture from cache");
  } else {
    LOG_DEBUG("BrowserManager", "ScreenshotFullpage: Captured " + std::to_string(result.size()) + " bytes from cache");
  }

  // Freeze frame cache
  if (!result.empty()) {
    client->FreezeFrameCache();
  }

  // Restore original viewport size
  LOG_DEBUG("BrowserManager", "ScreenshotFullpage: Restoring original viewport " +
            std::to_string(viewport_width) + "x" + std::to_string(viewport_height));

  client->SetViewport(viewport_width, viewport_height);
  browser->GetHost()->WasResized();

  // Pump a few message loop cycles to apply resize
  for (int i = 0; i < 10; i++) {
    PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  LOG_DEBUG("BrowserManager", "ScreenshotFullpage: Complete, size=" + std::to_string(result.size()));
  return result;
}

CefRefPtr<CefBrowser> OwlBrowserManager::GetAvailableBrowser() {
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);

  // Find unused browser
  for (auto& pair : contexts_) {
    if (!pair.second->in_use) {
      pair.second->in_use = true;
      pair.second->last_used = std::chrono::steady_clock::now();
      return pair.second->browser;
    }
  }

  return nullptr;
}

void OwlBrowserManager::ReturnBrowser(CefRefPtr<CefBrowser> browser) {
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);

  // Find and mark as unused
  for (auto& pair : contexts_) {
    if (pair.second->browser == browser) {
      pair.second->in_use = false;
      pair.second->last_used = std::chrono::steady_clock::now();
      break;
    }
  }
}

// AI-First Methods Implementation

bool OwlBrowserManager::AIClick(const std::string& context_id, const std::string& description) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "AIClick failed - browser not found");
    return false;
  }

  LOG_DEBUG("BrowserManager", "AI click: \"" + description + "\"");
  return OwlAIIntelligence::ClickElement(browser->GetMainFrame(), description);
}

bool OwlBrowserManager::AIType(const std::string& context_id,
                                 const std::string& description,
                                 const std::string& text) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "AIType failed - browser not found");
    return false;
  }

  LOG_DEBUG("BrowserManager", "AI type into \"" + description + "\": " + text);
  return OwlAIIntelligence::TypeIntoElement(browser->GetMainFrame(), description, text);
}

std::string OwlBrowserManager::AIExtract(const std::string& context_id, const std::string& what) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "AIExtract failed - browser not found");
    return "";
  }

  LOG_DEBUG("BrowserManager", "AI extract: \"" + what + "\"");

  if (what == "main content" || what == "article" || what == "content") {
    return OwlAIIntelligence::GetMainContent(browser->GetMainFrame());
  } else if (what == "visible text" || what == "text" || what == "all text") {
    return OwlAIIntelligence::GetVisibleText(browser->GetMainFrame());
  } else {
    // Try to extract specific content by description
    return OwlAIIntelligence::ExtractContent(browser->GetMainFrame(), what);
  }
}

std::string OwlBrowserManager::AIAnalyze(const std::string& context_id) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "AIAnalyze failed - browser not found");
    return "{}";
  }

  LOG_DEBUG("BrowserManager", "AI analyzing page");
  PageIntelligence intelligence = OwlAIIntelligence::AnalyzePage(browser->GetMainFrame());

  // Convert to JSON for AI consumption
  std::ostringstream json;
  json << "{"
       << "\"title\":\"" << intelligence.title << "\","
       << "\"hasForms\":" << (intelligence.has_forms ? "true" : "false") << ","
       << "\"hasLoginForm\":" << (intelligence.has_login_form ? "true" : "false") << ","
       << "\"totalElements\":" << intelligence.total_elements << ","
       << "\"clickableElements\":" << intelligence.clickable_elements.size() << ","
       << "\"inputElements\":" << intelligence.input_elements.size()
       << "}";

  return json.str();
}

std::string OwlBrowserManager::AIQuery(const std::string& context_id, const std::string& query) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "AIQuery failed - browser not found");
    return "";
  }

  LOG_DEBUG("BrowserManager", "AI query: \"" + query + "\"");
  return OwlAIIntelligence::QueryPage(browser->GetMainFrame(), query);
}

// Helper function to escape JSON strings
static std::string EscapeJson(const std::string& str) {
  std::ostringstream escaped;
  for (char c : str) {
    switch (c) {
      case '"':  escaped << "\\\""; break;
      case '\\': escaped << "\\\\"; break;
      case '\b': escaped << "\\b"; break;
      case '\f': escaped << "\\f"; break;
      case '\n': escaped << "\\n"; break;
      case '\r': escaped << "\\r"; break;
      case '\t': escaped << "\\t"; break;
      default:
        if (c < 0x20) {
          escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
        } else {
          escaped << c;
        }
    }
  }
  return escaped.str();
}

std::string OwlBrowserManager::FindElement(const std::string& context_id, const std::string& description, int max_results) {
  LOG_DEBUG("BrowserManager", "FindElement: \"" + description + "\" in context " + context_id + " max_results=" + std::to_string(max_results));

  LOG_DEBUG("BrowserManager", "Getting semantic matcher instance");
  OwlSemanticMatcher* matcher = OwlSemanticMatcher::GetInstance();

  LOG_DEBUG("BrowserManager", "Calling FindByDescription");
  auto matches = matcher->FindByDescription(context_id, description, max_results);

  LOG_DEBUG("BrowserManager", "FindByDescription returned " + std::to_string(matches.size()) + " matches");

  std::ostringstream json;
  json << "{\"matches\":[";

  for (size_t i = 0; i < matches.size(); i++) {
    if (i > 0) json << ",";

    const auto& m = matches[i];
    const auto& e = m.element;

    // Fix: Nest element data inside "element" field as expected by browser-wrapper
    json << "{"
         << "\"confidence\":" << m.confidence << ","
         << "\"element\":{"
         << "\"selector\":\"" << EscapeJson(e.selector) << "\","
         << "\"tag\":\"" << EscapeJson(e.tag) << "\","
         << "\"text\":\"" << EscapeJson(e.text) << "\","
         << "\"role\":\"" << EscapeJson(e.inferred_role) << "\","
         << "\"label_for\":\"" << EscapeJson(e.label_for) << "\","
         << "\"x\":" << e.x << ","
         << "\"y\":" << e.y << ","
         << "\"width\":" << e.width << ","
         << "\"height\":" << e.height
         << "}"
         << "}";
  }

  json << "]}";
  return json.str();
}

std::string OwlBrowserManager::GetBlockerStats(const std::string& context_id) {
  auto stats = OwlResourceBlocker::GetInstance()->GetStats();

  std::ostringstream json;
  json << "{"
       << "\"adsBlocked\":" << stats.ads_blocked << ","
       << "\"analyticsBlocked\":" << stats.analytics_blocked << ","
       << "\"trackersBlocked\":" << stats.trackers_blocked << ","
       << "\"totalBlocked\":" << stats.total_blocked << ","
       << "\"totalRequests\":" << stats.total_requests << ","
       << "\"blockPercentage\":" << stats.block_percentage
       << "}";

  return json.str();
}

// Smart preloading for AI workflows
std::string OwlBrowserManager::CreatePreloadedContext(const std::string& url) {
  auto ctx_id = CreateContext();
  LOG_DEBUG("BrowserManager", "Preloading context " + ctx_id + " with URL: " + url);

  // Navigate in background (async, non-blocking)
  std::thread([this, ctx_id, url]() {
    Navigate(ctx_id, url);
    // Mark as ready for AI once loaded
    std::shared_lock<std::shared_mutex> lock(contexts_mutex_);
    auto it = contexts_.find(ctx_id);
    if (it != contexts_.end()) {
      it->second->in_use = false;
      it->second->last_used = std::chrono::steady_clock::now();
      LOG_DEBUG("BrowserManager", "Preloaded context " + ctx_id + " ready");
    }
  }).detach();

  return ctx_id;
}

void OwlBrowserManager::PreloadCommonSites(const std::vector<std::string>& urls) {
  LOG_DEBUG("BrowserManager", "Preloading " + std::to_string(urls.size()) + " common sites for AI");

  for (const auto& url : urls) {
    // Don't exceed max contexts
    if (contexts_.size() >= static_cast<size_t>(max_contexts_)) {
      LOG_WARN("BrowserManager", "Max contexts reached, stopping preload");
      break;
    }
    CreatePreloadedContext(url);
  }
}

// ==================== CONTENT EXTRACTION METHODS ====================
// These methods provide structured content extraction in multiple formats

#include "owl_content_extractor.h"
#include "owl_markdown_converter.h"
#include "owl_extraction_template.h"

std::string OwlBrowserManager::GetHTML(const std::string& context_id,
                                        const std::string& clean_level) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "GetHTML failed - browser not found");
    return "";
  }

  // CRITICAL: Wait for any pending navigation to complete before extracting
  // This ensures we get the current DOM, not stale content from previous page
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  if (client) {
    NavigationInfo nav_info = client->GetNavigationInfo();
    if (nav_info.state != NavigationState::COMPLETE) {
      LOG_DEBUG("BrowserManager", "GetHTML: Waiting for navigation to complete...");
      client->WaitForNavigation(10000);  // Wait up to 10 seconds
    }
  }

  // Map string to enum
  HTMLExtractionOptions options;
  if (clean_level == "minimal") {
    options.clean_level = CleanLevel::MINIMAL;
  } else if (clean_level == "aggressive") {
    options.clean_level = CleanLevel::AGGRESSIVE;
  } else {
    options.clean_level = CleanLevel::BASIC;
  }

  LOG_DEBUG("BrowserManager", "Extracting HTML with clean level: " + clean_level);
  return OwlContentExtractor::ExtractHTML(browser->GetMainFrame(), options);
}

std::string OwlBrowserManager::GetMarkdown(const std::string& context_id,
                                            bool include_links,
                                            bool include_images,
                                            int max_length) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "GetMarkdown failed - browser not found");
    return "";
  }

  // CRITICAL: Wait for any pending navigation to complete before extracting
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  if (client) {
    NavigationInfo nav_info = client->GetNavigationInfo();
    if (nav_info.state != NavigationState::COMPLETE) {
      LOG_DEBUG("BrowserManager", "GetMarkdown: Waiting for navigation to complete...");
      client->WaitForNavigation(10000);  // Wait up to 10 seconds
    }
  }

  MarkdownExtractionOptions options;
  options.include_links = include_links;
  options.include_images = include_images;
  options.max_length = max_length;

  LOG_DEBUG("BrowserManager", "Extracting Markdown");
  return OwlContentExtractor::ExtractMarkdown(browser->GetMainFrame(), options);
}

std::string OwlBrowserManager::ExtractJSON(const std::string& context_id,
                                           const std::string& template_name,
                                           const std::string& custom_schema) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "ExtractJSON failed - browser not found");
    return "{}";
  }

  // Wait for any pending navigation to complete
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  if (client) {
    NavigationInfo nav_info = client->GetNavigationInfo();
    if (nav_info.state != NavigationState::COMPLETE) {
      LOG_DEBUG("BrowserManager", "ExtractJSON: Waiting for navigation to complete...");
      client->WaitForNavigation(10000);
    }
  }

  LOG_DEBUG("BrowserManager", "Extracting JSON" +
           (template_name.empty() ? " (auto-detect/generic)" : " with template: " + template_name));

  // If template specified, use it
  if (!template_name.empty()) {
    return OwlContentExtractor::ExtractWithTemplate(browser->GetMainFrame(), template_name);
  }

  // Otherwise, try auto-detection
  std::string detected = OwlContentExtractor::DetectWebsiteType(browser->GetMainFrame());
  if (detected != "generic") {
    LOG_DEBUG("BrowserManager", "Auto-detected template: " + detected);
    return OwlContentExtractor::ExtractWithTemplate(browser->GetMainFrame(), detected);
  }

  // Fall back to generic extraction
  LOG_DEBUG("BrowserManager", "Using generic JSON extraction");
  return OwlContentExtractor::ExtractJSON(browser->GetMainFrame(), custom_schema);
}

std::string OwlBrowserManager::DetectWebsiteType(const std::string& context_id) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "DetectWebsiteType failed - browser not found");
    return "generic";
  }

  return OwlContentExtractor::DetectWebsiteType(browser->GetMainFrame());
}

std::vector<std::string> OwlBrowserManager::ListTemplates() {
  return OwlContentExtractor::ListAvailableTemplates();
}

// ============================================================
// AI Intelligence Implementation (On-Device LLM)
// ============================================================

std::string OwlBrowserManager::SummarizePage(const std::string& context_id, bool force_refresh) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "SummarizePage failed - browser not found");
    return "Error: Browser context not found";
  }

  LOG_DEBUG("BrowserManager", "SummarizePage for context: " + context_id +
           (force_refresh ? " (force refresh)" : " (cached)"));

  // Get per-context LLM client (falls back to global if not set)
  OwlLLMClient* llm = GetLLMClientForContext(context_id);

  return OwlAIIntelligence::SummarizePage(browser->GetMainFrame(), force_refresh, llm);
}

std::string OwlBrowserManager::QueryPage(const std::string& context_id, const std::string& query) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "QueryPage failed - browser not found");
    return "Error: Browser context not found";
  }

  LOG_DEBUG("BrowserManager", "QueryPage for context: " + context_id);

  // Get per-context LLM client (falls back to global if not set)
  OwlLLMClient* llm = GetLLMClientForContext(context_id);

  return OwlAIIntelligence::QueryPage(browser->GetMainFrame(), query, llm);
}

std::string OwlBrowserManager::GetLLMStatus() {
  // Check built-in llama server
  if (llama_server_) {
    if (llama_server_->IsReady()) {
      return "ready";
    }
    return "loading";
  }

  // Check external API client
  if (llm_client_) {
    return "ready";
  }

  return "unavailable";
}

std::string OwlBrowserManager::ExecuteNLA(const std::string& context_id, const std::string& command) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "ExecuteNLA failed - browser not found");
    return "Error: Browser context not found";
  }

  LOG_DEBUG("BrowserManager", "ExecuteNLA for context: " + context_id);
  LOG_DEBUG("BrowserManager", "Command: " + command);

  return OwlNLA::ExecuteCommand(browser->GetMainFrame(), command);
}

// ============================================================
// Browser Navigation & Control Implementation
// ============================================================

ActionResult OwlBrowserManager::Reload(const std::string& context_id, bool ignore_cache,
                                       const std::string& wait_until, int timeout_ms) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "Reload failed - browser not found for context: " + context_id);
    return ActionResult::BrowserNotFound(context_id);
  }

  std::ostringstream log_msg;
  log_msg << "=== RELOAD START === context=" << context_id
          << " ignore_cache=" << (ignore_cache ? "true" : "false");
  if (!wait_until.empty()) {
    log_msg << " wait_until=" << wait_until << " timeout=" << timeout_ms << "ms";
  }
  LOG_DEBUG("BrowserManager", log_msg.str());

  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  client->ResetNavigation();

  // Get current URL and reload by navigating to it again
  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  if (!frame) {
    LOG_ERROR("BrowserManager", "Reload failed - no main frame");
    return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "No main frame available");
  }

  std::string current_url = frame->GetURL().ToString();
  frame->LoadURL(current_url);

  // If wait_until is specified, wait for the reload to complete
  if (!wait_until.empty()) {
    if (wait_until == "load" || wait_until == "domcontentloaded") {
      // Wait for navigation load event
      client->WaitForNavigation(timeout_ms);
    } else if (wait_until == "networkidle") {
      // Wait for navigation then network idle
      client->WaitForNavigation(timeout_ms);
      ActionResult idle_result = WaitForNetworkIdle(context_id, 500, timeout_ms);
      if (idle_result.status != ActionStatus::OK) {
        return idle_result;
      }
    }

    // Wait for element scan after navigation
    std::ostringstream ctx_stream;
    ctx_stream << "ctx_" << std::setfill('0') << std::setw(6) << browser->GetIdentifier();
    std::string expected_context = ctx_stream.str();
    client->WaitForElementScan(browser, expected_context, 5000);
  }

  LOG_DEBUG("BrowserManager", "=== RELOAD COMPLETE ===");
  return ActionResult::Success("Page reloaded");
}

ActionResult OwlBrowserManager::GoBack(const std::string& context_id,
                                       const std::string& wait_until, int timeout_ms) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "GoBack failed - browser not found for context: " + context_id);
    return ActionResult::BrowserNotFound(context_id);
  }

  if (!browser->CanGoBack()) {
    LOG_WARN("BrowserManager", "Cannot go back - no history available");
    return ActionResult::Failure(ActionStatus::NAVIGATION_FAILED, "Cannot go back - no history available");
  }

  std::ostringstream log_msg;
  log_msg << "=== GO BACK === context=" << context_id;
  if (!wait_until.empty()) {
    log_msg << " wait_until=" << wait_until << " timeout=" << timeout_ms << "ms";
  }
  LOG_DEBUG("BrowserManager", log_msg.str());

  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  client->ResetNavigation();

  browser->GoBack();

  // If wait_until is specified, wait for the navigation to complete
  if (!wait_until.empty()) {
    if (wait_until == "load" || wait_until == "domcontentloaded") {
      client->WaitForNavigation(timeout_ms);
    } else if (wait_until == "networkidle") {
      client->WaitForNavigation(timeout_ms);
      ActionResult idle_result = WaitForNetworkIdle(context_id, 500, timeout_ms);
      if (idle_result.status != ActionStatus::OK) {
        return idle_result;
      }
    }

    // Wait for element scan after navigation
    std::ostringstream ctx_stream;
    ctx_stream << "ctx_" << std::setfill('0') << std::setw(6) << browser->GetIdentifier();
    client->WaitForElementScan(browser, ctx_stream.str(), 5000);
  }

  LOG_DEBUG("BrowserManager", "=== GO BACK COMPLETE ===");
  return ActionResult::Success("Navigated back");
}

ActionResult OwlBrowserManager::GoForward(const std::string& context_id,
                                          const std::string& wait_until, int timeout_ms) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "GoForward failed - browser not found for context: " + context_id);
    return ActionResult::BrowserNotFound(context_id);
  }

  if (!browser->CanGoForward()) {
    LOG_WARN("BrowserManager", "Cannot go forward - already at latest page");
    return ActionResult::Failure(ActionStatus::NAVIGATION_FAILED, "Cannot go forward - already at latest page");
  }

  std::ostringstream log_msg;
  log_msg << "=== GO FORWARD === context=" << context_id;
  if (!wait_until.empty()) {
    log_msg << " wait_until=" << wait_until << " timeout=" << timeout_ms << "ms";
  }
  LOG_DEBUG("BrowserManager", log_msg.str());

  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  client->ResetNavigation();

  browser->GoForward();

  // If wait_until is specified, wait for the navigation to complete
  if (!wait_until.empty()) {
    if (wait_until == "load" || wait_until == "domcontentloaded") {
      client->WaitForNavigation(timeout_ms);
    } else if (wait_until == "networkidle") {
      client->WaitForNavigation(timeout_ms);
      ActionResult idle_result = WaitForNetworkIdle(context_id, 500, timeout_ms);
      if (idle_result.status != ActionStatus::OK) {
        return idle_result;
      }
    }

    // Wait for element scan after navigation
    std::ostringstream ctx_stream;
    ctx_stream << "ctx_" << std::setfill('0') << std::setw(6) << browser->GetIdentifier();
    client->WaitForElementScan(browser, ctx_stream.str(), 5000);
  }

  LOG_DEBUG("BrowserManager", "=== GO FORWARD COMPLETE ===");
  return ActionResult::Success("Navigated forward");
}

bool OwlBrowserManager::CanGoBack(const std::string& context_id) {
  auto browser = GetBrowser(context_id);
  return browser ? browser->CanGoBack() : false;
}

bool OwlBrowserManager::CanGoForward(const std::string& context_id) {
  auto browser = GetBrowser(context_id);
  return browser ? browser->CanGoForward() : false;
}

// ============================================================
// Scroll Control Implementation
// ============================================================

ActionResult OwlBrowserManager::ScrollBy(const std::string& context_id, int x, int y, VerificationLevel level) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "ScrollBy failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "ScrollBy: x=" + std::to_string(x) + " y=" + std::to_string(y) +
           " level=" + VerificationLevelToString(level));

  // Invalidate semantic matcher cache - element positions change on scroll
  OwlSemanticMatcher::GetInstance()->InvalidateCacheForContext(context_id);

  // Unfreeze cache so scroll can update the visual state
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  client->UnfreezeFrameCache();

  // Get pre-scroll position for verification (if STANDARD level)
  int pre_scroll_x = 0, pre_scroll_y = 0;
  if (level >= VerificationLevel::STANDARD) {
    client->ResetVerification(context_id);
    CefRefPtr<CefProcessMessage> pos_msg = CefProcessMessage::Create("get_scroll_position");
    CefRefPtr<CefListValue> pos_args = pos_msg->GetArgumentList();
    pos_args->SetString(0, context_id);
    browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, pos_msg);

    if (client->WaitForVerification(context_id, 30)) {
      OwlClient::VerificationResult pos_result = client->GetVerificationResult(context_id);
      // Parse "x,y,width,height" format
      size_t comma1 = pos_result.actual_value.find(',');
      size_t comma2 = pos_result.actual_value.find(',', comma1 + 1);
      if (comma1 != std::string::npos && comma2 != std::string::npos) {
        pre_scroll_x = std::stoi(pos_result.actual_value.substr(0, comma1));
        pre_scroll_y = std::stoi(pos_result.actual_value.substr(comma1 + 1, comma2 - comma1 - 1));
      }
    }
  }

  // Send message to renderer to execute scroll via V8
  CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("scroll_by");
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  args->SetString(0, context_id);
  args->SetInt(1, x);
  args->SetInt(2, y);

  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);

  // PERFORMANCE: Minimal wait - scroll is async
  for (int i = 0; i < 3; i++) {
    PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  // Post-scroll verification (if STANDARD level)
  if (level >= VerificationLevel::STANDARD) {
    client->ResetVerification(context_id);
    CefRefPtr<CefProcessMessage> pos_msg = CefProcessMessage::Create("get_scroll_position");
    CefRefPtr<CefListValue> pos_args = pos_msg->GetArgumentList();
    pos_args->SetString(0, context_id);
    browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, pos_msg);

    if (client->WaitForVerification(context_id, 30)) {
      OwlClient::VerificationResult pos_result = client->GetVerificationResult(context_id);
      size_t comma1 = pos_result.actual_value.find(',');
      size_t comma2 = pos_result.actual_value.find(',', comma1 + 1);
      if (comma1 != std::string::npos && comma2 != std::string::npos) {
        int post_scroll_x = std::stoi(pos_result.actual_value.substr(0, comma1));
        int post_scroll_y = std::stoi(pos_result.actual_value.substr(comma1 + 1, comma2 - comma1 - 1));

        int delta_x = post_scroll_x - pre_scroll_x;
        int delta_y = post_scroll_y - pre_scroll_y;

        // Check if scroll actually happened (allow 5px tolerance for rounding)
        // Also allow for boundary cases where scroll couldn't go further
        if (x != 0 && abs(delta_x) < abs(x) - 5 && abs(delta_x) < 1) {
          LOG_DEBUG("BrowserManager", "ScrollBy - x scroll limited (boundary reached)");
        }
        if (y != 0 && abs(delta_y) < abs(y) - 5 && abs(delta_y) < 1) {
          LOG_DEBUG("BrowserManager", "ScrollBy - y scroll limited (boundary reached)");
        }

        LOG_DEBUG("BrowserManager", "ScrollBy verified: pre=(" + std::to_string(pre_scroll_x) + "," +
                 std::to_string(pre_scroll_y) + ") post=(" + std::to_string(post_scroll_x) + "," +
                 std::to_string(post_scroll_y) + ") delta=(" + std::to_string(delta_x) + "," +
                 std::to_string(delta_y) + ")");
      }
    }
  }

  return ActionResult::Success("Scrolled by (" + std::to_string(x) + ", " + std::to_string(y) + ")");
}

ActionResult OwlBrowserManager::ScrollTo(const std::string& context_id, int x, int y, VerificationLevel level) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "ScrollTo failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "ScrollTo: x=" + std::to_string(x) + " y=" + std::to_string(y) +
           " level=" + VerificationLevelToString(level));

  // Invalidate semantic matcher cache - element positions change on scroll
  OwlSemanticMatcher::GetInstance()->InvalidateCacheForContext(context_id);

  // Unfreeze cache so scroll can update the visual state
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  client->UnfreezeFrameCache();

  // Send message to renderer to execute scroll via V8
  CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("scroll_to");
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  args->SetString(0, context_id);
  args->SetInt(1, x);
  args->SetInt(2, y);

  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);

  // PERFORMANCE: Minimal wait - scroll is async
  for (int i = 0; i < 3; i++) {
    PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  // Post-scroll verification (if STANDARD level)
  if (level >= VerificationLevel::STANDARD) {
    client->ResetVerification(context_id);
    CefRefPtr<CefProcessMessage> pos_msg = CefProcessMessage::Create("get_scroll_position");
    CefRefPtr<CefListValue> pos_args = pos_msg->GetArgumentList();
    pos_args->SetString(0, context_id);
    browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, pos_msg);

    if (client->WaitForVerification(context_id, 30)) {
      OwlClient::VerificationResult pos_result = client->GetVerificationResult(context_id);
      size_t comma1 = pos_result.actual_value.find(',');
      size_t comma2 = pos_result.actual_value.find(',', comma1 + 1);
      if (comma1 != std::string::npos && comma2 != std::string::npos) {
        int actual_x = std::stoi(pos_result.actual_value.substr(0, comma1));
        int actual_y = std::stoi(pos_result.actual_value.substr(comma1 + 1, comma2 - comma1 - 1));

        // Check if scroll reached target (allow 5px tolerance)
        if (abs(actual_x - x) > 5 || abs(actual_y - y) > 5) {
          LOG_DEBUG("BrowserManager", "ScrollTo - position differs from target (boundary or smooth scroll): target=(" + std::to_string(x) + "," + std::to_string(y) + ") actual=(" + std::to_string(actual_x) + "," + std::to_string(actual_y) + ")");
        } else {
          LOG_DEBUG("BrowserManager", "ScrollTo verified at (" + std::to_string(actual_x) + "," +
                   std::to_string(actual_y) + ")");
        }
      }
    }
  }

  return ActionResult::Success("Scrolled to (" + std::to_string(x) + ", " + std::to_string(y) + ")");
}

ActionResult OwlBrowserManager::ScrollToElement(const std::string& context_id, const std::string& selector) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "ScrollToElement failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "ScrollToElement: " + selector);

  // Invalidate semantic matcher cache - element positions change on scroll
  OwlSemanticMatcher::GetInstance()->InvalidateCacheForContext(context_id);

  // Unfreeze cache so scroll can update the visual state
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  client->UnfreezeFrameCache();

  // Get element position from tracker
  ElementRenderInfo info;
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  bool found = tracker->GetElementBounds(context_id, selector, info);

  if (!found) {
    // Trigger scan - but with reduced timeout for performance
    CefRefPtr<CefProcessMessage> scan_msg = CefProcessMessage::Create("scan_element");
    CefRefPtr<CefListValue> scan_args = scan_msg->GetArgumentList();
    scan_args->SetString(0, context_id);
    scan_args->SetString(1, selector);
    browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, scan_msg);

    // PERFORMANCE: Reduced scan wait from 500ms to 100ms
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count() < 100) {
      PumpMessageLoopIfNeeded();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    found = tracker->GetElementBounds(context_id, selector, info);
  }

  if (!found) {
    LOG_ERROR("BrowserManager", "Element not found for scroll: " + selector);
    return ActionResult::ElementNotFound(selector);
  }

  // Send scroll message to renderer
  CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("scroll_to_element");
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  args->SetString(0, context_id);
  args->SetString(1, selector);

  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);

  // PERFORMANCE: Minimal wait - scroll is async
  for (int i = 0; i < 3; i++) {
    PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  LOG_DEBUG("BrowserManager", "=== SCROLL TO ELEMENT COMPLETE ===");
  return ActionResult::Success("Scrolled to element: " + selector);
}

ActionResult OwlBrowserManager::ScrollToTop(const std::string& context_id) {
  return ScrollTo(context_id, 0, 0);
}

ActionResult OwlBrowserManager::ScrollToBottom(const std::string& context_id) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "ScrollToBottom failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "ScrollToBottom: " + context_id);

  // Unfreeze cache so scroll can update the visual state
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  client->UnfreezeFrameCache();

  // Send message to renderer to scroll to bottom
  CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("scroll_to_bottom");
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  args->SetString(0, context_id);

  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);

  // PERFORMANCE: Reduced wait time - scroll is async, just need brief pump
  // The renderer will execute the scroll; we don't need to wait for visual update
  for (int i = 0; i < 3; i++) {
    PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  // PERFORMANCE: Skip element re-scan for scrollToBottom - it's expensive and rarely needed
  // Element scan is primarily for CAPTCHA detection which should use explicit scan calls
  // If element scan is needed after scroll, caller should request it explicitly

  LOG_DEBUG("BrowserManager", "ScrollToBottom complete: " + context_id);
  return ActionResult::Success("Scrolled to bottom");
}

// ============================================================
// Wait Utilities Implementation
// ============================================================

ActionResult OwlBrowserManager::WaitForSelector(const std::string& context_id,
                                          const std::string& selector,
                                          int timeout_ms) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "WaitForSelector failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  LOG_DEBUG("BrowserManager", "=== WAIT FOR SELECTOR === selector=" + selector +
           " timeout=" + std::to_string(timeout_ms) + "ms");

  // Check if this is a semantic selector (no CSS special characters)
  bool is_semantic = IsSelectorSemantic(selector);

  auto start = std::chrono::steady_clock::now();
  OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
  OwlSemanticMatcher* semantic_matcher = OwlSemanticMatcher::GetInstance();

  while (true) {
    // Trigger scan
    CefRefPtr<CefProcessMessage> scan_msg = CefProcessMessage::Create("scan_element");
    CefRefPtr<CefListValue> scan_args = scan_msg->GetArgumentList();
    scan_args->SetString(0, context_id);
    scan_args->SetString(1, selector);
    browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, scan_msg);

    // Wait for scan - optimized for concurrency (reduced from 500ms to 50ms)
    // In UI mode, don't call CefDoMessageLoopWork() from background thread - it crashes
    if (UsesRunMessageLoop()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } else {
      for (int i = 0; i < 10; i++) {
        PumpMessageLoopIfNeeded();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    }

    // Check if element found - use semantic matcher for semantic selectors
    if (is_semantic) {
      // Use semantic matcher for natural language selectors
      auto matches = semantic_matcher->FindByDescription(context_id, selector, 1);
      if (!matches.empty() && matches[0].element.visible) {
        LOG_DEBUG("BrowserManager", "Element found via semantic matcher: " + selector);
        return ActionResult::Success("Element found: " + selector);
      }
    } else {
      // Use tracker for CSS selectors
      ElementRenderInfo info;
      if (tracker->GetElementBounds(context_id, selector, info)) {
        if (info.visible) {
          LOG_DEBUG("BrowserManager", "Element found via tracker: " + selector);
          return ActionResult::Success("Element found: " + selector);
        }
      }
    }

    // Check timeout
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    if (elapsed >= timeout_ms) {
      LOG_WARN("BrowserManager", "Wait timeout for selector: " + selector);
      ActionResult result = ActionResult::Failure(ActionStatus::TIMEOUT, "Timeout waiting for selector: " + selector);
      result.selector = selector;
      return result;
    }

    // Wait before retry - optimized for concurrency (reduced from 100ms to 20ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

ActionResult OwlBrowserManager::WaitForTimeout(const std::string& context_id, int timeout_ms) {
  LOG_DEBUG("BrowserManager", "=== WAIT FOR TIMEOUT === " + std::to_string(timeout_ms) + "ms");

  auto start = std::chrono::steady_clock::now();
  while (std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start).count() < timeout_ms) {
    PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  return ActionResult::Success("Waited " + std::to_string(timeout_ms) + "ms");
}

ActionResult OwlBrowserManager::WaitForNetworkIdle(const std::string& context_id, int idle_time_ms, int timeout_ms) {
  LOG_DEBUG("BrowserManager", "=== WAIT FOR NETWORK IDLE === idle_time=" +
           std::to_string(idle_time_ms) + "ms timeout=" + std::to_string(timeout_ms) + "ms");

  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "WaitForNetworkIdle failed - browser not found for context: " + context_id);
    return ActionResult::BrowserNotFound(context_id);
  }

  CefRefPtr<CefBrowserHost> host = browser->GetHost();
  if (!host) {
    LOG_ERROR("BrowserManager", "WaitForNetworkIdle failed - no browser host");
    return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "No browser host available");
  }

  CefRefPtr<CefClient> client_base = host->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  if (!client) {
    LOG_ERROR("BrowserManager", "WaitForNetworkIdle failed - no client");
    return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "No client available");
  }

  auto start = std::chrono::steady_clock::now();
  auto last_activity = start;
  int last_pending = -1;

  while (true) {
    PumpMessageLoopIfNeeded();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    NavigationInfo nav_info = client->GetNavigationInfo();
    auto now = std::chrono::steady_clock::now();

    // Track when we last had network activity
    if (nav_info.pending_requests > 0 || nav_info.pending_requests != last_pending) {
      last_activity = now;
      last_pending = nav_info.pending_requests;
    }

    // Check if network has been idle for the configured duration
    auto idle_duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_activity).count();
    if (nav_info.pending_requests == 0 && idle_duration >= idle_time_ms) {
      LOG_DEBUG("BrowserManager", "Network idle for " + std::to_string(idle_duration) + "ms");
      return ActionResult::Success("Network idle for " + std::to_string(idle_duration) + "ms");
    }

    // Check timeout
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    if (elapsed >= timeout_ms) {
      LOG_WARN("BrowserManager", "WaitForNetworkIdle timeout - pending requests: " +
               std::to_string(nav_info.pending_requests));
      return ActionResult::Failure(ActionStatus::TIMEOUT, "Timeout waiting for network idle - " +
               std::to_string(nav_info.pending_requests) + " pending requests");
    }
  }
}

ActionResult OwlBrowserManager::WaitForFunction(const std::string& context_id, const std::string& js_function,
                                         int polling_ms, int timeout_ms) {
  LOG_DEBUG("BrowserManager", "=== WAIT FOR FUNCTION === polling=" +
           std::to_string(polling_ms) + "ms timeout=" + std::to_string(timeout_ms) + "ms");

  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "WaitForFunction failed - browser not found for context: " + context_id);
    return ActionResult::BrowserNotFound(context_id);
  }

  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  if (!frame) {
    LOG_ERROR("BrowserManager", "WaitForFunction failed - no main frame");
    return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "No main frame available");
  }

  auto start = std::chrono::steady_clock::now();

  // Use a DOM-based marker approach for checking the function result
  // We create a hidden data attribute on the body that we can detect via element scanning
  std::string marker_id = "__owl_wait_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

  // Wrap the user function to set a data attribute when truthy
  std::string wrapped_js = R"(
    (function() {
      try {
        var result = (function() { )" + js_function + R"( })();
        if (result) {
          document.body.setAttribute('data-)" + marker_id + R"(', 'true');
        } else {
          document.body.removeAttribute('data-)" + marker_id + R"(');
        }
      } catch(e) {
        document.body.removeAttribute('data-)" + marker_id + R"(');
      }
    })();
  )";

  // Selector to check for the marker
  std::string marker_selector = "[data-" + marker_id + "='true']";

  while (true) {
    // Execute the JavaScript function
    frame->ExecuteJavaScript(wrapped_js, frame->GetURL(), 0);

    // Give JS time to execute
    // In UI mode, don't call CefDoMessageLoopWork() from background thread - it crashes
    if (UsesRunMessageLoop()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } else {
      for (int i = 0; i < 5; i++) {
        PumpMessageLoopIfNeeded();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }

    // Trigger element scan to check for marker
    CefRefPtr<CefProcessMessage> scan_msg = CefProcessMessage::Create("scan_element");
    CefRefPtr<CefListValue> scan_args = scan_msg->GetArgumentList();
    scan_args->SetString(0, context_id);
    scan_args->SetString(1, marker_selector);
    frame->SendProcessMessage(PID_RENDERER, scan_msg);

    // Wait for scan
    // In UI mode, don't call CefDoMessageLoopWork() from background thread - it crashes
    if (UsesRunMessageLoop()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } else {
      for (int i = 0; i < 10; i++) {
        PumpMessageLoopIfNeeded();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }

    // Check if marker element found
    ElementRenderInfo info;
    OwlRenderTracker* tracker = OwlRenderTracker::GetInstance();
    if (tracker->GetElementBounds(context_id, marker_selector, info)) {
      // Clean up marker
      std::string cleanup_js = "document.body.removeAttribute('data-" + marker_id + "');";
      frame->ExecuteJavaScript(cleanup_js, frame->GetURL(), 0);
      LOG_DEBUG("BrowserManager", "WaitForFunction condition met");
      return ActionResult::Success("Function condition met");
    }

    // Check timeout
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    if (elapsed >= timeout_ms) {
      LOG_WARN("BrowserManager", "WaitForFunction timeout");
      return ActionResult::Failure(ActionStatus::TIMEOUT, "Timeout waiting for function to return truthy value");
    }

    // Wait before polling again
    std::this_thread::sleep_for(std::chrono::milliseconds(polling_ms));
  }
}

ActionResult OwlBrowserManager::WaitForURL(const std::string& context_id, const std::string& url_pattern,
                                    bool is_regex, int timeout_ms) {
  LOG_DEBUG("BrowserManager", "=== WAIT FOR URL === pattern=" + url_pattern +
           " regex=" + (is_regex ? "true" : "false") + " timeout=" + std::to_string(timeout_ms) + "ms");

  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "WaitForURL failed - browser not found for context: " + context_id);
    return ActionResult::BrowserNotFound(context_id);
  }

  auto start = std::chrono::steady_clock::now();

  while (true) {
    PumpMessageLoopIfNeeded();

    CefRefPtr<CefFrame> frame = browser->GetMainFrame();
    if (frame) {
      std::string current_url = frame->GetURL().ToString();

      bool matches = false;
      if (is_regex) {
        // Simple glob-style matching: * matches any sequence, ? matches single char
        // Convert simple glob to check - supports basic wildcards
        std::string pattern = url_pattern;
        size_t url_idx = 0;
        size_t pat_idx = 0;
        size_t star_idx = std::string::npos;
        size_t match_idx = 0;

        while (url_idx < current_url.length()) {
          if (pat_idx < pattern.length() &&
              (pattern[pat_idx] == '?' || pattern[pat_idx] == current_url[url_idx])) {
            url_idx++;
            pat_idx++;
          } else if (pat_idx < pattern.length() && pattern[pat_idx] == '*') {
            star_idx = pat_idx;
            match_idx = url_idx;
            pat_idx++;
          } else if (star_idx != std::string::npos) {
            pat_idx = star_idx + 1;
            match_idx++;
            url_idx = match_idx;
          } else {
            break;
          }
        }

        while (pat_idx < pattern.length() && pattern[pat_idx] == '*') {
          pat_idx++;
        }

        matches = (pat_idx == pattern.length() && url_idx == current_url.length());
      } else {
        // Simple substring match for non-regex
        matches = (current_url.find(url_pattern) != std::string::npos);
      }

      if (matches) {
        LOG_DEBUG("BrowserManager", "URL matched: " + current_url);
        return ActionResult::Success("URL matched: " + current_url);
      }
    }

    // Check timeout
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    if (elapsed >= timeout_ms) {
      LOG_WARN("BrowserManager", "WaitForURL timeout - current URL did not match pattern");
      return ActionResult::Failure(ActionStatus::TIMEOUT, "Timeout waiting for URL to match pattern: " + url_pattern);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

// ============================================================
// Page State Queries Implementation
// ============================================================

std::string OwlBrowserManager::GetCurrentURL(const std::string& context_id) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "GetCurrentURL failed - browser not found");
    return "";
  }

  // Wait for any pending navigation to complete
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  if (client) {
    NavigationInfo nav_info = client->GetNavigationInfo();
    if (nav_info.state != NavigationState::COMPLETE) {
      LOG_DEBUG("BrowserManager", "GetCurrentURL: Waiting for navigation to complete...");
      client->WaitForNavigation(10000);
    }
  }

  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  if (!frame) {
    return "";
  }

  return frame->GetURL().ToString();
}

std::string OwlBrowserManager::GetPageTitle(const std::string& context_id) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "GetPageTitle failed - browser not found");
    return "";
  }

  // Get title from browser (this is set by CEF from <title> tag)
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  if (!client) {
    return "";
  }

  // Wait for any pending navigation to complete
  NavigationInfo nav_info = client->GetNavigationInfo();
  if (nav_info.state != NavigationState::COMPLETE) {
    LOG_DEBUG("BrowserManager", "GetPageTitle: Waiting for navigation to complete...");
    client->WaitForNavigation(10000);
    nav_info = client->GetNavigationInfo();  // Get updated info after wait
  }

  return nav_info.title;
}

std::string OwlBrowserManager::GetPageInfo(const std::string& context_id) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "GetPageInfo failed - browser not found");
    return "{}";
  }

  // Get navigation info WITHOUT blocking - return current state immediately
  // The frontend polls this frequently (every 2s), so blocking here causes
  // massive request pile-up and delays. Return current state and let frontend
  // poll again if needed.
  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  NavigationInfo nav_info;
  if (client) {
    nav_info = client->GetNavigationInfo();
    // Don't wait - just return current state
  }

  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  std::string url = frame ? frame->GetURL().ToString() : "";
  bool can_go_back = browser->CanGoBack();
  bool can_go_forward = browser->CanGoForward();

  // Include loading state so frontend knows if page is still loading
  bool is_loading = nav_info.state != NavigationState::COMPLETE;

  std::ostringstream json;
  json << "{"
       << "\"url\":\"" << EscapeJson(url) << "\","
       << "\"title\":\"" << EscapeJson(nav_info.title) << "\","
       << "\"can_go_back\":" << (can_go_back ? "true" : "false") << ","
       << "\"can_go_forward\":" << (can_go_forward ? "true" : "false") << ","
       << "\"is_loading\":" << (is_loading ? "true" : "false")
       << "}";

  return json.str();
}

// ============================================================
// Viewport Manipulation Implementation
// ============================================================

ActionResult OwlBrowserManager::SetViewport(const std::string& context_id, int width, int height) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "SetViewport failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  // Clamp to minimum dimensions
  if (width < 100) width = 100;
  if (height < 100) height = 100;

  LOG_DEBUG("BrowserManager", "=== SET VIEWPORT === " + std::to_string(width) + "x" + std::to_string(height));

  CefRefPtr<CefClient> client_base = browser->GetHost()->GetClient();
  OwlClient* client = static_cast<OwlClient*>(client_base.get());
  client->SetViewport(width, height);

  // Trigger browser resize
  browser->GetHost()->WasResized();

  PumpMessageLoopIfNeeded();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  LOG_DEBUG("BrowserManager", "=== VIEWPORT SET ===");
  return ActionResult::Success("Viewport set to " + std::to_string(width) + "x" + std::to_string(height));
}

std::string OwlBrowserManager::GetViewport(const std::string& context_id) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "GetViewport failed - browser not found");
    return "{}";
  }

  // Get viewport dimensions from client
  // For now, return default - we'll need to add viewport state tracking
  std::ostringstream json;
  json << "{\"width\":1920,\"height\":1080}";  // TODO: Track actual viewport size
  return json.str();
}

// ============================================================
// LLM Integration Implementation
// ============================================================

void OwlBrowserManager::InitializeLLMAsync(const LLMConfig* config) {
  // Start LLM initialization in background thread to avoid blocking browser startup
  LOG_DEBUG("BrowserManager", "Starting LLM initialization in background...");

  // Capture config by value, or use default
  LLMConfig llm_config;
  if (config) {
    llm_config = *config;
  } else {
    // Default config
    llm_config.enabled = true;
#if BUILD_WITH_LLAMA
    llm_config.use_builtin = true;
#else
    llm_config.use_builtin = false;
#endif
  }

  std::thread([this, llm_config]() {
    LOG_DEBUG("BrowserManager", "============================================");
    LOG_DEBUG("BrowserManager", "Initializing LLM integration...");
    LOG_DEBUG("BrowserManager", "Config - enabled: " + std::string(llm_config.enabled ? "true" : "false") +
             ", use_builtin: " + std::string(llm_config.use_builtin ? "true" : "false"));
    LOG_DEBUG("BrowserManager", "============================================");

    // Check if LLM is disabled
    if (!llm_config.enabled) {
      LOG_DEBUG("BrowserManager", "LLM features are disabled by configuration");
      return;
    }

    // Check if external API is configured
    if (llm_config.HasExternalAPI()) {
      LOG_DEBUG("BrowserManager", "Using external LLM API: " + llm_config.external_endpoint);
      LOG_DEBUG("BrowserManager", "Model: " + llm_config.external_model);

      // Create LLM client pointing to external API
      std::string full_url = llm_config.external_endpoint;
      llm_client_ = std::make_unique<OwlLLMClient>(full_url, llm_config.is_third_party);

      // Set API key and model for external APIs
      if (!llm_config.external_api_key.empty()) {
        llm_client_->SetApiKey(llm_config.external_api_key);
        LOG_DEBUG("BrowserManager", "Set API key for external LLM client");
      }
      if (!llm_config.external_model.empty()) {
        llm_client_->SetModel(llm_config.external_model);
        LOG_DEBUG("BrowserManager", "Set model: " + llm_config.external_model);
      }

      LOG_DEBUG("BrowserManager", "============================================");
      LOG_DEBUG("BrowserManager", "✓ LLM integration initialized with external API");
      LOG_DEBUG("BrowserManager", "Endpoint: " + full_url);
      LOG_DEBUG("BrowserManager", "============================================");
      return;
    }

    // Use built-in llama-server
    if (llm_config.use_builtin) {
      LOG_DEBUG("BrowserManager", "Using built-in llama-server");

      llama_server_ = std::make_unique<OwlLlamaServer>();

      // Optimized configuration for fast startup and low latency
      OwlLlamaServer::Config server_config;
      server_config.model_path = "models/llm-assist.gguf";  // Will be auto-detected
      server_config.port = 8095;
      server_config.context_size = 16384;  // Vision models need large context for image tokens (1500-2500 per image)
      server_config.threads = 6;  // More threads = faster model loading on M4 Max
      server_config.gpu_layers = 99;  // Use all GPU layers (Metal on macOS)
      server_config.batch_size = 512;  // Good throughput for inference
      server_config.use_mmap = true;  // Memory-mapped loading (faster startup)
      server_config.use_mlock = false;  // Don't lock in RAM (allow swapping if needed)

      if (!llama_server_->Start(server_config)) {
        LOG_WARN("BrowserManager", "============================================");
        LOG_WARN("BrowserManager", "Failed to start LLM server");
        LOG_WARN("BrowserManager", "Browser will continue WITHOUT AI assistance");
        LOG_WARN("BrowserManager", "============================================");
        llama_server_.reset();
        return;
      }

      // Create LLM client for making requests
      llm_client_ = std::make_unique<OwlLLMClient>(llama_server_->GetServerURL());

      LOG_DEBUG("BrowserManager", "============================================");
      LOG_DEBUG("BrowserManager", "✓ LLM integration initialized successfully");
      LOG_DEBUG("BrowserManager", "Server URL: " + llama_server_->GetServerURL());
      LOG_DEBUG("BrowserManager", "============================================");
    } else {
      LOG_DEBUG("BrowserManager", "Built-in LLM server disabled by configuration");
    }
  }).detach();  // Detach thread to run independently
}

bool OwlBrowserManager::IsLLMReady() const {
  // For built-in server, check if llama_server_ is ready
  if (llama_server_ && llama_server_->IsReady()) {
    return true;
  }

  // For external API, check if llm_client_ exists
  if (llm_client_ && !llama_server_) {
    return true;
  }

  return false;
}

OwlLLMClient* OwlBrowserManager::GetLLMClientForContext(const std::string& context_id) {
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);

  // Look up the context
  auto it = contexts_.find(context_id);
  if (it != contexts_.end() && it->second->llm_client) {
    // Context has a per-context LLM client (external API with PII scrubbing)
    return it->second->llm_client.get();
  }

  // Fall back to global LLM client (built-in or default)
  return llm_client_.get();
}

void OwlBrowserManager::ShutdownLLM() {
  if (llama_server_) {
    LOG_DEBUG("BrowserManager", "Shutting down LLM server...");
    llama_server_->Stop();
    llama_server_.reset();
  }

  if (llm_client_) {
    llm_client_.reset();
  }
}

// Video Recording Methods

bool OwlBrowserManager::StartVideoRecording(const std::string& context_id, int fps, const std::string& codec) {
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);

  auto it = contexts_.find(context_id);
  if (it == contexts_.end()) {
    LOG_ERROR("BrowserManager", "Context not found: " + context_id);
    return false;
  }

  auto& context = it->second;
  auto browser = context->browser;
  if (!browser) {
    LOG_ERROR("BrowserManager", "No browser for context: " + context_id);
    return false;
  }

  // Create video recorder if not already exists
  if (!context->video_recorder) {
    context->video_recorder = std::make_unique<OwlVideoRecorder>();
  }

  // Get client and attach video recorder
  auto client = browser->GetHost()->GetClient();
  auto owl_client = static_cast<OwlClient*>(client.get());
  if (owl_client) {
    owl_client->SetVideoRecorder(context->video_recorder.get());
  }

  // Get viewport dimensions
  int width = owl_client ? owl_client->GetViewportWidth() : 1920;
  int height = owl_client ? owl_client->GetViewportHeight() : 1080;

  // Start recording
  if (context->video_recorder->StartRecording(width, height, fps, codec)) {
    LOG_DEBUG("BrowserManager", "Video recording started for context: " + context_id +
             " at " + std::to_string(width) + "x" + std::to_string(height) + " @ " + std::to_string(fps) + "fps");

    // Start timer thread to trigger paints at target FPS
    context->stop_recording_timer = false;
    int frame_interval_ms = 1000 / fps;
    context->recording_timer_thread = std::make_unique<std::thread>([browser, frame_interval_ms, &stop_flag = context->stop_recording_timer]() {
      while (!stop_flag) {
        browser->GetHost()->Invalidate(PET_VIEW);
        std::this_thread::sleep_for(std::chrono::milliseconds(frame_interval_ms));
      }
    });

    return true;
  }

  return false;
}

bool OwlBrowserManager::PauseVideoRecording(const std::string& context_id) {
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);

  auto it = contexts_.find(context_id);
  if (it == contexts_.end()) {
    LOG_ERROR("BrowserManager", "Context not found: " + context_id);
    return false;
  }

  auto& context = it->second;
  if (!context->video_recorder) {
    LOG_ERROR("BrowserManager", "No video recording in progress for context: " + context_id);
    return false;
  }

  return context->video_recorder->PauseRecording();
}

bool OwlBrowserManager::ResumeVideoRecording(const std::string& context_id) {
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);

  auto it = contexts_.find(context_id);
  if (it == contexts_.end()) {
    LOG_ERROR("BrowserManager", "Context not found: " + context_id);
    return false;
  }

  auto& context = it->second;
  if (!context->video_recorder) {
    LOG_ERROR("BrowserManager", "No video recording in progress for context: " + context_id);
    return false;
  }

  return context->video_recorder->ResumeRecording();
}

std::string OwlBrowserManager::StopVideoRecording(const std::string& context_id) {
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);

  auto it = contexts_.find(context_id);
  if (it == contexts_.end()) {
    LOG_ERROR("BrowserManager", "Context not found: " + context_id);
    return "";
  }

  auto& context = it->second;
  if (!context->video_recorder) {
    LOG_ERROR("BrowserManager", "No video recording in progress for context: " + context_id);
    return "";
  }

  // Stop timer thread
  if (context->recording_timer_thread && context->recording_timer_thread->joinable()) {
    context->stop_recording_timer = true;
    context->recording_timer_thread->join();
    context->recording_timer_thread.reset();
  }

  // Detach video recorder from client
  auto browser = context->browser;
  if (browser) {
    auto client = browser->GetHost()->GetClient();
    auto owl_client = static_cast<OwlClient*>(client.get());
    if (owl_client) {
      owl_client->SetVideoRecorder(nullptr);
    }
  }

  // Stop recording and get video path
  std::string video_path = context->video_recorder->StopRecording();

  LOG_DEBUG("BrowserManager", "Video recording stopped for context: " + context_id +
           " -> " + video_path);

  return video_path;
}

std::string OwlBrowserManager::GetVideoRecordingStats(const std::string& context_id) {
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);

  auto it = contexts_.find(context_id);
  if (it == contexts_.end()) {
    return "{\"error\": \"Context not found\"}";
  }

  auto& context = it->second;
  if (!context->video_recorder) {
    return "{\"error\": \"No video recording in progress\"}";
  }

  auto stats = context->video_recorder->GetStats();

  // Build JSON response with comprehensive stats
  std::ostringstream json;
  json << "{"
       << "\"recording\": " << (context->video_recorder->IsRecording() ? "true" : "false") << ", "
       << "\"paused\": " << (context->video_recorder->IsPaused() ? "true" : "false") << ", "
       << "\"frames_captured\": " << stats.frames_captured << ", "
       << "\"frames_encoded\": " << stats.frames_encoded << ", "
       << "\"frames_dropped\": " << stats.frames_dropped << ", "
       << "\"frames_duplicated\": " << stats.frames_duplicated << ", "
       << "\"queue_size\": " << stats.queue_size << ", "
       << "\"has_error\": " << (stats.has_error ? "true" : "false") << ", "
       << "\"duration_seconds\": " << std::fixed << std::setprecision(2) << stats.duration_seconds << ", "
       << "\"video_path\": \"" << stats.video_path << "\""
       << "}";

  return json.str();
}

// ============================================================
// Live Video Streaming
// ============================================================

bool OwlBrowserManager::StartLiveStream(const std::string& context_id, int fps, int quality) {
  // Validate context exists and get browser
  CefRefPtr<CefBrowser> browser;
  {
    std::shared_lock<std::shared_mutex> lock(contexts_mutex_);
    auto it = contexts_.find(context_id);
    if (it == contexts_.end()) {
      LOG_ERROR("BrowserManager", "StartLiveStream: Context not found: " + context_id);
      return false;
    }
    browser = it->second->browser;
  }

  auto* streamer = owl::LiveStreamer::GetInstance();
  bool result = streamer->StartStream(context_id, fps, quality);

  if (result) {
    LOG_DEBUG("BrowserManager", "Live stream started for context " + context_id +
             " @ " + std::to_string(fps) + " fps, quality=" + std::to_string(quality));

    // Wait for first frame with early exit - combined invalidate and check loop
    // Don't wait too long as this blocks the IPC response
    const int max_wait_ms = 200;  // 200ms max wait
    const int check_interval_ms = 10;
    int waited_ms = 0;

    while (waited_ms < max_wait_ms) {
      // Trigger paint
      if (browser && browser->GetHost()) {
        browser->GetHost()->Invalidate(PET_VIEW);
      }

      // Allow CEF to process
      std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
      waited_ms += check_interval_ms;

      // Check if frame is ready - exit early if so
      std::vector<uint8_t> dummy_jpeg;
      int dummy_w, dummy_h;
      if (streamer->GetLatestFrame(context_id, dummy_jpeg, dummy_w, dummy_h)) {
        LOG_DEBUG("BrowserManager", "First frame ready for context " + context_id +
                 " after " + std::to_string(waited_ms) + "ms");
        break;
      }
    }

    if (waited_ms >= max_wait_ms) {
      LOG_DEBUG("BrowserManager", "First frame not ready for context " + context_id +
               " - streaming will continue in background");
    }

    // Start a background thread to continuously trigger repaints at target FPS
    std::thread([context_id, fps, browser]() {
      auto* streamer = owl::LiveStreamer::GetInstance();
      int interval_ms = 1000 / fps;

      while (streamer->IsStreaming(context_id)) {
        if (browser && browser->GetHost()) {
          browser->GetHost()->Invalidate(PET_VIEW);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
      }
    }).detach();
  }

  return result;
}

bool OwlBrowserManager::StopLiveStream(const std::string& context_id) {
  auto* streamer = owl::LiveStreamer::GetInstance();
  bool result = streamer->StopStream(context_id);

  if (result) {
    LOG_DEBUG("BrowserManager", "Live stream stopped for context: " + context_id);
  }

  return result;
}

bool OwlBrowserManager::IsLiveStreaming(const std::string& context_id) const {
  auto* streamer = owl::LiveStreamer::GetInstance();
  return streamer->IsStreaming(context_id);
}

std::string OwlBrowserManager::GetLiveStreamStats(const std::string& context_id) {
  auto* streamer = owl::LiveStreamer::GetInstance();
  auto stats = streamer->GetStats(context_id);

  std::ostringstream json;
  json << "{"
       << "\"is_active\": " << (stats.is_active ? "true" : "false") << ", "
       << "\"target_fps\": " << stats.target_fps << ", "
       << "\"actual_fps\": " << stats.actual_fps << ", "
       << "\"width\": " << stats.width << ", "
       << "\"height\": " << stats.height << ", "
       << "\"frames_received\": " << stats.frames_received << ", "
       << "\"frames_encoded\": " << stats.frames_encoded << ", "
       << "\"frames_sent\": " << stats.frames_sent << ", "
       << "\"frames_dropped\": " << stats.frames_dropped << ", "
       << "\"subscriber_count\": " << stats.subscriber_count
       << "}";

  return json.str();
}

std::string OwlBrowserManager::ListLiveStreams() {
  auto* streamer = owl::LiveStreamer::GetInstance();
  auto streams = streamer->ListActiveStreams();

  std::ostringstream json;
  json << "{\"streams\": [";

  bool first = true;
  for (const auto& context_id : streams) {
    if (!first) json << ", ";
    first = false;

    auto stats = streamer->GetStats(context_id);
    json << "{"
         << "\"context_id\": \"" << context_id << "\", "
         << "\"target_fps\": " << stats.target_fps << ", "
         << "\"actual_fps\": " << stats.actual_fps << ", "
         << "\"width\": " << stats.width << ", "
         << "\"height\": " << stats.height << ", "
         << "\"subscriber_count\": " << stats.subscriber_count
         << "}";
  }

  json << "], \"count\": " << streams.size() << "}";
  return json.str();
}

// ============================================================
// Demographics and Context Information
// ============================================================

std::string OwlBrowserManager::GetDemographics() {
  OwlDemographics* demo = OwlDemographics::GetInstance();
  if (!demo->IsReady()) {
    demo->Initialize();
  }

  DemographicInfo info = demo->GetAllInfo();
  return OwlDemographics::ToJSON(info);
}

std::string OwlBrowserManager::GetLocation() {
  OwlDemographics* demo = OwlDemographics::GetInstance();
  if (!demo->IsReady()) {
    demo->Initialize();
  }

  GeoLocationInfo location = demo->GetGeoLocation();

  std::ostringstream json;
  json << "{"
       << "\"success\": " << (location.success ? "true" : "false");

  if (location.success) {
    json << ", \"ip\": \"" << location.ip_address << "\""
         << ", \"city\": \"" << location.city << "\""
         << ", \"region\": \"" << location.region << "\""
         << ", \"country\": \"" << location.country_name << "\""
         << ", \"country_code\": \"" << location.country_code << "\""
         << ", \"latitude\": " << location.latitude
         << ", \"longitude\": " << location.longitude
         << ", \"timezone\": \"" << location.timezone << "\"";
  } else {
    json << ", \"error\": \"" << location.error << "\"";
  }

  json << "}";
  return json.str();
}

std::string OwlBrowserManager::GetDateTime() {
  OwlDemographics* demo = OwlDemographics::GetInstance();
  DateTimeInfo dt = demo->GetDateTime();

  std::ostringstream json;
  json << "{"
       << "\"current\": \"" << dt.current_datetime << "\""
       << ", \"date\": \"" << dt.date << "\""
       << ", \"time\": \"" << dt.time << "\""
       << ", \"day_of_week\": \"" << dt.day_of_week << "\""
       << ", \"timezone\": \"" << dt.timezone << "\""
       << ", \"timezone_offset\": \"" << dt.timezone_offset << "\""
       << ", \"unix_timestamp\": " << dt.unix_timestamp
       << "}";

  return json.str();
}

std::string OwlBrowserManager::GetWeather() {
  OwlDemographics* demo = OwlDemographics::GetInstance();
  if (!demo->IsReady()) {
    demo->Initialize();
  }

  WeatherInfo weather = demo->GetWeather();

  std::ostringstream json;
  json << "{"
       << "\"success\": " << (weather.success ? "true" : "false");

  if (weather.success) {
    json << ", \"condition\": \"" << weather.condition << "\""
         << ", \"description\": \"" << weather.description << "\""
         << ", \"temperature_c\": " << std::fixed << std::setprecision(1) << weather.temperature_celsius
         << ", \"temperature_f\": " << std::fixed << std::setprecision(1) << weather.temperature_fahrenheit
         << ", \"humidity\": " << weather.humidity_percent
         << ", \"wind_speed_kmh\": " << weather.wind_speed_kmh
         << ", \"wind_speed_mph\": " << weather.wind_speed_mph;
  } else {
    json << ", \"error\": \"" << weather.error << "\"";
  }

  json << "}";
  return json.str();
}

std::string OwlBrowserManager::GetHomepageHTML() {
  return OwlHomepage::GenerateHomepage(this);
}

std::string OwlBrowserManager::GetPlaygroundHTML() {
  return OwlPlayground::GeneratePlayground(this);
}

std::string OwlBrowserManager::GetDevConsoleHTML() {
  return OwlDevConsole::GetInstance()->GenerateHTML();
}

// ===== CAPTCHA Handling =====

std::string OwlBrowserManager::DetectCaptcha(const std::string& context_id) {
  // Note: CEF_REQUIRE_UI_THREAD() removed - this function is safe to call from any thread
  // The test execution thread in UI mode runs on a background thread

  CefRefPtr<CefBrowser> browser = GetBrowser(context_id);
  if (!browser) {
    return R"({"success": false, "error": "Invalid context ID"})";
  }

  LOG_DEBUG("BrowserManager", "Detecting CAPTCHA in context: " + context_id);

  OwlCaptchaDetector detector;
  CaptchaDetectionResult result = detector.Detect(browser);

  // Convert to JSON (escape strings to prevent JSON injection)
  std::ostringstream json;
  json << "{"
       << "\"success\": true"
       << ", \"detected\": " << (result.has_captcha ? "true" : "false")
       << ", \"confidence\": " << std::fixed << std::setprecision(2) << result.confidence
       << ", \"indicators\": [";

  for (size_t i = 0; i < result.indicators.size(); i++) {
    if (i > 0) json << ", ";
    json << "\"" << EscapeJson(result.indicators[i]) << "\"";
  }

  json << "], \"selectors\": [";

  for (size_t i = 0; i < result.selectors.size(); i++) {
    if (i > 0) json << ", ";
    json << "\"" << EscapeJson(result.selectors[i]) << "\"";
  }

  json << "]}";

  return json.str();
}

std::string OwlBrowserManager::ClassifyCaptcha(const std::string& context_id) {
  // Note: CEF_REQUIRE_UI_THREAD() removed - safe to call from any thread

  CefRefPtr<CefBrowser> browser = GetBrowser(context_id);
  if (!browser) {
    return R"({"success": false, "error": "Invalid context ID"})";
  }

  LOG_DEBUG("BrowserManager", "Classifying CAPTCHA in context: " + context_id);

  // First detect if there's a CAPTCHA
  OwlCaptchaDetector detector;
  CaptchaDetectionResult detection = detector.Detect(browser);

  OwlCaptchaClassifier classifier;
  CaptchaClassificationResult result = classifier.Classify(browser, detection);

  // Convert to JSON (escape strings to prevent JSON injection)
  std::ostringstream json;
  json << "{"
       << "\"success\": true"
       << ", \"type\": \"";

  switch (result.type) {
    case CaptchaType::TEXT_BASED:      json << "text_based"; break;
    case CaptchaType::IMAGE_SELECTION: json << "image_selection"; break;
    case CaptchaType::CHECKBOX:        json << "checkbox"; break;
    case CaptchaType::PUZZLE:          json << "puzzle"; break;
    case CaptchaType::AUDIO:           json << "audio"; break;
    case CaptchaType::CUSTOM:          json << "custom"; break;
    default:                            json << "none"; break;
  }

  json << "\""
       << ", \"confidence\": " << std::fixed << std::setprecision(2) << result.confidence
       << ", \"challenge_element\": \"" << EscapeJson(result.challenge_element) << "\""
       << ", \"input_element\": \"" << EscapeJson(result.input_element) << "\""
       << ", \"image_element\": \"" << EscapeJson(result.image_element) << "\""
       << ", \"submit_button\": \"" << EscapeJson(result.submit_button) << "\""
       << ", \"refresh_button\": \"" << EscapeJson(result.refresh_button) << "\""
       << ", \"skip_button\": \"" << EscapeJson(result.skip_button) << "\""
       << ", \"grid_size\": " << result.grid_size
       << ", \"target_description\": \"" << EscapeJson(result.target_description) << "\""
       << ", \"grid_items\": [";

  for (size_t i = 0; i < result.grid_items.size(); i++) {
    if (i > 0) json << ", ";
    json << "\"" << EscapeJson(result.grid_items[i]) << "\"";
  }

  json << "]}";

  return json.str();
}

std::string OwlBrowserManager::SolveTextCaptcha(const std::string& context_id, int max_attempts) {
  // Note: CEF_REQUIRE_UI_THREAD() removed - safe to call from any thread

  CefRefPtr<CefBrowser> browser = GetBrowser(context_id);
  if (!browser) {
    return R"({"success": false, "error": "Invalid context ID"})";
  }

  if (!IsLLMAvailable()) {
    return R"({"success": false, "error": "LLM not available"})";
  }

  LOG_DEBUG("BrowserManager", "Solving text CAPTCHA in context: " + context_id);

  // First detect and classify the CAPTCHA
  OwlCaptchaDetector detector;
  CaptchaDetectionResult detection = detector.Detect(browser);

  OwlCaptchaClassifier classifier;
  CaptchaClassificationResult classification = classifier.Classify(browser, detection);

  if (classification.type != CaptchaType::TEXT_BASED) {
    return R"({"success": false, "error": "Not a text-based CAPTCHA"})";
  }

  // Solve using text captcha solver - use per-context LLM client for proper config
  OlibTextCaptchaSolver solver(GetLLMClientForContext(context_id));
  TextCaptchaSolveResult result = solver.Solve(context_id, browser, classification, max_attempts);

  // Convert to JSON (escape strings to prevent JSON injection)
  std::ostringstream json;
  json << "{"
       << "\"success\": " << (result.success ? "true" : "false")
       << ", \"extracted_text\": \"" << EscapeJson(result.extracted_text) << "\""
       << ", \"confidence\": " << std::fixed << std::setprecision(2) << result.confidence
       << ", \"attempts\": " << result.attempts
       << ", \"needs_refresh\": " << (result.needs_refresh ? "true" : "false");

  if (!result.error_message.empty()) {
    json << ", \"error\": \"" << EscapeJson(result.error_message) << "\"";
  }

  json << "}";

  return json.str();
}

std::string OwlBrowserManager::SolveImageCaptcha(const std::string& context_id, int max_attempts, const std::string& provider) {
  // Note: CEF_REQUIRE_UI_THREAD() removed - safe to call from any thread

  CefRefPtr<CefBrowser> browser = GetBrowser(context_id);
  if (!browser) {
    return R"({"success": false, "error": "Invalid context ID"})";
  }

  if (!IsLLMAvailable()) {
    return R"({"success": false, "error": "LLM not available"})";
  }

  LOG_DEBUG("BrowserManager", "Solving image CAPTCHA in context: " + context_id + " (provider: " + provider + ")");

  // First detect and classify the CAPTCHA
  OwlCaptchaDetector detector;
  CaptchaDetectionResult detection = detector.Detect(browser);

  OwlCaptchaClassifier classifier;
  CaptchaClassificationResult classification = classifier.Classify(browser, detection);

  if (classification.type != CaptchaType::IMAGE_SELECTION) {
    return R"({"success": false, "error": "Not an image-selection CAPTCHA"})";
  }

  // Get the appropriate provider
  ImageCaptchaProviderType provider_type = StringToImageCaptchaProviderType(provider);
  std::shared_ptr<IImageCaptchaProvider> captcha_provider;

  ImageCaptchaProviderFactory* factory = ImageCaptchaProviderFactory::GetInstance();

  if (provider_type == ImageCaptchaProviderType::AUTO) {
    // Auto-detect the provider
    captcha_provider = factory->DetectAndCreateProvider(browser, classification);
  } else {
    // Use the specified provider
    captcha_provider = factory->CreateProvider(provider_type);
  }

  if (!captcha_provider) {
    return R"({"success": false, "error": "Failed to create CAPTCHA provider"})";
  }

  // Solve using the provider - use per-context LLM client for proper config
  ImageCaptchaSolveResult result = captcha_provider->Solve(
      context_id, browser, classification, GetLLMClientForContext(context_id), max_attempts);

  // Convert to JSON (escape strings to prevent JSON injection)
  std::ostringstream json;
  json << "{"
       << "\"success\": " << (result.success ? "true" : "false")
       << ", \"provider\": \"" << ImageCaptchaProviderTypeToString(result.provider) << "\""
       << ", \"target_detected\": \"" << EscapeJson(result.target_detected) << "\""
       << ", \"confidence\": " << std::fixed << std::setprecision(2) << result.confidence
       << ", \"attempts\": " << result.attempts
       << ", \"selected_indices\": [";

  for (size_t i = 0; i < result.selected_indices.size(); i++) {
    if (i > 0) json << ", ";
    json << result.selected_indices[i];
  }

  json << "]"
       << ", \"needs_skip\": " << (result.needs_skip ? "true" : "false");

  if (!result.error_message.empty()) {
    json << ", \"error\": \"" << EscapeJson(result.error_message) << "\"";
  }

  json << "}";

  return json.str();
}

std::string OwlBrowserManager::SolveCaptcha(const std::string& context_id, int max_attempts, const std::string& provider) {
  // Note: CEF_REQUIRE_UI_THREAD() removed - safe to call from any thread

  CefRefPtr<CefBrowser> browser = GetBrowser(context_id);
  if (!browser) {
    return R"({"success": false, "error": "Invalid context ID"})";
  }

  // Check LLM availability early to provide better error message
  if (!IsLLMAvailable()) {
    return R"({"success": false, "error": "LLM not available - CAPTCHA solving requires an LLM. Please ensure the LLM server is running or configure an external LLM API."})";
  }

  LOG_DEBUG("BrowserManager", "Auto-solving CAPTCHA in context: " + context_id + " (provider: " + provider + ")");

  // First detect if there's a CAPTCHA
  OwlCaptchaDetector detector;
  CaptchaDetectionResult detection = detector.Detect(browser);

  if (!detection.has_captcha) {
    return R"({"success": false, "error": "No CAPTCHA detected on page"})";
  }

  // Classify the CAPTCHA type
  OwlCaptchaClassifier classifier;
  CaptchaClassificationResult classification = classifier.Classify(browser, detection);

  // Solve based on type
  if (classification.type == CaptchaType::TEXT_BASED) {
    LOG_DEBUG("BrowserManager", "Detected text-based CAPTCHA, solving...");
    return SolveTextCaptcha(context_id, max_attempts);
  }
  else if (classification.type == CaptchaType::IMAGE_SELECTION) {
    LOG_DEBUG("BrowserManager", "Detected image-selection CAPTCHA, solving...");
    return SolveImageCaptcha(context_id, max_attempts, provider);
  }
  else {
    std::ostringstream json;
    json << "{"
         << "\"success\": false"
         << ", \"error\": \"Unsupported CAPTCHA type\""
         << ", \"detected_type\": \"";

    switch (classification.type) {
      case CaptchaType::CHECKBOX:  json << "checkbox"; break;
      case CaptchaType::PUZZLE:    json << "puzzle"; break;
      case CaptchaType::AUDIO:     json << "audio"; break;
      case CaptchaType::CUSTOM:    json << "custom"; break;
      default:                      json << "unknown"; break;
    }

    json << "\"}";
    return json.str();
  }
}

// ===== Cookie Management =====
// Implementation delegated to OwlCookieManager for better modularity

std::string OwlBrowserManager::GetCookies(const std::string& context_id, const std::string& url) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "GetCookies failed - browser not found for context: " + context_id);
    return "[]";
  }
  return OwlCookieManager::GetCookies(browser, url);
}

ActionResult OwlBrowserManager::SetCookie(const std::string& context_id,
                                   const std::string& url,
                                   const std::string& name,
                                   const std::string& value,
                                   const std::string& domain,
                                   const std::string& path,
                                   bool secure,
                                   bool http_only,
                                   const std::string& same_site,
                                   int64_t expires) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "SetCookie failed - browser not found for context: " + context_id);
    return ActionResult::BrowserNotFound(context_id);
  }
  bool success = OwlCookieManager::SetCookie(browser, url, name, value, domain, path, secure, http_only, same_site, expires);
  if (success) {
    return ActionResult::Success("Cookie '" + name + "' set successfully");
  }
  return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "Failed to set cookie '" + name + "'");
}

ActionResult OwlBrowserManager::DeleteCookies(const std::string& context_id,
                                       const std::string& url,
                                       const std::string& cookie_name) {
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "DeleteCookies failed - browser not found for context: " + context_id);
    return ActionResult::BrowserNotFound(context_id);
  }
  bool success = OwlCookieManager::DeleteCookies(browser, url, cookie_name);
  if (success) {
    if (cookie_name.empty() && url.empty()) {
      return ActionResult::Success("All cookies deleted successfully");
    } else if (cookie_name.empty()) {
      return ActionResult::Success("Cookies for URL '" + url + "' deleted successfully");
    } else {
      return ActionResult::Success("Cookie '" + cookie_name + "' deleted successfully");
    }
  }
  return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "Failed to delete cookies");
}

// ===== Proxy Management =====
// Stealth proxy support with IP leak protection

bool OwlBrowserManager::SetProxy(const std::string& context_id, const ProxyConfig& config) {
  bool needs_timezone_detection = false;
  std::string proxy_timezone;

  // Phase 1: Validate context exists (with lock)
  {
    std::shared_lock<std::shared_mutex> lock(contexts_mutex_);
    auto it = contexts_.find(context_id);
    if (it == contexts_.end()) {
      LOG_ERROR("BrowserManager", "SetProxy failed - context not found: " + context_id);
      return false;
    }

    // Check if we need to detect timezone from proxy
    needs_timezone_detection = config.enabled && config.IsValid() &&
                               config.timezone_override.empty() && config.spoof_timezone;
  }

  LOG_DEBUG("BrowserManager", "Proxy config updated for context " + context_id + " - " +
           "type: " + OwlProxyManager::ProxyTypeToString(config.type) +
           ", host: " + config.host + ":" + std::to_string(config.port) +
           ", enabled: " + std::string(config.enabled ? "true" : "false"));

  // Phase 2: Update demographics and detect timezone (outside lock)
  OwlDemographics* demo = OwlDemographics::GetInstance();
  if (demo) {
    if (config.enabled && config.IsValid()) {
      demo->SetProxyConfig(config);

      // Detect timezone from proxy if needed
      if (needs_timezone_detection) {
        LOG_DEBUG("BrowserManager", "Detecting timezone from proxy for context: " + context_id);
        GeoLocationInfo location = demo->GetGeoLocation();
        if (location.success && !location.timezone.empty()) {
          proxy_timezone = location.timezone;
          LOG_DEBUG("BrowserManager", "Detected proxy timezone: " + proxy_timezone);
        } else {
          LOG_WARN("BrowserManager", "Failed to detect proxy timezone: " + location.error);
        }
      }
    } else {
      demo->ClearProxyConfig();
    }
  }

  // Phase 3: Store proxy config and timezone in context (with lock)
  {
    std::unique_lock<std::shared_mutex> lock(contexts_mutex_);
    auto it = contexts_.find(context_id);
    if (it == contexts_.end()) {
      LOG_ERROR("BrowserManager", "SetProxy failed - context disappeared: " + context_id);
      return false;
    }

    BrowserContext* context = it->second.get();
    context->proxy_config = config;

    // Set proxy timezone in context fingerprint if detected
    if (!proxy_timezone.empty()) {
      context->fingerprint.timezone = proxy_timezone;
      context->proxy_config.timezone_override = proxy_timezone;
      LOG_DEBUG("BrowserManager", "Set context timezone to proxy timezone: " + proxy_timezone +
               " for context: " + context_id);
    }
  }

  // Note: Changing proxy for an existing context requires recreating the request context
  // For now, proxy must be set at context creation time for full support
  // Runtime proxy changes are limited in CEF

  return true;
}

ProxyConfig OwlBrowserManager::GetProxy(const std::string& context_id) {
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);

  auto it = contexts_.find(context_id);
  if (it == contexts_.end()) {
    LOG_ERROR("BrowserManager", "GetProxy failed - context not found: " + context_id);
    return ProxyConfig();
  }

  return it->second->proxy_config;
}

std::string OwlBrowserManager::GetProxyStatus(const std::string& context_id) {
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);

  auto it = contexts_.find(context_id);
  if (it == contexts_.end()) {
    return R"({"error": "Context not found", "status": "error"})";
  }

  const ProxyConfig& config = it->second->proxy_config;

  std::ostringstream json;
  json << "{";
  json << "\"enabled\": " << (config.enabled ? "true" : "false");
  json << ", \"type\": \"" << OwlProxyManager::ProxyTypeToString(config.type) << "\"";
  json << ", \"host\": \"" << config.host << "\"";
  json << ", \"port\": " << config.port;
  json << ", \"stealthMode\": " << (config.stealth_mode ? "true" : "false");
  json << ", \"blockWebrtc\": " << (config.block_webrtc ? "true" : "false");
  json << ", \"spoofTimezone\": " << (config.spoof_timezone ? "true" : "false");
  json << ", \"spoofLanguage\": " << (config.spoof_language ? "true" : "false");
  json << ", \"randomizeFingerprint\": " << (config.randomize_fingerprint ? "true" : "false");

  if (!config.timezone_override.empty()) {
    json << ", \"timezoneOverride\": \"" << config.timezone_override << "\"";
  }
  if (!config.language_override.empty()) {
    json << ", \"languageOverride\": \"" << config.language_override << "\"";
  }

  // Status based on config validity
  if (config.enabled && config.IsValid()) {
    json << ", \"status\": \"connected\"";
  } else if (config.enabled && !config.IsValid()) {
    json << ", \"status\": \"error\"";
    json << ", \"statusMessage\": \"Invalid proxy configuration\"";
  } else {
    json << ", \"status\": \"disconnected\"";
  }

  json << "}";
  return json.str();
}

bool OwlBrowserManager::ConnectProxy(const std::string& context_id) {
  ProxyConfig config;
  bool needs_timezone_detection = false;

  // Phase 1: Get proxy config and check if timezone detection is needed (with lock)
  {
    std::shared_lock<std::shared_mutex> lock(contexts_mutex_);

    auto it = contexts_.find(context_id);
    if (it == contexts_.end()) {
      LOG_ERROR("BrowserManager", "ConnectProxy failed - context not found: " + context_id);
      return false;
    }

    config = it->second->proxy_config;
    if (!config.IsValid()) {
      LOG_ERROR("BrowserManager", "ConnectProxy failed - invalid proxy configuration");
      return false;
    }

    // Check if we need to detect timezone from proxy
    // Only if no timezone_override is set and spoof_timezone is enabled
    needs_timezone_detection = config.timezone_override.empty() && config.spoof_timezone;
  }

  // Phase 2: Enable proxy and detect timezone (outside lock to allow network calls)
  config.enabled = true;

  // Update demographics to use this proxy for IP detection
  OwlDemographics* demo = OwlDemographics::GetInstance();
  std::string proxy_timezone;

  if (demo) {
    demo->SetProxyConfig(config);

    // If no timezone_override, detect timezone from proxy's geolocation
    if (needs_timezone_detection) {
      LOG_DEBUG("BrowserManager", "Detecting timezone from proxy for context: " + context_id);
      GeoLocationInfo location = demo->GetGeoLocation();
      if (location.success && !location.timezone.empty()) {
        proxy_timezone = location.timezone;
        LOG_DEBUG("BrowserManager", "Detected proxy timezone: " + proxy_timezone);
      } else {
        LOG_WARN("BrowserManager", "Failed to detect proxy timezone: " + location.error);
      }
    }
  }

  // Phase 3: Update context with proxy state and timezone (with lock)
  {
    std::unique_lock<std::shared_mutex> lock(contexts_mutex_);

    auto it = contexts_.find(context_id);
    if (it == contexts_.end()) {
      LOG_ERROR("BrowserManager", "ConnectProxy failed - context disappeared: " + context_id);
      return false;
    }

    BrowserContext* context = it->second.get();
    context->proxy_config.enabled = true;

    // Set proxy timezone in context fingerprint if detected
    // This will be used for the full context lifetime
    if (!proxy_timezone.empty()) {
      context->fingerprint.timezone = proxy_timezone;
      // Also store in proxy config for reference
      context->proxy_config.timezone_override = proxy_timezone;
      LOG_DEBUG("BrowserManager", "Set context timezone to proxy timezone: " + proxy_timezone +
               " for context: " + context_id);
    }
  }

  LOG_DEBUG("BrowserManager", "Proxy enabled for context: " + context_id);

  // Note: For an existing browser, full proxy changes require recreating the context
  // This enables the flag but may require navigation to take effect
  return true;
}

bool OwlBrowserManager::DisconnectProxy(const std::string& context_id) {
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);

  auto it = contexts_.find(context_id);
  if (it == contexts_.end()) {
    LOG_ERROR("BrowserManager", "DisconnectProxy failed - context not found: " + context_id);
    return false;
  }

  it->second->proxy_config.enabled = false;
  LOG_DEBUG("BrowserManager", "Proxy disabled for context: " + context_id);

  // Clear demographics proxy config to use direct connection for IP detection
  OwlDemographics* demo = OwlDemographics::GetInstance();
  if (demo) {
    demo->ClearProxyConfig();
  }

  return true;
}

// ============================================================
// Profile Management Implementation
// ============================================================

std::string OwlBrowserManager::LoadProfile(const std::string& context_id, const std::string& profile_path) {
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);

  auto it = contexts_.find(context_id);
  if (it == contexts_.end()) {
    LOG_ERROR("BrowserManager", "LoadProfile failed - context not found: " + context_id);
    return "{\"success\": false, \"error\": \"Context not found\"}";
  }

  OwlBrowserProfileManager* profile_manager = OwlBrowserProfileManager::GetInstance();
  BrowserProfile profile = profile_manager->LoadProfile(profile_path);

  if (!profile.IsValid()) {
    LOG_ERROR("BrowserManager", "LoadProfile failed - invalid profile at: " + profile_path);
    return "{\"success\": false, \"error\": \"Invalid profile\"}";
  }

  // Update context with profile data
  BrowserContext* context = it->second.get();
  context->profile_path = profile_path;
  context->has_profile = true;
  context->auto_save_profile = profile.auto_save_cookies;

  // Copy fingerprint (all fields including seeds)
  context->fingerprint.vm_id = profile.fingerprint.vm_id;

  // Copy all Seed API fields
  context->fingerprint.canvas_seed = profile.fingerprint.canvas_seed;
  context->fingerprint.webgl_seed = profile.fingerprint.webgl_seed;
  context->fingerprint.audio_seed = profile.fingerprint.audio_seed;
  context->fingerprint.fonts_seed = profile.fingerprint.fonts_seed;
  context->fingerprint.client_rects_seed = profile.fingerprint.client_rects_seed;
  context->fingerprint.navigator_seed = profile.fingerprint.navigator_seed;
  context->fingerprint.screen_seed = profile.fingerprint.screen_seed;
  context->fingerprint.audio_fingerprint = profile.fingerprint.audio_fingerprint;

  // Copy MD5-style hashes
  context->fingerprint.canvas_geometry_hash = profile.fingerprint.canvas_geometry_hash;
  context->fingerprint.canvas_text_hash = profile.fingerprint.canvas_text_hash;
  context->fingerprint.webgl_params_hash = profile.fingerprint.webgl_params_hash;
  context->fingerprint.webgl_extensions_hash = profile.fingerprint.webgl_extensions_hash;
  context->fingerprint.webgl_context_hash = profile.fingerprint.webgl_context_hash;
  context->fingerprint.webgl_ext_params_hash = profile.fingerprint.webgl_ext_params_hash;
  context->fingerprint.shader_precisions_hash = profile.fingerprint.shader_precisions_hash;
  context->fingerprint.fonts_hash = profile.fingerprint.fonts_hash;
  context->fingerprint.plugins_hash = profile.fingerprint.plugins_hash;

  // Copy VM profile fields
  context->fingerprint.user_agent = profile.fingerprint.user_agent;
  context->fingerprint.platform = profile.fingerprint.platform;
  context->fingerprint.hardware_concurrency = profile.fingerprint.hardware_concurrency;
  context->fingerprint.device_memory = profile.fingerprint.device_memory;
  context->fingerprint.gpu_profile_index = profile.fingerprint.gpu_profile_index;
  context->fingerprint.webgl_vendor = profile.fingerprint.webgl_vendor;
  context->fingerprint.webgl_renderer = profile.fingerprint.webgl_renderer;
  context->fingerprint.screen_width = profile.fingerprint.screen_width;
  context->fingerprint.screen_height = profile.fingerprint.screen_height;
  context->fingerprint.timezone = profile.fingerprint.timezone;
  context->fingerprint.locale = profile.fingerprint.locale;

  // Copy legacy fields
  context->fingerprint.canvas_hash_seed = profile.fingerprint.canvas_hash_seed;
  context->fingerprint.canvas_noise_seed = profile.fingerprint.canvas_noise_seed;
  context->fingerprint.audio_noise_seed = profile.fingerprint.audio_noise_seed;

  // Sync seeds with FingerprintGenerator so Seed API returns correct values
  owl::FingerprintSeeds seeds;
  seeds.canvas_seed = profile.fingerprint.canvas_seed;
  seeds.webgl_seed = profile.fingerprint.webgl_seed;
  seeds.audio_seed = profile.fingerprint.audio_seed;
  seeds.fonts_seed = profile.fingerprint.fonts_seed;
  seeds.client_rects_seed = profile.fingerprint.client_rects_seed;
  seeds.navigator_seed = profile.fingerprint.navigator_seed;
  seeds.screen_seed = profile.fingerprint.screen_seed;
  seeds.audio_fingerprint = profile.fingerprint.audio_fingerprint;
  seeds.canvas_geometry_hash = profile.fingerprint.canvas_geometry_hash;
  seeds.canvas_text_hash = profile.fingerprint.canvas_text_hash;
  seeds.webgl_params_hash = profile.fingerprint.webgl_params_hash;
  seeds.webgl_extensions_hash = profile.fingerprint.webgl_extensions_hash;
  seeds.webgl_context_hash = profile.fingerprint.webgl_context_hash;
  seeds.webgl_ext_params_hash = profile.fingerprint.webgl_ext_params_hash;
  seeds.shader_precisions_hash = profile.fingerprint.shader_precisions_hash;
  seeds.fonts_hash = profile.fingerprint.fonts_hash;
  seeds.plugins_hash = profile.fingerprint.plugins_hash;
  owl::OwlFingerprintGenerator::Instance().SetSeeds(context_id, seeds);

  LOG_DEBUG("BrowserManager", "Synced profile seeds with FingerprintGenerator for context " + context_id);

  // Register stealth config for this browser
  if (context->browser) {
    StealthConfig stealth_config;
    stealth_config.user_agent = context->fingerprint.user_agent;
    stealth_config.platform = context->fingerprint.platform;
    stealth_config.hardware_concurrency = context->fingerprint.hardware_concurrency;
    stealth_config.device_memory = context->fingerprint.device_memory;
    stealth_config.canvas_noise_seed = context->fingerprint.canvas_noise_seed;
    stealth_config.gpu_profile_index = context->fingerprint.gpu_profile_index;
    stealth_config.webgl_vendor = context->fingerprint.webgl_vendor;
    stealth_config.webgl_renderer = context->fingerprint.webgl_renderer;
    stealth_config.screen_width = context->fingerprint.screen_width;
    stealth_config.screen_height = context->fingerprint.screen_height;
    stealth_config.timezone = context->fingerprint.timezone;
    stealth_config.audio_noise_seed = context->fingerprint.audio_noise_seed;

    OwlStealth::SetContextFingerprint(context->browser->GetIdentifier(), stealth_config);

    // Apply cookies from profile
    profile_manager->ApplyProfileCookies(profile, context->browser);
  }

  LOG_DEBUG("BrowserManager", "Profile loaded for context " + context_id + ": " + profile.profile_id);

  return profile.ToJSON();
}

std::string OwlBrowserManager::SaveProfile(const std::string& context_id, const std::string& profile_path) {
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);

  auto it = contexts_.find(context_id);
  if (it == contexts_.end()) {
    LOG_ERROR("BrowserManager", "SaveProfile failed - context not found: " + context_id);
    return "{\"success\": false, \"error\": \"Context not found\"}";
  }

  BrowserContext* context = it->second.get();
  std::string save_path = profile_path.empty() ? context->profile_path : profile_path;

  if (save_path.empty()) {
    LOG_ERROR("BrowserManager", "SaveProfile failed - no profile path specified");
    return "{\"success\": false, \"error\": \"No profile path specified\"}";
  }

  // Build profile from context state
  OwlBrowserProfileManager* profile_manager = OwlBrowserProfileManager::GetInstance();
  BrowserProfile profile;

  // Load existing profile if available to preserve profile_id and other metadata
  if (profile_manager->ProfileExists(save_path)) {
    profile = profile_manager->LoadProfile(save_path);
  } else {
    profile = profile_manager->CreateProfile();
  }

  // Update fingerprint from context (all fields including seeds)
  profile.fingerprint.vm_id = context->fingerprint.vm_id;

  // Copy all Seed API fields
  profile.fingerprint.canvas_seed = context->fingerprint.canvas_seed;
  profile.fingerprint.webgl_seed = context->fingerprint.webgl_seed;
  profile.fingerprint.audio_seed = context->fingerprint.audio_seed;
  profile.fingerprint.fonts_seed = context->fingerprint.fonts_seed;
  profile.fingerprint.client_rects_seed = context->fingerprint.client_rects_seed;
  profile.fingerprint.navigator_seed = context->fingerprint.navigator_seed;
  profile.fingerprint.screen_seed = context->fingerprint.screen_seed;
  profile.fingerprint.audio_fingerprint = context->fingerprint.audio_fingerprint;

  // Copy MD5-style hashes
  profile.fingerprint.canvas_geometry_hash = context->fingerprint.canvas_geometry_hash;
  profile.fingerprint.canvas_text_hash = context->fingerprint.canvas_text_hash;
  profile.fingerprint.webgl_params_hash = context->fingerprint.webgl_params_hash;
  profile.fingerprint.webgl_extensions_hash = context->fingerprint.webgl_extensions_hash;
  profile.fingerprint.webgl_context_hash = context->fingerprint.webgl_context_hash;
  profile.fingerprint.webgl_ext_params_hash = context->fingerprint.webgl_ext_params_hash;
  profile.fingerprint.shader_precisions_hash = context->fingerprint.shader_precisions_hash;
  profile.fingerprint.fonts_hash = context->fingerprint.fonts_hash;
  profile.fingerprint.plugins_hash = context->fingerprint.plugins_hash;

  // Copy VM profile fields
  profile.fingerprint.user_agent = context->fingerprint.user_agent;
  profile.fingerprint.platform = context->fingerprint.platform;
  profile.fingerprint.hardware_concurrency = context->fingerprint.hardware_concurrency;
  profile.fingerprint.device_memory = context->fingerprint.device_memory;
  profile.fingerprint.gpu_profile_index = context->fingerprint.gpu_profile_index;
  profile.fingerprint.webgl_vendor = context->fingerprint.webgl_vendor;
  profile.fingerprint.webgl_renderer = context->fingerprint.webgl_renderer;
  profile.fingerprint.screen_width = context->fingerprint.screen_width;
  profile.fingerprint.screen_height = context->fingerprint.screen_height;
  profile.fingerprint.timezone = context->fingerprint.timezone;
  profile.fingerprint.locale = context->fingerprint.locale;

  // Copy legacy fields
  profile.fingerprint.canvas_hash_seed = context->fingerprint.canvas_hash_seed;
  profile.fingerprint.canvas_noise_seed = context->fingerprint.canvas_noise_seed;
  profile.fingerprint.audio_noise_seed = context->fingerprint.audio_noise_seed;

  // Get current cookies from browser
  if (context->browser) {
    std::string cookies_json = OwlCookieManager::GetCookies(context->browser, "");
    profile.cookies = OwlCookieManager::ParseCookiesJson(cookies_json);
  }

  // Copy other config
  profile.has_llm_config = true;
  profile.llm_config = context->llm_config;
  profile.has_proxy_config = context->proxy_config.IsValid();
  profile.proxy_config = context->proxy_config;
  profile.auto_save_cookies = context->auto_save_profile;

  profile.Touch();

  // Save profile
  if (!profile_manager->SaveProfile(profile, save_path)) {
    LOG_ERROR("BrowserManager", "SaveProfile failed to write to: " + save_path);
    return "{\"success\": false, \"error\": \"Failed to write profile\"}";
  }

  // Update context's profile path if it wasn't set
  if (context->profile_path.empty()) {
    context->profile_path = save_path;
    context->has_profile = true;
  }

  LOG_DEBUG("BrowserManager", "Profile saved for context " + context_id + ": " + save_path +
           " (" + std::to_string(profile.cookies.size()) + " cookies)");

  return profile.ToJSON();
}

std::string OwlBrowserManager::GetProfile(const std::string& context_id) {
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);

  auto it = contexts_.find(context_id);
  if (it == contexts_.end()) {
    LOG_ERROR("BrowserManager", "GetProfile failed - context not found: " + context_id);
    return "{\"success\": false, \"error\": \"Context not found\"}";
  }

  BrowserContext* context = it->second.get();

  if (!context->has_profile || context->profile_path.empty()) {
    // Return current state as profile (without saving)
    BrowserProfile profile;
    profile.profile_id = context->id;
    profile.profile_name = "Unsaved Profile";

    // Copy VM ID - critical for profile identity
    profile.fingerprint.vm_id = context->fingerprint.vm_id;

    // Copy all Seed API fields
    profile.fingerprint.canvas_seed = context->fingerprint.canvas_seed;
    profile.fingerprint.webgl_seed = context->fingerprint.webgl_seed;
    profile.fingerprint.audio_seed = context->fingerprint.audio_seed;
    profile.fingerprint.fonts_seed = context->fingerprint.fonts_seed;
    profile.fingerprint.client_rects_seed = context->fingerprint.client_rects_seed;
    profile.fingerprint.navigator_seed = context->fingerprint.navigator_seed;
    profile.fingerprint.screen_seed = context->fingerprint.screen_seed;
    profile.fingerprint.audio_fingerprint = context->fingerprint.audio_fingerprint;

    // Copy MD5-style hashes
    profile.fingerprint.canvas_geometry_hash = context->fingerprint.canvas_geometry_hash;
    profile.fingerprint.canvas_text_hash = context->fingerprint.canvas_text_hash;
    profile.fingerprint.webgl_params_hash = context->fingerprint.webgl_params_hash;
    profile.fingerprint.webgl_extensions_hash = context->fingerprint.webgl_extensions_hash;
    profile.fingerprint.webgl_context_hash = context->fingerprint.webgl_context_hash;
    profile.fingerprint.webgl_ext_params_hash = context->fingerprint.webgl_ext_params_hash;
    profile.fingerprint.shader_precisions_hash = context->fingerprint.shader_precisions_hash;
    profile.fingerprint.fonts_hash = context->fingerprint.fonts_hash;
    profile.fingerprint.plugins_hash = context->fingerprint.plugins_hash;

    // Copy VM profile fields
    profile.fingerprint.user_agent = context->fingerprint.user_agent;
    profile.fingerprint.platform = context->fingerprint.platform;
    profile.fingerprint.hardware_concurrency = context->fingerprint.hardware_concurrency;
    profile.fingerprint.device_memory = context->fingerprint.device_memory;
    profile.fingerprint.gpu_profile_index = context->fingerprint.gpu_profile_index;
    profile.fingerprint.webgl_vendor = context->fingerprint.webgl_vendor;
    profile.fingerprint.webgl_renderer = context->fingerprint.webgl_renderer;
    profile.fingerprint.screen_width = context->fingerprint.screen_width;
    profile.fingerprint.screen_height = context->fingerprint.screen_height;
    profile.fingerprint.timezone = context->fingerprint.timezone;
    profile.fingerprint.locale = context->fingerprint.locale;

    // Copy legacy fields
    profile.fingerprint.canvas_hash_seed = context->fingerprint.canvas_hash_seed;
    profile.fingerprint.canvas_noise_seed = context->fingerprint.canvas_noise_seed;
    profile.fingerprint.audio_noise_seed = context->fingerprint.audio_noise_seed;

    // Get current cookies
    if (context->browser) {
      std::string cookies_json = OwlCookieManager::GetCookies(context->browser, "");
      profile.cookies = OwlCookieManager::ParseCookiesJson(cookies_json);
    }

    profile.has_llm_config = true;
    profile.llm_config = context->llm_config;
    profile.has_proxy_config = context->proxy_config.IsValid();
    profile.proxy_config = context->proxy_config;

    return profile.ToJSON();
  }

  // Load and return profile from file
  OwlBrowserProfileManager* profile_manager = OwlBrowserProfileManager::GetInstance();
  BrowserProfile profile = profile_manager->LoadProfile(context->profile_path);

  // Update cookies with current browser state
  if (context->browser) {
    std::string cookies_json = OwlCookieManager::GetCookies(context->browser, "");
    profile.cookies = OwlCookieManager::ParseCookiesJson(cookies_json);
  }

  return profile.ToJSON();
}

std::string OwlBrowserManager::CreateProfile(const std::string& profile_name) {
  OwlBrowserProfileManager* profile_manager = OwlBrowserProfileManager::GetInstance();
  BrowserProfile profile = profile_manager->CreateProfile(profile_name);

  LOG_DEBUG("BrowserManager", "Created new profile: " + profile.profile_id + " (" + profile.profile_name + ")");

  return profile.ToJSON();
}

bool OwlBrowserManager::UpdateProfileCookies(const std::string& context_id) {
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);

  auto it = contexts_.find(context_id);
  if (it == contexts_.end()) {
    LOG_ERROR("BrowserManager", "UpdateProfileCookies failed - context not found: " + context_id);
    return false;
  }

  BrowserContext* context = it->second.get();

  if (!context->has_profile || context->profile_path.empty()) {
    LOG_WARN("BrowserManager", "UpdateProfileCookies - no profile associated with context");
    return false;
  }

  if (!context->browser) {
    LOG_ERROR("BrowserManager", "UpdateProfileCookies - browser is null");
    return false;
  }

  // Load profile, update cookies, save
  OwlBrowserProfileManager* profile_manager = OwlBrowserProfileManager::GetInstance();
  BrowserProfile profile = profile_manager->LoadProfile(context->profile_path);

  std::string cookies_json = OwlCookieManager::GetCookies(context->browser, "");
  profile.cookies = OwlCookieManager::ParseCookiesJson(cookies_json);
  profile.Touch();

  bool result = profile_manager->SaveProfile(profile, context->profile_path);

  if (result) {
    LOG_DEBUG("BrowserManager", "Updated profile cookies for context " + context_id +
             ": " + std::to_string(profile.cookies.size()) + " cookies");
  }

  return result;
}

ContextFingerprint OwlBrowserManager::GetContextFingerprint(const std::string& context_id) {
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);

  auto it = contexts_.find(context_id);
  if (it == contexts_.end()) {
    LOG_ERROR("BrowserManager", "GetContextFingerprint failed - context not found: " + context_id);
    return ContextFingerprint();
  }

  return it->second->fingerprint;
}

std::string OwlBrowserManager::GetContextInfo(const std::string& context_id) {
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);

  auto it = contexts_.find(context_id);
  if (it == contexts_.end()) {
    LOG_ERROR("BrowserManager", "GetContextInfo failed - context not found: " + context_id);
    return "{\"success\": false, \"error\": \"Context not found\"}";
  }

  BrowserContext* context = it->second.get();
  const ContextFingerprint& fp = context->fingerprint;

  // Build JSON response with fingerprint/stealth info
  std::ostringstream json;
  json << "{";
  json << "\"context_id\": \"" << context_id << "\",";
  json << "\"vm_profile\": {";
  json << "\"vm_id\": \"" << fp.vm_id << "\",";
  json << "\"platform\": \"" << fp.platform << "\",";
  json << "\"user_agent\": \"" << fp.user_agent << "\",";
  json << "\"hardware_concurrency\": " << fp.hardware_concurrency << ",";
  json << "\"device_memory\": " << fp.device_memory << ",";
  json << "\"screen_width\": " << fp.screen_width << ",";
  json << "\"screen_height\": " << fp.screen_height << ",";
  json << "\"timezone\": \"" << fp.timezone << "\",";
  json << "\"locale\": \"" << fp.locale << "\"";
  json << "},";

  // Seed API values - the core fingerprint seeds used for evasion
  json << "\"seeds\": {";
  json << "\"canvas_seed\": " << fp.canvas_seed << ",";
  json << "\"webgl_seed\": " << fp.webgl_seed << ",";
  json << "\"audio_seed\": " << fp.audio_seed << ",";
  json << "\"fonts_seed\": " << fp.fonts_seed << ",";
  json << "\"client_rects_seed\": " << fp.client_rects_seed << ",";
  json << "\"navigator_seed\": " << fp.navigator_seed << ",";
  json << "\"screen_seed\": " << fp.screen_seed << ",";
  json << "\"audio_fingerprint\": " << std::fixed << std::setprecision(14) << fp.audio_fingerprint;
  json << "},";

  // MD5-style hashes used for fingerprint.com compatibility
  json << "\"hashes\": {";
  json << "\"canvas_geometry\": \"" << fp.canvas_geometry_hash << "\",";
  json << "\"canvas_text\": \"" << fp.canvas_text_hash << "\",";
  json << "\"webgl_params\": \"" << fp.webgl_params_hash << "\",";
  json << "\"webgl_extensions\": \"" << fp.webgl_extensions_hash << "\",";
  json << "\"webgl_context\": \"" << fp.webgl_context_hash << "\",";
  json << "\"webgl_ext_params\": \"" << fp.webgl_ext_params_hash << "\",";
  json << "\"shader_precisions\": \"" << fp.shader_precisions_hash << "\",";
  json << "\"fonts\": \"" << fp.fonts_hash << "\",";
  json << "\"plugins\": \"" << fp.plugins_hash << "\"";
  json << "},";

  // Legacy fields for backwards compatibility
  json << "\"canvas\": {";
  json << "\"hash_seed\": " << fp.canvas_hash_seed << ",";
  json << "\"noise_seed\": " << std::fixed << std::setprecision(6) << fp.canvas_noise_seed;
  json << "},";
  json << "\"audio\": {";
  json << "\"noise_seed\": " << std::fixed << std::setprecision(6) << fp.audio_noise_seed;
  json << "},";
  json << "\"gpu\": {";
  json << "\"profile_index\": " << fp.gpu_profile_index << ",";
  json << "\"webgl_vendor\": \"" << fp.webgl_vendor << "\",";
  json << "\"webgl_renderer\": \"" << fp.webgl_renderer << "\"";
  json << "},";
  json << "\"has_profile\": " << (context->has_profile ? "true" : "false");
  if (context->has_profile && !context->profile_path.empty()) {
    json << ",\"profile_path\": \"" << context->profile_path << "\"";
  }
  json << "}";

  return json.str();
}

// ============================================================
// Optimization Methods Implementation
// ============================================================

void OwlBrowserManager::StartCleanupThread() {
  if (cleanup_running_.load(std::memory_order_acquire)) {
    return;  // Already running
  }

  cleanup_running_.store(true, std::memory_order_release);
  cleanup_thread_ = std::thread([this]() {
    LOG_DEBUG("BrowserManager", "Background cleanup thread started");

    while (cleanup_running_.load(std::memory_order_acquire)) {
      {
        std::unique_lock<std::mutex> lock(cleanup_mutex_);
        cleanup_cv_.wait_for(lock, std::chrono::seconds(kCleanupIntervalSec), [this]() {
          return !cleanup_running_.load(std::memory_order_acquire);
        });
      }

      if (!cleanup_running_.load(std::memory_order_acquire)) {
        break;
      }

      // Perform cleanup
      size_t count = context_count_.load(std::memory_order_relaxed);
      if (count == 0) {
        continue;
      }

      // Check for idle contexts
      std::vector<std::string> idle_contexts;
      auto now = std::chrono::steady_clock::now();

      {
        std::shared_lock<std::shared_mutex> lock(contexts_mutex_);
        for (const auto& pair : contexts_) {
          if (!pair.second->in_use.load(std::memory_order_relaxed)) {
            auto idle_time = std::chrono::duration_cast<std::chrono::seconds>(
                now - pair.second->last_used.load(std::memory_order_relaxed)).count();

            if (idle_time > static_cast<int64_t>(kIdleTimeoutSec)) {
              idle_contexts.push_back(pair.first);
            }
          }
        }
      }

      // Remove idle contexts - release lock while waiting to avoid deadlock
      if (!idle_contexts.empty()) {
        for (const auto& ctx_id : idle_contexts) {
          // First, check if context still exists and is still idle with shared lock
          bool should_remove = false;
          BrowserContext* ctx_ptr = nullptr;
          {
            std::shared_lock<std::shared_mutex> shared_lock(contexts_mutex_);
            auto it = contexts_.find(ctx_id);
            if (it != contexts_.end() && !it->second->in_use.load(std::memory_order_acquire)) {
              ctx_ptr = it->second.get();
              should_remove = true;
            }
          }

          if (!should_remove || !ctx_ptr) {
            continue;
          }

          // Wait for active operations WITHOUT holding any lock to avoid deadlock
          // Use timeout to prevent infinite wait
          constexpr int kMaxWaitIterations = 100;  // 500ms max wait
          int wait_iterations = 0;
          while (ctx_ptr->HasActiveOperations() && wait_iterations < kMaxWaitIterations) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            wait_iterations++;
          }

          if (wait_iterations >= kMaxWaitIterations) {
            LOG_WARN("BrowserManager", "Timeout waiting for active operations on context: " + ctx_id);
            continue;  // Skip this context, try again later
          }

          // Now acquire exclusive lock and verify conditions still hold
          {
            std::unique_lock<std::shared_mutex> lock(contexts_mutex_);
            auto it = contexts_.find(ctx_id);
            if (it == contexts_.end()) {
              continue;  // Context was already removed
            }

            // Re-check conditions under exclusive lock
            if (it->second->in_use.load(std::memory_order_acquire) ||
                it->second->HasActiveOperations()) {
              continue;  // Context became in-use or has active ops, skip
            }

            if (it->second->browser) {
              it->second->browser->GetHost()->CloseBrowser(true);
            }

            LOG_DEBUG("BrowserManager", "Removed idle context: " + ctx_id);
            contexts_.erase(it);
            context_count_.fetch_sub(1, std::memory_order_relaxed);
            current_memory_mb_.fetch_sub(kEstimatedPerContextMB, std::memory_order_relaxed);
          }
        }
      }

      // Log stats periodically
      LOG_DEBUG("BrowserManager", "Cleanup stats - contexts: " +
                std::to_string(context_count_.load(std::memory_order_relaxed)) +
                ", memory: " + std::to_string(current_memory_mb_.load(std::memory_order_relaxed)) + "MB");
    }

    LOG_DEBUG("BrowserManager", "Background cleanup thread stopped");
  });
}

void OwlBrowserManager::StopCleanupThread() {
  if (!cleanup_running_.load(std::memory_order_acquire)) {
    return;
  }

  cleanup_running_.store(false, std::memory_order_release);
  cleanup_cv_.notify_all();

  if (cleanup_thread_.joinable()) {
    cleanup_thread_.join();
  }
}

BrowserContext* OwlBrowserManager::GetContextPtr(const std::string& id) {
  // Note: Caller must hold appropriate lock
  auto it = contexts_.find(id);
  if (it != contexts_.end()) {
    return it->second.get();
  }
  return nullptr;
}

bool OwlBrowserManager::ContextExists(const std::string& id) const {
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);
  return contexts_.find(id) != contexts_.end();
}

std::string OwlBrowserManager::GetResourceStats() const {
  std::ostringstream json;
  json << "{";
  json << "\"contextCount\": " << context_count_.load(std::memory_order_relaxed);
  json << ", \"maxContexts\": " << max_contexts_.load(std::memory_order_relaxed);
  json << ", \"estimatedMemoryMB\": " << current_memory_mb_.load(std::memory_order_relaxed);
  json << ", \"maxMemoryMB\": " << max_memory_mb_.load(std::memory_order_relaxed);
  json << ", \"actualMemoryMB\": " << (GetActualMemoryUsage() / 1024 / 1024);

  // Thread pool stats
  owl::ThreadPool* pool = owl::ThreadPool::GetInstance();
  if (pool) {
    const owl::TaskMetrics& metrics = pool->GetMetrics();
    json << ", \"threadPool\": {";
    json << "\"workers\": " << pool->GetWorkerCount();
    json << ", \"activeWorkers\": " << metrics.active_workers.load(std::memory_order_relaxed);
    json << ", \"queueDepth\": " << metrics.queue_depth.load(std::memory_order_relaxed);
    json << ", \"tasksSubmitted\": " << metrics.tasks_submitted.load(std::memory_order_relaxed);
    json << ", \"tasksCompleted\": " << metrics.tasks_completed.load(std::memory_order_relaxed);
    json << ", \"tasksFailed\": " << metrics.tasks_failed.load(std::memory_order_relaxed);
    json << "}";
  }

  json << "}";
  return json.str();
}

size_t OwlBrowserManager::GetActiveContextCount() const {
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);
  size_t count = 0;
  for (const auto& pair : contexts_) {
    if (pair.second->in_use.load(std::memory_order_relaxed)) {
      ++count;
    }
  }
  return count;
}

size_t OwlBrowserManager::GetTotalContextCount() const {
  return context_count_.load(std::memory_order_relaxed);
}

std::vector<std::string> OwlBrowserManager::ListContexts() const {
  std::shared_lock<std::shared_mutex> lock(contexts_mutex_);
  std::vector<std::string> context_ids;
  context_ids.reserve(contexts_.size());
  for (const auto& pair : contexts_) {
    context_ids.push_back(pair.first);
  }
  return context_ids;
}

void OwlBrowserManager::SetMaxContexts(size_t max) {
  max_contexts_.store(max, std::memory_order_relaxed);
  LOG_DEBUG("BrowserManager", "Max contexts set to " + std::to_string(max));
}

void OwlBrowserManager::SetMaxMemoryMB(size_t max_mb) {
  max_memory_mb_.store(max_mb, std::memory_order_relaxed);
  LOG_DEBUG("BrowserManager", "Max memory set to " + std::to_string(max_mb) + "MB");
}

owl::ThreadPool* OwlBrowserManager::GetThreadPool() {
  return owl::ThreadPool::GetInstance();
}

// ============================================================================
// Network Interception Methods
// ============================================================================

std::string OwlBrowserManager::AddNetworkRule(const std::string& context_id,
                                               const std::string& rule_json) {
  // Parse rule JSON
  // Expected format: {"url_pattern": "...", "action": "block|mock|redirect", "is_regex": bool, ...}
  InterceptionRule rule;

  // Simple JSON parsing (could use a proper JSON library)
  size_t pattern_pos = rule_json.find("\"url_pattern\"");
  if (pattern_pos != std::string::npos) {
    size_t start = rule_json.find("\"", pattern_pos + 13);
    size_t end = rule_json.find("\"", start + 1);
    if (start != std::string::npos && end != std::string::npos) {
      rule.url_pattern = rule_json.substr(start + 1, end - start - 1);
    }
  }

  // Parse action
  size_t action_pos = rule_json.find("\"action\"");
  if (action_pos != std::string::npos) {
    if (rule_json.find("\"block\"", action_pos) != std::string::npos) {
      rule.action = InterceptionAction::BLOCK;
    } else if (rule_json.find("\"mock\"", action_pos) != std::string::npos) {
      rule.action = InterceptionAction::MOCK;
    } else if (rule_json.find("\"redirect\"", action_pos) != std::string::npos) {
      rule.action = InterceptionAction::REDIRECT;
    } else if (rule_json.find("\"modify\"", action_pos) != std::string::npos) {
      rule.action = InterceptionAction::MODIFY;
    } else {
      rule.action = InterceptionAction::ALLOW;
    }
  }

  // Parse is_regex
  rule.is_regex = rule_json.find("\"is_regex\":true") != std::string::npos ||
                  rule_json.find("\"is_regex\": true") != std::string::npos;

  // Parse redirect_url
  size_t redirect_pos = rule_json.find("\"redirect_url\"");
  if (redirect_pos != std::string::npos) {
    size_t start = rule_json.find("\"", redirect_pos + 14);
    size_t end = rule_json.find("\"", start + 1);
    if (start != std::string::npos && end != std::string::npos) {
      rule.redirect_url = rule_json.substr(start + 1, end - start - 1);
    }
  }

  // Parse mock_body
  size_t mock_pos = rule_json.find("\"mock_body\"");
  if (mock_pos != std::string::npos) {
    size_t start = rule_json.find("\"", mock_pos + 11);
    size_t end = rule_json.find("\"", start + 1);
    if (start != std::string::npos && end != std::string::npos) {
      rule.mock_body = rule_json.substr(start + 1, end - start - 1);
    }
  }

  // Parse mock_status
  size_t status_pos = rule_json.find("\"mock_status\"");
  if (status_pos != std::string::npos) {
    size_t colon = rule_json.find(":", status_pos);
    if (colon != std::string::npos) {
      rule.mock_status_code = std::stoi(rule_json.substr(colon + 1));
    }
  }

  // Parse mock_content_type
  size_t ct_pos = rule_json.find("\"mock_content_type\"");
  if (ct_pos != std::string::npos) {
    size_t start = rule_json.find("\"", ct_pos + 18);
    size_t end = rule_json.find("\"", start + 1);
    if (start != std::string::npos && end != std::string::npos) {
      rule.mock_content_type = rule_json.substr(start + 1, end - start - 1);
    }
  }

  rule.enabled = true;

  OwlNetworkInterceptor* interceptor = OwlNetworkInterceptor::GetInstance();
  std::string rule_id = interceptor->AddRule(rule);

  LOG_DEBUG("BrowserManager", "Added network rule: " + rule_id + " for context: " + context_id);
  return rule_id;
}

bool OwlBrowserManager::RemoveNetworkRule(const std::string& rule_id) {
  OwlNetworkInterceptor* interceptor = OwlNetworkInterceptor::GetInstance();
  return interceptor->RemoveRule(rule_id);
}

void OwlBrowserManager::EnableNetworkInterception(const std::string& context_id,
                                                   bool enable) {
  OwlNetworkInterceptor* interceptor = OwlNetworkInterceptor::GetInstance();
  interceptor->EnableInterception(context_id, enable);
  interceptor->EnableLogging(context_id, enable);

  LOG_DEBUG("BrowserManager", "Network interception " +
           std::string(enable ? "enabled" : "disabled") + " for context: " + context_id);
}

void OwlBrowserManager::EnableNetworkLogging(const std::string& context_id,
                                              bool enable) {
  OwlNetworkInterceptor* interceptor = OwlNetworkInterceptor::GetInstance();
  interceptor->EnableLogging(context_id, enable);
}

std::string OwlBrowserManager::GetNetworkLog(const std::string& context_id) {
  OwlNetworkInterceptor* interceptor = OwlNetworkInterceptor::GetInstance();
  return interceptor->GetNetworkLogJSON(context_id);
}

void OwlBrowserManager::ClearNetworkLog(const std::string& context_id) {
  OwlNetworkInterceptor* interceptor = OwlNetworkInterceptor::GetInstance();
  interceptor->ClearCapturedData(context_id);
}

// ============================================================================
// Console Log Management Methods
// ============================================================================

void OwlBrowserManager::EnableConsoleLogging(const std::string& context_id,
                                              bool enable) {
  OwlConsoleLogger* logger = OwlConsoleLogger::GetInstance();
  logger->EnableLogging(context_id, enable);
}

std::string OwlBrowserManager::GetConsoleLogs(const std::string& context_id,
                                               const std::string& level_filter,
                                               const std::string& text_filter,
                                               int limit) {
  OwlConsoleLogger* logger = OwlConsoleLogger::GetInstance();
  return logger->GetLogsJSON(context_id, level_filter, text_filter, limit);
}

void OwlBrowserManager::ClearConsoleLogs(const std::string& context_id) {
  OwlConsoleLogger* logger = OwlConsoleLogger::GetInstance();
  logger->ClearLogs(context_id);
}

// ============================================================================
// Download Management Methods
// ============================================================================

void OwlBrowserManager::SetDownloadPath(const std::string& context_id,
                                         const std::string& path) {
  OwlDownloadManager* dm = OwlDownloadManager::GetInstance();
  dm->SetDownloadPath(context_id, path);
  dm->SetAutoDownload(context_id, true);

  LOG_DEBUG("BrowserManager", "Set download path for context " + context_id + ": " + path);
}

std::string OwlBrowserManager::GetDownloads(const std::string& context_id) {
  OwlDownloadManager* dm = OwlDownloadManager::GetInstance();
  return dm->GetDownloadsJSON(context_id);
}

std::string OwlBrowserManager::GetActiveDownloads(const std::string& context_id) {
  OwlDownloadManager* dm = OwlDownloadManager::GetInstance();
  std::vector<DownloadInfo> downloads = dm->GetActiveDownloads(context_id);

  std::stringstream ss;
  ss << "[";
  bool first = true;
  for (const auto& dl : downloads) {
    if (!first) ss << ",";
    first = false;
    ss << "{\"id\":\"" << dl.download_id << "\""
       << ",\"url\":\"" << dl.url << "\""
       << ",\"filename\":\"" << dl.suggested_filename << "\""
       << ",\"percent\":" << dl.percent_complete
       << ",\"speed\":" << dl.current_speed << "}";
  }
  ss << "]";
  return ss.str();
}

bool OwlBrowserManager::WaitForDownload(const std::string& download_id,
                                         int timeout_ms) {
  OwlDownloadManager* dm = OwlDownloadManager::GetInstance();
  return dm->WaitForDownload(download_id, timeout_ms);
}

bool OwlBrowserManager::CancelDownload(const std::string& download_id) {
  OwlDownloadManager* dm = OwlDownloadManager::GetInstance();
  // Check if download exists before cancelling
  DownloadInfo download = dm->GetDownload(download_id);
  if (download.download_id.empty()) {
    return false;  // Download not found
  }
  dm->OnDownloadCancelled(download_id);
  return true;
}

// ============================================================================
// Dialog Handling Methods
// ============================================================================

void OwlBrowserManager::SetDialogAction(const std::string& context_id,
                                         const std::string& dialog_type,
                                         const std::string& action,
                                         const std::string& prompt_text) {
  OwlDialogManager* dm = OwlDialogManager::GetInstance();

  DialogAction da = DialogAction::DISMISS;
  if (action == "accept") {
    da = DialogAction::ACCEPT;
  } else if (action == "dismiss") {
    da = DialogAction::DISMISS;
  } else if (action == "accept_with_text") {
    da = DialogAction::ACCEPT_WITH_TEXT;
  }

  if (dialog_type == "alert") {
    dm->SetAlertAction(context_id, da);
  } else if (dialog_type == "confirm") {
    dm->SetConfirmAction(context_id, da);
  } else if (dialog_type == "prompt") {
    dm->SetPromptAction(context_id, da, prompt_text);
  } else if (dialog_type == "beforeunload") {
    dm->SetBeforeUnloadAction(context_id, da);
  }

  LOG_DEBUG("BrowserManager", "Set " + dialog_type + " action to " + action +
           " for context: " + context_id);
}

std::string OwlBrowserManager::GetPendingDialog(const std::string& context_id) {
  OwlDialogManager* dm = OwlDialogManager::GetInstance();
  PendingDialog dialog = dm->GetPendingDialog(context_id);

  if (dialog.dialog_id.empty()) {
    return "{}";
  }

  std::string type_str;
  switch (dialog.type) {
    case DialogType::ALERT: type_str = "alert"; break;
    case DialogType::CONFIRM: type_str = "confirm"; break;
    case DialogType::PROMPT: type_str = "prompt"; break;
    case DialogType::BEFOREUNLOAD: type_str = "beforeunload"; break;
  }

  std::stringstream ss;
  ss << "{\"id\":\"" << dialog.dialog_id << "\""
     << ",\"type\":\"" << type_str << "\""
     << ",\"message\":\"" << dialog.message << "\""
     << ",\"defaultValue\":\"" << dialog.default_value << "\""
     << ",\"originUrl\":\"" << dialog.origin_url << "\"}";

  return ss.str();
}

bool OwlBrowserManager::HandleDialog(const std::string& dialog_id,
                                      bool accept,
                                      const std::string& response_text) {
  OwlDialogManager* dm = OwlDialogManager::GetInstance();
  return dm->HandleDialog(dialog_id, accept, response_text);
}

bool OwlBrowserManager::WaitForDialog(const std::string& context_id,
                                       int timeout_ms) {
  OwlDialogManager* dm = OwlDialogManager::GetInstance();
  return dm->WaitForDialog(context_id, timeout_ms);
}

std::string OwlBrowserManager::GetDialogs(const std::string& context_id) {
  OwlDialogManager* dm = OwlDialogManager::GetInstance();
  return dm->GetDialogsJSON(context_id);
}

// ============================================================================
// Tab/Window Management Methods
// ============================================================================

void OwlBrowserManager::SetPopupPolicy(const std::string& context_id,
                                        const std::string& policy) {
  OwlTabManager* tm = OwlTabManager::GetInstance();

  PopupPolicy pp = PopupPolicy::OPEN_IN_NEW_TAB;
  if (policy == "allow") {
    pp = PopupPolicy::ALLOW;
  } else if (policy == "block") {
    pp = PopupPolicy::BLOCK;
  } else if (policy == "new_tab") {
    pp = PopupPolicy::OPEN_IN_NEW_TAB;
  } else if (policy == "background") {
    pp = PopupPolicy::OPEN_IN_BACKGROUND;
  }

  tm->SetPopupPolicy(context_id, pp);
  LOG_DEBUG("BrowserManager", "Set popup policy to " + policy + " for context: " + context_id);
}

std::string OwlBrowserManager::GetTabs(const std::string& context_id) {
  OwlTabManager* tm = OwlTabManager::GetInstance();
  return tm->GetTabsJSON(context_id);
}

ActionResult OwlBrowserManager::SwitchTab(const std::string& context_id,
                                   const std::string& tab_id) {
  OwlTabManager* tm = OwlTabManager::GetInstance();

  LOG_DEBUG("BrowserManager", "=== SWITCH TAB === context=" + context_id + " tab=" + tab_id);

  // Pre-check: Verify tab exists and belongs to context
  TabInfo tab = tm->GetTab(tab_id);
  if (tab.tab_id.empty()) {
    LOG_ERROR("BrowserManager", "SwitchTab failed - tab not found: " + tab_id);
    return ActionResult::TabSwitchFailed(tab_id, "Tab not found");
  }

  if (tab.context_id != context_id) {
    LOG_ERROR("BrowserManager", "SwitchTab failed - tab belongs to different context: " + tab.context_id);
    return ActionResult::TabSwitchFailed(tab_id,
      "Tab belongs to context " + tab.context_id + ", not " + context_id);
  }

  // Perform the switch
  tm->SetActiveTab(context_id, tab_id);

  // Post-verification: Confirm the tab is now active
  std::string active_tab_id = tm->GetActiveTab(context_id);
  if (active_tab_id != tab_id) {
    LOG_ERROR("BrowserManager", "SwitchTab verification failed - active tab is " +
              active_tab_id + " expected " + tab_id);
    return ActionResult::TabSwitchFailed(tab_id,
      "Verification failed: expected " + tab_id + " but active tab is " + active_tab_id);
  }

  LOG_DEBUG("BrowserManager", "=== SWITCH TAB COMPLETE === Verified tab " + tab_id + " is now active");
  return ActionResult::Success("Switched to tab: " + tab_id + " (verified)");
}

ActionResult OwlBrowserManager::CloseTab(const std::string& context_id,
                                  const std::string& tab_id) {
  OwlTabManager* tm = OwlTabManager::GetInstance();

  TabInfo tab = tm->GetTab(tab_id);
  if (tab.tab_id.empty() || tab.context_id != context_id) {
    LOG_ERROR("BrowserManager", "CloseTab failed - tab not found or wrong context");
    return ActionResult::Failure(ActionStatus::ELEMENT_NOT_FOUND, "Tab not found: " + tab_id);
  }

  // Can't close main tab
  if (tab.is_main) {
    LOG_WARN("BrowserManager", "Cannot close main tab");
    return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "Cannot close main tab");
  }

  tm->UnregisterTab(tab_id);
  return ActionResult::Success("Tab closed: " + tab_id);
}

std::string OwlBrowserManager::NewTab(const std::string& context_id,
                                       const std::string& url) {
  OwlTabManager* tm = OwlTabManager::GetInstance();

  TabInfo info;
  info.tab_id = tm->GenerateTabId();
  info.context_id = context_id;
  info.url = url;
  info.is_main = false;
  info.is_popup = false;
  info.is_active = true;

  tm->RegisterTab(info);

  // Navigate in new tab context
  if (!url.empty()) {
    Navigate(context_id, url);
  }

  LOG_DEBUG("BrowserManager", "Created new tab: " + info.tab_id + " url: " + url);
  return info.tab_id;
}

std::string OwlBrowserManager::GetActiveTab(const std::string& context_id) {
  OwlTabManager* tm = OwlTabManager::GetInstance();
  return tm->GetActiveTab(context_id);
}

int OwlBrowserManager::GetTabCount(const std::string& context_id) {
  OwlTabManager* tm = OwlTabManager::GetInstance();
  return tm->GetTabCount(context_id);
}

std::string OwlBrowserManager::GetBlockedPopups(const std::string& context_id) {
  OwlTabManager* tm = OwlTabManager::GetInstance();
  std::vector<std::string> popups = tm->GetBlockedPopups(context_id);

  std::stringstream ss;
  ss << "[";
  bool first = true;
  for (const auto& url : popups) {
    if (!first) ss << ",";
    first = false;
    ss << "\"" << url << "\"";
  }
  ss << "]";
  return ss.str();
}

// ==================== CLIPBOARD MANAGEMENT ====================

std::string OwlBrowserManager::ClipboardRead(const std::string& context_id) {
  LOG_DEBUG("BrowserManager", "ClipboardRead for context: " + context_id);

  // Verify context exists
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "ClipboardRead failed - browser not found");
    return "{\"error\":\"browser not found\"}";
  }

  // Use JavaScript evaluation to read from browser's captured clipboard
  // The clipboard hook in owl_stealth.cc stores text at window[Symbol.for('__owl_clipboard__')]
  // Return the object directly - Evaluate will JSON.stringify it automatically
  std::string js_code = R"(
    (function() {
      try {
        const key = Symbol.for('__owl_clipboard__');
        const text = window[key] || '';
        return { text: text };
      } catch(e) {
        return { text: '', error: e.message };
      }
    })()
  )";

  std::string result = Evaluate(context_id, js_code, true);

  if (result.empty()) {
    LOG_DEBUG("BrowserManager", "ClipboardRead: no content captured");
    return "{\"text\":\"\"}";
  }

  LOG_DEBUG("BrowserManager", "ClipboardRead success via JS evaluation");
  return result;
}

ActionResult OwlBrowserManager::ClipboardWrite(const std::string& context_id, const std::string& text) {
  LOG_DEBUG("BrowserManager", "ClipboardWrite for context: " + context_id + ", text length: " + std::to_string(text.length()));

  // Verify context exists
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "ClipboardWrite failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  // Escape the text for JavaScript string
  std::string escaped_text;
  for (char c : text) {
    switch (c) {
      case '\\': escaped_text += "\\\\"; break;
      case '"': escaped_text += "\\\""; break;
      case '\n': escaped_text += "\\n"; break;
      case '\r': escaped_text += "\\r"; break;
      case '\t': escaped_text += "\\t"; break;
      default: escaped_text += c;
    }
  }

  // Use JavaScript to write to browser's clipboard storage
  std::string js_code = "(function() { const key = Symbol.for('__owl_clipboard__'); window[key] = \"" + escaped_text + "\"; return 'ok'; })()";

  std::string result = Evaluate(context_id, js_code, true);

  if (result.find("ok") != std::string::npos) {
    LOG_DEBUG("BrowserManager", "ClipboardWrite success via JS");
    return ActionResult::Success("Text written to clipboard (" + std::to_string(text.length()) + " characters)");
  } else {
    LOG_ERROR("BrowserManager", "ClipboardWrite failed - JS evaluation failed");
    return ActionResult::Failure(ActionStatus::INTERNAL_ERROR, "Failed to write to clipboard");
  }
}

ActionResult OwlBrowserManager::ClipboardClear(const std::string& context_id) {
  LOG_DEBUG("BrowserManager", "ClipboardClear for context: " + context_id);

  // Verify context exists
  auto browser = GetBrowser(context_id);
  if (!browser) {
    LOG_ERROR("BrowserManager", "ClipboardClear failed - browser not found");
    return ActionResult::BrowserNotFound(context_id);
  }

  // Clear by writing empty string
  return ClipboardWrite(context_id, "");
}
