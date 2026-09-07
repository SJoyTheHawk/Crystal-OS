/* SPDX-License-Identifier: MIT */
#pragma once

#include <stddef.h>

#include "crystal_app.hpp"
#include "lvgl.h"

LV_IMG_DECLARE(calculator_icon);
extern "C" void calculator_icon_prepare(void);

class CalculatorApp final : public CrystalApp {
public:
    CalculatorApp();

protected:
    bool onCreate() override;
    bool onPause() override;
    bool onResume() override;
    bool onDestroy() override;

private:
    static void keyboardEvent(lv_event_t *event);

    bool isStartZero() const;
    bool isStartNum() const;
    bool isStartPercent() const;
    bool isLegalDot() const;
    double calculate(const char *input) const;
    void updateResult();
    void updateFormulaFont();
    bool append(const char *text);
    void restoreFormula();
    void saveFormula();

    lv_obj_t *root_ = nullptr;
    lv_obj_t *display_ = nullptr;
    lv_obj_t *history_label_ = nullptr;
    lv_obj_t *formula_label_ = nullptr;
    lv_obj_t *result_label_ = nullptr;
    lv_obj_t *keyboard_ = nullptr;
    size_t formula_len_ = 1;
};
