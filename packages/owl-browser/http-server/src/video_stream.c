/**
 * Owl Browser HTTP Server - Live Video Streaming Implementation
 *
 * Provides MJPEG over HTTP and WebSocket binary streaming of browser viewports.
 * Uses shared memory for zero-copy frame transfer from browser (Linux).
 */

#include "video_stream.h"
#include "browser_ipc.h"
#include "browser_ipc_async.h"
#include "shm_frame_reader.h"
#include "auth.h"
#include "log.h"
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>

// MJPEG boundary marker
#define MJPEG_BOUNDARY "--owlboundary"
#define MJPEG_CONTENT_TYPE "multipart/x-mixed-replace; boundary=owlboundary"

// Default configuration
static VideoStreamConfig g_config = {
    .max_clients = 50,
    .frame_timeout_ms = 5000,
    .poll_interval_ms = 50
};

// Per-context stream info (for shared memory readers)
#define MAX_STREAM_CONTEXTS 64

typedef struct {
    char context_id[128];
    char shm_name[128];
    ShmFrameReader* reader;
    bool active;
    volatile bool should_stop;  // Signal for streaming threads to stop
    volatile int streaming_threads;  // Count of active streaming threads
} StreamContext;

// Global state
static struct {
    bool initialized;
    pthread_mutex_t mutex;
    VideoStreamStats stats;
    StreamContext contexts[MAX_STREAM_CONTEXTS];

    // Track stopped streams (to signal threads after context removal)
    char stopped_contexts[MAX_STREAM_CONTEXTS][128];
    int stopped_count;
} g_video = {0};

// Check if a context is in the stopped list
static bool is_context_stopped(const char* context_id) {
    for (int i = 0; i < g_video.stopped_count; i++) {
        if (strcmp(g_video.stopped_contexts[i], context_id) == 0) {
            return true;
        }
    }
    return false;
}

// Mark a context as stopped (add to stopped list)
static void mark_context_stopped(const char* context_id) {
    // Check if already in list
    if (is_context_stopped(context_id)) {
        return;
    }

    // Add to list if space available
    if (g_video.stopped_count < MAX_STREAM_CONTEXTS) {
        strncpy(g_video.stopped_contexts[g_video.stopped_count], context_id,
                sizeof(g_video.stopped_contexts[0]) - 1);
        g_video.stopped_count++;
        LOG_DEBUG("VideoStream", "Marked context %s as stopped (count: %d)",
                  context_id, g_video.stopped_count);
    }
}

// Remove a context from the stopped list (when starting a new stream)
static void unmark_context_stopped(const char* context_id) {
    for (int i = 0; i < g_video.stopped_count; i++) {
        if (strcmp(g_video.stopped_contexts[i], context_id) == 0) {
            // Remove by shifting remaining elements
            for (int j = i; j < g_video.stopped_count - 1; j++) {
                strncpy(g_video.stopped_contexts[j],
                        g_video.stopped_contexts[j + 1],
                        sizeof(g_video.stopped_contexts[0]));
            }
            g_video.stopped_count--;
            LOG_DEBUG("VideoStream", "Unmarked context %s as stopped (count: %d)",
                      context_id, g_video.stopped_count);
            return;
        }
    }
}

// Find or create a stream context
static StreamContext* find_stream_context(const char* context_id, bool create) {
    StreamContext* empty_slot = NULL;

    for (int i = 0; i < MAX_STREAM_CONTEXTS; i++) {
        if (g_video.contexts[i].active &&
            strcmp(g_video.contexts[i].context_id, context_id) == 0) {
            return &g_video.contexts[i];
        }
        if (!g_video.contexts[i].active && !empty_slot) {
            empty_slot = &g_video.contexts[i];
        }
    }

    if (create && empty_slot) {
        memset(empty_slot, 0, sizeof(StreamContext));
        strncpy(empty_slot->context_id, context_id, sizeof(empty_slot->context_id) - 1);
        empty_slot->active = true;
        empty_slot->should_stop = false;
        empty_slot->streaming_threads = 0;
        return empty_slot;
    }

    return NULL;
}

