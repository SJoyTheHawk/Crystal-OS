# Simplified Slice 4R.4–4R.6: Stop Catalog Lookup and Handoff

**Date:** 2026-10-03  
**Status:** Planned (after TLS AES fix)  
**Estimated time:** 2-3 days  
**Depends on:** Slice 4R.0-4R.3 (already complete), TLS AES fix verified

## Goal

Complete the stop catalog work by adding local lookup functionality. Skip the production-grade fault injection and hosting requirements. The catalog infrastructure (converter, sync, A/B storage) already works—just connect it to the app.

## What We're Keeping from 4R

✅ **Keep (already done):**
- Python converter tool and binary format
- Manifest-based HTTPS sync
- A/B durable storage
- Device evidence: downloads work, warm boot works, unchanged detection works

🔄 **Simplify:**
- Skip the full D4 fault injection matrix (interrupt/corrupt/no-space)
- Use GitHub raw URLs for hosting (good enough for development)
- Skip scheduled regeneration (manual regeneration is fine)
- Minimal error states (just "catalog loading", "catalog ready", "catalog stale")

❌ **Remove:**
- Production hosting requirements
- Automatic scheduled artifact generation
- Full recovery matrix validation

## Work Packages

### 4R.4 — Local Lookup API

**File:** `components/bus_service/src/bus_stop_catalog.c`

Add a simple lookup function that reads from the persisted catalog files:

```c
typedef struct {
    char stop_id[16];
    char name_en[64];
    char name_tc[64];
    float lat;
    float lon;
    bool valid;
} bus_stop_metadata_t;

// Returns true if stop was found and output is populated
bool bus_stop_catalog_lookup(const char *stop_id,
                              bus_operator_t operator,
                              bus_stop_metadata_t *out);
```

**Implementation approach:**

1. **On first call**, load the active catalog file into memory:
   - KMB: `/spiffs/bus_stops_kmb_g1_s0.bsc` (or highest valid generation)
   - CTB: `/spiffs/bus_stops_ctb_g1_s0.bsc`
   - Parse BSC2 header, keep index in PSRAM
   - Keep file open or memory-map the string pool

2. **On lookup**, binary search the sorted index by stop_id

3. **Return** metadata struct with `valid=true` if found, `valid=false` if not

**States:**
- No catalog files exist → return `valid=false`, log once
- Catalog exists but lookup misses → return `valid=false`, don't log per miss
- Catalog exists and hit → return metadata with `valid=true`

**Memory budget:**
- KMB index: 243,108 bytes (6,753 × 36-byte entries)
- CTB index: 93,096 bytes (2,586 × 36-byte entries)
- Total: ~336KB in PSRAM (acceptable, you have 3.5MB free)

### 4R.5 — Integrate Lookup into Stop Request Handler

**File:** `components/bus_service/src/bus_service.c`

When a route-stop response arrives:

```c
void process_stops_request(bus_service_t *svc, crystal_http_response_t *response) {
    // ... existing parsing ...
    
    for (int i = 0; i < stop_count; i++) {
        // Already have: route_stops[i].stop_id, .sequence
        
        // NEW: Look up metadata from catalog
        bus_stop_metadata_t meta = {};
        if (bus_stop_catalog_lookup(route_stops[i].stop_id, 
                                     current_operator, 
                                     &meta)) {
            strlcpy(route_stops[i].name_en, meta.name_en, sizeof(...));
            strlcpy(route_stops[i].name_tc, meta.name_tc, sizeof(...));
            route_stops[i].lat = meta.lat;
            route_stops[i].lon = meta.lon;
        } else {
            // Fallback: show stop_id as name
            snprintf(route_stops[i].name_en, sizeof(...), "Stop %s", 
                     route_stops[i].stop_id);
            strlcpy(route_stops[i].name_tc, route_stops[i].name_en, sizeof(...));
        }
    }
    
    // Emit stop list event (existing code)
    post_stops_event(svc, route_stops, stop_count);
}
```

**No detail requests.** If lookup misses, use fallback text and move on.

### 4R.6 — Status API and Minimal UI

**Add catalog status API:**

