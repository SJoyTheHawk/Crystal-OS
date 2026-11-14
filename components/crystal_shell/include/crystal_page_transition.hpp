/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once
#include "lvgl.h"

// Call under the LVGL lock. A zero page_transition_ms switches synchronously
// with no animation or retained outgoing page. Nonzero timings retain at most
// one retiring page; starting another navigation settles it immediately.
void crystal_page_transition_settle();
// Hide the covered page once the incoming page is opaque (immediately at 0ms).
void crystal_page_transition_enter(lv_obj_t *page, lv_obj_t *covered_page = nullptr);
void crystal_page_transition_retire(lv_obj_t *page);

// Optional backdrop for translucent transitions; immediate Settings skips this.
// Binding transfers backdrop cleanup to the Settings root page lifetime.
lv_obj_t *crystal_page_transition_capture_backdrop(const lv_area_t &area);
void crystal_page_transition_bind_backdrop(lv_obj_t *page, lv_obj_t *backdrop);
