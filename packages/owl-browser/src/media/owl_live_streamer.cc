#include "owl_live_streamer.h"
#include "logger.h"
#include <cstring>
#include <ctime>
#include <algorithm>

// Shared memory support (Linux and macOS)
#include "media/shared_frame_buffer.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#ifdef __linux__
#include <sys/eventfd.h>  // Linux-only: eventfd for signaling
#endif

// Use stb_image_write for JPEG encoding (no external dependencies)
// Note: turbojpeg would be faster but requires linking against libturbojpeg
#define USE_TURBOJPEG 0
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ONLY_JPEG
// Suppress deprecation warnings for sprintf used in stb_image_write.h
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "stb/stb_image_write.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace owl {

// Singleton instance
LiveStreamer* LiveStreamer::instance_ = nullptr;
std::mutex LiveStreamer::instance_mutex_;

LiveStreamer* LiveStreamer::GetInstance() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (!instance_) {
    instance_ = new LiveStreamer();
  }
  return instance_;
}

LiveStreamer::LiveStreamer() {
  LOG_DEBUG("LiveStreamer", "Live streamer initialized");
}

LiveStreamer::~LiveStreamer() {
  // Stop all streams
  std::lock_guard<std::mutex> lock(streams_mutex_);
  for (auto& pair : streams_) {
    pair.second->active = false;
  }
  streams_.clear();
  LOG_DEBUG("LiveStreamer", "Live streamer shutdown");
}

bool LiveStreamer::StartStream(const std::string& context_id, int target_fps, int quality) {
  std::lock_guard<std::mutex> lock(streams_mutex_);

  int clamped_fps = std::min(std::max(target_fps, 1), 60);  // Clamp to 1-60 fps
  int clamped_quality = std::min(std::max(quality, 10), 100);  // Clamp to 10-100

  // Check if stream already exists - update settings and return success (idempotent)
  auto it = streams_.find(context_id);
  if (it != streams_.end()) {
    it->second->target_fps = clamped_fps;
    it->second->jpeg_quality = clamped_quality;
    it->second->frame_interval = std::chrono::microseconds(1000000 / clamped_fps);
    it->second->active = true;
    LOG_DEBUG("LiveStreamer", "Updated existing stream for context " + context_id +
             " @ " + std::to_string(clamped_fps) + " fps, quality=" + std::to_string(clamped_quality));
    return true;
  }

  // Create new stream context
  auto stream = std::make_unique<StreamContext>();
  stream->context_id = context_id;
  stream->target_fps = clamped_fps;
  stream->jpeg_quality = clamped_quality;
  stream->frame_interval = std::chrono::microseconds(1000000 / stream->target_fps);
  stream->last_frame_time = std::chrono::steady_clock::now();
  stream->fps_calc_start = stream->last_frame_time;
  stream->active = true;

#ifdef __linux__
  // Create shared memory for direct frame access from HTTP server (Linux only)
  // macOS uses IPC fallback due to shared memory compatibility issues
  char shm_name[128];
  shm_generate_name(context_id.c_str(), shm_name);
  stream->shm_name = shm_name;

  // Create shared memory region
  stream->shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
  if (stream->shm_fd >= 0) {
    // Set size
    if (ftruncate(stream->shm_fd, SHM_BUFFER_TOTAL_SIZE) == 0) {
      // Map memory
      void* ptr = mmap(NULL, SHM_BUFFER_TOTAL_SIZE, PROT_READ | PROT_WRITE,
                       MAP_SHARED, stream->shm_fd, 0);
      if (ptr != MAP_FAILED) {
        stream->shm_buffer = static_cast<SharedFrameBuffer*>(ptr);
        shm_frame_buffer_init(stream->shm_buffer, context_id.c_str(),
                              clamped_fps, clamped_quality);

        // Create eventfd for signaling new frames
        stream->eventfd = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE);
        if (stream->eventfd >= 0) {
          char eventfd_path[128];
          shm_generate_eventfd_path(context_id.c_str(), eventfd_path);
          stream->eventfd_path = eventfd_path;
        } else {
          LOG_WARN("LiveStreamer", "Failed to create eventfd, falling back to polling mode");
        }

        LOG_DEBUG("LiveStreamer", "Shared memory created: " + stream->shm_name +
                 " (size: " + std::to_string(SHM_BUFFER_TOTAL_SIZE) + " bytes)");
      } else {
        LOG_WARN("LiveStreamer", "Failed to mmap shared memory: " + std::string(strerror(errno)));
        close(stream->shm_fd);
        shm_unlink(shm_name);
        stream->shm_fd = -1;
      }
    } else {
      LOG_WARN("LiveStreamer", "Failed to set shared memory size: " + std::string(strerror(errno)));
      close(stream->shm_fd);
      shm_unlink(shm_name);
      stream->shm_fd = -1;
    }
  } else {
    LOG_WARN("LiveStreamer", "Failed to create shared memory: " + std::string(strerror(errno)));
  }
