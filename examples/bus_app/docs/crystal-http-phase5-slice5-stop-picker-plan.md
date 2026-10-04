# HTTPS Phase 5 — Slice 5: Stop Picker and Selection

**Status (2026-10-02):** Planned after revised Slice 4 (4R) prepared catalogs.
Original S4.4 diagnostics alone do not satisfy this dependency.

**Previous slice:** [Slice 4 — stop metadata catalog](crystal-http-phase5-slice4-stop-picker-plan.md)

**Code guide:** [Slice 5 code guide](crystal-http-phase5-slice5-stop-picker-code-guide.md)

## Decision

The stop page reads names from the local stop catalog. Opening a route fetches the official ordered route-stop mapping as needed,
then performs local name lookups. It must not schedule any individual stop-detail
request, directly or through background discovery. Route-stop data supplies order and source IDs;
the catalog supplies display metadata. Directional terminal names are consumed
only after Slice 4 validates the mapping rule. Provisional terminal enrichment
remains disabled in the UI and may be deferred.

The slice ends when a user can select a KMB or CTB stop in either direction
and the app emits a complete identity for the next transit operation.

## Work packages

### S5.0 — Local stop-list composition

Compose each route-stop row from the selected route variant, ordered route-stop
record, and a copied local metadata lookup result. Use the Slice 4 directional
terminal metadata for route and direction presentation when available. Show
sequence fallbacks for missing catalogs or IDs absent from the active snapshot.
A lookup miss does not schedule network work. Preserve the official route-stop
row even if the prepared dataset has not caught up with it. Do not derive
identity from names or terminal text.

### S5.1 — Picker states and bounded rendering

Implement loading, catalog updating, catalog stale, unresolved row, empty,
offline, failure, and cancellation states. Keep rendering on LVGL, cap rows,
and use bounded labels with ellipsis.

### S5.2 — Selection boundary

Add copied selected-stop state containing route, operator, bound, service type,
provider stop ID, sequence, names, and coordinates when available. Log the
complete identity. Do not submit ETA yet.

### S5.3 — Catalog refresh interaction

Allow a background catalog update to replace metadata after the page is shown.
Update matching rows by provider-qualified stop ID and page generation. A late
catalog result must not replace a newly selected route.

### S5.4 — Acceptance and handoff

Test both providers, both directions, shared route labels, warm and cold
catalogs, unresolved records, Wi-Fi loss, rapid route replacement, reboot,
and ten repeated selections without stale names or watchdog resets. Include
KMB unavailable with CTB usable, old/new snapshot generations, and metadata
request counts that remain independent of browsing activity.

## Exit criteria

- no per-row HTTPS requests are required to open a route;
- rows preserve route sequence and provider stop identity;
- names come from the local catalog when available;
- directional terminal text uses a validated rule or retains the existing fallback;
- unresolved and stale records have clear fallback states;
- selected-stop identity is complete and ready for the ETA slice;
- device and host checks pass.
