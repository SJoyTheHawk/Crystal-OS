// A weather glyph drawn from LVGL primitives instead of a hand-plotted bitmap.
//
// The bitmap version had three problems this one does not. It carried its own
// opaque background, so it read as a sticker pasted onto the card rather than
// artwork on it. Its edges were whatever a per-pixel circle test produced, with
// no anti-aliasing, which is what made the result look sketched. And it owned a
// single process-wide buffer, so two glyphs on screen at once could only ever
// show the same condition.
//
// Everything here is a rounded rectangle or a circle -- flat shapes, the same
// vocabulary the rest of the shell uses. LVGL anti-aliases their edges for free.

#include "weather_glyph.h"

// Shapes are authored against a 96 px box and scaled to the caller's size, so
// the proportions hold at any dimension without a second set of constants.
enum { AUTHORED = 96 };

#define S(v) ((lv_coord_t)((v) * size / AUTHORED))

typedef struct {
    // Kept here rather than read back with lv_obj_get_width(): coordinates are
    // only valid after a layout pass, and set_code() runs during onCreate,
    // before one has happened. Reading the width there yields 0 and every
    // scaled dimension collapses to nothing.
    lv_coord_t size;
    lv_obj_t *sun;
    lv_obj_t *cloud_body;
    lv_obj_t *cloud_left;
    lv_obj_t *cloud_right;
    lv_obj_t *drop[3];
    lv_obj_t *bolt;
    lv_obj_t *fog[2];
} GlyphParts;

static lv_obj_t *blank(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, lv_color_t fill,
                       lv_coord_t radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, fill, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

// The parts table is heap-allocated, and the glyph can be deleted by its parent
// being deleted -- which the app never sees. Free it from the delete event so
// the allocation cannot outlive the object.
static void free_parts(lv_event_t *e)
{
    GlyphParts *p = lv_event_get_user_data(e);
    if (p != NULL) lv_mem_free(p);
}

lv_obj_t *weather_glyph_create(lv_obj_t *parent, lv_coord_t size)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, size, size);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    GlyphParts *p = lv_mem_alloc(sizeof(GlyphParts));
    if (p == NULL) return root;
    lv_memset_00(p, sizeof(*p));
    p->size = size;

    const lv_color_t sun = lv_color_hex(0xFFC24B);
    const lv_color_t cloud = lv_color_hex(0xC9D6E2);
    const lv_color_t rain = lv_color_hex(0x4FB0F0);

    // Sun. Drawn first so the cloud overlaps it in the partly-cloudy case.
    p->sun = blank(root, S(38), S(38), sun, LV_RADIUS_CIRCLE);

    // Cloud: one rounded slab plus two circles for the puffs. Cheaper and
    // rounder than trying to describe the silhouette as a single shape.
    p->cloud_left = blank(root, S(34), S(34), cloud, LV_RADIUS_CIRCLE);
    p->cloud_right = blank(root, S(26), S(26), cloud, LV_RADIUS_CIRCLE);
    p->cloud_body = blank(root, S(58), S(20), cloud, S(10));

    for (int i = 0; i < 3; ++i) p->drop[i] = blank(root, S(5), S(14), rain, S(3));
    p->bolt = blank(root, S(9), S(22), sun, S(2));
    for (int i = 0; i < 2; ++i) p->fog[i] = blank(root, S(46), S(6), cloud, S(3));

    lv_obj_set_user_data(root, p);
    lv_obj_add_event_cb(root, free_parts, LV_EVENT_DELETE, p);
    return root;
}

void weather_glyph_set_code(lv_obj_t *glyph, uint8_t code)
{
    if (glyph == NULL) return;
    GlyphParts *p = lv_obj_get_user_data(glyph);
    if (p == NULL) return;

    const lv_coord_t size = p->size;
    const WeatherGlyph g = weather_group_for_code(code).glyph;

    const bool has_sun = g == WEATHER_GLYPH_SUN || g == WEATHER_GLYPH_SUN_CLOUD;
    const bool alone = g == WEATHER_GLYPH_SUN;
    const bool fog = g == WEATHER_GLYPH_FOG;
    const bool storm = g == WEATHER_GLYPH_STORM;
    const bool snow = g == WEATHER_GLYPH_SNOW;
    const bool drizzle = g == WEATHER_GLYPH_DRIZZLE;
    const bool rain = g == WEATHER_GLYPH_RAIN;
    const bool wet = drizzle || rain || snow;
    const bool has_cloud = g == WEATHER_GLYPH_SUN_CLOUD || g == WEATHER_GLYPH_CLOUD ||
                           fog || wet || storm;

    // A lone sun sits centred. Behind a cloud it moves up and right so the disc
    // reads as peeking out rather than as a circle stuck to the cloud's edge.
    if (has_sun) {
        lv_obj_align(p->sun, alone ? LV_ALIGN_CENTER : LV_ALIGN_TOP_RIGHT,
                     0, alone ? S(-4) : S(6));
    }
    lv_obj_set_style_opa(p->sun, has_sun ? LV_OPA_COVER : LV_OPA_TRANSP, 0);

    // Fog gets a bare cloud plus two bars under it, so it never looks like rain.
    const lv_coord_t cloud_y = wet || storm ? S(-14) : (fog ? S(-16) : S(2));
    lv_obj_align(p->cloud_body, LV_ALIGN_CENTER, S(-2), cloud_y + S(8));
    lv_obj_align(p->cloud_left, LV_ALIGN_CENTER, S(-12), cloud_y);
    lv_obj_align(p->cloud_right, LV_ALIGN_CENTER, S(12), cloud_y + S(2));
    const lv_opa_t cloud_opa = has_cloud ? LV_OPA_COVER : LV_OPA_TRANSP;
    lv_obj_set_style_opa(p->cloud_body, cloud_opa, 0);
    lv_obj_set_style_opa(p->cloud_left, cloud_opa, 0);
    lv_obj_set_style_opa(p->cloud_right, cloud_opa, 0);

    // Snow reuses the drop objects as short dots in white; drizzle uses two
    // instead of three, so light and heavy precipitation are distinguishable.
    const lv_color_t mark = snow ? lv_color_hex(0xF2F7FF) : lv_color_hex(0x4FB0F0);
    const int marks = storm ? 0 : (drizzle ? 2 : (wet ? 3 : 0));
    for (int i = 0; i < 3; ++i) {
        const bool on = i < marks;
        lv_obj_set_style_opa(p->drop[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        if (!on) continue;
        lv_obj_set_style_bg_color(p->drop[i], mark, 0);
        lv_obj_set_size(p->drop[i], snow ? S(6) : S(5), snow ? S(6) : S(14));
        lv_obj_set_style_radius(p->drop[i], LV_RADIUS_CIRCLE, 0);
        const lv_coord_t slot = marks == 2 ? S(11) : S(15);
        const lv_coord_t x = (lv_coord_t)((i - (marks - 1) / 2) * slot) - (marks == 2 ? S(5) : 0);
        lv_obj_align(p->drop[i], LV_ALIGN_CENTER, x, S(24) + (i % 2 ? S(3) : 0));
    }

    lv_obj_set_style_opa(p->bolt, storm ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    if (storm) lv_obj_align(p->bolt, LV_ALIGN_CENTER, 0, S(22));

    for (int i = 0; i < 2; ++i) {
        lv_obj_set_style_opa(p->fog[i], fog ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        if (fog) lv_obj_align(p->fog[i], LV_ALIGN_CENTER, i ? S(4) : S(-4), S(20) + (lv_coord_t)(i * S(12)));
    }
}
