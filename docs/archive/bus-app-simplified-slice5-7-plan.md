# Simplified Slice 5-7: Complete Bus App User Flow

**Date:** 2026-10-03  
**Status:** Planned  
**Estimated time:** 3-5 days  
**Depends on:** Slice 4R.4-4R.6 complete (stop catalog lookup working)

## Goal

Build the complete user experience: pick a stop from a list, see live ETAs, save favorites, and see them refresh automatically. Use the simplest implementation that demonstrates the full pattern.

## Architecture Decisions (Simplified)

**What we're building:**
- Stop picker: scrollable list with real names
- ETA screen: live arrival times, 30s auto-refresh, save button
- Favorites: 8 cards, each shows next 3 ETAs

**What we're NOT building (defer for later):**
- Co-operated route merging (just show KMB or CTB, not both)
- Direction/destination validation (use what the API returns)
- Offline queue/retry logic (just show "No connection")
- Edit mode with drag-reorder (just add/remove)
- Language switching (just show English or whatever is in catalog)

## Slice 5: Stop Picker (1 day)

**Goal:** Show a scrollable list of stops with real names, tap one to select it.

**UI Structure:**
```
┌─────────────────────────────────┐
│  < Route 101 Inbound            │  ← header with route info
├─────────────────────────────────┤
│  1. Hang Hau Station            │
│  2. Sheung Tak Estate           │
│  3. Po Lam Estate               │
│  ...                            │
│  35. Jordan MTR Station         │  ← scrollable list
└─────────────────────────────────┘
```

**Data flow:**
1. User selects route variant in Search → service fetches route-stops
2. Service enriches with catalog lookup (already done in 4R.5)
3. Event arrives with `stop_list[]` containing `{stop_id, sequence, name_en, name_tc, lat, lon}`
4. UI renders scrollable list
5. User taps row → save selection, navigate to ETA screen

**State to save:**
```cpp
struct SelectedStop {
    char route[16];
    bus_operator_t operator;
    char bound[16];           // "inbound" or "outbound"
    uint8_t service_type;
    char stop_id[16];
    char stop_name_en[64];
    char stop_name_tc[64];
    uint16_t sequence;
};
```

**Implementation:**
- `lv_list` or `lv_obj` with flex layout (vertical)
- One label per stop: "1. Stop Name"
- `LV_EVENT_CLICKED` → save selected stop, push ETA screen
- Back button → return to Search

**Exit criteria:**
- [ ] Route 101 shows 35 stops with real names
- [ ] Tapping stop #10 logs the complete selection
- [ ] Back button returns to Search
- [ ] No memory leaks on repeated open/back cycles

## Slice 6: ETA Screen (1-2 days)

**Goal:** Show live arrival predictions for the selected stop, auto-refresh every 30s, with a save button.

**UI Structure:**
```
┌─────────────────────────────────┐
│  < Route 101 → Jordan MTR       │  ← header
├─────────────────────────────────┤
│  Next arrivals:                 │
│                                 │
│  🚌  2 min   (16:34)            │  ← next 3 arrivals
│  🚌  8 min   (16:40)            │
│  🚌  15 min  (16:47)            │
│                                 │
│  Updated 5s ago                 │  ← freshness
│                                 │
│  [⭐ Add to Favorites]           │  ← save button
└─────────────────────────────────┘
```

**API Request:**
- KMB: `GET https://data.etabus.gov.hk/v1/transport/kmb/stop-eta/{stop_id}`
  - Filter by route + direction + service_type
- CTB: `GET https://rt.data.gov.hk/v2/transport/citybus/eta/{company}/stop/{stop_id}`
  - Filter by route

**Data structure:**
```c
typedef struct {
    int minutes_until;      // derived from eta_timestamp - now
    time_t eta_timestamp;   // absolute arrival time
    char remark[32];        // e.g., "Scheduled", "Delayed"
    bool is_scheduled;      // vs real-time
} bus_eta_prediction_t;

typedef struct {
    bus_eta_prediction_t predictions[3];  // next 3 only
    uint8_t count;
    time_t fetched_at;
    bool success;
} bus_eta_result_t;
```

**Implementation:**
- On screen open: submit ETA request via `crystal_http`
- Parse JSON, extract next 3 predictions, sort by time
- Start `lv_timer` for 30s refresh
- On `onPause()`: stop timer
- On `onDestroy()`: cancel pending request

