# Slice 2 Step 2.4 - Search and Stop Request Handoff

**Status:** Implemented; CTB inbound/outbound device verification passed

S2.4 uses the normalized `bus_route_variant_t` as the handoff between Search
and the stop page. A result row stores the provider, bound, and service type
in its LVGL user data. Selecting the row restores the matching cached variant
before queuing `bus_service_request_stops()`. If that exact identity is
missing, selection stops without a request; the route label is never used to
infer the provider or direction.

Search rows show the provider, direction, and available English destination
and origin, with destination first. The stop page identifies the selected route
with its provider and direction, and includes the English destination when the
normalized record contains one.
An empty CTB destination remains empty because the public CTB route catalog
does not associate a terminal with its direction.

Diagnostics now carry the complete identity through the handoff:

```text
Route variant selected: route=<route> op=<n> bound=<I|O> service_type=<n> destination=<...>
CTB stops: submitting route=<route> op=<n> bound=<I|O> service_type=<n> url=<...>
CTB stops submitted route=<route> op=<n> bound=<I|O> service_type=<n> ...
CTB stops transport complete route=<route> op=<n> bound=<I|O> service_type=<n> ...
CTB stops parsed route=<route> op=<n> bound=<I|O> service_type=<n> ...
```

The existing request identity checks compare route, operator, bound, and
service type before accepting a stop result. Late results are freed and
ignored when the selected page identity has changed.

## Host verification

The following checks pass from the repository root:

```text
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh && idf.py build
/private/tmp/ctb_parser_check components/bus_service/fixtures
/private/tmp/ctb_route_variant_check components/bus_service/fixtures
git diff --check
```

The current build produced `build/crystal_os.bin`.

## Device evidence

The flashed device completed both CTB directions for route 10:

```text
route=10 op=1 bound=I service_type=1
url=.../route-stop/CTB/10/inbound
status=200 attempts=1 body=4812 parsed count=40

route=10 op=1 bound=O service_type=1
url=.../route-stop/CTB/10/outbound
status=200 attempts=1 body=4458 parsed count=37
```

Framework responses `id=4` and `id=5` were each released once, and the app
accepted the matching stop lists. The same run completed the route catalog with
1,054 routes and 2,418 provider variants, with no watchdog warning, retry, or
stale-result message. CTB destinations are empty as expected because the
public `/route/CTB` response does not associate destinations with direction.

## Device gate

The remaining S2.4 follow-up is a shared-label check: select a KMB row with
the same printed route number as CTB and confirm the provider-qualified choices
remain independent.
