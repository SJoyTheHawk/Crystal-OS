#!/usr/bin/env python3
"""Exercise the production Developer Tools page with real LVGL and service stubs.

An optional --output-dir saves renders. Networking, task creation, and the
Settings header are fixtures; physical pairing remains an on-device check.
"""
import argparse
import re
from pathlib import Path
import subprocess
import tempfile

from test_manage_apps_page import CONF, png_from_ppm

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'components/crystal_shell/src/crystal_shell.cpp'


def function(source, signature):
    start = source.index(signature + '\n{')
    return source[start:source.index('\n}\n', start) + 3]


FIXTURE = r'''
#include <cassert>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include "lvgl.h"
struct TestStorage {
    uint8_t value = 1;
    uint8_t lua_value = 0;
    bool lua_present = false, fail_lua_write = false;
    size_t length = 1;
    bool present = false;
    unsigned writes = 0;
    bool get(const char *key, void *out, size_t *size) {
        if (!std::strcmp(key, "dev.lua_long")) {
            if (!lua_present) return false;
            assert(*size >= 1); *static_cast<uint8_t *>(out)=lua_value; *size=1; return true;
        }
        assert(!std::strcmp(key, "dev.resources"));
        if (!present) return false;
        if (*size >= length) std::memcpy(out, &value, length);
        *size = length; return true;
    }
    bool set(const char *key, const void *data, size_t size) {
        if (!std::strcmp(key, "dev.lua_long")) {
            assert(size==1); if(fail_lua_write)return false;
            lua_value=*static_cast<const uint8_t *>(data);lua_present=true;return true;
        }
        assert(!std::strcmp(key, "dev.resources") && size == 1);
        value = *static_cast<const uint8_t *>(data); length = size;
        present = true; ++writes; return true;
    }
};
static TestStorage storage;
static lv_obj_t *s_developer_tools_page, *s_control_api_row, *s_control_api_switch;
static lv_obj_t *s_control_status_label, *s_system_page_stack[4], *content;
static unsigned s_system_page_depth;
static bool s_control_start_pending, server_enabled, fail_task, fail_page;
static unsigned starts, stops, dialogs;
static bool pairing_visible;
static std::string shown_url;
constexpr int pdPASS = 1, MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
struct IWifi {
    struct IpConfig { unsigned ip = 0; };
    bool connected = true;
    bool has_ip() const { return connected; }
    bool ip_config(IpConfig *out) const { out->ip = 1; return connected; }
};
struct Hal { IWifi *wifi; TestStorage *storage; };
static IWifi wifi;
static Hal services{&wifi, &storage};
Hal &hal() { return services; }
static std::string toast;
void crystal_core_show_toast(const char *message) { toast=message; }
struct EthernetInfo { bool has_ip=false; unsigned ip=2; };
static EthernetInfo ethernet;
enum class CrystalNetworkType { None, WiFi, Ethernet };
CrystalNetworkType crystal_network_active_type(){return ethernet.has_ip?CrystalNetworkType::Ethernet:wifi.has_ip()?CrystalNetworkType::WiFi:CrystalNetworkType::None;}
EthernetInfo crystal_ethernet_info(){return ethernet;}
bool crystal_network_has_ip(){return wifi.has_ip()||ethernet.has_ip;}
struct ip4_addr_t { unsigned addr; };
void ip4addr_ntoa_r(const ip4_addr_t *, char *out, size_t n) { std::snprintf(out, n, "192.168.1.158"); }
bool crystal_control_enabled() { return server_enabled; }
void crystal_control_disable() { server_enabled = false; ++stops; }
void control_panel_start_worker(void *) {}
int xTaskCreateWithCaps(void (*worker)(void *), const char *name, unsigned stack,
                        void *arg, unsigned priority, void *handle, unsigned caps) {
    assert(worker == control_panel_start_worker && !std::strcmp(name, "control_start"));
    assert(stack == 6144 && !arg && priority == 2 && !handle);
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ++starts; return fail_task ? 0 : pdPASS;
}
void control_pair_dialog_close() { pairing_visible = false; }
void control_pair_dialog_build(const char *pin, const char *url) {
    assert(std::strlen(pin) == 6); ++dialogs; pairing_visible = true; shown_url = url;
}
static lv_obj_t *system_page_push(const char *title) {
    if (fail_page) return nullptr;
    auto *page = lv_obj_create(lv_scr_act());
    lv_obj_set_size(page, 480, 440); lv_obj_set_pos(page, 0, 40);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x11151b), 0);
    lv_obj_set_style_border_width(page, 0, 0); lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    auto *heading = lv_label_create(page); lv_label_set_text(heading, title);
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(heading, lv_color_white(), 0);
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 14);
    content = lv_obj_create(page);
    lv_obj_set_size(content, LV_PCT(100), 388); lv_obj_set_pos(content, 0, 52);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 10, 0); lv_obj_set_style_pad_row(content, 1, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    s_system_page_stack[s_system_page_depth++] = page;
    return content;
}
void settings_button_feedback(lv_obj_t *button);
void settings_row_reserve_right(lv_obj_t *row, lv_coord_t control_width);
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
'''

