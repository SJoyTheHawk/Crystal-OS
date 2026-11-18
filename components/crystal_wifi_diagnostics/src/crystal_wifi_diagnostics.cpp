/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
// Runtime log capture and on-demand checks. No automatic network traffic.
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include <cstdarg>
#include <mutex>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "crystal_hal.hpp"
#include "crystal_network_policy.hpp"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"
#include "crystal_wifi_diagnostics.hpp"
#if CONFIG_IDF_TARGET_ESP32P4
#include "esp_hosted.h"
#endif

namespace {
constexpr const char *TAG = "wifi_check";
constexpr size_t kLogLines = 32, kLineBytes = 224;
char s_lines[kLogLines][kLineBytes]{};
size_t s_next_line = 0, s_line_count = 0;
uint32_t s_revision = 1;
std::mutex s_logs_mutex, s_control_mutex;
std::atomic<bool> s_enabled{false};
std::atomic<uint32_t> s_generation{0};
std::atomic<vprintf_like_t> s_previous_output{nullptr};
bool s_hook_installed = false;
esp_log_level_t s_wifi_log_level = ESP_LOG_ERROR;
struct Work { unsigned operation; uint32_t generation; };
QueueHandle_t s_queue = nullptr;
TaskHandle_t s_worker = nullptr;

bool relevant(const char *line)
{
    const char *tags[] = {"wifi_check:", "crystal_hal:", "crystal_network:",
        "crystal_http:", "esp_netif_handlers:", "esp-tls:", "esp-tls-mbedtls:",
        "wifi:", "wifi_init:", "eh_", "sdio:", "esp_hosted:"};
    for (const char *tag : tags) if (strstr(line, tag)) return true;
    return false;
}

int capture_output(const char *format, va_list arguments)
{
    // Keep normal serial output intact. Use an independent va_list for capture.
    if (s_enabled.load(std::memory_order_relaxed)) {
        char formatted[kLineBytes]{};
        va_list copy; va_copy(copy, arguments);
        vsnprintf(formatted, sizeof(formatted), format, copy); va_end(copy);
        if (relevant(formatted)) {
            // Never stall a driver/event task behind the viewer, or log while
            // holding this lock. A full ring simply replaces its oldest line.
            std::unique_lock<std::mutex> lock(s_logs_mutex, std::try_to_lock);
            if (lock.owns_lock() && s_enabled.load(std::memory_order_relaxed)) {
                char *out = s_lines[s_next_line]; size_t n = 0;
                bool escape = false;
                for (const unsigned char *p = reinterpret_cast<const unsigned char *>(formatted); *p; ++p) {
                    if (*p == 27) { escape = true; continue; }
                    if (escape) { if (*p >= '@' && *p <= '~' && *p != '[') escape = false; continue; }
                    if (*p == '\r' || *p == '\n') continue;
                    if (*p >= 32 && n + 1 < kLineBytes) out[n++] = static_cast<char>(*p);
                }
                out[n] = 0;
                if (n) {
                    s_next_line = (s_next_line + 1) % kLogLines;
                    if (s_line_count < kLogLines) ++s_line_count;
                    ++s_revision;
                }
            }
        }
    }
    const auto previous = s_previous_output.load();
    return previous ? previous(format, arguments) : vprintf(format, arguments);
}

void status()
{
    const auto *app = esp_app_get_description();
    auto *wifi = hal().wifi;
    auto *route = esp_netif_get_default_netif();
    ESP_LOGI(TAG, "app=%s idf=%s enabled=%d associated=%d has_ip=%d route=%s internal=%u largest=%u",
             app->version, app->idf_ver, wifi && wifi->enabled(), wifi && wifi->connected(),
             wifi && wifi->has_ip(), route ? esp_netif_get_ifkey(route) : "none",
             unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    ESP_LOGI(TAG, "buffers host_dma_free=%u host_dma_largest=%u host_internal_low=%u; C6/pool occupancy not measured",
             unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)),
             unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    const char *keys[] = {"WIFI_STA_DEF", "ETH_DEF"};
    for (const char *key : keys) {
        auto *netif = esp_netif_get_handle_from_ifkey(key);
        if (!netif) continue;
        esp_netif_ip_info_t ip{};
        const auto err = esp_netif_get_ip_info(netif, &ip);
        ESP_LOGI(TAG, "%s up=%d ip_result=%s ip=" IPSTR " gateway=" IPSTR,
                 key, esp_netif_is_netif_up(netif), esp_err_to_name(err), IP2STR(&ip.ip), IP2STR(&ip.gw));
        for (int i = 0; i < 2; ++i) {
            esp_netif_dns_info_t dns{};
            const auto result = esp_netif_get_dns_info(netif, static_cast<esp_netif_dns_type_t>(i), &dns);
            if (result == ESP_OK && dns.ip.type == ESP_IPADDR_TYPE_V4)
                ESP_LOGI(TAG, "%s dns%d=" IPSTR, key, i, IP2STR(&dns.ip.u_addr.ip4));
            else ESP_LOGI(TAG, "%s dns%d result=%s type=%d", key, i, esp_err_to_name(result), int(dns.ip.type));
        }
    }
}

void radio()
{
    wifi_ps_type_t ps = WIFI_PS_NONE;
    const auto ps_err = esp_wifi_get_ps(&ps);
    ESP_LOGI(TAG, "get_ps result=%s mode=%d (valid only on success)", esp_err_to_name(ps_err), int(ps));
    wifi_ap_record_t ap{};
    const auto ap_err = esp_wifi_sta_get_ap_info(&ap);
    ESP_LOGI(TAG, "ap_info result=%s rssi=%d channel=%u (valid only on success)",
             esp_err_to_name(ap_err), int(ap.rssi), unsigned(ap.primary));
}

void c6_version()
{
#if CONFIG_IDF_TARGET_ESP32P4
    esp_hosted_coprocessor_fwver_t ver{};
    const auto err = esp_hosted_get_coprocessor_fwversion(&ver);
    if (err != ESP_OK || !(ver.major1 || ver.minor1 || ver.patch1))
        ESP_LOGI(TAG, "C6 version=unknown query_result=%s", esp_err_to_name(err));
    else ESP_LOGI(TAG, "C6 version=%lu.%lu.%lu",
                  (unsigned long)ver.major1, (unsigned long)ver.minor1, (unsigned long)ver.patch1);
#else
    ESP_LOGI(TAG, "C6 query not applicable to this target");
#endif
}

void numeric_ip(const char *host, const char *port_text)
{
    sockaddr_in address{};
    address.sin_family = AF_INET;
    char *end = nullptr;
    const long port = port_text ? strtol(port_text, &end, 10) : 80;
    if (!host || inet_pton(AF_INET, host, &address.sin_addr) != 1 ||
            (port_text && (!*port_text || *end)) || port < 1 || port > 65535) {
        ESP_LOGW(TAG, "usage: netdiag ip <IPv4> [port]; choose a known reachable TCP service");
        return;
    }
    address.sin_port = htons(static_cast<uint16_t>(port));
    const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) { ESP_LOGW(TAG, "socket errno=%d", errno); return; }
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
        ESP_LOGW(TAG, "nonblocking socket errno=%d", errno); close(fd); return;
    }
    int result = connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    int error = result == 0 ? 0 : errno;
    if (result != 0 && error == EINPROGRESS) {
        fd_set writable; FD_ZERO(&writable); FD_SET(fd, &writable);
        timeval timeout{5, 0};
        result = select(fd + 1, nullptr, &writable, nullptr, &timeout);
        error = result == 0 ? ETIMEDOUT : result < 0 ? errno : 0;
        if (result > 0) {
            socklen_t size = sizeof(error);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) != 0) error = errno;
        }
    }
    ESP_LOGI(TAG, "TCP numeric_ip=%s port=%ld errno=%d; refusal/timeout alone is not proof of WiFi failure", host, port, error);
    close(fd);
}

