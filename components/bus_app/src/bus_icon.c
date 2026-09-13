/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

#include "lvgl.h"

// Procedural, like clock_icon.c: 64x64 RGB565 is 8 KB of BSS and no flash cost
// for image data. A double-decker in silhouette, because that is what the app is
// about and it reads at launcher size.

enum { ICON_SIZE = 64 };
static lv_color_t bus_icon_map[ICON_SIZE * ICON_SIZE];

static void fill_rect(int x0, int y0, int x1, int y1, lv_color_t color)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > ICON_SIZE - 1) x1 = ICON_SIZE - 1;
    if (y1 > ICON_SIZE - 1) y1 = ICON_SIZE - 1;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) bus_icon_map[y * ICON_SIZE + x] = color;
    }
}

void bus_icon_prepare(void)
{
    // Light background: the wheels are near-black and were lost against the dark
    // slate this used to use. Same value as the calculator's key colour so the
    // launcher row still reads as one set.
    const lv_color_t background = lv_color_make(230, 237, 245);
    const lv_color_t body = lv_color_make(83, 200, 182);
    // Windows are knocked out to the background, so they follow it.
    const lv_color_t glass = background;
    const lv_color_t wheel = lv_color_make(18, 28, 36);

    for (int i = 0; i < ICON_SIZE * ICON_SIZE; ++i) bus_icon_map[i] = background;

    // Body, with the corners knocked off so it reads as rounded rather than as a
    // rectangle that happens to be teal.
    fill_rect(12, 8, 51, 49, body);
    fill_rect(12, 8, 13, 9, background);
    fill_rect(50, 8, 51, 9, background);
    fill_rect(12, 48, 13, 49, background);
    fill_rect(50, 48, 51, 49, background);

    // Upper deck windows: two rows of three, which is the visual cue that says
    // double-decker at this size.
    for (int row = 0; row < 2; ++row) {
        const int y = 14 + row * 13;
        for (int column = 0; column < 3; ++column) {
            const int x = 17 + column * 11;
            fill_rect(x, y, x + 7, y + 8, glass);
        }
    }

    // Wheels, sitting proud of the body's lower edge.
    fill_rect(18, 50, 25, 55, wheel);
    fill_rect(38, 50, 45, 55, wheel);
}

const lv_img_dsc_t bus_icon = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,
        .always_zero = 0,
        .reserved = 0,
        .w = ICON_SIZE,
        .h = ICON_SIZE,
    },
    .data_size = sizeof(bus_icon_map),
    .data = (const uint8_t *)bus_icon_map,
};
