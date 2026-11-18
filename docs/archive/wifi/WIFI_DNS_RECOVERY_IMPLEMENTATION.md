# WiFi DNS Recovery Implementation

> Archived: this monitor was disabled after false-positive recovery/reboots.
> The socket-leak attribution and reboot-only recovery claims below were not
> verified. Follow [current Wi-Fi checks](../../networking/WIFI_CHECKS.md); see the
> [archive summary](README.md) for the status of these hypotheses.

**Date:** 2026-11-19  
**Status:** DISABLED - Proactive monitoring caused false positives  
**Original issue:** ESP-IDF bug - esp-tls socket leak on select() timeout

## Implementation Status: DISABLED

The DNS health monitoring implementation was **disabled on 2026-11-19** after discovering it causes false positive reboots.

### Problem

The implementation probed `connectivity-check.espressif.com` to detect DNS failures, but this hostname may be blocked or unreachable in some networks (e.g., mainland China, corporate firewalls, etc.). This caused the monitor to detect "DNS failure" even when DNS was working perfectly fine for actual API endpoints.

### Evidence from Testing

Logs showed:
```
W (62981) crystal_hal: DNS health check: getaddrinfo returned 202 (EAI_ADDRFAMILY)
W (62981) crystal_hal: DNS health check failed (1/3)
...
I (76008) crystal_http: request queued id=5 owner=1
I (76376) crystal_http: body received id=5 bytes=375
I (76381) crystal_http: request completed id=5 attempts=1 elapsed=373 ms status=200
...
W (122985) crystal_hal: DNS health check: getaddrinfo returned 202 (EAI_ADDRFAMILY)
W (122985) crystal_hal: DNS health check failed (2/3)
```

**HTTPS requests to data.gov.hk were succeeding** (status 200, TLS connected, body received) while the health check was failing. This proves DNS was working - the probe hostname was just unreachable.

### Why the Approach Was Wrong

1. **Arbitrary probe hostname** - Testing a specific hostname doesn't prove DNS is broken; it might just be that hostname that's unreachable
2. **Regional network differences** - What works in one region (e.g., espressif.com in US/Europe) may be blocked in others (China)
3. **False recovery trigger** - System would reboot unnecessarily when nothing was actually wrong

### Current Status

DNS health monitoring is **commented out** in `crystal_hal_init()`:

```cpp
// DNS health monitoring DISABLED - caused false positive reboots
// The probe hostname was unreachable in some networks, triggering recovery
// even when DNS was working fine for actual API endpoints.
// start_dns_health_monitor();
```

The monitoring code remains in place but is not active.

## What We Learned

The original ESP-IDF socket leak bug investigation found:

## What We Learned

### Original Investigation (Valid Findings)

The original ESP-IDF socket leak bug investigation found:

1. **Pattern exists:** `[sock=XX] select() timeout` followed by `getaddrinfo error 202`
2. **WiFi cycle doesn't work:** Socket leak persists in lwip layer across WiFi reconnection
3. **Only reboot clears it:** System restart is the only recovery method
4. **Not a PM settings issue:** Waveshare reference has PM enabled and works fine

These findings remain valid - **if the socket leak bug actually occurs**, only a reboot will fix it.

### What Went Wrong with the Workaround

The proactive monitoring approach failed because:

1. **Can't reliably detect DNS corruption externally** - Testing arbitrary hostnames gives false positives
2. **Regional network differences matter** - espressif.com may be blocked in China but work elsewhere
3. **Actual bug may be rare** - We never confirmed the socket leak happens frequently enough to warrant monitoring

## Alternative Approaches (Future Consideration)

If the ESP-IDF socket leak bug proves to be a persistent problem, better detection methods:

### 1. Passive Monitoring (Recommended)

Monitor `crystal_http` request failures for the specific pattern:
- Track consecutive DNS resolution failures (error 202) for **actual API endpoints**
- Only trigger recovery when apps are reporting failures
- Don't probe arbitrary hostnames

