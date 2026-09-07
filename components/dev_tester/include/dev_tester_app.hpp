/* SPDX-License-Identifier: MIT */
#pragma once

#include "crystal_app.hpp"

LV_IMG_DECLARE(dev_tester_icon);
#ifdef __cplusplus
extern "C" {
#endif
void dev_tester_icon_prepare(void);
#ifdef __cplusplus
}
#endif

class DevTesterApp final : public CrystalApp {
public:
    DevTesterApp();

protected:
    bool onCreate() override;
    bool onDestroy() override;

private:
    lv_obj_t *add_field(const char *label, const char *placeholder, lv_coord_t y,
                        bool password = false);
    void update_output();
    static void field_event(lv_event_t *event);
    static void reveal_event(lv_event_t *event);

    lv_obj_t *viewport_ = nullptr;
    lv_obj_t *content_ = nullptr;
    lv_obj_t *top_field_ = nullptr;
    lv_obj_t *password_field_ = nullptr;
    lv_obj_t *bottom_field_ = nullptr;
    lv_obj_t *output_ = nullptr;
};
