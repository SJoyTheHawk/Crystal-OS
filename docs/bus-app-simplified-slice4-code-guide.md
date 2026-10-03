# Simplified Slice 4R.4-4R.6 Code Guide

**Date:** 2026-10-03  
**Status:** Implementation guide  
**Builds on:** Existing 4R.0-4R.3 implementation in `components/bus_service/`

## Overview

This guide adds local stop lookup to the existing catalog infrastructure. The catalog sync, A/B storage, and binary format already work. Now we connect lookups to the app.

## File Structure

```
components/bus_service/
├── include/
│   └── bus_stop_catalog.h          # Public lookup API (NEW)
├── src/
│   ├── bus_stop_catalog.c          # Catalog lookup implementation (MODIFY)
│   ├── bus_catalog_sync.c          # Sync worker (EXISTING, no changes)
│   ├── bus_service.c               # Route-stop handler (MODIFY)
│   └── bus_app.cpp                 # UI status display (MODIFY)
└── CMakeLists.txt                  # Add new public header
```

## Implementation Steps

### Step 1: Define Lookup API

**File:** `components/bus_service/include/bus_stop_catalog.h` (NEW)

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "bus_types.h"  // for bus_operator_t

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Stop metadata returned by lookup.
 * If valid==false, other fields are undefined.
 */
typedef struct {
    char stop_id[16];
    char name_en[64];
    char name_tc[64];
    float lat;
    float lon;
    bool valid;
} bus_stop_metadata_t;

/**
 * Catalog status for UI feedback.
 */
typedef enum {
    BUS_CATALOG_NONE,      // No catalog files exist yet
    BUS_CATALOG_LOADING,   // Sync worker is downloading
    BUS_CATALOG_READY,     // Loaded and usable
    BUS_CATALOG_STALE      // >30 days old (optional warning)
} bus_catalog_status_t;

typedef struct {
    bus_catalog_status_t status;
    uint32_t kmb_record_count;
    uint32_t ctb_record_count;
    time_t kmb_source_time;
    time_t ctb_source_time;
} bus_catalog_info_t;

/**
 * Look up stop metadata from local catalog.
 * Returns true if found, false if catalog missing or stop not found.
 * Thread-safe: can be called from any task.
 */
bool bus_stop_catalog_lookup(const char *stop_id,
                              bus_operator_t operator,
                              bus_stop_metadata_t *out);

/**
 * Get catalog status for UI display.
 */
void bus_catalog_get_info(bus_catalog_info_t *out);

/**
 * Force catalog to load from disk (call once at service init).
 * Returns ESP_OK if at least one provider loaded.
 */
esp_err_t bus_stop_catalog_init(void);

#ifdef __cplusplus
}
#endif
```

### Step 2: Implement Lazy-Load Lookup

**File:** `components/bus_service/src/bus_stop_catalog.c` (MODIFY EXISTING)

The file already has sync logic. Add the lookup logic beside it:

```c
#include "bus_stop_catalog.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "bus_stop_catalog";

// BSC2 binary format structures (already defined in your code)
typedef struct __attribute__((packed)) {
    uint32_t magic;          // 'BSC2'
    uint16_t version;        // 2
    uint16_t provider;       // 0=KMB, 1=CTB
    uint32_t record_count;
    uint32_t index_offset;   // offset to first index entry
    uint32_t pool_offset;    // offset to string pool
    uint32_t total_size;
    uint8_t  sha256[32];
} bsc2_header_t;

typedef struct __attribute__((packed)) {
    char     stop_id[16];
    uint32_t name_en_offset;
    uint16_t name_en_length;
    uint32_t name_tc_offset;
    uint16_t name_tc_length;
    float    lat;
    float    lon;
    uint8_t  has_coords;
    uint8_t  _reserved[3];
} bsc2_index_entry_t;

// In-memory catalog state (per provider)
typedef struct {
    bool loaded;
    uint32_t record_count;
    time_t source_time;
    bsc2_index_entry_t *index;  // PSRAM array
    uint8_t *pool;               // PSRAM string pool
    uint32_t pool_size;
} catalog_state_t;

static catalog_state_t s_kmb_catalog = {0};
static catalog_state_t s_ctb_catalog = {0};
static SemaphoreHandle_t s_lock = NULL;  // Protect catalog state

