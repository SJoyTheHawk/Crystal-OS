/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

#include "crystal_core.hpp"

#include <atomic>
#include <cmath>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "crystal_hal.hpp"
#include "crystal_brightness_policy.hpp"
#include "crystal_network.h"
#include "crystal_network_policy.hpp"
#include "crystal_ethernet.hpp"
#include "crystal_http.h"
#include "crystal_update_view.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "lvgl.h"

extern void crystal_shell_wifi_event(uint8_t event);
extern void crystal_shell_network_event();
extern void crystal_shell_ota_event();
extern void crystal_shell_update_failure_event(const char *message);
extern void crystal_shell_update_success_event();
extern void crystal_shell_control_start_event(const char pairing_pin[7]);

namespace {
constexpr size_t kQueueDepth = 12;
constexpr size_t kEventDataMax = 64;
constexpr size_t kToastQueueDepth = 12;
constexpr uint32_t kToastFadeInMs = 150;
constexpr uint32_t kToastHoldMs = 2500;
constexpr uint32_t kToastFadeOutMs = 200;
#if CONFIG_IDF_TARGET_ESP32P4
constexpr lv_coord_t kToastHorizontalPad = 24;
constexpr lv_coord_t kToastScreenMargin = 24;
constexpr lv_coord_t kToastMinHeight = 68;
constexpr lv_coord_t kToastVerticalPad = 16;
constexpr const lv_font_t *kToastFont = &lv_font_montserrat_20;
#else
constexpr lv_coord_t kToastHorizontalPad = 18;
constexpr lv_coord_t kToastScreenMargin = 16;
constexpr lv_coord_t kToastMinHeight = 52;
constexpr lv_coord_t kToastVerticalPad = 14;
constexpr const lv_font_t *kToastFont = nullptr;
#endif
#if CONFIG_IDF_TARGET_ESP32P4
constexpr uint8_t kFullBrightness = 100;
constexpr uint8_t kDimBrightnessMin = 1;
#else
constexpr uint8_t kFullBrightness = 95;
constexpr uint8_t kDimBrightnessMin = 5;
#endif
static const char *TAG = "crystal_core";
enum class PowerState : uint32_t { Full = 1, Dim = 2, Off = 3, ApplySaving = 4 };

struct EventMessage {
    crystal_evt_t type;
    uint8_t data[kEventDataMax];
    uint8_t length;
};

QueueHandle_t s_queue = nullptr;
lv_obj_t *s_toast = nullptr;
lv_obj_t *s_toast_label = nullptr;
lv_obj_t *s_clock_placeholder = nullptr;
lv_obj_t *s_timer_indicator = nullptr;
lv_timer_t *s_toast_phase_timer = nullptr;
lv_timer_t *s_toast_finish_timer = nullptr;
char s_toast_queue[kToastQueueDepth][kEventDataMax + 1] = {};
size_t s_toast_queue_head = 0;
size_t s_toast_queue_count = 0;
bool s_toast_active = false;
TaskHandle_t s_service_task = nullptr;
SemaphoreHandle_t s_startup_ready = nullptr;
lv_disp_t *s_display = nullptr;
crystal_clock_update_cb_t s_clock_update = nullptr;
crystal_connectivity_update_cb_t s_connectivity_update = nullptr;
crystal_battery_update_cb_t s_battery_update = nullptr;
void *s_status_context = nullptr;
PowerState s_power_state = PowerState::Full;
std::atomic_bool s_button_wake_requested{false};
std::atomic_bool s_time_valid{false};
std::atomic<uint32_t> s_time_revision{0};
struct TimeListener {
    uint32_t id = 0, revision = 0;
    crystal_time_ready_cb_t callback = nullptr;
    void *context = nullptr;
};
TimeListener s_time_listeners[16]{};
uint32_t s_time_next_id = 1;

void dispatch_time_ready()
{
    if (!s_time_valid.load()) return;
    const uint32_t revision = s_time_revision.load();
    // Snapshot IDs: callbacks may unsubscribe themselves/other apps or create
    // a subscriber. Newly added listeners wait until the next dispatch.
    uint32_t ids[16]{};
    for (size_t i = 0; i < 16; ++i) ids[i] = s_time_listeners[i].id;
    for (size_t i = 0; i < 16; ++i) {
        auto &listener = s_time_listeners[i];
        if (!ids[i] || listener.id != ids[i] || listener.revision == revision) continue;
        listener.revision = revision;
        auto callback = listener.callback;
        void *context = listener.context;
        callback(context);
    }
}
struct NetListener {
    uint32_t id = 0, revision = 0;
    bool delivered = false, online = false;
    crystal_net_change_cb_t callback = nullptr;
    void *context = nullptr;
};
NetListener s_net_listeners[16]{};
uint32_t s_net_next_id = 1;

void dispatch_net_changed()
{
    const uint32_t revision = crystal_network_revision();
    const bool online = crystal_net_is_ready();
    uint32_t ids[16]{};
    for (size_t i = 0; i < 16; ++i) ids[i] = s_net_listeners[i].id;
    for (size_t i = 0; i < 16; ++i) {
        auto &listener = s_net_listeners[i];
        if (!ids[i] || listener.id != ids[i] || (listener.delivered &&
            listener.revision == revision && listener.online == online)) continue;
        listener.revision = revision; listener.online = online; listener.delivered = true;
        auto callback = listener.callback;
        void *context = listener.context;
        callback(context, online);
    }
}
std::atomic_bool s_sntp_initialized{false};
std::atomic_bool s_sntp_sync_started{false};
esp_event_handler_instance_t s_network_handler = nullptr;
std::atomic<int64_t> s_timer_end_at{0};
std::atomic<int64_t> s_timer_deadline_us{0};
std::atomic<uint32_t> s_timer_duration{0};
std::atomic<uint32_t> s_timer_paused_remaining{0};
std::atomic_bool s_stopwatch_running{false};
std::atomic<int> s_battery_percent{-1};
std::atomic_bool s_battery_charging{false};

template <typename T>
T stored_value(const char *key, T fallback)
{
    T value = {};
    size_t length = sizeof(value);
    return hal().storage != nullptr && hal().storage->get(key, &value, &length) &&
           length == sizeof(value) ? value : fallback;
}

template <typename T>
void store_value(const char *key, T value)
{
    if (hal().storage != nullptr) (void)hal().storage->set(key, &value, sizeof(value));
}

// Read-through cache for the keys the LVGL timers read every tick.
//
// check_power_state() runs at 4Hz and update_clock() at 1Hz. Going to NVS from
// them costs an nvs_open per read -- a global lock plus a handle allocation --
// on the task that must never stall, which shows as animation jitter. Every
// writer of these keys is a setter in this file, so a RAM copy cannot go stale.
// -1 means "not loaded yet"; the first read fills it.
template <typename T>
T cached_value(const char *key, T fallback, std::atomic<int32_t> &cache)
{
    const int32_t held = cache.load(std::memory_order_relaxed);
    if (held >= 0) return static_cast<T>(held);
    const T value = stored_value<T>(key, fallback);
    cache.store(static_cast<int32_t>(value), std::memory_order_relaxed);
    return value;
}

template <typename T>
void store_cached(const char *key, T value, std::atomic<int32_t> &cache)
{
    store_value(key, value);
    cache.store(static_cast<int32_t>(value), std::memory_order_relaxed);
}

std::atomic<int32_t> s_cache_auto_dim{-1};
std::atomic<int32_t> s_cache_dim_seconds{-1};
std::atomic<int32_t> s_cache_saving_dim_seconds{-1};
std::atomic<int32_t> s_cache_off_seconds{-1};
std::atomic<int32_t> s_cache_saving{-1};
std::atomic<int32_t> s_cache_format24{-1};
std::atomic<int32_t> s_cache_lua_extended{-1};

bool one_of(uint16_t value, const uint16_t *allowed, size_t count)
{
    for (size_t i = 0; i < count; ++i) if (value == allowed[i]) return true;
    return false;
}

void update_connectivity(lv_timer_t *timer);
void sntp_synced(struct timeval *tv);

bool energy_saving_enabled()
{
    if (hal().storage == nullptr) return false;
    return cached_value<uint8_t>("power.saving", 0, s_cache_saving) != 0;
}

uint8_t saved_brightness()
{
    uint8_t value = kFullBrightness;
    size_t length = sizeof(value);
    if (hal().storage != nullptr && hal().storage->get("brightness", &value, &length) &&
            length == sizeof(value)) {
        return value > kFullBrightness ? kFullBrightness : value;
    }
    return value;
}

void show_toast(const char *message);
void display_toast(const char *message);

void start_toast_hold(lv_timer_t *)
{
    s_toast_phase_timer = nullptr;
    if (!s_toast_active) return;
    s_toast_phase_timer = lv_timer_create([](lv_timer_t *) {
        s_toast_phase_timer = nullptr;
        if (!s_toast_active || s_toast == nullptr) return;
        lv_obj_fade_out(s_toast, kToastFadeOutMs, 0);
        s_toast_finish_timer = lv_timer_create([](lv_timer_t *) {
            s_toast_finish_timer = nullptr;
            if (s_toast == nullptr) return;
            lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
            s_toast_active = false;

            if (s_toast_queue_count == 0) return;
            char next[kEventDataMax + 1] = {};
            memcpy(next, s_toast_queue[s_toast_queue_head], sizeof(next));
            s_toast_queue_head = (s_toast_queue_head + 1) % kToastQueueDepth;
            --s_toast_queue_count;

            // The next message starts only after the previous toast's fade-out
            // has completed, preserving FIFO order without cutting in.
            display_toast(next);
        }, kToastFadeOutMs, nullptr);
        if (s_toast_finish_timer != nullptr) {
            lv_timer_set_repeat_count(s_toast_finish_timer, 1);
        }
    }, kToastHoldMs, nullptr);
    if (s_toast_phase_timer != nullptr) {
        lv_timer_set_repeat_count(s_toast_phase_timer, 1);
    }
}

void display_toast(const char *message)
{
    if (s_toast == nullptr || s_toast_label == nullptr || message == nullptr) {
        ESP_LOGW(TAG, "display_toast() early return: s_toast=%p s_toast_label=%p message=%p",
                 s_toast, s_toast_label, message);
        return;
    }

    ESP_LOGI(TAG, "toast displayed: %s", message);

    // Toasts are created during core initialization, before Settings and other
    // system pages are pushed onto lv_layer_top().  Those pages would otherwise
    // be later siblings and draw over the toast.  Re-front it for every message
    // so notifications remain visible from any Settings page or dialog.
    lv_obj_move_foreground(s_toast);

    lv_label_set_text(s_toast_label, message);
    const lv_coord_t display_width = lv_disp_get_hor_res(nullptr);
    const lv_coord_t max_toast_width = display_width > 2 * kToastScreenMargin
        ? static_cast<lv_coord_t>(display_width - 2 * kToastScreenMargin) : 0;
    const lv_coord_t max_label_width = max_toast_width > 2 * kToastHorizontalPad + 2
        ? static_cast<lv_coord_t>(max_toast_width - 2 * kToastHorizontalPad - 2) : 0;
    lv_obj_set_width(s_toast, LV_SIZE_CONTENT);
    lv_obj_set_width(s_toast_label, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(s_toast, max_toast_width, 0);
    lv_obj_set_style_max_width(s_toast_label, max_label_width, 0);
    lv_obj_update_layout(s_toast);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_fade_in(s_toast, kToastFadeInMs, 0);
    s_toast_active = true;
    s_toast_phase_timer = lv_timer_create(start_toast_hold, kToastFadeInMs, nullptr);
    if (s_toast_phase_timer != nullptr) {
        lv_timer_set_repeat_count(s_toast_phase_timer, 1);
    }
}

void show_toast(const char *message)
{
    if (message == nullptr || s_toast == nullptr || s_toast_label == nullptr) {
        ESP_LOGW(TAG, "show_toast() early return: s_toast=%p s_toast_label=%p message=%p",
                 s_toast, s_toast_label, message);
        return;
    }
    if (!s_toast_active && s_toast_queue_count == 0) {
        display_toast(message);
        return;
    }
    if (s_toast_queue_count >= kToastQueueDepth) {
        ESP_LOGW(TAG, "toast queue full; dropping newest message: %s", message);
        return;
    }
    const size_t tail = (s_toast_queue_head + s_toast_queue_count) % kToastQueueDepth;
    strncpy(s_toast_queue[tail], message, kEventDataMax);
    s_toast_queue[tail][kEventDataMax] = '\0';
    ++s_toast_queue_count;
    ESP_LOGI(TAG, "toast queued (%u pending): %s",
             static_cast<unsigned>(s_toast_queue_count), message);
}

bool init_toast()
{
    s_toast = lv_obj_create(lv_layer_top());
    if (s_toast == nullptr) return false;
    lv_obj_set_size(s_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(s_toast, kToastMinHeight, 0);
    lv_obj_set_style_bg_color(s_toast, lv_color_hex(0x252a30), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_toast, 1, 0);
    lv_obj_set_style_border_color(s_toast, lv_color_hex(0x59636e), 0);
    lv_obj_set_style_radius(s_toast, 6, 0);
    lv_obj_set_style_pad_hor(s_toast, 18, 0);
    lv_obj_set_style_pad_ver(s_toast, kToastVerticalPad, 0);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -58);

    s_toast_label = lv_label_create(s_toast);
    if (s_toast_label == nullptr) return false;
    lv_obj_set_width(s_toast_label, LV_SIZE_CONTENT);
#if CONFIG_IDF_TARGET_ESP32P4
    lv_obj_set_style_text_font(s_toast_label, kToastFont, 0);
#endif
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

bool init_timer_indicator()
{
    s_timer_indicator = lv_label_create(lv_layer_top());
    if (s_timer_indicator == nullptr) return false;
    lv_label_set_text(s_timer_indicator, "T");
    lv_obj_set_style_text_font(s_timer_indicator, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_timer_indicator, lv_color_white(), 0);
    lv_obj_clear_flag(s_timer_indicator, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_timer_indicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_timer_indicator, LV_ALIGN_TOP_MID, 0, 10);
    return true;
}

void drain_event_queue(lv_timer_t *)
{
    dispatch_time_ready();
    dispatch_net_changed();
    if (s_queue == nullptr) return;
    EventMessage message;
    while (xQueueReceive(s_queue, &message, 0) == pdTRUE) {
        if (message.type == UI_EVT_WIFI_GOT_IP || message.type == UI_EVT_WIFI_DISCONNECTED ||
                message.type == UI_EVT_WIFI_SCAN_DONE || message.type == UI_EVT_WIFI_CONNECT_FAILED ||
                message.type == UI_EVT_WIFI_CONNECTING) {
            if (message.type == UI_EVT_WIFI_GOT_IP) show_toast("Connected to WiFi");
            else if (message.type == UI_EVT_WIFI_CONNECT_FAILED) show_toast("Failed to connect to WiFi network");
            crystal_shell_wifi_event(static_cast<uint8_t>(message.type));
            update_connectivity(nullptr);
        } else if (message.type == UI_EVT_NETWORK_CHANGED) {
            crystal_shell_network_event();
            update_connectivity(nullptr);
        } else if (message.type == UI_EVT_TOAST) {
            char text[kEventDataMax + 1] = {};
            memcpy(text, message.data, message.length);
            show_toast(text);
        } else if (message.type == UI_EVT_TIMER_EXPIRED) {
            show_toast("Timer finished");
        } else if (message.type == UI_EVT_CRASH_RECOVERED) {
            show_toast("Recovered from an error");
        } else if (message.type == UI_EVT_OTA_ROLLED_BACK) {
            crystal_shell_update_failure_event("Update failed, rolled back");
        } else if (message.type == UI_EVT_OTA_SUCCEEDED) {
            crystal_shell_update_success_event();
        } else if (message.type == UI_EVT_CONTROL_START) {
            crystal_shell_control_start_event(message.length == 7 ?
                reinterpret_cast<const char *>(message.data) : nullptr);
        } else if (message.type == UI_EVT_CONTROL_ACTION &&
                   message.length == sizeof(CrystalControlAction)) {
            crystal_shell_control_action_event(reinterpret_cast<const CrystalControlAction *>(message.data));
        } else if (message.type == UI_EVT_OTA_CHANGED) {
            crystal_update_snapshot_t snapshot = {};
            crystal_update_snapshot(&snapshot);
            if (snapshot.state == UPDATE_STATE_CURRENT) {
                show_toast("System is up to date");
            } else if (snapshot.state == UPDATE_STATE_FAILED && snapshot.error_message[0] != '\0') {
                // Update failures require explicit acknowledgement in the shell.
                crystal_shell_update_failure_event(snapshot.error_message);
            }
            crystal_shell_ota_event();
        } else if (message.type == UI_EVT_BATTERY &&
                   message.length == sizeof(int8_t) + sizeof(uint8_t) && s_battery_update != nullptr) {
            const int percent = static_cast<int8_t>(message.data[0]);
            const bool charging = message.data[1] != 0;
            s_battery_update(s_status_context, percent, charging);
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
    const bool format24 = crystal_time_format_24();
    const int hour = format24 ? now.tm_hour : (now.tm_hour % 12 == 0 ? 12 : now.tm_hour % 12);
    s_clock_update(s_status_context, hour, now.tm_min, !format24 && now.tm_hour >= 12, format24);
}

void update_connectivity(lv_timer_t *)
{
    if (s_connectivity_update != nullptr)
        s_connectivity_update(s_status_context, hal().wifi && hal().wifi->connected());
#if CONFIG_IDF_TARGET_ESP32P4
    crystal_shell_network_event();
#endif
}

void network_route_changed(CrystalNetworkType previous, CrystalNetworkType active, void *)
{
    const bool online = active != CrystalNetworkType::None;
    // Publish aggregate connectivity, including route switches. One interface
    // dropping must not claim the device is offline while the other is ready.
    s_sntp_sync_started = false;
    const esp_err_t err = esp_event_post(CRYSTAL_NETWORK_EVENT,
        online ? CRYSTAL_NETWORK_CONNECTED : CRYSTAL_NETWORK_DISCONNECTED, nullptr, 0, 0);
    if (err != ESP_OK) ESP_LOGW(TAG, "Network signal dropped: %s", esp_err_to_name(err));
    (void)previous;
    (void)crystal_ui_post(UI_EVT_NETWORK_CHANGED);
}

void wifi_event(IWifi::Event event, void *)
{
    crystal_network_policy_refresh();
    crystal_evt_t ui_event = UI_EVT_WIFI_DISCONNECTED;
    if (event == IWifi::GotIp) ui_event = UI_EVT_WIFI_GOT_IP;
    else if (event == IWifi::ScanDone) ui_event = UI_EVT_WIFI_SCAN_DONE;
    else if (event == IWifi::ConnectFailed) ui_event = UI_EVT_WIFI_CONNECT_FAILED;
    else if (event == IWifi::Connecting) ui_event = UI_EVT_WIFI_CONNECTING;
    (void)crystal_ui_post(ui_event);
}

void network_signal_handler(void *, esp_event_base_t, int32_t id, void *)
{
    if (id != CRYSTAL_NETWORK_CONNECTED || s_sntp_sync_started) return;
    if (!crystal_time_auto_enabled()) return;
    ESP_LOGI(TAG, "network connected signal received; starting SNTP");
    if (!s_sntp_initialized.load()) {
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        config.start = false;
        config.sync_cb = sntp_synced;
        const esp_err_t err = esp_netif_sntp_init(&config);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "SNTP initialization failed: %s", esp_err_to_name(err));
            return;
        }
        s_sntp_initialized.store(true);
    }
    const esp_err_t err = esp_netif_sntp_start();
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_sntp_sync_started = true;
        ESP_LOGI(TAG, "SNTP request started");
    }
    else ESP_LOGW(TAG, "SNTP start failed: %s", esp_err_to_name(err));
}

void update_timer_indicator(lv_timer_t *)
{
    if (s_timer_indicator == nullptr) return;
    const CrystalTimerState timer = crystal_timer_state();
    if (timer.running || timer.paused || s_stopwatch_running.load()) {
        lv_obj_clear_flag(s_timer_indicator, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_timer_indicator, LV_OBJ_FLAG_HIDDEN);
    }
}

void check_power_state(lv_timer_t *)
{
    if (s_display == nullptr || s_service_task == nullptr) return;
    if (s_button_wake_requested.exchange(false)) {
        lv_disp_trig_activity(s_display);
    }
    // Auto Dimming is the master switch for both timeouts. Off means the panel
    // stays at the user's brightness indefinitely, so restore Full if a timeout
    // had already taken it down before the switch was flipped.
    if (!crystal_power_auto_dim_enabled()) {
        if (s_power_state != PowerState::Full) {
            s_power_state = PowerState::Full;
            xTaskNotify(s_service_task, static_cast<uint32_t>(PowerState::Full), eSetValueWithOverwrite);
        }
        return;
    }
    const uint32_t inactive_ms = lv_disp_get_inactive_time(s_display);
    const uint32_t dim_ms = crystal_power_dim_seconds() * 1000u;
    const uint32_t off_ms = crystal_power_off_seconds() * 1000u;
    PowerState requested = PowerState::Full;
    if (off_ms != 0 && inactive_ms >= off_ms) requested = PowerState::Off;
    else if (dim_ms != 0 && inactive_ms >= dim_ms) requested = PowerState::Dim;
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

void dim_brightness()
{
    if (hal().brightness == nullptr) return;

    // Energy saving must never make a user-selected low brightness brighter.
    // Only apply the 20% dim target when the current level is above it.
    const uint8_t target = crystal_power_dim_level();
    if (hal().brightness->get() > target) {
        ramp_brightness(target);
    }
}

void power_saving_apply(bool enabled)
{
    // Energy Saving leaves CPU clocks unchanged; changes apply immediately.
    if (hal().wifi != nullptr) hal().wifi->set_power_save(enabled);
    const uint8_t target = crystal_effective_brightness(saved_brightness(), enabled);
    ramp_brightness(target);
}

void sntp_synced(struct timeval *tv)
{
    if (tv != nullptr && hal().rtc != nullptr) {
        struct tm corrected = {};
        if (localtime_r(&tv->tv_sec, &corrected) != nullptr) {
            (void)hal().rtc->write(&corrected);
        }
    }
    const int64_t monotonic_deadline = s_timer_deadline_us.load();
    if (monotonic_deadline > 0 && tv != nullptr) {
        const int64_t remaining_us = monotonic_deadline - esp_timer_get_time();
        if (remaining_us > 0) {
            int64_t expected = monotonic_deadline;
            if (s_timer_deadline_us.compare_exchange_strong(expected, 0)) {
                s_timer_end_at.store(static_cast<int64_t>(tv->tv_sec) +
                                     (remaining_us + 999999) / 1000000);
            }
        }
    }
    s_time_valid.store(true);
    s_time_revision.fetch_add(1);
    s_sntp_sync_started = false;
    if (tv != nullptr) {
        const int32_t last_sync = static_cast<int32_t>(tv->tv_sec);
        store_value("time.last_sync", last_sync);
    }
    ESP_LOGI(TAG, "SNTP synchronized system clock and RTC");
}

void service_task(void *)
{
    vTaskDelay(pdMS_TO_TICKS(1200));
    // Preserve the existing OTA settling interval, but do not assume it means
    // the shell has finished drawing. app_main explicitly releases this gate.
    xSemaphoreTake(s_startup_ready, portMAX_DELAY);
    ESP_LOGI(TAG, "Startup: first frame ready; starting background services");
    bool ota_confirmed = false;
    // Rollback-enabled OTA images boot as PENDING_VERIFY. Confirm only after
    // the display/core settling interval so an early boot failure can still
    // trigger the bootloader's automatic rollback.
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state = ESP_OTA_IMG_UNDEFINED;
    if (running != nullptr && esp_ota_get_state_partition(running, &ota_state) == ESP_OK &&
            ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "Confirming OTA image after 1.2s settling period");
        ESP_LOGI(TAG, "Partition: %s @ 0x%lx", running->label,
                 static_cast<unsigned long>(running->address));
        const esp_err_t confirm_err = esp_ota_mark_app_valid_cancel_rollback();
        if (confirm_err == ESP_OK) {
            crystal_update_clear_attempted_version();
            ESP_LOGI(TAG, "OTA image confirmed and rollback cancelled");
            ota_confirmed = true;
            // Log the state after confirmation
            esp_ota_img_states_t confirmed_state = ESP_OTA_IMG_UNDEFINED;
            if (esp_ota_get_state_partition(running, &confirmed_state) == ESP_OK) {
                ESP_LOGI(TAG, "Post-confirmation state: %d", confirmed_state);
            }
        } else {
            ESP_LOGE(TAG, "OTA image confirmation failed: %s", esp_err_to_name(confirm_err));
        }
    }
    static constexpr char kReadyMessage[] = "Core services ready";
    (void)crystal_ui_post(UI_EVT_TOAST, kReadyMessage, sizeof(kReadyMessage) - 1);
    if (ota_confirmed) (void)crystal_ui_post(UI_EVT_OTA_SUCCEEDED);

    (void)esp_netif_init();
    (void)esp_event_loop_create_default();
    crystal_network_set_callback(network_route_changed, nullptr);
    (void)esp_event_handler_instance_register(CRYSTAL_NETWORK_EVENT, CRYSTAL_NETWORK_CONNECTED,
                                              network_signal_handler, nullptr, &s_network_handler);
    if (hal().wifi != nullptr) {
        hal().wifi->set_event_callback(wifi_event, nullptr);
        hal().wifi->start();
    }
#if CONFIG_IDF_TARGET_ESP32P4
    if (crystal_ethernet_init() && !crystal_ethernet_start())
        ESP_LOGW(TAG, "Ethernet start failed; continuing with WiFi");
#endif
    crystal_network_policy_refresh();
    power_saving_apply(energy_saving_enabled());

    TickType_t last_battery_poll = 0;
    for (;;) {
        uint32_t command = 0;
        if (xTaskNotifyWait(0, UINT32_MAX, &command, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (command == static_cast<uint32_t>(PowerState::Full)) {
                const uint8_t saved = saved_brightness();
                ramp_brightness(crystal_effective_brightness(saved, energy_saving_enabled()));
            }
            else if (command == static_cast<uint32_t>(PowerState::Dim)) dim_brightness();
            else if (command == static_cast<uint32_t>(PowerState::Off)) ramp_brightness(0);
            else if (command == static_cast<uint32_t>(PowerState::ApplySaving)) {
                power_saving_apply(energy_saving_enabled());
            }
        }
        crystal_network_policy_refresh();
        const TickType_t now_ticks = xTaskGetTickCount();
        if ((last_battery_poll == 0 || now_ticks - last_battery_poll >= pdMS_TO_TICKS(30000)) &&
                hal().power != nullptr) {
            last_battery_poll = now_ticks;
            int percent = -1;
            bool charging = false;
            if (hal().power->readBattery(&percent, &charging)) {
                s_battery_percent.store(percent);
                s_battery_charging.store(charging);
                const uint8_t battery[] = {
                    static_cast<uint8_t>(static_cast<int8_t>(percent)),
                    static_cast<uint8_t>(charging),
                };
                (void)crystal_ui_post(UI_EVT_BATTERY, battery, sizeof(battery));
            }
        }
        const int64_t timer_end = s_timer_end_at.load();
        const int64_t monotonic_deadline = s_timer_deadline_us.load();
        if ((timer_end > 0 && static_cast<int64_t>(time(nullptr)) >= timer_end) ||
                (monotonic_deadline > 0 && esp_timer_get_time() >= monotonic_deadline)) {
            s_timer_end_at.store(0);
            s_timer_deadline_us.store(0);
            s_timer_duration.store(0);
            s_timer_paused_remaining.store(0);
            ESP_LOGI(TAG, "timer expired");
            if (crystal_sound_alerts_enabled()) crystal_hal_timer_alarm();
            (void)crystal_ui_post(UI_EVT_TIMER_EXPIRED);
        }
    }
}
} // namespace

ESP_EVENT_DEFINE_BASE(CRYSTAL_NETWORK_EVENT);

bool crystal_timezone_apply(const char *posix)
{
    if (posix == nullptr || posix[0] == '\0' || strlen(posix) >= 48) return false;
    if (hal().storage != nullptr &&
            !hal().storage->set("timezone", posix, strlen(posix) + 1)) return false;
    setenv("TZ", posix, 1);
    tzset();
    return true;
}

bool crystal_time_format_24()
{
    return cached_value<uint8_t>("time.format24", 0, s_cache_format24) != 0;
}
void crystal_time_set_format_24(bool enabled)
{
    store_cached<uint8_t>("time.format24", enabled ? 1 : 0, s_cache_format24);
    update_clock(nullptr);
}
bool crystal_time_auto_enabled() { return stored_value<uint8_t>("time.auto", 1) != 0; }
void crystal_time_set_auto(bool enabled)
{
    store_value<uint8_t>("time.auto", enabled ? 1 : 0);
    if (!enabled && s_sntp_initialized.load()) {
        esp_netif_sntp_deinit();
        s_sntp_initialized.store(false);
        s_sntp_sync_started.store(false);
    } else if (enabled && crystal_network_has_ip()) {
        network_signal_handler(nullptr, CRYSTAL_NETWORK_EVENT, CRYSTAL_NETWORK_CONNECTED, nullptr);
    }
}
int32_t crystal_time_last_sync() { return stored_value<int32_t>("time.last_sync", 0); }

uint16_t crystal_power_dim_seconds()
{
    static constexpr uint16_t allowed[] = {0, 15, 30, 60, 120, 300};
    const bool saving = energy_saving_enabled();
    const uint16_t fallback = saving ? 30 : 120;
    // Separate profiles replace the old shared timeout and implicit halving.
    const uint16_t value = cached_value<uint16_t>(saving ? "power.dim_save" : "power.dim_norm",
        fallback, saving ? s_cache_saving_dim_seconds : s_cache_dim_seconds);
    return one_of(value, allowed, sizeof(allowed) / sizeof(allowed[0])) ? value : fallback;
}
uint16_t crystal_power_off_seconds()
{
    static constexpr uint16_t allowed[] = {0, 60, 120, 300, 900};
    const uint16_t value = cached_value<uint16_t>("power.off_s", 300, s_cache_off_seconds);
    const uint16_t off = one_of(value, allowed, sizeof(allowed) / sizeof(allowed[0])) ? value : 300;
    const uint16_t dim = crystal_power_dim_seconds();
    // Old 60-second settings must not bypass the new two-minute dim stage.
    return off != 0 && dim != 0 && off <= dim ? (dim < 300 ? 300 : 900) : off;
}
uint8_t crystal_power_dim_level()
{
    const uint8_t value = stored_value<uint8_t>("power.dim_level", 20);
    return value >= kDimBrightnessMin && value <= 50 ? value : 20;
}
bool crystal_power_saving_enabled() { return energy_saving_enabled(); }
bool crystal_power_auto_dim_enabled()
{
    return cached_value<uint8_t>("power.auto_dim", 1, s_cache_auto_dim) != 0;
}
void crystal_power_set_auto_dim(bool enabled)
{
    store_cached<uint8_t>("power.auto_dim", enabled ? 1 : 0, s_cache_auto_dim);
}
void crystal_power_set_dim_seconds(uint16_t seconds)
{
    static constexpr uint16_t allowed[] = {0, 15, 30, 60, 120, 300};
    if (one_of(seconds, allowed, sizeof(allowed) / sizeof(allowed[0])) &&
            (crystal_power_off_seconds() == 0 || seconds == 0 || crystal_power_off_seconds() > seconds)) {
        const bool saving = energy_saving_enabled();
        store_cached(saving ? "power.dim_save" : "power.dim_norm", seconds,
                     saving ? s_cache_saving_dim_seconds : s_cache_dim_seconds);
    }
}
void crystal_power_set_off_seconds(uint16_t seconds)
{
    static constexpr uint16_t allowed[] = {0, 60, 120, 300, 900};
    if (one_of(seconds, allowed, sizeof(allowed) / sizeof(allowed[0])) &&
            (seconds == 0 || crystal_power_dim_seconds() == 0 || seconds > crystal_power_dim_seconds())) {
        store_cached("power.off_s", seconds, s_cache_off_seconds);
    }
}
void crystal_power_set_dim_level(uint8_t level)
{
    if (level >= kDimBrightnessMin && level <= 50) store_value("power.dim_level", level);
}
void crystal_power_set_saving(bool enabled)
{
    store_cached<uint8_t>("power.saving", enabled ? 1 : 0, s_cache_saving);
    if (s_service_task != nullptr) {
        xTaskNotify(s_service_task, static_cast<uint32_t>(PowerState::ApplySaving), eSetValueWithOverwrite);
    }
}
void crystal_brightness_set(uint8_t level)
{
    if (level > kFullBrightness) level = kFullBrightness;
    store_value("brightness", level);
    if (hal().brightness != nullptr) {
        hal().brightness->set(crystal_effective_brightness(level, crystal_power_saving_enabled()));
    }
}
uint8_t crystal_brightness_level() { return saved_brightness(); }
uint32_t crystal_lua_callback_timeout_seconds()
{
    return cached_value<uint8_t>("dev.lua_long", 0, s_cache_lua_extended) == 1 ? 30 : 5;
}

bool crystal_lua_set_extended_timeout(bool enabled)
{
    const uint8_t value = enabled ? 1 : 0;
    if (hal().storage == nullptr || !hal().storage->set("dev.lua_long", &value, sizeof(value))) return false;
    s_cache_lua_extended.store(value, std::memory_order_relaxed);
    return true;
}

bool crystal_sound_alerts_enabled() { return stored_value<uint8_t>("sound.alerts", 1) != 0; }
void crystal_sound_set_alerts(bool enabled)
{
    store_value<uint8_t>("sound.alerts", enabled ? 1 : 0);
}
bool crystal_battery_cached(int *percent, bool *charging)
{
    if (percent == nullptr || charging == nullptr || s_battery_percent.load() < 0) return false;
    *percent = s_battery_percent.load();
    *charging = s_battery_charging.load();
    return true;
}

bool crystal_time_is_ready() { return s_time_valid.load(); }

uint32_t crystal_time_subscribe_ready(crystal_time_ready_cb_t callback, void *context)
{
    if (!callback || !s_time_next_id) return 0;
    for (auto &listener : s_time_listeners) {
        if (!listener.id) {
            listener = {s_time_next_id++, 0, callback, context};
            return listener.id;
        }
    }
    return 0;
}

bool crystal_time_unsubscribe_ready(uint32_t subscription)
{
    for (auto &listener : s_time_listeners) {
        if (subscription && listener.id == subscription) { listener = {}; return true; }
    }
    return false;
}

bool crystal_net_is_ready() { return crystal_network_has_ip(); }

uint32_t crystal_net_subscribe(crystal_net_change_cb_t callback, void *context)
{
    if (!callback || !s_net_next_id) return 0;
    for (auto &listener : s_net_listeners) {
        if (!listener.id) {
            listener = {s_net_next_id++, 0, false, false, callback, context};
            return listener.id;
        }
    }
    return 0;
}

bool crystal_net_unsubscribe(uint32_t subscription)
{
    for (auto &listener : s_net_listeners) {
        if (subscription && listener.id == subscription) { listener = {}; return true; }
    }
    return false;
}

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
        s_time_revision.fetch_add(1);
        ESP_LOGI(TAG, "System time initialized from PCF85063");
    }
}

