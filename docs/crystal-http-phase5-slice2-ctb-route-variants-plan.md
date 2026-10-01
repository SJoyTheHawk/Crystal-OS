# Tracked Slice 2 — CTB Route Variants and Destinations

**Status:** Planned; implementation not started  
**Owner boundary:** `bus_service` owns CTB request meaning and normalization;
`crystal_http` owns HTTPS, buffering, retries, cancellation, and response release.

This slice follows the signed Slice 1 CTB stop migration. It replaces the
direction-only CTB choices created by Step 3A with route variants whose
direction and terminal fields come from an authoritative CTB response.

Related records:

- [Slice 1 code guide](bus-app-next-phase-code-guide.md)
- [Slice 1 evidence and sign-off](crystal-http-phase5-step7-slice1.md)
- [Slice 2 code guide](crystal-http-phase5-slice2-ctb-route-variants-code-guide.md)
- [Bus app integration plan](bus-app-integration-plan.md)
- [Global HTTPS/TLS plan](Incremental-Global-HTTPS:TLS-plan.md)

## Problem and invariant

Step 3A correctly keeps CTB separate from KMB, but the current CTB catalog
response does not reliably associate its origin and destination with `bound`.
It therefore creates two CTB choices with empty destinations. A route number
is a display label and cannot be used to infer the provider, direction, or
terminal.

Every persisted and selected CTB variant must retain this identity:

```text
route + operator=CTB + bound + service_type
```

The displayed destination is valid only when it came from a CTB record that
explicitly carries the same direction. A missing provider field remains empty.

## Scope

The slice includes:

1. Confirming the authoritative CTB route-variant endpoint or response fields.
2. Adding one CTB request through `crystal_http` with a bounded handoff.
3. Parsing and validating direction-specific English and Chinese terminal data.
4. Replacing synthetic CTB destination fields in the normalized cache records.
5. Preserving duplicate KMB/CTB labels and the selected provider identity.
6. Validating cache reload, stop-request identity, cancellation, and stale data.

The slice does not include ETA, favorites, language switching, NWFB, or the
deferred KMB Phase 4 failure/lifetime matrix.

## Traceable work packages

### S2.0 — Schema and endpoint evidence

Record the CTB URL, request parameters, response fields, direction encoding,
and one redacted fixture for a route with different inbound and outbound
terminals. If no authoritative direction association exists, stop and record
that result rather than copying fields or guessing from route numbering.

**Output:** fixture, field map, and a decision recorded in the code guide.

### S2.1 — CTB route-variant transport

Submit the confirmed request with a dedicated owner id and request context.
Use `crystal_http`, bounded PSRAM body storage, one framework response release,
and request/response ids in diagnostics. Preserve the existing catalog
bootstrap sequence and cancellation behavior.

**Output:** successful and controlled-failure transport evidence.

### S2.2 — Provider parser and normalized records

Parse valid, malformed, missing-data, empty-data, and invalid-record fixtures.
Map only explicit CTB direction values to `BUS_DIR_INBOUND` or
`BUS_DIR_OUTBOUND`. Populate `orig_en`, `dest_en`, `orig_tc`, and `dest_tc`
with bounded copies. Preserve `service_type` and reject unusable identities.

**Output:** parser fixture results and normalized variant table.

### S2.3 — Cache and duplicate-label integration

Merge CTB variants by `route + CTB + bound + service_type`, retain KMB records
with the same printed label, and bump the cache version if the serialized
layout changes. Write a validated temporary cache before replacing the active
cache. Reload the cache and compare identities and destinations byte-for-byte.

**Output:** cache version decision and reload comparison.

### S2.4 — Search and stop-request handoff

Display CTB origin/destination fields on the existing variant rows. Selecting a
row must copy the complete normalized variant into the stop request. The stop
request must retain CTB, `bound`, and `service_type`; it must never derive them
from the route label.

**Output:** device logs showing the selected CTB identity and matching stop URL.

### S2.5 — Lifecycle and acceptance

Repeat normal inbound/outbound selection, Back cancellation, route replacement,
Wi-Fi loss, and cache reload. Confirm one terminal event, one response release,
no stale rows, and stable heap/PSRAM. Record the flashed firmware commit.

**Output:** signed Slice 2 evidence record and updated status links.

## Exit criteria

The slice is accepted for handoff only when all of these are true:

- both CTB directions have provider-confirmed identity and destination data;
- a shared KMB/CTB label still yields separate provider choices;
- cache reload preserves route, operator, direction, service type, and names;
- stop requests carry the selected CTB identity;
- malformed, empty, cancellation, network-loss, and stale results terminate safely;
- `idf.py build`, parser fixtures, `git diff --check`, and source ownership checks pass;
- the evidence record names the flashed firmware commit and remaining risks.

If the CTB source cannot provide an explicit direction-to-destination mapping,
the slice stops at S2.0 and remains unaccepted.

## Evidence template

```text
slice=2
firmware commit=<sha>
schema source=<url or documented endpoint>
route=<route> operator=CTB bound=<I|O> service_type=<n>
origin_en=<...> destination_en=<...>
origin_tc=<...> destination_tc=<...>
bus request=<id> framework request=<id>
status=<...> error=<...> attempts=<...> body=<...>
cache version=<...> reload=<pass|fail>
response releases=<...>
heap/PSRAM before and after=<...>
result=<pass|fail|deferred>
known follow-up=<...>
```
