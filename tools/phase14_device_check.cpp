/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
// Exercise the actual device harness against production LVGL, registry and store.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "calculator_app.hpp"
#include "crystal_hal.hpp"
#include "crystal_package.hpp"
#include "crystal_phase14_device_test.h"
#include "crystal_registry.hpp"
namespace fs = std::filesystem;
namespace {
constexpr const char *kId = "org.crystal.phase14.calculator";
constexpr CrystalPackageLimits kLimits{1024 * 1024, 2 * 1024 * 1024, 4};
CrystalHal test_hal{};
class Storage : public IStorage {
public:
    std::map<std::string, std::vector<uint8_t>> values;
    bool get(const char *key, void *out, size_t *length) override {
        auto found = values.find(key);
        if (found == values.end() || *length < found->second.size()) return false;
        memcpy(out, found->second.data(), found->second.size());
        *length = found->second.size(); return true;
    }
    bool set(const char *key, const void *value, size_t length) override {
        assert(strlen(key) <= 15);
        auto *bytes = static_cast<const uint8_t *>(value);
        values[key] = {bytes, bytes + length}; return true;
    }
    bool erase(const char *key) override { return values.erase(key) != 0; }
};
bool restarted = false;
void restart() { assert(!restarted); restarted = true; }
void flush(lv_disp_drv_t *driver, const lv_area_t *, lv_color_t *) { lv_disp_flush_ready(driver); }
void read_touch(lv_indev_drv_t *, lv_indev_data_t *data) { data->state = LV_INDEV_STATE_RELEASED; data->point = {0, 0}; }
CrystalApp *builtin() { return new CalculatorApp; }
CrystalApp *factory(const CrystalPackageManifest &m) { return m.id == kId ? new CalculatorApp(m.name.c_str()) : nullptr; }
void settle() { for (unsigned i = 0; i < 45; ++i) { lv_tick_inc(10); lv_timer_handler(); } }
void drive(CrystalPhase14TestStatus until) {
    for (unsigned step = 0; step < 300 && crystal_phase14_device_test_status() != until; ++step) {
        assert(crystal_phase14_device_test_status() != CrystalPhase14TestStatus::Failed);
        settle();
    }
    assert(crystal_phase14_device_test_status() == until);
}
}
CrystalHal &hal() { return test_hal; }
int main(int argc, char **argv) {
    assert(argc == 3); setbuf(stdout, nullptr);
    std::ifstream source(argv[1], std::ios::binary);
    std::vector<uint8_t> fixture((std::istreambuf_iterator<char>(source)), {});
    assert(!fixture.empty());
    const fs::path root(argv[2]);
    Storage storage; test_hal.storage = &storage;
    std::string error;
    assert(crystal_packages_init(root.c_str(), kLimits, &error));
    CrystalState bundled("Calculator");
    const char sentinel[] = "7+8";
    assert(bundled.set("formula", sentinel, sizeof(sentinel)));
    lv_init();
    static lv_color_t buffer[480 * 40]; static lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw, buffer, nullptr, 480 * 40);
    static lv_disp_drv_t driver;
    static lv_indev_drv_t input;
    lv_disp_t *display = nullptr;
    lv_indev_t *pointer = nullptr;
    auto boot = [&]() {
        // A reboot starts with fresh display screens and input state too.
        lv_disp_drv_init(&driver);
        driver.hor_res = driver.ver_res = 480; driver.draw_buf = &draw; driver.flush_cb = flush;
        display = lv_disp_drv_register(&driver);
        lv_indev_drv_init(&input);
        input.type = LV_INDEV_TYPE_POINTER; input.read_cb = read_touch;
        pointer = lv_indev_drv_register(&input);
        assert(crystal_packages_init(root.c_str(), kLimits, &error));
        auto phone = std::make_unique<ESP_Brookesia_Phone>(display);
        ESP_Brookesia_PhoneStylesheet_t sheet = ESP_BROOKESIA_PHONE_480_480_DARK_STYLESHEET();
        sheet.core.manager.app.max_running_num = 1; sheet.home.flags.enable_recents_screen = 0;
        assert(phone->setTouchDevice(pointer) && phone->addStylesheet(sheet) && phone->activateStylesheet(sheet) && phone->begin());
        crystal_registry_set_package_factory(factory);
        const CrystalAppEntry apps[] = {{"calculator", builtin, true, 4}};
        assert(crystal_registry_install(phone.get(), apps, 1));
        // Match Crystal shell startup: an app is already active before the test.
        // After the checkpoint reboot, the shell remembers Calculator Test.
        auto *app = crystal_registry_installed_app(0);
        for (size_t i = 0; i < crystal_registry_installed_count(); ++i) {
            if (!strcmp(crystal_registry_installed_id(i), kId)) app = crystal_registry_installed_app(i);
        }
        ESP_Brookesia_CoreAppEventData_t event{app->getId(), ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START, nullptr};
        assert(phone->sendAppEvent(&event)); settle();
        return phone;
    };
    auto end_boot = [&](std::unique_ptr<ESP_Brookesia_Phone> &phone) {
        std::vector<CrystalApp *> instances;
        for (size_t i = 0; i < crystal_registry_installed_count(); ++i) instances.push_back(crystal_registry_installed_app(i));
        for (auto *app : instances) {
            if (phone->getManager().getActiveApp() == app) {
                ESP_Brookesia_CoreAppEventData_t stop{app->getId(), ESP_BROOKESIA_CORE_APP_EVENT_TYPE_STOP, nullptr};
                assert(phone->sendAppEvent(&stop)); settle();
            }
            assert(app->resources().report().clean());
            assert(phone->uninstallApp(app)); delete app;
        }
        assert(phone->del()); phone.reset();
        lv_indev_delete(pointer); lv_disp_remove(display);
        pointer = nullptr; display = nullptr;
    };
    auto start = [&](ESP_Brookesia_Phone *phone, unsigned run) {
        return crystal_phase14_device_test_start(phone, fixture.data(), fixture.size(), root.c_str(), restart, run);
    };
    auto phone = boot();
    assert(start(phone.get(), 1));
    drive(CrystalPhase14TestStatus::AwaitingReboot);
    assert(restarted && fs::exists(root / kId));
    end_boot(phone); // Keep NVS and files, reconstruct phone and registry at boot.
    phone = boot();
    restarted = false;
    assert(start(phone.get(), 1)); drive(CrystalPhase14TestStatus::Passed);
    assert(!restarted && crystal_packages().empty() && !fs::exists(root / kId));
    char formula[64]{}; size_t length = sizeof(formula);
    assert(bundled.get("formula", formula, &length) && length == sizeof(sentinel) && !strcmp(formula, sentinel));
    assert(start(phone.get(), 1));
    assert(crystal_phase14_device_test_status() == CrystalPhase14TestStatus::Passed);
    std::puts("PASS: production device harness, 10 cycles, UI formula/result restoration, simulated reboot, state deletion and completed-run guard");

    FILE *stream = fmemopen(fixture.data(), fixture.size(), "rb"); assert(stream);
    assert(crystal_package_install(stream, true, &error)); fclose(stream);
    assert(crystal_registry_apply_changes());
    uint32_t sentinel_value = 77;
    assert(crystal_package_state_set(kId, "keep", &sentinel_value, sizeof(sentinel_value)));
    assert(!start(phone.get(), 2));
    assert(crystal_phase14_device_test_status() == CrystalPhase14TestStatus::Failed && fs::exists(root / kId));
    uint32_t value = 0; length = sizeof(value);
    assert(crystal_package_state_get(kId, "keep", &value, &length) && value == sentinel_value);
    assert(crystal_registry_uninstall_package(kId));
    std::puts("PASS: harness refuses a pre-existing fixture ID and preserves its state");

    assert(start(phone.get(), 3)); drive(CrystalPhase14TestStatus::AwaitingReboot);
    assert(restarted);
    const char corrupt[] = "99";
    assert(crystal_package_state_set(kId, "formula", corrupt, sizeof(corrupt)));
    end_boot(phone); phone = boot();
    restarted = false;
    assert(!start(phone.get(), 3));
    assert(crystal_phase14_device_test_status() == CrystalPhase14TestStatus::Failed && !restarted && fs::exists(root / kId));
    assert(!start(phone.get(), 3)); // Failure cannot turn into a reboot loop.
    assert(crystal_registry_uninstall_package(kId));
    std::puts("PASS: corrupt reboot state fails, retains package for inspection, and cannot cause a reboot loop");
    end_boot(phone);
}
