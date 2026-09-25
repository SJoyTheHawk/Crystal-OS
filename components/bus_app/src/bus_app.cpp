#include "bus_app.hpp"
#include "bus_routes.h"
#include "crystal_network.h"
#include "esp_log.h"
#include <cstring>
#include <cstdio>
#include <ctime>

namespace {
constexpr const char *TAG = "bus_app";

// NVS keys (max 7 chars after namespace prefix)
constexpr const char *KEY_FAV_COUNT = "fav_cnt";
constexpr const char *KEY_FAV_DATA = "fav_dat";

// Colors
constexpr uint32_t kBgColor = 0x0F172A;
constexpr uint32_t kCardBg = 0x1E293B;
constexpr uint32_t kTextPrimary = 0xF8FAFC;
constexpr uint32_t kTextSecondary = 0x94A3B8;
constexpr uint32_t kTextWarning = 0xFBBF24;
constexpr uint32_t kTextError = 0xF87171;
constexpr uint32_t kBorder = 0x334155;
constexpr uint32_t kAccent = 0x38BDF8;

constexpr lv_coord_t kPad = 16;
constexpr lv_coord_t kGap = 12;

lv_obj_t *makeCard(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(kCardBg), 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, kPad, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(kBorder), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

lv_obj_t *makeButton(lv_obj_t *parent, const char *text, lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, width, height);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kAccent), 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    return btn;
}

void formatETA(int32_t minutes, char *buf, size_t size)
{
    if (minutes < 1) {
        snprintf(buf, size, "Due");
    } else {
        snprintf(buf, size, "%d min", (int)minutes);
    }
}

void formatAge(uint32_t fetched_at, char *buf, size_t size)
{
    if (fetched_at == 0) {
        strlcpy(buf, "Saved data", size);
        return;
    }

    const time_t now = time(nullptr);
    const uint32_t age = now > static_cast<time_t>(fetched_at)
                             ? static_cast<uint32_t>(now - fetched_at)
                             : 0;
    if (age < 60) {
        strlcpy(buf, "Updated just now", size);
    } else if (age < 3600) {
        snprintf(buf, size, "Updated %lu min ago", static_cast<unsigned long>(age / 60));
    } else {
        snprintf(buf, size, "Updated %lu h ago", static_cast<unsigned long>(age / 3600));
    }
}

}  // namespace

BusApp::BusApp() : CrystalApp("Bus", &bus_icon)
{
    bus_icon_prepare();
}

bool BusApp::onCreate()
{
    ESP_LOGI(TAG, "onCreate");

    const lv_area_t area = getVisualArea();
    const lv_coord_t width = area.x2 - area.x1 + 1;
    const lv_coord_t height = area.y2 - area.y1 + 1;
    ESP_LOGI(TAG, "visual area: x1=%d y1=%d x2=%d y2=%d (%dx%d)",
             area.x1, area.y1, area.x2, area.y2, width, height);

    // Create root container
    root_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, width, height);
    lv_obj_set_pos(root_, 0, 0);  // NOT area.x1, area.y1
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(kBgColor), 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    // Initialize service
    bus_service_init();
    bus_service_set_listener(onBusEvent, this);
    catalog_ready_ = bus_service_route_catalog_ready();
    if (!catalog_ready_) {
        ESP_LOGI(TAG, "Route catalog not ready; Search keypad will remain locked");
    }
    const esp_err_t network_register_err = esp_event_handler_instance_register(
        CRYSTAL_NETWORK_EVENT, ESP_EVENT_ANY_ID, onNetworkEvent, this, &network_handler_);
    ESP_LOGI(TAG, "Network event handler registration: %s",
             esp_err_to_name(network_register_err));

    // Load favorites from NVS
    loadFavoritesFromNVS();

    // Build UI
    const lv_coord_t tab_bar_height = 48;
    buildTabBar(width);
    buildFavoritesTab(width, height, tab_bar_height);
    buildSearchTab(width, height, tab_bar_height);

    catalog_bootstrap_timer_ = lv_timer_create(onCatalogBootstrapTimer, 500, this);
    evaluateCatalogBootstrap();

    // Start refresh timer (30s)
    eta_refresh_timer_ = lv_timer_create(onRefreshTimer, 30000, this);
    refreshFavoriteETAs();

    return true;
}

bool BusApp::onPause()
{
    ESP_LOGI(TAG, "onPause");

    if (eta_refresh_timer_) {
        lv_timer_del(eta_refresh_timer_);
        eta_refresh_timer_ = nullptr;
    }

    bus_service_cancel_all();
    saveFavoritesToNVS();

    return true;
}

bool BusApp::onResume()
{
    ESP_LOGI(TAG, "onResume");
    evaluateCatalogBootstrap();

    if (!eta_refresh_timer_) {
        eta_refresh_timer_ = lv_timer_create(onRefreshTimer, 30000, this);
    }

    refreshFavoriteETAs();

    return true;
}

bool BusApp::onDestroy()
{
    ESP_LOGI(TAG, "onDestroy");

    // Uninstall listener first
    bus_service_set_listener(nullptr, nullptr);
    bus_service_cancel_all();
    if (network_handler_ != nullptr) {
        (void)esp_event_handler_instance_unregister(CRYSTAL_NETWORK_EVENT,
                                                     ESP_EVENT_ANY_ID,
                                                     network_handler_);
        network_handler_ = nullptr;
    }

    if (eta_refresh_timer_) {
        lv_timer_del(eta_refresh_timer_);
        eta_refresh_timer_ = nullptr;
    }
    if (catalog_bootstrap_timer_) {
        lv_timer_del(catalog_bootstrap_timer_);
        catalog_bootstrap_timer_ = nullptr;
    }

    // NULL all pointers
    root_ = nullptr;
    tab_bar_ = nullptr;
    favorites_tab_button_ = nullptr;
    search_tab_button_ = nullptr;
    tab_view_ = nullptr;
    favorites_tab_ = nullptr;
    search_tab_ = nullptr;
    favorite_list_ = nullptr;
    favorites_status_ = nullptr;
    search_input_ = nullptr;
    search_enter_button_ = nullptr;
    keypad_container_ = nullptr;
    search_results_ = nullptr;
    catalog_overlay_ = nullptr;
    catalog_status_ = nullptr;
    memset(keypad_buttons_, 0, sizeof(keypad_buttons_));

    return true;
}

