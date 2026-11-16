#!/usr/bin/env python3
"""Clock regression using production C++ bindings with controlled service/UI adapters."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'components/crystal_script/src/crystal_script.cpp').read_text()

def function(signature):
    start = SOURCE.index(signature)
    end = SOURCE.index('\n}', start) + 2
    return SOURCE[start:end]

PREFIX = r'''
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "cJSON.h"
}
constexpr size_t kMaxFileBytes = 64 * 1024, kMaxStateBytes = 32 * 1024;
constexpr size_t kMaxLuaString = 4096, kMaxJsonNodes = 512;
constexpr unsigned kMaxJsonDepth = 16;
static std::string storage_root;
static int clock_now = 1794740917;
struct CrystalTimerState { bool running = false, paused = false; uint32_t duration_seconds = 0, remaining_seconds = 0; };
static CrystalTimerState timer_state;
static int timer_end;
bool crystal_timer_start(uint32_t seconds) {
    if (!seconds || seconds > 86400) return false;
    timer_state = {true, false, seconds, seconds}; timer_end = clock_now + seconds; return true;
}
CrystalTimerState crystal_timer_state() {
    if (timer_state.running) {
        timer_state.remaining_seconds = std::max(0, timer_end - clock_now);
        timer_state.running = timer_state.remaining_seconds > 0;
    }
    return timer_state;
}
bool crystal_timer_pause() { crystal_timer_state(); if (!timer_state.running) return false; timer_state.running = false; timer_state.paused = true; return true; }
bool crystal_timer_resume() { if (!timer_state.paused) return false; timer_end = clock_now + timer_state.remaining_seconds; timer_state.running = true; timer_state.paused = false; return true; }
void crystal_timer_reset() { timer_state = {}; }
std::string package_path(const std::string &id, const char *relative) { return storage_root + "/" + id + "/" + relative; }
class ScriptApp {
public:
    lua_State *lua_ = nullptr;
    struct { std::string id = "com.example.clock3p"; unsigned storage_kb = 32; bool system_timer = true; } manifest_;
    static ScriptApp *self(lua_State *state);
    cJSON *lua_to_json(int index, unsigned depth = 0, unsigned *node_count = nullptr);
    void json_to_lua(const cJSON *value, unsigned depth = 0);
    static int protected_json_value(lua_State*);
    int push_json_value(const cJSON*);
    bool read_data(const std::string &, std::string *, size_t) const;
    bool data_size(size_t *) const;
    bool write_data(const std::string &, const std::string &);
    static int state_get(lua_State *), state_set(lua_State *);
    static int service_timer_arm(lua_State *), service_timer_start(lua_State *), service_timer_pause(lua_State *), service_timer_resume(lua_State *), service_timer_reset(lua_State *), service_timer_status(lua_State *);
};
'''

MAIN = r'''
static int now(lua_State *state) { lua_pushinteger(state, clock_now); return 1; }
static int advance(lua_State *state) { clock_now += luaL_checkinteger(state, 1); return 0; }
int main(int argc, char **argv) {
    assert(argc == 3); storage_root = argv[1];
    ScriptApp app; app.lua_ = luaL_newstate(); auto *L = app.lua_; luaL_openlibs(L);
    lua_newtable(L);
    const luaL_Reg methods[] = {
        {"get", ScriptApp::state_get}, {"set", ScriptApp::state_set},
        {"arm", ScriptApp::service_timer_arm}, {"start", ScriptApp::service_timer_start},
        {"pause", ScriptApp::service_timer_pause}, {"resume", ScriptApp::service_timer_resume},
        {"reset", ScriptApp::service_timer_reset}, {"status", ScriptApp::service_timer_status}, {nullptr, nullptr}};
    for (const auto *method = methods; method->name; ++method) {
        lua_pushlightuserdata(L, &app); lua_pushcclosure(L, method->func, 1); lua_setfield(L, -2, method->name);
    }
    lua_setglobal(L, "bindings"); lua_pushcfunction(L, now); lua_setglobal(L, "now");
    lua_pushcfunction(L, advance); lua_setglobal(L, "advance");
    auto run = [&](const char *code) { if (luaL_dostring(L, code)) { std::fprintf(stderr, "%s\n", lua_tostring(L, -1)); std::exit(1); } };
    run(R"(
        assert(bindings.set('stamp', now()))
        assert(math.type(bindings.get('stamp')) == 'integer', 'timestamp lost integer type')
        assert(bindings.get('stamp') == now(), 'timestamp changed during state round trip')
        assert(bindings.set('sample', {16777217, -16777217, 0.25}))
        local sample = bindings.get('sample')
        assert(sample[1] == 16777217 and sample[2] == -16777217 and sample[3] == 0.25)
        assert(bindings.arm(30)); advance(5)
        assert(bindings.status().remaining_sec == 25)
        assert(bindings.pause()); advance(5); assert(bindings.status().remaining_sec == 25)
        assert(bindings.resume()); advance(5); assert(bindings.status().remaining_sec == 20)
        assert(bindings.reset())
        widgets = {}; timers = {}
        local function create(kind, parent, options)
            options = options or {}; options.kind = kind; options.parent = parent
            widgets[#widgets+1] = options; return #widgets
        end
        local ui = {screen = function() return 0 end, tab = function(_, index) return -index end}
        for _, kind in ipairs({'col','row','label','button','arc','list','tabview'}) do
            ui[kind] = function(parent, options) return create(kind, parent, options) end
        end
        ui.set_text = function(id, text) assert(widgets[id]); widgets[id].text = text; return true end
        ui.set_value = function(id, value) widgets[id].value = value; return true end
        ui.timer = function(_, callback) timers[#timers+1] = callback; return #timers end
        ui.list_add = function(id, text) local w = widgets[id]; w.items = w.items or {}; table.insert(w.items, text); return true end
        ui.list_insert = function(id, index, text) local w = widgets[id]; w.items = w.items or {}; table.insert(w.items, index+1, text); return true end
        ui.list_clear = function(id) widgets[id].items = {}; return true end
        crystal = {ui=ui, state=bindings, service={timer=bindings, time={now=now, format=function() return '12:34:56' end, timezone_name=function() return 'UTC' end}, notify={toast=function(message) error(message) end}}}
        function click(text, occurrence)
            occurrence = occurrence or 1
            for _, w in ipairs(widgets) do
                if w.kind == 'button' and w.text == text then
                    occurrence = occurrence - 1; if occurrence == 0 then w.on_click(); return end
                end
            end
            error('button missing: '..text)
        end
        function tick(seconds) advance(seconds); for _, callback in ipairs(timers) do callback() end end
        function has_label(text) for _, w in ipairs(widgets) do if w.kind == 'label' and w.text == text then return true end end return false end
    )");
    if (luaL_loadfile(L, argv[2]) || lua_pcall(L, 0, 1, 0)) { std::fprintf(stderr, "%s\n", lua_tostring(L, -1)); return 1; }
    lua_setglobal(L, "app");
    run(R"(
        assert(app.onCreate()); assert(#timers == 1); assert(has_label('12:34:56'))
        click('30s'); assert(bindings.status().state == 'running'); assert(has_label('00:30'))
        tick(5); assert(has_label('00:25')); click('Pause'); tick(5); assert(has_label('00:25'))
        click('Resume'); tick(5); assert(has_label('00:20')); click('Reset'); assert(bindings.status().state == 'idle')
        click('Start', 2); tick(5); assert(has_label('00:05'), 'stopwatch did not advance')
        click('Lap'); local laps = bindings.get('laps'); assert(laps[1] == 5)
        click('Pause'); tick(5); assert(has_label('00:05'))
        click('Start', 2); tick(3); assert(has_label('00:08'))
        click('Reset', 2); assert(bindings.get('sw_start') == nil and bindings.get('sw_paused') == nil)
        assert(app.onPause()); assert(app.onResume()); assert(#timers == 1)
    )");
    lua_close(L); std::puts("phase17 production state/timer bindings and Clock interactions passed");
}
'''

def main():
    names = ['bool tree_bytes(', 'bool make_parent_dirs(', 'bool write_all(', 'const cJSON *json_field(', 'bool lua_uint(',
             'ScriptApp *ScriptApp::self(', 'cJSON *ScriptApp::lua_to_json(', 'void ScriptApp::json_to_lua(', 'int ScriptApp::protected_json_value(', 'int ScriptApp::push_json_value(',
             'bool ScriptApp::read_data(', 'bool ScriptApp::data_size(', 'bool ScriptApp::write_data(',
             'int ScriptApp::state_get(', 'int ScriptApp::state_set(']
    names += ['int ScriptApp::service_timer_' + name + '(' for name in ('arm', 'start', 'pause', 'resume', 'reset', 'status')]
    lua = ROOT / 'managed_components/georgik__lua'
    cjson = ROOT / 'managed_components/espressif__cjson/cJSON'
    with tempfile.TemporaryDirectory(prefix='phase17-bindings-') as tmp:
        tmp = Path(tmp)
        source = tmp / 'check.cpp'
        source.write_text(PREFIX + '\n'.join(function(name) for name in names) + MAIN)
        objects = []
        for path in sorted((lua / 'lua').glob('*.c')) + [cjson / 'cJSON.c']:
            if path.name in ('lua.c', 'luac.c', 'onelua.c', 'ltests.c'): continue
            obj = tmp / (path.stem + '.o'); objects.append(str(obj))
            subprocess.run(['cc', '-O1', '-DLUA_USE_POSIX', f'-I{lua / "include"}', f'-I{lua / "lua"}', '-c', str(path), '-o', str(obj)], check=True)
        binary = tmp / 'check'
        subprocess.run(['c++', '-std=c++17', f'-I{lua / "lua"}', f'-I{lua / "include"}', f'-I{cjson}', str(source), *objects, '-o', str(binary)], check=True)
        subprocess.run([str(binary), str(tmp / 'apps'), str(ROOT / 'examples/clock3p/main.lua')], check=True)

if __name__ == '__main__': main()
