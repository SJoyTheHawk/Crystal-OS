#!/usr/bin/env python3
"""Exercise production diagnostic capture, bounded logs and queued checks on the host."""
from pathlib import Path
import subprocess
import tempfile
from test_developer_tools_page import function

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'components/crystal_wifi_diagnostics/src/crystal_wifi_diagnostics.cpp'

PREAMBLE = r'''
#include <cassert>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include "crystal_wifi_diagnostics.hpp"
using QueueHandle_t = void *;
using TaskHandle_t = void *;
using vprintf_like_t = int (*)(const char *, va_list);
enum esp_log_level_t { ESP_LOG_ERROR = 1, ESP_LOG_INFO = 3 };
constexpr unsigned pdPASS = 1, pdTRUE = 1, portMAX_DELAY = 0xffffffffU;
constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
'''

MOCK = r'''
struct Idle {};
static bool fail_queue, fail_task, stop_on_probe;
static unsigned tasks, hooks, statuses, radios, versions, ips, resolutions, requests;
static esp_log_level_t log_level = ESP_LOG_ERROR;
static std::deque<Work> pending;
static std::string serial;
static std::mutex serial_mutex;
static int sink(const char *format, va_list args) {
    char line[1024]; int n = vsnprintf(line, sizeof(line), format, args);
    std::lock_guard<std::mutex> lock(serial_mutex); serial += line; return n;
}
static vprintf_like_t output = sink;
static int emit(const char *format, ...) {
    va_list args; va_start(args, format); int n = output(format, args); va_end(args); return n;
}
#define ESP_LOGI(tag, format, ...) emit("I %s: " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW ESP_LOGI
static vprintf_like_t esp_log_set_vprintf(vprintf_like_t next) {
    ++hooks; auto previous = output; output = next; return previous;
}
static esp_log_level_t esp_log_level_get(const char *) { return log_level; }
static void esp_log_level_set(const char *, esp_log_level_t level) { log_level = level; }
static QueueHandle_t xQueueCreate(unsigned count, size_t size) {
    assert(count == 2 && size == sizeof(Work)); return fail_queue ? nullptr : (void *)1;
}
static int xTaskCreateWithCaps(void (*)(void *), const char *, unsigned stack, void *, unsigned priority,
                              TaskHandle_t *handle, unsigned caps) {
    assert(stack == 8192 && priority == 2 && caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (fail_task) return 0;
    ++tasks; *handle = (void *)1; return pdPASS;
}
static int xQueueSend(QueueHandle_t queue, const Work *work, unsigned timeout) {
    assert(queue && timeout == 0); if (pending.size() == 2) return 0;
    pending.push_back(*work); return pdTRUE;
}
static int xQueueReceive(QueueHandle_t, Work *work, unsigned timeout) {
    assert(timeout == portMAX_DELAY); if (pending.empty()) throw Idle{};
    *work = pending.front(); pending.pop_front(); return pdTRUE;
}
static void xQueueReset(QueueHandle_t) { pending.clear(); }
struct Address { unsigned addr = 1; };
struct esp_netif_ip_info_t { Address ip, gw; };
static void *esp_netif_get_default_netif() { return (void *)1; }
constexpr int ESP_OK = 0;
static int esp_netif_get_ip_info(void *, esp_netif_ip_info_t *) { return ESP_OK; }
#define IPSTR "%u"
#define IP2STR(p) ((p)->addr)
static void status() { ++statuses; ESP_LOGI(TAG, "local status"); }
static void radio() { ++radios; }
static void c6_version() { ++versions; }
static void numeric_ip(const char *, const char *) {
    ++ips; if (stop_on_probe) crystal_wifi_diagnostics_set_enabled(false);
}
static void resolve(const char *) { ++resolutions; }
static void https(const char *) { ++requests; }
'''

