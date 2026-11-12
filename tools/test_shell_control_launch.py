#!/usr/bin/env python3
"""Exercise production shell launch/return functions with a deterministic UI fixture.

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
#include <cstring>
#include <vector>
using namespace std;
struct lv_obj_t {};
struct CrystalApp {
    int id;
    int getId() const { return id; }
    bool clearData() { return true; }
};
struct Manager {
    CrystalApp *active = nullptr;
    CrystalApp *getActiveApp() { return active; }
};
enum { ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START, ESP_BROOKESIA_CORE_APP_EVENT_TYPE_STOP };
struct ESP_Brookesia_CoreAppEventData_t { int id; int type; void *data; };
enum { CRYSTAL_CONTROL_ACTION_LAUNCH, CRYSTAL_CONTROL_ACTION_UNINSTALL, CRYSTAL_CONTROL_ACTION_CLEAR_DATA };
struct CrystalControlAction { int kind; char id[80]; };
struct Entry { const char *id; CrystalApp *app; };
static vector<Entry> installed;
static CrystalApp first{10}, dashboard{20}, newly_installed{30};
struct Phone {
    Manager manager;
    bool send_failure = false, start_failure = false;
    unsigned starts = 0;
    Manager &getManager() { return manager; }
    bool sendAppEvent(const ESP_Brookesia_CoreAppEventData_t *event) {
        ++starts;
        if (send_failure) return false;
        if (event->type == ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START && !start_failure) {
            for (const auto &item : installed) if (item.app->id == event->id) manager.active = item.app;
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
size_t crystal_registry_installed_count() { return installed.size(); }
const char *crystal_registry_installed_id(size_t i) { return i < installed.size() ? installed[i].id : nullptr; }
CrystalApp *crystal_registry_installed_app(size_t i) { return i < installed.size() ? installed[i].app : nullptr; }
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
void update_page_dots() {}
void update_home_pill() {}
bool persist_current_card() { persisted_index = s_current_index; return true; }
void system_page_pop() { assert(s_system_page_depth); --s_system_page_depth; ++popped_pages; }
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
'''

CHECK = r'''
static void reset(size_t depth, int return_index = 0) {
    installed = {{"clock", &first}, {"org.crystal.declarative-dashboard", &dashboard}};
    phone = Phone{}; phone.manager.active = depth ? nullptr : &first;
    s_current_index = 0; s_system_page_depth = depth;
    s_last_app_before_settings = return_index; persisted_index = SIZE_MAX;
    popped_pages = 0; install_on_apply = false;
}
static void launch(const char *id) {
    CrystalControlAction action{}; action.kind = CRYSTAL_CONTROL_ACTION_LAUNCH;
    snprintf(action.id, sizeof(action.id), "%s", id);
    crystal_shell_control_action_event(&action);
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
}
'''


def main():
    source = SOURCE.read_text()
    functions = '\n'.join(function(source, signature) for signature in (
        'bool start_card(size_t index, bool animate)',
        'void close_settings_and_restore_app()',
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
