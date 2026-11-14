/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * Auto-generated peripheral configuration file
 * DO NOT MODIFY THIS FILE MANUALLY
 *
 * See LICENSE file for details.
 */

#include <stdbool.h>
#include <stdlib.h>
#include "esp_board_periph.h"
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "hal/gpio_types.h"
#include "periph_gpio.h"
#include "periph_i2s.h"
#include "periph_ledc.h"

// Peripheral configuration structures
const static i2c_master_bus_config_t esp_bmgr_i2c_master_cfg = {
    .i2c_port = I2C_NUM_1,
    .sda_io_num = 7,
    .scl_io_num = 8,
    .glitch_ignore_cnt = 7,
    .intr_priority = 1,
    .trans_queue_depth = 0,
    .flags = {
        .enable_internal_pullup = true,
    },
    .clk_source = I2C_CLK_SRC_DEFAULT,
};

const static periph_i2s_config_t esp_bmgr_i2s_audio_out_cfg = {
    .port = I2S_NUM_1,
    .role = I2S_ROLE_MASTER,
    .mode = I2S_COMM_MODE_STD,
    .direction = I2S_DIR_TX,
    .i2s_cfg = {
        .std = {
            .clk_cfg = {
                .sample_rate_hz = 24000,
                .clk_src = I2S_CLK_SRC_DEFAULT,
                .ext_clk_freq_hz = 0,
                .mclk_multiple = I2S_MCLK_MULTIPLE_256,
                .bclk_div = 8,
            },
            .slot_cfg = {
                .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                .slot_mode = I2S_SLOT_MODE_STEREO,
                .slot_mask = I2S_STD_SLOT_BOTH,
                .ws_width = 16,
                .ws_pol = false,
                .bit_shift = true,
                .left_align = true,
                .big_endian = false,
                .bit_order_lsb = false,
            },
            .gpio_cfg = {
                .mclk = 13,
                .bclk = 12,
                .ws = 10,
                .dout = 9,
                .din = 11,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false,
                },
            },
        },
    },
};

const static periph_i2s_config_t esp_bmgr_i2s_audio_in_cfg = {
    .port = I2S_NUM_1,
    .role = I2S_ROLE_MASTER,
    .mode = I2S_COMM_MODE_TDM,
    .direction = I2S_DIR_RX,
    .i2s_cfg = {
        .tdm = {
            .clk_cfg = {
                .sample_rate_hz = 24000,
                .clk_src = I2S_CLK_SRC_DEFAULT,
                .ext_clk_freq_hz = 0,
                .mclk_multiple = I2S_MCLK_MULTIPLE_256,
                .bclk_div = 8,
            },
            .slot_cfg = {
                .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                .slot_mode = I2S_SLOT_MODE_STEREO,
                .slot_mask = I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3,
                .ws_width = 16,
                .ws_pol = false,
                .bit_shift = true,
                .left_align = false,
                .big_endian = false,
                .bit_order_lsb = false,
                .skip_mask = false,
                .total_slot = 4,
            },
            .gpio_cfg = {
                .mclk = 13,
                .bclk = 12,
                .ws = 10,
                .dout = 9,
                .din = 11,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false,
                },
            },
        },
    },
};

const static periph_gpio_config_t esp_bmgr_gpio_pa_control_cfg = {
    .gpio_config = {
        .pin_bit_mask = BIT64(53),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    },
    .default_level = 0,
    .gpio_num = 53,
};

const static periph_gpio_config_t esp_bmgr_gpio_sd_power_cfg = {
    .gpio_config = {
        .pin_bit_mask = BIT64(45),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    },
    .default_level = 1,
    .gpio_num = 45,
};

const static esp_ldo_channel_config_t esp_bmgr_ldo_mipi_cfg = {
    .chan_id = 3,
    .voltage_mv = 2500,
    .flags = {
        .adjustable = 1,
        .owned_by_hw = 0,
    },
};

const static esp_lcd_dsi_bus_config_t esp_bmgr_dsi_display_cfg = {
    .bus_id = 0,
    .num_data_lanes = 2,
    .phy_clk_src = 0,
    .lane_bit_rate_mbps = 480,
    .flags = {
        .clock_lane_force_hs = false,
    },
};

const static periph_ledc_config_t esp_bmgr_ledc_backlight_cfg = {
    .handle = {
        .channel = LEDC_CHANNEL_1,
        .speed_mode = LEDC_LOW_SPEED_MODE,
    },
    .gpio_num = 26,
    .duty = 0,
    .freq_hz = 5000,
    .duty_resolution = LEDC_TIMER_10_BIT,
    .timer_sel = LEDC_TIMER_1,
    .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
    .output_invert = true,
};

// Peripheral descriptor array
const esp_board_periph_desc_t g_esp_board_peripherals[] = {
    {
        .next = &g_esp_board_peripherals[1],
        .name = "i2c_master",
        .type = "i2c",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_MASTER,
        .cfg = &esp_bmgr_i2c_master_cfg,
        .cfg_size = sizeof(esp_bmgr_i2c_master_cfg),
        .id = 0,
        .init_skip = false,
    },
    {
        .next = &g_esp_board_peripherals[2],
        .name = "i2s_audio_out",
        .type = "i2s",
        .format = "std-out",
        .role = ESP_BOARD_PERIPH_ROLE_MASTER,
        .cfg = &esp_bmgr_i2s_audio_out_cfg,
        .cfg_size = sizeof(esp_bmgr_i2s_audio_out_cfg),
        .id = 0,
        .init_skip = false,
    },
    {
        .next = &g_esp_board_peripherals[3],
        .name = "i2s_audio_in",
        .type = "i2s",
        .format = "tdm-in",
        .role = ESP_BOARD_PERIPH_ROLE_MASTER,
        .cfg = &esp_bmgr_i2s_audio_in_cfg,
        .cfg_size = sizeof(esp_bmgr_i2s_audio_in_cfg),
        .id = 0,
        .init_skip = false,
    },
    {
        .next = &g_esp_board_peripherals[4],
        .name = "gpio_pa_control",
        .type = "gpio",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_IO,
        .cfg = &esp_bmgr_gpio_pa_control_cfg,
        .cfg_size = sizeof(esp_bmgr_gpio_pa_control_cfg),
        .id = 0,
        .init_skip = false,
    },
    {
        .next = &g_esp_board_peripherals[5],
        .name = "gpio_sd_power",
        .type = "gpio",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_IO,
        .cfg = &esp_bmgr_gpio_sd_power_cfg,
        .cfg_size = sizeof(esp_bmgr_gpio_sd_power_cfg),
        .id = 0,
        .init_skip = false,
    },
    {
        .next = &g_esp_board_peripherals[6],
        .name = "ldo_mipi",
        .type = "ldo",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_NONE,
        .cfg = &esp_bmgr_ldo_mipi_cfg,
        .cfg_size = sizeof(esp_bmgr_ldo_mipi_cfg),
        .id = 0,
        .init_skip = false,
    },
    {
        .next = &g_esp_board_peripherals[7],
        .name = "dsi_display",
        .type = "dsi",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_NONE,
        .cfg = &esp_bmgr_dsi_display_cfg,
        .cfg_size = sizeof(esp_bmgr_dsi_display_cfg),
        .id = 0,
        .init_skip = false,
    },
    {
        .next = NULL,
        .name = "ledc_backlight",
        .type = "ledc",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_NONE,
        .cfg = &esp_bmgr_ledc_backlight_cfg,
        .cfg_size = sizeof(esp_bmgr_ledc_backlight_cfg),
        .id = 0,
        .init_skip = false,
    },
};
