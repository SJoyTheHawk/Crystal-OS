#include "crystal_update.h"
#include "crystal_update_manifest.h"
#include "crystal_version.h"

#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "psa/crypto.h"

static const char *TAG = "update";
#define OTA_WORKER_STACK_SIZE 16384
#define OTA_WORKER_PRIORITY 2
#define OTA_WORKER_CORE 0
#define MANIFEST_MAX_SIZE 2048
#define MANIFEST_PRIMARY "https://rockyroad.studio/apps/crystal-os/stable/manifest.json"
#define MANIFEST_MIRROR "https://updates.bubstal.com/crystal-os/stable/manifest.json"
#define EXPECTED_TARGET "waveshare-esp32-s3-touch-lcd-4b"
#define EXPECTED_CHANNEL "stable"

static crystal_update_snapshot_t s_snapshot;
static TaskHandle_t s_ota_worker_task;
static volatile bool s_cancel_requested;
static volatile bool s_check_requested;
static volatile bool s_download_requested;
static bool s_rollback_occurred;
static char s_rollback_attempted_version[32];
// These buffers are owned by the single OTA worker. Keep them out of its task
// stack because the manifest remains live while install_image() uses the I/O
// buffer and the HTTPS/OTA call frames are active.
static char s_manifest_json[MANIFEST_MAX_SIZE];
static uint8_t s_ota_io_buffer[4096];
extern bool crystal_ui_post_ota_changed(void);

// esp_http_client's synchronous open/read APIs can keep the OTA task on one
// CPU while mbedTLS performs an ECDH operation.  In that interval the idle
// task cannot feed the task watchdog.  Keep each async operation bounded and
// yield between retries so the idle task gets regular scheduling time.
static bool ota_http_wait(int64_t deadline_us)
{
    if (s_cancel_requested || esp_timer_get_time() >= deadline_us) return false;
    vTaskDelay(pdMS_TO_TICKS(1));
    return !s_cancel_requested && esp_timer_get_time() < deadline_us;
}

static esp_err_t ota_http_open_async(esp_http_client_handle_t client, int timeout_ms)
{
    const int64_t deadline_us = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    for (;;) {
        const esp_err_t err = esp_http_client_open(client, 0);
        if (err == ESP_OK) return ESP_OK;
        if (err != ESP_ERR_HTTP_EAGAIN && err != ESP_ERR_HTTP_CONNECTING) return err;
        if (!ota_http_wait(deadline_us)) {
            return s_cancel_requested ? ESP_ERR_INVALID_STATE : ESP_ERR_TIMEOUT;
        }
    }
}

static esp_err_t ota_http_fetch_headers_async(esp_http_client_handle_t client,
                                              int timeout_ms, int64_t *content_length)
{
    const int64_t deadline_us = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    for (;;) {
        const int64_t result = esp_http_client_fetch_headers(client);
        if (result >= 0) {
            *content_length = result;
            return ESP_OK;
        }
        if (result != -ESP_ERR_HTTP_EAGAIN) return ESP_FAIL;
        if (!ota_http_wait(deadline_us)) {
            return s_cancel_requested ? ESP_ERR_INVALID_STATE : ESP_ERR_TIMEOUT;
        }
    }
}

static int ota_http_read_async(esp_http_client_handle_t client, char *buffer, int length,
                               int timeout_ms, esp_err_t *error)
{
    const int64_t deadline_us = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    for (;;) {
        const int got = esp_http_client_read(client, buffer, length);
        if (got != -ESP_ERR_HTTP_EAGAIN) return got;
        if (!ota_http_wait(deadline_us)) {
            *error = s_cancel_requested ? ESP_ERR_INVALID_STATE : ESP_ERR_TIMEOUT;
            return -1;
        }
    }
}

