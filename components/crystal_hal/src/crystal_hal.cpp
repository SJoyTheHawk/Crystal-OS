/*
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "crystal_hal.hpp"

#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "bsp/display.h"
#include "bsp/esp32_s3_touch_lcd_4b.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"

#include <math.h>

namespace {

constexpr uint8_t kBrightnessMax = 95;
constexpr int kVolumeMax = 100;
constexpr const char *kStorageNamespace = "crystal";
constexpr const char *kWifiEnabledKey = "wifi_enabled";
constexpr const char *kDhcpKey = "net.dhcp";
constexpr uint8_t kRtcAddress = 0x51;
constexpr uint8_t kAxp2101Address = 0x34;
constexpr uint32_t kAlarmSampleRate = 22050;
constexpr float kPi = 3.14159265358979323846f;
static const char *TAG = "crystal_hal";
constexpr size_t kWifiMaxNetworks = 20;
// The board's only free tactile button. Wired active-low; also the ESP32-S3
// strapping pin, which is why we only ever read it after boot.
constexpr gpio_num_t kResetButtonGpio = GPIO_NUM_0;
constexpr uint32_t kResetHoldMs = 5000;
constexpr uint32_t kResetPollMs = 100;
// This buffer is used by the esp_event task; keep it out of that task's stack.
wifi_ap_record_t s_wifi_records[kWifiMaxNetworks] = {};

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
            ESP_LOGE(TAG, "NVS set key=%s open failed", key);
            return false;
        }

        // Phase 11 edit begin: verify and log every shell/core NVS write.
        uint8_t old_value[32] = {};
        size_t old_length = sizeof(old_value);
        const esp_err_t old_err = nvs_get_blob(handle, key, old_value, &old_length);
        const bool old_present = old_err == ESP_OK;
        if (!old_present) old_length = 0;
        esp_err_t err = nvs_set_blob(handle, key, value, length);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        bool readback_ok = false;
        if (err == ESP_OK) {
            size_t verify_length = length;
            uint8_t verify_value[32] = {};
            void *verify_buffer = length <= sizeof(verify_value) ? verify_value : nullptr;
            if (verify_buffer != nullptr && nvs_get_blob(handle, key, verify_buffer, &verify_length) == ESP_OK) {
                readback_ok = verify_length == length && memcmp(verify_buffer, value, length) == 0;
            } else if (verify_buffer == nullptr) {
                // Large values are uncommon in shell settings. The successful
                // commit is still useful to report when an inline comparison
                // would require an allocation on the LVGL task.
                readback_ok = true;
            }
        }
        char old_hex[65] = {};
        char new_hex[65] = {};
        const size_t old_dump = old_length < 32 ? old_length : 32;
        const size_t new_dump = length < 32 ? length : 32;
        for (size_t i = 0; i < old_dump; ++i) snprintf(old_hex + i * 2, 3, "%02x", old_value[i]);
        for (size_t i = 0; i < new_dump; ++i) snprintf(new_hex + i * 2, 3, "%02x", static_cast<const uint8_t *>(value)[i]);
        ESP_LOGI(TAG, "NVS set key=%s old=%s requested=%s write=%s readback=%s",
                 key, old_present ? old_hex : "<unset>", new_hex,
                 err == ESP_OK ? "ok" : esp_err_to_name(err), readback_ok ? "ok" : "failed");
        nvs_close(handle);
        // Phase 11 edit end.
        return err == ESP_OK && readback_ok;
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

DeviceStorage s_storage;

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
    void set_event_callback(EventCallback callback, void *context) override
    {
        callback_ = callback;
        callback_context_ = context;
    }

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
            esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
            return;
        }
        // The IDF driver emits a warning whenever a connect request arrives
        // while its scan has not found a matching AP. That is expected during
        // our paced reconnects and is not actionable; keep the HAL's own
        // disconnect diagnostics while silencing the driver's warning stream.
        esp_log_level_set("wifi", ESP_LOG_ERROR);
        (void)esp_wifi_set_storage(WIFI_STORAGE_FLASH);
        (void)esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &DeviceWifi::event_handler, this);
        (void)esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &DeviceWifi::event_handler, this);
        (void)esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &DeviceWifi::event_handler, this);
        enabled_ = read_enabled();
        apply_stored_ip_config();
        (void)set_hostname("crystal");
        wifi_config_t saved_config = {};
        const bool have_saved = esp_wifi_get_config(WIFI_IF_STA, &saved_config) == ESP_OK &&
                                saved_config.sta.ssid[0] != 0;
        if (have_saved) {
            // The config comes back from flash and may have been written by an
            // older build with a too-strict threshold, or pinned to a channel and
            // BSSID the AP has since moved off. Reset the routing fields so the
            // connect attempt does a full scan under a sane security floor.
            saved_config.sta.threshold.authmode =
                saved_config.sta.password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
            saved_config.sta.bssid_set = false;
            saved_config.sta.channel = 0;
            saved_config.sta.pmf_cfg.capable = true;
            saved_config.sta.pmf_cfg.required = false;
            (void)esp_wifi_set_config(WIFI_IF_STA, &saved_config);
        }
        // Arm the deferred connect before starting: WIFI_EVENT_STA_START can be
        // dispatched from inside esp_wifi_start().
        started_ = true;
        pending_connect_ = enabled_ && have_saved;
        if (enabled_ && esp_wifi_start() != ESP_OK) {
            pending_connect_ = false;
            started_ = false;
            return;
        }
        if (have_saved) {
            strlcpy(last_ssid_, reinterpret_cast<const char *>(saved_config.sta.ssid), sizeof(last_ssid_));
            notify(enabled_ ? Connecting : Disconnected);
        } else {
            // Notify the UI so the tile reflects either enabled/disconnected or
            // the persisted radio-off state when no network is remembered.
            notify(Disconnected);
        }
    }

    void scan() override
    {
        if (started_ && enabled_) {
            (void)esp_wifi_scan_start(nullptr, false);
        }
    }

    size_t scan_results(Network *out, size_t capacity) const override
    {
        const size_t count = scan_count_ < capacity ? scan_count_ : capacity;
        if (out != nullptr && count != 0) memcpy(out, scan_results_, count * sizeof(Network));
        return scan_count_;
    }

    void connect(const char *ssid, const char *pass) override
    {
        if (!started_ || !enabled_ || ssid == nullptr || pass == nullptr) {
            return;
        }

        wifi_config_t config = {};
        strlcpy(reinterpret_cast<char *>(config.sta.ssid), ssid, sizeof(config.sta.ssid));
        strlcpy(reinterpret_cast<char *>(config.sta.password), pass, sizeof(config.sta.password));
        config.sta.bssid_set = false;
        config.sta.channel = 0;
        // threshold.authmode is the *minimum* acceptable AP security. The enum is
        // ordered OPEN < WEP < WPA_PSK < WPA2_PSK < WPA_WPA2_PSK < ... < WPA3_PSK,
        // so anything above WPA_PSK silently filters out ordinary WPA2 routers and
        // the driver loops on "Haven't to connect to a suitable AP now!".
        config.sta.threshold.authmode = pass[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
        config.sta.pmf_cfg.capable = true;
        config.sta.pmf_cfg.required = false;
        has_connected_ = false;
        has_ip_ = false;
        pending_connect_ = false;
        retries_ = 0;
        backoff_index_ = 0;
        if (retry_timer_ != nullptr) (void)esp_timer_stop(retry_timer_);
        // Cancel any in-flight scan; esp_wifi_connect() fails while one is running.
        (void)esp_wifi_scan_stop();
        (void)esp_wifi_disconnect();
        if (esp_wifi_set_config(WIFI_IF_STA, &config) == ESP_OK) {
            strlcpy(last_ssid_, ssid, sizeof(last_ssid_));
            notify(Connecting);
            const esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
                notify(ConnectFailed);
            }
        }
    }

    void forget() override
    {
        if (!started_) return;
        pending_connect_ = false;
        retries_ = 0;
        backoff_index_ = 0;
        if (retry_timer_ != nullptr) (void)esp_timer_stop(retry_timer_);
        last_ssid_[0] = '\0';
        (void)esp_wifi_disconnect();
        wifi_config_t empty = {};
        const esp_err_t config_err = esp_wifi_set_config(WIFI_IF_STA, &empty);
        wifi_config_t verify = {};
        const esp_err_t verify_err = esp_wifi_get_config(WIFI_IF_STA, &verify);
        ESP_LOGI(TAG, "forgot WiFi credentials: set=%s, verify=%s, ssid_length=%u",
                 esp_err_to_name(config_err), esp_err_to_name(verify_err),
                 verify_err == ESP_OK ? static_cast<unsigned>(strlen(
                     reinterpret_cast<const char *>(verify.sta.ssid))) : 0U);
        has_connected_ = false;
        has_ip_ = false;
        notify(Disconnected);
    }

    bool connected() const override
    {
        wifi_ap_record_t record = {};
        return started_ && esp_wifi_sta_get_ap_info(&record) == ESP_OK;
    }

    bool has_ip() const override { return has_ip_; }

    bool enabled() const override { return enabled_; }
    void set_enabled(bool enabled) override
    {
        if (enabled == enabled_) return;
        enabled_ = enabled;
        write_enabled(enabled_);
        // The service task starts after the UI. Preserve an early toggle now;
        // start() will read the same persisted value before touching the radio.
        if (!started_) return;
        retries_ = 0;
        backoff_index_ = 0;
        if (retry_timer_ != nullptr) (void)esp_timer_stop(retry_timer_);
        if (enabled_) {
            // Connect from WIFI_EVENT_STA_START, not here: right after
            // esp_wifi_start() the station is not yet up and connect() returns
            // ESP_ERR_WIFI_STATE.
            pending_connect_ = last_ssid_[0] != 0;
            if (esp_wifi_start() != ESP_OK) {
                pending_connect_ = false;
                notify(Disconnected);
                return;
            }
            notify(pending_connect_ ? Connecting : Disconnected);
        } else {
            pending_connect_ = false;
            // Cancel any in-flight scan and drop its results: they are void once
            // the radio cycles, and a late SCAN_DONE would repopulate the UI with
            // networks from before the toggle.
            (void)esp_wifi_scan_stop();
            scan_count_ = 0;
            (void)esp_wifi_disconnect(); (void)esp_wifi_stop(); notify(Disconnected);
        }
    }
    const char *last_ssid() const override { return last_ssid_; }

    bool ip_config(IpConfig *out) const override
    {
        if (out == nullptr || netif_ == nullptr || !has_ip_) return false;
        esp_netif_ip_info_t info = {};
        esp_netif_dns_info_t dns = {};
        if (esp_netif_get_ip_info(netif_, &info) != ESP_OK) return false;
        out->dhcp = dhcp_enabled_;
        out->ip = info.ip.addr;
        out->mask = info.netmask.addr;
        out->gateway = info.gw.addr;
        out->dns1 = esp_netif_get_dns_info(netif_, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK
                        ? dns.ip.u_addr.ip4.addr : 0;
        out->dns2 = esp_netif_get_dns_info(netif_, ESP_NETIF_DNS_BACKUP, &dns) == ESP_OK
                        ? dns.ip.u_addr.ip4.addr : 0;
        return true;
    }

    bool set_ip_config(const IpConfig &config) override
    {
        if (netif_ == nullptr) return false;
        if (config.dhcp) {
            const esp_err_t err = esp_netif_dhcpc_start(netif_);
            if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) return false;
        } else {
            // The client must stop before applying the address or its next lease
            // will overwrite the static configuration.
            (void)esp_netif_dhcpc_stop(netif_);
            esp_netif_ip_info_t info = {};
            info.ip.addr = config.ip;
            info.netmask.addr = config.mask;
            info.gw.addr = config.gateway;
            if (esp_netif_set_ip_info(netif_, &info) != ESP_OK) return false;
            esp_netif_dns_info_t dns = {};
            dns.ip.type = ESP_IPADDR_TYPE_V4;
            dns.ip.u_addr.ip4.addr = config.dns1;
            if (esp_netif_set_dns_info(netif_, ESP_NETIF_DNS_MAIN, &dns) != ESP_OK) return false;
            dns.ip.u_addr.ip4.addr = config.dns2;
            (void)esp_netif_set_dns_info(netif_, ESP_NETIF_DNS_BACKUP, &dns);
        }
        dhcp_enabled_ = config.dhcp;
        if (!persist_ip_config(config)) {
            ESP_LOGE(TAG, "WiFi IP configuration applied but NVS persistence failed");
            return false;
        }
        if (started_ && enabled_ && last_ssid_[0] != '\0') {
            (void)esp_wifi_disconnect();
            pending_connect_ = false;
            (void)esp_wifi_connect();
        }
        return true;
    }

    bool mac(uint8_t out[6]) const override
    {
        return out != nullptr && esp_wifi_get_mac(WIFI_IF_STA, out) == ESP_OK;
    }

    bool rssi(int8_t *out) const override
    {
        if (out == nullptr) return false;
        wifi_ap_record_t record = {};
        if (esp_wifi_sta_get_ap_info(&record) != ESP_OK) return false;
        *out = record.rssi;
        return true;
    }

    void set_power_save(bool enabled) override
    {
        if (started_) (void)esp_wifi_set_ps(enabled ? WIFI_PS_MAX_MODEM : WIFI_PS_NONE);
    }

    bool set_hostname(const char *name) override
    {
        return netif_ != nullptr && name != nullptr && name[0] != '\0' &&
               esp_netif_set_hostname(netif_, name) == ESP_OK;
    }

private:
    static bool read_blob(const char *key, void *value, size_t size)
    {
        size_t length = size;
        return s_storage.get(key, value, &length) && length == size;
    }

    static bool persist_ip_config(const IpConfig &config)
    {
        const uint8_t dhcp = config.dhcp ? 1 : 0;
        if (!s_storage.set(kDhcpKey, &dhcp, sizeof(dhcp))) return false;
        if (config.dhcp) return true;

        return s_storage.set("net.ip", &config.ip, sizeof(config.ip)) &&
               s_storage.set("net.mask", &config.mask, sizeof(config.mask)) &&
               s_storage.set("net.gw", &config.gateway, sizeof(config.gateway)) &&
               s_storage.set("net.dns1", &config.dns1, sizeof(config.dns1)) &&
               s_storage.set("net.dns2", &config.dns2, sizeof(config.dns2));
    }

    void apply_stored_ip_config()
    {
        IpConfig config = {true, 0, 0, 0, 0, 0};
        uint8_t dhcp = 1;
        (void)read_blob(kDhcpKey, &dhcp, sizeof(dhcp));
        config.dhcp = dhcp != 0;
        if (!config.dhcp && read_blob("net.ip", &config.ip, sizeof(config.ip)) &&
                read_blob("net.mask", &config.mask, sizeof(config.mask)) &&
                read_blob("net.gw", &config.gateway, sizeof(config.gateway)) &&
                read_blob("net.dns1", &config.dns1, sizeof(config.dns1))) {
            (void)read_blob("net.dns2", &config.dns2, sizeof(config.dns2));
            (void)set_ip_config(config);
        } else {
            dhcp_enabled_ = true;
        }
    }

    static bool read_enabled()
    {
        nvs_handle_t handle;
        if (nvs_open(kStorageNamespace, NVS_READONLY, &handle) != ESP_OK) return true;
        uint8_t value = 1;
        const esp_err_t err = nvs_get_u8(handle, kWifiEnabledKey, &value);
        nvs_close(handle);
        return err == ESP_OK ? value != 0 : true;
    }

    static void write_enabled(bool enabled)
    {
        // Phase 11 edit begin: preserve the legacy u8 Wi-Fi key while verifying it.
        nvs_handle_t handle;
        if (nvs_open(kStorageNamespace, NVS_READWRITE, &handle) != ESP_OK) {
            ESP_LOGE(TAG, "NVS set key=%s open failed", kWifiEnabledKey);
            return;
        }
        uint8_t old_value = 0;
        const bool old_present = nvs_get_u8(handle, kWifiEnabledKey, &old_value) == ESP_OK;
        const uint8_t value = enabled ? 1 : 0;
        esp_err_t err = nvs_set_u8(handle, kWifiEnabledKey, value);
        if (err == ESP_OK) err = nvs_commit(handle);
        uint8_t verify = 0;
        const bool readback_ok = err == ESP_OK && nvs_get_u8(handle, kWifiEnabledKey, &verify) == ESP_OK && verify == value;
        ESP_LOGI(TAG, "NVS set key=%s old=%s requested=%u write=%s readback=%s",
                 kWifiEnabledKey, old_present ? (old_value ? "1" : "0") : "<unset>",
                 static_cast<unsigned>(value), err == ESP_OK ? "ok" : esp_err_to_name(err),
                 readback_ok ? "ok" : "failed");
        nvs_close(handle);
        // Phase 11 edit end.
    }

    static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
    {
        auto *self = static_cast<DeviceWifi *>(arg);
        if (self == nullptr) return;
        if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
            self->has_ip_ = true;
            self->notify(GotIp);
        } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
            self->has_ip_ = false;
        } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
            if (self->pending_connect_) {
                self->pending_connect_ = false;
                const esp_err_t err = esp_wifi_connect();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
                    self->notify(ConnectFailed);
                }
            }
        } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
            // The lease dies with the association, and LOST_IP is not always
            // delivered on a drop, so clear it here as well.
            self->has_ip_ = false;
            const auto *disconnected = static_cast<const wifi_event_sta_disconnected_t *>(data);
            const uint8_t reason = disconnected != nullptr ? disconnected->reason : 0;
            if (disconnected != nullptr) ESP_LOGW(TAG, "WiFi disconnected, reason=%u", reason);
            // AUTH_EXPIRE / handshake timeouts are routinely transient, especially
            // on the first attempt after boot while the panel and PSRAM are still
            // settling. Retry ten times before moving to the long reconnect
            // backoff. A missing AP is also retried: it may simply be out of
            // range temporarily and should not disable automatic recovery.
            if (self->enabled_ && self->last_ssid_[0] != 0) {
                ++self->retries_;
                if (self->retries_ <= kMaxRetries) {
                    ESP_LOGW(TAG, "Retrying connect (%u/%u)", self->retries_, kMaxRetries);
                    self->schedule_retry();
                } else {
                    self->retries_ = 0;
                    self->schedule_backoff();
                }
                self->notify(Connecting);
                return;
            }
            self->retries_ = 0;
            self->notify(self->has_connected_ ? Disconnected : ConnectFailed);
        } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
            uint16_t count = 0;
            if (esp_wifi_scan_get_ap_num(&count) != ESP_OK) { self->notify(ScanDone); return; }
            uint16_t fetch = count < 20 ? count : 20;
            if (esp_wifi_scan_get_ap_records(&fetch, s_wifi_records) == ESP_OK) {
                self->scan_count_ = 0;
                for (uint16_t i = 0; i < fetch && self->scan_count_ < 20; ++i) {
                    const char *ssid = reinterpret_cast<const char *>(s_wifi_records[i].ssid);
                    bool duplicate = false;
                    for (size_t existing = 0; existing < self->scan_count_; ++existing) {
                        if (strcmp(self->scan_results_[existing].ssid, ssid) == 0) { duplicate = true; break; }
                    }
                    if (duplicate || ssid[0] == '\0') continue;
                    Network &network = self->scan_results_[self->scan_count_++];
                    strlcpy(network.ssid, ssid, sizeof(network.ssid));
                    network.rssi = s_wifi_records[i].rssi;
                    network.secured = s_wifi_records[i].authmode != WIFI_AUTH_OPEN;
                }
                for (size_t i = 1; i < self->scan_count_; ++i) {
                    Network current = self->scan_results_[i];
                    size_t j = i;
                    while (j > 0 && self->scan_results_[j - 1].rssi < current.rssi) {
                        self->scan_results_[j] = self->scan_results_[j - 1]; --j;
                    }
                    self->scan_results_[j] = current;
                }
                wifi_ap_record_t connected_ap = {};
                if (esp_wifi_sta_get_ap_info(&connected_ap) == ESP_OK) {
                    const char *connected_ssid = reinterpret_cast<const char *>(connected_ap.ssid);
                    for (size_t i = 0; i < self->scan_count_; ++i) {
                        if (strcmp(self->scan_results_[i].ssid, connected_ssid) == 0 && i != 0) {
                            Network connected = self->scan_results_[i];
                            memmove(&self->scan_results_[1], &self->scan_results_[0], i * sizeof(Network));
                            self->scan_results_[0] = connected;
                            break;
                        }
                    }
                }
            }
            self->notify(ScanDone);
        }
        (void)data;
    }
    // Reconnect off the event-loop task: esp_wifi_connect() from inside the
    // disconnect handler re-enters the driver while it is still tearing the
    // previous association down.
    void schedule_retry()
    {
        if (retry_timer_ == nullptr) {
            const esp_timer_create_args_t args = {
                .callback = &DeviceWifi::retry_cb,
                .arg = this,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "wifi_retry",
                .skip_unhandled_events = true,
            };
            if (esp_timer_create(&args, &retry_timer_) != ESP_OK) return;
        }
        (void)esp_timer_stop(retry_timer_);
        // Keep the first ten attempts responsive; the long backoff starts only
        // after all ten have failed.
        (void)esp_timer_start_once(retry_timer_, static_cast<uint64_t>(retries_) * 1000000ULL);
    }

    void schedule_backoff()
    {
        if (retry_timer_ == nullptr) {
            const esp_timer_create_args_t args = {
                .callback = &DeviceWifi::retry_cb,
                .arg = this,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "wifi_retry",
                .skip_unhandled_events = true,
            };
            if (esp_timer_create(&args, &retry_timer_) != ESP_OK) return;
        }
        static constexpr uint32_t kBackoffMinutes[] = {1, 3, 5, 10, 30};
        const uint32_t minutes = kBackoffMinutes[backoff_index_];
        backoff_index_ = (backoff_index_ + 1) % (sizeof(kBackoffMinutes) / sizeof(kBackoffMinutes[0]));
        ESP_LOGW(TAG, "WiFi retries exhausted; reconnecting in %u minute%s",
                 minutes, minutes == 1 ? "" : "s");
        (void)esp_timer_stop(retry_timer_);
        (void)esp_timer_start_once(retry_timer_, static_cast<uint64_t>(minutes) * 60ULL * 1000000ULL);
    }

    static void retry_cb(void *arg)
    {
        auto *self = static_cast<DeviceWifi *>(arg);
        if (self == nullptr || !self->enabled_ || !self->started_) return;
        const esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Retry esp_wifi_connect failed: %s", esp_err_to_name(err));
            self->notify(ConnectFailed);
        }
    }

    void notify(Event event)
    {
        if (event == GotIp) { has_connected_ = true; retries_ = 0; backoff_index_ = 0; }
        if (callback_ != nullptr) callback_(event, callback_context_);
    }
    static constexpr uint8_t kMaxRetries = 10;
    esp_netif_t *netif_ = nullptr;
    bool started_ = false;
    bool enabled_ = true;
    EventCallback callback_ = nullptr;
    void *callback_context_ = nullptr;
    Network scan_results_[20] = {};
    size_t scan_count_ = 0;
    bool has_connected_ = false;
    // Written from the esp_event task, read from the service task, so keep the
    // compiler from caching it. Single byte, one writer: no lock needed.
    volatile bool has_ip_ = false;
    bool dhcp_enabled_ = true;
    volatile bool pending_connect_ = false;
    uint8_t retries_ = 0;
    uint8_t backoff_index_ = 0;
    esp_timer_handle_t retry_timer_ = nullptr;
    char last_ssid_[33] = {};
};

class Axp2101Power final : public IPower {
public:
    bool readBattery(int *percent, bool *charging) override
    {
        if (percent == nullptr || charging == nullptr || !ensureDevice()) {
            return false;
        }
        uint8_t status1 = 0;
        uint8_t status2 = 0;
        uint8_t capacity = 0;
        if (!readRegister(0x00, &status1) || !readRegister(0x01, &status2) ||
                !readRegister(0xA4, &capacity) || (status1 & (1U << 3)) == 0 || capacity > 100) {
            return false;
        }
        *percent = capacity;
        *charging = (status2 >> 5) == 0x01;
        return true;
    }

private:
    bool ensureDevice()
    {
        if (device_ != nullptr) {
            return true;
        }
        i2c_device_config_t config = {};
        config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = kAxp2101Address;
        config.scl_speed_hz = 100000;
        return i2c_master_bus_add_device(bsp_i2c_get_handle(), &config, &device_) == ESP_OK;
    }

    bool readRegister(uint8_t reg, uint8_t *value)
    {
        return i2c_master_transmit_receive(device_, &reg, 1, value, 1, 50) == ESP_OK;
    }

    i2c_master_dev_handle_t device_ = nullptr;
};

class DeviceSystemInfo final : public ISystemInfo {
public:
    uint32_t free_heap() const override { return esp_get_free_heap_size(); }
    uint32_t free_psram() const override { return heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }
    uint32_t uptime_seconds() const override
    {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
    }
    const char *reset_reason() const override
    {
        switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "Power on";
        case ESP_RST_SW: return "Software restart";
        case ESP_RST_PANIC: return "Software crash";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT: return "Watchdog";
        case ESP_RST_BROWNOUT: return "Brownout";
        case ESP_RST_DEEPSLEEP: return "Deep sleep wake";
        default: return "Other";
        }
    }
    const char *idf_version() const override { return esp_get_idf_version(); }
    const char *app_version() const override { return esp_app_get_description()->version; }
    bool chip_id(uint8_t out[6]) const override
    {
        return out != nullptr && esp_efuse_mac_get_default(out) == ESP_OK;
    }
    bool storage_bytes(uint32_t *used, uint32_t *total) const override
    {
        size_t used_bytes = 0;
        size_t total_bytes = 0;
        if (used == nullptr || total == nullptr ||
                esp_spiffs_info("storage", &total_bytes, &used_bytes) != ESP_OK) return false;
        *used = static_cast<uint32_t>(used_bytes);
        *total = static_cast<uint32_t>(total_bytes);
        return true;
    }
};

DeviceBrightness s_brightness;
Pcf85063Rtc s_rtc;
esp_codec_dev_handle_t s_speaker = nullptr;

bool ensure_speaker()
{
    if (s_speaker != nullptr) return true;
    s_speaker = bsp_audio_codec_speaker_init();
    if (s_speaker == nullptr) return false;
    esp_codec_dev_sample_info_t format = {};
    format.bits_per_sample = 16;
    format.channel = 1;
    format.sample_rate = kAlarmSampleRate;
    if (esp_codec_dev_open(s_speaker, &format) != ESP_CODEC_DEV_OK) {
        s_speaker = nullptr;
        return false;
    }
    return true;
}

bool write_alarm_tone(uint32_t frequency_hz, uint32_t duration_ms)
{
    int16_t samples[256] = {};
    const size_t total = kAlarmSampleRate * duration_ms / 1000;
    size_t written = 0;
    while (written < total) {
        const size_t count = (total - written) < 256 ? total - written : 256;
        for (size_t i = 0; i < count; ++i) {
            const size_t sample_index = written + i;
            const float phase = 2.0f * kPi * static_cast<float>(frequency_hz) *
                                static_cast<float>(sample_index) / static_cast<float>(kAlarmSampleRate);
            const size_t edge = sample_index < (total - sample_index) ? sample_index : total - sample_index;
            const float envelope = edge < 180 ? static_cast<float>(edge) / 180.0f : 1.0f;
            samples[i] = frequency_hz == 0 ? 0 : static_cast<int16_t>(14000.0f * envelope * sinf(phase));
        }
        if (esp_codec_dev_write(s_speaker, samples, static_cast<int>(count * sizeof(samples[0]))) != ESP_CODEC_DEV_OK) {
            return false;
        }
        written += count;
    }
    return true;
}
// Polled rather than interrupt-driven: a 5 s hold needs no debounce, and
// esp_lcd_touch already owns the shared GPIO ISR service.
void reset_button_task(void *)
{
    uint32_t held_ms = 0;
    bool armed = false;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(kResetPollMs));

        if (gpio_get_level(kResetButtonGpio) != 0) {
            if (held_ms >= kResetPollMs) ESP_LOGI(TAG, "reset button released after %ums", (unsigned)held_ms);
            held_ms = 0;
            armed = false;
            continue;
        }

        held_ms += kResetPollMs;

        // Log once per press so a partial hold is visible in the console.
        if (!armed && held_ms >= 1000) {
            armed = true;
            ESP_LOGW(TAG, "reset button held, rebooting in %ums unless released",
                     (unsigned)(kResetHoldMs - held_ms));
        }

        if (held_ms >= kResetHoldMs) {
            ESP_LOGW(TAG, "reset button held %ums, restarting", (unsigned)held_ms);
            // Flush the log line before the reset drops the UART.
            vTaskDelay(pdMS_TO_TICKS(50));
            esp_restart();
        }
    }
}

void start_reset_button()
{
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << kResetButtonGpio;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;

    const esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "reset button gpio_config failed: %s", esp_err_to_name(err));
        return;
    }

    if (xTaskCreate(reset_button_task, "reset_btn", 2560, nullptr, 3, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "reset button task creation failed");
        return;
    }
    ESP_LOGI(TAG, "reset button ready on GPIO%d, hold %ums to reboot",
             (int)kResetButtonGpio, (unsigned)kResetHoldMs);
}

DeviceWifi s_wifi;
DeviceTouch s_touch;
Axp2101Power s_power;
DeviceSystemInfo s_system_info;
CrystalHal s_hal = {&s_brightness, &s_rtc, &s_wifi, &s_storage, &s_touch, &s_power, &s_system_info};

} // namespace

CrystalHal &hal()
{
    return s_hal;
}

bool crystal_hal_set_volume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > kVolumeMax) volume = kVolumeMax;
    if (!ensure_speaker()) return false;
    // Same operation as reference bsp_extra_codec_volume_set().
    return esp_codec_dev_set_out_vol(s_speaker, volume) == ESP_CODEC_DEV_OK;
}

int crystal_hal_get_volume()
{
    if (!ensure_speaker()) return 85;
    int volume = 85;
    (void)esp_codec_dev_get_out_vol(s_speaker, &volume);
    return volume;
}

void crystal_hal_init()
{
    // Restore user controls after NVS and the board display have initialized.
    uint8_t brightness = s_brightness.get();
    size_t brightness_len = sizeof(brightness);
    if (s_storage.get("brightness", &brightness, &brightness_len) &&
        brightness_len == sizeof(brightness)) {
        s_brightness.set(brightness);
    } else {
        s_brightness.set(s_brightness.get());
    }

    // The codec is initialized lazily by the volume adapter. Apply the saved
    // value here so audio uses the user's setting before the quick panel opens.
    uint8_t volume = 85;
    size_t volume_len = sizeof(volume);
    if (s_storage.get("volume", &volume, &volume_len) &&
        volume_len == sizeof(volume)) {
        (void)crystal_hal_set_volume(volume);
    }

    start_reset_button();
}

void crystal_hal_bind_touch(void *lvgl_input_device)
{
    s_touch.bind(lvgl_input_device);
}

void crystal_hal_timer_alarm()
{
    if (!ensure_speaker()) {
        ESP_LOGE(TAG, "speaker initialization failed");
        return;
    }

    (void)bsp_audio_poweramp_enable(true);
    const bool played = write_alarm_tone(880, 120) && write_alarm_tone(0, 55) &&
                        write_alarm_tone(1175, 120) && write_alarm_tone(0, 55) &&
                        write_alarm_tone(880, 180) && write_alarm_tone(0, 40);
    (void)bsp_audio_poweramp_enable(false);
    if (played) ESP_LOGI(TAG, "timer alarm played");
    else ESP_LOGE(TAG, "timer alarm playback failed");
}
