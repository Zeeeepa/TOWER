#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include "owl_browser_manager.h"
#include "owl_proxy_manager.h"
#include "owl_cookie_manager.h"

// Browser fingerprint configuration - all the values needed to recreate a consistent identity
struct BrowserFingerprint {
  // VirtualMachine ID - links to the complete VM profile
  // When set, this determines ALL fingerprint values from the VM database
  std::string vm_id;                   // e.g., "win10-nvidia-gtx1660ti-chrome142"

  // User Agent and Navigator
  std::string user_agent;              // Full UA string
  std::string platform;                // e.g., "Win32"
  std::string vendor;                  // e.g., "Google Inc."
  std::vector<std::string> languages;  // e.g., ["en-US", "en"]
  int hardware_concurrency = 8;        // CPU cores
  int device_memory = 8;               // GB
  int max_touch_points = 0;            // Touch support

  // =========================================================================
  // Fingerprint Seeds - Core 64-bit seeds for deterministic noise generation
  // These are the PRIMARY seeds used by the Seed API (owl_seed_api.h)
  // =========================================================================
  uint64_t canvas_seed = 0;            // Seed for Canvas 2D fingerprint noise
  uint64_t webgl_seed = 0;             // Seed for WebGL fingerprint noise
  uint64_t audio_seed = 0;             // Seed for AudioContext fingerprint noise
  uint64_t fonts_seed = 0;             // Seed for font enumeration noise
  uint64_t client_rects_seed = 0;      // Seed for getBoundingClientRect noise
  uint64_t navigator_seed = 0;         // Seed for navigator property noise
  uint64_t screen_seed = 0;            // Seed for screen property noise

  // Audio fingerprint value (realistic value in 124.0-124.1 range)
  double audio_fingerprint = 124.04344968475198;

  // 32-char lowercase hex hashes (MD5-style format for fingerprint.com compatibility)
  std::string canvas_geometry_hash;    // For canvas.Geometry
  std::string canvas_text_hash;        // For canvas.Text
  std::string webgl_params_hash;       // For webGlExtensions.parameters
  std::string webgl_extensions_hash;   // For webGlExtensions.extensions
  std::string webgl_context_hash;      // For webGlExtensions.contextAttributes
  std::string webgl_ext_params_hash;   // For webGlExtensions.extensionParameters
  std::string shader_precisions_hash;  // For webGlExtensions.shaderPrecisions
  std::string fonts_hash;              // For font enumeration
  std::string plugins_hash;            // For plugin enumeration

  // Legacy fields (kept for backwards compatibility)
  uint64_t canvas_hash_seed = 0;       // Legacy: maps to canvas_seed
  double canvas_noise_seed = 0.0;      // Deterministic noise value (0.0001-0.0005)

  // WebGL fingerprinting
  int gpu_profile_index = 0;           // Index into GPU profiles (0=GTX1660Ti, 1=Intel UHD, 2=RTX3060)
  std::string webgl_vendor;            // e.g., "Google Inc. (NVIDIA)"
  std::string webgl_renderer;          // e.g., "ANGLE (NVIDIA, NVIDIA GeForce GTX 1660 Ti...)"

  // Screen/Display
  int screen_width = 1920;
  int screen_height = 1080;
  int color_depth = 24;
  int pixel_ratio = 1;

  // Timezone and locale
  std::string timezone;                // IANA timezone, e.g., "America/New_York"
  std::string locale;                  // e.g., "en-US"

  // Audio context (legacy - audio_seed is now the primary)
  double audio_noise_seed = 0.0;       // For AudioContext fingerprinting

  // Font fingerprinting
  std::vector<std::string> installed_fonts;  // Simulated font list

  // Plugin info
  bool has_pdf_plugin = true;
  bool has_chrome_pdf = true;

  // Default constructor with randomized values
  BrowserFingerprint();

  // Generate random fingerprint
  static BrowserFingerprint GenerateRandom();

  // Serialize to JSON
  std::string ToJSON() const;

