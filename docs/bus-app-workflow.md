# Hong Kong Bus App Workflow
## Design Document

**Date:** 2026-09-24  
**Status:** Draft  
**Purpose:** Define the data flow, caching strategy, and implementation workflow for the Crystal OS bus app

---

## Problem Statement

The previous bus app implementation failed due to:
1. **Underestimated data size**: Route and stop datasets are ~3MB combined
2. **Wrong workflow**: Fetched full datasets on every app launch/refresh
3. **API fragmentation**: KMB and CTB use different data formats (case sensitivity, field names)
4. **Memory constraints**: ESP32-S3 PSRAM limits and DRAM pressure

## Data Categories

### Static Data (slow-change, cacheable)
- **Bus route list**: ~5KB for 1,048 route names ([measured](../memory/hk-transit-api-measured-facts.md))
- **Route details**: Service type, origin, destination, fare
- **Bus stop list**: Full stop database with coordinates
- **Route-stop mapping**: Which stops belong to which routes

**Update frequency**: Daily or weekly  
**Size estimate**: 3MB total uncompressed

### Dynamic Data (real-time, never cached)
- **ETA (Estimated Time of Arrival)**: Per-route, per-stop, per-direction
- **Service status**: Delays, diversions, suspensions

**Update frequency**: On-demand, typically 30-60s refresh

---

## API Endpoints

### KMB (Kowloon Motor Bus)
- Base: `https://data.etabus.gov.hk/v1/transport/kmb/`
- Route list: `GET /route`
- Stop list: `GET /stop`
- Route-stop: `GET /route-stop/{route}/{direction}/{service_type}`
- ETA: `GET /stop-eta/{stop_id}`

### CTB (Citybus) / NWFB (New World First Bus)
- Base: `https://rt.data.gov.hk/v2/transport/citybus/`
- Route list: `GET /route/{company}` (company: `ctb` or `nwfb`)
- Stop list: `GET /stop/{company}`
- Route-stop: `GET /route-stop/{company}/{route}/{direction}`
- ETA: `GET /stop-eta/{company}/{stop_id}/{route}`

**Key difference**: CTB/NWFB require company discriminator in every request

---

## Data Format Differences

| Field | KMB | CTB/NWFB | Normalization |
|-------|-----|----------|---------------|
| Route ID | `"1"` | `"1"` | Direct |
| Stop ID | `"STOP123"` | `"stop123"` | Uppercase all |
| Direction | `"I"`, `"O"` | `"inbound"`, `"outbound"` | Map to enum |
| Coordinates | `lat`, `long` | `lat`, `lng` | Rename `lng` → `long` |
| Timestamp | ISO 8601 | Unix epoch | Convert to ISO |

