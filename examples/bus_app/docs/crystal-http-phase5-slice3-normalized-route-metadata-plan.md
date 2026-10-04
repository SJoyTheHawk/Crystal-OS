# Tracked Slice 3 — Normalized Route Metadata and Destination-First Search

**Status:** Slice 3 signed off on 2026-10-01; normalized metadata, cache
reload, Search summaries, route selection, cancellation, and device acceptance
verified. One transient TLS internal-memory allocation failure is recorded as
deferred follow-up because the existing retry path recovered successfully.
Repeated failures have since reopened this follow-up; see the
[TLS AES memory fix](crystal-http-tls-aes-memory-fix.md), pending device testing.

**Previous slice:** [Slice 2 — CTB route variants](crystal-http-phase5-slice2-ctb-route-variants-plan.md)

**Code guide:** [Slice 3 code guide](crystal-http-phase5-slice3-normalized-route-metadata-code-guide.md)

## Decision

The next slice moves route normalization and route-level destination display
ahead of stop-name resolution. The public CTB route endpoint contains useful
origin and destination pairs, but it does not associate those pairs with
`inbound` or `outbound`. The application can safely display those pairs on a
route row when they remain route-level metadata. It must not copy them into a
directional variant or use them to construct a stop request.

This gives the user useful CTB destinations sooner while preserving the
identity contract from Slice 2:

```text
directional identity = route + operator + bound + service_type
route summary        = route + operator + service_type + terminal pairs
```

The two records are related by route and operator, but they have different
meanings and are stored separately.

## Why the sequence changes

The existing implementation has normalized directional records for KMB and
CTB, but the normalization is spread across `bus_service.c` and the CTB
parser. CTB route-level terminal text is lost when direction is absent. The
Search UI then has no data to show, even though the provider response had
destinations.

The next slice therefore establishes one provider-neutral route metadata
contract, retains CTB terminal pairs, and presents them in Search. Stop
details, favorites, and ETA remain later work that can consume this contract.

## Scope

This slice includes:

1. A bounded normalized route metadata model separate from directional variants.
2. Shared normalization rules for route labels, operators, directions, service
   types, and localized text.
3. CTB route-list parsing that retains route-level terminal pairs when no
   direction is present.
4. KMB and CTB catalog ingestion into the same metadata store.
5. Cache persistence, validation, and versioning for route metadata.
6. Search route rows that show CTB destinations before a direction-specific
   destination is available.
7. Host fixtures and device checks for duplicate labels and missing direction
   associations.

This slice does not include:

- assigning CTB terminal pairs to inbound or outbound;
- changing CTB route-stop URLs or stop cancellation behavior;
- stop-name or coordinate resolution;
- ETA parsing, favorites, language switching, or NWFB discovery;
- a new destination endpoint or inference from array order, route numbering,
  or terminal text.

## Normalized data contract

Add a service-owned route summary with bounded storage. The exact C layout is
specified in the code guide, but the contract is:

```text
route + operator + service_type
  └── zero to two terminal pairs
        origin_en, destination_en, origin_tc, destination_tc
        direction = unknown unless the provider explicitly supplies it
```

`bus_route_variant_t` remains the only object that may be selected for a stop
request. A summary can enrich a row and a stop-page title, but it cannot
replace `current_route_` or fill an empty directional destination.

Normalization must be deterministic:

- route labels are trimmed, uppercased, bounded to four characters, and
  rejected when empty or oversized;
- operator values are mapped to the named enum;
- `I`, `O`, `inbound`, and `outbound` map only when explicitly present;
- CTB service type defaults to `1` only when the provider omits it;
- non-string terminal fields are rejected for that record, while absent
  optional fields remain empty;
- all copied text is NUL-terminated and safely truncated or rejected according
  to the field contract;
- duplicate route summaries merge terminal pairs deterministically without
  changing directional identity.

## Work packages

### S3.0 — Freeze the normalized route contracts

Document the summary and variant ownership, add compile-time bounds, and
define the service getter used by Search. Keep the existing route-label index
allocation-free for keypad queries. The new summary store is provider-qualified
and cache-backed.

