/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once
#include "lvgl.h"

// Install with the display lock held. Reports redraw work, not panel VSYNC FPS.
// Also restores the saved CPU/FPS overlay visibility on this display.
void crystal_ui_metrics_start(lv_disp_t *display);

bool crystal_resources_monitor_enabled();
// Call with the display lock held. Applies immediately and saves the preference.
void crystal_resources_monitor_set_enabled(bool enabled);
