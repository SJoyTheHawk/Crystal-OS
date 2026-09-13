/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

#include "lvgl.h"

// The route keypad: two fixed blocks over the 28-character route alphabet, with
// keys dimmed as the typed prefix narrows what can follow.
//
// Digits sit left in a phone pad, letters right in a 5-wide block, per design
// §2.2. Splitting them is what makes the thing readable as a number pad: a route
// is a number first and a suffix second, and 28 keys in one undifferentiated
// grid reads as an alphabet rather than as a keypad.
//
// All 18 letters are present, not the 10 the design sketch drew. Every one of
// them starts or ends a real route -- D, F, G, H, N, O, T and W are ~90 routes
// between them -- and a letter with no key is a route that cannot be typed.
//
// Not the shell keyboard overlay. Route names are 1-4 characters over a known
// alphabet, and a full QWERTY would cover half the screen for four keystrokes.
class BusKeypad {
public:
    using CharHandler = void (*)(void *context, char value);
    using PlainHandler = void (*)(void *context);

    void build(lv_obj_t *parent, void *context, CharHandler on_char,
               PlainHandler on_backspace, PlainHandler on_submit);

    // Builds the backspace and submit keys into `parent`, which is the display
    // row rather than the character grid: they act on the whole query, not on one
    // character, and mixing them into the grid would cost two letter positions.
    lv_obj_t *buildBackspace(lv_obj_t *parent);
    lv_obj_t *buildSubmit(lv_obj_t *parent);

    // Applies the advisory mask from the route index. `mask` is over
    // transit_route_charset() positions; `complete` lights the submit key.
    void applyMask(uint32_t mask, bool complete, bool any_typed);

    lv_obj_t *root() const { return root_; }

    // Total size of the two blocks, so the caller can align the keypad without
    // duplicating the key arithmetic.
    static lv_coord_t width();
    static lv_coord_t height();

    void forget();

private:
    // 10 digits in a fixed phone pad of 3 columns. The 18 letters live in a
    // horizontally scrollable strip beside it, following the reference web app:
    // a route is a number first, and 18 letter keys competing with the digits for
    // the same screen made both small.
    static constexpr size_t kDigitColumns = 3;
    static constexpr size_t kRows = 4;
    static constexpr size_t kDigitCount = 10;
    static constexpr size_t kKeyCount = 28;
    static constexpr size_t kLetterCount = kKeyCount - kDigitCount;
    // The strip is the same 3 columns as the digit pad, so the two blocks are the
    // same width and the keypad stays symmetrical. 18 letters over 3 columns is
    // 6 rows, of which 4 are in view.
    static constexpr size_t kLetterColumns = 3;

    static lv_coord_t blockWidth();

    lv_obj_t *root_ = nullptr;
    lv_obj_t *letters_ = nullptr;   // the scrollable strip
    lv_obj_t *keys_[kKeyCount] = {};
    lv_obj_t *submit_ = nullptr;
    lv_obj_t *backspace_ = nullptr;
    void *context_ = nullptr;
    // lv_tick when the strip last moved. Guards against a tap that was catching a
    // scroll being read as a keystroke.
    uint32_t moved_at_ = 0;
    CharHandler on_char_ = nullptr;
    PlainHandler on_backspace_ = nullptr;
    PlainHandler on_submit_ = nullptr;

    friend struct BusKeypadEvents;
};
