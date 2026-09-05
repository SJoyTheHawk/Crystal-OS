/* SPDX-License-Identifier: MIT */

#include "crystal_core.hpp"

#include <atomic>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "crystal_hal.hpp"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lvgl.h"

extern void crystal_shell_wifi_event(uint8_t event);
extern void crystal_shell_weather_event(const CrystalWeatherReading *reading);

namespace {
constexpr size_t kQueueDepth = 12;
constexpr size_t kEventDataMax = 64;
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
lv_obj_t *s_timer_indicator = nullptr;
lv_timer_t *s_toast_hide_timer = nullptr;
TaskHandle_t s_service_task = nullptr;
lv_disp_t *s_display = nullptr;
crystal_clock_update_cb_t s_clock_update = nullptr;
crystal_connectivity_update_cb_t s_connectivity_update = nullptr;
crystal_battery_update_cb_t s_battery_update = nullptr;
void *s_status_context = nullptr;
PowerState s_power_state = PowerState::Full;
std::atomic_bool s_time_valid{false};
std::atomic_bool s_sntp_initialized{false};
std::atomic_bool s_sntp_sync_started{false};
esp_event_handler_instance_t s_network_handler = nullptr;
std::atomic<int64_t> s_timer_end_at{0};
std::atomic<int64_t> s_timer_deadline_us{0};
std::atomic<uint32_t> s_timer_duration{0};
std::atomic<uint32_t> s_timer_paused_remaining{0};
std::atomic_bool s_stopwatch_running{false};
std::atomic_bool s_weather_request{false};
std::atomic<int64_t> s_weather_last_fetch{0};
// Retry pacing for automatic fetches. Ticks, not epoch seconds: the retry clock
// must not jump when SNTP steps the wall clock mid-backoff.
constexpr uint32_t kWeatherRetryMinMs = 60 * 1000;
constexpr uint32_t kWeatherRetryMaxMs = 30 * 60 * 1000;
std::atomic<uint32_t> s_weather_retry_ms{kWeatherRetryMinMs};
std::atomic<TickType_t> s_weather_next_try{0};
// Location resolution needs the same treatment: it is one TLS handshake per
// attempt, and without pacing a failing endpoint is retried every loop pass.
std::atomic<TickType_t> s_weather_next_locate{0};
std::atomic<uint32_t> s_weather_locate_ms{kWeatherRetryMinMs};
static char s_weather_response[1024];
double s_weather_latitude = 22.3193;
double s_weather_longitude = 114.1694;
char s_weather_city[24] = "Hong Kong (fallback)";
std::atomic_bool s_weather_location_ready{false};

// Fetches a small JSON document into s_weather_response, NUL-terminated.
//
// Do NOT use esp_http_client_perform() here. It drains the body internally to
// satisfy content_length, does not cache it (response->buffer->output_ptr is
// NULL), resets raw_len to 0, and closes the connection -- so a following
// esp_http_client_read_response() returns 0 bytes and every parse fails. The
// open/fetch_headers/read/close sequence is what hands the body to a caller.
bool http_get_json(const char *url)
{
    memset(s_weather_response, 0, sizeof(s_weather_response));
    esp_http_client_config_t config = {};
    config.url = url;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = 8000;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGW(TAG, "http client allocation failed");
        return false;
    }

    bool ok = false;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        // Negative content_length means chunked; read until the socket ends.
        const int64_t declared = esp_http_client_fetch_headers(client);
        const int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            int total = 0;
            const int limit = static_cast<int>(sizeof(s_weather_response)) - 1;
            while (total < limit) {
                const int read = esp_http_client_read(client, s_weather_response + total, limit - total);
                if (read <= 0) break;      // 0 = complete, <0 = error
                total += read;
            }
            s_weather_response[total] = '\0';
            ok = total > 0;
            if (!ok) ESP_LOGW(TAG, "empty body for %s (declared %lld)", url, static_cast<long long>(declared));
            else if (declared > limit) ESP_LOGW(TAG, "body truncated: %lld > %d bytes", static_cast<long long>(declared), limit);
        } else {
            ESP_LOGW(TAG, "http status %d for %s", status, url);
        }
        esp_http_client_close(client);
    } else {
        ESP_LOGW(TAG, "http open failed for %s", url);
    }
    esp_http_client_cleanup(client);
    return ok;
}

bool load_weather_location()
{
    if (hal().storage == nullptr) return false;
    double lat = 0.0, lon = 0.0; size_t n = sizeof(lat);
    if (!hal().storage->get("weather.lat", &lat, &n) || n != sizeof(lat)) return false;
    n = sizeof(lon);
    if (!hal().storage->get("weather.lon", &lon, &n) || n != sizeof(lon)) return false;
    n = sizeof(s_weather_city) - 1;
    if (hal().storage->get("weather.city", s_weather_city, &n)) s_weather_city[n < sizeof(s_weather_city) ? n : sizeof(s_weather_city) - 1] = '\0';
    s_weather_latitude = lat; s_weather_longitude = lon;
    return lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}

