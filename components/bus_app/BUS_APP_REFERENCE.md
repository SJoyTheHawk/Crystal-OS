# BusApp Class Reference

**File:** `components/bus_app/include/bus_app.hpp`, `components/bus_app/src/bus_app.cpp`  
**Inherits:** `CrystalApp`  
**Purpose:** Hong Kong bus arrival time tracker with route search and favorites management

---

## Table of Contents

1. [Class Overview](#class-overview)
2. [Public Interface](#public-interface)
   - [Constructor](#constructor)
   - [Lifecycle Methods](#lifecycle-methods-override-from-crystalapp)
3. [Private Members](#private-members)
   - [Data Structures](#data-structures)
   - [UI Object Pointers](#ui-object-pointers)
   - [State Variables](#state-variables)
   - [Timers](#timers)
4. [Private Methods](#private-methods)
   - [UI Builders](#ui-builders)
   - [Data Management](#data-management)
   - [Event Handlers](#event-handlers-static)
5. [Anonymous Namespace Helpers](#anonymous-namespace-helpers)
6. [Constants](#constants)
7. [Known Issues & TODOs](#known-issues--todos)
8. [Workflow Gaps](#workflow-gaps)
9. [Dependencies](#dependencies)
10. [Usage Example](#usage-example)

---

## Class Overview

BusApp provides a two-tab interface:
1. **Favorites Tab** - Displays saved bus stops with live ETA updates
2. **Search Tab** - Route number input with adaptive keypad

The app integrates with `bus_service` for API calls and stores favorites in NVS.

---

## Public Interface

### Constructor
```cpp
BusApp()
```
- Initializes base `CrystalApp("Bus", &bus_icon)`
- Calls `bus_icon_prepare()` to render the launcher icon

### Lifecycle Methods (Override from CrystalApp)

#### `bool onCreate()`
**Purpose:** Initialize app UI and service connection  
**Called:** When app is first launched  
**Returns:** `true` on success

**Actions:**
1. Retrieves visual area dimensions from `getVisualArea()`
2. Creates root container sized to display area, positioned at (0,0)
3. Initializes `bus_service` and registers event listener
4. Loads favorites from NVS
5. Builds UI: tab bar, favorites tab, search tab
6. Starts 30-second ETA refresh timer

**Known Issue:** Currently has display sizing bug - UI renders at ~40×40 instead of 480×480

---

#### `bool onPause()`
**Purpose:** Suspend background work when app loses focus  
**Called:** When user switches away from app  
**Returns:** `true`

**Actions:**
1. Deletes ETA refresh timer
2. Cancels all pending bus service requests
3. Saves current favorites to NVS

---

#### `bool onResume()`
**Purpose:** Resume background work when app gains focus  
**Called:** When user returns to app  
**Returns:** `true`

**Actions:**
1. Recreates 30-second ETA refresh timer
2. Triggers immediate refresh of favorite ETAs

---

#### `bool onDestroy()`
**Purpose:** Clean up all resources  
**Called:** When app is closed  
**Returns:** `true`

**Actions:**
1. Unregisters bus service event listener
2. Cancels all pending requests
3. Deletes ETA refresh timer
4. Nulls all LVGL object pointers (no explicit deletion - managed by LVGL)

---

#### `bool onBack()`
**Purpose:** Handle system back button  
**Called:** When user presses back button  
**Returns:** `true` if handled, `false` to exit app

**Logic:**
- If search buffer has text → clear it and return `true` (stay in app)
- Otherwise → return `false` (let shell exit app)

---

## Private Members

### Data Structures

#### `struct Favorite`
**Purpose:** Represents a saved bus stop  
**Storage:** NVS under key `"fav_dat"`

| Field | Type | Size | Description |
|-------|------|------|-------------|
| `route` | `char[]` | 5 bytes | Route number (e.g., "6X", "796C") |
| `op` | `uint8_t` | 1 byte | Operator ID (0=KMB, 1=CTB, 2=NLB) |
| `bound` | `char` | 1 byte | Direction ('O'=outbound, 'I'=inbound) |
| `service_type` | `uint8_t` | 1 byte | Service variant (1=normal, 2=special) |
| `stop_id` | `char[]` | 20 bytes | API stop identifier |
| `stop_name` | `char[]` | 60 bytes | Display name in Chinese/English |
| `last_eta` | `bus_eta_result_t` | varies | Cached ETA data from last refresh |

---

### UI Object Pointers

| Variable | Type | Purpose |
|----------|------|---------|
| `root_` | `lv_obj_t*` | Main container sized to visual area |
| `tab_bar_` | `lv_obj_t*` | Top navigation bar (50px height) |
| `tab_view_` | `lv_obj_t*` | ⚠️ **UNUSED** - leftover from design iteration |
| `favorites_tab_` | `lv_obj_t*` | Container for favorites list |
| `search_tab_` | `lv_obj_t*` | Container for route search UI |
| `favorite_list_` | `lv_obj_t*` | Scrollable flex container of favorite cards |
| `search_input_` | `lv_obj_t*` | Large label showing typed route number |
| `keypad_container_` | `lv_obj_t*` | Contains number grid + letter strip |

---

### State Variables

| Variable | Type | Purpose |
|----------|------|---------|
| `favorites_[]` | `Favorite[8]` | Fixed array of saved stops |
| `favorites_count_` | `uint8_t` | Number of valid entries in `favorites_[]` |
| `current_request_id_` | `uint32_t` | ⚠️ **UNUSED** - was for tracking async requests |
| `current_route_` | `bus_route_variant_t` | ⚠️ **UNUSED** - intended for direction chooser |
| `search_buffer_` | `char[5]` | User's typed route number (max 4 chars + null) |

---

### Timers

| Variable | Type | Purpose |
|----------|------|---------|
| `eta_refresh_timer_` | `lv_timer_t*` | Fires every 30 seconds to update ETAs |

**Callback:** `onRefreshTimer()` → calls `refreshFavoriteETAs()`

---

## Private Methods

### UI Builders

#### `void buildTabBar(lv_coord_t width)`
**Purpose:** Create top navigation with two tab buttons  
**Called by:** `onCreate()`

**Layout:**
- Fixed 50px height flex row
- Two buttons: "🏠 Favorites" and "⌨ Search"
- User data: button 0 = favorites, button 1 = search
- Event: `LV_EVENT_CLICKED` → `onTabChanged()`

---

#### `void buildFavoritesTab(lv_coord_t width, lv_coord_t height, lv_coord_t tab_bar_height)`
**Purpose:** Build favorites list or empty state  
**Called by:** `onCreate()`

**Logic:**
- If `favorites_count_ == 0` → show centered "No saved stops" message
- Otherwise → create scrollable flex column with favorite cards

**Favorite Card Structure:**
- 80px height card with 16px padding
- Route number (28pt, top-left)
- Stop name (16pt, 28px below route)
- ETA times (16pt, 52px below route) - shows up to 3 arrivals

---

#### `void buildSearchTab(lv_coord_t width, lv_coord_t height, lv_coord_t tab_bar_height)`
**Purpose:** Build route search interface  
**Called by:** `onCreate()`

**Components:**
1. Large label at top (48pt font) for displaying typed route
2. Keypad container (built by `buildKeypad()`)
3. Hidden by default (`LV_OBJ_FLAG_HIDDEN`)

---

#### `void buildKeypad(lv_coord_t width)`
**Purpose:** Create route number input keypad  
**Called by:** `buildSearchTab()`

**Layout:**
- **Number grid:** 4×3 grid of 60×60px buttons
  - Digits 1-9 in rows 0-2
  - Digit 0 in row 3, center column
  - 40px left offset, 8px spacing
- **Letter strip:** Vertical scrollable column on right edge
  - 60×60px letter tiles (A-Z minus numbers)
  - Shows 4 tiles, scroll for more
  - Positioned at `width * 9 / 10 - 60px`
- **Backspace:** Red button at top-right of number grid
- **Enter:** Green button below backspace

**Events:**
- Number/letter buttons → `onKeyPressed()`
- Backspace → `onBackspace()`
- Enter → `onEnter()`

---

### Data Management

#### `void loadFavoritesFromNVS()`
**Purpose:** Load saved favorites from non-volatile storage  
**Called by:** `onCreate()`

**Logic:**
1. Reads up to `sizeof(favorites_)` bytes from NVS key `"fav_dat"`
2. Calculates count as `bytes_read / sizeof(Favorite)`
3. Validates count ≤ `MAX_FAVORITES` (8)
4. Sets `favorites_count_`

---

#### `void saveFavoritesToNVS()`
**Purpose:** Persist current favorites to NVS  
**Called by:** `onPause()`

**Logic:**
- Only writes if `favorites_count_ > 0`
- Writes exactly `favorites_count_ * sizeof(Favorite)` bytes

---

#### `void refreshFavoriteETAs()`
**Purpose:** Request fresh ETA data for all favorites  
**Called by:** `onResume()`, `onRefreshTimer()`

**Logic:**
- Loops through `favorites_[]` array
- For each: calls `bus_service_request_eta()` with stop_id, route, op, service_type
- Response arrives asynchronously via `onBusEvent()`

---

#### `void updateFavoriteCard(int index)`
**Purpose:** Refresh UI for one favorite after ETA update  
**Status:** ⚠️ **STUB** - not implemented

**TODO:**
- Locate the card widget by index
- Update the ETA label with `favorites_[index].last_eta`
- Currently just logs and returns

---

#### `void showError(const char *message)`
**Purpose:** Display error message to user  
**Status:** ⚠️ **STUB** - only logs to serial

**TODO:**
- Show modal toast or alert dialog
- Auto-dismiss after 3 seconds

---

#### `void updateKeypadState()`
**Purpose:** Grey out invalid keys based on route trie  
**Status:** ⚠️ **PARTIAL** - calls trie functions but doesn't update UI

**TODO:**
- Call `bus_route_next_mask()` to get valid next characters
- Loop through all keypad buttons
- Set opacity or disabled state based on mask

---

### Event Handlers (Static)

All static handlers receive `this` pointer via `lv_event_get_user_data()`.

#### `void onBusEvent(const bus_event_t *event, void *user_data)`
**Purpose:** Process async responses from bus service  
**Registered:** `onCreate()` via `bus_service_set_listener()`

**Event Types:**
| Event | Action |
|-------|--------|
| `BUS_EVT_ETA` | Match stop_id → update `favorites_[i].last_eta` → call `updateFavoriteCard()` |
| `BUS_EVT_ROUTE_VARIANTS` | ⚠️ **TODO** - show direction chooser (currently just logs + frees) |
| `BUS_EVT_STOPS_LIST` | ⚠️ **TODO** - show stop picker (currently just logs + frees) |
| `BUS_EVT_ERROR` | Call `showError()` |

---

#### `void onTabChanged(lv_event_t *e)`
**Purpose:** Switch between favorites and search tabs  
**Trigger:** Click on tab bar button

**Logic:**
- Reads tab index from button's user data
- If 0: show `favorites_tab_`, hide `search_tab_`
- If 1: hide `favorites_tab_`, show `search_tab_`

---

#### `void onFavoriteClicked(lv_event_t *e)`
**Purpose:** Open ETA detail view for a favorite  
**Status:** ⚠️ **STUB** - only logs

**TODO:**
- Retrieve favorite index from event user data
- Create new screen with full stop schedule
- Show refresh button, remove from favorites button

---

#### `void onKeyPressed(lv_event_t *e)`
**Purpose:** Append character to search buffer  
**Trigger:** Click on number or letter button

**Logic:**
1. Extract first character from button label
2. If buffer length < 4: append character
3. Update `search_input_` label text
4. Call `updateKeypadState()`

---

#### `void onBackspace(lv_event_t *e)`
**Purpose:** Remove last character from search buffer  
**Trigger:** Click backspace button

**Logic:**
1. If buffer not empty: remove last character
2. Update `search_input_` label text
3. Call `updateKeypadState()`

---

#### `void onEnter(lv_event_t *e)`
**Purpose:** Submit route search  
**Trigger:** Click enter button

**Logic:**
1. Validate route via `bus_route_is_complete()`
2. If valid: call `bus_service_request_route(search_buffer_)`
3. Store request ID in `current_request_id_`
4. Wait for `BUS_EVT_ROUTE_VARIANTS` in `onBusEvent()`

---

#### `void onRefreshTimer(lv_timer_t *timer)`
**Purpose:** Periodic ETA refresh  
**Trigger:** Every 30 seconds

**Logic:**
- Null-checks `app->root_` (app may be destroyed)
- Calls `refreshFavoriteETAs()`

---

## Anonymous Namespace Helpers

These are file-local utility functions in `bus_app.cpp`.

### `lv_obj_t *makeCard(lv_obj_t *parent)`
**Purpose:** Create styled card container  
**Returns:** LVGL object with rounded corners, border, padding

**Style:**
- Background: `kCardBg` (0x1E293B - dark blue)
- Border: 1px `kBorder` (0x334155 - grey)
- Radius: 8px
- Padding: 16px
- Non-scrollable

---

### `lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color)`
**Purpose:** Create styled label  
**Returns:** LVGL label with specified font and color

---

### `lv_obj_t *makeButton(lv_obj_t *parent, const char *text, lv_coord_t width, lv_coord_t height)`
**Purpose:** Create styled button with centered label  
**Returns:** LVGL button with accent color background

**Style:**
- Background: `kAccent` (0x38BDF8 - sky blue)
- Radius: 8px

**⚠️ NOTE:** Currently unused in the code.

---

### `void formatETA(int32_t minutes, char *buf, size_t size)`
**Purpose:** Format ETA minutes as display string

**Logic:**
- `< 1 min` → "Due"
- `≥ 1 min` → "{N} min"

---

## Constants

### NVS Keys
- `KEY_FAV_COUNT` = `"fav_cnt"` (unused - count inferred from data size)
- `KEY_FAV_DATA` = `"fav_dat"`

### Colors (Slate theme)
| Constant | Hex | Purpose |
|----------|-----|---------|
| `kBgColor` | 0x0F172A | Main background (slate-900) |
| `kCardBg` | 0x1E293B | Card background (slate-800) |
| `kTextPrimary` | 0xF8FAFC | Main text (slate-50) |
| `kTextSecondary` | 0x94A3B8 | Secondary text (slate-400) |
| `kBorder` | 0x334155 | Card borders (slate-700) |
| `kAccent` | 0x38BDF8 | Interactive elements (sky-400) |

### Layout
- `kPad` = 16px
- `kGap` = 12px

---

## Known Issues & TODOs

### Critical Bugs
1. **Display sizing broken** - UI renders at ~40×40px instead of 480×480
   - Likely: `getVisualArea()` returning wrong dimensions
   - Need: Log output from `onCreate()` showing actual area values

### Incomplete Features
1. **Direction Chooser** - `BUS_EVT_ROUTE_VARIANTS` handler only logs
2. **Stop Picker** - `BUS_EVT_STOPS_LIST` handler only logs
3. **ETA Detail View** - `onFavoriteClicked()` is a stub
4. **Add to Favorites** - No UI to save a stop after viewing ETAs
5. **Remove Favorite** - No swipe-to-delete or long-press menu
6. **Keypad Masking** - `updateKeypadState()` doesn't grey out invalid keys
7. **Error UI** - `showError()` only logs, no toast/modal
8. **Dynamic Card Update** - `updateFavoriteCard()` doesn't refresh UI

### Unused Variables
- `tab_view_` - Never assigned or used
- `current_request_id_` - Set but never read
- `current_route_` - Never assigned or used

---

## Workflow Gaps

The app currently supports:
- ✅ Entering a route number
- ✅ Submitting the search
- ✅ Loading/saving favorites from NVS
- ✅ Displaying favorite cards
- ✅ Auto-refreshing favorite ETAs

Missing workflow steps:
1. Route search → Direction chooser (O/I)
2. Direction → Stop list picker
3. Stop selection → ETA detail view
4. Detail view → "Add to favorites" button
5. Favorites list → Long-press to remove

**Recommended Next Step:** Implement `onBusEvent()` handlers for `BUS_EVT_ROUTE_VARIANTS` and `BUS_EVT_STOPS_LIST` to complete the search → favorite flow.

---

## Dependencies

**Internal:**
- `crystal_app.hpp` - Base app class
- `crystal_core.hpp` - NVS state wrapper
- `bus_service.h` - API client (request_eta, request_route, etc.)
- `bus_routes.h` - Route trie (is_complete, next_mask, charset)

**External:**
- `lvgl.h` - UI framework

**Icons:**
- `bus_icon` - 64×64 solid red square (defined in `bus_icon.c`)

---

## Usage Example

```cpp
// In main.cpp
static CrystalApp *make_bus_app() { return new BusApp(); }

static const CrystalAppEntry kApps[] = {
    {"bus", make_bus_app, true, 5},
};
```

User flow:
1. Open app → see "No saved stops" if first launch
2. Tap "⌨ Search" → enter route like "6X"
3. Tap ✓ → *[TODO: direction chooser]*
4. Select direction → *[TODO: stop picker]*
5. Tap stop → *[TODO: ETA detail + save button]*
6. Return to favorites → see saved stop with live ETAs
7. Tap favorite → *[TODO: full ETA board]*
