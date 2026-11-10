/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
// Draw the real apps through the production Brookesia lifecycle. Capture LVGL's
// display flush output so status/system layers are included in each image.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include "clock_app.hpp"
#include "weather_app.hpp"
#include "calculator_app.hpp"
#include "crystal_hal.hpp"

extern "C" time_t crystal_capture_time(time_t *);
extern "C" bool crystal_capture_quick_settings(ESP_Brookesia_Phone *phone);
static lv_color_t frame[480 * 480];
static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels)
{
    for (int y = area->y1; y <= area->y2; ++y) {
        for (int x = area->x1; x <= area->x2; ++x) {
            if (x >= 0 && x < 480 && y >= 0 && y < 480) frame[y * 480 + x] = *pixels;
            ++pixels;
        }
    }
    lv_disp_flush_ready(driver);
}
static void read_touch(lv_indev_drv_t *, lv_indev_data_t *data)
{
    data->state = LV_INDEV_STATE_RELEASED;
    data->point = {0, 0};
}
static void settle()
{
    for (int i = 0; i < 40; ++i) { lv_tick_inc(10); lv_timer_handler(); }
}
static void capture(lv_disp_t *display, const char *path)
{
    settle();
    lv_obj_invalidate(lv_scr_act());
    lv_obj_invalidate(lv_layer_top());
    lv_obj_invalidate(lv_layer_sys());
    lv_refr_now(display);
    FILE *file = std::fopen(path, "wb");
    assert(file);
    std::fprintf(file, "P6\n480 480\n255\n");
    for (lv_color_t pixel : frame) {
        lv_color32_t rgb;
        rgb.full = lv_color_to32(pixel);
        const unsigned char bytes[] = {rgb.ch.red, rgb.ch.green, rgb.ch.blue};
        assert(std::fwrite(bytes, 1, 3, file) == 3);
    }
    assert(std::fclose(file) == 0);
    std::printf("Captured %s\n", path);
}
static lv_obj_t *find_class(lv_obj_t *root, const lv_obj_class_t *type)
{
    if (lv_obj_check_type(root, type)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) {
        if (auto *found = find_class(lv_obj_get_child(root, i), type)) return found;
    }
    return nullptr;
}
static bool has_label(lv_obj_t *root, const char *text)
{
    if (lv_obj_check_type(root, &lv_label_class) &&
        std::strcmp(lv_label_get_text(root), text) == 0) return true;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) {
        if (has_label(lv_obj_get_child(root, i), text)) return true;
    }
    return false;
}
int main()
{
    std::setbuf(stdout, nullptr);
    setenv("TZ", "HKT-8", 1);
    tzset();
    crystal_hal_init();
    lv_init();
    static lv_color_t buffer[480 * 40];
    static lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw, buffer, nullptr, 480 * 40);
    static lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = driver.ver_res = 480;
    driver.draw_buf = &draw;
    driver.flush_cb = flush;
    lv_disp_t *display = lv_disp_drv_register(&driver);
    static lv_indev_drv_t input;
    lv_indev_drv_init(&input);
    input.type = LV_INDEV_TYPE_POINTER;
    input.read_cb = read_touch;
    auto *pointer = lv_indev_drv_register(&input);

    ESP_Brookesia_Phone phone(display);
    ClockApp clock;
    WeatherApp weather;
    CalculatorApp calculator;
    ESP_Brookesia_PhoneStylesheet_t sheet = ESP_BROOKESIA_PHONE_480_480_DARK_STYLESHEET();
    sheet.core.manager.app.max_running_num = 1;
    sheet.home.flags.enable_recents_screen = 0;
    sheet.manager.gesture_mask_indicator_trigger_time_ms = UINT32_MAX;
    sheet.manager.gesture.flags.enable_indicator_bars[ESP_BROOKESIA_GESTURE_INDICATOR_BAR_TYPE_BOTTOM] = 0;
    assert(phone.setTouchDevice(pointer));
    assert(phone.addStylesheet(sheet));
    assert(phone.activateStylesheet(sheet));
    assert(phone.begin());
    assert(phone.installApp(clock) >= 0);
    assert(phone.installApp(weather) >= 0);
    assert(phone.installApp(calculator) >= 0);
    auto *bar = phone.getHome().getStatusBar();
    assert(bar);
    assert(bar->setClockFormat(ESP_Brookesia_StatusBar::ClockFormat::FORMAT_24H));
    assert(bar->setClock(10, 9, false));
    assert(bar->setBatteryPercent(false, 85));
    assert(bar->setWifiIconState(ESP_Brookesia_StatusBar::WifiState::SIGNAL_3));
    auto start = [&](CrystalApp &app) {
        ESP_Brookesia_CoreAppEventData_t event{};
        event.id = app.getId();
        event.type = ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START;
        assert(phone.sendAppEvent(&event));
        settle();
        assert(phone.getManager().getActiveApp() == &app);
    };
    start(clock);
    assert(has_label(lv_scr_act(), "10:09:00"));
    capture(display, "clock.ppm");

    start(weather);
    CrystalWeatherReading reading{};
    reading.temperature_c10 = 246;
    reading.humidity = 68;
    reading.weather_code = 2;
    reading.wind_kmh10 = 123;
    reading.fetched_at = static_cast<int32_t>(crystal_capture_time(nullptr));
    reading.success = true;
    std::strcpy(reading.city, "Hong Kong");
    weather.update(reading);
    assert(has_label(lv_scr_act(), "24.6"));
    capture(display, "weather.ppm");

    start(calculator);
    auto *keyboard = find_class(lv_scr_act(), &lv_btnmatrix_class);
    assert(keyboard);
    // Enter 128 x 3 + 64 through the production button event handler.
    for (uint16_t id : {12, 13, 5, 2, 14, 11, 10, 8}) {
        lv_btnmatrix_set_selected_btn(keyboard, id);
        lv_event_send(keyboard, LV_EVENT_VALUE_CHANGED, nullptr);
    }
    assert(has_label(lv_scr_act(), "= 448"));
    capture(display, "calculator.ppm");

    assert(phone.sendNavigateEvent(ESP_BROOKESIA_CORE_NAVIGATE_TYPE_HOME));
    settle();
    capture(display, "launcher.ppm");
    hal().wifi->start();
    assert(crystal_capture_quick_settings(&phone));
    capture(display, "quick_settings.ppm");
    assert(phone.sendNavigateEvent(ESP_BROOKESIA_CORE_NAVIGATE_TYPE_HOME));
    settle();
    assert(phone.uninstallApp(clock));
    assert(phone.uninstallApp(weather));
    assert(phone.uninstallApp(calculator));
    assert(phone.del());
    std::puts("PASS: production app, launcher and Quick Settings captures");
}
