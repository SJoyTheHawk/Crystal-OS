/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef enum { BUS_OP_KMB = 0, BUS_OP_CTB = 1, BUS_OP_NWFB = 2 } bus_operator_t;
typedef enum { BUS_DIR_OUTBOUND = 'O', BUS_DIR_INBOUND = 'I' } bus_direction_t;

typedef struct {
    char route[5]; uint8_t op; char bound; uint8_t service_type;
    char orig_en[48]; char dest_en[48]; char orig_tc[48]; char dest_tc[48];
} bus_route_variant_t;
typedef struct {
    char stop_id[20]; uint16_t seq; char name_en[60]; char name_tc[60];
    float lat, lon; bool resolved;
} bus_stop_t;
typedef struct { uint32_t eta_epoch; int32_t minutes_left; char remark_en[32]; char remark_tc[32]; } bus_eta_t;
typedef struct { bus_eta_t entries[3]; uint8_t count; uint32_t fetched_at; } bus_eta_result_t;

typedef enum { BUS_EVT_ROUTE_VARIANTS, BUS_EVT_STOPS_LIST, BUS_EVT_STOP_DETAIL, BUS_EVT_ETA, BUS_EVT_ERROR } bus_event_type_t;
typedef struct {
    bus_event_type_t type; uint32_t request_id; esp_err_t status;
    union {
        struct { bus_route_variant_t *variants; uint8_t count; } route_variants;
        struct { bus_stop_t *stops; uint16_t count; } stops_list;
        struct { uint16_t seq; bus_stop_t stop; } stop_detail;
        struct { char stop_id[20]; bus_eta_result_t result; } eta;
        struct { char message[64]; } error;
    } data;
} bus_event_t;
typedef void (*bus_listener_t)(const bus_event_t *, void *);

void bus_service_init(void);
void bus_service_set_listener(bus_listener_t cb, void *user_data);
bool bus_service_poll(bus_event_t *event);
void bus_service_suspend(bool suspended);
void bus_service_cancel_all(void);
uint32_t bus_service_request_routes(const char *route);
uint32_t bus_service_request_stops(const bus_route_variant_t *route);
uint32_t bus_service_request_eta(const char *stop_id, const char *route, uint8_t op, uint8_t service_type);
void bus_service_free(void *data);
#ifdef __cplusplus
}
#endif
