/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

#include "bus_keypad.hpp"
#include "crystal_app.hpp"
#include "lvgl.h"
#include "transit_service.h"

extern "C" const lv_img_dsc_t bus_icon;
extern "C" void bus_icon_prepare(void);

class BusApp final : public CrystalApp {
public:
    BusApp();

    // Called from LVGL event callbacks and the drain timer.
    void onKeyChar(char value);
    void onKeyBackspace();
    void onKeySubmit();
    void onRecentPicked(size_t index);
    void onVariantPicked(size_t index);
    void onStopPicked(size_t index);
    void onSavedPicked(size_t index);
    void onSavedRemove(size_t index);
    void onRefreshPressed();
    void onSavePressed();
    // The on-screen ‹ in the chooser, stop list, and board headers. Same one step
    // per press as the hardware Back, so there is one navigation rule and not two.
    void onBackPressed() { (void)onBack(); }
    void onNearestPressed();
    void onNearestCancel();
    void onTabChanged();
    void handleEvent(const transit_event_t *event);
    void tick();

protected:
    bool onCreate() override;
    bool onPause() override;
    bool onResume() override;
    bool onDestroy() override;
    bool onBack() override;

private:
    // Which Route-tab layer is showing. Tabs are the top-level navigation; these
    // are the stack inside the Route tab, and onBack() peels one at a time.
    enum class View : uint8_t { Keypad = 0, Chooser, Stops, Board };

    // One saved stop, packed. Persisted verbatim as an array under one key, so
    // Phase 13's CrystalState::clear() wipes the app with no per-key list.
    struct SavedStop {
        char    route[TRANSIT_ROUTE_NAME_LEN + 1];
        uint8_t op;
        char    bound;
        uint8_t service_type;
        char    stop_id[TRANSIT_STOP_ID_LEN];
        char    stop_name[TRANSIT_NAME_LEN];
        char    dest[TRANSIT_PLACE_LEN];
        float   lat, lon;
        int32_t eta[TRANSIT_MAX_ETA];   // cached, so Saved paints before any request
        int32_t fetched_at;
    };

    static constexpr size_t kMaxSaved = 8;      // design §2.1: a network budget too
    static constexpr size_t kMaxRecent = 6;

    // Build.
    void buildSavedTab(lv_obj_t *parent);
    void buildRouteTab(lv_obj_t *parent);
    void buildNearbyTab(lv_obj_t *parent);
    void buildBoard(lv_obj_t *parent);

    // Paint.
    void showView(View view);
    void renderRecents();
    void renderChooser();
    void renderStops();
    void renderStopRow(size_t index);
    void renderBoard();
    void renderSaved();
    void renderNearby();
    void updateAges();
    void setRouteStatus(const char *text, bool warn = false);

    // Act.
    void onQueryChanged();
    void maybeAutoSearch();
    void submitRoute();
    void selectVariant(size_t index);
    void selectStop(size_t index);
    void requestEta(bool force);
    void requestVisibleNames();
    void releaseRoute();
    bool isSaved(const char *stop_id) const;

    // Persistence.
    void loadState();
    void saveNavigation();
    void saveSavedStops();
    void pushRecent(const char *route);

    lv_obj_t *root_ = nullptr;
    lv_obj_t *tabs_ = nullptr;
    lv_obj_t *saved_tab_ = nullptr;
    lv_obj_t *route_tab_ = nullptr;
    lv_obj_t *nearby_tab_ = nullptr;

    // Route tab layers.
    lv_obj_t *keypad_view_ = nullptr;
    lv_obj_t *chooser_view_ = nullptr;
    lv_obj_t *stops_view_ = nullptr;
    lv_obj_t *board_view_ = nullptr;

