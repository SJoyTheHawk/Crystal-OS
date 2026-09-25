#pragma once

#include "crystal_app.hpp"
#include "crystal_core.hpp"
#include "lvgl.h"
#include "bus_service.h"

extern "C" void bus_icon_prepare(void);
LV_IMG_DECLARE(bus_icon);

#define MAX_FAVORITES 8

class BusApp final : public CrystalApp {
public:
    BusApp();
    bool onCreate() override;
    bool onPause() override;
    bool onResume() override;
    bool onDestroy() override;
    bool onBack() override;

private:
    // Favorite entry (stored in NVS)
    struct Favorite {
        char route[5];
        uint8_t op;
        char bound;
        uint8_t service_type;
        char stop_id[20];
        char stop_name[60];
        bus_eta_result_t last_eta;
    };

    // Pages
    lv_obj_t *root_ = nullptr;
    lv_obj_t *tab_bar_ = nullptr;
    lv_obj_t *favorites_tab_button_ = nullptr;
    lv_obj_t *search_tab_button_ = nullptr;
    lv_obj_t *tab_view_ = nullptr;
    lv_obj_t *favorites_tab_ = nullptr;
    lv_obj_t *search_tab_ = nullptr;
    lv_obj_t *favorite_list_ = nullptr;
    lv_obj_t *favorites_status_ = nullptr;
    lv_obj_t *search_input_ = nullptr;
    lv_obj_t *keypad_container_ = nullptr;
    lv_obj_t *search_results_ = nullptr;
    lv_obj_t *catalog_overlay_ = nullptr;
    lv_obj_t *catalog_status_ = nullptr;

    // State
    Favorite favorites_[MAX_FAVORITES];
    uint32_t favorite_request_ids_[MAX_FAVORITES] = {};
    uint8_t favorites_count_ = 0;
    uint8_t favorite_requests_pending_ = 0;
    uint8_t favorite_refresh_failures_ = 0;
    bool favorite_refresh_active_ = false;
    bool catalog_ready_ = false;
    uint32_t current_request_id_ = 0;
    bus_route_variant_t current_route_;
    char search_buffer_[5] = {0};

    // Timers
    lv_timer_t *eta_refresh_timer_ = nullptr;

    // UI builders
    void buildTabBar(lv_coord_t width);
    void buildFavoritesTab(lv_coord_t width, lv_coord_t height, lv_coord_t tab_bar_height);
    void buildSearchTab(lv_coord_t width, lv_coord_t height, lv_coord_t tab_bar_height);
    void buildKeypad(lv_coord_t width);
    void updateTabAppearance(int active_tab);
    void setCatalogState(bool ready, const char *message);
    void rebuildFavoritesView();
    void setFavoritesStatus(const char *message, uint32_t color);

    // Event handlers
    static void onBusEvent(const bus_event_t *event, void *user_data);
    static void onTabChanged(lv_event_t *e);
    static void onFavoriteClicked(lv_event_t *e);
    static void onKeyPressed(lv_event_t *e);
    static void onBackspace(lv_event_t *e);
    static void onReset(lv_event_t *e);
    static void onEnter(lv_event_t *e);
    static void onRefreshTimer(lv_timer_t *timer);

    // Helpers
    void updateKeypadState();
    void loadFavoritesFromNVS();
    void saveFavoritesToNVS();
    void refreshFavoriteETAs();
    void updateFavoriteCard(int index);
    void showError(const char *message);
};
