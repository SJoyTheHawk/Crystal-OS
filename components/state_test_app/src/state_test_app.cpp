/* SPDX-License-Identifier: MIT */

#include "state_test_app.hpp"

#include <stdio.h>

#include "crystal_registry.hpp"
#include "lvgl.h"

namespace {
constexpr const char *kHelloId = "hello";
constexpr const char *kStateTestId = "state_test";
}

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
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    count_label_ = lv_label_create(root);
    lv_obj_set_style_text_font(count_label_, &lv_font_montserrat_20, 0);
    lv_obj_align(count_label_, LV_ALIGN_TOP_MID, 0, 66);
    (void)state().get_u32("counter", &counter_);
    refresh_count();

    lv_obj_t *increment = lv_btn_create(root);
    lv_obj_set_size(increment, 260, 52);
    lv_obj_align(increment, LV_ALIGN_TOP_MID, 0, 104);
    lv_obj_add_event_cb(increment, increment_cb, LV_EVENT_CLICKED, this);
    lv_obj_t *increment_label = lv_label_create(increment);
    lv_label_set_text(increment_label, "Increment");
    lv_obj_center(increment_label);

    lv_obj_t *toggle_hello = lv_btn_create(root);
    lv_obj_set_size(toggle_hello, 260, 52);
    lv_obj_align(toggle_hello, LV_ALIGN_TOP_MID, 0, 170);
    lv_obj_add_event_cb(toggle_hello, toggle_hello_cb, LV_EVENT_CLICKED, this);
    hello_button_label_ = lv_label_create(toggle_hello);
    lv_obj_center(hello_button_label_);

    lv_obj_t *swap_order = lv_btn_create(root);
    lv_obj_set_size(swap_order, 260, 52);
    lv_obj_align(swap_order, LV_ALIGN_TOP_MID, 0, 236);
    lv_obj_add_event_cb(swap_order, swap_order_cb, LV_EVENT_CLICKED, this);
    order_button_label_ = lv_label_create(swap_order);
    lv_obj_center(order_button_label_);
    refresh_registry_status();

    lv_obj_t *back = lv_btn_create(root);
    lv_obj_set_size(back, 260, 52);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_add_event_cb(back, return_cb, LV_EVENT_CLICKED, this);
    lv_obj_t *back_label = lv_label_create(back);
    lv_label_set_text(back_label, "Return to launcher");
    lv_obj_center(back_label);
    return true;
}

bool StateTestApp::onResume()
{
    refresh_count();
    refresh_registry_status();
    return true;
}

bool StateTestApp::onPause()
{
    return state().set_u32("counter", counter_);
}

void StateTestApp::refresh_count()
{
    if (count_label_ == nullptr) {
        return;
    }
    char text[48];
    snprintf(text, sizeof(text), "Counter: %lu", static_cast<unsigned long>(counter_));
    lv_label_set_text(count_label_, text);
}

void StateTestApp::increment_cb(lv_event_t *event)
{
    auto *app = static_cast<StateTestApp *>(lv_event_get_user_data(event));
    ++app->counter_;
    app->refresh_count();
}

void StateTestApp::refresh_registry_status()
{
    const bool hello_enabled = crystal_registry_enabled(kHelloId, true);
    const uint16_t hello_slot = crystal_registry_slot(kHelloId, 0);
    const uint16_t state_slot = crystal_registry_slot(kStateTestId, 1);
    lv_label_set_text(hello_button_label_, hello_enabled ? "Disable Hello next boot" :
                                                  "Enable Hello next boot");
    lv_label_set_text(order_button_label_, hello_slot < state_slot ?
                                                  "State Test first next boot" :
                                                  "Hello first next boot");
}

void StateTestApp::toggle_hello_cb(lv_event_t *event)
{
    auto *app = static_cast<StateTestApp *>(lv_event_get_user_data(event));
    const bool enabled = crystal_registry_enabled(kHelloId, true);
    (void)crystal_registry_set_enabled(kHelloId, !enabled);
    app->refresh_registry_status();
}

void StateTestApp::swap_order_cb(lv_event_t *event)
{
    auto *app = static_cast<StateTestApp *>(lv_event_get_user_data(event));
    const uint16_t hello_slot = crystal_registry_slot(kHelloId, 0);
    const uint16_t state_slot = crystal_registry_slot(kStateTestId, 1);
    (void)crystal_registry_set_slot(kHelloId, state_slot);
    (void)crystal_registry_set_slot(kStateTestId, hello_slot);
    app->refresh_registry_status();
}

void StateTestApp::return_cb(lv_event_t *event)
{
    auto *app = static_cast<StateTestApp *>(lv_event_get_user_data(event));
    (void)app->notifyCoreClosed();
}
