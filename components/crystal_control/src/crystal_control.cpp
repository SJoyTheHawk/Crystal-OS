/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "crystal_control.hpp"
#include "crystal_crypto.hpp"

#include <cstdio>
#include <cerrno>
#include <cstring>

#include "cJSON.h"
#include "crystal_hal.hpp"
#include "crystal_package.hpp"
#include "crystal_registry.hpp"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "sodium.h"

namespace {
constexpr int64_t kPinLifetimeUs = 10LL * 60 * 1000000;
constexpr int64_t kTokenLifetimeUs = 24LL * 60 * 60 * 1000000;
constexpr size_t kMaxPairBody = 128;
constexpr size_t kMaxTokenHeader = 96;
static const char *TAG = "crystal_control";

httpd_handle_t s_server = nullptr;
char s_pairing_pin[7] = {};
char s_token[65] = {};
int64_t s_pin_expires_us = 0;
int64_t s_token_expires_us = 0;
unsigned s_pair_failures = 0;

esp_err_t json_response(httpd_req_t *request, int status, const char *body)
{
    httpd_resp_set_status(request, status == 200 ? "200 OK" :
        status == 400 ? "400 Bad Request" : status == 401 ? "401 Unauthorized" :
        status == 404 ? "404 Not Found" : "500 Internal Server Error");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, body);
}

bool token_matches(httpd_req_t *request)
{
    if (s_token[0] == '\0' || esp_timer_get_time() >= s_token_expires_us) return false;
    const size_t length = httpd_req_get_hdr_value_len(request, "Authorization");
    if (length < 8 || length >= kMaxTokenHeader) return false;
    char header[kMaxTokenHeader] = {};
    if (httpd_req_get_hdr_value_str(request, "Authorization", header, sizeof(header)) != ESP_OK ||
            strncmp(header, "Bearer ", 7) != 0 || strlen(header + 7) != sizeof(s_token) - 1) return false;
    return sodium_memcmp(header + 7, s_token, sizeof(s_token) - 1) == 0;
}

esp_err_t require_token(httpd_req_t *request)
{
    if (token_matches(request)) return ESP_OK;
    return json_response(request, 401, "{\"error\":\"unauthorized\"}");
}

esp_err_t pair_handler(httpd_req_t *request)
{
    // Session tokens require live RF entropy. Wi-Fi can be disabled after
    // enabling the panel, so check again before accepting a pairing request.
    if (!crystal_crypto_ready() || hal().wifi == nullptr || !hal().wifi->has_ip()) {
        return json_response(request, 500, "{\"error\":\"pairing requires an active Wi-Fi connection\"}");
    }
    if (request->content_len == 0 || request->content_len > kMaxPairBody) {
        return json_response(request, 400, "{\"error\":\"invalid request\"}");
    }
    char body[kMaxPairBody + 1] = {};
    size_t received = 0;
    while (received < request->content_len) {
        const int amount = httpd_req_recv(request, body + received,
                                         request->content_len - received);
        if (amount <= 0) return json_response(request, 400, "{\"error\":\"invalid request body\"}");
        received += static_cast<size_t>(amount);
    }
    cJSON *root = cJSON_Parse(body);
    const cJSON *pin = cJSON_IsObject(root) ? cJSON_GetObjectItemCaseSensitive(root, "pin") : nullptr;
    const bool shape_ok = cJSON_IsString(pin) && pin->valuestring && strlen(pin->valuestring) == 6;
    const bool pin_live = s_pairing_pin[0] != '\0' && esp_timer_get_time() < s_pin_expires_us;
    const bool matches = shape_ok && pin_live && sodium_memcmp(pin->valuestring, s_pairing_pin, 6) == 0;
    cJSON_Delete(root);
    if (!matches || s_pair_failures >= 5) {
        ++s_pair_failures;
        if (s_pair_failures >= 5) {
            sodium_memzero(s_pairing_pin, sizeof(s_pairing_pin));
            s_pin_expires_us = 0;
        }
        return json_response(request, 401, "{\"error\":\"pairing failed; generate a new PIN on device\"}");
    }

    uint8_t random[32];
    randombytes_buf(random, sizeof(random));
    sodium_bin2hex(s_token, sizeof(s_token), random, sizeof(random));
    sodium_memzero(random, sizeof(random));
    s_token_expires_us = esp_timer_get_time() + kTokenLifetimeUs;
    sodium_memzero(s_pairing_pin, sizeof(s_pairing_pin));
    s_pin_expires_us = 0;
    s_pair_failures = 0;

    cJSON *response = cJSON_CreateObject();
    if (!response || !cJSON_AddStringToObject(response, "token", s_token) ||
            !cJSON_AddNumberToObject(response, "expires_in", 86400)) {
        cJSON_Delete(response);
        sodium_memzero(s_token, sizeof(s_token));
        s_token_expires_us = 0;
        return json_response(request, 500, "{\"error\":\"could not create session\"}");
    }
    char *encoded = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (!encoded) {
        sodium_memzero(s_token, sizeof(s_token));
        s_token_expires_us = 0;
        return json_response(request, 500, "{\"error\":\"could not create session\"}");
    }
    esp_err_t result = json_response(request, 200, encoded);
    cJSON_free(encoded);
    return result;
}

