/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

#include "crystal_script.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <new>
#include <sys/stat.h>
#include <unistd.h>

#include "crystal_core.hpp"
#include "crystal_hal.hpp"
#include "crystal_package.hpp"
#include "crystal_script_url.hpp"
#include "crystal_network.h"
#include "crystal_http.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

namespace {
constexpr const char *TAG = "crystal_script";
constexpr size_t kMaxSourceBytes = 64 * 1024;
constexpr size_t kMaxStateBytes = 32 * 1024;
constexpr size_t kMaxFileBytes = 64 * 1024;
constexpr size_t kMaxResponseBytes = 64 * 1024;
constexpr size_t kMaxLuaString = 4096;
constexpr size_t kMaxJsonNodes = 512;
constexpr unsigned kMaxJsonDepth = 16;
constexpr size_t kMaxCallbacks = 64;
constexpr uint32_t kHttpDispatchMs = 50;
std::atomic<uint32_t> s_next_owner{1};

std::string package_path(const std::string &id, const char *relative)
{
    return std::string("/littlefs/apps/") + id + "/" + relative;
}

bool valid_relative(const char *path, std::string *out)
{
    if (!path || !*path || !out) return false;
    const size_t length = strnlen(path, 161);
    if (!length || length > 160 || path[0] == '/' || path[length - 1] == '/' || strchr(path, '\\')) return false;
    size_t start = 0;
    for (size_t i = 0; i <= length; ++i) {
        if (i != length && path[i] != '/') continue;
        const size_t size = i - start;
        if (!size || size > 63 || (size == 1 && path[start] == '.') ||
            (size == 2 && path[start] == '.' && path[start + 1] == '.')) return false;
        for (size_t j = start; j < i; ++j) {
            const unsigned char c = static_cast<unsigned char>(path[j]);
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
        }
        start = i + 1;
    }
    *out = path;
    return true;
}

bool tree_bytes(const std::string &directory, size_t *total)
{
    if (!total) return false;
    *total = 0;
    DIR *dir = opendir(directory.c_str());
    if (!dir) return errno == ENOENT;
    bool ok = true;
    while (auto *entry = readdir(dir)) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        struct stat st{};
        const std::string path = directory + "/" + entry->d_name;
        if (stat(path.c_str(), &st) != 0) { ok = false; break; }
        if (S_ISDIR(st.st_mode)) {
            size_t nested = 0;
            if (!tree_bytes(path, &nested) || nested > SIZE_MAX - *total) { ok = false; break; }
            *total += nested;
        } else if (S_ISREG(st.st_mode) && st.st_size >= 0) {
            if (static_cast<uint64_t>(st.st_size) > SIZE_MAX - *total) { ok = false; break; }
            *total += static_cast<size_t>(st.st_size);
        } else { ok = false; break; }
    }
    closedir(dir);
    return ok;
}

