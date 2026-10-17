# Phase 13.6 — Clear data confirmation validation

Date: 2026-10-17
Status: complete — implementation, automated validation, and user device acceptance.

## Implemented behavior

Settings > Manage Apps > Clear opens a modal titled **Clear App Data**, naming
the selected app and warning **This cannot be undone.** Cancel dismisses it;
Clear Data closes the app and resets its saved state immediately. Back cancels
the dialog first. Page rebuild/deletion also releases the dialog and target.

The shell stops a running app before clearing its constructor-name namespace
through CrystalApp::clearData() and state().clear(). Newly enabled apps without
an installed instance use a temporary factory instance. Clock resets retained
timer/stopwatch/lap state. Weather ignores late cache updates until reopening.
State Test initializes its counter before loading persisted state.

Clear preserves registry enable/slot settings and unrelated namespaces. It
does not create a restart requirement; prior install/reorder changes still
use the existing Restart now / Later prompt. Storage errors are reported as
potentially partial erasure and allow retry.

## Automated evidence

- Production LVGL UI test: PASS, including confirmation target, cancel/Back,
  success/failure/retry, duplicate-dialog guard, layout, teardown, and existing
  drag/toggle regressions. Command: python3 tools/test_manage_apps_page.py
  --output-dir /tmp/phase136-ui (with CommandLineTools DEVELOPER_DIR).
- Production storage/lifecycle test: PASS. Command: python3 tools/test_crystal_state.py.
  Checks stopped-app guard, reset hook, exact namespace, partial failure, and retry.
- Registry regression test: PASS. Command: python3 tools/test_crystal_registry.py.
- ESP-IDF 6.1 ./scripts/build_dev.sh: PASS. Signed build/crystal_os.bin is
  0x2c1000 bytes, with 45% of the smallest app partition free.
- git diff --check: PASS.
- Dialog render reviewed at /tmp/phase136-ui/clear-confirmation.png (480 × 480).

## Device acceptance

The user flashed the firmware and reported that all five supplied test groups
worked as intended, including the optional edge case:

1. Cancel preserves the saved Calculator formula; dialog title, app name,
   warning, and buttons display correctly.
2. Confirm clears the formula immediately; the old formula remains absent
   after reboot.
3. Clock timer/stopwatch/laps reset; Weather clearing removes the old cache
   when checked offline. System time/date/timezone remain unchanged.
4. Repeated dialog use and navigation work without freezes or duplicate
   dialogs; background actions are blocked, unrelated state/order are
   preserved, and clearing alone creates no restart prompt.
5. An app enabled since boot can be cleared before restart and opens with
   default saved state after restart.

Acceptance is based on the user's physical-device report, not an agent-operated
touch test. No serial transcript was supplied; clearing/success log statements
were verified in the implementation. The agent's flash attempt was blocked by
serial-access approval, after which the user flashed and completed testing.

Phase 13.6 is closed. Phase 13.7 remains pending. This document records
acceptance; it does not imply that a Git commit has been created.
