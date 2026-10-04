# Phase 9.7 — Transit (HK Bus): code guide

Shapes, threading, and call sites for the implementing session. Folds into
`CODE_GUIDE.md` on close, in the style of §"Phase 9.5 — Weather". Scope is in
[`PHASE_9_7_TRANSIT_PROPOSAL.md`](PHASE_9_7_TRANSIT_PROPOSAL.md); behaviour is in
[`PHASE_9_7_TRANSIT_DESIGN.md`](PHASE_9_7_TRANSIT_DESIGN.md).

Read `CODE_GUIDE.md` §"Conventions" first. Everything there applies unchanged, and
two entries are load-bearing here: every `lv_*` call is on the LVGL task, and no
app touches NVS except through `state()`.

## 1. Layout

```
components/transit_service/
  include/transit_service.h        C header. No LVGL, no C++, no UI concepts.
  include/transit_routes.h         packed index accessors
  src/transit_service.c            worker task, HTTP, dispatch
  src/transit_json.c               bounded incremental scanner
  src/transit_routes.c             index lookup + filter
  src/transit_index_data.c         generated: the packed baseline
  tools/gen_route_index.py         regenerates transit_index_data.c
components/bus_app/
  include/bus_app.hpp
  src/bus_app.cpp                  BusApp : CrystalApp
  src/bus_keypad.cpp               filtering keypad widget
  src/bus_icon.c                   procedural, like clock_icon.c
```

`transit_service` is a C component on purpose. It is the layer Phase 17 puts a Lua
binding on and Phase 16 puts `http.fetch` behind, and a C header with handles and
PODs is what survives that. If it ends up including `lvgl.h`, the design is wrong.

## 2. Changes to existing code

Two, both additive.

**`crystal_shell`** gains one row in Device Status, `Route list`, showing the date
`tr.checked` records — the place a user looks when a key greys out that shouldn't
(design §2.2). It reads through a service getter, not by opening the service's NVS
namespace. If §8 is cut, this row is cut with it.

**`crystal_core`** gains one getter:

```c
// Latitude/longitude/city as resolved by Phase 9.5 (Settings > Region & Time,
// then cached IP result, then the Hong Kong default). Returns false when no
// location is known yet, in which case nothing is written through the pointers.
bool crystal_location_get(double *latitude, double *longitude,
                          char *city, size_t city_size);
```

It reads the same `s_weather_latitude`/`s_weather_longitude` under
`s_weather_location_mux` that `weather_fetch()` reads. Do not add a second
geolocation path, and do not read `weather.lat` from NVS in app code — a
`nvs_open` per call is a global lock plus a malloc, and the transit app would be
doing it from a scrolling list.

Nothing else changes anywhere. In particular **do not add a
`UI_EVT_TRANSIT` enum value.** `EventMessage` is a 64-byte fixed payload
(`kEventDataMax`) shared by every event in the system; a stop list or an ETA set
does not fit, and widening the union costs every queued event in the firmware. §5
is the delivery path instead.

## 3. Data model

Fixed-size PODs, no `std::string`, no heap per record.

```c
typedef enum { TRANSIT_OP_KMB = 0, TRANSIT_OP_CTB = 1 } transit_operator_t;

typedef struct {
    char     route[5];          // "68X", NUL-terminated, max 4 chars
    uint8_t  op;                // transit_operator_t
    char     bound;             // 'O' or 'I' (KMB); mapped from
                                // outbound/inbound for CTB
    uint8_t  service_type;      // KMB only; 1 for CTB
    char     orig[40];
    char     dest[40];
} transit_variant_t;             // 91 B

typedef struct {
    char     stop_id[20];       // opaque operator id
    uint16_t seq;
    char     name[48];          // empty until resolved
    float    lat, lon;          // 0 until resolved
} transit_stop_t;                // 76 B
```

A 200-stop route is 200 × 76 = 15.2 KB. That allocation is PSRAM
(`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`), owned by the service, freed when the
app releases the route handle.

`bound` and `service_type` are the direction identity and both are required for
KMB. Confirm the CTB direction spelling against a live `/route-stop/CTB/...`
response before committing this — it is open question 1 in the proposal.

## 4. The route name index

The point of §3.1 of the proposal, and the part worth reading twice: **the device
never parses the 349 KB route table to run the keypad.** It holds only the names.

