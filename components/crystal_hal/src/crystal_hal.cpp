/*
 * SPDX-License-Identifier: MIT
 */

#include "crystal_hal.hpp"

#include "esp_err.h"
#include "bsp/display.h"

namespace {

constexpr uint8_t kBrightnessMax = 95;

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

DeviceBrightness s_brightness;
CrystalHal s_hal = {&s_brightness, nullptr, nullptr, nullptr, nullptr};

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
