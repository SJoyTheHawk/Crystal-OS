/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdint.h>
#include "crystal_core.hpp"
#include "lvgl.h"

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

// Shell-owned text input overlay. The viewport is clipped to the free band
// while open and scrolled only when the focused field would be obscured.
bool crystal_keyboard_show(lv_obj_t *field, lv_obj_t *viewport);
void crystal_keyboard_hide();
bool crystal_keyboard_is_open();
lv_coord_t crystal_keyboard_top();
// Top of the reserved keyboard band whether or not the keyboard is open. Use
// this to lay out dialogs that must never sit under the keyboard.
lv_coord_t crystal_keyboard_reserved_top();

// Call this when the frontmost full-screen layer changes -- a card switch, or a
// settings page opening or closing. The keyboard holds pointers into the layer
// that owned its field (s_field, s_viewport, and that viewport's saved height and
// scroll flag), so it cannot survive a switch to a different layer.
//
// This is deliberately NOT called for overlays. Quick Settings slides over the
// keyboard and leaves it running underneath (DESIGN.md 1), so the panel is not a
// front-layer change. The distinction is whether the outgoing layer is being
// replaced or merely covered.
void crystal_shell_front_layer_changed();

// Fires whenever the keyboard opens or closes, including the paths that close it
// without the caller asking. Only one listener is held; pass nullptr to clear it.
using crystal_keyboard_state_cb_t = void (*)(bool open, void *user_data);
void crystal_keyboard_set_state_cb(crystal_keyboard_state_cb_t callback, void *user_data);

// Called on the LVGL task when the WiFi adapter posts a state/scan event.
void crystal_shell_wifi_event(uint8_t event);
void crystal_shell_weather_event(const CrystalWeatherReading *reading);
