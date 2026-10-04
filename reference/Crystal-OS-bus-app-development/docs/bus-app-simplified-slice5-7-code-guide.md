# Simplified Slice 5-7 Code Guide

**Date:** 2026-10-03  
**Status:** Implementation guide  
**Builds on:** Slice 4R.4-4R.6 (stop catalog lookup working)

## Overview

This guide implements the complete bus app user flow in three incremental slices:
- **Slice 5:** Stop picker screen (scrollable list with names)
- **Slice 6:** ETA screen (live arrivals, auto-refresh, save button)
- **Slice 7:** Favorites tab (cards with live ETAs, edit mode)

Each slice builds on the previous one. Test each slice before moving to the next.

## File Structure

```
components/bus_app/
├── include/
│   └── bus_app.h
├── src/
│   ├── bus_app.cpp              # Main app class (MODIFY)
│   ├── bus_favorites.cpp        # Favorites persistence (NEW - Slice 7)
│   └── bus_eta_card.cpp         # Favorite card widget (NEW - Slice 7)
└── CMakeLists.txt

components/bus_service/
├── include/
│   └── bus_service.h            # Add ETA request API (MODIFY)
└── src/
    └── bus_service.c            # Add ETA handler (MODIFY)
```

---

## Slice 5: Stop Picker Implementation

### Step 5.1: Add Selection State to BusApp

**File:** `components/bus_app/include/bus_app.h`

```cpp
class BusApp : public CrystalApp {
public:
    // ... existing ...
    
private:
    // Current page state
    enum class Page {
        ROUTE_LIST,    // Search tab
        STOP_PICKER,   // NEW: selecting a stop
        ETA_SCREEN,    // NEW: Slice 6
    };
    
    Page current_page_ = Page::ROUTE_LIST;
    
    // Selected route variant (already exists from Slice 3)
    struct {
        char route[16];
        bus_operator_t operator;
        char bound[16];
        uint8_t service_type;
        bool valid;
    } selected_route_;
    
    // NEW: Selected stop (for ETA screen)
    struct {
        char route[16];
        bus_operator_t operator;
        char bound[16];
        uint8_t service_type;
        char stop_id[16];
        char stop_name_en[64];
        char stop_name_tc[64];
        float lat;
        float lon;
        uint16_t sequence;
        bool valid;
    } selected_stop_;
    
    // UI objects for stop picker
    lv_obj_t *stop_picker_page_ = nullptr;
    lv_obj_t *stop_list_container_ = nullptr;
    
    // Methods
    void build_stop_picker();
    void rebuild_stop_list(const bus_event_stops_list_t &evt);
    void on_stop_selected(uint16_t index);
    void show_page(Page page);
};
```

### Step 5.2: Build Stop Picker UI

**File:** `components/bus_app/src/bus_app.cpp`