  // Parse from JSON
  static BrowserFingerprint FromJSON(const std::string& json);
};

// Complete browser profile - identity + cookies + settings
struct BrowserProfile {
  // Profile metadata
  std::string profile_id;              // Unique profile identifier
  std::string profile_name;            // User-friendly name
  std::string created_at;              // ISO 8601 timestamp
  std::string modified_at;             // ISO 8601 timestamp
  int version = 1;                     // Profile version for migrations

  // Browser fingerprint (consistent identity)
  BrowserFingerprint fingerprint;

  // Cookies (keyed by domain for efficient lookup)
  std::vector<CookieData> cookies;

  // LLM configuration (optional)
  bool has_llm_config = false;
  LLMConfig llm_config;

  // Proxy configuration (optional)
  bool has_proxy_config = false;
  ProxyConfig proxy_config;

  // Local storage data (keyed by origin)
  std::map<std::string, std::map<std::string, std::string>> local_storage;

  // Session storage data (keyed by origin) - transient, but saved for session resume
  std::map<std::string, std::map<std::string, std::string>> session_storage;

  // Profile settings
  bool auto_save_cookies = true;       // Automatically update cookies on changes
  bool persist_local_storage = true;   // Save local storage on close

  // Default constructor
  BrowserProfile();

  // Create with ID
  explicit BrowserProfile(const std::string& id);

  // Serialize to JSON
  std::string ToJSON() const;

  // Parse from JSON
  static BrowserProfile FromJSON(const std::string& json);

  // Validate profile
  bool IsValid() const;

  // Update modified timestamp
  void Touch();
};

// Browser profile manager - handles loading, saving, and managing profiles
class OwlBrowserProfileManager {
public:
  // Singleton access
  static OwlBrowserProfileManager* GetInstance();

  // Profile lifecycle
  // Load profile from file path (creates new if doesn't exist)
  BrowserProfile LoadProfile(const std::string& profile_path);

  // Save profile to file path
  bool SaveProfile(const BrowserProfile& profile, const std::string& profile_path);

  // Create new profile with random fingerprint
  BrowserProfile CreateProfile(const std::string& profile_name = "");

  // Delete profile file
  bool DeleteProfile(const std::string& profile_path);

  // Profile validation
  bool ValidateProfilePath(const std::string& profile_path) const;
  bool ProfileExists(const std::string& profile_path) const;

  // Cookie management for profiles
  // Get all cookies from browser and update profile
  bool UpdateProfileCookies(BrowserProfile& profile, CefRefPtr<CefBrowser> browser);

  // Apply profile cookies to browser
  bool ApplyProfileCookies(const BrowserProfile& profile, CefRefPtr<CefBrowser> browser);

  // Fingerprint management
  // Apply fingerprint to stealth system for a context
  void ApplyFingerprintToContext(const BrowserFingerprint& fingerprint,
                                  const std::string& context_id);

  // Get current fingerprint from context
  BrowserFingerprint GetContextFingerprint(const std::string& context_id);

  // Profile directory
  std::string GetDefaultProfileDirectory() const;

  // List all profiles in directory
  std::vector<std::string> ListProfiles(const std::string& directory = "") const;

private:
  OwlBrowserProfileManager();
  ~OwlBrowserProfileManager() = default;

  // Prevent copying
  OwlBrowserProfileManager(const OwlBrowserProfileManager&) = delete;
  OwlBrowserProfileManager& operator=(const OwlBrowserProfileManager&) = delete;

  static OwlBrowserProfileManager* instance_;
  static std::mutex instance_mutex_;

  // Active profiles (context_id -> profile)
  std::map<std::string, BrowserProfile> active_profiles_;
  std::mutex profiles_mutex_;

  // Helper methods
  std::string GenerateProfileId() const;
  std::string GetCurrentTimestamp() const;
  bool CreateDirectoryIfNeeded(const std::string& dir_path) const;

public:
  // JSON parsing helpers (public for use by struct serialization)
  static std::string EscapeJSON(const std::string& str);
  static std::string UnescapeJSON(const std::string& str);
};
