# Tracked Slice 5 Code Guide — Stop Picker and Selection

**Status (2026-10-03):** Planned after revised Slice 4 (4R) prepared catalogs.
This guide defines the implementation order, ownership boundaries, and one
fixed device matrix for the stop picker. It is intentionally separate from
catalog synchronization tests; opening a route must never be used as an
implicit catalog-refresh test.

**Plan:** [Slice 5 implementation plan](crystal-http-phase5-slice5-stop-picker-plan.md)

## Entry gate and one-pass workflow

Start Slice 5 only after the 4R handoff records: zero detail requests during
browsing, deterministic provider artifacts, a validated manifest path, and
the active-generation/lookup contract. The 4R.3 recovery gate may remain a
documented dependency, but the picker must never paper over it by fetching
individual stops.

There are two data paths:

```text
route selection → ordered route-stop mapping → copied local lookup → LVGL rows
catalog refresh → manifest/provider artifact → validated generation → row refresh
```

Run the checks in this order, once per firmware revision. P0 and P4 are
device checks. P1–P3 should be host or developer-triggered checks when the
normal UI cannot create the required catalog-generation condition; do not
manufacture them by repeatedly disconnecting Wi-Fi.

| Row | Owner/setup | Required evidence |
| --- | --- | --- |
| P0 | Device: CTB 10 inbound/outbound, then KMB 101 service types 1/2 | ordered rows; zero stop-detail requests |
| P1 | Host/developer fixture: one known KMB ID and one known CTB ID | provider-qualified ID and local name are copied into the row |
| P2 | Host/developer fixture: one route-stop ID absent from the active snapshot | `Stop <sequence>` (or documented equivalent); no HTTP is queued |
| P3 | Developer trigger: publish one provider generation while the page is visible | matching rows refresh; replaced page receives no late update |
| P4 | Device: replace the route rapidly ten times and select ten rows | no stale route/ID/name and no watchdog reset |

P0–P2 prove the picker path. P3–P4 prove generation and cancellation
boundaries. Do not repeat P0 because P3 is difficult; attach the relevant log
to its own row. If a catalog update is needed, trigger it independently and
record its 4R phase.

For P0, wait for each route-stop response before making the next selection. A
live route can return a different number of rows over time; use successful
ordered parsing and identity logs as the assertion. The four route-stop
requests are the only expected bus requests in this cycle; an HTTPS smoke
request and independent weather request may also appear.

Common evidence for every row is the route identity, bound, service type,
provider stop ID, sequence, displayed name, page generation, request count, and
any transport error. A clipped UI-only observation cannot close a row.

## Rules

- Keep `route + op + bound + service_type + stop_id` as the identity.
- Use sequence only for ordering and fallback text.
- Copy catalog values before passing them to LVGL.
- Never issue or indirectly schedule stop-detail HTTPS requests from browsing.
- Unknown IDs remain local misses; catalog refresh has an independent lifecycle.
- Reject stale page-generation and route-identity results.
- Keep route-stop loading, catalog state, and row rendering as separate state
  machines. A missing name is a row state, not a reason to retry the route or
  start a stop-detail request.

## S5.0 — Compose rows

When route-stop data arrives, compose copied rows and local catalog lookups in
the service worker, then transfer bounded app-owned PSRAM data for LVGL
rendering. Do not scan catalog files or block on disk reads in an LVGL callback. Use the
active language, then the other available language, then `Stop <sequence>`.
Use the copied directional terminal metadata for destination presentation only;
keep the selected route bound and provider as the identity. Enable directional
terminal enrichment only after its evidence gate; otherwise preserve the
existing destination fallback.

Implementation boundary:

1. The worker owns route-stop parsing and the provider-qualified lookup.
2. The worker copies bounded text, coordinates, and identity into an
   app-owned row batch.
3. The LVGL task consumes that batch and owns widget lifetime only.

The batch must carry the route identity and page generation beside every row.
Never pass a pointer into a catalog file, parser buffer, HTTP response, or
temporary string pool to LVGL.

## S5.1 — UI states

Center `Loading bus stops...`, `Updating stop names...`, `Waiting for network`,
and terminal error messages in the list area. Use per-provider state so a
KMB update failure does not hide valid CTB names. Missing names do not imply
that the ordered route-stop request failed; keep usable rows visible. A stale catalog record may be
shown with a bounded stale indicator; it must never change the row identity.

Use this state precedence so the UI does not flicker while two operations
overlap: route loading, then catalog updating, then usable/stale names, then
unresolved rows, then terminal error. `Waiting for network` applies only when
the route-stop mapping itself cannot be obtained. An unresolved local name
does not replace a successful route response with an error screen.

## S5.2 — Selection

On row activation, copy the complete selected-stop record and log:

```text
Stop selected route=<...> op=<...> bound=<I|O> service_type=<...> stop_id=<...> seq=<...>
```

Return this copied state through the service boundary for the next ETA slice.
Do not start an ETA request here.

Selection acceptance is a log contract, not a visual guess. The selected record
must be copied before the page can be replaced and must contain all identity
fields even when the displayed name is a fallback. A second tap on the same
row must produce the same provider-qualified identity until the route changes.

## S5.3 — Catalog replacement

On a provider generation change, recompose matching rows by provider-qualified
stop ID, route identity, and page generation. Apply a copied batch on LVGL;
discard updates for replaced pages. A stale catalog can still supply names.
Do not start catalog synchronization just because a row is unresolved.

Apply replacement in this order: validate the new generation, match by
provider-qualified ID, compose a new batch for the current page generation,
post that batch to LVGL, then release the old batch. If the selected route or
page generation has changed at any point, discard the late batch. Keep the old
usable name while a replacement is being validated.

## S5.4 — Validation

Test local lookup hits, unresolved IDs, stale catalog records, provider ID
collisions, direction replacement, terminal-source evidence, cancellation,
Wi-Fi loss, reboot, and repeated selection. Confirm that no stop-detail
request is caused by opening, scrolling, or replacing the picker. Independent
manifest/artifact refresh may run concurrently, but foreground route requests
must not wait behind a catalog batch. Test provider failure isolation and
unknown IDs in a newer official route against an older prepared snapshot.

## Slice 5 stop rule

Slice 5 is ready for the ETA slice when P0–P4 pass, the selection log contains
complete identity for both providers and directions, and the request count
contains route-stop traffic only. Do not add ETA requests, destination guesses,
or deprecated catalog cleanup to this slice. If a test exposes a transport,
generation, or watchdog problem, file it against the owning 4R row and keep the
picker result separate.
