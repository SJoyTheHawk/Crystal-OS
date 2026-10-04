# Slice 2 Step 2.5 - Lifecycle and Acceptance

**Status:** Lifecycle guards and diagnostics implemented; target-device acceptance pending

S2.5 verifies that route replacement, Back, Wi-Fi loss, and cache reload do
not allow a late result to update the visible stop page. The app invalidates
the visible request id before cancellation, and every accepted stop event must
match route, operator, bound, and service type. Cancelled events arriving
after that invalidation are logged and ignored.

## Lifecycle behavior

| Case | Expected behavior |
| --- | --- |
| CTB inbound success | One CTB stop response, one release, matching `bound=I`, stops shown |
| CTB outbound success | One CTB stop response, one release, matching `bound=O`, stops shown |
| Back during stop request | Request id cleared before cancellation; late result is stale and ignored |
| Route B while route A is pending | A is invalidated before cancellation; only B can update the stop page |
| Wi-Fi loss during request | CTB/KMB owners are cancelled; late error is ignored as stale; reconnect re-arms catalog bootstrap |
| Cache reload after reboot | Cache load reports the same route count, provider variant count, and provider mask |

## Diagnostics

The relevant acceptance lines are:

```text
bus_service: Stop cancellation requested kmb=<n> ctb=<n>
bus_service: Network disconnected; bus HTTP cancellation ctb_route=<n> kmb_stops=<n> ctb_stops=<n>
bus_app: Ignoring stale stop result id=<id> route=<route> op=<op> bound=<I|O> service_type=<n>
bus_app: Ignoring stale cancelled bus request id=<id>
bus_service: Route catalog cache: loaded <routes> routes, <variants> provider variants mask=0x<mask>
crystal_http: response released id=<id>
```

`crystal_http` remains responsible for one terminal callback and one response
release per framework request. `bus_service` owns copied response bodies and
the app frees successful event arrays after the matching event is accepted.
On reconnect, the connected event clears the service's network-loss gate so
cached route data can be used for new stop requests immediately.

## Existing device evidence

The prior S2.4 capture already covered successful CTB route 10 inbound and
outbound stop requests:

```text
route=10 op=1 bound=I service_type=1 status=200 body=4812 parsed count=40
route=10 op=1 bound=O service_type=1 status=200 body=4458 parsed count=37
```

It also showed a complete refresh with 1,054 routes and 2,418 provider
variants. A reboot cache-load capture, Back cancellation, route replacement,
and Wi-Fi-loss capture still need to be recorded on the target device before
the S2.5 gate can be marked passed.

## Host verification

The implementation must continue to pass:

```text
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh && idf.py build
/private/tmp/ctb_parser_check components/bus_service/fixtures
/private/tmp/ctb_route_variant_check components/bus_service/fixtures
git diff --check
```
