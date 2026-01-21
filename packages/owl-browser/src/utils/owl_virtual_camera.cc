#include "owl_virtual_camera.h"
#include "owl_image_enhancer.h"
#include "logger.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>

// stb_image for image loading (implementation already defined in owl_image_enhancer.cc)
#include "stb_image.h"

// stb_image_write for encoding (implementation already defined in owl_live_streamer.cc)
#include "stb_image_write.h"

namespace OwlVirtualCamera {

// Static instance for singleton (legacy)
VirtualCameraManager* VirtualCameraManager::instance_ = nullptr;
std::mutex VirtualCameraManager::instance_mutex_;

// Static map for per-context instances
std::map<std::string, std::unique_ptr<VirtualCameraManager>> VirtualCameraManager::context_instances_;
std::mutex VirtualCameraManager::context_instances_mutex_;

VirtualCameraManager* VirtualCameraManager::GetInstance() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (instance_ == nullptr) {
    instance_ = new VirtualCameraManager();
  }
  return instance_;
}

VirtualCameraManager* VirtualCameraManager::GetInstanceForContext(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(context_instances_mutex_);

  auto it = context_instances_.find(context_id);
  if (it != context_instances_.end()) {
    return it->second.get();
  }

  // Create new instance for this context
  auto instance = std::unique_ptr<VirtualCameraManager>(new VirtualCameraManager());
  VirtualCameraManager* ptr = instance.get();
  context_instances_[context_id] = std::move(instance);

  LOG_DEBUG("VirtualCamera", "Created per-context camera instance for: " + context_id);
  return ptr;
}

void VirtualCameraManager::ReleaseInstanceForContext(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(context_instances_mutex_);

  auto it = context_instances_.find(context_id);
  if (it != context_instances_.end()) {
    LOG_DEBUG("VirtualCamera", "Released per-context camera instance for: " + context_id);
    context_instances_.erase(it);
  }
}

VirtualCameraManager::VirtualCameraManager() {
  // Set default device info to look like a real webcam
  device_info_.device_id = "virtual-camera-001";
  device_info_.label = "HD Webcam";
  device_info_.group_id = "default";
  device_info_.width = 640;
  device_info_.height = 480;
  device_info_.fps = 30;

  // Initialize with a default black frame
  current_frame_.width = 640;
  current_frame_.height = 480;
  current_frame_.stride = 640 * 4;
  current_frame_.data.resize(640 * 480 * 4, 0);
  current_frame_.timestamp_ms = 0;
}

VirtualCameraManager::~VirtualCameraManager() {
  Shutdown();
}

bool VirtualCameraManager::Initialize(const std::string& gestures_dir) {
  if (initialized_.load()) {
    return true;
  }

  LOG_DEBUG("VirtualCamera", "Initializing virtual camera system");

  // Set gestures directory
  if (!gestures_dir.empty()) {
    gestures_directory_ = gestures_dir;
  } else {
    // Default to app resources directory
    gestures_directory_ = "gestures";
  }

  // Map gesture types to expected filenames
  gesture_paths_[GestureType::THUMBS_UP] = "thumbs_up.png";
  gesture_paths_[GestureType::THUMBS_DOWN] = "thumbs_down.png";
  gesture_paths_[GestureType::PEACE_SIGN] = "peace_sign.png";
  gesture_paths_[GestureType::OPEN_PALM] = "open_palm.png";
  gesture_paths_[GestureType::CLOSED_FIST] = "closed_fist.png";
  gesture_paths_[GestureType::POINTING_UP] = "pointing_up.png";
  gesture_paths_[GestureType::POINTING_LEFT] = "pointing_left.png";
  gesture_paths_[GestureType::POINTING_RIGHT] = "pointing_right.png";
  gesture_paths_[GestureType::OK_SIGN] = "ok_sign.png";
  gesture_paths_[GestureType::WAVE] = "wave.png";
  gesture_paths_[GestureType::ROCK_ON] = "rock_on.png";
  gesture_paths_[GestureType::CALL_ME] = "call_me.png";
  gesture_paths_[GestureType::PINCH] = "pinch.png";

  initialized_.store(true);
  enabled_.store(true);

  LOG_DEBUG("VirtualCamera", "Virtual camera initialized (gestures_dir: " + gestures_directory_ + ")");
  return true;
}

void VirtualCameraManager::Shutdown() {
  if (!initialized_.load()) {
    return;
  }

  LOG_DEBUG("VirtualCamera", "Shutting down virtual camera");

  enabled_.store(false);

  {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    current_frame_.data.clear();
    video_frames_.clear();
  }

  initialized_.store(false);
}

// ============================================================================
// FRAME SOURCE MANAGEMENT
// ============================================================================

bool VirtualCameraManager::LoadImageSource(const std::string& image_path) {
  LOG_DEBUG("VirtualCamera", "Loading image source: " + image_path);

  // Read file
  std::ifstream file(image_path, std::ios::binary);
  if (!file.is_open()) {
    LOG_ERROR("VirtualCamera", "Failed to open image file: " + image_path);
    return false;
  }

  std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
  file.close();

  // Detect format
  InputFormat format = InputFormat::IMAGE_PNG;
  if (data.size() >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
    format = InputFormat::IMAGE_JPEG;
  }

  return LoadImageSource(data, format);
}

