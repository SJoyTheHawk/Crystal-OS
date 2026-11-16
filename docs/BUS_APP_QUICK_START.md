# Bus Phase 1 — install and board test

Updated 2026-11-16. Package **0.2.8** implements direct KMB/LWB and Citybus stop
sequences, metadata fallback and single-provider ETA. Host checks and the P4
firmware build pass. Physical-board acceptance is pending.

The stop picker is one continuous scrolling list. Progressive metadata updates
change row labels in place, preserving the scroll position. There are no stop
Previous/Next controls.

The implementation is `examples/bus3p/main.lua`. Keep the accepted Favorites /
Search / stop-picker / ETA layout. Joint-provider merging is Phase 2;
persistence hardening is Phase 3; scheduled refresh/countdown is Phase 4.
Manual refresh, existing saved stops and recent routes remain available.

## 1. Install the firmware once

This firmware includes the shared native list-selection and row-update APIs
used by the continuous stop list, plus native JSON cleanup when conversion
exhausts the Lua arena. OS version remains **0.17.0**; install the newly built
binary even if the board already reports that version.

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

Use **`/tmp/phase17-bus-live.capp`**. It is the signed **Bus Lite 0.2.8** package,
key ID 240, with network permissions for `data.etabus.gov.hk` and
`rt.data.gov.hk`. The Mac fixture server is unnecessary.

1. On P4, open Settings → Developer Tools → Control Panel and pair as usual.
2. Open the **P4's URL** in the Mac browser (previously `http://192.168.1.52/`;
   use the current address shown on the device).
3. Uninstall the previous Bus Lite through Manage Apps or the Control Panel.
   The current installer requires uninstall before replacement. This deletes
   its favorites and recent routes. Wait for app-storage cleanup to complete.
4. Upload `/tmp/phase17-bus-live.capp`, then apply/restart when prompted.
5. Confirm the installed name is **Bus Lite**, version **0.2.8**.

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
2. Select Search and type **1A**. Wait for catalog search to finish. Open the KMB
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
3. Remove it and reopen the app; confirm the final favorite stays removed.
4. Start a request, then immediately leave the page or app. Return and refresh.
   No old stop name or arrival should replace the current selection. Repeated
   refresh may briefly wait for a cancelled native worker to finish retiring.
5. Disconnect Wi-Fi, refresh, then reconnect and retry. Confirm a visible error,
   recovery and no crash. An unsynchronized clock shows **Time unavailable**;
   allow system time synchronization before judging countdown values.

## 6. Capture P4 resource evidence

Capture `BUS_RESOURCE` lines for launch, catalog/search, stops, ETA and teardown.
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
before accepting the current 512 KiB quota. Record current readings before
changing the quota.

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
ETA sorting, stale callbacks, async cancellation, pause/resume and UI reuse.
It forces JSON conversion OOM repeatedly and verifies native trees are freed.
HTTP and storage are controlled adapters; these checks do not establish board
TLS, touch, timing or resource acceptance.

The parser handles one response record per 100 ms scheduler tick. It scans at
most 2,048 response characters per tick and decodes either one record or the
small envelope. Stop metadata uses one bounded page read or one record decode
per tick. Route/service path components are percent-encoded; `O`/`I` becomes
`outbound`/`inbound`. Stop identities remain strings and repeated occurrences
retain their sequence positions. The single reusable timer and 64-callback
limit remain unchanged.

## After Phase 1 acceptance

Phase 2 adds CTB stop matching and merged arrivals for joint routes. Until then,
a joint catalog selection uses its primary provider and explicitly shows
**Partial arrivals: KMB only** (or the relevant provider). Phases 3 and 4 harden
persistence and implement scheduled refresh/countdown. Package **0.3.0** remains
the later functional release after board acceptance.
