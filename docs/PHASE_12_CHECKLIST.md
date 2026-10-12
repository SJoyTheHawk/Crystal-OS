# Phase 12 Implementation Checklist

**Purpose:** Track progress through Phase 12 sub-phases with visible validation checkpoints.

**How to use:** Check off each item as you complete it. Each sub-phase must pass its validation before moving to the next.

---

## Progress Overview

- [x] 12.1 Version Infrastructure (2h)
- [x] 12.2 Coredump NVS Infrastructure (3h)
- [x] 12.3 Crash Recovery Toast (2h)
- [x] 12.4 Signed Builds (4h)
- [x] 12.5 Update State Machine Stub (3h)
- [x] 12.6 Manifest Parsing (3h)
- [x] 12.7 OTA Worker Thread (2h)
- [x] 12.8 Download Progress UI (3h)
- [x] 12.9 Flash Write + Reboot (4h) — signed OTA update completed on device
- [x] 12.10 Rollback Detection (3h) — validated on device; clean firmware flashed after test
- [x] 12.11 Primary/Mirror Fallback (2h) — implementation complete; mirror-host validation deferred
- [ ] 12.12 USB Recovery Tool (4h)
- [ ] 12.13 Full Validation Suite (6h)

**Current status (2026-10-13):** Phases 12.1–12.8 are implemented and
device-checked. Phase 12.9 has been validated with a signed HTTPS download,
SHA-256 verification, inactive-slot flash, reboot selection, and delayed boot
confirmation. A same-version check against the hosted 0.12.8 manifest was also
verified. The 12.9 Install/Cancel confirmation is implemented. OTA HTTPS uses asynchronous
HTTP polling with short yields so software TLS ECDH does not starve `IDLE0`; the
latest build also persists the attempted version and detects a bootloader rollback.
Phase 12.10 has been validated with a signed 0.12.12 OTA, automatic rollback to
ota_0, last-invalid partition reporting, rollback detection, and the one-shot
notification; the temporary fault-injection block was then removed and clean
0.12.12 firmware was flashed over USB.
Phase 12.11 is implemented and closed in firmware: manifest and binary source
fallbacks are ordered and logged, all-source download failure is reported by a
blocking result dialog, and a successful OTA was confirmed on the device. The
Bubstal mirror host is currently unreachable, so a live mirror-success run remains
deferred until that host is restored.

**Total: ~41 hours (~1 week)**

---

## Sub-Phase 12.1: Version Infrastructure ✓ / ✗

### Tasks
- [ ] Create `main/crystal_version.{h,c}`
- [ ] Implement major/minor/patch/string accessors
- [ ] Update Device Status to show version
- [ ] Update CMakeLists.txt

### Validation Checkpoints
- [ ] Build succeeds
- [ ] `strings build/crystal_os.bin | grep "0.11.0"` shows version
- [ ] Device boots
- [ ] Settings → Device Status shows "Firmware Version: 0.11.0"

### Commit Message
```
Phase 12.1: Add version infrastructure and Device Status display

- Embed 0.11.0 from CMakeLists project() into firmware
- Add crystal_version API for major/minor/patch/string accessors
- Display version in Settings › Device Status

Checkpoint: Device Status shows 'Firmware Version: 0.11.0'
```

---

## Sub-Phase 12.2: Coredump NVS Infrastructure ✓ / ✗

### Tasks
- [ ] Create `main/crystal_crash.{h,c}`
- [ ] Implement crash summary structure (task/PC/backtrace/ELF SHA)
- [ ] Read coredump partition at boot
- [ ] Store summary in NVS
- [ ] Update Device Status to show "Last Crash"
- [ ] Call `crystal_crash_init()` in app_main

### Validation Checkpoints
- [ ] Build succeeds
- [ ] Boot logs show "No coredump found"
- [ ] Device Status shows "Last Crash: None"
- [ ] (Optional) Trigger test crash, verify summary saved
- [ ] After test crash, Device Status shows "Last Crash: task @ 0xXXXXXXXX"

### Commit Message
```
Phase 12.2: Add coredump NVS infrastructure

- Read coredump partition at boot, extract summary
- Store crash summary (task, PC, backtrace, ELF SHA) in NVS
- Display 'Last Crash' in Device Status
- Persist 'show toast' flag for next sub-phase

Checkpoint: Device Status shows 'Last Crash: None' (or details after crash)
```

