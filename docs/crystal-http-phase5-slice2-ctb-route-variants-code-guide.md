# Tracked Slice 2 Code Guide — CTB Route Variants and Destinations

**Status:** Ready for implementation, one gate at a time  
**Plan:** [Tracked Slice 2 implementation plan](crystal-http-phase5-slice2-ctb-route-variants-plan.md)

This guide is the executable companion to the Slice 2 plan. Do not implement
the later gate until the current gate has a recorded passing result.

## Contracts that must remain true

Use `bus_route_variant_t` as the normalized output. Its identity is:

```text
route + BUS_OP_CTB + bound + service_type
```

The route label remains a display group. It is never a cache key by itself.
For every CTB destination copied into the record, the parser must also have
validated the matching explicit provider direction. Keep missing fields empty.

`bus_service` owns the response handoff while parsing. The app owns successful
event arrays after LVGL delivery. `crystal_http` owns the framework response
until the callback releases it exactly once.

## S2.0 — Freeze the authoritative CTB schema

**Files:** documentation and a redacted fixture only.

1. Identify the CTB route-variant URL already used or documented by the
   provider. Do not assume `/route/ctb` supplies a direction association.
2. Capture one route with different inbound and outbound terminals.
3. Record the exact JSON names and types for route, operator, bound, service
   type, origin, destination, and language fields.
4. Record how an empty or unavailable result is represented.

**Gate:** The fixture proves which record owns each terminal. If it does not,
stop here and leave the current empty CTB destination behavior unchanged.

## S2.1 — Add the CTB request handoff

**Files:** `components/bus_service/src/bus_service.c` and its CMake file only
if a temporary test switch is required.

Create a route-variant handoff context containing the bus request id, framework
request id, route, operator, bound, service type, status, attempts, semaphore,
and bounded PSRAM body. Submit exactly one `crystal_http_get()` request using a
dedicated owner id and the endpoint recorded in S2.0.

The callback may copy the bounded body and signal the bus worker. It must not
parse JSON, call LVGL, invoke the app listener, or free the context owned by the
bus worker. Log both request ids and release the framework response once.

**Gate:** Build passes and a successful response plus a controlled HTTP or
transport failure each produce one terminal handoff with one response release.

## S2.2 — Add parser fixtures before app integration

**Files:** `components/bus_service/src/bus_provider_ctb.c`, its header, and
`components/bus_service/fixtures/`.

Add a route-variant parser with these outcomes:

- valid inbound/outbound records produce separate normalized variants;
- malformed JSON returns a parse error;
- missing or non-array data returns a controlled error;
- an empty array is a successful empty provider result;
- invalid direction or route identity is rejected;
- oversized strings are truncated safely and remain terminated;
- duplicate `route + bound + service_type` records collapse deterministically.

Keep this parser independent of the transport and app. Extend the host checker
or add a focused checker so every fixture can be run without an ESP32.

**Gate:** Fixture output shows exact route, CTB operator, bound, service type,
origin, destination, and language fields; every allocated output has one free.

## S2.3 — Replace synthetic CTB cache variants

**Files:** `components/bus_service/src/bus_service.c` and
`components/bus_service/include/bus_service.h` only if the contract needs a
documented field correction.

Route catalog integration must:

1. preserve all KMB records;
2. remove or replace only CTB records for the refreshed route;
3. key duplicate detection by route, operator, bound, and service type;
4. copy CTB destinations only from the validated parser output;
5. bump the serialized cache version when record bytes change;
6. load a temporary validated cache before atomic replacement.

Do not merge equal printed labels across operators. A KMB route 10 and a CTB
route 10 remain separate variants even when they share a visible search row.

**Gate:** A cache reload reproduces the same variant count, identities, and
destination strings as the in-memory result.

## S2.4 — Connect Search selection to stops

**Files:** `components/bus_app/src/bus_app.cpp` and service call sites.

When a CTB row is selected, copy the normalized variant into page state and
queue its stop request with CTB, the exact bound, and service type. Render the
provider-qualified destination from the selected record. Keep empty provider
fields visibly empty or use the existing placeholder; do not copy KMB text.

**Gate:** A shared KMB/CTB label shows independent choices, and device logs
show the selected CTB identity matching the route-stop URL and result identity.

## S2.5 — Exercise cancellation, stale identity, and reload

Repeat these cases on the target device:

```text
CTB inbound success
CTB outbound success
Back during CTB variant request
select route B while route A is pending
Wi-Fi loss during the request
cache reload after reboot
```

For each case record status, error, attempts, body bytes, response releases,
heap/PSRAM before and after, and whether any stale destination reached the UI.

**Gate:** One terminal event and one response release per request; stale and
cancelled results are ignored; the app remains usable after every case.

## S2.6 — Build and sign-off

Run from the repository root:

```bash
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
PYTHONPATH=/private/tmp idf.py build
git diff --check
/private/tmp/ctb_parser_check components/bus_service/fixtures
rg -n "esp_http_client|esp_tls|esp_crt_bundle|http_get_json" \
  components/bus_service/src/bus_service.c
```

The source check may find the legacy helper for operations outside this slice.
The CTB route-variant branch must call `crystal_http` and must not call the
legacy direct helper. Record the firmware commit, test switch values, and
known follow-up in a new evidence record before marking Slice 2 accepted.
