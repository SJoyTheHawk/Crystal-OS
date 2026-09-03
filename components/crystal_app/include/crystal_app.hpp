/* SPDX-License-Identifier: MIT */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

#include "esp_brookesia.hpp"

class CrystalState final {
public:
    explicit CrystalState(const char *app_name);

    bool get(const char *key, void *value, size_t *length) const;
    bool set(const char *key, const void *value, size_t length);
    bool erase(const char *key);
    bool get_u32(const char *key, uint32_t *value) const;
    bool set_u32(const char *key, uint32_t value);

private:
    bool make_key(const char *key, std::string *out) const;
    std::string prefix_;
};

class CrystalApp : public ESP_Brookesia_PhoneApp {
public:
    CrystalApp(const char *name, const void *launcher_icon = nullptr);
    ~CrystalApp() override = default;

    CrystalState &state() { return state_; }
    const CrystalState &state() const { return state_; }

protected:
    bool run() final;
    bool pause() final;
    bool resume() final;
    bool close() final;
    bool back() final;

    virtual bool onCreate() { return true; }
    virtual bool onPause() { return true; }
    virtual bool onResume() { return true; }
    virtual bool onDestroy() { return true; }
    virtual bool onBack() { return notifyCoreClosed(); }

private:
    CrystalState state_;
};
