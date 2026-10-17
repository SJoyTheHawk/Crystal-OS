# Phase 13 — Manage Apps release notes

Phase 13 adds an on-device Manage Apps page directly under Settings for the
bundled firmware apps.

## User-visible behavior

- Every bundled app appears, including apps that are currently disabled.
- **Install** enables an app immediately and keeps its saved card position.
- **Installed** disables an app for the next reboot. The app's private data is
  preserved.
- Drag the handle on a row to reorder cards. The order is saved to NVS and is
  restored after reboot.
- **Clear** opens a confirmation dialog and erases that app's persisted state.
- Leaving Manage Apps after a disable or reorder prompts that a restart is
  required. **Restart now** reboots immediately; **Later** returns to Settings.

## Scope and limitation

The catalog contains apps shipped in the firmware image. Phase 13 does not
download or install third-party packages; that work remains in the Phase 14–17
app-platform track. Disabling an app cannot remove an already-installed
Brookesia card from the running phone, so the launcher reflects that change
after reboot.

## Validation

Implementation and test evidence are tracked in
[`PHASE_13_CHECKLIST.md`](PHASE_13_CHECKLIST.md), with per-sub-phase records
from 13.1 through 13.7. Phase 13.8 is the final full-device regression and
release sign-off.