// Check if a stream should stop (called from streaming loop)
// Returns true if the stream should terminate
static bool should_stream_stop(const char* context_id) {
    // Check the stopped list (persists after context removal)
    if (is_context_stopped(context_id)) {
        return true;
    }

    // Check the context's should_stop flag
    StreamContext* ctx = find_stream_context(context_id, false);
    if (ctx && ctx->should_stop) {
        return true;
    }

    return false;
}

// Increment streaming thread count for a context
static void increment_streaming_threads(const char* context_id) {
    StreamContext* ctx = find_stream_context(context_id, false);
    if (ctx) {
        ctx->streaming_threads++;
        LOG_DEBUG("VideoStream", "Streaming threads for %s: %d",
                  context_id, ctx->streaming_threads);
    }
}

// Decrement streaming thread count for a context
static void decrement_streaming_threads(const char* context_id) {
    StreamContext* ctx = find_stream_context(context_id, false);
    if (ctx && ctx->streaming_threads > 0) {
        ctx->streaming_threads--;
        LOG_DEBUG("VideoStream", "Streaming threads for %s: %d",
                  context_id, ctx->streaming_threads);
    }
}

// Remove a stream context
static void remove_stream_context(const char* context_id) {
    for (int i = 0; i < MAX_STREAM_CONTEXTS; i++) {
        if (g_video.contexts[i].active &&
            strcmp(g_video.contexts[i].context_id, context_id) == 0) {
            if (g_video.contexts[i].reader) {
                shm_frame_reader_destroy(g_video.contexts[i].reader);
            }
            memset(&g_video.contexts[i], 0, sizeof(StreamContext));
            break;
        }
    }
}

// Clean up a context's resources after streaming thread exits
// Called by the streaming thread when it's the last one to exit
static void cleanup_stream_context(const char* context_id) {
    pthread_mutex_lock(&g_video.mutex);

    StreamContext* ctx = find_stream_context(context_id, false);
    if (ctx && ctx->streaming_threads == 0 && ctx->should_stop) {
        // Last thread exited, safe to clean up
        if (ctx->reader) {
            ShmFrameReader* reader = ctx->reader;
            ctx->reader = NULL;  // Prevent double-free
            pthread_mutex_unlock(&g_video.mutex);

            shm_frame_reader_destroy(reader);
            LOG_DEBUG("VideoStream", "Cleaned up SHM reader for context %s", context_id);

            // Remove from stopped list
            pthread_mutex_lock(&g_video.mutex);
            unmark_context_stopped(context_id);

            // Mark context as inactive
            ctx->active = false;
        }
    }

    pthread_mutex_unlock(&g_video.mutex);
}

// ============================================================================
// Initialization
// ============================================================================

int video_stream_init(const VideoStreamConfig* config) {
    if (g_video.initialized) return 0;

    if (config) {
        g_config = *config;
    }

    pthread_mutex_init(&g_video.mutex, NULL);
    memset(&g_video.stats, 0, sizeof(g_video.stats));

    g_video.initialized = true;
    LOG_INFO("VideoStream", "Video streaming initialized (max %d clients)",
             g_config.max_clients);
    return 0;
}

void video_stream_shutdown(void) {
    if (!g_video.initialized) return;

    pthread_mutex_destroy(&g_video.mutex);
    g_video.initialized = false;

    LOG_INFO("VideoStream", "Video streaming shutdown");
}

// ============================================================================
// Frame Retrieval via Shared Memory (primary) or IPC (fallback)
// ============================================================================

// Include shared memory buffer definition for SHM_MAX_FRAME_SIZE
#include "media/shared_frame_buffer.h"

// Base64 decoding table for IPC fallback
static const unsigned char base64_decode_table[256] = {
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
    64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
};

