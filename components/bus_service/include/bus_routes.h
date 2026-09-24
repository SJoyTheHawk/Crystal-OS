#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Alphabet: "0123456789ABCDEFGHKMNOPRSTWX"
#define BUS_ROUTE_CHARSET "0123456789ABCDEFGHKMNOPRSTWX"
#define BUS_ROUTE_CHARSET_LEN 28

// Packed index entry
typedef struct {
    char name[4];               // NOT NUL-terminated, space-padded
    uint8_t ops;                // bit 0 = KMB, bit 1 = CTB
} __attribute__((packed)) bus_route_name_t;

// Generated data (in bus_index_data.c)
extern const bus_route_name_t bus_route_index[];
extern const uint16_t bus_route_index_count;

// Query functions (allocation-free, LVGL task safe)
// Returns bitmask of allowed next characters for given prefix
uint32_t bus_route_next_mask(const char *prefix, size_t prefix_len);

// Check if name is a complete route (not just a prefix)
uint8_t bus_route_is_complete(const char *name, size_t len);

// Get available operators for a route (bit 0 = KMB, bit 1 = CTB)
uint8_t bus_route_get_operators(const char *name, size_t len);

#ifdef __cplusplus
}
#endif
