/**
 * Owl Browser HTTP Server - Configuration File Parser Implementation
 *
 * Supports JSON and YAML configuration files with a lightweight parser.
 */

#include "config_file.h"
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

// ============================================================================
// Helper Functions
// ============================================================================

static char* read_file_contents(const char* file_path, size_t* out_size) {
    FILE* file = fopen(file_path, "rb");
    if (!file) {
        fprintf(stderr, "Error: Cannot open config file: %s (%s)\n",
                file_path, strerror(errno));
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size <= 0 || size > 1024 * 1024) {  // Max 1MB config file
        fprintf(stderr, "Error: Config file size invalid or too large\n");
        fclose(file);
        return NULL;
    }

    char* content = malloc(size + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }

    size_t read_size = fread(content, 1, size, file);
    fclose(file);

    if (read_size != (size_t)size) {
        free(content);
        return NULL;
    }

    content[size] = '\0';
    if (out_size) *out_size = size;
    return content;
}

static void trim_whitespace(char* str) {
    if (!str) return;

    // Trim leading
    char* start = str;
    while (*start && isspace((unsigned char)*start)) start++;

    // Trim trailing
    char* end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';

    // Shift if needed
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

static bool parse_bool(const char* value) {
    if (!value) return false;
    return (strcmp(value, "true") == 0 ||
            strcmp(value, "yes") == 0 ||
            strcmp(value, "1") == 0 ||
            strcmp(value, "on") == 0);
}

// ============================================================================
// Format Detection
// ============================================================================

ConfigFormat config_detect_format(const char* file_path) {
    if (!file_path) return CONFIG_FORMAT_UNKNOWN;

    // Check by extension
    const char* ext = strrchr(file_path, '.');
    if (ext) {
        if (strcasecmp(ext, ".json") == 0) return CONFIG_FORMAT_JSON;
        if (strcasecmp(ext, ".yaml") == 0) return CONFIG_FORMAT_YAML;
        if (strcasecmp(ext, ".yml") == 0) return CONFIG_FORMAT_YAML;
    }

    // Try to detect from content
    char* content = read_file_contents(file_path, NULL);
    if (!content) return CONFIG_FORMAT_UNKNOWN;

    // Skip whitespace
    char* p = content;
    while (*p && isspace((unsigned char)*p)) p++;

    ConfigFormat format = CONFIG_FORMAT_UNKNOWN;

    // JSON starts with { or [
    if (*p == '{' || *p == '[') {
        format = CONFIG_FORMAT_JSON;
    }
    // YAML typically has key: value format or starts with ---
    else if (strncmp(p, "---", 3) == 0 || strchr(p, ':')) {
        format = CONFIG_FORMAT_YAML;
    }

    free(content);
    return format;
}

// ============================================================================
// JSON Configuration Loader
// ============================================================================

static void apply_json_value(ServerConfig* config, const char* key, JsonValue* value) {
    if (!key || !value) return;

    // Server settings
    if (strcmp(key, "host") == 0 && value->type == JSON_STRING) {
        strncpy(config->host, value->string_val, sizeof(config->host) - 1);
    }
    else if (strcmp(key, "port") == 0 && value->type == JSON_NUMBER) {
        config->port = (uint16_t)value->number_val;
    }
    else if (strcmp(key, "token") == 0 && value->type == JSON_STRING) {
        strncpy(config->auth_token, value->string_val, sizeof(config->auth_token) - 1);
    }
    else if (strcmp(key, "browser_path") == 0 && value->type == JSON_STRING) {
        strncpy(config->browser_path, value->string_val, sizeof(config->browser_path) - 1);
    }
    else if (strcmp(key, "max_connections") == 0 && value->type == JSON_NUMBER) {
        config->max_connections = (int)value->number_val;
    }
    else if (strcmp(key, "request_timeout_ms") == 0 && value->type == JSON_NUMBER) {
        config->request_timeout_ms = (int)value->number_val;
    }
    else if (strcmp(key, "browser_timeout_ms") == 0 && value->type == JSON_NUMBER) {
        config->browser_timeout_ms = (int)value->number_val;
    }
    else if (strcmp(key, "verbose") == 0 && value->type == JSON_BOOL) {
        config->verbose = value->bool_val;
    }
    else if (strcmp(key, "log_requests") == 0 && value->type == JSON_BOOL) {
        config->log_requests = value->bool_val;
    }
    else if (strcmp(key, "graceful_shutdown") == 0 && value->type == JSON_BOOL) {
        config->graceful_shutdown = value->bool_val;
    }
    else if (strcmp(key, "shutdown_timeout_sec") == 0 && value->type == JSON_NUMBER) {
        config->shutdown_timeout_sec = (int)value->number_val;
    }
    else if (strcmp(key, "keep_alive_timeout_sec") == 0 && value->type == JSON_NUMBER) {
        config->keep_alive_timeout_sec = (int)value->number_val;
    }
}

static void apply_json_rate_limit(ServerConfig* config, JsonValue* obj) {
    if (!obj || obj->type != JSON_OBJECT) return;

    JsonPair* pair = obj->object_val->pairs;
    while (pair) {
        if (strcmp(pair->key, "enabled") == 0 && pair->value->type == JSON_BOOL) {
            config->rate_limit.enabled = pair->value->bool_val;
        }
        else if (strcmp(pair->key, "requests_per_window") == 0 && pair->value->type == JSON_NUMBER) {
            config->rate_limit.requests_per_window = (int)pair->value->number_val;
        }
        else if (strcmp(pair->key, "window_seconds") == 0 && pair->value->type == JSON_NUMBER) {
            config->rate_limit.window_seconds = (int)pair->value->number_val;
        }
        else if (strcmp(pair->key, "burst_size") == 0 && pair->value->type == JSON_NUMBER) {
            config->rate_limit.burst_size = (int)pair->value->number_val;
        }
        pair = pair->next;
    }
}

static void apply_json_ip_whitelist(ServerConfig* config, JsonValue* obj) {
    if (!obj || obj->type != JSON_OBJECT) return;

    JsonPair* pair = obj->object_val->pairs;
    while (pair) {
        if (strcmp(pair->key, "enabled") == 0 && pair->value->type == JSON_BOOL) {
            config->ip_whitelist.enabled = pair->value->bool_val;
        }
        else if (strcmp(pair->key, "ips") == 0 && pair->value->type == JSON_ARRAY) {
            config->ip_whitelist.count = 0;
            int arr_len = json_array_length(pair->value);
            for (int i = 0; i < arr_len && config->ip_whitelist.count < MAX_WHITELIST_IPS; i++) {
                JsonValue* item = json_array_get(pair->value, i);
                if (item && item->type == JSON_STRING) {
                    strncpy(config->ip_whitelist.ips[config->ip_whitelist.count],
                            item->string_val, 63);
                    config->ip_whitelist.count++;
                }
            }
        }
        pair = pair->next;
    }
}

static void apply_json_ssl(ServerConfig* config, JsonValue* obj) {
    if (!obj || obj->type != JSON_OBJECT) return;

    JsonPair* pair = obj->object_val->pairs;
    while (pair) {
        if (strcmp(pair->key, "enabled") == 0 && pair->value->type == JSON_BOOL) {
            config->ssl.enabled = pair->value->bool_val;
        }
        else if (strcmp(pair->key, "cert_path") == 0 && pair->value->type == JSON_STRING) {
            strncpy(config->ssl.cert_path, pair->value->string_val,
                    sizeof(config->ssl.cert_path) - 1);
        }
        else if (strcmp(pair->key, "key_path") == 0 && pair->value->type == JSON_STRING) {
            strncpy(config->ssl.key_path, pair->value->string_val,
                    sizeof(config->ssl.key_path) - 1);
        }
        else if (strcmp(pair->key, "ca_path") == 0 && pair->value->type == JSON_STRING) {
            strncpy(config->ssl.ca_path, pair->value->string_val,
                    sizeof(config->ssl.ca_path) - 1);
        }
        else if (strcmp(pair->key, "verify_client") == 0 && pair->value->type == JSON_BOOL) {
            config->ssl.verify_client = pair->value->bool_val;
        }
        pair = pair->next;
    }
}

static void apply_json_cors(ServerConfig* config, JsonValue* obj) {
    if (!obj || obj->type != JSON_OBJECT) return;

    JsonPair* pair = obj->object_val->pairs;
    while (pair) {
        if (strcmp(pair->key, "enabled") == 0 && pair->value->type == JSON_BOOL) {
            config->cors.enabled = pair->value->bool_val;
        }
        else if (strcmp(pair->key, "allowed_origins") == 0 && pair->value->type == JSON_STRING) {
            strncpy(config->cors.allowed_origins, pair->value->string_val,
                    sizeof(config->cors.allowed_origins) - 1);
        }
        else if (strcmp(pair->key, "allowed_methods") == 0 && pair->value->type == JSON_STRING) {
            strncpy(config->cors.allowed_methods, pair->value->string_val,
                    sizeof(config->cors.allowed_methods) - 1);
        }
        else if (strcmp(pair->key, "allowed_headers") == 0 && pair->value->type == JSON_STRING) {
            strncpy(config->cors.allowed_headers, pair->value->string_val,
                    sizeof(config->cors.allowed_headers) - 1);
        }
        else if (strcmp(pair->key, "max_age_seconds") == 0 && pair->value->type == JSON_NUMBER) {
            config->cors.max_age_seconds = (int)pair->value->number_val;
        }
        pair = pair->next;
    }
}

static void apply_json_websocket(ServerConfig* config, JsonValue* obj) {
    if (!obj || obj->type != JSON_OBJECT) return;

    JsonPair* pair = obj->object_val->pairs;
    while (pair) {
        if (strcmp(pair->key, "enabled") == 0 && pair->value->type == JSON_BOOL) {
            config->websocket.enabled = pair->value->bool_val;
        }
        else if (strcmp(pair->key, "max_connections") == 0 && pair->value->type == JSON_NUMBER) {
            config->websocket.max_connections = (int)pair->value->number_val;
        }
        else if (strcmp(pair->key, "message_max_size") == 0 && pair->value->type == JSON_NUMBER) {
            config->websocket.message_max_size = (int)pair->value->number_val;
        }
        else if (strcmp(pair->key, "ping_interval_sec") == 0 && pair->value->type == JSON_NUMBER) {
            config->websocket.ping_interval_sec = (int)pair->value->number_val;
        }
        else if (strcmp(pair->key, "pong_timeout_sec") == 0 && pair->value->type == JSON_NUMBER) {
            config->websocket.pong_timeout_sec = (int)pair->value->number_val;
        }
        pair = pair->next;
    }
}

int config_load_json(ServerConfig* config, const char* file_path) {
    if (!config || !file_path) return -1;

    char* content = read_file_contents(file_path, NULL);
    if (!content) return -1;

    JsonValue* root = json_parse(content);
    free(content);

    if (!root || root->type != JSON_OBJECT) {
        fprintf(stderr, "Error: Invalid JSON in config file\n");
        json_free(root);
        return -1;
    }

    // Process top-level keys
    JsonPair* pair = root->object_val->pairs;
    while (pair) {
        if (strcmp(pair->key, "rate_limit") == 0) {
            apply_json_rate_limit(config, pair->value);
        }
        else if (strcmp(pair->key, "ip_whitelist") == 0) {
            apply_json_ip_whitelist(config, pair->value);
        }
        else if (strcmp(pair->key, "ssl") == 0) {
            apply_json_ssl(config, pair->value);
        }
        else if (strcmp(pair->key, "cors") == 0) {
            apply_json_cors(config, pair->value);
        }
        else if (strcmp(pair->key, "websocket") == 0) {
            apply_json_websocket(config, pair->value);
        }
        else {
            apply_json_value(config, pair->key, pair->value);
        }
        pair = pair->next;
    }

    json_free(root);
    return 0;
}

// ============================================================================
// YAML Configuration Loader (Lightweight Implementation)
// ============================================================================

typedef struct {
    char key[256];
    char value[1024];
    int indent;
    char section[64];
} YamlLine;

static int parse_yaml_line(const char* line, YamlLine* result) {
    memset(result, 0, sizeof(YamlLine));

    // Count indent
    const char* p = line;
    while (*p == ' ' || *p == '\t') {
        result->indent += (*p == '\t') ? 2 : 1;
        p++;
    }

    // Skip empty lines and comments
    if (*p == '\0' || *p == '\n' || *p == '#' || strncmp(p, "---", 3) == 0) {
        return 0;
    }

    // Handle array items (- value)
    if (*p == '-') {
        p++;
        while (*p == ' ') p++;
        strncpy(result->value, p, sizeof(result->value) - 1);
        trim_whitespace(result->value);
        strcpy(result->key, "-");
        return 1;
    }

    // Parse key: value
    char* colon = strchr(p, ':');
    if (!colon) return 0;

    // Extract key
    size_t key_len = colon - p;
    if (key_len >= sizeof(result->key)) key_len = sizeof(result->key) - 1;
    strncpy(result->key, p, key_len);
    result->key[key_len] = '\0';
    trim_whitespace(result->key);

    // Extract value (after colon)
    p = colon + 1;
    while (*p == ' ') p++;

    // Check if value exists on same line
    if (*p && *p != '\n' && *p != '#') {
        strncpy(result->value, p, sizeof(result->value) - 1);
        trim_whitespace(result->value);

        // Remove quotes if present
        size_t vlen = strlen(result->value);
        if (vlen >= 2) {
            if ((result->value[0] == '"' && result->value[vlen-1] == '"') ||
                (result->value[0] == '\'' && result->value[vlen-1] == '\'')) {
                memmove(result->value, result->value + 1, vlen - 2);
                result->value[vlen - 2] = '\0';
            }
        }
    }

    return 1;
}

int config_load_yaml(ServerConfig* config, const char* file_path) {
    if (!config || !file_path) return -1;

    char* content = read_file_contents(file_path, NULL);
    if (!content) return -1;

    char current_section[64] = "";
    char current_subsection[64] = "";
    int array_index = 0;

    char* line = strtok(content, "\n");
    while (line) {
        YamlLine parsed;
        if (parse_yaml_line(line, &parsed) && parsed.key[0]) {
            // Track sections based on indent
            if (parsed.indent == 0 && strlen(parsed.value) == 0) {
                strncpy(current_section, parsed.key, sizeof(current_section) - 1);
                current_subsection[0] = '\0';
                array_index = 0;
            }
            else if (parsed.indent == 2 && strlen(parsed.value) == 0) {
                strncpy(current_subsection, parsed.key, sizeof(current_subsection) - 1);
                array_index = 0;
            }
            else if (strlen(parsed.value) > 0 || strcmp(parsed.key, "-") == 0) {
                // Apply value based on section
                if (strcmp(current_section, "rate_limit") == 0) {
                    if (strcmp(parsed.key, "enabled") == 0) {
                        config->rate_limit.enabled = parse_bool(parsed.value);
                    }
                    else if (strcmp(parsed.key, "requests_per_window") == 0) {
                        config->rate_limit.requests_per_window = atoi(parsed.value);
                    }
                    else if (strcmp(parsed.key, "window_seconds") == 0) {
                        config->rate_limit.window_seconds = atoi(parsed.value);
                    }
                    else if (strcmp(parsed.key, "burst_size") == 0) {
                        config->rate_limit.burst_size = atoi(parsed.value);
                    }
                }
                else if (strcmp(current_section, "ip_whitelist") == 0) {
                    if (strcmp(parsed.key, "enabled") == 0) {
                        config->ip_whitelist.enabled = parse_bool(parsed.value);
                    }
                    else if (strcmp(parsed.key, "-") == 0 &&
                             config->ip_whitelist.count < MAX_WHITELIST_IPS) {
                        strncpy(config->ip_whitelist.ips[config->ip_whitelist.count],
                                parsed.value, 63);
                        config->ip_whitelist.count++;
                    }
                }
                else if (strcmp(current_section, "ssl") == 0) {
                    if (strcmp(parsed.key, "enabled") == 0) {
                        config->ssl.enabled = parse_bool(parsed.value);
                    }
                    else if (strcmp(parsed.key, "cert_path") == 0) {
                        strncpy(config->ssl.cert_path, parsed.value,
                                sizeof(config->ssl.cert_path) - 1);
                    }
                    else if (strcmp(parsed.key, "key_path") == 0) {
                        strncpy(config->ssl.key_path, parsed.value,
                                sizeof(config->ssl.key_path) - 1);
                    }
                    else if (strcmp(parsed.key, "ca_path") == 0) {
                        strncpy(config->ssl.ca_path, parsed.value,
                                sizeof(config->ssl.ca_path) - 1);
                    }
                    else if (strcmp(parsed.key, "verify_client") == 0) {
                        config->ssl.verify_client = parse_bool(parsed.value);
                    }
                }
                else if (strcmp(current_section, "cors") == 0) {
                    if (strcmp(parsed.key, "enabled") == 0) {
                        config->cors.enabled = parse_bool(parsed.value);
                    }
                    else if (strcmp(parsed.key, "allowed_origins") == 0) {
                        strncpy(config->cors.allowed_origins, parsed.value,
                                sizeof(config->cors.allowed_origins) - 1);
                    }
                    else if (strcmp(parsed.key, "allowed_methods") == 0) {
                        strncpy(config->cors.allowed_methods, parsed.value,
                                sizeof(config->cors.allowed_methods) - 1);
                    }
                    else if (strcmp(parsed.key, "allowed_headers") == 0) {
                        strncpy(config->cors.allowed_headers, parsed.value,
                                sizeof(config->cors.allowed_headers) - 1);
                    }
                    else if (strcmp(parsed.key, "max_age_seconds") == 0) {
                        config->cors.max_age_seconds = atoi(parsed.value);
                    }
                }
                else if (strcmp(current_section, "websocket") == 0) {
                    if (strcmp(parsed.key, "enabled") == 0) {
                        config->websocket.enabled = parse_bool(parsed.value);
                    }
                    else if (strcmp(parsed.key, "max_connections") == 0) {
                        config->websocket.max_connections = atoi(parsed.value);
                    }
                    else if (strcmp(parsed.key, "message_max_size") == 0) {
                        config->websocket.message_max_size = atoi(parsed.value);
                    }
                    else if (strcmp(parsed.key, "ping_interval_sec") == 0) {
                        config->websocket.ping_interval_sec = atoi(parsed.value);
                    }
                    else if (strcmp(parsed.key, "pong_timeout_sec") == 0) {
                        config->websocket.pong_timeout_sec = atoi(parsed.value);
                    }
                }
                else {
                    // Top-level settings
                    if (strcmp(parsed.key, "host") == 0) {
                        strncpy(config->host, parsed.value, sizeof(config->host) - 1);
                    }
                    else if (strcmp(parsed.key, "port") == 0) {
                        config->port = (uint16_t)atoi(parsed.value);
                    }
                    else if (strcmp(parsed.key, "token") == 0) {
                        strncpy(config->auth_token, parsed.value,
                                sizeof(config->auth_token) - 1);
                    }
                    else if (strcmp(parsed.key, "browser_path") == 0) {
                        strncpy(config->browser_path, parsed.value,
                                sizeof(config->browser_path) - 1);
                    }
                    else if (strcmp(parsed.key, "max_connections") == 0) {
                        config->max_connections = atoi(parsed.value);
                    }
                    else if (strcmp(parsed.key, "request_timeout_ms") == 0) {
                        config->request_timeout_ms = atoi(parsed.value);
                    }
                    else if (strcmp(parsed.key, "browser_timeout_ms") == 0) {
                        config->browser_timeout_ms = atoi(parsed.value);
                    }
                    else if (strcmp(parsed.key, "verbose") == 0) {
                        config->verbose = parse_bool(parsed.value);
                    }
                    else if (strcmp(parsed.key, "log_requests") == 0) {
                        config->log_requests = parse_bool(parsed.value);
                    }
                    else if (strcmp(parsed.key, "graceful_shutdown") == 0) {
                        config->graceful_shutdown = parse_bool(parsed.value);
                    }
                    else if (strcmp(parsed.key, "shutdown_timeout_sec") == 0) {
                        config->shutdown_timeout_sec = atoi(parsed.value);
                    }
                    else if (strcmp(parsed.key, "keep_alive_timeout_sec") == 0) {
                        config->keep_alive_timeout_sec = atoi(parsed.value);
                    }
                }
            }
        }
        line = strtok(NULL, "\n");
    }

    free(content);
    return 0;
}

// ============================================================================
// Auto-detect and Load
// ============================================================================

int config_load_file(ServerConfig* config, const char* file_path) {
    ConfigFormat format = config_detect_format(file_path);

    switch (format) {
        case CONFIG_FORMAT_JSON:
            return config_load_json(config, file_path);
        case CONFIG_FORMAT_YAML:
            return config_load_yaml(config, file_path);
        default:
            fprintf(stderr, "Error: Unknown config file format: %s\n", file_path);
            fprintf(stderr, "Supported formats: .json, .yaml, .yml\n");
            return -1;
    }
}

// ============================================================================
// IP Whitelist Parsing
// ============================================================================

int config_parse_ip_whitelist(ServerConfig* config, const char* ip_list) {
    if (!config || !ip_list) return 0;

    config->ip_whitelist.count = 0;
    char* copy = strdup(ip_list);
    if (!copy) return 0;

    char* token = strtok(copy, ",");
    while (token && config->ip_whitelist.count < MAX_WHITELIST_IPS) {
        // Trim whitespace
        while (*token == ' ') token++;
        char* end = token + strlen(token) - 1;
        while (end > token && *end == ' ') end--;
        *(end + 1) = '\0';

        if (strlen(token) > 0) {
            strncpy(config->ip_whitelist.ips[config->ip_whitelist.count], token, 63);
            config->ip_whitelist.count++;
        }
        token = strtok(NULL, ",");
    }

    free(copy);
    return config->ip_whitelist.count;
}

// ============================================================================
// Example Configuration Generator
// ============================================================================

int config_generate_example(const char* file_path, ConfigFormat format) {
    FILE* file = fopen(file_path, "w");
    if (!file) {
        fprintf(stderr, "Error: Cannot create file: %s\n", file_path);
        return -1;
    }

    if (format == CONFIG_FORMAT_JSON) {
        fprintf(file,
            "{\n"
            "  \"host\": \"127.0.0.1\",\n"
            "  \"port\": 8080,\n"
            "  \"token\": \"your-secret-token-here\",\n"
            "  \"browser_path\": \"/path/to/owl_browser\",\n"
            "  \"max_connections\": 100,\n"
            "  \"request_timeout_ms\": 30000,\n"
            "  \"browser_timeout_ms\": 60000,\n"
            "  \"verbose\": false,\n"
            "  \"log_requests\": false,\n"
            "  \"graceful_shutdown\": true,\n"
            "  \"shutdown_timeout_sec\": 30,\n"
            "  \"keep_alive_timeout_sec\": 60,\n"
            "\n"
            "  \"rate_limit\": {\n"
            "    \"enabled\": false,\n"
            "    \"requests_per_window\": 100,\n"
            "    \"window_seconds\": 60,\n"
            "    \"burst_size\": 20\n"
            "  },\n"
            "\n"
            "  \"ip_whitelist\": {\n"
            "    \"enabled\": false,\n"
            "    \"ips\": [\n"
            "      \"127.0.0.1\",\n"
            "      \"192.168.1.0/24\",\n"
            "      \"10.0.0.0/8\"\n"
            "    ]\n"
            "  },\n"
            "\n"
            "  \"ssl\": {\n"
            "    \"enabled\": false,\n"
            "    \"cert_path\": \"/path/to/cert.pem\",\n"
            "    \"key_path\": \"/path/to/key.pem\",\n"
            "    \"ca_path\": \"\",\n"
            "    \"verify_client\": false\n"
            "  },\n"
            "\n"
            "  \"cors\": {\n"
            "    \"enabled\": true,\n"
            "    \"allowed_origins\": \"*\",\n"
            "    \"allowed_methods\": \"GET,POST,PUT,DELETE,OPTIONS\",\n"
            "    \"allowed_headers\": \"Content-Type,Authorization\",\n"
            "    \"max_age_seconds\": 86400\n"
            "  },\n"
            "\n"
            "  \"websocket\": {\n"
            "    \"enabled\": true,\n"
            "    \"max_connections\": 50,\n"
            "    \"message_max_size\": 16777216,\n"
            "    \"ping_interval_sec\": 30,\n"
            "    \"pong_timeout_sec\": 10\n"
            "  }\n"
            "}\n"
        );
    }
    else if (format == CONFIG_FORMAT_YAML) {
        fprintf(file,
            "# Owl Browser HTTP Server Configuration\n"
            "# =====================================\n"
            "\n"
            "# Server Settings\n"
            "host: 127.0.0.1\n"
            "port: 8080\n"
            "token: your-secret-token-here\n"
            "browser_path: /path/to/owl_browser\n"
            "\n"
            "# Connection Settings\n"
            "max_connections: 100\n"
            "request_timeout_ms: 30000\n"
            "browser_timeout_ms: 60000\n"
            "keep_alive_timeout_sec: 60\n"
            "\n"
            "# Logging\n"
            "verbose: false\n"
            "log_requests: false\n"
            "\n"
            "# Shutdown\n"
            "graceful_shutdown: true\n"
            "shutdown_timeout_sec: 30\n"
            "\n"
            "# Rate Limiting\n"
            "rate_limit:\n"
            "  enabled: false\n"
            "  requests_per_window: 100\n"
            "  window_seconds: 60\n"
            "  burst_size: 20\n"
            "\n"
            "# IP Whitelist\n"
            "ip_whitelist:\n"
            "  enabled: false\n"
            "  ips:\n"
            "    - 127.0.0.1\n"
            "    - 192.168.1.0/24\n"
            "    - 10.0.0.0/8\n"
            "\n"
            "# SSL/TLS\n"
            "ssl:\n"
            "  enabled: false\n"
            "  cert_path: /path/to/cert.pem\n"
            "  key_path: /path/to/key.pem\n"
            "  ca_path: \"\"\n"
            "  verify_client: false\n"
            "\n"
            "# CORS Settings\n"
            "cors:\n"
            "  enabled: true\n"
            "  allowed_origins: \"*\"\n"
            "  allowed_methods: GET,POST,PUT,DELETE,OPTIONS\n"
            "  allowed_headers: Content-Type,Authorization\n"
            "  max_age_seconds: 86400\n"
            "\n"
            "# WebSocket Settings\n"
            "websocket:\n"
            "  enabled: true\n"
            "  max_connections: 50\n"
            "  message_max_size: 16777216\n"
            "  ping_interval_sec: 30\n"
            "  pong_timeout_sec: 10\n"
        );
    }

    fclose(file);
    printf("Example configuration written to: %s\n", file_path);
    return 0;
}

// ============================================================================
// Help
// ============================================================================

void config_print_file_help(void) {
    fprintf(stderr,
        "\nConfiguration File Support\n"
        "==========================\n\n"
        "The server supports configuration via JSON or YAML files.\n"
        "Use -c or --config to specify a config file path.\n\n"
        "Priority order (highest to lowest):\n"
        "  1. Command-line arguments\n"
        "  2. Environment variables\n"
        "  3. Configuration file\n"
        "  4. Default values\n\n"
        "Supported formats:\n"
        "  - JSON (.json)\n"
        "  - YAML (.yaml, .yml)\n\n"
        "Generate example configs:\n"
        "  owl_http_server --generate-config config.json\n"
        "  owl_http_server --generate-config config.yaml\n\n"
        "Example usage:\n"
        "  owl_http_server -c /path/to/config.yaml\n"
        "  owl_http_server --config /path/to/config.json\n\n"
    );
}
