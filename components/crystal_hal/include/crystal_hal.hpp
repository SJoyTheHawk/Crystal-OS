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
    virtual ~IWifi() = default;
    virtual void start() = 0;
    virtual void scan() = 0;
    virtual void connect(const char *ssid, const char *pass) = 0;
    virtual bool connected() const = 0;
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

struct CrystalHal {
    IBrightness *brightness;
    IRtc *rtc;
    IWifi *wifi;
    IStorage *storage;
    ITouchRaw *touch_raw;
};

CrystalHal &hal();

// Must be called after the board display has initialized its backlight.
void crystal_hal_init();
void crystal_hal_bind_touch(void *lvgl_input_device);

// Plays the short timer-expiry alert through the board speaker, if available.
void crystal_hal_timer_alarm();
