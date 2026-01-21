/**
 * Owl Browser HTTP Server - JSON Implementation
 *
 * Lightweight JSON parser and builder.
 */

#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

// ============================================================================
// Parser State
// ============================================================================

typedef struct {
    const char* json;
    size_t pos;
    size_t len;
    char error[256];
} ParseContext;

// Forward declarations
static JsonValue* parse_value(ParseContext* ctx);
static void skip_whitespace(ParseContext* ctx);

// ============================================================================
// Memory Management
// ============================================================================

static JsonValue* json_value_new(JsonType type) {
    JsonValue* v = calloc(1, sizeof(JsonValue));
    if (v) v->type = type;
    return v;
}

void json_free(JsonValue* value) {
    if (!value) return;

    switch (value->type) {
        case JSON_STRING:
            free(value->string_val);
            break;
        case JSON_ARRAY:
            if (value->array_val) {
                JsonElement* elem = value->array_val->elements;
                while (elem) {
                    JsonElement* next = elem->next;
                    json_free(elem->value);
                    free(elem);
                    elem = next;
                }
                free(value->array_val);
            }
            break;
        case JSON_OBJECT:
            if (value->object_val) {
                JsonPair* pair = value->object_val->pairs;
                while (pair) {
                    JsonPair* next = pair->next;
                    free(pair->key);
                    json_free(pair->value);
                    free(pair);
                    pair = next;
                }
                free(value->object_val);
            }
            break;
        default:
            break;
    }
    free(value);
}

// ============================================================================
// Parser Helpers
// ============================================================================

static void skip_whitespace(ParseContext* ctx) {
    while (ctx->pos < ctx->len && isspace(ctx->json[ctx->pos])) {
        ctx->pos++;
    }
}

static char peek(ParseContext* ctx) {
    skip_whitespace(ctx);
    return ctx->pos < ctx->len ? ctx->json[ctx->pos] : '\0';
}

static char consume(ParseContext* ctx) {
    skip_whitespace(ctx);
    return ctx->pos < ctx->len ? ctx->json[ctx->pos++] : '\0';
}

static bool match(ParseContext* ctx, const char* str) {
    skip_whitespace(ctx);
    size_t len = strlen(str);
    if (ctx->pos + len > ctx->len) return false;
    if (strncmp(ctx->json + ctx->pos, str, len) == 0) {
        ctx->pos += len;
        return true;
    }
    return false;
}

// ============================================================================
// Parse Functions
// ============================================================================

static JsonValue* parse_null(ParseContext* ctx) {
    if (match(ctx, "null")) {
        return json_value_new(JSON_NULL);
    }
    return NULL;
}

static JsonValue* parse_bool(ParseContext* ctx) {
    if (match(ctx, "true")) {
        JsonValue* v = json_value_new(JSON_BOOL);
        if (v) v->bool_val = true;
        return v;
    }
    if (match(ctx, "false")) {
        JsonValue* v = json_value_new(JSON_BOOL);
        if (v) v->bool_val = false;
        return v;
    }
    return NULL;
}

static JsonValue* parse_number(ParseContext* ctx) {
    skip_whitespace(ctx);

    const char* start = ctx->json + ctx->pos;
    char* end;
    double num = strtod(start, &end);

    if (end == start) return NULL;

    ctx->pos += (end - start);

    JsonValue* v = json_value_new(JSON_NUMBER);
    if (v) v->number_val = num;
    return v;
}

static char* parse_string_content(ParseContext* ctx) {
    if (consume(ctx) != '"') return NULL;

    size_t start = ctx->pos;
    size_t capacity = 256;
    char* result = malloc(capacity);
    if (!result) return NULL;
    size_t len = 0;

    while (ctx->pos < ctx->len) {
        char c = ctx->json[ctx->pos++];

        if (c == '"') {
            result[len] = '\0';
            return result;
        }

        if (c == '\\' && ctx->pos < ctx->len) {
            char next = ctx->json[ctx->pos++];
            switch (next) {
                case '"':  c = '"'; break;
                case '\\': c = '\\'; break;
                case '/':  c = '/'; break;
                case 'b':  c = '\b'; break;
                case 'f':  c = '\f'; break;
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                case 'u': {
                    // Unicode escape - simplified handling
                    if (ctx->pos + 4 <= ctx->len) {
                        char hex[5] = {0};
                        memcpy(hex, ctx->json + ctx->pos, 4);
                        ctx->pos += 4;
                        unsigned int code = strtoul(hex, NULL, 16);
                        if (code < 128) {
                            c = (char)code;
                        } else {
                            c = '?';  // Simplified: replace non-ASCII
                        }
                    }
                    break;
                }
                default:
                    c = next;
            }
        }

        // Grow buffer if needed
        if (len + 1 >= capacity) {
            capacity *= 2;
            char* new_result = realloc(result, capacity);
            if (!new_result) {
                free(result);
                return NULL;
            }
            result = new_result;
        }

        result[len++] = c;
    }

    free(result);
    return NULL;  // Unterminated string
}

