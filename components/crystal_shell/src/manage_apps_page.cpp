/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

#include "manage_apps_page.h"

#include <algorithm>
#include <vector>
#include "crystal_registry.hpp"
#include "esp_log.h"

namespace {
struct AppRow {
    const CrystalAppEntry *entry;
    uint16_t slot;
};

struct DragState {
    lv_obj_t *row = nullptr;
    lv_obj_t *placeholder = nullptr;
    lv_obj_t *content = nullptr;
    lv_obj_t *handle = nullptr;
    lv_coord_t left = 0;
    lv_coord_t grab_offset_y = 0;
    lv_coord_t width = 0;
    lv_coord_t height = 0;
    std::vector<AppRow> original;
};

DragState s_drag;

lv_obj_t *app_button(lv_obj_t *row, const char *text, lv_coord_t width, bool disabled)
{
    lv_obj_t *button = lv_btn_create(row);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, width, 40);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x1769aa), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x125486), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(button, lv_color_white(), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x28323e), LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_DISABLED);
    lv_obj_set_style_opa(button, LV_OPA_COVER, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(button, lv_color_hex(0x91a0b3), LV_STATE_DISABLED);
    if (disabled) lv_obj_add_state(button, LV_STATE_DISABLED);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_center(label);
    return button;
}

void set_notice(lv_obj_t *content, const char *message, bool error)
{
    auto *note = lv_obj_get_child(content, 0);
    lv_label_set_text(note, message);
    lv_obj_set_style_text_color(note, lv_color_hex(error ? 0xf87171 : 0x91a0b3), 0);
}

void row_move_to(const lv_point_t &point)
{
    if (s_drag.row == nullptr) return;
    lv_obj_set_pos(s_drag.row, s_drag.left, point.y - s_drag.grab_offset_y);
}

void drag_object_deleted(lv_event_t *event)
{
    if (s_drag.row == nullptr) return;
    auto *target = lv_event_get_target(event);
    if (target != s_drag.placeholder && target != s_drag.handle) return;
    auto *row = s_drag.row;
    auto *placeholder = s_drag.placeholder;
    s_drag = {};
    // Cleaning the page must also dispose of its lifted row on the top layer.
    if (target == placeholder) lv_obj_del(row);
    else lv_obj_del(placeholder);
}

