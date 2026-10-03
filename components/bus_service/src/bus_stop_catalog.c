#include "bus_stop_catalog.h"
#include "bus_provider_kmb.h"
#include "bus_provider_ctb_stop.h"
#include "crystal_http.h"
#include "crystal_network.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <sys/stat.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "bus_stop_catalog";

#define STOP_CATALOG_CAPACITY BUS_STOP_DISCOVERY_CAPACITY
#define STOP_CATALOG_BATCH 4
#define STOP_CATALOG_TIMEOUT_MS 8000
#define STOP_CATALOG_ATTEMPTS 3
#define STOP_CATALOG_MAX_BODY_BYTES (64u * 1024u)
#define STOP_CATALOG_HANDOFF_WAIT_MS \
    (STOP_CATALOG_TIMEOUT_MS * STOP_CATALOG_ATTEMPTS + 5000u)
#define STOP_CATALOG_RETRY_DELAY_US (60LL * 1000LL * 1000LL)
#define STOP_CATALOG_FAILURE_DELAY_US (5LL * 60LL * 1000LL * 1000LL)
#define STOP_CATALOG_OWNER_ID 0x53544D44u // "STMD"
#define STOP_CATALOG_PATH "/spiffs/bus_stop_catalog.bin"
#define STOP_CATALOG_TEMP_PATH "/spiffs/bus_stop_catalog.tmp"
#define STOP_CATALOG_MAGIC 0x42534D31u // "BSM1"
#define STOP_CATALOG_VERSION 1u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t record_count;
    uint16_t completed_count;
    uint16_t failure_count;
    uint8_t provider_mask;
    uint8_t reserved[3];
    uint32_t saved_at;
} stop_catalog_header_t;

typedef struct {
    bus_stop_key_t key;
    uint64_t retry_after_us;
    uint8_t attempts;
} stop_catalog_failure_t;

typedef struct {
    bus_stop_key_t key;
    uint32_t bus_request_id;
    uint32_t framework_request_id;
    SemaphoreHandle_t completed_signal;
    bool timed_out;
    esp_err_t transport_error;
    int status_code;
    uint8_t attempts;
    uint8_t *body;
    size_t body_len;
} stop_catalog_handoff_t;

static bus_stop_metadata_t *s_records;
static uint16_t s_record_count;
static stop_catalog_failure_t *s_failures;
static uint16_t s_failure_count;
static uint32_t s_saved_at;
static bool s_diagnostic_done;

/* Published BSC2 catalogs are provider-wide, sorted binary indexes.  Keep
 * their compact representation separate from the legacy per-stop cache above
 * so an upgrade can read either format during migration. */
typedef struct __attribute__((packed)) {
    char magic[4];
    uint16_t version;
    uint8_t provider;
    uint8_t reserved;
    uint32_t record_count;
    uint32_t pool_size;
} bsc2_header_t;

typedef struct __attribute__((packed)) {
    uint32_t id_offset;
    uint32_t id_length;
    uint32_t name_en_offset;
    uint32_t name_en_length;
    uint32_t name_tc_offset;
    uint32_t name_tc_length;
    float lat;
    float lon;
    uint8_t has_coords;
    uint8_t reserved[3];
} bsc2_entry_t;

typedef struct {
    bool loaded;
    uint32_t record_count;
    uint32_t pool_size;
    bsc2_entry_t *index;
    uint8_t *pool;
} bsc2_catalog_t;

static bsc2_catalog_t s_bsc2[2];
static SemaphoreHandle_t s_bsc2_lock;
static bool s_bsc2_attempted;

static void free_bsc2_catalog(bsc2_catalog_t *catalog)
{
    if (catalog == NULL) return;
    heap_caps_free(catalog->index);
    heap_caps_free(catalog->pool);
    memset(catalog, 0, sizeof(*catalog));
}

