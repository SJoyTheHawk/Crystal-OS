/* SPDX-License-Identifier: MIT */
#pragma once

#include "crystal_app.hpp"

class StateTestApp final : public CrystalApp {
public:
    StateTestApp();

protected:
    bool onCreate() override;
    bool onResume() override;
    bool onPause() override;

private:
    static void increment_cb(lv_event_t *event);
    static void return_cb(lv_event_t *event);
    void refresh_count();

    lv_obj_t *count_label_ = nullptr;
    uint32_t counter_ = 0;
};
