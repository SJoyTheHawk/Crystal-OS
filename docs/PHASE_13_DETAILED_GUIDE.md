# Phase 13 Detailed Implementation Guide

**Purpose:** Implement Manage Apps UI for installing/uninstalling apps, reordering, and clearing app data.

**Safety principle:** Each sub-phase produces a visible, testable change. No large untested merges. Commit after each sub-phase passes its validation.

---

## Overview: Phase 13 Sub-Phase Structure

| Sub-Phase | What Changes | Visible Checkpoint | Est. Time |
|-----------|--------------|-------------------|-----------|
| 13.1 | Verify registry query APIs work correctly | Unit test or manual verification | 2h |
| 13.2 | Add CrystalState::clear() | Unit test confirms prefix erasure | 2h |
| 13.3 | Create Manage Apps page UI | Page shows all apps with state | 3h |
| 13.4 | Wire install/uninstall toggle | Toggle changes enabled flag in NVS | 3h |
| 13.5 | Add drag-to-reorder | Drag handle updates slot numbers | 3h |
| 13.6 | Add clear-data with confirmation | Dialog confirms, then erases | 2h |
| 13.7 | Apply changes live (no reboot) | Enable app appears in launcher immediately | 4h |
| 13.8 | Full validation suite | All test cases pass | 4h |

**Total estimated time:** 23 hours (~3 days)

---

## Sub-Phase 13.1: Verify Registry Query API

### Goal
Confirm that all registry query functions work correctly before building UI on top of them.

**Repository-specific findings from Phase 13.1 validation:**
- All six APIs already exist; this sub-phase validates them.
- Installed queries enumerate successfully installed, enabled apps in launcher
  order, not the complete firmware catalog. The later Manage Apps page must
  enumerate the catalog separately to display disabled apps.
- Read defaults are caller supplied: use each entry's `default_enabled` and
  `default_slot`. Fresh devices need not have registry keys in NVS.
- This branch defaults to `dev_tester` (0), `clock` (2), `weather` (3), and
  `calculator` (4). The example below is illustrative.
- Roundtrip tests must check setter results, use opposite read defaults, test
  slots as well as enabled flags, and restore original NVS bytes and absent
  keys. Do not force a previously disabled app on.
- Repeatable host coverage: `python3 tools/test_crystal_registry.py`. It compiles
  the production registry against test adapters; real NVS needs device checks.
- Results and device evidence: [Phase 13.1 validation](PHASE_13_1_VALIDATION.md).

### Changes

1. **Review existing API** (`components/crystal_registry/include/crystal_registry.hpp`):
```cpp
// These functions should already exist:
bool     crystal_registry_enabled(const char *id, bool default_value);
uint16_t crystal_registry_slot(const char *id, uint16_t default_value);
bool     crystal_registry_set_enabled(const char *id, bool enabled);
bool     crystal_registry_set_slot(const char *id, uint16_t slot);
size_t   crystal_registry_installed_count();
const char *crystal_registry_installed_id(size_t index);
```

2. **Add debug test function** (temporary, in `main/main.cpp`):
```cpp
// Add after crystal_registry_install(), before UI init:
static void test_registry_api(void)
{
    ESP_LOGI("test", "=== Registry API Test ===");
    
    // Test count
    size_t count = crystal_registry_installed_count();
    ESP_LOGI("test", "Installed apps: %zu", count);
    
    // Test iteration
    for (size_t i = 0; i < count; i++) {
        const char *id = crystal_registry_installed_id(i);
        bool enabled = crystal_registry_enabled(id, false);
        uint16_t slot = crystal_registry_slot(id, 999);
        ESP_LOGI("test", "  [%zu] %s: enabled=%d, slot=%u", i, id, enabled, slot);
    }
    
    // Test set/get
    ESP_LOGI("test", "Testing set_enabled...");
    crystal_registry_set_enabled("clock", false);
    bool clock_enabled = crystal_registry_enabled("clock", true);
    ESP_LOGI("test", "clock enabled after disable: %d (should be 0)", clock_enabled);
    
    crystal_registry_set_enabled("clock", true);
    clock_enabled = crystal_registry_enabled("clock", true);
    ESP_LOGI("test", "clock enabled after enable: %d (should be 1)", clock_enabled);
    
    ESP_LOGI("test", "=== Test Complete ===");
}

// In app_main, call it:
crystal_registry_install(phone, kApps, sizeof(kApps) / sizeof(kApps[0]));
test_registry_api();   // <-- add this line
crystal_shell_init(phone);
```

### Validation

1. **Build and flash**:
```bash
./scripts/build_dev.sh
idf.py flash monitor
```

2. **Check logs**:
```
I (1234) test: === Registry API Test ===
I (1235) test: Installed apps: 4
I (1236) test:   [0] clock: enabled=1, slot=0
I (1237) test:   [1] weather: enabled=1, slot=1
I (1238) test:   [2] calculator: enabled=1, slot=2
I (1239) test:   [3] dev_tester: enabled=1, slot=3
I (1240) test: Testing set_enabled...
I (1241) test: clock enabled after disable: 0 (should be 0)
I (1242) test: clock enabled after enable: 1 (should be 1)
I (1243) test: === Test Complete ===
```

