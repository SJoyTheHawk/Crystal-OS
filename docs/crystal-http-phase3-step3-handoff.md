# Phase 3 Step 3 — Bounded KMB callback handoff

**Status:** Passed on source, build, and device success run; failure matrix pending.
**Plan:** [Phase 3 KMB catalog plan](crystal-http-phase3-kmb-catalog.md)

## Change recorded

The KMB framework callback now copies the response body into PSRAM-owned memory,
records the status, transport error, attempt count, and body length, releases
the framework response once, and signals the waiting bus operation. The bus
worker waits for a bounded interval and then owns the copied data. Step 3 does
not parse that data yet; Step 4 will connect it to the existing parser.

The callback does not call LVGL, publish UI events, or wait on the bus worker.
If the wait expires, the bus operation cancels the framework request and leaves
the context alive until the final callback can release it. Queue rejection and
body allocation failure use the same one-time cleanup paths.

## Verification

```text
git diff --check = pass
ESP-IDF = 6.1
idf.py build = pass
application bytes = 0x2b4b80
application partition free = 46%
device success run = passed (KMB status 200, body 349753, handoff received, CTB continued)
device failure matrix = pending
```

## Expected device evidence

Success should show the queued framework request, a callback with status and
body length, one `crystal_http` response release, and a matching
`KMB catalog handoff received` line. Forced transport failure, timeout,
cancellation, and queue rejection must finish without a crash or duplicate
cleanup. KMB route and variant counts remain zero at this step because parsing
is intentionally deferred.

## Gate result

The successful device handoff is passed. The captured run showed one response
release and a matching handoff receipt, with the CTB operation continuing. The
failure lifetime cases remain pending and should be covered during the later
failure/lifetime gate.