// Decode base64 string for IPC fallback
static unsigned char* video_base64_decode(const char* input, size_t* out_len) {
    size_t input_len = strlen(input);
    if (input_len == 0) {
        *out_len = 0;
        return NULL;
    }

    // Calculate output size (3 bytes per 4 input chars, minus padding)
    size_t output_len = (input_len / 4) * 3;
    if (input[input_len - 1] == '=') output_len--;
    if (input_len >= 2 && input[input_len - 2] == '=') output_len--;

    unsigned char* output = malloc(output_len + 1);
    if (!output) return NULL;

    size_t i = 0, j = 0;
    while (i < input_len) {
        unsigned char a = (i < input_len) ? base64_decode_table[(unsigned char)input[i++]] : 0;
        unsigned char b = (i < input_len) ? base64_decode_table[(unsigned char)input[i++]] : 0;
        unsigned char c = (i < input_len) ? base64_decode_table[(unsigned char)input[i++]] : 0;
        unsigned char d = (i < input_len) ? base64_decode_table[(unsigned char)input[i++]] : 0;

        if (a == 64 || b == 64) break;  // Invalid character

        output[j++] = (a << 2) | (b >> 4);
        if (c != 64 && j < output_len) output[j++] = (b << 4) | (c >> 2);
        if (d != 64 && j < output_len) output[j++] = (c << 6) | d;
    }

    *out_len = j;
    output[j] = '\0';
    return output;
}

// Get frame via IPC (fallback for macOS and when shared memory is unavailable)
static bool video_stream_get_frame_ipc(const char* context_id, VideoFrame* frame) {
    char params[256];
    snprintf(params, sizeof(params), "{\"context_id\":\"%s\"}", context_id);

    OperationResult op_result;
    memset(&op_result, 0, sizeof(op_result));

    if (browser_ipc_async_send_sync("getLiveFrame", params, &op_result) != 0) {
        if (op_result.data) free(op_result.data);
        return false;
    }

    if (!op_result.data) {
        return false;
    }

    // Parse JSON response: {"data":"base64...","width":W,"height":H}
    JsonValue* json = json_parse(op_result.data);
    free(op_result.data);

    if (!json || json->type != JSON_OBJECT) {
        if (json) json_free(json);
        return false;
    }

    const char* data_str = json_object_get_string(json, "data");
    int width = (int)json_object_get_number(json, "width", 0);
    int height = (int)json_object_get_number(json, "height", 0);

    if (!data_str || width <= 0 || height <= 0) {
        json_free(json);
        return false;
    }

    // Decode base64 JPEG data
    size_t decoded_len = 0;
    unsigned char* decoded = video_base64_decode(data_str, &decoded_len);
    json_free(json);

    if (!decoded || decoded_len == 0) {
        if (decoded) free(decoded);
        return false;
    }

    frame->data = decoded;
    frame->size = decoded_len;
    frame->width = width;
    frame->height = height;
    frame->timestamp_ms = (int64_t)(time(NULL) * 1000);

    return true;
}

bool video_stream_get_frame(const char* context_id, VideoFrame* frame) {
    if (!g_video.initialized || !context_id || !frame) return false;

    memset(frame, 0, sizeof(*frame));

    // MUST hold mutex for the entire SHM operation to prevent use-after-free
    // when video_stream_stop() removes the context
    pthread_mutex_lock(&g_video.mutex);

    // Check if stream should stop (context was stopped)
    if (is_context_stopped(context_id)) {
        pthread_mutex_unlock(&g_video.mutex);
        return false;
    }

    StreamContext* ctx = find_stream_context(context_id, false);
    if (!ctx || ctx->should_stop) {
        pthread_mutex_unlock(&g_video.mutex);
        return false;
    }

    // Check if we have a shared memory reader
    ShmFrameReader* reader = ctx->reader;
    if (!reader) {
        // No shared memory available (e.g., macOS) - use IPC fallback
        pthread_mutex_unlock(&g_video.mutex);
        return video_stream_get_frame_ipc(context_id, frame);
    }

    // Allocate buffer for JPEG data (do this before releasing mutex is fine)
    uint8_t* data = malloc(SHM_MAX_FRAME_SIZE);
    if (!data) {
        pthread_mutex_unlock(&g_video.mutex);
        LOG_ERROR("VideoStream", "Failed to allocate frame buffer");
        return false;
    }

    uint32_t size = 0;
    int32_t width = 0, height = 0;
    int64_t timestamp = 0;

    // Wait for new frame with short timeout
    // Note: shm_frame_reader_wait uses its own synchronization, safe to call with our mutex held
    if (shm_frame_reader_wait(reader, 100)) {  // 100ms timeout
        if (shm_frame_reader_read(reader, data, &size, &width, &height, &timestamp)) {
            pthread_mutex_unlock(&g_video.mutex);
            frame->data = data;
            frame->size = size;
            frame->width = width;
            frame->height = height;
            frame->timestamp_ms = timestamp;
            return true;
        }
    }

    // No new frame or read failed
    free(data);

    // Check if stream is still active
    bool still_active = shm_frame_reader_is_active(reader);
    pthread_mutex_unlock(&g_video.mutex);

    if (!still_active) {
        LOG_INFO("VideoStream", "Shared memory stream ended for %s", context_id);
        return false;
    }

    // No new frame yet, but stream is active - this is okay for polling
    return false;
}

