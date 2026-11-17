# Bus Phases 1–3 — install and board test

Updated 2026-11-18. Package **0.2.22** implements direct KMB/LWB and Citybus
stop sequences, metadata fallback, joint-provider ETA merging, and Phase 3
persistence validation. Possible alphabetic route keys now reflow into a
consistent two-column grid. The detail header is more compact, and stop entries
use content-sized heights with visible spacing. Detail pages now use the same
16 px scaled side padding as Favorites and Search, and the stop list fills the
remaining screen height to its bottom status line. The user reports the Phase 3 P4
board checklist passed.

Favorite and recent-route records are validated on load. Failed writes preserve
the current lists and show an error. The board checklist includes app restart
and persistence-write failure cases.

Version 0.2.17 keeps notifications in the bottom status field and removes the
duplicate guidance above route results and stops. Check Search, open a route,
wait for stops, open arrivals, and return to Favorites: all bottom statuses
use the same larger 20 px font. There should be no upper "Choose a route and
direction" or "Loading bus stops..." line, and "Tap a stop for arrivals"
appears only at the bottom. The compact header and stop-row spacing use APIs
already present in current firmware, so this visual adjustment needs no flash.

For 0.2.14 and later, first flash the updated firmware using the app-flash command below,
then uninstall the old Bus Lite package and install /tmp/phase17-bus-live.capp
through the Control Panel (uninstall removes saved stops). Search now scans
all resident routes in one callback, removing the 64-record batch and
one-match-per-tick pauses. Version 0.2.16 also publishes all visible route rows together.

In **Settings → Developer Tools → Extended Lua Timeout**, off selects the
5-second default; on selects 30 seconds. The setting survives reboot and takes
effect at the next Lua invocation. Test off → on → leave/reopen → reboot → off,
then search 1, 1A, 68X and 101, page results and change queries during publication.
Capture BUS_SEARCH scan/render/ready timestamps for actual P4 latency.
The separate task watchdog retains its existing settings; extended Lua time
can still block the UI and produce task-watchdog reports.

Version 0.2.15 displays **Loading...** for unresolved stop names, without a stop
ID, sequence prefix or retry suffix. Names replace placeholders in place when
resolved. Installing this package alone is sufficient on the latest firmware.

On its first scheduler callback, the app reads the complete bundled route
catalog into its 4 MiB Lua arena. The signed catalog includes a compact startup
route index retained as raw page strings; only visible results expand into
tables. This avoids thousands of startup arena allocations.
Search keys remain disabled during this load.
After it completes, numeric keys remain visible and disable when unavailable;
alphabetic keys hide unless they can continue the current prefix. Valid letters
reappear as the prefix changes. Clear and backspace update the keypad immediately.
Detailed stop metadata remains loaded on demand. Preload reads all compact
route pages together; it does not expand all records into tables.

Version 0.2.16 removes record/file/row timer pacing throughout the app. Check
that the catalog enables without pauses between its file-read logs, route
results appear together after typing, and each stop response populates the
complete list and available bundled names in one callback. Test a long route,
Loading... rows, joint ETA, refresh, navigation and pause/resume. Provider HTTP
requests remain sequential and asynchronous. The normal 200 ms typing debounce
and selected 5/30-second Lua deadline remain. Capture P4 elapsed time and
resource checkpoints; host tests cannot establish board responsiveness.

Earlier package history (the package-only notes below refer to those versions):

Version 0.2.10 still timed out on P4 during preload. Version 0.2.11 removes
on-device prefix-map construction: the host bundles a small keypad index.
Startup reads one compact file per callback without scanning its records.
The readiness update, search-page update and resource checkpoint use separate ticks.
Install `/tmp/phase17-bus-live.capp` through the Control Panel; this correction
is package-only and does not require `app-flash`. Uninstalling the previous
package removes saved stops. Expect `BUS_CATALOG read route-search-000.txt`
through `route-search-008.txt`, then `read keypad.txt`, `keypad_ready 2115`
and `BUS_RESOURCE catalog_loaded`. Confirm
search keys enable, then search 1A/68X/101, change result pages and open
stops/ETA. Capture startup and teardown logs for the P4 timing retest.
The separate roughly 1.46-second `onCreate` warning remains a performance follow-up.

