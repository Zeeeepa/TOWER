#include "owl_video_recorder.h"
#include "logger.h"
#include <cstdio>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

OwlVideoRecorder::OwlVideoRecorder()
  : ffmpeg_pipe_(nullptr),
    is_recording_(false),
    is_paused_(false),
    should_stop_(false),
    width_(0),
    height_(0),
    fps_(30),
    codec_("libx264"),
    frames_captured_(0),
    frames_encoded_(0),
    paused_duration_(0),
    has_last_frame_(false),
    frames_dropped_(0),
    frames_duplicated_(0),
    encoding_error_(false),
    next_output_frame_(0),
    frame_sequence_(0) {
}

OwlVideoRecorder::~OwlVideoRecorder() {
  if (is_recording_) {
    StopRecording();
  }
}

bool OwlVideoRecorder::StartRecording(int width, int height, int fps, const std::string& codec) {
  if (is_recording_) {
    LOG_ERROR("VideoRecorder", "Video recording already in progress");
    return false;
  }

  width_ = width;
  height_ = height;
  fps_ = fps;
  codec_ = codec;

  // Generate unique video path
  video_path_ = GenerateVideoPath();

  // Start ffmpeg process
  if (!StartFFmpeg()) {
    LOG_ERROR("VideoRecorder", "Failed to start ffmpeg");
    return false;
  }

  // Reset statistics
  frames_captured_ = 0;
  frames_encoded_ = 0;
  frames_dropped_ = 0;
  frames_duplicated_ = 0;
  encoding_error_ = false;
  start_time_ = std::chrono::steady_clock::now();
  recording_start_time_ = start_time_;
  paused_duration_ = std::chrono::duration<double>(0);
  next_output_frame_ = 0;
  frame_sequence_ = 0;
  has_last_frame_ = false;

  // Clear queues
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!frame_queue_.empty()) frame_queue_.pop();
  }
  {
    std::lock_guard<std::mutex> lock(output_mutex_);
    while (!output_queue_.empty()) output_queue_.pop();
  }

  // Start threads
  should_stop_ = false;
  is_recording_ = true;
  is_paused_ = false;

  // Start frame sync thread - handles timing and frame duplication
  frame_sync_thread_ = std::make_unique<std::thread>(&OwlVideoRecorder::FrameSyncThreadFunc, this);

  // Start encoding thread - writes frames to ffmpeg
  encoding_thread_ = std::make_unique<std::thread>(&OwlVideoRecorder::EncodingThreadFunc, this);

  LOG_DEBUG("VideoRecorder", "Video recording started: " + video_path_ + " @ " + std::to_string(fps) + "fps");
  return true;
}

bool OwlVideoRecorder::PauseRecording() {
  if (!is_recording_) {
    LOG_ERROR("VideoRecorder", "No recording in progress");
    return false;
  }

  if (is_paused_) {
    LOG_WARN("VideoRecorder", "Recording already paused");
    return false;
  }

  is_paused_ = true;
  pause_time_ = std::chrono::steady_clock::now();
  LOG_DEBUG("VideoRecorder", "Video recording paused");
  return true;
}

bool OwlVideoRecorder::ResumeRecording() {
  if (!is_recording_) {
    LOG_ERROR("VideoRecorder", "No recording in progress");
    return false;
  }

  if (!is_paused_) {
    LOG_WARN("VideoRecorder", "Recording not paused");
    return false;
  }

  // Accumulate paused duration
  auto now = std::chrono::steady_clock::now();
  paused_duration_ += std::chrono::duration_cast<std::chrono::duration<double>>(now - pause_time_);

  is_paused_ = false;
  LOG_DEBUG("VideoRecorder", "Video recording resumed");
  return true;
}

