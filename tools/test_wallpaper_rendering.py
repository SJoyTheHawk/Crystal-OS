#!/usr/bin/env python3
"""Check Crystal and stock wallpapers through real LVGL and Brookesia paging.

Requires CMake and a C++ compiler. This verifies rendering paths, not panel FPS.
"""
from pathlib import Path
import subprocess
import tempfile
from test_launcher_swipe import CONF

ROOT = Path(__file__).resolve().parents[1]
CHECK = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include "crystal_stylesheet.hpp"
#include "systems/phone/esp_brookesia_phone.hpp"
extern "C" { LV_IMG_DECLARE(stock_wallpaper); }
static const lv_img_dsc_t *observed;
static unsigned opens, lines, direct_draws;
static decltype(lv_draw_ctx_t::draw_img_decoded) original_draw;
static lv_res_t open_image(lv_img_decoder_t *decoder, lv_img_decoder_dsc_t *dsc) {
    auto result = lv_img_decoder_built_in_open(decoder, dsc);
    if (dsc->src == observed) {
        ++opens;
        assert(result == LV_RES_OK && dsc->img_data == observed->data);
        assert(dsc->user_data == nullptr); // No allocated palette or decoded copy.
    }
    return result;
}
static lv_res_t read_line(lv_img_decoder_t *decoder, lv_img_decoder_dsc_t *dsc,
                          lv_coord_t x, lv_coord_t y, lv_coord_t length, uint8_t *buffer) {
    if (dsc->src == observed) ++lines;
    return lv_img_decoder_built_in_read_line(decoder, dsc, x, y, length, buffer);
}
static void draw_image(lv_draw_ctx_t *ctx, const lv_draw_img_dsc_t *dsc,
                       const lv_area_t *area, const uint8_t *data, lv_img_cf_t cf) {
    if (observed && data == observed->data) {
        ++direct_draws;
        assert(cf == LV_IMG_CF_TRUE_COLOR);
        assert(dsc->zoom == LV_IMG_ZOOM_NONE && dsc->angle == 0);
        assert(dsc->recolor_opa == LV_OPA_TRANSP && dsc->opa == LV_OPA_COVER);
        assert(!lv_draw_mask_is_any(ctx->clip_area));
    }
    original_draw(ctx, dsc, area, data, cf);
}
static void flush(lv_disp_drv_t *d, const lv_area_t *, lv_color_t *) { lv_disp_flush_ready(d); }
static void touch(lv_indev_drv_t *, lv_indev_data_t *d) { d->state = LV_INDEV_STATE_RELEASED; }
static void advance() { for (int i = 0; i < 60; ++i) { lv_tick_inc(10); lv_timer_handler(); } }
static lv_obj_t *find_pager(lv_obj_t *root) {
    if (lv_obj_get_scroll_snap_x(root) == LV_SCROLL_SNAP_CENTER &&
        lv_obj_get_scroll_dir(root) == LV_DIR_HOR) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
        if (auto *p = find_pager(lv_obj_get_child(root, i))) return p;
    return nullptr;
}
class App : public ESP_Brookesia_PhoneApp {
public:
    App() : ESP_Brookesia_PhoneApp("Test", nullptr, true) {}
    bool run() override { return true; }
    bool back() override { return true; }
};
int main() {
    lv_init();
    auto *decoder = lv_img_decoder_create();
    lv_img_decoder_set_info_cb(decoder, lv_img_decoder_built_in_info);
    lv_img_decoder_set_open_cb(decoder, open_image);
    lv_img_decoder_set_read_line_cb(decoder, read_line);
    lv_img_decoder_set_close_cb(decoder, lv_img_decoder_built_in_close);
    static lv_color_t pixels[720 * 20];
    static lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer, pixels, nullptr, 720 * 20);
    lv_disp_drv_t driver; lv_disp_drv_init(&driver);
    driver.hor_res = driver.ver_res = 720; driver.draw_buf = &buffer; driver.flush_cb = flush;
    auto *display = lv_disp_drv_register(&driver);
    original_draw = display->driver->draw_ctx->draw_img_decoded;
    display->driver->draw_ctx->draw_img_decoded = draw_image;
    lv_indev_drv_t input; lv_indev_drv_init(&input);
    input.type = LV_INDEV_TYPE_POINTER; input.read_cb = touch;
    auto *pointer = lv_indev_drv_register(&input);
    std::unique_ptr<ESP_Brookesia_PhoneStylesheet_t> sheet(crystal_stylesheet_create_dark());
    const auto *crystal = &esp_brookesia_image_large_wallpaper_dark_720_720;
    assert(sheet && sheet->core.home.background.wallpaper_image_resource.resource == crystal);
    for (auto *asset : {crystal, &stock_wallpaper,
                       &esp_brookesia_image_middle_wallpaper_dark_480_480,
                       &esp_brookesia_image_small_wallpaper_dark_240_240}) {
        assert(asset->header.cf == LV_IMG_CF_TRUE_COLOR && !lv_img_cf_has_alpha(asset->header.cf));
        assert(asset->data_size == asset->header.w * asset->header.h * sizeof(lv_color_t));
    }
    assert(crystal->header.w == 720 && crystal->header.h == 720);
    assert(crystal->data_size == stock_wallpaper.data_size);
    assert(std::memcmp(crystal->data, stock_wallpaper.data, crystal->data_size) != 0);
    // Full pixel equality checks the native-size background painting and byte order.
    auto *canvas = lv_obj_create(nullptr); lv_obj_remove_style_all(canvas);
    lv_obj_set_size(canvas, 720, 720); lv_obj_set_style_bg_img_src(canvas, crystal, 0);
    observed = crystal;
    auto *snapshot = lv_snapshot_take(canvas, LV_IMG_CF_TRUE_COLOR);
    assert(snapshot && snapshot->header.w == 720 && snapshot->header.h == 720);
    assert(std::memcmp(snapshot->data, crystal->data, crystal->data_size) == 0);
    lv_snapshot_free(snapshot); lv_obj_del(canvas); observed = nullptr;
    {
        std::vector<std::unique_ptr<App>> apps;
        ESP_Brookesia_Phone phone(display);
        assert(phone.setTouchDevice(pointer)); assert(phone.addStylesheet(*sheet));
        assert(phone.activateStylesheet(*sheet)); assert(phone.begin());
        for (int i = 0; i < 9; ++i) {
            apps.emplace_back(new App); assert(phone.installApp(apps.back().get()) >= 0);
        }
        assert(phone.sendNavigateEvent(ESP_BROOKESIA_CORE_NAVIGATE_TYPE_HOME)); advance();
        auto *home = phone.getCoreHome().getMainScreenObject();
        assert(lv_obj_get_style_bg_img_src(home, 0) == crystal);
        auto *pager = find_pager(home); assert(pager && lv_obj_get_child_cnt(pager) > 1);
        auto *launcher = phone.getHome().getAppLauncher();
        for (auto *asset : {&stock_wallpaper, crystal}) {
            lv_obj_set_style_bg_img_src(home, asset, 0); observed = asset;
            opens = lines = direct_draws = 0; lv_obj_invalidate(home); advance();
            assert(launcher->scrollToPage(1)); advance();
            assert(launcher->getActiveScreenIndex() == 1 && lv_obj_get_scroll_x(pager) == 720);
            assert(launcher->scrollToPage(0)); advance();
            assert(launcher->getActiveScreenIndex() == 0 && lv_obj_get_scroll_x(pager) == 0);
            assert(opens > 0 && direct_draws > 0 && lines == 0);
            std::printf("PASS %s: page changes use native opaque buffer, %u direct draws, zero decoded lines\n",
                        asset == crystal ? "Crystal" : "stock", direct_draws);
        }
        observed = nullptr;
        for (auto &app : apps) assert(phone.uninstallApp(app.get()));
    }
    lv_indev_delete(pointer); lv_disp_remove(display); lv_img_decoder_delete(decoder);
    std::puts("PASS: full wallpaper pixel equality; original production background and paging paths");
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix='crystal-wallpaper-render-') as directory:
        temp = Path(directory)
        (temp / 'lv_conf.h').write_text(CONF + '\n#define LV_IMG_CACHE_DEF_SIZE 0\n#define LV_FONT_MONTSERRAT_24 1\n#define LV_FONT_MONTSERRAT_28 1\n')
        (temp / 'sdkconfig.h').write_text('#pragma once\n')
        (temp / 'check.cpp').write_text(CHECK)
        brookesia = ROOT / 'components/esp-brookesia/src'
        shell = ROOT / 'components/crystal_shell'
        relative = 'assets/images/large/esp_brookesia_image_large_wallpaper_dark_720_720.c'
        stock = ROOT / 'reference/Crystal-OS-bus-app-development/components/esp-brookesia/src' / relative
        (temp / 'stock.c').write_text(stock.read_text().replace(
            'esp_brookesia_image_large_wallpaper_dark_720_720', 'stock_wallpaper'))
        (temp / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.16)
