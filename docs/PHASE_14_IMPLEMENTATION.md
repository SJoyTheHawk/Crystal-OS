# Phase 14 — package foundation

**Status: Closed on 2026-11-11 at the user's request.** Firmware, host validation, and the Calculator board test pass. The watchdog investigation and deferred hardware checks are carried forward as follow-up work below.

This implements Phase 14 of [the v2 plan](APP_PLATFORM_V2_IMPLEMENTATION_PLAN.md): packaging, LittleFS, reconciliation, resource ownership, admission control, isolated state, and the Calculator proof package. Phase 15 supplies the USB/Wi-Fi installer, pairing, and production Ed25519 trust store. Declarative and Lua execution remain Phases 16 and 17.

The implementation, design, and code checklists are marked complete for the
Phase 14 foundation, with the adaptations documented here. Closure accepts the
Calculator board evidence and host resource-leak refusal tests. The full Bus
network workload, manual touch/timing checks, and power-interruption tests
remain deferred; they have not been reported as passed. The watchdog timeout
will be addressed separately, as requested. Phase 15 has not started.

## Implementation

- `components/crystal_package/` owns manifests, bounded `.capp` streaming, CRC validation, staging, registry metadata, package state, and resource ledgers.
- `components/crystal_registry/` combines builtin and package entries, keeps enabled/slot settings, creates apps through the curated factory, and stops/verifies/detaches apps before uninstalling their files and state. Removed catalog slots are reused so repeated installs do not grow metadata indefinitely.
- `CrystalApp` binds package state and resources to each instance, cleans a failed `onCreate`, records launch results, and blocks reuse/clear-data while registered resources remain. Three consecutive package launch failures disable the package.
- The native Calculator package uses the production Calculator UI and icon. Its root tree is tracked and explicitly deleted on destroy. The firmware's bundled Calculator and its existing saved-state namespace are retained.
- Manage Apps uses the existing Settings rows, colors, typography, and confirmation card. Buttons have 44 px touch targets. Builtins have Enable/Disable and Clear; packages have Enable/Disable and a confirmed Uninstall. At least one enabled app must remain.
- The extracted Bus app registers its retained worker and queue and its LVGL timers. Closing it exposes the worker/queue that its singleton service does not destroy; the registry disables it. The original reference checkout remains untouched.

## Concrete format and state decisions

The guide contains illustrative code that cannot be used literally on this device. These are the implementation's explicit resolutions:

1. All multibyte `.capp` integers are big-endian. A zero `u16 path_len` ends the entry stream, before the fixed 66-byte trailer. The end marker is part of the signed bytes. Entry paths must be unique, relative, and bounded; `app.json`, `data/`, symlinks, traversal, and special files cannot be supplied as payload entries. The manifest is at most 8 KiB and the package at most 128 files. JSON depth/node counts are also bounded.
2. Trailer algorithm 0 means an unsigned development package and requires the caller to explicitly supply Developer Mode. Its remaining 65 bytes must be zero. Algorithm 1 carries an Ed25519 signature. The host can sign it now; the firmware rejects it unless a verification callback is supplied. Production key management and the verifier are Phase 15. `inspect` checks structure and CRCs and explicitly reports that signatures are not verified.
3. NVS keys have a 15-character limit, so neither a full reverse-DNS registry key nor `<app_id>:key` fits. Package metadata is one committed `pkg.registry` blob, capped at 32 apps. Package state stores the full owner ID and logical `<app_id>:key` entries inside one bounded blob per app. Its physical key is a 56-bit hash; the full owner ID is checked before reading, writing, or clearing, so a collision fails closed. Uninstall erases the entire state blob. Builtin state keeps the existing v1 key format.
4. The Phase 14 package state adapter has a 4 KiB serialized envelope limit and a 1,536-byte per-value limit, additionally constrained by the declared storage quota. Lua's JSON file adapter and general filesystem write APIs belong to later runtime work. Values in the envelope are hex-encoded binary values for the existing `CrystalState` C++ interface.
5. Installation supports a fresh app ID; replacing an existing ID requires uninstalling it first. A staging directory is pruned at boot. A directory committed before its NVS row is recovered by reconciliation. Missing directories remove their metadata and state; damaged manifests retain a disabled recovery record and state instead of being mistaken for a deletion.
6. Native packages resolve only to compiled, curated factories. Phase 14 recognizes `org.crystal.calculator`, runtime `native`, entry `calculator.native`. The opt-in device test also recognizes its separate `org.crystal.phase14.calculator` fixture. It does not load arbitrary machine code. Other runtime packages can be stored, but the registry disables them when no factory is available.
7. Resources use per-instance ledgers with globally unique, typed `u32` handles, a memory ceiling for the owned allocator, and a worker quota. Each ledger has 128 slots. Services must register task/timer/queue/allocation ownership and release it only after destruction or quiescence. Native raw allocations and unregistered OS handles cannot be discovered automatically; this is a curated native API contract, not an MMU sandbox. `crystal_app_verify_cleanup(resources, &report)` returns a Boolean and reports every registered leak category.
8. Cleanup precedes file/state deletion. A surviving resource disables the app, blocks relaunch and clear-data, and retains the files, state, and installed owner. No running task is forcibly deleted to make a cleanup report pass.

