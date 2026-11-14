/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

#include "crystal_shell.hpp"
#include "crystal_stylesheet.hpp"
#include "crystal_page_transition.hpp"
#include "crystal_boot_screen.hpp"
#include "manage_apps_page.h"

#include <cmath>
#include <algorithm>
#include <memory>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "crystal_app.hpp"
#include "crystal_ui.hpp"
#include "crystal_control.hpp"
#include "crystal_core.hpp"
#include "crystal_hal.hpp"
#include "crystal_ethernet.hpp"
#include "crystal_network.h"
#include "crystal_network_policy.hpp"
#include "crystal_registry.hpp"
#include "crystal_update_view.h"

extern "C" void crystal_update_snapshot(crystal_update_snapshot_t *out);
#include "weather_app.hpp"
#include "esp_brookesia.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lvgl.h"

static const char *TAG = "crystal_shell";

extern "C" int crystal_update_check(void);

static lv_obj_t *s_ota_modal = nullptr;
static lv_obj_t *s_ota_message = nullptr;
static lv_obj_t *s_ota_progress = nullptr;
static lv_obj_t *s_ota_percent = nullptr;
static lv_obj_t *s_update_dialog = nullptr;
static lv_obj_t *s_update_failure_dialog = nullptr;
static lv_obj_t *s_control_pair_dialog = nullptr;
static lv_obj_t *s_developer_tools_page = nullptr;
static lv_obj_t *s_control_api_row = nullptr;
static lv_obj_t *s_control_api_switch = nullptr;
static lv_obj_t *s_control_status_label = nullptr;
static bool s_control_start_pending = false;

static void control_panel_start_worker(void *)
{
    char pairing_pin[7] = {};
    const bool started = crystal_control_enable(pairing_pin);
    bool posted = false;
    for (unsigned attempt = 0; attempt < 10 && !posted; ++attempt) {
        posted = crystal_ui_post(UI_EVT_CONTROL_START,
            started ? pairing_pin : nullptr, started ? sizeof(pairing_pin) : 0);
        if (!posted) vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!posted) {
        ESP_LOGE(TAG, "Could not report local panel startup result to the UI");
        if (started) crystal_control_disable();
    }
    memset(pairing_pin, 0, sizeof(pairing_pin));
    vTaskDeleteWithCaps(nullptr);
}

static void control_pair_dialog_close()
{
    if (s_control_pair_dialog != nullptr) {
        lv_obj_del(s_control_pair_dialog);
        s_control_pair_dialog = nullptr;
        crystal_shell_set_modal_open(false);
    }
}

