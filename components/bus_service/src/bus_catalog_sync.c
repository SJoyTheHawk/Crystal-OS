#include "bus_catalog_sync.h"

#include "crystal_http.h"
#include "crystal_network.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "psa/crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#ifndef CRYSTAL_BUS_CATALOG_MANIFEST_URL
#define CRYSTAL_BUS_CATALOG_MANIFEST_URL ""
#endif

#define CATALOG_OWNER_ID 0x53544D44u
#define MANIFEST_MAX_BYTES (16u * 1024u)
#define ARTIFACT_MAX_BYTES (768u * 1024u)
/*
 * Catalog transfers are background work.  Keep the socket inactivity window
 * short so a foreground route request or a Wi-Fi loss can release the HTTP
 * worker promptly instead of waiting behind a stalled large-body read.
 * KMB/CTB artifacts arrive in many chunks, so this is an inactivity timeout,
 * not a total transfer limit.
 */
#define HTTP_TIMEOUT_MS 5000u
#define HTTP_ATTEMPTS 2u
#define RESULT_WAIT_MS 45000u
#define RETRY_DELAY_US (5LL * 60LL * 1000000LL)
#define CHECK_DELAY_US (24LL * 60LL * 60LL * 1000000LL)

static const char *TAG = "bus_catalog_sync";

typedef struct {
    const char *name;
    const char *artifact;
    const char *candidate;
    uint8_t tag;
    char revision[65];
} provider_state_t;

typedef struct {
    char sha[65];
    size_t bytes;
    uint32_t records;
    bool valid;
} provider_target_t;

static provider_state_t s_providers[2] = {
    {.name = "KMB", .artifact = "kmb-stops.bsc",
     .candidate = "/spiffs/kmb-stops.candidate", .tag = 1},
    {.name = "CTB", .artifact = "ctb-stops.bsc",
     .candidate = "/spiffs/ctb-stops.candidate", .tag = 2},
};
static QueueHandle_t s_results;
static TaskHandle_t s_task;
static atomic_uint s_foreground_count;
static atomic_bool s_network_ready;
static atomic_bool s_refresh_requested;
static atomic_bool s_shutdown_requested;
static int64_t s_next_check_us;

static bool manifest_configured(void)
{
    const char *url = CRYSTAL_BUS_CATALOG_MANIFEST_URL;
    const char *suffix = "/manifest.json";
    const size_t length = strlen(url);
    const size_t suffix_length = strlen(suffix);
    return length < 256 && length > 8 + suffix_length &&
           strncmp(url, "https://", 8) == 0 &&
           strcmp(url + length - suffix_length, suffix) == 0;
}

static void wake_worker(void)
{
    if (s_task != NULL) xTaskNotifyGive(s_task);
}

static bool available(void)
{
    return atomic_load(&s_foreground_count) == 0 &&
           atomic_load(&s_network_ready) && crystal_network_has_ip() &&
           crystal_http_is_ready() &&
           !atomic_load(&s_shutdown_requested);
}

static void response_callback(const crystal_http_response_t *response, void *context)
{
    (void)context;
    if (response == NULL) return;
    if (s_results == NULL || xQueueSend(s_results, response, 0) != pdTRUE) {
        crystal_http_response_release(response);
        ESP_LOGE(TAG, "Catalog completion queue full; response released");
    }
}

static bool request(const char *url, size_t max_bytes,
                    crystal_http_response_t *result)
{
    crystal_http_options_t options = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .max_attempts = HTTP_ATTEMPTS,
        .retry_backoff_ms = 500,
        .retry_backoff_max_ms = 2000,
        .max_body_bytes = max_bytes,
        .keep_alive = false,
        .owner_id = CATALOG_OWNER_ID,
    };
    const uint32_t id = crystal_http_get(&options, response_callback, NULL);
    if (id == 0) return false;

    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(RESULT_WAIT_MS);
    while (true) {
        crystal_http_response_t incoming = {0};
        const TickType_t now = xTaskGetTickCount();
        if (now >= deadline ||
            xQueueReceive(s_results, &incoming, deadline - now) != pdTRUE) {
            (void)crystal_http_cancel(id);
            ESP_LOGW(TAG, "Catalog request timed out id=%lu", (unsigned long)id);
            return false;
        }
        if (incoming.request_id == id) {
            *result = incoming;
            return true;
        }
        crystal_http_response_release(&incoming);
    }
}

