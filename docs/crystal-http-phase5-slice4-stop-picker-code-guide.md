# Tracked Slice 4 Code Guide — Stop Picker and Stop Names

**Status:** Planned; implement in order and keep ETA/favorites outside this
slice.

**Plan:** [Slice 4 implementation plan](crystal-http-phase5-slice4-stop-picker-plan.md)

The selected route variant remains the only source of route-stop identity. The
stop picker adds source IDs, sequence, names, and optional coordinates around
that identity. Provider adapters own provider JSON and URL details; the app
owns copied page state and rendering.

## Non-negotiable rules

- Use `route + op + bound + service_type + stop_id` as the stop identity.
- Preserve provider source IDs exactly, including case and punctuation.
- Keep route sequence as an integer and sort numerically.
- Never match or merge stops by name alone.
- Never infer CTB direction from terminal text or array order.
- Never update LVGL from the bus worker task.
- Every request and event is correlated with a request ID and selected-route
  identity.
- A detail failure leaves the route-stop list usable with a sequence fallback.
- All strings and arrays have fixed bounds and one owner.

## S4.0 — Fixtures and endpoint confirmation

Files:

- `docs/crystal-http-phase5-slice4-stop-picker-plan.md`
- `components/bus_service/src/bus_provider_kmb.*`
- `components/bus_service/src/bus_provider_ctb.*`
- `tools/`
- `components/bus_service/fixtures/` if present

Capture the exact response bodies before coding parsers. Name fixture files by
provider and operation, direction, and language. A fixture must preserve the
fields needed to prove source ID, sequence, EN, TC, latitude, longitude, and
null/empty cases. If a provider has no single-stop endpoint, document the
confirmed bulk endpoint and adapt the bounded loader to it; do not silently
turn one route into an unbounded request fan-out.

## S4.1 — Data and event contracts

Files:

- `components/bus_service/include/bus_service.h`
- `components/bus_service/src/bus_service.c`
- provider headers
- `components/bus_app/include/bus_app.hpp`

Add or refine a detail type with bounded fields:

```c
typedef struct {
    char stop_id[20];
    char name_en[60];
    char name_tc[60];
    float lat;
    float lon;
    bool has_coordinates;
    bool resolved;
} bus_stop_detail_t;
```

Keep `bus_stop_t` as the route-stop row object. If detail fields are copied
into it, define that the successful `BUS_EVT_STOPS_LIST` array owns the row
storage and the app frees it after copying. A separate detail event carries a
copied value and no internal pointer.

Add an explicit detail request identity if the existing event identity cannot
carry `stop_id`. Preserve `bound` and `service_type` even though the detail
URL may not use them; they protect against stale page results.

## S4.2 — Provider parsers and transport

Files:

- `components/bus_service/src/bus_provider_kmb.c/.h`
- `components/bus_service/src/bus_provider_ctb.c/.h`
- `components/bus_service/src/bus_service.c`

Implement one parser per provider. Each parser must:

1. reject non-object JSON and missing required source IDs;
2. preserve sequence from the route-stop response;
3. accept missing localized fields as empty;
4. validate finite coordinates and discard invalid coordinates only;
5. truncate or reject overlong text according to the documented bounds;
6. return `ESP_ERR_NOT_FOUND` for a valid response with no matching stop;
7. avoid allocating a JSON-sized copy after the HTTP body is released.

Implement `REQ_TYPE_STOP_DETAIL` using the existing handoff pattern. Log the
provider, route/stop identity, URL operation, status, body size, attempts, and
final error. Use PSRAM for response bodies and free them exactly once after
parsing. Do not call a parser while holding a UI object or event callback.

Cancellation must terminate both queued and in-flight detail work. A late
success after cancellation is released and ignored by request ID.

## S4.3 — Cache and bounded loading

Files:

- `components/bus_service/src/bus_service.c`
- `components/bus_service/include/bus_service.h`
- `components/bus_app/src/bus_app.cpp`

Use a small service-owned cache keyed by operator, stop ID, and language
generation. The cache may be a fixed array or bounded LRU; establish a hard
capacity and log evictions. Do not persist the full stop database in this
slice. Persisting favorite names belongs to the favorites slice.

When the route-stop response arrives:

1. show rows immediately in route sequence order;
2. copy cached details into matching rows;
3. request only uncached visible rows or a bounded batch;
4. update rows on the LVGL task as detail events arrive;
5. retain fallback text for failed or offline rows.

Deduplicate a detail request already pending for the same composite identity.
Cancel pending detail work when leaving the page or replacing the route.

## S4.4 — Stop-picker UI and selection

Files:

- `components/bus_app/src/bus_app.cpp`
- `components/bus_app/include/bus_app.hpp`

Add page state for:

- the stop-list request ID;
- a detail-generation or page-generation counter;
- copied selected variant;
- route-stop array ownership;
- selected stop identity and copied display name.

Render rows with a stable layout such as `01  <name>` and a secondary
coordinate or source label only when useful. Keep labels bounded and use LVGL
ellipsis for long names. Center `Loading bus stops...`, `Waiting for network`,
and terminal error text in the list area. Do not expose request IDs in normal
user-facing text.

On row activation, copy the full `bus_stop_t` into selected-stop state and log:

```text
Stop selected route=<...> op=<...> bound=<I|O> service_type=<...> stop_id=<...> seq=<...>
```

Do not queue ETA yet. Back from the picker cancels detail work and returns to
the prior Search or route page according to the current navigation contract.

## S4.5 — Tests and device checks

Host checks should cover:

- valid KMB and CTB detail payloads;
- missing EN or TC text;
- invalid coordinates;
- duplicate and overlong IDs/text;
- empty and malformed payloads;
- cache hit/miss and provider-qualified key collisions;
- stale event rejection after direction replacement.

Device checks should cover:

1. cold route-stop load for KMB and CTB in both directions;
2. named rows appearing after placeholder rows;
3. warm cache reload after revisiting the route;
4. Back and immediate selection of the other direction;
5. Wi-Fi loss during route-stop and detail requests;
6. reboot and route catalog cache reload;
7. ten repeated selections with no watchdog and no stale names;
8. heap, TLS retry, AES allocation, and CPU observations.

Required commands:

```text
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
idf.py build
git diff --check
```

Record the result with the plan's evidence template. A transport timeout,
provider parse failure, and stale UI event are separate failures and should be
reported separately.
