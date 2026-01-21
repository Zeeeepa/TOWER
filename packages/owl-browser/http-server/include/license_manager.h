/**
 * Owl Browser HTTP Server - License Manager
 *
 * Manages browser licenses by running the browser binary directly
 * with --license commands. Used when browser IPC is not available
 * (e.g., when browser fails to start due to license error).
 */

#ifndef OWL_HTTP_LICENSE_MANAGER_H
#define OWL_HTTP_LICENSE_MANAGER_H

#include <stdbool.h>
#include <stddef.h>

// License status information
typedef struct {
    bool valid;
    char status[64];          // "valid", "not_found", "expired", "invalid", etc.
    char message[512];        // Human-readable message
    char fingerprint[128];    // Hardware fingerprint
    char licensee[256];       // License holder name
    char license_type[64];    // License type (e.g., "ENTERPRISE")
    int seat_current;         // Current seat number
    int seat_total;           // Total seats
    char expiry[32];          // Expiry date (ISO format)
} LicenseInfo;

// License operation result
typedef struct {
    bool success;
    char message[512];
    char error[512];
} LicenseOpResult;

/**
 * Initialize license manager with browser path.
 * @param browser_path Path to the browser binary
 */
void license_manager_init(const char* browser_path);

/**
 * Shutdown license manager.
 */
void license_manager_shutdown(void);

/**
 * Get license status by running browser --license status.
 * @param info Output license information
 * @return true on success, false on error
 */
bool license_manager_get_status(LicenseInfo* info);

/**
 * Get hardware fingerprint by running browser --license fingerprint.
 * @param fingerprint Output buffer (at least 128 bytes)
 * @return true on success, false on error
 */
bool license_manager_get_fingerprint(char* fingerprint, size_t size);

/**
 * Add a license file.
 * @param license_path Path to the license file (.olic)
 * @param result Output operation result
 * @return true on success, false on error
 */
bool license_manager_add_license(const char* license_path, LicenseOpResult* result);

/**
 * Add a license from base64-encoded content.
 * @param license_content Base64-encoded license file content
 * @param result Output operation result
 * @return true on success, false on error
 */
bool license_manager_add_license_content(const char* license_content, LicenseOpResult* result);

/**
 * Remove the current license.
 * @param result Output operation result
 * @return true on success, false on error
 */
bool license_manager_remove_license(LicenseOpResult* result);

/**
 * Generate JSON response for license status.
 * Caller must free the result.
 */
char* license_manager_status_to_json(const LicenseInfo* info);

/**
 * Generate JSON response for license operation result.
 * Caller must free the result.
 */
char* license_manager_result_to_json(const LicenseOpResult* result);

#endif // OWL_HTTP_LICENSE_MANAGER_H
