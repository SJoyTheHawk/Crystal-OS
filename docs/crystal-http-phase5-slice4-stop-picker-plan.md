# HTTPS Phase 5 — Slice 4: Stop Picker and Stop Names

**Status:** Planned; Slice 3 route metadata and destination presentation are
complete. The TLS AES memory follow-up is implemented separately and must be
included in the device baseline for this slice.

**Previous slice:** [Slice 3 — normalized route metadata](crystal-http-phase5-slice3-normalized-route-metadata-plan.md)

**Code guide:** [Slice 4 code guide](crystal-http-phase5-slice4-stop-picker-code-guide.md)

## Decision

The next slice completes the first useful drill-down flow:

```text
Search → provider/direction row → route-stop list → named stop row
```

The selected `bus_route_variant_t` remains the request identity. Stop names,
coordinates, and localized labels enrich a stop row; they never determine the
route, provider, direction, service type, or stop URL.

The slice ends when a user can select a named stop from a selected KMB or CTB
route in both directions. ETA requests, favorite persistence, and nearby
search remain later slices.

## Scope

This slice includes:

1. A complete stop-list contract with provider source ID and route sequence.
2. Provider-specific stop-detail adapters for KMB and CTB, after endpoint
   shapes are confirmed against live responses and fixtures.
3. Bounded stop-name and coordinate caching with active-language selection.
4. A scrollable stop-picker page with loading, partial, empty, failure,
   offline, cancellation, and retry states.
5. Request correlation and cancellation for route-stop and stop-detail work.
6. Host fixtures and device acceptance for KMB, CTB, shared route labels,
   both directions, reboot, and Wi-Fi loss.

This slice does not include:

- ETA parsing, refresh timers, or an ETA board;
- saving, editing, or deleting favorites;
- assigning CTB route-level terminal pairs to a direction;
- inferring a stop identity from its display name or sequence;
- language switching UI, although the service stores EN/TC values and uses a
  single active-language selector;
- nearby search or bulk geolocation discovery.

## Current baseline

The route-stop requests already work through `crystal_http` for KMB and CTB.
They return ordered source stop IDs and sequence numbers in `bus_stop_t`, but
the app currently renders `Stop N` placeholders. `REQ_TYPE_STOP_DETAIL` is
declared and queued, while the worker still discards it. The app has one stop
page and request identity guards, but it does not yet retain a selected stop
or display resolved names.

The baseline also includes the software-AES TLS configuration from
`crystal-http-tls-aes-memory-fix.md`. Record its successful device result
before diagnosing stop-detail failures; transport allocation and provider
parsing must remain distinguishable.

## Contracts

### Stop identity

Every stop row is identified by:

```text
route + operator + bound + service_type + provider_stop_id
```

`seq` controls display order only. A stop name is never an identity key.
KMB and CTB IDs remain provider-qualified, even when their text happens to
match.

### Stop detail

The service owns resolved details and returns copied values:

```c
typedef struct {
    char stop_id[20];
    char name_en[60];
    char name_tc[60];
    float lat;
    float lon;
    bool has_coordinates;
    bool resolved;
} bus_stop_detail_t;
```

The existing `bus_stop_t` may carry these fields if that keeps ownership clear.
Do not expose mutable cache pointers to LVGL. Missing localized text is valid;
the UI uses the other language or a bounded `Stop <sequence>` fallback.

### Request lifetime

Every route-stop and stop-detail event carries the request ID and full route
identity. The app accepts a result only when the page generation and selected
variant still match. Leaving the page, selecting another direction, Wi-Fi
loss, pause, and destroy cancel active work. Late successful arrays and detail
events are freed or ignored according to the existing Slice 2 ownership rule.

## Work packages

### S4.0 — Freeze stop contracts and capture provider fixtures

Record one successful KMB and CTB route-stop response in both directions,
plus the corresponding detail responses. Confirm the exact detail URL,
localized field names, coordinate representation, empty-name behavior, and
HTTP status semantics from live data before writing provider code. Add fixtures
for a normal stop, missing TC/EN text, invalid coordinates, duplicate IDs,
and an empty route-stop response.

