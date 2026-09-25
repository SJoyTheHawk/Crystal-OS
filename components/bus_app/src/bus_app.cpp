#include "bus_app.hpp"
#include "bus_routes.h"
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

    // Load favorites from NVS
    loadFavoritesFromNVS();

    // Build UI
    const lv_coord_t tab_bar_height = 56;
    buildTabBar(width);
    buildFavoritesTab(width, height, tab_bar_height);
    buildSearchTab(width, height, tab_bar_height);

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

    if (eta_refresh_timer_) {
        lv_timer_del(eta_refresh_timer_);
        eta_refresh_timer_ = nullptr;
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
    keypad_container_ = nullptr;

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
    lv_obj_set_size(tab_bar_, width, 56);
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
    lv_obj_set_size(fav_btn, width / 2, 56);
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
    lv_obj_set_size(search_btn, width - width / 2, 56);
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
    lv_obj_add_flag(search_tab_, LV_OBJ_FLAG_HIDDEN);  // Start hidden

    // Input display
    search_input_ = makeLabel(search_tab_, &lv_font_montserrat_48, kTextPrimary);
    lv_label_set_text(search_input_, "");
    lv_obj_align(search_input_, LV_ALIGN_TOP_MID, 0, kPad);

    // Build keypad
    buildKeypad(width);
}