3. **Checkpoint**: All functions return expected values

4. **Remove test function** before commit

5. **Commit**:
```bash
git add main/main.cpp
git commit -m "Phase 13.1: Verify registry query API

- Confirmed crystal_registry_installed_count() works
- Confirmed crystal_registry_installed_id() iteration works
- Confirmed crystal_registry_enabled() get/set works
- Confirmed crystal_registry_slot() get/set works

Checkpoint: All registry query functions return expected values

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Sub-Phase 13.2: Add CrystalState::clear()

### Goal
Implement `CrystalState::clear()` to erase all keys under an app's NVS prefix.

**Phase 13.2 integration notes:**
- Use ESP-IDF 6.1's error-returning iterator API, as implemented below. The
  earlier pointer-returning example and literal `NVS_DEFAULT` partition name
  were incompatible with this project.
- Prefixes derive from the app constructor name, including case and spaces
  (`Clock`, not registry ID `clock`). Prefer `app->state().clear()`.
- Complete enumeration before erasing; empty/no-match clears succeed. Storage
  errors return false, possibly after partial erasure. This is not atomic.
- Stop app writers before clearing. Live objects, shared services, and registry
  enable/slot flags are not reset. Existing 24-bit prefix hashing is unchanged.
- Use isolated test prefixes and verify existing NVS values and types survive.
  Remove temporary boot calls after validation and reflash the final image.
- Host regression test: `python3 tools/test_crystal_state.py`.

### Changes

1. **Add declaration** (`components/crystal_app/include/crystal_app.hpp`):
```cpp
class CrystalState final {
public:
    bool get(const char *key, void *value, size_t *length) const;
    bool set(const char *key, const void *value, size_t length);
    bool erase(const char *key);
    bool get_u32(const char *key, uint32_t *value) const;
    bool set_u32(const char *key, uint32_t value);
    
    /**
     * Clear all keys under this app's prefix.
     * Returns true if successful, false on error.
     */
    bool clear();   // NEW

private:
    std::string prefix_;
};
```

2. **Implement clear()** (`components/crystal_app/src/crystal_app.cpp` or new `crystal_state.cpp`):
```cpp
#include <vector>
#include "nvs.h"

bool CrystalState::clear()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("crystal", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "clear '%s': nvs_open failed: %s", prefix_.c_str(), esp_err_to_name(err));
        return false;
    }
    struct CloseHandle {
        nvs_handle_t handle;
        ~CloseHandle() { nvs_close(handle); }
    } close_handle{handle};
    struct ReleaseIterator {
        nvs_iterator_t value = nullptr;
        ~ReleaseIterator() { nvs_release_iterator(value); }
    } iterator;

    // Enumerate the opened namespace using the ESP-IDF 6.1 iterator API.
    // Finish the scan before mutating NVS; a failed scan must not erase a
    // partial list. Include the trailing dot when matching the app prefix.
    std::vector<std::string> keys;
    err = nvs_entry_find_in_handle(handle, NVS_TYPE_ANY, &iterator.value);
    while (err == ESP_OK) {
        nvs_entry_info_t info;
        err = nvs_entry_info(iterator.value, &info);
        if (err != ESP_OK) break;
        if (strncmp(info.key, prefix_.c_str(), prefix_.size()) == 0) {
            keys.emplace_back(info.key);
        }
        err = nvs_entry_next(&iterator.value);
    }
    // The iterator API may already have released it on reaching the end.
    nvs_release_iterator(iterator.value);
    iterator.value = nullptr;
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "clear '%s': NVS enumeration failed: %s", prefix_.c_str(), esp_err_to_name(err));
        return false;
    }

    bool ok = true;
    size_t erased = 0;
    for (const auto &key : keys) {
        err = nvs_erase_key(handle, key.c_str());
        if (err == ESP_OK) {
            ++erased;
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "clear '%s': erase %s failed: %s",
                     prefix_.c_str(), key.c_str(), esp_err_to_name(err));
            ok = false;
        }
    }
    if (!keys.empty()) {
        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "clear '%s': nvs_commit failed: %s", prefix_.c_str(), esp_err_to_name(err));
            ok = false;
        }
    }
    if (ok) {
        ESP_LOGI(TAG, "Cleared %zu keys under prefix '%s'", erased, prefix_.c_str());
    }
    return ok;
}