```cpp
void BusApp::build_stop_picker() {
    // Create full-screen page
    const lv_area_t area = getVisualArea();
    stop_picker_page_ = lv_obj_create(lv_scr_act());
    lv_obj_set_size(stop_picker_page_, lv_area_get_width(&area), 
                    lv_area_get_height(&area));
    lv_obj_set_pos(stop_picker_page_, 0, 0);
    lv_obj_clear_flag(stop_picker_page_, LV_OBJ_FLAG_SCROLLABLE);
    
    // Header with route info and back button
    lv_obj_t *header = lv_obj_create(stop_picker_page_);
    lv_obj_set_size(header, lv_area_get_width(&area), 50);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    
    // Back button
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 40, 40);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 5, 0);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);
    
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) {
        auto *app = static_cast<BusApp *>(e->user_data);
        app->show_page(Page::ROUTE_LIST);
    }, LV_EVENT_CLICKED, this);
    
    // Route info label
    lv_obj_t *route_label = lv_label_create(header);
    char route_text[64];
    snprintf(route_text, sizeof(route_text), "Route %s %s",
             selected_route_.route, selected_route_.bound);
    lv_label_set_text(route_label, route_text);
    lv_obj_align(route_label, LV_ALIGN_CENTER, 0, 0);
    
    // Scrollable stop list container
    stop_list_container_ = lv_obj_create(stop_picker_page_);
    lv_obj_set_size(stop_list_container_, lv_area_get_width(&area),
                    lv_area_get_height(&area) - 50);
    lv_obj_align(stop_list_container_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(stop_list_container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(stop_list_container_, 5, 0);
    
    // Loading state initially
    lv_obj_t *loading = lv_label_create(stop_list_container_);
    lv_label_set_text(loading, "Loading stops...");
    lv_obj_center(loading);
}

void BusApp::rebuild_stop_list(const bus_event_stops_list_t &evt) {
    if (!stop_list_container_) return;
    
    // Clear existing children
    lv_obj_clean(stop_list_container_);
    
    if (evt.count == 0) {
        lv_obj_t *empty = lv_label_create(stop_list_container_);
        lv_label_set_text(empty, "No stops found");
        lv_obj_center(empty);
        return;
    }
    
    // Create one row per stop
    for (uint16_t i = 0; i < evt.count; i++) {
        const auto &stop = evt.stops[i];
        
        // Stop row button
        lv_obj_t *row = lv_btn_create(stop_list_container_);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, 50);
        
        // Stop label: "1. Stop Name" or "1. Stop ABC123" (fallback)
        lv_obj_t *label = lv_label_create(row);
        char text[128];
        snprintf(text, sizeof(text), "%d. %s", 
                 stop.sequence, 
                 stop.has_metadata ? stop.name_en : stop.stop_id);
        lv_label_set_text(label, text);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);
        
        // Store index as user data
        lv_obj_set_user_data(row, (void *)(uintptr_t)i);
        
        // Click handler
        lv_obj_add_event_cb(row, [](lv_event_t *e) {
            auto *app = static_cast<BusApp *>(e->user_data);
            lv_obj_t *row = lv_event_get_target(e);
            uint16_t index = (uint16_t)(uintptr_t)lv_obj_get_user_data(row);
            app->on_stop_selected(index);
        }, LV_EVENT_CLICKED, this);
    }
}
```

### Step 5.3: Handle Stop Selection

```cpp
void BusApp::on_stop_selected(uint16_t index) {
    // Copy stop data from the last stops event
    // (You'll need to keep the stops array in a member variable)
    const auto &stop = current_stops_[index];
    
    selected_stop_.valid = true;
    strlcpy(selected_stop_.route, selected_route_.route, 
            sizeof(selected_stop_.route));
    selected_stop_.operator = selected_route_.operator;
    strlcpy(selected_stop_.bound, selected_route_.bound, 
            sizeof(selected_stop_.bound));
    selected_stop_.service_type = selected_route_.service_type;
    strlcpy(selected_stop_.stop_id, stop.stop_id, 
            sizeof(selected_stop_.stop_id));
    strlcpy(selected_stop_.stop_name_en, stop.name_en, 
            sizeof(selected_stop_.stop_name_en));
    strlcpy(selected_stop_.stop_name_tc, stop.name_tc, 
            sizeof(selected_stop_.stop_name_tc));
    selected_stop_.lat = stop.lat;
    selected_stop_.lon = stop.lon;
    selected_stop_.sequence = stop.sequence;
    
    ESP_LOGI(TAG, "selected stop: route=%s operator=%d stop=%s name=%s",
             selected_stop_.route, selected_stop_.operator,
             selected_stop_.stop_id, selected_stop_.stop_name_en);
    
    // Move to ETA screen (Slice 6)
    show_page(Page::ETA_SCREEN);
}

void BusApp::show_page(Page page) {
    // Hide all pages
    if (root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    if (stop_picker_page_) lv_obj_add_flag(stop_picker_page_, LV_OBJ_FLAG_HIDDEN);
    if (eta_screen_page_) lv_obj_add_flag(eta_screen_page_, LV_OBJ_FLAG_HIDDEN);
    
    // Show requested page
    switch (page) {
    case Page::ROUTE_LIST:
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        break;
    case Page::STOP_PICKER:
        if (!stop_picker_page_) build_stop_picker();
        lv_obj_clear_flag(stop_picker_page_, LV_OBJ_FLAG_HIDDEN);
        break;
    case Page::ETA_SCREEN:
        if (!eta_screen_page_) build_eta_screen();
        lv_obj_clear_flag(eta_screen_page_, LV_OBJ_FLAG_HIDDEN);
        start_eta_refresh();
        break;
    }
    
    current_page_ = page;
}
```