bool crystal_time_set(const struct tm *local_time)
{
    if (local_time == nullptr || hal().rtc == nullptr) return false;
    struct tm adjusted = *local_time;
    adjusted.tm_isdst = -1;
    const time_t epoch = mktime(&adjusted);
    if (epoch < 1577836800 || adjusted.tm_year != local_time->tm_year ||
            adjusted.tm_mon != local_time->tm_mon || adjusted.tm_mday != local_time->tm_mday ||
            adjusted.tm_hour != local_time->tm_hour || adjusted.tm_min != local_time->tm_min) return false;

    struct timeval tv = {};
    tv.tv_sec = epoch;
    if (settimeofday(&tv, nullptr) != 0 || !hal().rtc->write(&adjusted)) return false;
    s_time_valid.store(true);
    s_time_revision.fetch_add(1);
    ESP_LOGI(TAG, "System and PCF85063 time updated");
    return true;
}

bool crystal_timer_start(uint32_t duration_seconds)
{
    if (duration_seconds == 0 || duration_seconds > 24U * 60U * 60U) return false;
    s_timer_paused_remaining.store(0);
    s_timer_duration.store(duration_seconds);
    if (s_time_valid.load()) {
        s_timer_deadline_us.store(0);
        s_timer_end_at.store(static_cast<int64_t>(time(nullptr)) + duration_seconds);
    } else {
        s_timer_end_at.store(0);
        s_timer_deadline_us.store(esp_timer_get_time() +
                                  static_cast<int64_t>(duration_seconds) * 1000000);
    }
    ESP_LOGI(TAG, "timer started: %lu seconds", static_cast<unsigned long>(duration_seconds));
    return true;
}

