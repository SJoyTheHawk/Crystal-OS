/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

#include <stddef.h>
#include <stdint.h>

class CrystalApp;
class ESP_Brookesia_Phone;

struct CrystalAppEntry {
    const char *id;
    CrystalApp *(*factory)();
    bool default_enabled;
    uint16_t default_slot;
};

bool crystal_registry_install(ESP_Brookesia_Phone *phone,
                              const CrystalAppEntry *entries, size_t count);

// Full validated firmware catalog, including disabled and failed-install apps,
// in table order. Entry pointers remain valid until the next install() call;
// referenced IDs must have static lifetime. Out-of-range returns nullptr.
size_t crystal_registry_catalog_count();
const CrystalAppEntry *crystal_registry_catalog_entry(size_t index);

// Successfully installed, enabled apps from the last install(), in launcher
// slot order (table order breaks ties). This is not the full firmware catalog.
// Persisted enable/slot changes do not alter this list until the next boot.
size_t crystal_registry_installed_count();
// Out-of-range indices return nullptr.
CrystalApp *crystal_registry_installed_app(size_t index);
const char *crystal_registry_installed_id(size_t index);

// Missing/unreadable values and invalid IDs return the caller's default.
bool crystal_registry_enabled(const char *id, bool default_value);
uint16_t crystal_registry_slot(const char *id, uint16_t default_value);
// Persist settings for the next boot; false means the write failed or ID is
// null/empty. IDs need not be in the currently installed app list.
bool crystal_registry_set_enabled(const char *id, bool enabled);
bool crystal_registry_set_slot(const char *id, uint16_t slot);
