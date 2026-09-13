/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

#include "bus_keypad.hpp"

#include <string.h>

#include "transit_routes.h"

namespace {
// Two blocks: a phone pad of digits, and the letters 5 across. Both are four
// rows, so they share a baseline and the keypad reads as one control.
//
// Within each block the order is the charset's own -- ascending digits, then
// alphabetical letters. A frequency-sorted or QWERTY-like arrangement would make
// the position of a key unlearnable, and position is the only thing a user can
// rely on once keys start dimming.
constexpr lv_coord_t kKeyWidth = 46;
constexpr lv_coord_t kKeyHeight = 44;
constexpr lv_coord_t kGap = 8;
// Between the two blocks. Wider than the key gap, so the split is legible as a
// split rather than as one grid with an odd column.
constexpr lv_coord_t kBlockGap = 20;
// How long after the strip last moved a tap is still treated as aimed at the
// strip. Covers the scroll-throw decay, which LVGL runs off its own timer.
constexpr uint32_t kSettleMs = 220;

// Digits in phone order with 0 centred on the fourth row. The two cells beside
// it stay empty: reserve space, never fill it.
constexpr int8_t kDigitSlots[12] = {
    1, 2, 3,
    4, 5, 6,
    7, 8, 9,
   -1, 0, -1,
};

constexpr uint32_t kKeyFill = 0x273541;
constexpr uint32_t kKeyPressed = 0x344553;
constexpr uint32_t kKeyText = 0xE6EDF5;
constexpr uint32_t kAccent = 0x53C8B6;
constexpr uint32_t kPage = 0x11181F;

lv_obj_t *makeKey(lv_obj_t *parent, const char *text, const lv_font_t *font,
                  lv_event_cb_t handler, void *user_data, lv_coord_t width = kKeyWidth)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, width, kKeyHeight);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(kKeyFill), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(kKeyPressed), LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, handler, LV_EVENT_CLICKED, user_data);

    // The label is centred in the key rather than placed at an offset. This is
    // what stops glyphs from crowding an edge, and it holds at every key size.
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(kKeyText), 0);
    lv_obj_center(label);
    return button;
}
}  // namespace

struct BusKeypadEvents {
    // Marks the strip as moving under a finger. LVGL already withholds CLICKED
    // when a press turned into a scroll, but it does not do so for the press that
    // catches a throw still in flight: that press scrolls nothing itself, so
    // scroll_obj is NULL at release and the key types. Catching a moving strip is
    // how anyone stops it, and it must not enter a character.
    static void scrollStarted(lv_event_t *event)
    {
        auto *keypad = static_cast<BusKeypad *>(lv_event_get_user_data(event));
        if (keypad != nullptr) keypad->moved_at_ = lv_tick_get();
    }

    static void character(lv_event_t *event)
    {
        auto *keypad = static_cast<BusKeypad *>(lv_event_get_user_data(event));
        if (keypad == nullptr || keypad->on_char_ == nullptr) return;

        // A press that landed while the strip was still moving, or within a beat
        // of it stopping, was aimed at the strip and not at this key.
        if (keypad->moved_at_ != 0 && lv_tick_elaps(keypad->moved_at_) < kSettleMs) {
            keypad->moved_at_ = lv_tick_get();   // still settling; keep the guard up
            return;
        }

        // The character rides in the button's user data, so there is no lookup
        // from label text and no parallel array to keep in step.
        const char value = static_cast<char>(
            reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target(event))));
        keypad->on_char_(keypad->context_, value);
    }

    static void backspace(lv_event_t *event)
    {
        auto *keypad = static_cast<BusKeypad *>(lv_event_get_user_data(event));
        if (keypad != nullptr && keypad->on_backspace_ != nullptr) {
            keypad->on_backspace_(keypad->context_);
        }
    }

    static void submit(lv_event_t *event)
    {
        auto *keypad = static_cast<BusKeypad *>(lv_event_get_user_data(event));
        if (keypad != nullptr && keypad->on_submit_ != nullptr) {
            keypad->on_submit_(keypad->context_);
        }
    }
};

