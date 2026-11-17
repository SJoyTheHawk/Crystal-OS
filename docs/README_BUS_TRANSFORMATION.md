# Bus App Transformation — Complete Documentation Index

> Current implementation (0.2.18, 2026-11-17): local work finishes without
> record/file/row timer pacing, including preload, route publication, response
> decoding and stop metadata lookup. HTTP stays sequential and asynchronous;
> typing retains its 200 ms debounce. Lua uses 4 MiB and the Developer Tools
> 5/30-second deadline. Phase 3 validates persisted records and commits
> candidate state only after successful writes. Earlier per-tick/512 KiB/50 ms passages describe the
> previous implementation. BUS_APP_LUA_CODE_PATTERNS.md and
> BUS_APP_QUICK_START.md describe the current contract and board retest.


## Overview

This directory contains the complete plan for transforming the **S3 native bus app** into a **working P4 Lua implementation** that replicates—not reinvents—the proven functionality.

**Status**: Phases 1–3 are implemented in Bus Lite 0.2.18; host checks pass and
the P4 build remains valid. Board acceptance is pending. See
`BUS_APP_QUICK_START.md` for app-flash and Control Panel installation, and
`BUS_APP_PHASE_1_RESULTS.md` for Phase 1 evidence, and
`BUS_APP_PHASE_2_RESULTS.md` for joint-route implementation evidence.

**Goal**: Feature parity with S3 app using Flutter's proven data logic

---

## Document Structure

### 1. **BUS_APP_TRANSFORMATION_GUIDE.md** (Main reference)
**Purpose**: Strategic overview and complete transformation plan

**Contains**:
- Reference analysis (S3 C++, Flutter Dart, Current P4 Lua)
- Root cause analysis ("inventing something different" diagnosis)
- 4-phase transformation strategy with acceptance criteria
- MCU-specific adaptations (memory, network, computation)
- Module architecture and data flow diagrams
- Risk mitigation strategies
- Success metrics and post-transformation roadmap

**Read this first**: Understand the "why" and "what" before the "how"

---

### 2. **BUS_APP_LUA_CODE_PATTERNS.md** (Implementation reference)
**Purpose**: Concrete Lua code patterns for all modules

**Contains**:
- Provider module: URL builders and response parsers (KMB/CTB)
- Arrivals module: Timestamp parsing, stop matching, ETA sorting
- Controller module: HTTP workflow, request queue, joint ETA fetching
- Store module: Favorites and recents persistence
- View module: ETA page rendering, refresh timer
- Lifecycle integration: onCreate/onResume/onPause/onDestroy
- Testing utilities: Mocks, resource checkpoints

**Use this during coding**: Copy-paste-adapt these patterns into `main.lua`

---

### 3. **BUS_APP_QUICK_START.md** (Phase 1 board acceptance)
**Purpose**: Install and validate the live-provider milestone

**Contains**:
- Firmware app-flash and signed CAPP installation through the Control Panel
- KMB 1A and Citybus 2 live stop/ETA checks
- Navigation, saved-stop and network recovery checks
- P4 resource capture and acceptance criteria

**Start here**: If you want immediate progress, follow this guide first

---

### 4. **APP_PLATFORM_V2_PHASE_17_3_BUS_MIGRATION_PLAN.md** (Legacy)
**Purpose**: Original acceptance test plan (Stage A-E)

**Status**: Being replaced by transformation guides above

**Note**: Keep for historical context and Stage C measurements, but follow the new guides for implementation

---

## Implementation Roadmap

### Phase 1: Provider Integration (Quick Start)
**Status**: Implemented in 0.2.8; P4 acceptance pending

**Goal**: Real KMB/CTB API calls, stop lists, basic ETA  
**Deliverable**: Route 1A and route 2 work end-to-end without fixture server

**Steps**:
1. Add Provider module (URL builders, parsers)
2. Replace fixture calls with real HTTPS requests
3. Add basic timestamp parsing
4. Build ETA display page
5. Test KMB and CTB routes

**Guide**: `BUS_APP_QUICK_START.md`

---

### Phase 2: Joint Routes
**Status**: Implemented in 0.2.9; P4 acceptance pending

**Goal**: Merged KMB+CTB ETA with provider labels  
**Deliverable**: Route 101 (JOINT) displays combined arrivals

