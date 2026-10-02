# HTTPS Phase 5 — Slice 4: Prepared Stop Catalogs

**Status (2026-10-02):** Acquisition design revised during original S4.4.
Original S4.0–S4.4 implementation exists but is not accepted under this revision.
The on-device discovery/per-stop download workflow is superseded. No code was
reverted as part of this documentation change.

**Previous:** [Slice 3](crystal-http-phase5-slice3-normalized-route-metadata-plan.md)
**Guide:** [Slice 4 code guide](crystal-http-phase5-slice4-stop-picker-code-guide.md)
**Next:** [Slice 5](crystal-http-phase5-slice5-stop-picker-plan.md)

## Decision and user flow

Keep the established KMB/CTB route catalog. Download prepared stop catalogs
independently of route browsing. Selecting a route still fetches that variant's
ordered stop IDs from the official route-stop API, then resolves names locally.
Opening or revisiting routes must never schedule individual stop-detail HTTP
requests, including indirectly through a service discovery queue.

```text
Off-device preparation:
  KMB official bulk stop JSON → compact KMB catalog
  HK Bus Crawling snapshot → CTB stop extraction → compact CTB catalog

Device initialization / scheduled refresh:
  manifest + provider catalog → validate → persistent active catalog

Route selection:
  official route-stop response → ordered IDs → local names → stop rows

Later ETA slice:
  selected provider/route/direction/stop → live ETA request
```

The recommended initial CTB source is HK Bus Crawling's published database.
This introduces a third-party data dependency. The host tool extracts only IDs
referenced under CTB in that snapshot's route records and preserves their exact
source IDs. It must not infer operators from ID length or names. Snapshot
coverage can lag official route changes; unknown IDs stay visibly unresolved.

Both provider inputs are converted off-device to one compact format. This
avoids parsing a large multi-operator JSON database on the ESP32 and makes
transport, storage, and lookup shared. KMB's official bulk API remains the KMB
source; we are changing where JSON conversion runs. Full route-stop database
preloading, as hkbus does, is outside this revision.

A supported HTTPS publication location for the generated artifacts must be
chosen and recorded in 4R.1. No working URL or automated publishing service
exists merely because this plan names it. Locally provisioned artifacts can
validate storage and lookup first, but do not satisfy online update acceptance.

## Evidence and correction

