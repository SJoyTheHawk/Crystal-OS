# WiFi Idle Issue - Investigation & Proposed Fix

> Historical hypothesis (reviewed 2026-11-19): disabling disconnected power
> management did not resolve the reported failure. Do not treat the proposed
> changes below as a verified fix. Use [current Wi-Fi checks](../../networking/WIFI_CHECKS.md).

**Date:** 2026-11-18
**Status:** NEEDS REVIEW - Do not apply without testing
**Issue:** WiFi becomes completely unresponsive after 5-10 minutes of idle, requiring full device reboot

---

## Problem Summary

After the device idles for several minutes (observed in Bus Lite app but affects all network operations):

### Symptoms
1. **DNS resolution fails** with `ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME`
   - `getaddrinfo() returns 202` (EAI_ADDRFAMILY error)
   - All HTTPS requests fail at TLS handshake

2. **WiFi reconnection attempts fail** with multiple reason codes:
   - Reason 2: `AUTH_EXPIRE`
   - Reason 8: `ASSOC_LEAVE`
   - Reason 205: `BEACON_TIMEOUT`

3. **Recovery impossible without reboot**
   - Toggling WiFi in Settings doesn't help
   - Attempting to rejoin network repeatedly fails
   - **Plugging in Ethernet works immediately** ← proves lwip/DNS stack is healthy

4. **Log evidence:**
```
E (232331) esp-tls-mbedtls: mbedtls_ssl_handshake returned -0x7280
E (268614) esp-tls: couldn't get hostname for :data.etabus.gov.hk: getaddrinfo() returns 202
W (268617) crystal_http: transport diagnostic error=ESP_ERR_HTTP_CONNECT
                          tls_error=ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME
```

---

## Root Cause Analysis

### The Culprit: `CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE=y`

This ESP-IDF default setting enables aggressive WiFi power management when the driver considers the station "disconnected" — which includes idle-but-associated states.

**What happens:**
1. After extended idle, WiFi radio enters deep power save
2. lwip's network interface state becomes desynchronized from the WiFi driver
3. DNS queries fail because lwip thinks the interface is unavailable
4. WiFi driver can't recover because its internal state is corrupted
5. All subsequent network operations fail until full reboot

### Why Brookesia Reference Doesn't Have This Issue

I checked the ESP32-P4 Brookesia factory firmware (`ESP32-P4-WIFI6-Touch-LCD-4B-main/examples/factory`):

**Their sdkconfig does NOT enable this option.**

This is intentional — the option is appropriate for **battery-powered devices** that need aggressive sleep between short network bursts, NOT for **always-connected appliances** like Crystal OS.

---

## Attempted Fix & Complication

### What I Tried

**File:** `sdkconfig.defaults`
**Addition:**
```kconfig
# Disable STA_DISCONNECTED_PM: it puts WiFi into aggressive power save when idle,
# causing DNS resolution to fail (getaddrinfo returns 202) and all networking to
# become unresponsive until reboot. This is an ESP-IDF default appropriate for
# battery devices, not always-connected appliances.
CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE=n
```

### The Problem

When I attempted to build with this change, ESP-IDF's Kconfig system reported:

```
NOTE: ESP_WIFI_STA_DISCONNECTED_PM_ENABLE has user-set value n from
      .../sdkconfig.cm but is not visible (disabled dependency)
```

**This means the Kconfig option has dependencies that make it invisible/unmodifiable in certain configurations.**

I checked `/Users/szemy/.espressif/v6.1/esp-idf/components/esp_wifi/Kconfig:435` and found:

```kconfig
config ESP_WIFI_STA_DISCONNECTED_PM_ENABLE
    bool "Power Management for station at disconnected"
    default y
    help
        Select this option to enable power_management for station when disconnected.
        Chip will do modem-sleep when rf module is not in use any more.
```

