#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <functional>
#include "include/cef_browser.h"
#include "include/cef_app.h"
#include "include/cef_request_context_handler.h"
#include "owl_video_recorder.h"  // Need full definition for unique_ptr in BrowserContext
#include "owl_llm_client.h"      // Need full definition for unique_ptr in BrowserContext
#include "owl_proxy_manager.h"   // Proxy configuration
#include "owl_thread_pool.h"     // Thread pool for concurrent operations
#include "action_result.h"       // ActionResult and VerificationLevel

// Forward declarations
class OwlLlamaServer;
struct BrowserProfile;
struct BrowserFingerprint;

// Full include for CefRefPtr member (can't use forward declaration with CefRefPtr)
#include "owl_request_context_handler.h"

namespace owl {
  class ThreadPool;
  class VideoTimerManager;
}

// LLM Configuration
struct LLMConfig {
  bool enabled = true;                       // Enable/disable LLM features
  bool use_builtin = true;                   // Use built-in llama-server (if available)
  std::string provider_name = "";            // User-friendly provider name
  std::string external_endpoint = "";        // External API endpoint (e.g., "https://api.openai.com")
  std::string external_model = "";           // External model name (e.g., "gpt-4-vision-preview")
  std::string external_api_key = "";         // External API key
  bool is_third_party = false;               // Is this a third-party LLM (true) or local/private (false)?
                                             // Third-party LLMs trigger PII scrubbing

  // Check if external API is configured
  bool HasExternalAPI() const {
    return !external_endpoint.empty() && !external_model.empty();
  }
};

// Fingerprint configuration stored in context (full version with all Seed API values)
struct ContextFingerprint {
  // VirtualMachine ID - determines the complete fingerprint profile
  std::string vm_id;

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

  std::string user_agent;
  std::string platform = "Win32";
  int hardware_concurrency = 8;
  int device_memory = 8;
  double canvas_noise_seed = 0.0003;   // Legacy deterministic noise value
  int gpu_profile_index = 0;
  std::string webgl_vendor;
  std::string webgl_renderer;
  int screen_width = 1920;
  int screen_height = 1080;
  std::string timezone;
  std::string locale = "en-US";
  double audio_noise_seed = 0.0;       // Legacy audio noise seed

  // Default constructor
  ContextFingerprint() {
    user_agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36";
    webgl_vendor = "Google Inc. (NVIDIA)";
    webgl_renderer = "ANGLE (NVIDIA, NVIDIA GeForce GTX 1660 Ti Direct3D11 vs_5_0 ps_5_0, D3D11)";
    timezone = "America/New_York";
  }
};

struct BrowserContext {
  CefRefPtr<CefBrowser> browser;
  std::string id;
  std::atomic<bool> in_use{false};  // Changed to atomic for thread-safety
  std::chrono::time_point<std::chrono::steady_clock> created;
  std::atomic<std::chrono::steady_clock::time_point> last_used;  // Atomic for concurrent access
  std::unique_ptr<OwlVideoRecorder> video_recorder;  // Video recorder for this context

  // Video recording - now uses shared VideoTimerManager instead of per-context threads
  // Legacy fields kept for compatibility but not used
  std::unique_ptr<std::thread> recording_timer_thread;  // DEPRECATED: Use VideoTimerManager
  std::atomic<bool> stop_recording_timer{false};  // DEPRECATED

  // LLM configuration for this specific context
  LLMConfig llm_config;

  // Per-context LLM client (for external APIs with PII scrubbing)
  std::unique_ptr<OwlLLMClient> llm_client;

  // Proxy configuration for this specific context
  ProxyConfig proxy_config;

  // Profile configuration for this context
  std::string profile_path;               // Path to profile JSON file (empty = no profile)
  bool has_profile = false;               // Is a profile loaded?
  ContextFingerprint fingerprint;         // Current fingerprint settings
  bool auto_save_profile = true;          // Auto-save cookies on changes

