/**
 * Shared Memory Frame Reader Implementation
 *
 * Opens shared memory created by the browser and reads frames directly.
 */

#include "shm_frame_reader.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>

// Include shared memory buffer definition
#include "media/shared_frame_buffer.h"

struct ShmFrameReader {
    char shm_name[128];
    int shm_fd;
    SharedFrameBuffer* buffer;
    uint64_t last_sequence;
    uint64_t frames_read;
    uint64_t frames_missed;
};

ShmFrameReader* shm_frame_reader_create(const char* shm_name) {
    if (!shm_name) return NULL;

    ShmFrameReader* reader = calloc(1, sizeof(ShmFrameReader));
    if (!reader) return NULL;

    strncpy(reader->shm_name, shm_name, sizeof(reader->shm_name) - 1);
    reader->shm_fd = -1;
    reader->last_sequence = 0;

    // Open shared memory
    reader->shm_fd = shm_open(shm_name, O_RDONLY, 0);
    if (reader->shm_fd < 0) {
        LOG_ERROR("ShmFrameReader", "Failed to open shared memory %s: %s",
                  shm_name, strerror(errno));
        free(reader);
        return NULL;
    }

    // Map memory
    void* ptr = mmap(NULL, SHM_BUFFER_TOTAL_SIZE, PROT_READ, MAP_SHARED,
                     reader->shm_fd, 0);
    if (ptr == MAP_FAILED) {
        LOG_ERROR("ShmFrameReader", "Failed to mmap shared memory: %s",
                  strerror(errno));
        close(reader->shm_fd);
        free(reader);
        return NULL;
    }

    reader->buffer = (SharedFrameBuffer*)ptr;

    // Validate magic number
    if (reader->buffer->magic != SHM_MAGIC) {
        LOG_ERROR("ShmFrameReader", "Invalid shared memory magic: 0x%08x (expected 0x%08x)",
                  reader->buffer->magic, SHM_MAGIC);
        munmap(reader->buffer, SHM_BUFFER_TOTAL_SIZE);
        close(reader->shm_fd);
        free(reader);
        return NULL;
    }

    LOG_INFO("ShmFrameReader", "Connected to shared memory: %s (context: %s)",
             shm_name, reader->buffer->context_id);

    return reader;
}

void shm_frame_reader_destroy(ShmFrameReader* reader) {
    if (!reader) return;

    if (reader->buffer) {
        munmap(reader->buffer, SHM_BUFFER_TOTAL_SIZE);
    }
    if (reader->shm_fd >= 0) {
        close(reader->shm_fd);
    }

    LOG_INFO("ShmFrameReader", "Disconnected from shared memory: %s "
             "(read: %llu, missed: %llu)",
             reader->shm_name,
             (unsigned long long)reader->frames_read,
             (unsigned long long)reader->frames_missed);

    free(reader);
}

bool shm_frame_reader_has_new(ShmFrameReader* reader) {
    if (!reader || !reader->buffer) return false;
    return shm_frame_buffer_has_new(reader->buffer, reader->last_sequence);
}

bool shm_frame_reader_read(ShmFrameReader* reader, uint8_t* out_data,
                            uint32_t* out_size, int32_t* out_width,
                            int32_t* out_height, int64_t* out_timestamp) {
    if (!reader || !reader->buffer || !out_data) return false;

    uint64_t new_seq = shm_frame_buffer_read(reader->buffer, out_data,
                                              out_size, out_width, out_height,
                                              out_timestamp, reader->last_sequence);

    if (new_seq == 0) {
        return false;  // No new frame
    }

    // Check for missed frames
    if (new_seq > reader->last_sequence + 1) {
        uint64_t missed = new_seq - reader->last_sequence - 1;
        reader->frames_missed += missed;
    }

    reader->last_sequence = new_seq;
    reader->frames_read++;

    return true;
}

bool shm_frame_reader_wait(ShmFrameReader* reader, int timeout_ms) {
    if (!reader) return false;

    // Simple polling implementation
    // A better implementation would use eventfd, but for simplicity we poll
    int elapsed = 0;
    int sleep_interval = 5;  // 5ms

    while (elapsed < timeout_ms || timeout_ms < 0) {
        if (shm_frame_reader_has_new(reader)) {
            return true;
        }

        if (!shm_frame_reader_is_active(reader)) {
            return false;  // Stream stopped
        }

        usleep(sleep_interval * 1000);
        elapsed += sleep_interval;
    }

    return false;  // Timeout
}

bool shm_frame_reader_is_active(ShmFrameReader* reader) {
    if (!reader || !reader->buffer) return false;
    return shm_frame_buffer_is_active(reader->buffer);
}

void shm_frame_reader_stats(ShmFrameReader* reader, uint64_t* out_frames_read,
                             uint64_t* out_frames_missed) {
    if (!reader) return;

    if (out_frames_read) *out_frames_read = reader->frames_read;
    if (out_frames_missed) *out_frames_missed = reader->frames_missed;
}
