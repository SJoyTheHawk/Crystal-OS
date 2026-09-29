# Crystal OS Global HTTPS/TLS — Phase 3 KMB Catalog Plan

**Status:** Draft  
**Parent phase:** [Incremental Global HTTPS/TLS plan](Incremental-Global-HTTPS:TLS-plan.md)
**Scope:** Migrate only the KMB route catalog request in `bus_service` to `crystal_http`.

CTB catalog requests, KMB route variants, stops, ETA requests, weather requests, and the existing direct HTTP helper remain unchanged until their own phases. The current KMB URL is `https://data.etabus.gov.hk/v1/transport/kmb/route/`.

## How to execute this plan

Treat each numbered step as a separate reviewable change. There are ten gates, Step 0 through Step 9. Do not combine steps to make a failing check harder to localize. A step may be committed only after its gate passes. If a gate fails, stop at that step, record the failure, and restore the last passing commit before changing the next concern.

Every device check records the firmware commit, request ids, result, and memory readings. “Framework request id” and “bus request id” are different identifiers; log both whenever a request is submitted or completed.

The migration must preserve:

- `BUS_EVT_ROUTE_CATALOG_PROGRESS` messages and their ordering;
- KMB JSON parsing and KMB variant extraction;
- route-catalog reset, partial-result handling, and filesystem caching;
- the final `BUS_EVT_ROUTE_CATALOG` status and provider counts;
- the existing bus request id;
- PSRAM ownership for parsed KMB variant data.

## Step 0 — Freeze the KMB catalog baseline

Execution record: [Step 0 baseline](crystal-http-phase3-step0-baseline.md).
Build verified; device runs and catalog memory measurements pending.

**Change:** Documentation and a test record only. Do not change runtime code.

Capture one successful fresh KMB+CTB catalog fetch, one KMB failure, and one KMB-success/CTB-failure run. Record progress-event order, provider counts, cache result, request id, route and variant counts, and internal heap/PSRAM before and after the operation.

**Gate:** The three runs are reproducible and the expected event sequence is written down. If the baseline is not understood, do not start the migration.

## Step 1 — Add a KMB-only handoff context

Execution record: [Step 1 handoff context](crystal-http-phase3-step1-handoff.md).
Source/build checks pass; device behavior is unchanged. Step 0 remains pending,
so this preparatory change is not an acceptance of the Phase 3 device gates.

**Change:** Add a small context owned by the catalog operation. It contains the bus request id, KMB URL, framework request id, completion state, response pointer, and cancellation/timeout state. Add the context and helper declarations without changing the request path.

**Gate:** The project builds, no device behavior changes, and the context has one clear owner and one cleanup function. Document which task allocates and releases it.

## Step 2 — Submit the KMB request through `crystal_http`

Execution record: [Step 2 submission](crystal-http-phase3-step2-submission.md).
Build passes; device capture is pending because the serial port is currently
held by another Python monitor.

**Change:** Replace only the KMB catalog submission with `crystal_http_get()`. Keep the callback diagnostic-only: record framework status, body length, and both request ids, then release the response. Do not feed the body to the catalog parser yet.

Use the explicit initial policy:

```text
timeout: 15 seconds
max attempts: 10
backoff: 500 ms, capped at 8 seconds
keep-alive: disabled initially
body limit: measured KMB catalog size plus a bounded safety margin
owner: dedicated bus catalog owner id
```

**Gate:** On the device, a successful request logs submission, each attempt, status, body length, and exactly one response release. A forced transport failure reaches its final callback without a crash or leaked response. The existing parser remains untouched, and the CTB path still runs as before.

## Step 3 — Make callback-to-bus handoff bounded and safe

Implementation record: [Step 3 bounded handoff](crystal-http-phase3-step3-handoff.md).

**Change:** Transfer response ownership from the callback to the waiting bus operation and signal the bus worker. The worker waits with a finite timeout, then owns parsing and release. The callback must not use LVGL, mutate UI state, or block on the bus worker. A timeout, cancellation, queue-submission failure, or stale context must cause a safe release or ignore path exactly once.

**Gate:** Exercise success, callback error, timeout, cancellation, and queue-submission failure. Each case ends with one context cleanup and one response release at most; the `crystal_http` worker never waits for the bus worker. Record the result before proceeding.

## Step 4 — Reuse the existing KMB parser

Implementation started after the successful Step 3 handoff capture.

**Change:** Pass the owned response body to the existing KMB `data` array parser and variant extraction code. Parse before `crystal_http_response_release()`. Do not retain pointers into the response body; preserve PSRAM allocation and the existing “KMB data downloaded” and “KMB data resolved” progress messages.

**Gate:** A captured valid KMB body produces the same route and variant counts as the baseline. Invalid JSON and a missing `data` array produce a controlled KMB failure, no crash, and no response-body leak.

## Step 5 — Restore complete catalog sequencing

Step 4 device gate passed; proceed to sequencing verification.
Implementation record: [Step 5 sequencing](crystal-http-phase3-step5-sequencing.md).

**Change:** Reconnect the migrated KMB result to the existing catalog operation: reset state at the same point, publish the same progress events in the same order, continue to the unchanged CTB fetch, and retain complete-cache and partial-cache behavior.

**Gate:** Run fresh KMB+CTB success and KMB success followed by CTB failure. Compare event order, provider counts, route/variant counts, and cache result with Step 0. No CTB source or behavior changes are allowed in this step.

## Step 6 — Remove nested KMB retry behavior

**Change:** Delete the KMB branch’s local retry loop only after framework retries are observed. Let `crystal_http` own transport retries, backoff, timeout, and body-limit decisions. Keep the CTB retry loop unchanged.

**Gate:** A forced KMB failure shows the configured framework attempt count and backoff, one final bus failure result, and no multiplied or nested attempts. A successful request still produces one catalog result.

## Step 7 — Remove the KMB direct-client call

**Change:** Remove only the KMB catalog `esp_http_client` call and its now-unused local state/includes. Retain direct-client code needed by CTB and all later operations. Do not change the generic helper globally.

**Gate:** Repository search identifies no direct `esp_http_client` call in the KMB catalog branch, while CTB and other existing operations still compile and link. The Step 5 success and partial-cache runs still pass.

## Step 8 — Run the failure and lifetime matrix

**Change:** No new feature code. Exercise the integrated path under controlled failures:

1. transport failure after retries;
2. non-success HTTP status;
3. body-limit failure;
4. invalid JSON;
5. bus timeout while the callback is pending;
6. cancellation/app pause during retry;
7. stale callback after catalog teardown.

**Gate:** Every case reports the existing bus failure/partial-catalog result, preserves the HTTP status where available, ignores stale callbacks, and releases the response exactly once. No stale route or variant data is committed.

## Step 9 — Verify repeatability and record acceptance

**Change:** No runtime change. Run at least five repeated catalog fetches, including one forced failure, and compare internal heap, largest internal block, and PSRAM against the Step 0 watermark.

Record:

```text
phase3 commit=<sha>
bus request id=<id> crystal_http request id=<id>
KMB status=<...> body=<...> attempts=<...>
CTB status=<...>
providers succeeded=<...> failed=<...>
cache result=<complete|partial|restored|none>
heap before/after=<...>/<...>
psram before/after=<...>/<...>
result=pass|fail
```

**Gate:** Normal KMB catalog fetch, forced KMB failure, partial-provider handling, cancellation/timeout cleanup, invalid JSON, and repeated-fetch memory checks pass without changing CTB behavior. Only then mark Phase 3 complete and create the phase commit.
