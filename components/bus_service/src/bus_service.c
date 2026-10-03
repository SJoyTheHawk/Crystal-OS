#include "bus_service.h"
#include "bus_routes.h"
#include "bus_provider_ctb.h"
#include "bus_provider_kmb.h"
#include "bus_provider_ctb_stop.h"
#include "bus_stop_catalog.h"
#include "bus_normalize.h"
#include "crystal_network.h"
#include "crystal_http.h"
#include "bus_catalog_sync.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "esp_heap_caps.h"

static const char *TAG = "bus_service";

#define KMB_BASE_URL "https://data.etabus.gov.hk/v1/transport/kmb"
#ifdef CRYSTAL_HTTP_PHASE4_KMB_STOPS_FORCE_FAILURE
#define KMB_STOPS_REQUEST_URL "https://invalid-kmb-stop-test.invalid/route-stop/"
#else
#define KMB_STOPS_REQUEST_URL KMB_BASE_URL "/route-stop/"
#endif
#ifdef CRYSTAL_HTTP_PHASE3_KMB_FORCE_FAILURE
#define KMB_CATALOG_REQUEST_URL "https://invalid-kmb-test.invalid/route/"
#else
#define KMB_CATALOG_REQUEST_URL KMB_BASE_URL "/route/"
#endif
#define CTB_BASE_URL "https://rt.data.gov.hk/v2/transport/citybus"
#ifdef CRYSTAL_HTTP_PHASE5_CTB_ROUTE_FORCE_FAILURE
#define CTB_ROUTE_VARIANT_REQUEST_URL "https://invalid-ctb-route-test.invalid/route/CTB"
#else
#define CTB_ROUTE_VARIANT_REQUEST_URL CTB_BASE_URL "/route/CTB"
#endif
#ifdef CRYSTAL_HTTP_PHASE5_CTB_STOPS_FORCE_FAILURE
#define CTB_STOPS_BASE_URL "https://invalid-ctb-stop-test.invalid"
#else
#define CTB_STOPS_BASE_URL CTB_BASE_URL
#endif
#define ROUTE_CACHE_PATH "/spiffs/bus_route_catalog.bin"
#define ROUTE_CACHE_TEMP_PATH "/spiffs/bus_route_catalog.tmp"
#define ROUTE_CACHE_MAGIC 0x42524331u
#define ROUTE_CACHE_VERSION 8u
#define ROUTE_CACHE_MAX_AGE_SECONDS (7u * 24u * 60u * 60u)
#define BUS_HTTP_TIMEOUT_MS 15000
#define BUS_ROUTE_FETCH_ATTEMPTS 10
#define BUS_STOP_FETCH_ATTEMPTS BUS_ROUTE_FETCH_ATTEMPTS
#define ROUTE_VARIANT_CAPACITY 4096
#define ROUTE_METADATA_CAPACITY 2048
#define KMB_CATALOG_OWNER_ID 0x4B4D4243u // "KMBC"
#define KMB_STOPS_OWNER_ID 0x4B4D5354u // "KMST"
#define CTB_ROUTE_VARIANTS_OWNER_ID 0x43545256u // "CTRV"
#define CTB_STOPS_OWNER_ID 0x43544253u // "CTBS"
#define STOP_CATALOG_OWNER_ID 0x53544D44u // "STMD"
#define KMB_STOP_TIMEOUT_MS 8000
#define KMB_STOP_MAX_ATTEMPTS 3
#define CTB_STOP_TIMEOUT_MS 8000
#define CTB_STOP_MAX_ATTEMPTS 3
// Step 0 live CTB measurements are still deferred; keep this bounded
// provisional limit until inbound and outbound captures establish the margin.
#define CTB_STOP_MAX_BODY_BYTES (64u * 1024u)
#define CTB_ROUTE_VARIANT_MAX_BODY_BYTES (512u * 1024u)
#ifdef CRYSTAL_HTTP_PHASE4_KMB_STOPS_FORCE_BODY_LIMIT
#define KMB_STOP_MAX_BODY_BYTES (1u * 1024u)
#else
#define KMB_STOP_MAX_BODY_BYTES (64u * 1024u)
#endif
#define KMB_CATALOG_MAX_BODY_BYTES (512u * 1024u)
#define KMB_CATALOG_HANDOFF_WAIT_MS (BUS_HTTP_TIMEOUT_MS * BUS_ROUTE_FETCH_ATTEMPTS + 10000u)
#define KMB_STOP_HANDOFF_WAIT_MS (KMB_STOP_TIMEOUT_MS * KMB_STOP_MAX_ATTEMPTS + 5000u)
#define CTB_STOP_HANDOFF_WAIT_MS (CTB_STOP_TIMEOUT_MS * CTB_STOP_MAX_ATTEMPTS + 5000u)
#define CTB_ROUTE_VARIANT_HANDOFF_WAIT_MS \
    (BUS_HTTP_TIMEOUT_MS * BUS_ROUTE_FETCH_ATTEMPTS + 10000u)

_Static_assert(BUS_ROUTE_METADATA_PAIR_CAPACITY == 2,
               "route summaries must remain bounded to two terminal pairs");
_Static_assert(sizeof(((bus_route_metadata_t *)0)->route) == 5,
               "route metadata keys must remain four characters plus NUL");

// Request types
typedef enum {
    REQ_TYPE_ROUTE,
    REQ_TYPE_STOPS,
    REQ_TYPE_STOP_DETAIL,
    REQ_TYPE_ETA,
    REQ_TYPE_ROUTE_CATALOG,
    REQ_TYPE_STOP_CATALOG,
} req_type_t;

// Request structure
typedef struct {
    req_type_t type;
    uint32_t id;
    char route[5];
    char stop_id[20];
    uint8_t op;
    char bound;
    uint8_t service_type;
} bus_request_t;

// Step 3 handoff context. The callback copies response data into owned memory,
// signals the bus worker, and never waits for or touches UI state.
typedef struct {
    uint32_t bus_request_id;
    char kmb_url[128];
    uint32_t framework_request_id;
    SemaphoreHandle_t completed_signal;
    bool completed;
    bool timed_out;
    esp_err_t transport_error;
    int status_code;
    uint8_t attempts;
    uint8_t *body;
    size_t body_len;
    bool cancelled;
} kmb_catalog_handoff_t;

// Phase 4 Step 1 handoff context. The context is intentionally disconnected
// from the live stop request until the framework submission step. The future
// callback will copy the response body into this context and signal the bus
// worker; it must never access LVGL state.
typedef struct {
    uint32_t bus_request_id;
    char route[5];
    char direction[9];
    uint8_t service_type;
    char kmb_url[256];
    uint32_t framework_request_id;
    SemaphoreHandle_t completed_signal;
    bool completed;
    bool timed_out;
    bool cancelled;
    esp_err_t transport_error;
    int status_code;
    uint8_t attempts;
    uint8_t *body;
    size_t body_len;
} kmb_stops_handoff_t;

// Step 3 CTB transport handoff. The framework response is released by the
// callback after its body is copied into this PSRAM-owned buffer. Parsing and
// event delivery remain disconnected until the next step.
typedef struct {
    uint32_t bus_request_id;
    char route[5];
    bus_direction_t bound;
    bus_operator_t op;
    uint8_t service_type;
    uint32_t framework_request_id;
    SemaphoreHandle_t completed_signal;
    bool completed;
    bool timed_out;
    bool cancelled;
    esp_err_t transport_error;
    int status_code;
    uint8_t attempts;
    uint8_t *body;
    size_t body_len;
} ctb_stops_handoff_t;

// Slice 2 route-variant transport handoff. The callback owns only the
// framework response lifetime; the bus worker owns this context and its body.
typedef struct {
    uint32_t bus_request_id;
    uint32_t framework_request_id;
    char route[5];
    bus_operator_t op;
    bus_direction_t bound;
    uint8_t service_type;
    SemaphoreHandle_t completed_signal;
    bool completed;
    bool timed_out;
    bool cancelled;
    esp_err_t transport_error;
    int status_code;
    uint8_t attempts;
    uint8_t *body;
    size_t body_len;
} ctb_route_variants_handoff_t;

// Service state
static TaskHandle_t s_worker_task = NULL;
static QueueHandle_t s_request_queue = NULL;
static bus_listener_t s_listener = NULL;
static void *s_listener_user_data = NULL;
static uint32_t s_next_request_id = 1;
static volatile bool s_cancel_all = false;
static volatile bool s_network_lost = false;
static volatile esp_http_client_handle_t s_active_direct_client = NULL;
static time_t s_route_cache_fetched_at = 0;
static uint8_t s_route_cache_provider_mask = 0;
// Route labels are shared by providers, so this store is deliberately
// provider-neutral. Identity is route + operator + bound + service type.
static bus_route_variant_t *s_route_variants = NULL;
static uint16_t s_route_variant_count = 0;
static bus_route_metadata_t *s_route_metadata = NULL;
static uint16_t s_route_metadata_count = 0;

static void reset_route_metadata_store(void)
{
    free(s_route_metadata);
    s_route_metadata = NULL;
    s_route_metadata_count = 0;
}

// Forward declarations
static void bus_worker_task(void *arg);
static void process_route_request(const bus_request_t *req);
static void process_stops_request(const bus_request_t *req);
static void enrich_route_stops_from_catalog(bus_stop_t *stops, uint16_t count);
static void process_eta_request(const bus_request_t *req);
static void process_route_catalog_request(const bus_request_t *req);
static void process_stop_catalog_request(const bus_request_t *req);
static void post_event(const bus_event_t *event);
static void deliver_event_async(void *user_data);
static void free_event_payload(bus_event_t *event);
static char *normalize_stop_id(char *stop_id);
static bool load_route_catalog_cache(void);
static bool wait_for_network(void);
static kmb_catalog_handoff_t *kmb_catalog_handoff_create(uint32_t bus_request_id,
                                                          const char *kmb_url);
static void kmb_catalog_handoff_cleanup(kmb_catalog_handoff_t *context);
static void kmb_catalog_handoff_callback(const crystal_http_response_t *response,
                                          void *user_data);
static kmb_stops_handoff_t *kmb_stops_handoff_create(const bus_request_t *request,
                                                     const char *direction,
                                                     const char *kmb_url);
static void kmb_stops_handoff_cleanup(kmb_stops_handoff_t *context);
static void kmb_stops_handoff_callback(const crystal_http_response_t *response,
                                       void *user_data);
static kmb_stops_handoff_t *submit_kmb_stops(const bus_request_t *request,
                                             const char *direction,
                                             const char *kmb_url);
static ctb_stops_handoff_t *ctb_stops_handoff_create(const bus_request_t *request);
static void ctb_stops_handoff_cleanup(ctb_stops_handoff_t *context);
static void ctb_stops_handoff_callback(const crystal_http_response_t *response,
                                       void *user_data);
static ctb_stops_handoff_t *submit_ctb_stops(const bus_request_t *request,
                                             const char *ctb_url);
static ctb_route_variants_handoff_t *ctb_route_variants_handoff_create(
    uint32_t bus_request_id, const char *route, bus_operator_t op,
    bus_direction_t bound, uint8_t service_type);
static void ctb_route_variants_handoff_cleanup(
    ctb_route_variants_handoff_t *context);
static void ctb_route_variants_handoff_callback(
    const crystal_http_response_t *response, void *user_data);
static ctb_route_variants_handoff_t *submit_ctb_route_variants(
    uint32_t bus_request_id, const char *route, bus_operator_t op,
    bus_direction_t bound, uint8_t service_type, const char *url);
static uint16_t fetch_ctb_route_provider(const char *label, const char *url,
                                         uint32_t request_id);
static uint16_t submit_kmb_catalog(uint32_t bus_request_id);
static uint16_t resolve_route_provider_json(const char *label, cJSON *root,
                                            uint8_t op, uint32_t request_id);
static uint16_t append_route_variants_from_item(cJSON *item, bus_operator_t op);
static bool append_normalized_route_variant(const bus_route_variant_t *candidate);
static bool append_route_metadata(const bus_route_metadata_t *candidate);
static uint16_t resolve_ctb_route_provider_body(const char *label,
                                                const uint8_t *body,
                                                size_t body_len,
                                                uint32_t request_id);
void bus_stop_catalog_schedule(void);
void bus_stop_catalog_sync_pass(uint32_t request_id);
void bus_stop_catalog_retry_now(void);
bool bus_stop_catalog_load(void);
esp_err_t bus_stop_catalog_lookup(bus_operator_t op, const char *stop_id,
                                  bus_stop_metadata_t *out);