#else
  // macOS: No shared memory, will use IPC fallback
  LOG_DEBUG("LiveStreamer", "Using IPC fallback for video streaming (macOS)");
#endif

  streams_[context_id] = std::move(stream);

  LOG_DEBUG("LiveStreamer", "Started stream for context " + context_id +
           " @ " + std::to_string(clamped_fps) + " fps, quality=" + std::to_string(clamped_quality));
  return true;
}

bool LiveStreamer::StopStream(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(streams_mutex_);

  auto it = streams_.find(context_id);
  if (it == streams_.end()) {
    LOG_WARN("LiveStreamer", "Stream not found for context: " + context_id);
    return false;
  }

  it->second->active = false;

#ifdef __linux__
  // Clean up shared memory resources (Linux only)
  auto& stream = it->second;
  if (stream->shm_buffer) {
    shm_frame_buffer_deactivate(stream->shm_buffer);
    munmap(stream->shm_buffer, SHM_BUFFER_TOTAL_SIZE);
    stream->shm_buffer = nullptr;
  }
  if (stream->shm_fd >= 0) {
    close(stream->shm_fd);
    shm_unlink(stream->shm_name.c_str());
    stream->shm_fd = -1;
    LOG_DEBUG("LiveStreamer", "Shared memory destroyed: " + stream->shm_name);
  }
  if (stream->eventfd >= 0) {
    close(stream->eventfd);
    stream->eventfd = -1;
  }
#endif

  streams_.erase(it);

  LOG_DEBUG("LiveStreamer", "Stopped stream for context: " + context_id);
  return true;
}

bool LiveStreamer::IsStreaming(const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(streams_mutex_);
  auto it = streams_.find(context_id);
  return it != streams_.end() && it->second->active;
}

SubscriberId LiveStreamer::Subscribe(const std::string& context_id, FrameCallback callback) {
  std::lock_guard<std::mutex> lock(streams_mutex_);

  auto it = streams_.find(context_id);
  if (it == streams_.end()) {
    LOG_ERROR("LiveStreamer", "Cannot subscribe: stream not found for context: " + context_id);
    return 0;
  }

  SubscriberId id = next_subscriber_id_.fetch_add(1, std::memory_order_relaxed);

  {
    std::lock_guard<std::mutex> sub_lock(it->second->subscribers_mutex);
    it->second->subscribers[id] = std::move(callback);
  }

  LOG_DEBUG("LiveStreamer", "Subscriber " + std::to_string(id) + " added to context " + context_id +
           " (total: " + std::to_string(it->second->subscribers.size()) + ")");

  // Send latest frame to new subscriber immediately if available
  {
    std::lock_guard<std::mutex> frame_lock(it->second->latest_frame_mutex);
    if (!it->second->latest_jpeg.empty()) {
      auto& subscribers = it->second->subscribers;
      auto sub_it = subscribers.find(id);
      if (sub_it != subscribers.end()) {
        sub_it->second(it->second->latest_jpeg, it->second->width, it->second->height,
                       it->second->latest_timestamp_ms);
      }
    }
  }

  return id;
}

void LiveStreamer::Unsubscribe(const std::string& context_id, SubscriberId subscriber_id) {
  std::lock_guard<std::mutex> lock(streams_mutex_);

  auto it = streams_.find(context_id);
  if (it == streams_.end()) {
    return;
  }

  {
    std::lock_guard<std::mutex> sub_lock(it->second->subscribers_mutex);
    it->second->subscribers.erase(subscriber_id);
  }

  LOG_DEBUG("LiveStreamer", "Subscriber " + std::to_string(subscriber_id) +
           " removed from context " + context_id);
}

