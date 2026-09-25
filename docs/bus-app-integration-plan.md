# Bus App Integration Readiness and Incremental Plan

**Date:** 2026-09-25  
**Status:** Working implementation plan  
**Scope:** The first two tabs (Favorites and Search), route and stop drill-down, live ETA, and persistence. Nearby remains deferred.

This document compares the requested workflow in [`bus-app-workflow.md`](bus-app-workflow.md), [`bus-app-ui-design.md`](bus-app-ui-design.md), and [`bus-app-code-guide.md`](bus-app-code-guide.md) with the code that currently exists in `components/bus_service` and `components/bus_app`. Each implementation step should be completed and verified before starting the next one.

## Current conclusion

The current code is a UI and service skeleton. It can create the two tab containers, accept a route string, queue a route request, queue stop and ETA requests, render persisted favorite records, and start a 30-second timer. It cannot yet complete the requested user flow.

The largest blockers are:

1. The route index is a seven-entry test fixture, not the documented full index. It is in [`bus_index_data.c`](../components/bus_service/src/bus_index_data.c).
2. Route discovery only queries KMB. CTB/NWFB variants are not aggregated.
3. Stop names and coordinates are never resolved. `REQ_TYPE_STOP_DETAIL` is queued but intentionally discarded by the worker.
4. The service listener is called directly from the worker task, although the design requires delivery on the LVGL task. LVGL objects must not be updated from the worker.
5. The app has no direction chooser, stop picker, ETA detail page, favorite add/remove action, or edit/delete mode.
6. ETA requests do not carry a direction. A favorite can therefore receive the opposite direction, and co-operated routes cannot be merged.
7. The planned language and seven-day cache system does not exist. The repository currently has a 4 MB SPIFFS partition, while the documents describe LittleFS.

The existing code also contains correctness issues that must be handled before adding screens:

- `bus_route_next_mask()` can enable characters from unrelated routes because its second comparison does not require the typed prefix to match.
- `onFavoriteClicked()` receives an index as event user data but casts that value to `BusApp *`.
- `updateFavoriteCard()` and `showError()` are stubs, so successful background responses are not reflected in the UI.
- Route and stop request failures are emitted as `BUS_EVT_ROUTE_VARIANTS` or `BUS_EVT_STOPS_LIST` with an error status, while the app only reports `BUS_EVT_ERROR`.
- The request queue has depth four, but one refresh can enqueue up to eight favorites. Requests can be dropped without an app-visible result.
- `Favorite::op` stores one operator, but a co-operated route needs an operator set or separate normalized ETA requests.
- The current favorite ETA match uses only `stop_id`; the same stop can have multiple route, direction, service type, or operator records.

## Capability matrix

