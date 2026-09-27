# Crystal OS Global HTTPS/TLS — Phase 3 Implementation Plan

**Status:** Draft  
**Parent phase:** Incremental Global HTTPS/TLS plan  
**Scope:** Migrate only the KMB route catalog request in `bus_service` to `crystal_http`.

CTB catalog requests, KMB route variants, stops, ETA requests, weather requests, and the existing direct HTTP helper remain unchanged until their own phases. The current KMB URL is `https://data.etabus.gov.hk/v1/transport/kmb/route/`.

## 3.1 Preserve the current bus behavior

The migration must retain:

- `BUS_EVT_ROUTE_CATALOG_PROGRESS` messages and their ordering;
- KMB JSON parsing and KMB variant extraction;
- route-catalog reset, partial-result handling, and filesystem caching;
- the final `BUS_EVT_ROUTE_CATALOG` status and provider counts;
- the existing request id used by bus events;
- PSRAM ownership for the parsed KMB variant data.

The framework request id and bus request id are different identifiers. Log both when the KMB request is submitted and when its callback completes.

## 3.2 Choose the handoff model

Use a small request context owned by the bus catalog operation. It should contain the bus request id, the KMB URL, the framework request id, and the state needed to deliver the response back to the bus worker.

The preferred first implementation is a bounded handoff:

1. `process_route_catalog_request()` posts the existing “Preparing route data fetch” event.
2. It submits the KMB request with `crystal_http_get()`.
3. The callback records the framework response and signals the bus worker.
4. The bus worker waits with a finite timeout, releases the response after parsing, and continues to the existing CTB fetch.

The callback must not use LVGL or mutate UI state. It may copy the response metadata and transfer ownership of the response body to the waiting bus operation. If the wait times out or the bus operation is cancelled, the callback must still release the response safely and ignore stale context.

An asynchronous bus state machine is acceptable instead, but it must preserve the same event ordering and ownership rules. Do not block the `crystal_http` worker waiting for the bus worker.

## 3.3 Request policy and error mapping

Start with the values measured in phase 2. The initial KMB catalog policy should be explicit:

```text
timeout: 15 seconds
max attempts: 10
backoff: 500 ms, capped at 8 seconds
keep-alive: disabled initially
body limit: measured KMB catalog size plus a bounded safety margin
owner: dedicated bus catalog owner id
```

Let `crystal_http` perform transport and retry decisions. Remove the KMB-specific retry loop from the catalog branch so it does not create nested retries. Preserve the CTB retry loop until CTB is migrated.

Map the final framework response as follows:

- successful 2xx response with a valid JSON body: continue with “KMB data downloaded” and parsing;
- transport failure, cancellation, body-limit failure, or invalid JSON: report KMB failure and continue the existing partial-catalog logic;
- non-success HTTP status: preserve the status in logs and report the existing bus failure event.

Always call `crystal_http_response_release()` exactly once after the body is no longer needed. Parse the body before releasing it. Do not store pointers into the response body in route or variant structures.

## 3.4 Refactor boundaries

The current generic `http_get_json()` helper is used by both KMB and CTB paths. Split the KMB catalog fetch from that helper rather than changing the helper’s behavior globally.

Keep these operations in the existing path:

- CTB catalog fetch;
- KMB route-variant fetch;
- KMB and CTB stop requests;
- ETA requests.

After the change, `bus_service` may still include direct HTTP dependencies because CTB and later operations still use them. Phase 3 is not complete until the KMB catalog branch itself no longer calls `esp_http_client` directly.

## 3.5 Implementation checkpoints

### Checkpoint A — Request handoff

Submit a KMB request through `crystal_http` and log both ids. Use a temporary callback path that validates status, body length, and release behavior before changing catalog parsing.

### Checkpoint B — KMB parsing

Feed the received body through the existing KMB `data` array parsing and variant extraction. Preserve PSRAM allocation and the current progress messages.

### Checkpoint C — Catalog sequencing

Run KMB through the framework, then CTB through the old helper. Verify complete-cache and partial-cache behavior, including a KMB success followed by CTB failure.

### Checkpoint D — Cleanup

Remove only the KMB catalog direct-client call and its now-unused local retry code. Keep the direct-client code required by CTB and other operations. Confirm response release on every success, parse failure, timeout, cancellation, and queue-submission failure path.

## 3.6 Device tests

Run these cases on the device:

1. Fresh KMB and CTB catalog fetch: verify progress events, parsed routes, KMB variants, and saved cache.
2. KMB transport failure: verify framework retries, one final bus failure result, and a usable app.
3. KMB success with CTB failure: verify partial-provider counts and cache behavior.
4. KMB response with invalid JSON: verify no crash and no leaked response body.
5. Catalog cancellation or app pause during KMB retry: verify the callback is ignored or completed once and no stale catalog data is committed.
6. Repeated catalog fetches: verify post-release heap returns to the phase 2 watermark.

## Phase 3 acceptance record

Record:

```text
phase3 commit=<sha>
bus request id=<id> crystal_http request id=<id>
KMB status=<...> body=<...> attempts=<...>
CTB status=<...>
providers succeeded=<...> failed=<...>
cache result=<complete|partial|restored|none>
result=pass|fail
```

Phase 3 is complete when normal KMB catalog fetch, forced KMB failure, partial-provider handling, cancellation/timeout cleanup, and repeated-fetch memory checks all pass without changing CTB behavior.
