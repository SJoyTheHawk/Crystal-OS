# Bus App Memory Analysis - ESP32 Crystal OS

**Date**: 2026-10-04  
**Current HEAD**: fc44d47 — Fix Bus app startup stability and stop picker recovery  
**Status**: Analysis before implementing fix

## Executive Summary

The Bus app crashes during startup with internal DRAM exhaustion in three distinct patterns. The crashes occur because **multiple subsystems allocate from internal DRAM simultaneously** during the Calculator → Bus app transition. The primary failure (719 bytes remaining) happens when:

1. Bus app builds all UI upfront (Favorites tab, Search tab with full numeric + letter keypad, hidden Stop page)
2. Bus catalog sync loads and validates existing catalog files synchronously during `onCreate()`
3. WiFi stack initializes 10 static RX buffers (16KB each = 16KB total internal DRAM)
4. Crystal OS framework triggers HTTPS smoke test immediately after network connection
5. lwIP attempts to allocate a semaphore for DNS resolution → **OOM at 719 bytes**

**Critical finding**: The previous recommendation to enable `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` **breaks WiFi scanning completely**. After WiFi can only allocate 3/10 static RX buffers, initialization fails, and the device cannot scan for networks.

## Three Distinct Crash Patterns

### Pattern 1: lwIP Semaphore Allocation Failure (Most Common)

**Log**: `/Users/szemy/.codex/attachments/28d51913-345b-4231-aeba-f1e5f782b122/Pasted text.txt`

**Timeline**:
```
I (4184) crystal_shell: card crossover started: 4 -> 5, PSRAM free=4721492
I (5301) crystal_app: Bus lifecycle: onCreate
I (5574) crystal_app: Bus onCreate took 275 ms (budget 80 ms)
I (5653) esp_netif_handlers: sta ip: 192.168.1.193
I (5665) crystal_core: starting HTTPS smoke test
I (5670) crystal_http: request queued id=1
I (5806) crystal_http: heap before id=1 internal=719 largest_internal=512 psram=4718408
E (5812) lwip_arch: thread_sem_init: out of memory
Guru Meditation Error: Core 0 panic'ed (LoadProhibited)
```

**Root cause**: Internal DRAM drops to **719 bytes** before HTTPS smoke test. lwIP's `netconn_gethostbyname_addrtype_n` attempts to allocate a semaphore from internal DRAM → fails → null pointer dereference in `memcmp`.

**After reboot**:
```
W (4381) wifi:malloc buffer fail
I (4384) wifi:Init static rx buffer num: 3
E (4392) wifi:Expected to init 10 rx buffer, actual is 3
```

WiFi initialization fails because the previous crash consumed so much internal DRAM that even after reboot, only 3/10 static RX buffers can be allocated.

### Pattern 2: TLS Handshake Allocation Failure (Intermittent)

**Log**: `/Users/szemy/.codex/attachments/de5abef7-ecff-409a-87e7-990e4de00274/Pasted text.txt`

**Evidence**:
```
I (39724) crystal_http: heap before id=4 internal=27807 psram=2473368
E (44752) esp-tls: [sock=54] select() timeout
I (44781) crystal_http: heap failure id=4 internal=3503 largest_internal=3072 psram=1248004
I (78677) crystal_http: heap tls-peak id=4 internal=3491 psram=1248004

Successful TLS connections for comparison:
I (5804) crystal_http: heap before id=1 internal=29167 psram=4226472
I (13788) crystal_http: heap tls-peak id=1 internal=19091 psram=3630972
```

**Root cause**: During CTB catalog resolution, internal DRAM drops from 27KB → **3.5KB** during TLS handshake. Even though `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` is enabled, some TLS allocations require internal DRAM. Successful handshakes need ~19KB internal DRAM; failures occur with <10KB.

**Note**: This failure is **separate** from Pattern 1. It occurs during actual HTTP requests (catalog fetches, stop queries), not during the framework HTTPS smoke test.

### Pattern 3: FreeRTOS Scheduler Corruption (Rare, Different Root Cause?)

**Log**: `/Users/szemy/.codex/attachments/82ace513-3f13-44b0-bc2f-230da102bcaf/Pasted text.txt`

**Evidence**:
```
I (6412) crystal_app: Bus lifecycle: onCreate
I (6518) bus_catalog_sync: KMB has no valid committed generation
Guru Meditation Error: Core 1 panic'ed (LoadProhibited)
PC: 0x40385fad: prvSelectHighestPriorityTaskSMP
EXCVADDR: 0x00000046
Backtrace: |<-CORRUPTED

Before crash:
I (5945) crystal_http: heap before id=1 internal=31047 largest_internal=22528 psram=4720080
```

