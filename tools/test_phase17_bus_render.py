#!/usr/bin/env python3
"""Bus package regression using production Lua UI bindings and real LVGL."""
import argparse
import json
from pathlib import Path
import subprocess
from test_manage_apps_page import CONF, png_from_ppm
ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'components/crystal_script/src/crystal_script.cpp').read_text()
def function(signature):
    start = SOURCE.index(signature)
    return SOURCE[start:SOURCE.index('\n}', start) + 2]
PREFIX = r'''
#include <algorithm>
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
class ScriptApp {
public:
    CrystalResources resources_;
    CrystalUI ui_{resources_};
    lua_State *lua_=nullptr;
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
    std::list<std::unique_ptr<CallbackRef>> callbacks_;
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
    bool load_image(lv_obj_t*,const char*) { return false; }
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
static std::string asset_root;
static int read_asset(lua_State *state) {
    const char *path=luaL_checkstring(state,1);
    std::ifstream file(asset_root+"/"+path,std::ios::binary);
    if(!file){lua_pushnil(state);return 1;}
    std::ostringstream bytes;bytes<<file.rdbuf();const auto value=bytes.str();
    lua_pushlstring(state,value.data(),value.size());return 1;
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
    lv_obj_scroll_to_view(target,LV_ANIM_OFF); lv_obj_update_layout(lv_scr_act());
    if(lv_obj_has_state(target,LV_STATE_DISABLED)) return 0;
    *static_cast<ScriptApp **>(lua_getextraspace(state))=test_app;
    lua_sethook(state,ScriptApp::instruction_hook,LUA_MASKCOUNT,10000);
    lv_event_send(target,LV_EVENT_CLICKED,nullptr);
    lua_sethook(state,nullptr,0,0);
    if(test_app->runtime_failed_)return luaL_error(state,"touch callback failed production guard");
    return 0;
}
static int is_enabled(lua_State *state) {
    auto *label=find(lv_scr_act(),luaL_checkstring(state,1));assert(label);
    auto *target=lv_obj_get_parent(label);
    lua_pushboolean(state,!lv_obj_has_state(target,LV_STATE_DISABLED));return 1;
}
static int has_text(lua_State *state) {lua_pushboolean(state,find(lv_scr_act(),luaL_checkstring(state,1))!=nullptr);return 1;}
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
static void flush(lv_disp_drv_t *d,const lv_area_t*,lv_color_t*) {lv_disp_flush_ready(d);}
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
    ScriptApp app;
    test_app=&app;app.arena_size_=atoi(argv[4])*1024;
    asset_root=std::string(argv[1]).substr(0,std::string(argv[1]).find_last_of('/'));
    app.arena_=malloc(app.arena_size_); assert(app.arena_);
    auto *first=static_cast<ScriptApp::Block*>(app.arena_);
    *first={app.arena_size_-sizeof(ScriptApp::Block),nullptr,nullptr,true};
    L=app.lua_=lua_newstate(observed_allocate,&app);assert(L);luaL_openlibs(L);
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
    for(auto type:{"col","row","scroll_view","label","button","list"}){
        lua_pushlightuserdata(L,&app);lua_pushstring(L,type);lua_pushcclosure(L,ScriptApp::ui_create,2);lua_setfield(L,-2,type);
    }
    lua_setglobal(L,"ui");
    lua_pushcfunction(L,read_asset);lua_setglobal(L,"read_asset");
    lua_pushlightuserdata(L,&app);lua_pushcclosure(L,ScriptApp::json_decode,1);lua_setglobal(L,"decode_json");
    lua_pushcfunction(L,memory_stats);lua_setglobal(L,"memory_stats");
    lua_pushcfunction(L,run_callback);lua_setglobal(L,"run_callback");
    lua_pushcfunction(L,click);lua_setglobal(L,"click");
    lua_pushcfunction(L,is_enabled);lua_setglobal(L,"is_enabled");
    lua_pushcfunction(L,has_text);lua_setglobal(L,"has_text");
    lua_pushcfunction(L,text_font_size);lua_setglobal(L,"text_font_size");
    lua_pushcfunction(L,text_count);lua_setglobal(L,"text_count");
    lua_pushcfunction(L,list_scroll_y);lua_setglobal(L,"list_scroll_y");
    lua_pushcfunction(L,snapshot);lua_setglobal(L,"snapshot");
    lua_pushstring(L,argv[5]);lua_setglobal(L,"expected_counts");
    lua_pushstring(L,argv[1]);lua_setglobal(L,"package_path");
    if(luaL_dofile(L,argv[2])){fprintf(stderr,"%s; arena used=%zu/%zu\n",lua_tostring(L,-1),app.arena_used_,app.arena_size_);return 1;}
    assert(app.callbacks_.size()<=64);
    assert(native_json_allocations==0);
    printf("PASS: production Lua/UI and LVGL at %dpx; %zu callbacks (limit 64)\n",width,app.callbacks_.size());
    assert(!app.runtime_failed_ && lua_gettop(L)==0);
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
    names += ['void ScriptApp::instruction_hook(']
    names += ['int ScriptApp::ui_'+name+'(' for name in ('create','screen','get_size','set_text','get_text',
              'set_hidden','set_enabled','set_style','scroll_to','delete','list_add','list_insert',
              'list_set_text','list_remove','list_clear','list_count')]
    (work/'check.cpp').write_text(PREFIX+'\n'.join(function(name) for name in names)+MAIN)
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
    (output/'prefix-counts.json').write_text(json.dumps(counts))
    quota=args.arena_kb or json.loads((ROOT/'examples/bus3p/app.yaml').read_text())['memory_kb']
    for width in (480,720):
        folder=output/str(width);folder.mkdir(parents=True,exist_ok=True)
        drivers=['test_phase17_bus_catalog_ui.lua'] if args.catalog_only else [
            'test_phase17_bus_ui.lua','test_phase17_bus_catalog_ui.lua','test_phase17_bus_persistence.lua']
        for driver in drivers:
            subprocess.run([str(work/'build/check'),str(ROOT/'examples/bus3p/main.lua'),
                            str(ROOT/'tools'/driver),str(width),str(quota),json.dumps(counts)],cwd=folder,check=True)
        for ppm in folder.glob('*.ppm'):png_from_ppm(ppm)
    print('Rendered previews:',output)
if __name__=='__main__':main()
