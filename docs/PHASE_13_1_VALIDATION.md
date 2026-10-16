# Phase 13.1 — Registry Query API Validation

Date: 2026-10-15 (Asia/Hong_Kong)

## Scope and implementation

Phase 13.1 verifies the existing registry API before Manage Apps UI work.
The six requested functions already existed and passed validation; production
registry behavior needed no changes. The public header now documents defaults,
installed-list scope, index bounds, and next-boot setter behavior.

Permanent regression coverage is in `tools/test_crystal_registry.py`. It compiles
the actual registry source with host adapters for storage, app instances, logging,
and Brookesia installation, using the real registry and HAL headers and
`-Wall -Wextra -Werror`. It covers:

- Installed count, IDs, app pointers, and out-of-range indices.
- Missing keys, invalid IDs, absent storage, read/write failures, and malformed
  stored blob lengths.
- Enabled false/true readback with opposite defaults.
- Slot 0/42/65535 roundtrips, separate keys, and long app IDs.
- Default and persisted order, stable equal-slot ordering, disabled/all-disabled
  lists, and unchanged installed lists after setters.
- Invalid install arguments, failed factories, failed phone installation,
  partial installation, and rejected-app cleanup.

Run: `python3 tools/test_crystal_registry.py`.

## Hardware validation

Board: ESP32-S3 revision v0.2 on `/dev/cu.usbmodem101`.

A temporary boot test ran after registry installation and before shell
initialization. **All 32 checks passed** against real encrypted NVS. It enumerated:

| Index | App ID | Enabled | Slot |
|---|---|---|---|
| 0 | dev_tester | true | 0 |
| 1 | clock | true | 2 |
| 2 | weather | true | 3 |
| 3 | calculator | true | 4 |

Clock enabled/slot keys were snapshotted before writing. Both were originally
absent; after false/true and 0/42/65535 roundtrips they were erased, committed,
and verified absent through a newly opened NVS handle. No saved app policy was
changed. Setters left the running installed list unchanged. The diagnostic boot
continued into the shell and returned normally from `app_main`; first-frame
baseline was 2568 ms, with 5076 bytes of main-task stack headroom.

The temporary helper and startup call were removed after validation. The final
firmware uses the original startup path. The retained host test adds no firmware
code or boot-time NVS writes.

## Build and burn procedure

Commands used for diagnostic and final images:

```bash
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
./scripts/build_dev.sh
idf.py -p /dev/cu.usbmodem101 flash monitor
```

The active sdkconfig and monitor use **115200 baud**, while README/defaults
mention 2000000. The requested command selects the active 115200 rate; no baud
configuration was changed. Serial access required execution outside the sandbox.
Flashing used the existing partition table and signed development profile,
without erase-flash or efuse changes.

Session logs (temporary paths):
- `/tmp/crystal_phase13_build_diagnostic.log`
- `/tmp/crystal_phase13_build_final.log`
- `/tmp/crystal_phase13_device_diagnostic.log`
- `/tmp/crystal_phase13_device_final.log`

## Final firmware result

Both signed builds succeeded without compiler warnings. The final flash passed
hash verification and installed all four apps in slots 0/2/3/4. The shell selected
dev_tester, first-frame baseline was 2285 ms, and main-task stack headroom was
5172 bytes. app_main returned normally, with no diagnostic test logs, panic, or
watchdog reset in the observed boot. The built-in HTTPS smoke test also returned
HTTP 200 and passed. The monitor was exited with Ctrl+].

## Findings for later sub-phases

Installed queries describe running enabled apps, not the complete firmware
catalog. Manage Apps must enumerate disabled catalog entries separately. Reads
use caller defaults and do not seed NVS: `enabled(id, false)` can report false
for a default-enabled app without a persisted key. Display state using the
corresponding firmware entry defaults. A serial monitor is not a C++ interpreter.

Phase 13.2 and later work is outside this change. This verification does not
claim full Phase 12 regressions, OTA validation, touch interaction testing, or
power-cycle durability of newly changed settings. Wi-Fi connection retries were
observed during monitoring, including after the successful HTTPS smoke test;
sustained connectivity was not validated in this phase.

## Captured registry evidence

```text
I (3312) phase13_registry_test: === Registry API Test ===
I (3314) phase13_registry_test: Installed apps: 4
I (3316) phase13_registry_test: PASS: installed count matches enabled firmware entries
I (3324) phase13_registry_test: PASS: installed ID and launcher order
I (3330) phase13_registry_test: PASS: installed app pointer
I (3335) phase13_registry_test:   [0] dev_tester: enabled=1, slot=0
I (3341) phase13_registry_test: PASS: installed ID and launcher order
I (3347) phase13_registry_test: PASS: installed app pointer
I (3352) phase13_registry_test:   [1] clock: enabled=1, slot=2
I (3358) phase13_registry_test: PASS: installed ID and launcher order
I (3364) phase13_registry_test: PASS: installed app pointer
I (3369) phase13_registry_test:   [2] weather: enabled=1, slot=3
I (3375) phase13_registry_test: PASS: installed ID and launcher order
I (3381) phase13_registry_test: PASS: installed app pointer
I (3387) phase13_registry_test:   [3] calculator: enabled=1, slot=4
I (3393) phase13_registry_test: PASS: out-of-range queries
I (3398) phase13_registry_test: PASS: invalid ID defaults
I (3403) phase13_registry_test: PASS: snapshot original clock keys
I (3422) phase13_registry_test: PASS: disable write
I (3428) phase13_registry_test: PASS: disabled readback with opposite default
I (3447) phase13_registry_test: PASS: enable write
I (3452) phase13_registry_test: PASS: enabled readback with opposite default
I (3472) phase13_registry_test: PASS: slot write
I (3477) phase13_registry_test: PASS: slot readback
I (3494) phase13_registry_test: PASS: slot write
I (3499) phase13_registry_test: PASS: slot readback
I (3517) phase13_registry_test: PASS: slot write
I (3522) phase13_registry_test: PASS: slot readback
I (3526) phase13_registry_test: PASS: setters leave installed count unchanged
I (3533) phase13_registry_test: PASS: setters leave installed order unchanged
I (3539) phase13_registry_test: PASS: setters leave installed order unchanged
I (3546) phase13_registry_test: PASS: setters leave installed order unchanged
I (3553) phase13_registry_test: PASS: setters leave installed order unchanged
I (3561) phase13_registry_test: PASS: restore original clock key
I (3567) phase13_registry_test: PASS: restore original clock key
I (3572) phase13_registry_test: PASS: commit restored clock keys
I (3577) phase13_registry_test: PASS: original key value/absence restored
I (3584) phase13_registry_test: PASS: original key value/absence restored
I (3590) phase13_registry_test: === Registry API Test PASS ===
```
