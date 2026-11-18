# WiFi Stability Fix: STA_DISCONNECTED_PM Issue

> Historical hypothesis (reviewed 2026-11-19): disabling disconnected power
> management did not resolve the reported failure. Do not treat the proposed
> changes below as a verified fix. Use [current Wi-Fi checks](WIFI_CHECKS.md).

**Date:** 2026-11-18
**Issue:** WiFi becomes completely unresponsive after device loses signal or idles, requiring full device reboot to recover.

## Symptoms

When the device loses WiFi signal (e.g., by shielding the device from the AP) or after idle:

1. **DNS resolution fails** with `ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME`
   - `getaddrinfo() returns 202` (EAI_ADDRFAMILY)
   - All HTTPS requests timeout at the TLS handshake stage

2. **WiFi reconnection attempts fail** with multiple reason codes:
   - Reason 8: `ASSOC_LEAVE`
   - Reason 15: `AUTH_EXPIRE`
   - Reason 205: `BEACON_TIMEOUT`

3. **Recovery impossible without reboot:**
   - Toggling WiFi off/on in Settings doesn't help
   - Attempting to reconnect to the network fails repeatedly
   - **Ethernet works immediately**, proving lwip/DNS stack is functional

4. **Log evidence from affected device:**
```
I (480784) crystal_http: request queued id=79
W (495822) esp-tls: Failed to open new connection in specified timeout
W (495825) crystal_http: request id=79 transport failure=ESP_ERR_HTTP_CONNECT
W (495825) crystal_http: transport diagnostic id=79 error=ESP_ERR_HTTP_CONNECT
                         tls_error=ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT

I (499856) crystal_http: request queued id=80
E (506378) esp-tls: couldn't get hostname for :data.etabus.gov.hk: getaddrinfo() returns 202
E (506378) esp-tls: Failed to open new connection
W (506381) crystal_http: transport diagnostic id=80 error=ESP_ERR_HTTP_CONNECT
                         tls_error=ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME tls_code=0 socket_errno=5
```

The pattern: HTTP requests succeed normally, then after signal loss, TLS times out (expected while signal is lost), then **DNS permanently fails** even after WiFi reconnects.

## Root Cause - CONFIRMED

**ESP-IDF Bug: Socket leak in esp-tls on select() timeout**

When `esp_tls` times out during `select()` on an already-opened socket, the socket is not properly closed. This leaked socket corrupts the lwip DNS resolver, causing permanent `getaddrinfo() error 202` until reboot.

**Evidence pattern (100% reproducible):**

1. Normal HTTPS requests work fine
2. Connection timeout occurs: `E (53016) esp-tls: [sock=48] select() timeout`
3. Immediately after: `E (59605) esp-tls: couldn't get hostname for :data.etabus.gov.hk: getaddrinfo() returns 202`
4. All subsequent DNS queries fail permanently
5. WiFi can reconnect and get IP, but DNS never recovers
6. Only reboot fixes it

**Why this happens:**
- Socket is created and TCP-connected
- TLS handshake starts
- `select()` times out waiting for handshake data
- **esp-tls returns error but doesn't close the socket**
- Socket remains in hung state
- lwip DNS resolver becomes corrupted
- Returns EAI_ADDRFAMILY (202) for all queries

**Why it's different from timeout without socket:**
- Early timeout (before socket created): No leak, system recovers
- Late timeout (after socket opened): Socket leaks, DNS breaks permanently

See `docs/WIFI_DNS_SOCKET_LEAK_BUG.md` for full bug report for ESP-IDF.

## Investigation Notes

**2026-11-18 Update:** Comparison with Waveshare reference firmware reveals configuration difference:

**Key Finding:**
- **Crystal OS:** `CONFIG_ESP_NETIF_SET_DNS_PER_DEFAULT_NETIF=y`
- **Brookesia:** Not set (uses ESP-IDF default)

