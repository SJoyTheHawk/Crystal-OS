# Crystal OS Global HTTPS/TLS — Phase 4 KMB Route Stops Plan

**Status:** Implementation complete; final repeatability acceptance deferred.
**Parent phase:** [Incremental Global HTTPS/TLS plan](Incremental-Global-HTTPS:TLS-plan.md)
**Scope:** Migrate KMB route-stop requests from the direct `esp_http_client`
path in `bus_service` to `crystal_http`.

CTB route-stop requests, KMB route variants, ETA requests, the CTB catalog
request, and the weather helper remain unchanged until their own phases. The
KMB route catalog is already migrated and is the implementation pattern for
the response handoff used here.

## Current code and constraints

The current KMB stop path is `process_stops_request()` in
`components/bus_service/src/bus_service.c`:

- It maps the catalog direction codes correctly: `I` to `inbound` and `O` to
  `outbound`.
- It calls the shared local `http_get_json()` helper, which still owns a
  direct `esp_http_client` connection.
- It retries the whole request locally up to ten times with a fixed 500 ms
  delay. This retry loop is separate from `crystal_http` and must be removed
  after framework retries are observed.
- It allocates the response from PSRAM using the server's declared content
  length, with no route-stop-specific body limit and no chunked-body support.
- A successful response is parsed into a PSRAM `bus_stop_t` array and emitted
  as `BUS_EVT_STOPS_LIST`. Failures emit `BUS_EVT_ERROR` with a generic
  `ESP_FAIL` status.

The current ten-attempt budget is shared with the slower catalog work. It is
not automatically a good interactive stop-page policy: a user can wait through
several long connection timeouts before seeing an error. The initial Phase 4
policy therefore uses a stop-specific timeout and retry budget, subject to the
Step 0 timing record:

```text
timeout: 8 seconds
max attempts: 3
initial backoff: 500 ms
backoff cap: 2 seconds
```

These values are starting values for the device gate. Change them only when a
baseline measurement shows that the KMB stop endpoint needs a different
interactive budget, and record the reason in the step document.

The stop page already protects against stale successful responses by comparing
`event->request_id` with `stop_request_id_` and freeing stale stop arrays.
That check must remain. The current app pause/destroy path calls
`bus_service_cancel_all()`, but that function only resets the queue; it does
not cancel a direct request already running. Phase 4 must give the KMB stop
operation an owner id and make active cancellation reach the framework.

The framework has one worker and one active HTTPS operation. The KMB catalog
uses a callback-owned response copy plus a semaphore for the bus worker. Phase
4 should reuse that ownership model with a separate owner id for stops, for
example `KMB_STOPS_OWNER_ID`, so catalog and stop cancellation cannot interfere
with each other.

## Execution rules

Treat each numbered step as a separate reviewable change. Run the source check,
firmware build, and device check listed for the step before advancing. If a
gate fails, stop at that step and record the failing log and last known good
commit. Do not combine parser, retry, and lifetime changes into one unbounded
change.

Every device record includes the firmware commit, bus request id, framework
request id, route, direction, status, transport error, attempts, response
release count, stop count, and before/after internal heap and PSRAM. The bus
request id identifies the UI operation; the framework request id identifies the
HTTPS operation.

The migration must preserve:

- the corrected KMB URL mapping (`inbound`/`outbound`);
- `BUS_EVT_STOPS_LIST`, `BUS_EVT_ERROR`, and their request ids;
- stop-row order, stop ids, sequence numbers, and PSRAM ownership;
- the existing stop-page loading, success, error, and stale-response behavior;
- cancellation on network loss and app pause/destroy;
- one response release and one handoff cleanup for every terminal request.

## Step 0 — Freeze the KMB stop baseline

Execution record: [Step 0 baseline](crystal-http-phase4-step0-baseline.md).
Source review is complete; the device captures are still pending.

**Change:** Documentation and a test record only. Do not change runtime code.

Capture, on the current image:

1. A successful KMB inbound request.
2. A successful KMB outbound request.
3. A connection or TLS failure while the stop page is loading.
4. A route selection change that makes an earlier response stale, if the UI can
   produce the overlap.

For each run record the generated URL, progress and result logs, stop count and
order, request id, response length, elapsed time, heap readings, and whether the
stop page remains usable after failure. Confirm that CTB stop behavior is not
being measured as a KMB result.

**Gate:** Both KMB directions produce the same stop rows as the current build,
the failure path leaves the page usable, and the request/response ownership is
understood before the direct client is replaced.

## Step 1 — Add a KMB stop handoff context

Implementation record: [Step 1 handoff context](crystal-http-phase4-step1-handoff.md).
This source change adds the isolated context and its single cleanup helper. The
live direct stop request remains unchanged.

