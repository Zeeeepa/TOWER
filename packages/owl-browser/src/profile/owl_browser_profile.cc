#include "owl_browser_profile.h"
#include "owl_virtual_machine.h"
#include "stealth/owl_fingerprint_generator.h"
#include "logger.h"
#include <fstream>
#include <sstream>
#include <random>
#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <sys/stat.h>
#include <dirent.h>

#if defined(OS_WIN)
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/types.h>
#endif

// Static instance
OwlBrowserProfileManager* OwlBrowserProfileManager::instance_ = nullptr;
std::mutex OwlBrowserProfileManager::instance_mutex_;

// ============================================================================
// BrowserFingerprint Implementation
// ============================================================================

BrowserFingerprint::BrowserFingerprint() {
  // Set sensible defaults that match a typical Windows Chrome user
  user_agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36";
  platform = "Win32";
  vendor = "Google Inc.";
  languages = {"en-US", "en"};
  hardware_concurrency = 8;
  device_memory = 8;
  max_touch_points = 0;
  canvas_noise_seed = 0.0003;  // Middle of range
  gpu_profile_index = 0;
  webgl_vendor = "Google Inc. (NVIDIA)";
  webgl_renderer = "ANGLE (NVIDIA, NVIDIA GeForce GTX 1660 Ti Direct3D11 vs_5_0 ps_5_0, D3D11)";
  screen_width = 1920;
  screen_height = 1080;
  color_depth = 24;
  pixel_ratio = 1;
  timezone = "America/New_York";
  locale = "en-US";
  audio_noise_seed = 0.0;
  has_pdf_plugin = true;
  has_chrome_pdf = true;
}

BrowserFingerprint BrowserFingerprint::GenerateRandom() {
  BrowserFingerprint fp;

  std::random_device rd;
  std::mt19937_64 gen(rd());

  // NOTE: We do NOT pre-select vm_id here!
  // VM selection must happen in CreateContext AFTER browser_id is known.
  // This ensures both browser process and renderer process use the SAME
  // browser_id-based seed to select the same VM.
  //
  // If we pre-selected here with a random seed, the renderer (different process)
  // wouldn't know this vm_id and would use browser_id seed, picking a DIFFERENT VM.
  // This caused "Different operating systems" detection on browserscan.net.
  //
  // The vm_id will be set in CreateContext using: browser_id * 0x9E3779B97F4A7C15ULL
  // which matches the fallback seed used in InjectHardwareSimulation (renderer).

  // Set default values - these will be overwritten when VM is selected in CreateContext
  fp.vm_id = "";  // IMPORTANT: Leave empty - will be set by CreateContext
  fp.user_agent = owl::VirtualMachineDB::Instance().GetDefaultUserAgent();
  fp.platform = "Win32";
  fp.vendor = "Google Inc.";
  fp.languages = {"en-US", "en"};
  fp.hardware_concurrency = 8;
  fp.device_memory = 8;
  fp.webgl_vendor = "Google Inc. (NVIDIA)";
  fp.webgl_renderer = "ANGLE (NVIDIA, NVIDIA GeForce GTX 1660 Ti Direct3D11 vs_5_0 ps_5_0, D3D11)";
  fp.screen_width = 1920;
  fp.screen_height = 1080;
  fp.timezone = "America/New_York";
  fp.locale = "en-US";

  // Generate unique fingerprint seeds using FingerprintGenerator
  // Use a temporary unique context_id for profile creation
  std::string temp_context_id = "profile_" + std::to_string(gen());
  owl::FingerprintSeeds seeds = owl::OwlFingerprintGenerator::Instance().GetSeeds(temp_context_id);

  // Copy all seeds to BrowserFingerprint
  fp.canvas_seed = seeds.canvas_seed;
  fp.webgl_seed = seeds.webgl_seed;
  fp.audio_seed = seeds.audio_seed;
  fp.fonts_seed = seeds.fonts_seed;
  fp.client_rects_seed = seeds.client_rects_seed;
  fp.navigator_seed = seeds.navigator_seed;
  fp.screen_seed = seeds.screen_seed;
  fp.audio_fingerprint = seeds.audio_fingerprint;

  // Copy all MD5-style hashes
  fp.canvas_geometry_hash = seeds.canvas_geometry_hash;
  fp.canvas_text_hash = seeds.canvas_text_hash;
  fp.webgl_params_hash = seeds.webgl_params_hash;
  fp.webgl_extensions_hash = seeds.webgl_extensions_hash;
  fp.webgl_context_hash = seeds.webgl_context_hash;
  fp.webgl_ext_params_hash = seeds.webgl_ext_params_hash;
  fp.shader_precisions_hash = seeds.shader_precisions_hash;
  fp.fonts_hash = seeds.fonts_hash;
  fp.plugins_hash = seeds.plugins_hash;

  // Legacy field (for backwards compatibility)
  fp.canvas_hash_seed = seeds.canvas_seed;

  // Clear the temporary context from generator (we'll re-register with real context_id later)
  owl::OwlFingerprintGenerator::Instance().ClearContext(temp_context_id);

  // Generate noise seeds (these are separate from the main seeds)
  std::uniform_real_distribution<> noise_dist(0.0001, 0.0005);
  fp.canvas_noise_seed = noise_dist(gen);
  fp.audio_noise_seed = noise_dist(gen);

  LOG_DEBUG("ProfileManager", "Generated random fingerprint with unique seeds (vm_id will be set by CreateContext)");

  return fp;
}

