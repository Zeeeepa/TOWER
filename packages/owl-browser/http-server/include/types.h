/**
 * Owl Browser HTTP Server - Common Types
 *
 * Shared type definitions used across modules.
 */

#ifndef OWL_HTTP_TYPES_H
#define OWL_HTTP_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

// HTTP Methods
typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_OPTIONS,
    HTTP_UNKNOWN
} HttpMethod;

// HTTP Status Codes
typedef enum {
    HTTP_101_SWITCHING_PROTOCOLS = 101,
    HTTP_200_OK = 200,
    HTTP_201_CREATED = 201,
    HTTP_204_NO_CONTENT = 204,
    HTTP_400_BAD_REQUEST = 400,
    HTTP_401_UNAUTHORIZED = 401,
    HTTP_403_FORBIDDEN = 403,
    HTTP_404_NOT_FOUND = 404,
    HTTP_405_METHOD_NOT_ALLOWED = 405,
    HTTP_408_REQUEST_TIMEOUT = 408,
    HTTP_413_PAYLOAD_TOO_LARGE = 413,
    HTTP_422_UNPROCESSABLE_ENTITY = 422,
    HTTP_429_TOO_MANY_REQUESTS = 429,
    HTTP_500_INTERNAL_ERROR = 500,
    HTTP_502_BAD_GATEWAY = 502,
    HTTP_503_SERVICE_UNAVAILABLE = 503
} HttpStatus;

// HTTP Request
typedef struct {
    HttpMethod method;
    char path[4096];
    char query_string[4096];
    char content_type[256];
    char authorization[512];
    char cookie[1024];  // Cookie header for auth
    size_t content_length;
    char* body;
    size_t body_size;
    char client_ip[64];  // Client IP address for rate limiting/filtering
} HttpRequest;

// HTTP Response
typedef struct {
    HttpStatus status;
    char content_type[256];
    char* body;
    size_t body_size;
    bool owns_body;  // If true, body should be freed
} HttpResponse;

// Tool parameter types
typedef enum {
    PARAM_STRING,
    PARAM_INT,
    PARAM_BOOL,
    PARAM_NUMBER,  // floating point
    PARAM_ENUM
} ParamType;

// Tool parameter definition
typedef struct {
    const char* name;
    ParamType type;
    bool required;
    const char* description;
    const char** enum_values;  // NULL-terminated array for PARAM_ENUM
    int enum_count;
} ToolParam;

// Tool definition
typedef struct {
    const char* name;
    const char* description;
    const ToolParam* params;
    int param_count;
} ToolDef;

// Validation error
typedef struct {
    char field[256];
    char message[1024];
} ValidationError;

// Context state
typedef enum {
    CONTEXT_STATE_ACTIVE,
    CONTEXT_STATE_CLOSING,
    CONTEXT_STATE_CLOSED
} ContextState;

// Browser context
typedef struct {
    char id[64];
    ContextState state;
    int64_t created_at;
    int64_t last_used;
    char current_url[4096];
} BrowserContext;

// Result types for operations
typedef struct {
    bool success;
    char* data;        // JSON string result
    size_t data_size;
    char error[1024];  // Error message if !success
} OperationResult;

// Helper to initialize HttpResponse
static inline void http_response_init(HttpResponse* resp) {
    resp->status = HTTP_200_OK;
    resp->content_type[0] = '\0';
    resp->body = NULL;
    resp->body_size = 0;
    resp->owns_body = false;
}

// Helper to free HttpResponse body if owned
static inline void http_response_free(HttpResponse* resp) {
    if (resp->owns_body && resp->body) {
        free(resp->body);
        resp->body = NULL;
    }
}

// Helper to initialize HttpRequest
static inline void http_request_init(HttpRequest* req) {
    req->method = HTTP_UNKNOWN;
    req->path[0] = '\0';
    req->query_string[0] = '\0';
    req->content_type[0] = '\0';
    req->authorization[0] = '\0';
    req->cookie[0] = '\0';
    req->content_length = 0;
    req->body = NULL;
    req->body_size = 0;
}

// Helper to free HttpRequest body
static inline void http_request_free(HttpRequest* req) {
    if (req->body) {
        free(req->body);
        req->body = NULL;
    }
}

#endif // OWL_HTTP_TYPES_H
