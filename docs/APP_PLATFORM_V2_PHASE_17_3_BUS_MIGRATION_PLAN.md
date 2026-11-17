
# P4 Bus App Transformation — Implementation Plan and Code Guide

## 1. Goal and working principles

Build a functional Lua Bus package for ESP32-P4 covering **route discovery, joint-route merging, complete stop browsing and live ETA**. Use the Flutter app at `/Users/szemy/Workspace/Gorbusapp/mobile/nextbus_flutter` as a reference for behavior and data relationships, adapting processing and scheduling to P4.

The first milestone is working pages and connected data flows. Detailed matching accuracy, provider exceptions and visual refinement follow after this milestone and its resource measurements.

Preserve the accepted P4 layout: Favorites/Search, blank route input, clear/backspace symbols, readable destinations and stops, back arrow, and top-right refresh.

During implementation:

- Replace the acceptance-only scope in `docs/APP_PLATFORM_V2_PHASE_17_3_BUS_MIGRATION_PLAN.md` with this plan.
- Add the verified P4 API patterns below to `docs/APP_PLATFORM_V2_CODE_GUIDE.md`.
- Keep S3 reports as historical evidence. Record P4 measurements separately.
- Use real provider responses for live operation; retain explicitly labeled fixtures for repeatable tests.

## Current implementation status — 2026-11-16

Stage A is closed for the bounded fixture baseline. Bus Lite **0.2.3** (160 KiB
Lua arena, 16 KiB app storage, one worker) was installed through the P4
developer control panel and exercised through search, route, stops, ETA,
Favorites, and reopening a saved stop. The saved-stop callback timeout was
removed by keeping arrival minutes transient and avoiding synchronous state/UI
work inside the HTTP callback. No crash was reported in the retest. The
baseline production-UI/LVGL checks pass at 480/720 pixels with 64 callbacks;
the current Stage C package artifact is recorded below.
The checkpoint logger is present, but the supplied board excerpt did not
include its numeric `BUS_RESOURCE` lines; those P4 measurements remain to be
recorded during Stage E.

The Lua memory-failure fixes are included in the successful ESP-IDF build for
OS **0.17.0**. Firmware image `build/crystal_os.bin` SHA-256 is
`9892f2612facc0eb6b24817ce441b553a88e9c9a1fc03cd1726d2afafe743faa`; the Bus
package entry source SHA-256 is
`954f439d3c67c794bb915fc235f98de9487deb55ada4c4f4d83f864e897b2856`. The
configured target is ESP32-P4 rev 1.x on the P4 Function EV board profile, with
32 MiB flash, 200 MHz octal PSRAM and hosted ESP32-C6 Wi-Fi.

Stage B host preparation is complete and Stage C catalog-backed page integration is in progress. `tools/build_phase17_bus_catalog.py`
creates normalized KMB/CTB/joint route pages, operator-qualified stop metadata,
source hashes, and split indexes from recorded provider snapshots. Data pages
use JSON Lines so the P4 Lua reader can decode one record at a time under the
512-node conversion limit. The first 2026-10-02 catalog output contains 2,115
route records (160 joint), 6,753 KMB stops and 2,586 CTB stops in 115 files,
totaling 1,802,584 bytes. JSON Lines pages are capped at 16 KiB (below the
32 KiB contract); the 128-file and 5.5 MiB package limits are enforced as build
errors. The current pinned route source is the recorded combined
`routeFareList` snapshot; an official-feed refresh is a source-update task and
is recorded in the manifest rather than silently substituting live data. The
Stage C now connects those assets to the Lua reader. The current signed host
package is 1,847,721 bytes (SHA-256
`48cd34b505df5c898f6eefbabea927d69a5db45170242d608ca4717216326bac`) and the
entry source SHA-256 is
`e2d3d3ee11bda3b5f89de2f37a31f2e13ce2dc1b12abd3eea5717937144277cb`. These are
host artifacts; the board acceptance pass must record the flashed image and
package actually installed on P4.

### Stage C launch correction — 2026-11-16

The user board log demonstrated a launch failure at the original 160 KiB
quota (109 KiB used at launch_begin, 131 KiB peak payload at teardown).
Approximately 24 MiB PSRAM remained free. Payload accounting excludes block
headers; fragmentation also limits available allocations. This was an app
arena failure, not evidence of whole-board memory exhaustion.

