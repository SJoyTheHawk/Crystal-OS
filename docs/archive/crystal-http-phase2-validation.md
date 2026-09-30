# Crystal OS Global HTTPS/TLS — Phase 2 Validation Plan

**Status:** Passed on device
**Parent phase:** Incremental Global HTTPS/TLS plan  
**Scope:** Measure TLS memory behavior through `crystal_http` before migrating bus operations.

Phase 2 is a measurement phase. It should not change application networking or tune sdkconfig values without evidence. Each test run must record the firmware commit, sdkconfig, endpoint, request policy, and device conditions.

## 2.1 Confirm the test request

Use the existing framework smoke request in `crystal_core`. Keep the first test conservative:

```text
timeout: 15 seconds
attempts: 1
body limit: 8 KiB
keep-alive: disabled
```

The phase 2 repeated series was run with a temporary diagnostic build configuration. That test runner has been removed after acceptance; normal firmware submits the single framework smoke request.

Record for every request:

- request id and attempt number;
- internal free heap and largest internal block before TLS;
- free PSRAM before TLS;
- internal and PSRAM values at the TLS allocation peak;
- response status, transport error, body length, and elapsed time;
- internal and PSRAM values after response release.

The existing before/after logs in `crystal_http` are useful, but before accepting this phase the implementation must expose a meaningful peak sample during the connection. A periodic sample, allocation hook, or equivalent high-water measurement is acceptable if it does not materially change timing.

The current implementation samples after client initialization, TLS open, and header fetch. It reports the lowest free internal heap, largest internal block, and PSRAM value observed across those TLS samples in both the response log and cumulative transport statistics.

## 2.2 Run the baseline series

Run at least 20 successful requests against the same small HTTPS endpoint, with the device booted into the normal UI and Wi-Fi already connected. Repeat the series after a cold boot if the first series shows a significant difference between the first handshake and later requests.

For each series calculate:

- minimum and maximum internal heap before, during, and after TLS;
- minimum largest internal block;
- minimum PSRAM during the request;
- maximum response body size;
- success count, transport-failure count, and HTTP-failure count;
- first-request and steady-state elapsed time;
- change in the post-release heap watermark from request 1 to the final request.

Acceptance requires all requests to complete without a leak or progressive reduction in the post-release heap watermark. A failed request must still restore the allocation watermark within the normal allocator variation observed in a successful request.

## 2.3 Evaluate configuration choices

Review the generated sdkconfig and the active ESP-IDF version for:

- `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`;
- mbedTLS input and output record-buffer sizes;
- ESP-TLS dynamic-buffer support and strategy;
- internal heap reserve settings;
- PSRAM allocation thresholds and capabilities.

Change one setting at a time only when the measurements show a failure or an unacceptable internal-heap margin. Repeat the complete baseline series after every change. Do not increase TLS buffers solely to improve a single slow request.

The preferred result is the smallest configuration that gives reliable handshakes while preserving a safe internal-heap margin for LVGL, Wi-Fi, and filesystem work. Record rejected changes and their measured effect so later phases do not revisit them.

## 2.4 Failure and cleanup checks

Run shorter repeated series for:

- an unreachable host or forced connection failure;
- a TLS handshake failure;
- a response at the body limit;
- an oversized response;
- cancellation during an in-flight request.

After each series verify that response storage is released, the queue returns to depth zero, and the next normal smoke request succeeds. Do not treat a cancellation as a memory leak merely because it has no response body.

## Phase 2 acceptance record

Commit the measurement/instrumentation changes separately from any sdkconfig change. Attach a device log or table containing:

```text
phase2 commit=<sha>
idf/sdkconfig=<version and relevant settings>
requests=<n> successes=<n> transport_failures=<n> http_failures=<n>
internal before min/max=<...> largest block min=<...>
internal tls peak min/max=<...> psram tls peak min/max=<...>
post-release heap delta first-to-last=<...>
max response body=<...>
result=pass|fail
```

Phase 2 is complete when the repeated series passes, failure cleanup passes, and any configuration change is justified by the recorded measurements.

## Phase 2 completed result

Cold-start device run completed with the phase 2 diagnostic build:

```text
phase2 config repeat_count=20 disable_weather=1
requests=20 successes=20 transport_failures=0 http_failures=0
status=200 for all requests
body=559 bytes for all requests
internal before request 1=77415 bytes
minimum internal during TLS=60383 bytes
largest internal block=31744 bytes
minimum PSRAM during TLS=4706776 bytes
PSRAM after every response release=4728820 bytes
internal after request 1=76719 bytes
internal after request 20=73007 bytes
result=pass
```

The repeated run showed no transport failures, no response-buffer leak, and no progressive PSRAM loss. Internal heap settled in the same range observed in the earlier run. The phase 2 instrumentation and bounded repeat harness remain available for later diagnostics; normal firmware defaults keep the repeat count and weather suppression disabled.
