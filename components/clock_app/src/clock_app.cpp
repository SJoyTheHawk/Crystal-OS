/* SPDX-License-Identifier: MIT */

#include "clock_app.hpp"

#include <stdio.h>
#include <time.h>

#include "crystal_core.hpp"
#include "lvgl.h"

namespace {
lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, 120, 46);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

void set_label_font(lv_obj_t *label, const lv_font_t *font)
{
    lv_obj_set_style_text_font(label, font, 0);
}

void format_seconds(uint32_t seconds, char *out, size_t out_size)
{
    snprintf(out, out_size, "%02lu:%02lu", static_cast<unsigned long>(seconds / 60),
             static_cast<unsigned long>(seconds % 60));
}
}

ClockApp::ClockApp()
    : CrystalApp("Clock", &clock_icon)
{
    clock_icon_prepare();
}

bool ClockApp::onCreate()
{
    if (!boot_setup_done_) {
        boot_setup_done_ = true;
        crystal_timer_reset();
        (void)state().erase("tm_end");
        (void)state().erase("tm_ps");
        (void)state().erase("tm_dur");

        uint32_t stopwatch_started = 0;
        (void)state().get_u32("sw_st", &stopwatch_started);
        crystal_stopwatch_set_running(stopwatch_started != 0);
    }

    const lv_area_t area = getVisualArea();
    const lv_coord_t width = area.x2 - area.x1 + 1;
    const lv_coord_t height = area.y2 - area.y1 + 1;
    lv_obj_t *root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(root, width, height);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    lv_obj_t *tabs = lv_tabview_create(root, LV_DIR_TOP, 44);
    lv_obj_set_size(tabs, width, height - 40);
    lv_obj_t *clock_tab = lv_tabview_add_tab(tabs, "Clock");
    lv_obj_t *timer_tab = lv_tabview_add_tab(tabs, "Timer");
    lv_obj_t *stopwatch_tab = lv_tabview_add_tab(tabs, "Stopwatch");
    lv_obj_clear_flag(lv_tabview_get_content(tabs), LV_OBJ_FLAG_SCROLLABLE);

    clock_label_ = lv_label_create(clock_tab);
    set_label_font(clock_label_, &lv_font_montserrat_28);
    lv_obj_align(clock_label_, LV_ALIGN_CENTER, 0, -45);
    date_label_ = lv_label_create(clock_tab);
    set_label_font(date_label_, &lv_font_montserrat_20);
    lv_obj_align(date_label_, LV_ALIGN_CENTER, 0, 5);
    timezone_label_ = lv_label_create(clock_tab);
    lv_obj_align(timezone_label_, LV_ALIGN_CENTER, 0, 38);
    lv_label_set_text(timezone_label_, "Hong Kong (UTC+08:00)");

    timer_arc_ = lv_arc_create(timer_tab);
    lv_obj_set_size(timer_arc_, 92, 92);
    lv_obj_align(timer_arc_, LV_ALIGN_TOP_MID, 0, 2);
    lv_arc_set_range(timer_arc_, 0, 100);
    lv_arc_set_bg_angles(timer_arc_, 0, 360);
    lv_obj_remove_style(timer_arc_, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(timer_arc_, LV_OBJ_FLAG_CLICKABLE);
    timer_label_ = lv_label_create(timer_tab);
    set_label_font(timer_label_, &lv_font_montserrat_28);
    lv_obj_align(timer_label_, LV_ALIGN_TOP_MID, 0, 29);
    timer_status_ = lv_label_create(timer_tab);
    lv_obj_align(timer_status_, LV_ALIGN_TOP_MID, 0, 76);
    lv_label_set_text(timer_status_, "Ready");
    const uint32_t presets[] = {30, 60, 180, 300, 600, 1200, 1800};
    const char *preset_names[] = {"30s", "1m", "3m", "5m", "10m", "20m", "30m"};
    for (size_t i = 0; i < 7; ++i) {
        lv_obj_t *button = make_button(timer_tab, preset_names[i], timer_preset_cb, this);
        lv_obj_set_size(button, 96, 42);
        lv_obj_set_pos(button, 8 + static_cast<lv_coord_t>((i % 4) * 108),
                       104 + static_cast<lv_coord_t>((i / 4) * 48));
        lv_obj_set_user_data(button, reinterpret_cast<void *>(static_cast<uintptr_t>(presets[i])));
    }
    lv_obj_t *minus = make_button(timer_tab, "-30s", timer_action_cb, this);
    lv_obj_set_size(minus, 96, 42);
    lv_obj_set_pos(minus, 8 + 3 * 108, 152);
    lv_obj_set_user_data(minus, reinterpret_cast<void *>(5));
    lv_obj_t *plus = make_button(timer_tab, "+30s", timer_action_cb, this);
    lv_obj_set_size(plus, 96, 42);
    lv_obj_set_pos(plus, 8, 204);
    lv_obj_set_user_data(plus, reinterpret_cast<void *>(6));
    lv_obj_t *start = make_button(timer_tab, "Start", timer_action_cb, this);
    lv_obj_set_size(start, 96, 42);
    lv_obj_set_pos(start, 116, 204);
    lv_obj_set_user_data(start, reinterpret_cast<void *>(1));
    lv_obj_t *pause = make_button(timer_tab, "Pause", timer_action_cb, this);
    lv_obj_set_size(pause, 96, 42);
    lv_obj_set_pos(pause, 224, 204);
    lv_obj_set_user_data(pause, reinterpret_cast<void *>(2));
    lv_obj_t *resume = make_button(timer_tab, "Resume", timer_action_cb, this);
    lv_obj_set_size(resume, 96, 42);
    lv_obj_set_pos(resume, 332, 204);
    lv_obj_set_user_data(resume, reinterpret_cast<void *>(3));
    lv_obj_t *reset = make_button(timer_tab, "Reset", timer_action_cb, this);
    lv_obj_set_size(reset, 150, 42);
    lv_obj_set_pos(reset, 149, 256);
    lv_obj_set_user_data(reset, reinterpret_cast<void *>(4));

    stopwatch_label_ = lv_label_create(stopwatch_tab);
    set_label_font(stopwatch_label_, &lv_font_montserrat_28);
    lv_obj_align(stopwatch_label_, LV_ALIGN_TOP_MID, 0, 42);
    stopwatch_status_ = lv_label_create(stopwatch_tab);
    lv_obj_align(stopwatch_status_, LV_ALIGN_TOP_MID, 0, 86);
    lv_obj_t *sw_start = make_button(stopwatch_tab, "Start/Pause", stopwatch_action_cb, this);
    lv_obj_set_pos(sw_start, 14, 120);
    lv_obj_set_user_data(sw_start, reinterpret_cast<void *>(1));
    lv_obj_t *sw_lap = make_button(stopwatch_tab, "Lap", stopwatch_action_cb, this);
    lv_obj_set_pos(sw_lap, 146, 120);
    lv_obj_set_user_data(sw_lap, reinterpret_cast<void *>(2));
    lv_obj_t *sw_reset = make_button(stopwatch_tab, "Reset", stopwatch_action_cb, this);
    lv_obj_set_pos(sw_reset, 278, 120);
    lv_obj_set_user_data(sw_reset, reinterpret_cast<void *>(3));
    lv_obj_t *sw_resume = make_button(stopwatch_tab, "Resume", stopwatch_action_cb, this);
    lv_obj_set_pos(sw_resume, 146, 176);
    lv_obj_set_user_data(sw_resume, reinterpret_cast<void *>(4));
    lap_label_ = lv_label_create(stopwatch_tab);
    lv_obj_set_width(lap_label_, 360);
    lv_obj_set_style_text_align(lap_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lap_label_, LV_ALIGN_TOP_MID, 0, 235);

    uint32_t timer_end = 0;
    uint32_t timer_pause = 0;
    uint32_t timer_duration = 0;
    (void)state().get_u32("tm_end", &timer_end);
    (void)state().get_u32("tm_ps", &timer_pause);
    (void)state().get_u32("tm_dur", &timer_duration);
    if (timer_duration != 0 && (timer_end != 0 || timer_pause != 0)) {
        if (crystal_timer_restore(static_cast<time_t>(timer_end), timer_pause, timer_duration)) {
            selected_duration_ = timer_duration;
        }
    }
    (void)state().get_u32("sw_el", &stopwatch_elapsed_);
    (void)state().get_u32("sw_st", &stopwatch_started_);
    stopwatch_running_ = stopwatch_started_ != 0;
    stopwatch_paused_ = !stopwatch_running_ && stopwatch_elapsed_ != 0;
    crystal_stopwatch_set_running(stopwatch_running_);
    uint32_t lap_values[50] = {};
    size_t lap_bytes = sizeof(lap_values);
    if (state().get("sw_laps", lap_values, &lap_bytes) && lap_bytes % sizeof(uint32_t) == 0) {
        laps_.assign(lap_values, lap_values + lap_bytes / sizeof(uint32_t));
    }
    refresh_laps();

    refresh();
    ui_timer_ = lv_timer_create(tick_cb, 200, this);
    return ui_timer_ != nullptr;
}

bool ClockApp::onPause()
{
    if (ui_timer_ != nullptr) {
        lv_timer_del(ui_timer_);
        ui_timer_ = nullptr;
    }
    save_stopwatch();
    const CrystalTimerState timer = crystal_timer_state();
    (void)state().set_u32("tm_end", static_cast<uint32_t>(timer.running ? timer.end_at : 0));
    (void)state().set_u32("tm_ps", timer.paused ? timer.remaining_seconds : 0);
    (void)state().set_u32("tm_dur", (timer.running || timer.paused) ? timer.duration_seconds : 0);
    return true;
}

bool ClockApp::onResume()
{
    refresh();
    if (ui_timer_ == nullptr) ui_timer_ = lv_timer_create(tick_cb, 200, this);
    return ui_timer_ != nullptr;
}

bool ClockApp::onDestroy()
{
    save_stopwatch();
    return true;
}

void ClockApp::save_stopwatch()
{
    uint32_t elapsed = stopwatch_elapsed_;
    if (stopwatch_running_ && stopwatch_started_ != 0) {
        const uint32_t now = static_cast<uint32_t>(time(nullptr));
        elapsed += now - stopwatch_started_;
        stopwatch_started_ = now;
    }
    (void)state().set_u32("sw_el", elapsed);
    (void)state().set_u32("sw_st", stopwatch_running_ ? stopwatch_started_ : 0);
    if (!laps_.empty()) {
        (void)state().set("sw_laps", laps_.data(), laps_.size() * sizeof(uint32_t));
    } else {
        (void)state().erase("sw_laps");
    }
    stopwatch_elapsed_ = elapsed;
}

void ClockApp::refresh()
{
    const time_t now = time(nullptr);
    struct tm local = {};
    if (localtime_r(&now, &local) != nullptr) {
        char clock_text[16];
        char date_text[32];
        snprintf(clock_text, sizeof(clock_text), "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);
        snprintf(date_text, sizeof(date_text), "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
        lv_label_set_text(clock_label_, clock_text);
        lv_label_set_text(date_label_, date_text);
    }
    refresh_timer();
    refresh_stopwatch();
}

void ClockApp::refresh_timer()
{
    const CrystalTimerState timer = crystal_timer_state();
    const uint32_t shown = (timer.running || timer.paused) ? timer.remaining_seconds : selected_duration_;
    char text[16];
    format_seconds(shown, text, sizeof(text));
    lv_label_set_text(timer_label_, text);
    lv_label_set_text(timer_status_, timer.running ? "Running" : (timer.paused ? "Paused" : "Ready"));
    const uint32_t denominator = selected_duration_ == 0 ? 1 : selected_duration_;
    lv_arc_set_value(timer_arc_, static_cast<int32_t>((shown > denominator ? denominator : shown) * 100 / denominator));
}

void ClockApp::refresh_stopwatch()
{
    uint32_t elapsed = stopwatch_elapsed_;
    if (stopwatch_running_ && stopwatch_started_ != 0) {
        elapsed += static_cast<uint32_t>(time(nullptr)) - stopwatch_started_;
    }
    char text[16];
    format_seconds(elapsed, text, sizeof(text));
    lv_label_set_text(stopwatch_label_, text);
    lv_label_set_text(stopwatch_status_, stopwatch_running_ ? "Running" : "Stopped");
}

void ClockApp::tick_cb(lv_timer_t *timer)
{
    static_cast<ClockApp *>(timer->user_data)->refresh();
}

void ClockApp::timer_preset_cb(lv_event_t *event)
{
    auto *app = static_cast<ClockApp *>(lv_event_get_user_data(event));
    app->selected_duration_ = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target(event))));
    crystal_timer_reset();
    app->refresh_timer();
}

