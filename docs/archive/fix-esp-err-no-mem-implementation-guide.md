# Fix ESP_ERR_NO_MEM - Implementation Guide

## Problem Summary

The ESP32-S3 crashes with `ESP_ERR_NO_MEM` in two scenarios:
1. **WiFi disable after retry exhaustion** - crashes in `phy_track_pll_init` when creating an esp_timer
2. **TLS handshake failures** - `mbedtls_ssl_setup` fails during HTTPS requests in the bus app

Both are caused by critically low internal DRAM (~8-12KB free), well below the ~18-20KB needed for reliable TLS operation.

## Root Cause Analysis

### Memory Pressure Points

1. **WiFi/LWIP buffers** - 64+ buffers (32 dynamic RX + 32 dynamic TX) consuming internal DRAM exclusively
2. **HTTP client buffers** - 1KB buffers preferring internal DRAM due to high `SPIRAM_MALLOC_ALWAYSINTERNAL` threshold
3. **TLS handshake** - temporary allocations during handshake need internal DRAM
4. **WiFi PHY** - shutdown requires additional internal DRAM for timer creation

### Current Memory Stats (from crash logs)

**Successful TLS connections:**
```
id=1: internal=19447 bytes at TLS peak → SUCCESS
id=2: internal=19543 bytes at TLS peak → SUCCESS
id=3: internal=8123 bytes at TLS peak → barely succeeded
```

**Failed TLS connections:**
```
id=4: internal=12283 bytes → ESP_ERR_NO_MEM (mbedtls_ssl_setup)
id=7: internal=10675 bytes → ESP_ERR_NO_MEM
id=8: internal=12451 bytes → ESP_ERR_NO_MEM
id=9: internal=10771 bytes → ESP_ERR_NO_MEM
```

**WiFi disable crash:**
```
After TLS: internal=17991 bytes
TLS peak: internal=10151 bytes
WiFi stop → phy_track_pll_init → esp_timer_create → ESP_ERR_NO_MEM
```

### Threshold Identified

TLS needs **~18-20KB internal DRAM minimum** to reliably complete handshakes. Current state drops to 8-12KB.

## Solution: Two-Part Config Change

### Part 1: Enable WiFi/LWIP PSRAM Allocation

**Impact**: Frees ~40-60KB internal DRAM by moving WiFi RX buffers to PSRAM

**Config Change:**
```
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=n  →  y
```

**Tradeoff**: Potential WiFi latency increase, but Crystal OS's retry logic handles transient issues gracefully.

### Part 2: Reduce Internal DRAM Threshold

**Impact**: Forces HTTP client 1KB buffers to PSRAM, saving ~2-4KB per request

**Config Change:**
```
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384  →  4096
```

**Tradeoff**: More PSRAM fragmentation, but PSRAM is abundant (8MB available).

### Part 3 (Optional Fallback): Reduce WiFi Buffer Counts

**Impact**: Saves additional ~20-30KB internal DRAM if Parts 1 & 2 insufficient

**Config Changes:**
```
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=32  →  16
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=32  →  16
```

**Tradeoff**: Lower throughput ceiling, but acceptable for periodic HTTP requests.

## Implementation Steps

### Step 1: Run menuconfig

```bash
cd /Users/szemy/Workspace/ESP32\ Crystal\ OS
idf.py menuconfig
```

### Step 2: Enable WiFi/LWIP PSRAM Allocation

**Navigation path:**
```
Component config
  → ESP PSRAM
    → Support for external, SPI-connected RAM
      → [*] Try to allocate memories of WiFi and LWIP in SPIRAM firstly
```

**Action**: Press `Y` to enable, then `Enter`

**Config symbol**: `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`

### Step 3: Reduce Internal DRAM Threshold

**Navigation path:**
```
Component config
  → ESP PSRAM
    → Support for external, SPI-connected RAM
      → Maximum malloc() size, in bytes, to always put in internal memory
```

**Current value**: `16384`  
**New value**: `4096`

**Action**: 
1. Press `Enter` to edit
2. Clear current value
3. Type `4096`
4. Press `Enter` to confirm

**Config symbol**: `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`

### Step 4 (Optional): Reduce WiFi Buffer Counts

Only proceed with this step if Steps 2-3 prove insufficient after testing.

**Navigation path for RX buffers:**
```
Component config
  → Wi-Fi
    → Max number of WiFi dynamic RX buffers
```

**Current value**: `32`  
**New value**: `16`

**Config symbol**: `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM`

