/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "esp_brookesia.hpp"

class PerfSpike final : public ESP_Brookesia_PhoneApp {
public:
    PerfSpike();

    bool init() override;
    bool run() override;
    bool back() override;
    bool close() override;

private:
    static void card_event_cb(lv_event_t *event);
    static void stats_timer_cb(lv_timer_t *timer);

    lv_obj_t *card_ = nullptr;
    lv_point_t press_point_{};
    int32_t card_start_x_ = 0;
    bool dragging_ = false;
};
