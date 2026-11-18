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
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/ledc.h"
#include "dev_audio_codec.h"
#include "periph_i2s.h"
#include "hal/axi_icm_ll.h"

static const char *TAG = "crystal_board_p4";
static const uint32_t kBacklightMinDuty = 480;
static const uint32_t kBacklightMaxDuty = 1023;
static const uint32_t kBrightnessScaleMax = 100;
static dev_display_lcd_handles_t *s_display_handles;
static dev_lcd_touch_handles_t *s_touch_handles;
static periph_ledc_handle_t *s_brightness_handle;
static i2c_master_bus_handle_t s_i2c_bus;
static i2s_chan_handle_t s_audio_tx;
static i2s_chan_handle_t s_audio_rx;
static dev_audio_codec_handles_t *s_speaker_handles;
static lv_display_t *s_display;
static lv_indev_t *s_input;

// Waveshare ESP32-P4-WIFI6-Touch-LCD-4B board wiring, as declared by its
// board_devices.yaml and board_peripherals.yaml.
#define BSP_AUDIO_TX_PERIPHERAL "i2s_audio_out"
#define BSP_AUDIO_RX_PERIPHERAL "i2s_audio_in"
#define BSP_AUDIO_DAC_DEVICE "audio_dac"
#define BSP_POWER_AMP_IO GPIO_NUM_53

static esp_err_t ensure_audio_bus(void)
{
    if (s_audio_tx != NULL && s_audio_rx != NULL) {
        return ESP_OK;
    }
    if (s_audio_tx != NULL || s_audio_rx != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Match Waveshare bsp_audio_init(NULL): 22050 Hz, 16-bit mono STD on both
    // directions. The board metadata's normal voice profile is 24 kHz TX plus
    // 24 kHz TDM RX, which would leave the shared I2S port at different clock
    // settings from Crystal's 22050 Hz alarm stream.
    periph_i2s_config_t *board_tx_config = NULL;
    esp_err_t err = esp_board_periph_get_config(BSP_AUDIO_TX_PERIPHERAL,
                                                (void **)&board_tx_config);
    if (err != ESP_OK || board_tx_config == NULL) {
        ESP_LOGE(TAG, "Audio I2S TX configuration unavailable: %s", esp_err_to_name(err));
        return err != ESP_OK ? err : ESP_ERR_INVALID_STATE;
    }
    periph_i2s_config_t tx_config = *board_tx_config;
    tx_config.role = I2S_ROLE_MASTER;
    tx_config.mode = I2S_COMM_MODE_STD;
    tx_config.direction = I2S_DIR_TX;
    tx_config.i2s_cfg.std.clk_cfg.sample_rate_hz = BSP_AUDIO_SAMPLE_RATE_HZ;
    const i2s_std_slot_config_t mono_slots = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    tx_config.i2s_cfg.std.slot_cfg = mono_slots;
    periph_i2s_config_t rx_config = tx_config;
    rx_config.direction = I2S_DIR_RX;
    err = esp_board_periph_override_config(BSP_AUDIO_TX_PERIPHERAL, &tx_config,
                                           sizeof(tx_config));
    if (err == ESP_OK) {
        err = esp_board_periph_override_config(BSP_AUDIO_RX_PERIPHERAL, &rx_config,
                                               sizeof(rx_config));
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Audio I2S configuration failed: %s", esp_err_to_name(err));
        return err;
    }

    // Create TX before RX, matching the Waveshare BSP's required order.
    err = esp_board_periph_ref_handle(BSP_AUDIO_TX_PERIPHERAL,
                                      (void **)&s_audio_tx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Audio I2S TX init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_board_periph_ref_handle(BSP_AUDIO_RX_PERIPHERAL,
                                      (void **)&s_audio_rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Audio I2S RX init failed: %s", esp_err_to_name(err));
        (void)esp_board_periph_unref_handle(BSP_AUDIO_TX_PERIPHERAL);
        s_audio_tx = NULL;
        return err;
    }
    return ESP_OK;
}

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

    // IDF's DPI scanout uses DW-GDMA master 1 to read PSRAM (master 0
    // writes the DSI bridge). Keep those deadline-sensitive reads above the
    // reset priority of CPU/cache/DMA2D traffic. Otherwise heavy UI updates
    // can starve scanout; the DPI driver reports an underrun/blue frame.
    // Preserve write priority and any higher read priority already installed.
    const uint32_t scanout_read_qos = AXI_ICM.mst_arqos_reg0.reg_gdma_mst2_arqos;
    if (scanout_read_qos < 1) {
        axi_icm_ll_set_dw_gdma_qos_arbiter_prio(1,
            AXI_ICM.mst_awqos_reg0.reg_gdma_mst2_awqos, 1);
    }
    ESP_LOGI(TAG, "Display PSRAM read priority: %u",
             (unsigned)AXI_ICM.mst_arqos_reg0.reg_gdma_mst2_arqos);

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
    } else if (brightness_percent > (int)kBrightnessScaleMax) {
        brightness_percent = (int)kBrightnessScaleMax;
    }
    esp_err_t err = ensure_brightness_device();
    if (err != ESP_OK) {
        return err;
    }
    // The panel does not emit visible light below duty 480. Preserve zero as
    // the explicit off state and stretch the usable range across 1..100.
    const uint32_t duty = brightness_percent == 0
                              ? 0
                              : kBacklightMinDuty +
                                    ((kBacklightMaxDuty - kBacklightMinDuty) *
                                     (uint32_t)(brightness_percent - 1)) /
                                        (kBrightnessScaleMax - 1);
    ESP_LOGI(TAG, "Backlight PWM: %d/100 -> duty %u/1023", brightness_percent,
             (unsigned)duty);
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
    if (s_speaker_handles != NULL && s_speaker_handles->codec_dev != NULL) {
        return s_speaker_handles->codec_dev;
    }

    esp_err_t err = ensure_audio_bus();
    if (err != ESP_OK) {
        return NULL;
    }
    err = esp_board_manager_init_device_by_name(BSP_AUDIO_DAC_DEVICE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Audio DAC initialization failed: %s", esp_err_to_name(err));
        return NULL;
    }
    err = esp_board_manager_get_device_handle(BSP_AUDIO_DAC_DEVICE,
                                              (void **)&s_speaker_handles);
    if (err != ESP_OK || s_speaker_handles == NULL ||
            s_speaker_handles->codec_dev == NULL) {
        ESP_LOGE(TAG, "Audio DAC handle unavailable: %s", esp_err_to_name(err));
        (void)esp_board_manager_deinit_device_by_name(BSP_AUDIO_DAC_DEVICE);
        s_speaker_handles = NULL;
        return NULL;
    }
    return s_speaker_handles->codec_dev;
}

esp_err_t bsp_audio_poweramp_enable(bool enable)
{
    if (s_speaker_handles == NULL || s_speaker_handles->codec_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return gpio_set_level(BSP_POWER_AMP_IO, enable ? 1 : 0);
}
