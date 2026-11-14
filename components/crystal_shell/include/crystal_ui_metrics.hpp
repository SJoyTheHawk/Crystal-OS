/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once
#include "lvgl.h"

// Install with the display lock held. Reports redraw work, not panel VSYNC FPS.
void crystal_ui_metrics_start(lv_disp_t *display);