void resolve_weather_location()
{
    if (s_weather_location_ready.load() || load_weather_location()) { s_weather_location_ready.store(true); return; }

    // ipapi.co is behind a Cloudflare interstitial and answers 403 to a device,
    // so it is not a usable fallback. ipwho.is answers 200; ?fields= trims the
    // body from 957 bytes to ~106, well clear of the response buffer.
    const char *endpoints[] = {
        "https://ipwho.is/?fields=success,city,latitude,longitude",
    };
    for (const char *endpoint : endpoints) {
        if (!http_get_json(endpoint)) continue;

        double lat = 0.0, lon = 0.0; char city[24] = {};
        const char *p = strstr(s_weather_response, "\"latitude\"");
        const char *q = strstr(s_weather_response, "\"longitude\"");
        const char *c = strstr(s_weather_response, "\"city\"");
        // The body is pretty-printed, so a literal ":\"" does not match. Skip
        // the key, then let %lf consume the whitespace and scan_quoted find the
        // opening quote wherever it is.
        if (p != nullptr && q != nullptr &&
                sscanf(p + strlen("\"latitude\""), " :%lf", &lat) == 1 &&
                sscanf(q + strlen("\"longitude\""), " :%lf", &lon) == 1 &&
                lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0) {
            if (c != nullptr) {
                const char *open_quote = strchr(c + strlen("\"city\""), '"');
                if (open_quote != nullptr) sscanf(open_quote + 1, "%23[^\"]", city);
            }
            s_weather_latitude = lat; s_weather_longitude = lon;
            if (city[0] != '\0') strlcpy(s_weather_city, city, sizeof(s_weather_city));
            if (hal().storage != nullptr) {
                (void)hal().storage->set("weather.lat", &lat, sizeof(lat));
                (void)hal().storage->set("weather.lon", &lon, sizeof(lon));
                (void)hal().storage->set("weather.city", s_weather_city, strlen(s_weather_city) + 1);
            }
            s_weather_location_ready.store(true);
            ESP_LOGI(TAG, "weather location resolved: %.4f, %.4f (%s)", lat, lon, s_weather_city);
            return;
        }
        ESP_LOGW(TAG, "weather location parse failed for %s", endpoint);
    }
    // Do not retry every service tick; retain the compiled fallback.
    s_weather_location_ready.store(true);
    strlcpy(s_weather_city, "Hong Kong (fallback)", sizeof(s_weather_city));
    ESP_LOGW(TAG, "weather location unavailable; using Hong Kong fallback");
}

bool weather_fetch(CrystalWeatherReading *out)
{
    if (out == nullptr || hal().wifi == nullptr || !hal().wifi->connected()) return false;
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m",
             s_weather_latitude, s_weather_longitude);
    bool ok = false;
    if (http_get_json(url)) {
        // "current_units" precedes "current" in the body and repeats every key
        // name with a string value. The closing quote in the pattern is what
        // separates them; ':{' additionally pins this to the values object.
        const char *current = strstr(s_weather_response, "\"current\":{");
        const char *temp = current != nullptr ? strstr(current, "\"temperature_2m\":") : nullptr;
        const char *humidity = current != nullptr ? strstr(current, "\"relative_humidity_2m\":") : nullptr;
        const char *code = current != nullptr ? strstr(current, "\"weather_code\":") : nullptr;
        const char *wind = current != nullptr ? strstr(current, "\"wind_speed_10m\":") : nullptr;
        double t = 0.0, w = 0.0; int h = 0, c = 0;
        if (temp != nullptr && humidity != nullptr && code != nullptr && wind != nullptr &&
                sscanf(temp, "\"temperature_2m\":%lf", &t) == 1 &&
                sscanf(humidity, "\"relative_humidity_2m\":%d", &h) == 1 &&
                sscanf(code, "\"weather_code\":%d", &c) == 1 &&
                sscanf(wind, "\"wind_speed_10m\":%lf", &w) == 1) {
            out->temperature_c10 = static_cast<int16_t>(t * 10.0);
            out->humidity = static_cast<uint8_t>(h < 0 ? 0 : h > 100 ? 100 : h);
            out->weather_code = static_cast<uint8_t>(c);
            out->wind_kmh10 = static_cast<uint16_t>(w * 10.0);
            out->fetched_at = static_cast<int32_t>(time(nullptr));
            out->success = true;
            strlcpy(out->city, s_weather_city, sizeof(out->city));
            ok = true;
            ESP_LOGI(TAG, "weather fetched: %.1f C, humidity %d%%, code %d, wind %.1f km/h", t, h, c, w);
        }
    }
    if (!ok) ESP_LOGW(TAG, "weather parse failed; body: %.120s", s_weather_response);
    return ok;
}

