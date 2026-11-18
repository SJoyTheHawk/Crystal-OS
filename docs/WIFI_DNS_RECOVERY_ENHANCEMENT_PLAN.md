# WiFi DNS Recovery Enhancement Plan

> Investigation status (2026-11-19): the socket-leak explanation and upstream
> attribution below are unverified hypotheses. No socket-lifetime evidence or
> confirmed upstream issue is recorded here. Recovery proposals are not
> implemented policy. Use [current Wi-Fi checks](WIFI_CHECKS.md) for validation.

**Date:** 2026-11-19
**Issue:** ESP-IDF bug causes permanent DNS failure after `select()` timeout during TLS handshake
**Goal:** Implement application-level detection and recovery until ESP-IDF fixes the underlying bug

## Problem Statement

ESP-IDF's esp-tls component fails to call `esp_tls_conn_destroy()` when `select()` times out during TLS handshake, causing socket leaks that corrupt the lwip DNS resolver. Once corrupted, DNS returns `getaddrinfo() error 202` (EAI_ADDRFAMILY) for all queries, and the system cannot recover without a reboot.

**Root cause:** ESP-IDF bug in esp_http_client or esp-tls component
**Impact:** Permanent network failure after signal loss during active HTTPS requests
**Upstream status:** Unknown if fixed in latest ESP-IDF versions

## Constraints

1. **Cannot fix the socket leak** - it occurs inside ESP-IDF's esp-tls before control returns to Crystal OS
2. **Cannot prevent the trigger** - signal loss during HTTPS requests is normal operation
3. **Cannot detect the leak directly** - no API to query open socket count or leaked sockets
4. **Must maintain user experience** - automatic recovery should be seamless

## Enhancement Strategy

Implement **DNS health monitoring with automatic system recovery** as a defensive measure until ESP-IDF fixes the bug.

### Phase 1: DNS Health Detection

**Goal:** Detect when DNS resolver has failed

**Implementation:**

1. **Add DNS health probe in network monitoring task**
   - Perform periodic DNS lookup of a known-good hostname (e.g., `connectivity-check.espressif.com`)
   - Run every 60 seconds when WiFi connected
   - Timeout: 5 seconds
   - Low priority task (runs in background)

2. **Failure criteria:**
   - `getaddrinfo()` returns 202 (EAI_ADDRFAMILY)
   - Three consecutive failures (avoid false positives from temporary network issues)
   - WiFi reports connected with valid IP

3. **Add detection state to crystal_network:**
   ```c
   typedef struct {
       uint8_t dns_failure_count;
       bool dns_health_check_enabled;
       int64_t last_dns_check_time;
   } dns_health_state_t;
   ```

**Files to modify:**
- `components/crystal_network/src/crystal_network.cpp`
- `components/crystal_network/include/crystal_network.h`

**Effort:** 1-2 hours

### Phase 2: Automatic Recovery

**Goal:** Recover DNS functionality without user intervention

**Options evaluated:**

| Option | Pros | Cons | Verdict |
|--------|------|------|---------|
| WiFi disconnect/reconnect | Non-invasive, preserves app state | May not clear socket leak | Try first |
| Reboot system | Guaranteed recovery | Disruptive, loses app state | Last resort |
| Restart network stack | Targeted fix | Requires esp_netif teardown/reinit | Complex, risky |

**Selected approach: Escalating recovery**

1. **Level 1: WiFi cycle (try first)**
   - Disconnect WiFi
   - Wait 2 seconds
   - Reconnect to last SSID
   - Test DNS after reconnection
   - If still failing, escalate

2. **Level 2: System reboot (guaranteed recovery)**
   - Log reason: "DNS resolver corruption detected"
   - Show toast notification: "Network recovering, please wait..."
   - Wait 1 second for toast to display
   - Call `esp_restart()`

**Implementation:**

```c
void crystal_network_handle_dns_failure() {
    if (recovery_attempt == 0) {
        ESP_LOGW(TAG, "DNS failure detected, attempting WiFi cycle");
        crystal_hal_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(2000));
        crystal_hal_wifi_connect_last();
        recovery_attempt = 1;
    } else {
        ESP_LOGE(TAG, "DNS failure persists, system restart required");
        crystal_core_show_toast("Network recovering, please wait...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
}
```

**Files to modify:**
- `components/crystal_network/src/crystal_network.cpp`
- `components/crystal_core/include/crystal_core.h` (for toast API if needed)

