#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lv_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_display_t *bsp_display_start(void);
lv_indev_t *bsp_display_get_input_dev(void);
esp_err_t bsp_display_brightness_set(int brightness_percent);
bool bsp_display_lock(int32_t timeout_ms);
void bsp_display_unlock(void);

#ifdef __cplusplus
}
#endif
