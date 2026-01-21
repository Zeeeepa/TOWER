/**
 * Owl Browser HTTP Server - Live Video Streaming
 *
 * Provides live video streaming of browser viewports via:
 * 1. MJPEG over HTTP (multipart/x-mixed-replace) - natively supported by browsers
 * 2. WebSocket binary frames - for custom clients
 * 3. Single frame GET endpoint - for polling/snapshot
 *
 * Usage:
 * - GET /video/frame/{context_id} - Get latest frame as JPEG
 * - GET /video/stream/{context_id} - MJPEG stream (multipart/x-mixed-replace)
 * - WS /ws with method "subscribeVideo" - WebSocket binary frames
 */

#ifndef VIDEO_STREAM_H
#define VIDEO_STREAM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Forward declarations
typedef struct VideoStreamManager VideoStreamManager;
typedef struct VideoStreamClient VideoStreamClient;

// Configuration
typedef struct {
    int max_clients;           // Max streaming clients (default: 50)
    int frame_timeout_ms;      // Timeout for frame retrieval (default: 5000)
    int poll_interval_ms;      // How often to poll for new frames (default: 50)
} VideoStreamConfig;

// Frame data
typedef struct {
    uint8_t* data;
    size_t size;
    int width;
    int height;
    int64_t timestamp_ms;
} VideoFrame;

// Client info for monitoring
typedef struct {
    const char* client_ip;
    const char* context_id;
    int64_t connected_at;
    uint64_t frames_sent;
    uint64_t bytes_sent;
    bool is_mjpeg;  // true = MJPEG, false = WebSocket
} VideoClientInfo;

// Initialize video streaming
int video_stream_init(const VideoStreamConfig* config);

// Shutdown video streaming
void video_stream_shutdown(void);

// Get the latest frame for a context (from browser IPC)
// Caller must free frame->data when done
bool video_stream_get_frame(const char* context_id, VideoFrame* frame);

// Start/stop live stream for a context (sends IPC to browser)
bool video_stream_start(const char* context_id, int fps, int quality);
bool video_stream_stop(const char* context_id);

// Get stream stats
typedef struct {
    int active_streams;
    int active_clients;
    uint64_t total_frames_sent;
    uint64_t total_bytes_sent;
} VideoStreamStats;
void video_stream_get_stats(VideoStreamStats* stats);

// List active streams
// Returns JSON: {"streams": [...], "count": N}
char* video_stream_list(void);

// HTTP handler for video endpoints
// Returns 0 if handled, -1 if not a video request
// Note: For MJPEG streaming, this function blocks until client disconnects
int video_stream_handle_request(int client_fd, const char* path,
                                 const char* client_ip, const char* auth_header,
                                 const char* cookie_header);

// WebSocket handlers for video streaming
// Called from websocket message handler
bool video_stream_ws_subscribe(void* ws_conn, const char* context_id);
void video_stream_ws_unsubscribe(void* ws_conn);

#endif // VIDEO_STREAM_H
