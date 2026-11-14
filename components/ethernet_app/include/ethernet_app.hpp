/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once
#include "crystal_app.hpp"
#include "lvgl.h"
LV_IMG_DECLARE(crystal_ethernet_icon);
LV_IMG_DECLARE(crystal_ethernet_status_icon);
class EthernetInfoApp final : public CrystalApp {
public:
    EthernetInfoApp();
    bool onCreate() override;
    bool onPause() override;
    bool onResume() override;
    bool onDestroy() override;
    void refresh();
private:
    lv_obj_t *root_ = nullptr;
    lv_obj_t *values_[8] = {};
    lv_timer_t *timer_ = nullptr;
};