**Solution reference**: [hk-bus-crawling normalization logic](https://github.com/hkbus/hk-bus-crawling)

---

## Workflow Pseudocode

### 1. Initialization (First Launch)

```
FUNCTION initialize_bus_data():
    IF NOT cache_exists("bus_metadata.version"):
        PRINT "First launch detected, downloading base datasets..."
        
        // Download and normalize static data
        kmb_routes = fetch_and_parse("kmb/route")
        ctb_routes = fetch_and_parse("ctb/route/ctb")
        nwfb_routes = fetch_and_parse("ctb/route/nwfb")
        
        all_routes = normalize_routes(kmb_routes + ctb_routes + nwfb_routes)
        
        kmb_stops = fetch_and_parse("kmb/stop")
        ctb_stops = fetch_and_parse("ctb/stop/ctb")
        nwfb_stops = fetch_and_parse("ctb/stop/nwfb")
        
        all_stops = normalize_stops(kmb_stops + ctb_stops + nwfb_stops)
        
        // Store compressed to NVS or LittleFS
        save_compressed_cache("routes.json.gz", all_routes)
        save_compressed_cache("stops.json.gz", all_stops)
        
        // Mark version and timestamp
        save_cache("bus_metadata.version", CURRENT_VERSION)
        save_cache("bus_metadata.last_update", current_timestamp())
        
        PRINT "Base datasets downloaded: " + size_of(all_routes) + size_of(all_stops)
    ELSE:
        PRINT "Cache found, skipping initial download"
    END IF
END FUNCTION
```

---

### 2. Daily/Weekly Update Check

```
FUNCTION check_for_updates():
    last_update = read_cache("bus_metadata.last_update")
    current_time = current_timestamp()
    
    // Update every 7 days (604800 seconds)
    IF (current_time - last_update) > UPDATE_INTERVAL:
        PRINT "Cache expired, checking for updates..."
        
        // Fetch only route list to check if count changed
        kmb_routes_new = fetch_and_parse("kmb/route")
        kmb_routes_cached = read_compressed_cache("routes.json.gz", filter="kmb")
        
        IF routes_differ(kmb_routes_new, kmb_routes_cached):
            PRINT "Route changes detected, re-downloading..."
            initialize_bus_data()  // Full refresh
        ELSE:
            PRINT "No changes detected, extending cache TTL"
            save_cache("bus_metadata.last_update", current_time)
        END IF
    ELSE:
        PRINT "Cache still valid, skipping update"
    END IF
END FUNCTION
```

---

### 3. Route Search (Uses Cached Data)

```
FUNCTION search_routes(query_string):
    // Load from compressed cache (one-time decompress per session)
    IF NOT in_memory_cache("routes"):
        routes = read_compressed_cache("routes.json.gz")
        load_into_memory("routes", routes)  // Keep in PSRAM
    ELSE:
        routes = get_from_memory("routes")
    END IF
    
    // Filter by user query
    results = []
    FOR EACH route IN routes:
        IF route.number CONTAINS query_string OR
           route.origin_tc CONTAINS query_string OR
           route.dest_tc CONTAINS query_string:
            results.append(route)
        END IF
    END FOR
    
    RETURN results[0:20]  // Limit to 20 results
END FUNCTION
```

---

### 4. Stop List for Route (Uses Cached Data)

```
FUNCTION get_stops_for_route(route_id, direction, company):
    // Fetch route-stop mapping (small, can be fetched on-demand)
    IF company == "kmb":
        url = "kmb/route-stop/" + route_id + "/" + direction + "/1"
    ELSE:
        url = "ctb/route-stop/" + company + "/" + route_id + "/" + direction
    END IF
    
    route_stops = fetch_and_parse(url)
    
    // Load stop details from cache
    stops = read_compressed_cache("stops.json.gz")
    
    // Merge: attach full stop details to route-stop sequence
    result = []
    FOR EACH rs IN route_stops:
        stop_detail = stops.find(stop_id == rs.stop_id)
        result.append({
            seq: rs.seq,
            stop_id: rs.stop_id,
            name_tc: stop_detail.name_tc,
            name_en: stop_detail.name_en,
            lat: stop_detail.lat,
            long: stop_detail.long
        })
    END FOR
    
    RETURN result
END FUNCTION
```

---

### 5. ETA Query (Real-Time, Never Cached)

```
FUNCTION get_eta(route_id, stop_id, company):
    // Always fetch fresh
    IF company == "kmb":
        url = "kmb/stop-eta/" + stop_id
        eta_list = fetch_and_parse(url)
        
        // Filter by route (KMB returns all routes for a stop)
        eta_list = eta_list.filter(eta.route == route_id)
    ELSE:
        url = "ctb/stop-eta/" + company + "/" + stop_id + "/" + route_id
        eta_list = fetch_and_parse(url)
    END IF
    
    // Normalize timestamps
    FOR EACH eta IN eta_list:
        IF eta.timestamp IS unix_epoch:
            eta.timestamp = convert_to_iso8601(eta.timestamp)
        END IF
        
        // Calculate minutes until arrival
        eta.minutes_left = calculate_diff(current_time(), eta.timestamp)
    END FOR
    
    RETURN eta_list[0:3]  // Show next 3 buses
END FUNCTION
```

---

### 6. Normalization Functions

```
FUNCTION normalize_routes(raw_routes):
    normalized = []
    FOR EACH route IN raw_routes:
        normalized.append({
            id: route.route,
            company: detect_company(route),
            origin_tc: route.orig_tc,
            origin_en: route.orig_en,
            dest_tc: route.dest_tc,
            dest_en: route.dest_en,
            service_type: route.service_type OR "1"
        })
    END FOR
    RETURN normalized
END FUNCTION

FUNCTION normalize_stops(raw_stops):
    normalized = []
    FOR EACH stop IN raw_stops:
        normalized.append({
            id: to_uppercase(stop.stop_id),  // KMB uppercase, CTB lowercase
            name_tc: stop.name_tc,
            name_en: stop.name_en,
            lat: stop.lat,
            long: stop.long OR stop.lng,  // Handle CTB 'lng' vs KMB 'long'
            company: detect_company(stop)
        })
    END FOR
    RETURN normalized
END FUNCTION

FUNCTION detect_company(record):
    IF record HAS FIELD "co":
        RETURN record.co  // KMB uses 'co' field
    ELSE IF record.url CONTAINS "citybus":
        RETURN "ctb"
    ELSE:
        RETURN "unknown"
    END IF
END FUNCTION
```

---

### 7. Compression Strategy

```
FUNCTION save_compressed_cache(filename, data):
    json_string = JSON.stringify(data)
    compressed = gzip_compress(json_string)  // Use miniz or similar
    
    // Write to LittleFS partition (not NVS, too large)
    file = open_file("/littlefs/" + filename, WRITE)
    file.write(compressed)
    file.close()
    
    PRINT "Saved " + size_of(compressed) + " bytes (was " + size_of(json_string) + ")"
END FUNCTION

FUNCTION read_compressed_cache(filename, filter=NULL):
    file = open_file("/littlefs/" + filename, READ)
    compressed = file.read()
    file.close()
    
    json_string = gzip_decompress(compressed)
    data = JSON.parse(json_string)
    
    // Optional: filter by company to reduce memory usage
    IF filter:
        data = data.filter(record.company == filter)
    END IF
    
    RETURN data
END FUNCTION
```

---

## Memory Management Strategy

### Phase 1: Cold Start (App Launch)
1. Decompress `routes.json.gz` → PSRAM (keep in memory for session)
2. Do NOT load `stops.json.gz` yet (too large)

### Phase 2: User Searches Route
1. Filter in-memory route list
2. Display results (route number + origin/dest only)

### Phase 3: User Selects Route
1. Fetch route-stop mapping from API (small, ~2-5 KB per route)
2. Decompress `stops.json.gz` → PSRAM (if not already loaded)
3. Merge stop details with route-stop sequence
4. Display stop list

### Phase 4: User Selects Stop
1. Fetch ETA from API (real-time, ~1-3 KB)
2. Display countdown timers
3. Auto-refresh every 30 seconds (fetch ETA again)

### Phase 5: App Backgrounded (onPause)
1. Free `stops.json.gz` decompressed data from PSRAM
2. Keep `routes.json.gz` in memory (smaller, reused often)
3. Cancel ETA refresh timer

---

## Data Size Estimates

| Dataset | Uncompressed | Compressed (gzip) | Load Target |
|---------|--------------|-------------------|-------------|
| Route list | ~500 KB | ~50 KB | PSRAM (session) |
| Stop list | ~2.5 MB | ~250 KB | PSRAM (on-demand) |
| Route-stop mapping | ~2-5 KB/route | N/A | Fetch live |
| ETA | ~1-3 KB/query | N/A | Fetch live |

**Total cached footprint**: ~300 KB on LittleFS  
**Peak PSRAM usage**: ~3 MB (both datasets decompressed)

---

## Update Strategy Decision Tree

```
START
  ↓
Is cache present?
  ↓ No → Full download (initialize_bus_data)
  ↓ Yes
Has 7 days passed?
  ↓ No → Use cache
  ↓ Yes
Fetch route list count
  ↓
Count changed?
  ↓ Yes → Full refresh
  ↓ No → Extend TTL, use cache
```

---

## Error Handling

```
FUNCTION fetch_and_parse(endpoint):
    TRY:
        response = http_get(API_BASE + endpoint)
        
        IF response.status != 200:
            THROW NetworkError("HTTP " + response.status)
        END IF
        
        data = JSON.parse(response.body)
        
        IF data IS NULL OR data.length == 0:
            THROW DataError("Empty response from " + endpoint)
        END IF
        
        RETURN data
        
    CATCH NetworkError AS e:
        PRINT "Network error: " + e.message
        RETURN read_cache_fallback(endpoint)  // Use stale cache if available
        
    CATCH DataError AS e:
        PRINT "Data error: " + e.message
        RETURN []
        
    CATCH TimeoutError AS e:
        PRINT "Request timeout: " + endpoint
        RETURN read_cache_fallback(endpoint)
    END TRY
END FUNCTION
```

---

## Implementation Checklist

### Phase 1: Core Infrastructure
- [ ] HTTP client with TLS for data.gov.hk ([avoid close()](../memory/esp-http-client-close-kills-tls.md))
- [ ] JSON parser (cJSON or ArduinoJson with PSRAM allocator)
- [ ] gzip decompression (miniz library)
- [ ] LittleFS partition for cache storage (300 KB minimum)

### Phase 2: Data Layer
- [ ] Cache manager (check/save/load/expire)
- [ ] Normalization functions (KMB ↔ CTB format converter)
- [ ] In-memory index for fast route search

### Phase 3: UI Components
- [ ] Route search screen with virtual keyboard
- [ ] Stop list screen (route-stop sequence with ETA preview)
- [ ] ETA detail screen (countdown + auto-refresh)
- [ ] Loading indicators (network activity)

### Phase 4: Lifecycle Integration
- [ ] Initialize on app launch (check_for_updates)
- [ ] Free stop data on onPause ([snapshot pattern](../memory/lvgl-snapshot-offscreen-pattern.md))
- [ ] Cancel ETA timers on onStop
- [ ] No NVS reads from LVGL timers ([cache in RAM](../memory/no-nvs-reads-from-lvgl-timers.md))

---

## Open Questions

1. **Compression format**: gzip vs LZ4 vs ZSTD?
   - **RESOLVED: gzip** — S3 ROM has `tinfl_decompress`, best ecosystem support
   
2. **Incremental updates**: Can we fetch only changed records?
   - **RESOLVED: Fetch all records** — API doesn't support delta sync, full fetch more stable for small system
   
3. **Multilingual**: Store both TC + EN names, or user-selectable language only?
   - **RESOLVED: User-selectable only** — Add Settings option, store active language only (TC requires 0.6-2.5MB fonts)
   
4. **Favorites**: Store in NVS as route IDs, or full route objects?
   - **RESOLVED: Route IDs** — Fetch names on load, saves NVS space (40B vs 146B per favorite)
   
5. **Offline mode**: Show cached routes when WiFi is down?
   - **RESOLVED: Yes, but no ETA** — Show cached favorites with age, degrade gracefully

6. **Keypad layout**: Fixed grid vs scrollable strip?
   - **RESOLVED: Scrollable letter strip** — Fixed numbers (0-9), scrollable letters (A-Z), reduces visual overwhelm

---

## App Icon Design

### Procedural Icon Specification

**Reference:** Official KMB app icon (front view of double-decker bus on red background)

**Crystal OS Version** (64×64px, procedural in `bus_icon.c`):

```c
// Color palette
#define ICON_BUS_RED       lv_color_hex(0xE31E24)  // KMB brand red
#define ICON_WHITE         lv_color_hex(0xF8FAFC)  // Off-white outline
#define ICON_WINDOW_BLUE   lv_color_hex(0x38BDF8)  // Sky blue (glass)
#define ICON_AMBER         lv_color_hex(0xFBBF24)  // Headlights
#define ICON_WHEEL_GRAY    lv_color_hex(0x334155)  // Wheels

// Layout (simplified bus front view)
// - Red rounded square background (60×60, centered)
// - White bus silhouette (40×48, centered)
// - 2 blue window rectangles (8×12 each)
// - 4 amber headlight dots (3px radius)
// - 2 gray wheel circles (6px radius, bottom)
```

**Simplified rendering:**
1. Draw red rounded rect (main body)
2. Draw white rounded rect outline (bus frame)
3. Draw 2 small blue rects (upper deck windows)
4. Draw 1 blue rect (windshield)
5. Draw 4 small amber circles (headlights, 2 per side)
6. Draw 2 gray circles (wheels, bottom corners)

**App name:** "Bus" (displayed below icon in launcher)

---

## References

- [hk-bus-crawling](https://github.com/hkbus/hk-bus-crawling) — Normalized daily datasets
- [KMB API Specification](https://data.etabus.gov.hk/datagovhk/kmb_eta_api_specification.pdf)
- [hk-bus-eta](https://github.com/hkbus/hk-bus-eta) — ETA query library
- [Crystal OS memory constraints](../memory/MEMORY.md)

---

**Next Steps:**
1. Review this workflow with the team
2. Prototype compression + cache layer (measure actual sizes)
3. Draft implementation plan with file structure
4. Build minimal UI mockup for user flow validation