bool VirtualCameraManager::LoadImageSource(const std::vector<uint8_t>& image_data, InputFormat format) {
  return DecodeImage(image_data, format);
}

bool VirtualCameraManager::DecodeImage(const std::vector<uint8_t>& data, InputFormat format) {
  int width, height, channels;

  // Decode with stb_image
  unsigned char* pixels = stbi_load_from_memory(
      data.data(), static_cast<int>(data.size()),
      &width, &height, &channels, 4);  // Force 4 channels (RGBA)

  if (!pixels) {
    LOG_ERROR("VirtualCamera", "Failed to decode image: " + std::string(stbi_failure_reason()));
    return false;
  }

  // Update current frame
  {
    std::lock_guard<std::mutex> lock(frame_mutex_);

    current_frame_.width = width;
    current_frame_.height = height;
    current_frame_.stride = width * 4;
    current_frame_.data.resize(width * height * 4);
    std::memcpy(current_frame_.data.data(), pixels, width * height * 4);
    current_frame_.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    // Also update device info to match frame size
    device_info_.width = width;
    device_info_.height = height;
  }

  stbi_image_free(pixels);

  LOG_DEBUG("VirtualCamera", "Loaded image: " + std::to_string(width) + "x" + std::to_string(height));
  return true;
}

bool VirtualCameraManager::LoadVideoSource(const std::string& video_path) {
  // Check if it's a Y4M file
  if (video_path.find(".y4m") != std::string::npos ||
      video_path.find(".Y4M") != std::string::npos) {
    return LoadY4MVideo(video_path);
  }

  LOG_ERROR("VirtualCamera", "Unsupported video format. Use Y4M for uncompressed video.");
  return false;
}

bool VirtualCameraManager::LoadY4MVideo(const std::string& path) {
  LOG_DEBUG("VirtualCamera", "Loading Y4M video: " + path);

  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    LOG_ERROR("VirtualCamera", "Failed to open Y4M file: " + path);
    return false;
  }

  // Read Y4M header
  std::string header;
  std::getline(file, header);

  if (header.find("YUV4MPEG2") != 0) {
    LOG_ERROR("VirtualCamera", "Invalid Y4M header");
    return false;
  }

  // Parse header parameters
  int width = 0, height = 0;
  std::istringstream hs(header);
  std::string token;
  hs >> token;  // Skip "YUV4MPEG2"

  while (hs >> token) {
    if (token[0] == 'W') {
      width = std::stoi(token.substr(1));
    } else if (token[0] == 'H') {
      height = std::stoi(token.substr(1));
    }
  }

  if (width <= 0 || height <= 0) {
    LOG_ERROR("VirtualCamera", "Failed to parse Y4M dimensions");
    return false;
  }

  LOG_DEBUG("VirtualCamera", "Y4M video size: " + std::to_string(width) + "x" + std::to_string(height));

  // Read frames
  std::vector<VideoFrame> frames;
  size_t yuv_frame_size = width * height * 3 / 2;  // YUV420

  while (file.good()) {
    // Read frame header
    std::string frame_header;
    std::getline(file, frame_header);

    if (frame_header.find("FRAME") != 0) {
      break;
    }

    // Read YUV data
    std::vector<uint8_t> yuv_data(yuv_frame_size);
    file.read(reinterpret_cast<char*>(yuv_data.data()), yuv_frame_size);

    if (file.gcount() != static_cast<std::streamsize>(yuv_frame_size)) {
      break;
    }

    // Convert YUV420 to RGBA
    VideoFrame frame;
    frame.width = width;
    frame.height = height;
    frame.stride = width * 4;
    frame.data.resize(width * height * 4);
    frame.timestamp_ms = frames.size() * 33;  // Assume 30fps

    // YUV420 to RGBA conversion
    const uint8_t* y_plane = yuv_data.data();
    const uint8_t* u_plane = y_plane + width * height;
    const uint8_t* v_plane = u_plane + (width * height / 4);

    for (int py = 0; py < height; py++) {
      for (int px = 0; px < width; px++) {
        int y_idx = py * width + px;
        int uv_idx = (py / 2) * (width / 2) + (px / 2);

        int y = y_plane[y_idx];
        int u = u_plane[uv_idx] - 128;
        int v = v_plane[uv_idx] - 128;

        int r = std::clamp(y + (1.370705 * v), 0.0, 255.0);
        int g = std::clamp(y - (0.337633 * u) - (0.698001 * v), 0.0, 255.0);
        int b = std::clamp(y + (1.732446 * u), 0.0, 255.0);

        int rgba_idx = (py * width + px) * 4;
        frame.data[rgba_idx + 0] = static_cast<uint8_t>(r);
        frame.data[rgba_idx + 1] = static_cast<uint8_t>(g);
        frame.data[rgba_idx + 2] = static_cast<uint8_t>(b);
        frame.data[rgba_idx + 3] = 255;
      }
    }

    frames.push_back(std::move(frame));
  }

  file.close();

  if (frames.empty()) {
    LOG_ERROR("VirtualCamera", "No frames loaded from Y4M file");
    return false;
  }

  // Store frames
  {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    video_frames_ = std::move(frames);
    current_video_frame_index_ = 0;
    current_frame_ = video_frames_[0];
    device_info_.width = width;
    device_info_.height = height;
  }

  LOG_DEBUG("VirtualCamera", "Loaded " + std::to_string(video_frames_.size()) + " frames from Y4M");
  return true;
}