- [KMB bulk stop API documentation](https://data.gov.hk/en-data/dataset/hk-td-tis_21-etakmb/resource/3d6ded6c-ee36-40a0-a6fe-8f40966dff67)
  explicitly documents `/v1/transport/kmb/stop` returning all stops.
- [HK Bus Crawling KMB adapter](https://github.com/hkbus/hk-bus-crawling/blob/master/crawling/kmb.py)
  uses bulk stop and route-stop endpoints.
- [CTB adapter](https://github.com/hkbus/hk-bus-crawling/blob/master/crawling/ctb.py)
  builds stop metadata through individual requests in its separate crawler.
- [Published database](https://hkbus.github.io/hk-bus-crawling/routeFareList.min.json)
  is described in the [crawler README](https://github.com/hkbus/hk-bus-crawling).
- The app loads and caches the prepared database in
  [db.ts](https://github.com/hkbus/hk-independent-bus-eta/blob/140999f0497397ea1ef0e181fb3a6e558e51ad70/src/db.ts)
  and renders names through local `stopList[stopId]` lookups.

The previous fixture document's claim that neither provider has bulk stop data
was incorrect for KMB. Moving individual requests to a background worker did
not implement the intended preloaded directory experience. A source change
also does not prove that existing KMB TLS failures are fixed; transport retains
its own acceptance gate.

## Recovery decision

**Use a selective refactor; do not reset the repository to Slice 3.**
At review, `HEAD` is `e79d24a` (Slice 3). Slice 4 has uncommitted changes in
three tracked bus-service files plus new modules/fixtures. The app and shared
HTTP implementation have no tracked Slice 4 diffs. Preserve unrelated dirty
`reference/ESP32-S3-Touch-LCD-4B` content.

| Existing work | Treatment |
| --- | --- |
| Provider-qualified keys, copied metadata, field validation | Keep and adapt to measured catalog limits |
| Local lookup API shape | Keep; revise per-provider state and synchronization |
| Individual detail parsers/fixtures | Useful input-validation evidence; not the runtime acquisition path |
| Discovery queue and scheduling after route selection | Retire |
| Per-key retries and four-stop HTTP batches | Replace with provider snapshot updates |
| v1 partial catalog and pending-key persistence | Replace with versioned complete provider generations |
| Global ready/fresh counters and diagnostics | Replace with per-provider usable/fresh/update state |
| Slices 1–3 route identity and lifecycle work | Preserve |

Before code refactoring, preserve tracked diffs AND untracked source/fixtures
in a local recovery snapshot or explicit checkpoint. `git diff` alone misses
new files. A blanket `git restore` would discard reusable work and is not the
recommended next action.

## Revised work packages

Use **4R.x** identifiers to distinguish this sequence from original S4.0–S4.4.
Old device tests remain evidence only for the paths they exercised.

### 4R.0 — Retire per-stop acquisition and preserve contracts

Snapshot the current work. Disconnect discovery-triggered detail requests,
per-key retry scheduling, and reconnect restarts of that queue. Preserve route
catalogs, route-stop fetching, cancellation, and copied record contracts.
Legacy partial records may be ignored; do not erase other SPIFFS data.

**Exit:** build passes; opening both providers' routes and reconnecting produces
no stop-detail requests. Sequence fallback remains functional.

### 4R.1 — Prepare sources, artifacts, and resource budget

Implement a reproducible host converter for KMB bulk JSON and a pinned HK Bus
Crawling snapshot. Validate CTB membership using CTB route stop references;
record missing references and source provenance. Generate separate sorted KMB
and CTB catalogs plus a manifest with schema, source revision/time, byte count,
record count, and SHA-256. Keep EN/TC and optional coordinates.

Measure input/output sizes, longest UTF-8 names, counts, index RAM, HTTP body
peak, and update disk space. The current 512-key/64-KiB limits are not valid
full-catalog budgets. Fit catalogs alongside routes in the existing 4-MiB
SPIFFS partition, including one replacement generation. Do not silently
truncate records or change partitions. Choose and verify the artifact hosting
URL and update owner/cadence; include source attribution with generated files.
A scheduled host build is the intended update mechanism; generating once is
only a development milestone.

**Exit:** deterministic full-provider artifacts and round-trip tests pass;
resource budget and delivery configuration are documented. If no host is
configured, report that dependency explicitly and continue local fixture work.

### 4R.2 — Synchronize provider catalogs through crystal_http

Fetch the small manifest and only changed provider artifacts, one at a time.
Refresh on bootstrap when missing/stale, or explicit refresh; route navigation
never starts synchronization. Coalesce duplicate work. A selected route can
cancel/defer background transfer and take the next HTTP slot; bulk download
must not block the bus worker from handling that selection. Wi-Fi loss retains
the active generation; retry the bounded transfer after recovery. Byte-range
resume is not required.

**Exit:** missing/unchanged/changed paths, cancellation, foreground priority,
provider isolation, and low-heap transport pass. No per-stop fan-out exists.

### 4R.3 — Persist complete provider generations safely

Stage and validate a replacement before publishing it. Verify schema, provider,
counts, bounds, ordering, length, and checksum. Keep the active catalog until
its replacement is durable. Use recoverable A/B provider slots and boot
validation; do not remove the only valid file when rename fails. Update one
provider at a time; failure of KMB must not invalidate CTB. Reject v1 partial
files as complete catalogs without affecting route caches.

**Exit:** power-loss/interrupted-write, corruption, no-space, provider failure,
and warm-boot tests recover a valid generation when one previously existed.

### 4R.4 — Local lookup and truthful provider status

Reuse copied direct and route-stop lookups with a bounded index. Expose usable,
fresh, updating, active generation/count, last success, and last error per
provider. A complete validated snapshot is usable even if stale; freshness
must reflect source age and successful validation, never a failed save/retry.
An unknown stop is a local miss and never launches a detail request. Unknown
clock means unknown freshness. Report CTB snapshot coverage separately from
claims of completeness of the live operator network.

**Exit:** both provider catalogs resolve names locally after reboot, one
provider's failure does not imply the other's failure, and misses are safe.

### 4R.5 — Optional directional destination evidence

Keep the prior CTB terminal proposal deferred until catalogs pass. Recheck
provider direction semantics and the official app before selecting a rule.
Explicit supported directional destination takes precedence; final-stop names
are only a provisional display fallback. No guessed destination becomes route
identity, and no provisional rule is enabled in the UI without validation.
This work may be deferred explicitly without blocking catalog handoff.

### 4R.6 — Acceptance and Slice 5 handoff

Verify cold bootstrap, warm boot, unchanged manifest, provider update/failure,
interruption, corrupt files, full disk, stale/missing names, Wi-Fi recovery,
and rapid route replacement. Record HTTP request counts: metadata requests
must depend on catalog updates, not the number of stops/routes browsed.
Run host checks, `idf.py build`, `git diff --check`, and device tests.

## Acceptance record

```text
revision=4R firmware=<sha> source_snapshot=<revision/hash/time>
provider=<KMB|CTB> records=<n> bytes=<n> generation=<id>
usable=<yes/no> freshness=<fresh/stale/unknown> coverage=<definition>
bootstrap=<pass/fail> update=<pass/fail> recovery=<pass/fail>
route browsing detail requests=0
route-stop requests=<n> catalog requests=<n> retries=<n>
peak internal/largest block/PSRAM=<bytes> filesystem headroom=<bytes>
watchdog=<none/observed> KMB transport gate=<pass/fail>
artifact hosting/refresh=<configured/local-only> terminal evidence=<pass/deferred>
```
