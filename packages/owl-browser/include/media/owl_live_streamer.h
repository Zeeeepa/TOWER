#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <chrono>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <unordered_set>

// Forward declaration for shared memory
struct SharedFrameBuffer;

/**
 * Live video streamer for browser sessions
 * Captures frames from CEF's OnPaint and streams them to connected clients
 *
 * Protocol: MJPEG over WebSocket
 * - Each frame is encoded as JPEG and sent as binary WebSocket message
 * - Clients receive frames at the configured target FPS
 * - Frame skipping when clients are slow to prevent memory buildup
 *
 * Usage:
 * 1. Create stream for a context: StartStream(context_id, fps)
 * 2. Register subscriber callbacks: Subscribe(context_id, callback)
 * 3. Feed frames from OnPaint: AddFrame(context_id, data, width, height)
 * 4. Frames are automatically JPEG-encoded and sent to all subscribers
 * 5. Stop when done: StopStream(context_id)
 */

// Forward declaration
namespace owl {
class LiveStreamer;
}

// Callback type for frame delivery
// Receives: JPEG data, width, height, timestamp_ms
using FrameCallback = std::function<void(const std::vector<uint8_t>& jpeg_data, int width, int height, int64_t timestamp_ms)>;

// Subscriber ID type
using SubscriberId = uint64_t;

namespace owl {

class LiveStreamer {
public:
  static LiveStreamer* GetInstance();

  // Stream management
  bool StartStream(const std::string& context_id, int target_fps = 15, int quality = 75);
  bool StopStream(const std::string& context_id);
  bool IsStreaming(const std::string& context_id) const;

  // Subscriber management
  SubscriberId Subscribe(const std::string& context_id, FrameCallback callback);
  void Unsubscribe(const std::string& context_id, SubscriberId subscriber_id);
  size_t GetSubscriberCount(const std::string& context_id) const;

  // Frame input (called from CEF's OnPaint via OwlClient)
  void AddFrame(const std::string& context_id, const void* bgra_data, int width, int height);

  // Get the latest JPEG frame for a context (for HTTP GET requests)
  bool GetLatestFrame(const std::string& context_id, std::vector<uint8_t>& out_jpeg, int& out_width, int& out_height);

  // Statistics
  struct StreamStats {
    int target_fps = 0;
    int actual_fps = 0;
    int width = 0;
    int height = 0;
    uint64_t frames_received = 0;
    uint64_t frames_encoded = 0;
    uint64_t frames_sent = 0;
    uint64_t frames_dropped = 0;
    size_t subscriber_count = 0;
    bool is_active = false;
  };
  StreamStats GetStats(const std::string& context_id) const;

  // Shared memory streaming info (for HTTP server direct access)
  struct SharedMemoryInfo {
    std::string shm_name;      // POSIX shared memory name (e.g., "/owl_stream_ctx_000001")
    std::string eventfd_path;  // Path to eventfd file (Linux) or empty
    int eventfd;               // eventfd file descriptor (Linux) or -1
    bool available;            // Whether shared memory is available
  };
  SharedMemoryInfo GetSharedMemoryInfo(const std::string& context_id) const;

  // List all active streams
  std::vector<std::string> ListActiveStreams() const;

private:
  LiveStreamer();
  ~LiveStreamer();

  // Non-copyable
  LiveStreamer(const LiveStreamer&) = delete;
  LiveStreamer& operator=(const LiveStreamer&) = delete;

  // Stream context for each browser context
  struct StreamContext {
    std::string context_id;
    int target_fps = 15;
    int jpeg_quality = 75;
    int width = 0;
    int height = 0;

    // Frame timing
    std::chrono::steady_clock::time_point last_frame_time;
    std::chrono::microseconds frame_interval;

    // Latest encoded frame (for new subscribers and HTTP GET)
    std::vector<uint8_t> latest_jpeg;
    std::mutex latest_frame_mutex;
    int64_t latest_timestamp_ms = 0;

    // Subscribers
    std::unordered_map<SubscriberId, FrameCallback> subscribers;
    mutable std::mutex subscribers_mutex;

    // Statistics
    std::atomic<uint64_t> frames_received{0};
    std::atomic<uint64_t> frames_encoded{0};
    std::atomic<uint64_t> frames_sent{0};
    std::atomic<uint64_t> frames_dropped{0};

    // FPS calculation
    std::chrono::steady_clock::time_point fps_calc_start;
    std::atomic<int> fps_frame_count{0};
    std::atomic<int> actual_fps{0};

    // State
    std::atomic<bool> active{false};

#ifdef __linux__
    // Shared memory (Linux only) - for direct frame access from HTTP server
    // macOS uses IPC fallback due to shared memory compatibility issues
    SharedFrameBuffer* shm_buffer = nullptr;  // Mapped shared memory
    int shm_fd = -1;                          // Shared memory file descriptor
    std::string shm_name;                     // POSIX shm name
    int eventfd = -1;                         // eventfd for signaling
    std::string eventfd_path;                 // eventfd path
#endif
  };

  // JPEG encoding (using libjpeg-turbo if available, fallback to stb_image_write)
  static bool EncodeJPEG(const uint8_t* bgra_data, int width, int height,
                         int quality, std::vector<uint8_t>& out_jpeg);

  // Stream management
  std::unordered_map<std::string, std::unique_ptr<StreamContext>> streams_;
  mutable std::mutex streams_mutex_;

  // Global subscriber ID counter
  std::atomic<SubscriberId> next_subscriber_id_{1};

  // Singleton instance
  static LiveStreamer* instance_;
  static std::mutex instance_mutex_;
};

} // namespace owl
