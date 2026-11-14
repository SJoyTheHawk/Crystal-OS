#!/usr/bin/env python3
"""Render production pairing dialogs and verify P4/S3 typography and dismissal."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from test_developer_tools_page import function
from test_manage_apps_page import CONF, png_from_ppm

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "lvgl.h"
static lv_obj_t *s_control_pair_dialog;
static bool modal_open;
void crystal_shell_set_modal_open(bool open){modal_open=open;}
'''
CHECK = r'''
static void flush(lv_disp_drv_t *display,const lv_area_t *,lv_color_t *){lv_disp_flush_ready(display);}
static void advance(){for(unsigned i=0;i<40;i++){lv_tick_inc(10);lv_timer_handler();}}
static lv_area_t box(lv_obj_t *object){lv_area_t area;lv_obj_get_coords(object,&area);return area;}
static void snapshot(const char *path){
    auto *shot=lv_snapshot_take(lv_layer_top(),LV_IMG_CF_TRUE_COLOR);assert(shot);
    auto *file=std::fopen(path,"wb");assert(file);
    std::fprintf(file,"P6\n%u %u\n255\n",shot->header.w,shot->header.h);
    auto *pixels=reinterpret_cast<const lv_color_t *>(shot->data);
    for(unsigned i=0;i<shot->header.w*shot->header.h;++i){
        lv_color32_t color;color.full=lv_color_to32(pixels[i]);
        const unsigned char rgb[]={color.ch.red,color.ch.green,color.ch.blue};std::fwrite(rgb,1,3,file);
    }
    std::fclose(file);lv_snapshot_free(shot);
}
int main(){
    constexpr bool p4=CONFIG_IDF_TARGET_ESP32P4;
    const int resolution=p4?720:480;
    lv_init();static lv_color_t pixels[720*20];static lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer,pixels,nullptr,720*20);static lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);driver.hor_res=driver.ver_res=resolution;
    driver.draw_buf=&buffer;driver.flush_cb=flush;auto *display=lv_disp_drv_register(&driver);
    lv_obj_set_style_bg_color(lv_scr_act(),lv_color_hex(0x11151b),0);
    const char *urls[]={"http://192.168.1.233/","http://255.255.255.255/","http://crystal-os.local/"};
    for(const char *url:urls){
        control_pair_dialog_build("123456",url);advance();assert(modal_open);
        auto *card=lv_obj_get_child(s_control_pair_dialog,0);
        assert(lv_obj_get_width(card)==(p4?600:400));
        assert(lv_obj_get_height(card)==(p4?495:330));
        assert(lv_obj_get_child_cnt(card)==5);
        auto *title=lv_obj_get_child(card,0),*code=lv_obj_get_child(card,1);
        auto *detail=lv_obj_get_child(card,2),*notice=lv_obj_get_child(card,3);
        auto *done=lv_obj_get_child(card,4),*done_label=lv_obj_get_child(done,0);
        assert(lv_obj_get_style_text_font(title,0)==(p4?&lv_font_montserrat_28:&lv_font_montserrat_20));
        assert(lv_obj_get_style_text_font(code,0)==(p4?&lv_font_montserrat_48:&lv_font_montserrat_28));
        assert(lv_obj_get_style_text_font(detail,0)==(p4?&lv_font_montserrat_24:&lv_font_montserrat_16));
        assert(lv_obj_get_style_text_font(notice,0)==(p4?&lv_font_montserrat_20:&lv_font_montserrat_14));
        assert(lv_obj_get_width(done)==(p4?210:140) && lv_obj_get_height(done)==(p4?63:42));
        assert(!lv_obj_has_flag(card,LV_OBJ_FLAG_SCROLLABLE));
        const auto c=box(card),t=box(title),n=box(code),d=box(detail),w=box(notice),b=box(done),label=box(done_label);
        assert(c.x1>=0 && c.y1>=0 && c.x2<resolution && c.y2<resolution);
        assert(t.y2<n.y1 && n.y2<d.y1 && d.y2<w.y1 && w.y2<b.y1);
        assert(label.x1>=b.x1 && label.x2<=b.x2 && label.y1>=b.y1 && label.y2<=b.y2);
        for(auto *object:{title,code,detail,notice,done}){
            const auto r=box(object);assert(r.x1>c.x1 && r.x2<c.x2 && r.y1>c.y1 && r.y2<c.y2);
        }
        if(p4){
            assert(lv_obj_get_style_text_font(done_label,0)==&lv_font_montserrat_24);
            lv_point_t required;
            lv_txt_get_size(&required,lv_label_get_text(detail),lv_obj_get_style_text_font(detail,0),
                lv_obj_get_style_text_letter_space(detail,0),lv_obj_get_style_text_line_space(detail,0),
                lv_obj_get_content_width(detail),LV_TEXT_FLAG_NONE);
            if(required.y>lv_obj_get_content_height(detail))
                std::fprintf(stderr,"detail needs %dpx, has %dpx\n",required.y,lv_obj_get_content_height(detail));
            assert(required.y<=lv_obj_get_content_height(detail));
        }
        if(url==urls[0])snapshot(p4?"pairing-p4.ppm":"pairing-s3.ppm");
        lv_event_send(done,LV_EVENT_CLICKED,nullptr);advance();
        assert(!s_control_pair_dialog && !modal_open);
    }
    control_pair_dialog_build("654321",urls[0]);
    control_pair_dialog_build("123456",urls[1]);advance();
    assert(lv_obj_get_child_cnt(lv_layer_top())==1);
    control_pair_dialog_close();control_pair_dialog_close();advance();
    assert(!modal_open && lv_obj_get_child_cnt(lv_layer_top())==0);
    lv_disp_remove(display);
    std::printf("PASS: %s production pairing geometry, typography, URL wrapping, non-overlap, Done and rebuild cleanup\n",p4?"P4":"S3");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    source = (ROOT/'components/crystal_shell/src/crystal_shell.cpp').read_text()
    functions = '\n'.join(function(source, signature) for signature in (
        'static void control_pair_dialog_close()',
        'static void control_pair_dialog_build(const char *pin, const char *url)',
    ))
    with tempfile.TemporaryDirectory(prefix='crystal-pairing-') as directory:
        temp = Path(directory)
        (temp/'lv_conf.h').write_text(CONF+'\n#define LV_FONT_MONTSERRAT_24 1\n#define LV_FONT_MONTSERRAT_28 1\n#define LV_FONT_MONTSERRAT_48 1\n')
        (temp/'check.cpp').write_text(FIXTURE+functions+CHECK)
        cmake = f'''cmake_minimum_required(VERSION 3.16)
project(pairing_dialog_test LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(LV_CONF_PATH "{temp}/lv_conf.h" CACHE STRING "" FORCE)
add_subdirectory("{ROOT}/managed_components/lvgl__lvgl" lvgl)
'''
        for target, value in (('s3', 0), ('p4', 1)):
            cmake += f'''
add_executable(check_{target} check.cpp)
target_compile_definitions(check_{target} PRIVATE CONFIG_IDF_TARGET_ESP32P4={value})
target_compile_options(check_{target} PRIVATE -Wall -Wextra -Werror -g -fsanitize=address,undefined)
target_link_options(check_{target} PRIVATE -fsanitize=address,undefined)
target_link_libraries(check_{target} PRIVATE lvgl)
'''
        (temp/'CMakeLists.txt').write_text(cmake)
        subprocess.run(['cmake', '-S', str(temp), '-B', str(temp/'build')], check=True)
        subprocess.run(['cmake', '--build', str(temp/'build'), '--target', 'check_s3', 'check_p4', '-j', '8'], check=True)
        output = args.output_dir or temp
        output.mkdir(parents=True, exist_ok=True)
        for target in ('s3', 'p4'):
            subprocess.run([str(temp/f'build/check_{target}')], check=True, cwd=output)
            png_from_ppm(output/f'pairing-{target}.ppm')


if __name__ == '__main__':
    main()