bool VirtualCameraManager::LoadGestureImage(GestureType gesture) {
  auto it = gesture_paths_.find(gesture);
  if (it == gesture_paths_.end()) {
    LOG_ERROR("VirtualCamera", "Unknown gesture type");
    return false;
  }

  std::string full_path = gestures_directory_ + "/" + it->second;
  LOG_DEBUG("VirtualCamera", "Loading gesture image: " + full_path);

  return LoadImageSource(full_path);
}

void VirtualCameraManager::SetSolidColor(uint8_t r, uint8_t g, uint8_t b) {
  std::lock_guard<std::mutex> lock(frame_mutex_);

  int width = device_info_.width;
  int height = device_info_.height;

  current_frame_.width = width;
  current_frame_.height = height;
  current_frame_.stride = width * 4;
  current_frame_.data.resize(width * height * 4);

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int idx = (y * width + x) * 4;
      current_frame_.data[idx + 0] = r;
      current_frame_.data[idx + 1] = g;
      current_frame_.data[idx + 2] = b;
      current_frame_.data[idx + 3] = 255;
    }
  }

  current_frame_.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();

  LOG_DEBUG("VirtualCamera", "Set solid color: RGB(" +
           std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + ")");
}

void VirtualCameraManager::SetTestPattern() {
  std::lock_guard<std::mutex> lock(frame_mutex_);

  int width = device_info_.width;
  int height = device_info_.height;

  current_frame_.width = width;
  current_frame_.height = height;
  current_frame_.stride = width * 4;
  current_frame_.data.resize(width * height * 4);

  // Generate color bars with moving element
  test_pattern_offset_ = (test_pattern_offset_ + 5) % width;

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int idx = (y * width + x) * 4;

      // Color bars
      int bar = (x * 8) / width;
      uint8_t r = 0, g = 0, b = 0;

      switch (bar) {
        case 0: r = 255; g = 255; b = 255; break;  // White
        case 1: r = 255; g = 255; b = 0; break;    // Yellow
        case 2: r = 0; g = 255; b = 255; break;    // Cyan
        case 3: r = 0; g = 255; b = 0; break;      // Green
        case 4: r = 255; g = 0; b = 255; break;    // Magenta
        case 5: r = 255; g = 0; b = 0; break;      // Red
        case 6: r = 0; g = 0; b = 255; break;      // Blue
        case 7: r = 0; g = 0; b = 0; break;        // Black
      }

      // Add moving circle
      int cx = test_pattern_offset_;
      int cy = height / 2;
      int dx = x - cx;
      int dy = y - cy;
      if (dx * dx + dy * dy < 2500) {  // Radius 50
        r = 255; g = 128; b = 0;  // Orange circle
      }

      current_frame_.data[idx + 0] = r;
      current_frame_.data[idx + 1] = g;
      current_frame_.data[idx + 2] = b;
      current_frame_.data[idx + 3] = 255;
    }
  }

  current_frame_.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ============================================================================
// BACKGROUND LAYER
// ============================================================================

void VirtualCameraManager::SetBackgroundColor(uint8_t r, uint8_t g, uint8_t b) {
  std::lock_guard<std::mutex> lock(frame_mutex_);

  bg_color_r_ = r;
  bg_color_g_ = g;
  bg_color_b_ = b;
  background_type_ = BackgroundType::SOLID_COLOR;

  // Clear any image/video background
  background_frame_.data.clear();
  background_video_frames_.clear();

  // Update composited frame
  CompositeFrame();

  LOG_DEBUG("VirtualCamera", "Background set to solid color: RGB(" +
           std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + ")");
}

bool VirtualCameraManager::SetBackgroundImage(const std::string& image_path) {
  LOG_DEBUG("VirtualCamera", "Setting background image: " + image_path);

  // Read file
  std::ifstream file(image_path, std::ios::binary);
  if (!file.is_open()) {
    LOG_ERROR("VirtualCamera", "Failed to open background image: " + image_path);
    return false;
  }

  std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
  file.close();

  // Detect format
  InputFormat format = InputFormat::IMAGE_PNG;
  if (data.size() >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
    format = InputFormat::IMAGE_JPEG;
  }

  return SetBackgroundImage(data, format);
}

