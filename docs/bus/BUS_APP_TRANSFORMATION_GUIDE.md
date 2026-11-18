# Bus App Transformation Guide — S3 Native to P4 Lua

> Current implementation (0.2.24, 2026-11-18): local work finishes without
> record/file/row timer pacing, including preload, route publication, response
> decoding and stop metadata lookup. HTTP stays sequential and asynchronous;
> typing retains its 200 ms debounce. Lua uses 4 MiB and the Developer Tools
> 5/30-second deadline. Phase 3 validates saved data and preserves in-memory
> state when a persistence write fails. Detail pages use the same side padding as
> Favorites and Search; the stop list fills the viewport to its bottom status line.
> Earlier per-tick/512 KiB/50 ms passages describe the
> previous implementation. BUS_APP_LUA_CODE_PATTERNS.md and
> BUS_APP_QUICK_START.md describe the current contract and board retest.


## Executive Summary

This guide transforms the **partial S3 native bus app** into a **functional Lua-based P4 app** that replicates—not reinvents—the proven behavior. The S3 app provides a reference for the workflow: route search with catalog, HTTP requests to KMB/CTB APIs, stop lists, ETA display, and NVS favorites. The Flutter mobile app provides the reference logic for joint-route merging, stop matching, and ETA processing that handles real-world edge cases.

**Goal**: Replicate the S3 app's complete functionality on P4 using Lua, with Flutter's proven data logic, adapted for MCU constraints.

**Not goal**: Invent new UX patterns, simplify features, or create a "proof of concept" that lacks the original capabilities.

---

## Phase 1 implementation update — 2026-11-16

Bus Lite **0.2.8** now implements direct KMB/LWB and Citybus stop sequences,
bundled metadata lookup with provider fallback, exact timestamp parsing and
single-provider ETA. The accepted P4 layout, favorites and recent-route UI are
retained. Host package/catalog/production UI tests and the P4 build pass;
physical-board acceptance was pending at this checkpoint. The
[Phase 1 results](../archive/bus/BUS_APP_PHASE_1_RESULTS.md) preserve its original
hashes. Use the [installation guide](BUS_APP_QUICK_START.md) and
[current Phase 4 results](BUS_APP_PHASE_4_RESULTS.md) for ongoing work.

## Phase 2 implementation update — 2026-11-16

Bus Lite **0.2.9** adds the Flutter-compatible joint-route ETA path. A JOINT
catalog selection keeps the KMB stop sequence as the primary list, fetches the
CTB route sequence, resolves coordinates from the bundled catalog or provider
detail endpoint, chooses the nearest valid CTB stop, and then fetches CTB ETA
sequentially. Arrivals retain their provider labels and are merged by timestamp
with stable source order. If one source or the stop match fails, successful
arrivals remain visible with an explicit partial-data status. Host joint-flow
coverage passes; P4 joint-route acceptance remains pending.

## Phase 3 implementation update — 2026-11-18

Bus Lite **0.2.18** introduced validation of favorite and recent-route records when loading
state, preserves joint-provider route identity and stop occurrence, and shows
when malformed entries are skipped. Add, remove and recent-route operations
build candidate lists and publish them in memory only after `state.set()`
succeeds. Failed writes leave the existing list intact and report an error.
P4 board acceptance passed on 2026-11-18 for favorite save/reopen across app
relaunch and reboot, persistent removal across relaunch and reboot, recent-route
ordering/deduplication and five-route limit, joint-route favorite reopening,
and the eight-favorite limit. Simulated failed-write handling is covered by host
tests; an injected storage failure was not part of the board checklist. The
accepted UI remains unchanged. The current package carrying this work is
**0.2.24**. Phase 3 is closed and Phase 4 scheduled refresh/countdown is
implemented; the detailed Phase 3 closure record is in
[BUS_APP_PHASE_3_RESULTS.md](../archive/bus/BUS_APP_PHASE_3_RESULTS.md).

The original status/estimates below describe the planning baseline. S3 was
resource limited and incomplete; its reports do not establish P4 capacity.
Phase 1 uses the existing 512 KiB quota pending board measurements. The source
uses bounded per-record processing rather than the bulk-decode sketches below.
Both stop endpoints require `outbound`/`inbound`, not `O`/`I` in the URL.

