/**
 * Shared Frame Buffer Implementation
 *
 * Lock-free ring buffer for cross-process video frame transfer.
 * Uses atomic operations for producer/consumer synchronization.
 */

#include "media/shared_frame_buffer.h"
#include <string.h>
#include <stdio.h>

// Atomic operations for cross-compiler compatibility
#if defined(__GNUC__) || defined(__clang__)
#define atomic_load_relaxed(ptr) __atomic_load_n(ptr, __ATOMIC_RELAXED)
#define atomic_store_relaxed(ptr, val) __atomic_store_n(ptr, val, __ATOMIC_RELAXED)
#define atomic_load_acquire(ptr) __atomic_load_n(ptr, __ATOMIC_ACQUIRE)
#define atomic_store_release(ptr, val) __atomic_store_n(ptr, val, __ATOMIC_RELEASE)
#define atomic_fetch_add(ptr, val) __atomic_fetch_add(ptr, val, __ATOMIC_SEQ_CST)
#define memory_barrier() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#else
#include <stdatomic.h>
#define atomic_load_relaxed(ptr) atomic_load_explicit((_Atomic typeof(*(ptr))*)(ptr), memory_order_relaxed)
#define atomic_store_relaxed(ptr, val) atomic_store_explicit((_Atomic typeof(*(ptr))*)(ptr), val, memory_order_relaxed)
#define atomic_load_acquire(ptr) atomic_load_explicit((_Atomic typeof(*(ptr))*)(ptr), memory_order_acquire)
#define atomic_store_release(ptr, val) atomic_store_explicit((_Atomic typeof(*(ptr))*)(ptr), val, memory_order_release)
#define atomic_fetch_add(ptr, val) atomic_fetch_add_explicit((_Atomic typeof(*(ptr))*)(ptr), val, memory_order_seq_cst)
#define memory_barrier() atomic_thread_fence(memory_order_seq_cst)
#endif

void shm_frame_buffer_init(SharedFrameBuffer* buffer, const char* context_id,
                           int target_fps, int jpeg_quality) {
    if (!buffer) return;

    // Clear everything first
    memset(buffer, 0, sizeof(SharedFrameBuffer));

    // Set header
    buffer->magic = SHM_MAGIC;
    buffer->version = SHM_VERSION;
    buffer->target_fps = target_fps;
    buffer->jpeg_quality = jpeg_quality;
    buffer->active = 1;

    // Copy context ID
    if (context_id) {
        strncpy((char*)buffer->context_id, context_id, sizeof(buffer->context_id) - 1);
    }

    // Initialize slots
    for (int i = 0; i < SHM_FRAME_BUFFER_SLOTS; i++) {
        buffer->slots[i].state = FRAME_SLOT_EMPTY;
        buffer->slots[i].sequence = 0;
    }

    // Memory barrier to ensure all writes are visible
    memory_barrier();
}

bool shm_frame_buffer_write(SharedFrameBuffer* buffer, const uint8_t* jpeg_data,
                            uint32_t data_size, int32_t width, int32_t height,
                            int64_t timestamp_ms) {
    if (!buffer || !jpeg_data || data_size == 0) return false;
    if (data_size > SHM_MAX_FRAME_SIZE) return false;
    if (!atomic_load_relaxed(&buffer->active)) return false;

    // Get next write index (round-robin)
    int write_idx = atomic_load_relaxed(&buffer->write_index);
    int next_idx = (write_idx + 1) % SHM_FRAME_BUFFER_SLOTS;

    SharedFrameSlot* slot = &buffer->slots[write_idx];

    // Check if slot is available (not being read)
    uint32_t expected_state = atomic_load_acquire(&slot->state);
    if (expected_state == FRAME_SLOT_READING) {
        // Slot is being read, try next slot or drop frame
        atomic_fetch_add(&buffer->frames_dropped, 1);
        return false;
    }

    // Mark slot as writing
    atomic_store_release(&slot->state, FRAME_SLOT_WRITING);

    // Get next sequence number
    uint64_t seq = atomic_fetch_add(&buffer->write_sequence, 1) + 1;

    // Write frame data
    memcpy((void*)slot->data, jpeg_data, data_size);
    slot->data_size = data_size;
    slot->width = width;
    slot->height = height;
    slot->timestamp_ms = timestamp_ms;
    slot->sequence = seq;

    // Memory barrier before marking ready
    memory_barrier();

    // Mark slot as ready
    atomic_store_release(&slot->state, FRAME_SLOT_READY);

    // Update write index
    atomic_store_relaxed(&buffer->write_index, next_idx);

    // Update statistics
    atomic_fetch_add(&buffer->frames_written, 1);
    atomic_fetch_add(&buffer->bytes_written, data_size);

    return true;
}

