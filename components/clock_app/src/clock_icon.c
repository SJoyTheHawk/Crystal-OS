/* SPDX-License-Identifier: MIT */

#include "lvgl.h"

enum { ICON_SIZE = 64 };
static lv_color_t clock_icon_map[ICON_SIZE * ICON_SIZE];

void clock_icon_prepare(void)
{
    const lv_color_t background = lv_color_make(28, 45, 58);
    const lv_color_t face = lv_color_make(52, 184, 166);
    const lv_color_t hand = lv_color_make(255, 255, 255);
    for (int y = 0; y < ICON_SIZE; ++y) {
        for (int x = 0; x < ICON_SIZE; ++x) {
            const int dx = x - 32;
            const int dy = y - 32;
            const int r2 = dx * dx + dy * dy;
            clock_icon_map[y * ICON_SIZE + x] = (r2 >= 23 * 23 && r2 <= 27 * 27) ? face : background;
        }
    }
    for (int y = 17; y <= 33; ++y) {
        for (int x = 30; x <= 34; ++x) clock_icon_map[y * ICON_SIZE + x] = hand;
    }
    for (int y = 30; y <= 34; ++y) {
        for (int x = 32; x <= 46; ++x) clock_icon_map[y * ICON_SIZE + x] = hand;
    }
}

const lv_img_dsc_t clock_icon = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,
        .always_zero = 0,
        .reserved = 0,
        .w = ICON_SIZE,
        .h = ICON_SIZE,
    },
    .data_size = sizeof(clock_icon_map),
    .data = (const uint8_t *)clock_icon_map,
};