**Exit:** fixture fields and URL templates are documented; no provider detail
endpoint is guessed from a route-stop response.

### S4.1 — Complete the stop-list and detail service contracts

Extend `bus_service.h` and the provider headers with explicit stop-detail
events and bounded status values. Keep `bus_stop_t` source IDs and sequence
numbers intact. Add a provider-neutral detail parser interface while keeping
JSON field names inside KMB and CTB adapters.

**Exit:** host fixtures can parse route-stop and detail payloads into copied
records without LVGL or network dependencies.

### S4.2 — Implement bounded detail transport and cache

Implement the worker branch for `REQ_TYPE_STOP_DETAIL` through
`crystal_http`, using the same handoff, retry, timeout, cancellation, and
PSRAM ownership rules as route-stop requests. Add a bounded in-memory cache
keyed by provider-qualified stop identity and language-independent fields.

Resolve visible rows lazily or in a small bounded batch. Do not enqueue one
request for every stop immediately; a route can contain many stops and the
request queue is finite. Deduplicate pending detail requests and retain a
successful cached value across page rebuilds. A failed detail request must
leave the route-stop list usable.

**Exit:** repeated visits reuse cached names, cancellation releases all
buffers, and offline requests terminate with `Waiting for network`.

### S4.3 — Build the stop-picker presentation

Replace placeholder rows with sequence, active-language name, and a compact
secondary provider/source indicator where needed. Preserve route title and
destination formatting from Slice 3. Keep rows deterministic in route
sequence order and make the entire row selectable.

Required states:

- route-stop loading;
- route-stop success with names still resolving;
- individual name pending;
- partial detail failure with fallback text;
- empty route-stop result;
- provider/network failure;
- Wi-Fi loss while either request is active;
- Back/cancellation followed immediately by another direction.

Network-loss and loading messages are centered in the list area. A stale
detail event must never replace a newly selected direction's rows.

**Exit:** KMB and CTB route 10 (or an equivalent fixture) show ordered named
stops in both directions, with no stale rows after rapid replacement.

### S4.4 — Stop selection boundary

Add a selected-stop state containing the full composite identity and copied
display data. Tapping a row should produce a service-ready identity for the
next ETA slice, but must not submit an ETA request yet. Back returns to the
route-stop page and preserves its loaded data when safe.

**Exit:** logs show the selected provider stop ID, route, bound, and service
type; the displayed name is informational and cannot alter that identity.

### S4.5 — Acceptance and handoff

Run host fixtures, build checks, and device tests for cache cold start,
reboot, both providers, both directions, shared route numbers, detail cache
reuse, Wi-Fi loss, cancellation, rapid replacement, and low-memory TLS
requests. Record heap before/after transport and confirm no AES allocation
errors or watchdog resets.

## Exit criteria

The slice is complete when:

- route-stop arrays retain source IDs and route order;
- KMB and CTB detail adapters produce bounded EN/TC names and optional
  coordinates;
- stop-detail requests use `crystal_http` and honor cancellation/network loss;
- the stop picker renders named rows and safe fallback states;
- selected-stop identity includes route, provider, bound, service type, and
  source stop ID;
- stale results cannot replace a newer direction or page;
- cache reuse reduces repeat detail requests without serving another
  provider's stop;
- host fixtures, `idf.py build`, `git diff --check`, and device acceptance
  pass.

## Evidence template

```text
slice=4
firmware commit=<sha>
route=<route> operator=<KMB|CTB> bound=<I|O> service_type=<n>
route_stops=<n> named=<n> fallback=<n>
detail_cache=<cold|warm> language=<en|tc>
selected stop identity=<provider stop id + route + bound + service type>
wifi loss/cancellation=<pass|fail>
tls aes allocation errors=<count>
watchdog=<none|observed>
result=<pass|fail|deferred>
known follow-up=<...>
```