Implementation would be in the HTTP client layer:
```cpp
static uint8_t consecutive_dns_failures = 0;

void crystal_http_handle_error(esp_err_t err, const char *hostname) {
    if (is_dns_error_202(err)) {
        consecutive_dns_failures++;
        ESP_LOGW(TAG, "DNS failure for %s (%u/3)", hostname, consecutive_dns_failures);
        
        if (consecutive_dns_failures >= 3) {
            ESP_LOGE(TAG, "Multiple DNS failures detected, system restart required");
            esp_restart();
        }
    } else {
        consecutive_dns_failures = 0;  // Reset on other errors
    }
}
```

Benefits:
- Only reacts to real failures
- Tests actual hostnames the user cares about
- No false positives from probe hostnames

### 2. Manual Recovery (Simplest)

Add a "Restart Device" button in Developer Tools or Ethernet Info. Users can manually restart if they experience persistent network failures.

Benefits:
- No automatic false positives
- User confirms the problem exists before restarting
- Simplest implementation

### 3. Wait for ESP-IDF Fix

The socket leak may already be fixed in newer ESP-IDF versions. Before implementing any recovery:
1. Search ESP-IDF GitHub for related issues
2. Check if upgrading ESP-IDF resolves it
3. Test under signal loss conditions for several days

## Removed Implementation Details

The original implementation added ~140 lines to `crystal_hal.cpp`:
- `DnsHealthMonitor` struct
- `dns_probe_check()` function
- `dns_recovery_reboot()` function  
- `dns_health_monitor_task()` background task
- `start_dns_health_monitor()` initialization

All remain in the code but are disabled. To completely remove them, search for "DnsHealthMonitor" and delete those sections.

## Testing Results

### False Positive Confirmed (2026-11-19)

User tested the implementation and reported:

### False Positive Confirmed (2026-11-19)

User tested the implementation and reported:

> "I asked sonnet to implement the recovery plan to check dns health check, but it keeps doing wrong implementation. From the log, you could see that, the ETA api endpoint is established. The health check keeps failing, and eventually made me reboot, even the connection has no problem."

**Logs showed:**
- DNS health check failing with error 202
- **Same time:** HTTPS requests succeeding (status 200, TLS connected, body received)
- Endpoints: `data.gov.hk` (Hong Kong transit API)
- Result: System would reboot unnecessarily after 3 failed checks (~3 minutes)

**Root cause:** The probe hostname `connectivity-check.espressif.com` was unreachable in the user's network, but actual API endpoints worked fine. This proves DNS was functioning - only the probe was failing.

## Recommendation

**Do not re-enable proactive DNS monitoring** without addressing the false positive issue.

If the ESP-IDF socket leak bug becomes a frequent problem:
1. Implement **passive monitoring** (track actual app request failures, not probe requests)
2. Add **manual restart** option in Developer Tools
3. Or **upgrade ESP-IDF** to a version where the bug is fixed

For now, the monitoring remains disabled to prevent false positive reboots.

## Files Modified

- `components/crystal_hal/src/crystal_hal.cpp`
  - DNS health monitoring code remains but is **commented out** in init
  - Can be completely removed if not needed

## Related Documentation

- [Bug Report](WIFI_DNS_SOCKET_LEAK_BUG.md) - Original investigation (findings still valid)
- [Enhancement Plan](README.md#disabled-dns-monitor) - Original plan (approach was flawed)
- [WiFi Stability](WIFI_STABILITY_FIX.md) - Root cause analysis
- [WiFi Checks](../../networking/WIFI_CHECKS.md) - Current recommended validation approach

---

**Status as of 2026-11-19:**
- Monitoring: **DISABLED**
- Reason: False positive reboots
- Action: No automatic recovery - manual restart if needed
- Future: Consider passive monitoring if bug proves frequent