Bus Lite 0.2.4 raises the provisional quota to 512 KiB. It retains only eight
search results, rescanning sorted source pages on Next/Previous; the full
catalog remains accessible. Full manifest provenance and descriptor tables
are discarded after extracting filenames/letters. Forward references for
status/checkpoint functions are now local and resolved before callbacks run.

The old UI test mocked JSON and omitted fs, so it exercised the small fixture
fallback, not the installed catalog. The extended existing production harness
now loads real assets and uses production json_to_lua, allocator and UI code.
It covers all pages of prefix 1, 39 subsequent prefix searches, repeated stop
IDs, 17-stop paging, ETA navigation, pause/resume and stale callbacks.
Both 480px and 720px runs pass; peak payload is 379,248 / 524,288 bytes
(27.7% payload headroom). Occupancy including headers can be higher (434,688
bytes at one checkpoint), so this is not proof of 25% total arena headroom.
A 448 KiB trial passed but had less than 25% payload headroom. Host pointers,
GC timing and test adapters differ from P4; quota remains provisional until
board measurements. Isolated forced-OOM/teardown tests still pass at 160 KiB.
No firmware changes or flashing are required by this correction.

### Stage C callback correction — 0.2.5

The next P4 log confirms 0.2.4 launches: 116/512 KiB current payload,
178 KiB peak, 290 KiB largest free arena block at launch_ready. Three search
attempts then exceeded the 50 ms callback limit; peak payload reached at most
250 KiB in the supplied log. Teardown reports tracked=0, clean=1. Startup
also took 870–919 ms against the 200 ms lifecycle budget. These are board
observations, not a completed Stage C acceptance pass.

The reader previously decoded all records in a 16 KiB page per timer tick.
0.2.5 keeps a page cursor and does one file read OR at most one needed JSON
conversion per tick. Up to 16 bounded text checks skip irrelevant records.
Runtime directory entries filter route pages by actual initials and stop
pages by provider-qualified ID ranges. All route matches remain reachable.
A small generated text directory replaces launch-time provenance/index JSON
conversion. Existing manifest/provenance files remain packaged for auditing.
The generator enforces the same file/package limits including this new file.

The existing host catalog/UI suite checks work bounds per tick, zero catalog
JSON decodes during launch, complete prefix paging, duplicate stop occurrences,
17-stop lists, navigation and late callbacks. Host results do not establish
P4 callback timing; retest search completion and startup warning on the board.
No runtime watchdog or firmware changes are part of this correction.

### Stage C P4 retest — 0.2.5 (partial pass)

The supplied board log starts at uptime 1,023,080 ms and shows installation
and launch at 1,094,688 ms. It reaches launch_ready, catalog_loaded,
search_ready, stops_loaded and eta_complete. No Lua quota failure, callback
timeout, app destruction or reboot appears in this excerpt. The log does not
print the package version/hash; attribution to 0.2.5 follows the retest context.

- launch_ready: 179/512 KiB used, 179 KiB peak, largest free arena block 290 KiB.
- search_ready: 132/512 KiB used, 235 KiB peak, largest free block 206 KiB.
- eta_complete: 148/512 KiB used, 247 KiB peak, largest free block 186 KiB.
- Internal free memory remains 236 KiB and largest internal block 156 KiB at
  the recorded checkpoints; roughly 24 MiB PSRAM remains free.
- catalog_loaded to search_ready spans 7,093 ms. This includes timer scheduling
  and any intervening typing/debounce; it is not an isolated query benchmark.
- onCreate still takes 746 ms against the 200 ms lifecycle budget. The startup
  performance issue remains open despite the successful search retest.

The pasted fixture terminal history may mix multiple tests and lacks request
timestamps. Earlier /api/routes entries cannot be assigned to this launch;
the current catalog path searches offline. The current fixture only supplies
1A KMB, 2 CTB and 68X KMB. Requests for 1 or 101/101R therefore return HTTP 200
with the 22-byte {"stops":[],"etas":[]} body. These empty stop lists do not
establish a provider or UI failure. 101 can validate joint search identity,
but this fixture cannot validate its stop/ETA flow.