```

3. **Add test function** (temporary, in `main/main.cpp`):
```cpp
static void test_crystal_state_clear(void)
{
    ESP_LOGI("test", "=== CrystalState::clear() Test ===");
    
    // Create a test state
    CrystalState state("test_app");
    
    // Write some test data
    state.set_u32("counter", 42);
    state.set_u32("score", 100);
    const char *msg = "hello";
    state.set("message", msg, strlen(msg) + 1);
    
    // Verify writes
    uint32_t counter = 0, score = 0;
    state.get_u32("counter", &counter);
    state.get_u32("score", &score);
    ESP_LOGI("test", "Before clear: counter=%u, score=%u", counter, score);
    
    // Clear
    bool success = state.clear();
    ESP_LOGI("test", "clear() returned: %d", success);
    
    // Verify erasure
    counter = 999;
    score = 999;
    bool counter_exists = state.get_u32("counter", &counter);
    bool score_exists = state.get_u32("score", &score);
    ESP_LOGI("test", "After clear: counter exists=%d, score exists=%d", 
             counter_exists, score_exists);
    
    ESP_LOGI("test", "=== Test Complete ===");
}

// Call in app_main after crystal_core_init:
test_crystal_state_clear();
```

### Validation

1. **Build and flash**:
```bash
./scripts/build_dev.sh
idf.py flash monitor
```

2. **Check logs**:
```
I (1234) test: === CrystalState::clear() Test ===
I (1235) test: Before clear: counter=42, score=100
I (1236) CrystalState: Cleared 3 keys under prefix 'aXXXXXX.'
I (1237) test: clear() returned: 1
I (1238) test: After clear: counter exists=0, score exists=0
I (1239) test: === Test Complete ===
```

3. **Checkpoint**: Keys erased, get() returns false after clear

4. **Remove test function** before commit

5. **Commit**:
```bash
git add components/crystal_app/include/crystal_app.hpp components/crystal_app/src/crystal_app.cpp
git commit -m "Phase 13.2: Add CrystalState::clear()

- Implement clear() to erase all keys under app's prefix
- Iterate NVS namespace and filter by prefix
- Erase collected keys and commit

Checkpoint: Unit test confirms all keys under prefix are erased

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Sub-Phase 13.3: Create Manage Apps Page UI

### Goal
Create the Manage Apps page that displays all apps (enabled and disabled) with their current state.

### Implemented integration (2026-10-15)

Settings lives in components/crystal_shell/src/crystal_shell.cpp, not the
illustrative main/ui/settings path. Phase 13.3 reuses the existing page stack
for the header, Back button, viewport, Home teardown, and return to the app.

- components/crystal_shell/src/manage_apps_page.{h,cpp} builds the content.
- manage_apps_page_open() pushes the shell page and populates it.
- Settings has a top-level Manage Apps row immediately before System (user-requested placement).
- The shell component CMake file registers the new source.
- crystal_registry_catalog_count() / crystal_registry_catalog_entry() expose
  all validated firmware entries, including disabled ones. Installed queries
  cannot serve this page because they exclude disabled apps.
- Rows use each entry's actual enabled/slot defaults and persisted overrides,
  sorted by slot with table order breaking ties. No duplicate list of IDs or
  construction of disabled apps is needed.
- Each row shows a three-bar handle, registry ID, and Install/Installed control;
  enabled rows also show Clear. LV_SYMBOL_BARS renders in the configured font;
  the original U+2261 and Montserrat 18 example do not match this build.
- Controls are visibly disabled with a notice. Install, drag, and clear actions
  belong to Phases 13.4, 13.5, and 13.6 respectively.
- No widget pointers are cached by the builder. Popping deletes all children;
  reopening reads the current catalog and saved state.

### Validation

~~~sh
python3 tools/test_crystal_registry.py
python3 tools/test_crystal_state.py
python3 tools/test_manage_apps_page.py --output-dir /tmp/crystal-phase13-3-ui
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
./scripts/build_dev.sh
idf.py -p /dev/cu.usbmodem101 flash monitor
~~~

Navigate through the Quick Settings gear to Settings > Manage Apps.
Check all four entries, state labels, enabled-only Clear buttons, and Back to
Settings. The host test covers disabled/empty catalogs, defaults and overrides,
slot order/ties, layout bounds, scrolling, and teardown with real LVGL.
See [Phase 13.3 validation](PHASE_13_3_VALIDATION.md) for device results and
limits of automated testing.

---

## Sub-Phase 13.4: Wire Install/Uninstall Toggle

### Goal
Make the Install/Installed button functional — toggle the enabled flag in NVS and refresh the list.

### Implemented integration (2026-10-15)

Manage Apps remains directly under Settings, per the requested navigation.
The production implementation is in crystal_shell/src/manage_apps_page.cpp.

- Install/Installed is active. The callback reads the catalog entry's actual
  default, flips the saved flag via crystal_registry_set_enabled(), and updates
  the label and Clear visibility after a successful write.
- The row refreshes in place. Its button/event target and the list scroll offset
  survive; no global vector of widget pointers or delayed rebuild is needed.
- Failed writes show a visible error without optimistically changing the row.
- The page explicitly says a restart is required. Existing apps remain running
  until restart; no live app construction/removal is attempted in this phase.
