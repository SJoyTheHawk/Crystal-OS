# Bus standard-service catalog — November 18, 2026

## Provider policy

- KMB/LWB: retain only explicit string/numeric service 1 before duplicate
  collapse and joint matching. A service-2 row appearing first cannot replace
  service 1. A direction with no service 1 is excluded, without fallback.
- Citybus: the official route list has no service-type field. Normalize its
  origin/destination to outbound and reversed inbound, as Flutter does, and
  use internal service_type 1. Route-stop/ETA requests have no service argument.
  This selects the public canonical route directions; it does not assert that
  every Citybus route operates all day or that every ETA is a regular trip.
- routeFareList's CTB serviceType values are aggregator variants, not Citybus
  API service IDs. Legacy snapshot input accepts only variant 1; release route
  generation now uses official snapshots instead. Stop metadata still uses
  the October snapshots. Missing names continue through the existing live
  stop-details fallback.
- Flutter `services/route_merger.dart` keeps the first KMB/LWB service per
  route/direction; it does **not** explicitly filter to 1. Its `ctb_api.dart`
  creates two directions with internal serviceType 1. The P4 policy makes the
  KMB restriction explicit rather than relying on source ordering.

## Recorded inputs and output

The official responses report generated_timestamp 2026-11-18T03:59:23+08:00.
Compressed raw responses are stored in the historical catalog sources folder
outside the app package. Manifest source labels are the original endpoint URLs;
uncompressed and archive hashes are recorded.

| Input | Source records | Selected normalized records | Excluded variants |
|---|---:|---:|---:|
| KMB /route/ | 1,573 | 1,285 | 288 |
| Citybus /route/CTB | 409 | 818 directions | 0 |

After destination matching: 1,968 entries including 135 joint entries.
Every top-level and joint-part service_type is 1. Route JSONL, compact resident
search files, and keypad next-character indexes are regenerated together.
Existing saved favorites/recents are preserved; this change does not rewrite
previously saved provider identities or service types.

## Regenerate

Run from the repository root:

```sh
python3 tools/build_phase17_bus_catalog.py \
  --kmb-routes examples/bus_app/artifacts/bus_catalog/sources/kmb-routes-20261118.json.gz \
  --ctb-routes examples/bus_app/artifacts/bus_catalog/sources/ctb-routes-20261118.json.gz \
  --kmb-stops examples/bus_app/artifacts/bus_catalog/sources/kmb-stop-20261002.json.gz \
  --ctb-stops examples/bus_app/artifacts/bus_catalog/sources/hkbus-routeFareList-20261002.json.gz \
  --output examples/bus3p/assets/catalog \
  --generated-at 2026-11-18T03:59:23+08:00 \
  --source-revision official-routes-20261118-stops-20261002 \
  --kmb-route-label https://data.etabus.gov.hk/v1/transport/kmb/route/ \
  --ctb-route-label https://rt.data.gov.hk/v2/transport/citybus/route/CTB \
  --kmb-route-timestamp 2026-11-18T03:59:23+08:00 \
  --ctb-route-timestamp 2026-11-18T03:59:23+08:00 \
  --kmb-stop-timestamp 2026-10-02T22:37:17+08:00 \
  --ctb-stop-timestamp 2026-10-02T01:18:18Z
```

## Acceptance

Host checks: `test_phase17_bus_catalog.py`, `test_phase17_bus_package.py`,
and `test_phase17_bus_render.py`. Cover out-of-order services, special-only
directions, missing KMB service, numeric/string values, Citybus reverse
direction and explicit snapshot bounds, joint identities and keypad coverage.

On P4, install the rebuilt live package and search 1A, 101, 110 and a CTB-only
route. Open both available directions, check destinations, stop sequences and
ETA, then save/reopen one joint stop. New search selections should have only
service 1; old saved special-service entries remain until removed.
No firmware changes or new runtime APIs are required.
