# HTTPS/TLS Phase 5, Slice 1 — Bus Service Contracts and CTB Normalization

**Status:** Ready for implementation
**Phase:** HTTPS/TLS Phase 5, first implementation slice
**Parent plans:**

- [Incremental Global HTTPS/TLS plan](Incremental-Global-HTTPS:TLS-plan.md)
- [Bus app integration plan](bus-app-integration-plan.md)

This slice starts Phase 5 after the KMB catalog and KMB route-stop migrations.
It adds the service contracts needed by the bus app and moves CTB route-stop
requests through `crystal_http`. It does not complete Phase 5. KMB route
variants, ETA, CTB catalog, CTB ETA, weather, and the remaining direct HTTP
paths are later slices.

The implementation is divided into independent gates. Finish and record one
step before starting the next. If a validation fails, stop at that step and
keep the last passing state available for review.

## Rules for every step

The TLS plan owns transport behavior: TLS setup, certificate validation, PSRAM
response buffering, body limits, timeout, retry, cancellation, response
release, and transport diagnostics. The bus plan owns provider parsing,
normalized records, service events, and UI state.

The callback runs on the `crystal_http` worker. It may copy a bounded response
into a service handoff context, record status/error/attempts, release the
framework response exactly once, and signal the bus worker. It must not call
LVGL, invoke the app listener, parse JSON, or free a context still used by the
bus worker.

`bus_service` owns a handoff context while parsing. `bus_app` owns event
payloads after delivery on the LVGL task. Every terminal request produces one
event and one cleanup path. Every event carries a bus request id; every
framework submission also records its framework request id.

Do not add ETA merging, favorites, language switching, or Nearby in this
slice. They depend on the contracts below.

## Step 0 — Freeze the CTB baseline and inspect the response schema

**Purpose:** Capture provider behavior and the current memory boundary before
changing CTB code.

**Code change:** Documentation only.

**Device validation:** With the current image, select one CTB route in both
directions if the current UI exposes it. Record:

```text
route, direction, generated URL, HTTP status, body bytes, stop count/order
internal heap before/after, largest block before/after, PSRAM before/after
```

Capture one successful CTB route response and one route-stop response. Save
the relevant JSON fields or a redacted fixture. Confirm the actual CTB field
names for route, direction, stop id, sequence, names, and coordinates. Also
record whether CTB returns an empty `data` array or a missing `data` field for
no-result cases.

**Pass condition:** The response schema is recorded, both directions are
understood, and the current CTB path remains usable after a successful fetch.
No transport code changes are made in this step.

## Step 1 — Define normalized records and request identity

**Purpose:** Give KMB and CTB one stable service contract before adding another
provider implementation.

**Files:**

- `components/bus_service/include/bus_service.h`
- `components/bus_service/src/bus_service.c`
- `components/bus_app/src/bus_app.cpp` only where field names require it

**Change:** Preserve fixed-size C records and add or correct fields needed by
both providers. A route variant identity is:

```text
route + operator + bound + service_type
```

A stop identity retains the provider source id:

```text
route + operator + bound + service_type + provider stop id
```

Stop and ETA requests must carry operator, bound, and service type. Do not
assume KMB and CTB stop ids are interchangeable. Keep existing ABI names when
possible; update all C and C++ call sites together if a field changes.

Document event ownership in the header. Successful arrays are owned by the
listener after LVGL delivery; error events contain no allocated array.

**Validation:**

```bash
git diff --check
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
idf.py build
```

Compile the public header from both a C source and a C++ source, and inspect
the diff for LVGL pointers or provider-specific HTTP handles in the public
contract.

**Pass condition:** The firmware builds, the public records represent both
providers, and each event payload has one documented owner and cleanup path.

## Step 2 — Add provider parser boundaries and CTB fixtures

**Purpose:** Validate CTB normalization without involving TLS or UI timing.

**Files:** Start with internal helpers in
`components/bus_service/src/bus_service.c`. Split into provider files only if
the code becomes large enough to warrant it.

**Change:** Add a CTB route-stop parser with this contract:

- malformed JSON returns a parse error;
- missing or non-array `data` returns a controlled error;
- an empty array is an explicit empty result;
- invalid records are skipped only when usable records remain;
- no valid records never becomes a successful stop event;
- output arrays use PSRAM and have one documented owner;
- source stop ids and sequence order are preserved.

Add fixtures for one valid response, malformed JSON, missing `data`, empty
`data`, and one invalid record. Keep KMB parsing behavior unchanged.

**Validation:** Run the parser fixture or host check for every input. Verify
the expected error code, output count, order, source ids, and one free of every
allocated output. Then run `idf.py build` and `git diff --check`.

**Pass condition:** All fixture cases produce the documented result without a
leak, invalid free, or successful event for unusable data.

## Step 3 — Submit CTB route stops through `crystal_http`

**Purpose:** Move CTB stop transport to the shared TLS owner while the
existing parser and UI result path remain isolated.

**Files:**

- `components/bus_service/src/bus_service.c`
- `components/bus_service/CMakeLists.txt` only for temporary controlled test
  switches, default OFF

**Change:** Add a dedicated `CTB_STOPS_OWNER_ID` and a CTB handoff context
containing the bus id, route, bound, operator, framework id, semaphore,
status/error/attempts, and bounded PSRAM body.