P4 logs now confirm 0.2.11 preload succeeds; a later timeout occurs during
typing. Version 0.2.12 skips unchanged keypad states, avoids search-scan string
allocations and publishes one result row per tick. Retest repeated 1/1A, clear,
68X/backspace and 101, including edits before results finish. Save the
BUS_SEARCH key/key_ready/scan/render/ready markers if it stops again.

Version 0.2.13 applies the S3 keypad visibility behavior on P4: impossible
digits stay visible but disabled, while impossible letters hide and valid
letters return. Install the rebuilt CAPP through the Control Panel; no firmware
flash is needed.

The stop picker is one continuous scrolling list. Progressive metadata updates
change row labels in place, preserving the scroll position. There are no stop
Previous/Next controls.

The implementation is `examples/bus3p/main.lua`. Keep the accepted Favorites /
Search / stop-picker / ETA layout. Phase 2 implements joint-provider merging;
Phase 3 covers persistence hardening, and Phase 4 covers scheduled
refresh/countdown.
Manual refresh, existing saved stops and recent routes remain available.

## 1. Install the firmware once

The package-owned Bus Lite icon also requires the launcher icon fix in the
current firmware. Earlier firmware reset constructor-assigned package icons
during installation, replacing them with the default. Flash the rebuilt OS
with `app-flash` and reboot; an installed package already containing
`assets/icon.bin` does not need to be uninstalled or reinstalled for this fix.

This firmware includes the shared native list-selection and row-update APIs
used by the continuous stop list, plus native JSON cleanup when conversion
exhausts the Lua arena. OS version remains **0.17.0**; install
the newly built binary even if the board already reports that version.

From the repository terminal with ESP-IDF 6.1 enabled:

```sh
cd "/Users/szemy/Workspace/ESP32 Crystal OS"
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
printf 'P4 serial port: '
read -r PORT
idf.py -p "$PORT" app-flash monitor
```

`app-flash` preserves app storage. Keep the serial monitor open during testing.
No flash has been performed as part of the implementation.

## 2. Install the live package through the Control Panel

Build `/tmp/phase17-bus-live.capp` from the current source below. The previous
signed package may be an older version; confirm the package is **Bus Lite
0.2.22**, key ID 240, with network permissions for `data.etabus.gov.hk` and
`rt.data.gov.hk`. The Mac fixture server is unnecessary.

1. On P4, open Settings → Developer Tools → Control Panel and pair as usual.
2. Open the **P4's URL** in the Mac browser (previously `http://192.168.1.52/`;
   use the current address shown on the device).
3. Uninstall the previous Bus Lite through Manage Apps or the Control Panel.
   The current installer requires uninstall before replacement. This deletes
   its favorites and recent routes. Wait for app-storage cleanup to complete.
4. Upload `/tmp/phase17-bus-live.capp`, then apply/restart when prompted.
5. Confirm the installed name is **Bus Lite**, version **0.2.22**.

Do not choose an older `/tmp/phase17-bus.capp` or
`/tmp/phase17-bus-stagec.capp`. An explicitly generated fixture package has the
name **Bus Lite (fixture)** and remains a separate development option.

To rebuild the live CAPP later (use Python with `tools/requirements-pack.txt`):

```sh
PACKAGE_KEY="$HOME/.crystal_keys/phase17-test-ed25519.pem"
KEY_ID=240
python3 tools/crystal.py pack examples/bus3p --key "$PACKAGE_KEY" --key-id "$KEY_ID" -o /tmp/phase17-bus-live.capp
python3 tools/crystal.py inspect /tmp/phase17-bus-live.capp
```

## 3. Test KMB 1A

