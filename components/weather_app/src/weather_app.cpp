#include "weather_app.hpp"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "weather_codes.h"

namespace {
constexpr const char *TAG = "weather_app";

// Cache keys. CrystalState allows seven characters after the per-app namespace
// prefix, so every one of these is deliberately short. A longer key is rejected
// by make_key() and the write silently does nothing.
constexpr const char *kKeyTemp = "tc10";
constexpr const char *kKeyHumidity = "humid";
constexpr const char *kKeyCode = "wmo";
constexpr const char *kKeyWind = "wind";
constexpr const char *kKeyFetched = "fetched";
constexpr const char *kKeyCity = "city";

// A reading older than this is refreshed on entry. Anything younger is shown as
// it stands: a fifteen-minute-old temperature is information, not a blank page.
constexpr int32_t kStaleSeconds = 15 * 60;
// Past this the reading stops being presented as current. Weather has a diurnal
// cycle, so a morning reading shown at night is not slightly stale -- it is a
// different day's weather wearing a 48 px font. Six hours is the point where the
// number is still worth keeping but must not look authoritative.
constexpr int32_t kExpirySeconds = 6 * 60 * 60;
// A request with no answer by then is treated as failed. The service task only
// posts a result when WiFi is up, so without this the UI waits forever.
constexpr int32_t kRequestTimeoutSeconds = 25;
// 2020-01-01. The board's PCF85063 has no backup cell, so a cold boot starts the
// clock near the epoch and stays there until SNTP lands. Anything below this is
// not a time, it is an unset counter -- the same floor crystal_time_init() uses
// to reject an RTC read. Two things fall under it: the clock right now, and the
// fetched_at of a reading that was stamped while the clock was in that state.
constexpr int32_t kMinPlausibleEpoch = 1577836800;
// Drives the age line and the request timeout, so neither goes stale on screen.
constexpr uint32_t kTickMs = 5000;

// The tile palette. Dimming a value without dimming its caption would leave the
// caption brighter than the number it labels, so both steps down together and the
// caption stays the quieter of the pair in either state.
constexpr uint32_t kValue = 0xE6EDF5;
constexpr uint32_t kValueDim = 0x6B7C8D;   // matches the hero temperature
constexpr uint32_t kCaption = 0x7C90A4;
constexpr uint32_t kCaptionDim = 0x5E6E7E;  // matches the hero condition

constexpr lv_coord_t kPad = 16;
constexpr lv_coord_t kGap = 12;

bool isLive(const CrystalApp *app)
{
    switch (app->lifecycle_state()) {
    case CrystalApp::LifecycleState::Installed:
    case CrystalApp::LifecycleState::Created:
    case CrystalApp::LifecycleState::Started:
    case CrystalApp::LifecycleState::Resumed:
        return true;
    default:
        return false;
    }
}

void tick(lv_timer_t *timer)
{
    if (timer != nullptr && timer->user_data != nullptr) {
        static_cast<WeatherApp *>(timer->user_data)->refresh();
    }
}

void onRefreshClicked(lv_event_t *event)
{
    auto *app = static_cast<WeatherApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->requestManual();
}

template <typename T>
bool readValue(const CrystalState &state, const char *key, T *out)
{
    size_t size = sizeof(*out);
    return state.get(key, out, &size) && size == sizeof(*out);
}

int32_t nowEpoch()
{
    return static_cast<int32_t>(time(nullptr));
}

bool clockIsSet()
{
    return nowEpoch() >= kMinPlausibleEpoch;
}

// Cards and tiles share one surface treatment so the page reads as one design.
lv_obj_t *makeCard(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1C2733), 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_pad_all(card, kPad, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

// "Just now" beats "Updated 0 min ago", and hours beat a three-digit minute
// count. This is the only place an age is turned into words.
void formatAge(int32_t seconds, char *out, size_t out_size)
{
    if (seconds < 90) {
        snprintf(out, out_size, "Updated just now");
    }
    else if (seconds < 3600) {
        snprintf(out, out_size, "Updated %d min ago", static_cast<int>(seconds / 60));
    }
    else if (seconds < 2 * 3600) {
        snprintf(out, out_size, "Updated 1 hour ago");
    }
    else {
        snprintf(out, out_size, "Updated %d hours ago", static_cast<int>(seconds / 3600));
    }
}
}  // namespace

WeatherApp::WeatherApp() : CrystalApp("Weather", &weather_icon)
{
    // The icon bitmap is computed, not stored, so it has to be filled before
    // the launcher first draws it.
    weather_icon_prepare();
}

bool WeatherApp::onCreate()
{
    const lv_area_t area = getVisualArea();
    const lv_coord_t width = area.x2 - area.x1 + 1;
    const lv_coord_t height = area.y2 - area.y1 + 1;

    // Position is (0, 0), not (area.x1, area.y1). The active app screen is
    // already app-area sized and uses local coordinates, so offsetting by the
    // status bar height pushed a full-height root below the bottom edge -- which
    // is what made the page scroll and appear to have dropped down.
    root_ = lv_obj_create(lv_scr_act());
    lv_obj_set_size(root_, width, height);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x11181F), 0);
    lv_obj_set_style_pad_all(root_, kPad, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    const lv_coord_t content = width - 2 * kPad;

    // Header: the resolved location. With automatic detection this is the only
    // way a user can tell the guess was wrong, so it is never hidden.
    // Manual refresh. Placed first so the location label can be sized to the
    // space that is left, rather than being overlapped by the button.
    // kInset keeps the whole button out of the 24 px right-edge gesture band
    // (main.cpp: gesture.threshold.horizontal_edge). At kPad alone the button's
    // right columns sat inside the band, so touches there were claimed as an
    // app-switch swipe and never became a click. ext_click_area gives the touch
    // target back the width the inset costs, without moving the visible circle.
    constexpr lv_coord_t kButton = 40;
    constexpr lv_coord_t kInset = 14;
    refresh_button_ = lv_btn_create(root_);
    lv_obj_set_size(refresh_button_, kButton, kButton);
    lv_obj_align(refresh_button_, LV_ALIGN_TOP_RIGHT, -kInset, -6);
    lv_obj_set_ext_click_area(refresh_button_, 6);
    // Low profile: no fill and no border at rest, so the header reads as text
    // with an affordance in it rather than as a toolbar. The pressed state is
    // the only time it draws a background -- which is also the touch feedback.
    lv_obj_set_style_bg_opa(refresh_button_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(refresh_button_, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(refresh_button_, lv_color_hex(0xC9D6E2), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(refresh_button_, 0, 0);
    lv_obj_set_style_border_width(refresh_button_, 0, 0);
    lv_obj_set_style_radius(refresh_button_, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(refresh_button_, onRefreshClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *refresh_glyph = lv_label_create(refresh_button_);
    lv_label_set_text(refresh_glyph, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(refresh_glyph, lv_color_hex(0x8FA3B7), 0);
    lv_obj_center(refresh_glyph);

    location_ = makeLabel(root_, &lv_font_montserrat_20, 0xE6EDF5);
    lv_label_set_long_mode(location_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(location_, content - kButton - kInset - kGap);
    lv_obj_align(location_, LV_ALIGN_TOP_LEFT, 0, 0);

    // Hero card: temperature on the left, glyph on the right, condition beneath.
    lv_obj_t *hero = makeCard(root_);
    lv_obj_set_size(hero, content, 168);
    lv_obj_align(hero, LV_ALIGN_TOP_LEFT, 0, 34);

    temperature_ = makeLabel(hero, &lv_font_montserrat_48, 0xFFFFFF);
    lv_obj_align(temperature_, LV_ALIGN_TOP_LEFT, 0, 4);

    // The degree mark is its own label so it can sit at the temperature's cap
    // height instead of on its baseline, where a 48 px '°' looks misplaced.
    degree_ = makeLabel(hero, &lv_font_montserrat_20, 0x9FB3C8);
    lv_label_set_text(degree_, "C");

    condition_ = makeLabel(hero, &lv_font_montserrat_28, 0xC9D6E2);
    lv_label_set_long_mode(condition_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(condition_, content - 2 * kPad - 96);
    lv_obj_align(condition_, LV_ALIGN_BOTTOM_LEFT, 0, -4);

    glyph_ = weather_glyph_create(hero, 96);
    lv_obj_align(glyph_, LV_ALIGN_TOP_RIGHT, 0, 0);

    // Two tiles, two facts. Nothing is invented to fill the row.
    const lv_coord_t tile_w = (content - kGap) / 2;
    lv_obj_t *humidity_tile = makeCard(root_);
    lv_obj_set_size(humidity_tile, tile_w, 88);
    lv_obj_align(humidity_tile, LV_ALIGN_TOP_LEFT, 0, 34 + 168 + kGap);
    humidity_caption_ = makeLabel(humidity_tile, &lv_font_montserrat_14, kCaption);
    lv_label_set_text(humidity_caption_, "HUMIDITY");
    lv_obj_align(humidity_caption_, LV_ALIGN_TOP_LEFT, 0, 0);
    humidity_ = makeLabel(humidity_tile, &lv_font_montserrat_28, kValue);
    lv_obj_align(humidity_, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *wind_tile = makeCard(root_);
    lv_obj_set_size(wind_tile, tile_w, 88);
    lv_obj_align(wind_tile, LV_ALIGN_TOP_RIGHT, 0, 34 + 168 + kGap);
    wind_caption_ = makeLabel(wind_tile, &lv_font_montserrat_14, kCaption);
    lv_label_set_text(wind_caption_, "WIND");
    lv_obj_align(wind_caption_, LV_ALIGN_TOP_LEFT, 0, 0);
    wind_ = makeLabel(wind_tile, &lv_font_montserrat_28, kValue);
    lv_obj_align(wind_, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    status_ = makeLabel(root_, &lv_font_montserrat_14, 0x7C90A4);
    lv_obj_align(status_, LV_ALIGN_TOP_LEFT, 0, 34 + 168 + kGap + 88 + kGap);

    // Paint the cache before asking for anything. A stored reading appears on
    // the first frame; the fetch then quietly replaces it.
    const Reading cached = readCache();
    applyReading(cached);
    if (needsRefresh(cached)) {
        request();
    }

    clock_was_valid_ = clockIsSet();
    timer_ = lv_timer_create(tick, kTickMs, this);
    return timer_ != nullptr;
}

bool WeatherApp::onPause()
{
    if (timer_ != nullptr) {
        lv_timer_del(timer_);
        timer_ = nullptr;
    }
    return true;
}

bool WeatherApp::onResume()
{
    const Reading cached = readCache();
    applyReading(cached);
    if (needsRefresh(cached)) {
        request();
    }
    clock_was_valid_ = clockIsSet();
    timer_ = lv_timer_create(tick, kTickMs, this);
    return timer_ != nullptr;
}

bool WeatherApp::onDestroy()
{
    if (timer_ != nullptr) {
        lv_timer_del(timer_);
        timer_ = nullptr;
    }
    // The shell deletes the screen's children; these pointers must not survive
    // it, because a late weather event would otherwise write to freed objects.
    root_ = glyph_ = temperature_ = degree_ = condition_ = nullptr;
    humidity_ = wind_ = location_ = status_ = refresh_button_ = nullptr;
    humidity_caption_ = wind_caption_ = nullptr;
    requested_at_ = 0;
    clock_was_valid_ = false;
    return true;
}

WeatherApp::Reading WeatherApp::readCache() const
{
    Reading reading;
    // Temperature is the gate: without it there is nothing worth drawing.
    reading.valid = readValue(state(), kKeyTemp, &reading.temperature_c10);
    (void)readValue(state(), kKeyHumidity, &reading.humidity);
    (void)readValue(state(), kKeyCode, &reading.weather_code);
    (void)readValue(state(), kKeyWind, &reading.wind_kmh10);
    (void)readValue(state(), kKeyFetched, &reading.fetched_at);

    size_t city_size = sizeof(reading.city) - 1;
    if (state().get(kKeyCity, reading.city, &city_size) && city_size < sizeof(reading.city)) {
        reading.city[city_size] = '\0';
    }
    return reading;
}

bool WeatherApp::hasKnownAge(const Reading &reading) const
{
    // Three ways the age is unknowable, and all three used to produce the same
    // wrong answer -- "Updated just now" -- because the negative or absurd
    // difference was clamped to zero on its way to formatAge().
    if (reading.fetched_at < kMinPlausibleEpoch) return false;  // stamped unset
    if (!clockIsSet()) return false;                            // clock unset now
    return nowEpoch() >= reading.fetched_at;                    // clock behind stamp
}

int32_t WeatherApp::readingAge(const Reading &reading) const
{
    if (!hasKnownAge(reading)) return -1;
    return nowEpoch() - reading.fetched_at;
}

bool WeatherApp::needsRefresh(const Reading &reading) const
{
    if (!reading.valid) return true;
    const int32_t age = readingAge(reading);
    // An undatable reading is due. It may be four minutes old or four days; the
    // only way to find out is to fetch, and until then it cannot be presented as
    // current. Previously this comparison went negative and silently passed.
    return age < 0 || age > kStaleSeconds;
}

void WeatherApp::request()
{
    requested_at_ = nowEpoch();
    last_fetch_failed_ = false;
    crystal_weather_request();
}

void WeatherApp::requestManual()
{
    // Ignore a double tap while a fetch is already out: a second request would
    // reset the timeout clock and make the first one look like it never expired.
    if (requested_at_ != 0) {
        return;
    }
    request();
    // Repaint now so the status line acknowledges the tap in the same frame.
    // Without this the label does not change until the 5 s tick, and the button
    // feels dead.
    applyReading(readCache());
}

void WeatherApp::refresh()
{
    if (!isLive(this) || status_ == nullptr) {
        return;
    }
    // Nothing arrived in time. Say so once, and stop claiming to be busy.
    if (requested_at_ != 0 && nowEpoch() - requested_at_ > kRequestTimeoutSeconds) {
        requested_at_ = 0;
        last_fetch_failed_ = true;
        ESP_LOGW(TAG, "weather request timed out");
    }

    const Reading reading = readCache();
    // SNTP just landed. Until this moment nothing on screen could be dated, so
    // re-run the check the entry gate ran: either the reading turns out to be
    // fresh and the status line stops hedging, or it is stale and we refetch.
    // Only on the transition -- a permanently clockless device must not loop.
    const bool clock_valid = clockIsSet();
    if (clock_valid && !clock_was_valid_) {
        clock_was_valid_ = true;
        ESP_LOGI(TAG, "clock synchronized; re-checking weather freshness");
        if (requested_at_ == 0 && needsRefresh(reading)) {
            request();
        }
    }
    else {
        clock_was_valid_ = clock_valid;
    }

    applyReading(reading);
}

void WeatherApp::applyReading(const Reading &reading)
{
    if (temperature_ == nullptr || condition_ == nullptr || glyph_ == nullptr) {
        return;
    }
    if (!reading.valid) {
        if (requested_at_ != 0) {
            showEmpty("--", "Checking weather", reading.city);
        }
        else if (last_fetch_failed_) {
            showEmpty("--", "Couldn't reach the weather service", reading.city);
        }
        else {
            showEmpty("--", "Connect to WiFi to refresh", reading.city);
        }
        return;
    }

    lv_obj_clear_flag(degree_, LV_OBJ_FLAG_HIDDEN);

    // An expired reading is dimmed rather than deleted. The information is still
    // the best we have; what has to go is its air of authority, because at full
    // contrast a 48 px numeral reads as a live measurement no matter what the
    // 14 px status line says underneath it.
    const int32_t age = readingAge(reading);
    // A reading whose age is unknown gets the same treatment as an expired one.
    // It may well be current, but "may well be" is not what a full-contrast 48 px
    // numeral communicates, and the honest answer here is that we cannot say.
    const bool unconfirmed = age < 0;
    const bool expired = age > kExpirySeconds;
    const bool dimmed = unconfirmed || expired;
    lv_obj_set_style_text_color(temperature_, lv_color_hex(dimmed ? 0x6B7C8D : 0xFFFFFF), 0);
    lv_obj_set_style_text_color(condition_, lv_color_hex(dimmed ? 0x5E6E7E : 0xC9D6E2), 0);
    lv_obj_set_style_opa(glyph_, dimmed ? LV_OPA_40 : LV_OPA_COVER, 0);
    // Humidity and wind came from the same fetch as the temperature, so they carry
    // exactly the same doubt and must not stay bright while the hero fades.
    setTileState(dimmed);

    // Split the sign off before dividing, or -0.4 C prints as 0.4: the integer
    // part of -4/10 is 0 and the minus goes missing.
    const int magnitude = reading.temperature_c10 < 0 ? -static_cast<int>(reading.temperature_c10)
                                                      : static_cast<int>(reading.temperature_c10);
    char text[64];
    snprintf(text, sizeof(text), "%s%d.%d", reading.temperature_c10 < 0 ? "-" : "",
             magnitude / 10, magnitude % 10);
    lv_label_set_text(temperature_, text);
    lv_obj_align_to(degree_, temperature_, LV_ALIGN_OUT_RIGHT_TOP, 6, 10);

    lv_label_set_text(condition_, weather_group_for_code(reading.weather_code).label);
    weather_glyph_set_code(glyph_, reading.weather_code);

    snprintf(text, sizeof(text), "%u%%", reading.humidity);
    lv_label_set_text(humidity_, text);
    snprintf(text, sizeof(text), "%u.%u km/h", reading.wind_kmh10 / 10, reading.wind_kmh10 % 10);
    lv_label_set_text(wind_, text);

    lv_label_set_text(location_, reading.city[0] != '\0' ? reading.city : "Location not set");
    setStatus(reading, unconfirmed, expired);
}

void WeatherApp::setTileState(bool dimmed)
{
    if (humidity_ == nullptr || wind_ == nullptr) return;
    const uint32_t value = dimmed ? kValueDim : kValue;
    const uint32_t caption = dimmed ? kCaptionDim : kCaption;
    lv_obj_set_style_text_color(humidity_, lv_color_hex(value), 0);
    lv_obj_set_style_text_color(wind_, lv_color_hex(value), 0);
    if (humidity_caption_ != nullptr) {
        lv_obj_set_style_text_color(humidity_caption_, lv_color_hex(caption), 0);
    }
    if (wind_caption_ != nullptr) {
        lv_obj_set_style_text_color(wind_caption_, lv_color_hex(caption), 0);
    }
}

void WeatherApp::showEmpty(const char *headline, const char *detail, const char *city)
{
    lv_label_set_text(temperature_, headline);
    lv_obj_add_flag(degree_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(condition_, detail);
    weather_glyph_set_code(glyph_, 0);
    lv_label_set_text(humidity_, "--");
    lv_label_set_text(wind_, "--");
    // A placeholder is not a value. Leaving the tiles bright here would also let a
    // dimmed reading hand its styling over to "--" at full contrast.
    setTileState(true);
    lv_label_set_text(location_, city != nullptr && city[0] != '\0' ? city : "Locating");
    lv_label_set_text(status_, requested_at_ != 0 ? "Fetching" : "");
}

void WeatherApp::setStatus(const Reading &reading, bool unconfirmed, bool expired)
{
    // With data on screen the status line carries the qualifier, so a refresh
    // in flight or a failed one never blanks a reading the user can still use.
    char text[80];
    const int32_t age = readingAge(reading);
    if (reading.fetched_at <= 0 || age < 0) {
        // No age to report. Saying nothing is right while a fetch is in flight or
        // has failed, since those branches supply their own words below.
        text[0] = '\0';
    }
    else {
        formatAge(age, text, sizeof(text));
    }

    char line[160];
    if (requested_at_ != 0) {
        snprintf(line, sizeof(line), "Refreshing%s%s", text[0] != '\0' ? " - " : "", text);
    }
    else if (last_fetch_failed_) {
        snprintf(line, sizeof(line), "Couldn't refresh%s%s", text[0] != '\0' ? " - " : "", text);
    }
    else if (unconfirmed) {
        // The one case with no age to quote. Naming the clock rather than the
        // reading is deliberate: the temperature may be fine, and the thing the
        // user can act on is that the device does not know what time it is.
        snprintf(line, sizeof(line), "Age unknown - waiting for clock sync");
    }
    else if (expired) {
        // Say the quiet part out loud. "Updated 9 hours ago" states a fact and
        // leaves the reader to draw the conclusion; this draws it for them.
        snprintf(line, sizeof(line), "%s - may no longer be accurate", text);
    }
    else {
        snprintf(line, sizeof(line), "%s", text);
    }
    lv_obj_set_style_text_color(status_,
                                lv_color_hex(unconfirmed || expired ? 0xC98A3C : 0x7C90A4), 0);
    lv_label_set_text(status_, line);
}

void WeatherApp::update(const CrystalWeatherReading &incoming)
{
    requested_at_ = 0;
    if (!incoming.success) {
        last_fetch_failed_ = true;
        // Keep whatever is cached on screen and let the status line explain.
        if (isLive(this)) {
            applyReading(readCache());
        }
        return;
    }
    last_fetch_failed_ = false;

    bool ok = state().set(kKeyTemp, &incoming.temperature_c10, sizeof(incoming.temperature_c10));
    ok = state().set(kKeyHumidity, &incoming.humidity, sizeof(incoming.humidity)) && ok;
    ok = state().set(kKeyCode, &incoming.weather_code, sizeof(incoming.weather_code)) && ok;
    ok = state().set(kKeyWind, &incoming.wind_kmh10, sizeof(incoming.wind_kmh10)) && ok;
    ok = state().set(kKeyFetched, &incoming.fetched_at, sizeof(incoming.fetched_at)) && ok;
    ok = state().set(kKeyCity, incoming.city, strnlen(incoming.city, sizeof(incoming.city)) + 1) && ok;
    if (!ok) {
        ESP_LOGW(TAG, "weather cache write failed");
    }

    if (isLive(this)) {
        applyReading(readCache());
    }
}