bool VirtualCameraManager::SetBackgroundImage(const std::vector<uint8_t>& image_data, InputFormat format) {
  std::lock_guard<std::mutex> lock(frame_mutex_);

  if (!DecodeImageToFrame(image_data, format, background_frame_)) {
    return false;
  }

  background_type_ = BackgroundType::IMAGE;
  background_video_frames_.clear();

  // Update frame dimensions
  frame_width_ = background_frame_.width;
  frame_height_ = background_frame_.height;
  device_info_.width = frame_width_;
  device_info_.height = frame_height_;

  // Update composited frame
  CompositeFrame();

  LOG_DEBUG("VirtualCamera", "Background image set: " +
           std::to_string(background_frame_.width) + "x" + std::to_string(background_frame_.height));
  return true;
}

bool VirtualCameraManager::SetBackgroundVideo(const std::string& video_path) {
  LOG_DEBUG("VirtualCamera", "Setting background video: " + video_path);

  std::lock_guard<std::mutex> lock(frame_mutex_);

  if (!LoadY4MVideoToFrames(video_path, background_video_frames_)) {
    return false;
  }

  background_type_ = BackgroundType::VIDEO;
  background_video_index_ = 0;
  background_frame_.data.clear();

  // Update frame dimensions from first video frame
  if (!background_video_frames_.empty()) {
    frame_width_ = background_video_frames_[0].width;
    frame_height_ = background_video_frames_[0].height;
    device_info_.width = frame_width_;
    device_info_.height = frame_height_;
  }

  // Update composited frame
  CompositeFrame();

  LOG_DEBUG("VirtualCamera", "Background video set: " +
           std::to_string(background_video_frames_.size()) + " frames");
  return true;
}

void VirtualCameraManager::ClearBackground() {
  std::lock_guard<std::mutex> lock(frame_mutex_);

  background_type_ = BackgroundType::SOLID_COLOR;
  bg_color_r_ = 26;
  bg_color_g_ = 26;
  bg_color_b_ = 46;  // Default dark blue-gray
  background_frame_.data.clear();
  background_video_frames_.clear();

  CompositeFrame();

  LOG_DEBUG("VirtualCamera", "Background cleared to default color");
}

// ============================================================================
// OVERLAY LAYER
// ============================================================================

bool VirtualCameraManager::SetOverlayImage(const std::string& image_path) {
  LOG_DEBUG("VirtualCamera", "Setting overlay image: " + image_path);

  // Read file
  std::ifstream file(image_path, std::ios::binary);
  if (!file.is_open()) {
    LOG_ERROR("VirtualCamera", "Failed to open overlay image: " + image_path);
    return false;
  }

  std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
  file.close();

  // Detect format
  InputFormat format = InputFormat::IMAGE_PNG;
  if (data.size() >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
    format = InputFormat::IMAGE_JPEG;
  }

  return SetOverlayImage(data, format);
}

bool VirtualCameraManager::SetOverlayImage(const std::vector<uint8_t>& image_data, InputFormat format) {
  std::lock_guard<std::mutex> lock(frame_mutex_);

  if (!DecodeImageToFrame(image_data, format, overlay_frame_)) {
    return false;
  }

  overlay_active_.store(true);
  overlay_is_video_ = false;
  overlay_video_frames_.clear();

  // Update composited frame
  CompositeFrame();

  LOG_DEBUG("VirtualCamera", "Overlay image set: " +
           std::to_string(overlay_frame_.width) + "x" + std::to_string(overlay_frame_.height));
  return true;
}

bool VirtualCameraManager::SetOverlayVideo(const std::string& video_path) {
  LOG_DEBUG("VirtualCamera", "Setting overlay video: " + video_path);

  std::lock_guard<std::mutex> lock(frame_mutex_);

  if (!LoadY4MVideoToFrames(video_path, overlay_video_frames_)) {
    return false;
  }

  overlay_active_.store(true);
  overlay_is_video_ = true;
  overlay_video_index_ = 0;
  overlay_frame_.data.clear();

  // Update composited frame
  CompositeFrame();

  LOG_DEBUG("VirtualCamera", "Overlay video set: " +
           std::to_string(overlay_video_frames_.size()) + " frames");
  return true;
}

bool VirtualCameraManager::ShowGesture(GestureType gesture) {
  auto it = gesture_paths_.find(gesture);
  if (it == gesture_paths_.end()) {
    LOG_ERROR("VirtualCamera", "Unknown gesture type");
    return false;
  }

  std::string full_path = gestures_directory_ + "/" + it->second;
  LOG_DEBUG("VirtualCamera", "Showing gesture as overlay: " + full_path);

  return SetOverlayImage(full_path);
}

void VirtualCameraManager::ClearOverlay() {
  std::lock_guard<std::mutex> lock(frame_mutex_);

  overlay_active_.store(false);
  overlay_is_video_ = false;
  overlay_frame_.data.clear();
  overlay_video_frames_.clear();

  // Update composited frame (now shows only background)
  CompositeFrame();

  LOG_DEBUG("VirtualCamera", "Overlay cleared");
}

// ============================================================================
// COMPOSITING
// ============================================================================

