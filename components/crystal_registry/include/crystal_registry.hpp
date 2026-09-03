/* SPDX-License-Identifier: MIT */
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

size_t crystal_registry_installed_count();
CrystalApp *crystal_registry_installed_app(size_t index);
const char *crystal_registry_installed_id(size_t index);

bool crystal_registry_enabled(const char *id, bool default_value);
uint16_t crystal_registry_slot(const char *id, uint16_t default_value);
bool crystal_registry_set_enabled(const char *id, bool enabled);
bool crystal_registry_set_slot(const char *id, uint16_t slot);
