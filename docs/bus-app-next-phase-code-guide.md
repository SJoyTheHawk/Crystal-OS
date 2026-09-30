# HTTPS/TLS Phase 5, Slice 1 — Bus Service Contracts and CTB Normalization Code Guide

**Status:** Ready for implementation
**Phase:** HTTPS/TLS Phase 5, first implementation slice
**Scope:** Bus-service groundwork, CTB route/stop normalization, and the first
CTB operation through `crystal_http`
**Parent plans:**

- [Incremental Global HTTPS/TLS plan](Incremental-Global-HTTPS:TLS-plan.md)
- [Bus app integration plan](bus-app-integration-plan.md)

This guide defines the first Phase 5 implementation slice after the KMB catalog and KMB
route-stop migration. It deliberately leaves the KMB runtime path unchanged and
keeps the final Phase 4 failure/lifetime acceptance deferred.

This is not the complete Phase 5. Later Phase 5 slices will migrate KMB route
variants, KMB ETA, the CTB catalog, CTB route variants, CTB ETA, and any
remaining bus HTTPS calls.

## Outcome of this slice

At the end of this slice:

- KMB catalog and KMB stop requests still use `crystal_http`.
- Bus events have one documented ownership and delivery rule.
- KMB and CTB route variants and stops can be represented by one normalized
  model without losing provider-specific identifiers.
- One CTB operation, preferably CTB route stops, uses `crystal_http` with an
  explicit body limit, retry policy, cancellation owner, and bounded handoff.
- The app can distinguish a current result from a stale result by request id
  and normalized identity.
- No new code creates or cleans up an `esp_http_client` handle.

Do not add ETA merging, favorites, language switching, or Nearby in this
slice. Those features depend on these contracts.

## Read the existing code before editing

The current implementation is concentrated in:

| Area | File | Role |
| --- | --- | --- |
| Public service contract | `components/bus_service/include/bus_service.h` | Requests, normalized records, events, listener API |
| Worker and provider paths | `components/bus_service/src/bus_service.c` | Queue, KMB handoffs, CTB direct paths, parsing |
| Route index | `components/bus_service/include/bus_routes.h` and `src/bus_routes.c` | Prefix filtering and runtime catalog |
| App state and pages | `components/bus_app/src/bus_app.cpp` | LVGL pages, request generations, stale-result checks |
| Shared transport | `components/crystal_http/include/crystal_http.h` and `src/crystal_http.c` | Queue, TLS, PSRAM body, retry, cancellation, release |

`bus_service.c` still contains the legacy direct helper for CTB and other
operations. Keep that path isolated while this slice is implemented, then
remove it only when the corresponding operation has moved to `crystal_http`.
The KMB stop handoff is the ownership pattern to copy.

The older [bus-app-code-guide.md](bus-app-code-guide.md) describes the target
application shape, but its direct HTTP examples are obsolete. For networking,
this guide and the TLS plan take precedence.

## Ownership and threading rules

There are three ownership domains:

1. `crystal_http` owns its active client and response body until the callback
   returns or `crystal_http_response_release()` is called.
2. `bus_service` owns a handoff context and any copied response body while the
   worker parses it.
3. `bus_app` owns copied event payloads after the event is delivered on the
   LVGL task.

The callback runs on the framework worker. It must only copy bounded data into
the handoff context, record status/error/attempts, release the framework
response exactly once, and signal the bus worker. It must not call LVGL, invoke
the app listener, parse JSON, or free a context that the bus worker may still
use.

`post_event()` remains the only service-to-app delivery path. It must copy the
event, queue delivery on the LVGL task, and free payloads if the listener is
absent or queueing fails. The listener must be detached before app destruction.

Every request carries both identities:

```text
bus request id       identifies the user operation
framework request id identifies the HTTPS operation
normalized identity  route + operator + bound + service type (+ stop id)
```

The app accepts an event only when its bus request id and current page identity
match. A late callback may clean up its own context, but it must never replace
the current page's rows.

## Normalize the public data model first

Update `bus_service.h` before adding another provider path. Preserve fixed-size
POD records so the service remains C-only and payload sizes are predictable.

The normalized route variant identity is:

```text
route + operator + bound + service_type
```

The normalized stop identity is provider-specific:

```text
route + operator + bound + service_type + source stop id
```

Do not assume KMB and CTB stop IDs are interchangeable. A co-operated stop can
hold one source ID per operator and later be merged at the app layer.

The public records should contain at least:

```c
typedef struct {
    char route[5];
    bus_operator_t op;
    bus_direction_t bound;
    uint8_t service_type;
    char origin_en[48];
    char destination_en[48];
    char origin_tc[48];
    char destination_tc[48];
} bus_route_variant_t;

typedef struct {
    char stop_id[20];
    uint16_t sequence;
    bus_operator_t op;
    char name_en[60];
    char name_tc[60];
    float latitude;
    float longitude;
    bool resolved;
} bus_stop_t;
```

Use the repository's existing field names and ABI where possible. If a field is
renamed, update all C and C++ call sites in the same change and record the
compatibility impact. Do not silently overload `stop_id` with a cross-provider
identity.

Extend request records so stop and ETA requests carry `op`, `bound`, and
`service_type`. An ETA request without direction is unsafe because a provider
can return both directions for one stop.

## Provider adapter boundary

Keep provider-specific JSON parsing separate from normalization. Add internal C
helpers in `bus_service.c` first; split them into `bus_provider_kmb.c`,
`bus_provider_ctb.c`, and `bus_normalize.c` only when the boundaries become
large enough to test independently.

Each adapter should expose the equivalent of:

```c
typedef struct {
    bus_operator_t op;
    const char *url;
    size_t body_limit;
    uint32_t timeout_ms;
    uint8_t max_attempts;
    uint32_t owner_id;
} bus_provider_policy_t;

static esp_err_t parse_route_variants(const uint8_t *body, size_t len,
                                      bus_route_variant_t **out,
                                      uint8_t *count);

static esp_err_t parse_route_stops(const uint8_t *body, size_t len,
                                   bus_operator_t op,
                                   bus_stop_t **out,
                                   uint16_t *count);
```

The parser contract is strict:

- malformed JSON returns a parse error;
- missing or non-array `data` returns a controlled error;
- an empty array returns an explicit empty result;
- invalid records are skipped only when the remaining records are usable;
- a response with no valid records never becomes a successful event;
- output arrays are allocated in PSRAM and have one documented owner.

Before coding the CTB parser, capture and inspect one real route response and
one real route-stop response. Confirm field names, direction values, route
codes, stop IDs, sequence types, and whether the response contains provider
metadata. Do not infer the schema from the KMB payload.

## First CTB migration: route stops

Use CTB route stops as the first CTB operation because it has a clear visible
result and exercises the same handoff that KMB stops already proved.

### Request path

1. `bus_service_request_stops()` validates route, operator, bound, and service
   type, assigns a bus request id, and queues one request.
2. The worker builds the CTB URL from the normalized request.
3. The worker allocates a CTB handoff context containing the bus request id,
   normalized identity, framework id, semaphore, status/error/attempts, and
   bounded PSRAM body.
4. The worker submits one `crystal_http_get()` request with a CTB-specific
   policy. It does not call the legacy `http_get_json()` helper.
5. The callback copies the body, releases the framework response, and signals
   the worker.
6. The worker parses the copied body with the CTB adapter, emits one terminal
   event, and cleans up the handoff exactly once.

Use a dedicated owner id such as `CTB_STOPS_OWNER_ID`; do not reuse the KMB
owner. Start with an interactive policy sized from measured CTB responses:

```text
timeout: 8 seconds
max attempts: 3
backoff: 500 ms, capped at 2 seconds
keep-alive: disabled initially
body limit: measured maximum plus a bounded safety margin
```

The policy is a starting point. Change it only when a device measurement
justifies the change.

### Cancellation

Network loss, app pause/destroy, Back, and a superseding stop request must call
`crystal_http_cancel_owner(CTB_STOPS_OWNER_ID)` and mark the request stale.
Cancellation must prevent another retry while there is no network lease. A
late callback still releases its response and signals only a live context.

The app must show a reconnect or cancelled state and remain usable. It must not
display rows from the cancelled CTB request.

## App integration contract

The app should remain provider-neutral after service delivery:

- Search rows display normalized route variants and operator labels.
- Stop rows display sequence and resolved name when available, otherwise a
  clearly marked placeholder.
- Back returns to the previous page and cancels the page owner.
- A result is accepted only for the active page generation.
- Provider-specific source IDs remain hidden inside the normalized stop record.

Do not merge KMB and CTB arrays by position. Merge only by an explicit
normalized identity or a later stop-resolution identity that includes operator
source IDs.

## Implementation gates

### Gate A — Contract and ownership review

Change only headers, internal types, and event ownership documentation.

Checks:

- `bus_service.h` compiles from both C and C++.
- request records include operator, bound, and service type where required;
- every event payload has one owner and one cleanup path;
- no LVGL pointer enters a service or framework callback.

### Gate B — CTB parser fixture

Add a small fixture or host-side parser check for one valid route response, one
valid stop response, malformed JSON, missing `data`, empty `data`, and one
invalid record.

Checks:

- valid records preserve order and source IDs;
- invalid input returns a controlled error;
- every allocated output is freed by the documented owner.

### Gate C — CTB framework submission

Replace only CTB route-stop submission. Keep CTB parsing and UI behavior
unchanged until the framework handoff is proven.

Expected logs:

```text
bus_service: CTB stops submitted route=... bound=... bus_id=... framework_id=...
crystal_http: request attempt id=... attempt=1/3
crystal_http: response status=200 body=... bytes
bus_service: CTB stops response ... status=200 body=... attempts=1 error=ESP_OK
```

### Gate D — CTB parser and normalized event

Reconnect the CTB parser and emit `BUS_EVT_STOPS_LIST` with the normalized
records. Validate inbound and outbound routes and one empty/error response.

### Gate E — Cancellation and stale selection

Disconnect Wi-Fi during connection and body read, press Back during an active
request, and select another route before completion. Confirm one release, no
late rows, no retry storm, and stable heap/PSRAM.

### Gate F — Direct-client boundary

After CTB route stops pass, remove only that operation's direct-client path.
Leave unrelated CTB and ETA direct paths untouched until their own migration.
Run a source check that the CTB stop branch contains no `esp_http_client`,
`esp_tls`, or certificate-bundle ownership.

## Build and device checks

From the repository root:

```bash
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
idf.py build
git diff --check
```

Keep the Phase 3/4 temporary test options disabled for normal CTB work:

```bash
grep CRYSTAL_HTTP_PHASE build/CMakeCache.txt
```

Record for every device run:

```text
firmware commit=<sha>
bus request=<id> framework request=<id>
operator=<KMB|CTB> route=<route> bound=<I|O>
status=<...> error=<...> attempts=<...> body=<...>
stops=<...> response releases=<...>
heap/PSRAM before and after=<...>
result=pass|fail
```

Do not proceed to CTB ETA or co-operated-route merging until the CTB stop
operation passes Gates A–F. Return to the deferred KMB Phase 4 matrix after
the CTB and weather clients are available, as required by the parent plan.
