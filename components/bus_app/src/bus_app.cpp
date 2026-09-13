/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

#include "bus_app.hpp"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "crystal_core.hpp"
#include "esp_log.h"

namespace {
constexpr const char *TAG = "bus_app";

// CrystalState allows seven characters after the per-app namespace prefix. A
// longer key is rejected by make_key() and the write silently does nothing.
constexpr const char *kKeySaved = "saved";
constexpr const char *kKeyRecent = "recent";
constexpr const char *kKeyNav = "nav";

constexpr lv_coord_t kPad = 16;
constexpr lv_coord_t kGap = 12;
// The bottom 20 px of the app area is the vertical gesture edge band with the
// home indicator drawn on it. Anything interactive placed there is taken as the
// start of a swipe and never becomes a click -- the same trap the calculator
// keyboard hit.
constexpr lv_coord_t kBottomSafe = 26;
// main.cpp sets gesture.threshold.horizontal_edge to 24, so a button whose
// columns reach into that band loses its taps to the app-switch gesture.
constexpr lv_coord_t kEdgeInset = 14;
constexpr lv_coord_t kTabHeight = 44;
// The 40 px back button plus its gap. Titles in the stacked views start here.
constexpr lv_coord_t kBackWidth = 46;

constexpr uint32_t kPage = 0x11181F;
constexpr uint32_t kCard = 0x1C2733;
constexpr uint32_t kValue = 0xE6EDF5;
constexpr uint32_t kValueDim = 0x6B7C8D;
constexpr uint32_t kCaption = 0x7C90A4;
constexpr uint32_t kCaptionDim = 0x5E6E7E;
constexpr uint32_t kAccent = 0x53C8B6;
constexpr uint32_t kWarn = 0xC98A3C;

// 2020-01-01. The board's PCF85063 has no backup cell, so a cold boot starts the
// clock near the epoch until SNTP lands. Below this it is not a time, it is an
// unset counter.
constexpr int32_t kMinPlausibleEpoch = 1577836800;
// Past this the reading is presented as possibly out of date; past the second,
// the minute counts are withdrawn entirely. A 30-minute-old "3 min" is not
// stale, it is wrong.
constexpr int32_t kDimAfterSeconds = 5 * 60;
constexpr int32_t kWithdrawAfterSeconds = 30 * 60;
constexpr uint32_t kAutoRefreshMs = 30000;
// Live search settles this long after the last keystroke. Short enough that it
// feels like a consequence of typing, long enough that a fourth character
// arriving normally beats it.
constexpr uint32_t kAutoSearchMs = 450;
// The keypad tick has to be quicker than the 1 s service drain or a 450 ms
// debounce would resolve up to a second late.
constexpr uint32_t kTickMs = 200;

int32_t nowEpoch() { return static_cast<int32_t>(time(nullptr)); }
bool clockIsSet() { return nowEpoch() >= kMinPlausibleEpoch; }

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                    const char *text = "")
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    return label;
}

lv_obj_t *makeCard(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(kCard), 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, kPad, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

// A full-bleed layer inside a tab. The Route tab's four views are these, shown
// one at a time, so there are no nested screens to arbitrate gestures with.
lv_obj_t *makeLayer(lv_obj_t *parent)
{
    lv_obj_t *layer = lv_obj_create(parent);
    lv_obj_remove_style_all(layer);
    lv_obj_set_size(layer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(layer, 0, 0);
    lv_obj_clear_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
    return layer;
}

lv_obj_t *makeIconButton(lv_obj_t *parent, const char *glyph, lv_event_cb_t handler,
                         void *user_data)
{
    constexpr lv_coord_t kButton = 40;
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, kButton, kButton);
    lv_obj_set_ext_click_area(button, 6);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xC9D6E2), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(button, handler, LV_EVENT_CLICKED, user_data);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, glyph);
    lv_obj_set_style_text_color(label, lv_color_hex(0x8FA3B7), 0);
    lv_obj_center(label);
    return button;
}

bool isLive(const CrystalApp *app)
{
    switch (app->lifecycle_state()) {
    case CrystalApp::LifecycleState::Installed:
    case CrystalApp::LifecycleState::Created:
    case CrystalApp::LifecycleState::Started:
    case CrystalApp::LifecycleState::Resumed:
        return true;
    default:
        return false;
    }
}

// "3 min" is what you act on. Under a minute is Due, because a countdown that
// reads "0 min" for sixty seconds looks stuck.
void formatMinutes(int32_t seconds, char *out, size_t out_size)
{
    if (seconds < 60) snprintf(out, out_size, "Due");
    else snprintf(out, out_size, "%d min", static_cast<int>(seconds / 60));
}

void formatClock(int32_t epoch, char *out, size_t out_size)
{
    const time_t when = static_cast<time_t>(epoch);
    struct tm local = {};
    if (localtime_r(&when, &local) == nullptr) { out[0] = '\0'; return; }
    if (crystal_time_format_24()) {
        snprintf(out, out_size, "%02d:%02d", local.tm_hour, local.tm_min);
    } else {
        const int hour = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12;
        snprintf(out, out_size, "%d:%02d %s", hour, local.tm_min,
                 local.tm_hour >= 12 ? "PM" : "AM");
    }
}

void formatAge(int32_t seconds, char *out, size_t out_size)
{
    if (seconds < 30) snprintf(out, out_size, "Updated just now");
    else if (seconds < 60) snprintf(out, out_size, "Updated %d s ago", static_cast<int>(seconds));
    else if (seconds < 3600) snprintf(out, out_size, "Updated %d min ago", static_cast<int>(seconds / 60));
    else if (seconds < 2 * 3600) snprintf(out, out_size, "Updated 1 hour ago");
    else snprintf(out, out_size, "Updated %d hours ago", static_cast<int>(seconds / 3600));
}

double haversineMetres(double lat1, double lon1, double lat2, double lon2)
{
    const double to_rad = M_PI / 180.0;
    const double dlat = (lat2 - lat1) * to_rad;
    const double dlon = (lon2 - lon1) * to_rad;
    const double a = sin(dlat / 2) * sin(dlat / 2) +
                     cos(lat1 * to_rad) * cos(lat2 * to_rad) * sin(dlon / 2) * sin(dlon / 2);
    return 6371000.0 * 2.0 * asin(sqrt(a < 0 ? 0 : (a > 1 ? 1 : a)));
}

void formatDistance(double metres, char *out, size_t out_size)
{
    if (metres < 1000.0) snprintf(out, out_size, "%d m", static_cast<int>(metres + 0.5));
    else snprintf(out, out_size, "%.1f km", metres / 1000.0);
}

const char *operatorName(uint8_t op) { return op == TRANSIT_OP_CTB ? "Citybus" : "KMB/LWB"; }
}  // namespace

// ------------------------------------------------------------ event trampolines

namespace {
template <void (BusApp::*Method)()>
void dispatch(lv_event_t *event)
{
    auto *app = static_cast<BusApp *>(lv_event_get_user_data(event));
    if (app != nullptr) (app->*Method)();
}

template <void (BusApp::*Method)(size_t)>
void dispatchIndex(lv_event_t *event)
{
    auto *app = static_cast<BusApp *>(lv_event_get_user_data(event));
    if (app == nullptr) return;
    const size_t index = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target(event)));
    (app->*Method)(index);
}

void onKeyChar(void *context, char value)
{
    if (context != nullptr) static_cast<BusApp *>(context)->onKeyChar(value);
}
void onKeyBackspace(void *context)
{
    if (context != nullptr) static_cast<BusApp *>(context)->onKeyBackspace();
}
void onKeySubmit(void *context)
{
    if (context != nullptr) static_cast<BusApp *>(context)->onKeySubmit();
}

void onTick(lv_timer_t *timer)
{
    if (timer != nullptr && timer->user_data != nullptr) {
        static_cast<BusApp *>(timer->user_data)->tick();
    }
}

void onServiceEvent(const transit_event_t *event, void *user_data)
{
    if (user_data != nullptr) static_cast<BusApp *>(user_data)->handleEvent(event);
}
}  // namespace

BusApp::BusApp() : CrystalApp("Bus", &bus_icon)
{
    // The icon bitmap is computed, not stored, so it has to be filled before the
    // launcher first draws it.
    bus_icon_prepare();
}

// ------------------------------------------------------------------- lifecycle

