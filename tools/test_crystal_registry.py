#!/usr/bin/env python3
"""Verify the production registry with host storage and phone adapters.

Run: python3 tools/test_crystal_registry.py
Checks API contracts and boot ordering; real NVS and UI require device testing.
"""
import os
import pathlib
import shlex
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

APP = r'''
#pragma once
class CrystalApp {
public:
    static int live;
    CrystalApp() { ++live; }
    virtual ~CrystalApp() { --live; }
};
'''

PHONE = r'''
#pragma once
#include <vector>
#include "crystal_app.hpp"
class ESP_Brookesia_Phone {
public:
    bool fail = false;
    std::vector<CrystalApp *> apps;
    int installApp(CrystalApp *app) {
        if (fail) return -1;
        apps.push_back(app);
        return static_cast<int>(apps.size() - 1);
    }
    ~ESP_Brookesia_Phone() { for (auto *app : apps) delete app; }
};
'''

LOG = r'''
#pragma once
#include <cstdio>
#include <cstdarg>
inline void registry_log(const char *tag, const char *format, ...) {
    std::printf("%s: ", tag);
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::puts("");
}
#define ESP_LOGI registry_log
#define ESP_LOGE registry_log
'''

CHECK = r'''
#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "crystal_registry.hpp"
#include "crystal_hal.hpp"
#include "esp_brookesia.hpp"

int CrystalApp::live = 0;
static CrystalHal test_hal{};
CrystalHal &hal() { return test_hal; }

class Storage final : public IStorage {
public:
    std::map<std::string, std::vector<uint8_t>> blobs;
    bool fail_read = false, fail_write = false;
    bool get(const char *key, void *value, size_t *length) override {
        const auto found = blobs.find(key);
        if (fail_read || found == blobs.end()) return false;
        const auto &blob = found->second;
        const size_t capacity = *length;
        *length = blob.size();
        if (capacity < blob.size()) return false;
        if (!blob.empty()) std::memcpy(value, blob.data(), blob.size());
        return true;
    }
    bool set(const char *key, const void *value, size_t length) override {
        assert(std::strlen(key) <= 15); // NVS key limit, including long app IDs.
        if (fail_write) return false;
        const auto *bytes = static_cast<const uint8_t *>(value);
        blobs[key] = std::vector<uint8_t>(bytes, bytes + length);
        return true;
    }
    bool erase(const char *key) override { return blobs.erase(key) != 0; }
};

static CrystalApp *make_app() { return new CrystalApp; }
static CrystalApp *fail_factory() { return nullptr; }
static const CrystalAppEntry apps[] = {
    {"dev_tester", make_app, true, 0},
    {"clock", make_app, true, 2},
    {"weather", make_app, true, 3},
    {"calculator", make_app, true, 4},
};

static void expect_ids(std::initializer_list<const char *> expected) {
    assert(crystal_registry_installed_count() == expected.size());
    size_t index = 0;
    for (const char *id : expected) {
        assert(crystal_registry_installed_id(index) != nullptr);
        assert(std::strcmp(crystal_registry_installed_id(index), id) == 0);
        assert(crystal_registry_installed_app(index) != nullptr);
        ++index;
    }
    assert(crystal_registry_installed_id(index) == nullptr);
    assert(crystal_registry_installed_app(index) == nullptr);
    assert(crystal_registry_installed_id(SIZE_MAX) == nullptr);
    assert(crystal_registry_installed_app(SIZE_MAX) == nullptr);
}

int main() {
    Storage storage;
    test_hal.storage = &storage;
    expect_ids({});
    assert(crystal_registry_catalog_count() == 0);
    assert(crystal_registry_catalog_entry(0) == nullptr);
    for (const char *id : {static_cast<const char *>(nullptr), "", "missing"}) {
        assert(crystal_registry_enabled(id, true));
        assert(!crystal_registry_enabled(id, false));
        assert(crystal_registry_slot(id, 65535) == 65535);
        assert(crystal_registry_slot(id, 0) == 0);
    }
    assert(!crystal_registry_set_enabled(nullptr, true));
    assert(!crystal_registry_set_enabled("", true));
    assert(!crystal_registry_set_slot(nullptr, 1));
    assert(!crystal_registry_set_slot("", 1));
    assert(storage.blobs.empty());

    for (const char *id : {"clock", "org.crystal.phase13.long-app-id"}) {
        assert(crystal_registry_set_enabled(id, false));
        assert(!crystal_registry_enabled(id, true));
        assert(crystal_registry_set_enabled(id, true));
        assert(crystal_registry_enabled(id, false));
        for (uint16_t slot : {0, 42, 65535}) {
            assert(crystal_registry_set_slot(id, slot));
            assert(crystal_registry_slot(id, slot == 0 ? 999 : 0) == slot);
        }
        assert(crystal_registry_enabled(id, false));
    }
    assert(storage.blobs.size() == 4); // enabled/slot and separate IDs do not alias.
    storage.fail_write = true;
    assert(!crystal_registry_set_enabled("clock", false));
    assert(!crystal_registry_set_slot("clock", 7));
    assert(crystal_registry_enabled("clock", false));
    assert(crystal_registry_slot("clock", 0) == 65535);
    storage.fail_write = false;
    storage.fail_read = true;
    assert(!crystal_registry_enabled("clock", false));
    assert(crystal_registry_slot("clock", 123) == 123);
    storage.fail_read = false;
    test_hal.storage = nullptr;
    assert(crystal_registry_enabled("clock", true));
    assert(crystal_registry_slot("clock", 123) == 123);
    assert(!crystal_registry_set_enabled("clock", false));
    assert(!crystal_registry_set_slot("clock", 7));
    test_hal.storage = &storage;

    // Wrong-length NVS blobs must use defaults, including a successful short read.
    storage.blobs.clear();
    assert(crystal_registry_set_slot("clock", 42));
    auto &slot_blob = storage.blobs.begin()->second;
    slot_blob.resize(1);
    assert(crystal_registry_slot("clock", 999) == 999);
    slot_blob.resize(3);
    assert(crystal_registry_slot("clock", 999) == 999);
    storage.blobs.clear();
    assert(crystal_registry_set_enabled("clock", true));
    storage.blobs.begin()->second.clear();
    assert(crystal_registry_enabled("clock", true));
    storage.blobs.begin()->second.resize(2);
    assert(!crystal_registry_enabled("clock", false));

    storage.blobs.clear();
    {
        ESP_Brookesia_Phone phone;
        assert(crystal_registry_install(&phone, apps, 4));
        expect_ids({"dev_tester", "clock", "weather", "calculator"});
        for (size_t i = 0; i < 4; ++i) {
            assert(crystal_registry_installed_app(i) == phone.apps[i]);
            std::printf("[%zu] %s: enabled=%d, slot=%u\n", i,
                crystal_registry_installed_id(i),
                crystal_registry_enabled(apps[i].id, apps[i].default_enabled),
                crystal_registry_slot(apps[i].id, apps[i].default_slot));
        }
        assert(crystal_registry_set_enabled("weather", false));
        assert(crystal_registry_set_slot("calculator", 0));
        // Setters persist but cannot change an already installed list.
        expect_ids({"dev_tester", "clock", "weather", "calculator"});
    }
    assert(CrystalApp::live == 0);
    {
        ESP_Brookesia_Phone reboot;
        assert(crystal_registry_install(&reboot, apps, 4));
        expect_ids({"dev_tester", "calculator", "clock"});
        assert(crystal_registry_catalog_count() == 4);
        for (size_t i = 0; i < 4; ++i) {
            const auto *entry = crystal_registry_catalog_entry(i);
            assert(entry && std::strcmp(entry->id, apps[i].id) == 0);
            assert(entry->default_enabled == apps[i].default_enabled);
            assert(entry->default_slot == apps[i].default_slot);
        }
        assert(crystal_registry_catalog_entry(4) == nullptr);
        assert(crystal_registry_catalog_entry(SIZE_MAX) == nullptr);
        // Equal slots use table order. Disabled apps have no installed entry.
        for (const auto &app : apps) assert(crystal_registry_set_enabled(app.id, false));
    }
    {
        ESP_Brookesia_Phone reboot;
        assert(crystal_registry_install(&reboot, apps, 4));
        expect_ids({});
        assert(crystal_registry_catalog_count() == 4);
        assert(crystal_registry_install(&reboot, nullptr, 0));
        assert(crystal_registry_catalog_count() == 0);
        assert(!crystal_registry_install(nullptr, apps, 4));
        assert(!crystal_registry_install(&reboot, nullptr, 1));
        const CrystalAppEntry invalid[] = {{nullptr, make_app, true, 0}};
        assert(!crystal_registry_install(&reboot, invalid, 1));
        const CrystalAppEntry no_factory[] = {{"invalid", nullptr, true, 0}};
        assert(!crystal_registry_install(&reboot, no_factory, 1));
    }
    storage.blobs.clear();
    {
        ESP_Brookesia_Phone phone;
        phone.fail = true;
        assert(!crystal_registry_install(&phone, apps, 4));
        expect_ids({});
        assert(CrystalApp::live == 0); // Rejected instances are deleted.
        phone.fail = false;
        const CrystalAppEntry partial[] = {
            {"broken", fail_factory, true, 0}, {"clock", make_app, true, 1},
            {"disabled", make_app, false, 2},
        };
        assert(!crystal_registry_install(&phone, partial, 3));
        expect_ids({"clock"});
        assert(crystal_registry_catalog_count() == 3);
        assert(!crystal_registry_catalog_entry(2)->default_enabled);
    }
    assert(CrystalApp::live == 0);
    std::puts("PASS: registry queries, defaults, enable/slot roundtrips, bounds, storage errors, boot ordering and install failures");
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="crystal-registry-") as directory:
        temp = pathlib.Path(directory)
        for name, body in (("crystal_app.hpp", APP), ("esp_brookesia.hpp", PHONE),
                           ("esp_log.h", LOG), ("check.cpp", CHECK)):
            (temp / name).write_text(body)
        subprocess.run([
            *shlex.split(os.environ.get("CXX", "c++")),
            "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-I", str(temp),
            "-I", str(ROOT / "components/crystal_registry/include"),
            "-I", str(ROOT / "components/crystal_hal/include"),
            str(ROOT / "components/crystal_registry/src/crystal_registry.cpp"),
            str(temp / "check.cpp"), "-o", str(temp / "check"),
        ], check=True)
        subprocess.run([str(temp / "check")], check=True)


if __name__ == "__main__":
    main()
