# Bus App Lessons for App Platform Design

**Date:** 2026-10-05  
**Source:** `examples/bus_app/` extracted at fc20cf7  
**Status:** Architecture evidence for Phases 14-17

## Executive Summary

The bus app development validated that the `CrystalApp` lifecycle works for complex 
multi-API workloads, and identified five critical boundaries that Phase 14+ package 
infrastructure must formalize:

1. **Resource ownership** — singleton services cannot unload cleanly
2. **Memory budgets** — no upfront quota enforcement or admission control
3. **Permission model** — no manifest-declared network allowlists
4. **Transactional teardown** — no verification that resources were released
5. **State isolation** — NVS keys lack namespace prefixes

Development was halted due to ESP32-S3 internal DRAM exhaustion (719 bytes remaining 
at worst case), but the extracted code serves as the demanding reference workload 
for designing the app platform's resource management contracts.

## What the Bus App Demonstrated

### ✅ Lifecycle Compliance

- **Four-state contract**: onCreate/onPause/onResume/onDestroy properly implemented
- **State persistence**: 8 favorites (32 bytes each) survive app destruction via NVS
- **Timer cleanup**: 30-second ETA refresh timer deleted at onPause
- **Listener cleanup**: Network event handler unregistered at onDestroy
- **Request cancellation**: In-flight HTTP requests aborted via `bus_service_cancel_all()`

**Validation**: The app survived 20+ switch-away/switch-back cycles without leaking 
timers, listeners, or memory allocations.

### ✅ Service/UI Boundary

- **Worker thread**: FreeRTOS task on Core 0 handles HTTP requests (8KB stack)
- **Event dispatch**: Service posts events to LVGL task via `lv_async_call()`
- **Ownership transfer**: PSRAM arrays (route lists, stop lists) passed to UI with 
  explicit free() responsibility
- **No blocking**: All network I/O on worker thread; LVGL task never blocks

**Validation**: No watchdog timeouts occurred during normal operation; only the 
deliberate infinite loop test triggered the 5-second task watchdog.

### ✅ Request Ownership and Stale Callback Protection

- **Generation counters**: App tracks `stop_request_id_`; ignores events that don't 
  match current request
- **Cancellation**: `bus_service_cancel_all()` aborts active HTTP client
- **Listener nulling**: `bus_service_set_listener(nullptr, nullptr)` at onDestroy 
  prevents callbacks to freed memory

**Validation**: Rapid tab switching (10 switches in 5 seconds) never crashed; stale 
route/stop events were silently discarded.

### ✅ Large Dataset Handling

- **943 KB stop catalogs**: KMB (6,909 stops) + CTB (2,158 stops) in binary format
- **A/B commit pattern**: Atomic swap between staging and committed generations
- **Memory-mapped lookup**: Binary search over SPIFFS files without loading into RAM
- **7-day cache**: Route catalog (1,048 routes, ~200 KB PSRAM) persists across reboots

**Validation**: Cold boot with catalogs present took 150-200ms to validate checksums; 
warm boot with valid cache skipped network fetch entirely.

### ✅ Multi-Provider Normalization

- **Provider-qualified identity**: `bus_route_variant_t` includes `op` (KMB/CTB) and 
  `service_type` fields
- **Unified schema**: KMB's "I"/"O" and CTB's "inbound"/"outbound" mapped to common 
  `bus_direction_t` enum
- **Parallel requests**: Both providers queried simultaneously; results merged in UI

**Validation**: Route "2" exists on both KMB and CTB; app correctly showed both 
variants and fetched stops from the selected provider.

## What Failed: Memory Constraints

### Failure Mode 1: Internal DRAM Exhaustion (Most Common)

**Symptom**: `E (5812) lwip_arch: thread_sem_init: out of memory` followed by crash  
**Internal DRAM remaining**: 719 bytes at point of failure

**Converging allocations**:
1. WiFi static RX buffers: 10 × 1600 bytes = 16,000 bytes
2. Bus app onCreate(): ~30 LVGL objects (Favorites tab + Search tab + Stop page upfront)
3. Catalog validation: `load_generations()` with 4KB stack buffer
4. Crystal OS HTTPS smoke test: triggered immediately after network connection
5. lwIP DNS resolution: attempted to allocate semaphore → **OOM**

