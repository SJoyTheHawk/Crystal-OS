#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Operators
typedef enum {
    BUS_OP_KMB = 0,
    BUS_OP_CTB = 1,
    BUS_OP_NWFB = 2
} bus_operator_t;

// Direction
typedef enum {
    BUS_DIR_OUTBOUND = 'O',
    BUS_DIR_INBOUND = 'I'
} bus_direction_t;

// Route variant (one company + direction)
typedef struct {
    char route[5];              // "68X", NUL-terminated, max 4 chars
    uint8_t op;                 // bus_operator_t
    char bound;                 // 'O' or 'I'
    uint8_t service_type;       // KMB only; always 1 for CTB
    char orig_en[48];
    char dest_en[48];
} bus_route_variant_t;

// Stop info (lazy-loaded)
typedef struct {
    char stop_id[20];
    uint16_t seq;               // Sequence number on route (1-indexed)
    char name_en[60];
    float lat, lon;
    bool resolved;              // Name fetched flag
} bus_stop_t;

// ETA entry
typedef struct {
    uint32_t eta_epoch;
    int32_t minutes_left;
    char remark_en[32];
} bus_eta_t;

// ETA result (up to 3 next buses)
typedef struct {
    bus_eta_t entries[3];
    uint8_t count;
    uint32_t fetched_at;
} bus_eta_result_t;

// Event types
typedef enum {
    BUS_EVT_ROUTE_VARIANTS,
    BUS_EVT_STOPS_LIST,
    BUS_EVT_STOP_DETAIL,
    BUS_EVT_ETA,
    BUS_EVT_ROUTE_CATALOG,
    BUS_EVT_ERROR
} bus_event_type_t;

// Event payload
typedef struct {
    bus_event_type_t type;
    uint32_t request_id;
    esp_err_t status;
    union {
        struct {
            bus_route_variant_t *variants;
            uint8_t count;
        } route_variants;
        struct {
            bus_stop_t *stops;
            uint16_t count;
        } stops_list;
        struct {
            uint16_t seq;
            bus_stop_t stop;
        } stop_detail;
        struct {
            char stop_id[20];
            bus_eta_result_t result;
        } eta;
        struct {
            uint16_t route_count;
            uint8_t providers_succeeded;
            uint8_t providers_failed;
        } route_catalog;
        struct {
            char message[64];
        } error;
    } data;
} bus_event_t;

// Listener callback (called on LVGL task)
typedef void (*bus_listener_t)(const bus_event_t *event, void *user_data);

// Initialize service (creates worker task)
void bus_service_init(void);

// Set event listener
void bus_service_set_listener(bus_listener_t cb, void *user_data);

// Request route variants
uint32_t bus_service_request_route(const char *route_name);

// Fetch and persist the complete searchable route catalog from active
// providers. A cache is loaded during service initialization when available.
uint32_t bus_service_request_route_catalog(void);

// True only when a complete KMB + CTB catalog is loaded in memory.
bool bus_service_route_catalog_ready(void);

// Request stops for a route
uint32_t bus_service_request_stops(const char *route,
                                     uint8_t op,
                                     char bound,
                                     uint8_t service_type);

// Request stop detail (name + coords)
uint32_t bus_service_request_stop_detail(const char *stop_id, uint8_t op);

// Request ETA
uint32_t bus_service_request_eta(const char *stop_id,
                                   const char *route,
                                   uint8_t op,
                                   uint8_t service_type);

// Cancel all pending requests
void bus_service_cancel_all(void);

#ifdef __cplusplus
}
#endif
