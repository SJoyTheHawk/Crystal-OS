/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once
#include "crystal_core.hpp"

// LVGL-owned session. HTTP callbacks retain only a detached completion object,
// never an app/UI pointer. All parsing, settings commits and delivery run in poll.
class WeatherRequest final {
public:
    WeatherRequest() = default;
    ~WeatherRequest();
    WeatherRequest(const WeatherRequest &) = delete;
    WeatherRequest &operator=(const WeatherRequest &) = delete;
    bool start();
    void cancel();
    bool poll(CrystalWeatherReading &reading);
    bool pending() const { return pending_ != nullptr; }
    CrystalLocationSnapshot location() const { return location_; }
private:
    struct Pending;
    bool submit(const char *url);
    bool forecast();
    Pending *pending_ = nullptr;
    uint32_t request_id_ = 0;
    int64_t deadline_us_ = 0;
    bool locating_ = false;
    CrystalLocationSnapshot location_{};
};
