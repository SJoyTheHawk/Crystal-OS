#include "bus_catalog_sync.h"

#include "crystal_http.h"
#include "crystal_network.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>

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
#ifndef CRYSTAL_BUS_CATALOG_TEST_FAULT
#define CRYSTAL_BUS_CATALOG_TEST_FAULT "none"
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
    const char *slot[2];
    const char *commit[2];
    uint8_t tag;
    char revision[65];
    uint32_t generation;
    int active_slot;
} provider_state_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint8_t provider;
    uint8_t reserved;
    uint32_t generation;
    uint32_t bytes;
    uint32_t records;
    char sha[65];
} catalog_commit_t;

#define CATALOG_COMMIT_MAGIC 0x32474342u /* "BCG2" */
#define CATALOG_COMMIT_VERSION 1u

typedef struct {
    char sha[65];
    size_t bytes;
    uint32_t records;
    bool valid;
} provider_target_t;

static provider_state_t s_providers[2] = {
    {.name = "KMB", .artifact = "kmb-stops.bsc",
     .candidate = "/spiffs/kmb-stops.candidate",
     .slot = {"/spiffs/kmb-stops.a", "/spiffs/kmb-stops.b"},
     .commit = {"/spiffs/kmb-stops.a.commit", "/spiffs/kmb-stops.b.commit"},
     .tag = 1, .active_slot = -1},
    {.name = "CTB", .artifact = "ctb-stops.bsc",
     .candidate = "/spiffs/ctb-stops.candidate",
     .slot = {"/spiffs/ctb-stops.a", "/spiffs/ctb-stops.b"},
     .commit = {"/spiffs/ctb-stops.a.commit", "/spiffs/ctb-stops.b.commit"},
     .tag = 2, .active_slot = -1},
};
static QueueHandle_t s_results;
static TaskHandle_t s_task;
static atomic_uint s_foreground_count;
static atomic_bool s_network_ready;
static atomic_bool s_refresh_requested;
static atomic_bool s_shutdown_requested;
static atomic_bool s_cleanup_requested;
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
    const uint8_t *entries = body + 16;
    const uint8_t *strings = entries + (size_t)count * 36u;
    const uint8_t *previous_id = NULL;
    uint32_t previous_id_len = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *entry = entries + (size_t)i * 36u;
        const uint32_t id_offset = (uint32_t)entry[0] | (uint32_t)entry[1] << 8 |
                                   (uint32_t)entry[2] << 16 | (uint32_t)entry[3] << 24;
        const uint32_t id_len = (uint32_t)entry[4] | (uint32_t)entry[5] << 8 |
                                (uint32_t)entry[6] << 16 | (uint32_t)entry[7] << 24;
        const uint32_t en_offset = (uint32_t)entry[8] | (uint32_t)entry[9] << 8 |
                                   (uint32_t)entry[10] << 16 | (uint32_t)entry[11] << 24;
        const uint32_t en_len = (uint32_t)entry[12] | (uint32_t)entry[13] << 8 |
                                (uint32_t)entry[14] << 16 | (uint32_t)entry[15] << 24;
        const uint32_t tc_offset = (uint32_t)entry[16] | (uint32_t)entry[17] << 8 |
                                   (uint32_t)entry[18] << 16 | (uint32_t)entry[19] << 24;
        const uint32_t tc_len = (uint32_t)entry[20] | (uint32_t)entry[21] << 8 |
                                (uint32_t)entry[22] << 16 | (uint32_t)entry[23] << 24;
        if (id_len == 0 || id_offset > pool || id_len > pool - id_offset ||
            en_offset > pool || en_len > pool - en_offset ||
            tc_offset > pool || tc_len > pool - tc_offset ||
            (en_len == 0 && tc_len == 0)) return false;
        const uint8_t *id = strings + id_offset;
        if (previous_id != NULL && (previous_id_len > id_len ||
            (previous_id_len == id_len && memcmp(previous_id, id, id_len) >= 0))) return false;
        previous_id = id;
        previous_id_len = id_len;
        uint32_t lat_bits = (uint32_t)entry[24] | (uint32_t)entry[25] << 8 |
                            (uint32_t)entry[26] << 16 | (uint32_t)entry[27] << 24;
        uint32_t lon_bits = (uint32_t)entry[28] | (uint32_t)entry[29] << 8 |
                            (uint32_t)entry[30] << 16 | (uint32_t)entry[31] << 24;
        float lat, lon;
        memcpy(&lat, &lat_bits, sizeof(lat));
        memcpy(&lon, &lon_bits, sizeof(lon));
        if (entry[32] != 0 && (!isfinite(lat) || !isfinite(lon) || lat < -90.0f ||
                               lat > 90.0f || lon < -180.0f || lon > 180.0f)) return false;
    }
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