## 1. Reference Analysis

### S3 Native Implementation (C++)
**Location**: `/Users/szemy/Workspace/ESP32 Crystal OS/reference/Crystal-OS-bus-app-development/components/bus_app/`

**What it does**:
- Loads KMB/CTB route catalogs (1000+ routes) from embedded data
- Alphanumeric keypad input with real-time route filtering
- Fetches stop sequences from `data.etabus.gov.hk` and `rt.data.gov.hk`
- Displays stop lists (up to 128 stops rendered, actually supports full sequences)
- Fetches and displays ETA with 30-second refresh
- Saves up to 8 favorites in NVS with stop metadata
- Handles network state changes and graceful degradation

**Key patterns**:
```cpp
// Route identity (bus_routes.h)
struct bus_route_variant_t {
    const char *route;
    bus_operator_t op;        // KMB, LWB, CTB, JOINT
    char bound;               // 'O' or 'I'
    uint8_t service_type;
    const char *dest_en;
    const char *orig_en;
};

// HTTP request pattern
stop_request_id_ = bus_service_request_stops(
    selected_route_.route,
    selected_route_.op,
    selected_route_.bound,
    selected_route_.service_type
);

// ETA refresh on 30s timer
void BusApp::refreshFavoriteETAs() {
    for (uint8_t i = 0; i < favorites_count_; i++) {
        bus_service_request_eta(
            favorites_[i].identity.route,
            favorites_[i].identity.op,
            favorites_[i].stop_id,
            favorites_[i].identity.bound,
            favorites_[i].identity.service_type
        );
    }
}
```

### Flutter Mobile Reference (Dart)
**Location**: `/Users/szemy/Workspace/Gorbusapp/mobile/nextbus_flutter/lib/`

**What it provides**:
- **RouteMerger**: Collapses KMB/LWB variants, pairs compatible KMB+CTB routes as JOINT
- **StopMatcher**: Matches CTB stops to KMB stops by coordinates for joint routes
- **TransitEta**: Sequential KMB+CTB ETA fetching, merging, timestamp-based sorting
- **Provider adapters**: Correct endpoint shapes, response parsing, error handling

**Critical logic to replicate**:
```dart
// Joint route creation
Route(
  operator: 'JOINT',
  operators: [route.operator, 'CTB'],
  jointParts: {'kmb': route, 'ctb': ctb[i]},
  // Preserve KMB's bound/serviceType for primary requests
  bound: route.bound,
  serviceType: route.serviceType
)

// Stop matching for joint ETA
final match = StopMatcher.match(
  kmbStop: Stop(stopId: request.stopId, lat: request.lat, ...),
  ctbStops: stops
);

// ETA ordering: timestamp first, source order breaks ties
entries.indexed.toList()..sort((a, b) {
  final time = a.$2.eta!.compareTo(b.$2.eta!);
  return time == 0 ? a.$1.compareTo(b.$1) : time;
});
```

---

## 2. Planning baseline before Phase 1

**Location**: `/Users/szemy/Workspace/ESP32 Crystal OS/examples/bus3p/`

**What works**:
- Catalog-backed search with prefix matching and paging
- Direct KMB/LWB and CTB provider requests with metadata fallback
- Joint-route nearest-stop matching and sequential merged ETA in Bus Lite 0.2.9
- Generation-based request invalidation
- Resource measurement checkpoints
- 512 KiB Lua arena with bounded JSON decoding

**Gaps identified at planning time** (the following list is historical; Phase 1
and Phase 2 updates above supersede the provider and joint-route entries):
1. **No live provider HTTP requests** — still uses fixture server at `http://phase17.bus.fixture:8000`
2. **No joint route ETA** — resolved in Bus Lite 0.2.9; P4 acceptance remains
   pending