**30-second refresh pattern:**
```cpp
static void eta_refresh_timer_cb(lv_timer_t *timer) {
    BusApp *app = static_cast<BusApp *>(timer->user_data);
    
    // Don't queue duplicate requests
    if (app->eta_request_pending_) return;
    
    app->eta_request_pending_ = true;
    bus_service_request_eta(app->selected_stop_);
}

bool BusApp::onCreate() {
    // ...
    eta_refresh_timer_ = lv_timer_create(eta_refresh_timer_cb, 30000, this);
}

bool BusApp::onPause() {
    if (eta_refresh_timer_) {
        lv_timer_del(eta_refresh_timer_);
        eta_refresh_timer_ = nullptr;
    }
}
```

**Save to favorites:**
```cpp
void BusApp::on_save_favorite_clicked() {
    // Read existing favorites from NVS
    Favorite favs[8] = {};
    size_t count = load_favorites(favs, 8);
    
    if (count >= 8) {
        crystal_toast("Maximum 8 favorites");
        return;
    }
    
    // Add new favorite
    favs[count] = {
        .route = selected_stop_.route,
        .operator = selected_stop_.operator,
        .bound = selected_stop_.bound,
        .service_type = selected_stop_.service_type,
        .stop_id = selected_stop_.stop_id,
        .stop_name_en = selected_stop_.stop_name_en,
        // ...
    };
    count++;
    
    // Save back to NVS
    save_favorites(favs, count);
    
    crystal_toast("Added to favorites");
    
    // Update button to show "⭐ Saved"
    lv_obj_add_state(save_btn_, LV_STATE_CHECKED);
}
```

**Exit criteria:**
- [ ] ETA screen shows 3 arrival times
- [ ] Times update every 30s automatically
- [ ] "Updated Xs ago" text updates every second
- [ ] Save button adds to favorites (check NVS)
- [ ] No refresh while app is paused
- [ ] Back button returns to stop picker

## Slice 7: Favorites Tab (1-2 days)

**Goal:** Show favorite stops as cards, each fetching and displaying its own ETAs.

**UI Structure:**
```
┌─────────────────────────────────┐
│  Favorites                      │
├─────────────────────────────────┤
│  ┌───────────────────────────┐ │
│  │ 101 → Jordan MTR          │ │
│  │ 🚌 2 min  🚌 8 min  🚌 15  │ │  ← card 1
│  │ Updated 10s ago           │ │
│  └───────────────────────────┘ │
│                                 │
│  ┌───────────────────────────┐ │
│  │ 10 → Causeway Bay         │ │
│  │ 🚌 5 min  🚌 12 min        │ │  ← card 2
│  │ Updating...               │ │
│  └───────────────────────────┘ │
│                                 │
│  [Edit]                         │  ← edit mode button
└─────────────────────────────────┘
```

**Data flow:**
1. On `onCreate()`: load favorites from NVS
2. For each favorite: render card with cached data (if any)
3. Start staggered ETA requests (0s, 2s, 4s... to avoid burst)
4. As each ETA arrives: update that card
5. Start 30s timer to refresh all favorites

**NVS storage:**
```c
// Key: "bus.fav.0" through "bus.fav.7"
typedef struct {
    char route[16];
    uint8_t operator;
    char bound[16];
    uint8_t service_type;
    char stop_id[16];
    char stop_name_en[64];
    char stop_name_tc[64];
} favorite_t;

// Helper functions
size_t load_favorites(favorite_t *out, size_t max_count);
void save_favorites(const favorite_t *favs, size_t count);
```

**Card implementation:**
```cpp
class FavoriteCard {
public:
    FavoriteCard(lv_obj_t *parent, const favorite_t &fav, uint8_t index);
    void update_etas(const bus_eta_result_t &result);
    void show_loading();
    void show_error();
    
private:
    lv_obj_t *card_;
    lv_obj_t *route_label_;
    lv_obj_t *eta_labels_[3];
    lv_obj_t *updated_label_;
    favorite_t favorite_;
    uint8_t index_;
};

void FavoriteCard::update_etas(const bus_eta_result_t &result) {
    if (!result.success) {
        lv_label_set_text(eta_labels_[0], "Unable to load");
        return;
    }
    
    for (int i = 0; i < 3 && i < result.count; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "🚌 %d min", result.predictions[i].minutes_until);
        lv_label_set_text(eta_labels_[i], buf);
    }
    
    // Update freshness
    int age = (int)(time(nullptr) - result.fetched_at);
    char updated[32];
    snprintf(updated, sizeof(updated), "Updated %ds ago", age);
    lv_label_set_text(updated_label_, updated);
}
```

**Staggered request pattern:**
```cpp
void BusApp::refresh_all_favorites() {
    for (size_t i = 0; i < favorite_count_; i++) {
        // Stagger requests by 2 seconds each to avoid burst
        lv_timer_t *delay = lv_timer_create([](lv_timer_t *t) {
            auto *app = static_cast<BusApp *>(t->user_data);
            size_t idx = (size_t)lv_timer_get_user_data(t);
            
            bus_service_request_eta(&app->favorites_[idx]);
            lv_timer_del(t);  // one-shot
        }, i * 2000, this);
        lv_timer_set_user_data(delay, (void *)i);
    }
}
```