  // Per-context mutex for fine-grained locking
  mutable std::mutex context_mutex;

  // Active operation count for graceful shutdown
  std::atomic<uint32_t> active_ops{0};

  // Resource blocking (ads, trackers, analytics) - enabled by default
  bool resource_blocking_enabled = true;

  // Request context handler for ServiceWorker interception (remote hosts)
  // This is created before the request context and updated when VM is selected
  CefRefPtr<OwlRequestContextHandler> request_context_handler;

  BrowserContext() : has_profile(false), auto_save_profile(true), resource_blocking_enabled(true) {
    in_use.store(false, std::memory_order_relaxed);
    stop_recording_timer.store(false, std::memory_order_relaxed);
    last_used.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
    active_ops.store(0, std::memory_order_relaxed);

    // Default LLM config
    llm_config.enabled = true;
#if BUILD_WITH_LLAMA
    llm_config.use_builtin = true;
#else
    llm_config.use_builtin = false;
#endif
    // Default proxy config (disabled)
    proxy_config.enabled = false;
    proxy_config.stealth_mode = true;  // Always use stealth when proxy is enabled
  }

  // Helper for RAII operation tracking
  void BeginOperation() { active_ops.fetch_add(1, std::memory_order_relaxed); }
  void EndOperation() { active_ops.fetch_sub(1, std::memory_order_relaxed); }
  bool HasActiveOperations() const { return active_ops.load(std::memory_order_relaxed) > 0; }
};

class OwlBrowserManager {
public:
  static OwlBrowserManager* GetInstance();

  void Initialize();
  void Shutdown();

  // Message loop mode tracking (UI vs Headless)
  static void SetUsesRunMessageLoop(bool uses_run_loop);
  static bool UsesRunMessageLoop();
  static void PumpMessageLoopIfNeeded();  // Safe message loop pump (no-op in UI mode)

  // Context management
  std::string CreateContext(const LLMConfig* llm_config = nullptr, const ProxyConfig* proxy_config = nullptr, const std::string& profile_path = "", bool resource_blocking = true, const std::string& os_filter = "", const std::string& gpu_filter = "");
  void ReleaseContext(const std::string& id);  // Mark context as not in use (for pooling)
  bool CloseContext(const std::string& id);    // Actually close and destroy the context
  CefRefPtr<CefBrowser> GetBrowser(const std::string& id);
  void RegisterUIBrowser(const std::string& context_id, CefRefPtr<CefBrowser> browser, const LLMConfig* llm_config = nullptr);  // Register UI browser

  // Profile management
  std::string LoadProfile(const std::string& context_id, const std::string& profile_path);  // Load profile into existing context
  std::string SaveProfile(const std::string& context_id, const std::string& profile_path = "");  // Save context state to profile (uses context's path if empty)
  std::string GetProfile(const std::string& context_id);  // Get current profile as JSON
  std::string CreateProfile(const std::string& profile_name = "");  // Create new profile, return profile JSON
  bool UpdateProfileCookies(const std::string& context_id);  // Update profile with current cookies
  ContextFingerprint GetContextFingerprint(const std::string& context_id);  // Get fingerprint for context
  std::string GetContextInfo(const std::string& context_id);  // Get context info (VM profile, hashes) as JSON

  // LLM Configuration (file-based for UI mode)
  static std::string GetLLMConfigPath();             // Get default config file path (~/.owl_browser/llm_config.json)
  static LLMConfig LoadLLMConfigFromFile(const std::string& config_path = "");  // Load from file (uses default if empty)
  static bool SaveLLMConfigToFile(const LLMConfig& config, const std::string& config_path = "");  // Save to file

