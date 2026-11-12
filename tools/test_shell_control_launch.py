#!/usr/bin/env python3
"""Exercise production shell launch/clear functions with a deterministic UI fixture.

Checks shell selection bookkeeping, not LVGL rendering or real event delivery.
Run alongside the platform integration suite and the hardware Settings test.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'components/crystal_shell/src/crystal_shell.cpp'


def function(source, signature):
    start = source.index(signature + '\n{')
    return source[start:source.index('\n}\n', start) + 3]


FIXTURE = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
using namespace std;
struct lv_obj_t {};
static unsigned clear_attempts = 0, temporary_clears = 0, temporary_destroys = 0;
static unsigned missing_running_lookups = 0;
struct CrystalApp {
    int id;
    bool running = false, fail_clear = false, temporary = false;
    unsigned saved_value = 7;
    ~CrystalApp() { if (temporary) ++temporary_destroys; }
    int getId() const { return id; }
    bool clearData() {
        ++clear_attempts;
        if (running || fail_clear) return false;
        saved_value = 0;
        if (temporary) ++temporary_clears;
        return true;
    }
};
struct Manager {
    CrystalApp *active = nullptr;
    CrystalApp *getActiveApp() { return active; }
    CrystalApp *getRunningAppById(int id);
    uint8_t getRunningAppCount() const;
    CrystalApp *getRunningAppByIdenx(uint8_t index);
};
enum { ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START, ESP_BROOKESIA_CORE_APP_EVENT_TYPE_STOP };
struct ESP_Brookesia_CoreAppEventData_t { int id; int type; void *data; };
enum { CRYSTAL_CONTROL_ACTION_LAUNCH, CRYSTAL_CONTROL_ACTION_UNINSTALL, CRYSTAL_CONTROL_ACTION_CLEAR_DATA };
struct CrystalControlAction { int kind; char id[80]; };
struct Entry { const char *id; CrystalApp *app; };
static vector<Entry> installed;
struct CrystalAppEntry { const char *id; bool available = true; };
static vector<CrystalAppEntry> catalog;
static bool fail_factory = false;
static CrystalApp first{10}, dashboard{20}, newly_installed{30};
CrystalApp *Manager::getRunningAppById(int id) {
    for (const auto &entry : installed)
        if (entry.app->id == id && entry.app->running) return entry.app;
    ++missing_running_lookups;
    return nullptr;
}
uint8_t Manager::getRunningAppCount() const {
    uint8_t count = 0;
    for (const auto &entry : installed) if (entry.app->running) ++count;
    return count;
}
CrystalApp *Manager::getRunningAppByIdenx(uint8_t index) {
    for (const auto &entry : installed) {
        if (entry.app->running && index-- == 0) return entry.app;
    }
    return nullptr;
}
struct Phone {
    Manager manager;
    bool send_failure = false, start_failure = false, stop_failure = false;
    unsigned starts = 0, stops = 0;
    Manager &getManager() { return manager; }
    bool sendAppEvent(const ESP_Brookesia_CoreAppEventData_t *event) {
        if (event->type == ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START) ++starts;
        else ++stops;
        if (send_failure) return false;
        if (event->type == ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START && !start_failure) {
            for (const auto &item : installed) if (item.app->id == event->id) {
                manager.active = item.app;
                item.app->running = true;
            }
        }
        if (event->type == ESP_BROOKESIA_CORE_APP_EVENT_TYPE_STOP && !stop_failure) {
            for (const auto &item : installed) if (item.app->id == event->id) {
                // Teardown may persist old state; clearing must happen later.
                item.app->saved_value = 9;
                item.app->running = false;
                if (manager.active == item.app) manager.active = nullptr;
            }
        }
        return true;
    }
};
static Phone phone;
static Phone *s_phone = &phone;
static size_t s_current_index = 0, s_system_page_depth = 0, persisted_index = SIZE_MAX;
static int s_last_app_before_settings = -1;
static bool s_switching = false, install_on_apply = false;
static unsigned popped_pages = 0;
static size_t displayed_count = 0, highlighted_index = SIZE_MAX;
size_t crystal_registry_installed_count() { return installed.size(); }
const char *crystal_registry_installed_id(size_t i) { return i < installed.size() ? installed[i].id : nullptr; }
CrystalApp *crystal_registry_installed_app(size_t i) { return i < installed.size() ? installed[i].app : nullptr; }
size_t crystal_registry_catalog_count() { return catalog.size(); }
const CrystalAppEntry *crystal_registry_catalog_entry(size_t i) { return i < catalog.size() ? &catalog[i] : nullptr; }
CrystalApp *crystal_registry_create_app(const CrystalAppEntry *) {
    if (fail_factory) return nullptr;
    auto *app = new CrystalApp{99}; app->temporary = true; return app;
}
bool crystal_registry_apply_changes() {
    if (install_on_apply) { installed.push_back({"new.app", &newly_installed}); install_on_apply = false; }
    return true;
}
bool crystal_registry_uninstall_package(const char *) { return false; }
void crystal_shell_front_layer_changed() {}
lv_obj_t *show_transition_cover() { return nullptr; }
lv_obj_t *show_snapshot_transition() { return nullptr; }
void finish_snapshot_transition(lv_obj_t *) {}
void lv_obj_del(lv_obj_t *) {}
void update_page_dots() { displayed_count = installed.size(); highlighted_index = s_current_index; }
void update_home_pill() {}
bool persist_current_card() { persisted_index = s_current_index; return true; }
void system_page_pop() { assert(s_system_page_depth); --s_system_page_depth; ++popped_pages; }
static const char *TAG = "shell-test";
static string last_log;
static void shell_log(const char *, const char *format, ...) {
    char message[256]; va_list args; va_start(args, format);
    vsnprintf(message, sizeof(message), format, args); va_end(args);
    last_log = message;
}
#define ESP_LOGI shell_log
#define ESP_LOGW shell_log
#define ESP_LOGE shell_log
'''

CHECK = r'''
static void reset(size_t depth, int return_index = 0) {
    first = CrystalApp{10}; dashboard = CrystalApp{20}; newly_installed = CrystalApp{30};
    installed = {{"clock", &first}, {"org.crystal.declarative-dashboard", &dashboard}};
    catalog = {{"clock"}, {"org.crystal.declarative-dashboard"}, {"disabled.app"}};
    phone = Phone{}; phone.manager.active = depth ? nullptr : &first;
    first.running = true;
    s_current_index = 0; s_system_page_depth = depth;
    s_last_app_before_settings = return_index; persisted_index = SIZE_MAX;
    popped_pages = 0; install_on_apply = false;
    clear_attempts = temporary_clears = temporary_destroys = 0;
    missing_running_lookups = 0;
    displayed_count = installed.size(); highlighted_index = s_current_index;
    fail_factory = false; last_log.clear();
}
static void launch(const char *id) {
    CrystalControlAction action{}; action.kind = CRYSTAL_CONTROL_ACTION_LAUNCH;
    snprintf(action.id, sizeof(action.id), "%s", id);
    crystal_shell_control_action_event(&action);
}
static void clear_data(const char *id) {
    CrystalControlAction action{}; action.kind = CRYSTAL_CONTROL_ACTION_CLEAR_DATA;
    snprintf(action.id, sizeof(action.id), "%s", id);
    crystal_shell_control_action_event(&action);
    assert(missing_running_lookups == 0); // Expected absence must not log an error.
}
int main() {
    for (size_t depth : {size_t(1), size_t(3)}) {
        reset(depth);
        launch("org.crystal.declarative-dashboard");
        assert(phone.manager.active == &dashboard);
        assert(s_current_index == 1 && persisted_index == 1);
        assert(s_system_page_depth == depth && popped_pages == 0);
        assert(s_last_app_before_settings == 1);
        // Repeating Launch must keep the selection and the open Settings pages.
        launch("org.crystal.declarative-dashboard");
        assert(phone.manager.active == &dashboard && s_last_app_before_settings == 1);
        close_settings_and_restore_app();
        assert(phone.manager.active == &dashboard && s_current_index == 1);
        assert(s_system_page_depth == 0 && popped_pages == depth);
        assert(s_last_app_before_settings == -1);
    }
    reset(2, -1); // Settings opened from the launcher.
    launch("org.crystal.declarative-dashboard");
    close_settings_and_restore_app();
    assert(phone.manager.active == &dashboard && s_current_index == 1);
    reset(0);
    launch("org.crystal.declarative-dashboard");
    assert(phone.manager.active == &dashboard && persisted_index == 1);
    reset(2);
    install_on_apply = true;
    launch("new.app");
    assert(phone.manager.active == &newly_installed && s_last_app_before_settings == 2);
    close_settings_and_restore_app();
    assert(phone.manager.active == &newly_installed);
    for (bool send_failure : {false, true}) {
        reset(2); phone.send_failure = send_failure; phone.start_failure = !send_failure;
        launch("org.crystal.declarative-dashboard");
        assert(s_system_page_depth == 2 && s_last_app_before_settings == 0);
        assert(s_current_index == 0 && persisted_index == SIZE_MAX);
    }
    reset(2); launch("missing.app");
    assert(phone.starts == 0 && s_last_app_before_settings == 0 && s_system_page_depth == 2);
    crystal_shell_control_action_event(nullptr);
    puts("PASS: remote launch behind root/nested Settings, return target, repeated launch, launcher, new install, and failure preservation");
    for (size_t depth : {size_t(0), size_t(1), size_t(3)}) {
        reset(depth, 1);
        dashboard.running = true;
        phone.manager.active = depth ? nullptr : &dashboard;
        clear_data("org.crystal.declarative-dashboard");
        assert(phone.stops == 1 && clear_attempts == 1 && !dashboard.running);
        assert(dashboard.saved_value == 0 && phone.manager.active == nullptr);
        assert(first.running); // Clearing Dashboard leaves other apps alone.
        assert(s_system_page_depth == depth && popped_pages == 0);
        assert(last_log == "Control-panel clear data succeeded for org.crystal.declarative-dashboard");
        clear_data("org.crystal.declarative-dashboard");
        assert(phone.stops == 1 && clear_attempts == 2 && dashboard.saved_value == 0);
        launch("org.crystal.declarative-dashboard");
        assert(phone.manager.active == &dashboard && dashboard.running);
        if (depth) {
            close_settings_and_restore_app();
            assert(s_system_page_depth == 0 && phone.manager.active == &dashboard);
        }
    }
    for (bool send_failure : {false, true}) {
        reset(2); dashboard.running = true;
        phone.send_failure = send_failure; phone.stop_failure = !send_failure;
        clear_data("org.crystal.declarative-dashboard");
        assert(phone.stops == 1 && clear_attempts == 0 && dashboard.saved_value == 7);
        assert(last_log == "Control-panel clear data failed for org.crystal.declarative-dashboard");
    }
    reset(2); dashboard.fail_clear = true;
    clear_data("org.crystal.declarative-dashboard");
    assert(clear_attempts == 1 && dashboard.saved_value == 7);
    assert(last_log == "Control-panel clear data failed for org.crystal.declarative-dashboard");
    dashboard.fail_clear = false; clear_data("org.crystal.declarative-dashboard");
    assert(clear_attempts == 2 && dashboard.saved_value == 0);
    reset(2); clear_data("disabled.app");
    assert(phone.stops == 0 && clear_attempts == 1 && temporary_clears == 1 && temporary_destroys == 1);
    reset(2); fail_factory = true; clear_data("disabled.app");
    assert(clear_attempts == 0 && temporary_destroys == 0);
    reset(2); clear_data("missing.app");
    assert(phone.stops == 0 && clear_attempts == 0 && temporary_destroys == 0);
    catalog[1].available = false; clear_data("org.crystal.declarative-dashboard");
    assert(clear_attempts == 0);
    puts("PASS: clear data stops foreground/paused apps before erasing, verifies STOP, handles disabled apps, failure/retry, repeated clears and relaunch");
    for (size_t current = 0; current < 3; ++current) {
        for (size_t removed = 0; removed < 3; ++removed) {
            for (int return_index : {-1, 0, 1, 2}) {
                reset(2, return_index);
                installed.push_back({"new.app", &newly_installed});
                s_current_index = current;
                installed.erase(installed.begin() + removed);
                on_installed_app_removed(removed);
                const size_t shifted = current > removed ? current - 1 : current;
                assert(s_current_index == (shifted >= installed.size() ? installed.size() - 1 : shifted));
                const int expected_return = return_index == static_cast<int>(removed) ? -1 :
                    return_index > static_cast<int>(removed) ? return_index - 1 : return_index;
                assert(s_last_app_before_settings == expected_return);
                assert(displayed_count == 2 && highlighted_index == s_current_index);
                assert(persisted_index == s_current_index && s_system_page_depth == 2);
                if (expected_return >= 0) {
                    close_settings_and_restore_app();
                    assert(phone.manager.active == installed[expected_return].app);
                }
            }
        }
    }
    reset(0); installed.clear(); on_installed_app_removed(0);
    assert(s_current_index == 0 && displayed_count == 0 && persisted_index == SIZE_MAX);
    puts("PASS: removal refreshes card count and selection, shifts Settings return indices, and clears removed return targets");
}
'''


def main():
    source = SOURCE.read_text()
    functions = '\n'.join(function(source, signature) for signature in (
        'bool start_card(size_t index, bool animate)',
        'void close_settings_and_restore_app()',
        'bool manage_apps_clear_data(const CrystalAppEntry *entry)',
        'void on_installed_app_removed(size_t removed_index)',
        'void crystal_shell_control_action_event(const CrystalControlAction *action)',
    ))
    with tempfile.TemporaryDirectory(prefix='crystal-shell-launch-') as directory:
        work = Path(directory)
        (work / 'check.cpp').write_text(FIXTURE + functions + CHECK)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-g',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        str(work / 'check.cpp'), '-o', str(work / 'check')], check=True)
        subprocess.run([str(work / 'check')], check=True,
                       env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1'))


if __name__ == '__main__':
    main()