static bool read_bsc2_file(const char *path, uint8_t provider,
                           bsc2_catalog_t *catalog)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    bsc2_header_t header = {0};
    bool ok = fread(&header, sizeof(header), 1, file) == 1 &&
              memcmp(header.magic, "BSC2", 4) == 0 && header.version == 2 &&
              header.provider == provider && header.record_count > 0 &&
              header.record_count <= 20000 && header.pool_size <= 1024u * 1024u;
    const uint64_t index_bytes = (uint64_t)header.record_count * sizeof(bsc2_entry_t);
    if (ok) {
        struct stat info = {0};
        ok = fstat(fileno(file), &info) == 0 &&
             (uint64_t)info.st_size == sizeof(header) + index_bytes + header.pool_size;
    }
    if (ok) {
        catalog->index = heap_caps_malloc((size_t)index_bytes,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        catalog->pool = heap_caps_malloc(header.pool_size,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ok = catalog->index != NULL && catalog->pool != NULL &&
             fread(catalog->index, sizeof(bsc2_entry_t), header.record_count, file) ==
                 header.record_count &&
             fread(catalog->pool, 1, header.pool_size, file) == header.pool_size;
    }
    fclose(file);
    if (!ok) {
        free_bsc2_catalog(catalog);
        return false;
    }
    catalog->record_count = header.record_count;
    catalog->pool_size = header.pool_size;
    catalog->loaded = true;
    return true;
}

static void load_bsc2_catalogs_locked(void)
{
    if (s_bsc2_attempted) return;
    s_bsc2_attempted = true;
    const char *paths[2][2] = {
        {"/spiffs/kmb-stops.a", "/spiffs/kmb-stops.b"},
        {"/spiffs/ctb-stops.a", "/spiffs/ctb-stops.b"},
    };
    for (uint8_t provider = 1; provider <= 2; provider++) {
        for (size_t slot = 0; slot < 2; slot++) {
            bsc2_catalog_t candidate = {0};
            if (read_bsc2_file(paths[provider - 1][slot], provider, &candidate)) {
                free_bsc2_catalog(&s_bsc2[provider - 1]);
                s_bsc2[provider - 1] = candidate;
                break;
            }
        }
        if (s_bsc2[provider - 1].loaded) {
            ESP_LOGI(TAG, "loaded BSC2 catalog provider=%u records=%lu",
                     (unsigned)provider,
                     (unsigned long)s_bsc2[provider - 1].record_count);
        }
    }
}

esp_err_t bus_stop_catalog_init(void)
{
    if (s_bsc2_lock == NULL) s_bsc2_lock = xSemaphoreCreateMutex();
    if (s_bsc2_lock == NULL) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_bsc2_lock, portMAX_DELAY);
    load_bsc2_catalogs_locked();
    const bool loaded = s_bsc2[0].loaded || s_bsc2[1].loaded;
    xSemaphoreGive(s_bsc2_lock);
    return loaded ? ESP_OK : ESP_ERR_NOT_FOUND;
}

void bus_catalog_get_info(bus_catalog_info_t *out)
{
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    if (s_bsc2_lock == NULL) {
        out->status = BUS_CATALOG_NONE;
        return;
    }
    xSemaphoreTake(s_bsc2_lock, portMAX_DELAY);
    load_bsc2_catalogs_locked();
    out->kmb_record_count = s_bsc2[0].loaded ? s_bsc2[0].record_count : 0;
    out->ctb_record_count = s_bsc2[1].loaded ? s_bsc2[1].record_count : 0;
    if (out->kmb_record_count > 0 || out->ctb_record_count > 0 ||
        s_record_count > 0) {
        out->status = BUS_CATALOG_READY;
    } else {
        out->status = BUS_CATALOG_NONE;
    }
    xSemaphoreGive(s_bsc2_lock);
}