1. Launch Bus Lite. Confirm Favorites/Search tabs, blank input, clear/backspace
   symbols and normal empty state. Stop the Mac fixture server if it is running.
2. Open Search immediately. While the route catalog loads, confirm route keys
   are disabled. After the `BUS_RESOURCE catalog_loaded` checkpoint, confirm
   keys become active. At an empty query only catalog-leading characters should
   be enabled; after typing **1**, `A` and valid numeric continuations should be
   enabled while unrelated letters stay disabled. Clear and backspace should
   recompute the key state. Available letters should always pack alphabetically
   into consecutive two-column rows, even when only sparse letters remain.
   Tap a reflowed letter and confirm it enters the displayed character. Then
   type **1A** and open the KMB
   card **To STAR FERRY** (outbound). The opposite card is **To SAU MAU PING
   (CENTRAL)**. These destinations come from the packaged snapshot; provider
   sequence data is fetched live.
3. Confirm a numbered, scrollable stop list appears. Names may fill in
   progressively from the bundled metadata. Missing names trigger sequential
   provider detail requests and remain identifiable by stop ID if a lookup
   fails. These updates should not jump the list back to its first row.
4. Scroll to the final stop, then back toward the beginning. The full sequence
   is one list, with no Previous/Next controls. Select a stop near the end and
   confirm its stop ID is used for ETA. The captured 35-stop response is only a
   test sample, not a permanent stop count.
5. Select a stop. Confirm the separate ETA board shows the selected name and up
   to three arrivals with **KMB** labels and provider remarks. Times are rounded
   minutes; less than 30 seconds displays **Arriving**. Provider timestamps are
   converted to epoch seconds with calendar and timezone arithmetic.
6. Tap the top-right refresh symbol. Confirm fresh results, or a visible network
   error with retry guidance. A successful response with no valid arrival
   timestamps correctly shows **No arrivals available**; test during service
   hours and try another stop before treating that as a failure.
7. Back returns to the stop list, then Search. Open the opposite 1A direction
   and repeat a stop/ETA check. Its request must use `inbound/1`.

Expected outbound request shapes:

```text
https://data.etabus.gov.hk/v1/transport/kmb/route-stop/1A/outbound/1
https://data.etabus.gov.hk/v1/transport/kmb/stop/<string-stop-id>
https://data.etabus.gov.hk/v1/transport/kmb/eta/<string-stop-id>/1A/1
```

Stop-detail requests occur when bundled metadata is missing. The HTTP service
logs request IDs and status; it does not promise to print complete URLs or raw
response bodies. The host suite verifies exact URLs independently.

## 4. Test Citybus 2

1. Search **2** and choose a **CTB** card, not KMB. The outbound catalog card is
   **To Central (Macao Ferry)**; inbound is **To Grand Promenade**.
2. Confirm the ordered stop list, stop names and continuous scrolling through
   the final stop; there are no Previous/Next controls.
3. Select a stop and confirm **CTB** labels on returned arrivals.
4. Refresh, go Back and test the other CTB direction. Leading zeroes in CTB stop
   IDs must survive metadata, ETA requests and saved stops.

Expected outbound request shapes:

```text
https://rt.data.gov.hk/v2/transport/citybus/route-stop/CTB/2/outbound
https://rt.data.gov.hk/v2/transport/citybus/stop/<six-digit-stop-id>
https://rt.data.gov.hk/v2/transport/citybus/eta/CTB/<six-digit-stop-id>/2
```

## 5. Check saved stops and navigation

1. Save a selected stop, go Back to Favorites and reopen it. Confirm the same
   provider, route, service, stop ID and sequence are requested.
2. Favorites says **Tap to load live arrivals** instead of persisting an old
   minute countdown as current information. Reopening fetches fresh ETA.
3. Remove it, close and relaunch Bus Lite; confirm the final favorite stays
   removed. Save another stop and relaunch to confirm the save persists.
4. Confirm failed save/remove writes leave the existing list and action intact.
   A failed recent-route write must not add that route to the recent list.
