/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "crystal_ui.hpp"
#include "cJSON.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdlib>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

namespace {
std::atomic<uint32_t> next_handle{1};
CrystalUI::Focus focus_hook = nullptr;
CrystalUI::Blur blur_hook = nullptr;
constexpr lv_event_code_t events[] = {LV_EVENT_CLICKED, LV_EVENT_LONG_PRESSED,
    LV_EVENT_VALUE_CHANGED, LV_EVENT_FOCUSED, LV_EVENT_DEFOCUSED, LV_EVENT_SCROLL, LV_EVENT_DELETE};
uint32_t allocate_handle() {
    uint32_t value = next_handle.load();
    do { if (value == UINT32_MAX) return 0; }
    while (!next_handle.compare_exchange_weak(value, value + 1));
    return value;
}
bool bounded_text(const char *text, size_t bound) { return text && strnlen(text, bound + 1) <= bound; }
bool descendant(lv_obj_t *object, lv_obj_t *parent) {
    for (; object; object = lv_obj_get_parent(object)) if (object == parent) return true;
    return false;
}
lv_obj_t *owned_tree(lv_obj_t *object) {
    return lv_obj_check_type(object, &lv_msgbox_class) && lv_obj_get_parent(object) &&
        lv_obj_check_type(lv_obj_get_parent(object), &lv_msgbox_backdrop_class)
        ? lv_obj_get_parent(object) : object;
}
}

