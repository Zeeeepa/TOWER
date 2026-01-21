#pragma once

#include <string>
#include <map>
#include <mutex>
#include "include/cef_frame.h"

// Forward declarations
struct ContextFingerprint;
namespace owl { struct VirtualMachine; }

// Per-context stealth configuration
struct StealthConfig {
  // VirtualMachine ID for cross-process lookup
  std::string vm_id;  // e.g., "ubuntu2204-intel-uhd620-chrome142"

  // Browser context ID for dynamic fingerprint seed generation
  std::string context_id;  // Unique per browser context, used by OwlFingerprintGenerator

  // Canvas fingerprinting
  double canvas_noise_seed = 0.0003;

  // WebGL fingerprinting
  int gpu_profile_index = 0;
  int gpu_vendor_index = -1;  // -1 = auto-select, 0 = Intel, 1 = NVIDIA, 2 = AMD
  std::string webgl_vendor;           // Masked vendor (e.g., "Google Inc. (NVIDIA)")
  std::string webgl_renderer;         // Masked renderer (e.g., "ANGLE (NVIDIA, ...)")
  std::string webgl_unmasked_vendor;  // Unmasked vendor (e.g., "NVIDIA Corporation")
  std::string webgl_unmasked_renderer; // Unmasked renderer (e.g., "NVIDIA GeForce RTX 3060")

  // Navigator properties
  std::string user_agent;
  std::string platform = "Win32";
  int hardware_concurrency = 8;
  int device_memory = 8;

  // Timezone
  std::string timezone;

  // Audio fingerprinting
  double audio_noise_seed = 0.0;

  // Screen dimensions
  int screen_width = 1920;
  int screen_height = 1080;

  // Default constructor
  StealthConfig() {
    webgl_vendor = "Google Inc. (NVIDIA)";
    webgl_renderer = "ANGLE (NVIDIA, NVIDIA GeForce GTX 1660 Ti Direct3D11 vs_5_0 ps_5_0, D3D11)";
    webgl_unmasked_vendor = "NVIDIA Corporation";
    webgl_unmasked_renderer = "NVIDIA GeForce GTX 1660 Ti";
    user_agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36";
    timezone = "America/New_York";
  }
};

class OwlStealth {
public:
  // Inject stealth patches with per-context fingerprint configuration
  static void InjectStealthPatchesWithConfig(CefRefPtr<CefFrame> frame, const StealthConfig& config);

  // Per-context fingerprint management
  static void SetContextFingerprint(int browser_id, const StealthConfig& config);
  static StealthConfig GetContextFingerprint(int browser_id);

  // Per-context VirtualMachine profile management
  static void SetContextVM(int browser_id, const owl::VirtualMachine& vm);
  static const owl::VirtualMachine* GetContextVM(int browser_id);

  // Individual patch methods (called internally by InjectStealthPatchesWithConfig)
  static void RemoveWebDriver(CefRefPtr<CefFrame> frame);
  static void RemoveCDPArtifacts(CefRefPtr<CefFrame> frame);
  static void BlockMacOSFonts(CefRefPtr<CefFrame> frame);
  static void BlockMacOSFontsConditional(CefRefPtr<CefFrame> frame, bool block);
  static void BlockClientHints(CefRefPtr<CefFrame> frame);
  static void BlockWebRTC(CefRefPtr<CefFrame> frame);
  static void BlockGeolocation(CefRefPtr<CefFrame> frame);
  static void BlockWebGPU(CefRefPtr<CefFrame> frame);
  static void PatchIframeCreationWithConfig(CefRefPtr<CefFrame> frame, const StealthConfig& config);
  static void SpoofTimezone(CefRefPtr<CefFrame> frame, const std::string& timezone);

  // Virtual Camera support - inject device spoofing for WebRTC/getUserMedia
  static void InjectVirtualCamera(CefRefPtr<CefFrame> frame);

  // Hardware simulation - realistic GPU/Canvas/Audio fingerprinting using VirtualMachine profiles
  static void InjectHardwareSimulation(CefRefPtr<CefFrame> frame, const StealthConfig& config);

  // Generate session-specific noise for fingerprinting
  static std::string GenerateSessionNoise();

  // Get effective timezone (proxy override > GeoIP from public IP > default)
  static std::string GetEffectiveTimezone(const std::string& proxy_timezone_override = "");

private:
  static std::string session_noise_;

  // Per-browser fingerprint storage (browser_id -> config)
  static std::map<int, StealthConfig> browser_configs_;
  static std::map<int, owl::VirtualMachine> browser_vms_;
  static std::mutex configs_mutex_;
};