---

## Sub-Phase 12.3: Crash Recovery Toast ✓ / ✗

### Tasks
- [ ] Add `UI_EVT_CRASH_RECOVERED` event
- [ ] Post event from shell init if flag set
- [ ] Handle event to show toast
- [ ] Clear notify flag after posting

### Validation Checkpoints
- [ ] Normal boot shows no toast
- [ ] Trigger crash (test button or deliberate abort)
- [ ] After reboot, toast "Recovered from an error" appears for 3 seconds
- [ ] Reboot again, toast does NOT appear (flag cleared)
- [ ] Device Status still shows crash details (persistent)

### Commit Message
```
Phase 12.3: Add crash recovery toast

- Post UI_EVT_CRASH_RECOVERED from shell init if flag set
- Show 'Recovered from an error' toast once
- Clear notify flag immediately after posting event

Checkpoint: Toast appears once after crash, not on subsequent reboots
```

---

## Sub-Phase 12.4: Signed Builds ✓ / ✗

### Tasks
- [ ] Enable `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y`
- [ ] Generate dev RSA-3072 key: `~/.crystal_keys/dev_signing_key.pem`
- [ ] Generate prod RSA-3072 key: `~/.crystal_keys/prod_signing_key.pem`
- [ ] Create `scripts/build_dev.sh`
- [ ] Create `scripts/build_prod.sh`
- [ ] Make scripts executable
- [ ] Update .gitignore to exclude `*.pem`

### Validation Checkpoints
- [ ] `./scripts/build_dev.sh` succeeds
- [ ] `espsecure.py signature_info_v2 build/crystal_os.bin` shows valid signature
- [ ] `./scripts/build_prod.sh` succeeds
- [ ] Prod signature differs from dev signature
- [ ] Binary size < 5 MiB
- [ ] Flash dev build, device boots normally

### Commit Message
```
Phase 12.4: Add signed build profiles (dev + prod)

- Enable CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT
- Create build_dev.sh and build_prod.sh scripts
- Generate separate RSA-3072 keys (outside repo)
- Add release archive structure

Checkpoint: Both profiles build, signatures verify, keys are different
```

---

## Sub-Phase 12.5: Update State Machine Stub ✓ / ✗

### Tasks
- [ ] Create `main/crystal_update.{h,c}`
- [ ] Define state enum and snapshot struct
- [ ] Implement stub functions (check/cancel/install/snapshot)
- [ ] Add "Check for Updates" button in Settings
- [ ] Call `crystal_update_init()` in app_main

### Validation Checkpoints
- [ ] Build succeeds
- [ ] Settings shows "Check for Updates" button
- [ ] Tap button, logs show "Checking for updates (stub)"
- [ ] No crash

### Commit Message
```
Phase 12.5: Add update state machine stub

- Define crystal_update API: check/cancel/install/snapshot
- Implement stub that logs actions without real OTA
- Add 'Check for Updates' button in Settings › System

Checkpoint: Button appears, tap logs 'Checking for updates (stub)'
```

---

## Sub-Phase 12.6: Manifest Parsing ✓ / ✗

### Tasks
- [ ] Create `main/crystal_update_manifest.{h,c}`
- [ ] Implement SemVer comparison function
- [ ] Implement manifest parser with strict validation
- [ ] Create `main/test/test_update_manifest.c`
- [ ] Add unit tests for SemVer and manifest cases

### Validation Checkpoints
- [ ] Build succeeds
- [ ] Flash to device
- [ ] Run `test "[update]"` in monitor
- [ ] All 5+ unit tests pass (SemVer, valid manifest, version checks, target/channel, HTTPS)

### Commit Message
```
Phase 12.6: Add manifest parsing and validation

- Parse manifest JSON with cJSON (schema/target/channel/version/size/sha256/urls)
- Implement strict SemVer comparison (reject equal/older/malformed)
- Enforce HTTPS-only URLs, target/channel matching
- Add comprehensive unit tests for validation rules

Checkpoint: All unit tests pass, strict version ordering confirmed
```

---

## Sub-Phase 12.7: OTA Worker Thread ✓ / ✗

### Tasks
- [ ] Add worker task creation in `crystal_update_init()`
- [ ] Create request queue
- [ ] Pin worker to Core 0
- [ ] Update `crystal_update_check()` to queue request
- [ ] Worker processes check/cancel requests

