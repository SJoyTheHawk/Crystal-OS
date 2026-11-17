# Bus App Phase 2 Results

## Consistent bottom notifications (0.2.17)

Removed the upper route-result and stop-list hint controls, including the
duplicate Choose a route and direction, Loading bus stops... and Tap a stop
for arrivals messages. The existing bottom status field retains loading,
guidance, results and error feedback. Every page now uses the same 20 px status
font, one step larger than the prior 16 px font.

Host package checks and production Lua/LVGL regressions pass at 480 and 720 px.
Assertions verify a single bottom stop instruction, removal of the upper hints,
and the actual 20 px font on Search, Favorites, stops and arrivals. Rendered
720 px Search and stops previews were visually reviewed.

Signed package: /tmp/phase17-bus-live.capp, 2,029,521 bytes, Ed25519 verified
with key ID 240. SHA-256:
e4d1431830c84ae9feacb4b4695875e9c96275a203d1d241d8a3e02c6581c51f

Package-only update for the current runtime; no firmware flash performed.
Board visual confirmation remains pending.

## Complete local processing without timer pacing (0.2.16)

Removed per-file startup waits, per-row route publication, per-record HTTP
framing/decoding delays, per-stop native-list insertion waits, and per-page/record
bundled metadata waits for primary and joint providers. Controller.run_ready
finishes ready local jobs together, stopping for asynchronous HTTP or an empty
queue. HTTP completion resumes this processing immediately. Stop metadata is
still read on demand; this change does not preload the entire stop catalog.

Individual JSON records are decoded separately to avoid a duplicate full
response tree, but all records are processed in the same callback. The 200 ms
typing debounce, 4 MiB arena, 5/30-second runtime deadline, native list capacity,
one-active-request rule, HTTP timeout and generation/cancellation checks remain.
Worker retirement retries yield to actual completion instead of busy-waiting.
Unresolved rows retain Loading... and names replace labels in place.

Host production Lua/UI checks pass at 480/720 pixels: 80 stops decoded and
rendered on response without timer ticks; 80 missing names across 40 real
catalog pages resolved before callback return; repeated IDs, long scrolling,
invalid/truncated JSON, sequential joint ETA, refresh, cancellation and late
callbacks. Catalog preload and all eight route result rows complete together.
The extended workload peaks at 880,464 bytes of the 4 MiB host Lua arena.
Package checks and production 5/30/5-second timeout/OOM regressions pass.
These host results do not certify P4 execution time or UI responsiveness.

Install the signed Bus Lite 0.2.16 CAPP through the Control Panel. No additional
firmware reflash is needed on the current runtime. Board retest: catalog load,
1/1A/68X/101 search, long stop lists, metadata failure/recovery, joint ETA and
navigation/pause/resume. Capture BUS_SEARCH and BUS_RESOURCE checkpoints.

Signed package: /tmp/phase17-bus-live.capp, 2,029,935 bytes,
Ed25519 verified with key ID 240. SHA-256:
b51609474318a58bb7f836cb9a9143ada447e201b31c85ac8b4a4da7cef015ed

## Unresolved stop labels (0.2.15)

Stop rows without a usable name now display exactly **Loading...**, without
sequence numbers, provider IDs or retry suffixes. Resolved names replace these
labels in place and retain their sequence prefix. Opening an unresolved stop
also uses Loading... for the selected-stop heading.

Package and production Lua/LVGL checks pass at 480 and 720 pixels, including
missing metadata, placeholder selection, last-stop scrolling and metadata
replacement. This package-only change needs no firmware reflash on the latest
runtime. Board visual confirmation remains pending.

Signed package: /tmp/phase17-bus-live.capp, 2,029,887 bytes,
Ed25519 verified with key ID 240. SHA-256:
595fbf9e9d997a99f22dc27763893262f6c630a01b814747ae1e37752fb0ee46

## Resident full-catalog search and developer timeout (0.2.14)

