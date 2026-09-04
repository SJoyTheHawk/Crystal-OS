/* SPDX-License-Identifier: MIT */
#pragma once

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
