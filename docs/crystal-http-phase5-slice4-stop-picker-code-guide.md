# Tracked Slice 4 Code Guide — Prepared Stop Catalogs

**Status (2026-10-02):** Revision 4R replaces original S4.0–S4.4 acquisition.
This is an implementation guide, not a statement that the new code exists.

**Plan:** [Slice 4 plan](crystal-http-phase5-slice4-stop-picker-plan.md)

## Invariants

- Identity remains `(operator, source_stop_id)`; sequence belongs to route-stop data.
- Route selection fetches an ordered mapping and performs local name lookups.
- Neither UI callbacks nor service discovery may enqueue stop-detail HTTP.
- All on-device HTTPS uses `crystal_http`; LVGL owns UI rendering.
- Keep active records available during refresh. Callers receive copied values.
- One provider's missing/failed catalog cannot invalidate the other provider.

## 4R.0 — Selective retirement

Take a recovery snapshot including untracked modules and fixtures first.
In `bus_service.c`, remove discovery/sync calls from both successful
`process_stops_request()` branches. Replace the old catalog reconnect and
per-key retry path. Retire `bus_stop_discovery.c` and its CMake registration
when unused; do not leave a hidden detail-fetch fallback.

Retain `bus_stop_metadata_t`, provider keys, and lookup entry points as useful
contracts, adjusting them deliberately where needed. Replace the transport
loop in `bus_stop_catalog.c`; do not carry `fetch_one()`, four-record batches,
or `BUS_STOP_DISCOVERY_CAPACITY` into the full-directory design. Preserve
Slice 3 route-stop request/cancellation identity and route cache format.

## 4R.1 — Host converter and format

Proposed tool: `tools/bus_catalog/build_stop_catalogs.py`. It accepts saved
source files for deterministic tests and supports explicitly downloading inputs.
KMB input is `/v1/transport/kmb/stop` bulk JSON. CTB input is a pinned HK Bus
Crawling snapshot: collect exact IDs under each route's CTB stops, deduplicate,
and look them up in `stopList`. Validate the observed upstream schema before
coding extraction. Never infer membership from six-digit IDs or shared names.
Reject missing required source records; publish no silently partial replacement.
Record raw-source hashes, generation time, upstream timestamp when available,
source URLs, coverage counts, and attribution. Do not substitute generation
time for upstream freshness evidence. Do not copy upstream crawler code just
to implement an independent format converter.

Use separate provider files with explicit little-endian serialization, not raw
C-struct dumps. Recommended layout: versioned header, sorted fixed-size ID/index
entries, and a UTF-8 string pool. Index entries contain string offsets/lengths,
coordinate validity and values. Validate offsets and multiplication overflow.
Measure maximum field lengths; preserve full strings in storage. Bounded UI
copies must truncate only at UTF-8 boundaries. Sort and search with the same
byte-order comparator; preserve leading zeroes and original IDs.

The manifest describes each provider's schema, immutable artifact location,
byte length, record count, source revision/time, and SHA-256. Freeze exact field
widths and limits after measuring real inputs. Write fixtures for malformed
manifest, wrong provider, duplicate IDs, missing names, invalid coordinates,
long UTF-8 text, broken offsets and checksum mismatch.

Document the measured peak budget: existing app/route memory + TLS headroom +
HTTP response capacity + handoff copy + active index + replacement working set.
`crystal_http` currently buffers the entire response; it has no file-streaming
API. Compact artifacts must fit that model with an explicit body cap. If they
do not, add a separately tested bounded streaming/file-sink transport step
before 4R.2. Never assume streaming already exists or fall back to internal RAM
for a large body. Budget active KMB + active CTB + one candidate + route cache
and filesystem overhead within the existing 4-MiB partition.

## 4R.2 — Snapshot synchronization

Use a catalog state machine with dedicated HTTP ownership and one pending
refresh per provider. Bootstrap/age/manual refresh are the triggers. Provider
manifest and artifact requests have bounded timeouts, retries, and backoff.
A callback transfers completion to the worker, which validates/stores data;
release each framework response exactly once and avoid unnecessary body copies.

Do not wait for a multi-request catalog batch inside the foreground bus worker.
Return control while transfer is pending. On a route selection, cancel/defer a
background transfer if necessary, then admit the selected route's request.
Keep callbacks safe after cancellation using request/generation tokens.
Network restoration retries a provider update, not thousands of stop keys.
Coalesce progress events and yield during long parsing/index work.

Start with daily source-age checks when time is valid; unchanged revisions skip
artifact downloads. An already stale source does not become fresh by redownload.
Offline/unknown-clock states keep valid cached values usable. Missing source
metadata must not trigger busy retries. Hosting and scheduled artifact generation
must be configured for online acceptance; a local file test is not equivalent.

Log provider, source revision, phase, HTTP status, actual transport error,
body bytes, attempts, validation result, and active generation. A failed HTTP
request must not be reported merely as a JSON parse failure.

## 4R.3 — Durable generations

Use version-2 provider catalogs in recoverable A/B slots. Serialize and validate
explicitly; generation selection must use a documented commit/validation rule.
Keep the current slot while writing the inactive one. On boot select a complete,
checksum-valid committed generation; interrupted files must not win selection.
Test actual SPIFFS behavior. Never implement replacement by deleting the sole
valid active file after a failed rename. Reclaim obsolete slots only while a
valid active generation remains, and update providers sequentially to bound space.

Treat the legacy `/spiffs/bus_stop_catalog.bin` v1 partial format as obsolete.
Ignore it safely, log once, and fetch a new catalog. Any eventual targeted cleanup
must not erase the route cache or other filesystem contents.

## 4R.4 — Lookup and status

Keep caller-owned output APIs such as `bus_service_lookup_stop_metadata()` and
`bus_service_lookup_route_stop()`. Use a sorted index and bounded local reads;
no linear scan over the entire catalog per row. Synchronize generation swaps,
reads, and status copies so no reader references freed storage. Do not perform
large filesystem scans under the LVGL lock; compose row copies in the worker.

Return found/missing/invalid explicitly; report stale as separate status or keep
the existing documented stale return with copied output. Clear output on misses
so diagnostics cannot print an earlier CTB record as a KMB result.

Expose per-provider status: usable, freshness enum, updating, generation,
record count, source time, last successful update, last transport/validation
error. Unknown clock is not fresh. `usable` means a complete validated provider
artifact is active; it does not guarantee that a newer official route contains
no unknown IDs. Global readiness must not be `record_count > 0`.

## 4R.5 — Directional destination follow-up

Keep source evidence and official-app comparison as a separate optional gate.
No automatic origin/destination swap or final-stop substitution without a
validated rule. Do not change route identity or enable provisional UI text.

## 4R.6 — Verification and handoff

Host tests: deterministic conversion; CTB membership; provider isolation;
round-trip encoding; full-size limits; corrupt/truncated files; generation
recovery; stale/unknown clock; copied lookup during replacement.
Device tests: both providers, cold/warm start, full catalog sizes, navigation
during update, Wi-Fi loss/recovery, HTTP failures, disk-full, rapid route switch,
watchdog/heap, and no detail requests caused by browsing. Existing KMB transport
failure must be reproduced or cleared with device evidence, not assumed fixed.

```text
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
idf.py build
git diff --check
```

Handoff must identify completed **4R.x** steps and remaining source/hosting/device
gates. Do not resume old S4.5 merely because original S4.4 diagnostics ran.
