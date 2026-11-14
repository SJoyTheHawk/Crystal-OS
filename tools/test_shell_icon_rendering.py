#!/usr/bin/env python3
"""Render production launcher/switcher/network icons and check corner/edge pixels."""
from pathlib import Path
import subprocess
import tempfile
from test_launcher_swipe import CONF
from test_developer_tools_page import function
ROOT=Path(__file__).resolve().parents[1]
CHECK=r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include "systems/phone/esp_brookesia_phone.hpp"
LV_IMG_DECLARE(crystal_ethernet_status_icon);
enum class CrystalNetworkType { None, Wifi, Ethernet };
static CrystalNetworkType active;
static CrystalNetworkType crystal_network_active_type(){return active;}
// CALLBACK
static lv_obj_t *switcher(lv_obj_t *incoming_card,const void *icon_resource,lv_coord_t height){
    lv_obj_t *identity_icon=nullptr;
// SWITCHER
    return identity_icon;
}
static void flush(lv_disp_drv_t *d,const lv_area_t *,lv_color_t *){lv_disp_flush_ready(d);}
static void touch(lv_indev_drv_t *,lv_indev_data_t *d){d->state=LV_INDEV_STATE_RELEASED;}
static void advance(){for(int i=0;i<10;++i){lv_tick_inc(10);lv_timer_handler();}}
static lv_obj_t *find_image(lv_obj_t *root,const void *src){
    if(lv_obj_check_type(root,&lv_img_class) && lv_img_get_src(root)==src)return root;
    for(uint32_t i=0;i<lv_obj_get_child_cnt(root);++i)
        if(auto *o=find_image(lv_obj_get_child(root,i),src))return o;
    return nullptr;
}
static lv_area_t box(lv_obj_t *o){lv_area_t a;lv_obj_get_coords(o,&a);return a;}
static lv_color_t pixel(lv_img_dsc_t *s,int x,int y){
    assert(x>=0 && y>=0 && x<s->header.w && y<s->header.h);
    return reinterpret_cast<const lv_color_t *>(s->data)[y*s->header.w+x];
}
static void check_round(lv_obj_t *root,lv_obj_t *image){
    lv_obj_update_layout(root);auto *s=lv_snapshot_take(root,LV_IMG_CF_TRUE_COLOR);assert(s);
    auto a=box(image),r=box(root);
    int x=a.x1-r.x1,y=a.y1-r.y1,w=lv_area_get_width(&a),h=lv_area_get_height(&a);
    assert(pixel(s,x+1,y+1).full==lv_color_black().full);
    assert(pixel(s,x+w/2,y+2).full==lv_color_white().full);
    assert(pixel(s,x+w/2,y+h/2).full==lv_color_white().full);
    lv_snapshot_free(s);
}
int main(){
    lv_init();static lv_color_t buffer[720*20];static lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw,buffer,nullptr,720*20);static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);drv.hor_res=drv.ver_res=720;drv.draw_buf=&draw;drv.flush_cb=flush;
    auto *display=lv_disp_drv_register(&drv);static lv_indev_drv_t input;lv_indev_drv_init(&input);
    input.type=LV_INDEV_TYPE_POINTER;input.read_cb=touch;auto *pointer=lv_indev_drv_register(&input);
    ESP_Brookesia_Phone phone(display);
    auto sheet=ESP_Brookesia_PhoneStylesheet_t ESP_BROOKESIA_PHONE_480_480_DARK_STYLESHEET();
    sheet.core.screen_size.width=sheet.core.screen_size.height=720;
    sheet.home.status_bar.data.main.text_font.size_px=24;
    assert(phone.addStylesheet(sheet));assert(phone.activateStylesheet(sheet));
    assert(phone.setTouchDevice(pointer));assert(phone.begin());advance();
    auto *bar=phone.getHome().getStatusBar();assert(bar);assert(bar->setVerticalOffset(2));
    // Run the production callback including first update already on Ethernet.
    for(auto type:{CrystalNetworkType::Ethernet,CrystalNetworkType::Wifi,CrystalNetworkType::Ethernet,CrystalNetworkType::None,CrystalNetworkType::Ethernet}){
        active=type;update_status_connectivity(&phone,type==CrystalNetworkType::Wifi);advance();
        auto *img=find_image(phone.getCoreHome().getSystemScreenObject(),&crystal_ethernet_status_icon);assert(img);
        auto *slot=lv_obj_get_parent(img),*area=lv_obj_get_parent(slot);
        assert(lv_obj_get_index(slot)==0);
        assert(lv_obj_has_flag(slot,LV_OBJ_FLAG_HIDDEN)==(type!=CrystalNetworkType::Ethernet));
        if(type==CrystalNetworkType::Ethernet){
            assert(lv_obj_has_flag(lv_obj_get_child(area,1),LV_OBJ_FLAG_HIDDEN));
            assert(box(slot).x2<box(lv_obj_get_child(area,2)).x1);
            assert(box(lv_obj_get_child(area,2)).x2<box(lv_obj_get_child(area,3)).x1);
            auto *s=lv_snapshot_take(slot,LV_IMG_CF_TRUE_COLOR);assert(s);unsigned lit=0;
            for(unsigned y=0;y<s->header.h;++y)for(unsigned x=0;x<s->header.w;++x){
                lv_color32_t c;c.full=lv_color_to32(pixel(s,x,y));if(c.ch.red>180 && c.ch.green>180 && c.ch.blue>180)++lit;
            }
            assert(lit>80);lv_snapshot_free(s);
        }
    }
    auto *panel=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel,500,500);lv_obj_set_style_bg_color(panel,lv_color_black(),0);lv_obj_set_style_bg_opa(panel,255,0);
    static lv_color_t white[64*64];for(auto &c:white)c=lv_color_white();
    lv_img_dsc_t artwork={};artwork.header.cf=LV_IMG_CF_TRUE_COLOR;artwork.header.w=artwork.header.h=64;
    artwork.data_size=sizeof(white);artwork.data=reinterpret_cast<const uint8_t *>(white);
    ESP_Brookesia_AppLauncherIconInfo_t info={};info.name="Test";info.image.resource=&artwork;info.id=999;
    auto data=phone.getHome().getData().app_launcher.data.icon;
    ESP_Brookesia_AppLauncherIcon launcher(phone,info,data);assert(launcher.begin(panel));advance();
    auto *img=find_image(panel,&artwork);assert(img);check_round(panel,img);
    auto original=box(img);auto caption=box(lv_obj_get_child(launcher.getMainObject(),1));
    for(auto event:{LV_EVENT_PRESSED,LV_EVENT_RELEASED,LV_EVENT_PRESSED,LV_EVENT_PRESS_LOST}){
        lv_event_send(img,event,nullptr);advance();check_round(panel,img);
        assert(box(lv_obj_get_child(launcher.getMainObject(),1)).y1==caption.y1);
        if(event==LV_EVENT_PRESSED)assert(lv_obj_get_width(img)<lv_area_get_width(&original));
        else assert(lv_obj_get_width(img)==lv_area_get_width(&original));
    }
    assert(launcher.del());
    // Source-edge markers must survive 1.5x scaling inside the rounded frame.
    for(int y=0;y<64;++y)for(int x=0;x<64;++x)
        if(x<4 || y<4 || x>=60 || y>=60)white[y*64+x]=lv_color_hex(0xff0000);
    auto *frame=switcher(panel,&artwork,400);assert(frame);lv_obj_clear_flag(frame,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(frame,100,100);advance();
    assert(lv_obj_get_width(frame)>=96 && lv_obj_get_height(frame)>=96);
    auto *s=lv_snapshot_take(panel,LV_IMG_CF_TRUE_COLOR);assert(s);auto a=box(frame),r=box(panel);
    int x=a.x1-r.x1,y=a.y1-r.y1,w=lv_area_get_width(&a),h=lv_area_get_height(&a);
    assert(pixel(s,x+1,y+1).full==lv_color_black().full);
    assert(pixel(s,x+w/2,y+2).full==lv_color_hex(0xff0000).full);
    // LVGL includes transparent antialias padding in transformed bounds.
    // Require each red source edge within the outer eight rendered pixels.
    bool top=false,bottom=false,left=false,right=false;
    for(int i=0;i<8;++i){
        auto red=lv_color_hex(0xff0000).full;
        top |= pixel(s,x+w/2,y+i).full==red;
        bottom |= pixel(s,x+w/2,y+h-1-i).full==red;
        left |= pixel(s,x+i,y+h/2).full==red;
        right |= pixel(s,x+w-1-i,y+h/2).full==red;
    }
    assert(top && bottom && left && right);
    lv_snapshot_free(s);lv_obj_del(panel);assert(phone.del());
    std::puts("PASS: rendered pressed/released/lost-press corners, stable captions, complete zoomed switcher edges, visible Ethernet and network/battery ordering across route changes");
}
'''
def main():
    shell=(ROOT/'components/crystal_shell/src/crystal_shell.cpp').read_text()
    start=shell.index('            identity_icon = lv_obj_create(incoming_card);',shell.index('bool begin_card_transition'))
    end=shell.index('\n        }\n        identity_name',start)
    callback=function((ROOT/'main/main.cpp').read_text(),'static void update_status_connectivity(void *context, bool connected)')
    code=CHECK.replace('// CALLBACK',callback).replace('// SWITCHER',shell[start:end])
    with tempfile.TemporaryDirectory(prefix='crystal-icon-render-') as directory:
        temp=Path(directory);(temp/'check.cpp').write_text(code)
        (temp/'lv_conf.h').write_text(CONF+'\n#define LV_FONT_MONTSERRAT_24 1\n#define LV_FONT_MONTSERRAT_28 1\n')
        brookesia=ROOT/'components/esp-brookesia/src'
        (temp/'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.16)
project(icon_render_test LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(LV_CONF_PATH "{temp}/lv_conf.h" CACHE STRING "" FORCE)
add_subdirectory("{ROOT}/managed_components/lvgl__lvgl" lvgl)
file(GLOB_RECURSE SOURCES "{brookesia}/core/*.cpp" "{brookesia}/core/*.c" "{brookesia}/widgets/*.cpp" "{brookesia}/systems/phone/*.cpp" "{brookesia}/assets/*.c")
add_executable(check check.cpp {chr(36)}{{SOURCES}} "{ROOT}/components/ethernet_app/src/ethernet_icon.c")
target_include_directories(check PRIVATE "{brookesia}" "{temp}")
target_compile_definitions(check PRIVATE CONFIG_IDF_TARGET_ESP32P4=1 ESP_BROOKESIA_KCONFIG_IGNORE ESP_BROOKESIA_CONF_SKIP ESP_BROOKESIA_SQUARELINE_USE_INTERNAL_UI_HELPERS=1 ESP_BROOKESIA_SQ1_4_1_LV8_3_11)
target_compile_options(check PRIVATE -g -fsanitize=address -Wno-c99-designator -Wno-reorder-init-list)
target_link_options(check PRIVATE -fsanitize=address)
target_link_libraries(check PRIVATE lvgl)
''')
        subprocess.run(['cmake','-S',str(temp),'-B',str(temp/'build')],check=True)
        subprocess.run(['cmake','--build',str(temp/'build'),'--target','check','-j','8'],check=True)
        subprocess.run([str(temp/'build/check')],check=True,cwd=temp)
if __name__=='__main__':
    main()