| # | Workflow capability | Current implementation | Readiness | Required work |
|---:|---|---|---:|---|
| 1 | Full route catalog bootstrap and cache | Runtime KMB/CTB route catalog fetch, in-memory index, atomic SPIFFS cache, seven-day freshness, provider progress logs, and Search loading lock | Complete | Stop details are intentionally lazy and belong to the route-stop workflow; language-specific stop data belongs to points 11/20. NWFB remains skipped unless a live endpoint is confirmed. |
| 2 | Launch on Favorites | `onCreate()`, `buildFavoritesTab()`, `rebuildFavoritesView()` | Complete | Persisted favorites render immediately; cached ETAs remain visible during refresh, with loading, age, empty, partial-failure, and no-network states. |
| 3 | Search tab and keypad | `buildSearchTab()`, `buildKeypad()` | Partial | Compact reference-style keypad, reset key, loading lock, and scrollable result area are in place; add catalog-backed result rows, adaptive key styling, and route selection. |
| 4 | Route prefix validation | `bus_route_is_complete()` | Partial | Replace placeholder index with the catalog; fix `bus_route_next_mask()` prefix matching and operator metadata. |
| 5 | Route result sorting and both directions | None | Missing | Add route result model and sorted variant list. |
| 6 | KMB route variants | `process_route_request()` | Partial | Validate all response fields and stale request handling. |
| 7 | CTB route variants | None | Missing | Add CTB request and normalization. |
| 8 | NWFB compatibility | Enum only; non-KMB falls through to CTB | Missing | Treat NWFB as retired/merged unless a live endpoint is confirmed; keep an extensible operator adapter. |
| 9 | Direction chooser | Event only logs | Missing | Build variant page and select one normalized variant. |
| 10 | Route-stop list | `process_stops_request()` returns IDs and sequence | Partial | Add request correlation, operator-specific paths, empty results, and display state. |
| 11 | Stop names and coordinates | `BUS_EVT_STOP_DETAIL` declared; worker does nothing | Missing | Implement detail requests or a bounded cache/bulk loader, with EN/TC fields. |
| 12 | Stop picker | Event only logs | Missing | Scrollable list, placeholder names, loading/error rows, and back navigation. |
| 13 | ETA for one operator | `process_eta_request()` | Partial | Correct API parsing, direction filtering, clock-invalid behavior, and error/empty result semantics. |
| 14 | ETA for co-operated route | None | Missing | Issue both requests, merge and sort predictions, deduplicate, and retain per-operator error state. |
| 15 | ETA refresh cadence | 30-second app timer | Partial | Make visible-page scoped, prevent duplicate queueing, and preserve last good data on errors. |
| 16 | ETA detail page | None | Missing | Add detail page, freshness, manual refresh, no-info text, and save toggle. |
| 17 | Favorite add/remove | NVS load/save only | Missing | Define identity, duplicate detection, max-eight feedback, toggle action, and atomic persistence. |
| 18 | Favorite edit/delete | None | Missing | Edit mode and trash action; rebuild list after deletion. |
| 19 | Favorite card update | Stub | Missing | Keep card pointers or rebuild safely; show three merged ETAs and age. |
| 20 | Language selection | No language state or TC fields | Missing | Add active language, endpoint/query selection, cache generation, and UI strings. |
| 21 | Seven-day refresh | No timestamp check | Missing | Implement valid-clock checks and daily/app-open expiry checks. |
| 22 | Offline behavior | None | Missing | Use cached names/routes/favorites and show stale/no-network status. |
| 23 | Nearby tab | Deliberately absent | Deferred | Add only after location input and coordinate cache decisions are implemented. |

## Data and service contract that must be settled first

### Normalized identity

Use a normalized route variant as the identity for discovery:

```text
route + operator + bound + service_type
```

Use a favorite identity that includes the stop:

```text
route + operator-set + bound + service_type + stop_id
```

For a co-operated route, `operator-set` contains every operator that serves that route variant and stop. If the APIs use different stop IDs for the same physical stop, retain the operator-specific IDs inside the favorite record rather than assuming uppercase IDs are globally unique.

The current `Favorite` structure has one `op` field and must be revised before co-operated favorites are implemented. The ETA request contract also needs `bound` (or a guaranteed response filter) so an inbound favorite cannot display outbound predictions.

### Event delivery and ownership

The service should own the worker task and HTTP buffers. It should post completed events to a queue drained by an LVGL timer or the platform UI event mechanism. The listener must run on the LVGL task. Every event must carry its request ID, and the app must ignore events for a superseded page or request.

Allocated arrays in `BUS_EVT_ROUTE_VARIANTS` and `BUS_EVT_STOPS_LIST` need an explicit ownership rule. The recommended rule is: the listener owns and frees successful payload arrays after copying the data needed by the current page; error events contain no allocated payload.

### API and normalization

Keep the operator adapters separate even when they share a normalized output:

- KMB uses `bound` values `I`/`O`, KMB service types, and route-specific or stop-wide ETA endpoints.
- CTB uses its own route/stop endpoint shape and may use direction names or codes depending on the endpoint version.
- NWFB was removed from the current public combined API specification; the adapter should be retained only if a live data source is confirmed. Do not silently send NWFB requests to CTB.
- Stop records need `name_en`, `name_tc`, latitude, longitude, operator, and source ID. Do not expose `"Stop N"` as a resolved name.
- ETA parsing must accept null ETA values, remarks, operator, route, direction, and the provider timestamp. Empty ETA data is a valid “No info available” result.