### Step 5.4: Connect to Route Selection

When user selects a route variant in Search (existing code), show stop picker:

```cpp
void BusApp::on_route_variant_selected(/* ... */) {
    // ... save selected_route_ ...
    
    // Request stops from service (existing)
    bus_service_request_stops(selected_route_.route,
                               selected_route_.operator,
                               selected_route_.bound,
                               selected_route_.service_type);
    
    // Show stop picker page
    show_page(Page::STOP_PICKER);
}

// In your bus event handler
void BusApp::on_bus_event(bus_event_t type, const void *data, size_t len) {
    if (type == BUS_EVT_STOPS_LIST) {
        const auto *evt = static_cast<const bus_event_stops_list_t *>(data);
        
        // Keep a copy for selection
        current_stop_count_ = evt->count;
        memcpy(current_stops_, evt->stops, 
               evt->count * sizeof(evt->stops[0]));
        
        // Rebuild list UI
        rebuild_stop_list(*evt);
    }
}
```

### Slice 5 Test

**Build and flash:**
```bash
idf.py build
idf.py -p /dev/cu.usbmodem1201 flash monitor
```

**Test:**
1. Search for route 101
2. Select "KMB 101 Inbound" → stop picker should appear
3. See 35 stops with real names (not "Stop XXX")
4. Tap stop #10 → logs show selection
5. Back button → returns to Search

**Pass criteria:** ✅ All 5 steps work without crashes

---

## Slice 6: ETA Screen Implementation

### Step 6.1: Add ETA Request to Service

**File:** `components/bus_service/include/bus_service.h`

```c
typedef struct {
    int minutes_until;
    time_t eta_timestamp;
    char remark[32];
    bool is_scheduled;
} bus_eta_prediction_t;

typedef struct {
    bus_eta_prediction_t predictions[3];
    uint8_t count;
    time_t fetched_at;
    bool success;
    char error_msg[64];
} bus_eta_result_t;

// Event type
#define BUS_EVT_ETA_RESULT  5

typedef struct {
    char route[16];
    char stop_id[16];
    bus_eta_result_t result;
} bus_event_eta_t;

// Request ETA for a specific stop
void bus_service_request_eta(const char *route,
                              bus_operator_t operator,
                              const char *bound,
                              uint8_t service_type,
                              const char *stop_id);
```

**File:** `components/bus_service/src/bus_service.c`

