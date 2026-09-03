/* SPDX-License-Identifier: MIT */

#include "crystal_registry.hpp"

#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "crystal_app.hpp"
#include "crystal_hal.hpp"
#include "esp_brookesia.hpp"
#include "esp_log.h"

static const char *TAG = "crystal_registry";

namespace {
struct RegistryRow {
    const CrystalAppEntry *entry;
    uint16_t slot;
    size_t table_index;
};

struct InstalledRow {
    const char *id;
    CrystalApp *app;
};

std::vector<InstalledRow> s_installed;

bool make_key(const char *id, char suffix, char *out, size_t out_size)
{
    if (id == nullptr || id[0] == '\0' || out == nullptr || out_size < 12) {
        return false;
    }
    uint32_t hash = 2166136261u;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(id); *p != '\0'; ++p) {
        hash ^= *p;
        hash *= 16777619u;
    }
    return snprintf(out, out_size, "r%08lx.%c",
                    static_cast<unsigned long>(hash), suffix) > 0;
}

template <typename T>
bool read_value(const char *id, char suffix, T *value)
{
    char key[12];
    size_t length = sizeof(*value);
    return value != nullptr && make_key(id, suffix, key, sizeof(key)) &&
           hal().storage != nullptr && hal().storage->get(key, value, &length) &&
           length == sizeof(*value);
}

template <typename T>
bool write_value(const char *id, char suffix, const T &value)
{
    char key[12];
    return make_key(id, suffix, key, sizeof(key)) && hal().storage != nullptr &&
           hal().storage->set(key, &value, sizeof(value));
}
}

size_t crystal_registry_installed_count()
{
    return s_installed.size();
}

CrystalApp *crystal_registry_installed_app(size_t index)
{
    return index < s_installed.size() ? s_installed[index].app : nullptr;
}

const char *crystal_registry_installed_id(size_t index)
{
    return index < s_installed.size() ? s_installed[index].id : nullptr;
}

bool crystal_registry_enabled(const char *id, bool default_value)
{
    uint8_t stored = 0;
    return read_value(id, 'e', &stored) ? stored != 0 : default_value;
}

uint16_t crystal_registry_slot(const char *id, uint16_t default_value)
{
    uint16_t stored = 0;
    return read_value(id, 's', &stored) ? stored : default_value;
}

bool crystal_registry_set_enabled(const char *id, bool enabled)
{
    const uint8_t stored = enabled ? 1 : 0;
    const bool ok = write_value(id, 'e', stored);
    if (ok) {
        ESP_LOGI(TAG, "%s enabled=%d (applies next boot)", id, enabled ? 1 : 0);
    } else {
        ESP_LOGE(TAG, "failed to save enabled state: %s", id != nullptr ? id : "<null>");
    }
    return ok;
}

bool crystal_registry_set_slot(const char *id, uint16_t slot)
{
    const bool ok = write_value(id, 's', slot);
    if (ok) {
        ESP_LOGI(TAG, "%s slot=%u (applies next boot)", id, static_cast<unsigned>(slot));
    } else {
        ESP_LOGE(TAG, "failed to save slot: %s", id != nullptr ? id : "<null>");
    }
    return ok;
}

bool crystal_registry_install(ESP_Brookesia_Phone *phone,
                              const CrystalAppEntry *entries, size_t count)
{
    if (phone == nullptr || (entries == nullptr && count != 0)) {
        return false;
    }

    s_installed.clear();
    std::vector<RegistryRow> enabled;
    enabled.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const CrystalAppEntry &entry = entries[i];
        if (entry.id == nullptr || entry.id[0] == '\0' || entry.factory == nullptr) {
            ESP_LOGE(TAG, "invalid app table entry at index %u", static_cast<unsigned>(i));
            return false;
        }
        if (!crystal_registry_enabled(entry.id, entry.default_enabled)) {
            ESP_LOGI(TAG, "disabled: %s", entry.id);
            continue;
        }
        enabled.push_back({&entry, crystal_registry_slot(entry.id, entry.default_slot), i});
    }

    std::stable_sort(enabled.begin(), enabled.end(), [](const RegistryRow &a, const RegistryRow &b) {
        if (a.slot != b.slot) {
            return a.slot < b.slot;
        }
        return a.table_index < b.table_index;
    });

    bool ok = true;
    for (const RegistryRow &row : enabled) {
        CrystalApp *app = row.entry->factory();
        if (app == nullptr || phone->installApp(app) < 0) {
            ESP_LOGE(TAG, "install failed: %s", row.entry->id);
            delete app;
            ok = false;
            continue;
        }
        s_installed.push_back({row.entry->id, app});
        ESP_LOGI(TAG, "installed: %s (slot %u)", row.entry->id,
                 static_cast<unsigned>(row.slot));
    }
    return ok;
}
