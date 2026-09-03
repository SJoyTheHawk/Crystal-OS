/* SPDX-License-Identifier: MIT */

#include "state_test_app.hpp"

#include <stdio.h>

#include "lvgl.h"

StateTestApp::StateTestApp()
    : CrystalApp("State Test")
{
}

bool StateTestApp::onCreate()
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

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "State Test");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    count_label_ = lv_label_create(root);
    lv_obj_set_style_text_font(count_label_, &lv_font_montserrat_20, 0);
    lv_obj_align(count_label_, LV_ALIGN_CENTER, 0, -30);
    refresh_count();

    lv_obj_t *increment = lv_btn_create(root);
    lv_obj_set_size(increment, 220, 60);
    lv_obj_align(increment, LV_ALIGN_CENTER, 0, 45);
    lv_obj_add_event_cb(increment, increment_cb, LV_EVENT_CLICKED, this);
    lv_obj_t *increment_label = lv_label_create(increment);
    lv_label_set_text(increment_label, "Increment");
    lv_obj_center(increment_label);

    lv_obj_t *back = lv_btn_create(root);
    lv_obj_set_size(back, 220, 54);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(back, return_cb, LV_EVENT_CLICKED, this);
    lv_obj_t *back_label = lv_label_create(back);
    lv_label_set_text(back_label, "Return to launcher");
    lv_obj_center(back_label);
    return true;
}

bool StateTestApp::onResume()
{
    refresh_count();
    return true;
}

void StateTestApp::refresh_count()
{
    if (count_label_ == nullptr) {
        return;
    }
    uint32_t count = 0;
    (void)state().get_u32("counter", &count);
    char text[48];
    snprintf(text, sizeof(text), "Saved counter: %lu", static_cast<unsigned long>(count));
    lv_label_set_text(count_label_, text);
}

void StateTestApp::increment_cb(lv_event_t *event)
{
    auto *app = static_cast<StateTestApp *>(lv_event_get_user_data(event));
    uint32_t count = 0;
    (void)app->state().get_u32("counter", &count);
    (void)app->state().set_u32("counter", ++count);
    app->refresh_count();
}

void StateTestApp::return_cb(lv_event_t *event)
{
    auto *app = static_cast<StateTestApp *>(lv_event_get_user_data(event));
    (void)app->notifyCoreClosed();
}
