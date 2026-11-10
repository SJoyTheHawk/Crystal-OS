/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
// Host integration: production package store, registry, Calculator, Brookesia and LVGL.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <fstream>
#include <string>
#include <vector>
#include "crystal_package.hpp"
#include "crystal_registry.hpp"
#include "calculator_app.hpp"
#include "crystal_hal.hpp"
namespace fs = std::filesystem;
static CrystalHal test_hal{};
CrystalHal &hal() { return test_hal; }
class Storage : public IStorage {
public:
    bool fail_write = false;
    std::map<std::string, std::vector<uint8_t>> values;
    bool get(const char *key, void *out, size_t *length) override {
        auto it = values.find(key);
        if (it == values.end() || *length < it->second.size()) return false;
        memcpy(out, it->second.data(), it->second.size()); *length = it->second.size(); return true;
    }
    bool set(const char *key, const void *data, size_t length) override {
        assert(strlen(key) <= 15);
        if (fail_write) return false;
        auto *bytes = static_cast<const uint8_t *>(data);
        values[key] = {bytes, bytes + length}; return true;
    }
    bool erase(const char *key) override { return values.erase(key) != 0; }
};
static Storage storage;
static std::string error;
static fs::path fixtures;
static bool install(const char *name, bool developer = true) {
    FILE *f = fopen((fixtures / name).c_str(), "rb"); assert(f);
    bool ok = crystal_package_install(f, developer, &error); fclose(f); return ok;
}
static void settle() { for (int i = 0; i < 45; ++i) { lv_tick_inc(10); lv_timer_handler(); } }
static void flush(lv_disp_drv_t *driver, const lv_area_t *, lv_color_t *) { lv_disp_flush_ready(driver); }
static void read_touch(lv_indev_drv_t *, lv_indev_data_t *data) { data->state = LV_INDEV_STATE_RELEASED; data->point = {0, 0}; }
static CrystalApp *factory(const CrystalPackageManifest &m) {
    return m.id == "org.crystal.calculator" && m.runtime == "native" && m.entry == "calculator.native" ? new CalculatorApp(m.name.c_str()) : nullptr;
}
static CrystalApp *builtin() { return new CalculatorApp; }
static CrystalApp *package_app() {
    for (size_t i = 0; i < crystal_registry_installed_count(); ++i)
        if (!strcmp(crystal_registry_installed_id(i), "org.crystal.calculator")) return crystal_registry_installed_app(i);
    return nullptr;
}
int main(int argc, char **argv) {
    assert(argc == 3); fixtures = argv[1]; const fs::path root = argv[2];
    setbuf(stdout, nullptr); test_hal.storage = &storage;
    CrystalPackageLimits limits{1024 * 1024, 2 * 1024 * 1024, 4};
    assert(crystal_packages_init(root.c_str(), limits, &error));
    assert(!install("calculator.capp", false));
    for (auto name : {"crc.capp", "truncated.capp", "traversal.capp", "duplicate.capp", "reserved.capp", "abi.capp", "network.capp", "missing.capp", "trailing.capp", "signature.capp", "depth.capp"}) {
        assert(!install(name));
        assert(crystal_packages().empty());
        assert(!fs::exists(root / ".staging"));
    }
    assert(crystal_packages_init(root.c_str(), {1024, 2 * 1024 * 1024, 4}, &error));
    assert(!install("calculator.capp"));
    assert(crystal_packages_init(root.c_str(), {1024 * 1024, 100, 4}, &error));
    assert(!install("calculator.capp"));
    assert(crystal_packages_init(root.c_str(), limits, &error));
    storage.fail_write = true;
    assert(!install("calculator.capp"));
    assert(!fs::exists(root / "org.crystal.calculator"));
    storage.fail_write = false;
    // A power loss after directory rename but before NVS commit is reconciled.
    fs::create_directories(root / ".staging");
    fs::copy_file(fixtures / "app.json", root / ".staging/app.json");
    assert(crystal_packages_init(root.c_str(), limits, &error));
    assert(!fs::exists(root / ".staging"));
    assert(install("calculator.capp"));
    assert(!install("calculator.capp"));
    assert(crystal_package_set_enabled("org.crystal.calculator", false));
    assert(crystal_package_set_slot("org.crystal.calculator", 42));
    assert(crystal_packages_init(root.c_str(), limits, &error));
    assert(crystal_packages().size() == 1 && !crystal_packages()[0].enabled && crystal_packages()[0].slot == 42);
    assert(crystal_package_set_enabled("org.crystal.calculator", true));
    assert(!install("large.capp")); // Aggregate budget includes installed apps.
    assert(install("sibling.capp"));
    uint32_t first = 41, second = 73, out = 0;
    assert(crystal_package_state_set("org.crystal.calculator", "same-key", &first, sizeof(first)));
    assert(crystal_package_state_set("org.crystal.sentinel", "same-key", &second, sizeof(second)));
    size_t length = sizeof(out);
    assert(crystal_package_state_get("org.crystal.calculator", "same-key", &out, &length) && out == first);
    length = sizeof(out);
    assert(crystal_package_state_get("org.crystal.sentinel", "same-key", &out, &length) && out == second);
    storage.fail_write = true;
    assert(!crystal_package_set_enabled("org.crystal.calculator", false));
    assert(!crystal_package_state_set("org.crystal.calculator", "same-key", &second, sizeof(second)));
    storage.fail_write = false;
    length = sizeof(out);
    assert(crystal_package_state_get("org.crystal.calculator", "same-key", &out, &length) && out == first);
    char too_large[1537]{};
    assert(!crystal_package_state_set("org.crystal.calculator", "large", too_large, sizeof(too_large)));
    auto saved_registry = storage.values["pkg.registry"];
    storage.values["pkg.registry"] = {'b', 'a', 'd'};
    assert(!crystal_packages_init(root.c_str(), limits, &error));
    assert(fs::exists(root / "org.crystal.calculator"));
    storage.values["pkg.registry"] = saved_registry;
    assert(crystal_packages_init(root.c_str(), limits, &error));
    // Keep state when a manifest is damaged; repair does not silently re-enable.
    std::ofstream(root / "org.crystal.calculator/app.json") << "{}";
    assert(crystal_packages_init(root.c_str(), limits, &error));
    length = sizeof(out);
    assert(crystal_package_state_get("org.crystal.calculator", "same-key", &out, &length) && out == first);
    assert(!crystal_package_set_enabled("org.crystal.calculator", true));
    fs::copy_file(fixtures / "app.json", root / "org.crystal.calculator/app.json", fs::copy_options::overwrite_existing);
    assert(crystal_packages_init(root.c_str(), limits, &error));
    assert(crystal_package_set_enabled("org.crystal.calculator", true));
    assert(crystal_package_remove("org.crystal.sentinel", true, &error));
    assert(crystal_packages_init(root.c_str(), {1024 * 1024, 2 * 1024 * 1024, 0}, &error));
    assert(!install("worker.capp"));
    assert(crystal_packages_init(root.c_str(), limits, &error));
    assert(crystal_package_launch_result("org.crystal.calculator", false));
    assert(crystal_package_launch_result("org.crystal.calculator", false));
    assert(crystal_package_launch_result("org.crystal.calculator", false));
    assert(!crystal_packages()[0].enabled);
    assert(crystal_package_set_enabled("org.crystal.calculator", true));
    assert(crystal_package_remove("org.crystal.calculator", true, &error));
    std::puts("PASS: package bounds, CRC, traversal, duplicates, reserved paths, ABI, permissions, quotas, signature rejection, rollback, staging and persistence");

    // Resource ledger: typed handles, stale handles, memory ceiling and all leak categories.
    CrystalResources owner(128, 1), other(128, 1);
    int task = 1, queue = 2, timer = 3, object = 4, task2 = 5;
    auto t = owner.track(CrystalResourceKind::Task, &task, 32); assert(t);
    auto other_task = other.track(CrystalResourceKind::Task, &task);
    assert(other_task && other_task != t && !owner.lookup(other_task, CrystalResourceKind::Task));
    assert(!owner.track(CrystalResourceKind::Task, &task2));
    assert(!other.lookup(t, CrystalResourceKind::Task));
    assert(!owner.lookup(t, CrystalResourceKind::Timer));
    auto q = owner.track(CrystalResourceKind::Queue, &queue, 16); assert(q);
    auto tm = owner.track(CrystalResourceKind::Timer, &timer); assert(tm);
    auto obj = owner.track(CrystalResourceKind::Object, &object); assert(obj);
    auto *memory = owner.allocate(80); assert(memory && !owner.allocate(1));
    CrystalCleanupReport report;
    assert(!crystal_app_verify_cleanup(owner, &report));
    assert(report.tasks == 1 && report.queues == 1 && report.timers == 1 && report.allocations == 1 && report.objects == 1 && report.heap_bytes == 128);
    assert(owner.release(t) && !owner.lookup(t, CrystalResourceKind::Task));
    auto fresh = owner.track(CrystalResourceKind::Task, &task); assert(fresh && fresh != t);
    assert(owner.release(fresh) && owner.release(q) && owner.release(tm) && owner.release(obj) && owner.deallocate(memory));
    assert(crystal_app_verify_cleanup(owner, &report));
    assert(other.release(other_task));
    std::puts("PASS: per-instance resource ownership, typed/stale handles, memory and worker ceilings, task/timer/queue/object/allocation leak scans");

    lv_init();
    static lv_color_t buffer[480 * 40]; static lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw, buffer, nullptr, 480 * 40);
    static lv_disp_drv_t driver; lv_disp_drv_init(&driver);
    driver.hor_res = driver.ver_res = 480; driver.draw_buf = &draw; driver.flush_cb = flush;
    auto *display = lv_disp_drv_register(&driver);
    static lv_indev_drv_t input; lv_indev_drv_init(&input); input.type = LV_INDEV_TYPE_POINTER; input.read_cb = read_touch;
    auto *pointer = lv_indev_drv_register(&input);
    ESP_Brookesia_Phone phone(display);
    ESP_Brookesia_PhoneStylesheet_t sheet = ESP_BROOKESIA_PHONE_480_480_DARK_STYLESHEET();
    sheet.core.manager.app.max_running_num = 1; sheet.home.flags.enable_recents_screen = 0;
    assert(phone.setTouchDevice(pointer) && phone.addStylesheet(sheet) && phone.activateStylesheet(sheet) && phone.begin());
    crystal_registry_set_package_factory(factory);
    const CrystalAppEntry builtins[] = {{"calculator", builtin, true, 4}};
    assert(crystal_registry_install(&phone, builtins, 1)); settle();
    CrystalState builtin_state("Calculator"); assert(builtin_state.set_u32("keep", 99));
    for (unsigned i = 0; i < 10; ++i) {
        assert(install("calculator.capp"));
        assert(crystal_registry_apply_changes());
        assert(crystal_registry_catalog_count() == 2); // Removed slots are reused.
        auto *app = package_app(); assert(app);
        for (unsigned launch = 0; launch < 10; ++launch) {
            ESP_Brookesia_CoreAppEventData_t event{app->getId(), ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START, nullptr};
            assert(phone.sendAppEvent(&event)); settle();
            assert(phone.getManager().getActiveApp() == app);
            assert(app->resources().report().objects == 1);
            assert(app->state().set_u32("counter", launch));
            uint32_t value = 0; assert(app->state().get_u32("counter", &value) && value == launch);
            event.type = ESP_BROOKESIA_CORE_APP_EVENT_TYPE_STOP;
            assert(phone.sendAppEvent(&event)); settle();
            assert(crystal_app_verify_cleanup(app->resources(), &report));
        }
        assert(crystal_registry_uninstall_package("org.crystal.calculator")); settle();
        assert(!package_app() && crystal_packages().empty());
        assert(!fs::exists(root / "org.crystal.calculator"));
        uint32_t value = 0; assert(builtin_state.get_u32("keep", &value) && value == 99);
        for (const auto &kv : storage.values) assert(kv.first[0] != 'p' || kv.first == "pkg.registry");
    }
    std::puts("PASS: real Calculator package 10 install/uninstall cycles, 100 launches with production LVGL/Brookesia, zero tracked leaks, builtin state preserved");
    assert(install("calculator.capp")); assert(crystal_registry_apply_changes());
    auto *active_package = package_app(); assert(active_package);
    ESP_Brookesia_CoreAppEventData_t start{active_package->getId(), ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START, nullptr};
    assert(phone.sendAppEvent(&start)); settle();
    assert(crystal_registry_set_enabled("org.crystal.calculator", false));
    // Normal live UI objects must not be mistaken for a failed teardown when
    // undoing Disable from the Settings overlay above that app.
    assert(crystal_registry_set_enabled("org.crystal.calculator", true));
    assert(crystal_registry_uninstall_package("org.crystal.calculator")); settle();
    assert(!package_app() && crystal_packages().empty());
    std::puts("PASS: enable/disable with a live UI and uninstall while the package is running");
    assert(install("bus-leak.capp")); assert(crystal_registry_apply_changes());
    auto *app = package_app(); assert(app);
    // Reproduce the Bus service's retained worker and queue ownership. A full
    // HTTP/catalog run on the ESP32 is a separate hardware gate.
    auto worker = app->resources().track(CrystalResourceKind::Task, &task);
    auto retained_queue = app->resources().track(CrystalResourceKind::Queue, &queue);
    assert(worker && retained_queue);
    assert(app->state().set_u32("keep", 7));
    assert(!crystal_registry_uninstall_package("org.crystal.calculator"));
    assert(!crystal_packages()[0].enabled && fs::exists(root / "org.crystal.calculator"));
    assert(!crystal_registry_set_enabled("org.crystal.calculator", true));
    uint32_t value = 0; assert(app->state().get_u32("keep", &value) && value == 7);
    assert(app->resources().release(worker) && app->resources().release(retained_queue));
    assert(crystal_registry_uninstall_package("org.crystal.calculator"));
    assert(install("calculator.capp"));
    assert(crystal_package_state_set("org.crystal.calculator", "keep", &value, sizeof(value)));
    fs::remove_all(root / "org.crystal.calculator");
    assert(crystal_packages_init(root.c_str(), limits, &error) && crystal_packages().empty());
    for (const auto &kv : storage.values) assert(kv.first[0] != 'p' || kv.first == "pkg.registry");
    std::puts("PASS: leak blocks uninstall, disables app and retains data; hand-deleted folder prunes metadata and all package state");
    // Brookesia's phone destructor expects its home to remain alive while apps
    // are detached, so explicitly remove the fixture builtin first.
    auto *bundled = crystal_registry_installed_app(0);
    assert(phone.uninstallApp(bundled)); delete bundled;
}
