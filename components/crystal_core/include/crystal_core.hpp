/* SPDX-License-Identifier: MIT */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

enum crystal_evt_t : uint8_t {
    UI_EVT_WIFI_GOT_IP,
    UI_EVT_WIFI_DISCONNECTED,
    UI_EVT_WIFI_SCAN_DONE,
    UI_EVT_WIFI_CONNECT_FAILED,
    UI_EVT_TIME_SYNCED,
    UI_EVT_BATTERY,
    UI_EVT_TOAST,
};

bool crystal_ui_post(crystal_evt_t type, const void *data = nullptr, size_t len = 0);

// Loads timezone and initializes system time from the hardware RTC before UI startup.
void crystal_time_init();

// Applies local time to both the system clock and the PCF85063 RTC.
bool crystal_time_set(const struct tm *local_time);

using crystal_clock_update_cb_t = void (*)(void *context, int hour, int minute, bool is_pm);

// Must be called while the LVGL lock is held, after the phone has begun.
bool crystal_core_init(void *display, crystal_clock_update_cb_t clock_update, void *clock_context);
