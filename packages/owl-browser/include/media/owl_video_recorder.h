#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <chrono>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <queue>
#include <cstdio>  // For FILE*

/**
 * Video recorder for browser sessions
 * Captures frames from CEF's OnPaint and encodes to video using ffmpeg
 *
 * IMPROVED: Uses frame rate synchronization with proper timing to handle
 * irregular frame arrival from CEF's OnPaint callback. This produces
 * smoother videos by:
 * 1. Duplicating the last frame when new frames don't arrive on time
 * 2. Using CFR (Constant Frame Rate) encoding for consistent playback
 * 3. Properly tracking timestamps for accurate duration
 */
class OwlVideoRecorder {
public:
  OwlVideoRecorder();
  ~OwlVideoRecorder();

  // Start recording with specified parameters
  // Returns: true if recording started successfully
  bool StartRecording(int width, int height, int fps = 30, const std::string& codec = "libx264");

  // Pause recording (stops encoding frames but keeps encoder alive)
  bool PauseRecording();

  // Resume recording after pause
  bool ResumeRecording();

  // Stop recording and finalize video file
  // Returns: path to saved video file in /tmp
  std::string StopRecording();

  // Add a frame to the video (called from OnPaint)
  // frame_data: BGRA pixel data from CEF
  void AddFrame(const void* frame_data, int width, int height);

  // Check recording state
  bool IsRecording() const { return is_recording_.load(std::memory_order_relaxed); }
  bool IsPaused() const { return is_paused_.load(std::memory_order_relaxed); }

  // Get current video path (empty if not recording)
  std::string GetVideoPath() const;

  // Get recording statistics
  struct Stats {
    int frames_captured;
    int frames_encoded;
    uint64_t frames_dropped;
    uint64_t frames_duplicated;  // NEW: Track frame duplication for smooth playback
    size_t queue_size;
    double duration_seconds;
    std::string video_path;
    bool has_error;
  };
  Stats GetStats() const;

private:
  // ffmpeg process management
  FILE* ffmpeg_pipe_;

  // Recording state
  std::atomic<bool> is_recording_;
  std::atomic<bool> is_paused_;
  std::atomic<bool> should_stop_;

  // Video parameters
  int width_;
  int height_;
  int fps_;
  std::string codec_;
  std::string video_path_;

  // Frame statistics
  std::atomic<int> frames_captured_;
  std::atomic<int> frames_encoded_;
  std::chrono::time_point<std::chrono::steady_clock> start_time_;
  std::chrono::time_point<std::chrono::steady_clock> pause_time_;
  std::chrono::duration<double> paused_duration_;

  // Thread-safe frame queue with backpressure management
  struct Frame {
    std::vector<uint8_t> data;
    int width;
    int height;
    std::chrono::time_point<std::chrono::steady_clock> timestamp;
    uint64_t sequence_number;  // For proper frame ordering
  };

  // Input queue - raw frames from CEF's OnPaint
  std::queue<Frame> frame_queue_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;

  // Output queue - rate-synchronized frames for encoding
  std::queue<Frame> output_queue_;
  std::mutex output_mutex_;
  std::condition_variable output_cv_;

  // Last frame for duplication when no new frames arrive
  Frame last_frame_;
  std::mutex last_frame_mutex_;
  bool has_last_frame_ = false;

  // Frame queue limits to prevent unbounded memory growth
  static constexpr size_t kMaxQueuedFrames = 120;  // ~4 seconds at 30fps (increased buffer)
  static constexpr size_t kDropThreshold = 90;     // Start dropping at 3s backlog
  static constexpr size_t kMaxOutputFrames = 60;   // Output queue limit
  std::atomic<uint64_t> frames_dropped_{0};
  std::atomic<uint64_t> frames_duplicated_{0};     // NEW: Track duplications

  // Encoding error tracking
  std::atomic<bool> encoding_error_{false};

  // Background threads
  std::unique_ptr<std::thread> encoding_thread_;
  std::unique_ptr<std::thread> frame_sync_thread_;  // NEW: Frame rate synchronization

  // Frame timing for synchronization
  std::chrono::steady_clock::time_point recording_start_time_;
  std::atomic<uint64_t> next_output_frame_{0};  // Next frame number to output
  std::atomic<uint64_t> frame_sequence_{0};     // Input frame sequence counter

  // Helper methods
  bool StartFFmpeg();
  void StopFFmpeg();
  void EncodingThreadFunc();
  void FrameSyncThreadFunc();  // NEW: Frame rate synchronization thread
  std::string GenerateVideoPath();
  void ConvertBGRAtoRGB(const uint8_t* bgra, uint8_t* rgb, int width, int height);
};
