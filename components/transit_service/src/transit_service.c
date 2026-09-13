/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

#include "transit_service.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "transit_http.h"
#include "transit_index.h"
#include "transit_json.h"

static const char *TAG = "transit";

#define KMB_BASE "https://data.etabus.gov.hk/v1/transport/kmb"
#define CTB_BASE "https://rt.data.gov.hk/v2/transport/citybus"

// Both feeds are HTTPS. There is no http:// fallback: these carry no secrets,
// but a plaintext request to data.gov.hk is answered by a redirect this client
// does not follow, so it reads as a silent failure rather than an insecure win.

struct transit_route_handle {
    transit_variant_t variant;
    size_t            count;
    transit_stop_t    stops[TRANSIT_MAX_STOPS];
};

typedef struct {
    uint32_t                id;
    uint32_t                generation;   // cancel epoch this request was made in
    transit_request_kind_t  kind;
    transit_variant_t       variant;
    char                    stop_id[TRANSIT_STOP_ID_LEN];
    char                    route[TRANSIT_ROUTE_NAME_LEN + 1];
    transit_route_handle_t *route_handle;
    size_t                  first_index;
    size_t                  count;
    double                  latitude;
    double                  longitude;
} request_t;

static QueueHandle_t      s_requests;
static QueueHandle_t      s_events;
static TaskHandle_t       s_worker;
static transit_listener_t s_listener;
static void              *s_listener_data;
static volatile uint32_t  s_cancel_id;       // a single request to abandon
// Bumped by cancel_all(). A request whose generation is behind this was submitted
// before the cancel and is abandoned; one submitted after is not. A sticky
// "cancel everything" flag would have silently killed every later request.
static volatile uint32_t  s_cancel_generation;
static volatile bool      s_suspended;
static volatile bool      s_foreground;
static uint32_t           s_next_id = 1;
static volatile uint32_t  s_active_id;
// Held by the worker for as long as it is reading a caller's route handle, and
// taken by transit_route_release(). Without it, an app tearing down mid-batch
// frees 15 KB of PSRAM that a stop-name request is still writing into -- the
// same class of bug as a late event reaching a freed widget, but on the worker
// side where the app's null checks cannot see it.
static SemaphoreHandle_t  s_route_mux;

// ---------------------------------------------------------------- cancellation

static bool request_cancelled(const request_t *request)
{
    if (request->generation != s_cancel_generation) return true;
    const uint32_t cancel = s_cancel_id;
    return cancel != 0 && cancel == request->id;
}

static bool cancel_poll(void *user_data)
{
    return request_cancelled((const request_t *)user_data);
}

// ------------------------------------------------------------------- delivery

static void emit(const transit_event_t *event)
{
    if (s_events == NULL) return;
    if (xQueueSend(s_events, event, 0) != pdTRUE) {
        // The drain timer runs at 250 ms and the queue holds eight, so this only
        // happens if the LVGL task has stalled. Releasing the handle here keeps
        // a dropped event from leaking 15 KB of PSRAM.
        ESP_LOGW(TAG, "event queue full; dropping result for request %lu",
                 (unsigned long)event->request_id);
        if (event->kind == TRANSIT_REQ_STOPS && event->data.stops.route != NULL) {
            transit_route_release(event->data.stops.route);
        }
    }
}

static void emit_status(const request_t *request, transit_status_t status)
{
    transit_event_t event = {
        .request_id = request->id,
        .kind = request->kind,
        .status = status,
    };
    emit(&event);
}

// ------------------------------------------------------------ variant lookup

// KMB /route/{route}/{direction}/{service_type} returns one object. CTB
// /route/CTB/{route} likewise. Both are small and flat, so a single-object
// collector is enough for either.
typedef struct {
    char orig[TRANSIT_PLACE_LEN];
    char dest[TRANSIT_PLACE_LEN];
    char route[TRANSIT_ROUTE_NAME_LEN + 1];
    char bound;
    bool have_route;
} detail_t;

