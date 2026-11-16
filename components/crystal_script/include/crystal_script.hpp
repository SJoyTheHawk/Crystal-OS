/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "crystal_app.hpp"
#include "crystal_package.hpp"
#include "crystal_ui.hpp"
#include "crystal_http.h"
#include "lvgl.h"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include "cJSON.h"

// Package-backed Lua 5.4 runtime. Lua sees only integer UI handles and the
// crystal.* tables; LVGL objects and package paths stay inside this class.
class ScriptApp final : public CrystalApp {
public:
    explicit ScriptApp(const CrystalPackageManifest &manifest);
    ~ScriptApp() override;

protected:
    bool onCreate() override;
    bool onPause() override;
    bool onResume() override;
    bool onDestroy() override;
    bool onBack() override;

private:
    struct Block;
    struct CallbackRef { ScriptApp *app = nullptr; int ref = LUA_NOREF; };
    struct HttpResult { uint32_t id = 0; int status = 0; std::string body; };
    struct Invocation {
        ScriptApp *app;
        const char *hook = nullptr;
        int ref = LUA_NOREF;
        CrystalUI::Handle handle = 0;
        const char *message = nullptr;
        const HttpResult *response = nullptr;
        bool required = false;
    };
    static int protected_invoke(lua_State *state);
    bool invoke(Invocation &invocation);
    struct AsyncState {
        std::mutex mutex;
        bool active = true;
        std::vector<HttpResult> completed;
    };
    struct HttpContext { std::shared_ptr<AsyncState> state; };
    struct Image { std::string path; lv_img_dsc_t descriptor{}; std::vector<uint8_t> pixels; };

    static void *lua_allocate(void *context, void *pointer, size_t old_size, size_t new_size);
    void *arena_allocate(size_t bytes);
    void arena_release(void *pointer);
    static void instruction_hook(lua_State *state, lua_Debug *debug);
    static int coroutine_create(lua_State *state);
    static int coroutine_wrap(lua_State *state);
    static int coroutine_wrap_resume(lua_State *state);
    bool execute(const char *hook, bool required = false);
    void runtime_error(const char *message);
    void install_sandbox();
    void install_api();
    void push_module(const char *name, const luaL_Reg *functions);
    static ScriptApp *self(lua_State *state);
    static int print_binding(lua_State *state);
    static int ui_screen(lua_State *state);
    static int ui_create(lua_State *state);
    static int ui_tab(lua_State *state);
    static int ui_focus(lua_State *state);
    static int ui_delete(lua_State *state);
    static int ui_set_text(lua_State *state);
    static int ui_get_text(lua_State *state);
    static int ui_set_value(lua_State *state);
    static int ui_get_value(lua_State *state);
    static int ui_set_hidden(lua_State *state);
    static int ui_set_enabled(lua_State *state);
    static int ui_set_style(lua_State *state);
    static int ui_scroll_to(lua_State *state);
    static int ui_set_parent(lua_State *state);
    static int ui_get_size(lua_State *state);
    static int ui_on(lua_State *state);
    static int ui_off(lua_State *state);
    static int ui_list_add(lua_State *state);
    static int ui_list_insert(lua_State *state);
    static int ui_list_set_text(lua_State *state);
    static int ui_list_remove(lua_State *state);
    static int ui_list_clear(lua_State *state);
    static int ui_list_count(lua_State *state);
    static int ui_chart_add_series(lua_State *state);
    static int ui_chart_set_data(lua_State *state);
    static int ui_chart_set_range(lua_State *state);
    static int ui_chart_clear(lua_State *state);
    static int ui_timer(lua_State *state);
    static int ui_after(lua_State *state);
    static int ui_timer_delete(lua_State *state);
    static int ui_anim(lua_State *state);
    static int ui_anim_cancel(lua_State *state);
    static int state_get(lua_State *state);
    static int state_set(lua_State *state);
    static int state_erase(lua_State *state);
    static int state_flush(lua_State *state);
    static int json_encode(lua_State *state);
    static int json_decode(lua_State *state);
    static int fs_read(lua_State *state);
    static int fs_write(lua_State *state);
    static int fs_exists(lua_State *state);
    static int fs_list(lua_State *state);
    static int service_time_now(lua_State *state);
    static int service_time_format(lua_State *state);
    static int service_time_zone(lua_State *state);
    static int service_timer_arm(lua_State *state);
    static int service_timer_start(lua_State *state);
    static int service_timer_pause(lua_State *state);
    static int service_timer_resume(lua_State *state);
    static int service_timer_reset(lua_State *state);
    static int service_timer_status(lua_State *state);
    static int service_toast(lua_State *state);
    static int service_chime(lua_State *state);
    static int service_net_status(lua_State *state);
    static int service_location_get(lua_State *state);
    static int service_system_info(lua_State *state);
    static int http_fetch(lua_State *state);
    static int http_cancel(lua_State *state);
    static void http_dispatch(lv_timer_t *timer);
    static void http_callback(const crystal_http_response_t *response, void *context);
    static void runtime_close_dispatch(lv_timer_t *timer);