bool BusApp::onCreate()
{
    const lv_area_t area = getVisualArea();
    const lv_coord_t width = lv_area_get_width(&area);
    const lv_coord_t height = lv_area_get_height(&area);

    // Position is (0, 0), not (area.x1, area.y1). getVisualArea() is display
    // coordinates and the active app screen already uses local ones, so the
    // offset shows up as a page that scrolls.
    root_ = lv_obj_create(lv_scr_act());
    lv_obj_set_size(root_, width, height);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(kPage), 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    content_width_ = width - 2 * kPad;
    page_height_ = height - kTabHeight - kPad - kBottomSafe;

    // Paint from cache before anything is requested, so a stored reading is on
    // the first frame.
    loadState();

    tabs_ = lv_tabview_create(root_, LV_DIR_TOP, kTabHeight);
    lv_obj_set_size(tabs_, width, height);
    lv_obj_set_style_bg_color(tabs_, lv_color_hex(kPage), 0);

    lv_obj_t *buttons = lv_tabview_get_tab_btns(tabs_);
    const auto items = static_cast<lv_style_selector_t>(LV_PART_ITEMS);
    const auto checked = static_cast<lv_style_selector_t>(
        static_cast<uint32_t>(LV_PART_ITEMS) | static_cast<uint32_t>(LV_STATE_CHECKED));
    lv_obj_set_style_bg_color(buttons, lv_color_hex(kPage), 0);
    lv_obj_set_style_text_font(buttons, &lv_font_montserrat_16, items);
    lv_obj_set_style_text_color(buttons, lv_color_hex(kCaption), items);
    lv_obj_set_style_border_width(buttons, 0, items);
    lv_obj_set_style_text_color(buttons, lv_color_hex(kValue), checked);
    lv_obj_set_style_bg_color(buttons, lv_color_hex(kCard), checked);
    lv_obj_add_event_cb(tabs_, dispatch<&BusApp::onTabChanged>, LV_EVENT_VALUE_CHANGED, this);

    saved_tab_ = lv_tabview_add_tab(tabs_, "Saved");
    route_tab_ = lv_tabview_add_tab(tabs_, "Route");
    nearby_tab_ = lv_tabview_add_tab(tabs_, "Nearby");
    lv_obj_clear_flag(lv_tabview_get_content(tabs_), LV_OBJ_FLAG_SCROLLABLE);

    for (lv_obj_t *tab : {saved_tab_, route_tab_, nearby_tab_}) {
        lv_obj_set_style_bg_color(tab, lv_color_hex(kPage), 0);
        lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(tab, 0, 0);
        lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    }

    buildSavedTab(saved_tab_);
    buildRouteTab(route_tab_);
    // Nearby is built on first visit, not here. onCreate() runs on the main task
    // under the LVGL lock against an 80 ms budget, and this tab is the one the
    // app never lands on: renderNearby() already no-ops while its list is null.

    showView(view_);
    renderSaved();
    renderRecents();

    // Landing tab: Saved whenever anything is saved, since that is the single
    // glance the app exists for. Otherwise Route, which is the path to filling it.
    lv_tabview_set_act(tabs_, saved_count_ > 0 ? 0 : 1, LV_ANIM_OFF);

    transit_service_set_foreground(true);
    transit_service_set_listener(onServiceEvent, this);

    // Never a network call from onCreate(): the budget is 80 ms under the LVGL
    // lock and the 5 s task watchdog is the real failure mode. The tick below
    // issues the first request instead.
    last_eta_refresh_ = 0;
    timer_ = lv_timer_create(onTick, kTickMs, this);
    return timer_ != nullptr;
}

bool BusApp::onPause()
{
    if (timer_ != nullptr) {
        lv_timer_del(timer_);
        timer_ = nullptr;
    }
    // A paused app issues no requests, and a resolve left running would be
    // writing for an app that may never come back.
    transit_service_set_foreground(false);
    transit_service_cancel_all();
    nearest_running_ = false;
    variants_request_ = stops_request_ = names_request_ = 0;
    eta_request_ = nearest_request_ = saved_eta_request_ = 0;
    eta_pending_ = false;
    saveNavigation();
    saveSavedStops();
    return true;
}

bool BusApp::onResume()
{
    transit_service_set_foreground(true);
    transit_service_set_listener(onServiceEvent, this);
    // Repaint from cache first, then let the tick decide whether to re-request.
    renderSaved();
    renderNearby();
    renderBoard();
    updateAges();
    last_eta_refresh_ = 0;
    timer_ = lv_timer_create(onTick, kTickMs, this);
    return timer_ != nullptr;
}

bool BusApp::onDestroy()
{
    if (timer_ != nullptr) {
        lv_timer_del(timer_);
        timer_ = nullptr;
    }
    saveNavigation();
    saveSavedStops();

    // Clearing the listener drops every queued result, and releasing the route
    // cancels the worker's borrow of it first. Order matters: release before the
    // pointers are forgotten, or the handle leaks.
    transit_service_set_foreground(false);
    transit_service_set_listener(nullptr, nullptr);
    transit_service_cancel_all();
    releaseRoute();

    // The shell deletes the screen's children; these pointers must not survive
    // it, because a late event would otherwise write to freed objects.
    root_ = tabs_ = saved_tab_ = route_tab_ = nearby_tab_ = nullptr;
    keypad_view_ = chooser_view_ = stops_view_ = board_view_ = nullptr;
    route_display_ = route_status_ = recent_row_ = nullptr;
    chooser_list_ = chooser_title_ = stops_title_ = stops_list_ = nullptr;
    nearest_button_ = nearest_label_ = nullptr;
    board_title_ = board_stop_ = board_sequence_ = board_empty_ = board_age_ = nullptr;
    save_button_ = save_glyph_ = nullptr;
    for (size_t i = 0; i < TRANSIT_MAX_ETA; ++i) {
        board_minutes_[i] = board_clock_[i] = board_remark_[i] = nullptr;
    }
    saved_list_ = saved_empty_ = saved_age_ = nullptr;
    nearby_list_ = nearby_note_ = nullptr;
    keypad_.forget();
    return true;
}

bool BusApp::onBack()
{
    // One layer per press, and only inside the Route tab. The home pill is Home;
    // Back is Back.
    if (tabs_ != nullptr && lv_tabview_get_tab_act(tabs_) == 1) {
        switch (view_) {
        case View::Board:
            showView(route_ != nullptr ? View::Stops : View::Keypad);
            return true;
        case View::Stops:
            showView(variant_count_ > 1 ? View::Chooser : View::Keypad);
            return true;
        case View::Chooser:
            showView(View::Keypad);
            return true;
        case View::Keypad:
            break;
        }
    }
    return CrystalApp::onBack();
}

// ------------------------------------------------------------------ tab layout