static bool detail_member(const transit_json_member_t *member, void *user_data)
{
    detail_t *detail = user_data;
    if (strcmp(member->key, "orig_en") == 0) strlcpy(detail->orig, member->value, sizeof(detail->orig));
    else if (strcmp(member->key, "dest_en") == 0) strlcpy(detail->dest, member->value, sizeof(detail->dest));
    else if (strcmp(member->key, "route") == 0) {
        strlcpy(detail->route, member->value, sizeof(detail->route));
        detail->have_route = true;
    } else if (strcmp(member->key, "bound") == 0 || strcmp(member->key, "dir") == 0) {
        if (member->value[0] != '\0') detail->bound = member->value[0];
    }
    return true;
}

static transit_status_t fetch_detail(transit_client_t *client, const char *url, detail_t *detail)
{
    memset(detail, 0, sizeof(*detail));
    transit_json_parser_t parser;
    transit_json_init(&parser, detail_member, NULL, detail);
    const transit_status_t status = transit_http_get_json(client, url, &parser, NULL, NULL);
    if (status != TRANSIT_STATUS_OK) return status;
    // KMB answers 200 with "data":{} for a route it does not serve, so presence
    // of the route field is what makes a variant real. Never invent one.
    return detail->have_route ? TRANSIT_STATUS_OK : TRANSIT_STATUS_EMPTY;
}

static void do_find_variants(const request_t *request)
{
    transit_event_t event = {
        .request_id = request->id,
        .kind = TRANSIT_REQ_VARIANTS,
        .status = TRANSIT_STATUS_EMPTY,
    };
    size_t count = 0;
    bool network_ok = false;
    transit_status_t worst = TRANSIT_STATUS_EMPTY;

    // KMB first, driven by the compiled index: the API has no endpoint that
    // lists a route's variants, so the (bound, service_type) pairs come from the
    // index and each is confirmed against the live feed.
    transit_route_variant_t known[TRANSIT_MAX_VARIANTS];
    const size_t variants = transit_route_kmb_variants(request->route, strlen(request->route),
                                                       known, TRANSIT_MAX_VARIANTS);
    transit_client_t *client = transit_client_open(KMB_BASE "/route/");
    for (size_t i = 0; i < variants && count < TRANSIT_MAX_VARIANTS; ++i) {
        if (request_cancelled(request)) { transit_client_close(client); emit_status(request, TRANSIT_STATUS_CANCELLED); return; }
        char url[220];
        snprintf(url, sizeof(url), KMB_BASE "/route/%s/%s/%u", request->route,
                 known[i].bound == 'I' ? "inbound" : "outbound",
                 (unsigned)known[i].service_type);
        detail_t detail;
        const transit_status_t status = fetch_detail(client, url, &detail);
        if (status == TRANSIT_STATUS_OK) {
            network_ok = true;
            transit_variant_t *out = &event.data.variants.items[count++];
            memset(out, 0, sizeof(*out));
            strlcpy(out->route, detail.route, sizeof(out->route));
            out->op = TRANSIT_OP_KMB;
            out->bound = detail.bound != '\0' ? detail.bound : known[i].bound;
            out->service_type = known[i].service_type;
            strlcpy(out->orig, detail.orig, sizeof(out->orig));
            strlcpy(out->dest, detail.dest, sizeof(out->dest));
        } else if (status == TRANSIT_STATUS_EMPTY) {
            network_ok = true;   // the index is stale for this variant, not offline
        } else {
            worst = status;
        }
    }
    transit_client_close(client);

    // Citybus, always tried: the index may not know a brand new route, and the
    // live API is the authority. One object, two directions.
    if (count < TRANSIT_MAX_VARIANTS && !request_cancelled(request)) {
        char url[220];
        snprintf(url, sizeof(url), CTB_BASE "/route/CTB/%s", request->route);
        detail_t detail;
        // Its own client, opened on the CTB host. Passing NULL here made
        // transit_http open and tear one down per call, which is the same
        // handshake with an extra allocation.
        transit_client_t *ctb = transit_client_open(url);
        const transit_status_t status = fetch_detail(ctb, url, &detail);
        transit_client_close(ctb);
        if (status == TRANSIT_STATUS_OK) {
            network_ok = true;
            const char bounds[2] = {'O', 'I'};
            for (size_t i = 0; i < 2 && count < TRANSIT_MAX_VARIANTS; ++i) {
                transit_variant_t *out = &event.data.variants.items[count++];
                memset(out, 0, sizeof(*out));
                strlcpy(out->route, detail.route, sizeof(out->route));
                out->op = TRANSIT_OP_CTB;
                out->bound = bounds[i];
                out->service_type = 1;
                // Inbound is the same pair of places, read the other way round.
                strlcpy(out->orig, i == 0 ? detail.orig : detail.dest, sizeof(out->orig));
                strlcpy(out->dest, i == 0 ? detail.dest : detail.orig, sizeof(out->dest));
            }
        } else if (status == TRANSIT_STATUS_EMPTY) {
            network_ok = true;
        } else {
            worst = status;
        }
    }

    event.data.variants.count = count;
    if (count > 0) event.status = TRANSIT_STATUS_OK;
    else if (!network_ok) event.status = worst;   // could not reach either feed
    else event.status = TRANSIT_STATUS_EMPTY;     // both answered, neither serves it
    emit(&event);
}

