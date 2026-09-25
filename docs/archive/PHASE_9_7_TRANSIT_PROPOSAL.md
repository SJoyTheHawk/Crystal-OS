# Phase 9.7 — Transit (HK Bus)

Refines [`HK_BUS_APP_EVALUATION.md`](HK_BUS_APP_EVALUATION.md) into a scoped,
buildable phase. Where the two disagree, this document wins.

Companion documents, both written for the implementing session:

- [`PHASE_9_7_TRANSIT_DESIGN.md`](PHASE_9_7_TRANSIT_DESIGN.md) — user-facing
  behaviour, screens, states. Folds into `DESIGN.md` §9.5 on close.
- [`PHASE_9_7_TRANSIT_CODE_GUIDE.md`](PHASE_9_7_TRANSIT_CODE_GUIDE.md) — shapes,
  threading, call sites. Folds into `CODE_GUIDE.md` on close.

## 1. Why 9.7 and not 13

The app is a bundled C++ `CrystalApp`, exactly like Weather and Calculator. It
belongs beside them in the numbering, not in Phase 13. Phase 13 is the *catalog
UI* — the screen that enables, disables, reorders, and clears bundled apps. A new
bundled app is an entry in `kApps`, which Phase 5 already supports. Filing this as
9.7 keeps Phase 13's exit criterion about the catalog rather than about a bus app.

Numbering it 9.7 also states the honest limit up front: until Phase 13 ships there
is no user-facing way to turn this app off, and until Phase 14-17 ship there is no
way to distribute it separately from firmware.

## 2. Skipping Phase 12 — what it costs, and the one piece to keep

Nothing in this phase depends on Phase 12. The app can be built, flashed, and
validated with Phase 12 entirely absent. Two consequences, both accepted
knowingly:

- **Every update is a USB flash.** No OTA means no remote fix for a bad transit
  build, on any unit already handed out.
- **A wedge has no automatic escape.** Without
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, an image that boots and then hangs in
  the network path stays the running image until someone reflashes it over USB.

The recommendation is not "do Phase 12 first". It is narrower: **ship Phase 12
piece 1 (crash reporting) before or alongside this phase.** The three Phase 12
pieces are independent by `IMPLEMENTATION_PLAN.md` §Phase 12, and piece 1 is small
— a coredump summary read at boot, one NVS blob, one one-shot flag, one Device
Status row. A new app that opens TLS sockets, parses third-party JSON, and holds a
few hundred KB of PSRAM is the most likely new source of crashes this firmware has
gained since Phase 9. Landing this app with no crash reporting means the first
field failure arrives as "it restarted" and nothing else.

If that trade is declined, note it in the phase record and proceed. It does not
block anything below.

## 3. Scope decisions

Five decisions that differ from the web app. Each is a deliberate narrowing, and
each has a visible consequence the design document has to show the user.

**3.1 A route *name* index, not the route table.** Two different objects, and
conflating them is what made this look expensive:

| Object | Measured | On device |
| --- | ---: | --- |
| Route table (`/v1/transport/kmb/route/`, 1,598 rows) | 348,938 B | never held |
| Route table, gzipped | 38,322 B | transfer only |
| Distinct route names, KMB 792 + CTB 406, union 1,048 | ~6 KB packed | held, always |

The table carries operator, bound, service type, origin, and destination in
English and two Chinese variants — none of which a keypad needs. The names are 1-4
characters over the alphabet `0123456789ABCDEFGHKMNOPRSTWX`. Packed as sorted
4-byte names plus an operator mask, the whole territory's route list is ~6 KB of
flash, binary-searchable.

So the keypad filters live, the way the official KMB app does: after `2`, only the
characters that can actually follow `2` stay lit and the rest grey out. A prefix
range is one `bsearch`; the allowed next characters are the distinct characters at
that offset across the range. Microseconds, no allocation, no network. Per-route
detail still comes from the per-route endpoints, so the table itself is never
parsed for search.

The index is compiled in as a baseline and refreshed opportunistically (§3.6). It
greys keys but never blocks them: a key the index does not know is still tappable,
and the live API remains the authority. That way a stale index costs a lit key, not
a reachable route.

**3.6 Refresh the index on a schedule, by validator, not by polling.** Measured
against the live endpoints:

- **KMB** serves `ETag: W/"5530a-…"` (content-derived) and honours
  `If-None-Match` — verified `304` with a 0-byte body. It also honours
  `Accept-Encoding: gzip` at 38,322 B, and the S3 ROM exports `tinfl_decompress`,
  so inflate costs no flash and one 32 KB PSRAM window.
