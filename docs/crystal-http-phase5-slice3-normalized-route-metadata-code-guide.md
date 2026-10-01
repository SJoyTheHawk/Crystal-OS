# Tracked Slice 3 Code Guide — Normalized Route Metadata and Destination-First Search

**Plan:** [Slice 3 implementation plan](crystal-http-phase5-slice3-normalized-route-metadata-plan.md)

This guide is the implementation order for the next slice. Keep the existing
Slice 2 lifecycle and stop-request contracts intact while adding route-level
metadata.

## Non-negotiable contracts

`bus_route_variant_t` remains the selected request identity:

```text
route + BUS_OP_* + bound + service_type
```

Add a separate bounded summary contract, for example:

```c
typedef struct {
    char orig_en[48];
    char dest_en[48];
    char orig_tc[48];
    char dest_tc[48];
} bus_route_terminal_pair_t;

typedef struct {
    char route[5];
    uint8_t op;
    uint8_t service_type;
    uint8_t pair_count;
    bus_route_terminal_pair_t pairs[2];
} bus_route_metadata_t;
```

The names may be adjusted to match the repository style, but the semantics
must remain. A summary has no implicit direction. It is valid to display its
destination text in a route row; it is invalid to copy it into a directional
variant or use it to construct a stop URL.

The service owns cached summaries. The app receives copied values through a
read-only getter or event payload and never frees the service cache. Existing
framework responses and successful event arrays retain their Slice 2 ownership
rules.

## S3.0 — Add the contract and getter

Files:

- `components/bus_service/include/bus_service.h`
- `components/bus_service/src/bus_service.c`
- `components/bus_service/include/bus_routes.h` only if a public route-label
  helper is required

Add a bounded metadata store keyed by `route + op + service_type`, with a
getter such as:

```c
uint8_t bus_service_get_cached_route_metadata(
    const char *route, uint8_t op, uint8_t service_type,
    bus_route_metadata_t *out);
```

The getter returns zero or false when no summary exists. It must be safe from
the LVGL task while the catalog is stable, and it must not expose mutable
internal pointers.

Do not put terminal strings into `bus_route_name_t`; that structure is the
compact keypad index and changing it would unnecessarily increase every route
lookup and invalidate the compiled fixture format.

## S3.1 — Centralize normalization

Files:

- `components/bus_service/src/bus_normalize.c`
- `components/bus_service/src/bus_normalize.h`
- provider adapters and their host fixtures

If the repository prefers to keep this helper inside an existing provider
module, preserve the same boundaries. The helper should provide the following
operations:

1. canonicalize a route into a four-character, uppercase, NUL-terminated key;
2. map an explicit direction string to `BUS_DIR_INBOUND` or
   `BUS_DIR_OUTBOUND`, returning unknown for absent values;
3. validate or default provider service type according to provider rules;
4. copy localized text into fixed fields with termination;
5. compare and append terminal pairs deterministically.

KMB and CTB adapters call these functions after reading their own JSON field
names. They must not share raw JSON assumptions. Unknown direction remains
unknown; never infer it from array position, origin/destination reversal, or
route number.

The helper must have a host build path using `calloc`/`free` or caller-owned
buffers and an ESP path using the existing PSRAM allocation policy.

## S3.2 — Parse CTB summaries and variants

Files:

- `components/bus_service/src/bus_provider_ctb.c`
- `components/bus_service/src/bus_provider_ctb.h`
- `components/bus_service/fixtures/`
- `tools/ctb_route_variant_check.c`

Refactor the CTB route-list parser so one pass produces:

- normalized route summaries containing each provider terminal pair;
- normalized directional variants when `bound`/`dir` is explicit;
- empty inbound/outbound identities for directionless records only if the
  current catalog contract still requires those identities.

