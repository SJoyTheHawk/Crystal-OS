/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "esp_brookesia.hpp"

LV_IMG_DECLARE(hello_icon);
#ifdef __cplusplus
extern "C" {
#endif
void hello_icon_prepare(void);
#ifdef __cplusplus
}
#endif

class HelloApp final : public ESP_Brookesia_PhoneApp {
public:
    HelloApp();

    bool init() override;
    bool run() override;
    bool back() override;
    bool close() override;

private:
    static void return_button_cb(lv_event_t *event);
};
