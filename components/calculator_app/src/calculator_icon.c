/* SPDX-License-Identifier: MIT */

#include "lvgl.h"

enum { ICON_SIZE = 64 };
static lv_color_t calculator_icon_map[ICON_SIZE * ICON_SIZE];

static int inside_rounded_rect(int x, int y, int x0, int y0, int x1, int y1, int radius)
{
    if (x < x0 || x > x1 || y < y0 || y > y1) return 0;
    int cx = x < x0 + radius ? x0 + radius : (x > x1 - radius ? x1 - radius : x);
    int cy = y < y0 + radius ? y0 + radius : (y > y1 - radius ? y1 - radius : y);
    const int dx = x - cx;
    const int dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

void calculator_icon_prepare(void)
{
    const lv_color_t background = lv_color_hex(0x16212B);
    const lv_color_t body = lv_color_hex(0x273541);
    const lv_color_t display = lv_color_hex(0x53C8B6);
    const lv_color_t key = lv_color_hex(0xE6EDF5);
    const lv_color_t operator_key = lv_color_hex(0xFFC857);

    for (int y = 0; y < ICON_SIZE; ++y) {
        for (int x = 0; x < ICON_SIZE; ++x) {
            lv_color_t color = background;
            if (inside_rounded_rect(x, y, 10, 5, 53, 58, 7)) color = body;
            if (inside_rounded_rect(x, y, 16, 11, 47, 24, 3)) color = display;

            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 3; ++col) {
                    const int cx = 19 + col * 13;
                    const int cy = 34 + row * 9;
                    const int dx = x - cx;
                    const int dy = y - cy;
                    if (dx * dx + dy * dy <= 3 * 3) {
                        color = col == 2 ? operator_key : key;
                    }
                }
            }
            calculator_icon_map[y * ICON_SIZE + x] = color;
        }
    }
}

const lv_img_dsc_t calculator_icon = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,
        .always_zero = 0,
        .reserved = 0,
        .w = ICON_SIZE,
        .h = ICON_SIZE,
    },
    .data_size = sizeof(calculator_icon_map),
    .data = (const uint8_t *)calculator_icon_map,
};