// Load one provider's catalog into memory
static esp_err_t load_catalog(const char *path, catalog_state_t *state) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "catalog not found: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    bsc2_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
        fclose(f);
        return ESP_FAIL;
    }

    if (hdr.magic != 0x32435342) {  // 'BSC2'
        ESP_LOGE(TAG, "invalid magic in %s", path);
        fclose(f);
        return ESP_ERR_INVALID_VERSION;
    }

    // Allocate index in PSRAM
    const size_t index_size = hdr.record_count * sizeof(bsc2_index_entry_t);
    state->index = heap_caps_malloc(index_size, MALLOC_CAP_SPIRAM);
    if (!state->index) {
        ESP_LOGE(TAG, "failed to allocate %zu bytes for index", index_size);
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    // Read index
    fseek(f, hdr.index_offset, SEEK_SET);
    if (fread(state->index, sizeof(bsc2_index_entry_t), hdr.record_count, f) 
        != hdr.record_count) {
        heap_caps_free(state->index);
        state->index = NULL;
        fclose(f);
        return ESP_FAIL;
    }

    // Allocate and read string pool
    state->pool_size = hdr.total_size - hdr.pool_offset;
    state->pool = heap_caps_malloc(state->pool_size, MALLOC_CAP_SPIRAM);
    if (!state->pool) {
        heap_caps_free(state->index);
        state->index = NULL;
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    fseek(f, hdr.pool_offset, SEEK_SET);
    if (fread(state->pool, 1, state->pool_size, f) != state->pool_size) {
        heap_caps_free(state->index);
        heap_caps_free(state->pool);
        state->index = NULL;
        state->pool = NULL;
        fclose(f);
        return ESP_FAIL;
    }

    state->record_count = hdr.record_count;
    state->loaded = true;
    fclose(f);

    ESP_LOGI(TAG, "loaded catalog %s: %u records, %zu index, %u pool",
             path, hdr.record_count, index_size, state->pool_size);
    return ESP_OK;
}

esp_err_t bus_stop_catalog_init(void) {
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }

    // Load KMB (find highest valid generation)
    // For now, hardcode g1_s0 (your existing files)
    (void)load_catalog("/spiffs/bus_stops_kmb_g1_s0.bsc", &s_kmb_catalog);

    // Load CTB
    (void)load_catalog("/spiffs/bus_stops_ctb_g1_s0.bsc", &s_ctb_catalog);

    if (!s_kmb_catalog.loaded && !s_ctb_catalog.loaded) {
        ESP_LOGW(TAG, "no catalogs loaded; lookups will fail");
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

// Binary search helper
static int compare_stop_id(const void *a, const void *b) {
    const char *key = (const char *)a;
    const bsc2_index_entry_t *entry = (const bsc2_index_entry_t *)b;
    return strncmp(key, entry->stop_id, sizeof(entry->stop_id));
}

bool bus_stop_catalog_lookup(const char *stop_id,
                              bus_operator_t operator,
                              bus_stop_metadata_t *out) {
    if (!stop_id || !out) return false;

    memset(out, 0, sizeof(*out));
    out->valid = false;

    catalog_state_t *cat = (operator == BUS_OP_KMB) ? &s_kmb_catalog 
                                                     : &s_ctb_catalog;

    if (!s_lock) return false;  // Not initialized
    xSemaphoreTake(s_lock, portMAX_DELAY);

    if (!cat->loaded) {
        xSemaphoreGive(s_lock);
        return false;
    }

    // Binary search (index is sorted by stop_id)
    bsc2_index_entry_t *entry = bsearch(
        stop_id,
        cat->index,
        cat->record_count,
        sizeof(bsc2_index_entry_t),
        compare_stop_id
    );

    if (!entry) {
        xSemaphoreGive(s_lock);
        return false;  // Not found
    }

    // Copy stop_id
    strlcpy(out->stop_id, entry->stop_id, sizeof(out->stop_id));

    // Extract name_en from pool
    if (entry->name_en_offset + entry->name_en_length <= cat->pool_size) {
        const size_t copy_len = (entry->name_en_length < sizeof(out->name_en) - 1)
                                ? entry->name_en_length
                                : sizeof(out->name_en) - 1;
        memcpy(out->name_en, cat->pool + entry->name_en_offset, copy_len);
        out->name_en[copy_len] = '\0';
    }

    // Extract name_tc from pool
    if (entry->name_tc_offset + entry->name_tc_length <= cat->pool_size) {
        const size_t copy_len = (entry->name_tc_length < sizeof(out->name_tc) - 1)
                                ? entry->name_tc_length
                                : sizeof(out->name_tc) - 1;
        memcpy(out->name_tc, cat->pool + entry->name_tc_offset, copy_len);
        out->name_tc[copy_len] = '\0';
    }

    // Coordinates
    if (entry->has_coords) {
        out->lat = entry->lat;
        out->lon = entry->lon;
    }

    out->valid = true;
    xSemaphoreGive(s_lock);
    return true;
}

void bus_catalog_get_info(bus_catalog_info_t *out) {
    memset(out, 0, sizeof(*out));

    if (!s_lock) {
        out->status = BUS_CATALOG_NONE;
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    if (s_kmb_catalog.loaded || s_ctb_catalog.loaded) {
        out->status = BUS_CATALOG_READY;
        out->kmb_record_count = s_kmb_catalog.record_count;
        out->ctb_record_count = s_ctb_catalog.record_count;
        // source_time would come from manifest parsing (optional)
    } else {
        // Check if sync worker is running (you'd need a flag from sync module)
        out->status = BUS_CATALOG_NONE;
    }

    xSemaphoreGive(s_lock);
}
```

**Notes:**
- This loads catalogs once at init, keeps them in PSRAM
- Binary search is O(log n), very fast (13 lookups for 6753 records)
- Thread-safe with mutex
- Falls back gracefully if catalog missing

### Step 3: Call Init at Service Startup

**File:** `components/bus_service/src/bus_service.c`

In your existing `bus_service_init()` or equivalent:

```c
#include "bus_stop_catalog.h"

esp_err_t bus_service_init(void) {
    // ... existing init code ...
    
    // Initialize stop catalog lookup
    esp_err_t err = bus_stop_catalog_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "stop catalog init failed: %s (will use fallbacks)", 
                 esp_err_to_name(err));
        // Don't fail service init, just proceed without names
    }
    
    // ... rest of init ...
}
```

### Step 4: Integrate Lookup into Route-Stop Handler

**File:** `components/bus_service/src/bus_service.c`

Find your `process_stops_request()` or equivalent that handles route-stop responses:

```c
#include "bus_stop_catalog.h"

static void process_stops_request(bus_service_t *svc, 
                                   crystal_http_response_t *response) {
    // ... existing JSON parsing that fills route_stops[] array ...
    
    // Example: you already have something like:
    // route_stop_t route_stops[MAX_STOPS];
    // int stop_count = parse_route_stops_json(response->body, route_stops);
    
    // NEW: Enrich each stop with catalog metadata
    for (int i = 0; i < stop_count; i++) {
        bus_stop_metadata_t meta = {};
        
        if (bus_stop_catalog_lookup(route_stops[i].stop_id, 
                                     svc->current_operator, 
                                     &meta)) {
            // Success: copy names and coords
            strlcpy(route_stops[i].name_en, meta.name_en, 
                    sizeof(route_stops[i].name_en));
            strlcpy(route_stops[i].name_tc, meta.name_tc, 
                    sizeof(route_stops[i].name_tc));
            route_stops[i].lat = meta.lat;
            route_stops[i].lon = meta.lon;
            route_stops[i].has_metadata = true;
        } else {
            // Fallback: show stop_id as name
            snprintf(route_stops[i].name_en, sizeof(route_stops[i].name_en),
                     "Stop %s", route_stops[i].stop_id);
            strlcpy(route_stops[i].name_tc, route_stops[i].name_en,
                    sizeof(route_stops[i].name_tc));
            route_stops[i].has_metadata = false;
        }
    }
    
    // Emit event to UI (existing code)
    bus_event_stops_list_t evt = {
        .stops = route_stops,
        .count = stop_count,
        // ... other fields ...
    };
    post_bus_event(BUS_EVT_STOPS_LIST, &evt, sizeof(evt));
}
```

**Key points:**
- No HTTP requests triggered by lookup misses
- Fallback text is fine (new stops not yet in catalog)
- `has_metadata` flag tells UI whether name is real or fallback

### Step 5: Update CMakeLists.txt

**File:** `components/bus_service/CMakeLists.txt`

```cmake
idf_component_register(
    SRCS 
        "src/bus_service.c"
        "src/bus_catalog_sync.c"
        "src/bus_stop_catalog.c"
        # ... other sources ...
    INCLUDE_DIRS 
        "include"
    REQUIRES
        crystal_http
        nvs_flash
        esp_http_client
        json
        # ... other deps ...
)
```

### Step 6: Optional UI Status Display

**File:** `components/bus_app/src/bus_app.cpp`

Show catalog status while loading:

```cpp
#include "bus_stop_catalog.h"

bool BusApp::onCreate() {
    // ... existing UI creation ...
    
    // Check catalog status
    bus_catalog_info_t info = {};
    bus_catalog_get_info(&info);
    
    if (info.status == BUS_CATALOG_LOADING) {
        // Show loading overlay on route list
        lv_obj_t *loading = lv_label_create(search_tab_);
        lv_label_set_text(loading, "Loading stop data...");
        lv_obj_center(loading);
    } else if (info.status == BUS_CATALOG_READY) {
        ESP_LOGI(TAG, "catalog ready: KMB %u stops, CTB %u stops",
                 info.kmb_record_count, info.ctb_record_count);
    }
    
    // Enable search regardless (fallbacks will work)
    // ...
}
```

**Don't overcomplicate this.** Just show loading once, then let fallbacks handle the rest.

## Build and Test

### Build Commands

```bash
cd /Users/szemy/Workspace/ESP32\ Crystal\ OS
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
idf.py build
```

Expected: clean build, no errors

### Flash and Test

```bash
idf.py -p /dev/cu.usbmodem1201 flash monitor -b 2000000
```

### Test Sequence

**Test 1: With existing catalogs (warm boot)**

Logs should show:
```
I (xxxx) bus_stop_catalog: loaded catalog /spiffs/bus_stops_kmb_g1_s0.bsc: 6753 records
I (xxxx) bus_stop_catalog: loaded catalog /spiffs/bus_stops_ctb_g1_s0.bsc: 2586 records
```

Browse CTB route 10 inbound → stop names appear (not "Stop 001A")

**Test 2: With no catalogs (cold boot)**

Delete catalog files or erase flash, then boot:
```
W (xxxx) bus_stop_catalog: catalog not found: /spiffs/bus_stops_kmb_g1_s0.bsc
W (xxxx) bus_stop_catalog: no catalogs loaded; lookups will fail
```

Browse route → fallback "Stop XXX" text appears
Wait for sync → catalogs download
Browse again → real names appear

**Test 3: Lookup performance**

Add timing logs around lookup:
```c
int64_t start = esp_timer_get_time();
bool found = bus_stop_catalog_lookup(stop_id, operator, &meta);
int64_t elapsed = esp_timer_get_time() - start;
ESP_LOGI(TAG, "lookup %s: found=%d time=%lld us", stop_id, found, elapsed);
```

Expected: <100 microseconds per lookup (binary search is fast)

## Common Issues

**Issue 1: "Failed to allocate index"**

The index needs ~336KB PSRAM. Check free PSRAM:
```c
ESP_LOGI(TAG, "free PSRAM: %u", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
```

Should be >500KB. If not, you have a PSRAM leak elsewhere.

**Issue 2: "Names are garbage"**

String pool offsets might be wrong. Check:
- Is the file the correct BSC2 format?
- Re-run `python3 tools/bus_catalog/build_stop_catalogs.py` to regenerate
- Verify SHA-256 in manifest matches file

**Issue 3: "Lookup always returns false"**

Check:
- Did `bus_stop_catalog_init()` run at startup?
- Are catalog files actually present in `/spiffs/`?
- Is stop_id uppercase/lowercase correct? (Should match exactly)

**Issue 4: "Crash in bsearch"**

Null pointer. Check:
- `cat->index != NULL`
- `cat->record_count > 0`
- Mutex is held during lookup

## Exit Checklist

Before moving to Slice 5, verify:

- [ ] `bus_stop_catalog_lookup()` compiles and links
- [ ] Catalog files load at startup (check logs)
- [ ] CTB route 10 shows 40 real stop names, not "Stop XXX"
- [ ] KMB route 101 shows 35 real stop names
- [ ] Unknown stops show fallback text (no crash)
- [ ] No HTTP detail requests in logs (grep for "stop-detail")
- [ ] Warm boot reuses catalogs (no re-download)
- [ ] Lookup time <100 microseconds (log timing)

When all 8 checks pass → Slice 4 is complete, move to Slice 5.
