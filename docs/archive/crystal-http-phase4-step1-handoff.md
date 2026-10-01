# Phase 4 Step 1 — KMB Stop Handoff Context

**Status:** Source/build gate passed; device behavior gate not applicable.
**Plan:** [Phase 4 KMB route-stop plan](crystal-http-phase4-kmb-stops.md)

## Change recorded

Added an isolated `kmb_stops_handoff_t` in
`components/bus_service/src/bus_service.c`. It contains:

- the bus request id, route, direction, and service type;
- the KMB URL and future `crystal_http` request id;
- completion, timeout, and cancellation state;
- response status, transport error, attempt count, and body ownership;
- a completion semaphore for the future bus-worker handoff.

Added `kmb_stops_handoff_create()` and
`kmb_stops_handoff_cleanup()` as the context's single allocation and cleanup
path. The context is not submitted, read, or connected to the current direct
KMB stop request. The existing URL construction, retry loop, parser, and stop
events therefore remain unchanged.

Added the dedicated `KMB_STOPS_OWNER_ID` for the next submission step. It is
currently unused so catalog cancellation and future stop cancellation remain
separate.

## Verification

```text
git diff --check = pass
ESP-IDF = 6.1
idf.py build = pass
application bytes = 2840112 (0x2b5630)
application partition free = 46%
firmware SHA-256 = 883c24ab90893c88e2ec34459bbc1ba2882e0935d3eedfa02b8b2a48979843f3
sdkconfig SHA-256 = dae74a630f1bc45982785032205a178cb15f9ba55fe8e3e2b0ae29754a51cf0d
device flash/run = not performed
```

The compiler reports the two new helpers as unused. This is expected because
Step 1 requires the live request path to remain unchanged; Step 2 will consume
them when KMB stop submission moves to `crystal_http`.

## Gate result

Step 1 source and build checks pass. Step 0 remains pending until the inbound,
outbound, and failure baseline captures are recorded in
[crystal-http-phase4-step0-baseline.md](crystal-http-phase4-step0-baseline.md).
