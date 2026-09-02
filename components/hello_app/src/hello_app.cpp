/*
 * SPDX-License-Identifier: MIT
 */

#include "hello_app.hpp"

#include "esp_log.h"
#include "lvgl.h"

LV_IMG_DECLARE(hello_icon);
static const char *TAG = "hello_app";

HelloApp::HelloApp()
    : ESP_Brookesia_PhoneApp("Hello", &hello_icon, true)
{
    hello_icon_prepare();
}

bool HelloApp::init()
{
    return true;
}

bool HelloApp::run()
{
    const lv_area_t area = getVisualArea();
    const lv_coord_t width = area.x2 - area.x1 + 1;
    const lv_coord_t height = area.y2 - area.y1 + 1;
    ESP_LOGI(TAG, "visual area: x1=%d y1=%d x2=%d y2=%d (%dx%d)",
             area.x1, area.y1, area.x2, area.y2, width, height);

    // Brookesia creates the app screen immediately before run(). Size our root
    // explicitly so layout does not depend on the active display screen state.
    lv_obj_t *root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(root, width, height);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "Hello, Crystal!");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, height / 3);

    lv_obj_t *subtitle = lv_label_create(root);
    lv_label_set_text(subtitle, "Phase 0 boot baseline");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(subtitle, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);

    lv_obj_t *button = lv_btn_create(root);
    lv_obj_set_size(button, 260, 64);
    lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_add_event_cb(button, return_button_cb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(button, return_button_cb, LV_EVENT_PRESSED, this);

    lv_obj_t *button_label = lv_label_create(button);
    lv_label_set_text(button_label, "Return to launcher");
    lv_obj_set_style_text_font(button_label, &lv_font_montserrat_16, 0);
    lv_obj_center(button_label);

    return true;
}

bool HelloApp::back()
{
    notifyCoreClosed();
    return true;
}

bool HelloApp::close()
{
    return true;
}

void HelloApp::return_button_cb(lv_event_t *event)
{
    auto *app = static_cast<HelloApp *>(lv_event_get_user_data(event));
    lv_point_t point;
    lv_indev_get_point(lv_indev_get_act(), &point);
    const lv_event_code_t code = lv_event_get_code(event);
    ESP_LOGI(TAG, "button event=%d at (%d,%d)", static_cast<int>(code), point.x, point.y);
    if (code == LV_EVENT_CLICKED) {
        app->notifyCoreClosed();
    }
}
