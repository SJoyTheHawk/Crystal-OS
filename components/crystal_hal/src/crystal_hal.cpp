/*
 * SPDX-License-Identifier: MIT
 */

#include "crystal_hal.hpp"

#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "bsp/display.h"
#include "bsp/esp32_s3_touch_lcd_4b.h"
#include "driver/i2c_master.h"
#include "lvgl.h"
#include "nvs.h"

namespace {

constexpr uint8_t kBrightnessMax = 95;
constexpr const char *kStorageNamespace = "crystal";
constexpr uint8_t kRtcAddress = 0x51;

static uint8_t bcd_to_bin(uint8_t value) { return static_cast<uint8_t>((value >> 4) * 10 + (value & 0x0f)); }
static uint8_t bin_to_bcd(uint8_t value) { return static_cast<uint8_t>((value / 10) << 4 | (value % 10)); }

class DeviceBrightness final : public IBrightness {
public:
    void set(uint8_t pct) override
    {
        if (pct > kBrightnessMax) {
            pct = kBrightnessMax;
        }
        (void)bsp_display_brightness_set(pct);
        pct_ = pct;
    }

    uint8_t get() const override
    {
        return pct_;
    }

private:
    uint8_t pct_ = kBrightnessMax;
};

class DeviceStorage final : public IStorage {
public:
    bool get(const char *key, void *value, size_t *length) override
    {
        if (key == nullptr || value == nullptr || length == nullptr) {
            return false;
        }

        nvs_handle_t handle;
        if (nvs_open(kStorageNamespace, NVS_READONLY, &handle) != ESP_OK) {
            return false;
        }
        const esp_err_t err = nvs_get_blob(handle, key, value, length);
        nvs_close(handle);
        return err == ESP_OK;
    }

    bool set(const char *key, const void *value, size_t length) override
    {
        if (key == nullptr || (value == nullptr && length != 0)) {
            return false;
        }

        nvs_handle_t handle;
        if (nvs_open(kStorageNamespace, NVS_READWRITE, &handle) != ESP_OK) {
            return false;
        }
        esp_err_t err = nvs_set_blob(handle, key, value, length);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
        return err == ESP_OK;
    }

    bool erase(const char *key) override
    {
        if (key == nullptr) {
            return false;
        }

        nvs_handle_t handle;
        if (nvs_open(kStorageNamespace, NVS_READWRITE, &handle) != ESP_OK) {
            return false;
        }
        esp_err_t err = nvs_erase_key(handle, key);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
        return err == ESP_OK;
    }
};

class FallbackRtc final : public IRtc {
public:
    bool read(struct tm *out) override
    {
        if (out == nullptr) {
            return false;
        }
        time_t now = ::time(nullptr);
        return localtime_r(&now, out) != nullptr;
    }

    bool write(const struct tm *in) override
    {
        if (in == nullptr) {
            return false;
        }
        struct tm copy = *in;
        const time_t epoch = mktime(&copy);
        if (epoch < 0) {
            return false;
        }
        struct timeval tv = {};
        tv.tv_sec = epoch;
        return settimeofday(&tv, nullptr) == 0;
    }
};

class Pcf85063Rtc final : public IRtc {
public:
    bool read(struct tm *out) override
    {
        if (out == nullptr || !ensure_device()) return false;
        uint8_t reg = 0x04, data[7] = {};
        if (i2c_master_transmit_receive(device_, &reg, 1, data, sizeof(data), 100) != ESP_OK) return false;
        if ((data[0] & 0x80) != 0) return false;
        out->tm_sec = bcd_to_bin(data[0] & 0x7f);
        out->tm_min = bcd_to_bin(data[1] & 0x7f);
        out->tm_hour = bcd_to_bin(data[2] & 0x3f);
        out->tm_mday = bcd_to_bin(data[3] & 0x3f);
        out->tm_wday = bcd_to_bin(data[4] & 0x07);
        out->tm_mon = bcd_to_bin(data[5] & 0x1f) - 1;
        out->tm_year = 100 + bcd_to_bin(data[6]);
        out->tm_isdst = -1;
        return out->tm_sec <= 59 && out->tm_min <= 59 && out->tm_hour <= 23 &&
               out->tm_mday >= 1 && out->tm_mday <= 31 && out->tm_mon >= 0 && out->tm_mon <= 11;
    }

