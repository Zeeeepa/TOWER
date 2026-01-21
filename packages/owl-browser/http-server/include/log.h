/**
 * Owl Browser HTTP Server - Logging
 *
 * Simple thread-safe logging with levels.
 */

#ifndef OWL_HTTP_LOG_H
#define OWL_HTTP_LOG_H

#include <stdbool.h>

// Log levels
typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;

/**
 * Initialize logging.
 *
 * @param verbose Enable verbose (debug) logging
 */
void log_init(bool verbose);

/**
 * Set log level.
 */
void log_set_level(LogLevel level);

/**
 * Log a message.
 */
void log_write(LogLevel level, const char* component, const char* fmt, ...);

// Convenience macros
#define LOG_DEBUG(component, ...) log_write(LOG_LEVEL_DEBUG, component, __VA_ARGS__)
#define LOG_INFO(component, ...)  log_write(LOG_LEVEL_INFO, component, __VA_ARGS__)
#define LOG_WARN(component, ...)  log_write(LOG_LEVEL_WARN, component, __VA_ARGS__)
#define LOG_ERROR(component, ...) log_write(LOG_LEVEL_ERROR, component, __VA_ARGS__)

/**
 * Shutdown logging.
 */
void log_shutdown(void);

#endif // OWL_HTTP_LOG_H
