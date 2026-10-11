# Phase 12.10 OTA Rollback Investigation

## Current Problem

The device performed OTA from a previous firmware to 0.12.11, and the new image was injected with a deliberate crash before OTA confirmation. Expected behavior: bootloader should roll back to the previous OTA slot. Actual behavior: the device keeps rebooting into the same faulty 0.12.11 image at ota_1 (0x530000).

## Boot Log Analysis

### First Boot (after OTA reboot)
```
I (605) boot: Loaded app from partition at offset 0x530000
...
I (3158) update: OTA image pending confirmation: running 0.12.11
E (3168) crystal_boot: FAULT INJECTION: crashing before OTA confirmation
```

### Second Boot (after crash)
```
I (605) boot: Loaded app from partition at offset 0x530000
...
I (3158) update: OTA image pending confirmation: running 0.12.11
E (3168) crystal_boot: FAULT INJECTION: crashing before OTA confirmation
```

**Critical observation:** The bootloader keeps loading the same partition (0x530000 = ota_1) despite the image being in PENDING_VERIFY state and crashing without confirmation.

## Root Cause Analysis

### 1. Bootloader Version Mismatch

The bootloader timestamp shows:
```
I (24) boot: compile time Oct 11 2026 05:20:26
```

The current app was built on Oct 12 2026. **OTA updates DO NOT update the bootloader**. The bootloader currently on the device may have been flashed before `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` was added to sdkconfig.defaults.

### 2. Rollback Mechanism Requirements

For ESP32 automatic rollback to work:
- Bootloader must be built with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`
- New image boots in `ESP_OTA_IMG_PENDING_VERIFY` state
- If the image doesn't call `esp_ota_mark_app_valid_cancel_rollback()` AND crashes/reboots 3+ times, bootloader should roll back
- **Critical:** The rollback counter is in RTC memory and persists across reboots

### 3. Potential Issues

1. **Bootloader on device predates rollback support** (most likely)
2. RTC memory not preserving rollback counter
3. Not enough reboot attempts (ESP-IDF may require 3-5 failed boots)
4. OTA data partition not correctly tracking slot states

## Diagnostic Steps

### Step 1: Check OTA Data Partition

Add logging to read and display the OTA data partition before and after operations:

```c
#include "esp_ota_ops.h"

void log_ota_data_info(const char *context) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    
    esp_ota_img_states_t running_state = ESP_OTA_IMG_UNDEFINED;
    if (running) esp_ota_get_state_partition(running, &running_state);
    
    ESP_LOGI(TAG, "=== OTA Status: %s ===", context);
    ESP_LOGI(TAG, "Running partition: %s @ 0x%lx (state=%d)",
             running ? running->label : "NULL",
             running ? running->address : 0,
             running_state);
    ESP_LOGI(TAG, "Boot partition: %s @ 0x%lx",
             boot ? boot->label : "NULL",
             boot ? boot->address : 0);
    ESP_LOGI(TAG, "Next update slot: %s @ 0x%lx",
             next ? next->label : "NULL",
             next ? next->address : 0);
    
    // Check if rollback is possible
    const esp_partition_t *last_valid = esp_ota_get_last_invalid_partition();
    ESP_LOGI(TAG, "Last invalid partition: %s @ 0x%lx",
             last_valid ? last_valid->label : "NULL",
             last_valid ? last_valid->address : 0);
    
    // Check rollback possibility
    bool rollback_possible = esp_ota_check_rollback_is_possible();
    ESP_LOGI(TAG, "Rollback possible: %s", rollback_possible ? "YES" : "NO");
}
```

### Step 2: Force Rollback Test

Test the rollback mechanism explicitly:

```c
// After detecting PENDING_VERIFY state, try explicit rollback
if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
    ESP_LOGW(TAG, "Testing explicit rollback mechanism");
    log_ota_data_info("before rollback");
    esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
    // Should not reach here
    ESP_LOGE(TAG, "Rollback failed: %s", esp_err_to_name(err));
}
```

### Step 3: Flash Complete Baseline

The most reliable fix is to flash everything from scratch:

```bash
# Build with rollback-enabled bootloader
idf.py build

# Flash bootloader, partition table, ota_data_initial, and app
idf.py flash

# This ensures:
# - Bootloader has rollback support
# - OTA data partition starts clean
# - A known-good baseline is in ota_0
```

### Step 4: Multi-Boot Counter Test

ESP-IDF's rollback mechanism may require multiple consecutive failed boots. Add a reboot counter:

```c
// In NVS: "ota.boot_count"
static void increment_boot_counter() {
    nvs_handle_t handle;
    if (nvs_open("ota", NVS_READWRITE, &handle) != ESP_OK) return;
    
    uint32_t count = 0;
    nvs_get_u32(handle, "boot_count", &count);
    count++;
    nvs_set_u32(handle, "boot_count", count);
    nvs_commit(handle);
    nvs_close(handle);
    
    ESP_LOGI(TAG, "Boot attempt #%lu for this image", count);
}

static void clear_boot_counter() {
    nvs_handle_t handle;
    if (nvs_open("ota", NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_erase_key(handle, "boot_count");
    nvs_commit(handle);
    nvs_close(handle);
}
```

## Recommended Fix Order

1. **Immediate**: Add OTA partition logging at boot (Step 1)
2. **Test**: Try explicit rollback with `esp_ota_mark_app_invalid_rollback_and_reboot()`
3. **Baseline**: Perform full USB flash with new bootloader
4. **Validation**: Repeat OTA test from clean baseline
5. **Cleanup**: Remove fault injection after successful rollback validation

## ESP-IDF Rollback Behavior (from docs)

From ESP-IDF documentation:
- Bootloader tracks boot attempts in RTC memory
- After 3 consecutive failed boots (watchdog reset, panic, abort), marks image as invalid
- "Failed boot" = doesn't call `esp_ota_mark_app_valid_cancel_rollback()` within timeout
- Rolls back to previous valid image in the other OTA slot

**Key insight**: A single crash may not be enough. The bootloader needs to see a pattern of failure.

## Next Steps

1. Add comprehensive OTA data logging
2. Check if previous firmware is still valid in ota_0
3. Test explicit rollback API
4. Perform full baseline flash with rollback-enabled bootloader
5. Document actual vs. expected rollback threshold
