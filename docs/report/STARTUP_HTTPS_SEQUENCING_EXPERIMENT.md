# Startup HTTPS sequencing experiment

Date: 2026-11-11. Baseline commit: 8065327.

This is an experiment for CPU0 idle-task starvation during startup. It does not
establish that all watchdog warnings or display glitches are fixed.

## Change

- Keep the existing 1.2-second service settling interval, and additionally wait
  for an explicit signal after the first frame and backlight setup finish.
- Route weather/location JSON requests through the existing single HTTP worker.
  The service task blocks on a completion semaphore instead of opening another
  TLS connection. LVGL does not wait on that semaphore.
- Submit the one-time HTTPS smoke test after the first weather attempt, whether
  it succeeds or fails (or immediately when weather is already fresh).
- Preserve the 8-second transport timeout, single attempt, existing weather
  retry policy, and 1023-byte JSON body limit. Oversized/failed responses are now
  rejected instead of parsing a truncated body.
- Bound the service task's queue/completion wait at 30 seconds. Cancellation can
  complete later; reference-counted callback state prevents stale writes and
  releases the response once both participants finish.
- Build the baseline with CRYSTAL_PHASE14_DEVICE_TEST=OFF and log that choice.
  The automatic package test remains available through its existing option.

The initial sequencing change did not change watchdog timeout/monitoring, task
priorities/affinity, TLS security, or display timing. The follow-up below changes
the HTTP worker priority. OTA uses its existing separate transport; this experiment
serializes location, weather, and the smoke test, not every network operation.
The service task still waits for weather, so this is not an asynchronous redesign
of all service functions.

## Validation

- python3 tools/test_crystal_core_http.py: PASS under ASan/UBSan. Covers immediate
  and delayed completion, queue/semaphore allocation rejection, response bounds,
  HTTP/transport failure, timeout, late callback isolation, and cancellation race.
- python3 tools/test_crystal_http_retry.py: PASS (existing worker regressions).
- idf.py -DCRYSTAL_PHASE14_DEVICE_TEST=OFF build: PASS; app size 0x2a1000,
  34% of the 4 MB application partition free.
- Application-only flash to /dev/cu.usbmodem101: verified successfully. No factory
  filesystem images or partition table were flashed.
- One unattended normal boot on the Clock card, monitored beyond 61 seconds:
  zero task-watchdog warnings, no unexpected reboot, HTTP 200 for both requests.
  ELF SHA256 prefix: 6d24ec8d9.

Observed device timestamps (milliseconds since boot):

| Event | ms |
| --- | ---: |
| First-frame log (2543 ms measured application initialization) | 3813 |
| Background services released | 4344 |
| Weather HTTP attempt starts | 6604 |
| Weather HTTP request completes (2411 ms) | 9014 |
| Weather parsed successfully | 9064 |
| Smoke HTTP attempt starts | 9086 |
| Smoke HTTP request completes (2949 ms) | 12034 |
| Normal display idle timeout logs | 61481–61506 |

Local raw capture: /tmp/crystal-startup-device.log.
Local build output: /tmp/crystal-startup-build.log.

## Remaining device checks

This run confirms sequential requests and a clean baseline boot. It does not
isolate sequencing from removal of the automatic test, prove that a single TLS
operation always yields enough CPU time, or visually verify the panel.

Next compare repeated boots and swiping while networking starts. Re-enable the
Phase 14 test separately if stress comparison is needed; it writes package test
state and may reboot. A clean baseline alone is not a stress-test result.

To repeat the baseline without replacing data partitions:

    idf.py -p /dev/cu.usbmodem101 -DCRYSTAL_PHASE14_DEVICE_TEST=OFF build app-flash monitor

If reverting, restore only this experiment's source/test/documentation changes
against 8065327, rebuild, and app-flash again. Reverting source alone does not
replace the firmware running on the board.

## Follow-up: allow idle time during a synchronous TLS request

The user's subsequent capture, ELF 6d24ec8d9, reproduced the warning while
switching cards, with Phase 14 disabled and only the first HTTPS request active:

- Weather request starts at 6733 ms.
- At 14187 ms, IDLE0 misses the watchdog deadline. CPU0 is running crystal_http
  inside X.509 / ECDSA certificate verification; CPU1 is running IDLE1.
- That request eventually succeeds (HTTP 200) at 16986 ms, elapsed 10254 ms.
- The smoke request has not started at the warning. The original sequencing
  therefore works, but is insufficient to prevent idle-task starvation.

Capture: /Users/szemy/.codex/attachments/c2fd9190-2efc-4830-80c8-052416940337/Pasted text.txt

On 2026-11-11, retain the sequencing and change only the existing HTTP worker's
scheduling priority from 2 to tskIDLE_PRIORITY (0), on the same core (CPU0).
The installed IDF FreeRTOS configuration enables preemption and time slicing,
and disables configIDLE_SHOULD_YIELD. This allows the worker and IDLE0 to share
CPU0 through tick-driven scheduling even while the worker is inside a long
synchronous crypto call. A compile-time check requires preemption/time slicing.
The worker logs its actual priority at startup to identify the running change.

This deliberately runs background HTTP below UI and networking work. Request
latency can increase under sustained load, and requests can still time out. It
does not change certificate verification, affinity, display timing, HTTP or
watchdog timeouts, or the watchdog subscriptions. It does not feed the watchdog
on another task's behalf. A higher-priority task monopolizing CPU0, a long
critical section, and OTA's separate worker remain outside this change.

Why not simply add a delay inside the HTTP loop? The captured stall is inside
esp_http_client_open(), before that loop can run. Why not just enable
restartable ECC? The installed esp_mbedtls_handshake() treats errors other
than WANT_READ/WANT_WRITE as failures; that approach needs further transport
integration, rather than a configuration-only change.

Validation of the follow-up:

- Host HTTP worker regression: PASS, including initialization at idle priority
  on CPU0 and idempotent initialization, plus existing retry/cleanup tests.
- Host service adapter ASan/UBSan regression: PASS.
- IDF build with Phase 14 OFF: PASS, app size 0x2a1000; 34% partition free.
- git diff --check: PASS.
- After the user released the serial port, application-only flash to
  /dev/cu.usbmodem101 succeeded. Firmware ELF SHA256 prefix: 70ba2df61.
- Device log confirms worker core=0, priority=0; first-frame gating and
  weather-before-smoke sequencing remain active.

Device results with manual card transitions during HTTPS:

| Captured boot | Card transitions started | Weather result | Smoke result | Watchdog warnings | Last logged uptime |
| --- | ---: | --- | --- | ---: | ---: |
| 1 | 17 | HTTP 200, 6214 ms | Connection reset, 16202 ms | 0 | 80.322 s |
| 2 | 8 | Interrupted by explicit reset before completion | Not started | 0 | 21.671 s |
| 3 | 16 | HTTP 200, 10167 ms | HTTP 200, 12319 ms | 0 | 114.993 s |

Boot 1 ended after the physical reset button was held for five seconds (logged
by crystal_hal). Boot 2 ended with the monitor's requested USB/RTS reset.
Neither reset is evidence of a watchdog crash. Boot 3 stayed alive beyond
114 seconds and completed both HTTPS requests while cards were being switched.
The monitor was then closed cleanly, leaving the new firmware running.

The first smoke failure was MBEDTLS_ERR_NET_CONN_RESET (-0x0050), rather than a
watchdog warning. Its cause is not established by this capture; slower
background scheduling and network/server behavior remain possible factors.
Do not discard this failure or claim the change guarantees network reliability.
The later successful run shows that requests lasting longer than the five-second
watchdog interval can now complete without the observed IDLE0 warning.

Device capture: /tmp/crystal-http-fairness-device.log. Display artifacts were
not visually assessed by the agent.

Build output: /tmp/crystal-http-fairness-build.log. Host tests do not emulate
FreeRTOS scheduling, so they do not establish that the watchdog issue is fixed.
Continue repeated startup with card transitions during the weather TLS
handshake, checking HTTP completion as well as watchdog warnings. These short
device checks support retaining the scheduling change, but are not a long-run
reliability result or proof that all display artifacts are resolved.