static void control_pair_dialog_build(const char *pin, const char *url)
{
    // Match the accepted 720px Settings scale without changing the S3 dialog.
    constexpr auto px = [](lv_coord_t value) -> lv_coord_t {
#if CONFIG_IDF_TARGET_ESP32P4
        return value * 3 / 2;
#else
        return value;
#endif
    };
#if CONFIG_IDF_TARGET_ESP32P4
    const lv_font_t *title_font = &lv_font_montserrat_28;
    const lv_font_t *code_font = &lv_font_montserrat_48;
    const lv_font_t *body_font = &lv_font_montserrat_24;
    const lv_font_t *notice_font = &lv_font_montserrat_20;
#else
    const lv_font_t *title_font = &lv_font_montserrat_20;
    const lv_font_t *code_font = &lv_font_montserrat_28;
    const lv_font_t *body_font = &lv_font_montserrat_16;
    const lv_font_t *notice_font = &lv_font_montserrat_14;
#endif
    control_pair_dialog_close();
    s_control_pair_dialog = lv_obj_create(lv_layer_top());
    if (s_control_pair_dialog == nullptr) return;
    lv_obj_set_size(s_control_pair_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_control_pair_dialog, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_control_pair_dialog, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_control_pair_dialog, 0, 0);
    lv_obj_add_flag(s_control_pair_dialog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_control_pair_dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(s_control_pair_dialog);
    lv_obj_set_size(card, px(400), px(330));
    lv_obj_set_style_bg_color(card, lv_color_hex(0x252a30), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x59636e), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, px(16), 0);
    lv_obj_set_style_pad_all(card, px(22), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(card);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "Pair Control Panel");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, title_font, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *code = lv_label_create(card);
    lv_label_set_text(code, pin);
    lv_obj_set_style_text_color(code, lv_color_hex(0x72a7ff), 0);
    lv_obj_set_style_text_font(code, code_font, 0);
    lv_obj_set_style_text_letter_space(code, px(5), 0);
    lv_obj_align(code, LV_ALIGN_TOP_MID, 0, px(55));

    lv_obj_t *detail = lv_label_create(card);
    lv_obj_set_width(detail, px(350));
    lv_obj_set_height(detail, px(75));
    lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(detail, "Open %s on a computer on the same local network.\nThe code expires in 10 minutes and can be used once.", url);
    lv_obj_set_style_text_color(detail, lv_color_hex(0xcbd5e1), 0);
    lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(detail, body_font, 0);
    lv_obj_align(detail, LV_ALIGN_TOP_MID, 0, px(112));

    lv_obj_t *notice = lv_label_create(card);
    lv_label_set_text(notice, "Keep this code private. The panel is available until restart or disabled here.");
    lv_obj_set_width(notice, px(350));
    lv_label_set_long_mode(notice, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(notice, lv_color_hex(0xf0b35a), 0);
    lv_obj_set_style_text_align(notice, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(notice, notice_font, 0);
    lv_obj_align(notice, LV_ALIGN_BOTTOM_MID, 0, -px(54));

    lv_obj_t *done = lv_btn_create(card);
    lv_obj_set_size(done, px(140), px(42));
    lv_obj_align(done, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(done, lv_color_hex(0x3b82f6), 0);
    lv_obj_t *done_label = lv_label_create(done);
    lv_label_set_text(done_label, "Done");
#if CONFIG_IDF_TARGET_ESP32P4
    lv_obj_set_style_text_font(done_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(done_label, lv_color_white(), 0);
#endif
    lv_obj_center(done_label);
    lv_obj_add_event_cb(done, [](lv_event_t *) { control_pair_dialog_close(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_move_foreground(s_control_pair_dialog);
    crystal_shell_set_modal_open(true);
}

static void update_failure_dialog_close()
{
    if (s_update_failure_dialog != nullptr) {
        lv_obj_del(s_update_failure_dialog);
        s_update_failure_dialog = nullptr;
    }
    crystal_shell_set_modal_open(false);
}

static void update_result_dialog_build(const char *title_text, const char *message)
{
    if (title_text == nullptr || title_text[0] == '\0' || message == nullptr ||
            message[0] == '\0' || s_update_failure_dialog != nullptr) return;

    s_update_failure_dialog = lv_obj_create(lv_layer_top());
    if (s_update_failure_dialog == nullptr) return;
    lv_obj_set_size(s_update_failure_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_update_failure_dialog, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_update_failure_dialog, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_update_failure_dialog, 0, 0);
    lv_obj_add_flag(s_update_failure_dialog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_update_failure_dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(s_update_failure_dialog);
    lv_obj_set_size(card, 360, 230);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x252a30), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x59636e), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 24, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(card);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *detail = lv_label_create(card);
    lv_obj_set_width(detail, 312);
    lv_obj_set_height(detail, 90);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
    lv_label_set_text(detail, message);
    lv_obj_set_style_text_color(detail, lv_color_hex(0xcbd5e1), 0);
    lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(detail, LV_ALIGN_TOP_MID, 0, 48);

    lv_obj_t *ok = lv_btn_create(card);
    lv_obj_set_size(ok, 140, 44);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(ok, lv_color_hex(0x3b82f6), 0);
    lv_obj_t *ok_label = lv_label_create(ok);
    lv_label_set_text(ok_label, "OK");
    lv_obj_center(ok_label);
    lv_obj_add_event_cb(ok, [](lv_event_t *) {
        update_failure_dialog_close();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_move_foreground(s_update_failure_dialog);
    crystal_shell_set_modal_open(true);
}

static void update_failure_dialog_build(const char *message)
{
    update_result_dialog_build("Update Failed", message);
}

static void update_dialog_close()
{
    if (s_update_dialog != nullptr) {
        lv_obj_del(s_update_dialog);
        s_update_dialog = nullptr;
    }
    crystal_shell_set_modal_open(false);
}

static void update_dialog_build(const crystal_update_snapshot_t &snapshot)
{
    update_dialog_close();
    s_update_dialog = lv_obj_create(lv_layer_top());
    if (s_update_dialog == nullptr) return;
    lv_obj_set_size(s_update_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_update_dialog, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_update_dialog, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_update_dialog, 0, 0);
    lv_obj_clear_flag(s_update_dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(s_update_dialog);
    lv_obj_set_size(card, 360, 280);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x252a30), 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 24, 0);
    lv_obj_center(card);

    lv_obj_t *title = lv_label_create(card);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_label_set_text(title, "Update Available");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *version = lv_label_create(card);
    char version_text[64];
    snprintf(version_text, sizeof(version_text), "Crystal OS %s", snapshot.available_version);
    lv_label_set_text(version, version_text);
    lv_obj_set_style_text_color(version, lv_color_hex(0xcbd5e1), 0);
    lv_obj_set_style_text_font(version, &lv_font_montserrat_16, 0);
    lv_obj_align(version, LV_ALIGN_TOP_MID, 0, 32);

    lv_obj_t *size = lv_label_create(card);
    char size_text[32];
    snprintf(size_text, sizeof(size_text), "%.1f MB", snapshot.download_size / (1024.0f * 1024.0f));
    lv_label_set_text(size, size_text);
    lv_obj_set_style_text_color(size, lv_color_hex(0x94a3b8), 0);
    lv_obj_set_style_text_font(size, &lv_font_montserrat_14, 0);
    lv_obj_align(size, LV_ALIGN_TOP_MID, 0, 58);

    lv_obj_t *notes = lv_label_create(card);
    lv_obj_set_width(notes, 312);
    lv_obj_set_height(notes, 80);
    lv_label_set_long_mode(notes, LV_LABEL_LONG_WRAP);
    lv_label_set_text(notes, snapshot.notes[0] != '\0' ? snapshot.notes : "A new Crystal OS update is available.");
    lv_obj_set_style_text_color(notes, lv_color_hex(0xcbd5e1), 0);
    lv_obj_set_style_text_font(notes, &lv_font_montserrat_14, 0);
    lv_obj_align(notes, LV_ALIGN_TOP_LEFT, 0, 90);

    lv_obj_t *cancel = lv_btn_create(card);
    lv_obj_set_size(cancel, 140, 44);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(0x424852), 0);
    lv_obj_t *cancel_label = lv_label_create(cancel);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel, [](lv_event_t *) {
        crystal_update_cancel_check();
        update_dialog_close();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *download = lv_btn_create(card);
    lv_obj_set_size(download, 140, 44);
    lv_obj_align(download, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(download, lv_color_hex(0x3b82f6), 0);
    lv_obj_t *download_label = lv_label_create(download);
    lv_label_set_text(download_label, "Download");
    lv_obj_center(download_label);
    lv_obj_add_event_cb(download, [](lv_event_t *) {
        if (crystal_update_start_download() == ESP_OK) {
            ESP_LOGI(TAG, "User confirmed download");
        }
    }, LV_EVENT_CLICKED, nullptr);

    crystal_shell_set_modal_open(true);
}

static void ota_modal_build()
{
    if (s_ota_modal != nullptr) return;
    s_ota_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_ota_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_ota_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ota_modal, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_ota_modal, 0, 0);
    lv_obj_clear_flag(s_ota_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(s_ota_modal);
    lv_obj_set_size(card, 360, 180);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x252a30), 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 24, 0);
    lv_obj_center(card);

    s_ota_message = lv_label_create(card);
    lv_obj_set_width(s_ota_message, 312);
    lv_obj_set_style_text_color(s_ota_message, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_ota_message, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_ota_message, "Checking for updates...");
    lv_obj_align(s_ota_message, LV_ALIGN_TOP_MID, 0, 12);

    s_ota_progress = lv_bar_create(card);
    lv_obj_set_size(s_ota_progress, 300, 14);
    lv_obj_align(s_ota_progress, LV_ALIGN_CENTER, 0, 4);
    lv_bar_set_range(s_ota_progress, 0, 100);
    lv_bar_set_value(s_ota_progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ota_progress, lv_color_hex(0x424852), 0);
    lv_obj_set_style_bg_color(s_ota_progress, lv_color_hex(0x3b82f6), LV_PART_INDICATOR);

    s_ota_percent = lv_label_create(card);
    lv_obj_set_style_text_color(s_ota_percent, lv_color_hex(0xcbd5e1), 0);
    lv_label_set_text(s_ota_percent, "0%");
    lv_obj_align(s_ota_percent, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_add_flag(s_ota_modal, LV_OBJ_FLAG_HIDDEN);
}

static void ota_modal_update(const crystal_update_snapshot_t &snapshot)
{
    // Checking remains cancellable and non-modal. The modal is reserved for the
    // transfer phase, when navigation must not interrupt the progress display.
    if (snapshot.state == UPDATE_STATE_CHECKING || snapshot.state == UPDATE_STATE_READY ||
            snapshot.state == UPDATE_STATE_FAILED || snapshot.state == UPDATE_STATE_CURRENT) {
        if (s_ota_modal != nullptr) lv_obj_add_flag(s_ota_modal, LV_OBJ_FLAG_HIDDEN);
        crystal_shell_set_modal_open(false);
        return;
    }
    ota_modal_build();
    lv_obj_clear_flag(s_ota_modal, LV_OBJ_FLAG_HIDDEN);
    crystal_shell_set_modal_open(true);
    const char *message = "Checking for updates...";
    if (snapshot.state == UPDATE_STATE_DOWNLOADING) message = "Updating firmware...";
    else if (snapshot.state == UPDATE_STATE_INSTALLING) message = "Installing update...";
    else if (snapshot.state == UPDATE_STATE_READY) message = "Update ready to install";
    else if (snapshot.state == UPDATE_STATE_FAILED) message = snapshot.error_message;
    lv_label_set_text(s_ota_message, message);
    lv_bar_set_value(s_ota_progress, snapshot.progress_percent, LV_ANIM_OFF);
    char percent[8];
    snprintf(percent, sizeof(percent), "%u%%", snapshot.progress_percent);
    lv_label_set_text(s_ota_percent, percent);
}

void wifi_page_open();
void wifi_page_close();
void settings_open();
void settings_open_at_wifi();
void wifi_tile_text(char *out, size_t size);
bool shell_consume_back();
void close_settings_and_restore_app();
void system_page_pop();
void settings_button_feedback(lv_obj_t *button);

namespace {
constexpr int kTopBand = 20;
constexpr int kBottomBand = 24;
// Home pill geometry. Width matches iOS's proportion: its indicator is 140pt on
// a 390pt-wide screen, 35.9%, which is 172px here. Stealth comes from opacity
// instead -- 30% white, against app backgrounds that are all dark in this tree
// (0x11181F, 0x101827), so its resting state reads without a shadow. The inset
// puts it inside kBottomBand, so it marks the band it belongs to.
#if CONFIG_IDF_TARGET_ESP32P4
constexpr lv_coord_t kHomePillWidth = 216;
constexpr lv_coord_t kHomePillHeight = 6;
constexpr lv_coord_t kHomePillInset = 10;
constexpr lv_coord_t kHomePillHitMargin = 12;
constexpr lv_coord_t kQuickCell = 86;
constexpr lv_coord_t kQuickGap = 14;
constexpr lv_coord_t kQuickPad = 18;
constexpr lv_coord_t kQuickMargin = 16;
constexpr lv_coord_t kQuickGapTop = 12;
constexpr lv_coord_t kQuickCornerWidth = 180;
constexpr lv_coord_t kQuickNativeIconSize = 28;
constexpr const lv_font_t *kQuickIconFont = &lv_font_montserrat_28;
constexpr lv_coord_t kSettingsHeaderHeight = 72;
constexpr lv_coord_t kSettingsContentInset = 16;
constexpr lv_coord_t kSettingsRowHeight = 84;
constexpr lv_coord_t kSettingsRowPad = 20;
constexpr lv_coord_t kSettingsTitleTop = 10;
constexpr lv_coord_t kSettingsSummaryTop = 48;
constexpr lv_coord_t kSettingsSummaryHeight = 24;
constexpr lv_coord_t kSettingsBackWidth = 76;
constexpr lv_coord_t kSettingsBackHeight = 58;
constexpr lv_coord_t kSettingsBackLeft = 12;
constexpr lv_coord_t kSettingsBackTop = 7;
constexpr lv_coord_t kSettingsHeadingTop = 21;
constexpr lv_coord_t kSettingsSwitchWidth = 64;
constexpr lv_coord_t kSettingsSwitchHeight = 38;
constexpr lv_coord_t kSettingsSwitchClick = 22;
constexpr lv_coord_t kSettingsSliderWidth = 240;
constexpr lv_coord_t kSettingsSliderHeight = 24;
constexpr lv_coord_t kSettingsNarrowSliderWidth = 220;
// Keep sliders on the same right edge as the settings toggles. Their expanded
// hit area remains independent of the visual alignment.
constexpr lv_coord_t kSettingsSliderClick = 20;
constexpr lv_coord_t kSettingsSliderRightInset = 0;
constexpr int kBrightnessUiMin = 1;
constexpr int kBrightnessUiMax = 100;
constexpr int kDimBrightnessUiMin = 1;
constexpr int kDimBrightnessUiMax = 50;
constexpr lv_coord_t kSettingsDropdownWidth = 200;
constexpr lv_coord_t kSettingsInputHeight = 60;
constexpr lv_coord_t kSettingsConfirmWidth = 480;
constexpr lv_coord_t kSettingsConfirmHeight = 240;
constexpr lv_coord_t kSettingsConfirmButtonWidth = 168;
constexpr lv_coord_t kSettingsConfirmButtonHeight = 54;
constexpr lv_coord_t kManageRestartWidth = 500;
constexpr lv_coord_t kManageRestartHeight = 300;
constexpr lv_coord_t kManageRestartButtonWidth = 190;
constexpr lv_coord_t kManageRestartButtonHeight = 54;
constexpr lv_coord_t kForgetDialogWidth = 500;
constexpr lv_coord_t kForgetDialogHeight = 210;
constexpr lv_coord_t kForgetButtonWidth = 168;
constexpr lv_coord_t kForgetButtonHeight = 52;
constexpr lv_coord_t kForgetButtonInset = 32;
constexpr lv_coord_t kForgetNameTop = 62;
constexpr lv_coord_t kWifiDialogWidth = 600;
constexpr lv_coord_t kWifiDialogHeight = 248;
constexpr lv_coord_t kWifiDialogGap = 24;
constexpr lv_coord_t kWifiDialogInset = 24;
constexpr lv_coord_t kWifiFieldWidth = 432;
constexpr lv_coord_t kWifiFieldHeight = 56;
constexpr lv_coord_t kWifiFieldTop = 96;
constexpr lv_coord_t kWifiRevealWidth = 108;
constexpr lv_coord_t kWifiActionWidth = 140;
constexpr lv_coord_t kWifiActionHeight = 50;
constexpr lv_coord_t kWifiActionInset = 90;
constexpr lv_coord_t kWifiActionBottom = 16;
constexpr lv_coord_t kWifiListHeight = 480;
constexpr lv_coord_t kWifiNetworkRowHeight = 68;
constexpr lv_coord_t kWifiNetworkRowInset = 24;
constexpr const lv_font_t *kSettingsTitleFont = &lv_font_montserrat_28;
constexpr const lv_font_t *kSettingsSummaryFont = &lv_font_montserrat_20;
constexpr const lv_font_t *kSettingsStatusFont = &lv_font_montserrat_20;
constexpr const lv_font_t *kSettingsInputFont = &lv_font_montserrat_20;
constexpr const lv_font_t *kWifiTitleFont = &lv_font_montserrat_28;
constexpr const lv_font_t *kWifiBodyFont = &lv_font_montserrat_20;
constexpr const lv_font_t *kQuickTileFont = &lv_font_montserrat_20;
#else
constexpr lv_coord_t kHomePillWidth = 172;
constexpr lv_coord_t kHomePillHeight = 6;
constexpr lv_coord_t kHomePillInset = 7;
constexpr lv_coord_t kHomePillHitMargin = 8;
constexpr lv_coord_t kQuickCell = 62;
constexpr lv_coord_t kQuickGap = 10;
constexpr lv_coord_t kQuickPad = 14;
constexpr lv_coord_t kQuickMargin = 12;
constexpr lv_coord_t kQuickGapTop = 8;
constexpr lv_coord_t kQuickCornerWidth = 120;
constexpr lv_coord_t kQuickNativeIconSize = 16;
constexpr lv_coord_t kSettingsHeaderHeight = 52;
constexpr lv_coord_t kSettingsContentInset = 10;
constexpr lv_coord_t kSettingsRowHeight = 60;
constexpr lv_coord_t kSettingsRowPad = 14;
constexpr lv_coord_t kSettingsTitleTop = 8;
constexpr lv_coord_t kSettingsSummaryTop = 33;
constexpr lv_coord_t kSettingsSummaryHeight = 18;
constexpr lv_coord_t kSettingsBackWidth = 64;
constexpr lv_coord_t kSettingsBackHeight = 44;
constexpr lv_coord_t kSettingsBackLeft = 8;
constexpr lv_coord_t kSettingsBackTop = 4;
constexpr lv_coord_t kSettingsHeadingTop = 14;
constexpr lv_coord_t kSettingsSwitchWidth = 52;
constexpr lv_coord_t kSettingsSwitchHeight = 30;
constexpr lv_coord_t kSettingsSwitchClick = 15;
constexpr lv_coord_t kSettingsSliderWidth = 180;
constexpr lv_coord_t kSettingsSliderHeight = 18;
constexpr lv_coord_t kSettingsNarrowSliderWidth = 160;
constexpr lv_coord_t kSettingsSliderClick = 20;
constexpr lv_coord_t kSettingsSliderRightInset = 0;
constexpr int kBrightnessUiMin = 0;
constexpr int kBrightnessUiMax = 95;
constexpr int kDimBrightnessUiMin = 5;
constexpr int kDimBrightnessUiMax = 50;
constexpr lv_coord_t kSettingsDropdownWidth = 170;
constexpr lv_coord_t kSettingsInputHeight = 52;
constexpr lv_coord_t kSettingsConfirmWidth = 360;
constexpr lv_coord_t kSettingsConfirmHeight = 190;
constexpr lv_coord_t kSettingsConfirmButtonWidth = 140;
constexpr lv_coord_t kSettingsConfirmButtonHeight = 44;
constexpr lv_coord_t kManageRestartWidth = 380;
constexpr lv_coord_t kManageRestartHeight = 220;
constexpr lv_coord_t kManageRestartButtonWidth = 154;
constexpr lv_coord_t kManageRestartButtonHeight = 44;
constexpr lv_coord_t kForgetDialogWidth = 350;
constexpr lv_coord_t kForgetDialogHeight = 150;
constexpr lv_coord_t kForgetButtonWidth = 120;
constexpr lv_coord_t kForgetButtonHeight = 38;
constexpr lv_coord_t kForgetButtonInset = 28;
constexpr lv_coord_t kForgetNameTop = 42;
constexpr lv_coord_t kWifiDialogWidth = 450;
constexpr lv_coord_t kWifiDialogHeight = 200;
constexpr lv_coord_t kWifiDialogGap = 20;
constexpr lv_coord_t kWifiDialogInset = 20;
constexpr lv_coord_t kWifiFieldWidth = 300;
constexpr lv_coord_t kWifiFieldHeight = 44;
constexpr lv_coord_t kWifiFieldTop = 76;
constexpr lv_coord_t kWifiRevealWidth = 90;
constexpr lv_coord_t kWifiActionWidth = 120;
constexpr lv_coord_t kWifiActionHeight = 38;
constexpr lv_coord_t kWifiActionInset = 78;
constexpr lv_coord_t kWifiActionBottom = 14;
constexpr lv_coord_t kWifiListHeight = 320;
constexpr lv_coord_t kWifiNetworkRowHeight = 48;
constexpr lv_coord_t kWifiNetworkRowInset = 10;
constexpr const lv_font_t *kSettingsTitleFont = &lv_font_montserrat_20;
constexpr const lv_font_t *kSettingsSummaryFont = &lv_font_montserrat_16;
constexpr const lv_font_t *kSettingsStatusFont = &lv_font_montserrat_16;
constexpr const lv_font_t *kSettingsInputFont = &lv_font_montserrat_16;
constexpr const lv_font_t *kWifiTitleFont = &lv_font_montserrat_20;
constexpr const lv_font_t *kWifiBodyFont = &lv_font_montserrat_16;
constexpr const lv_font_t *kQuickTileFont = nullptr;
#endif
// Scale the existing native icon artwork, with its original 16px coordinates.
// The target picks the size at compile time; S3 coordinates remain unchanged.
constexpr lv_coord_t quick_icon_px(lv_coord_t pixels)
{
    return static_cast<lv_coord_t>((pixels * kQuickNativeIconSize + 8) / 16);
}

constexpr lv_coord_t kHomePillHitWidth = kHomePillWidth + 2 * kHomePillHitMargin;
constexpr lv_coord_t kHomePillMaxLift = 8;
#if CONFIG_IDF_TARGET_ESP32P4
constexpr uint32_t kHomePillSettleMs = 180;
#else
constexpr uint32_t kHomePillSettleMs = 120;
#endif
constexpr lv_opa_t kHomePillTouchGlowOpa = LV_OPA_20;
constexpr lv_opa_t kHomePillMaxGlowOpa = LV_OPA_50;
constexpr lv_coord_t kHomePillTouchGlowWidth = 6;
constexpr lv_coord_t kHomePillMaxGlowWidth = 16;
// Travel required to commit the bottom swipe. Brookesia only classifies a
// direction after 50px (direction_vertical in the 480x480 stylesheet), so this
// must exceed that or the owner is claimed on gestures that never resolve as UP.
// 80px is a comfortable flick and still far above a tap in the band.
constexpr int kHomeSwipeTravel = 80;
// Crop the captured app area to 90% x 90% about its centre, then render it at
// 1/kSnapshotScaleDivisor of the app's resolution. On this panel the observed
// 480x440 app area produces a 240x220 snapshot (~103 KiB).
constexpr uint32_t kSnapshotCropPercent = 90;
constexpr uint32_t kSnapshotScaleDivisor = 2;
constexpr uint32_t kIdentityRevealPercent = 10;
constexpr uint32_t kCrossoverCommitPercent = 50;
#if CONFIG_IDF_TARGET_ESP32P4
constexpr uint32_t kTransitionMs = 300;
constexpr uint32_t kCrossoverSettleMs = 300;
constexpr uint32_t kQuickAnimMs = 350;
#else
constexpr uint32_t kTransitionMs = 180;
constexpr uint32_t kCrossoverSettleMs = 250;
constexpr uint32_t kQuickAnimMs = 200;
#endif
constexpr lv_coord_t kQuickPanelSize = 4 * kQuickCell + 3 * kQuickGap + 2 * kQuickPad;
constexpr size_t kSystemPageDepthMax = 4;
// An lv_anim ready callback runs inside lv_timer_handler, before the display is
// refreshed, so the animation's final frame is still unpainted at that moment.
// Doing the Brookesia switch there costs the user that frame and makes the
// commit look like it overlaps the slide. One frame of slack lets each stage
// reach the panel before the next one starts.
constexpr uint32_t kCommitStageMs = 20;
constexpr char kCurrentCardKey[] = "shell.card";

ESP_Brookesia_Phone *s_phone = nullptr;
size_t s_current_index = 0;
bool s_switching = false;
bool s_quick_settings_open = false;
bool s_keyboard_open = false;
bool s_modal_open = false;
bool s_swallow_wake_touch = false;
bool s_home_gesture_active = false;
int s_last_app_before_settings = -1;  // -1 = launcher, >= 0 = app index
CrystalGestureOwner s_gesture_owner = CrystalGestureOwner::None;
ESP_Brookesia_Gesture *s_gesture = nullptr;
lv_obj_t *s_page_dots = nullptr;
lv_obj_t *s_home_pill_catcher = nullptr;
lv_obj_t *s_home_pill = nullptr;
struct HomePillFeedbackState {
    lv_coord_t lift = 0;
    lv_opa_t glow_opa = LV_OPA_TRANSP;
    lv_coord_t settle_start_lift = 0;
    lv_opa_t settle_start_glow_opa = LV_OPA_TRANSP;
};
HomePillFeedbackState s_home_pill_feedback;
lv_obj_t *s_quick_root = nullptr;
lv_obj_t *s_quick_panel = nullptr;
lv_obj_t *s_quick_brightness = nullptr;
lv_obj_t *s_quick_volume = nullptr;
lv_obj_t *s_quick_wifi = nullptr;
lv_obj_t *s_wifi_dialog = nullptr;
lv_obj_t *s_system_dialog = nullptr;
lv_obj_t *s_manage_apps_page = nullptr;
lv_obj_t *s_manage_apps_restart_dialog = nullptr;
lv_obj_t *s_wifi_page = nullptr;
lv_obj_t *s_wifi_page_list = nullptr;
lv_obj_t *s_wifi_page_status = nullptr;
lv_obj_t *s_wifi_scan_spinner = nullptr;
lv_obj_t *s_wifi_refresh = nullptr;
lv_timer_t *s_wifi_scan_timeout = nullptr;
bool s_wifi_scanning = false;
IWifi::Network s_wifi_rows[20] = {};
size_t s_wifi_row_count = 0;
lv_obj_t *s_system_page_stack[kSystemPageDepthMax] = {};
size_t s_system_page_depth = 0;
bool s_settings_back_gesture_saved = false;
bool s_settings_back_gesture_was_enabled = false;
lv_obj_t *s_ip_fields[5] = {};
lv_obj_t *s_ip_apply_status = nullptr;
lv_obj_t *s_root_network_row = nullptr;
lv_obj_t *s_root_settings_page = nullptr;
lv_obj_t *s_root_power_row = nullptr;
lv_obj_t *s_root_sound_row = nullptr;
lv_obj_t *s_root_region_row = nullptr;
lv_obj_t *s_network_wifi_row = nullptr;
lv_obj_t *s_network_details_row = nullptr;
lv_obj_t *s_network_page = nullptr;
lv_obj_t *s_connection_details_content = nullptr;
lv_obj_t *s_ethernet_page = nullptr;
lv_obj_t *s_ethernet_rows[8] = {};
lv_obj_t *s_network_ethernet_row = nullptr;
lv_obj_t *s_network_active_row = nullptr;
lv_timer_t *s_ethernet_timer = nullptr;
lv_obj_t *s_power_auto_dim_row = nullptr;
lv_obj_t *s_sound_alerts_row = nullptr;
lv_obj_t *s_region_auto_time_row = nullptr;
lv_obj_t *s_region_timezone_row = nullptr;
lv_obj_t *s_region_format_row = nullptr;
lv_obj_t *s_region_manual_time_row = nullptr;
lv_obj_t *s_region_location_row = nullptr;
struct LocationFormState {
    lv_obj_t *page = nullptr;
    lv_obj_t *city = nullptr;
    lv_obj_t *latitude = nullptr;
    lv_obj_t *longitude = nullptr;
    lv_obj_t *status = nullptr;
};
LocationFormState s_location_form;
struct ManualTimeFormState {
    lv_obj_t *page = nullptr;
    lv_obj_t *date = nullptr;
    lv_obj_t *clock = nullptr;
    lv_obj_t *status = nullptr;
};
ManualTimeFormState s_manual_time_form;
char s_wifi_selected[33] = {};
char s_wifi_connecting[33] = {};
void (*s_quick_after_close)() = nullptr;
lv_obj_t *s_quick_catcher = nullptr;
lv_coord_t s_quick_y_rest = 0;
lv_coord_t s_quick_y_hidden = 0;
bool s_quick_settling = false;
bool s_quick_closing = false;
static lv_coord_t s_quick_cols[] = {kQuickCell, kQuickCell, kQuickCell, kQuickCell, LV_GRID_TEMPLATE_LAST};
static lv_coord_t s_quick_rows[] = {kQuickCell, kQuickCell, kQuickCell, kQuickCell, LV_GRID_TEMPLATE_LAST};

enum class CardTransitionPhase {
    Idle,
    Dragging,
    Settling,
};

struct CardTransition {
    CardTransitionPhase phase = CardTransitionPhase::Idle;
    lv_obj_t *root = nullptr;
    lv_obj_t *incoming_card = nullptr;
    lv_obj_t *identity_icon = nullptr;
    lv_obj_t *identity_name = nullptr;
    size_t target_index = SIZE_MAX;
    lv_coord_t width = 0;
    lv_coord_t progress = 0;
    int direction = 0;
    bool commit = false;
};

CardTransition s_card_transition;

bool start_card(size_t index, bool animate = true);
lv_area_t active_app_area();
lv_img_dsc_t *downscale_crop(const lv_img_dsc_t *source, const lv_area_t &crop,
                             lv_coord_t dest_w, lv_coord_t dest_h);

void quick_snapshot_cleanup(lv_timer_t *timer)
{
    lv_obj_t *root = static_cast<lv_obj_t *>(timer->user_data);
    if (root != nullptr) {
        if (root == s_quick_root) {
            s_quick_root = nullptr;
            s_quick_panel = nullptr;
            s_quick_brightness = nullptr;
            s_quick_volume = nullptr;
            s_quick_wifi = nullptr;
            s_wifi_dialog = nullptr;
            crystal_keyboard_set_state_cb(nullptr, nullptr);
            s_quick_catcher = nullptr;
            s_quick_settling = false;
            s_quick_closing = false;
        }
        lv_obj_del(root);
        if (s_quick_after_close != nullptr) {
            void (*next)() = s_quick_after_close;
            s_quick_after_close = nullptr;
            next();
        }
    }
    lv_timer_del(timer);
}

void quick_anim_y(void *obj, int32_t y)
{
    lv_obj_set_y(static_cast<lv_obj_t *>(obj), static_cast<lv_coord_t>(y));
}

void quick_anim_ready(lv_anim_t *anim)
{
    if (anim == nullptr || s_quick_root == nullptr) return;
    s_quick_settling = false;
    if (s_quick_closing) {
        s_quick_settings_open = false;
        lv_timer_create(quick_snapshot_cleanup, 1, s_quick_root);
    } else if (s_quick_catcher == nullptr) {
        s_quick_catcher = lv_obj_create(s_quick_root);
        lv_obj_set_size(s_quick_catcher, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
        lv_obj_set_pos(s_quick_catcher, 0, 0);
        lv_obj_set_style_bg_opa(s_quick_catcher, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_quick_catcher, 0, 0);
        lv_obj_add_event_cb(s_quick_catcher, [](lv_event_t *) {
            if (s_gesture_owner == CrystalGestureOwner::QuickSettings || s_quick_settling) return;
            lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, s_quick_panel);
            lv_anim_set_exec_cb(&a, quick_anim_y); lv_anim_set_values(&a, lv_obj_get_y(s_quick_panel), s_quick_y_hidden);
            lv_anim_set_time(&a, kQuickAnimMs); lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
            lv_anim_set_ready_cb(&a, quick_anim_ready); s_quick_settling = true; s_quick_closing = true; lv_anim_start(&a);
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_move_background(s_quick_catcher);
        s_quick_settings_open = true;
    }
}

void quick_set_bar_from_touch(lv_obj_t *bar)
{
    if (bar == nullptr) return;
    lv_point_t point;
    lv_indev_get_point(lv_indev_get_act(), &point);
    lv_area_t area;
    lv_obj_get_coords(bar, &area);
    const int height = LV_MAX(1, lv_area_get_height(&area));
    int value = ((area.y2 - point.y) * 100) / height;
    value = LV_CLAMP(0, value, 100);
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
    if (bar == s_quick_brightness) {
        value = LV_CLAMP(kBrightnessUiMin, value, kBrightnessUiMax);
        crystal_brightness_set(static_cast<uint8_t>(value));
    } else if (bar == s_quick_volume) {
        if (crystal_hal_set_volume(value) && hal().storage != nullptr) {
            const uint8_t pct = static_cast<uint8_t>(value);
            hal().storage->set("volume", &pct, sizeof(pct));
        }
    }
}

void quick_bar_event(lv_event_t *event)
{
    if (event == nullptr) return;
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        quick_set_bar_from_touch(static_cast<lv_obj_t *>(lv_event_get_target(event)));
    }
}

void close_quick_settings(void (*then)())
{
    if (s_quick_panel == nullptr) { if (then != nullptr) then(); return; }
    s_quick_after_close = then;
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, s_quick_panel);
    lv_anim_set_exec_cb(&a, quick_anim_y); lv_anim_set_values(&a, lv_obj_get_y(s_quick_panel), s_quick_y_hidden);
    lv_anim_set_time(&a, kQuickAnimMs); lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, quick_anim_ready); s_quick_settling = true; s_quick_closing = true; lv_anim_start(&a);
}

#if CONFIG_IDF_TARGET_ESP32P4
void quick_tile_add_captions(lv_obj_t *tile)
{
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(tile, 8, 0);
    lv_obj_set_style_pad_right(tile, 8, 0);
    lv_obj_set_style_pad_row(tile, 8, 0);
    // Give title and status their own lines. Long SSIDs use an ellipsis rather
    // than wrapping and moving the icon or the rest of the tile's contents.
    for (unsigned line = 0; line < 2; ++line) {
        lv_obj_t *caption = lv_label_create(tile);
        lv_obj_set_size(caption, LV_PCT(100), lv_font_get_line_height(kQuickTileFont));
        lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(caption, kQuickTileFont, 0);
        lv_obj_set_style_text_color(caption, lv_color_hex(0xf2f4f7), 0);
    }
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
}
#endif

void quick_tile_set_text(lv_obj_t *tile, const char *text)
{
    if (tile == nullptr || text == nullptr) return;
    lv_obj_t *icon = lv_obj_get_child(tile, 0);
    if (icon == nullptr) return;
#if CONFIG_IDF_TARGET_ESP32P4
    const char *separator = strchr(text, '\n');
    if (separator != nullptr) {
        lv_obj_t *caption = lv_obj_get_child(tile, 1);
        lv_obj_t *status = lv_obj_get_child(tile, 2);
        if (caption != nullptr && status != nullptr) {
            lv_label_set_text_fmt(icon, "%.*s", static_cast<int>(separator - text), text);
            const char *status_separator = strchr(separator + 1, '\n');
            if (status_separator != nullptr) {
                lv_label_set_text_fmt(caption, "%.*s", static_cast<int>(status_separator - separator - 1), separator + 1);
                lv_label_set_text(status, status_separator + 1);
            } else {
                lv_label_set_text(caption, separator + 1);
                lv_label_set_text(status, "");
            }
            return;
        }
    }
#endif
    lv_label_set_text(icon, text);
}

bool create_quick_settings()
{
    if (s_quick_root != nullptr) return true;
    s_quick_root = lv_obj_create(lv_layer_top());
    if (s_quick_root == nullptr) return false;
    // Both overlays live on the top layer. Keep the keyboard alive underneath
    // and put the panel in front so dismissing it reveals the same input state.
    lv_obj_move_foreground(s_quick_root);
    lv_obj_set_size(s_quick_root, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
    s_quick_y_rest = active_app_area().y1 + kQuickGapTop;
    s_quick_y_hidden = s_quick_y_rest - kQuickPanelSize;
    lv_obj_set_pos(s_quick_root, 0, 0);
    lv_obj_set_style_bg_opa(s_quick_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_quick_root, 0, 0);
    lv_obj_set_style_pad_all(s_quick_root, 0, 0);
    lv_obj_clear_flag(s_quick_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scrollbar_mode(s_quick_root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_quick_root, LV_OBJ_FLAG_SCROLLABLE);

    s_quick_panel = lv_obj_create(s_quick_root);
    lv_obj_set_size(s_quick_panel, kQuickPanelSize, kQuickPanelSize);
    lv_obj_set_pos(s_quick_panel, lv_disp_get_hor_res(nullptr) - kQuickPanelSize - kQuickMargin, s_quick_y_hidden);
    lv_obj_set_style_bg_color(s_quick_panel, lv_color_hex(0x20242c), 0);
    lv_obj_set_style_bg_opa(s_quick_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_color(s_quick_panel, lv_color_hex(0x1b1e24), 0);
    lv_obj_set_style_bg_grad_dir(s_quick_panel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(s_quick_panel, 22, 0);
    lv_obj_set_style_border_width(s_quick_panel, 1, 0);
    lv_obj_set_style_border_side(s_quick_panel, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(s_quick_panel, lv_color_white(), 0);
    lv_obj_set_style_border_opa(s_quick_panel, 26, 0);
    lv_obj_set_style_pad_all(s_quick_panel, kQuickPad, 0);
    lv_obj_clear_flag(s_quick_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_quick_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_layout(s_quick_panel, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(s_quick_panel, s_quick_cols, s_quick_rows);
    lv_obj_set_style_pad_row(s_quick_panel, kQuickGap, 0);
    lv_obj_set_style_pad_column(s_quick_panel, kQuickGap, 0);

    auto tile = [](const char *text, lv_grid_align_t col_align, int col, int col_span, int row, int row_span) {
        lv_obj_t *obj = lv_btn_create(s_quick_panel);
        lv_obj_set_grid_cell(obj, col_align, col, col_span, LV_GRID_ALIGN_STRETCH, row, row_span);
        lv_obj_set_style_radius(obj, col_span > 1 ? 16 : 14, 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffff), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(obj, 31, LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x3b82f6), LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_CHECKED);
        lv_obj_set_style_transform_zoom(obj, 248, LV_STATE_PRESSED);
        lv_obj_t *label = lv_label_create(obj);
        lv_obj_center(label);
#if CONFIG_IDF_TARGET_ESP32P4
        lv_obj_set_style_text_font(label, kQuickIconFont, 0);
        if (strchr(text, '\n') != nullptr) {
            quick_tile_add_captions(obj);
        }
#endif
        lv_obj_set_style_text_color(label, lv_color_hex(0xf2f4f7), 0);
        quick_tile_set_text(obj, text);
        return obj;
    };
    char wifi_text[64] = {};
    wifi_tile_text(wifi_text, sizeof(wifi_text));
    s_quick_wifi = tile(wifi_text, LV_GRID_ALIGN_STRETCH, 0, 2, 0, 2);
    // Reflect the adapter's state immediately; the service event will refine
    // the label once connection status is known.
    if (hal().wifi != nullptr && hal().wifi->enabled()) {
        lv_obj_add_state(s_quick_wifi, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(s_quick_wifi, [](lv_event_t *) { close_quick_settings(settings_open_at_wifi); }, LV_EVENT_LONG_PRESSED, nullptr);
    lv_obj_add_event_cb(s_quick_wifi, [](lv_event_t *event) {
        lv_obj_t *tile = static_cast<lv_obj_t *>(lv_event_get_target(event));
        if (hal().wifi == nullptr) return;
        const bool enabled = !hal().wifi->enabled();
        hal().wifi->set_enabled(enabled);
        if (enabled) lv_obj_add_state(tile, LV_STATE_CHECKED);
        else lv_obj_clear_state(tile, LV_STATE_CHECKED);
    }, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_t *bt = tile(LV_SYMBOL_BLUETOOTH "\nBluetooth\nUnavailable", LV_GRID_ALIGN_STRETCH, 2, 2, 0, 2); lv_obj_set_style_bg_opa(bt, 15, 0); lv_obj_set_style_text_opa(lv_obj_get_child(bt, 0), LV_OPA_40, 0); lv_obj_add_state(bt, LV_STATE_DISABLED);
#if CONFIG_IDF_TARGET_ESP32P4
    lv_obj_set_style_text_opa(lv_obj_get_child(bt, 1), LV_OPA_40, 0);
    lv_obj_set_style_text_opa(lv_obj_get_child(bt, 2), LV_OPA_40, 0);
#endif
    s_quick_brightness = lv_bar_create(s_quick_panel); lv_obj_set_grid_cell(s_quick_brightness, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 2); lv_obj_set_style_radius(s_quick_brightness, 18, 0); lv_obj_set_style_bg_color(s_quick_brightness, lv_color_hex(0xffffff), LV_PART_MAIN); lv_obj_set_style_bg_opa(s_quick_brightness, 31, LV_PART_MAIN); lv_obj_set_style_bg_color(s_quick_brightness, lv_color_hex(0x3b82f6), LV_PART_INDICATOR); lv_obj_set_style_bg_opa(s_quick_brightness, LV_OPA_COVER, LV_PART_INDICATOR); lv_obj_set_style_radius(s_quick_brightness, 18, LV_PART_MAIN); lv_obj_set_style_radius(s_quick_brightness, 18, LV_PART_INDICATOR); lv_bar_set_range(s_quick_brightness, kBrightnessUiMin, kBrightnessUiMax); lv_bar_set_value(s_quick_brightness, crystal_brightness_level(), LV_ANIM_OFF); lv_obj_add_event_cb(s_quick_brightness, quick_bar_event, LV_EVENT_ALL, nullptr);
    // The bundled font has no sun glyph, so draw a native icon without
    // theme styles. Its bottom alignment matches the volume symbol exactly.
    lv_obj_t *brightness_icon = lv_obj_create(s_quick_brightness); lv_obj_remove_style_all(brightness_icon); lv_obj_set_size(brightness_icon, kQuickNativeIconSize, kQuickNativeIconSize); lv_obj_align(brightness_icon, LV_ALIGN_BOTTOM_MID, 0, -quick_icon_px(8)); lv_obj_clear_flag(brightness_icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *sun_core = lv_obj_create(brightness_icon); lv_obj_remove_style_all(sun_core); lv_obj_set_size(sun_core, quick_icon_px(8), quick_icon_px(8)); lv_obj_center(sun_core); lv_obj_set_style_bg_color(sun_core, lv_color_hex(0xf2f4f7), 0); lv_obj_set_style_bg_opa(sun_core, LV_OPA_COVER, 0); lv_obj_set_style_radius(sun_core, LV_RADIUS_CIRCLE, 0); lv_obj_clear_flag(sun_core, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    const lv_coord_t ray_pos[][2] = {{7, 0}, {7, 14}, {0, 7}, {14, 7}, {2, 2}, {12, 2}, {2, 12}, {12, 12}}; for (const auto &pos : ray_pos) { lv_obj_t *ray = lv_obj_create(brightness_icon); lv_obj_remove_style_all(ray); lv_obj_set_size(ray, quick_icon_px(2), quick_icon_px(2)); lv_obj_set_pos(ray, quick_icon_px(pos[0]), quick_icon_px(pos[1])); lv_obj_set_style_bg_color(ray, lv_color_hex(0xf2f4f7), 0); lv_obj_set_style_bg_opa(ray, LV_OPA_COVER, 0); lv_obj_set_style_radius(ray, LV_RADIUS_CIRCLE, 0); lv_obj_clear_flag(ray, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE); }
    s_quick_volume = lv_bar_create(s_quick_panel); lv_obj_set_grid_cell(s_quick_volume, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 2, 2); lv_obj_set_style_radius(s_quick_volume, 18, 0); lv_obj_set_style_bg_color(s_quick_volume, lv_color_hex(0xffffff), LV_PART_MAIN); lv_obj_set_style_bg_opa(s_quick_volume, 31, LV_PART_MAIN); lv_obj_set_style_radius(s_quick_volume, 18, LV_PART_MAIN); lv_obj_set_style_radius(s_quick_volume, 18, LV_PART_INDICATOR); lv_bar_set_range(s_quick_volume, 0, 100); lv_bar_set_value(s_quick_volume, crystal_hal_get_volume(), LV_ANIM_OFF); lv_obj_add_event_cb(s_quick_volume, quick_bar_event, LV_EVENT_ALL, nullptr);
    lv_obj_t *volume_icon = lv_label_create(s_quick_volume); lv_label_set_text(volume_icon, LV_SYMBOL_VOLUME_MID); lv_obj_align(volume_icon, LV_ALIGN_BOTTOM_MID, 0, -quick_icon_px(8)); lv_obj_set_style_text_color(volume_icon, lv_color_hex(0xf2f4f7), 0); lv_obj_clear_flag(volume_icon, LV_OBJ_FLAG_CLICKABLE);
#if CONFIG_IDF_TARGET_ESP32P4
    lv_obj_set_style_text_font(volume_icon, kQuickIconFont, 0);
#endif
    lv_obj_t *energy = tile(LV_SYMBOL_BATTERY_FULL, LV_GRID_ALIGN_STRETCH, 2, 1, 2, 1);
    // Draw the leaf natively; the board font does not reliably provide the
    // Font Awesome leaf glyph used by some LVGL symbol builds.
    lv_obj_t *energy_leaf = lv_obj_create(energy); lv_obj_remove_style_all(energy_leaf); lv_obj_set_size(energy_leaf, quick_icon_px(20), quick_icon_px(18)); lv_obj_align(energy_leaf, LV_ALIGN_CENTER, quick_icon_px(10), quick_icon_px(2)); lv_obj_clear_flag(energy_leaf, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *leaf_left = lv_obj_create(energy_leaf); lv_obj_remove_style_all(leaf_left); lv_obj_set_size(leaf_left, quick_icon_px(7), quick_icon_px(12)); lv_obj_set_pos(leaf_left, quick_icon_px(3), quick_icon_px(2)); lv_obj_set_style_bg_color(leaf_left, lv_color_hex(0x9be15b), 0); lv_obj_set_style_bg_opa(leaf_left, LV_OPA_COVER, 0); lv_obj_set_style_radius(leaf_left, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_transform_angle(leaf_left, 350, 0); lv_obj_clear_flag(leaf_left, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *leaf_right = lv_obj_create(energy_leaf); lv_obj_remove_style_all(leaf_right); lv_obj_set_size(leaf_right, quick_icon_px(7), quick_icon_px(12)); lv_obj_set_pos(leaf_right, quick_icon_px(9), quick_icon_px(2)); lv_obj_set_style_bg_color(leaf_right, lv_color_hex(0x70bd43), 0); lv_obj_set_style_bg_opa(leaf_right, LV_OPA_COVER, 0); lv_obj_set_style_radius(leaf_right, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_transform_angle(leaf_right, 550, 0); lv_obj_clear_flag(leaf_right, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    if (crystal_power_saving_enabled()) lv_obj_add_state(energy, LV_STATE_CHECKED);
    lv_obj_add_event_cb(energy, [](lv_event_t *e) { lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_target(e)); const bool on = !lv_obj_has_state(obj, LV_STATE_CHECKED); if (on) lv_obj_add_state(obj, LV_STATE_CHECKED); else lv_obj_clear_state(obj, LV_STATE_CHECKED); crystal_power_set_saving(on); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *gear = tile(LV_SYMBOL_SETTINGS, LV_GRID_ALIGN_STRETCH, 3, 1, 2, 1);
    lv_obj_add_event_cb(gear, [](lv_event_t *) { close_quick_settings(settings_open); }, LV_EVENT_CLICKED, nullptr);
    // Auto Dimming. Sun outline plus rays and an "A", drawn natively for the same
    // reason as the brightness sun and the energy leaf: the bundled font has no
    // sun glyph. Monochrome to match the other tiles; the leaf is deliberately
    // the only coloured element on the panel.
    lv_obj_t *auto_dim = tile("", LV_GRID_ALIGN_STRETCH, 2, 1, 3, 1);
    // Match the brightness sun at the target's native icon size.
    lv_obj_t *dim_icon = lv_obj_create(auto_dim); lv_obj_remove_style_all(dim_icon); lv_obj_set_size(dim_icon, kQuickNativeIconSize, kQuickNativeIconSize); lv_obj_align(dim_icon, LV_ALIGN_CENTER, -quick_icon_px(6), 0); lv_obj_clear_flag(dim_icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *dim_disc = lv_obj_create(dim_icon); lv_obj_remove_style_all(dim_disc); lv_obj_set_size(dim_disc, quick_icon_px(9), quick_icon_px(9)); lv_obj_center(dim_disc); lv_obj_set_style_bg_opa(dim_disc, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(dim_disc, quick_icon_px(2), 0); lv_obj_set_style_border_color(dim_disc, lv_color_hex(0xf2f4f7), 0); lv_obj_set_style_border_opa(dim_disc, LV_OPA_COVER, 0); lv_obj_set_style_radius(dim_disc, LV_RADIUS_CIRCLE, 0); lv_obj_clear_flag(dim_disc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    // Cardinal rays as 2x3 / 3x2 bars, diagonals as 2x2 dots. Same ray geometry
    // as the brightness sun so the two read as a family.
    const lv_coord_t dim_bars[][4] = {{7, 0, 2, 3}, {7, 13, 2, 3}, {0, 7, 3, 2}, {13, 7, 3, 2}};
    for (const auto &bar : dim_bars) { lv_obj_t *ray = lv_obj_create(dim_icon); lv_obj_remove_style_all(ray); lv_obj_set_pos(ray, quick_icon_px(bar[0]), quick_icon_px(bar[1])); lv_obj_set_size(ray, quick_icon_px(bar[2]), quick_icon_px(bar[3])); lv_obj_set_style_bg_color(ray, lv_color_hex(0xf2f4f7), 0); lv_obj_set_style_bg_opa(ray, LV_OPA_COVER, 0); lv_obj_set_style_radius(ray, 1, 0); lv_obj_clear_flag(ray, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE); }
    const lv_coord_t dim_dots[][2] = {{2, 2}, {12, 2}, {2, 12}, {12, 12}};
    for (const auto &dot : dim_dots) { lv_obj_t *ray = lv_obj_create(dim_icon); lv_obj_remove_style_all(ray); lv_obj_set_pos(ray, quick_icon_px(dot[0]), quick_icon_px(dot[1])); lv_obj_set_size(ray, quick_icon_px(2), quick_icon_px(2)); lv_obj_set_style_bg_color(ray, lv_color_hex(0xf2f4f7), 0); lv_obj_set_style_bg_opa(ray, LV_OPA_COVER, 0); lv_obj_set_style_radius(ray, LV_RADIUS_CIRCLE, 0); lv_obj_clear_flag(ray, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE); }
    lv_obj_t *dim_a = lv_label_create(auto_dim); lv_label_set_text(dim_a, "A"); lv_obj_set_style_text_color(dim_a, lv_color_hex(0xf2f4f7), 0); lv_obj_align(dim_a, LV_ALIGN_CENTER, quick_icon_px(6), quick_icon_px(4)); lv_obj_clear_flag(dim_a, LV_OBJ_FLAG_CLICKABLE);
#if CONFIG_IDF_TARGET_ESP32P4
    lv_obj_set_style_text_font(dim_a, kQuickTileFont, 0);
#endif
    if (crystal_power_auto_dim_enabled()) lv_obj_add_state(auto_dim, LV_STATE_CHECKED);
    lv_obj_add_event_cb(auto_dim, [](lv_event_t *e) {
        lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_target(e));
        const bool on = !lv_obj_has_state(obj, LV_STATE_CHECKED);
        if (on) lv_obj_add_state(obj, LV_STATE_CHECKED);
        else lv_obj_clear_state(obj, LV_STATE_CHECKED);
        crystal_power_set_auto_dim(on);
    }, LV_EVENT_CLICKED, nullptr);
    s_quick_settings_open = true;
    return true;
}

void update_quick_settings(const ESP_Brookesia_GestureInfo_t &info)
{
    if (s_quick_root == nullptr) return;
    const int dy = info.stop_y - info.start_y;
    const lv_coord_t origin = s_quick_catcher != nullptr ? s_quick_y_rest : s_quick_y_hidden;
    const lv_coord_t y = LV_CLAMP(s_quick_y_hidden, static_cast<lv_coord_t>(origin + dy), s_quick_y_rest);
    lv_obj_set_y(s_quick_panel, y);
}

void release_quick_settings(const ESP_Brookesia_GestureInfo_t &info)
{
    if (s_quick_root == nullptr) return;
    const lv_coord_t offset = static_cast<lv_coord_t>(lv_obj_get_y(s_quick_panel) - s_quick_y_hidden);
    const bool open = (info.stop_y - info.start_y) > kQuickPanelSize / 2 || offset > kQuickPanelSize / 2;
    lv_anim_t animation; lv_anim_init(&animation); lv_anim_set_var(&animation, s_quick_panel);
    lv_anim_set_exec_cb(&animation, quick_anim_y); lv_anim_set_values(&animation, lv_obj_get_y(s_quick_panel), open ? s_quick_y_rest : s_quick_y_hidden);
    lv_anim_set_time(&animation, kQuickAnimMs); lv_anim_set_path_cb(&animation, lv_anim_path_ease_out); lv_anim_set_ready_cb(&animation, quick_anim_ready); s_quick_settling = true; s_quick_closing = !open; lv_anim_start(&animation);
}

bool crossover_threshold_reached(lv_coord_t progress, lv_coord_t width,
                                 uint32_t percent)
{
    if (width <= 0) {
        return false;
    }
    const lv_coord_t threshold = LV_MAX(1, static_cast<lv_coord_t>(
        (static_cast<int32_t>(width) * percent) / 100));
    return progress >= threshold;
}

bool os_owns_gesture()
{
    return s_gesture_owner == CrystalGestureOwner::AppSwitch ||
           s_gesture_owner == CrystalGestureOwner::QuickSettings ||
           s_gesture_owner == CrystalGestureOwner::Navigation;
}

bool home_pill_target_contains(lv_coord_t x, lv_coord_t y)
{
    const lv_coord_t display_width = lv_disp_get_hor_res(nullptr);
    const lv_coord_t display_height = lv_disp_get_ver_res(nullptr);
    const lv_coord_t left = (display_width - kHomePillHitWidth) / 2;
    return x >= left && x < left + kHomePillHitWidth &&
           y >= display_height - kBottomBand && y < display_height;
}

void apply_home_pill_feedback(lv_coord_t lift, lv_opa_t glow_opa)
{
    s_home_pill_feedback.lift = lift;
    s_home_pill_feedback.glow_opa = glow_opa;
    if (s_home_pill == nullptr) {
        return;
    }

    lv_obj_align(s_home_pill, LV_ALIGN_BOTTOM_MID, 0, -kHomePillInset - lift);
    lv_coord_t glow_width = 0;
    if (glow_opa <= kHomePillTouchGlowOpa) {
        glow_width = (kHomePillTouchGlowWidth * glow_opa) / kHomePillTouchGlowOpa;
    } else {
        glow_width = kHomePillTouchGlowWidth +
            ((kHomePillMaxGlowWidth - kHomePillTouchGlowWidth) *
             (glow_opa - kHomePillTouchGlowOpa)) /
                (kHomePillMaxGlowOpa - kHomePillTouchGlowOpa);
    }
    lv_obj_set_style_shadow_width(s_home_pill, glow_width, 0);
    lv_obj_set_style_shadow_opa(s_home_pill, glow_opa, 0);
}

void home_pill_settle_anim(void *, int32_t progress)
{
    const int32_t remaining = 256 - progress;
    apply_home_pill_feedback(
        static_cast<lv_coord_t>((s_home_pill_feedback.settle_start_lift * remaining) / 256),
        static_cast<lv_opa_t>((s_home_pill_feedback.settle_start_glow_opa * remaining) / 256));
}

void settle_home_pill_feedback()
{
    lv_anim_del(&s_home_pill_feedback, home_pill_settle_anim);
    if (s_home_pill_feedback.lift == 0 && s_home_pill_feedback.glow_opa == LV_OPA_TRANSP) {
        return;
    }

    s_home_pill_feedback.settle_start_lift = s_home_pill_feedback.lift;
    s_home_pill_feedback.settle_start_glow_opa = s_home_pill_feedback.glow_opa;
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, &s_home_pill_feedback);
    lv_anim_set_exec_cb(&animation, home_pill_settle_anim);
    lv_anim_set_values(&animation, 0, 256);
    lv_anim_set_time(&animation, kHomePillSettleMs);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

void begin_home_pill_feedback()
{
    lv_anim_del(&s_home_pill_feedback, home_pill_settle_anim);
    apply_home_pill_feedback(0, kHomePillTouchGlowOpa);
}

void update_home_pill_feedback(const ESP_Brookesia_GestureInfo_t &info)
{
    int32_t travel = info.start_y - info.stop_y;
    if (travel < 0) travel = 0;
    if (travel > kHomeSwipeTravel) travel = kHomeSwipeTravel;

    const lv_coord_t lift = static_cast<lv_coord_t>((travel * kHomePillMaxLift) / kHomeSwipeTravel);
    const lv_opa_t glow_opa = static_cast<lv_opa_t>(
        kHomePillTouchGlowOpa +
        (travel * (kHomePillMaxGlowOpa - kHomePillTouchGlowOpa)) / kHomeSwipeTravel);
    apply_home_pill_feedback(lift, glow_opa);
}

// The pill hides when the keyboard is up, because the keyboard occupies the
// bottom edge and the arbiter hands the band to the app while it is open, so the
// cue would be pointing at a gesture that does not fire. It stays visible
// everywhere else, including the launcher and system pages: a bottom swipe on a
// system page still peels one layer via shell_consume_back().
void update_home_pill()
{
    if (s_home_pill == nullptr) {
        return;
    }
    // Hide when keyboard is up, because the keyboard occupies the bottom edge and
    // the arbiter hands the band to the app, so the cue would point at a gesture
    // that does not fire. The launcher no longer needs a special case: Brookesia's
    // bottom indicator bar is disabled in the stylesheet (see main.cpp), so this is
    // the only pill on every screen.
    if (s_keyboard_open) {
        ESP_LOGD(TAG, "Hiding pill: keyboard up");
        lv_anim_del(&s_home_pill_feedback, home_pill_settle_anim);
        apply_home_pill_feedback(0, LV_OPA_TRANSP);
        if (s_home_pill_catcher != nullptr) {
            lv_obj_add_flag(s_home_pill_catcher, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(s_home_pill, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    ESP_LOGD(TAG, "Showing pill: depth=%zu", s_system_page_depth);
    if (s_home_pill_catcher != nullptr) {
        lv_obj_clear_flag(s_home_pill_catcher, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_home_pill_catcher);
    }
    lv_obj_clear_flag(s_home_pill, LV_OBJ_FLAG_HIDDEN);
    // System pages are created on lv_layer_top() after the pill, so they cover
    // it on z-order alone. Re-front it whenever it should be showing. Dialogs
    // and toasts are kept above it by their own foreground handling
    // (DESIGN.md layers 5 and 6), so this is only called where the pill must be
    // on top.
    lv_obj_move_foreground(s_home_pill);
}

void update_page_dots()
{
    if (s_page_dots == nullptr) {
        return;
    }
    const size_t count = crystal_registry_installed_count();

    // The installed-app list can grow while Manage Apps is open. The overlay
    // used to create its dots only during shell initialization, leaving the
    // newly installed card without an indicator after a live enable.
    while (lv_obj_get_child_cnt(s_page_dots) < count) {
        lv_obj_t *dot = lv_obj_create(s_page_dots);
        if (dot == nullptr) break;
        lv_obj_set_size(dot, 5, 5);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_white(), 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    while (lv_obj_get_child_cnt(s_page_dots) > count) {
        lv_obj_del(lv_obj_get_child(s_page_dots, -1));
    }
    lv_obj_set_width(s_page_dots, static_cast<lv_coord_t>(count * 9));
    for (size_t i = 0; i < count; ++i) {
        lv_obj_t *dot = lv_obj_get_child(s_page_dots, static_cast<int32_t>(i));
        if (dot != nullptr) {
            lv_obj_set_style_bg_opa(dot, i == s_current_index ? LV_OPA_COVER : LV_OPA_40, 0);
        }
    }
}

bool init_indicator_overlay()
{
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

    // Reserve a centered 24 px-high target for system navigation. LVGL focuses
    // text areas during the initial press, before Brookesia has classified any
    // movement, so the shell must win hit testing immediately. PRESS_LOCK keeps
    // the touch assigned here after the finger moves above the target.
    s_home_pill_catcher = lv_obj_create(lv_layer_top());
    if (s_home_pill_catcher == nullptr) {
        return false;
    }
    lv_obj_set_size(s_home_pill_catcher, kHomePillHitWidth, kBottomBand);
    lv_obj_set_style_bg_opa(s_home_pill_catcher, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_home_pill_catcher, 0, 0);
    lv_obj_set_style_shadow_width(s_home_pill_catcher, 0, 0);
    lv_obj_set_style_pad_all(s_home_pill_catcher, 0, 0);
    lv_obj_add_flag(s_home_pill_catcher, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(s_home_pill_catcher,
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_align(s_home_pill_catcher, LV_ALIGN_BOTTOM_MID, 0, 0);

    // Home pill. Crystal draws its own because Brookesia's was not a resting hint:
    // size_min is RECT(0, 10), zero width, so it only existed while a drag stretched
    // it. This one rests visible as the discoverability cue for the bottom edge,
    // sized to the page dots' visual weight rather than to Brookesia's 240px
    // maximum. Brookesia's is disabled outright in the stylesheet (see main.cpp).
    s_home_pill = lv_obj_create(lv_layer_top());
    if (s_home_pill == nullptr) {
        return false;
    }
    lv_obj_set_size(s_home_pill, kHomePillWidth, kHomePillHeight);
    lv_obj_set_style_radius(s_home_pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_home_pill, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_home_pill, LV_OPA_50, 0);
    // Add dark border for visibility on light backgrounds, matching iOS design
    lv_obj_set_style_border_width(s_home_pill, 1, 0);
    lv_obj_set_style_border_color(s_home_pill, lv_color_black(), 0);
    lv_obj_set_style_border_opa(s_home_pill, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(s_home_pill, lv_color_white(), 0);
    lv_obj_set_style_shadow_width(s_home_pill, 0, 0);
    lv_obj_set_style_shadow_opa(s_home_pill, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_home_pill, 0, 0);
    // Input belongs to the larger transparent catcher; the pill is visual only.
    lv_obj_clear_flag(s_home_pill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_home_pill, LV_ALIGN_BOTTOM_MID, 0, -kHomePillInset);
    update_home_pill();
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

    // The outgoing card is paused, not destroyed, so its field never fires
    // DELETE and the keyboard would survive on the top layer into the next card.
    crystal_shell_front_layer_changed();

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

    const lv_coord_t reveal_progress = LV_MAX(1, static_cast<lv_coord_t>(
        (static_cast<int32_t>(transition.width) * kIdentityRevealPercent) / 100));
    const lv_coord_t opaque_progress = LV_MAX(reveal_progress + 1, static_cast<lv_coord_t>(
        (static_cast<int32_t>(transition.width) * kCrossoverCommitPercent) / 100));
    const lv_coord_t icon_half_width = transition.identity_icon != nullptr
        ? static_cast<lv_coord_t>(lv_obj_get_width(transition.identity_icon) / 2) : 0;
    lv_coord_t screen_center;
    if (transition.progress <= reveal_progress) {
        screen_center = transition.direction > 0
            ? static_cast<lv_coord_t>(-icon_half_width)
            : static_cast<lv_coord_t>(transition.width + icon_half_width);
    } else if (transition.progress < opaque_progress) {
        const int32_t range = opaque_progress - reveal_progress;
        const int32_t elapsed = transition.progress - reveal_progress;
        const lv_coord_t start = transition.direction > 0
            ? static_cast<lv_coord_t>(-icon_half_width)
            : static_cast<lv_coord_t>(transition.width + icon_half_width);
        const lv_coord_t end = transition.direction > 0
            ? static_cast<lv_coord_t>(opaque_progress / 2)
            : static_cast<lv_coord_t>(transition.width - opaque_progress / 2);
        screen_center = static_cast<lv_coord_t>(start +
            (static_cast<int32_t>(end - start) * elapsed) / range);
    } else {
        screen_center = transition.direction > 0
            ? static_cast<lv_coord_t>(transition.progress / 2)
            : static_cast<lv_coord_t>(transition.width - transition.progress / 2);
    }
    const lv_coord_t card_center = static_cast<lv_coord_t>(screen_center - x);
    if (transition.identity_icon != nullptr) {
        lv_obj_set_x(transition.identity_icon,
                     static_cast<lv_coord_t>(card_center - lv_obj_get_width(transition.identity_icon) / 2));
    }
    if (transition.identity_name != nullptr) {
        lv_obj_set_x(transition.identity_name,
                     static_cast<lv_coord_t>(card_center - lv_obj_get_width(transition.identity_name) / 2));
    }

    const bool identity_visible = transition.progress >= reveal_progress;
    if (transition.identity_icon != nullptr) {
        if (identity_visible) {
            lv_obj_clear_flag(transition.identity_icon, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(transition.identity_icon, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (transition.identity_name != nullptr) {
        if (identity_visible) {
            lv_obj_clear_flag(transition.identity_name, LV_OBJ_FLAG_HIDDEN);
            const int32_t fade_progress = LV_CLAMP(
                0, static_cast<int32_t>(transition.progress - reveal_progress),
                static_cast<int32_t>(opaque_progress - reveal_progress));
            const lv_opa_t opacity = static_cast<lv_opa_t>(
                (fade_progress * LV_OPA_COVER) / (opaque_progress - reveal_progress));
            lv_obj_set_style_opa(transition.identity_name, opacity, 0);
        } else {
            lv_obj_add_flag(transition.identity_name, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void finish_card_transition(lv_anim_t *)
{
    CardTransition &transition = s_card_transition;
    const bool committed = transition.commit;
    const size_t active_index = s_current_index;

    if (transition.root != nullptr) {
        lv_obj_del(transition.root);
    }
    transition = CardTransition{};
    ESP_LOGI(TAG, "card crossover %s; active=%u, PSRAM free=%u",
             committed ? "committed" : "cancelled",
             static_cast<unsigned>(active_index + 1),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

// Stage 3: the destination is live and has had a frame to draw under the
// overlay, so the overlay can go without exposing a half-built tree.
void card_reveal_cb(lv_timer_t *timer)
{
    lv_timer_del(timer);
    finish_card_transition(nullptr);
}

// Stage 2: the card has been presented at full width. Now run the Brookesia
// lifecycle -- A onPause/onDestroy, B onCreate/onResume -- behind the card.
void card_commit_cb(lv_timer_t *timer)
{
    lv_timer_del(timer);
    CardTransition &transition = s_card_transition;
    if (transition.phase != CardTransitionPhase::Settling || !transition.commit) {
        finish_card_transition(nullptr);
        return;
    }

    const bool started = transition.target_index < crystal_registry_installed_count() &&
                         start_card(transition.target_index, false);
    if (!started) {
        transition.commit = false;
        ESP_LOGE(TAG, "card commit failed after cover; keeping current app");
        finish_card_transition(nullptr);
        return;
    }
    lv_timer_create(card_reveal_cb, kCommitStageMs, nullptr);
}

// Stage 1: the slide has finished in the object tree but has not been flushed
// yet. Yield so the full-width card reaches the panel before anything else.
void schedule_card_commit(lv_anim_t *)
{
    CardTransition &transition = s_card_transition;
    if (transition.phase != CardTransitionPhase::Settling || !transition.commit) {
        finish_card_transition(nullptr);
        return;
    }
    lv_timer_create(card_commit_cb, kCommitStageMs, nullptr);
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
    if (width <= 0 || height <= 0) {
        return false;
    }

    lv_obj_t *root = lv_obj_create(lv_layer_top());
    if (root == nullptr) {
        return false;
    }
    lv_obj_set_size(root, width, height);
    lv_obj_set_pos(root, app_area.x1, app_area.y1);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *incoming_card = lv_obj_create(root);
    if (incoming_card == nullptr) {
        lv_obj_del(root);
        return false;
    }

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

    lv_obj_t *identity_icon = nullptr;
    lv_obj_t *identity_name = nullptr;
    CrystalApp *target_app = crystal_registry_installed_app(target_index);
    if (target_app != nullptr) {
        const void *icon_resource = target_app->getLauncherIcon().resource;
        if (icon_resource != nullptr) {
            identity_icon = lv_obj_create(incoming_card);
            if (identity_icon != nullptr) {
                lv_obj_remove_style_all(identity_icon);
                lv_obj_set_style_bg_opa(identity_icon, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(identity_icon, 0, 0);
                lv_obj_clear_flag(identity_icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_t *icon_image = lv_img_create(identity_icon);
                if (icon_image != nullptr) {
                    lv_img_set_src(icon_image, icon_resource);
                    // Measure the zoomed artwork, not its unscaled source bounds.
                    lv_img_set_size_mode(icon_image, LV_IMG_SIZE_MODE_REAL);
#if CONFIG_IDF_TARGET_ESP32P4
                    lv_img_set_zoom(icon_image, LV_IMG_ZOOM_NONE * 3 / 2);
#endif
                    lv_obj_refr_size(icon_image);
                    const lv_coord_t icon_width = lv_obj_get_width(icon_image);
                    const lv_coord_t icon_height = lv_obj_get_height(icon_image);
                    lv_obj_set_size(identity_icon, icon_width, icon_height);
                    lv_obj_set_style_radius(identity_icon, std::min(icon_width, icon_height) / 5, 0);
                    lv_obj_set_style_clip_corner(identity_icon, true, 0);
                    lv_obj_center(icon_image);
#if CONFIG_IDF_TARGET_ESP32P4
                    const lv_coord_t label_height = lv_font_get_line_height(&lv_font_montserrat_28);
                    const lv_coord_t group_height = icon_height + 20 + label_height;
                    lv_obj_set_y(identity_icon, static_cast<lv_coord_t>((height - group_height) / 2));
#else
                    lv_obj_set_y(identity_icon, static_cast<lv_coord_t>((height - icon_height) / 2 - 34));
#endif
                    lv_obj_clear_flag(icon_image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
                } else {
                    lv_obj_del(identity_icon);
                    identity_icon = nullptr;
                }
                if (identity_icon != nullptr) {
                    lv_obj_set_style_opa(identity_icon, LV_OPA_COVER, 0);
                    lv_obj_add_flag(identity_icon, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
        identity_name = lv_label_create(incoming_card);
        if (identity_name != nullptr) {
            lv_obj_set_width(identity_name, static_cast<lv_coord_t>(width - 40));
            lv_label_set_long_mode(identity_name, LV_LABEL_LONG_DOT);
            lv_label_set_text(identity_name, target_app->getName());
#if CONFIG_IDF_TARGET_ESP32P4
            lv_obj_set_style_text_font(identity_name, &lv_font_montserrat_28, 0);
#else
            lv_obj_set_style_text_font(identity_name, &lv_font_montserrat_20, 0);
#endif
            lv_obj_set_style_text_color(identity_name, lv_color_hex(0x1C2A36), 0);
            lv_obj_set_style_text_align(identity_name, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_opa(identity_name, LV_OPA_TRANSP, 0);
#if CONFIG_IDF_TARGET_ESP32P4
            const lv_coord_t label_height = lv_font_get_line_height(&lv_font_montserrat_28);
            const lv_coord_t icon_height = identity_icon != nullptr ? lv_obj_get_height(identity_icon) : 0;
            const lv_coord_t group_height = icon_height + 20 + label_height;
            lv_obj_set_y(identity_name, static_cast<lv_coord_t>((height - group_height) / 2 + icon_height + 20));
#else
            lv_obj_set_y(identity_name, static_cast<lv_coord_t>(
                (height - lv_obj_get_height(identity_name)) / 2 + 42));
#endif
            lv_obj_add_flag(identity_name, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(identity_name, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        }
    }
    // Resolve the image descriptor and label dimensions once before the first
    // progress update. Per-frame layout would make the drag unnecessarily heavy.
    lv_obj_update_layout(incoming_card);

    s_card_transition.phase = CardTransitionPhase::Dragging;
    s_card_transition.root = root;
    s_card_transition.incoming_card = incoming_card;
    s_card_transition.identity_icon = identity_icon;
    s_card_transition.identity_name = identity_name;
    s_card_transition.target_index = target_index;
    s_card_transition.width = width;
    s_card_transition.direction = direction;

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
}

void settle_card_transition(bool commit)
{
    CardTransition &transition = s_card_transition;
    if (transition.phase != CardTransitionPhase::Dragging) {
        return;
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
    lv_anim_set_ready_cb(&animation, commit ? schedule_card_commit : finish_card_transition);
    lv_anim_start(&animation);
}

void on_installed_app_removed(size_t removed_index)
{
    const size_t count = crystal_registry_installed_count();
    if (s_current_index > removed_index) --s_current_index;
    if (count == 0) s_current_index = 0;
    else if (s_current_index >= count) s_current_index = count - 1;
    if (s_last_app_before_settings == static_cast<int>(removed_index)) {
        s_last_app_before_settings = -1; // The return target was uninstalled.
    } else if (s_last_app_before_settings > static_cast<int>(removed_index)) {
        --s_last_app_before_settings;
    }
    update_page_dots();
    if (count != 0) (void)persist_current_card();
    update_home_pill();
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
            update_home_pill();
            return;
        }
    }
    // If we get here, the app start event wasn't for any of our cards, which
    // means we're switching to the launcher (getActiveApp() will be nullptr).
    // Update the pill visibility so it hides on launcher.
    update_home_pill();
}

void on_gesture_press(lv_event_t *event)
{
    s_gesture_owner = CrystalGestureOwner::None;
    s_swallow_wake_touch = crystal_core_consume_wake_touch() || crystal_boot_screen_active();
    if (s_swallow_wake_touch && s_gesture != nullptr) {
        (void)s_gesture->setMaskObjectVisible(true);
    }

    // Use the same target as the LVGL catcher so feedback and navigation cannot
    // begin from app-owned space.
    auto *info = static_cast<ESP_Brookesia_GestureInfo_t *>(lv_event_get_param(event));
    if (!s_swallow_wake_touch && info != nullptr &&
            home_pill_target_contains(info->start_x, info->start_y) &&
            s_home_pill != nullptr && !lv_obj_has_flag(s_home_pill, LV_OBJ_FLAG_HIDDEN)) {
        s_home_gesture_active = true;
        begin_home_pill_feedback();
        ESP_LOGD(TAG, "Bottom edge touch started, blocking input");
    } else {
        s_home_gesture_active = false;
    }
}

void on_gesture_pressing(lv_event_t *event)
{
    auto *info = static_cast<ESP_Brookesia_GestureInfo_t *>(lv_event_get_param(event));
    if (info == nullptr || s_swallow_wake_touch) {
        return;
    }
    if (s_home_gesture_active) {
        update_home_pill_feedback(*info);
    }
    if (s_gesture_owner == CrystalGestureOwner::AppSwitch) {
        update_card_transition(*info);
        return;
    }
    if (s_gesture_owner == CrystalGestureOwner::QuickSettings) {
        lv_area_t panel_area{};
        if (s_quick_panel != nullptr) lv_obj_get_coords(s_quick_panel, &panel_area);
        const bool inside_panel = s_quick_catcher != nullptr && s_quick_panel != nullptr &&
                                  info->start_x >= panel_area.x1 && info->start_x <= panel_area.x2 &&
                                  info->start_y >= panel_area.y1 && info->start_y <= panel_area.y2;
        if (inside_panel) return;
        if (!s_quick_settings_open) (void)create_quick_settings();
        update_quick_settings(*info);
        return;
    }
    if (s_gesture_owner != CrystalGestureOwner::None ||
            info->direction == ESP_BROOKESIA_GESTURE_DIR_NONE) {
        return;
    }

    if (s_modal_open || crystal_manage_apps_page_confirmation_open()) {
        s_gesture_owner = CrystalGestureOwner::App;
    } else if (s_quick_settings_open) {
        // Controls inside the panel (brightness, volume, tiles) own their
        // vertical drags. Only an upward drag that starts outside the panel
        // dismisses Quick Settings.
        lv_area_t panel_area{};
        if (s_quick_panel != nullptr) lv_obj_get_coords(s_quick_panel, &panel_area);
        const bool inside_panel = s_quick_catcher != nullptr && s_quick_panel != nullptr &&
                                  info->start_x >= panel_area.x1 && info->start_x <= panel_area.x2 &&
                                  info->start_y >= panel_area.y1 && info->start_y <= panel_area.y2;
        s_gesture_owner = (inside_panel || info->direction == ESP_BROOKESIA_GESTURE_DIR_UP)
            ? CrystalGestureOwner::QuickSettings : CrystalGestureOwner::App;
    } else if (s_keyboard_open) {
        s_gesture_owner = CrystalGestureOwner::App;
    } else if (s_home_gesture_active && info->direction == ESP_BROOKESIA_GESTURE_DIR_UP) {
        // Note: s_home_gesture_active is already set in on_gesture_press
        s_gesture_owner = CrystalGestureOwner::Navigation;
    } else if (s_system_page_depth != 0 || s_switching ||
               s_card_transition.phase != CardTransitionPhase::Idle) {
        s_gesture_owner = CrystalGestureOwner::App;
    } else {
        const bool from_left = (info->start_area & ESP_BROOKESIA_GESTURE_AREA_LEFT_EDGE) != 0;
        const bool from_right = (info->start_area & ESP_BROOKESIA_GESTURE_AREA_RIGHT_EDGE) != 0;
        const bool horizontal_edge =
            (from_left && info->direction == ESP_BROOKESIA_GESTURE_DIR_RIGHT) ||
            (from_right && info->direction == ESP_BROOKESIA_GESTURE_DIR_LEFT);
        const bool in_quick_corner = info->start_x >= lv_disp_get_hor_res(nullptr) - kQuickCornerWidth;
        const bool top_pull = info->start_y < kTopBand && in_quick_corner &&
                              info->direction == ESP_BROOKESIA_GESTURE_DIR_DOWN;
        if (top_pull) {
            s_gesture_owner = CrystalGestureOwner::QuickSettings;
        } else if (s_keyboard_open) {
            s_gesture_owner = CrystalGestureOwner::App;
        } else if (horizontal_edge && s_phone != nullptr &&
                   s_phone->getManager().getActiveApp() != nullptr) {
            s_gesture_owner = CrystalGestureOwner::AppSwitch;
        } else {
            // The launcher's native pager owns horizontal drags, including
            // ones starting at an edge. Showing the OS mask would steal them.
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
    const bool home_gesture_active = s_home_gesture_active;
    s_home_gesture_active = false;
    if (home_gesture_active) {
        settle_home_pill_feedback();
    }
    if (s_swallow_wake_touch) {
        s_swallow_wake_touch = false;
        return;
    }
    if (owner == CrystalGestureOwner::Navigation) {
        // kHomeSwipeTravel, not ver_res / 2. Half the screen is right for card
        // switching, where the drag animates a card across and the commit point
        // should be the midpoint. This gesture moves only the home pill rather
        // than the page content, so demanding 240px of travel from a 24px band
        // made it almost unreachable and releases were silently dropped.
        if (info != nullptr && info->start_y - info->stop_y >= kHomeSwipeTravel) {
            ESP_LOGI(TAG, "Home gesture: travel=%d, depth=%zu, quick=%d, last_app=%d",
                     info->start_y - info->stop_y, s_system_page_depth, s_quick_settings_open,
                     s_last_app_before_settings);
            // Close all Settings pages if open
            if (s_system_page_depth > 0) {
                close_settings_and_restore_app();
            }
            // Close quick settings if open
            else if (s_quick_settings_open) {
                ESP_LOGI(TAG, "Closing quick settings");
                close_quick_settings(nullptr);
            }
            // No overlays - go to launcher (standard home button behavior)
            else if (s_phone != nullptr) {
                ESP_LOGI(TAG, "Sending HOME to go to launcher");
                (void)s_phone->sendNavigateEvent(ESP_BROOKESIA_CORE_NAVIGATE_TYPE_HOME);
            }
        } else if (info != nullptr) {
            ESP_LOGD(TAG, "Home gesture insufficient: travel=%d < %d",
                     info->start_y - info->stop_y, kHomeSwipeTravel);
        }
        return;
    }
    if (owner == CrystalGestureOwner::QuickSettings && info != nullptr) {
        lv_area_t panel_area{};
        if (s_quick_panel != nullptr) lv_obj_get_coords(s_quick_panel, &panel_area);
        const bool inside_panel = s_quick_catcher != nullptr && s_quick_panel != nullptr &&
                                  info->start_x >= panel_area.x1 && info->start_x <= panel_area.x2 &&
                                  info->start_y >= panel_area.y1 && info->start_y <= panel_area.y2;
        if (inside_panel) return;
        if (!s_quick_settings_open) (void)create_quick_settings();
        release_quick_settings(*info);
        return;
    }
    if (owner != CrystalGestureOwner::AppSwitch || info == nullptr || s_phone == nullptr || s_switching ||
            s_phone->getManager().getActiveApp() == nullptr) {
        return;
    }

    if (s_card_transition.phase == CardTransitionPhase::Dragging) {
        update_card_transition(*info);
        settle_card_transition(crossover_threshold_reached(
            s_card_transition.progress, s_card_transition.width,
            kCrossoverCommitPercent));
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
    crystal_registry_set_removed_hook(on_installed_app_removed);
    crystal_app_set_shell_back_hook(shell_consume_back);
    CrystalUI::set_focus_hooks(crystal_keyboard_show, crystal_keyboard_hide_for);
    s_current_index = load_current_index();
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

// Centred on the display while the keyboard is away, lifted clear of the band
// while it is up. Offsets are measured from the parent rather than assumed,
// because the parent starts below the status bar.
static void wifi_dialog_place(bool keyboard_open)
{
    if (s_wifi_dialog == nullptr) return;
    lv_obj_t *parent = lv_obj_get_parent(s_wifi_dialog);
    if (parent == nullptr) return;
    lv_obj_update_layout(parent);
    lv_area_t parent_area{};
    lv_obj_get_coords(parent, &parent_area);
    const lv_coord_t centred_top = static_cast<lv_coord_t>((lv_disp_get_ver_res(nullptr) -
                                   kWifiDialogHeight) / 2);
    lv_coord_t top = centred_top;
    if (keyboard_open) {
        const lv_coord_t overlap = static_cast<lv_coord_t>(centred_top + kWifiDialogHeight +
                                   kWifiDialogGap - crystal_keyboard_reserved_top());
        if (overlap > 0) top = static_cast<lv_coord_t>(top - overlap);
    }
    lv_coord_t relative_top = static_cast<lv_coord_t>(top - parent_area.y1);
    if (relative_top < 0) {
        ESP_LOGW(TAG, "WiFi dialog cannot fully clear keyboard band; clamping top");
        relative_top = 0;
    }
    lv_obj_align(s_wifi_dialog, LV_ALIGN_TOP_MID, 0, relative_top);
}

// Clearing the hook first keeps the hide from repositioning a dialog that is
// about to be deleted.
static void wifi_close_credentials()
{
    crystal_keyboard_set_state_cb(nullptr, nullptr);
    crystal_keyboard_hide();
    if (s_wifi_dialog != nullptr) {
        lv_obj_del(s_wifi_dialog);
        s_wifi_dialog = nullptr;
    }
    crystal_shell_set_modal_open(false);
}

static void wifi_open_credentials(const char *ssid)
{
    lv_obj_t *parent = s_wifi_page != nullptr ? s_wifi_page : s_quick_panel;
    if (ssid == nullptr || parent == nullptr) return;
    strlcpy(s_wifi_selected, ssid, sizeof(s_wifi_selected));
    if (s_wifi_dialog != nullptr) wifi_close_credentials();
    s_wifi_dialog = lv_obj_create(parent);
    lv_obj_set_size(s_wifi_dialog, kWifiDialogWidth, kWifiDialogHeight);
    wifi_dialog_place(crystal_keyboard_is_open());
    lv_obj_set_style_bg_color(s_wifi_dialog, lv_color_hex(0x252a30), 0);
    lv_obj_set_style_bg_opa(s_wifi_dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_wifi_dialog, lv_color_hex(0x59636e), 0);
    lv_obj_set_style_border_width(s_wifi_dialog, 1, 0);
    lv_obj_set_style_pad_all(s_wifi_dialog, 0, 0);
    lv_obj_clear_flag(s_wifi_dialog, LV_OBJ_FLAG_SCROLLABLE);
    crystal_shell_set_modal_open(true);

    lv_obj_t *title = lv_label_create(s_wifi_dialog);
    lv_label_set_text(title, "Enter WiFi password");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, kWifiTitleFont, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, kWifiDialogInset);

    lv_obj_t *network = lv_label_create(s_wifi_dialog);
    char network_text[48];
    snprintf(network_text, sizeof(network_text), "Network: %.32s", s_wifi_selected);
    lv_label_set_text(network, network_text);
    lv_obj_set_style_text_color(network, lv_color_hex(0xcbd5e1), 0);
    lv_obj_set_style_text_font(network, kWifiBodyFont, 0);
    lv_obj_set_width(network, kWifiDialogWidth - 2 * kWifiDialogInset);
    lv_label_set_long_mode(network, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(network, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(network, LV_ALIGN_TOP_MID, 0,
                 kWifiDialogInset + static_cast<lv_coord_t>(kWifiTitleFont->line_height) + 8);

    // Keep the field and visibility control on one compact, aligned row.
    lv_obj_t *input = lv_textarea_create(s_wifi_dialog);
    lv_obj_set_size(input, kWifiFieldWidth, kWifiFieldHeight);
    lv_obj_align(input, LV_ALIGN_TOP_LEFT, kWifiDialogInset, kWifiFieldTop);
    lv_obj_set_style_text_font(input, kWifiBodyFont, 0);
    lv_obj_set_style_bg_color(input, lv_color_hex(0x1b2028), 0);
    lv_obj_set_style_bg_opa(input, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(input, lv_color_hex(0xf2f4f7), 0);
    lv_obj_set_style_border_color(input, lv_color_hex(0x59636e), 0);
    lv_obj_set_style_border_color(input, lv_color_hex(0x3b82f6), LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(input, lv_color_hex(0x94a3b8), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(input, lv_color_hex(0xf2f4f7), LV_PART_CURSOR);
    lv_textarea_set_password_mode(input, true);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_max_length(input, 64);
    lv_textarea_set_placeholder_text(input, "Password");
    // Without this the field is dead after the first dismissal.
    lv_obj_add_event_cb(input, [](lv_event_t *event) {
        lv_obj_t *field = static_cast<lv_obj_t *>(lv_event_get_target(event));
        if (s_wifi_dialog != nullptr) (void)crystal_keyboard_show(field, s_wifi_dialog);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *reveal = lv_btn_create(s_wifi_dialog);
    settings_button_feedback(reveal);
    lv_obj_set_style_bg_color(reveal, lv_color_hex(0x334155), 0);
    lv_obj_set_style_bg_color(reveal, lv_color_hex(0x3b82f6), LV_STATE_PRESSED);
    lv_obj_set_size(reveal, kWifiRevealWidth, kWifiFieldHeight);
    lv_obj_align(reveal, LV_ALIGN_TOP_RIGHT, -kWifiDialogInset, kWifiFieldTop);
    lv_obj_t *reveal_label = lv_label_create(reveal);
    lv_label_set_text(reveal_label, "Show");
    lv_obj_set_style_text_font(reveal_label, kWifiBodyFont, 0);
    lv_obj_center(reveal_label);
    lv_obj_add_event_cb(reveal, [](lv_event_t *event) {
        auto *password = static_cast<lv_obj_t *>(lv_event_get_user_data(event));
        if (password == nullptr) return;
        const bool masked = lv_textarea_get_password_mode(password);
        lv_textarea_set_password_mode(password, !masked);
        // This is part of password entry, not an outside tap. Cancel the
        // keyboard's deferred dismissal without moving or rebinding the form.
        if (crystal_keyboard_is_open() && s_wifi_dialog != nullptr)
            (void)crystal_keyboard_show(password, s_wifi_dialog);
        lv_obj_t *button = static_cast<lv_obj_t *>(lv_event_get_target(event));
        lv_obj_t *label = lv_obj_get_child(button, 0);
        if (label != nullptr) lv_label_set_text(label, masked ? "Hide" : "Show");
    }, LV_EVENT_CLICKED, input);

    // Keep the same 54 px gap as the 350 px forget-network box. This dialog is
    // 100 px wider, so add half of that difference to each edge offset.
    lv_obj_t *connect = lv_btn_create(s_wifi_dialog);
    settings_button_feedback(connect);
    lv_obj_set_style_bg_color(connect, lv_color_hex(0x3b82f6), 0);
    lv_obj_set_style_bg_color(connect, lv_color_hex(0x2563eb), LV_STATE_PRESSED);
    lv_obj_set_size(connect, kWifiActionWidth, kWifiActionHeight);
    lv_obj_align(connect, LV_ALIGN_BOTTOM_LEFT, kWifiActionInset, -kWifiActionBottom);
    lv_obj_t *label = lv_label_create(connect);
    lv_label_set_text(label, "Connect");
    lv_obj_set_style_text_font(label, kWifiBodyFont, 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(connect, [](lv_event_t *e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto *password = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
        if (hal().wifi != nullptr && password != nullptr) {
            hal().wifi->connect(s_wifi_selected, lv_textarea_get_text(password));
        }
        wifi_close_credentials();
    }, LV_EVENT_CLICKED, input);

    lv_obj_t *cancel = lv_btn_create(s_wifi_dialog);
    settings_button_feedback(cancel);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(0x334155), 0);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(0x475569), LV_STATE_PRESSED);
    lv_obj_set_size(cancel, kWifiActionWidth, kWifiActionHeight);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_RIGHT, -kWifiActionInset, -kWifiActionBottom);
    lv_obj_t *cancel_label = lv_label_create(cancel);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_font(cancel_label, kWifiBodyFont, 0);
    lv_obj_set_style_text_color(cancel_label, lv_color_white(), 0);
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel, [](lv_event_t *) { wifi_close_credentials(); },
                        LV_EVENT_CLICKED, nullptr);

    lv_textarea_set_cursor_click_pos(input, true);
    lv_group_t *group = lv_group_get_default(); if (group != nullptr) lv_group_focus_obj(input);
    crystal_keyboard_set_state_cb([](bool open, void *) { wifi_dialog_place(open); }, nullptr);
    (void)crystal_keyboard_show(input, s_wifi_dialog);
}

static void wifi_open_forget_confirm(const char *ssid)
{
    if (s_wifi_page == nullptr || ssid == nullptr) return;
    if (s_wifi_dialog != nullptr) wifi_close_credentials();
    lv_obj_t *box = lv_obj_create(s_wifi_page);
    if (box == nullptr) return;
    s_wifi_dialog = box;
    crystal_shell_set_modal_open(true);
    lv_obj_set_size(box, kForgetDialogWidth, kForgetDialogHeight); lv_obj_center(box);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x252a30), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lv_label_create(box); lv_label_set_text(title, "Forget network?");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, kWifiTitleFont, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_t *name = lv_label_create(box); char text[40]; snprintf(text, sizeof(text), "%.32s", ssid);
    lv_label_set_text(name, text); lv_obj_set_style_text_color(name, lv_color_hex(0xcbd5e1), 0);
    lv_obj_set_style_text_font(name, kWifiBodyFont, 0);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_t *forget = lv_btn_create(box);
    lv_obj_set_size(forget, kForgetButtonWidth, kForgetButtonHeight);
    lv_obj_align(forget, LV_ALIGN_BOTTOM_LEFT, kForgetButtonInset, -14);
    lv_obj_t *fl = lv_label_create(forget); lv_label_set_text(fl, "Forget");
    lv_obj_set_style_text_font(fl, kWifiBodyFont, 0); lv_obj_center(fl);
    lv_obj_add_event_cb(forget, [](lv_event_t *) { if (hal().wifi != nullptr) hal().wifi->forget(); wifi_close_credentials(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cancel = lv_btn_create(box);
    lv_obj_set_size(cancel, kForgetButtonWidth, kForgetButtonHeight);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_RIGHT, -kForgetButtonInset, -14);
    lv_obj_t *cl = lv_label_create(cancel); lv_label_set_text(cl, "Cancel");
    lv_obj_set_style_text_font(cl, kWifiBodyFont, 0); lv_obj_center(cl);
    lv_obj_add_event_cb(cancel, [](lv_event_t *) { wifi_close_credentials(); }, LV_EVENT_CLICKED, nullptr);
}

void wifi_tile_text(char *out, size_t size)
{
    IWifi *wifi = hal().wifi;
    if (wifi == nullptr || !wifi->enabled()) {
        strlcpy(out, LV_SYMBOL_WIFI "\nWiFi\nOff", size);
    } else if (wifi->connected()) {
        snprintf(out, size, LV_SYMBOL_WIFI "\nWiFi\n%.32s", wifi->last_ssid());
    } else if (s_wifi_connecting[0] != '\0') {
        strlcpy(out, LV_SYMBOL_WIFI "\nWiFi\nConnecting...", size);
    } else {
        strlcpy(out, LV_SYMBOL_WIFI "\nWiFi\nNot connected", size);
    }
}

void system_page_pop();
void settings_push_network();
void settings_push_display_power();
void settings_push_sound();
void settings_push_region_time();
void settings_push_system();
void settings_push_developer_tools();

// Settings supplies its own pressed colors. Do not also inherit the theme's
// grow/filter animation or its delayed release transition: those briefly
// expand a dense row over its neighbours and pulse when navigating back.
void settings_button_feedback(lv_obj_t *button)
{
    // Remove theme styles rather than assigning a null transition descriptor:
    // LVGL 8 dereferences descriptors when changing state.
    lv_obj_remove_style_all(button);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_text_color(button, lv_color_hex(0xf2f4f7), 0);
}

lv_obj_t *system_page_push(const char *title)
{
    if (s_system_page_depth >= kSystemPageDepthMax) return nullptr;
    crystal_page_transition_settle();
    crystal_shell_front_layer_changed();
    lv_obj_t *covered_page = s_system_page_depth != 0
        ? s_system_page_stack[s_system_page_depth - 1] : nullptr;
    const lv_area_t area = active_app_area();
    lv_obj_t *page = lv_obj_create(lv_layer_top());
    if (page == nullptr) return nullptr;
    lv_obj_set_size(page, lv_area_get_width(&area), lv_area_get_height(&area));
    lv_obj_set_pos(page, area.x1, area.y1);
    lv_obj_set_style_bg_color(page, lv_color_hex(crystal_visual_style().background), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    if (s_system_page_depth == 0 && s_phone != nullptr) {
        auto &manager = s_phone->getManager();
        s_settings_back_gesture_was_enabled = manager.isGestureNavigationBackEnabled();
        s_settings_back_gesture_saved = manager.setGestureNavigationBackEnabled(false);
        ESP_LOGD(TAG, "Settings overlay: app back gesture %s (was %s)",
                 s_settings_back_gesture_saved ? "disabled" : "unchanged",
                 s_settings_back_gesture_was_enabled ? "enabled" : "disabled");
    }
    // Add event filter to block all input events when home gesture is active.
    // We need to catch PRESSING (early drag) not just PRESSED (tap complete).
    lv_obj_add_event_cb(page, [](lv_event_t *e) {
        const lv_event_code_t code = lv_event_get_code(e);
        if (s_home_gesture_active && (code == LV_EVENT_PRESSED ||
                                      code == LV_EVENT_PRESSING ||
                                      code == LV_EVENT_CLICKED)) {
            ESP_LOGD(TAG, "Blocking input event (%d) during home gesture", code);
            lv_event_stop_processing(e);
            lv_event_stop_bubbling(e);
        }
    }, LV_EVENT_ALL, nullptr);
    s_system_page_stack[s_system_page_depth++] = page;

    lv_obj_t *back = lv_btn_create(page);
    settings_button_feedback(back);
    lv_obj_set_size(back, kSettingsBackWidth, kSettingsBackHeight);
    lv_obj_set_pos(back, kSettingsBackLeft, kSettingsBackTop);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, [](lv_event_t *) { (void)shell_consume_back(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *back_label = lv_label_create(back);
    lv_label_set_text(back_label, s_system_page_depth == 1 ? LV_SYMBOL_CLOSE : LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_label, kSettingsTitleFont, 0);
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xe6edf5), 0);
    lv_obj_center(back_label);

    lv_obj_t *heading = lv_label_create(page);
    lv_label_set_text(heading, title);
    lv_obj_set_style_text_font(heading, kSettingsTitleFont, 0);
    lv_obj_set_style_text_color(heading, lv_color_white(), 0);
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, kSettingsHeadingTop);

    lv_obj_t *content = lv_obj_create(page);
    lv_obj_set_size(content, LV_PCT(100), lv_area_get_height(&area) - kSettingsHeaderHeight);
    // Phase 11 edit begin: keep the form's top edge stable when the keyboard reduces
    // the viewport height. A bottom anchor would move every child downward as
    // soon as the viewport is resized.
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, kSettingsHeaderHeight);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, kSettingsContentInset, 0);
    lv_obj_set_style_pad_row(content,
#if CONFIG_IDF_TARGET_ESP32P4
                             8,
#else
                             1,
#endif
                             0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    // Remove Settings overscroll and throw lag. LVGL's default elastic
    // overscroll exposes blank bands and its throw animation makes this dense
    // page feel delayed on the panel.
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    // Phase 11 edit end.
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    update_home_pill();
    // Explicitly ensure the pill target is on top of the Settings page.
    if (s_home_pill_catcher != nullptr) {
        lv_obj_move_foreground(s_home_pill_catcher);
    }
    if (s_home_pill != nullptr) {
        lv_obj_move_foreground(s_home_pill);
        ESP_LOGD(TAG, "Moved pill to foreground after creating Settings page");
    }
    // Immediate mode covers the parent in this same LVGL dispatch. The helper
    // also keeps ownership correct if an animated transition is used later.
    crystal_page_transition_enter(page, covered_page);
    return content;
}

void clear_page_cache(lv_obj_t *page)
{
    if (s_manage_apps_page == page) {
        s_manage_apps_page = nullptr;
    }
    if (s_root_settings_page == page) {
        s_root_settings_page = nullptr;
        s_root_network_row = nullptr;
    }
    if (s_network_page == page) {
        s_network_page = nullptr;
        s_network_wifi_row = nullptr;
        s_network_details_row = nullptr;
        s_network_ethernet_row = s_network_active_row = nullptr;
    }
    if (s_connection_details_content != nullptr &&
            lv_obj_get_parent(s_connection_details_content) == page) {
        s_connection_details_content = nullptr;
    }
    if (page == s_wifi_page) {
        if (s_wifi_scan_timeout != nullptr) {
            lv_timer_del(s_wifi_scan_timeout);
            s_wifi_scan_timeout = nullptr;
        }
        s_wifi_scanning = false;
        s_wifi_row_count = 0;
        s_wifi_scan_spinner = nullptr;
        s_wifi_refresh = nullptr;
        s_wifi_page = nullptr;
        s_wifi_page_list = nullptr;
        s_wifi_page_status = nullptr;
        if (s_wifi_dialog != nullptr) {
            s_wifi_dialog = nullptr;
            crystal_shell_set_modal_open(false);
        }
    }
    if (page == s_developer_tools_page) {
        s_developer_tools_page = nullptr;
        s_control_api_row = nullptr;
        s_control_api_switch = nullptr;
        s_control_status_label = nullptr;
    }
    if (page == s_location_form.page) s_location_form = {};
    if (page == s_manual_time_form.page) s_manual_time_form = {};
    memset(s_ip_fields, 0, sizeof(s_ip_fields));
    s_ip_apply_status = nullptr;
    if (s_system_page_depth == 0) {
        s_root_network_row = nullptr;
        s_root_settings_page = nullptr;
        s_root_power_row = nullptr;
        s_root_sound_row = nullptr;
        s_root_region_row = nullptr;
        s_network_wifi_row = nullptr;
        s_network_details_row = nullptr;
        s_network_page = nullptr;
        s_network_ethernet_row = s_network_active_row = nullptr;
        s_connection_details_content = nullptr;
        s_power_auto_dim_row = nullptr;
        s_sound_alerts_row = nullptr;
        s_region_auto_time_row = nullptr;
        s_region_timezone_row = nullptr;
        s_region_format_row = nullptr;
        s_region_manual_time_row = nullptr;
        s_region_location_row = nullptr;
    }
}

void system_page_pop()
{
    if (s_system_page_depth == 0) return;
    crystal_page_transition_settle();
    crystal_shell_front_layer_changed();
    lv_obj_t *page = s_system_page_stack[--s_system_page_depth];
    s_system_page_stack[s_system_page_depth] = nullptr;
    crystal_keyboard_set_state_cb(nullptr, nullptr);
    clear_page_cache(page);
    crystal_page_transition_retire(page);
    if (s_system_page_depth != 0) {
        lv_obj_clear_flag(s_system_page_stack[s_system_page_depth - 1], LV_OBJ_FLAG_HIDDEN);
    }
    if (s_system_page_depth == 0 && s_settings_back_gesture_saved && s_phone != nullptr) {
        (void)s_phone->getManager().setGestureNavigationBackEnabled(s_settings_back_gesture_was_enabled);
        ESP_LOGD(TAG, "Settings overlay closed: app back gesture %s",
                 s_settings_back_gesture_was_enabled ? "restored" : "kept disabled");
        s_settings_back_gesture_saved = false;
    }
    update_home_pill();
}

void close_settings_and_restore_app()
{
    if (s_system_page_depth == 0) return;
    ESP_LOGI(TAG, "Closing all %zu Settings pages", s_system_page_depth);
    while (s_system_page_depth > 0) system_page_pop();

    if (s_last_app_before_settings >= 0) {
        ESP_LOGI(TAG, "Returning to app index %d", s_last_app_before_settings);
        if (!start_card(static_cast<size_t>(s_last_app_before_settings), false)) {
            ESP_LOGW(TAG, "Failed to return to app %d", s_last_app_before_settings);
        }
        update_home_pill();
    }
    s_last_app_before_settings = -1;
}

lv_obj_t *settings_row(lv_obj_t *parent, const char *label, const char *summary = nullptr)
{
    lv_obj_t *row = lv_btn_create(parent);
    settings_button_feedback(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, kSettingsRowHeight);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1b2028), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x28323e), LV_STATE_PRESSED);
    // Without these the default theme paints disabled rows a pale grey that reads
    // as "highlighted" rather than "unavailable". Keep the row on the dark palette
    // and fade its contents instead.
    lv_obj_set_style_bg_color(row, lv_color_hex(0x15191f), LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_DISABLED);
    lv_obj_set_style_opa(row, LV_OPA_50, LV_STATE_DISABLED);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_hor(row, kSettingsRowPad, 0);
    // The two labels are placed from the row top with explicit offsets, so the
    // theme's default vertical button padding must go or it shifts both inwards
    // until the title and summary collide.
    lv_obj_set_style_pad_ver(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lv_label_create(row);
    lv_label_set_text(title, label);
    lv_obj_set_style_text_font(title, kSettingsTitleFont, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xf2f4f7), 0);
    if (summary == nullptr) {
        lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);
    } else {
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, kSettingsTitleTop);
        lv_obj_t *detail = lv_label_create(row);
        lv_label_set_text(detail, summary);
        lv_label_set_long_mode(detail, LV_LABEL_LONG_DOT);
        // Clamp to one line; the target-specific height forces truncation rather than wrap. The width
        // takes the whole row so the ellipsis is a last resort instead of the norm -
        // rows that put a control on the right shrink this via settings_row_reserve_right().
        lv_obj_set_width(detail, LV_PCT(100));
        lv_obj_set_height(detail, kSettingsSummaryHeight);
        lv_obj_set_style_text_font(detail, kSettingsSummaryFont, 0);
        lv_obj_set_style_text_color(detail, lv_color_hex(0x91a0b3), 0);
        lv_obj_align(detail, LV_ALIGN_TOP_LEFT, 0, kSettingsSummaryTop);
        // Remembered so controls added later can reclaim their own space.
        lv_obj_set_user_data(row, detail);
    }
    return row;
}

void settings_row_set_summary(lv_obj_t *row, const char *summary)
{
    // Phase 11 edit begin: refresh descriptions without rebuilding the page.
    if (row == nullptr) return;
    auto *detail = static_cast<lv_obj_t *>(lv_obj_get_user_data(row));
    if (detail == nullptr) return;
    const char *text = summary != nullptr ? summary : "";
    if (strcmp(lv_label_get_text(detail), text) != 0) lv_label_set_text(detail, text);
    // Phase 11 edit end.
}

lv_obj_t *settings_status_label(lv_obj_t *parent)
{
    lv_obj_t *status = lv_label_create(parent);
    lv_label_set_text(status, "");
    lv_obj_set_width(status, LV_PCT(100));
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(status, kSettingsStatusFont, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(0xf59e0b), 0);
    return status;
}

void settings_style_input(lv_obj_t *input)
{
    lv_obj_set_style_text_font(input, kSettingsInputFont, 0);
}

// Trims the summary label so it stops short of a right-aligned control instead of
// running underneath it. No-op for rows created without a summary.
void settings_row_reserve_right(lv_obj_t *row, lv_coord_t control_width)
{
    auto *detail = static_cast<lv_obj_t *>(lv_obj_get_user_data(row));
    if (detail == nullptr) return;
    lv_obj_update_layout(row);
    lv_coord_t width = lv_obj_get_content_width(row) - control_width - 12;
    if (width < 80) width = 80;
    lv_obj_set_width(detail, width);
}

lv_obj_t *settings_switch(lv_obj_t *row, bool checked)
{
    lv_obj_t *control = lv_switch_create(row);
    lv_obj_set_size(control, kSettingsSwitchWidth, kSettingsSwitchHeight);
    lv_obj_align(control, LV_ALIGN_RIGHT_MID, 0, 0);
    // The 52x30 body is a small target for a finger. Padding the click area by 15px
    // makes it 82x60 - the full row height - without changing how it looks. LVGL
    // hit-tests children before the row, so these presses no longer land on the row.
    lv_obj_set_ext_click_area(control, kSettingsSwitchClick);
    if (checked) lv_obj_add_state(control, LV_STATE_CHECKED);
    settings_row_reserve_right(row, kSettingsSwitchWidth);
    return control;
}

void populate_connection_details(lv_obj_t *content)
{
    if (content == nullptr) return;
    if (s_wifi_connecting[0] != '\0') {
        char status[64] = {};
        snprintf(status, sizeof(status), "Connecting to %.32s...", s_wifi_connecting);
        (void)settings_row(content, "Status", status);
        (void)settings_row(content, "Network", s_wifi_connecting);
        return;
    }
    IWifi::IpConfig config = {};
    int8_t rssi = 0;
    uint8_t mac[6] = {};
    if (hal().wifi == nullptr || !hal().wifi->ip_config(&config)) {
        (void)settings_row(content, "Status", "Not connected");
        return;
    }
    char value[64] = {};
    (void)settings_row(content, "Network", hal().wifi->last_ssid());
    ip4addr_ntoa_r(reinterpret_cast<const ip4_addr_t *>(&config.ip), value, sizeof(value));
    (void)settings_row(content, "IP Address", value);
    ip4addr_ntoa_r(reinterpret_cast<const ip4_addr_t *>(&config.mask), value, sizeof(value));
    (void)settings_row(content, "Subnet Mask", value);
    ip4addr_ntoa_r(reinterpret_cast<const ip4_addr_t *>(&config.gateway), value, sizeof(value));
    (void)settings_row(content, "Gateway", value);
    ip4addr_ntoa_r(reinterpret_cast<const ip4_addr_t *>(&config.dns1), value, sizeof(value));
    (void)settings_row(content, "Primary DNS", value);
    if (config.dns2 != 0) { ip4addr_ntoa_r(reinterpret_cast<const ip4_addr_t *>(&config.dns2), value, sizeof(value)); (void)settings_row(content, "Secondary DNS", value); }
    if (hal().wifi->rssi(&rssi)) { snprintf(value, sizeof(value), "%d dBm", rssi); (void)settings_row(content, "Signal", value); }
    if (hal().wifi->mac(mac)) { snprintf(value, sizeof(value), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]); (void)settings_row(content, "WiFi MAC", value); }
}

void settings_push_connection_details()
{
    s_connection_details_content = system_page_push("Connection Details");
    populate_connection_details(s_connection_details_content);
}

void refresh_connection_details()
{
    if (s_connection_details_content == nullptr) return;
    lv_obj_clean(s_connection_details_content);
    populate_connection_details(s_connection_details_content);
}

bool stored_ip_value(const char *key, uint32_t *out)
{
    size_t length = sizeof(*out);
    return out != nullptr && hal().storage != nullptr &&
           hal().storage->get(key, out, &length) && length == sizeof(*out);
}

void ip_field_focus(lv_event_t *event)
{
    lv_obj_t *field = static_cast<lv_obj_t *>(lv_event_get_target(event));
    (void)crystal_keyboard_show(field, lv_obj_get_parent(field));
}

void set_ip_fields_enabled(bool enabled)
{
    for (lv_obj_t *field : s_ip_fields) {
        if (field == nullptr) continue;
        if (enabled) lv_obj_clear_state(field, LV_STATE_DISABLED);
        else lv_obj_add_state(field, LV_STATE_DISABLED);
    }
}

void ip_apply(lv_event_t *event)
{
    lv_obj_t *dhcp_switch = static_cast<lv_obj_t *>(lv_event_get_user_data(event));
    IWifi::IpConfig config = {};
    config.dhcp = lv_obj_has_state(dhcp_switch, LV_STATE_CHECKED);
    uint32_t *values[] = {&config.ip, &config.mask, &config.gateway, &config.dns1, &config.dns2};
    bool complete = true;
    for (size_t i = 0; i < 5; ++i) {
        const char *text = s_ip_fields[i] != nullptr ? lv_textarea_get_text(s_ip_fields[i]) : nullptr;
        if (text == nullptr || text[0] == '\0') {
            if (i < 4) complete = false;
            continue;
        }
        if (inet_aton(text, reinterpret_cast<in_addr *>(values[i])) == 0) {
            *values[i] = 0;
            if (!config.dhcp) {
                lv_label_set_text(s_ip_apply_status, "Enter valid dotted-quad addresses");
                return;
            }
        }
    }
    if (!config.dhcp) {
        if (!complete) {
            lv_label_set_text(s_ip_apply_status, "Enter valid dotted-quad addresses");
            return;
        }
        const uint32_t mask = ntohl(config.mask);
        const uint32_t wildcard = ~mask;
        if (mask == 0 || (wildcard & (wildcard + 1)) != 0 ||
                (config.ip & config.mask) != (config.gateway & config.mask) ||
                (config.ip & ~config.mask) == 0 ||
                (config.ip & ~config.mask) == ~config.mask) {
            lv_label_set_text(s_ip_apply_status, "Check subnet mask, gateway, and host address");
            return;
        }
    }
    if (hal().wifi != nullptr && hal().wifi->set_ip_config(config)) {
        lv_label_set_text(s_ip_apply_status, config.dhcp ? "Automatic addressing enabled" : "Static configuration applied");
        settings_row_set_summary(s_network_details_row, config.dhcp ? "Connected - DHCP" : "Connected - Static");
    } else {
        lv_label_set_text(s_ip_apply_status, "Could not apply network settings");
    }
}

void settings_push_ip()
{
    lv_obj_t *content = system_page_push("IP Settings");
    if (content == nullptr) return;
    uint8_t stored_dhcp = 1;
    size_t length = sizeof(stored_dhcp);
    if (hal().storage != nullptr) (void)hal().storage->get("net.dhcp", &stored_dhcp, &length);
    lv_obj_t *mode = settings_row(content, "Automatic (DHCP)", stored_dhcp ? "Router assigns the address" : "Manual configuration");
    lv_obj_t *dhcp_switch = settings_switch(mode, stored_dhcp != 0);
    lv_obj_add_event_cb(dhcp_switch, [](lv_event_t *e) {
        auto *target = static_cast<lv_obj_t *>(lv_event_get_target(e));
        const bool automatic = lv_obj_has_state(target, LV_STATE_CHECKED);
        if (automatic) crystal_keyboard_hide();
        set_ip_fields_enabled(!automatic);
        settings_row_set_summary(lv_obj_get_parent(target),
                                 automatic ? "Router assigns the address" : "Manual configuration");
        if (!automatic) {
            lv_label_set_text(s_ip_apply_status, "Enter the addresses below, then press Apply");
            return;
        }
        IWifi::IpConfig config = {};
        config.dhcp = true;
        if (hal().wifi != nullptr && hal().wifi->set_ip_config(config)) {
            lv_label_set_text(s_ip_apply_status, "Automatic addressing enabled");
            settings_row_set_summary(s_network_details_row, "Connected - DHCP");
        } else {
            lv_label_set_text(s_ip_apply_status, "Could not enable automatic addressing");
        }
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    static const char *labels[] = {"IP address", "Subnet mask", "Gateway", "Primary DNS", "Secondary DNS (optional)"};
    static const char *keys[] = {"net.ip", "net.mask", "net.gw", "net.dns1", "net.dns2"};
    for (size_t i = 0; i < 5; ++i) {
        s_ip_fields[i] = lv_textarea_create(content);
        lv_obj_set_size(s_ip_fields[i], LV_PCT(100), kSettingsInputHeight);
        settings_style_input(s_ip_fields[i]);
        lv_textarea_set_one_line(s_ip_fields[i], true);
        lv_textarea_set_placeholder_text(s_ip_fields[i], labels[i]);
        lv_textarea_set_max_length(s_ip_fields[i], 15);
        uint32_t address = 0;
        char text[IP4ADDR_STRLEN_MAX] = {};
        if (stored_ip_value(keys[i], &address) && address != 0) {
            ip4addr_ntoa_r(reinterpret_cast<const ip4_addr_t *>(&address), text, sizeof(text));
            lv_textarea_set_text(s_ip_fields[i], text);
        }
        lv_obj_add_event_cb(s_ip_fields[i], ip_field_focus, LV_EVENT_FOCUSED, nullptr);
    }
    set_ip_fields_enabled(stored_dhcp == 0);
    lv_obj_t *apply = settings_row(content, "Apply", "Validates all fields before changing the interface");
    lv_obj_add_event_cb(apply, ip_apply, LV_EVENT_CLICKED, dhcp_switch);
    s_ip_apply_status = settings_status_label(content);
}

const char *ethernet_status(const CrystalEthernetInfo &info)
{
    if (!info.supported) return "Not available on this board";
    if (info.error != ESP_OK) return "Unavailable - WiFi still works";
    if (!info.initialized) return "Starting...";
    if (!info.started) return "Stopped";
    if (!info.link) return "Cable disconnected";
    return info.has_ip ? "Connected" : "Getting an IP address...";
}

void ethernet_details_refresh(lv_timer_t *)
{
    if (s_ethernet_page == nullptr) return;
    const auto info = crystal_ethernet_info();
    char value[64] = {};
    const auto set = [](size_t index, const char *text) {
        auto *detail = static_cast<lv_obj_t *>(lv_obj_get_user_data(s_ethernet_rows[index]));
        if (detail && strcmp(lv_label_get_text(detail), text) != 0) lv_label_set_text(detail, text);
    };
    set(0, ethernet_status(info));
    set(1, crystal_network_active_type() == CrystalNetworkType::Ethernet ? "Active connection" : "Automatic - preferred when connected");
    const uint32_t addresses[] = {info.ip, info.mask, info.gateway, info.dns};
    for (size_t i = 0; i < 4; ++i) {
        if (info.has_ip && addresses[i] != 0)
            ip4addr_ntoa_r(reinterpret_cast<const ip4_addr_t *>(&addresses[i]), value, sizeof(value));
        else strlcpy(value, "Not assigned", sizeof(value));
        set(i + 2, value);
    }
    if (info.initialized) snprintf(value, sizeof(value), "%02X:%02X:%02X:%02X:%02X:%02X",
        info.mac[0], info.mac[1], info.mac[2], info.mac[3], info.mac[4], info.mac[5]);
    else strlcpy(value, "Unavailable", sizeof(value));
    set(6, value);
    if (info.link && info.speed_mbps) snprintf(value, sizeof(value), "%u Mbps - %s duplex", info.speed_mbps, info.full_duplex ? "Full" : "Half");
    else strlcpy(value, "No link", sizeof(value));
    set(7, value);
}

void ethernet_details_deleted(lv_event_t *event)
{
    if (lv_event_get_target(event) != s_ethernet_page) return;
    if (s_ethernet_timer) lv_timer_del(s_ethernet_timer);
    s_ethernet_timer = nullptr;
    s_ethernet_page = nullptr;
    memset(s_ethernet_rows, 0, sizeof(s_ethernet_rows));
}

void settings_push_ethernet()
{
    lv_obj_t *content = system_page_push("Ethernet");
    if (!content) return;
    s_ethernet_page = lv_obj_get_parent(content);
    lv_obj_add_event_cb(s_ethernet_page, ethernet_details_deleted, LV_EVENT_DELETE, nullptr);
    const char *labels[] = {"Status", "Network Priority", "IP Address", "Subnet Mask", "Gateway", "DNS Server", "Ethernet MAC", "Link Speed"};
    for (size_t i = 0; i < 8; ++i) {
        s_ethernet_rows[i] = settings_row(content, labels[i], "--");
        lv_obj_clear_flag(s_ethernet_rows[i], LV_OBJ_FLAG_CLICKABLE);
    }
    ethernet_details_refresh(nullptr);
    s_ethernet_timer = lv_timer_create(ethernet_details_refresh, 1000, nullptr);
}

void settings_push_network()
{
    lv_obj_t *content = system_page_push("Network");
    if (content == nullptr) return;
    s_network_page = lv_obj_get_parent(content);
    IWifi *wifi = hal().wifi;
#if CONFIG_IDF_TARGET_ESP32P4
    s_network_active_row = settings_row(content, "Active Connection", crystal_network_type_name(crystal_network_active_type()));
    lv_obj_clear_flag(s_network_active_row, LV_OBJ_FLAG_CLICKABLE);
    s_network_ethernet_row = settings_row(content, "Ethernet", ethernet_status(crystal_ethernet_info()));
    lv_obj_add_event_cb(s_network_ethernet_row, [](lv_event_t *) { settings_push_ethernet(); }, LV_EVENT_CLICKED, nullptr);
#endif
    const char *summary = wifi == nullptr || !wifi->enabled() ? "Off" :
                          s_wifi_connecting[0] != '\0' ? "Connecting..." :
                          wifi->connected() ? wifi->last_ssid() : "On - Not connected";
    lv_obj_t *wifi_row = settings_row(content, "WiFi", summary);
    s_network_wifi_row = wifi_row;
    lv_obj_t *wifi_switch = settings_switch(wifi_row, wifi != nullptr && wifi->enabled());
    lv_obj_add_event_cb(wifi_switch, [](lv_event_t *e) {
        const bool enabled = lv_obj_has_state(static_cast<lv_obj_t *>(lv_event_get_target(e)), LV_STATE_CHECKED);
        if (hal().wifi != nullptr) hal().wifi->set_enabled(enabled);
        settings_row_set_summary(s_network_wifi_row, enabled ? "On - Not connected" : "Off");
        settings_row_set_summary(s_root_network_row, enabled ? "WiFi On - Not connected" : "WiFi Off");
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_t *networks = settings_row(content, "WiFi Networks", "Scan, connect, or forget");
    lv_obj_add_event_cb(networks, [](lv_event_t *) { wifi_page_open(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *ip = settings_row(content, "WiFi IP Settings", "Automatic or validated manual address");
    lv_obj_add_event_cb(ip, [](lv_event_t *) { settings_push_ip(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *details = settings_row(content, "Connection Details", wifi != nullptr && wifi->has_ip() ? "Connected" : "Not connected");
    s_network_details_row = details;
    lv_obj_add_event_cb(details, [](lv_event_t *) { settings_push_connection_details(); }, LV_EVENT_CLICKED, nullptr);
}

void settings_push_display_power()
{
    lv_obj_t *content = system_page_push("Display & Power");
    if (content == nullptr) return;
    lv_obj_t *brightness_row = settings_row(content, "Brightness",
#if CONFIG_IDF_TARGET_ESP32P4
                                             "1-100"
#else
                                             "0-95"
#endif
                                            );
    lv_obj_t *brightness = lv_slider_create(brightness_row);
    lv_obj_set_size(brightness, kSettingsSliderWidth, kSettingsSliderHeight);
    lv_obj_set_ext_click_area(brightness, kSettingsSliderClick);
    lv_obj_align(brightness, LV_ALIGN_RIGHT_MID, -kSettingsSliderRightInset, 0);
    settings_row_reserve_right(brightness_row, kSettingsSliderWidth + kSettingsSliderRightInset);
    lv_slider_set_range(brightness, kBrightnessUiMin, kBrightnessUiMax);
    lv_slider_set_value(brightness, crystal_brightness_level(), LV_ANIM_OFF);
    lv_obj_add_event_cb(brightness, [](lv_event_t *e) { crystal_brightness_set(static_cast<uint8_t>(lv_slider_get_value(static_cast<lv_obj_t *>(lv_event_get_target(e))))); }, LV_EVENT_VALUE_CHANGED, nullptr);

    // Master switch for the two dropdowns below. They keep their stored values
    // while it is off so turning it back on restores the user's choices.
    lv_obj_t *auto_dim_row = settings_row(content, "Auto Dimming", "Dim and turn off the screen when idle");
    s_power_auto_dim_row = auto_dim_row;
    lv_obj_t *auto_dim_sw = settings_switch(auto_dim_row, crystal_power_auto_dim_enabled());
    lv_obj_add_event_cb(auto_dim_sw, [](lv_event_t *e) {
        const bool enabled = lv_obj_has_state(static_cast<lv_obj_t *>(lv_event_get_target(e)), LV_STATE_CHECKED);
        crystal_power_set_auto_dim(enabled);
        settings_row_set_summary(s_power_auto_dim_row, enabled ? "Dim and turn off the screen when idle" : "Automatic dimming disabled");
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *dim_row = settings_row(content, "Dim After", nullptr);
    lv_obj_t *dim = lv_dropdown_create(dim_row);
    lv_dropdown_set_options(dim, "Never\n15 seconds\n30 seconds\n1 minute\n5 minutes");
    const uint16_t dim_values[] = {0, 15, 30, 60, 300};
    const uint16_t current_dim = crystal_power_dim_seconds();
    for (uint16_t i = 0; i < 5; ++i) if (dim_values[i] == current_dim) lv_dropdown_set_selected(dim, i);
    lv_obj_set_width(dim, kSettingsDropdownWidth); lv_obj_align(dim, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_text_font(dim, kSettingsInputFont, 0);
    lv_obj_add_event_cb(dim, [](lv_event_t *e) { static const uint16_t values[] = {0, 15, 30, 60, 300}; lv_obj_t *dropdown = static_cast<lv_obj_t *>(lv_event_get_target(e)); crystal_power_set_dim_seconds(values[lv_dropdown_get_selected(dropdown)]); const uint16_t stored = crystal_power_dim_seconds(); for (uint16_t i = 0; i < 5; ++i) if (values[i] == stored) lv_dropdown_set_selected(dropdown, i); }, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *off_row = settings_row(content, "Screen Off After", nullptr);
    lv_obj_t *off = lv_dropdown_create(off_row);
    lv_dropdown_set_options(off, "Never\n1 minute\n2 minutes\n5 minutes\n15 minutes");
    const uint16_t off_values[] = {0, 60, 120, 300, 900};
    const uint16_t current_off = crystal_power_off_seconds();
    for (uint16_t i = 0; i < 5; ++i) if (off_values[i] == current_off) lv_dropdown_set_selected(off, i);
    lv_obj_set_width(off, kSettingsDropdownWidth); lv_obj_align(off, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_text_font(off, kSettingsInputFont, 0);
    lv_obj_add_event_cb(off, [](lv_event_t *e) { static const uint16_t values[] = {0, 60, 120, 300, 900}; lv_obj_t *dropdown = static_cast<lv_obj_t *>(lv_event_get_target(e)); crystal_power_set_off_seconds(values[lv_dropdown_get_selected(dropdown)]); const uint16_t stored = crystal_power_off_seconds(); for (uint16_t i = 0; i < 5; ++i) if (values[i] == stored) lv_dropdown_set_selected(dropdown, i); }, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *level_row = settings_row(content, "Dim Brightness", "1-50");
    lv_obj_t *level = lv_slider_create(level_row);
    lv_obj_set_size(level, kSettingsNarrowSliderWidth, kSettingsSliderHeight);
    lv_obj_set_ext_click_area(level, kSettingsSliderClick);
    lv_obj_align(level, LV_ALIGN_RIGHT_MID, -kSettingsSliderRightInset, 0);
    settings_row_reserve_right(level_row, kSettingsNarrowSliderWidth + kSettingsSliderRightInset);
    lv_slider_set_range(level, kDimBrightnessUiMin, kDimBrightnessUiMax); lv_slider_set_value(level, crystal_power_dim_level(), LV_ANIM_OFF);
    lv_obj_add_event_cb(level, [](lv_event_t *e) {
        auto *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
        const lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING || code == LV_EVENT_VALUE_CHANGED) {
            const uint8_t dim_level = static_cast<uint8_t>(lv_slider_get_value(slider));
            if (code == LV_EVENT_VALUE_CHANGED) crystal_power_set_dim_level(dim_level);
            if (hal().brightness != nullptr) hal().brightness->set(dim_level);
        } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            // Dim Brightness is a preview while editing. Restore the user's
            // normal brightness after the control is released.
            crystal_brightness_set(crystal_brightness_level());
        }
    }, LV_EVENT_ALL, nullptr);

    lv_obj_t *saving_row = settings_row(content, "Energy Saving", "Shorter timeouts and lower power");
    lv_obj_t *saving = settings_switch(saving_row, crystal_power_saving_enabled());
    lv_obj_add_event_cb(saving, [](lv_event_t *e) {
        const bool enabled = lv_obj_has_state(static_cast<lv_obj_t *>(lv_event_get_target(e)), LV_STATE_CHECKED);
        crystal_power_set_saving(enabled);
        settings_row_set_summary(s_root_power_row, enabled ? "Energy Saving On" : "Energy Saving Off");
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    int percent = 0; bool charging = false; char battery[40] = "Waiting for battery reading";
    if (crystal_battery_cached(&percent, &charging)) snprintf(battery, sizeof(battery), "%d%% - %s", percent, charging ? "Charging" : "On battery");
    (void)settings_row(content, "Battery", battery);
}

void settings_push_sound()
{
    lv_obj_t *content = system_page_push("Sound");
    if (content == nullptr) return;
    lv_obj_t *volume_row = settings_row(content, "Volume", "0-100");
    lv_obj_t *volume = lv_slider_create(volume_row);
    lv_obj_set_size(volume, kSettingsSliderWidth, kSettingsSliderHeight);
    lv_obj_set_ext_click_area(volume, kSettingsSliderClick);
    lv_obj_align(volume, LV_ALIGN_RIGHT_MID, -kSettingsSliderRightInset, 0);
    settings_row_reserve_right(volume_row, kSettingsSliderWidth + kSettingsSliderRightInset);
    lv_slider_set_range(volume, 0, 100); lv_slider_set_value(volume, crystal_hal_get_volume(), LV_ANIM_OFF);
    lv_obj_add_event_cb(volume, [](lv_event_t *e) { const uint8_t value = static_cast<uint8_t>(lv_slider_get_value(static_cast<lv_obj_t *>(lv_event_get_target(e)))); if (crystal_hal_set_volume(value) && hal().storage != nullptr) (void)hal().storage->set("volume", &value, sizeof(value)); }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_t *alerts_row = settings_row(content, "Timer & Alarm Sounds", "Play timer completion alerts");
    s_sound_alerts_row = alerts_row;
    lv_obj_t *alerts = settings_switch(alerts_row, crystal_sound_alerts_enabled());
    lv_obj_add_event_cb(alerts, [](lv_event_t *e) {
        const bool enabled = lv_obj_has_state(static_cast<lv_obj_t *>(lv_event_get_target(e)), LV_STATE_CHECKED);
        crystal_sound_set_alerts(enabled);
        settings_row_set_summary(s_sound_alerts_row, enabled ? "Play timer completion alerts" : "Timer completion alerts disabled");
        settings_row_set_summary(s_root_sound_row, enabled ? "Alerts On" : "Alerts Off");
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_t *test = settings_row(content, "Test Sound", "Play the timer chime now");
    lv_obj_add_event_cb(test, [](lv_event_t *) { crystal_hal_timer_alarm(); }, LV_EVENT_CLICKED, nullptr);
}

struct TimezoneEntry { const char *label; const char *posix; };
constexpr TimezoneEntry kTimezones[] = {
    {"Hong Kong (UTC+08:00)", "HKT-8"},
    {"Singapore (UTC+08:00)", "SGT-8"},
    {"Tokyo (UTC+09:00)", "JST-9"},
    {"Sydney (UTC+10:00)", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Dubai (UTC+04:00)", "GST-4"},
    {"London (UTC+00:00)", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Berlin (UTC+01:00)", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"New York (UTC-05:00)", "EST5EDT,M3.2.0,M11.1.0"},
    {"Chicago (UTC-06:00)", "CST6CDT,M3.2.0,M11.1.0"},
    {"Los Angeles (UTC-08:00)", "PST8PDT,M3.2.0,M11.1.0"},
    {"UTC", "UTC0"},
};

const char *current_timezone_label()
{
    char timezone[48] = "HKT-8";
    size_t length = sizeof(timezone) - 1;
    if (hal().storage != nullptr && hal().storage->get("timezone", timezone, &length)) timezone[length < sizeof(timezone) ? length : sizeof(timezone) - 1] = '\0';
    for (const auto &entry : kTimezones) if (strcmp(entry.posix, timezone) == 0) return entry.label;
    return "Custom timezone";
}

void settings_push_timezones()
{
    lv_obj_t *content = system_page_push("Timezone");
    if (content == nullptr) return;
    for (const auto &entry : kTimezones) {
        lv_obj_t *row = settings_row(content, entry.label, strcmp(entry.label, current_timezone_label()) == 0 ? "Selected" : nullptr);
        lv_obj_add_event_cb(row, [](lv_event_t *e) {
            const auto *zone = static_cast<const TimezoneEntry *>(lv_event_get_user_data(e));
            if (zone != nullptr && crystal_timezone_apply(zone->posix)) {
                settings_row_set_summary(s_region_timezone_row, zone->label);
                settings_row_set_summary(s_root_region_row, zone->label);
                system_page_pop();
            }
        }, LV_EVENT_CLICKED, const_cast<TimezoneEntry *>(&entry));
    }
}

void set_location_fields_enabled(bool enabled)
{
    for (lv_obj_t *field : {s_location_form.city, s_location_form.latitude,
                            s_location_form.longitude}) {
        if (field == nullptr) continue;
        if (enabled) lv_obj_clear_state(field, LV_STATE_DISABLED);
        else lv_obj_add_state(field, LV_STATE_DISABLED);
    }
}

void location_mode_changed(lv_event_t *event)
{
    const bool automatic = lv_obj_has_state(
        static_cast<lv_obj_t *>(lv_event_get_target(event)), LV_STATE_CHECKED);
    if (automatic) crystal_keyboard_hide();
    crystal_weather_set_automatic(automatic);
    set_location_fields_enabled(!automatic);
    settings_row_set_summary(s_region_location_row, automatic ? "Automatic" : "Manual");
    settings_row_set_summary(s_root_region_row,
                             automatic ? current_timezone_label() : "Manual location");
    if (s_location_form.status != nullptr) {
        lv_label_set_text(s_location_form.status,
                          automatic ? "" : "Enter a city label and coordinates below");
    }
}

void location_apply(lv_event_t *)
{
    if (s_location_form.city == nullptr || s_location_form.latitude == nullptr ||
            s_location_form.longitude == nullptr || s_location_form.status == nullptr) return;

    const char *city = lv_textarea_get_text(s_location_form.city);
    const char *lat_text = lv_textarea_get_text(s_location_form.latitude);
    const char *lon_text = lv_textarea_get_text(s_location_form.longitude);
    if (city == nullptr || city[0] == '\0') {
        lv_label_set_text(s_location_form.status, "Enter a city label for Weather");
        return;
    }
    if (lat_text == nullptr || lat_text[0] == '\0' ||
            lon_text == nullptr || lon_text[0] == '\0') {
        lv_label_set_text(s_location_form.status,
                          "Enter latitude and longitude. A city name alone cannot set the location.");
        return;
    }

    char *end = nullptr;
    const double latitude = strtod(lat_text, &end);
    if (end == lat_text || end == nullptr || *end != '\0') {
        lv_label_set_text(s_location_form.status, "Latitude must be a number, e.g. 25.03");
        return;
    }
    end = nullptr;
    const double longitude = strtod(lon_text, &end);
    if (end == lon_text || end == nullptr || *end != '\0') {
        lv_label_set_text(s_location_form.status, "Longitude must be a number, e.g. 121.57");
        return;
    }
    if (!std::isfinite(latitude) || !std::isfinite(longitude) ||
            latitude < -90.0 || latitude > 90.0 ||
            longitude < -180.0 || longitude > 180.0) {
        lv_label_set_text(s_location_form.status,
                          "Latitude must be -90 to 90 and longitude -180 to 180");
        return;
    }
    if (!crystal_weather_set_location(latitude, longitude, city)) {
        lv_label_set_text(s_location_form.status, "Could not save the manual location");
        return;
    }
    settings_row_set_summary(s_region_location_row, "Manual");
    settings_row_set_summary(s_root_region_row, "Manual location");
    system_page_pop();
}

void settings_push_location()
{
    lv_obj_t *content = system_page_push("Location");
    if (content == nullptr) return;
    s_location_form.page = lv_obj_get_parent(content);
    lv_obj_t *auto_row = settings_row(content, "Automatic Location", "Uses the network location");
    lv_obj_t *automatic = settings_switch(auto_row, crystal_weather_location_automatic());
    s_location_form.city = lv_textarea_create(content);
    lv_obj_set_size(s_location_form.city, LV_PCT(100), kSettingsInputHeight);
    settings_style_input(s_location_form.city);
    lv_textarea_set_one_line(s_location_form.city, true);
    lv_textarea_set_placeholder_text(s_location_form.city, "City label for display (required)");
    lv_textarea_set_max_length(s_location_form.city, 23);
    if (hal().storage != nullptr) {
        char city[24] = {};
        size_t length = sizeof(city);
        if (hal().storage->get("weather.city", city, &length)) {
            city[length < sizeof(city) ? length : sizeof(city) - 1] = '\0';
            lv_textarea_set_text(s_location_form.city, city);
        }
    }
    s_location_form.latitude = lv_textarea_create(content);
    lv_obj_set_size(s_location_form.latitude, LV_PCT(100), kSettingsInputHeight);
    settings_style_input(s_location_form.latitude);
    lv_textarea_set_one_line(s_location_form.latitude, true);
    lv_textarea_set_placeholder_text(s_location_form.latitude, "Latitude -90 to 90 (required)");
    lv_textarea_set_max_length(s_location_form.latitude, 16);
    s_location_form.longitude = lv_textarea_create(content);
    lv_obj_set_size(s_location_form.longitude, LV_PCT(100), kSettingsInputHeight);
    settings_style_input(s_location_form.longitude);
    lv_textarea_set_one_line(s_location_form.longitude, true);
    lv_textarea_set_placeholder_text(s_location_form.longitude, "Longitude -180 to 180 (required)");
    lv_textarea_set_max_length(s_location_form.longitude, 16);
    if (hal().storage != nullptr) {
        double latitude = 0.0;
        double longitude = 0.0;
        size_t length = sizeof(latitude);
        const bool has_latitude = hal().storage->get("weather.lat", &latitude, &length) &&
                                  length == sizeof(latitude);
        length = sizeof(longitude);
        const bool has_longitude = hal().storage->get("weather.lon", &longitude, &length) &&
                                   length == sizeof(longitude);
        char coordinate[24] = {};
        if (has_latitude && std::isfinite(latitude)) {
            snprintf(coordinate, sizeof(coordinate), "%.6f", latitude);
            lv_textarea_set_text(s_location_form.latitude, coordinate);
        }
        if (has_longitude && std::isfinite(longitude)) {
            snprintf(coordinate, sizeof(coordinate), "%.6f", longitude);
            lv_textarea_set_text(s_location_form.longitude, coordinate);
        }
    }
    for (lv_obj_t *field : {s_location_form.city, s_location_form.latitude,
                            s_location_form.longitude}) {
        lv_obj_add_event_cb(field, ip_field_focus, LV_EVENT_FOCUSED, nullptr);
    }
    set_location_fields_enabled(!crystal_weather_location_automatic());
    lv_obj_add_event_cb(automatic, location_mode_changed, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_t *apply = settings_row(content, "Apply Manual Location", "Refreshes Weather immediately");
    lv_obj_add_event_cb(apply, location_apply, LV_EVENT_CLICKED, nullptr);
    s_location_form.status = settings_status_label(content);
}

bool parse_digits(const char *text, size_t offset, size_t count, int *out)
{
    if (text == nullptr || out == nullptr) return false;
    int value = 0;
    for (size_t i = 0; i < count; ++i) {
        const char c = text[offset + i];
        if (c < '0' || c > '9') return false;
        value = value * 10 + c - '0';
    }
    *out = value;
    return true;
}

void manual_time_apply(lv_event_t *)
{
    if (s_manual_time_form.date == nullptr || s_manual_time_form.clock == nullptr ||
            s_manual_time_form.status == nullptr) return;
    const char *date = lv_textarea_get_text(s_manual_time_form.date);
    const char *clock = lv_textarea_get_text(s_manual_time_form.clock);
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    if (date == nullptr || clock == nullptr || strlen(date) != 10 || strlen(clock) != 5 ||
            date[4] != '-' || date[7] != '-' || clock[2] != ':' ||
            !parse_digits(date, 0, 4, &year) || !parse_digits(date, 5, 2, &month) ||
            !parse_digits(date, 8, 2, &day) || !parse_digits(clock, 0, 2, &hour) ||
            !parse_digits(clock, 3, 2, &minute)) {
        lv_label_set_text(s_manual_time_form.status, "Use YYYY-MM-DD and HH:MM");
        return;
    }

    struct tm value = {};
    value.tm_year = year - 1900;
    value.tm_mon = month - 1;
    value.tm_mday = day;
    value.tm_hour = hour;
    value.tm_min = minute;
    value.tm_isdst = -1;
    struct tm normalized = value;
    const time_t epoch = mktime(&normalized);
    if (epoch < 1577836800 || normalized.tm_year != value.tm_year ||
            normalized.tm_mon != value.tm_mon || normalized.tm_mday != value.tm_mday ||
            normalized.tm_hour != value.tm_hour || normalized.tm_min != value.tm_min) {
        lv_label_set_text(s_manual_time_form.status, "Check the date and time values");
        return;
    }
    if (!crystal_time_set(&value)) {
        lv_label_set_text(s_manual_time_form.status, "Could not update the system clock and RTC");
        return;
    }
    system_page_pop();
}

void settings_push_manual_time()
{
    lv_obj_t *content = system_page_push("Set Date & Time");
    if (content == nullptr) return;
    s_manual_time_form.page = lv_obj_get_parent(content);
    s_manual_time_form.date = lv_textarea_create(content);
    lv_obj_set_size(s_manual_time_form.date, LV_PCT(100), kSettingsInputHeight);
    settings_style_input(s_manual_time_form.date);
    lv_textarea_set_one_line(s_manual_time_form.date, true);
    lv_textarea_set_placeholder_text(s_manual_time_form.date, "YYYY-MM-DD");
    lv_textarea_set_max_length(s_manual_time_form.date, 10);
    s_manual_time_form.clock = lv_textarea_create(content);
    lv_obj_set_size(s_manual_time_form.clock, LV_PCT(100), kSettingsInputHeight);
    settings_style_input(s_manual_time_form.clock);
    lv_textarea_set_one_line(s_manual_time_form.clock, true);
    lv_textarea_set_placeholder_text(s_manual_time_form.clock, "HH:MM");
    lv_textarea_set_max_length(s_manual_time_form.clock, 5);
    lv_obj_add_event_cb(s_manual_time_form.date, ip_field_focus, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(s_manual_time_form.clock, ip_field_focus, LV_EVENT_FOCUSED, nullptr);
    lv_obj_t *apply = settings_row(content, "Set Date & Time", "Writes the system clock and RTC");
    lv_obj_add_event_cb(apply, manual_time_apply, LV_EVENT_CLICKED, nullptr);
    s_manual_time_form.status = settings_status_label(content);
}

void settings_push_region_time()
{
    lv_obj_t *content = system_page_push("Region & Time");
    if (content == nullptr) return;
    char sync_summary[48] = "No successful sync yet";
    const int32_t last_sync = crystal_time_last_sync();
    const time_t last_sync_epoch = static_cast<time_t>(last_sync);
    struct tm sync_time = {};
    if (last_sync > 0 && localtime_r(&last_sync_epoch, &sync_time) != nullptr) {
        snprintf(sync_summary, sizeof(sync_summary), "Last sync %04d-%02d-%02d %02d:%02d",
                 sync_time.tm_year + 1900, sync_time.tm_mon + 1, sync_time.tm_mday,
                 sync_time.tm_hour, sync_time.tm_min);
    }
    lv_obj_t *auto_row = settings_row(content, "Set Time Automatically", sync_summary);
    s_region_auto_time_row = auto_row;
    lv_obj_t *automatic = settings_switch(auto_row, crystal_time_auto_enabled());
    lv_obj_add_event_cb(automatic, [](lv_event_t *e) {
        const bool enabled = lv_obj_has_state(static_cast<lv_obj_t *>(lv_event_get_target(e)), LV_STATE_CHECKED);
        crystal_time_set_auto(enabled);
        settings_row_set_summary(s_region_auto_time_row, enabled ? "Automatic time enabled" : "Automatic time disabled");
        settings_row_set_summary(s_region_manual_time_row, enabled ? "Turn automatic time off first" : "Manual");
        if (s_region_manual_time_row != nullptr) {
            if (enabled) lv_obj_add_state(s_region_manual_time_row, LV_STATE_DISABLED);
            else lv_obj_clear_state(s_region_manual_time_row, LV_STATE_DISABLED);
        }
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_t *timezone = settings_row(content, "Timezone", current_timezone_label());
    s_region_timezone_row = timezone;
    lv_obj_add_event_cb(timezone, [](lv_event_t *) { settings_push_timezones(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *format_row = settings_row(content, "24-Hour Time", crystal_time_format_24() ? "24-hour" : "12-hour");
    s_region_format_row = format_row;
    lv_obj_t *format = settings_switch(format_row, crystal_time_format_24());
    lv_obj_add_event_cb(format, [](lv_event_t *e) {
        const bool enabled = lv_obj_has_state(static_cast<lv_obj_t *>(lv_event_get_target(e)), LV_STATE_CHECKED);
        crystal_time_set_format_24(enabled);
        settings_row_set_summary(s_region_format_row, enabled ? "24-hour" : "12-hour");
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_t *manual = settings_row(content, "Set Date & Time", crystal_time_auto_enabled() ? "Turn automatic time off first" : "Manual");
    s_region_manual_time_row = manual;
    if (crystal_time_auto_enabled()) lv_obj_add_state(manual, LV_STATE_DISABLED);
    lv_obj_add_event_cb(manual, [](lv_event_t *) {
        if (!crystal_time_auto_enabled()) settings_push_manual_time();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *location = settings_row(content, "Location", crystal_weather_location_automatic() ? "Automatic" : "Manual");
    s_region_location_row = location;
    lv_obj_add_event_cb(location, [](lv_event_t *) { settings_push_location(); }, LV_EVENT_CLICKED, nullptr);
}

void settings_push_legal()
{
    lv_obj_t *content = system_page_push("Legal & Attribution");
    if (content == nullptr) return;
    (void)settings_row(content, "Crystal OS", "PolyForm Noncommercial 1.0.0");
    (void)settings_row(content, "ESP-IDF", "Copyright Espressif Systems");
    (void)settings_row(content, "ESP-Brookesia", "Copyright Espressif Systems");
    (void)settings_row(content, "Third-Party Notices", "See NOTICE in the firmware source");
}

void settings_push_about()
{
    lv_obj_t *content = system_page_push("About");
    if (content == nullptr) return;
    ISystemInfo *info = hal().system_info;
    (void)settings_row(content, "Device", "Crystal OS Display");
    (void)settings_row(content, "Hardware",
#if CONFIG_IDF_TARGET_ESP32P4
                       "Waveshare ESP32-P4-WIFI6-Touch-LCD-4B");
#else
                       "Waveshare ESP32-S3-Touch-LCD-4B");
#endif
    (void)settings_row(content, "Platform", "Crystal OS open-source project");
    (void)settings_row(content, "Crystal OS", info != nullptr ? info->app_version() : "Unknown");
    (void)settings_row(content, "ESP-IDF", info != nullptr ? info->idf_version() : "Unknown");
    char id[32] = "Unavailable";
    uint8_t bytes[6] = {};
    if (info != nullptr && info->chip_id(bytes)) snprintf(id, sizeof(id), "%02X%02X%02X%02X%02X%02X", bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
    (void)settings_row(content, "Serial / Chip ID", id);
    lv_obj_t *legal = settings_row(content, "Legal & Attribution", "Licenses and required credits");
    lv_obj_add_event_cb(legal, [](lv_event_t *) { settings_push_legal(); }, LV_EVENT_CLICKED, nullptr);
}

void settings_push_status()
{
    lv_obj_t *content = system_page_push("Device Status");
    if (content == nullptr) return;
    ISystemInfo *info = hal().system_info;
    char value[64] = "Unavailable";
    (void)settings_row(content, "Firmware Version",
                       info != nullptr && info->app_version() != nullptr
                           ? info->app_version() : "Unknown");
    (void)settings_row(content, "Last Crash",
                       info != nullptr && strcmp(info->reset_reason(), "Software crash") == 0
                           ? "Crash recovered" : "None");
    if (info != nullptr) { snprintf(value, sizeof(value), "%lu seconds", static_cast<unsigned long>(info->uptime_seconds())); (void)settings_row(content, "Uptime", value); }
    (void)settings_row(content, "WiFi", hal().wifi != nullptr && hal().wifi->has_ip() ? hal().wifi->last_ssid() : "Not connected");
    (void)settings_row(content, "Active Connection", crystal_network_type_name(crystal_network_active_type()));
    IWifi::IpConfig ip = {};
    const auto ethernet = crystal_ethernet_info();
    if (crystal_network_active_type() == CrystalNetworkType::Ethernet && ethernet.has_ip)
        ip4addr_ntoa_r(reinterpret_cast<const ip4_addr_t *>(&ethernet.ip), value, sizeof(value));
    else if (hal().wifi != nullptr && hal().wifi->ip_config(&ip))
        ip4addr_ntoa_r(reinterpret_cast<const ip4_addr_t *>(&ip.ip), value, sizeof(value));
    else strlcpy(value, "Unavailable", sizeof(value));
    (void)settings_row(content, "IP Address", value);
    int percent = 0; bool charging = false;
    if (crystal_battery_cached(&percent, &charging)) snprintf(value, sizeof(value), "%d%% - %s", percent, charging ? "Charging" : "On battery"); else strlcpy(value, "Waiting for reading", sizeof(value));
    (void)settings_row(content, "Battery", value);
    if (info != nullptr) {
        snprintf(value, sizeof(value), "%lu KiB", static_cast<unsigned long>(info->free_heap() / 1024)); (void)settings_row(content, "Free Internal Heap", value);
        snprintf(value, sizeof(value), "%lu KiB", static_cast<unsigned long>(info->free_psram() / 1024)); (void)settings_row(content, "Free PSRAM", value);
        uint32_t used = 0, total = 0; if (info->storage_bytes(&used, &total)) { snprintf(value, sizeof(value), "%lu / %lu KiB", static_cast<unsigned long>(used / 1024), static_cast<unsigned long>(total / 1024)); (void)settings_row(content, "Storage Used", value); }
        (void)settings_row(content, "Last Reset", info->reset_reason());
    }
    lv_obj_t *refresh = settings_row(content, "Refresh", "Read current cached and system values");
    lv_obj_add_event_cb(refresh, [](lv_event_t *) { system_page_pop(); settings_push_status(); }, LV_EVENT_CLICKED, nullptr);
}

void system_dialog_close()
{
    if (s_system_dialog != nullptr) lv_obj_del(s_system_dialog);
    s_system_dialog = nullptr;
    crystal_shell_set_modal_open(false);
}

void open_restart_confirm()
{
    if (s_system_dialog != nullptr) return;
    s_system_dialog = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_system_dialog, kSettingsConfirmWidth, kSettingsConfirmHeight);
    lv_obj_center(s_system_dialog);
    lv_obj_set_style_bg_color(s_system_dialog, lv_color_hex(0x252a30), 0);
    lv_obj_set_style_bg_opa(s_system_dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_system_dialog, 8, 0);
    lv_obj_t *title = lv_label_create(s_system_dialog); lv_label_set_text(title, "Restart Crystal OS?"); lv_obj_set_style_text_font(title, kSettingsTitleFont, 0); lv_obj_set_style_text_color(title, lv_color_white(), 0); lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_t *detail = lv_label_create(s_system_dialog); lv_label_set_text(detail, "The display will be unavailable briefly.\nNo settings or app data will be erased."); lv_obj_set_width(detail, kSettingsConfirmWidth - 48); lv_obj_set_style_text_font(detail, kSettingsSummaryFont, 0); lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_color(detail, lv_color_hex(0xcbd5e1), 0); lv_obj_align(detail, LV_ALIGN_TOP_MID, 0,
#if CONFIG_IDF_TARGET_ESP32P4
                 62);
#else
                 48);
#endif
    lv_obj_t *restart = lv_btn_create(s_system_dialog); lv_obj_set_size(restart, kSettingsConfirmButtonWidth, kSettingsConfirmButtonHeight); lv_obj_align(restart, LV_ALIGN_BOTTOM_LEFT, 12, -10); lv_obj_t *rl = lv_label_create(restart); lv_label_set_text(rl, "Restart"); lv_obj_set_style_text_font(rl, kSettingsSummaryFont, 0); lv_obj_center(rl); lv_obj_add_event_cb(restart, [](lv_event_t *) { esp_restart(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cancel = lv_btn_create(s_system_dialog); lv_obj_set_size(cancel, kSettingsConfirmButtonWidth, kSettingsConfirmButtonHeight); lv_obj_align(cancel, LV_ALIGN_BOTTOM_RIGHT, -12, -10); lv_obj_t *cl = lv_label_create(cancel); lv_label_set_text(cl, "Cancel"); lv_obj_set_style_text_font(cl, kSettingsSummaryFont, 0); lv_obj_center(cl); lv_obj_add_event_cb(cancel, [](lv_event_t *) { system_dialog_close(); }, LV_EVENT_CLICKED, nullptr);
    crystal_shell_set_modal_open(true);
}

bool manage_apps_clear_data(const CrystalAppEntry *entry)
{
    if (entry == nullptr || s_phone == nullptr) return false;
    CrystalApp *app = nullptr;
    for (size_t i = 0; i < crystal_registry_installed_count(); ++i) {
        const char *id = crystal_registry_installed_id(i);
        if (id != nullptr && strcmp(id, entry->id) == 0) {
            app = crystal_registry_installed_app(i);
            break;
        }
    }
    // Newly enabled apps have no installed instance until restart. A temporary
    // factory instance supplies the exact namespace without launching its UI.
    std::unique_ptr<CrystalApp> temporary;
    if (app == nullptr) {
        temporary.reset(crystal_registry_create_app(entry));
        app = temporary.get();
    }
    if (app == nullptr) return false;
    auto &manager = s_phone->getManager();
    // Absence is expected before clearing an idle app and after STOP. The
    // ID lookup logs an error on absence, so inspect the running list quietly.
    const auto is_running = [&] {
        for (uint8_t i = 0; i < manager.getRunningAppCount(); ++i) {
            if (manager.getRunningAppByIdenx(i) == app) return true;
        }
        return false;
    };
    if (!temporary && is_running()) {
        ESP_Brookesia_CoreAppEventData_t event = {
            .id = app->getId(), .type = ESP_BROOKESIA_CORE_APP_EVENT_TYPE_STOP, .data = nullptr,
        };
        if (!s_phone->sendAppEvent(&event) || is_running()) return false;
    }
    // onPause/onDestroy can save old values. Erase only after STOP completes.
    return app->clearData();
}

void manage_apps_page_open()
{
    lv_obj_t *content = system_page_push("Manage Apps");
    if (content != nullptr) {
        s_manage_apps_page = lv_obj_get_parent(content);
        crystal_manage_apps_page_build(content, manage_apps_clear_data);
    }
}

void manage_apps_restart_prompt_close()
{
    if (s_manage_apps_restart_dialog != nullptr) {
        lv_obj_del(s_manage_apps_restart_dialog);
        s_manage_apps_restart_dialog = nullptr;
    }
    crystal_shell_set_modal_open(false);
}

void manage_apps_restart_later()
{
    manage_apps_restart_prompt_close();
    if (s_manage_apps_page != nullptr && s_system_page_depth > 0 &&
            s_system_page_stack[s_system_page_depth - 1] == s_manage_apps_page) {
        system_page_pop();
    }
}

void open_manage_apps_restart_prompt()
{
    if (s_manage_apps_restart_dialog != nullptr) return;
    s_manage_apps_restart_dialog = lv_obj_create(lv_layer_top());
    if (s_manage_apps_restart_dialog == nullptr) return;
    lv_obj_set_size(s_manage_apps_restart_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_manage_apps_restart_dialog, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_manage_apps_restart_dialog, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_manage_apps_restart_dialog, 0, 0);
    lv_obj_add_flag(s_manage_apps_restart_dialog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_manage_apps_restart_dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(s_manage_apps_restart_dialog);
    lv_obj_set_size(card, kManageRestartWidth, kManageRestartHeight);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x252a30), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x59636e), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card,
#if CONFIG_IDF_TARGET_ESP32P4
                             24,
#else
                             22,
#endif
                             0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(card);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "Restart to Apply Changes?");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, kSettingsTitleFont, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *detail = lv_label_create(card);
    lv_obj_set_width(detail, kManageRestartWidth - 48);
    lv_obj_set_height(detail,
#if CONFIG_IDF_TARGET_ESP32P4
                      96);
#else
                      72);
#endif
    lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
    lv_label_set_text(detail,
                      "Your app changes have been saved. Restart Crystal OS now to apply them?");
    lv_obj_set_style_text_color(detail, lv_color_hex(0xcbd5e1), 0);
    lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(detail, kSettingsSummaryFont, 0);
    lv_obj_align(detail, LV_ALIGN_TOP_MID, 0,
#if CONFIG_IDF_TARGET_ESP32P4
                 64);
#else
                 48);
#endif

    lv_obj_t *restart = lv_btn_create(card);
    lv_obj_set_size(restart, kManageRestartButtonWidth, kManageRestartButtonHeight);
    lv_obj_align(restart, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(restart, lv_color_hex(0x3b82f6), 0);
    lv_obj_t *restart_label = lv_label_create(restart);
    lv_label_set_text(restart_label, "Restart now");
    lv_obj_set_style_text_font(restart_label, kSettingsSummaryFont, 0);
    lv_obj_center(restart_label);
    lv_obj_add_event_cb(restart, [](lv_event_t *) { esp_restart(); },
                        LV_EVENT_CLICKED, nullptr);

    lv_obj_t *later = lv_btn_create(card);
    lv_obj_set_size(later, kManageRestartButtonWidth, kManageRestartButtonHeight);
    lv_obj_align(later, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(later, lv_color_hex(0x424852), 0);
    lv_obj_t *later_label = lv_label_create(later);
    lv_label_set_text(later_label, "Later");
    lv_obj_set_style_text_font(later_label, kSettingsSummaryFont, 0);
    lv_obj_center(later_label);
    lv_obj_add_event_cb(later, [](lv_event_t *) { manage_apps_restart_later(); },
                        LV_EVENT_CLICKED, nullptr);
    crystal_shell_set_modal_open(true);
}

void settings_push_system()
{
    lv_obj_t *content = system_page_push("System");
    if (content == nullptr) return;
    lv_obj_t *updates = settings_row(content, "Check for Updates", "Check for a newer Crystal OS firmware");
    lv_obj_add_event_cb(updates, [](lv_event_t *) {
        ESP_LOGI("settings", "Check for Updates tapped");
        (void)crystal_update_check();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *about = settings_row(content, "About", "Device, software, and attribution");
    lv_obj_add_event_cb(about, [](lv_event_t *) { settings_push_about(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *status = settings_row(content, "Device Status", "Network, battery, memory, and storage");
    lv_obj_add_event_cb(status, [](lv_event_t *) { settings_push_status(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *restart = settings_row(content, "Restart", "Restarts without erasing data");
    lv_obj_set_style_text_color(lv_obj_get_child(restart, 0), lv_color_hex(0xf87171), 0);
    lv_obj_add_event_cb(restart, [](lv_event_t *) { open_restart_confirm(); }, LV_EVENT_CLICKED, nullptr);
}

void control_panel_toggle_changed(lv_event_t *event)
{
    auto *control = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if (control != s_control_api_switch || s_control_api_row == nullptr ||
            s_control_status_label == nullptr) return;
    if (s_control_start_pending) {
        lv_obj_add_state(control, LV_STATE_CHECKED | LV_STATE_DISABLED);
        return;
    }
    if (!lv_obj_has_state(control, LV_STATE_CHECKED)) {
        control_pair_dialog_close();
        crystal_control_disable();
        settings_row_set_summary(s_control_api_row, "Off - enable to pair a browser");
        lv_label_set_text(s_control_status_label, "Local control panel disabled.");
        lv_obj_set_style_text_color(s_control_status_label, lv_color_hex(0x91a0b3), 0);
        return;
    }
    if (!crystal_network_has_ip()) {
        lv_obj_clear_state(control, LV_STATE_CHECKED);
        settings_row_set_summary(s_control_api_row, "Off - enable to pair a browser");
        lv_label_set_text(s_control_status_label, "Connect WiFi or Ethernet before enabling the local panel.");
        lv_obj_set_style_text_color(s_control_status_label, lv_color_hex(0xf59e0b), 0);
        return;
    }
    s_control_start_pending = true;
    lv_obj_add_state(control, LV_STATE_DISABLED);
    settings_row_set_summary(s_control_api_row, "Starting...");
    lv_label_set_text(s_control_status_label, "Starting the local control panel...");
    lv_obj_set_style_text_color(s_control_status_label, lv_color_hex(0x91a0b3), 0);
    // This temporary worker otherwise competes with the HTTP server's
    // 8 KiB internal stack while TLS is active. Keep its stack in PSRAM.
    if (xTaskCreateWithCaps(control_panel_start_worker, "control_start", 6144,
                    nullptr, 2, nullptr, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_control_start_pending = false;
        lv_obj_clear_state(control, LV_STATE_CHECKED | LV_STATE_DISABLED);
        settings_row_set_summary(s_control_api_row, "Off - enable to pair a browser");
        lv_label_set_text(s_control_status_label, "Could not start the local panel. Try again.");
        lv_obj_set_style_text_color(s_control_status_label, lv_color_hex(0xf87171), 0);
    }
}

void settings_push_developer_tools()
{
    lv_obj_t *content = system_page_push("Developer Tools");
    if (content == nullptr) return;
    s_developer_tools_page = s_system_page_stack[s_system_page_depth - 1];

    lv_obj_t *status = settings_status_label(content);
    s_control_status_label = status;
    const bool enabled = crystal_control_enabled();
    lv_label_set_text(status, s_control_start_pending ?
        "Starting the local control panel..." :
        enabled ? "Local control panel is enabled for this boot." :
                  "The local API is off until you enable it.");
    lv_obj_set_style_text_color(status, lv_color_hex(0x91a0b3), 0);

    lv_obj_t *api = settings_row(content, "Local Control Panel",
        s_control_start_pending ? "Starting..." :
        enabled ? "Enabled for this boot" : "Off - enable to pair a browser");
    s_control_api_row = api;
    s_control_api_switch = settings_switch(api, enabled || s_control_start_pending);
    if (s_control_start_pending) lv_obj_add_state(s_control_api_switch, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_control_api_switch, control_panel_toggle_changed,
                        LV_EVENT_VALUE_CHANGED, nullptr);

    (void)settings_row(content, "Catalog", "Signed online catalog and HTTPS .capp downloads");
    lv_obj_t *note = settings_status_label(content);
    lv_label_set_text(note, "Use the Wi-Fi panel to install and manage apps. Developers can also install using the USB command-line tool.");
    lv_obj_set_style_text_color(note, lv_color_hex(0x91a0b3), 0);
}

void crystal_shell_control_start_event(const char pairing_pin[7])
{
    s_control_start_pending = false;
    // If the user left Developer Tools while the worker was starting, do not
    // leave an enabled server running without showing its one-time pairing PIN.
    if (s_developer_tools_page == nullptr || s_control_api_row == nullptr ||
            s_control_api_switch == nullptr ||
            s_control_status_label == nullptr || s_system_page_depth == 0 ||
            s_system_page_stack[s_system_page_depth - 1] != s_developer_tools_page) {
        if (pairing_pin != nullptr) crystal_control_disable();
        return;
    }
    if (pairing_pin == nullptr) {
        lv_obj_clear_state(s_control_api_switch, LV_STATE_CHECKED | LV_STATE_DISABLED);
        settings_row_set_summary(s_control_api_row, "Off - enable to pair a browser");
        lv_label_set_text(s_control_status_label, "Could not start the local panel. Try again.");
        lv_obj_set_style_text_color(s_control_status_label, lv_color_hex(0xf87171), 0);
        return;
    }

    lv_obj_clear_state(s_control_api_switch, LV_STATE_DISABLED);
    lv_obj_add_state(s_control_api_switch, LV_STATE_CHECKED);
    settings_row_set_summary(s_control_api_row, "Enabled for this boot");
    lv_label_set_text(s_control_status_label, "Panel enabled. Pair within 10 minutes using the code on screen.");
    lv_obj_set_style_text_color(s_control_status_label, lv_color_hex(0x4ade80), 0);

    IWifi *wifi = hal().wifi;
    IWifi::IpConfig config = {};
    char address[24] = {};
    char url[48] = "http://device/";
    const auto ethernet = crystal_ethernet_info();
    if (crystal_network_active_type() == CrystalNetworkType::Ethernet && ethernet.has_ip) {
        ip4addr_ntoa_r(reinterpret_cast<const ip4_addr_t *>(&ethernet.ip), address, sizeof(address));
        snprintf(url, sizeof(url), "http://%s/", address);
    } else if (wifi != nullptr && wifi->ip_config(&config)) {
        ip4addr_ntoa_r(reinterpret_cast<const ip4_addr_t *>(&config.ip), address, sizeof(address));
        snprintf(url, sizeof(url), "http://%s/", address);
    }
    control_pair_dialog_build(pairing_pin, url);
}

void crystal_shell_control_action_event(const CrystalControlAction *action)
{
    if (action == nullptr || action->id[0] == '\0' || s_phone == nullptr) return;
    CrystalApp *app = nullptr;
    size_t app_index = SIZE_MAX;
    for (size_t i = 0; i < crystal_registry_installed_count(); ++i) {
        if (crystal_registry_installed_id(i) != nullptr && !strcmp(crystal_registry_installed_id(i), action->id)) {
            app = crystal_registry_installed_app(i);
            app_index = i;
            break;
        }
    }
    if (action->kind == CRYSTAL_CONTROL_ACTION_LAUNCH) {
        if (app == nullptr) {
            (void)crystal_registry_apply_changes();
            for (size_t i = 0; i < crystal_registry_installed_count(); ++i) {
                if (crystal_registry_installed_id(i) != nullptr && !strcmp(crystal_registry_installed_id(i), action->id)) {
                    app = crystal_registry_installed_app(i);
                    app_index = i;
                    break;
                }
            }
        }
        if (app != nullptr && start_card(app_index, false)) {
            // Keep Settings visible, but make its close action return to the
            // app just launched instead of restoring the previous selection.
            if (s_system_page_depth != 0) {
                s_last_app_before_settings = static_cast<int>(app_index);
            }
            update_home_pill();
            ESP_LOGI(TAG, "Control-panel launched %s%s", action->id,
                     s_system_page_depth != 0 ? " behind Settings" : "");
            return;
        }
        ESP_LOGW(TAG, "Control-panel launch failed for %s", action->id);
        return;
    }
    if (action->kind == CRYSTAL_CONTROL_ACTION_UNINSTALL) {
        if (crystal_registry_uninstall_package(action->id)) {
            ESP_LOGI(TAG, "Control-panel uninstalled %s", action->id);
        } else {
            ESP_LOGW(TAG, "Control-panel uninstall failed for %s", action->id);
        }
        return;
    }
    if (action->kind == CRYSTAL_CONTROL_ACTION_CLEAR_DATA) {
        const CrystalAppEntry *entry = nullptr;
        for (size_t i = 0; i < crystal_registry_catalog_count(); ++i) {
            const auto *candidate = crystal_registry_catalog_entry(i);
            if (candidate != nullptr && candidate->available && !strcmp(candidate->id, action->id)) {
                entry = candidate;
                break;
            }
        }
        // Settings can leave an app paused in the running map. Use the same
        // verified STOP-before-clear path as on-device Manage Apps.
        if (manage_apps_clear_data(entry)) {
            ESP_LOGI(TAG, "Control-panel clear data succeeded for %s", action->id);
        } else {
            ESP_LOGW(TAG, "Control-panel clear data failed for %s", action->id);
        }
    }
}

void settings_open()
{
    if (s_system_page_depth != 0) return;
    lv_obj_t *backdrop = nullptr;
    crystal_page_transition_settle();
    // Save what app was active before opening Settings
    if (s_phone != nullptr && s_phone->getManager().getActiveApp() != nullptr) {
        s_last_app_before_settings = static_cast<int>(s_current_index);
#if CONFIG_IDF_TARGET_ESP32P4
        // Only a translucent transition needs an app snapshot. Immediate
        // Settings covers HOME in this LVGL dispatch without allocating one.
        if (crystal_visual_style().page_transition_ms != 0) {
            backdrop = crystal_page_transition_capture_backdrop(active_app_area());
            if (backdrop == nullptr) {
                ESP_LOGW(TAG, "Cannot create Settings backdrop; keeping the app visible");
                s_last_app_before_settings = -1;
                return;
            }
        }
#endif
        ESP_LOGI(TAG, "Opening Settings from app index %d, pausing app", s_last_app_before_settings);
        // Preserve app pause/restore; the Settings page covers the next frame.
        (void)s_phone->sendNavigateEvent(ESP_BROOKESIA_CORE_NAVIGATE_TYPE_HOME);
    } else {
        s_last_app_before_settings = -1;  // Opened from launcher
        ESP_LOGI(TAG, "Opening Settings from launcher");
    }
    lv_obj_t *content = system_page_push("Settings");
    if (content == nullptr) {
        if (s_last_app_before_settings >= 0) {
            (void)start_card(static_cast<size_t>(s_last_app_before_settings), false);
        }
        if (backdrop != nullptr) lv_obj_del(backdrop);
        s_last_app_before_settings = -1;
        return;
    }
    s_root_settings_page = lv_obj_get_parent(content);
    crystal_page_transition_bind_backdrop(s_root_settings_page, backdrop);
    IWifi *wifi = hal().wifi;
    const char *network_summary = crystal_network_active_type() == CrystalNetworkType::Ethernet ? "Ethernet connected" :
                                  wifi == nullptr || !wifi->enabled() ? "WiFi Off" :
                                  wifi->connected() ? wifi->last_ssid() : "WiFi On - Not connected";
    lv_obj_t *network = settings_row(content, "Network", network_summary);
    s_root_network_row = network;
    lv_obj_add_event_cb(network, [](lv_event_t *) { settings_push_network(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *power = settings_row(content, "Display & Power", crystal_power_saving_enabled() ? "Energy Saving On" : "Energy Saving Off");
    s_root_power_row = power;
    lv_obj_add_event_cb(power, [](lv_event_t *) { settings_push_display_power(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *sound = settings_row(content, "Sound", crystal_sound_alerts_enabled() ? "Alerts On" : "Alerts Off");
    s_root_sound_row = sound;
    lv_obj_add_event_cb(sound, [](lv_event_t *) { settings_push_sound(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *region = settings_row(content, "Region & Time", current_timezone_label());
    s_root_region_row = region;
    lv_obj_add_event_cb(region, [](lv_event_t *) { settings_push_region_time(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *manage = settings_row(content, "Manage Apps", "View installed and available apps");
    lv_obj_add_event_cb(manage, [](lv_event_t *) { manage_apps_page_open(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *developer = settings_row(content, "Developer Tools", "Local control panel and pairing");
    lv_obj_add_event_cb(developer, [](lv_event_t *) { settings_push_developer_tools(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *system = settings_row(content, "System", hal().system_info != nullptr ? hal().system_info->app_version() : "About and status");
    lv_obj_add_event_cb(system, [](lv_event_t *) { settings_push_system(); }, LV_EVENT_CLICKED, nullptr);
}

void settings_open_at_wifi()
{
    settings_open();
    settings_push_network();
    wifi_page_open();
}

// The only teardown path for the WiFi page. Every cached pointer into the page
// tree is cleared here, because lv_obj_del() frees the children too and a stale
// pointer passes the != nullptr guards at every use site.
void wifi_page_close()
{
    if (s_wifi_page == nullptr) return;
    if (s_system_page_depth != 0 && s_system_page_stack[s_system_page_depth - 1] == s_wifi_page) {
        system_page_pop();
    }
}

// One Back dismisses one shell layer. Returning false means the app is now the
// topmost layer and its normal Back behaviour should run.
bool shell_consume_back()
{
    if (crystal_manage_apps_page_dismiss_confirmation()) return true;
    if (crystal_keyboard_is_open()) {
        crystal_keyboard_hide();
        return true;
    }
    if (s_wifi_dialog != nullptr) {
        wifi_close_credentials();
        return true;
    }
    if (s_system_dialog != nullptr) {
        system_dialog_close();
        return true;
    }
    if (s_manage_apps_restart_dialog != nullptr) {
        manage_apps_restart_prompt_close();
        return true;
    }
    if (s_update_dialog != nullptr) {
        crystal_update_cancel_check();
        update_dialog_close();
        return true;
    }
    if (s_update_failure_dialog != nullptr) {
        // The failure acknowledgement is explicit; Back and swipes cannot dismiss it.
        return true;
    }
    if (s_system_page_depth > 0) {
        if (s_manage_apps_page != nullptr &&
                s_system_page_stack[s_system_page_depth - 1] == s_manage_apps_page &&
                crystal_manage_apps_page_has_pending_restart()) {
            open_manage_apps_restart_prompt();
            return true;
        }
        // At the Settings root (depth 1), close Settings entirely instead of
        // popping. The back button shows LV_SYMBOL_CLOSE at depth 1 to signal
        // this; the bottom swipe matches that behaviour.
        if (s_system_page_depth == 1) {
            close_settings_and_restore_app();
        } else {
            system_page_pop();
        }
        return true;
    }
    if (s_quick_settings_open) {
        close_quick_settings(nullptr);
        return true;
    }
    return false;
}

static void wifi_scan_busy(bool busy)
{
    s_wifi_scanning = busy;
    if (s_wifi_scan_spinner != nullptr) {
        if (busy) lv_obj_clear_flag(s_wifi_scan_spinner, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_wifi_scan_spinner, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_wifi_refresh != nullptr) {
        if (busy) lv_obj_add_state(s_wifi_refresh, LV_STATE_DISABLED);
        else lv_obj_clear_state(s_wifi_refresh, LV_STATE_DISABLED);
    }
    if (!busy && s_wifi_scan_timeout != nullptr) {
        lv_timer_del(s_wifi_scan_timeout);
        s_wifi_scan_timeout = nullptr;
    }
}

static void wifi_scan_worker(void *)
{
    // Hosted Wi-Fi RPCs can wait for the C6 transport. Keep that wait off the
    // LVGL task; the HAL posts ScanDone through Crystal core as before.
    if (hal().wifi != nullptr) hal().wifi->scan();
    vTaskDeleteWithCaps(nullptr);
}

static void wifi_scan_start()
{
    if (s_wifi_page == nullptr || s_wifi_scanning || hal().wifi == nullptr) return;
    if (!hal().wifi->enabled()) {
        lv_label_set_text(s_wifi_page_status, "WiFi is off. Enable it in Network settings.");
        return;
    }
    wifi_scan_busy(true);
    ESP_LOGI(TAG, "WiFi scan requested");
    lv_label_set_text(s_wifi_page_status, "Finding nearby networks...");
    s_wifi_scan_timeout = lv_timer_create([](lv_timer_t *) {
        wifi_scan_busy(false);
        if (s_wifi_page_status != nullptr) {
            lv_label_set_text(s_wifi_page_status, "Scan timed out. Tap refresh to try again.");
        }
    }, 15000, nullptr);
    if (s_wifi_scan_timeout == nullptr) {
        wifi_scan_busy(false);
        lv_label_set_text(s_wifi_page_status, "Cannot start scan. Please try again.");
        return;
    }
    if (xTaskCreateWithCaps(wifi_scan_worker, "wifi_scan_ui", 4096, nullptr, 2, nullptr,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        wifi_scan_busy(false);
        lv_label_set_text(s_wifi_page_status, "Cannot start scan. Please try again.");
    }
}

void wifi_page_open()
{
    if (s_wifi_page != nullptr) return;
    lv_obj_t *content = system_page_push("WiFi Networks");
    if (content == nullptr) return;
    const auto &style = crystal_visual_style();
    s_wifi_page = s_system_page_stack[s_system_page_depth - 1];
    auto *toolbar = lv_obj_create(content);
    lv_obj_remove_style_all(toolbar);
    lv_obj_set_size(toolbar, LV_PCT(100), kWifiFieldHeight);
    lv_obj_clear_flag(toolbar, LV_OBJ_FLAG_SCROLLABLE);
    s_wifi_page_status = lv_label_create(toolbar);
    lv_label_set_text(s_wifi_page_status, "Nearby networks");
    lv_obj_set_style_text_color(s_wifi_page_status, lv_color_hex(style.secondary), 0);
    lv_obj_set_style_text_font(s_wifi_page_status, kSettingsStatusFont, 0);
    lv_obj_set_width(s_wifi_page_status, lv_disp_get_hor_res(nullptr) -
                     2 * kSettingsContentInset - kWifiFieldHeight - 60);
    lv_label_set_long_mode(s_wifi_page_status, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_wifi_page_status, LV_ALIGN_LEFT_MID, 0, 0);
    s_wifi_scan_spinner = lv_spinner_create(toolbar, 1000, 60);
    lv_obj_set_size(s_wifi_scan_spinner, 28, 28);
    lv_obj_align(s_wifi_scan_spinner, LV_ALIGN_RIGHT_MID, -kWifiFieldHeight - 12, 0);
    lv_obj_set_style_arc_width(s_wifi_scan_spinner, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_wifi_scan_spinner, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_wifi_scan_spinner, lv_color_hex(style.accent), LV_PART_INDICATOR);
    s_wifi_refresh = lv_btn_create(toolbar);
    settings_button_feedback(s_wifi_refresh);
    lv_obj_set_size(s_wifi_refresh, kWifiFieldHeight, kWifiFieldHeight);
    lv_obj_align(s_wifi_refresh, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(s_wifi_refresh, lv_color_hex(style.elevated), 0);
    auto *refresh_icon = lv_label_create(s_wifi_refresh);
    lv_label_set_text(refresh_icon, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(refresh_icon, kWifiTitleFont, 0);
    lv_obj_center(refresh_icon);
    lv_obj_add_event_cb(s_wifi_refresh, [](lv_event_t *) { wifi_scan_start(); }, LV_EVENT_CLICKED, nullptr);
    s_wifi_page_list = lv_list_create(content);
    // Fill the space below the toolbar. A fixed legacy list height leaves a
    // keyboard-sized empty band on the taller P4 after the keyboard closes.
    lv_obj_set_size(s_wifi_page_list, LV_PCT(100), 0);
    lv_obj_set_flex_grow(s_wifi_page_list, 1);
    lv_obj_set_style_pad_all(s_wifi_page_list, 0, 0);
    lv_obj_set_style_pad_row(s_wifi_page_list, 6, 0);
    lv_obj_set_style_bg_color(s_wifi_page_list, lv_color_hex(style.surface), 0);
    lv_obj_set_style_bg_opa(s_wifi_page_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_wifi_page_list, 0, 0);
    lv_obj_clear_flag(s_wifi_page_list, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    wifi_scan_busy(false);
    wifi_scan_start();
}

static void wifi_page_fill_list()
{
    if (s_wifi_page == nullptr || s_wifi_page_list == nullptr || hal().wifi == nullptr || s_wifi_scanning) return;
    const uint32_t render_started = lv_tick_get();
    const auto &style = crystal_visual_style();
    lv_obj_clean(s_wifi_page_list);
    IWifi::Network networks[20] = {};
    const size_t count = std::min<size_t>(hal().wifi->scan_results(networks, 20), 20);
    // connected() is a hosted C6 RPC on P4. Sorting must use one snapshot,
    // rather than making a round trip for every comparison and every row.
    char connected_ssid[33] = {};
    if (hal().wifi->connected()) strlcpy(connected_ssid, hal().wifi->last_ssid(), sizeof(connected_ssid));
    s_wifi_row_count = 0;
    // Snapshot and deduplicate by SSID. Row callbacks use this stable snapshot,
    // not an index into HAL results that can change after another scan.
    for (size_t i = 0; i < count; ++i) {
        if (networks[i].ssid[0] == 0) continue;
        size_t row = 0;
        while (row < s_wifi_row_count && strcmp(s_wifi_rows[row].ssid, networks[i].ssid) != 0) ++row;
        if (row == s_wifi_row_count) s_wifi_rows[s_wifi_row_count++] = networks[i];
        else if (networks[i].rssi > s_wifi_rows[row].rssi) s_wifi_rows[row] = networks[i];
    }
    std::sort(s_wifi_rows, s_wifi_rows + s_wifi_row_count, [&connected_ssid](const IWifi::Network &a, const IWifi::Network &b) {
        const bool ac = strcmp(a.ssid, connected_ssid) == 0;
        const bool bc = strcmp(b.ssid, connected_ssid) == 0;
        if (ac != bc) return ac;
        return a.rssi > b.rssi;
    });
    if (s_wifi_page_status != nullptr) {
        if (!hal().wifi->enabled()) lv_label_set_text(s_wifi_page_status, "WiFi is off");
        else if (s_wifi_connecting[0] != 0) lv_label_set_text_fmt(s_wifi_page_status, "Connecting to %.32s...", s_wifi_connecting);
        else lv_label_set_text(s_wifi_page_status, s_wifi_row_count == 0 ? "No networks found. Tap refresh to try again." : "Choose a network");
    }
    for (size_t i = 0; i < s_wifi_row_count; ++i) {
        const auto &network = s_wifi_rows[i];
        const bool connected = strcmp(network.ssid, connected_ssid) == 0;
        const bool attempting = strcmp(network.ssid, s_wifi_connecting) == 0;
        auto *button = lv_btn_create(s_wifi_page_list);
        settings_button_feedback(button);
        lv_obj_set_size(button, LV_PCT(100), kWifiNetworkRowHeight);
        lv_obj_set_style_pad_all(button, 10, 0);
        // Keep the row full width; inset its signal, name and status instead.
        lv_obj_set_style_pad_hor(button, kWifiNetworkRowInset, 0);
        lv_obj_set_style_radius(button, 10, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(connected || attempting ? 0x173254 : style.elevated), 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(style.accent), LV_STATE_PRESSED);
        auto *bars = lv_obj_create(button);
        lv_obj_remove_style_all(bars);
        lv_obj_set_size(bars, 30, 28);
        lv_obj_align(bars, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_clear_flag(bars, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        const int strength = network.rssi >= -55 ? 3 : network.rssi >= -70 ? 2 : 1;
        for (int bar = 0; bar < 3; ++bar) {
            auto *segment = lv_obj_create(bars);
            lv_obj_remove_style_all(segment);
            lv_obj_set_size(segment, 6, 8 + bar * 8);
            lv_obj_set_pos(segment, bar * 10, 20 - bar * 8);
            lv_obj_set_style_radius(segment, 2, 0);
            lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(segment, lv_color_hex(bar < strength ? style.text : 0x475569), 0);
            lv_obj_clear_flag(segment, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        }
        auto *name = lv_label_create(button);
        lv_obj_set_style_text_font(name, kWifiBodyFont, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(style.text), 0);
        lv_obj_set_width(name, LV_PCT(44));
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_label_set_text(name, network.ssid);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 44, 0);
        auto *detail = lv_label_create(button);
        lv_obj_set_style_text_font(detail, kWifiBodyFont, 0);
        lv_obj_set_style_text_color(detail, lv_color_hex(connected ? style.success : style.secondary), 0);
        lv_label_set_text_fmt(detail, "%s%s%s", connected ? LV_SYMBOL_OK " " : "", attempting ? "Joining" : network.secured ? "Secured" : "Open", !attempting ? LV_SYMBOL_RIGHT : "");
        lv_obj_align(detail, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(button, [](lv_event_t *event) {
            const auto index = reinterpret_cast<size_t>(lv_event_get_user_data(event));
            if (index >= s_wifi_row_count || hal().wifi == nullptr) return;
            const auto network = s_wifi_rows[index];
            if (hal().wifi->connected() && strcmp(network.ssid, hal().wifi->last_ssid()) == 0) wifi_open_forget_confirm(network.ssid);
            else if (network.secured) wifi_open_credentials(network.ssid);
            else hal().wifi->connect(network.ssid, "");
        }, LV_EVENT_CLICKED, reinterpret_cast<void *>(i));
    }
    // Commit the replacement list as a single page redraw, including the old
    // scroll position and spinner area. Do not flush intermediate empty rows.
    lv_obj_update_layout(s_wifi_page);
    lv_obj_invalidate(s_wifi_page);
    ESP_LOGI(TAG, "WiFi list ready: %u networks, %u ms UI work",
             static_cast<unsigned>(s_wifi_row_count), static_cast<unsigned>(lv_tick_elaps(render_started)));
}

void crystal_shell_wifi_event(uint8_t event)
{
    if (event == UI_EVT_WIFI_GOT_IP) {
        s_wifi_connecting[0] = '\0';
        refresh_connection_details();
        if (s_network_wifi_row != nullptr && hal().wifi != nullptr) {
            settings_row_set_summary(s_network_wifi_row, hal().wifi->last_ssid());
        }
        if (s_network_details_row != nullptr) settings_row_set_summary(s_network_details_row, "Connected");
        if (s_root_network_row != nullptr && hal().wifi != nullptr) {
            settings_row_set_summary(s_root_network_row, hal().wifi->last_ssid());
        }
        if (s_quick_wifi != nullptr) lv_obj_add_state(s_quick_wifi, LV_STATE_CHECKED);
        wifi_page_close();
        if (s_quick_wifi == nullptr) return;
        char text[64]; wifi_tile_text(text, sizeof(text)); quick_tile_set_text(s_quick_wifi, text);
    } else if (event == UI_EVT_WIFI_CONNECTING) {
        if (s_quick_wifi != nullptr) lv_obj_add_state(s_quick_wifi, LV_STATE_CHECKED);
        const char *ssid = hal().wifi != nullptr ? hal().wifi->last_ssid() : "";
        strlcpy(s_wifi_connecting, ssid, sizeof(s_wifi_connecting));
        refresh_connection_details();
        if (s_network_wifi_row != nullptr) settings_row_set_summary(s_network_wifi_row, "Connecting...");
        if (s_network_details_row != nullptr) settings_row_set_summary(s_network_details_row, "Connecting...");
        if (s_root_network_row != nullptr) settings_row_set_summary(s_root_network_row, "Connecting...");
        if (s_quick_wifi != nullptr) { char text[64]; wifi_tile_text(text, sizeof(text)); quick_tile_set_text(s_quick_wifi, text); }
        if (s_wifi_page != nullptr && s_wifi_page_status != nullptr) { char text[64]; snprintf(text, sizeof(text), "Connecting to %.32s...", ssid); lv_label_set_text(s_wifi_page_status, text); }
        wifi_page_fill_list();
    } else if (event == UI_EVT_WIFI_DISCONNECTED) {
        s_wifi_connecting[0] = '\0';
        refresh_connection_details();
        if (s_network_wifi_row != nullptr && hal().wifi != nullptr) {
            settings_row_set_summary(s_network_wifi_row, hal().wifi->enabled() ? "On - Not connected" : "Off");
        }
        if (s_network_details_row != nullptr) settings_row_set_summary(s_network_details_row, "Not connected");
        if (s_root_network_row != nullptr && hal().wifi != nullptr) {
            settings_row_set_summary(s_root_network_row, hal().wifi->enabled() ? "WiFi On - Not connected" : "WiFi Off");
        }
        wifi_page_fill_list();
        if (s_quick_wifi == nullptr) return;
        if (hal().wifi != nullptr && hal().wifi->enabled()) lv_obj_add_state(s_quick_wifi, LV_STATE_CHECKED);
        else lv_obj_clear_state(s_quick_wifi, LV_STATE_CHECKED);
        char text[64]; wifi_tile_text(text, sizeof(text)); quick_tile_set_text(s_quick_wifi, text);
    } else if (event == UI_EVT_WIFI_CONNECT_FAILED) {
        s_wifi_connecting[0] = '\0';
        refresh_connection_details();
        if (s_network_wifi_row != nullptr && hal().wifi != nullptr) {
            settings_row_set_summary(s_network_wifi_row, hal().wifi->enabled() ? "On - Not connected" : "Off");
        }
        if (s_network_details_row != nullptr) settings_row_set_summary(s_network_details_row, "Not connected");
        if (s_root_network_row != nullptr && hal().wifi != nullptr) {
            settings_row_set_summary(s_root_network_row, hal().wifi->enabled() ? "WiFi On - Not connected" : "WiFi Off");
        }
        wifi_page_fill_list();
        if (s_wifi_page != nullptr && s_wifi_page_status != nullptr) lv_label_set_text(s_wifi_page_status, "Failed to connect");
        if (s_quick_wifi != nullptr) { char text[64]; wifi_tile_text(text, sizeof(text)); quick_tile_set_text(s_quick_wifi, text); }
    } else if (event == UI_EVT_WIFI_SCAN_DONE) {
        wifi_scan_busy(false);
        wifi_page_fill_list();
    }
    if (crystal_network_active_type() == CrystalNetworkType::Ethernet)
        settings_row_set_summary(s_root_network_row, "Ethernet connected");
}

void crystal_shell_network_event()
{
    const auto active = crystal_network_active_type();
    if (s_network_active_row) settings_row_set_summary(s_network_active_row, crystal_network_type_name(active));
    if (s_network_ethernet_row) settings_row_set_summary(s_network_ethernet_row, ethernet_status(crystal_ethernet_info()));
    if (active == CrystalNetworkType::Ethernet) settings_row_set_summary(s_root_network_row, "Ethernet connected");
    else if (hal().wifi && hal().wifi->has_ip()) settings_row_set_summary(s_root_network_row, hal().wifi->last_ssid());
    else settings_row_set_summary(s_root_network_row, "No active connection");
    ethernet_details_refresh(nullptr);
}

void crystal_shell_weather_event(const CrystalWeatherReading *reading)
{
    if (reading == nullptr) return;
    for (size_t i = 0; i < crystal_registry_installed_count(); ++i) {
        if (strcmp(crystal_registry_installed_id(i), "weather") == 0) {
            auto *weather = static_cast<WeatherApp *>(crystal_registry_installed_app(i));
            if (weather != nullptr) weather->update(*reading);
            break;
        }
    }
}

// A paused app is not destroyed and a covered page is not deleted, so neither
// fires DELETE and watched_object_deleted never runs. The outgoing layer's field
// has to be released here instead.
void crystal_shell_front_layer_changed()
{
    crystal_keyboard_hide();
}

CrystalGestureOwner crystal_shell_gesture_owner() { return s_gesture_owner; }
void crystal_shell_set_quick_settings_open(bool open) { s_quick_settings_open = open; }
void crystal_shell_set_keyboard_open(bool open)
{
    s_keyboard_open = open;
    update_home_pill();
}
void crystal_shell_set_modal_open(bool open) { s_modal_open = open; }

void crystal_shell_ota_event()
{
    crystal_update_snapshot_t snapshot = {};
    crystal_update_snapshot(&snapshot);
    // Queued notifications read the latest snapshot, so multiple notifications
    // can observe the same percentage. Avoid duplicate logs and redraws.
    static int last_logged_state = -1;
    static int last_logged_progress = -1;
    const bool state_changed = static_cast<int>(snapshot.state) != last_logged_state;
    const bool progress_changed = static_cast<int>(snapshot.progress_percent) != last_logged_progress;

    // Early return for duplicate DOWNLOADING progress events
    if (snapshot.state == UPDATE_STATE_DOWNLOADING && !state_changed && !progress_changed) return;

    if (state_changed) {
        ESP_LOGI(TAG, "OTA notification state=%d", snapshot.state);
        last_logged_state = snapshot.state;
    }
    if (snapshot.state == UPDATE_STATE_IDLE) {
        update_dialog_close();
        if (s_ota_modal != nullptr) lv_obj_add_flag(s_ota_modal, LV_OBJ_FLAG_HIDDEN);
        crystal_shell_set_modal_open(false);
        return;
    }
    if (snapshot.state == UPDATE_STATE_AVAILABLE) {
        update_dialog_build(snapshot);
        if (s_ota_modal != nullptr) lv_obj_add_flag(s_ota_modal, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (snapshot.state == UPDATE_STATE_CHECKING || snapshot.state == UPDATE_STATE_CURRENT) {
        update_dialog_close();
        if (s_ota_modal != nullptr) lv_obj_add_flag(s_ota_modal, LV_OBJ_FLAG_HIDDEN);
        crystal_shell_set_modal_open(false);
        return;
    }
    if (snapshot.state == UPDATE_STATE_FAILED) {
        update_dialog_close();
        if (s_ota_modal != nullptr) lv_obj_add_flag(s_ota_modal, LV_OBJ_FLAG_HIDDEN);
        crystal_shell_set_modal_open(false);
        update_failure_dialog_build(snapshot.error_message);
        return;
    }

    // DOWNLOADING or INSTALLING state - show modal
    update_dialog_close();
    ota_modal_update(snapshot);
    if (state_changed || progress_changed) {
        ESP_LOGI(TAG, "OTA modal state=%d progress=%u", snapshot.state, snapshot.progress_percent);
        last_logged_progress = snapshot.progress_percent;
    }
}

void crystal_shell_update_failure_event(const char *message)
{
    update_failure_dialog_build(message);
}

void crystal_shell_update_success_event()
{
    update_result_dialog_build("Update Successful", "Crystal OS has been updated successfully.");
}
