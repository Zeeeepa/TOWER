/**
 * Shared Frame Buffer for Real-Time Video Streaming
 *
 * This header defines the shared memory layout used for zero-copy
 * frame transfer between the browser process and HTTP server.
 *
 * Architecture:
 * - Ring buffer with N slots for frames
 * - Lock-free producer/consumer using atomic sequence numbers
 * - eventfd for signaling new frames (Linux only)
 * - POSIX shared memory for cross-process access
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Configuration
#define SHM_FRAME_BUFFER_SLOTS 4        // Number of frame slots in ring buffer
#define SHM_MAX_FRAME_SIZE (4 * 1024 * 1024)  // 4MB max per frame (JPEG compressed)
#define SHM_BUFFER_NAME_PREFIX "/owl_stream_"
#define SHM_EVENTFD_NAME_PREFIX "/tmp/owl_stream_event_"

// Magic number for validation
#define SHM_MAGIC 0x4F574C53  // "OWLS"
#define SHM_VERSION 1

// Frame slot state (for lock-free operation)
typedef enum {
    FRAME_SLOT_EMPTY = 0,      // Slot is available for writing
    FRAME_SLOT_WRITING = 1,    // Producer is writing to this slot
    FRAME_SLOT_READY = 2,      // Frame is ready for reading
    FRAME_SLOT_READING = 3     // Consumer is reading this slot
} FrameSlotState;

// Single frame slot in the ring buffer
typedef struct {
    volatile uint32_t state;       // FrameSlotState
    volatile uint64_t sequence;    // Monotonic sequence number
    volatile int64_t timestamp_ms; // Frame timestamp
    volatile uint32_t data_size;   // Actual JPEG data size
    volatile int32_t width;        // Frame width
    volatile int32_t height;       // Frame height
    uint8_t data[SHM_MAX_FRAME_SIZE];  // JPEG frame data
} __attribute__((aligned(64))) SharedFrameSlot;

// Shared memory header
typedef struct SharedFrameBuffer {
    // Header validation
    volatile uint32_t magic;           // SHM_MAGIC
    volatile uint32_t version;         // SHM_VERSION

    // Stream configuration
    volatile int32_t target_fps;
    volatile int32_t jpeg_quality;
    char context_id[64];               // Browser context ID

    // Ring buffer state
    volatile uint64_t write_sequence;  // Next sequence number to write
    volatile uint64_t read_sequence;   // Last sequence number read
    volatile int32_t write_index;      // Current write slot index
    volatile int32_t active;           // Stream is active

    // Statistics
    volatile uint64_t frames_written;
    volatile uint64_t frames_dropped;
    volatile uint64_t bytes_written;

    // Padding to align frame slots
    uint8_t reserved[64];

    // Frame slots follow
    SharedFrameSlot slots[SHM_FRAME_BUFFER_SLOTS];
} __attribute__((aligned(64))) SharedFrameBuffer;

// Calculate total shared memory size
#define SHM_BUFFER_TOTAL_SIZE sizeof(SharedFrameBuffer)

// Helper functions (implemented in shared_frame_buffer.c)

/**
 * Initialize a shared frame buffer (producer side)
 * @param buffer Pointer to mapped shared memory
 * @param context_id Browser context ID
 * @param target_fps Target frame rate
 * @param jpeg_quality JPEG quality (1-100)
 */
void shm_frame_buffer_init(SharedFrameBuffer* buffer, const char* context_id,
                           int target_fps, int jpeg_quality);

/**
 * Write a frame to the buffer (producer side)
 * @param buffer Pointer to shared frame buffer
 * @param jpeg_data JPEG encoded frame data
 * @param data_size Size of JPEG data
 * @param width Frame width
 * @param height Frame height
 * @param timestamp_ms Frame timestamp
 * @return true if frame was written, false if buffer is full
 */
bool shm_frame_buffer_write(SharedFrameBuffer* buffer, const uint8_t* jpeg_data,
                            uint32_t data_size, int32_t width, int32_t height,
                            int64_t timestamp_ms);

/**
 * Read the latest frame from the buffer (consumer side)
 * @param buffer Pointer to shared frame buffer
 * @param out_jpeg_data Output buffer for JPEG data (must be SHM_MAX_FRAME_SIZE)
 * @param out_size Output: actual JPEG data size
 * @param out_width Output: frame width
 * @param out_height Output: frame height
 * @param out_timestamp Output: frame timestamp
 * @param last_sequence Last sequence number read (for detecting new frames)
 * @return New sequence number if frame read, 0 if no new frame
 */
uint64_t shm_frame_buffer_read(SharedFrameBuffer* buffer, uint8_t* out_jpeg_data,
                               uint32_t* out_size, int32_t* out_width,
                               int32_t* out_height, int64_t* out_timestamp,
                               uint64_t last_sequence);

/**
 * Check if buffer has new frames available
 * @param buffer Pointer to shared frame buffer
 * @param last_sequence Last sequence number read
 * @return true if new frames available
 */
bool shm_frame_buffer_has_new(SharedFrameBuffer* buffer, uint64_t last_sequence);

/**
 * Mark buffer as inactive (producer shutting down)
 */
void shm_frame_buffer_deactivate(SharedFrameBuffer* buffer);

/**
 * Check if buffer is active
 */
bool shm_frame_buffer_is_active(SharedFrameBuffer* buffer);

/**
 * Generate shared memory name for a context
 * @param context_id Browser context ID
 * @param out_name Output buffer (at least 128 bytes)
 */
void shm_generate_name(const char* context_id, char* out_name);

/**
 * Generate eventfd path for a context
 * @param context_id Browser context ID
 * @param out_path Output buffer (at least 128 bytes)
 */
void shm_generate_eventfd_path(const char* context_id, char* out_path);

#ifdef __cplusplus
}
#endif