```c
void bus_service_request_eta(const char *route,
                              bus_operator_t operator,
                              const char *bound,
                              uint8_t service_type,
                              const char *stop_id) {
    // Build URL based on operator
    char url[256];
    if (operator == BUS_OP_KMB) {
        // KMB stop-level ETA
        snprintf(url, sizeof(url),
                 "https://data.etabus.gov.hk/v1/transport/kmb/stop-eta/%s",
                 stop_id);
    } else {
        // CTB stop-level ETA
        const char *company = "ctb";  // or "nwfb"
        snprintf(url, sizeof(url),
                 "https://rt.data.gov.hk/v2/transport/citybus/eta/%s/stop/%s",
                 company, stop_id);
    }
    
    // Submit to crystal_http
    crystal_http_request_t req = {
        .url = url,
        .owner_id = BUS_SERVICE_OWNER_ID,
        .timeout_ms = 8000,
        .retry_count = 2,
        .body_cap = 65536,  // 64KB
        .callback = eta_response_callback,
        .user_data = /* context with route/stop */
    };
    
    crystal_http_submit(&req);
}

static void eta_response_callback(crystal_http_response_t *response) {
    bus_eta_result_t result = {0};
    
    if (response->error != ESP_OK || response->status != 200) {
        result.success = false;
        snprintf(result.error_msg, sizeof(result.error_msg), 
                 "HTTP %d", response->status);
        goto done;
    }
    
    // Parse JSON
    cJSON *root = cJSON_Parse(response->body);
    if (!root) {
        result.success = false;
        strlcpy(result.error_msg, "Parse error", sizeof(result.error_msg));
        goto done;
    }
    
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!cJSON_IsArray(data)) {
        cJSON_Delete(root);
        result.success = false;
        goto done;
    }
    
    // Extract predictions, filter by route/bound/service_type
    time_t now = time(NULL);
    int count = 0;
    
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, data) {
        if (count >= 3) break;
        
        // Filter: match route + bound + service_type
        cJSON *j_route = cJSON_GetObjectItem(item, "route");
        cJSON *j_bound = cJSON_GetObjectItem(item, "dir");
        cJSON *j_service = cJSON_GetObjectItem(item, "service_type");
        cJSON *j_eta = cJSON_GetObjectItem(item, "eta");
        
        // KMB uses "I"/"O", CTB uses "inbound"/"outbound"
        // (normalize in your comparison)
        
        if (/* filters match */) {
            // Parse ISO8601 timestamp: "2026-10-03T16:34:00+08:00"
            struct tm tm = {0};
            const char *eta_str = cJSON_GetStringValue(j_eta);
            if (eta_str && strptime(eta_str, "%Y-%m-%dT%H:%M:%S", &tm)) {
                time_t eta_time = mktime(&tm);
                int minutes = (int)((eta_time - now) / 60);
                
                if (minutes >= 0) {  // Ignore past predictions
                    result.predictions[count].minutes_until = minutes;
                    result.predictions[count].eta_timestamp = eta_time;
                    result.predictions[count].is_scheduled = false;
                    count++;
                }
            }
        }
    }
    
    result.count = count;
    result.success = (count > 0);
    result.fetched_at = now;
    
    cJSON_Delete(root);
    
done:
    // Post event to UI
    bus_event_eta_t evt = {0};
    strlcpy(evt.route, /* from context */, sizeof(evt.route));
    strlcpy(evt.stop_id, /* from context */, sizeof(evt.stop_id));
    evt.result = result;
    
    post_bus_event(BUS_EVT_ETA_RESULT, &evt, sizeof(evt));
    
    crystal_http_release_response(response);
}
```

### Step 6.2: Build ETA Screen UI

**File:** `components/bus_app/src/bus_app.cpp`

