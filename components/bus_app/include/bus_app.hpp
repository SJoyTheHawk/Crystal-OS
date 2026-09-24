/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once
#include "crystal_app.hpp"
#include "bus_service.h"
#include "bus_routes.h"
#include "lvgl.h"
LV_IMG_DECLARE(bus_icon);
extern "C" void bus_icon_prepare(void);

class BusApp final : public CrystalApp {
public: BusApp();
    bool onCreate() override; bool onPause() override; bool onResume() override; bool onDestroy() override; bool onBack() override;
    static void event_cb(lv_event_t*);
private:
    enum Page { FAVORITES, SEARCH, STOPS, ETA } page_=FAVORITES;
    lv_obj_t *root_=nullptr,*content_=nullptr,*title_=nullptr,*body_=nullptr,*back_=nullptr,*refresh_=nullptr,*search_input_=nullptr,*letters_=nullptr,*suggestions_=nullptr,*status_=nullptr;
    lv_timer_t *timer_=nullptr; uint32_t eta_tick_=0; uint32_t latest_request_id_=0; bus_route_variant_t route_{}; bus_route_variant_t candidates_[8]{}; uint8_t candidate_count_=0; bus_stop_t *stops_=nullptr; uint16_t stop_count_=0; char stop_id_[20]{}; char route_input_[5]{}; uint8_t route_len_=0; bool saved_=false;
    static void tick_cb(lv_timer_t*); static void bus_event_cb(const bus_event_t*,void*);
    void build_shell(); void show_favorites(); void show_search(); void show_stops(); void show_eta(); void show_suggestions(); void submit_route(); void append(char); void backspace(); void render_eta(const bus_eta_result_t*); void clear_body();
};
