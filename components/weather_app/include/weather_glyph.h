#pragma once

#include "lvgl.h"
#include "weather_codes.h"

#ifdef __cplusplus
extern "C" {
#endif

// Creates a transparent, square glyph of `size` px built from LVGL shapes.
// Each call returns an independent object, so several glyphs can show different
// conditions at once -- unlike the shared-bitmap version this replaced.
lv_obj_t *weather_glyph_create(lv_obj_t *parent, lv_coord_t size);

// Repoints an existing glyph at a WMO code. Grouping comes from
// weather_group_for_code(), the same table the condition label reads, so the
// picture and the words cannot disagree.
void weather_glyph_set_code(lv_obj_t *glyph, uint8_t code);

#ifdef __cplusplus
}
#endif
