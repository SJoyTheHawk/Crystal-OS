/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

// show() requires the display lock and a running LVGL display. Updates copy
// their message into a single-slot mailbox; they never touch LVGL cross-task.
bool crystal_boot_screen_show();
void crystal_boot_screen_update(const char *message, int progress);
void crystal_boot_screen_finish();
void crystal_boot_screen_fail(const char *message);
bool crystal_boot_screen_active();