    static void ui_event(CrystalUI::Handle handle, void *context);
    static void ui_timer(CrystalUI::Handle handle, void *context);
    static void ui_anim_done(CrystalUI::Handle handle, void *context);
    bool call_ref(int ref, CrystalUI::Handle handle, const char *message = nullptr);
    CallbackRef *make_callback(int index);
    void release_callbacks();
    cJSON *lua_to_json(int index, unsigned depth = 0, unsigned *nodes = nullptr);
    void json_to_lua(const cJSON *value, unsigned depth = 0);
    static int protected_json_value(lua_State *state);
    int push_json_value(const cJSON *value);
    bool read_data(const std::string &path, std::string *out, size_t limit) const;
    bool write_data(const std::string &path, const std::string &bytes);
    bool data_size(size_t *bytes) const;
    bool valid_data_path(const char *path, std::string *relative) const;
    static lv_obj_t *build_widget(const cJSON *spec, lv_obj_t *parent, void *context);
    static bool apply_style(lv_obj_t *object, const cJSON *style);
    bool load_image(lv_obj_t *object, const char *path);
    void cancel_requests();
    void schedule_runtime_close();
    uint32_t owner_id_ = 0;
    CrystalPackageManifest manifest_;
    CrystalUI ui_{resources()};
    lua_State *lua_ = nullptr;
    void *arena_ = nullptr;
    size_t arena_size_ = 0;
    size_t arena_used_ = 0;
    size_t arena_peak_ = 0;
    size_t json_peak_internal_bytes_ = 0;
    size_t json_peak_psram_bytes_ = 0;
    uint32_t native_internal_baseline_ = 0;
    uint32_t native_psram_baseline_ = 0;
    uint32_t native_internal_low_water_ = 0;
    uint32_t native_psram_low_water_ = 0;
    uint32_t json_decode_count_ = 0;
    uint32_t json_largest_input_bytes_ = 0;
    int hooks_ref_ = LUA_NOREF;
    int64_t deadline_us_ = 0;
    bool deadline_exceeded_ = false;
    bool runtime_failed_ = false;
    bool in_destroy_ = false;
    lv_obj_t *root_ = nullptr;
    lv_timer_t *http_timer_ = nullptr;
    CrystalResources::Handle http_timer_resource_ = 0;
    lv_timer_t *runtime_close_timer_ = nullptr;
    CrystalResources::Handle runtime_close_resource_ = 0;
    bool runtime_close_scheduled_ = false;
    std::shared_ptr<AsyncState> async_;
    std::map<uint32_t, int> request_callbacks_;
    std::set<uint32_t> active_requests_;
    std::list<std::unique_ptr<CallbackRef>> callbacks_;
    std::map<std::pair<uint32_t, std::string>, CallbackRef *> event_callbacks_;
    std::vector<std::unique_ptr<Image>> images_;
};