static bool bsc2_lookup(bus_operator_t op, const char *stop_id,
                        bus_stop_metadata_t *out)
{
    const uint8_t provider = op == BUS_OP_KMB ? 1 : 2;
    if (bus_stop_catalog_init() != ESP_OK) return false;
    /* A cold boot may initialize before the background sync publishes its
     * first generation. Retry the provider once when its file appears later. */
    if (!s_bsc2[provider - 1].loaded) {
        xSemaphoreTake(s_bsc2_lock, portMAX_DELAY);
        s_bsc2_attempted = false;
        load_bsc2_catalogs_locked();
        xSemaphoreGive(s_bsc2_lock);
    }
    xSemaphoreTake(s_bsc2_lock, portMAX_DELAY);
    bsc2_catalog_t *catalog = &s_bsc2[provider - 1];
    uint32_t lo = 0, hi = catalog->loaded ? catalog->record_count : 0;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        const bsc2_entry_t *entry = &catalog->index[mid];
        if (entry->id_offset > catalog->pool_size ||
            entry->id_length > catalog->pool_size - entry->id_offset ||
            entry->name_en_offset > catalog->pool_size ||
            entry->name_en_length > catalog->pool_size - entry->name_en_offset ||
            entry->name_tc_offset > catalog->pool_size ||
            entry->name_tc_length > catalog->pool_size - entry->name_tc_offset) {
            break;
        }
        const size_t id_len = entry->id_length < BUS_STOP_ID_MAX ? entry->id_length : BUS_STOP_ID_MAX - 1;
        const int cmp = strncmp(stop_id, (const char *)catalog->pool + entry->id_offset, id_len);
        if (cmp == 0 && strlen(stop_id) == id_len) {
            memset(out, 0, sizeof(*out));
            out->op = op;
            memcpy(out->stop_id, catalog->pool + entry->id_offset, id_len);
            const size_t en_len = entry->name_en_length < BUS_STOP_NAME_MAX ? entry->name_en_length : BUS_STOP_NAME_MAX - 1;
            const size_t tc_len = entry->name_tc_length < BUS_STOP_NAME_MAX ? entry->name_tc_length : BUS_STOP_NAME_MAX - 1;
            if (entry->name_en_offset + en_len <= catalog->pool_size) memcpy(out->name_en, catalog->pool + entry->name_en_offset, en_len);
            if (entry->name_tc_offset + tc_len <= catalog->pool_size) memcpy(out->name_tc, catalog->pool + entry->name_tc_offset, tc_len);
            out->lat = entry->lat; out->lon = entry->lon; out->has_coordinates = entry->has_coords != 0; out->resolved = true;
            xSemaphoreGive(s_bsc2_lock);
            return true;
        }
        if (cmp < 0) hi = mid; else lo = mid + 1;
    }
    xSemaphoreGive(s_bsc2_lock);
    return false;
}

extern void bus_stop_catalog_report_progress(uint16_t discovered,
                                             uint16_t resolved,
                                             uint16_t pending,
                                             uint16_t failed,
                                             const char *message);
extern void bus_stop_catalog_schedule(void);
void bus_stop_catalog_run_diagnostic(void);