// ============================================================================
// Stream Control via IPC
// ============================================================================

bool video_stream_start(const char* context_id, int fps, int quality) {
    if (!g_video.initialized || !context_id) return false;

    // Clear any previous stopped state for this context
    pthread_mutex_lock(&g_video.mutex);
    unmark_context_stopped(context_id);

    // Check if stream is already active for this context (avoid redundant IPC)
    StreamContext* existing = find_stream_context(context_id, false);
    if (existing && existing->reader && shm_frame_reader_is_active(existing->reader)) {
        LOG_DEBUG("VideoStream", "Stream already active for context %s, skipping IPC", context_id);
        pthread_mutex_unlock(&g_video.mutex);
        return true;  // Already started
    }
    pthread_mutex_unlock(&g_video.mutex);

    char params[256];
    snprintf(params, sizeof(params),
             "{\"context_id\":\"%s\",\"fps\":%d,\"quality\":%d}",
             context_id, fps, quality);

    OperationResult op_result;
    memset(&op_result, 0, sizeof(op_result));

    // Use async IPC (same as router)
    int ipc_ret = browser_ipc_async_send_sync("startLiveStream", params, &op_result);
    if (ipc_ret != 0) {
        LOG_ERROR("VideoStream", "Failed to start stream for context %s: %s",
                  context_id, op_result.error);
        if (op_result.data) free(op_result.data);
        return false;
    }

    // Parse response to check success and get shm_name
    // Response format: {"success":true,"shm_name":"/owl_stream_ctx_000001","shm_available":true}
    bool success = false;
    char shm_name[128] = {0};
    bool shm_available = false;

    if (op_result.data) {
        JsonValue* json = json_parse(op_result.data);
        if (json) {
            if (json->type == JSON_BOOL) {
                // Direct boolean result (browser returned just true/false)
                success = json->bool_val;
            } else if (json->type == JSON_OBJECT) {
                // Object result - check for "success" field
                JsonValue* success_field = json_object_get(json, "success");
                if (success_field && success_field->type == JSON_BOOL) {
                    success = success_field->bool_val;
                }

                // Get shm_name if available
                const char* shm_name_str = json_object_get_string(json, "shm_name");
                if (shm_name_str) {
                    strncpy(shm_name, shm_name_str, sizeof(shm_name) - 1);
                }

                // Check if shm is available
                JsonValue* shm_avail_field = json_object_get(json, "shm_available");
                if (shm_avail_field && shm_avail_field->type == JSON_BOOL) {
                    shm_available = shm_avail_field->bool_val;
                }
            }
            json_free(json);
        }
        free(op_result.data);
    }

    if (success) {
        pthread_mutex_lock(&g_video.mutex);
        g_video.stats.active_streams++;

        // Create stream context and try to connect to shared memory
        StreamContext* ctx = find_stream_context(context_id, true);
        if (ctx) {
            if (shm_available && shm_name[0] != '\0') {
                strncpy(ctx->shm_name, shm_name, sizeof(ctx->shm_name) - 1);

                // Try to create shared memory reader
                ctx->reader = shm_frame_reader_create(shm_name);
                if (ctx->reader) {
                    LOG_INFO("VideoStream", "Connected to shared memory %s for context %s",
                             shm_name, context_id);
                } else {
                    LOG_WARN("VideoStream", "Failed to connect to shared memory %s, "
                             "falling back to IPC for context %s", shm_name, context_id);
                }
            } else {
                LOG_INFO("VideoStream", "Shared memory not available for context %s, "
                         "using IPC fallback", context_id);
            }
        }

        pthread_mutex_unlock(&g_video.mutex);
        LOG_INFO("VideoStream", "Started stream for context %s @ %d fps", context_id, fps);
    }

    return success;
}