void update_connectivity(lv_timer_t *timer);
void sntp_synced(struct timeval *tv);

bool energy_saving_enabled()
{
    if (hal().storage == nullptr) return false;
    uint8_t value = 0;
    size_t length = sizeof(value);
    return hal().storage->get("power.saving", &value, &length) &&
           length == sizeof(value) && value != 0;
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
        } else if (message.type == UI_EVT_TOAST) {
            char text[kEventDataMax + 1] = {};
            memcpy(text, message.data, message.length);
            show_toast(text);
            ESP_LOGI(TAG, "toast displayed: %s", text);
        } else if (message.type == UI_EVT_TIME_SYNCED) {
            show_toast("Time synchronized");
        } else if (message.type == UI_EVT_TIMER_EXPIRED) {
            show_toast("Timer finished");
        } else if (message.type == UI_EVT_WEATHER && message.length == sizeof(CrystalWeatherReading)) {
            crystal_shell_weather_event(reinterpret_cast<const CrystalWeatherReading *>(message.data));
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
    const int hour_12 = now.tm_hour % 12 == 0 ? 12 : now.tm_hour % 12;
    s_clock_update(s_status_context, hour_12, now.tm_min, now.tm_hour >= 12);
}

void update_connectivity(lv_timer_t *)
{
    if (s_connectivity_update != nullptr && hal().wifi != nullptr) {
        s_connectivity_update(s_status_context, hal().wifi->connected());
    }
}

void wifi_event(IWifi::Event event, void *)
{
    if (event == IWifi::GotIp) {
        // The lease just landed, so any backoff accumulated while offline is
        // stale -- it was measuring a missing network, not an unwell server.
        // Clear it so the first fetch happens now instead of minutes from now.
        s_weather_retry_ms.store(kWeatherRetryMinMs);
        s_weather_next_try.store(0);
        s_weather_locate_ms.store(kWeatherRetryMinMs);
        s_weather_next_locate.store(0);
        ESP_LOGI(TAG, "publishing network connected signal");
        const esp_err_t err = esp_event_post(CRYSTAL_NETWORK_EVENT, CRYSTAL_NETWORK_CONNECTED,
                                             nullptr, 0, 0);
        if (err != ESP_OK) ESP_LOGW(TAG, "network connected signal dropped: %s", esp_err_to_name(err));
    } else if (event == IWifi::Disconnected || event == IWifi::ConnectFailed) {
        s_sntp_sync_started = false;
        ESP_LOGI(TAG, "publishing network disconnected signal (%s)",
                 event == IWifi::ConnectFailed ? "connect failed" : "disconnected");
        const esp_err_t err = esp_event_post(CRYSTAL_NETWORK_EVENT, CRYSTAL_NETWORK_DISCONNECTED,
                                             nullptr, 0, 0);
        if (err != ESP_OK) ESP_LOGW(TAG, "network disconnected signal dropped: %s", esp_err_to_name(err));
    }
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
    const uint32_t inactive_ms = lv_disp_get_inactive_time(s_display);
    PowerState requested = PowerState::Full;
    if (energy_saving_enabled()) {
        if (inactive_ms >= kOffTimeoutMs) requested = PowerState::Off;
        else if (inactive_ms >= kDimTimeoutMs) requested = PowerState::Dim;
    }
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
    s_sntp_sync_started = false;
    ESP_LOGI(TAG, "SNTP synchronized system clock and RTC");
    (void)crystal_ui_post(UI_EVT_TIME_SYNCED);
}

