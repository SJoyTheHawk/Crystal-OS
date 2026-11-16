# Bus App Transformation — Complete Documentation Index

## Overview

This directory contains the complete plan for transforming the **S3 native bus app** into a **working P4 Lua implementation** that replicates—not reinvents—the proven functionality.

**Status**: Documentation complete, implementation ready to start

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

### 3. **BUS_APP_QUICK_START.md** (First milestone)
**Purpose**: Get to "working" in one focused session

**Contains**:
- Step-by-step Phase 1 implementation (4-6 hours)
- Provider integration with real API calls
- Basic ETA display (no joint routes yet)
- Test procedures and success criteria
- Troubleshooting guide

**Start here**: If you want immediate progress, follow this guide first

---

### 4. **APP_PLATFORM_V2_PHASE_17_3_BUS_MIGRATION_PLAN.md** (Legacy)
**Purpose**: Original acceptance test plan (Stage A-E)

**Status**: Being replaced by transformation guides above

**Note**: Keep for historical context and Stage C measurements, but follow the new guides for implementation

---

## Implementation Roadmap

### Phase 1: Provider Integration (Quick Start)
**Time**: 4-6 hours  
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
**Time**: 2-3 hours  
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
**Time**: 2 hours  
**Goal**: Save/restore favorites across app restarts  
**Deliverable**: Favorites tab with 8 saved stops

**Steps**:
1. Implement Store module (add/remove/load)
2. Build Favorites tab UI
3. Add Save button to ETA page
4. Test save → exit → relaunch cycle

**Guide**: `BUS_APP_LUA_CODE_PATTERNS.md` § 4

---

### Phase 4: Refresh & Polish
**Time**: 1-2 hours  
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

**Total time estimate**: 8-12 hours development  
**Result**: Complete working bus app on P4 with Lua

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
- Stop matching algorithm (haversine, 500m threshold)
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

**Missing features** (to be added):
- ❌ Real provider API calls (still uses fixture)
- ❌ Joint route ETA merging
- ❌ Favorites persistence UI
- ❌ ETA refresh timer
- ❌ Complete stop metadata resolution

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

**Response format**: Same as KMB

---

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
**Tool**: Python test harnesses in `tools/`

**Coverage**:
- Provider URL building
- Response parsing
- Timestamp conversion
- Stop matching (haversine)
- ETA sorting

**Run**:
```bash
python3 tools/test_phase17_bus_providers.py
python3 tools/test_phase17_bus_arrivals.py
```

---

### Integration Tests (Device)
**Tool**: Lua test scripts via serial console

**Coverage**:
- KMB route flow (search → stops → ETA)
- CTB route flow
- Joint route flow
- Favorites persistence
- Refresh and cancellation

**Run**:
```bash
python3 tools/test_phase17_bus_ui.lua
```

---

### Acceptance Tests (Manual)
**Environment**: P4 Function EV board with WiFi

**Checklist**:
1. Search "1A" → see KMB routes
2. Select "1A to Chuk Yuen Estate"
3. See stop list with names
4. Tap first stop → see ETA (e.g., "3 min", "8 min")
5. Search "2" → see CTB routes
6. Repeat stop → ETA flow
7. Search "101" → see JOINT route
8. Verify ETA shows both "KMB • 3 min" and "CTB • 5 min"
9. Save a favorite → exit → relaunch → tap favorite → ETA loads
10. Repeat 10 install/uninstall cycles → no crash or leak

---

## Resource Requirements

### Memory
**Lua arena**: 512 KB (current), may need 640 KB for joint routes  
**Peak usage target**: <400 KB (25% headroom)  
**Current baseline**: 379 KB (Stage C catalog search)

**Checkpoints**:
- `launch_ready`: ~180 KB
- `search_ready`: ~235 KB
- `stops_loaded`: ~250 KB
- `eta_complete`: ~250 KB (estimated with provider parsers)

---

### Network
**Concurrent requests**: 1 (queue others)  
**Timeout**: 15 seconds per request  
**TLS**: mbedTLS with session reuse  
**Typical request size**: 2-5 KB response body

---

### Computation
**JSON decoding**: Bounded to 512-node limit (use JSON Lines)  
**Timestamp parsing**: Manual (no `os.time` in sandbox)  
**Distance calculation**: Haversine formula with `math` library

---

## Success Criteria

### Functional
- [x] Search works with full catalog (2115 routes)
- [ ] KMB routes fetch real stops and ETA
- [ ] CTB routes fetch real stops and ETA
- [ ] Joint routes display merged ETA with provider labels
- [ ] Favorites persist across restarts (up to 8)
- [ ] ETA refreshes every 20 seconds
- [ ] Stops paginate (8 per page, all reachable)
- [ ] Network errors show user-friendly messages
- [ ] No fixture server dependency

---

### Performance
- [ ] onCreate completes in <1 second
- [ ] ETA fetch completes in <3 seconds
- [ ] Peak Lua usage <400 KB
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
**Fix**: Increase quota in `app.yaml`, check peak usage in checkpoints

**Issue**: "Invalid JSON"  
**Cause**: Response truncated or malformed  
**Fix**: Print raw body, check `json_largest_input_bytes` in checkpoint

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
-- Check log for: I (timestamp) crystal_http: GET https://...
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
1. Run Python tests: `python3 tools/test_phase17_bus_*.py`
2. Run device acceptance: manual checklist + Lua test script
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

### Immediate (Phase 1)
1. Open `BUS_APP_QUICK_START.md`
2. Follow Step 1-6 (4-6 hours)
3. Get KMB route 1A working end-to-end with real API

### Short-term (Phase 2-4)
1. Add joint route support (2-3 hours)
2. Implement favorites (2 hours)
3. Add refresh timer (1 hour)
4. **Result**: Complete bus app

### Long-term (Post-0.3.0)
1. Accuracy refinement (provider exceptions, remark filtering)
2. UX enhancements (GPS, route map, notifications)
3. Data updates (OTA catalog refresh)
4. Localization (TC/EN/SC)

---

## Conclusion

This transformation is **achievable**. The S3 app proves the UX works. Flutter proves the logic works. P4 Lua runtime proves the platform works. The gap is **integration**, not invention.

**Critical success factor**: Follow the references. When Flutter does X, do X. When S3 does Y, do Y.

**Estimated effort**: 8-12 development hours for a complete, working bus app on P4.

**Start now**: Open `BUS_APP_QUICK_START.md` and begin Phase 1.

The bus app **will work** on P4. Let's make it happen. 🚀

---

## Document Maintenance

**Last updated**: 2026-11-16  
**Version**: 1.0  
**Authors**: User (system architect), Claude Fable 5 (documentation)  
**Status**: Documentation complete, ready for implementation

**Change log**:
- 2026-11-16: Initial creation, all guides completed
