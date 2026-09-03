/* SPDX-License-Identifier: MIT */

#include "crystal_app.hpp"

#include <stdio.h>
#include <string.h>

#include "crystal_hal.hpp"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "crystal_app";

namespace {
constexpr size_t kMaxStateBytes = 2048;
constexpr size_t kMaxKeyBytes = 7;
}

CrystalState::CrystalState(const char *app_name)
    : prefix_("a")
{
    uint32_t hash = 2166136261u;
    if (app_name != nullptr) {
        for (const unsigned char *p = reinterpret_cast<const unsigned char *>(app_name); *p != '\0'; ++p) {
            hash ^= *p;
            hash *= 16777619u;
        }
    }
    char suffix[8];
    snprintf(suffix, sizeof(suffix), "%06lx", static_cast<unsigned long>(hash & 0xFFFFFFu));
    prefix_ += suffix;
    prefix_ += ".";
}

bool CrystalState::make_key(const char *key, std::string *out) const
{
    if (key == nullptr || out == nullptr || key[0] == '\0' || strlen(key) > kMaxKeyBytes) {
        return false;
    }
    *out = prefix_ + key;
    return out->size() < 64;
}

bool CrystalState::get(const char *key, void *value, size_t *length) const
{
    std::string full_key;
    if (value == nullptr || length == nullptr || *length > kMaxStateBytes || !make_key(key, &full_key)) {
        return false;
    }
    return hal().storage != nullptr && hal().storage->get(full_key.c_str(), value, length);
}

bool CrystalState::set(const char *key, const void *value, size_t length)
{
    std::string full_key;
    if (value == nullptr || length > kMaxStateBytes || !make_key(key, &full_key)) {
        return false;
    }
    return hal().storage != nullptr && hal().storage->set(full_key.c_str(), value, length);
}

bool CrystalState::erase(const char *key)
{
    std::string full_key;
    if (!make_key(key, &full_key)) {
        return false;
    }
    return hal().storage != nullptr && hal().storage->erase(full_key.c_str());
}

bool CrystalState::get_u32(const char *key, uint32_t *value) const
{
    size_t length = sizeof(*value);
    return value != nullptr && get(key, value, &length) && length == sizeof(*value);
}

bool CrystalState::set_u32(const char *key, uint32_t value)
{
    return set(key, &value, sizeof(value));
}

CrystalApp::CrystalApp(const char *name, const void *launcher_icon)
    : ESP_Brookesia_PhoneApp(name, launcher_icon, true), state_(name)
{
}

bool CrystalApp::run()
{
    ESP_LOGI(TAG, "%s lifecycle: onCreate", getName());
    return onCreate();
}

bool CrystalApp::pause()
{
    ESP_LOGI(TAG, "%s lifecycle: onPause", getName());
    return onPause();
}

bool CrystalApp::resume()
{
    ESP_LOGI(TAG, "%s lifecycle: onResume", getName());
    const uint32_t start = lv_tick_get();
    const bool ok = onResume();
    const uint32_t elapsed = lv_tick_elaps(start);
    if (elapsed > 80) {
        ESP_LOGW(TAG, "%s onResume took %lu ms (budget 80 ms)",
                 getName(), static_cast<unsigned long>(elapsed));
    }
    return ok;
}
bool CrystalApp::close()
{
    ESP_LOGI(TAG, "%s lifecycle: onDestroy", getName());
    return onDestroy();
}

bool CrystalApp::back()
{
    ESP_LOGI(TAG, "%s lifecycle: onBack", getName());
    return onBack();
}