static bool sha256_file(const char *path, char hex[65], size_t *bytes)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    unsigned char digest[32];
    unsigned char buffer[4096];
    size_t total = 0, digest_len = 0;
    bool ok = psa_crypto_init() == PSA_SUCCESS &&
              psa_hash_setup(&operation, PSA_ALG_SHA_256) == PSA_SUCCESS;
    while (ok) {
        const size_t count = fread(buffer, 1, sizeof(buffer), file);
        if (count > 0) {
            total += count;
            ok = psa_hash_update(&operation, buffer, count) == PSA_SUCCESS;
        }
        if (count < sizeof(buffer)) {
            ok = ok && feof(file);
            break;
        }
    }
    if (ok) ok = psa_hash_finish(&operation, digest, sizeof(digest), &digest_len) == PSA_SUCCESS;
    else (void)psa_hash_abort(&operation);
    fclose(file);
    if (!ok || digest_len != sizeof(digest)) return false;
    for (size_t i = 0; i < sizeof(digest); ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    if (bytes != NULL) *bytes = total;
    return true;
}

static bool commit_valid(const provider_state_t *provider, int slot,
                         catalog_commit_t *commit)
{
    FILE *meta = fopen(provider->commit[slot], "rb");
    if (meta == NULL || fread(commit, 1, sizeof(*commit), meta) != sizeof(*commit)) {
        if (meta != NULL) fclose(meta);
        return false;
    }
    fclose(meta);
    if (commit->magic != CATALOG_COMMIT_MAGIC || commit->version != CATALOG_COMMIT_VERSION ||
        commit->provider != provider->tag || commit->generation == 0 ||
        commit->bytes < 16 || commit->bytes > ARTIFACT_MAX_BYTES ||
        commit->records == 0 || !valid_sha(commit->sha)) return false;
    struct stat info;
    if (stat(provider->slot[slot], &info) != 0 || (size_t)info.st_size != commit->bytes) return false;
    char sha[65];
    size_t bytes = 0;
    if (!sha256_file(provider->slot[slot], sha, &bytes) || bytes != commit->bytes ||
        strcmp(sha, commit->sha) != 0) return false;
    FILE *artifact = fopen(provider->slot[slot], "rb");
    unsigned char header[16];
    const bool header_ok = artifact != NULL && fread(header, 1, sizeof(header), artifact) == sizeof(header);
    if (artifact != NULL) fclose(artifact);
    if (!header_ok || memcmp(header, "BSC2", 4) != 0 || header[4] != 2 ||
        header[6] != provider->tag ||
        ((uint32_t)header[8] | (uint32_t)header[9] << 8 | (uint32_t)header[10] << 16 |
         (uint32_t)header[11] << 24) != commit->records) return false;
    return 16ULL + (uint64_t)commit->records * 36ULL +
           ((uint32_t)header[12] | (uint32_t)header[13] << 8 |
            (uint32_t)header[14] << 16 | (uint32_t)header[15] << 24) == commit->bytes;
}

static void load_generations(void)
{
    for (size_t i = 0; i < 2; ++i) {
        provider_state_t *provider = &s_providers[i];
        catalog_commit_t best = {0};
        for (int slot = 0; slot < 2; ++slot) {
            catalog_commit_t candidate = {0};
            if (commit_valid(provider, slot, &candidate) &&
                (provider->active_slot < 0 || candidate.generation > best.generation)) {
                best = candidate;
                provider->active_slot = slot;
            }
        }
        if (provider->active_slot >= 0) {
            memcpy(provider->revision, best.sha, sizeof(provider->revision));
            provider->generation = best.generation;
            ESP_LOGI(TAG, "%s active generation=%lu slot=%d records=%lu bytes=%lu",
                     provider->name, (unsigned long)best.generation, provider->active_slot,
                     (unsigned long)best.records, (unsigned long)best.bytes);
        } else {
            ESP_LOGI(TAG, "%s has no valid committed generation", provider->name);
            atomic_store(&s_cleanup_requested, true);
        }
    }
}

