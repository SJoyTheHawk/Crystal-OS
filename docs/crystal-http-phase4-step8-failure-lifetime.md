# Phase 4 Step 8 — KMB Stop Failure and Lifetime Matrix

**Status:** Partially validated — transport, body-limit, Back/Wi-Fi cancellation,
and stale-selection captures passed. Full plan matrix coverage remains pending.

Coverage correction: the earlier sign-off conflated body-limit rejection with
HTTP failure and omitted the plan's invalid JSON and missing/empty data rows.
Those cases require separate evidence. Back cancellation also does not by
itself prove app pause/destroy during retry/handoff, and Wi-Fi loss during
connect does not prove loss during body read. Retain these as pending where
no matching capture is available; do not mark the full phase complete yet.
**Plan:** [Phase 4 KMB route-stop plan](crystal-http-phase4-kmb-stops.md)

Step 7 proved that the KMB stop branch contains no direct client call and that
normal inbound/outbound requests still work. Step 8 exercises terminal errors,
allocation/body limits, cancellation, and stale selections after that boundary.

## Controlled failure build

Use the temporary component option below for the transport-failure row:

```text
-DCRYSTAL_HTTP_PHASE4_KMB_STOPS_FORCE_FAILURE=ON
```

The option changes only the KMB route-stop endpoint to an invalid hostname. It
must be `OFF` for normal runs and removed before Step 9 acceptance.

For the body-limit row, use this separate temporary option. It lowers only the
KMB stop limit from 64 KiB to 1 KiB; it must also be `OFF` for normal runs and
Step 9 acceptance:

```text
-DCRYSTAL_HTTP_PHASE4_KMB_STOPS_FORCE_BODY_LIMIT=ON
```

## Matrix

| Case | Device action | Expected evidence |
| --- | --- | --- |
| Normal inbound | Select a KMB inbound variant | HTTP 200, ordered stop rows, one release |
| Normal outbound | Select a KMB outbound variant | HTTP 200, ordered stop rows, one release |
| Transport failure | Flash forced-failure image and select KMB stops | attempts 1/3 through 3/3, one error, no rows |
| Body/HTTP failure | Use controlled endpoint or existing limit hook | terminal error, one release, no rows |
| Wi-Fi loss | Disconnect during connect/body read | cancellation, no later retry, no stale rows |
| App/back cancellation | Leave stop page during request | `cancelled` increments, stale callback ignored |
| Stale selection | Start route A, select route B before completion | A is ignored; B remains current |

Record the bus/framework ids, route/direction, status, transport error,
attempts, response release count, stop count, and before/after heap and PSRAM
for each row. A row passes only when the app remains usable and cleanup occurs
exactly once.

## Current evidence

Normal inbound, outbound, retry, Wi-Fi cancellation, and Back cancellation are
already captured in the Step 4–7 records. The forced transport failure and the
remaining lifetime rows are pending device captures.

### Transport failure capture

The forced-failure image was exercised with KMB routes 102P inbound, 102
outbound, and 102 inbound. Each framework request used the invalid test
hostname, reached attempt 3/3, and completed with `status=0` and
`ESP_ERR_HTTP_CONNECT`:

```text
framework_id=3 attempts=3 error=ESP_ERR_HTTP_CONNECT response released once
framework_id=4 attempts=3 error=ESP_ERR_HTTP_CONNECT response released once
framework_id=5 attempts=3 error=ESP_ERR_HTTP_CONNECT response released once
```

The framework statistics increased from `retries=0 failures=0` before the
stop tests to `retries=6 failures=3` afterward, matching two retries per
failed request. No stop rows were emitted, no crash occurred, and PSRAM stayed
stable at approximately 4.50 MiB after each cleanup. This row passes.

### Wi-Fi loss capture

During KMB route 102 inbound request `bus_id=2` / `framework_id=3`, Wi-Fi was
disabled while attempt 1 was active. The network event cancelled owner
`1263358804` with count 1. The framework completed the in-flight TLS operation
as a cancellation race with `ESP_ERR_INVALID_STATE`, made no retry attempt,
and released the response once:

```text
disconnect=56584 ms
owner cancellation=56593 ms
callback=57156 ms error=ESP_ERR_INVALID_STATE attempts=1
stats cancelled=1 retries=0
response released once; heap released at 57196 ms
```

The final bus error is the expected network-loss result. No retry followed and
the request cleaned up safely, so this row passes.

### App/back cancellation capture

KMB route 102 inbound request `bus_id=4` / `framework_id=5` was cancelled by
Back while attempt 1 was active:

```text
Back/cancel=232293 ms
owner cancellation count=1
callback=232804 ms error=ESP_ERR_INVALID_STATE attempts=1
stats cancelled=2 retries=0
response released once; heap released at 232845 ms
stale request ignored id=4
```

The request raced through TLS and body handling after cancellation, but its
result was discarded (`body=0` at handoff), no retry occurred, and the app
ignored the stale cancelled event. This row passes.

### Rapid route-change / stale-selection capture

Several route selections were made in quick succession while earlier stop
requests were still completing. Requests for routes 102, 102P, and 102R were
cancelled as the stop page changed; each cancellation completed with
`ESP_ERR_INVALID_STATE`, released its response once, and produced an
`Ignoring stale cancelled bus request` message. No cancelled response added
rows to the page. The final active requests completed successfully:

```text
route=102P direction=outbound status=200 body=2296 count=26
route=102 direction=inbound status=200 body=2942 count=34
cancelled=6 retries=0 failures=0
```

The active selection remained usable and stale callbacks did not overwrite it,
so this row passes.

### Body-limit failure capture

The temporary 1 KiB body-limit build was exercised with KMB route 102 inbound,
102P inbound, and 102 outbound responses. Each normal response exceeded the
test limit and terminated cleanly:

```text
response sizes=2942, 2296, 2690 bytes; limit=1024 bytes
status=200 error=ESP_ERR_INVALID_SIZE attempts=1 body=0 for each request
responses released once; stop rows=0; no retries or cancellations
```

The framework recorded three failures without a crash or heap regression, so
the body-limit row passes. Restore the production 64 KiB limit and keep both
Step 8 test options disabled before Step 9.
