#!/usr/bin/env python3
"""Test production Wi-Fi view functions with real LVGL and a deterministic HAL.

Radio RPC/task execution is a fixture; device tests must verify actual scanning.
"""
from pathlib import Path
import subprocess
import tempfile
import argparse
import json
from test_p4_visuals import CONF
from test_manage_apps_page import png_from_ppm

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'components/crystal_shell/src/crystal_shell.cpp'

def function(source, signature):
    start = source.index(signature + '\n{')
    return source[start:source.index('\n}\n', start) + 3]

FIXTURE = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>
#include "lvgl.h"
#include "crystal_shell.hpp"
struct CrystalVisualStyle { unsigned background=0x11151b, surface=0x1b2028, elevated=0x252a30, text=0xf2f4f7, secondary=0xcbd5e1, muted=0x94a3b8, accent=0x3b82f6, success=0x53c8b6; };
const CrystalVisualStyle &crystal_visual_style() { static CrystalVisualStyle s; return s; }
struct IWifi {
    struct Network { char ssid[33]; int8_t rssi; bool secured; };
    std::vector<Network> results;
    bool radio=true, online=false;
    char saved[33]="";
    unsigned scans=0, connects=0;
    mutable unsigned connection_queries=0;
    bool enabled() const { return radio; }
    bool connected() const { ++connection_queries; return online; }
    const char *last_ssid() const { return saved; }
    size_t scan_results(Network *out, size_t n) { std::copy_n(results.begin(), std::min(n, results.size()), out); return results.size(); }
    void scan() { ++scans; }
    void connect(const char *ssid, const char *pass) { assert(!*pass); std::snprintf(saved, sizeof(saved), "%s", ssid); ++connects; }
};
struct Hal { IWifi *wifi; };
static IWifi radio;
static Hal services{&radio};
Hal &hal() { return services; }
static lv_obj_t *s_wifi_page, *s_wifi_page_list, *s_wifi_page_status, *s_wifi_scan_spinner, *s_wifi_refresh;
static lv_obj_t *s_wifi_dialog, *s_quick_panel;
static char s_wifi_selected[33];
static lv_obj_t *s_system_page_stack[4];
static size_t s_system_page_depth;
static lv_timer_t *s_wifi_scan_timeout;
static bool s_wifi_scanning;
static IWifi::Network s_wifi_rows[20];
static size_t s_wifi_row_count;
static char s_wifi_connecting[33];
static unsigned forgets;
constexpr int pdPASS=1, MALLOC_CAP_SPIRAM=1, MALLOC_CAP_8BIT=2;
static bool fail_task;
void vTaskDeleteWithCaps(void *) {}
int xTaskCreateWithCaps(void (*worker)(void *), const char *, unsigned, void *, unsigned, void *, unsigned) {
    if (fail_task) return 0;
    worker(nullptr); return pdPASS;
}
void crystal_shell_set_keyboard_open(bool) {}
void crystal_shell_set_modal_open(bool) {}
constexpr const char *TAG="test";
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
void wifi_open_forget_confirm(const char *) { ++forgets; }
constexpr int kSettingsContentInset=16, kWifiFieldHeight=56, kWifiListHeight=480, kWifiNetworkRowHeight=68;
constexpr int kWifiNetworkRowInset=24;
constexpr auto *kSettingsStatusFont=&lv_font_montserrat_20;
constexpr auto *kWifiTitleFont=&lv_font_montserrat_28;
constexpr auto *kWifiBodyFont=&lv_font_montserrat_20;
lv_obj_t *system_page_push(const char *) {
    auto *p=lv_obj_create(lv_layer_top()); lv_obj_set_size(p,720,680); lv_obj_set_pos(p,0,40);
    lv_obj_set_style_pad_all(p,0,0);lv_obj_set_style_border_width(p,0,0);
    lv_obj_clear_flag(p,LV_OBJ_FLAG_SCROLLABLE);
    auto *content=lv_obj_create(p);lv_obj_set_size(content,LV_PCT(100),580);
    lv_obj_align(content,LV_ALIGN_TOP_MID,0,100);
    lv_obj_set_style_border_width(content,0,0);
    lv_obj_set_style_pad_all(content,16,0);lv_obj_set_style_pad_row(content,8,0);
    lv_obj_set_flex_flow(content,LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(content,LV_OBJ_FLAG_SCROLL_ELASTIC|LV_OBJ_FLAG_SCROLL_MOMENTUM);
    s_system_page_stack[s_system_page_depth++]=p; return content;
}
'''
CHECK = r'''
static void flush(lv_disp_drv_t *d,const lv_area_t *,lv_color_t *) { lv_disp_flush_ready(d); }
static void advance(int ms) { for(int i=0;i<ms;i+=10) { lv_tick_inc(10);lv_timer_handler(); } }
static void snapshot(const char *path) {
    auto *shot=lv_snapshot_take(lv_layer_top(),LV_IMG_CF_TRUE_COLOR);assert(shot);
    auto *file=std::fopen(path,"wb");assert(file);
    std::fprintf(file,"P6\n%u %u\n255\n",shot->header.w,shot->header.h);
    const auto *pixels=reinterpret_cast<const lv_color_t *>(shot->data);
    for(size_t i=0;i<static_cast<size_t>(shot->header.w)*shot->header.h;++i) {
        lv_color32_t color;color.full=lv_color_to32(pixels[i]);
        const unsigned char rgb[]={color.ch.red,color.ch.green,color.ch.blue};
        std::fwrite(rgb,1,sizeof(rgb),file);
    }
    std::fclose(file);lv_snapshot_free(shot);
}
static void close_page() {
    wifi_scan_busy(false); lv_obj_del(s_wifi_page);
    s_wifi_page=s_wifi_page_list=s_wifi_page_status=s_wifi_scan_spinner=s_wifi_refresh=nullptr;
    s_system_page_depth=0;
}
int main() {
    lv_init(); static lv_color_t px[720*20]; static lv_disp_draw_buf_t buf;
    lv_disp_draw_buf_init(&buf,px,nullptr,720*20); static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);drv.hor_res=drv.ver_res=720;drv.draw_buf=&buf;drv.flush_cb=flush;
    auto *display=lv_disp_drv_register(&drv);
    wifi_page_open();advance(40);
    assert(s_wifi_scanning && radio.scans==1 && lv_obj_has_state(s_wifi_refresh,LV_STATE_DISABLED));
    wifi_scan_start();assert(radio.scans==1);
    radio.results={{"Weak",-90,true},{"Strong",-40,true},{"Weak",-50,true},{"Cafe",-65,false},{"",-20,true}};
    wifi_scan_busy(false);radio.connection_queries=0;wifi_page_fill_list();advance(40);
    assert(radio.connection_queries==1); // C6 calls cannot scale with rows/sort comparisons
    assert(s_wifi_row_count==3);
    assert(!std::strcmp(s_wifi_rows[0].ssid,"Strong"));
    assert(!std::strcmp(s_wifi_rows[1].ssid,"Weak") && s_wifi_rows[1].rssi==-50);
    assert(lv_obj_has_flag(s_wifi_scan_spinner,LV_OBJ_FLAG_HIDDEN));
    for(unsigned i=0;i<3;++i) {
        auto *row=lv_obj_get_child(s_wifi_page_list,i);
        lv_area_t row_bounds{},list_bounds{};
        lv_obj_get_coords(row,&row_bounds);lv_obj_get_coords(s_wifi_page_list,&list_bounds);
        assert(row_bounds.x1==list_bounds.x1 && row_bounds.x2==list_bounds.x2);
        auto *bars=lv_obj_get_child(row,0);
        lv_area_t bars_bounds{};lv_obj_get_coords(bars,&bars_bounds);
        assert(bars_bounds.x1-row_bounds.x1>=kWifiNetworkRowInset);
        auto *name=lv_obj_get_child(row,1);auto *detail=lv_obj_get_child(row,2);
        lv_area_t n{},d{};lv_obj_get_coords(name,&n);lv_obj_get_coords(detail,&d);
        assert(row_bounds.x2-d.x2>=kWifiNetworkRowInset);
        assert(n.x2<d.x1 && d.x2<720); // long SSID and right-side details never collide
    }
    radio.results={{"Changed after rendering",-20,true}};
    lv_event_send(lv_obj_get_child(s_wifi_page_list,0),LV_EVENT_CLICKED,nullptr);
    assert(!std::strcmp(s_wifi_selected,"Strong")); // callbacks retain rendered snapshot
    assert(s_wifi_dialog && crystal_keyboard_is_open());
    auto *dialog=s_wifi_dialog;
    auto *field=lv_obj_get_child(dialog,2);
    auto *reveal=lv_obj_get_child(dialog,3);
    auto *connect=lv_obj_get_child(dialog,4);
    auto *cancel=lv_obj_get_child(dialog,5);
    advance(300);
    lv_area_t input_area{},reveal_area{},dialog_open{},list_open{};
    lv_obj_get_coords(field,&input_area);lv_obj_get_coords(reveal,&reveal_area);
    lv_obj_get_coords(dialog,&dialog_open);lv_obj_get_coords(s_wifi_page_list,&list_open);
    assert(reveal_area.x1-input_area.x2<=13 && input_area.x2<reveal_area.x1);
    assert(dialog_open.y2<crystal_keyboard_reserved_top());
    assert(crystal_keyboard_reserved_top()==420);
    assert(lv_obj_get_style_bg_color(connect,0).full==lv_color_hex(0x3b82f6).full);
    assert(lv_obj_get_style_bg_color(cancel,0).full==lv_color_hex(0x334155).full);
    assert(lv_textarea_get_password_mode(field));
    snapshot("wifi-keyboard-open.ppm");
    lv_event_send(reveal,LV_EVENT_CLICKED,nullptr);advance(40);
    assert(!lv_textarea_get_password_mode(field));
    // Visibility controls must keep the keyboard in place while typing.
    assert(crystal_keyboard_is_open());
    lv_event_send(dialog,LV_EVENT_CLICKED,nullptr);advance(300);
    assert(!crystal_keyboard_is_open());
    lv_area_t dialog_closed{},list_closed{};
    lv_obj_get_coords(dialog,&dialog_closed);lv_obj_get_coords(s_wifi_page_list,&list_closed);
    assert(dialog_closed.y1==(720-kWifiDialogHeight)/2);
    assert(list_open.y1==list_closed.y1 && list_open.y2==list_closed.y2);
    assert(list_closed.y2>=700); // list reclaims the full page below the keyboard
    snapshot("wifi-keyboard-closed.ppm");
    lv_event_send(field,LV_EVENT_CLICKED,nullptr);advance(300);
    assert(crystal_keyboard_is_open());
    wifi_close_credentials();advance(40);
    assert(!crystal_keyboard_is_open() && !s_wifi_dialog);
    lv_event_send(lv_obj_get_child(s_wifi_page_list,2),LV_EVENT_CLICKED,nullptr);
    assert(radio.connects==1 && !std::strcmp(radio.saved,"Cafe"));
    radio.online=true;std::strcpy(radio.saved,"Weak");
    radio.results={{"Strong",-40,true},{"Weak",-90,true}};
    wifi_page_fill_list();assert(!std::strcmp(s_wifi_rows[0].ssid,"Weak"));
    lv_event_send(lv_obj_get_child(s_wifi_page_list,0),LV_EVENT_CLICKED,nullptr);assert(forgets==1);
    wifi_scan_start();advance(15100);
    assert(!s_wifi_scanning && !s_wifi_scan_timeout);
    assert(std::strstr(lv_label_get_text(s_wifi_page_status),"timed out"));
    fail_task=true;wifi_scan_start();assert(!s_wifi_scanning && !s_wifi_scan_timeout);fail_task=false;
    close_page();radio.radio=false;wifi_page_open();assert(!s_wifi_scanning);
    assert(std::strstr(lv_label_get_text(s_wifi_page_status),"off"));
    close_page();radio.radio=true;radio.results.clear();wifi_page_open();wifi_scan_busy(false);wifi_page_fill_list();
    assert(!s_wifi_row_count && std::strstr(lv_label_get_text(s_wifi_page_status),"No networks"));
    close_page();lv_disp_remove(display);
    std::puts("PASS: bounded connection queries, WiFi scan/refresh/failure/timeout, signal rows, stable SSID selection, connected sorting, password colors/spacing/toggle, keyboard dismissal/reopen, full-height list restoration");
}
'''

GEOMETRY = r'''
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include "crystal_shell.hpp"
void crystal_shell_set_keyboard_open(bool) {}
static void flush(lv_disp_drv_t *d,const lv_area_t *,lv_color_t *) { lv_disp_flush_ready(d); }
static void advance() { for(int i=0;i<30;++i) { lv_tick_inc(10);lv_timer_handler(); } }
int main() {
    lv_init();static lv_color_t pixels[720*20];static lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer,pixels,nullptr,720*20);static lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);driver.hor_res=driver.ver_res=CONFIG_IDF_TARGET_ESP32P4 ? 720 : 480;
    driver.draw_buf=&buffer;driver.flush_cb=flush;auto *display=lv_disp_drv_register(&driver);
    auto *root=lv_obj_create(lv_scr_act());lv_obj_remove_style_all(root);
    const int original_height=driver.ver_res-40;
    lv_obj_set_size(root,driver.hor_res,original_height);
    auto *field=lv_textarea_create(root);lv_textarea_set_one_line(field,true);
    lv_textarea_set_text(field,"");lv_obj_set_size(field,200,40);
    lv_obj_set_pos(field,10,driver.ver_res/2);advance();
    assert(crystal_keyboard_show(field,root));advance();
    auto *keyboard=lv_obj_get_child(lv_layer_top(),-1);
    assert(lv_obj_check_type(keyboard,&lv_keyboard_class));
    assert(lv_obj_get_height(keyboard)==(CONFIG_IDF_TARGET_ESP32P4 ? 261 : 174));
    assert(crystal_keyboard_reserved_top()==(CONFIG_IDF_TARGET_ESP32P4 ? 420 : 280));
    assert(lv_obj_get_style_text_font(keyboard,LV_PART_ITEMS)->line_height==
           (CONFIG_IDF_TARGET_ESP32P4 ? lv_font_montserrat_24.line_height : lv_font_montserrat_16.line_height));
    auto *matrix=reinterpret_cast<lv_btnmatrix_t *>(keyboard);
    const auto first=matrix->button_areas[0];
    const int modes[]={LV_KEYBOARD_MODE_TEXT_LOWER,LV_KEYBOARD_MODE_TEXT_UPPER,
                       LV_KEYBOARD_MODE_SPECIAL,LV_KEYBOARD_MODE_USER_1};
    for(int mode : modes) {
        lv_keyboard_set_mode(keyboard,static_cast<lv_keyboard_mode_t>(mode));advance();
        assert(matrix->btn_cnt==33);
        for(unsigned i=0;i<matrix->btn_cnt;++i) {
            const auto &area=matrix->button_areas[i];
            assert(std::abs(lv_area_get_height(&area)-lv_area_get_height(&first))<=1);
            assert(area.x1>=0 && area.x2<driver.hor_res);
            assert(area.y1>=0 && area.y2<lv_obj_get_height(keyboard));
        }
    }
    lv_keyboard_set_mode(keyboard,LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_btnmatrix_set_selected_btn(keyboard,0);lv_event_send(keyboard,LV_EVENT_VALUE_CHANGED,nullptr);
    assert(!std::strcmp(lv_textarea_get_text(field),"q"));
    lv_event_send(keyboard,LV_EVENT_READY,nullptr);advance();assert(!crystal_keyboard_is_open());
    assert(lv_obj_get_height(root)==original_height);
    assert(crystal_keyboard_show(field,root));advance();crystal_keyboard_hide();advance();
    assert(lv_obj_get_height(root)==original_height);
    std::printf("{\"width\":%d,\"height\":%d}\n",lv_area_get_width(&first),lv_area_get_height(&first));
    lv_obj_del(root);lv_disp_remove(display);
}
'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',type=Path)
    args=parser.parse_args()
    source = SOURCE.read_text()
    signatures = ['void settings_button_feedback(lv_obj_t *button)',
                  'static void wifi_dialog_place(bool keyboard_open)', 'static void wifi_close_credentials()',
                  'static void wifi_open_credentials(const char *ssid)',
                  'static void wifi_scan_busy(bool busy)', 'static void wifi_scan_worker(void *)',
                  'static void wifi_scan_start()', 'void wifi_page_open()', 'static void wifi_page_fill_list()']
    constants = source.split('constexpr lv_coord_t kWifiDialogWidth = 600;',1)[1].split('constexpr lv_coord_t kWifiListHeight',1)[0]
    constants = 'constexpr lv_coord_t kWifiDialogWidth = 600;' + constants
    constants = '\n'.join(line for line in constants.splitlines() if 'kWifiFieldHeight' not in line)
    code = FIXTURE + constants + '\n'.join(function(source, name) for name in signatures) + CHECK
    with tempfile.TemporaryDirectory(prefix='crystal-wifi-view-') as directory:
        temp = Path(directory)
        (temp / 'lv_conf.h').write_text(CONF + '\n#define LV_FONT_MONTSERRAT_24 1\n#define LV_FONT_MONTSERRAT_28 1\n#define LV_COLOR_SCREEN_TRANSP 1\n')
        (temp / 'check.cpp').write_text(code)
        (temp / 'geometry.cpp').write_text(GEOMETRY)
        (temp / 'crystal_shell.hpp').write_text('''#pragma once
#include "lvgl.h"
using crystal_keyboard_state_cb_t = void (*)(bool, void *);
bool crystal_keyboard_show(lv_obj_t *, lv_obj_t *);
void crystal_keyboard_hide();
bool crystal_keyboard_is_open();
lv_coord_t crystal_keyboard_reserved_top();
void crystal_keyboard_set_state_cb(crystal_keyboard_state_cb_t, void *);
void crystal_shell_set_keyboard_open(bool);
''')
        (temp / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.16)