void ClockApp::timer_action_cb(lv_event_t *event)
{
    auto *app = static_cast<ClockApp *>(lv_event_get_user_data(event));
    const uintptr_t action = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target(event)));
    if (action == 1) (void)crystal_timer_start(app->selected_duration_);
    else if (action == 2) (void)crystal_timer_pause();
    else if (action == 3) (void)crystal_timer_resume();
    else if (action == 4) crystal_timer_reset();
    else if (action == 5) {
        app->selected_duration_ = app->selected_duration_ > 30 ? app->selected_duration_ - 30 : 30;
        crystal_timer_reset();
    } else if (action == 6) {
        if (app->selected_duration_ < 24U * 60U * 60U - 30U) app->selected_duration_ += 30;
        crystal_timer_reset();
    }
    app->refresh_timer();
}

void ClockApp::stopwatch_action_cb(lv_event_t *event)
{
    auto *app = static_cast<ClockApp *>(lv_event_get_user_data(event));
    const uintptr_t action = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target(event)));
    if (action == 1) {
        if (!app->stopwatch_running_) {
            app->stopwatch_started_ = static_cast<uint32_t>(time(nullptr));
            app->stopwatch_running_ = true;
            app->stopwatch_paused_ = false;
            crystal_stopwatch_set_running(true);
        } else {
            const uint32_t now = static_cast<uint32_t>(time(nullptr));
            app->stopwatch_elapsed_ += now - app->stopwatch_started_;
            app->stopwatch_started_ = 0;
            app->stopwatch_running_ = false;
            app->stopwatch_paused_ = true;
            crystal_stopwatch_set_running(false);
            app->save_stopwatch();
        }
    } else if (action == 2) {
        if (app->stopwatch_running_) {
            app->laps_.push_back(app->stopwatch_elapsed_ + static_cast<uint32_t>(time(nullptr)) - app->stopwatch_started_);
            if (app->laps_.size() > 50) app->laps_.erase(app->laps_.begin());
            app->refresh_laps();
        }
    } else if (action == 3) {
        app->stopwatch_running_ = false;
        app->stopwatch_started_ = 0;
        app->stopwatch_elapsed_ = 0;
        app->stopwatch_paused_ = false;
        app->laps_.clear();
        crystal_stopwatch_set_running(false);
        app->save_stopwatch();
    } else if (action == 4 && app->stopwatch_paused_) {
        app->stopwatch_started_ = static_cast<uint32_t>(time(nullptr));
        app->stopwatch_running_ = true;
        app->stopwatch_paused_ = false;
        crystal_stopwatch_set_running(true);
    }
    app->refresh_stopwatch();
}

void ClockApp::refresh_laps()
{
    if (lap_label_ == nullptr) return;
    if (laps_.empty()) {
        lv_label_set_text(lap_label_, "No laps");
        return;
    }
    char text[192] = {};
    size_t used = 0;
    for (size_t i = laps_.size(); i > 0 && used + 16 < sizeof(text); --i) {
        char line[24];
        format_seconds(laps_[i - 1], line, sizeof(line));
        used += static_cast<size_t>(snprintf(text + used, sizeof(text) - used, "Lap %u  %s\n",
                                              static_cast<unsigned>(i), line));
    }
    lv_label_set_text(lap_label_, text);
}
