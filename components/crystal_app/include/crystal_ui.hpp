/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

#include <array>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include "crystal_resources.hpp"
#include "lvgl.h"

struct cJSON;

// Shared UI facade. All calls run on the LVGL task. Zero/false means failure;
// output arguments are written only on success. Handles are process-unique,
// app-owned and never reused, including after a close/reopen.
class CrystalUI final {
public:
    using Handle = uint32_t;
    using Callback = void (*)(Handle, void *);
    using Builder = lv_obj_t *(*)(const cJSON *, lv_obj_t *, void *);
    using Style = bool (*)(lv_obj_t *, const cJSON *);
    using ImageLoader = bool (*)(lv_obj_t *, const char *, void *);
    using Focus = bool (*)(lv_obj_t *, lv_obj_t *);
    using Blur = void (*)(lv_obj_t *);
    struct Size { int w, h, x, y; };
    struct ListItem { const char *primary; const char *secondary = nullptr; const char *icon = nullptr; Callback on_click = nullptr; void *user = nullptr; };

    explicit CrystalUI(CrystalResources &resources) : resources_(resources) {}
    ~CrystalUI();
    CrystalUI(const CrystalUI &) = delete;
    CrystalUI &operator=(const CrystalUI &) = delete;
    void configure(Builder builder, Style style, ImageLoader image, void *context);
    static void set_focus_hooks(Focus focus, Blur blur);
    Handle adopt(lv_obj_t *object, lv_obj_t *text = nullptr);
    Handle handle(lv_obj_t *object) const;
    void set_root(lv_obj_t *root) { root_ = root; }
    Handle screen() const { return handle(root_); }
    Handle create(const char *type, Handle parent, const cJSON *options = nullptr);
    Handle tab(Handle view, unsigned index) const;
    bool remove(Handle object);
    bool set_text(Handle object, const char *text);
    bool get_text(Handle object, std::string *text) const;
    bool set_value(Handle object, int value);
    bool get_value(Handle object, int *value) const;
    bool set_hidden(Handle object, bool hidden);
    bool set_enabled(Handle object, bool enabled);
    bool set_style(Handle object, const cJSON *style);
    bool set_focus(Handle object);
    bool scroll_to(Handle object, bool animate = false, const char *align = "center");
    bool set_parent(Handle object, Handle parent);
    bool get_size(Handle object, Size *size) const;
    bool on(Handle object, const char *event, Callback callback, void *user = nullptr);
    bool off(Handle object, const char *event);
    bool list_add(Handle list, const ListItem &item);
    bool list_insert(Handle list, unsigned index, const ListItem &item);
    bool list_remove(Handle list, unsigned index);
    bool list_clear(Handle list);
    bool list_count(Handle list, unsigned *count) const;
    bool list_set_item_height(Handle list, unsigned height);
    Handle chart_add_series(Handle chart, uint32_t color);
    Handle chart_series(Handle chart, unsigned index) const;
    bool chart_set_data(Handle chart, Handle series, const std::vector<int> &values);
    bool chart_set_range(Handle chart, int minimum, int maximum);
    bool chart_clear(Handle chart);
    Handle timer(uint32_t period_ms, Callback callback, void *user = nullptr, bool once = false);
    Handle after(uint32_t delay_ms, Callback callback, void *user = nullptr) { return timer(delay_ms, callback, user, true); }
    bool timer_delete(Handle timer);
    Handle anim(Handle object, const cJSON *properties, uint32_t duration_ms,
                const char *easing = "ease_out", uint32_t delay_ms = 0,
                Callback done = nullptr, void *user = nullptr);
    bool anim_cancel(Handle animation);
    void pause();
    void resume();
    void clear_callbacks();
    // Called before deleting the root, then again to discard dead metadata.
    void reset();

private:
    // Small host metadata must not consume the ESP32's scarce internal heap.
    struct Metadata {
        static void *operator new(size_t bytes, const std::nothrow_t &) noexcept;
        static void operator delete(void *memory) noexcept;
        static void operator delete(void *memory, const std::nothrow_t &) noexcept;
    };
    struct Listener { Callback callback = nullptr; void *user = nullptr; };
    struct Node : Metadata {
        CrystalUI *owner = nullptr;
        Handle id = 0;
        lv_obj_t *object = nullptr, *text = nullptr;
        bool chart_auto_range = true;
        unsigned item_height = 0;
        std::array<Listener, 7> events{};
    };
    struct Series { Handle id; Handle chart; lv_chart_series_t *series; };
    struct Timer : Metadata { CrystalUI *owner; Handle id; lv_timer_t *timer; CrystalResources::Handle resource; Callback callback; void *user; bool once; };
    struct Animation : Metadata {
        CrystalUI *owner; Handle id, object; uint32_t duration, delay;
        lv_anim_path_cb_t path; Callback done; void *user;
        std::vector<std::pair<std::string, int32_t>> from, to;
    };
    Node *node(Handle handle) const;
    static void event_cb(lv_event_t *event);
    static void timer_cb(lv_timer_t *timer);
    static void anim_step(void *context, int32_t progress);
    static void anim_done(lv_anim_t *animation);
    bool valid_type(Handle handle, const lv_obj_class_t *type) const;
    static int event_index(const char *event);
    void invalidate(lv_obj_t *object);
    CrystalResources &resources_;
    lv_obj_t *root_ = nullptr;
    Builder builder_ = nullptr;
    Style style_ = nullptr;
    ImageLoader image_ = nullptr;
    void *context_ = nullptr;
    std::vector<std::unique_ptr<Node>> nodes_;
    std::vector<Series> series_;
    std::vector<std::unique_ptr<Timer>> timers_;
    std::vector<std::unique_ptr<Animation>> animations_;
    bool paused_ = false, resetting_ = false;
};