Mark launch/search/fixture stops/ETA stability as observed in this excerpt.
Full Stage C acceptance still needs explicit long-route 68X paging, joint
identity inspection, missing-metadata retry, cancellation/teardown and repeat
cycle evidence. The excerpt contains no final teardown checkpoint, so it does
not prove leak-free teardown or ten-cycle stability. No new package is needed
to continue those checks; Stage D live provider work remains separate.

### Stage C 68X follow-up — old fixture response

The next P4 excerpt covers request IDs 18–33 (uptime 1,625,383–1,684,082 ms).
All 16 requests complete with HTTP 200 and no quota, callback deadline or
reboot error appears. Internal heap after each response is 242,211 bytes and
largest internal block is 159,744 bytes. PSRAM varies slightly during requests;
this excerpt alone does not establish long-term leak freedom.

The supplied Mac requests select 68X in both directions, but only stop IDs
001–003 appear. Board stop response bodies are 289 bytes (IDs 18, 22 and 30),
which exactly matches the previous three-stop 68X fixture. The current 17-stop
fixture produces a 1,665-byte compact JSON body. The likely cause is a Python
fixture process started before the long-route change. Restart that server,
verify 17 stops over HTTP, then check the three UI pages and select stop 017.
The installed app package does not need replacement for this fixture refresh.

The excerpt ends with Home and onPause; it contains no onDestroy or teardown
resource checkpoint. Pause/navigation stability is observed; full teardown
and 17-stop paging remain pending. The test guide now gives explicit server
verification and final cleanup steps.

### Stage C stop 17 and uninstall watchdog follow-up

After the Mac fixture restart, the supplied board log receives the expected
1,665-byte 17-stop response for 68X (HTTP request IDs 37 and 39). Mac requests
then select KMB-68X-017 in inbound and outbound directions. This demonstrates
reachability beyond the first two eight-row pages in both directions; the
fixture uses synthetic stop sequences, so this is navigation coverage rather
than provider direction accuracy. ETA requests finish with HTTP 200.

Manage Apps invokes onDestroy at uptime 2,369,122 ms. Lua teardown reports
153/512 KiB used, peak 247 KiB, tracked=0 and clean=1. The app resource ledger
cleanup passes for this run. It does not establish ten-cycle stability.

A separate firmware problem follows: the LVGL task watchdog fires at
2,373,847 ms inside flash access, before uninstall reports success at
2,373,996 ms. Package file deletion was recursive and synchronous inside the
Manage Apps event callback, blocking the UI for roughly five seconds. This
prevents declaring the full uninstall workflow passed despite clean app
resources. Startup latency also remains open from the previous log.

The package store now renames a verified-clean app into hidden .trash,
clears private state and registry metadata, then reclaims files in bounded
steps on a 20 ms management timer. No app Lua or UI callbacks run from the
cleanup cursor. New installs receive an explicit retry message while deletion
is pending, preserving storage admission. Interrupted trash is reclaimed at
boot and cannot be rediscovered as an installed app. Existing failure/leak
checks remain in place. Do not widen or disable watchdog limits.

Validation: production platform integration under ASan/UBSan passes,
including a 117-file/16-KiB-per-file deletion, bounded cleanup progression,
install admission during cleanup, registry-write failure rollback and
interrupted-uninstall recovery. Existing ten-cycle and simulated-reboot
acceptance harnesses pass. Signed ESP32-P4 firmware builds successfully.
The new image is 5050368 bytes, SHA-256
07ef3ea76f3be930fad88b0db6047032e0647a6880e583690be15ce72ae1587b.
It has not been flashed or tested on the board in this run. Application-flash
is required for this package-store change; Bus Lite remains 0.2.5. Retest
uninstall with the same catalog-sized package and capture clean=1, UI
responsiveness and App storage cleanup complete without a task_wdt warning.

### Deferred uninstall P4 retest — pass

The user's subsequent board excerpt confirms the fix: at uptime 339,100 ms,
Bus Lite teardown reports 156/512 KiB current payload, 248 KiB peak, tracked=0
and clean=1. Largest internal block is 167,936 bytes and internal free heap
242,067 bytes. Manage Apps reports successful uninstall at 339,126 ms, refreshes
to five catalog apps at 339,141 ms, and deferred storage cleanup completes at
347,087 ms. Physical reclamation takes about 7.96 seconds after logical
uninstall, spread across timer steps. No watchdog warning appears in the
supplied excerpt. The user confirms the completion log is present.