bool bus_stop_catalog_ready(void);
bool bus_stop_catalog_fresh(void);
void bus_stop_catalog_run_diagnostic(void);

static bus_stop_catalog_progress_t s_stop_catalog_progress;

void bus_stop_catalog_report_progress(uint16_t discovered,
                                      uint16_t resolved,
                                      uint16_t pending,
                                      uint16_t failed,
                                      const char *message)
{
    bus_event_t event = {0};
    s_stop_catalog_progress.discovered = discovered;
    s_stop_catalog_progress.resolved = resolved;
    s_stop_catalog_progress.pending = pending;
    s_stop_catalog_progress.failed = failed;
    strlcpy(s_stop_catalog_progress.last_error,
            failed > 0 || pending > 0 ? (message != NULL ? message : "") : "",
            sizeof(s_stop_catalog_progress.last_error));
    event.type = BUS_EVT_STOP_CATALOG_PROGRESS;
    event.status = ESP_OK;
    event.data.stop_catalog_progress.discovered = discovered;
    event.data.stop_catalog_progress.resolved = resolved;
    event.data.stop_catalog_progress.pending = pending;
    event.data.stop_catalog_progress.failed = failed;
    strlcpy(event.data.stop_catalog_progress.message,
            message != NULL ? message : "Stop catalog progress",
            sizeof(event.data.stop_catalog_progress.message));
    post_event(&event);
}

bool bus_service_stop_catalog_get_progress(bus_stop_catalog_progress_t *out)
{
    if (out == NULL) {
        return false;
    }
    *out = s_stop_catalog_progress;
    return true;
}

esp_err_t bus_service_lookup_stop_metadata(bus_operator_t op,
                                            const char *stop_id,
                                            bus_stop_metadata_t *out)
{
    return bus_stop_catalog_lookup(op, stop_id, out);
}