project(wifi_view_test LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(LV_CONF_PATH "{temp}/lv_conf.h" CACHE STRING "" FORCE)
add_subdirectory("{ROOT}/managed_components/lvgl__lvgl" lvgl)
add_executable(check check.cpp "{ROOT}/components/crystal_shell/src/crystal_keyboard.cpp")
target_include_directories(check PRIVATE "{temp}")
target_compile_definitions(check PRIVATE CONFIG_IDF_TARGET_ESP32P4=1)
target_compile_options(check PRIVATE -g -fsanitize=address)
target_link_options(check PRIVATE -fsanitize=address)
target_link_libraries(check PRIVATE lvgl)
''')
        with (temp/'CMakeLists.txt').open('a') as cmake:
            for target,value in [('s3',0),('p4',1)]:
                cmake.write(f'''
add_executable(geometry_{target} geometry.cpp "{ROOT}/components/crystal_shell/src/crystal_keyboard.cpp")
target_include_directories(geometry_{target} PRIVATE "{temp}")
target_compile_definitions(geometry_{target} PRIVATE CONFIG_IDF_TARGET_ESP32P4={value})
target_compile_options(geometry_{target} PRIVATE -g -fsanitize=address)
target_link_options(geometry_{target} PRIVATE -fsanitize=address)
target_link_libraries(geometry_{target} PRIVATE lvgl)
''')
        subprocess.run(['cmake', '-S', str(temp), '-B', str(temp/'build')], check=True)
        subprocess.run(['cmake','--build',str(temp/'build'),'--target','check','geometry_s3','geometry_p4','-j','8'], check=True)
        subprocess.run([str(temp/'build/check')], check=True, cwd=temp)
        geometry={target:json.loads(subprocess.check_output([str(temp/f'build/geometry_{target}')],text=True))
                  for target in ('s3','p4')}
        for dimension in ('width','height'):
            assert abs(geometry['p4'][dimension]/1.5-geometry['s3'][dimension])<=1,geometry
        print('PASS: S3/P4 key touch areas match physical proportions, four keyboard planes and Done/reopen restore the viewport:',geometry)
        if args.output_dir:
            args.output_dir.mkdir(parents=True,exist_ok=True)
            for path in temp.glob('*.ppm'):
                output=args.output_dir/path.name
                output.write_bytes(path.read_bytes())
                png_from_ppm(output)

if __name__ == '__main__':
    main()
