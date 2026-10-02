# Tracked Slice 5 Code Guide — Stop Picker and Selection

**Status (2026-10-02):** Planned after revised Slice 4 (4R) prepared catalogs.

**Plan:** [Slice 5 implementation plan](crystal-http-phase5-slice5-stop-picker-plan.md)

## Rules

- Keep `route + op + bound + service_type + stop_id` as the identity.
- Use sequence only for ordering and fallback text.
- Copy catalog values before passing them to LVGL.
- Never issue or indirectly schedule stop-detail HTTPS requests from browsing.
- Unknown IDs remain local misses; catalog refresh has an independent lifecycle.
- Reject stale page-generation and route-identity results.

## S5.0 — Compose rows

When route-stop data arrives, compose copied rows and local catalog lookups in
the service worker, then transfer bounded app-owned PSRAM data for LVGL
rendering. Do not scan catalog files or block on disk reads in an LVGL callback. Use the
active language, then the other available language, then `Stop <sequence>`.
Use the copied directional terminal metadata for destination presentation only;
keep the selected route bound and provider as the identity. Enable directional
terminal enrichment only after its evidence gate; otherwise preserve the
existing destination fallback.

## S5.1 — UI states

Center `Loading bus stops...`, `Updating stop names...`, `Waiting for network`,
and terminal error messages in the list area. Use per-provider state so a
KMB update failure does not hide valid CTB names. Missing names do not imply
that the ordered route-stop request failed; keep usable rows visible. A stale catalog record may be
shown with a bounded stale indicator; it must never change the row identity.

## S5.2 — Selection

On row activation, copy the complete selected-stop record and log:

```text
Stop selected route=<...> op=<...> bound=<I|O> service_type=<...> stop_id=<...> seq=<...>
```

Return this copied state through the service boundary for the next ETA slice.
Do not start an ETA request here.

## S5.3 — Catalog replacement

On a provider generation change, recompose matching rows by provider-qualified
stop ID, route identity, and page generation. Apply a copied batch on LVGL;
discard updates for replaced pages. A stale catalog can still supply names.
Do not start catalog synchronization just because a row is unresolved.

## S5.4 — Validation

Test local lookup hits, unresolved IDs, stale catalog records, provider ID
collisions, direction replacement, terminal-source evidence, cancellation,
Wi-Fi loss, reboot, and repeated selection. Confirm that no stop-detail
request is caused by opening, scrolling, or replacing the picker. Independent
manifest/artifact refresh may run concurrently, but foreground route requests
must not wait behind a catalog batch. Test provider failure isolation and
unknown IDs in a newer official route against an older prepared snapshot.
