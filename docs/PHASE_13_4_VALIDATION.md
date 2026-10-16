# Phase 13.4 — Install/uninstall toggle

Date: 2026-10-15

## Behavior

Manage Apps remains directly under Settings, immediately before System.
Install/Installed buttons are active. A successful click persists the registry
flag and refreshes that row's label and enabled-only Clear control. The page
explains that restarting applies app changes. Uninstall preserves app state.
Clear remains disabled; dragging and live application remain later phases.

The callback uses the full catalog entry's default, not a generic false default.
Rows update in place so the active event target and scroll position survive.
There is no global widget cache and no asynchronous rebuild to outlive the page.
A failed save shows an error without changing the displayed selection.

Removing the last enabled catalog entry is blocked with a visible explanation:
the current shell cannot initialize with zero installed apps. This guard covers
normal UI changes; it does not repair corrupt storage or guarantee startup when
an enabled app's factory itself fails.

## Host tests

- tools/test_manage_apps_page.py: PASS with real LVGL and production UI code.
  Covers enable/disable clicks, Clear disappearance/reappearance, saved-state
  reopening, failed save and retry, repeated clicks, default-disabled install,
  last-app guard and recovery by installing another app, scroll preservation,
  slot ordering/ties, empty catalogs, layout bounds and teardown.
- tools/test_crystal_registry.py: PASS with production registry code, including
  persisted flags, boot selection, defaults, storage errors and catalog access.
- tools/test_crystal_state.py: PASS for the unchanged clear-state primitive.
- Host renders reviewed at /tmp/crystal-phase13-4-ui. They render production
  page content with a fixture header, not a complete shell or panel photograph.

## Device procedure

A temporary helper enters Settings > Manage Apps using the production click
callback and checks Back to the Settings root. It saves the original clock
registry byte (or absence) in a temporary NVS namespace before changing it.
It toggles clock, restarts, checks the saved flag and actual installed list,
toggles back, restores the exact original byte/absence, restarts again, checks
restoration, then removes its diagnostic keys. Other apps and app data are not
modified. Normal shell navigation re-saves the current card through existing code.

The first helper run checked a label before LVGL layout had completed and
reported a failed lookup before making a toggle. It did not panic or reboot.
The helper was corrected to update layout before locating the row.

The corrected helper passed across three boots: stage 0 had 13 checks,
stage 1 had 14, and stage 2 had 10: **37 checks, zero failures**. Clock was
absent from the installed list on the disabled boot and present on the restored
boot. Exact original storage and removal of the diagnostic keys were verified.
No panic, abort, or watchdog warning occurred in the corrected run. The helper
and main hook have been removed before building the final firmware.

## Logs and limits

Temporary logs are under /tmp/crystal_phase13_4_*:
ui.log, registry.log, state.log, build_diagnostic.log, device_diagnostic.log,
device_retry.log, build_final.log and device_final.log.

Automated events do not validate physical touch hit-testing or gesture feel.
Save failures are injected on the host, not by damaging device storage.
Phase 13.5 drag, 13.6 clear confirmation, 13.7 live apply, and the full Phase 13.8
regression matrix are not implemented by this phase. No Git commit was created.

## Captured reboot evidence

~~~text
I (3192) crystal_registry: installed: clock (slot 2)
I (3706) manage_apps: Toggling clock: 1 -> 0
I (3742) phase13_4: PASS stage 0: 13 checks, 0 failures
I (3168) crystal_registry: disabled: clock
I (3692) manage_apps: Toggling clock: 0 -> 1
I (3749) phase13_4: PASS stage 1: 14 checks, 0 failures
I (3182) crystal_registry: installed: clock (slot 2)
I (3718) phase13_4: PASS stage 2: 10 checks, 0 failures
~~~

## Final firmware

Final signed development build passed without compiler warnings. Flash hashes
verified on /dev/cu.usbmodem101. All four apps installed and app_main returned
with no panic, abort, or watchdog warning in the monitored boot. Temporary
diagnostic hooks are absent. git diff --check passed; changes are uncommitted.

~~~text
I (3184) crystal_registry: installed: dev_tester (slot 0)
I (3191) crystal_registry: installed: clock (slot 2)
I (3306) crystal_registry: installed: weather (slot 3)
I (3314) crystal_registry: installed: calculator (slot 4)
I (3580) crystal_boot: first-frame baseline: 2288 ms
I (3580) crystal_boot: main task stack headroom: 5172 bytes
I (3581) main_task: Returned from app_main()
~~~