**Root cause**: Multiple subsystems allocated from internal DRAM simultaneously during 
Calculator → Bus app transition. The 80ms onCreate budget is achievable for simple 
apps (Clock, Weather) but not for apps that build complex UI + validate large datasets 
upfront.

### Failure Mode 2: TLS Handshake Failures (Intermittent)

**Symptom**: `E (44752) esp-tls: [sock=54] select() timeout`  
**Internal DRAM drop**: 27KB → 3.5KB during handshake

**Root cause**: Despite `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`, some TLS allocations 
require internal DRAM. Successful handshakes need ~19KB internal DRAM; failures occur 
with <10KB. This happened during catalog sync when multiple concurrent allocations 
(JSON parsing, HTTP buffers, TLS state) competed for internal DRAM.

### Failure Mode 3: Scheduler Corruption (Rare)

**Symptom**: `Core 1 panic'ed (LoadProhibited)` in `prvSelectHighestPriorityTaskSMP`  
**Internal DRAM before crash**: 31KB (healthy)

**Root cause**: Unclear. Crash occurred during `load_generations()` in catalog sync 
worker. EXCVADDR=0x00000046 suggests null pointer dereference. Corrupted backtrace 
indicates possible stack overflow in 6144-byte worker stack or memory corruption from 
race condition.

### What Didn't Work: `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`

**Attempted fix**: Move WiFi buffers from internal DRAM to PSRAM  
**Result**: WiFi initialization failed; only 3/10 static RX buffers allocated  
**Outcome**: Device could not scan for networks; completely broken WiFi

**Lesson**: WiFi driver has hard internal DRAM requirements that cannot be worked 
around. Apps must fit within the DRAM budget after WiFi takes its 16KB.

## Architecture Gaps Identified

### 1. No Per-Instance Resource Ownership

**Problem**: `bus_service` uses static state:
```c
static TaskHandle_t s_worker_task = NULL;
static QueueHandle_t s_request_queue = NULL;
static bus_listener_t s_listener = NULL;
static bus_route_variant_t *s_route_variants = NULL;  // 200 KB PSRAM cache
```

**Impact**:
- Multiple app instances (not v1 requirement) would collide on global state
- Uninstalling the app doesn't release the worker task or queue
- No way to track "which resources belong to this app" for cleanup verification

**What Phase 14 needs**:
```c
// Per-app service handle
typedef struct bus_service_t* bus_service_handle_t;

bus_service_handle_t bus_service_create(const char *app_id);
void bus_service_destroy(bus_service_handle_t handle);  // blocks until worker exits

uint32_t bus_service_request_route(bus_service_handle_t handle, const char *route);
// All APIs take handle as first parameter
```

**Exit criterion**: After `bus_service_destroy()`, enumerate FreeRTOS tasks/queues 
and verify none are tagged with the app's ID.

### 2. No Memory Budget Enforcement

**Problem**: Service allocates freely:
- Route cache: 1,048 routes × 192 bytes = ~200 KB PSRAM (no quota check)
- Stop catalog: 943 KB on SPIFFS (no storage quota)
- Worker stack: 8192 bytes internal DRAM (not counted against app)

**Impact**: An app can exhaust PSRAM or flash without the OS detecting overcommit 
until an allocation fails at runtime.

**What Phase 14 needs**:
```yaml
# app.yaml
memory_kb: 256           # peak PSRAM usage
storage_kb: 1024         # persistent cache size
worker_threads: 1        # background task count
```

**Admission control at install time**:
```c
// Pseudo-code
bool can_install(manifest) {
    total_memory = sum(installed_apps.memory_kb) + manifest.memory_kb;
    total_storage = sum(installed_apps.storage_kb) + manifest.storage_kb;
    total_workers = count(installed_apps.worker_threads) + manifest.worker_threads;
    
    return total_memory <= available_psram() &&
           total_storage <= apps_partition_size() &&
           total_workers <= (CONFIG_FREERTOS_MAX_TASKS - os_reserved);
}
```

**Exit criterion**: Install correctly rejects package when resource sum exceeds limits.

### 3. No Permission Model

**Problem**: Service accesses URLs directly:
```c
#define KMB_BASE_URL "https://data.etabus.gov.hk/v1/transport/kmb/"
#define CTB_BASE_URL "https://rt.data.gov.hk/v2/transport/citybus/"
// No manifest declaration, no runtime check
```

**Impact**: An app can contact any HTTPS host; no user visibility into network access.

