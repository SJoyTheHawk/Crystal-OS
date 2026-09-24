/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "bus_service.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BUS_ROUTE_CHARSET "0123456789ABCDEFGHKMNOPRSTWX"
#define BUS_ROUTE_CHARSET_LEN 28
typedef struct { char name[4]; uint8_t ops; } bus_route_name_t;
extern const bus_route_name_t bus_route_index[];
extern const uint16_t bus_route_index_count;
uint32_t bus_route_next_mask(const char *prefix, size_t prefix_len);
uint8_t bus_route_is_complete(const char *name, size_t len);
#ifdef __cplusplus
}
#endif