bool VirtualCameraManager::DecodeImageToFrame(const std::vector<uint8_t>& data, InputFormat format, VideoFrame& out_frame) {
  int width, height, channels;

  // Decode with stb_image
  unsigned char* pixels = stbi_load_from_memory(
      data.data(), static_cast<int>(data.size()),
      &width, &height, &channels, 4);  // Force 4 channels (RGBA)

  if (!pixels) {
    LOG_ERROR("VirtualCamera", "Failed to decode image: " + std::string(stbi_failure_reason()));
    return false;
  }

  out_frame.width = width;
  out_frame.height = height;
  out_frame.stride = width * 4;
  out_frame.data.resize(width * height * 4);
  std::memcpy(out_frame.data.data(), pixels, width * height * 4);
  out_frame.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();

  stbi_image_free(pixels);
  return true;
}

bool VirtualCameraManager::LoadY4MVideoToFrames(const std::string& path, std::vector<VideoFrame>& out_frames) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    LOG_ERROR("VirtualCamera", "Failed to open Y4M file: " + path);
    return false;
  }

  // Read Y4M header
  std::string header;
  std::getline(file, header);

  if (header.find("YUV4MPEG2") != 0) {
    LOG_ERROR("VirtualCamera", "Invalid Y4M header");
    return false;
  }

  // Parse header parameters
  int width = 0, height = 0;
  std::istringstream hs(header);
  std::string token;
  hs >> token;  // Skip "YUV4MPEG2"

  while (hs >> token) {
    if (token[0] == 'W') {
      width = std::stoi(token.substr(1));
    } else if (token[0] == 'H') {
      height = std::stoi(token.substr(1));
    }
  }

  if (width <= 0 || height <= 0) {
    LOG_ERROR("VirtualCamera", "Failed to parse Y4M dimensions");
    return false;
  }

  // Read frames
  out_frames.clear();
  size_t yuv_frame_size = width * height * 3 / 2;  // YUV420

  while (file.good()) {
    // Read frame header
    std::string frame_header;
    std::getline(file, frame_header);

    if (frame_header.find("FRAME") != 0) {
      break;
    }

    // Read YUV data
    std::vector<uint8_t> yuv_data(yuv_frame_size);
    file.read(reinterpret_cast<char*>(yuv_data.data()), yuv_frame_size);

    if (file.gcount() != static_cast<std::streamsize>(yuv_frame_size)) {
      break;
    }

    // Convert YUV420 to RGBA
    VideoFrame frame;
    frame.width = width;
    frame.height = height;
    frame.stride = width * 4;
    frame.data.resize(width * height * 4);
    frame.timestamp_ms = out_frames.size() * 33;  // Assume 30fps

    // YUV420 to RGBA conversion
    const uint8_t* y_plane = yuv_data.data();
    const uint8_t* u_plane = y_plane + width * height;
    const uint8_t* v_plane = u_plane + (width * height / 4);

    for (int py = 0; py < height; py++) {
      for (int px = 0; px < width; px++) {
        int y_idx = py * width + px;
        int uv_idx = (py / 2) * (width / 2) + (px / 2);

        int y = y_plane[y_idx];
        int u = u_plane[uv_idx] - 128;
        int v = v_plane[uv_idx] - 128;

        int r = std::clamp(y + (1.370705 * v), 0.0, 255.0);
        int g = std::clamp(y - (0.337633 * u) - (0.698001 * v), 0.0, 255.0);
        int b = std::clamp(y + (1.732446 * u), 0.0, 255.0);

        int rgba_idx = (py * width + px) * 4;
        frame.data[rgba_idx + 0] = static_cast<uint8_t>(r);
        frame.data[rgba_idx + 1] = static_cast<uint8_t>(g);
        frame.data[rgba_idx + 2] = static_cast<uint8_t>(b);
        frame.data[rgba_idx + 3] = 255;
      }
    }

    out_frames.push_back(std::move(frame));
  }

  file.close();
  return !out_frames.empty();
}