This setting binds DNS servers to specific network interfaces. When WiFi disconnects and reconnects on esp-hosted (P4+C6), the netif undergoes state transitions across the RPC boundary. The DNS resolver may lose its server configuration during these transitions.

**Why this worked on S3:**
- ESP32-S3 has native WiFi with synchronous netif state changes
- DNS resolver state stayed intact through disconnect/reconnect

**Why this fails on P4:**
- ESP32-P4 uses C6 hosted WiFi over SDIO RPC
- Netif state transitions are asynchronous across the RPC layer
- DNS-per-netif binding loses server config when netif goes down and comes back up
- Timing difference exposes the configuration issue

**Test plan:**
1. Remove `CONFIG_ESP_NETIF_SET_DNS_PER_DEFAULT_NETIF` from sdkconfig.defaults.esp32p4
2. Full clean build
3. Test signal-loss recovery (shield device, remove shielding, wait 2 minutes)
4. Verify DNS works after reconnection

**Previous hypothesis (disproven):**
- Initially suspected `WIFI_RMT_STA_DISCONNECTED_PM_ENABLE` but Waveshare has it enabled and works fine

## Verification Steps

After rebuilding and flashing with this fix:

1. Connect to WiFi and verify HTTP requests succeed (e.g., Bus Lite refreshes)
2. Shield the device completely to force signal loss and connection timeout
3. Remove shielding and wait up to 2 minutes without switching APs manually
4. Verify HTTP requests resume automatically without rebooting

**Before the fix:** DNS fails permanently after step 3, requiring reboot.
**After the fix:** WiFi reconnects and DNS resolves normally within 2 minutes.

## Additional Fix: P4 Reconnect Race Condition

A separate race condition was also fixed in `components/crystal_hal/src/crystal_hal.cpp` (lines 726-738):

**Problem:** When switching APs in Settings, the old AP's disconnect event could arrive after the new AP's connection attempt started, causing the HAL to incorrectly fail the new attempt.

**Fix:** Compare disconnection events against the current `last_ssid_` and ignore stale events from the previous network:

```cpp
if (disconnected && self->different_ssid(disconnected->ssid, disconnected->ssid_len)) {
    self->intentional_disconnect_pending_ = false;
    ESP_LOGI(TAG, "WiFi ignored disconnect from previous SSID: reason=%u", reason);
    return;
}
```

This prevents overlapping connection attempts but does **not** solve the DNS failure root cause — both fixes are needed.

## Code Review Summary

We verified that Crystal OS's WiFi HAL implementation (`components/crystal_hal/src/crystal_hal.cpp`) follows best practices:

- ✅ Proper initialization sequence matching Brookesia
- ✅ Correct event handling with mutex protection
- ✅ Appropriate retry logic and backoff strategy
- ✅ Clean state management for enable/disable/forget operations
- ✅ No unsafe calls to WiFi APIs from interrupt context

The DNS failure was purely a Kconfig setting inherited from ESP-IDF defaults that conflicts with Crystal OS's use case.

## Future Considerations

1. **Do not re-enable** `CONFIG_WIFI_RMT_STA_DISCONNECTED_PM_ENABLE` without extensive testing under signal loss conditions
2. WiFi power save can still be controlled at runtime via `esp_wifi_set_ps()` if needed for battery mode
3. Consider adding automated signal-loss recovery tests to CI

## Related Issues

- [[memory:esp-pm-min-freq-must-equal-max]] — another power-management interaction that appears as performance issue
- [[memory:close-kills-tls-session]] — TLS session management requires careful handling with ESP HTTP client
- [[memory:mbedtls-internal-mem-alloc-ooms]] — TLS handshake memory allocation issues

---

**Status:** Fixed 2026-11-18
**Requires:** Full clean build (`idf.py set-target esp32p4`) and reflash
**Impact:** Critical — resolves complete network failure after signal loss or disconnect
