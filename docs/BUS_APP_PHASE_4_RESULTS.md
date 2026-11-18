# Bus App Phase 4 Results

**Package:** Bus Lite 0.2.26

**Target:** ESP32-P4

**Signed artifact:** `/tmp/phase17-bus-live.capp` · key ID 240 · SHA-256
`5438676c9e3a740d71f3390f5e83b83909e77bce59716d4fc26564af9050f72f`

The Lua entry source is 65,426 bytes, below the runtime's 64 KiB source limit;
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


## Clock-ready subscription correction — 2026-11-19 (0.2.26)

The reported 29-million-minute countdown was caused by subtracting an unset,
but positive, boot clock from provider epoch timestamps. The status bar also
showed an unset clock. Bus now uses the OS readiness state and subscribes to
clock-ready notifications, instead of deciding readiness from an epoch threshold.

- The OS publishes clock revisions after RTC initialization, SNTP synchronization
  and successful manual setup. Its existing UI dispatcher delivers subscriptions
  on the LVGL task; no Lua or LVGL call runs on an SNTP worker.
- Lua time.is_ready(), time.on_ready(callback) and time.off_ready() expose this
  state. One replaceable subscription consumes one callback slot per app. An
  already-ready clock replays asynchronously. Pause coalesces notifications;
  resume delivers them and destruction removes the subscriber.
- Pending time shows -- and Waiting for device time. Notifications immediately
  recalculate cached favorite/detail arrivals and rebase refresh deadlines.
  Favorites recalculate visible minutes between network fetches as well.
- Host tests exercise unset zero/positive clocks, a plausible timestamp while OS
  readiness is false, immediate repaint after notification, repeat corrections,
  ready-before-subscribe, capacity, replacement, unsubscribe during dispatch,
  pause/resume, cleanup and existing Bus flows at 480/720 pixels.
- Firmware build passes; signed app is 0x4d1000 bytes (40% slot free), SHA-256
  95f5d5c05ae9c56a93f2f81649b48cd360984ec0aa9432cca6c3972fbb508314.
  This build uses the current working tree, including concurrent Wi-Fi work.

Install order: app-flash the new firmware, then install signed Bus 0.2.26 from
/tmp/phase17-bus-live.capp via Developer Tools. Old firmware lacks the binding
and the package reports an explicit update-firmware error. Normal app replacement
requires uninstalling and deletes favorites; firmware app-flash itself retains
app storage. Neither flashing nor installation was performed in this change.

Board check: start with unset time if available, open Favorites and a stop ETA,
confirm there is no huge countdown, then synchronize or set the system clock.
Countdowns should appear without leaving the page or manually refreshing.
Also open Bus with time already ready, and synchronize while Bus is paused,
then resume. Retest normal KMB/CTB/joint refresh and reboot persistence.

### Board feedback — 2026-11-19

After the clock-ready subscription change, the user reported that the app is
running well and requested repository cleanup and a commit. This records
positive board feedback for the correction; it does not establish completion
of every unset-clock, pause/resume, network-recovery or ten-cycle soak check
listed above. Those checks remain separately tracked.