  // Traditional automation methods
  // wait_until: "" = no wait (default), "load" = wait for load event, "networkidle" = wait for network idle
  ActionResult Navigate(const std::string& context_id, const std::string& url,
                        const std::string& wait_until = "", int timeout_ms = 30000);
  ActionResult WaitForNavigation(const std::string& context_id, int timeout_ms = 30000);  // Wait for navigation to complete
  ActionResult Click(const std::string& context_id, const std::string& selector,
                     VerificationLevel level = VerificationLevel::STANDARD);
  ActionResult Type(const std::string& context_id, const std::string& selector, const std::string& text,
                    VerificationLevel level = VerificationLevel::STANDARD);
  ActionResult Pick(const std::string& context_id, const std::string& selector, const std::string& value,
                    VerificationLevel level = VerificationLevel::STANDARD);  // Select option from dropdown
  ActionResult PressKey(const std::string& context_id, const std::string& key);  // Press special keys: Enter, Tab, Escape, etc.
  ActionResult SubmitForm(const std::string& context_id);  // Submit form by pressing Enter (for search boxes)
  ActionResult Highlight(const std::string& context_id, const std::string& selector,
                const std::string& border_color = "#FF0000",
                const std::string& background_color = "rgba(255, 0, 0, 0.2)");  // Highlight element for debugging
  ActionResult ShowGridOverlay(const std::string& context_id,
                       int horizontal_lines = 25,
                       int vertical_lines = 25,
                       const std::string& line_color = "rgba(255, 0, 0, 0.15)",
                       const std::string& text_color = "rgba(255, 0, 0, 0.4)");  // Show grid overlay with XY coordinates
  ActionResult DragDrop(const std::string& context_id,
               int start_x, int start_y,
               int end_x, int end_y,
               const std::vector<std::pair<int, int>>& mid_points = {});  // Drag from start to end with optional waypoints
  ActionResult HTML5DragDrop(const std::string& context_id,
                     const std::string& source_selector,
                     const std::string& target_selector);  // HTML5 drag/drop for draggable="true" elements

  // Advanced mouse interactions
  ActionResult Hover(const std::string& context_id, const std::string& selector);  // Mouse hover without click
  ActionResult DoubleClick(const std::string& context_id, const std::string& selector);  // Double-click element
  ActionResult RightClick(const std::string& context_id, const std::string& selector);  // Right-click (context menu)

  // Human-like mouse movement (curved path with natural timing)
  ActionResult MouseMove(const std::string& context_id,
                 int start_x, int start_y,
                 int end_x, int end_y,
                 int steps = 0,  // 0 = auto-calculate based on distance
                 const std::vector<std::pair<int, int>>& stop_points = {});  // Optional pause points

  // Input control
  ActionResult ClearInput(const std::string& context_id, const std::string& selector);  // Clear text field
  ActionResult Focus(const std::string& context_id, const std::string& selector);  // Focus element
  ActionResult Blur(const std::string& context_id, const std::string& selector);  // Blur (unfocus) element
  ActionResult SelectAll(const std::string& context_id, const std::string& selector);  // Select all text in element

  // Keyboard combinations
  ActionResult KeyboardCombo(const std::string& context_id, const std::string& combo);  // E.g., "Ctrl+A", "Shift+Enter"

  // JavaScript evaluation
  std::string Evaluate(const std::string& context_id, const std::string& script, bool return_value = false);  // Execute JS and return result (return_value=true wraps as expression)

  // Element state checks
  ActionResult IsVisible(const std::string& context_id, const std::string& selector);
  ActionResult IsEnabled(const std::string& context_id, const std::string& selector);
  ActionResult IsChecked(const std::string& context_id, const std::string& selector);
  std::string GetAttribute(const std::string& context_id, const std::string& selector, const std::string& attribute);
  std::string GetBoundingBox(const std::string& context_id, const std::string& selector);  // Returns JSON {x,y,width,height}
  std::string GetElementAtPosition(const std::string& context_id, int x, int y);  // Returns JSON element info at position
  std::string GetInteractiveElements(const std::string& context_id);  // Returns JSON array of interactive elements with bounds