project(wallpaper_test LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(LV_CONF_PATH "{temp}/lv_conf.h" CACHE STRING "" FORCE)
add_compile_options(-g -fsanitize=address)
add_link_options(-fsanitize=address)
add_subdirectory("{ROOT}/managed_components/lvgl__lvgl" lvgl)
file(GLOB_RECURSE BROOKESIA_SOURCES
    "{brookesia}/core/*.cpp" "{brookesia}/core/*.c"
    "{brookesia}/widgets/*.cpp" "{brookesia}/systems/phone/*.cpp"
    "{brookesia}/assets/*.c")
add_executable(check check.cpp stock.c "{shell}/src/crystal_stylesheet.cpp" {chr(36)}{{BROOKESIA_SOURCES}})
target_include_directories(check PRIVATE "{temp}" "{brookesia}" "{shell}/include")
target_compile_definitions(check PRIVATE CONFIG_IDF_TARGET_ESP32P4=1
    ESP_BROOKESIA_KCONFIG_IGNORE ESP_BROOKESIA_CONF_SKIP
    ESP_BROOKESIA_SQUARELINE_USE_INTERNAL_UI_HELPERS=1 ESP_BROOKESIA_SQ1_4_1_LV8_3_11)
target_compile_options(check PRIVATE -Wno-c99-designator -Wno-reorder-init-list)
target_link_libraries(check PRIVATE lvgl)
''')
        subprocess.run(['cmake', '-S', str(temp), '-B', str(temp / 'build')], check=True)
        subprocess.run(['cmake', '--build', str(temp / 'build'), '--target', 'check', '-j', '8'], check=True)
        subprocess.run([str(temp / 'build/check')], check=True, cwd=temp)


if __name__ == '__main__':
    main()
