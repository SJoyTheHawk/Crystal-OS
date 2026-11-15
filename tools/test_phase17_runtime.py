#!/usr/bin/env python3
"""Host smoke test for the Phase 17 Lua boundary.

This deliberately tests the Lua boundary without requiring an LVGL host port:
the firmware build covers ScriptApp's ESP-IDF integration, while this test
checks the embedded Lua version, removed host capabilities, module ABI shape,
instruction deadline, and bounded allocator behavior.
"""
from pathlib import Path
import subprocess
import sys
import tempfile


HARNESS = r'''#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static size_t used;
static int timed_out;
static struct timespec deadline;

static void *bounded_alloc(void *ud, void *ptr, size_t old_size, size_t new_size) {
    const size_t limit = *(const size_t *)ud;
    if (new_size == 0) {
        free(ptr);
        if (ptr && used >= old_size) used -= old_size;
        return NULL;
    }
    size_t next = used - (ptr ? old_size : 0);
    if (new_size > limit - (next < limit ? next : limit)) return NULL;
    void *replacement = realloc(ptr, new_size);
    if (!replacement) return NULL;
    used = next + new_size;
    return replacement;
}

static void deadline_hook(lua_State *state, lua_Debug *debug) {
    (void)debug;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec > deadline.tv_sec ||
        (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
        timed_out = 1;
        luaL_error(state, "App stopped responding (50 ms callback limit)");
    }
}

static int protected_coroutine_create(lua_State *state) {
    luaL_checktype(state, 1, LUA_TFUNCTION);
    lua_State *thread = lua_newthread(state);
    lua_pushvalue(state, 1);
    lua_xmove(state, thread, 1);
    lua_sethook(thread, deadline_hook, LUA_MASKCOUNT, 1000);
    return 1;
}

static int wrapped_coroutine_resume(lua_State *state) {
    lua_State *thread = lua_tothread(state, lua_upvalueindex(1));
    int arguments = lua_gettop(state);
    lua_xmove(state, thread, arguments);
    int results = 0;
    int status = lua_resume(thread, state, arguments, &results);
    if (status != LUA_OK && status != LUA_YIELD) {
        lua_xmove(thread, state, 1);
        return lua_error(state);
    }
    lua_xmove(thread, state, results);
    return results;
}

static int protected_coroutine_wrap(lua_State *state) {
    luaL_checktype(state, 1, LUA_TFUNCTION);
    lua_pushcfunction(state, protected_coroutine_create);
    lua_pushvalue(state, 1);
    lua_call(state, 1, 1);
    lua_pushcclosure(state, wrapped_coroutine_resume, 1);
    return 1;
}

static int fail(lua_State *state, const char *where) {
    fprintf(stderr, "%s: %s\n", where, lua_tostring(state, -1));
    lua_close(state);
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const size_t limit = 512 * 1024;
    lua_State *state = lua_newstate(bounded_alloc, (void *)&limit);
    if (!state) return 3;
    luaL_openlibs(state);
    const char *removed[] = {"os", "io", "package", "require", "load", "loadfile",
                             "loadstring", "dofile", "collectgarbage", "debug", NULL};
    for (const char **name = removed; *name; ++name) {
        lua_pushnil(state);
        lua_setglobal(state, *name);
    }
    lua_getglobal(state, "coroutine");
    lua_pushcfunction(state, protected_coroutine_create);
    lua_setfield(state, -2, "create");
    lua_pushcfunction(state, protected_coroutine_wrap);
    lua_setfield(state, -2, "wrap");
    lua_pop(state, 1);
    if (luaL_dostring(state,
            "assert(os == nil and io == nil and package == nil and require == nil and "
            "load == nil and loadstring == nil and loadfile == nil and dofile == nil and "
            "collectgarbage == nil and debug == nil)"))
        return fail(state, "sandbox");

    if (luaL_dostring(state,
            "local next_handle = 1; local function handle() next_handle = next_handle + 1; return next_handle end; "
            "local ui = { screen = function() return 1 end, "
            "set_text = function(_, text) assert(type(text) == 'string'); text_update_count = text_update_count + 1; return true end, "
            "set_value = function() return true end, list_add = function() return true end, "
            "list_insert = function() return true end, list_clear = function() return true end, "
            "timer = function(period, callback) assert(period == 1000 and type(callback) == 'function'); "
            "timer_create_count = timer_create_count + 1; last_timer_callback = callback; return timer_create_count end, "
            "timer_delete = function(id) assert(id); timer_delete_count = timer_delete_count + 1; return true end}; "
            "for _, name in ipairs({'col','label','row','arc','button','list','tabview'}) do "
            "ui[name] = function() return handle() end end; "
            "ui.tab = function() return handle() end; "
            "crystal = {ui = ui, state = {get = function() return nil end, set = function() return true end}, "
            "service = {time = {format = function() return '12:34:56' end, "
            "timezone_name = function() return 'UTC' end, now = function() return 100 end}, "
            "timer = {status = function() return {state = 'idle', remaining_sec = 0, total_sec = 0} end}}}; "
            "timer_create_count = 0; timer_delete_count = 0; text_update_count = 0"))
        return fail(state, "Clock lifecycle mock setup");
    if (luaL_loadfilex(state, argv[1], "t") != LUA_OK)
        return fail(state, "clock load");
    if (lua_pcall(state, 0, 1, 0) != LUA_OK)
        return fail(state, "clock execute");
    if (!lua_istable(state, -1)) {
        fprintf(stderr, "clock module did not return a hook table\n");
        lua_close(state);
        return 1;
    }
    lua_pushvalue(state, -1);
    lua_setglobal(state, "clock_app");
    lua_getfield(state, -1, "onCreate");
    if (!lua_isfunction(state, -1)) {
        fprintf(stderr, "clock module is missing onCreate\n");
        lua_close(state);
        return 1;
    }
    lua_pop(state, 2);
    if (luaL_dostring(state,
            "assert(clock_app.onCreate() == true); assert(timer_create_count == 1); "
            "local before = text_update_count; last_timer_callback(2); "
            "assert(text_update_count > before); "
            "assert(clock_app.onPause() == true); assert(timer_delete_count == 0); "
            "assert(clock_app.onResume() == true); assert(timer_create_count == 1)"))
        return fail(state, "Clock refresh timer lifecycle");

    if (luaL_loadstring(state,
            "local co = coroutine.create(function() while true do end end); "
            "local ok = coroutine.resume(co); assert(not ok)"))
        return fail(state, "coroutine load");
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_nsec += 50 * 1000 * 1000;
    if (deadline.tv_nsec >= 1000000000L) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000L; }
    timed_out = 0;
    lua_sethook(state, deadline_hook, LUA_MASKCOUNT, 1000);
    if (lua_pcall(state, 0, 0, 0) != LUA_OK || !timed_out)
        return fail(state, "created coroutine deadline");

    if (luaL_loadstring(state,
            "local ok = pcall(function() while true do end end); assert(not ok)"))
        return fail(state, "caught deadline load");
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_nsec += 50 * 1000 * 1000;
    if (deadline.tv_nsec >= 1000000000L) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000L; }
    timed_out = 0;
    if (lua_pcall(state, 0, 0, 0) != LUA_OK || !timed_out)
        return fail(state, "caught deadline enforcement");

    if (luaL_loadstring(state,
            "local run = coroutine.wrap(function() while true do end end); run()"))
        return fail(state, "wrapped coroutine load");
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_nsec += 50 * 1000 * 1000;
    if (deadline.tv_nsec >= 1000000000L) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000L; }
    timed_out = 0;
    if (lua_pcall(state, 0, 0, 0) == LUA_OK || !timed_out)
        return fail(state, "wrapped coroutine deadline");
    lua_pop(state, 1);

    if (luaL_loadstring(state,
            "local values = {}; while true do values[#values + 1] = string.rep('x', 1024) end"))
        return fail(state, "memory quota load");
    if (lua_pcall(state, 0, 0, 0) == LUA_OK || used > limit)
        return fail(state, "memory quota enforcement");
    lua_pop(state, 1);

    if (luaL_loadstring(state, "while true do end") != LUA_OK)
        return fail(state, "loop load");
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_nsec += 50 * 1000 * 1000;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    timed_out = 0;
    lua_sethook(state, deadline_hook, LUA_MASKCOUNT, 1000);
    const int status = lua_pcall(state, 0, 0, 0);
    lua_sethook(state, NULL, 0, 0);
    if (status == LUA_OK || !timed_out) {
        fprintf(stderr, "runaway script was not interrupted\n");
        lua_close(state);
        return 1;
    }
    lua_close(state);
    puts("phase17 Lua sandbox, ABI, coroutine deadlines, and allocator checks passed");
    return 0;
}
'''


