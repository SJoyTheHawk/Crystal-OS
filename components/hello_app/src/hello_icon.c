/*
 * SPDX-License-Identifier: MIT
 */

#include "lvgl.h"

// Text icon: blue tile with white "HELLO" / "WORLD" pixel lettering.
enum { ICON_SIZE = 64, GLYPH_SCALE = 2 };
static lv_color_t hello_icon_map[ICON_SIZE * ICON_SIZE];

static const uint8_t glyphs[2][5][7] = {
    {{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
     {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
     {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}}, // HELLO
    {{0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
     {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
     {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}}, // WORLD
};

void hello_icon_prepare(void)
{
    for (int i = 0; i < ICON_SIZE * ICON_SIZE; ++i) {
        hello_icon_map[i] = lv_color_make(25, 105, 170);
    }

    for (int line = 0; line < 2; ++line) {
        const int y0 = 13 + line * 22;
        for (int letter = 0; letter < 5; ++letter) {
            const int x0 = 3 + letter * 12;
            for (int y = 0; y < 7; ++y) {
                for (int bit = 0; bit < 5; ++bit) {
                    if (glyphs[line][letter][y] & (1u << (4 - bit))) {
                        for (int dy = 0; dy < GLYPH_SCALE; ++dy) {
                            for (int dx = 0; dx < GLYPH_SCALE; ++dx) {
                                const int px = x0 + bit * GLYPH_SCALE + dx;
                                const int py = y0 + y * GLYPH_SCALE + dy;
                                hello_icon_map[py * ICON_SIZE + px] = lv_color_make(255, 255, 255);
                            }
                        }
                    }
                }
            }
        }
    }
}

const lv_img_dsc_t hello_icon = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,
        .always_zero = 0,
        .reserved = 0,
        .w = ICON_SIZE,
        .h = ICON_SIZE,
    },
    .data_size = sizeof(hello_icon_map),
    .data = (const uint8_t *)hello_icon_map,
};