// ------------------------------------------------------------------ route stops

typedef struct {
    transit_route_handle_t *route;
    char                    stop_id[TRANSIT_STOP_ID_LEN];
    uint16_t                seq;
    bool                    overflowed;
} stops_ctx_t;

static bool stops_member(const transit_json_member_t *member, void *user_data)
{
    stops_ctx_t *ctx = user_data;
    // seq is a quoted string on KMB and a bare number on CTB, so both types are
    // accepted for the same key.
    if (strcmp(member->key, "stop") == 0) strlcpy(ctx->stop_id, member->value, sizeof(ctx->stop_id));
    else if (strcmp(member->key, "seq") == 0) ctx->seq = (uint16_t)atoi(member->value);
    return true;
}

static bool stops_object_end(uint8_t depth, bool in_array, void *user_data)
{
    (void)depth;
    stops_ctx_t *ctx = user_data;
    // Only elements of the "data" array are records.
    if (!in_array || ctx->stop_id[0] == '\0') return true;
    if (ctx->route->count < TRANSIT_MAX_STOPS) {
        transit_stop_t *stop = &ctx->route->stops[ctx->route->count++];
        memset(stop, 0, sizeof(*stop));
        strlcpy(stop->stop_id, ctx->stop_id, sizeof(stop->stop_id));
        stop->seq = ctx->seq != 0 ? ctx->seq : (uint16_t)ctx->route->count;
    } else {
        ctx->overflowed = true;
    }
    ctx->stop_id[0] = '\0';
    ctx->seq = 0;
    return true;
}

static void do_load_stops(const request_t *request)
{
    // 200 stops x 76 bytes is 15.2 KB, which belongs in PSRAM rather than the
    // internal heap a TLS session is competing for.
    transit_route_handle_t *route = heap_caps_calloc(1, sizeof(*route), MALLOC_CAP_SPIRAM);
    if (route == NULL) route = calloc(1, sizeof(*route));   // no PSRAM: still try
    if (route == NULL) {
        emit_status(request, TRANSIT_STATUS_OFFLINE);
        return;
    }
    route->variant = request->variant;

    char url[260];
    if (request->variant.op == TRANSIT_OP_KMB) {
        snprintf(url, sizeof(url), KMB_BASE "/route-stop/%s/%s/%u", request->variant.route,
                 request->variant.bound == 'I' ? "inbound" : "outbound",
                 (unsigned)request->variant.service_type);
    } else {
        snprintf(url, sizeof(url), CTB_BASE "/route-stop/CTB/%s/%s", request->variant.route,
                 request->variant.bound == 'I' ? "inbound" : "outbound");
    }

    stops_ctx_t ctx = {.route = route};
    transit_json_parser_t parser;
    transit_json_init(&parser, stops_member, stops_object_end, &ctx);
    const transit_status_t status = transit_http_get_json(NULL, url, &parser,
                                                          cancel_poll, (void *)request);
    if (status != TRANSIT_STATUS_OK || route->count == 0) {
        transit_route_release(route);
        // A real suspended route lists no stops, which is not an error.
        emit_status(request, status == TRANSIT_STATUS_OK ? TRANSIT_STATUS_EMPTY : status);
        return;
    }
    if (ctx.overflowed) {
        ESP_LOGW(TAG, "route %s truncated at %d stops", request->variant.route, TRANSIT_MAX_STOPS);
    }

    transit_event_t event = {
        .request_id = request->id,
        .kind = TRANSIT_REQ_STOPS,
        .status = TRANSIT_STATUS_OK,
    };
    event.data.stops.route = route;   // ownership passes to the app
    event.data.stops.count = route->count;
    emit(&event);
}

