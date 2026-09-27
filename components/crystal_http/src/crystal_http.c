/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

#include "crystal_http.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "crystal_http";

#define CRYSTAL_HTTP_QUEUE_DEPTH 8
#define CRYSTAL_HTTP_URL_MAX 256
#define CRYSTAL_HTTP_CONTEXT_SLOTS CRYSTAL_HTTP_QUEUE_DEPTH
#define CRYSTAL_HTTP_SYNTHETIC_ERROR ESP_ERR_NOT_SUPPORTED

typedef struct {
    bool used;
    bool cancelled;
    uint32_t request_id;
    uint32_t owner_id;
    char url[CRYSTAL_HTTP_URL_MAX];
    crystal_http_callback_t callback;
    void *context;
    uint32_t timeout_ms;
    uint8_t max_attempts;
    size_t max_body_bytes;
    bool keep_alive;
} request_slot_t;

typedef struct {
    uint8_t slot;
} queue_item_t;

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_worker;
static request_slot_t s_slots[CRYSTAL_HTTP_CONTEXT_SLOTS];
static uint32_t s_next_request_id = 1;
static bool s_ready;

static request_slot_t *slot_for(uint8_t index)
{
    return index < CRYSTAL_HTTP_CONTEXT_SLOTS ? &s_slots[index] : NULL;
}

static void deliver_request(request_slot_t *slot)
{
    const int64_t started = esp_timer_get_time();
    crystal_http_response_t response = {0};
    response.request_id = slot->request_id;
    response.status_code = 0;
    response.transport_error = slot->cancelled ? ESP_ERR_INVALID_STATE : ESP_FAIL;
    response.attempts = 1;

    if (!slot->cancelled && strncmp(slot->url, "crystal://", 10) == 0) {
        response.transport_error = CRYSTAL_HTTP_SYNTHETIC_ERROR;
    } else if (!slot->cancelled) {
        esp_http_client_config_t config = {
            .url = slot->url,
            .timeout_ms = slot->timeout_ms != 0 ? slot->timeout_ms : 15000,
            .keep_alive_enable = slot->keep_alive,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .buffer_size = 4096,
            .buffer_size_tx = 2048,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) {
            response.transport_error = ESP_ERR_NO_MEM;
        } else {
            const esp_err_t open_err = esp_http_client_open(client, 0);
            if (open_err != ESP_OK) {
                response.transport_error = open_err;
                ESP_LOGW(TAG, "request id=%lu transport failure=%s",
                         (unsigned long)slot->request_id, esp_err_to_name(open_err));
            } else {
                ESP_LOGI(TAG, "TLS connected id=%lu", (unsigned long)slot->request_id);
                const int64_t declared = esp_http_client_fetch_headers(client);
                response.status_code = esp_http_client_get_status_code(client);
                const size_t limit = slot->max_body_bytes != 0 ? slot->max_body_bytes : 8192;
                if (declared > (int64_t)limit) {
                    response.transport_error = ESP_ERR_INVALID_SIZE;
                    ESP_LOGW(TAG, "response id=%lu exceeds body limit (%lld > %u)",
                             (unsigned long)slot->request_id, (long long)declared,
                             (unsigned)limit);
                } else {
                    /* A negative length means chunked transfer. Some servers
                     * also omit Content-Length and report zero, so both cases
                     * use the same bounded read loop. */
                    size_t capacity = declared > 0 ? (size_t)declared + 1 : 1024;
                    if (capacity > limit + 1) capacity = limit + 1;
                    response.body = heap_caps_malloc(capacity,
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (response.body == NULL) {
                        response.transport_error = ESP_ERR_NO_MEM;
                    } else {
                        ESP_LOGI(TAG, "body allocation id=%lu bytes=%u memory=PSRAM",
                                 (unsigned long)slot->request_id, (unsigned)capacity);
                        size_t total = 0;
                        response.transport_error = ESP_OK;
                        while (!slot->cancelled) {
                            if (total + 1 >= capacity) {
                                if (capacity >= limit + 1) {
                                    /* Probe for another byte so an exact-limit
                                     * response is accepted while a larger one
                                     * is rejected. */
                                    char extra = 0;
                                    const int read = esp_http_client_read(client, &extra, 1);
                                    if (read > 0) response.transport_error = ESP_ERR_INVALID_SIZE;
                                    else if (read < 0) response.transport_error = ESP_FAIL;
                                    break;
                                }
                                size_t next = capacity * 2;
                                if (next > limit + 1) next = limit + 1;
                                uint8_t *grown = heap_caps_realloc(response.body, next,
                                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                                if (grown == NULL) {
                                    response.transport_error = ESP_ERR_NO_MEM;
                                    break;
                                }
                                response.body = grown;
                                capacity = next;
                            }
                            const int read = esp_http_client_read(client,
                                                                  (char *)response.body + total,
                                                                  (int)(capacity - total - 1));
                            if (read == 0) break;
                            if (read < 0) {
                                response.transport_error = ESP_FAIL;
                                break;
                            }
                            total += (size_t)read;
                        }
                        if (slot->cancelled) response.transport_error = ESP_ERR_INVALID_STATE;
                        else if (declared > 0 && total != (size_t)declared &&
                                 response.transport_error == ESP_OK) response.transport_error = ESP_FAIL;
                        response.body[total] = '\0';
                        response.body_len = total;
                        if (response.transport_error == ESP_OK) {
                            ESP_LOGI(TAG, "body received id=%lu bytes=%u",
                                     (unsigned long)slot->request_id, (unsigned)total);
                            if (total == 0) ESP_LOGW(TAG, "response id=%lu body empty",
                                                     (unsigned long)slot->request_id);
                        }
                    }
                }
                esp_http_client_close(client);
            }
            esp_http_client_cleanup(client);
        }
    }
    response.elapsed_ms = (uint32_t)((esp_timer_get_time() - started + 999) / 1000);

    if (slot->callback != NULL) {
        ESP_LOGI(TAG, "request callback id=%lu error=%s",
                 (unsigned long)response.request_id, esp_err_to_name(response.transport_error));
        slot->callback(&response, slot->context);
    }
}

static void worker_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "worker started core=%d", xPortGetCoreID());

    queue_item_t item;
    while (xQueueReceive(s_queue, &item, portMAX_DELAY) == pdTRUE) {
        request_slot_t *slot = slot_for(item.slot);
        if (slot == NULL) continue;

        deliver_request(slot);

        if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
            memset(slot, 0, sizeof(*slot));
            xSemaphoreGive(s_lock);
        }
    }
    vTaskDelete(NULL);
}