  // File operations
  ActionResult UploadFile(const std::string& context_id, const std::string& selector, const std::vector<std::string>& file_paths);

  // Frame/iframe handling
  std::string ListFrames(const std::string& context_id);  // List all frames
  ActionResult SwitchToFrame(const std::string& context_id, const std::string& frame_selector);  // Switch to iframe
  ActionResult SwitchToMainFrame(const std::string& context_id);  // Return to main frame

  // Network interception
  std::string AddNetworkRule(const std::string& context_id, const std::string& rule_json);  // Add interception rule
  bool RemoveNetworkRule(const std::string& rule_id);  // Remove rule by ID
  void ClearNetworkRules(const std::string& context_id);  // Clear all rules for context
  std::string GetNetworkRules(const std::string& context_id);  // Get rules as JSON
  void EnableNetworkInterception(const std::string& context_id, bool enable);
  void EnableNetworkLogging(const std::string& context_id, bool enable);
  std::string GetNetworkLog(const std::string& context_id);  // Get captured requests/responses
  void ClearNetworkLog(const std::string& context_id);  // Clear captured data

  // Console log management
  void EnableConsoleLogging(const std::string& context_id, bool enable);
  std::string GetConsoleLogs(const std::string& context_id,
                              const std::string& level_filter = "",
                              const std::string& text_filter = "",
                              int limit = 0);  // Get console logs as JSON
  void ClearConsoleLogs(const std::string& context_id);  // Clear console logs

  // Download management
  void SetDownloadPath(const std::string& context_id, const std::string& path);
  std::string GetDownloadPath(const std::string& context_id);
  void SetAutoDownload(const std::string& context_id, bool auto_download);
  std::string GetDownloads(const std::string& context_id);  // Get downloads as JSON
  std::string GetActiveDownloads(const std::string& context_id);  // Get active downloads as JSON
  bool WaitForDownload(const std::string& download_id, int timeout_ms);
  bool CancelDownload(const std::string& download_id);  // Cancel a download
  void ClearDownloads(const std::string& context_id);

  // Dialog handling (alert/confirm/prompt)
  void SetDialogAction(const std::string& context_id, const std::string& dialog_type, const std::string& action, const std::string& prompt_text = "");
  std::string GetPendingDialog(const std::string& context_id);  // Get pending dialog info
  bool HandleDialog(const std::string& dialog_id, bool accept, const std::string& response_text = "");
  bool WaitForDialog(const std::string& context_id, int timeout_ms);
  std::string GetDialogs(const std::string& context_id);  // Get all dialogs as JSON

  // Tab/window management
  void SetPopupPolicy(const std::string& context_id, const std::string& policy);  // "allow", "block", "new_tab", "background"
  std::string GetTabs(const std::string& context_id);  // Get tabs as JSON
  std::string GetActiveTab(const std::string& context_id);
  ActionResult SwitchTab(const std::string& context_id, const std::string& tab_id);
  ActionResult CloseTab(const std::string& context_id, const std::string& tab_id);  // Close a tab
  std::string NewTab(const std::string& context_id, const std::string& url = "");  // Open new tab
  int GetTabCount(const std::string& context_id);
  std::string GetBlockedPopups(const std::string& context_id);

  // Clipboard management
  std::string ClipboardRead(const std::string& context_id);   // Read text from clipboard
  ActionResult ClipboardWrite(const std::string& context_id, const std::string& text);  // Write text to clipboard
  ActionResult ClipboardClear(const std::string& context_id); // Clear clipboard

  std::string ExtractText(const std::string& context_id, const std::string& selector);
  std::vector<uint8_t> Screenshot(const std::string& context_id);
  std::vector<uint8_t> ScreenshotElement(const std::string& context_id, const std::string& selector);  // Capture specific element
  std::vector<uint8_t> ScreenshotFullpage(const std::string& context_id);  // Capture entire scrollable page

