#ifndef OWL_VIRTUAL_CAMERA_H_
#define OWL_VIRTUAL_CAMERA_H_

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <map>

namespace OwlVirtualCamera {

// Frame format for virtual camera
struct VideoFrame {
  std::vector<uint8_t> data;  // RGBA or BGRA pixel data
  int width;
  int height;
  int stride;  // bytes per row
  uint64_t timestamp_ms;
};

// Supported input formats
enum class InputFormat {
  IMAGE_PNG,
  IMAGE_JPEG,
  VIDEO_Y4M,     // Uncompressed YUV video (Chromium native format)
  VIDEO_RAW,     // Raw RGBA frames
  GENERATED      // Programmatically generated frames
};

// Background type for virtual camera
enum class BackgroundType {
  SOLID_COLOR,   // Single solid color
  IMAGE,         // Static image
  VIDEO          // Looping video
};

// Camera device info for enumeration spoofing
struct VirtualDeviceInfo {
  std::string device_id;
  std::string label;
  std::string group_id;
  int width;
  int height;
  int fps;
};

// Gesture types for reCAPTCHA hand gesture challenges
enum class GestureType {
  UNKNOWN,
  THUMBS_UP,
  THUMBS_DOWN,
  PEACE_SIGN,       // V sign / victory
  OPEN_PALM,        // Show palm / stop
  CLOSED_FIST,
  POINTING_UP,
  POINTING_LEFT,
  POINTING_RIGHT,
  OK_SIGN,          // Circle with thumb and index
  WAVE,             // Waving hand
  ROCK_ON,          // Devil horns
  CALL_ME,          // Phone gesture
  PINCH,            // Pinching fingers
};

// Virtual camera manager - supports both singleton (legacy) and per-context instances
class VirtualCameraManager {
public:
  // Get global singleton instance (legacy - for backward compatibility)
  static VirtualCameraManager* GetInstance();

  // Get or create a per-context camera instance (for concurrent usage)
  static VirtualCameraManager* GetInstanceForContext(const std::string& context_id);

  // Release a per-context camera instance
  static void ReleaseInstanceForContext(const std::string& context_id);

  // Initialize with gesture images directory
  bool Initialize(const std::string& gestures_dir = "");

  // Shutdown and cleanup
  void Shutdown();

  // Check if virtual camera is enabled
  bool IsEnabled() const { return enabled_.load(); }

  // Enable/disable virtual camera
  void SetEnabled(bool enabled) { enabled_.store(enabled); }

  // ============================================================================
  // BACKGROUND LAYER (always visible, base layer)
  // ============================================================================

  // Set solid color background (default: dark gray)
  void SetBackgroundColor(uint8_t r, uint8_t g, uint8_t b);

  // Set image as background (static)
  bool SetBackgroundImage(const std::string& image_path);
  bool SetBackgroundImage(const std::vector<uint8_t>& image_data, InputFormat format);

  // Set video as background (loops automatically)
  bool SetBackgroundVideo(const std::string& video_path);

  // Clear background (reset to default color)
  void ClearBackground();

  // Get current background type
  BackgroundType GetBackgroundType() const { return background_type_; }

  // ============================================================================
  // OVERLAY LAYER (on top of background, for gestures/temporary content)
  // ============================================================================

  // Show overlay image (e.g., gesture image)
  bool SetOverlayImage(const std::string& image_path);
  bool SetOverlayImage(const std::vector<uint8_t>& image_data, InputFormat format);

  // Show overlay video (loops until cleared)
  bool SetOverlayVideo(const std::string& video_path);

  // Load and show a gesture image by type
  bool ShowGesture(GestureType gesture);

  // Clear overlay (show only background)
  void ClearOverlay();

  // Check if overlay is active
  bool HasOverlay() const { return overlay_active_.load(); }

  // ============================================================================
  // LEGACY FRAME SOURCE MANAGEMENT (kept for compatibility)
  // ============================================================================

  // Load a static image as the camera source
  bool LoadImageSource(const std::string& image_path);
  bool LoadImageSource(const std::vector<uint8_t>& image_data, InputFormat format);

  // Load a video file as the camera source (loops automatically)
  bool LoadVideoSource(const std::string& video_path);

  // Load a gesture image by type (deprecated, use ShowGesture instead)
  bool LoadGestureImage(GestureType gesture);