5. Start a request, then immediately leave the page or app. Return and refresh.
   No old stop name or arrival should replace the current selection. Repeated
   refresh may briefly wait for a cancelled native worker to finish retiring.
6. Disconnect Wi-Fi, refresh, then reconnect and retry. Confirm a visible error,
   recovery and no crash. An unsynchronized clock shows **Time unavailable**;
   allow system time synchronization before judging countdown values.

### Phase 3 P4 acceptance result — 2026-11-18

The user reports that all seven Phase 3 checks passed on the P4: favorite save,
relaunch and reboot persistence; removal persistence across relaunch and reboot;
recent-route display, identity, deduplication/order and five-item cap; joint
favorite reopening with both provider arrivals; and the eight-favorite limit.
Host tests separately cover malformed stored entries and simulated write
failures. The board checklist did not inject a storage write failure.

## 6. Capture P4 resource evidence

Capture `BUS_RESOURCE` lines for launch, catalog-loaded, search, stops, ETA and teardown.
They are emitted at checkpoints once per app launch, without recurring idle
logs. Compare with `BUS_APP_PHASE_1_RESULTS.md`; host memory figures and older
S3 reports are not P4 measurements.

The attached earlier P4 log shows successful KMB route-stop and ETA requests,
with a 301 KiB Lua peak in a 512 KiB arena and a 112 KiB largest free block at
stop/ETA checkpoints. It predates Bus Lite 0.2.8, so it does not cover the new
continuous-list UI or Citybus. It also reports `onCreate` durations of 1.37 s
and 1.15 s against a 200 ms warning budget; capture the new version's startup
time as part of acceptance.

Check for callback-budget warnings, Lua quota failures, stable internal heap /
PSRAM and clean teardown (`tracked=0`, `clean=1`). Repeat launch/use/leave and
include a long route and a final-page stop. Ten install/use/uninstall cycles
remain part of complete app-platform acceptance.

Phase 1 board acceptance requires both providers, both directions, complete
stop-list scrolling, selected-stop ETA, refresh, failure recovery and clean navigation
without the Mac server. Confirm the P4 arena peak has at least 25% headroom
before accepting the current 4 MiB quota (4096 KiB, requested 2026-11-17).
Earlier 512 KiB readings remain historical; measure this package on P4.

## Host checks and implementation contracts

```sh
python3 tools/test_phase17_bus_package.py
python3 tools/test_phase17_bus_catalog.py
python3 tools/test_phase17_bus_render.py
python3 tools/test_phase17_bindings.py
```

The render suite uses production Lua allocator, JSON conversion and UI bindings
with real LVGL at 480/720 pixels. It checks complete catalog paging, repeated
stop occurrences, exact provider URLs, metadata fallback, timestamp offsets,
ETA sorting, stale callbacks, async cancellation, pause/resume, UI reuse,
malformed persisted records, duplicate/over-limit entries and failed state writes.
It forces JSON conversion OOM repeatedly and verifies native trees are freed.
HTTP and storage are controlled adapters; these checks do not establish board
TLS, touch, timing or resource acceptance.

The parser handles the complete response in one callback, decoding individual
records to avoid a duplicate full response tree, then validating the envelope.
Stop metadata reads relevant source pages and resolves all available names in
the same callback. Route/service path components are percent-encoded; `O`/`I` becomes
`outbound`/`inbound`. Stop identities remain strings and repeated occurrences
retain their sequence positions. The single reusable timer and 64-callback
limit remain unchanged.

## Phase 2 joint-route acceptance

Phase 2 adds CTB stop matching and merged arrivals for joint routes. Search for
**101**, select a **KMB/CTB** card, choose a stop and verify requests proceed
through KMB ETA, CTB route-stop matching and CTB ETA. The arrival rows retain
their **KMB** or **CTB** labels. If one source fails, successful arrivals remain
visible with a partial-data status. Phases 3 and 4 harden
persistence and implement scheduled refresh/countdown. Package **0.3.0** remains
the later functional release after board acceptance.