### Validation Checkpoints
- [ ] Build succeeds
- [ ] Boot logs show "OTA worker started on Core 0"
- [ ] (Optional) Task list shows `ota_worker` on Core 0
- [ ] Tap "Check for Updates"
- [ ] Logs show "Processing update check request"

### Commit Message
```
Phase 12.7: Add dedicated OTA worker thread

- Create ota_worker task on Core 0 with 8KB stack
- Add request queue for check/cancel operations
- Worker processes requests without blocking LVGL task

Checkpoint: Boot log shows 'OTA worker started on Core 0'
```

---

## Sub-Phase 12.8: Download Progress UI ✓ / ✗

### Tasks
- [ ] Add `UI_EVT_OTA_CHANGED` event
- [ ] Create `main/ui/ota_modal.{h,cpp}`
- [ ] Implement modal with progress bar
- [ ] Update worker to fetch manifest via HTTP
- [ ] Parse manifest in worker
- [ ] Simulate download progress (10% steps)
- [ ] Post UI events for progress updates
- [ ] Handle events in shell

### Validation Checkpoints
- [ ] Build succeeds
- [ ] Device has Wi-Fi connected
- [ ] Tap "Check for Updates"
- [ ] Modal appears with "Checking for updates..."
- [ ] Logs show "Manifest fetched: X bytes"
- [ ] Logs show "Manifest valid: 0.11.0 → 0.12.0"
- [ ] Progress bar animates 0% → 100%
- [ ] Modal closes, toast shows "Update ready to install"
- [ ] Test offline: modal shows "Network error"

### Commit Message
```
Phase 12.8: Add download progress UI

- Create OTA modal with progress bar and message label
- Fetch manifest from primary URL via esp_http_client
- Parse manifest, simulate download progress (10% steps)
- Post UI_EVT_OTA_CHANGED to show modal updates

Checkpoint: Modal shows progress 0% → 100%, toast confirms completion
```

---

## Sub-Phase 12.9: Flash Write + Reboot ✓ / ✗

### Tasks
- [ ] Replace simulated download with `esp_https_ota_begin/perform/finish`
- [ ] Stream download with progress updates
- [ ] Persist attempted version in NVS before reboot
- [ ] Mark partition as pending
- [ ] Reboot after 2-second announcement
- [ ] Confirm image after 1.2s settling in shell init

### Validation Checkpoints
- [ ] Setup OTA server (local or cloud) with 0.12.0 firmware
- [ ] Flash 0.11.0 to device
- [ ] Tap "Check for Updates"
- [ ] Progress bar shows real download (not simulated)
- [ ] Logs show "OTA image received completely"
- [ ] Logs show "OTA successful, rebooting in 2 seconds..."
- [ ] Modal shows "Installing update..."
- [ ] Device reboots
- [ ] After reboot, logs show "Confirming OTA image after 1.2s settling"
- [ ] Device Status shows "Firmware Version: 0.12.0"

### Commit Message
```
Phase 12.9: Implement real OTA flash write and reboot

- Replace simulated download with esp_https_ota
- Stream download with progress updates
- Write to OTA partition, mark as pending
- Reboot after 2-second announcement
- Confirm image after 1.2s settling interval

Checkpoint: Real OTA completes, device reboots to new version
```

---

## Sub-Phase 12.10: Rollback Detection ✓ / ✗

### Context

