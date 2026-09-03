/* SPDX-License-Identifier: MIT */
#pragma once

#include "crystal_app.hpp"

class StateTestApp final : public CrystalApp {
public:
    StateTestApp();

protected:
    bool onCreate() override;
    bool onResume() override;
    bool onPause() override;

private:
    static void increment_cb(lv_event_t *event);
    static void toggle_hello_cb(lv_event_t *event);
    static void swap_order_cb(lv_event_t *event);
    static void return_cb(lv_event_t *event);
    void refresh_count();
    void refresh_registry_status();

    lv_obj_t *count_label_ = nullptr;
    lv_obj_t *hello_button_label_ = nullptr;
    lv_obj_t *order_button_label_ = nullptr;
    uint32_t counter_ = 0;
};