static bool valid_sha(const char *sha)
{
    if (sha == NULL || strlen(sha) != 64) return false;
    for (size_t i = 0; i < 64; ++i) {
        if (!((sha[i] >= '0' && sha[i] <= '9') ||
              (sha[i] >= 'a' && sha[i] <= 'f'))) return false;
    }
    return true;
}

static bool parse_target(const cJSON *root, const provider_state_t *provider,
                         provider_target_t *target)
{
    const cJSON *providers = cJSON_GetObjectItemCaseSensitive(root, "providers");
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(providers, provider->name);
    const cJSON *artifact = cJSON_GetObjectItemCaseSensitive(item, "artifact");
    const cJSON *sha = cJSON_GetObjectItemCaseSensitive(item, "artifact_sha256");
    const cJSON *bytes = cJSON_GetObjectItemCaseSensitive(item, "artifact_bytes");
    const cJSON *records = cJSON_GetObjectItemCaseSensitive(item, "records");
    if (!cJSON_IsObject(item) || !cJSON_IsString(artifact) ||
        strcmp(artifact->valuestring, provider->artifact) != 0 ||
        !cJSON_IsString(sha) || !valid_sha(sha->valuestring) ||
        !cJSON_IsNumber(bytes) || bytes->valuedouble < 16 ||
        bytes->valuedouble > ARTIFACT_MAX_BYTES ||
        bytes->valuedouble != (double)bytes->valueint ||
        !cJSON_IsNumber(records) || records->valueint <= 0 ||
        records->valueint > ARTIFACT_MAX_BYTES / 36 ||
        records->valuedouble != (double)records->valueint) return false;
    memcpy(target->sha, sha->valuestring, 65);
    target->bytes = (size_t)bytes->valueint;
    target->records = (uint32_t)records->valueint;
    target->valid = true;
    return true;
}

static bool parse_manifest(const crystal_http_response_t *response,
                           provider_target_t targets[2])
{
    if (response->body == NULL || response->body_len == 0 ||
        response->body_len > MANIFEST_MAX_BYTES) return false;
    cJSON *root = cJSON_ParseWithLength((const char *)response->body,
                                       response->body_len);
    if (root == NULL) return false;
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    const bool valid = cJSON_IsString(schema) &&
                       strcmp(schema->valuestring, "crystal-stop-catalog-v2") == 0;
    for (size_t i = 0; valid && i < 2; ++i) {
        if (!parse_target(root, &s_providers[i], &targets[i])) {
            ESP_LOGW(TAG, "%s manifest entry invalid", s_providers[i].name);
        }
    }
    cJSON_Delete(root);
    return valid;
}

static bool artifact_valid(const crystal_http_response_t *response,
                           const provider_state_t *provider,
                           const provider_target_t *target)
{
    if (response->body == NULL || response->body_len != target->bytes ||
        response->body_len < 16 ||
        memcmp(response->body, "BSC2", 4) != 0 ||
        response->body[4] != 2 || response->body[5] != 0 ||
        response->body[6] != provider->tag) return false;
    const uint8_t *body = response->body;
    const uint32_t count = (uint32_t)body[8] | (uint32_t)body[9] << 8 |
                           (uint32_t)body[10] << 16 | (uint32_t)body[11] << 24;
    const uint32_t pool = (uint32_t)body[12] | (uint32_t)body[13] << 8 |
                          (uint32_t)body[14] << 16 | (uint32_t)body[15] << 24;
    if (count != target->records ||
        16ULL + (uint64_t)count * 36ULL + pool != response->body_len) return false;
    unsigned char digest[32];
    char hex[65];
    size_t digest_len = 0;
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_compute(PSA_ALG_SHA_256, response->body, response->body_len,
                         digest, sizeof(digest), &digest_len) != PSA_SUCCESS ||
        digest_len != sizeof(digest))
        return false;
    for (size_t i = 0; i < 32; ++i) {
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    }
    return strcmp(hex, target->sha) == 0;
}

