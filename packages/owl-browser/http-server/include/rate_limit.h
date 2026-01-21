/**
 * Owl Browser HTTP Server - Rate Limiting
 *
 * Token bucket rate limiter with per-IP tracking.
 * Supports configurable requests per window with burst allowance.
 */

#ifndef OWL_HTTP_RATE_LIMIT_H
#define OWL_HTTP_RATE_LIMIT_H

#include "config.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/**
 * Rate limit check result
 */
typedef struct {
    bool allowed;           // true if request is allowed
    int remaining;          // requests remaining in current window
    int limit;              // maximum requests per window
    int64_t reset_at;       // Unix timestamp when window resets
    int retry_after;        // seconds to wait (if not allowed)
} RateLimitResult;

/**
 * Initialize the rate limiter.
 *
 * @param config Rate limiting configuration
 * @return 0 on success, -1 on error
 */
int rate_limit_init(const RateLimitConfig* config);

/**
 * Check if a request from the given IP should be allowed.
 *
 * @param client_ip The client's IP address
 * @param result Output: Rate limit check result
 * @return true if request is allowed, false if rate limited
 */
bool rate_limit_check(const char* client_ip, RateLimitResult* result);

/**
 * Record a request from the given IP (after it's been processed).
 * Call this after rate_limit_check returns true and the request completes.
 *
 * @param client_ip The client's IP address
 */
void rate_limit_record(const char* client_ip);

/**
 * Get current rate limit status for an IP without consuming a request.
 *
 * @param client_ip The client's IP address
 * @param result Output: Current rate limit status
 */
void rate_limit_status(const char* client_ip, RateLimitResult* result);

/**
 * Reset rate limit for a specific IP.
 *
 * @param client_ip The client's IP address
 */
void rate_limit_reset_ip(const char* client_ip);

/**
 * Reset all rate limits.
 */
void rate_limit_reset_all(void);

/**
 * Get statistics about rate limiting.
 */
typedef struct {
    uint64_t total_requests;
    uint64_t allowed_requests;
    uint64_t blocked_requests;
    int tracked_ips;
    int max_tracked_ips;
} RateLimitStats;

void rate_limit_get_stats(RateLimitStats* stats);

/**
 * Check if rate limiting is enabled.
 */
bool rate_limit_is_enabled(void);

/**
 * Cleanup expired entries (call periodically).
 */
void rate_limit_cleanup(void);

/**
 * Shutdown the rate limiter and free resources.
 */
void rate_limit_shutdown(void);

#endif // OWL_HTTP_RATE_LIMIT_H