void VirtualCameraManager::CompositeFrame() {
  // This should be called with frame_mutex_ held

  // Determine output dimensions
  int width = frame_width_;
  int height = frame_height_;

  // Ensure current_frame_ is sized correctly
  if (current_frame_.width != width || current_frame_.height != height ||
      current_frame_.data.size() != static_cast<size_t>(width * height * 4)) {
    current_frame_.width = width;
    current_frame_.height = height;
    current_frame_.stride = width * 4;
    current_frame_.data.resize(width * height * 4);
  }

  // Step 1: Draw background
  switch (background_type_) {
    case BackgroundType::SOLID_COLOR:
      // Fill with solid color
      for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
          int idx = (y * width + x) * 4;
          current_frame_.data[idx + 0] = bg_color_r_;
          current_frame_.data[idx + 1] = bg_color_g_;
          current_frame_.data[idx + 2] = bg_color_b_;
          current_frame_.data[idx + 3] = 255;
        }
      }
      break;

    case BackgroundType::IMAGE:
      if (!background_frame_.data.empty()) {
        // Copy background image at native resolution (no scaling)
        // The JavaScript side handles aspect ratio and centering for display
        if (background_frame_.width == width && background_frame_.height == height) {
          std::memcpy(current_frame_.data.data(), background_frame_.data.data(), width * height * 4);
        } else {
          // Simple copy - the frame dimensions should match the background image
          // This happens when frame_width_/frame_height_ match background dimensions
          std::memcpy(current_frame_.data.data(), background_frame_.data.data(),
                      std::min(current_frame_.data.size(), background_frame_.data.size()));
        }
      }
      break;

    case BackgroundType::VIDEO:
      if (!background_video_frames_.empty()) {
        // Get current video frame and advance index
        const VideoFrame& bg_frame = background_video_frames_[background_video_index_];
        background_video_index_ = (background_video_index_ + 1) % background_video_frames_.size();

        if (bg_frame.width == width && bg_frame.height == height) {
          std::memcpy(current_frame_.data.data(), bg_frame.data.data(), width * height * 4);
        } else {
          // Simple nearest-neighbor scaling
          for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
              int src_x = x * bg_frame.width / width;
              int src_y = y * bg_frame.height / height;
              int src_idx = (src_y * bg_frame.width + src_x) * 4;
              int dst_idx = (y * width + x) * 4;
              std::memcpy(&current_frame_.data[dst_idx], &bg_frame.data[src_idx], 4);
            }
          }
        }
      }
      break;
  }

  // Step 2: Draw overlay on top (if active)
  if (overlay_active_.load()) {
    const VideoFrame* overlay = nullptr;

    if (overlay_is_video_ && !overlay_video_frames_.empty()) {
      overlay = &overlay_video_frames_[overlay_video_index_];
      overlay_video_index_ = (overlay_video_index_ + 1) % overlay_video_frames_.size();
    } else if (!overlay_frame_.data.empty()) {
      overlay = &overlay_frame_;
    }

    if (overlay && !overlay->data.empty()) {
      // Alpha blend overlay onto current frame (centered)
      int ox = (width - overlay->width) / 2;
      int oy = (height - overlay->height) / 2;

      for (int y = 0; y < overlay->height; y++) {
        int dst_y = oy + y;
        if (dst_y < 0 || dst_y >= height) continue;

        for (int x = 0; x < overlay->width; x++) {
          int dst_x = ox + x;
          if (dst_x < 0 || dst_x >= width) continue;

          int src_idx = (y * overlay->width + x) * 4;
          int dst_idx = (dst_y * width + dst_x) * 4;

          uint8_t src_a = overlay->data[src_idx + 3];
          if (src_a == 255) {
            // Fully opaque - just copy
            std::memcpy(&current_frame_.data[dst_idx], &overlay->data[src_idx], 4);
          } else if (src_a > 0) {
            // Alpha blend
            float alpha = src_a / 255.0f;
            float inv_alpha = 1.0f - alpha;

            current_frame_.data[dst_idx + 0] = static_cast<uint8_t>(
                overlay->data[src_idx + 0] * alpha + current_frame_.data[dst_idx + 0] * inv_alpha);
            current_frame_.data[dst_idx + 1] = static_cast<uint8_t>(
                overlay->data[src_idx + 1] * alpha + current_frame_.data[dst_idx + 1] * inv_alpha);
            current_frame_.data[dst_idx + 2] = static_cast<uint8_t>(
                overlay->data[src_idx + 2] * alpha + current_frame_.data[dst_idx + 2] * inv_alpha);
            current_frame_.data[dst_idx + 3] = 255;
          }
          // src_a == 0 means transparent, do nothing
        }
      }
    }
  }

  current_frame_.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ============================================================================
// FRAME RETRIEVAL
// ============================================================================

bool VirtualCameraManager::GetCurrentFrame(VideoFrame& frame) {
  std::lock_guard<std::mutex> lock(frame_mutex_);

  // If we have legacy video frames (for compatibility), advance to next frame
  if (!video_frames_.empty()) {
    current_video_frame_index_ = (current_video_frame_index_ + 1) % video_frames_.size();
    current_frame_ = video_frames_[current_video_frame_index_];
  }
  // If using layered approach with background/overlay videos, recomposite
  else if (background_type_ == BackgroundType::VIDEO || (overlay_active_.load() && overlay_is_video_)) {
    CompositeFrame();
  }

  frame = current_frame_;
  return !frame.data.empty();
}

std::vector<uint8_t> VirtualCameraManager::GetCurrentFramePNG() {
  VideoFrame frame;
  if (!GetCurrentFrame(frame)) {
    return {};
  }

  // Encode to PNG using stb_image_write
  std::vector<uint8_t> png_data;

  auto write_func = [](void* context, void* data, int size) {
    auto* vec = static_cast<std::vector<uint8_t>*>(context);
    auto* bytes = static_cast<uint8_t*>(data);
    vec->insert(vec->end(), bytes, bytes + size);
  };

  stbi_write_png_to_func(write_func, &png_data,
                          frame.width, frame.height, 4,
                          frame.data.data(), frame.stride);

  return png_data;
}