std::string BrowserFingerprint::ToJSON() const {
  std::ostringstream json;
  json << std::fixed << std::setprecision(14);

  json << "{\n";
  json << "  \"vm_id\": \"" << OwlBrowserProfileManager::EscapeJSON(vm_id) << "\",\n";

  // Core fingerprint seeds (Seed API integration)
  json << "  \"seeds\": {\n";
  json << "    \"canvas\": " << canvas_seed << ",\n";
  json << "    \"webgl\": " << webgl_seed << ",\n";
  json << "    \"audio\": " << audio_seed << ",\n";
  json << "    \"fonts\": " << fonts_seed << ",\n";
  json << "    \"client_rects\": " << client_rects_seed << ",\n";
  json << "    \"navigator\": " << navigator_seed << ",\n";
  json << "    \"screen\": " << screen_seed << "\n";
  json << "  },\n";

  // Audio fingerprint value (realistic 124.x)
  json << "  \"audio_fingerprint\": " << audio_fingerprint << ",\n";

  // MD5-style hashes for fingerprint.com compatibility
  json << "  \"hashes\": {\n";
  json << "    \"canvas_geometry\": \"" << OwlBrowserProfileManager::EscapeJSON(canvas_geometry_hash) << "\",\n";
  json << "    \"canvas_text\": \"" << OwlBrowserProfileManager::EscapeJSON(canvas_text_hash) << "\",\n";
  json << "    \"webgl_params\": \"" << OwlBrowserProfileManager::EscapeJSON(webgl_params_hash) << "\",\n";
  json << "    \"webgl_extensions\": \"" << OwlBrowserProfileManager::EscapeJSON(webgl_extensions_hash) << "\",\n";
  json << "    \"webgl_context\": \"" << OwlBrowserProfileManager::EscapeJSON(webgl_context_hash) << "\",\n";
  json << "    \"webgl_ext_params\": \"" << OwlBrowserProfileManager::EscapeJSON(webgl_ext_params_hash) << "\",\n";
  json << "    \"shader_precisions\": \"" << OwlBrowserProfileManager::EscapeJSON(shader_precisions_hash) << "\",\n";
  json << "    \"fonts\": \"" << OwlBrowserProfileManager::EscapeJSON(fonts_hash) << "\",\n";
  json << "    \"plugins\": \"" << OwlBrowserProfileManager::EscapeJSON(plugins_hash) << "\"\n";
  json << "  },\n";

  // Legacy fields (backwards compatibility)
  json << "  \"canvas_hash_seed\": " << canvas_hash_seed << ",\n";
  json << "  \"user_agent\": \"" << OwlBrowserProfileManager::EscapeJSON(user_agent) << "\",\n";
  json << "  \"platform\": \"" << OwlBrowserProfileManager::EscapeJSON(platform) << "\",\n";
  json << "  \"vendor\": \"" << OwlBrowserProfileManager::EscapeJSON(vendor) << "\",\n";

  json << "  \"languages\": [";
  for (size_t i = 0; i < languages.size(); i++) {
    if (i > 0) json << ", ";
    json << "\"" << OwlBrowserProfileManager::EscapeJSON(languages[i]) << "\"";
  }
  json << "],\n";

  json << "  \"hardware_concurrency\": " << hardware_concurrency << ",\n";
  json << "  \"device_memory\": " << device_memory << ",\n";
  json << "  \"max_touch_points\": " << max_touch_points << ",\n";
  json << "  \"canvas_noise_seed\": " << canvas_noise_seed << ",\n";
  json << "  \"gpu_profile_index\": " << gpu_profile_index << ",\n";
  json << "  \"webgl_vendor\": \"" << OwlBrowserProfileManager::EscapeJSON(webgl_vendor) << "\",\n";
  json << "  \"webgl_renderer\": \"" << OwlBrowserProfileManager::EscapeJSON(webgl_renderer) << "\",\n";
  json << "  \"screen_width\": " << screen_width << ",\n";
  json << "  \"screen_height\": " << screen_height << ",\n";
  json << "  \"color_depth\": " << color_depth << ",\n";
  json << "  \"pixel_ratio\": " << pixel_ratio << ",\n";
  json << "  \"timezone\": \"" << OwlBrowserProfileManager::EscapeJSON(timezone) << "\",\n";
  json << "  \"locale\": \"" << OwlBrowserProfileManager::EscapeJSON(locale) << "\",\n";
  json << "  \"audio_noise_seed\": " << audio_noise_seed << ",\n";

  json << "  \"installed_fonts\": [";
  for (size_t i = 0; i < installed_fonts.size(); i++) {
    if (i > 0) json << ", ";
    json << "\"" << OwlBrowserProfileManager::EscapeJSON(installed_fonts[i]) << "\"";
  }
  json << "],\n";

  json << "  \"has_pdf_plugin\": " << (has_pdf_plugin ? "true" : "false") << ",\n";
  json << "  \"has_chrome_pdf\": " << (has_chrome_pdf ? "true" : "false") << "\n";
  json << "}";

  return json.str();
}

