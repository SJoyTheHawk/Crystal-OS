/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "crystal_control.hpp"
#include "crystal_crypto.hpp"

#include <cstdio>
#include <cerrno>
#include <cstring>
#include <atomic>
#include <deque>
#include <algorithm>
#include <string>
#include <utility>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "crystal_hal.hpp"
#include "crystal_package.hpp"
#include "crystal_registry.hpp"
#include "crystal_core.hpp"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "driver/usb_serial_jtag.h"
#include "sodium.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

namespace {
constexpr int64_t kPinLifetimeUs = 10LL * 60 * 1000000;
constexpr int64_t kTokenLifetimeUs = 24LL * 60 * 60 * 1000000;
constexpr size_t kMaxPairBody = 128;
constexpr size_t kMaxTokenHeader = 96;
constexpr size_t kMaxPackageBytes = 5632 * 1024 + 66;
constexpr size_t kMaxLogLines = 64;
constexpr size_t kUsbChunkBytes = 2048;
constexpr char kUsbInstallPath[] = "/littlefs/apps/.usb-incoming.capp";
static const char *TAG = "crystal_control";

httpd_handle_t s_server = nullptr;
TaskHandle_t s_usb_task = nullptr;
bool s_usb_driver_owned = false;
char s_pairing_pin[7] = {};
char s_token[65] = {};
int64_t s_pin_expires_us = 0;
int64_t s_token_expires_us = 0;
unsigned s_pair_failures = 0;
std::atomic<bool> s_installing{false};
std::deque<std::pair<uint32_t, std::string>> s_logs;
uint32_t s_next_log_id = 1;
std::mutex s_log_mutex;

esp_err_t json_response(httpd_req_t *request, int status, const char *body)
{
    httpd_resp_set_status(request, status == 200 ? "200 OK" : status == 202 ? "202 Accepted" :
        status == 409 ? "409 Conflict" : status == 413 ? "413 Payload Too Large" :
        status == 400 ? "400 Bad Request" : status == 401 ? "401 Unauthorized" :
        status == 404 ? "404 Not Found" : "500 Internal Server Error");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, body);
}

bool read_request_body(httpd_req_t *request, FILE *file)
{
    if (request == nullptr || file == nullptr || request->content_len == 0 ||
            request->content_len > kMaxPackageBytes) return false;
    size_t received = 0;
    char buffer[4096];
    while (received < request->content_len) {
        const size_t wanted = std::min(sizeof(buffer), request->content_len - received);
        const int amount = httpd_req_recv(request, buffer, wanted);
        if (amount <= 0 || fwrite(buffer, 1, static_cast<size_t>(amount), file) != static_cast<size_t>(amount)) return false;
        received += static_cast<size_t>(amount);
    }
    return fflush(file) == 0 && fsync(fileno(file)) == 0 && fseek(file, 0, SEEK_SET) == 0;
}

bool request_path_id(httpd_req_t *request, std::string *id, const char **suffix = nullptr)
{
    if (request == nullptr || id == nullptr) return false;
    const char *prefix = "/api/v1/apps/";
    if (strncmp(request->uri, prefix, strlen(prefix)) != 0) return false;
    const char *start = request->uri + strlen(prefix);
    const char *slash = strchr(start, '/');
    const size_t length = slash ? static_cast<size_t>(slash - start) : strlen(start);
    if (length == 0 || length > 63) return false;
    id->assign(start, length);
    if (!crystal_package_valid_id(*id)) return false;
    if (suffix) *suffix = slash ? slash : nullptr;
    return slash == nullptr || !strcmp(slash, "/data") || !strcmp(slash, "/enabled") || !strcmp(slash, "/launch");
}

bool post_control_action(uint8_t kind, const std::string &id)
{
    CrystalControlAction action{};
    action.kind = kind;
    if (id.size() >= sizeof(action.id)) return false;
    memcpy(action.id, id.c_str(), id.size() + 1);
    return crystal_ui_post(UI_EVT_CONTROL_ACTION, &action, sizeof(action));
}

esp_err_t action_response(httpd_req_t *request, bool queued)
{
    return json_response(request, queued ? 202 : 500,
        queued ? "{\"queued\":true,\"restart_required\":false}" : "{\"error\":\"UI queue unavailable\"}");
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
    // A successfully sent 401 is still an authorization failure. Callers
    // must stop instead of executing the requested operation afterward.
    (void)json_response(request, 401, "{\"error\":\"unauthorized\"}");
    return ESP_FAIL;
}

void reboot_worker(void *)
{
    // HTTP server work runs after the current handler returns, so the 202
    // response is sent first. Reuse its task rather than allocate a new stack.
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_LOGI(TAG, "Rebooting at the paired control panel's request");
    esp_restart();
}

