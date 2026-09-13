/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TRANSIT_ROUTE_NAME_LEN 4
#define TRANSIT_OP_MASK_KMB (1u << 0)
#define TRANSIT_OP_MASK_CTB (1u << 1)

// name is space-padded rather than NUL-terminated so the record is exactly 5
// bytes with no padding hole, and memcmp over 4 bytes is a total order matching
// strcmp on the unpadded strings (space, 0x20, sorts below every character in
// transit_charset).
typedef struct {
    char    name[TRANSIT_ROUTE_NAME_LEN];
    uint8_t ops;   // TRANSIT_OP_MASK_*
} transit_route_name_t;

// KMB variant identity. KMB has no direction-free per-route endpoint --
// /route/68X/O/1 answers 422 "Invalid direction" -- so a lookup needs the
// bound and service_type before it can build a URL. Service types run 1-9.
typedef struct {
    uint16_t name_index;    // index into transit_route_index[]
    char     bound;         // 'O' or 'I'
    uint8_t  service_type;
} transit_route_variant_t;

extern const char transit_charset[];
extern const transit_route_name_t transit_route_index[];
extern const uint16_t transit_route_index_count;
extern const transit_route_variant_t transit_route_variants[];
extern const uint16_t transit_route_variant_count;

// Distinct characters that can legally follow `prefix`, as a bitmask over
// transit_charset. Allocation-free and safe to call from the LVGL task.
// Returns 0 for a prefix at or past the maximum name length.
uint32_t transit_route_next_mask(const char *prefix, size_t length);

// Nonzero when `name` is itself a complete route name; the value is the
// operator mask. Drives whether the submit key lights.
uint8_t transit_route_is_complete(const char *name, size_t length);

// The charset as a NUL-terminated string, for the keypad's bit positions.
const char *transit_route_charset(void);

// Swaps a refreshed index in over the compiled baseline. The tables must outlive
// the call and stay sorted; transit_index.c owns the only buffers passed here.
void transit_index_install(const transit_route_name_t *names, uint16_t name_count,
                           const transit_route_variant_t *variants, uint16_t variant_count);

// Copies the KMB (bound, service_type) variants for an exact route name into
// `out`, sequence order preserved. Returns the number written, which is 0 for
// a route KMB does not serve. Never allocates.
size_t transit_route_kmb_variants(const char *name, size_t length,
                                  transit_route_variant_t *out, size_t capacity);

#ifdef __cplusplus
}
#endif
