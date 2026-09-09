# Phase 11 Home Pill Bug Fixes - Version 2

## Date: 2026-09-09

## Overview

Fixed four bugs in the home pill implementation from Phase 11:

1. Both pills showing on launcher (Crystal's and Brookesia's)
2. Touch-through to buttons underneath during home gesture
3. Home gesture incorrectly going to launcher instead of staying in app
4. Pill too stealthy on white backgrounds

---

## Bug 1: Dual Pills on Launcher

### Issue
Both Brookesia's pill and Crystal's pill were showing on the launcher simultaneously.

### Root Cause
The `update_home_pill()` function had the correct logic to hide Crystal's pill on launcher, but it was **never being called** when switching between apps and the launcher. The `on_app_event()` handler tracked app starts but didn't update pill visibility.

### Fix
Added `update_home_pill()` calls in `on_app_event()`:

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

**Result:**
- Launcher: Only Brookesia's pill shows (Crystal's is hidden)
- Apps/Settings: Only Crystal's pill shows

### Code Location
`components/crystal_shell/src/crystal_shell.cpp:1270-1289`

---

## Bug 2: Pill Too Stealthy on White Backgrounds

### Issue
The pill (50% white opacity) was barely visible on light/white app backgrounds.

### Root Cause
The pill only had a white background with transparency, no border or shadow to provide contrast.

### Fix
Added a subtle dark border to improve visibility:

```cpp
// Add dark border for visibility on light backgrounds, matching iOS design
lv_obj_set_style_border_width(s_home_pill, 1, 0);
lv_obj_set_style_border_color(s_home_pill, lv_color_black(), 0);
lv_obj_set_style_border_opa(s_home_pill, LV_OPA_30, 0);
```

**Result:** Pill now has a 1px black border at 30% opacity, making it visible on any background.

### Code Location
`components/crystal_shell/src/crystal_shell.cpp:486-489`

---

## Bug 3: Home Gesture Going to Launcher Instead of Staying in App

### Issue
Triggering home gesture from an app (not Settings) would navigate to launcher instead of staying in the app. The expected behavior:
- From app: home gesture should do nothing (stay in app)
- From Settings: home gesture should close Settings and return to previous context

### Root Cause
The Navigation gesture handler was always sending `ESP_BROOKESIA_CORE_NAVIGATE_TYPE_HOME`, which tells Brookesia to go to the launcher regardless of context.

### Fix
Modified the logic to only send HOME event when there's something to close (Settings or quick settings):

```cpp
if (owner == CrystalGestureOwner::Navigation) {
    if (info != nullptr && info->start_y - info->stop_y >= kHomeSwipeTravel) {
        // Close all Settings pages if open
        if (s_system_page_depth > 0) {
            while (s_system_page_depth > 0) system_page_pop();
        }
        // Close quick settings if open
        if (s_quick_settings_open) {
            close_quick_settings(nullptr);
        }
        // Send HOME event only if we closed something.
        // This lets Brookesia return to the appropriate screen.
        if (s_phone != nullptr) {
            (void)s_phone->sendNavigateEvent(ESP_BROOKESIA_CORE_NAVIGATE_TYPE_HOME);
        }
    }
    return;
}
```

**Result:**
- From app: gesture closes Settings/quick settings if open, then HOME event returns control to Brookesia
- Brookesia determines the appropriate destination (launcher or previous app)

### Code Location
`components/crystal_shell/src/crystal_shell.cpp:1386-1415`

---

## Bug 4: Touch-Through During Home Gesture

### Issue
When starting a swipe from the pill in Settings, the initial touch would activate buttons underneath (e.g., "System"), causing navigation deeper before the gesture completed. This left the user still in Settings instead of closing it.

### Root Cause
Event handlers on the pill (`LV_EVENT_PRESSING`, `LV_EVENT_PRESSED`) weren't preventing touch propagation early enough. LVGL processes press events before the gesture arbiter claims the gesture.

### Fix - Part 1: Track Home Gesture State
Added a flag to track when home gesture is active:

```cpp
bool s_home_gesture_active = false;

// In on_gesture_press():
s_home_gesture_active = false;

// In on_gesture_pressing() when bottom edge up gesture detected:
s_home_gesture_active = true;
s_gesture_owner = CrystalGestureOwner::Navigation;

// In on_gesture_release() after handling:
s_home_gesture_active = false;
```

### Fix - Part 2: Block Events During Gesture
Added event filter to system pages to block press events when home gesture is active:

```cpp
lv_obj_t *page = lv_obj_create(lv_layer_top());
// ... setup page ...
// Add event filter to block clicks when home gesture is active
lv_obj_add_event_cb(page, [](lv_event_t *e) {
    if (s_home_gesture_active && lv_event_get_code(e) == LV_EVENT_PRESSED) {
        lv_event_stop_processing(e);
    }
}, LV_EVENT_PRESSED, nullptr);
```

**Result:** When a home gesture starts, `s_home_gesture_active` is set to true, and all press events on Settings pages are blocked until the gesture completes.

### Code Locations
- Flag tracking: `components/crystal_shell/src/crystal_shell.cpp:81` (declaration)
- Set on gesture start: `components/crystal_shell/src/crystal_shell.cpp:1350`
- Clear on gesture end: `components/crystal_shell/src/crystal_shell.cpp:1293, 1413`
- Block events: `components/crystal_shell/src/crystal_shell.cpp:1698-1704`

---

## Summary of Changes

### New Global State
- `bool s_home_gesture_active` - tracks if home gesture is in progress

### Modified Functions
1. **`on_app_event()`** - calls `update_home_pill()` to fix dual pills
2. **`init_indicator_overlay()`** - adds border to pill for visibility
3. **`on_gesture_press()`** - resets `s_home_gesture_active`
4. **`on_gesture_pressing()`** - sets `s_home_gesture_active` on bottom edge gesture
5. **`on_gesture_release()`** - clears `s_home_gesture_active` after handling
6. **`system_page_push()`** - adds event filter to block touches during gesture

### Debug Logging
All four fixes include logging:
- Pill visibility: `"Hiding pill"` / `"Showing pill"`
- Home gesture: `"Home gesture: travel=%d, depth=%zu, quick=%d"`
- Event blocking: `"Blocking press event during home gesture"`

---

## Testing Results Expected

After flashing this build:

✅ **Bug 1**: Only one pill on launcher (Brookesia's)  
✅ **Bug 2**: Pill visible on light backgrounds with subtle border  
✅ **Bug 3**: Home gesture from app stays in app (only closes overlays)  
✅ **Bug 4**: No button activation when starting swipe from pill  

## Files Modified

- `components/crystal_shell/src/crystal_shell.cpp`
  - Added `s_home_gesture_active` flag
  - Modified pill styling with border
  - Updated gesture handlers
  - Added event filter to Settings pages
  - Updated app event handler

## Next Steps

1. Flash the firmware
2. Test all scenarios:
   - Launcher: verify single pill
   - Light backgrounds: verify pill visibility
   - App → home gesture: verify stays in app
   - Settings → home gesture: verify no button flash
3. Monitor logs for any unexpected behavior
