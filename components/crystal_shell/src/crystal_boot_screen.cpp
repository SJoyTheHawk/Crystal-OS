/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "crystal_boot_screen.hpp"
#include "crystal_boot_logo.hpp"
#include "crystal_stylesheet.hpp"
#include "sdkconfig.h"
#include "lvgl.h"
#include <stdio.h>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#if CONFIG_IDF_TARGET_ESP32P4
namespace {
enum class State { Loading, Ready, Failed };
struct Update {
    char message[112];
    int progress;
    State state;
};
QueueHandle_t s_updates = nullptr;
lv_obj_t *s_root = nullptr;
lv_obj_t *s_logo = nullptr;
lv_obj_t *s_phase = nullptr;
lv_obj_t *s_status = nullptr;
lv_obj_t *s_progress = nullptr;
lv_timer_t *s_timer = nullptr;
bool s_terminal = false;
std::atomic<bool> s_active{false};

void queue_update(const char *message, int progress, State state)
{
    if (s_updates == nullptr) return;
    Update update{};
    snprintf(update.message, sizeof(update.message), "%s", message != nullptr ? message : "");
    update.progress = LV_CLAMP(0, progress, 100);
    update.state = state;
    xQueueOverwrite(s_updates, &update);
}

void fade_finished(lv_anim_t *animation)
{
    lv_obj_del(static_cast<lv_obj_t *>(animation->var));
    s_root = s_logo = s_phase = s_status = s_progress = nullptr;
    s_active.store(false);
}

void apply_update(lv_timer_t *)
{
    if (s_root == nullptr || s_terminal) return;
    // Shell overlays are created after this one. Keep startup input blocked
    // until the real registry and shell are ready, including during the fade.
    lv_obj_move_foreground(s_root);
    Update update{};
    if (xQueueReceive(s_updates, &update, 0) != pdTRUE) return;
    const auto &style = crystal_visual_style();
    lv_label_set_text(s_status, update.message);
    lv_bar_set_value(s_progress, update.progress, LV_ANIM_ON);
    if (update.state == State::Loading) return;
    s_terminal = true;
    lv_timer_del(s_timer);
    s_timer = nullptr;
    const bool failed = update.state == State::Failed;
    lv_label_set_text(s_phase, failed ? "STARTUP FAILED" : "READY");
    lv_obj_set_style_text_color(s_phase, lv_color_hex(failed ? style.error : style.success), 0);
    lv_obj_set_style_bg_color(s_progress, lv_color_hex(failed ? style.error : style.success), LV_PART_INDICATOR);
    if (failed) return;
    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, s_root);
    lv_anim_set_values(&fade, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_delay(&fade, style.boot_ready_hold_ms);
    lv_anim_set_time(&fade, style.boot_fade_ms);
    lv_anim_set_path_cb(&fade, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&fade, [](void *object, int32_t opacity) {
        lv_obj_set_style_opa(static_cast<lv_obj_t *>(object), opacity, 0);
    });
    lv_anim_set_ready_cb(&fade, fade_finished);
    lv_anim_start(&fade);
}

lv_obj_t *label(const char *text, const lv_font_t *font, uint32_t color, lv_coord_t y)
{
    auto *object = lv_label_create(s_root);
    if (object == nullptr) return nullptr;
    lv_label_set_text(object, text);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_obj_align(object, LV_ALIGN_CENTER, 0, y);
    return object;
}
}
#endif

bool crystal_boot_screen_show()
{
#if CONFIG_IDF_TARGET_ESP32P4
    if (s_root != nullptr) {
        lv_obj_move_foreground(s_root);
        return true;
    }
    if (s_updates == nullptr) s_updates = xQueueCreate(1, sizeof(Update));
    if (s_updates == nullptr) return false;
    xQueueReset(s_updates);
    s_terminal = false;
    const auto &style = crystal_visual_style();
    s_root = lv_obj_create(lv_layer_top());
    if (s_root == nullptr) return false;
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_root, lv_color_hex(style.background), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    auto *title = label("Crystal OS", &lv_font_montserrat_48, style.text, 88);
    s_phase = label("STARTING", &lv_font_montserrat_20, style.accent, 142);
    s_status = label("Preparing your workspace...", &lv_font_montserrat_20, style.secondary, 186);
    s_logo = lv_img_create(s_root);
    s_progress = lv_bar_create(s_root);
    if (title == nullptr || s_phase == nullptr || s_status == nullptr ||
            s_logo == nullptr || s_progress == nullptr) {
        lv_obj_del(s_root);
        s_root = s_logo = s_phase = s_status = s_progress = nullptr;
        return false;
    }
    lv_obj_set_width(s_status, LV_PCT(84));
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    // The 598px source stays untouched in reference/. Draw a pre-sized 256px
    // copy at native zoom, preserving its alpha edges and square proportions.
    lv_img_set_src(s_logo, &crystal_boot_logo);
    lv_obj_align(s_logo, LV_ALIGN_CENTER, 0, -120);
    lv_obj_clear_flag(s_logo, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_progress, LV_PCT(52), 8);
    lv_obj_align(s_progress, LV_ALIGN_CENTER, 0, 230);
    lv_bar_set_range(s_progress, 0, 100);
    lv_obj_set_style_bg_color(s_progress, lv_color_hex(style.elevated), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress, lv_color_hex(style.accent), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_progress, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(s_progress, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    s_timer = lv_timer_create(apply_update, 30, nullptr);
    if (s_timer == nullptr) {
        lv_obj_del(s_root);
        s_root = s_logo = s_phase = s_status = s_progress = nullptr;
        return false;
    }
    queue_update("Preparing your workspace...", 10, State::Loading);
    s_active.store(true);
#endif
    return true;
}

void crystal_boot_screen_update(const char *message, int progress)
{
#if CONFIG_IDF_TARGET_ESP32P4
    queue_update(message, progress, State::Loading);
#else
    (void)message; (void)progress;
#endif
}
void crystal_boot_screen_finish()
{
#if CONFIG_IDF_TARGET_ESP32P4
    queue_update("Your workspace is ready", 100, State::Ready);
#endif
}
void crystal_boot_screen_fail(const char *message)
{
#if CONFIG_IDF_TARGET_ESP32P4
    queue_update(message, 100, State::Failed);
#else
    (void)message;
#endif
}

bool crystal_boot_screen_active()
{
#if CONFIG_IDF_TARGET_ESP32P4
    return s_active.load();
#else
    return false;
#endif
}
