/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

#include "lvgl.h"

// Populate a shell-owned Settings viewport on the LVGL task. The shell owns
// header/back navigation and deletes all children when the page is popped.
// Install toggles persist registry state for the next boot. Clear/reorder
// remain previews; at least one app must stay enabled for shell startup.
void crystal_manage_apps_page_build(lv_obj_t *content);

// The shell uses this state to warn before leaving the page. Changes are
// already persisted, but the launcher only reads them during the next boot.
bool crystal_manage_apps_page_has_pending_restart();
void crystal_manage_apps_page_clear_pending_restart();