- Clear stays disabled and drag handles stay decorative for 13.6 and 13.5.
- Removing the last enabled catalog entry is blocked with an explanation.
  The current boot shell requires at least one installed app; this prevents
  the normal UI from saving an all-disabled configuration that cannot start.
- Uninstall only changes the registry flag. It does not erase app state.

### Validation

~~~sh
python3 tools/test_manage_apps_page.py --output-dir /tmp/crystal-phase13-4-ui
python3 tools/test_crystal_registry.py
python3 tools/test_crystal_state.py
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
./scripts/build_dev.sh
idf.py -p /dev/cu.usbmodem101 flash monitor
~~~

Check Settings > Manage Apps: Installed changes to Install and Clear disappears;
Install changes back and Clear reappears. Restart via Settings > System > Restart
to apply the selection. Read the visible save status before restarting.

See [Phase 13.4 validation](PHASE_13_4_VALIDATION.md) for test evidence, error
handling, reboot behavior, and the distinction between saving and live apply.

---

## Sub-Phase 13.5: Add Drag-to-Reorder

### Goal
Make the drag handle (≡) functional for reordering apps.

### Changes

1. **Update manage_apps_page.cpp**:
```cpp
// Add at file scope:
static int s_drag_from_idx = -1;
static lv_obj_t *s_drag_overlay = nullptr;

static void handle_drag_start(lv_event_t *e)
{
    lv_obj_t *handle = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    
    s_drag_from_idx = idx;
    ESP_LOGI(TAG, "Drag started from index %d", idx);
    
    // Create drag overlay (visual feedback)
    s_drag_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_drag_overlay, 100, 50);
    lv_obj_set_style_bg_color(s_drag_overlay, lv_color_hex(0x3388FF), 0);
    lv_obj_set_style_bg_opa(s_drag_overlay, LV_OPA_70, 0);
    
    const char *app_id = crystal_registry_installed_id(idx);
    lv_obj_t *overlay_label = lv_label_create(s_drag_overlay);
    lv_label_set_text(overlay_label, app_id);
    lv_obj_center(overlay_label);
    
    // Position follows finger
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    lv_obj_set_pos(s_drag_overlay, p.x - 50, p.y - 25);
}

static void handle_drag_move(lv_event_t *e)
{
    if (s_drag_overlay == nullptr) return;
    
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    lv_obj_set_pos(s_drag_overlay, p.x - 50, p.y - 25);
}

static void handle_drag_end(lv_event_t *e)
{
    if (s_drag_from_idx < 0) return;
    
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    
    ESP_LOGI(TAG, "Drag ended: %d -> %d", s_drag_from_idx, idx);
    
    if (s_drag_from_idx != idx) {
        // Reorder: swap slot numbers
        const char *from_id = crystal_registry_installed_id(s_drag_from_idx);
        const char *to_id = crystal_registry_installed_id(idx);
        
        uint16_t from_slot = crystal_registry_slot(from_id, 999);
        uint16_t to_slot = crystal_registry_slot(to_id, 999);
        
        ESP_LOGI(TAG, "Swapping slots: %s[%u] <-> %s[%u]", 
                 from_id, from_slot, to_id, to_slot);
        
        crystal_registry_set_slot(from_id, to_slot);
        crystal_registry_set_slot(to_id, from_slot);
        
        // Rebuild to show new order
        build_app_list();
    }
    
    // Clean up overlay
    if (s_drag_overlay != nullptr) {
        lv_obj_del(s_drag_overlay);
        s_drag_overlay = nullptr;
    }
    s_drag_from_idx = -1;
}

// Update build_app_list() to add drag handlers:
static void build_app_list(void)
{
    if (s_list == nullptr) return;
    
    lv_obj_clean(s_list);
    s_app_rows.clear();
    
    size_t count = crystal_registry_installed_count();
    ESP_LOGI(TAG, "Building list for %zu apps", count);
    
    for (size_t i = 0; i < count; i++) {
        const char *id = crystal_registry_installed_id(i);
        bool enabled = crystal_registry_enabled(id, false);
        
        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_set_size(row, LV_PCT(100), 60);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        
        // Drag handle (≡) — now interactive
        lv_obj_t *handle = lv_label_create(row);
        lv_label_set_text(handle, "≡");
        lv_obj_set_style_text_font(handle, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(handle, lv_color_hex(0x888888), 0);
        lv_obj_set_width(handle, 30);
        lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE);  // Make it interactive
        
        // Wire drag events
        lv_obj_add_event_cb(handle, handle_drag_start, LV_EVENT_PRESSED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(handle, handle_drag_move, LV_EVENT_PRESSING, (void *)(intptr_t)i);
        lv_obj_add_event_cb(handle, handle_drag_end, LV_EVENT_RELEASED, (void *)(intptr_t)i);
        
        // ... rest of row creation unchanged ...
    }
}
```

### Validation

1. **Build and flash**:
```bash
./scripts/build_dev.sh
idf.py flash monitor
```

2. **Navigate to Manage Apps**

