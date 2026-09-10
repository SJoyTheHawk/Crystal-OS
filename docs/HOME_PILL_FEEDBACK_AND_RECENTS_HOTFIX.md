# Recents Null-Safety Hotfix and Home-Pill Feedback

## Summary

Disabling Brookesia's Recents Screen leaves `home.getRecentsScreen()` null. The
phone manager must treat that as a supported configuration throughout app
lifecycle handling. Crystal's home pill also gains direct interaction feedback:
a restrained white glow on touch, plus up to 8 px of lift during an upward drag.

## Recents hotfix

- Keep `stylesheet->home.flags.enable_recents_screen = 0` so the Recents widget
  is not constructed.
- In `ESP_Brookesia_PhoneManager::processAppCloseExtra()`, check that the
  Recents pointer is non-null before calling `checkVisible()`. This path runs
  when Crystal's side switch starts a new app and Brookesia closes the previous
  app to enforce `max_running_num = 1`.
- Keep the manager guards that disable Recents gesture state and refuse to
  classify a long bottom swipe as Recents when the widget does not exist.
- Do not change Crystal's side switching or the one-running-app limit.

`managed_components/` is Git-ignored. The Brookesia manager changes are local
dependency patches and must be moved into a durable component override or
reapplied whenever managed dependencies are regenerated.

**Status 2026-09-10.** The patched `esp_brookesia_phone_manager.cpp` was force-added
past `.gitignore` in `62256ad`, so it is tracked and survives a clone — but a
dependency re-resolve or `fullclean` regenerates the directory and reverts it
silently. Because `enable_recents_screen = 0`, the symptom is a null dereference on
side-switch rather than a build failure. The durability options are compared in
`PHASE_11_BUG_FIXES_V3.md`; a durable component override is the recommendation and
is still to be done.

## Home-pill feedback and input priority

- A transparent, press-locked shell target reserves a centered 188 x 24 px area:
  the 172 px pill width plus 8 px of horizontal margin on each side. It wins
  LVGL hit testing before an underlying text field can focus and open the
  keyboard, and it retains the touch after the drag leaves the target.
- A touch beginning inside that target immediately adds a subtle white shadow
  glow to the pill. Touches elsewhere in the bottom band remain app-owned.
- Upward travel maps linearly over the existing 80 px Home threshold. The pill
  remains horizontally centered, lifts from 0 to 8 px, and strengthens its
  glow as the drag progresses.
- Reversing the drag to its starting point returns the pill to its resting
  position while retaining the touch-down glow.
- Release or cancellation eases the current lift and glow to zero over 120 ms.
- Wake-only touches, keyboard-hidden state, and gestures assigned to another
  owner cannot leave the pill elevated or glowing.
- Pill dimensions, Home commit behavior, Settings and Quick Settings dismissal,
  horizontal app switching, and disabled Recents behavior remain unchanged.

## Verification

- Build the ESP-IDF firmware successfully.
- Repeatedly side-switch between apps with `max_running_num = 1`; the old app
  closes, the new app starts, and no null dereference or reboot occurs.
- Confirm a long bottom-edge drag-and-hold never opens Recents or changes app
  lifecycle unexpectedly.
- Check touch-down glow, proportional lift, reverse-drag restoration, cancelled
  release, and committed Home release on the device.
- Recheck Settings dismissal, Quick Settings dismissal, keyboard behavior,
  wake-touch handling, and horizontal app switching.
- Place a text field beneath the pill target. Taps and upward drags inside the
  centered 188 x 24 px target must not focus it or open the keyboard; touching
  the field outside that target must continue to work normally.

## Interfaces and assumptions

- No public APIs or shared types change.
- The centered 188 x 24 px pill target exclusively owns its input. App content
  outside that target remains interactive, including the rest of the bottom
  band.
- Crash correction is verified before device-testing the animation.