  // AI-First methods - natural language interaction
  bool AIClick(const std::string& context_id, const std::string& description);
  bool AIType(const std::string& context_id, const std::string& description, const std::string& text);
  std::string AIExtract(const std::string& context_id, const std::string& what);  // "main content", "visible text", etc.
  std::string AIAnalyze(const std::string& context_id);  // Get page intelligence
  std::string AIQuery(const std::string& context_id, const std::string& query);  // Ask questions about page

  // Semantic element finding
  std::string FindElement(const std::string& context_id, const std::string& description, int max_results = 5);

  // Performance stats
  std::string GetBlockerStats(const std::string& context_id);

  // Session pooling
  CefRefPtr<CefBrowser> GetAvailableBrowser();
  void ReturnBrowser(CefRefPtr<CefBrowser> browser);

  // Smart preloading - AI models often navigate to same sites
  std::string CreatePreloadedContext(const std::string& url);
  void PreloadCommonSites(const std::vector<std::string>& urls);

  // Content Extraction - NEW!
  std::string GetHTML(const std::string& context_id, const std::string& clean_level = "basic");
  std::string GetMarkdown(const std::string& context_id,
                         bool include_links = true,
                         bool include_images = true,
                         int max_length = -1);
  std::string ExtractJSON(const std::string& context_id,
                         const std::string& template_name = "",
                         const std::string& custom_schema = "");
  std::string DetectWebsiteType(const std::string& context_id);
  std::vector<std::string> ListTemplates();

  // AI Intelligence (On-Device LLM)
  std::string SummarizePage(const std::string& context_id, bool force_refresh = false);
  std::string QueryPage(const std::string& context_id, const std::string& query);
  std::string GetLLMStatus();

  // Natural Language Actions (NLA) - HUGE Feature!
  std::string ExecuteNLA(const std::string& context_id, const std::string& command);

  // Browser Navigation & Control
  ActionResult Reload(const std::string& context_id, bool ignore_cache = false,
                      const std::string& wait_until = "load", int timeout_ms = 30000);
  ActionResult GoBack(const std::string& context_id,
                      const std::string& wait_until = "load", int timeout_ms = 30000);
  ActionResult GoForward(const std::string& context_id,
                         const std::string& wait_until = "load", int timeout_ms = 30000);
  bool CanGoBack(const std::string& context_id);
  bool CanGoForward(const std::string& context_id);

  // Scroll Control
  ActionResult ScrollBy(const std::string& context_id, int x, int y, VerificationLevel level = VerificationLevel::NONE);
  ActionResult ScrollTo(const std::string& context_id, int x, int y, VerificationLevel level = VerificationLevel::NONE);
  ActionResult ScrollToElement(const std::string& context_id, const std::string& selector);
  ActionResult ScrollToTop(const std::string& context_id);
  ActionResult ScrollToBottom(const std::string& context_id);

  // Wait Utilities
  ActionResult WaitForSelector(const std::string& context_id, const std::string& selector, int timeout_ms = 5000);
  ActionResult WaitForTimeout(const std::string& context_id, int timeout_ms);
  ActionResult WaitForNetworkIdle(const std::string& context_id, int idle_time_ms = 500, int timeout_ms = 30000);
  ActionResult WaitForFunction(const std::string& context_id, const std::string& js_function, int polling_ms = 100, int timeout_ms = 30000);
  ActionResult WaitForURL(const std::string& context_id, const std::string& url_pattern, bool is_regex = false, int timeout_ms = 30000);

  // Page State Queries
  std::string GetCurrentURL(const std::string& context_id);
  std::string GetPageTitle(const std::string& context_id);
  std::string GetPageInfo(const std::string& context_id);

  // Viewport Manipulation
  ActionResult SetViewport(const std::string& context_id, int width, int height);
  std::string GetViewport(const std::string& context_id);