CHECK = r'''
static std::string logs(size_t capacity = 8192) {
    std::string text(capacity, '?'); uint32_t revision;
    auto size = crystal_wifi_diagnostics_copy_logs(text.data(), capacity, &revision);
    assert(size < capacity && text[size] == 0); text.resize(size); return text;
}
static void drain() { try { diagnostic_worker(nullptr); } catch (const Idle &) {} }
int main() {
    assert(!crystal_wifi_diagnostics_enabled());
    assert(!crystal_wifi_diagnostics_request(CrystalWifiDiagnostic::Snapshot));
    fail_queue = true; assert(!crystal_wifi_diagnostics_set_enabled(true));
    fail_queue = false; fail_task = true; assert(!crystal_wifi_diagnostics_set_enabled(true));
    assert(!crystal_wifi_diagnostics_enabled() && hooks == 0);
    fail_task = false; assert(crystal_wifi_diagnostics_set_enabled(true));
    assert(hooks == 1 && tasks == 1 && log_level == ESP_LOG_INFO);
    assert(crystal_wifi_diagnostics_set_enabled(true) && pending.size() == 1);
    drain(); assert(statuses == 1 && radios == 0 && versions == 0 && ips == 0 && requests == 0);
    crystal_wifi_diagnostics_clear();
    emit("I other: ignore %d\n", 8); assert(logs().empty());
    emit("\033[32mI wifi_check: IP %d\033[0m\r\n", 42);
    assert(logs() == "I wifi_check: IP 42\n");
    assert(serial.find("IP 42") != std::string::npos);
    uint32_t before, after; crystal_wifi_diagnostics_copy_logs(nullptr, 0, &before);
    emit("E eh_host_port_sdio: timeout\n");
    crystal_wifi_diagnostics_copy_logs(nullptr, 0, &after); assert(before != after);
    for (unsigned i = 0; i < 40; ++i) emit("I crystal_http: line %02u\n", i);
    auto text = logs(); assert(text.find("line 07") == std::string::npos);
    assert(text.find("line 08") != std::string::npos && text.find("line 39") != std::string::npos);
    assert(logs(25) == "I crystal_http: line 39\n");
    assert(logs(1).empty());
    for (size_t n = 1; n < 260; ++n) (void)logs(n);
    std::thread producer([] { for (int i = 0; i < 2000; ++i) emit("W wifi: disconnect %d\n", i); });
    for (int i = 0; i < 1000; ++i) (void)logs(256);
    producer.join();
    assert(crystal_wifi_diagnostics_request(CrystalWifiDiagnostic::Snapshot));
    assert(crystal_wifi_diagnostics_request(CrystalWifiDiagnostic::NetworkChecks));
    assert(!crystal_wifi_diagnostics_request(CrystalWifiDiagnostic::Snapshot));
    drain(); assert(radios == 1 && versions == 1 && ips == 1 && resolutions == 1 && requests == 1);
    Work stale{2, s_generation.load()};
    assert(crystal_wifi_diagnostics_request(CrystalWifiDiagnostic::NetworkChecks));
    assert(crystal_wifi_diagnostics_set_enabled(false));
    assert(pending.empty() && log_level == ESP_LOG_ERROR);
    text = logs(); emit("W wifi: disabled\n"); assert(logs() == text);
    assert(crystal_wifi_diagnostics_set_enabled(true));
    pending.push_back(stale); drain(); assert(ips == 1);
    stop_on_probe = true; assert(crystal_wifi_diagnostics_request(CrystalWifiDiagnostic::NetworkChecks));
    drain(); assert(ips == 2 && resolutions == 1 && requests == 1);
    assert(!crystal_wifi_diagnostics_enabled() && hooks == 1 && tasks == 1);
    puts("PASS: opt-in capture, serial forwarding, filtering, bounded logs, concurrent reads, queued checks and cancellation");
}
'''

def main():
    source = SOURCE.read_text()
    globals_ = source.split('namespace {\n', 1)[1].split('bool relevant(', 1)[0]
    names = ('bool relevant(const char *line)', 'int capture_output(const char *format, va_list arguments)',
             'bool current(const Work &work)', 'void diagnostic_worker(void *)',
             'void crystal_wifi_diagnostics_clear()',
             'size_t crystal_wifi_diagnostics_copy_logs(char *out, size_t capacity, uint32_t *revision)',
             'bool crystal_wifi_diagnostics_set_enabled(bool enabled)',
             'bool crystal_wifi_diagnostics_request(CrystalWifiDiagnostic operation)')
    production = '\n'.join(function(source, name) for name in names)
    production += '\nbool crystal_wifi_diagnostics_enabled() { return s_enabled.load(); }\n'
    with tempfile.TemporaryDirectory(prefix='wifi-diagnostics-test-') as directory:
        path = Path(directory)
        (path / 'test.cpp').write_text(PREAMBLE + globals_ + MOCK + production + CHECK)
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread',
                        '-I', str(ROOT / 'components/crystal_wifi_diagnostics/include'),
                        str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)

if __name__ == '__main__':
    main()
