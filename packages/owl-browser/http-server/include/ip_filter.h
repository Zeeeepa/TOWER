/**
 * Owl Browser HTTP Server - IP Filtering
 *
 * IP whitelist/blacklist filtering with CIDR range support.
 */

#ifndef OWL_HTTP_IP_FILTER_H
#define OWL_HTTP_IP_FILTER_H

#include "config.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * IP filter result
 */
typedef enum {
    IP_FILTER_ALLOWED,      // IP is allowed (in whitelist or no filter)
    IP_FILTER_DENIED,       // IP is denied (not in whitelist)
    IP_FILTER_INVALID       // IP address is invalid
} IpFilterResult;

/**
 * Initialize the IP filter.
 *
 * @param config IP whitelist configuration
 * @return 0 on success, -1 on error
 */
int ip_filter_init(const IpWhitelistConfig* config);

/**
 * Check if an IP address is allowed.
 *
 * @param client_ip The client's IP address (IPv4 or IPv6)
 * @return IP_FILTER_ALLOWED if allowed, IP_FILTER_DENIED if not
 */
IpFilterResult ip_filter_check(const char* client_ip);

/**
 * Add an IP or CIDR range to the whitelist.
 *
 * @param ip_or_cidr IP address or CIDR range (e.g., "192.168.1.0/24")
 * @return 0 on success, -1 on error
 */
int ip_filter_add(const char* ip_or_cidr);

/**
 * Remove an IP or CIDR range from the whitelist.
 *
 * @param ip_or_cidr IP address or CIDR range
 * @return 0 on success, -1 if not found
 */
int ip_filter_remove(const char* ip_or_cidr);

/**
 * Clear all entries from the whitelist.
 */
void ip_filter_clear(void);

/**
 * Check if IP filtering is enabled.
 */
bool ip_filter_is_enabled(void);

/**
 * Get the number of entries in the whitelist.
 */
int ip_filter_count(void);

/**
 * Get whitelist entry by index.
 *
 * @param index Entry index
 * @param buffer Output buffer for IP/CIDR string
 * @param buffer_size Size of output buffer
 * @return 0 on success, -1 if index out of range
 */
int ip_filter_get_entry(int index, char* buffer, size_t buffer_size);

/**
 * Validate an IP address or CIDR range string.
 *
 * @param ip_or_cidr IP address or CIDR range to validate
 * @return true if valid, false otherwise
 */
bool ip_filter_validate(const char* ip_or_cidr);

/**
 * Check if an IP matches a CIDR range.
 *
 * @param ip IP address to check
 * @param cidr CIDR range (e.g., "192.168.1.0/24")
 * @return true if IP is within the CIDR range
 */
bool ip_filter_match_cidr(const char* ip, const char* cidr);

/**
 * Get statistics about IP filtering.
 */
typedef struct {
    uint64_t total_checks;
    uint64_t allowed_count;
    uint64_t denied_count;
    int whitelist_entries;
} IpFilterStats;

void ip_filter_get_stats(IpFilterStats* stats);

/**
 * Shutdown the IP filter and free resources.
 */
void ip_filter_shutdown(void);

#endif // OWL_HTTP_IP_FILTER_H
