#include "bus_routes.h"
#include <string.h>
#include <ctype.h>

// Get allowed next characters for prefix
uint32_t bus_route_next_mask(const char *prefix, size_t prefix_len)
{
    if (prefix_len >= 4) {
        return 0;  // No more chars allowed
    }

    uint32_t mask = 0;
    char test[4] = {' ', ' ', ' ', ' '};

    // Copy prefix
    for (size_t i = 0; i < prefix_len && i < 4; i++) {
        test[i] = toupper((unsigned char)prefix[i]);
    }

    // Check each character in charset
    for (size_t c = 0; c < BUS_ROUTE_CHARSET_LEN; c++) {
        char ch = BUS_ROUTE_CHARSET[c];
        test[prefix_len] = ch;

        // See if any route starts with this prefix
        for (uint16_t i = 0; i < bus_route_index_count; i++) {
            if (memcmp(bus_route_index[i].name, test, prefix_len + 1) == 0 ||
                (prefix_len < 3 && bus_route_index[i].name[prefix_len] == ch)) {
                mask |= (1u << c);
                break;
            }
        }
    }

    return mask;
}

// Check if name is complete route
uint8_t bus_route_is_complete(const char *name, size_t len)
{
    if (len == 0 || len > 4) {
        return 0;
    }

    char test[4] = {' ', ' ', ' ', ' '};
    for (size_t i = 0; i < len && i < 4; i++) {
        test[i] = toupper((unsigned char)name[i]);
    }

    for (uint16_t i = 0; i < bus_route_index_count; i++) {
        if (memcmp(bus_route_index[i].name, test, 4) == 0) {
            return 1;
        }
    }

    return 0;
}

// Get operators for route
uint8_t bus_route_get_operators(const char *name, size_t len)
{
    if (len == 0 || len > 4) {
        return 0;
    }

    char test[4] = {' ', ' ', ' ', ' '};
    for (size_t i = 0; i < len && i < 4; i++) {
        test[i] = toupper((unsigned char)name[i]);
    }

    for (uint16_t i = 0; i < bus_route_index_count; i++) {
        if (memcmp(bus_route_index[i].name, test, 4) == 0) {
            return bus_route_index[i].ops;
        }
    }

    return 0;
}
