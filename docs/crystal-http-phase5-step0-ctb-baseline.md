# Phase 5 Step 0 — CTB Baseline

**Status:** Schema recorded; device and live-network measurements deferred
**Scope:** CTB route and route-stop response shape before code changes

Step 0 is documentation only. No CTB transport or parser code is changed by
this record.

## Requests

The current service uses these CTB endpoints:

```text
route list:
https://rt.data.gov.hk/v2/transport/citybus/route/ctb

route stops (provider spelling):
https://rt.data.gov.hk/v2/transport/citybus/route-stop/ctb/{route}/{inbound|outbound}
```

The current legacy service constructs the same path with an uppercase `CTB`
company segment. The device capture must record the exact generated URL and
whether the provider accepts that spelling.

The route-stop path uses the normalized application direction `I` as
`inbound` and `O` as `outbound`.

## Recorded schema

The CTB route list contains records with the following fields. Traditional
Chinese fields are included in the provider response and must remain available
to a later normalized route model.

```json
{
  "data": [
    {
      "co": "CTB",
      "route": "969",
      "orig_en": "<origin>",
      "orig_tc": "<origin-tc>",
      "dest_en": "<destination>",
      "dest_tc": "<destination-tc>"
    }
  ]
}
```

The route-stop response contains one record per stop and preserves route order:

```json
{
  "data": [
    {
      "co": "CTB",
      "route": "969",
      "dir": "I",
      "seq": 1,
      "stop": "002536",
      "data_timestamp": "<timestamp>"
    }
  ]
}
```

`stop` is the CTB source identifier and must not be merged with a KMB stop id
by string equality. Names and coordinates are not part of this route-stop
record; they come from the provider stop detail dataset and remain a later
step.

The no-result behavior still needs a device capture for this project. The
parser contract in Step 2 therefore treats a missing or non-array `data` field
as controlled invalid input and an empty array as an explicit empty result.

## Device capture record

Run this against the current firmware once a device and network are available:

```text
route=<...> direction=I generated_url=<...> status=<...> body_bytes=<...>
stop_count=<...> sequence_order=<...>
internal_heap_before=<...> internal_heap_after=<...>
largest_internal_before=<...> largest_internal_after=<...>
psram_before=<...> psram_after=<...>

route=<...> direction=O generated_url=<...> status=<...> body_bytes=<...>
stop_count=<...> sequence_order=<...>
internal_heap_before=<...> internal_heap_after=<...>
largest_internal_before=<...> largest_internal_after=<...>
psram_before=<...> psram_after=<...>
```

The current host session could not resolve `rt.data.gov.hk`, so the inbound
and outbound HTTP status/body and heap measurements remain **deferred**. This
record must be updated with one successful response for each direction before
Step 3 chooses a measured CTB body limit.