**Steps**:
1. Implement haversine distance for stop matching
2. Add sequential joint ETA workflow (KMB → stops → CTB → merge)
3. Handle partial results (one provider fails)
4. Add operator labels to ETA display

**Guide**: `BUS_APP_LUA_CODE_PATTERNS.md` § 2.2, 3.4

---

### Phase 3: Persistence
**Status**: Implemented in Bus Lite 0.2.18; user-reported P4 acceptance passed 2026-11-18

**Goal**: Save/restore favorites across app restarts  
**Deliverable**: Favorites tab with 8 saved stops

**Steps**:
1. Validate favorite and recent-route records on load
2. Commit in-memory changes only after `state.set()` succeeds
3. P4 save/relaunch/reboot/remove, recents, joint favorite and limit checks passed
4. Host tests cover malformed state and simulated write failures

**Guide**: `BUS_APP_LUA_CODE_PATTERNS.md` § 4

---

### Phase 4: Refresh & Polish
**Status**: Scheduled refresh and live countdown remain

**Goal**: Auto-refresh ETA, live countdown, complete UX  
**Deliverable**: Full S3 feature parity

**Steps**:
1. Add 20s refresh timer to ETA page
2. Implement live minute countdown
3. Add manual Refresh button
4. Test pause/resume cancellation
5. Add recents to Search tab

**Guide**: `BUS_APP_LUA_CODE_PATTERNS.md` § 5.2, 6

---

Existing manual refresh, recents and saved-stop controls are retained. The
follow-up phases extend their behavior rather than rebuilding those pages.
The functional 0.3.0 release remains gated on host and P4 acceptance.

---

## Reference Materials

### S3 Native Implementation
**Location**: `/Users/szemy/Workspace/ESP32 Crystal OS/reference/Crystal-OS-bus-app-development/`

**Key files**:
- `components/bus_app/src/bus_app.cpp` — Main app logic
- `components/bus_service/` — HTTP service, catalog
- `components/crystal_http/` — HTTP client wrapper

**What to reference**:
- UI layout and styling (`makeCard`, `makeLabel`, colors)
- HTTP request patterns (generation-based invalidation)
- NVS persistence (favorites storage)
- Lifecycle management (onCreate/onPause/onResume/onDestroy)

---

### Flutter Mobile Reference
**Location**: `/Users/szemy/Workspace/Gorbusapp/mobile/nextbus_flutter/lib/`

**Key files**:
- `services/route_merger.dart` — Joint route logic
- `services/stop_matcher.dart` — Coordinate-based stop matching
- `services/transit_eta.dart` — Sequential ETA fetching
- `providers/eta_provider.dart` — Provider adapters

**What to reference**:
- Route merging rules (`collapseKmbVariants`, `similar` matching)
- Stop matching algorithm (haversine, matching policy (verify against the actual Flutter source before Phase 2))
- ETA sorting (timestamp first, source order breaks ties)
- Response parsing (handling both KMB and CTB shapes)

---

### Current P4 Implementation
**Location**: `/Users/szemy/Workspace/ESP32 Crystal OS/examples/bus3p/`

**Current features**:
- ✅ Catalog-backed search with prefix matching
- ✅ Bounded JSON decoding (one record per tick)
- ✅ Generation-based request invalidation
- ✅ Resource checkpoints

**Transformation status**:
- ✅ Direct KMB/LWB and CTB provider adapters (board validation pending)
- ✅ Joint route ETA merging with nearest CTB stop matching
- ✅ Favorite/recent validation and candidate writes in Bus Lite 0.2.18; P4 persistence checklist passed 2026-11-18
- ❌ ETA refresh timer
- ✅ Bundled stop metadata plus sequential provider fallback and retry states

---

## API Endpoints Reference

### KMB/LWB
**Base**: `https://data.etabus.gov.hk/v1/transport/kmb`

**Endpoints**:
- Route stops: `/route-stop/{route}/{bound}/{service}`
- Stop details: `/stop/{stop_id}`
- ETA: `/eta/{stop_id}/{route}/{service}`

**Response format**:
```json
{
  "type": "RouteStop" | "Stop" | "ETA",
  "version": "1.0",
  "data": [ /* array of records */ ]
}
```

---

### CTB (Citybus)
**Base**: `https://rt.data.gov.hk/v2/transport/citybus`

**Endpoints**:
- Route stops: `/route-stop/CTB/{route}/{bound}`
- Stop details: `/stop/{stop_id}`
- ETA: `/eta/CTB/{stop_id}/{route}`

