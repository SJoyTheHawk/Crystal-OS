/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "crystal_core.hpp"
#include "crystal_hal.hpp"
#include <cmath>
#include <cstring>
#include <mutex>

namespace {
std::mutex location_lock;
bool loaded = false;
CrystalLocationSnapshot location{22.3193, 114.1694, "Hong Kong (fallback)", true, false, 1};
bool valid(double lat, double lon) {
    return std::isfinite(lat) && std::isfinite(lon) && lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
}
template<class T> bool read(const char *key, T &value) {
    size_t n = sizeof(value);
    return hal().storage && hal().storage->get(key, &value, &n) && n == sizeof(value);
}
void load() {
    if (loaded) return;
    loaded = true;
    uint8_t automatic = 1;
    read("loc.auto", automatic);
    location.automatic = automatic != 0;
    double lat, lon;
    if (read("weather.lat", lat) && read("weather.lon", lon) && valid(lat, lon)) {
        location.latitude = lat; location.longitude = lon; location.resolved = true;
        size_t n = sizeof(location.city);
        if (hal().storage) hal().storage->get("weather.city", location.city, &n);
        location.city[sizeof(location.city) - 1] = 0;
    }
}
void save() {
    if (!hal().storage) return;
    hal().storage->set("weather.lat", &location.latitude, sizeof(location.latitude));
    hal().storage->set("weather.lon", &location.longitude, sizeof(location.longitude));
    hal().storage->set("weather.city", location.city, strlen(location.city) + 1);
}
}
CrystalLocationSnapshot crystal_location_snapshot() {
    std::lock_guard<std::mutex> guard(location_lock); load(); return location;
}
bool crystal_location_get(double *lat, double *lon, char *city, size_t size) {
    if (!lat || !lon) return false;
    const auto value = crystal_location_snapshot();
    *lat = value.latitude; *lon = value.longitude;
    if (city && size) strlcpy(city, value.city, size);
    return true;
}
bool crystal_weather_location_automatic() { return crystal_location_snapshot().automatic; }
bool crystal_weather_set_location(double lat, double lon, const char *city) {
    if (!valid(lat, lon) || !city || !city[0]) return false;
    std::lock_guard<std::mutex> guard(location_lock); load();
    location.latitude = lat; location.longitude = lon;
    strlcpy(location.city, city, sizeof(location.city));
    location.automatic = false; location.resolved = true; ++location.revision;
    const uint8_t automatic = 0;
    if (hal().storage) hal().storage->set("loc.auto", &automatic, sizeof(automatic));
    save(); return true;
}
void crystal_weather_set_automatic(bool automatic) {
    std::lock_guard<std::mutex> guard(location_lock); load();
    if (automatic == location.automatic) return;
    location.automatic = automatic; ++location.revision;
    const uint8_t value = automatic;
    if (hal().storage) hal().storage->set("loc.auto", &value, sizeof(value));
    if (automatic) {
        location.resolved = false;
        location.latitude = 22.3193; location.longitude = 114.1694;
        strlcpy(location.city, "Hong Kong (fallback)", sizeof(location.city));
        if (hal().storage) for (const char *key : {"weather.lat", "weather.lon", "weather.city"}) hal().storage->erase(key);
    } else save();
}
bool crystal_location_accept(uint32_t revision, double lat, double lon, const char *city) {
    if (!valid(lat, lon) || !city) return false;
    std::lock_guard<std::mutex> guard(location_lock); load();
    if (!location.automatic || location.revision != revision) return false;
    location.latitude = lat; location.longitude = lon; location.resolved = true;
    strlcpy(location.city, city, sizeof(location.city));
    ++location.revision; save(); return true;
}
