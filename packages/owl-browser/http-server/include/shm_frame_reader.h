/**
 * Shared Memory Frame Reader for HTTP Server
 *
 * Opens and reads video frames from shared memory created by the browser.
 * Provides zero-copy frame access for real-time MJPEG streaming.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration (actual struct defined in shared_frame_buffer.h)
struct SharedFrameBuffer;

// Opaque handle for the reader
typedef struct ShmFrameReader ShmFrameReader;

/**
 * Create a shared memory frame reader
 * @param shm_name POSIX shared memory name (e.g., "/owl_stream_ctx_000001")
 * @return Reader handle or NULL on failure
 */
ShmFrameReader* shm_frame_reader_create(const char* shm_name);

/**
 * Destroy a shared memory frame reader
 * @param reader Reader handle
 */
void shm_frame_reader_destroy(ShmFrameReader* reader);

/**
 * Check if a new frame is available
 * @param reader Reader handle
 * @return true if new frame available
 */
bool shm_frame_reader_has_new(ShmFrameReader* reader);

/**
 * Read the latest frame
 * @param reader Reader handle
 * @param out_data Output buffer for JPEG data (must be large enough)
 * @param out_size Output: actual size of JPEG data
 * @param out_width Output: frame width
 * @param out_height Output: frame height
 * @param out_timestamp Output: frame timestamp in milliseconds
 * @return true if frame read successfully, false otherwise
 */
bool shm_frame_reader_read(ShmFrameReader* reader, uint8_t* out_data,
                            uint32_t* out_size, int32_t* out_width,
                            int32_t* out_height, int64_t* out_timestamp);

/**
 * Wait for a new frame with timeout
 * @param reader Reader handle
 * @param timeout_ms Timeout in milliseconds (-1 for infinite)
 * @return true if new frame available, false on timeout
 */
bool shm_frame_reader_wait(ShmFrameReader* reader, int timeout_ms);

/**
 * Check if the shared memory stream is still active
 * @param reader Reader handle
 * @return true if stream is active
 */
bool shm_frame_reader_is_active(ShmFrameReader* reader);

/**
 * Get statistics
 * @param reader Reader handle
 * @param out_frames_read Output: total frames read
 * @param out_frames_missed Output: frames missed (overwritten before read)
 */
void shm_frame_reader_stats(ShmFrameReader* reader, uint64_t* out_frames_read,
                             uint64_t* out_frames_missed);

#ifdef __cplusplus
}
#endif
