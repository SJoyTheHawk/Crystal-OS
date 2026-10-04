#pragma once

#include <stddef.h>
#include <stdint.h>

#include "bus_service.h"

#ifdef __cplusplus
extern "C" {
#endif

// Parse one CTB stop-detail response. The CTB provider is kept separate from
// KMB even though both adapters produce bus_stop_metadata_t.
esp_err_t bus_ctb_parse_stop_metadata(const uint8_t *body, size_t len,
                                      const char *requested_stop_id,
                                      bus_stop_metadata_t *out);

#ifdef __cplusplus
}
#endif
