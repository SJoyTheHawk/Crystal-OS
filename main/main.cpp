/*
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>
#include <stdlib.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "esp_brookesia.hpp"
#include "hello_app.hpp"
#include "state_test_app.hpp"
#include "clock_app.hpp"
#include "crystal_hal.hpp"
#include "crystal_core.hpp"
#include "crystal_registry.hpp"
#include "lvgl.h"
#include "nvs_flash.h"

static const char *TAG = "crystal_boot";
static int64_t s_boot_start_us;

static CrystalApp *make_hello_app() { return new HelloApp(); }
static CrystalApp *make_state_test_app() { return new StateTestApp(); }
static CrystalApp *make_clock_app() { return new ClockApp(); }

static const CrystalAppEntry kApps[] = {
    {"hello", make_hello_app, true, 0},
    {"state_test", make_state_test_app, true, 1},
    {"clock", make_clock_app, true, 2},
};

static void update_status_clock(void *context, int hour, int minute, bool is_pm)
{
    auto *phone = static_cast<ESP_Brookesia_Phone *>(context);
    auto *status_bar = phone->getHome().getStatusBar();
    if (status_bar != nullptr) {
        (void)status_bar->setClock(hour, minute, is_pm);
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
        crystal_core_init(display, update_status_clock, phone),
        "Failed to start Crystal core services"
    );

    ESP_LOGI(TAG, "Hello icon: %ux%u, data=%u bytes, ptr=%p",
             hello_icon.header.w, hello_icon.header.h,
             static_cast<unsigned>(hello_icon.data_size), hello_icon.data);
    require_boot_step(
        crystal_registry_install(phone, kApps, sizeof(kApps) / sizeof(kApps[0])),
        "Failed to install app registry"
    );

    lv_refr_now(display);
    const int64_t elapsed_ms = (esp_timer_get_time() - s_boot_start_us) / 1000;
    ESP_LOGI(TAG, "first-frame baseline: %" PRId64 " ms", elapsed_ms);
    bsp_display_unlock();
}