3. **Incomplete stop metadata resolution** — async catalog lookup but no fallback to provider API
4. **Persistence hardening** — resolved in 0.2.18; user-reported P4 acceptance passed 2026-11-18
5. **No ETA refresh** — one-shot requests only, no 20s periodic refresh
6. **Incomplete Flutter logic at planning time** — route merging was in the
   Python catalog generator; stop matching and ETA merging are implemented in
   Bus Lite 0.2.9, with P4 acceptance pending.

**Root cause at planning time**: Phase 17 focused on platform validation (Lua
runtime, package format, catalog feasibility), not feature parity. Phase 1 and
Phase 2 now add direct providers and joint-route behavior; board acceptance is
still pending.

---

## 3. Transformation Strategy

### Phase 1: Direct Provider Integration (Stage D equivalent)

**Objective**: Replace fixture server with real KMB/CTB APIs

**Changes**:
1. Add `Providers` module with URL builders and response parsers
   ```lua
   Providers.route_stops_url(part)  -- Returns "https://data.etabus.gov.hk/v1/..."
   Providers.parse_route_stops(part, decoded)  -- Normalizes {data:[{seq,stop}]}
   Providers.eta_url(part, stop)
   Providers.parse_eta(part, stop, decoded)  -- Normalizes {data:[{eta,rmk_tc}]}
   ```

2. Update `BASE` from fixture to provider domains
   ```lua
   -- Remove: local BASE = "http://phase17.bus.fixture:8000"
   -- Requests now use Providers.route_stops_url(selected_part)
   ```

3. Handle both KMB and CTB response shapes
   - KMB: `/v1/transport/kmb/route-stop/{route}/{bound}/{service}`
   - CTB: `/v2/transport/citybus/route-stop/CTB/{route}/{bound}`
   - Both return `{type:"RouteStop", version:"1.0", data:[...]}`

**Acceptance**: Route 1A (KMB), route 2 (CTB) both fetch real stop sequences and display names from bundled metadata, with provider detail fallback for gaps.

### Phase 2: Joint Route Implementation

**Status (2026-11-16)**: Implemented in Bus Lite 0.2.9; P4 acceptance pending.
See [BUS_APP_PHASE_2_RESULTS.md](../archive/bus/BUS_APP_PHASE_2_RESULTS.md) for the host checks and package digest.

**Objective**: Implement Flutter's joint-route ETA logic

**Implemented behavior**:
1. Add `Arrivals` module
   ```lua
   Arrivals.match_joint_stop(primary_stop, secondary_stops)
     -- Distance-based matching using lat/lon
     -- Returns best match or nil
   
   Arrivals.parse_timestamp(iso8601_string)
     -- Manual parsing: "2026-11-16T14:30:00+08:00" → epoch seconds
     -- Handle +08:00 offset explicitly
   
   Arrivals.select(entries, now_sec)
     -- Sort by epoch_sec, stable source order
     -- Return first 3
   ```

2. Implement sequential joint ETA workflow
   ```lua
   if selected.operator == "JOINT" then
     local kmb_part = selected.parts[1]
     local ctb_part = selected.parts[2]
     
     -- Fetch KMB first
     fetch_eta(kmb_part, selected_stop, function(kmb_arrivals)
       -- Fetch CTB stops to find matching stop
       fetch_route_stops(ctb_part, function(ctb_stops)
         local match = Arrivals.match_joint_stop(selected_stop, ctb_stops)
         if match then
           fetch_eta(ctb_part, match, function(ctb_arrivals)
             local merged = Arrivals.select(
               {unpack(kmb_arrivals), unpack(ctb_arrivals)},
               crystal.service.time.now()
             )
             render_eta(merged)
           end)
         else
           render_eta(kmb_arrivals)  -- Partial data, show KMB only
         end
       end)
     end)
   end
   ```

3. Preserve provider labels in arrival display
   - Each arrival keeps `operator` field
   - UI shows "KMB • 3 min" or "CTB • 5 min"

**Acceptance**: Route 101 (JOINT) fetches both operators, merges by timestamp, displays provider labels. Partial results shown if one provider fails.

### Phase 3: Favorites and Persistence

**Objective**: Replicate S3's NVS-backed favorites

