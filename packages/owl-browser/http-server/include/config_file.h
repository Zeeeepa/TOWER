/**
 * Owl Browser HTTP Server - Configuration File Parser
 *
 * Supports loading configuration from JSON and YAML files.
 * Priority order: CLI args > Environment variables > Config file > Defaults
 */

#ifndef OWL_HTTP_CONFIG_FILE_H
#define OWL_HTTP_CONFIG_FILE_H

#include "config.h"
#include <stdbool.h>

/**
 * Supported configuration file formats
 */
typedef enum {
    CONFIG_FORMAT_UNKNOWN,
    CONFIG_FORMAT_JSON,
    CONFIG_FORMAT_YAML
} ConfigFormat;

/**
 * Detect configuration file format from file extension or content.
 *
 * @param file_path Path to the configuration file
 * @return Detected format or CONFIG_FORMAT_UNKNOWN
 */
ConfigFormat config_detect_format(const char* file_path);

/**
 * Load configuration from a JSON file.
 *
 * @param config Output: Configuration structure to populate
 * @param file_path Path to the JSON configuration file
 * @return 0 on success, -1 on error
 */
int config_load_json(ServerConfig* config, const char* file_path);

/**
 * Load configuration from a YAML file.
 *
 * @param config Output: Configuration structure to populate
 * @param file_path Path to the YAML configuration file
 * @return 0 on success, -1 on error
 */
int config_load_yaml(ServerConfig* config, const char* file_path);

/**
 * Load configuration from a file (auto-detects format).
 *
 * @param config Output: Configuration structure to populate
 * @param file_path Path to the configuration file
 * @return 0 on success, -1 on error
 */
int config_load_file(ServerConfig* config, const char* file_path);

/**
 * Parse IP whitelist string (comma-separated) into config structure.
 *
 * @param config Output: Configuration structure to populate
 * @param ip_list Comma-separated list of IPs or CIDR ranges
 * @return Number of IPs parsed
 */
int config_parse_ip_whitelist(ServerConfig* config, const char* ip_list);

/**
 * Generate example configuration file.
 *
 * @param file_path Path to write the example config
 * @param format Format to generate (JSON or YAML)
 * @return 0 on success, -1 on error
 */
int config_generate_example(const char* file_path, ConfigFormat format);

/**
 * Print configuration file help.
 */
void config_print_file_help(void);

#endif // OWL_HTTP_CONFIG_FILE_H