**Edit mode:**
```cpp
void BusApp::enter_edit_mode() {
    edit_mode_ = true;
    
    for (auto *card : favorite_cards_) {
        // Add delete button to each card
        lv_obj_t *del_btn = lv_btn_create(card->container());
        lv_obj_set_size(del_btn, 40, 40);
        lv_obj_align(del_btn, LV_ALIGN_TOP_RIGHT, -5, 5);
        
        lv_obj_t *label = lv_label_create(del_btn);
        lv_label_set_text(label, LV_SYMBOL_TRASH);
        
        lv_obj_add_event_cb(del_btn, [](lv_event_t *e) {
            auto *app = static_cast<BusApp *>(e->user_data);
            auto *card = static_cast<FavoriteCard *>(lv_event_get_user_data(e));
            app->delete_favorite(card->index());
        }, LV_EVENT_CLICKED, this);
    }
}

void BusApp::delete_favorite(uint8_t index) {
    // Remove from array
    for (size_t i = index; i < favorite_count_ - 1; i++) {
        favorites_[i] = favorites_[i + 1];
    }
    favorite_count_--;
    
    // Save to NVS
    save_favorites(favorites_, favorite_count_);
    
    // Rebuild UI
    rebuild_favorites_view();
}
```

**Exit criteria:**
- [ ] Up to 8 favorite cards render on tab open
- [ ] Each card shows route/stop name
- [ ] ETAs update automatically every 30s
- [ ] Tapping card opens ETA screen for that stop
- [ ] Edit mode shows delete buttons
- [ ] Deleting a favorite removes it and updates NVS
- [ ] Favorites survive app restart (check NVS persistence)
- [ ] Empty state shows "No favorites yet"

## Integration Testing

After all three slices:

**Test 1: Complete flow**
1. Search for route 101
2. Select inbound
3. See 35 stops with names
4. Tap stop #10 "Jordan MTR"
5. See 3 ETAs updating
6. Wait 30s → ETAs refresh automatically
7. Tap "Add to Favorites"
8. Go to Favorites tab
9. See card with "101 → Jordan MTR"
10. Card shows ETAs

**Test 2: Multiple favorites**
1. Add 3 different stops to favorites
2. All 3 cards show on Favorites tab
3. All 3 refresh every 30s
4. Edit mode → delete one → 2 remain
5. Reboot device → 2 favorites still there

**Test 3: Error handling**
1. Turn off Wi-Fi
2. Open ETA screen → shows "No connection"
3. Turn on Wi-Fi
4. Next refresh succeeds

**Test 4: Memory stability**
1. Switch between tabs 20 times
2. Open/close ETA screen 10 times
3. Check heap: no progressive leak
4. Check logs: no watchdog warnings

## Deferred Features

These are intentionally NOT in Slice 5-7:

- ❌ Co-operated route merging (KMB + CTB on same route)
- ❌ Multiple operator ETA requests and sorting
- ❌ Language switching (EN/TC toggle)
- ❌ Offline queue (save failed requests, retry on reconnect)
- ❌ Favorite reordering (drag to rearrange)
- ❌ Favorite groups/folders
- ❌ Push notifications for favorite arrivals
- ❌ Route map view with stop pins
- ❌ Nearby tab (needs location)

All of these can be added later. For now, just demonstrate the core pattern:
**Search → Pick → Watch → Save → Refresh**

## Success Criteria

The bus app is "done" when:

1. ✅ A user can find a route by number
2. ✅ They see real stop names (not "Stop 123")
3. ✅ They see live arrival times
4. ✅ Times refresh automatically
5. ✅ They can save up to 8 favorites
6. ✅ Favorites show on app open with live ETAs
7. ✅ Everything survives reboot
8. ✅ No memory leaks or crashes over 30 minutes of use

When those 8 checks pass → **Bus app is complete as a reference implementation.**

You can then:
- Use it as the `CrystalApp` pattern example
- Transform it to `.lua` for Phase 14-17 testing
- Move to Phase 12-13 app platform core
- Come back later to add co-operated routes, language switching, etc.

## Time Estimate

| Slice | Work | Time |
|-------|------|------|
| 5 | Stop picker UI + selection | 1 day |
| 6 | ETA screen + 30s refresh + save | 1-2 days |
| 7 | Favorites tab + cards + edit | 1-2 days |
| **Total** | | **3-5 days** |

Add 1 day buffer for integration testing and bug fixes → **4-6 days total to complete bus app**.
