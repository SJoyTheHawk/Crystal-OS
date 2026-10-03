# Bus App Completion Roadmap — Summary

**Date:** 2026-10-03  
**Status:** Implementation roadmap  
**Goal:** Complete the bus app as a reference implementation in 1-2 weeks

## Overview

You've been working on the bus app for a long time with production-level complexity. This roadmap simplifies the remaining work to get a **working, demonstrable app** quickly, which can then serve as the reference for Phase 12-13 app platform work.

## Current Status

**✅ Already Complete (Slice 1-3, 4R.0-4R.3):**
- Runtime KMB/CTB route catalogs with 7-day caching
- Provider-qualified route variants
- Normalized route metadata with destinations
- Search UI with keypad and route selection
- HTTPS sync through `crystal_http`
- Prepared stop catalogs (943KB) with A/B durable storage
- Python converter tool and binary format
- Manifest-based download with unchanged detection

**⚠️ Known Issue:**
- TLS hardware AES occasionally fails (internal RAM exhaustion)
- Fix is one config line: `CONFIG_MBEDTLS_HARDWARE_AES=n`

**🔄 Remaining Work:**
- Local stop lookup (4R.4-4R.6): 2-3 days
- Stop picker UI (Slice 5): 1 day
- ETA screen with refresh (Slice 6): 1-2 days
- Favorites with cards (Slice 7): 1-2 days
- **Total: ~1 week**

## Implementation Order

### Step 0: Fix TLS AES Issue (30 minutes)

**Document:** `crystal-http-tls-aes-fix-procedure.md`

Apply the one-line config fix to prevent TLS allocation failures:

```bash
echo "CONFIG_MBEDTLS_HARDWARE_AES=n" >> sdkconfig.defaults
idf.py reconfigure
idf.py build
idf.py flash
```

**Test:** Browse CTB route 10 + KMB route 101, verify no `esp-aes` errors in logs.

**Why now:** This error will get worse as you add ETA refresh. Fix it before adding more HTTPS requests.

---

### Step 1: Simplified 4R.4-4R.6 (2-3 days)

**Documents:**
- Plan: `bus-app-simplified-slice4-completion.md`
- Code Guide: `bus-app-simplified-slice4-code-guide.md`

**What you're building:**
- `bus_stop_catalog_lookup(stop_id, operator)` → returns name, coords
- Load catalog files into PSRAM at startup (binary search for lookups)
- Integrate lookup into route-stop handler (enrich with names)

**What you're skipping:**
- ❌ Full D4 fault injection (interrupt/corrupt/no-space)
- ❌ Production hosting setup
- ❌ Scheduled regeneration

**Exit criteria:**
- [ ] `bus_stop_catalog_lookup()` works for both KMB and CTB
- [ ] Route 101 shows 35 stops with real names (not "Stop XXX")
- [ ] Warm boot reuses cached catalogs
- [ ] No HTTP detail requests triggered by browsing

**Key files to create/modify:**
- `components/bus_service/include/bus_stop_catalog.h` (NEW)
- `components/bus_service/src/bus_stop_catalog.c` (MODIFY - add lookup)
- `components/bus_service/src/bus_service.c` (MODIFY - enrich stops)

---

### Step 2: Slice 5 — Stop Picker (1 day)

**Documents:**
- Plan: `bus-app-simplified-slice5-7-plan.md` (Slice 5 section)
- Code Guide: `bus-app-simplified-slice5-7-code-guide.md` (Slice 5 section)

**What you're building:**
- Scrollable list showing "1. Stop Name", "2. Stop Name", etc.
- Tap a stop → save selection, navigate to ETA screen
- Back button → return to Search

**Exit criteria:**
- [ ] Route 101 inbound shows 35 named stops
- [ ] Tapping stop #10 logs the complete selection
- [ ] Back button returns to Search
- [ ] No crashes on repeated open/back cycles

**Key additions:**
- `BusApp::build_stop_picker()` - create UI
- `BusApp::rebuild_stop_list()` - populate from event
- `BusApp::on_stop_selected()` - handle tap

---

### Step 3: Slice 6 — ETA Screen (1-2 days)

**Documents:**
- Plan: `bus-app-simplified-slice5-7-plan.md` (Slice 6 section)
- Code Guide: `bus-app-simplified-slice5-7-code-guide.md` (Slice 6 section)

**What you're building:**
- ETA screen showing next 3 arrival times
- 30-second auto-refresh
- "Updated Xs ago" freshness display
- "Add to Favorites" button

**Exit criteria:**
- [ ] ETA screen shows 3 arrival times with minutes + clock time
- [ ] Times update every 30s automatically
- [ ] Freshness text updates every 1s
- [ ] Save button adds to favorites (check NVS)
- [ ] No refresh while app is paused
- [ ] Back button returns to stop picker

**Key additions:**
- `bus_service_request_eta()` - API request
- `BusApp::build_eta_screen()` - create UI
- `BusApp::start_eta_refresh()` - 30s timer
- `BusApp::on_save_favorite_clicked()` - NVS save

---

### Step 4: Slice 7 — Favorites (1-2 days)

**Documents:**
- Plan: `bus-app-simplified-slice5-7-plan.md` (Slice 7 section)
- Code Guide: `bus-app-simplified-slice5-7-code-guide.md` (Slice 7 section)

**What you're building:**
- Favorites tab with up to 8 cards
- Each card shows route → stop name + next 3 ETAs
- Staggered refresh (avoid burst)
- Edit mode with delete button