  // Video Recording
  bool StartVideoRecording(const std::string& context_id, int fps = 30, const std::string& codec = "libx264");
  bool PauseVideoRecording(const std::string& context_id);
  bool ResumeVideoRecording(const std::string& context_id);
  std::string StopVideoRecording(const std::string& context_id);  // Returns video path
  std::string GetVideoRecordingStats(const std::string& context_id);

  // Live Video Streaming - Stream browser viewport to connected clients via WebSocket/MJPEG
  bool StartLiveStream(const std::string& context_id, int fps = 15, int quality = 75);
  bool StopLiveStream(const std::string& context_id);
  bool IsLiveStreaming(const std::string& context_id) const;
  std::string GetLiveStreamStats(const std::string& context_id);
  std::string ListLiveStreams();  // List all active streams as JSON

  // LLM Integration (async background initialization)
  void InitializeLLMAsync(const LLMConfig* config = nullptr);  // Initialize with optional config
  void ShutdownLLM();
  OwlLLMClient* GetLLMClient() { return llm_client_.get(); }  // Global LLM client (for default config)
  OwlLLMClient* GetLLMClientForContext(const std::string& context_id);  // Per-context LLM client (preferred)
  bool IsLLMAvailable() const { return llm_client_ != nullptr; }
  bool IsLLMReady() const;

  // Demographics and Context Information
  std::string GetDemographics();    // Get all demographics as JSON
  std::string GetLocation();        // Get location info as JSON
  std::string GetDateTime();        // Get date/time info as JSON
  std::string GetWeather();         // Get weather info as JSON

  // Homepage and Playground
  std::string GetHomepageHTML();    // Get the custom browser homepage HTML
  std::string GetPlaygroundHTML();  // Get the developer playground HTML
  std::string GetDevConsoleHTML();  // Get the developer console HTML

  // CAPTCHA Handling - Integrated Vision-Based Solving
  std::string DetectCaptcha(const std::string& context_id);           // Detect if page has CAPTCHA (heuristic)
  std::string ClassifyCaptcha(const std::string& context_id);         // Classify CAPTCHA type
  std::string SolveTextCaptcha(const std::string& context_id,         // Solve text-based CAPTCHA
                                int max_attempts = 3);
  std::string SolveImageCaptcha(const std::string& context_id,        // Solve image-selection CAPTCHA
                                 int max_attempts = 3,
                                 const std::string& provider = "auto");  // Provider: auto, owl, recaptcha, cloudflare
  std::string SolveCaptcha(const std::string& context_id,             // Auto-detect and solve any CAPTCHA
                            int max_attempts = 3,
                            const std::string& provider = "auto");    // Provider: auto, owl, recaptcha, cloudflare

  // Cookie Management - Full control over cookies including third-party and restricted cookies
  std::string GetCookies(const std::string& context_id, const std::string& url = "");  // Get cookies as JSON array
  ActionResult SetCookie(const std::string& context_id,                       // Set a single cookie with all attributes
                 const std::string& url,
                 const std::string& name,
                 const std::string& value,
                 const std::string& domain = "",
                 const std::string& path = "/",
                 bool secure = false,
                 bool http_only = false,
                 const std::string& same_site = "lax",
                 int64_t expires = -1);  // Unix timestamp, -1 for session cookie
  ActionResult DeleteCookies(const std::string& context_id,                   // Delete cookies (empty url/name = all)
                     const std::string& url = "",
                     const std::string& cookie_name = "");

  // Proxy Management - Stealth proxy support with IP leak protection
  bool SetProxy(const std::string& context_id, const ProxyConfig& config);  // Set proxy for context
  ProxyConfig GetProxy(const std::string& context_id);                       // Get current proxy config
  std::string GetProxyStatus(const std::string& context_id);                 // Get proxy status as JSON
  bool ConnectProxy(const std::string& context_id);                          // Connect to configured proxy
  bool DisconnectProxy(const std::string& context_id);                       // Disconnect from proxy