static JsonValue* parse_string(ParseContext* ctx) {
    skip_whitespace(ctx);
    if (peek(ctx) != '"') return NULL;

    char* str = parse_string_content(ctx);
    if (!str) return NULL;

    JsonValue* v = json_value_new(JSON_STRING);
    if (!v) {
        free(str);
        return NULL;
    }
    v->string_val = str;
    return v;
}

static JsonValue* parse_array(ParseContext* ctx) {
    if (consume(ctx) != '[') return NULL;

    JsonValue* v = json_value_new(JSON_ARRAY);
    if (!v) return NULL;

    v->array_val = calloc(1, sizeof(JsonArray));
    if (!v->array_val) {
        free(v);
        return NULL;
    }

    JsonElement* last = NULL;

    skip_whitespace(ctx);
    if (peek(ctx) == ']') {
        consume(ctx);
        return v;
    }

    while (1) {
        JsonValue* elem_val = parse_value(ctx);
        if (!elem_val) {
            json_free(v);
            return NULL;
        }

        JsonElement* elem = calloc(1, sizeof(JsonElement));
        if (!elem) {
            json_free(elem_val);
            json_free(v);
            return NULL;
        }
        elem->value = elem_val;

        if (last) {
            last->next = elem;
        } else {
            v->array_val->elements = elem;
        }
        last = elem;
        v->array_val->count++;

        skip_whitespace(ctx);
        char c = peek(ctx);
        if (c == ']') {
            consume(ctx);
            return v;
        }
        if (c != ',') {
            json_free(v);
            return NULL;
        }
        consume(ctx);
    }
}

static JsonValue* parse_object(ParseContext* ctx) {
    if (consume(ctx) != '{') return NULL;

    JsonValue* v = json_value_new(JSON_OBJECT);
    if (!v) return NULL;

    v->object_val = calloc(1, sizeof(JsonObject));
    if (!v->object_val) {
        free(v);
        return NULL;
    }

    JsonPair* last = NULL;

    skip_whitespace(ctx);
    if (peek(ctx) == '}') {
        consume(ctx);
        return v;
    }

    while (1) {
        skip_whitespace(ctx);
        char* key = parse_string_content(ctx);
        if (!key) {
            json_free(v);
            return NULL;
        }

        skip_whitespace(ctx);
        if (consume(ctx) != ':') {
            free(key);
            json_free(v);
            return NULL;
        }

        JsonValue* val = parse_value(ctx);
        if (!val) {
            free(key);
            json_free(v);
            return NULL;
        }

        JsonPair* pair = calloc(1, sizeof(JsonPair));
        if (!pair) {
            free(key);
            json_free(val);
            json_free(v);
            return NULL;
        }
        pair->key = key;
        pair->value = val;

        if (last) {
            last->next = pair;
        } else {
            v->object_val->pairs = pair;
        }
        last = pair;
        v->object_val->count++;

        skip_whitespace(ctx);
        char c = peek(ctx);
        if (c == '}') {
            consume(ctx);
            return v;
        }
        if (c != ',') {
            json_free(v);
            return NULL;
        }
        consume(ctx);
    }
}

static JsonValue* parse_value(ParseContext* ctx) {
    skip_whitespace(ctx);
    char c = peek(ctx);

    if (c == 'n') return parse_null(ctx);
    if (c == 't' || c == 'f') return parse_bool(ctx);
    if (c == '"') return parse_string(ctx);
    if (c == '[') return parse_array(ctx);
    if (c == '{') return parse_object(ctx);
    if (c == '-' || isdigit(c)) return parse_number(ctx);

    return NULL;
}