// Helper function to extract a string value from JSON (simple parser)
static std::string ExtractJSONString(const std::string& json, const std::string& key) {
  std::string search = "\"" + key + "\"";
  size_t pos = json.find(search);
  if (pos == std::string::npos) return "";

  pos = json.find(":", pos);
  if (pos == std::string::npos) return "";

  pos = json.find("\"", pos + 1);
  if (pos == std::string::npos) return "";

  size_t end_pos = pos + 1;
  while (end_pos < json.size()) {
    if (json[end_pos] == '"' && json[end_pos - 1] != '\\') break;
    end_pos++;
  }

  return OwlBrowserProfileManager::UnescapeJSON(json.substr(pos + 1, end_pos - pos - 1));
}

// Helper function to extract an integer value from JSON
static int ExtractJSONInt(const std::string& json, const std::string& key, int default_val = 0) {
  std::string search = "\"" + key + "\"";
  size_t pos = json.find(search);
  if (pos == std::string::npos) return default_val;

  pos = json.find(":", pos);
  if (pos == std::string::npos) return default_val;

  pos = json.find_first_of("-0123456789", pos + 1);
  if (pos == std::string::npos) return default_val;

  size_t end_pos = json.find_first_not_of("-0123456789", pos);
  if (end_pos == std::string::npos) end_pos = json.size();

  std::string num_str = json.substr(pos, end_pos - pos);
  if (num_str.empty()) return default_val;

  // Manual parsing to avoid exceptions
  int result = 0;
  int sign = 1;
  size_t i = 0;
  if (num_str[0] == '-') {
    sign = -1;
    i = 1;
  }
  for (; i < num_str.size(); i++) {
    if (num_str[i] < '0' || num_str[i] > '9') return default_val;
    result = result * 10 + (num_str[i] - '0');
  }
  return result * sign;
}

// Helper function to extract a double value from JSON
static double ExtractJSONDouble(const std::string& json, const std::string& key, double default_val = 0.0) {
  std::string search = "\"" + key + "\"";
  size_t pos = json.find(search);
  if (pos == std::string::npos) return default_val;

  pos = json.find(":", pos);
  if (pos == std::string::npos) return default_val;

  pos = json.find_first_of("-0123456789.", pos + 1);
  if (pos == std::string::npos) return default_val;

  size_t end_pos = json.find_first_not_of("-0123456789.eE+-", pos);
  if (end_pos == std::string::npos) end_pos = json.size();

  std::string num_str = json.substr(pos, end_pos - pos);
  if (num_str.empty()) return default_val;

  // Use atof which doesn't throw (returns 0.0 on failure)
  double result = std::atof(num_str.c_str());
  // If atof returns 0 but the string isn't "0", return default
  if (result == 0.0 && num_str != "0" && num_str != "0.0" && num_str[0] != '0') {
    return default_val;
  }
  return result;
}

// Helper function to extract a uint64_t value from JSON
static uint64_t ExtractJSONUint64(const std::string& json, const std::string& key, uint64_t default_val = 0) {
  std::string search = "\"" + key + "\"";
  size_t pos = json.find(search);
  if (pos == std::string::npos) return default_val;

  pos = json.find(":", pos);
  if (pos == std::string::npos) return default_val;

  pos = json.find_first_of("0123456789", pos + 1);
  if (pos == std::string::npos) return default_val;

  size_t end_pos = json.find_first_not_of("0123456789", pos);
  if (end_pos == std::string::npos) end_pos = json.size();

  std::string num_str = json.substr(pos, end_pos - pos);
  if (num_str.empty()) return default_val;

  return std::stoull(num_str);
}

// Helper function to extract a boolean value from JSON
static bool ExtractJSONBool(const std::string& json, const std::string& key, bool default_val = false) {
  std::string search = "\"" + key + "\"";
  size_t pos = json.find(search);
  if (pos == std::string::npos) return default_val;

  pos = json.find(":", pos);
  if (pos == std::string::npos) return default_val;

  pos = json.find_first_not_of(" \t\n\r", pos + 1);
  if (pos == std::string::npos) return default_val;

  return json.substr(pos, 4) == "true";
}

// Helper function to extract a string array from JSON
static std::vector<std::string> ExtractJSONStringArray(const std::string& json, const std::string& key) {
  std::vector<std::string> result;

  std::string search = "\"" + key + "\"";
  size_t pos = json.find(search);
  if (pos == std::string::npos) return result;

  pos = json.find("[", pos);
  if (pos == std::string::npos) return result;

  size_t end_pos = json.find("]", pos);
  if (end_pos == std::string::npos) return result;

  std::string array_content = json.substr(pos + 1, end_pos - pos - 1);

  // Parse each string in the array
  size_t start = 0;
  while ((start = array_content.find("\"", start)) != std::string::npos) {
    size_t end = start + 1;
    while (end < array_content.size()) {
      if (array_content[end] == '"' && array_content[end - 1] != '\\') break;
      end++;
    }
    if (end <= array_content.size()) {
      result.push_back(OwlBrowserProfileManager::UnescapeJSON(array_content.substr(start + 1, end - start - 1)));
    }
    start = end + 1;
  }

  return result;
}

// Helper function to extract a nested JSON object as string
static std::string ExtractJSONObject(const std::string& json, const std::string& key) {
  std::string search = "\"" + key + "\"";
  size_t pos = json.find(search);
  if (pos == std::string::npos) return "";

  pos = json.find("{", pos);
  if (pos == std::string::npos) return "";

  // Find matching closing brace
  int brace_count = 1;
  size_t end_pos = pos + 1;
  while (end_pos < json.size() && brace_count > 0) {
    if (json[end_pos] == '{') brace_count++;
    else if (json[end_pos] == '}') brace_count--;
    end_pos++;
  }

  return json.substr(pos, end_pos - pos);
}

