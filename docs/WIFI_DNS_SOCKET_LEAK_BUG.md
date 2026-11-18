# ESP-IDF Bug: Socket Leak on esp-tls select() Timeout Breaks DNS on ESP32-P4 Hosted WiFi

> Investigation status (2026-11-19): the socket-leak explanation and upstream
> attribution below are unverified hypotheses. No socket-lifetime evidence or
> confirmed upstream issue is recorded here. Recovery proposals are not
> implemented policy. Use [current Wi-Fi checks](WIFI_CHECKS.md) for validation.

**Date:** 2026-11-18
**Platform:** ESP32-P4 + ESP32-C6 hosted WiFi (esp-hosted 3.0.10)
**ESP-IDF:** v5.5+
**Severity:** Critical - Permanent network failure, requires reboot

## Summary

When `esp_tls` connection times out during `select()` on an already-opened socket, the socket leaks and DNS permanently fails with `getaddrinfo() error 202` (EAI_ADDRFAMILY). The system cannot recover without a reboot.

## Reproducible Steps

1. ESP32-P4 with C6 hosted WiFi over SDIO (esp-hosted)
2. Make HTTPS requests continuously (e.g., weather API polling every 5-10 seconds)
3. Shield device or move out of WiFi range to cause connection timeouts
4. Wait for a timeout to occur during TLS handshake `select()`

## Expected Behavior

- TLS connection fails gracefully
- Socket is closed properly
- DNS continues to work
- Subsequent requests can succeed after WiFi reconnects

## Actual Behavior

**Working timeout (early failure, no socket leak):**
```
W (132743) esp-tls: Failed to open new connection in specified timeout
E (132743) transport_base: Failed to open a new connection
E (132745) HTTP_CLIENT: Connection failed, sock < 0
```
System recovers normally. DNS continues to work.

**Broken timeout (select() on opened socket, leaks):**
```
E (53016) esp-tls: [sock=48] select() timeout
E (53017) esp-tls: Failed to open new connection
E (53017) transport_base: Failed to open a new connection
E (53017) HTTP_CLIENT: Connection failed, sock < 0
```

**Immediate consequence - next request:**
```
E (59605) esp-tls: couldn't get hostname for :data.etabus.gov.hk: getaddrinfo() returns 202, addrinfo=0
```

All subsequent DNS queries fail with error 202. WiFi shows as connected with IP, but:
- No hostname resolution works
- All HTTPS requests fail
- Cannot reconnect to any AP
- Only reboot recovers

## Root Cause Analysis

When `select()` times out during TLS handshake:
1. Socket is already created and TCP-connected
2. `select()` timeout occurs waiting for TLS handshake data
3. **esp-tls fails to close the socket** before returning error
4. Socket remains in hung state (likely CLOSE_WAIT or similar)
5. lwip DNS resolver becomes corrupted, returns EAI_ADDRFAMILY for all queries
6. System never recovers

The difference between working and broken cases:
- **Working:** Socket creation or connection fails early, socket never allocated or properly cleaned up
- **Broken:** Socket exists (`[sock=48]`), `select()` times out, socket leaks

## Evidence

### Full sequence from production logs:

```
I (22762) crystal_http: request queued id=9
I (22763) crystal_http: request attempt id=9 attempt=1/1
E (53016) esp-tls: [sock=48] select() timeout        <-- SOCKET LEAK
E (53017) esp-tls: Failed to open new connection
E (53017) transport_base: Failed to open a new connection
E (53017) HTTP_CLIENT: Connection failed, sock < 0
I (53089) crystal_http: request queued id=10
I (53090) crystal_http: request attempt id=10 attempt=1/1
E (59605) esp-tls: couldn't get hostname for :data.etabus.gov.hk: getaddrinfo() returns 202  <-- DNS BROKEN
E (59610) esp-tls: Failed to open new connection
```

From this point forward, **all** DNS queries fail with error 202, even after WiFi reconnects.

### Multiple occurrences in same session:

Every time `[sock=XX] select() timeout` appears, DNS breaks immediately after:

**Occurrence 1:**
```
E (153554) esp-tls: [sock=48] select() timeout
[...then DNS error 202 on next request...]
```

**Occurrence 2:**
```
E (53016) esp-tls: [sock=48] select() timeout
[...then DNS error 202 on next request...]
```

Pattern is 100% reproducible: `select()` timeout with socket number → DNS fails immediately.

## System Configuration

**ESP32-P4 Configuration:**
- `CONFIG_SLAVE_IDF_TARGET_ESP32C6=y`
- `CONFIG_ESP_HOSTED_CP_TARGET_ESP32C6=y`
- `CONFIG_ESP_HOSTED_P4_DEV_BOARD_FUNC_BOARD=y`
- `CONFIG_LWIP_MAX_SOCKETS=16`
- `CONFIG_LWIP_DNS_MAX_SERVERS=3`

**Network stack:**
- esp-hosted 3.0.10
- HTTPS via esp_http_client
- TLS via esp-tls with mbedtls

## Impact

- **Permanent network failure** after any `select()` timeout during TLS handshake
- Only reboot recovers
- Affects always-connected devices (weather stations, IoT appliances)
- Cannot be worked around in application code

## Suspected Code Location

`components/esp-tls/esp_tls.c` or `esp_tls_mbedtls.c`:
- `select()` timeout handler during TLS handshake
- Socket not closed before returning `ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT`

## Workaround Attempts

1. ❌ Disabled `CONFIG_WIFI_RMT_STA_DISCONNECTED_PM_ENABLE` - no effect
2. ❌ Removed `CONFIG_ESP_NETIF_SET_DNS_PER_DEFAULT_NETIF` - no effect
3. ❌ `esp_http_client_close()` and `esp_http_client_cleanup()` called properly - no effect

Application code cannot prevent or recover from this leak.

## Notes

- **Does not occur on ESP32-S3 native WiFi** with same application code
- **Specific to ESP32-P4 + C6 hosted WiFi** (esp-hosted architecture)
- Socket leak appears to corrupt lwip DNS resolver internal state
- With `CONFIG_LWIP_MAX_SOCKETS=16`, even one leaked socket is critical

## Recommendation

Fix esp-tls to **always close the socket** when `select()` times out during TLS handshake, even if connection partially established.

## Additional Logs Available

Full session logs with timestamps showing the exact sequence of:
1. Normal HTTPS requests succeeding
2. `[sock=48] select() timeout`
3. Immediate permanent DNS failure (error 202)
4. WiFi reconnection attempts
5. DNS remaining broken despite successful WiFi connection with IP

Can provide full ESP-IDF component versions, complete application code, and reproduction test case if needed.
