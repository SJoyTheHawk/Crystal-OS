#!/usr/bin/env python3
"""Test production CrystalState against a host NVS adapter, including failures.

Run: python3 tools/test_crystal_state.py
Real encrypted NVS behavior is checked separately on the board.
"""
import os
import pathlib
import shlex
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

BROOKESIA = r'''
#pragma once
struct ESP_Brookesia_PhoneAppData_t { struct { int enable_navigation_gesture; } flags; };
#define ESP_BROOKESIA_PHONE_APP_DATA_DEFAULT(icon, a, b) ((void)(icon), ESP_Brookesia_PhoneAppData_t{})
#define ESP_BROOKESIA_CORE_APP_DATA_DEFAULT(name, icon, a) ((void)(name), (void)(icon), 0)
class ESP_Brookesia_PhoneApp {
public:
    ESP_Brookesia_PhoneApp(int, ESP_Brookesia_PhoneAppData_t) {}
    virtual ~ESP_Brookesia_PhoneApp() = default;
    virtual bool init() = 0;
    virtual bool deinit() = 0;
    virtual bool run() = 0;
    virtual bool pause() = 0;
    virtual bool resume() = 0;
    virtual bool close() = 0;
    virtual bool back() = 0;
    bool notifyCoreClosed() { return true; }
};
'''
LOG = r'''
#pragma once
#include <cstdarg>
#include <cstdio>
inline void test_log(const char *tag, const char *fmt, ...) {
    std::printf("%s: ", tag);
    va_list args; va_start(args, fmt); std::vprintf(fmt, args); va_end(args);
    std::puts("");
}
#define ESP_LOGI test_log
#define ESP_LOGW test_log
#define ESP_LOGE test_log
'''
NVS = r'''
#pragma once
#include <cstdint>
using esp_err_t = int;
using nvs_handle_t = unsigned;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NVS_NOT_FOUND = 1;
constexpr int ESP_ERR_NO_MEM = 2, ESP_ERR_INVALID_ARG = 3;
constexpr int NVS_READWRITE = 1, NVS_TYPE_ANY = 255;
struct Iterator;
using nvs_iterator_t = Iterator *;
struct nvs_entry_info_t { char namespace_name[16]; char key[16]; int type; };
const char *esp_err_to_name(esp_err_t);
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
void nvs_close(nvs_handle_t);
esp_err_t nvs_entry_find_in_handle(nvs_handle_t, int, nvs_iterator_t *);
esp_err_t nvs_entry_info(nvs_iterator_t, nvs_entry_info_t *);
esp_err_t nvs_entry_next(nvs_iterator_t *);
void nvs_release_iterator(nvs_iterator_t);
esp_err_t nvs_erase_key(nvs_handle_t, const char *);
esp_err_t nvs_commit(nvs_handle_t);
'''
CHECK = r'''
#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "crystal_app.hpp"
#include "crystal_hal.hpp"
#include "nvs.h"

using Bytes = std::vector<uint8_t>;
using Namespace = std::map<std::string, Bytes>;
static std::map<std::string, Namespace> namespaces;
static bool fail_open, fail_find, fail_info, fail_next, fail_commit;
static bool race_missing;
static std::string fail_erase;
static unsigned handles, iterators, erases, commits;
static CrystalHal test_hal{};
CrystalHal &hal() { return test_hal; }

struct Iterator { std::vector<std::string> keys; size_t index = 0; };
const char *esp_err_to_name(esp_err_t err) { return err == ESP_OK ? "OK" : "injected error"; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) {
    assert(std::strcmp(name, "crystal") == 0 && mode == NVS_READWRITE);
    if (fail_open) return ESP_FAIL;
    assert(handles == 0);
    ++handles; *handle = 1; return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { assert(handle == 1 && handles == 1); --handles; }
esp_err_t nvs_entry_find_in_handle(nvs_handle_t handle, int type, nvs_iterator_t *out) {
    assert(handle == 1 && handles == 1 && type == NVS_TYPE_ANY);
    *out = nullptr;
    if (fail_find) return ESP_ERR_NO_MEM;
    if (namespaces["crystal"].empty()) return ESP_ERR_NVS_NOT_FOUND;
    *out = new Iterator; ++iterators;
    for (const auto &entry : namespaces["crystal"]) (*out)->keys.push_back(entry.first);
    return ESP_OK;
}
esp_err_t nvs_entry_info(nvs_iterator_t it, nvs_entry_info_t *info) {
    assert(it && iterators == 1);
    if (fail_info) return ESP_ERR_INVALID_ARG;
    std::strcpy(info->key, it->keys[it->index].c_str()); return ESP_OK;
}
void nvs_release_iterator(nvs_iterator_t it) {
    if (it) { assert(iterators == 1); --iterators; delete it; }
}
esp_err_t nvs_entry_next(nvs_iterator_t *it) {
    assert(*it && iterators == 1);
    if (fail_next) { nvs_release_iterator(*it); *it = nullptr; return ESP_FAIL; }
    if (++(*it)->index < (*it)->keys.size()) return ESP_OK;
    nvs_release_iterator(*it); *it = nullptr; return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) {
    assert(handle == 1 && handles == 1 && iterators == 0);
    ++erases;
    if (fail_erase == key) return ESP_FAIL;
    if (race_missing) { namespaces["crystal"].erase(key); return ESP_ERR_NVS_NOT_FOUND; }
    return namespaces["crystal"].erase(key) ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
    assert(handle == 1 && handles == 1 && iterators == 0); ++commits;
    return fail_commit ? ESP_FAIL : ESP_OK;
}
class Storage final : public IStorage {
public:
    std::string last_key;
    bool get(const char *key, void *value, size_t *length) override {
        const auto it = namespaces["crystal"].find(key);
        if (it == namespaces["crystal"].end()) return false;
        const auto capacity = *length; *length = it->second.size();
        if (capacity < *length) return false;
        std::memcpy(value, it->second.data(), *length); return true;
    }
    bool set(const char *key, const void *value, size_t length) override {
        assert(std::strlen(key) <= 15);
        last_key = key;
        const auto *bytes = static_cast<const uint8_t *>(value);
        namespaces["crystal"][key] = Bytes(bytes, bytes + length); return true;
    }
    bool erase(const char *key) override { return namespaces["crystal"].erase(key) != 0; }
};
static void no_leaks() { assert(handles == 0 && iterators == 0); }
int main() {
    Storage storage; test_hal.storage = &storage;
    CrystalState target("Phase13.2 target"), other("Phase13.2 sentinel");
    assert(target.clear()); no_leaks();
    assert(commits == 0 && erases == 0);
    assert(target.set_u32("counter", 42));
    const std::string key = storage.last_key;
    const auto prefix = key.substr(0, 8);
    assert(target.set_u32("score", 100));
    const char message[] = "hello";
    assert(target.set("message", message, sizeof(message)));
    assert(other.set_u32("counter", 77));
    uint32_t value = 0;
    assert(target.get_u32("counter", &value) && value == 42);
    assert(target.get_u32("score", &value) && value == 100);
    // Unrelated system/registry keys, a prefix lookalike, and another namespace.
    namespaces["crystal"]["r12345678.e"] = {1};
    namespaces["crystal"]["brightness"] = {80};
    const auto lookalike = prefix.substr(0, 7) + "x.keep";
    namespaces["crystal"][lookalike] = {19};
    namespaces["separate"][key] = {99};
    auto expected = namespaces;
    for (auto it = expected["crystal"].begin(); it != expected["crystal"].end();) {
        if (it->first.compare(0, prefix.size(), prefix) == 0) it = expected["crystal"].erase(it);
        else ++it;
    }
    assert(target.clear()); no_leaks();
    assert(namespaces == expected && erases == 3 && commits == 1);
    assert(!target.get_u32("counter", &value));
    assert(!target.get_u32("score", &value));
    char out[16]{}; size_t length = sizeof(out);
    assert(!target.get("message", out, &length));
    assert(other.get_u32("counter", &value) && value == 77);
    assert(target.clear()); no_leaks();
    assert(namespaces == expected && erases == 3 && commits == 1);
    assert(target.set_u32("counter", 84));
    assert(target.get_u32("counter", &value) && value == 84);
    assert(target.set_u32("score", 100));
    const auto before_failure = namespaces;
    for (bool *failure : {&fail_open, &fail_find, &fail_info, &fail_next}) {
        *failure = true;
        assert(!target.clear()); no_leaks();
        assert(namespaces == before_failure && commits == 1 && erases == 3);
        *failure = false;
    }
    fail_erase = key;
    assert(!target.clear()); no_leaks();
    assert(target.get_u32("counter", &value) && value == 84);
    assert(!target.get_u32("score", &value));
    assert(commits == 2);
    fail_erase.clear();
    fail_commit = true;
    assert(!target.clear()); no_leaks();
    assert(commits == 3);
    fail_commit = false;
    assert(target.set_u32("counter", 42));
    race_missing = true;
    assert(target.clear()); no_leaks();
    race_missing = false;
    assert(namespaces == expected);
    std::puts("PASS: CrystalState clear, prefix/namespace isolation, empty/repeated clear, reuse, storage failures and resource cleanup");
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="crystal-state-") as directory:
        temp = pathlib.Path(directory)
        for name, body in (("esp_brookesia.hpp", BROOKESIA), ("esp_log.h", LOG),
                           ("nvs.h", NVS), ("check.cpp", CHECK),
                           ("lvgl.h", "#pragma once\n#include <cstdint>\ninline uint32_t lv_tick_get() { return 0; }\ninline uint32_t lv_tick_elaps(uint32_t) { return 0; }\n")):
            (temp / name).write_text(body)
        subprocess.run([
            *shlex.split(os.environ.get("CXX", "c++")),
            "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-I", str(temp), "-I", str(ROOT / "components/crystal_app/include"),
            "-I", str(ROOT / "components/crystal_hal/include"),
            str(ROOT / "components/crystal_app/src/crystal_app.cpp"),
            str(temp / "check.cpp"), "-o", str(temp / "check"),
        ], check=True)
        subprocess.run([str(temp / "check")], check=True)


if __name__ == "__main__":
    main()