Submit one `crystal_http_get()` request. Do not call the legacy
`http_get_json()` helper. Start with this measured policy:

```text
timeout: 8 seconds
max attempts: 3
backoff: 500 ms, capped at 2 seconds
keep-alive: disabled
body limit: measured CTB maximum plus a bounded safety margin
```

The diagnostic callback copies the body, records both ids and response fields,
releases the framework response once, and signals the worker. Do not reconnect
the CTB parser until the handoff is proven.

**Validation:** Build and run one CTB inbound and one outbound request. Confirm
logs similar to:

```text
bus_service: CTB stops submitted route=... bound=... bus_id=... framework_id=...
crystal_http: request attempt id=... attempt=1/3
crystal_http: response status=200 body=... bytes
bus_service: CTB stops response ... status=200 body=... attempts=1 error=ESP_OK
```

Run a temporary invalid-host failure if needed. Confirm the final callback,
one response release, no crash, and stable heap/PSRAM. Keep the switch OFF for
normal firmware.

**Pass condition:** CTB transport succeeds in both directions, controlled
failure reaches one terminal callback, and the existing UI behavior is not
claimed as migrated until Step 4.

## Step 4 — Connect the CTB parser and normalized stop event

**Purpose:** Deliver CTB stops through the existing app event path.

**Files:**

- `components/bus_service/src/bus_service.c`
- `components/bus_service/include/bus_service.h` if event fields need revision
- `components/bus_app/src/bus_app.cpp`

**Change:** Parse the owned CTB body in the bus worker, create normalized
`bus_stop_t` records, and emit `BUS_EVT_STOPS_LIST` with the original bus
request id. Emit one controlled error for HTTP failure, invalid JSON, missing
or empty data, allocation failure, and transport failure. Never pass the
handoff body or an error string as a stop array.

The app accepts the event only when the request id and selected route identity
match the current stop page. It frees the received array exactly once and
shows a usable empty or error state.

**Validation:** Select one CTB inbound and one outbound route. Confirm the
displayed rows preserve provider sequence and source ids. Exercise one
malformed or empty fixture and select another route afterward.

**Pass condition:** Valid CTB rows appear in the stop page, invalid responses
produce one error or empty state, and a later route remains usable.

## Step 5 — Add CTB cancellation and stale-result handling

**Purpose:** Make CTB requests safe during page and network changes.

**Files:**

- `components/bus_service/src/bus_service.c`
- `components/bus_service/include/bus_service.h`
- `components/bus_app/src/bus_app.cpp`

**Change:** Network loss, app pause/destroy, Back, and a superseding stop
request cancel `CTB_STOPS_OWNER_ID`. Mark the request stale before cancelling.
No retry may start while the network lease is lost. A late callback releases
its own framework response and can signal only a live handoff context.

**Validation:** Perform four device checks:

1. disconnect Wi-Fi during TLS connection;
2. disconnect Wi-Fi during body download;
3. press Back during an active CTB request;
4. select route B before route A completes.

Record cancellation latency, attempts, terminal error, response releases,
stale-event handling, and heap/PSRAM after cleanup.

**Pass condition:** Each case has one terminal event and one response release,
does not consume the remaining retry budget, adds no stale rows, and leaves the
app usable.

## Step 6 — Remove only the CTB stop direct-client path

**Purpose:** Close the CTB route-stop ownership boundary after transport,
parser, and lifetime behavior pass.

**Files:**

- `components/bus_service/src/bus_service.c`
- `components/bus_service/CMakeLists.txt` if a temporary test hook is removed

**Change:** Remove the CTB stop branch's direct `esp_http_client` ownership
and local retry loop. Leave unrelated CTB catalog, route variant, ETA, and
weather paths untouched until their own migrations.

**Validation:** Run:

```bash
rg -n "esp_http_client|esp_tls|esp_crt_bundle|http_get_json" \
  components/bus_service/src/bus_service.c
git diff --check
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
idf.py build
```

The source result may still show legacy helpers used by other operations, but
the CTB stop branch must contain no direct client call. Repeat one CTB inbound,
one outbound, one cancellation, and one stale-selection run.

**Pass condition:** CTB stops still work through `crystal_http`, the direct
client is absent from that branch, and all earlier device checks still pass.

## Step 7 — Record the slice and hand off to the next Phase 5 slice

**Purpose:** Make the result reviewable before migrating another operation.

Record:

```text
phase5 slice=1
firmware commit=<sha>
step=<0..6>
operator=<KMB|CTB> route=<route> bound=<I|O>
bus request=<id> framework request=<id>
status=<...> error=<...> attempts=<...> body=<...> stops=<...>
response releases=<...>
heap/PSRAM before and after=<...>
result=pass|fail|deferred
known follow-up=<...>
```

The slice is ready to hand off when Steps 0 through 6 pass and the normal test
switches are disabled. The next Phase 5 slice can migrate KMB route variants
or the CTB catalog, using the same provider contract and handoff. Return to
the deferred KMB Phase 4 failure/lifetime matrix after CTB and weather clients
are available.

## Common build commands

From the repository root:

```bash
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
idf.py build
git diff --check
grep CRYSTAL_HTTP_PHASE build/CMakeCache.txt
```

All temporary Phase 3/4 test options must be `OFF` during normal CTB runs.