**Effort:** 2-3 hours

### Phase 3: User Visibility

**Goal:** Make DNS failures visible for debugging and validation

**Implementation:**

1. **Add DNS failure counter to Ethernet Info app**
   - Display: "DNS failures since boot: X"
   - Display: "Last DNS recovery: [timestamp]"
   - Reset counter on manual reboot (not auto-recovery)

2. **Enhanced logging:**
   ```
   W (XXXXX) crystal_network: DNS health check failed (1/3)
   E (XXXXX) crystal_network: DNS resolver corrupted, initiating recovery
   I (XXXXX) crystal_network: DNS recovery: WiFi cycle attempt
   E (XXXXX) crystal_network: DNS recovery failed, rebooting system
   ```

3. **Toast notifications:**
   - No notification for WiFi cycle (silent recovery)
   - Show toast before reboot: "Network recovering, please wait..."

**Files to modify:**
- `components/crystal_apps/ethernet_info/ethernet_info.cpp`
- `components/crystal_network/src/crystal_network.cpp`

**Effort:** 1-2 hours

### Phase 4: Telemetry and Analysis

**Goal:** Collect data to validate fix and track ESP-IDF upgrade impact

**Implementation:**

1. **NVS counters (persist across reboots):**
   - `dns_failure_count` - total DNS corruption events
   - `dns_recovery_wifi_cycle_count` - recoveries via WiFi cycle
   - `dns_recovery_reboot_count` - recoveries via system reboot
   - `last_dns_failure_timestamp` - most recent event

2. **Add to Developer Tools:**
   - Display DNS failure statistics
   - Button: "Clear DNS statistics"
   - Export to log for analysis

3. **Log pattern analysis:**
   - Track time between `[sock=XX] select() timeout` and DNS failure
   - Count occurrences per session
   - Measure recovery success rate

**Files to modify:**
- `components/crystal_network/src/crystal_network.cpp`
- `components/crystal_apps/dev_tools/dev_tools.cpp`

**Effort:** 2-3 hours

## Implementation Priority

### Must Have (Phase 1 + 2)
- DNS health detection
- Automatic recovery (WiFi cycle → reboot)
- Basic logging

**Total effort:** 3-5 hours

### Should Have (Phase 3)
- User-visible statistics in Ethernet Info
- Toast notification before reboot

**Total effort:** +1-2 hours

### Nice to Have (Phase 4)
- Detailed telemetry in Developer Tools
- Pattern analysis for ESP-IDF upgrade validation

**Total effort:** +2-3 hours

## Testing Plan

### Test Case 1: DNS Corruption Trigger
1. Connect to WiFi
2. Start Weather app (continuous HTTPS requests every 5-10 seconds)
3. Shield device until `[sock=48] select() timeout` appears in logs
4. Verify DNS error 202 appears on next request
5. **Expected:** Recovery initiated within 60 seconds

### Test Case 2: WiFi Cycle Recovery
1. Trigger DNS corruption (Test Case 1)
2. **Expected:** WiFi disconnects and reconnects automatically
3. Verify DNS works after reconnection
4. **Success if:** No reboot required

### Test Case 3: Reboot Recovery
1. If WiFi cycle doesn't fix DNS (unlikely but test anyway)
2. **Expected:** System reboots after WiFi cycle fails
3. Verify DNS works after reboot
4. **Success if:** DNS functional after reboot

### Test Case 4: False Positive Avoidance
1. Disconnect WiFi manually
2. **Expected:** DNS health check pauses while disconnected
3. Reconnect
4. **Expected:** No recovery triggered

### Test Case 5: Long-term Stability
1. Run device for 24+ hours with periodic signal loss
2. Monitor recovery events in logs
3. **Expected:** Automatic recovery each time without manual intervention

## Configuration Options

Add to `components/crystal_network/Kconfig`:

```kconfig
config CRYSTAL_NETWORK_DNS_HEALTH_CHECK
    bool "Enable DNS health monitoring"
    default y
    help
        Monitor DNS resolver health and automatically recover from ESP-IDF socket leak bug.
        Disable only if ESP-IDF bug is confirmed fixed in your version.

config CRYSTAL_NETWORK_DNS_CHECK_INTERVAL_SEC
    int "DNS health check interval (seconds)"
    default 60
    range 10 300
    depends on CRYSTAL_NETWORK_DNS_HEALTH_CHECK
    help
        How often to probe DNS resolver health when WiFi connected.

config CRYSTAL_NETWORK_DNS_FAILURE_THRESHOLD
    int "DNS failure threshold before recovery"
    default 3
    range 1 10
    depends on CRYSTAL_NETWORK_DNS_HEALTH_CHECK
    help
        Number of consecutive DNS failures before triggering recovery.
```

