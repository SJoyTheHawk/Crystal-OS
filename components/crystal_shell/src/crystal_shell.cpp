/* SPDX-License-Identifier: MIT */

#include "crystal_shell.hpp"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <vector>

#include "crystal_app.hpp"
#include "crystal_core.hpp"
#include "crystal_hal.hpp"
#include "crystal_registry.hpp"
#include "esp_brookesia.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "crystal_shell";

void wifi_page_open();
void wifi_tile_text(char *out, size_t size);

namespace {
constexpr int kTopBand = 20;
// Crop the captured app area to 90% x 90% about its centre, then store it at
// 1/kSnapshotScaleDivisor of the app's resolution. On this panel the observed
// 480x440 app area produces a 240x220 snapshot (~103 KiB).
constexpr uint32_t kSnapshotCropPercent = 90;
constexpr uint32_t kSnapshotScaleDivisor = 2;
constexpr uint32_t kCrossoverCommitPercent = 50;
constexpr uint32_t kTransitionMs = 180;
constexpr uint32_t kCrossoverSettleMs = 250;
constexpr lv_coord_t kQuickCell = 62;
constexpr lv_coord_t kQuickGap = 10;
constexpr lv_coord_t kQuickPad = 14;
constexpr lv_coord_t kQuickMargin = 12;
constexpr lv_coord_t kQuickGapTop = 8;
constexpr lv_coord_t kQuickCornerWidth = 120;
constexpr lv_coord_t kQuickPanelSize = 4 * kQuickCell + 3 * kQuickGap + 2 * kQuickPad;
constexpr uint32_t kQuickAnimMs = 200;
// An lv_anim ready callback runs inside lv_timer_handler, before the display is
// refreshed, so the animation's final frame is still unpainted at that moment.
// Doing the Brookesia switch there costs the user that frame and makes the
// commit look like it overlaps the slide. One frame of slack lets each stage
// reach the panel before the next one starts.
constexpr uint32_t kCommitStageMs = 20;
constexpr char kCurrentCardKey[] = "shell.card";
constexpr char kPreviewPathPrefix[] = "/spiffs/crystal_preview_";
constexpr uint32_t kPreviewMagic = 0x43525056;

ESP_Brookesia_Phone *s_phone = nullptr;
size_t s_current_index = 0;
bool s_switching = false;
bool s_quick_settings_open = false;
bool s_keyboard_open = false;
bool s_settings_open = false;
bool s_modal_open = false;
bool s_swallow_wake_touch = false;
CrystalGestureOwner s_gesture_owner = CrystalGestureOwner::None;
ESP_Brookesia_Gesture *s_gesture = nullptr;
lv_obj_t *s_page_dots = nullptr;
lv_obj_t *s_quick_root = nullptr;
lv_obj_t *s_quick_panel = nullptr;
lv_obj_t *s_quick_brightness = nullptr;
lv_obj_t *s_quick_volume = nullptr;
lv_obj_t *s_quick_wifi = nullptr;
lv_obj_t *s_wifi_dialog = nullptr;
lv_obj_t *s_wifi_page = nullptr;
lv_obj_t *s_wifi_page_list = nullptr;
lv_obj_t *s_wifi_page_status = nullptr;
char s_wifi_selected[33] = {};
char s_wifi_connecting[33] = {};
void (*s_quick_after_close)() = nullptr;
lv_obj_t *s_quick_catcher = nullptr;
lv_coord_t s_quick_y_rest = 0;
lv_coord_t s_quick_y_hidden = 0;
bool s_quick_settling = false;
bool s_quick_closing = false;
static lv_coord_t s_quick_cols[] = {kQuickCell, kQuickCell, kQuickCell, kQuickCell, LV_GRID_TEMPLATE_LAST};
static lv_coord_t s_quick_rows[] = {kQuickCell, kQuickCell, kQuickCell, kQuickCell, LV_GRID_TEMPLATE_LAST};

struct CardPane {
    lv_img_dsc_t *image = nullptr;
};

enum class CardTransitionPhase {
    Idle,
    Dragging,
    Settling,
};

struct CardTransition {
    CardTransitionPhase phase = CardTransitionPhase::Idle;
    lv_obj_t *root = nullptr;
    lv_obj_t *incoming_card = nullptr;
    lv_obj_t *incoming_image = nullptr;
    lv_img_dsc_t *outgoing = nullptr;
    size_t original_index = SIZE_MAX;
    size_t target_index = SIZE_MAX;
    lv_coord_t width = 0;
    lv_coord_t progress = 0;
    int direction = 0;
    bool commit = false;
};

std::vector<CardPane> s_pane_cache;
CardTransition s_card_transition;

bool start_card(size_t index, bool animate = true);
lv_area_t active_app_area();
lv_img_dsc_t *capture_app_area_full();
lv_img_dsc_t *downscale_crop(const lv_img_dsc_t *source, const lv_area_t &crop,
                             lv_coord_t dest_w, lv_coord_t dest_h);

void quick_snapshot_cleanup(lv_timer_t *timer)
{
    lv_obj_t *root = static_cast<lv_obj_t *>(timer->user_data);
    if (root != nullptr) {
        if (root == s_quick_root) {
            s_quick_root = nullptr;
            s_quick_panel = nullptr;
            s_quick_brightness = nullptr;
            s_quick_volume = nullptr;
            s_quick_wifi = nullptr;
            s_wifi_dialog = nullptr;
            s_quick_catcher = nullptr;
            s_quick_settling = false;
            s_quick_closing = false;
        }
        lv_obj_del(root);
        if (s_quick_after_close != nullptr) {
            void (*next)() = s_quick_after_close;
            s_quick_after_close = nullptr;
            next();
        }
    }
    lv_timer_del(timer);
}

void quick_anim_y(void *obj, int32_t y)
{
    lv_obj_set_y(static_cast<lv_obj_t *>(obj), static_cast<lv_coord_t>(y));
}

void quick_anim_ready(lv_anim_t *anim)
{
    if (anim == nullptr || s_quick_root == nullptr) return;
    s_quick_settling = false;
    if (s_quick_closing) {
        s_quick_settings_open = false;
        lv_timer_create(quick_snapshot_cleanup, 1, s_quick_root);
    } else if (s_quick_catcher == nullptr) {
        s_quick_catcher = lv_obj_create(s_quick_root);
        lv_obj_set_size(s_quick_catcher, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
        lv_obj_set_pos(s_quick_catcher, 0, 0);
        lv_obj_set_style_bg_opa(s_quick_catcher, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_quick_catcher, 0, 0);
        lv_obj_add_event_cb(s_quick_catcher, [](lv_event_t *) {
            if (s_gesture_owner == CrystalGestureOwner::QuickSettings || s_quick_settling) return;
            lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, s_quick_panel);
            lv_anim_set_exec_cb(&a, quick_anim_y); lv_anim_set_values(&a, lv_obj_get_y(s_quick_panel), s_quick_y_hidden);
            lv_anim_set_time(&a, kQuickAnimMs); lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
            lv_anim_set_ready_cb(&a, quick_anim_ready); s_quick_settling = true; s_quick_closing = true; lv_anim_start(&a);
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_move_background(s_quick_catcher);
        s_quick_settings_open = true;
    }
}

void quick_set_bar_from_touch(lv_obj_t *bar)
{
    if (bar == nullptr) return;
    lv_point_t point;
    lv_indev_get_point(lv_indev_get_act(), &point);
    lv_area_t area;
    lv_obj_get_coords(bar, &area);
    const int height = LV_MAX(1, lv_area_get_height(&area));
    int value = ((area.y2 - point.y) * 100) / height;
    value = LV_CLAMP(0, value, 100);
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
    if (bar == s_quick_brightness) {
        value = LV_CLAMP(0, value, 95);
        hal().brightness->set(static_cast<uint8_t>(value));
        const uint8_t pct = hal().brightness->get();
        if (hal().storage != nullptr) hal().storage->set("brightness", &pct, sizeof(pct));
    } else if (bar == s_quick_volume) {
        if (crystal_hal_set_volume(value) && hal().storage != nullptr) {
            const uint8_t pct = static_cast<uint8_t>(value);
            hal().storage->set("volume", &pct, sizeof(pct));
        }
    }
}

void quick_bar_event(lv_event_t *event)
{
    if (event == nullptr) return;
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        quick_set_bar_from_touch(static_cast<lv_obj_t *>(lv_event_get_target(event)));
    }
}

void quick_show_message(lv_event_t *)
{
    static const char message[] = "Settings coming in Phase 11";
    (void)crystal_ui_post(UI_EVT_TOAST, message, sizeof(message));
}

void close_quick_settings(void (*then)())
{
    if (s_quick_panel == nullptr) { if (then != nullptr) then(); return; }
    s_quick_after_close = then;
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, s_quick_panel);
    lv_anim_set_exec_cb(&a, quick_anim_y); lv_anim_set_values(&a, lv_obj_get_y(s_quick_panel), s_quick_y_hidden);
    lv_anim_set_time(&a, kQuickAnimMs); lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, quick_anim_ready); s_quick_settling = true; s_quick_closing = true; lv_anim_start(&a);
}