  // Resource statistics
  std::string GetResourceStats() const;  // Get current resource usage stats as JSON
  size_t GetActiveContextCount() const;
  size_t GetTotalContextCount() const;
  std::vector<std::string> ListContexts() const;  // Get list of all context IDs

  // Configuration
  void SetMaxContexts(size_t max);
  void SetMaxMemoryMB(size_t max_mb);
  size_t GetMaxContexts() const { return max_contexts_; }
  size_t GetMaxMemoryMB() const { return max_memory_mb_; }

  // Thread pool access
  static owl::ThreadPool* GetThreadPool();

private:
  OwlBrowserManager();
  ~OwlBrowserManager();

  static OwlBrowserManager* instance_;
  static std::mutex instance_mutex_;
  static bool uses_run_message_loop_;  // True if using CefRunMessageLoop, false if manual pumping

  // Context storage with reader-writer lock for scalable concurrent access
  // Changed from std::map to std::unordered_map for O(1) lookup
  std::unordered_map<std::string, std::unique_ptr<BrowserContext>> contexts_;
  mutable std::shared_mutex contexts_mutex_;  // Reader-writer lock: multiple readers, single writer

  // Scalable limits - increased from 10 to 1000 for production use
  std::atomic<size_t> max_contexts_{1000};  // Maximum concurrent contexts (was 10)
  std::atomic<uint64_t> next_context_id_{1};
  std::atomic<int> next_browser_id_{1};  // Predict CEF browser ID (starts at 1, increments)
  std::atomic<bool> initialized_{false};

  // Memory management with actual tracking
  std::atomic<size_t> max_memory_mb_{32000};  // 32GB max (was 800MB)
  std::atomic<size_t> current_memory_mb_{0};  // Track actual usage
  std::atomic<size_t> context_count_{0};      // Track context count atomically
  size_t GetTotalMemoryUsage() const;
  size_t GetActualMemoryUsage() const;  // Platform-specific actual memory measurement

  // LLM components
  std::unique_ptr<OwlLlamaServer> llama_server_;
  std::unique_ptr<OwlLLMClient> llm_client_;
  std::mutex llm_init_mutex_;  // Protect LLM initialization

  // Background cleanup
  std::atomic<bool> cleanup_running_{false};
  std::thread cleanup_thread_;
  std::condition_variable cleanup_cv_;
  std::mutex cleanup_mutex_;
  void CleanupLoop();
  void StartCleanupThread();
  void StopCleanupThread();

  // Helper methods
  std::string GenerateContextId();
  void CleanupOldContexts();
  void RemoveOldestContext();  // DEPRECATED: Use two-phase cleanup instead
  std::unique_ptr<BrowserContext> ExtractOldestContext(std::string& out_id);  // Phase 1: Extract under lock
  void CloseBrowserContext(std::unique_ptr<BrowserContext> ctx, const std::string& context_id);  // Phase 2: Close outside lock

  // Context access helpers with fine-grained locking
  BrowserContext* GetContextPtr(const std::string& id);  // Get raw pointer (caller must handle locking)
  bool ContextExists(const std::string& id) const;

  // Configuration - OPTIMIZED for high-performance automation
  static constexpr size_t kDefaultMaxContexts = 1000;
  static constexpr size_t kDefaultMaxMemoryMB = 32000;  // 32GB
  // MEMORY OPTIMIZATION: Updated estimate with new CEF flags
  // - V8 heap reduced to 96MB (was 128MB)
  // - Disk cache limited to 50MB
  // - Media cache limited to 32MB
  // - Background video disabled
  // - BackForwardCache disabled
  // Target: ~150MB per context (down from 200MB)
  static constexpr size_t kEstimatedPerContextMB = 150;  // ~150MB per context (optimized)
  // MEMORY OPTIMIZATION: Reduced timeouts for faster cleanup
  static constexpr size_t kIdleTimeoutSec = 120;         // 2 minutes idle timeout
  static constexpr size_t kCleanupIntervalSec = 30;      // Check every 30s
};
