/* SPDX-License-Identifier: MIT */

// Launcher icon: sun behind a cloud, on a dusk-blue tile.
//
// Drawn per-pixel because the launcher wants an lv_img_dsc_t, not an object
// tree -- unlike the in-app glyph, which is LVGL primitives (weather_glyph.c).
// Every shape is sampled 4x4 per pixel and blended by coverage, so the curves
// come out smooth instead of stair-stepped. The one-off cost is paid once in
// weather_icon_prepare(); nothing here runs per frame.

#include "lvgl.h"

enum { ICON_SIZE = 64, SUB = 4 };

static lv_color_t weather_icon_map[ICON_SIZE * ICON_SIZE];

// Coverage of a disc over one pixel, 0..SUB*SUB.
static int disc_coverage(int px, int py, float cx, float cy, float r)
{
    const float r2 = r * r;
    int hits = 0;
    for (int sy = 0; sy < SUB; ++sy) {
        for (int sx = 0; sx < SUB; ++sx) {
            const float x = (float)px + ((float)sx + 0.5f) / SUB - cx;
            const float y = (float)py + ((float)sy + 0.5f) / SUB - cy;
            if (x * x + y * y <= r2) ++hits;
        }
    }
    return hits;
}

// Coverage of an axis-aligned rounded rectangle over one pixel.
static int rrect_coverage(int px, int py, float x0, float y0, float x1, float y1, float r)
{
    int hits = 0;
    for (int sy = 0; sy < SUB; ++sy) {
        for (int sx = 0; sx < SUB; ++sx) {
            const float x = (float)px + ((float)sx + 0.5f) / SUB;
            const float y = (float)py + ((float)sy + 0.5f) / SUB;
            if (x < x0 || x > x1 || y < y0 || y > y1) continue;
            // Push the sample toward the nearest corner centre; outside the
            // corner arc it is outside the shape.
            const float cx = x < x0 + r ? x0 + r : (x > x1 - r ? x1 - r : x);
            const float cy = y < y0 + r ? y0 + r : (y > y1 - r ? y1 - r : y);
            const float dx = x - cx;
            const float dy = y - cy;
            if (dx * dx + dy * dy <= r * r) ++hits;
        }
    }
    return hits;
}

static void blend(int index, lv_color_t colour, int hits)
{
    if (hits <= 0) return;
    const lv_opa_t opa = (lv_opa_t)(hits * 255 / (SUB * SUB));
    weather_icon_map[index] = lv_color_mix(colour, weather_icon_map[index], opa);
}

void weather_icon_prepare(void)
{
    const lv_color_t sky_top = lv_color_hex(0x1B2A3A);
    const lv_color_t sky_bottom = lv_color_hex(0x2C4560);
    const lv_color_t sun = lv_color_hex(0xFFC24B);
    const lv_color_t cloud = lv_color_hex(0xEEF4FA);
    const lv_color_t cloud_shade = lv_color_hex(0xC4D3E2);

    // Vertical gradient background, so the tile has depth without a texture.
    for (int y = 0; y < ICON_SIZE; ++y) {
        const lv_opa_t t = (lv_opa_t)(y * 255 / (ICON_SIZE - 1));
        const lv_color_t row = lv_color_mix(sky_bottom, sky_top, t);
        for (int x = 0; x < ICON_SIZE; ++x) weather_icon_map[y * ICON_SIZE + x] = row;
    }

    // Sun: disc up and to the right, rays around it. Drawn first so the cloud
    // overlaps it -- that overlap is what reads as "weather" rather than "sun".
    const float sun_cx = 41.0f, sun_cy = 22.0f, sun_r = 11.0f;
    // Ray centres at 45 degree steps, radius 16 from the sun centre. Written
    // out rather than derived: the values are fixed, and 11.31 is 16*cos(45).
    // Only the five that clear the cloud are drawn -- a ray behind the cloud
    // is either invisible or, worse, a stray dot on the cloud's edge.
    const float ray[5][2] = {
        { 16.0f,   0.0f}, { 11.31f, -11.31f}, {  0.0f, -16.0f}, {-11.31f, -11.31f},
        { 11.31f,  11.31f},
    };
    for (int i = 0; i < 5; ++i) {
        const float rx = sun_cx + ray[i][0];
        const float ry = sun_cy + ray[i][1];
        for (int y = 0; y < ICON_SIZE; ++y) {
            for (int x = 0; x < ICON_SIZE; ++x) {
                blend(y * ICON_SIZE + x, sun, disc_coverage(x, y, rx, ry, 2.6f));
            }
        }
    }
    for (int y = 0; y < ICON_SIZE; ++y) {
        for (int x = 0; x < ICON_SIZE; ++x) {
            blend(y * ICON_SIZE + x, sun, disc_coverage(x, y, sun_cx, sun_cy, sun_r));
        }
    }

    // Cloud: three puffs over a rounded base, bottom-left, overlapping the sun.
    // The shaded underside is a second pass along the base's lower edge.
    for (int y = 0; y < ICON_SIZE; ++y) {
        for (int x = 0; x < ICON_SIZE; ++x) {
            const int index = y * ICON_SIZE + x;
            blend(index, cloud_shade, rrect_coverage(x, y, 8.0f, 42.0f, 50.0f, 53.0f, 5.5f));
            blend(index, cloud, disc_coverage(x, y, 21.0f, 36.0f, 11.0f));
            blend(index, cloud, disc_coverage(x, y, 34.0f, 39.0f, 9.0f));
            blend(index, cloud, disc_coverage(x, y, 13.0f, 42.0f, 7.5f));
            blend(index, cloud, rrect_coverage(x, y, 8.0f, 40.0f, 50.0f, 51.0f, 5.5f));
        }
    }
}

const lv_img_dsc_t weather_icon = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,
        .always_zero = 0,
        .reserved = 0,
        .w = ICON_SIZE,
        .h = ICON_SIZE,
    },
    .data_size = sizeof(weather_icon_map),
    .data = (const uint8_t *)weather_icon_map,
};
