#!/usr/bin/env python3
"""Bus package regression using production Lua UI bindings and real LVGL."""
import argparse
import json
from pathlib import Path
import subprocess
from test_manage_apps_page import CONF, png_from_ppm
ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'components/crystal_script/src/crystal_script.cpp').read_text()
CORE = (ROOT / 'components/crystal_core/src/crystal_core.cpp').read_text()
def function(signature):
    start = SOURCE.index(signature)
    return SOURCE[start:SOURCE.index('\n}', start) + 2]
PREFIX = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cstddef>
#include <chrono>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <fstream>
#include <sstream>
#include "crystal_ui.hpp"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "cJSON.h"
}
static std::string asset_root;
constexpr size_t kMaxFileBytes=64*1024;
constexpr size_t kMaxLuaString=4096, kMaxJsonNodes=512, kMaxCallbacks=64;
constexpr unsigned kMaxJsonDepth=16;
static uint32_t test_timeout_seconds=5;
static uint32_t crystal_lua_callback_timeout_seconds() { return test_timeout_seconds; }
static int64_t test_time_offset_us=0;
static int64_t esp_timer_get_time() { return test_time_offset_us + std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
constexpr int MALLOC_CAP_INTERNAL=1, MALLOC_CAP_8BIT=2, MALLOC_CAP_SPIRAM=4;
static size_t native_json_allocations=0;
static void *json_allocate(size_t size) {void *ptr=malloc(size);if(ptr)++native_json_allocations;return ptr;}
static void json_release(void *ptr) {if(ptr)--native_json_allocations;free(ptr);}
static size_t heap_caps_get_free_size(int) {return 1024*1024;}
static bool test_online=true;
static uint32_t test_net_revision=0;
static bool crystal_network_has_ip() {return test_online;}
static uint32_t crystal_network_revision() {return test_net_revision;}
using crystal_net_change_cb_t = void (*)(void *,bool);
bool crystal_net_is_ready();
uint32_t crystal_net_subscribe(crystal_net_change_cb_t,void*);
bool crystal_net_unsubscribe(uint32_t);
using crystal_time_ready_cb_t = void (*)(void *);
bool crystal_time_is_ready();
uint32_t crystal_time_subscribe_ready(crystal_time_ready_cb_t,void*);
bool crystal_time_unsubscribe_ready(uint32_t);
class ScriptApp {
public:
    struct ImageAsset { std::vector<lv_color_t> pixels; lv_img_dsc_t descriptor{}; };
    CrystalResources resources_;
    CrystalUI ui_{resources_};
    lua_State *lua_=nullptr;
    uint32_t net_subscription_=0;
    bool net_pending_=false;
    static int service_net_is_ready(lua_State*),service_net_on_change(lua_State*),service_net_off_change(lua_State*);
    static void net_changed(void*,bool);
    void clear_net_subscription();
    uint32_t time_subscription_=0;
    bool time_paused_=false,time_pending_=false;
    static int service_time_is_ready(lua_State*),service_time_on_ready(lua_State*),service_time_off_ready(lua_State*);
    static void time_ready(void*);
    void clear_time_subscription();
    lv_obj_t *root_=nullptr;
    struct Block {size_t size; Block *previous; Block *next; bool free;};
    void *arena_=nullptr; size_t arena_size_=256*1024, arena_used_=0, arena_peak_=0;
    bool runtime_failed_=false, deadline_exceeded_=false, in_destroy_=false;
    int hooks_ref_=LUA_NOREF; int64_t deadline_us_=0;
    const char *timeout_message_="App stopped responding (5 s callback limit)";
    static void *lua_allocate(void*,void*,size_t,size_t);
    static void instruction_hook(lua_State*,lua_Debug*);
    void *arena_allocate(size_t); void arena_release(void*);
    struct HttpResult {uint32_t id=0; int status=0; std::string body;};
    struct Invocation {ScriptApp *app; const char *hook=nullptr; int ref=LUA_NOREF; CrystalUI::Handle handle=0; const char *message=nullptr; const HttpResult *response=nullptr; bool required=false;};
    static int protected_invoke(lua_State*);
    bool invoke(Invocation &);
    bool execute(const char*,bool=false);
    void runtime_error(const char *msg) {runtime_failed_=true; fprintf(stderr,"contained: %s\n",msg);}
    struct CallbackRef { ScriptApp *app=nullptr; int ref=LUA_NOREF; };
    CallbackRef *time_callback_=nullptr,*net_callback_=nullptr;
    std::list<std::unique_ptr<CallbackRef>> callbacks_;
    std::map<std::string,std::unique_ptr<ImageAsset>> image_assets_;
    std::map<std::pair<uint32_t,std::string>,CallbackRef*> event_callbacks_;
    static ScriptApp *self(lua_State *L) { return static_cast<ScriptApp*>(lua_touserdata(L,lua_upvalueindex(1))); }
    CallbackRef *make_callback(int);
    static void ui_event(CrystalUI::Handle,void*);
    bool call_ref(int,CrystalUI::Handle,const char * = nullptr);
    cJSON *lua_to_json(int,unsigned=0,unsigned * = nullptr);
    void json_to_lua(const cJSON*,unsigned=0);
    static int protected_json_value(lua_State*),json_decode(lua_State*);
    int push_json_value(const cJSON*);
    uint32_t json_decode_count_=0,json_largest_input_bytes_=0;
    size_t json_peak_internal_bytes_=0,json_peak_psram_bytes_=0;
    static lv_obj_t *build_widget(const cJSON*,lv_obj_t*,void*);
    static bool apply_style(lv_obj_t*,const cJSON*);
    bool load_image(lv_obj_t*,const char*);
    static int ui_create(lua_State*),ui_screen(lua_State*),ui_get_size(lua_State*),ui_set_text(lua_State*),
        ui_get_text(lua_State*),ui_set_hidden(lua_State*),ui_set_enabled(lua_State*),ui_set_style(lua_State*),
        ui_scroll_to(lua_State*),ui_delete(lua_State*),ui_list_add(lua_State*),ui_list_insert(lua_State*),
        ui_list_set_text(lua_State*),ui_list_remove(lua_State*),ui_list_clear(lua_State*),ui_list_count(lua_State*);
};
'''
MAIN = r'''
static lua_State *L;
static unsigned allocation_failures;
static ScriptApp *test_app;
static int net_signal(lua_State *state) {
    test_online=lua_toboolean(state,1);++test_net_revision;
    dispatch_net_changed();return 0;
}
static int net_dispatch(lua_State *) {dispatch_net_changed();return 0;}
static int net_reset(lua_State *) {test_app->clear_net_subscription();return 0;}
static int clock_signal(lua_State *state) {
    s_time_valid.store(lua_toboolean(state,1));
    s_time_revision.fetch_add(1);
    dispatch_time_ready();return 0;
}
static int clock_dispatch(lua_State *) {dispatch_time_ready();return 0;}
static int clock_callback_count(lua_State *state) {lua_pushinteger(state,test_app->callbacks_.size());return 1;}
static int clock_reset(lua_State *) {test_app->clear_time_subscription();return 0;}
static void check_clock_listener_lifetimes() {
    s_time_valid.store(false);s_time_revision.store(0);
    unsigned calls=0;
    auto count=[](void *p){++*static_cast<unsigned*>(p);};
    uint32_t ids[16]{};
    for(auto &id:ids){id=crystal_time_subscribe_ready(count,&calls);assert(id);}
    assert(!crystal_time_subscribe_ready(count,&calls));
    dispatch_time_ready();assert(calls==0);
    s_time_valid.store(true);s_time_revision.fetch_add(1);dispatch_time_ready();assert(calls==16);
    dispatch_time_ready();assert(calls==16);
    for(auto id:ids)assert(crystal_time_unsubscribe_ready(id));
    struct Reentrant {uint32_t old=0,next=0;unsigned calls=0;} state;
    auto replace=[](void *p){
        auto &s=*static_cast<Reentrant*>(p);
        assert(crystal_time_unsubscribe_ready(s.old));
        s.next=crystal_time_subscribe_ready([](void *q){++static_cast<Reentrant*>(q)->calls;},&s);
    };
    state.old=crystal_time_subscribe_ready(replace,&state);dispatch_time_ready();assert(state.next && !state.calls);
    dispatch_time_ready();assert(state.calls==1);
    assert(!crystal_time_unsubscribe_ready(state.old));
    assert(crystal_time_unsubscribe_ready(state.next));
    puts("PASS: core clock subscription capacity, coalescing and reentrant teardown");
}
static void check_net_listener_lifetimes() {
    test_online=false;test_net_revision=0;
    unsigned calls=0;
    auto count=[](void *p,bool){++*static_cast<unsigned*>(p);};
    uint32_t ids[16]{};
    for(auto &id:ids){id=crystal_net_subscribe(count,&calls);assert(id);}
    assert(!crystal_net_subscribe(count,&calls));
    dispatch_net_changed();assert(calls==16);
    dispatch_net_changed();assert(calls==16);
    test_online=true;++test_net_revision;dispatch_net_changed();assert(calls==32);
    ++test_net_revision;dispatch_net_changed();assert(calls==48);
    for(auto id:ids)assert(crystal_net_unsubscribe(id));
    struct Reentrant {uint32_t old=0,next=0;unsigned calls=0;} state;
    auto replace=[](void *p,bool){
        auto &s=*static_cast<Reentrant*>(p);
        assert(crystal_net_unsubscribe(s.old));
        s.next=crystal_net_subscribe([](void *q,bool){++static_cast<Reentrant*>(q)->calls;},&s);
    };
    state.old=crystal_net_subscribe(replace,&state);dispatch_net_changed();assert(state.next && !state.calls);
    dispatch_net_changed();assert(state.calls==1);
    assert(!crystal_net_unsubscribe(state.old));assert(crystal_net_unsubscribe(state.next));
    puts("PASS: core network capacity, deferred replay, route revisions and reentrant teardown");
}
static int clock_pause(lua_State *state) {
    test_app->time_paused_=lua_toboolean(state,1);
    if(!test_app->time_paused_ && test_app->time_pending_) ScriptApp::time_ready(test_app);
    if(!test_app->time_paused_ && test_app->net_pending_) ScriptApp::net_changed(test_app,test_online);
    return 0;
}
static int run_callback(lua_State *state) {
    luaL_checktype(state,1,LUA_TFUNCTION);lua_pushvalue(state,1);
    const int ref=luaL_ref(state,LUA_REGISTRYINDEX);
    *static_cast<ScriptApp **>(lua_getextraspace(state))=test_app;
    lua_sethook(state,ScriptApp::instruction_hook,LUA_MASKCOUNT,10000);
    const bool ok=test_app->call_ref(ref,0);
    lua_sethook(state,nullptr,0,0);luaL_unref(state,LUA_REGISTRYINDEX,ref);
    if(!ok)return luaL_error(state,"timer exceeded production callback budget");
    return 0;
}
static int encode_json(lua_State *state) {
    cJSON *value=test_app->lua_to_json(1);if(!value)return luaL_error(state,"state serialization failed");
    char *encoded=cJSON_PrintUnformatted(value);cJSON_Delete(value);
    if(!encoded)return luaL_error(state,"state encoding failed");
    lua_pushstring(state,encoded);cJSON_free(encoded);return 1;
}
static int read_asset(lua_State *state) {
    const char *path=luaL_checkstring(state,1);
    std::ifstream file(asset_root+"/"+path,std::ios::binary);
    if(!file){lua_pushnil(state);return 1;}
    std::ostringstream bytes;bytes<<file.rdbuf();const auto value=bytes.str();
    lua_pushlstring(state,value.data(),value.size());return 1;
}
bool ScriptApp::load_image(lv_obj_t *object,const char *path) {
    if(!object||!path||strncmp(path,"assets/",7))return false;
    auto found=image_assets_.find(path);
    if(found==image_assets_.end()){
        std::ifstream file(asset_root+"/"+path,std::ios::binary);std::string magic;unsigned width=0,height=0;
        if(!(file>>magic>>width>>height)||magic!="CR16"||!width||!height||width>480||height>480)return false;
        auto asset=std::make_unique<ImageAsset>();asset->pixels.resize(static_cast<size_t>(width)*height);
        for(auto &pixel:asset->pixels){unsigned color=0;if(!(file>>std::hex>>color)||color>0xffffff)return false;pixel=lv_color_hex(color);}
        asset->descriptor.header.cf=LV_IMG_CF_TRUE_COLOR;asset->descriptor.header.always_zero=0;
        asset->descriptor.header.reserved=0;asset->descriptor.header.w=width;asset->descriptor.header.h=height;
        asset->descriptor.data_size=asset->pixels.size()*sizeof(lv_color_t);
        asset->descriptor.data=reinterpret_cast<const uint8_t*>(asset->pixels.data());
        found=image_assets_.emplace(path,std::move(asset)).first;
    }
    lv_img_set_src(object,&found->second->descriptor);return true;
}
static int memory_stats(lua_State *state) {
    auto &app=*test_app;size_t largest=0,occupied=0;
    for(auto *b=static_cast<ScriptApp::Block*>(app.arena_);b;b=b->next){
        if(b->free)largest=std::max(largest,b->size);else occupied+=b->size+sizeof(*b);
    }
    lua_newtable(state);
    const std::pair<const char*,size_t> fields[]={{"used",app.arena_used_},{"peak",app.arena_peak_},
        {"capacity",app.arena_size_},{"largest",largest},{"occupied",occupied}};
    for(auto field:fields){lua_pushinteger(state,field.second);lua_setfield(state,-2,field.first);}return 1;
}
static void *observed_allocate(void *ctx,void *ptr,size_t old_size,size_t size) {
    void *result=ScriptApp::lua_allocate(ctx,ptr,old_size,size);
    if(size && !result) ++allocation_failures;
    return result;
}
static bool visible(lv_obj_t *obj) {
    for(auto *p=obj;p;p=lv_obj_get_parent(p)) if(lv_obj_has_flag(p,LV_OBJ_FLAG_HIDDEN)) return false;
    return true;
}
static lv_obj_t *find_nth(lv_obj_t *obj,const char *text,int &remaining) {
    if(!visible(obj)) return nullptr;
    if(lv_obj_check_type(obj,&lv_label_class) && !strcmp(lv_label_get_text(obj),text) && --remaining==0) return obj;
    for(unsigned i=0;i<lv_obj_get_child_cnt(obj);++i) if(auto *p=find_nth(lv_obj_get_child(obj,i),text,remaining)) return p;
    return nullptr;
}
static lv_obj_t *find(lv_obj_t *obj,const char *text) {int remaining=1;return find_nth(obj,text,remaining);}
static int click(lua_State *state) {
    int remaining=luaL_optinteger(state,2,1);assert(remaining>0);
    auto *label=find_nth(lv_scr_act(),luaL_checkstring(state,1),remaining); assert(label);
    auto *target=lv_obj_get_parent(label);
    int ancestors=luaL_optinteger(state,3,0);assert(ancestors>=0);
    while(ancestors-- && target) target=lv_obj_get_parent(target);
    assert(target);
    lv_obj_scroll_to_view(target,LV_ANIM_OFF); lv_obj_update_layout(lv_scr_act());
    if(lv_obj_has_state(target,LV_STATE_DISABLED)) return 0;
    *static_cast<ScriptApp **>(lua_getextraspace(state))=test_app;
    lua_sethook(state,ScriptApp::instruction_hook,LUA_MASKCOUNT,10000);
    lv_event_send(target,LV_EVENT_CLICKED,nullptr);
    lua_sethook(state,nullptr,0,0);
    if(test_app->runtime_failed_)return luaL_error(state,"touch callback failed production guard");
    return 0;
}
static int check_tab_hit_area(lua_State *state) {
    auto *label=find(lv_scr_act(),luaL_checkstring(state,1));assert(label);
    auto *target=lv_obj_get_parent(label);lv_obj_update_layout(lv_scr_act());
    lv_area_t area{};lv_obj_get_coords(target,&area);
    assert(lv_obj_get_width(target)==lv_disp_get_hor_res(nullptr)/2);
    assert(lv_obj_get_height(target)==48*lv_disp_get_hor_res(nullptr)/480-3);
    for(int y=area.y1;y<=area.y2;++y) for(int x=area.x1;x<=area.x2;++x) {
        lv_point_t point{static_cast<lv_coord_t>(x),static_cast<lv_coord_t>(y)};
        assert(lv_indev_search_obj(lv_scr_act(),&point)==target);
    }
    return 0;
}
static int is_enabled(lua_State *state) {
    auto *label=find(lv_scr_act(),luaL_checkstring(state,1));assert(label);
    auto *target=lv_obj_get_parent(label);
    lua_pushboolean(state,!lv_obj_has_state(target,LV_STATE_DISABLED));return 1;
}
static int has_text(lua_State *state) {lua_pushboolean(state,find(lv_scr_act(),luaL_checkstring(state,1))!=nullptr);return 1;}
static int has_icon(lua_State *state) {
    auto *label=find(lv_scr_act(),luaL_checkstring(state,1));if(!label){lua_pushboolean(state,false);return 1;}
    auto *button=lv_obj_get_parent(label);auto *group=button?lv_obj_get_parent(button):nullptr;bool found=false;
    for(unsigned i=0;i<lv_obj_get_child_cnt(button);++i)
        if(lv_obj_check_type(lv_obj_get_child(button,i),&lv_img_class)){found=true;break;}
    for(unsigned i=0;!found && group && i<lv_obj_get_child_cnt(group);++i)
        if(lv_obj_check_type(lv_obj_get_child(group,i),&lv_img_class)){found=true;break;}
    lua_pushboolean(state,found);return 1;
}
static int position(lua_State *state) {
    auto *label=find(lv_scr_act(),luaL_checkstring(state,1));assert(label);
    auto *button=lv_obj_get_parent(label);lv_obj_update_layout(lv_scr_act());lv_area_t area{};
    lv_obj_get_coords(button,&area);lua_pushinteger(state,area.x1);lua_pushinteger(state,area.y1);return 2;
}
static int widget_metrics(lua_State *state) {
    auto *label=find(lv_scr_act(),luaL_checkstring(state,1));assert(label);
    auto *widget=lv_obj_get_parent(label);lv_obj_update_layout(lv_scr_act());lv_area_t area{};
    lv_obj_get_coords(widget,&area);lua_pushinteger(state,area.x1);lua_pushinteger(state,area.y1);
    lua_pushinteger(state,area.x2-area.x1+1);lua_pushinteger(state,area.y2-area.y1+1);return 4;
}
static int label_bounds(lua_State *state) {
    auto *label=find(lv_scr_act(),luaL_checkstring(state,1));assert(label);
    lv_obj_update_layout(lv_scr_act());lv_area_t area{};lv_obj_get_coords(label,&area);
    lua_pushinteger(state,area.x1);lua_pushinteger(state,area.y1);
    lua_pushinteger(state,area.x2-area.x1+1);lua_pushinteger(state,area.y2-area.y1+1);return 4;
}
static int text_font_size(lua_State *state) {
    auto *label=find(lv_scr_act(),luaL_checkstring(state,1));assert(label);
    auto *font=lv_obj_get_style_text_font(label,LV_PART_MAIN);
    const int size=font==&lv_font_montserrat_16?16:font==&lv_font_montserrat_20?20:font==&lv_font_montserrat_28?28:0;
    lua_pushinteger(state,size);return 1;
}
static int text_count(lua_State *state) {
    const char *value=luaL_checkstring(state,1);int count=0;
    for(int nth=1;;++nth){int remaining=nth;if(!find_nth(lv_scr_act(),value,remaining))break;++count;}
    lua_pushinteger(state,count);return 1;
}
static int list_scroll_y(lua_State *state) {
    int remaining=luaL_optinteger(state,2,1);assert(remaining>0);
    auto *label=find_nth(lv_scr_act(),luaL_checkstring(state,1),remaining);assert(label);
    auto *row=lv_obj_get_parent(label);auto *list=row?lv_obj_get_parent(row):nullptr;assert(list);
    lv_obj_scroll_to_view(row,LV_ANIM_OFF);lv_obj_update_layout(lv_scr_act());
    lua_pushinteger(state,lv_obj_get_scroll_y(list));return 1;
}
static int list_row_metrics(lua_State *state) {
    auto *label=find(lv_scr_act(),luaL_checkstring(state,1));assert(label);
    auto *row=lv_obj_get_parent(label);lv_obj_update_layout(lv_scr_act());lv_area_t area{};
    lv_obj_get_coords(row,&area);lua_pushinteger(state,area.y1);lua_pushinteger(state,area.y2-area.y1+1);return 2;
}
static int list_bounds(lua_State *state) {
    auto *label=find(lv_scr_act(),luaL_checkstring(state,1));assert(label);
    auto *row=lv_obj_get_parent(label);auto *list=row?lv_obj_get_parent(row):nullptr;assert(list);
    lv_obj_update_layout(lv_scr_act());lv_area_t area{};lv_obj_get_coords(list,&area);
    lua_pushinteger(state,area.x1);lua_pushinteger(state,area.x2-area.x1+1);return 2;
}
static unsigned flush_count=0;
static void flush(lv_disp_drv_t *d,const lv_area_t*,lv_color_t*) {++flush_count;lv_disp_flush_ready(d);}
static void check_repeated_text_refresh() {
    CrystalResources resources; CrystalUI ui(resources);
    auto *root=lv_obj_create(lv_scr_act());lv_obj_set_size(root,300,200);
    ui.set_root(root);assert(ui.adopt(root));
    auto *label=lv_label_create(root);lv_label_set_text(label,"3 min");
    const auto label_id=ui.adopt(label);
    auto *button=lv_btn_create(root);auto *caption=lv_label_create(button);
    lv_label_set_text(caption,"Refresh");const auto button_id=ui.adopt(button,caption);
    auto *list=lv_list_create(root);lv_obj_set_size(list,250,100);lv_obj_set_y(list,60);
    const auto list_id=ui.adopt(list);assert(ui.list_add(list_id,{"1. City Hall"}));
    lv_refr_now(nullptr);flush_count=0;
    for(int i=0;i<30;++i) {
        assert(ui.set_text(label_id,"3 min"));assert(ui.set_text(button_id,"Refresh"));
        assert(ui.list_set_text(list_id,0,"1. City Hall"));
        lv_refr_now(nullptr);
    }
    assert(flush_count==0 && "unchanged text must not cause framebuffer traffic");
    assert(ui.set_text(label_id,"2 min"));lv_refr_now(nullptr);assert(flush_count>0);
    flush_count=0;assert(ui.set_text(button_id,"Retry"));lv_refr_now(nullptr);assert(flush_count>0);
    flush_count=0;assert(ui.list_set_text(list_id,0,"1. Updated stop"));
    lv_refr_now(nullptr);assert(flush_count>0);
    ui.reset();lv_obj_del(root);
    puts("PASS: unchanged label/button/list text produces zero flushes; changed text redraws");
}
static int snapshot(lua_State *state) {
    lv_obj_update_layout(lv_scr_act());
    auto *shot=lv_snapshot_take(lv_scr_act(),LV_IMG_CF_TRUE_COLOR);assert(shot);
    auto *file=fopen(luaL_checkstring(state,1),"wb");assert(file);
    fprintf(file,"P6\n%u %u\n255\n",shot->header.w,shot->header.h);
    auto *pixels=reinterpret_cast<const lv_color_t*>(shot->data);
    for(unsigned i=0;i<shot->header.w*shot->header.h;++i){
        lv_color32_t c;c.full=lv_color_to32(pixels[i]);const unsigned char rgb[]={c.ch.red,c.ch.green,c.ch.blue};fwrite(rgb,1,3,file);
    }
    fclose(file);lv_snapshot_free(shot);return 0;
}
int main(int argc,char **argv) {
    assert(argc==6); int width=atoi(argv[3]);
    cJSON_Hooks hooks{json_allocate,json_release};cJSON_InitHooks(&hooks);
    lv_init(); static lv_color_t pixels[720*20];static lv_disp_draw_buf_t buf;
    lv_disp_draw_buf_init(&buf,pixels,nullptr,720*20);lv_disp_drv_t drv;lv_disp_drv_init(&drv);
    drv.hor_res=width;drv.ver_res=width-40;drv.draw_buf=&buf;drv.flush_cb=flush;lv_disp_drv_register(&drv);
    check_clock_listener_lifetimes();
    check_net_listener_lifetimes();
    check_repeated_text_refresh();
    ScriptApp app;
    test_app=&app;app.arena_size_=atoi(argv[4])*1024;
    asset_root=std::string(argv[1]).substr(0,std::string(argv[1]).find_last_of('/'));
    app.arena_=malloc(app.arena_size_); assert(app.arena_);
    auto *first=static_cast<ScriptApp::Block*>(app.arena_);
    *first={app.arena_size_-sizeof(ScriptApp::Block),nullptr,nullptr,true};
    L=app.lua_=lua_newstate(observed_allocate,&app);assert(L);luaL_openlibs(L);
    lua_pushinteger(L,width);lua_setglobal(L,"screen_width");
    lua_pushinteger(L,width-40);lua_setglobal(L,"screen_height");
    for(auto name:{"os","io","package","require","debug","collectgarbage"}){lua_pushnil(L);lua_setglobal(L,name);}
    app.root_=lv_obj_create(lv_scr_act());lv_obj_remove_style_all(app.root_);
    lv_obj_set_size(app.root_,width,width-40);lv_obj_set_style_bg_opa(app.root_,LV_OPA_COVER,0);
    lv_obj_set_style_text_font(app.root_,&lv_font_montserrat_20,0);
    lv_obj_clear_flag(app.root_,LV_OBJ_FLAG_SCROLLABLE);
    app.ui_.set_root(app.root_);assert(app.ui_.adopt(app.root_));
    app.ui_.configure(ScriptApp::build_widget,ScriptApp::apply_style,nullptr,&app);
    lua_newtable(L);
    const luaL_Reg methods[]={
        {"screen",ScriptApp::ui_screen},{"get_size",ScriptApp::ui_get_size},{"set_text",ScriptApp::ui_set_text},
        {"get_text",ScriptApp::ui_get_text},{"set_hidden",ScriptApp::ui_set_hidden},{"set_enabled",ScriptApp::ui_set_enabled},
        {"set_style",ScriptApp::ui_set_style},{"scroll_to",ScriptApp::ui_scroll_to},{"delete",ScriptApp::ui_delete},
        {"list_add",ScriptApp::ui_list_add},{"list_insert",ScriptApp::ui_list_insert},
        {"list_set_text",ScriptApp::ui_list_set_text},{"list_remove",ScriptApp::ui_list_remove},
        {"list_count",ScriptApp::ui_list_count},{"list_clear",ScriptApp::ui_list_clear},{nullptr,nullptr}};
    for(auto *m=methods;m->name;++m){lua_pushlightuserdata(L,&app);lua_pushcclosure(L,m->func,1);lua_setfield(L,-2,m->name);}
    for(auto type:{"col","row","scroll_view","label","button","image","list"}){
        lua_pushlightuserdata(L,&app);lua_pushstring(L,type);lua_pushcclosure(L,ScriptApp::ui_create,2);lua_setfield(L,-2,type);
    }
    lua_setglobal(L,"ui");
    lua_newtable(L);
    for(const auto &entry : std::initializer_list<std::pair<const char*,lua_CFunction>>{
        {"is_ready",ScriptApp::service_time_is_ready},{"on_ready",ScriptApp::service_time_on_ready},
        {"off_ready",ScriptApp::service_time_off_ready}}) {
        lua_pushlightuserdata(L,&app);lua_pushcclosure(L,entry.second,1);lua_setfield(L,-2,entry.first);
    }
    lua_setglobal(L,"clock_api");
    lua_newtable(L);
    for(const auto &entry : std::initializer_list<std::pair<const char*,lua_CFunction>>{
        {"is_ready",ScriptApp::service_net_is_ready},{"on_change",ScriptApp::service_net_on_change},
        {"off_change",ScriptApp::service_net_off_change}}) {
        lua_pushlightuserdata(L,&app);lua_pushcclosure(L,entry.second,1);lua_setfield(L,-2,entry.first);
    }
    lua_setglobal(L,"net_api");
    lua_pushcfunction(L,net_signal);lua_setglobal(L,"net_signal");
    lua_pushcfunction(L,net_dispatch);lua_setglobal(L,"net_dispatch");
    lua_pushcfunction(L,net_reset);lua_setglobal(L,"net_reset");
    lua_pushcfunction(L,clock_signal);lua_setglobal(L,"clock_signal");
    lua_pushcfunction(L,clock_dispatch);lua_setglobal(L,"clock_dispatch");
    lua_pushcfunction(L,clock_callback_count);lua_setglobal(L,"clock_callback_count");
    lua_pushcfunction(L,clock_reset);lua_setglobal(L,"clock_reset");
    lua_pushcfunction(L,clock_pause);lua_setglobal(L,"clock_pause");
    s_time_valid.store(true);s_time_revision.store(1);
    lua_pushcfunction(L,read_asset);lua_setglobal(L,"read_asset");
    lua_pushcfunction(L,encode_json);lua_setglobal(L,"encode_json");
    lua_pushlightuserdata(L,&app);lua_pushcclosure(L,ScriptApp::json_decode,1);lua_setglobal(L,"decode_json");
    lua_pushcfunction(L,memory_stats);lua_setglobal(L,"memory_stats");
    lua_pushcfunction(L,run_callback);lua_setglobal(L,"run_callback");
    lua_pushcfunction(L,click);lua_setglobal(L,"click");
    lua_pushcfunction(L,check_tab_hit_area);lua_setglobal(L,"check_tab_hit_area");
    lua_pushcfunction(L,is_enabled);lua_setglobal(L,"is_enabled");
    lua_pushcfunction(L,has_text);lua_setglobal(L,"has_text");
    lua_pushcfunction(L,has_icon);lua_setglobal(L,"has_icon");
    lua_pushcfunction(L,position);lua_setglobal(L,"position");
    lua_pushcfunction(L,widget_metrics);lua_setglobal(L,"widget_metrics");
    lua_pushcfunction(L,label_bounds);lua_setglobal(L,"label_bounds");
    lua_pushcfunction(L,text_font_size);lua_setglobal(L,"text_font_size");
    lua_pushcfunction(L,text_count);lua_setglobal(L,"text_count");
    lua_pushcfunction(L,list_scroll_y);lua_setglobal(L,"list_scroll_y");
    lua_pushcfunction(L,list_row_metrics);lua_setglobal(L,"list_row_metrics");
    lua_pushcfunction(L,list_bounds);lua_setglobal(L,"list_bounds");
    lua_pushcfunction(L,snapshot);lua_setglobal(L,"snapshot");
    lua_pushstring(L,argv[5]);lua_setglobal(L,"expected_counts");
    lua_pushstring(L,argv[1]);lua_setglobal(L,"package_path");
    if(luaL_dofile(L,argv[2])){fprintf(stderr,"%s; arena used=%zu/%zu\n",lua_tostring(L,-1),app.arena_used_,app.arena_size_);return 1;}
    assert(app.callbacks_.size()<=64);
    assert(native_json_allocations==0);
    printf("PASS: production Lua/UI and LVGL at %dpx; %zu callbacks (limit 64)\n",width,app.callbacks_.size());
    assert(!app.runtime_failed_ && lua_gettop(L)==0);
    app.clear_time_subscription();
    app.clear_net_subscription();
    for(const auto &listener:s_net_listeners) assert(!listener.id);
    for(const auto &listener:s_time_listeners) assert(!listener.id);
    app.ui_.reset(); lv_obj_del(app.root_);app.ui_.reset();lua_close(L);
    assert(app.arena_used_==0);free(app.arena_);
    // Host pointers and block headers are twice the P4 size. Rendering uses
    // the manifest quota; the isolated allocator/dispatch regression below uses
    // the old 160 KiB fixture quota, without the test driver and UI adapters.
    app.arena_size_=160*1024;app.arena_=malloc(app.arena_size_);assert(app.arena_);
    first=static_cast<ScriptApp::Block*>(app.arena_);
    *first={app.arena_size_-sizeof(ScriptApp::Block),nullptr,nullptr,true};
    L=app.lua_=lua_newstate(observed_allocate,&app);assert(L);luaL_openlibs(L);
    // Exercise production invoke/hook using a simulated elapsed clock so both
    // timeout modes and a live change are checked without waiting 70 seconds.
    *static_cast<ScriptApp **>(lua_getextraspace(L))=&app;
    lua_pushcfunction(L,[](lua_State *state) -> int {
        test_time_offset_us += luaL_checkinteger(state,1);
        return 0;
    });lua_setglobal(L,"advance_time");
    for (uint32_t seconds : {5u,30u,5u}) {
        test_timeout_seconds=seconds;
        for (bool over : {false,true}) {
            app.runtime_failed_=false;
            std::string code="return function() advance_time("+
                std::to_string(static_cast<int64_t>(seconds)*1000000+(over?100000:-100000))+
                "); local x=0; for i=1,20000 do x=x+1 end end";
            assert(luaL_loadstring(L,code.c_str())==LUA_OK);
            assert(lua_pcall(L,0,1,0)==LUA_OK);
            const int timeout_ref=luaL_ref(L,LUA_REGISTRYINDEX);
            lua_sethook(L,ScriptApp::instruction_hook,LUA_MASKCOUNT,10000);
            assert(app.call_ref(timeout_ref,0)==!over);
            lua_sethook(L,nullptr,0,0);
            assert(app.deadline_exceeded_==over && lua_gettop(L)==0);
            assert(std::strstr(app.timeout_message_,seconds==30?"30 s":"5 s"));
            luaL_unref(L,LUA_REGISTRYINDEX,timeout_ref);
        }
    }
    test_time_offset_us=0;app.runtime_failed_=false;
    puts("PASS: production 5/30/5-second deadline boundaries and timeout messages");
    // Successful callbacks may force an emergency collection inside Lua.
    assert(luaL_dostring(L,"return function() for i=1,3000 do local t={string.rep('x',200)} end end")==LUA_OK);
    int ref=luaL_ref(L,LUA_REGISTRYINDEX);
    const unsigned before=allocation_failures;
    lua_gc(L,LUA_GCSTOP,0); // Force emergency collection rather than normal steps.
    assert(app.call_ref(ref,0) && lua_gettop(L)==0);
    assert(allocation_failures>before && !app.runtime_failed_);
    lua_gc(L,LUA_GCRESTART,0);
    luaL_unref(L,LUA_REGISTRYINDEX,ref);
    // Repeat a real json.decode conversion that cannot fit the Lua arena.
    // Every native cJSON allocation must be freed before Lua rethrows OOM.
    lua_pushlightuserdata(L,&app);lua_pushcclosure(L,ScriptApp::json_decode,1);lua_setglobal(L,"decode_json");
    std::string large_json="[";
    for(int i=0;i<2000;++i) {if(i)large_json+=",";large_json+="{\"stop\":\"000001\",\"seq\":1}";}
    large_json+="]";assert(large_json.size()<kMaxFileBytes);
    lua_pushlstring(L,large_json.data(),large_json.size());lua_setglobal(L,"large_json");
    for(int i=0;i<10;++i) {
        assert(luaL_dostring(L,"local ok=pcall(decode_json,large_json);assert(not ok)")==LUA_OK);
        assert(native_json_allocations==0);lua_gc(L,LUA_GCCOLLECT,0);
    }
    lua_pushnil(L);lua_setglobal(L,"large_json");lua_gc(L,LUA_GCCOLLECT,0);
    // Actual callback exhaustion is contained and teardown hooks are skipped.
    assert(luaL_dostring(L,"return {onDestroy=function() error('must not run') end}")==LUA_OK);
    app.hooks_ref_=luaL_ref(L,LUA_REGISTRYINDEX);
    assert(luaL_dostring(L,"return function() local all={} for i=1,10000 do all[i]=string.rep('x',200) end end")==LUA_OK);
    ref=luaL_ref(L,LUA_REGISTRYINDEX);
    assert(!app.call_ref(ref,0) && app.runtime_failed_ && lua_gettop(L)==0);
    assert(!app.execute("onDestroy") && lua_gettop(L)==0);
    luaL_unref(L,LUA_REGISTRYINDEX,ref);
    lua_gc(L,LUA_GCCOLLECT,0);app.runtime_failed_=false; // Isolate next host scenario.
    // An oversized HTTP body must fail within pcall, not panic while pushing it.
    assert(luaL_dostring(L,"return function() error('must not run') end")==LUA_OK);
    ref=luaL_ref(L,LUA_REGISTRYINDEX);
    ScriptApp::HttpResult response;response.body.assign(200*1024,'x');
    ScriptApp::Invocation call{&app};call.ref=ref;call.response=&response;
    assert(!app.invoke(call) && app.runtime_failed_ && lua_gettop(L)==0);
    assert(!app.call_ref(ref,0)); // No re-entry into failed VM.
    luaL_unref(L,LUA_REGISTRYINDEX,ref);
    lua_close(L);
    assert(app.arena_used_==0);free(app.arena_);
    puts("PASS: 160 KiB arena, GC recovery, OOM containment, stack balance and complete arena release");
}
'''
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',type=Path)
    parser.add_argument('--arena-kb',type=int)
    parser.add_argument('--catalog-only',action='store_true')
    args=parser.parse_args()
    work=ROOT/'.cache/phase17-bus-render'
    work.mkdir(parents=True,exist_ok=True)
    (work/'lv_conf.h').write_text(CONF+'\n#define LV_FONT_MONTSERRAT_48 1\n')
    names=['void *ScriptApp::lua_allocate(', 'void *ScriptApp::arena_allocate(', 'void ScriptApp::arena_release(', 'int ScriptApp::protected_invoke(', 'bool ScriptApp::invoke(', 'bool ScriptApp::execute(', 'bool ScriptApp::call_ref(', 'const cJSON *json_field(', 'bool finite_integer(', 'bool json_int(', 'bool lua_uint(',
           'void ScriptApp::json_to_lua(', 'int ScriptApp::protected_json_value(', 'int ScriptApp::push_json_value(', 'int ScriptApp::json_decode(', 'cJSON *ScriptApp::lua_to_json(', 'ScriptApp::CallbackRef *ScriptApp::make_callback(',
           'void ScriptApp::ui_event(', 'lv_obj_t *ScriptApp::build_widget(', 'bool ScriptApp::apply_style(']
    names += ['void ScriptApp::instruction_hook(', 'int ScriptApp::service_time_is_ready(', 'int ScriptApp::service_time_on_ready(', 'int ScriptApp::service_time_off_ready(', 'void ScriptApp::time_ready(', 'void ScriptApp::clear_time_subscription(']
    names += ['int ScriptApp::service_net_is_ready(', 'int ScriptApp::service_net_on_change(', 'int ScriptApp::service_net_off_change(', 'void ScriptApp::net_changed(', 'void ScriptApp::clear_net_subscription(']
    names += ['int ScriptApp::ui_'+name+'(' for name in ('create','screen','get_size','set_text','get_text',
              'set_hidden','set_enabled','set_style','scroll_to','delete','list_add','list_insert',
              'list_set_text','list_remove','list_clear','list_count')]
    clock_state=CORE[CORE.index('std::atomic_bool s_time_valid'):CORE.index('std::atomic_bool s_sntp_initialized')]
    clock_functions=CORE[CORE.index('bool crystal_time_is_ready()'):CORE.index('void crystal_time_init()')]
    (work/'check.cpp').write_text(PREFIX+clock_state+clock_functions+'\n'.join(function(name) for name in names)+MAIN)
    lua=ROOT/'managed_components/georgik__lua'
    lua_sources=[p for p in (lua/'lua').glob('*.c') if p.name not in ('lua.c','luac.c','onelua.c','ltests.c')]
    sources=[ROOT/'components/crystal_app/src/crystal_ui.cpp',ROOT/'components/crystal_package/src/crystal_resources.cpp',
             ROOT/'managed_components/espressif__cjson/cJSON/cJSON.c',*lua_sources]
    quoted='\n'.join('"'+str(p)+'"' for p in sources)
    (work/'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.16)
project(bus_render LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(LV_CONF_PATH "{work}/lv_conf.h" CACHE STRING "" FORCE)
add_subdirectory("{ROOT}/managed_components/lvgl__lvgl" lvgl)
add_executable(check check.cpp {quoted})
target_include_directories(check PRIVATE "{lua}/lua" "{lua}/include"
 "{ROOT}/managed_components/espressif__cjson/cJSON" "{ROOT}/components/crystal_app/include"
 "{ROOT}/components/crystal_package/include")
target_compile_definitions(check PRIVATE LUA_USE_POSIX)
target_link_libraries(check PRIVATE lvgl)
''')
    subprocess.run(['cmake','-S',str(work),'-B',str(work/'build')],check=True,stdout=subprocess.DEVNULL)
    subprocess.run(['cmake','--build',str(work/'build'),'-j','8'],check=True,stdout=subprocess.DEVNULL)
    output=(args.output_dir or work/'renders').resolve()
    output.mkdir(parents=True,exist_ok=True)
    records=[json.loads(line) for page in (ROOT/'examples/bus3p/assets/catalog').glob('routes-*.jsonl')
             for line in page.read_text().splitlines()]
    counts={prefix:sum(row['route'].startswith(prefix) for row in records) for prefix in '123456789ABEN'}
    manifest=json.loads((ROOT/'examples/bus3p/assets/catalog/manifest.json').read_text())
    counts['_preload_reads']=len(manifest['routes']['search_pages'])+1
    (output/'prefix-counts.json').write_text(json.dumps(counts))
    quota=args.arena_kb or json.loads((ROOT/'examples/bus3p/app.yaml').read_text())['memory_kb']
    for width in (480,720):
        folder=output/str(width);folder.mkdir(parents=True,exist_ok=True)
        drivers=['test_phase17_bus_catalog_ui.lua'] if args.catalog_only else [
            'test_phase17_bus_ui.lua','test_phase17_bus_catalog_ui.lua','test_phase17_bus_persistence.lua','test_phase17_bus_endpoints.lua','test_phase17_bus_favorites_single.lua','test_phase17_clock_ready.lua','test_phase17_network_ready.lua']
        for driver in drivers:
            subprocess.run([str(work/'build/check'),str(ROOT/'examples/bus3p/main.lua'),
                            str(ROOT/'tools'/driver),str(width),str(quota),json.dumps(counts)],cwd=folder,check=True)
        for ppm in folder.glob('*.ppm'):png_from_ppm(ppm)
    print('Rendered previews:',output)
if __name__=='__main__':main()