bool crystal_http_init(void)
{
    if (s_ready) return true;

    s_lock = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(CRYSTAL_HTTP_QUEUE_DEPTH, sizeof(queue_item_t));
    if (s_lock == NULL || s_queue == NULL) {
        if (s_queue != NULL) vQueueDelete(s_queue);
        if (s_lock != NULL) vSemaphoreDelete(s_lock);
        s_queue = NULL;
        s_lock = NULL;
        return false;
    }

    if (xTaskCreatePinnedToCore(worker_task, "crystal_http", 8192, NULL, 2,
                                &s_worker, 0) != pdPASS) {
        vQueueDelete(s_queue);
        vSemaphoreDelete(s_lock);
        s_queue = NULL;
        s_lock = NULL;
        s_worker = NULL;
        return false;
    }

    s_ready = true;
    ESP_LOGI(TAG, "component initialized");
    ESP_LOGI(TAG, "queue depth=%d", CRYSTAL_HTTP_QUEUE_DEPTH);
    return true;
}

bool crystal_http_is_ready(void)
{
    return s_ready;
}

uint32_t crystal_http_get(const crystal_http_options_t *options,
                          crystal_http_callback_t callback,
                          void *context)
{
    if (!s_ready || options == NULL || callback == NULL || options->url == NULL ||
            options->url[0] == '\0' || strlen(options->url) >= CRYSTAL_HTTP_URL_MAX) {
        return 0;
    }

    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return 0;

    int free_slot = -1;
    for (int i = 0; i < CRYSTAL_HTTP_CONTEXT_SLOTS; ++i) {
        if (!s_slots[i].used) {
            free_slot = i;
            break;
        }
    }
    if (free_slot < 0) {
        xSemaphoreGive(s_lock);
        return 0;
    }

    request_slot_t *slot = &s_slots[free_slot];
    memset(slot, 0, sizeof(*slot));
    slot->used = true;
    slot->request_id = s_next_request_id++;
    if (slot->request_id == 0) slot->request_id = s_next_request_id++;
    slot->owner_id = options->owner_id;
    strlcpy(slot->url, options->url, sizeof(slot->url));
    slot->callback = callback;
    slot->context = context;
    slot->timeout_ms = options->timeout_ms;
    slot->max_attempts = options->max_attempts;
    slot->max_body_bytes = options->max_body_bytes;
    slot->keep_alive = options->keep_alive;

    queue_item_t item = {.slot = (uint8_t)free_slot};
    if (xQueueSend(s_queue, &item, 0) != pdTRUE) {
        memset(slot, 0, sizeof(*slot));
        xSemaphoreGive(s_lock);
        return 0;
    }
    const uint32_t request_id = slot->request_id;
    ESP_LOGI(TAG, "request queued id=%lu owner=%lu",
             (unsigned long)request_id, (unsigned long)slot->owner_id);
    xSemaphoreGive(s_lock);
    return request_id;
}

bool crystal_http_cancel(uint32_t request_id)
{
    if (!s_ready || request_id == 0 || xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    bool found = false;
    for (size_t i = 0; i < CRYSTAL_HTTP_CONTEXT_SLOTS; ++i) {
        if (s_slots[i].used && s_slots[i].request_id == request_id) {
            s_slots[i].cancelled = true;
            found = true;
            break;
        }
    }
    xSemaphoreGive(s_lock);
    return found;
}

size_t crystal_http_cancel_owner(uint32_t owner_id)
{
    if (!s_ready || xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return 0;
    size_t count = 0;
    for (size_t i = 0; i < CRYSTAL_HTTP_CONTEXT_SLOTS; ++i) {
        if (s_slots[i].used && s_slots[i].owner_id == owner_id) {
            s_slots[i].cancelled = true;
            ++count;
        }
    }
    xSemaphoreGive(s_lock);
    return count;
}

void crystal_http_response_release(const crystal_http_response_t *response)
{
    if (response == NULL) return;
    free(response->body);
    ESP_LOGI(TAG, "response released id=%lu", (unsigned long)response->request_id);
}
