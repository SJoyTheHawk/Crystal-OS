
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