// -------------------------------------------------------------- stop details

typedef struct {
    char  name[TRANSIT_NAME_LEN];
    float lat, lon;
    bool  have_name;
} stop_detail_t;

static bool stop_detail_member(const transit_json_member_t *member, void *user_data)
{
    stop_detail_t *detail = user_data;
    if (strcmp(member->key, "name_en") == 0) {
        strlcpy(detail->name, member->value, sizeof(detail->name));
        detail->have_name = detail->name[0] != '\0';
    } else if (strcmp(member->key, "lat") == 0) detail->lat = strtof(member->value, NULL);
    else if (strcmp(member->key, "long") == 0) detail->lon = strtof(member->value, NULL);
    return true;
}

// Resolves one stop into the handle. Returns the transport status so a caller can
// tell "server said no" from "network is gone".
static transit_status_t resolve_stop(transit_client_t *client, transit_route_handle_t *route,
                                     size_t index, const request_t *request)
{
    transit_stop_t *stop = &route->stops[index];
    char url[220];
    if (route->variant.op == TRANSIT_OP_KMB) {
        snprintf(url, sizeof(url), KMB_BASE "/stop/%s", stop->stop_id);
    } else {
        snprintf(url, sizeof(url), CTB_BASE "/stop/%s", stop->stop_id);
    }

    stop_detail_t detail = {0};
    transit_json_parser_t parser;
    transit_json_init(&parser, stop_detail_member, NULL, &detail);
    const transit_status_t status = transit_http_get_json(client, url, &parser,
                                                          cancel_poll, (void *)request);
    if (status != TRANSIT_STATUS_OK) return status;
    if (detail.have_name) strlcpy(stop->name, detail.name, sizeof(stop->name));
    stop->lat = detail.lat;
    stop->lon = detail.lon;
    return TRANSIT_STATUS_OK;
}

static void do_load_stop_names(const request_t *request)
{
    transit_route_handle_t *route = request->route_handle;
    if (route == NULL) { emit_status(request, TRANSIT_STATUS_EMPTY); return; }

    const size_t first = request->first_index < route->count ? request->first_index : route->count;
    size_t end = first + request->count;
    if (end > route->count) end = route->count;

    // One kept-alive connection for the whole window, with progress emitted as
    // names land. The window used to be 12 and the app re-requested after each
    // batch, which cost a fresh TLS session per 12 stops: ~700 ms of handshake
    // against ~1.2 s of actual work, and a 50-stop route showed "Stop 13" through
    // "Stop 50" for seconds at a time. Names now stream in on one handshake.
    transit_client_t *client = transit_client_open(KMB_BASE "/stop/");
    size_t resolved = 0;
    size_t pending_first = first;   // start of the not-yet-announced run
    transit_status_t worst = TRANSIT_STATUS_OK;
    for (size_t i = first; i < end; ++i) {
        if (request_cancelled(request)) { worst = TRANSIT_STATUS_CANCELLED; break; }
        if (route->stops[i].name[0] != '\0') { ++resolved; continue; }   // cached
        const transit_status_t status = resolve_stop(client, route, i, request);
        if (status == TRANSIT_STATUS_OK) ++resolved;
        else if (status == TRANSIT_STATUS_OFFLINE || status == TRANSIT_STATUS_CANCELLED) {
            worst = status;
            break;   // stop hammering a connection that is gone
        }

        // Announce in fours. One event per stop would put 50 round trips through
        // the queue and repaint the list 50 times for no visible gain.
        if (i - pending_first >= 3) {
            transit_event_t tick = {
                .request_id = request->id,
                .kind = TRANSIT_REQ_STOP_NAMES,
                .status = TRANSIT_STATUS_OK,
            };
            tick.data.names.first_index = pending_first;
            tick.data.names.count = i - pending_first + 1;
            tick.data.names.final = false;
            emit(&tick);
            pending_first = i + 1;
        }
    }
    transit_client_close(client);

    transit_event_t event = {
        .request_id = request->id,
        .kind = TRANSIT_REQ_STOP_NAMES,
        .status = resolved > 0 ? TRANSIT_STATUS_OK : (worst == TRANSIT_STATUS_OK ? TRANSIT_STATUS_EMPTY : worst),
    };
    // The final event covers whatever the last tick did not, so every row is
    // repainted exactly once.
    event.data.names.first_index = pending_first;
    event.data.names.count = end > pending_first ? end - pending_first : 0;
    event.data.names.final = true;
    emit(&event);
}

