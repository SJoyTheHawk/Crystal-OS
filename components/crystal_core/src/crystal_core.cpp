/* SPDX-License-Identifier: MIT */

#include "crystal_core.hpp"

#include <atomic>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "crystal_hal.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lvgl.h"

namespace {
constexpr size_t kQueueDepth = 12;
constexpr size_t kEventDataMax = 32;
constexpr uint32_t kDimTimeoutMs = 30000;
constexpr uint32_t kOffTimeoutMs = 60000;
constexpr uint8_t kFullBrightness = 95;
constexpr uint8_t kDimBrightness = 20;
static const char *TAG = "crystal_core";

enum class PowerState : uint32_t { Full = 1, Dim = 2, Off = 3 };

struct EventMessage {
    crystal_evt_t type;
    uint8_t data[kEventDataMax];
    uint8_t length;
};

QueueHandle_t s_queue = nullptr;
lv_obj_t *s_toast = nullptr;
lv_obj_t *s_toast_label = nullptr;
lv_obj_t *s_clock_placeholder = nullptr;
lv_timer_t *s_toast_hide_timer = nullptr;
TaskHandle_t s_service_task = nullptr;
lv_disp_t *s_display = nullptr;
crystal_clock_update_cb_t s_clock_update = nullptr;
void *s_clock_context = nullptr;
PowerState s_power_state = PowerState::Full;
std::atomic_bool s_time_valid{false};

void hide_toast(lv_timer_t *)
{
    if (s_toast != nullptr) {
        lv_obj_fade_out(s_toast, 200, 0);
    }
    s_toast_hide_timer = nullptr;
}

void show_toast(const char *message)
{
    if (s_toast == nullptr || s_toast_label == nullptr || message == nullptr) return;

    if (s_toast_hide_timer != nullptr) {
        lv_timer_del(s_toast_hide_timer);
        s_toast_hide_timer = nullptr;
    }
    lv_label_set_text(s_toast_label, message);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_fade_in(s_toast, 150, 0);
    s_toast_hide_timer = lv_timer_create(hide_toast, 2500, nullptr);
    if (s_toast_hide_timer != nullptr) {
        lv_timer_set_repeat_count(s_toast_hide_timer, 1);
    }
}

bool init_toast()
{
    s_toast = lv_obj_create(lv_layer_top());
    if (s_toast == nullptr) return false;
    lv_obj_set_size(s_toast, 360, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(s_toast, 52, 0);
    lv_obj_set_style_bg_color(s_toast, lv_color_hex(0x252a30), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_toast, 1, 0);
    lv_obj_set_style_border_color(s_toast, lv_color_hex(0x59636e), 0);
    lv_obj_set_style_radius(s_toast, 6, 0);
    lv_obj_set_style_pad_hor(s_toast, 18, 0);
    lv_obj_set_style_pad_ver(s_toast, 14, 0);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -58);

    s_toast_label = lv_label_create(s_toast);
    if (s_toast_label == nullptr) return false;
    lv_obj_set_width(s_toast_label, LV_PCT(100));
    lv_obj_set_style_text_color(s_toast_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_toast_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_toast_label, LV_LABEL_LONG_WRAP);
    return true;
}

bool init_clock_placeholder()
{
    s_clock_placeholder = lv_label_create(lv_layer_top());
    if (s_clock_placeholder == nullptr) return false;
    lv_obj_set_size(s_clock_placeholder, 170, 40);
    lv_obj_align(s_clock_placeholder, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_clock_placeholder, lv_color_hex(0x38393a), 0);
    lv_obj_set_style_bg_opa(s_clock_placeholder, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_clock_placeholder, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_clock_placeholder, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_pad_left(s_clock_placeholder, 26, 0);
    lv_obj_set_style_pad_top(s_clock_placeholder, 11, 0);
    lv_obj_clear_flag(s_clock_placeholder, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_text(s_clock_placeholder, "--:--");
    if (s_time_valid.load()) {
        lv_obj_add_flag(s_clock_placeholder, LV_OBJ_FLAG_HIDDEN);
    }
    return true;
}

void drain_event_queue(lv_timer_t *)
{
    if (s_queue == nullptr) return;
    EventMessage message;
    while (xQueueReceive(s_queue, &message, 0) == pdTRUE) {
        if (message.type == UI_EVT_TOAST) {
            char text[kEventDataMax + 1] = {};
            memcpy(text, message.data, message.length);
            show_toast(text);
            ESP_LOGI(TAG, "toast displayed: %s", text);
        } else if (message.type == UI_EVT_TIME_SYNCED) {
            show_toast("Time synchronized");
        }
    }
}

void update_clock(lv_timer_t *)
{
    if (s_clock_update == nullptr) return;
    if (!s_time_valid.load()) {
        if (s_clock_placeholder != nullptr) lv_obj_clear_flag(s_clock_placeholder, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (s_clock_placeholder != nullptr) lv_obj_add_flag(s_clock_placeholder, LV_OBJ_FLAG_HIDDEN);
    const time_t epoch = time(nullptr);
    struct tm now = {};
    if (epoch < 1577836800 || localtime_r(&epoch, &now) == nullptr) return;
    const int hour_12 = now.tm_hour % 12 == 0 ? 12 : now.tm_hour % 12;
    s_clock_update(s_clock_context, hour_12, now.tm_min, now.tm_hour >= 12);
}

void check_power_state(lv_timer_t *)
{
    if (s_display == nullptr || s_service_task == nullptr) return;
    const uint32_t inactive_ms = lv_disp_get_inactive_time(s_display);
    PowerState requested = PowerState::Full;
    if (inactive_ms >= kOffTimeoutMs) requested = PowerState::Off;
    else if (inactive_ms >= kDimTimeoutMs) requested = PowerState::Dim;
    if (requested != s_power_state) {
        s_power_state = requested;
        xTaskNotify(s_service_task, static_cast<uint32_t>(requested), eSetValueWithOverwrite);
    }
}

void ramp_brightness(uint8_t target)
{
    if (hal().brightness == nullptr) return;
    uint8_t current = hal().brightness->get();
    while (current != target) {
        if (current < target) current = static_cast<uint8_t>(current + ((target - current) > 5 ? 5 : target - current));
        else current = static_cast<uint8_t>(current - ((current - target) > 5 ? 5 : current - target));
        hal().brightness->set(current);
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

void sntp_synced(struct timeval *tv)
{
    if (tv != nullptr && hal().rtc != nullptr) {
        struct tm corrected = {};
        if (localtime_r(&tv->tv_sec, &corrected) != nullptr) {
            (void)hal().rtc->write(&corrected);
        }
    }
    s_time_valid.store(true);
    (void)crystal_ui_post(UI_EVT_TIME_SYNCED);
}

void start_sntp()
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.sync_cb = sntp_synced;
    const esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "SNTP initialization failed: %s", esp_err_to_name(err));
    }
}

void service_task(void *)
{
    vTaskDelay(pdMS_TO_TICKS(1200));
    static constexpr char kReadyMessage[] = "Core services ready";
    (void)crystal_ui_post(UI_EVT_TOAST, kReadyMessage, sizeof(kReadyMessage) - 1);

    if (hal().wifi != nullptr) {
        hal().wifi->start();
        start_sntp();
    }

    TickType_t last_rtc_log = 0;
    for (;;) {
        uint32_t command = 0;
        if (xTaskNotifyWait(0, UINT32_MAX, &command, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (command == static_cast<uint32_t>(PowerState::Full)) ramp_brightness(kFullBrightness);
            else if (command == static_cast<uint32_t>(PowerState::Dim)) ramp_brightness(kDimBrightness);
            else if (command == static_cast<uint32_t>(PowerState::Off)) ramp_brightness(0);
        }
        const TickType_t now_ticks = xTaskGetTickCount();
        if (now_ticks - last_rtc_log >= pdMS_TO_TICKS(60000)) {
            last_rtc_log = now_ticks;
            struct tm now = {};
            if (hal().rtc != nullptr && hal().rtc->read(&now)) {
                ESP_LOGD(TAG, "RTC time: %04d-%02d-%02d %02d:%02d:%02d",
                         now.tm_year + 1900, now.tm_mon + 1, now.tm_mday,
                         now.tm_hour, now.tm_min, now.tm_sec);
            }
        }
    }
}
} // namespace

void crystal_time_init()
{
    char timezone[48] = "HKT-8";
    size_t timezone_len = sizeof(timezone) - 1;
    if (hal().storage != nullptr && hal().storage->get("timezone", timezone, &timezone_len)) {
        timezone[timezone_len < sizeof(timezone) ? timezone_len : sizeof(timezone) - 1] = '\0';
    }
    setenv("TZ", timezone, 1);
    tzset();

    struct tm rtc_time = {};
    if (hal().rtc == nullptr || !hal().rtc->read(&rtc_time)) {
        ESP_LOGW(TAG, "RTC unavailable or unset; waiting for SNTP");
        return;
    }
    rtc_time.tm_isdst = -1;
    const time_t epoch = mktime(&rtc_time);
    if (epoch < 1577836800) {
        ESP_LOGW(TAG, "RTC time is invalid; waiting for SNTP");
        return;
    }
    struct timeval tv = {};
    tv.tv_sec = epoch;
    if (settimeofday(&tv, nullptr) == 0) {
        s_time_valid.store(true);
        ESP_LOGI(TAG, "System time initialized from PCF85063");
    }
}

bool crystal_time_set(const struct tm *local_time)
{
    if (local_time == nullptr || hal().rtc == nullptr) return false;
    struct tm adjusted = *local_time;
    adjusted.tm_isdst = -1;
    const time_t epoch = mktime(&adjusted);
    if (epoch < 1577836800) return false;

    struct timeval tv = {};
    tv.tv_sec = epoch;
    if (settimeofday(&tv, nullptr) != 0 || !hal().rtc->write(&adjusted)) return false;
    s_time_valid.store(true);
    ESP_LOGI(TAG, "System and PCF85063 time updated");
    return true;
}

bool crystal_ui_post(crystal_evt_t type, const void *data, size_t len)
{
    if (s_queue == nullptr || len > kEventDataMax || (len != 0 && data == nullptr)) {
        return false;
    }
    EventMessage message = {};
    message.type = type;
    message.length = static_cast<uint8_t>(len);
    if (len != 0) memcpy(message.data, data, len);
    return xQueueSend(s_queue, &message, 0) == pdTRUE;
}

bool crystal_core_init(void *display, crystal_clock_update_cb_t clock_update, void *clock_context)
{
    if (s_queue != nullptr) return true;
    if (display == nullptr || clock_update == nullptr || clock_context == nullptr) return false;
    s_clock_update = clock_update;
    s_clock_context = clock_context;
    s_display = static_cast<lv_disp_t *>(display);
    s_queue = xQueueCreate(kQueueDepth, sizeof(EventMessage));
    if (s_queue == nullptr) return false;
    if (!init_toast()) return false;
    if (!init_clock_placeholder()) return false;
    if (lv_timer_create(drain_event_queue, 50, nullptr) == nullptr) return false;
    if (lv_timer_create(update_clock, 1000, nullptr) == nullptr) return false;
    if (lv_timer_create(check_power_state, 250, nullptr) == nullptr) return false;
    update_clock(nullptr);
    return xTaskCreatePinnedToCore(service_task, "crystal_service", 4096, nullptr, 2, &s_service_task, 0) == pdPASS;
}
