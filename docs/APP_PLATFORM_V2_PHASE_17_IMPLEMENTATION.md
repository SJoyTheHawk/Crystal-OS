# App Platform v2 — Phase 17 implementation

**Date:** 2026-11-15
**Status:** Runtime core implemented; device acceptance and Bus script migration remain open
**Scope:** Lua 5.4 script apps, bounded storage, asynchronous HTTP and worker quotas

Phase 17 resumes after the P4.3 network expansion pass. The implementation is
integrated into the P4 firmware and uses the existing `CrystalApp`, resource,
package, UI and HTTP services. This record separates code and host evidence
from checks that require a connected board.

## Delivered

### Script runtime

`ScriptApp` is selected by the package registry when `manifest.runtime` is
`lua`. It creates one Lua 5.4 state per app and reads only the manifest entry
file from the installed package. The entry must return a table containing the
v1 lifecycle hooks `onCreate`, `onResume`, `onPause`, `onDestroy` and optional
`onBack`; hook return value `false` rejects the lifecycle operation.

The state exposes `crystal.ui`, `crystal.service`, `crystal.state`,
`crystal.json` and `crystal.fs`. UI values are resource-table handles, never
LVGL pointers. The binding covers the v1 widget creators and operations,
properties/events, lists/charts, app-owned timers/animations, nested JSON
conversion, permission-gated system services and package-local assets/data.

### Sandbox and budgets

The runtime opens the standard Lua library set and removes process, file,
module-loading, dynamic-code and debug facilities (`os`, `io`, `package`,
`require`, `load`, `loadstring`, `loadfile`, `dofile`, `collectgarbage` and
`debug`). `print` is routed to the Crystal log. Script-created coroutine
threads receive the same instruction hook as the main state.

Each callback receives a 50 ms deadline and a count hook. A timeout or memory
failure remains fatal even if Lua catches the error with `pcall`; the runtime
reports a toast and schedules app closure on the LVGL task. Lua allocations use
a first-fit arena allocated from the app resource manager, sized by
`manifest.memory_kb`, with coalescing and no fallback to an unbounded heap.

### Storage and lifecycle cleanup

`crystal.fs` permits read-only access to package `assets/` and read/write access
to package `data/`. Relative paths reject absolute paths, traversal, backslashes
and invalid components. Writes enforce the manifest `storage_kb` quota, a 64 KiB
per-file limit and temporary-file-plus-rename replacement. `crystal.state`
stores bounded JSON in `data/state.json`.

Destroy and pause release Lua callback references, UI callbacks, timers,
images and request callbacks. HTTP requests carry an owner ID and are
cancelled by owner during app teardown. Completion data is held by shared state
rather than a raw app pointer, so a late worker completion cannot call a
destroyed Lua state.

### Async HTTP

`crystal.service.http.fetch(url, options, callback)` supports bounded GET and
POST requests. The options allow a 1–60 second timeout, up to eight headers and
a 2 KiB POST body; responses are capped at 64 KiB. URLs require `http` or
`https`, reject userinfo and malformed ports, and match an exact lower-cased
host from `network_hosts`. Redirects are disabled for this host-restricted
path, preventing a successful request from escaping the manifest allowlist.

Requests use the shared `crystal_http` worker. `manifest.worker_threads` is an
active-request quota for each script app; an excess request returns
`nil, "worker quota exceeded"`. Completion callbacks are dispatched by a
50 ms LVGL timer on the UI task. The existing HTTP API remains compatible for
older callers: omitted method data preserves GET behavior and redirect handling
is unchanged unless the new option is set. The shared HTTP worker serves
multiple apps; `worker_threads` is an app-local concurrency cap within its
existing queue, not a count of FreeRTOS tasks created per app.

## Example package

[`examples/clock3p`](../examples/clock3p) is now an executable Lua package,
not a design-only sample. It returns the hook table ABI, builds the clock,
countdown and stopwatch tabs with the handle-based UI API, uses the OS-owned
timer service, and persists stopwatch/lap state under its data quota. Its
manifest requests only `notify`, `audio` and `system_timer`; it has no network
allowlist and sets `worker_threads: 0`. The icon is a bounded CR16 image asset
accepted by the current package image loader.

The package starts its one-second refresh timer in `onCreate` because a fresh
card launch enters `onCreate` without an initial `onResume` callback. CrystalUI
suspends app-owned timers while the card is paused and resumes them on return, so
the package keeps one timer handle across card switches.

The board report of unchanged `--:--:--`/`00:00` labels exposed two additional
firmware defects. `CrystalUI::adopt` now treats a standalone label as its own
text target, allowing Lua `ui.set_text`/`ui.get_text` calls to reach it. Script
storage directory creation now preserves the leading slash in absolute package
data paths instead of failing on `mkdir("")`; stopwatch state can therefore be
written. These corrections require an application firmware flash, even when the
latest Clock Lua package is already installed.

