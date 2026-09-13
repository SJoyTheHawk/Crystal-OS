/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

#include "lvgl.h"

enum { ICON_SIZE = 64 };
static lv_color_t dev_tester_icon_map[ICON_SIZE * ICON_SIZE];

void dev_tester_icon_prepare(void)
{
    const lv_color_t background = lv_color_hex(0x25282d);
    const lv_color_t keyboard = lv_color_hex(0xd1d3d9);
    const lv_color_t key = lv_color_hex(0xf7f7f8);
    const lv_color_t action = lv_color_hex(0x087bdb);
    for (int y = 0; y < ICON_SIZE; ++y) {
        for (int x = 0; x < ICON_SIZE; ++x) {
            lv_color_t color = background;
            if (x >= 5 && x <= 58 && y >= 15 && y <= 50) color = keyboard;
            if (y >= 20 && y <= 27 && x >= 9 && x <= 54 && ((x - 9) % 10) <= 6) color = key;
            if (y >= 31 && y <= 38 && x >= 13 && x <= 50 && ((x - 13) % 10) <= 6) color = key;
            if (y >= 42 && y <= 47 && x >= 17 && x <= 43) color = key;
            if (y >= 42 && y <= 47 && x >= 47 && x <= 55) color = action;
            dev_tester_icon_map[y * ICON_SIZE + x] = color;
        }
    }
}

const lv_img_dsc_t dev_tester_icon = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,
        .always_zero = 0,
        .reserved = 0,
        .w = ICON_SIZE,
        .h = ICON_SIZE,
    },
    .data_size = sizeof(dev_tester_icon_map),
    .data = (const uint8_t *)dev_tester_icon_map,
};
