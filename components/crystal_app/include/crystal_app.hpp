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
    enum class LifecycleState : uint8_t {
        Installed,
        Created,
        Started,
        Resumed,
        Paused,
        Destroyed,
    };

    CrystalApp(const char *name, const void *launcher_icon = nullptr);
    ~CrystalApp() override = default;

    CrystalState &state() { return state_; }
    const CrystalState &state() const { return state_; }
    LifecycleState lifecycle_state() const { return lifecycle_state_; }

protected:
    bool init() final;
    bool deinit() final;
    bool run() final;
    bool pause() final;
    bool resume() final;
    bool close() final;
    bool back() final;

    // No onInstall()/onUninstall(): Android has no such hook, and the name
    // misdescribed what it did. Once-per-boot setup now belongs in onCreate(),
    // guarded by the app if it must not repeat on every launch.
    //
    // Six Android lifecycle hooks are declared. v1 dispatches onCreate, onPause,
    // onResume, and onDestroy (plus onBack). onStart/onStop are reserved: cards are
    // opened and closed by swipes with nothing in between, so there is no event
    // to map them to yet. They stay wired so adding one later is additive
    // rather than an ABI break.
    virtual bool onCreate() { return true; }
    virtual bool onStart() { return true; }
    virtual bool onPause() { return true; }
    virtual bool onResume() { return true; }
    virtual bool onStop() { return true; }
    virtual bool onDestroy() { return true; }
    virtual bool onBack() { return notifyCoreClosed(); }

private:
    bool is_active() const;

    std::string app_name_;
    CrystalState state_;
    LifecycleState lifecycle_state_ = LifecycleState::Destroyed;
};
