#pragma once

#include "crystal_app.hpp"
#include "crystal_core.hpp"
#include "lvgl.h"
#include "weather_glyph.h"

LV_IMG_DECLARE(weather_icon);
extern "C" void weather_icon_prepare(void);

class WeatherApp final : public CrystalApp {
public:
    WeatherApp();
    bool onCreate() override;
    bool onPause() override;
    bool onResume() override;
    bool onDestroy() override;

    // Called on the LVGL task when the service task finishes a fetch.
    void update(const CrystalWeatherReading &reading);
    // Repaints from cache. Safe to call with no cache and while a fetch is out.
    void refresh();
    // Manual refresh, from the header button. Ignores the staleness threshold
    // and the service's backoff; the user asking is reason enough. Public
    // because the LVGL event callback is a free function.
    void requestManual();

private:
    struct Reading {
        int16_t temperature_c10 = 0;
        uint8_t humidity = 0;
        uint8_t weather_code = 0;
        uint16_t wind_kmh10 = 0;
        int32_t fetched_at = 0;
        char city[24] = {};
        bool valid = false;
    };

    Reading readCache() const;
    void request();
    void applyReading(const Reading &reading);
    void showEmpty(const char *headline, const char *detail, const char *city);
    // Dims or restores the humidity and wind tiles, captions included, so they
    // carry the same confidence as the hero rather than contradicting it.
    void setTileState(bool dimmed);
    void setStatus(const Reading &reading, bool unconfirmed, bool expired);
    // False when the reading's age cannot be worked out: either it was stamped
    // before the clock was set, or the clock is not set now. Age drives both the
    // status line and the auto-refresh, so neither may assume it is knowable.
    bool hasKnownAge(const Reading &reading) const;
    // Age in seconds, or -1 when unknowable. Callers must handle -1 rather than
    // letting a negative subtraction clamp to zero and read as "just now".
    int32_t readingAge(const Reading &reading) const;
    // True when the reading is missing, older than kStaleSeconds, or of unknown
    // age. Unknown counts as due: a reading we cannot date is one we must refetch.
    bool needsRefresh(const Reading &reading) const;

    lv_obj_t *root_ = nullptr;
    lv_obj_t *glyph_ = nullptr;
    lv_obj_t *temperature_ = nullptr;
    lv_obj_t *degree_ = nullptr;
    lv_obj_t *condition_ = nullptr;
    lv_obj_t *humidity_ = nullptr;
    lv_obj_t *wind_ = nullptr;
    // Held only so the captions can be dimmed with their values.
    lv_obj_t *humidity_caption_ = nullptr;
    lv_obj_t *wind_caption_ = nullptr;
    lv_obj_t *location_ = nullptr;
    lv_obj_t *status_ = nullptr;
    lv_obj_t *refresh_button_ = nullptr;
    lv_timer_t *timer_ = nullptr;

    // Epoch second the outstanding request was made, or 0 when none is out.
    // Doubles as the pending flag: the service task posts nothing at all when
    // WiFi is down, so a request with no reply has to time out on its own.
    int32_t requested_at_ = 0;
    bool last_fetch_failed_ = false;
    // Clock validity as of the last tick. SNTP can land at any moment, and the
    // step it applies retroactively makes a cached reading datable; this is how
    // the tick notices the transition and re-runs the staleness check once.
    bool clock_was_valid_ = false;
};
