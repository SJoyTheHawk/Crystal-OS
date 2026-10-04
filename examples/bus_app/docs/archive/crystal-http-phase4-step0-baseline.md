# Phase 4 Step 0 — KMB Route-Stop Baseline

**Status:** Inbound and outbound device captures recorded; failure capture pending.
**Plan:** [Phase 4 KMB route-stop plan](crystal-http-phase4-kmb-stops.md)

## Scope

This record freezes the current direct-client KMB stop behavior before the
Phase 4 handoff context and framework submission are connected. The current
implementation is `process_stops_request()` in
`components/bus_service/src/bus_service.c`; it builds the KMB URL with
`inbound`/`outbound`, calls the direct `http_get_json()` helper, retries up to
ten times with a 500 ms delay, and emits `BUS_EVT_STOPS_LIST` on success.

Step 0 changes no runtime behavior. Capture the runs below on the current
firmware image and attach the serial log or paste the relevant section into
this record.

## Required captures

### A. KMB inbound success

```text
firmware commit:
route:
bound=I URL:
bus request id:
stop count:
first/last sequence:
elapsed:
internal heap before/after:
largest internal block before/after:
PSRAM before/after:
result=pass|fail
```

Expected URL shape:

```text
https://data.etabus.gov.hk/v1/transport/kmb/route-stop/<route>/inbound/<service_type>
```

### B. KMB outbound success

```text
firmware commit:
route:
bound=O URL:
bus request id:
stop count:
first/last sequence:
elapsed:
internal heap before/after:
largest internal block before/after:
PSRAM before/after:
result=pass|fail
```

Expected URL shape:

```text
https://data.etabus.gov.hk/v1/transport/kmb/route-stop/<route>/outbound/<service_type>
```

### C. KMB failure while the stop page is loading

Use a controlled connection/TLS failure or disconnect Wi-Fi during the
request. Record:

```text
firmware commit:
route/bound:
bus request id:
attempts observed:
failure status/error:
stop-page message:
retry timing:
internal heap before/after:
PSRAM before/after:
result=pass|fail
```

The failure must leave the app usable and must not display a stop array made
from error text.

### D. Stale route response (if reproducible)

Select route A, select route B before A completes, and record both request ids.
The later response must remain the visible result; if the current UI cannot
produce overlap, record `not reproducible` and test this lifetime case in Step
6 after framework handoff is active.

## Source/build checkpoint

The original baseline source review confirmed the direct helper and the app's
`stop_request_id_` stale-event filter. The subsequent Step 2–4 changes now
route KMB stops through `crystal_http` while preserving that filter.

```text
git diff --check: pass
device inbound/outbound capture: recorded
device failure capture: pending
Step 0 gate: pending until capture C is recorded
```

## Supplied device capture

The supplied run records the inbound baseline on the current direct path:

```text
route=102 bound=I
URL=https://data.etabus.gov.hk/v1/transport/kmb/route-stop/102/inbound/1
bus request id=2
request attempt=1/10
stop count=34
request started=98587 ms
stops delivered=99304 ms
result=pass
```

The URL mapping and stop rows are correct, and no error or invalid stop payload
appears. The same run also recorded the outbound baseline:

```text
route=102 bound=O
URL=https://data.etabus.gov.hk/v1/transport/kmb/route-stop/102/outbound/1
bus request id=3
framework request id=4
attempts=2 (first attempt timed out, second returned HTTP 200)
stop count=31
result=pass
```

The controlled KMB failure capture is still pending, so the Step 0 gate is not
yet complete.

Do not treat a successful catalog fetch as a KMB stop baseline. Phase 4 Step 0
is complete only when both directions and one stop failure are recorded.
