#include "bus_routes.h"
#include <string.h>
#include <ctype.h>

#define BUS_ROUTE_CATALOG_CAPACITY 2048

static bus_route_name_t s_catalog[BUS_ROUTE_CATALOG_CAPACITY];
static uint16_t s_catalog_count = 0;

static const bus_route_name_t *active_index(uint16_t *count)
{
    if (s_catalog_count > 0) {
        *count = s_catalog_count;
        return s_catalog;
    }
    *count = bus_route_index_count;
    return bus_route_index;
}

void bus_route_catalog_reset(void)
{
    s_catalog_count = 0;
}

bool bus_route_catalog_add(const char *name, size_t len, uint8_t ops)
{
    if (name == NULL || len == 0 || len > sizeof(s_catalog[0].name) || ops == 0) {
        return false;
    }

    char normalized[4] = {' ', ' ', ' ', ' '};
    for (size_t i = 0; i < len; i++) {
        normalized[i] = (char)toupper((unsigned char)name[i]);
    }

    for (uint16_t i = 0; i < s_catalog_count; i++) {
        if (memcmp(s_catalog[i].name, normalized, sizeof(normalized)) == 0) {
            s_catalog[i].ops |= ops;
            return true;
        }
    }

    if (s_catalog_count >= BUS_ROUTE_CATALOG_CAPACITY) {
        return false;
    }
    memcpy(s_catalog[s_catalog_count].name, normalized, sizeof(normalized));
    s_catalog[s_catalog_count].ops = ops;
    s_catalog_count++;
    return true;
}

uint16_t bus_route_catalog_count(void)
{
    return s_catalog_count;
}

bool bus_route_catalog_get(uint16_t index, bus_route_name_t *out)
{
    if (out == NULL || index >= s_catalog_count) {
        return false;
    }
    *out = s_catalog[index];
    return true;
}

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
        uint16_t count = 0;
        const bus_route_name_t *index = active_index(&count);
        for (uint16_t i = 0; i < count; i++) {
            if (memcmp(index[i].name, test, prefix_len + 1) == 0) {
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

    uint16_t count = 0;
    const bus_route_name_t *index = active_index(&count);
    for (uint16_t i = 0; i < count; i++) {
        if (memcmp(index[i].name, test, 4) == 0) {
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

    uint16_t count = 0;
    const bus_route_name_t *index = active_index(&count);
    for (uint16_t i = 0; i < count; i++) {
        if (memcmp(index[i].name, test, 4) == 0) {
            return index[i].ops;
        }
    }

    return 0;
}