bool video_stream_stop(const char* context_id) {
    if (!g_video.initialized || !context_id) return false;

    LOG_INFO("VideoStream", "Stopping stream for context %s", context_id);

    pthread_mutex_lock(&g_video.mutex);

    // FIRST: Mark this context as stopped - this signals streaming threads to exit
    mark_context_stopped(context_id);

    // Set should_stop flag on the context if it exists
    StreamContext* ctx = find_stream_context(context_id, false);
    int active_threads = 0;
    if (ctx) {
        ctx->should_stop = true;
        active_threads = ctx->streaming_threads;
        LOG_DEBUG("VideoStream", "Set should_stop flag for context %s (threads: %d)",
                  context_id, active_threads);
    }

    // Decrement active streams count now (don't wait)
    if (g_video.stats.active_streams > 0) {
        g_video.stats.active_streams--;
    }

    pthread_mutex_unlock(&g_video.mutex);

    // NOTE: We do NOT wait for streaming threads to exit anymore.
    // They will exit on their own when they see should_stop flag.
    // The SHM reader cleanup will happen in video_stream_cleanup_context()
    // which is called by the streaming thread when it exits.

    if (active_threads > 0) {
        LOG_DEBUG("VideoStream", "Stream stop signaled for %s, %d threads will exit asynchronously",
                  context_id, active_threads);
    }

    // Send stop command to browser (non-blocking - browser will stop writing to SHM)
    char params[256];
    snprintf(params, sizeof(params), "{\"context_id\":\"%s\"}", context_id);

    OperationResult op_result;
    memset(&op_result, 0, sizeof(op_result));

    // Use async IPC - if it fails, that's okay, streaming thread will clean up
    int ipc_result = browser_ipc_async_send_sync("stopLiveStream", params, &op_result);
    if (ipc_result != 0) {
        LOG_WARN("VideoStream", "IPC to stop stream failed for context %s: %s (streaming thread will clean up)",
                  context_id, op_result.error);
        if (op_result.data) free(op_result.data);
        // Return true anyway - the stop was signaled successfully
        LOG_INFO("VideoStream", "Stopped stream for context %s (signaled)", context_id);
        return true;
    }

    // Parse response
    bool success = false;
    if (op_result.data) {
        JsonValue* json = json_parse(op_result.data);
        if (json) {
            if (json->type == JSON_BOOL) {
                success = json->bool_val;
            } else if (json->type == JSON_OBJECT) {
                success = json_object_get_bool(json, "success", false);
            }
            json_free(json);
        }
        free(op_result.data);
    }

    LOG_INFO("VideoStream", "Stopped stream for context %s", context_id);
    return true;  // Return true as long as we signaled the stop
}

// ============================================================================
// Stats and List
// ============================================================================

void video_stream_get_stats(VideoStreamStats* stats) {
    if (!stats) return;

    pthread_mutex_lock(&g_video.mutex);
    memcpy(stats, &g_video.stats, sizeof(VideoStreamStats));
    pthread_mutex_unlock(&g_video.mutex);
}

char* video_stream_list(void) {
    if (!g_video.initialized) {
        return strdup("{\"streams\":[],\"count\":0}");
    }

    // Get list from browser
    OperationResult op_result;
    memset(&op_result, 0, sizeof(op_result));

    if (browser_ipc_send_command("listLiveStreams", "{}", &op_result) != 0) {
        if (op_result.data) free(op_result.data);
        return strdup("{\"streams\":[],\"count\":0,\"error\":\"IPC failed\"}");
    }

    if (op_result.data) {
        // The result.data contains the full response, we can return it directly
        // since browser already formats it as JSON
        char* list = op_result.data;  // Transfer ownership
        return list;
    }

    return strdup("{\"streams\":[],\"count\":0}");
}