void BusKeypad::build(lv_obj_t *parent, void *context, CharHandler on_char,
                      PlainHandler on_backspace, PlainHandler on_submit)
{
    context_ = context;
    on_char_ = on_char;
    on_backspace_ = on_backspace;
    on_submit_ = on_submit;

    const char *charset = transit_route_charset();
    const size_t available = strlen(charset);

    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_style_bg_opa(root_, LV_OPA_TRANSP, 0);
    lv_obj_set_size(root_, width(), height());
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    // Absolute positions inside each block, not flex. A wrapping flex row cannot
    // hold two blocks of different column counts, and the digit pad needs an
    // empty cell either side of 0 that flex would collapse.
    for (size_t slot = 0; slot < 12; ++slot) {
        const int8_t digit = kDigitSlots[slot];
        if (digit < 0) continue;   // the two blanks beside 0
        const size_t index = static_cast<size_t>(digit);
        if (index >= available) continue;

        const char text[2] = {charset[index], '\0'};
        keys_[index] = makeKey(root_, text, &lv_font_montserrat_20,
                               BusKeypadEvents::character, this);
        lv_obj_set_pos(keys_[index],
                       static_cast<lv_coord_t>((slot % kDigitColumns) * (kKeyWidth + kGap)),
                       static_cast<lv_coord_t>((slot / kDigitColumns) * (kKeyHeight + kGap)));
        lv_obj_set_user_data(keys_[index],
                             reinterpret_cast<void *>(static_cast<uintptr_t>(charset[index])));
    }

    // The letter strip. Scrolls vertically, never horizontally: the shell takes a
    // horizontal drag near either edge as an app switch, so a sideways strip would
    // lose half its scrolls to it.
    letters_ = lv_obj_create(root_);
    lv_obj_remove_style_all(letters_);
    lv_obj_set_style_bg_opa(letters_, LV_OPA_TRANSP, 0);
    lv_obj_set_size(letters_, blockWidth(), height());
    lv_obj_set_pos(letters_, static_cast<lv_coord_t>(blockWidth() + kBlockGap), 0);
    lv_obj_set_style_pad_all(letters_, 0, 0);
    lv_obj_set_scroll_dir(letters_, LV_DIR_VER);
    // Snap to a row, so the strip never rests with a half key at the top edge.
    lv_obj_set_scroll_snap_y(letters_, LV_SCROLL_SNAP_START);
    lv_obj_set_style_pad_row(letters_, kGap, 0);
    lv_obj_set_scrollbar_mode(letters_, LV_SCROLLBAR_MODE_ACTIVE);
    // SCROLL fires for every pixel of movement, including the throw's decay, so
    // the guard's timestamp stays fresh until the strip is genuinely still.
    lv_obj_add_event_cb(letters_, BusKeypadEvents::scrollStarted, LV_EVENT_SCROLL_BEGIN, this);
    lv_obj_add_event_cb(letters_, BusKeypadEvents::scrollStarted, LV_EVENT_SCROLL, this);

    for (size_t i = kDigitCount; i < kKeyCount && i < available; ++i) {
        const size_t slot = i - kDigitCount;
        const char text[2] = {charset[i], '\0'};
        keys_[i] = makeKey(letters_, text, &lv_font_montserrat_20,
                           BusKeypadEvents::character, this);
        lv_obj_set_pos(keys_[i],
                       static_cast<lv_coord_t>((slot % kLetterColumns) * (kKeyWidth + kGap)),
                       static_cast<lv_coord_t>((slot / kLetterColumns) *
                           (kKeyHeight + kGap)));
        lv_obj_set_user_data(keys_[i],
                             reinterpret_cast<void *>(static_cast<uintptr_t>(charset[i])));
    }
}

lv_coord_t BusKeypad::blockWidth()
{
    return static_cast<lv_coord_t>(kDigitColumns * kKeyWidth + (kDigitColumns - 1) * kGap);
}

lv_coord_t BusKeypad::width()
{
    return static_cast<lv_coord_t>(2 * blockWidth() + kBlockGap);
}

lv_coord_t BusKeypad::height()
{
    return static_cast<lv_coord_t>(kRows * kKeyHeight + (kRows - 1) * kGap);
}

lv_obj_t *BusKeypad::buildBackspace(lv_obj_t *parent)
{
    backspace_ = makeKey(parent, LV_SYMBOL_BACKSPACE, &lv_font_montserrat_20,
                         BusKeypadEvents::backspace, this);
    return backspace_;
}

lv_obj_t *BusKeypad::buildSubmit(lv_obj_t *parent)
{
    submit_ = makeKey(parent, LV_SYMBOL_OK, &lv_font_montserrat_20,
                      BusKeypadEvents::submit, this);
    return submit_;
}

void BusKeypad::applyMask(uint32_t mask, bool complete, bool any_typed)
{
    for (size_t i = 0; i < kKeyCount; ++i) {
        if (keys_[i] == nullptr) continue;
        // With nothing typed every key is live: the empty-prefix case is the one
        // the index does not need to answer.
        const bool live = !any_typed || (mask & (1u << i)) != 0;

        // Dimmed, never disabled. The index is a cache of someone else's data and
        // can be behind; if a greyed key refused the tap, a device that had never
        // refreshed could not reach a route that exists. Grey means "we don't
        // think so", never "you can't".
        lv_obj_set_style_opa(keys_[i], live ? LV_OPA_COVER : LV_OPA_40, 0);
    }

    // Bring the first live letter into view. The strip does not reorder -- position
    // stays learnable -- but a live 'X' six rows down is invisible, which is the
    // one thing a fixed grid had over a strip. Only when the user is not touching
    // it: taking the scroll away mid-drag is worse than a key out of sight.
    if (letters_ != nullptr && any_typed &&
        (moved_at_ == 0 || lv_tick_elaps(moved_at_) >= kSettleMs)) {
        for (size_t i = kDigitCount; i < kKeyCount; ++i) {
            if (keys_[i] == nullptr || (mask & (1u << i)) == 0) continue;
            lv_obj_scroll_to_view(keys_[i], LV_ANIM_ON);
            break;
        }
    }

    if (submit_ != nullptr) {
        // Submit lights only on a complete name. Letters can stay live at the
        // same time -- 2 is a real route and so is 2A -- so complete is not
        // the same as finished.
        lv_obj_set_style_bg_color(submit_, lv_color_hex(complete ? kAccent : kKeyFill), 0);
        lv_obj_t *label = lv_obj_get_child(submit_, 0);
        if (label != nullptr) {
            lv_obj_set_style_text_color(label, lv_color_hex(complete ? kPage : kKeyText), 0);
        }
    }
}

void BusKeypad::forget()
{
    root_ = letters_ = submit_ = backspace_ = nullptr;
    moved_at_ = 0;
    for (size_t i = 0; i < kKeyCount; ++i) keys_[i] = nullptr;
}