**Root cause**: **Unclear if memory-related**. Crash occurs in FreeRTOS task scheduler during catalog loading. Internal DRAM was at **31KB** before the crash, which is healthy. EXCVADDR=0x00000046 suggests null/near-null pointer dereference. Corrupted backtrace indicates stack corruption or memory corruption.

**Hypothesis**: This may be a **separate bug** unrelated to memory exhaustion. The crash timing (during `load_generations()`) and location (scheduler) suggest a race condition or stack overflow in the `bus_catalog_sync` worker (6144-byte stack) or synchronous catalog validation.

## Allocation Ownership Analysis

### Proven Allocations from Internal DRAM

**From crash logs and code inspection:**

1. **WiFi static RX buffers**: 10 × 1600 bytes = **16,000 bytes**
   - Config: `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=10`
   - Allocation: Internal DRAM only (not affected by `SPIRAM_TRY_ALLOCATE_WIFI_LWIP`)

2. **WiFi dynamic buffers**: Prefer internal DRAM when `SPIRAM_TRY_ALLOCATE_WIFI_LWIP=n`
   - Config: `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=32`, `DYNAMIC_TX_BUFFER_NUM=32`
   - Actual allocation unknown, but dynamic buffers allocate on-demand

3. **Bus app LVGL UI objects**: **Unknown exact size**
   - `buildFavoritesTab()`: ~7 LVGL objects (container, list, status label, etc.)
   - `buildSearchTab()`: ~6 base objects + keypad
   - `buildKeypad()`: ~23 objects (9 numeric buttons, 3 command buttons, 10+ letter buttons, scrollable container)
   - `buildStopPage()`: ~5 objects (container, back button, title, list)
   - **Total**: ~40-45 LVGL objects created immediately in `onCreate()`
   - LVGL uses `CONFIG_LV_MEM_CUSTOM=y` with stdlib `malloc/free`, which defaults to internal DRAM for small allocations

