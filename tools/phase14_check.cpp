/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
// Host integration: production package store, registry, Calculator, Brookesia and LVGL.
#include <cassert>
#include <cstdio>
#include <cstdlib>
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
#include "crystal_core.hpp"
#include "crystal_network.h"
#include "crystal_declarative.hpp"
namespace fs = std::filesystem;
static CrystalHal test_hal{};
CrystalHal &hal() { return test_hal; }
static CrystalTimerState test_timer{};
bool crystal_timer_start(uint32_t seconds) {
    if (!seconds) return false;
    test_timer = {true, false, seconds, seconds, time(nullptr) + seconds}; return true;
}
bool crystal_timer_restore(time_t end_at, uint32_t paused_remaining, uint32_t duration_seconds) {
    test_timer = {paused_remaining == 0, paused_remaining != 0, duration_seconds,
                  paused_remaining ? paused_remaining : static_cast<uint32_t>(end_at - time(nullptr)), end_at};
    return true;
}
bool crystal_timer_pause() {
    if (!test_timer.running) return false;
    test_timer.running = false; test_timer.paused = true; return true;
}
bool crystal_timer_resume() {
    if (!test_timer.paused) return false;
    test_timer.running = true; test_timer.paused = false; return true;
}
void crystal_timer_reset() { test_timer = {}; }
CrystalTimerState crystal_timer_state() { return test_timer; }
extern "C" bool crystal_network_has_ip(void) { return true; }
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
static lv_color_t frame[480 * 480];
static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels) {
    for (int y = area->y1; y <= area->y2; ++y) {
        for (int x = area->x1; x <= area->x2; ++x) {
            if (x >= 0 && x < 480 && y >= 0 && y < 480) frame[y * 480 + x] = *pixels;
            ++pixels;
        }
    }
    lv_disp_flush_ready(driver);
}
static void capture_dashboard(lv_disp_t *display, const char *name) {
    const char *directory = std::getenv("CRYSTAL_LAYOUT_CAPTURE_DIR");
    if (!directory) return;
    fs::create_directories(directory);
    lv_obj_invalidate(lv_scr_act()); lv_obj_invalidate(lv_layer_top());
    lv_refr_now(display);
    std::ofstream file(fs::path(directory) / name, std::ios::binary);
    assert(file); file << "P6\n480 480\n255\n";
    for (const lv_color_t pixel : frame) {
        lv_color32_t color; color.full = lv_color_to32(pixel);
        const char rgb[] = {static_cast<char>(color.ch.red), static_cast<char>(color.ch.green), static_cast<char>(color.ch.blue)};
        file.write(rgb, sizeof(rgb));
    }
    assert(file.good());
}
static void read_touch(lv_indev_drv_t *, lv_indev_data_t *data) { data->state = LV_INDEV_STATE_RELEASED; data->point = {0, 0}; }
static CrystalApp *factory(const CrystalPackageManifest &m) {
    if (m.id == "org.crystal.calculator" && m.runtime == "native" && m.entry == "calculator.native")
        return new CalculatorApp(m.name.c_str());
    if (m.runtime == "declarative")
        return new DeclarativeApp(m.name.c_str(), m.id.c_str(), m.entry.c_str(), m.system_timer);
    return nullptr;
}
static CrystalApp *builtin() { return new CalculatorApp; }
static CrystalApp *package_app() {
    for (size_t i = 0; i < crystal_registry_installed_count(); ++i)
        if (!strcmp(crystal_registry_installed_id(i), "org.crystal.calculator")) return crystal_registry_installed_app(i);
    return nullptr;
}
static CrystalApp *installed_app(const char *id) {
    for (size_t i = 0; i < crystal_registry_installed_count(); ++i)
        if (!strcmp(crystal_registry_installed_id(i), id)) return crystal_registry_installed_app(i);
    return nullptr;
}
static lv_obj_t *find_label(lv_obj_t *root, const char *text) {
    if (root == nullptr) return nullptr;
    if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) {
        if (lv_obj_t *found = find_label(lv_obj_get_child(root, i), text)) return found;
    }
    return nullptr;
}
static lv_obj_t *find_display_label(lv_disp_t *display, const char *text) {
    if (display == nullptr) return nullptr;
    lv_obj_t *found = find_label(lv_disp_get_scr_act(display), text);
    if (found == nullptr) found = find_label(lv_disp_get_scr_prev(display), text);
    if (found == nullptr) found = find_label(lv_disp_get_layer_top(display), text);
    if (found == nullptr) found = find_label(lv_disp_get_layer_sys(display), text);
    return found;
}
static lv_obj_t *find_object_type(lv_obj_t *root, const lv_obj_class_t *type) {
    if (root == nullptr) return nullptr;
    if (lv_obj_check_type(root, type)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) {
        if (lv_obj_t *found = find_object_type(lv_obj_get_child(root, i), type)) return found;
    }
    return nullptr;
}
static void print_labels(lv_obj_t *root) {
    if (root == nullptr) return;
    if (lv_obj_check_type(root, &lv_label_class)) std::fprintf(stderr, "LABEL=[%s]\n", lv_label_get_text(root));
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) print_labels(lv_obj_get_child(root, i));
}
static void click_button_text(lv_disp_t *display, const char *text) {
    lv_obj_t *label = find_display_label(display, text);
    if (label == nullptr) {
        std::fprintf(stderr, "Could not find requested button label: %s\n", text);
        print_labels(lv_disp_get_scr_act(display));
        print_labels(lv_disp_get_scr_prev(display));
        print_labels(lv_disp_get_layer_top(display));
        print_labels(lv_disp_get_layer_sys(display));
    }
    assert(label != nullptr);
    lv_obj_t *button = lv_obj_get_parent(label);
    assert(button != nullptr);
    lv_event_send(button, LV_EVENT_CLICKED, nullptr);
}
static void assert_inside(lv_obj_t *child, lv_obj_t *parent) {
    lv_area_t box{}, bounds{};
    lv_obj_get_coords(child, &box); lv_obj_get_content_coords(parent, &bounds);
    assert(box.x1 >= bounds.x1 && box.x2 <= bounds.x2);
    assert(box.y1 >= bounds.y1 && box.y2 <= bounds.y2);
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
    std::string package_content;
    assert(crystal_package_read_file("org.crystal.calculator", "calculator.native", &package_content, 128));
    assert(package_content == "crystal-native:calculator-v1\n");
    assert(!crystal_package_read_file("org.crystal.calculator", "data/state.json", &package_content, 128));
    assert(!crystal_package_read_file("org.crystal.calculator", "../outside", &package_content, 128));
    assert(!crystal_package_read_file("org.crystal.calculator", "calculator.native", &package_content, 2));
    assert(package_content.empty());
    assert(!crystal_package_read_file("org.crystal.calculator", "missing.txt", &package_content, 128));
    {
        const auto assets = root / "org.crystal.calculator";
        std::ofstream(assets / "empty.bin", std::ios::binary).close();
        assert(crystal_package_read_file("org.crystal.calculator", "empty.bin", &package_content, 1));
        assert(package_content.empty());
        const std::string binary("A\0B", 3);
        { std::ofstream file(assets / "binary.bin", std::ios::binary); file.write(binary.data(), binary.size()); }
        assert(crystal_package_read_file("org.crystal.calculator", "binary.bin", &package_content, 3));
        assert(package_content == binary);
        fs::create_directory(assets / "folder");
        assert(!crystal_package_read_file("org.crystal.calculator", "folder", &package_content, 128));
        fs::remove(assets / "empty.bin"); fs::remove(assets / "binary.bin"); fs::remove(assets / "folder");
    }
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
    // Exercise the actual launcher across page boundaries, using reversed IDs
    // to ensure compaction preserves visual order rather than map order.
    {
        const auto &launcher_data = phone.getHome().getData().app_launcher.data;
        const size_t capacity = (launcher_data.table.size.width / launcher_data.icon.main.size.width) *
                                (launcher_data.table.size.height / launcher_data.icon.main.size.height);
        assert(capacity > 0 && capacity < 100);
        auto *parent = lv_obj_create(lv_layer_top());
        lv_obj_set_size(parent, 480, 480);
        {
            std::vector<std::string> names;
            for (size_t i = 0; i < capacity * 2 + 1; ++i) names.push_back("Launcher fixture " + std::to_string(i));
            names.front() = "Action Guard Check";
            names[1] = "Clock";
            names.back() = "Very long app title that must stay within its launcher cell";
            ESP_Brookesia_AppLauncher launcher(phone, launcher_data);
            assert(launcher.begin(parent));
            for (size_t i = 0; i < names.size(); ++i) {
                ESP_Brookesia_AppLauncherIconInfo_t info{names[i].c_str(),
                    phone.getHome().getData().app_launcher.default_image, 1000 - static_cast<int>(i)};
                assert(launcher.addIcon(0, info));
            }
            auto *table = lv_obj_get_child(lv_obj_get_child(parent, 0), 0);
            auto *indicators = lv_obj_get_child(lv_obj_get_child(parent, 0), 1);
            std::vector<lv_obj_t *> objects;
            std::vector<int> ids;
            for (size_t i = 0; i < names.size(); ++i) {
                auto *grid = lv_obj_get_child(lv_obj_get_child(table, i / capacity), 0);
                objects.push_back(lv_obj_get_child(grid, i % capacity));
                ids.push_back(1000 - static_cast<int>(i));
            }
            auto check_launcher = [&] {
                lv_obj_update_layout(parent);
                const size_t pages = std::max(static_cast<size_t>(launcher_data.table.default_num),
                    (ids.size() + capacity - 1) / capacity);
                settle(); // Finish native page scrolling before checking input.
                assert(lv_obj_get_child_cnt(table) == pages);
                assert(lv_obj_get_child_cnt(indicators) == pages);
                assert(launcher.getActiveScreenIndex() < pages);
                size_t next = 0;
                for (size_t page = 0; page < pages; ++page) {
                    auto *grid = lv_obj_get_child(lv_obj_get_child(table, page), 0);
                    const size_t expected = std::min(capacity, ids.size() - next);
                    assert(lv_obj_get_child_cnt(grid) == expected);
                    for (size_t index = 0; index < expected; ++index, ++next) {
                        assert(lv_obj_get_child(grid, index) == objects[next]);
                        assert(lv_obj_is_valid(objects[next]));
                        auto *image = lv_obj_get_child(lv_obj_get_child(objects[next], 0), 0);
                        assert(lv_obj_has_flag(image, LV_OBJ_FLAG_CLICKABLE));
                        auto *caption = lv_obj_get_child(objects[next], 1);
                        assert(lv_label_get_long_mode(caption) == LV_LABEL_LONG_DOT);
                        assert(lv_obj_get_style_text_align(caption, LV_PART_MAIN) == LV_TEXT_ALIGN_CENTER);
                        const auto *font = lv_obj_get_style_text_font(caption, LV_PART_MAIN);
                        assert(lv_obj_get_height(caption) == lv_font_get_line_height(font));
                        lv_area_t cell_bounds, caption_bounds;
                        lv_obj_get_coords(objects[next], &cell_bounds);
                        lv_obj_get_coords(caption, &caption_bounds);
                        assert(caption_bounds.x1 >= cell_bounds.x1 && caption_bounds.x2 <= cell_bounds.x2);
                        assert(caption_bounds.y1 >= cell_bounds.y1 && caption_bounds.y2 <= cell_bounds.y2);
                        if (ids[next] == 999) assert(std::strcmp(lv_label_get_text(caption), "Clock") == 0);
                        if (ids[next] == 1000 - static_cast<int>(names.size() - 1))
                            assert(std::strstr(lv_label_get_text(caption), "...") != nullptr);
                    }
                }
                assert(next == ids.size());
            };
            check_launcher();
            assert(names.front() == "Action Guard Check");
            assert(names.back() == "Very long app title that must stay within its launcher cell");
            assert(launcher.scrollToPage(lv_obj_get_child_cnt(table) - 1));
            for (size_t position : {size_t(0), capacity - 1, capacity, size_t(1)}) {
                assert(position < ids.size());
                auto *removed = objects[position];
                assert(launcher.removeIcon(ids[position]));
                ids.erase(ids.begin() + position); objects.erase(objects.begin() + position);
                assert(!lv_obj_is_valid(removed));
                check_launcher();
            }
            ESP_Brookesia_AppLauncherIconInfo_t replacement{"Reinstalled fixture",
                phone.getHome().getData().app_launcher.default_image, 2000};
            assert(launcher.addIcon(0, replacement));
            auto *replacement_grid = lv_obj_get_child(lv_obj_get_child(table, ids.size() / capacity), 0);
            objects.push_back(lv_obj_get_child(replacement_grid, ids.size() % capacity));
            ids.push_back(2000);
            check_launcher();
            assert(launcher.removeIcon(2000));
            ids.pop_back(); objects.pop_back(); check_launcher();
            while (!ids.empty()) {
                assert(launcher.removeIcon(ids.back()));
                ids.pop_back(); objects.pop_back(); check_launcher();
            }
        }
        lv_obj_del(parent);
        std::puts("PASS: live launcher compacts first/middle/last removals across pages, preserves icon objects/order, removes empty pages and clamps indicators");
        std::puts("PASS: launcher captions use centered one-line ellipsis within cell bounds and preserve original app names");
    }
    static size_t removed_notifications = 0;
    crystal_registry_set_removed_hook([](size_t index) {
        assert(index == 1 && crystal_registry_installed_count() == 1);
        ++removed_notifications;
    });
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
        assert(removed_notifications == i + 1);
        assert(!fs::exists(root / "org.crystal.calculator"));
        uint32_t value = 0; assert(builtin_state.get_u32("keep", &value) && value == 99);
        for (const auto &kv : storage.values) assert(kv.first[0] != 'p' || kv.first == "pkg.registry");
    }
    std::puts("PASS: real Calculator package 10 install/uninstall cycles, 100 launches with production LVGL/Brookesia, zero tracked leaks, builtin state preserved");
    crystal_registry_set_removed_hook(nullptr);
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
    assert(install("declarative.capp") && crystal_registry_apply_changes());
    CrystalApp *declarative = installed_app("org.crystal.declarative-dashboard"); assert(declarative);
    ESP_Brookesia_CoreAppEventData_t declarative_start{declarative->getId(), ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START, nullptr};
    assert(phone.sendAppEvent(&declarative_start)); settle();
    assert(phone.getManager().getActiveApp() == declarative);
    assert(declarative->resources().report().objects == 1 && declarative->resources().report().timers == 1);
    assert(crystal_registry_set_slot("org.crystal.declarative-dashboard", 0));
    // Repeated launch requests from the control panel must be idempotent while
    // the target app is already foregrounded.
    assert(phone.sendAppEvent(&declarative_start)); settle();
    assert(phone.getManager().getActiveApp() == declarative);
    assert(declarative->resources().report().objects == 1 && declarative->resources().report().timers == 1);
    lv_obj_t *dashboard_tabs = find_object_type(lv_scr_act(), &lv_tabview_class);
    assert(dashboard_tabs);
    lv_obj_t *dashboard_root = lv_obj_get_parent(dashboard_tabs);
    const lv_area_t dashboard_area = declarative->getVisualArea();
    assert(lv_obj_get_width(dashboard_root) == lv_area_get_width(&dashboard_area));
    assert(lv_obj_get_height(dashboard_root) == lv_area_get_height(&dashboard_area));
    assert(lv_obj_get_width(dashboard_tabs) == lv_obj_get_content_width(dashboard_root));
    assert_inside(dashboard_tabs, dashboard_root);
    lv_obj_t *tab_pages = lv_tabview_get_content(dashboard_tabs);
    assert(lv_obj_get_child_cnt(tab_pages) == 3);
    assert_inside(find_display_label(display, "Network: connected"), lv_obj_get_child(tab_pages, 0));
    capture_dashboard(display, "dashboard-status.ppm");
    lv_tabview_set_act(dashboard_tabs, 1, LV_ANIM_OFF); settle();
    std::vector<lv_area_t> timer_button_boxes;
    for (const char *text : {"Start 5 min", "Pause", "Resume", "Reset"}) {
        lv_obj_t *label = find_display_label(display, text); assert(label);
        lv_obj_t *button = lv_obj_get_parent(label);
        assert(lv_obj_get_height(button) >= 44 && lv_obj_get_width(button) >= 44);
        assert_inside(label, button);
        assert_inside(button, lv_obj_get_child(tab_pages, 1));
        lv_area_t box{}; lv_obj_get_coords(button, &box);
        for (const auto &other : timer_button_boxes) {
            assert(box.x1 > other.x2 || box.x2 < other.x1 || box.y1 > other.y2 || box.y2 < other.y1);
        }
        timer_button_boxes.push_back(box);
    }
    capture_dashboard(display, "dashboard-timer.ppm");
    lv_tabview_set_act(dashboard_tabs, 2, LV_ANIM_OFF); settle();
    lv_obj_t *controls_page = lv_obj_get_child(tab_pages, 2);
    lv_obj_t *controls_scroll = lv_obj_get_child(controls_page, 0);
    assert_inside(controls_scroll, controls_page);
    assert(lv_obj_get_scroll_bottom(controls_scroll) > 0);
    capture_dashboard(display, "dashboard-controls.ppm");
    lv_obj_t *sample_input = find_object_type(controls_scroll, &lv_textarea_class);
    assert(sample_input);
    lv_obj_scroll_to_view(sample_input, LV_ANIM_OFF); settle();
    assert_inside(sample_input, controls_scroll);
    capture_dashboard(display, "dashboard-controls-scrolled.ppm");
    lv_tabview_set_act(dashboard_tabs, 0, LV_ANIM_OFF); settle();
    assert(find_display_label(display, "Network: connected") != nullptr);
    lv_obj_t *offline_hint = find_display_label(display, "Connect to Wi-Fi to bring services online.");
    assert(offline_hint != nullptr && lv_obj_has_flag(offline_hint, LV_OBJ_FLAG_HIDDEN));
    lv_obj_t *toggle = find_object_type(lv_disp_get_scr_act(display), &lv_switch_class);
    assert(toggle != nullptr && lv_obj_has_state(toggle, LV_STATE_CHECKED));
    lv_obj_t *matrix = find_object_type(lv_disp_get_scr_act(display), &lv_btnmatrix_class);
    assert(matrix != nullptr);
    assert(find_object_type(lv_disp_get_scr_act(display), &lv_img_class) != nullptr);
    assert(find_object_type(lv_disp_get_scr_act(display), &lv_list_class) != nullptr);
    assert(find_object_type(lv_disp_get_scr_act(display), &lv_chart_class) != nullptr);
    assert(find_object_type(lv_disp_get_scr_act(display), &lv_textarea_class) != nullptr);
    assert(find_object_type(lv_disp_get_scr_act(display), &lv_spinner_class) != nullptr);
    lv_obj_t *dialog = find_object_type(lv_disp_get_scr_act(display), &lv_msgbox_class);
    assert(dialog != nullptr && lv_obj_has_flag(dialog, LV_OBJ_FLAG_HIDDEN));
    assert(strcmp(lv_btnmatrix_get_btn_text(lv_msgbox_get_btns(dialog), 0), "OK") == 0);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_HIDDEN);
    lv_btnmatrix_set_selected_btn(lv_msgbox_get_btns(dialog), 0);
    lv_event_send(lv_msgbox_get_btns(dialog), LV_EVENT_VALUE_CHANGED, nullptr);
    settle();
    assert(!lv_obj_is_valid(dialog));
    click_button_text(display, "Start 5 min");
    assert(crystal_timer_state().running && crystal_timer_state().remaining_seconds == 300);
    for (unsigned i = 0; i < 110; ++i) { lv_tick_inc(10); lv_timer_handler(); }
    assert(find_display_label(display, "300 s remaining") != nullptr);
    click_button_text(display, "Pause");
    assert(crystal_timer_state().paused && !crystal_timer_state().running);
    click_button_text(display, "Resume");
    assert(crystal_timer_state().running && !crystal_timer_state().paused);
    click_button_text(display, "Reset");
    assert(!crystal_timer_state().running && !crystal_timer_state().paused);
    lv_btnmatrix_set_selected_btn(matrix, 0);
    lv_event_send(matrix, LV_EVENT_VALUE_CHANGED, nullptr);
    assert(!crystal_timer_state().running && !crystal_timer_state().paused);
    assert(declarative->state().set_u32("clear", 73));
    assert(phone.sendNavigateEvent(ESP_BROOKESIA_CORE_NAVIGATE_TYPE_HOME)); settle();
    assert(phone.getManager().getActiveApp() == nullptr);
    assert(phone.getManager().getRunningAppById(declarative->getId()) == declarative);
    assert(declarative->lifecycle_state() == CrystalApp::LifecycleState::Paused);
    assert(!declarative->clearData()); // A paused app still owns its UI.
    declarative_start.type = ESP_BROOKESIA_CORE_APP_EVENT_TYPE_STOP;
    assert(phone.sendAppEvent(&declarative_start)); settle();
    assert(phone.getManager().getRunningAppById(declarative->getId()) == nullptr);
    assert(crystal_app_verify_cleanup(declarative->resources(), &report));
    uint32_t cleared_value = 0;
    assert(declarative->clearData());
    assert(!declarative->state().get_u32("clear", &cleared_value));
    assert(declarative->clearData()); // Repeating the operation is harmless.
    std::puts("PASS: paused Dashboard rejects clear until STOP, then erases saved state and supports repeated clear");
    declarative_start.type = ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START;
    assert(phone.sendAppEvent(&declarative_start)); settle();
    declarative_start.type = ESP_BROOKESIA_CORE_APP_EVENT_TYPE_STOP;
    assert(phone.sendAppEvent(&declarative_start)); settle();
    assert(crystal_app_verify_cleanup(declarative->resources(), &report));
    assert(crystal_registry_uninstall_package("org.crystal.declarative-dashboard")); settle();
    assert(installed_app("org.crystal.declarative-dashboard") == nullptr && crystal_packages().empty());
    assert(install("declarative-denied.capp") && crystal_registry_apply_changes());
    CrystalApp *denied = installed_app("org.crystal.declarative-denied"); assert(denied);
    ESP_Brookesia_CoreAppEventData_t denied_start{denied->getId(), ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START, nullptr};
    assert(phone.sendAppEvent(&denied_start)); settle();
    assert(phone.getManager().getActiveApp() == denied);
    click_button_text(display, "Start 5 min");
    assert(!crystal_timer_state().running && !crystal_timer_state().paused);
    denied_start.type = ESP_BROOKESIA_CORE_APP_EVENT_TYPE_STOP;
    assert(phone.sendAppEvent(&denied_start)); settle();
    assert(crystal_app_verify_cleanup(denied->resources(), &report));
    assert(crystal_registry_uninstall_package("org.crystal.declarative-denied")); settle();
    assert(installed_app("org.crystal.declarative-denied") == nullptr && crystal_packages().empty());
    std::puts("PASS: declarative package fills the app area, lays out tabs/buttons/scrolling without overlap, binds live data/visibility, routes permitted timer events, and cleans up across reopen");
    struct VisualCase { const char *file; const char *id; const char *label; const lv_obj_class_t *widget; };
    const VisualCase visual_cases[] = {
        {"declarative-clock.capp", "org.crystal.visual-clock", "Hong Kong (UTC+08:00)", &lv_tabview_class},
        {"declarative-weather.capp", "org.crystal.visual-weather", "24 C", &lv_img_class},
        {"declarative-calculator.capp", "org.crystal.visual-calculator", "45 x 6 - 2", &lv_btnmatrix_class}
    };
    for (const auto &visual : visual_cases) {
        assert(install(visual.file) && crystal_registry_apply_changes());
        CrystalApp *visual_app = installed_app(visual.id); assert(visual_app);
        ESP_Brookesia_CoreAppEventData_t visual_start{visual_app->getId(), ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START, nullptr};
        assert(phone.sendAppEvent(&visual_start)); settle();
        assert(phone.getManager().getActiveApp() == visual_app);
        assert(find_display_label(display, visual.label) != nullptr);
        assert(find_object_type(lv_disp_get_scr_act(display), visual.widget) != nullptr);
        visual_start.type = ESP_BROOKESIA_CORE_APP_EVENT_TYPE_STOP;
        assert(phone.sendAppEvent(&visual_start)); settle();
        assert(crystal_app_verify_cleanup(visual_app->resources(), &report));
        assert(crystal_registry_uninstall_package(visual.id)); settle();
        assert(installed_app(visual.id) == nullptr && crystal_packages().empty());
    }
    std::puts("PASS: Clock, Weather and Calculator visual references build, launch, and clean up using production LVGL");
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