**Changes**:
1. Implement `Store` module
   ```lua
   Store.add_favorite(route, stop)
     -- Validates limit (8 max)
     -- Stores {route, operator, bound, service_type, stop_id, stop_name, lat, lon}
     -- Calls crystal.state.set("favorites", favorites)
   
   Store.remove_favorite(index)
   Store.load_favorites()
     -- Returns array of favorite records
   ```

2. Add Favorites tab UI
   - Card per favorite showing route number, stop name, last ETA
   - Tap to reopen stop+ETA view
   - Long-press or swipe to remove

3. Implement recent routes (5 max)
   - Deduplicated by route identity
   - Shown in Search tab above keypad

**Acceptance**: Save route 1A stop 001, exit app, relaunch — favorite persists and tapping it loads ETA.

### Phase 4: ETA Refresh and Lifecycle

**Objective**: Auto-refresh visible ETA, cancel on navigation

**Changes**:
1. Add refresh timer logic
   ```lua
   local last_refresh_time = 0
   local REFRESH_INTERVAL = 20  -- seconds
   
   function on_timer_tick()
     local now = crystal.service.time.now()
     if v.page == "eta" and now - last_refresh_time > REFRESH_INTERVAL then
       last_refresh_time = now
       fetch_eta_for_current_stop()
     end
   end
   ```

2. Update ETA display with age
   - Show "Updated just now", "Updated 2 min ago"
   - Recalculate minutes from stored epoch_sec on each tick

3. Cancel on pause/navigation
   ```lua
   function onPause()
     invalidate_requests()
     if timer then crystal.ui.timer_stop(timer) end
   end
   
   function onResume()
     if not timer then timer = crystal.ui.timer(100, on_timer_tick) end
     if v.page == "eta" then refresh_current_eta() end
   end
   ```

**Acceptance**: ETA page refreshes every 20s, minutes count down live, navigation cancels pending requests, resume re-fetches.

---

## 4. MCU-Specific Adaptations

### Memory Management

**Challenge**: S3 has ~400 KB free DRAM, P4 Lua has 512 KB arena (currently ~380 KB peak)

**Solutions**:
1. **Streaming JSON parsing**: Already implemented — one JSON Lines record per tick
2. **Discard source pages**: Keep only normalized results, release decoded catalog pages
3. **No catalog retention**: Fetch stop sequences from provider, don't cache 6K+ stop records
4. **Bounded UI**: 8 result rows, 8 stop rows per page — matches S3's rendering limit

### Network Constraints

**Challenge**: One HTTP request at a time, TLS session reuse

**Solutions**:
1. **Request queue**: Already implemented — generation-based invalidation, single active request
2. **Sequential joint ETA**: Fetch KMB, then CTB — matches S3's single-request pattern
3. **No keep-alive close**: Memory note says skip `close()` when body ended — follow that pattern

### Computation Limits

**Challenge**: No `os.time()` for date parsing, no coordinate math libraries

**Solutions**:
1. **Manual ISO8601 parsing**:
   ```lua
   function parse_iso8601(s)
     local y,mo,d,h,mi,s,ofs = s:match("^(%d%d%d%d)-(%d%d)-(%d%d)T(%d%d):(%d%d):(%d%d)([+-]%d%d):(%d%d)$")
     -- Implement day-of-year calculation, apply offset
     -- Return epoch seconds
   end
   ```

2. **Haversine distance** (for stop matching):
   ```lua
   function haversine(lat1, lon1, lat2, lon2)
     local dlat = math.rad(lat2 - lat1)
     local dlon = math.rad(lon2 - lon1)
     local a = math.sin(dlat/2)^2 + 
               math.cos(math.rad(lat1)) * math.cos(math.rad(lat2)) * 
               math.sin(dlon/2)^2
     return 6371000 * 2 * math.atan2(math.sqrt(a), math.sqrt(1-a))  -- meters
   end
   ```