void resolve(const char *host)
{
    if (!host) { ESP_LOGW(TAG, "usage: netdiag dns <hostname>"); return; }
    addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    addrinfo *answer = nullptr;
    const int err = getaddrinfo(host, nullptr, &hints, &answer);
    if (err == 0 && answer) {
        char ip[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in *>(answer->ai_addr)->sin_addr, ip, sizeof(ip));
        ESP_LOGI(TAG, "DNS host=%s result=0 address=%s (resolver may use cache)", host, ip);
    } else ESP_LOGW(TAG, "DNS host=%s result=%d", host, err);
    if (answer) freeaddrinfo(answer);
}

void https(const char *url)
{
    if (!url || strncmp(url, "https://", 8)) { ESP_LOGW(TAG, "usage: netdiag https <https://URL>"); return; }
    esp_http_client_config_t config{};
    config.url = url; config.method = HTTP_METHOD_HEAD; config.timeout_ms = 5000;
    config.crt_bundle_attach = esp_crt_bundle_attach; config.disable_auto_redirect = true;
    auto client = esp_http_client_init(&config);
    if (!client) { ESP_LOGW(TAG, "HTTPS client allocation failed"); return; }
    const auto err = esp_http_client_perform(client);
    int tls_code = 0, tls_flags = 0;
    const auto tls_err = esp_http_client_get_and_clear_last_tls_error(client, &tls_code, &tls_flags);
    ESP_LOGI(TAG, "HTTPS result=%s status=%d errno=%d tls=%s tls_code=%d flags=%d",
             esp_err_to_name(err), esp_http_client_get_status_code(client), esp_http_client_get_errno(client),
             esp_err_to_name(tls_err), tls_code, tls_flags);
    esp_http_client_cleanup(client);
}

