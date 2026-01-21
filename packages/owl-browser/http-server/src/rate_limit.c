/**
 * Owl Browser HTTP Server - Rate Limiting Implementation
 *
 * Token bucket rate limiter with per-IP tracking using a hash table.
 */

#include "rate_limit.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

// ============================================================================
// Configuration
// ============================================================================

#define MAX_TRACKED_IPS 10000
#define HASH_TABLE_SIZE 4096
#define CLEANUP_INTERVAL_SEC 60

// ============================================================================
// Data Structures
// ============================================================================

typedef struct RateLimitEntry {
    char ip[64];
    int tokens;                    // current available tokens
    time_t window_start;           // when current window started
    time_t last_request;           // last request time
    struct RateLimitEntry* next;   // for hash collision chaining
} RateLimitEntry;

typedef struct {
    RateLimitConfig config;
    RateLimitEntry* buckets[HASH_TABLE_SIZE];
    int entry_count;
    pthread_mutex_t mutex;
    RateLimitStats stats;
    time_t last_cleanup;
    bool initialized;
} RateLimiter;

static RateLimiter g_limiter = {0};

// ============================================================================
// Hash Function
// ============================================================================

static unsigned int hash_ip(const char* ip) {
    unsigned int hash = 5381;
    int c;
    while ((c = *ip++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % HASH_TABLE_SIZE;
}

// ============================================================================
// Entry Management
// ============================================================================

static RateLimitEntry* find_entry(const char* ip) {
    unsigned int idx = hash_ip(ip);
    RateLimitEntry* entry = g_limiter.buckets[idx];

    while (entry) {
        if (strcmp(entry->ip, ip) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

static RateLimitEntry* create_entry(const char* ip) {
    if (g_limiter.entry_count >= MAX_TRACKED_IPS) {
        return NULL;  // At capacity
    }

    RateLimitEntry* entry = calloc(1, sizeof(RateLimitEntry));
    if (!entry) return NULL;

    strncpy(entry->ip, ip, sizeof(entry->ip) - 1);
    entry->tokens = g_limiter.config.requests_per_window + g_limiter.config.burst_size;
    entry->window_start = time(NULL);
    entry->last_request = entry->window_start;

    // Insert at head of bucket
    unsigned int idx = hash_ip(ip);
    entry->next = g_limiter.buckets[idx];
    g_limiter.buckets[idx] = entry;
    g_limiter.entry_count++;

    return entry;
}

static void refill_tokens(RateLimitEntry* entry, time_t now) {
    // Check if window has passed and refill tokens
    int window_sec = g_limiter.config.window_seconds;
    time_t elapsed = now - entry->window_start;

    if (elapsed >= window_sec) {
        // Calculate how many windows have passed
        int windows_passed = (int)(elapsed / window_sec);
        int max_tokens = g_limiter.config.requests_per_window + g_limiter.config.burst_size;

        // Add tokens for each window that passed
        entry->tokens += windows_passed * g_limiter.config.requests_per_window;
        if (entry->tokens > max_tokens) {
            entry->tokens = max_tokens;
        }

        // Update window start
        entry->window_start = now - (elapsed % window_sec);
    }
}

// ============================================================================
// Public API
// ============================================================================

int rate_limit_init(const RateLimitConfig* config) {
    if (!config) return -1;

    memset(&g_limiter, 0, sizeof(g_limiter));
    memcpy(&g_limiter.config, config, sizeof(RateLimitConfig));

    if (pthread_mutex_init(&g_limiter.mutex, NULL) != 0) {
        return -1;
    }

    g_limiter.last_cleanup = time(NULL);
    g_limiter.initialized = true;

    return 0;
}

bool rate_limit_check(const char* client_ip, RateLimitResult* result) {
    if (!g_limiter.initialized || !g_limiter.config.enabled) {
        if (result) {
            result->allowed = true;
            result->remaining = -1;  // unlimited
            result->limit = -1;
            result->reset_at = 0;
            result->retry_after = 0;
        }
        return true;
    }

    if (!client_ip) {
        if (result) result->allowed = false;
        return false;
    }

    pthread_mutex_lock(&g_limiter.mutex);

    time_t now = time(NULL);
    g_limiter.stats.total_requests++;

    RateLimitEntry* entry = find_entry(client_ip);
    if (!entry) {
        entry = create_entry(client_ip);
        if (!entry) {
            // At capacity, allow request but don't track
            pthread_mutex_unlock(&g_limiter.mutex);
            if (result) {
                result->allowed = true;
                result->remaining = 0;
            }
            return true;
        }
    }

    // Refill tokens based on time elapsed
    refill_tokens(entry, now);

    bool allowed = entry->tokens > 0;

    if (result) {
        result->limit = g_limiter.config.requests_per_window;
        result->reset_at = entry->window_start + g_limiter.config.window_seconds;

        if (allowed) {
            result->allowed = true;
            result->remaining = entry->tokens - 1;  // Will be consumed
            result->retry_after = 0;
            g_limiter.stats.allowed_requests++;
        } else {
            result->allowed = false;
            result->remaining = 0;
            result->retry_after = (int)(result->reset_at - now);
            if (result->retry_after < 1) result->retry_after = 1;
            g_limiter.stats.blocked_requests++;
        }
    }

    pthread_mutex_unlock(&g_limiter.mutex);
    return allowed;
}

void rate_limit_record(const char* client_ip) {
    if (!g_limiter.initialized || !g_limiter.config.enabled || !client_ip) {
        return;
    }

    pthread_mutex_lock(&g_limiter.mutex);

    RateLimitEntry* entry = find_entry(client_ip);
    if (entry && entry->tokens > 0) {
        entry->tokens--;
        entry->last_request = time(NULL);
    }

    pthread_mutex_unlock(&g_limiter.mutex);
}

void rate_limit_status(const char* client_ip, RateLimitResult* result) {
    if (!result) return;

    if (!g_limiter.initialized || !g_limiter.config.enabled) {
        result->allowed = true;
        result->remaining = -1;
        result->limit = -1;
        result->reset_at = 0;
        result->retry_after = 0;
        return;
    }

    pthread_mutex_lock(&g_limiter.mutex);

    time_t now = time(NULL);
    RateLimitEntry* entry = find_entry(client_ip);

    if (!entry) {
        result->allowed = true;
        result->remaining = g_limiter.config.requests_per_window;
        result->limit = g_limiter.config.requests_per_window;
        result->reset_at = now + g_limiter.config.window_seconds;
        result->retry_after = 0;
    } else {
        refill_tokens(entry, now);
        result->allowed = entry->tokens > 0;
        result->remaining = entry->tokens;
        result->limit = g_limiter.config.requests_per_window;
        result->reset_at = entry->window_start + g_limiter.config.window_seconds;
        result->retry_after = result->allowed ? 0 : (int)(result->reset_at - now);
    }

    pthread_mutex_unlock(&g_limiter.mutex);
}

void rate_limit_reset_ip(const char* client_ip) {
    if (!g_limiter.initialized || !client_ip) return;

    pthread_mutex_lock(&g_limiter.mutex);

    unsigned int idx = hash_ip(client_ip);
    RateLimitEntry* entry = g_limiter.buckets[idx];
    RateLimitEntry* prev = NULL;

    while (entry) {
        if (strcmp(entry->ip, client_ip) == 0) {
            // Remove from list
            if (prev) {
                prev->next = entry->next;
            } else {
                g_limiter.buckets[idx] = entry->next;
            }
            free(entry);
            g_limiter.entry_count--;
            break;
        }
        prev = entry;
        entry = entry->next;
    }

    pthread_mutex_unlock(&g_limiter.mutex);
}

void rate_limit_reset_all(void) {
    if (!g_limiter.initialized) return;

    pthread_mutex_lock(&g_limiter.mutex);

    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        RateLimitEntry* entry = g_limiter.buckets[i];
        while (entry) {
            RateLimitEntry* next = entry->next;
            free(entry);
            entry = next;
        }
        g_limiter.buckets[i] = NULL;
    }
    g_limiter.entry_count = 0;

    pthread_mutex_unlock(&g_limiter.mutex);
}

void rate_limit_get_stats(RateLimitStats* stats) {
    if (!stats) return;

    if (!g_limiter.initialized) {
        memset(stats, 0, sizeof(RateLimitStats));
        return;
    }

    pthread_mutex_lock(&g_limiter.mutex);
    memcpy(stats, &g_limiter.stats, sizeof(RateLimitStats));
    stats->tracked_ips = g_limiter.entry_count;
    stats->max_tracked_ips = MAX_TRACKED_IPS;
    pthread_mutex_unlock(&g_limiter.mutex);
}

bool rate_limit_is_enabled(void) {
    return g_limiter.initialized && g_limiter.config.enabled;
}

void rate_limit_cleanup(void) {
    if (!g_limiter.initialized) return;

    time_t now = time(NULL);

    // Only cleanup periodically
    if (now - g_limiter.last_cleanup < CLEANUP_INTERVAL_SEC) {
        return;
    }

    pthread_mutex_lock(&g_limiter.mutex);

    g_limiter.last_cleanup = now;

    // Remove entries that haven't been used in 2x the window time
    time_t expire_threshold = now - (g_limiter.config.window_seconds * 2);

    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        RateLimitEntry* entry = g_limiter.buckets[i];
        RateLimitEntry* prev = NULL;

        while (entry) {
            RateLimitEntry* next = entry->next;

            if (entry->last_request < expire_threshold) {
                // Remove expired entry
                if (prev) {
                    prev->next = next;
                } else {
                    g_limiter.buckets[i] = next;
                }
                free(entry);
                g_limiter.entry_count--;
            } else {
                prev = entry;
            }
            entry = next;
        }
    }

    pthread_mutex_unlock(&g_limiter.mutex);
}

void rate_limit_shutdown(void) {
    if (!g_limiter.initialized) return;

    rate_limit_reset_all();
    pthread_mutex_destroy(&g_limiter.mutex);
    g_limiter.initialized = false;
}