- **CTB** serves neither an `ETag` nor gzip. Only `Cache-Control: max-age=300` and
  111,887 bytes. Detecting a change means streaming the body and comparing a
  SHA-256 of it — the transfer cannot be avoided, only made rare.
- `generated_timestamp` in the KMB body is **not** a change detector. It tracks the
  5-minute cache window, not the content; two fetches two seconds apart returned
  the same value and it moves on every window regardless of whether anything
  changed.

The policy that follows: check at most once every 24 h, only while online, only
while the app is not in the foreground, and never on the boot path. Persist the KMB
`ETag` and the CTB body hash. Steady state is one conditional GET returning 304 for
KMB and one 112 KB stream discarded for CTB — so schedule CTB weekly rather than
daily, since it pays full price every time. Route lists change a few times a year;
neither feed deserves a daily 112 KB.

**3.2 Stop names resolve lazily, in bounded batches.** A route has 60-120 stops
and each name is a separate `/stop/{id}` request. The app fetches names for the
visible list window plus a small look-ahead, over one kept-alive TLS connection,
and caches them in RAM. Rows show their sequence number until the name lands.

**3.3 Nearest stop is an explicit action, not a page load.** Ranking stops by
distance needs coordinates for every stop on the route, which is the full 60-120
request batch. It runs when the user asks for it, shows progress, and is
cancellable. It is capped at 200 stops per route.

**3.4 No maps, no in-app language switch.** `openMaps()` has no Crystal
equivalent and is dropped; the stop detail sheet shows coordinates as text
instead. English only, matching `DESIGN.md` §10. The evaluation's per-language
font analysis stands as future work and is not part of this phase — Traditional
Chinese in particular is a 0.6-2.5 MB asset question that belongs after the
Phase 14 `apps` partition exists.

**3.5 Location comes from Settings, not a second geolocation path.** Phase 9.5
already resolves and caches coordinates. This phase adds a read-only getter to
`crystal_core` and uses it. No new IP-geolocation call, no `hal().wifi` access
from app code.

## 4. Architecture

Two new components. Nothing else in the tree changes except one additive getter.

- `components/transit_service/` — OS-owned. One worker task pinned to Core 0,
  `esp_http_client` with the existing certificate bundle, a bounded incremental
  JSON scanner, the packed route-name index and its refresh job, an LRU stop-name
  cache, and its own result delivery path. No `lv_*` call anywhere in it.
- `components/bus_app/` — `BusApp : CrystalApp`, plus a procedural
  `bus_icon.c`. All LVGL, all on the LVGL task.
- `crystal_core` gains `crystal_location_get(double*, double*, char*, size_t)`,
  and `crystal_shell` gains one Device Status row. Nothing else. `crystal_core`
  does **not** gain a `UI_EVT_TRANSIT` enum value: transit
  results are larger than the 64-byte shared event payload and would force the
  queue element wider for every event in the system. The service delivers through
  its own listener and its own queue, drained by its own `lv_timer`. See the code
  guide for why this direction of dependency is the only one that holds.

## 5. Gates

Measured with `idf.py size-components` and heap watermarks on device, not
estimated. All figures incremental to the current 2,806,304-byte
`crystal_os.bin`.

| Gate | Limit |
| --- | ---: |
| Added flash (`bus_app` + `transit_service` + icon + ~6 KB index) | ≤ 100 KB |
| Index refresh peak PSRAM (32 KB inflate window + scanner) | ≤ 64 KB |
| Peak app-owned PSRAM, worst case (200-stop resolve) | ≤ 400 KB |
| Peak added heap during a TLS request | ≤ 60 KB |
| `onCreate()` to first frame | ≤ 80 ms |
| App-owned heap after `onDestroy()` | returns to pre-launch watermark |
| `lv_*` calls off the LVGL task | 0 |
| Blocking network calls on the LVGL task | 0 |

Exceeding the flash gate is a design conversation, not a waiver. Exceeding the
teardown gate is a leak and blocks the phase.

## 6. Exit criteria

1. Typing a route number and picking a direction lists that route's stops, with
   names filling in as the list scrolls. Keys that cannot extend the typed prefix
   are greyed as each character is entered, from the compiled index, with no
   network request.
1a. An index refresh check on a device whose lists are current transfers a 304 for
   KMB and issues no second KMB request. Forcing a changed KMB list installs the
   new index and the keypad's greying changes accordingly.