ESP32 OTA rollback requires a bootloader built with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`. **OTA updates DO NOT update the bootloader**. The bootloader on your device must be flashed via USB before testing rollback.

Automatic rollback works by counting consecutive failed boots (typically 3+). A "failed boot" means the app crashes or reboots before calling `esp_ota_mark_app_valid_cancel_rollback()`.

### Tasks
- [x] Persist attempted version in NVS before reboot (already in 12.9)
- [x] Add `crystal_update_check_rollback()` function
- [x] Detect rollback (current != attempted) at boot
- [x] Add `UI_EVT_OTA_ROLLED_BACK` event
- [x] Post event from shell if rollback detected
- [x] Show "Update failed, rolled back" result dialog with OK
- [x] Clear rollback flag after display
- [x] Add comprehensive OTA partition state logging
- [x] Use temporary fault injection for rollback testing, then remove it

### Validation Checkpoints

**IMPORTANT:** Follow the detailed procedure in `docs/PHASE_12_10_TEST_PROCEDURE.md`

#### Pre-Test Setup
- [x] Flash bootloader via USB: `idf.py flash`
- [x] Verify baseline boots successfully from ota_0
- [x] Confirm boot logs show: `Rollback possible: NO` (expected at baseline)

#### OTA Rollback Test
- [x] Perform OTA update to trigger fault injection
- [x] First boot shows: `Running: ota_1 @ 0x530000 state=1` (PENDING_VERIFY)
- [x] First boot shows: `Rollback possible: YES`
- [x] Fault injection crashes the app
- [x] After 3+ crash/reboot cycles, device rolls back to ota_0
- [x] Boot log shows: `Last invalid: ota_1 @ 0x530000`
- [x] Boot log shows: `Rollback detected: attempted X.Y.Z, running X.Y.W`
- [x] Result dialog: "Update failed, rolled back" remains until OK
- [x] Device is stable on previous firmware
- [x] `attempted_ver` cleared from NVS
- [x] Reboot again, toast does NOT appear

#### Post-Test Cleanup
- [x] Remove the temporary fault-injection block from `main.cpp`
- [x] Build and flash clean firmware: `idf.py flash`

### Troubleshooting

**Device keeps crashing, no rollback:**
- Root cause: Bootloader doesn't have rollback support
- Fix: Flash via USB (`idf.py flash`)

**Previous firmware not available:**
- Root cause: ota_0 was empty or invalid
- Fix: Let baseline run stable before OTA test

### Commit Message
```
Phase 12.10: Implement OTA rollback detection and validation

- Persist attempted OTA version before reboot
- Detect rollback when running version differs from attempted
- Add comprehensive OTA partition state logging
- Use temporary fault injection for rollback testing, then remove it
- Clear attempted version marker after successful confirmation

Validation procedure: docs/PHASE_12_10_TEST_PROCEDURE.md
Investigation notes: docs/PHASE_12_10_ROLLBACK_INVESTIGATION.md

Checkpoint: Rollback detection logs and result dialog behave correctly; automatic rollback
was validated and the temporary fault injection was removed afterward.

```

---

## Sub-Phase 12.11: Primary/Mirror Fallback ✓

### Tasks
- [x] Extract `fetch_manifest()` helper function
- [x] Try primary manifest, then mirror on failure
- [x] Loop through manifest URLs for binary download
- [x] Log fallback attempts

### Validation Checkpoints
- [x] Review the primary-to-mirror manifest fallback path
- [x] Tap "Check for Updates" and verify the update flow on the device
- [x] Verify ordered source attempts and fallback failure handling in the device log
- [ ] Live mirror manifest success (deferred while `updates.bubstal.com` is unreachable)
- [x] Verify all-source binary failure and the blocking failure dialog
- [ ] OTA succeeds from the second URL (deferred while `updates.bubstal.com` is unreachable)

### Closure note

The fallback implementation is closed and included in the Phase 12.11 commit.
The two unchecked rows require a reachable Bubstal host and are an environment
follow-up, not an open firmware change.

### Commit Message
```
Phase 12.11: Add primary/mirror fallback logic

- Try mirror manifest if primary fetch fails
- Try all URLs in manifest.urls[] order until one succeeds
- Log fallback attempts for debugging

Checkpoint: Mirror fallback works for both manifest and binary
```

---

## Sub-Phase 12.12: USB Recovery Tool ✓ / ✗

### Tasks
- [ ] Create `tools/crystal_recovery.py`
- [ ] Implement preserve mode (flash bootloader/partition/app, keep NVS)
- [ ] Implement factory reset mode (erase all, flash from scratch)
- [ ] Implement coredump extraction
- [ ] Add confirmation prompts
- [ ] Create `tools/README.md` with usage examples
- [ ] Make script executable

### Validation Checkpoints
- [ ] Build firmware with `./scripts/build_dev.sh`
- [ ] Run `./tools/crystal_recovery.py --port <port> --firmware build/ --preserve`
- [ ] Script shows menu, asks for confirmation
- [ ] Flash completes, shows "✅ Recovery complete!"
- [ ] Device boots, settings are preserved
- [ ] Run `./tools/crystal_recovery.py --port <port> --firmware build/ --factory-reset`
- [ ] Script shows two warnings, asks twice for confirmation
- [ ] Flash completes, all data erased
- [ ] Device boots to factory state
- [ ] (If coredump exists) Extract with `--extract-coredump`, decode with esp-coredump.py

### Commit Message
```
Phase 12.12: Add USB recovery tool