bool current(const Work &work)
{
    return s_enabled.load() && work.generation == s_generation.load();
}

void diagnostic_worker(void *)
{
    for (;;) {
        Work work{};
        if (xQueueReceive(s_queue, &work, portMAX_DELAY) != pdTRUE || !current(work)) continue;
        ESP_LOGI(TAG, "BEGIN %s", work.operation == 2 ? "network checks" : "snapshot");
        status();
        if (work.operation == 2) {
            auto *route = esp_netif_get_default_netif(); esp_netif_ip_info_t ip{};
            if (!route || esp_netif_get_ip_info(route, &ip) != ESP_OK || !ip.ip.addr) {
                ESP_LOGW(TAG, "No active IP route; network checks skipped");
            } else {
                char gateway[16]{}; snprintf(gateway, sizeof(gateway), IPSTR, IP2STR(&ip.gw));
                if (ip.gw.addr && current(work)) numeric_ip(gateway, "80");
                if (current(work)) resolve("data.etabus.gov.hk");
                if (current(work)) https("https://data.etabus.gov.hk/");
            }
        } else if (work.operation == 1) {
            if (current(work)) radio();
            if (current(work)) c6_version();
        }
        if (current(work)) ESP_LOGI(TAG, "END checks");
    }
}

void command(char *line)
{
    char *save = nullptr;
    const char *prefix = strtok_r(line, " \r\t", &save);
    if (!prefix || strcmp(prefix, "netdiag")) return;
    if (!s_enabled.load()) {
        ESP_LOGW(TAG, "Enable Wi-Fi Diagnostics in Developer Tools first"); return;
    }
    const char *operation = strtok_r(nullptr, " \r\t", &save);
    const char *argument = strtok_r(nullptr, " \r\t", &save);
    const auto started = esp_timer_get_time();
    ESP_LOGI(TAG, "BEGIN %s", operation ? operation : "help");
    if (operation && !strcmp(operation, "status")) status();
    else if (operation && !strcmp(operation, "radio")) radio();
    else if (operation && !strcmp(operation, "c6")) c6_version();
    else if (operation && !strcmp(operation, "ip")) numeric_ip(argument, strtok_r(nullptr, " \r\t", &save));
    else if (operation && !strcmp(operation, "dns")) resolve(argument);
    else if (operation && !strcmp(operation, "https")) https(argument);
    else ESP_LOGI(TAG, "netdiag status | radio | c6 | ip <IPv4> [port] | dns <host> | https <URL>");
    ESP_LOGI(TAG, "END elapsed_ms=%lld", (long long)((esp_timer_get_time() - started) / 1000));
}