    lv_obj_t *route_display_ = nullptr;
    lv_obj_t *route_status_ = nullptr;
    lv_obj_t *recent_row_ = nullptr;
    lv_obj_t *chooser_list_ = nullptr;
    lv_obj_t *chooser_title_ = nullptr;
    lv_obj_t *stops_title_ = nullptr;
    lv_obj_t *stops_list_ = nullptr;
    lv_obj_t *nearest_button_ = nullptr;
    lv_obj_t *nearest_label_ = nullptr;

    lv_obj_t *board_title_ = nullptr;
    lv_obj_t *board_stop_ = nullptr;
    lv_obj_t *board_sequence_ = nullptr;
    lv_obj_t *board_minutes_[TRANSIT_MAX_ETA] = {};
    lv_obj_t *board_clock_[TRANSIT_MAX_ETA] = {};
    lv_obj_t *board_remark_[TRANSIT_MAX_ETA] = {};
    lv_obj_t *board_empty_ = nullptr;
    lv_obj_t *board_age_ = nullptr;
    lv_obj_t *save_button_ = nullptr;
    lv_obj_t *save_glyph_ = nullptr;

    lv_obj_t *saved_list_ = nullptr;
    lv_obj_t *saved_empty_ = nullptr;
    lv_obj_t *saved_age_ = nullptr;
    lv_obj_t *nearby_list_ = nullptr;
    lv_obj_t *nearby_note_ = nullptr;

    BusKeypad keypad_;

    char query_[TRANSIT_ROUTE_NAME_LEN + 1] = {};
    char recent_[kMaxRecent][TRANSIT_ROUTE_NAME_LEN + 1] = {};
    size_t recent_count_ = 0;

    transit_variant_t variants_[TRANSIT_MAX_VARIANTS] = {};
    size_t variant_count_ = 0;
    transit_variant_t variant_ = {};
    bool have_variant_ = false;

    transit_route_handle_t *route_ = nullptr;
    char stop_id_[TRANSIT_STOP_ID_LEN] = {};
    char stop_name_[TRANSIT_NAME_LEN] = {};
    size_t stop_index_ = 0;

    transit_eta_t eta_[TRANSIT_MAX_ETA] = {};
    size_t eta_count_ = 0;
    int32_t eta_fetched_at_ = 0;
    bool eta_failed_ = false;
    bool eta_pending_ = false;
    bool eta_empty_ = false;

    SavedStop saved_[kMaxSaved] = {};
    size_t saved_count_ = 0;
    size_t saved_refresh_cursor_ = 0;

    // Every in-flight request id the app still cares about. A result whose id is
    // not here is dropped: it is not an error, it is a race the design permits.
    uint32_t variants_request_ = 0;
    uint32_t stops_request_ = 0;
    uint32_t names_request_ = 0;
    uint32_t eta_request_ = 0;
    uint32_t nearest_request_ = 0;
    uint32_t saved_eta_request_ = 0;
    size_t saved_eta_target_ = 0;

    // Real pixels, resolved once in onCreate() from getVisualArea(). Sizes are
    // not written as LV_PCT(100) - n: LV_PCT sets a type flag bit rather than
    // holding a plain number, so subtracting from it produces a garbage
    // coordinate rather than "percent minus n".
    lv_coord_t content_width_ = 0;    // inside the page's horizontal padding
    lv_coord_t page_height_ = 0;      // inside the tab bar and bottom safe area

    View view_ = View::Keypad;
    lv_timer_t *timer_ = nullptr;
    // Live search, per the reference app: a complete route name looks itself up
    // without ⏎. The tick is what debounces it, so typing 6-8-X does not fire a
    // lookup for 6 and another for 68 on the way through.
    uint32_t query_changed_at_ = 0;
    uint32_t last_slow_tick_ = 0;   // gates the per-second half of tick()
    char searched_[TRANSIT_ROUTE_NAME_LEN + 1] = {};   // last name auto-submitted
    uint32_t last_eta_refresh_ = 0;
    uint32_t names_requested_upto_ = 0;
    bool nearest_running_ = false;
};