**Navigation path for TX buffers:**
```
Component config
  → Wi-Fi
    → Max number of WiFi dynamic TX buffers
```

**Current value**: `32`  
**New value**: `16`

**Config symbol**: `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM`

### Step 5: Save Configuration

**Action**: 
1. Press `S` to save
2. Press `Enter` to confirm (saves to `sdkconfig`)
3. Press `Q` to quit menuconfig

### Step 6: Verify Changes in sdkconfig

```bash
grep -E "CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP|CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL" sdkconfig
```

**Expected output:**
```
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096
```

If you also reduced buffer counts:
```bash
grep -E "CONFIG_ESP_WIFI_DYNAMIC_(RX|TX)_BUFFER_NUM" sdkconfig
```

**Expected output:**
```
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=16
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=16
```

### Step 7: Clean Build

**Required** - ensures all components pick up new memory configuration:

```bash
idf.py fullclean
idf.py build
```

### Step 8: Flash Firmware

```bash
idf.py flash monitor
```

## Verification & Testing

### Phase 1: Boot and Basic Operation

**Monitor for:**
1. Clean boot without crashes
2. WiFi connects successfully
3. Weather fetch succeeds (first HTTPS request)
4. Bus app loads and displays routes

**Check heap stats in logs:**
```
Look for lines like:
I crystal_http: heap after id=X internal=XXXXX largest_internal=XXXX psram=XXXXXX
I crystal_http: heap tls-peak id=X internal=XXXXX largest_internal=XXXX psram=XXXXXX
```

**Expected improvement:**
- `internal` after TLS should be **>30KB** (was ~18KB)
- `tls-peak internal` should be **>25KB** (was ~10KB)

### Phase 2: TLS Stress Test

**Test scenario**: Rapidly switch between route variants in bus app to trigger multiple HTTPS requests

**Steps:**
1. Open bus app
2. Select a route (e.g., 101)
3. Quickly tap through different service types (1→2→3→1→2)
4. Monitor serial output for TLS handshakes

**Success criteria:**
- No `ESP_ERR_NO_MEM` errors
- All `mbedtls_ssl_setup` succeed
- HTTP requests complete with status=200
- `heap failure` logs show **internal >20KB**

**Previous failure pattern (should NOT see):**
```
E esp-tls-mbedtls: mbedtls_ssl_setup returned -0x008D
E esp-tls: create_ssl_handle failed
W crystal_http: transport diagnostic id=X error=ESP_ERR_NO_MEM
I crystal_http: heap failure id=X internal=12283  ← too low
```

### Phase 3: WiFi Retry Exhaustion Test

**Test scenario**: Force WiFi connection failures to trigger retry exhaustion and WiFi disable

**Steps:**
1. **Option A**: Move device far from WiFi AP (out of range)
2. **Option B**: Temporarily change WiFi password in router to mismatch device credentials
3. Wait for 10 retry attempts (~2-3 minutes)
4. Monitor serial output for WiFi disable sequence

**Success criteria:**
- WiFi retries exhaust after 10 attempts
- NVS write succeeds: `wifi_enabled old=1 requested=0 write=ok`
- **No crash** during `esp_wifi_stop()`
- Clean logs without `ESP_ERROR_CHECK failed`

**Previous failure pattern (should NOT see):**
```
W crystal_hal: WiFi retries exhausted; reconnecting in 1 minute
I crystal_hal: NVS set key=wifi_enabled old=1 requested=0 write=ok readback=ok
ESP_ERROR_CHECK failed: esp_err_t 0x101 (ESP_ERR_NO_MEM) at 0x420e5991
expression: esp_timer_create(&phy_track_pll_timer_args, &phy_track_pll_timer)
abort() was called at PC 0x40384e8b on core 0
```

### Phase 4: Regression Testing

**WiFi Connectivity:**
- Verify normal WiFi connection stability
- Monitor disconnection rate (should remain low)
- Check connection time (may increase slightly due to PSRAM buffers)

**HTTP Request Success Rate:**
- Bus stop queries should succeed consistently
- Weather updates should fetch without errors
- Target: **>95% success rate**

**App Performance:**
- Bus app transitions should remain smooth
- No noticeable UI lag
- Gesture responsiveness unchanged

### Expected Heap Stats After Fix

**Successful TLS connection (all requests should look like this):**
```
I crystal_http: heap before id=X internal=~35000-40000 ...
I crystal_http: TLS connected id=X
I crystal_http: heap after id=X internal=~30000-35000 ...
I crystal_http: heap tls-peak id=X internal=~25000-30000 ...  ← KEY METRIC
I crystal_http: request completed id=X ... status=200
```

