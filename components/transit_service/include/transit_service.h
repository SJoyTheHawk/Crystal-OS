/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

// OS-owned transit data service. No LVGL, no C++, no UI concepts: this is the
// layer Phase 16 puts http.fetch behind and Phase 17 binds Lua to, and a C
// header with handles and PODs is what survives that.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "transit_routes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TRANSIT_MAX_STOPS      200   // §3.3 cap; also the route handle's capacity
#define TRANSIT_MAX_VARIANTS   8     // chooser rows; KMB tops out at 7 today
#define TRANSIT_MAX_ETA        3
#define TRANSIT_STOP_ID_LEN    20
#define TRANSIT_NAME_LEN       48
#define TRANSIT_PLACE_LEN      40
#define TRANSIT_REMARK_LEN     32
// Stop names resolved per request. This is a whole route, not a screenful: the
// cost that matters is the TLS handshake, so one request holding one connection
// beats several that each pay for their own. Progress arrives as non-final
// events while it runs.
#define TRANSIT_BATCH_MAX      TRANSIT_MAX_STOPS

typedef enum { TRANSIT_OP_KMB = 0, TRANSIT_OP_CTB = 1 } transit_operator_t;

typedef enum {
    TRANSIT_REQ_VARIANTS = 1,   // exact-name lookup across both operators
    TRANSIT_REQ_STOPS,          // route-stop list into the route handle
    TRANSIT_REQ_STOP_NAMES,     // bounded batch of /stop/{id} lookups
    TRANSIT_REQ_ETA,
    TRANSIT_REQ_NEAREST,        // resolve every stop's coordinates, then rank
} transit_request_kind_t;

typedef enum {
    TRANSIT_STATUS_OK = 0,
    TRANSIT_STATUS_EMPTY,        // the API answered, with nothing in it
    TRANSIT_STATUS_OFFLINE,      // no usable network
    TRANSIT_STATUS_HTTP_ERROR,   // reached the server, it refused
    TRANSIT_STATUS_PARSE_ERROR,  // reached the server, could not read it
    TRANSIT_STATUS_CANCELLED,
    TRANSIT_STATUS_BUSY,         // queue full; nothing was submitted
} transit_status_t;

// A route variant is the direction identity. Both bound and service_type are
// required for KMB; CTB uses bound with service_type 1.
typedef struct {
    char    route[TRANSIT_ROUTE_NAME_LEN + 1];
    uint8_t op;                          // transit_operator_t
    char    bound;                       // 'O' or 'I'
    uint8_t service_type;
    char    orig[TRANSIT_PLACE_LEN];
    char    dest[TRANSIT_PLACE_LEN];
} transit_variant_t;

typedef struct {
    char     stop_id[TRANSIT_STOP_ID_LEN];
    uint16_t seq;
    char     name[TRANSIT_NAME_LEN];   // empty until resolved
    float    lat, lon;                 // 0 until resolved
} transit_stop_t;

typedef struct {
    int32_t eta_epoch;                 // 0 when the entry carried no time
    char    remark[TRANSIT_REMARK_LEN];
} transit_eta_t;

// Opaque, service-owned, PSRAM-backed. Released by transit_route_release().
typedef struct transit_route_handle transit_route_handle_t;

typedef struct {
    uint32_t               request_id;
    transit_request_kind_t kind;
    transit_status_t       status;
    union {
        struct {
            size_t            count;
            transit_variant_t items[TRANSIT_MAX_VARIANTS];
        } variants;
        struct {
            transit_route_handle_t *route;   // caller owns it from here
            size_t                  count;
        } stops;
        struct {
            size_t   first_index;   // window this tick filled, into the route
            size_t   count;
            bool     final;         // false for progress ticks; more are coming
        } names;
        struct {
            char         stop_id[TRANSIT_STOP_ID_LEN];
            size_t       count;
            transit_eta_t items[TRANSIT_MAX_ETA];
            int32_t      fetched_at;
        } eta;
        struct {
            size_t resolved;      // stops whose coordinates are known
            size_t total;
            size_t nearest_index; // valid when status is OK
            bool   final;         // false for progress ticks
        } nearest;
    } data;
} transit_event_t;

// Called on the LVGL task, from the app's own drain of transit_service_poll().
typedef void (*transit_listener_t)(const transit_event_t *event, void *user_data);

// Installs the single listener. Passing NULL clears it and drops every result
// still queued, releasing any route handle those results carried.
void transit_service_set_listener(transit_listener_t listener, void *user_data);

// Drains one queued result. Returns false when the queue is empty.
bool transit_service_poll(transit_event_t *event);

// Drains every queued result to the installed listener. Returns the number
// delivered. The service creates no lv_timer of its own -- the app calls this
// from its own timer -- which is how this component avoids linking LVGL while
// still guaranteeing results arrive on the LVGL task.
size_t transit_service_dispatch(void);

// Each returns a monotonic request id, or 0 when nothing was submitted. None of
// them block: a full queue rejects, because the LVGL task must never wait on
// the worker.
uint32_t transit_service_find_variants(const char *route);
uint32_t transit_service_load_stops(const transit_variant_t *variant);
uint32_t transit_service_load_stop_names(transit_route_handle_t *route,
                                         size_t first_index, size_t count);
uint32_t transit_service_load_eta(const transit_variant_t *variant, const char *stop_id);
uint32_t transit_service_find_nearest(transit_route_handle_t *route,
                                      double latitude, double longitude);

// Honoured within one in-flight request; does not abort a socket mid-read.
void transit_service_cancel(uint32_t request_id);
void transit_service_cancel_all(void);

// Read-only views of a route handle, safe on the LVGL task once the handle has
// been delivered to the app.
size_t transit_route_stop_count(const transit_route_handle_t *route);
const transit_stop_t *transit_route_stop(const transit_route_handle_t *route, size_t index);
void transit_route_release(transit_route_handle_t *route);

// The app is on screen. The index refresh never runs while this is true, and
// never on the boot path.
void transit_service_set_foreground(bool foreground);

// Phase 12's OTA worker calls this where it pauses weather.
void transit_service_suspend(bool suspended);

// Epoch of the last successful route-list confirmation, or 0 when never. Drives
// the Device Status "Route list" row.
int32_t transit_service_index_checked_at(void);

#ifdef __cplusplus
}
#endif
