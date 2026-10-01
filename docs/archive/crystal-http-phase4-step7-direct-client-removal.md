# Phase 4 Step 7 — Remove the KMB Stop Direct Client Path

**Status:** Passed — source inspection, diff check, and firmware build complete.
**Plan:** [Phase 4 KMB route-stop plan](crystal-http-phase4-kmb-stops.md)

## Change

The KMB route-stop branch in `process_stops_request()` submits through
`crystal_http` and receives its response through the bounded KMB stop handoff.
It no longer calls the local `http_get_json()` helper or owns an
`esp_http_client` connection. The shared direct helper remains for route
catalog/provider fetches, CTB stops, route variants, and ETA operations, as
required by the Phase 4 boundary.

No additional runtime change was needed for this gate because the direct KMB
stop path was removed while Steps 2–4 reconnected submission, handoff, and
parsing. Step 7 records the final source boundary and verifies that the
remaining direct operations still build.

## Verification

```text
KMB branch direct-client calls = 0
KMB branch submits via submit_kmb_stops()/crystal_http_get() = pass
remaining http_get_json() callers = catalog/provider fetches, CTB stops, route variants, ETA
git diff --check = pass
idf.py build = pass
```

## Device validation

The post-removal run completed both KMB directions:

```text
inbound route 102: framework_id=2, attempt 1 timed out, attempt 2/3
  status=200, body=2942, stops=34, error=ESP_OK
outbound route 102: framework_id=3, attempt 1/3
  status=200, body=2690, stops=31, error=ESP_OK
```

The framework reported one retry for the inbound timeout and no local nested
retry loop. Each response was released once. The heap returned after each
request (`psram=4544072` after inbound and `psram=4544264` after outbound), and
both stop pages remained usable.

The KMB stop success, framework failure, and cancellation captures from Steps
4–6 remain the acceptance evidence for the migrated path. This step does not
claim Phase 4 completion; the failure/lifetime matrix and repeatability gates
remain in Steps 8 and 9.