// ============================================================================
// HTTP Request Handler
// ============================================================================

// Send HTTP response header
static void send_http_header(int fd, int status, const char* content_type, size_t content_length) {
    char header[512];
    const char* status_text = status == 200 ? "OK" : (status == 404 ? "Not Found" : "Error");

    int len;
    if (content_length > 0) {
        len = snprintf(header, sizeof(header),
            "HTTP/1.1 %d %s\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Cache-Control: no-cache, no-store, must-revalidate\r\n"
            "\r\n",
            status, status_text, content_type, content_length);
    } else {
        // For streaming (no content-length)
        len = snprintf(header, sizeof(header),
            "HTTP/1.1 %d %s\r\n"
            "Content-Type: %s\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Cache-Control: no-cache, no-store, must-revalidate\r\n"
            "\r\n",
            status, status_text, content_type);
    }

    write(fd, header, len);
}

// Send a single MJPEG frame
static int send_mjpeg_frame(int fd, const uint8_t* data, size_t size) {
    char header[256];
    int header_len = snprintf(header, sizeof(header),
        "\r\n" MJPEG_BOUNDARY "\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: %zu\r\n"
        "\r\n",
        size);

    if (write(fd, header, header_len) != header_len) return -1;
    if (write(fd, data, size) != (ssize_t)size) return -1;

    return 0;
}

// Handle single frame request: GET /video/frame/{context_id}
static int handle_frame_request(int fd, const char* context_id) {
    VideoFrame frame;
    if (!video_stream_get_frame(context_id, &frame)) {
        send_http_header(fd, 404, "application/json", 0);
        const char* error = "{\"error\":\"Failed to get frame\"}";
        write(fd, error, strlen(error));
        return 0;
    }

    send_http_header(fd, 200, "image/jpeg", frame.size);
    write(fd, frame.data, frame.size);

    pthread_mutex_lock(&g_video.mutex);
    g_video.stats.total_frames_sent++;
    g_video.stats.total_bytes_sent += frame.size;
    pthread_mutex_unlock(&g_video.mutex);

    free(frame.data);
    return 0;
}