size_t LiveStreamer::GetSubscriberCount(const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(streams_mutex_);

  auto it = streams_.find(context_id);
  if (it == streams_.end()) {
    return 0;
  }

  std::lock_guard<std::mutex> sub_lock(it->second->subscribers_mutex);
  return it->second->subscribers.size();
}

void LiveStreamer::AddFrame(const std::string& context_id, const void* bgra_data, int width, int height) {
  StreamContext* stream = nullptr;

  {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    auto it = streams_.find(context_id);
    if (it == streams_.end() || !it->second->active) {
      return;  // No stream for this context
    }
    stream = it->second.get();
  }

  stream->frames_received.fetch_add(1, std::memory_order_relaxed);

  // Check if enough time has passed since last frame (rate limiting)
  auto now = std::chrono::steady_clock::now();
  auto elapsed = now - stream->last_frame_time;
  if (elapsed < stream->frame_interval) {
    stream->frames_dropped.fetch_add(1, std::memory_order_relaxed);
    return;  // Too soon, skip this frame
  }
  stream->last_frame_time = now;

  // Check if we have any subscribers
  bool has_subscribers = false;
  {
    std::lock_guard<std::mutex> sub_lock(stream->subscribers_mutex);
    has_subscribers = !stream->subscribers.empty();
  }

  // Encode frame as JPEG (always encode to support GetLatestFrame)
  std::vector<uint8_t> jpeg_data;
  if (!EncodeJPEG(static_cast<const uint8_t*>(bgra_data), width, height,
                  stream->jpeg_quality, jpeg_data)) {
    LOG_ERROR("LiveStreamer", "Failed to encode JPEG for context: " + context_id);
    return;
  }

  stream->frames_encoded.fetch_add(1, std::memory_order_relaxed);
  stream->width = width;
  stream->height = height;

  // Calculate timestamp
  int64_t timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch()).count();

  // Store latest frame
  {
    std::lock_guard<std::mutex> frame_lock(stream->latest_frame_mutex);
    stream->latest_jpeg = jpeg_data;  // Copy for storage
    stream->latest_timestamp_ms = timestamp_ms;
  }

#ifdef __linux__
  // Write to shared memory for direct HTTP server access (Linux only)
  if (stream->shm_buffer) {
    if (shm_frame_buffer_write(stream->shm_buffer, jpeg_data.data(),
                                static_cast<uint32_t>(jpeg_data.size()),
                                width, height, timestamp_ms)) {
      // Signal new frame via eventfd
      if (stream->eventfd >= 0) {
        uint64_t val = 1;
        ssize_t ret = write(stream->eventfd, &val, sizeof(val));
        (void)ret;  // Ignore write errors (non-blocking)
      }
    }
  }