CrystalUI::~CrystalUI() { reset(); }
void *CrystalUI::Metadata::operator new(size_t bytes, const std::nothrow_t &) noexcept {
#ifdef ESP_PLATFORM
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return std::malloc(bytes);
#endif
}
void CrystalUI::Metadata::operator delete(void *memory) noexcept { std::free(memory); }
void CrystalUI::Metadata::operator delete(void *memory, const std::nothrow_t &) noexcept { std::free(memory); }
void CrystalUI::configure(Builder builder, Style style, ImageLoader image, void *context) {
    builder_ = builder; style_ = style; image_ = image; context_ = context;
}
void CrystalUI::set_focus_hooks(Focus focus, Blur blur) { focus_hook = focus; blur_hook = blur; }
CrystalUI::Node *CrystalUI::node(Handle id) const {
    if (!id) return nullptr;
    for (const auto &row : nodes_) if (row->id == id && row->object) return row.get();
    return nullptr;
}
CrystalUI::Handle CrystalUI::handle(lv_obj_t *object) const {
    if (object) for (const auto &row : nodes_) if (row->object == object) return row->id;
    return 0;
}
bool CrystalUI::valid_type(Handle id, const lv_obj_class_t *type) const {
    auto *row = node(id); return row && lv_obj_check_type(row->object, type);
}
CrystalUI::Handle CrystalUI::adopt(lv_obj_t *object, lv_obj_t *text) {
    if (!object || resetting_) return 0;
    if (auto id = handle(object)) { if (text) node(id)->text = text; return id; }
    size_t live = 0; for (const auto &row : nodes_) if (row->object) ++live;
    if (live >= 192) return 0;
    auto item = std::unique_ptr<Node>(new (std::nothrow) Node{});
    if (!item) return 0;
    item->owner = this; item->id = allocate_handle(); item->object = object; item->text = text;
    if (!item->id) return 0;
    auto *raw = item.get();
    auto slot = std::find_if(nodes_.begin(), nodes_.end(), [](const auto &row) { return !row->object; });
    if (slot == nodes_.end()) nodes_.push_back(std::move(item)); else *slot = std::move(item);
    lv_obj_add_event_cb(object, event_cb, LV_EVENT_ALL, raw);
    return raw->id;
}
CrystalUI::Handle CrystalUI::create(const char *type, Handle parent, const cJSON *options) {
    auto *row = node(parent);
    if (!row || !type || !builder_ || resetting_ || (options && !cJSON_IsObject(options))) return 0;
    if (!lv_obj_check_type(row->object, &lv_obj_class)) return 0;
    // Factory options are the same bounded JSON object used by ui.json.
    cJSON *spec = options ? cJSON_Duplicate(options, true) : cJSON_CreateObject();
    if (!spec) return 0;
    cJSON_DeleteItemFromObjectCaseSensitive(spec, "type");
    if (!cJSON_AddStringToObject(spec, "type", type)) { cJSON_Delete(spec); return 0; }
    if (!strcmp(type, "tabview")) {
        const auto *labels = cJSON_GetObjectItemCaseSensitive(spec, "tab_labels");
        if (labels) {
            if (!cJSON_IsArray(labels) || cJSON_GetArraySize(labels) < 1 || cJSON_GetArraySize(labels) > 8 ||
                cJSON_GetObjectItemCaseSensitive(spec, "children")) { cJSON_Delete(spec); return 0; }
            auto *children = cJSON_AddArrayToObject(spec, "children");
            if (!children) { cJSON_Delete(spec); return 0; }
            const cJSON *label = nullptr;
            cJSON_ArrayForEach(label, labels) {
                auto *tab = cJSON_CreateObject();
                if (!cJSON_IsString(label) || !label->valuestring || !tab ||
                    !cJSON_AddStringToObject(tab, "type", "tab") || !cJSON_AddStringToObject(tab, "title", label->valuestring)) {
                    cJSON_Delete(tab); cJSON_Delete(spec); return 0;
                }
                cJSON_AddItemToArray(children, tab);
            }
        }
    }
    lv_obj_t *object = builder_(spec, row->object, context_);
    cJSON_Delete(spec);
    return handle(object);
}
CrystalUI::Handle CrystalUI::tab(Handle view, unsigned index) const {
    if (!valid_type(view, &lv_tabview_class)) return 0;
    auto *content = lv_tabview_get_content(node(view)->object);
    if (index >= lv_obj_get_child_cnt(content)) return 0;
    return handle(lv_obj_get_child(content, static_cast<int32_t>(index)));
}
bool CrystalUI::remove(Handle id) {
    auto *row = node(id); if (!row || row->object == root_) return false;
    lv_obj_del(owned_tree(row->object)); return true;
}
bool CrystalUI::set_text(Handle id, const char *text) {
    auto *row = node(id); if (!row || !bounded_text(text, 4096)) return false;
    if (lv_obj_check_type(row->object, &lv_textarea_class)) lv_textarea_set_text(row->object, text);
    else if (row->text && lv_obj_check_type(row->text, &lv_label_class)) lv_label_set_text(row->text, text);
    else return false;
    return true;
}
bool CrystalUI::get_text(Handle id, std::string *text) const {
    auto *row = node(id); if (!row || !text) return false;
    if (lv_obj_check_type(row->object, &lv_textarea_class)) *text = lv_textarea_get_text(row->object);
    else if (row->text && lv_obj_check_type(row->text, &lv_label_class)) *text = lv_label_get_text(row->text);
    else return false;
    return true;
}
bool CrystalUI::set_value(Handle id, int value) {
    auto *row = node(id); if (!row) return false;
    auto *object = row->object;
    if (lv_obj_check_type(object, &lv_slider_class)) {
        if (value < lv_slider_get_min_value(object) || value > lv_slider_get_max_value(object)) return false;
        lv_slider_set_value(object, value, LV_ANIM_OFF);
    } else if (lv_obj_check_type(object, &lv_arc_class)) {
        if (value < lv_arc_get_min_value(object) || value > lv_arc_get_max_value(object)) return false;
        lv_arc_set_value(object, value);
    } else if (lv_obj_check_type(object, &lv_switch_class)) {
        if (value != 0 && value != 1) return false;
        if (value) lv_obj_add_state(object, LV_STATE_CHECKED); else lv_obj_clear_state(object, LV_STATE_CHECKED);
    } else return false;
    return true;
}
bool CrystalUI::get_value(Handle id, int *value) const {
    auto *row = node(id); if (!row || !value) return false;
    if (lv_obj_check_type(row->object, &lv_slider_class)) *value = lv_slider_get_value(row->object);
    else if (lv_obj_check_type(row->object, &lv_arc_class)) *value = lv_arc_get_value(row->object);
    else if (lv_obj_check_type(row->object, &lv_switch_class)) *value = lv_obj_has_state(row->object, LV_STATE_CHECKED);
    else return false;
    return true;
}
bool CrystalUI::set_hidden(Handle id, bool hidden) {
    auto *row = node(id); if (!row) return false;
    if (hidden) { if (blur_hook) blur_hook(row->object); row = node(id); if (!row) return false; lv_obj_add_flag(row->object, LV_OBJ_FLAG_HIDDEN); }
    else lv_obj_clear_flag(row->object, LV_OBJ_FLAG_HIDDEN);
    if (hidden) lv_obj_add_flag(owned_tree(row->object), LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(owned_tree(row->object), LV_OBJ_FLAG_HIDDEN);
    return true;
}
bool CrystalUI::set_enabled(Handle id, bool enabled) {
    auto *row = node(id); if (!row) return false;
    if (enabled) lv_obj_clear_state(row->object, LV_STATE_DISABLED);
    else { if (blur_hook) blur_hook(row->object); row = node(id); if (!row) return false; lv_obj_add_state(row->object, LV_STATE_DISABLED); }
    return true;
}
bool CrystalUI::set_style(Handle id, const cJSON *style) {
    auto *row = node(id); return row && style_ && style_(row->object, style);
}
bool CrystalUI::set_focus(Handle id) {
    auto *row = node(id); if (!row || paused_) return false;
    for (auto *object = row->object; object; object = lv_obj_get_parent(object))
        if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN) || lv_obj_has_state(object, LV_STATE_DISABLED)) return false;
    if (lv_obj_check_type(row->object, &lv_textarea_class) && (!focus_hook || !focus_hook(row->object, root_))) return false;
    row = node(id); if (!row) return false;
    return lv_event_send(row->object, LV_EVENT_FOCUSED, nullptr) == LV_RES_OK;
}
bool CrystalUI::scroll_to(Handle id, bool animate, const char *align) {
    auto *row = node(id);
    if (!row || !align || (strcmp(align, "top") && strcmp(align, "center") && strcmp(align, "bottom"))) return false;
    auto *object = row->object, *parent = lv_obj_get_parent(object);
    if (!parent) return false;
    lv_obj_update_layout(parent);
    lv_obj_scroll_to_view_recursive(object, animate ? LV_ANIM_ON : LV_ANIM_OFF);
    lv_area_t child_area{}, parent_area{}; lv_obj_get_coords(object, &child_area); lv_obj_get_coords(parent, &parent_area);
    const int wanted = !strcmp(align, "top") ? parent_area.y1 + lv_obj_get_style_pad_top(parent, 0)
        : !strcmp(align, "bottom") ? parent_area.y2 - lv_obj_get_style_pad_bottom(parent, 0) - lv_area_get_height(&child_area) + 1
        : parent_area.y1 + (lv_area_get_height(&parent_area) - lv_area_get_height(&child_area)) / 2;
    lv_obj_scroll_by_bounded(parent, 0, wanted - child_area.y1, animate ? LV_ANIM_ON : LV_ANIM_OFF);
    return true;
}
bool CrystalUI::set_parent(Handle id, Handle parent_id) {
    auto *row = node(id), *parent = node(parent_id);
    if (!row || !parent || row->object == root_ || descendant(parent->object, row->object)) return false;
    // Only layout containers may receive children; controls own internal parts.
    auto *object = parent->object;
    if (!lv_obj_check_type(object, &lv_obj_class)) return false;
    lv_obj_set_parent(owned_tree(row->object), object); return true;
}
bool CrystalUI::get_size(Handle id, Size *size) const {
    auto *row = node(id); if (!row || !size) return false;
    lv_obj_update_layout(row->object); lv_area_t area{}; lv_obj_get_coords(row->object, &area);
    *size = {lv_area_get_width(&area), lv_area_get_height(&area), area.x1, area.y1}; return true;
}
int CrystalUI::event_index(const char *name) {
    const char *names[] = {"click", "long_press", "change", "focus", "blur", "scroll", "delete"};
    if (name) for (int i = 0; i < 7; ++i) if (!strcmp(name, names[i])) return i;
    return -1;
}
bool CrystalUI::on(Handle id, const char *event, Callback callback, void *user) {
    auto *row = node(id); const int index = event_index(event);
    if (!row || index < 0 || !callback) return false;
    row->events[index] = {callback, user}; return true;
}
bool CrystalUI::off(Handle id, const char *event) {
    auto *row = node(id); const int index = event_index(event);
    if (!row || index < 0) return false;
    row->events[index] = {}; return true;
}
void CrystalUI::invalidate(lv_obj_t *object) {
    auto id = handle(object);
    series_.erase(std::remove_if(series_.begin(), series_.end(), [id](const Series &row) { return row.chart == id; }), series_.end());
    for (size_t i = 0; i < animations_.size();) {
        if (animations_[i]->object == id) anim_cancel(animations_[i]->id); else ++i;
    }
    if (object == root_) root_ = nullptr;
}
void CrystalUI::event_cb(lv_event_t *event) {
    auto *row = static_cast<Node *>(lv_event_get_user_data(event));
    if (!row || lv_event_get_target(event) != row->object) return;
    auto *owner = row->owner; auto id = row->id; const auto code = lv_event_get_code(event);
    Listener listener{};
    for (size_t i = 0; i < 7; ++i) if (code == events[i]) listener = row->events[i];
    if (code == LV_EVENT_DELETE) { owner->invalidate(row->object); row->object = row->text = nullptr; }
    if (code == LV_EVENT_FOCUSED && !owner->paused_ && focus_hook && row->object &&
        lv_obj_check_type(row->object, &lv_textarea_class)) (void)focus_hook(row->object, owner->root_);
    if (listener.callback && (!owner->paused_ || code == LV_EVENT_DELETE || code == LV_EVENT_DEFOCUSED))
        listener.callback(id, listener.user);
}
bool CrystalUI::list_count(Handle id, unsigned *count) const {
    if (!count || !valid_type(id, &lv_list_class)) return false;
    *count = lv_obj_get_child_cnt(node(id)->object); return true;
}
bool CrystalUI::list_add(Handle id, const ListItem &item) {
    unsigned count; return list_count(id, &count) && list_insert(id, count, item);
}
bool CrystalUI::list_set_item_height(Handle id, unsigned height) {
    if (!valid_type(id, &lv_list_class) || height < 44 || height > 480) return false;
    node(id)->item_height = height;
    auto *list = node(id)->object;
    for (unsigned i = 0; i < lv_obj_get_child_cnt(list); ++i) lv_obj_set_height(lv_obj_get_child(list, i), height);
    return true;
}
bool CrystalUI::list_insert(Handle id, unsigned index, const ListItem &item) {
    unsigned count;
    if (!list_count(id, &count) || index > count || count >= 64 || !bounded_text(item.primary, 96) ||
        (item.secondary && !bounded_text(item.secondary, 96)) || (item.icon && (!image_ || !bounded_text(item.icon, 160)))) return false;
    auto *list = node(id)->object; auto *row = lv_btn_create(list);
    if (!row) return false;
    lv_obj_set_width(row, LV_PCT(100)); lv_obj_set_height(row, node(id)->item_height ? node(id)->item_height : LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, 44, 0); lv_obj_set_style_bg_color(row, lv_color_hex(0x1C2733), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x344553), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(row, lv_color_hex(0xE6EDF5), 0);
    lv_obj_set_style_border_width(row, 0, 0); lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    if (item.icon) {
        auto *icon = lv_img_create(row);
        if (!icon || !image_(icon, item.icon, context_)) { lv_obj_del(row); return false; }
    }
    auto *label = lv_label_create(row);
    if (!label) { lv_obj_del(row); return false; }
    std::string text(item.primary); if (item.secondary) { text += "\n"; text += item.secondary; }
    lv_label_set_text(label, text.c_str()); lv_obj_set_flex_grow(label, 1);
    auto row_id = adopt(row, label);
    if (!row_id) { lv_obj_del(row); return false; }
    if (item.on_click) on(row_id, "click", item.on_click, item.user);
    lv_obj_move_to_index(row, static_cast<int32_t>(index)); return true;
}
bool CrystalUI::list_remove(Handle id, unsigned index) {
    unsigned count; if (!list_count(id, &count) || index >= count) return false;
    lv_obj_del(lv_obj_get_child(node(id)->object, static_cast<int32_t>(index))); return true;
}
bool CrystalUI::list_clear(Handle id) {
    if (!valid_type(id, &lv_list_class)) return false;
    lv_obj_clean(node(id)->object); return true;
}
CrystalUI::Handle CrystalUI::chart_add_series(Handle id, uint32_t color) {
    if (!valid_type(id, &lv_chart_class) || color > 0xffffff || series_.size() >= 16) return 0;
    auto *chart = node(id)->object; unsigned count = 0;
    for (auto *row = lv_chart_get_series_next(chart, nullptr); row; row = lv_chart_get_series_next(chart, row)) ++count;
    if (count >= 4) return 0;
    auto handle = allocate_handle(); if (!handle) return 0;
    auto *series = lv_chart_add_series(chart, lv_color_hex(color), LV_CHART_AXIS_PRIMARY_Y);
    if (!series) return 0;
    series_.push_back({handle, id, series}); return handle;
}
bool CrystalUI::chart_set_data(Handle id, Handle series_id, const std::vector<int> &values) {
    if (!valid_type(id, &lv_chart_class) || values.size() > 128) return false;
    auto found = std::find_if(series_.begin(), series_.end(), [=](const Series &row) { return row.id == series_id && row.chart == id; });
    if (found == series_.end()) return false;
    auto *chart = node(id)->object;
    if (values.size() > lv_chart_get_point_count(chart)) return false;
    for (auto value : values) if (value < -32768 || value >= LV_CHART_POINT_NONE) return false;
    lv_chart_set_all_value(chart, found->series, LV_CHART_POINT_NONE);
    const bool scatter = lv_chart_get_type(chart) == LV_CHART_TYPE_SCATTER;
    for (size_t i = 0; i < values.size(); ++i) {
        if (scatter) lv_chart_set_value_by_id2(chart, found->series, i, i, values[i]);
        else lv_chart_set_value_by_id(chart, found->series, i, values[i]);
    }
    if (node(id)->chart_auto_range) {
        int lo = 32767, hi = -32768;
        for (auto *series = lv_chart_get_series_next(chart, nullptr); series; series = lv_chart_get_series_next(chart, series)) {
            const auto *data = lv_chart_get_y_array(chart, series);
            for (unsigned i = 0; i < lv_chart_get_point_count(chart); ++i) if (data[i] != LV_CHART_POINT_NONE) {
                lo = std::min(lo, int(data[i])); hi = std::max(hi, int(data[i]));
            }
        }
        if (lo <= hi) {
            if (lo == hi) { if (hi < 32766) ++hi; else --lo; }
            lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, lo, hi);
        }
    }
    if (scatter) lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_X, 0, std::max(1, int(lv_chart_get_point_count(chart)) - 1));
    lv_chart_refresh(chart); return true;
}
CrystalUI::Handle CrystalUI::chart_series(Handle id, unsigned index) const {
    if (!valid_type(id, &lv_chart_class)) return 0;
    for (const auto &row : series_) if (row.chart == id) {
        if (!index--) return row.id;
    }
    return 0;
}
bool CrystalUI::chart_set_range(Handle id, int lo, int hi) {
    if (!valid_type(id, &lv_chart_class) || lo >= hi || lo < -32768 || hi > 32767) return false;
    node(id)->chart_auto_range = false;
    lv_chart_set_range(node(id)->object, LV_CHART_AXIS_PRIMARY_Y, lo, hi); return true;
}
bool CrystalUI::chart_clear(Handle id) {
    if (!valid_type(id, &lv_chart_class)) return false;
    auto *chart = node(id)->object;
    for (auto *series = lv_chart_get_series_next(chart, nullptr); series; series = lv_chart_get_series_next(chart, series))
        lv_chart_set_all_value(chart, series, LV_CHART_POINT_NONE);
    lv_chart_refresh(chart); return true;
}
CrystalUI::Handle CrystalUI::timer(uint32_t period, Callback callback, void *user, bool once) {
    if (!period || period > 86400000 || !callback || timers_.size() >= 16 || !root_ || resetting_) return 0;
    auto id = allocate_handle(); if (!id) return 0;
    auto row = std::unique_ptr<Timer>(new (std::nothrow) Timer{});
    if (!row) return 0;
    row->owner = this; row->id = id; row->callback = callback; row->user = user; row->once = once;
    row->timer = lv_timer_create(timer_cb, period, row.get()); if (!row->timer) return 0;
    row->resource = resources_.track(CrystalResourceKind::Timer, row->timer);
    if (!row->resource) { lv_timer_del(row->timer); return 0; }
    if (paused_) lv_timer_pause(row->timer);
    timers_.push_back(std::move(row)); return id;
}
void CrystalUI::timer_cb(lv_timer_t *timer) {
    auto *row = static_cast<Timer *>(timer->user_data); auto *owner = row->owner;
    auto callback = row->callback; auto id = row->id; auto *user = row->user;
    if (row->once) owner->timer_delete(id);
    callback(id, user); // Callback may delete/recreate timers or close the app.
}
bool CrystalUI::timer_delete(Handle id) {
    for (auto it = timers_.begin(); it != timers_.end(); ++it) if ((*it)->id == id) {
        lv_timer_del((*it)->timer); resources_.release((*it)->resource); timers_.erase(it); return true;
    }
    return false;
}
CrystalUI::Handle CrystalUI::anim(Handle object, const cJSON *props, uint32_t duration, const char *easing,
                                uint32_t delay, Callback done, void *user) {
    auto *row = node(object);
    if (!row || !cJSON_IsObject(props) || !props->child || !duration || duration > 60000 || delay > 60000 ||
        !easing || animations_.size() >= 16 || paused_ || resetting_) return 0;
    lv_anim_path_cb_t path = !strcmp(easing, "ease_out") ? lv_anim_path_ease_out : !strcmp(easing, "ease_in") ? lv_anim_path_ease_in
        : !strcmp(easing, "ease_in_out") ? lv_anim_path_ease_in_out : !strcmp(easing, "linear") ? lv_anim_path_linear : nullptr;
    if (!path) return 0;
    auto animation = std::unique_ptr<Animation>(new (std::nothrow) Animation{});
    if (!animation) return 0;
    animation->owner = this; animation->object = object; animation->duration = duration; animation->delay = delay;
    animation->path = path; animation->done = done; animation->user = user;
    const cJSON *prop = nullptr;
    cJSON_ArrayForEach(prop, props) {
        if (!prop->string || !cJSON_IsNumber(prop) || !std::isfinite(prop->valuedouble) || std::floor(prop->valuedouble) != prop->valuedouble) return 0;
        const std::string key(prop->string); int32_t from = 0, maximum = 0;
        if (key == "opacity") { maximum = 255; from = lv_obj_get_style_opa(row->object, 0); }
        else if (key == "radius") { maximum = 255; from = lv_obj_get_style_radius(row->object, 0); }
        else if (key == "bg_color") { maximum = 0xffffff; from = lv_color_to32(lv_obj_get_style_bg_color(row->object, 0)) & 0xffffff; }
        else if (key == "text_color") { maximum = 0xffffff; from = lv_color_to32(lv_obj_get_style_text_color(row->object, 0)) & 0xffffff; }
        else return 0;
        if (prop->valuedouble < 0 || prop->valuedouble > maximum) return 0;
        animation->from.emplace_back(key, from); animation->to.emplace_back(key, static_cast<int32_t>(prop->valuedouble));
    }
    animation->id = allocate_handle(); if (!animation->id) return 0;
    auto id = animation->id; auto *raw = animation.get(); animations_.push_back(std::move(animation));
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, raw); lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, duration); lv_anim_set_delay(&a, delay); lv_anim_set_path_cb(&a, path);
    lv_anim_set_exec_cb(&a, anim_step); lv_anim_set_ready_cb(&a, anim_done);
    if (!lv_anim_start(&a)) { anim_cancel(id); return 0; }
    return id;
}
void CrystalUI::anim_step(void *context, int32_t progress) {
    auto *a = static_cast<Animation *>(context); auto *row = a->owner->node(a->object); if (!row) return;
    for (size_t i = 0; i < a->from.size(); ++i) {
        const auto &key = a->from[i].first; const int32_t from = a->from[i].second, to = a->to[i].second;
        const int32_t value = from + static_cast<int64_t>(to - from) * progress / 255;
        if (key == "opacity") lv_obj_set_style_opa(row->object, value, 0);
        else if (key == "radius") lv_obj_set_style_radius(row->object, value, 0);
        else {
            auto color = lv_color_mix(lv_color_hex(to), lv_color_hex(from), progress);
            if (key == "bg_color") lv_obj_set_style_bg_color(row->object, color, 0);
            else lv_obj_set_style_text_color(row->object, color, 0);
        }
    }
}
void CrystalUI::anim_done(lv_anim_t *animation) {
    auto *row = static_cast<Animation *>(animation->var); auto *owner = row->owner;
    auto callback = row->done; auto id = row->id; auto *user = row->user;
    owner->anim_cancel(id); if (callback) callback(id, user);
}
bool CrystalUI::anim_cancel(Handle id) {
    for (auto it = animations_.begin(); it != animations_.end(); ++it) if ((*it)->id == id) {
        lv_anim_del(it->get(), anim_step); animations_.erase(it); return true;
    }
    return false;
}
void CrystalUI::pause() {
    paused_ = true; if (root_ && blur_hook) blur_hook(root_);
    for (const auto &row : timers_) lv_timer_pause(row->timer);
    while (!animations_.empty()) anim_cancel(animations_.back()->id);
}
void CrystalUI::clear_callbacks() {
    pause();
    while (!timers_.empty()) timer_delete(timers_.back()->id);
}
void CrystalUI::resume() {
    paused_ = false; for (const auto &row : timers_) lv_timer_resume(row->timer);
}
void CrystalUI::reset() {
    resetting_ = true; clear_callbacks();
    for (const auto &row : nodes_) if (row->object) lv_obj_remove_event_cb(row->object, event_cb);
    nodes_.clear(); series_.clear(); root_ = nullptr; paused_ = false; resetting_ = false;
}