**Key improvements:**
- `tls-peak internal`: **25-30KB** (was 8-12KB) ✅
- No `ESP_ERR_NO_MEM` during TLS ✅
- WiFi disable succeeds ✅

## Monitoring in Production

### Key Metrics to Watch

1. **Internal DRAM headroom**
   - Monitor `tls-peak internal` in crystal_http logs
   - Should stay **>20KB** consistently
   - Alert if drops below 15KB

2. **TLS handshake success rate**
   - Track `mbedtls_ssl_setup` failures
   - Target: **0% failures** due to ESP_ERR_NO_MEM

3. **WiFi stability**
   - Connection success rate
   - Disconnection frequency
   - Reconnection time

4. **HTTP request performance**
   - crystal_http success rate
   - Average request latency
   - Retry rate

### Debug Commands

**Check current heap state:**
```c
// Add to crystal_http.c or crystal_hal.cpp for debugging
size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
ESP_LOGI(TAG, "Heap check: internal=%u largest=%u psram=%u", 
         internal_free, internal_largest, psram_free);
```

**Check WiFi buffer allocation:**
```bash
# After boot, in serial monitor, check for:
# Should see PSRAM allocation messages if CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP works
grep -i "wifi.*spiram\|lwip.*spiram" <boot_log.txt>
```

## Rollback Plan

If the changes cause WiFi instability or other issues:

### Quick Rollback via menuconfig

```bash
idf.py menuconfig
# Navigate to CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP → disable
# Navigate to CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL → change back to 16384
# Save and rebuild
idf.py fullclean && idf.py build && idf.py flash
```

### Rollback via git

```bash
git checkout sdkconfig
idf.py fullclean
idf.py build
idf.py flash
```

### Alternative: Code-Based Workaround

If config changes cause unacceptable WiFi performance degradation, implement graceful WiFi disable deferral:

**File**: `components/crystal_hal/src/crystal_hal.cpp`  
**Location**: Line ~450 in `set_enabled(false)` block

```cpp
if (!enabled_) {
    pending_connect_ = false;
    (void)esp_wifi_scan_stop();
    scan_count_ = 0;
    
    // Check internal DRAM before WiFi stop to prevent PHY timer allocation failure
    const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (internal_free < 20480) {
        ESP_LOGW(TAG, "WiFi disable deferred: internal DRAM low (%u bytes)", 
                 (unsigned)internal_free);
        // WiFi stays nominally enabled but idle until next reboot
        // This prevents crash at cost of leaving WiFi subsystem active
        return;
    }
    
    (void)esp_wifi_disconnect(); 
    (void)esp_wifi_stop(); 
    notify(Disconnected);
}
```

**When to use**: Only if Parts 1-3 prove insufficient and crash persists after testing.

## References

### Memory Configuration Docs
- `[[mbedtls-internal-mem-alloc-ooms]]` - TLS PSRAM allocation pattern (already working)
- ESP-IDF PSRAM docs: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/external-ram.html

### Crash Locations
- WiFi PHY crash: `esp_phy/src/phy_common.c:126` in `phy_track_pll_init`
- WiFi disable path: `crystal_hal.cpp:444-452` in `set_enabled(false)`
- TLS setup failure: `mbedtls_ssl_setup` in esp-tls-mbedtls

### Critical Files Modified
- `sdkconfig` - main configuration (auto-generated from menuconfig)

### No Code Changes Required
This fix is **configuration-only** unless the optional code-based workaround is needed.

## Success Criteria Summary

✅ **Primary goal**: No more ESP_ERR_NO_MEM crashes  
✅ **TLS handshakes**: 100% success rate with internal DRAM >20KB  
✅ **WiFi disable**: Clean shutdown without crashes  
✅ **Performance**: WiFi and HTTP remain functional with acceptable latency  
✅ **Heap headroom**: Internal DRAM consistently >25KB during TLS operations

## Timeline

- **Implementation**: ~10 minutes (menuconfig + build)
- **Initial verification**: ~15 minutes (boot + basic tests)
- **Full validation**: ~1 hour (stress tests + retry exhaustion)
- **Monitoring period**: 24-48 hours in normal use

## Contact / Escalation

If issues persist after applying all three parts:
1. Capture full serial boot log with heap stats
2. Check for memory leaks in crystal_http or bus_service
3. Consider enabling `CONFIG_MBEDTLS_DYNAMIC_BUFFER` for additional savings
4. Review task stack sizes (LWIP, main task) for reduction opportunities