Removed both the 64-entry scan slice and the early return after each visible
match. All 2,115 resident route records are checked in a single search callback,
with complete counts and eight retained visible results; no route-file rereads.
Debounce, stale-query invalidation and staged row publication remain.

Firmware now exposes **Extended Lua Timeout** in Developer Tools: off/default
is 5 seconds, on is 30 seconds. It is persisted, cached in RAM, and applied at
the next Lua invocation/entry evaluation, with duration-specific error text.
Failed saves restore the switch and show a toast. ESP-IDF task-watchdog settings
are independent. This change requires firmware app-flash plus the 0.2.14 CAPP.

Host acceptance: package checks; real LVGL developer toggle/default/persistence/failed-save tests; production Lua/UI at 480 and 720 pixels; full-catalog single-scan callback and complete paging; simulated production deadline boundaries at 5/30/5 seconds; allocator/OOM/teardown regressions. P4 firmware build passes.

Signed package: /tmp/phase17-bus-live.capp, 2,029,650 bytes, ed25519 (verified), key ID 240.
SHA-256: 26e9824cc9c24c5353d03a3c793139f7c6f2a68f2bbe0ac023bc52e13ed8dd37
Firmware SHA-256: ab257cbb5530bddb1154616b7d41e31e78b488bd0d97d50bbd96d09862ccbe8f

Board timing and interaction acceptance remain pending.


## Route keypad visibility (0.2.13, 2026-11-17)

The P4 search keypad now follows the established S3 behavior. Numeric keys
remain in place and are disabled when the current prefix cannot continue with
that digit. Alphabetic keys and their empty rows are hidden when impossible;
they appear again when a new prefix makes them valid. Clear and backspace stay
visible, with their enabled state tracking whether they can act.

The production-binding/LVGL tests cover initial catalog preload, numeric
disabled states, letters hiding and reappearing across prefix changes, and both
480 px and 720 px layouts. Install Bus Lite 0.2.13 through the Control Panel;
this package-only change does not need a firmware flash.

## Typing-triggered timeout correction (0.2.12, 2026-11-17)

The user confirmed the post-preload timeout happens while typing/changing the
route number. Recent-route persistence is not the target of this correction.
The exact expensive operation remains unmeasured on P4. The typing/search path
had redundant writes to all 38 keypad buttons, substring allocation for every
scanned catalog record, and combined scanning/eight-row rendering/diagnostics.

0.2.12 caches keypad enabled states, compares route prefix bytes without
temporary strings, and separates search setup, scanning, one-row rendering,
navigation controls, completion status and the resource checkpoint across
timer callbacks. Query edits invalidate clickable result mappings immediately;
generation checks discard stale work. The 4 MiB quota, 50 ms watchdog and
preloaded catalog remain unchanged.

Host package/render tests pass at 480/720 pixels. Both simulated touch and
timer callbacks now use the production watchdog hook. Regressions check no
redundant keypad writes, at most one result destination per timer tick, and
query replacement during result publication. Real-catalog peak payload is
745,384 bytes; host timing is not P4 timing.

Retest the signed 0.2.12 package through the Control Panel (package-only; no
firmware flash). After catalog_loaded, repeatedly type 1, 1A, clear, 68X,
backspace, and 101; also edit while results are loading and page results.
BUS_SEARCH key/key_ready/scan/render/ready markers distinguish input, scan and
publication if another timeout occurs. These are interaction logs, not idle
logs. Board interaction acceptance and the slow onCreate warning remain open.

Artifact: `/tmp/phase17-bus-live.capp`, 2,029,974 bytes, Ed25519 verified with
key ID 240. SHA-256:
`46be0ebe5987666155cfd274241d9331356fd39305b0906b48d89e0dd1f3da05`.

## P4 preload passes; post-search timeout remains (2026-11-17)

Attachment `2e7da0ee-da5a-4447-b927-e4331a44f1d9/Pasted text.txt` contains
older failures followed by two launches with the 0.2.11 catalog markers:

| Checkpoint | First new launch (ms) | Second new launch (ms) |
|---|---:|---:|
| launch_ready | 3,354,945 | 3,403,582 |
| keypad_ready 2115 | 3,357,049 | 3,405,706 |
| catalog_loaded | 3,357,259 | 3,405,917 |
| search_ready | 3,373,944 | 3,414,030 |
| callback timeout | 3,395,879 | 3,419,463 |

Both launches load the full catalog in roughly 2.3 seconds after launch_ready.
At catalog_loaded, Lua uses 291/4096 KiB, internal free memory is 239 KiB,
and PSRAM free memory is 20,726 KiB. Both reach search_ready. Later teardown
peaks are 395 and 378 KiB, with tracked=0 clean=1. This verifies the preload
correction on the board but does not close interaction/Phase 2 acceptance.

There are no HTTP request-queued entries between search_ready and the later
timeouts in this excerpt. The exact triggering interaction is not logged.
Route selection currently combines synchronous recent-route persistence with
page setup; search rendering and keypad changes are also synchronous. These
are investigation targets, not established causes. Confirm the action before
changing another batch size or attributing the failure to network fetching.
The onCreate warning also remains (1,363 and 1,357 ms against 200 ms).

## Follow-up startup timeout (2026-11-17, 0.2.11)

The 0.2.10 board retest still fails: launch at 2,792,200 ms, ready at
2,793,650 ms and watchdog stop at 2,797,942 ms, before catalog_loaded.
Peak Lua payload is 273 KiB / 4096 KiB, teardown tracked=0 clean=1. The log
does not identify the exact instruction, so P4 timing acceptance stays open.
The loader still built the keypad prefix map in 32-record batches.

0.2.11 generates the complete prefix map on the host as keypad.txt (2,672
bytes). New packages read and retain nine compact route pages and that index,
one file per callback, with no per-route processing during preload. Signed
descriptors provide byte lengths and record counts, verified by host tests.
Legacy packages use one record per tick. Readiness, UI updates and diagnostics
are separated. The quota remains 4 MiB and the watchdog remains 50 ms.

Finite BUS_CATALOG markers name each file before its read and report
keypad_ready 2115 before publication; there are no recurring idle logs.
The production host tests cover startup pause/resume, complete paging, prefix
filtering, resident search without file rereads, provider workflows and cleanup
at 480/720 pixels. Host timing cannot certify the P4 callback budget.
Install the rebuilt 0.2.11 CAPP through the Control Panel; no firmware change
is required. Retest launch through catalog_loaded, then route search/stops/ETA.
If a timeout remains, retain the last BUS_CATALOG marker in the serial log.

Host catalog/package/render checks pass. Signed artifact: 2,029,050 bytes,
Ed25519 verified with key ID 240; SHA-256
`f48ec4491733ce7fb63cc65d9e8be2aa0b75e39f50779ad34ba40d9c1ee56b13`.

## Startup callback correction (2026-11-17, 0.2.10)

The P4 log stops twice during initial catalog loading with the 50 ms callback
watchdog, at only 213 KiB peak Lua payload out of 4096 KiB. Both teardown checks
are clean (`tracked=0 clean=1`). The separate roughly 1.46-second onCreate
warning remains open for board profiling.

The previous loader expanded 32 complete records per tick through the first-fit
PSRAM arena. The correction retains compact pages as strings and inspects only
route numbers for keypad prefixes. Search scans at most 64 records and expands
at most one visible record per tick, without rereading route files. The full
catalog stays resident; the 4 MiB quota and 50 ms watchdog are unchanged.

Package/catalog checks and production UI tests pass at 480 and 720 pixels. The
real-catalog timer test now uses the production invocation and instruction hook.
Tests enforce compact preload memory and zero route-file rereads during search.
The catalog workflow peaks at 809,800 bytes (about 0.77 MiB), compared with the
previous roughly 1.62 MiB. Host results do not establish P4 timing.

