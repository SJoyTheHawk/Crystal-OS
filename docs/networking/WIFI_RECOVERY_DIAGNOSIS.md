# Wi-Fi and local panel incident — 2026-11-18

## What the supplied log establishes

Times below are device uptime, not wall-clock timestamps. The excerpt starts
around 23 minutes and ends mid-line around 64 minutes.

- Requests 12–17 complete with HTTP 200. Internal free heap remains about
  243 KB with a 160 KB largest block. No allocation failure, panic, watchdog
  reset, or sustained memory decline appears in this excerpt. This does not
  rule out a problem outside the captured interval.
- At 1425300 ms, the panel reports receive error 104 (`ECONNRESET`, peer
  reset). Subsequent outbound HTTPS succeeds. This warning alone does not
  establish a radio failure.
- At 3386281 ms, the control panel successfully uninstalls Bus Lite. The
  teardown reports `clean=1`. Receive error 11 (`EAGAIN`/`EWOULDBLOCK`)
  appears at 3419400 and 3424413 ms; the HTTP server maps this errno to a
  socket timeout. These messages do not identify the original cause of
  browser unreachability.
- At 3814904 ms, the panel starts again with approximately 253 KB free
  internal memory and publishes its mDNS name. This confirms server startup,
  not that a browser can resolve or reach it.
- At 3831540 ms, the station disconnects with reason 8 (association leave),
  followed by station stop/start events. This is consistent with the reported
  off/on toggle, not proof of an unexplained initial link loss.
- At 3839793 ms, reason 15 means four-way handshake timeout, with RSSI -79.
  At 3843206 ms, reason 205 means connection failure. The latter's RSSI -128
  should not be interpreted as a useful associated-link measurement.
  Weak reception/interference is a plausible contributor to the handshake
  failure, but this log cannot distinguish it from AP/security behavior.
- The host receives C6 RPC events and schedules retries 1 and 2. The excerpt
  does not show a permanently hung C6, failed SDIO transport, or the final
  outcome of recovery. It does not prove that the bugs below caused this
  particular incident.

Reason values and errno mappings were checked against the installed ESP-IDF
6.1 Wi-Fi headers, HTTP server source, and RISC-V toolchain errno header.

## Confirmed code defects fixed

1. Reconnect ran `esp_wifi_connect()` while holding a mutex inside a shared
   `ESP_TIMER_TASK` callback. On P4 this is a synchronous C6 RPC and can wait
   for seconds. That blocks other ESP timer callbacks. The timer now only
   notifies a dedicated 4096-byte-stack worker, which rechecks current state
   before accessing the driver. Startup reconnection uses the same worker.
2. An immediate connect/RPC error logged a failure without scheduling another
   attempt. Such errors need not emit DISCONNECTED, so recovery could stop
   indefinitely. Explicit connection failures and worker failures now use
   the same paced retry/backoff policy as disconnection events. IP-setting
   reconnects also use this path.
3. Disabling Wi-Fi did not immediately clear `has_ip`; it depended on a later
   event. Lost-IP and disconnect-during-scan paths also failed to immediately
   notify route consumers. They now clear/publish offline state, and queued
   connection/IP events cannot restore a route while disabled or forgotten.
   Cross-task enabled/IP flags are atomic.
4. Added HAL diagnostics for disconnect RSSI, enabled state, reconnect
   attempts/memory, radio start/stop errors, and successful IP recovery.

The existing retry delays/backoff are preserved. This patch does not reset
the C6, reboot the device automatically, repair lost driver events, or add a
DHCP/transport watchdog. A failed radio start is logged; recovery from a
persistent driver/transport fault still needs hardware evidence and testing.

## Validation and device follow-up

Host ASan/UBSan tests exercise the actual adapter with scripted failures:

```sh
python3 tools/test_crystal_wifi.py
python3 tools/test_network_policy.py
python3 tools/test_control_logs.py
python3 tools/test_crystal_http_retry.py
```

Coverage includes immediate driver errors without disconnect events, retry
exhaustion and recovery, deferred boot/radio-start work, notification-only
timer dispatch, cancellation after queued work, stale events after disable
or forget, lease loss, and existing scan/reconnect arbitration. These are
host tests, not validation of RF quality or real FreeRTOS scheduling.

The signed P4 firmware builds with ESP-IDF 6.1. It has not been flashed or
tested on the affected device as part of this diagnosis. Preserve the full
USB serial log from boot through the next failure and recovery.

After flashing the application, test near the AP first:

1. Enable the local panel and test both its displayed numeric IP and mDNS
   hostname. An IP-only success narrows the issue to name discovery.
2. Repeat Wi-Fi off/on cycles and confirm `WiFi IP acquired` followed by
   `Active route: Offline -> WiFi`. Refresh the browser after recovery;
   previous TCP sessions may have closed. Use the newly displayed IP.
3. Remove/restore AP availability, including while scanning. Verify retry
   attempts continue and the panel becomes reachable again. Allow for the
   existing backoff interval shown in the log.
4. Run a multi-hour panel and HTTPS soak, then repeat at the original
   location. Capture disconnect reasons, RSSI, heap, RPC timeouts, SDIO
   errors, and whether touch/UI still responds.

For an already configured build, application-only flashing preserves data:
`idf.py -p YOUR_PORT app-flash monitor`. Rebooting the currently installed
firmware alone does not install these fixes.
