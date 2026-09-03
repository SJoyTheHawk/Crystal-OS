/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "crystal_app.hpp"

LV_IMG_DECLARE(hello_icon);
#ifdef __cplusplus
extern "C" {
#endif
void hello_icon_prepare(void);
#ifdef __cplusplus
}
#endif

class HelloApp final : public CrystalApp {
public:
    HelloApp();

protected:
    bool onCreate() override;
    bool onDestroy() override;

private:
    static void return_button_cb(lv_event_t *event);
};