    bool write(const struct tm *in) override
    {
        if (in == nullptr || !ensure_device()) return false;
        uint8_t data[8] = {0x04, bin_to_bcd(in->tm_sec), bin_to_bcd(in->tm_min), bin_to_bcd(in->tm_hour),
                           bin_to_bcd(in->tm_mday), bin_to_bcd(in->tm_wday), bin_to_bcd(in->tm_mon + 1),
                           bin_to_bcd(in->tm_year % 100)};
        return i2c_master_transmit(device_, data, sizeof(data), 100) == ESP_OK;
    }

private:
    bool ensure_device()
    {
        if (device_ != nullptr) return true;
        i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
        if (bus == nullptr) return false;
        i2c_device_config_t config = {};
        config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = kRtcAddress;
        config.scl_speed_hz = 400000;
        return i2c_master_bus_add_device(bus, &config, &device_) == ESP_OK;
    }
    i2c_master_dev_handle_t device_ = nullptr;
};

class DeviceTouch final : public ITouchRaw {
public:
    bool read(Point *out) override
    {
        if (out == nullptr || indev_ == nullptr) return false;
        lv_indev_data_t data = {};
        _lv_indev_read(indev_, &data);
        out->x = data.point.x;
        out->y = data.point.y;
        out->pressed = data.state == LV_INDEV_STATE_PRESSED;
        return true;
    }
    void bind(void *indev) { indev_ = static_cast<lv_indev_t *>(indev); }
private:
    lv_indev_t *indev_ = nullptr;
};

class DeviceWifi final : public IWifi {
public:
    void start() override
    {
        if (started_) {
            return;
        }

        const esp_err_t netif_err = esp_netif_init();
        if (netif_err != ESP_OK && netif_err != ESP_ERR_INVALID_STATE) {
            return;
        }
        const esp_err_t event_err = esp_event_loop_create_default();
        if (event_err != ESP_OK && event_err != ESP_ERR_INVALID_STATE) {
            return;
        }
        netif_ = esp_netif_create_default_wifi_sta();
        if (netif_ == nullptr) {
            return;
        }

        wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
        if (esp_wifi_init(&init_config) != ESP_OK ||
            esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
            esp_wifi_start() != ESP_OK) {
            return;
        }
        started_ = true;
        wifi_config_t saved_config = {};
        if (esp_wifi_get_config(WIFI_IF_STA, &saved_config) == ESP_OK && saved_config.sta.ssid[0] != 0) {
            (void)esp_wifi_connect();
        }
    }

    void scan() override
    {
        if (started_) {
            (void)esp_wifi_scan_start(nullptr, false);
        }
    }

    void connect(const char *ssid, const char *pass) override
    {
        if (!started_ || ssid == nullptr || pass == nullptr) {
            return;
        }

        wifi_config_t config = {};
        strlcpy(reinterpret_cast<char *>(config.sta.ssid), ssid, sizeof(config.sta.ssid));
        strlcpy(reinterpret_cast<char *>(config.sta.password), pass, sizeof(config.sta.password));
        if (esp_wifi_set_config(WIFI_IF_STA, &config) == ESP_OK) {
            (void)esp_wifi_connect();
        }
    }

    bool connected() const override
    {
        wifi_ap_record_t record = {};
        return started_ && esp_wifi_sta_get_ap_info(&record) == ESP_OK;
    }

private:
    esp_netif_t *netif_ = nullptr;
    bool started_ = false;
};

DeviceBrightness s_brightness;
DeviceStorage s_storage;
Pcf85063Rtc s_rtc;
DeviceWifi s_wifi;
DeviceTouch s_touch;
CrystalHal s_hal = {&s_brightness, &s_rtc, &s_wifi, &s_storage, &s_touch};

} // namespace

CrystalHal &hal()
{
    return s_hal;
}

void crystal_hal_init()
{
    // Keep the initial board-selected brightness; later settings may change it.
    s_brightness.set(s_brightness.get());
}

void crystal_hal_bind_touch(void *lvgl_input_device)
{
    s_touch.bind(lvgl_input_device);
}