void handle_drag_start(lv_event_t *event)
{
    if (s_drag.row != nullptr || crystal_registry_catalog_count() < 2) return;
    auto *handle = lv_event_get_target(event);
    auto *row = lv_obj_get_parent(handle);
    auto *content = lv_obj_get_parent(row);
    const auto *entry = static_cast<const CrystalAppEntry *>(lv_obj_get_user_data(row));
    if (entry == nullptr) return;
    lv_obj_update_layout(content);
    lv_area_t row_box, handle_box;
    lv_obj_get_coords(row, &row_box);
    lv_obj_get_coords(handle, &handle_box);
    lv_point_t point = {static_cast<lv_coord_t>((handle_box.x1 + handle_box.x2) / 2),
                        static_cast<lv_coord_t>((handle_box.y1 + handle_box.y2) / 2)};
    auto *indev = lv_indev_get_act();
    if (indev != nullptr) lv_indev_get_point(indev, &point);
    const uint32_t row_index = lv_obj_get_index(row);
    std::vector<AppRow> original;
    for (uint32_t i = 1; i < lv_obj_get_child_cnt(content); ++i) {
        auto *candidate = lv_obj_get_child(content, i);
        const auto *candidate_entry = static_cast<const CrystalAppEntry *>(lv_obj_get_user_data(candidate));
        if (candidate_entry != nullptr) {
            original.push_back({candidate_entry,
                crystal_registry_slot(candidate_entry->id, candidate_entry->default_slot)});
        }
    }
    const lv_coord_t width = lv_obj_get_style_width(row, 0);
    const lv_coord_t height = lv_obj_get_style_height(row, 0);
    const lv_coord_t pixel_width = lv_obj_get_width(row);
    const lv_coord_t pixel_height = lv_obj_get_height(row);
    auto *placeholder = lv_obj_create(content);
    lv_obj_remove_style_all(placeholder);
    lv_obj_set_size(placeholder, pixel_width, pixel_height);
    lv_obj_clear_flag(placeholder, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(placeholder, 8, 0);
    lv_obj_set_style_bg_color(placeholder, lv_color_hex(0x1769aa), 0);
    lv_obj_set_style_bg_opa(placeholder, LV_OPA_20, 0);
    lv_obj_set_style_border_width(placeholder, 2, 0);
    lv_obj_set_style_border_color(placeholder, lv_color_hex(0x1769aa), 0);
    lv_obj_add_event_cb(placeholder, drag_object_deleted, LV_EVENT_DELETE, nullptr);
    lv_obj_move_to_index(placeholder, row_index);
    lv_obj_set_parent(row, lv_layer_top());
    lv_obj_set_size(row, pixel_width, pixel_height);
    s_drag = {row, placeholder, content, handle, row_box.x1,
              static_cast<lv_coord_t>(point.y - row_box.y1), width, height, std::move(original)};
    row_move_to(point);
    lv_obj_update_layout(content);
    ESP_LOGI("manage_apps", "Dragging %s", entry->id);
}

void handle_drag_move(lv_event_t *event)
{
    if (s_drag.row == nullptr || lv_event_get_target(event) != s_drag.handle) return;
    auto *indev = lv_indev_get_act();
    if (indev == nullptr) return;
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    row_move_to(point);
    lv_obj_update_layout(s_drag.content);
    const lv_coord_t center_y = point.y - s_drag.grab_offset_y + lv_obj_get_height(s_drag.row) / 2;
    uint32_t index = lv_obj_get_index(s_drag.placeholder);
    // Crossing a neighbor moves the gap and shifts that row immediately.
    while (index > 1) {
        lv_area_t box;
        lv_obj_get_coords(lv_obj_get_child(s_drag.content, index - 1), &box);
        if (center_y >= (box.y1 + box.y2) / 2) break;
        lv_obj_move_to_index(s_drag.placeholder, --index);
        lv_obj_update_layout(s_drag.content);
    }
    while (index + 1 < lv_obj_get_child_cnt(s_drag.content)) {
        lv_area_t box;
        lv_obj_get_coords(lv_obj_get_child(s_drag.content, index + 1), &box);
        if (center_y <= (box.y1 + box.y2) / 2) break;
        lv_obj_move_to_index(s_drag.placeholder, ++index);
        lv_obj_update_layout(s_drag.content);
    }
}

void refresh_row_order(lv_obj_t *content)
{
    std::vector<lv_obj_t *> rows;
    // Catalog order supplies the stable tie-breaker, regardless of current UI order.
    for (size_t i = 0; i < crystal_registry_catalog_count(); ++i) {
        const auto *entry = crystal_registry_catalog_entry(i);
        for (uint32_t j = 1; j < lv_obj_get_child_cnt(content); ++j) {
            auto *row = lv_obj_get_child(content, j);
            if (lv_obj_get_user_data(row) == entry) rows.push_back(row);
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [](lv_obj_t *a, lv_obj_t *b) {
        const auto *ea = static_cast<const CrystalAppEntry *>(lv_obj_get_user_data(a));
        const auto *eb = static_cast<const CrystalAppEntry *>(lv_obj_get_user_data(b));
        return crystal_registry_slot(ea->id, ea->default_slot) < crystal_registry_slot(eb->id, eb->default_slot);
    });
    for (size_t i = 0; i < rows.size(); ++i) lv_obj_move_to_index(rows[i], i + 1);
    lv_obj_update_layout(content);
}

void handle_drag_end(lv_event_t *event, bool dropped)
{
    if (s_drag.row == nullptr || lv_event_get_target(event) != s_drag.handle) return;
    if (dropped) handle_drag_move(event);
    auto drag = std::move(s_drag);
    s_drag = {};
    auto *content = drag.content;
    const uint32_t final_index = lv_obj_get_index(drag.placeholder);
    lv_obj_set_parent(drag.row, content);
    lv_obj_del(drag.placeholder);
    lv_obj_move_to_index(drag.row, final_index);
    lv_obj_set_size(drag.row, drag.width, drag.height);
    if (!dropped) {
        refresh_row_order(content);
        return;
    }
    std::vector<const CrystalAppEntry *> entries;
    for (uint32_t i = 1; i < lv_obj_get_child_cnt(content); ++i) {
        entries.push_back(static_cast<const CrystalAppEntry *>(
            lv_obj_get_user_data(lv_obj_get_child(content, i))));
    }
    bool saved = entries.size() == drag.original.size();
    bool changed = false;
    bool unique_slots = true;
    for (size_t i = 0; saved && i < entries.size(); ++i) {
        if (entries[i] != drag.original[i].entry) changed = true;
        if (i > 0 && drag.original[i].slot <= drag.original[i - 1].slot) unique_slots = false;
    }
    if (saved && !changed) {
        lv_obj_update_layout(content);
        return;
    }
    // Preserve slot spacing. Ties need unique ranks to survive the boot sort.
    for (size_t i = 0; saved && i < entries.size(); ++i) {
        const auto *entry = entries[i];
        if (entry == nullptr || i > UINT16_MAX) { saved = false; break; }
        const uint16_t slot = unique_slots ? drag.original[i].slot : static_cast<uint16_t>(i);
        if (crystal_registry_slot(entry->id, entry->default_slot) != slot &&
            !crystal_registry_set_slot(entry->id, slot)) saved = false;
    }
    bool restored = true;
    if (!saved) {
        // Restore by app identity, including a failed write that changed data.
        for (const auto &app : drag.original) {
            if (crystal_registry_slot(app.entry->id, app.entry->default_slot) != app.slot &&
                !crystal_registry_set_slot(app.entry->id, app.slot)) restored = false;
        }
    }
    if (!saved) {
        const char *message = restored
            ? "Could not save order. Previous order restored.\nPlease try again."
            : "Order could not be fully saved or restored.\nCheck the list and try again.";
        set_notice(content, message, true);
    }
    else set_notice(content, "Order saved. Restart to apply on the launcher.\nClear is coming later.", false);
    refresh_row_order(content);
}

void handle_drag_drop(lv_event_t *event) { handle_drag_end(event, true); }
void handle_drag_cancel(lv_event_t *event) { handle_drag_end(event, false); }

void toggle_btn_cb(lv_event_t *event)
{
    const auto *entry = static_cast<const CrystalAppEntry *>(lv_event_get_user_data(event));
    auto *button = lv_event_get_target(event);
    auto *row = lv_obj_get_parent(button);
    auto *content = lv_obj_get_parent(row);
    const bool enabled = crystal_registry_enabled(entry->id, entry->default_enabled);
    if (enabled) {
        size_t enabled_count = 0;
        for (size_t i = 0; i < crystal_registry_catalog_count(); ++i) {
            const auto *candidate = crystal_registry_catalog_entry(i);
            if (candidate && crystal_registry_enabled(candidate->id, candidate->default_enabled)) ++enabled_count;
        }
        // The boot shell requires at least one installed app. Keep Settings
        // reachable instead of persisting a configuration that cannot boot.
        if (enabled_count <= 1) {
            set_notice(content, "Keep at least one app installed.\nInstall another app before removing this one.", true);
            return;
        }
    }
    ESP_LOGI("manage_apps", "Toggling %s: %d -> %d", entry->id, enabled, !enabled);
    if (!crystal_registry_set_enabled(entry->id, !enabled)) {
        set_notice(content, "Could not save the change. Please try again.\nThe app selection has not changed.", true);
        return;
    }
    // Refresh just this row: preserve scroll/focus and the event target. No
    // asynchronous callbacks or cached widget pointers can outlive the page.
    lv_label_set_text(lv_obj_get_child(button, 0), enabled ? "Install" : "Installed");
    auto *clear = lv_obj_get_child(row, 3);
    if (enabled && clear) lv_obj_del(clear);
    if (!enabled && !clear) app_button(row, "Clear", 64, true);
    set_notice(content, "Saved. Restart to apply app changes.\nDrag handles to reorder. Clear is coming later.", false);
}
}

void crystal_manage_apps_page_build(lv_obj_t *content)
{
    if (content == nullptr) return;
    lv_obj_clean(content);
    lv_obj_set_style_pad_row(content, 8, 0);

    lv_obj_t *note = lv_label_create(content);
    lv_label_set_text(note, "Tap Installed to uninstall. Drag handles to reorder.\nRestart applies changes; Clear is coming later.");
    lv_obj_set_width(note, LV_PCT(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(note, lv_color_hex(0x91a0b3), 0);

    std::vector<AppRow> apps;
    const size_t count = crystal_registry_catalog_count();
    apps.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const auto *entry = crystal_registry_catalog_entry(i);
        if (entry != nullptr) {
            apps.push_back({entry, crystal_registry_slot(entry->id, entry->default_slot)});
        }
    }
    std::stable_sort(apps.begin(), apps.end(), [](const AppRow &a, const AppRow &b) {
        return a.slot < b.slot;
    });

    for (const auto &app : apps) {
        const bool enabled = crystal_registry_enabled(app.entry->id, app.entry->default_enabled);
        lv_obj_t *row = lv_obj_create(content);
        lv_obj_set_size(row, LV_PCT(100), 62);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1b2028), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_pad_all(row, 10, 0);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_user_data(row, const_cast<CrystalAppEntry *>(app.entry));

        lv_obj_t *handle = lv_label_create(row);
        // LVGL's built-in icon font has this glyph; Unicode U+2261 is absent
        // from the firmware's Montserrat fonts.
        lv_label_set_text(handle, LV_SYMBOL_BARS);
        lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_clear_flag(handle, LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_set_ext_click_area(handle, 12);
        lv_obj_set_width(handle, 24);
        lv_obj_set_style_text_font(handle, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(handle, lv_color_hex(0x91a0b3), 0);
        lv_obj_add_event_cb(handle, handle_drag_start, LV_EVENT_PRESSED, nullptr);
        lv_obj_add_event_cb(handle, handle_drag_move, LV_EVENT_PRESSING, nullptr);
        lv_obj_add_event_cb(handle, handle_drag_drop, LV_EVENT_RELEASED, nullptr);
        lv_obj_add_event_cb(handle, handle_drag_cancel, LV_EVENT_PRESS_LOST, nullptr);
        // Also fires when Back/Home deletes the page or a rebuild cleans rows.
        lv_obj_add_event_cb(handle, drag_object_deleted, LV_EVENT_DELETE, nullptr);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, app.entry->id);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, 0);
        lv_obj_set_flex_grow(name, 1);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(0xf2f4f7), 0);

        auto *toggle = app_button(row, enabled ? "Installed" : "Install", 100, false);
        lv_obj_add_event_cb(toggle, toggle_btn_cb, LV_EVENT_CLICKED,
                            const_cast<CrystalAppEntry *>(app.entry));
        if (enabled) app_button(row, "Clear", 64, true);
    }
    if (apps.empty()) {
        lv_obj_t *empty = lv_label_create(content);
        lv_label_set_text(empty, "No apps in this firmware.");
        lv_obj_set_style_text_color(empty, lv_color_hex(0xf2f4f7), 0);
    }
    ESP_LOGI("manage_apps", "Showing %zu catalog apps (restart to apply)", apps.size());
}
