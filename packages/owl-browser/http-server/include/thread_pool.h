/**
 * Owl Browser HTTP Server - Thread Pool
 *
 * Simple but efficient thread pool for handling concurrent HTTP requests.
 * Uses a work-stealing queue for load balancing.
 */

#ifndef OWL_HTTP_THREAD_POOL_H
#define OWL_HTTP_THREAD_POOL_H

#include <stdbool.h>
#include <stddef.h>

// Forward declaration
typedef struct ThreadPool ThreadPool;

// Work function type
typedef void (*WorkFunc)(void* arg);

// Thread pool configuration
typedef struct {
    int num_threads;        // Number of worker threads (0 = auto)
    int queue_size;         // Maximum queue size
    bool start_immediately; // Start threads on creation
} ThreadPoolConfig;

/**
 * Create a new thread pool.
 * @param config Configuration (can be NULL for defaults)
 * @return Thread pool handle, or NULL on error
 */
ThreadPool* thread_pool_create(const ThreadPoolConfig* config);

/**
 * Destroy the thread pool.
 * Waits for all pending work to complete.
 */
void thread_pool_destroy(ThreadPool* pool);

/**
 * Submit work to the thread pool.
 * @param pool Thread pool
 * @param func Work function
 * @param arg Argument passed to work function
 * @return 0 on success, -1 if queue is full
 */
int thread_pool_submit(ThreadPool* pool, WorkFunc func, void* arg);

/**
 * Get number of pending work items.
 */
int thread_pool_pending(ThreadPool* pool);

/**
 * Get number of active workers.
 */
int thread_pool_active(ThreadPool* pool);

/**
 * Wait for all pending work to complete.
 */
void thread_pool_wait(ThreadPool* pool);

/**
 * Get thread pool statistics.
 */
typedef struct {
    int num_threads;
    int active_threads;
    int pending_tasks;
    int completed_tasks;
} ThreadPoolStats;

void thread_pool_stats(ThreadPool* pool, ThreadPoolStats* stats);

#endif // OWL_HTTP_THREAD_POOL_H