bool crystal_timer_restore(time_t end_at, uint32_t paused_remaining, uint32_t duration_seconds)
{
    const time_t now = time(nullptr);
    if (duration_seconds == 0 || duration_seconds > 24U * 60U * 60U) {
        crystal_timer_reset();
        return false;
    }
    if (now >= 1577836800 && end_at > now) {
        s_timer_duration.store(duration_seconds);
        s_timer_paused_remaining.store(0);
        s_timer_end_at.store(static_cast<int64_t>(end_at));
        s_timer_deadline_us.store(0);
        return true;
    }
    if (paused_remaining > 0 && paused_remaining <= 24U * 60U * 60U) {
        s_timer_duration.store(duration_seconds);
        s_timer_end_at.store(0);
        s_timer_deadline_us.store(0);
        s_timer_paused_remaining.store(paused_remaining);
        return true;
    }
    s_timer_end_at.store(0);
    s_timer_deadline_us.store(0);
    s_timer_duration.store(0);
    s_timer_paused_remaining.store(0);
    return false;
}

bool crystal_timer_pause()
{
    const int64_t end_at = s_timer_end_at.exchange(0);
    const int64_t monotonic_deadline = s_timer_deadline_us.exchange(0);
    int64_t remaining = 0;
    if (end_at > 0) {
        remaining = end_at - static_cast<int64_t>(time(nullptr));
    } else if (monotonic_deadline > 0) {
        remaining = (monotonic_deadline - esp_timer_get_time() + 999999) / 1000000;
    }
    if (remaining <= 0) {
        s_timer_paused_remaining.store(0);
        return false;
    }
    s_timer_paused_remaining.store(static_cast<uint32_t>(remaining));
    ESP_LOGI(TAG, "timer paused");
    return true;
}

