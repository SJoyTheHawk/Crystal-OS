# Code Implementation Guide: Fix Display "Shake" on Startup

## Problem Summary

After fixing the display corruption issue, a new cosmetic issue remains: the screen "shakes" or flickers once during startup, showing partial/intermediate UI states before stabilizing.

**Visual evidence**: The contact sheet from IMG_7651.MOV shows:
- Frames 1-4: Boot messages/early content displayed
- Frames 5-7: Visible "shake" - content becomes distorted/corrupted briefly
- Frames 8+: Final UI appears stable

## Root Cause

The display is configured with **RGB Direct Mode** (`CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE=y`), which means:
- LVGL draws directly to the framebuffer
- The RGB panel continuously scans this framebuffer to the display
- Every draw operation is immediately visible on screen

**Current problematic sequence**:
```
1. bsp_display_start()                    [line 146]
2. bsp_display_brightness_set(0)          [line 152] ← backlight OFF
3. phone->begin()                         [line 216] ← creates UI objects, triggers many redraws
4. crystal_core_init()                    [line 217-220]
5. crystal_registry_install()             [line 231-233]
6. crystal_shell_init()                   [line 235]
7. lv_refr_now(display)                   [line 243] ← explicit refresh
8. bsp_display_brightness_set(...)        [line 246] ← backlight ON **TOO EARLY**
9. bsp_display_unlock()                   [line 245] ← LVGL unlocked, more draws can happen
```

**The problem**: The backlight turns on at line 246, but `bsp_display_unlock()` happens at line 245 (this line number seems off in the trace, but the unlock is before the end). After unlock, LVGL can process any queued draw operations, and these partial frames are visible because the backlight is already on.

In RGB direct mode with the backlight on, you see every intermediate frame as Brookesia and Crystal Shell create and arrange UI objects.

## Solution

**Delay backlight turn-on until ALL initialization is complete and the display is in its final state.**

Move the `bsp_display_brightness_set()` call to the very end of `app_main()`, after `bsp_display_unlock()`.

---

## Code Changes Required

### File: `main/main.cpp`

**Step 1: Remove the premature backlight turn-on**

Find and **DELETE** these lines (currently around line 244-246):

```cpp
    lv_refr_now(display);
    // First frame is fully rendered in the framebuffer; wake the backlight at
    // the user's saved level so boot shows the final image with no flash.
    (void)bsp_display_brightness_set(hal().brightness->get());
    const int64_t elapsed_ms = (esp_timer_get_time() - s_boot_start_us) / 1000;
```

**Replace with** (keep the refresh, remove the brightness call):

```cpp
    lv_refr_now(display);
    const int64_t elapsed_ms = (esp_timer_get_time() - s_boot_start_us) / 1000;
```

---

**Step 2: Add backlight turn-on at the very end**

At the end of `app_main()`, after `bsp_display_unlock()` (currently line ~245), add:

```cpp
    ESP_LOGI(TAG, "main task stack headroom: %u bytes",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    bsp_display_unlock();
    
    // Turn backlight on after ALL UI initialization is complete and the display
    // is unlocked. In RGB direct mode, every LVGL draw is immediately visible,
    // so we keep the backlight OFF through the entire Brookesia/Crystal init
    // sequence to avoid showing partial UI states during object creation.
    (void)bsp_display_brightness_set(hal().brightness->get());
}
```

---

## Complete New Sequence

After the fix, the boot sequence will be:

```
1. bsp_display_start()
2. bsp_display_brightness_set(0)          ← backlight OFF
3. phone->begin()                         ← all UI creation happens in the dark
4. crystal_core_init()
5. crystal_registry_install()
6. crystal_shell_init()
7. lv_refr_now(display)                   ← final explicit refresh
8. bsp_display_unlock()                   ← release LVGL lock
9. bsp_display_brightness_set(...)        ← backlight ON, final frame instantly visible
```

**Result**: The user sees a black screen briefly, then the fully-formed launcher appears instantly with no shake or flicker.

---

## Why This Works

1. **RGB Direct Mode limitation**: In direct mode, the RGB panel continuously scans the framebuffer. Any partial draw is visible if the backlight is on.

2. **LVGL object creation triggers draws**: When Brookesia creates the launcher, status bar, and app cards, each object triggers redraws. These are visible as the "shake".

3. **Backlight masks the problem**: By keeping the backlight OFF until the very end, all intermediate frames are invisible. When the backlight turns on, only the final, stable UI is visible.

4. **No flash**: The first `lv_refr_now()` at line 243 ensures the framebuffer has valid content before unlock, so there's no flash of uninitialized data.

---

## Trade-offs

**Pros**:
- Eliminates the visible "shake" completely
- Professional boot experience - black screen → instant UI
- No risk of corruption or instability

**Cons**:
- Boot time **perception** may feel slightly longer (black screen persists ~100-200ms more)
- Actual boot time is unchanged - we're just hiding the intermediate states

---

## Verification Steps

1. Build and flash the firmware
2. Press the boot button to restart
3. **Verify**: Screen should go black, then the launcher should appear instantly with no shake/flicker
4. **Verify**: No visible partial UI states or distortion during boot
5. Repeat 5-10 times to ensure consistency

---

## Alternative: Add a Splash Screen (Future Enhancement)

If the black screen duration feels too long, consider adding a splash screen:
- Show a static Crystal OS logo immediately after `bsp_display_start()`
- Keep it visible through all init
- Fade to launcher when backlight turns on

This would require creating a simple LVGL screen with a centered logo before `phone->begin()`, but provides a more polished boot experience.

---

## Technical Notes

### Why not use full-refresh or avoid-tearing mode?

- **AVOID_TEAR is already enabled** (`CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR=y`), which minimizes tearing but doesn't eliminate intermediate state visibility
- **Direct mode** is chosen for performance - it avoids double-buffering overhead
- Switching modes would change memory layout and performance characteristics
- Controlling backlight timing is simpler and doesn't change the rendering architecture

### Why not wait for LVGL idle?

LVGL may have pending timer callbacks or animations that would delay boot indefinitely. The explicit `lv_refr_now()` + unlock + backlight-on sequence gives deterministic timing.
