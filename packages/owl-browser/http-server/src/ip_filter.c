/**
 * Owl Browser HTTP Server - IP Filtering Implementation
 *
 * IP whitelist with CIDR range support for both IPv4 and IPv6.
 */

#include "ip_filter.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <arpa/inet.h>

// ============================================================================
// Data Structures
// ============================================================================

typedef struct {
    bool is_ipv6;
    union {
        struct {
            uint32_t addr;
            uint32_t mask;
        } v4;
        struct {
            uint8_t addr[16];
            uint8_t mask[16];
        } v6;
    };
    char original[64];  // original string for display
} IpEntry;

typedef struct {
    IpEntry* entries;
    int count;
    int capacity;
    bool enabled;
    pthread_mutex_t mutex;
    IpFilterStats stats;
    bool initialized;
} IpFilter;

static IpFilter g_filter = {0};

// ============================================================================
// IP Parsing Helpers
// ============================================================================

static bool parse_ipv4(const char* ip_str, uint32_t* addr) {
    struct in_addr in;
    if (inet_pton(AF_INET, ip_str, &in) == 1) {
        *addr = ntohl(in.s_addr);
        return true;
    }
    return false;
}

static bool parse_ipv6(const char* ip_str, uint8_t* addr) {
    struct in6_addr in6;
    if (inet_pton(AF_INET6, ip_str, &in6) == 1) {
        memcpy(addr, in6.s6_addr, 16);
        return true;
    }
    return false;
}

static uint32_t cidr_to_mask_v4(int prefix_len) {
    if (prefix_len <= 0) return 0;
    if (prefix_len >= 32) return 0xFFFFFFFF;
    return ~((1U << (32 - prefix_len)) - 1);
}

static void cidr_to_mask_v6(int prefix_len, uint8_t* mask) {
    memset(mask, 0, 16);
    int full_bytes = prefix_len / 8;
    int remaining_bits = prefix_len % 8;

    for (int i = 0; i < full_bytes && i < 16; i++) {
        mask[i] = 0xFF;
    }
    if (full_bytes < 16 && remaining_bits > 0) {
        mask[full_bytes] = (0xFF << (8 - remaining_bits)) & 0xFF;
    }
}

static bool parse_ip_entry(const char* ip_or_cidr, IpEntry* entry) {
    memset(entry, 0, sizeof(IpEntry));
    strncpy(entry->original, ip_or_cidr, sizeof(entry->original) - 1);

    char ip_part[64];
    int prefix_len = -1;

    // Check for CIDR notation
    const char* slash = strchr(ip_or_cidr, '/');
    if (slash) {
        size_t ip_len = slash - ip_or_cidr;
        if (ip_len >= sizeof(ip_part)) return false;
        strncpy(ip_part, ip_or_cidr, ip_len);
        ip_part[ip_len] = '\0';
        prefix_len = atoi(slash + 1);
    } else {
        strncpy(ip_part, ip_or_cidr, sizeof(ip_part) - 1);
    }

    // Try IPv4 first
    if (parse_ipv4(ip_part, &entry->v4.addr)) {
        entry->is_ipv6 = false;
        entry->v4.mask = (prefix_len >= 0) ?
            cidr_to_mask_v4(prefix_len) : 0xFFFFFFFF;
        return true;
    }

    // Try IPv6
    if (parse_ipv6(ip_part, entry->v6.addr)) {
        entry->is_ipv6 = true;
        if (prefix_len >= 0) {
            cidr_to_mask_v6(prefix_len, entry->v6.mask);
        } else {
            memset(entry->v6.mask, 0xFF, 16);
        }
        return true;
    }

    return false;
}

static bool match_ipv4(uint32_t addr, uint32_t entry_addr, uint32_t mask) {
    return (addr & mask) == (entry_addr & mask);
}