static bool ensure_state(void)
{
    if (s_records == NULL) {
        s_records = heap_caps_calloc(STOP_CATALOG_CAPACITY,
                                     sizeof(*s_records),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (s_failures == NULL) {
        s_failures = heap_caps_calloc(STOP_CATALOG_CAPACITY,
                                      sizeof(*s_failures),
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    return s_records != NULL && s_failures != NULL;
}

static int record_index(const bus_stop_key_t *key)
{
    for (uint16_t i = 0; i < s_record_count; i++) {
        bus_stop_key_t record_key = {.op = s_records[i].op};
        memcpy(record_key.stop_id, s_records[i].stop_id,
               sizeof(record_key.stop_id));
        if (bus_stop_key_equal(key, &record_key)) {
            return (int)i;
        }
    }
    return -1;
}

static int failure_index(const bus_stop_key_t *key)
{
    for (uint16_t i = 0; i < s_failure_count; i++) {
        if (bus_stop_key_equal(key, &s_failures[i].key)) {
            return (int)i;
        }
    }
    return -1;
}

static void clear_failure(const bus_stop_key_t *key)
{
    const int index = failure_index(key);
    if (index < 0) {
        return;
    }
    s_failures[index] = s_failures[--s_failure_count];
}

static void record_failure(const bus_stop_key_t *key, uint8_t attempts,
                           bool retryable)
{
    int index = failure_index(key);
    if (index < 0) {
        if (s_failure_count >= STOP_CATALOG_CAPACITY) {
            return;
        }
        index = s_failure_count++;
        memset(&s_failures[index], 0, sizeof(s_failures[index]));
        s_failures[index].key = *key;
    }
    s_failures[index].attempts = attempts;
    s_failures[index].retry_after_us = (uint64_t)esp_timer_get_time() +
        (retryable ? STOP_CATALOG_RETRY_DELAY_US : STOP_CATALOG_FAILURE_DELAY_US);
}

static bool key_valid(const bus_stop_key_t *key)
{
    return key != NULL && (key->op == BUS_OP_KMB || key->op == BUS_OP_CTB) &&
           memchr(key->stop_id, '\0', sizeof(key->stop_id)) != NULL &&
           key->stop_id[0] != '\0';
}

static bool metadata_valid(const bus_stop_metadata_t *record)
{
    bus_stop_key_t key = {0};
    if (record == NULL) {
        return false;
    }
    key.op = record->op;
    memcpy(key.stop_id, record->stop_id, sizeof(key.stop_id));
    if (!key_valid(&key) ||
        memchr(record->name_en, '\0', sizeof(record->name_en)) == NULL ||
        memchr(record->name_tc, '\0', sizeof(record->name_tc)) == NULL) {
        return false;
    }
    if (record->has_coordinates &&
        (!isfinite(record->lat) || !isfinite(record->lon) ||
         record->lat < -90.0f || record->lat > 90.0f ||
         record->lon < -180.0f || record->lon > 180.0f)) {
        return false;
    }
    return true;
}

static uint8_t provider_mask(void)
{
    uint8_t mask = 0;
    for (uint16_t i = 0; i < s_record_count; i++) {
        if (s_records[i].op == BUS_OP_KMB) mask |= 0x01;
        if (s_records[i].op == BUS_OP_CTB) mask |= 0x02;
    }
    for (uint16_t i = 0; i < s_failure_count; i++) {
        if (s_failures[i].key.op == BUS_OP_KMB) mask |= 0x01;
        if (s_failures[i].key.op == BUS_OP_CTB) mask |= 0x02;
    }
    return mask;
}

static bool save_catalog(void)
{
    if (!ensure_state() || s_record_count > STOP_CATALOG_CAPACITY ||
        s_failure_count > STOP_CATALOG_CAPACITY) {
        return false;
    }
    FILE *file = fopen(STOP_CATALOG_TEMP_PATH, "wb");
    if (file == NULL) {
        ESP_LOGW(TAG, "Stop catalog: cannot open temporary file errno=%d", errno);
        return false;
    }
    stop_catalog_header_t header = {
        .magic = STOP_CATALOG_MAGIC,
        .version = STOP_CATALOG_VERSION,
        .record_count = s_record_count,
        .completed_count = s_record_count,
        .failure_count = s_failure_count,
        .provider_mask = provider_mask(),
        .saved_at = (uint32_t)time(NULL),
    };
    bool ok = fwrite(&header, sizeof(header), 1, file) == 1;
    for (uint16_t i = 0; ok && i < s_record_count; i++) {
        ok = metadata_valid(&s_records[i]) &&
             fwrite(&s_records[i], sizeof(s_records[i]), 1, file) == 1;
    }
    for (uint16_t i = 0; ok && i < s_failure_count; i++) {
        ok = key_valid(&s_failures[i].key) &&
             fwrite(&s_failures[i], sizeof(s_failures[i]), 1, file) == 1;
    }
    if (fclose(file) != 0) ok = false;
    if (ok && rename(STOP_CATALOG_TEMP_PATH, STOP_CATALOG_PATH) != 0) {
        if (remove(STOP_CATALOG_PATH) != 0 ||
            rename(STOP_CATALOG_TEMP_PATH, STOP_CATALOG_PATH) != 0) {
            ok = false;
        }
    }
    if (!ok) {
        remove(STOP_CATALOG_TEMP_PATH);
        ESP_LOGW(TAG, "Stop catalog: atomic save failed");
    } else {
        s_saved_at = header.saved_at;
        ESP_LOGI(TAG, "Stop catalog: saved records=%u pending=%u mask=0x%02x",
                 (unsigned)s_record_count, (unsigned)s_failure_count,
                 (unsigned)header.provider_mask);
    }
    return ok;
}

bool bus_stop_catalog_load(void)
{
    FILE *file = fopen(STOP_CATALOG_PATH, "rb");
    if (file == NULL) return false;
    struct stat info;
    stop_catalog_header_t header = {0};
    bool ok = fstat(fileno(file), &info) == 0 &&
              fread(&header, sizeof(header), 1, file) == 1 &&
              header.magic == STOP_CATALOG_MAGIC &&
              header.version == STOP_CATALOG_VERSION &&
              header.record_count <= STOP_CATALOG_CAPACITY &&
              header.failure_count <= STOP_CATALOG_CAPACITY &&
              header.completed_count == header.record_count &&
              (header.provider_mask & ~0x03u) == 0 &&
              (uint64_t)info.st_size == sizeof(header) +
                  (uint64_t)header.record_count * sizeof(bus_stop_metadata_t) +
                  (uint64_t)header.failure_count * sizeof(stop_catalog_failure_t);
    const time_t now = time(NULL);
    if (ok && header.saved_at != 0 && now > (time_t)header.saved_at &&
        (uint32_t)(now - (time_t)header.saved_at) > (7u * 24u * 60u * 60u)) {
        ok = false;
        ESP_LOGW(TAG, "Stop catalog: cache is stale");
    }
    if (ok && !ensure_state()) ok = false;
    if (ok) {
        s_record_count = 0;
        s_failure_count = 0;
        for (uint16_t i = 0; i < header.record_count && ok; i++) {
            bus_stop_metadata_t record;
            ok = fread(&record, sizeof(record), 1, file) == 1 &&
                 metadata_valid(&record) && record_index((bus_stop_key_t *)&record) < 0;
            if (ok) s_records[s_record_count++] = record;
        }
        for (uint16_t i = 0; i < header.failure_count && ok; i++) {
            stop_catalog_failure_t failure;
            ok = fread(&failure, sizeof(failure), 1, file) == 1 &&
                 key_valid(&failure.key) && failure_index(&failure.key) < 0 &&
                 record_index(&failure.key) < 0;
            if (ok) {
                failure.retry_after_us = 0;
                s_failures[s_failure_count++] = failure;
            }
        }
    }
    fclose(file);
    if (!ok) {
        s_record_count = 0;
        s_failure_count = 0;
        ESP_LOGW(TAG, "Stop catalog: rejected invalid or corrupt cache");
        return false;
    }
    ESP_LOGI(TAG, "Stop catalog: loaded records=%u pending=%u mask=0x%02x",
             (unsigned)s_record_count, (unsigned)s_failure_count,
             (unsigned)header.provider_mask);
    s_saved_at = header.saved_at;
    return true;
}

static bool failure_deferred(const bus_stop_key_t *key)
{
    const int index = failure_index(key);
    return index >= 0 && (uint64_t)esp_timer_get_time() <
        s_failures[index].retry_after_us;
}

static stop_catalog_handoff_t *handoff_create(const bus_stop_key_t *key,
                                              uint32_t bus_request_id)
{
    stop_catalog_handoff_t *context = calloc(1, sizeof(*context));
    if (context == NULL) {
        return NULL;
    }
    context->key = *key;
    context->bus_request_id = bus_request_id;
    context->completed_signal = xSemaphoreCreateBinary();
    if (context->completed_signal == NULL) {
        free(context);
        return NULL;
    }
    return context;
}

static void handoff_cleanup(stop_catalog_handoff_t *context)
{
    if (context == NULL) {
        return;
    }
    if (context->completed_signal != NULL) {
        vSemaphoreDelete(context->completed_signal);
    }
    free(context->body);
    free(context);
}

static void handoff_callback(const crystal_http_response_t *response,
                             void *user_data)
{
    stop_catalog_handoff_t *context = user_data;
    if (context == NULL) {
        if (response != NULL) {
            crystal_http_response_release(response);
        }
        return;
    }
    if (response != NULL) {
        context->status_code = response->status_code;
        context->transport_error = response->transport_error;
        context->attempts = response->attempts;
        if (response->body != NULL && response->body_len > 0) {
            context->body = heap_caps_malloc(response->body_len + 1,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (context->body != NULL) {
                memcpy(context->body, response->body, response->body_len);
                context->body[response->body_len] = '\0';
                context->body_len = response->body_len;
            } else {
                context->transport_error = ESP_ERR_NO_MEM;
            }
        }
        crystal_http_response_release(response);
    } else {
        context->transport_error = ESP_FAIL;
    }
    xSemaphoreGive(context->completed_signal);
    if (context->timed_out) {
        handoff_cleanup(context);
    }
}

static stop_catalog_handoff_t *fetch_one(const bus_stop_key_t *key,
                                         uint32_t bus_request_id)
{
    stop_catalog_handoff_t *context = handoff_create(key, bus_request_id);
    if (context == NULL) {
        return NULL;
    }
    char url[256];
    if (key->op == BUS_OP_KMB) {
        snprintf(url, sizeof(url),
                 "https://data.etabus.gov.hk/v1/transport/kmb/stop/%s",
                 key->stop_id);
    } else {
        snprintf(url, sizeof(url),
                 "https://rt.data.gov.hk/v2/transport/citybus/stop/%s",
                 key->stop_id);
    }
    crystal_http_options_t options = {
        .url = url,
        .timeout_ms = STOP_CATALOG_TIMEOUT_MS,
        .max_attempts = STOP_CATALOG_ATTEMPTS,
        .retry_backoff_ms = 500,
        .retry_backoff_max_ms = 2000,
        .max_body_bytes = STOP_CATALOG_MAX_BODY_BYTES,
        .keep_alive = false,
        .owner_id = STOP_CATALOG_OWNER_ID,
    };
    context->framework_request_id = crystal_http_get(&options,
                                                     handoff_callback,
                                                     context);
    if (context->framework_request_id == 0) {
        handoff_cleanup(context);
        return NULL;
    }
    if (xSemaphoreTake(context->completed_signal,
                       pdMS_TO_TICKS(STOP_CATALOG_HANDOFF_WAIT_MS)) != pdTRUE) {
        context->timed_out = true;
        (void)crystal_http_cancel(context->framework_request_id);
        return NULL;
    }
    return context;
}

static bool append_record(const bus_stop_metadata_t *record)
{
    bus_stop_key_t key = {.op = record->op};
    memcpy(key.stop_id, record->stop_id, sizeof(key.stop_id));
    const int existing = record_index(&key);
    if (existing >= 0) {
        s_records[existing] = *record;
        return true;
    }
    if (s_record_count >= STOP_CATALOG_CAPACITY) {
        return false;
    }
    s_records[s_record_count++] = *record;
    return true;
}

static void publish_progress(uint16_t discovered, const char *message)
{
    uint16_t pending = discovered >= s_record_count
        ? (uint16_t)(discovered - s_record_count) : 0;
    bus_stop_catalog_report_progress(discovered, s_record_count, pending,
                                     s_failure_count, message);
}

void bus_stop_catalog_sync_pass(uint32_t request_id)
{
    if (!ensure_state()) {
        ESP_LOGE(TAG, "Stop catalog state allocation failed");
        return;
    }
    if (!crystal_network_has_ip()) {
        publish_progress(bus_service_stop_discovery_pending(),
                         "Waiting for network");
        return;
    }
    const uint16_t discovered = bus_service_stop_discovery_pending();
    if (discovered == 0) {
        publish_progress(0, "No discovered stops");
        return;
    }
    bus_stop_key_t *keys = heap_caps_malloc(
        (size_t)discovered * sizeof(*keys), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (keys == NULL) {
        publish_progress(discovered, "Stop catalog memory deferred");
        return;
    }
    const uint16_t copied = bus_service_stop_discovery_copy(keys, discovered);
    uint8_t processed = 0;
    for (uint16_t i = 0; i < copied && processed < STOP_CATALOG_BATCH; i++) {
        const bus_stop_key_t *key = &keys[i];
        if (record_index(key) >= 0 || failure_deferred(key)) {
            continue;
        }
        stop_catalog_handoff_t *handoff = fetch_one(key, request_id);
        processed++;
        if (handoff == NULL) {
            record_failure(key, 0, true);
            continue;
        }
        bus_stop_metadata_t record;
        esp_err_t parse_status = ESP_ERR_INVALID_RESPONSE;
        if (handoff->transport_error == ESP_OK &&
            handoff->status_code >= 200 && handoff->status_code < 300 &&
            handoff->body != NULL && handoff->body_len > 0) {
            parse_status = key->op == BUS_OP_KMB
                ? bus_kmb_parse_stop_metadata(handoff->body, handoff->body_len,
                                              key->stop_id, &record)
                : bus_ctb_parse_stop_metadata(handoff->body, handoff->body_len,
                                              key->stop_id, &record);
        }
        const bool success = parse_status == ESP_OK && append_record(&record);
        ESP_LOGI(TAG,
                 "Stop catalog provider=%u stop=%s status=%d body=%u attempts=%u parse=%s result=%s",
                 (unsigned)key->op, key->stop_id, handoff->status_code,
                 (unsigned)handoff->body_len, (unsigned)handoff->attempts,
                 esp_err_to_name(parse_status), success ? "resolved" : "pending");
        if (success) {
            clear_failure(key);
        } else {
            const bool retryable = handoff->transport_error != ESP_OK ||
                                   handoff->status_code >= 500 ||
                                   handoff->status_code == 0;
            record_failure(key, handoff->attempts, retryable);
        }
        handoff_cleanup(handoff);
        publish_progress(discovered, success ? "Stop metadata resolved" :
                         "Stop metadata pending");
        if (!crystal_network_has_ip()) {
            break;
        }
    }
    free(keys);
    (void)save_catalog();
    bus_stop_catalog_run_diagnostic();
    (void)processed;
}

void bus_stop_catalog_retry_now(void)
{
    for (uint16_t i = 0; i < s_failure_count; i++) {
        s_failures[i].retry_after_us = 0;
    }
}

uint16_t bus_service_stop_catalog_resolved(void)
{
    return s_record_count;
}

uint16_t bus_service_stop_catalog_failed(void)
{
    return s_failure_count;
}

void bus_stop_catalog_run_diagnostic(void)
{
    if (s_diagnostic_done || s_record_count == 0) {
        return;
    }
    bus_stop_catalog_progress_t progress = {0};
    (void)bus_service_stop_catalog_get_progress(&progress);
    ESP_LOGI(TAG, "S4.4 diagnostic begin ready=%d fresh=%d discovered=%u resolved=%u pending=%u failed=%u",
             bus_service_stop_catalog_ready(), bus_service_stop_catalog_fresh(),
             (unsigned)progress.discovered, (unsigned)progress.resolved,
             (unsigned)progress.pending, (unsigned)progress.failed);

    bus_stop_metadata_t metadata = {0};
    bool kmb_checked = false;
    bool ctb_checked = false;
    for (uint16_t i = 0; i < s_record_count; i++) {
        if (s_records[i].op == BUS_OP_KMB && !kmb_checked) {
            const esp_err_t status = bus_service_lookup_stop_metadata(
                BUS_OP_KMB, s_records[i].stop_id, &metadata);
            ESP_LOGI(TAG, "S4.4 KMB lookup stop=%s status=%s returned_op=%u",
                     s_records[i].stop_id, esp_err_to_name(status),
                     (unsigned)metadata.op);
            kmb_checked = true;
        }
        if (s_records[i].op == BUS_OP_CTB && !ctb_checked) {
            const esp_err_t status = bus_service_lookup_stop_metadata(
                BUS_OP_CTB, s_records[i].stop_id, &metadata);
            ESP_LOGI(TAG, "S4.4 CTB lookup stop=%s status=%s returned_op=%u",
                     s_records[i].stop_id, esp_err_to_name(status),
                     (unsigned)metadata.op);
            bus_stop_t route_stop = {0};
            route_stop.op = BUS_OP_CTB;
            strlcpy(route_stop.stop_id, s_records[i].stop_id,
                    sizeof(route_stop.stop_id));
            const esp_err_t route_status = bus_service_lookup_route_stop(
                &route_stop, &metadata);
            ESP_LOGI(TAG, "S4.4 route-stop lookup stop=%s status=%s returned_op=%u",
                     route_stop.stop_id, esp_err_to_name(route_status),
                     (unsigned)metadata.op);
            const esp_err_t cross_status = bus_service_lookup_stop_metadata(
                BUS_OP_KMB, s_records[i].stop_id, &metadata);
            ESP_LOGI(TAG, "S4.4 provider isolation text_id=%s KMB_status=%s returned_op=%u",
                     s_records[i].stop_id, esp_err_to_name(cross_status),
                     (unsigned)metadata.op);
            ctb_checked = true;
        }
        if (kmb_checked && ctb_checked) {
            break;
        }
    }
    const esp_err_t missing_status = bus_service_lookup_stop_metadata(
        BUS_OP_CTB, "__s4_4_missing__", &metadata);
    ESP_LOGI(TAG, "S4.4 unresolved lookup status=%s",
             esp_err_to_name(missing_status));
    const esp_err_t invalid_status = bus_service_lookup_stop_metadata(
        (bus_operator_t)99, "", &metadata);
    ESP_LOGI(TAG, "S4.4 invalid lookup status=%s",
             esp_err_to_name(invalid_status));
    ESP_LOGI(TAG, "S4.4 diagnostic end kmb_checked=%d ctb_checked=%d",
             kmb_checked, ctb_checked);
    s_diagnostic_done = kmb_checked && ctb_checked;
}

esp_err_t bus_stop_catalog_lookup(bus_operator_t op, const char *stop_id,
                                  bus_stop_metadata_t *out)
{
    if (out == NULL || stop_id == NULL || stop_id[0] == '\0' ||
        (op != BUS_OP_KMB && op != BUS_OP_CTB) ||
        strnlen(stop_id, BUS_STOP_ID_MAX) >= BUS_STOP_ID_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    bus_stop_key_t key = {.op = op};
    strlcpy(key.stop_id, stop_id, sizeof(key.stop_id));
    const int index = record_index(&key);
    if (index < 0) {
        if (bsc2_lookup(op, stop_id, out)) return ESP_OK;
        return ESP_ERR_NOT_FOUND;
    }
    *out = s_records[index];
    const time_t now = time(NULL);
    if (s_saved_at == 0 || now <= 0 ||
        now < (time_t)s_saved_at ||
        (uint32_t)(now - (time_t)s_saved_at) > (7u * 24u * 60u * 60u)) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

bool bus_stop_catalog_ready(void)
{
    return s_record_count > 0;
}

bool bus_stop_catalog_fresh(void)
{
    const time_t now = time(NULL);
    return s_saved_at != 0 && now > 0 && now >= (time_t)s_saved_at &&
           (uint32_t)(now - (time_t)s_saved_at) <= (7u * 24u * 60u * 60u);
}