CHECK = r'''
static void flush(lv_disp_drv_t *driver, const lv_area_t *, lv_color_t *) { lv_disp_flush_ready(driver); }
static lv_obj_t *resources_switch() {
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(content); ++i) {
        auto *row = lv_obj_get_child(content, i);
        if (!lv_obj_check_type(row, &lv_btn_class)) continue;
        if (!std::strcmp(lv_label_get_text(lv_obj_get_child(row, 0)), "Resources Monitor")) {
            return lv_obj_get_child(row, -1);
        }
    }
    assert(false); return nullptr;
}
static lv_obj_t *timeout_switch() {
    for(uint32_t i=0;i<lv_obj_get_child_cnt(content);++i) {
        auto *row=lv_obj_get_child(content,i);
        if(lv_obj_check_type(row,&lv_btn_class) &&
           !std::strcmp(lv_label_get_text(lv_obj_get_child(row,0)),"Extended Lua Timeout"))
            return lv_obj_get_child(row,-1);
    }
    assert(false);return nullptr;
}
static void timeout_toggle(bool on) {
    auto *control=timeout_switch();
    if(on)lv_obj_add_state(control,LV_STATE_CHECKED);
    else lv_obj_clear_state(control,LV_STATE_CHECKED);
    lv_event_send(control,LV_EVENT_VALUE_CHANGED,nullptr);
}
static void resources_toggle(bool on) {
    auto *control = resources_switch();
    assert(lv_obj_check_type(control, &lv_switch_class));
    if (on) lv_obj_add_state(control, LV_STATE_CHECKED);
    else lv_obj_clear_state(control, LV_STATE_CHECKED);
    lv_event_send(control, LV_EVENT_VALUE_CHANGED, nullptr);
}
static void page_close() {
    auto *page = s_developer_tools_page;
    s_developer_tools_page = s_control_api_row = s_control_api_switch = s_control_status_label = nullptr;
    s_system_page_depth = 0;
    if (page) lv_obj_del(page);
}
static void reset(bool enabled = false) {
    page_close(); server_enabled = enabled; s_control_start_pending = false;
    fail_task = fail_page = false; starts = stops = dialogs = 0; ethernet.has_ip=false;
    pairing_visible = false; shown_url.clear(); services.wifi = &wifi; wifi.connected = true;
    settings_push_developer_tools(); lv_obj_update_layout(lv_scr_act());
}
static void toggle(bool on) {
    assert(s_control_api_switch && !lv_obj_has_state(s_control_api_switch, LV_STATE_DISABLED));
    if (on) lv_obj_add_state(s_control_api_switch, LV_STATE_CHECKED);
    else lv_obj_clear_state(s_control_api_switch, LV_STATE_CHECKED);
    lv_event_send(s_control_api_switch, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_update_layout(lv_scr_act());
}
static void state(bool on, bool disabled) {
    assert(lv_obj_check_type(s_control_api_switch, &lv_switch_class));
    assert(lv_obj_has_state(s_control_api_switch, LV_STATE_CHECKED) == on);
    assert(lv_obj_has_state(s_control_api_switch, LV_STATE_DISABLED) == disabled);
    assert(lv_obj_get_width(s_control_api_switch) == 52 && lv_obj_get_height(s_control_api_switch) == 30);
}
static void summary(const char *expected) {
    assert(!std::strcmp(lv_label_get_text(static_cast<lv_obj_t *>(lv_obj_get_user_data(s_control_api_row))), expected));
}
static void snapshot(const char *name) {
    // Settle the switch's theme animation before capturing its checked color.
    for (unsigned i = 0; i < 20; ++i) { lv_tick_inc(20); lv_timer_handler(); }
    lv_obj_update_layout(lv_scr_act());
    auto *snap = lv_snapshot_take(lv_scr_act(), LV_IMG_CF_TRUE_COLOR); assert(snap);
    auto *file = std::fopen(name, "wb"); assert(file);
    std::fprintf(file, "P6\n%u %u\n255\n", snap->header.w, snap->header.h);
    auto *pixels = reinterpret_cast<const lv_color_t *>(snap->data);
    for (size_t i = 0; i < size_t(snap->header.w) * snap->header.h; ++i) {
        lv_color32_t c; c.full = lv_color_to32(pixels[i]);
        unsigned char rgb[] = {c.ch.red, c.ch.green, c.ch.blue}; std::fwrite(rgb, 1, 3, file);
    }
    std::fclose(file); lv_snapshot_free(snap);
}
int main() {
    lv_init();
    static lv_color_t pixels[480 * 20]; static lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw, pixels, nullptr, 480 * 20);
    static lv_disp_drv_t driver; lv_disp_drv_init(&driver);
    driver.hor_res = driver.ver_res = 480; driver.draw_buf = &draw; driver.flush_cb = flush;
    auto *display = lv_disp_drv_register(&driver);
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x11151b), 0);

    // Restore the preference before LVGL lazily creates its CPU/FPS label.
    assert(crystal_resources_monitor_enabled()); // Fresh devices keep the existing overlay.
    storage.present = true; storage.value = 0;
    crystal_ui_metrics_start(display);
    auto *dialog = lv_label_create(lv_layer_top()); lv_label_set_text(dialog, "Dialog stays visible");
    for (unsigned i = 0; i < 40; ++i) { lv_tick_inc(10); lv_timer_handler(); }
    auto *monitor = lv_obj_get_child(lv_layer_sys(), 0);
    assert(monitor && lv_obj_check_type(monitor, &lv_label_class));
    assert(std::strstr(lv_label_get_text(monitor), "FPS") && std::strstr(lv_label_get_text(monitor), "CPU"));
    assert(!lv_obj_is_visible(monitor) && lv_obj_is_visible(dialog));
    assert(storage.writes == 0); // Restoring never writes flash.
    reset();
    assert(crystal_lua_callback_timeout_seconds()==5);
    assert(!lv_obj_has_state(timeout_switch(),LV_STATE_CHECKED));
    timeout_toggle(true);assert(crystal_lua_callback_timeout_seconds()==30 && storage.lua_value==1);
    reset();assert(lv_obj_has_state(timeout_switch(),LV_STATE_CHECKED));
    s_cache_lua_extended=-1;assert(crystal_lua_callback_timeout_seconds()==30);
    storage.fail_lua_write=true;timeout_toggle(false);
    assert(lv_obj_has_state(timeout_switch(),LV_STATE_CHECKED));
    assert(crystal_lua_callback_timeout_seconds()==30 && toast=="Could not save Lua timeout");
    storage.fail_lua_write=false;timeout_toggle(false);
    s_cache_lua_extended=-1;assert(crystal_lua_callback_timeout_seconds()==5 && storage.lua_value==0);
    reset();assert(!lv_obj_has_state(timeout_switch(),LV_STATE_CHECKED));
    assert(!lv_obj_has_state(resources_switch(), LV_STATE_CHECKED));
    resources_toggle(true);
    assert(lv_obj_is_visible(monitor) && lv_obj_is_visible(dialog));
    assert(crystal_resources_monitor_enabled() && storage.value == 1 && storage.writes == 1);
    page_close(); settings_push_developer_tools();
    assert(lv_obj_has_state(resources_switch(), LV_STATE_CHECKED));
    resources_toggle(false);
    assert(!lv_obj_is_visible(monitor) && !crystal_resources_monitor_enabled());
    page_close(); settings_push_developer_tools();
    assert(!lv_obj_has_state(resources_switch(), LV_STATE_CHECKED));
    // Startup reapplies the saved choice even if the display's flags were reset.
    lv_obj_clear_flag(lv_layer_sys(), LV_OBJ_FLAG_HIDDEN);
    crystal_ui_metrics_start(display);
    assert(!lv_obj_is_visible(monitor) && storage.writes == 2);
    storage.value = 2; assert(crystal_resources_monitor_enabled());
    storage.value = 0; storage.length = 0; assert(crystal_resources_monitor_enabled());
    services.storage = nullptr; assert(crystal_resources_monitor_enabled());
    crystal_resources_monitor_set_enabled(true); assert(lv_obj_is_visible(monitor));
    services.storage = &storage; storage.length = 1;
    crystal_ui_metrics_start(display);
    assert(!lv_obj_is_visible(monitor) && lv_obj_is_visible(dialog));
    lv_obj_del(dialog);

    reset(); state(false, false); summary("Off - enable to pair a browser");
    assert(lv_obj_get_child_cnt(s_control_api_row) == 3); // title, summary, switch
    lv_area_t title_box, summary_box, toggle_box, row_box;
    lv_obj_get_coords(lv_obj_get_child(s_control_api_row, 0), &title_box);
    lv_obj_get_coords(lv_obj_get_child(s_control_api_row, 1), &summary_box);
    lv_obj_get_coords(s_control_api_switch, &toggle_box); lv_obj_get_coords(s_control_api_row, &row_box);
    assert(title_box.x2 < toggle_box.x1 && summary_box.x2 < toggle_box.x1);
    assert(toggle_box.x2 <= row_box.x2 && toggle_box.y1 >= row_box.y1 && toggle_box.y2 <= row_box.y2);
    snapshot("developer-tools-off.ppm");
    lv_event_send(s_control_api_row, LV_EVENT_CLICKED, nullptr);
    assert(starts == 0 && stops == 0); // Only the explicit switch changes the service.
    toggle(true); state(true, true); summary("Starting...");
    assert(starts == 1 && s_control_start_pending && dialogs == 0);
    lv_event_send(s_control_api_switch, LV_EVENT_VALUE_CHANGED, nullptr);
    assert(starts == 1); state(true, true);
    snapshot("developer-tools-starting.ppm");
    server_enabled = true; crystal_shell_control_start_event("123456");
    state(true, false); summary("Enabled for this boot");
    assert(!s_control_start_pending && dialogs == 1 && pairing_visible);
    assert(shown_url == "http://192.168.1.158/");
    snapshot("developer-tools-on.ppm");
    control_pair_dialog_close(); page_close(); settings_push_developer_tools();
    lv_obj_update_layout(lv_scr_act()); state(true, false);
    assert(std::strstr(lv_label_get_text(s_control_status_label), "enabled"));
    toggle(false); state(false, false); summary("Off - enable to pair a browser");
    assert(stops == 1 && !server_enabled && !pairing_visible);

    for (bool missing_wifi : {false, true}) {
        reset(); wifi.connected = false; if (missing_wifi) services.wifi = nullptr;
        toggle(true); state(false, false); assert(starts == 0 && !s_control_start_pending);
        assert(std::strstr(lv_label_get_text(s_control_status_label), "Connect"));
    }
    reset(); fail_task = true; toggle(true); state(false, false);
    assert(!s_control_start_pending && starts == 1); summary("Off - enable to pair a browser");
    fail_task = false; toggle(true); state(true, true);
    crystal_shell_control_start_event(nullptr); state(false, false);
    assert(!s_control_start_pending && dialogs == 0);
    snapshot("developer-tools-failed.ppm");
    toggle(true); server_enabled = true; crystal_shell_control_start_event("654321");
    state(true, false); assert(dialogs == 1);

    reset(); toggle(true); page_close();
    server_enabled = true; crystal_shell_control_start_event("123456");
    assert(!s_control_start_pending && !server_enabled && stops == 1 && dialogs == 0);
    settings_push_developer_tools(); lv_obj_update_layout(lv_scr_act()); state(false, false);
    toggle(true); page_close(); settings_push_developer_tools();
    lv_obj_update_layout(lv_scr_act()); state(true, true);
    server_enabled = true; crystal_shell_control_start_event("123456");
    state(true, false); assert(dialogs == 1);
    reset(); wifi.connected=false; ethernet.has_ip=true;
    toggle(true);state(true,true);assert(starts==1);
    server_enabled=true;crystal_shell_control_start_event("123456");state(true,false);
    assert(dialogs==1 && shown_url=="http://192.168.1.158/");
    toggle(false);assert(!server_enabled);
    page_close(); fail_page = true; settings_push_developer_tools(); assert(!s_developer_tools_page);
    puts("PASS: real LVGL resource monitor visibility and saved preference, panel toggle layout, startup lock, pairing, disable, no-Wi-Fi rollback, task/server failures, retry, and page exit/reopen");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    source = SOURCE.read_text()
    # Phase 1.5 moved layout literals into target constants. Exercise the S3
    # values used by this 480px fixture directly from production definitions.
    target_block = source.split("constexpr lv_coord_t kHomePillWidth = 216;", 1)[1]
    target_block = target_block.split("#else", 1)[1].split("#endif", 1)[0]
    functions = '\n'.join(function(source, signature) for signature in (
        'void settings_button_feedback(lv_obj_t *button)',
        'lv_obj_t *settings_row(lv_obj_t *parent, const char *label, const char *summary = nullptr)',
        'void settings_row_set_summary(lv_obj_t *row, const char *summary)',
        'lv_obj_t *settings_status_label(lv_obj_t *parent)',
        'void settings_row_reserve_right(lv_obj_t *row, lv_coord_t control_width)',
        'lv_obj_t *settings_switch(lv_obj_t *row, bool checked)',
        'void control_panel_toggle_changed(lv_event_t *event)',
        'void settings_push_developer_tools()',
        'void crystal_shell_control_start_event(const char pairing_pin[7])',
    ))
    constants = "\n".join(
        line for line in target_block.splitlines()
        if (match := re.match(r"constexpr .*[* ](kSettings\w+)\s*=", line))
        and re.search(r"\b" + match[1] + r"\b", functions)
    )
    core = (ROOT / 'components/crystal_core/src/crystal_core.cpp').read_text()
    timeout_code = 'std::atomic<int32_t> s_cache_lua_extended{-1};\n'
    for signature in ('T stored_value(const char *key, T fallback)',
                      'T cached_value(const char *key, T fallback, std::atomic<int32_t> &cache)',
                      'uint32_t crystal_lua_callback_timeout_seconds()',
                      'bool crystal_lua_set_extended_timeout(bool enabled)'):
        if signature.startswith('T '): timeout_code += 'template <typename T>\n'
        timeout_code += function(core, signature)
    with tempfile.TemporaryDirectory(prefix='crystal-developer-tools-') as directory:
        temp = Path(directory)
        (temp / 'lv_conf.h').write_text(CONF + '\n#define LV_USE_PERF_MONITOR 1\n')
        (temp / 'sdkconfig.h').write_text('#pragma once\n#define CONFIG_IDF_TARGET_ESP32P4 0\n')
        (temp / 'esp_log.h').write_text('#pragma once\n')
        (temp / 'crystal_hal.hpp').write_text('#pragma once\n')
        metrics = ROOT / 'components/crystal_shell/src/crystal_ui_metrics.cpp'
        (temp / 'check.cpp').write_text(FIXTURE + f'\n#include "{metrics}"\n' + constants + timeout_code + functions + CHECK)
        (temp / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.16)
project(developer_tools_test LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(LV_CONF_PATH "{temp}/lv_conf.h" CACHE STRING "" FORCE)
add_subdirectory("{ROOT}/managed_components/lvgl__lvgl" lvgl)
add_executable(check check.cpp)
target_include_directories(check PRIVATE "{temp}" "{ROOT}/components/crystal_shell/include")
target_compile_options(check PRIVATE -Wall -Wextra -Werror)
target_link_libraries(check PRIVATE lvgl)
''')
        subprocess.run(['cmake', '-S', str(temp), '-B', str(temp / 'build')], check=True)
        subprocess.run(['cmake', '--build', str(temp / 'build'), '--target', 'check', '-j', '8'], check=True)
        output = args.output_dir or temp
        output.mkdir(parents=True, exist_ok=True)
        subprocess.run([str(temp / 'build/check')], check=True, cwd=output)
        for state in ('off', 'starting', 'on', 'failed'):
            png_from_ppm(output / f'developer-tools-{state}.ppm')
        if args.output_dir:
            print(f'Rendered previews: {output.resolve()}')


if __name__ == '__main__':
    main()
