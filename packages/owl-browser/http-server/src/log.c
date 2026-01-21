/**
 * Owl Browser HTTP Server - Logging Implementation
 */

#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <string.h>

static LogLevel g_log_level = LOG_LEVEL_INFO;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char* level_names[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR"
};

static const char* level_colors[] = {
    "\033[36m",  // Cyan for DEBUG
    "\033[32m",  // Green for INFO
    "\033[33m",  // Yellow for WARN
    "\033[31m"   // Red for ERROR
};

static const char* reset_color = "\033[0m";

void log_init(bool verbose) {
    g_log_level = verbose ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO;
}

void log_set_level(LogLevel level) {
    g_log_level = level;
}

void log_write(LogLevel level, const char* component, const char* fmt, ...) {
    if (level < g_log_level) {
        return;
    }

    pthread_mutex_lock(&g_log_mutex);

    // Get timestamp
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    // Print log line
    fprintf(stderr, "%s[%s] [%s%-5s%s] [%s] ",
            time_buf,
            time_buf,
            level_colors[level],
            level_names[level],
            reset_color,
            component);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
    fflush(stderr);

    pthread_mutex_unlock(&g_log_mutex);
}

void log_shutdown(void) {
    // Nothing to do for now
}
