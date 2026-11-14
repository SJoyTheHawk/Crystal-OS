/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

#include "dev_tester_app.hpp"

#include <stdio.h>

#include "crystal_shell.hpp"
#include "lvgl.h"

namespace {
#if CONFIG_IDF_TARGET_ESP32P4
constexpr lv_coord_t kContentHeight = 720;
constexpr lv_coord_t kContentInset = 24;
constexpr lv_coord_t kFieldWidth = 672;
constexpr lv_coord_t kPasswordFieldWidth = 520;
constexpr lv_coord_t kRevealWidth = 128;
constexpr lv_coord_t kFieldHeight = 62;
constexpr lv_coord_t kFieldTopOffset = 32;
constexpr lv_coord_t kTitleY = 24;
constexpr lv_coord_t kTopFieldY = 92;
constexpr lv_coord_t kPasswordFieldY = 220;
constexpr lv_coord_t kDividerY = 356;
constexpr lv_coord_t kBottomFieldY = 404;
constexpr lv_coord_t kOutputY = 548;
constexpr const lv_font_t *kCaptionFont = &lv_font_montserrat_20;
constexpr const lv_font_t *kFieldFont = &lv_font_montserrat_20;
constexpr const lv_font_t *kTitleFont = &lv_font_montserrat_28;
#else
constexpr lv_coord_t kContentHeight = 650;
constexpr lv_coord_t kContentInset = 20;
constexpr lv_coord_t kFieldWidth = 440;
constexpr lv_coord_t kPasswordFieldWidth = 350;
constexpr lv_coord_t kRevealWidth = 80;
constexpr lv_coord_t kFieldHeight = 50;
constexpr lv_coord_t kFieldTopOffset = 26;
constexpr lv_coord_t kTitleY = 16;
constexpr lv_coord_t kTopFieldY = 66;
constexpr lv_coord_t kPasswordFieldY = 190;
constexpr lv_coord_t kDividerY = 336;
constexpr lv_coord_t kBottomFieldY = 470;
constexpr lv_coord_t kOutputY = 578;
constexpr const lv_font_t *kCaptionFont = &lv_font_montserrat_16;
constexpr const lv_font_t *kFieldFont = nullptr;
constexpr const lv_font_t *kTitleFont = &lv_font_montserrat_28;
#endif
constexpr uint32_t kPage = 0xf2f3f5;
constexpr uint32_t kInk = 0x17191d;
constexpr uint32_t kMuted = 0x666b73;
constexpr uint32_t kAccent = 0x087bdb;
}

DevTesterApp::DevTesterApp()
    : CrystalApp("Dev Tester", &dev_tester_icon)
{
    dev_tester_icon_prepare();
}

