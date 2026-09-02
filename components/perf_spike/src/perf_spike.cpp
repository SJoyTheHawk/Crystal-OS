/*
 * SPDX-License-Identifier: MIT
 */

#include "perf_spike.hpp"

#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "phase1_perf";

PerfSpike::PerfSpike()
    : ESP_Brookesia_PhoneApp("Phase 1 Perf", nullptr, true)
{
}

bool PerfSpike::init()
{
    return true;
}

bool PerfSpike::run()
{
    const lv_area_t area = getVisualArea();
    const lv_coord_t width = area.x2 - area.x1 + 1;
    const lv_coord_t height = area.y2 - area.y1 + 1;

    lv_obj_t *root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(root, width, height);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x101827), 0);

    // Approximate the low-resolution blurred snapshot used by the future switcher.
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            lv_obj_t *tile = lv_obj_create(root);
            lv_obj_set_size(tile, width / 8 + 2, height / 8 + 2);
            lv_obj_set_pos(tile, col * width / 8 - 1, row * height / 8 - 1);
            lv_obj_set_style_radius(tile, 0, 0);
            lv_obj_set_style_border_width(tile, 0, 0);
            lv_obj_set_style_pad_all(tile, 0, 0);
            const uint32_t color = 0x14283a + static_cast<uint32_t>((row * 8 + col) * 0x030507);
            lv_obj_set_style_bg_color(tile, lv_color_hex(color & 0xFFFFFF), 0);
            lv_obj_clear_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        }
    }

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "PHASE 1 PERFORMANCE SPIKE");
    lv_obj_set_style_text_color(title, lv_color_hex(0xB7D7F5), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *hint = lv_label_create(root);
    lv_label_set_text(hint, "Drag the card left and right for 10 seconds");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x89A1B8), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 44);

    card_ = lv_obj_create(root);
    lv_obj_set_size(card_, 300, height - 130);
    lv_obj_align(card_, LV_ALIGN_CENTER, 0, 28);
    lv_obj_set_style_radius(card_, 12, 0);
    lv_obj_set_style_border_width(card_, 2, 0);
    lv_obj_set_style_border_color(card_, lv_color_hex(0x63B3ED), 0);
    lv_obj_set_style_bg_color(card_, lv_color_hex(0x263B54), 0);
    lv_obj_set_style_shadow_width(card_, 18, 0);
    lv_obj_set_style_shadow_opa(card_, LV_OPA_50, 0);
    lv_obj_add_event_cb(card_, card_event_cb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(card_, card_event_cb, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(card_, card_event_cb, LV_EVENT_RELEASED, this);

    lv_obj_t *card_label = lv_label_create(card_);
    lv_label_set_text(card_label, "LIVE CARD\n\nFull-screen drag workload");
    lv_obj_set_style_text_align(card_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(card_label);

    lv_timer_create(stats_timer_cb, 1000, this);
    ESP_LOGI(TAG, "spike ready: RGB buffer count=%d; drag card continuously", CONFIG_BSP_LCD_RGB_BUFFER_NUMS);
    return true;
}

bool PerfSpike::back()
{
    notifyCoreClosed();
    return true;
}

bool PerfSpike::close()
{
    return true;
}

void PerfSpike::card_event_cb(lv_event_t *event)
{
    auto *app = static_cast<PerfSpike *>(lv_event_get_user_data(event));
    lv_point_t point;
    lv_indev_get_point(lv_indev_get_act(), &point);
    const lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_PRESSED) {
        app->press_point_ = point;
        app->card_start_x_ = lv_obj_get_x(app->card_);
        app->dragging_ = true;
    } else if (code == LV_EVENT_PRESSING) {
        const lv_coord_t dx = point.x - app->press_point_.x;
        const lv_coord_t max_x = 480 - lv_obj_get_width(app->card_);
        lv_obj_set_x(app->card_, LV_CLAMP(0, app->card_start_x_ + dx, max_x));
    } else if (code == LV_EVENT_RELEASED) {
        app->dragging_ = false;
    }
}

void PerfSpike::stats_timer_cb(lv_timer_t *timer)
{
    auto *app = static_cast<PerfSpike *>(timer->user_data);
    if (app->dragging_) {
        ESP_LOGI(TAG, "drag FPS avg=%" LV_PRIu32 " (RGB buffers=%d)",
                 lv_refr_get_fps_avg(), CONFIG_BSP_LCD_RGB_BUFFER_NUMS);
    }
}