static bool save_candidate(const provider_state_t *provider,
                           const crystal_http_response_t *response)
{
    size_t capacity = 0;
    size_t used = 0;
    if (esp_spiffs_info("storage", &capacity, &used) != ESP_OK) {
        ESP_LOGE(TAG, "%s SPIFFS information unavailable", provider->name);
        return false;
    }
    if (used > capacity) return false;
    ESP_LOGI(TAG, "%s SPIFFS before candidate total=%u used=%u free=%u candidate=%u",
             provider->name, (unsigned)capacity, (unsigned)used,
             (unsigned)(capacity - used), (unsigned)response->body_len);
    if (capacity - used < response->body_len) return false;
    char incoming[64];
    if (snprintf(incoming, sizeof(incoming), "%s.incoming", provider->candidate) >=
        (int)sizeof(incoming)) return false;
    FILE *file = fopen(incoming, "wb");
    if (file == NULL) return false;
    size_t written = 0;
    while (written < response->body_len && available()) {
        const size_t remaining = response->body_len - written;
        const size_t chunk = remaining < 4096 ? remaining : 4096;
        const size_t count = fwrite(response->body + written, 1, chunk, file);
        if (count != chunk) break;
        written += count;
        taskYIELD();
    }
    const bool closed = fclose(file) == 0;
    if (written != response->body_len || !closed || !available()) {
        remove(incoming);
        return false;
    }
    if (rename(incoming, provider->candidate) != 0) {
        // Candidates are staging files only; 4R.3 owns active A/B generations.
        if (remove(provider->candidate) != 0 ||
            rename(incoming, provider->candidate) != 0) {
            remove(incoming);
            return false;
        }
    }
    if (esp_spiffs_info("storage", &capacity, &used) == ESP_OK) {
        ESP_LOGI(TAG, "%s SPIFFS after candidate total=%u used=%u free=%u",
                 provider->name, (unsigned)capacity, (unsigned)used,
                 (unsigned)(capacity - used));
    }
    return true;
}

static bool artifact_url(const char *artifact, char out[256])
{
    const char *manifest = CRYSTAL_BUS_CATALOG_MANIFEST_URL;
    const char *last_slash = strrchr(manifest, '/');
    if (last_slash == NULL || strcmp(last_slash, "/manifest.json") != 0)
        return false;
    const size_t prefix = (size_t)(last_slash - manifest) + 1;
    const size_t length = prefix + strlen(artifact);
    if (length >= 256) return false;
    memcpy(out, manifest, prefix);
    strcpy(out + prefix, artifact);
    return true;
}