The subsequent board clarification confirmed that the countdown Timer works;
the remaining failure is Stopwatch. The bundled Lua configuration uses 32-bit
integers and floats. Converting an integer Unix timestamp through `lua_tonumber`
and restoring it with `lua_pushnumber` loses second-level precision. JSON/state
conversion now preserves integer values in both directions. The regression in
`tools/test_phase17_bindings.py` compiles the production JSON, filesystem, state
and timer bindings with the bundled Lua configuration. It rejects the previous
timestamp conversion and checks exact timestamp round trips, stopwatch start,
pause/resume, nonzero laps and reset using controlled time and UI adapters.
It also checks the already-working countdown API for regressions. This is not
a full ScriptApp or physical-board test.

Manage Apps uses the P4 Settings proportions: 54-pixel-high action buttons
and 84-pixel rows. Inline Enable/Disable, Clear and Uninstall widths are
112, 88 and 120 pixels; confirmation buttons match Settings at 168 by 54
pixels. Its S3 dimensions remain unchanged.
`tools/test_manage_apps_page.py --target p4` and `--target s3` exercise the
production page with real LVGL and render layout previews for both sizes.

**P4 board acceptance (2026-11-15):** The captured serial log confirms a
300-second system timer started, the Clock package was destroyed and
successfully uninstalled, and the OS timer still expired with the `Timer
finished` toast. The log shows the app catalog count drop from six to five, and
the user confirmed the launcher menu was correct after removal. The speaker
initialization failed in this run, so audible alarm output remains unverified.
The board also confirmed all three Clock tabs working. The `crystal run` signed
install and launch path returned success. A subsequent signed runaway-package
test recorded the 50 ms callback limit at 28,921 ms, the App stopped toast,
onPause/onDestroy, and continued UI redraws through 58,904 ms without a reset.
This passes the serial-evidenced timeout and app-closure check; visual launcher
and touch confirmation and package cleanup remain unrecorded. The capture
reports flashed ELF prefix `6d39dc7da...` and a mismatch with the local build,
so it does not establish acceptance of that local binary. See Step 5 of the
test guide for the evidence reference. A later P4 capture (OS 0.17.0, silicon
revision v1.3) passes HTTP success, quota, allowlist, cancellation and worker
recovery: request id 1 returned 200, request id 3 was cancelled during owner
teardown with `ESP_ERR_INVALID_STATE`/`cancelled=1` and no Lua callback, and the
worker-check request then returned 200. Both test apps were uninstalled. The
full firmware image hash and physical board revision were not recorded. The
runaway test's visual launcher/touch confirmation and cleanup, plus Bus
conversion, remain open.

## Evidence

The following checks pass in the repository workspace:

```text
python3 tools/test_phase17_runtime.py
phase17 Lua sandbox, ABI, coroutine deadlines, and allocator checks passed
phase17 exact-host and redirect-boundary checks passed
phase17 production storage path checks passed
```

The host test compiles the bundled Lua 5.4 sources in a boundary harness that
mirrors the runtime's removed sandbox facilities, returned hook-table ABI,
main and protected coroutine deadline handling, caught timeout behavior and
bounded allocation. Its allocator is a host `realloc`-based limit harness; it
does not instantiate `ScriptApp` or exercise the firmware PSRAM arena. A
separate C++ harness checks exact-host matching, userinfo/port rejection and
redirect-boundary inputs. A filesystem harness compiles the production directory
helper verbatim and checks absolute/nested paths, existing directories, a
state-file round trip and rejection of file-as-parent paths. The platform suite
(`tools/test_app_platform_v2.py`) also exercises standalone label adoption and
text updates through the production facade and real LVGL under ASan/UBSan.
These checks do not replace a full ScriptApp or physical-board test.

The ESP32-P4 firmware also builds and links with the runtime, Lua dependency,
HTTP extensions and factory integration. The real Clock package packs and
inspects successfully with:

```text
python tools/crystal.py pack examples/clock3p --unsigned -o /tmp/clock3p.capp
python tools/crystal.py inspect /tmp/clock3p.capp
```

## Remaining work

The script runtime and Clock package passed the basic P4 Clock and timer-after-
uninstall checks. The remaining device checks and Bus conversion are:

- an infinite Lua loop producing app closure and a toast without a watchdog reset;
- conversion of the Bus app to the script tier and resource-cleanup verification.

The phase checklist in the implementation plan and code guide tracks these
board checks and the Bus migration separately.
Follow the [step-by-step P4 board test guide](APP_PLATFORM_V2_PHASE_17_TEST_GUIDE.md)
to run and record those checks.