BrowserFingerprint BrowserFingerprint::FromJSON(const std::string& json) {
  BrowserFingerprint fp;

  // VirtualMachine ID - critical for consistent fingerprinting
  fp.vm_id = ExtractJSONString(json, "vm_id");

  // Parse core fingerprint seeds from nested "seeds" object
  std::string seeds_json = ExtractJSONObject(json, "seeds");
  if (!seeds_json.empty()) {
    fp.canvas_seed = ExtractJSONUint64(seeds_json, "canvas", 0);
    fp.webgl_seed = ExtractJSONUint64(seeds_json, "webgl", 0);
    fp.audio_seed = ExtractJSONUint64(seeds_json, "audio", 0);
    fp.fonts_seed = ExtractJSONUint64(seeds_json, "fonts", 0);
    fp.client_rects_seed = ExtractJSONUint64(seeds_json, "client_rects", 0);
    fp.navigator_seed = ExtractJSONUint64(seeds_json, "navigator", 0);
    fp.screen_seed = ExtractJSONUint64(seeds_json, "screen", 0);
  }

  // Audio fingerprint value (realistic 124.x)
  fp.audio_fingerprint = ExtractJSONDouble(json, "audio_fingerprint", 124.04344968475198);

  // Parse MD5-style hashes from nested "hashes" object
  std::string hashes_json = ExtractJSONObject(json, "hashes");
  if (!hashes_json.empty()) {
    fp.canvas_geometry_hash = ExtractJSONString(hashes_json, "canvas_geometry");
    fp.canvas_text_hash = ExtractJSONString(hashes_json, "canvas_text");
    fp.webgl_params_hash = ExtractJSONString(hashes_json, "webgl_params");
    fp.webgl_extensions_hash = ExtractJSONString(hashes_json, "webgl_extensions");
    fp.webgl_context_hash = ExtractJSONString(hashes_json, "webgl_context");
    fp.webgl_ext_params_hash = ExtractJSONString(hashes_json, "webgl_ext_params");
    fp.shader_precisions_hash = ExtractJSONString(hashes_json, "shader_precisions");
    fp.fonts_hash = ExtractJSONString(hashes_json, "fonts");
    fp.plugins_hash = ExtractJSONString(hashes_json, "plugins");
  }

  // Generate fallback hashes for any empty hash fields
  // This ensures old profiles without hashes get unique ones
  auto generateFallbackHash = []() -> std::string {
    std::random_device rd;
    std::seed_seq seed_seq{rd(), rd(), rd(), rd()};
    std::mt19937_64 gen(seed_seq);
    uint64_t high = gen();
    uint64_t low = gen();
    // Apply mixing for better distribution
    high ^= high >> 33;
    high *= 0xff51afd7ed558ccdULL;
    low ^= low >> 33;
    low *= 0xc4ceb9fe1a85ec53ULL;
    std::ostringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << high
       << std::setw(16) << std::setfill('0') << low;
    return ss.str();
  };

  // Generate unique hashes for any empty hash fields (backwards compatibility)
  if (fp.canvas_geometry_hash.empty()) fp.canvas_geometry_hash = generateFallbackHash();
  if (fp.canvas_text_hash.empty()) fp.canvas_text_hash = generateFallbackHash();
  if (fp.webgl_params_hash.empty()) fp.webgl_params_hash = generateFallbackHash();
  if (fp.webgl_extensions_hash.empty()) fp.webgl_extensions_hash = generateFallbackHash();
  if (fp.webgl_context_hash.empty()) fp.webgl_context_hash = generateFallbackHash();
  if (fp.webgl_ext_params_hash.empty()) fp.webgl_ext_params_hash = generateFallbackHash();
  if (fp.shader_precisions_hash.empty()) fp.shader_precisions_hash = generateFallbackHash();
  if (fp.fonts_hash.empty()) fp.fonts_hash = generateFallbackHash();
  if (fp.plugins_hash.empty()) fp.plugins_hash = generateFallbackHash();

  // Legacy canvas hash seed - for backwards compatibility
  fp.canvas_hash_seed = ExtractJSONUint64(json, "canvas_hash_seed", 0);

  // If no new seeds but legacy seed exists, use legacy for canvas_seed
  if (fp.canvas_seed == 0 && fp.canvas_hash_seed != 0) {
    fp.canvas_seed = fp.canvas_hash_seed;
  }

  // If no seeds at all, generate random (backwards compatibility)
  if (fp.canvas_seed == 0) {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    fp.canvas_seed = gen();
    fp.canvas_hash_seed = fp.canvas_seed;  // Keep legacy in sync
  }

  fp.user_agent = ExtractJSONString(json, "user_agent");
  if (fp.user_agent.empty()) {
    // Use default if not found
    fp.user_agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36";
  }

  fp.platform = ExtractJSONString(json, "platform");
  if (fp.platform.empty()) fp.platform = "Win32";

  fp.vendor = ExtractJSONString(json, "vendor");
  if (fp.vendor.empty()) fp.vendor = "Google Inc.";

  fp.languages = ExtractJSONStringArray(json, "languages");
  if (fp.languages.empty()) {
    fp.languages = {"en-US", "en"};
  }

  fp.hardware_concurrency = ExtractJSONInt(json, "hardware_concurrency", 8);
  fp.device_memory = ExtractJSONInt(json, "device_memory", 8);
  fp.max_touch_points = ExtractJSONInt(json, "max_touch_points", 0);
  fp.canvas_noise_seed = ExtractJSONDouble(json, "canvas_noise_seed", 0.0003);
  fp.gpu_profile_index = ExtractJSONInt(json, "gpu_profile_index", 0);

  fp.webgl_vendor = ExtractJSONString(json, "webgl_vendor");
  if (fp.webgl_vendor.empty()) fp.webgl_vendor = "Google Inc. (NVIDIA)";

  fp.webgl_renderer = ExtractJSONString(json, "webgl_renderer");
  if (fp.webgl_renderer.empty()) {
    fp.webgl_renderer = "ANGLE (NVIDIA, NVIDIA GeForce GTX 1660 Ti Direct3D11 vs_5_0 ps_5_0, D3D11)";
  }

  fp.screen_width = ExtractJSONInt(json, "screen_width", 1920);
  fp.screen_height = ExtractJSONInt(json, "screen_height", 1080);
  fp.color_depth = ExtractJSONInt(json, "color_depth", 24);
  fp.pixel_ratio = ExtractJSONInt(json, "pixel_ratio", 1);

  fp.timezone = ExtractJSONString(json, "timezone");
  if (fp.timezone.empty()) fp.timezone = "America/New_York";

  fp.locale = ExtractJSONString(json, "locale");
  if (fp.locale.empty()) fp.locale = "en-US";

  fp.audio_noise_seed = ExtractJSONDouble(json, "audio_noise_seed", 0.0);
  fp.installed_fonts = ExtractJSONStringArray(json, "installed_fonts");
  fp.has_pdf_plugin = ExtractJSONBool(json, "has_pdf_plugin", true);
  fp.has_chrome_pdf = ExtractJSONBool(json, "has_chrome_pdf", true);

  return fp;
}