```c
typedef enum {
    BUS_CATALOG_NONE,      // No catalog files exist
    BUS_CATALOG_LOADING,   // Sync in progress
    BUS_CATALOG_READY,     // Catalogs loaded and usable
    BUS_CATALOG_STALE      // >30 days old (optional, can defer)
} bus_catalog_status_t;

typedef struct {
    bus_catalog_status_t status;
    uint32_t kmb_record_count;
    uint32_t ctb_record_count;
    time_t   kmb_source_time;  // from manifest
    time_t   ctb_source_time;
} bus_catalog_info_t;

void bus_catalog_get_info(bus_catalog_info_t *out);
```

**UI integration (minimal):**

In `bus_app.cpp`, show catalog status only when relevant:
- While `LOADING`: show spinner on route list, disable search
- When `READY`: enable search, browsing works normally
- When `NONE`: show "Downloading stop data..." once, then work normally

Don't add a separate settings page or refresh button yet. Just make it work.

## Device Testing

**Test 1: Cold boot with no catalogs**
1. Erase SPIFFS: `idf.py -p /dev/cu.usbmodem1201 erase-flash` (or just delete catalog files via monitor)
2. Flash and boot
3. Wait for catalog sync to download KMB and CTB
4. Browse CTB route 10 inbound → verify stop names appear (not "Stop 001A")
5. Browse KMB route 101 → verify stop names appear

**Test 2: Warm boot with catalogs**
1. Reboot device (without erasing)
2. Browse immediately → stop names should appear without waiting for download
3. Check logs: "KMB catalog unchanged", "CTB catalog unchanged"

**Test 3: Catalog missing for one provider**
1. Manually delete CTB catalog file (via monitor or USB)
2. Reboot
3. Browse KMB route → should work with names
4. Browse CTB route → should work with fallback "Stop 001A" text
5. Wait for sync → CTB should download and names appear on next selection

**Test 4: Lookup miss (stop not in catalog)**
1. Browse a route with stops
2. If any stop shows "Stop XXX" fallback → that's a miss, expected for new stops
3. Verify app doesn't crash, doesn't trigger detail HTTP request

**Success criteria:**
- Stop names appear for KMB and CTB routes
- Fallback text appears for unknown stops (not crashes)
- No per-stop HTTP detail requests
- Catalog survives reboot
- Warm boot doesn't re-download unchanged catalogs

## Simplified Acceptance

**Required:**
- ✅ Local lookup works for both providers
- ✅ Stop names appear in route-stop list
- ✅ Fallback text for unknown stops
- ✅ No detail HTTP requests triggered by browsing
- ✅ Warm boot reuses cached catalogs

**Deferred/Skipped:**
- ❌ Full D4 fault injection (we already have basic A/B working)
- ❌ Production hosting setup (GitHub raw is fine)
- ❌ Scheduled regeneration (manual is fine)
- ❌ Stale detection UI (can add later if needed)
- ❌ Directional destination enrichment (Slice 5 can handle display)

## Exit Criteria

When these five checks pass, Slice 4 is done:

1. **Lookup API exists and works:**
   ```c
   bus_stop_metadata_t meta;
   bool found = bus_stop_catalog_lookup("001A", BUS_OP_KMB, &meta);
   // If catalog exists, found==true and meta.name_en is populated
   ```

2. **Route browsing shows names:**
   - CTB route 10 inbound: 40 stops with Chinese/English names
   - KMB route 101 ST1: 35 stops with names

3. **No detail requests in logs:**
   - Grep for `stop-detail` or individual stop HTTP → should be zero
   - Only route-stop and catalog-sync requests appear

4. **Warm boot is fast:**
   - Reboot, immediately browse → names appear
   - Log shows: "loaded from generation X"

5. **Unknown stops don't crash:**
   - New stop not in catalog → shows "Stop 123"
   - App stays usable, no null pointers, no HTTP fan-out

## What Happens Next

After 4R.6 exit criteria pass → **Move to Slice 5** (stop picker UI)

The catalog work is done. You have:
- 943KB of stop metadata on device
- A working lookup function
- No per-stop HTTP requests

That's all Slice 5 needs to render a scrollable stop list with real names.
