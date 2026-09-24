#include "bus_service.h"
#include "bus_routes.h"

#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "cJSON.h"

static const char *TAG = "bus_service";

#define KMB_BASE_URL "https://data.etabus.gov.hk/v1/transport/kmb"
#define CTB_BASE_URL "https://rt.data.gov.hk/v2/transport/citybus"

// Request types
typedef enum {
    REQ_TYPE_ROUTE,
    REQ_TYPE_STOPS,
    REQ_TYPE_STOP_DETAIL,
    REQ_TYPE_ETA,
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

// Service state
static TaskHandle_t s_worker_task = NULL;
static QueueHandle_t s_request_queue = NULL;
static bus_listener_t s_listener = NULL;
static void *s_listener_user_data = NULL;
static uint32_t s_next_request_id = 1;
static volatile bool s_cancel_all = false;

// Forward declarations
static void bus_worker_task(void *arg);
static void process_route_request(const bus_request_t *req);
static void process_stops_request(const bus_request_t *req);
static void process_eta_request(const bus_request_t *req);
static void post_event(const bus_event_t *event);
static char *normalize_stop_id(char *stop_id);

void bus_service_init(void)
{
    if (s_worker_task != NULL) {
        return;  // Already initialized
    }

    s_request_queue = xQueueCreate(4, sizeof(bus_request_t));
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
            }
        }
    }
}

// HTTP helper
static esp_err_t http_get_json(const char *url, cJSON **out_json)
{
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
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

    buffer[total_read] = '\0';

    // Don't call close() to preserve TLS session
    esp_http_client_cleanup(client);

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

    // For now, just query KMB if available
    if (ops & 0x01) {
        snprintf(url, sizeof(url), "%s/route/%s", KMB_BASE_URL, req->route);

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

                                if (route && bound && orig_en && dest_en) {
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
        snprintf(url, sizeof(url), "%s/route-stop/%s/%c/%d",
                 KMB_BASE_URL, req->route, req->bound, req->service_type);
    } else {
        snprintf(url, sizeof(url), "%s/route-stop/CTB/%s/%s",
                 CTB_BASE_URL, req->route, req->bound == 'O' ? "outbound" : "inbound");
    }

    if (http_get_json(url, &root) == ESP_OK && root != NULL) {
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

// Post event to listener
static void post_event(const bus_event_t *event)
{
    if (s_listener != NULL) {
        // Call listener directly (already on worker task, will be refactored to post to LVGL task)
        s_listener(event, s_listener_user_data);
    }
}