```c
// transit_routes.h
// Sorted ascending by strcmp over the name. Sorted-ness is the invariant every
// lookup below depends on; the generator asserts it and so does a debug check at
// first use.
typedef struct {
    char    name[4];   // NOT NUL-terminated; space-padded to 4. See below.
    uint8_t ops;       // bit 0 = KMB, bit 1 = CTB
} transit_route_name_t;   // 5 B

extern const transit_route_name_t transit_route_index[];
extern const uint16_t transit_route_index_count;
```

`name[4]` is space-padded rather than NUL-terminated so the record is exactly 5
bytes with no padding hole and `memcmp` over 4 bytes is a total order that matches
`strcmp` on the unpadded strings (space, 0x20, sorts below every character in the
alphabet `0-9A-X`). 1,048 records is 5,240 bytes of `.rodata`.

Two operations, both on the LVGL task, both allocation-free:

```c
// Distinct characters that can legally follow `prefix`, as a bitmask over
// transit_charset[] (28 characters: "0123456789ABCDEFGHKMNOPRSTWX").
uint32_t transit_route_next_mask(const char *prefix, size_t prefix_len);

// Nonzero when `name` is itself a complete route name; the value is the
// operator mask. Drives whether the submit key lights.
uint8_t transit_route_is_complete(const char *name, size_t len);
```

`next_mask` binary-searches the lower and upper bound of the prefix range, then
walks that range collecting `name[prefix_len]`. The range after one character is a
few hundred records at worst and after two it is a handful, so the walk is bounded
by the index size only in the degenerate empty-prefix case — which the keypad does
not call, because with nothing typed every key is live. No allocation, no recursion,
no network.

Both functions must tolerate `prefix_len >= 4` by returning 0 / the exact-match
result. A route name is never longer than 4 characters and the keypad must not be
the thing that enforces it.

### 4.1 Greying is advisory

```cpp
// bus_keypad.cpp — a key outside the mask is dimmed, never disabled.
lv_obj_set_style_opa(key, live ? LV_OPA_COVER : LV_OPA_40, 0);
// Deliberately NOT lv_obj_add_state(key, LV_STATE_DISABLED).
```

The index is a cache of someone else's data and it can be behind. If a greyed key
refused the tap, a device that had never refreshed could not reach a route that
exists — the UI would be enforcing a guess as a rule. The API is the authority; the
index only decides what looks promising. Design doc §2.2 states this to the user as
"grey means we don't think so, never you can't", and the code has to keep that
promise.

The keys' positions are fixed for the same reason a list row does not reflow when
its name resolves: a moving target under a travelling finger produces a mistap the
user cannot attribute to anything.

### 4.2 Regenerating the index

`tools/gen_route_index.py` fetches both route lists, takes the union of distinct
`route` values, sorts, space-pads, and emits `transit_index_data.c` with the source
timestamp in a comment. Run it by hand; it is not a build step, because a build that
depends on a third-party endpoint being up is a build that fails offline.

Measured 2026-09-11: KMB 792 distinct names of 1,598 rows, CTB 406, union 1,048,
overlap 150, max length 4, alphabet `0123456789ABCDEFGHKMNOPRSTWX`.

## 5. Result delivery

The service runs on Core 0 and every result must reach the LVGL task. Weather goes
through `crystal_core`'s shared queue and a `crystal_shell_weather_event()` hook,
which works because a reading is 40 bytes. Transit results are too large for that
payload, so the service owns its own path:

```c
// Called on the LVGL task, from the service's own drain timer.
typedef void (*transit_listener_t)(const transit_event_t *event, void *user_data);

// Installs the single listener and lazily creates the drain lv_timer.
// Passing NULL clears the listener, deletes the timer, and drops every result
// still queued. BusApp::onDestroy() must call it with NULL.
void transit_service_set_listener(transit_listener_t cb, void *user_data);
```

The dependency direction is the whole reason for this shape. `bus_app` requires
`transit_service`; `transit_service` requires neither `bus_app` nor `crystal_shell`.
If the service called into the app, adding a second consumer — the Phase 16 façade,
or a dashboard tile — would mean editing the service. The listener keeps that
additive.

`transit_event_t` carries a request id, a status, and a small union: never a
pointer the worker still owns, never a `char *` into a response buffer.

**A result can arrive after the app is gone.** Same hazard as
`CODE_GUIDE.md` §"A reading can arrive after the app is gone", and worse here
because a nearest-stop resolve can have 200 requests behind it. Two defences, both
required: clear the listener in `onDestroy()`, and stamp every request with a
monotonic id that the app compares before touching a widget. A stale id is dropped
silently — it is not an error, it is a race the design permits.

## 6. Threading

```c
xTaskCreatePinnedToCore(transit_worker, "transit", 7168, NULL, 2, &s_worker, 0);
```