Install the signed `/tmp/phase17-bus-live.capp` (0.2.10) through the Control Panel
after uninstalling the old version (which removes saved stops). No firmware
flash is needed for this correction. Wait for `BUS_RESOURCE catalog_loaded`,
confirm the keypad enables, then search 1A/68X/101, change result pages and open
stops/ETA. Capture startup and teardown logs. Board retest remains required.

The following entries record earlier Phase 2 builds and measurements.

Corrected artifact: 2,023,750 bytes, version 0.2.10, Ed25519 signature verified
with key ID 240. SHA-256:
`c9ee32eb57cd576090a6d13ea79f7d9137026eb8dcd14751e7cb8eb72648dd2a`.

Updated 2026-11-16. Phase 2 adds joint-route ETA handling to Bus Lite 0.2.9.

The production Lua entry now follows the Flutter `TransitEta` and
`StopMatcher` behavior:

- The KMB part remains the primary route and stop identity.
- CTB route stops are fetched after KMB ETA, then resolved from the bundled
  CTB stop catalog or sequential provider detail requests.
- The nearest CTB stop with valid coordinates is selected. If the KMB stop has
  no coordinates, the first CTB stop is used as the Flutter fallback.
- CTB ETA is fetched only after matching, keeping the P4 one-request worker
  constraint.
- KMB and CTB rows are merged, sorted by timestamp and stable source order,
  capped at three, and retain their provider labels.
- A failed source or missing match preserves successful rows and reports
  partial data; both-source failure reports a retryable error.

## Verification

The host production UI harness covers route 101 with these requests:

```text
KMB route-stop → KMB stop metadata → KMB ETA
CTB route-stop → CTB stop metadata fallback → CTB ETA for nearest matched stop
```

It asserts both `KMB` and `CTB` arrival labels, verifies the closer CTB stop is
selected even when it is second in route order and more than 500 m away, and
confirms CTB arrivals remain visible with a partial-data status when KMB returns
an HTTP error, and KMB arrivals remain visible when CTB fails. If both sources
fail, the app shows a retryable combined error. It runs at 480 and 720 pixels.
The package, catalog, binding, and render checks pass. The render harness also
continues to verify JSON allocation cleanup, callback reuse, cancellation,
late-callback invalidation, and teardown.

The signed package is `/tmp/phase17-bus-live.capp`:

| Item | Value |
|---|---|
| Package version | `0.2.9` |
| Size | 2,022,940 bytes |
| SHA-256 | `2522fee4d77305f074e236d6dad2f54679f663bab74227b8c9a3671efac3cb0a` |
| Signature | Ed25519, key ID 240, verified |
| Lua quota | 4096 KiB (4 MiB) |

P4 installation and live route 101 acceptance remain to be performed. Phase 1
board measurements remain the resource baseline; this host result does not
establish P4 arena headroom.

Quota updated at user request on 2026-11-17 from 512 KiB to 4 MiB. Package checks pass and the rebuilt signature is verified. P4 resource acceptance remains pending; no firmware flash is needed for this manifest-only quota change.

## Route preload and keypad filtering (2026-11-17)

At app startup, the package now reads compact search pages and incrementally
loads the full 2,115-record bundled route catalog. Search keys remain disabled until the route table
is ready; each edit enables only the next characters represented by matching
route prefixes. Clear and backspace recompute the key mask. Search filters the
resident records in bounded search batches, while detailed stop
metadata remains on demand.

Production Lua/UI tests at 480 and 720 pixels pass. The 4 MiB host Lua arena
peaks at about 1.62 MiB with the full catalog and representative navigation.
This is host allocator evidence only; board startup duration, callback budget,
internal heap and PSRAM headroom still need P4 measurement. The signed package
was rebuilt and verified under key ID 240. Install it through Developer Tools
Control Panel after uninstalling the previous package.
