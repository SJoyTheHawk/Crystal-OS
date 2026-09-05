/* SPDX-License-Identifier: MIT */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(CRYSTAL_NETWORK_EVENT);
enum crystal_network_event_id_t {
    CRYSTAL_NETWORK_CONNECTED,
    CRYSTAL_NETWORK_DISCONNECTED,
};

enum crystal_evt_t : uint8_t {
    UI_EVT_WIFI_GOT_IP,
    UI_EVT_WIFI_DISCONNECTED,
    UI_EVT_WIFI_SCAN_DONE,
    UI_EVT_WIFI_CONNECT_FAILED,
    UI_EVT_WIFI_CONNECTING,
    UI_EVT_TIME_SYNCED,
    UI_EVT_TIMER_EXPIRED,
    UI_EVT_BATTERY,
    UI_EVT_TOAST,
    UI_EVT_WEATHER,
};

struct CrystalWeatherReading {
    int16_t temperature_c10;
    uint8_t humidity;
    uint8_t weather_code;
    uint16_t wind_kmh10;
    int32_t fetched_at;
    bool success;
    char city[24];
};

struct CrystalTimerState {
    bool running;
    bool paused;
    uint32_t duration_seconds;
    uint32_t remaining_seconds;
    time_t end_at;
};

bool crystal_ui_post(crystal_evt_t type, const void *data = nullptr, size_t len = 0);
void crystal_shell_weather_event(const CrystalWeatherReading *reading);
// Requests one throttled Open-Meteo fetch on the crystal service task.
void crystal_weather_request();

// Called by the gesture arbiter on touch-down. Returns true exactly when that
// touch must wake the display without being delivered as an app interaction.
bool crystal_core_consume_wake_touch();

// Loads timezone and initializes system time from the hardware RTC before UI startup.
void crystal_time_init();

// Applies local time to both the system clock and the PCF85063 RTC.
bool crystal_time_set(const struct tm *local_time);

bool crystal_timer_start(uint32_t duration_seconds);
bool crystal_timer_restore(time_t end_at, uint32_t paused_remaining, uint32_t duration_seconds);
bool crystal_timer_pause();
bool crystal_timer_resume();
void crystal_timer_reset();
CrystalTimerState crystal_timer_state();
void crystal_stopwatch_set_running(bool running);

using crystal_clock_update_cb_t = void (*)(void *context, int hour, int minute, bool is_pm);
using crystal_connectivity_update_cb_t = void (*)(void *context, bool wifi_connected);
using crystal_battery_update_cb_t = void (*)(void *context, int percent, bool charging);

// Must be called while the LVGL lock is held, after the phone has begun.
bool crystal_core_init(void *display, crystal_clock_update_cb_t clock_update,
                       crystal_connectivity_update_cb_t connectivity_update,
                       crystal_battery_update_cb_t battery_update, void *status_context);