// ============================================================================
// Public Parse Function
// ============================================================================

JsonValue* json_parse(const char* json_str) {
    if (!json_str) return NULL;

    ParseContext ctx = {
        .json = json_str,
        .pos = 0,
        .len = strlen(json_str),
        .error = {0}
    };

    return parse_value(&ctx);
}

// ============================================================================
// Value Access Functions
// ============================================================================

JsonValue* json_object_get(const JsonValue* obj, const char* key) {
    if (!obj || obj->type != JSON_OBJECT || !obj->object_val || !key) {
        return NULL;
    }

    JsonPair* pair = obj->object_val->pairs;
    while (pair) {
        if (strcmp(pair->key, key) == 0) {
            return pair->value;
        }
        pair = pair->next;
    }
    return NULL;
}

const char* json_object_get_string(const JsonValue* obj, const char* key) {
    JsonValue* v = json_object_get(obj, key);
    if (v && v->type == JSON_STRING) {
        return v->string_val;
    }
    return NULL;
}

int64_t json_object_get_int(const JsonValue* obj, const char* key, int64_t default_val) {
    JsonValue* v = json_object_get(obj, key);
    if (v && v->type == JSON_NUMBER) {
        return (int64_t)v->number_val;
    }
    return default_val;
}

double json_object_get_number(const JsonValue* obj, const char* key, double default_val) {
    JsonValue* v = json_object_get(obj, key);
    if (v && v->type == JSON_NUMBER) {
        return v->number_val;
    }
    return default_val;
}

bool json_object_get_bool(const JsonValue* obj, const char* key, bool default_val) {
    JsonValue* v = json_object_get(obj, key);
    if (v && v->type == JSON_BOOL) {
        return v->bool_val;
    }
    return default_val;
}

bool json_object_has(const JsonValue* obj, const char* key) {
    return json_object_get(obj, key) != NULL;
}

int json_array_length(const JsonValue* arr) {
    if (!arr || arr->type != JSON_ARRAY || !arr->array_val) {
        return 0;
    }
    return arr->array_val->count;
}

JsonValue* json_array_get(const JsonValue* arr, int index) {
    if (!arr || arr->type != JSON_ARRAY || !arr->array_val || index < 0) {
        return NULL;
    }

    JsonElement* elem = arr->array_val->elements;
    for (int i = 0; elem && i < index; i++) {
        elem = elem->next;
    }

    return elem ? elem->value : NULL;
}

// ============================================================================
// JSON Builder
// ============================================================================

#define INITIAL_CAPACITY 1024

void json_builder_init(JsonBuilder* builder) {
    builder->capacity = INITIAL_CAPACITY;
    builder->buffer = malloc(builder->capacity);
    builder->size = 0;
    if (builder->buffer) {
        builder->buffer[0] = '\0';
    }
}

void json_builder_free(JsonBuilder* builder) {
    free(builder->buffer);
    builder->buffer = NULL;
    builder->size = 0;
    builder->capacity = 0;
}

char* json_builder_finish(JsonBuilder* builder) {
    char* result = builder->buffer;
    builder->buffer = NULL;
    builder->size = 0;
    builder->capacity = 0;
    return result;
}

static void ensure_capacity(JsonBuilder* builder, size_t additional) {
    size_t needed = builder->size + additional + 1;
    if (needed > builder->capacity) {
        size_t new_cap = builder->capacity * 2;
        while (new_cap < needed) new_cap *= 2;
        char* new_buf = realloc(builder->buffer, new_cap);
        if (new_buf) {
            builder->buffer = new_buf;
            builder->capacity = new_cap;
        }
    }
}

void json_builder_append(JsonBuilder* builder, const char* str) {
    if (!str || !builder->buffer) return;
    size_t len = strlen(str);
    ensure_capacity(builder, len);
    memcpy(builder->buffer + builder->size, str, len + 1);
    builder->size += len;
}

void json_builder_appendf(JsonBuilder* builder, const char* fmt, ...) {
    if (!builder->buffer) return;

    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (needed < 0) return;

    ensure_capacity(builder, needed);

    va_start(args, fmt);
    vsnprintf(builder->buffer + builder->size, needed + 1, fmt, args);
    va_end(args);

    builder->size += needed;
}

void json_builder_object_start(JsonBuilder* builder) {
    json_builder_append(builder, "{");
}