void BusApp::buildSavedTab(lv_obj_t *parent)
{
    lv_obj_t *page = makeLayer(parent);
    lv_obj_set_style_pad_hor(page, kPad, 0);
    lv_obj_set_style_pad_top(page, kPad, 0);
    lv_obj_set_style_pad_bottom(page, kBottomSafe, 0);

    saved_age_ = makeLabel(page, &lv_font_montserrat_16, kCaption);
    lv_obj_align(saved_age_, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // The list scrolls vertically only, so there is nothing to arbitrate against
    // the horizontal app-switch gesture.
    saved_list_ = lv_obj_create(page);
    lv_obj_remove_style_all(saved_list_);
    lv_obj_set_style_bg_opa(saved_list_, LV_OPA_TRANSP, 0);
    // 24 px reserved at the foot for the age line.
    lv_obj_set_size(saved_list_, content_width_, page_height_ - 24);
    lv_obj_align(saved_list_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_flex_flow(saved_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(saved_list_, kGap, 0);
    lv_obj_set_scroll_dir(saved_list_, LV_DIR_VER);

    saved_empty_ = lv_obj_create(page);
    lv_obj_remove_style_all(saved_empty_);
    lv_obj_set_style_bg_opa(saved_empty_, LV_OPA_TRANSP, 0);
    lv_obj_set_size(saved_empty_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(saved_empty_, LV_ALIGN_TOP_LEFT, 0, 40);
    lv_obj_t *headline = makeLabel(saved_empty_, &lv_font_montserrat_20, kValue,
                                   "No saved stops yet.");
    lv_obj_align(headline, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *hint = makeLabel(saved_empty_, &lv_font_montserrat_16, kCaption,
                               "Find a route to get started.");
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 0, 30);
}

void BusApp::buildRouteTab(lv_obj_t *parent)
{
    keypad_view_ = makeLayer(parent);
    lv_obj_set_style_pad_hor(keypad_view_, kPad, 0);
    lv_obj_set_style_pad_top(keypad_view_, 8, 0);
    lv_obj_set_style_pad_bottom(keypad_view_, kBottomSafe, 0);

    // Display row: backspace, the query, submit. All three on one line so the
    // grid below is nothing but characters and its geometry stays regular.
    lv_obj_t *display_row = lv_obj_create(keypad_view_);
    lv_obj_remove_style_all(display_row);
    lv_obj_set_style_bg_opa(display_row, LV_OPA_TRANSP, 0);
    lv_obj_set_size(display_row, LV_PCT(100), 52);
    lv_obj_align(display_row, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(display_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *backspace = keypad_.buildBackspace(display_row);
    lv_obj_align(backspace, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *submit = keypad_.buildSubmit(display_row);
    lv_obj_align(submit, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *display_card = makeCard(display_row);
    lv_obj_set_size(display_card, 180, 48);
    lv_obj_align(display_card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(display_card, 0, 0);

    // A label, not a textarea: an LVGL textarea never sends itself DEFOCUSED, so
    // it keeps a cursor blinking in a field this app never wants focused.
    route_display_ = makeLabel(display_card, &lv_font_montserrat_28, kValue, "");
    lv_obj_set_width(route_display_, 180);
    lv_obj_set_style_text_align(route_display_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(route_display_);

    keypad_.build(keypad_view_, this, ::onKeyChar, ::onKeyBackspace, ::onKeySubmit);
    lv_obj_align(keypad_.root(), LV_ALIGN_TOP_MID, 0, 60);

    // Three lines share the strip under the grid. Each is aligned to the bottom
    // rather than to a fixed y, so they stay inside the safe area whatever the
    // grid above them does.
    recent_row_ = lv_obj_create(keypad_view_);
    lv_obj_remove_style_all(recent_row_);
    lv_obj_set_style_bg_opa(recent_row_, LV_OPA_TRANSP, 0);
    lv_obj_set_size(recent_row_, content_width_, LV_SIZE_CONTENT);
    lv_obj_align(recent_row_, LV_ALIGN_BOTTOM_LEFT, 0, -48);
    lv_obj_set_flex_flow(recent_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(recent_row_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(recent_row_, 8, 0);
    lv_obj_clear_flag(recent_row_, LV_OBJ_FLAG_SCROLLABLE);

    route_status_ = makeLabel(keypad_view_, &lv_font_montserrat_16, kCaption,
                              "Type a route number.");
    lv_label_set_long_mode(route_status_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(route_status_, content_width_);
    lv_obj_align(route_status_, LV_ALIGN_BOTTOM_LEFT, 0, -22);

    // Public feeds, credited where the user can see it and not only in a doc.
    lv_obj_t *credit = makeLabel(keypad_view_, &lv_font_montserrat_16, kCaptionDim,
                                 "Data: KMB/LWB and Citybus open data");
    lv_obj_align(credit, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // --- chooser
    chooser_view_ = makeLayer(parent);
    lv_obj_set_style_pad_hor(chooser_view_, kPad, 0);
    lv_obj_set_style_pad_top(chooser_view_, kPad, 0);
    lv_obj_set_style_pad_bottom(chooser_view_, kBottomSafe, 0);
    // Design §2.3: a back affordance in the header. The hardware Back does the
    // same thing, but nothing on screen said so, and the gesture is not
    // discoverable from inside a view that arrived by a tap.
    lv_obj_t *chooser_back = makeIconButton(chooser_view_, LV_SYMBOL_LEFT,
                                            dispatch<&BusApp::onBackPressed>, this);
    lv_obj_align(chooser_back, LV_ALIGN_TOP_LEFT, -kPad + kEdgeInset, -8);

    chooser_title_ = makeLabel(chooser_view_, &lv_font_montserrat_20, kValue);
    lv_obj_align(chooser_title_, LV_ALIGN_TOP_LEFT, kBackWidth, 4);
    chooser_list_ = lv_obj_create(chooser_view_);
    lv_obj_remove_style_all(chooser_list_);
    lv_obj_set_style_bg_opa(chooser_list_, LV_OPA_TRANSP, 0);
    lv_obj_set_size(chooser_list_, content_width_, page_height_ - 40);
    lv_obj_align(chooser_list_, LV_ALIGN_TOP_LEFT, 0, 36);
    lv_obj_set_flex_flow(chooser_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(chooser_list_, 8, 0);
    lv_obj_set_scroll_dir(chooser_list_, LV_DIR_VER);

    // --- stop list
    stops_view_ = makeLayer(parent);
    lv_obj_set_style_pad_hor(stops_view_, kPad, 0);
    lv_obj_set_style_pad_top(stops_view_, kPad, 0);
    lv_obj_set_style_pad_bottom(stops_view_, kBottomSafe, 0);
    lv_obj_t *stops_back = makeIconButton(stops_view_, LV_SYMBOL_LEFT,
                                          dispatch<&BusApp::onBackPressed>, this);
    lv_obj_align(stops_back, LV_ALIGN_TOP_LEFT, -kPad + kEdgeInset, -8);

    stops_title_ = makeLabel(stops_view_, &lv_font_montserrat_16, kValue);
    lv_label_set_long_mode(stops_title_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(stops_title_, content_width_ - kBackWidth);
    lv_obj_align(stops_title_, LV_ALIGN_TOP_LEFT, kBackWidth, 6);

    nearest_button_ = lv_btn_create(stops_view_);
    lv_obj_set_size(nearest_button_, content_width_ - 2 * kEdgeInset, 44);
    lv_obj_align(nearest_button_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(nearest_button_, 10, 0);
    lv_obj_set_style_border_width(nearest_button_, 0, 0);
    lv_obj_set_style_shadow_width(nearest_button_, 0, 0);
    lv_obj_set_style_bg_color(nearest_button_, lv_color_hex(kCard), 0);
    lv_obj_add_event_cb(nearest_button_, dispatch<&BusApp::onNearestPressed>,
                        LV_EVENT_CLICKED, this);
    nearest_label_ = makeLabel(nearest_button_, &lv_font_montserrat_16, kValue,
                               "Nearest stop to me");
    lv_obj_center(nearest_label_);

    stops_list_ = lv_obj_create(stops_view_);
    lv_obj_remove_style_all(stops_list_);
    lv_obj_set_style_bg_opa(stops_list_, LV_OPA_TRANSP, 0);
    // 28 px of title above, then the 44 px nearest button plus its gap below.
    lv_obj_set_size(stops_list_, content_width_, page_height_ - 92);
    lv_obj_align(stops_list_, LV_ALIGN_TOP_LEFT, 0, 28);
    lv_obj_set_flex_flow(stops_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(stops_list_, 4, 0);
    lv_obj_set_scroll_dir(stops_list_, LV_DIR_VER);

    // --- board
    board_view_ = makeLayer(parent);
    buildBoard(board_view_);
}

void BusApp::buildBoard(lv_obj_t *parent)
{
    lv_obj_set_style_pad_hor(parent, kPad, 0);
    lv_obj_set_style_pad_top(parent, kPad, 0);
    lv_obj_set_style_pad_bottom(parent, kBottomSafe, 0);

    lv_obj_t *board_back = makeIconButton(parent, LV_SYMBOL_LEFT,
                                          dispatch<&BusApp::onBackPressed>, this);
    lv_obj_align(board_back, LV_ALIGN_TOP_LEFT, -kPad + kEdgeInset, -4);

    board_title_ = makeLabel(parent, &lv_font_montserrat_16, kCaption);
    lv_label_set_long_mode(board_title_, LV_LABEL_LONG_DOT);
    // Stops short of the two 40 px controls on the right and the back button on
    // the left, so a long destination ellipsises instead of running under them.
    lv_obj_set_width(board_title_, content_width_ - 96 - kBackWidth);
    lv_obj_align(board_title_, LV_ALIGN_TOP_LEFT, kBackWidth, 4);

    // Both controls take the edge inset, so neither sits in the gesture band.
    lv_obj_t *refresh = makeIconButton(parent, LV_SYMBOL_REFRESH,
                                       dispatch<&BusApp::onRefreshPressed>, this);
    lv_obj_align(refresh, LV_ALIGN_TOP_RIGHT, -kEdgeInset + kPad, -4);
    save_button_ = makeIconButton(parent, LV_SYMBOL_SAVE,
                                  dispatch<&BusApp::onSavePressed>, this);
    lv_obj_align(save_button_, LV_ALIGN_TOP_RIGHT, -kEdgeInset + kPad - 44, -4);
    save_glyph_ = lv_obj_get_child(save_button_, 0);

    board_stop_ = makeLabel(parent, &lv_font_montserrat_28, kValue);
    lv_label_set_long_mode(board_stop_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(board_stop_, LV_PCT(100));
    lv_obj_align(board_stop_, LV_ALIGN_TOP_LEFT, 0, 36);

    board_sequence_ = makeLabel(parent, &lv_font_montserrat_16, kCaption);
    lv_obj_align(board_sequence_, LV_ALIGN_TOP_LEFT, 0, 74);

    lv_obj_t *card = makeCard(parent);
    lv_obj_set_size(card, LV_PCT(100), 176);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 104);

    // Three rows, always present. Empty cells stay empty rather than being filled
    // with a placeholder: reserve space, never fill it.
    for (size_t i = 0; i < TRANSIT_MAX_ETA; ++i) {
        const lv_coord_t y = static_cast<lv_coord_t>(i * 48);
        board_minutes_[i] = makeLabel(card, &lv_font_montserrat_28, kValue);
        lv_obj_align(board_minutes_[i], LV_ALIGN_TOP_LEFT, 0, y);
        board_clock_[i] = makeLabel(card, &lv_font_montserrat_20, kCaption);
        lv_obj_align(board_clock_[i], LV_ALIGN_TOP_LEFT, 130, y + 6);
        board_remark_[i] = makeLabel(card, &lv_font_montserrat_16, kCaptionDim);
        lv_label_set_long_mode(board_remark_[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(board_remark_[i], 150);
        lv_obj_align(board_remark_[i], LV_ALIGN_TOP_LEFT, 224, y + 10);
    }

    board_empty_ = makeLabel(card, &lv_font_montserrat_20, kCaption,
                             "No departures scheduled");
    lv_obj_align(board_empty_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_flag(board_empty_, LV_OBJ_FLAG_HIDDEN);

    board_age_ = makeLabel(parent, &lv_font_montserrat_16, kCaption);
    lv_obj_align(board_age_, LV_ALIGN_TOP_LEFT, 0, 292);
}

void BusApp::buildNearbyTab(lv_obj_t *parent)
{
    lv_obj_t *page = makeLayer(parent);
    lv_obj_set_style_pad_hor(page, kPad, 0);
    lv_obj_set_style_pad_top(page, kPad, 0);
    lv_obj_set_style_pad_bottom(page, kBottomSafe, 0);

    // States it plainly. This tab is not "all stops near me": that needs a
    // searchable index of every stop in the territory, which is a ~1.5 MB
    // download this device has nowhere to put.
    lv_obj_t *header = makeLabel(page, &lv_font_montserrat_16, kCaption,
                                 "Your saved stops, nearest first.");
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);

    nearby_note_ = makeLabel(page, &lv_font_montserrat_16, kCaption);
    lv_label_set_long_mode(nearby_note_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(nearby_note_, LV_PCT(100));
    lv_obj_align(nearby_note_, LV_ALIGN_TOP_LEFT, 0, 40);

    nearby_list_ = lv_obj_create(page);
    lv_obj_remove_style_all(nearby_list_);
    lv_obj_set_style_bg_opa(nearby_list_, LV_OPA_TRANSP, 0);
    lv_obj_set_size(nearby_list_, content_width_, page_height_ - 36);
    lv_obj_align(nearby_list_, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_flex_flow(nearby_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(nearby_list_, 8, 0);
    lv_obj_set_scroll_dir(nearby_list_, LV_DIR_VER);
}

// ----------------------------------------------------------------- view switch

void BusApp::showView(View view)
{
    view_ = view;
    lv_obj_t *layers[] = {keypad_view_, chooser_view_, stops_view_, board_view_};
    for (size_t i = 0; i < 4; ++i) {
        if (layers[i] == nullptr) continue;
        if (i == static_cast<size_t>(view)) lv_obj_clear_flag(layers[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(layers[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (view == View::Board) renderBoard();
}

void BusApp::setRouteStatus(const char *text, bool warn)
{
    if (route_status_ == nullptr) return;
    lv_label_set_text(route_status_, text != nullptr ? text : "");
    lv_obj_set_style_text_color(route_status_, lv_color_hex(warn ? kWarn : kCaption), 0);
}

// -------------------------------------------------------------------- keypad

void BusApp::onKeyChar(char value)
{
    const size_t length = strlen(query_);
    if (length >= TRANSIT_ROUTE_NAME_LEN) return;
    query_[length] = value;
    query_[length + 1] = '\0';
    onQueryChanged();
}

void BusApp::onKeyBackspace()
{
    const size_t length = strlen(query_);
    if (length == 0) return;
    query_[length - 1] = '\0';
    onQueryChanged();
}

// Every path that edits the query lands here, so live search cannot be bypassed
// by adding a new way to type.
void BusApp::onQueryChanged()
{
    // 0 means "never edited", so a tick that genuinely reads 0 borrows 1. One
    // millisecond of debounce is not a difference anyone can perceive; a query that
    // silently never searches is.
    query_changed_at_ = lv_tick_get();
    if (query_changed_at_ == 0) query_changed_at_ = 1;
    // A query that has been edited is no longer the one that was searched, so the
    // next complete name searches again even if it is one seen before.
    searched_[0] = '\0';
    // An in-flight lookup for a prefix the user has already moved past is work
    // whose answer nobody will read.
    if (variants_request_ != 0) {
        transit_service_cancel(variants_request_);
        variants_request_ = 0;
    }
    renderRecents();
}

// The reference app fetches on a matching number rather than on ⏎. The delay is
// what makes that affordable: typing 2-6-4-M passes through 2, 26 and 264, and
// two of those are real routes. Firing on each would open three TLS sessions to
// answer a question the user was not asking yet.
void BusApp::maybeAutoSearch()
{
    // Only a query the user has edited in this session searches itself. loadState()
    // restores query_ from the saved variant, and with no keystroke behind it the
    // debounce below is satisfied from tick zero: the app dialled out ~3.9 s into
    // boot, before esp_netif_init() had run, and lwIP asserted. Opening the app is
    // not asking for a lookup.
    if (query_changed_at_ == 0) return;
    if (query_[0] == '\0' || variants_request_ != 0) return;
    if (view_ != View::Keypad) return;              // not while a result is showing
    if (strcmp(query_, searched_) == 0) return;     // already answered
    if (transit_route_is_complete(query_, strlen(query_)) == 0) return;
    if (lv_tick_elaps(query_changed_at_) < kAutoSearchMs) return;

    strlcpy(searched_, query_, sizeof(searched_));
    submitRoute();
}

void BusApp::onKeySubmit()
{
    submitRoute();
}

void BusApp::onRecentPicked(size_t index)
{
    if (index >= recent_count_) return;
    strlcpy(query_, recent_[index], sizeof(query_));
    onQueryChanged();
    // A recent is a deliberate pick, so it does not wait for the debounce. Marking
    // it searched keeps live search from submitting the same name a second time.
    strlcpy(searched_, query_, sizeof(searched_));
    submitRoute();
}

void BusApp::renderRecents()
{
    const size_t length = strlen(query_);
    if (route_display_ != nullptr) {
        lv_label_set_text(route_display_, length > 0 ? query_ : "Route");
        lv_obj_set_style_text_color(route_display_,
                                    lv_color_hex(length > 0 ? kValue : kCaptionDim), 0);
    }
    // The mask is advisory and costs one bsearch: no allocation, no network.
    keypad_.applyMask(transit_route_next_mask(query_, length),
                      transit_route_is_complete(query_, length) != 0, length > 0);

    if (recent_row_ == nullptr) return;
    lv_obj_clean(recent_row_);
    if (recent_count_ == 0) return;

    lv_obj_t *caption = makeLabel(recent_row_, &lv_font_montserrat_16, kCaptionDim, "Recent");
    lv_obj_set_style_pad_top(caption, 10, 0);
    for (size_t i = 0; i < recent_count_; ++i) {
        lv_obj_t *chip = lv_btn_create(recent_row_);
        lv_obj_set_size(chip, 54, 36);
        lv_obj_set_style_radius(chip, 8, 0);
        lv_obj_set_style_border_width(chip, 0, 0);
        lv_obj_set_style_shadow_width(chip, 0, 0);
        lv_obj_set_style_bg_color(chip, lv_color_hex(kCard), 0);
        lv_obj_set_user_data(chip, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(chip, dispatchIndex<&BusApp::onRecentPicked>,
                            LV_EVENT_CLICKED, this);
        lv_obj_t *label = makeLabel(chip, &lv_font_montserrat_16, kValue, recent_[i]);
        lv_obj_center(label);
    }
}

void BusApp::submitRoute()
{
    if (query_[0] == '\0') return;
    if (variants_request_ != 0) return;   // one lookup at a time

    variant_count_ = 0;
    have_variant_ = false;
    // Whatever asked for this lookup -- ⏎, a recent, or the debounce -- this name
    // is now the one being answered, so live search does not ask again for it.
    strlcpy(searched_, query_, sizeof(searched_));
    char message[64];
    snprintf(message, sizeof(message), "Looking up %s...", query_);
    setRouteStatus(message);
    variants_request_ = transit_service_find_variants(query_);
    if (variants_request_ == 0) setRouteStatus("Busy - try again in a moment", true);
}

// -------------------------------------------------------------------- chooser

void BusApp::renderChooser()
{
    if (chooser_list_ == nullptr) return;
    lv_obj_clean(chooser_list_);
    if (chooser_title_ != nullptr) {
        char title[48];
        snprintf(title, sizeof(title), "%s - choose a direction", query_);
        lv_label_set_text(chooser_title_, title);
    }

    for (size_t i = 0; i < variant_count_; ++i) {
        const transit_variant_t &variant = variants_[i];
        lv_obj_t *row = lv_btn_create(chooser_list_);
        lv_obj_set_size(row, LV_PCT(100), 64);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(kCard), 0);
        lv_obj_set_style_pad_all(row, 12, 0);
        lv_obj_set_user_data(row, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(row, dispatchIndex<&BusApp::onVariantPicked>,
                            LV_EVENT_CLICKED, this);

        char heading[80];
        snprintf(heading, sizeof(heading), "%s  %s", variant.route, operatorName(variant.op));
        lv_obj_t *top = makeLabel(row, &lv_font_montserrat_20, kValue, heading);
        lv_obj_align(top, LV_ALIGN_TOP_LEFT, 0, 0);

        char destination[96];
        snprintf(destination, sizeof(destination), "to %s",
                 variant.dest[0] != '\0' ? variant.dest : "terminus");
        lv_obj_t *bottom = makeLabel(row, &lv_font_montserrat_16, kCaption, destination);
        lv_label_set_long_mode(bottom, LV_LABEL_LONG_DOT);
        lv_obj_set_width(bottom, LV_PCT(100));
        lv_obj_align(bottom, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
}

void BusApp::onVariantPicked(size_t index)
{
    selectVariant(index);
}

void BusApp::selectVariant(size_t index)
{
    if (index >= variant_count_) return;
    variant_ = variants_[index];
    have_variant_ = true;
    pushRecent(variant_.route);
    releaseRoute();

    if (stops_title_ != nullptr) {
        char title[140];
        snprintf(title, sizeof(title), "%s  %s  to %s", variant_.route,
                 operatorName(variant_.op),
                 variant_.dest[0] != '\0' ? variant_.dest : "terminus");
        lv_label_set_text(stops_title_, title);
    }
    if (stops_list_ != nullptr) {
        lv_obj_clean(stops_list_);
        lv_obj_t *loading = makeLabel(stops_list_, &lv_font_montserrat_16, kCaption,
                                      "Loading stops...");
        (void)loading;
    }
    showView(View::Stops);
    stops_request_ = transit_service_load_stops(&variant_);
    if (stops_request_ == 0) setRouteStatus("Busy - try again in a moment", true);
}

// ------------------------------------------------------------------ stop list

void BusApp::renderStops()
{
    if (stops_list_ == nullptr) return;
    lv_obj_clean(stops_list_);
    const size_t count = transit_route_stop_count(route_);
    if (count == 0) {
        (void)makeLabel(stops_list_, &lv_font_montserrat_16, kCaption,
                        "No stops listed for this route.");
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        lv_obj_t *row = lv_btn_create(stops_list_);
        lv_obj_set_size(row, LV_PCT(100), 44);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_20, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xC9D6E2), LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_user_data(row, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(row, dispatchIndex<&BusApp::onStopPicked>, LV_EVENT_CLICKED, this);

        const transit_stop_t *stop = transit_route_stop(route_, i);
        char sequence[8];
        snprintf(sequence, sizeof(sequence), "%u",
                 static_cast<unsigned>(stop != nullptr ? stop->seq : i + 1));
        // The sequence number is information the device has instantly, and it
        // holds the row's identity so a name landing later moves nothing.
        lv_obj_t *number = makeLabel(row, &lv_font_montserrat_16, kCaptionDim, sequence);
        lv_obj_set_width(number, 34);
        lv_obj_set_style_text_align(number, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(number, LV_ALIGN_LEFT_MID, 0, 0);

        // A clear gap between the number and the name. Two labels butted together
        // is exactly how "68X" and a stop name end up looking like one word.
        lv_obj_t *name = makeLabel(row, &lv_font_montserrat_16, kValue);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        // 46 px of number column plus its gap, so the name never touches it.
        lv_obj_set_width(name, content_width_ - 58);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 46, 0);

        renderStopRow(i);
    }
    names_requested_upto_ = 0;
    requestVisibleNames();
}

void BusApp::renderStopRow(size_t index)
{
    if (stops_list_ == nullptr) return;
    lv_obj_t *row = lv_obj_get_child(stops_list_, static_cast<int32_t>(index));
    if (row == nullptr) return;
    lv_obj_t *name = lv_obj_get_child(row, 1);
    if (name == nullptr) return;

    const transit_stop_t *stop = transit_route_stop(route_, index);
    if (stop == nullptr) return;
    if (stop->name[0] != '\0') {
        lv_label_set_text(name, stop->name);
        lv_obj_set_style_text_color(name, lv_color_hex(kValue), 0);
    } else {
        // No row is ever blank. A quiet placeholder until the name lands.
        char placeholder[24];
        snprintf(placeholder, sizeof(placeholder), "Stop %u", static_cast<unsigned>(stop->seq));
        lv_label_set_text(name, placeholder);
        lv_obj_set_style_text_color(name, lv_color_hex(kCaptionDim), 0);
    }
}

// Resolves names for the visible window plus a look-ahead, in bounded batches.
void BusApp::requestVisibleNames()
{
    if (route_ == nullptr || names_request_ != 0) return;
    const size_t count = transit_route_stop_count(route_);
    if (count == 0) return;

    // Find the first unresolved stop at or after what has already been asked for.
    size_t first = count;
    for (size_t i = names_requested_upto_; i < count; ++i) {
        const transit_stop_t *stop = transit_route_stop(route_, i);
        if (stop != nullptr && stop->name[0] == '\0') { first = i; break; }
    }
    if (first >= count) return;

    names_request_ = transit_service_load_stop_names(route_, first, TRANSIT_BATCH_MAX);
    if (names_request_ != 0) names_requested_upto_ = first;
}

void BusApp::onStopPicked(size_t index)
{
    selectStop(index);
}

void BusApp::selectStop(size_t index)
{
    const transit_stop_t *stop = transit_route_stop(route_, index);
    if (stop == nullptr) return;
    stop_index_ = index;
    strlcpy(stop_id_, stop->stop_id, sizeof(stop_id_));
    strlcpy(stop_name_, stop->name, sizeof(stop_name_));

    eta_count_ = 0;
    eta_fetched_at_ = 0;
    eta_failed_ = false;
    eta_empty_ = false;
    showView(View::Board);
    requestEta(true);
}

void BusApp::onNearestPressed()
{
    if (nearest_running_) { onNearestCancel(); return; }
    if (route_ == nullptr) return;

    double latitude = 0, longitude = 0;
    if (!crystal_location_get(&latitude, &longitude, nullptr, 0)) {
        if (nearest_label_ != nullptr) {
            lv_label_set_text(nearest_label_, "Set your location in Settings > Region & Time");
        }
        return;
    }
    nearest_request_ = transit_service_find_nearest(route_, latitude, longitude);
    if (nearest_request_ == 0) return;
    nearest_running_ = true;
    if (nearest_label_ != nullptr) lv_label_set_text(nearest_label_, "Checking stops...");
}

void BusApp::onNearestCancel()
{
    if (nearest_request_ != 0) transit_service_cancel(nearest_request_);
    nearest_request_ = 0;
    nearest_running_ = false;
    if (nearest_label_ != nullptr) lv_label_set_text(nearest_label_, "Nearest stop to me");
}

// ---------------------------------------------------------------------- board

void BusApp::requestEta(bool force)
{
    if (!have_variant_ || stop_id_[0] == '\0') return;
    if (eta_request_ != 0) return;
    if (!force && lv_tick_elaps(last_eta_refresh_) < kAutoRefreshMs) return;

    eta_request_ = transit_service_load_eta(&variant_, stop_id_);
    if (eta_request_ == 0) return;
    eta_pending_ = true;
    last_eta_refresh_ = lv_tick_get();
    renderBoard();
}

void BusApp::onRefreshPressed()
{
    // Manual refresh ignores the throttle, but not an in-flight request: a second
    // one would reset the clock and make the first look like it never expired.
    requestEta(true);
}

void BusApp::renderBoard()
{
    if (board_stop_ == nullptr) return;

    if (board_title_ != nullptr) {
        char title[140];
        snprintf(title, sizeof(title), "%s to %s", variant_.route,
                 variant_.dest[0] != '\0' ? variant_.dest : "terminus");
        lv_label_set_text(board_title_, title);
    }
    lv_label_set_text(board_stop_, stop_name_[0] != '\0' ? stop_name_ : "Selected stop");

    if (board_sequence_ != nullptr) {
        const size_t total = transit_route_stop_count(route_);
        const transit_stop_t *stop = transit_route_stop(route_, stop_index_);
        char text[48];
        if (stop != nullptr && total > 0) {
            snprintf(text, sizeof(text), "Stop %u of %u",
                     static_cast<unsigned>(stop->seq), static_cast<unsigned>(total));
        } else {
            text[0] = '\0';
        }
        lv_label_set_text(board_sequence_, text);
    }

    if (save_glyph_ != nullptr) {
        const bool saved = isSaved(stop_id_);
        lv_obj_set_style_text_color(save_glyph_,
                                    lv_color_hex(saved ? kAccent : 0x8FA3B7), 0);
    }

    const int32_t now = nowEpoch();
    const bool age_known = eta_fetched_at_ >= kMinPlausibleEpoch && clockIsSet() &&
                           now >= eta_fetched_at_;
    const int32_t age = age_known ? now - eta_fetched_at_ : -1;
    // Past five minutes the numbers stop looking authoritative; past thirty the
    // minute counts are withdrawn entirely, because a 30-minute-old "3 min" is
    // not stale, it is wrong.
    const bool dimmed = !age_known || age > kDimAfterSeconds;
    const bool withdraw_minutes = age_known && age > kWithdrawAfterSeconds;

    const bool have_rows = eta_count_ > 0;
    if (board_empty_ != nullptr) {
        if (have_rows || eta_pending_) lv_obj_add_flag(board_empty_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(board_empty_, LV_OBJ_FLAG_HIDDEN);
    }

    for (size_t i = 0; i < TRANSIT_MAX_ETA; ++i) {
        if (board_minutes_[i] == nullptr) continue;
        if (i >= eta_count_) {
            // Empty cells stay empty.
            lv_label_set_text(board_minutes_[i], "");
            lv_label_set_text(board_clock_[i], "");
            lv_label_set_text(board_remark_[i], "");
            continue;
        }

        char minutes[16] = "";
        if (!withdraw_minutes) {
            const int32_t seconds = eta_[i].eta_epoch - now;
            if (clockIsSet()) formatMinutes(seconds > 0 ? seconds : 0, minutes, sizeof(minutes));
        }
        lv_label_set_text(board_minutes_[i], minutes);

        // The clock time is what survives a stale reading: if the refresh failed,
        // 14:32 is still checkable while "3 min" has quietly become a lie. Hidden
        // only when the device has no clock to render it from.
        char clock[16] = "";
        if (clockIsSet()) formatClock(eta_[i].eta_epoch, clock, sizeof(clock));
        lv_label_set_text(board_clock_[i], clock);
        lv_label_set_text(board_remark_[i], eta_[i].remark);

        // Values and captions dim together, so a caption never ends up brighter
        // than the number it labels.
        lv_obj_set_style_text_color(board_minutes_[i], lv_color_hex(dimmed ? kValueDim : kValue), 0);
        lv_obj_set_style_text_color(board_clock_[i], lv_color_hex(dimmed ? kCaptionDim : kCaption), 0);
    }

    if (board_age_ == nullptr) return;
    char line[120];
    char age_text[64] = "";
    if (age >= 0) formatAge(age, age_text, sizeof(age_text));

    if (eta_pending_ && !have_rows) {
        snprintf(line, sizeof(line), "Checking departures...");
    } else if (eta_pending_) {
        snprintf(line, sizeof(line), "Refreshing%s%s", age_text[0] != '\0' ? " - " : "", age_text);
    } else if (eta_failed_) {
        // Never blank a good reading to report a bad request.
        snprintf(line, sizeof(line), "Couldn't refresh%s%s",
                 age_text[0] != '\0' ? " - showing " : "", age_text[0] != '\0' ? age_text : "");
    } else if (!clockIsSet()) {
        snprintf(line, sizeof(line), "Updated at an unknown time");
    } else if (withdraw_minutes) {
        snprintf(line, sizeof(line), "%s - too old to count down", age_text);
    } else if (dimmed && age >= 0) {
        snprintf(line, sizeof(line), "%s - may be out of date", age_text);
    } else if (age_text[0] != '\0') {
        snprintf(line, sizeof(line), "%s", age_text);
    } else {
        line[0] = '\0';
    }
    lv_label_set_text(board_age_, line);
    lv_obj_set_style_text_color(board_age_,
                                lv_color_hex(eta_failed_ || dimmed ? kWarn : kCaption), 0);
}

void BusApp::onSavePressed()
{
    if (stop_id_[0] == '\0' || !have_variant_) return;

    // Already saved: this un-saves it.
    for (size_t i = 0; i < saved_count_; ++i) {
        if (strcmp(saved_[i].stop_id, stop_id_) == 0) {
            for (size_t j = i; j + 1 < saved_count_; ++j) saved_[j] = saved_[j + 1];
            --saved_count_;
            saveSavedStops();
            renderSaved();
            renderNearby();
            renderBoard();
            return;
        }
    }

    if (saved_count_ >= kMaxSaved) {
        // Every saved stop is one ETA request per refresh, so the cap is a network
        // budget as much as a layout one.
        if (board_age_ != nullptr) {
            lv_label_set_text(board_age_, "Saved stops full - remove one first");
            lv_obj_set_style_text_color(board_age_, lv_color_hex(kWarn), 0);
        }
        return;
    }

    SavedStop &entry = saved_[saved_count_++];
    memset(&entry, 0, sizeof(entry));
    strlcpy(entry.route, variant_.route, sizeof(entry.route));
    entry.op = variant_.op;
    entry.bound = variant_.bound;
    entry.service_type = variant_.service_type;
    strlcpy(entry.stop_id, stop_id_, sizeof(entry.stop_id));
    strlcpy(entry.stop_name, stop_name_, sizeof(entry.stop_name));
    strlcpy(entry.dest, variant_.dest, sizeof(entry.dest));
    const transit_stop_t *stop = transit_route_stop(route_, stop_index_);
    if (stop != nullptr) { entry.lat = stop->lat; entry.lon = stop->lon; }
    for (size_t i = 0; i < eta_count_ && i < TRANSIT_MAX_ETA; ++i) entry.eta[i] = eta_[i].eta_epoch;
    entry.fetched_at = eta_fetched_at_;

    saveSavedStops();
    renderSaved();
    renderNearby();
    renderBoard();
}

bool BusApp::isSaved(const char *stop_id) const
{
    if (stop_id == nullptr || stop_id[0] == '\0') return false;
    for (size_t i = 0; i < saved_count_; ++i) {
        if (strcmp(saved_[i].stop_id, stop_id) == 0) return true;
    }
    return false;
}

// ---------------------------------------------------------------------- saved

namespace {
// One saved card: route and destination, stop name, then the ETAs. Built the same
// way in both the Saved and Nearby tabs so the two read as one design.
lv_obj_t *buildSavedCard(lv_obj_t *parent, lv_coord_t width, const char *route,
                         const char *dest, const char *stop_name, const char *etas,
                         const char *trailer, bool dim)
{
    lv_obj_t *card = makeCard(parent);
    lv_obj_set_size(card, width, 96);
    const lv_coord_t inner = width - 24;   // the card's own 12 px padding
    lv_obj_set_style_pad_all(card, 12, 0);

    lv_obj_t *number = makeLabel(card, &lv_font_montserrat_28, kValue, route);
    lv_obj_align(number, LV_ALIGN_TOP_LEFT, 0, 0);

    // Placed relative to the route number rather than at a fixed offset, so "1"
    // and "968A" both leave the same visible gap before the destination.
    char destination[96];
    snprintf(destination, sizeof(destination), "to %s", dest != nullptr && dest[0] != '\0' ? dest : "terminus");
    lv_obj_t *to = makeLabel(card, &lv_font_montserrat_16, kCaption, destination);
    lv_label_set_long_mode(to, LV_LABEL_LONG_DOT);
    lv_obj_set_width(to, inner - 90);
    lv_obj_align_to(to, number, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, -6);

    lv_obj_t *name = makeLabel(card, &lv_font_montserrat_16, kValue,
                              stop_name != nullptr && stop_name[0] != '\0' ? stop_name : "Saved stop");
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, inner);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 38);

    lv_obj_t *times = makeLabel(card, &lv_font_montserrat_20, dim ? kValueDim : kValue, etas);
    lv_obj_align(times, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    if (trailer != nullptr && trailer[0] != '\0') {
        lv_obj_t *note = makeLabel(card, &lv_font_montserrat_16, kCaptionDim, trailer);
        lv_obj_align(note, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }
    return card;
}

// "3 min - 11 min - 24 min", or a word when there is nothing to count.
void formatSavedEtas(const int32_t *etas, size_t capacity, int32_t fetched_at,
                     char *out, size_t out_size)
{
    const int32_t now = nowEpoch();
    const bool age_known = fetched_at >= kMinPlausibleEpoch && clockIsSet() && now >= fetched_at;
    const bool withdraw = age_known && now - fetched_at > kWithdrawAfterSeconds;

    out[0] = '\0';
    size_t used = 0;
    for (size_t i = 0; i < capacity; ++i) {
        if (etas[i] == 0) continue;
        char cell[16];
        if (withdraw || !clockIsSet()) formatClock(etas[i], cell, sizeof(cell));
        else {
            const int32_t seconds = etas[i] - now;
            formatMinutes(seconds > 0 ? seconds : 0, cell, sizeof(cell));
        }
        const int written = snprintf(out + used, out_size - used, "%s%s",
                                     used > 0 ? "  -  " : "", cell);
        if (written <= 0) break;
        used += static_cast<size_t>(written);
        if (used >= out_size) break;
    }
    if (out[0] == '\0') snprintf(out, out_size, "No departures");
}
}  // namespace

void BusApp::renderSaved()
{
    if (saved_list_ == nullptr) return;
    lv_obj_clean(saved_list_);

    if (saved_count_ == 0) {
        if (saved_empty_ != nullptr) lv_obj_clear_flag(saved_empty_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(saved_list_, LV_OBJ_FLAG_HIDDEN);
        if (saved_age_ != nullptr) lv_label_set_text(saved_age_, "");
        return;
    }
    if (saved_empty_ != nullptr) lv_obj_add_flag(saved_empty_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(saved_list_, LV_OBJ_FLAG_HIDDEN);

    int32_t oldest = 0;
    for (size_t i = 0; i < saved_count_; ++i) {
        const SavedStop &entry = saved_[i];
        char etas[80];
        formatSavedEtas(entry.eta, TRANSIT_MAX_ETA, entry.fetched_at, etas, sizeof(etas));
        const int32_t now = nowEpoch();
        const bool age_known = entry.fetched_at >= kMinPlausibleEpoch && clockIsSet() &&
                               now >= entry.fetched_at;
        const bool dim = !age_known || now - entry.fetched_at > kDimAfterSeconds;
        if (entry.fetched_at != 0 && (oldest == 0 || entry.fetched_at < oldest)) {
            oldest = entry.fetched_at;
        }

        lv_obj_t *card = buildSavedCard(saved_list_, content_width_, entry.route, entry.dest,
                                        entry.stop_name, etas, operatorName(entry.op), dim);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(card, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(card, dispatchIndex<&BusApp::onSavedPicked>, LV_EVENT_CLICKED, this);
        // Long-press offers Remove. A saved stop is never removed silently -- the
        // user saved it.
        lv_obj_add_event_cb(card, dispatchIndex<&BusApp::onSavedRemove>, LV_EVENT_LONG_PRESSED, this);
    }

    if (saved_age_ != nullptr) {
        char line[80] = "";
        const int32_t now = nowEpoch();
        if (oldest != 0 && clockIsSet() && now >= oldest) formatAge(now - oldest, line, sizeof(line));
        else if (!clockIsSet()) snprintf(line, sizeof(line), "Updated at an unknown time");
        lv_label_set_text(saved_age_, line);
    }
}

void BusApp::onSavedPicked(size_t index)
{
    if (index >= saved_count_) return;
    const SavedStop &entry = saved_[index];

    // Rebuild just enough variant identity to ask for an ETA. The stop list is
    // not needed for a board, so opening a saved stop costs one request.
    memset(&variant_, 0, sizeof(variant_));
    strlcpy(variant_.route, entry.route, sizeof(variant_.route));
    variant_.op = entry.op;
    variant_.bound = entry.bound;
    variant_.service_type = entry.service_type;
    strlcpy(variant_.dest, entry.dest, sizeof(variant_.dest));
    have_variant_ = true;

    strlcpy(stop_id_, entry.stop_id, sizeof(stop_id_));
    strlcpy(stop_name_, entry.stop_name, sizeof(stop_name_));
    releaseRoute();
    stop_index_ = 0;

    // Paint the cached ETAs immediately, then refresh behind them.
    eta_count_ = 0;
    for (size_t i = 0; i < TRANSIT_MAX_ETA; ++i) {
        if (entry.eta[i] == 0) continue;
        eta_[eta_count_].eta_epoch = entry.eta[i];
        eta_[eta_count_].remark[0] = '\0';
        ++eta_count_;
    }
    eta_fetched_at_ = entry.fetched_at;
    eta_failed_ = false;
    eta_empty_ = false;

    lv_tabview_set_act(tabs_, 1, LV_ANIM_OFF);
    showView(View::Board);
    requestEta(true);
}

void BusApp::onSavedRemove(size_t index)
{
    if (index >= saved_count_) return;
    for (size_t i = index; i + 1 < saved_count_; ++i) saved_[i] = saved_[i + 1];
    --saved_count_;
    saveSavedStops();
    renderSaved();
    renderNearby();
    renderBoard();
}

// --------------------------------------------------------------------- nearby

void BusApp::renderNearby()
{
    if (nearby_list_ == nullptr) return;
    lv_obj_clean(nearby_list_);

    double latitude = 0, longitude = 0;
    const bool have_location = crystal_location_get(&latitude, &longitude, nullptr, 0);

    if (nearby_note_ != nullptr) {
        if (saved_count_ == 0) {
            lv_label_set_text(nearby_note_,
                              "No saved stops yet. Find a route to get started.");
        } else if (!have_location) {
            lv_label_set_text(nearby_note_,
                              "Set your location in Settings > Region & Time to sort by distance.");
        } else {
            lv_label_set_text(nearby_note_, "");
        }
    }
    if (saved_count_ == 0) return;

    // Index order by distance. Stops with no coordinates sort last rather than
    // being dropped: they are still saved stops.
    size_t order[kMaxSaved];
    double distance[kMaxSaved];
    for (size_t i = 0; i < saved_count_; ++i) {
        order[i] = i;
        const bool has_coords = saved_[i].lat != 0.0f || saved_[i].lon != 0.0f;
        distance[i] = (have_location && has_coords)
                    ? haversineMetres(latitude, longitude, saved_[i].lat, saved_[i].lon)
                    : 1e12;
    }
    for (size_t i = 1; i < saved_count_; ++i) {
        for (size_t j = i; j > 0 && distance[order[j - 1]] > distance[order[j]]; --j) {
            const size_t swap = order[j - 1];
            order[j - 1] = order[j];
            order[j] = swap;
        }
    }

    for (size_t i = 0; i < saved_count_; ++i) {
        const SavedStop &entry = saved_[order[i]];
        char etas[80];
        formatSavedEtas(entry.eta, TRANSIT_MAX_ETA, entry.fetched_at, etas, sizeof(etas));
        char trailer[24] = "";
        if (distance[order[i]] < 1e11) formatDistance(distance[order[i]], trailer, sizeof(trailer));

        lv_obj_t *card = buildSavedCard(nearby_list_, content_width_, entry.route, entry.dest,
                                        entry.stop_name, etas, trailer, false);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(card, reinterpret_cast<void *>(static_cast<uintptr_t>(order[i])));
        lv_obj_add_event_cb(card, dispatchIndex<&BusApp::onSavedPicked>, LV_EVENT_CLICKED, this);
    }
}

void BusApp::onTabChanged()
{
    // Leaving the Route tab does not tear down its stack, so coming back lands
    // where the user left off.
    if (tabs_ == nullptr) return;
    const uint16_t active = lv_tabview_get_tab_act(tabs_);
    if (active == 0) {
        renderSaved();
    } else if (active == 2) {
        if (nearby_list_ == nullptr && nearby_tab_ != nullptr) buildNearbyTab(nearby_tab_);
        renderNearby();
    }
}

// ------------------------------------------------------------- service results

void BusApp::handleEvent(const transit_event_t *event)
{
    if (event == nullptr || !isLive(this) || root_ == nullptr) return;

    switch (event->kind) {
    case TRANSIT_REQ_VARIANTS: {
        // A stale id is dropped silently. It is not an error, it is a race the
        // design permits.
        if (event->request_id != variants_request_) return;
        variants_request_ = 0;
        if (event->status == TRANSIT_STATUS_OK) {
            variant_count_ = event->data.variants.count;
            for (size_t i = 0; i < variant_count_; ++i) variants_[i] = event->data.variants.items[i];
            setRouteStatus("");
            if (variant_count_ == 1) selectVariant(0);
            else { renderChooser(); showView(View::Chooser); }
        } else if (event->status == TRANSIT_STATUS_EMPTY) {
            // The query stays on screen so it can be edited rather than retyped.
            char message[80];
            snprintf(message, sizeof(message), "Route %s not found. Check and try again.", query_);
            setRouteStatus(message, true);
        } else if (event->status == TRANSIT_STATUS_OFFLINE) {
            setRouteStatus("No connection - check WiFi and try again", true);
        } else if (event->status == TRANSIT_STATUS_CANCELLED) {
            setRouteStatus("");
        } else {
            setRouteStatus("Couldn't reach the transit service", true);
        }
        break;
    }

    case TRANSIT_REQ_STOPS: {
        if (event->request_id != stops_request_) {
            // Ownership still transferred, so a dropped result must be released or
            // it leaks the whole route.
            if (event->data.stops.route != nullptr) transit_route_release(event->data.stops.route);
            return;
        }
        stops_request_ = 0;
        if (event->status == TRANSIT_STATUS_OK) {
            releaseRoute();
            route_ = event->data.stops.route;
            renderStops();
        } else {
            if (event->data.stops.route != nullptr) transit_route_release(event->data.stops.route);
            if (stops_list_ != nullptr) {
                lv_obj_clean(stops_list_);
                const char *message =
                    event->status == TRANSIT_STATUS_EMPTY ? "No stops listed for this route." :
                    event->status == TRANSIT_STATUS_OFFLINE ? "No connection - check WiFi." :
                    "Couldn't load this route.";
                (void)makeLabel(stops_list_, &lv_font_montserrat_16, kCaption, message);
            }
        }
        break;
    }

    case TRANSIT_REQ_STOP_NAMES: {
        if (event->request_id != names_request_) return;
        // Progress ticks keep the request open: one connection is resolving the
        // whole route and these are the names that have landed so far.
        if (!event->data.names.final) {
            const size_t end = event->data.names.first_index + event->data.names.count;
            for (size_t i = event->data.names.first_index; i < end; ++i) renderStopRow(i);
            names_requested_upto_ = end;
            return;
        }
        names_request_ = 0;
        if (event->status == TRANSIT_STATUS_OK) {
            // Names replace their placeholder in place; nothing reflows.
            const size_t end = event->data.names.first_index + event->data.names.count;
            for (size_t i = event->data.names.first_index; i < end; ++i) renderStopRow(i);
            names_requested_upto_ = end;
            // Refresh the selected stop's name if it just landed.
            const transit_stop_t *stop = transit_route_stop(route_, stop_index_);
            if (stop != nullptr && strcmp(stop->stop_id, stop_id_) == 0 && stop->name[0] != '\0') {
                strlcpy(stop_name_, stop->name, sizeof(stop_name_));
                if (view_ == View::Board) renderBoard();
            }
            requestVisibleNames();   // continue with the next batch
        }
        break;
    }

    case TRANSIT_REQ_ETA: {
        if (event->request_id == saved_eta_request_) {
            // A background refresh of one saved card.
            saved_eta_request_ = 0;
            if (event->status == TRANSIT_STATUS_OK && saved_eta_target_ < saved_count_) {
                SavedStop &entry = saved_[saved_eta_target_];
                if (strcmp(entry.stop_id, event->data.eta.stop_id) == 0) {
                    for (size_t i = 0; i < TRANSIT_MAX_ETA; ++i) {
                        entry.eta[i] = i < event->data.eta.count
                                     ? event->data.eta.items[i].eta_epoch : 0;
                    }
                    entry.fetched_at = event->data.eta.fetched_at;
                    saveSavedStops();
                    renderSaved();
                    renderNearby();
                }
            }
            return;
        }
        if (event->request_id != eta_request_) return;
        eta_request_ = 0;
        eta_pending_ = false;
        if (event->status == TRANSIT_STATUS_OK) {
            eta_failed_ = false;
            eta_empty_ = false;
            eta_count_ = event->data.eta.count;
            for (size_t i = 0; i < eta_count_; ++i) eta_[i] = event->data.eta.items[i];
            eta_fetched_at_ = event->data.eta.fetched_at;

            // Keep a saved copy current, so Saved paints before any request next
            // time the app opens.
            for (size_t i = 0; i < saved_count_; ++i) {
                if (strcmp(saved_[i].stop_id, stop_id_) != 0) continue;
                for (size_t j = 0; j < TRANSIT_MAX_ETA; ++j) {
                    saved_[i].eta[j] = j < eta_count_ ? eta_[j].eta_epoch : 0;
                }
                saved_[i].fetched_at = eta_fetched_at_;
                saveSavedStops();
                break;
            }
        } else if (event->status == TRANSIT_STATUS_EMPTY) {
            eta_failed_ = false;
            eta_empty_ = true;
            eta_count_ = 0;
            eta_fetched_at_ = event->data.eta.fetched_at != 0 ? event->data.eta.fetched_at : nowEpoch();
        } else if (event->status != TRANSIT_STATUS_CANCELLED) {
            // Keep the previous reading and its age; never blank a good reading to
            // report a bad request.
            eta_failed_ = true;
        }
        renderBoard();
        break;
    }

    case TRANSIT_REQ_NEAREST: {
        if (event->request_id != nearest_request_) return;
        if (!event->data.nearest.final) {
            if (nearest_label_ != nullptr) {
                char line[64];
                snprintf(line, sizeof(line), "Checking stops... %u of %u  (tap to cancel)",
                         static_cast<unsigned>(event->data.nearest.resolved),
                         static_cast<unsigned>(event->data.nearest.total));
                lv_label_set_text(nearest_label_, line);
            }
            return;
        }
        nearest_request_ = 0;
        nearest_running_ = false;
        if (nearest_label_ != nullptr) lv_label_set_text(nearest_label_, "Nearest stop to me");
        if (event->status == TRANSIT_STATUS_OK) {
            const size_t index = event->data.nearest.nearest_index;
            for (size_t i = 0; i < transit_route_stop_count(route_); ++i) renderStopRow(i);
            if (stops_list_ != nullptr) {
                lv_obj_t *row = lv_obj_get_child(stops_list_, static_cast<int32_t>(index));
                if (row != nullptr) lv_obj_scroll_to_view(row, LV_ANIM_ON);
            }
            selectStop(index);
        } else if (event->status != TRANSIT_STATUS_CANCELLED && nearest_label_ != nullptr) {
            lv_label_set_text(nearest_label_, "Couldn't check stops - try again");
        }
        break;
    }
    }
}

void BusApp::tick()
{
    if (!isLive(this) || root_ == nullptr) return;

    // The service owns no lv_timer of its own, so this drain is the only place its
    // results cross onto the LVGL task. It calls back into handleEvent() through
    // the listener installed in onCreate().
    (void)transit_service_dispatch();

    maybeAutoSearch();

    // Everything below is per-second work. The tick runs at kTickMs so live search
    // resolves promptly; refresh scheduling and age lines do not want that rate,
    // and renderSaved() at 5 Hz would rebuild eight cards for nothing.
    const uint32_t now_tick = lv_tick_get();
    if (last_slow_tick_ != 0 && lv_tick_elaps(last_slow_tick_) < 1000) return;
    last_slow_tick_ = now_tick;

    const bool board_visible = tabs_ != nullptr && lv_tabview_get_tab_act(tabs_) == 1 &&
                               view_ == View::Board;
    if (board_visible) {
        // Once on open, then every 30 s while the board is the visible tab. Never
        // on a tighter timer, and never while paused.
        if (last_eta_refresh_ == 0) requestEta(true);
        else requestEta(false);
        updateAges();
    }

    // Saved cards refresh one at a time, round-robin, so eight saved stops do not
    // become eight simultaneous TLS sessions.
    const bool saved_visible = tabs_ != nullptr && lv_tabview_get_tab_act(tabs_) == 0;
    if (saved_visible && saved_count_ > 0 && saved_eta_request_ == 0) {
        const int32_t now = nowEpoch();
        for (size_t attempt = 0; attempt < saved_count_; ++attempt) {
            const size_t index = (saved_refresh_cursor_ + attempt) % saved_count_;
            const SavedStop &entry = saved_[index];
            const bool stale = entry.fetched_at == 0 || !clockIsSet() ||
                               now - entry.fetched_at >= static_cast<int32_t>(kAutoRefreshMs / 1000);
            if (!stale) continue;
            transit_variant_t variant = {};
            strlcpy(variant.route, entry.route, sizeof(variant.route));
            variant.op = entry.op;
            variant.bound = entry.bound;
            variant.service_type = entry.service_type;
            saved_eta_request_ = transit_service_load_eta(&variant, entry.stop_id);
            if (saved_eta_request_ != 0) {
                saved_eta_target_ = index;
                saved_refresh_cursor_ = (index + 1) % saved_count_;
            }
            break;
        }
        renderSaved();
    }
}

void BusApp::updateAges()
{
    if (view_ == View::Board) renderBoard();
}

void BusApp::releaseRoute()
{
    if (route_ == nullptr) return;
    // Cancel first: the worker may be mid-batch on this handle, and release waits
    // for it to let go.
    names_request_ = 0;
    nearest_request_ = 0;
    nearest_running_ = false;
    transit_route_release(route_);
    route_ = nullptr;
    names_requested_upto_ = 0;
}

// ----------------------------------------------------------------- persistence

void BusApp::loadState()
{
    size_t length = sizeof(saved_);
    if (state().get(kKeySaved, saved_, &length) && length % sizeof(SavedStop) == 0) {
        saved_count_ = length / sizeof(SavedStop);
        if (saved_count_ > kMaxSaved) saved_count_ = kMaxSaved;
    }

    char recent[kMaxRecent][TRANSIT_ROUTE_NAME_LEN + 1] = {};
    length = sizeof(recent);
    if (state().get(kKeyRecent, recent, &length) && length % sizeof(recent[0]) == 0) {
        recent_count_ = length / sizeof(recent[0]);
        if (recent_count_ > kMaxRecent) recent_count_ = kMaxRecent;
        for (size_t i = 0; i < recent_count_; ++i) {
            recent[i][TRANSIT_ROUTE_NAME_LEN] = '\0';
            strlcpy(recent_[i], recent[i], sizeof(recent_[i]));
        }
    }

    struct Navigation {
        transit_variant_t variant;
        char              stop_id[TRANSIT_STOP_ID_LEN];
        char              stop_name[TRANSIT_NAME_LEN];
        uint8_t           view;
        bool              have_variant;
    } navigation = {};
    length = sizeof(navigation);
    if (state().get(kKeyNav, &navigation, &length) && length == sizeof(navigation)) {
        variant_ = navigation.variant;
        have_variant_ = navigation.have_variant;
        strlcpy(stop_id_, navigation.stop_id, sizeof(stop_id_));
        strlcpy(stop_name_, navigation.stop_name, sizeof(stop_name_));
        // The route handle is not persisted -- 15 KB of stop ids is not state, it
        // is a cache -- so a restored board goes back to the keypad behind it.
        view_ = (navigation.view == static_cast<uint8_t>(View::Board) && have_variant_ &&
                 stop_id_[0] != '\0') ? View::Board : View::Keypad;
        if (have_variant_) strlcpy(query_, variant_.route, sizeof(query_));
    }
}

void BusApp::saveNavigation()
{
    struct Navigation {
        transit_variant_t variant;
        char              stop_id[TRANSIT_STOP_ID_LEN];
        char              stop_name[TRANSIT_NAME_LEN];
        uint8_t           view;
        bool              have_variant;
    } navigation = {};
    navigation.variant = variant_;
    strlcpy(navigation.stop_id, stop_id_, sizeof(navigation.stop_id));
    strlcpy(navigation.stop_name, stop_name_, sizeof(navigation.stop_name));
    navigation.view = static_cast<uint8_t>(view_);
    navigation.have_variant = have_variant_;
    (void)state().set(kKeyNav, &navigation, sizeof(navigation));

    if (recent_count_ > 0) {
        (void)state().set(kKeyRecent, recent_, recent_count_ * sizeof(recent_[0]));
    }
}

void BusApp::saveSavedStops()
{
    if (saved_count_ == 0) {
        (void)state().erase(kKeySaved);
        return;
    }
    // 8 x sizeof(SavedStop) stays inside the 2,048-byte CrystalState value cap.
    if (!state().set(kKeySaved, saved_, saved_count_ * sizeof(SavedStop))) {
        ESP_LOGW(TAG, "saved stop write failed");
    }
}

void BusApp::pushRecent(const char *route)
{
    if (route == nullptr || route[0] == '\0') return;
    // Most recent first, deduplicated.
    size_t existing = recent_count_;
    for (size_t i = 0; i < recent_count_; ++i) {
        if (strcmp(recent_[i], route) == 0) { existing = i; break; }
    }
    if (existing == 0 && recent_count_ > 0) return;   // already at the front

    const size_t limit = existing < recent_count_ ? existing : kMaxRecent - 1;
    for (size_t i = limit; i > 0; --i) strlcpy(recent_[i], recent_[i - 1], sizeof(recent_[i]));
    strlcpy(recent_[0], route, sizeof(recent_[0]));
    if (existing == recent_count_ && recent_count_ < kMaxRecent) ++recent_count_;
    renderRecents();
}
