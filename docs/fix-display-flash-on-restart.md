# Code Implementation Guide: Fix Display Flash and Corruption on Restart

## Problem Summary

The device exhibits two related issues on restart:
1. **Flash on startup**: The screen flashes once during boot
2. **Screen corruption**: By chance, the screen becomes corrupted/broken after restart

## Root Cause

**Uninitialized backlight state during LCD panel initialization.**

The BSP initializes the LEDC PWM channel with `duty = 0` (line 338 in `esp32_s3_touch_lcd_4b.c`), but Crystal OS never explicitly sets the backlight brightness before the LCD receives initialization commands. This causes:
- Undefined backlight state during LCD init sequence
- Flash when first frame renders and brightness suddenly changes
- Potential LCD corruption if init commands execute with wrong backlight state

All esp-brookesia examples call `bsp_display_backlight_on()` immediately after `bsp_display_start()` - Crystal OS is missing this pattern.

---

## Code Changes Required

### File: `main/main.cpp`

**Location 1: After `bsp_display_start()` (line 146)**

Add immediately after the `require_boot_step(display != nullptr, ...)` check:

```cpp
lv_display_t *display = bsp_display_start();
require_boot_step(display != nullptr, "Failed to start display");

// Turn backlight OFF before any LVGL rendering or HAL init to prevent flash
// and ensure LCD init sequence ran with a known backlight state.
(void)bsp_display_brightness_set(0);
```

**Why**: Ensures the LCD initialization sequence completes with backlight definitely OFF, preventing corruption and establishing a known state.

---

**Location 2: After `lv_refr_now(display)` (line 238)**

Add immediately after the first frame refresh and before the timing log:

```cpp
lv_refr_now(display);

// Turn backlight ON after first frame is fully rendered. This prevents any
// visible flash from uninitialized state and matches the pattern from all
// esp-brookesia reference examples.
(void)bsp_display_brightness_set(95);  // kFullBrightness from crystal_core.cpp

const int64_t elapsed_ms = (esp_timer_get_time() - s_boot_start_us) / 1000;
```

**Why**: First frame is fully rendered in the framebuffer before backlight turns on. No visible flash, smooth boot experience.

---

## Important Notes

1. **BSP brightness is inverted**: `bsp_display_brightness_set(0)` = 100% duty = screen OFF, `bsp_display_brightness_set(100)` = 0% duty = screen ON. The value `95` is used by Crystal OS as "full brightness" (from `kFullBrightness` in crystal_core.cpp).

2. **Do not modify the BSP**: Keep the Waveshare BSP component unchanged to ease future updates. Control backlight at the application layer as the Brookesia pattern recommends.

3. **Both calls are required**: 
   - First call (set to 0): Establishes known state during LCD init
   - Second call (set to 95): Turns on backlight after UI is ready

4. **Header not needed**: `bsp_display_brightness_set()` is already declared in `bsp/display.h`, which is already included via line 15.

---

## Verification Steps

1. Build and flash firmware
2. Power cycle device multiple times (cold boot, not just reset button)
3. Confirm no flash visible during boot sequence
4. Confirm screen content is always correct, never corrupted
5. Check serial logs for any LEDC or backlight-related errors

---

## Reference

All esp-brookesia phone examples follow this pattern:
- `components/esp-brookesia/examples/esp_idf/esp_brookesia_phone_*/main/main.cpp`
- Each calls `bsp_display_backlight_on()` immediately after `bsp_display_start()`

Crystal OS should follow the same pattern but with finer control (OFF → render → ON) to prevent any visible flash.
