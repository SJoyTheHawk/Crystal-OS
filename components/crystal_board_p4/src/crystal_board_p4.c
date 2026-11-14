#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"

#include <string.h>

#include "dev_display_lcd.h"
#include "dev_lcd_touch.h"
#include "dev_ledc_ctrl.h"
#include "esp_board_manager.h"
#include "esp_board_periph.h"
#include "esp_lv_adapter.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "driver/ledc.h"

static const char *TAG = "crystal_board_p4";
static dev_display_lcd_handles_t *s_display_handles;
static dev_lcd_touch_handles_t *s_touch_handles;
static periph_ledc_handle_t *s_brightness_handle;
static i2c_master_bus_handle_t s_i2c_bus;
static lv_display_t *s_display;
static lv_indev_t *s_input;

static esp_err_t ensure_brightness_device(void)
{
    if (s_brightness_handle != NULL) {
        return ESP_OK;
    }

    esp_err_t err = esp_board_manager_init_device_by_name("lcd_brightness");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Brightness device init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_board_manager_get_device_handle("lcd_brightness", (void **)&s_brightness_handle);
    if (err == ESP_OK && s_brightness_handle == NULL) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Brightness handle unavailable: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t bsp_spiffs_mount(void)
{
    const esp_vfs_spiffs_conf_t config = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = false,
    };
    esp_err_t err = esp_vfs_spiffs_register(&config);
    if (err == ESP_OK) {
        size_t total_bytes = 0;
        size_t used_bytes = 0;
        err = esp_spiffs_info(config.partition_label, &total_bytes, &used_bytes);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Storage mounted: %u/%u bytes used",
                     (unsigned)used_bytes, (unsigned)total_bytes);
        }
    }
    return err;
}

lv_display_t *bsp_display_start(void)
{
    if (s_display != NULL) {
        return s_display;
    }

    const esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    esp_err_t err = esp_lv_adapter_init(&adapter_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL adapter init failed: %s", esp_err_to_name(err));
        return NULL;
    }
    if (ensure_brightness_device() != ESP_OK) {
        return NULL;
    }

    err = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4 display device init failed: %s", esp_err_to_name(err));
        return NULL;
    }
    err = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD,
                                              (void **)&s_display_handles);
    if (err != ESP_OK || s_display_handles == NULL || s_display_handles->panel_handle == NULL) {
        ESP_LOGE(TAG, "P4 display handles unavailable: %s", esp_err_to_name(err));
        return NULL;
    }

    dev_display_lcd_config_t *lcd_config = NULL;
    err = esp_board_manager_get_device_config(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, (void **)&lcd_config);
    if (err != ESP_OK || lcd_config == NULL ||
            strcmp(lcd_config->sub_type, ESP_BOARD_DEVICE_LCD_SUB_TYPE_DSI) != 0) {
        ESP_LOGE(TAG, "P4 display configuration unavailable or not MIPI-DSI");
        return NULL;
    }

    const esp_lv_adapter_display_config_t display_config = {
        .panel = s_display_handles->panel_handle,
        .panel_io = s_display_handles->io_handle,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_MIPI_DSI,
            .rotation = ESP_LV_ADAPTER_ROTATE_0,
            .hor_res = lcd_config->lcd_width,
            .ver_res = lcd_config->lcd_height,
            .buffer_height = 50,
            .use_psram = false,
            .enable_ppa_accel = false,
            .require_double_buffer = false,
            .mono_layout = ESP_LV_ADAPTER_MONO_LAYOUT_NONE,
        },
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .te_sync = ESP_LV_ADAPTER_TE_SYNC_DISABLED(),
    };
    s_display = esp_lv_adapter_register_display(&display_config);
    if (s_display == NULL) {
        ESP_LOGE(TAG, "P4 display registration with LVGL failed");
        return NULL;
    }

    err = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_LCD_TOUCH);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4 touch device init failed: %s", esp_err_to_name(err));
        return NULL;
    }
    err = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_LCD_TOUCH, (void **)&s_touch_handles);
    if (err != ESP_OK || s_touch_handles == NULL || s_touch_handles->touch_handle == NULL) {
        ESP_LOGE(TAG, "P4 touch handle unavailable: %s", esp_err_to_name(err));
        return NULL;
    }
    const esp_lv_adapter_touch_config_t touch_config =
        ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(s_display, s_touch_handles->touch_handle);
    s_input = esp_lv_adapter_register_touch(&touch_config);
    if (s_input == NULL) {
        ESP_LOGE(TAG, "P4 touch registration with LVGL failed");
        return NULL;
    }

    if (s_i2c_bus == NULL) {
        err = esp_board_periph_ref_handle("i2c_master", (void **)&s_i2c_bus);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Board I2C handle unavailable: %s", esp_err_to_name(err));
        }
    }

    err = esp_lv_adapter_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL adapter start failed: %s", esp_err_to_name(err));
        return NULL;
    }
    ESP_LOGI(TAG, "P4 MIPI-DSI display and touch ready (%ux%u)",
             (unsigned)lcd_config->lcd_width, (unsigned)lcd_config->lcd_height);
    return s_display;
}

lv_indev_t *bsp_display_get_input_dev(void)
{
    return s_input;
}

esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    if (brightness_percent < 0) {
        brightness_percent = 0;
    } else if (brightness_percent > 100) {
        brightness_percent = 100;
    }
    esp_err_t err = ensure_brightness_device();
    if (err != ESP_OK) {
        return err;
    }
    const uint32_t duty = (1023U * (uint32_t)brightness_percent) / 100U;
    err = ledc_set_duty(s_brightness_handle->speed_mode, s_brightness_handle->channel, duty);
    if (err == ESP_OK) {
        err = ledc_update_duty(s_brightness_handle->speed_mode, s_brightness_handle->channel);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4 backlight update failed: %s", esp_err_to_name(err));
    }
    return err;
}

bool bsp_display_lock(int32_t timeout_ms)
{
    // Crystal and the S3 BSP use zero for an indefinite wait; the P4 adapter
    // uses a negative timeout for that contract.
    return esp_lv_adapter_lock(timeout_ms <= 0 ? -1 : timeout_ms) == ESP_OK;
}

void bsp_display_unlock(void)
{
    esp_lv_adapter_unlock();
}

i2c_master_bus_handle_t bsp_i2c_get_handle(void)
{
    if (s_i2c_bus == NULL &&
            esp_board_periph_ref_handle("i2c_master", (void **)&s_i2c_bus) != ESP_OK) {
        return NULL;
    }
    return s_i2c_bus;
}

esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void)
{
    return NULL;
}

esp_err_t bsp_audio_poweramp_enable(bool enable)
{
    (void)enable;
    return ESP_ERR_NOT_SUPPORTED;
}
