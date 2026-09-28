#include "bus_service.h"
#include "bus_routes.h"
#include "crystal_network.h"
#include "crystal_http.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <errno.h>
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
#define CTB_BASE_URL "https://rt.data.gov.hk/v2/transport/citybus"
#define ROUTE_CACHE_PATH "/spiffs/bus_route_catalog.bin"
#define ROUTE_CACHE_TEMP_PATH "/spiffs/bus_route_catalog.tmp"
#define ROUTE_CACHE_MAGIC 0x42524331u
#define ROUTE_CACHE_VERSION 4u
#define ROUTE_CACHE_MAX_AGE_SECONDS (7u * 24u * 60u * 60u)
#define BUS_HTTP_TIMEOUT_MS 15000
#define BUS_ROUTE_FETCH_ATTEMPTS 10
#define BUS_STOP_FETCH_ATTEMPTS BUS_ROUTE_FETCH_ATTEMPTS
#define KMB_VARIANT_CAPACITY 2048
#define KMB_CATALOG_OWNER_ID 0x4B4D4243u // "KMBC"
#define KMB_CATALOG_MAX_BODY_BYTES (512u * 1024u)
#define KMB_CATALOG_HANDOFF_WAIT_MS (BUS_HTTP_TIMEOUT_MS * BUS_ROUTE_FETCH_ATTEMPTS + 10000u)

// Request types
typedef enum {
    REQ_TYPE_ROUTE,
    REQ_TYPE_STOPS,
    REQ_TYPE_STOP_DETAIL,
    REQ_TYPE_ETA,
    REQ_TYPE_ROUTE_CATALOG,
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

// Service state
static TaskHandle_t s_worker_task = NULL;
static QueueHandle_t s_request_queue = NULL;
static bus_listener_t s_listener = NULL;
static void *s_listener_user_data = NULL;
static uint32_t s_next_request_id = 1;
static volatile bool s_cancel_all = false;
static time_t s_route_cache_fetched_at = 0;
static uint8_t s_route_cache_provider_mask = 0;
static bus_route_variant_t *s_kmb_variants = NULL;
static uint16_t s_kmb_variant_count = 0;

// Forward declarations
static void bus_worker_task(void *arg);
static void process_route_request(const bus_request_t *req);
static void process_stops_request(const bus_request_t *req);
static void process_eta_request(const bus_request_t *req);
static void process_route_catalog_request(const bus_request_t *req);
static void post_event(const bus_event_t *event);
static void deliver_event_async(void *user_data);
static void free_event_payload(bus_event_t *event);
static char *normalize_stop_id(char *stop_id);
static bool load_route_catalog_cache(void);
static bool wait_for_network(void);
static kmb_catalog_handoff_t *kmb_catalog_handoff_create(uint32_t bus_request_id,
                                                          const char *kmb_url);
static void kmb_catalog_handoff_cleanup(kmb_catalog_handoff_t *context);
static void kmb_catalog_diagnostic_callback(const crystal_http_response_t *response,
                                            void *user_data);
static uint16_t submit_kmb_catalog_diagnostic(uint32_t bus_request_id);
static uint16_t resolve_route_provider_json(const char *label, cJSON *root,
                                            uint8_t op, uint32_t request_id);

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

static void kmb_catalog_diagnostic_callback(const crystal_http_response_t *response,
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
        ESP_LOGI(TAG, "KMB catalog diagnostic bus_id=%lu framework_id=%lu status=%d body=%u attempts=%u error=%s",
                 (unsigned long)context->bus_request_id,
                 (unsigned long)context->framework_request_id,
                 response->status_code, (unsigned)response->body_len,
                 (unsigned)response->attempts,
                 esp_err_to_name(response->transport_error));
        crystal_http_response_release(response);
    } else {
        ESP_LOGW(TAG, "KMB catalog diagnostic bus_id=%lu framework_id=%lu missing response",
                 (unsigned long)context->bus_request_id,
                 (unsigned long)context->framework_request_id);
    }
    context->completed = true;
    xSemaphoreGive(context->completed_signal);
    if (context->timed_out) {
        kmb_catalog_handoff_cleanup(context);
    }
}