bool crystal_timer_resume()
{
    const uint32_t remaining = s_timer_paused_remaining.exchange(0);
    if (remaining == 0) return false;
    if (s_time_valid.load()) {
        s_timer_deadline_us.store(0);
        s_timer_end_at.store(static_cast<int64_t>(time(nullptr)) + remaining);
    } else {
        s_timer_end_at.store(0);
        s_timer_deadline_us.store(esp_timer_get_time() +
                                  static_cast<int64_t>(remaining) * 1000000);
    }
    ESP_LOGI(TAG, "timer resumed");
    return true;
}

void crystal_timer_reset()
{
    s_timer_end_at.store(0);
    s_timer_deadline_us.store(0);
    s_timer_duration.store(0);
    s_timer_paused_remaining.store(0);
    ESP_LOGI(TAG, "timer reset");
}

CrystalTimerState crystal_timer_state()
{
    CrystalTimerState result = {};
    const int64_t now = static_cast<int64_t>(time(nullptr));
    const int64_t end_at = s_timer_end_at.load();
    const int64_t monotonic_deadline = s_timer_deadline_us.load();
    result.duration_seconds = s_timer_duration.load();
    const uint32_t paused = s_timer_paused_remaining.load();
    if (end_at > now) {
        result.running = true;
        result.remaining_seconds = static_cast<uint32_t>(end_at - now);
        result.end_at = static_cast<time_t>(end_at);
    } else if (monotonic_deadline > esp_timer_get_time()) {
        result.running = true;
        result.remaining_seconds = static_cast<uint32_t>(
            (monotonic_deadline - esp_timer_get_time() + 999999) / 1000000);
    } else if (paused > 0) {
        result.paused = true;
        result.remaining_seconds = paused;
    }
    return result;
}

