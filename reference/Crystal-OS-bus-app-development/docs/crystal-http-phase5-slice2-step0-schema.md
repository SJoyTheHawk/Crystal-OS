# Slice 2 Step 0 - CTB Route Schema Evidence

**Status:** Recorded; direction association is unavailable in the public route
list response, so CTB destinations remain empty until an authoritative mapping
is confirmed.

## Endpoint

The current route catalog request is:

```text
https://rt.data.gov.hk/v2/transport/citybus/route/CTB
```

The provider route API documentation describes the route-list payload as a
`RouteList` envelope whose `data` value is an array. Each route record has the
following fields and types:

| JSON field | Type | Recorded meaning |
| --- | --- | --- |
| `co` | string | Provider code, `CTB` |
| `route` | string | Printed route label |
| `orig_en` | string | English origin |
| `orig_tc` | string | Traditional Chinese origin |
| `orig_sc` | string | Simplified Chinese origin |
| `dest_en` | string | English destination |
| `dest_tc` | string | Traditional Chinese destination |
| `dest_sc` | string | Simplified Chinese destination |
| `data_timestamp` | string | Provider timestamp |

The provider route records do not contain `bound` or `service_type`. The
redacted fixture contains two records for route `1` with reversed terminals;
the payload therefore demonstrates different terminal pairs but does not prove
which record is inbound or outbound. No direction is inferred from array order,
route numbering, or terminal text.

## Empty response

`data: []` is the explicit empty result. A missing or non-array `data` field is
invalid input for the later parser gate.

## Decision

The public `/route/CTB` response cannot establish the required identity

```text
route + CTB + bound + service_type
```

for a destination record. A route-level origin/destination pair is useful
display metadata, but it is not safe to copy into both bound-specific records:
that would claim a direction association the provider did not return. Slice 2
therefore keeps normalized CTB destination fields empty and does not copy KMB
values. The S2.1 transport handoff may capture this response, but parser, cache
replacement, and app integration remain blocked until a provider response
explicitly associates each terminal pair with a direction.

## S2.1 transport handoff

The CTB route-list request now uses a dedicated `crystal_http` owner
(`CTB_ROUTE_VARIANTS_OWNER_ID`) and a bounded 512 KiB PSRAM handoff body. The
callback copies bytes, records status/error/attempts, logs the bus and framework
request IDs, releases the framework response exactly once, and signals the bus
worker. It does not parse JSON or call the app. Global cancellation and network
loss cancel this owner as well.

The `CRYSTAL_HTTP_PHASE5_CTB_ROUTE_FORCE_FAILURE` CMake switch selects a
controlled invalid endpoint for failure testing. Source compilation passed with
the switch enabled. Device success/failure response-release evidence remains
pending because this host cannot resolve the provider endpoint.

Source references:

- Citybus API specifications, Route API and response fields:
  `https://www.citybus.com.hk/datagovhk/bus_eta_api_specifications.pdf`
- Citybus data dictionary, Route field definitions:
  `https://www.citybus.com.hk/datagovhk/bus_eta_data_dictionary.pdf`