- Python script wraps esptool for firmware recovery
- Preserve mode: flash bootloader/partition/app, keep NVS/SPIFFS
- Factory reset mode: erase all, flash from scratch
- Coredump extraction and decode instructions

Checkpoint: Recovery tool restores firmware, shows usage menu
```

---

## Sub-Phase 12.13: Full Validation Suite ✓ / ✗

### Pre-Flight Checklist
- [ ] All previous sub-phases completed
- [ ] All unit tests passing
- [ ] Device has stable Wi-Fi connection
- [ ] OTA server configured with test firmware

### Manifest Validation Tests
- [ ] Valid manifest (newer version) → Accepted
- [ ] Equal version → Rejected
- [ ] Older version → Rejected
- [ ] Wrong target → Rejected
- [ ] Wrong channel → Rejected
- [ ] Malformed JSON → Rejected
- [ ] Non-HTTPS URL → Rejected

### Network Tests
- [ ] Offline (no Wi-Fi) → Error displayed
- [ ] Primary fails, mirror succeeds → Downloads from mirror
- [ ] Both manifests fail → Error displayed
- [ ] Primary binary fails, mirror succeeds → Downloads from mirror
- [ ] All binary URLs fail → Error displayed

### OTA Workflow Tests
- [ ] Full successful OTA → Progress shown, reboot, new version
- [ ] Cancel during download → Stopped, state reset
- [ ] Power interrupt during write → Running firmware unchanged
- [ ] Low battery (<40%) → Blocked
- [ ] Unknown battery state → Blocked

### Rollback Tests
- [ ] Faulty image (crash before confirm) → Auto-rollback, toast
- [ ] Second boot after rollback → No toast, history visible

### USB Recovery Tests
- [ ] Preserve mode → Firmware restored, settings kept
- [ ] Factory reset → Firmware restored, all data erased
- [ ] Coredump extraction → File saved, decodable

### Phase 11 Regression Tests
- [ ] Settings navigation → All pages load
- [ ] Wi-Fi scan and connect → Works
- [ ] Keyboard input → Text entry works
- [ ] Gesture navigation (swipe back) → Works
- [ ] Timer alert → Alert shown
- [ ] Power modes (sleep/wake) → Works correctly
- [ ] Device Status display → All fields populated

### Documentation Updates
- [ ] Update `docs/CODE_GUIDE.md` with OTA patterns
- [ ] Update `docs/DESIGN.md` with recovery architecture
- [ ] Create release notes for Phase 12

### Final Commit
```
Phase 12.13: Complete validation suite

Tested:
- All manifest validation rules
- Network fallback (primary/mirror)
- Full OTA workflow (0.11.0 → 0.12.0)
- Automatic rollback on faulty image
- USB recovery (preserve and factory reset)
- Coredump extraction and decode
- Phase 11 regressions (Settings, Wi-Fi, timers, gestures)

All test cases passed. Phase 12 complete.
```

---

## Final Checklist

### Before Marking Phase 12 Complete
- [ ] All 13 sub-phases completed
- [ ] All validation checkpoints passed
- [ ] All commits pushed to repository
- [ ] Documentation updated
- [ ] No known regressions
- [ ] Release artifacts archived

### Phase 12 Deliverables
- [ ] Firmware reports version (0.11.0)
- [ ] Crash tracking and recovery toast working
- [ ] Signed dev and prod build profiles
- [ ] OTA update system functional (manifest → download → flash → reboot)
- [ ] Automatic rollback on faulty images
- [ ] Primary/mirror fallback logic
- [ ] USB recovery tool (preserve + factory reset)
- [ ] Coredump extraction capability
- [ ] Comprehensive test results documented

---

## Notes

**Safety Principle:** Each sub-phase produces a visible, testable change. Commit after passing validation.

**If a Sub-Phase Fails:**
1. Review the detailed guide in `PHASE_12_DETAILED_GUIDE.md`
2. Check logs for specific error messages
3. Verify all prerequisites from previous sub-phases
4. Test each validation checkpoint individually
5. Do not proceed to next sub-phase until current one passes

**Estimated Timeline:**
- Ideal case: 1 week (41 hours)
- With testing/debugging: 1.5-2 weeks
- First-time implementation: 2-3 weeks

**Document Version:** 1.0  
**Created:** 2026-10-11
