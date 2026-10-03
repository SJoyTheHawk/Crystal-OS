# TLS Hardware AES Disable — Fix Procedure

**Date:** 2026-10-03  
**Status:** Ready to apply  
**Time required:** 30 minutes  

## Problem Summary

The ESP32-S3 hardware AES accelerator requires internal DMA-capable RAM for staging buffers (up to 1600 bytes). With only ~8KB internal RAM free during HTTPS operations, these allocations occasionally fail, causing TLS handshake errors:

- `esp-aes: Failed to allocate memory`
- `mbedtls_ssl_handshake returned -0x7F80` (MBEDTLS_ERR_SSL_HW_ACCEL_FAILED)
- `mbedtls_ssl_setup returned -0x008D` (PSA_ERROR_INSUFFICIENT_MEMORY)

The device has 3.5MB free PSRAM. Disabling hardware AES moves encryption to software using PSRAM buffers, eliminating the internal RAM constraint.

## Trade-offs

| Aspect | Hardware AES (current) | Software AES (proposed) |
|--------|------------------------|-------------------------|
| Internal RAM usage | 1600+ bytes per handshake | Minimal |
| PSRAM usage | TLS buffers only | TLS buffers + encryption |
| Reliability | Occasional failures | No allocation failures |
| TLS speed | Fastest (~2-3s handshake) | Slightly slower (~2.5-3.5s) |
| CPU usage | Lower | Higher during encryption |

**Recommendation:** Disable hardware AES. The reliability gain outweighs the small performance cost.

## Procedure

### Step 1: Check Current Configuration

```bash
cd /Users/szemy/Workspace/ESP32\ Crystal\ OS
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh

# Check if hardware AES is currently enabled
grep MBEDTLS_HARDWARE_AES sdkconfig
```

Expected output: `CONFIG_MBEDTLS_HARDWARE_AES=y` (or no line, which means enabled by default)

### Step 2: Disable Hardware AES

**Option A: Edit sdkconfig.defaults (recommended)**

```bash
# Open the file
nano sdkconfig.defaults

# Add this line at the end:
CONFIG_MBEDTLS_HARDWARE_AES=n
```

**Option B: Use menuconfig**

```bash
idf.py menuconfig
# Navigate to: Component config → mbedTLS → Use hardware AES acceleration
# Set to [N]
# Save and exit
```

### Step 3: Reconfigure and Build

```bash
idf.py reconfigure
idf.py build
```

Verify the setting was applied:
```bash
grep MBEDTLS_HARDWARE_AES sdkconfig
```

Expected output: `CONFIG_MBEDTLS_HARDWARE_AES=n`

Also check that external memory allocation remains enabled:
```bash
grep MBEDTLS_EXTERNAL_MEM_ALLOC sdkconfig
```

Expected output: `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`

### Step 4: Flash and Monitor

```bash
idf.py -p /dev/cu.usbmodem1201 flash monitor -b 2000000
```

### Step 5: Verify the Fix

**Test 1: Startup confirmation**

Look for this line in the boot log:
```
I (xxx) crystal_http: TLS: external_alloc=1 hw_aes=0
```

The `hw_aes=0` confirms software AES is active.

**Test 2: Route browsing (same as previous test)**

1. Open Bus app
2. Wait for route catalog to load
3. Search for route 10
4. Select CTB route 10 inbound → wait for stops to load
5. Go back, select CTB route 10 outbound → wait for stops to load
6. Search for route 101
7. Select KMB route 101 service type 1 → wait for stops to load
8. Select KMB route 101 service type 2 → wait for stops to load

**Success criteria:**
- ✅ All four stop lists load successfully (40, 37, 35, 22 stops)
- ✅ No `esp-aes: Failed to allocate memory` in logs
- ✅ No `-0x7F80` or `-0x008D` errors
- ✅ Automatic retries (if any) succeed on attempt 1 or 2
- ✅ Response times are acceptable (a few seconds, not 10+)

**Test 3: Catalog sync (if manifest URL is configured)**

1. Stay on Bus route list (don't select a route)
2. Wait for catalog sync to complete
3. Check logs for KMB and CTB validation

**Success criteria:**
- ✅ Both providers download and validate
- ✅ No TLS allocation errors during large transfers (685KB KMB, 258KB CTB)

### Step 6: Record Results

Create a verification note with:
- Date and time
- Build commit hash: `git rev-parse HEAD`
- Test routes browsed
- Number of framework requests attempted/succeeded
- Any retry attempts and their outcomes
- Internal heap minimum reported
- Confirmation: "No esp-aes allocation errors observed"

Example:
```
Date: 2026-10-03 14:30
Commit: abc1234
Tests: CTB 10 I/O, KMB 101 ST1/ST2
Requests: 8 attempted, 8 succeeded, 1 retry (attempt 2 success)
TLS internal heap min: 8459 bytes
Result: No esp-aes errors. Fix verified.
```

## Expected Log Pattern (After Fix)

**Before (with errors):**
```
E (12345) esp-aes: Failed to allocate memory
E (12346) esp-tls-mbedtls: mbedtls_ssl_handshake returned -0x7F80
E (12347) crystal_http: TLS error flags=0x8000 errno=0 ssl=-32640
```

**After (clean):**
```
I (12345) crystal_http: request id=5 owner=bus_svc attempt=1/3
I (12346) crystal_http: TLS connected elapsed=2847 ms
I (12347) crystal_http: response status=200 body=4812 bytes
```

## If Problems Persist

**Scenario 1: Still seeing allocation errors**

Verify the setting actually applied:
```bash
grep MBEDTLS_HARDWARE_AES sdkconfig
# Must show: CONFIG_MBEDTLS_HARDWARE_AES=n
```

If it shows `=y`, the reconfigure didn't work. Try:
```bash
rm sdkconfig
idf.py reconfigure
idf.py build
```

**Scenario 2: Much slower TLS (>5 seconds)**

This is acceptable but check:
- CPU frequency: should be 240MHz, not 80MHz
- Other tasks blocking: check if catalog sync is running simultaneously

**Scenario 3: Different errors appear**

If you see new error codes (not `-0x7F80` or `-0x008D`), those are separate network/timeout issues, not AES allocation. Document them separately.

## Rollback (if needed)

If software AES causes unacceptable performance:

```bash
# Edit sdkconfig.defaults
# Change: CONFIG_MBEDTLS_HARDWARE_AES=n
# To:     CONFIG_MBEDTLS_HARDWARE_AES=y

idf.py reconfigure
idf.py build
idf.py flash
```

But note: this returns you to the allocation failure problem.

## Commit Message

After verification passes:

```
Disable hardware AES to fix TLS allocation failures

Hardware AES requires internal DMA buffers that occasionally fail to
allocate when only ~8KB internal RAM is free. Software AES uses PSRAM
buffers instead, eliminating allocation failures at the cost of slightly
slower handshakes (~300ms increase).

Verified: CTB/KMB route browsing completes without esp-aes errors.

CONFIG_MBEDTLS_HARDWARE_AES=n
CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y (unchanged)
```

## Next Steps

After this fix is verified:
1. Commit the config change
2. Proceed to simplified Slice 4R.4-4R.6 (local lookup)
3. Then Slice 5-7 (picker, ETA, favorites)
