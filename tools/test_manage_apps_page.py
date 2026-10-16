#!/usr/bin/env python3
"""Test production Manage Apps with real LVGL; optional --output-dir saves renders.

The shell header is a fixture. Device checks cover actual navigation.
Requires CMake, a C++ compiler, and the checked-out managed LVGL dependency.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
CONF = '''
#pragma once
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_USE_SNAPSHOT 1
#define LV_USE_LOG 0
'''
CHECK = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <map>
#include <vector>
#include "lvgl.h"
#include "manage_apps_page.h"
#include "crystal_registry.hpp"
static std::vector<CrystalAppEntry> catalog;
static bool overrides = false;
static bool fail_write = false;
static unsigned writes = 0;
static std::map<std::string, bool> saved;
static std::map<std::string, uint16_t> slots;
static unsigned slot_writes = 0;
static unsigned fail_slot_at = 0;
static bool fail_rollback = false;
static lv_point_t touch_point{};
static lv_indev_state_t touch_state = LV_INDEV_STATE_RELEASED;
static void read_touch(lv_indev_drv_t *, lv_indev_data_t *data) {
    data->point = touch_point;
    data->state = touch_state;
}
bool crystal_registry_set_slot(const char *id, uint16_t slot) {
    ++slot_writes;
    if (slot_writes == fail_slot_at || (fail_rollback && slot_writes > fail_slot_at)) return false;
    slots[id] = slot;
    return true;
}
size_t crystal_registry_catalog_count() { return catalog.size(); }
const CrystalAppEntry *crystal_registry_catalog_entry(size_t i) {
    return i < catalog.size() ? &catalog[i] : nullptr;
}
bool crystal_registry_enabled(const char *id, bool fallback) {
    auto found = saved.find(id);
    if (found != saved.end()) return found->second;
    return overrides && std::strcmp(id, "weather") == 0 ? false : fallback;
}
bool crystal_registry_set_enabled(const char *id, bool enabled) {
    ++writes;
    if (fail_write) return false;
    saved[id] = enabled;
    return true;
}
uint16_t crystal_registry_slot(const char *id, uint16_t fallback) {
    auto found = slots.find(id);
    if (found != slots.end()) return found->second;
    return overrides && std::strcmp(id, "calculator") == 0 ? 0 : fallback;
}
static void flush(lv_disp_drv_t *driver, const lv_area_t *, lv_color_t *) {
    lv_disp_flush_ready(driver);
}
static const char *label(lv_obj_t *parent, int i) {
    auto *child = lv_obj_get_child(parent, i);
    assert(child && lv_obj_check_type(child, &lv_label_class));
    return lv_label_get_text(child);
}
static void check_row(lv_obj_t *content, int i, const char *id, bool enabled) {
    auto *row = lv_obj_get_child(content, i + 1);
    assert(row && lv_obj_get_child_cnt(row) == (enabled ? 4u : 3u));
    assert(std::strcmp(label(row, 0), LV_SYMBOL_BARS) == 0);
    assert(std::strcmp(label(row, 1), id) == 0);
    auto *button = lv_obj_get_child(row, 2);
    assert(std::strcmp(label(button, 0), enabled ? "Installed" : "Install") == 0);
    assert(!lv_obj_has_state(button, LV_STATE_DISABLED));
    if (enabled) {
        auto *clear = lv_obj_get_child(row, 3);
        assert(std::strcmp(label(clear, 0), "Clear") == 0);
        assert(lv_obj_has_state(clear, LV_STATE_DISABLED));
    }
    lv_area_t bounds;
    lv_obj_get_coords(row, &bounds);
    int last_right = bounds.x1;
    for (uint32_t c = 0; c < lv_obj_get_child_cnt(row); ++c) {
        lv_area_t box;
        lv_obj_get_coords(lv_obj_get_child(row, c), &box);
        assert(box.x1 >= last_right && box.x2 <= bounds.x2);
        assert(box.y1 >= bounds.y1 && box.y2 <= bounds.y2);
        last_right = box.x2;
    }
    assert(lv_obj_get_width(lv_obj_get_child(row, 1)) >= 120);
}
static void snapshot(lv_obj_t *page, const char *name) {
    auto *snap = lv_snapshot_take(page, LV_IMG_CF_TRUE_COLOR);
    assert(snap);
    FILE *file = std::fopen(name, "wb");
    assert(file);
    std::fprintf(file, "P6\n%u %u\n255\n", snap->header.w, snap->header.h);
    auto *pixels = reinterpret_cast<const lv_color_t *>(snap->data);
    for (size_t i = 0; i < static_cast<size_t>(snap->header.w) * snap->header.h; ++i) {
        lv_color32_t color;
        color.full = lv_color_to32(pixels[i]);
        unsigned char rgb[] = {color.ch.red, color.ch.green, color.ch.blue};
        std::fwrite(rgb, 1, 3, file);
    }
    std::fclose(file);
    lv_snapshot_free(snap);
}
int main() {
    lv_init();
    static lv_color_t pixels[480 * 20];
    static lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw, pixels, nullptr, 480 * 20);
    static lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = 480; driver.ver_res = 480;
    driver.draw_buf = &draw; driver.flush_cb = flush;
    lv_disp_drv_register(&driver);
    static lv_indev_drv_t input;
    lv_indev_drv_init(&input);
    input.type = LV_INDEV_TYPE_POINTER;
    input.read_cb = read_touch;
    auto *pointer = lv_indev_drv_register(&input);
    auto touch = [&](int x, int y, lv_indev_state_t state) {
        touch_point = {static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y)};
        touch_state = state;
        lv_tick_inc(20);
        lv_indev_read_timer_cb(pointer->driver->read_timer);
    };
    auto *page = lv_obj_create(lv_scr_act());
    lv_obj_set_size(page, 480, 480);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x11151b), 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    auto *back = lv_label_create(page);
    lv_label_set_text(back, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(back, lv_color_hex(0xe6edf5), 0);
    lv_obj_set_pos(back, 30, 14);
    auto *title = lv_label_create(page);
    lv_label_set_text(title, "Manage Apps");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);
    auto *content = lv_obj_create(page);
    lv_obj_set_size(content, 480, 428); lv_obj_set_pos(content, 0, 52);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 10, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    catalog = {{"dev_tester", nullptr, true, 0}, {"clock", nullptr, true, 2},
               {"weather", nullptr, true, 3}, {"calculator", nullptr, true, 4}};
    crystal_manage_apps_page_build(content);
    lv_obj_update_layout(page);
    assert(lv_obj_get_child_cnt(content) == 5);
    for (int i = 0; i < 4; ++i) check_row(content, i, catalog[i].id, true);
    snapshot(page, "all-enabled.ppm");
    // Real pointer samples traverse LVGL hit-testing, press loss, scrolling,
    // and release. Sending events directly would miss the reported bug.
    auto drag = [&](int from, int to, bool cancel = false) {
        lv_obj_update_layout(page);
        auto *row = lv_obj_get_child(content, from + 1);
        auto *handle = lv_obj_get_child(row, 0);
        lv_area_t source, target, initial;
        lv_obj_get_coords(handle, &source);
        lv_obj_get_coords(row, &initial);
        lv_obj_get_coords(lv_obj_get_child(content, to + 1), &target);
        const int x = (source.x1 + source.x2) / 2;
        const int y = (source.y1 + source.y2) / 2;
        const int end_x = x + 50;
        const int end_y = y + target.y1 - initial.y1 + (to > from ? 4 : to < from ? -4 : 0);
        touch(x, y, LV_INDEV_STATE_PRESSED);
        lv_obj_update_layout(page);
        assert(lv_obj_get_child_cnt(lv_layer_top()) == 1);
        assert(lv_obj_get_child(lv_layer_top(), 0) == row);
        lv_area_t lifted;
        lv_obj_get_coords(row, &lifted);
        assert(lifted.x1 == initial.x1 && lifted.y1 == initial.y1);
        assert(lifted.x2 == initial.x2 && lifted.y2 == initial.y2);
        const unsigned before = slot_writes;
        for (int step = 1; step <= 5; ++step) {
            touch(x + (end_x-x)*step/5, y + (end_y-y)*step/5, LV_INDEV_STATE_PRESSED);
            lv_obj_update_layout(page);
            assert(lv_obj_get_child_cnt(lv_layer_top()) == 1);
        }
        auto *gap = lv_obj_get_child(content, to + 1);
        assert(lv_obj_get_user_data(gap) == nullptr);
        assert(lv_obj_get_height(gap) == initial.y2 - initial.y1 + 1);
        touch(end_x, end_y, LV_INDEV_STATE_PRESSED);
        lv_obj_update_layout(page);
        assert(lv_obj_get_child(content, to + 1) == gap);
        assert(slot_writes == before);
        if (cancel) lv_event_send(handle, LV_EVENT_PRESS_LOST, nullptr);
        touch(end_x, end_y, LV_INDEV_STATE_RELEASED);
        lv_obj_update_layout(page);
        assert(lv_obj_get_child_cnt(lv_layer_top()) == 0);
        assert(lv_obj_get_child_cnt(content) == catalog.size() + 1);
    };
    drag(1, 2);
    assert(slot_writes == 2 && slots.at("clock") == 3 && slots.at("weather") == 2);
    check_row(content, 1, "weather", true);
    check_row(content, 2, "clock", true);
    drag(2, 1);
    for (int i = 0; i < 4; ++i) check_row(content, i, catalog[i].id, true);
    drag(0, 3);
    check_row(content, 0, "clock", true);
    check_row(content, 3, "dev_tester", true);
    crystal_manage_apps_page_build(content);
    lv_obj_update_layout(page);
    check_row(content, 3, "dev_tester", true);
    drag(3, 0);
    const unsigned before_noop = slot_writes;
    drag(1, 1);
    drag(1, 3, true);
    assert(slot_writes == before_noop);
    for (int i = 0; i < 4; ++i) check_row(content, i, catalog[i].id, true);
    for (int attempt = 1; attempt <= 3; ++attempt) {
        const auto original = slots;
        fail_slot_at = slot_writes + attempt;
        drag(0, 3);
        assert(slots == original);
        assert(std::strstr(label(content, 0), "Previous order restored") != nullptr);
        for (int i = 0; i < 4; ++i) check_row(content, i, catalog[i].id, true);
        fail_slot_at = 0;
    }
    fail_slot_at = slot_writes + 2;
    fail_rollback = true;
    drag(0, 3);
    assert(std::strstr(label(content, 0), "fully saved or restored") != nullptr);
    fail_slot_at = 0;
    fail_rollback = false;
    slots.clear();
    crystal_manage_apps_page_build(content);
    lv_obj_update_layout(page);
    for (int i = 0; i < 4; ++i) check_row(content, i, catalog[i].id, true);
    std::puts("PASS: full-row pointer dragging, live shifts, repeated moves, no-op/cancel, persistence and rollback");
    auto click = [&](int i) {
        auto *row = lv_obj_get_child(content, i + 1);
        lv_event_send(lv_obj_get_child(row, 2), LV_EVENT_CLICKED, nullptr);
        lv_obj_update_layout(page);
    };
    // Successful clicks update only the target row and preserve its widgets.
    auto *clock_row = lv_obj_get_child(content, 2);
    click(1);
    assert(!saved.at("clock"));
    assert(lv_obj_get_child(content, 2) == clock_row);
    check_row(content, 1, "clock", false);
    assert(std::strstr(label(content, 0), "Restart") != nullptr);
    click(1);
    check_row(content, 1, "clock", true);
    assert(saved.at("clock"));
    // Save failure leaves the old state and Clear visibility intact.
    fail_write = true;
    unsigned before = writes;
    click(1);
    assert(writes == before + 1 && saved.at("clock"));
    check_row(content, 1, "clock", true);
    assert(std::strstr(label(content, 0), "Could not save") != nullptr);
    fail_write = false;
    click(1);
    crystal_manage_apps_page_build(content); // reopen from saved data
    lv_obj_update_layout(page);
    check_row(content, 1, "clock", false);
    click(1);
    for (int i = 0; i < 20; ++i) click(1);
    check_row(content, 1, "clock", true);
    // Leaving one app enabled must remain possible; removing it must not write.
    click(1); click(2); click(3);
    before = writes;
    click(0);
    assert(writes == before);
    check_row(content, 0, "dev_tester", true);
    assert(std::strstr(label(content, 0), "at least one") != nullptr);
    // A disabled app can always be restored, allowing the prior last app off.
    click(1); click(0);
    check_row(content, 0, "dev_tester", false);
    check_row(content, 1, "clock", true);
    saved.clear();
    overrides = true;
    crystal_manage_apps_page_build(content);
    lv_obj_update_layout(page);
    check_row(content, 0, "dev_tester", true); check_row(content, 1, "calculator", true);
    check_row(content, 2, "clock", true); check_row(content, 3, "weather", false);
    snapshot(page, "disabled-weather.ppm");
    overrides = false;
    for (auto &entry : catalog) entry.default_enabled = false;
    crystal_manage_apps_page_build(content);
    lv_obj_update_layout(page);
    for (int i = 0; i < 4; ++i) check_row(content, i, catalog[i].id, false);
    click(1); // default false, no stored entry
    assert(saved.at("clock"));
    check_row(content, 1, "clock", true);
    saved.clear();
    for (int i = 0; i < 25; ++i) crystal_manage_apps_page_build(content);
    assert(lv_obj_get_child_cnt(content) == 5);
    catalog.clear();
    crystal_manage_apps_page_build(content);
    assert(lv_obj_get_child_cnt(content) == 2);
    assert(std::strcmp(label(content, 1), "No apps in this firmware.") == 0);
    std::vector<std::string> ids;
    for (int i = 0; i < 12; ++i) ids.push_back("app_" + std::to_string(i));
    for (int i = 0; i < 12; ++i) catalog.push_back({ids[i].c_str(), nullptr, true, static_cast<uint16_t>(i)});
    crystal_manage_apps_page_build(content);
    lv_obj_update_layout(page);
    assert(lv_obj_get_scroll_bottom(content) > 0);
    auto *last = lv_obj_get_child(content, -1);
    lv_obj_scroll_to_view(last, LV_ANIM_OFF);
    lv_obj_update_layout(page);
    assert(lv_obj_get_scroll_y(content) > 0);
    lv_area_t last_box, viewport;
    lv_obj_get_coords(last, &last_box); lv_obj_get_coords(content, &viewport);
    assert(last_box.y1 >= viewport.y1 && last_box.y2 <= viewport.y2);
    const auto scroll_before = lv_obj_get_scroll_y(content);
    click(11);
    assert(lv_obj_get_scroll_y(content) == scroll_before);
    check_row(content, 11, "app_11", false);
    lv_obj_del(page);
    std::puts("PASS: real LVGL layout, defaults/overrides, catalog rows, slot sorting/ties, toggle persistence, failure/retry, last-app guard, empty/rebuild/scroll/teardown");
}
'''

def png_from_ppm(source):
    with source.open('rb') as stream:
        assert stream.readline() == b'P6\n'
        width, height = map(int, stream.readline().split())
        assert stream.readline() == b'255\n'
        pixels = stream.read()
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + pixels[y*width*3:(y+1)*width*3] for y in range(height))
    source.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='crystal-manage-apps-') as directory:
        temp = Path(directory)
        (temp / 'lv_conf.h').write_text(CONF)
        (temp / 'esp_log.h').write_text('#pragma once\n#define ESP_LOGI(...) ((void)0)\n')
        (temp / 'check.cpp').write_text(CHECK)
        lvgl = ROOT / 'managed_components/lvgl__lvgl'
        shell = ROOT / 'components/crystal_shell/src'
        (temp / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.16)
project(manage_apps_test LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(LV_CONF_PATH "{temp}/lv_conf.h" CACHE STRING "" FORCE)
add_subdirectory("{lvgl}" lvgl)
add_executable(check check.cpp "{shell}/manage_apps_page.cpp")
target_include_directories(check PRIVATE "{temp}" "{shell}" "{ROOT}/components/crystal_registry/include")
target_compile_options(check PRIVATE -Wall -Wextra -Werror)
target_link_libraries(check PRIVATE lvgl)
''')
        subprocess.run(['cmake', '-S', str(temp), '-B', str(temp / 'build')], check=True)
        subprocess.run(['cmake', '--build', str(temp / 'build'), '--target', 'check', '-j', '8'], check=True)
        output = args.output_dir or temp
        output.mkdir(parents=True, exist_ok=True)
        subprocess.run([str(temp / 'build/check')], check=True, cwd=output)
        for name in ('all-enabled.ppm', 'disabled-weather.ppm'):
            png_from_ppm(output / name)
        if args.output_dir:
            print(f'Rendered previews: {output.resolve()}')

if __name__ == '__main__':
    main()
