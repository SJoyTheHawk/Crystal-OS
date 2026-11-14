/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "crystal_page_transition.hpp"
#include "crystal_stylesheet.hpp"

namespace {
lv_obj_t *s_retiring = nullptr;
lv_obj_t *s_entering = nullptr;
lv_obj_t *s_covered = nullptr;
void enter_frame(void *object, int32_t value)
{
    auto *page = static_cast<lv_obj_t *>(object);
    lv_obj_set_style_opa(page, (LV_OPA_COVER * value) / 256, 0);
}
void complete_enter()
{
    auto *page = s_entering;
    auto *covered = s_covered;
    s_entering = s_covered = nullptr;
    if (page != nullptr && lv_obj_is_valid(page)) {
        lv_anim_del(page, enter_frame);
        lv_obj_set_style_opa(page, LV_OPA_COVER, 0);
    }
    if (covered != nullptr && lv_obj_is_valid(covered)) {
        lv_obj_add_flag(covered, LV_OBJ_FLAG_HIDDEN);
    }
}
void enter_ready(lv_anim_t *)
{
    complete_enter();
}
void retire_frame(void *object, int32_t value)
{
    auto *page = static_cast<lv_obj_t *>(object);
    lv_obj_set_style_translate_x(page, (36 * value) / 256, 0);
    lv_obj_set_style_opa(page, (LV_OPA_COVER * (256 - value)) / 256, 0);
}
void retire_ready(lv_anim_t *animation)
{
    auto *page = static_cast<lv_obj_t *>(animation->var);
    if (s_retiring == page) s_retiring = nullptr;
    lv_obj_del(page);
}
void animate(lv_obj_t *page, lv_anim_exec_xcb_t callback, lv_anim_ready_cb_t ready)
{
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, page);
    lv_anim_set_values(&animation, 0, 256);
    lv_anim_set_time(&animation, crystal_visual_style().page_transition_ms);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&animation, callback);
    lv_anim_set_ready_cb(&animation, ready);
    lv_anim_start(&animation);
}
void disable_input(lv_obj_t *object)
{
    lv_obj_add_state(object, LV_STATE_DISABLED);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i) {
        disable_input(lv_obj_get_child(object, static_cast<int32_t>(i)));
    }
}
}

void crystal_page_transition_settle()
{
    complete_enter();
    if (s_retiring == nullptr) return;
    auto *page = s_retiring;
    s_retiring = nullptr;
    lv_anim_del(page, retire_frame);
    lv_obj_del(page);
}

void crystal_page_transition_enter(lv_obj_t *page, lv_obj_t *covered_page)
{
    complete_enter();
    if (page == nullptr) return;
    s_entering = page;
    s_covered = covered_page;
    if (crystal_visual_style().page_transition_ms == 0) {
        complete_enter();
        return;
    }
    lv_obj_add_event_cb(page, [](lv_event_t *event) {
        if (lv_event_get_target(event) == s_entering) {
            s_entering = s_covered = nullptr;
        }
    }, LV_EVENT_DELETE, nullptr);
    animate(page, enter_frame, enter_ready);
}

void crystal_page_transition_retire(lv_obj_t *page)
{
    crystal_page_transition_settle();
    if (page == nullptr) return;
    lv_anim_del(page, enter_frame);
    if (crystal_visual_style().page_transition_ms == 0) {
        lv_obj_del(page);
        return;
    }
    // An outgoing page must not intercept a second tap or retain keyboard
    // focus. Keep it visible but let input reach the revealed page beneath.
    disable_input(page);
    lv_obj_set_style_translate_x(page, 0, 0);
    lv_obj_set_style_opa(page, LV_OPA_COVER, 0);
    s_retiring = page;
    animate(page, retire_frame, retire_ready);
}

lv_obj_t *crystal_page_transition_capture_backdrop(const lv_area_t &area)
{
    auto *screen = lv_scr_act();
    auto *snapshot = lv_snapshot_take(screen, LV_IMG_CF_TRUE_COLOR);
    lv_area_t source_area{};
    lv_obj_get_coords(screen, &source_area);
    auto *backdrop = lv_obj_create(lv_layer_top());
    if (backdrop == nullptr) {
        if (snapshot != nullptr) lv_snapshot_free(snapshot);
        return nullptr;
    }
    lv_obj_remove_style_all(backdrop);
    lv_obj_set_pos(backdrop, area.x1, area.y1);
    lv_obj_set_size(backdrop, lv_area_get_width(&area), lv_area_get_height(&area));
    lv_obj_set_style_bg_color(backdrop, lv_color_hex(crystal_visual_style().background), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_COVER, 0);
    lv_obj_clear_flag(backdrop, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    // If snapshot allocation fails, an opaque Crystal surface still prevents
    // the launcher from flashing through. Never change app lifecycle for UX.
    if (snapshot == nullptr) return backdrop;
    auto *image = lv_img_create(backdrop);
    if (image == nullptr) {
        lv_snapshot_free(snapshot);
        return backdrop;
    }
    lv_img_set_src(image, snapshot);
    // LVGL snapshots begin at the object's own coordinates, not (0, 0).
    // Brookesia positions app screens below the status bar. Subtracting the
    // app area alone would shift that frozen image upward by the status band.
    const lv_coord_t extra_x = (snapshot->header.w - lv_obj_get_width(screen)) / 2;
    const lv_coord_t extra_y = (snapshot->header.h - lv_obj_get_height(screen)) / 2;
    lv_obj_set_pos(image, source_area.x1 - extra_x - area.x1,
                         source_area.y1 - extra_y - area.y1);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(image, [](lv_event_t *event) {
        lv_snapshot_free(static_cast<lv_img_dsc_t *>(lv_event_get_user_data(event)));
    }, LV_EVENT_DELETE, snapshot);
    return backdrop;
}

void crystal_page_transition_bind_backdrop(lv_obj_t *page, lv_obj_t *backdrop)
{
    if (page == nullptr || backdrop == nullptr) return;
    lv_obj_add_event_cb(page, [](lv_event_t *event) {
        if (lv_event_get_target(event) != lv_event_get_current_target(event)) return;
        auto *backdrop = static_cast<lv_obj_t *>(lv_event_get_user_data(event));
        // A display teardown can delete the older backdrop before the page.
        if (lv_obj_is_valid(backdrop)) lv_obj_del(backdrop);
    }, LV_EVENT_DELETE, backdrop);
}