void json_builder_object_end(JsonBuilder* builder) {
    json_builder_append(builder, "}");
}

void json_builder_array_start(JsonBuilder* builder) {
    json_builder_append(builder, "[");
}

void json_builder_array_end(JsonBuilder* builder) {
    json_builder_append(builder, "]");
}

void json_builder_key(JsonBuilder* builder, const char* key) {
    json_builder_append(builder, "\"");
    json_builder_append(builder, key);
    json_builder_append(builder, "\":");
}

void json_builder_string(JsonBuilder* builder, const char* value) {
    if (!value) {
        json_builder_append(builder, "null");
        return;
    }

    if (!builder->buffer) return;

    // Pre-calculate required size to minimize reallocations
    size_t extra_size = 2; // for quotes
    for (const char* p = value; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\' || c == '\b' || c == '\f' ||
            c == '\n' || c == '\r' || c == '\t') {
            extra_size += 2; // escape sequences like \"
        } else if (c < 32) {
            extra_size += 6; // \uXXXX
        } else {
            extra_size += 1;
        }
    }

    ensure_capacity(builder, extra_size);

    // Build escaped string directly into buffer
    char* out = builder->buffer + builder->size;
    *out++ = '"';

    for (const char* p = value; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
            case '"':  *out++ = '\\'; *out++ = '"'; break;
            case '\\': *out++ = '\\'; *out++ = '\\'; break;
            case '\b': *out++ = '\\'; *out++ = 'b'; break;
            case '\f': *out++ = '\\'; *out++ = 'f'; break;
            case '\n': *out++ = '\\'; *out++ = 'n'; break;
            case '\r': *out++ = '\\'; *out++ = 'r'; break;
            case '\t': *out++ = '\\'; *out++ = 't'; break;
            default:
                if (c < 32) {
                    out += sprintf(out, "\\u%04x", c);
                } else {
                    *out++ = c;
                }
        }
    }

    *out++ = '"';
    *out = '\0';
    builder->size = out - builder->buffer;
}

void json_builder_int(JsonBuilder* builder, int64_t value) {
    json_builder_appendf(builder, "%lld", (long long)value);
}

void json_builder_number(JsonBuilder* builder, double value) {
    json_builder_appendf(builder, "%g", value);
}

void json_builder_bool(JsonBuilder* builder, bool value) {
    json_builder_append(builder, value ? "true" : "false");
}

void json_builder_null(JsonBuilder* builder) {
    json_builder_append(builder, "null");
}

void json_builder_raw(JsonBuilder* builder, const char* json) {
    json_builder_append(builder, json);
}

void json_builder_comma(JsonBuilder* builder) {
    json_builder_append(builder, ",");
}

// ============================================================================
// Convenience Functions
// ============================================================================

char* json_escape_string(const char* str) {
    if (!str) return strdup("null");

    JsonBuilder builder;
    json_builder_init(&builder);
    json_builder_string(&builder, str);
    return json_builder_finish(&builder);
}

char* json_error_response(const char* error_message) {
    JsonBuilder builder;
    json_builder_init(&builder);

    json_builder_object_start(&builder);
    json_builder_key(&builder, "success");
    json_builder_bool(&builder, false);
    json_builder_comma(&builder);
    json_builder_key(&builder, "error");
    json_builder_string(&builder, error_message);
    json_builder_object_end(&builder);

    return json_builder_finish(&builder);
}

char* json_success_response(const char* result) {
    JsonBuilder builder;
    json_builder_init(&builder);

    json_builder_object_start(&builder);
    json_builder_key(&builder, "success");
    json_builder_bool(&builder, true);
    json_builder_comma(&builder);
    json_builder_key(&builder, "result");
    json_builder_string(&builder, result);
    json_builder_object_end(&builder);

    return json_builder_finish(&builder);
}

char* json_success_response_raw(const char* raw_json) {
    JsonBuilder builder;
    json_builder_init(&builder);

    json_builder_object_start(&builder);
    json_builder_key(&builder, "success");
    json_builder_bool(&builder, true);
    json_builder_comma(&builder);
    json_builder_key(&builder, "result");
    json_builder_raw(&builder, raw_json ? raw_json : "null");
    json_builder_object_end(&builder);

    return json_builder_finish(&builder);
}