bool create_quick_settings()
{
    if (s_quick_root != nullptr) return true;
    s_quick_root = lv_obj_create(lv_layer_top());
    if (s_quick_root == nullptr) return false;
    lv_obj_set_size(s_quick_root, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
    s_quick_y_rest = active_app_area().y1 + kQuickGapTop;
    s_quick_y_hidden = s_quick_y_rest - kQuickPanelSize;
    lv_obj_set_pos(s_quick_root, 0, 0);
    lv_obj_set_style_bg_opa(s_quick_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_quick_root, 0, 0);
    lv_obj_set_style_pad_all(s_quick_root, 0, 0);
    lv_obj_clear_flag(s_quick_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scrollbar_mode(s_quick_root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_quick_root, LV_OBJ_FLAG_SCROLLABLE);

    s_quick_panel = lv_obj_create(s_quick_root);
    lv_obj_set_size(s_quick_panel, kQuickPanelSize, kQuickPanelSize);
    lv_obj_set_pos(s_quick_panel, lv_disp_get_hor_res(nullptr) - kQuickPanelSize - kQuickMargin, s_quick_y_hidden);
    lv_obj_set_style_bg_color(s_quick_panel, lv_color_hex(0x20242c), 0);
    lv_obj_set_style_bg_opa(s_quick_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_color(s_quick_panel, lv_color_hex(0x1b1e24), 0);
    lv_obj_set_style_bg_grad_dir(s_quick_panel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(s_quick_panel, 22, 0);
    lv_obj_set_style_border_width(s_quick_panel, 1, 0);
    lv_obj_set_style_border_side(s_quick_panel, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(s_quick_panel, lv_color_white(), 0);
    lv_obj_set_style_border_opa(s_quick_panel, 26, 0);
    lv_obj_set_style_pad_all(s_quick_panel, kQuickPad, 0);
    lv_obj_clear_flag(s_quick_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_quick_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_layout(s_quick_panel, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(s_quick_panel, s_quick_cols, s_quick_rows);
    lv_obj_set_style_pad_row(s_quick_panel, kQuickGap, 0);
    lv_obj_set_style_pad_column(s_quick_panel, kQuickGap, 0);

    auto tile = [](const char *text, lv_grid_align_t col_align, int col, int col_span, int row, int row_span) {
        lv_obj_t *obj = lv_btn_create(s_quick_panel);
        lv_obj_set_grid_cell(obj, col_align, col, col_span, LV_GRID_ALIGN_STRETCH, row, row_span);
        lv_obj_set_style_radius(obj, col_span > 1 ? 16 : 14, 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffff), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(obj, 31, LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x3b82f6), LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_CHECKED);
        lv_obj_set_style_transform_zoom(obj, 248, LV_STATE_PRESSED);
        lv_obj_t *label = lv_label_create(obj); lv_label_set_text(label, text); lv_obj_center(label);
        lv_obj_set_style_text_color(label, lv_color_hex(0xf2f4f7), 0);
        return obj;
    };
    char wifi_text[64] = {};
    wifi_tile_text(wifi_text, sizeof(wifi_text));
    s_quick_wifi = tile(wifi_text, LV_GRID_ALIGN_STRETCH, 0, 2, 0, 2);
    // Reflect the adapter's state immediately; the service event will refine
    // the label once connection status is known.
    if (hal().wifi != nullptr && hal().wifi->enabled()) {
        lv_obj_add_state(s_quick_wifi, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(s_quick_wifi, [](lv_event_t *) { close_quick_settings(wifi_page_open); }, LV_EVENT_LONG_PRESSED, nullptr);
    lv_obj_add_event_cb(s_quick_wifi, [](lv_event_t *event) {
        lv_obj_t *tile = static_cast<lv_obj_t *>(lv_event_get_target(event));
        if (hal().wifi == nullptr) return;
        const bool enabled = !hal().wifi->enabled();
        hal().wifi->set_enabled(enabled);
        if (enabled) lv_obj_add_state(tile, LV_STATE_CHECKED);
        else lv_obj_clear_state(tile, LV_STATE_CHECKED);
    }, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_t *bt = tile(LV_SYMBOL_BLUETOOTH "\nBluetooth\nUnavailable", LV_GRID_ALIGN_STRETCH, 2, 2, 0, 2); lv_obj_set_style_bg_opa(bt, 15, 0); lv_obj_set_style_text_opa(lv_obj_get_child(bt, 0), LV_OPA_40, 0); lv_obj_add_state(bt, LV_STATE_DISABLED);
    s_quick_brightness = lv_bar_create(s_quick_panel); lv_obj_set_grid_cell(s_quick_brightness, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 2); lv_obj_set_style_radius(s_quick_brightness, 18, 0); lv_obj_set_style_bg_color(s_quick_brightness, lv_color_hex(0xffffff), LV_PART_MAIN); lv_obj_set_style_bg_opa(s_quick_brightness, 31, LV_PART_MAIN); lv_obj_set_style_bg_color(s_quick_brightness, lv_color_hex(0x3b82f6), LV_PART_INDICATOR); lv_obj_set_style_bg_opa(s_quick_brightness, LV_OPA_COVER, LV_PART_INDICATOR); lv_obj_set_style_radius(s_quick_brightness, 18, LV_PART_MAIN); lv_obj_set_style_radius(s_quick_brightness, 18, LV_PART_INDICATOR); lv_bar_set_range(s_quick_brightness, 0, 95); lv_bar_set_value(s_quick_brightness, hal().brightness->get(), LV_ANIM_OFF); lv_obj_add_event_cb(s_quick_brightness, quick_bar_event, LV_EVENT_ALL, nullptr);
    // The bundled font has no sun glyph, so draw a small flat icon without
    // theme styles. Its bottom alignment matches the volume symbol exactly.
    lv_obj_t *brightness_icon = lv_obj_create(s_quick_brightness); lv_obj_remove_style_all(brightness_icon); lv_obj_set_size(brightness_icon, 16, 16); lv_obj_align(brightness_icon, LV_ALIGN_BOTTOM_MID, 0, -8); lv_obj_clear_flag(brightness_icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *sun_core = lv_obj_create(brightness_icon); lv_obj_remove_style_all(sun_core); lv_obj_set_size(sun_core, 8, 8); lv_obj_center(sun_core); lv_obj_set_style_bg_color(sun_core, lv_color_hex(0xf2f4f7), 0); lv_obj_set_style_bg_opa(sun_core, LV_OPA_COVER, 0); lv_obj_set_style_radius(sun_core, LV_RADIUS_CIRCLE, 0); lv_obj_clear_flag(sun_core, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    const lv_coord_t ray_pos[][2] = {{7, 0}, {7, 14}, {0, 7}, {14, 7}, {2, 2}, {12, 2}, {2, 12}, {12, 12}}; for (const auto &pos : ray_pos) { lv_obj_t *ray = lv_obj_create(brightness_icon); lv_obj_remove_style_all(ray); lv_obj_set_size(ray, 2, 2); lv_obj_set_pos(ray, pos[0], pos[1]); lv_obj_set_style_bg_color(ray, lv_color_hex(0xf2f4f7), 0); lv_obj_set_style_bg_opa(ray, LV_OPA_COVER, 0); lv_obj_set_style_radius(ray, LV_RADIUS_CIRCLE, 0); lv_obj_clear_flag(ray, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE); }
    uint8_t stored_volume = 85; size_t volume_len = sizeof(stored_volume); if (hal().storage != nullptr && hal().storage->get("volume", &stored_volume, &volume_len) && volume_len == sizeof(stored_volume)) (void)crystal_hal_set_volume(stored_volume);
    s_quick_volume = lv_bar_create(s_quick_panel); lv_obj_set_grid_cell(s_quick_volume, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 2, 2); lv_obj_set_style_radius(s_quick_volume, 18, 0); lv_obj_set_style_bg_color(s_quick_volume, lv_color_hex(0xffffff), LV_PART_MAIN); lv_obj_set_style_bg_opa(s_quick_volume, 31, LV_PART_MAIN); lv_obj_set_style_radius(s_quick_volume, 18, LV_PART_MAIN); lv_obj_set_style_radius(s_quick_volume, 18, LV_PART_INDICATOR); lv_bar_set_range(s_quick_volume, 0, 100); lv_bar_set_value(s_quick_volume, crystal_hal_get_volume(), LV_ANIM_OFF); lv_obj_add_event_cb(s_quick_volume, quick_bar_event, LV_EVENT_ALL, nullptr);
    lv_obj_t *volume_icon = lv_label_create(s_quick_volume); lv_label_set_text(volume_icon, LV_SYMBOL_VOLUME_MID); lv_obj_align(volume_icon, LV_ALIGN_BOTTOM_MID, 0, -8); lv_obj_set_style_text_color(volume_icon, lv_color_hex(0xf2f4f7), 0); lv_obj_clear_flag(volume_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *energy = tile(LV_SYMBOL_BATTERY_FULL, LV_GRID_ALIGN_STRETCH, 2, 1, 2, 1);
    // Draw the leaf natively; the board font does not reliably provide the
    // Font Awesome leaf glyph used by some LVGL symbol builds.
    lv_obj_t *energy_leaf = lv_obj_create(energy); lv_obj_remove_style_all(energy_leaf); lv_obj_set_size(energy_leaf, 20, 18); lv_obj_align(energy_leaf, LV_ALIGN_CENTER, 10, 2); lv_obj_clear_flag(energy_leaf, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *leaf_left = lv_obj_create(energy_leaf); lv_obj_remove_style_all(leaf_left); lv_obj_set_size(leaf_left, 7, 12); lv_obj_set_pos(leaf_left, 3, 2); lv_obj_set_style_bg_color(leaf_left, lv_color_hex(0x9be15b), 0); lv_obj_set_style_bg_opa(leaf_left, LV_OPA_COVER, 0); lv_obj_set_style_radius(leaf_left, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_transform_angle(leaf_left, 350, 0); lv_obj_clear_flag(leaf_left, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *leaf_right = lv_obj_create(energy_leaf); lv_obj_remove_style_all(leaf_right); lv_obj_set_size(leaf_right, 7, 12); lv_obj_set_pos(leaf_right, 9, 2); lv_obj_set_style_bg_color(leaf_right, lv_color_hex(0x70bd43), 0); lv_obj_set_style_bg_opa(leaf_right, LV_OPA_COVER, 0); lv_obj_set_style_radius(leaf_right, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_transform_angle(leaf_right, 550, 0); lv_obj_clear_flag(leaf_right, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    uint8_t saving = 0; size_t saving_len = sizeof(saving); if (hal().storage != nullptr) (void)hal().storage->get("power.saving", &saving, &saving_len); if (saving) lv_obj_add_state(energy, LV_STATE_CHECKED);
    lv_obj_add_event_cb(energy, [](lv_event_t *e) { lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_target(e)); const bool on = !lv_obj_has_state(obj, LV_STATE_CHECKED); if (on) lv_obj_add_state(obj, LV_STATE_CHECKED); else lv_obj_clear_state(obj, LV_STATE_CHECKED); uint8_t value = on ? 1 : 0; if (hal().storage != nullptr) hal().storage->set("power.saving", &value, sizeof(value)); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *gear = tile(LV_SYMBOL_SETTINGS, LV_GRID_ALIGN_STRETCH, 3, 1, 2, 1); lv_obj_add_event_cb(gear, [](lv_event_t *) { quick_show_message(nullptr); lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, s_quick_panel); lv_anim_set_exec_cb(&a, quick_anim_y); lv_anim_set_values(&a, lv_obj_get_y(s_quick_panel), s_quick_y_hidden); lv_anim_set_time(&a, kQuickAnimMs); lv_anim_set_path_cb(&a, lv_anim_path_ease_out); lv_anim_set_ready_cb(&a, quick_anim_ready); s_quick_settling = true; s_quick_closing = true; lv_anim_start(&a); }, LV_EVENT_CLICKED, nullptr);
    s_quick_settings_open = true;
    return true;
}

void update_quick_settings(const ESP_Brookesia_GestureInfo_t &info)
{
    if (s_quick_root == nullptr) return;
    const int dy = info.stop_y - info.start_y;
    const lv_coord_t origin = s_quick_catcher != nullptr ? s_quick_y_rest : s_quick_y_hidden;
    const lv_coord_t y = LV_CLAMP(s_quick_y_hidden, static_cast<lv_coord_t>(origin + dy), s_quick_y_rest);
    lv_obj_set_y(s_quick_panel, y);
}

void release_quick_settings(const ESP_Brookesia_GestureInfo_t &info)
{
    if (s_quick_root == nullptr) return;
    const lv_coord_t offset = static_cast<lv_coord_t>(lv_obj_get_y(s_quick_panel) - s_quick_y_hidden);
    const bool open = (info.stop_y - info.start_y) > kQuickPanelSize / 2 || offset > kQuickPanelSize / 2;
    lv_anim_t animation; lv_anim_init(&animation); lv_anim_set_var(&animation, s_quick_panel);
    lv_anim_set_exec_cb(&animation, quick_anim_y); lv_anim_set_values(&animation, lv_obj_get_y(s_quick_panel), open ? s_quick_y_rest : s_quick_y_hidden);
    lv_anim_set_time(&animation, kQuickAnimMs); lv_anim_set_path_cb(&animation, lv_anim_path_ease_out); lv_anim_set_ready_cb(&animation, quick_anim_ready); s_quick_settling = true; s_quick_closing = !open; lv_anim_start(&animation);
}

bool crossover_threshold_reached(lv_coord_t progress, lv_coord_t width,
                                 uint32_t percent)
{
    if (width <= 0) {
        return false;
    }
    const lv_coord_t threshold = LV_MAX(1, static_cast<lv_coord_t>(
        (static_cast<int32_t>(width) * percent) / 100));
    return progress >= threshold;
}

bool os_owns_gesture()
{
    return s_gesture_owner == CrystalGestureOwner::AppSwitch ||
           s_gesture_owner == CrystalGestureOwner::QuickSettings;
}

void update_page_dots()
{
    if (s_page_dots == nullptr) {
        return;
    }
    const size_t count = crystal_registry_installed_count();
    for (size_t i = 0; i < count; ++i) {
        lv_obj_t *dot = lv_obj_get_child(s_page_dots, static_cast<int32_t>(i));
        if (dot != nullptr) {
            lv_obj_set_style_bg_opa(dot, i == s_current_index ? LV_OPA_COVER : LV_OPA_40, 0);
        }
    }
}

bool init_indicator_overlay()
{
    lv_obj_t *logo = lv_label_create(lv_layer_top());
    if (logo == nullptr) {
        return false;
    }
    lv_label_set_text(logo, "C");
    lv_obj_set_style_text_font(logo, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(logo, lv_color_white(), 0);
    lv_obj_clear_flag(logo, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(logo, LV_ALIGN_TOP_LEFT, 8, 10);

    const size_t count = crystal_registry_installed_count();
    s_page_dots = lv_obj_create(lv_layer_top());
    if (s_page_dots == nullptr) {
        return false;
    }
    lv_obj_set_size(s_page_dots, static_cast<lv_coord_t>(count * 9), 7);
    lv_obj_set_style_bg_opa(s_page_dots, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_page_dots, 0, 0);
    lv_obj_set_style_pad_all(s_page_dots, 0, 0);
    lv_obj_set_style_pad_column(s_page_dots, 4, 0);
    lv_obj_set_flex_flow(s_page_dots, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(s_page_dots, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_page_dots, LV_ALIGN_TOP_MID, 0, 31);
    for (size_t i = 0; i < count; ++i) {
        lv_obj_t *dot = lv_obj_create(s_page_dots);
        if (dot == nullptr) {
            return false;
        }
        lv_obj_set_size(dot, 5, 5);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_white(), 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    update_page_dots();
    return true;
}

// Takes the clipping container returned by show_snapshot_transition(), frees the
// snapshot buffer its child image points at, then deletes the container.
void discard_snapshot(lv_obj_t *container)
{
    if (container == nullptr) {
        return;
    }
    lv_obj_t *image = lv_obj_get_child(container, 0);
    if (image != nullptr) {
        auto *snapshot = const_cast<lv_img_dsc_t *>(
            static_cast<const lv_img_dsc_t *>(lv_img_get_src(image)));
        if (snapshot != nullptr) {
            lv_img_buf_free(snapshot);
        }
    }
    lv_obj_del(container);
}

void transition_cleanup_cb(lv_timer_t *timer)
{
    discard_snapshot(static_cast<lv_obj_t *>(timer->user_data));
    lv_timer_del(timer);
}

// The app area excludes the status bar: Brookesia subtracts it when it calibrates
// each app's visual area at install time. Falls back to the full display only when
// no app is active, which should not happen on a switch.
lv_area_t active_app_area()
{
    if (s_phone != nullptr && s_phone->getManager().getActiveApp() != nullptr) {
        return s_phone->getManager().getActiveApp()->getVisualArea();
    }
    return lv_area_t{0, 0,
                     static_cast<lv_coord_t>(lv_disp_get_hor_res(nullptr) - 1),
                     static_cast<lv_coord_t>(lv_disp_get_ver_res(nullptr) - 1)};
}

lv_img_dsc_t *capture_app_area_full()
{
    lv_img_dsc_t *screen = lv_snapshot_take(lv_scr_act(), LV_IMG_CF_TRUE_COLOR);
    if (screen == nullptr || screen->header.w == 0 || screen->header.h == 0) {
        return nullptr;
    }

    const lv_area_t area = active_app_area();
    const lv_coord_t width = lv_area_get_width(&area);
    const lv_coord_t height = lv_area_get_height(&area);
    // Brookesia's active app screen is normally app-area sized, so its snapshot
    // uses local (0, 0) coordinates even though getVisualArea() is expressed in
    // display coordinates below the status bar. Keep supporting a full-display
    // snapshot as well, since LVGL screen sizing can vary by app configuration.
    const lv_coord_t source_x = screen->header.w == width ? 0 : area.x1;
    const lv_coord_t source_y = screen->header.h == height ? 0 : area.y1;
    if (width <= 0 || height <= 0 || source_x < 0 || source_y < 0 ||
            source_x + width > static_cast<lv_coord_t>(screen->header.w) ||
            source_y + height > static_cast<lv_coord_t>(screen->header.h)) {
        ESP_LOGE(TAG, "cannot crop app pane: source=%ux%u area=(%d,%d %dx%d)",
                 screen->header.w, screen->header.h, area.x1, area.y1, width, height);
        lv_snapshot_free(screen);
        return nullptr;
    }

    lv_img_dsc_t *pane = lv_img_buf_alloc(width, height, LV_IMG_CF_TRUE_COLOR);
    if (pane == nullptr) {
        lv_snapshot_free(screen);
        return nullptr;
    }

    const auto *source = reinterpret_cast<const lv_color_t *>(screen->data);
    auto *dest = reinterpret_cast<lv_color_t *>(const_cast<uint8_t *>(pane->data));
    for (lv_coord_t y = 0; y < height; ++y) {
        memcpy(dest + static_cast<size_t>(y) * width,
               source + static_cast<size_t>(source_y + y) * screen->header.w + source_x,
               static_cast<size_t>(width) * sizeof(lv_color_t));
    }
    lv_snapshot_free(screen);

    return pane;
}

struct PreviewFileHeader { uint32_t magic; uint16_t width; uint16_t height; };

bool preview_path(size_t index, char *path, size_t size)
{
    const char *id = crystal_registry_installed_id(index);
    if (id == nullptr || path == nullptr || size == 0) return false;
    char safe[40]; size_t n = 0;
    for (; id[n] != '\0' && n + 1 < sizeof(safe); ++n) {
        const char c = id[n];
        safe[n] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '_' || c == '-') ? c : '_';
    }
    safe[n] = '\0';
    const int written = snprintf(path, size, "%s%s.bin", kPreviewPathPrefix, safe);
    return written > 0 && static_cast<size_t>(written) < size;
}

lv_img_dsc_t *load_persistent_preview(size_t index)
{
    const char *id = crystal_registry_installed_id(index);
    char path[96];
    if (!preview_path(index, path, sizeof(path))) {
        ESP_LOGW(TAG, "preview load: id=%s path construction failed", id != nullptr ? id : "<null>");
        return nullptr;
    }
    FILE *file = fopen(path, "rb");
    if (file == nullptr) {
        ESP_LOGI(TAG, "preview load: id=%s path=%s result=missing",
                 id != nullptr ? id : "<null>", path);
        return nullptr;
    }
    PreviewFileHeader header{}; lv_img_dsc_t *image = nullptr;
    const bool header_ok = fread(&header, sizeof(header), 1, file) == 1 &&
                           header.magic == kPreviewMagic && header.width > 0 && header.height > 0;
    if (!header_ok) {
        ESP_LOGW(TAG, "preview load: id=%s path=%s result=invalid-header",
                 id != nullptr ? id : "<null>", path);
    } else {
        image = lv_img_buf_alloc(header.width, header.height, LV_IMG_CF_TRUE_COLOR);
        if (image == nullptr) {
            ESP_LOGW(TAG, "preview load: id=%s path=%s result=allocation-failed size=%ux%u",
                     id != nullptr ? id : "<null>", path, header.width, header.height);
        } else if (fread(const_cast<uint8_t *>(image->data), image->data_size, 1, file) != 1) {
            ESP_LOGW(TAG, "preview load: id=%s path=%s result=truncated expected_bytes=%u",
                     id != nullptr ? id : "<null>", path, static_cast<unsigned>(image->data_size));
            lv_img_buf_free(image);
            image = nullptr;
        } else {
            ESP_LOGI(TAG, "preview load: id=%s path=%s result=success size=%ux%u bytes=%u",
                     id != nullptr ? id : "<null>", path, header.width, header.height,
                     static_cast<unsigned>(image->data_size));
        }
    }
    fclose(file);
    return image;
}

void save_persistent_preview(size_t index, const lv_img_dsc_t *image)
{
    const char *id = crystal_registry_installed_id(index);
    if (image == nullptr) {
        ESP_LOGW(TAG, "preview save: id=%s result=no-image", id != nullptr ? id : "<null>");
        return;
    }
    char path[96];
    if (!preview_path(index, path, sizeof(path))) {
        ESP_LOGW(TAG, "preview save: id=%s path construction failed", id != nullptr ? id : "<null>");
        return;
    }
    // SPIFFS is configured with a 32-character object-name limit. Appending
    // ".tmp" to the full preview filename makes the state_test object too
    // long, so use a short temporary basename and retain the stable ID.
    const char *stable_name = path + strlen(kPreviewPathPrefix);
    const size_t stable_name_len = strlen(stable_name);
    const size_t id_len = stable_name_len > 4 ? stable_name_len - 4 : stable_name_len;
    char temp[104];
    const int written = snprintf(temp, sizeof(temp), "/spiffs/.tmp_%.*s",
                                 static_cast<int>(id_len), stable_name);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(temp)) {
        ESP_LOGW(TAG, "preview save: id=%s path=%s result=temp-path-failed",
                 id != nullptr ? id : "<null>", path);
        return;
    }
    FILE *file = fopen(temp, "wb");
    if (file == nullptr) {
        ESP_LOGW(TAG, "preview save: id=%s path=%s result=open-failed",
                 id != nullptr ? id : "<null>", path);
        return;
    }
    const PreviewFileHeader header = {kPreviewMagic, image->header.w, image->header.h};
    const bool ok = fwrite(&header, sizeof(header), 1, file) == 1 &&
                    fwrite(image->data, image->data_size, 1, file) == 1;
    fclose(file);
    if (ok && remove(path) == 0) {
        // Removing an existing file is expected on refresh; continue to rename.
    }
    const int rename_result = ok ? rename(temp, path) : -1;
    if (rename_result == 0) {
        ESP_LOGI(TAG, "preview save: id=%s path=%s result=success size=%ux%u bytes=%u",
                 id != nullptr ? id : "<null>", path, image->header.w, image->header.h,
                 static_cast<unsigned>(image->data_size));
    } else {
        ESP_LOGW(TAG, "preview save: id=%s path=%s result=%s bytes=%u",
                 id != nullptr ? id : "<null>", path, ok ? "rename-failed" : "write-failed",
                 static_cast<unsigned>(image->data_size));
        (void)remove(temp);
    }
}

void replace_cached_pane(size_t index, lv_img_dsc_t *image)
{
    if (index >= s_pane_cache.size()) {
        if (image != nullptr) {
            lv_img_buf_free(image);
        }
        return;
    }
    if (s_pane_cache[index].image != nullptr && s_pane_cache[index].image != image) {
        lv_img_buf_free(s_pane_cache[index].image);
    }
    s_pane_cache[index].image = image;
}

void prune_pane_cache(size_t current_index)
{
    for (size_t i = 0; i < s_pane_cache.size(); ++i) {
        const bool neighbour = (i + 1 == current_index) || (i == current_index + 1);
        if (!neighbour && s_pane_cache[i].image != nullptr) {
            lv_img_buf_free(s_pane_cache[i].image);
            s_pane_cache[i].image = nullptr;
        }
    }
}

// Box-averages `crop` out of `source` into a freshly allocated dest_w x dest_h
// image. Both images must be LV_IMG_CF_TRUE_COLOR. Returns nullptr on failure.
lv_img_dsc_t *downscale_crop(const lv_img_dsc_t *source, const lv_area_t &crop,
                             lv_coord_t dest_w, lv_coord_t dest_h)
{
    if (dest_w <= 0 || dest_h <= 0) {
        return nullptr;
    }
    lv_img_dsc_t *dest = lv_img_buf_alloc(dest_w, dest_h, LV_IMG_CF_TRUE_COLOR);
    if (dest == nullptr) {
        return nullptr;
    }

    const lv_coord_t crop_w = lv_area_get_width(&crop);
    const lv_coord_t crop_h = lv_area_get_height(&crop);
    const auto *source_px = reinterpret_cast<const lv_color_t *>(source->data);
    auto *dest_px = reinterpret_cast<lv_color_t *>(const_cast<uint8_t *>(dest->data));
    const lv_coord_t source_stride = source->header.w;

    for (lv_coord_t y = 0; y < dest_h; ++y) {
        // Source rows [row_begin, row_end) average into destination row y.
        const lv_coord_t row_begin = crop.y1 + (y * crop_h) / dest_h;
        const lv_coord_t row_end = LV_MAX(row_begin + 1, crop.y1 + ((y + 1) * crop_h) / dest_h);
        for (lv_coord_t x = 0; x < dest_w; ++x) {
            const lv_coord_t col_begin = crop.x1 + (x * crop_w) / dest_w;
            const lv_coord_t col_end = LV_MAX(col_begin + 1, crop.x1 + ((x + 1) * crop_w) / dest_w);
            uint32_t red = 0;
            uint32_t green = 0;
            uint32_t blue = 0;
            uint32_t count = 0;
            for (lv_coord_t sy = row_begin; sy < row_end; ++sy) {
                const lv_color_t *row = source_px + static_cast<size_t>(sy) * source_stride;
                for (lv_coord_t sx = col_begin; sx < col_end; ++sx) {
                    const lv_color_t c = row[sx];
                    red += LV_COLOR_GET_R(c);
                    green += LV_COLOR_GET_G(c);
                    blue += LV_COLOR_GET_B(c);
                    ++count;
                }
            }
            lv_color_t out;
            LV_COLOR_SET_R(out, red / count);
            LV_COLOR_SET_G(out, green / count);
            LV_COLOR_SET_B(out, blue / count);
            dest_px[static_cast<size_t>(y) * dest_w + x] = out;
        }
    }
    return dest;
}

// Covers the app area (never the status bar) with a magnified copy of what the app
// area currently shows. Returns the clipping container, which owns the image;
// pass it to finish_snapshot_transition() or discard_snapshot().
lv_obj_t *show_snapshot_transition()
{
    lv_img_dsc_t *source = lv_snapshot_take(lv_scr_act(), LV_IMG_CF_TRUE_COLOR);
    if (source == nullptr || source->header.w == 0 || source->header.h == 0) {
        return nullptr;
    }

    const lv_area_t app_area = active_app_area();
    const lv_coord_t app_w = lv_area_get_width(&app_area);
    const lv_coord_t app_h = lv_area_get_height(&app_area);

    // Crop 90% about the centre of the app area, clamped to the captured screen so
    // an unexpected visual area cannot read outside the source buffer.
    const lv_coord_t crop_w = static_cast<lv_coord_t>((app_w * kSnapshotCropPercent) / 100);
    const lv_coord_t crop_h = static_cast<lv_coord_t>((app_h * kSnapshotCropPercent) / 100);
    lv_area_t crop = {
        static_cast<lv_coord_t>(app_area.x1 + (app_w - crop_w) / 2),
        static_cast<lv_coord_t>(app_area.y1 + (app_h - crop_h) / 2),
        0, 0,
    };
    crop.x1 = LV_CLAMP(0, crop.x1, static_cast<lv_coord_t>(source->header.w - 1));
    crop.y1 = LV_CLAMP(0, crop.y1, static_cast<lv_coord_t>(source->header.h - 1));
    crop.x2 = LV_MIN(static_cast<lv_coord_t>(crop.x1 + crop_w - 1),
                     static_cast<lv_coord_t>(source->header.w - 1));
    crop.y2 = LV_MIN(static_cast<lv_coord_t>(crop.y1 + crop_h - 1),
                     static_cast<lv_coord_t>(source->header.h - 1));

    // Half the app resolution per axis, so the stored image keeps the app's aspect
    // ratio and one zoom factor serves both axes.
    const lv_coord_t small_w = LV_MAX(1, static_cast<lv_coord_t>(app_w / kSnapshotScaleDivisor));
    const lv_coord_t small_h = LV_MAX(1, static_cast<lv_coord_t>(app_h / kSnapshotScaleDivisor));
    lv_img_dsc_t *small = downscale_crop(source, crop, small_w, small_h);
    lv_snapshot_free(source);
    if (small == nullptr) {
        return nullptr;
    }

    // Container clipped to the app area: whatever the zoom rounds to, nothing can
    // paint over the status bar.
    lv_obj_t *container = lv_obj_create(lv_layer_top());
    if (container == nullptr) {
        lv_img_buf_free(small);
        return nullptr;
    }
    lv_obj_set_size(container, app_w, app_h);
    lv_obj_set_pos(container, app_area.x1, app_area.y1);
    lv_obj_set_style_radius(container, 0, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *image = lv_img_create(container);
    if (image == nullptr) {
        lv_obj_del(container);
        lv_img_buf_free(small);
        return nullptr;
    }
    lv_img_set_src(image, small);
    lv_img_set_size_mode(image, LV_IMG_SIZE_MODE_REAL);
    // Round the zoom up so the magnified image covers the app area rather than
    // leaving a seam; the container clips the overshoot.
    const uint32_t zoom_x = (static_cast<uint32_t>(app_w) * 256u + small_w - 1) / small_w;
    const uint32_t zoom_y = (static_cast<uint32_t>(app_h) * 256u + small_h - 1) / small_h;
    lv_img_set_zoom(image, static_cast<uint16_t>(LV_MAX(zoom_x, zoom_y)));
    lv_obj_center(image);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);

    ESP_LOGI(TAG, "snapshot transition: app %dx%d at (%d,%d) crop=%u%% (%dx%d) "
             "-> %dx%d, zoom=%u/256, %u ms",
             app_w, app_h, app_area.x1, app_area.y1,
             static_cast<unsigned>(kSnapshotCropPercent),
             lv_area_get_width(&crop), lv_area_get_height(&crop),
             small_w, small_h,
             static_cast<unsigned>(LV_MAX(zoom_x, zoom_y)),
             static_cast<unsigned>(kTransitionMs));
    return container;
}

lv_obj_t *show_transition_cover()
{
    lv_obj_t *cover = lv_obj_create(lv_layer_top());
    if (cover == nullptr) {
        return nullptr;
    }
    lv_obj_set_size(cover, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
    lv_obj_set_pos(cover, 0, 0);
    lv_obj_set_style_radius(cover, 0, 0);
    lv_obj_set_style_border_width(cover, 0, 0);
    lv_obj_set_style_pad_all(cover, 0, 0);
    lv_obj_set_style_bg_color(cover, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, 0);
    lv_obj_clear_flag(cover, LV_OBJ_FLAG_CLICKABLE);
    return cover;
}

// Opacity cascades to children in LVGL 8, so fading the container fades the image.
void finish_snapshot_transition(lv_obj_t *container)
{
    if (container == nullptr) {
        return;
    }
    lv_obj_fade_out(container, kTransitionMs, 0);
    lv_timer_create(transition_cleanup_cb, kTransitionMs + 20, container);
}

bool persist_current_card()
{
    const char *id = crystal_registry_installed_id(s_current_index);
    if (id == nullptr || hal().storage == nullptr) {
        return false;
    }
    return hal().storage->set(kCurrentCardKey, id, strlen(id) + 1);
}

size_t load_current_index()
{
    char stored_id[32] = {};
    size_t length = sizeof(stored_id);
    if (hal().storage == nullptr ||
            !hal().storage->get(kCurrentCardKey, stored_id, &length) ||
            length == 0 || length > sizeof(stored_id) || stored_id[length - 1] != '\0') {
        return 0;
    }

    for (size_t i = 0; i < crystal_registry_installed_count(); ++i) {
        const char *id = crystal_registry_installed_id(i);
        if (id != nullptr && strcmp(id, stored_id) == 0) {
            return i;
        }
    }
    return 0;
}

bool start_card(size_t index, bool animate)
{
    CrystalApp *app = crystal_registry_installed_app(index);
    if (s_phone == nullptr || app == nullptr || index >= crystal_registry_installed_count()) {
        return false;
    }

    lv_obj_t *cover = nullptr;
    if (animate && s_phone->getManager().getActiveApp() != nullptr &&
            s_phone->getManager().getActiveApp() != app) {
        cover = show_transition_cover();
    }

    ESP_Brookesia_CoreAppEventData_t event_data = {
        .id = app->getId(),
        .type = ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START,
        .data = nullptr,
    };
    s_switching = true;
    const bool sent = s_phone->sendAppEvent(&event_data);
    s_switching = false;
    if (!sent || s_phone->getManager().getActiveApp() != app) {
        if (cover != nullptr) {
            lv_obj_del(cover);
        }
        ESP_LOGE(TAG, "failed to open card %u (%s)", static_cast<unsigned>(index),
                 crystal_registry_installed_id(index));
        return false;
    }

    lv_obj_t *transition = animate ? show_snapshot_transition() : nullptr;
    if (cover != nullptr) {
        lv_obj_del(cover);
    }
    if (animate) {
        finish_snapshot_transition(transition);
    }

    s_current_index = index;
    update_page_dots();
    if (!persist_current_card()) {
        ESP_LOGW(TAG, "failed to persist current card");
    }
    ESP_LOGI(TAG, "active card %u/%u: %s", static_cast<unsigned>(index + 1),
             static_cast<unsigned>(crystal_registry_installed_count()),
             crystal_registry_installed_id(index));
    return true;
}

void set_transition_progress(void *, int32_t value)
{
    CardTransition &transition = s_card_transition;
    if (transition.incoming_card == nullptr || transition.width <= 0) {
        return;
    }
    transition.progress = LV_CLAMP(0, static_cast<lv_coord_t>(value), transition.width);
    const lv_coord_t x = transition.direction > 0
        ? static_cast<lv_coord_t>(transition.progress - transition.width)
        : static_cast<lv_coord_t>(transition.width - transition.progress);
    lv_obj_set_x(transition.incoming_card, x);
}

// Downscaling the outgoing pane and writing it to storage is the heaviest work
// in the whole gesture. It is deferred to its own stage so it cannot stall the
// slide or the destination's first frame; the caller hands over ownership of the
// full-resolution buffer.
struct PendingPreview {
    lv_img_dsc_t *outgoing = nullptr;
    size_t index = SIZE_MAX;
};
PendingPreview s_pending_preview;

void store_outgoing_preview(lv_img_dsc_t *outgoing, size_t index)
{
    if (outgoing == nullptr) {
        return;
    }
    if (index < s_pane_cache.size()) {
        const lv_coord_t preview_w = LV_MAX(1, static_cast<lv_coord_t>(
            outgoing->header.w / kSnapshotScaleDivisor));
        const lv_coord_t preview_h = LV_MAX(1, static_cast<lv_coord_t>(
            outgoing->header.h / kSnapshotScaleDivisor));
        const lv_area_t source_area = {0, 0,
                                       static_cast<lv_coord_t>(outgoing->header.w - 1),
                                       static_cast<lv_coord_t>(outgoing->header.h - 1)};
        lv_img_dsc_t *preview = downscale_crop(outgoing, source_area, preview_w, preview_h);
        replace_cached_pane(index, preview);
        save_persistent_preview(index, preview);
    }
    lv_img_buf_free(outgoing);
}

void preview_persist_cb(lv_timer_t *timer)
{
    lv_timer_del(timer);
    const PendingPreview pending = s_pending_preview;
    s_pending_preview = PendingPreview{};
    store_outgoing_preview(pending.outgoing, pending.index);
}

void finish_card_transition(lv_anim_t *)
{
    CardTransition &transition = s_card_transition;
    const bool committed = transition.commit;
    const size_t original_index = transition.original_index;
    const size_t active_index = s_current_index;
    lv_img_dsc_t *outgoing = transition.outgoing;

    if (transition.root != nullptr) {
        lv_obj_del(transition.root);
    }
    transition = CardTransition{};

    if (committed && outgoing != nullptr) {
        s_pending_preview = PendingPreview{outgoing, original_index};
        lv_timer_create(preview_persist_cb, kCommitStageMs, nullptr);
    } else if (outgoing != nullptr) {
        lv_img_buf_free(outgoing);
    }

    prune_pane_cache(active_index);
    ESP_LOGI(TAG, "card crossover %s; active=%u, PSRAM free=%u",
             committed ? "committed" : "cancelled",
             static_cast<unsigned>(active_index + 1),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

// Stage 3: the destination is live and has had a frame to draw under the
// overlay, so the overlay can go without exposing a half-built tree.
void card_reveal_cb(lv_timer_t *timer)
{
    lv_timer_del(timer);
    finish_card_transition(nullptr);
}

// Stage 2: the card has been presented at full width. Now run the Brookesia
// lifecycle -- A onPause/onDestroy, B onCreate/onResume -- behind the card.
void card_commit_cb(lv_timer_t *timer)
{
    lv_timer_del(timer);
    CardTransition &transition = s_card_transition;
    if (transition.phase != CardTransitionPhase::Settling || !transition.commit) {
        finish_card_transition(nullptr);
        return;
    }

    const bool started = transition.target_index < crystal_registry_installed_count() &&
                         start_card(transition.target_index, false);
    if (!started) {
        transition.commit = false;
        ESP_LOGE(TAG, "card commit failed after cover; keeping current app");
        finish_card_transition(nullptr);
        return;
    }
    lv_timer_create(card_reveal_cb, kCommitStageMs, nullptr);
}

// Stage 1: the slide has finished in the object tree but has not been flushed
// yet. Yield so the full-width card reaches the panel before anything else.
void schedule_card_commit(lv_anim_t *)
{
    CardTransition &transition = s_card_transition;
    if (transition.phase != CardTransitionPhase::Settling || !transition.commit) {
        finish_card_transition(nullptr);
        return;
    }
    lv_timer_create(card_commit_cb, kCommitStageMs, nullptr);
}

bool attach_incoming_image(lv_img_dsc_t *pane)
{
    CardTransition &transition = s_card_transition;
    if (pane == nullptr || transition.incoming_card == nullptr) {
        return false;
    }
    if (transition.incoming_image == nullptr) {
        transition.incoming_image = lv_img_create(transition.incoming_card);
        if (transition.incoming_image == nullptr) {
            return false;
        }
        lv_obj_clear_flag(transition.incoming_image,
                          LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_img_set_src(transition.incoming_image, pane);
    const uint32_t zoom_x = (static_cast<uint32_t>(transition.width) * 256u + pane->header.w - 1) /
                            pane->header.w;
    const uint32_t zoom_y = (static_cast<uint32_t>(lv_obj_get_height(transition.incoming_card)) * 256u +
                             pane->header.h - 1) / pane->header.h;
    lv_img_set_zoom(transition.incoming_image, static_cast<uint16_t>(LV_MAX(zoom_x, zoom_y)));
    lv_obj_center(transition.incoming_image);
    return true;
}

bool begin_card_transition(const ESP_Brookesia_GestureInfo_t &info)
{
    if (s_card_transition.phase != CardTransitionPhase::Idle || s_phone == nullptr ||
            s_phone->getManager().getActiveApp() == nullptr) {
        return false;
    }

    const bool previous = (info.start_area & ESP_BROOKESIA_GESTURE_AREA_LEFT_EDGE) != 0 &&
                          info.direction == ESP_BROOKESIA_GESTURE_DIR_RIGHT;
    const bool next = (info.start_area & ESP_BROOKESIA_GESTURE_AREA_RIGHT_EDGE) != 0 &&
                      info.direction == ESP_BROOKESIA_GESTURE_DIR_LEFT;
    size_t target_index = SIZE_MAX;
    int direction = 0;
    if (previous && s_current_index > 0) {
        target_index = s_current_index - 1;
        direction = 1;
    } else if (next && s_current_index + 1 < crystal_registry_installed_count()) {
        target_index = s_current_index + 1;
        direction = -1;
    } else {
        return false;
    }

    const lv_area_t app_area = active_app_area();
    const lv_coord_t width = lv_area_get_width(&app_area);
    const lv_coord_t height = lv_area_get_height(&app_area);
    lv_img_dsc_t *outgoing = capture_app_area_full();
    if (outgoing == nullptr || width <= 0 || height <= 0) {
        if (outgoing != nullptr) {
            lv_img_buf_free(outgoing);
        }
        return false;
    }

    lv_obj_t *root = lv_obj_create(lv_layer_top());
    if (root == nullptr) {
        lv_img_buf_free(outgoing);
        return false;
    }
    lv_obj_set_size(root, width, height);
    lv_obj_set_pos(root, app_area.x1, app_area.y1);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *outgoing_image = lv_img_create(root);
    lv_obj_t *incoming_card = lv_obj_create(root);
    if (outgoing_image == nullptr || incoming_card == nullptr) {
        lv_obj_del(root);
        lv_img_buf_free(outgoing);
        return false;
    }

    lv_img_set_src(outgoing_image, outgoing);
    const uint32_t outgoing_zoom_x = (static_cast<uint32_t>(width) * 256u + outgoing->header.w - 1) /
                                     outgoing->header.w;
    const uint32_t outgoing_zoom_y = (static_cast<uint32_t>(height) * 256u + outgoing->header.h - 1) /
                                     outgoing->header.h;
    lv_img_set_zoom(outgoing_image, static_cast<uint16_t>(LV_MAX(outgoing_zoom_x, outgoing_zoom_y)));
    lv_obj_center(outgoing_image);
    lv_obj_clear_flag(outgoing_image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_size(incoming_card, width, height);
    lv_obj_set_style_radius(incoming_card, 12, 0);
    lv_obj_set_style_clip_corner(incoming_card, true, 0);
    lv_obj_set_style_border_width(incoming_card, 0, 0);
    lv_obj_set_style_pad_all(incoming_card, 0, 0);
    lv_obj_set_style_bg_color(incoming_card, lv_color_hex(0xE8EDF2), 0);
    lv_obj_set_style_bg_opa(incoming_card, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(incoming_card, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(incoming_card, LV_OPA_30, 0);
    lv_obj_set_style_shadow_width(incoming_card, 12, 0);
    lv_obj_set_style_shadow_ofs_x(incoming_card, direction > 0 ? 4 : -4, 0);
    lv_obj_clear_flag(incoming_card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    CrystalApp *target_app = crystal_registry_installed_app(target_index);
    if (target_app != nullptr) {
        const void *icon_resource = target_app->getLauncherIcon().resource;
        if (icon_resource != nullptr) {
            lv_obj_t *icon = lv_img_create(incoming_card);
            if (icon != nullptr) {
                lv_img_set_src(icon, icon_resource);
                lv_obj_align(icon, LV_ALIGN_CENTER, 0, -34);
                lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            }
        }
        lv_obj_t *name = lv_label_create(incoming_card);
        if (name != nullptr) {
            lv_obj_set_width(name, static_cast<lv_coord_t>(width - 40));
            lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
            lv_label_set_text(name, target_app->getName());
            lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(name, lv_color_hex(0x1C2A36), 0);
            lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(name, LV_ALIGN_CENTER, 0, 42);
            lv_obj_clear_flag(name, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        }
    }

    s_card_transition.phase = CardTransitionPhase::Dragging;
    s_card_transition.root = root;
    s_card_transition.incoming_card = incoming_card;
    s_card_transition.outgoing = outgoing;
    s_card_transition.original_index = s_current_index;
    s_card_transition.target_index = target_index;
    s_card_transition.width = width;
    s_card_transition.direction = direction;
    if (target_index < s_pane_cache.size()) {
        lv_img_dsc_t *pane = s_pane_cache[target_index].image;
        if (pane == nullptr) {
            pane = load_persistent_preview(target_index);
            if (pane != nullptr) s_pane_cache[target_index].image = pane;
        }
        (void)attach_incoming_image(pane);
    }

    const int dx = info.stop_x - info.start_x;
    set_transition_progress(nullptr, direction > 0 ? dx : -dx);
    ESP_LOGI(TAG, "card crossover started: %u -> %u, PSRAM free=%u",
             static_cast<unsigned>(s_current_index + 1),
             static_cast<unsigned>(target_index + 1),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    return true;
}

void update_card_transition(const ESP_Brookesia_GestureInfo_t &info)
{
    CardTransition &transition = s_card_transition;
    if (transition.phase != CardTransitionPhase::Dragging) {
        return;
    }
    const int dx = info.stop_x - info.start_x;
    set_transition_progress(nullptr, transition.direction > 0 ? dx : -dx);
}

void settle_card_transition(bool commit)
{
    CardTransition &transition = s_card_transition;
    if (transition.phase != CardTransitionPhase::Dragging) {
        return;
    }

    transition.phase = CardTransitionPhase::Settling;
    transition.commit = commit;
    if (transition.root != nullptr) {
        lv_obj_add_flag(transition.root, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, &transition);
    lv_anim_set_exec_cb(&animation, set_transition_progress);
    lv_anim_set_values(&animation, transition.progress, commit ? transition.width : 0);
    lv_anim_set_time(&animation, kCrossoverSettleMs);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&animation, commit ? schedule_card_commit : finish_card_transition);
    lv_anim_start(&animation);
}

void on_app_event(lv_event_t *event)
{
    auto *data = static_cast<ESP_Brookesia_CoreAppEventData_t *>(lv_event_get_param(event));
    if (s_switching || data == nullptr || data->type != ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START) {
        return;
    }
    for (size_t i = 0; i < crystal_registry_installed_count(); ++i) {
        CrystalApp *app = crystal_registry_installed_app(i);
        if (app != nullptr && app->getId() == data->id) {
            s_current_index = i;
            update_page_dots();
            (void)persist_current_card();
            return;
        }
    }
}

void on_gesture_press(lv_event_t *)
{
    s_gesture_owner = CrystalGestureOwner::None;
    s_swallow_wake_touch = crystal_core_consume_wake_touch();
    if (s_swallow_wake_touch && s_gesture != nullptr) {
        (void)s_gesture->setMaskObjectVisible(true);
    }
}

void on_gesture_pressing(lv_event_t *event)
{
    auto *info = static_cast<ESP_Brookesia_GestureInfo_t *>(lv_event_get_param(event));
    if (info == nullptr || s_swallow_wake_touch) {
        return;
    }
    if (s_gesture_owner == CrystalGestureOwner::AppSwitch) {
        update_card_transition(*info);
        return;
    }
    if (s_gesture_owner == CrystalGestureOwner::QuickSettings) {
        lv_area_t panel_area{};
        if (s_quick_panel != nullptr) lv_obj_get_coords(s_quick_panel, &panel_area);
        const bool inside_panel = s_quick_catcher != nullptr && s_quick_panel != nullptr &&
                                  info->start_x >= panel_area.x1 && info->start_x <= panel_area.x2 &&
                                  info->start_y >= panel_area.y1 && info->start_y <= panel_area.y2;
        if (inside_panel) return;
        if (!s_quick_settings_open) (void)create_quick_settings();
        update_quick_settings(*info);
        return;
    }
    if (s_gesture_owner != CrystalGestureOwner::None ||
            info->direction == ESP_BROOKESIA_GESTURE_DIR_NONE) {
        return;
    }

    if (s_modal_open) {
        s_gesture_owner = CrystalGestureOwner::App;
    } else if (s_quick_settings_open) {
        // Controls inside the panel (brightness, volume, tiles) own their
        // vertical drags. Only an upward drag that starts outside the panel
        // dismisses Quick Settings.
        lv_area_t panel_area{};
        if (s_quick_panel != nullptr) lv_obj_get_coords(s_quick_panel, &panel_area);
        const bool inside_panel = s_quick_catcher != nullptr && s_quick_panel != nullptr &&
                                  info->start_x >= panel_area.x1 && info->start_x <= panel_area.x2 &&
                                  info->start_y >= panel_area.y1 && info->start_y <= panel_area.y2;
        s_gesture_owner = (inside_panel || info->direction == ESP_BROOKESIA_GESTURE_DIR_UP)
            ? CrystalGestureOwner::QuickSettings : CrystalGestureOwner::App;
    } else if (s_keyboard_open || s_settings_open || s_switching ||
               s_card_transition.phase != CardTransitionPhase::Idle) {
        s_gesture_owner = CrystalGestureOwner::App;
    } else {
        const bool from_left = (info->start_area & ESP_BROOKESIA_GESTURE_AREA_LEFT_EDGE) != 0;
        const bool from_right = (info->start_area & ESP_BROOKESIA_GESTURE_AREA_RIGHT_EDGE) != 0;
        const bool horizontal_edge =
            (from_left && info->direction == ESP_BROOKESIA_GESTURE_DIR_RIGHT) ||
            (from_right && info->direction == ESP_BROOKESIA_GESTURE_DIR_LEFT);
        const bool in_quick_corner = info->start_x >= lv_disp_get_hor_res(nullptr) - kQuickCornerWidth;
        const bool top_pull = info->start_y < kTopBand && in_quick_corner &&
                              info->direction == ESP_BROOKESIA_GESTURE_DIR_DOWN;
        if (horizontal_edge) {
            s_gesture_owner = CrystalGestureOwner::AppSwitch;
        } else if (top_pull) {
            s_gesture_owner = CrystalGestureOwner::QuickSettings;
        } else {
            s_gesture_owner = CrystalGestureOwner::App;
        }
    }

    if (os_owns_gesture() && s_gesture != nullptr) {
        (void)s_gesture->setMaskObjectVisible(true);
    }
    if (s_gesture_owner == CrystalGestureOwner::AppSwitch) {
        (void)begin_card_transition(*info);
    }
    ESP_LOGD(TAG, "gesture owner locked: %u", static_cast<unsigned>(s_gesture_owner));
}

void on_gesture_release(lv_event_t *event)
{
    auto *info = static_cast<ESP_Brookesia_GestureInfo_t *>(lv_event_get_param(event));
    const CrystalGestureOwner owner = s_gesture_owner;
    s_gesture_owner = CrystalGestureOwner::None;
    if (s_swallow_wake_touch) {
        s_swallow_wake_touch = false;
        return;
    }
    if (owner == CrystalGestureOwner::QuickSettings && info != nullptr) {
        lv_area_t panel_area{};
        if (s_quick_panel != nullptr) lv_obj_get_coords(s_quick_panel, &panel_area);
        const bool inside_panel = s_quick_catcher != nullptr && s_quick_panel != nullptr &&
                                  info->start_x >= panel_area.x1 && info->start_x <= panel_area.x2 &&
                                  info->start_y >= panel_area.y1 && info->start_y <= panel_area.y2;
        if (inside_panel) return;
        if (!s_quick_settings_open) (void)create_quick_settings();
        release_quick_settings(*info);
        return;
    }
    if (owner != CrystalGestureOwner::AppSwitch || info == nullptr || s_phone == nullptr || s_switching ||
            s_phone->getManager().getActiveApp() == nullptr) {
        return;
    }

    if (s_card_transition.phase == CardTransitionPhase::Dragging) {
        update_card_transition(*info);
        settle_card_transition(crossover_threshold_reached(
            s_card_transition.progress, s_card_transition.width,
            kCrossoverCommitPercent));
        return;
    }

    const int dx = info->stop_x - info->start_x;
    const int switch_distance = lv_disp_get_hor_res(nullptr) / 2;
    const bool previous = (info->start_area & ESP_BROOKESIA_GESTURE_AREA_LEFT_EDGE) &&
                          info->direction == ESP_BROOKESIA_GESTURE_DIR_RIGHT &&
                          dx >= switch_distance;
    const bool next = (info->start_area & ESP_BROOKESIA_GESTURE_AREA_RIGHT_EDGE) &&
                      info->direction == ESP_BROOKESIA_GESTURE_DIR_LEFT &&
                      dx <= -switch_distance;

    if (previous && s_current_index > 0) {
        (void)start_card(s_current_index - 1);
    } else if (next && s_current_index + 1 < crystal_registry_installed_count()) {
        (void)start_card(s_current_index + 1);
    }
}
}

bool crystal_shell_init(ESP_Brookesia_Phone *phone)
{
    if (phone == nullptr || crystal_registry_installed_count() == 0) {
        return false;
    }

    ESP_Brookesia_Gesture *gesture = phone->getManager().getGesture();
    if (gesture == nullptr || gesture->getEventObj() == nullptr) {
        ESP_LOGE(TAG, "Brookesia gesture source is unavailable");
        return false;
    }

    s_phone = phone;
    s_gesture = gesture;
    s_current_index = load_current_index();
    s_pane_cache.resize(crystal_registry_installed_count());
    lv_obj_add_event_cb(gesture->getEventObj(), on_gesture_press,
                        gesture->getPressEventCode(), nullptr);
    lv_obj_add_event_cb(gesture->getEventObj(), on_gesture_pressing,
                        gesture->getPressingEventCode(), nullptr);
    lv_obj_add_event_cb(gesture->getEventObj(), on_gesture_release,
                        gesture->getReleaseEventCode(), nullptr);
    if (!phone->registerAppEventCallback(on_app_event, nullptr)) {
        ESP_LOGE(TAG, "failed to observe app starts");
        return false;
    }

    if (!start_card(s_current_index) && s_current_index != 0) {
        ESP_LOGW(TAG, "saved card unavailable; falling back to slot 0");
        if (!start_card(0)) {
            return false;
        }
    }
    return phone->getManager().getActiveApp() != nullptr && init_indicator_overlay();
}

static void wifi_open_credentials(const char *ssid)
{
    lv_obj_t *parent = s_wifi_page != nullptr ? s_wifi_page : s_quick_panel;
    if (ssid == nullptr || parent == nullptr) return;
    strlcpy(s_wifi_selected, ssid, sizeof(s_wifi_selected));
    if (s_wifi_dialog != nullptr) lv_obj_del(s_wifi_dialog);
    s_wifi_dialog = lv_obj_create(parent);
    lv_obj_set_size(s_wifi_dialog, 450, 395);
    lv_obj_center(s_wifi_dialog);
    lv_obj_set_style_bg_color(s_wifi_dialog, lv_color_hex(0x252a30), 0);
    lv_obj_set_style_bg_opa(s_wifi_dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_wifi_dialog, lv_color_hex(0x59636e), 0);
    lv_obj_set_style_border_width(s_wifi_dialog, 1, 0);
    lv_obj_clear_flag(s_wifi_dialog, LV_OBJ_FLAG_SCROLLABLE);
    crystal_shell_set_modal_open(true);

    lv_obj_t *back = lv_btn_create(s_wifi_dialog);
    lv_obj_set_size(back, 78, 34);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 10, 8);
    lv_obj_t *back_label = lv_label_create(back);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back, [](lv_event_t *) {
        if (s_wifi_dialog != nullptr) {
            lv_obj_del(s_wifi_dialog);
            s_wifi_dialog = nullptr;
            crystal_shell_set_modal_open(false);
        }
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *title = lv_label_create(s_wifi_dialog);
    lv_label_set_text(title, "Enter WiFi password");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 28, 8);

    lv_obj_t *network = lv_label_create(s_wifi_dialog);
    char network_text[48];
    snprintf(network_text, sizeof(network_text), "Network: %.32s", s_wifi_selected);
    lv_label_set_text(network, network_text);
    lv_obj_set_style_text_color(network, lv_color_hex(0xcbd5e1), 0);
    lv_obj_align(network, LV_ALIGN_TOP_MID, 0, 38);

    lv_obj_t *input = lv_textarea_create(s_wifi_dialog);
    lv_obj_set_size(input, 400, 44);
    lv_obj_align(input, LV_ALIGN_TOP_MID, 0, 62);
    lv_textarea_set_password_mode(input, true);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_placeholder_text(input, "Password");

    lv_obj_t *connect = lv_btn_create(s_wifi_dialog);
    lv_obj_set_size(connect, 130, 40);
    lv_obj_align(connect, LV_ALIGN_TOP_MID, 0, 116);
    lv_obj_t *label = lv_label_create(connect);
    lv_label_set_text(label, "Connect");
    lv_obj_center(label);
    lv_obj_add_event_cb(connect, [](lv_event_t *e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto *password = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
        if (hal().wifi != nullptr && password != nullptr) {
            hal().wifi->connect(s_wifi_selected, lv_textarea_get_text(password));
        }
        lv_obj_del(s_wifi_dialog);
        s_wifi_dialog = nullptr;
        crystal_shell_set_modal_open(false);
    }, LV_EVENT_CLICKED, input);

    lv_obj_t *keyboard = lv_keyboard_create(s_wifi_dialog);
    lv_obj_set_size(keyboard, 420, 190);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(keyboard, input);
    lv_textarea_set_cursor_click_pos(input, true);
    lv_group_t *group = lv_group_get_default(); if (group != nullptr) lv_group_focus_obj(input);
}

static void wifi_open_forget_confirm(const char *ssid)
{
    if (s_wifi_page == nullptr || ssid == nullptr) return;
    lv_obj_t *box = lv_obj_create(s_wifi_page);
    if (box == nullptr) return;
    lv_obj_set_size(box, 350, 150); lv_obj_center(box);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x252a30), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lv_label_create(box); lv_label_set_text(title, "Forget network?");
    lv_obj_set_style_text_color(title, lv_color_white(), 0); lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_t *name = lv_label_create(box); char text[40]; snprintf(text, sizeof(text), "%.32s", ssid);
    lv_label_set_text(name, text); lv_obj_set_style_text_color(name, lv_color_hex(0xcbd5e1), 0); lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_t *forget = lv_btn_create(box); lv_obj_set_size(forget, 120, 38); lv_obj_align(forget, LV_ALIGN_BOTTOM_LEFT, 28, -14);
    lv_obj_t *fl = lv_label_create(forget); lv_label_set_text(fl, "Forget"); lv_obj_center(fl);
    lv_obj_add_event_cb(forget, [](lv_event_t *e) { if (hal().wifi != nullptr) hal().wifi->forget(); lv_obj_del(lv_obj_get_parent(lv_event_get_current_target(e))); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cancel = lv_btn_create(box); lv_obj_set_size(cancel, 120, 38); lv_obj_align(cancel, LV_ALIGN_BOTTOM_RIGHT, -28, -14);
    lv_obj_t *cl = lv_label_create(cancel); lv_label_set_text(cl, "Cancel"); lv_obj_center(cl);
    lv_obj_add_event_cb(cancel, [](lv_event_t *e) { lv_obj_del(lv_obj_get_parent(lv_event_get_current_target(e))); }, LV_EVENT_CLICKED, nullptr);
}

void wifi_tile_text(char *out, size_t size)
{
    IWifi *wifi = hal().wifi;
    if (wifi == nullptr || !wifi->enabled()) {
        strlcpy(out, LV_SYMBOL_WIFI "\nWiFi\nOff", size);
    } else if (wifi->connected()) {
        snprintf(out, size, LV_SYMBOL_WIFI "\nWiFi\n%.32s", wifi->last_ssid());
    } else {
        strlcpy(out, LV_SYMBOL_WIFI "\nWiFi\nOn\nNot Connected", size);
    }
}

// The only teardown path for the WiFi page. Every cached pointer into the page
// tree is cleared here, because lv_obj_del() frees the children too and a stale
// pointer passes the != nullptr guards at every use site.
void wifi_page_close()
{
    if (s_wifi_page == nullptr) return;
    lv_obj_del(s_wifi_page);
    s_wifi_page = nullptr;
    s_wifi_page_list = nullptr;
    s_wifi_page_status = nullptr;
    if (s_wifi_dialog != nullptr) {
        s_wifi_dialog = nullptr;
        crystal_shell_set_modal_open(false);
    }
    crystal_shell_set_settings_open(false);
}

void wifi_page_open()
{
    if (s_wifi_page != nullptr) return;
    s_wifi_page = lv_obj_create(lv_layer_top());
    lv_area_t area = active_app_area();
    lv_obj_set_size(s_wifi_page, lv_area_get_width(&area), lv_area_get_height(&area));
    lv_obj_set_pos(s_wifi_page, area.x1, area.y1);
    lv_obj_set_style_bg_color(s_wifi_page, lv_color_hex(0x11151b), 0);
    lv_obj_set_style_bg_opa(s_wifi_page, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_wifi_page, LV_OBJ_FLAG_SCROLLABLE);
    crystal_shell_set_settings_open(true);
    lv_obj_t *back = lv_btn_create(s_wifi_page); lv_obj_set_size(back, 76, 38); lv_obj_align(back, LV_ALIGN_TOP_LEFT, 10, 8);
    lv_obj_t *back_label = lv_label_create(back); lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back"); lv_obj_set_style_text_color(back_label, lv_color_white(), 0); lv_obj_center(back_label);
    lv_obj_add_event_cb(back, [](lv_event_t *) { wifi_page_close(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *title = lv_label_create(s_wifi_page); lv_label_set_text(title, "WiFi Networks"); lv_obj_set_style_text_color(title, lv_color_white(), 0); lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0); lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    s_wifi_page_status = lv_label_create(s_wifi_page); lv_label_set_text(s_wifi_page_status, "Scanning..."); lv_obj_set_style_text_color(s_wifi_page_status, lv_color_hex(0xcbd5e1), 0); lv_obj_align(s_wifi_page_status, LV_ALIGN_TOP_MID, 0, 38);
    s_wifi_page_list = lv_list_create(s_wifi_page); lv_obj_set_size(s_wifi_page_list, LV_PCT(100), lv_area_get_height(&area) - 92); lv_obj_align(s_wifi_page_list, LV_ALIGN_TOP_MID, 0, 74); lv_obj_set_style_bg_color(s_wifi_page_list, lv_color_hex(0x1b2028), 0); lv_obj_set_style_bg_opa(s_wifi_page_list, LV_OPA_COVER, 0);
    lv_obj_t *scanning = lv_list_add_text(s_wifi_page_list, "Scanning...");
    if (scanning != nullptr) lv_obj_set_style_text_color(scanning, lv_color_white(), 0);
    if (hal().wifi != nullptr) hal().wifi->scan();
}

static void wifi_page_fill_list()
{
    if (s_wifi_page == nullptr || s_wifi_page_list == nullptr || hal().wifi == nullptr) return;
    lv_obj_clean(s_wifi_page_list);
    IWifi::Network networks[20] = {};
    const size_t count = hal().wifi->scan_results(networks, 20);
    if (count == 0) { if (s_wifi_page_status != nullptr) lv_label_set_text(s_wifi_page_status, "No networks found"); lv_obj_t *empty = lv_list_add_text(s_wifi_page_list, "No networks found"); if (empty != nullptr) lv_obj_set_style_text_color(empty, lv_color_white(), 0); return; }
    if (s_wifi_page_status != nullptr) lv_label_set_text(s_wifi_page_status, "Select a network");
    for (size_t i = 0; i < count && i < 20; ++i) {
        if (networks[i].ssid[0] == 0) continue;
        IWifi *wifi = hal().wifi;
        const bool connected = wifi != nullptr && wifi->connected() &&
                               strcmp(networks[i].ssid, wifi->last_ssid()) == 0;
        const bool attempting = strcmp(networks[i].ssid, s_wifi_connecting) == 0;
        char text[64]; snprintf(text, sizeof(text), "%s%.32s  %d%.3s", connected ? LV_SYMBOL_OK " " : "", networks[i].ssid, networks[i].rssi, networks[i].secured ? "  *" : "");
        lv_obj_t *button = lv_list_add_btn(s_wifi_page_list, networks[i].secured ? LV_SYMBOL_WIFI : LV_SYMBOL_WIFI, text);
        if (button != nullptr) {
            lv_obj_set_style_text_color(button, lv_color_white(), 0);
            lv_obj_set_style_bg_color(button, lv_color_hex(0x252a30), 0);
            lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
            if (attempting) {
                lv_obj_set_style_bg_color(button, lv_color_hex(0x2563eb), 0);
                lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
            }
            for (uint32_t child_index = 0; ; ++child_index) {
                lv_obj_t *child = lv_obj_get_child(button, static_cast<int32_t>(child_index));
                if (child == nullptr) break;
                lv_obj_set_style_text_color(child, lv_color_white(), 0);
            }
            lv_obj_add_event_cb(button, [](lv_event_t *e) { size_t index = reinterpret_cast<size_t>(lv_event_get_user_data(e)); IWifi::Network rows[20] = {}; if (hal().wifi != nullptr && hal().wifi->scan_results(rows, 20) > index) { if (hal().wifi->connected() && strcmp(rows[index].ssid, hal().wifi->last_ssid()) == 0) wifi_open_forget_confirm(rows[index].ssid); else wifi_open_credentials(rows[index].ssid); } }, LV_EVENT_CLICKED, reinterpret_cast<void *>(i));
        }
    }
}

void crystal_shell_wifi_event(uint8_t event)
{
    if (event == UI_EVT_WIFI_GOT_IP) {
        s_wifi_connecting[0] = '\0';
        if (s_quick_wifi != nullptr) lv_obj_add_state(s_quick_wifi, LV_STATE_CHECKED);
        wifi_page_close();
        if (s_quick_wifi == nullptr) return;
        lv_obj_t *label = lv_obj_get_child(s_quick_wifi, 0);
        if (label != nullptr) { char text[64]; wifi_tile_text(text, sizeof(text)); lv_label_set_text(label, text); }
    } else if (event == UI_EVT_WIFI_CONNECTING) {
        if (s_quick_wifi != nullptr) lv_obj_add_state(s_quick_wifi, LV_STATE_CHECKED);
        const char *ssid = hal().wifi != nullptr ? hal().wifi->last_ssid() : "";
        strlcpy(s_wifi_connecting, ssid, sizeof(s_wifi_connecting));
        if (s_quick_wifi != nullptr) { lv_obj_t *label = lv_obj_get_child(s_quick_wifi, 0); if (label != nullptr) { char text[64]; wifi_tile_text(text, sizeof(text)); lv_label_set_text(label, text); } }
        if (s_wifi_page != nullptr && s_wifi_page_status != nullptr) { char text[64]; snprintf(text, sizeof(text), "Connecting to %.32s...", ssid); lv_label_set_text(s_wifi_page_status, text); }
        wifi_page_fill_list();
    } else if (event == UI_EVT_WIFI_DISCONNECTED) {
        s_wifi_connecting[0] = '\0';
        wifi_page_fill_list();
        if (s_quick_wifi == nullptr) return;
        if (hal().wifi != nullptr && hal().wifi->enabled()) lv_obj_add_state(s_quick_wifi, LV_STATE_CHECKED);
        else lv_obj_clear_state(s_quick_wifi, LV_STATE_CHECKED);
        lv_obj_t *label = lv_obj_get_child(s_quick_wifi, 0);
        if (label != nullptr) { char text[64]; wifi_tile_text(text, sizeof(text)); lv_label_set_text(label, text); }
    } else if (event == UI_EVT_WIFI_CONNECT_FAILED) {
        s_wifi_connecting[0] = '\0';
        wifi_page_fill_list();
        if (s_wifi_page != nullptr && s_wifi_page_status != nullptr) lv_label_set_text(s_wifi_page_status, "Failed to connect");
    } else if (event == UI_EVT_WIFI_SCAN_DONE) {
        wifi_page_fill_list();
    }
}

CrystalGestureOwner crystal_shell_gesture_owner() { return s_gesture_owner; }
void crystal_shell_set_quick_settings_open(bool open) { s_quick_settings_open = open; }
void crystal_shell_set_keyboard_open(bool open) { s_keyboard_open = open; }
void crystal_shell_set_settings_open(bool open) { s_settings_open = open; }
void crystal_shell_set_modal_open(bool open) { s_modal_open = open; }