// ---------------------------------------------------------------------- ETA

typedef struct {
    transit_eta_t items[TRANSIT_MAX_ETA];
    size_t        count;
    char          eta[40];
    char          remark[TRANSIT_REMARK_LEN];
    bool          overflow;
} eta_ctx_t;

static bool eta_member(const transit_json_member_t *member, void *user_data)
{
    eta_ctx_t *ctx = user_data;
    if (strcmp(member->key, "eta") == 0) {
        // A null eta is a scheduled departure with no live time; keep the remark
        // and drop the entry at object end.
        strlcpy(ctx->eta, member->type == TRANSIT_JSON_STRING ? member->value : "", sizeof(ctx->eta));
    } else if (strcmp(member->key, "rmk_en") == 0) {
        strlcpy(ctx->remark, member->value, sizeof(ctx->remark));
    }
    return true;
}

static bool eta_object_end(uint8_t depth, bool in_array, void *user_data)
{
    (void)depth;
    eta_ctx_t *ctx = user_data;
    if (!in_array) return true;
    const int32_t epoch = transit_parse_iso8601(ctx->eta);
    if (epoch != 0) {
        if (ctx->count < TRANSIT_MAX_ETA) {
            ctx->items[ctx->count].eta_epoch = epoch;
            strlcpy(ctx->items[ctx->count].remark, ctx->remark, TRANSIT_REMARK_LEN);
            ctx->count++;
        } else {
            ctx->overflow = true;
        }
    }
    ctx->eta[0] = '\0';
    ctx->remark[0] = '\0';
    // Keep reading: the feed is not guaranteed sorted, so all entries are seen
    // and the earliest three are kept below.
    return true;
}

static void do_load_eta(const request_t *request)
{
    char url[300];
    if (request->variant.op == TRANSIT_OP_KMB) {
        snprintf(url, sizeof(url), KMB_BASE "/eta/%s/%s/%u", request->stop_id,
                 request->variant.route, (unsigned)request->variant.service_type);
    } else {
        snprintf(url, sizeof(url), CTB_BASE "/eta/CTB/%s/%s", request->stop_id,
                 request->variant.route);
    }

    eta_ctx_t ctx = {0};
    transit_json_parser_t parser;
    transit_json_init(&parser, eta_member, eta_object_end, &ctx);
    const transit_status_t status = transit_http_get_json(NULL, url, &parser,
                                                          cancel_poll, (void *)request);

    transit_event_t event = {
        .request_id = request->id,
        .kind = TRANSIT_REQ_ETA,
        .status = status,
    };
    strlcpy(event.data.eta.stop_id, request->stop_id, sizeof(event.data.eta.stop_id));
    if (status != TRANSIT_STATUS_OK) { emit(&event); return; }

    // Sort ascending: the board reads top to bottom as next-to-last.
    for (size_t i = 1; i < ctx.count; ++i) {
        transit_eta_t key = ctx.items[i];
        size_t j = i;
        while (j > 0 && ctx.items[j - 1].eta_epoch > key.eta_epoch) {
            ctx.items[j] = ctx.items[j - 1];
            --j;
        }
        ctx.items[j] = key;
    }
    event.status = ctx.count > 0 ? TRANSIT_STATUS_OK : TRANSIT_STATUS_EMPTY;
    event.data.eta.count = ctx.count;
    memcpy(event.data.eta.items, ctx.items, sizeof(ctx.items));
    event.data.eta.fetched_at = (int32_t)time(NULL);
    emit(&event);
}