// ============================================================================
// BrowserProfile Implementation
// ============================================================================

BrowserProfile::BrowserProfile() {
  profile_id = "";
  profile_name = "";
  created_at = "";
  modified_at = "";
  version = 1;
  has_llm_config = false;
  has_proxy_config = false;
  auto_save_cookies = true;
  persist_local_storage = true;
}

BrowserProfile::BrowserProfile(const std::string& id) : BrowserProfile() {
  profile_id = id;
}

void BrowserProfile::Touch() {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::ostringstream ss;
  ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
  modified_at = ss.str();
}

bool BrowserProfile::IsValid() const {
  return !profile_id.empty();
}

std::string BrowserProfile::ToJSON() const {
  std::ostringstream json;

  json << "{\n";
  json << "  \"profile_id\": \"" << OwlBrowserProfileManager::EscapeJSON(profile_id) << "\",\n";
  json << "  \"profile_name\": \"" << OwlBrowserProfileManager::EscapeJSON(profile_name) << "\",\n";
  json << "  \"created_at\": \"" << OwlBrowserProfileManager::EscapeJSON(created_at) << "\",\n";
  json << "  \"modified_at\": \"" << OwlBrowserProfileManager::EscapeJSON(modified_at) << "\",\n";
  json << "  \"version\": " << version << ",\n";

  // Fingerprint
  json << "  \"fingerprint\": " << fingerprint.ToJSON() << ",\n";

  // Cookies
  json << "  \"cookies\": " << OwlCookieManager::CookiesToJson(cookies) << ",\n";

  // LLM config (optional)
  json << "  \"has_llm_config\": " << (has_llm_config ? "true" : "false") << ",\n";
  if (has_llm_config) {
    json << "  \"llm_config\": {\n";
    json << "    \"enabled\": " << (llm_config.enabled ? "true" : "false") << ",\n";
    json << "    \"use_builtin\": " << (llm_config.use_builtin ? "true" : "false") << ",\n";
    json << "    \"external_endpoint\": \"" << OwlBrowserProfileManager::EscapeJSON(llm_config.external_endpoint) << "\",\n";
    json << "    \"external_model\": \"" << OwlBrowserProfileManager::EscapeJSON(llm_config.external_model) << "\",\n";
    json << "    \"external_api_key\": \"" << OwlBrowserProfileManager::EscapeJSON(llm_config.external_api_key) << "\",\n";
    json << "    \"is_third_party\": " << (llm_config.is_third_party ? "true" : "false") << "\n";
    json << "  },\n";
  }

  // Proxy config (optional)
  json << "  \"has_proxy_config\": " << (has_proxy_config ? "true" : "false") << ",\n";
  if (has_proxy_config) {
    json << "  \"proxy_config\": {\n";
    json << "    \"type\": \"" << OwlProxyManager::ProxyTypeToString(proxy_config.type) << "\",\n";
    json << "    \"host\": \"" << OwlBrowserProfileManager::EscapeJSON(proxy_config.host) << "\",\n";
    json << "    \"port\": " << proxy_config.port << ",\n";
    json << "    \"username\": \"" << OwlBrowserProfileManager::EscapeJSON(proxy_config.username) << "\",\n";
    json << "    \"password\": \"" << OwlBrowserProfileManager::EscapeJSON(proxy_config.password) << "\",\n";
    json << "    \"enabled\": " << (proxy_config.enabled ? "true" : "false") << ",\n";
    json << "    \"stealth_mode\": " << (proxy_config.stealth_mode ? "true" : "false") << ",\n";
    json << "    \"block_webrtc\": " << (proxy_config.block_webrtc ? "true" : "false") << ",\n";
    json << "    \"spoof_timezone\": " << (proxy_config.spoof_timezone ? "true" : "false") << ",\n";
    json << "    \"timezone_override\": \"" << OwlBrowserProfileManager::EscapeJSON(proxy_config.timezone_override) << "\"\n";
    json << "  },\n";
  }

  // Local storage (simplified - just empty for now, will be expanded)
  json << "  \"local_storage\": {},\n";
  json << "  \"session_storage\": {},\n";

  // Settings
  json << "  \"auto_save_cookies\": " << (auto_save_cookies ? "true" : "false") << ",\n";
  json << "  \"persist_local_storage\": " << (persist_local_storage ? "true" : "false") << "\n";

  json << "}";

  return json.str();
}