3. **Simplified matching**: If coordinates missing, use first CTB stop (Flutter's fallback)

---

## 5. Code Organization

### Module Structure

```lua
-- Bus package entry: examples/bus3p/main.lua

local Catalog = {
  -- Existing: manifest, route_pages, stop_pages, search_step, stop_step
}

local Providers = {
  route_stops_url = function(part) ... end,
  parse_route_stops = function(part, decoded) ... end,
  eta_url = function(part, stop) ... end,
  parse_eta = function(part, stop, decoded) ... end,
  stop_details_url = function(operator, stop_id) ... end,
  parse_stop_details = function(operator, decoded) ... end
}

local Arrivals = {
  match_joint_stop = function(primary_stop, secondary_stops) ... end,
  parse_timestamp = function(iso8601) ... end,
  select = function(entries, now_sec) ... end,
  format_minutes = function(epoch_sec, now_sec) ... end
}

local Store = {
  load_favorites = function() ... end,
  add_favorite = function(route, stop) ... end,
  remove_favorite = function(index) ... end,
  load_recents = function() ... end,
  add_recent = function(route) ... end
}

local Controller = {
  -- Existing: generation, active_id, invalidate_requests, start_request
  -- Add: fetch_route_stops, fetch_eta, fetch_joint_eta
}

local View = {
  -- Existing: search UI, route results, stop list
  -- Add: eta page, favorites tab, refresh button
}
```

### Data Flow

```
User types "1A"
  ↓
Catalog.search_step() → route_results (from local catalog)
  ↓
User selects "1A to Chuk Yuen Estate (KMB)"
  ↓
Controller.fetch_route_stops(selected_route.parts[1])
  → Providers.route_stops_url() → "https://data.etabus.gov.hk/..."
  → HTTP callback → Providers.parse_route_stops()
  → stop_results (with seq, id, but maybe missing name/lat/lon)
  ↓
Catalog.stop_step() resolves names (if available in offline catalog)
  ↓
View renders the full route as one scrollable native stop list
  ↓
User selects stop "001"
  ↓
Controller.fetch_eta(selected_route, selected_stop)
  → If JOINT: fetch_joint_eta()
  → Otherwise: Providers.eta_url()
  → HTTP callback → Providers.parse_eta()
  → Arrivals.select()
  → View.render_eta_page()
  ↓
Timer tick (every 100ms)
  → Check if 20s elapsed since last_refresh_time
  → If yes: Controller.fetch_eta() again
  → Update displayed minutes from epoch_sec
```

---

## 6. Implementation Phases

### Phase 1: Provider Integration (3-5 days)

**Tasks**:
1. Implement `Providers.route_stops_url()` and `parse_route_stops()`
2. Update `open_route()` to call provider instead of fixture
3. Test KMB route 1A, CTB route 2
4. Implement `Providers.eta_url()` and `parse_eta()`
5. Add ETA page UI (reuse S3's card layout)
6. Test ETA display for single-operator routes

**Deliverable**: Route search → stops → ETA works for KMB and CTB routes without fixture server

### Phase 2: Joint Routes (2-3 days)

**Tasks**:
1. Implement `Arrivals.match_joint_stop()` with haversine distance
2. Implement `Arrivals.parse_timestamp()` with manual ISO8601 parsing
3. Implement `Controller.fetch_joint_eta()` with sequential KMB+CTB flow
4. Test route 101 (JOINT)
5. Add operator labels to ETA display

**Deliverable**: Joint routes display merged ETA with provider labels

### Phase 3: Persistence (implemented in 0.2.18; closed in 0.2.22)

**Implemented**:
1. Validate stored favorites and recents, including joint-route and stop identity.
2. Build candidate lists and publish changes only after `state.set()` succeeds.
3. Keep the accepted Favorites/Search and ETA save/remove UI.
4. Host-test malformed state, duplicates, limits, failed writes and save/reopen/remove.

**P4 acceptance passed (2026-11-18)**: Save, relaunch, reboot, remove, recent
route behavior, joint-route persistence and the eight-favorite limit passed the
user's board checklist. Host tests cover simulated failed writes; actual storage
failure injection remains untested on hardware.

### Phase 4: Refresh and Polish (implemented in 0.2.24; P4 acceptance pending)

**Implemented**:
1. A shared 100 ms timer recalculates visible ETA minutes about once per second.
2. The selected stop refreshes after 20 seconds when no request is active.
3. Favorites refresh sequentially every 30 seconds while visible and show up to
   three transient ETA values per saved stop.
4. Manual refresh, generation invalidation and pause/resume lifecycle behavior
   remain active.
5. Recent routes remain available in Search; they are refreshed by the existing
   persistence flow rather than duplicated in the scheduler.

**Deliverable**: Host-validated Phase 4 package 0.2.24. P4 board refresh,
network-recovery and stability acceptance is recorded in
`BUS_APP_PHASE_4_RESULTS.md`.

**Total estimate**: 8-12 days

---

## 7. Testing Strategy

### Unit-Level Validation (Host)

Extend existing Python test harnesses:

```python
# test_phase17_bus_providers.py
def test_kmb_route_stops_url():
    part = {"operator": "KMB", "route": "1A", "bound": "O", "service_type": "1"}
    url = providers_route_stops_url(part)
    assert url == "https://data.etabus.gov.hk/v1/transport/kmb/route-stop/1A/outbound/1"

def test_parse_kmb_eta_response():
    json_text = '{"data":[{"eta":"2026-11-16T14:30:00+08:00","rmk_tc":"原定班次"}]}'
    arrivals = providers_parse_eta("KMB", json_text)
    assert len(arrivals) == 1
    assert arrivals[0]["epoch_sec"] > 0

# test_phase17_bus_arrivals.py
def test_match_joint_stop():
    kmb_stop = {"id": "001", "lat": 22.3193, "lon": 114.1694}
    ctb_stops = [
        {"id": "CTB001", "lat": 22.3195, "lon": 114.1696},  # 30m away
        {"id": "CTB099", "lat": 22.3500, "lon": 114.2000}   # 3km away
    ]
    match = arrivals_match_joint_stop(kmb_stop, ctb_stops)
    assert match["id"] == "CTB001"

def test_eta_timestamp_sorting():
    entries = [
        {"operator": "CTB", "epoch_sec": 1700000200},
        {"operator": "KMB", "epoch_sec": 1700000100},
        {"operator": "KMB", "epoch_sec": 1700000150}
    ]
    sorted_entries = arrivals_select(entries, 1700000000)
    assert sorted_entries[0]["epoch_sec"] == 1700000100  # earliest first
    assert len(sorted_entries) == 3
```

### Device Integration Tests

```lua
-- Run on P4 via tools/test_phase17_bus_ui.lua

-- Test 1: KMB route stops
select_route("1A", "KMB")
wait_for_stops()
assert(#stop_results > 0, "KMB route returned no stops")
assert(stop_results[1].name_en ~= nil, "Stop name missing")

-- Test 2: CTB route stops
select_route("2", "CTB")
wait_for_stops()
assert(#stop_results > 0, "CTB route returned no stops")

-- Test 3: Joint route ETA
select_route("101", "JOINT")
wait_for_stops()
select_stop(1)
wait_for_eta()
assert(#arrivals > 0, "Joint route returned no ETA")
local has_kmb = false
local has_ctb = false
for _, arr in ipairs(arrivals) do
  if arr.operator == "KMB" then has_kmb = true end
  if arr.operator == "CTB" then has_ctb = true end
end
assert(has_kmb or has_ctb, "Joint ETA missing provider labels")

-- Test 4: Favorites persistence
select_route("1A", "KMB")
wait_for_stops()
select_stop(1)
wait_for_eta()
save_favorite()
exit_app()
launch_app()
assert(#favorites > 0, "Favorite not persisted")
select_favorite(1)
wait_for_eta()
assert(#arrivals > 0, "Reopened favorite returned no ETA")
```

### Acceptance Criteria

1. **Feature parity**: Every S3 app feature works on P4 Lua
2. **Real data**: No fixture server, all requests go to `data.etabus.gov.hk` / `rt.data.gov.hk`
3. **Joint routes**: 101, 101R, 102, etc. display merged KMB+CTB ETA
4. **Persistence**: Favorites survive app restart
5. **Refresh**: ETA auto-updates every 20s, minutes count down
6. **Stability**: 10 install/use/uninstall cycles without crash or leak
7. **Resource**: Peak Lua usage stays under 400 KB (25% headroom in 512 KB arena)

---

## 8. Risk Mitigation

### Risk 1: Lua Arena Overflow

**Symptom**: Catalog + HTTP responses + UI exceed 512 KB

**Mitigation**:
- Historical host measurements are not P4 evidence; Stage C P4 reported 248 KiB peak. Measure Phase 1 on P4 before sizing the quota.
- ETA responses are small (~2 KB per provider)
- Joint ETA needs sequential fetching (not parallel) to avoid double-buffering
- If still tight: increase to 640 KB (Flutter logic adds ~50 KB, ETA buffers ~30 KB)

### Risk 2: ISO8601 Parsing Complexity

**Symptom**: Manual date arithmetic has off-by-one errors

**Mitigation**:
- Use Flutter's proven timestamp examples as test cases
- Validate against known ETA: "2026-11-16T14:30:00+08:00" → specific epoch
- Python test harness can generate Lua timestamp parser from reference impl

### Risk 3: Stop Matching Accuracy

**Symptom**: Joint routes show wrong CTB stop

**Mitigation**:
- Flutter's StopMatcher is the reference for real HK transit data.
- Select the nearest CTB stop with valid coordinates; Flutter has no distance
  cutoff. Keep the first route stop as fallback when the primary has no valid
  coordinates.
- Log matched pairs during development and validate them against Flutter.
- If no valid CTB match exists, keep usable KMB arrivals visible.

### Risk 4: Network Reliability

**Symptom**: Providers return 503, timeout, or malformed JSON

**Mitigation**:
- Implement retry with exponential backoff (already in Flutter)
- Show user-friendly error: "Unable to fetch ETA, tap Refresh to retry"
- Cache last successful ETA in favorites (already in S3 app)
- Never crash on bad JSON — `json.decode()` returns nil, handle it

---

## 9. Success Metrics

### Functional Completeness

- [ ] All S3 features implemented: search, stops, ETA, favorites, refresh
- [ ] Joint routes (KMB+CTB) work correctly
- [ ] Real provider APIs, no fixture dependency
- [ ] Favorites persist across restarts
- [ ] ETA auto-refreshes every 20s

### Resource Efficiency

- [ ] Peak Lua usage < 400 KB (documented in checkpoint logs)
- [ ] No memory leaks in 10-cycle test
- [ ] Startup time < 1 second (current: 746ms, target: <500ms with provider URLs)
- [ ] ETA fetch < 3 seconds per request

### Code Quality

- [ ] All modules documented with contracts
- [ ] Python test harness covers Providers and Arrivals logic
- [ ] Lua integration tests cover complete workflows
- [ ] No magic numbers — refresh interval and favorite limit are named constants

---

## 10. Post-Transformation Roadmap

### Phase 5: Accuracy Refinement (post-0.3.0)

- Validate joint-stop matching against Flutter's test cases
- Handle provider exceptions (route not found, stop not served)
- Implement remark filtering (Flutter's ETA remarks module)
- Add service type disambiguation for ambiguous KMB routes

### Phase 6: UX Enhancements (post-0.4.0)

- GPS integration for nearest stop
- Route map view (if framebuffer allows)
- Notification for favorite ETA changes
- Dark/light theme (already in color constants)

### Phase 7: Data Updates (post-0.5.0)

- OTA catalog updates (download new route snapshots)
- Provider API versioning (handle schema changes)
- Localization (TC/EN/SC — Flutter already has i18n)

---

## Conclusion

This transformation is feasible and well-scoped. The S3 app proves the UX works, Flutter proves the data logic works, and P4 Lua runtime proves the platform works. The current gap is **integration**, not invention.

**Critical success factor**: Follow the references faithfully. When Flutter does X, do X. When S3 does Y, do Y. The temptation to "simplify" or "improve" is the root cause of "inventing something different."

**Estimated effort**: 8-12 development days for a developer familiar with the codebase.

**First milestone**: Phase 1 complete (provider integration) — validates the technical path without requiring all features.

This is achievable. The bus app will work on P4.
