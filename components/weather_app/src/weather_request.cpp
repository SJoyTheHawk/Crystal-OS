/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "weather_request.hpp"
#include "crystal_http.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "cJSON.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

struct WeatherRequest::Pending {
    std::atomic<unsigned> references{2};
    std::atomic_bool done{false};
    bool success = false;
    char body[1024]{};
    void release() { if (references.fetch_sub(1) == 1) delete this; }
    static void completed(const crystal_http_response_t *response, void *context) {
        auto *p = static_cast<Pending *>(context);
        p->success = response && response->transport_error == ESP_OK && response->status_code == 200 &&
            response->body && response->body_len > 0 && response->body_len < sizeof(p->body);
        if (p->success) memcpy(p->body, response->body, response->body_len);
        if (response) crystal_http_response_release(response);
        p->done.store(true, std::memory_order_release);
        p->release();
    }
};
namespace {
bool number(const cJSON *object, const char *key, double &value) {
    const auto *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble)) return false;
    value = item->valuedouble; return true;
}
}
WeatherRequest::~WeatherRequest() { cancel(); }
void WeatherRequest::cancel() {
    if (request_id_) {
        ESP_LOGI("weather_request", "cancel id=%lu", static_cast<unsigned long>(request_id_));
        crystal_http_cancel(request_id_);
    }
    request_id_ = 0;
    if (pending_) { pending_->release(); pending_ = nullptr; }
}
bool WeatherRequest::submit(const char *url) {
    auto *p = new (std::nothrow) Pending;
    if (!p) return false;
    crystal_http_options_t options{};
    options.url = url;
#if CONFIG_IDF_TARGET_ESP32P4
    options.timeout_ms = 15000;
#else
    options.timeout_ms = 8000;
#endif
    options.max_attempts = 1;
    options.max_body_bytes = sizeof(p->body) - 1;
    options.owner_id = 0x57454154; // WEAT; session cancels exact request IDs.
    request_id_ = crystal_http_get(&options, Pending::completed, p);
    if (!request_id_) { p->release(); p->release(); return false; }
    ESP_LOGI("weather_request", "queued %s id=%lu", locating_ ? "location" : "forecast",
             static_cast<unsigned long>(request_id_));
    pending_ = p;
    deadline_us_ = esp_timer_get_time() + 30000000;
    return true;
}
bool WeatherRequest::forecast() {
    locating_ = false;
    char url[256];
    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m",
        location_.latitude, location_.longitude);
    return submit(url);
}
bool WeatherRequest::start() {
    cancel(); location_ = crystal_location_snapshot();
    locating_ = location_.automatic && !location_.resolved;
    return locating_ ? submit("https://ipwho.is/?fields=success,city,latitude,longitude") : forecast();
}
bool WeatherRequest::poll(CrystalWeatherReading &reading) {
    if (!pending_) return false;
    reading = {};
    // Settings changes and timeouts invalidate this session before delivery.
    if (crystal_location_snapshot().revision != location_.revision || esp_timer_get_time() >= deadline_us_) {
        cancel(); return true;
    }
    if (!pending_->done.load(std::memory_order_acquire)) return false;
    auto *p = pending_; pending_ = nullptr; request_id_ = 0;
    cJSON *json = p->success ? cJSON_Parse(p->body) : nullptr;
    p->release();
    if (locating_) {
        double lat, lon;
        const auto *city = cJSON_GetObjectItemCaseSensitive(json, "city");
        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "success")) &&
            number(json, "latitude", lat) && number(json, "longitude", lon) && cJSON_IsString(city)) {
            if (crystal_location_accept(location_.revision, lat, lon, city->valuestring))
                location_ = crystal_location_snapshot();
        }
        cJSON_Delete(json);
        // Failed resolution uses the snapshot's labelled fallback for this
        // request only; it never turns an unsuccessful lookup into saved data.
        if (crystal_location_snapshot().revision != location_.revision) return true;
        if (forecast()) return false;
        return true;
    }
    const auto *current = cJSON_GetObjectItemCaseSensitive(json, "current");
    double temp, humidity, code, wind;
    if (number(current, "temperature_2m", temp) && number(current, "relative_humidity_2m", humidity) &&
        number(current, "weather_code", code) && number(current, "wind_speed_10m", wind) &&
        temp >= -3276.8 && temp <= 3276.7 && humidity >= 0 && humidity <= 100 &&
        code >= 0 && code <= 255 && wind >= 0 && wind <= 6553.5) {
        reading.temperature_c10 = static_cast<int16_t>(temp * 10);
        reading.humidity = static_cast<uint8_t>(humidity);
        reading.weather_code = static_cast<uint8_t>(code);
        reading.wind_kmh10 = static_cast<uint16_t>(wind * 10);
        reading.fetched_at = static_cast<int32_t>(time(nullptr));
        strlcpy(reading.city, location_.city, sizeof(reading.city));
        reading.success = true;
    }
    cJSON_Delete(json); return true;
}