void console_task(void *)
{
    // UART VFS already owns the console; do not reinstall or reconfigure it.
    const int fd = open("/dev/uart/0", O_RDONLY | O_NONBLOCK);
    if (fd < 0) { ESP_LOGE(TAG, "Cannot open UART console errno=%d", errno); vTaskDelete(nullptr); return; }
    char line[384]{}; size_t used = 0; bool overflow = false;
    ESP_LOGI(TAG, "UART netdiag ready; commands only, no automatic probes");
    for (;;) {
        char input[64];
        const int count = read(fd, input, sizeof(input));
        for (int i = 0; i < count; ++i) {
            const char ch = input[i];
            if (ch == '\n') {
                if (!overflow) { line[used] = 0; command(line); }
                else ESP_LOGW(TAG, "Command too long; discarded");
                used = 0; overflow = false;
            } else if (ch != '\r' && ch != '\0') {
                if (used + 1 < sizeof(line)) line[used++] = ch; else overflow = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
}

bool crystal_wifi_diagnostics_enabled() { return s_enabled.load(); }

void crystal_wifi_diagnostics_clear()
{
    std::lock_guard<std::mutex> lock(s_logs_mutex);
    s_line_count = s_next_line = 0; ++s_revision;
}

size_t crystal_wifi_diagnostics_copy_logs(char *out, size_t capacity, uint32_t *revision)
{
    std::lock_guard<std::mutex> lock(s_logs_mutex);
    if (revision) *revision = s_revision;
    if (!out || !capacity) return 0;
    size_t used = 0, first = s_line_count;
    // Fit the newest whole lines; never leave a half-line at the beginning.
    while (first) {
        const size_t index = (s_next_line + kLogLines - (s_line_count - first + 1)) % kLogLines;
        const size_t length = strlen(s_lines[index]) + 1;
        if (used + length >= capacity) break;
        used += length; --first;
    }
    used = 0;
    for (size_t i = first; i < s_line_count; ++i) {
        const char *line = s_lines[(s_next_line + kLogLines - s_line_count + i) % kLogLines];
        const size_t length = strlen(line);
        memcpy(out + used, line, length); used += length; out[used++] = '\n';
    }
    out[used] = 0; return used;
}

bool crystal_wifi_diagnostics_set_enabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(s_control_mutex);
    if (s_enabled.load() == enabled) return true;
    if (enabled) {
        if (!s_queue) s_queue = xQueueCreate(2, sizeof(Work));
        if (!s_queue) return false;
        if (!s_worker && xTaskCreateWithCaps(diagnostic_worker, "wifi_diag", 8192, nullptr, 2,
                &s_worker, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) return false;
        crystal_wifi_diagnostics_clear();
        if (!s_hook_installed) {
            s_previous_output = esp_log_set_vprintf(capture_output); s_hook_installed = true;
        }
        s_wifi_log_level = esp_log_level_get("wifi");
        if (s_wifi_log_level < ESP_LOG_INFO) esp_log_level_set("wifi", ESP_LOG_INFO);
        ++s_generation; s_enabled.store(true);
        ESP_LOGI(TAG, "Diagnostics enabled for this boot; passive capture, no periodic probes");
        Work work{0, s_generation.load()}; (void)xQueueSend(s_queue, &work, 0);
    } else {
        s_enabled.store(false); ++s_generation;
        xQueueReset(s_queue); esp_log_level_set("wifi", s_wifi_log_level);
    }
    return true;
}

bool crystal_wifi_diagnostics_request(CrystalWifiDiagnostic operation)
{
    std::lock_guard<std::mutex> lock(s_control_mutex);
    if (!s_enabled.load() || !s_queue) return false;
    Work work{operation == CrystalWifiDiagnostic::Snapshot ? 1u : 2u, s_generation.load()};
    return xQueueSend(s_queue, &work, 0) == pdTRUE;
}

void crystal_wifi_diagnostics_start_uart()
{
    if (xTaskCreate(console_task, "wifi_check", 8192, nullptr, 2, nullptr) != pdPASS)
        ESP_LOGE(TAG, "Cannot start diagnostic task");
}
