/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

#include "crystal_shell.hpp"

#include <string.h>

#include "lvgl.h"

namespace {
// Match Calculator's proven bottom inset. The reserved band remains 200 px so
// the keyboard top does not move; the actual object stops above the gesture
// edge and home indicator.
constexpr lv_coord_t kKeyboardBandHeight = 200;
constexpr lv_coord_t kBottomSafe = 26;
constexpr lv_coord_t kKeyboardHeight = kKeyboardBandHeight - kBottomSafe;
constexpr uint32_t kRevealAnimMs = 250;

#define KEY (LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT | 1)
#define WIDE_KEY (LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT | 2)

// Every plane uses the same row count and relative widths (10/9/9/5). Only the
// key caps change, so switching planes never moves delete, space, cursor, or
// Done. Plane order follows iOS: letters -> 123 -> #+= -> back to letters.
static const char *kLowerMap[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    "ABC", "z", "x", "c", "v", "b", "n", "m", LV_SYMBOL_BACKSPACE, "\n",
    "123", LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};
static const char *kUpperMap[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
    "abc", "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_BACKSPACE, "\n",
    "123", LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};
// Digits and everyday punctuation. Row 3 leads to the symbols plane and row 4
// returns to letters, so the two plane keys never collide.
static const char *kNumberMap[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "-", "/", ":", ";", "(", ")", "$", "&", "@", "\n",
    "#+=", ".", ",", "?", "!", "'", "\"", "%", LV_SYMBOL_BACKSPACE, "\n",
    "abc", LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};
// Brackets, maths, and the remaining ASCII punctuation. Together the number and
// symbol planes reach every printable ASCII symbol.
static const char *kSymbolMap[] = {
    "[", "]", "{", "}", "#", "^", "*", "+", "=", "~", "\n",
    "_", "\\", "|", "<", ">", "`", "$", "&", "@", "\n",
    "123", ".", ",", "?", "!", "'", ":", ";", LV_SYMBOL_BACKSPACE, "\n",
    "abc", LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};

// One control map serves every plane because the geometry is identical.
static const lv_btnmatrix_ctrl_t kControls[] = {
    KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY,
    KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY,
    KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY,
    WIDE_KEY, WIDE_KEY, KEY | 6, WIDE_KEY, WIDE_KEY,
};

#undef KEY
#undef WIDE_KEY

lv_obj_t *s_keyboard = nullptr;
lv_obj_t *s_field = nullptr;
lv_obj_t *s_viewport = nullptr;
lv_coord_t s_viewport_height = 0;
// Phase 11 edit begin: preserve viewport state while the keyboard owns the layout.
lv_coord_t s_viewport_scroll_y = 0;
bool s_viewport_was_scrollable = false;
bool s_viewport_resized = false;
lv_dir_t s_viewport_scroll_dir = LV_DIR_NONE;
bool s_viewport_was_elastic = false;
bool s_viewport_was_momentum = false;
bool s_hiding = false;
// Phase 11 edit end.

// Lets a caller re-lay out around the keyboard band. The keyboard closes through
// several paths (Done, Cancel, background tap, field deletion), so a dialog that
// moves with the keyboard needs one notification point rather than four.
crystal_keyboard_state_cb_t s_state_cb = nullptr;
void *s_state_user_data = nullptr;

// Returns the plane a cap switches to, or -1 when the cap is not a plane key.
int plane_for_key(const char *text)
{
    if (text == nullptr) return -1;
    if (!strcmp(text, "abc")) return LV_KEYBOARD_MODE_TEXT_LOWER;
    if (!strcmp(text, "ABC")) return LV_KEYBOARD_MODE_TEXT_UPPER;
    if (!strcmp(text, "123")) return LV_KEYBOARD_MODE_SPECIAL;
    if (!strcmp(text, "#+=")) return LV_KEYBOARD_MODE_USER_1;
    return -1;
}

bool is_control_key(const char *text)
{
    return text != nullptr &&
           (plane_for_key(text) >= 0 ||
            !strcmp(text, LV_SYMBOL_BACKSPACE) || !strcmp(text, LV_SYMBOL_LEFT) ||
            !strcmp(text, LV_SYMBOL_RIGHT));
}

// The stock handler only understands abc/ABC/1#, so a fourth plane needs its own
// dispatch. Plane keys are consumed here; everything else falls through to LVGL.
void key_event_cb(lv_event_t *event)
{
    lv_obj_t *keyboard = static_cast<lv_obj_t *>(lv_event_get_target(event));
    const uint16_t id = lv_btnmatrix_get_selected_btn(keyboard);
    if (id == LV_BTNMATRIX_BTN_NONE) return;
    const int plane = plane_for_key(lv_btnmatrix_get_btn_text(keyboard, id));
    if (plane >= 0) {
        lv_keyboard_set_mode(keyboard, static_cast<lv_keyboard_mode_t>(plane));
        return;
    }
    lv_keyboard_def_event_cb(event);
}

void style_key_cb(lv_event_t *event)
{
    auto *dsc = static_cast<lv_obj_draw_part_dsc_t *>(lv_event_get_draw_part_dsc(event));
    if (dsc == nullptr || dsc->class_p != &lv_btnmatrix_class ||
            dsc->type != LV_BTNMATRIX_DRAW_PART_BTN) return;

    lv_obj_t *keyboard = static_cast<lv_obj_t *>(lv_event_get_target(event));
    const char *text = lv_btnmatrix_get_btn_text(keyboard, static_cast<uint16_t>(dsc->id));
    dsc->rect_dsc->radius = 5;
    dsc->rect_dsc->border_width = 0;
    dsc->rect_dsc->shadow_width = 1;
    dsc->rect_dsc->shadow_ofs_y = 1;
    dsc->rect_dsc->shadow_color = lv_color_hex(0x73767d);
    dsc->label_dsc->color = lv_color_hex(0x111214);
    if (text != nullptr && !strcmp(text, LV_SYMBOL_OK)) {
        dsc->rect_dsc->bg_color = lv_color_hex(0x087bdb);
        dsc->label_dsc->color = lv_color_white();
    } else if (is_control_key(text)) {
        dsc->rect_dsc->bg_color = lv_color_hex(0xadb1b8);
    } else {
        dsc->rect_dsc->bg_color = lv_color_hex(0xf7f7f8);
    }
}

void watched_object_deleted(lv_event_t *event)
{
    lv_obj_t *deleted = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if (deleted == s_field) s_field = nullptr;
    if (deleted == s_viewport) s_viewport = nullptr;
    if (!s_hiding) crystal_keyboard_hide();
}

// Deleting the keyboard inside its own READY/CANCEL makes lv_event_send return
// LV_RES_INV, and LVGL then skips the matching send to the textarea. Field-level
// listeners depend on that second send, so the delete has to happen after the
// dispatch unwinds.
void keyboard_close_async(void *)
{
    crystal_keyboard_hide();
}

void keyboard_close_event(lv_event_t *)
{
    lv_async_call(keyboard_close_async, nullptr);
}

void viewport_click_event(lv_event_t *event)
{
    if (lv_event_get_target(event) == s_viewport) crystal_keyboard_hide();
}

void center_covered_field()
{
    if (s_field == nullptr || s_viewport == nullptr || s_keyboard == nullptr) return;
    lv_obj_update_layout(s_keyboard);
    // Phase 11 edit begin: calculate a bounded reveal from the real top of the form.
    // Start from the real top of the form. Focus events can leave LVGL's
    // automatic scroll position at the bottom after the viewport is resized;
    // using that position as the basis for another delta hides the whole form.
    lv_obj_scroll_to_y(s_viewport, 0, LV_ANIM_OFF);
    lv_obj_update_layout(s_viewport);
    lv_area_t field_area{};
    lv_area_t keyboard_area{};
    lv_obj_get_coords(s_field, &field_area);
    lv_obj_get_coords(s_keyboard, &keyboard_area);
    lv_area_t viewport_area{};
    lv_obj_get_coords(s_viewport, &viewport_area);
    const lv_coord_t margin = 8;
    const lv_coord_t safe_top = viewport_area.y1 + margin;
    const lv_coord_t safe_bottom = keyboard_area.y1 - margin;
    lv_coord_t delta = 0;
    if (field_area.y2 > safe_bottom) delta = static_cast<lv_coord_t>(field_area.y2 - safe_bottom);
    else if (field_area.y1 < safe_top) delta = static_cast<lv_coord_t>(field_area.y1 - safe_top);
    if (delta == 0) return;

    const lv_coord_t current = lv_obj_get_scroll_y(s_viewport);
    // LVGL exposes scroll_top as the current offset and scroll_bottom as the
    // remaining range. Their sum is the actual maximum scroll position.
    const lv_coord_t maximum = LV_MAX(0, static_cast<lv_coord_t>(
        lv_obj_get_scroll_top(s_viewport) + lv_obj_get_scroll_bottom(s_viewport)));
    const lv_coord_t target = LV_CLAMP(0, static_cast<lv_coord_t>(current + delta), maximum);
    lv_obj_set_style_anim_time(s_viewport, kRevealAnimMs, LV_PART_MAIN);
    lv_obj_scroll_to_y(s_viewport, target, LV_ANIM_ON);
    // Phase 11 edit end.
}
} // namespace