// Handle MJPEG stream request: GET /video/stream/{context_id}
// This blocks until client disconnects or stream is stopped
static int handle_stream_request(int fd, const char* context_id, int fps) {
    // Send MJPEG header FIRST to release browser connection slot
    // This allows other requests to proceed while we set up the stream
    send_http_header(fd, 200, MJPEG_CONTENT_TYPE, 0);

    // Force flush the header to the client
    // TCP_NODELAY should be set, but let's make sure the header is sent
    fsync(fd);

    pthread_mutex_lock(&g_video.mutex);
    g_video.stats.active_clients++;

    // Check if this context was already stopped (race condition)
    if (is_context_stopped(context_id)) {
        LOG_INFO("VideoStream", "Context %s was stopped before stream could start", context_id);
        if (g_video.stats.active_clients > 0) {
            g_video.stats.active_clients--;
        }
        pthread_mutex_unlock(&g_video.mutex);
        return 0;
    }
    pthread_mutex_unlock(&g_video.mutex);

    // Now start live stream on browser side (IPC may block)
    if (!video_stream_start(context_id, fps, 75)) {
        LOG_ERROR("VideoStream", "video_stream_start failed for context %s", context_id);
        // Already sent 200, can't change status. Just close connection.
        pthread_mutex_lock(&g_video.mutex);
        if (g_video.stats.active_clients > 0) {
            g_video.stats.active_clients--;
        }
        pthread_mutex_unlock(&g_video.mutex);
        return 0;
    }

    // Register this streaming thread with the context
    pthread_mutex_lock(&g_video.mutex);
    increment_streaming_threads(context_id);
    pthread_mutex_unlock(&g_video.mutex);

    // Calculate frame interval
    int frame_interval_us = 1000000 / fps;

    // Short delay for first frame to be ready (reduced from 100ms)
    usleep(50000);  // 50ms initial delay

    // Counter for periodic stop checks when no frames arrive
    int no_frame_count = 0;
    const int MAX_NO_FRAME_BEFORE_CHECK = 10;  // Check stop signal every ~10 frames worth of time

    // Stream frames until client disconnects or stream is stopped
    while (1) {
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        // Check if stream should stop (e.g., stopLiveStream was called)
        pthread_mutex_lock(&g_video.mutex);
        bool stop_requested = should_stream_stop(context_id);
        pthread_mutex_unlock(&g_video.mutex);

        if (stop_requested) {
            LOG_INFO("VideoStream", "Stream stop requested for context %s, terminating", context_id);
            break;
        }

        VideoFrame frame;
        if (video_stream_get_frame(context_id, &frame)) {
            no_frame_count = 0;  // Reset counter on successful frame

            if (send_mjpeg_frame(fd, frame.data, frame.size) < 0) {
                free(frame.data);
                break;  // Client disconnected
            }

            pthread_mutex_lock(&g_video.mutex);
            g_video.stats.total_frames_sent++;
            g_video.stats.total_bytes_sent += frame.size;
            pthread_mutex_unlock(&g_video.mutex);

            free(frame.data);
        } else {
            // No frame available - might be because stream was stopped
            no_frame_count++;
            if (no_frame_count >= MAX_NO_FRAME_BEFORE_CHECK) {
                // Do an extra stop check when we haven't received frames for a while
                pthread_mutex_lock(&g_video.mutex);
                stop_requested = should_stream_stop(context_id);
                pthread_mutex_unlock(&g_video.mutex);

                if (stop_requested) {
                    LOG_INFO("VideoStream", "Stream stop detected (no frames) for context %s", context_id);
                    break;
                }
                no_frame_count = 0;  // Reset to avoid constant checking
            }
        }

        // Always sleep to maintain frame rate (prevents tight loop when no frame available)
        clock_gettime(CLOCK_MONOTONIC, &end);
        int64_t elapsed_us = (end.tv_sec - start.tv_sec) * 1000000 +
                             (end.tv_nsec - start.tv_nsec) / 1000;
        int64_t sleep_us = frame_interval_us - elapsed_us;

        // Minimum sleep of 10ms to prevent CPU spin
        if (sleep_us < 10000) {
            sleep_us = 10000;
        }
        usleep((useconds_t)sleep_us);
    }

    // Unregister this streaming thread from the context
    pthread_mutex_lock(&g_video.mutex);
    decrement_streaming_threads(context_id);

    // Check what action to take based on context state
    StreamContext* ctx = find_stream_context(context_id, false);
    bool is_last_thread = (ctx != NULL && ctx->streaming_threads == 0);
    bool was_externally_stopped = (ctx != NULL && ctx->should_stop);

    if (g_video.stats.active_clients > 0) {
        g_video.stats.active_clients--;
    }
    pthread_mutex_unlock(&g_video.mutex);

    if (is_last_thread) {
        if (!was_externally_stopped) {
            // Client disconnected naturally (not via stopLiveStream API)
            // Need to notify browser to stop the stream
            video_stream_stop(context_id);
        }
        // Clean up the SHM reader (safe now since we're the last thread)
        cleanup_stream_context(context_id);
    }

    LOG_INFO("VideoStream", "MJPEG stream ended for context %s", context_id);
    return 0;
}