## Build and use

The partition table now has two 4 MiB OTA slots, 2 MiB SPIFFS for OS assets, and 5.5 MiB LittleFS for apps. The generated `apps.bin` is an empty factory filesystem; its source is `apps_factory/`. A blank partition may be formatted after confirming the whole partition is erased. A failed mount on nonblank data never triggers automatic formatting. Package storage failure leaves builtin apps available.

Changing from the old layout requires a serial reflash, with an explicit decision about preserving or resetting existing data. Do not deliver this partition change as an ordinary OTA update. Flashing the factory `apps.bin` replaces the app filesystem. No device was reflashed for this change.

```sh
# The example app.yaml uses JSON syntax, which is also valid YAML, so this
# example needs only Python's standard library.
./tools/crystal pack examples/calculator_package --unsigned -o /tmp/calculator.capp
./tools/crystal inspect /tmp/calculator.capp

# General YAML input and optional Ed25519 signing:
python3 -m pip install -r tools/requirements-pack.txt
./tools/crystal pack ./my-app --key /path/to/ed25519.pem --key-id 1

# Normal ESP-IDF setup, then an isolated build:
idf.py -B build-phase14 -D SDKCONFIG=build-phase14/sdkconfig build
```

Phase 14's installation entry point is a C++ management API. The opt-in test below embeds a fixture and calls that API; general USB/Wi-Fi installation arrives in Phase 15. A separate developer harness can also consume a local `.capp` file after storage initialization. Hold the display lock and run management operations serially; stop/rebuild any Settings view referencing removed rows before reusing those catalog slots.

```cpp
std::string error;
FILE *package = fopen("/path/to/calculator.capp", "rb");
if (package != nullptr) {
    // Explicitly authorized development harness only. The production default
    // must be false until Developer Mode and signature policy are wired in.
    const bool developer_mode = true;
    bool installed = crystal_package_install(package, developer_mode, &error);
    fclose(package);
    if (installed) crystal_registry_apply_changes();
}
// The registry, rather than the low-level store, owns live app removal:
// crystal_registry_uninstall_package("org.crystal.calculator");
```

## Run the Calculator test on the board

For a board already flashed with the Phase 14 partition layout, open an ESP-IDF
terminal in the repository root and run:

```sh
idf.py -DCRYSTAL_PHASE14_DEVICE_TEST=ON build app-flash monitor
```

Use the same build directory you normally use; if that is `build-phase14`, add
`-B build-phase14 -D SDKCONFIG=build-phase14/sdkconfig` before `build`. Add
`-p /dev/cu.YOUR_PORT` if automatic port detection cannot select your board.
`app-flash` updates the application while preserving the installed LittleFS
filesystem. A full `flash` also writes the empty factory `apps.bin`.

The build generates and embeds an unsigned fixture named **Calculator Test**,
with the isolated ID `org.crystal.phase14.calculator`. Normal builds do not
enable its factory or the test runner. The test refuses to replace an existing
installation of that ID. It closes the startup card, records the bundled
Calculator's saved formula, then runs automatically in LVGL timer steps. Leave
the panel untouched for about one to two minutes; one automatic reboot is
expected. Keep the monitor running across that reboot.

Look for these serial messages (timestamps and logging prefixes omitted):