static void notify_ota_ui(void)
{
    // A download can produce many chunks at the same integer percentage.
    // Only enqueue visible progress changes; retry on a full UI queue.
    static int last_posted_state = -1;
    static int last_posted_progress = -1;

    // For DOWNLOADING state, throttle by percentage only - update on every % change
    if (s_snapshot.state == UPDATE_STATE_DOWNLOADING &&
            last_posted_state == UPDATE_STATE_DOWNLOADING &&
            last_posted_progress == s_snapshot.progress_percent) {
        return;  // Same percentage, skip
    }

    if (!crystal_ui_post_ota_changed()) {
        ESP_LOGW(TAG, "OTA UI event queue is full or not initialized (state=%d)", s_snapshot.state);
    } else {
        if (s_snapshot.state == UPDATE_STATE_DOWNLOADING &&
            last_posted_progress != s_snapshot.progress_percent) {
            ESP_LOGI(TAG, "Download progress: %u%%", s_snapshot.progress_percent);
        }
        last_posted_state = s_snapshot.state;
        last_posted_progress = s_snapshot.progress_percent;
    }
}
static void set_error(const char *message)
{
    s_snapshot.state = UPDATE_STATE_FAILED;
    strlcpy(s_snapshot.error_message, message, sizeof(s_snapshot.error_message));
    notify_ota_ui();
}

static esp_err_t persist_attempted_version(const char *version)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("ota", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, "attempted_ver", version);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

void crystal_update_clear_attempted_version(void)
{
    nvs_handle_t handle;
    if (nvs_open("ota", NVS_READWRITE, &handle) != ESP_OK) return;
    (void)nvs_erase_key(handle, "attempted_ver");
    (void)nvs_commit(handle);
    nvs_close(handle);
}

static bool check_rollback_at_boot(void)
{
    nvs_handle_t handle;
    if (nvs_open("ota", NVS_READONLY, &handle) != ESP_OK) return false;
    size_t length = sizeof(s_rollback_attempted_version);
    const esp_err_t read_err = nvs_get_str(handle, "attempted_ver",
                                           s_rollback_attempted_version, &length);
    nvs_close(handle);
    if (read_err != ESP_OK || s_rollback_attempted_version[0] == '\0') return false;

    const bool rolled_back = strcmp(crystal_version_string(), s_rollback_attempted_version) != 0;
    if (rolled_back) {
        ESP_LOGW(TAG, "Rollback detected: attempted %s, running %s",
                 s_rollback_attempted_version, crystal_version_string());
        s_rollback_occurred = true;
    } else {
        ESP_LOGI(TAG, "OTA image pending confirmation: running %s",
                 crystal_version_string());
    }

    // A rollback consumes the marker immediately. For a matching version,
    // retain it until the delayed OTA confirmation succeeds; if this image
    // crashes while still pending, the bootloader will roll back and the
    // previous image can report the failed attempt.
    if (rolled_back) crystal_update_clear_attempted_version();
    return rolled_back;
}

