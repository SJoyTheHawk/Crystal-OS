/* SPDX-License-Identifier: MIT */

#include "crystal_shell.hpp"

#include <stdlib.h>
#include <string.h>

#include "crystal_app.hpp"
#include "crystal_hal.hpp"
#include "crystal_registry.hpp"
#include "esp_brookesia.hpp"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "crystal_shell";

namespace {
constexpr int kSwitchDistance = 240;
// Crop the captured app area to 90% x 90% about its centre, then store it at
// 1/kSnapshotScaleDivisor of the app's resolution. On this panel the observed
// 480x440 app area produces a 240x220 snapshot (~103 KiB).
constexpr uint32_t kSnapshotCropPercent = 90;
constexpr uint32_t kSnapshotScaleDivisor = 2;
constexpr uint32_t kTransitionMs = 180;
constexpr char kCurrentCardKey[] = "shell.card";

ESP_Brookesia_Phone *s_phone = nullptr;
size_t s_current_index = 0;
bool s_switching = false;

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

bool start_card(size_t index)
{
    CrystalApp *app = crystal_registry_installed_app(index);
    if (s_phone == nullptr || app == nullptr || index >= crystal_registry_installed_count()) {
        return false;
    }

    lv_obj_t *cover = nullptr;
    if (s_phone->getManager().getActiveApp() != nullptr &&
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

    lv_obj_t *transition = show_snapshot_transition();
    if (cover != nullptr) {
        lv_obj_del(cover);
    }
    finish_snapshot_transition(transition);

    s_current_index = index;
    if (!persist_current_card()) {
        ESP_LOGW(TAG, "failed to persist current card");
    }
    ESP_LOGI(TAG, "active card %u/%u: %s", static_cast<unsigned>(index + 1),
             static_cast<unsigned>(crystal_registry_installed_count()),
             crystal_registry_installed_id(index));
    return true;
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
            (void)persist_current_card();
            return;
        }
    }
}

void on_gesture_release(lv_event_t *event)
{
    auto *info = static_cast<ESP_Brookesia_GestureInfo_t *>(lv_event_get_param(event));
    if (info == nullptr || s_phone == nullptr || s_switching ||
            s_phone->getManager().getActiveApp() == nullptr) {
        return;
    }

    const int dx = info->stop_x - info->start_x;
    const bool previous = (info->start_area & ESP_BROOKESIA_GESTURE_AREA_LEFT_EDGE) &&
                          info->direction == ESP_BROOKESIA_GESTURE_DIR_RIGHT &&
                          dx >= kSwitchDistance;
    const bool next = (info->start_area & ESP_BROOKESIA_GESTURE_AREA_RIGHT_EDGE) &&
                      info->direction == ESP_BROOKESIA_GESTURE_DIR_LEFT &&
                      dx <= -kSwitchDistance;

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
    s_current_index = load_current_index();
    lv_obj_add_event_cb(gesture->getEventObj(), on_gesture_release,
                        gesture->getReleaseEventCode(), nullptr);
    if (!phone->registerAppEventCallback(on_app_event, nullptr)) {
        ESP_LOGE(TAG, "failed to observe app starts");
        return false;
    }

    if (!start_card(s_current_index) && s_current_index != 0) {
        ESP_LOGW(TAG, "saved card unavailable; falling back to slot 0");
        return start_card(0);
    }
    return phone->getManager().getActiveApp() != nullptr;
}
