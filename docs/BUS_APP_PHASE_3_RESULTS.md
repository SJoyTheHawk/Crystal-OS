# Bus App Phase 3 Results

**Package:** Bus Lite 0.2.23

**Board:** ESP32-P4

**Acceptance date:** 2026-11-18

Phase 3 persistence is closed for the current Bus Lite implementation. The
P4 board checklist passed the saved-stop, recent-route, joint-route and limit
workflows. The persistence implementation first landed in 0.2.18 and is
included in the current 0.2.23 package.

## Implemented contract

- Favorites and recent routes are validated when loaded. Malformed, duplicate
  and over-limit records are ignored without making the app unusable.
- Mutations build a candidate list and publish it only after `state.set()`
  succeeds. A failed write leaves the displayed list and action unchanged.
- Favorites are capped at eight entries and recent routes at five entries.
- Saved records retain route, provider, direction, service type, stop ID and
  stop occurrence context, including both provider identities for joint routes.
- ETA minutes remain transient display data. Reopening a saved stop requests
  fresh arrivals rather than trusting a stored countdown.

## P4 acceptance

The user reported all seven checks as passed:

| Check | Result |
| --- | --- |
| Save a favorite and reopen it | PASS |
| Relaunch persistence | PASS |
| Reboot persistence | PASS |
| Remove persistence, including the final item | PASS |
| Recent routes, identity, ordering, deduplication and five-item cap | PASS |
| Joint-route favorite reopening with both provider arrivals | PASS |
| Eight-favorite limit and ninth-save rejection | PASS |

## Host validation

The existing production-binding and package harnesses pass for the current
source. They cover malformed state, duplicate and limit handling, simulated
write failures, package/catalog constraints, callback stability, navigation,
late callbacks, allocator failure and teardown. The current signed package was
rebuilt as `/tmp/phase17-bus-live.capp` from the workspace source.

Hardware storage-write failure injection was not part of the board run. The
next planned work is Phase 4 ETA refresh and live countdown; the ten-cycle P4
stability and broader resource acceptance checks remain platform-level work.

## 0.2.23 UI follow-up

The user confirmed improved tab switching on P4 after expanding each tab body
to a full half-width touch target. Production LVGL host checks cover every
pixel of the tab body at 480 px and 720 px, including icons and labels.
This adjustment uses the existing ABI; a shared ABI convention is deferred
for discussion. Phase 4 is not closed by these checks.
