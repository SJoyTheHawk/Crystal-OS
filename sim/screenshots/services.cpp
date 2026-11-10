/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
// Deterministic screenshot fixtures, never linked into firmware.
#include "crystal_core.hpp"

// 2026-11-10 10:09:00 HKT. Only app source files substitute their time() calls.
extern "C" time_t crystal_capture_time(time_t *out)
{
    const time_t value = 1794276540;
    if (out != nullptr) *out = value;
    return value;
}

bool crystal_time_format_24() { return true; }
void crystal_weather_request() {} // the runner supplies an explicitly sampled reading
void crystal_stopwatch_set_running(bool) {}
uint8_t crystal_brightness_level() { return 72; }
void crystal_brightness_set(uint8_t) {}
bool crystal_power_saving_enabled() { return false; }
void crystal_power_set_saving(bool) {}
bool crystal_power_auto_dim_enabled() { return true; }
void crystal_power_set_auto_dim(bool) {}
bool crystal_hal_set_volume(int) { return true; }
int crystal_hal_get_volume() { return 60; }
static CrystalTimerState timer{};
CrystalTimerState crystal_timer_state() { return timer; }
void crystal_timer_reset() { timer = {}; }
bool crystal_timer_start(uint32_t duration)
{
    timer = {true, false, duration, duration, crystal_capture_time(nullptr) + duration};
    return true;
}
bool crystal_timer_pause() { timer.running = false; timer.paused = true; return true; }
bool crystal_timer_resume() { timer.running = true; timer.paused = false; return true; }
bool crystal_timer_restore(time_t end, uint32_t remaining, uint32_t duration)
{
    timer = {end > 0, remaining > 0, duration, remaining, end};
    return true;
}