```cpp
void BusApp::build_eta_screen() {
    const lv_area_t area = getVisualArea();
    eta_screen_page_ = lv_obj_create(lv_scr_act());
    lv_obj_set_size(eta_screen_page_, lv_area_get_width(&area),
                    lv_area_get_height(&area));
    lv_obj_set_pos(eta_screen_page_, 0, 0);
    
    // Header
    lv_obj_t *header = lv_obj_create(eta_screen_page_);
    lv_obj_set_size(header, lv_area_get_width(&area), 50);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 40, 40);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) {
        auto *app = static_cast<BusApp *>(e->user_data);
        app->show_page(Page::STOP_PICKER);
    }, LV_EVENT_CLICKED, this);
    
    // Title
    eta_title_label_ = lv_label_create(header);
    char title[128];
    snprintf(title, sizeof(title), "Route %s → %s",
             selected_stop_.route, selected_stop_.stop_name_en);
    lv_label_set_text(eta_title_label_, title);
    lv_obj_align(eta_title_label_, LV_ALIGN_CENTER, 0, 0);
    
    // Content area
    lv_obj_t *content = lv_obj_create(eta_screen_page_);
    lv_obj_set_size(content, lv_area_get_width(&area), 
                    lv_area_get_height(&area) - 50);
    lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, 
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(content, 20, 0);
    
    // "Next arrivals:" label
    lv_obj_t *header_label = lv_label_create(content);
    lv_label_set_text(header_label, "Next arrivals:");
    
    // 3 ETA labels
    for (int i = 0; i < 3; i++) {
        eta_labels_[i] = lv_label_create(content);
        lv_label_set_text(eta_labels_[i], "--");
        lv_obj_set_style_text_font(eta_labels_[i], &lv_font_montserrat_28, 0);
    }
    
    // Updated label
    eta_updated_label_ = lv_label_create(content);
    lv_label_set_text(eta_updated_label_, "Loading...");
    lv_obj_set_style_text_color(eta_updated_label_, 
                                 lv_color_hex(0x888888), 0);
    
    // Save button
    save_favorite_btn_ = lv_btn_create(content);
    lv_obj_set_size(save_favorite_btn_, 200, 50);
    lv_obj_t *save_label = lv_label_create(save_favorite_btn_);
    lv_label_set_text(save_label, LV_SYMBOL_PLUS " Add to Favorites");
    lv_obj_center(save_label);
    
    lv_obj_add_event_cb(save_favorite_btn_, [](lv_event_t *e) {
        auto *app = static_cast<BusApp *>(e->user_data);
        app->on_save_favorite_clicked();
    }, LV_EVENT_CLICKED, this);
}

void BusApp::start_eta_refresh() {
    // Request ETA immediately
    request_eta();
    
    // Start 30s timer
    if (eta_refresh_timer_) {
        lv_timer_del(eta_refresh_timer_);
    }
    
    eta_refresh_timer_ = lv_timer_create([](lv_timer_t *t) {
        auto *app = static_cast<BusApp *>(t->user_data);
        app->request_eta();
    }, 30000, this);
    
    // Start 1s timer for "Updated Xs ago" text
    if (eta_age_timer_) {
        lv_timer_del(eta_age_timer_);
    }
    
    eta_age_timer_ = lv_timer_create([](lv_timer_t *t) {
        auto *app = static_cast<BusApp *>(t->user_data);
        app->update_eta_age_display();
    }, 1000, this);
}

void BusApp::request_eta() {
    if (eta_request_pending_) return;  // Don't queue duplicates
    
    eta_request_pending_ = true;
    bus_service_request_eta(selected_stop_.route,
                            selected_stop_.operator,
                            selected_stop_.bound,
                            selected_stop_.service_type,
                            selected_stop_.stop_id);
}

void BusApp::on_eta_result(const bus_event_eta_t &evt) {
    eta_request_pending_ = false;
    last_eta_result_ = evt.result;
    
    if (!evt.result.success) {
        lv_label_set_text(eta_labels_[0], "Unable to load");
        lv_label_set_text(eta_labels_[1], "");
        lv_label_set_text(eta_labels_[2], "");
        lv_label_set_text(eta_updated_label_, evt.result.error_msg);
        return;
    }
    
    // Update ETA labels
    for (int i = 0; i < 3; i++) {
        if (i < evt.result.count) {
            char buf[64];
            const auto &pred = evt.result.predictions[i];
            
            struct tm tm;
            localtime_r(&pred.eta_timestamp, &tm);
            char time_str[16];
            strftime(time_str, sizeof(time_str), "%H:%M", &tm);
            
            snprintf(buf, sizeof(buf), "🚌  %d min   (%s)",
                     pred.minutes_until, time_str);
            lv_label_set_text(eta_labels_[i], buf);
        } else {
            lv_label_set_text(eta_labels_[i], "");
        }
    }
    
    update_eta_age_display();
}

void BusApp::update_eta_age_display() {
    if (!last_eta_result_.success) return;
    
    int age = (int)(time(nullptr) - last_eta_result_.fetched_at);
    char buf[64];
    snprintf(buf, sizeof(buf), "Updated %ds ago", age);
    lv_label_set_text(eta_updated_label_, buf);
}

bool BusApp::onPause() {
    // Stop timers
    if (eta_refresh_timer_) {
        lv_timer_del(eta_refresh_timer_);
        eta_refresh_timer_ = nullptr;
    }
    if (eta_age_timer_) {
        lv_timer_del(eta_age_timer_);
        eta_age_timer_ = nullptr;
    }
    return true;
}

bool BusApp::onDestroy() {
    // Cancel pending request
    if (eta_request_pending_) {
        bus_service_cancel_owner_requests(BUS_SERVICE_OWNER_ID);
        eta_request_pending_ = false;
    }
    return true;
}
```