**Response format**: Route-stop/ETA use a `data` array; stop details use a `data` object, as with KMB.

---

Directions in URLs are `outbound`/`inbound`, mapped from identity `O`/`I`. Path components are percent-encoded.

### Common Fields

**Route Stop**:
```json
{
  "stop": "001",
  "seq": 1
}
```

**Stop Details**:
```json
{
  "stop": "001",
  "name_en": "Chuk Yuen Estate",
  "name_tc": "竹園邨",
  "lat": 22.3430,
  "long": 114.1778
}
```

**ETA Entry**:
```json
{
  "eta": "2026-11-16T14:30:00+08:00",
  "rmk_en": "Scheduled",
  "rmk_tc": "原定班次",
  "dir": "O",
  "service_type": 1
}
```

---

## Testing Strategy

### Unit Tests (Host)
**Tool**: Existing production-binding host harnesses in `tools/`

**Coverage**:
- Provider URL building
- Response parsing
- Timestamp conversion
- Metadata fallback, complete paging and stop occurrences
- ETA sorting

**Run**:
```bash
python3 tools/test_phase17_bus_package.py
python3 tools/test_phase17_bus_catalog.py
python3 tools/test_phase17_bus_render.py
python3 tools/test_phase17_bindings.py
python3 tools/test_phase17_runtime.py
```

---

### Integration Tests
**Tool**: Production Lua/UI/JSON bindings with LVGL at 480/720 pixels

**Coverage**:
- KMB route flow (search → stops → ETA)
- CTB route flow
- Recorded official responses and partial-provider indication
- Existing favorite save/reopen/remove behavior
- Refresh and cancellation

**Run**:
```bash
python3 tools/test_phase17_bus_render.py
```

---

### Acceptance Tests (Manual)
**Environment**: P4 Function EV board with WiFi

**Checklist**:
1. Search "1A" → see KMB routes
2. Select KMB 1A "To STAR FERRY"
3. See stop list with names
4. Tap first stop → see ETA (e.g., "3 min", "8 min")
5. Search "2" → see CTB routes
6. Repeat stop → ETA flow
7. Repeat both directions and scroll through the complete stop list
8. Navigate away during a request and verify stale callbacks are ignored
9. Save a favorite → exit → relaunch → tap favorite → ETA loads
10. Repeat 10 install/uninstall cycles → no crash or leak

---

## Resource Requirements

### Memory
**Lua arena**: 512 KiB; change only after measuring the P4 workload

**Peak usage target**: At least 25% arena headroom, with fragmentation checked separately

**Baseline**: Stage C P4 reported 248/512 KiB peak. Host figures use different pointer/header sizes; current Phase 1 P4 measurements remain pending.

**Checkpoints**: `launch_ready`, `search_ready`, `stops_loaded`, `eta_complete`
and teardown. See `BUS_APP_PHASE_1_RESULTS.md` for measured host results. The
host real-catalog peak does not meet the 25% target; P4 measurements are pending.

---

### Network
**Concurrent requests**: 1 (queue others)  
**Timeout**: 15 seconds per request  
**TLS**: Existing ESP-IDF HTTPS client; device validation is pending

**Response sizing**: Record actual HTTP body peaks; do not assume a fixed size

---

### Computation
**JSON decoding**: Frame at most 2,048 characters and decode one provider record per tick; catalog JSON Lines are also incremental

**Timestamp parsing**: Manual (no `os.time` in sandbox)  
**Distance calculation**: Phase 2 follows Flutter's nearest-coordinate matching;
no distance cutoff is applied, and missing primary coordinates use the first
CTB stop as fallback. See `BUS_APP_PHASE_2_RESULTS.md`.

---

## Success Criteria

### Functional
- [x] Search works with full catalog (2115 routes)
- [ ] KMB routes fetch real stops and ETA
- [ ] CTB routes fetch real stops and ETA
- [x] Host checks cover joint matching, merged ETA labels and one-provider failure
- [ ] Favorites persist across restarts (up to 8)
- [ ] ETA refreshes every 20 seconds
- [x] Host checks cover continuous stop-list scrolling and final-row selection
- [ ] Network errors show user-friendly messages
- [x] Live package uses official provider hosts without the fixture server

Unchecked live-provider criteria require P4 evidence. Host implementation and
recorded-response checks pass; they do not establish device TLS or resource safety.

