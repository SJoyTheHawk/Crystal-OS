/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once
#include <stdint.h>
#include "esp_brookesia.hpp"

// Caller owns the object; Brookesia copies it on registration.
ESP_Brookesia_PhoneStylesheet_t *crystal_stylesheet_create_dark();

struct CrystalVisualStyle {
    uint32_t background;
    uint32_t surface;
    uint32_t elevated;
    uint32_t text;
    uint32_t secondary;
    uint32_t muted;
    uint32_t accent;
    uint32_t success;
    uint32_t error;
    uint32_t page_transition_ms;
    uint32_t boot_ready_hold_ms;
    uint32_t boot_fade_ms;
};
const CrystalVisualStyle &crystal_visual_style();