Close the specific uninstall watchdog regression and single-run app resource
cleanup check. This does not establish ten-cycle device stability or confirm
the flashed firmware hash, which is not included in the excerpt. Combined
with prior 68X-017 inbound/outbound evidence, long-list reachability and
single-run uninstall now pass. Startup latency, explicit missing-metadata
retry/joint identity checks and cancellation/late-work board coverage remain
tracked separately. No further code change is required by this retest.

## 2. Implementation stages

### Stage A — Establish the P4 baseline

Confirm the firmware contains the Lua memory-failure fixes and record firmware hash, package version and board configuration.

Add development-only measurements at launch, catalog loading, stop loading, ETA completion and teardown:

- Lua arena size, current usage, peak usage and largest free arena block.
- Internal heap and largest free block.
- PSRAM and native HTTP/JSON/UI allocation costs.
- Callback count, tracked resources and app-storage usage.

Run the existing Bus flow before adding catalogs. Keep diagnostics at test checkpoints rather than recurring idle logs.

**Completion:** A known firmware/package baseline works through search, stops, arrivals and navigation; resource readings are captured.

### Stage B — Prepare complete catalog access

Generate catalog assets on the computer and include them in the signed package.

- Use recorded official route lists as the route source, with source timestamps and hashes.
- Use available stop snapshots for metadata, recording gaps rather than silently excluding routes.
- Normalize and merge route records before packaging.
- Split route and stop data into files no larger than **32 KiB**, including their small lookup indexes.
- Enforce the existing **128-file and 5.5 MiB package limits** during generation. Reject oversized output instead of dropping records.
- Load only relevant catalog pages on P4. Retain compact results and discard decoded source pages.
- Fetch the selected route’s current stop sequence directly from its provider; packaged stop metadata supplies names and coordinates.

**Completion:** The supplied full route catalogs are searchable without loading all route and stop records into Lua.

Current Stage C host implementation:

- `examples/bus3p/main.lua` loads the manifest and descriptor indexes, then reads one JSON Lines page per 100 ms tick.
- Search results are prefix matched, naturally sorted, deduplicated by provider-qualified identity, and shown through eight reusable rows with Previous/Next.
- Selected stop sequences remain complete; missing names are resolved through a second bounded metadata job and shown with a retry state while unresolved.
- Joint records retain both constituent provider identities and display `KMB/CTB`; live joint requests remain Stage D.

The board acceptance pass must still exercise this catalog path with the signed package and record the P4 checkpoint measurements.

Current Stage B host checks:

```sh
python3 tools/test_phase17_bus_catalog.py
python3 tools/test_phase17_bus_package.py
python3 tools/test_phase17_bus_render.py
```

To regenerate the pinned package assets:

```sh
python3 tools/build_phase17_bus_catalog.py \
  --kmb-routes examples/bus_app/artifacts/bus_catalog/sources/kmb-routes-20261118.json.gz \
  --ctb-routes examples/bus_app/artifacts/bus_catalog/sources/ctb-routes-20261118.json.gz \
  --kmb-stops examples/bus_app/artifacts/bus_catalog/sources/kmb-stop-20261002.json.gz \
  --ctb-stops examples/bus_app/artifacts/bus_catalog/sources/hkbus-routeFareList-20261002.json.gz \
  --output examples/bus3p/assets/catalog \
  --generated-at 2026-11-18T03:59:23+08:00 \
  --source-revision official-routes-20261118-stops-20261002 \
  --kmb-route-label https://data.etabus.gov.hk/v1/transport/kmb/route/ \
  --ctb-route-label https://rt.data.gov.hk/v2/transport/citybus/route/CTB \
  --kmb-route-timestamp 2026-11-18T03:59:23+08:00 \
  --ctb-route-timestamp 2026-11-18T03:59:23+08:00 \
  --kmb-stop-timestamp 2026-10-02T22:37:17+08:00 \
  --ctb-stop-timestamp 2026-10-02T01:18:18Z
```