**Change:** Add a context owned by one KMB stop operation. It should contain the
bus request id, route, direction, service type, URL, framework request id,
completion semaphore, status, transport error, attempt count, response body
copy, body length, and terminal/cancelled state. Add one cleanup helper and
document that the bus worker owns the context after the callback signals it.

Do not change the active stop request path in this step. Do not put LVGL
pointers in the context.

**Build/source checks:** The context has one cleanup function, all fields have
one owner, and the existing direct stop request still compiles unchanged.

**Gate:** A normal firmware build passes and a source review shows no runtime
behavior change or second owner for the response body.

## Step 2 — Submit KMB stops through `crystal_http`

Implementation record: [Step 2 submission](crystal-http-phase4-step2-submission.md).

**Change:** Replace only the KMB stop submission with `crystal_http_get()`.
Keep the callback diagnostic-only for this step: log both ids, status, body
length, attempts, and transport error; release the framework response exactly
once. The parser and stop event path are reconnected in Step 4, so this
intermediate image is a transport/ownership check and is not yet a stop-page
acceptance run.

Use an explicit request policy:

```text
timeout: KMB_STOP_TIMEOUT_MS (initially 8 seconds)
max attempts: KMB_STOP_MAX_ATTEMPTS (initially 3, capped by crystal_http)
initial backoff: 500 ms
backoff cap: 2 seconds
keep-alive: disabled initially
body limit: measured KMB stop body plus a bounded safety margin
owner: dedicated KMB stop owner id
```

The body limit must be selected from Step 0 measurements. It must be large
enough for the largest valid route-stop body and small enough to reject an
unbounded response; do not use the framework default implicitly.

**Device check:** A successful inbound and outbound request each show one
framework submission and a 200 response. A forced connection failure reaches
the final callback without a crash or leaked response.

For the forced failure row, add a temporary component option such as
`CRYSTAL_HTTP_PHASE4_KMB_STOPS_FORCE_FAILURE` that selects an invalid KMB stop
hostname. Keep it `OFF` by default, use it only for the controlled device run,
and remove the hook before Step 9. The normal image must retain the production
KMB URL.

**Gate:** The CTB direct path is unchanged, every diagnostic response has
exactly one release, and the intermediate loss of stop rows is understood and
recorded as expected until Step 4 reconnects the parser.

## Step 3 — Establish bounded callback-to-bus ownership

Implementation record: [Step 3 handoff](crystal-http-phase4-step3-handoff.md).

**Change:** Replace the diagnostic callback with a bounded handoff modeled on
the Phase 3 KMB catalog handoff. The callback copies the response body into
PSRAM-owned context memory, records status/error/attempts, releases the
framework response, and signals the bus worker. It must not call LVGL, parse
JSON, or wait for the bus worker.

The bus worker waits with a finite deadline. On callback error, queue failure,
timeout, cancellation, or stale context, it must release or ignore exactly
once. A late callback after timeout must be able to clean up safely without
touching freed context memory.

**Device/build checks:** Exercise success, a forced transport failure, and a
request timeout or cancellation. Record callback timing, one response release,
one context cleanup, and the final stop-page event.

**Gate:** The framework worker never waits for the bus worker, no callback uses
LVGL, and all three cases end without a leak, double free, or use-after-free.

## Step 4 — Reconnect the existing stop parser and event delivery

Implementation record: [Step 4 parser integration](crystal-http-phase4-step4-parser.md).

**Change:** Parse the owned body before handoff cleanup. Reuse the existing
`data` array parser and `normalize_stop_id()` behavior. Preserve sequence order,
stop ids, placeholder names, PSRAM allocation, and `BUS_EVT_STOPS_LIST` delivery.

On a valid non-empty array, post the same successful event. On HTTP failure,
invalid JSON, missing/non-array `data`, empty data, allocation failure, or
transport failure, post the existing error event with a meaningful
`esp_err_t`; never pass an error string or response-body pointer as a stop
array. Preserve the bus request id so the app's stale-response check remains
effective.

**Gate:** Inbound and outbound counts and row order match Step 0. Invalid JSON,
missing `data`, and an empty array produce a controlled error or empty state,
release all owned memory, and leave a later route selection usable.

## Step 5 — Move retry and timeout policy to the framework

**Change:** Remove the KMB branch's local ten-attempt loop and fixed delay.
Submit one framework request and let `crystal_http` own timeout, retry,
backoff, body-limit, and transport classification. Keep the CTB and other
direct operations unchanged.

The final bus event must be emitted once after the framework's final callback.
Do not retry malformed JSON, body-limit failures, allocation failures, or
cancellation unless the framework explicitly classifies them as retryable.

**Gate:** A forced KMB failure shows framework attempts `1/3` through `3/3`
(or the configured final attempt), the expected backoff, one final bus error,
and one release. A normal request still produces one stop list and no nested
attempts.

## Step 6 — Complete cancellation and stale-request handling

