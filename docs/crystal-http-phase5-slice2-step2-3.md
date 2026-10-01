# Slice 2 Steps 2.2 and 2.3 - CTB Parser and Cache

**Status:** Implemented; device route refresh and cache save evidence captured.

## S2.2 parser

`bus_ctb_parse_route_variants()` is independent of transport and app code. It
validates the CTB provider code, bounded route identity, optional service type,
and explicit `bound`/`dir` values. It accepts `I`, `O`, `inbound`, and
`outbound`; an invalid direction rejects that record. A missing direction
creates inbound and outbound identities with empty terminal fields. Terminal
strings are copied into bounded, terminated fields only when the record carries
an explicit direction.

Duplicate identities use `route + CTB + bound + service_type`. The first
record wins, except that a later explicit terminal record completes an earlier
direction-only empty placeholder. Malformed JSON, missing/non-array data, and
an all-invalid non-empty array return controlled errors. An empty array returns
`ESP_OK` with no output allocation.

Fixtures cover valid directions, the documented direction-less route list,
invalid records, oversized strings, duplicates, malformed JSON, missing data,
non-array data, and an empty result. Run both host checks:

```text
/private/tmp/ctb_parser_check components/bus_service/fixtures
/private/tmp/ctb_route_variant_check components/bus_service/fixtures
```

The focused route-variant checker reports 10 passing cases, including route
filter identity rejection and one free of every allocated output.

## S2.3 cache integration

The CTB catalog path now parses its bounded `crystal_http` body through the
provider parser before adding route groups or normalized variants. KMB records
remain in the same provider-qualified store. Duplicate detection uses route,
operator, bound, and service type, and explicit terminal data can replace only
an earlier empty placeholder with the same identity.

The serialized cache version is now `7` because the normalized variant bytes
and service-type handling changed. Cache writes continue through the temporary
file and atomic rename path. Cache load and save validate NUL termination,
operator, bound, service type, and duplicate identities before accepting the
record set.

## Remaining gate

The device refresh capture recorded a successful CTB route request at
`/route/CTB`: HTTP 200, 112270 bytes, one attempt, and one framework response
release. The parser resolved 814 CTB route records and the combined catalog
saved 1054 routes with 2418 provider variants and both providers successful.
The same run selected CTB route 10 outbound and parsed 37 stops.

That capture also exposed a watchdog warning while the cache duplicate check
scanned the 2418 variants. The validation loops now yield periodically to the
FreeRTOS scheduler; one follow-up device refresh is still needed to confirm
the warning is gone. The filesystem rename fallback reported `errno=5` but
completed successfully and the cache was saved.
