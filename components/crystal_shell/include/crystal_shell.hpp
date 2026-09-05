/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdint.h>
#include "crystal_core.hpp"

class ESP_Brookesia_Phone;

enum class CrystalGestureOwner {
    None,
    AppSwitch,
    QuickSettings,
    App,
};

bool crystal_shell_init(ESP_Brookesia_Phone *phone);

CrystalGestureOwner crystal_shell_gesture_owner();
void crystal_shell_set_quick_settings_open(bool open);
void crystal_shell_set_keyboard_open(bool open);
void crystal_shell_set_settings_open(bool open);
void crystal_shell_set_modal_open(bool open);

// Called on the LVGL task when the WiFi adapter posts a state/scan event.
void crystal_shell_wifi_event(uint8_t event);
void crystal_shell_weather_event(const CrystalWeatherReading *reading);