BrowserProfile BrowserProfile::FromJSON(const std::string& json) {
  BrowserProfile profile;

  profile.profile_id = ExtractJSONString(json, "profile_id");
  profile.profile_name = ExtractJSONString(json, "profile_name");
  profile.created_at = ExtractJSONString(json, "created_at");
  profile.modified_at = ExtractJSONString(json, "modified_at");
  profile.version = ExtractJSONInt(json, "version", 1);

  // Parse fingerprint (find the fingerprint object)
  size_t fp_start = json.find("\"fingerprint\"");
  if (fp_start != std::string::npos) {
    fp_start = json.find("{", fp_start);
    if (fp_start != std::string::npos) {
      int brace_count = 1;
      size_t fp_end = fp_start + 1;
      while (fp_end < json.size() && brace_count > 0) {
        if (json[fp_end] == '{') brace_count++;
        else if (json[fp_end] == '}') brace_count--;
        fp_end++;
      }
      std::string fp_json = json.substr(fp_start, fp_end - fp_start);
      profile.fingerprint = BrowserFingerprint::FromJSON(fp_json);
    }
  }

  // Parse cookies (find the cookies array)
  size_t cookies_start = json.find("\"cookies\"");
  if (cookies_start != std::string::npos) {
    cookies_start = json.find("[", cookies_start);
    if (cookies_start != std::string::npos) {
      int bracket_count = 1;
      size_t cookies_end = cookies_start + 1;
      while (cookies_end < json.size() && bracket_count > 0) {
        if (json[cookies_end] == '[') bracket_count++;
        else if (json[cookies_end] == ']') bracket_count--;
        cookies_end++;
      }
      std::string cookies_json = json.substr(cookies_start, cookies_end - cookies_start);
      profile.cookies = OwlCookieManager::ParseCookiesJson(cookies_json);
    }
  }

  // Parse LLM config
  profile.has_llm_config = ExtractJSONBool(json, "has_llm_config", false);
  if (profile.has_llm_config) {
    size_t llm_start = json.find("\"llm_config\"");
    if (llm_start != std::string::npos) {
      llm_start = json.find("{", llm_start);
      if (llm_start != std::string::npos) {
        int brace_count = 1;
        size_t llm_end = llm_start + 1;
        while (llm_end < json.size() && brace_count > 0) {
          if (json[llm_end] == '{') brace_count++;
          else if (json[llm_end] == '}') brace_count--;
          llm_end++;
        }
        std::string llm_json = json.substr(llm_start, llm_end - llm_start);
        profile.llm_config.enabled = ExtractJSONBool(llm_json, "enabled", true);
        profile.llm_config.use_builtin = ExtractJSONBool(llm_json, "use_builtin", true);
        profile.llm_config.external_endpoint = ExtractJSONString(llm_json, "external_endpoint");
        profile.llm_config.external_model = ExtractJSONString(llm_json, "external_model");
        profile.llm_config.external_api_key = ExtractJSONString(llm_json, "external_api_key");
        profile.llm_config.is_third_party = ExtractJSONBool(llm_json, "is_third_party", false);
      }
    }
  }

  // Parse proxy config
  profile.has_proxy_config = ExtractJSONBool(json, "has_proxy_config", false);
  if (profile.has_proxy_config) {
    size_t proxy_start = json.find("\"proxy_config\"");
    if (proxy_start != std::string::npos) {
      proxy_start = json.find("{", proxy_start);
      if (proxy_start != std::string::npos) {
        int brace_count = 1;
        size_t proxy_end = proxy_start + 1;
        while (proxy_end < json.size() && brace_count > 0) {
          if (json[proxy_end] == '{') brace_count++;
          else if (json[proxy_end] == '}') brace_count--;
          proxy_end++;
        }
        std::string proxy_json = json.substr(proxy_start, proxy_end - proxy_start);
        profile.proxy_config.type = OwlProxyManager::StringToProxyType(ExtractJSONString(proxy_json, "type"));
        profile.proxy_config.host = ExtractJSONString(proxy_json, "host");
        profile.proxy_config.port = ExtractJSONInt(proxy_json, "port", 0);
        profile.proxy_config.username = ExtractJSONString(proxy_json, "username");
        profile.proxy_config.password = ExtractJSONString(proxy_json, "password");
        profile.proxy_config.enabled = ExtractJSONBool(proxy_json, "enabled", false);
        profile.proxy_config.stealth_mode = ExtractJSONBool(proxy_json, "stealth_mode", true);
        profile.proxy_config.block_webrtc = ExtractJSONBool(proxy_json, "block_webrtc", true);
        profile.proxy_config.spoof_timezone = ExtractJSONBool(proxy_json, "spoof_timezone", true);
        profile.proxy_config.timezone_override = ExtractJSONString(proxy_json, "timezone_override");
      }
    }
  }

  // Settings
  profile.auto_save_cookies = ExtractJSONBool(json, "auto_save_cookies", true);
  profile.persist_local_storage = ExtractJSONBool(json, "persist_local_storage", true);

  return profile;
}