Implementation record: [Step 6 cancellation](crystal-http-phase4-step6-cancellation.md).

**Change:** Extend the existing cancellation paths to the KMB stop owner.

- `bus_service_network_disconnected()` cancels an active KMB stop framework
  request immediately and prevents another stop retry while there is no IP.
- `bus_service_cancel_all()` cancels the active KMB stop request as well as
  queued work, covering app pause and destroy.
- A new stop request supersedes an older one at the bus/app boundary. A late
  callback may release its own response, but it must not post stop rows into
  the current page.
- The stop page must clear its loading state and show a usable error or
  reconnect message on cancellation.

**Device checks:** Disconnect Wi-Fi during TLS connection and during body
download; pause/destroy the bus app during retry; select another route before
the first callback. Record cancellation latency, callback result, event ids,
and heap/PSRAM after each case.

**Gate:** No full retry budget is consumed after cancellation, no stale rows
replace the current route, and each cancelled request releases once.

## Step 7 — Remove only the KMB stop direct-client path

Implementation record: [Step 7 direct-client removal](crystal-http-phase4-step7-direct-client-removal.md).

**Change:** Remove the KMB stop branch's direct `esp_http_client` use and its
now-unused local ownership. Keep the generic direct helper for CTB catalog,
CTB stops, route variants, and ETA operations until their own migrations.

**Source/build checks:** Repository inspection identifies no direct client call
in the KMB stop branch; the remaining CTB and other operations still compile.
Run `git diff --check` and a normal firmware build.

**Gate:** The Step 4 success, Step 5 forced failure, and Step 6 cancellation
captures still pass after the direct KMB stop code is removed.

## Step 8 — Run the KMB stop failure and lifetime matrix

Validation record: [Step 8 failure/lifetime matrix](crystal-http-phase4-step8-failure-lifetime.md).

Run each case with a controlled endpoint or device action and record the exact
bus/framework ids and release count:

| Case | Setup | Expected evidence |
| --- | --- | --- |
| Normal inbound | Select a KMB inbound variant | 200, valid stop rows, correct order |
| Normal outbound | Select a KMB outbound variant | 200, valid stop rows, correct order |
| Transport failure | Invalid KMB test endpoint | Framework final attempt, one error event, no rows |
| HTTP failure | Controlled 4xx/5xx response | Status/error preserved, no parse or rows |
| Body limit | Response over configured limit | `ESP_ERR_INVALID_SIZE`, one release, no rows |
| Invalid JSON | HTTP 200 malformed body | Controlled parse error, no leak |
| Missing/empty data | HTTP 200 without usable `data` | Controlled empty/error state, no stale rows |
| Wi-Fi loss | Disconnect during connect and body read | Immediate cancellation, no late rows |
| App pause/destroy | Leave bus app during retry/handoff | Cancellation and safe late callback |
| Stale selection | Select route B before route A completes | A is ignored; B remains current |

**Gate:** Every case leaves the app usable, reports one terminal event, and
shows no stale stop data, duplicate release, invalid free, or persistent heap
loss. Stop here if any lifetime row fails.

## Step 9 — Verify repeatability and record acceptance

**Status:** Deferred by schedule. Normal repeatability evidence is recorded,
but the final Step 8/9 matrix will be run after the next bus integration slice
on a known firmware commit. This is a deferred acceptance gate, not a failed
runtime implementation.

**Change:** No runtime change. Run at least five KMB stop fetches, alternating
inbound and outbound routes, with one forced failure and one Wi-Fi cancellation.
Include at least one route with a larger stop list.

Record:

```text
phase4 commit=<sha>
route=<route> bound=<I|O> service_type=<n>
bus request id=<id> crystal_http request id=<id>
status=<...> body=<...> stops=<...> attempts=<...>
transport error=<...> response releases=<...>
heap before/after=<...>/<...> psram before/after=<...>/<...>
result=pass|fail
```

**Gate:** Normal inbound and outbound fetches, framework retry behavior,
failure parsing, Wi-Fi/app cancellation, stale-request handling, and repeated
heap cleanup pass. Only then mark Phase 4 complete and create the phase commit.

Do not mark Phase 4 formally accepted until this deferred gate is completed.

## Expected logs

The normal path should become observable as:

```text
bus_service: KMB stops submitted route=102 bound=I request=... framework_id=...
crystal_http: request attempt id=... attempt=1/10
crystal_http: TLS connected id=...
crystal_http: body received id=... bytes=...
crystal_http: request completed id=... attempts=1 status=200
bus_service: KMB stops response ... status=200 body=... attempts=1 error=ESP_OK
bus_service: found ... KMB stops
```

The failure path must show the final framework error and a single bus error;
the app must remain on a usable stop page. A network-loss run should show
cancellation near the disconnect event, no later attempts for that framework
request, one response release, and no stale stop rows.