void crystal_stopwatch_set_running(bool running)
{
    s_stopwatch_running.store(running);
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

extern "C" bool crystal_ui_post_ota_changed(void)
{
    return crystal_ui_post(UI_EVT_OTA_CHANGED);
}

bool crystal_core_consume_wake_touch()
{
    if (s_power_state != PowerState::Off) {
        return false;
    }
    s_power_state = PowerState::Full;
    if (s_service_task != nullptr) {
        xTaskNotify(s_service_task, static_cast<uint32_t>(PowerState::Full), eSetValueWithOverwrite);
    }
    return true;
}

void crystal_core_wake_display()
{
    s_button_wake_requested.store(true);
    s_power_state = PowerState::Full;
    if (s_service_task != nullptr) {
        xTaskNotify(s_service_task, static_cast<uint32_t>(PowerState::Full), eSetValueWithOverwrite);
    }
}

bool crystal_core_init(void *display, crystal_clock_update_cb_t clock_update,
                       crystal_connectivity_update_cb_t connectivity_update,
                       crystal_battery_update_cb_t battery_update, void *status_context)
{
    if (s_queue != nullptr) return true;
    if (display == nullptr || clock_update == nullptr || connectivity_update == nullptr ||
            battery_update == nullptr || status_context == nullptr) return false;

    // The HTTP worker can start as soon as a restored package runs its
    // onCreate hook. Initialize lwIP before creating that worker; otherwise a
    // request queued during boot can enter esp_http_client while tcpip's
    // mailbox is still invalid (tcpip_send_msg_wait_sem assertion).
    const esp_err_t netif_err = esp_netif_init();
    if (netif_err != ESP_OK && netif_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "failed to initialize esp_netif: %s", esp_err_to_name(netif_err));
        return false;
    }

    // Apps register handlers during installation, before the service task has
    // a chance to create the default loop.
    const esp_err_t event_loop_err = esp_event_loop_create_default();
    if (event_loop_err != ESP_OK && event_loop_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "failed to create default event loop: %s",
                 esp_err_to_name(event_loop_err));
        return false;
    }

    if (!crystal_http_init()) {
        ESP_LOGE(TAG, "failed to initialize crystal_http");
        return false;
    }

    s_clock_update = clock_update;
    s_connectivity_update = connectivity_update;
    s_battery_update = battery_update;
    s_status_context = status_context;
    s_display = static_cast<lv_disp_t *>(display);
    s_queue = xQueueCreate(kQueueDepth, sizeof(EventMessage));
    if (s_queue == nullptr) return false;
    if (!init_toast()) return false;
    if (!init_clock_placeholder()) return false;
    if (!init_timer_indicator()) return false;
    if (lv_timer_create(drain_event_queue, 50, nullptr) == nullptr) return false;
    if (lv_timer_create(update_clock, 1000, nullptr) == nullptr) return false;
    if (lv_timer_create(update_connectivity, 1000, nullptr) == nullptr) return false;
    if (lv_timer_create(update_timer_indicator, 250, nullptr) == nullptr) return false;
    if (lv_timer_create(check_power_state, 250, nullptr) == nullptr) return false;
    update_clock(nullptr);
    update_connectivity(nullptr);
    s_startup_ready = xSemaphoreCreateBinary();
    if (s_startup_ready == nullptr) return false;
    if (xTaskCreatePinnedToCore(service_task, "crystal_service", 8192, nullptr, 2,
                                &s_service_task, 0) != pdPASS) {
        vSemaphoreDelete(s_startup_ready);
        s_startup_ready = nullptr;
        return false;
    }

    return true;
}

void crystal_core_start_services()
{
    if (s_startup_ready != nullptr) xSemaphoreGive(s_startup_ready);
}

void crystal_core_show_toast(const char *message)
{
    if (message != nullptr) {
        (void)crystal_ui_post(UI_EVT_TOAST, message, strnlen(message, kEventDataMax));
    }
}