### Slice 6 Test

**Build and flash, then:**
1. Select route 101, pick stop #10
2. ETA screen appears with "Loading..."
3. After 1-2 seconds, see 3 arrival times
4. Wait 30 seconds → ETAs refresh automatically
5. "Updated Xs ago" increments every second
6. Back button works

**Pass criteria:** ✅ All 6 steps work

---

## Slice 7: Favorites Implementation

### Step 7.1: Favorites Persistence

**File:** `components/bus_app/src/bus_favorites.cpp` (NEW)

```cpp
#include "bus_app.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "bus_favorites";
static const char *NVS_NAMESPACE = "bus";

size_t BusApp::load_favorites(favorite_t *out, size_t max_count) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return 0;
    }
    
    size_t count = 0;
    for (size_t i = 0; i < max_count; i++) {
        char key[16];
        snprintf(key, sizeof(key), "fav.%zu", i);
        
        size_t len = sizeof(favorite_t);
        err = nvs_get_blob(handle, key, &out[count], &len);
        if (err == ESP_OK && len == sizeof(favorite_t)) {
            count++;
        } else {
            break;  // No more favorites
        }
    }
    
    nvs_close(handle);
    ESP_LOGI(TAG, "loaded %zu favorites", count);
    return count;
}

void BusApp::save_favorites(const favorite_t *favs, size_t count) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to open NVS: %s", esp_err_to_name(err));
        return;
    }
    
    // Save each favorite
    for (size_t i = 0; i < count; i++) {
        char key[16];
        snprintf(key, sizeof(key), "fav.%zu", i);
        nvs_set_blob(handle, key, &favs[i], sizeof(favorite_t));
    }
    
    // Erase slots beyond count (if favorites were deleted)
    for (size_t i = count; i < 8; i++) {
        char key[16];
        snprintf(key, sizeof(key), "fav.%zu", i);
        nvs_erase_key(handle, key);  // OK if key doesn't exist
    }
    
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "saved %zu favorites", count);
}
```

### Step 7.2: Favorite Card Widget

**File:** `components/bus_app/src/bus_eta_card.cpp` (NEW)