3. **Press and drag the ≡ handle on clock app**:
   - **Checkpoint**: Overlay appears showing app name
   - **Checkpoint**: Overlay follows finger

4. **Drop on another app**:
   - **Checkpoint**: Logs show "Swapping slots: clock[0] <-> weather[1]"
   - **Checkpoint**: List reorders immediately

5. **Reboot and check**:
   - **Checkpoint**: New order persists

6. **Commit**:
```bash
git add main/ui/settings/manage_apps_page.cpp
git commit -m "Phase 13.5: Add drag-to-reorder

- Make ≡ handle interactive (LV_OBJ_FLAG_CLICKABLE)
- Add drag_start/move/end handlers
- Swap slot numbers on drop
- Rebuild list to show new order immediately

Checkpoint: Drag handle moves app position, persists to NVS

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

---

## Sub-Phase 13.6: Add Clear Data Confirmation

**Status: complete, device accepted 2026-10-17.** The user confirmed all five
device test groups, including clearing a newly enabled app before restart.
See [Phase 13.6 validation and sign-off](PHASE_13_6_VALIDATION.md).

### Repository implementation notes

The illustrative code below is historical and is superseded by the production
implementation in `components/crystal_shell/src/manage_apps_page.cpp` and the
shell's `manage_apps_clear_data()` callback:

- Manage Apps remains directly under Settings. Clear opens a modal with Cancel
  and Clear Data, reports success/failure, and resets saved data immediately.
- Resolve the installed app and stop it before clearing. Use
  `CrystalApp::clearData()` and its `state().clear()`; constructor names define
  namespaces (Calculator), not registry IDs (calculator). The snippet's
  `CrystalState state(s_clear_target_id)` must not be copied into this system.
- Newly enabled apps use a temporary factory instance for the correct namespace.
- Reset hooks discard Clock's retained timer/stopwatch/lap state and suppress
  late Weather cache writes until reopening. Storage failures can be partial
  and are shown explicitly with a retry option.
- Back, cancellation, page deletion, and rebuilding release the modal safely.
  The production card uses configured fonts and keeps both buttons in bounds.
- Clear preserves install/order settings and creates no new restart requirement.
  Prior install/reorder changes retain the Restart now / Later behavior.

### Goal
Wire the Clear button to show a confirmation dialog, then erase the app's data on confirm.

### Changes

1. **Update manage_apps_page.cpp**:
```cpp
// Add confirmation dialog state:
static lv_obj_t *s_confirm_dialog = nullptr;
static const char *s_clear_target_id = nullptr;

static void clear_data_confirmed(lv_event_t *e)
{
    if (s_clear_target_id == nullptr) return;
    
    ESP_LOGI(TAG, "Clearing data for %s", s_clear_target_id);
    
    // Create CrystalState for this app and clear it
    CrystalState state(s_clear_target_id);
    bool success = state.clear();
    
    if (success) {
        ESP_LOGI(TAG, "Successfully cleared data for %s", s_clear_target_id);
    } else {
        ESP_LOGE(TAG, "Failed to clear data for %s", s_clear_target_id);
    }
    
    // Close dialog
    if (s_confirm_dialog != nullptr) {
        lv_obj_del(s_confirm_dialog);
        s_confirm_dialog = nullptr;
    }
    s_clear_target_id = nullptr;
}

static void clear_data_cancelled(lv_event_t *e)
{
    ESP_LOGI(TAG, "Clear data cancelled");
    
    if (s_confirm_dialog != nullptr) {
        lv_obj_del(s_confirm_dialog);
        s_confirm_dialog = nullptr;
    }
    s_clear_target_id = nullptr;
}

