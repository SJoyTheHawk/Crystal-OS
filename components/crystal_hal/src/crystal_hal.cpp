/*
 * SPDX-License-Identifier: MIT
 */

#include "crystal_hal.hpp"

#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "bsp/display.h"
#include "bsp/esp32_s3_touch_lcd_4b.h"
#include "driver/i2c_master.h"
#include "lvgl.h"
#include "nvs.h"

#include <math.h>

namespace {

constexpr uint8_t kBrightnessMax = 95;
constexpr int kVolumeMax = 100;
constexpr const char *kStorageNamespace = "crystal";
constexpr uint8_t kRtcAddress = 0x51;
constexpr uint8_t kAxp2101Address = 0x34;
constexpr uint32_t kAlarmSampleRate = 22050;
constexpr float kPi = 3.14159265358979323846f;
static const char *TAG = "crystal_hal";
constexpr size_t kWifiMaxNetworks = 20;
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
        (void)esp_wifi_set_storage(WIFI_STORAGE_FLASH);
        (void)esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &DeviceWifi::event_handler, this);
        (void)esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &DeviceWifi::event_handler, this);
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
        pending_connect_ = have_saved;
        if (esp_wifi_start() != ESP_OK) {
            pending_connect_ = false;
            return;
        }
        started_ = true;
        if (have_saved) {
            strlcpy(last_ssid_, reinterpret_cast<const char *>(saved_config.sta.ssid), sizeof(last_ssid_));
            notify(Connecting);
        } else {
            // The radio is enabled even when no remembered network exists.
            // Notify the UI so the tile uses the enabled/disconnected color.
            notify(Disconnected);
        }
    }

    void scan() override
    {
        if (started_) {
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
        if (!started_ || ssid == nullptr || pass == nullptr) {
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
        pending_connect_ = false;
        retries_ = 0;
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
        notify(Disconnected);
    }

    bool connected() const override
    {
        wifi_ap_record_t record = {};
        return started_ && esp_wifi_sta_get_ap_info(&record) == ESP_OK;
    }

    bool enabled() const override { return enabled_; }
    void set_enabled(bool enabled) override
    {
        if (!started_ || enabled == enabled_) return;
        enabled_ = enabled;
        retries_ = 0;
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

private:
    static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
    {
        auto *self = static_cast<DeviceWifi *>(arg);
        if (self == nullptr) return;
        if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
            self->notify(GotIp);
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
            const auto *disconnected = static_cast<const wifi_event_sta_disconnected_t *>(data);
            const uint8_t reason = disconnected != nullptr ? disconnected->reason : 0;
            if (disconnected != nullptr) ESP_LOGW(TAG, "WiFi disconnected, reason=%u", reason);
            // AUTH_EXPIRE / handshake timeouts are routinely transient, especially
            // on the first attempt after boot while the panel and PSRAM are still
            // settling. Retry a few times before telling the UI it failed.
            if (self->enabled_ && self->last_ssid_[0] != 0 && reason != WIFI_REASON_AUTH_FAIL &&
                reason != WIFI_REASON_NO_AP_FOUND && self->retries_ < kMaxRetries) {
                ++self->retries_;
                ESP_LOGW(TAG, "Retrying connect (%u/%u)", self->retries_, kMaxRetries);
                self->schedule_retry();
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
        // Linear backoff: 1s, 2s, 3s ...
        (void)esp_timer_start_once(retry_timer_, static_cast<uint64_t>(retries_) * 1000000ULL);
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
        if (event == GotIp) { has_connected_ = true; retries_ = 0; }
        if (callback_ != nullptr) callback_(event, callback_context_);
    }
    static constexpr uint8_t kMaxRetries = 5;
    esp_netif_t *netif_ = nullptr;
    bool started_ = false;
    bool enabled_ = true;
    EventCallback callback_ = nullptr;
    void *callback_context_ = nullptr;
    Network scan_results_[20] = {};
    size_t scan_count_ = 0;
    bool has_connected_ = false;
    volatile bool pending_connect_ = false;
    uint8_t retries_ = 0;
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

DeviceBrightness s_brightness;
DeviceStorage s_storage;
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
DeviceWifi s_wifi;
DeviceTouch s_touch;
Axp2101Power s_power;
CrystalHal s_hal = {&s_brightness, &s_rtc, &s_wifi, &s_storage, &s_touch, &s_power};

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
