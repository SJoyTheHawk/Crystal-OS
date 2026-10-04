#pragma once

#include <stddef.h>
#include <stdint.h>

#include "bus_service.h"

esp_err_t bus_parse_stop_metadata_fields(const uint8_t *body, size_t len,
                                         const char *requested_stop_id,
                                         bus_operator_t op,
                                         const char *stop_field,
                                         const char *name_en_field,
                                         const char *name_tc_field,
                                         const char *lat_field,
                                         const char *lon_field,
                                         bus_stop_metadata_t *out);
