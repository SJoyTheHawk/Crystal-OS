# Bus transformation Phase 1 results

> Archived milestone: commands, package versions and pending statements below
> describe their capture date. See [current Bus guides](../../bus/README.md) for ongoing work.

Recorded 2026-11-16, Asia/Hong_Kong. Source baseline commit: `116b7de`.
Phase 1 changes are in the working tree, following that checkpoint commit.
Implementation and host validation are complete; physical P4 acceptance is
pending. Nothing has been flashed or installed on the board by this task.

## Firmware and package pair

| Item | Value |
| --- | --- |
| Firmware | `build/crystal_os.bin`, Crystal OS 0.17.0, 5,050,368 bytes |
| Firmware SHA-256 | `cc6b0cc02bf057b58507195d23d413ee31667a7172e885d7ee653c6d61dae667` |
| Package | `/tmp/phase17-bus-live.capp`, Bus Lite 0.2.8, 1,869,170 bytes |
| Package SHA-256 | `c919e4aa36615608b3fe1630f2d0163fc88d527b3d9474628d41b2c1c96af6f3` |
| Package signature | Ed25519, key ID 240, verified by the package tooling |
| Package identity | `com.example.bus3p` |
| Resources | 512 KiB Lua arena, 16 KiB app storage, one HTTP worker |
| Entry source | 44,279 bytes, below 64 KiB |
| File count | 118, below the 128-file limit |
| Catalog | 116 files, 1,807,554 bytes; largest file 16,384 bytes |
| Network hosts | `data.etabus.gov.hk`, `rt.data.gov.hk` |

Board configuration is the current Waveshare P4 build: `esp32p4` target,
ESP-IDF 6.1, ESP-Hosted C6 companion, 200 MHz PSRAM and 80 MHz flash. The firmware
image fits the 8 MiB app partition with about 40% free. Configuration was not
changed for Phase 1.

Install instructions and the step-by-step board checklist are in
[BUS_APP_QUICK_START.md](../../bus/BUS_APP_QUICK_START.md). The runtime cleanup fix needs
`app-flash` even on firmware already reporting 0.17.0. Then upload the live CAPP
through Developer Tools → Control Panel. Uninstall-before-replacement currently
deletes saved app data; wait for deferred storage cleanup before reinstalling.

## Implemented behavior

- Offline catalog search and the accepted P4 page layout remain in place.
  Selected routes fetch current stop sequences directly from KMB/LWB or CTB.
- Route-stop directions map `O`/`I` to `outbound`/`inbound`; each path component
  is percent-encoded. Service identity, string stop IDs and leading zeroes are
  preserved. Every stop occurrence retains its own sequence position.
- One continuous native stop list exposes every route position without
  Previous/Next controls. Shared selection dispatch avoids a Lua callback per
  stop. Names fill progressively from
  bounded bundled catalog pages; missing IDs are fetched once per sequence
  pass through the provider detail API. Failed details remain identifiable and
  retry when selected/refreshed. Selecting a stop preempts background metadata.
- Provider arrays are framed incrementally (at most 512 characters scanned and
  one record decoded per tick). Catalog scans inspect at most four JSONL rows
  per tick. These limits keep background work below the P4 callback watchdog;
  successful arrays are envelope-validated.
  Ordered stop results remain compact instead of retaining decoded raw trees.
- ETA uses exact Gregorian date/offset arithmetic in epoch seconds, accepts
  signed offsets and `Z` with optional fractional seconds, validates dates and
  stays within years 1970–2037 for the current 32-bit Lua integer contract.
- Valid timestamp rows are retained despite optional metadata disagreements,
  sorted by timestamp/source order and limited to three visible arrivals.
  Minutes are rounded; less than 30 seconds displays Arriving. Unsynchronized
  device time displays Time unavailable.
- Manual refresh, saved-stop reopening/removal and recent routes use the live
  path. Favorites retain route/stop occurrence context and do not save stale
  minute summaries as fresh information.
- Navigation/pause/destruction invalidate jobs before cancellation. One pending
  foreground request can wait for an asynchronously cancelled worker to retire;
  repeated taps do not create a request backlog. Resume refreshes the selection.
- Production `json.decode` and `state.get` release cJSON trees before propagating
  Lua allocation failures. JSON decoding rejects trailing non-whitespace data.
  The shared conversion is protected, and state-read C++ strings are destroyed
  before a Lua error is rethrown.

