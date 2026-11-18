# Bus App Phase 4 Results

**Package:** Bus Lite 0.2.29

**Target:** ESP32-P4

**Signed artifact:** `/tmp/phase17-bus-live.capp` · key ID 240 · SHA-256
`f076bc07bcefee27bf09c5b26a19ad089dc81d02a062671a794fd080020790f9`

The Lua entry source is 65,506 bytes, below the runtime's 64 KiB source limit;
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

## Network-ready subscription — 2026-11-19 (0.2.29)

Bus reads OS network readiness at launch/resume and subscribes to connectivity
changes. Wi-Fi must acquire IPv4 before provider requests start; Ethernet remains
a supported route. No connection is displayed offline, active responses are
invalidated before cancellation, and refresh passes stop until reconnect.
Reconnection refreshes the visible favorite/detail selection automatically.
Catalog search remains local. DNS/HTTP failures while IP is available still use
the existing request error handling; readiness is not an Internet health check.

Core reuses the existing UI dispatch and network revision; no app polling timer
or worker callback enters Lua. Subscribers replay initial state asynchronously,
coalesce paused notifications and release their callback slot on destruction.
The keypad allocates slots only for catalog letters, retaining two-column packing
and leaving room under the 64-slot limit. Private identifiers were shortened to
keep the entry file within 64 KiB; persisted records and endpoints are unchanged.

Validation: production Lua/UI harness passes at 480 and 720 pixels, including
offline startup, offline favorite opening, interrupted requests, ignored late
responses, reconnect refresh and pause/resume. Subscriber replacement, replay,
self-unsubscribe and teardown tests pass, as do package and runtime checks.
The signed package was rebuilt and signature-verified. No device was flashed.

The existing build cache identifies the newer local ESP-IDF 6.1.1 checkout.
Use that SDK for this workspace; an attempted build with the older 6.1 checkout
hit an errno compatibility error and a bootloader cache source mismatch.

### Board check

1. Flash the matching firmware with app-flash, then install Bus 0.2.29 through
   Developer Tools. Normal uninstall-before-replacement deletes favorites.
2. Save a stop, disable Wi-Fi (disconnect Ethernet too), and reopen Bus.
   Confirm No connection and no Bus provider requests.
3. Enable Wi-Fi. Once it obtains an IP, Favorites should refresh without a tap.
4. Open a stop and disconnect during refresh. Confirm No connection promptly,
   no automatic retries while offline, and automatic refresh on reconnect.
5. Repeat while Bus is paused, then resume. Confirm one current refresh flow,
   no duplicate/late provider result, and normal clock-ready countdown behavior.
6. Search a bundled route offline; search should still work, while opening its
   live stop data reports No connection until networking returns.

### Network-ready build result

ESP-IDF 6.1.1 build passed: signed firmware is 0x4d1000 bytes, with 40% of the
application partition free. Firmware SHA-256: d25e79caacc27c0c2c60348ae0dd77915b20e40fe3f58f93422f83fe6770af83.
This build includes the pre-existing concurrent DNS recovery work in the HAL;
this change only added a forward declaration there to fix compilation order.
No recovery policy was changed here. No flashing or board acceptance was performed.

### Favorites offline and completion status — 2026-11-19 (0.2.29)

Offline favorite ETA fields now retain their three-line placeholder (double
dashes) while the page status reports No connection. After the automatic
Favorites refresh finishes, the status returns to Tap a saved stop for live
arrivals instead of remaining on Refreshing arrivals... Host coverage checks
both transitions.

### Reconnect refresh status — 2026-11-19 (0.2.29)

The Favorites page now announces Refreshing arrivals only when a refresh pass
actually starts. A one-stop reconnect pass is covered through its final response;
completion returns the status to Tap a saved stop for live arrivals. This avoids
leaving a refresh label behind when the scheduler declines to start a pass.
