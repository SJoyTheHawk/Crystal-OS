/*
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include <inttypes.h>
#include <stdlib.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "esp_brookesia.hpp"
#include "dev_tester_app.hpp"
#include "clock_app.hpp"
#include "weather_app.hpp"
#include "calculator_app.hpp"
#include "crystal_hal.hpp"
#include "crystal_core.hpp"
#include "crystal_registry.hpp"
#include "crystal_shell.hpp"
#include "lvgl.h"
#include "nvs_flash.h"

static const char *TAG = "crystal_boot";
static int64_t s_boot_start_us;

static CrystalApp *make_dev_tester_app() { return new DevTesterApp(); }
static CrystalApp *make_clock_app() { return new ClockApp(); }
static CrystalApp *make_weather_app() { return new WeatherApp(); }
static CrystalApp *make_calculator_app() { return new CalculatorApp(); }

static const CrystalAppEntry kApps[] = {
    {"dev_tester", make_dev_tester_app, true, 0},
    {"clock", make_clock_app, true, 2},
    {"weather", make_weather_app, true, 3},
    {"calculator", make_calculator_app, true, 4},
};

static void update_status_clock(void *context, int hour, int minute, bool is_pm, bool format24)
{
    auto *phone = static_cast<ESP_Brookesia_Phone *>(context);
    auto *status_bar = phone->getHome().getStatusBar();
    if (status_bar != nullptr) {
        (void)status_bar->setClockFormat(format24 ? ESP_Brookesia_StatusBar::ClockFormat::FORMAT_24H
                                                  : ESP_Brookesia_StatusBar::ClockFormat::FORMAT_12H);
        (void)status_bar->setClock(hour, minute, is_pm);
    }
}

static void update_status_connectivity(void *context, bool connected)
{
    auto *phone = static_cast<ESP_Brookesia_Phone *>(context);
    auto *status_bar = phone->getHome().getStatusBar();
    if (status_bar != nullptr) {
        (void)status_bar->setWifiIconState(connected
            ? ESP_Brookesia_StatusBar::WifiState::SIGNAL_3
            : ESP_Brookesia_StatusBar::WifiState::DISCONNECTED);
    }
}

static void update_status_battery(void *context, int percent, bool charging)
{
    auto *phone = static_cast<ESP_Brookesia_Phone *>(context);
    auto *status_bar = phone->getHome().getStatusBar();
    if (status_bar != nullptr) {
        (void)status_bar->setBatteryPercent(charging, percent);
    }
}

static void require_boot_step(bool succeeded, const char *message)
{
    if (!succeeded) {
        ESP_LOGE(TAG, "%s", message);
        abort();
    }
}

// Kept verbatim from the board reference: RGB flush areas must be even-aligned.
static void my_rounder_cb(lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    (void)disp_drv;
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

extern "C" void app_main(void)
{
    s_boot_start_us = esp_timer_get_time();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    crystal_time_init();
    ESP_ERROR_CHECK(bsp_spiffs_mount());

    lv_display_t *display = bsp_display_start();
    require_boot_step(display != nullptr, "Failed to start display");
    crystal_hal_init();
    if (display->driver != nullptr) {
        display->driver->rounder_cb = my_rounder_cb;
    }

    bsp_display_lock(0);

    auto *phone = new ESP_Brookesia_Phone(display);
    require_boot_step(phone != nullptr, "Failed to create Brookesia phone");

    auto *stylesheet = new ESP_Brookesia_PhoneStylesheet_t
        ESP_BROOKESIA_PHONE_480_480_DARK_STYLESHEET();
    require_boot_step(stylesheet != nullptr, "Failed to create phone stylesheet");
    stylesheet->core.manager.app.max_running_num = 1;
    stylesheet->home.flags.enable_recents_screen = 0;
    stylesheet->manager.gesture.threshold.direction_horizon = 12;
    stylesheet->manager.gesture.threshold.direction_vertical = 12;
    stylesheet->manager.gesture.threshold.horizontal_edge = 24;
    stylesheet->manager.gesture_mask_indicator_trigger_time_ms = UINT32_MAX;
    // Kill Brookesia's bottom indicator bar so Crystal's home pill is the only one.
    // This flag is the only gate the manager does not re-assert: begin() calls
    // setIndicatorBarVisible(BOTTOM, true) once, and the MAIN branch of
    // processGestureScreenChange() re-shows it on every return to the launcher, so
    // hiding the object from the shell loses on the next navigation. With the flag
    // clear, setIndicatorBarVisible() and setIndicatorBarLength() return early and
    // the bar keeps the LV_OBJ_FLAG_HIDDEN it was created with. Gesture detection is
    // unaffected: the flag only gates the bar's visuals, not the touch handling.
    // LEFT and RIGHT already ship as 0, so this path is already exercised.
    stylesheet->manager.gesture.flags.enable_indicator_bars
        [ESP_BROOKESIA_GESTURE_INDICATOR_BAR_TYPE_BOTTOM] = 0;
    require_boot_step(phone->addStylesheet(stylesheet), "Failed to add phone stylesheet");
    require_boot_step(phone->activateStylesheet(stylesheet), "Failed to activate phone stylesheet");
    delete stylesheet;

    require_boot_step(phone->setTouchDevice(bsp_display_get_input_dev()), "Failed to set touch device");
    crystal_hal_bind_touch(bsp_display_get_input_dev());
    phone->registerLvLockCallback(
        reinterpret_cast<ESP_Brookesia_LvLockCallback_t>(bsp_display_lock), 0
    );
    phone->registerLvUnlockCallback(
        reinterpret_cast<ESP_Brookesia_LvUnlockCallback_t>(bsp_display_unlock)
    );
    require_boot_step(phone->begin(), "Failed to start Brookesia phone");
    require_boot_step(
        crystal_core_init(display, update_status_clock, update_status_connectivity,
                          update_status_battery, phone),
        "Failed to start Crystal core services"
    );

    require_boot_step(
        crystal_registry_install(phone, kApps, sizeof(kApps) / sizeof(kApps[0])),
        "Failed to install app registry"
    );
    require_boot_step(crystal_shell_init(phone), "Failed to start card shell");

    lv_refr_now(display);
    const int64_t elapsed_ms = (esp_timer_get_time() - s_boot_start_us) / 1000;
    ESP_LOGI(TAG, "first-frame baseline: %" PRId64 " ms", elapsed_ms);
    // The card's onCreate() and LVGL's recursive draw both ran on this stack, so
    // this number is the real headroom on the deepest widget tree Crystal builds.
    ESP_LOGI(TAG, "main task stack headroom: %u bytes",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    bsp_display_unlock();
}
