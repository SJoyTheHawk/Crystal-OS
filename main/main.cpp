/*
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include <inttypes.h>
#include <stdlib.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
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
#include "crystal_network_policy.hpp"
#if CONFIG_IDF_TARGET_ESP32P4
#include "ethernet_app.hpp"
#endif
#include "crystal_crash.h"
#include "crystal_update.h"
#include "crystal_core.hpp"
#include "crystal_registry.hpp"
#include "crystal_package.hpp"
#include "crystal_signature.hpp"
#include "crystal_crypto.hpp"
#include "crystal_memory.h"
#include "crystal_control.hpp"
#include "crystal_declarative.hpp"
#include "crystal_script.hpp"
#include "crystal_shell.hpp"
#include "crystal_stylesheet.hpp"
#include "crystal_boot_screen.hpp"
#include "crystal_ui_metrics.hpp"
#include "lvgl.h"
#include "nvs_flash.h"
#ifdef CRYSTAL_PHASE14_DEVICE_TEST
#include "crystal_phase14_device_test.h"
#include "esp_system.h"
extern const uint8_t phase14_package_start[] asm("_binary_phase14_calculator_capp_start");
extern const uint8_t phase14_package_end[] asm("_binary_phase14_calculator_capp_end");
#endif

static const char *TAG = "crystal_boot";
static int64_t s_boot_start_us;
static bool s_boot_display_locked = false;
static bool s_boot_ui_visible = false;
static lv_timer_t *s_package_cleanup_timer = nullptr;

static CrystalApp *make_dev_tester_app() { return new DevTesterApp(); }
static CrystalApp *make_clock_app() { return new ClockApp(); }
static CrystalApp *make_weather_app() { return new WeatherApp(); }
static CrystalApp *make_calculator_app() { return new CalculatorApp(); }
#if CONFIG_IDF_TARGET_ESP32P4
static CrystalApp *make_ethernet_app() { return new EthernetInfoApp(); }
#endif

static CrystalApp *make_package_app(const CrystalPackageManifest &manifest)
{
    // Phase 14 native apps are curated firmware factories. Packages cannot
    // supply machine code or select arbitrary symbols in kernel memory.
    bool curated_calculator = manifest.id == "org.crystal.calculator";
#ifdef CRYSTAL_PHASE14_DEVICE_TEST
    curated_calculator = curated_calculator || manifest.id == "org.crystal.phase14.calculator";
#endif
    if (curated_calculator && manifest.runtime == "native" &&
        manifest.entry == "calculator.native") return new CalculatorApp(manifest.name.c_str());
    if (manifest.runtime == "declarative") {
        return new DeclarativeApp(manifest.name.c_str(), manifest.id.c_str(), manifest.entry.c_str(),
                                  manifest.system_timer);
    }
    if (manifest.runtime == "lua") return new ScriptApp(manifest);
    return nullptr;
}

static const CrystalAppEntry kApps[] = {
    {"dev_tester", make_dev_tester_app, true, 0},
    {"clock", make_clock_app, true, 2},
    {"weather", make_weather_app, true, 3},
    {"calculator", make_calculator_app, true, 4},
#if CONFIG_IDF_TARGET_ESP32P4
    {"ethernet", make_ethernet_app, true, 5},
#endif
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
#if CONFIG_IDF_TARGET_ESP32P4
        static ESP_Brookesia_StatusBarIconData_t ethernet_icon = {};
        static bool icon_added = false;
        constexpr int kEthernetStatusId = 0x435259;
        if (!icon_added) {
            ethernet_icon.size.width = ethernet_icon.size.height = 28;
            ethernet_icon.icon.image_num = 1;
            ethernet_icon.icon.images[0].resource = &crystal_ethernet_status_icon;
            ethernet_icon.icon.images[0].recolor.color = 0xf2f4f7;
            ethernet_icon.icon.images[0].recolor.opacity = 255;
            ethernet_icon.icon.images[0].flags.enable_recolor = 1;
            icon_added = status_bar->addIcon(ethernet_icon,
                                             phone->getHome().getData().status_bar.data.wifi.area_index,
                                             kEthernetStatusId, 0);
        }
        const bool wired = crystal_network_active_type() == CrystalNetworkType::Ethernet;
        if (icon_added) (void)status_bar->setIconState(kEthernetStatusId, wired ? 0 : -1);
        if (wired) { (void)status_bar->setWifiIconState(-1); return; }
#endif
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
        crystal_boot_screen_fail(message);
        if (s_boot_display_locked) {
            bsp_display_unlock();
            s_boot_display_locked = false;
        }
        // Allow the LVGL task to present the error before the normal crash and
        // rollback path restarts the device. Pre-display failures still abort.
        if (s_boot_ui_visible) vTaskDelay(pdMS_TO_TICKS(1500));
        abort();
    }
}

// Kept verbatim from the board reference: RGB flush areas must be even-aligned.
#if !CONFIG_IDF_TARGET_ESP32P4
static void my_rounder_cb(lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    (void)disp_drv;
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}
#endif

extern "C" void app_main(void)
{
    s_boot_start_us = esp_timer_get_time();
    crystal_memory_init();
    require_boot_step(crystal_crypto_init(), "Failed to initialize cryptography");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    crystal_crash_init();

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t running_state = ESP_OTA_IMG_UNDEFINED;
    if (running != nullptr &&
            esp_ota_get_state_partition(running, &running_state) == ESP_OK &&
            running_state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "First boot after OTA; image pending confirmation");
    }

    crystal_time_init();
    ESP_ERROR_CHECK(bsp_spiffs_mount());

    lv_display_t *display = bsp_display_start();
    require_boot_step(display != nullptr, "Failed to start display");
    crystal_hal_set_button_callback(crystal_core_wake_display);
    // Establish a known OFF state before any LVGL rendering. crystal_hal_init()
    // below restores only the cached user level, so the panel stays dark until
    // the first frame is fully rendered.
    (void)bsp_display_brightness_set(0);
    crystal_hal_init();
#if CONFIG_IDF_TARGET_ESP32P4
    require_boot_step(bsp_display_lock(0), "Failed to lock display for boot screen");
    s_boot_display_locked = true;
    crystal_ui_metrics_start(display);
    require_boot_step(crystal_boot_screen_show(), "Failed to create boot screen");
    lv_refr_now(display);
    bsp_display_unlock();
    s_boot_display_locked = false;
    (void)bsp_display_brightness_set(hal().brightness->get());
    s_boot_ui_visible = true;
    ESP_LOGI(TAG, "boot-screen first frame: %" PRId64 " ms",
             (esp_timer_get_time() - s_boot_start_us) / 1000);
#endif
    crystal_boot_screen_update("Verifying app trust...", 20);
    require_boot_step(crystal_signature_init(), "Failed to initialize package signature trust store");
    crystal_app_set_log_hook(crystal_control_log_line);
    crystal_registry_set_package_factory(make_package_app);
    crystal_boot_screen_update("Loading your apps...", 35);
    std::string package_error;
    const esp_partition_t *apps_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, "apps");
    const size_t package_storage_bytes = apps_partition != nullptr && apps_partition->size > 128 * 1024
        ? apps_partition->size - 128 * 1024 : 0;
    if (apps_partition == nullptr || !crystal_packages_mount() ||
        !crystal_packages_init("/littlefs/apps",
            {static_cast<size_t>(hal().system_info->free_psram()) * 3 / 4,
             package_storage_bytes, 4}, &package_error)) {
        ESP_LOGE(TAG, "Package storage unavailable: %s", package_error.c_str());
    } else {
        ESP_LOGI(TAG, "LittleFS mounted at /littlefs; package storage ready (%zu packages)", crystal_packages().size());
    }
    require_boot_step(bsp_display_lock(0), "Failed to lock display for UI initialization");
    s_boot_display_locked = true;
    crystal_boot_screen_update("Preparing Crystal OS...", 55);
#if !CONFIG_IDF_TARGET_ESP32P4
    if (display->driver != nullptr) {
        display->driver->rounder_cb = my_rounder_cb;
    }
#endif

    // Subscribe the LVGL task to the watchdog because touch I2C reads can block
    // for several seconds in critical sections, starving the IDLE task.
    const char *lvgl_task_name = "taskLVGL";
#if CONFIG_IDF_TARGET_ESP32P4
    lvgl_task_name = "lvgl";
#endif
    TaskHandle_t lvgl_task = xTaskGetHandle(lvgl_task_name);
    if (lvgl_task != nullptr) {
        esp_err_t wdt_err = esp_task_wdt_add(lvgl_task);
        if (wdt_err == ESP_OK) {
            ESP_LOGI(TAG, "Subscribed LVGL task to watchdog");
            // Create an LVGL timer to reset the watchdog from the LVGL task context
            lv_timer_t *wdt_timer = lv_timer_create([](lv_timer_t *) {
                esp_task_wdt_reset();
            }, 1000, nullptr);
            if (wdt_timer == nullptr) {
                ESP_LOGW(TAG, "Failed to create watchdog reset timer");
            }
        } else {
            ESP_LOGW(TAG, "Failed to subscribe LVGL task to watchdog: %s", esp_err_to_name(wdt_err));
        }
    } else {
        ESP_LOGW(TAG, "Could not find LVGL task to subscribe to watchdog");
    }

    // Package removal detaches the verified-clean app immediately. Reclaim
    // just one file/traversal step per tick so catalog-sized bundles cannot
    // block LVGL input or its watchdog heartbeat for several seconds.
    s_package_cleanup_timer = lv_timer_create([](lv_timer_t *timer) {
        static bool cleanup_failed = false, cleanup_pending = false;
        std::string error;
        const bool idle = crystal_package_cleanup_step(&error);
        if (!error.empty() && !cleanup_failed)
            ESP_LOGW(TAG, "Deferred app storage cleanup: %s", error.c_str());
        cleanup_failed = !error.empty();
        if (idle && cleanup_pending) ESP_LOGI(TAG, "App storage cleanup complete");
        cleanup_pending = !idle;
        if (idle) lv_timer_pause(timer);
    }, 20, nullptr);
    require_boot_step(s_package_cleanup_timer != nullptr, "Failed to schedule app storage cleanup");
    lv_timer_pause(s_package_cleanup_timer);
    crystal_package_set_cleanup_ready_hook([] {
        lv_timer_resume(s_package_cleanup_timer);
        lv_timer_ready(s_package_cleanup_timer);
    });

    auto *phone = new ESP_Brookesia_Phone(display);
    require_boot_step(phone != nullptr, "Failed to create Brookesia phone");

    auto *stylesheet = crystal_stylesheet_create_dark();
    require_boot_step(stylesheet != nullptr, "Failed to create phone stylesheet");
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
#if CONFIG_IDF_TARGET_ESP32P4
    auto *status_bar = phone->getHome().getStatusBar();
    require_boot_step(status_bar != nullptr && status_bar->setVerticalOffset(2),
                      "Failed to align status bar");
#endif
    crystal_boot_screen_update("Starting system services...", 70);
    require_boot_step(
        crystal_core_init(display, update_status_clock, update_status_connectivity,
                          update_status_battery, phone),
        "Failed to start Crystal core services"
    );
    // Start the OTA worker after the UI/event queue and core services exist.
    // Starting it during early boot can race queue users while app_main is
    // still bringing up the display and LVGL.
    crystal_update_init();
    if (crystal_crash_should_notify()) {
        (void)crystal_ui_post(UI_EVT_CRASH_RECOVERED);
        crystal_crash_mark_displayed();
    }

    crystal_boot_screen_update("Restoring your workspace...", 85);
    require_boot_step(
        crystal_registry_install(phone, kApps, sizeof(kApps) / sizeof(kApps[0])),
        "Failed to install app registry"
    );
    require_boot_step(crystal_shell_init(phone), "Failed to start card shell");
    if (!crystal_control_usb_start()) {
        ESP_LOGW(TAG, "USB developer transport unavailable");
    }
#ifdef CRYSTAL_PHASE14_DEVICE_TEST
    (void)crystal_phase14_device_test_start(phone, phase14_package_start,
        static_cast<size_t>(phase14_package_end - phase14_package_start),
        "/littlefs/apps", esp_restart, CRYSTAL_PHASE14_DEVICE_TEST_RUN);
#else
    ESP_LOGI(TAG, "Startup baseline: automatic Phase 14 device test disabled");
#endif
    // The update subsystem checks the bootloader result before UI creation.
    // Queue the one-shot rollback notification only after the shell exists.
    if (crystal_update_did_rollback()) {
        ESP_LOGW(TAG, "Posting OTA rollback notification");
        (void)crystal_ui_post(UI_EVT_OTA_ROLLED_BACK);
    }

    crystal_boot_screen_finish();
#if CONFIG_IDF_TARGET_ESP32P4
    // Shell objects live on the top layer too; present startup above them until
    // its timer applies Ready and completes the fade. Never wait for Wi-Fi.
    (void)crystal_boot_screen_show();
#endif
    lv_refr_now(display);
    const int64_t elapsed_ms = (esp_timer_get_time() - s_boot_start_us) / 1000;
    ESP_LOGI(TAG, "first-frame baseline: %" PRId64 " ms", elapsed_ms);
    // The card's onCreate() and LVGL's recursive draw both ran on this stack, so
    // this number is the real headroom on the deepest widget tree Crystal builds.
    ESP_LOGI(TAG, "main task stack headroom: %u bytes",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    bsp_display_unlock();
    s_boot_display_locked = false;
    // S3 stays dark through UI construction. P4 has already presented the boot
    // overlay, which now fades away when the shell is ready.
    (void)bsp_display_brightness_set(hal().brightness->get());
    crystal_core_start_services();
}