static bool match_ipv6(const uint8_t* addr, const uint8_t* entry_addr, const uint8_t* mask) {
    for (int i = 0; i < 16; i++) {
        if ((addr[i] & mask[i]) != (entry_addr[i] & mask[i])) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// Public API
// ============================================================================

int ip_filter_init(const IpWhitelistConfig* config) {
    if (!config) return -1;

    memset(&g_filter, 0, sizeof(g_filter));

    if (pthread_mutex_init(&g_filter.mutex, NULL) != 0) {
        return -1;
    }

    g_filter.enabled = config->enabled;

    // Pre-allocate entries
    g_filter.capacity = MAX_WHITELIST_IPS;
    g_filter.entries = calloc(g_filter.capacity, sizeof(IpEntry));
    if (!g_filter.entries) {
        pthread_mutex_destroy(&g_filter.mutex);
        return -1;
    }

    // Load initial whitelist
    for (int i = 0; i < config->count && i < MAX_WHITELIST_IPS; i++) {
        ip_filter_add(config->ips[i]);
    }

    g_filter.initialized = true;
    return 0;
}

IpFilterResult ip_filter_check(const char* client_ip) {
    if (!g_filter.initialized) {
        return IP_FILTER_ALLOWED;
    }

    pthread_mutex_lock(&g_filter.mutex);
    g_filter.stats.total_checks++;

    // If filtering is disabled, allow all
    if (!g_filter.enabled || g_filter.count == 0) {
        g_filter.stats.allowed_count++;
        pthread_mutex_unlock(&g_filter.mutex);
        return IP_FILTER_ALLOWED;
    }

    if (!client_ip || strlen(client_ip) == 0) {
        g_filter.stats.denied_count++;
        pthread_mutex_unlock(&g_filter.mutex);
        return IP_FILTER_INVALID;
    }

    // Parse the client IP
    IpEntry client_entry;
    if (!parse_ip_entry(client_ip, &client_entry)) {
        g_filter.stats.denied_count++;
        pthread_mutex_unlock(&g_filter.mutex);
        return IP_FILTER_INVALID;
    }

    // Check against whitelist
    for (int i = 0; i < g_filter.count; i++) {
        IpEntry* entry = &g_filter.entries[i];

        // Type must match (IPv4 vs IPv6)
        if (entry->is_ipv6 != client_entry.is_ipv6) {
            continue;
        }

        bool match = false;
        if (entry->is_ipv6) {
            match = match_ipv6(client_entry.v6.addr, entry->v6.addr, entry->v6.mask);
        } else {
            match = match_ipv4(client_entry.v4.addr, entry->v4.addr, entry->v4.mask);
        }

        if (match) {
            g_filter.stats.allowed_count++;
            pthread_mutex_unlock(&g_filter.mutex);
            return IP_FILTER_ALLOWED;
        }
    }

    g_filter.stats.denied_count++;
    pthread_mutex_unlock(&g_filter.mutex);
    return IP_FILTER_DENIED;
}

int ip_filter_add(const char* ip_or_cidr) {
    if (!g_filter.initialized || !ip_or_cidr) return -1;

    pthread_mutex_lock(&g_filter.mutex);

    if (g_filter.count >= g_filter.capacity) {
        pthread_mutex_unlock(&g_filter.mutex);
        return -1;
    }

    IpEntry entry;
    if (!parse_ip_entry(ip_or_cidr, &entry)) {
        pthread_mutex_unlock(&g_filter.mutex);
        return -1;
    }

    // Check for duplicates
    for (int i = 0; i < g_filter.count; i++) {
        if (strcmp(g_filter.entries[i].original, ip_or_cidr) == 0) {
            pthread_mutex_unlock(&g_filter.mutex);
            return 0;  // Already exists
        }
    }

    memcpy(&g_filter.entries[g_filter.count], &entry, sizeof(IpEntry));
    g_filter.count++;

    pthread_mutex_unlock(&g_filter.mutex);
    return 0;
}

int ip_filter_remove(const char* ip_or_cidr) {
    if (!g_filter.initialized || !ip_or_cidr) return -1;

    pthread_mutex_lock(&g_filter.mutex);

    for (int i = 0; i < g_filter.count; i++) {
        if (strcmp(g_filter.entries[i].original, ip_or_cidr) == 0) {
            // Shift remaining entries
            for (int j = i; j < g_filter.count - 1; j++) {
                memcpy(&g_filter.entries[j], &g_filter.entries[j + 1], sizeof(IpEntry));
            }
            g_filter.count--;
            pthread_mutex_unlock(&g_filter.mutex);
            return 0;
        }
    }

    pthread_mutex_unlock(&g_filter.mutex);
    return -1;  // Not found
}

void ip_filter_clear(void) {
    if (!g_filter.initialized) return;

    pthread_mutex_lock(&g_filter.mutex);
    g_filter.count = 0;
    pthread_mutex_unlock(&g_filter.mutex);
}

bool ip_filter_is_enabled(void) {
    return g_filter.initialized && g_filter.enabled;
}

int ip_filter_count(void) {
    if (!g_filter.initialized) return 0;

    pthread_mutex_lock(&g_filter.mutex);
    int count = g_filter.count;
    pthread_mutex_unlock(&g_filter.mutex);
    return count;
}

int ip_filter_get_entry(int index, char* buffer, size_t buffer_size) {
    if (!g_filter.initialized || !buffer || buffer_size == 0) return -1;

    pthread_mutex_lock(&g_filter.mutex);

    if (index < 0 || index >= g_filter.count) {
        pthread_mutex_unlock(&g_filter.mutex);
        return -1;
    }

    strncpy(buffer, g_filter.entries[index].original, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';

    pthread_mutex_unlock(&g_filter.mutex);
    return 0;
}

bool ip_filter_validate(const char* ip_or_cidr) {
    IpEntry entry;
    return parse_ip_entry(ip_or_cidr, &entry);
}

bool ip_filter_match_cidr(const char* ip, const char* cidr) {
    IpEntry ip_entry, cidr_entry;

    if (!parse_ip_entry(ip, &ip_entry) || !parse_ip_entry(cidr, &cidr_entry)) {
        return false;
    }

    if (ip_entry.is_ipv6 != cidr_entry.is_ipv6) {
        return false;
    }

    if (ip_entry.is_ipv6) {
        return match_ipv6(ip_entry.v6.addr, cidr_entry.v6.addr, cidr_entry.v6.mask);
    } else {
        return match_ipv4(ip_entry.v4.addr, cidr_entry.v4.addr, cidr_entry.v4.mask);
    }
}

void ip_filter_get_stats(IpFilterStats* stats) {
    if (!stats) return;

    if (!g_filter.initialized) {
        memset(stats, 0, sizeof(IpFilterStats));
        return;
    }

    pthread_mutex_lock(&g_filter.mutex);
    memcpy(stats, &g_filter.stats, sizeof(IpFilterStats));
    stats->whitelist_entries = g_filter.count;
    pthread_mutex_unlock(&g_filter.mutex);
}

void ip_filter_shutdown(void) {
    if (!g_filter.initialized) return;

    pthread_mutex_lock(&g_filter.mutex);

    free(g_filter.entries);
    g_filter.entries = NULL;
    g_filter.count = 0;
    g_filter.capacity = 0;

    pthread_mutex_unlock(&g_filter.mutex);
    pthread_mutex_destroy(&g_filter.mutex);

    g_filter.initialized = false;
}