4. **Bus catalog validation (synchronous)**: **4,096-byte stack buffer**
   - Location: `bus_catalog_sync.c:296` in `sha256_file()`
   - Called from: `load_generations()` → `commit_valid()` → `sha256_file()`
   - Timing: **Runs synchronously during `bus_catalog_sync_init()`**, which is called from `bus_service_init()` during `BusApp::onCreate()`
   - Stack usage: `sha256_file()` allocates `unsigned char buffer[4096]` on the stack
   - Impact: This 4KB buffer lives on the **main task stack** (or LVGL task stack if called from there), not the bus_catalog_sync worker (which hasn't started yet)

5. **Bus service worker task**: 8192-byte stack (internal DRAM)

6. **Bus catalog sync worker task**: 6144-byte stack (internal DRAM, but **not yet created** at crash time)

7. **lwIP semaphores and DNS state**: Size unknown, but semaphore allocation failing suggests **<100 bytes remaining**

### Unproven / Speculative Allocations

- **LVGL memory pool**: Total size unknown; individual object sizes unmeasured
- **HTTP client buffers**: 1KB buffers affected by `SPIRAM_MALLOC_ALWAYSINTERNAL`, but threshold=16384 means these prefer internal DRAM but fall back to PSRAM
- **TLS temporary allocations**: Some handshake structures need internal DRAM despite `EXTERNAL_MEM_ALLOC=y`
- **Task stacks for LVGL, main, HTTP worker**: Sizes configured, but actual usage unmeasured

## Why CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y Breaks WiFi

**User feedback**: "right after the edit, I cannot scan WiFi, it shows No networks found. Only after idling for a long while, the WiFi scan page appears again."

**Analysis from ESP-IDF Kconfig**:

```
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM:
  default 10 if !(SPIRAM_TRY_ALLOCATE_WIFI_LWIP && !SPIRAM_IGNORE_NOTFOUND)
  default 16 if (SPIRAM_TRY_ALLOCATE_WIFI_LWIP && !SPIRAM_IGNORE_NOTFOUND)
```

**Problem**: When `SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`, ESP-IDF expects to allocate **dynamic RX buffers from PSRAM**. However:

1. Static RX buffers **cannot** be allocated from PSRAM (DMA requirement)
2. The config increases the static buffer count from 10 → 16, expecting PSRAM dynamic buffers to compensate
3. **But**: If PSRAM allocation fails or is slow, WiFi initialization hangs or fails
4. The "No networks found" symptom suggests WiFi scanning cannot proceed because buffer allocation is incomplete or deadlocked

**Evidence from crash log after reboot with TRY_ALLOCATE_WIFI_LWIP=n**:
```
W (4381) wifi:malloc buffer fail
I (4384) wifi:Init static rx buffer num: 3
E (4392) wifi:Expected to init 10 rx buffer, actual is 3
```

Even with PSRAM disabled, only 3/10 static buffers can be allocated when internal DRAM is critically low.

**Conclusion**: `SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` is **not appropriate for this board/firmware** because:
- The timing of Bus app UI allocation + catalog validation leaves insufficient internal DRAM for WiFi static buffers
- PSRAM dynamic buffer allocation either fails or introduces unacceptable latency
- WiFi scanning becomes unreliable or impossible

## Why CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096 Doesn't Help

**Previous claim** (in fix-esp-err-no-mem-implementation-guide.md):
> "Forces HTTP client 1KB buffers to PSRAM, saving ~2-4KB per request"

**User correction**:
> "ALWAYSINTERNAL=4096 does NOT move 1 KB HTTP buffers to PSRAM. It changes allocation preference for larger ordinary allocations, with fallback."

**Analysis**:
- `SPIRAM_MALLOC_ALWAYSINTERNAL` sets the threshold above which `malloc()` **prefers** PSRAM
- Allocations **below** this threshold prefer internal DRAM **but fall back to PSRAM if internal exhausted**
- HTTP client 1KB buffers (below 4096 threshold) would **still prefer internal DRAM first**
- Reducing from 16384 → 4096 does **not force** small allocations to PSRAM
- The claimed "2-4KB savings" is **unsubstantiated**

**Why it was ineffective**:
- The crash happens when internal DRAM is already at 719 bytes
- Preference changes don't matter when the heap is already exhausted
- TLS handshake failures occur at 3-10KB remaining, still well below both thresholds

## Smallest Justified Fix

### Recommended Approach: Lazy UI Construction

**Rationale**: The Bus app creates **all UI components upfront** during `onCreate()`, including:
- A full numeric keypad (12 buttons)
- A scrollable letter keypad (10+ buttons)
- An entire hidden Stop page that isn't visible yet

This eager construction happens **during the same 275-500ms window** when catalog validation is running synchronously and WiFi is initializing.

**Proposed change**: Defer Search tab keypad and Stop page construction until first use.

**Implementation**:

1. **Move keypad construction** from `onCreate()` → `onTabChanged()` when Search tab is first selected
2. **Move Stop page construction** from `onCreate()` → `showStopPage()` when first route is selected
3. **Keep Favorites tab eager** since it's the default view

**Expected impact**:
- Saves allocation of **~25-30 LVGL objects** during onCreate()
- Estimated savings: **5-10KB internal DRAM** (unverified, needs measurement)
- onCreate() time reduces from 275-500ms → ~150-250ms
- Search/Stop pages still build instantly on first use (typical LVGL object creation is <50ms)

**Tradeoffs**:
- First Search tab switch may show brief delay (acceptable for prototype)
- Adds small complexity to lifecycle management
- Does NOT address TLS handshake failures (Pattern 2) or scheduler crash (Pattern 3)

### Alternative: Defer Catalog Validation

**Current behavior**: `bus_catalog_sync_init()` calls `load_generations()` **synchronously**, which:
- Opens catalog files
- Reads commit metadata
- Calls `sha256_file()` with 4096-byte stack buffer
- Validates file integrity

**Proposal**: Move `load_generations()` to the worker task, called asynchronously after `onCreate()` returns.

**Expected impact**:
- Removes 4KB stack buffer from onCreate() execution
- Catalog validation completes in background without blocking UI
- Bus app starts faster

**Tradeoffs**:
- Catalog readiness delayed by ~100-200ms
- Search keypad must remain locked until validation completes (already implemented)
- Requires refactoring `bus_catalog_sync_init()` to defer validation

**Why this is riskier**:
- Touches shared catalog sync logic used by both apps
- May introduce race conditions if other code assumes catalogs are validated at init time
- Harder to test comprehensively

### NOT Recommended: Reduce WiFi Buffer Counts

**Proposal**: `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=32` → `16`

**Why NOT recommended**:
- WiFi buffers are sized for **normal operation**, not just Bus app startup
- Other apps (Weather, future network apps) may need higher throughput
- This is a **system-wide regression** to patch one app's startup issue
- Does not align with "preserve the OS foundation" requirement

## Recommended Fix

### Phase 1: Lazy UI Construction (Lowest Risk)

**File**: `components/bus_app/src/bus_app.cpp`

**Changes**:

1. **onCreate()** (line 183-185):
   ```cpp
   // BEFORE:
   buildFavoritesTab(width, height, tab_bar_height);
   buildSearchTab(width, height, tab_bar_height);  // Eager
   buildStopPage(width, height);                    // Eager
   
   // AFTER:
   buildFavoritesTab(width, height, tab_bar_height);
   // Defer Search tab and Stop page - see onTabChanged() and showStopPage()
   ```

2. **onTabChanged()** (new logic):
   ```cpp
   // When switching to Search tab (index=1), build keypad if not yet created
   if (active_tab == 1 && search_tab_ == nullptr) {
       buildSearchTab(width, height, tab_bar_height);
   }
   ```

3. **showStopPage()** (line 543, add guard):
   ```cpp
   if (stop_page_ == nullptr) {
       buildStopPage(width, height);
   }
   // ... existing showStopPage logic
   ```

**Verification**:
1. Boot device, Bus app starts on Favorites tab
2. **Measure heap** immediately after onCreate() returns (add log)
3. Switch to Search tab → keypad builds on first switch
4. Select route → Stop page builds on first show
5. **Compare internal DRAM** before/after: expect +5-10KB headroom
6. Run HTTPS smoke test → should not crash with lwIP OOM

### Phase 2: Measure Allocation Sizes (Validation)

**Add heap logging**:

```cpp
// In BusApp::onCreate(), before each build call:
size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
buildFavoritesTab(width, height, tab_bar_height);
size_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
ESP_LOGI(TAG, "buildFavoritesTab consumed %d bytes internal DRAM", before - after);
```

**Purpose**: Verify actual LVGL allocation sizes and validate that lazy construction provides sufficient headroom.

### Phase 3 (If Needed): Async Catalog Validation

Only proceed if Phase 1 proves insufficient.

**File**: `components/bus_service/src/bus_catalog_sync.c`

**Change**: Move `load_generations()` call from `bus_catalog_sync_init()` → `worker()` task first action.

**Impact**: Adds 200-300ms delay to catalog readiness, but frees 4KB during onCreate().

## Device Test Plan

### Test 1: Clean Boot Sequence

**Steps**:
1. Flash firmware with lazy UI construction
2. Hard reset device
3. Wait for Bus app to load (should default to Favorites tab)
4. **Monitor serial output** for:
   - onCreate() duration (target: <200ms)
   - Heap stats after onCreate()
   - HTTPS smoke test result (should not crash)
   - WiFi initialization success (10/10 static RX buffers)

**Success criteria**:
- onCreate() completes without crash
- HTTPS smoke test passes
- Internal DRAM after onCreate() >20KB
- WiFi buffers initialize fully

### Test 2: Search Tab First Use

**Steps**:
1. From Favorites tab, tap Search tab
2. **Monitor** for keypad construction time
3. Enter route number (e.g., "101")
4. Verify route search works

**Success criteria**:
- Search tab appears instantly (<100ms delay)
- Keypad is functional
- Route search returns results

### Test 3: Stop Page First Use

**Steps**:
1. Search for route "101"
2. Select a route variant
3. **Monitor** for Stop page construction
4. Verify stop list displays

**Success criteria**:
- Stop page appears instantly
- Stop list is functional

### Test 4: TLS Handshake Stress Test

**Steps**:
1. Trigger multiple HTTPS requests in rapid succession:
   - Switch between route variants (CTB routes preferred, they trigger TLS reconnections)
   - Tap different stops
   - Wait for Weather app background refresh

**Monitor**:
- `crystal_http: heap tls-peak` logs
- Any ESP_ERR_NO_MEM during mbedtls_ssl_setup

**Success criteria**:
- All TLS handshakes succeed
- `tls-peak internal` stays >15KB
- No ESP_ERR_NO_MEM failures

### Test 5: Catalog Validation (If Phase 3 Applied)

**Steps**:
1. Boot device with async catalog validation
2. Immediately switch to Search tab
3. **Verify** keypad remains locked until catalog loads
4. Wait for "Fetching route data..." overlay to disappear
5. Enter route number

**Success criteria**:
- Search keypad locks until validation completes
- Catalog loads within 500ms of onCreate()
- Route search works after unlock

### Test 6: WiFi Retry Exhaustion (Regression Test)

**Purpose**: Verify Pattern 1 fix doesn't reintroduce WiFi disable crash

**Steps**:
1. Move device out of WiFi range OR change WiFi password to force failures
2. Wait for 10 retry attempts (~3 minutes)
3. **Monitor** WiFi disable sequence

**Success criteria**:
- WiFi retries exhaust cleanly
- `esp_wifi_stop()` succeeds without ESP_ERR_NO_MEM
- No crash during PHY shutdown

## Tradeoffs and Risks

### Lazy UI Construction

**Pros**:
- Smallest code change (< 20 lines)
- Preserves OS foundation (no config changes)
- Reduces onCreate() time (user-visible improvement)
- Aligns with Android lifecycle best practices (defer non-visible work)

**Cons**:
- First Search tab switch shows brief construction delay (acceptable for prototype)
- Does not fully solve TLS handshake failures (Pattern 2)
- Does not address scheduler crash (Pattern 3, likely separate bug)

**Risk level**: Low

### Async Catalog Validation

**Pros**:
- Frees 4KB during critical onCreate() window
- Improves app startup latency

**Cons**:
- Requires refactoring shared catalog sync logic
- May introduce race conditions
- Harder to test comprehensively
- Search tab delay increases from immediate → ~200-300ms

**Risk level**: Medium

### NOT Reducing WiFi Buffers

**Why avoided**:
- System-wide regression for one app's issue
- Violates "preserve OS foundation" requirement
- WiFi buffers are sized for normal throughput, not startup edge case
- May cause subtle WiFi reliability issues in other apps

## Unresolved Questions

1. **Pattern 3 scheduler crash**: Is this memory-related or a separate race condition?
   - EXCVADDR=0x00000046 suggests null/near-null pointer
   - Corrupted backtrace suggests stack overflow or memory corruption
   - Occurs during `load_generations()` synchronous call
   - Requires further investigation with stack usage monitoring

2. **Actual LVGL allocation sizes**: How much internal DRAM do 40+ UI objects consume?
   - Needs instrumentation (heap snapshots before/after each build call)
   - LVGL `lv_obj` base size is typically 100-200 bytes + style overhead
   - Estimated total: 8-15KB for all UI objects, but **unverified**

3. **TLS handshake minimum internal DRAM**: Why do some handshakes succeed at 19KB but fail at 10KB?
   - `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` should move TLS buffers to PSRAM
   - Some temporary allocations (esp-tls connection state?) still need internal DRAM
   - Fragmentation may play a role (largest_internal often <8KB during failures)

4. **WiFi PSRAM allocation behavior**: Why does `TRY_ALLOCATE_WIFI_LWIP=y` break scanning?
   - ESP-IDF documentation warns of performance impact
   - Likely: PSRAM dynamic buffer allocation fails or deadlocks during scan
   - Needs deeper investigation into ESP-IDF WiFi PSRAM allocation path

## References

### Crash Logs
- Primary lwIP OOM: `/Users/szemy/.codex/attachments/28d51913-345b-4231-aeba-f1e5f782b122/Pasted text.txt`
- TLS handshake failures: `/Users/szemy/.codex/attachments/de5abef7-ecff-409a-87e7-990e4de00274/Pasted text.txt`
- Scheduler crash: `/Users/szemy/.codex/attachments/82ace513-3f13-44b0-bc2f-230da102bcaf/Pasted text.txt`

### Critical Files
- Bus app: `components/bus_app/src/bus_app.cpp` (lines 138-195 onCreate, 183-185 UI building)
- Catalog sync: `components/bus_service/src/bus_catalog_sync.c` (line 588 init, line 355 load_generations, line 293 sha256_file)
- HTTPS smoke test: `components/crystal_core/src/crystal_core.cpp` (line 70)

### Configuration
- WiFi buffers: `sdkconfig` lines 2155-2157
- PSRAM allocation: `sdkconfig` line 1887-1888
- LVGL memory: `CONFIG_LV_MEM_CUSTOM=y` (stdlib malloc/free)
- TLS memory: `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`, `CONFIG_MBEDTLS_HARDWARE_AES=n`

### Memory Documentation
- `[[mbedtls-internal-mem-alloc-ooms]]` - TLS PSRAM allocation pattern
- `[[reserve-space-never-fill-it]]` - Crystal OS UI design principles
- `[[crystal-os-v1-architecture-rules]]` - App lifecycle and threading model

## Next Steps

1. **Implement Phase 1** (lazy UI construction) and **measure** heap impact
2. **Run device tests** 1-4 to verify fix addresses Pattern 1 crash
3. **If insufficient**, implement Phase 3 (async catalog validation)
4. **Investigate Pattern 3** separately (scheduler crash) - may be unrelated to memory
5. **Monitor Pattern 2** (TLS handshake failures) in production - may require separate fix

**Do NOT**:
- Enable `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` (breaks WiFi scanning)
- Reduce `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` to 4096 (ineffective, unsubstantiated savings)
- Reduce WiFi buffer counts (system-wide regression)

---

**Analysis completed**: 2026-10-04  
**Recommendation**: Proceed with Phase 1 (lazy UI construction) only after user approval
