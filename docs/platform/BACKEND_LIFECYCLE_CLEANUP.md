# Backend lifecycle cleanup

Implemented November 16, 2026. Built-ins remain native. The saved app still opens
at boot and may fetch through its normal lifecycle. No reconnect grace period.

## Ownership and startup inventory

| Work | Trigger / owner after cleanup |
| --- | --- |
| Storage, crypto, trust/catalog, crash recovery | Local system initialization at boot |
| Display, touch, shell, watchdog | System startup; watchdog heartbeat every second |
| App registration | Creates/registers enabled instances, without running app hooks |
| Saved app | Shell restores saved card (slot-zero fallback); normal app lifecycle |
| Core service task | First-frame gate and existing 1.2-second OTA settling interval; starts networking, checks power commands and countdowns approximately every second |
| Wi-Fi / Ethernet | System connectivity; saved-network connection, retry/backoff and route selection preserved |
| SNTP | Connected/route event when automatic time is enabled |
| HTTP worker | Created at boot; sleeps on its queue; one transport request at a time |
| Weather / automatic location | Active Weather request session only; no autonomous core requests |
| HTTPS startup smoke test | Removed |
| Battery | Core checks approximately every 30 seconds, independent of app HTTP |
| Countdown alarm | Core checks deadlines regardless of foreground app; close/reset semantics unchanged |
| Core UI timers | Events 50 ms; clock/connectivity 1 s; countdown indicator/power 250 ms |
| Deferred package deletion | Paused 20-ms timer; management-task notification resumes it; one traversal step per tick; pauses again when empty |
| OTA | Worker blocks on task notification; explicit check/download/cancel wakes it; no automatic check |
| USB developer transport | Boot-started host-command listener, preserved |
| Web control panel / mDNS | Explicit Settings opt-in, preserved |
| Reset button | S3 hardware polling preserved |

## Weather lifecycle and interfaces

WeatherRequest is an app-owned asynchronous session. HTTP callbacks hold only a
reference-counted completion buffer, never an app or LVGL pointer. A live app
polls it on a tracked 250-ms timer. Parsing, location commits, cache writes and UI
updates happen on LVGL. Pause/destroy cancels the exact request ID, detaches the
completion and deletes the timer. Late completion only releases its own memory.

Weather displays cached data on entry and requests missing, unknown-age or
30-minute-old data. While active, successful refreshes use a monotonic 30-minute
interval; failures retry after 1, 3, 5, 10 and then 30 minutes. Manual refresh
bypasses backoff and coalesces repeated taps. Reconnection/route generation
changes allow active, due Weather to recover; they never launch a closed app.
A clockless successful reading does not create a per-tick refetch loop.

Location reads are cache/configuration only. crystal_location_snapshot() exposes
coordinates, label, automatic/resolved state and a revision.
crystal_location_accept() rejects outdated automatic results after settings
change. The Lua location.get() interface remains unchanged. Existing global
location storage keys and Weather reading keys are retained. Additional per-app
coordinate/mode keys detect changed-location readings across reboot. Legacy
readings remain visible but need one confirming fetch.

Automatic resolution is performed only as the first stage of an active request
when needed. Failed resolution uses the explicitly labelled Hong Kong fallback
for that request; it is not persisted as a successful lookup. Settings setters
never fetch. Unknown-age/invalidated readings are displayed with stale qualifiers.

The blocking core JSON adapter and global Weather request/dispatch path were
removed. UI_EVT_WEATHER remains a reserved enum value to avoid renumbering other
events. The old core HTTP test command delegates to the new session tests.
crystal_network_revision() exposes route transitions without registering an
app callback on the system event task.

Cancellation is cooperative: an in-flight DNS/TLS operation can still hold the
single HTTP worker until its transport step returns. Core power/battery/countdown
processing no longer waits for that worker.

## Verification

- P4 firmware build completed using ESP-IDF 6.1.
- tools/test_weather_requests.py: production request/location code,
  ASan/UBSan, P4 and S3 timeout variants. Immediate/late/cancellation-race
  completion, queue rejection, timeout, malformed responses, fallback, settings
  revisions, detached owner lifetime and cache-only getters.
- tools/test_weather_lifecycle.py: production Weather, Brookesia and
  LVGL under ASan/UBSan. Registration/other-app isolation, offline open,
  reconnection, 30-minute refresh, backoff, pause/resume, close, route changes,
  settings changes, cache display and late completion after native clear-data.
- tools/test_backend_idle.py: extracted production OTA worker/control
  functions with deterministic notifications; idle blocking, queued-before-wait
  work, check/download/cancel and invalid requests. Source boundary assertions
  verify no core app HTTP waits or startup smoke request.
- tools/test_app_platform_v2.py: package and lifecycle integration,
  including cleanup wakeup registration, bounded deletion, install admission
  and interrupted-uninstall recovery. Negative-case FAIL messages are expected
  in its recovery scenarios; the suite exits successfully.
- HTTP retry, route policy, registry tests and production screenshot capture pass.

The isolated full S3 build could not resolve its required components in the
installed P4 dependency tree (component manager disabled to avoid replacing
shared dependencies). This is not a completed S3 firmware validation.

A serial sample identifies a P4 running Bus, but does not establish that this
backend revision is flashed. No firmware was flashed by this task. Concurrent
Bus source/package changes were left untouched. Physical P4 reconnect timing,
slow-HTTP power/countdown behavior and interactive Weather switching remain to
be verified on this exact firmware.

## Device acceptance procedure

Use an app-only flash of the reviewed P4 image, preserving NVS and app storage.
Capture the console at the configured 1,000,000 baud.

1. Restore Bus, reboot and reconnect Wi-Fi; expect no weather_request entries,
   location requests or example.com smoke request. Repeat an Ethernet route switch.
2. Open Weather; expect cache first and at most one location/forecast chain when
   due. Rapid refresh taps must coalesce. Return Home or switch apps during both
   queued and active requests; verify cancellation and no hidden retry.
3. Open Weather offline; reconnect while it stays active. Expect one due refresh.
   Reconnect while Weather is paused and verify no Weather request.
4. Change location with Weather closed; verify no request until next activation,
   then confirm the new location. Clear Weather data while a canceled result is
   pending and confirm that it cannot repopulate the cache.
5. With slow/failing HTTP, verify countdown expiry, dim/wake and battery updates
   continue. Shared HTTP contention may remain until transport cancellation returns.
6. Verify explicit OTA check/cancel and download flow, and uninstall a large app;
   cleanup must finish incrementally and become idle afterward.