2. Selecting a stop shows live ETAs counting down, refreshed on a timer and on
   demand, with the age of the reading visible.
3. Saving a stop persists it. Reopening the app shows saved stops with ETAs
   before any new request completes.
4. Swiping away mid-lookup and returning restores the same route, direction, and
   stop from `CrystalState`, with no orphaned request writing to freed objects.
5. With WiFi down: saved stops still render, ETAs read as unavailable with their
   age, and nothing spins forever.
6. A cancelled nearest-stop resolve stops issuing requests within one in-flight
   request and leaves the app usable.
7. All §5 gates measured and recorded.

## 7. Compatibility with later phases

Stated per phase, because "does not conflict" is the question this proposal was
asked to answer.

- **Phase 12 (reliability/OTA).** Independent. The app adds no partition, no
  sdkconfig entry, and no boot-path work. It grows the image by ≤ 100 KB against
  2,436,576 bytes of free slot. The one interaction worth writing down: an OTA
  write and a transit fetch should not compete for TLS heap. Phase 12 already
  pauses weather during OTA; the transit service exposes
  `transit_service_suspend(bool)` for the same reason, and Phase 12's OTA worker
  calls it in the same place it pauses weather.
- **Phase 13 (app catalog).** The app is one more `kApps` row, so it appears in
  the catalog for free. Clear-data still needs `CrystalState::clear()`, which is
  Phase 13's own to-build. This phase keeps every persisted key inside its own
  `CrystalState` namespace, so `clear()` will wipe it completely with no per-app
  key list — which is exactly the property Phase 13's guide asks for.
- **Phase 14 (packages).** The registry gains `origin`; a builtin app stays
  `BUILTIN`. Nothing here assumes a folder or a manifest.
- **Phase 16 (declarative runtime / ABI v1 freeze).** This is the one to watch,
  and the interaction is favourable. ABI v1 §6.3 already declares
  `http.fetch(url, opts, cb)` async and host-allowlisted, and `location.get()`.
  Building a real streaming HTTP consumer now gives Phase 16 a working
  implementation to put behind those two entries instead of a design sketch. The
  requirement this phase carries: keep the transit service's public header free of
  LVGL and of app-specific UI concepts, so a future façade can sit on top of it.
- **Phase 17 (Lua).** The evaluation's preferred end state — a Lua bus app over a
  shared native transit service — is *this service* plus a script front-end. The
  service is written to be that shared layer from day one. Its measured cost is
  therefore paid once, not twice.

Nothing in this phase touches the partition table, the gesture arbiter, the
keyboard overlay, the power state machine, or the lifecycle contract. The route
index lives in existing SPIFFS if §8 ships, and in `.rodata` if it does not. The `onStart`
/`onStop` decision in `IMPLEMENTATION_PLAN.md` §7 stands; this app uses the four
dispatched hooks.

## 8. Licensing

The snapshot at `reference/gorhkbus/index.html` carries no license and no
repository. Treat it as an unlicensed reference for *behaviour*, not source. No
line of its markup, CSS, or JavaScript is copied. The two embedded WebP banners
are not used. The KMB and Citybus endpoints are public HK government data feeds
used under their own terms; the design document credits them on screen.

## 9. Open questions for the implementing session

1. **Citybus route-stop direction naming.** CTB uses `inbound`/`outbound` where
   KMB uses `O`/`I` plus `service_type`. Confirm against a live response before
   committing the direction model in §3 of the code guide.
2. **Keep-alive across `/stop/{id}` batches.** The 12-second figure for 120 stops
   assumes one reused TLS session. Measure it; if the server closes between
   requests, §3.3's nearest-stop feature needs its cap lowered rather than its
   timeout raised.
3. **Recent-routes cap.** Six is proposed. It is a `CrystalState` blob under the
   2048-byte value limit either way, so this is a screen-space question.
4. **Where the refreshed index lives.** SPIFFS (`/spiffs/transit/routes.idx`) is
   proposed, since ~6 KB is far past the 2,048-byte `CrystalState` value cap and
   the partition is already mounted. The alternative — never refresh, ship the
   baseline only, and rely on greyed keys staying tappable — is genuinely
   defensible for v1 and removes the scheduler, the inflate window, and the CTB
   hash comparison entirely. Decide before building §3.6; the keypad works either
   way.
5. **Does the index cover NLB, GMB, or MTR bus?** Out of scope as proposed, matching
   the source app's KMB/LWB + CTB coverage. Worth stating in the UI so absence
   does not read as a bug.