void BusApp::buildKeypad(lv_coord_t width)
{
    keypad_container_ = lv_obj_create(search_tab_);
    lv_obj_remove_style_all(keypad_container_);
    lv_obj_set_size(keypad_container_, width * 9 / 10, 320);
    lv_obj_align(keypad_container_, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(keypad_container_, lv_color_hex(kBgColor), 0);
    lv_obj_set_style_bg_opa(keypad_container_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(keypad_container_, 0, 0);
    lv_obj_set_style_pad_all(keypad_container_, 0, 0);
    lv_obj_clear_flag(keypad_container_, LV_OBJ_FLAG_SCROLLABLE);

    // Number grid (4x3)
    const char *numbers[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
    lv_coord_t btn_size = 60;
    lv_coord_t spacing = 8;

    for (int i = 0; i < 10; i++) {
        int row = i / 3;
        int col = i % 3;
        if (i == 9) { row = 3; col = 1; }  // 0 in bottom center

        lv_obj_t *btn = lv_btn_create(keypad_container_);
        lv_obj_set_size(btn, btn_size, btn_size);
        lv_obj_set_pos(btn, col * (btn_size + spacing) + 40, row * (btn_size + spacing));
        lv_obj_set_style_bg_color(btn, lv_color_hex(kCardBg), 0);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, numbers[i]);
        lv_obj_center(label);

        lv_obj_add_event_cb(btn, onKeyPressed, LV_EVENT_CLICKED, this);
    }

    // Alphabet strip: four tiles are visible at once beside the keypad. The
    // remaining route letters can be reached with a vertical swipe.
    lv_obj_t *letter_strip = lv_obj_create(keypad_container_);
    constexpr lv_coord_t letter_tile = 60;
    constexpr lv_coord_t letter_gap = 8;
    constexpr lv_coord_t letter_visible_height = letter_tile * 4 + letter_gap * 3;
    lv_obj_set_size(letter_strip, letter_tile, letter_visible_height);
    lv_obj_set_pos(letter_strip, width * 9 / 10 - letter_tile, 0);
    lv_obj_set_style_bg_color(letter_strip, lv_color_hex(kBgColor), 0);
    lv_obj_set_style_border_width(letter_strip, 0, 0);
    lv_obj_set_style_pad_all(letter_strip, 0, 0);
    lv_obj_set_style_pad_row(letter_strip, letter_gap, 0);
    lv_obj_set_flex_flow(letter_strip, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(letter_strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(letter_strip, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(letter_strip, LV_SCROLLBAR_MODE_OFF);

    // Add letter buttons
    const char *letters = BUS_ROUTE_CHARSET + 10;  // Skip digits
    for (const char *p = letters; *p; p++) {
        lv_obj_t *btn = lv_btn_create(letter_strip);
        lv_obj_set_size(btn, letter_tile, letter_tile);
        lv_obj_set_style_bg_color(btn, lv_color_hex(kCardBg), 0);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text_fmt(label, "%c", *p);
        lv_obj_center(label);

        lv_obj_add_event_cb(btn, onKeyPressed, LV_EVENT_CLICKED, this);
    }

    // Backspace button
    lv_obj_t *backspace = lv_btn_create(keypad_container_);
    lv_obj_set_size(backspace, btn_size, btn_size);
    lv_obj_set_pos(backspace, 3 * (btn_size + spacing) + 40, 0);
    lv_obj_set_style_bg_color(backspace, lv_color_hex(0x991B1B), 0);  // Red
    lv_obj_t *bs_label = lv_label_create(backspace);
    lv_label_set_text(bs_label, LV_SYMBOL_BACKSPACE);
    lv_obj_center(bs_label);
    lv_obj_add_event_cb(backspace, onBackspace, LV_EVENT_CLICKED, this);

    // Enter button
    lv_obj_t *enter = lv_btn_create(keypad_container_);
    lv_obj_set_size(enter, btn_size, btn_size);
    lv_obj_set_pos(enter, 3 * (btn_size + spacing) + 40, (btn_size + spacing));
    lv_obj_set_style_bg_color(enter, lv_color_hex(0x15803D), 0);  // Green
    lv_obj_t *enter_label = lv_label_create(enter);
    lv_label_set_text(enter_label, LV_SYMBOL_OK);
    lv_obj_center(enter_label);
    lv_obj_add_event_cb(enter, onEnter, LV_EVENT_CLICKED, this);
}

void BusApp::updateKeypadState()
{
    // TODO: Implement adaptive key greying based on route index
    size_t len = strlen(search_buffer_);
    uint32_t mask = bus_route_next_mask(search_buffer_, len);

    // For now, just update enter button state
    uint8_t is_complete = bus_route_is_complete(search_buffer_, len);
    (void)is_complete;  // TODO: enable/disable enter button
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
                ESP_LOGI(TAG, "Found %d variants", event->data.route_variants.count);
                // TODO: Show direction chooser or go to stops
                free(event->data.route_variants.variants);
            }
            break;

        case BUS_EVT_STOPS_LIST:
            if (event->status == ESP_OK && event->data.stops_list.count > 0) {
                ESP_LOGI(TAG, "Found %d stops", event->data.stops_list.count);
                // TODO: Show stop picker
                free(event->data.stops_list.stops);
            }
            break;

        case BUS_EVT_ERROR:
            app->showError(event->data.error.message);
            break;

        default:
            break;
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
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    const char *text = lv_label_get_text(label);
    if (!text || text[0] == '\0') return;

    char ch = text[0];
    size_t len = strlen(app->search_buffer_);
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

    size_t len = strlen(app->search_buffer_);
    if (len > 0) {
        app->search_buffer_[len - 1] = '\0';
        lv_label_set_text(app->search_input_, app->search_buffer_);
        app->updateKeypadState();
    }
}

void BusApp::onEnter(lv_event_t *e)
{
    BusApp *app = static_cast<BusApp*>(lv_event_get_user_data(e));

    if (bus_route_is_complete(app->search_buffer_, strlen(app->search_buffer_))) {
        ESP_LOGI(TAG, "Searching for route: %s", app->search_buffer_);
        app->current_request_id_ = bus_service_request_route(app->search_buffer_);
    }
}

void BusApp::onRefreshTimer(lv_timer_t *timer)
{
    BusApp *app = static_cast<BusApp*>(timer->user_data);
    if (app && app->root_) {
        app->refreshFavoriteETAs();
    }
}
