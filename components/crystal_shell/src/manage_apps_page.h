/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

#include "lvgl.h"

// Populate a shell-owned Settings viewport on the LVGL task. The shell owns
// header/back navigation and deletes all children when the page is popped.
// Install/reorder persist for next boot. Confirmed Clear resets saved data
// immediately; at least one app must stay enabled for shell startup.
struct CrystalAppEntry;
using crystal_manage_apps_clear_cb_t = bool (*)(const CrystalAppEntry *entry);
void crystal_manage_apps_page_build(lv_obj_t *content,
                                    crystal_manage_apps_clear_cb_t clear_callback = nullptr);
bool crystal_manage_apps_page_confirmation_open();
bool crystal_manage_apps_page_dismiss_confirmation();

// The shell uses this state to warn before leaving the page. Changes are
// already persisted, but the launcher only reads them during the next boot.
bool crystal_manage_apps_page_has_pending_restart();
void crystal_manage_apps_page_clear_pending_restart();