---

### Performance
- [ ] onCreate completes in <1 second
- [ ] ETA fetch completes in <3 seconds
- [ ] P4 Lua arena has at least 25% measured headroom
- [ ] No memory leaks in 10-cycle test
- [ ] Callback deadlines not exceeded (<50ms per tick)

---

### Code Quality
- [ ] All modules documented with contracts
- [ ] Python tests pass for Providers and Arrivals
- [ ] Lua integration tests pass on device
- [ ] Resource checkpoints at all key stages
- [ ] No magic numbers (use named constants)

---

## Troubleshooting Resources

### Common Issues

**Issue**: "No matching routes"  
**Cause**: Catalog not loaded  
**Fix**: Check `Catalog.available`, verify `assets/catalog/runtime.txt`

**Issue**: "Network error 0"  
**Cause**: WiFi not connected or TLS handshake failed  
**Fix**: Check serial log for IP address, verify cert chain

**Issue**: "Lua allocation failed"  
**Cause**: Arena exhausted  
**Fix**: Capture arena usage, peak and largest free block; fix retained allocations or paging first, then size the quota from measured P4 workload

**Issue**: "Invalid JSON"  
**Cause**: Response truncated or malformed  
**Fix**: Check response status, body size and bounded parser diagnostics; do not log full provider payloads by default

---

### Debug Tools

**Serial log**:
```bash
idf.py monitor
```

**Resource checkpoint**:
```lua
resource_checkpoint("my_stage")  -- Logs memory, HTTP, callbacks
```

**HTTP request trace**:
```lua
print("HTTP GET", url)
-- Development-only URL trace; normal HTTP logs identify requests/status.
```

**Lua memory dump**:
```lua
local info = crystal.service.system.info()
print("Lua used:", info.lua_used_kb, "/", info.lua_arena_kb, "KB")
print("Peak:", info.lua_peak_kb, "KB")
```

---

## Contributing

### Before Starting
1. Read `BUS_APP_TRANSFORMATION_GUIDE.md` (understand the strategy)
2. Review S3 `bus_app.cpp` and Flutter `route_provider.dart` (see what works)
3. Check current `bus3p/main.lua` (know what's already there)

### During Development
1. Follow `BUS_APP_QUICK_START.md` for Phase 1
2. Reference `BUS_APP_LUA_CODE_PATTERNS.md` for code snippets
3. Add resource checkpoints at each milestone
4. Test on device frequently (don't code blind)

### After Each Phase
1. Run the host commands in Testing Strategy above.
2. Follow the phase-specific manual P4 checklist in Quick Start.
3. Document peak memory usage in checkpoint logs
4. Commit with clear phase marker:
   ```
   git commit -m "Phase X: [feature]
   
   - [change 1]
   - [change 2]
   
   Acceptance: [test results]
   Peak Lua: [X] KB / 512 KB
   
   Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
   ```

---

## Next Steps

### Immediate (P4 acceptance)
1. Open `BUS_APP_QUICK_START.md`
2. Install the signed 0.2.9 CAPP.
3. Validate KMB 1A, CTB 2, and joint route 101 live on P4; record resource checkpoints.

### Short-term (Phase 4)
1. Add scheduled refresh and live countdown.
2. **Result**: Complete bus app

### Long-term (Post-0.3.0)
1. Accuracy refinement (provider exceptions, remark filtering)
2. UX enhancements (GPS, route map, notifications)
3. Data updates (OTA catalog refresh)
4. Localization (TC/EN/SC)

---

## Conclusion

This transformation is **achievable**. The S3 app proves the UX works. Flutter proves the logic works. P4 Lua runtime proves the platform works. The gap is **integration**, not invention.

**Critical success factor**: Follow the references. When Flutter does X, do X. When S3 does Y, do Y.

**Next**: Follow `BUS_APP_QUICK_START.md` to install 0.2.9 and capture Phase 1/2 board acceptance.

The bus app **will work** on P4. Let's make it happen. 🚀

---

## Document Maintenance

**Last updated**: 2026-11-16  
**Version**: 1.1

**Authors**: User (system architect), Claude Fable 5 (documentation)  
**Status**: Phase 1 implemented; physical P4 acceptance pending

**Change log**:
- 2026-11-16: Initial creation, all guides completed
- 2026-11-16: Phase 1 status, actual host commands and P4 installation/acceptance links
