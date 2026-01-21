/**
 * Owl Browser HTTP Server - JSON Utilities
 *
 * Lightweight JSON parsing and generation.
 * Uses a simple recursive descent parser for parsing and
 * string building for generation.
 */

#ifndef OWL_HTTP_JSON_H
#define OWL_HTTP_JSON_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// JSON value types
typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

// Forward declaration
typedef struct JsonValue JsonValue;
typedef struct JsonObject JsonObject;
typedef struct JsonArray JsonArray;

// JSON object key-value pair
typedef struct JsonPair {
    char* key;
    JsonValue* value;
    struct JsonPair* next;
} JsonPair;

// JSON object
struct JsonObject {
    JsonPair* pairs;
    int count;
};

// JSON array element
typedef struct JsonElement {
    JsonValue* value;
    struct JsonElement* next;
} JsonElement;

// JSON array
struct JsonArray {
    JsonElement* elements;
    int count;
};

// JSON value
struct JsonValue {
    JsonType type;
    union {
        bool bool_val;
        double number_val;
        char* string_val;
        JsonArray* array_val;
        JsonObject* object_val;
    };
};

// ============================================================================
// Parsing
// ============================================================================

/**
 * Parse a JSON string into a JsonValue.
 * Returns NULL on parse error.
 * Caller must free with json_free().
 */
JsonValue* json_parse(const char* json_str);

/**
 * Free a JsonValue and all nested values.
 */
void json_free(JsonValue* value);

// ============================================================================
// Value Access
// ============================================================================

/**
 * Get a value from a JSON object by key.
 * Returns NULL if not found or value is not an object.
 */
JsonValue* json_object_get(const JsonValue* obj, const char* key);

/**
 * Get a string value from a JSON object by key.
 * Returns NULL if not found or not a string.
 */
const char* json_object_get_string(const JsonValue* obj, const char* key);

/**
 * Get an integer value from a JSON object by key.
 * Returns default_val if not found or not a number.
 */
int64_t json_object_get_int(const JsonValue* obj, const char* key, int64_t default_val);

/**
 * Get a double value from a JSON object by key.
 * Returns default_val if not found or not a number.
 */
double json_object_get_number(const JsonValue* obj, const char* key, double default_val);

/**
 * Get a boolean value from a JSON object by key.
 * Returns default_val if not found or not a boolean.
 */
bool json_object_get_bool(const JsonValue* obj, const char* key, bool default_val);

/**
 * Check if object has a key.
 */
bool json_object_has(const JsonValue* obj, const char* key);

/**
 * Get array length.
 * Returns 0 if not an array.
 */
int json_array_length(const JsonValue* arr);

/**
 * Get array element at index.
 * Returns NULL if out of bounds or not an array.
 */
JsonValue* json_array_get(const JsonValue* arr, int index);

// ============================================================================
// String Builder for JSON Generation
// ============================================================================

typedef struct {
    char* buffer;
    size_t size;
    size_t capacity;
} JsonBuilder;

/**
 * Initialize a JSON builder.
 */
void json_builder_init(JsonBuilder* builder);

/**
 * Free a JSON builder's resources.
 */
void json_builder_free(JsonBuilder* builder);

/**
 * Get the built string (transfers ownership, builder is reset).
 */
char* json_builder_finish(JsonBuilder* builder);

/**
 * Append raw string.
 */
void json_builder_append(JsonBuilder* builder, const char* str);

/**
 * Append formatted string.
 */
void json_builder_appendf(JsonBuilder* builder, const char* fmt, ...);

/**
 * Start an object: {
 */
void json_builder_object_start(JsonBuilder* builder);

/**
 * End an object: }
 */
void json_builder_object_end(JsonBuilder* builder);

/**
 * Start an array: [
 */
void json_builder_array_start(JsonBuilder* builder);

/**
 * End an array: ]
 */
void json_builder_array_end(JsonBuilder* builder);

/**
 * Add object key: "key":
 */
void json_builder_key(JsonBuilder* builder, const char* key);

/**
 * Add string value with proper escaping.
 */
void json_builder_string(JsonBuilder* builder, const char* value);

/**
 * Add integer value.
 */
void json_builder_int(JsonBuilder* builder, int64_t value);

/**
 * Add double value.
 */
void json_builder_number(JsonBuilder* builder, double value);

/**
 * Add boolean value.
 */
void json_builder_bool(JsonBuilder* builder, bool value);

/**
 * Add null value.
 */
void json_builder_null(JsonBuilder* builder);

/**
 * Add raw JSON (already formatted).
 */
void json_builder_raw(JsonBuilder* builder, const char* json);

/**
 * Add comma separator if needed.
 */
void json_builder_comma(JsonBuilder* builder);

// ============================================================================
// Convenience Functions
// ============================================================================

/**
 * Escape a string for JSON.
 * Caller must free the result.
 */
char* json_escape_string(const char* str);

/**
 * Build a simple error response JSON.
 * Caller must free the result.
 */
char* json_error_response(const char* error_message);

/**
 * Build a simple success response with string result.
 * Caller must free the result.
 */
char* json_success_response(const char* result);

/**
 * Build a success response with raw JSON result.
 * Caller must free the result.
 */
char* json_success_response_raw(const char* raw_json);

#endif // OWL_HTTP_JSON_H
