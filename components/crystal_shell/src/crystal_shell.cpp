/* SPDX-License-Identifier: MIT */

#include "crystal_shell.hpp"

#include <stdlib.h>
#include <string.h>
#include <vector>

#include "crystal_app.hpp"
#include "crystal_core.hpp"
#include "crystal_hal.hpp"
#include "crystal_registry.hpp"
#include "esp_brookesia.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "crystal_shell";

namespace {
constexpr int kTopBand = 20;
// Crop the captured app area to 90% x 90% about its centre, then store it at
// 1/kSnapshotScaleDivisor of the app's resolution. On this panel the observed
// 480x440 app area produces a 240x220 snapshot (~103 KiB).
constexpr uint32_t kSnapshotCropPercent = 90;
constexpr uint32_t kSnapshotScaleDivisor = 2;
constexpr uint32_t kTransitionMs = 180;
constexpr uint32_t kCrossoverSettleMs = 250;
constexpr char kCurrentCardKey[] = "shell.card";

ESP_Brookesia_Phone *s_phone = nullptr;
size_t s_current_index = 0;
bool s_switching = false;
bool s_quick_settings_open = false;
bool s_keyboard_open = false;
bool s_settings_open = false;
bool s_modal_open = false;
bool s_swallow_wake_touch = false;
CrystalGestureOwner s_gesture_owner = CrystalGestureOwner::None;
ESP_Brookesia_Gesture *s_gesture = nullptr;
lv_obj_t *s_page_dots = nullptr;

struct CardPane {
    lv_img_dsc_t *image = nullptr;
};

enum class CardTransitionPhase {
    Idle,
    Dragging,
    Settling,
};

struct CardTransition {
    CardTransitionPhase phase = CardTransitionPhase::Idle;
    lv_obj_t *root = nullptr;
    lv_obj_t *incoming_card = nullptr;
    lv_obj_t *incoming_image = nullptr;
    lv_img_dsc_t *outgoing = nullptr;
    size_t original_index = SIZE_MAX;
    size_t target_index = SIZE_MAX;
    lv_coord_t width = 0;
    lv_coord_t progress = 0;
    int direction = 0;
    bool destination_live = false;
    bool commit = false;
};

std::vector<CardPane> s_pane_cache;
CardTransition s_card_transition;

bool start_card(size_t index, bool animate = true);

bool os_owns_gesture()
{
    return s_gesture_owner == CrystalGestureOwner::AppSwitch ||
           s_gesture_owner == CrystalGestureOwner::QuickSettings;
}

void update_page_dots()
{
    if (s_page_dots == nullptr) {
        return;
    }
    const size_t count = crystal_registry_installed_count();
    for (size_t i = 0; i < count; ++i) {
        lv_obj_t *dot = lv_obj_get_child(s_page_dots, static_cast<int32_t>(i));
        if (dot != nullptr) {
            lv_obj_set_style_bg_opa(dot, i == s_current_index ? LV_OPA_COVER : LV_OPA_40, 0);
        }
    }
}

bool init_indicator_overlay()
{
    lv_obj_t *logo = lv_label_create(lv_layer_top());
    if (logo == nullptr) {
        return false;
    }
    lv_label_set_text(logo, "C");
    lv_obj_set_style_text_font(logo, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(logo, lv_color_white(), 0);
    lv_obj_clear_flag(logo, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(logo, LV_ALIGN_TOP_LEFT, 8, 10);

    const size_t count = crystal_registry_installed_count();
    s_page_dots = lv_obj_create(lv_layer_top());
    if (s_page_dots == nullptr) {
        return false;
    }
    lv_obj_set_size(s_page_dots, static_cast<lv_coord_t>(count * 9), 7);
    lv_obj_set_style_bg_opa(s_page_dots, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_page_dots, 0, 0);
    lv_obj_set_style_pad_all(s_page_dots, 0, 0);
    lv_obj_set_style_pad_column(s_page_dots, 4, 0);
    lv_obj_set_flex_flow(s_page_dots, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(s_page_dots, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_page_dots, LV_ALIGN_TOP_MID, 0, 31);
    for (size_t i = 0; i < count; ++i) {
        lv_obj_t *dot = lv_obj_create(s_page_dots);
        if (dot == nullptr) {
            return false;
        }
        lv_obj_set_size(dot, 5, 5);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_white(), 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    update_page_dots();
    return true;
}

// Takes the clipping container returned by show_snapshot_transition(), frees the
// snapshot buffer its child image points at, then deletes the container.
void discard_snapshot(lv_obj_t *container)
{
    if (container == nullptr) {
        return;
    }
    lv_obj_t *image = lv_obj_get_child(container, 0);
    if (image != nullptr) {
        auto *snapshot = const_cast<lv_img_dsc_t *>(
            static_cast<const lv_img_dsc_t *>(lv_img_get_src(image)));
        if (snapshot != nullptr) {
            lv_img_buf_free(snapshot);
        }
    }
    lv_obj_del(container);
}

void transition_cleanup_cb(lv_timer_t *timer)
{
    discard_snapshot(static_cast<lv_obj_t *>(timer->user_data));
    lv_timer_del(timer);
}

// The app area excludes the status bar: Brookesia subtracts it when it calibrates
// each app's visual area at install time. Falls back to the full display only when
// no app is active, which should not happen on a switch.
lv_area_t active_app_area()
{
    if (s_phone != nullptr && s_phone->getManager().getActiveApp() != nullptr) {
        return s_phone->getManager().getActiveApp()->getVisualArea();
    }
    return lv_area_t{0, 0,
                     static_cast<lv_coord_t>(lv_disp_get_hor_res(nullptr) - 1),
                     static_cast<lv_coord_t>(lv_disp_get_ver_res(nullptr) - 1)};
}

lv_img_dsc_t *capture_app_area()
{
    lv_img_dsc_t *screen = lv_snapshot_take(lv_scr_act(), LV_IMG_CF_TRUE_COLOR);
    if (screen == nullptr || screen->header.w == 0 || screen->header.h == 0) {
        return nullptr;
    }

    const lv_area_t area = active_app_area();
    const lv_coord_t width = lv_area_get_width(&area);
    const lv_coord_t height = lv_area_get_height(&area);
    // Brookesia's active app screen is normally app-area sized, so its snapshot
    // uses local (0, 0) coordinates even though getVisualArea() is expressed in
    // display coordinates below the status bar. Keep supporting a full-display
    // snapshot as well, since LVGL screen sizing can vary by app configuration.
    const lv_coord_t source_x = screen->header.w == width ? 0 : area.x1;
    const lv_coord_t source_y = screen->header.h == height ? 0 : area.y1;
    if (width <= 0 || height <= 0 || source_x < 0 || source_y < 0 ||
            source_x + width > static_cast<lv_coord_t>(screen->header.w) ||
            source_y + height > static_cast<lv_coord_t>(screen->header.h)) {
        ESP_LOGE(TAG, "cannot crop app pane: source=%ux%u area=(%d,%d %dx%d)",
                 screen->header.w, screen->header.h, area.x1, area.y1, width, height);
        lv_snapshot_free(screen);
        return nullptr;
    }

    lv_img_dsc_t *pane = lv_img_buf_alloc(width, height, LV_IMG_CF_TRUE_COLOR);
    if (pane == nullptr) {
        lv_snapshot_free(screen);
        return nullptr;
    }

    const auto *source = reinterpret_cast<const lv_color_t *>(screen->data);
    auto *dest = reinterpret_cast<lv_color_t *>(const_cast<uint8_t *>(pane->data));
    for (lv_coord_t y = 0; y < height; ++y) {
        memcpy(dest + static_cast<size_t>(y) * width,
               source + static_cast<size_t>(source_y + y) * screen->header.w + source_x,
               static_cast<size_t>(width) * sizeof(lv_color_t));
    }
    lv_snapshot_free(screen);
    return pane;
}

void replace_cached_pane(size_t index, lv_img_dsc_t *image)
{
    if (index >= s_pane_cache.size()) {
        if (image != nullptr) {
            lv_img_buf_free(image);
        }
        return;
    }
    if (s_pane_cache[index].image != nullptr && s_pane_cache[index].image != image) {
        lv_img_buf_free(s_pane_cache[index].image);
    }
    s_pane_cache[index].image = image;
}

void prune_pane_cache(size_t current_index)
{
    for (size_t i = 0; i < s_pane_cache.size(); ++i) {
        const bool neighbour = (i + 1 == current_index) || (i == current_index + 1);
        if (!neighbour && s_pane_cache[i].image != nullptr) {
            lv_img_buf_free(s_pane_cache[i].image);
            s_pane_cache[i].image = nullptr;
        }
    }
}

// Box-averages `crop` out of `source` into a freshly allocated dest_w x dest_h
// image. Both images must be LV_IMG_CF_TRUE_COLOR. Returns nullptr on failure.
lv_img_dsc_t *downscale_crop(const lv_img_dsc_t *source, const lv_area_t &crop,
                             lv_coord_t dest_w, lv_coord_t dest_h)
{
    if (dest_w <= 0 || dest_h <= 0) {
        return nullptr;
    }
    lv_img_dsc_t *dest = lv_img_buf_alloc(dest_w, dest_h, LV_IMG_CF_TRUE_COLOR);
    if (dest == nullptr) {
        return nullptr;
    }

    const lv_coord_t crop_w = lv_area_get_width(&crop);
    const lv_coord_t crop_h = lv_area_get_height(&crop);
    const auto *source_px = reinterpret_cast<const lv_color_t *>(source->data);
    auto *dest_px = reinterpret_cast<lv_color_t *>(const_cast<uint8_t *>(dest->data));
    const lv_coord_t source_stride = source->header.w;

    for (lv_coord_t y = 0; y < dest_h; ++y) {
        // Source rows [row_begin, row_end) average into destination row y.
        const lv_coord_t row_begin = crop.y1 + (y * crop_h) / dest_h;
        const lv_coord_t row_end = LV_MAX(row_begin + 1, crop.y1 + ((y + 1) * crop_h) / dest_h);
        for (lv_coord_t x = 0; x < dest_w; ++x) {
            const lv_coord_t col_begin = crop.x1 + (x * crop_w) / dest_w;
            const lv_coord_t col_end = LV_MAX(col_begin + 1, crop.x1 + ((x + 1) * crop_w) / dest_w);
            uint32_t red = 0;
            uint32_t green = 0;
            uint32_t blue = 0;
            uint32_t count = 0;
            for (lv_coord_t sy = row_begin; sy < row_end; ++sy) {
                const lv_color_t *row = source_px + static_cast<size_t>(sy) * source_stride;
                for (lv_coord_t sx = col_begin; sx < col_end; ++sx) {
                    const lv_color_t c = row[sx];
                    red += LV_COLOR_GET_R(c);
                    green += LV_COLOR_GET_G(c);
                    blue += LV_COLOR_GET_B(c);
                    ++count;
                }
            }
            lv_color_t out;
            LV_COLOR_SET_R(out, red / count);
            LV_COLOR_SET_G(out, green / count);
            LV_COLOR_SET_B(out, blue / count);
            dest_px[static_cast<size_t>(y) * dest_w + x] = out;
        }
    }
    return dest;
}

// Covers the app area (never the status bar) with a magnified copy of what the app
// area currently shows. Returns the clipping container, which owns the image;
// pass it to finish_snapshot_transition() or discard_snapshot().
lv_obj_t *show_snapshot_transition()
{
    lv_img_dsc_t *source = lv_snapshot_take(lv_scr_act(), LV_IMG_CF_TRUE_COLOR);
    if (source == nullptr || source->header.w == 0 || source->header.h == 0) {
        return nullptr;
    }

    const lv_area_t app_area = active_app_area();
    const lv_coord_t app_w = lv_area_get_width(&app_area);
    const lv_coord_t app_h = lv_area_get_height(&app_area);

    // Crop 90% about the centre of the app area, clamped to the captured screen so
    // an unexpected visual area cannot read outside the source buffer.
    const lv_coord_t crop_w = static_cast<lv_coord_t>((app_w * kSnapshotCropPercent) / 100);
    const lv_coord_t crop_h = static_cast<lv_coord_t>((app_h * kSnapshotCropPercent) / 100);
    lv_area_t crop = {
        static_cast<lv_coord_t>(app_area.x1 + (app_w - crop_w) / 2),
        static_cast<lv_coord_t>(app_area.y1 + (app_h - crop_h) / 2),
        0, 0,
    };
    crop.x1 = LV_CLAMP(0, crop.x1, static_cast<lv_coord_t>(source->header.w - 1));
    crop.y1 = LV_CLAMP(0, crop.y1, static_cast<lv_coord_t>(source->header.h - 1));
    crop.x2 = LV_MIN(static_cast<lv_coord_t>(crop.x1 + crop_w - 1),
                     static_cast<lv_coord_t>(source->header.w - 1));
    crop.y2 = LV_MIN(static_cast<lv_coord_t>(crop.y1 + crop_h - 1),
                     static_cast<lv_coord_t>(source->header.h - 1));

    // Half the app resolution per axis, so the stored image keeps the app's aspect
    // ratio and one zoom factor serves both axes.
    const lv_coord_t small_w = LV_MAX(1, static_cast<lv_coord_t>(app_w / kSnapshotScaleDivisor));
    const lv_coord_t small_h = LV_MAX(1, static_cast<lv_coord_t>(app_h / kSnapshotScaleDivisor));
    lv_img_dsc_t *small = downscale_crop(source, crop, small_w, small_h);
    lv_snapshot_free(source);
    if (small == nullptr) {
        return nullptr;
    }

    // Container clipped to the app area: whatever the zoom rounds to, nothing can
    // paint over the status bar.
    lv_obj_t *container = lv_obj_create(lv_layer_top());
    if (container == nullptr) {
        lv_img_buf_free(small);
        return nullptr;
    }
    lv_obj_set_size(container, app_w, app_h);
    lv_obj_set_pos(container, app_area.x1, app_area.y1);
    lv_obj_set_style_radius(container, 0, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *image = lv_img_create(container);
    if (image == nullptr) {
        lv_obj_del(container);
        lv_img_buf_free(small);
        return nullptr;
    }
    lv_img_set_src(image, small);
    lv_img_set_size_mode(image, LV_IMG_SIZE_MODE_REAL);
    // Round the zoom up so the magnified image covers the app area rather than
    // leaving a seam; the container clips the overshoot.
    const uint32_t zoom_x = (static_cast<uint32_t>(app_w) * 256u + small_w - 1) / small_w;
    const uint32_t zoom_y = (static_cast<uint32_t>(app_h) * 256u + small_h - 1) / small_h;
    lv_img_set_zoom(image, static_cast<uint16_t>(LV_MAX(zoom_x, zoom_y)));
    lv_obj_center(image);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);

    ESP_LOGI(TAG, "snapshot transition: app %dx%d at (%d,%d) crop=%u%% (%dx%d) "
             "-> %dx%d, zoom=%u/256, %u ms",
             app_w, app_h, app_area.x1, app_area.y1,
             static_cast<unsigned>(kSnapshotCropPercent),
             lv_area_get_width(&crop), lv_area_get_height(&crop),
             small_w, small_h,
             static_cast<unsigned>(LV_MAX(zoom_x, zoom_y)),
             static_cast<unsigned>(kTransitionMs));
    return container;
}

lv_obj_t *show_transition_cover()
{
    lv_obj_t *cover = lv_obj_create(lv_layer_top());
    if (cover == nullptr) {
        return nullptr;
    }
    lv_obj_set_size(cover, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
    lv_obj_set_pos(cover, 0, 0);
    lv_obj_set_style_radius(cover, 0, 0);
    lv_obj_set_style_border_width(cover, 0, 0);
    lv_obj_set_style_pad_all(cover, 0, 0);
    lv_obj_set_style_bg_color(cover, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, 0);
    lv_obj_clear_flag(cover, LV_OBJ_FLAG_CLICKABLE);
    return cover;
}

// Opacity cascades to children in LVGL 8, so fading the container fades the image.
void finish_snapshot_transition(lv_obj_t *container)
{
    if (container == nullptr) {
        return;
    }
    lv_obj_fade_out(container, kTransitionMs, 0);
    lv_timer_create(transition_cleanup_cb, kTransitionMs + 20, container);
}

bool persist_current_card()
{
    const char *id = crystal_registry_installed_id(s_current_index);
    if (id == nullptr || hal().storage == nullptr) {
        return false;
    }
    return hal().storage->set(kCurrentCardKey, id, strlen(id) + 1);
}

size_t load_current_index()
{
    char stored_id[32] = {};
    size_t length = sizeof(stored_id);
    if (hal().storage == nullptr ||
            !hal().storage->get(kCurrentCardKey, stored_id, &length) ||
            length == 0 || length > sizeof(stored_id) || stored_id[length - 1] != '\0') {
        return 0;
    }

    for (size_t i = 0; i < crystal_registry_installed_count(); ++i) {
        const char *id = crystal_registry_installed_id(i);
        if (id != nullptr && strcmp(id, stored_id) == 0) {
            return i;
        }
    }
    return 0;
}

bool start_card(size_t index, bool animate)
{
    CrystalApp *app = crystal_registry_installed_app(index);
    if (s_phone == nullptr || app == nullptr || index >= crystal_registry_installed_count()) {
        return false;
    }

    lv_obj_t *cover = nullptr;
    if (animate && s_phone->getManager().getActiveApp() != nullptr &&
            s_phone->getManager().getActiveApp() != app) {
        cover = show_transition_cover();
    }

    ESP_Brookesia_CoreAppEventData_t event_data = {
        .id = app->getId(),
        .type = ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START,
        .data = nullptr,
    };
    s_switching = true;
    const bool sent = s_phone->sendAppEvent(&event_data);
    s_switching = false;
    if (!sent || s_phone->getManager().getActiveApp() != app) {
        if (cover != nullptr) {
            lv_obj_del(cover);
        }
        ESP_LOGE(TAG, "failed to open card %u (%s)", static_cast<unsigned>(index),
                 crystal_registry_installed_id(index));
        return false;
    }

    lv_obj_t *transition = animate ? show_snapshot_transition() : nullptr;
    if (cover != nullptr) {
        lv_obj_del(cover);
    }
    if (animate) {
        finish_snapshot_transition(transition);
    }

    s_current_index = index;
    update_page_dots();
    if (!persist_current_card()) {
        ESP_LOGW(TAG, "failed to persist current card");
    }
    ESP_LOGI(TAG, "active card %u/%u: %s", static_cast<unsigned>(index + 1),
             static_cast<unsigned>(crystal_registry_installed_count()),
             crystal_registry_installed_id(index));
    return true;
}

void set_transition_progress(void *, int32_t value)
{
    CardTransition &transition = s_card_transition;
    if (transition.incoming_card == nullptr || transition.width <= 0) {
        return;
    }
    transition.progress = LV_CLAMP(0, static_cast<lv_coord_t>(value), transition.width);
    const lv_coord_t x = transition.direction > 0
        ? static_cast<lv_coord_t>(transition.progress - transition.width)
        : static_cast<lv_coord_t>(transition.width - transition.progress);
    lv_obj_set_x(transition.incoming_card, x);
}

void finish_card_transition(lv_anim_t *)
{
    CardTransition &transition = s_card_transition;
    const bool committed = transition.commit;
    const size_t original_index = transition.original_index;
    const size_t active_index = s_current_index;

    if (transition.root != nullptr) {
        lv_obj_del(transition.root);
    }
    if (committed && original_index < s_pane_cache.size()) {
        replace_cached_pane(original_index, transition.outgoing);
        transition.outgoing = nullptr;
    }
    if (transition.outgoing != nullptr) {
        lv_img_buf_free(transition.outgoing);
    }
    transition = CardTransition{};
    prune_pane_cache(active_index);
    ESP_LOGI(TAG, "card crossover %s; active=%u, PSRAM free=%u",
             committed ? "committed" : "cancelled",
             static_cast<unsigned>(active_index + 1),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

bool attach_incoming_image(lv_img_dsc_t *pane)
{
    CardTransition &transition = s_card_transition;
    if (pane == nullptr || transition.incoming_card == nullptr) {
        return false;
    }
    if (transition.incoming_image == nullptr) {
        transition.incoming_image = lv_img_create(transition.incoming_card);
        if (transition.incoming_image == nullptr) {
            return false;
        }
        lv_obj_set_pos(transition.incoming_image, 0, 0);
        lv_obj_clear_flag(transition.incoming_image,
                          LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_img_set_src(transition.incoming_image, pane);
    return true;
}

bool prepare_transition_destination()
{
    CardTransition &transition = s_card_transition;
    if (transition.destination_live) {
        return true;
    }
    if (transition.target_index >= crystal_registry_installed_count() ||
            !start_card(transition.target_index, false)) {
        return false;
    }

    transition.destination_live = true;
    lv_img_dsc_t *fresh = capture_app_area();
    if (fresh != nullptr && transition.target_index < s_pane_cache.size()) {
        lv_img_dsc_t *old = s_pane_cache[transition.target_index].image;
        s_pane_cache[transition.target_index].image = fresh;
        (void)attach_incoming_image(fresh);
        if (old != nullptr && old != fresh) {
            lv_img_buf_free(old);
        }
    }
    ESP_LOGI(TAG, "card crossover reached 50%%; destination %u is live",
             static_cast<unsigned>(transition.target_index + 1));
    return true;
}

bool begin_card_transition(const ESP_Brookesia_GestureInfo_t &info)
{
    if (s_card_transition.phase != CardTransitionPhase::Idle || s_phone == nullptr ||
            s_phone->getManager().getActiveApp() == nullptr) {
        return false;
    }

    const bool previous = (info.start_area & ESP_BROOKESIA_GESTURE_AREA_LEFT_EDGE) != 0 &&
                          info.direction == ESP_BROOKESIA_GESTURE_DIR_RIGHT;
    const bool next = (info.start_area & ESP_BROOKESIA_GESTURE_AREA_RIGHT_EDGE) != 0 &&
                      info.direction == ESP_BROOKESIA_GESTURE_DIR_LEFT;
    size_t target_index = SIZE_MAX;
    int direction = 0;
    if (previous && s_current_index > 0) {
        target_index = s_current_index - 1;
        direction = 1;
    } else if (next && s_current_index + 1 < crystal_registry_installed_count()) {
        target_index = s_current_index + 1;
        direction = -1;
    } else {
        return false;
    }

    const lv_area_t app_area = active_app_area();
    const lv_coord_t width = lv_area_get_width(&app_area);
    const lv_coord_t height = lv_area_get_height(&app_area);
    lv_img_dsc_t *outgoing = capture_app_area();
    if (outgoing == nullptr || width <= 0 || height <= 0) {
        if (outgoing != nullptr) {
            lv_img_buf_free(outgoing);
        }
        return false;
    }

    lv_obj_t *root = lv_obj_create(lv_layer_top());
    if (root == nullptr) {
        lv_img_buf_free(outgoing);
        return false;
    }
    lv_obj_set_size(root, width, height);
    lv_obj_set_pos(root, app_area.x1, app_area.y1);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *outgoing_image = lv_img_create(root);
    lv_obj_t *incoming_card = lv_obj_create(root);
    if (outgoing_image == nullptr || incoming_card == nullptr) {
        lv_obj_del(root);
        lv_img_buf_free(outgoing);
        return false;
    }
    lv_img_set_src(outgoing_image, outgoing);
    lv_obj_set_pos(outgoing_image, 0, 0);
    lv_obj_clear_flag(outgoing_image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_size(incoming_card, width, height);
    lv_obj_set_style_radius(incoming_card, 12, 0);
    lv_obj_set_style_clip_corner(incoming_card, true, 0);
    lv_obj_set_style_border_width(incoming_card, 0, 0);
    lv_obj_set_style_pad_all(incoming_card, 0, 0);
    lv_obj_set_style_bg_color(incoming_card, lv_color_hex(0xE8EDF2), 0);
    lv_obj_set_style_bg_opa(incoming_card, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(incoming_card, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(incoming_card, LV_OPA_30, 0);
    lv_obj_set_style_shadow_width(incoming_card, 12, 0);
    lv_obj_set_style_shadow_ofs_x(incoming_card, direction > 0 ? 4 : -4, 0);
    lv_obj_clear_flag(incoming_card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    CrystalApp *target_app = crystal_registry_installed_app(target_index);
    if (target_app != nullptr) {
        const void *icon_resource = target_app->getLauncherIcon().resource;
        if (icon_resource != nullptr) {
            lv_obj_t *icon = lv_img_create(incoming_card);
            if (icon != nullptr) {
                lv_img_set_src(icon, icon_resource);
                lv_obj_align(icon, LV_ALIGN_CENTER, 0, -34);
                lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            }
        }
        lv_obj_t *name = lv_label_create(incoming_card);
        if (name != nullptr) {
            lv_obj_set_width(name, static_cast<lv_coord_t>(width - 40));
            lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
            lv_label_set_text(name, target_app->getName());
            lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(name, lv_color_hex(0x1C2A36), 0);
            lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(name, LV_ALIGN_CENTER, 0, 42);
            lv_obj_clear_flag(name, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        }
    }

    s_card_transition.phase = CardTransitionPhase::Dragging;
    s_card_transition.root = root;
    s_card_transition.incoming_card = incoming_card;
    s_card_transition.outgoing = outgoing;
    s_card_transition.original_index = s_current_index;
    s_card_transition.target_index = target_index;
    s_card_transition.width = width;
    s_card_transition.direction = direction;
    if (target_index < s_pane_cache.size()) {
        (void)attach_incoming_image(s_pane_cache[target_index].image);
    }

    const int dx = info.stop_x - info.start_x;
    set_transition_progress(nullptr, direction > 0 ? dx : -dx);
    ESP_LOGI(TAG, "card crossover started: %u -> %u, PSRAM free=%u",
             static_cast<unsigned>(s_current_index + 1),
             static_cast<unsigned>(target_index + 1),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    return true;
}

void update_card_transition(const ESP_Brookesia_GestureInfo_t &info)
{
    CardTransition &transition = s_card_transition;
    if (transition.phase != CardTransitionPhase::Dragging) {
        return;
    }
    const int dx = info.stop_x - info.start_x;
    set_transition_progress(nullptr, transition.direction > 0 ? dx : -dx);
    if (!transition.destination_live && transition.progress * 2 >= transition.width) {
        (void)prepare_transition_destination();
    }
}

void settle_card_transition(bool commit)
{
    CardTransition &transition = s_card_transition;
    if (transition.phase != CardTransitionPhase::Dragging) {
        return;
    }

    if (commit && !prepare_transition_destination()) {
        commit = false;
    }
    if (!commit && transition.destination_live) {
        if (start_card(transition.original_index, false)) {
            transition.destination_live = false;
        } else {
            commit = true;
        }
    }

    transition.phase = CardTransitionPhase::Settling;
    transition.commit = commit;
    if (transition.root != nullptr) {
        lv_obj_add_flag(transition.root, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, &transition);
    lv_anim_set_exec_cb(&animation, set_transition_progress);
    lv_anim_set_values(&animation, transition.progress, commit ? transition.width : 0);
    lv_anim_set_time(&animation, kCrossoverSettleMs);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&animation, finish_card_transition);
    lv_anim_start(&animation);
}

void on_app_event(lv_event_t *event)
{
    auto *data = static_cast<ESP_Brookesia_CoreAppEventData_t *>(lv_event_get_param(event));
    if (s_switching || data == nullptr || data->type != ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START) {
        return;
    }
    for (size_t i = 0; i < crystal_registry_installed_count(); ++i) {
        CrystalApp *app = crystal_registry_installed_app(i);
        if (app != nullptr && app->getId() == data->id) {
            s_current_index = i;
            update_page_dots();
            (void)persist_current_card();
            return;
        }
    }
}

void on_gesture_press(lv_event_t *)
{
    s_gesture_owner = CrystalGestureOwner::None;
    s_swallow_wake_touch = crystal_core_consume_wake_touch();
    if (s_swallow_wake_touch && s_gesture != nullptr) {
        (void)s_gesture->setMaskObjectVisible(true);
    }
}

void on_gesture_pressing(lv_event_t *event)
{
    auto *info = static_cast<ESP_Brookesia_GestureInfo_t *>(lv_event_get_param(event));
    if (info == nullptr || s_swallow_wake_touch) {
        return;
    }
    if (s_gesture_owner == CrystalGestureOwner::AppSwitch) {
        update_card_transition(*info);
        return;
    }
    if (s_gesture_owner != CrystalGestureOwner::None ||
            info->direction == ESP_BROOKESIA_GESTURE_DIR_NONE) {
        return;
    }

    if (s_modal_open) {
        s_gesture_owner = CrystalGestureOwner::App;
    } else if (s_quick_settings_open) {
        s_gesture_owner = info->direction == ESP_BROOKESIA_GESTURE_DIR_UP
            ? CrystalGestureOwner::QuickSettings : CrystalGestureOwner::App;
    } else if (s_keyboard_open || s_settings_open || s_switching ||
               s_card_transition.phase != CardTransitionPhase::Idle) {
        s_gesture_owner = CrystalGestureOwner::App;
    } else {
        const bool from_left = (info->start_area & ESP_BROOKESIA_GESTURE_AREA_LEFT_EDGE) != 0;
        const bool from_right = (info->start_area & ESP_BROOKESIA_GESTURE_AREA_RIGHT_EDGE) != 0;
        const bool horizontal_edge =
            (from_left && info->direction == ESP_BROOKESIA_GESTURE_DIR_RIGHT) ||
            (from_right && info->direction == ESP_BROOKESIA_GESTURE_DIR_LEFT);
        const bool top_pull = info->start_y < kTopBand &&
                              info->direction == ESP_BROOKESIA_GESTURE_DIR_DOWN;
        if (horizontal_edge) {
            s_gesture_owner = CrystalGestureOwner::AppSwitch;
        } else if (top_pull) {
            s_gesture_owner = CrystalGestureOwner::QuickSettings;
        } else {
            s_gesture_owner = CrystalGestureOwner::App;
        }
    }

    if (os_owns_gesture() && s_gesture != nullptr) {
        (void)s_gesture->setMaskObjectVisible(true);
    }
    if (s_gesture_owner == CrystalGestureOwner::AppSwitch) {
        (void)begin_card_transition(*info);
    }
    ESP_LOGD(TAG, "gesture owner locked: %u", static_cast<unsigned>(s_gesture_owner));
}

void on_gesture_release(lv_event_t *event)
{
    auto *info = static_cast<ESP_Brookesia_GestureInfo_t *>(lv_event_get_param(event));
    const CrystalGestureOwner owner = s_gesture_owner;
    s_gesture_owner = CrystalGestureOwner::None;
    if (s_swallow_wake_touch) {
        s_swallow_wake_touch = false;
        return;
    }
    if (owner != CrystalGestureOwner::AppSwitch || info == nullptr || s_phone == nullptr || s_switching ||
            s_phone->getManager().getActiveApp() == nullptr) {
        return;
    }

    if (s_card_transition.phase == CardTransitionPhase::Dragging) {
        update_card_transition(*info);
        settle_card_transition(s_card_transition.progress * 2 >= s_card_transition.width);
        return;
    }

    const int dx = info->stop_x - info->start_x;
    const int switch_distance = lv_disp_get_hor_res(nullptr) / 2;
    const bool previous = (info->start_area & ESP_BROOKESIA_GESTURE_AREA_LEFT_EDGE) &&
                          info->direction == ESP_BROOKESIA_GESTURE_DIR_RIGHT &&
                          dx >= switch_distance;
    const bool next = (info->start_area & ESP_BROOKESIA_GESTURE_AREA_RIGHT_EDGE) &&
                      info->direction == ESP_BROOKESIA_GESTURE_DIR_LEFT &&
                      dx <= -switch_distance;

    if (previous && s_current_index > 0) {
        (void)start_card(s_current_index - 1);
    } else if (next && s_current_index + 1 < crystal_registry_installed_count()) {
        (void)start_card(s_current_index + 1);
    }
}
}

bool crystal_shell_init(ESP_Brookesia_Phone *phone)
{
    if (phone == nullptr || crystal_registry_installed_count() == 0) {
        return false;
    }

    ESP_Brookesia_Gesture *gesture = phone->getManager().getGesture();
    if (gesture == nullptr || gesture->getEventObj() == nullptr) {
        ESP_LOGE(TAG, "Brookesia gesture source is unavailable");
        return false;
    }

    s_phone = phone;
    s_gesture = gesture;
    s_current_index = load_current_index();
    s_pane_cache.resize(crystal_registry_installed_count());
    lv_obj_add_event_cb(gesture->getEventObj(), on_gesture_press,
                        gesture->getPressEventCode(), nullptr);
    lv_obj_add_event_cb(gesture->getEventObj(), on_gesture_pressing,
                        gesture->getPressingEventCode(), nullptr);
    lv_obj_add_event_cb(gesture->getEventObj(), on_gesture_release,
                        gesture->getReleaseEventCode(), nullptr);
    if (!phone->registerAppEventCallback(on_app_event, nullptr)) {
        ESP_LOGE(TAG, "failed to observe app starts");
        return false;
    }

    if (!start_card(s_current_index) && s_current_index != 0) {
        ESP_LOGW(TAG, "saved card unavailable; falling back to slot 0");
        if (!start_card(0)) {
            return false;
        }
    }
    return phone->getManager().getActiveApp() != nullptr && init_indicator_overlay();
}

CrystalGestureOwner crystal_shell_gesture_owner() { return s_gesture_owner; }
void crystal_shell_set_quick_settings_open(bool open) { s_quick_settings_open = open; }
void crystal_shell_set_keyboard_open(bool open) { s_keyboard_open = open; }
void crystal_shell_set_settings_open(bool open) { s_settings_open = open; }
void crystal_shell_set_modal_open(bool open) { s_modal_open = open; }