std::string VirtualCameraManager::GetCurrentFrameBase64JPEG(int quality) {
  VideoFrame frame;
  if (!GetCurrentFrame(frame)) {
    return "";
  }

  // Encode to JPEG
  std::vector<uint8_t> jpeg_data;

  auto write_func = [](void* context, void* data, int size) {
    auto* vec = static_cast<std::vector<uint8_t>*>(context);
    auto* bytes = static_cast<uint8_t*>(data);
    vec->insert(vec->end(), bytes, bytes + size);
  };

  stbi_write_jpg_to_func(write_func, &jpeg_data,
                          frame.width, frame.height, 4,
                          frame.data.data(), quality);

  // Base64 encode
  static const char base64_chars[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string base64;
  base64.reserve(((jpeg_data.size() + 2) / 3) * 4);

  for (size_t i = 0; i < jpeg_data.size(); i += 3) {
    uint32_t triple = (jpeg_data[i] << 16);
    if (i + 1 < jpeg_data.size()) triple |= (jpeg_data[i + 1] << 8);
    if (i + 2 < jpeg_data.size()) triple |= jpeg_data[i + 2];

    base64 += base64_chars[(triple >> 18) & 0x3F];
    base64 += base64_chars[(triple >> 12) & 0x3F];
    base64 += (i + 1 < jpeg_data.size()) ? base64_chars[(triple >> 6) & 0x3F] : '=';
    base64 += (i + 2 < jpeg_data.size()) ? base64_chars[triple & 0x3F] : '=';
  }

  return base64;
}

// ============================================================================
// DEVICE ENUMERATION
// ============================================================================

VirtualDeviceInfo VirtualCameraManager::GetDeviceInfo() const {
  return device_info_;
}

void VirtualCameraManager::SetDeviceInfo(const VirtualDeviceInfo& info) {
  device_info_ = info;
}

// ============================================================================
// GESTURE DETECTION HELPERS
// ============================================================================

GestureType VirtualCameraManager::ParseGestureFromText(const std::string& text) {
  std::string lower = text;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

  if (lower.find("thumbs up") != std::string::npos ||
      lower.find("thumbs-up") != std::string::npos) {
    return GestureType::THUMBS_UP;
  }
  if (lower.find("thumbs down") != std::string::npos ||
      lower.find("thumbs-down") != std::string::npos) {
    return GestureType::THUMBS_DOWN;
  }
  if (lower.find("peace") != std::string::npos ||
      lower.find("victory") != std::string::npos ||
      lower.find("v sign") != std::string::npos) {
    return GestureType::PEACE_SIGN;
  }
  if (lower.find("open palm") != std::string::npos ||
      lower.find("palm") != std::string::npos ||
      lower.find("stop") != std::string::npos ||
      lower.find("show hand") != std::string::npos) {
    return GestureType::OPEN_PALM;
  }
  if (lower.find("fist") != std::string::npos ||
      lower.find("closed fist") != std::string::npos) {
    return GestureType::CLOSED_FIST;
  }
  if (lower.find("point up") != std::string::npos ||
      lower.find("pointing up") != std::string::npos) {
    return GestureType::POINTING_UP;
  }
  if (lower.find("point left") != std::string::npos ||
      lower.find("pointing left") != std::string::npos) {
    return GestureType::POINTING_LEFT;
  }
  if (lower.find("point right") != std::string::npos ||
      lower.find("pointing right") != std::string::npos) {
    return GestureType::POINTING_RIGHT;
  }
  if (lower.find("ok sign") != std::string::npos ||
      lower.find("okay") != std::string::npos) {
    return GestureType::OK_SIGN;
  }
  if (lower.find("wave") != std::string::npos ||
      lower.find("waving") != std::string::npos) {
    return GestureType::WAVE;
  }
  if (lower.find("rock") != std::string::npos ||
      lower.find("horn") != std::string::npos) {
    return GestureType::ROCK_ON;
  }
  if (lower.find("call") != std::string::npos ||
      lower.find("phone") != std::string::npos) {
    return GestureType::CALL_ME;
  }
  if (lower.find("pinch") != std::string::npos) {
    return GestureType::PINCH;
  }

  return GestureType::UNKNOWN;
}

std::string VirtualCameraManager::GetGestureImagePath(GestureType gesture) const {
  auto it = gesture_paths_.find(gesture);
  if (it != gesture_paths_.end()) {
    return gestures_directory_ + "/" + it->second;
  }
  return "";
}

std::vector<GestureType> VirtualCameraManager::GetAvailableGestures() const {
  std::vector<GestureType> gestures;
  for (const auto& pair : gesture_paths_) {
    gestures.push_back(pair.first);
  }
  return gestures;
}

// ============================================================================
// JAVASCRIPT INJECTION CODE
// ============================================================================

std::string VirtualCameraManager::GetDeviceSpoofingJS() {
  // JavaScript to override navigator.mediaDevices
  return R"JS(
(function() {
  'use strict';

  // Store original functions
  const originalEnumerateDevices = navigator.mediaDevices.enumerateDevices.bind(navigator.mediaDevices);
  const originalGetUserMedia = navigator.mediaDevices.getUserMedia.bind(navigator.mediaDevices);

  // Virtual camera device info
  const virtualCameraDevice = {
    deviceId: 'virtual-camera-001',
    groupId: 'default',
    kind: 'videoinput',
    label: 'HD Webcam'
  };

  // Virtual microphone device info
  const virtualMicDevice = {
    deviceId: 'virtual-mic-001',
    groupId: 'default',
    kind: 'audioinput',
    label: 'Default Microphone'
  };

  // Override enumerateDevices to include our virtual devices
  navigator.mediaDevices.enumerateDevices = async function() {
    try {
      const realDevices = await originalEnumerateDevices();

      // Check if we already have video input devices
      const hasVideoInput = realDevices.some(d => d.kind === 'videoinput');

      if (!hasVideoInput) {
        // Add virtual camera if no real camera exists
        realDevices.push(new MediaDeviceInfo(virtualCameraDevice));
      }

      return realDevices;
    } catch (e) {
      // Return just virtual devices if enumeration fails
      return [
        new MediaDeviceInfo(virtualCameraDevice),
        new MediaDeviceInfo(virtualMicDevice)
      ];
    }
  };

  // Track if we need to use virtual camera
  let useVirtualCamera = false;

  // Create fake video track
  function createFakeVideoTrack(width, height) {
    const canvas = document.createElement('canvas');
    canvas.width = width || 640;
    canvas.height = height || 480;
    const ctx = canvas.getContext('2d');

    // Draw initial frame
    ctx.fillStyle = '#1a1a2e';
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = '#16213e';
    ctx.font = '24px Arial';
    ctx.textAlign = 'center';
    ctx.fillText('Virtual Camera', canvas.width / 2, canvas.height / 2);

    // Get stream from canvas
    const stream = canvas.captureStream(30);
    return stream.getVideoTracks()[0];
  }

  // Override getUserMedia
  navigator.mediaDevices.getUserMedia = async function(constraints) {
    try {
      // First try to get real media
      const stream = await originalGetUserMedia(constraints);
      return stream;
    } catch (error) {
      // If real camera fails, create fake stream
      if (constraints.video) {
        const width = constraints.video.width?.ideal || constraints.video.width || 640;
        const height = constraints.video.height?.ideal || constraints.video.height || 480;

        const fakeVideoTrack = createFakeVideoTrack(width, height);
        const fakeStream = new MediaStream([fakeVideoTrack]);

        // Add fake audio if requested
        if (constraints.audio) {
          const audioCtx = new AudioContext();
          const oscillator = audioCtx.createOscillator();
          const dest = audioCtx.createMediaStreamDestination();
          oscillator.connect(dest);
          oscillator.frequency.value = 0; // Silent
          oscillator.start();

          fakeStream.addTrack(dest.stream.getAudioTracks()[0]);
        }

        return fakeStream;
      }

      throw error;
    }
  };

  // Also patch legacy navigator.getUserMedia if it exists
  if (navigator.getUserMedia) {
    navigator.getUserMedia = function(constraints, success, error) {
      navigator.mediaDevices.getUserMedia(constraints)
        .then(success)
        .catch(error);
    };
  }

})();
)JS";
}

