/*
 * SPDX-License-Identifier: MIT
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

struct CrystalHal {
    IBrightness *brightness;
    IRtc *rtc;
    IWifi *wifi;
    IStorage *storage;
    ITouchRaw *touch_raw;
    IPower *power;
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