```text
LittleFS mounted at /littlefs; package storage ready (... packages)
Starting Phase 14 device test (run 1). ...
PASS: cycle 1/10 install/launch/save/reopen/uninstall; zero tracked leaks
...
PASS: cycle 10/10 install/launch/save/reopen/uninstall; zero tracked leaks
PASS: formula saved. Rebooting once to verify persistence.
Resuming Phase 14 device test after reboot
PASS: formula and result restored after reboot
PASS: Phase 14 device test complete (run 1). ...
```

Each cycle enters `128x3+64` through the production Calculator input callback,
checks `= 448`, closes and reopens it, verifies saved state and the resource
ledger, and uninstalls it. After the reboot check, a fresh install verifies that
the previous package state was erased. The final pass removes the fixture and
checks that the bundled Calculator's saved formula is unchanged. Memory lines
report internal DRAM, its largest free block, and PSRAM; compare successive
cycles for unexplained downward trends. Zero tracked leaks does not prove that
unregistered framework allocations cannot leak.

The NVS key `phase14.test` records progress. A completed run does not repeat at
every boot. To repeat it deliberately, increase the run number:

```sh
idf.py -DCRYSTAL_PHASE14_DEVICE_TEST=ON -DCRYSTAL_PHASE14_DEVICE_TEST_RUN=2 build app-flash monitor
```

On `FAIL`, keep the log. The runner stops and retains any installed fixture for
inspection. If a new run reports that **Calculator Test** already exists,
uninstall it through Settings → Manage Apps before using a new run number.
An interrupted run also stops instead of repeatedly rebooting.

Exit the monitor with **Ctrl+]**, then return to the normal firmware:

```sh
idf.py -DCRYSTAL_PHASE14_DEVICE_TEST=OFF build app-flash monitor
```

The option is cached in the build directory, so turn it off explicitly after
testing. This test covers the Calculator path. The Bus service, power-loss
recovery, and manual touch checks below are carried forward as hardware follow-ups.

## Validation evidence

The following commands passed on the host:

```sh
python3 tools/test_app_platform_v2.py
python3 tools/test_crystal_registry.py
python3 tools/test_crystal_state.py
python3 tools/test_manage_apps_page.py --output-dir .cache/phase14-manage-apps
```

The Phase 14 integration target links the production package store, registry, Calculator, Brookesia manager, and LVGL, with AddressSanitizer and UndefinedBehaviorSanitizer. It performs 10 complete package install/uninstall cycles and 10 launches per cycle (100 launches), checks zero tracked resources after every close, verifies bounded catalog reuse, and preserves bundled Calculator state. Global LVGL/framework allocations are excluded from LeakSanitizer; the explicit per-app ledger is the cleanup gate.

The same command also compiles the actual device test runner against the host
framework. It checks all 10 cycles and UI results, reconstructs the display,
phone and registry while retaining files and mock NVS to simulate reboot,
checks fresh state and the completed-run guard, and verifies refusal to replace
an existing fixture or continue after corrupt reboot state. This simulation
does not validate the board's flash or physical reset behavior.

Other checks cover malformed/truncated packages, CRC failures, traversal, duplicate/reserved paths, missing entry files, excessive JSON depth, ABI/network manifest errors, unsigned and untrusted signature rejection, memory/storage/worker admission, aggregate memory reservations, registry write rollback, enable/slot persistence, three-strike disable, cross-app state isolation, state size limits, damaged registry/manifest recovery, stale/wrong-owner handles, retained worker/queue removal refusal, and hand-deleted app reconciliation. Manage Apps checks use real LVGL pointer input for drag/reorder, plus confirmation, last-app protection, removal failure/retry, and layout checks.

Both normal and opt-in device-test ESP32-S3 firmware builds passed, including both filesystem images. The signed application fits the new 4 MiB OTA slot. The physical Calculator board run also passed as recorded below.

Additional checks passed for uninstalling a running package, undoing Disable while its UI is still alive, rejecting re-enable while a retained worker remains, and cleaning a failed `onCreate` exactly once. The host packer's Ed25519 output was independently verified with the generated public key. Production app screenshots were rendered with `tools/capture_readme.py`, and the package row and uninstall dialog were visually checked against the existing system elements.

## Calculator board evidence

The user supplied a serial log on 2026-11-11 from firmware `0.12.14`, ELF SHA256
prefix `70cd0553e`, ESP-IDF v6.1, device-test run 1. It records:

```text
LittleFS mounted at /littlefs; package storage ready (0 packages)
PASS: cycle 10/10 install/launch/save/reopen/uninstall; zero tracked leaks
PASS: formula saved. Rebooting once to verify persistence.
LittleFS mounted at /littlefs; package storage ready (1 packages)
PASS: formula and result restored after reboot
PASS: Phase 14 device test complete (run 1). 10 cycles, reboot persistence, fresh state and cleanup verified.
```

NVS reports encryption enabled on both boots. This is evidence for mounting,
package discovery after a software reboot, saved-formula restoration, isolated
builtin state, uninstall, and fresh state on reinstall on the actual device.
Cycles 3–10 report identical free PSRAM of 4,806,672 bytes; free internal DRAM
ranges from 35,387 to 35,651 bytes, with a largest free block of 7,680 bytes.
Those samples show no progressive loss across the settled cycles, within the
limits of this run and the explicit resource ledger.

The run also contains an `IDLE0` task-watchdog timeout at 12,275 ms while CPU 0
was executing `crystal_service`; the decoded stack is in mbedTLS ECDSA/X.509
certificate verification. It continued without a watchdog reset, completed
HTTPS, and passed the package test, including its planned software reboot.
Investigate service scheduling during TLS before treating this as a clean
system stress run. The log does not establish the root cause or prove that
every native allocation is tracked.

## Cleanup of earlier development diagnostics

The post-closure cleanup removes verbose NVS old/new value dumps and
the detailed boot-time OTA partition investigation dump. Storage writes still
check `nvs_set_*` and `nvs_commit` failures, use the same persisted keys/types,
and log errors. Small blob writes retain their readback verification and failure
return; the Wi-Fi enabled flag retains its readback error reporting. Review
identified these checks as functional reliability behavior, so their initial
removal was corrected. Larger blobs retain the existing commit-based behavior.
OTA pending-image confirmation and rollback behavior remain.

The unregistered `hello_app`, `state_test_app`, and `perf_spike` components were
removed; Dev Tester and the host regression suites provide the retained
development tools. Manage Apps now requires the real registry live-apply
implementation; the standalone host page test supplies its own explicit stub
instead of putting a weak success fallback in production code.

The Phase 14 device-test build option remains available for deliberate board
regression runs. Device/host platform guards, TLS configuration checks, app
state compatibility, and resource/admission checks are still required.
The automatic HTTPS smoke request is retained: commit `a7f0453` deliberately
kept it when removing HTTP acceptance-test code, and the no-bus baseline report
still uses it to exercise `crystal_http` on the board. Review reversed its
initial removal because it is not established as obsolete, especially while
the TLS watchdog investigation is open.

### Removal review evidence

- `perf_spike`: the v1 plan calls it a throwaway Phase 1 spike; no current caller
  or launcher registration remains.
- `hello_app` and `state_test_app`: the v1 Phase 10 plan explicitly records their
  replacement by Dev Tester; current state/registry regression suites remain.
- Manage Apps weak live-apply fallback: a host-linking convenience from
  `0c48ba8`, replaced by an explicit test stub. Firmware links the actual registry.
- OTA partition dump: introduced for rollback investigation in `895664c`; its
  queries only fed logs. Pending-image confirmation and rollback handling remain.
- NVS old/new dumps: diagnostic formatting removed. Readback checks introduced
  in the settings reliability change `131610c` are retained because small-blob
  verification affects the write result. No stored-key migration was removed.

No obsolete build-time feature flag was identified in this removal set. Existing
platform, TLS configuration, and Phase 14 test guards still serve current paths.

## Follow-ups after Phase 14 closure

The user requested Phase 14 closure after the successful Calculator board run
and will address the watchdog separately. These items remain open for follow-up:

- [ ] Investigate the `crystal_service`/TLS watchdog timeout and repeat the board run to confirm it no longer starves `IDLE0`. The startup HTTPS baseline check remains available.
- [ ] Check manual touch responsiveness and lifecycle timing during package operations; the automatic Calculator run does not establish these.
- [ ] Exercise the instrumented Bus service with actual HTTP/catalog work and verify its retained worker/queue disables it and blocks removal. The host test reproduces those ownership records; it does not run the full Bus network stack.
- [ ] Interrupt power during staging, directory commit, and uninstall, then confirm recovery without unrelated state loss.

These follow-ups are retained with Phase 14 closed. The Calculator board pass
above does not establish full-system validation or measured frame-rate claims.