// ============================================================================
// OwlBrowserProfileManager Implementation
// ============================================================================

OwlBrowserProfileManager::OwlBrowserProfileManager() {}

OwlBrowserProfileManager* OwlBrowserProfileManager::GetInstance() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (instance_ == nullptr) {
    instance_ = new OwlBrowserProfileManager();
  }
  return instance_;
}

std::string OwlBrowserProfileManager::EscapeJSON(const std::string& str) {
  std::string result;
  result.reserve(str.length());
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
          snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
          result += buf;
        } else {
          result += c;
        }
    }
  }
  return result;
}

std::string OwlBrowserProfileManager::UnescapeJSON(const std::string& str) {
  std::string result;
  result.reserve(str.length());
  for (size_t i = 0; i < str.length(); ++i) {
    if (str[i] == '\\' && i + 1 < str.length()) {
      switch (str[i + 1]) {
        case '"':  result += '"'; i++; break;
        case '\\': result += '\\'; i++; break;
        case 'b':  result += '\b'; i++; break;
        case 'f':  result += '\f'; i++; break;
        case 'n':  result += '\n'; i++; break;
        case 'r':  result += '\r'; i++; break;
        case 't':  result += '\t'; i++; break;
        case 'u':
          // Handle unicode escape (simplified - just skip)
          if (i + 5 < str.length()) {
            result += '?';  // Placeholder for unicode
            i += 5;
          }
          break;
        default:   result += str[i]; break;
      }
    } else {
      result += str[i];
    }
  }
  return result;
}

std::string OwlBrowserProfileManager::GenerateProfileId() const {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 15);

  const char* hex = "0123456789abcdef";
  std::string uuid;
  uuid.reserve(36);

  for (int i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      uuid += '-';
    } else if (i == 14) {
      uuid += '4';  // Version 4 UUID
    } else if (i == 19) {
      uuid += hex[(dis(gen) & 0x3) | 0x8];  // Variant
    } else {
      uuid += hex[dis(gen)];
    }
  }

  return uuid;
}

std::string OwlBrowserProfileManager::GetCurrentTimestamp() const {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::ostringstream ss;
  ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
  return ss.str();
}

bool OwlBrowserProfileManager::CreateDirectoryIfNeeded(const std::string& dir_path) const {
  struct stat st;
  if (stat(dir_path.c_str(), &st) == 0) {
    return S_ISDIR(st.st_mode);
  }

#if defined(OS_WIN)
  return mkdir(dir_path.c_str()) == 0;
#else
  return mkdir(dir_path.c_str(), 0755) == 0;
#endif
}

std::string OwlBrowserProfileManager::GetDefaultProfileDirectory() const {
  std::string dir;

#if defined(OS_MACOS)
  const char* home = getenv("HOME");
  if (home) {
    dir = std::string(home) + "/Library/Application Support/OwlBrowser/profiles";
  }
#elif defined(OS_WIN)
  const char* appdata = getenv("APPDATA");
  if (appdata) {
    dir = std::string(appdata) + "\\OwlBrowser\\profiles";
  }
#else
  const char* home = getenv("HOME");
  if (home) {
    const char* xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config) {
      dir = std::string(xdg_config) + "/owl-browser/profiles";
    } else {
      dir = std::string(home) + "/.config/owl-browser/profiles";
    }
  }
#endif

  return dir;
}

bool OwlBrowserProfileManager::ValidateProfilePath(const std::string& profile_path) const {
  if (profile_path.empty()) return false;

  // Check if path ends with .json
  if (profile_path.length() < 5) return false;
  std::string ext = profile_path.substr(profile_path.length() - 5);
  if (ext != ".json") return false;

  return true;
}

bool OwlBrowserProfileManager::ProfileExists(const std::string& profile_path) const {
  struct stat st;
  return stat(profile_path.c_str(), &st) == 0;
}

BrowserProfile OwlBrowserProfileManager::CreateProfile(const std::string& profile_name) {
  BrowserProfile profile;

  profile.profile_id = GenerateProfileId();
  profile.profile_name = profile_name.empty() ? ("Profile " + profile.profile_id.substr(0, 8)) : profile_name;
  profile.created_at = GetCurrentTimestamp();
  profile.modified_at = profile.created_at;
  profile.fingerprint = BrowserFingerprint::GenerateRandom();

  LOG_DEBUG("ProfileManager", "Created new profile: " + profile.profile_id + " (" + profile.profile_name + ")");

  return profile;
}