For the current public response, two records for the same route commonly carry
different origin/destination pairs without a direction. Store both pairs in
the summary. A later duplicate pair is ignored; a new pair is appended until
the bounded pair limit. If the limit is exceeded, keep the first deterministic
set and emit a diagnostic counter.

Use the existing fixtures as regression cases and add:

- a directionless route with two different terminal pairs;
- an explicit-direction route whose directional destination must not be
  replaced by a summary pair;
- duplicate and over-limit terminal pairs;
- mixed case and whitespace route labels;
- missing and non-string localized fields.

The checker must assert both outputs independently: summary pairs are present,
and directional variants remain empty when direction is absent.

## S3.3 — Catalog and cache integration

Files:

- `components/bus_service/src/bus_service.c`
- `components/bus_service/include/bus_service.h`

Populate summaries for KMB and CTB through the same append/merge rules. KMB
direction-specific records may populate a summary pair tagged internally with
their explicit direction; CTB directionless records populate untagged pairs.
Do not let a summary merge across operators or service types.

Extend the route cache record with metadata count and records. Bump the cache
schema version from the current value. Cache load must reject an old version,
truncated strings, invalid operators, zero service types, excessive pair
counts, and duplicate summary identities. Cache save must validate the full
temporary file before the existing atomic replacement.

Log one summary line per provider refresh, for example:

```text
Route metadata: resolved CTB summaries=<n> terminal_pairs=<n>
Route catalog cache: loaded <routes> routes, <variants> variants, <summaries> summaries
```

Yield in validation loops as the current 2,418-variant cache already requires
it. Do not reintroduce the offline timer rebuild that was fixed in the app.

## S3.4 — Render route-level destinations

Files:

- `components/bus_app/src/bus_app.cpp`
- `components/bus_app/include/bus_app.hpp` only if a display helper is added

Add an app helper that formats a bounded route summary without allocating on
the LVGL task stack. The display rules are:

- route-prefix rows: `CTB • To <destination A> / To <destination B>` when
  summary pairs exist;
- directional rows: `CTB • Inbound` or `CTB • Outbound`, followed by `To ...`
  only when that variant carries an explicit destination;
- when a directional CTB variant has no explicit destination but a summary
  exists, show `Route destinations: <A> / <B>` so the scope is clear;
- KMB directional rows keep their current `To` and `From` text;
- empty summaries retain the existing short provider/direction text.

The row user data must continue to encode the complete provider, bound, and
service type. Selecting a row must still call
`bus_service_get_cached_route_variants()` and copy the exact matching variant
before `bus_service_request_stops()`.

Add logs only at the route-selection boundary, for example:

```text
Route row summary: route=10 op=1 pairs=2 destinations=...
Route variant selected: route=10 op=1 bound=I service_type=1 destination=
```

The empty directional destination in the second line is expected when the
provider did not publish a direction association.

## S3.5 — Verification gates

Run from the repository root:

```bash
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
PYTHONPATH=/private/tmp idf.py build
git diff --check
/private/tmp/ctb_parser_check components/bus_service/fixtures
/private/tmp/ctb_route_variant_check components/bus_service/fixtures
rg -n "esp_http_client|esp_tls|esp_crt_bundle|http_get_json" \
  components/bus_service/src/bus_service.c
```

The direct-client search may still find legacy operations outside this slice.
The route catalog and metadata path must continue to use the existing
`crystal_http` handoff.

Device checks:

1. Load a fresh catalog and search a CTB route with two terminal pairs.
2. Confirm the route rows show both CTB destinations.
3. Select inbound and outbound and confirm the stop URLs still use the exact
   selected bound.
4. Select a shared KMB/CTB label and confirm provider choices remain separate.
5. Reboot and confirm summaries survive cache reload.
6. Disconnect Wi-Fi while on Search and confirm the UI remains usable and CPU
   does not return to the previous repeated-rebuild level.

Record response releases, cache counts, heap/PSRAM, and any stale-result logs
with the firmware commit in the Slice 3 evidence record.