bool crystal_keyboard_show(lv_obj_t *field, lv_obj_t *viewport)
{
    if (field == nullptr || viewport == nullptr) return false;
    if (s_keyboard != nullptr && s_field == field) return true;

    // Moving between fields in one form must not restore and shrink the viewport
    // again. Apart from making the form jump under the active touch, that layout
    // change can leave focus on the field that moved beneath the pointer. Rebind
    // the existing keyboard in place, matching the stable behavior of the dense
    // IP Settings form.
    if (s_keyboard != nullptr && s_viewport == viewport) {
        lv_obj_t *previous = s_field;
        if (previous != nullptr && previous != viewport) {
            lv_obj_remove_event_cb(previous, watched_object_deleted);
            lv_event_send(previous, LV_EVENT_DEFOCUSED, nullptr);
        }
        s_field = field;
        lv_keyboard_set_textarea(s_keyboard, field);
        lv_obj_add_event_cb(field, watched_object_deleted, LV_EVENT_DELETE, nullptr);
        center_covered_field();
        return true;
    }

    crystal_keyboard_hide();

    s_field = field;
    s_viewport = viewport;
    // Phase 11 edit begin: save all viewport state before keyboard rebinding.
    s_viewport_height = lv_obj_get_height(viewport);
    s_viewport_scroll_y = lv_obj_get_scroll_y(viewport);
    s_viewport_was_scrollable = lv_obj_has_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);
    s_viewport_scroll_dir = lv_obj_get_scroll_dir(viewport);
    s_viewport_was_elastic = lv_obj_has_flag(viewport, LV_OBJ_FLAG_SCROLL_ELASTIC);
    s_viewport_was_momentum = lv_obj_has_flag(viewport, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    s_viewport_resized = false;

    // Announced before the viewport is measured so a listener that moves itself
    // clear of the band is measured where it ends up, not where it started.
    if (s_state_cb != nullptr) s_state_cb(true, s_state_user_data);
    lv_obj_update_layout(viewport);

    const lv_coord_t display_height = lv_disp_get_ver_res(nullptr);
    const lv_coord_t display_width = lv_disp_get_hor_res(nullptr);
    lv_area_t viewport_area{};
    lv_obj_get_coords(viewport, &viewport_area);
    const lv_coord_t keyboard_top = static_cast<lv_coord_t>(display_height - kKeyboardBandHeight);
    // Only clip a viewport the keyboard would actually cover. Shrinking one that
    // already fits would stretch it and drag its top edge off position.
    if (viewport_area.y2 >= keyboard_top) {
        const lv_coord_t free_height = LV_MAX(1, keyboard_top - viewport_area.y1);
        lv_obj_set_height(viewport, free_height);
        lv_obj_update_layout(viewport);
        lv_area_t resized_area{};
        lv_obj_get_coords(viewport, &resized_area);
        if (resized_area.y1 != viewport_area.y1) {
            lv_obj_set_y(viewport, static_cast<lv_coord_t>(lv_obj_get_y(viewport) +
                         viewport_area.y1 - resized_area.y1));
        }
        lv_obj_add_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(viewport, LV_DIR_VER);
        // Prevent elastic/momentum effects while revealing a field.
        lv_obj_clear_flag(viewport, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);
        s_viewport_resized = true;
    }

    s_keyboard = lv_keyboard_create(lv_layer_top());
    if (s_keyboard == nullptr) {
        crystal_keyboard_hide();
        return false;
    }
    lv_keyboard_set_map(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER, kLowerMap, kControls);
    lv_keyboard_set_map(s_keyboard, LV_KEYBOARD_MODE_TEXT_UPPER, kUpperMap, kControls);
    lv_keyboard_set_map(s_keyboard, LV_KEYBOARD_MODE_SPECIAL, kNumberMap, kControls);
    lv_keyboard_set_map(s_keyboard, LV_KEYBOARD_MODE_USER_1, kSymbolMap, kControls);
    lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    // Replace the stock dispatch so the #+= plane key is not typed as literal text.
    lv_obj_remove_event_cb(s_keyboard, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(s_keyboard, key_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_keyboard_set_popovers(s_keyboard, false);
    lv_keyboard_set_textarea(s_keyboard, field);
    lv_obj_set_size(s_keyboard, display_width, kKeyboardHeight);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, -kBottomSafe);
    lv_obj_set_style_bg_color(s_keyboard, lv_color_hex(0xd1d3d9), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_keyboard, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_keyboard, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_keyboard, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_keyboard, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_keyboard, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_column(s_keyboard, 2, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_keyboard, &lv_font_montserrat_16, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_keyboard, style_key_cb, LV_EVENT_DRAW_PART_BEGIN, nullptr);
    lv_obj_add_event_cb(s_keyboard, keyboard_close_event, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(s_keyboard, keyboard_close_event, LV_EVENT_CANCEL, nullptr);
    lv_obj_add_event_cb(field, watched_object_deleted, LV_EVENT_DELETE, nullptr);
    if (viewport != field) lv_obj_add_event_cb(viewport, watched_object_deleted, LV_EVENT_DELETE, nullptr);
    lv_obj_add_event_cb(viewport, viewport_click_event, LV_EVENT_CLICKED, nullptr);
    crystal_shell_set_keyboard_open(true);
    center_covered_field();
    return true;
}

void crystal_keyboard_hide()
{
    if (s_hiding) return;
    s_hiding = true;
    crystal_shell_set_keyboard_open(false);
    // The indev reset below aborts LVGL's normal focus handoff. Defocus the old
    // field explicitly so its cursor cannot remain visible after rebinding.
    if (s_field != nullptr) {
        lv_event_send(s_field, LV_EVENT_DEFOCUSED, nullptr);
    }
    if (s_viewport != nullptr) {
        lv_obj_remove_event_cb(s_viewport, viewport_click_event);
        lv_obj_remove_event_cb(s_viewport, watched_object_deleted);
        if (s_viewport_resized) {
            lv_obj_set_height(s_viewport, s_viewport_height);
            if (!s_viewport_was_scrollable) lv_obj_clear_flag(s_viewport, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_scroll_dir(s_viewport, s_viewport_scroll_dir);
            if (s_viewport_was_elastic) lv_obj_add_flag(s_viewport, LV_OBJ_FLAG_SCROLL_ELASTIC);
            else lv_obj_clear_flag(s_viewport, LV_OBJ_FLAG_SCROLL_ELASTIC);
            if (s_viewport_was_momentum) lv_obj_add_flag(s_viewport, LV_OBJ_FLAG_SCROLL_MOMENTUM);
            else lv_obj_clear_flag(s_viewport, LV_OBJ_FLAG_SCROLL_MOMENTUM);
            // Restore the original scroll position after teardown.
            lv_obj_update_layout(s_viewport);
            const lv_coord_t maximum = LV_MAX(0, static_cast<lv_coord_t>(
                lv_obj_get_scroll_top(s_viewport) + lv_obj_get_scroll_bottom(s_viewport)));
            lv_obj_scroll_to_y(s_viewport, LV_CLAMP(0, s_viewport_scroll_y, maximum), LV_ANIM_OFF);
        }
    }
    // Phase 11 edit end.
    s_viewport_resized = false;
    if (s_field != nullptr && s_field != s_viewport) {
        lv_obj_remove_event_cb(s_field, watched_object_deleted);
    }
    if (s_keyboard != nullptr) {
        lv_obj_t *keyboard = s_keyboard;
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_indev_reset(nullptr, keyboard);
        lv_obj_del(keyboard);
    }
    s_keyboard = nullptr;
    s_field = nullptr;
    s_viewport = nullptr;
    s_hiding = false;
    if (s_state_cb != nullptr) s_state_cb(false, s_state_user_data);
}

void crystal_keyboard_set_state_cb(crystal_keyboard_state_cb_t callback, void *user_data)
{
    s_state_cb = callback;
    s_state_user_data = user_data;
}

bool crystal_keyboard_is_open()
{
    return s_keyboard != nullptr;
}

lv_coord_t crystal_keyboard_top()
{
    return s_keyboard == nullptr ? lv_disp_get_ver_res(nullptr) : crystal_keyboard_reserved_top();
}

lv_coord_t crystal_keyboard_reserved_top()
{
    return static_cast<lv_coord_t>(lv_disp_get_ver_res(nullptr) - kKeyboardBandHeight);
}