static uint16_t submit_kmb_catalog_diagnostic(uint32_t bus_request_id)
{
    kmb_catalog_handoff_t *context = kmb_catalog_handoff_create(
        bus_request_id, KMB_BASE_URL "/route/");
    if (context == NULL) {
        ESP_LOGE(TAG, "KMB catalog diagnostic context allocation failed");
        return 0;
    }
    crystal_http_options_t options = {
        .url = context->kmb_url,
        .timeout_ms = BUS_HTTP_TIMEOUT_MS,
        .max_attempts = BUS_ROUTE_FETCH_ATTEMPTS,
        .retry_backoff_ms = 500,
        .retry_backoff_max_ms = 8000,
        .max_body_bytes = KMB_CATALOG_MAX_BODY_BYTES,
        .keep_alive = false,
        .owner_id = KMB_CATALOG_OWNER_ID,
    };
    context->framework_request_id = crystal_http_get(
        &options, kmb_catalog_diagnostic_callback, context);
    if (context->framework_request_id == 0) {
        ESP_LOGW(TAG, "KMB catalog diagnostic queue failed bus_id=%lu",
                 (unsigned long)bus_request_id);
        kmb_catalog_handoff_cleanup(context);
        return 0;
    }
    ESP_LOGI(TAG, "KMB catalog diagnostic queued bus_id=%lu framework_id=%lu",
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

    if (xQueueSend(s_request_queue, &req, 0) != pdTRUE) {
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
    for (uint16_t i = 0; s_kmb_variants != NULL && i < s_kmb_variant_count && copied < max_count; i++) {
        if (strcmp(s_kmb_variants[i].route, route) == 0) {
            out[copied++] = s_kmb_variants[i];
        }
    }
    return copied;
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

    if (xQueueSend(s_request_queue, &req, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Request queue full");
        return 0;
    }

    return req.id;
}

uint32_t bus_service_request_stop_detail(const char *stop_id, uint8_t op)
{
    if (s_request_queue == NULL) {
        bus_service_init();
    }

    bus_request_t req = {0};
    req.type = REQ_TYPE_STOP_DETAIL;
    req.id = s_next_request_id++;
    strlcpy(req.stop_id, stop_id, sizeof(req.stop_id));
    req.op = op;

    if (xQueueSend(s_request_queue, &req, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Request queue full");
        return 0;
    }

    return req.id;
}

uint32_t bus_service_request_eta(const char *stop_id,
                                   const char *route,
                                   uint8_t op,
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
    // Clear queue
    if (s_request_queue != NULL) {
        xQueueReset(s_request_queue);
    }
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
                continue;
            }

            ESP_LOGI(TAG, "Processing request id=%lu type=%d", req.id, req.type);

            switch (req.type) {
                case REQ_TYPE_ROUTE:
                    process_route_request(&req);
                    break;
                case REQ_TYPE_STOPS:
                    process_stops_request(&req);
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
            }
        }
    }
}

// HTTP helper
static esp_err_t http_get_json(const char *url, cJSON **out_json)
{
    if (!crystal_network_has_ip()) {
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

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    if (content_length < 0) {
        ESP_LOGW(TAG, "HTTP headers unavailable (status=%d)", status);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    if (status != 200) {
        ESP_LOGW(TAG, "HTTP %d", status);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    // Allocate buffer for response (use PSRAM)
    char *buffer = heap_caps_malloc(content_length + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes", content_length);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    int total_read = 0;
    while (total_read < content_length) {
        int read_len = esp_http_client_read(client, buffer + total_read, content_length - total_read);
        if (read_len <= 0) {
            break;
        }
        total_read += read_len;
    }

    if (total_read != content_length) {
        ESP_LOGW(TAG, "HTTP body incomplete: read %d of %d bytes", total_read, content_length);
    }

    buffer[total_read] = '\0';

    // Don't call close() to preserve TLS session
    esp_http_client_cleanup(client);
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
    uint16_t reserved2;
} route_cache_header_t;

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
    free(s_kmb_variants);
    s_kmb_variants = NULL;
    s_kmb_variant_count = 0;
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
    if (header.variant_count > KMB_VARIANT_CAPACITY) {
        bus_route_catalog_reset();
        fclose(file);
        ESP_LOGW(TAG, "Route catalog cache: invalid variant count");
        return false;
    }
    if (header.variant_count > 0) {
        s_kmb_variants = heap_caps_calloc(header.variant_count, sizeof(*s_kmb_variants),
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (header.variant_count > 0 && s_kmb_variants == NULL) {
        bus_route_catalog_reset();
        fclose(file);
        ESP_LOGW(TAG, "Route catalog cache: no memory for variants");
        return false;
    }
    for (uint16_t i = 0; i < header.variant_count; i++) {
        if (fread(&s_kmb_variants[i], sizeof(s_kmb_variants[i]), 1, file) != 1) {
            bus_route_catalog_reset();
            free(s_kmb_variants);
            s_kmb_variants = NULL;
            s_kmb_variant_count = 0;
            fclose(file);
            ESP_LOGW(TAG, "Route catalog cache: truncated variant data");
            return false;
        }
    }
    s_kmb_variant_count = header.variant_count;
    s_route_cache_fetched_at = (time_t)header.fetched_at;
    s_route_cache_provider_mask = header.provider_mask;
    fclose(file);
    const time_t now = time(NULL);
    if (s_route_cache_fetched_at > 0 && now > s_route_cache_fetched_at) {
        ESP_LOGI(TAG, "Route catalog cache: loaded %u routes (%ld days old)",
                 bus_route_catalog_count(),
                 (long)((now - s_route_cache_fetched_at) / 86400));
    } else {
        ESP_LOGI(TAG, "Route catalog cache: loaded %u routes (clock unavailable)",
                 bus_route_catalog_count());
    }
    return true;
}

static bool save_route_catalog_cache(uint8_t provider_mask)
{
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
        .variant_count = s_kmb_variant_count,
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
        ok = fwrite(&s_kmb_variants[i], sizeof(*s_kmb_variants), 1, file) == 1;
        if (!ok) {
            ESP_LOGE(TAG, "Route catalog cache: variant write failed at %u (errno=%d)", i, errno);
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
        ESP_LOGI(TAG, "Route catalog: saved data in filesystem (%u routes, %u KMB variants)",
                 header.count, header.variant_count);
    }
    return ok;
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
    if (op == (1u << BUS_OP_KMB)) {
        free(s_kmb_variants);
        s_kmb_variants = heap_caps_calloc(KMB_VARIANT_CAPACITY, sizeof(*s_kmb_variants),
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_kmb_variant_count = 0;
    }
    if (cJSON_IsArray(data)) {
        const int count = cJSON_GetArraySize(data);
        for (int i = 0; i < count; i++) {
            cJSON *item = cJSON_GetArrayItem(data, i);
            cJSON *route = item != NULL ? cJSON_GetObjectItem(item, "route") : NULL;
            if (route != NULL && cJSON_IsString(route) &&
                bus_route_catalog_add(route->valuestring, strlen(route->valuestring), op)) {
                fetched++;
            }
            if (op == (1u << BUS_OP_KMB) && s_kmb_variants != NULL && route != NULL &&
                cJSON_IsString(route) && s_kmb_variant_count < KMB_VARIANT_CAPACITY) {
                cJSON *bound = cJSON_GetObjectItem(item, "bound");
                cJSON *service_type = cJSON_GetObjectItem(item, "service_type");
                cJSON *orig_en = cJSON_GetObjectItem(item, "orig_en");
                cJSON *dest_en = cJSON_GetObjectItem(item, "dest_en");
                if (cJSON_IsString(bound) && cJSON_IsString(orig_en) && cJSON_IsString(dest_en) &&
                    bound->valuestring[0] != '\0') {
                    bus_route_variant_t *variant = &s_kmb_variants[s_kmb_variant_count++];
                    strlcpy(variant->route, route->valuestring, sizeof(variant->route));
                    variant->op = BUS_OP_KMB;
                    variant->bound = bound->valuestring[0];
                    variant->service_type = cJSON_IsNumber(service_type) ? service_type->valueint : 1;
                    strlcpy(variant->orig_en, orig_en->valuestring, sizeof(variant->orig_en));
                    strlcpy(variant->dest_en, dest_en->valuestring, sizeof(variant->dest_en));
                }
            }
        }
    }
    cJSON_Delete(root);
    if (fetched == 0) {
        ESP_LOGE(TAG, "Route catalog: %s returned no routes", label);
    } else {
        ESP_LOGI(TAG, "Route catalog: resolved %u %s route records (%u KMB variants)",
                 fetched, label, s_kmb_variant_count);
    }
    return fetched;
}

static uint16_t fetch_route_provider(const char *label, const char *url, uint8_t op, uint32_t request_id)
{
    cJSON *root = NULL;
    for (uint8_t attempt = 1; attempt <= BUS_ROUTE_FETCH_ATTEMPTS; attempt++) {
        bus_event_t progress = {0};
        progress.type = BUS_EVT_ROUTE_CATALOG_PROGRESS;
        progress.request_id = request_id;
        snprintf(progress.data.route_catalog_progress.message,
                 sizeof(progress.data.route_catalog_progress.message),
                 "Downloading %s route data...\nPlease wait", label);
        post_event(&progress);
        ESP_LOGI(TAG, "Route catalog: downloading data from %s API %s (attempt %u/%u)",
                 label, url, (unsigned)attempt, (unsigned)BUS_ROUTE_FETCH_ATTEMPTS);
        if (http_get_json(url, &root) == ESP_OK && root != NULL) {
            ESP_LOGI(TAG, "Route catalog: data downloaded from %s API", label);
            bus_event_t downloaded = {0};
            downloaded.type = BUS_EVT_ROUTE_CATALOG_PROGRESS;
            downloaded.request_id = request_id;
            snprintf(downloaded.data.route_catalog_progress.message,
                     sizeof(downloaded.data.route_catalog_progress.message),
                     "%s data downloaded\nResolving route data...", label);
            post_event(&downloaded);
            break;
        }
        root = NULL;
        if (attempt < BUS_ROUTE_FETCH_ATTEMPTS) {
            ESP_LOGW(TAG, "Route catalog: %s request failed; retrying", label);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
    if (root == NULL) {
        ESP_LOGE(TAG, "Route catalog: %s fetch failed after %u attempts",
                 label, BUS_ROUTE_FETCH_ATTEMPTS);
        return 0;
    }

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
    // `op` is the route catalog operator bitmask here (KMB = 1), while
    // bus_route_variant_t::op stores the enum value (KMB = 0).
    if (op == (1u << BUS_OP_KMB)) {
        free(s_kmb_variants);
        s_kmb_variants = heap_caps_calloc(KMB_VARIANT_CAPACITY, sizeof(*s_kmb_variants),
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_kmb_variant_count = 0;
        if (s_kmb_variants == NULL) {
            ESP_LOGE(TAG, "Route catalog: no memory for KMB variants");
        }
    }
    if (cJSON_IsArray(data)) {
        const int count = cJSON_GetArraySize(data);
        for (int i = 0; i < count; i++) {
            cJSON *item = cJSON_GetArrayItem(data, i);
            cJSON *route = item != NULL ? cJSON_GetObjectItem(item, "route") : NULL;
            if (route != NULL && cJSON_IsString(route) &&
                bus_route_catalog_add(route->valuestring, strlen(route->valuestring), op)) {
                fetched++;
            }
            if (op == (1u << BUS_OP_KMB) && s_kmb_variants != NULL && item != NULL && route != NULL &&
                cJSON_IsString(route) && s_kmb_variant_count < KMB_VARIANT_CAPACITY) {
                cJSON *bound = cJSON_GetObjectItem(item, "bound");
                cJSON *service_type = cJSON_GetObjectItem(item, "service_type");
                cJSON *orig_en = cJSON_GetObjectItem(item, "orig_en");
                cJSON *dest_en = cJSON_GetObjectItem(item, "dest_en");
                if (cJSON_IsString(bound) && cJSON_IsString(orig_en) && cJSON_IsString(dest_en) &&
                    bound->valuestring[0] != '\0') {
                    bus_route_variant_t *variant = &s_kmb_variants[s_kmb_variant_count++];
                    strlcpy(variant->route, route->valuestring, sizeof(variant->route));
                    variant->op = BUS_OP_KMB;
                    variant->bound = bound->valuestring[0];
                    variant->service_type = cJSON_IsNumber(service_type) ? service_type->valueint : 1;
                    strlcpy(variant->orig_en, orig_en->valuestring, sizeof(variant->orig_en));
                    strlcpy(variant->dest_en, dest_en->valuestring, sizeof(variant->dest_en));
                }
            }
        }
    }
    cJSON_Delete(root);
    if (fetched == 0) {
        ESP_LOGE(TAG, "Route catalog: %s returned no routes", label);
    } else {
        ESP_LOGI(TAG, "Route catalog: resolved %u %s route records (%u KMB variants)",
                 fetched, label, s_kmb_variant_count);
    }
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

    // Step 4 parses the owned KMB response before releasing its handoff.
    const uint16_t kmb_count = submit_kmb_catalog_diagnostic(req->id);
    const uint16_t ctb_count = fetch_route_provider(
        "CTB", CTB_BASE_URL "/route/ctb", 2u, req->id);
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
        event.status = ESP_FAIL;
        event.data.route_catalog.route_count = bus_route_catalog_count();
        ESP_LOGE(TAG, "Route catalog: update failed; cached catalog %savailable",
                 had_cache ? "" : "un");
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
    char url[256];
    cJSON *root = NULL;
    bus_event_t event = {0};
    event.type = BUS_EVT_ROUTE_VARIANTS;
    event.request_id = req->id;

    // Check which operators have this route
    uint8_t ops = bus_route_get_operators(req->route, strlen(req->route));

    // The KMB API exposes route records through the collection endpoint.
    // Filter the returned records to the selected route locally.
    if (ops & 0x01) {
        snprintf(url, sizeof(url), "%s/route/", KMB_BASE_URL);
        ESP_LOGI(TAG, "Route variants: fetching KMB collection for %s", req->route);

        if (http_get_json(url, &root) == ESP_OK && root != NULL) {
            cJSON *data = cJSON_GetObjectItem(root, "data");
            if (cJSON_IsArray(data)) {
                int count = cJSON_GetArraySize(data);
                if (count > 0) {
                    bus_route_variant_t *variants = heap_caps_calloc(count, sizeof(bus_route_variant_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (variants != NULL) {
                        int valid_count = 0;
                        for (int i = 0; i < count; i++) {
                            cJSON *item = cJSON_GetArrayItem(data, i);
                            if (item != NULL) {
                                cJSON *route = cJSON_GetObjectItem(item, "route");
                                cJSON *bound = cJSON_GetObjectItem(item, "bound");
                                cJSON *service_type = cJSON_GetObjectItem(item, "service_type");
                                cJSON *orig_en = cJSON_GetObjectItem(item, "orig_en");
                                cJSON *dest_en = cJSON_GetObjectItem(item, "dest_en");

                                if (cJSON_IsString(route) &&
                                    strcmp(route->valuestring, req->route) == 0 &&
                                    cJSON_IsString(bound) && cJSON_IsString(orig_en) &&
                                    cJSON_IsString(dest_en)) {
                                    strlcpy(variants[valid_count].route, route->valuestring, sizeof(variants[valid_count].route));
                                    variants[valid_count].op = BUS_OP_KMB;
                                    variants[valid_count].bound = bound->valuestring[0];
                                    variants[valid_count].service_type = service_type ? service_type->valueint : 1;
                                    strlcpy(variants[valid_count].orig_en, orig_en->valuestring, sizeof(variants[valid_count].orig_en));
                                    strlcpy(variants[valid_count].dest_en, dest_en->valuestring, sizeof(variants[valid_count].dest_en));
                                    valid_count++;
                                }
                            }
                        }

                        event.data.route_variants.variants = variants;
                        event.data.route_variants.count = valid_count;
                        event.status = ESP_OK;
                        ESP_LOGI(TAG, "Route variants: found %d KMB records for %s",
                                 valid_count, req->route);
                        post_event(&event);

                        // Don't free variants here - listener owns them
                    }
                }
            }
            cJSON_Delete(root);
            return;
        }
    }

    // Error case
    event.status = ESP_FAIL;
    strlcpy(event.data.error.message, "Route not found", sizeof(event.data.error.message));
    post_event(&event);
}

// Process stops request
static void process_stops_request(const bus_request_t *req)
{
    char url[256];
    cJSON *root = NULL;
    bus_event_t event = {0};
    event.type = BUS_EVT_STOPS_LIST;
    event.request_id = req->id;

    if (req->op == BUS_OP_KMB) {
        // KMB route data uses I/O bound codes, but the live route-stop
        // endpoint requires the path words "inbound" and "outbound".
        const char *direction = req->bound == BUS_DIR_OUTBOUND ? "outbound" : "inbound";
        snprintf(url, sizeof(url), "%s/route-stop/%s/%s/%d",
                 KMB_BASE_URL, req->route, direction, req->service_type);
    } else {
        snprintf(url, sizeof(url), "%s/route-stop/CTB/%s/%s",
                 CTB_BASE_URL, req->route, req->bound == 'O' ? "outbound" : "inbound");
    }
    ESP_LOGI(TAG, "Route stops: fetching %s", url);

    esp_err_t request_err = ESP_FAIL;
    for (uint8_t attempt = 1; attempt <= BUS_STOP_FETCH_ATTEMPTS; attempt++) {
        root = NULL;
        ESP_LOGI(TAG, "Route stops: request attempt %u/%u", (unsigned)attempt,
                 (unsigned)BUS_STOP_FETCH_ATTEMPTS);
        request_err = http_get_json(url, &root);
        if (request_err == ESP_OK && root != NULL) {
            break;
        }
        if (attempt < BUS_STOP_FETCH_ATTEMPTS) {
            ESP_LOGW(TAG, "Route stops request failed; retrying (%u/%u)",
                     (unsigned)attempt, (unsigned)BUS_STOP_FETCH_ATTEMPTS);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    if (request_err == ESP_OK && root != NULL) {
        cJSON *data = cJSON_GetObjectItem(root, "data");
        if (cJSON_IsArray(data)) {
            int count = cJSON_GetArraySize(data);
            if (count > 0) {
                bus_stop_t *stops = heap_caps_calloc(count, sizeof(bus_stop_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (stops != NULL) {
                    int valid_count = 0;
                    for (int i = 0; i < count; i++) {
                        cJSON *item = cJSON_GetArrayItem(data, i);
                        if (item != NULL) {
                            cJSON *stop_id = cJSON_GetObjectItem(item, "stop");
                            cJSON *seq = cJSON_GetObjectItem(item, "seq");

                            if (stop_id && seq) {
                                strlcpy(stops[valid_count].stop_id, stop_id->valuestring, sizeof(stops[valid_count].stop_id));
                                normalize_stop_id(stops[valid_count].stop_id);
                                stops[valid_count].seq = seq->valueint;
                                stops[valid_count].resolved = false;
                                snprintf(stops[valid_count].name_en, sizeof(stops[valid_count].name_en), "Stop %d", seq->valueint);
                                valid_count++;
                            }
                        }
                    }

                    event.data.stops_list.stops = stops;
                    event.data.stops_list.count = valid_count;
                    event.status = ESP_OK;
                    post_event(&event);

                    // Don't free stops here - listener owns them
                }
            }
        }
        cJSON_Delete(root);
        return;
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