esp_err_t apps_handler(httpd_req_t *request)
{
    if (require_token(request) != ESP_OK) return ESP_OK;
    cJSON *root = cJSON_CreateObject();
    cJSON *apps = cJSON_CreateArray();
    if (!root || !apps || !cJSON_AddNumberToObject(root, "schema", 1) ||
            !cJSON_AddItemToObject(root, "apps", apps)) {
        cJSON_Delete(root);
        return json_response(request, 500, "{\"error\":\"out of memory\"}");
    }
    for (size_t i = 0; i < crystal_registry_catalog_count(); ++i) {
        const CrystalAppEntry *entry = crystal_registry_catalog_entry(i);
        if (!entry) continue;
        cJSON *app = cJSON_CreateObject();
        if (!app || !cJSON_AddStringToObject(app, "id", entry->id) ||
                !cJSON_AddStringToObject(app, "name", entry->display_name ? entry->display_name : entry->id) ||
                !cJSON_AddStringToObject(app, "origin", entry->origin == CRYSTAL_ORIGIN_PACKAGE ? "package" : "builtin") ||
                !cJSON_AddBoolToObject(app, "enabled", crystal_registry_enabled(entry->id, entry->default_enabled)) ||
                !cJSON_AddNumberToObject(app, "slot", crystal_registry_slot(entry->id, entry->default_slot))) {
            cJSON_Delete(app);
            cJSON_Delete(root);
            return json_response(request, 500, "{\"error\":\"out of memory\"}");
        }
        const char *version = nullptr;
        if (entry->origin == CRYSTAL_ORIGIN_PACKAGE) {
            for (const auto &record : crystal_packages()) if (record.manifest.id == entry->id) {
                version = record.manifest.version.c_str();
                break;
            }
        }
        if (version) cJSON_AddStringToObject(app, "version", version);
        else cJSON_AddNullToObject(app, "version");
        cJSON_AddItemToArray(apps, app);
    }
    char *encoded = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!encoded) return json_response(request, 500, "{\"error\":\"out of memory\"}");
    esp_err_t result = json_response(request, 200, encoded);
    cJSON_free(encoded);
    return result;
}

esp_err_t system_handler(httpd_req_t *request)
{
    if (require_token(request) != ESP_OK) return ESP_OK;
    ISystemInfo *info = hal().system_info;
    cJSON *root = cJSON_CreateObject();
    uint8_t chip_id[6] = {};
    char serial[13] = {};
    if (info && info->chip_id(chip_id)) {
        std::snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X",
            chip_id[0], chip_id[1], chip_id[2], chip_id[3], chip_id[4], chip_id[5]);
    }
    uint32_t used = 0, total = 0;
    const bool storage_ok = info && info->storage_bytes(&used, &total);
    if (!root || !cJSON_AddNumberToObject(root, "schema", 1) ||
            !cJSON_AddStringToObject(root, "os_version", info && info->app_version() ? info->app_version() : "unknown") ||
            !cJSON_AddNumberToObject(root, "abi_version", 1) ||
            !cJSON_AddNumberToObject(root, "free_psram_kb", info ? info->free_psram() / 1024 : 0) ||
            !cJSON_AddNumberToObject(root, "free_heap_kb", info ? info->free_heap() / 1024 : 0) ||
            !cJSON_AddNumberToObject(root, "free_storage_kb", storage_ok ? (total - used) / 1024 : 0) ||
            !cJSON_AddStringToObject(root, "serial", serial[0] ? serial : "unknown")) {
        cJSON_Delete(root);
        return json_response(request, 500, "{\"error\":\"out of memory\"}");
    }
    char *encoded = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!encoded) return json_response(request, 500, "{\"error\":\"out of memory\"}");
    esp_err_t result = json_response(request, 200, encoded);
    cJSON_free(encoded);
    return result;
}