static esp_err_t fetch_manifest(const char *url, char *body, size_t *length)
{
    esp_http_client_config_t config = {
        .url = url, .timeout_ms = 15000, .crt_bundle_attach = esp_crt_bundle_attach,
        .is_async = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = ota_http_open_async(client, config.timeout_ms);
    if (err == ESP_OK) {
        int64_t content_length = 0;
        err = ota_http_fetch_headers_async(client, config.timeout_ms, &content_length);
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Manifest response url=%s status=%d content_length=%lld",
                 url, status, (long long)content_length);
        if (err != ESP_OK) {
            // Keep the transport error from the async header poll.
        } else if (status != 200 || content_length >= (int64_t)MANIFEST_MAX_SIZE) {
            err = ESP_FAIL;
        } else {
            size_t used = 0;
            while (used + 1 < MANIFEST_MAX_SIZE) {
                size_t remaining = MANIFEST_MAX_SIZE - used - 1;
                if (content_length > 0) {
                    if (used >= (size_t)content_length) break;
                    const size_t expected = (size_t)content_length - used;
                    if (remaining > expected) remaining = expected;
                }
                esp_err_t read_err = ESP_OK;
                int got = ota_http_read_async(client, body + used, (int)remaining,
                                              config.timeout_ms, &read_err);
                if (got < 0) { err = read_err; break; }
                if (got == 0) break;
                used += (size_t)got;
            }
            if (err == ESP_OK && used > 0 &&
                    (content_length <= 0 || used == (size_t)content_length)) {
                body[used] = '\0'; *length = used;
            } else if (err == ESP_OK) {
                err = ESP_FAIL;
            }
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) ESP_LOGW(TAG, "Manifest fetch failed url=%s error=%s", url, esp_err_to_name(err));
    return err;
}

static esp_err_t install_image(const update_manifest_t *manifest)
{
    ESP_LOGI(TAG, "Downloading OTA image url=%s expected_size=%u",
             manifest->urls[0], (unsigned)manifest->size);
    esp_http_client_config_t config = {
        .url = manifest->urls[0], .timeout_ms = 30000, .crt_bundle_attach = esp_crt_bundle_attach,
        .is_async = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = ota_http_open_async(client, config.timeout_ms);
    esp_ota_handle_t ota = 0;
    const esp_partition_t *partition = NULL;
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    size_t received = 0;
    bool began = false;
    bool hash_ready = false;
    if (err == ESP_OK) {
        int64_t content_length = 0;
        err = ota_http_fetch_headers_async(client, config.timeout_ms, &content_length);
        if (err == ESP_OK && (esp_http_client_get_status_code(client) != 200 ||
                              content_length != manifest->size)) err = ESP_FAIL;
        if (err == ESP_OK) {
            partition = esp_ota_get_next_update_partition(NULL);
            if (partition == NULL) err = ESP_ERR_NOT_FOUND;
            else err = esp_ota_begin(partition, manifest->size, &ota);
            if (err == ESP_OK) {
                began = true;
                if (psa_hash_setup(&hash, PSA_ALG_SHA_256) != PSA_SUCCESS) {
                    err = ESP_FAIL;
                } else {
                    hash_ready = true;
                }
            }
        }
        while (err == ESP_OK && received < manifest->size && !s_cancel_requested) {
            esp_err_t read_err = ESP_OK;
            int got = ota_http_read_async(client, (char *)s_ota_io_buffer,
                                          sizeof(s_ota_io_buffer), config.timeout_ms,
                                          &read_err);
            if (got <= 0) {
                err = read_err != ESP_OK ? read_err : ESP_FAIL;
                break;
            }
            err = esp_ota_write(ota, s_ota_io_buffer, (size_t)got);
            if (err != ESP_OK) break;
            if (psa_hash_update(&hash, s_ota_io_buffer, (size_t)got) != PSA_SUCCESS) { err = ESP_FAIL; break; }
            received += (size_t)got;
            s_snapshot.downloaded_bytes = received;
            s_snapshot.progress_percent = (uint8_t)((received * 100u) / manifest->size);
            notify_ota_ui();
            // Successful reads may return immediately from buffered data.
            // Block too on this path so rendering and Core 0 idle get CPU time.
            vTaskDelay(pdMS_TO_TICKS(10) > 0 ? pdMS_TO_TICKS(10) : 1);
        }
        if (err == ESP_OK && (received != manifest->size || s_cancel_requested)) err = ESP_ERR_INVALID_SIZE;
    }
    uint8_t digest[32] = {};
    size_t digest_len = 0;
    if (hash_ready) {
        const psa_status_t hash_err = psa_hash_finish(&hash, digest, sizeof(digest), &digest_len);
        if (err == ESP_OK && (hash_err != PSA_SUCCESS || digest_len != sizeof(digest))) {
            ESP_LOGE(TAG, "SHA-256 calculation failed: status=%ld length=%u",
                     (long)hash_err, (unsigned)digest_len);
            err = ESP_FAIL;
        }
    }
    if (err == ESP_OK && memcmp(digest, manifest->sha256, sizeof(digest)) != 0) {
        char expected_hex[sizeof(digest) * 2 + 1];
        char actual_hex[sizeof(digest) * 2 + 1];
        for (size_t i = 0; i < sizeof(digest); ++i) {
            snprintf(expected_hex + i * 2, 3, "%02x", manifest->sha256[i]);
            snprintf(actual_hex + i * 2, 3, "%02x", digest[i]);
        }
        expected_hex[sizeof(expected_hex) - 1] = '\0';
        actual_hex[sizeof(actual_hex) - 1] = '\0';
        ESP_LOGE(TAG, "Downloaded image SHA-256 mismatch: expected=%s actual=%s",
                 expected_hex, actual_hex);
        err = ESP_ERR_INVALID_CRC;
    }
    if (began) {
        esp_err_t end_err = esp_ota_end(ota);
        if (err == ESP_OK) err = end_err;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA image received completely (%u bytes)", (unsigned)received);
        err = esp_ota_set_boot_partition(partition);
    }
    return err;
}

static void ota_worker_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "OTA worker started on Core %d", xPortGetCoreID());
    update_manifest_t cached_manifest = {};
    bool have_manifest = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (s_check_requested && !have_manifest) {
            s_check_requested = false;
            ESP_LOGI(TAG, "Processing update check request");
            s_cancel_requested = false;
            s_snapshot.state = UPDATE_STATE_CHECKING; s_snapshot.progress_percent = 0; notify_ota_ui();
            memset(s_manifest_json, 0, sizeof(s_manifest_json));
            size_t manifest_len = 0;
            esp_err_t err = fetch_manifest(MANIFEST_PRIMARY, s_manifest_json, &manifest_len);
            if (s_cancel_requested) { set_error("Update check cancelled"); continue; }
            if (err != ESP_OK) err = fetch_manifest(MANIFEST_MIRROR, s_manifest_json, &manifest_len);
            if (s_cancel_requested) { set_error("Update check cancelled"); continue; }
            if (err != ESP_OK) { set_error("Update source unavailable"); continue; }
            memset(&cached_manifest, 0, sizeof(cached_manifest));
            err = crystal_update_parse_manifest(s_manifest_json, manifest_len, EXPECTED_TARGET,
                                                EXPECTED_CHANNEL, crystal_version_string(), &cached_manifest);
            if (err == ESP_ERR_NOT_FOUND) {
                ESP_LOGI(TAG, "No newer firmware is available (current=%s)", crystal_version_string());
                s_snapshot.state = UPDATE_STATE_CURRENT;
                notify_ota_ui();
                continue;
            }
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Manifest validation failed: %s", esp_err_to_name(err));
                set_error("Invalid update manifest");
                continue;
            }
            ESP_LOGI(TAG, "Update available: %s (%lu bytes)", cached_manifest.version,
                     (unsigned long)cached_manifest.size);
            memcpy(s_snapshot.available_version, cached_manifest.version, sizeof(s_snapshot.available_version));
            strlcpy(s_snapshot.notes, cached_manifest.notes, sizeof(s_snapshot.notes));
            s_snapshot.download_size = cached_manifest.size; s_snapshot.downloaded_bytes = 0;
            s_snapshot.progress_percent = 0; s_snapshot.state = UPDATE_STATE_AVAILABLE; notify_ota_ui();
            have_manifest = true;
        }

        if (s_cancel_requested && have_manifest) {
            s_cancel_requested = false;
            s_download_requested = false;
            have_manifest = false;
            s_snapshot.state = UPDATE_STATE_IDLE;
            notify_ota_ui();
            continue;
        }

        if (!s_download_requested || !have_manifest) continue;
        s_download_requested = false;
        ESP_LOGI(TAG, "User confirmed download, starting transfer");
        s_snapshot.state = UPDATE_STATE_DOWNLOADING; notify_ota_ui();
        esp_err_t err = install_image(&cached_manifest);
        have_manifest = false;
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA install failed: %s (0x%x), received=%u/%u",
                     esp_err_to_name(err), (unsigned)err,
                     (unsigned)s_snapshot.downloaded_bytes,
                     (unsigned)cached_manifest.size);
            if (s_cancel_requested) {
                set_error("Update cancelled");
            } else if (err == ESP_ERR_INVALID_CRC) {
                set_error("Downloaded image hash mismatch");
            } else if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
                set_error("Downloaded image failed validation");
            } else if (err == ESP_ERR_OTA_ROLLBACK_INVALID_STATE) {
                set_error("Previous update is still pending confirmation");
            } else {
                set_error("Update failed, nothing changed");
            }
            continue;
        }
        s_snapshot.state = UPDATE_STATE_INSTALLING; s_snapshot.progress_percent = 100;
        strlcpy(s_snapshot.error_message, "Restarting to apply update", sizeof(s_snapshot.error_message)); notify_ota_ui();
        const esp_err_t persist_err = persist_attempted_version(cached_manifest.version);
        if (persist_err != ESP_OK) {
            ESP_LOGE(TAG, "Could not persist attempted OTA version: %s", esp_err_to_name(persist_err));
            set_error("Could not prepare update reboot");
            continue;
        }
        ESP_LOGI(TAG, "Persisted attempted OTA version %s", cached_manifest.version);
        ESP_LOGI(TAG, "OTA successful, rebooting in 2 seconds...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }
}