// ----------------------------------------------------------- nearest stop

static double haversine_m(double lat1, double lon1, double lat2, double lon2)
{
    const double to_rad = 3.14159265358979323846 / 180.0;
    const double dlat = (lat2 - lat1) * to_rad;
    const double dlon = (lon2 - lon1) * to_rad;
    const double a = sin(dlat / 2) * sin(dlat / 2) +
                     cos(lat1 * to_rad) * cos(lat2 * to_rad) * sin(dlon / 2) * sin(dlon / 2);
    return 6371000.0 * 2.0 * asin(sqrt(a < 0 ? 0 : (a > 1 ? 1 : a)));
}

static void do_find_nearest(const request_t *request)
{
    transit_route_handle_t *route = request->route_handle;
    if (route == NULL || route->count == 0) { emit_status(request, TRANSIT_STATUS_EMPTY); return; }

    transit_client_t *client = transit_client_open(KMB_BASE "/stop/");
    size_t resolved = 0;
    transit_status_t failure = TRANSIT_STATUS_OK;

    for (size_t i = 0; i < route->count; ++i) {
        if (request_cancelled(request)) { failure = TRANSIT_STATUS_CANCELLED; break; }
        if (route->stops[i].lat == 0.0f && route->stops[i].lon == 0.0f) {
            const transit_status_t status = resolve_stop(client, route, i, request);
            if (status == TRANSIT_STATUS_OFFLINE || status == TRANSIT_STATUS_CANCELLED) {
                failure = status;
                break;
            }
        }
        if (route->stops[i].lat != 0.0f || route->stops[i].lon != 0.0f) ++resolved;

        // Progress every few stops, so "Checking stops... 34 of 118" moves
        // without one event per request.
        if ((i % 5) == 4) {
            transit_event_t tick = {
                .request_id = request->id,
                .kind = TRANSIT_REQ_NEAREST,
                .status = TRANSIT_STATUS_OK,
            };
            tick.data.nearest.resolved = i + 1;
            tick.data.nearest.total = route->count;
            tick.data.nearest.final = false;
            emit(&tick);
        }
    }
    transit_client_close(client);

    transit_event_t event = {
        .request_id = request->id,
        .kind = TRANSIT_REQ_NEAREST,
        .status = failure != TRANSIT_STATUS_OK ? failure : TRANSIT_STATUS_OK,
    };
    event.data.nearest.resolved = resolved;
    event.data.nearest.total = route->count;
    event.data.nearest.final = true;

    if (failure == TRANSIT_STATUS_OK) {
        double best = 0;
        bool found = false;
        for (size_t i = 0; i < route->count; ++i) {
            if (route->stops[i].lat == 0.0f && route->stops[i].lon == 0.0f) continue;
            const double distance = haversine_m(request->latitude, request->longitude,
                                                route->stops[i].lat, route->stops[i].lon);
            if (!found || distance < best) {
                best = distance;
                event.data.nearest.nearest_index = i;
                found = true;
            }
        }
        if (!found) event.status = TRANSIT_STATUS_EMPTY;
    }
    emit(&event);
}

// -------------------------------------------------------------------- worker

// A default netif with an address is the cheapest honest answer to "are we
// online". The service does not touch hal().wifi: app-facing network state is
// crystal_core's, and this component sits below it.
static bool network_up(void)
{
    esp_netif_t *netif = esp_netif_get_default_netif();
    if (netif == NULL) return false;
    esp_netif_ip_info_t ip = {0};
    return esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0;
}