The official public specification describes KMB's `/stop-eta/{stop_id}` response as all routes at a stop and its `/eta/{stop_id}/{route}/{service_type}` response as route-specific; the implementation must filter by route, direction, and service type before displaying a favorite. See the [KMB API specification](https://data.etabus.gov.hk/datagovhk/kmb_eta_api_specification.pdf) and the [combined bus ETA data dictionary](https://static.data.gov.hk/ogcio/datagovhk/opendata/eta/bus-route-list-and-eta-specific-stop-api-data-dictionary.pdf).

## Incremental implementation order

### Step 0 — Freeze the contracts and create a deterministic baseline

**Goal:** Make the next changes measurable without changing the complete UI.

- Record the current build and device dimensions.
- Replace implicit numeric operator values and raw direction chars at call sites with named enums/helpers.
- Define event ownership, request cancellation, request IDs, error status handling, and the normalized route/stop/favorite structures.
- Add a small service test fixture containing one KMB route, one CTB route, one co-operated route, two directions, and an empty ETA response.
- Keep the existing seven-entry index only as a test fixture; label it as such.

**Exit check:** The project builds, the fixture can exercise each normalized structure, and no UI code is called from the worker task.

### Step 1 — Make the service transport safe and observable

**Goal:** Establish a reliable asynchronous request path before adding pages.

- Move event delivery from `post_event()` into a UI-task drain queue.
- Implement consistent success/error events for route, stops, stop detail, and ETA requests.
- Handle unknown/chunked HTTP content lengths and bounded allocations.
- Honor cancellation for the in-flight request and discard late results by request ID.
- Add request de-duplication or a queue capacity that covers the eight-favorite refresh case.

**Exit check:** A fixture or controlled API response produces callbacks on the LVGL task, reports HTTP/parse errors, and never updates a destroyed app.

### Step 2 — Implement the route catalog and freshness policy

**Goal:** Make Search usable and prevent large data downloads on every app open.

- Generate the full route index from the selected data source, including KMB and CTB operator bits and any supported replacement for NWFB.
- Fix `bus_route_next_mask()` so a candidate character is allowed only when the complete prefix matches.
- Add active-language metadata and a cache format. The repository has SPIFFS mounted as `/spiffs`; either implement the cache there or deliberately add/configure LittleFS before coding against the guide's LittleFS paths.
- Store cache generation, language, provider version/hash, and last successful update time.
- On app open, check for a valid clock, a future/invalid stored time, and age greater than seven days. Run this check at most once per day and whenever the app opens.
- Fetch into a temporary file and atomically replace the active cache only after validation.
- Emit progress/error state so Search can show “waiting for route data” while the first cache is being built.

**Exit check:** Search can load the active language's complete route list from cache, survives a failed refresh with the old cache, and can switch EN/TC by replacing the active language dataset.

### Step 3 — Finish the Search tab

**Goal:** Implement the first half of the discovery flow using cached routes only.

- Add reset and backspace behavior exactly as specified.
- Store keypad button pointers and apply the route mask to digits and letters after every edit. Keep invalid keys visible but dimmed, as the UI design requests.
- Show matching route numbers in ascending order; show no route rows for an empty input.
- Show both directions and operator labels for a complete route. Keep the Enter action disabled/dimmed until a complete route is entered.
- Add a loading overlay while route cache initialization/update is incomplete.
- Carry the selected route query as a request generation so old results cannot replace new results.

**Exit check:** The keypad and result list work with the fixture and with the full generated index, including reset, backspace, impossible-character dimming, and both directions.

### Step 4 — Add route variants and the stop picker

**Goal:** Let a user choose a direction and a stop.

- Implement route discovery for every supported operator and merge variants by normalized route/bound/service type.
- Build the direction chooser. If only one variant exists, select it automatically; otherwise show each operator and origin/destination.
- Implement route-stop requests for the selected variant and return sequence plus source stop IDs.
- Implement stop-detail resolution with active language. Use a bounded in-memory cache and lazy loading as rows enter the viewport, or a validated route-scoped cache.
- Build the scrollable stop picker with placeholder rows, loading/error states, and a back button.
- Retain the selected variant and stop sequence in page state; never infer them from a label.

