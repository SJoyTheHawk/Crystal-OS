/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "crystal_stylesheet.hpp"
#include "sdkconfig.h"

const CrystalVisualStyle &crystal_visual_style()
{
    // Match Settings, Quick Settings and Crystal's existing card shell.
    static const CrystalVisualStyle style = {
        0x11151b, 0x1b2028, 0x252a30, 0xf2f4f7, 0xcbd5e1, 0x94a3b8,
        0x3b82f6, 0x53c8b6, 0xf06a6a,
#if CONFIG_IDF_TARGET_ESP32P4
        300, 300, 240,
#else
        0, 0, 0,
#endif
    };
    return style;
}

ESP_Brookesia_PhoneStylesheet_t *crystal_stylesheet_create_dark()
{
    auto *stylesheet = new ESP_Brookesia_PhoneStylesheet_t ESP_BROOKESIA_PHONE_480_480_DARK_STYLESHEET();
    if (stylesheet == nullptr) return nullptr;
#if CONFIG_IDF_TARGET_ESP32P4
    // Brookesia has no square 720px preset; adapt Crystal's square dark style to
    // the P4 canvas while retaining its navigation and motion treatment.
    stylesheet->core.name = "Crystal OS 720x720 Dark";
    stylesheet->core.screen_size.width = 720;
    stylesheet->core.screen_size.height = 720;
    stylesheet->core.home.background.wallpaper_image_resource.resource =
        &esp_brookesia_image_large_wallpaper_dark_720_720;
    auto &launcher = stylesheet->home.app_launcher.data;
    stylesheet->home.status_bar.data.main.text_font.size_px = 24;
    launcher.table.size.height = 660;
    launcher.icon.main.size.width = 216;
    launcher.icon.main.size.height = 216;
    launcher.icon.image.default_size.width_percent = 63;
    launcher.icon.image.default_size.height_percent = 63;
    launcher.icon.image.default_size.flags.enable_width_percent = 1;
    launcher.icon.image.default_size.flags.enable_height_percent = 1;
    launcher.icon.image.default_size.flags.enable_square = 1;
    launcher.icon.image.press_size.width_percent = 56;
    launcher.icon.image.press_size.height_percent = 56;
    launcher.icon.image.press_size.flags.enable_width_percent = 1;
    launcher.icon.image.press_size.flags.enable_height_percent = 1;
    launcher.icon.image.press_size.flags.enable_square = 1;
    launcher.icon.label.text_font.size_px = 28;
    launcher.indicator.main_size.height = 120;
    launcher.indicator.main_layout_column_pad = 15;
    launcher.indicator.spot_inactive_size.width = 18;
    launcher.indicator.spot_inactive_size.height = 18;
    launcher.indicator.spot_active_size.width = 45;
    launcher.indicator.spot_active_size.height = 18;
#endif
    stylesheet->core.manager.app.max_running_num = 1;
    stylesheet->home.flags.enable_recents_screen = 0;
    stylesheet->home.app_launcher.data.flags.enable_table_scroll_anim = 1;
    stylesheet->manager.gesture.threshold.direction_horizon = 12;
    stylesheet->manager.gesture.threshold.direction_vertical = 12;
    stylesheet->manager.gesture.threshold.horizontal_edge = 24;
    stylesheet->manager.gesture_mask_indicator_trigger_time_ms = UINT32_MAX;
    // Kill Brookesia's bottom indicator bar so Crystal's home pill is the only one.
    // This flag is the only gate the manager does not re-assert: begin() calls
    // setIndicatorBarVisible(BOTTOM, true) once, and the MAIN branch of
    // processGestureScreenChange() re-shows it on every return to the launcher, so
    // hiding the object from the shell loses on the next navigation. With the flag
    // clear, setIndicatorBarVisible() and setIndicatorBarLength() return early and
    // the bar keeps the LV_OBJ_FLAG_HIDDEN it was created with. Gesture detection is
    // unaffected: the flag only gates the bar's visuals, not the touch handling.
    // LEFT and RIGHT already ship as 0, so this path is already exercised.
    stylesheet->manager.gesture.flags.enable_indicator_bars
        [ESP_BROOKESIA_GESTURE_INDICATOR_BAR_TYPE_BOTTOM] = 0;
    return stylesheet;
}