std::string OwlVideoRecorder::StopRecording() {
  if (!is_recording_) {
    LOG_ERROR("VideoRecorder", "No recording in progress");
    return "";
  }

  LOG_DEBUG("VideoRecorder", "Stopping video recording...");

  // Signal threads to stop
  should_stop_ = true;
  queue_cv_.notify_all();
  output_cv_.notify_all();

  // Wait for frame sync thread to finish
  if (frame_sync_thread_ && frame_sync_thread_->joinable()) {
    frame_sync_thread_->join();
  }

  // Wait for encoding thread to finish
  if (encoding_thread_ && encoding_thread_->joinable()) {
    encoding_thread_->join();
  }

  // Stop ffmpeg
  StopFFmpeg();

  is_recording_ = false;
  is_paused_ = false;

  LOG_DEBUG("VideoRecorder", "Video recording stopped. Saved to: " + video_path_ +
           " (captured=" + std::to_string(frames_captured_.load()) +
           ", encoded=" + std::to_string(frames_encoded_.load()) +
           ", duplicated=" + std::to_string(frames_duplicated_.load()) +
           ", dropped=" + std::to_string(frames_dropped_.load()) + ")");
  return video_path_;
}

void OwlVideoRecorder::AddFrame(const void* frame_data, int width, int height) {
  if (!is_recording_ || is_paused_) {
    return;
  }

  // Check for encoding errors - stop accepting frames if encoder failed
  if (encoding_error_.load(std::memory_order_acquire)) {
    return;
  }

  if (width != width_ || height != height_) {
    LOG_WARN("VideoRecorder", "Frame size mismatch. Expected " + std::to_string(width_) + "x" + std::to_string(height_) +
             " but got " + std::to_string(width) + "x" + std::to_string(height));
    return;
  }

  // Check queue size for backpressure management
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    // If queue is at max capacity, drop oldest frame
    if (frame_queue_.size() >= kMaxQueuedFrames) {
      frame_queue_.pop();  // Drop oldest
      frames_dropped_.fetch_add(1, std::memory_order_relaxed);
    }
    // If above drop threshold, also drop incoming frame to catch up
    else if (frame_queue_.size() >= kDropThreshold) {
      frames_dropped_.fetch_add(1, std::memory_order_relaxed);
      // Don't add this frame - sync thread is falling behind
      return;
    }

    // Create frame and copy data
    Frame frame;
    frame.width = width;
    frame.height = height;
    frame.timestamp = std::chrono::steady_clock::now();
    frame.sequence_number = frame_sequence_.fetch_add(1, std::memory_order_relaxed);

    size_t frame_size = width * height * 4; // BGRA
    frame.data.resize(frame_size);
    std::memcpy(frame.data.data(), frame_data, frame_size);

    frame_queue_.push(std::move(frame));
    frames_captured_++;
  }

  queue_cv_.notify_one();
}

std::string OwlVideoRecorder::GetVideoPath() const {
  return video_path_;
}

OwlVideoRecorder::Stats OwlVideoRecorder::GetStats() const {
  Stats stats;
  stats.frames_captured = frames_captured_.load(std::memory_order_relaxed);
  stats.frames_encoded = frames_encoded_.load(std::memory_order_relaxed);
  stats.frames_dropped = frames_dropped_.load(std::memory_order_relaxed);
  stats.frames_duplicated = frames_duplicated_.load(std::memory_order_relaxed);
  stats.has_error = encoding_error_.load(std::memory_order_relaxed);
  stats.video_path = video_path_;

  // Get queue size under lock
  {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(queue_mutex_));
    stats.queue_size = frame_queue_.size();
  }

  if (is_recording_.load(std::memory_order_relaxed)) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(now - start_time_);
    stats.duration_seconds = (elapsed - paused_duration_).count();
  } else {
    stats.duration_seconds = 0;
  }

  return stats;
}

bool OwlVideoRecorder::StartFFmpeg() {
  // Build ffmpeg command with improved encoding settings for smooth playback
  std::ostringstream cmd;
  cmd << "ffmpeg -y"                          // Overwrite output file
      << " -f rawvideo"                       // Input format
      << " -pix_fmt rgb24"                    // Pixel format (we'll convert BGRA to RGB)
      << " -s " << width_ << "x" << height_   // Frame size
      << " -r " << fps_                       // Input frame rate
      << " -i -"                              // Read from stdin
      << " -c:v " << codec_                   // Video codec
      << " -preset veryfast"                  // Faster encoding (was 'fast')
      << " -tune zerolatency"                 // Reduce latency for smoother output
      << " -crf 23"                           // Quality level (lower = better quality)
      << " -pix_fmt yuv420p"                  // Output pixel format (for compatibility)
      << " -g " << (fps_ * 2)                 // GOP size: keyframe every 2 seconds
      << " -bf 0"                             // No B-frames for lower latency
      << " -movflags +faststart"              // Enable fast start for web playback
      << " \"" << video_path_ << "\""         // Output file
      << " >/dev/null 2>&1";                  // Redirect output to avoid blocking

  LOG_DEBUG("VideoRecorder", "Starting ffmpeg: " + cmd.str());

  // Open pipe to ffmpeg
  ffmpeg_pipe_ = popen(cmd.str().c_str(), "w");
  if (!ffmpeg_pipe_) {
    LOG_ERROR("VideoRecorder", "Failed to open pipe to ffmpeg");
    return false;
  }

  // Set pipe to non-buffering mode for more responsive writes
  setvbuf(ffmpeg_pipe_, NULL, _IONBF, 0);

  return true;
}

