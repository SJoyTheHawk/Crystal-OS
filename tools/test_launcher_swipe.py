#!/usr/bin/env python3
"""Exercise the production Brookesia pager with real LVGL pointer input.

Requires CMake, a C++ compiler, and managed_components/lvgl__lvgl.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CONF = '''
#pragma once
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_USE_SNAPSHOT 1
#define LV_USE_LOG 0
'''
CHECK = r'''
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>
#include "systems/phone/esp_brookesia_phone.hpp"

static lv_point_t point{};
static lv_indev_state_t state = LV_INDEV_STATE_RELEASED;
static unsigned launches = 0;
static void read_touch(lv_indev_drv_t *, lv_indev_data_t *data) {
    data->point = point;
    data->state = state;
}
static void flush(lv_disp_drv_t *driver, const lv_area_t *, lv_color_t *) {
    lv_disp_flush_ready(driver);
}
class App : public ESP_Brookesia_PhoneApp {
public:
    App() : ESP_Brookesia_PhoneApp("Test", nullptr, true) {}
    bool run() override { ++launches; return true; }
    bool back() override { return true; }
};
static lv_obj_t *find_pager(lv_obj_t *root) {
    if (lv_obj_get_scroll_snap_x(root) == LV_SCROLL_SNAP_CENTER &&
        lv_obj_get_scroll_dir(root) == LV_DIR_HOR) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) {
        if (auto *found = find_pager(lv_obj_get_child(root, i))) return found;
    }
    return nullptr;
}
static lv_obj_t *icon(lv_obj_t *pager, int page) {
    auto *grid = lv_obj_get_child(lv_obj_get_child(pager, page), 0);
    auto *tile = lv_obj_get_child(grid, 0);
    return lv_obj_get_child(lv_obj_get_child(tile, 0), 0);
}
int main() {
    std::setbuf(stdout, nullptr);
    lv_init();
    static lv_color_t pixels[480 * 20];
    static lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer, pixels, nullptr, 480 * 20);
    static lv_disp_drv_t display_driver;
    lv_disp_drv_init(&display_driver);
    display_driver.hor_res = display_driver.ver_res = 480;
    display_driver.draw_buf = &buffer;
    display_driver.flush_cb = flush;
    auto *display = lv_disp_drv_register(&display_driver);
    static lv_indev_drv_t input;
    lv_indev_drv_init(&input);
    input.type = LV_INDEV_TYPE_POINTER;
    input.read_cb = read_touch;
    auto *pointer = lv_indev_drv_register(&input);
    lv_timer_pause(pointer->driver->read_timer);
    auto advance = [](int ms) {
        for (int elapsed = 0; elapsed < ms; elapsed += 10) {
            lv_tick_inc(10);
            lv_timer_handler();
        }
    };
    auto touch = [&](int x, int y, bool down, int ms = 20) {
        point = {static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y)};
        state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        lv_indev_read_timer_cb(pointer->driver->read_timer);
        advance(ms);
    };
    auto drag = [&](int x1, int x2, int y, int step = 12) {
        touch(x1, y, true);
        const int dir = x2 > x1 ? 1 : -1;
        for (int x = x1 + dir * step; dir * (x2 - x) > 0; x += dir * step) {
            touch(x, y, true);
        }
        touch(x2, y, true);
    };
    std::vector<std::unique_ptr<App>> apps;
    ESP_Brookesia_Phone phone(display);
    ESP_Brookesia_PhoneStylesheet_t sheet = ESP_BROOKESIA_PHONE_480_480_DARK_STYLESHEET();
    sheet.home.flags.enable_recents_screen = 0;
    sheet.home.app_launcher.data.flags.enable_table_scroll_anim = 1;
    sheet.manager.gesture.threshold.direction_horizon = 12;
    sheet.manager.gesture.threshold.direction_vertical = 12;
    sheet.manager.gesture_mask_indicator_trigger_time_ms = UINT32_MAX;
    sheet.manager.gesture.flags.enable_indicator_bars[ESP_BROOKESIA_GESTURE_INDICATOR_BAR_TYPE_BOTTOM] = 0;
    assert(phone.setTouchDevice(pointer));
    assert(phone.addStylesheet(sheet));
    assert(phone.activateStylesheet(sheet));
    assert(phone.begin());
    for (int i = 0; i < 9; ++i) {
        apps.emplace_back(new App);
        assert(phone.installApp(apps.back().get()) >= 0);
    }
    assert(phone.sendNavigateEvent(ESP_BROOKESIA_CORE_NAVIGATE_TYPE_HOME));
    advance(400);
    auto *launcher = phone.getHome().getAppLauncher();
    auto *pager = find_pager(phone.getCoreHome().getMainScreenObject());
    assert(pager && lv_obj_get_child_cnt(pager) == 3);
    auto *indicator = lv_obj_get_child(lv_obj_get_parent(pager), 1);
    auto check = [&](int page) {
        advance(400);
        assert(launcher->getActiveScreenIndex() == page);
        assert(lv_obj_get_scroll_x(pager) == page * 480);
        for (int i = 0; i < 3; ++i) {
            assert(lv_obj_has_state(lv_obj_get_child(indicator, i), LV_STATE_USER_1) == (i == page));
        }
        assert(launches == 0);
        assert(phone.getManager().getActiveApp() == nullptr);
        assert(!phone.getManager().getGesture()->checkMaskVisible());
    };
    check(0);
    // A middle-of-screen drag moves both real pages while still held.
    drag(360, 90, 210);
    assert(lv_indev_get_scroll_obj(pointer) == pager);
    const int offset = lv_obj_get_scroll_x(pager);
    assert(offset > 0 && offset < 480);
    lv_area_t outgoing, incoming;
    lv_obj_get_coords(lv_obj_get_child(pager, 0), &outgoing);
    lv_obj_get_coords(lv_obj_get_child(pager, 1), &incoming);
    assert(outgoing.x1 < 0 && outgoing.x2 >= 0);
    assert(incoming.x1 > 0 && incoming.x1 < 480);
    touch(90, 210, false);
    assert(lv_anim_get(pager, nullptr) != nullptr);
    check(1);
    // Swipe on the page-indicator band, then back from the left edge.
    drag(360, 70, 435);
    touch(70, 435, false);
    check(2);
    drag(3, 310, 210);
    touch(310, 210, false);
    check(1);
    // Short slow drags cancel; short flicks advance one page.
    drag(300, 240, 210);
    for (int i = 0; i < 12; ++i) touch(240, 210, true);
    touch(240, 210, false);
    assert(lv_anim_get(pager, nullptr) != nullptr);
    check(1);
    drag(300, 220, 210, 40);
    touch(220, 210, false);
    check(2);
    // Outward swipes stop at either endpoint.
    drag(360, 80, 210);
    touch(80, 210, false);
    check(2);
    assert(launcher->scrollToPage(0));
    check(0);
    drag(100, 390, 210);
    touch(390, 210, false);
    check(0);
    // A swipe starting on an icon must scroll, not launch that app.
    lv_area_t bounds;
    lv_obj_get_coords(icon(pager, 0), &bounds);
    const int ix = (bounds.x1 + bounds.x2) / 2;
    const int iy = (bounds.y1 + bounds.y2) / 2;
    drag(ix, ix - 80, iy, 40);
    touch(ix - 80, iy, false);
    check(1);
    // Reversing direction and dragging vertically do not advance a page.
    drag(300, 220, 210);
    for (int x = 230; x <= 300; x += 10) touch(x, 210, true);
    for (int i = 0; i < 12; ++i) touch(300, 210, true);
    touch(300, 210, false);
    check(1);
    touch(240, 170, true);
    for (int y = 180; y <= 320; y += 10) touch(240, y, true);
    touch(240, 320, false);
    check(1);
    // A second gesture can reverse an in-flight snap without leaving a
    // fractional page offset or permanently disabling its icons.
    drag(350, 70, 210);
    touch(70, 210, false);
    advance(40);
    touch(80, 210, true);
    touch(104, 210, true);
    assert(lv_indev_get_scroll_obj(pointer) == pager);
    assert(lv_anim_get(pager, nullptr) == nullptr);
    for (int x = 116; x < 390; x += 12) touch(x, 210, true);
    touch(390, 210, true);
    touch(390, 210, false);
    check(1);
    // Taps begun during a programmatic settle stay cancelled after it ends.
    assert(launcher->scrollToPage(2));
    auto *target = icon(pager, 2);
    lv_event_send(target, LV_EVENT_PRESSED, pointer);
    advance(400);
    lv_event_send(target, LV_EVENT_RELEASED, pointer);
    lv_event_send(target, LV_EVENT_CLICKED, pointer);
    check(2);
    // A fresh tap after settling still opens its app exactly once.
    lv_obj_get_coords(target, &bounds);
    touch((bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2, true);
    touch((bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2, false);
    advance(400);
    assert(launches == 1 && phone.getManager().getActiveApp() != nullptr);
    assert(phone.sendNavigateEvent(ESP_BROOKESIA_CORE_NAVIGATE_TYPE_HOME));
    advance(400);
    assert(launcher->getActiveScreenIndex() == 2);
    assert(launcher->scrollToPage(0));
    advance(40);
    // Apps must be uninstalled while the phone's home objects still exist.
    for (auto &app : apps) assert(phone.uninstallApp(app.get()));
    apps.clear();
    assert(phone.del()); // teardown with a page animation in flight
    advance(400);
    std::puts("PASS: real-page wipe, centre/edge/icon/dot swipes, snap animation, flick, cancel, reverse, interruption, endpoints, vertical rejection, click suppression, app return, teardown");
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix='crystal-launcher-') as directory:
        temp = Path(directory)
        (temp / 'lv_conf.h').write_text(CONF)
        (temp / 'check.cpp').write_text(CHECK)
        lvgl = ROOT / 'managed_components/lvgl__lvgl'
        brookesia = ROOT / 'components/esp-brookesia/src'
        (temp / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.16)
project(launcher_test LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(LV_CONF_PATH "{temp}/lv_conf.h" CACHE STRING "" FORCE)
add_subdirectory("{lvgl}" lvgl)
file(GLOB_RECURSE BROOKESIA_SOURCES
    "{brookesia}/core/*.cpp" "{brookesia}/core/*.c"
    "{brookesia}/widgets/*.cpp" "{brookesia}/systems/phone/*.cpp"
    "{brookesia}/assets/*.c")
add_executable(check check.cpp {chr(36)}{{BROOKESIA_SOURCES}})
target_include_directories(check PRIVATE "{brookesia}" "{temp}")
target_compile_definitions(check PRIVATE ESP_BROOKESIA_KCONFIG_IGNORE ESP_BROOKESIA_CONF_SKIP
    ESP_BROOKESIA_SQUARELINE_USE_INTERNAL_UI_HELPERS=1 ESP_BROOKESIA_SQ1_4_1_LV8_3_11)
target_compile_options(check PRIVATE -g -fsanitize=address -Wno-c99-designator -Wno-reorder-init-list)
target_link_options(check PRIVATE -fsanitize=address)
target_link_libraries(check PRIVATE lvgl)
''')
        subprocess.run(['cmake', '-S', str(temp), '-B', str(temp / 'build')], check=True)
        subprocess.run(['cmake', '--build', str(temp / 'build'), '--target', 'check', '-j', '8'], check=True)
        subprocess.run([str(temp / 'build/check')], check=True, cwd=temp)

if __name__ == '__main__':
    main()