URL_HARNESS = r'''#include "crystal_script_url.hpp"
#include <cstdio>

int main() {
    const std::vector<std::string> hosts = {"api.example.com"};
    const struct { const char *url; bool allowed; } cases[] = {
        {"https://api.example.com/v1/data", true},
        {"http://API.EXAMPLE.COM:8080/", true},
        {"://api.example.com/", false},
        {"https://api.example.com:bad/", false},
        {"https://api.example.com:65536/", false},
        {"https://api.example.com@evil.test/", false},
        {"https://api.example.com:443@evil.test/", false},
        {"https://evil-api.example.com/", false},
        {"https://evil.test/?next=https://api.example.com", false},
    };
    for (const auto &test : cases) {
        if (crystal_script_host_allowed(test.url, hosts) != test.allowed) {
            std::fprintf(stderr, "host policy mismatch: %s\n", test.url);
            return 1;
        }
    }
    std::puts("phase17 exact-host and redirect-boundary checks passed");
    return 0;
}
'''


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    lua = root / "managed_components" / "georgik__lua" / "lua"
    include = root / "managed_components" / "georgik__lua" / "include"
    component_sources = {
        "lapi.c", "lauxlib.c", "lbaselib.c", "lcode.c", "lctype.c", "lcorolib.c",
        "ldblib.c", "ldebug.c", "ldo.c", "lfunc.c", "lgc.c", "linit.c", "liolib.c",
        "llex.c", "lmathlib.c", "lmem.c", "lopcodes.c", "loadlib.c", "loslib.c",
        "lstate.c", "ltable.c", "ltm.c", "lvm.c", "lobject.c", "lparser.c",
        "lstrlib.c", "lstring.c", "ltablib.c", "ldump.c", "lundump.c", "lutf8lib.c",
        "lzio.c",
    }
    sources = sorted(lua / name for name in component_sources)
    if not sources:
        print("managed Lua sources are missing; run idf.py reconfigure first", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="crystal-phase17-") as directory:
        directory = Path(directory)
        harness = directory / "harness.c"
        binary = directory / "phase17-lua"
        url_harness = directory / "url_harness.cpp"
        url_binary = directory / "phase17-url"
        harness.write_text(HARNESS)
        url_harness.write_text(URL_HARNESS)
        command = ["cc", "-std=c11", "-O2", "-DLUA_USE_POSIX", "-D_POSIX_C_SOURCE=200809L",
                   f"-I{lua}", f"-I{include}", str(harness), *(str(p) for p in sources),
                   "-lm", "-ldl", "-o", str(binary)]
        subprocess.run(command, cwd=root, check=True)
        subprocess.run([str(binary), str(root / "examples" / "clock3p" / "main.lua")],
                       cwd=root, check=True)
        subprocess.run(["c++", "-std=c++17", f"-I{root / 'components/crystal_script/include'}",
                        str(url_harness), "-o", str(url_binary)], cwd=root, check=True)
        subprocess.run([str(url_binary)], cwd=root, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
