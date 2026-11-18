# Bus App Phase 4 Results

**Package:** Bus Lite 0.2.24

**Target:** ESP32-P4

**Signed artifact:** `/tmp/phase17-bus-live.capp` · key ID 240 · SHA-256
`72310a654cb668c4d0e51b8920e858b87be2c961c538b0a570c95856789da413`

The Lua entry source is 62,680 bytes, below the runtime's 64 KiB source limit;
the package requests a 4 MiB Lua quota.

Phase 4 implements scheduled ETA refresh and live countdown behavior in the
existing Bus Lite layout. The implementation uses the one reusable 100 ms UI
timer already required by the package callback budget.

## Implemented behavior

- Visible ETA rows recalculate from stored epoch seconds about once per second,
  including the existing 30-second **Arriving** threshold.
- The selected stop starts a fresh provider request after 20 seconds. A slow
  request cannot create a backlog because the controller still permits one
  active request and generation checks reject stale results.
- Favorites refresh their saved stops sequentially about every 30 seconds while
  the Favorites tab is visible. Each row displays up to three transient ETA
  values and shows loading, retry or no-arrival state without persisting times.
- Manual refresh remains available from the detail header. Pause, navigation and
  destruction invalidate the active request and pending favorite work; resume
  refreshes the visible detail selection or schedules the Favorites pass.
- Existing route, stop, joint-provider, persistence and full-width tab-touch
  behavior remains unchanged.
- Favorite card clicks now bubble through nested route and destination layout
  children to the card's existing callback, so the route number, destination,
  stop name and ETA area all open the saved stop without adding callbacks.

## Host validation

The production LVGL harness passes at 480 px and 720 px. It covers scheduled
refresh at the 20-second boundary, live timestamp rendering, favorite ETA
requests, cancellation, late callbacks, repeated refresh and callback-count
stability. Package and catalog checks pass, and the Lua entry source remains
below the runtime's 64 KiB limit.

P4 acceptance remains: install the signed package, leave a selected ETA page
open for at least 25 seconds, observe a new provider request, then wait on
Favorites for a refresh cycle. Verify Wi-Fi loss/recovery, pause/resume,
joint-route arrivals, stable heap/resource checkpoints and ten repeated
install/use/leave/uninstall cycles.

The card-touch routing fix is part of the Crystal UI firmware. Flash the
updated firmware with `app-flash` before testing that touch behavior; the
package itself remains ABI-compatible.
