/**
 * Owl Browser HTTP Server - API Playground
 *
 * Beautiful API playground UI for exploring and testing browser tools.
 * Self-contained HTML/CSS/JS with tool schema translation layer.
 */

#ifndef OWL_HTTP_PLAYGROUND_H
#define OWL_HTTP_PLAYGROUND_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Initialize the playground module.
 */
void playground_init(void);

/**
 * Get the complete HTML for the API playground.
 * Returns static memory - do not free.
 */
const char* playground_get_html(void);

/**
 * Get the HTML content size.
 */
size_t playground_get_html_size(void);

/**
 * Generate full tool schema as JSON for the UI.
 * Includes all tools with full parameter details, categories, and examples.
 * Caller must free the returned string.
 */
char* playground_get_tool_schema(void);

/**
 * Get the logo SVG content.
 * Returns static memory - do not free.
 */
const char* playground_get_logo(void);

/**
 * Get the logo content size.
 */
size_t playground_get_logo_size(void);

/**
 * Shutdown and cleanup.
 */
void playground_shutdown(void);

#endif // OWL_HTTP_PLAYGROUND_H
