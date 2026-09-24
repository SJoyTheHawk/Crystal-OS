#include "lvgl.h"

// Color palette
#define ICON_BUS_RED       0xE31E24

// Icon canvas (64x64)
static lv_color_t icon_buf[64 * 64];

const lv_img_dsc_t bus_icon = {
    .header = {
        .always_zero = 0,
        .w = 64,
        .h = 64,
        .cf = LV_IMG_CF_TRUE_COLOR,
    },
    .data_size = sizeof(icon_buf),
    .data = (const uint8_t*)icon_buf,
};

void bus_icon_prepare(void)
{
    // Simple solid icon for now - fill with KMB red
    lv_color_t red = lv_color_hex(ICON_BUS_RED);
    for (int i = 0; i < 64 * 64; i++) {
        icon_buf[i] = red;
    }
}
