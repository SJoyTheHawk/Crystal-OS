# Phase 11 Home Pill Bug Fixes

## Date: 2026-09-09

## Overview

Fixed three bugs in the home pill implementation from Phase 11:

1. Dual pills showing on launcher
2. Touch-through to buttons underneath the pill
3. Home gesture incorrectly acting as "back" instead of "home"

## Bug 1: Dual Pills on Launcher

### Issue
Both Brookesia's pill and Crystal's pill were potentially showing on the launcher simultaneously.

### Root Cause
The visibility logic in `update_home_pill()` was correct, but it wasn't being called when switching to/from the launcher. The `on_app_event()` handler tracks app starts but never called `update_home_pill()`, so the pill state was stale after navigation.

### Fix
Added `update_home_pill()` calls to `on_app_event()`:

```cpp
void on_app_event(lv_event_t *event)
{
    // ... existing code to find matching app ...
    for (size_t i = 0; i < crystal_registry_installed_count(); ++i) {
        CrystalApp *app = crystal_registry_installed_app(i);
        if (app != nullptr && app->getId() == data->id) {
            s_current_index = i;
            update_page_dots();
            (void)persist_current_card();
            update_home_pill();  // Added: update pill when app starts
            return;
        }
    }
    // Added: update pill when switching to launcher (no matching app)
    update_home_pill();
}
```

This ensures the pill is hidden when navigating to the launcher and shown when entering an app.

Also added debug logging to `update_home_pill()` to track visibility changes.

### Code Locations
- `components/crystal_shell/src/crystal_shell.cpp:394-419` (update_home_pill with logging)
- `components/crystal_shell/src/crystal_shell.cpp:1270-1289` (on_app_event handler)

## Bug 2: Touch-Through in Settings

### Issue
Dragging from the pill activated buttons underneath (e.g., "System" in Settings), causing navigation deeper before the swipe committed, leaving the user still in Settings instead of closing it.

### Root Cause
Making the pill `LV_OBJ_FLAG_CLICKABLE` alone wasn't sufficient to block touch events. The gesture arbiter handles touches in the bottom band, but press events were still propagating to underlying objects before the gesture was claimed.

### Fix
Added event handlers to consume press events and prevent propagation:

```cpp
lv_obj_add_event_cb(s_home_pill, [](lv_event_t *e) {
    lv_event_stop_bubbling(e);
}, LV_EVENT_PRESSING, nullptr);

lv_obj_add_event_cb(s_home_pill, [](lv_event_t *e) {
    lv_event_stop_bubbling(e);
}, LV_EVENT_PRESSED, nullptr);
```

These handlers stop events from propagating to objects underneath the pill, preventing button activations during the initial touch phase before the gesture arbiter takes over.

### Code Location
`components/crystal_shell/src/crystal_shell.cpp:486-495`

## Bug 3: Home Gesture Acting as Back (MAJOR FIX)

### Issue - Corrected Understanding
The home pill swipe should act like a "home button" that returns to the previous context:
- **From Settings opened via launcher** → return to launcher
- **From Settings opened via app** → return to that app
- Currently it was treating the swipe as "back navigation", going up one level in Settings

### Root Cause
The gesture handler was calling `shell_consume_back()`, which implements back navigation (pop one Settings page). This is fundamentally wrong for a home gesture.

### Fix
Completely rewrote the Navigation gesture handler to implement true "home" behavior:

```cpp
if (owner == CrystalGestureOwner::Navigation) {
    if (info != nullptr && info->start_y - info->stop_y >= kHomeSwipeTravel) {
        // Close all Settings pages if open (not back navigation, but "go home")
        if (s_system_page_depth > 0) {
            while (s_system_page_depth > 0) system_page_pop();
        }
        // Close quick settings if open
        if (s_quick_settings_open) {
            close_quick_settings(nullptr);
        }
        // Send HOME event to return to launcher or previous app
        if (s_phone != nullptr) {
            (void)s_phone->sendNavigateEvent(ESP_BROOKESIA_CORE_NAVIGATE_TYPE_HOME);
        }
    }
    return;
}
```

**Key changes:**
1. **Unconditionally close ALL Settings pages** when home gesture fires (not just one page)
2. **Close quick settings** if open
3. **Send HOME navigation event** to Brookesia, which handles returning to the appropriate screen (launcher or previous app)
4. **No longer calls `shell_consume_back()`** - this was treating home as back navigation

### Behavior
- **Swipe up from pill in Settings** → closes all Settings pages, sends HOME event
- **Brookesia's HOME handler** decides where to go based on what was active before Settings opened
- **From launcher → Settings → home gesture** → returns to launcher
- **From app → Settings → home gesture** → returns to app

### Code Location
`components/crystal_shell/src/crystal_shell.cpp:1367-1396`

## Debug Logging Added

Added comprehensive logging to help diagnose issues:

1. **Pill visibility** (`update_home_pill`):
   - Logs when pill is hidden: `"Hiding pill: keyboard=%d, launcher=%d"`
   - Logs when pill is shown: `"Showing pill: depth=%zu"`

2. **Home gesture** (`on_gesture_release`):
   - Logs gesture travel: `"Home gesture: travel=%d, depth=%zu, quick=%d"`
   - Logs Settings closure: `"Closing all %zu Settings pages"`
   - Logs quick settings: `"Closing quick settings"`
   - Logs HOME event: `"Sending HOME navigation event"`
   - Logs insufficient travel: `"Home gesture insufficient: travel=%d < %d"`

## Testing Checklist

- [ ] Verify only one pill shows on launcher (not two)
- [ ] Verify pill is visible in apps and Settings
- [ ] Verify pill is hidden when keyboard is open
- [ ] Verify dragging from pill doesn't activate buttons underneath
- [ ] From launcher: open Settings → home swipe → returns to launcher
- [ ] From app: open Settings → home swipe → returns to app
- [ ] From app: Settings → WiFi → home swipe → returns to app (not WiFi page)
- [ ] Quick settings open → home swipe → closes quick settings and returns to previous screen
- [ ] Verify gesture travel threshold (80px) is comfortable

## Files Modified

- `components/crystal_shell/src/crystal_shell.cpp`
  - Added forward declaration for `system_page_pop()`
  - Modified `update_home_pill()` with debug logging
  - Modified `init_indicator_overlay()` to add event handlers for touch blocking
  - Completely rewrote Navigation gesture handler in `on_gesture_release()`

## Notes

- The fix maintains the existing `shell_consume_back()` function unchanged - it's still used for back button presses in Settings
- The home pill gesture now has fundamentally different behavior from the back button:
  - **Back button at depth 1**: closes Settings (via `shell_consume_back()`)
  - **Home gesture at any depth**: closes ALL Settings and returns to previous context
- Debug logging can be filtered with `TAG = "crystal_shell"` and log level INFO
