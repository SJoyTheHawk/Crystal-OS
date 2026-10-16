# Phase 13.3 — Manage Apps page validation

Date: 2026-10-15

## Implementation

Settings now contains a top-level Manage Apps row, immediately before System. The page
uses the existing shell stack, header/Back button, viewport and Home teardown.
It lists every firmware catalog entry in saved slot order, using table order
for ties and each entry's real defaults when storage has no override. Disabled
entries remain visible without constructing their apps.

Rows contain a bars handle, registry ID, Install/Installed preview, and Clear
for enabled entries. Actions are disabled and the page explains that controls
are coming later. Install/uninstall, dragging, clear confirmation and live apply
are not included in Phase 13.3.

The original guide's installed queries omit disabled entries. The registry now
exposes catalog_count/catalog_entry; it copies the validated entry descriptors,
while referenced ID strings must outlive the catalog. Installed queries retain
their existing meaning. Invalid tables are rejected before publishing a new
catalog. No duplicate hardcoded catalog exists in the UI.

The implementation is in components/crystal_shell/src/manage_apps_page.{h,cpp},
with navigation in crystal_shell.cpp and the source registered in the shell's
CMakeLists.txt. The illustrative main/ui/settings paths do not exist here.

## Host validation

- python3 tools/test_crystal_registry.py: PASS, including new catalog coverage
  for disabled/all-disabled entries, failures, defaults and bounds.
- python3 tools/test_crystal_state.py: PASS (Phase 13.2 regression).
- python3 tools/test_manage_apps_page.py --output-dir /tmp/crystal-phase13-3-ui:
  PASS using the real production page builder and checked-out LVGL 8.4.0.
- UI tests exercise all enabled, persisted disabled, default-disabled, and empty
  catalogs; saved slot ordering and ties; repeated population; non-overlapping
  row controls at 480px; disabled controls; larger-list scrolling; and teardown.
- Host renders were visually reviewed. The content comes from production code;
  the header is a fixture, not a photograph or complete shell simulator.
- git diff --check: PASS.

## Device validation

Built with the requested ESP-IDF 6.1 environment and scripts/build_dev.sh, then
flashed with idf.py -p /dev/cu.usbmodem101 flash monitor. ESP-IDF's CMake process
inspection needed expanded sandbox access. No source workaround was added.

Temporary checks ran under the existing display lock, after shell startup.
The original validation, before the navigation move, sent actual LVGL click
events through Settings > System > Manage Apps,
checked heading, all four IDs, saved/default labels, Clear visibility, disabled
controls, row geometry, and state preservation after simulated clicks. They
clicked Back, reopened the page, closed all Settings pages, and verified return
to the original app. Ten cycles passed: **360 checks, zero failures**.

The tight ten-cycle diagnostic loop produced a task-watchdog warning while
rendering under the startup display lock; it continued and passed. This is a
limitation of that stress harness, not evidence of clean timing under normal
interaction. The harness was removed before the final build. Manual touch
hit-testing, gesture feel, and extended interactive soak remain acceptance
checks; automated LVGL events do not physically press the panel.

No test changes were made to enabled, slot, or app-state keys on the board.
Normal app restoration re-saves the same shell.card value via existing code.
Disabled app rendering was tested on the host without altering device settings.

## Issue found and corrected

An intermediate styling attempt set LVGL's transition descriptor to nullptr.
It caused a host segmentation fault and a device LoadProhibited panic during
page construction. That call was removed; preview buttons now remove inherited
theme styles and set explicit disabled styling. Host tests and the 360 device
checks passed after correction. Last Crash may retain this diagnostic failure;
crash history was not manually erased. The first monitor also reported an ELF
checksum mismatch after a newer build completed while it was still monitoring;
no symbols from that mismatched ELF were used as validation evidence.

## Final firmware

Temporary main/shell hooks have been removed. The normal startup path remains.
The final signed build passed without compiler warnings. Flash hashes verified.
Normal boot installed dev_tester, clock, weather, and calculator, then returned
from app_main. First frame: **2285 ms**. Main stack headroom: **5172 bytes**.
No panic, abort, or task-watchdog warning occurred in the final monitored boot.
The final production firmware also logged Manage Apps opening with all four
catalog entries at 16092 ms, with the temporary test hooks absent. WiFi still
retried with reason 2; network/OTA validation is outside this phase.

## Logs

Temporary full logs (not committed):

- /tmp/crystal_phase13_3_registry.log
- /tmp/crystal_phase13_3_state.log
- /tmp/crystal_phase13_3_ui.log
- /tmp/crystal_phase13_3_build_final.log
- /tmp/crystal_phase13_3_device_diagnostic.log (intermediate failure)
- /tmp/crystal_phase13_3_device_retry.log (360 checks; stress warning)
- /tmp/crystal_phase13_3_device_final.log

Host previews can be regenerated with the command above. No Git commit was
created; changes remain available for review with the earlier Phase 13 work.

## Captured final boot evidence

~~~text
I (3180) crystal_registry: installed: dev_tester (slot 0)
I (3187) crystal_registry: installed: clock (slot 2)
I (3302) crystal_registry: installed: weather (slot 3)
I (3310) crystal_registry: installed: calculator (slot 4)
I (3577) crystal_boot: first-frame baseline: 2285 ms
I (3577) crystal_boot: main task stack headroom: 5172 bytes
I (3578) main_task: Returned from app_main()
~~~

## Navigation placement revision

At the user's request, Manage Apps moved from System to the main Settings
list, immediately before System. Back now returns directly to Settings.
The Phase 13 guides reflect this placement for subsequent work.

Revision validation: signed development build passed without compiler warnings;
source checks confirm exactly one Manage Apps row in settings_open(), none in
settings_push_system(), and the existing Back handler pops its parent page.
The original 360-check diagnostic was not rerun for this row relocation.
Build/device logs: /tmp/crystal_manage_apps_move_build.log and
/tmp/crystal_manage_apps_move_device.log.

The relocated-menu firmware flashed successfully and returned from app_main
without a panic, abort, or task-watchdog warning in the monitored boot.

~~~text
I (3578) crystal_boot: first-frame baseline: 2286 ms
I (3578) crystal_boot: main task stack headroom: 5156 bytes
I (3579) main_task: Returned from app_main()
~~~
