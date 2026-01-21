/**
 * Owl Browser HTTP Server - Request Router
 *
 * Routes HTTP requests to appropriate handlers.
 */

#ifndef OWL_HTTP_ROUTER_H
#define OWL_HTTP_ROUTER_H

#include "types.h"
#include "config.h"

/**
 * Initialize the router.
 */
void router_init(const ServerConfig* config);

/**
 * Route and handle an HTTP request.
 *
 * @param request The incoming HTTP request
 * @param response Output: The HTTP response to send
 * @return 0 on success, -1 on error
 */
int router_handle_request(const HttpRequest* request, HttpResponse* response);

/**
 * Shutdown the router.
 */
void router_shutdown(void);

#endif // OWL_HTTP_ROUTER_H