void crystal_update_init(void)
{
    memset(&s_snapshot, 0, sizeof(s_snapshot)); s_snapshot.state = UPDATE_STATE_IDLE;
    s_rollback_occurred = false;
    memset(s_rollback_attempted_version, 0, sizeof(s_rollback_attempted_version));
    (void)check_rollback_at_boot();
    s_check_requested = false;
    s_download_requested = false;
    // Async reads legitimately retry with EAGAIN while the TLS transport waits
    // for the next packet. Keep real client errors visible without flooding
    // the monitor with expected warning-level retry messages.
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_ERROR);
    if (xTaskCreatePinnedToCore(ota_worker_task, "ota_worker", OTA_WORKER_STACK_SIZE, NULL,
                                OTA_WORKER_PRIORITY, &s_ota_worker_task, OTA_WORKER_CORE) != pdPASS) {
        s_ota_worker_task = NULL;
        return;
    }
    ESP_LOGI(TAG, "OTA worker initialized on Core %d", OTA_WORKER_CORE);
}

bool crystal_update_did_rollback(void)
{
    const bool occurred = s_rollback_occurred;
    s_rollback_occurred = false;
    return occurred;
}

esp_err_t crystal_update_check(void)
{
    if (s_snapshot.state != UPDATE_STATE_IDLE && s_snapshot.state != UPDATE_STATE_CURRENT && s_snapshot.state != UPDATE_STATE_FAILED) return ESP_ERR_INVALID_STATE;
    if (s_ota_worker_task == NULL) return ESP_ERR_INVALID_STATE;
    s_snapshot.state = UPDATE_STATE_CHECKING; s_cancel_requested = false;
    s_check_requested = true;
    return ESP_OK;
}
void crystal_update_cancel_check(void) { s_cancel_requested = true; }
esp_err_t crystal_update_start_download(void)
{
    if (s_snapshot.state != UPDATE_STATE_AVAILABLE) return ESP_ERR_INVALID_STATE;
    s_download_requested = true;
    return ESP_OK;
}
esp_err_t crystal_update_install(void) { return crystal_update_start_download(); }
void crystal_update_snapshot(crystal_update_snapshot_t *out) { if (out) *out = s_snapshot; }
