/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

#include "manage_apps_page.h"

#include <algorithm>
#include <cstdio>
#include <vector>
#include "crystal_registry.hpp"
#include "esp_log.h"

// The standalone host renderer supplies registry stubs and may not provide
// the optional live-apply symbol. The firmware's strong implementation
// overrides this weak fallback at link time.
bool crystal_registry_apply_changes(void) __attribute__((weak));
bool crystal_registry_apply_changes(void) { return true; }

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
bool s_pending_restart = false;
lv_obj_t *s_clear_dialog = nullptr;
const CrystalAppEntry *s_clear_target = nullptr;
lv_obj_t *s_clear_content = nullptr;
crystal_manage_apps_clear_cb_t s_clear_callback = nullptr;

lv_obj_t *app_button(lv_obj_t *row, const char *text, lv_coord_t width, bool disabled);
void set_notice(lv_obj_t *content, const char *message, bool error);

void close_clear_dialog()
{
    if (s_clear_dialog != nullptr) {
        lv_obj_del(s_clear_dialog);
        s_clear_dialog = nullptr;
    }
    s_clear_target = nullptr;
    s_clear_content = nullptr;
}

void content_deleted(lv_event_t *event)
{
    if (s_clear_content == lv_event_get_target(event)) close_clear_dialog();
}

void clear_data_cancelled(lv_event_t *)
{
    ESP_LOGI("manage_apps", "Clear data cancelled");
    close_clear_dialog();
}

void clear_data_confirmed(lv_event_t *)
{
    const CrystalAppEntry *target = s_clear_target;
    lv_obj_t *content = s_clear_content;
    bool success = false;
    if (target != nullptr) {
        ESP_LOGI("manage_apps", "Clearing data for %s", target->id);
        success = s_clear_callback != nullptr && s_clear_callback(target);
        if (success) ESP_LOGI("manage_apps", "Successfully cleared data for %s", target->id);
        else ESP_LOGE("manage_apps", "Failed to clear data for %s", target->id);
    }
    close_clear_dialog();
    if (content != nullptr) {
        set_notice(content, success ? "App data cleared." :
            "Could not fully clear app data.\nSome data may be removed. Please try again.", !success);
    }
}

void show_clear_confirmation(lv_obj_t *content, const CrystalAppEntry *entry)
{
    if (s_clear_dialog != nullptr || s_drag.row != nullptr || content == nullptr || entry == nullptr) return;
    s_clear_target = entry;
    s_clear_content = content;
    s_clear_dialog = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_clear_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_clear_dialog, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_clear_dialog, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_clear_dialog, 0, 0);
    lv_obj_add_flag(s_clear_dialog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_clear_dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(s_clear_dialog);
    lv_obj_set_size(card, 380, 250);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1b2028), 0);
    lv_obj_center(card);
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "Clear App Data");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_pos(title, 0, 0);
    lv_obj_t *message = lv_label_create(card);
    char text[128];
    snprintf(text, sizeof(text), "Clear saved data for %s?\nThe app will close and reset.\n\nThis cannot be undone.", entry->id);
    lv_label_set_text(message, text);
    lv_obj_set_width(message, 340);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(message, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(message, lv_color_hex(0xcbd5e1), 0);
    lv_obj_set_pos(message, 0, 38);
    lv_obj_t *cancel = app_button(card, "Cancel", 140, false);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(cancel, clear_data_cancelled, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *confirm = app_button(card, "Clear Data", 140, false);
    lv_obj_align(confirm, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(confirm, lv_color_hex(0xb42318), 0);
    lv_obj_set_style_bg_color(confirm, lv_color_hex(0x8f1c13), LV_STATE_PRESSED);
    lv_obj_add_event_cb(confirm, clear_data_confirmed, LV_EVENT_CLICKED, nullptr);
}

void clear_btn_cb(lv_event_t *event)
{
    auto *entry = static_cast<const CrystalAppEntry *>(lv_event_get_user_data(event));
    show_clear_confirmation(lv_obj_get_parent(lv_obj_get_parent(lv_event_get_target(event))), entry);
}

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
    if (s_clear_dialog != nullptr || s_drag.row != nullptr || crystal_registry_catalog_count() < 2) return;
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
    else {
        s_pending_restart = true;
        set_notice(content, "Order saved. Restart to apply on the launcher.", false);
    }
    refresh_row_order(content);
}

void handle_drag_drop(lv_event_t *event) { handle_drag_end(event, true); }
void handle_drag_cancel(lv_event_t *event) { handle_drag_end(event, false); }

void toggle_btn_cb(lv_event_t *event)
{
    if (s_clear_dialog != nullptr) return;
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
    if (!enabled) {
        if (crystal_registry_apply_changes()) {
            ESP_LOGI("manage_apps", "Applied changes live, %s should appear in launcher", entry->id);
        } else {
            ESP_LOGI("manage_apps", "Could not apply live, changes take effect on next boot");
            s_pending_restart = true;
            set_notice(content, "Saved, but the app will appear\nafter restart.", true);
        }
    } else {
        s_pending_restart = true;
        set_notice(content, "App disabled. Restart to apply the change.\nEnable changes apply immediately.", false);
    }
    // Refresh just this row: preserve scroll/focus and the event target. No
    // asynchronous callbacks or cached widget pointers can outlive the page.
    lv_label_set_text(lv_obj_get_child(button, 0), enabled ? "Install" : "Installed");
    auto *clear = lv_obj_get_child(row, 3);
    if (enabled && clear) lv_obj_del(clear);
    if (!enabled && !clear) {
        auto *clear_button = app_button(row, "Clear", 64, false);
        lv_obj_add_event_cb(clear_button, clear_btn_cb, LV_EVENT_CLICKED,
                            const_cast<CrystalAppEntry *>(entry));
    }
}
}

bool crystal_manage_apps_page_has_pending_restart()
{
    return s_pending_restart;
}

void crystal_manage_apps_page_clear_pending_restart()
{
    s_pending_restart = false;
}

bool crystal_manage_apps_page_confirmation_open()
{
    return s_clear_dialog != nullptr;
}

bool crystal_manage_apps_page_dismiss_confirmation()
{
    if (s_clear_dialog == nullptr) return false;
    close_clear_dialog();
    return true;
}

void crystal_manage_apps_page_build(lv_obj_t *content, crystal_manage_apps_clear_cb_t clear_callback)
{
    if (content == nullptr) return;
    close_clear_dialog();
    s_clear_callback = clear_callback;
    lv_obj_remove_event_cb(content, content_deleted);
    lv_obj_add_event_cb(content, content_deleted, LV_EVENT_DELETE, nullptr);
    lv_obj_clean(content);
    lv_obj_set_style_pad_row(content, 8, 0);

    lv_obj_t *note = lv_label_create(content);
    lv_label_set_text(note, "Drag to reorder. Enable applies now.\nDisable needs restart; Clear resets data.");
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
        if (enabled) {
            auto *clear = app_button(row, "Clear", 64, false);
            lv_obj_add_event_cb(clear, clear_btn_cb, LV_EVENT_CLICKED,
                                const_cast<CrystalAppEntry *>(app.entry));
        }
    }
    if (apps.empty()) {
        lv_obj_t *empty = lv_label_create(content);
        lv_label_set_text(empty, "No apps in this firmware.");
        lv_obj_set_style_text_color(empty, lv_color_hex(0xf2f4f7), 0);
    }
    ESP_LOGI("manage_apps", "Showing %zu catalog apps (restart to apply)", apps.size());
}