esp_err_t panel_handler(httpd_req_t *request)
{
    // The partition label is 'storage', but its VFS mount point is configured
    // by the board (currently /spiffs). A partition label is not a file path.
    constexpr const char *path = CONFIG_BSP_SPIFFS_MOUNT_POINT "/control-panel/index.html";
    FILE *file = std::fopen(path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Cannot open panel asset %s: errno=%d", path, errno);
        return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Control panel is not installed");
    }
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    char buffer[1024];
    while (true) {
        const size_t length = std::fread(buffer, 1, sizeof(buffer), file);
        if (!length) break;
        if (httpd_resp_send_chunk(request, buffer, length) != ESP_OK) {
            std::fclose(file);
            return ESP_FAIL;
        }
    }
    std::fclose(file);
    return httpd_resp_send_chunk(request, nullptr, 0);
}

bool register_uri(httpd_uri_t *uri, const char *path, httpd_method_t method,
                  esp_err_t (*handler)(httpd_req_t *))
{
    *uri = {};
    uri->uri = path;
    uri->method = method;
    uri->handler = handler;
    return httpd_register_uri_handler(s_server, uri) == ESP_OK;
}

httpd_uri_t s_root_uri, s_pair_uri, s_apps_uri, s_system_uri;
}

bool crystal_control_enable(char pairing_pin[7])
{
    if (pairing_pin == nullptr || !crystal_crypto_ready()) return false;
    // This service is intentionally bound only when the device has a LAN
    // address. It is opt-in in Settings and never starts during boot.
    if (hal().wifi == nullptr || !hal().wifi->has_ip()) return false;
    if (s_server != nullptr) crystal_control_disable();
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 4;
    // Keep the panel's session pool small; this is a read-only local service
    // and must coexist with outbound HTTPS clients using the shared lwIP pool.
    config.max_open_sockets = 4;
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    const size_t internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t largest_before = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "Starting local panel: sockets=%d clients=%u internal=%u largest_internal=%u psram=%u",
        CONFIG_LWIP_MAX_SOCKETS, static_cast<unsigned>(config.max_open_sockets),
        static_cast<unsigned>(internal_before), static_cast<unsigned>(largest_before),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    const esp_err_t start_result = httpd_start(&s_server, &config);
    if (start_result != ESP_OK) {
        ESP_LOGE(TAG, "Local panel startup failed: %s; internal=%u largest_internal=%u psram=%u",
            esp_err_to_name(start_result),
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        s_server = nullptr;
        return false;
    }
    const bool handlers_ok = register_uri(&s_root_uri, "/", HTTP_GET, panel_handler) &&
        register_uri(&s_pair_uri, "/api/v1/pair", HTTP_POST, pair_handler) &&
        register_uri(&s_apps_uri, "/api/v1/apps", HTTP_GET, apps_handler) &&
        register_uri(&s_system_uri, "/api/v1/system", HTTP_GET, system_handler);
    if (!handlers_ok) {
        crystal_control_disable();
        return false;
    }
    std::snprintf(s_pairing_pin, sizeof(s_pairing_pin), "%06lu",
        static_cast<unsigned long>(randombytes_uniform(1000000)));
    s_pin_expires_us = esp_timer_get_time() + kPinLifetimeUs;
    s_pair_failures = 0;
    sodium_memzero(s_token, sizeof(s_token));
    s_token_expires_us = 0;
    std::memcpy(pairing_pin, s_pairing_pin, sizeof(s_pairing_pin));
    ESP_LOGI(TAG, "Local control API enabled for this boot; pairing PIN expires in 10 minutes");
    return true;
}

void crystal_control_disable(void)
{
    if (s_server != nullptr) {
        httpd_stop(s_server);
        s_server = nullptr;
    }
    sodium_memzero(s_pairing_pin, sizeof(s_pairing_pin));
    sodium_memzero(s_token, sizeof(s_token));
    s_pin_expires_us = 0;
    s_token_expires_us = 0;
    s_pair_failures = 0;
}

bool crystal_control_enabled(void)
{
    return s_server != nullptr;
}
