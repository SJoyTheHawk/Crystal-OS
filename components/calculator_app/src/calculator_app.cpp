/*
 * SPDX-License-Identifier: MIT
 *
 * Arithmetic and input rules adapted from the Waveshare esp-brookesia
 * calculator example. UI ownership and lifecycle are Crystal OS specific.
 */

#include "calculator_app.hpp"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>

namespace {
constexpr const char *kFormulaKey = "formula";
constexpr size_t kFormulaMax = 63;
constexpr lv_coord_t kPagePad = 16;
constexpr lv_coord_t kGap = 12;

constexpr uint32_t kPage = 0x11181F;
constexpr uint32_t kPanel = 0x1C2733;
constexpr uint32_t kKey = 0x273541;
constexpr uint32_t kKeyPressed = 0x344553;
constexpr uint32_t kAccent = 0x53C8B6;
constexpr uint32_t kText = 0xF4F7FA;
constexpr uint32_t kMuted = 0x7C90A4;

const char *kKeyboardMap[] = {
    "C", "/", "x", LV_SYMBOL_BACKSPACE, "\n",
    "7", "8", "9", "-", "\n",
    "4", "5", "6", "+", "\n",
    "1", "2", "3", "%", "\n",
    "0", ".", "=", ""
};

bool isOperatorButton(uint32_t id)
{
    return id == 0 || id == 1 || id == 2 || id == 3 ||
           id == 7 || id == 11 || id == 15 || id == 18;
}
}  // namespace

CalculatorApp::CalculatorApp() : CrystalApp("Calculator", &calculator_icon)
{
    calculator_icon_prepare();
}

