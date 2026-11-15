#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_err.h"

#define BSP_AUDIO_SAMPLE_RATE_HZ 22050U

#ifdef __cplusplus
extern "C" {
#endif

i2c_master_bus_handle_t bsp_i2c_get_handle(void);
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void);
esp_err_t bsp_audio_poweramp_enable(bool enable);

#ifdef __cplusplus
}
#endif