  // Set a solid color as camera source (for testing)
  void SetSolidColor(uint8_t r, uint8_t g, uint8_t b);

  // Generate test pattern (moving shapes)
  void SetTestPattern();

  // ============================================================================
  // FRAME RETRIEVAL
  // ============================================================================

  // Get the current frame (thread-safe)
  bool GetCurrentFrame(VideoFrame& frame);

  // Get frame as PNG data
  std::vector<uint8_t> GetCurrentFramePNG();

  // Get frame as base64-encoded JPEG (for WebRTC)
  std::string GetCurrentFrameBase64JPEG(int quality = 85);

  // ============================================================================
  // DEVICE ENUMERATION
  // ============================================================================

  // Get virtual device info for JavaScript injection
  VirtualDeviceInfo GetDeviceInfo() const;

  // Set custom device info
  void SetDeviceInfo(const VirtualDeviceInfo& info);

  // ============================================================================
  // GESTURE DETECTION HELPERS
  // ============================================================================

  // Parse gesture type from text description
  static GestureType ParseGestureFromText(const std::string& text);

  // Get gesture image path
  std::string GetGestureImagePath(GestureType gesture) const;

  // Get all available gesture types
  std::vector<GestureType> GetAvailableGestures() const;

  // ============================================================================
  // JAVASCRIPT INJECTION CODE
  // ============================================================================

  // Get JavaScript code to inject for device spoofing
  // This overrides navigator.mediaDevices to return our virtual camera
  static std::string GetDeviceSpoofingJS();

  // Get JavaScript code to create a fake MediaStream
  static std::string GetFakeMediaStreamJS();

  // Allow deletion for per-context instances
  ~VirtualCameraManager();

private:
  VirtualCameraManager();

  // Prevent copying
  VirtualCameraManager(const VirtualCameraManager&) = delete;
  VirtualCameraManager& operator=(const VirtualCameraManager&) = delete;

  // Internal frame management
  void UpdateTestPatternFrame();
  bool DecodeImage(const std::vector<uint8_t>& data, InputFormat format);
  bool DecodeImageToFrame(const std::vector<uint8_t>& data, InputFormat format, VideoFrame& out_frame);
  bool LoadY4MVideo(const std::string& path);
  bool LoadY4MVideoToFrames(const std::string& path, std::vector<VideoFrame>& out_frames);

  // Compositing - combines background + overlay into current_frame_
  void CompositeFrame();

  // Member variables
  std::atomic<bool> enabled_{false};
  std::atomic<bool> initialized_{false};

  mutable std::mutex frame_mutex_;
  VideoFrame current_frame_;  // Final composited frame

  // Background layer
  BackgroundType background_type_{BackgroundType::SOLID_COLOR};
  uint8_t bg_color_r_{26}, bg_color_g_{26}, bg_color_b_{46};  // Default dark blue-gray
  VideoFrame background_frame_;
  std::vector<VideoFrame> background_video_frames_;
  size_t background_video_index_{0};

  // Overlay layer
  std::atomic<bool> overlay_active_{false};
  VideoFrame overlay_frame_;
  std::vector<VideoFrame> overlay_video_frames_;
  size_t overlay_video_index_{0};
  bool overlay_is_video_{false};

  // Legacy video playback (for compatibility)
  std::vector<VideoFrame> video_frames_;
  size_t current_video_frame_index_{0};

  std::string gestures_directory_;
  std::map<GestureType, std::string> gesture_paths_;

  VirtualDeviceInfo device_info_;

  // Test pattern state
  int test_pattern_offset_{0};

  // Frame dimensions (default 640x480)
  int frame_width_{640};
  int frame_height_{480};

  static VirtualCameraManager* instance_;
  static std::mutex instance_mutex_;
  static std::map<std::string, std::unique_ptr<VirtualCameraManager>> context_instances_;
  static std::mutex context_instances_mutex_;
};

// ============================================================================
// CEF INTEGRATION HELPERS
// ============================================================================

// Get command-line switches needed for fake media stream
std::vector<std::string> GetFakeMediaCommandLineSwitches();

// Check if a permission request is for camera access
bool IsCameraPermissionRequest(uint32_t requested_permissions);

// Check if a permission request is for microphone access
bool IsMicrophonePermissionRequest(uint32_t requested_permissions);

}  // namespace OwlVirtualCamera

#endif  // OWL_VIRTUAL_CAMERA_H_