bool CalculatorApp::onCreate()
{
    const lv_area_t area = getVisualArea();
    const lv_coord_t width = lv_area_get_width(&area);
    const lv_coord_t height = lv_area_get_height(&area);

    root_ = lv_obj_create(lv_scr_act());
    lv_obj_set_size(root_, width, height);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(kPage), 0);
    lv_obj_set_style_pad_all(root_, kPagePad, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    const lv_coord_t content_width = width - 2 * kPagePad;
    const lv_coord_t content_height = height - 2 * kPagePad;
    const lv_coord_t display_height = 112;
    const lv_coord_t keyboard_height = content_height - display_height - kGap;

    display_ = lv_obj_create(root_);
    lv_obj_set_size(display_, content_width, display_height);
    lv_obj_align(display_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(display_, 8, 0);
    lv_obj_set_style_border_width(display_, 0, 0);
    lv_obj_set_style_bg_color(display_, lv_color_hex(kPanel), 0);
    lv_obj_set_style_pad_hor(display_, 16, 0);
    lv_obj_set_style_pad_ver(display_, 10, 0);
    lv_obj_clear_flag(display_, LV_OBJ_FLAG_SCROLLABLE);

    history_label_ = lv_label_create(display_);
    lv_obj_set_width(history_label_, content_width - 32);
    lv_label_set_long_mode(history_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(history_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(history_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(history_label_, lv_color_hex(kMuted), 0);
    lv_obj_align(history_label_, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_label_set_text(history_label_, "");

    formula_label_ = lv_label_create(display_);
    lv_obj_set_width(formula_label_, content_width - 32);
    lv_label_set_long_mode(formula_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(formula_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(formula_label_, lv_color_hex(kText), 0);
    lv_obj_align(formula_label_, LV_ALIGN_TOP_RIGHT, 0, 21);

    result_label_ = lv_label_create(display_);
    lv_obj_set_width(result_label_, content_width - 32);
    lv_label_set_long_mode(result_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(result_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(result_label_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(result_label_, lv_color_hex(kAccent), 0);
    lv_obj_align(result_label_, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    keyboard_ = lv_btnmatrix_create(root_);
    lv_btnmatrix_set_map(keyboard_, kKeyboardMap);
    lv_btnmatrix_set_btn_width(keyboard_, 16, 2);
    lv_btnmatrix_set_btn_width(keyboard_, 17, 1);
    lv_btnmatrix_set_btn_width(keyboard_, 18, 1);
    lv_obj_set_size(keyboard_, content_width, keyboard_height);
    lv_obj_align(keyboard_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(keyboard_, 8, 0);
    lv_obj_set_style_border_width(keyboard_, 0, 0);
    lv_obj_set_style_bg_color(keyboard_, lv_color_hex(kPage), 0);
    lv_obj_set_style_bg_opa(keyboard_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(keyboard_, 0, 0);
    lv_obj_set_style_pad_row(keyboard_, 8, 0);
    lv_obj_set_style_pad_column(keyboard_, 8, 0);
    lv_obj_set_style_text_font(keyboard_, &lv_font_montserrat_28, LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard_, lv_color_hex(kText), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard_, lv_color_hex(kKey), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard_, lv_color_hex(kKeyPressed),
                              static_cast<lv_style_selector_t>(static_cast<uint32_t>(LV_PART_ITEMS) |
                                                               static_cast<uint32_t>(LV_STATE_PRESSED)));
    lv_obj_set_style_border_width(keyboard_, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(keyboard_, 8, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(keyboard_, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(keyboard_, keyboardEvent, LV_EVENT_ALL, this);

    restoreFormula();
    updateFormulaFont();
    updateResult();
    return true;
}

bool CalculatorApp::onPause()
{
    saveFormula();
    return true;
}

bool CalculatorApp::onResume()
{
    restoreFormula();
    updateFormulaFont();
    updateResult();
    return true;
}

bool CalculatorApp::onDestroy()
{
    root_ = display_ = history_label_ = formula_label_ = result_label_ = keyboard_ = nullptr;
    formula_len_ = 1;
    return true;
}

void CalculatorApp::restoreFormula()
{
    char formula[kFormulaMax + 1] = {};
    size_t length = sizeof(formula);
    if (!state().get(kFormulaKey, formula, &length) || length == 0 ||
        length > sizeof(formula) || formula[length - 1] != '\0') {
        strcpy(formula, "0");
    }
    lv_label_set_text(formula_label_, formula);
    formula_len_ = strlen(formula);
    if (formula_len_ == 0) {
        lv_label_set_text(formula_label_, "0");
        formula_len_ = 1;
    }
}

void CalculatorApp::saveFormula()
{
    if (formula_label_ == nullptr) return;
    const char *formula = lv_label_get_text(formula_label_);
    (void)state().set(kFormulaKey, formula, strlen(formula) + 1);
}

bool CalculatorApp::append(const char *text)
{
    const size_t added = text != nullptr ? strlen(text) : 0;
    if (added == 0 || formula_len_ + added > kFormulaMax) return false;
    lv_label_ins_text(formula_label_, static_cast<uint32_t>(formula_len_), text);
    formula_len_ += added;
    return true;
}

bool CalculatorApp::isStartZero() const
{
    const char *text = lv_label_get_text(formula_label_);
    const size_t length = strlen(text);
    if (length == 1 && text[0] == '0') return true;
    return length >= 2 && text[length - 1] == '0' &&
           (text[length - 2] < '0' || text[length - 2] > '9');
}

bool CalculatorApp::isStartNum() const
{
    const char *text = lv_label_get_text(formula_label_);
    const size_t length = strlen(text);
    return length > 0 && text[length - 1] >= '0' && text[length - 1] <= '9';
}

bool CalculatorApp::isStartPercent() const
{
    const char *text = lv_label_get_text(formula_label_);
    const size_t length = strlen(text);
    return length > 0 && text[length - 1] == '%';
}

bool CalculatorApp::isLegalDot() const
{
    const char *text = lv_label_get_text(formula_label_);
    size_t length = strlen(text);
    while (length > 0) {
        const char c = text[--length];
        if (c == '.') return false;
        if (c < '0' || c > '9') return true;
    }
    return true;
}

double CalculatorApp::calculate(const char *input) const
{
    std::vector<double> stack;
    const int input_len = static_cast<int>(strlen(input));
    double num = 0;
    bool dot_flag = false;
    int dot_len = 0;
    char previous_sign = '+';

    for (int i = 0; i < input_len; ++i) {
        if (input[i] == '.') {
            dot_flag = true;
            dot_len = 0;
        } else if (isdigit(static_cast<unsigned char>(input[i]))) {
            if (!dot_flag) num = num * 10 + input[i] - '0';
            else num += (input[i] - '0') / pow(10.0, ++dot_len);
        } else if (input[i] == '%') {
            num /= 100.0;
        } else if (i != input_len - 1) {
            dot_flag = false;
            dot_len = 0;
            switch (previous_sign) {
            case '+': stack.push_back(num); break;
            case '-': stack.push_back(-num); break;
            case 'x': stack.back() *= num; break;
            default:
                if (num == 0) return 0;
                stack.back() /= num;
                break;
            }
            num = 0;
            previous_sign = input[i];
        }

        if (i == input_len - 1) {
            switch (previous_sign) {
            case '+': stack.push_back(num); break;
            case '-': stack.push_back(-num); break;
            case 'x': stack.back() *= num; break;
            default:
                if (num == 0) return 0;
                stack.back() /= num;
                break;
            }
        }
    }

    num = 0;
    for (double value : stack) num += value;
    return num;
}

void CalculatorApp::updateFormulaFont()
{
    const lv_font_t *font = formula_len_ <= 11 ? &lv_font_montserrat_48 :
                            formula_len_ <= 20 ? &lv_font_montserrat_28 :
                                                 &lv_font_montserrat_20;
    lv_obj_set_style_text_font(formula_label_, font, 0);
}

void CalculatorApp::updateResult()
{
    const double result = calculate(lv_label_get_text(formula_label_));
    char value[40];
    if (isfinite(result) && result >= static_cast<double>(LONG_MIN) &&
        result <= static_cast<double>(LONG_MAX) && static_cast<double>(static_cast<long>(result)) == result) {
        snprintf(value, sizeof(value), "%ld", static_cast<long>(result));
    } else {
        snprintf(value, sizeof(value), "%.6g", result);
    }
    lv_label_set_text_fmt(result_label_, "= %s", value);
}

void CalculatorApp::keyboardEvent(lv_event_t *event)
{
    auto *app = static_cast<CalculatorApp *>(lv_event_get_user_data(event));
    if (app == nullptr) return;

    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DRAW_PART_BEGIN) {
        auto *descriptor = static_cast<lv_obj_draw_part_dsc_t *>(lv_event_get_draw_part_dsc(event));
        if (descriptor != nullptr && descriptor->class_p == &lv_btnmatrix_class &&
            descriptor->type == LV_BTNMATRIX_DRAW_PART_BTN && isOperatorButton(descriptor->id)) {
            descriptor->label_dsc->color = lv_color_hex(kAccent);
            if (descriptor->id == 18) {
                descriptor->rect_dsc->bg_color = lv_color_hex(kAccent);
                descriptor->label_dsc->color = lv_color_hex(kPage);
            }
        }
        return;
    }
    if (code != LV_EVENT_VALUE_CHANGED) return;

    const uint32_t button = lv_btnmatrix_get_selected_btn(app->keyboard_);
    bool recalculate = false;
    bool equal = false;

    switch (button) {
    case 0:
        lv_label_set_text(app->formula_label_, "0");
        app->formula_len_ = 1;
        recalculate = true;
        break;
    case 3:
        if (app->formula_len_ == 1 && app->isStartZero()) break;
        lv_label_cut_text(app->formula_label_, static_cast<uint32_t>(--app->formula_len_), 1);
        if (app->formula_len_ == 0) {
            lv_label_set_text(app->formula_label_, "0");
            app->formula_len_ = 1;
        }
        recalculate = true;
        break;
    case 18:
        recalculate = true;
        equal = true;
        break;
    case 1:
    case 2:
    case 7:
    case 11:
    case 15:
        if (app->isStartPercent() || app->isStartNum()) {
            if ((button == 7 || button == 11) && app->isStartZero()) {
                lv_label_cut_text(app->formula_label_, static_cast<uint32_t>(--app->formula_len_), 1);
            }
            if (app->append(lv_btnmatrix_get_btn_text(app->keyboard_, button)) && button == 15) {
                recalculate = true;
            }
        }
        break;
    case 4:
    case 5:
    case 6:
    case 8:
    case 9:
    case 10:
    case 12:
    case 13:
    case 14:
    case 16:
        if (app->isStartZero()) {
            lv_label_cut_text(app->formula_label_, static_cast<uint32_t>(--app->formula_len_), 1);
        }
        if (!app->isStartPercent()) {
            recalculate = app->append(lv_btnmatrix_get_btn_text(app->keyboard_, button));
        }
        break;
    case 17:
        if (app->isLegalDot() && app->isStartNum()) recalculate = app->append(".");
        break;
    default:
        break;
    }

    app->updateFormulaFont();
    if (recalculate) app->updateResult();

    if (equal) {
        const char *result = lv_label_get_text(app->result_label_);
        result = result[0] == '=' && result[1] == ' ' ? result + 2 : result;
        char history[kFormulaMax + 16];
        snprintf(history, sizeof(history), "%s = %s", lv_label_get_text(app->formula_label_), result);
        lv_label_set_text(app->history_label_, history);
        lv_label_set_text(app->formula_label_, result);
        app->formula_len_ = strlen(result);
        app->updateFormulaFont();
    }
}