int video_stream_handle_request(int client_fd, const char* path,
                                 const char* client_ip, const char* auth_header,
                                 const char* cookie_header) {
    if (!g_video.initialized || !path) return -1;

    // Check authentication - first try Authorization header
    bool auth_valid = auth_validate(auth_header);

    // If header auth fails, try token from Cookie header (for img tags that can't set headers)
    // Cookie format: owl_token=<token>; other=value
    if (!auth_valid && cookie_header && strlen(cookie_header) > 0) {
        const char* token_start = strstr(cookie_header, "owl_token=");
        if (token_start) {
            token_start += 10;  // Skip "owl_token="

            // Extract token value (until ; or end of string)
            char token_value[512] = {0};
            size_t i = 0;
            while (token_start[i] && token_start[i] != ';' && token_start[i] != ' ' && i < sizeof(token_value) - 1) {
                token_value[i] = token_start[i];
                i++;
            }

            // Construct Bearer auth string and validate
            if (strlen(token_value) > 0) {
                char bearer_auth[600];
                snprintf(bearer_auth, sizeof(bearer_auth), "Bearer %s", token_value);
                auth_valid = auth_validate(bearer_auth);
            }
        }
    }

    if (!auth_valid) {
        send_http_header(client_fd, 401, "application/json", 0);
        const char* error = "{\"error\":\"Unauthorized\"}";
        write(client_fd, error, strlen(error));
        return 0;  // Handled, but unauthorized
    }

    // Parse path: /video/frame/{context_id} or /video/stream/{context_id}
    if (strncmp(path, "/video/", 7) != 0) {
        return -1;  // Not a video request
    }

    const char* subpath = path + 7;

    // Single frame: /video/frame/{context_id}
    if (strncmp(subpath, "frame/", 6) == 0) {
        const char* context_id = subpath + 6;
        if (strlen(context_id) == 0) {
            send_http_header(client_fd, 400, "application/json", 0);
            const char* error = "{\"error\":\"Missing context_id\"}";
            write(client_fd, error, strlen(error));
            return 0;
        }
        return handle_frame_request(client_fd, context_id);
    }

    // MJPEG stream: /video/stream/{context_id}[?fps=N]
    if (strncmp(subpath, "stream/", 7) == 0) {
        char context_id[128] = {0};
        int fps = 15;  // Default FPS

        // Extract context_id and optional fps
        const char* rest = subpath + 7;
        const char* query = strchr(rest, '?');

        if (query) {
            size_t id_len = query - rest;
            if (id_len >= sizeof(context_id)) id_len = sizeof(context_id) - 1;
            strncpy(context_id, rest, id_len);

            // Parse fps from query
            const char* fps_param = strstr(query, "fps=");
            if (fps_param) {
                fps = atoi(fps_param + 4);
                if (fps < 1) fps = 1;
                if (fps > 60) fps = 60;
            }
        } else {
            strncpy(context_id, rest, sizeof(context_id) - 1);
        }

        if (strlen(context_id) == 0) {
            send_http_header(client_fd, 400, "application/json", 0);
            const char* error = "{\"error\":\"Missing context_id\"}";
            write(client_fd, error, strlen(error));
            return 0;
        }

        return handle_stream_request(client_fd, context_id, fps);
    }

    // List streams: /video/list
    if (strcmp(subpath, "list") == 0) {
        char* list = video_stream_list();
        send_http_header(client_fd, 200, "application/json", strlen(list));
        write(client_fd, list, strlen(list));
        free(list);
        return 0;
    }

    // Stats: /video/stats
    if (strcmp(subpath, "stats") == 0) {
        VideoStreamStats stats;
        video_stream_get_stats(&stats);

        char json[512];
        int len = snprintf(json, sizeof(json),
            "{\"active_streams\":%d,\"active_clients\":%d,"
            "\"total_frames_sent\":%llu,\"total_bytes_sent\":%llu}",
            stats.active_streams, stats.active_clients,
            (unsigned long long)stats.total_frames_sent,
            (unsigned long long)stats.total_bytes_sent);

        send_http_header(client_fd, 200, "application/json", len);
        write(client_fd, json, len);
        return 0;
    }

    return -1;  // Unknown video path
}

// ============================================================================
// WebSocket Video Streaming
// ============================================================================

// Note: WebSocket video streaming is handled via the existing WebSocket
// message handler. Clients send {"method":"subscribeVideo","context_id":"..."}
// and receive binary JPEG frames.

bool video_stream_ws_subscribe(void* ws_conn, const char* context_id) {
    // This would need integration with the WebSocket module
    // For now, MJPEG over HTTP is the primary streaming method
    LOG_INFO("VideoStream", "WebSocket subscribe request for context %s", context_id);
    return true;
}

void video_stream_ws_unsubscribe(void* ws_conn) {
    // Unsubscribe handling
    LOG_INFO("VideoStream", "WebSocket unsubscribe");
}