static void cleanup_invalid_files(void)
{
    for (size_t i = 0; i < 2; ++i) {
        provider_state_t *provider = &s_providers[i];
        if (provider->active_slot >= 0) continue;
        (void)remove(provider->candidate);
        vTaskDelay(pdMS_TO_TICKS(10));
        for (int slot = 0; slot < 2; ++slot) {
            (void)remove(provider->slot[slot]);
            (void)remove(provider->commit[slot]);
            char incoming[80];
            if (snprintf(incoming, sizeof(incoming), "%s.incoming", provider->slot[slot]) <
                (int)sizeof(incoming)) (void)remove(incoming);
            if (snprintf(incoming, sizeof(incoming), "%s.incoming", provider->commit[slot]) <
                (int)sizeof(incoming)) (void)remove(incoming);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        provider->active_slot = -1;
        provider->generation = 0;
    }
    atomic_store(&s_cleanup_requested, false);
}

static bool publish_generation(provider_state_t *provider,
                               const crystal_http_response_t *response,
                               const provider_target_t *target)
{
#if defined(CRYSTAL_BUS_CATALOG_TEST_FAULT)
    if (strcmp(CRYSTAL_BUS_CATALOG_TEST_FAULT, "no_space") == 0) {
        ESP_LOGW(TAG, "%s TEST_FAULT=no_space; retaining active generation", provider->name);
        return false;
    }
#endif
    size_t capacity = 0, used = 0;
    if (esp_spiffs_info("storage", &capacity, &used) != ESP_OK || used > capacity ||
        capacity - used < response->body_len + sizeof(catalog_commit_t)) return false;
    const int slot = provider->active_slot == 0 ? 1 : 0;
    char incoming[64], commit_incoming[80];
    if (snprintf(incoming, sizeof(incoming), "%s.incoming", provider->slot[slot]) >= (int)sizeof(incoming) ||
        snprintf(commit_incoming, sizeof(commit_incoming), "%s.incoming", provider->commit[slot]) >= (int)sizeof(commit_incoming)) return false;
    FILE *file = fopen(incoming, "wb");
    if (file == NULL) return false;
    size_t written = 0;
    while (written < response->body_len && available()) {
        const size_t chunk = response->body_len - written < 4096 ? response->body_len - written : 4096;
        if (fwrite(response->body + written, 1, chunk, file) != chunk) break;
        written += chunk;
        taskYIELD();
    }
    // SPIFFS VFS may not implement POSIX fsync; close/rename is its durable
    // publication boundary, while fflush makes the stdio buffer explicit.
    const bool flushed = fflush(file) == 0;
    (void)fsync(fileno(file));
    const bool closed = fclose(file) == 0;
    if (written != response->body_len || !flushed || !closed || !available()) {
        ESP_LOGW(TAG, "%s publication write incomplete written=%u expected=%u flushed=%d available=%d",
                 provider->name, (unsigned)written, (unsigned)response->body_len,
                 flushed, available());
        remove(incoming);
        return false;
    }
    // The selected slot is inactive or invalid. Removing it cannot remove the
    // active generation, and permits SPIFFS VFS rename on existing paths.
    (void)remove(provider->slot[slot]);
    if (rename(incoming, provider->slot[slot]) != 0) {
        ESP_LOGW(TAG, "%s publication artifact rename failed slot=%d", provider->name, slot);
        remove(incoming);
        return false;
    }
#if defined(CRYSTAL_BUS_CATALOG_TEST_FAULT)
    if (strcmp(CRYSTAL_BUS_CATALOG_TEST_FAULT, "interrupt") == 0) {
        ESP_LOGW(TAG, "%s TEST_FAULT=interrupt; artifact left without commit", provider->name);
        return false;
    }
#endif
    catalog_commit_t commit = {.magic = CATALOG_COMMIT_MAGIC, .version = CATALOG_COMMIT_VERSION,
        .provider = provider->tag, .generation = provider->generation + 1,
        .bytes = (uint32_t)response->body_len, .records = target->records};
    memcpy(commit.sha, target->sha, sizeof(commit.sha));
    FILE *meta = fopen(commit_incoming, "wb");
    bool meta_ok = meta != NULL;
    if (meta_ok) meta_ok = fwrite(&commit, 1, sizeof(commit), meta) == sizeof(commit);
    if (meta_ok) {
        meta_ok = fflush(meta) == 0;
        (void)fsync(fileno(meta));
    }
    if (meta != NULL && fclose(meta) != 0) meta_ok = false;
    if (!meta_ok) {
        ESP_LOGW(TAG, "%s publication commit write failed slot=%d", provider->name, slot);
        remove(commit_incoming);
        return false;
    }
    (void)remove(provider->commit[slot]);
    if (rename(commit_incoming, provider->commit[slot]) != 0) {
        ESP_LOGW(TAG, "%s publication commit rename failed slot=%d", provider->name, slot);
        remove(commit_incoming);
        return false;
    }
    provider->active_slot = slot;
    provider->generation = commit.generation;
    memcpy(provider->revision, commit.sha, sizeof(provider->revision));
    ESP_LOGI(TAG, "%s published generation=%lu slot=%d records=%lu bytes=%lu",
             provider->name, (unsigned long)commit.generation, slot,
             (unsigned long)commit.records, (unsigned long)commit.bytes);
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
                              publish_generation(provider, &response, &targets[i]);
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
        if (atomic_exchange(&s_cleanup_requested, false)) {
            cleanup_invalid_files();
            continue;
        }
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
    load_generations();
#if defined(CRYSTAL_BUS_CATALOG_TEST_FAULT)
    if (strcmp(CRYSTAL_BUS_CATALOG_TEST_FAULT, "corrupt") == 0) {
        // Corrupt one provider only so the test also proves provider isolation.
        provider_state_t *provider = &s_providers[0];
        if (provider->active_slot >= 0) {
            FILE *file = fopen(provider->slot[provider->active_slot], "r+b");
            if (file != NULL) {
                unsigned char byte = 0;
                if (fread(&byte, 1, 1, file) == 1) {
                    byte ^= 0x01;
                    rewind(file);
                    (void)fwrite(&byte, 1, 1, file);
                    (void)fflush(file);
                    (void)fsync(fileno(file));
                    ESP_LOGW(TAG, "%s TEST_FAULT=corrupt; active slot damaged for next boot", provider->name);
                }
                fclose(file);
            }
        }
    }
#endif
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