void OwlVideoRecorder::StopFFmpeg() {
  if (ffmpeg_pipe_) {
    // Close pipe (sends EOF to ffmpeg) - pclose waits for process to finish
    int status = pclose(ffmpeg_pipe_);
    ffmpeg_pipe_ = nullptr;

    if (status == -1) {
      LOG_ERROR("VideoRecorder", "pclose failed: " + std::string(strerror(errno)));
    } else if (WIFEXITED(status)) {
      int exit_code = WEXITSTATUS(status);
      if (exit_code == 0) {
        LOG_DEBUG("VideoRecorder", "ffmpeg process terminated successfully");
      } else {
        LOG_WARN("VideoRecorder", "ffmpeg process exited with code: " + std::to_string(exit_code));
      }
    } else if (WIFSIGNALED(status)) {
      LOG_WARN("VideoRecorder", "ffmpeg process killed by signal: " + std::to_string(WTERMSIG(status)));
    } else {
      LOG_DEBUG("VideoRecorder", "ffmpeg process terminated");
    }
  }
}

// Frame Rate Synchronization Thread
// This thread ensures we output frames at a constant rate for smooth video playback.
// When CEF doesn't provide frames fast enough (due to page being static), we duplicate
// the last frame to maintain the target frame rate.
void OwlVideoRecorder::FrameSyncThreadFunc() {
  LOG_DEBUG("VideoRecorder", "Frame sync thread started (target: " + std::to_string(fps_) + " fps)");

  // Calculate frame interval in microseconds for precise timing
  const auto frame_interval = std::chrono::microseconds(1000000 / fps_);
  auto next_frame_time = recording_start_time_;

  while (!should_stop_.load(std::memory_order_acquire)) {
    // Check if paused
    if (is_paused_.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      // Reset timing after pause
      next_frame_time = std::chrono::steady_clock::now();
      continue;
    }

    auto now = std::chrono::steady_clock::now();

    // Wait until it's time for the next frame
    if (now < next_frame_time) {
      auto sleep_time = std::chrono::duration_cast<std::chrono::microseconds>(next_frame_time - now);
      if (sleep_time.count() > 0) {
        std::this_thread::sleep_for(sleep_time);
      }
    }

    // Try to get a new frame from the input queue
    Frame frame_to_output;
    bool got_new_frame = false;

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      if (!frame_queue_.empty()) {
        frame_to_output = std::move(frame_queue_.front());
        frame_queue_.pop();
        got_new_frame = true;
      }
    }

    if (got_new_frame) {
      // Store as last frame for potential duplication
      {
        std::lock_guard<std::mutex> lock(last_frame_mutex_);
        last_frame_ = frame_to_output;  // Copy for future use
        has_last_frame_ = true;
      }

      // Push to output queue
      {
        std::lock_guard<std::mutex> lock(output_mutex_);
        if (output_queue_.size() < kMaxOutputFrames) {
          output_queue_.push(std::move(frame_to_output));
        }
      }
      output_cv_.notify_one();
    } else {
      // No new frame available - duplicate last frame for smooth playback
      std::lock_guard<std::mutex> lock(last_frame_mutex_);
      if (has_last_frame_) {
        Frame dup_frame;
        dup_frame.data = last_frame_.data;  // Copy the data
        dup_frame.width = last_frame_.width;
        dup_frame.height = last_frame_.height;
        dup_frame.timestamp = now;
        dup_frame.sequence_number = next_output_frame_.load(std::memory_order_relaxed);

        {
          std::lock_guard<std::mutex> output_lock(output_mutex_);
          if (output_queue_.size() < kMaxOutputFrames) {
            output_queue_.push(std::move(dup_frame));
            frames_duplicated_.fetch_add(1, std::memory_order_relaxed);
          }
        }
        output_cv_.notify_one();
      }
    }

    next_output_frame_.fetch_add(1, std::memory_order_relaxed);
    next_frame_time += frame_interval;

    // If we fell behind significantly, reset timing to prevent burst output
    auto time_behind = std::chrono::steady_clock::now() - next_frame_time;
    if (time_behind > std::chrono::milliseconds(500)) {
      LOG_WARN("VideoRecorder", "Frame sync fell behind by " +
               std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(time_behind).count()) +
               "ms, resetting timing");
      next_frame_time = std::chrono::steady_clock::now();
    }
  }

  LOG_DEBUG("VideoRecorder", "Frame sync thread finished");
}

