/* SPDX-License-Identifier: MIT */
#pragma once

#include <vector>

#include "crystal_app.hpp"

LV_IMG_DECLARE(clock_icon);
extern "C" void clock_icon_prepare(void);

class ClockApp final : public CrystalApp {
public:
    ClockApp();

protected:
    bool onCreate() override;
    bool onPause() override;
    bool onResume() override;
    bool onDestroy() override;

private:
    static void timer_preset_cb(lv_event_t *event);
    static void timer_action_cb(lv_event_t *event);
    static void stopwatch_action_cb(lv_event_t *event);
    static void tick_cb(lv_timer_t *timer);
    void refresh();
    void refresh_timer();
    void refresh_stopwatch();
    void save_stopwatch();
    void refresh_laps();

    lv_obj_t *clock_label_ = nullptr;
    lv_obj_t *date_label_ = nullptr;
    lv_obj_t *timezone_label_ = nullptr;
    lv_obj_t *timer_label_ = nullptr;
    lv_obj_t *timer_status_ = nullptr;
    lv_obj_t *timer_arc_ = nullptr;
    lv_obj_t *stopwatch_label_ = nullptr;
    lv_obj_t *stopwatch_status_ = nullptr;
    lv_obj_t *lap_label_ = nullptr;
    lv_timer_t *ui_timer_ = nullptr;
    uint32_t selected_duration_ = 180;
    bool stopwatch_running_ = false;
    bool stopwatch_paused_ = false;
    uint32_t stopwatch_elapsed_ = 0;
    uint32_t stopwatch_started_ = 0;
    bool boot_setup_done_ = false;
    std::vector<uint32_t> laps_;
};