BrowserProfile OwlBrowserProfileManager::LoadProfile(const std::string& profile_path) {
  if (!ValidateProfilePath(profile_path)) {
    LOG_ERROR("ProfileManager", "Invalid profile path: " + profile_path);
    return CreateProfile();  // Return new profile
  }

  if (!ProfileExists(profile_path)) {
    LOG_DEBUG("ProfileManager", "Profile does not exist, creating new: " + profile_path);
    BrowserProfile new_profile = CreateProfile();
    SaveProfile(new_profile, profile_path);
    return new_profile;
  }

  // Read file
  std::ifstream file(profile_path);
  if (!file.is_open()) {
    LOG_ERROR("ProfileManager", "Failed to open profile: " + profile_path);
    return CreateProfile();
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  file.close();

  std::string json = buffer.str();
  if (json.empty()) {
    LOG_ERROR("ProfileManager", "Profile file is empty: " + profile_path);
    return CreateProfile();
  }

  BrowserProfile profile = BrowserProfile::FromJSON(json);

  if (!profile.IsValid()) {
    LOG_WARN("ProfileManager", "Invalid profile data, creating new: " + profile_path);
    return CreateProfile();
  }

  LOG_DEBUG("ProfileManager", "Loaded profile: " + profile.profile_id + " (" + profile.profile_name + ") with " +
           std::to_string(profile.cookies.size()) + " cookies");

  return profile;
}

bool OwlBrowserProfileManager::SaveProfile(const BrowserProfile& profile, const std::string& profile_path) {
  if (!ValidateProfilePath(profile_path)) {
    LOG_ERROR("ProfileManager", "Invalid profile path: " + profile_path);
    return false;
  }

  // Create directory if needed
  size_t last_slash = profile_path.find_last_of("/\\");
  if (last_slash != std::string::npos) {
    std::string dir = profile_path.substr(0, last_slash);
    CreateDirectoryIfNeeded(dir);
  }

  // Write JSON
  std::ofstream file(profile_path);
  if (!file.is_open()) {
    LOG_ERROR("ProfileManager", "Failed to open profile for writing: " + profile_path);
    return false;
  }

  file << profile.ToJSON();
  file.close();

  LOG_DEBUG("ProfileManager", "Saved profile: " + profile.profile_id + " to " + profile_path);

  return true;
}

bool OwlBrowserProfileManager::DeleteProfile(const std::string& profile_path) {
  if (!ProfileExists(profile_path)) {
    LOG_WARN("ProfileManager", "Profile does not exist: " + profile_path);
    return false;
  }

  if (remove(profile_path.c_str()) != 0) {
    LOG_ERROR("ProfileManager", "Failed to delete profile: " + profile_path);
    return false;
  }

  LOG_DEBUG("ProfileManager", "Deleted profile: " + profile_path);
  return true;
}

std::vector<std::string> OwlBrowserProfileManager::ListProfiles(const std::string& directory) const {
  std::vector<std::string> profiles;
  std::string dir = directory.empty() ? GetDefaultProfileDirectory() : directory;

  DIR* d = opendir(dir.c_str());
  if (!d) {
    LOG_WARN("ProfileManager", "Cannot open profile directory: " + dir);
    return profiles;
  }

  struct dirent* entry;
  while ((entry = readdir(d)) != nullptr) {
    std::string filename = entry->d_name;
    if (filename.length() > 5 && filename.substr(filename.length() - 5) == ".json") {
      profiles.push_back(dir + "/" + filename);
    }
  }

  closedir(d);

  LOG_DEBUG("ProfileManager", "Found " + std::to_string(profiles.size()) + " profiles in " + dir);

  return profiles;
}

bool OwlBrowserProfileManager::UpdateProfileCookies(BrowserProfile& profile, CefRefPtr<CefBrowser> browser) {
  if (!browser) {
    LOG_ERROR("ProfileManager", "Cannot update cookies - browser is null");
    return false;
  }

  // Get all cookies from the browser
  std::string cookies_json = OwlCookieManager::GetCookies(browser, "");

  // Parse cookies
  profile.cookies = OwlCookieManager::ParseCookiesJson(cookies_json);
  profile.Touch();

  LOG_DEBUG("ProfileManager", "Updated profile cookies: " + std::to_string(profile.cookies.size()) + " cookies");

  return true;
}

bool OwlBrowserProfileManager::ApplyProfileCookies(const BrowserProfile& profile, CefRefPtr<CefBrowser> browser) {
  if (!browser) {
    LOG_ERROR("ProfileManager", "Cannot apply cookies - browser is null");
    return false;
  }

  int success_count = 0;

  for (const auto& cookie : profile.cookies) {
    // Build URL from cookie domain
    std::string url = (cookie.secure ? "https://" : "http://");
    std::string domain = cookie.domain;
    if (!domain.empty() && domain[0] == '.') {
      domain = domain.substr(1);  // Remove leading dot
    }
    url += domain + cookie.path;

    bool success = OwlCookieManager::SetCookie(
      browser,
      url,
      cookie.name,
      cookie.value,
      cookie.domain,
      cookie.path,
      cookie.secure,
      cookie.http_only,
      cookie.same_site,
      cookie.expires
    );

    if (success) success_count++;
  }

  LOG_DEBUG("ProfileManager", "Applied " + std::to_string(success_count) + "/" +
           std::to_string(profile.cookies.size()) + " cookies from profile");

  return success_count > 0 || profile.cookies.empty();
}

void OwlBrowserProfileManager::ApplyFingerprintToContext(const BrowserFingerprint& fingerprint,
                                                           const std::string& context_id) {
  // Store fingerprint for context - this will be used by OwlStealth
  std::lock_guard<std::mutex> lock(profiles_mutex_);
  // We'll update this once we modify BrowserContext to include fingerprint
  LOG_DEBUG("ProfileManager", "Applied fingerprint to context: " + context_id);
}

BrowserFingerprint OwlBrowserProfileManager::GetContextFingerprint(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(profiles_mutex_);
  // Return default fingerprint for now
  return BrowserFingerprint();
}