**What Phase 14 needs**:
```yaml
# app.yaml
permissions:
  network_hosts: ["data.etabus.gov.hk", "rt.data.gov.hk"]
```

**Runtime enforcement**:
```c
// In http.fetch implementation
esp_err_t crystal_http_fetch(const char *app_id, const char *url, ...) {
    if (!is_host_allowed(app_id, url)) {
        return ESP_ERR_NOT_ALLOWED;
    }
    // proceed with request
}
```

**Exit criterion**: `http.fetch` call to unlisted host returns error; logs show 
permission denial.

### 4. No Transactional Teardown Verification

**Problem**: `onDestroy()` unregisters listener and cancels requests, but:
- No verification that worker task exited
- No check for leaked LVGL objects
- No scan for orphaned NVS keys after uninstall

**Impact**: Silent resource leaks accumulate; uninstall leaves cruft in NVS.

**What Phase 14 needs**:
```c
typedef struct {
    uint16_t leaked_tasks;
    uint16_t leaked_timers;
    uint16_t leaked_lvgl_objects;
    uint32_t leaked_heap_bytes;
    char details[256];
} cleanup_report_t;

esp_err_t crystal_app_verify_cleanup(const char *app_id, cleanup_report_t *report);
```

**Phase 14 uninstall flow**:
1. Call `onDestroy()` and wait for return
2. Call `crystal_app_verify_cleanup(app_id, &report)`
3. If `report.leaked_tasks > 0`: log warning, force-disable app (don't delete folder)
4. If clean: delete `/apps/<app_id>/` and erase matching NVS keys
5. Prune registry row

**Exit criterion**: After uninstall, `ps` equivalent shows zero tasks tagged with 
app_id; NVS enumeration finds zero keys prefixed with `<app_id>:`.

### 5. No State Namespace Isolation

**Problem**: App writes NVS directly:
```c
if (!state().set(KEY_FAV_DATA, favorites_, favorites_count_ * sizeof(Favorite))) {
    ESP_LOGE(TAG, "Failed to save favorites");
}
```

**Impact**: Two apps using key "fav_dat" would collide. Deleting app folder leaves 
orphaned NVS keys.

**What Phase 14 needs**:
- For script apps: `crystal.state.set(k, v)` writes NVS key as `<app_id>:k`
- For bundled C++ apps: `CrystalState::set()` unchanged (backward compat)
- On uninstall: enumerate NVS, erase all keys matching `<app_id>:*`

**Exit criterion**: Install app, set `state.set("test", 42)`, uninstall, verify 
`nvs_get_i32(handle, "com.example.app:test", &val)` returns `ESP_ERR_NVS_NOT_FOUND`.

## Recommended onCreate Budget Policy

### Current: 80ms Hard Limit

**Works for**: Clock (50ms), Weather (65ms), Calculator (45ms)  
**Fails for**: Bus app (275-500ms)

### Proposed: Tiered Budget

| App Complexity | onCreate Budget | What Fits |
|----------------|-----------------|-----------|
| Simple | 80ms | Clock, Calculator, Settings |
| Standard | 200ms | Weather (network status check), basic data apps |
| Data-heavy | 500ms | Bus app (catalog validation), transit apps, rich dashboards |

**Manifest field**:
```yaml
startup_budget_ms: 200  # optional; defaults to 80
```

**Enforcement**: Log warning if exceeded in debug builds; no hard failure (same as current).

**Alternative: Lazy UI Construction**

Instead of relaxing budget, defer expensive work:
```cpp
bool BusApp::onCreate() {
    buildFavoritesTab();  // default view, always shown
    // Defer Search tab until first switch
    // Defer Stop page until first route selection
    // Defer catalog validation to onResume or background timer
    return true;
}

// First tab switch triggers:
void onTabChanged(int tab) {
    if (tab == 1 && search_tab_ == nullptr) {
        buildSearchTab();  // ~50ms one-time cost
    }
}
```

**Bus app impact**: onCreate drops from 275ms → ~120ms; Search tab builds on first 
access with imperceptible delay.

## Async HTTP Pattern (Validated)

The bus app's worker thread + async callback pattern is the right model for script apps.

### Architecture

```
┌─────────────┐
│  LVGL Task  │  (Core 1, script execution)
│  (UI only)  │
└──────┬──────┘
       │ http.fetch(url, cb)
       ▼
┌──────────────────┐
│  crystal_http    │  (host-managed worker thread)
│  Worker (Core 0) │
└──────┬───────────┘
       │ perform request
       │ parse response
       │ lv_async_call(cb, result)
       ▼
┌─────────────┐
│  LVGL Task  │
│  cb(id, result)  │  (script callback fires here)
└─────────────┘
```

### Request Lifecycle

1. **Script calls** `http.fetch(url, opts, cb)`
2. **Host validates** URL against manifest `network_hosts` allowlist
3. **Host checks** `worker_threads >= 1` in manifest; reject if zero
4. **Host assigns** `request_id`, stores `(request_id → app_instance)` mapping
5. **Host queues** request to worker thread
6. **Worker executes** HTTP request (may block on network)
7. **Worker posts** result via `lv_async_call(deliver_callback, result)`
8. **LVGL task** invokes script callback as `cb(request_id, status, body)`
9. **Script checks** `request_id` matches expected; discards if stale

### Cancellation

```lua
-- Script
local req = http.fetch("https://api.example.com/data", {}, function(id, status, body)
    print("Got response:", body)
end)

-- Later, user navigates away:
function onPause()
    http.cancel(req)  -- host aborts HTTP client, suppresses callback
end
```

### Host Responsibilities

- Track `request_id → app_instance` mapping
- On `onDestroy()`: cancel all app's requests, suppress racing callbacks
- Enforce `worker_threads` quota: queue or reject when limit reached
- Enforce `network_hosts` allowlist before opening socket
- Free response body after callback returns (script cannot hold references)

### Why Not Blocking Calls?

```lua
-- THIS WOULD HANG THE PANEL FOR 5+ SECONDS:
local body = http.fetch_sync("https://slow-api.example.com/data")
-- LVGL is not drawing during this wait → UI frozen → watchdog triggers
```

Script runs on LVGL task. Blocking that task means:
- No touch input processing
- No animation frames
- No timer callbacks
- 5-second watchdog timeout → reboot

Async-only is a hard requirement for script tier.

## Comparison: APK Model vs ESP32 Constraints

### How Android APK Works

1. **Dalvik/ART bytecode**: Apps compile to DEX bytecode, run in managed VM
2. **Process isolation**: Each app runs in separate Linux process with own memory space
3. **Binder IPC**: Apps call system services via Binder (remote procedure calls)
4. **Dynamic linking**: System libraries (libc, OpenGL) loaded at runtime
5. **Package Manager**: Tracks installed apps, enforces permissions, manages updates
6. **Zygote process**: Pre-warmed VM fork for fast app launch

### ESP32 Constraints

| Android Capability | ESP32 Reality | Crystal OS Approach |
|--------------------|---------------|---------------------|
| MMU memory protection | ❌ No MMU on Xtensa LX7 | Script sandbox with Lua allocator hook |
| Process isolation | ❌ Single address space | Handle-based ABI, no raw pointers to script |
| Dynamic linking | ❌ Xtensa ELF lacks PIC support (can load but risky) | Native tier = curated only; script tier = safe |
| 2+ GB RAM | ❌ 8 MB PSRAM | Upfront `memory_kb` quota, admission control |
| 64+ GB flash | ❌ 16 MB flash | Apps partition = 5.5 MB; install rejects overcommit |
| Multi-core scheduler | ✅ 2 cores, but LVGL = single-threaded | Worker threads for I/O only, not parallel UI |
| JIT compilation | ❌ No JIT on MCU | Lua bytecode interpreted; accept the perf hit |

### What Crystal OS Can Borrow from APK

1. **Manifest-declared permissions**: `AndroidManifest.xml` → `app.yaml`
2. **Package signing**: APK signature verification → `.capp` Ed25519 signature
3. **Install-time validation**: Check permissions, disk space, version compat
4. **Transactional install**: Atomic rename from staging to installed
5. **Centralized package manager**: One registry, reconcile at boot
6. **Resource cleanup on uninstall**: Delete folder + clear cached data

### What Crystal OS Cannot Borrow

1. **Hot code reload**: No JVM/ART, so updating app = delete + reinstall (acceptable)
2. **Background services**: No way to keep an app running when not visible (Phase 8 
   `onStop` defers this)
3. **Inter-app communication**: No Binder IPC; apps are isolated (acceptable for v1)
4. **System-wide content providers**: No shared SQLite databases between apps
5. **Rich permission dialogs**: 480×480 screen → show permissions at install, not runtime

## Third-Party App Distribution Model

### Phase 1: Sideload (Phases 14-15)

**Flow**:
1. Developer writes app, tests with `crystal run` over USB
2. Developer runs `crystal pack ./my_app` → `com.example.myapp-1.0.0.capp`
3. Developer signs with private key (dev mode) or submits for signing (production)
4. User connects device via USB or WiFi
5. User drags `.capp` to web panel → installs
6. App appears in launcher; user launches and tests
7. User can uninstall via Manage Apps

**Security**: 
- Dev mode: unsigned packages install with explicit warning
- Production: only signature-verified packages install

### Phase 2: Curated Catalog (Future)

**Flow**:
1. Device fetches `https://apps.crystalos.org/catalog.json` (signed)
2. Catalog lists 10-20 curated apps with descriptions, screenshots, package URLs
3. User browses catalog on-device (Manage Apps › Browse)
4. User taps "Install" → device downloads `.capp`, verifies signature, installs
5. Updates: device checks catalog weekly, shows "Update available" badge

**Catalog format**:
```json
{
  "schema": 1,
  "apps": [
    {
      "id": "com.example.clock3p",
      "name": "Clock",
      "version": "1.0.0",
      "author": "Example Co.",
      "description": "Simple clock with timer",
      "icon_url": "https://apps.crystalos.org/icons/clock3p.png",
      "package_url": "https://apps.crystalos.org/packages/com.example.clock3p-1.0.0.capp",
      "size_kb": 42,
      "min_os": "1.0.0"
    }
  ]
}
```

**Curation criteria**:
- Works on reference hardware
- Passes memory/storage budget checks
- No malicious network access
- Reasonable UX quality
- Signed by known developer key

### Phase 3: Open Repository (Further Future)

**Flow**:
1. Developer submits app to public repository (GitHub-style)
2. Automated build service compiles/packs for multiple targets
3. User adds custom repositories to device
4. Device merges curated + custom catalogs
5. Warnings for non-curated apps

**Not in v1-v2 scope**: This requires:
- Build infrastructure for arbitrary code
- Moderation/review process
- Developer accounts and key management
- Legal terms for user-submitted content

## Sizing: What Fits in 5.5 MB?

**Apps partition**: 5.5 MB LittleFS (from updated partition table)

**Overhead**:
- LittleFS metadata: ~2% (110 KB)
- Boot reconcile buffer: ~16 KB
- **Available for apps**: ~5.37 MB

**Realistic app sizes**:
- Simple apps (Clock, Calculator): 30-50 KB
- Standard apps (Weather): 80-120 KB
- Data-heavy apps (Bus): 1.2-1.5 MB (943 KB catalogs + 200 KB code/UI)

**Capacity estimates**:
- 100+ simple apps
- 40-60 standard apps
- 3-4 data-heavy apps
- **Realistic mixed install**: 10-15 apps (5 simple + 8 standard + 2 data-heavy)

**What doesn't fit**:
- Apps with >1 MB image assets (photo galleries, wallpaper packs)
- Apps requiring >2 MB persistent cache (full offline map tiles)
- Games with large sprite sheets

**Solution for large assets**: Stream from network when needed, don't bundle in package.

## Summary: What Phase 14 Must Deliver

1. **Per-app resource handles** — replace singletons with instance-based APIs
2. **Admission control** — reject install when resource sum exceeds capacity
3. **Transactional teardown** — verify zero leaks before marking app as destroyed
4. **Namespaced state** — NVS keys prefixed with `<app_id>:`; cleanup on uninstall
5. **Permission enforcement** — `network_hosts` allowlist checked at runtime
6. **Async HTTP pattern** — worker thread + callback, never block LVGL task
7. **onCreate budget policy** — tiered (80ms/200ms/500ms) or lazy UI construction

**Exit gate**: Convert Calculator to `.capp`, install, launch 10 times, uninstall, 
verify `crystal_app_verify_cleanup()` reports zero leaks. Repeat with dev-mode 
signature and production signature.

**Bus app's role**: Preserved at `examples/bus_app/` as the demanding reference 
workload. Use it to stress-test resource tracking, quota enforcement, and cleanup 
verification. If Calculator passes but Bus app leaks, the verification is incomplete.
