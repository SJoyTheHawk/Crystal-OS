# Tracked Slice 4R Code Guide — Prepared Stop Catalogs

**Status (2026-10-03):** Revision 4R replaces original S4.0–S4.4 acquisition.
This guide is the execution record for 4R.0–4R.6. It separates the current
4R.0–4R.3 implementation checkpoint from later acceptance gates so a device
test is run once for a defined purpose instead of being repeated under
changing conditions.

**Plan:** [Slice 4 plan](crystal-http-phase5-slice4-stop-picker-plan.md)

## How to use this guide

The work has three owners. Host conversion and artifact checks run on the
development machine. Firmware changes and the build run in the repository.
The device owner flashes one named firmware build and captures one complete
monitor log for each row in the test matrix below. Do not change the manifest,
fault mode, Wi-Fi state, or storage state in the middle of a row.

Run rows in order and stop when the row's evidence is present:

| Row | Purpose | Required result |
| --- | --- | --- |
| H | Host artifacts | deterministic KMB/CTB artifacts and manifest; measured sizes and hashes |
| D0 | 4R.0 browsing | route-stop requests work and detail-request count is zero |
| D1 | 4R.2 cold sync | manifest 200; changed providers validate; candidate heap/storage lines present |
| D2 | 4R.2 unchanged sync | manifest only after reconnect; both providers log unchanged; no artifact request follows |
| D3 | 4R.3 publication and warm boot | both generations publish, reboot selects them, and the next sync is unchanged |
| D4 | 4R.3 recovery gate | interruption, corruption, and no-space preserve an already valid generation |

D0–D3 are the current review checkpoint. D4 is still open because the earlier
fault runs were made before a valid generation existed. Do not repeat D0–D3 to
try to prove D4. Run D4 only after the storage budget has a confirmed
second-generation result and the device contains a valid active generation.

Before any device row, run the common build once:

```sh
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
idf.py build
git diff --check
```

Use the same build output for all normal rows. Rebuild only when source,
manifest URL, artifact bytes, or `CRYSTAL_BUS_CATALOG_TEST_FAULT` changes.
The complete log must include boot, Wi-Fi, HTTPS smoke, catalog, and route
lines; a clipped log cannot close a row.

## Invariants

- Identity remains `(operator, source_stop_id)`; sequence belongs to route-stop data.
- Route selection fetches an ordered mapping and performs local name lookups.
- Neither UI callbacks nor service discovery may enqueue stop-detail HTTP.
- All on-device HTTPS uses `crystal_http`; LVGL owns UI rendering.
- Keep active records available during refresh. Callers receive copied values.
- One provider's missing/failed catalog cannot invalidate the other provider.

### 4R.0 device run (D0)

Use one fixed browsing cycle so route changes do not become a source of test
variation: CTB route 10 inbound, CTB route 10 outbound, KMB route 101 service
type 1, and KMB route 101 service type 2. Wait for each route-stop request to
finish before opening the next selection. Live route data may change, so the
pass condition is HTTP 200 plus a non-empty ordered stop list, not an exact row
count. The complete log must contain no stop-detail request, discovery fetch,
or per-key retry request. This row is already covered by the supplied device
evidence; do not repeat it unless the route-stop or discovery code changes.

## 4R.0 — Selective retirement

Take a recovery snapshot including untracked modules and fixtures first.
In `bus_service.c`, remove discovery/sync calls from both successful
`process_stops_request()` branches. Replace the old catalog reconnect and
per-key retry path. Disable the detail-fetch fallback, but keep
`bus_stop_discovery.c`, its CMake registration, and the other deprecated
modules until 4R.6 acceptance explicitly authorizes their removal.

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

### 4R.1 host run (run once per artifact revision)

Run the small fixture tests after converter edits. Run the full-source check
only after regenerating the canonical full artifacts; a deliberate one-byte
test artifact must be recorded as a failed production check rather than
retested on the device.

```sh
python3 tools/bus_catalog/test_stop_catalogs.py
python3 tools/bus_catalog/test_stop_catalogs.py --full
```

Record the manifest URL, source revision/hash, artifact hashes, record counts,
UTF-8 maxima, body sizes, SPIFFS total/free space, and the `crystal_http` body
cap in the evidence file. The host row is complete only when the artifacts are
published over HTTPS or the remaining hosting dependency is explicitly named.
If the repository intentionally carries a test-only artifact revision, keep its
hash in the evidence and do not call that file a production feed.

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

### 4R.2 device run (D1 then D2)

Configure the manifest once, then use the same binary for D1 and D2:

```sh
export CATALOG_MANIFEST_URL='https://host.example/bus_catalog/manifest.json'
idf.py -DCRYSTAL_BUS_CATALOG_MANIFEST_URL="$CATALOG_MANIFEST_URL" reconfigure
idf.py build
idf.py -p /dev/cu.usbmodem1201 flash monitor -b 2000000
```

Replace the example URL and device port once. Do not edit the manifest between
D1 and D2. D3 uses the same source/configuration but requires a separate cold
catalog state; the existing D3 evidence is sufficient unless persistence code
changes. On D1, start with no committed
provider generations and stay on the Bus route list until both providers have
finished. Accept only these markers:

```text
manifest status=200 error=ESP_OK
KMB ... status=200 ... validated=1
CTB ... status=200 ... validated=1
```

Then perform D2 without rebuilding: disable and restore Wi-Fi once, wait for
the reconnect sync, and require `KMB catalog unchanged` and `CTB catalog
unchanged` with no following provider artifact request. Route browsing after
these markers is a separate D0 check and does not change D2's result.

If a route is selected during a bulk transfer, record it as the foreground
cancellation case; do not combine that run with D1. A transport failure,
watchdog warning, or low-space rejection is recorded with its own marker and
does not become a catalog parse failure.

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

### 4R.3 device run (D3, then the deferred D4 gate)

For D3, use the post-fix firmware with fault mode `none`, start without valid
generations, let KMB and CTB publish, reboot once, and capture the first boot
lines. Require an active generation for each provider before the reboot and the
same generation after the reboot. A warm boot that logs both providers
unchanged closes D3.

For D4, first confirm that both providers already have valid committed
generations. Build and flash one fault mode at a time without erasing SPIFFS:

```sh
idf.py -DCRYSTAL_BUS_CATALOG_TEST_FAULT=interrupt reconfigure
idf.py build
# flash, interrupt power at the documented publication point, then reboot

idf.py -DCRYSTAL_BUS_CATALOG_TEST_FAULT=corrupt reconfigure
idf.py build
# flash, reboot once, and verify the other valid slot is selected

idf.py -DCRYSTAL_BUS_CATALOG_TEST_FAULT=no_space reconfigure
idf.py build
# flash, run one update, and verify the active generation is retained

idf.py -DCRYSTAL_BUS_CATALOG_TEST_FAULT=none reconfigure
idf.py build
```

Do not call D4 passed when the boot begins with `has no valid committed
generation`; that only proves the hook ran. The required recovery result is an
older generation selected after the fault, with the provider record count and
SHA unchanged. Restore `none` before normal use.

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

## Current checkpoint and stop rule

The current evidence closes the 4R.0 browsing path, 4R.1 host measurements,
4R.2 cold/unchanged synchronization, and 4R.3 publication/warm-boot
checkpoint. It does not close 4R.3 recovery, 4R.4 lookup/status, or 4R.6
cleanup. The known dependencies are the production KMB artifact revision,
hosting owner/cadence, and enough SPIFFS replacement space for a second
generation. Do not rerun a successful row because a different row is still
open. Attach the complete log and update the evidence table instead.