**Exit criteria:**
- [ ] Up to 8 favorite cards render
- [ ] Each card shows route/stop name
- [ ] ETAs update automatically every 30s
- [ ] Tapping card opens ETA screen
- [ ] Edit mode shows delete buttons
- [ ] Deleting a favorite removes it from NVS
- [ ] Favorites survive reboot
- [ ] Empty state shows "No favorites yet"

**Key additions:**
- `BusApp::load_favorites()` / `save_favorites()` - NVS persistence
- `FavoriteCard` class - card widget
- `BusApp::refresh_all_favorites()` - staggered requests
- `BusApp::enter_edit_mode()` - delete UI

---

## Final Testing

After all slices are complete:

### Complete User Flow Test
1. ✅ Search for route 101
2. ✅ Select inbound → see 35 stops with names
3. ✅ Tap stop #10 "Jordan MTR"
4. ✅ See 3 ETAs updating every 30s
5. ✅ Tap "Add to Favorites"
6. ✅ Go to Favorites tab → see card
7. ✅ Card shows ETAs refreshing
8. ✅ Reboot device → favorite still there

### Memory Stability Test
1. ✅ Switch tabs 20 times
2. ✅ Open/close ETA screen 10 times
3. ✅ Add/remove 5 favorites
4. ✅ Let it run for 30 minutes
5. ✅ Check heap: no progressive leak
6. ✅ Check logs: no watchdog warnings

### Error Handling Test
1. ✅ Turn off Wi-Fi → shows "No connection"
2. ✅ Turn on Wi-Fi → next refresh succeeds
3. ✅ Browse route with unknown stops → shows fallback text
4. ✅ Empty favorites → shows "No favorites yet"

## Success Criteria

The bus app is **complete as a reference implementation** when:

1. ✅ User can search for routes by number
2. ✅ Stop names are real (from catalog), not IDs
3. ✅ Live arrival times display and auto-refresh
4. ✅ Users can save up to 8 favorites
5. ✅ Favorites persist across reboot
6. ✅ No memory leaks over extended use
7. ✅ No crashes or watchdog timeouts
8. ✅ Works gracefully offline

## What This Demonstrates

When complete, the bus app proves:

- ✅ `CrystalApp` lifecycle (onCreate/onPause/onResume/onDestroy)
- ✅ Multi-screen navigation with state preservation
- ✅ Service/UI task boundary (no LVGL on worker thread)
- ✅ Async HTTPS through `crystal_http`
- ✅ Request ownership and cancellation
- ✅ Background auto-refresh patterns
- ✅ NVS persistence and recovery
- ✅ Large dataset handling (943KB catalogs)
- ✅ Multi-provider API normalization

**This is sufficient** as the reference for Phase 12-13 app platform work.

## What We're Intentionally Deferring

These features are **not** in this roadmap (add later if needed):

- ❌ Co-operated route merging (showing KMB + CTB on same route)
- ❌ Language switching (EN/TC toggle)
- ❌ Offline request queue
- ❌ Favorite reordering (drag to rearrange)
- ❌ Push notifications for arrivals
- ❌ Route map view
- ❌ Nearby tab (requires location)
- ❌ Production hosting infrastructure
- ❌ Automatic catalog regeneration

All of these can be added later. The goal now is: **working app, quickly**.

## Time Estimate

| Task | Time | Calendar Days |
|------|------|---------------|
| TLS AES fix | 30 min | 0.5 |
| 4R.4-4R.6 (lookup) | 2-3 days | 2-3 |
| Slice 5 (picker) | 1 day | 1 |
| Slice 6 (ETA) | 1-2 days | 1-2 |
| Slice 7 (favorites) | 1-2 days | 1-2 |
| Integration testing | 1 day | 1 |
| **Total** | **6-10 days** | **~1-2 weeks** |

## Next Steps After Bus App

Once the bus app is complete:

1. **Document the patterns** you learned (1 day):
   - Service/UI boundary
   - Async request lifecycle
   - State persistence strategies
   - The widget contracts an app needs

2. **Move to Phase 14-17** (app platform core):
   - Convert Calculator to a packaged app
   - Build the `.capp` packer
   - Prove install/uninstall/boot-reconcile
   - Design the widget façade based on what bus app needed

3. **Optional: Transform bus app to `.lua`**:
   - Use the working C++ version as the reference
   - Port to Lua to test the script runtime
   - This validates Phase 17 without building a new app

## Key Principle

> **"Done is better than perfect for a reference implementation."**

The bus app's job is to:
- ✅ Demonstrate the `CrystalApp` pattern in a real multi-API app
- ✅ Prove the service/UI boundary works
- ✅ Show how state persistence should work

It doesn't need to:
- ❌ Handle every edge case perfectly
- ❌ Have production-grade fault injection
- ❌ Support every possible feature

Get it working, document what you learned, and move to the app platform core. You can always come back to add features later with better infrastructure.

## Documents Reference

All implementation details are in these files:

1. **TLS Fix:** `crystal-http-tls-aes-fix-procedure.md`
2. **Slice 4 Plan:** `bus-app-simplified-slice4-completion.md`
3. **Slice 4 Code:** `bus-app-simplified-slice4-code-guide.md`
4. **Slice 5-7 Plan:** `bus-app-simplified-slice5-7-plan.md`
5. **Slice 5-7 Code:** `bus-app-simplified-slice5-7-code-guide.md`
6. **This Summary:** `bus-app-completion-roadmap.md`

Start with the TLS fix, then work through 4R.4 → 4R.5 → 4R.6 → Slice 5 → Slice 6 → Slice 7.

Test each slice before moving to the next. When all tests pass → **bus app is done!**
