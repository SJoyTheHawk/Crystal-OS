# Phase 13.5 — Drag-to-reorder validation

Date: 2026-10-16

Phase 13.5 is implemented in Settings > Manage Apps. Pressing a bars handle
lifts the complete app row and leaves a same-sized placeholder in the list. As
the row crosses another row's center, the placeholder moves and the intervening
rows shift up or down. Releasing writes the resulting row order to the saved
slot values; the launcher reads that order after restart.

Writes are defensive: equal slots are a no-op; a failed write attempts to
restore every original slot and reports whether the previous order was restored.
Disabled catalog entries can be reordered without constructing their apps. No
app state or enabled flags are changed by dragging.

The original guide snippet uses installed-only indices and an obsolete page
container. This repository uses catalog entry pointers attached to rows and the
existing shell page stack. Manage Apps remains directly under Settings.

Validation:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools python3 tools/test_manage_apps_page.py --output-dir /tmp/whole-row-ui`: PASS. This uses real LVGL pointer samples and checks placeholder movement, release handling, persistence writes, layout, toggles, scrolling, and teardown.
- `./scripts/build_dev.sh`: PASS, signed development image.
- `git diff --check`: PASS.
- Device acceptance (2026-10-17): after flashing, physical full-row drag/release
  works and the resulting order survives reboot. A follow-up restart prompt
  now warns when leaving Manage Apps with saved-but-unapplied app changes.

Phase 13.6 clear-data confirmation is next. Changes are uncommitted.