static void worker_task(void *unused)
{
    (void)unused;
    request_t request;
    for (;;) {
        // A timed wait rather than portMAX_DELAY, so the idle path can run the
        // route-list check. The interval only decides how often the schedule is
        // *considered*; transit_index_maybe_refresh() decides whether anything is
        // due, and it is at most daily.
        if (xQueueReceive(s_requests, &request, pdMS_TO_TICKS(60000)) != pdTRUE) {
            if (!s_suspended) {
                (void)transit_index_maybe_refresh(network_up(), s_foreground);
            }
            continue;
        }

        s_active_id = request.id;
        if (s_suspended || request_cancelled(&request)) {
            // A cancelled stops request never allocated, so there is nothing to
            // release; a cancelled names/nearest request does not own its handle.
            emit_status(&request, TRANSIT_STATUS_CANCELLED);
            s_active_id = 0;
            continue;
        }

        // No IP, no request. This is not an optimisation: esp_netif_init() runs on
        // crystal_core's service task ~1.2 s after boot, and until it has,
        // getaddrinfo() reaches an lwIP whose tcpip thread does not exist and
        // asserts on an invalid mbox rather than returning an error. A request
        // arriving in that window used to panic the device.
        //
        // has_ip, not associated: association lands ~1 s before the DHCP lease, and
        // a fetch in that gap dies in getaddrinfo() -- the same trap crystal_core's
        // weather path documents.
        if (!network_up()) {
            emit_status(&request, TRANSIT_STATUS_OFFLINE);
            s_active_id = 0;
            continue;
        }

        // Only the two kinds that read a caller-owned handle take the mutex, so a
        // variants or ETA request never blocks a release.
        const bool borrows_route = request.kind == TRANSIT_REQ_STOP_NAMES ||
                                   request.kind == TRANSIT_REQ_NEAREST;
        if (borrows_route) xSemaphoreTake(s_route_mux, portMAX_DELAY);

        switch (request.kind) {
        case TRANSIT_REQ_VARIANTS:   do_find_variants(&request); break;
        case TRANSIT_REQ_STOPS:      do_load_stops(&request); break;
        case TRANSIT_REQ_STOP_NAMES: do_load_stop_names(&request); break;
        case TRANSIT_REQ_ETA:        do_load_eta(&request); break;
        case TRANSIT_REQ_NEAREST:    do_find_nearest(&request); break;
        }

        if (borrows_route) xSemaphoreGive(s_route_mux);
        s_active_id = 0;

        // A one-shot cancel is consumed once the request it named is done.
        if (s_cancel_id == request.id) s_cancel_id = 0;
    }
}

static bool ensure_started(void)
{
    if (s_worker != NULL) return true;
    // Created on first use, not at boot: a device whose owner never opens the bus
    // app pays no task and no stack for it.
    s_requests = xQueueCreate(4, sizeof(request_t));
    s_events = xQueueCreate(8, sizeof(transit_event_t));
    if (s_route_mux == NULL) s_route_mux = xSemaphoreCreateMutex();
    if (s_requests == NULL || s_events == NULL || s_route_mux == NULL) {
        ESP_LOGE(TAG, "queue allocation failed");
        return false;
    }
    // Core 0, priority 2, matching crystal_service. 7168 bytes because a TLS
    // handshake plus the scanner's frame needs more than the 4 KB default.
    if (xTaskCreatePinnedToCore(worker_task, "transit", 7168, NULL, 2, &s_worker, 0) != pdPASS) {
        ESP_LOGE(TAG, "worker task creation failed");
        s_worker = NULL;
        return false;
    }
    return true;
}

static uint32_t submit(const request_t *prototype)
{
    if (!ensure_started()) return 0;
    request_t request = *prototype;
    request.id = s_next_id++;
    if (s_next_id == 0) s_next_id = 1;   // 0 means "no request"
    request.generation = s_cancel_generation;
    if (xQueueSend(s_requests, &request, 0) != pdTRUE) {
        ESP_LOGW(TAG, "request queue full; rejecting kind %d", (int)request.kind);
        return 0;
    }
    return request.id;
}

// --------------------------------------------------------------- public API

void transit_service_set_listener(transit_listener_t listener, void *user_data)
{
    s_listener = listener;
    s_listener_data = user_data;
    if (listener != NULL || s_events == NULL) return;

    // Clearing the listener drops every queued result. Route handles in those
    // results would otherwise leak, since the app is gone and cannot free them.
    transit_event_t event;
    while (xQueueReceive(s_events, &event, 0) == pdTRUE) {
        if (event.kind == TRANSIT_REQ_STOPS && event.data.stops.route != NULL) {
            transit_route_release(event.data.stops.route);
        }
    }
}

