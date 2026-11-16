/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
// Deterministic screenshot fixtures, never linked into firmware.
#include "crystal_core.hpp"
#include "crystal_ethernet.hpp"
#include "crystal_network_policy.hpp"
#include <arpa/inet.h>

// Documentation-only addresses and locally administered MAC; no real network.
CrystalEthernetInfo crystal_ethernet_info()
{
    CrystalEthernetInfo info{};
    info.supported = info.initialized = info.started = info.link = info.has_ip = true;
    info.ip = inet_addr("192.0.2.42");
    info.mask = inet_addr("255.255.255.0");
    info.gateway = info.dns = inet_addr("192.0.2.1");
    info.mac[0] = 0x02;
    info.mac[5] = 0x42;
    info.speed_mbps = 100;
    info.full_duplex = true;
    return info;
}
CrystalNetworkType crystal_network_active_type() { return CrystalNetworkType::Ethernet; }
const char *crystal_network_type_name(CrystalNetworkType type)
{
    return type == CrystalNetworkType::Ethernet ? "Ethernet" :
           type == CrystalNetworkType::WiFi ? "WiFi" : "Offline";
}

// 2026-11-10 10:09:00 HKT. Only app source files substitute their time() calls.
extern "C" time_t crystal_capture_time(time_t *out)
{
    const time_t value = 1794276540;
    if (out != nullptr) *out = value;
    return value;
}

bool crystal_time_format_24() { return true; }
// Screenshot runner injects a sampled reading; no network is performed.
#include "weather_request.hpp"
#include "lvgl.h"
int64_t esp_timer_get_time() { return static_cast<int64_t>(lv_tick_get()) * 1000; }
uint32_t crystal_network_revision() { return 0; }
CrystalLocationSnapshot crystal_location_snapshot() { return {22.3193, 114.1694, "Hong Kong", true, true, 1}; }
WeatherRequest::~WeatherRequest() = default;
bool WeatherRequest::start() { return false; }
void WeatherRequest::cancel() {}
bool WeatherRequest::poll(CrystalWeatherReading &) { return false; }
void crystal_stopwatch_set_running(bool) {}
uint8_t crystal_brightness_level() { return 72; }
void crystal_brightness_set(uint8_t) {}
bool crystal_power_saving_enabled() { return false; }
void crystal_power_set_saving(bool) {}
bool crystal_power_auto_dim_enabled() { return true; }
void crystal_power_set_auto_dim(bool) {}
bool crystal_hal_set_volume(int) { return true; }
int crystal_hal_get_volume() { return 60; }
static CrystalTimerState timer{};
CrystalTimerState crystal_timer_state() { return timer; }
void crystal_timer_reset() { timer = {}; }
bool crystal_timer_start(uint32_t duration)
{
    timer = {true, false, duration, duration, crystal_capture_time(nullptr) + duration};
    return true;
}
bool crystal_timer_pause() { timer.running = false; timer.paused = true; return true; }
bool crystal_timer_resume() { timer.running = true; timer.paused = false; return true; }
bool crystal_timer_restore(time_t end, uint32_t remaining, uint32_t duration)
{
    timer = {end > 0, remaining > 0, duration, remaining, end};
    return true;
}
