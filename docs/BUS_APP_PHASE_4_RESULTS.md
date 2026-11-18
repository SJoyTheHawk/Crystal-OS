# Bus App Phase 4 Results

**Package:** Bus Lite 0.2.25

**Target:** ESP32-P4

**Signed artifact:** `/tmp/phase17-bus-live.capp` · key ID 240 · SHA-256
`42f98c31c89098039b00602c3e2e13637a2f21adf244eaba36f3377bc80e92b5`

The Lua entry source is 64,510 bytes, below the runtime's 64 KiB source limit;
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
- Favorites persist validated provider-specific ETA URLs and stop IDs. A joint
  favorite stores both KMB and matched CTB endpoints and replays them directly
  during refresh, without route-stop fetching or matching on each pass. Results
  merge by timestamp and retain successful arrivals when one provider fails.
- New joint saves wait until both stops are resolved. Existing joint favorites
  without endpoint records resolve and persist them once; failed writes leave
  the original record intact and retry on a later pass. Malformed URLs are
  rejected during loading. No countdown values are stored.
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
stability. Added regressions cover both saved endpoints, timestamp merging,
either provider failing, old favorite migration, failed migration writes,
malformed endpoint rejection, cancellation/late callbacks and production JSON
serialization of eight joint favorites. Package and catalog checks pass, and the Lua entry source remains
below the runtime's 64 KiB limit.

P4 acceptance remains: install the signed package, leave a selected ETA page
open for at least 25 seconds, observe a new provider request, then wait on
Favorites for a refresh cycle. Verify Wi-Fi loss/recovery, pause/resume,
joint-route arrivals, stable heap/resource checkpoints and ten repeated
install/use/leave/uninstall cycles.

The card-touch routing fix is part of the Crystal UI firmware. Flash the
updated firmware with `app-flash` before testing that touch behavior; the
package itself remains ABI-compatible.

## Endpoint refresh board check (0.2.25)

1. Install the new signed package using the Developer Tools control panel.
   This app-only change needs no firmware flash. The current replacement flow
   requires uninstalling first, which deletes saved favorites.
2. Search a joint route such as 101, open a stop and wait for the matched CTB
   stop to resolve. Save it, then return to Favorites.
3. Confirm refresh requests go directly to KMB and CTB ETA endpoints. There
   should be no route-stop or stop-detail request during normal favorite refresh.
4. Reboot and repeat on Favorites. Both saved provider URLs should be reused.
5. If one provider fails, confirm arrivals from the other remain visible.
   Navigation/pause during a refresh must not reopen work after leaving.

Older favorite state retained by a development/update workflow is migrated on
its first successful refresh. A normal uninstall/reinstall instead starts with
an empty list, so migration is covered separately by the host regression.