#endif

  // Calculate actual FPS
  stream->fps_frame_count.fetch_add(1, std::memory_order_relaxed);
  auto fps_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - stream->fps_calc_start);
  if (fps_elapsed.count() >= 1) {
    stream->actual_fps = stream->fps_frame_count.load(std::memory_order_relaxed);
    stream->fps_frame_count = 0;
    stream->fps_calc_start = now;
  }

  // Deliver to subscribers (if any)
  if (has_subscribers) {
    std::lock_guard<std::mutex> sub_lock(stream->subscribers_mutex);
    for (auto& pair : stream->subscribers) {
      // Call subscriber callback - exceptions are disabled so no try/catch needed
      pair.second(jpeg_data, width, height, timestamp_ms);
      stream->frames_sent.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

bool LiveStreamer::GetLatestFrame(const std::string& context_id, std::vector<uint8_t>& out_jpeg,
                                   int& out_width, int& out_height) {
  std::lock_guard<std::mutex> lock(streams_mutex_);

  auto it = streams_.find(context_id);
  if (it == streams_.end()) {
    return false;
  }

  std::lock_guard<std::mutex> frame_lock(it->second->latest_frame_mutex);
  if (it->second->latest_jpeg.empty()) {
    return false;
  }

  out_jpeg = it->second->latest_jpeg;
  out_width = it->second->width;
  out_height = it->second->height;
  return true;
}

LiveStreamer::StreamStats LiveStreamer::GetStats(const std::string& context_id) const {
  StreamStats stats;

  std::lock_guard<std::mutex> lock(streams_mutex_);
  auto it = streams_.find(context_id);
  if (it == streams_.end()) {
    return stats;
  }

  auto& stream = it->second;
  stats.target_fps = stream->target_fps;
  stats.actual_fps = stream->actual_fps.load(std::memory_order_relaxed);
  stats.width = stream->width;
  stats.height = stream->height;
  stats.frames_received = stream->frames_received.load(std::memory_order_relaxed);
  stats.frames_encoded = stream->frames_encoded.load(std::memory_order_relaxed);
  stats.frames_sent = stream->frames_sent.load(std::memory_order_relaxed);
  stats.frames_dropped = stream->frames_dropped.load(std::memory_order_relaxed);
  stats.is_active = stream->active.load(std::memory_order_relaxed);

  {
    std::lock_guard<std::mutex> sub_lock(stream->subscribers_mutex);
    stats.subscriber_count = stream->subscribers.size();
  }

  return stats;
}

LiveStreamer::SharedMemoryInfo LiveStreamer::GetSharedMemoryInfo(const std::string& context_id) const {
  SharedMemoryInfo info;
  info.available = false;
  info.eventfd = -1;

#ifdef __linux__
  std::lock_guard<std::mutex> lock(streams_mutex_);
  auto it = streams_.find(context_id);
  if (it == streams_.end()) {
    return info;
  }

  // Shared memory is only available on Linux
  auto& stream = it->second;
  if (stream->shm_buffer && stream->shm_fd >= 0) {
    info.shm_name = stream->shm_name;
    info.eventfd_path = stream->eventfd_path;
    info.eventfd = stream->eventfd;
    info.available = true;
  }
#else
  // macOS: shared memory not available, use IPC fallback
  (void)context_id;
#endif

  return info;
}

std::vector<std::string> LiveStreamer::ListActiveStreams() const {
  std::vector<std::string> result;
  std::lock_guard<std::mutex> lock(streams_mutex_);

  for (const auto& pair : streams_) {
    if (pair.second->active) {
      result.push_back(pair.first);
    }
  }

  return result;
}

bool LiveStreamer::EncodeJPEG(const uint8_t* bgra_data, int width, int height,
                               int quality, std::vector<uint8_t>& out_jpeg) {
#if USE_TURBOJPEG
  // Use turbojpeg for faster encoding
  tjhandle compressor = tjInitCompress();
  if (!compressor) {
    return false;
  }

  // Convert BGRA to RGB first (turbojpeg doesn't handle BGRA directly)
  std::vector<uint8_t> rgb_data(width * height * 3);
  for (int i = 0; i < width * height; i++) {
    rgb_data[i * 3 + 0] = bgra_data[i * 4 + 2];  // R
    rgb_data[i * 3 + 1] = bgra_data[i * 4 + 1];  // G
    rgb_data[i * 3 + 2] = bgra_data[i * 4 + 0];  // B
  }

  unsigned char* jpeg_buf = nullptr;
  unsigned long jpeg_size = 0;

  int result = tjCompress2(compressor, rgb_data.data(), width, 0, height,
                           TJPF_RGB, &jpeg_buf, &jpeg_size, TJSAMP_420, quality,
                           TJFLAG_FASTDCT);

  tjDestroy(compressor);

  if (result != 0 || jpeg_buf == nullptr) {
    if (jpeg_buf) tjFree(jpeg_buf);
    return false;
  }

  out_jpeg.assign(jpeg_buf, jpeg_buf + jpeg_size);
  tjFree(jpeg_buf);
  return true;

#else
  // Use stb_image_write as fallback
  // Convert BGRA to RGB first
  std::vector<uint8_t> rgb_data(width * height * 3);
  for (int i = 0; i < width * height; i++) {
    rgb_data[i * 3 + 0] = bgra_data[i * 4 + 2];  // R
    rgb_data[i * 3 + 1] = bgra_data[i * 4 + 1];  // G
    rgb_data[i * 3 + 2] = bgra_data[i * 4 + 0];  // B
  }

  // Use stbi_write_jpg_to_func to write to memory
  struct WriteContext {
    std::vector<uint8_t>* buffer;
  };

  auto write_func = [](void* context, void* data, int size) {
    auto* ctx = static_cast<WriteContext*>(context);
    auto* bytes = static_cast<uint8_t*>(data);
    ctx->buffer->insert(ctx->buffer->end(), bytes, bytes + size);
  };

  WriteContext ctx{&out_jpeg};
  int result = stbi_write_jpg_to_func(write_func, &ctx, width, height, 3,
                                       rgb_data.data(), quality);

  return result != 0;
#endif
}

} // namespace owl
