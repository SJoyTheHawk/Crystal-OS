# Phase 12.10 OTA Rollback Test Procedure

## Prerequisites

The bootloader currently on your device **must** have been built with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`. Since OTA updates **do not update the bootloader**, you need to flash via USB first.

## Step 1: Flash Rollback-Enabled Bootloader

This is critical. Without a rollback-enabled bootloader, the test will fail.

```bash
# In the Crystal OS project directory
idf.py build
idf.py flash

# This flashes:
# - Bootloader (with rollback support)
# - Partition table
# - ota_data_initial.bin (clean OTA data partition)
# - The current firmware into ota_0
```

**Wait for the device to boot completely and verify it's stable.**

## Step 2: Verify Baseline Firmware

Connect to the serial monitor and verify:

```
I (1159) app_init: App version:      0.12.11  # Or whatever version you flashed
I (1318) crystal_boot: === OTA Partition State at Boot ===
I (1318) crystal_boot: Running: ota_0 @ 0x30000 state=2  # ESP_OTA_IMG_VALID
I (1319) crystal_boot: Boot: ota_0 @ 0x30000 state=2
I (1324) crystal_boot: Next update slot: ota_1 @ 0x530000
I (1329) crystal_boot: Last invalid: NULL @ 0x0
I (1333) crystal_boot: Rollback possible: NO  # No rollback yet, baseline is good
```

Key points:
- Running from `ota_0`
- State is `2` (ESP_OTA_IMG_VALID; `0` is NEW and `1` is PENDING_VERIFY)
- Last invalid is NULL
- Rollback possible: NO (expected at this stage)

## Step 3: Perform OTA Update

Trigger an OTA update from the device:
1. Open Settings
2. Navigate to System > Software Update
3. Check for updates
4. Install the available update (should be 0.12.11 or newer)

**Expected behavior:**
- Download reaches 100%
- RSA signature verification passes
- Device reboots

## Step 4: Observe First Boot After OTA

The device will boot into the new firmware. Monitor the serial output:

```
I (605) boot: Loaded app from partition at offset 0x530000  # ota_1
I (1159) app_init: App version:      0.12.11
I (1318) crystal_boot: === OTA Partition State at Boot ===
I (1318) crystal_boot: Running: ota_1 @ 0x530000 state=1  # ESP_OTA_IMG_PENDING_VERIFY
I (1319) crystal_boot: Boot: ota_1 @ 0x530000 state=1
I (1324) crystal_boot: Next update slot: ota_0 @ 0x30000
I (1329) crystal_boot: Last invalid: NULL @ 0x0
I (1333) crystal_boot: Rollback possible: YES  # Rollback is now possible
I (1315) crystal_boot: First boot after OTA; image pending confirmation
I (3158) update: OTA image pending confirmation: running 0.12.11
E (3168) crystal_boot: === FAULT INJECTION ACTIVE ===
E (3168) crystal_boot: Partition: ota_1 @ 0x530000
E (3170) crystal_boot: State: PENDING_VERIFY (expecting rollback after multiple crashes)
E (3178) crystal_boot: FAULT INJECTION: crashing before OTA confirmation
```

The device will crash and reboot.

## Step 5: Monitor Crash/Reboot Cycles

ESP-IDF's automatic rollback typically requires **3 consecutive failed boots**. You should see:

### Boot #2 (First retry)
```
I (605) boot: Loaded app from partition at offset 0x530000  # Still ota_1
...
E (3168) crystal_boot: FAULT INJECTION: crashing before OTA confirmation
```

### Boot #3 (Second retry)
```
I (605) boot: Loaded app from partition at offset 0x530000  # Still ota_1
...
E (3168) crystal_boot: FAULT INJECTION: crashing before OTA confirmation
```

### Boot #4 (Rollback should trigger)

**If rollback works:**
```
I (605) boot: Loaded app from partition at offset 0x30000  # Back to ota_0
I (1159) app_init: App version:      0.12.10  # Previous version
I (1318) crystal_boot: === OTA Partition State at Boot ===
I (1318) crystal_boot: Running: ota_0 @ 0x30000 state=2  # ESP_OTA_IMG_VALID
I (1329) crystal_boot: Last invalid: ota_1 @ 0x530000  # Marked as invalid
I (3162) update: Rollback detected: attempted 0.12.11, running 0.12.10
I (3165) update: OTA worker initialized on Core 0
W (3226) crystal_boot: Posting OTA rollback notification
```

You should see a toast notification: **"Update failed, rolled back"**

**If rollback does NOT work:**
- The device keeps booting ota_1 @ 0x530000
- Same crash repeats indefinitely
- **Root cause**: Bootloader on device doesn't have rollback support

## Step 6: Validation Checklist

After successful rollback:

- [ ] Device booted back into ota_0 (previous firmware)
- [ ] Log shows: "Rollback detected: attempted X.Y.Z, running X.Y.W"
- [ ] Toast notification shown: "Update failed, rolled back"
- [ ] ota_1 marked as "Last invalid"
- [ ] Device is stable on the previous firmware
- [ ] OTA marker `attempted_ver` was cleared from NVS

## Troubleshooting

### Device keeps crashing on ota_1, no rollback

**Cause**: The bootloader on your device was not built with rollback support.

**Fix**: Return to Step 1 and reflash via USB. The bootloader is **only** updated via serial flash, never via OTA.

### Rollback works, but previous app doesn't exist

**Symptoms**: Device won't boot, or boots to a very old firmware

**Cause**: ota_0 was empty or contained an invalid image

**Fix**: After Step 1, let the device run for a while to ensure ota_0 is known-good before performing the OTA test.

### Rollback takes more than 3 boots

**Behavior**: Normal. ESP-IDF documentation mentions "several failed boots" but doesn't guarantee the exact count. It could be 3-5 depending on the failure mode.

**Action**: Keep monitoring. If it reaches 10 boots without rollback, the bootloader likely doesn't support it.

## Step 7: Remove Fault Injection

After successful rollback validation:

1. Open `main/main.cpp`
2. Change line 36:
   ```c
   #define CRYSTAL_OTA_FAULT_INJECTION 1
   ```
   to:
   ```c
   #define CRYSTAL_OTA_FAULT_INJECTION 0
   ```
3. Build and flash: `idf.py flash`

## Expected Log Markers

### Successful OTA (normal case)
```
I (1315) crystal_boot: First boot after OTA; image pending confirmation
I (3158) update: OTA image pending confirmation: running X.Y.Z
I (3714) crystal_core: Confirming OTA image after 1.2s settling period
I (3714) crystal_core: OTA image confirmed and rollback cancelled
I (3722) crystal_core: Post-confirmation state: 1
```

### Rollback Detection
```
I (1329) crystal_boot: Last invalid: ota_1 @ 0x530000
I (3162) update: Rollback detected: attempted X.Y.Z, running A.B.C
W (3226) crystal_boot: Posting OTA rollback notification
```

## Notes

- RTC memory preserves the rollback counter across reboots
- A "failed boot" = doesn't call `esp_ota_mark_app_valid_cancel_rollback()` before crash/watchdog/panic
- The 1.2s settling delay in `crystal_core.cpp:service_task()` gives early crashes time to trigger rollback
- After successful confirmation, the image state changes from `1` (PENDING_VERIFY) to `2` (VALID)

### Download reaches 100%, then reports "Update failed, nothing changed"

This is a pre-reboot installation failure, so rollback has not started. Check the
`update` log for `OTA install failed:` and `Downloaded image SHA-256 mismatch:`.
The manifest hash must be calculated from the exact signed `.bin` file served at
the URL, and the hosted binary must be uploaded before the manifest is replaced.
