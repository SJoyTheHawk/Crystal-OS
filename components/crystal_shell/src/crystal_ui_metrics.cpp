/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "crystal_ui_metrics.hpp"
#include "sdkconfig.h"
#include "esp_log.h"

#if CONFIG_IDF_TARGET_ESP32P4
namespace {
void (*s_previous_monitor)(lv_disp_drv_t *, uint32_t, uint32_t) = nullptr;
uint32_t s_started = 0;
uint32_t s_frames = 0;
uint32_t s_total_ms = 0;
uint32_t s_max_ms = 0;
uint32_t s_over_budget = 0;
void monitor(lv_disp_drv_t *driver, uint32_t time, uint32_t pixels)
{
    if (s_previous_monitor != nullptr) s_previous_monitor(driver, time, pixels);
    ++s_frames;
    s_total_ms += time;
    s_max_ms = LV_MAX(s_max_ms, time);
    if (time > 16) ++s_over_budget;
    const uint32_t elapsed = lv_tick_elaps(s_started);
    if (elapsed < 5000) return;
    ESP_LOGD("crystal_ui_perf",
             "redraws=%lu window_ms=%lu avg_draw_ms=%lu max_draw_ms=%lu over_16ms=%lu refresh_ms=%u",
             static_cast<unsigned long>(s_frames), static_cast<unsigned long>(elapsed),
             static_cast<unsigned long>(s_total_ms / s_frames),
             static_cast<unsigned long>(s_max_ms), static_cast<unsigned long>(s_over_budget),
             static_cast<unsigned>(LV_DISP_DEF_REFR_PERIOD));
    s_started = lv_tick_get();
    s_frames = s_total_ms = s_max_ms = s_over_budget = 0;
}
}
#endif

void crystal_ui_metrics_start(lv_disp_t *display)
{
#if CONFIG_IDF_TARGET_ESP32P4
    if (display == nullptr || display->driver == nullptr || display->driver->monitor_cb == monitor) return;
    s_previous_monitor = display->driver->monitor_cb;
    display->driver->monitor_cb = monitor;
    s_started = lv_tick_get();
#else
    (void)display;
#endif
}