## Official response verification

The host fetched these official HTTPS paths on 2026-11-16 at approximately
17:29 HKT using macOS curl with certificate validation enabled:

| Provider | Outbound sequence | First stop | ETA rows in recorded response |
| --- | --- | --- | --- |
| KMB 1A | 35 positions | `A3ADFCDF8487ADB9`, SAU MAU PING (CENTRAL) | 6 |
| CTB 2 | 33 positions | `003155`, Sai Wan Ho (Grand Promenade) | 5 |

The counts describe the captured responses, not fixed service guarantees.
Both stop details and ETA data were retrieved for the first stop. Exact URLs,
generated timestamps, sizes and SHA-256 hashes are recorded in
`tools/fixtures/bus-phase1-providers/provenance.json`. These test-only recordings
are replayed through the app's production UI/JSON flow. They are outside the
package source tree and are not installed as cached live ETA.

KMB endpoint verification: [official 1A route-stop response](https://data.etabus.gov.hk/v1/transport/kmb/route-stop/1A/outbound/1).
CTB API contract: [Citybus API specification](https://www.citybus.com.hk/datagovhk/bus_eta_api_specifications.pdf).
Flutter adapter/reference logic was checked locally under
`/Users/szemy/Workspace/Gorbusapp/mobile/nextbus_flutter/lib/`.

The default Python HTTPS trust store failed in this environment; the successful
curl probe used the macOS certificate store. This says nothing about ESP-IDF
certificate validation on the physical board, which remains to be tested.

## Host validation

All of these pass:

```sh
python3 tools/test_phase17_bus_package.py
python3 tools/test_phase17_bus_catalog.py
python3 tools/test_phase17_bus_render.py
python3 tools/test_phase17_bindings.py
python3 tools/test_phase17_runtime.py
idf.py build
```

The existing LVGL harness exercises 480/720 pixel layouts with production Lua
allocator, JSON and UI bindings. It covers KMB/CTB URL shapes and directions,
selected service type, percent encoding, string IDs, both recorded responses,
35-stop continuous scrolling and final-row selection, repeated stop occurrences, metadata fallback and
retry, leap-day/timezone/rounding cases, malformed/trailing JSON, network
failure, empty ETA, saved occurrence reopening, cancellation, late callbacks,
pause/resume and 100 consecutive refreshes.

The real-catalog harness verifies every page of prefix `1`, repeated broad
searches, rapid replacement of a query, catalog metadata reuse, selected-stop
ETA and navigation. The 35-stop provider flow reaches and selects its final row
in one continuous list. Stop rows append one per scheduler tick. Each timer
callback performs at most one file read OR one JSON conversion. The catalog
case registers 53 callbacks including the shared selector and timer; the
provider case registers 38, both below the limit of 64.

Forced production JSON conversions fail ten times in a 160 KiB arena with zero
retained native cJSON allocations after each failure. The existing emergency-GC,
terminal OOM containment, stack balance and complete arena-release checks pass.
The production state/timer binding suite also preserves Clock behavior.

The attached host transcript predates the continuous-list change: it reports
the earlier 342,856-byte provider peak, 423,416-byte catalog peak and 62
callbacks. The above figures are from rerunning the suite on 0.2.8; the runtime
suite was run separately in this workspace because it was not in the attached
command list.

## Resource evidence and limits

| Environment/workload | Peak Lua payload | Arena |
| --- | --- | --- |
| Earlier Stage C P4 board run (historical) | 248 KiB | 512 KiB |
| Phase 1 0.2.8 host provider workflow including official recordings | 366,584 bytes | 524,288 bytes |
| Phase 1 0.2.8 host real-catalog repeated search/navigation | 414,136 bytes | 524,288 bytes |
| User-shared Bus Lite 0.2.6 P4 KMB workflow | 301 KiB | 512 KiB |
| Bus Lite 0.2.8 continuous-list P4 workflow | Pending board test | 512 KiB |

These are separate environments. Host pointers/block headers and test-driver
allocations differ from P4. The host catalog run does **not** meet a 25% payload
headroom target; it passes the configured arena without exhaustion. It does not
justify changing the P4 quota or claiming device headroom. Capture the physical
board's launch/search/stops/ETA/teardown checkpoints and largest free blocks
before accepting resource sizing.

Internal heap, PSRAM, native TLS/HTTP/UI peaks, timing budgets and ten repeated
install/use/uninstall cycles remain board acceptance work. The earlier clean
Stage C teardown is historical evidence, not a new Phase 1 resource result.

The user's 2026-11-16 P4 serial log shows a successful KMB 1A route-stop
response (2,991 bytes), stop metadata and repeated ETA responses (893-byte
bodies). At the stop and ETA checkpoints, Lua use peaked at 301/512 KiB, with
a 112 KiB largest free arena block. Internal free
heap is 234 KiB with a 156 KiB largest block; PSRAM free is about 24,315 KiB.
This is useful KMB evidence for 0.2.6, not acceptance of the new 0.2.8 list UI.
The same log records a Wi-Fi disconnect/reconnect during search and a 1,370 ms
`onCreate` callback against a 200 ms budget. The initial weather request times
out, then the HTTPS smoke test and a later weather request succeed after network
recovery. Investigate startup time separately; provider requests in the shown
Bus flow complete successfully.

The log's session-wide HTTP peak body reached 4,070 bytes. It also records a
second 1,157 ms `onCreate` call, a request cancelled
during pause, successful requests after resume, and a clean teardown
(`tracked=0 clean=1`). A Wi-Fi disconnect is followed by failed requests while
the device is offline and successful traffic after it reconnects. This is
useful lifecycle evidence for the prior package, but not acceptance of 0.2.8's
continuous list or a controlled offline-recovery test.

The latest 0.2.8 log records one app-level watchdog stop at 143670:
`App stopped responding (50 ms callback limit)`. It is followed by the normal
Bus `onPause`/`onDestroy` path, a clean resource checkpoint and a fresh
`onCreate`; there is no Guru Meditation, assert or MCU reboot in this log. The
most likely trigger is a scheduler tick doing too much catalog/provider work at
once, so the package now uses smaller per-tick framing and catalog scan slices.
The same log shows the reconnect delay: an automatic Weather request occupies
the single shared HTTP worker for about 15 seconds before the Bus request can
complete. Weather/location scheduling remains unchanged; this timing is
recorded as an architectural follow-up for the shared HTTP service.

### Phase 1 closure checklist

| Acceptance item | Evidence/status |
| --- | --- |
| Host package, catalog, production UI, bindings, runtime tests and P4 build | Passed on 0.2.8 in this workspace |
| Signed 0.2.8 CAPP | Built and Ed25519 signature verified with key ID 240 |
| KMB outbound live route stops and ETA | Demonstrated on the prior package; repeat on 0.2.8 |
| Continuous list, stable metadata updates and final-stop selection on P4 | Pending 0.2.8 board test |
| KMB inbound and CTB outbound/inbound live flows | Pending board test |
| Manual refresh and controlled network-failure recovery | Pending board confirmation; log has incidental Wi-Fi loss/recovery |
| Favorite save/reopen/remove after restart | Pending board confirmation |
| Pause/cancel/resume and clean teardown | Prior package log shows cancellation, successful resume requests and `tracked=0 clean=1`; repeat on 0.2.8 |
| 10 install/use/uninstall cycles | Pending |
| Startup responsiveness and board resource headroom | Pending 0.2.8 measurement; previous `onCreate` exceeded its 200 ms warning budget |

Phase 1 cannot be closed from the attached log because it predates 0.2.8 and
does not verify the new list behavior, CTB, both directions, persistence, or
the full repeated-cycle acceptance.

## Remaining phases

- Phase 1 board acceptance: KMB 1A and CTB 2, both directions, continuous
  stop-list scrolling,
  selected-stop ETA, manual refresh, failed network/recovery, navigation and
  cleanup, with current P4 resource measurements.
- Phase 2: joint stop matching and sequential KMB + CTB ETA merge. A joint
  selection currently uses its primary provider and shows explicit partial
  arrivals rather than claiming merged coverage.
- Phase 3: persistence validation and failed-write handling. Existing saved-stop
  and recent-route behavior remains available in this version.
- Phase 4: scheduled 20-second refresh, live countdown and further UX work.

The functional 0.3.0 release remains gated on the planned board acceptance.