**Output:** header contract, ownership notes, and a fixture table.

### S3.1 — Centralize provider normalization

Move shared route/text/direction/service-type normalization into a small
provider-neutral helper used by KMB and CTB adapters. Keep provider field names
and URL construction inside provider adapters. The helper must not guess a
direction from a terminal pair.

**Output:** host fixture results covering valid, missing, oversized, and
invalid fields.

### S3.2 — Preserve CTB route-level terminal pairs

Parse `/v2/transport/citybus/route/CTB` into normalized summaries and
directional variants in one bounded handoff. A CTB record without an explicit
direction contributes its terminal pair to the summary and contributes empty
directional identities for compatibility. A record with an explicit direction
continues to populate only that directional variant.

**Output:** route 1 or another fixture with two different terminal pairs,
showing both pairs retained and both directional destinations still empty when
direction is not supplied.

### S3.3 — Persist and reload metadata

Add the summary records to the route cache. Bump the cache schema version,
validate temporary data before atomic replacement, reject duplicate identities,
and make old caches fail closed so a provider refresh rebuilds them.

**Output:** in-memory versus reload comparison for route labels, provider,
service type, pair count, and all localized strings.

### S3.4 — Show destination summaries in Search

Use the route summary getter while building the existing Search rows:

- prefix rows show `CTB` and a compact `To A / To B` summary when available;
- directional rows show `To ...` only for an explicitly directional
  destination;
- when only route-level CTB data exists, directional rows show a clearly
  labelled route summary rather than pretending it belongs to the direction;
- KMB rows keep their existing direction-specific display;
- shared KMB/CTB route labels remain separate choices.

The selected stop request continues to copy only the normalized variant.

**Output:** device capture showing CTB destinations in Search and logs proving
that the selected `bound` and stop URL are unchanged.

### S3.5 — Acceptance and handoff

Verify cold cache, cache reload, shared labels, malformed data, empty data,
directionless CTB records, route selection, Back, and Wi-Fi loss. Confirm the
route summary never changes the request identity and that the offline Search
path remains low CPU after the previous fix.

## Exit criteria

The slice is complete when:

- KMB and CTB records use the same normalized summary and variant contracts;
- CTB route rows show provider-supplied destinations even when direction is
  absent;
- no CTB destination is presented as direction-specific without an explicit
  provider direction;
- cache reload preserves all route summary pairs and directional identities;
- duplicate KMB/CTB labels remain independent;
- route selection still produces the same CTB route-stop URL and identity;
- malformed, empty, stale, cancelled, and offline cases terminate safely;
- `idf.py build`, host fixtures, `git diff --check`, and source ownership
  checks pass.

## Evidence template

```text
slice=3
firmware commit=<sha>
route=<route> operator=<KMB|CTB> service_type=<n>
summary_pairs=<n> destinations=<...>
directional_variants=<n>
cache version=<n> reload=<pass|fail>
selected route=<route> operator=<n> bound=<I|O> service_type=<n>
stop URL identity=<...>
cpu/offline search=<observation>
result=<pass|fail|deferred>
known follow-up=<...>
```

## Acceptance and sign-off (2026-10-01)

```text
slice=3
firmware commit=working tree build; no commit hash recorded
route=10 operator=CTB service_type=1
summary_pairs=1 destinations=North Point Ferry Pier
directional_variants=2 (inbound/outbound identities retained)
cache version=8 reload=pass
selected route=10 operator=1 bound=I/O service_type=1
stop URL identity=route-stop/CTB/10/inbound and route-stop/CTB/10/outbound
cpu/offline search=previous offline-search reduction verified; no watchdog in acceptance run
result=pass
known follow-up=one transient esp-aes TLS allocation failure; retry recovered; investigate only if repeatable
```

The route summary survived reboot from the cache, and the device logs retained
the exact CTB bound and stop URL identity. The transient TLS allocation failure
returned `ESP_FAIL` after retries on one run, without a watchdog or stale UI
state; a subsequent retry completed normally. It is deferred to transport
reliability work and does not block Slice 3 sign-off.