bool make_parent_dirs(const std::string &path)
{
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return false;
    const std::string parent = path.substr(0, slash);
    // Package data paths are absolute. Skip the root separator instead of
    // trying mkdir("") and preserve it when appending the first component.
    std::string current = path.front() == '/' ? "/" : "";
    size_t start = current.size();
    while (start < parent.size()) {
        const size_t end = parent.find('/', start);
        if (!current.empty() && current.back() != '/') current.push_back('/');
        current.append(parent, start, end == std::string::npos ? end : end - start);
        if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) return false;
        struct stat st{};
        if (stat(current.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

bool write_all(int fd, const char *data, size_t length)
{
    size_t written = 0;
    while (written < length) {
        const ssize_t amount = write(fd, data + written, length - written);
        if (amount < 0 && errno == EINTR) continue;
        if (amount <= 0) return false;
        written += static_cast<size_t>(amount);
    }
    return true;
}

const cJSON *json_field(const cJSON *object, const char *name)
{
    return object ? cJSON_GetObjectItemCaseSensitive(object, name) : nullptr;
}

bool finite_integer(const cJSON *item, int minimum, int maximum, int *value)
{
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        std::floor(item->valuedouble) != item->valuedouble ||
        item->valuedouble < minimum || item->valuedouble > maximum) return false;
    *value = static_cast<int>(item->valuedouble);
    return true;
}

bool lua_uint(lua_State *state, int index, uint32_t minimum, uint32_t maximum, uint32_t *out)
{
    if (!lua_isinteger(state, index) || !out) return false;
    const lua_Integer number = lua_tointeger(state, index);
    if (number < minimum || static_cast<uint64_t>(number) > maximum) return false;
    *out = static_cast<uint32_t>(number);
    return true;
}

bool json_int(const cJSON *object, const char *name, int minimum, int maximum, int *value)
{
    const cJSON *field = json_field(object, name);
    return !field || finite_integer(field, minimum, maximum, value);
}
} // namespace

struct ScriptApp::Block {
    size_t size;
    Block *previous;
    Block *next;
    bool free;
};

ScriptApp::ScriptApp(const CrystalPackageManifest &manifest)
    : CrystalApp(manifest.name.c_str()), manifest_(manifest), async_(std::make_shared<AsyncState>())
{
    setStartupBudget(200);
    owner_id_ = s_next_owner.fetch_add(1);
    if (owner_id_ == 0) owner_id_ = s_next_owner.fetch_add(1);
    ui_.configure(build_widget, apply_style,
        [](lv_obj_t *object, const char *path, void *context) {
            return static_cast<ScriptApp *>(context)->load_image(object, path);
        }, this);
}

ScriptApp::~ScriptApp()
{
    if (lua_ || arena_ || root_ || http_timer_) (void)onDestroy();
}

void *ScriptApp::lua_allocate(void *context, void *pointer, size_t, size_t new_size)
{
    auto *app = static_cast<ScriptApp *>(context);
    if (new_size == 0) { app->arena_release(pointer); return nullptr; }
    if (new_size > SIZE_MAX - alignof(std::max_align_t)) {
        return nullptr;
    }
    if (!pointer) {
        void *result = app->arena_allocate(new_size);
        return result;
    }
    auto *block = static_cast<Block *>(pointer) - 1;
    const size_t previous_size = block->size;
    const size_t alignment = alignof(std::max_align_t);
    new_size = (new_size + alignment - 1) & ~(alignment - 1);
    if (new_size <= previous_size) {
        if (new_size < previous_size && previous_size - new_size >= sizeof(Block) + alignment) {
            auto *split = reinterpret_cast<Block *>(reinterpret_cast<uint8_t *>(block + 1) + new_size);
            split->size = previous_size - new_size - sizeof(Block);
            split->previous = block;
            split->next = block->next;
            split->free = true;
            if (split->next) split->next->previous = split;
            block->next = split;
            block->size = new_size;
            app->arena_used_ -= previous_size - new_size;
            if (split->next && split->next->free) {
                Block *next = split->next;
                split->size += sizeof(Block) + next->size;
                split->next = next->next;
                if (split->next) split->next->previous = split;
            }
        }
        return pointer;
    }
    if (block->next && block->next->free &&
        previous_size + sizeof(Block) + block->next->size >= new_size) {
        Block *next = block->next;
        const size_t combined = previous_size + sizeof(Block) + next->size;
        block->size = new_size;
        block->next = next->next;
        if (block->next) block->next->previous = block;
        if (combined - new_size >= sizeof(Block) + alignment) {
            auto *split = reinterpret_cast<Block *>(reinterpret_cast<uint8_t *>(block + 1) + new_size);
            split->size = combined - new_size - sizeof(Block);
            split->previous = block;
            split->next = block->next;
            split->free = true;
            if (split->next) split->next->previous = split;
            block->next = split;
        } else {
            block->size = combined;
        }
        app->arena_used_ += block->size - previous_size;
        if (app->arena_used_ > app->arena_peak_) app->arena_peak_ = app->arena_used_;
        return pointer;
    }
    void *replacement = app->arena_allocate(new_size);
    if (!replacement) return nullptr;
    memcpy(replacement, pointer, previous_size);
    app->arena_release(pointer);
    return replacement;
}

void *ScriptApp::arena_allocate(size_t bytes)
{
    if (!arena_ || !bytes) return nullptr;
    constexpr size_t alignment = alignof(std::max_align_t);
    if (bytes > SIZE_MAX - alignment) return nullptr;
    bytes = (bytes + alignment - 1) & ~(alignment - 1);
    auto *first = static_cast<Block *>(arena_);
    for (Block *block = first; block; block = block->next) {
        if (!block->free || block->size < bytes) continue;
        const size_t remaining = block->size - bytes;
        if (remaining >= sizeof(Block) + alignment) {
            auto *split = reinterpret_cast<Block *>(reinterpret_cast<uint8_t *>(block + 1) + bytes);
            split->size = remaining - sizeof(Block);
            split->previous = block;
            split->next = block->next;
            split->free = true;
            if (split->next) split->next->previous = split;
            block->next = split;
            block->size = bytes;
        }
        block->free = false;
        arena_used_ += block->size;
        if (arena_used_ > arena_peak_) arena_peak_ = arena_used_;
        return block + 1;
    }
    return nullptr;
}

void ScriptApp::arena_release(void *pointer)
{
    if (!pointer || !arena_) return;
    auto *block = static_cast<Block *>(pointer) - 1;
    if (reinterpret_cast<uint8_t *>(block) < arena_ ||
        reinterpret_cast<uint8_t *>(block) >= static_cast<uint8_t *>(arena_) + arena_size_ || block->free) return;
    block->free = true;
    arena_used_ -= block->size;
    if (block->next && block->next->free) {
        Block *next = block->next;
        block->size += sizeof(Block) + next->size;
        block->next = next->next;
        if (block->next) block->next->previous = block;
    }
    if (block->previous && block->previous->free) {
        Block *previous = block->previous;
        previous->size += sizeof(Block) + block->size;
        previous->next = block->next;
        if (previous->next) previous->next->previous = previous;
    }
}

void ScriptApp::instruction_hook(lua_State *state, lua_Debug *)
{
    auto *app = *static_cast<ScriptApp **>(lua_getextraspace(state));
    if (app && esp_timer_get_time() > app->deadline_us_) {
        app->deadline_exceeded_ = true;
        luaL_error(state, "App stopped responding (50 ms callback limit)");
    }
}

ScriptApp *ScriptApp::self(lua_State *state)
{
    return static_cast<ScriptApp *>(lua_touserdata(state, lua_upvalueindex(1)));
}

void ScriptApp::push_module(const char *name, const luaL_Reg *functions)
{
    lua_newtable(lua_);
    for (const luaL_Reg *entry = functions; entry->name; ++entry) {
        lua_pushlightuserdata(lua_, this);
        lua_pushcclosure(lua_, entry->func, 1);
        lua_setfield(lua_, -2, entry->name);
    }
    lua_setfield(lua_, -2, name);
}

void ScriptApp::install_sandbox()
{
    luaL_openlibs(lua_);
    const char *removed[] = {"os", "io", "package", "require", "dofile", "loadfile", "load", "loadstring", "collectgarbage", "debug", nullptr};
    for (const char **name = removed; *name; ++name) { lua_pushnil(lua_); lua_setglobal(lua_, *name); }
    // Lua hooks are per-thread. Wrap both standard coroutine constructors so
    // script-created threads inherit the same per-callback deadline.
    lua_getglobal(lua_, "coroutine");
    if (lua_istable(lua_, -1)) {
        lua_pushcfunction(lua_, coroutine_create); lua_setfield(lua_, -2, "create");
        lua_pushcfunction(lua_, coroutine_wrap); lua_setfield(lua_, -2, "wrap");
    }
    lua_pop(lua_, 1);
    lua_pushlightuserdata(lua_, this);
    lua_pushcclosure(lua_, print_binding, 1);
    lua_setglobal(lua_, "print");
}

int ScriptApp::coroutine_create(lua_State *state)
{
    luaL_checktype(state, 1, LUA_TFUNCTION);
    auto *app = *static_cast<ScriptApp **>(lua_getextraspace(state));
    lua_State *thread = lua_newthread(state);
    *static_cast<ScriptApp **>(lua_getextraspace(thread)) = app;
    lua_pushvalue(state, 1);
    lua_xmove(state, thread, 1);
    lua_sethook(thread, instruction_hook, LUA_MASKCOUNT, 10000);
    return 1;
}

int ScriptApp::coroutine_wrap(lua_State *state)
{
    luaL_checktype(state, 1, LUA_TFUNCTION);
    lua_pushcfunction(state, coroutine_create);
    lua_pushvalue(state, 1);
    lua_call(state, 1, 1);
    lua_pushcclosure(state, coroutine_wrap_resume, 1);
    return 1;
}

int ScriptApp::coroutine_wrap_resume(lua_State *state)
{
    lua_State *thread = lua_tothread(state, lua_upvalueindex(1));
    if (!thread) return luaL_error(state, "invalid coroutine");
    const int arguments = lua_gettop(state);
    lua_xmove(state, thread, arguments);
    int results = 0;
    const int status = lua_resume(thread, state, arguments, &results);
    if (status != LUA_OK && status != LUA_YIELD) {
        lua_xmove(thread, state, 1);
        return lua_error(state);
    }
    lua_xmove(thread, state, results);
    return results;
}

int ScriptApp::print_binding(lua_State *state)
{
    char line[320] = {};
    size_t used = 0;
    const int count = lua_gettop(state);
    for (int i = 1; i <= count && used + 2 < sizeof(line); ++i) {
        lua_getglobal(state, "tostring"); lua_pushvalue(state, i);
        if (lua_pcall(state, 1, 1, 0) != LUA_OK) { lua_pop(state, 1); continue; }
        size_t length = 0; const char *part = lua_tolstring(state, -1, &length);
        if (i > 1) line[used++] = '\t';
        if (part && length) {
            const size_t copy = std::min(length, sizeof(line) - used - 2);
            memcpy(line + used, part, copy); used += copy;
        }
        lua_pop(state, 1);
    }
    ESP_LOGI(TAG, "%s", line);
    return 0;
}

void ScriptApp::install_api()
{
    static const luaL_Reg ui_functions[] = {
        {"screen", ui_screen}, {"tab", ui_tab}, {"focus", ui_focus}, {"set_focus", ui_focus}, {"delete", ui_delete},
        {"set_text", ui_set_text}, {"get_text", ui_get_text}, {"set_value", ui_set_value}, {"get_value", ui_get_value},
        {"set_hidden", ui_set_hidden}, {"set_enabled", ui_set_enabled}, {"set_style", ui_set_style},
        {"scroll_to", ui_scroll_to}, {"set_parent", ui_set_parent}, {"get_size", ui_get_size},
        {"on", ui_on}, {"off", ui_off}, {"list_add", ui_list_add}, {"list_insert", ui_list_insert},
        {"list_set_text", ui_list_set_text},
        {"list_remove", ui_list_remove}, {"list_clear", ui_list_clear}, {"list_count", ui_list_count},
        {"chart_add_series", ui_chart_add_series}, {"chart_set_data", ui_chart_set_data},
        {"chart_set_range", ui_chart_set_range}, {"chart_clear", ui_chart_clear},
        {"timer", ui_timer}, {"after", ui_after}, {"timer_delete", ui_timer_delete},
        {"anim", ui_anim}, {"anim_cancel", ui_anim_cancel}, {nullptr, nullptr}
    };
    static const luaL_Reg state_functions[] = {
        {"get", state_get}, {"set", state_set}, {"erase", state_erase}, {"flush", state_flush}, {nullptr, nullptr}
    };
    static const luaL_Reg json_functions[] = {{"encode", json_encode}, {"decode", json_decode}, {nullptr, nullptr}};
    static const luaL_Reg fs_functions[] = {
        {"read", fs_read}, {"write", fs_write}, {"exists", fs_exists}, {"list", fs_list}, {nullptr, nullptr}
    };
    static const luaL_Reg time_functions[] = {
        {"now", service_time_now}, {"format", service_time_format}, {"timezone_name", service_time_zone}, {nullptr, nullptr}
    };
    static const luaL_Reg timer_functions[] = {
        {"arm", service_timer_arm}, {"start", service_timer_start}, {"pause", service_timer_pause},
        {"resume", service_timer_resume}, {"reset", service_timer_reset}, {"status", service_timer_status}, {nullptr, nullptr}
    };
    static const luaL_Reg notify_functions[] = {{"toast", service_toast}, {"chime", service_chime}, {nullptr, nullptr}};
    static const luaL_Reg net_functions[] = {{"status", service_net_status}, {nullptr, nullptr}};
    static const luaL_Reg location_functions[] = {{"get", service_location_get}, {nullptr, nullptr}};
    static const luaL_Reg system_functions[] = {{"info", service_system_info}, {nullptr, nullptr}};
    static const luaL_Reg http_functions[] = {{"fetch", http_fetch}, {"cancel", http_cancel}, {nullptr, nullptr}};

    lua_newtable(lua_);
    push_module("ui", ui_functions);
    // Creator names are closures whose second upvalue is the fixed ABI type.
    lua_getfield(lua_, -1, "ui");
    const char *creators[] = {"container", "row", "col", "scroll_view", "label", "button", "image", "list",
        "slider", "switch", "arc", "chart", "textarea", "tabview", "spinner", "dialog", nullptr};
    for (const char **type = creators; *type; ++type) {
        lua_pushlightuserdata(lua_, this);
        lua_pushstring(lua_, *type);
        lua_pushcclosure(lua_, ui_create, 2);
        lua_setfield(lua_, -2, *type);
    }
    lua_pop(lua_, 1);
    push_module("state", state_functions);
    push_module("json", json_functions);
    push_module("fs", fs_functions);
    lua_newtable(lua_);
    push_module("time", time_functions);
    push_module("timer", timer_functions);
    push_module("notify", notify_functions);
    push_module("net", net_functions);
    push_module("location", location_functions);
    push_module("system", system_functions);
    push_module("http", http_functions);
    lua_setfield(lua_, -2, "service");
    lua_setglobal(lua_, "crystal");
}

bool ScriptApp::onCreate()
{
    if (lua_ || root_) (void)onDestroy();
    if (async_) {
        std::lock_guard<std::mutex> lock(async_->mutex);
        async_->active = false;
    }
    async_ = std::make_shared<AsyncState>();
    deadline_exceeded_ = false;
    runtime_failed_ = false;
    native_internal_baseline_ = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    native_psram_baseline_ = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    native_internal_low_water_ = native_internal_baseline_;
    native_psram_low_water_ = native_psram_baseline_;
    arena_peak_ = 0;
    json_peak_internal_bytes_ = 0;
    json_peak_psram_bytes_ = 0;
    json_decode_count_ = 0;
    json_largest_input_bytes_ = 0;
    arena_size_ = static_cast<size_t>(manifest_.memory_kb) * 1024;
    arena_ = resources().allocate(arena_size_);
    if (!arena_ || arena_size_ <= sizeof(Block) + alignof(std::max_align_t)) return false;
    auto *first = static_cast<Block *>(arena_);
    first->size = arena_size_ - sizeof(Block); first->previous = nullptr; first->next = nullptr; first->free = true;
    lua_ = lua_newstate(lua_allocate, this);
    if (!lua_) return false;
    *static_cast<ScriptApp **>(lua_getextraspace(lua_)) = this;
    install_sandbox();
    install_api();

    const lv_area_t area = getVisualArea();
    root_ = lv_obj_create(lv_scr_act());
    if (!root_ || !trackObject(root_)) { if (root_) lv_obj_del(root_); root_ = nullptr; return false; }
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, lv_area_get_width(&area), lv_area_get_height(&area));
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x11181F), 0);
    lv_obj_set_style_text_color(root_, lv_color_hex(0xE6EDF5), 0);
    lv_obj_set_style_text_font(root_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_pad_all(root_, 16, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    ui_.set_root(root_);
    if (!ui_.adopt(root_)) return false;

    std::string source;
    if (!crystal_package_read_file(manifest_.id, manifest_.entry, &source, kMaxSourceBytes)) return false;
    deadline_us_ = esp_timer_get_time() + 50000;
    lua_sethook(lua_, instruction_hook, LUA_MASKCOUNT, 10000);
    deadline_exceeded_ = false;
    int status = luaL_loadbufferx(lua_, source.data(), source.size(), manifest_.entry.c_str(), "t");
    if (status == LUA_OK) status = lua_pcall(lua_, 0, 1, 0);
    if (status != LUA_OK || deadline_exceeded_) {
        const char *message = deadline_exceeded_ ? "App stopped responding (50 ms callback limit)" :
            status == LUA_ERRMEM ? "Per-app Lua memory quota exceeded" : lua_tostring(lua_, -1);
        runtime_error(message);
        lua_pop(lua_, 1);
        return false;
    }
    if (!lua_istable(lua_, -1)) { lua_pop(lua_, 1); ESP_LOGE(TAG, "%s module must return a hook table", manifest_.id.c_str()); return false; }
    hooks_ref_ = luaL_ref(lua_, LUA_REGISTRYINDEX);
    return execute("onCreate");
}

// All allocating argument setup and hook lookup run inside lua_pcall. A
// failed first allocation is normal: Lua can collect garbage and retry it.
// Only the protected call's final LUA_ERRMEM means the app exhausted its quota.
int ScriptApp::protected_invoke(lua_State *state)
{
    auto *call = static_cast<Invocation *>(lua_touserdata(state, 1));
    auto *app = call->app;
    if (call->hook) {
        lua_rawgeti(state, LUA_REGISTRYINDEX, app->hooks_ref_);
        lua_getfield(state, -1, call->hook);
        lua_remove(state, -2);
        if (lua_isnil(state, -1)) { lua_pushboolean(state, !call->required); return 1; }
        if (!lua_isfunction(state, -1)) return luaL_error(state, "Lifecycle hook is not a function");
        lua_call(state, 0, 1);
        const bool is_back = !strcmp(call->hook, "onBack");
        lua_pushboolean(state, is_back ? (lua_isboolean(state, -1) && lua_toboolean(state, -1)) :
            !(lua_isboolean(state, -1) && !lua_toboolean(state, -1)));
    } else {
        lua_rawgeti(state, LUA_REGISTRYINDEX, call->ref);
        if (call->response) {
            lua_pushinteger(state, call->response->id);
            lua_pushinteger(state, call->response->status);
            lua_pushlstring(state, call->response->body.data(), call->response->body.size());
            lua_call(state, 3, 0);
        } else {
            if (call->message) lua_pushstring(state, call->message);
            else lua_pushinteger(state, call->handle);
            lua_call(state, 1, 0);
        }
        lua_pushboolean(state, true);
    }
    return 1;
}

bool ScriptApp::invoke(Invocation &invocation)
{
    if (!lua_ || runtime_failed_) return false;
    const int top = lua_gettop(lua_);
    if (!lua_checkstack(lua_, 2)) { runtime_error("Per-app Lua memory quota exceeded"); return false; }
    lua_pushcfunction(lua_, protected_invoke); // Zero-upvalue C function: no allocation.
    lua_pushlightuserdata(lua_, &invocation);
    deadline_us_ = esp_timer_get_time() + 50000;
    deadline_exceeded_ = false;
    const int status = lua_pcall(lua_, 1, 1, 0);
    const bool accepted = status == LUA_OK && lua_toboolean(lua_, -1);
    if (status != LUA_OK || deadline_exceeded_) {
        const char *message = deadline_exceeded_ ? "App stopped responding (50 ms callback limit)" :
            status == LUA_ERRMEM ? "Per-app Lua memory quota exceeded" : lua_tostring(lua_, -1);
        runtime_error(message ? message : "Lua callback failed");
    }
    // A successful zero-result callback has no error to pop. Always restore
    // the saved stack depth, including when a caught deadline remains fatal.
    lua_settop(lua_, top);
    return accepted && !runtime_failed_;
}

bool ScriptApp::execute(const char *hook, bool required)
{
    if (!lua_ || hooks_ref_ == LUA_NOREF) return !required;
    Invocation call{this}; call.hook = hook; call.required = required;
    return invoke(call);
}

void ScriptApp::runtime_error(const char *message)
{
    runtime_failed_ = true; // Never re-enter a failed VM from timers or teardown hooks.
    const char *detail = message ? message : "Lua runtime error";
    ESP_LOGE(TAG, "%s: %s", manifest_.id.c_str(), detail);
    char toast[128];
    snprintf(toast, sizeof(toast), "App stopped: %.96s", detail);
    crystal_core_show_toast(toast);
    if (!in_destroy_ && lifecycle_state() != LifecycleState::Installed &&
        lifecycle_state() != LifecycleState::Destroyed) schedule_runtime_close();
}

void ScriptApp::schedule_runtime_close()
{
    if (runtime_close_scheduled_ || lifecycle_state() == LifecycleState::Installed ||
        lifecycle_state() == LifecycleState::Destroyed) return;
    runtime_close_scheduled_ = true;
    runtime_close_timer_ = lv_timer_create(runtime_close_dispatch, 1, this);
    if (!runtime_close_timer_) { runtime_close_scheduled_ = false; return; }
    runtime_close_resource_ = resources().track(CrystalResourceKind::Timer, runtime_close_timer_);
    if (!runtime_close_resource_) {
        lv_timer_del(runtime_close_timer_);
        runtime_close_timer_ = nullptr;
        runtime_close_scheduled_ = false;
        return;
    }
    lv_timer_set_repeat_count(runtime_close_timer_, 1);
}

void ScriptApp::runtime_close_dispatch(lv_timer_t *timer)
{
    auto *app = static_cast<ScriptApp *>(timer->user_data);
    if (!app) return;
    app->runtime_close_timer_ = nullptr;
    if (app->runtime_close_resource_) {
        (void)app->resources().release(app->runtime_close_resource_);
        app->runtime_close_resource_ = 0;
    }
    app->runtime_close_scheduled_ = false;
    (void)app->notifyCoreClosed();
}

bool ScriptApp::onPause()
{
    (void)execute("onPause");
    cancel_requests();
    ui_.pause();
    return true;
}

bool ScriptApp::onResume()
{
    ui_.resume();
    return execute("onResume");
}

bool ScriptApp::onBack()
{
    if (!runtime_failed_ && execute("onBack", true)) return true;
    return CrystalApp::onBack();
}

void ScriptApp::release_callbacks()
{
    if (lua_) {
        for (auto &entry : callbacks_) if (entry && entry->ref != LUA_NOREF) {
            luaL_unref(lua_, LUA_REGISTRYINDEX, entry->ref);
            entry->ref = LUA_NOREF;
        }
    }
    callbacks_.clear(); event_callbacks_.clear();
    for (auto &entry : request_callbacks_) if (lua_ && entry.second != LUA_NOREF) luaL_unref(lua_, LUA_REGISTRYINDEX, entry.second);
    request_callbacks_.clear();
}

bool ScriptApp::onDestroy()
{
    const size_t quota_before_release = arena_size_;
    const size_t used_before_release = arena_used_;
    const size_t peak_before_release = arena_peak_;
    in_destroy_ = true;
    (void)execute("onDestroy");
    cancel_requests();
    if (async_) {
        std::lock_guard<std::mutex> lock(async_->mutex);
        async_->active = false;
        async_->completed.clear();
    }
    active_requests_.clear();
    if (http_timer_) { lv_timer_del(http_timer_); http_timer_ = nullptr; }
    if (http_timer_resource_) { resources().release(http_timer_resource_); http_timer_resource_ = 0; }
    if (runtime_close_timer_) { lv_timer_del(runtime_close_timer_); runtime_close_timer_ = nullptr; }
    if (runtime_close_resource_) { resources().release(runtime_close_resource_); runtime_close_resource_ = 0; }
    runtime_close_scheduled_ = false;
    ui_.clear_callbacks();
    release_callbacks();
    if (root_) { lv_obj_del(root_); root_ = nullptr; }
    ui_.reset();
    images_.clear();
    if (lua_) {
        if (hooks_ref_ != LUA_NOREF) { luaL_unref(lua_, LUA_REGISTRYINDEX, hooks_ref_); hooks_ref_ = LUA_NOREF; }
        lua_close(lua_); lua_ = nullptr;
    }
    if (arena_) { resources().deallocate(arena_); arena_ = nullptr; arena_size_ = 0; arena_used_ = 0; }
    const auto cleanup = resources().report();
    ESP_LOGI(TAG, "resource-checkpoint teardown id=%s lua_kb=%u lua_used_kb=%u lua_peak_kb=%u internal_free=%u largest_internal=%u psram_free=%u tracked=%zu clean=%u",
             manifest_.id.c_str(), static_cast<unsigned>(quota_before_release / 1024),
             static_cast<unsigned>(used_before_release / 1024), static_cast<unsigned>(peak_before_release / 1024),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
             cleanup.tasks + cleanup.timers + cleanup.queues + cleanup.allocations + cleanup.objects,
             cleanup.clean() ? 1U : 0U);
    if (!async_) async_ = std::make_shared<AsyncState>();
    in_destroy_ = false;
    return true;
}

ScriptApp::CallbackRef *ScriptApp::make_callback(int index)
{
    if (!lua_ || !lua_isfunction(lua_, index) || callbacks_.size() >= kMaxCallbacks) return nullptr;
    index = lua_absindex(lua_, index);
    lua_pushvalue(lua_, index);
    auto callback = std::make_unique<CallbackRef>();
    callback->app = this;
    callback->ref = luaL_ref(lua_, LUA_REGISTRYINDEX);
    auto *result = callback.get(); callbacks_.push_back(std::move(callback));
    return result;
}

void ScriptApp::ui_event(CrystalUI::Handle handle, void *context)
{
    auto *callback = static_cast<CallbackRef *>(context);
    if (callback && callback->app) (void)callback->app->call_ref(callback->ref, handle);
}
void ScriptApp::ui_timer(CrystalUI::Handle handle, void *context) { ui_event(handle, context); }
void ScriptApp::ui_anim_done(CrystalUI::Handle handle, void *context) { ui_event(handle, context); }

bool ScriptApp::call_ref(int ref, CrystalUI::Handle handle, const char *message)
{
    if (!lua_ || ref == LUA_NOREF || in_destroy_ || runtime_failed_) return false;
    Invocation call{this}; call.ref = ref; call.handle = handle; call.message = message;
    return invoke(call);
}

lv_obj_t *ScriptApp::build_widget(const cJSON *spec, lv_obj_t *parent, void *context)
{
    auto *app = static_cast<ScriptApp *>(context);
    const cJSON *type_item = json_field(spec, "type");
    if (!app || !parent || !cJSON_IsString(type_item) || !type_item->valuestring) return nullptr;
    const char *type = type_item->valuestring;
    lv_obj_t *object = nullptr;
    lv_obj_t *text = nullptr;
    const bool container = !strcmp(type, "container") || !strcmp(type, "row") || !strcmp(type, "col") || !strcmp(type, "scroll_view");
    if (container) object = lv_obj_create(parent);
    else if (!strcmp(type, "label")) object = lv_label_create(parent);
    else if (!strcmp(type, "button")) { object = lv_btn_create(parent); if (object) text = lv_label_create(object); }
    else if (!strcmp(type, "image")) object = lv_img_create(parent);
    else if (!strcmp(type, "list")) object = lv_list_create(parent);
    else if (!strcmp(type, "slider")) object = lv_slider_create(parent);
    else if (!strcmp(type, "switch")) object = lv_switch_create(parent);
    else if (!strcmp(type, "arc")) object = lv_arc_create(parent);
    else if (!strcmp(type, "chart")) object = lv_chart_create(parent);
    else if (!strcmp(type, "textarea")) object = lv_textarea_create(parent);
    else if (!strcmp(type, "spinner")) object = lv_spinner_create(parent, 1000, 60);
    else if (!strcmp(type, "tabview")) object = lv_tabview_create(parent, LV_DIR_TOP, 44);
    else if (!strcmp(type, "dialog")) {
        const cJSON *title = json_field(spec, "title"), *message = json_field(spec, "message");
        if (!cJSON_IsString(title) || !cJSON_IsString(message)) return nullptr;
        static const char *buttons[] = {"OK", ""};
        object = lv_msgbox_create(nullptr, title->valuestring, message->valuestring, buttons, true);
        if (object) {
            lv_obj_t *backdrop = lv_obj_get_parent(object);
            lv_obj_set_parent(backdrop, app->root_);
            lv_obj_add_flag(backdrop, LV_OBJ_FLAG_FLOATING);
            lv_obj_set_size(backdrop, LV_PCT(100), LV_PCT(100));
            lv_obj_center(object);
        }
    }
    if (!object) return nullptr;
    if (!app->ui_.adopt(object, text)) { lv_obj_del(object); return nullptr; }
    if (container) {
        lv_obj_remove_style_all(object);
        lv_obj_set_width(object, LV_PCT(100));
        lv_obj_set_height(object, LV_SIZE_CONTENT);
        if (!strcmp(type, "scroll_view")) lv_obj_add_flag(object, LV_OBJ_FLAG_SCROLLABLE);
        else lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(object, !strcmp(type, "row") ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(object, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    }

    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    if (!strcmp(type, "label")) {
        lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(object, lv_color_hex(0xE6EDF5), 0);
    } else if (!strcmp(type, "button")) {
        lv_obj_set_style_bg_color(object, lv_color_hex(0x273541), 0);
        lv_obj_set_style_text_color(object, lv_color_hex(0xF4F7FA), 0);
        lv_obj_set_style_radius(object, 8, 0);
        // Let the label inherit the button's text style, including later
        // set_style updates (active tabs, left-aligned rows, disabled actions).
        lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
        if (text) { lv_obj_set_width(text, LV_PCT(100)); lv_obj_center(text); }
    } else if (!strcmp(type, "arc")) {
        lv_obj_set_style_arc_color(object, lv_color_hex(0x344553), LV_PART_MAIN);
        lv_obj_set_style_arc_color(object, lv_color_hex(0x53C8B6), LV_PART_INDICATOR);
    } else if (!strcmp(type, "slider") || !strcmp(type, "switch")) {
        lv_obj_set_style_bg_color(object, lv_color_hex(0x344553), LV_PART_MAIN);
        lv_obj_set_style_bg_color(object, lv_color_hex(0x53C8B6), LV_PART_INDICATOR);
    } else if (!strcmp(type, "list") || !strcmp(type, "chart") || !strcmp(type, "textarea")) {
        lv_obj_set_style_bg_color(object, lv_color_hex(0x1C2733), LV_PART_MAIN);
        lv_obj_set_style_text_color(object, lv_color_hex(0xE6EDF5), LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    }

    int dimension = 0;
    if (!json_int(spec, "width", 1, 960, &dimension) && json_field(spec, "width")) { lv_obj_del(object); return nullptr; }
    if (json_field(spec, "width")) lv_obj_set_width(object, dimension);
    if (!json_int(spec, "height", 1, 960, &dimension) && json_field(spec, "height")) { lv_obj_del(object); return nullptr; }
    if (json_field(spec, "height")) lv_obj_set_height(object, dimension);
    if (!json_int(spec, "min_height", 1, 480, &dimension) && json_field(spec, "min_height")) { lv_obj_del(object); return nullptr; }
    if (json_field(spec, "min_height")) lv_obj_set_style_min_height(object, dimension, 0);
    if (!json_int(spec, "gap", 0, 128, &dimension) && json_field(spec, "gap")) { lv_obj_del(object); return nullptr; }
    if (json_field(spec, "gap")) { lv_obj_set_style_pad_row(object, dimension, 0); lv_obj_set_style_pad_column(object, dimension, 0); }

    const cJSON *text_value = json_field(spec, "text");
    if (cJSON_IsString(text_value)) {
        if (!strcmp(type, "label")) lv_label_set_text(object, text_value->valuestring);
        else if (!strcmp(type, "button") && text) lv_label_set_text(text, text_value->valuestring);
        else if (!strcmp(type, "textarea")) lv_textarea_set_text(object, text_value->valuestring);
    }
    const cJSON *placeholder = json_field(spec, "placeholder");
    if (!strcmp(type, "textarea") && cJSON_IsString(placeholder)) lv_textarea_set_placeholder_text(object, placeholder->valuestring);
    const cJSON *font = json_field(spec, "font");
    if (cJSON_IsString(font)) {
        const lv_font_t *value = !strcmp(font->valuestring, "small") ? &lv_font_montserrat_16 :
            !strcmp(font->valuestring, "large") ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
        lv_obj_set_style_text_font(!strcmp(type, "button") && text ? text : object, value, 0);
    }

    int minimum = 0, maximum = 100, value = 0;
    if (!json_int(spec, "min", -32768, 32767, &minimum) || !json_int(spec, "max", -32768, 32767, &maximum) || minimum >= maximum ||
        !json_int(spec, "value", minimum, maximum, &value)) { lv_obj_del(object); return nullptr; }
    if (!strcmp(type, "slider")) { lv_slider_set_range(object, minimum, maximum); lv_slider_set_value(object, value, LV_ANIM_OFF); }
    else if (!strcmp(type, "arc")) { lv_arc_set_range(object, minimum, maximum); lv_arc_set_value(object, value); }
    else if (!strcmp(type, "switch") && value) lv_obj_add_state(object, LV_STATE_CHECKED);

    const cJSON *children = json_field(spec, "children");
    if (!strcmp(type, "tabview") && cJSON_IsArray(children)) {
        const cJSON *item = nullptr;
        cJSON_ArrayForEach(item, children) {
            const cJSON *title = json_field(item, "title");
            lv_obj_t *tab = cJSON_IsString(title) ? lv_tabview_add_tab(object, title->valuestring) : nullptr;
            if (!tab || !app->ui_.adopt(tab)) { lv_obj_del(object); return nullptr; }
        }
    }
    const cJSON *style = json_field(spec, "style");
    if (style && !apply_style(object, style)) { lv_obj_del(object); return nullptr; }
    if (!strcmp(type, "image")) {
        const cJSON *source = json_field(spec, "src");
        if (!cJSON_IsString(source) || !app->load_image(object, source->valuestring)) { lv_obj_del(object); return nullptr; }
    }
    return object;
}

bool ScriptApp::apply_style(lv_obj_t *object, const cJSON *style)
{
    if (!object || !cJSON_IsObject(style)) return false;
    for (const cJSON *item = style->child; item; item = item->next) {
        if (!item->string) return false;
        int value = 0;
        if (!strcmp(item->string, "bg_color") || !strcmp(item->string, "text_color") || !strcmp(item->string, "border_color")) {
            if (!finite_integer(item, 0, 0xFFFFFF, &value)) return false;
            if (!strcmp(item->string, "bg_color")) lv_obj_set_style_bg_color(object, lv_color_hex(value), 0);
            else if (!strcmp(item->string, "text_color")) lv_obj_set_style_text_color(object, lv_color_hex(value), 0);
            else lv_obj_set_style_border_color(object, lv_color_hex(value), 0);
        } else if (!strcmp(item->string, "opacity")) {
            if (!finite_integer(item, 0, 255, &value)) return false;
            lv_obj_set_style_opa(object, value, 0);
        } else if (!strcmp(item->string, "bg_opa")) {
            if (!finite_integer(item, 0, 255, &value)) return false;
            lv_obj_set_style_bg_opa(object, value, 0);
        } else if (!strcmp(item->string, "radius") || !strcmp(item->string, "padding") || !strcmp(item->string, "border_width")) {
            if (!finite_integer(item, 0, 255, &value)) return false;
            if (!strcmp(item->string, "radius")) lv_obj_set_style_radius(object, value, 0);
            else if (!strcmp(item->string, "padding")) lv_obj_set_style_pad_all(object, value, 0);
            else lv_obj_set_style_border_width(object, value, 0);
        } else if (!strcmp(item->string, "font_size")) {
            if (!finite_integer(item, 12, 48, &value)) return false;
            const lv_font_t *font = value <= 16 ? &lv_font_montserrat_16 : value <= 22 ? &lv_font_montserrat_20 :
                value <= 28 ? &lv_font_montserrat_28 : &lv_font_montserrat_48;
            lv_obj_set_style_text_font(object, font, 0);
        } else if (!strcmp(item->string, "align")) {
            if (!cJSON_IsString(item)) return false;
            const lv_text_align_t align = !strcmp(item->valuestring, "left") ? LV_TEXT_ALIGN_LEFT :
                !strcmp(item->valuestring, "right") ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER;
            lv_obj_set_style_text_align(object, align, 0);
        } else return false;
    }
    return true;
}

bool ScriptApp::load_image(lv_obj_t *object, const char *path)
{
    if (!object || !path || strncmp(path, "assets/", 7)) return false;
    for (const auto &image : images_) if (image->path == path) { lv_img_set_src(object, &image->descriptor); return true; }
    std::string bytes;
    if (!crystal_package_read_file(manifest_.id, path, &bytes, 256 * 1024)) return false;
    unsigned width = 0, height = 0; int consumed = 0;
    if (sscanf(bytes.c_str(), "CR16 %u %u%n", &width, &height, &consumed) != 2 || !width || !height || width > 480 || height > 480) return false;
    const size_t count = static_cast<size_t>(width) * height;
    if (count > 64 * 1024 / sizeof(lv_color_t)) return false;
    auto image = std::make_unique<Image>(); image->path = path; image->pixels.resize(count * sizeof(lv_color_t));
    const char *cursor = bytes.c_str() + consumed;
    for (size_t index = 0; index < count; ++index) {
        while (*cursor == ' ' || *cursor == '\n' || *cursor == '\r' || *cursor == '\t') ++cursor;
        char *end = nullptr; const unsigned long color = strtoul(cursor, &end, 16);
        if (end == cursor || color > 0xFFFFFF) return false;
        const lv_color_t pixel = lv_color_hex(static_cast<uint32_t>(color));
        memcpy(image->pixels.data() + index * sizeof(pixel), &pixel, sizeof(pixel)); cursor = end;
    }
    while (*cursor == ' ' || *cursor == '\n' || *cursor == '\r' || *cursor == '\t') ++cursor;
    if (*cursor) return false;
    image->descriptor.header.cf = LV_IMG_CF_TRUE_COLOR; image->descriptor.header.always_zero = 0; image->descriptor.header.reserved = 0;
    image->descriptor.header.w = width; image->descriptor.header.h = height;
    image->descriptor.data_size = image->pixels.size(); image->descriptor.data = image->pixels.data();
    auto *result = image.get(); images_.push_back(std::move(image)); lv_img_set_src(object, &result->descriptor); return true;
}

int ScriptApp::ui_screen(lua_State *state)
{
    auto *app = self(state); if (app) lua_pushinteger(state, app->ui_.screen()); else lua_pushnil(state); return 1;
}
int ScriptApp::ui_create(lua_State *state)
{
    auto *app = self(state); const char *type = lua_tostring(state, lua_upvalueindex(2));
    if (!app || !type) { lua_pushnil(state); return 1; }
    const bool dialog = !strcmp(type, "dialog"); const int options_index = dialog ? 1 : 2;
    uint32_t parent = app->ui_.screen();
    if (!dialog && !lua_uint(state, 1, 1, UINT32_MAX, &parent)) { lua_pushnil(state); return 1; }
    cJSON *options = nullptr;
    if (lua_istable(state, options_index)) options = app->lua_to_json(options_index);
    else if (!lua_isnoneornil(state, options_index)) { lua_pushnil(state); return 1; }
    const auto handle = app->ui_.create(type, parent, options); cJSON_Delete(options);
    if (!handle) { lua_pushnil(state); return 1; }
    if (lua_istable(state, options_index)) {
        const std::pair<const char *, const char *> callbacks[] = {{"on_click", "click"}, {"on_change", "change"}, {"on_focus", "focus"}, {"on_blur", "blur"}, {"on_select", "select"}};
        for (const auto &entry : callbacks) {
            lua_getfield(state, options_index, entry.first);
            if (lua_isfunction(state, -1)) {
                auto *callback = app->make_callback(-1);
                const bool ok = callback && (!strcmp(entry.second, "select")
                    ? (!strcmp(type, "list") && app->ui_.list_on_click(handle, ui_event, callback))
                    : app->ui_.on(handle, entry.second, ui_event, callback));
                if (ok) app->event_callbacks_[{handle, entry.second}] = callback;
            }
            lua_pop(state, 1);
        }
    }
    lua_pushinteger(state, handle); return 1;
}
int ScriptApp::ui_tab(lua_State *state)
{
    auto *app = self(state); uint32_t view = 0, index = 0;
    const auto result = app && lua_uint(state, 1, 1, UINT32_MAX, &view) && lua_uint(state, 2, 0, 7, &index) ? app->ui_.tab(view, index) : 0;
    if (result) lua_pushinteger(state, result); else lua_pushnil(state); return 1;
}
int ScriptApp::ui_focus(lua_State *state)
{
    auto *app = self(state); uint32_t handle = 0;
    const bool ok = app && lua_uint(state, 1, 1, UINT32_MAX, &handle) && app->ui_.set_focus(handle);
    lua_pushboolean(state, ok); return 1;
}
int ScriptApp::ui_delete(lua_State *state)
{
    auto *app = self(state); uint32_t handle = 0; const bool ok = app && lua_uint(state, 1, 1, UINT32_MAX, &handle) && app->ui_.remove(handle);
    lua_pushboolean(state, ok); return 1;
}
int ScriptApp::ui_set_text(lua_State *state)
{
    auto *app = self(state); uint32_t handle = 0; size_t size = 0; const char *text = lua_tolstring(state, 2, &size);
    const bool ok = app && lua_uint(state, 1, 1, UINT32_MAX, &handle) && text && size <= kMaxLuaString && app->ui_.set_text(handle, text);
    lua_pushboolean(state, ok); return 1;
}
int ScriptApp::ui_get_text(lua_State *state)
{
    auto *app = self(state); uint32_t handle = 0; std::string text;
    if (app && lua_uint(state, 1, 1, UINT32_MAX, &handle) && app->ui_.get_text(handle, &text)) lua_pushlstring(state, text.data(), text.size());
    else lua_pushnil(state);
    return 1;
}
int ScriptApp::ui_set_value(lua_State *state)
{
    auto *app = self(state); uint32_t handle = 0;
    const bool ok = app && lua_uint(state, 1, 1, UINT32_MAX, &handle) && lua_isinteger(state, 2) && app->ui_.set_value(handle, static_cast<int>(lua_tointeger(state, 2)));
    lua_pushboolean(state, ok); return 1;
}
int ScriptApp::ui_get_value(lua_State *state)
{
    auto *app = self(state); uint32_t handle = 0; int value = 0;
    if (app && lua_uint(state, 1, 1, UINT32_MAX, &handle) && app->ui_.get_value(handle, &value)) lua_pushinteger(state, value);
    else lua_pushnil(state);
    return 1;
}
int ScriptApp::ui_set_hidden(lua_State *state)
{
    auto *app = self(state); uint32_t handle = 0; const bool ok = app && lua_uint(state, 1, 1, UINT32_MAX, &handle) && lua_isboolean(state, 2) && app->ui_.set_hidden(handle, lua_toboolean(state, 2));
    lua_pushboolean(state, ok); return 1;
}
int ScriptApp::ui_set_enabled(lua_State *state)
{
    auto *app = self(state); uint32_t handle = 0; const bool ok = app && lua_uint(state, 1, 1, UINT32_MAX, &handle) && lua_isboolean(state, 2) && app->ui_.set_enabled(handle, lua_toboolean(state, 2));
    lua_pushboolean(state, ok); return 1;
}
int ScriptApp::ui_set_style(lua_State *state)
{
    auto *app = self(state); uint32_t handle = 0; cJSON *style = nullptr;
    if (app && lua_uint(state, 1, 1, UINT32_MAX, &handle) && lua_istable(state, 2)) style = app->lua_to_json(2);
    const bool ok = app && style && app->ui_.set_style(handle, style); cJSON_Delete(style); lua_pushboolean(state, ok); return 1;
}
int ScriptApp::ui_scroll_to(lua_State *state)
{
    auto *app = self(state); uint32_t handle = 0; bool animate = false; const char *align = "center";
    if (lua_istable(state, 2)) {
        lua_getfield(state, 2, "animate"); animate = lua_isboolean(state, -1) && lua_toboolean(state, -1); lua_pop(state, 1);
        lua_getfield(state, 2, "align"); if (lua_isstring(state, -1)) align = lua_tostring(state, -1); lua_pop(state, 1);
    }
    const bool ok = app && lua_uint(state, 1, 1, UINT32_MAX, &handle) && app->ui_.scroll_to(handle, animate, align);
    lua_pushboolean(state, ok); return 1;
}
int ScriptApp::ui_set_parent(lua_State *state)
{
    auto *app = self(state); uint32_t child = 0, parent = 0;
    const bool ok = app && lua_uint(state, 1, 1, UINT32_MAX, &child) && lua_uint(state, 2, 1, UINT32_MAX, &parent) && app->ui_.set_parent(child, parent);
    lua_pushboolean(state, ok); return 1;
}
int ScriptApp::ui_get_size(lua_State *state)
{
    auto *app = self(state); uint32_t handle = 0; CrystalUI::Size size{};
    if (!app || !lua_uint(state, 1, 1, UINT32_MAX, &handle) || !app->ui_.get_size(handle, &size)) { lua_pushnil(state); return 1; }
    lua_createtable(state, 0, 4);
    lua_pushinteger(state, size.w); lua_setfield(state, -2, "w"); lua_pushinteger(state, size.h); lua_setfield(state, -2, "h");
    lua_pushinteger(state, size.x); lua_setfield(state, -2, "x"); lua_pushinteger(state, size.y); lua_setfield(state, -2, "y"); return 1;
}
int ScriptApp::ui_on(lua_State *state)
{
    auto *app = self(state); uint32_t handle = 0; const char *event = lua_tostring(state, 2);
    if (!app || !lua_uint(state, 1, 1, UINT32_MAX, &handle) || !event || !lua_isfunction(state, 3)) { lua_pushboolean(state, false); return 1; }
    auto *callback = app->make_callback(3); const bool ok = callback && app->ui_.on(handle, event, ui_event, callback);
    if (ok) app->event_callbacks_[{handle, event}] = callback;
    lua_pushboolean(state, ok); return 1;
}
int ScriptApp::ui_off(lua_State *state)
{
    auto *app = self(state); uint32_t handle = 0; const char *event = lua_tostring(state, 2);
    const bool ok = app && lua_uint(state, 1, 1, UINT32_MAX, &handle) && event && app->ui_.off(handle, event);
    if (ok) app->event_callbacks_.erase({handle, event});
    lua_pushboolean(state, ok); return 1;
}
int ScriptApp::ui_list_add(lua_State *state)
{
    // Preserve the item argument and append at the actual list length.
    // UINT32_MAX wraps to -1 in the firmware's 32-bit Lua integer build.
    auto *app = self(state); uint32_t handle = 0; unsigned count = 0;
    if (!app || !lua_uint(state, 1, 1, UINT32_MAX, &handle) ||
        !app->ui_.list_count(handle, &count)) { lua_pushboolean(state, false); return 1; }
    lua_pushinteger(state, count); lua_insert(state, 2); return ui_list_insert(state);
}
int ScriptApp::ui_list_insert(lua_State *state)
{
    auto *app = self(state); uint32_t handle = 0, index = 0;
    if (!app || !lua_uint(state, 1, 1, UINT32_MAX, &handle) || !lua_uint(state, 2, 0, UINT32_MAX, &index)) { lua_pushboolean(state, false); return 1; }
    CrystalUI::ListItem item{}; std::string primary, secondary, icon;
    if (lua_isstring(state, 3)) primary = lua_tostring(state, 3);
    else if (lua_istable(state, 3)) {
        lua_getfield(state, 3, "primary"); if (lua_isstring(state, -1)) primary = lua_tostring(state, -1); lua_pop(state, 1);
        lua_getfield(state, 3, "secondary"); if (lua_isstring(state, -1)) secondary = lua_tostring(state, -1); lua_pop(state, 1);
        lua_getfield(state, 3, "icon"); if (lua_isstring(state, -1)) icon = lua_tostring(state, -1); lua_pop(state, 1);
    } else { lua_pushboolean(state, false); return 1; }
    if (primary.empty()) { lua_pushboolean(state, false); return 1; }
    item.primary = primary.c_str(); item.secondary = secondary.empty() ? nullptr : secondary.c_str(); item.icon = icon.empty() ? nullptr : icon.c_str();
    const bool ok = index == UINT32_MAX ? app->ui_.list_add(handle, item) : app->ui_.list_insert(handle, index, item);
    lua_pushboolean(state, ok); return 1;
}
int ScriptApp::ui_list_set_text(lua_State *state)
{
    auto *app = self(state); uint32_t handle = 0, index = 0; size_t length = 0;
    const char *text = lua_tolstring(state, 3, &length);
    const bool ok = app && lua_uint(state, 1, 1, UINT32_MAX, &handle) &&
        lua_uint(state, 2, 0, 255, &index) && text && length <= 96 && app->ui_.list_set_text(handle, index, text);
    lua_pushboolean(state, ok); return 1;
}
int ScriptApp::ui_list_remove(lua_State *state)
{
    auto *app = self(state); uint32_t handle = 0, index = 0; const bool ok = app && lua_uint(state, 1, 1, UINT32_MAX, &handle) && lua_uint(state, 2, 0, UINT32_MAX, &index) && app->ui_.list_remove(handle, index);
    lua_pushboolean(state, ok); return 1;
}
int ScriptApp::ui_list_clear(lua_State *state)
{
    auto *app = self(state); uint32_t handle = 0; const bool ok = app && lua_uint(state, 1, 1, UINT32_MAX, &handle) && app->ui_.list_clear(handle);
    lua_pushboolean(state, ok); return 1;
}
int ScriptApp::ui_list_count(lua_State *state)
{
    auto *app = self(state); uint32_t handle = 0; unsigned count = 0;
    if (app && lua_uint(state, 1, 1, UINT32_MAX, &handle) && app->ui_.list_count(handle, &count)) lua_pushinteger(state, count); else lua_pushnil(state); return 1;
}
int ScriptApp::ui_chart_add_series(lua_State *state)
{
    auto *app = self(state); uint32_t handle = 0, color = 0; const auto series = app && lua_uint(state, 1, 1, UINT32_MAX, &handle) && lua_uint(state, 2, 0, 0xFFFFFF, &color) ? app->ui_.chart_add_series(handle, color) : 0;
    if (series) lua_pushinteger(state, series); else lua_pushnil(state); return 1;
}
int ScriptApp::ui_chart_set_data(lua_State *state)
{
    auto *app = self(state); uint32_t chart = 0, series = 0; std::vector<int> values;
    if (app && lua_uint(state, 1, 1, UINT32_MAX, &chart) && lua_uint(state, 2, 1, UINT32_MAX, &series) && lua_istable(state, 3)) {
        const size_t count = lua_rawlen(state, 3);
        if (count && count <= 128) for (size_t i = 1; i <= count; ++i) {
            lua_rawgeti(state, 3, i); if (!lua_isinteger(state, -1)) { values.clear(); lua_pop(state, 1); break; }
            values.push_back(static_cast<int>(lua_tointeger(state, -1))); lua_pop(state, 1);
        }
    }
    const bool ok = app && !values.empty() && app->ui_.chart_set_data(chart, series, values); lua_pushboolean(state, ok); return 1;
}
int ScriptApp::ui_chart_set_range(lua_State *state)
{
    auto *app = self(state); uint32_t chart = 0; const bool ok = app && lua_uint(state, 1, 1, UINT32_MAX, &chart) && lua_isinteger(state, 2) && lua_isinteger(state, 3) && app->ui_.chart_set_range(chart, static_cast<int>(lua_tointeger(state, 2)), static_cast<int>(lua_tointeger(state, 3)));
    lua_pushboolean(state, ok); return 1;
}
int ScriptApp::ui_chart_clear(lua_State *state)
{
    auto *app = self(state); uint32_t chart = 0; const bool ok = app && lua_uint(state, 1, 1, UINT32_MAX, &chart) && app->ui_.chart_clear(chart); lua_pushboolean(state, ok); return 1;
}
int ScriptApp::ui_timer(lua_State *state)
{
    auto *app = self(state); uint32_t period = 0; if (!app || !lua_uint(state, 1, 1, 86400000, &period) || !lua_isfunction(state, 2)) { lua_pushnil(state); return 1; }
    auto *callback = app->make_callback(2); const auto id = callback ? app->ui_.timer(period, ui_timer, callback) : 0; if (id) lua_pushinteger(state, id); else lua_pushnil(state); return 1;
}
int ScriptApp::ui_after(lua_State *state)
{
    auto *app = self(state); uint32_t delay = 0; if (!app || !lua_uint(state, 1, 1, 86400000, &delay) || !lua_isfunction(state, 2)) { lua_pushnil(state); return 1; }
    auto *callback = app->make_callback(2); const auto id = callback ? app->ui_.after(delay, ui_timer, callback) : 0; if (id) lua_pushinteger(state, id); else lua_pushnil(state); return 1;
}
int ScriptApp::ui_timer_delete(lua_State *state)
{
    auto *app = self(state); uint32_t id = 0; const bool ok = app && lua_uint(state, 1, 1, UINT32_MAX, &id) && app->ui_.timer_delete(id); lua_pushboolean(state, ok); return 1;
}
int ScriptApp::ui_anim(lua_State *state)
{
    auto *app = self(state); uint32_t handle = 0, duration = 0, delay = 0; cJSON *props = nullptr; const char *easing = "ease_out";
    if (app && lua_uint(state, 1, 1, UINT32_MAX, &handle) && lua_istable(state, 2)) props = app->lua_to_json(2);
    if (lua_istable(state, 4)) {
        lua_getfield(state, 4, "easing"); if (lua_isstring(state, -1)) easing = lua_tostring(state, -1); lua_pop(state, 1);
        lua_getfield(state, 4, "delay_ms"); if (!lua_isnil(state, -1)) (void)lua_uint(state, -1, 0, 60000, &delay); lua_pop(state, 1);
    }
    CallbackRef *done = nullptr;
    if (app && lua_istable(state, 4)) { lua_getfield(state, 4, "on_done"); if (lua_isfunction(state, -1)) done = app->make_callback(-1); lua_pop(state, 1); }
    const auto id = app && props && lua_uint(state, 3, 1, 60000, &duration) ? app->ui_.anim(handle, props, duration, easing, delay, done ? ui_anim_done : nullptr, done) : 0;
    cJSON_Delete(props); if (id) lua_pushinteger(state, id); else lua_pushnil(state); return 1;
}
int ScriptApp::ui_anim_cancel(lua_State *state)
{
    auto *app = self(state); uint32_t id = 0; const bool ok = app && lua_uint(state, 1, 1, UINT32_MAX, &id) && app->ui_.anim_cancel(id); lua_pushboolean(state, ok); return 1;
}

cJSON *ScriptApp::lua_to_json(int index, unsigned depth, unsigned *node_count)
{
    unsigned local_count = 0;
    if (!node_count) node_count = &local_count;
    if (++*node_count > kMaxJsonNodes || depth > kMaxJsonDepth) return nullptr;
    index = lua_absindex(lua_, index);
    switch (lua_type(lua_, index)) {
    case LUA_TNIL: return cJSON_CreateNull();
    case LUA_TBOOLEAN: return cJSON_CreateBool(lua_toboolean(lua_, index));
    case LUA_TNUMBER: {
        // The embedded Lua build uses float32; timestamps must stay integers.
        const double value = lua_isinteger(lua_, index)
            ? static_cast<double>(lua_tointeger(lua_, index))
            : static_cast<double>(lua_tonumber(lua_, index));
        return std::isfinite(value) ? cJSON_CreateNumber(value) : nullptr;
    }
    case LUA_TSTRING: {
        size_t length = 0; const char *value = lua_tolstring(lua_, index, &length);
        if (!value || length > kMaxLuaString || memchr(value, '\0', length)) return nullptr;
        return cJSON_CreateString(value);
    }
    case LUA_TTABLE: {
        const size_t array_size = lua_rawlen(lua_, index);
        cJSON *result = array_size ? cJSON_CreateArray() : cJSON_CreateObject();
        if (!result || array_size > kMaxJsonNodes) { cJSON_Delete(result); return nullptr; }
        if (array_size) {
            for (size_t i = 1; i <= array_size; ++i) {
                lua_rawgeti(lua_, index, static_cast<lua_Integer>(i));
                cJSON *value = lua_to_json(-1, depth + 1, node_count); lua_pop(lua_, 1);
                if (!value) { cJSON_Delete(result); return nullptr; }
                cJSON_AddItemToArray(result, value);
            }
            lua_pushnil(lua_);
            while (lua_next(lua_, index) != 0) {
                const bool valid = lua_isinteger(lua_, -2) && lua_tointeger(lua_, -2) >= 1 &&
                    static_cast<uint64_t>(lua_tointeger(lua_, -2)) <= array_size;
                lua_pop(lua_, 1);
                if (!valid) { lua_pop(lua_, 1); cJSON_Delete(result); return nullptr; }
            }
        } else {
            lua_pushnil(lua_);
            while (lua_next(lua_, index) != 0) {
                if (!lua_isstring(lua_, -2)) { lua_pop(lua_, 2); cJSON_Delete(result); return nullptr; }
                size_t key_length = 0; const char *key = lua_tolstring(lua_, -2, &key_length);
                if (!key || !key_length || key_length > 63 || memchr(key, '\0', key_length)) { lua_pop(lua_, 2); cJSON_Delete(result); return nullptr; }
                // Callback fields belong to the host event layer, never JSON options.
                if (lua_isfunction(lua_, -1) &&
                    (!strncmp(key, "on_", 3) || !strcmp(key, "on_click") || !strcmp(key, "on_change"))) {
                    lua_pop(lua_, 1); continue;
                }
                cJSON *value = lua_to_json(-1, depth + 1, node_count);
                const std::string name(key, key_length);
                if (!value || !cJSON_AddItemToObject(result, name.c_str(), value)) {
                    cJSON_Delete(value); lua_pop(lua_, 2); cJSON_Delete(result); return nullptr;
                }
                lua_pop(lua_, 1);
            }
        }
        return result;
    }
    default: return nullptr;
    }
}

void ScriptApp::json_to_lua(const cJSON *value, unsigned depth)
{
    if (!value || depth > kMaxJsonDepth) { lua_pushnil(lua_); return; }
    if (cJSON_IsNull(value)) lua_pushnil(lua_);
    else if (cJSON_IsBool(value)) lua_pushboolean(lua_, cJSON_IsTrue(value));
    else if (cJSON_IsNumber(value)) {
        const double number = value->valuedouble;
        if (std::isfinite(number) && std::floor(number) == number &&
            number >= static_cast<double>(LUA_MININTEGER) &&
            number <= static_cast<double>(LUA_MAXINTEGER))
            lua_pushinteger(lua_, static_cast<lua_Integer>(number));
        else lua_pushnumber(lua_, number);
    }
    else if (cJSON_IsString(value)) lua_pushstring(lua_, value->valuestring ? value->valuestring : "");
    else if (cJSON_IsArray(value)) {
        const int count = cJSON_GetArraySize(value); lua_createtable(lua_, count, 0);
        int index = 1;
        for (const cJSON *item = value->child; item; item = item->next, ++index) { json_to_lua(item, depth + 1); lua_rawseti(lua_, -2, index); }
    } else if (cJSON_IsObject(value)) {
        lua_createtable(lua_, 0, 8);
        for (const cJSON *item = value->child; item; item = item->next) { json_to_lua(item, depth + 1); lua_setfield(lua_, -2, item->string ? item->string : ""); }
    } else lua_pushnil(lua_);
}

int ScriptApp::protected_json_value(lua_State *state)
{
    auto *app = static_cast<ScriptApp *>(lua_touserdata(state, 1));
    app->json_to_lua(static_cast<const cJSON *>(lua_touserdata(state, 2)));
    return 1;
}

int ScriptApp::push_json_value(const cJSON *value)
{
    // Callers reserve stack space before acquiring native JSON allocations.
    // Lua allocation errors must unwind to here so the caller can free cJSON.
    lua_pushcfunction(lua_, protected_json_value);
    lua_pushlightuserdata(lua_, this);
    lua_pushlightuserdata(lua_, const_cast<cJSON *>(value));
    return lua_pcall(lua_, 2, 1, 0);
}

bool ScriptApp::valid_data_path(const char *path, std::string *relative) const
{
    std::string value;
    if (!valid_relative(path, &value) || value.compare(0, 5, "data/") != 0 || value.size() <= 5) return false;
    *relative = value.substr(5);
    return true;
}

bool ScriptApp::read_data(const std::string &path, std::string *out, size_t limit) const
{
    if (!out || path.empty()) return false;
    const std::string absolute = package_path(manifest_.id, ("data/" + path).c_str());
    const int fd = open(absolute.c_str(), O_RDONLY);
    if (fd < 0) return false;
    struct stat st{};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || static_cast<uint64_t>(st.st_size) > limit) { ::close(fd); return false; }
    out->assign(static_cast<size_t>(st.st_size), '\0');
    size_t received = 0;
    while (received < out->size()) {
        const ssize_t amount = read(fd, &(*out)[received], out->size() - received);
        if (amount < 0 && errno == EINTR) continue;
        if (amount <= 0) break;
        received += static_cast<size_t>(amount);
    }
    ::close(fd);
    if (received != out->size()) { out->clear(); return false; }
    return true;
}

bool ScriptApp::data_size(size_t *bytes) const
{
    return tree_bytes(package_path(manifest_.id, "data"), bytes);
}

bool ScriptApp::write_data(const std::string &path, const std::string &bytes)
{
    if (bytes.size() > kMaxFileBytes) return false;
    const std::string relative = "data/" + path;
    const std::string absolute = package_path(manifest_.id, relative.c_str());
    if (!make_parent_dirs(absolute)) return false;
    size_t current = 0;
    if (!data_size(&current)) return false;
    struct stat old{};
    size_t old_size = 0;
    if (stat(absolute.c_str(), &old) == 0) {
        if (!S_ISREG(old.st_mode) || old.st_size < 0) return false;
        old_size = static_cast<size_t>(old.st_size);
    } else if (errno != ENOENT) return false;
    const size_t quota = static_cast<size_t>(manifest_.storage_kb) * 1024;
    if (old_size > current || bytes.size() > quota || current - old_size > quota - bytes.size()) return false;
    const std::string temporary = absolute + ".tmp";
    const int fd = open(temporary.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0) return false;
    const bool ok = write_all(fd, bytes.data(), bytes.size()) && fsync(fd) == 0;
    ::close(fd);
    if (!ok || rename(temporary.c_str(), absolute.c_str()) != 0) { unlink(temporary.c_str()); return false; }
    return true;
}

int ScriptApp::state_get(lua_State *state)
{
    auto *app = self(state); size_t length = 0; const char *key = lua_tolstring(state, 1, &length);
    if (!app || !key || !length || length > 63 || memchr(key, '\0', length)) { lua_pushnil(state); return 1; }
    luaL_checkstack(state, 4, "JSON conversion");
    int result = LUA_OK;
    {
        std::string text; cJSON *root = nullptr;
        if (app->read_data("state.json", &text, kMaxStateBytes)) root = cJSON_ParseWithLength(text.c_str(), text.size());
        const cJSON *value = root ? json_field(root, std::string(key, length).c_str()) : nullptr;
        if (value) result = app->push_json_value(value); else lua_pushnil(state);
        cJSON_Delete(root);
    }
    if (result != LUA_OK) return lua_error(state);
    return 1;
}
int ScriptApp::state_set(lua_State *state)
{
    auto *app = self(state); size_t length = 0; const char *key = lua_tolstring(state, 1, &length);
    if (!app || !key || !length || length > 63 || memchr(key, '\0', length)) { lua_pushboolean(state, false); return 1; }
    std::string text; cJSON *root = nullptr;
    if (app->read_data("state.json", &text, kMaxStateBytes)) root = cJSON_ParseWithLength(text.c_str(), text.size());
    if (!root) root = cJSON_CreateObject();
    if (!root || !cJSON_IsObject(root)) { cJSON_Delete(root); lua_pushboolean(state, false); return 1; }
    const std::string name(key, length);
    if (lua_isnil(state, 2)) cJSON_DeleteItemFromObjectCaseSensitive(root, name.c_str());
    else {
        cJSON *value = app->lua_to_json(2);
        if (!value) { cJSON_Delete(root); lua_pushboolean(state, false); return 1; }
        cJSON_DeleteItemFromObjectCaseSensitive(root, name.c_str());
        if (!cJSON_AddItemToObject(root, name.c_str(), value)) { cJSON_Delete(value); cJSON_Delete(root); lua_pushboolean(state, false); return 1; }
    }
    char *encoded = cJSON_PrintUnformatted(root);
    const bool ok = encoded && strlen(encoded) <= kMaxStateBytes && app->write_data("state.json", encoded);
    cJSON_free(encoded); cJSON_Delete(root); lua_pushboolean(state, ok); return 1;
}
int ScriptApp::state_erase(lua_State *state)
{
    lua_pushnil(state); lua_replace(state, 2); return state_set(state);
}
int ScriptApp::state_flush(lua_State *state) { (void)self(state); lua_pushboolean(state, true); return 1; }

int ScriptApp::json_encode(lua_State *state)
{
    auto *app = self(state); cJSON *value = app ? app->lua_to_json(1) : nullptr;
    char *encoded = value ? cJSON_PrintUnformatted(value) : nullptr;
    if (encoded && strlen(encoded) <= kMaxFileBytes) lua_pushstring(state, encoded); else lua_pushnil(state);
    cJSON_free(encoded); cJSON_Delete(value); return 1;
}
int ScriptApp::json_decode(lua_State *state)
{
    size_t length = 0; const char *text = lua_tolstring(state, 1, &length);
    if (!text || length > kMaxFileBytes || memchr(text, '\0', length)) { lua_pushnil(state); return 1; }
    auto *app = self(state);
    luaL_checkstack(state, 4, "JSON conversion");
    const size_t internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const char *parse_end = nullptr;
    cJSON *value = cJSON_ParseWithLengthOpts(text, length, &parse_end, false);
    if (value) {
        while (parse_end < text + length &&
               (*parse_end == ' ' || *parse_end == '\t' || *parse_end == '\r' || *parse_end == '\n')) ++parse_end;
        if (parse_end != text + length) { cJSON_Delete(value); value = nullptr; }
    }
    const size_t internal_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (app) {
        ++app->json_decode_count_;
        if (length > app->json_largest_input_bytes_)
            app->json_largest_input_bytes_ = static_cast<uint32_t>(length);
        const size_t internal_cost = internal_before > internal_after ? internal_before - internal_after : 0;
        const size_t psram_cost = psram_before > psram_after ? psram_before - psram_after : 0;
        if (internal_cost > app->json_peak_internal_bytes_) app->json_peak_internal_bytes_ = internal_cost;
        if (psram_cost > app->json_peak_psram_bytes_) app->json_peak_psram_bytes_ = psram_cost;
    }
    if (!value) { lua_pushnil(state); return 1; }
    int result = LUA_OK;
    if (app) result = app->push_json_value(value); else lua_pushnil(state);
    cJSON_Delete(value);
    if (result != LUA_OK) return lua_error(state);
    return 1;
}

int ScriptApp::fs_read(lua_State *state)
{
    auto *app = self(state); const char *path = lua_tostring(state, 1); std::string clean, bytes;
    if (!app || !valid_relative(path, &clean)) { lua_pushnil(state); return 1; }
    bool ok = false;
    if (clean.compare(0, 5, "data/") == 0) ok = app->read_data(clean.substr(5), &bytes, kMaxFileBytes);
    else if (clean.compare(0, 7, "assets/") == 0) ok = crystal_package_read_file(app->manifest_.id, clean, &bytes, kMaxFileBytes);
    if (ok) lua_pushlstring(state, bytes.data(), bytes.size()); else lua_pushnil(state); return 1;
}
int ScriptApp::fs_write(lua_State *state)
{
    auto *app = self(state); const char *path = lua_tostring(state, 1); size_t length = 0; const char *data = lua_tolstring(state, 2, &length);
    std::string relative;
    const bool ok = app && data && length <= kMaxFileBytes && app->valid_data_path(path, &relative) && app->write_data(relative, std::string(data, length));
    lua_pushboolean(state, ok); return 1;
}
int ScriptApp::fs_exists(lua_State *state)
{
    auto *app = self(state); const char *path = lua_tostring(state, 1); std::string clean;
    bool exists = false;
    if (app && valid_relative(path, &clean)) {
        struct stat st{};
        if (clean.compare(0, 5, "data/") == 0) exists = stat(package_path(app->manifest_.id, clean.c_str()).c_str(), &st) == 0 && S_ISREG(st.st_mode);
        else if (clean.compare(0, 7, "assets/") == 0) {
            std::string ignored;
            exists = crystal_package_read_file(app->manifest_.id, clean, &ignored, kMaxFileBytes);
        }
    }
    lua_pushboolean(state, exists); return 1;
}
int ScriptApp::fs_list(lua_State *state)
{
    auto *app = self(state); const char *directory = lua_tostring(state, 1); std::string clean;
    lua_newtable(state);
    if (!app || !directory || !valid_relative(directory, &clean) ||
        (clean != "data" && clean != "assets" && clean.compare(0, 5, "data/") && clean.compare(0, 7, "assets/"))) return 1;
    const std::string absolute = package_path(app->manifest_.id, clean.c_str());
    DIR *dir = opendir(absolute.c_str()); if (!dir) return 1;
    int index = 1;
    while (auto *entry = readdir(dir)) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") || index > 64) continue;
        lua_pushstring(state, entry->d_name); lua_rawseti(state, -2, index++);
    }
    closedir(dir); return 1;
}

int ScriptApp::service_time_now(lua_State *state)
{
    lua_pushinteger(state, static_cast<lua_Integer>(time(nullptr))); return 1;
}
int ScriptApp::service_time_format(lua_State *state)
{
    const char *format = lua_tostring(state, 1); if (!format || strlen(format) > 64) { lua_pushnil(state); return 1; }
    const time_t now = time(nullptr); struct tm local{};
    if (!localtime_r(&now, &local)) { lua_pushnil(state); return 1; }
    char output[128] = {};
    if (!strftime(output, sizeof(output), format, &local)) { lua_pushnil(state); return 1; }
    lua_pushstring(state, output); return 1;
}
int ScriptApp::service_time_zone(lua_State *state)
{
    const time_t now = time(nullptr); struct tm local{}; char output[24] = {};
    if (localtime_r(&now, &local)) (void)strftime(output, sizeof(output), "%Z", &local);
    lua_pushstring(state, output[0] ? output : "Local"); return 1;
}
int ScriptApp::service_timer_arm(lua_State *state)
{
    auto *app = self(state); uint32_t seconds = 0;
    const bool ok = app && app->manifest_.system_timer && lua_uint(state, 1, 1, 86400, &seconds) && crystal_timer_start(seconds);
    lua_pushboolean(state, ok); return 1;
}
int ScriptApp::service_timer_start(lua_State *state)
{
    auto *app = self(state); const auto current = crystal_timer_state();
    const bool ok = app && app->manifest_.system_timer && current.duration_seconds && crystal_timer_start(current.duration_seconds);
    lua_pushboolean(state, ok); return 1;
}
int ScriptApp::service_timer_pause(lua_State *state)
{
    auto *app = self(state); const bool ok = app && app->manifest_.system_timer && crystal_timer_pause(); lua_pushboolean(state, ok); return 1;
}
int ScriptApp::service_timer_resume(lua_State *state)
{
    auto *app = self(state); const bool ok = app && app->manifest_.system_timer && crystal_timer_resume(); lua_pushboolean(state, ok); return 1;
}
int ScriptApp::service_timer_reset(lua_State *state)
{
    auto *app = self(state); const bool ok = app && app->manifest_.system_timer;
    if (ok) crystal_timer_reset();
    lua_pushboolean(state, ok); return 1;
}
int ScriptApp::service_timer_status(lua_State *state)
{
    const CrystalTimerState current = crystal_timer_state();
    const char *status = current.running ? "running" : current.paused ? "paused" : current.duration_seconds ? "finished" : "idle";
    lua_createtable(state, 0, 5); lua_pushstring(state, status); lua_setfield(state, -2, "state");
    lua_pushinteger(state, current.remaining_seconds); lua_setfield(state, -2, "remaining_sec");
    lua_pushinteger(state, current.remaining_seconds); lua_setfield(state, -2, "remaining");
    lua_pushinteger(state, current.duration_seconds); lua_setfield(state, -2, "total_sec");
    lua_pushinteger(state, current.duration_seconds); lua_setfield(state, -2, "total"); return 1;
}
int ScriptApp::service_toast(lua_State *state)
{
    auto *app = self(state); size_t length = 0; const char *message = lua_tolstring(state, 1, &length);
    if (app && app->manifest_.notify && message && length <= 120) { char text[121]; memcpy(text, message, length); text[length] = 0; crystal_core_show_toast(text); }
    return 0;
}
int ScriptApp::service_chime(lua_State *state)
{
    auto *app = self(state);
    const bool ok = app && app->manifest_.audio;
    if (ok) crystal_hal_timer_alarm();
    lua_pushboolean(state, ok); return 1;
}
int ScriptApp::service_net_status(lua_State *state)
{
    auto *wifi = hal().wifi; const bool connected = crystal_network_has_ip();
    lua_createtable(state, 0, 3); lua_pushstring(state, connected ? "connected" : wifi && wifi->connected() ? "connecting" : "disconnected"); lua_setfield(state, -2, "state");
    lua_pushstring(state, connected && wifi && wifi->last_ssid() ? wifi->last_ssid() : ""); lua_setfield(state, -2, "ssid");
    char ip[20] = ""; IWifi::IpConfig config{};
    if (connected && wifi && wifi->ip_config(&config)) snprintf(ip, sizeof(ip), "%u.%u.%u.%u", unsigned(config.ip & 255), unsigned((config.ip >> 8) & 255), unsigned((config.ip >> 16) & 255), unsigned((config.ip >> 24) & 255));
    lua_pushstring(state, ip); lua_setfield(state, -2, "ip"); return 1;
}
int ScriptApp::service_location_get(lua_State *state)
{
    auto *app = self(state); double latitude = 0, longitude = 0; char city[32] = {};
    if (!app || !app->manifest_.location || !crystal_location_get(&latitude, &longitude, city, sizeof(city))) { lua_pushnil(state); return 1; }
    lua_createtable(state, 0, 3); lua_pushnumber(state, latitude); lua_setfield(state, -2, "lat"); lua_pushnumber(state, longitude); lua_setfield(state, -2, "long"); lua_pushstring(state, city); lua_setfield(state, -2, "city"); return 1;
}
int ScriptApp::service_system_info(lua_State *state)
{
    auto *app = self(state);
    lua_createtable(state, 0, 40);
    auto set = [state](const char *key, lua_Integer value) {
        lua_pushinteger(state, value); lua_setfield(state, -2, key);
    };
    lua_pushstring(state, hal().system_info ? hal().system_info->app_version() : "unknown"); lua_setfield(state, -2, "os_version");
    set("abi_version", 1);
    const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (app) {
        if (!app->native_internal_low_water_ || internal_free < app->native_internal_low_water_)
            app->native_internal_low_water_ = static_cast<uint32_t>(internal_free);
        if (!app->native_psram_low_water_ || psram_free < app->native_psram_low_water_)
            app->native_psram_low_water_ = static_cast<uint32_t>(psram_free);
    }
    set("free_internal_kb", static_cast<lua_Integer>(internal_free / 1024));
    set("largest_internal_kb", static_cast<lua_Integer>(internal_largest / 1024));
    set("free_psram_kb", static_cast<lua_Integer>(psram_free / 1024));
    set("largest_psram_kb", static_cast<lua_Integer>(psram_largest / 1024));
    set("native_internal_low_kb", app ? app->native_internal_low_water_ / 1024 : 0);
    set("native_psram_low_kb", app ? app->native_psram_low_water_ / 1024 : 0);
    set("native_internal_delta_kb", app ?
        (static_cast<lua_Integer>(app->native_internal_baseline_) - static_cast<lua_Integer>(internal_free)) / 1024 : 0);
    set("native_psram_delta_kb", app ?
        (static_cast<lua_Integer>(app->native_psram_baseline_) - static_cast<lua_Integer>(psram_free)) / 1024 : 0);
    set("lua_arena_kb", app ? static_cast<lua_Integer>(app->arena_size_ / 1024) : 0);
    set("lua_used_kb", app ? static_cast<lua_Integer>(app->arena_used_ / 1024) : 0);
    set("lua_peak_kb", app ? static_cast<lua_Integer>(app->arena_peak_ / 1024) : 0);
    size_t largest_arena_block = 0;
    if (app && app->arena_) {
        for (auto *block = static_cast<Block *>(app->arena_); block; block = block->next) {
            if (block->free && block->size > largest_arena_block) largest_arena_block = block->size;
        }
    }
    set("lua_largest_free_kb", static_cast<lua_Integer>(largest_arena_block / 1024));
    const CrystalCleanupReport resources = app ? app->resources().report() : CrystalCleanupReport{};
    set("app_tasks", static_cast<lua_Integer>(resources.tasks));
    set("app_timers", static_cast<lua_Integer>(resources.timers));
    set("app_queues", static_cast<lua_Integer>(resources.queues));
    set("app_allocations", static_cast<lua_Integer>(resources.allocations));
    set("app_objects", static_cast<lua_Integer>(resources.objects));
    set("app_tracked_bytes", static_cast<lua_Integer>(resources.heap_bytes));
    set("app_callbacks", app ? static_cast<lua_Integer>(app->callbacks_.size()) : 0);
    set("app_http_callbacks", app ? static_cast<lua_Integer>(app->request_callbacks_.size()) : 0);
    size_t app_storage_bytes = 0;
    if (app) (void)app->data_size(&app_storage_bytes);
    set("app_storage_bytes", static_cast<lua_Integer>(app_storage_bytes));
    set("json_peak_internal_bytes", app ? static_cast<lua_Integer>(app->json_peak_internal_bytes_) : 0);
    set("json_peak_psram_bytes", app ? static_cast<lua_Integer>(app->json_peak_psram_bytes_) : 0);
    set("json_decode_count", app ? app->json_decode_count_ : 0);
    set("json_largest_input_bytes", app ? app->json_largest_input_bytes_ : 0);
    crystal_http_stats_t http{};
    if (crystal_http_get_stats(&http)) {
        set("http_peak_body_bytes", http.peak_body_bytes);
        set("http_min_tls_internal_bytes", http.min_tls_internal_free);
        set("http_min_tls_largest_bytes", http.min_tls_largest_internal);
        set("http_min_tls_psram_bytes", http.min_tls_psram_free);
        set("http_completed", http.completed);
        set("http_cancellations", http.cancellations);
    } else {
        set("http_peak_body_bytes", 0); set("http_min_tls_internal_bytes", 0);
        set("http_min_tls_largest_bytes", 0); set("http_min_tls_psram_bytes", 0);
        set("http_completed", 0); set("http_cancellations", 0);
    }
    uint32_t storage_used = 0, storage_total = 0;
    if (hal().system_info) (void)hal().system_info->storage_bytes(&storage_used, &storage_total);
    set("free_storage_kb", storage_total > storage_used ? (storage_total - storage_used) / 1024 : 0);
    return 1;
}

int ScriptApp::http_fetch(lua_State *state)
{
    auto *app = self(state);
    if (!app || !lua_isstring(state, 1) || !lua_isfunction(state, 3)) {
        lua_pushnil(state); lua_pushstring(state, "expected url, options, callback"); return 2;
    }
    size_t url_length = 0;
    const char *url = lua_tolstring(state, 1, &url_length);
    if (!url || url_length == 0 || url_length >= 256 || memchr(url, '\0', url_length) ||
        !crystal_script_host_allowed(std::string(url, url_length), app->manifest_.network_hosts)) {
        lua_pushnil(state); lua_pushstring(state, "network host is not allowed"); return 2;
    }
    if (!app->manifest_.worker_threads || app->active_requests_.size() >= app->manifest_.worker_threads) {
        lua_pushnil(state); lua_pushstring(state, "worker quota exceeded"); return 2;
    }
    if (!crystal_http_is_ready()) {
        lua_pushnil(state); lua_pushstring(state, "HTTP service unavailable"); return 2;
    }

    std::string method = "GET";
    std::string body;
    std::vector<std::string> header_names;
    std::vector<std::string> header_values;
    uint32_t timeout_ms = 15000;
    if (!lua_isnoneornil(state, 2)) {
        if (!lua_istable(state, 2)) { lua_pushnil(state); lua_pushstring(state, "options must be a table"); return 2; }
        lua_getfield(state, 2, "method");
        if (!lua_isnil(state, -1)) {
            if (!lua_isstring(state, -1)) { lua_pop(state, 1); lua_pushnil(state); lua_pushstring(state, "invalid method"); return 2; }
            method = lua_tostring(state, -1); std::transform(method.begin(), method.end(), method.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        }
        lua_pop(state, 1);
        if (method != "GET" && method != "POST") { lua_pushnil(state); lua_pushstring(state, "method must be GET or POST"); return 2; }
        lua_getfield(state, 2, "timeout_sec");
        if (!lua_isnil(state, -1)) {
            uint32_t seconds = 0;
            if (!lua_uint(state, -1, 1, 60, &seconds)) { lua_pop(state, 1); lua_pushnil(state); lua_pushstring(state, "timeout_sec must be 1..60"); return 2; }
            timeout_ms = seconds * 1000;
        }
        lua_pop(state, 1);
        lua_getfield(state, 2, "body");
        if (!lua_isnil(state, -1)) {
            size_t length = 0; const char *value = lua_tolstring(state, -1, &length);
            if (!value || length > 2048 || memchr(value, '\0', length)) { lua_pop(state, 1); lua_pushnil(state); lua_pushstring(state, "body exceeds 2048 bytes or is not text"); return 2; }
            body.assign(value, length);
        }
        lua_pop(state, 1);
        if (method == "GET" && !body.empty()) { lua_pushnil(state); lua_pushstring(state, "GET does not accept a body"); return 2; }
        lua_getfield(state, 2, "headers");
        if (!lua_isnil(state, -1)) {
            if (!lua_istable(state, -1)) { lua_pop(state, 1); lua_pushnil(state); lua_pushstring(state, "headers must be a table"); return 2; }
            lua_pushnil(state);
            while (lua_next(state, -2) != 0) {
                size_t name_length = 0, value_length = 0;
                const char *name = lua_tolstring(state, -2, &name_length);
                const char *value = lua_tolstring(state, -1, &value_length);
                if (!name || !value || !name_length || name_length > 63 || value_length > 255 ||
                    memchr(name, '\0', name_length) || memchr(value, '\0', value_length) ||
                    header_names.size() >= 8) {
                    lua_pop(state, 3); lua_pushnil(state); lua_pushstring(state, "invalid or excessive headers"); return 2;
                }
                header_names.emplace_back(name, name_length);
                header_values.emplace_back(value, value_length);
                lua_pop(state, 1);
            }
        }
        lua_pop(state, 1);
    }

    std::vector<crystal_http_header_t> headers;
    headers.reserve(header_names.size());
    for (size_t i = 0; i < header_names.size(); ++i) headers.push_back({header_names[i].c_str(), header_values[i].c_str()});
    crystal_http_options_t options{};
    std::string url_copy(url, url_length);
    options.url = url_copy.c_str();
    options.timeout_ms = timeout_ms;
    options.max_attempts = 1;
    options.max_body_bytes = kMaxResponseBytes;
    options.keep_alive = true;
    options.disable_auto_redirect = true;
    options.owner_id = app->owner_id_;
    options.method = method.c_str();
    options.body = body.data();
    options.body_len = body.size();
    options.headers = headers.data();
    options.header_count = headers.size();

    if (!app->http_timer_) {
        app->http_timer_ = lv_timer_create(http_dispatch, kHttpDispatchMs, app);
        if (!app->http_timer_) { lua_pushnil(state); lua_pushstring(state, "could not create dispatch timer"); return 2; }
        app->http_timer_resource_ = app->resources().track(CrystalResourceKind::Timer, app->http_timer_);
        if (!app->http_timer_resource_) { lv_timer_del(app->http_timer_); app->http_timer_ = nullptr; lua_pushnil(state); lua_pushstring(state, "timer resource quota exceeded"); return 2; }
    }
    lua_pushvalue(state, 3);
    const int callback_ref = luaL_ref(state, LUA_REGISTRYINDEX);
    auto *context = new (std::nothrow) HttpContext{app->async_};
    if (!context) { luaL_unref(state, LUA_REGISTRYINDEX, callback_ref); lua_pushnil(state); lua_pushstring(state, "out of memory"); return 2; }
    const uint32_t request_id = crystal_http_get(&options, http_callback, context);
    if (!request_id) {
        delete context;
        luaL_unref(state, LUA_REGISTRYINDEX, callback_ref);
        lua_pushnil(state); lua_pushstring(state, "HTTP queue is full"); return 2;
    }
    app->request_callbacks_[request_id] = callback_ref;
    app->active_requests_.insert(request_id);
    lv_timer_resume(app->http_timer_);
    lua_pushinteger(state, request_id);
    return 1;
}

int ScriptApp::http_cancel(lua_State *state)
{
    auto *app = self(state); uint32_t id = 0;
    bool ok = false;
    if (app && lua_uint(state, 1, 1, UINT32_MAX, &id) &&
        app->active_requests_.find(id) != app->active_requests_.end()) {
        ok = crystal_http_cancel(id);
        if (ok) {
            auto callback = app->request_callbacks_.find(id);
            if (callback != app->request_callbacks_.end()) {
                if (app->lua_ && callback->second != LUA_NOREF)
                    luaL_unref(app->lua_, LUA_REGISTRYINDEX, callback->second);
                app->request_callbacks_.erase(callback);
            }
        }
    }
    lua_pushboolean(state, ok); return 1;
}

void ScriptApp::http_callback(const crystal_http_response_t *response, void *context)
{
    std::unique_ptr<HttpContext> holder(static_cast<HttpContext *>(context));
    if (!response || !holder || !holder->state) { crystal_http_response_release(response); return; }
    HttpResult result{};
    result.id = response->request_id;
    result.status = response->transport_error == ESP_OK ? response->status_code : -1;
    if (response->body && response->body_len <= kMaxResponseBytes)
        result.body.assign(reinterpret_cast<const char *>(response->body), response->body_len);
    {
        std::lock_guard<std::mutex> lock(holder->state->mutex);
        if (holder->state->active && holder->state->completed.size() < 8)
            holder->state->completed.push_back(std::move(result));
    }
    crystal_http_response_release(response);
}

void ScriptApp::http_dispatch(lv_timer_t *timer)
{
    auto *app = static_cast<ScriptApp *>(timer->user_data);
    if (!app || !app->lua_ || app->in_destroy_ || app->runtime_failed_) return;
    HttpResult result{};
    bool has_result = false;
    {
        std::lock_guard<std::mutex> lock(app->async_->mutex);
        if (!app->async_->completed.empty()) {
            result = std::move(app->async_->completed.front());
            app->async_->completed.erase(app->async_->completed.begin());
            has_result = true;
        }
    }
    if (has_result) {
        app->active_requests_.erase(result.id);
        auto found = app->request_callbacks_.find(result.id);
        if (found != app->request_callbacks_.end()) {
            const int ref = found->second;
            app->request_callbacks_.erase(found);
            Invocation call{app}; call.ref = ref; call.response = &result;
            const bool ok = app->invoke(call);
            luaL_unref(app->lua_, LUA_REGISTRYINDEX, ref);
            if (!ok) return;
        }
    }
    if (app->active_requests_.empty()) lv_timer_pause(timer);
}

void ScriptApp::cancel_requests()
{
    (void)crystal_http_cancel_owner(owner_id_);
    for (const auto &entry : request_callbacks_)
        if (lua_ && entry.second != LUA_NOREF) luaL_unref(lua_, LUA_REGISTRYINDEX, entry.second);
    request_callbacks_.clear();
    active_requests_.clear();
    if (async_) {
        std::lock_guard<std::mutex> lock(async_->mutex);
        async_->completed.clear();
    }
    if (http_timer_) {
        lv_timer_pause(http_timer_);
    }
}
