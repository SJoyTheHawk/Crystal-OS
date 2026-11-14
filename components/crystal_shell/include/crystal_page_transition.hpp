/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once
#include "lvgl.h"

// Call under the LVGL lock. At most one retiring page is retained. Starting
// another navigation settles it immediately, so repeated Back cannot leak pages.
void crystal_page_transition_settle();
// Keep the covered page visible until the incoming opacity reaches full cover.
void crystal_page_transition_enter(lv_obj_t *page, lv_obj_t *covered_page = nullptr);
void crystal_page_transition_retire(lv_obj_t *page);

// Preserve the outgoing app beneath the translucent Settings transition.
// Binding transfers backdrop cleanup to the Settings root page lifetime.
lv_obj_t *crystal_page_transition_capture_backdrop(const lv_area_t &area);
void crystal_page_transition_bind_backdrop(lv_obj_t *page, lv_obj_t *backdrop);