// Encoding Thread
// This thread takes frames from the output queue and writes them to ffmpeg.
void OwlVideoRecorder::EncodingThreadFunc() {
  LOG_DEBUG("VideoRecorder", "Video encoding thread started");

  std::vector<uint8_t> rgb_buffer;
  rgb_buffer.resize(width_ * height_ * 3);  // RGB buffer

  while (!should_stop_.load(std::memory_order_acquire) || !output_queue_.empty()) {
    Frame frame;

    // Wait for frame or stop signal
    {
      std::unique_lock<std::mutex> lock(output_mutex_);
      output_cv_.wait_for(lock, std::chrono::milliseconds(50), [this] {
        return should_stop_.load(std::memory_order_acquire) || !output_queue_.empty();
      });

      if (output_queue_.empty()) {
        if (should_stop_.load(std::memory_order_acquire)) {
          break;
        }
        continue;
      }

      frame = std::move(output_queue_.front());
      output_queue_.pop();
    }

    // Check if pipe is still valid
    if (!ffmpeg_pipe_) {
      LOG_ERROR("VideoRecorder", "ffmpeg pipe is null, stopping encoding");
      encoding_error_.store(true, std::memory_order_release);
      break;
    }

    // Convert BGRA to RGB
    ConvertBGRAtoRGB(frame.data.data(), rgb_buffer.data(), frame.width, frame.height);

    // Write frame to ffmpeg with error checking
    size_t bytes_written = fwrite(rgb_buffer.data(), 1, rgb_buffer.size(), ffmpeg_pipe_);
    if (bytes_written != rgb_buffer.size()) {
      LOG_ERROR("VideoRecorder", "Failed to write frame to ffmpeg (wrote " +
                std::to_string(bytes_written) + "/" + std::to_string(rgb_buffer.size()) + " bytes)");
      encoding_error_.store(true, std::memory_order_release);
      break;
    }

    // Check for write errors (pipe broken, etc.)
    if (ferror(ffmpeg_pipe_)) {
      LOG_ERROR("VideoRecorder", "ffmpeg pipe error detected");
      encoding_error_.store(true, std::memory_order_release);
      break;
    }

    frames_encoded_.fetch_add(1, std::memory_order_relaxed);
  }

  LOG_DEBUG("VideoRecorder", "Video encoding thread finished. Encoded " +
           std::to_string(frames_encoded_.load(std::memory_order_relaxed)) + " frames");
}

std::string OwlVideoRecorder::GenerateVideoPath() {
  // Generate unique filename with timestamp
  auto now = std::time(nullptr);
  auto tm = *std::localtime(&now);

  std::ostringstream oss;
  oss << "/tmp/owl_recording_"
      << std::put_time(&tm, "%Y%m%d_%H%M%S")
      << "_" << getpid()
      << ".mp4";

  return oss.str();
}

void OwlVideoRecorder::ConvertBGRAtoRGB(const uint8_t* bgra, uint8_t* rgb, int width, int height) {
  // CEF provides BGRA format, ffmpeg expects RGB
  // Optimized loop with minimal branching
  const int pixel_count = width * height;
  for (int i = 0; i < pixel_count; i++) {
    rgb[i * 3 + 0] = bgra[i * 4 + 2];  // R
    rgb[i * 3 + 1] = bgra[i * 4 + 1];  // G
    rgb[i * 3 + 2] = bgra[i * 4 + 0];  // B
    // Skip alpha channel
  }
}