The generated assets are packaged with Bus Lite. The Lua catalog reader and
Search integration are implemented for Stage C; the fixture HTTP path remains
available for repeatable Stage A and provider-response tests. Joint live-provider
ETA merging remains Stage D.

### Stage C — Make the pages functional

Connect Search → route/direction → stops → ETA.

- Support uppercase alphanumeric input up to six characters and a 200 ms debounce.
- Use prefix matching and natural route-number sorting.
- Show joint routes with both provider identities preserved.
- Use eight reusable rows per result/stop page, with Previous/Next controls. Page size must not limit total coverage.
- Retain complete ordered stop sequences, including repeated stop IDs at different positions.
- Show missing metadata as an identifiable row with a retry state.
- Keep the existing separate ETA page and current navigation style.

**Completion:** KMB/LWB, CTB and joint routes navigate correctly, including routes with more than twelve stops.

### Stage D — Connect live ETA and saved stops

- Query the operators directly using one active HTTP request per app.
- For joint routes, fetch each operator sequentially and merge arrivals.
- Keep provider labels on arrival rows and retain successful results when another source fails.
- Refresh visible arrivals on entry/resume, manually, and approximately every 20 seconds.
- Preserve eight favorites and five recent routes, including joint-provider context and stop occurrence.
- Cancel or invalidate work on navigation, pause and destruction.

**Completion:** Live arrivals, joint results, save/reopen/remove, and refresh operate through the installed package without the Mac server.

### Stage E — Measure, stabilize and release

Run the complete workflow on P4, compare it with Stage A, and adjust the responsible allocations, paging or scheduling when limits appear.

Release the functional package as **0.3.0** after host and board acceptance. Record accuracy issues for the next refinement phase instead of treating them as reasons to abandon the functional transformation.

## 3. Code guides

### A. Code organization and runtime contracts

Keep the package entry point in `examples/bus3p/main.lua`. Organize it into local tables with clear responsibilities:

| Local component | Responsibility |
|---|---|
| `Catalog` | Read asset indexes, search routes and resolve stop metadata |
| `Providers` | Build provider URLs and normalize response shapes |
| `Arrivals` | Joint-stop matching, timestamp conversion and arrival ordering |
| `Store` | Validate and persist favorites/recents |
| `Controller` | Page state, selection, request queue and scheduling |
| `View` | Create controls once and update their displayed contents |

Use local tables and functions within the entry file. The sandbox removes `require`, `load`, `dofile`, `io` and `os`; splitting runtime logic into imported Lua files would require additional platform work. Enforce the existing **64 KiB entry-source limit** in package tests.

Use these implemented APIs:

| API | Contract to use |
|---|---|
| `crystal.fs.read("assets/…")` | Returns file contents or `nil`; current whole-file limit is 64 KiB |
| `crystal.json.decode(text)` | Returns a Lua value or `nil` for invalid input; decoded memory must fit the arena |
| `crystal.service.http.fetch(url, opts, callback)` | Returns request ID or `nil, error`; callback receives `(id, status, body)` |
| `crystal.service.http.cancel(id)` | Cancels the request; application generation checks still guard stale results |
| `crystal.service.time.now()` | Unix time in **seconds** |
| `crystal.ui.timer(period_ms, callback)` | Returns a timer handle; consumes a callback slot |
| `crystal.state.set(key, value)` | Returns whether persistence succeeded |
| `crystal.state.flush()` | Currently returns success without an additional write; `set()` is the meaningful persistence operation |

The initial milestone requires no new public Bus-specific firmware API. Diagnostics and fixes to existing runtime behavior remain separate from app logic.

### B. Data records and identity

Normalize provider data at the boundary. Keep provider IDs as strings, including leading zeroes.

Illustrative internal records:

```lua
-- One operator's route identity.
local part = {
  operator = "KMB",
  route = "1A",
  bound = "O",
  service_type = "1"
}

-- Search result, including constituent routes when joint.
local route = {
  key = "...",                 -- Deterministic key from constituent identities
  route = "1A",
  operator = "KMB",             -- KMB, LWB, CTB or JOINT
  destination_en = "...",
  destination_tc = "...",
  parts = {part}               -- Two provider records for a joint route
}

-- One occurrence in the displayed route's stop sequence.
local stop = {
  operator = "KMB",
  id = "...",
  seq = 1,
  name_en = "...",
  name_tc = "...",
  lat = nil,
  lon = nil
}

-- Normalized arrival.
local arrival = {
  operator = "KMB",
  stop_id = "...",
  epoch_sec = 0,
  remark_en = "",
  remark_tc = "",
  source_order = 1
}
```

Identity rules:

- Provider route identity includes operator, route number, direction and service type.
- Joint routes retain both constituent identities; never replace them with one shared direction.
- Stop occurrence identity includes provider, stop ID and sequence.
- Favorites store sufficient route and stop context to reopen independently of the currently loaded catalog page.
- Store arrival timestamps only as transient display state; do not persist a minute countdown as fresh information.

### C. Catalog generation and lookup

The host generator performs expensive normalization, sorting and joint-route candidate matching.

Use Flutter’s `RouteMerger` as the initial reference:

- Collapse redundant KMB/LWB search variants while preserving the selected variant’s actual service type.
- Pair compatible KMB/CTB destinations for the same route number.
- Keep unrelated same-number routes separate.
- Preserve source order where the reference uses it to resolve ties.

Write sorted route-page descriptors containing first/last search keys and file names. Stop-page descriptors use operator-qualified stop-ID ranges. Split index files as needed to keep every file within the 32 KiB target.

On P4:

1. Find matching descriptors.
2. Read and decode one page.
3. Copy only needed records into the bounded result buffer.
4. Release the source page reference.
5. Continue on a later scheduler tick if more work is needed.

Paging exposes every result. Searches carry a generation number so a changed query abandons unfinished work.

Large JSON decoding cannot be bounded merely by truncating its result afterward. Measure full decode cost and lower page size if needed to stay within the callback budget and Lua quota.

### D. Provider adapters and ETA processing

Keep provider handling in small functions:

```lua
Providers.route_stops_url(part)
Providers.parse_route_stops(part, decoded)
Providers.eta_url(part, stop)
Providers.parse_eta(part, stop, decoded)

Arrivals.match_joint_stop(primary_stop, secondary_stops)
Arrivals.parse_timestamp(value)
Arrivals.select(entries, now_sec)
```

Use the endpoint shapes already used by Flutter:

| Operation | KMB/LWB path | CTB path |
|---|---|---|
| Route stops | `/v1/transport/kmb/route-stop/{route}/{direction}/{service}` | `/v2/transport/citybus/route-stop/CTB/{route}/{direction}` |
| Stop details | `/v1/transport/kmb/stop/{id}` | `/v2/transport/citybus/stop/{id}` |
| ETA | `/v1/transport/kmb/eta/{stop}/{route}/{service}` | `/v2/transport/citybus/eta/CTB/{stop}/{route}` |

Allowlist `data.etabus.gov.hk` and `rt.data.gov.hk`. Percent-encode URL path components rather than stripping characters.

Initial joint-route behavior:

- Use KMB’s stop sequence when available; retain provider identity if CTB supplies a fallback sequence.
- Match a CTB stop by nearest valid coordinates in the paired CTB direction, using Flutter’s matching behavior as the starting point.
- If no match is available, show the usable provider’s arrivals with a partial-data indication.
- Keep valid timestamp rows even when optional direction/sequence metadata disagrees. Detailed filtering belongs to the accuracy pass.
- Sort by timestamp with stable source ordering, then show three arrivals.

Implement timestamp parsing with calendar arithmetic and explicit timezone offsets; Lua has no `os.time`. Keep calculations in epoch seconds to avoid overflowing 32-bit integers with epoch milliseconds. Initially use Flutter’s rounded-minute display and its 30-second “Arriving” threshold.

### E. Requests, navigation and scheduling

All HTTP work goes through one controller queue. Foreground selection and manual refresh take priority over favorites refresh.

Use generation invalidation before cancellation:

```lua
local generation, active_id = 0, nil

local function invalidate_requests()
  generation = generation + 1
  Controller.clear_queue()

  local old_id = active_id
  active_id = nil
  if old_id then
    crystal.service.http.cancel(old_id)
  end
end

local function start_request(url, complete)
  if active_id then return nil, "busy" end
  local started_generation = generation

  local id, err = crystal.service.http.fetch(
    url, {timeout_sec = 15},
    function(request_id, status, body)
      if generation ~= started_generation
          or active_id ~= request_id then
        return
      end

      active_id = nil
      complete(status, body)
      -- Queue continuation also checks the generation.
    end
  )

  if id then active_id = id end
  return id, err
end
```

Each queued job captures its route/stop identity. Do not construct a response handler from mutable global selection state.

Create one reusable **100 ms UI timer**:

- Two ticks provide the search debounce.
- Approximately ten ticks trigger displayed-time recalculation.
- Approximately 200 ticks make visible ETA work eligible for refresh.
- Actual arrival minutes always use `service.time.now()`.
- A slow request delays the next pass; missed ticks never create a backlog.

Reuse the timer across pause/resume. `onCreate()` must initialize the first visible flow because first launch does not depend on `onResume()`. On pause, invalidate requests and pending jobs; on resume, refresh the visible selection.

### F. UI reuse and persistence

Create controls and callbacks once. Row callbacks use their fixed slot to retrieve the current displayed record:

```lua
for i = 1, 8 do
  local slot = i
  View.rows[slot] = crystal.ui.button(parent, {
    text = "",
    on_click = function()
      local record = Controller.visible_rows[slot]
      if record then Controller.select(record) end
    end
  })
end
```

Reuse a shared Previous/Next control pair for the active list and avoid adding redundant navigation actions. Assert total registered callbacks remain at or below 64. Do not repeatedly create `ui.after()` callbacks for typing or refresh.

For persistence:

- Build a candidate favorites/recents list.
- Call `state.set()` and publish the candidate only after success.
- Keep favorites and recents compact and within state serialization limits; never save catalog tables.
- Show failed saves, malformed stored data and missing metadata explicitly.
- Verify removing the last favorite survives reopening.

### G. Resource sizing

Start profiling with the current quota, then increase only when the measured workload requires it. Select the smallest 64 KiB quota increment that passes the representative P4 workload with at least 25% measured arena headroom. This is a proposed sizing rule, not an existing capacity measurement.

Check fragmentation and native allocation peaks separately. Passing the Lua headroom target does not establish system-wide safety.

Exercise JSON conversion under forced allocation failure. Native cJSON trees and HTTP buffers must be released even when Lua raises an allocation error.

## 4. Test and acceptance guide

Extend the existing package and production-binding harnesses rather than creating a second test framework.

- **Catalog tests:** deterministic generation, file/package limits, prefix paging, natural ordering, complete source-record accounting, missing metadata and operator-qualified IDs.
- **Logic tests:** joint candidates, separate same-number routes, long stop lists, repeated stop occurrences, both providers’ response shapes, timestamp offsets, stable ETA ordering and partial failures.
- **Controller tests:** rapid typing, navigation during requests, manual refresh during a favorites pass, cancellation, pause/resume and late callbacks.
- **Persistence tests:** save/reload/remove, joint metadata, failed writes and clearing the final favorite.
- **Runtime tests:** production JSON conversion, callback-count stability, repeated page reuse, recoverable memory pressure, terminal OOM containment and teardown.
- **P4 tests:** KMB, CTB and joint flows; both available directions; a long route; live arrivals; reboot persistence; network failure/recovery; ten install/use/leave/uninstall cycles.

Replace current tests that require fixture truncation or a package below 20 KiB with behavioral paging checks and the actual package limits.

The functional milestone passes when the pages operate with real provider requests, every supplied route and selected route’s stop sequence is reachable, and repeated use causes no crash, quota failure or progressive resource loss. Accuracy findings remain recorded for refinement.

## 5. Delivery and follow-up

Deliver a signed **0.3.0** package through the P4 web portal after installing the required firmware. Record firmware hash, package version, catalog provenance and measured resource results together.

Use bundled catalogs for this milestone. Device-side catalog updates, detailed joint-stop accuracy, provider exceptions and design tuning follow after the functional workload is established.

Document the current uninstall-before-replacement requirement and its effect on saved data. GPS/nearest-stop features, mobile widgets, additional themes and multilingual presentation remain later work.