## Rollback Plan

If the enhancement causes issues:

1. **Disable via Kconfig:**
   ```
   # CONFIG_CRYSTAL_NETWORK_DNS_HEALTH_CHECK is not set
   ```

2. **Remove at runtime:**
   - Delete NVS key: `dns_health_enabled`
   - System continues without monitoring

3. **Complete removal:**
   - Revert commits from Phases 1-4
   - No impact on other components

## Future Considerations

### When ESP-IDF Bug is Fixed

1. **Verify fix in release notes:**
   - Search for "esp-tls socket leak" or "esp_tls_conn_destroy"
   - Check esp_http_client changelog

2. **Validation process:**
   - Upgrade ESP-IDF to fixed version
   - Run Test Case 1 (trigger DNS corruption)
   - Monitor for 7 days with DNS health check enabled
   - If zero DNS failures → mark as resolved

3. **Cleanup:**
   - Change `CRYSTAL_NETWORK_DNS_HEALTH_CHECK` default to `n`
   - Add note in Kconfig: "Only needed for ESP-IDF < vX.Y.Z"
   - Keep code for backward compatibility with older IDF versions

### Alternative: Local ESP-IDF Patch

If ESP-IDF bug remains unfixed long-term, consider patching esp_http_client:

```c
// In esp_http_client.c, after esp_tls connection failure:
if (client->tls) {
    esp_tls_conn_destroy(client->tls);  // <-- Add this
    client->tls = NULL;
}
```

**Pros:**
- Fixes root cause
- No application-level recovery needed

**Cons:**
- Must maintain patch across ESP-IDF upgrades
- Requires rebuilding ESP-IDF components
- May conflict with future ESP-IDF fixes

**Verdict:** Only pursue if ESP-IDF bug remains unfixed after 6+ months

## Documentation Updates

### Files to update:

1. **README.md:**
   - Change "Known Issues" status to "Mitigated"
   - Note: "Automatic recovery implemented as workaround"

2. **docs/WIFI_DNS_SOCKET_LEAK_BUG.md:**
   - Add section: "Application-Level Mitigation"
   - Document recovery behavior

3. **Memory file:**
   - Update `esp-tls-socket-leak-breaks-dns-p4.md`
   - Add: "Crystal OS implements automatic DNS health monitoring and recovery"

## Success Criteria

1. **Functional:**
   - DNS corruption detected within 60 seconds
   - Automatic recovery successful in >95% of cases
   - No false positive recoveries during normal operation

2. **User Experience:**
   - Recovery transparent to user (no manual intervention)
   - Toast notification only shown before reboot (worst case)
   - Apps continue working after recovery

3. **Maintainability:**
   - Can be disabled via Kconfig if ESP-IDF bug is fixed
   - Easy to validate fix effectiveness via telemetry
   - Clear migration path when bug is resolved

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| False positive triggers recovery | Low | Medium | 3-failure threshold, check WiFi state |
| Recovery disrupts user workflow | Low | Low | WiFi cycle is fast, reboot is rare |
| Masking other DNS issues | Medium | Low | Log all recovery events, track patterns |
| ESP-IDF upgrade breaks detection | Low | Medium | Test after each IDF upgrade |
| Recovery itself fails | Very Low | Medium | Reboot is guaranteed recovery |

## Estimated Total Effort

- **Phase 1 (Must Have):** 3-5 hours
- **Phase 2 (Must Have):** Included in Phase 1
- **Phase 3 (Should Have):** 1-2 hours
- **Phase 4 (Nice to Have):** 2-3 hours
- **Testing:** 2-3 hours
- **Documentation:** 1 hour

**Total: 9-14 hours for complete implementation**

**Minimum viable: 5-8 hours (Phases 1-2 + testing)**

## Next Steps

1. Review and approve this plan
2. Implement Phase 1 + 2 (must have features)
3. Test with signal loss scenarios
4. Monitor for 2-3 days in production use
5. Implement Phase 3 (user visibility) if recovery events are frequent
6. Implement Phase 4 (telemetry) to track ESP-IDF upgrade impact