esp_err_t reboot_handler(httpd_req_t *request)
{
    if (require_token(request) != ESP_OK) return ESP_OK;
    // Reserve the same gate as Wi-Fi and USB installs until the reset, so a
    // reboot cannot interrupt a package write or allow a new upload to start.
    bool expected = false;
    if (!s_installing.compare_exchange_strong(expected, true)) {
        return json_response(request, 409, "{\"error\":\"an install or reboot is already in progress\"}");
    }
    if (httpd_queue_work(s_server, reboot_worker, nullptr) != ESP_OK) {
        s_installing = false;
        return json_response(request, 500, "{\"error\":\"could not schedule reboot; please try again\"}");
    }
    return json_response(request, 202, "{\"rebooting\":true}");
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

esp_err_t install_handler(httpd_req_t *request)
{
    if (require_token(request) != ESP_OK) return ESP_OK;
    bool expected = false;
    if (!s_installing.compare_exchange_strong(expected, true)) return json_response(request, 409, "{\"error\":\"another install is in progress\"}");
    if (request->content_len == 0 || request->content_len > kMaxPackageBytes) {
        s_installing = false;
        return json_response(request, 413, "{\"error\":\"package is too large\"}");
    }
    FILE *file = fopen("/littlefs/apps/.incoming.capp", "wb+");
    bool ok = file != nullptr && read_request_body(request, file);
    std::string error;
    if (ok) ok = crystal_package_install(file, false, &error);
    if (file) fclose(file);
    unlink("/littlefs/apps/.incoming.capp");
    s_installing = false;
    if (!ok) {
        crystal_control_log_line("control", error.c_str());
        char response[256];
        snprintf(response, sizeof(response), "{\"error\":\"%s\"}", error.empty() ? "package installation failed" : error.c_str());
        return json_response(request, 400, response);
    }
    crystal_registry_refresh_catalog();
    crystal_control_log_line("control", "package installed; restart or apply it from Manage Apps");
    return json_response(request, 202, "{\"installed\":true,\"restart_required\":true}");
}

esp_err_t order_handler(httpd_req_t *request)
{
    if (require_token(request) != ESP_OK) return ESP_OK;
    if (request->content_len == 0 || request->content_len > 4096) return json_response(request, 400, "{\"error\":\"invalid order\"}");
    std::string body(request->content_len, '\0'); size_t received = 0;
    while (received < body.size()) {
        const int n = httpd_req_recv(request, &body[received], body.size() - received);
        if (n <= 0) return json_response(request, 400, "{\"error\":\"invalid request body\"}");
        received += static_cast<size_t>(n);
    }
    cJSON *root = cJSON_ParseWithLength(body.c_str(), body.size());
    if (!cJSON_IsArray(root) || cJSON_GetArraySize(root) > 32) { cJSON_Delete(root); return json_response(request, 400, "{\"error\":\"order must be an array\"}"); }
    bool ok = true;
    for (int i = 0; i < cJSON_GetArraySize(root); ++i) {
        const cJSON *item = cJSON_GetArrayItem(root, i);
        const cJSON *id = cJSON_IsObject(item) ? cJSON_GetObjectItemCaseSensitive(item, "id") : nullptr;
        const cJSON *slot = cJSON_IsObject(item) ? cJSON_GetObjectItemCaseSensitive(item, "slot") : nullptr;
        if (!cJSON_IsString(id) || !id->valuestring || !cJSON_IsNumber(slot) || slot->valuedouble < 0 || slot->valuedouble >= UINT16_MAX ||
                !crystal_registry_set_slot(id->valuestring, static_cast<uint16_t>(slot->valuedouble))) { ok = false; break; }
    }
    cJSON_Delete(root);
    return ok ? json_response(request, 202, "{\"saved\":true,\"restart_required\":true}") : json_response(request, 400, "{\"error\":\"order could not be saved\"}");
}

esp_err_t app_action_handler(httpd_req_t *request)
{
    if (require_token(request) != ESP_OK) return ESP_OK;
    std::string id; const char *suffix = nullptr;
    if (!request_path_id(request, &id, &suffix)) return json_response(request, 400, "{\"error\":\"invalid app id\"}");
    if (request->method == HTTP_DELETE && suffix && !strcmp(suffix, "/data")) {
        return action_response(request, post_control_action(CRYSTAL_CONTROL_ACTION_CLEAR_DATA, id));
    }
    if (request->method == HTTP_DELETE) return action_response(request, post_control_action(CRYSTAL_CONTROL_ACTION_UNINSTALL, id));
    if (request->method == HTTP_POST && suffix && !strcmp(suffix, "/enabled")) {
        if (request->content_len == 0 || request->content_len > 128) return json_response(request, 400, "{\"error\":\"invalid request\"}");
        char body[129] = {}; size_t received = 0;
        while (received < request->content_len) {
            const int n = httpd_req_recv(request, body + received, request->content_len - received);
            if (n <= 0) return json_response(request, 400, "{\"error\":\"invalid request body\"}");
            received += static_cast<size_t>(n);
        }
        cJSON *root = cJSON_ParseWithLength(body, received);
        const cJSON *value = cJSON_IsObject(root) ? cJSON_GetObjectItemCaseSensitive(root, "enabled") : nullptr;
        const bool valid = cJSON_IsBool(value);
        const bool enabled = valid && cJSON_IsTrue(value);
        const bool changed = valid && crystal_registry_set_enabled(id.c_str(), enabled);
        cJSON_Delete(root);
        if (!changed) return json_response(request, 400, "{\"error\":\"unknown app or state could not be saved\"}");
        return json_response(request, 202, enabled ? "{\"enabled\":true,\"restart_required\":true}" : "{\"enabled\":false,\"restart_required\":true}");
    }
    if (request->method == HTTP_POST && (suffix == nullptr || !strcmp(suffix, "/launch"))) {
        return action_response(request, post_control_action(CRYSTAL_CONTROL_ACTION_LAUNCH, id));
    }
    return json_response(request, 400, "{\"error\":\"unsupported app action\"}");
}

esp_err_t logs_handler(httpd_req_t *request)
{
    if (require_token(request) != ESP_OK) return ESP_OK;
    httpd_resp_set_type(request, "text/event-stream");
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(request, "Connection", "keep-alive");
    char query[48] = {};
    uint32_t since = 0;
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK) {
        char value[16] = {};
        if (httpd_query_key_value(query, "since", value, sizeof(value)) == ESP_OK) since = static_cast<uint32_t>(strtoul(value, nullptr, 10));
    }
    for (unsigned tick = 0; tick < 60; ++tick) {
        std::deque<std::pair<uint32_t, std::string>> pending;
        {
            std::lock_guard<std::mutex> lock(s_log_mutex);
            pending = s_logs;
        }
        for (const auto &row : pending) {
            if (static_cast<int32_t>(row.first - since) <= 0) continue;
            std::string event = "id: " + std::to_string(row.first) + "\ndata: " + row.second + "\n\n";
            if (httpd_resp_send_chunk(request, event.c_str(), event.size()) != ESP_OK) return ESP_FAIL;
            since = row.first;
        }
        const char heartbeat[] = ": keep-alive\n\n";
        if (httpd_resp_send_chunk(request, heartbeat, sizeof(heartbeat) - 1) != ESP_OK) return ESP_FAIL;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return httpd_resp_send_chunk(request, nullptr, 0);
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

bool usb_write(const char *text)
{
    if (text == nullptr) return false;
    const size_t length = strlen(text);
    return usb_serial_jtag_write_bytes(text, length, pdMS_TO_TICKS(2000)) == static_cast<int>(length);
}

bool usb_read_line(std::string *line)
{
    if (line == nullptr) return false;
    line->clear();
    for (size_t i = 0; i < 128; ++i) {
        char value = 0;
        const int count = usb_serial_jtag_read_bytes(&value, 1, pdMS_TO_TICKS(500));
        if (count != 1) return false;
        if (value == '\n') return true;
        if (value != '\r') line->push_back(value);
    }
    return false;
}

bool usb_read_exact(uint8_t *buffer, size_t length)
{
    size_t received = 0;
    while (received < length) {
        const int count = usb_serial_jtag_read_bytes(buffer + received,
            static_cast<uint32_t>(length - received), pdMS_TO_TICKS(5000));
        if (count <= 0) return false;
        received += static_cast<size_t>(count);
    }
    return true;
}

bool parse_usb_install(const std::string &line, size_t *length)
{
    constexpr char prefix[] = "CRYSTAL/1 INSTALL ";
    if (length == nullptr || line.compare(0, sizeof(prefix) - 1, prefix) != 0) return false;
    const char *start = line.c_str() + sizeof(prefix) - 1;
    char *end = nullptr;
    errno = 0;
    const unsigned long long value = strtoull(start, &end, 10);
    if (errno != 0 || end == start || *end != '\0' || value == 0 || value > kMaxPackageBytes) return false;
    *length = static_cast<size_t>(value);
    return true;
}

void usb_transport_task(void *)
{
    std::string line;
    uint8_t buffer[kUsbChunkBytes];
    while (true) {
        if (!usb_read_line(&line)) continue;
        size_t package_size = 0;
        if (!parse_usb_install(line, &package_size)) {
            (void)usb_write("CRYSTAL/1 ERROR expected INSTALL <bytes>\n");
            continue;
        }
        bool expected = false;
        if (!s_installing.compare_exchange_strong(expected, true)) {
            (void)usb_write("CRYSTAL/1 ERROR install busy\n");
            continue;
        }
        FILE *file = fopen(kUsbInstallPath, "wb+");
        bool ok = file != nullptr;
        if (ok) (void)usb_write("CRYSTAL/1 READY\n");
        size_t received = 0;
        while (ok && received < package_size) {
            const size_t want = std::min(sizeof(buffer), package_size - received);
            if (!usb_read_exact(buffer, want) || fwrite(buffer, 1, want, file) != want) {
                ok = false;
                break;
            }
            received += want;
            char acknowledgement[40];
            snprintf(acknowledgement, sizeof(acknowledgement), "CRYSTAL/1 ACK %zu\n", received);
            if (!usb_write(acknowledgement)) { ok = false; break; }
        }
        std::string error;
        if (ok) {
            if (fflush(file) != 0 || fsync(fileno(file)) != 0 || fseek(file, 0, SEEK_SET) != 0) ok = false;
            if (ok) ok = crystal_package_install(file, true, &error);
        }
        if (file != nullptr) fclose(file);
        unlink(kUsbInstallPath);
        s_installing = false;
        if (!ok) {
            if (error.empty()) error = "USB package upload failed";
            if (error.size() > 200) error.resize(200);
            std::string response = "CRYSTAL/1 ERROR " + error + "\n";
            (void)usb_write(response.c_str());
            crystal_control_log_line("usb", error.c_str());
            continue;
        }
        crystal_registry_refresh_catalog();
        (void)usb_write("CRYSTAL/1 OK {\"installed\":true,\"restart_required\":true}\n");
        crystal_control_log_line("usb", "package installed in developer mode; restart or apply it from Manage Apps");
    }
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

httpd_uri_t s_root_uri, s_pair_uri, s_apps_uri, s_system_uri, s_install_uri,
    s_order_uri, s_action_uri, s_logs_uri, s_reboot_uri;
}

extern "C" void crystal_control_log_line(const char *app_id, const char *line)
{
    if (line == nullptr || line[0] == '\0') return;
    std::string value = app_id && app_id[0] ? std::string(app_id) + ": " + line : line;
    if (value.size() > 240) value.resize(240);
    std::lock_guard<std::mutex> lock(s_log_mutex);
    s_logs.emplace_back(s_next_log_id++, std::move(value));
    while (s_logs.size() > kMaxLogLines) s_logs.pop_front();
}

bool crystal_control_enable(char pairing_pin[7])
{
    if (pairing_pin == nullptr || !crystal_crypto_ready()) return false;
    // This service is intentionally bound only when the device has a LAN
    // address. It is opt-in in Settings and never starts during boot.
    if (hal().wifi == nullptr || !hal().wifi->has_ip()) return false;
    if (s_server != nullptr) crystal_control_disable();
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    // Keep the panel's session pool small; this local service
    // and must coexist with outbound HTTPS clients using the shared lwIP pool.
    config.max_open_sockets = 4;
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
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
        register_uri(&s_install_uri, "/api/v1/apps", HTTP_POST, install_handler) &&
        register_uri(&s_system_uri, "/api/v1/system", HTTP_GET, system_handler) &&
        register_uri(&s_reboot_uri, "/api/v1/system/reboot", HTTP_POST, reboot_handler) &&
        register_uri(&s_order_uri, "/api/v1/apps/order", HTTP_PATCH, order_handler) &&
        register_uri(&s_action_uri, "/api/v1/apps/*", HTTP_DELETE, app_action_handler) &&
        register_uri(&s_action_uri, "/api/v1/apps/*", HTTP_POST, app_action_handler) &&
        register_uri(&s_logs_uri, "/api/v1/logs", HTTP_GET, logs_handler);
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

bool crystal_control_usb_start(void)
{
    if (s_usb_task != nullptr) return true;
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        // The host waits for an ACK after each 2 KB chunk. Two chunks of
        // receive capacity leave margin without reserving 8 KB before Wi-Fi
        // allocates its internal DMA buffers.
        config.rx_buffer_size = 2 * kUsbChunkBytes;
        if (usb_serial_jtag_driver_install(&config) != ESP_OK) return false;
        s_usb_driver_owned = true;
    }
    const BaseType_t created = xTaskCreateWithCaps(usb_transport_task, "crystal_usb", 12288,
        nullptr, 4, &s_usb_task, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (created != pdPASS) {
        s_usb_task = nullptr;
        if (s_usb_driver_owned) {
            usb_serial_jtag_driver_uninstall();
            s_usb_driver_owned = false;
        }
        return false;
    }
    ESP_LOGI(TAG, "USB developer transport ready (CRYSTAL/1 INSTALL <bytes>)");
    return true;
}