```cpp
#include "bus_app.h"

FavoriteCard::FavoriteCard(lv_obj_t *parent, const favorite_t &fav, 
                           uint8_t index, BusApp *app)
    : favorite_(fav), index_(index), app_(app) {
    
    // Card container
    card_ = lv_obj_create(parent);
    lv_obj_set_size(card_, lv_pct(100), 100);
    lv_obj_set_style_radius(card_, 8, 0);
    lv_obj_set_style_border_width(card_, 1, 0);
    lv_obj_set_style_pad_all(card_, 10, 0);
    
    // Route label
    route_label_ = lv_label_create(card_);
    char text[128];
    snprintf(text, sizeof(text), "%s → %s", fav.route, fav.stop_name_en);
    lv_label_set_text(route_label_, text);
    lv_obj_set_style_text_font(route_label_, &lv_font_montserrat_20, 0);
    lv_obj_align(route_label_, LV_ALIGN_TOP_LEFT, 0, 0);
    
    // ETA container (horizontal)
    lv_obj_t *eta_row = lv_obj_create(card_);
    lv_obj_set_size(eta_row, lv_pct(100), 30);
    lv_obj_align(eta_row, LV_ALIGN_TOP_LEFT, 0, 30);
    lv_obj_set_flex_flow(eta_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(eta_row, LV_FLEX_ALIGN_START, 
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(eta_row, 0, 0);
    lv_obj_set_style_bg_opa(eta_row, LV_OPA_TRANSP, 0);
    
    for (int i = 0; i < 3; i++) {
        eta_labels_[i] = lv_label_create(eta_row);
        lv_label_set_text(eta_labels_[i], "--");
        lv_obj_set_style_pad_right(eta_labels_[i], 20, 0);
    }
    
    // Updated label
    updated_label_ = lv_label_create(card_);
    lv_label_set_text(updated_label_, "Loading...");
    lv_obj_set_style_text_color(updated_label_, lv_color_hex(0x888888), 0);
    lv_obj_align(updated_label_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    
    // Make card clickable to open ETA screen
    lv_obj_add_flag(card_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card_, [](lv_event_t *e) {
        auto *card = static_cast<FavoriteCard *>(e->user_data);
        card->on_card_clicked();
    }, LV_EVENT_CLICKED, this);
}

void FavoriteCard::update_etas(const bus_eta_result_t &result) {
    if (!result.success) {
        lv_label_set_text(eta_labels_[0], "Unable to load");
        lv_label_set_text(eta_labels_[1], "");
        lv_label_set_text(eta_labels_[2], "");
        lv_label_set_text(updated_label_, result.error_msg);
        return;
    }
    
    for (int i = 0; i < 3; i++) {
        if (i < result.count) {
            char buf[32];
            snprintf(buf, sizeof(buf), "🚌 %d min", 
                     result.predictions[i].minutes_until);
            lv_label_set_text(eta_labels_[i], buf);
        } else {
            lv_label_set_text(eta_labels_[i], "");
        }
    }
    
    int age = (int)(time(nullptr) - result.fetched_at);
    char updated[64];
    snprintf(updated, sizeof(updated), "Updated %ds ago", age);
    lv_label_set_text(updated_label_, updated);
}

void FavoriteCard::on_card_clicked() {
    // Tell app to open ETA screen for this favorite
    app_->open_eta_for_favorite(favorite_);
}
```

### Step 7.3: Favorites Tab UI

**File:** `components/bus_app/src/bus_app.cpp`

```cpp
void BusApp::build_favorites_tab() {
    // Already created in onCreate(), now populate it
    
    // Load favorites from NVS
    favorite_count_ = load_favorites(favorites_, 8);
    
    if (favorite_count_ == 0) {
        // Empty state
        lv_obj_t *empty = lv_label_create(favorites_tab_);
        lv_label_set_text(empty, "No favorites yet\nAdd stops from the ETA screen");
        lv_obj_center(empty);
        return;
    }
    
    // Create favorite cards
    for (size_t i = 0; i < favorite_count_; i++) {
        FavoriteCard *card = new FavoriteCard(favorites_tab_, favorites_[i], 
                                               i, this);
        favorite_cards_.push_back(card);
    }
    
    // Start staggered ETA requests
    refresh_all_favorites();
    
    // Start 30s refresh timer
    favorites_refresh_timer_ = lv_timer_create([](lv_timer_t *t) {
        auto *app = static_cast<BusApp *>(t->user_data);
        app->refresh_all_favorites();
    }, 30000, this);
}

void BusApp::refresh_all_favorites() {
    for (size_t i = 0; i < favorite_count_; i++) {
        // Stagger by 2 seconds each to avoid burst
        lv_timer_t *delay = lv_timer_create([](lv_timer_t *t) {
            auto *app = static_cast<BusApp *>(lv_timer_get_user_data(t));
            size_t idx = (size_t)t->user_data;
            
            const auto &fav = app->favorites_[idx];
            bus_service_request_eta(fav.route, fav.operator, fav.bound,
                                    fav.service_type, fav.stop_id);
            
            lv_timer_del(t);  // One-shot
        }, i * 2000, nullptr);
        lv_timer_set_user_data(delay, this);
        delay->user_data = (void *)i;
    }
}

void BusApp::on_eta_result_for_favorite(const bus_event_eta_t &evt) {
    // Find which favorite this is for
    for (size_t i = 0; i < favorite_count_; i++) {
        if (strcmp(favorites_[i].stop_id, evt.stop_id) == 0) {
            if (i < favorite_cards_.size()) {
                favorite_cards_[i]->update_etas(evt.result);
            }
            break;
        }
    }
}

void BusApp::on_save_favorite_clicked() {
    if (favorite_count_ >= 8) {
        crystal_toast("Maximum 8 favorites");
        return;
    }
    
    // Check if already saved
    for (size_t i = 0; i < favorite_count_; i++) {
        if (strcmp(favorites_[i].stop_id, selected_stop_.stop_id) == 0) {
            crystal_toast("Already in favorites");
            return;
        }
    }
    
    // Add new favorite
    favorite_t &fav = favorites_[favorite_count_];
    strlcpy(fav.route, selected_stop_.route, sizeof(fav.route));
    fav.operator = selected_stop_.operator;
    strlcpy(fav.bound, selected_stop_.bound, sizeof(fav.bound));
    fav.service_type = selected_stop_.service_type;
    strlcpy(fav.stop_id, selected_stop_.stop_id, sizeof(fav.stop_id));
    strlcpy(fav.stop_name_en, selected_stop_.stop_name_en, 
            sizeof(fav.stop_name_en));
    strlcpy(fav.stop_name_tc, selected_stop_.stop_name_tc, 
            sizeof(fav.stop_name_tc));
    favorite_count_++;
    
    // Save to NVS
    save_favorites(favorites_, favorite_count_);
    
    crystal_toast("Added to favorites");
    
    // Update button
    lv_obj_t *label = lv_obj_get_child(save_favorite_btn_, 0);
    lv_label_set_text(static_cast<lv_label_t *>(label), 
                      LV_SYMBOL_OK " Saved");
    lv_obj_add_state(save_favorite_btn_, LV_STATE_DISABLED);
}
```