Core 0, priority 2, matching `crystal_service`. 7168 bytes because a TLS handshake
plus the scanner's frame needs more than the 4 KB default; measure the watermark and
record it rather than trusting this number.

Created on first use, not at boot. A device whose owner never opens the bus app pays
no task and no stack for it.

- One request in flight at a time. The queue is depth 4 and a full queue rejects
  rather than blocks — the LVGL task must never wait on the worker.
- `transit_service_cancel(request_id)` sets a flag the worker checks between
  requests and inside the read loop. Cancellation is honoured within one in-flight
  request; it does not abort a socket mid-read.
- `transit_service_suspend(bool)` stops new requests without tearing the task down.
  Phase 12's OTA worker calls this where it pauses weather.

## 7. HTTP

`esp_http_client` with `crt_bundle_attach = esp_crt_bundle_attach`, as Weather
does. **Use `open`/`fetch_headers`/`read`/`close`, never `perform()`** —
`CODE_GUIDE.md` §"`perform()` does not leave you a body" is the same trap and it
bites harder here, where bodies are streamed.

Weather's `http_get_json()` cannot be reused: it targets a 1,024-byte static
buffer. Transit needs a streaming read into a fixed window with an incremental
scanner over it.

### 7.1 Endpoints

| Purpose | Endpoint | Measured |
| --- | --- | ---: |
| KMB route variants | `…/kmb/route/{route}` | small |
| KMB route-stop | `…/kmb/route-stop/{route}/{bound}/{service_type}` | 10-20 KB |
| KMB stop detail | `…/kmb/stop/{stop_id}` | small |
| KMB ETA | `…/kmb/eta/{stop_id}/{route}/{service_type}` | small |
| CTB equivalents | `…/citybus/route/CTB/{route}` etc. | small |
| KMB route table (index refresh only) | `…/kmb/route/` | 348,938 B / 38,322 B gzip |
| CTB route table (index refresh only) | `…/citybus/route/CTB` | 111,887 B, no gzip |

### 7.2 Keep-alive for stop batches

Resolving 60-200 stop names is 60-200 requests to one host. Reuse a single client
handle with `keep_alive_enable = true` across the batch instead of building a TLS
session per stop — the difference is roughly 100 ms per stop against 600-900 ms.
Measure it; open question 2 in the proposal exists because if the server closes
between requests, the answer is to lower the 200-stop cap, not to raise a timeout.

## 8. Index refresh

Runs on the worker, guarded by everything in §3.6 of the proposal: at most every
24 h (CTB weekly), online only, app not in the foreground, never on the boot path.

```c
// NVS, via the service's own namespace -- not CrystalState, which belongs to the
// app and whose 2048-byte value cap the index exceeds anyway.
"tr.etag"     char[64]    KMB ETag, verbatim
"tr.ctbhash"  uint8[32]   SHA-256 of the last CTB body
"tr.checked"  int32       epoch of the last successful check
```

**KMB.** Send `If-None-Match: <stored etag>` and `Accept-Encoding: gzip`. A `304`
with a zero-length body is the steady state and costs a few hundred bytes; return
immediately. On `200`, inflate and scan. Verified against the live endpoint on
2026-09-11: the `ETag` is content-derived (`W/"5530a-…"`, the hex prefix being the
body length) and `If-None-Match` returns `304` with `size_download=0`.

**Do not use `generated_timestamp` as a change detector.** It tracks the
`max-age=300` cache window, not the content: two fetches two seconds apart returned
the identical value, and it advances every window whether or not a route changed.
Trusting it means either a needless 349 KB fetch every five minutes or a change
missed entirely, depending on which direction the comparison is written.

**CTB.** No `ETag`, and `Accept-Encoding: gzip` returned the full 111,887 bytes, so
the transfer cannot be avoided. Stream it, SHA-256 the body as it passes, and
compare at the end; discard when unchanged. This is why CTB is weekly and KMB is
daily — one is nearly free and the other is not.

**Inflate.** The S3 ROM exports `tinfl_decompress` (`esp32s3.rom.ld`, group
`miniz`), so gzip costs no flash. Strip the 10-byte gzip header and any FNAME/FEXTRA
fields before handing the deflate stream to `tinfl`, and give it a 32 KB PSRAM
dictionary window. `tinfl_decompress` is the streaming entry; the
`_mem_to_*` variants want the whole input resident and are the wrong ones here.

Write the new index to `/spiffs/transit/routes.idx` via a `.tmp` file and a rename,
so a power loss mid-write leaves the previous index intact. Load order at first use
is SPIFFS file, then the compiled baseline. A file that fails its header check or
sortedness assertion is deleted, not used — a corrupt index would grey out real
routes with no way for the user to tell why.

