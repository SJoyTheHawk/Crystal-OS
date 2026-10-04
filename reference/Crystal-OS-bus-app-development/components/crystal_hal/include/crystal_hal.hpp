/*
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

struct IBrightness {
    virtual ~IBrightness() = default;
    virtual void set(uint8_t pct) = 0;
    virtual uint8_t get() const = 0;
};

struct IRtc {
    virtual ~IRtc() = default;
    virtual bool read(struct tm *out) = 0;
    virtual bool write(const struct tm *in) = 0;
};

struct IWifi {
    enum Event : uint8_t { GotIp, Disconnected, ScanDone, ConnectFailed, Connecting };
    struct Network { char ssid[33]; int8_t rssi; bool secured; };
    struct IpConfig {
        bool dhcp;
        uint32_t ip;
        uint32_t mask;
        uint32_t gateway;
        uint32_t dns1;
        uint32_t dns2;
    };
    using EventCallback = void (*)(Event event, void *context);
    virtual ~IWifi() = default;
    virtual void set_event_callback(EventCallback callback, void *context) = 0;
    virtual void start() = 0;
    virtual void scan() = 0;
    virtual void connect(const char *ssid, const char *pass) = 0;
    virtual void forget() = 0;
    // Associated with an AP. True before DHCP completes, so this is the right
    // question for a status indicator and the wrong one for network I/O.
    virtual bool connected() const = 0;
    // Associated *and* holding an IP lease. Anything that resolves a hostname
    // must gate on this: getaddrinfo() fails with EAI_FAIL between association
    // and GOT_IP, which is a window of roughly a second on a normal join.
    virtual bool has_ip() const = 0;
    virtual bool enabled() const = 0;
    virtual void set_enabled(bool enabled) = 0;
    virtual const char *last_ssid() const = 0;
    virtual size_t scan_results(Network *out, size_t capacity) const = 0;
    virtual bool ip_config(IpConfig *out) const = 0;
    virtual bool set_ip_config(const IpConfig &config) = 0;
    virtual bool mac(uint8_t out[6]) const = 0;
    virtual bool rssi(int8_t *out) const = 0;
    virtual void set_power_save(bool enabled) = 0;
    virtual bool set_hostname(const char *name) = 0;
};

struct IStorage {
    virtual ~IStorage() = default;
    virtual bool get(const char *key, void *value, size_t *length) = 0;
    virtual bool set(const char *key, const void *value, size_t length) = 0;
    virtual bool erase(const char *key) = 0;
};

struct ITouchRaw {
    virtual ~ITouchRaw() = default;
    struct Point { int16_t x; int16_t y; bool pressed; };
    virtual bool read(Point *out) = 0;
};

struct IPower {
    virtual ~IPower() = default;
    virtual bool readBattery(int *percent, bool *charging) = 0;
};

struct ISystemInfo {
    virtual ~ISystemInfo() = default;
    virtual uint32_t free_heap() const = 0;
    virtual uint32_t free_psram() const = 0;
    virtual uint32_t uptime_seconds() const = 0;
    virtual const char *reset_reason() const = 0;
    virtual const char *idf_version() const = 0;
    virtual const char *app_version() const = 0;
    virtual bool chip_id(uint8_t out[6]) const = 0;
    virtual bool storage_bytes(uint32_t *used, uint32_t *total) const = 0;
};

struct CrystalHal {
    IBrightness *brightness;
    IRtc *rtc;
    IWifi *wifi;
    IStorage *storage;
    ITouchRaw *touch_raw;
    IPower *power;
    ISystemInfo *system_info;
};

CrystalHal &hal();

// Board codec output volume, matching the reference bsp_extra 0..100 API.
// The adapter lazily initializes the speaker codec on first use.
bool crystal_hal_set_volume(int volume);
int crystal_hal_get_volume();

// Must be called after the board display has initialized its backlight.
void crystal_hal_init();
void crystal_hal_bind_touch(void *lvgl_input_device);

// Plays the short timer-expiry alert through the board speaker, if available.
void crystal_hal_timer_alarm();