### Slice 7 Test

1. Add 3 stops to favorites from ETA screen
2. Go to Favorites tab → see 3 cards
3. Each card loads ETAs staggered (0s, 2s, 4s)
4. Wait 30s → all cards refresh
5. Reboot device → 3 favorites still there
6. Tap a card → opens ETA screen for that stop

**Pass criteria:** ✅ All 6 steps work

---

## Final Integration Test

After all three slices work:

```bash
# Full flow test
1. Search route 101
2. Select inbound → 35 stops
3. Tap stop #10
4. See 3 ETAs, wait for refresh
5. Save to favorites
6. Go to Favorites tab
7. See card with ETAs
8. Reboot
9. Card still there with ETAs loading

# Memory test
10. Switch tabs 20 times
11. Open/close ETA 10 times
12. Check heap: no leak
```

**When this passes → Bus app is complete!**

## Build Commands

```bash
cd /Users/szemy/Workspace/ESP32\ Crystal\ OS
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
idf.py build
idf.py -p /dev/cu.usbmodem1201 flash monitor -b 2000000
```

## Common Issues

**Issue: ETAs always show "Unable to load"**
- Check crystal_http logs for HTTP errors
- Verify URL format matches API docs
- Check if filtering logic matches response format

**Issue: Favorites don't persist**
- Check NVS namespace is "bus"
- Verify nvs_commit() is called
- Check NVS partition isn't full

**Issue: Memory leak on tab switching**
- Make sure timers are deleted in onPause()
- Check ETA requests are cancelled in onDestroy()
- Verify favorite cards are deleted when rebuilding

**Issue: Watchdog timeout during ETA refresh**
- Check you're not blocking LVGL thread
- Verify parsing happens in service task, not UI
- Check 30s timer doesn't queue duplicate requests

## Exit Checklist

Bus app is complete when:

- [ ] Stop picker shows real names
- [ ] ETA screen shows 3 arrival times
- [ ] ETAs auto-refresh every 30s
- [ ] Save button adds to favorites
- [ ] Favorites tab shows up to 8 cards
- [ ] Each card fetches its own ETAs
- [ ] Favorites persist across reboot
- [ ] No memory leaks over 30 minutes
- [ ] No watchdog warnings
- [ ] Works offline (shows "No connection")

When all 10 check → **Done! Move to Phase 12-13 app platform.**
