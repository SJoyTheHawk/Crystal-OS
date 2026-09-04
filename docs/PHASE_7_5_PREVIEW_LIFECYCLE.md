# Phase 7.5 — Preview lifecycle and deferred app switching

## Purpose

This document defines the lifecycle-safe card preview model for the Phase 7.5
visual crossover. A card preview is a shell-owned visual representation only; it
must never start, resume, pause, or destroy a Brookesia app during a drag.

The current app remains the only live app until the user releases the gesture and
has selected the destination by dragging at least 50% of the app area.

## Required interaction

```text
App A is active
  → App A preview is generated or refreshed from its already-live screen

Drag begins at an edge
  → show the target's stored/RAM preview
  → if no preview exists, show its icon and name identity card
  → keep App A alive and interactive state owned by the shell

Drag moves past 10%
  → no app switch
  → no target onCreate(), onResume(), or other lifecycle callback
  → no special threshold action; the card simply continues following the finger

Release below 50%
  → cancel the card transition
  → keep App A active
  → dispatch no target lifecycle callbacks

Release at or beyond 50%
  → animate the selected card to 100% app-area coverage
  → commit the selected card only after the cover animation completes
  → start the target through Brookesia
  → App A onPause()
  → App A onDestroy()
  → target onCreate()
  → target onResume()
  → remove the preview overlay after the target is ready
```

The 10% position is deliberately **not** an app-switch threshold. In this
revision, dragging over 10% by itself does nothing beyond the normal finger-
tracked preview movement. The only switch threshold is 50%, evaluated on release.

## Preview ownership

Previews belong to `crystal_shell`, not to a running Brookesia app instance. The
shell may keep a bounded RAM cache for immediate neighbours and persist previews
under stable app IDs, for example:

```text
/spiffs/crystal_preview_<stable-app-id>.bin
```

The preview format is the existing app-area RGB565 image at reduced resolution
(approximately 240×220 on the current panel). `CrystalState` remains reserved for
small app state values and is not used for image blobs.

Preview selection order during a drag:

1. in-memory neighbour preview;
2. persistent preview loaded by stable app ID;
3. shell-rendered identity card with launcher icon and app name.

A missing or unreadable preview must never produce a black or blank card.

## Preview generation

Only an app that has actually been visited may produce a real preview. The
preferred capture points are:

- after the app has rendered its first stable frame, to make the current card
  immediately cacheable; and
- `onPause()`, while its LVGL tree is still alive, to refresh the persistent
  preview immediately before Brookesia destroys the app.

Generating a preview must not instantiate a neighbour merely to populate the
cache. A never-visited app therefore correctly uses its identity card until it
has been opened and captured.

## Implementation steps

1. Add a shell preview repository keyed by stable app ID, with RAM-cache lookup,
   persistent load/save, replacement, and bounded cleanup.
2. Add preview capture after a visited app has a stable rendered screen and/or
   from the app's `onPause()` path. Capture only the app area, downscale to the
   established RGB565 preview size, and release temporary buffers promptly.
3. Refactor `begin_card_transition()` so it creates only the shell overlay,
   outgoing snapshot, and target preview/identity card. It must not call
   `start_card()` or send a Brookesia start event.
4. Keep `update_card_transition()` purely visual. Progress, including movement
   past 10%, must not affect app lifecycle or active-app selection.
5. In `on_gesture_release()`, use 50% of the app-area width as the sole commit
   test. Below 50% cancels; at or above 50% calls `start_card()` exactly once.
6. Keep the preview overlay covering app construction during the committed
   transition, then remove it only after the target is live and resumed.
7. Verify cancellation, commit, boundary swipes, touch ownership, memory
   cleanup, status-bar clipping, and tear-free rendering on hardware.

## Lifecycle invariants

- At most one Brookesia app is live (`max_running_num = 1`).
- No target lifecycle callback occurs during `Dragging`.
- `start_card()` is called only by the post-release commit path (or normal
  non-gesture card launch paths).
- A cancelled drag leaves the active app and its lifecycle state unchanged.
- A committed drag uses the normal Brookesia lifecycle sequence; the shell does
  not manually invoke app lifecycle hooks.
- The target lifecycle runs only after the incoming card has reached full app-area
  coverage; lifecycle work must not compete with the cover animation.

## Validation checklist

- [x] Drag begins with a cached preview or identity card, without target app
  construction.
- [x] Dragging through and beyond 10% does not switch apps or emit target
  lifecycle logs.
- [x] Release below 50% cancels and leaves the current app active.
- [x] Release at or beyond 50% switches exactly once through Brookesia.
- [x] Logs show App A `onPause()` → `onDestroy()` before target `onCreate()` →
  `onResume()`.
- [x] Previously visited apps show their persisted/downscaled preview.
- [x] Never-visited apps show icon/name identity fallback.
- [x] No blank/black preview, status-bar occlusion, touch leakage, or obvious
  tearing occurs during drag or settle.

### Persistence finding

SPIFFS is configured with a 32-character object-name limit. The original
temporary save name (`crystal_preview_state_test.bin.tmp`) exceeded that limit,
so State Test alone reported `result=open-failed`. Temporary files now use the
short form `/spiffs/.tmp_<stable-app-id>`, while the final preview filenames are
unchanged. State Test now saves and loads successfully across reboot.

## Status

Preview repository and lifecycle separation are implemented and validated on
hardware. Phase 7.5 is closed.