void service_task(void *)
{
    vTaskDelay(pdMS_TO_TICKS(1200));
    static constexpr char kReadyMessage[] = "Core services ready";
    (void)crystal_ui_post(UI_EVT_TOAST, kReadyMessage, sizeof(kReadyMessage) - 1);

    if (hal().wifi != nullptr) {
        hal().wifi->set_event_callback(wifi_event, nullptr);
        (void)esp_netif_init();
        (void)esp_event_loop_create_default();
        (void)esp_event_handler_instance_register(CRYSTAL_NETWORK_EVENT, CRYSTAL_NETWORK_CONNECTED,
                                                  network_signal_handler, nullptr, &s_network_handler);
        hal().wifi->start();
    }

    TickType_t last_battery_poll = 0;
    for (;;) {
        uint32_t command = 0;
        if (xTaskNotifyWait(0, UINT32_MAX, &command, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (command == static_cast<uint32_t>(PowerState::Full)) ramp_brightness(saved_brightness());
            else if (command == static_cast<uint32_t>(PowerState::Dim)) ramp_brightness(kDimBrightness);
            else if (command == static_cast<uint32_t>(PowerState::Off)) ramp_brightness(0);
        }
        const int64_t now_epoch = static_cast<int64_t>(time(nullptr));
        const TickType_t tick_now = xTaskGetTickCount();
        // has_ip(), not connected(). Association happens ~1 s before DHCP
        // finishes, and a fetch in that window dies in getaddrinfo() with
        // EAI_FAIL -- which then burned a backoff step on a failure that was
        // only ever a race with the DHCP lease.
        const bool online = hal().wifi != nullptr && hal().wifi->has_ip();

        // Automatic refresh. The retry gate is what makes a failure survivable:
        // last_fetch is only stored on success, so without it a failed fetch left
        // this condition true and the branch ran every loop pass -- one DNS+TLS
        // attempt per second, indefinitely. The `last_fetch > 0` guard used to
        // sit here too, which meant a device that had never fetched successfully
        // never auto-retried at all. Both directions were wrong.
        const bool due = s_weather_last_fetch.load() == 0 ||
                         (now_epoch > 0 && now_epoch - s_weather_last_fetch.load() >= 1800);
        const TickType_t next_try = s_weather_next_try.load();
        if (online && due && (next_try == 0 || tick_now >= next_try)) {
            s_weather_request.store(true);
        }
        if (online && !s_weather_location_ready.load()) {
            const TickType_t next_locate = s_weather_next_locate.load();
            if (next_locate == 0 || tick_now >= next_locate) {
                resolve_weather_location();
                if (s_weather_location_ready.load()) {
                    s_weather_locate_ms.store(kWeatherRetryMinMs);
                }
                else {
                    const uint32_t wait = s_weather_locate_ms.load();
                    s_weather_next_locate.store(tick_now + pdMS_TO_TICKS(wait));
                    s_weather_locate_ms.store(wait >= kWeatherRetryMaxMs / 2 ? kWeatherRetryMaxMs
                                                                            : wait * 2);
                    ESP_LOGW(TAG, "weather location retry in %u s", (unsigned)(wait / 1000));
                }
            }
        }
        if (s_weather_request.load()) {
            s_weather_request.store(false);
            CrystalWeatherReading reading = {};
            // Answer even when offline. The request used to be left pending
            // until WiFi came back, so a caller waiting on a result waited
            // forever and had no way to tell a slow fetch from no network.
            if (online && weather_fetch(&reading)) {
                s_weather_last_fetch.store(reading.fetched_at);
                s_weather_retry_ms.store(kWeatherRetryMinMs);
                s_weather_next_try.store(0);
            }
            else {
                reading.success = false;
                // Back off only the automatic trigger. An app-initiated request
                // still goes straight through, so pulling to refresh stays
                // responsive while an idle device stops hammering the network.
                const uint32_t wait = s_weather_retry_ms.load();
                s_weather_next_try.store(tick_now + pdMS_TO_TICKS(wait));
                s_weather_retry_ms.store(wait >= kWeatherRetryMaxMs / 2 ? kWeatherRetryMaxMs
                                                                        : wait * 2);
                ESP_LOGW(TAG, "weather fetch failed; next automatic try in %u s",
                         (unsigned)(wait / 1000));
            }
            (void)crystal_ui_post(UI_EVT_WEATHER, &reading, sizeof(reading));
        }
        const TickType_t now_ticks = xTaskGetTickCount();
        if ((last_battery_poll == 0 || now_ticks - last_battery_poll >= pdMS_TO_TICKS(30000)) &&
                hal().power != nullptr) {
            last_battery_poll = now_ticks;
            int percent = -1;
            bool charging = false;
            if (hal().power->readBattery(&percent, &charging)) {
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
            crystal_hal_timer_alarm();
            (void)crystal_ui_post(UI_EVT_TIMER_EXPIRED);
        }
    }
}
} // namespace

ESP_EVENT_DEFINE_BASE(CRYSTAL_NETWORK_EVENT);

void crystal_weather_request() { s_weather_request.store(true); }

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

bool crystal_core_init(void *display, crystal_clock_update_cb_t clock_update,
                       crystal_connectivity_update_cb_t connectivity_update,
                       crystal_battery_update_cb_t battery_update, void *status_context)
{
    if (s_queue != nullptr) return true;
    if (display == nullptr || clock_update == nullptr || connectivity_update == nullptr ||
            battery_update == nullptr || status_context == nullptr) return false;
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
    return xTaskCreatePinnedToCore(service_task, "crystal_service", 8192, nullptr, 2, &s_service_task, 0) == pdPASS;
}