static bool transfer(void)
{
    crystal_http_response_t response = {0};
    provider_target_t targets[2] = {0};
    if (!request(CRYSTAL_BUS_CATALOG_MANIFEST_URL, MANIFEST_MAX_BYTES,
                 &response)) return false;
    ESP_LOGI(TAG, "manifest status=%d error=%s body=%u attempts=%u tls_internal=%u tls_psram=%u",
             response.status_code, esp_err_to_name(response.transport_error),
             (unsigned)response.body_len, (unsigned)response.attempts,
             (unsigned)response.tls_internal_free_min,
             (unsigned)response.tls_psram_free_min);
    const bool valid = response.transport_error == ESP_OK &&
                       response.status_code == 200 &&
                       parse_manifest(&response, targets);
    crystal_http_response_release(&response);
    if (!valid) return false;

    bool complete = true;
    for (size_t i = 0; i < 2; ++i) {
        provider_state_t *provider = &s_providers[i];
        if (!available()) return false;
        if (!targets[i].valid) {
            complete = false;
            continue;
        }
        if (strcmp(provider->revision, targets[i].sha) == 0) {
            ESP_LOGI(TAG, "%s catalog unchanged", provider->name);
            continue;
        }
        char url[256];
        if (!artifact_url(provider->artifact, url)) return false;
        response = (crystal_http_response_t){0};
        if (!request(url, ARTIFACT_MAX_BYTES, &response)) {
            complete = false;
            continue;
        }
        ESP_LOGI(TAG, "%s transfer heap internal=%u largest=%u psram=%u",
                 provider->name,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        const bool accepted = available() && response.transport_error == ESP_OK &&
                              response.status_code == 200 &&
                              artifact_valid(&response, provider, &targets[i]) &&
                              save_candidate(provider, &response);
        ESP_LOGI(TAG, "%s source=%s status=%d error=%s body=%u attempts=%u validated=%d tls_internal=%u tls_psram=%u",
                 provider->name, targets[i].sha, response.status_code,
                 esp_err_to_name(response.transport_error),
                 (unsigned)response.body_len, (unsigned)response.attempts,
                 accepted, (unsigned)response.tls_internal_free_min,
                 (unsigned)response.tls_psram_free_min);
        crystal_http_response_release(&response);
        ESP_LOGI(TAG, "%s released heap internal=%u largest=%u psram=%u",
                 provider->name,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (accepted) {
            memcpy(provider->revision, targets[i].sha, 65);
        } else {
            complete = false;
        }
    }
    return complete;
}

static void worker(void *arg)
{
    (void)arg;
    while (true) {
        const int64_t now = esp_timer_get_time();
        if (available() &&
            (atomic_exchange(&s_refresh_requested, false) ||
             now >= s_next_check_us)) {
            const bool success = transfer();
            s_next_check_us = !available() ? 0 : esp_timer_get_time() +
                              (success ? CHECK_DELAY_US : RETRY_DELAY_US);
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30000));
    }
}

bool bus_catalog_sync_init(void)
{
    if (s_task != NULL) return true;
    if (!manifest_configured()) {
        ESP_LOGW(TAG, "HTTPS catalog manifest URL unset or invalid; online sync disabled");
        return true;
    }
    s_results = xQueueCreate(2, sizeof(crystal_http_response_t));
    if (s_results == NULL) return false;
    atomic_store(&s_network_ready, crystal_network_has_ip());
    if (xTaskCreatePinnedToCore(worker, "bus_catalog_sync", 6144,
                                NULL, 2, &s_task, 0) != pdPASS) {
        vQueueDelete(s_results);
        s_results = NULL;
        return false;
    }
    return true;
}

bool bus_catalog_sync_request(bool explicit_refresh)
{
    if (s_task == NULL) return false;
    atomic_store(&s_shutdown_requested, false);
    atomic_store(&s_network_ready, crystal_network_has_ip());
    if (explicit_refresh) atomic_store(&s_refresh_requested, true);
    wake_worker();
    return true;
}

void bus_catalog_sync_foreground_begin(void)
{
    if (s_task == NULL) return;
    atomic_store(&s_shutdown_requested, false);
    atomic_fetch_add(&s_foreground_count, 1);
    (void)crystal_http_cancel_owner(CATALOG_OWNER_ID);
}

void bus_catalog_sync_foreground_end(void)
{
    if (s_task == NULL) return;
    unsigned int current = atomic_load(&s_foreground_count);
    while (current > 0 && !atomic_compare_exchange_weak(
               &s_foreground_count, &current, current - 1)) {}
    wake_worker();
}

void bus_catalog_sync_cancel_all(void)
{
    if (s_task == NULL) return;
    atomic_store(&s_shutdown_requested, true);
    atomic_store(&s_foreground_count, 0);
    (void)crystal_http_cancel_owner(CATALOG_OWNER_ID);
}

void bus_catalog_sync_network_lost(void)
{
    if (s_task == NULL) return;
    atomic_store(&s_network_ready, false);
    (void)crystal_http_cancel_owner(CATALOG_OWNER_ID);
}