bool DevTesterApp::onCreate()
{
    const lv_area_t area = getVisualArea();
    viewport_ = lv_obj_create(lv_scr_act());
    lv_obj_set_size(viewport_, lv_area_get_width(&area), lv_area_get_height(&area));
    lv_obj_set_pos(viewport_, 0, 0);
    lv_obj_set_style_bg_color(viewport_, lv_color_hex(kPage), 0);
    lv_obj_set_style_bg_opa(viewport_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(viewport_, 0, 0);
    lv_obj_set_style_radius(viewport_, 0, 0);
    lv_obj_set_style_pad_all(viewport_, 0, 0);
    lv_obj_set_scroll_dir(viewport_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(viewport_, LV_SCROLLBAR_MODE_AUTO);

    content_ = lv_obj_create(viewport_);
    lv_obj_set_size(content_, LV_PCT(100), kContentHeight);
    lv_obj_set_pos(content_, 0, 0);
    lv_obj_set_style_bg_opa(content_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_style_pad_all(content_, 0, 0);
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(content_, [](lv_event_t *event) {
        if (lv_event_get_target(event) == lv_event_get_current_target(event)) {
            crystal_keyboard_hide();
        }
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *title = lv_label_create(content_);
    lv_label_set_text(title, "Keyboard");
    lv_obj_set_style_text_font(title, kTitleFont, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(kInk), 0);
    lv_obj_set_pos(title, kContentInset, kTitleY);

    top_field_ = add_field("Always visible", "Type here", kTopFieldY);
    password_field_ = add_field("Password", "Enter a secret", kPasswordFieldY, true);

    lv_obj_t *divider = lv_obj_create(content_);
    lv_obj_set_size(divider, kFieldWidth, 1);
    lv_obj_set_pos(divider, kContentInset, kDividerY);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0xc7c9ce), 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    bottom_field_ = add_field("Covered field", "Viewport should reveal this", kBottomFieldY);
    output_ = lv_label_create(content_);
    lv_obj_set_width(output_, kFieldWidth);
    lv_obj_set_style_text_font(output_, kCaptionFont, 0);
    lv_obj_set_style_text_color(output_, lv_color_hex(kMuted), 0);
    lv_obj_set_pos(output_, kContentInset, kOutputY);
    update_output();
    return true;
}

lv_obj_t *DevTesterApp::add_field(const char *label, const char *placeholder,
                                  lv_coord_t y, bool password)
{
    lv_obj_t *caption = lv_label_create(content_);
    lv_label_set_text(caption, label);
    lv_obj_set_style_text_color(caption, lv_color_hex(kMuted), 0);
    lv_obj_set_style_text_font(caption, kCaptionFont, 0);
    lv_obj_set_pos(caption, kContentInset, y);

    lv_obj_t *field = lv_textarea_create(content_);
    const lv_coord_t field_width = password ? kPasswordFieldWidth : kFieldWidth;
    lv_obj_set_size(field, field_width, kFieldHeight);
    lv_obj_set_pos(field, kContentInset, y + kFieldTopOffset);
#if CONFIG_IDF_TARGET_ESP32P4
    lv_obj_set_style_text_font(field, kFieldFont, 0);
#endif
    lv_textarea_set_one_line(field, true);
    lv_textarea_set_cursor_click_pos(field, true);
    lv_textarea_set_placeholder_text(field, placeholder);
    lv_textarea_set_password_mode(field, password);
    lv_obj_set_style_bg_color(field, lv_color_white(), 0);
    lv_obj_set_style_border_color(field, lv_color_hex(0xb8bbc1), 0);
    lv_obj_set_style_border_color(field, lv_color_hex(kAccent), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(field, 1, 0);
    lv_obj_set_style_border_width(field, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(field, 7, 0);
    lv_obj_add_event_cb(field, field_event, LV_EVENT_ALL, this);

    if (password) {
        lv_obj_t *reveal = lv_btn_create(content_);
        lv_obj_set_size(reveal, kRevealWidth, kFieldHeight);
        lv_obj_set_pos(reveal, kContentInset + field_width + 16, y + kFieldTopOffset);
        lv_obj_set_style_bg_color(reveal, lv_color_hex(0xd7d9dd), 0);
        lv_obj_set_style_radius(reveal, 7, 0);
        lv_obj_t *text = lv_label_create(reveal);
        lv_label_set_text(text, "Show");
#if CONFIG_IDF_TARGET_ESP32P4
        lv_obj_set_style_text_font(text, kFieldFont, 0);
#endif
        lv_obj_set_style_text_color(text, lv_color_hex(kInk), 0);
        lv_obj_center(text);
        lv_obj_add_event_cb(reveal, reveal_event, LV_EVENT_CLICKED, field);
    }
    return field;
}

void DevTesterApp::field_event(lv_event_t *event)
{
    auto *app = static_cast<DevTesterApp *>(lv_event_get_user_data(event));
    if (app == nullptr) return;
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        (void)crystal_keyboard_show(static_cast<lv_obj_t *>(lv_event_get_target(event)), app->viewport_);
    } else if (code == LV_EVENT_READY) {
        app->update_output();
    }
}

void DevTesterApp::reveal_event(lv_event_t *event)
{
    auto *field = static_cast<lv_obj_t *>(lv_event_get_user_data(event));
    if (field == nullptr) return;
    const bool masked = lv_textarea_get_password_mode(field);
    lv_textarea_set_password_mode(field, !masked);
    lv_obj_t *label = lv_obj_get_child(static_cast<lv_obj_t *>(lv_event_get_target(event)), 0);
    if (label != nullptr) lv_label_set_text(label, masked ? "Hide" : "Show");
}

void DevTesterApp::update_output()
{
    if (output_ == nullptr) return;
    const char *top = top_field_ == nullptr ? "" : lv_textarea_get_text(top_field_);
    const char *bottom = bottom_field_ == nullptr ? "" : lv_textarea_get_text(bottom_field_);
    char text[160];
    snprintf(text, sizeof(text), "Committed\nTop: %.40s\nBottom: %.40s", top, bottom);
    lv_label_set_text(output_, text);
}

bool DevTesterApp::onDestroy()
{
    crystal_keyboard_hide();
    viewport_ = nullptr;
    content_ = nullptr;
    top_field_ = nullptr;
    password_field_ = nullptr;
    bottom_field_ = nullptr;
    output_ = nullptr;
    return true;
}
