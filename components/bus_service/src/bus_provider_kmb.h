#pragma once

#include <stddef.h>
#include <stdint.h>

#include "bus_service.h"

#ifdef __cplusplus
extern "C" {
#endif

// Parse one KMB stop-detail response. Missing localized names are valid.
// Empty data/features returns ESP_ERR_NOT_FOUND. Coordinates are retained
// only when both values are finite and within geographic bounds.
esp_err_t bus_kmb_parse_stop_metadata(const uint8_t *body, size_t len,
                                      const char *requested_stop_id,
                                      bus_stop_metadata_t *out);

#ifdef __cplusplus
}
#endif