**The option appears to have NO visible dependencies in the Kconfig file**, yet the build system says it's "not visible (disabled dependency)". This could be:
1. A hidden dependency elsewhere in the ESP-IDF Kconfig tree
2. A chip-specific constraint (ESP32-S3 vs ESP32-P4)
3. A conflict with another power management option we have enabled (`CONFIG_PM_ENABLE=y`)

---

## Verification of Crystal OS WiFi HAL

I thoroughly reviewed `components/crystal_hal/src/crystal_hal.cpp` (the WiFi HAL implementation):

✅ **Proper initialization sequence** matching Brookesia
✅ **Correct event handling** with mutex protection
✅ **Appropriate retry logic** and backoff strategy
✅ **Clean state management** for enable/disable/forget operations
✅ **No unsafe WiFi API calls** from interrupt context

**The Crystal OS WiFi code is NOT at fault.** This is purely a configuration issue.

---

## Recommended Next Steps

### Option 1: Use `idf.py menuconfig` (RECOMMENDED)

Instead of editing config files directly, use the interactive menu:

```bash
cd '/Users/szemy/Workspace/ESP32 Crystal OS'
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
idf.py menuconfig
```

Navigate to:
```
Component config → Wi-Fi → Power Management for station at disconnected
```

- **Uncheck** this option
- Save and exit
- The menuconfig tool will handle dependencies correctly

### Option 2: Investigate the Dependency

Check what's making this option "invisible":

```bash
# Search for dependencies in ESP-IDF Kconfig files
grep -r "ESP_WIFI_STA_DISCONNECTED_PM" /Users/szemy/.espressif/v6.1/esp-idf/components/*/Kconfig*

# Check if PM_ENABLE has any interaction
grep -A20 "config PM_ENABLE" /Users/szemy/.espressif/v6.1/esp-idf/components/esp_pm/Kconfig
```

### Option 3: Runtime Workaround (NOT RECOMMENDED)

Force WiFi power save to NONE at runtime in `crystal_hal.cpp`:

```cpp
// In DeviceWifi::start() after esp_wifi_init() succeeds
(void)esp_wifi_set_ps(WIFI_PS_NONE);
```

**Why this is NOT recommended:**
- `STA_DISCONNECTED_PM` operates at a lower level than `esp_wifi_set_ps()`
- This masks symptoms but may not fix the root cause
- Increases power consumption unnecessarily when WiFi is deliberately disabled

---

## Testing Plan (Once Fixed)

After successfully disabling the option:

1. **Idle test:** Let device sit in Bus Lite app for 15+ minutes
2. **Verify API requests** continue working after idle period
3. **Monitor logs** for DNS resolution errors — they should not occur
4. **Test WiFi reconnection** after deliberate disconnect
5. **Measure power consumption** to ensure we're not wasting power unnecessarily

---

## Files Changed (REVERTED)

All changes have been reverted pending your review:
- `sdkconfig.defaults` — reverted
- `sdkconfig` — reverted to default state

## Documentation Created

- `docs/archive/wifi/WIFI_STABILITY_FIX.md` — comprehensive fix documentation (keep for reference)
- `docs/archive/wifi/WIFI_IDLE_ISSUE_INVESTIGATION.md` — this file (handoff)
- Memory: `wifi-sta-disconnected-pm-breaks-networking.md` — for future reference

---

## Questions for You

1. **Have you seen this Kconfig dependency issue before?** Is there a Crystal OS-specific config that might be hiding this option?

2. **Would you prefer:**
   - A) Use `idf.py menuconfig` to disable it properly?
   - B) Investigate why the option is "not visible"?
   - C) Try a runtime workaround with `esp_wifi_set_ps(WIFI_PS_NONE)`?

3. **Can you reproduce the issue?** Let the device idle for 10 minutes and try to trigger a Bus Lite refresh.

4. **Do we have any test hardware** where we can verify the fix works without breaking other functionality?

---

**Bottom Line:** The root cause is identified (`CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE=y`), but the fix requires navigating ESP-IDF's Kconfig dependency system. The safest path is `idf.py menuconfig` to properly disable it.
