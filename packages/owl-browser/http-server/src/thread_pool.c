/**
 * Owl Browser HTTP Server - Thread Pool Implementation
 */

#include "thread_pool.h"
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

// Work item
typedef struct WorkItem {
    WorkFunc func;
    void* arg;
    struct WorkItem* next;
} WorkItem;

// Thread pool structure
struct ThreadPool {
    pthread_t* threads;
    int num_threads;

    // Work queue
    WorkItem* queue_head;
    WorkItem* queue_tail;
    int queue_size;
    int max_queue_size;

    // Synchronization
    pthread_mutex_t mutex;
    pthread_cond_t work_cond;
    pthread_cond_t done_cond;

    // State
    volatile bool shutdown;
    volatile int active_count;
    volatile int completed_count;
};

// Worker thread function
static void* worker_thread(void* arg) {
    ThreadPool* pool = (ThreadPool*)arg;

    while (1) {
        pthread_mutex_lock(&pool->mutex);

        // Wait for work or shutdown
        while (!pool->shutdown && !pool->queue_head) {
            pthread_cond_wait(&pool->work_cond, &pool->mutex);
        }

        if (pool->shutdown && !pool->queue_head) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        // Dequeue work item
        WorkItem* item = pool->queue_head;
        if (item) {
            pool->queue_head = item->next;
            if (!pool->queue_head) {
                pool->queue_tail = NULL;
            }
            pool->queue_size--;
            pool->active_count++;
        }

        pthread_mutex_unlock(&pool->mutex);

        // Execute work
        if (item) {
            item->func(item->arg);
            free(item);

            pthread_mutex_lock(&pool->mutex);
            pool->active_count--;
            pool->completed_count++;

            // Signal if all work is done
            if (pool->queue_size == 0 && pool->active_count == 0) {
                pthread_cond_broadcast(&pool->done_cond);
            }
            pthread_mutex_unlock(&pool->mutex);
        }
    }

    return NULL;
}

ThreadPool* thread_pool_create(const ThreadPoolConfig* config) {
    ThreadPool* pool = calloc(1, sizeof(ThreadPool));
    if (!pool) return NULL;

    // Determine number of threads
    int num_threads = 4;  // Default
    int max_queue = 1024; // Default

    if (config) {
        if (config->num_threads > 0) {
            num_threads = config->num_threads;
        } else {
            // Auto: use number of CPU cores
            num_threads = (int)sysconf(_SC_NPROCESSORS_ONLN);
            if (num_threads < 2) num_threads = 2;
            if (num_threads > 64) num_threads = 64;
        }
        if (config->queue_size > 0) {
            max_queue = config->queue_size;
        }
    }

    pool->num_threads = num_threads;
    pool->max_queue_size = max_queue;
    pool->shutdown = false;
    pool->active_count = 0;
    pool->completed_count = 0;

    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->work_cond, NULL);
    pthread_cond_init(&pool->done_cond, NULL);

    // Create threads
    pool->threads = calloc(num_threads, sizeof(pthread_t));
    if (!pool->threads) {
        free(pool);
        return NULL;
    }

    // Set up thread attributes with larger stack size (2MB instead of default 512KB)
    // This prevents stack overflow for functions with large local variables
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    size_t stack_size = 2 * 1024 * 1024;  // 2MB stack
    pthread_attr_setstacksize(&attr, stack_size);

    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->threads[i], &attr, worker_thread, pool) != 0) {
            // Cleanup on failure
            pool->shutdown = true;
            pthread_cond_broadcast(&pool->work_cond);
            for (int j = 0; j < i; j++) {
                pthread_join(pool->threads[j], NULL);
            }
            pthread_attr_destroy(&attr);
            free(pool->threads);
            free(pool);
            return NULL;
        }
    }
    pthread_attr_destroy(&attr);

    return pool;
}

void thread_pool_destroy(ThreadPool* pool) {
    if (!pool) return;

    // Signal shutdown
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->work_cond);
    pthread_mutex_unlock(&pool->mutex);

    // Wait for all threads
    for (int i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    // Free remaining work items
    while (pool->queue_head) {
        WorkItem* item = pool->queue_head;
        pool->queue_head = item->next;
        free(item);
    }

    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->work_cond);
    pthread_cond_destroy(&pool->done_cond);
    free(pool->threads);
    free(pool);
}

int thread_pool_submit(ThreadPool* pool, WorkFunc func, void* arg) {
    if (!pool || !func) return -1;

    WorkItem* item = malloc(sizeof(WorkItem));
    if (!item) return -1;

    item->func = func;
    item->arg = arg;
    item->next = NULL;

    pthread_mutex_lock(&pool->mutex);

    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->mutex);
        free(item);
        return -1;
    }

    if (pool->queue_size >= pool->max_queue_size) {
        pthread_mutex_unlock(&pool->mutex);
        free(item);
        return -1;  // Queue full
    }

    // Enqueue
    if (pool->queue_tail) {
        pool->queue_tail->next = item;
    } else {
        pool->queue_head = item;
    }
    pool->queue_tail = item;
    pool->queue_size++;

    pthread_cond_signal(&pool->work_cond);
    pthread_mutex_unlock(&pool->mutex);

    return 0;
}

int thread_pool_pending(ThreadPool* pool) {
    if (!pool) return 0;

    pthread_mutex_lock(&pool->mutex);
    int count = pool->queue_size;
    pthread_mutex_unlock(&pool->mutex);

    return count;
}

int thread_pool_active(ThreadPool* pool) {
    if (!pool) return 0;

    pthread_mutex_lock(&pool->mutex);
    int count = pool->active_count;
    pthread_mutex_unlock(&pool->mutex);

    return count;
}

void thread_pool_wait(ThreadPool* pool) {
    if (!pool) return;

    pthread_mutex_lock(&pool->mutex);
    while (pool->queue_size > 0 || pool->active_count > 0) {
        pthread_cond_wait(&pool->done_cond, &pool->mutex);
    }
    pthread_mutex_unlock(&pool->mutex);
}

void thread_pool_stats(ThreadPool* pool, ThreadPoolStats* stats) {
    if (!pool || !stats) return;

    pthread_mutex_lock(&pool->mutex);
    stats->num_threads = pool->num_threads;
    stats->active_threads = pool->active_count;
    stats->pending_tasks = pool->queue_size;
    stats->completed_tasks = pool->completed_count;
    pthread_mutex_unlock(&pool->mutex);
}
