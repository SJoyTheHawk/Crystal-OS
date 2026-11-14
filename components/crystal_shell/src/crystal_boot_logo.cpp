/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "crystal_boot_logo.hpp"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32P4
#if LV_COLOR_DEPTH != 16 || LV_COLOR_16_SWAP
#error "Crystal boot logo requires the existing RGB565 display format"
#endif

extern const uint8_t crystal_boot_logo_data[] asm("_binary_crystal_boot_logo_rgb565a8_start");

const lv_img_dsc_t crystal_boot_logo = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
        .always_zero = 0,
        .reserved = 0,
        .w = 256,
        .h = 256,
    },
    .data_size = 256 * 256 * LV_IMG_PX_SIZE_ALPHA_BYTE,
    .data = crystal_boot_logo_data,
};
#endif