uint64_t shm_frame_buffer_read(SharedFrameBuffer* buffer, uint8_t* out_jpeg_data,
                               uint32_t* out_size, int32_t* out_width,
                               int32_t* out_height, int64_t* out_timestamp,
                               uint64_t last_sequence) {
    if (!buffer || !out_jpeg_data) return 0;
    if (!atomic_load_relaxed(&buffer->active)) return 0;

    // Find the slot with the highest sequence number that's ready
    // NOTE: This is a read-only operation - we don't modify the buffer
    // The producer can overwrite slots; we just read the latest one
    int best_slot = -1;
    uint64_t best_seq = last_sequence;

    for (int i = 0; i < SHM_FRAME_BUFFER_SLOTS; i++) {
        SharedFrameSlot* slot = &buffer->slots[i];

        // Check if slot is ready (not being written)
        uint32_t state = atomic_load_acquire(&slot->state);
        if (state != FRAME_SLOT_READY && state != FRAME_SLOT_EMPTY) {
            continue;  // Slot is being written, skip it
        }

        // Check sequence number
        uint64_t seq = atomic_load_acquire(&slot->sequence);
        if (seq > best_seq && seq > 0) {
            best_seq = seq;
            best_slot = i;
        }
    }

    if (best_slot < 0) {
        return 0;  // No new frame available
    }

    SharedFrameSlot* slot = &buffer->slots[best_slot];

    // Read sequence number first
    uint64_t seq_before = atomic_load_acquire(&slot->sequence);
    if (seq_before <= last_sequence) {
        return 0;  // No new frame
    }

    // Read frame data
    uint32_t size = atomic_load_acquire(&slot->data_size);
    if (size == 0 || size > SHM_MAX_FRAME_SIZE) {
        return 0;  // Invalid size
    }

    // Copy frame data
    memcpy(out_jpeg_data, (void*)slot->data, size);
    *out_size = size;
    *out_width = atomic_load_acquire(&slot->width);
    *out_height = atomic_load_acquire(&slot->height);
    *out_timestamp = atomic_load_acquire(&slot->timestamp_ms);

    // Memory barrier to ensure all reads are complete
    memory_barrier();

    // Re-check sequence to detect if slot was overwritten during read
    uint64_t seq_after = atomic_load_acquire(&slot->sequence);
    if (seq_after != seq_before) {
        // Slot was overwritten during our read - data may be corrupted
        // Return 0 to signal failure, caller should retry
        return 0;
    }

    return seq_before;
}

bool shm_frame_buffer_has_new(SharedFrameBuffer* buffer, uint64_t last_sequence) {
    if (!buffer) return false;
    if (!atomic_load_relaxed(&buffer->active)) return false;

    uint64_t write_seq = atomic_load_acquire(&buffer->write_sequence);
    return write_seq > last_sequence;
}

void shm_frame_buffer_deactivate(SharedFrameBuffer* buffer) {
    if (!buffer) return;
    atomic_store_release(&buffer->active, 0);
    memory_barrier();
}

bool shm_frame_buffer_is_active(SharedFrameBuffer* buffer) {
    if (!buffer) return false;
    return atomic_load_acquire(&buffer->active) != 0 &&
           buffer->magic == SHM_MAGIC;
}

void shm_generate_name(const char* context_id, char* out_name) {
    if (!context_id || !out_name) return;
    snprintf(out_name, 128, "%s%s", SHM_BUFFER_NAME_PREFIX, context_id);
}

void shm_generate_eventfd_path(const char* context_id, char* out_path) {
    if (!context_id || !out_path) return;
    snprintf(out_path, 128, "%s%s", SHM_EVENTFD_NAME_PREFIX, context_id);
}
