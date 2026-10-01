#pragma once

#include <stddef.h>
#include <stdint.h>

#include "bus_service.h"

#ifdef __cplusplus
extern "C" {
#endif

// Parse one CTB route-stop response. A successful empty response returns
// ESP_OK with *out == NULL and *count == 0; the caller decides whether that
// is a successful service event. Non-empty output is PSRAM-owned by the
// caller and must be released with free() after delivery.
esp_err_t bus_ctb_parse_route_stops(const uint8_t *body,
                                     size_t len,
                                     const char *route,
                                     bus_direction_t bound,
                                     uint8_t service_type,
                                     bus_stop_t **out,
                                     uint16_t *count);

// Parse a CTB route-list response. A route filter may be NULL or empty for
// the complete list. Records without an explicit direction produce empty
// inbound/outbound identities; terminal fields are copied only for records
// carrying that direction. Non-empty output is PSRAM-owned by the caller and
// must be released with free().
esp_err_t bus_ctb_parse_route_variants(const uint8_t *body,
                                       size_t len,
                                       const char *route_filter,
                                       bus_route_variant_t **out,
                                       uint16_t *count);

#ifdef __cplusplus
}
#endif
