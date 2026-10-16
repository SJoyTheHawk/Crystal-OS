# Phase 13.2 — CrystalState Clear

Validated: 2026-10-15 (Asia/Hong_Kong).

## Implementation

Added `bool CrystalState::clear()` in the existing public header and implementation.
It opens the same `crystal` namespace as the HAL, enumerates all NVS types using
ESP-IDF 6.1's `nvs_entry_find_in_handle` / `nvs_entry_next` API, collects keys
matching the full existing app prefix including its trailing dot, then erases
and commits. Iterators and handles are released on success and error paths.
The existing `nvs_flash` component dependency was sufficient.

An empty/no-match clear succeeds without a commit. Open or enumeration failures
return false before any erasure. Erase and commit failures also return false;
successful erasures may already have persisted, so the operation is not atomic.
A key already absent at erase time is treated as cleared.

App writers must be stopped before clearing. The method resets persisted app
data only, not in-memory app objects or shared services. It leaves registry
flags, shell settings, and other prefixes untouched. Prefixes continue to use
the existing 24-bit hash of the app constructor name, including case/spaces;
this phase does not eliminate hash collisions or change the storage format.
For later UI integration, use the app's `state()` instance rather than creating
state from its lowercase registry ID.

## Host validation

Run from the project root:

```bash
python3 tools/test_crystal_state.py
python3 tools/test_crystal_registry.py
```

Both passed. The new test compiles the production `crystal_app.cpp` and real
public/HAL headers with host adapters, using `-Wall -Wextra -Werror`. Coverage:

- Counter, score, and message erased; another app, registry/system keys,
  prefix lookalikes, and another namespace retained.
- Empty and repeated clear; writing and reading state again after clearing.
- Open, iterator-find, iterator-info, iterator-next, erase, and commit failures.
- No partial erasure after an enumeration error, honest partial-erase failure,
  already-missing keys, and handle/iterator cleanup.

Host adapters do not prove hardware durability. Device checks below use the
actual encrypted NVS implementation.

## Device validation

Board: ESP32-S3 revision v0.2 at `/dev/cu.usbmodem101`.

A temporary boot test after registry installation used two unused test prefixes.
It snapshotted every existing value and NVS type in the `crystal` namespace
without printing their contents. **All 27 checks passed**, including:

- Writing and reading counter=42, score=100, and message=hello.
- Clearing all three and confirming each read fails afterward.
- Retaining the other-app sentinel and all preexisting values/types.
- Empty/repeated clear and writing counter=84 after clearing.
- Erasing mixed blob and native integer keys with `NVS_TYPE_ANY`.
- Removing all test fixtures and comparing the final namespace snapshot with
  the original: exact match.

The first diagnostic helper assumed every existing key was a blob. It rejected
a native-typed key before invoking clear or writing fixtures. Its fail-fast
startup call caused diagnostic abort/reboot cycles. The helper was corrected
to snapshot native types too, and the forced abort was removed. Crystal's Last
Crash metadata may retain that test abort; no crash history was erased. This
was a test-harness failure, not a failure of `CrystalState::clear()`.

After the passing run, the temporary helper and startup call were removed.
`main/main.cpp` matches the pre-task startup path. No automatic clear runs in
the final firmware. This phase introduces no UI changes.

## Build and flash

Used the user's development signing and burn procedure:

```bash
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
./scripts/build_dev.sh
idf.py -p /dev/cu.usbmodem101 flash monitor
```

The active configuration selects 115200 baud, as in Phase 13.1. No baud settings,
partition layout, signing policy, or efuses were changed.

Temporary session logs:
- `/tmp/crystal_phase13_2_host.log`
- `/tmp/crystal_phase13_2_registry.log`
- `/tmp/crystal_phase13_2_build_diagnostic.log`
- `/tmp/crystal_phase13_2_build_final.log`
- `/tmp/crystal_phase13_2_device_diagnostic.log` (initial helper failure)
- `/tmp/crystal_phase13_2_device_diagnostic_retry.log` (27 passing checks)
- `/tmp/crystal_phase13_2_device_final.log`

Phase 13.3 and later UI work is not included. Full Phase 12 regression, OTA
testing, and touch-driven clear-data confirmation remain outside this validation.

## Final firmware result

Diagnostic and final signed builds succeeded without compiler warnings. The
final flash passed hash verification. Its boot installed dev_tester, clock,
weather, and calculator in slots 0/2/3/4, entered the shell, and returned from
app_main normally. First-frame baseline: 2287 ms; main-task stack headroom:
5156 bytes. No temporary diagnostic calls, abort, or watchdog reset appeared
in the observed final boot. The monitor was exited with Ctrl+].

Wi-Fi connection retries were observed; network reliability and OTA were not
validated. The earlier diagnostic abort remains documented above.

## Captured passing device evidence

```text
I (3468) phase13_state_test: === CrystalState::clear() Test ===
I (3479) phase13_state_test: PASS: snapshot existing Crystal NVS values and types
I (3479) phase13_state_test: PASS: test prefixes unused in saved NVS
I (3486) crystal_app: Cleared 0 keys under prefix 'a5e093e.'
I (3487) phase13_state_test: PASS: empty prefix clear
I (3500) phase13_state_test: PASS: write counter
I (3514) phase13_state_test: PASS: write score
I (3527) phase13_state_test: PASS: write message
I (3540) phase13_state_test: PASS: write other-app sentinel
I (3546) phase13_state_test: PASS: read counter before clear
I (3551) phase13_state_test: PASS: read score before clear
I (3557) phase13_state_test: PASS: read message before clear
I (3570) crystal_app: Cleared 3 keys under prefix 'a5e093e.'
I (3570) phase13_state_test: PASS: clear populated prefix
I (3572) phase13_state_test: PASS: counter erased
I (3576) phase13_state_test: PASS: score erased
I (3580) phase13_state_test: PASS: message erased
I (3585) phase13_state_test: PASS: other app retained
I (3600) phase13_state_test: PASS: snapshot after clear
I (3600) phase13_state_test: PASS: all preexisting NVS values and types unchanged
I (3607) crystal_app: Cleared 0 keys under prefix 'a5e093e.'
I (3607) phase13_state_test: PASS: repeat clear succeeds
I (3621) phase13_state_test: PASS: write after clear
I (3626) phase13_state_test: PASS: read after clear
I (3630) phase13_state_test: PASS: open native-type fixture
I (3636) phase13_state_test: PASS: write native NVS integer
I (3641) phase13_state_test: PASS: commit native fixture
I (3652) crystal_app: Cleared 2 keys under prefix 'a5e093e.'
I (3652) phase13_state_test: PASS: clear mixed blob/native types
I (3663) crystal_app: Cleared 1 keys under prefix 'a8d8ea9.'
I (3663) phase13_state_test: PASS: remove sentinel fixture
I (3678) phase13_state_test: PASS: snapshot final NVS
I (3678) phase13_state_test: PASS: exact original NVS restored; all fixtures gone
I (3680) phase13_state_test: === CrystalState::clear() Test PASS (27 checks) ===
```