Proposal open question 4 asks whether to ship §8 at all in this phase. The keypad
works without it. If it is cut, cut the whole section: the scheduler, the inflate
window, the CTB hash, and the SPIFFS file, keeping only
`transit_index_data.c`.

## 9. JSON

A bounded incremental scanner in `transit_json.c`, not cJSON and not string
matching.

- cJSON is out for the route table: it builds a full DOM, which is the
  600 KB-1.5 MB failure mode the evaluation flagged. It is acceptable for the small
  per-route and ETA responses, and using it there and the scanner for bulk is a
  reasonable split — but one scanner for everything is less code and no second
  dependency on a parser's allocation behaviour.
- Weather's `strstr` approach does not generalise. It works on a 1 KB document with
  known unique keys; on a 349 KB array it cannot tell which object a key belongs to.
- The scanner reads a fixed window (4 KB is enough), tracks depth, and emits
  key/value pairs for the current object. Values longer than their destination are
  truncated, never wrapped and never allocated.
- Every field is bounds-checked against its destination with `strlcpy`. This is
  third-party data on a device with no MMU; a 3,000-character `dest_en` must
  truncate, not corrupt the heap.
- A malformed document fails the whole request and leaves the cache untouched. Do
  not salvage partial records.

## 10. State

`CrystalState`, keys ≤ 7 characters — `make_key()` rejects longer ones and the
write then silently does nothing (`CODE_GUIDE.md` §"Phase 4 — CrystalState").
Values ≤ 2,048 bytes.

| Key | Contents |
| --- | --- |
| `saved` | packed saved stops, ≤ 8 × (route, op, bound, service_type, stop_id) |
| `savenm` | saved stops' names, so the Saved tab paints before any request |
| `recent` | last 6 route names |
| `lastrt` | route/bound/service_type in flight when the app was destroyed |
| `laststp` | stop id in flight when the app was destroyed |
| `tab` | which tab was open |

Every key stays in this namespace, so Phase 13's `CrystalState::clear()` wipes the
app completely with no per-app key list. Do not put transit data in a global NVS
namespace, and do not put the route index here — §8 explains where it goes.

## 11. Lifecycle

Four dispatched hooks. `onStart`/`onStop` stay undispatched per
`IMPLEMENTATION_PLAN.md` §7.

- `onCreate()` — build the tree, paint from `CrystalState` first, then install the
  listener and request. Budget is 80 ms under the LVGL lock; the 5 s task watchdog
  is the real failure mode. Never make a network call here.
- `onPause()` — delete every `lv_timer`, cancel any in-flight resolve, persist the
  current route/stop/tab.
- `onResume()` — repaint from cache, reinstall the listener, recreate timers,
  request once if stale.
- `onDestroy()` — `transit_service_set_listener(NULL, NULL)`, cancel outstanding
  requests, release the route handle, then null every widget pointer. Weather nulls
  its pointers for exactly this reason and the comment there says why: a late event
  writing to freed objects.

Position the root at `(0, 0)`, not `area.x1/y1` — `getVisualArea()` is display
coordinates and the wrong offset shows up as a page that scrolls
(`CODE_GUIDE.md` §"Layout uses the visual area").

## 12. Registration

```cpp
// main/main.cpp
static CrystalApp *make_bus_app() { return new BusApp(); }

static const CrystalAppEntry kApps[] = {
    {"dev_tester", make_dev_tester_app, true, 0},
    {"clock",      make_clock_app,      true, 2},
    {"weather",    make_weather_app,    true, 3},
    {"calculator", make_calculator_app, true, 4},
    {"bus",        make_bus_app,        true, 5},
};
```

Add `bus_app` to `main/CMakeLists.txt` `REQUIRES`. `bus_icon_prepare()` is called
from the `BusApp` constructor, as every other app calls its own.

## 13. What this does not do

Written down so it is not mistaken for missing work.

- No route table on the device. §4 holds names only.
- No fuzzy search and no suggestions. Exact names, with the keypad making a typo
  hard to enter in the first place.
- No stop search across the territory. That needs an index of ~6,600 stops with
  coordinates, which is a different feature with a different budget.
- No map launch. Coordinates as text instead.
- No language switch. English, per `DESIGN.md` §10. The evaluation's font analysis
  is future work and Traditional Chinese in particular waits for the Phase 14
  `apps` partition.
- No NLB, GMB, or MTR bus. KMB/LWB and CTB, matching the source app.