bool BusApp::onBack()
{
    // If on search tab with input, clear input
    if (search_buffer_[0] != '\0') {
        search_buffer_[0] = '\0';
        if (search_input_) {
            lv_label_set_text(search_input_, "");
        }
        updateKeypadState();
        return true;
    }

    // Otherwise, let shell handle it (exit app)
    return false;
}

void BusApp::buildTabBar(lv_coord_t width)
{
    // Tab bar at top
    tab_bar_ = lv_obj_create(root_);
    lv_obj_remove_style_all(tab_bar_);
    lv_obj_set_size(tab_bar_, width, 48);
    lv_obj_align(tab_bar_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(tab_bar_, lv_color_hex(kCardBg), 0);
    lv_obj_set_style_bg_opa(tab_bar_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(tab_bar_, 0, 0);
    lv_obj_set_style_border_width(tab_bar_, 0, 0);
    lv_obj_set_style_radius(tab_bar_, 0, 0);
    lv_obj_set_flex_flow(tab_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab_bar_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(tab_bar_, LV_OBJ_FLAG_SCROLLABLE);

    // Flat tab slots. The active slot is identified by its accent text and
    // bottom rule instead of a raised button surface.
    lv_obj_t *fav_btn = lv_btn_create(tab_bar_);
    favorites_tab_button_ = fav_btn;
    lv_obj_set_size(fav_btn, width / 2, 48);
    lv_obj_set_style_radius(fav_btn, 0, 0);
    lv_obj_set_style_bg_opa(fav_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(fav_btn, 0, 0);
    lv_obj_set_style_border_width(fav_btn, 0, 0);
    lv_obj_set_style_pad_all(fav_btn, 0, 0);
    lv_obj_t *fav_label = lv_label_create(fav_btn);
    lv_label_set_text(fav_label, LV_SYMBOL_HOME " Favorites");
    lv_obj_center(fav_label);
    lv_obj_add_event_cb(fav_btn, onTabChanged, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(fav_btn, reinterpret_cast<void *>(0));

    lv_obj_t *search_btn = lv_btn_create(tab_bar_);
    search_tab_button_ = search_btn;
    lv_obj_set_size(search_btn, width - width / 2, 48);
    lv_obj_set_style_radius(search_btn, 0, 0);
    lv_obj_set_style_bg_opa(search_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(search_btn, 0, 0);
    lv_obj_set_style_border_width(search_btn, 0, 0);
    lv_obj_set_style_pad_all(search_btn, 0, 0);
    lv_obj_t *search_label = lv_label_create(search_btn);
    lv_label_set_text(search_label, LV_SYMBOL_KEYBOARD " Search");
    lv_obj_center(search_label);
    lv_obj_add_event_cb(search_btn, onTabChanged, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(search_btn, reinterpret_cast<void *>(1));

    updateTabAppearance(0);
}

void BusApp::updateTabAppearance(int active_tab)
{
    lv_obj_t *buttons[] = {favorites_tab_button_, search_tab_button_};
    for (int i = 0; i < 2; i++) {
        if (buttons[i] == nullptr) {
            continue;
        }
        lv_obj_t *label = lv_obj_get_child(buttons[i], 0);
        const bool active = i == active_tab;
        lv_obj_set_style_text_color(label, lv_color_hex(active ? kAccent : kTextSecondary), 0);
        lv_obj_set_style_border_width(buttons[i], active ? 3 : 0, 0);
        lv_obj_set_style_border_side(buttons[i], LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(buttons[i], lv_color_hex(kAccent), 0);
    }
}

void BusApp::buildFavoritesTab(lv_coord_t width, lv_coord_t height, lv_coord_t tab_bar_height)
{
    favorites_tab_ = lv_obj_create(root_);
    lv_obj_remove_style_all(favorites_tab_);
    lv_obj_set_size(favorites_tab_, width, height - tab_bar_height);
    lv_obj_align(favorites_tab_, LV_ALIGN_TOP_MID, 0, tab_bar_height);
    lv_obj_set_style_bg_color(favorites_tab_, lv_color_hex(kBgColor), 0);
    lv_obj_set_style_bg_opa(favorites_tab_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(favorites_tab_, 0, 0);
    lv_obj_set_style_radius(favorites_tab_, 0, 0);
    lv_obj_set_style_pad_all(favorites_tab_, kPad, 0);

    favorites_status_ = makeLabel(favorites_tab_, &lv_font_montserrat_16, kTextSecondary);
    lv_obj_align(favorites_status_, LV_ALIGN_TOP_MID, 0, 4);

    favorite_list_ = lv_obj_create(favorites_tab_);
    lv_obj_remove_style_all(favorite_list_);
    lv_obj_set_size(favorite_list_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(favorite_list_, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_bg_color(favorite_list_, lv_color_hex(kBgColor), 0);
    lv_obj_set_style_bg_opa(favorite_list_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(favorite_list_, 0, 0);
    lv_obj_set_style_pad_all(favorite_list_, 0, 0);
    lv_obj_set_flex_flow(favorite_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(favorite_list_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(favorite_list_, kGap, 0);

    rebuildFavoritesView();
}

void BusApp::setFavoritesStatus(const char *message, uint32_t color)
{
    if (favorites_status_ != nullptr) {
        lv_obj_set_style_text_color(favorites_status_, lv_color_hex(color), 0);
        lv_label_set_text(favorites_status_, message != nullptr ? message : "");
    }
}

void BusApp::rebuildFavoritesView()
{
    if (favorite_list_ == nullptr) {
        return;
    }

    lv_obj_clean(favorite_list_);
    if (favorites_count_ == 0) {
        setFavoritesStatus("No saved stops yet  •  Tap Search to find a route", kTextSecondary);
        return;
    }

    for (uint8_t i = 0; i < favorites_count_; i++) {
        lv_obj_t *card = makeCard(favorite_list_);
        lv_obj_set_size(card, LV_PCT(100), 96);
        lv_obj_set_user_data(card, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(card, onFavoriteClicked, LV_EVENT_CLICKED, this);

        lv_obj_t *route_label = makeLabel(card, &lv_font_montserrat_28, kTextPrimary);
        lv_label_set_text(route_label, favorites_[i].route);
        lv_obj_align(route_label, LV_ALIGN_TOP_LEFT, 0, 0);

        char age_text[32];
        formatAge(favorites_[i].last_eta.fetched_at, age_text, sizeof(age_text));
        lv_obj_t *age_label = makeLabel(card, &lv_font_montserrat_16, kTextSecondary);
        lv_label_set_text(age_label, age_text);
        lv_obj_align(age_label, LV_ALIGN_TOP_RIGHT, 0, 4);

        lv_obj_t *stop_label = makeLabel(card, &lv_font_montserrat_16, kTextSecondary);
        lv_label_set_text(stop_label, favorites_[i].stop_name[0] != '\0' ? favorites_[i].stop_name : "Loading stop name...");
        lv_obj_align(stop_label, LV_ALIGN_TOP_LEFT, 0, 30);

        char eta_text[128] = "";
        if (favorites_[i].last_eta.count > 0) {
            char eta_buf[32];
            const uint8_t count = favorites_[i].last_eta.count > 3 ? 3 : favorites_[i].last_eta.count;
            for (uint8_t j = 0; j < count; j++) {
                formatETA(favorites_[i].last_eta.entries[j].minutes_left, eta_buf, sizeof(eta_buf));
                if (j > 0) strlcat(eta_text, " · ", sizeof(eta_text));
                strlcat(eta_text, eta_buf, sizeof(eta_text));
            }
        } else {
            strlcpy(eta_text, "No ETA information", sizeof(eta_text));
        }
        lv_obj_t *eta_label = makeLabel(card, &lv_font_montserrat_16,
                                        favorites_[i].last_eta.count > 0 ? kAccent : kTextSecondary);
        lv_label_set_text(eta_label, eta_text);
        lv_obj_align(eta_label, LV_ALIGN_TOP_LEFT, 0, 58);
    }
}

void BusApp::buildSearchTab(lv_coord_t width, lv_coord_t height, lv_coord_t tab_bar_height)
{
    search_tab_ = lv_obj_create(root_);
    lv_obj_remove_style_all(search_tab_);
    lv_obj_set_size(search_tab_, width, height - tab_bar_height);
    lv_obj_align(search_tab_, LV_ALIGN_TOP_MID, 0, tab_bar_height);
    lv_obj_set_style_bg_color(search_tab_, lv_color_hex(kBgColor), 0);
    lv_obj_set_style_bg_opa(search_tab_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(search_tab_, 0, 0);
    lv_obj_set_style_radius(search_tab_, 0, 0);
    lv_obj_set_style_pad_all(search_tab_, kPad, 0);
    lv_obj_add_flag(search_tab_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(search_tab_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(search_tab_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(search_tab_, LV_OBJ_FLAG_HIDDEN);  // Start hidden

    // Input display
    search_input_ = makeLabel(search_tab_, &lv_font_montserrat_48, kTextPrimary);
    lv_label_set_text(search_input_, "");
    lv_obj_align(search_input_, LV_ALIGN_TOP_MID, 0, 4);

    search_enter_button_ = lv_btn_create(search_tab_);
    lv_obj_set_size(search_enter_button_, 60, 34);
    lv_obj_align(search_enter_button_, LV_ALIGN_TOP_RIGHT, -kPad, 8);
    lv_obj_set_style_radius(search_enter_button_, 8, 0);
    lv_obj_set_style_bg_color(search_enter_button_, lv_color_hex(0x334155), 0);
    lv_obj_t *enter_label = makeLabel(search_enter_button_, &lv_font_montserrat_16, kTextSecondary);
    lv_label_set_text(enter_label, "Go");
    lv_obj_center(enter_label);
    lv_obj_add_event_cb(search_enter_button_, onEnter, LV_EVENT_CLICKED, this);

    // The results belong below the keypad. The Search page itself scrolls so
    // this area can grow when Step 3 adds catalog-backed route rows.
    buildKeypad(width);

    search_results_ = lv_obj_create(search_tab_);
    lv_obj_remove_style_all(search_results_);
    lv_obj_set_size(search_results_, width - 2 * kPad, 96);
    lv_obj_align(search_results_, LV_ALIGN_TOP_MID, 0, 244);
    lv_obj_set_style_bg_color(search_results_, lv_color_hex(kCardBg), 0);
    lv_obj_set_style_bg_opa(search_results_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(search_results_, 10, 0);
    lv_obj_set_style_pad_all(search_results_, 10, 0);
    lv_obj_set_style_border_width(search_results_, 1, 0);
    lv_obj_set_style_border_color(search_results_, lv_color_hex(kBorder), 0);
    // The page owns vertical scrolling. This container grows with its route
    // rows instead of becoming a nested scroll view.
    lv_obj_clear_flag(search_results_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(search_results_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t *results_hint = makeLabel(search_results_, &lv_font_montserrat_16, kTextSecondary);
    lv_label_set_text(results_hint, "Matching routes will appear here");
    lv_obj_center(results_hint);

    // Keep the keypad physically covered until both provider catalogs are
    // loaded. Event handlers also check catalog_ready_ as a second guard.
    catalog_overlay_ = lv_obj_create(search_tab_);
    lv_obj_remove_style_all(catalog_overlay_);
    lv_obj_set_size(catalog_overlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(catalog_overlay_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(catalog_overlay_, lv_color_hex(kBgColor), 0);
    lv_obj_set_style_bg_opa(catalog_overlay_, LV_OPA_90, 0);
    lv_obj_set_style_pad_all(catalog_overlay_, kPad, 0);
    lv_obj_add_flag(catalog_overlay_, LV_OBJ_FLAG_CLICKABLE);

    catalog_status_ = makeLabel(catalog_overlay_, &lv_font_montserrat_20, kTextPrimary);
    lv_label_set_text(catalog_status_, "Fetching route data...\nPlease wait");
    lv_obj_set_style_text_align(catalog_status_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(catalog_status_);
    setCatalogState(catalog_ready_, catalog_ready_ ? "" :
                    (crystal_network_has_ip() ? "Fetching route data...\nPlease wait"
                                               : "Waiting for Wi-Fi connection..."));
}

void BusApp::setCatalogState(bool ready, const char *message)
{
    catalog_ready_ = ready;
    if (ready) {
        updateKeypadState();
    }
    if (catalog_status_ != nullptr) {
        lv_label_set_text(catalog_status_, message != nullptr ? message : "");
    }
    if (catalog_overlay_ != nullptr) {
        if (ready) {
            lv_obj_add_flag(catalog_overlay_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(catalog_overlay_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(catalog_overlay_);
        }
    }
}

void BusApp::buildKeypad(lv_coord_t width)
{
    keypad_container_ = lv_obj_create(search_tab_);
    lv_obj_remove_style_all(keypad_container_);
    const lv_coord_t keypad_width = width * 3 / 4;
    constexpr lv_coord_t keypad_height = 180;
    lv_obj_set_size(keypad_container_, keypad_width, keypad_height);
    // Input occupies the top band. Results are placed immediately after this
    // keypad and the Search page scrolls when the result list grows.
    // Leave a visible margin below the route number before the first row.
    lv_obj_align(keypad_container_, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_color(keypad_container_, lv_color_hex(kBgColor), 0);
    lv_obj_set_style_bg_opa(keypad_container_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(keypad_container_, 0, 0);
    lv_obj_set_style_pad_all(keypad_container_, 0, 0);
    lv_obj_clear_flag(keypad_container_, LV_OBJ_FLAG_SCROLLABLE);

    // Numeric keypad: three columns and four rows, matching the compact
    // hardware-keypad proportions used by the reference apps.
    constexpr lv_coord_t gap = 5;
    const lv_coord_t numeric_width = keypad_width * 2 / 3;
    const lv_coord_t number_width = (numeric_width - 2 * gap) / 3;
    constexpr lv_coord_t number_height = 40;
    const char *numbers[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
    for (int i = 0; i < 9; i++) {
        const int row = i / 3;
        const int col = i % 3;
        lv_obj_t *btn = lv_btn_create(keypad_container_);
        lv_obj_set_size(btn, number_width, number_height);
        lv_obj_set_pos(btn, col * (number_width + gap), row * (number_height + gap));
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(kCardBg), 0);
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, numbers[i]);
        lv_obj_center(label);
        lv_obj_add_event_cb(btn, onKeyPressed, LV_EVENT_CLICKED, this);
        const char *key = strchr(BUS_ROUTE_CHARSET, numbers[i][0]);
        if (key != nullptr) {
            keypad_buttons_[key - BUS_ROUTE_CHARSET] = btn;
        }
    }

    const lv_coord_t command_y = 3 * (number_height + gap);
    lv_obj_t *reset = lv_btn_create(keypad_container_);
    lv_obj_set_size(reset, number_width, number_height);
    lv_obj_set_pos(reset, 0, command_y);
    lv_obj_set_style_radius(reset, 8, 0);
    lv_obj_set_style_bg_color(reset, lv_color_hex(kCardBg), 0);
    lv_obj_t *reset_label = lv_label_create(reset);
    lv_label_set_text(reset_label, LV_SYMBOL_CLOSE);
    lv_obj_center(reset_label);
    lv_obj_add_event_cb(reset, onReset, LV_EVENT_CLICKED, this);

    lv_obj_t *zero = lv_btn_create(keypad_container_);
    lv_obj_set_size(zero, number_width, number_height);
    lv_obj_set_pos(zero, number_width + gap, command_y);
    lv_obj_set_style_radius(zero, 8, 0);
    lv_obj_set_style_bg_color(zero, lv_color_hex(kCardBg), 0);
    lv_obj_t *zero_label = lv_label_create(zero);
    lv_label_set_text(zero_label, "0");
    lv_obj_center(zero_label);
    lv_obj_add_event_cb(zero, onKeyPressed, LV_EVENT_CLICKED, this);
    keypad_buttons_[9] = zero;

    lv_obj_t *backspace = lv_btn_create(keypad_container_);
    lv_obj_set_size(backspace, number_width, number_height);
    lv_obj_set_pos(backspace, 2 * (number_width + gap), command_y);
    lv_obj_set_style_radius(backspace, 8, 0);
    lv_obj_set_style_bg_color(backspace, lv_color_hex(kCardBg), 0);
    lv_obj_t *bs_label = lv_label_create(backspace);
    lv_label_set_text(bs_label, LV_SYMBOL_BACKSPACE);
    lv_obj_center(bs_label);
    lv_obj_add_event_cb(backspace, onBackspace, LV_EVENT_CLICKED, this);

    // Alphabet bank: two columns, vertically scrollable. Keeping the bank
    // separate leaves the numeric keys large enough for reliable touch input.
    lv_obj_t *letter_strip = lv_obj_create(keypad_container_);
    const lv_coord_t letter_x = numeric_width + gap;
    const lv_coord_t letter_width = keypad_width - letter_x;
    const lv_coord_t letter_gap = 6;
    const lv_coord_t letter_tile_width = (letter_width - letter_gap) / 2;
    lv_obj_set_size(letter_strip, letter_width, keypad_height);
    lv_obj_set_pos(letter_strip, letter_x, 0);
    lv_obj_set_style_bg_color(letter_strip, lv_color_hex(kBgColor), 0);
    lv_obj_set_style_border_width(letter_strip, 0, 0);
    lv_obj_set_style_pad_all(letter_strip, 0, 0);
    lv_obj_set_style_pad_row(letter_strip, letter_gap, 0);
    lv_obj_set_style_pad_column(letter_strip, letter_gap, 0);
    lv_obj_set_flex_flow(letter_strip, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(letter_strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(letter_strip, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(letter_strip, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(letter_strip, LV_FLEX_FLOW_ROW_WRAP);

    // Add letter buttons
    const char *letters = BUS_ROUTE_CHARSET + 10;  // Skip digits
    for (const char *p = letters; *p; p++) {
        lv_obj_t *btn = lv_btn_create(letter_strip);
        lv_obj_set_size(btn, letter_tile_width, number_height);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(kCardBg), 0);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text_fmt(label, "%c", *p);
        lv_obj_center(label);

        lv_obj_add_event_cb(btn, onKeyPressed, LV_EVENT_CLICKED, this);
        const char *key = strchr(BUS_ROUTE_CHARSET, *p);
        if (key != nullptr) {
            keypad_buttons_[key - BUS_ROUTE_CHARSET] = btn;
        }
    }
}

void BusApp::updateKeypadState()
{
    size_t len = strlen(search_buffer_);
    uint32_t mask = bus_route_next_mask(search_buffer_, len);
    for (size_t i = 0; i < BUS_ROUTE_CHARSET_LEN; i++) {
        lv_obj_t *button = keypad_buttons_[i];
        if (button == nullptr) {
            continue;
        }
        const bool enabled = (mask & (1u << i)) != 0;
        lv_obj_t *label = lv_obj_get_child(button, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(enabled ? kCardBg : 0x111827), 0);
        if (label != nullptr) {
            lv_obj_set_style_text_color(label, lv_color_hex(enabled ? kTextPrimary : 0x475569), 0);
        }
    }
    const bool complete = bus_route_is_complete(search_buffer_, len);
    if (search_enter_button_ != nullptr) {
        lv_obj_t *label = lv_obj_get_child(search_enter_button_, 0);
        lv_obj_set_style_bg_color(search_enter_button_, lv_color_hex(complete ? kAccent : 0x334155), 0);
        if (label != nullptr) {
            lv_obj_set_style_text_color(label, lv_color_hex(complete ? kBgColor : kTextSecondary), 0);
        }
    }
    rebuildSearchResults();
}

void BusApp::rebuildSearchResults()
{
    if (search_results_ == nullptr) {
        return;
    }

    lv_obj_clean(search_results_);
    const size_t prefix_len = strlen(search_buffer_);
    if (prefix_len == 0) {
        lv_obj_set_height(search_results_, 96);
        lv_obj_t *hint = makeLabel(search_results_, &lv_font_montserrat_16, kTextSecondary);
        lv_label_set_text(hint, "Matching routes will appear here");
        lv_obj_center(hint);
        return;
    }

    uint16_t matches[2048];
    uint16_t match_count = 0;
    const uint16_t catalog_count = bus_route_catalog_count();
    for (uint16_t i = 0; i < catalog_count && match_count < 2048; i++) {
        bus_route_name_t entry;
        if (!bus_route_catalog_get(i, &entry)) {
            continue;
        }
        bool matches_prefix = true;
        for (size_t c = 0; c < prefix_len; c++) {
            if (entry.name[c] != search_buffer_[c]) {
                matches_prefix = false;
                break;
            }
        }
        if (matches_prefix) {
            matches[match_count++] = i;
        }
    }

    // The provider order is not a display contract. Sort matching route names
    // so the keypad results remain stable and easy to scan.
    for (uint16_t i = 1; i < match_count; i++) {
        uint16_t value = matches[i];
        bus_route_name_t value_entry;
        bus_route_catalog_get(value, &value_entry);
        uint16_t j = i;
        while (j > 0) {
            bus_route_name_t previous;
            bus_route_catalog_get(matches[j - 1], &previous);
            if (memcmp(previous.name, value_entry.name, sizeof(previous.name)) <= 0) {
                break;
            }
            matches[j] = matches[j - 1];
            j--;
        }
        matches[j] = value;
    }

    const lv_coord_t row_height = 44;
    const lv_coord_t row_gap = 4;
    // Include the container's vertical padding as well as the row spacing;
    // otherwise the last result is clipped at the bottom of the card.
    lv_obj_set_height(search_results_, 28 + match_count * (row_height + row_gap));
    if (match_count == 0) {
        lv_obj_t *empty = makeLabel(search_results_, &lv_font_montserrat_16, kTextSecondary);
        lv_label_set_text(empty, "No matching routes");
        lv_obj_center(empty);
        return;
    }

    for (uint16_t i = 0; i < match_count; i++) {
        bus_route_name_t entry;
        bus_route_catalog_get(matches[i], &entry);
        char route[5] = {0};
        memcpy(route, entry.name, sizeof(entry.name));
        for (int c = 3; c >= 0 && route[c] == ' '; c--) {
            route[c] = '\0';
        }

        lv_obj_t *row = lv_btn_create(search_results_);
        lv_obj_set_size(row, LV_PCT(100), row_height);
        lv_obj_set_pos(row, 0, 6 + i * (row_height + row_gap));
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(kCardBg), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(kBorder), 0);
        lv_obj_set_user_data(row, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(row, onRouteVariantClicked, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(row, onRouteResultClicked, LV_EVENT_CLICKED, this);

        lv_obj_t *route_label = makeLabel(row, &lv_font_montserrat_20, kTextPrimary);
        lv_label_set_text(route_label, route);
        lv_obj_align(route_label, LV_ALIGN_LEFT_MID, 10, 0);

        char operators[40] = "";
        if (entry.ops & (1u << BUS_OP_KMB)) strlcat(operators, "KMB", sizeof(operators));
        if ((entry.ops & (1u << BUS_OP_KMB)) && (entry.ops & (1u << BUS_OP_CTB))) {
            strlcat(operators, " / ", sizeof(operators));
        }
        if (entry.ops & (1u << BUS_OP_CTB)) strlcat(operators, "CTB", sizeof(operators));
        lv_obj_t *op_label = makeLabel(row, &lv_font_montserrat_14, kTextSecondary);
        lv_label_set_text(op_label, operators);
        lv_obj_align(op_label, LV_ALIGN_RIGHT_MID, -10, 0);
    }
}

void BusApp::rebuildRouteVariantResults()
{
    if (search_results_ == nullptr) {
        return;
    }

    lv_obj_clean(search_results_);
    if (route_variant_count_ == 0) {
        lv_obj_set_height(search_results_, 96);
        lv_obj_t *hint = makeLabel(search_results_, &lv_font_montserrat_16, kTextSecondary);
        lv_label_set_text(hint, "Choose a route direction");
        lv_obj_center(hint);
        return;
    }

    // Keep every direction as its own row and make the provider response
    // order irrelevant. Inbound rows precede outbound rows; ties are stable
    // by operator, service type, and terminal names.
    for (uint8_t i = 1; i < route_variant_count_; i++) {
        bus_route_variant_t value = route_variants_[i];
        uint8_t j = i;
        while (j > 0) {
            const bus_route_variant_t &previous = route_variants_[j - 1];
            bool after = false;
            if (previous.bound != value.bound) {
                after = previous.bound == BUS_DIR_OUTBOUND;
            } else if (previous.op != value.op) {
                after = previous.op > value.op;
            } else if (previous.service_type != value.service_type) {
                after = previous.service_type > value.service_type;
            } else {
                after = strcmp(previous.dest_en, value.dest_en) > 0;
            }
            if (!after) {
                break;
            }
            route_variants_[j] = route_variants_[j - 1];
            j--;
        }
        route_variants_[j] = value;
    }

    const lv_coord_t row_height = 58;
    const lv_coord_t row_gap = 4;
    lv_obj_set_height(search_results_, 28 + route_variant_count_ * (row_height + row_gap));
    for (uint8_t i = 0; i < route_variant_count_; i++) {
        const bus_route_variant_t &variant = route_variants_[i];
        lv_obj_t *row = lv_btn_create(search_results_);
        lv_obj_set_size(row, LV_PCT(100), row_height);
        lv_obj_set_pos(row, 0, 6 + i * (row_height + row_gap));
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(kCardBg), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(kBorder), 0);

        lv_obj_t *route_label = makeLabel(row, &lv_font_montserrat_20, kTextPrimary);
        lv_label_set_text(route_label, variant.route);
        lv_obj_align(route_label, LV_ALIGN_TOP_LEFT, 10, 5);

        const char *direction = variant.bound == BUS_DIR_INBOUND ? "Inbound" : "Outbound";
        const char *operator_name = variant.op == BUS_OP_KMB ? "KMB" :
                                    variant.op == BUS_OP_CTB ? "CTB" : "NWFB";
        char detail[96];
        snprintf(detail, sizeof(detail), "%s  •  %s  •  To %s",
                 direction, operator_name, variant.dest_en);
        lv_obj_t *detail_label = makeLabel(row, &lv_font_montserrat_14, kTextSecondary);
        lv_label_set_text(detail_label, detail);
        lv_label_set_long_mode(detail_label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(detail_label, LV_PCT(100));
        lv_obj_align(detail_label, LV_ALIGN_BOTTOM_LEFT, 10, -5);
    }
}

void BusApp::loadFavoritesFromNVS()
{
    memset(favorites_, 0, sizeof(favorites_));
    size_t size = sizeof(favorites_);
    favorites_count_ = 0;

    if (state().get(KEY_FAV_DATA, favorites_, &size)) {
        size_t count = size / sizeof(Favorite);
        if (count > MAX_FAVORITES) count = MAX_FAVORITES;
        favorites_count_ = static_cast<uint8_t>(count);
        ESP_LOGI(TAG, "Loaded %d favorites", favorites_count_);
    }
}

void BusApp::saveFavoritesToNVS()
{
    if (favorites_count_ > 0) {
        if (!state().set(KEY_FAV_DATA, favorites_, favorites_count_ * sizeof(Favorite))) {
            ESP_LOGE(TAG, "Failed to save favorites");
        }
        ESP_LOGI(TAG, "Saved %d favorites", favorites_count_);
    } else {
        state().erase(KEY_FAV_DATA);
    }
}

void BusApp::refreshFavoriteETAs()
{
    if (favorites_count_ == 0 || favorite_refresh_active_) {
        return;
    }

    memset(favorite_request_ids_, 0, sizeof(favorite_request_ids_));
    favorite_requests_pending_ = 0;
    favorite_refresh_failures_ = 0;
    favorite_refresh_active_ = true;
    setFavoritesStatus("Refreshing ETAs...", kTextWarning);

    for (uint8_t i = 0; i < favorites_count_; i++) {
        favorite_request_ids_[i] = bus_service_request_eta(
            favorites_[i].stop_id,
            favorites_[i].route,
            favorites_[i].op,
            favorites_[i].service_type
        );
        if (favorite_request_ids_[i] != 0) {
            favorite_requests_pending_++;
        } else {
            favorite_refresh_failures_++;
        }
    }

    if (favorite_requests_pending_ == 0) {
        favorite_refresh_active_ = false;
        setFavoritesStatus("Unable to refresh ETAs", kTextError);
    }
}

void BusApp::updateFavoriteCard(int index)
{
    if (index < 0 || index >= favorites_count_) {
        return;
    }
    rebuildFavoritesView();
}

void BusApp::showError(const char *message)
{
    ESP_LOGE(TAG, "Error: %s", message);
    setFavoritesStatus(message != nullptr ? message : "Network error", kTextError);
}

// Static event handlers
void BusApp::onBusEvent(const bus_event_t *event, void *user_data)
{
    BusApp *app = static_cast<BusApp*>(user_data);
    if (!app || !app->root_) {
        return;  // App destroyed
    }

    switch (event->type) {
        case BUS_EVT_ETA:
            for (uint8_t i = 0; i < app->favorites_count_; i++) {
                if (app->favorite_request_ids_[i] != event->request_id) {
                    continue;
                }

                app->favorite_request_ids_[i] = 0;
                if (app->favorite_requests_pending_ > 0) {
                    app->favorite_requests_pending_--;
                }
                if (event->status == ESP_OK) {
                    app->favorites_[i].last_eta = event->data.eta.result;
                    app->updateFavoriteCard(i);
                } else {
                    app->favorite_refresh_failures_++;
                }
                break;
            }
            if (app->favorite_refresh_active_ && app->favorite_requests_pending_ == 0) {
                app->favorite_refresh_active_ = false;
                if (app->favorite_refresh_failures_ == app->favorites_count_) {
                    app->setFavoritesStatus("Unable to refresh ETAs; showing saved data", kTextError);
                } else if (app->favorite_refresh_failures_ > 0) {
                    app->setFavoritesStatus("Some ETAs unavailable; showing saved data", kTextWarning);
                } else {
                    app->setFavoritesStatus("Updated just now", kTextSecondary);
                }
            }
            break;

        case BUS_EVT_ROUTE_VARIANTS:
            if (event->status == ESP_OK && event->data.route_variants.count > 0) {
                if (event->request_id != app->current_request_id_) {
                    free(event->data.route_variants.variants);
                    break;
                }
                app->route_variant_count_ = event->data.route_variants.count;
                if (app->route_variant_count_ > 32) {
                    app->route_variant_count_ = 32;
                }
                memcpy(app->route_variants_, event->data.route_variants.variants,
                       app->route_variant_count_ * sizeof(bus_route_variant_t));
                ESP_LOGI(TAG, "Found %d variants for %s", app->route_variant_count_,
                         app->search_buffer_);
                free(event->data.route_variants.variants);
                app->rebuildRouteVariantResults();
            } else if (event->request_id == app->current_request_id_) {
                app->route_variant_count_ = 0;
                app->rebuildRouteVariantResults();
            }
            break;

        case BUS_EVT_STOPS_LIST:
            if (event->status == ESP_OK && event->data.stops_list.count > 0) {
                ESP_LOGI(TAG, "Found %d stops", event->data.stops_list.count);
                // TODO: Show stop picker
                free(event->data.stops_list.stops);
            }
            break;

        case BUS_EVT_ROUTE_CATALOG:
            app->catalog_request_started_ = false;
            app->catalog_bootstrap_checked_ = true;
            if (event->status == ESP_OK) {
                app->setCatalogState(true, "");
                ESP_LOGI(TAG, "Route catalog ready: %u routes, %u providers succeeded, %u failed",
                         event->data.route_catalog.route_count,
                         event->data.route_catalog.providers_succeeded,
                         event->data.route_catalog.providers_failed);
            } else if (event->status == ESP_ERR_NOT_FINISHED) {
                app->setCatalogState(false, "Route data incomplete\nRetrying provider fetch...");
                ESP_LOGW(TAG, "Route catalog partial: %u routes, %u providers succeeded, %u failed; refresh will retry",
                         event->data.route_catalog.route_count,
                         event->data.route_catalog.providers_succeeded,
                         event->data.route_catalog.providers_failed);
            } else {
                app->setCatalogState(false, "Route data unavailable\nConnect to Wi-Fi and retry");
                ESP_LOGE(TAG, "Route catalog bootstrap failed; cached routes remain active (%u routes)",
                         event->data.route_catalog.route_count);
            }
            break;

        case BUS_EVT_ERROR:
            app->showError(event->data.error.message);
            break;

        default:
            break;
    }
}

void BusApp::onNetworkEvent(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)base;
    (void)data;
    BusApp *app = static_cast<BusApp *>(arg);
    if (app == nullptr) {
        return;
    }
    ESP_LOGI(TAG, "Network event received: id=%ld", static_cast<long>(id));
    if (id == CRYSTAL_NETWORK_DISCONNECTED) {
        app->catalog_request_started_ = false;
        app->catalog_bootstrap_checked_ = false;
        ESP_LOGI(TAG, "Wi-Fi disconnected; catalog bootstrap is waiting");
        return;
    }
    if (id != CRYSTAL_NETWORK_CONNECTED) {
        return;
    }
    ESP_LOGI(TAG, "Wi-Fi connected; catalog bootstrap will be evaluated on the LVGL task");
}

void BusApp::onCatalogBootstrapTimer(lv_timer_t *timer)
{
    BusApp *app = static_cast<BusApp *>(timer != nullptr ? timer->user_data : nullptr);
    if (app != nullptr && app->root_ != nullptr) {
        app->evaluateCatalogBootstrap();
    }
}

void BusApp::evaluateCatalogBootstrap()
{
    if (catalog_bootstrap_checked_) {
        return;
    }

    const bool cached_catalog_available = bus_service_route_catalog_ready();
    const time_t now = time(nullptr);
    const bool time_synced = now >= 1577836800 && crystal_time_last_sync() > 0;
    if (!time_synced) {
        setCatalogState(cached_catalog_available, cached_catalog_available ? "" :
                        "Waiting for time synchronization...");
        return;
    }
    if (!crystal_network_has_ip()) {
        setCatalogState(cached_catalog_available, cached_catalog_available ? "" :
                        "Waiting for Wi-Fi connection...");
        return;
    }
    if (catalog_request_started_) {
        return;
    }

    catalog_request_started_ = true;
    const uint32_t request_id = bus_service_request_route_catalog();
    ESP_LOGI(TAG, "Prerequisites ready; route catalog bootstrap requested (id=%lu)",
             static_cast<unsigned long>(request_id));
    if (request_id == 0) {
        if (cached_catalog_available) {
            catalog_bootstrap_checked_ = true;
            catalog_ready_ = true;
            setCatalogState(true, "");
        } else {
            catalog_request_started_ = false;
            ESP_LOGW(TAG, "Route catalog request was not queued; bootstrap will retry");
        }
    }
}

void BusApp::onTabChanged(lv_event_t *e)
{
    BusApp *app = static_cast<BusApp*>(lv_event_get_user_data(e));
    if (app == nullptr) return;
    intptr_t tab_idx = reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_target(e)));

    if (tab_idx == 0) {
        // Show favorites
        lv_obj_clear_flag(app->favorites_tab_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(app->search_tab_, LV_OBJ_FLAG_HIDDEN);
        app->updateTabAppearance(0);
    } else {
        // Show search
        lv_obj_add_flag(app->favorites_tab_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(app->search_tab_, LV_OBJ_FLAG_HIDDEN);
        app->updateTabAppearance(1);
    }
}

void BusApp::onFavoriteClicked(lv_event_t *e)
{
    BusApp *app = static_cast<BusApp*>(lv_event_get_user_data(e));
    lv_obj_t *card = lv_event_get_target(e);
    int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(card)));

    ESP_LOGI(TAG, "Favorite %d clicked", index);
    // TODO: Show ETA board
    (void)app;
}

void BusApp::onKeyPressed(lv_event_t *e)
{
    BusApp *app = static_cast<BusApp*>(lv_event_get_user_data(e));
    if (app == nullptr || !app->catalog_ready_) return;
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    const char *text = lv_label_get_text(label);
    if (!text || text[0] == '\0') return;

    char ch = text[0];
    size_t len = strlen(app->search_buffer_);
    const char *key = strchr(BUS_ROUTE_CHARSET, ch);
    if (key == nullptr || (bus_route_next_mask(app->search_buffer_, len) &
                           (1u << (key - BUS_ROUTE_CHARSET))) == 0) {
        return;
    }
    if (len < 4) {
        app->search_buffer_[len] = ch;
        app->search_buffer_[len + 1] = '\0';
        lv_label_set_text(app->search_input_, app->search_buffer_);
        app->updateKeypadState();
    }
}

void BusApp::onBackspace(lv_event_t *e)
{
    BusApp *app = static_cast<BusApp*>(lv_event_get_user_data(e));
    if (app == nullptr || !app->catalog_ready_) return;

    size_t len = strlen(app->search_buffer_);
    if (len > 0) {
        app->search_buffer_[len - 1] = '\0';
        lv_label_set_text(app->search_input_, app->search_buffer_);
        app->updateKeypadState();
    }
}

void BusApp::onReset(lv_event_t *e)
{
    BusApp *app = static_cast<BusApp*>(lv_event_get_user_data(e));
    if (app == nullptr || !app->catalog_ready_) return;
    app->search_buffer_[0] = '\0';
    lv_label_set_text(app->search_input_, "");
    app->updateKeypadState();
}

void BusApp::onEnter(lv_event_t *e)
{
    BusApp *app = static_cast<BusApp*>(lv_event_get_user_data(e));
    if (app == nullptr || !app->catalog_ready_) return;

    if (bus_route_is_complete(app->search_buffer_, strlen(app->search_buffer_))) {
        app->route_variant_count_ = bus_service_get_cached_route_variants(
            app->search_buffer_, app->route_variants_,
            static_cast<uint8_t>(sizeof(app->route_variants_) / sizeof(app->route_variants_[0])));
        ESP_LOGI(TAG, "Searching cached route variants: %s (%u variants)",
                 app->search_buffer_, app->route_variant_count_);
        app->rebuildRouteVariantResults();
    }
}

void BusApp::onRouteResultClicked(lv_event_t *e)
{
    BusApp *app = static_cast<BusApp*>(lv_event_get_user_data(e));
    if (app == nullptr || !app->catalog_ready_) {
        return;
    }
    lv_obj_t *row = lv_event_get_target(e);
    lv_obj_t *route_label = lv_obj_get_child(row, 0);
    if (route_label == nullptr) {
        return;
    }
    const char *route = lv_label_get_text(route_label);
    if (route == nullptr || route[0] == '\0') {
        return;
    }
    strlcpy(app->search_buffer_, route, sizeof(app->search_buffer_));
    lv_label_set_text(app->search_input_, app->search_buffer_);
    app->route_variant_count_ = 0;
    app->route_variant_count_ = bus_service_get_cached_route_variants(
        app->search_buffer_, app->route_variants_,
        static_cast<uint8_t>(sizeof(app->route_variants_) / sizeof(app->route_variants_[0])));
    app->rebuildRouteVariantResults();
    ESP_LOGI(TAG, "Route result selected from cache: %s (%u variants)", route,
             app->route_variant_count_);
}

void BusApp::onRouteVariantClicked(lv_event_t *e)
{
    BusApp *app = static_cast<BusApp *>(lv_event_get_user_data(e));
    if (app == nullptr || !app->catalog_ready_) {
        return;
    }

    lv_obj_t *row = lv_event_get_target(e);
    const uintptr_t index = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(row));
    if (index >= app->route_variant_count_) {
        return;
    }

    app->current_route_ = app->route_variants_[index];
    const uint32_t request_id = bus_service_request_stops(
        app->current_route_.route,
        app->current_route_.op,
        app->current_route_.bound,
        app->current_route_.service_type);
    ESP_LOGI(TAG, "Route variant selected: %s %c %s -> stops request=%lu",
             app->current_route_.route,
             app->current_route_.bound,
             app->current_route_.dest_en,
             static_cast<unsigned long>(request_id));
}

void BusApp::onRefreshTimer(lv_timer_t *timer)
{
    BusApp *app = static_cast<BusApp*>(timer->user_data);
    if (app && app->root_) {
        app->refreshFavoriteETAs();
    }
}