**Exit check:** Search → route variant → stop picker works for KMB, CTB, and a co-operated fixture route in both languages.

### Step 5 — Implement ETA normalization and the ETA board

**Goal:** Show correct live predictions for a selected stop.

- Add direction to ETA requests or filter every provider response by normalized route, operator, direction, and service type.
- Issue one request per operator in a co-operated favorite/selection. Associate each response with a composite identity, not only `stop_id`.
- Normalize timestamps to minutes and absolute time when the system clock is valid. Preserve provider remarks and support null/no-ETA records.
- Merge all operator results, sort by arrival time, deduplicate equivalent predictions, and keep the next three.
- Build the ETA board with back, manual refresh, freshness age, last-good-data retention, and the required EN/TC no-info strings.
- Use a visible-page 30-second timer (the current UI target; verify the observed reference behavior on the device); do not enqueue another refresh while one is already pending.

**Exit check:** A selected stop displays the correct direction, three merged ETAs or the no-info state, and remains useful after a timeout or app pause/resume.

### Step 6 — Implement favorites and persistence

**Goal:** Complete the main landing-page use case.

- Add a save/unsave toggle on the ETA board using the composite favorite identity.
- Enforce the eight-entry limit with an actionable message.
- Append newly saved entries at the bottom and preserve that order when reloading.
- Persist versioned favorite records atomically. Include operator-specific stop IDs for co-operated routes and the active display name/language or a resolvable name key.
- Load cached ETAs immediately, then refresh in the background.
- Rebuild or update the affected card after each response; display up to three merged ETAs and the last-updated age.
- Add an explicit edit mode with a trash action and confirm/remove behavior.
- Fix the current event user-data collision by storing both the app pointer and favorite index in a stable context object.

**Exit check:** A user can search, choose a stop, save it, see it on Favorites after reopening the app, refresh it, and remove it without corrupting neighboring entries.

### Step 7 — Navigation, error states, and lifecycle hardening

**Goal:** Make the flow safe on the device.

- Implement a page stack: Favorites/Search tabs, direction chooser, stop picker, and ETA board. Hide tabs on drill-down pages as specified.
- On tab changes, release page-specific request state and buffers before loading the selected tab's data.
- Make Back return to the previous page first, clear search input only on the Search page, and exit only when the app is at its root state.
- Add empty, loading, timeout, offline, invalid-cache, and route-changed states without blanking last-good ETAs.
- Release page-specific arrays, cache buffers, and pointers on page replacement, pause, and destroy.
- Validate first render and visual-area dimensions on the target display.

**Exit check:** Repeated tab switching, drill-down/back, pause/resume, and destroy/reopen cycles do not leak objects, deliver stale results to the wrong page, or crash LVGL.

### Step 8 — Defer Nearby until location input is real

Nearby requires a reliable user location source, coordinate caching, permission/error states, and a cost limit for resolving many stops. Leave it out of the first integration slice. Reuse the normalized stop coordinates and cache interfaces when it is started later.

## Per-step verification record

For each step, record:

```text
Step:
Changed files:
Service contract changed:
Manual device check:
Build/check command:
Observed memory/network behavior:
Known follow-up:
```

Do not start a later step while an earlier exit check is failing. This keeps each function addition reviewable and prevents another all-at-once integration.

## Reference notes

The reference project uses a versioned route database, stores metadata such as schema/hash/update time, and renews the database on a seven-day policy when automatic renewal is enabled. See its [`db.ts`](https://raw.githubusercontent.com/hkbus/hk-independent-bus-eta/master/src/db.ts). The reference repository describes itself as an ad-free Hong Kong ETA application with data from DATA.GOV.HK and HK Bus Crawling: [hkbus/hk-independent-bus-eta](https://github.com/hkbus/hk-independent-bus-eta).