esp_err_t bus_service_lookup_route_stop(const bus_stop_t *route_stop,
                                        bus_stop_metadata_t *out)
{
    if (route_stop == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return bus_stop_catalog_lookup(route_stop->op, route_stop->stop_id, out);
}

bool bus_service_stop_catalog_ready(void)
{
    return bus_stop_catalog_ready();
}

bool bus_service_stop_catalog_fresh(void)
{
    return bus_stop_catalog_fresh();
}

static kmb_catalog_handoff_t *kmb_catalog_handoff_create(uint32_t bus_request_id,
                                                          const char *kmb_url)
{
    kmb_catalog_handoff_t *context = calloc(1, sizeof(*context));
    if (context == NULL) {
        return NULL;
    }
    context->bus_request_id = bus_request_id;
    strlcpy(context->kmb_url, kmb_url, sizeof(context->kmb_url));
    context->completed_signal = xSemaphoreCreateBinary();
    if (context->completed_signal == NULL) {
        free(context);
        return NULL;
    }
    return context;
}

static void kmb_catalog_handoff_cleanup(kmb_catalog_handoff_t *context)
{
    if (context == NULL) return;
    if (context->completed_signal != NULL) {
        vSemaphoreDelete(context->completed_signal);
    }
    free(context->body);
    free(context);
}

static kmb_stops_handoff_t *kmb_stops_handoff_create(const bus_request_t *request,
                                                     const char *direction,
                                                     const char *kmb_url)
{
    if (request == NULL || direction == NULL || kmb_url == NULL) {
        return NULL;
    }

    kmb_stops_handoff_t *context = calloc(1, sizeof(*context));
    if (context == NULL) {
        return NULL;
    }
    context->bus_request_id = request->id;
    strlcpy(context->route, request->route, sizeof(context->route));
    strlcpy(context->direction, direction, sizeof(context->direction));
    context->service_type = request->service_type;
    strlcpy(context->kmb_url, kmb_url, sizeof(context->kmb_url));
    context->completed_signal = xSemaphoreCreateBinary();
    if (context->completed_signal == NULL) {
        free(context);
        return NULL;
    }
    return context;
}

static void kmb_stops_handoff_cleanup(kmb_stops_handoff_t *context)
{
    if (context == NULL) return;
    if (context->completed_signal != NULL) {
        vSemaphoreDelete(context->completed_signal);
    }
    free(context->body);
    free(context);
}

static void kmb_stops_handoff_callback(const crystal_http_response_t *response,
                                       void *user_data)
{
    kmb_stops_handoff_t *context = user_data;
    if (context == NULL) {
        if (response != NULL) crystal_http_response_release(response);
        return;
    }
    if (response != NULL) {
        context->status_code = response->status_code;
        context->transport_error = response->transport_error;
        context->attempts = response->attempts;
        context->body_len = response->body_len;
        if (response->body != NULL && response->body_len > 0) {
            context->body = heap_caps_malloc(response->body_len + 1,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (context->body == NULL) {
                context->body_len = 0;
                context->transport_error = ESP_ERR_NO_MEM;
                ESP_LOGE(TAG, "KMB stops handoff body allocation failed bytes=%u",
                         (unsigned)response->body_len);
            } else {
                memcpy(context->body, response->body, response->body_len);
                context->body[response->body_len] = '\0';
            }
        }
        ESP_LOGI(TAG, "KMB stops diagnostic bus_id=%lu framework_id=%lu route=%s direction=%s status=%d body=%u attempts=%u error=%s",
                 (unsigned long)context->bus_request_id,
                 (unsigned long)context->framework_request_id,
                 context->route, context->direction,
                 response->status_code, (unsigned)response->body_len,
                 (unsigned)response->attempts,
                 esp_err_to_name(response->transport_error));
        crystal_http_response_release(response);
    } else {
        context->transport_error = ESP_FAIL;
        ESP_LOGW(TAG, "KMB stops diagnostic bus_id=%lu framework_id=%lu missing response",
                 (unsigned long)context->bus_request_id,
                 (unsigned long)context->framework_request_id);
    }
    context->completed = true;
    xSemaphoreGive(context->completed_signal);
    if (context->timed_out) {
        kmb_stops_handoff_cleanup(context);
    }
}

static kmb_stops_handoff_t *submit_kmb_stops(const bus_request_t *request,
                                             const char *direction,
                                             const char *kmb_url)
{
    kmb_stops_handoff_t *context = kmb_stops_handoff_create(request, direction, kmb_url);
    if (context == NULL) {
        ESP_LOGE(TAG, "KMB stops diagnostic context allocation failed bus_id=%lu",
                 (unsigned long)(request != NULL ? request->id : 0));
        return NULL;
    }

    crystal_http_options_t options = {
        .url = context->kmb_url,
        .timeout_ms = KMB_STOP_TIMEOUT_MS,
        .max_attempts = KMB_STOP_MAX_ATTEMPTS,
        .retry_backoff_ms = 500,
        .retry_backoff_max_ms = 2000,
        .max_body_bytes = KMB_STOP_MAX_BODY_BYTES,
        .keep_alive = false,
        .owner_id = KMB_STOPS_OWNER_ID,
    };
    context->framework_request_id = crystal_http_get(
        &options, kmb_stops_handoff_callback, context);
    if (context->framework_request_id == 0) {
        ESP_LOGW(TAG, "KMB stops diagnostic queue failed bus_id=%lu",
                 (unsigned long)request->id);
        kmb_stops_handoff_cleanup(context);
        return NULL;
    }
    ESP_LOGI(TAG, "KMB stops submitted route=%s bound=%c bus_id=%lu framework_id=%lu",
             request->route, request->bound,
             (unsigned long)request->id,
             (unsigned long)context->framework_request_id);

    if (xSemaphoreTake(context->completed_signal,
                       pdMS_TO_TICKS(KMB_STOP_HANDOFF_WAIT_MS)) != pdTRUE) {
        context->timed_out = true;
        context->cancelled = crystal_http_cancel(context->framework_request_id);
        ESP_LOGW(TAG, "KMB stops diagnostic timed out bus_id=%lu framework_id=%lu cancelled=%d",
                 (unsigned long)request->id,
                 (unsigned long)context->framework_request_id,
                 context->cancelled);
        return NULL;
    }

    ESP_LOGI(TAG, "KMB stops handoff received bus_id=%lu framework_id=%lu status=%d body=%u attempts=%u error=%s",
             (unsigned long)request->id,
             (unsigned long)context->framework_request_id,
             context->status_code, (unsigned)context->body_len,
             (unsigned)context->attempts, esp_err_to_name(context->transport_error));
    return context;
}

static ctb_stops_handoff_t *ctb_stops_handoff_create(const bus_request_t *request)
{
    if (request == NULL) {
        return NULL;
    }

    ctb_stops_handoff_t *context = calloc(1, sizeof(*context));
    if (context == NULL) {
        return NULL;
    }
    context->bus_request_id = request->id;
    strlcpy(context->route, request->route, sizeof(context->route));
    context->bound = (bus_direction_t)request->bound;
    context->op = (bus_operator_t)request->op;
    context->service_type = request->service_type;
    context->completed_signal = xSemaphoreCreateBinary();
    if (context->completed_signal == NULL) {
        free(context);
        return NULL;
    }
    return context;
}

static void ctb_stops_handoff_cleanup(ctb_stops_handoff_t *context)
{
    if (context == NULL) return;
    if (context->completed_signal != NULL) {
        vSemaphoreDelete(context->completed_signal);
    }
    free(context->body);
    free(context);
}

static void ctb_stops_handoff_callback(const crystal_http_response_t *response,
                                       void *user_data)
{
    ctb_stops_handoff_t *context = user_data;
    if (context == NULL) {
        if (response != NULL) crystal_http_response_release(response);
        return;
    }

    if (response != NULL) {
        context->status_code = response->status_code;
        context->transport_error = response->transport_error;
        context->attempts = response->attempts;
        context->body_len = response->body_len;
        if (response->body != NULL && response->body_len > 0) {
            context->body = heap_caps_malloc(response->body_len + 1,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (context->body == NULL) {
                context->body_len = 0;
                context->transport_error = ESP_ERR_NO_MEM;
                ESP_LOGE(TAG, "CTB stops handoff body allocation failed bytes=%u",
                         (unsigned)response->body_len);
            } else {
                memcpy(context->body, response->body, response->body_len);
                context->body[response->body_len] = '\0';
            }
        }
        ESP_LOGI(TAG, "CTB stops response bus_id=%lu framework_id=%lu route=%s bound=%c op=%u status=%d body=%u attempts=%u error=%s",
                 (unsigned long)context->bus_request_id,
                 (unsigned long)context->framework_request_id,
                 context->route, (char)context->bound, (unsigned)context->op,
                 response->status_code, (unsigned)response->body_len,
                 (unsigned)response->attempts,
                 esp_err_to_name(response->transport_error));
        crystal_http_response_release(response);
    } else {
        context->transport_error = ESP_FAIL;
        ESP_LOGW(TAG, "CTB stops response bus_id=%lu framework_id=%lu missing response",
                 (unsigned long)context->bus_request_id,
                 (unsigned long)context->framework_request_id);
    }

    context->completed = true;
    xSemaphoreGive(context->completed_signal);
    if (context->timed_out) {
        ctb_stops_handoff_cleanup(context);
    }
}

static ctb_stops_handoff_t *submit_ctb_stops(const bus_request_t *request,
                                             const char *ctb_url)
{
    ctb_stops_handoff_t *context = ctb_stops_handoff_create(request);
    if (context == NULL) {
        ESP_LOGE(TAG, "CTB stops handoff context allocation failed bus_id=%lu",
                 (unsigned long)(request != NULL ? request->id : 0));
        return NULL;
    }

    crystal_http_options_t options = {
        .url = ctb_url,
        .timeout_ms = CTB_STOP_TIMEOUT_MS,
        .max_attempts = CTB_STOP_MAX_ATTEMPTS,
        .retry_backoff_ms = 500,
        .retry_backoff_max_ms = 2000,
        .max_body_bytes = CTB_STOP_MAX_BODY_BYTES,
        .keep_alive = false,
        .owner_id = CTB_STOPS_OWNER_ID,
    };
    context->framework_request_id = crystal_http_get(
        &options, ctb_stops_handoff_callback, context);
    if (context->framework_request_id == 0) {
        ESP_LOGW(TAG, "CTB stops queue failed bus_id=%lu",
                 (unsigned long)request->id);
        ctb_stops_handoff_cleanup(context);
        return NULL;
    }

    ESP_LOGI(TAG, "CTB stops submitted route=%s op=%u bound=%c service_type=%u bus_id=%lu framework_id=%lu",
             request->route, (unsigned)request->op, request->bound,
             (unsigned)request->service_type,
             (unsigned long)request->id,
             (unsigned long)context->framework_request_id);

    if (xSemaphoreTake(context->completed_signal,
                       pdMS_TO_TICKS(CTB_STOP_HANDOFF_WAIT_MS)) != pdTRUE) {
        context->timed_out = true;
        context->cancelled = crystal_http_cancel(context->framework_request_id);
        ESP_LOGW(TAG, "CTB stops handoff timed out bus_id=%lu framework_id=%lu cancelled=%d",
                 (unsigned long)request->id,
                 (unsigned long)context->framework_request_id,
                 context->cancelled);
        return NULL;
    }

    ESP_LOGI(TAG, "CTB stops handoff received bus_id=%lu framework_id=%lu status=%d body=%u attempts=%u error=%s",
             (unsigned long)request->id,
             (unsigned long)context->framework_request_id,
             context->status_code, (unsigned)context->body_len,
             (unsigned)context->attempts,
             esp_err_to_name(context->transport_error));
    return context;
}

static ctb_route_variants_handoff_t *ctb_route_variants_handoff_create(
    uint32_t bus_request_id, const char *route, bus_operator_t op,
    bus_direction_t bound, uint8_t service_type)
{
    ctb_route_variants_handoff_t *context = calloc(1, sizeof(*context));
    if (context == NULL) {
        return NULL;
    }
    context->bus_request_id = bus_request_id;
    if (route != NULL) {
        strlcpy(context->route, route, sizeof(context->route));
    }
    context->op = op;
    context->bound = bound;
    context->service_type = service_type;
    context->completed_signal = xSemaphoreCreateBinary();
    if (context->completed_signal == NULL) {
        free(context);
        return NULL;
    }
    return context;
}

static void ctb_route_variants_handoff_cleanup(
    ctb_route_variants_handoff_t *context)
{
    if (context == NULL) return;
    if (context->completed_signal != NULL) {
        vSemaphoreDelete(context->completed_signal);
    }
    free(context->body);
    free(context);
}

static void ctb_route_variants_handoff_callback(
    const crystal_http_response_t *response, void *user_data)
{
    ctb_route_variants_handoff_t *context = user_data;
    if (context == NULL) {
        if (response != NULL) crystal_http_response_release(response);
        return;
    }

    if (response != NULL) {
        context->status_code = response->status_code;
        context->transport_error = response->transport_error;
        context->attempts = response->attempts;
        context->body_len = response->body_len;
        if (response->body != NULL && response->body_len > 0) {
            context->body = heap_caps_malloc(response->body_len + 1,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (context->body == NULL) {
                context->body_len = 0;
                context->transport_error = ESP_ERR_NO_MEM;
                ESP_LOGE(TAG,
                         "CTB route variants handoff body allocation failed bytes=%u",
                         (unsigned)response->body_len);
            } else {
                memcpy(context->body, response->body, response->body_len);
                context->body[response->body_len] = '\0';
            }
        }
        ESP_LOGI(TAG,
                 "CTB route variants response bus_id=%lu framework_id=%lu route=%s op=%u bound=%c service_type=%u status=%d body=%u attempts=%u error=%s",
                 (unsigned long)context->bus_request_id,
                 (unsigned long)context->framework_request_id,
                 context->route, (unsigned)context->op,
                 context->bound == BUS_DIR_INBOUND || context->bound == BUS_DIR_OUTBOUND
                     ? (char)context->bound : '-',
                 (unsigned)context->service_type, response->status_code,
                 (unsigned)response->body_len, (unsigned)response->attempts,
                 esp_err_to_name(response->transport_error));
        // The callback is the sole owner of the framework response release.
        crystal_http_response_release(response);
    } else {
        context->transport_error = ESP_FAIL;
        ESP_LOGW(TAG,
                 "CTB route variants response bus_id=%lu framework_id=%lu missing response",
                 (unsigned long)context->bus_request_id,
                 (unsigned long)context->framework_request_id);
    }

    context->completed = true;
    xSemaphoreGive(context->completed_signal);
    if (context->timed_out) {
        ctb_route_variants_handoff_cleanup(context);
    }
}

static ctb_route_variants_handoff_t *submit_ctb_route_variants(
    uint32_t bus_request_id, const char *route, bus_operator_t op,
    bus_direction_t bound, uint8_t service_type, const char *url)
{
    if (url == NULL || url[0] == '\0') {
        return NULL;
    }
    ctb_route_variants_handoff_t *context =
        ctb_route_variants_handoff_create(bus_request_id, route, op, bound,
                                           service_type);
    if (context == NULL) {
        ESP_LOGE(TAG, "CTB route variants handoff context allocation failed bus_id=%lu",
                 (unsigned long)bus_request_id);
        return NULL;
    }

    crystal_http_options_t options = {
        .url = url,
        .timeout_ms = BUS_HTTP_TIMEOUT_MS,
        .max_attempts = BUS_ROUTE_FETCH_ATTEMPTS,
        .retry_backoff_ms = 500,
        .retry_backoff_max_ms = 8000,
        .max_body_bytes = CTB_ROUTE_VARIANT_MAX_BODY_BYTES,
        .keep_alive = false,
        .owner_id = CTB_ROUTE_VARIANTS_OWNER_ID,
    };
    context->framework_request_id = crystal_http_get(
        &options, ctb_route_variants_handoff_callback, context);
    if (context->framework_request_id == 0) {
        ESP_LOGW(TAG, "CTB route variants queue failed bus_id=%lu",
                 (unsigned long)bus_request_id);
        ctb_route_variants_handoff_cleanup(context);
        return NULL;
    }

    ESP_LOGI(TAG,
             "CTB route variants submitted route=%s op=%u bound=%c service_type=%u bus_id=%lu framework_id=%lu url=%s",
             context->route, (unsigned)context->op,
             context->bound == BUS_DIR_INBOUND || context->bound == BUS_DIR_OUTBOUND
                 ? (char)context->bound : '-',
             (unsigned)context->service_type, (unsigned long)bus_request_id,
             (unsigned long)context->framework_request_id,
             url);

    if (xSemaphoreTake(context->completed_signal,
                       pdMS_TO_TICKS(CTB_ROUTE_VARIANT_HANDOFF_WAIT_MS)) != pdTRUE) {
        context->timed_out = true;
        context->cancelled = crystal_http_cancel(context->framework_request_id);
        ESP_LOGW(TAG,
                 "CTB route variants handoff timed out bus_id=%lu framework_id=%lu cancelled=%d",
                 (unsigned long)bus_request_id,
                 (unsigned long)context->framework_request_id,
                 context->cancelled);
        return NULL;
    }

    ESP_LOGI(TAG,
             "CTB route variants handoff received bus_id=%lu framework_id=%lu status=%d body=%u attempts=%u error=%s",
             (unsigned long)bus_request_id,
             (unsigned long)context->framework_request_id,
             context->status_code, (unsigned)context->body_len,
             (unsigned)context->attempts, esp_err_to_name(context->transport_error));
    return context;
}

static void kmb_catalog_handoff_callback(const crystal_http_response_t *response,
                                          void *user_data)
{
    kmb_catalog_handoff_t *context = user_data;
    if (context == NULL) {
        if (response != NULL) crystal_http_response_release(response);
        return;
    }
    if (response != NULL) {
        context->status_code = response->status_code;
        context->transport_error = response->transport_error;
        context->attempts = response->attempts;
        context->body_len = response->body_len;
        if (response->body != NULL && response->body_len > 0) {
            context->body = heap_caps_malloc(response->body_len,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (context->body == NULL) {
                context->body_len = 0;
                context->transport_error = ESP_ERR_NO_MEM;
                ESP_LOGE(TAG, "KMB catalog handoff body allocation failed bytes=%u",
                         (unsigned)response->body_len);
            } else {
                memcpy(context->body, response->body, response->body_len);
            }
        }
        ESP_LOGI(TAG, "KMB catalog response bus_id=%lu framework_id=%lu status=%d body=%u attempts=%u error=%s",
                 (unsigned long)context->bus_request_id,
                 (unsigned long)context->framework_request_id,
                 response->status_code, (unsigned)response->body_len,
                 (unsigned)response->attempts,
                 esp_err_to_name(response->transport_error));
        crystal_http_response_release(response);
    } else {
        ESP_LOGW(TAG, "KMB catalog response bus_id=%lu framework_id=%lu missing response",
                 (unsigned long)context->bus_request_id,
                 (unsigned long)context->framework_request_id);
    }
    context->completed = true;
    xSemaphoreGive(context->completed_signal);
    if (context->timed_out) {
        kmb_catalog_handoff_cleanup(context);
    }
}

static uint16_t submit_kmb_catalog(uint32_t bus_request_id)
{
    kmb_catalog_handoff_t *context = kmb_catalog_handoff_create(
        bus_request_id, KMB_CATALOG_REQUEST_URL);
    if (context == NULL) {
        ESP_LOGE(TAG, "KMB catalog handoff context allocation failed");
        return 0;
    }
    crystal_http_options_t options = {
        .url = context->kmb_url,
        .timeout_ms = BUS_HTTP_TIMEOUT_MS,
        // KMB retries are owned exclusively by crystal_http. The catalog
        // operation submits once and receives one final callback.
        .max_attempts = BUS_ROUTE_FETCH_ATTEMPTS,
        .retry_backoff_ms = 500,
        .retry_backoff_max_ms = 8000,
        .max_body_bytes = KMB_CATALOG_MAX_BODY_BYTES,
        .keep_alive = false,
        .owner_id = KMB_CATALOG_OWNER_ID,
    };
    context->framework_request_id = crystal_http_get(
        &options, kmb_catalog_handoff_callback, context);
    if (context->framework_request_id == 0) {
        ESP_LOGW(TAG, "KMB catalog queue failed bus_id=%lu",
                 (unsigned long)bus_request_id);
        kmb_catalog_handoff_cleanup(context);
        return 0;
    }
    ESP_LOGI(TAG, "KMB catalog queued bus_id=%lu framework_id=%lu",
             (unsigned long)bus_request_id,
             (unsigned long)context->framework_request_id);

    const TickType_t wait_ticks = pdMS_TO_TICKS(KMB_CATALOG_HANDOFF_WAIT_MS);
    if (xSemaphoreTake(context->completed_signal, wait_ticks) != pdTRUE) {
        context->timed_out = true;
        context->cancelled = crystal_http_cancel(context->framework_request_id);
        ESP_LOGW(TAG, "KMB catalog handoff timed out bus_id=%lu framework_id=%lu cancelled=%d",
                 (unsigned long)bus_request_id,
                 (unsigned long)context->framework_request_id,
                 context->cancelled);
        return 0;
    }

    ESP_LOGI(TAG, "KMB catalog handoff received bus_id=%lu framework_id=%lu status=%d body=%u",
             (unsigned long)bus_request_id,
             (unsigned long)context->framework_request_id,
             context->status_code, (unsigned)context->body_len);
    uint16_t result = 0;
    if (context->body != NULL && context->body_len > 0 &&
        context->status_code == 200 && context->transport_error == ESP_OK) {
        bus_event_t downloaded = {0};
        downloaded.type = BUS_EVT_ROUTE_CATALOG_PROGRESS;
        downloaded.request_id = bus_request_id;
        snprintf(downloaded.data.route_catalog_progress.message,
                 sizeof(downloaded.data.route_catalog_progress.message),
                 "KMB data downloaded\nResolving route data...");
        post_event(&downloaded);
        cJSON *root = cJSON_ParseWithLength((const char *)context->body, context->body_len);
        if (root != NULL) {
            result = resolve_route_provider_json("KMB", root, 1u, bus_request_id);
        } else {
            ESP_LOGW(TAG, "Route catalog: KMB response JSON parse failed");
        }
    }
    kmb_catalog_handoff_cleanup(context);
    return result;
}

void bus_service_init(void)
{
    if (s_worker_task != NULL) {
        return;  // Already initialized
    }

    load_route_catalog_cache();
    const esp_err_t stop_catalog_status = bus_stop_catalog_init();
    if (stop_catalog_status != ESP_OK && stop_catalog_status != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Stop catalog lookup initialization failed: %s",
                 esp_err_to_name(stop_catalog_status));
    }
    if (bus_stop_catalog_load()) {
        bus_stop_catalog_report_progress(
            (uint16_t)(bus_service_stop_catalog_resolved() +
                       bus_service_stop_catalog_failed()),
            bus_service_stop_catalog_resolved(),
            bus_service_stop_catalog_failed(),
            bus_service_stop_catalog_failed(), "Loaded stop catalog");
    }

    // Leave room for catalog bootstrap alongside favorite ETA refreshes.
    s_request_queue = xQueueCreate(16, sizeof(bus_request_t));
    if (s_request_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create request queue");
        return;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(
        bus_worker_task,
        "bus_worker",
        8192,
        NULL,
        2,
        &s_worker_task,
        0  // Core 0
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create worker task");
        vQueueDelete(s_request_queue);
        s_request_queue = NULL;
    } else if (!bus_catalog_sync_init()) {
        ESP_LOGE(TAG, "Failed to start stop catalog sync worker");
    } else {
        bus_catalog_sync_request(false);
    }
}

void bus_service_set_listener(bus_listener_t cb, void *user_data)
{
    s_listener = cb;
    s_listener_user_data = user_data;
}

uint32_t bus_service_request_route(const char *route_name)
{
    if (s_request_queue == NULL) {
        bus_service_init();
    }

    bus_request_t req = {0};
    req.type = REQ_TYPE_ROUTE;
    req.id = s_next_request_id++;
    strlcpy(req.route, route_name, sizeof(req.route));

    bus_catalog_sync_foreground_begin();
    if (xQueueSend(s_request_queue, &req, 0) != pdTRUE) {
        bus_catalog_sync_foreground_end();
        ESP_LOGW(TAG, "Request queue full");
        return 0;
    }

    return req.id;
}

uint32_t bus_service_request_route_catalog(void)
{
    if (s_request_queue == NULL) {
        bus_service_init();
    }

    const time_t now = time(NULL);
    if (bus_route_catalog_count() > 0 && s_route_cache_fetched_at > 0 &&
        now >= s_route_cache_fetched_at &&
        s_route_cache_provider_mask == 0x03 &&
        (uint32_t)(now - s_route_cache_fetched_at) < ROUTE_CACHE_MAX_AGE_SECONDS) {
        ESP_LOGI(TAG, "Route catalog cache: fresh, skipping provider fetch");
        return 0;
    }

    bus_request_t req = {0};
    req.type = REQ_TYPE_ROUTE_CATALOG;
    req.id = s_next_request_id++;
    if (xQueueSend(s_request_queue, &req, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Route catalog request queue full");
        return 0;
    }
    return req.id;
}

bool bus_service_route_catalog_ready(void)
{
    return bus_route_catalog_count() > 0 && s_route_cache_provider_mask == 0x03;
}

uint8_t bus_service_get_cached_route_variants(const char *route,
                                               bus_route_variant_t *out,
                                               uint8_t max_count)
{
    if (route == NULL || out == NULL || max_count == 0) {
        return 0;
    }

    uint8_t copied = 0;
    for (uint16_t i = 0; s_route_variants != NULL && i < s_route_variant_count && copied < max_count; i++) {
        if (strcmp(s_route_variants[i].route, route) == 0) {
            out[copied++] = s_route_variants[i];
        }
    }
    return copied;
}

bool bus_service_get_cached_route_metadata(const char *route,
                                           uint8_t op,
                                           uint8_t service_type,
                                           bus_route_metadata_t *out)
{
    char normalized_route[5] = {0};
    if (route == NULL || out == NULL || service_type == 0 ||
        !bus_normalize_route_label(route, normalized_route)) {
        return false;
    }
    for (uint16_t i = 0; s_route_metadata != NULL &&
                          i < s_route_metadata_count; i++) {
        const bus_route_metadata_t *entry = &s_route_metadata[i];
        if (strcmp(entry->route, normalized_route) == 0 && entry->op == op &&
            entry->service_type == service_type) {
            *out = *entry;
            return true;
        }
    }
    return false;
}

uint32_t bus_service_request_stops(const char *route,
                                     uint8_t op,
                                     char bound,
                                     uint8_t service_type)
{
    if (s_request_queue == NULL) {
        bus_service_init();
    }

    bus_request_t req = {0};
    req.type = REQ_TYPE_STOPS;
    req.id = s_next_request_id++;
    strlcpy(req.route, route, sizeof(req.route));
    req.op = op;
    req.bound = bound;
    req.service_type = service_type;

    bus_catalog_sync_foreground_begin();
    if (xQueueSend(s_request_queue, &req, 0) != pdTRUE) {
        bus_catalog_sync_foreground_end();
        ESP_LOGW(TAG, "Request queue full");
        return 0;
    }

    return req.id;
}

void bus_stop_catalog_schedule(void)
{
    ESP_LOGI(TAG, "Legacy per-stop catalog scheduling disabled; prepared catalogs are required");
}

static void process_stop_catalog_request(const bus_request_t *req)
{
    (void)req;
    ESP_LOGW(TAG, "Ignoring deprecated per-stop catalog request; no detail HTTP is scheduled");
}

uint32_t bus_service_request_stop_catalog_sync(void)
{
    if (s_request_queue == NULL) {
        bus_service_init();
    }
    if (s_worker_task == NULL) {
        return 0;
    }
    if (!bus_catalog_sync_request(true)) return 0;
    return s_next_request_id++;
}

uint32_t bus_service_request_stop_detail(const char *stop_id,
                                         uint8_t op,
                                         char bound,
                                         uint8_t service_type)
{
    if (s_request_queue == NULL) {
        bus_service_init();
    }

    bus_request_t req = {0};
    req.type = REQ_TYPE_STOP_DETAIL;
    req.id = s_next_request_id++;
    strlcpy(req.stop_id, stop_id, sizeof(req.stop_id));
    req.op = op;
    req.bound = bound;
    req.service_type = service_type;

    if (xQueueSend(s_request_queue, &req, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Request queue full");
        return 0;
    }

    return req.id;
}

uint32_t bus_service_request_eta(const char *stop_id,
                                   const char *route,
                                   uint8_t op,
                                   char bound,
                                   uint8_t service_type)
{
    if (s_request_queue == NULL) {
        bus_service_init();
    }

    bus_request_t req = {0};
    req.type = REQ_TYPE_ETA;
    req.id = s_next_request_id++;
    strlcpy(req.stop_id, stop_id, sizeof(req.stop_id));
    strlcpy(req.route, route, sizeof(req.route));
    req.op = op;
    req.bound = bound;
    req.service_type = service_type;

    if (xQueueSend(s_request_queue, &req, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Request queue full");
        return 0;
    }

    return req.id;
}

void bus_service_cancel_all(void)
{
    s_cancel_all = true;
    bus_catalog_sync_cancel_all();
    const size_t kmb_catalog = crystal_http_cancel_owner(KMB_CATALOG_OWNER_ID);
    const size_t ctb_route = crystal_http_cancel_owner(CTB_ROUTE_VARIANTS_OWNER_ID);
    const size_t kmb_stops = crystal_http_cancel_owner(KMB_STOPS_OWNER_ID);
    const size_t ctb_stops = crystal_http_cancel_owner(CTB_STOPS_OWNER_ID);
    const size_t stop_catalog = crystal_http_cancel_owner(STOP_CATALOG_OWNER_ID);
    ESP_LOGI(TAG, "All bus cancellation requested kmb_catalog=%u ctb_route=%u kmb_stops=%u ctb_stops=%u stop_catalog=%u",
             (unsigned)kmb_catalog, (unsigned)ctb_route,
             (unsigned)kmb_stops, (unsigned)ctb_stops, (unsigned)stop_catalog);
    // Clear queue
    if (s_request_queue != NULL) {
        xQueueReset(s_request_queue);
    }
}

void bus_service_cancel_stops(void)
{
    const size_t kmb_cancelled = crystal_http_cancel_owner(KMB_STOPS_OWNER_ID);
    const size_t ctb_cancelled = crystal_http_cancel_owner(CTB_STOPS_OWNER_ID);
    ESP_LOGI(TAG, "Stop cancellation requested kmb=%u ctb=%u",
             (unsigned)kmb_cancelled, (unsigned)ctb_cancelled);
}

void bus_service_network_disconnected(void)
{
    s_network_lost = true;
    bus_catalog_sync_network_lost();
    (void)crystal_http_cancel_owner(KMB_CATALOG_OWNER_ID);
    const size_t ctb_route_cancelled =
        crystal_http_cancel_owner(CTB_ROUTE_VARIANTS_OWNER_ID);
    const size_t kmb_cancelled = crystal_http_cancel_owner(KMB_STOPS_OWNER_ID);
    const size_t ctb_cancelled = crystal_http_cancel_owner(CTB_STOPS_OWNER_ID);
    const size_t catalog_cancelled = crystal_http_cancel_owner(STOP_CATALOG_OWNER_ID);
    ESP_LOGW(TAG,
             "Network disconnected; bus HTTP cancellation ctb_route=%u kmb_stops=%u ctb_stops=%u stop_catalog=%u",
             (unsigned)ctb_route_cancelled, (unsigned)kmb_cancelled,
             (unsigned)ctb_cancelled, (unsigned)catalog_cancelled);
    esp_http_client_handle_t client = s_active_direct_client;
    if (client != NULL) {
        ESP_LOGW(TAG, "Network disconnected; cancelling active direct HTTP request");
        (void)esp_http_client_close(client);
    }
}

void bus_service_network_connected(void)
{
    if (!crystal_network_has_ip()) {
        ESP_LOGW(TAG, "Network connected signal received without an IP lease");
        return;
    }
    if (s_network_lost) {
        ESP_LOGI(TAG, "Network lease restored; bus HTTP requests enabled");
    }
    s_network_lost = false;
    // A recovered lease must re-check the hosted manifest immediately.  The
    // periodic worker deadline may still be many hours away after a previous
    // successful sync, so an ordinary bootstrap request would do nothing.
    bus_catalog_sync_request(true);
}

// Worker task
static void bus_worker_task(void *arg)
{
    bus_request_t req;

    ESP_LOGI(TAG, "Worker task started");

    while (1) {
        if (xQueueReceive(s_request_queue, &req, portMAX_DELAY) == pdTRUE) {
            if (s_cancel_all) {
                s_cancel_all = false;
                if (req.type == REQ_TYPE_ROUTE || req.type == REQ_TYPE_STOPS) {
                    bus_catalog_sync_foreground_end();
                }
                continue;
            }

            ESP_LOGI(TAG, "Processing request id=%lu type=%d", req.id, req.type);

            switch (req.type) {
                case REQ_TYPE_ROUTE:
                    process_route_request(&req);
                    bus_catalog_sync_foreground_end();
                    break;
                case REQ_TYPE_STOPS:
                    process_stops_request(&req);
                    bus_catalog_sync_foreground_end();
                    break;
                case REQ_TYPE_STOP_DETAIL:
                    // TODO: implement
                    break;
                case REQ_TYPE_ETA:
                    process_eta_request(&req);
                    break;
                case REQ_TYPE_ROUTE_CATALOG:
                    process_route_catalog_request(&req);
                    break;
                case REQ_TYPE_STOP_CATALOG:
                    process_stop_catalog_request(&req);
                    break;
            }
        }
    }
}

// HTTP helper
static esp_err_t http_get_json(const char *url, cJSON **out_json)
{
    if (s_network_lost || !crystal_network_has_ip()) {
        ESP_LOGW(TAG, "HTTP request deferred: network has no IP address");
        return ESP_ERR_INVALID_STATE;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        // CTB's route catalog can take roughly 14 seconds before sending
        // headers. Fifteen seconds leaves a small margin for that response.
        .timeout_ms = BUS_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
        .buffer_size = 4096,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }
    s_active_direct_client = client;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        s_active_direct_client = NULL;
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    if (content_length < 0) {
        ESP_LOGW(TAG, "HTTP headers unavailable (status=%d)", status);
        s_active_direct_client = NULL;
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    if (status != 200) {
        ESP_LOGW(TAG, "HTTP %d", status);
        s_active_direct_client = NULL;
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    // Allocate buffer for response (use PSRAM)
    char *buffer = heap_caps_malloc(content_length + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes", content_length);
        s_active_direct_client = NULL;
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    int total_read = 0;
    while (!s_network_lost && total_read < content_length) {
        int read_len = esp_http_client_read(client, buffer + total_read, content_length - total_read);
        if (read_len <= 0) {
            break;
        }
        total_read += read_len;
    }

    const bool cancelled = s_network_lost;
    if (cancelled || total_read != content_length) {
        ESP_LOGW(TAG, "HTTP body incomplete: read %d of %d bytes", total_read, content_length);
    }

    buffer[total_read] = '\0';

    // Don't call close() to preserve TLS session
    s_active_direct_client = NULL;
    esp_http_client_cleanup(client);
    if (cancelled) {
        free(buffer);
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Route catalog: API payload downloaded");

    // Parse JSON
    cJSON *json = cJSON_Parse(buffer);
    free(buffer);

    if (json == NULL) {
        ESP_LOGE(TAG, "JSON parse failed");
        return ESP_FAIL;
    }

    *out_json = json;
    return ESP_OK;
}

static bool wait_for_network(void)
{
    for (uint8_t attempt = 0; attempt < 30; attempt++) {
        if (s_cancel_all) {
            return false;
        }
        if (crystal_network_has_ip()) {
            ESP_LOGI(TAG, "Network ready; starting route catalog fetch");
            return true;
        }
        if (attempt == 0) {
            ESP_LOGI(TAG, "Route catalog waiting for network IP");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGE(TAG, "Route catalog failed: network IP was not acquired");
    return false;
}

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint8_t provider_mask;
    uint8_t reserved[3];
    int64_t fetched_at;
    uint16_t variant_count;
    uint16_t metadata_count;
} route_cache_header_t;

static bool route_metadata_pair_equal(const bus_route_terminal_pair_t *left,
                                      const bus_route_terminal_pair_t *right);

static bool route_variant_record_valid(const bus_route_variant_t *variant)
{
    if (variant == NULL || memchr(variant->route, '\0', sizeof(variant->route)) == NULL ||
        variant->route[0] == '\0' || variant->op > BUS_OP_NWFB ||
        (variant->bound != BUS_DIR_INBOUND &&
         variant->bound != BUS_DIR_OUTBOUND) || variant->service_type == 0) {
        return false;
    }
    return memchr(variant->orig_en, '\0', sizeof(variant->orig_en)) != NULL &&
           memchr(variant->dest_en, '\0', sizeof(variant->dest_en)) != NULL &&
           memchr(variant->orig_tc, '\0', sizeof(variant->orig_tc)) != NULL &&
           memchr(variant->dest_tc, '\0', sizeof(variant->dest_tc)) != NULL;
}

static bool route_variant_identity_equal(const bus_route_variant_t *left,
                                         const bus_route_variant_t *right)
{
    return left != NULL && right != NULL &&
           strcmp(left->route, right->route) == 0 && left->op == right->op &&
           left->bound == right->bound &&
           left->service_type == right->service_type;
}

static bool route_metadata_record_valid(const bus_route_metadata_t *metadata)
{
    if (metadata == NULL || memchr(metadata->route, '\0', sizeof(metadata->route)) == NULL ||
        metadata->route[0] == '\0' || metadata->op > BUS_OP_NWFB ||
        metadata->service_type == 0 || metadata->pair_count == 0 ||
        metadata->pair_count > BUS_ROUTE_METADATA_PAIR_CAPACITY) {
        return false;
    }
    char normalized_route[sizeof(metadata->route)] = {0};
    if (!bus_normalize_route_label(metadata->route, normalized_route) ||
        strcmp(normalized_route, metadata->route) != 0) {
        return false;
    }
    for (uint8_t i = 0; i < metadata->pair_count; i++) {
        const bus_route_terminal_pair_t *pair = &metadata->pairs[i];
        if (memchr(pair->orig_en, '\0', sizeof(pair->orig_en)) == NULL ||
            memchr(pair->dest_en, '\0', sizeof(pair->dest_en)) == NULL ||
            memchr(pair->orig_tc, '\0', sizeof(pair->orig_tc)) == NULL ||
            memchr(pair->dest_tc, '\0', sizeof(pair->dest_tc)) == NULL) {
            return false;
        }
        for (uint8_t previous = 0; previous < i; previous++) {
            if (route_metadata_pair_equal(&metadata->pairs[previous], pair)) {
                return false;
            }
        }
    }
    return true;
}

static bool route_metadata_identity_equal(const bus_route_metadata_t *left,
                                          const bus_route_metadata_t *right)
{
    return left != NULL && right != NULL && strcmp(left->route, right->route) == 0 &&
           left->op == right->op && left->service_type == right->service_type;
}

static bool route_metadata_pair_equal(const bus_route_terminal_pair_t *left,
                                      const bus_route_terminal_pair_t *right)
{
    return left != NULL && right != NULL && strcmp(left->orig_en, right->orig_en) == 0 &&
           strcmp(left->dest_en, right->dest_en) == 0 &&
           strcmp(left->orig_tc, right->orig_tc) == 0 &&
           strcmp(left->dest_tc, right->dest_tc) == 0;
}

static bool append_route_metadata(const bus_route_metadata_t *candidate)
{
    if (!route_metadata_record_valid(candidate) || s_route_metadata == NULL) {
        return false;
    }
    for (uint16_t i = 0; i < s_route_metadata_count; i++) {
        bus_route_metadata_t *existing = &s_route_metadata[i];
        if (!route_metadata_identity_equal(existing, candidate)) {
            continue;
        }
        for (uint8_t p = 0; p < candidate->pair_count; p++) {
            bool duplicate = false;
            for (uint8_t e = 0; e < existing->pair_count; e++) {
                if (route_metadata_pair_equal(&existing->pairs[e], &candidate->pairs[p])) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate && existing->pair_count < BUS_ROUTE_METADATA_PAIR_CAPACITY) {
                existing->pairs[existing->pair_count++] = candidate->pairs[p];
            }
        }
        return false;
    }
    if (s_route_metadata_count >= ROUTE_METADATA_CAPACITY) {
        ESP_LOGW(TAG, "Route metadata capacity reached; dropping route=%s op=%u service_type=%u",
                 candidate->route, candidate->op, candidate->service_type);
        return false;
    }
    s_route_metadata[s_route_metadata_count++] = *candidate;
    return true;
}

static bool load_route_catalog_cache(void)
{
    FILE *file = fopen(ROUTE_CACHE_PATH, "rb");
    if (file == NULL) {
        ESP_LOGI(TAG, "Route catalog cache: no cache found");
        return false;
    }

    route_cache_header_t header = {0};
    const bool header_ok = fread(&header, sizeof(header), 1, file) == 1 &&
                           header.magic == ROUTE_CACHE_MAGIC &&
                           header.version == ROUTE_CACHE_VERSION &&
                           header.count > 0 && header.count <= 2048 &&
                           header.provider_mask != 0;
    if (!header_ok) {
        fclose(file);
        ESP_LOGW(TAG, "Route catalog cache: invalid header");
        return false;
    }

    bus_route_catalog_reset();
    free(s_route_variants);
    s_route_variants = NULL;
    s_route_variant_count = 0;
    reset_route_metadata_store();
    bus_route_name_t entry;
    for (uint16_t i = 0; i < header.count; i++) {
        if (fread(&entry, sizeof(entry), 1, file) != 1 ||
            !bus_route_catalog_add(entry.name, sizeof(entry.name), entry.ops)) {
            bus_route_catalog_reset();
            fclose(file);
            ESP_LOGW(TAG, "Route catalog cache: truncated or invalid");
            return false;
        }
    }
    if (header.variant_count > ROUTE_VARIANT_CAPACITY) {
        bus_route_catalog_reset();
        fclose(file);
        ESP_LOGW(TAG, "Route catalog cache: invalid variant count");
        return false;
    }
    if (header.variant_count > 0) {
        s_route_variants = heap_caps_calloc(header.variant_count, sizeof(*s_route_variants),
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (header.variant_count > 0 && s_route_variants == NULL) {
        bus_route_catalog_reset();
        fclose(file);
        ESP_LOGW(TAG, "Route catalog cache: no memory for variants");
        return false;
    }
    for (uint16_t i = 0; i < header.variant_count; i++) {
        if (fread(&s_route_variants[i], sizeof(s_route_variants[i]), 1, file) != 1 ||
            !route_variant_record_valid(&s_route_variants[i])) {
            bus_route_catalog_reset();
            free(s_route_variants);
            s_route_variants = NULL;
            s_route_variant_count = 0;
            fclose(file);
            ESP_LOGW(TAG, "Route catalog cache: truncated variant data");
            return false;
        }
        for (uint16_t previous = 0; previous < i; previous++) {
            if (route_variant_identity_equal(&s_route_variants[previous],
                                             &s_route_variants[i])) {
                bus_route_catalog_reset();
                free(s_route_variants);
                s_route_variants = NULL;
                s_route_variant_count = 0;
                fclose(file);
                ESP_LOGW(TAG, "Route catalog cache: duplicate variant identity");
                return false;
            }
        }
        // The cache can contain a few thousand variants. Yield periodically
        // while validating identities so this worker does not starve IDLE.
        if ((i & 0x3Fu) == 0x3Fu) {
            vTaskDelay(1);
        }
    }
    s_route_variant_count = header.variant_count;
    if (header.metadata_count > ROUTE_METADATA_CAPACITY) {
        bus_route_catalog_reset();
        free(s_route_variants);
        s_route_variants = NULL;
        s_route_variant_count = 0;
        fclose(file);
        ESP_LOGW(TAG, "Route catalog cache: invalid metadata count");
        return false;
    }
    if (header.metadata_count > 0) {
        s_route_metadata = heap_caps_calloc(header.metadata_count,
                                            sizeof(*s_route_metadata),
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (header.metadata_count > 0 && s_route_metadata == NULL) {
        bus_route_catalog_reset();
        free(s_route_variants);
        s_route_variants = NULL;
        s_route_variant_count = 0;
        fclose(file);
        ESP_LOGW(TAG, "Route catalog cache: no memory for metadata");
        return false;
    }
    for (uint16_t i = 0; i < header.metadata_count; i++) {
        if (fread(&s_route_metadata[i], sizeof(*s_route_metadata), 1, file) != 1 ||
            !route_metadata_record_valid(&s_route_metadata[i])) {
            bus_route_catalog_reset();
            free(s_route_variants);
            s_route_variants = NULL;
            s_route_variant_count = 0;
            reset_route_metadata_store();
            fclose(file);
            ESP_LOGW(TAG, "Route catalog cache: truncated metadata data");
            return false;
        }
        for (uint16_t previous = 0; previous < i; previous++) {
            if (route_metadata_identity_equal(&s_route_metadata[previous],
                                              &s_route_metadata[i])) {
                bus_route_catalog_reset();
                free(s_route_variants);
                s_route_variants = NULL;
                s_route_variant_count = 0;
                reset_route_metadata_store();
                fclose(file);
                ESP_LOGW(TAG, "Route catalog cache: duplicate metadata identity");
                return false;
            }
        }
        if ((i & 0x3Fu) == 0x3Fu) {
            vTaskDelay(1);
        }
    }
    s_route_metadata_count = header.metadata_count;
    s_route_cache_fetched_at = (time_t)header.fetched_at;
    s_route_cache_provider_mask = header.provider_mask;
    fclose(file);
    const time_t now = time(NULL);
    if (s_route_cache_fetched_at > 0 && now > s_route_cache_fetched_at) {
        ESP_LOGI(TAG, "Route catalog cache: loaded %u routes, %u provider variants, %u summaries mask=0x%02x (%ld days old)",
                 bus_route_catalog_count(), (unsigned)s_route_variant_count,
                 (unsigned)s_route_metadata_count,
                 (unsigned)s_route_cache_provider_mask,
                 (long)((now - s_route_cache_fetched_at) / 86400));
    } else {
        ESP_LOGI(TAG, "Route catalog cache: loaded %u routes, %u provider variants, %u summaries mask=0x%02x (clock unavailable)",
                 bus_route_catalog_count(), (unsigned)s_route_variant_count,
                 (unsigned)s_route_metadata_count,
                 (unsigned)s_route_cache_provider_mask);
    }
    return true;
}

static bool save_route_catalog_cache(uint8_t provider_mask)
{
    if (s_route_variant_count > 0 && s_route_variants == NULL) {
        ESP_LOGE(TAG, "Route catalog cache: missing variant store");
        return false;
    }
    for (uint16_t i = 0; i < s_route_variant_count; i++) {
        if (!route_variant_record_valid(&s_route_variants[i])) {
            ESP_LOGE(TAG, "Route catalog cache: invalid variant at %u", i);
            return false;
        }
        for (uint16_t previous = 0; previous < i; previous++) {
            if (route_variant_identity_equal(&s_route_variants[previous],
                                             &s_route_variants[i])) {
                ESP_LOGE(TAG, "Route catalog cache: duplicate variant at %u", i);
                return false;
            }
        }
        if ((i & 0x3Fu) == 0x3Fu) {
            vTaskDelay(1);
        }
    }
    if (s_route_metadata_count > 0 && s_route_metadata == NULL) {
        ESP_LOGE(TAG, "Route catalog cache: missing metadata store");
        return false;
    }
    for (uint16_t i = 0; i < s_route_metadata_count; i++) {
        if (!route_metadata_record_valid(&s_route_metadata[i])) {
            ESP_LOGE(TAG, "Route catalog cache: invalid metadata at %u", i);
            return false;
        }
        for (uint16_t previous = 0; previous < i; previous++) {
            if (route_metadata_identity_equal(&s_route_metadata[previous],
                                              &s_route_metadata[i])) {
                ESP_LOGE(TAG, "Route catalog cache: duplicate metadata at %u", i);
                return false;
            }
        }
        if ((i & 0x3Fu) == 0x3Fu) {
            vTaskDelay(1);
        }
    }
    FILE *file = fopen(ROUTE_CACHE_TEMP_PATH, "wb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Route catalog cache: cannot open temporary file (errno=%d)", errno);
        return false;
    }

    route_cache_header_t header = {
        .magic = ROUTE_CACHE_MAGIC,
        .version = ROUTE_CACHE_VERSION,
        .count = bus_route_catalog_count(),
        .provider_mask = provider_mask,
        .fetched_at = (int64_t)time(NULL),
        .variant_count = s_route_variant_count,
        .metadata_count = s_route_metadata_count,
    };
    bool ok = header.count > 0 && fwrite(&header, sizeof(header), 1, file) == 1;
    if (!ok) {
        ESP_LOGE(TAG, "Route catalog cache: header write failed (errno=%d)", errno);
    }
    for (uint16_t i = 0; ok && i < header.count; i++) {
        bus_route_name_t entry;
        ok = bus_route_catalog_get(i, &entry) && fwrite(&entry, sizeof(entry), 1, file) == 1;
        if (!ok) {
            ESP_LOGE(TAG, "Route catalog cache: route write failed at %u (errno=%d)", i, errno);
        }
    }
    for (uint16_t i = 0; ok && i < header.variant_count; i++) {
        ok = fwrite(&s_route_variants[i], sizeof(*s_route_variants), 1, file) == 1;
        if (!ok) {
            ESP_LOGE(TAG, "Route catalog cache: variant write failed at %u (errno=%d)", i, errno);
        }
    }
    for (uint16_t i = 0; ok && i < header.metadata_count; i++) {
        ok = fwrite(&s_route_metadata[i], sizeof(*s_route_metadata), 1, file) == 1;
        if (!ok) {
            ESP_LOGE(TAG, "Route catalog cache: metadata write failed at %u (errno=%d)", i, errno);
        }
    }
    if (fclose(file) != 0) {
        ok = false;
        ESP_LOGE(TAG, "Route catalog cache: close failed (errno=%d)", errno);
    }
    if (ok && rename(ROUTE_CACHE_TEMP_PATH, ROUTE_CACHE_PATH) != 0) {
        const int rename_errno = errno;
        ESP_LOGW(TAG, "Route catalog cache: rename over existing file failed (errno=%d); replacing", rename_errno);
        if (remove(ROUTE_CACHE_PATH) != 0 || rename(ROUTE_CACHE_TEMP_PATH, ROUTE_CACHE_PATH) != 0) {
            ok = false;
            ESP_LOGE(TAG, "Route catalog cache: rename failed after replacement attempt (errno=%d)", errno);
        }
    }
    if (!ok) {
        remove(ROUTE_CACHE_TEMP_PATH);
        ESP_LOGE(TAG, "Route catalog cache: write failed");
    } else {
        s_route_cache_fetched_at = (time_t)header.fetched_at;
        s_route_cache_provider_mask = provider_mask;
        ESP_LOGI(TAG, "Route catalog: saved data in filesystem (%u routes, %u provider variants, %u summaries)",
                 header.count, header.variant_count, header.metadata_count);
    }
    return ok;
}

static bool route_variant_has_terminals(const bus_route_variant_t *variant)
{
    return variant != NULL &&
           (variant->orig_en[0] != '\0' || variant->dest_en[0] != '\0' ||
            variant->orig_tc[0] != '\0' || variant->dest_tc[0] != '\0');
}

static bool append_normalized_route_variant(const bus_route_variant_t *candidate)
{
    if (candidate == NULL || s_route_variants == NULL ||
        candidate->route[0] == '\0' ||
        strlen(candidate->route) >= sizeof(candidate->route) ||
        (candidate->bound != BUS_DIR_INBOUND &&
         candidate->bound != BUS_DIR_OUTBOUND) || candidate->service_type == 0) {
        return false;
    }
    for (uint16_t i = 0; i < s_route_variant_count; i++) {
        bus_route_variant_t *existing = &s_route_variants[i];
        if (strcmp(existing->route, candidate->route) == 0 &&
            existing->op == candidate->op &&
            existing->bound == candidate->bound &&
            existing->service_type == candidate->service_type) {
            // A later explicit provider record may complete an earlier
            // direction-only placeholder, but duplicates remain stable.
            if (!route_variant_has_terminals(existing) &&
                route_variant_has_terminals(candidate)) {
                *existing = *candidate;
            }
            return false;
        }
    }
    if (s_route_variant_count >= ROUTE_VARIANT_CAPACITY) {
        return false;
    }
    s_route_variants[s_route_variant_count++] = *candidate;
    return true;
}

static uint16_t append_route_variants_from_item(cJSON *item, bus_operator_t op)
{
    if (item == NULL || s_route_variants == NULL) {
        return 0;
    }

    cJSON *route = cJSON_GetObjectItem(item, "route");
    char normalized_route[sizeof(s_route_variants[0].route)] = {0};
    if (!cJSON_IsString(route) ||
        !bus_normalize_route_label(route->valuestring, normalized_route)) {
        return 0;
    }

    cJSON *bound = cJSON_GetObjectItem(item, "bound");
    cJSON *service_type = cJSON_GetObjectItem(item, "service_type");
    cJSON *orig_en = cJSON_GetObjectItem(item, "orig_en");
    cJSON *dest_en = cJSON_GetObjectItem(item, "dest_en");
    cJSON *orig_tc = cJSON_GetObjectItem(item, "orig_tc");
    cJSON *dest_tc = cJSON_GetObjectItem(item, "dest_tc");
    char normalized_bound = 0;
    if (bound != NULL && !cJSON_IsString(bound)) {
        return 0;
    }
    if (!bus_normalize_direction(cJSON_IsString(bound) ? bound->valuestring : NULL,
                                 &normalized_bound)) {
        return 0;
    }
    const bool has_bound = normalized_bound != 0;
    uint8_t variant_service_type = 1;
    char service_type_text[4] = {0};
    bool service_type_present = service_type != NULL;
    if (service_type_present && cJSON_IsNumber(service_type)) {
        if (service_type->valuedouble < 1.0 ||
            service_type->valuedouble > UINT8_MAX ||
            service_type->valuedouble != (double)service_type->valueint) {
            return 0;
        }
        snprintf(service_type_text, sizeof(service_type_text), "%u",
                 (unsigned)service_type->valueint);
    } else if (service_type_present && cJSON_IsString(service_type)) {
        if (service_type->valuestring == NULL ||
            strlen(service_type->valuestring) >= sizeof(service_type_text)) {
            return 0;
        }
        strlcpy(service_type_text, service_type->valuestring,
                sizeof(service_type_text));
    } else if (service_type_present) {
        return 0;
    }
    if (!bus_normalize_service_type(service_type_text, service_type_present,
                                    true, &variant_service_type)) {
        return 0;
    }
    if (op == BUS_OP_KMB && !has_bound) {
        return 0;
    }

    bus_route_variant_t candidate = {0};
    strlcpy(candidate.route, normalized_route, sizeof(candidate.route));
    candidate.op = op;
    candidate.service_type = variant_service_type;
    bus_route_metadata_t metadata = {0};
    strlcpy(metadata.route, normalized_route, sizeof(metadata.route));
    metadata.op = op;
    metadata.service_type = variant_service_type;
    if (bus_normalize_text(cJSON_IsString(orig_en) ? orig_en->valuestring : NULL,
                           metadata.pairs[0].orig_en, sizeof(metadata.pairs[0].orig_en), true) &&
        bus_normalize_text(cJSON_IsString(dest_en) ? dest_en->valuestring : NULL,
                           metadata.pairs[0].dest_en, sizeof(metadata.pairs[0].dest_en), true) &&
        bus_normalize_text(cJSON_IsString(orig_tc) ? orig_tc->valuestring : NULL,
                           metadata.pairs[0].orig_tc, sizeof(metadata.pairs[0].orig_tc), true) &&
        bus_normalize_text(cJSON_IsString(dest_tc) ? dest_tc->valuestring : NULL,
                           metadata.pairs[0].dest_tc, sizeof(metadata.pairs[0].dest_tc), true)) {
        if (metadata.pairs[0].orig_en[0] != '\0' || metadata.pairs[0].dest_en[0] != '\0' ||
            metadata.pairs[0].orig_tc[0] != '\0' || metadata.pairs[0].dest_tc[0] != '\0') {
            metadata.pair_count = 1;
            (void)append_route_metadata(&metadata);
        }
    }
    const char directions[2] = {BUS_DIR_INBOUND, BUS_DIR_OUTBOUND};
    const uint8_t direction_count = has_bound ? 1u : 2u;
    uint16_t appended = 0;
    for (uint8_t direction_index = 0; direction_index < direction_count;
         direction_index++) {
        memset(candidate.orig_en, 0, sizeof(candidate.orig_en));
        memset(candidate.dest_en, 0, sizeof(candidate.dest_en));
        memset(candidate.orig_tc, 0, sizeof(candidate.orig_tc));
        memset(candidate.dest_tc, 0, sizeof(candidate.dest_tc));
        candidate.bound = has_bound ? normalized_bound
                                    : directions[direction_index];
        if (has_bound || op == BUS_OP_KMB) {
            if (!bus_normalize_text(cJSON_IsString(orig_en) ? orig_en->valuestring : NULL,
                                    candidate.orig_en, sizeof(candidate.orig_en), true) ||
                !bus_normalize_text(cJSON_IsString(dest_en) ? dest_en->valuestring : NULL,
                                    candidate.dest_en, sizeof(candidate.dest_en), true) ||
                !bus_normalize_text(cJSON_IsString(orig_tc) ? orig_tc->valuestring : NULL,
                                    candidate.orig_tc, sizeof(candidate.orig_tc), true) ||
                !bus_normalize_text(cJSON_IsString(dest_tc) ? dest_tc->valuestring : NULL,
                                    candidate.dest_tc, sizeof(candidate.dest_tc), true)) {
                continue;
            }
        }
        if (append_normalized_route_variant(&candidate)) {
            appended++;
        }
    }
    return appended;
}

static uint16_t resolve_route_provider_json(const char *label, cJSON *root,
                                            uint8_t op, uint32_t request_id)
{
    if (root == NULL) return 0;
    cJSON *data = cJSON_GetObjectItem(root, "data");
    uint16_t fetched = 0;
    bus_event_t progress = {0};
    progress.type = BUS_EVT_ROUTE_CATALOG_PROGRESS;
    progress.request_id = request_id;
    snprintf(progress.data.route_catalog_progress.message,
             sizeof(progress.data.route_catalog_progress.message),
             "Resolving %s route data...\nPlease wait", label);
    post_event(&progress);
    ESP_LOGI(TAG, "Route catalog: resolving %s data", label);
    if (cJSON_IsArray(data)) {
        const int count = cJSON_GetArraySize(data);
        for (int i = 0; i < count; i++) {
            cJSON *item = cJSON_GetArrayItem(data, i);
            cJSON *route = item != NULL ? cJSON_GetObjectItem(item, "route") : NULL;
            char normalized_route[5] = {0};
            if (route != NULL && cJSON_IsString(route) &&
                bus_normalize_route_label(route->valuestring, normalized_route) &&
                bus_route_catalog_add(normalized_route, strlen(normalized_route), op)) {
                fetched++;
            }
            (void)append_route_variants_from_item(
                item, op == (1u << BUS_OP_KMB) ? BUS_OP_KMB : BUS_OP_CTB);
            // Catalog insertion scans the existing route index. Yield during
            // large provider payloads so IDLE can service the task watchdog.
            if ((i & 0x1Fu) == 0x1Fu) {
                vTaskDelay(1);
            }
        }
    }
    cJSON_Delete(root);
    if (fetched == 0) {
        ESP_LOGE(TAG, "Route catalog: %s returned no routes", label);
    } else {
        ESP_LOGI(TAG, "Route catalog: resolved %u %s route records (%u provider variants)",
                 fetched, label, s_route_variant_count);
    }
    return fetched;
}

static uint16_t resolve_ctb_route_provider_body(const char *label,
                                                const uint8_t *body,
                                                size_t body_len,
                                                uint32_t request_id)
{
    bus_route_variant_t *variants = NULL;
    bus_route_metadata_t *metadata = NULL;
    uint16_t variant_count = 0;
    uint16_t metadata_count = 0;
    const esp_err_t parse_status = bus_ctb_parse_route_catalog(
        body, body_len, NULL, &variants, &variant_count,
        &metadata, &metadata_count);
    if (parse_status != ESP_OK) {
        ESP_LOGW(TAG, "Route catalog: %s variant parse failed status=%s",
                 label, esp_err_to_name(parse_status));
        return 0;
    }

    bus_event_t progress = {0};
    progress.type = BUS_EVT_ROUTE_CATALOG_PROGRESS;
    progress.request_id = request_id;
    snprintf(progress.data.route_catalog_progress.message,
             sizeof(progress.data.route_catalog_progress.message),
             "Resolving %s route data...\nPlease wait", label);
    post_event(&progress);

    uint16_t fetched = 0;
    for (uint16_t i = 0; i < variant_count; i++) {
        if (bus_route_catalog_add(variants[i].route,
                                  strlen(variants[i].route),
                                  1u << BUS_OP_CTB)) {
            fetched++;
        }
        (void)append_normalized_route_variant(&variants[i]);
        if ((i & 0x1Fu) == 0x1Fu) {
            vTaskDelay(1);
        }
    }
    for (uint16_t i = 0; i < metadata_count; i++) {
        (void)append_route_metadata(&metadata[i]);
        if ((i & 0x1Fu) == 0x1Fu) {
            vTaskDelay(1);
        }
    }
    free(variants);
    free(metadata);
    ESP_LOGI(TAG,
             "Route catalog: resolved %u %s route records (%u provider variants)",
             fetched, label, s_route_variant_count);
    uint16_t terminal_pair_count = 0;
    for (uint16_t i = 0; i < metadata_count; i++) {
        terminal_pair_count = (uint16_t)(terminal_pair_count + metadata[i].pair_count);
    }
    ESP_LOGI(TAG, "Route metadata: resolved %s summaries=%u terminal_pairs=%u",
             label, (unsigned)metadata_count, (unsigned)terminal_pair_count);
    return fetched;
}

static uint16_t fetch_ctb_route_provider(const char *label, const char *url,
                                         uint32_t request_id)
{
    if (label == NULL || url == NULL) {
        return 0;
    }

    bus_event_t progress = {0};
    progress.type = BUS_EVT_ROUTE_CATALOG_PROGRESS;
    progress.request_id = request_id;
    snprintf(progress.data.route_catalog_progress.message,
             sizeof(progress.data.route_catalog_progress.message),
             "Downloading %s route data...\nPlease wait", label);
    post_event(&progress);
    ESP_LOGI(TAG,
             "Route catalog: downloading data from %s API %s through crystal_http",
             label, url);

    // This is one framework request. Retry attempts, response buffering, and
    // response release remain owned by crystal_http and the handoff callback.
    ctb_route_variants_handoff_t *context = submit_ctb_route_variants(
        request_id, "", BUS_OP_CTB, (bus_direction_t)0, 1, url);
    if (context == NULL) {
        ESP_LOGE(TAG, "Route catalog: %s handoff failed", label);
        return 0;
    }

    uint16_t fetched = 0;
    if (context->body != NULL && context->body_len > 0 &&
        context->status_code == 200 && context->transport_error == ESP_OK) {
        progress = (bus_event_t){0};
        progress.type = BUS_EVT_ROUTE_CATALOG_PROGRESS;
        progress.request_id = request_id;
        snprintf(progress.data.route_catalog_progress.message,
                 sizeof(progress.data.route_catalog_progress.message),
                 "%s data downloaded\nResolving route data...", label);
        post_event(&progress);
        fetched = resolve_ctb_route_provider_body(label, context->body,
                                                  context->body_len,
                                                  request_id);
    } else {
        ESP_LOGW(TAG,
                 "Route catalog: %s transport failed status=%d body=%u error=%s",
                 label, context->status_code, (unsigned)context->body_len,
                 esp_err_to_name(context->transport_error));
    }

    ctb_route_variants_handoff_cleanup(context);
    return fetched;
}

static void process_route_catalog_request(const bus_request_t *req)
{
    (void)req;
    if (!wait_for_network()) {
        bus_event_t event = {0};
        event.type = BUS_EVT_ROUTE_CATALOG;
        event.request_id = req->id;
        event.status = ESP_ERR_INVALID_STATE;
        event.data.route_catalog.route_count = bus_route_catalog_count();
        post_event(&event);
        return;
    }
    s_network_lost = false;
    bus_event_t progress = {0};
    progress.type = BUS_EVT_ROUTE_CATALOG_PROGRESS;
    progress.request_id = req->id;
    strlcpy(progress.data.route_catalog_progress.message,
            "Preparing route data fetch...\nPlease wait",
            sizeof(progress.data.route_catalog_progress.message));
    post_event(&progress);
    const bool had_cache = bus_route_catalog_count() > 0;
    const bool had_complete_cache = had_cache && s_route_cache_provider_mask == 0x03;
    bus_route_catalog_reset();
    free(s_route_variants);
    s_route_variants = heap_caps_calloc(ROUTE_VARIANT_CAPACITY,
                                        sizeof(*s_route_variants),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_route_variant_count = 0;
    reset_route_metadata_store();
    s_route_metadata = heap_caps_calloc(ROUTE_METADATA_CAPACITY,
                                        sizeof(*s_route_metadata),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_route_variants == NULL) {
        ESP_LOGE(TAG, "Route catalog: no memory for provider route variants");
    }

    // The framework-owned KMB response is parsed before its handoff is released.
    const uint16_t kmb_count = submit_kmb_catalog(req->id);
    const uint16_t ctb_count = fetch_ctb_route_provider(
        "CTB", CTB_ROUTE_VARIANT_REQUEST_URL, req->id);
    const bool cancelled_by_network = s_network_lost;
    const uint8_t succeeded = (kmb_count > 0 ? 1 : 0) + (ctb_count > 0 ? 1 : 0);
    const uint8_t failed = 2 - succeeded;

    bus_event_t event = {0};
    event.type = BUS_EVT_ROUTE_CATALOG;
    event.request_id = req->id;
    event.data.route_catalog.providers_succeeded = succeeded;
    event.data.route_catalog.providers_failed = failed;

    bool cache_saved = false;
    if (succeeded == 2 && bus_route_catalog_count() > 0) {
        progress = (bus_event_t){0};
        progress.type = BUS_EVT_ROUTE_CATALOG_PROGRESS;
        progress.request_id = req->id;
        strlcpy(progress.data.route_catalog_progress.message,
                "Saving route data...\nPlease wait",
                sizeof(progress.data.route_catalog_progress.message));
        post_event(&progress);
        cache_saved = save_route_catalog_cache(0x03);
        if (cache_saved) {
            event.status = ESP_OK;
            event.data.route_catalog.route_count = bus_route_catalog_count();
            ESP_LOGI(TAG, "Route catalog: complete with %u routes (%u/%u providers)",
                     bus_route_catalog_count(), succeeded, succeeded + failed);
        }
    } else if (succeeded > 0 && bus_route_catalog_count() > 0 && !had_complete_cache) {
        progress = (bus_event_t){0};
        progress.type = BUS_EVT_ROUTE_CATALOG_PROGRESS;
        progress.request_id = req->id;
        strlcpy(progress.data.route_catalog_progress.message,
                "Saving route data...\nPlease wait",
                sizeof(progress.data.route_catalog_progress.message));
        post_event(&progress);
        cache_saved = save_route_catalog_cache((kmb_count > 0 ? 0x01 : 0) |
                                               (ctb_count > 0 ? 0x02 : 0));
        if (cache_saved) {
            event.status = ESP_ERR_NOT_FINISHED;
            event.data.route_catalog.route_count = bus_route_catalog_count();
            ESP_LOGW(TAG, "Route catalog: partial cache saved with %u/%u providers",
                     succeeded, succeeded + failed);
        }
    }
    if (!cache_saved) {
        if (had_complete_cache || had_cache) {
            load_route_catalog_cache();
        } else {
            bus_route_catalog_reset();
        }
        event.status = cancelled_by_network ? ESP_ERR_INVALID_STATE : ESP_FAIL;
        event.data.route_catalog.route_count = bus_route_catalog_count();
        if (cancelled_by_network) {
            ESP_LOGW(TAG, "Route catalog: update cancelled by network loss; retry will follow reconnect");
        } else {
            ESP_LOGE(TAG, "Route catalog: update failed; cached catalog %savailable",
                     had_cache ? "" : "un");
        }
    }
    post_event(&event);
}

// Normalize stop ID to uppercase
static char *normalize_stop_id(char *stop_id)
{
    for (char *p = stop_id; *p; p++) {
        *p = toupper((unsigned char)*p);
    }
    return stop_id;
}

// Process route request (get variants)
static void process_route_request(const bus_request_t *req)
{
    bus_event_t event = {0};
    event.type = BUS_EVT_ROUTE_VARIANTS;
    event.request_id = req->id;
    strlcpy(event.identity.route, req->route, sizeof(event.identity.route));
    // Route labels can belong to both providers. Resolve the complete
    // provider-qualified option set from the normalized catalog instead of
    // selecting an API from the printed route number.
    bus_route_variant_t *variants = heap_caps_calloc(
        UINT8_MAX, sizeof(*variants), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (variants != NULL) {
        const uint8_t count = bus_service_get_cached_route_variants(
            req->route, variants, UINT8_MAX);
        if (count > 0) {
            event.data.route_variants.variants = variants;
            event.data.route_variants.count = count;
            event.status = ESP_OK;
            post_event(&event);
            return;
        }
    }
    free(variants);
    event.status = ESP_FAIL;
    strlcpy(event.data.error.message, "Route not found", sizeof(event.data.error.message));
    post_event(&event);
}

// Process stops request
static void enrich_route_stops_from_catalog(bus_stop_t *stops, uint16_t count)
{
    if (stops == NULL) return;
    for (uint16_t i = 0; i < count; ++i) {
        bus_stop_metadata_t metadata = {0};
        const esp_err_t status = bus_stop_catalog_lookup(
            stops[i].op, stops[i].stop_id, &metadata);
        if (status == ESP_OK) {
            strlcpy(stops[i].name_en, metadata.name_en,
                    sizeof(stops[i].name_en));
            strlcpy(stops[i].name_tc, metadata.name_tc,
                    sizeof(stops[i].name_tc));
            if (metadata.has_coordinates) {
                stops[i].lat = metadata.lat;
                stops[i].lon = metadata.lon;
            }
            stops[i].resolved = true;
        } else {
            snprintf(stops[i].name_en, sizeof(stops[i].name_en),
                     "Stop %s", stops[i].stop_id);
            strlcpy(stops[i].name_tc, stops[i].name_en,
                    sizeof(stops[i].name_tc));
            stops[i].resolved = false;
        }
    }
}

static void process_stops_request(const bus_request_t *req)
{
    char url[256];
    cJSON *root = NULL;
    kmb_stops_handoff_t *handoff = NULL;
    ctb_stops_handoff_t *ctb_handoff = NULL;
    esp_err_t request_err = ESP_FAIL;
    bus_event_t event = {0};
    event.type = BUS_EVT_STOPS_LIST;
    event.request_id = req->id;
    strlcpy(event.identity.route, req->route, sizeof(event.identity.route));
    event.identity.op = (bus_operator_t)req->op;
    event.identity.bound = (bus_direction_t)req->bound;
    event.identity.service_type = req->service_type;

    // A request queued during a lost network lease must terminate locally.
    // Do not submit it to crystal_http, where it could consume retry budget
    // before the connection is restored.
    if (s_network_lost || !crystal_network_has_ip()) {
        event.type = BUS_EVT_ERROR;
        event.status = ESP_ERR_INVALID_STATE;
        strlcpy(event.data.error.message, "Waiting for network",
                sizeof(event.data.error.message));
        ESP_LOGW(TAG, "Stops request deferred while network is unavailable route=%s bus_id=%lu",
                 req->route, (unsigned long)req->id);
        post_event(&event);
        return;
    }

    if (req->op == BUS_OP_KMB) {
        // KMB route data uses I/O bound codes, but the live route-stop
        // endpoint requires the path words "inbound" and "outbound".
        const char *direction = req->bound == BUS_DIR_OUTBOUND ? "outbound" : "inbound";
        snprintf(url, sizeof(url), "%s%s/%s/%d",
                 KMB_STOPS_REQUEST_URL, req->route, direction, req->service_type);

        handoff = submit_kmb_stops(req, direction, url);
        if (handoff != NULL && handoff->transport_error == ESP_OK &&
                handoff->status_code == 200 && handoff->body != NULL &&
                handoff->body_len > 0) {
            ESP_LOGI(TAG, "KMB stops data downloaded route=%s direction=%s bytes=%u",
                     req->route, direction, (unsigned)handoff->body_len);
            ESP_LOGI(TAG, "KMB stops parsing route=%s body=%u",
                     req->route, (unsigned)handoff->body_len);
            root = cJSON_ParseWithLength((const char *)handoff->body,
                                         handoff->body_len);
            if (root == NULL) {
                ESP_LOGW(TAG, "KMB stops response JSON parse failed route=%s",
                         req->route);
            }
            request_err = root != NULL ? ESP_OK : ESP_FAIL;
            kmb_stops_handoff_cleanup(handoff);
            handoff = NULL;
        } else {
            const esp_err_t failure_status = handoff != NULL
                ? handoff->transport_error : ESP_FAIL;
            if (handoff != NULL) {
                ESP_LOGW(TAG, "KMB stops request failed route=%s status=%d error=%s",
                         req->route, handoff->status_code,
                         esp_err_to_name(handoff->transport_error));
                kmb_stops_handoff_cleanup(handoff);
                handoff = NULL;
            }
            event.type = BUS_EVT_ERROR;
            event.status = failure_status;
            strlcpy(event.data.error.message,
                    failure_status == ESP_ERR_INVALID_STATE || s_network_lost
                        ? "Waiting for network" : "Failed to fetch stops",
                    sizeof(event.data.error.message));
            post_event(&event);
            return;
        }
    } else {
        const char *direction = req->bound == BUS_DIR_OUTBOUND
            ? "outbound" : "inbound";
        snprintf(url, sizeof(url), "%s/route-stop/CTB/%s/%s",
                 CTB_STOPS_BASE_URL, req->route, direction);
        ESP_LOGI(TAG, "CTB stops: submitting route=%s op=%u bound=%c service_type=%u url=%s",
                 req->route, (unsigned)req->op, req->bound,
                 (unsigned)req->service_type, url);
        ctb_handoff = submit_ctb_stops(req, url);
        if (ctb_handoff == NULL) {
            event.type = BUS_EVT_ERROR;
            event.status = ESP_FAIL;
            strlcpy(event.data.error.message,
                    s_network_lost || !crystal_network_has_ip()
                        ? "Waiting for network" : "Failed to fetch stops",
                    sizeof(event.data.error.message));
            post_event(&event);
            return;
        }

        ESP_LOGI(TAG, "CTB stops transport complete route=%s op=%u bound=%c service_type=%u status=%d body=%u attempts=%u error=%s",
                 req->route, (unsigned)req->op, req->bound,
                 (unsigned)req->service_type, ctb_handoff->status_code,
                 (unsigned)ctb_handoff->body_len,
                 (unsigned)ctb_handoff->attempts,
                 esp_err_to_name(ctb_handoff->transport_error));
        if (ctb_handoff->transport_error != ESP_OK ||
            ctb_handoff->status_code != 200 ||
            ctb_handoff->body == NULL || ctb_handoff->body_len == 0) {
            const esp_err_t failure_status = ctb_handoff->transport_error != ESP_OK
                ? ctb_handoff->transport_error : ESP_FAIL;
            ctb_stops_handoff_cleanup(ctb_handoff);
            ctb_handoff = NULL;
            event.type = BUS_EVT_ERROR;
            event.status = failure_status;
            strlcpy(event.data.error.message,
                    failure_status == ESP_ERR_INVALID_STATE || s_network_lost
                        ? "Waiting for network" : "Failed to fetch stops",
                    sizeof(event.data.error.message));
            post_event(&event);
            return;
        }

        bus_stop_t *stops = NULL;
        uint16_t stop_count = 0;
        const esp_err_t parse_status = bus_ctb_parse_route_stops(
            ctb_handoff->body, ctb_handoff->body_len, req->route,
            (bus_direction_t)req->bound, req->service_type,
            &stops, &stop_count);
        ESP_LOGI(TAG, "CTB stops parsed route=%s op=%u bound=%c service_type=%u status=%s count=%u",
                 req->route, (unsigned)req->op, req->bound,
                 (unsigned)req->service_type, esp_err_to_name(parse_status),
                 (unsigned)stop_count);
        ctb_stops_handoff_cleanup(ctb_handoff);
        ctb_handoff = NULL;

        if (parse_status != ESP_OK) {
            event.type = BUS_EVT_ERROR;
            event.status = parse_status;
            strlcpy(event.data.error.message, "Unable to read bus stops",
                    sizeof(event.data.error.message));
            post_event(&event);
            return;
        }
        if (stop_count == 0 || stops == NULL) {
            event.type = BUS_EVT_ERROR;
            event.status = ESP_ERR_NOT_FOUND;
            strlcpy(event.data.error.message, "No bus stops available",
                    sizeof(event.data.error.message));
            post_event(&event);
            return;
        }

        enrich_route_stops_from_catalog(stops, stop_count);
        event.data.stops_list.stops = stops;
        event.data.stops_list.count = stop_count;
        event.status = ESP_OK;
        post_event(&event);
        return;
    }

    if (request_err == ESP_OK && root != NULL) {
        cJSON *data = cJSON_GetObjectItem(root, "data");
        ESP_LOGI(TAG, "KMB stops parser data=%s",
                 cJSON_IsArray(data) ? "array" : "missing-or-invalid");
        if (cJSON_IsArray(data)) {
            int count = cJSON_GetArraySize(data);
            ESP_LOGI(TAG, "KMB stops parser entries=%d", count);
            if (count > 0) {
                bus_stop_t *stops = heap_caps_calloc(count, sizeof(bus_stop_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (stops != NULL) {
                    int valid_count = 0;
                    for (int i = 0; i < count; i++) {
                        cJSON *item = cJSON_GetArrayItem(data, i);
                        if (item != NULL) {
                            cJSON *stop_id = cJSON_GetObjectItem(item, "stop");
                            cJSON *seq = cJSON_GetObjectItem(item, "seq");

                            if (cJSON_IsString(stop_id) &&
                                    (cJSON_IsNumber(seq) || cJSON_IsString(seq))) {
                                const int sequence = cJSON_IsNumber(seq)
                                    ? seq->valueint : atoi(seq->valuestring);
                                if (sequence <= 0) {
                                    continue;
                                }
                                strlcpy(stops[valid_count].stop_id, stop_id->valuestring, sizeof(stops[valid_count].stop_id));
                                normalize_stop_id(stops[valid_count].stop_id);
                                strlcpy(stops[valid_count].route, req->route,
                                        sizeof(stops[valid_count].route));
                                stops[valid_count].seq = (uint16_t)sequence;
                                stops[valid_count].op = BUS_OP_KMB;
                                stops[valid_count].bound = (bus_direction_t)req->bound;
                                stops[valid_count].service_type = req->service_type;
                                stops[valid_count].resolved = false;
                                snprintf(stops[valid_count].name_en, sizeof(stops[valid_count].name_en), "Stop %d", sequence);
                                valid_count++;
                            }
                        }
                    }

                    if (valid_count > 0) {
                        enrich_route_stops_from_catalog(stops, (uint16_t)valid_count);
                        event.data.stops_list.stops = stops;
                        event.data.stops_list.count = valid_count;
                        event.status = ESP_OK;
                        ESP_LOGI(TAG, "KMB stops resolved route=%s count=%d",
                                 req->route, valid_count);
                        post_event(&event);

                        // Don't free stops here - listener owns them
                        cJSON_Delete(root);
                        return;
                    }
                    ESP_LOGW(TAG, "KMB stops parser found no valid entries route=%s",
                             req->route);
                    free(stops);
                }
                else {
                    ESP_LOGE(TAG, "KMB stops parser allocation failed entries=%d",
                             count);
                }
            }
            else {
                ESP_LOGW(TAG, "KMB stops parser returned empty data route=%s",
                         req->route);
            }
        }
        cJSON_Delete(root);
    }

    // Error case
    event.type = BUS_EVT_ERROR;
    event.status = ESP_FAIL;
    strlcpy(event.data.error.message, "Failed to fetch stops", sizeof(event.data.error.message));
    post_event(&event);
}

// Process ETA request
static void process_eta_request(const bus_request_t *req)
{
    char url[256];
    cJSON *root = NULL;
    bus_event_t event = {0};
    event.type = BUS_EVT_ETA;
    event.request_id = req->id;
    strlcpy(event.identity.route, req->route, sizeof(event.identity.route));
    event.identity.op = (bus_operator_t)req->op;
    event.identity.bound = (bus_direction_t)req->bound;
    event.identity.service_type = req->service_type;
    strlcpy(event.identity.stop_id, req->stop_id, sizeof(event.identity.stop_id));
    strlcpy(event.data.eta.stop_id, req->stop_id, sizeof(event.data.eta.stop_id));

    if (req->op == BUS_OP_KMB) {
        snprintf(url, sizeof(url), "%s/eta/%s/%s/%d",
                 KMB_BASE_URL, req->stop_id, req->route, req->service_type);
    } else {
        snprintf(url, sizeof(url), "%s/eta/CTB/%s/%s",
                 CTB_BASE_URL, req->stop_id, req->route);
    }

    if (http_get_json(url, &root) == ESP_OK && root != NULL) {
        cJSON *data = cJSON_GetObjectItem(root, "data");
        if (cJSON_IsArray(data)) {
            int count = cJSON_GetArraySize(data);
            int eta_count = 0;
            time_t now = time(NULL);

            for (int i = 0; i < count && eta_count < 3; i++) {
                cJSON *item = cJSON_GetArrayItem(data, i);
                if (item != NULL) {
                    cJSON *eta = cJSON_GetObjectItem(item, "eta");
                    cJSON *rmk_en = cJSON_GetObjectItem(item, "rmk_en");

                    if (eta && cJSON_IsString(eta) && eta->valuestring[0] != '\0') {
                        // Parse ISO8601 timestamp (simplified)
                        struct tm tm = {0};
                        if (sscanf(eta->valuestring, "%d-%d-%dT%d:%d:%d",
                                   &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                                   &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
                            tm.tm_year -= 1900;
                            tm.tm_mon -= 1;
                            time_t eta_time = mktime(&tm);

                            event.data.eta.result.entries[eta_count].eta_epoch = eta_time;
                            event.data.eta.result.entries[eta_count].minutes_left = (eta_time - now) / 60;

                            if (rmk_en && cJSON_IsString(rmk_en)) {
                                strlcpy(event.data.eta.result.entries[eta_count].remark_en,
                                       rmk_en->valuestring,
                                       sizeof(event.data.eta.result.entries[eta_count].remark_en));
                            }

                            eta_count++;
                        }
                    }
                }
            }

            event.data.eta.result.count = eta_count;
            event.data.eta.result.fetched_at = now;
            event.status = ESP_OK;
        }
        cJSON_Delete(root);
    } else {
        event.status = ESP_FAIL;
        strlcpy(event.data.error.message, "Failed to fetch ETA", sizeof(event.data.error.message));
    }

    post_event(&event);
}

static void free_event_payload(bus_event_t *event)
{
    if (event == NULL) {
        return;
    }
    if (event->type == BUS_EVT_ROUTE_VARIANTS) {
        free(event->data.route_variants.variants);
        event->data.route_variants.variants = NULL;
    } else if (event->type == BUS_EVT_STOPS_LIST) {
        free(event->data.stops_list.stops);
        event->data.stops_list.stops = NULL;
    }
}

// Bus requests run on a worker task, while LVGL objects may only be accessed
// by the UI task. Copy the event and deliver it from LVGL's async queue.
static void deliver_event_async(void *user_data)
{
    bus_event_t *event = (bus_event_t *)user_data;
    if (event == NULL) {
        return;
    }
    if (s_listener != NULL) {
        s_listener(event, s_listener_user_data);
    } else {
        free_event_payload(event);
    }
    free(event);
}

// Post event to listener on the LVGL task.
static void post_event(const bus_event_t *event)
{
    if (event == NULL) {
        return;
    }
    bus_event_t *copy = malloc(sizeof(*copy));
    if (copy == NULL) {
        ESP_LOGE(TAG, "Unable to queue bus event: out of memory");
        return;
    }
    memcpy(copy, event, sizeof(*copy));
    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(TAG, "Unable to lock LVGL while queueing bus event");
        free_event_payload(copy);
        free(copy);
        return;
    }
    const lv_res_t result = lv_async_call(deliver_event_async, copy);
    lvgl_port_unlock();
    if (result != LV_RES_OK) {
        ESP_LOGE(TAG, "Unable to queue bus event on LVGL task");
        free_event_payload(copy);
        free(copy);
    }
}