bool transit_service_poll(transit_event_t *event)
{
    if (s_events == NULL || event == NULL) return false;
    return xQueueReceive(s_events, event, 0) == pdTRUE;
}

size_t transit_service_dispatch(void)
{
    if (s_events == NULL) return 0;
    size_t delivered = 0;
    transit_event_t event;
    while (xQueueReceive(s_events, &event, 0) == pdTRUE) {
        if (s_listener != NULL) {
            s_listener(&event, s_listener_data);
            ++delivered;
        } else if (event.kind == TRANSIT_REQ_STOPS && event.data.stops.route != NULL) {
            // No listener means no owner for the handle this result carries.
            transit_route_release(event.data.stops.route);
        }
    }
    return delivered;
}

uint32_t transit_service_find_variants(const char *route)
{
    if (route == NULL || route[0] == '\0') return 0;
    request_t request = {.kind = TRANSIT_REQ_VARIANTS};
    strlcpy(request.route, route, sizeof(request.route));
    return submit(&request);
}

uint32_t transit_service_load_stops(const transit_variant_t *variant)
{
    if (variant == NULL || variant->route[0] == '\0') return 0;
    request_t request = {.kind = TRANSIT_REQ_STOPS, .variant = *variant};
    return submit(&request);
}

uint32_t transit_service_load_stop_names(transit_route_handle_t *route,
                                         size_t first_index, size_t count)
{
    if (route == NULL || count == 0 || first_index >= route->count) return 0;
    if (count > TRANSIT_BATCH_MAX) count = TRANSIT_BATCH_MAX;
    request_t request = {
        .kind = TRANSIT_REQ_STOP_NAMES,
        .route_handle = route,
        .first_index = first_index,
        .count = count,
    };
    return submit(&request);
}

uint32_t transit_service_load_eta(const transit_variant_t *variant, const char *stop_id)
{
    if (variant == NULL || stop_id == NULL || stop_id[0] == '\0') return 0;
    request_t request = {.kind = TRANSIT_REQ_ETA, .variant = *variant};
    strlcpy(request.stop_id, stop_id, sizeof(request.stop_id));
    return submit(&request);
}

uint32_t transit_service_find_nearest(transit_route_handle_t *route,
                                      double latitude, double longitude)
{
    if (route == NULL || route->count == 0) return 0;
    request_t request = {
        .kind = TRANSIT_REQ_NEAREST,
        .route_handle = route,
        .latitude = latitude,
        .longitude = longitude,
    };
    return submit(&request);
}

void transit_service_cancel(uint32_t request_id)
{
    if (request_id != 0) s_cancel_id = request_id;
}

void transit_service_cancel_all(void)
{
    // Everything already queued or in flight is abandoned; anything submitted
    // after this call runs normally.
    s_cancel_generation++;
    s_cancel_id = 0;
}

size_t transit_route_stop_count(const transit_route_handle_t *route)
{
    return route != NULL ? route->count : 0;
}

const transit_stop_t *transit_route_stop(const transit_route_handle_t *route, size_t index)
{
    if (route == NULL || index >= route->count) return NULL;
    return &route->stops[index];
}

void transit_route_release(transit_route_handle_t *route)
{
    if (route == NULL) return;
    // The worker may still be reading this handle for a names or nearest batch.
    // Cancel first so it stops at its next check, then wait for it to let go
    // rather than freeing underneath it. Cancellation is honoured within one
    // in-flight HTTP request, so this blocks for at most one request.
    if (s_route_mux != NULL) {
        transit_service_cancel_all();
        xSemaphoreTake(s_route_mux, portMAX_DELAY);
        xSemaphoreGive(s_route_mux);
    }
    free(route);
}

void transit_service_set_foreground(bool foreground)
{
    s_foreground = foreground;
}

void transit_service_suspend(bool suspended)
{
    s_suspended = suspended;
}

int32_t transit_service_index_checked_at(void)
{
    return transit_index_checked_at();
}
