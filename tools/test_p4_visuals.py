#!/usr/bin/env python3
"""Exercise production P4 visuals with real LVGL and AddressSanitizer.

The host queue fixture copies data; panel timing remains a device check.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile
from test_launcher_swipe import CONF
from test_manage_apps_page import png_from_ppm

ROOT = Path(__file__).resolve().parents[1]
CHECK = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include "lvgl.h"
#include "crystal_boot_screen.hpp"
#include "crystal_boot_logo.hpp"
#include "crystal_stylesheet.hpp"
#include "crystal_page_transition.hpp"
static void flush(lv_disp_drv_t *d, const lv_area_t *, lv_color_t *) { lv_disp_flush_ready(d); }
static void advance(int ms) {
    for (int i = 0; i < ms; i += 10) { lv_tick_inc(10); lv_timer_handler(); }
}
static lv_obj_t *find(lv_obj_t *root, const char *text) {
    if (lv_obj_check_type(root, &lv_label_class) && !std::strcmp(lv_label_get_text(root), text)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) {
        if (auto *found = find(lv_obj_get_child(root, i), text)) return found;
    }
    return nullptr;
}
static void snapshot_png_source(lv_obj_t *root, const char *path) {
    auto *snapshot = lv_snapshot_take(root, LV_IMG_CF_TRUE_COLOR);
    assert(snapshot);
    FILE *file = std::fopen(path, "wb");
    assert(file);
    std::fprintf(file, "P6\n%u %u\n255\n", snapshot->header.w, snapshot->header.h);
    auto *pixels = reinterpret_cast<const lv_color_t *>(snapshot->data);
    assert(pixels[0].full == lv_color_hex(crystal_visual_style().background).full);
    for (size_t i = 0; i < static_cast<size_t>(snapshot->header.w) * snapshot->header.h; ++i) {
        lv_color32_t color;
        color.full = lv_color_to32(pixels[i]);
        const unsigned char rgb[] = {color.ch.red, color.ch.green, color.ch.blue};
        std::fwrite(rgb, 1, sizeof(rgb), file);
    }
    std::fclose(file);
    lv_snapshot_free(snapshot);
}
int main() {
    lv_init();
    static lv_color_t pixels[720 * 20];
    static lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer, pixels, nullptr, 720 * 20);
    static lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
#if CONFIG_IDF_TARGET_ESP32P4
    driver.hor_res = driver.ver_res = 720;
#else
    driver.hor_res = driver.ver_res = 480;
#endif
    driver.draw_buf = &buffer;
    driver.flush_cb = flush;
    auto *display = lv_disp_drv_register(&driver);
    auto *sheet = crystal_stylesheet_create_dark();
    assert(sheet && sheet->core.screen_size.width == driver.hor_res);
    assert(!sheet->home.flags.enable_recents_screen);
    assert(!sheet->manager.gesture.flags.enable_indicator_bars[ESP_BROOKESIA_GESTURE_INDICATOR_BAR_TYPE_BOTTOM]);
    delete sheet;
    // A root Settings transition must reveal the outgoing app, never the
    // launcher that Brookesia loads when pausing that app for the overlay.
    auto *app = lv_obj_create(lv_scr_act());
    lv_obj_set_size(app, driver.hor_res, driver.ver_res);
    lv_obj_set_style_bg_color(app, lv_color_hex(0x284860), 0);
    lv_obj_set_style_bg_opa(app, LV_OPA_COVER, 0);
    advance(40);
    const auto overlay_count = lv_obj_get_child_cnt(lv_layer_top());
    lv_area_t area = {0, 40, static_cast<lv_coord_t>(driver.hor_res - 1),
                          static_cast<lv_coord_t>(driver.ver_res - 1)};
    auto *backdrop = crystal_page_transition_capture_backdrop(area);
    assert(backdrop && lv_obj_get_child_cnt(backdrop) == 1);
    auto *image = lv_obj_get_child(backdrop, 0);
    auto *snapshot = static_cast<const lv_img_dsc_t *>(lv_img_get_src(image));
    assert(snapshot->header.w == driver.hor_res && snapshot->header.h == driver.ver_res);
    const auto *snapshot_pixels = reinterpret_cast<const lv_color_t *>(snapshot->data);
    assert(snapshot_pixels[(driver.ver_res / 2) * driver.hor_res + driver.hor_res / 2].full == lv_color_hex(0x284860).full);
    lv_obj_del(app); // stands in for the app pause/screen change
    auto *settings = lv_obj_create(lv_layer_top());
    crystal_page_transition_bind_backdrop(settings, backdrop);
    crystal_page_transition_enter(settings);
    advance(40);
    assert(lv_obj_is_valid(backdrop));
    crystal_page_transition_retire(settings);
#if CONFIG_IDF_TARGET_ESP32P4
    assert(lv_obj_is_valid(backdrop)); // it survives the outgoing fade too
#endif
    advance(400);
    assert(lv_obj_get_child_cnt(lv_layer_top()) == overlay_count);
    // Display teardown deletes older top-layer siblings before the page.
    backdrop = crystal_page_transition_capture_backdrop(area);
    settings = lv_obj_create(lv_layer_top());
    crystal_page_transition_bind_backdrop(settings, backdrop);
    lv_obj_del(backdrop);
    lv_obj_del(settings);
    // Brookesia app screens themselves start below the 40px status bar.
    // A screen-local snapshot must be placed back at the same display origin.
    auto *home_screen = lv_scr_act();
    auto *app_screen = lv_obj_create(nullptr);
    lv_scr_load(app_screen);
    lv_obj_set_pos(app_screen, 0, 40);
    lv_obj_set_size(app_screen, driver.hor_res, driver.ver_res - 40);
    lv_obj_set_style_pad_all(app_screen, 0, 0);
    lv_obj_set_style_border_width(app_screen, 0, 0);
    lv_obj_set_style_bg_color(app_screen, lv_color_hex(0x284860), 0);
    lv_obj_set_style_bg_opa(app_screen, LV_OPA_COVER, 0);
    auto *marker = lv_obj_create(app_screen);
    lv_obj_set_pos(marker, 24, 30);
    lv_obj_set_size(marker, 40, 40);
    lv_obj_set_style_bg_color(marker, lv_color_hex(0xf06020), 0);
    lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
    advance(40);
    backdrop = crystal_page_transition_capture_backdrop(area);
    assert(backdrop);
    image = lv_obj_get_child(backdrop, 0);
    lv_obj_update_layout(backdrop);
    lv_area_t image_bounds{}, app_bounds{};
    lv_obj_get_coords(image, &image_bounds);
    lv_obj_get_coords(app_screen, &app_bounds);
    assert(image_bounds.x1 == app_bounds.x1 && image_bounds.y1 == app_bounds.y1);
    settings = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(settings, 0, 40);
    lv_obj_set_size(settings, driver.hor_res, driver.ver_res - 40);
    crystal_page_transition_bind_backdrop(settings, backdrop);
    crystal_page_transition_enter(settings);
    for (int frame = 0; frame < 35; ++frame) {
        advance(10);
        assert(lv_obj_get_style_translate_x(settings, 0) == 0);
        assert(lv_obj_get_style_translate_y(settings, 0) == 0);
        lv_obj_get_coords(image, &image_bounds);
        assert(image_bounds.x1 == app_bounds.x1 && image_bounds.y1 == app_bounds.y1);
    }
    auto *child = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(child, 0, 40);
    lv_obj_set_size(child, driver.hor_res, driver.ver_res - 40);
    crystal_page_transition_enter(child, settings);
#if CONFIG_IDF_TARGET_ESP32P4
    for (int frame = 0; frame < 10; ++frame) {
        advance(10);
        assert(!lv_obj_has_flag(settings, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_get_style_opa(settings, 0) == LV_OPA_COVER);
        assert(lv_obj_get_style_translate_x(child, 0) == 0);
    }
#endif
    advance(400);
    assert(lv_obj_has_flag(settings, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_get_style_opa(child, 0) == LV_OPA_COVER);
    lv_obj_clear_flag(settings, LV_OBJ_FLAG_HIDDEN);
    crystal_page_transition_retire(child);
    advance(400);
    // A second push while entry is in flight must finish its parent first.
    child = lv_obj_create(lv_layer_top());
    crystal_page_transition_enter(child, settings);
    advance(30);
    crystal_page_transition_settle();
    assert(lv_obj_has_flag(settings, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_get_style_opa(child, 0) == LV_OPA_COVER);
    lv_obj_del(child);
    lv_obj_del(settings); // owns backdrop
    lv_scr_load(home_screen);
    lv_obj_del(app_screen);
    assert(lv_obj_get_child_cnt(lv_layer_top()) == overlay_count);
    const auto before = lv_obj_get_child_cnt(lv_layer_top());
    assert(crystal_boot_screen_show());
#if CONFIG_IDF_TARGET_ESP32P4
    assert(crystal_boot_screen_active());
    assert(lv_obj_get_child_cnt(lv_layer_top()) == before + 1);
    auto *boot = lv_obj_get_child(lv_layer_top(), -1);
    lv_obj_t *logo = nullptr;
    lv_obj_t *progress = nullptr;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(boot); ++i) {
        auto *object = lv_obj_get_child(boot, static_cast<int32_t>(i));
        assert(!lv_obj_check_type(object, &lv_spinner_class));
        if (lv_obj_check_type(object, &lv_img_class)) logo = object;
        if (lv_obj_check_type(object, &lv_bar_class)) progress = object;
    }
    assert(logo && progress && lv_img_get_src(logo) == &crystal_boot_logo);
    assert(crystal_boot_logo.header.cf == LV_IMG_CF_TRUE_COLOR_ALPHA);
    assert(crystal_boot_logo.data_size == 256 * 256 * 3);
    assert(crystal_boot_logo.data[2] == 0); // transparent corner, not a black box
    lv_obj_update_layout(boot);
    assert(lv_obj_get_width(logo) == 256 && lv_obj_get_height(logo) == 256);
    assert(lv_img_get_zoom(logo) == LV_IMG_ZOOM_NONE);
    lv_coord_t last_bottom = -1;
    for (auto *object : {logo, find(boot, "Crystal OS"), find(boot, "STARTING"),
                          find(boot, "Preparing your workspace..."), progress}) {
        assert(object);
        lv_area_t bounds{};
        lv_obj_get_coords(object, &bounds);
        assert(bounds.x1 >= 0 && bounds.x2 < 720);
        assert(bounds.y1 > last_bottom && bounds.y2 < 720);
        last_bottom = bounds.y2;
    }
    snapshot_png_source(boot, "boot-logo-p4.ppm");
    char message[] = "Loading installed apps";
    std::thread update([&] { crystal_boot_screen_update(message, 42); });
    update.join();
    std::memset(message, 'x', sizeof(message) - 1);
    advance(40);
    assert(find(lv_layer_top(), "Loading installed apps"));
    auto *late = lv_obj_create(lv_layer_top());
    lv_obj_set_size(late, 720, 720);
    assert(crystal_boot_screen_show());
    assert(lv_obj_get_child(lv_layer_top(), -1) != late);
    crystal_boot_screen_finish();
    advance(40);
    assert(find(lv_layer_top(), "READY"));
    assert(!lv_obj_has_flag(logo, LV_OBJ_FLAG_HIDDEN));
    assert(crystal_boot_screen_active());
    advance(800);
    assert(!crystal_boot_screen_active());
    assert(lv_obj_get_child_cnt(lv_layer_top()) == before + 1);
    lv_obj_del(late);
#else
    assert(!crystal_boot_screen_active());
    assert(lv_obj_get_child_cnt(lv_layer_top()) == before);
#endif
    for (int i = 0; i < 40; ++i) {
        auto *page = lv_obj_create(lv_scr_act());
        lv_obj_set_size(page, 400, 400);
        auto *button = lv_btn_create(page);
        crystal_page_transition_enter(page);
        advance(i % 2 ? 30 : 400);
        crystal_page_transition_retire(page);
#if CONFIG_IDF_TARGET_ESP32P4
        assert(!lv_obj_has_flag(button, LV_OBJ_FLAG_CLICKABLE));
        assert(lv_obj_has_state(button, LV_STATE_DISABLED));
#else
        (void)button;
#endif
        if (i % 3) crystal_page_transition_settle();
        advance(400);
        assert(lv_obj_get_child_cnt(lv_scr_act()) == 0);
    }
    crystal_page_transition_settle();
#if CONFIG_IDF_TARGET_ESP32P4
    assert(crystal_boot_screen_show());
    crystal_boot_screen_fail("App registry unavailable");
    advance(40);
    assert(find(lv_layer_top(), "STARTUP FAILED"));
    assert(find(lv_layer_top(), "App registry unavailable"));
    advance(1000);
    assert(crystal_boot_screen_active());
#endif
    lv_disp_remove(display);
    std::puts("PASS: production theme, copied cross-task boot updates, Ready/failure, fade ownership, repeated/interrupted Settings navigation, S3 fallback");
}
'''
QUEUE = r'''
#pragma once
#include <cstring>
#include <mutex>
#include <vector>
struct HostQueue { std::mutex mutex; std::vector<unsigned char> value; bool ready = false; explicit HostQueue(size_t n): value(n) {} };
using QueueHandle_t = HostQueue *;
inline QueueHandle_t xQueueCreate(unsigned, size_t size) { return new HostQueue(size); }
inline int xQueueReset(QueueHandle_t q) { std::lock_guard<std::mutex> lock(q->mutex); q->ready = false; return 1; }
inline int xQueueOverwrite(QueueHandle_t q, const void *v) { std::lock_guard<std::mutex> lock(q->mutex); std::memcpy(q->value.data(), v, q->value.size()); q->ready = true; return 1; }
inline int xQueueReceive(QueueHandle_t q, void *v, int) { std::lock_guard<std::mutex> lock(q->mutex); if (!q->ready) return 0; std::memcpy(v, q->value.data(), q->value.size()); q->ready = false; return 1; }
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='crystal-p4-visuals-') as directory:
        temp = Path(directory)
        (temp / 'freertos').mkdir()
        (temp / 'freertos/FreeRTOS.h').write_text('#pragma once\n#define pdTRUE 1\n')
        (temp / 'freertos/queue.h').write_text(QUEUE)
        (temp / 'sdkconfig.h').write_text('#pragma once\n')
        (temp / 'lv_conf.h').write_text(CONF + '\n#define LV_FONT_MONTSERRAT_28 1\n#define LV_FONT_MONTSERRAT_48 1\n')
        (temp / 'check.cpp').write_text(CHECK)
        shell = ROOT / 'components/crystal_shell'
        logo = (shell / 'assets/crystalos_boot_logo_256.bin').read_bytes()
        data = ','.join(str(byte) for byte in logo)
        (temp / 'logo_data.cpp').write_text(
            '#include <stdint.h>\nextern const uint8_t host_logo_data[] '
            'asm("_binary_crystal_boot_logo_rgb565a8_start") = {' + data + '};\n')
        brookesia = ROOT / 'components/esp-brookesia/src'
        lvgl = ROOT / 'managed_components/lvgl__lvgl'
        assets = ' '.join('"' + str(p) + '"' for p in (brookesia / 'assets').rglob('*.c'))
        cmake = f'''cmake_minimum_required(VERSION 3.16)
project(p4_visual_test LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(LV_CONF_PATH "{temp}/lv_conf.h" CACHE STRING "" FORCE)
add_subdirectory("{lvgl}" lvgl)
'''
        for target in ('p4', 's3'):
            cmake += f'''
add_executable(check_{target} check.cpp "{shell}/src/crystal_boot_screen.cpp"
    "{shell}/src/crystal_stylesheet.cpp" "{shell}/src/crystal_page_transition.cpp"
    "{shell}/src/crystal_boot_logo.cpp" logo_data.cpp {assets})
target_include_directories(check_{target} PRIVATE "{temp}" "{shell}/include" "{brookesia}")
target_compile_definitions(check_{target} PRIVATE CONFIG_IDF_TARGET_ESP32P4={int(target == 'p4')}
    ESP_BROOKESIA_KCONFIG_IGNORE ESP_BROOKESIA_CONF_SKIP ESP_BROOKESIA_SQUARELINE_USE_INTERNAL_UI_HELPERS=1
    ESP_BROOKESIA_SQ1_4_1_LV8_3_11)
target_compile_options(check_{target} PRIVATE -g -fsanitize=address -Wno-c99-designator -Wno-reorder-init-list)
target_link_options(check_{target} PRIVATE -fsanitize=address)
target_link_libraries(check_{target} PRIVATE lvgl)
'''
        (temp / 'CMakeLists.txt').write_text(cmake)
        subprocess.run(['cmake', '-S', str(temp), '-B', str(temp / 'build')], check=True)
        subprocess.run(['cmake', '--build', str(temp / 'build'), '--target', 'check_p4', 'check_s3', '-j', '8'], check=True)
        output = args.output_dir.resolve() if args.output_dir else temp
        output.mkdir(parents=True, exist_ok=True)
        for target in ('p4', 's3'):
            subprocess.run([str(temp / ('build/check_' + target))], check=True, cwd=output)
        png_from_ppm(output / 'boot-logo-p4.ppm')
        if args.output_dir:
            print(f'Rendered boot preview: {output / "boot-logo-p4.png"}')

if __name__ == '__main__':
    main()