static void show_clear_confirmation(const char *app_id)
{
    if (s_confirm_dialog != nullptr) {
        ESP_LOGW(TAG, "Dialog already open");
        return;
    }
    
    s_clear_target_id = app_id;
    
    // Create modal background
    s_confirm_dialog = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_confirm_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_confirm_dialog, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_confirm_dialog, LV_OPA_60, 0);
    lv_obj_center(s_confirm_dialog);
    
    // Create dialog card
    lv_obj_t *card = lv_obj_create(s_confirm_dialog);
    lv_obj_set_size(card, 360, 200);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_center(card);
    
    // Title
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "Clear App Data");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(title, 0, 0);
    
    // Message
    lv_obj_t *message = lv_label_create(card);
    char buf[128];
    snprintf(buf, sizeof(buf), "Clear all data for %s?\n\nThis cannot be undone.", app_id);
    lv_label_set_text(message, buf);
    lv_obj_set_style_text_font(message, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(message, 0, 40);
    lv_obj_set_width(message, 320);
    
    // Buttons
    lv_obj_t *button_row = lv_obj_create(card);
    lv_obj_set_size(button_row, 320, 50);
    lv_obj_set_pos(button_row, 0, 140);
    lv_obj_set_flex_flow(button_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(button_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(button_row, 0, 0);
    
    // Cancel button
    lv_obj_t *cancel_btn = lv_btn_create(button_row);
    lv_obj_set_size(cancel_btn, 140, 40);
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel_btn, clear_data_cancelled, LV_EVENT_CLICKED, nullptr);
    
    // Confirm button
    lv_obj_t *confirm_btn = lv_btn_create(button_row);
    lv_obj_set_size(confirm_btn, 140, 40);
    lv_obj_set_style_bg_color(confirm_btn, lv_color_hex(0xFF3333), 0);
    lv_obj_t *confirm_label = lv_label_create(confirm_btn);
    lv_label_set_text(confirm_label, "Clear Data");
    lv_obj_center(confirm_label);
    lv_obj_add_event_cb(confirm_btn, clear_data_confirmed, LV_EVENT_CLICKED, nullptr);
}

static void clear_btn_cb(lv_event_t *e)
{
    const char *app_id = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Clear requested for %s", app_id);
    show_clear_confirmation(app_id);
}

// Update build_app_list() to wire Clear button:
// ... in the "Clear button" section:
if (enabled) {
    lv_obj_t *clear_btn = lv_btn_create(row);
    lv_obj_set_size(clear_btn, 80, 40);
    lv_obj_t *clear_label = lv_label_create(clear_btn);
    lv_label_set_text(clear_label, "Clear");
    lv_obj_center(clear_label);
    lv_obj_add_event_cb(clear_btn, clear_btn_cb, LV_EVENT_CLICKED, (void *)id);
}
```

### Validation

1. **Build and flash**:
```bash
./scripts/build_dev.sh
idf.py flash monitor
```

2. **Navigate to Manage Apps**

3. **Tap "Clear" on an enabled app** (e.g., calculator):
   - **Checkpoint**: Dialog appears with title "Clear App Data"
   - **Checkpoint**: Message shows "Clear all data for calculator?"

4. **Tap "Cancel"**:
   - **Checkpoint**: Dialog closes, no data cleared

5. **Tap "Clear" again, then "Clear Data"**:
   - **Checkpoint**: Logs show "Clearing data for calculator"
   - **Checkpoint**: Logs show "Successfully cleared data for calculator"
   - **Checkpoint**: Dialog closes

6. **Open Calculator app**:
   - **Checkpoint**: In-progress formula is gone (reset state)

7. **Commit**:
```bash
git add main/ui/settings/manage_apps_page.cpp
git commit -m "Phase 13.6: Add clear data confirmation

- Create confirmation dialog with Cancel and Clear Data buttons
- Call CrystalState::clear() on confirmation
- Show warning message about irreversibility
- Wire Clear button to show_clear_confirmation()

Checkpoint: Clear-data shows dialog and erases on confirm

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Sub-Phase 13.7: Apply Changes Live (No Reboot)

### Goal
When an app is enabled, construct and install it immediately so it appears in the launcher without rebooting.

### Changes

1. **Add live enable/disable to crystal_registry** (`components/crystal_registry/src/crystal_registry.cpp`):
```cpp
// Add at file scope:
#include <vector>
#include <string>
#include <algorithm>

// Note: AppEntry is defined in main/main.cpp and passed to crystal_registry_install()
static ESP_Brookesia_Phone *s_phone = nullptr;
static const AppEntry *kApps = nullptr;
static size_t kAppsCount = 0;

// Add new function:
bool crystal_registry_apply_changes(void)
{
    if (s_phone == nullptr) {
        ESP_LOGE(TAG, "Phone not set, cannot apply changes");
        return false;
    }
    
    ESP_LOGI(TAG, "Applying registry changes live");
    
    // Get current installed apps
    std::vector<std::string> installed_ids;
    for (size_t i = 0; i < crystal_registry_installed_count(); i++) {
        installed_ids.push_back(crystal_registry_installed_id(i));
    }
    
    // Rebuild installed apps from kApps
    struct Row { const AppEntry *e; int slot; };
    std::vector<Row> rows;
    
    for (size_t i = 0; i < kAppsCount; i++) {
        const AppEntry *e = &kApps[i];
        if (!crystal_registry_enabled(e->id, e->default_enabled)) continue;
        rows.push_back({ e, crystal_registry_slot(e->id, 999) });
    }
    std::sort(rows.begin(), rows.end(),
              [](const Row &a, const Row &b) { return a.slot < b.slot; });
    
    // Uninstall apps that are no longer enabled
    for (const auto &id : installed_ids) {
        bool still_enabled = crystal_registry_enabled(id.c_str(), false);
        if (!still_enabled) {
            ESP_LOGI(TAG, "Uninstalling %s", id.c_str());
            // TODO: s_phone->uninstallApp() by ID — Brookesia doesn't expose this yet
            // For now, requires reboot
        }
    }
    
    // Install newly enabled apps
    for (auto &r : rows) {
        // Check if already installed
        bool already_installed = false;
        for (const auto &id : installed_ids) {
            if (strcmp(id.c_str(), r.e->id) == 0) {
                already_installed = true;
                break;
            }
        }
        
        if (!already_installed) {
            ESP_LOGI(TAG, "Installing %s", r.e->id);
            CrystalApp *app = r.e->factory();
            if (s_phone->installApp(app) < 0) {
                ESP_LOGE(TAG, "install failed: %s", r.e->id);
                delete app;
            }
        }
    }
    
    return true;
}

// Update crystal_registry_install to save phone pointer:
void crystal_registry_install(ESP_Brookesia_Phone *phone, const AppEntry *apps, size_t count)
{
    s_phone = phone;   // Save for later
    kApps = apps;
    kAppsCount = count;
    
    // ... existing implementation ...
}
```

2. **Add to header** (`components/crystal_registry/include/crystal_registry.hpp`):
```cpp
/**
 * Apply registry changes immediately without rebooting.
 * Installs newly enabled apps, uninstalls disabled ones.
 * Returns true if successful.
 * 
 * Note: Uninstall requires Brookesia support; may require reboot in v1.
 */
bool crystal_registry_apply_changes(void);
```

3. **Call from Manage Apps** (`main/ui/settings/manage_apps_page.cpp`):
```cpp
// Update toggle_btn_cb():
static void toggle_btn_cb(lv_event_t *e)
{
    const char *app_id = (const char *)lv_event_get_user_data(e);
    bool currently_enabled = crystal_registry_enabled(app_id, false);
    bool new_state = !currently_enabled;
    
    ESP_LOGI(TAG, "Toggling %s: %d -> %d", app_id, currently_enabled, new_state);
    
    if (crystal_registry_set_enabled(app_id, new_state)) {
        ESP_LOGI(TAG, "Successfully toggled %s to %d", app_id, new_state);
        
        // Apply changes live
        if (new_state) {
            // Enabling — try to install immediately
            if (crystal_registry_apply_changes()) {
                ESP_LOGI(TAG, "Applied changes live, %s should appear in launcher", app_id);
            } else {
                ESP_LOGW(TAG, "Could not apply live, changes take effect on next boot");
            }
        } else {
            // Disabling — Brookesia doesn't support uninstall yet
            ESP_LOGI(TAG, "Disable will take effect on next boot");
        }
        
        // Rebuild list to reflect change
        build_app_list();
    } else {
        ESP_LOGE(TAG, "Failed to toggle %s", app_id);
    }
}
```

4. **Add note to UI** (optional but recommended):
```cpp
// In manage_apps_page_open(), after title:
lv_obj_t *note = lv_label_create(header);
lv_label_set_text(note, "Enable takes effect immediately. Disable requires reboot.");
lv_obj_set_style_text_font(note, &lv_font_montserrat_12, 0);
lv_obj_set_style_text_color(note, lv_color_hex(0x888888), 0);
lv_obj_align(note, LV_ALIGN_BOTTOM_MID, 0, -5);
```

### Validation

1. **Build and flash**:
```bash
./scripts/build_dev.sh
idf.py flash monitor
```

2. **Navigate to Manage Apps**

3. **Disable calculator** (if enabled):
   - **Checkpoint**: Button changes to "Install"
   - **Checkpoint**: Note about reboot appears

4. **Exit to launcher**:
   - **Checkpoint**: Calculator still visible (requires reboot to remove)

5. **Enable calculator**:
   - **Checkpoint**: Button changes to "Installed"
   - **Checkpoint**: Logs show "Applied changes live"

6. **Exit to launcher**:
   - **Checkpoint**: Calculator appears immediately (no reboot needed)

7. **Open calculator**:
   - **Checkpoint**: Works correctly

8. **Commit**:
```bash
git add components/crystal_registry/include/crystal_registry.hpp components/crystal_registry/src/crystal_registry.cpp main/ui/settings/manage_apps_page.cpp
git commit -m "Phase 13.7: Apply enable changes live (disable requires reboot)

- Add crystal_registry_apply_changes() to install newly enabled apps
- Call from toggle when enabling an app
- Note in UI that disable requires reboot (Brookesia limitation)
- Newly enabled apps appear in launcher immediately

Checkpoint: Enable app appears in launcher without reboot

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Sub-Phase 13.8: Full Validation Suite

### Goal
Run comprehensive test matrix, verify all scenarios work correctly.

### Test Matrix

| Test Case | Expected Result | Pass/Fail |
|-----------|----------------|-----------|
| **UI Navigation** | | |
| Open Manage Apps from Settings | Page opens with app list | |
| Back button returns to Settings | Returns correctly | |
| **App List Display** | | |
| All apps from kApps shown | Clock, Weather, Calculator, DevTester visible | |
| Enabled apps show "Installed" | Correct button text | |
| Disabled apps show "Install" | Correct button text | |
| Enabled apps show "Clear" button | Clear button present | |
| Disabled apps hide "Clear" button | Clear button absent | |
| **Install/Uninstall** | | |
| Tap "Install" on disabled app | Button changes to "Installed", Clear appears | |
| Newly enabled app appears in launcher | App visible without reboot | |
| Open newly enabled app | App launches correctly | |
| Tap "Installed" on enabled app | Button changes to "Install", Clear disappears | |
| Disabled app removed after reboot | App not in launcher after restart | |
| NVS persists enabled state | State survives reboot | |
| **Reorder** | | |
| Drag ≡ handle | Overlay appears, follows finger | |
| Drop on different position | Apps swap positions | |
| New order persists | Order survives reboot | |
| Launcher reflects new order | Cards match Manage Apps order | |
| **Clear Data** | | |
| Tap "Clear" | Confirmation dialog appears | |
| Dialog shows app name | Correct app identified | |
| Tap "Cancel" | Dialog closes, no data erased | |
| Tap "Clear Data" | Dialog closes, data erased | |
| App state reset | In-progress data gone | |
| Re-open app after clear | Starts fresh | |
| **Edge Cases** | | |
| Enable all apps | All show "Installed" | |
| Disable all apps | All show "Install" | |
| Reorder with only 2 apps | Works correctly | |
| Clear data on app with no data | No error | |
| Rapid toggle | No race condition | |
| **Regressions** | | |
| Phase 12 OTA still works | Update succeeds | |
| Settings navigation intact | All pages load | |
| Launcher app switching works | Swipe between cards | |
| WiFi scan and connect work | Networks listed, connection succeeds | |

### Validation Steps

1. **Functional tests**:
   - Open Manage Apps
   - Test Install/Uninstall on each app
   - Test reorder with different apps
   - Test Clear Data with confirmation
   - Exit and re-enter to verify persistence

2. **Live enable test**:
   - Disable calculator (will need reboot)
   - Reboot device
   - Verify calculator not in launcher
   - Enable calculator in Manage Apps
   - Exit to launcher
   - **Checkpoint**: Calculator appears immediately
   - Open calculator, verify it works

3. **Reorder persistence test**:
   - Reorder apps (e.g., move weather to first position)
   - Reboot device
   - **Checkpoint**: New order persists in launcher

4. **Clear data test**:
   - Open calculator, enter formula "2+3="
   - Exit to Manage Apps
   - Clear calculator data
   - Open calculator
   - **Checkpoint**: Formula is reset (shows "0")

5. **Phase 12 regression test**:
   - Check for updates (should work)
   - Device Status shows version (should work)
   - Last Crash row present (should work)

6. **Completion criteria**:
   - [ ] All functional tests pass
   - [ ] Live enable works without reboot
   - [ ] Reorder persists correctly
   - [ ] Clear data erases app state
   - [ ] No Phase 12 regressions
   - [ ] Documentation updated

### Final Commit

```bash
git add -A
git commit -m "Phase 13.8: Complete validation suite

Tested:
- All UI navigation and display
- Install/uninstall with live enable
- Drag-to-reorder with persistence
- Clear data with confirmation
- Edge cases (all enabled, all disabled, etc.)
- Phase 12 regressions (OTA, Device Status, crash tracking)

All test cases passed. Phase 13 complete.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Summary: Phase 13 Complete

### What Was Built

1. **Registry query API** — Verified existing API works correctly
2. **CrystalState::clear()** — Erase all keys under app's prefix
3. **Manage Apps page** — Full UI showing all apps with state
4. **Install/Uninstall** — Toggle enabled flag, persist to NVS
5. **Drag-to-reorder** — Swap slot numbers, persist to NVS
6. **Clear data** — Confirmation dialog, erase app state
7. **Live enable** — Newly enabled apps appear without reboot
8. **Full validation** — All test cases pass, no regressions

### Key Files Added/Modified

**New Files:**
- `main/ui/settings/manage_apps_page.{h,cpp}` — Manage Apps UI

**Modified Files:**
- `components/crystal_app/include/crystal_state.hpp` — Add clear()
- `components/crystal_app/src/crystal_state.cpp` — Implement clear()
- `components/crystal_registry/include/crystal_registry.hpp` — Add apply_changes()
- `components/crystal_registry/src/crystal_registry.cpp` — Implement live enable
- `main/ui/settings/settings_main_screen.cpp` — Add "Manage Apps" button
- `main/CMakeLists.txt` — Include new files

### Architecture Decisions Implemented

- Install/uninstall is an NVS flag flip
- Reorder is slot number assignment
- Clear-data iterates NVS by prefix
- Enable takes effect live (install immediately)
- Disable requires reboot (Brookesia limitation)
- Confirmation required for destructive actions

### Known Limitations

- **Uninstall requires reboot** — Brookesia doesn't expose `uninstallApp()` by ID
- **Apps must be in kApps** — Can only install what shipped in firmware
- **No app download** — v2 feature, not v1

### Next Phase

Phase 13 is complete. Ready for production deployment or Phase 14 (if defined).

---

**Document Version:** 1.0  
**Last Updated:** 2026-10-13  
**Phase Status:** Implementation-ready