std::string VirtualCameraManager::GetFakeMediaStreamJS() {
  // JavaScript to create a fake MediaStream with custom frame injection
  return R"JS(
(function() {
  'use strict';

  // Create a fake media stream that can be fed custom frames
  window.__createFakeMediaStream = function(width, height, fps) {
    width = width || 640;
    height = height || 480;
    fps = fps || 30;

    const canvas = document.createElement('canvas');
    canvas.width = width;
    canvas.height = height;
    const ctx = canvas.getContext('2d');

    // Fill with initial color
    ctx.fillStyle = '#1a1a2e';
    ctx.fillRect(0, 0, width, height);

    // Create stream
    const stream = canvas.captureStream(fps);

    // Add method to inject frames
    stream.__injectFrame = function(imageDataUrl) {
      const img = new Image();
      img.onload = function() {
        ctx.drawImage(img, 0, 0, width, height);
      };
      img.src = imageDataUrl;
    };

    stream.__canvas = canvas;
    stream.__ctx = ctx;

    return stream;
  };

  // Function to inject a frame from base64 JPEG
  window.__injectCameraFrame = function(stream, base64Jpeg) {
    if (stream && stream.__injectFrame) {
      stream.__injectFrame('data:image/jpeg;base64,' + base64Jpeg);
    }
  };

})();
)JS";
}

// ============================================================================
// CEF INTEGRATION HELPERS
// ============================================================================

std::vector<std::string> GetFakeMediaCommandLineSwitches() {
  return {
    // Enable media stream support
    "enable-media-stream",

    // Use fake devices for testing (Chromium built-in)
    "use-fake-device-for-media-stream",

    // Auto-grant permissions (skip permission prompts)
    "use-fake-ui-for-media-stream",

    // Allow file access for local testing
    "allow-file-access-from-files",

    // Disable gesture requirement for getUserMedia
    "disable-gesture-requirement-for-media-playback"
  };
}

bool IsCameraPermissionRequest(uint32_t requested_permissions) {
  // CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE = 1 << 1 = 2
  return (requested_permissions & 2) != 0;
}

bool IsMicrophonePermissionRequest(uint32_t requested_permissions) {
  // CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE = 1 << 0 = 1
  return (requested_permissions & 1) != 0;
}

}  // namespace OwlVirtualCamera
