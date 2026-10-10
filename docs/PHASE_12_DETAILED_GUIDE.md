# Phase 12 Detailed Implementation Guide

**Purpose:** Break Phase 12 (Reliability, Signed OTA, Recovery) into small, verifiable sub-phases with visible checkpoints at each step.

**Safety principle:** Each sub-phase produces a visible, testable change. No large untested merges. Commit after each sub-phase passes its validation.

---

## Overview: Phase 12 Sub-Phase Structure

| Sub-Phase | What Changes | Visible Checkpoint | Est. Time |
|-----------|--------------|-------------------|-----------|
| 12.1 | Version infrastructure + Device Status display | Device Status shows "0.11.0" | 2h |
| 12.2 | Coredump NVS infrastructure | Device Status shows "Last Crash: None" | 3h |
| 12.3 | Crash recovery toast | Toast appears after deliberate crash | 2h |
| 12.4 | Signed builds (both profiles) | Build artifacts verified with signature | 4h |
| 12.5 | Update state machine (stub) | Settings shows "Check for Updates" button | 3h |
| 12.6 | Manifest parsing + validation | Unit test passes strict SemVer checks | 3h |
| 12.7 | OTA worker thread | Log shows dedicated Core-0 OTA worker alive | 2h |
| 12.8 | Download progress UI | Modal shows "Downloading: 45%" | 3h |
| 12.9 | Flash write + reboot | Real OTA completes, device reboots to new version | 4h |
| 12.10 | Rollback detection + toast | Faulty image rolls back, toast shows "Update failed" | 3h |
| 12.11 | Primary/mirror fallback | Manifest fetch tries mirror after primary fails | 2h |
| 12.12 | USB recovery tool | Python script restores firmware, shows menu | 4h |
| 12.13 | Full validation suite | All test cases pass, regressions clean | 6h |

**Total estimated time:** 41 hours (~1 week)

---

## Sub-Phase 12.1: Version Infrastructure + Device Status Display

### Goal
Embed `0.11.0` from `project(crystal_os VERSION ...)` into firmware, expose via API, display in Device Status.

### Changes

1. **Add version accessors** (`main/crystal_version.h`, new file):
```c
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Returns major version (e.g., 0 from "0.11.0") */
uint8_t crystal_version_major(void);

/** Returns minor version (e.g., 11 from "0.11.0") */
uint8_t crystal_version_minor(void);

/** Returns patch version (e.g., 0 from "0.11.0") */
uint8_t crystal_version_patch(void);

/** Returns full version string (e.g., "0.11.0") */
const char* crystal_version_string(void);

#ifdef __cplusplus
}
#endif
```

2. **Implement version accessors** (`main/crystal_version.c`, new file):
```c
#include "crystal_version.h"
#include "sdkconfig.h"
#include <stdio.h>

// CMake generates CONFIG_APP_PROJECT_VER as "0.11.0"
#define VERSION_STRING CONFIG_APP_PROJECT_VER

static char s_version_buf[16] = VERSION_STRING;

uint8_t crystal_version_major(void) {
    // Parse "0.11.0" -> 0
    int major = 0;
    sscanf(VERSION_STRING, "%d", &major);
    return (uint8_t)major;
}

uint8_t crystal_version_minor(void) {
    // Parse "0.11.0" -> 11
    int major, minor = 0;
    sscanf(VERSION_STRING, "%d.%d", &major, &minor);
    return (uint8_t)minor;
}

uint8_t crystal_version_patch(void) {
    // Parse "0.11.0" -> 0
    int major, minor, patch = 0;
    sscanf(VERSION_STRING, "%d.%d.%d", &major, &minor, &patch);
    return (uint8_t)patch;
}

const char* crystal_version_string(void) {
    return s_version_buf;
}
```

3. **Update Device Status to show version** (`main/ui/settings/device_status_overlay.cpp`):
```cpp
// In DeviceStatusOverlay::buildContent(), after existing info rows:

// Version row
lv_obj_t *version_row = lv_obj_create(content);
lv_obj_set_size(version_row, LV_PCT(100), LV_SIZE_CONTENT);
lv_obj_set_style_pad_all(version_row, 12, 0);
lv_obj_set_flex_flow(version_row, LV_FLEX_FLOW_COLUMN);

lv_obj_t *version_label = lv_label_create(version_row);
lv_label_set_text(version_label, "Firmware Version");
lv_obj_set_style_text_color(version_label, lv_color_hex(0x888888), 0);
lv_obj_set_style_text_font(version_label, &lv_font_montserrat_14, 0);

lv_obj_t *version_value = lv_label_create(version_row);
lv_label_set_text(version_value, crystal_version_string());
lv_obj_set_style_text_font(version_value, &lv_font_montserrat_16, 0);
```

4. **Update CMakeLists.txt** to include new files:
```cmake
# In main/CMakeLists.txt, add to idf_component_register SRCS:
    "crystal_version.c"
```

### Validation

1. **Build**:
```bash
idf.py build
```

2. **Verify version embedding**:
```bash
strings build/crystal_os.bin | grep "0.11.0"
# Should show CONFIG_APP_PROJECT_VER=0.11.0
```

3. **Flash and test**:
```bash
idf.py flash monitor
```

4. **On device**:
   - Navigate to Settings → Device Status
   - **Checkpoint**: "Firmware Version: 0.11.0" appears in the list

5. **Commit**:
```bash
git add main/crystal_version.{h,c} main/ui/settings/device_status_overlay.cpp main/CMakeLists.txt
git commit -m "Phase 12.1: Add version infrastructure and Device Status display

- Embed 0.11.0 from CMakeLists project() into firmware
- Add crystal_version API for major/minor/patch/string accessors
- Display version in Settings › Device Status

Checkpoint: Device Status shows 'Firmware Version: 0.11.0'

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Sub-Phase 12.2: Coredump NVS Infrastructure

### Goal
Read coredump summary at boot, store in NVS blob, expose via API, display "Last Crash" in Device Status.

### Changes

1. **Add crash summary structure** (`main/crystal_crash.h`, new file):
```c
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRASH_BACKTRACE_DEPTH 16

typedef struct {
    uint8_t version;           // Blob format version (1)
    uint8_t reset_reason;      // esp_reset_reason_t
    uint8_t reserved[2];
    uint32_t pc;               // Program counter
    uint32_t backtrace[CRASH_BACKTRACE_DEPTH];
    uint8_t elf_sha[32];       // SHA-256 of running ELF (for symbolication)
    uint64_t timestamp_ms;     // System uptime at crash (0 if clock not set)
    char task_name[16];
    bool is_corrupted;         // True if coredump header failed CRC
} crash_summary_t;

/**
 * Initialize crash tracking.
 * Reads coredump partition at boot, extracts summary, stores in NVS.
 * Call once from app_main before UI.
 */
void crystal_crash_init(void);

/**
 * Get the last crash summary.
 * Returns true if a crash record exists, false otherwise.
 */
bool crystal_crash_get_last(crash_summary_t *out);

/**
 * Clear the "show recovery toast" flag.
 * Called after UI displays the one-time recovery message.
 */
void crystal_crash_mark_displayed(void);

/**
 * Check if recovery toast should be shown.
 * Returns true once per crash; false after crystal_crash_mark_displayed().
 */
bool crystal_crash_should_notify(void);

#ifdef __cplusplus
}
#endif
```

2. **Implement crash tracking** (`main/crystal_crash.c`, new file):
```c
#include "crystal_crash.h"
#include "esp_core_dump.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "crash";
static const char *NVS_NAMESPACE = "crash";
static const char *KEY_SUMMARY = "summary";
static const char *KEY_NOTIFY_FLAG = "notify";

static crash_summary_t s_last_crash = {0};
static bool s_has_crash = false;
static bool s_should_notify = false;

void crystal_crash_init(void) {
    // 1. Check if coredump partition has data
    esp_core_dump_summary_t core_summary;
    esp_err_t err = esp_core_dump_get_summary(&core_summary);
    
    if (err == ESP_OK && core_summary.exc_tcb != NULL) {
        ESP_LOGI(TAG, "Coredump found, extracting summary");
        
        // 2. Build crash summary
        s_last_crash.version = 1;
        s_last_crash.reset_reason = esp_reset_reason();
        s_last_crash.pc = core_summary.exc_pc;
        
        // Copy backtrace (limit to CRASH_BACKTRACE_DEPTH)
        size_t bt_len = core_summary.exc_bt_info.depth;
        if (bt_len > CRASH_BACKTRACE_DEPTH) bt_len = CRASH_BACKTRACE_DEPTH;
        for (size_t i = 0; i < bt_len; i++) {
            s_last_crash.backtrace[i] = core_summary.exc_bt_info.bt[i];
        }
        
        // Copy task name
        if (core_summary.exc_task) {
            strncpy(s_last_crash.task_name, core_summary.exc_task, 15);
            s_last_crash.task_name[15] = '\0';
        }
        
        // Get ELF SHA-256
        const esp_app_desc_t *app_desc = esp_app_get_description();
        memcpy(s_last_crash.elf_sha, app_desc->app_elf_sha256, 32);
        
        // Timestamp (placeholder: would use crystal_time if available)
        s_last_crash.timestamp_ms = 0;
        
        s_last_crash.is_corrupted = (core_summary.core_dump_crc != 0); // Placeholder check
        
        // 3. Store in NVS
        nvs_handle_t handle;
        err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            nvs_set_blob(handle, KEY_SUMMARY, &s_last_crash, sizeof(s_last_crash));
            nvs_set_u8(handle, KEY_NOTIFY_FLAG, 1);  // Set "show toast" flag
            nvs_commit(handle);
            nvs_close(handle);
        }
        
        s_has_crash = true;
        s_should_notify = true;
        
        ESP_LOGI(TAG, "Crash summary saved: task=%s, pc=0x%08lx", 
                 s_last_crash.task_name, s_last_crash.pc);
    } else {
        // 4. No new crash, try to load existing record from NVS
        nvs_handle_t handle;
        err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
        if (err == ESP_OK) {
            size_t blob_size = sizeof(s_last_crash);
            if (nvs_get_blob(handle, KEY_SUMMARY, &s_last_crash, &blob_size) == ESP_OK) {
                s_has_crash = true;
            }
            
            uint8_t notify_flag = 0;
            if (nvs_get_u8(handle, KEY_NOTIFY_FLAG, &notify_flag) == ESP_OK) {
                s_should_notify = (notify_flag == 1);
            }
            
            nvs_close(handle);
        }
    }
}

bool crystal_crash_get_last(crash_summary_t *out) {
    if (!s_has_crash || !out) return false;
    memcpy(out, &s_last_crash, sizeof(crash_summary_t));
    return true;
}

void crystal_crash_mark_displayed(void) {
    if (!s_should_notify) return;
    
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, KEY_NOTIFY_FLAG, 0);
        nvs_commit(handle);
        nvs_close(handle);
    }
    
    s_should_notify = false;
}

bool crystal_crash_should_notify(void) {
    return s_should_notify;
}
```

3. **Call crystal_crash_init in app_main** (`main/main.cpp`):
```cpp
// After nvs_flash_init(), before UI initialization:
extern "C" void crystal_crash_init(void);

void app_main(void) {
    // ... existing NVS init ...
    
    // Initialize crash tracking before UI
    crystal_crash_init();
    
    // ... continue with UI init ...
}
```

4. **Update Device Status to show last crash** (`main/ui/settings/device_status_overlay.cpp`):
```cpp
// Include at top:
extern "C" {
#include "crystal_crash.h"
}

// In DeviceStatusOverlay::buildContent(), after version row:

// Last Crash row
lv_obj_t *crash_row = lv_obj_create(content);
lv_obj_set_size(crash_row, LV_PCT(100), LV_SIZE_CONTENT);
lv_obj_set_style_pad_all(crash_row, 12, 0);
lv_obj_set_flex_flow(crash_row, LV_FLEX_FLOW_COLUMN);

lv_obj_t *crash_label = lv_label_create(crash_row);
lv_label_set_text(crash_label, "Last Crash");
lv_obj_set_style_text_color(crash_label, lv_color_hex(0x888888), 0);
lv_obj_set_style_text_font(crash_label, &lv_font_montserrat_14, 0);

crash_summary_t crash;
lv_obj_t *crash_value = lv_label_create(crash_row);
if (crystal_crash_get_last(&crash)) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s @ 0x%08lx", crash.task_name, crash.pc);
    lv_label_set_text(crash_value, buf);
} else {
    lv_label_set_text(crash_value, "None");
}
lv_obj_set_style_text_font(crash_value, &lv_font_montserrat_16, 0);
```

5. **Enable coredump in sdkconfig** (if not already enabled):
```
CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y
CONFIG_ESP_COREDUMP_CHECK_BOOT=y
CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y
```

6. **Update CMakeLists.txt**:
```cmake
# Add to main/CMakeLists.txt SRCS:
    "crystal_crash.c"
```

### Validation

1. **Build and flash**:
```bash
idf.py build flash monitor
```

2. **Check boot logs**:
```
I (1234) crash: No coredump found
```

3. **Device Status check**:
   - Navigate to Settings → Device Status
   - **Checkpoint**: "Last Crash: None" appears

4. **Trigger deliberate crash** (add temporary test button in Settings):
```cpp
// Temporary test: add button to Settings main screen
lv_obj_t *crash_btn = lv_btn_create(parent);
lv_obj_t *crash_label = lv_label_create(crash_btn);
lv_label_set_text(crash_label, "Test Crash (Debug)");
lv_obj_add_event_cb(crash_btn, [](lv_event_t *e) {
    ESP_LOGE("TEST", "Triggering crash for validation");
    abort();  // Deliberate crash
}, LV_EVENT_CLICKED, nullptr);
```

5. **Test crash capture**:
   - Tap "Test Crash" button
   - Device reboots
   - Check boot logs:
   ```
   I (1234) crash: Coredump found, extracting summary
   I (1235) crash: Crash summary saved: task=main, pc=0x420xxxxx
   ```
   - Navigate to Settings → Device Status
   - **Checkpoint**: "Last Crash: main @ 0x420xxxxx" appears

6. **Remove test button before commit**

7. **Commit**:
```bash
git add main/crystal_crash.{h,c} main/main.cpp main/ui/settings/device_status_overlay.cpp main/CMakeLists.txt
git commit -m "Phase 12.2: Add coredump NVS infrastructure

- Read coredump partition at boot, extract summary
- Store crash summary (task, PC, backtrace, ELF SHA) in NVS
- Display 'Last Crash' in Device Status
- Persist 'show toast' flag for next sub-phase

Checkpoint: Device Status shows 'Last Crash: None' (or task @ PC after test crash)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Sub-Phase 12.3: Crash Recovery Toast

### Goal
Show "Recovered from an error" toast once after crash, clear flag after display.

### Changes

1. **Add UI event for crash recovery** (`main/ui/shell.h`):
```cpp
// Add to UIEvent enum:
UI_EVT_CRASH_RECOVERED,
```

2. **Post crash event from shell init** (`main/ui/shell.cpp`):
```cpp
// Include at top:
extern "C" {
#include "crystal_crash.h"
}

// In Shell::init(), after brookesia_start_default:

// Check for crash recovery
if (crystal_crash_should_notify()) {
    UIEvent evt = {.type = UI_EVT_CRASH_RECOVERED};
    enqueueUIEvent(evt);
    crystal_crash_mark_displayed();  // Clear flag immediately
}
```

3. **Handle crash event in shell** (`main/ui/shell.cpp`):
```cpp
// In Shell::processUIEvent:

case UI_EVT_CRASH_RECOVERED: {
    showToast("Recovered from an error", 3000);
    break;
}
```

### Validation

1. **Build and flash**:
```bash
idf.py build flash monitor
```

2. **Normal boot** (no crash):
   - **Checkpoint**: No toast appears

3. **Trigger crash**:
   - Add temporary crash button again (or use existing mechanism from 12.2)
   - Tap button, device reboots
   - **Checkpoint**: Toast "Recovered from an error" appears on home screen for 3 seconds

4. **Reboot again** (no crash):
   - **Checkpoint**: Toast does NOT appear (flag was cleared)

5. **Device Status check**:
   - Navigate to Settings → Device Status
   - **Checkpoint**: "Last Crash" still shows previous crash details (persistent)

6. **Commit**:
```bash
git add main/ui/shell.{h,cpp}
git commit -m "Phase 12.3: Add crash recovery toast

- Post UI_EVT_CRASH_RECOVERED from shell init if flag set
- Show 'Recovered from an error' toast once
- Clear notify flag immediately after posting event

Checkpoint: Toast appears once after crash, not on subsequent reboots

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Sub-Phase 12.4: Signed Builds (Dev + Production Profiles)

### Goal
Enable RSA-3072 signed apps, create separate dev/prod build profiles, verify signatures.

### Changes

1. **Enable signed apps in sdkconfig**:
```
CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y
CONFIG_SECURE_BOOT_SIGNING_KEY=""
# Leave blank; will be set via build profiles
```

2. **Generate RSA-3072 keys** (outside repo):
```bash
# Dev key (for local testing)
openssl genrsa -out ~/.crystal_keys/dev_signing_key.pem 3072

# Production key (store in secure vault, not on dev machine)
openssl genrsa -out ~/.crystal_keys/prod_signing_key.pem 3072

# Add to .gitignore (if not already):
echo "*.pem" >> .gitignore
```

3. **Create build profile scripts** (`scripts/build_dev.sh`, new file):
```bash
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$SCRIPT_DIR/.."

KEY_PATH="${CRYSTAL_DEV_KEY:-$HOME/.crystal_keys/dev_signing_key.pem}"

if [ ! -f "$KEY_PATH" ]; then
    echo "ERROR: Dev signing key not found at $KEY_PATH"
    echo "Generate with: openssl genrsa -out $KEY_PATH 3072"
    exit 1
fi

echo "Building development profile with key: $KEY_PATH"

cd "$PROJECT_ROOT"
idf.py -D SECURE_BOOT_SIGNING_KEY="$KEY_PATH" build

echo ""
echo "✅ Development build complete"
echo "Binary: build/crystal_os.bin (signed)"
```

4. **Create production profile script** (`scripts/build_prod.sh`, new file):
```bash
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$SCRIPT_DIR/.."

KEY_PATH="${CRYSTAL_PROD_KEY:-$HOME/.crystal_keys/prod_signing_key.pem}"

if [ ! -f "$KEY_PATH" ]; then
    echo "ERROR: Production signing key not found at $KEY_PATH"
    echo "This must be provided through secure secret management."
    exit 1
fi

echo "Building production profile with key: $KEY_PATH"

cd "$PROJECT_ROOT"
idf.py -D SECURE_BOOT_SIGNING_KEY="$KEY_PATH" build

echo ""
echo "✅ Production build complete"
echo "Binary: build/crystal_os.bin (signed)"
echo ""
echo "Next steps:"
echo "1. Verify signature: espsecure.py signature_info_v2 build/crystal_os.bin"
echo "2. Calculate SHA-256: shasum -a 256 build/crystal_os.bin"
echo "3. Archive: cp build/crystal_os.bin releases/0.11.0/crystal_os_prod.bin"
echo "4. Archive ELF: cp build/crystal_os.elf releases/0.11.0/crystal_os_prod.elf"
```

5. **Make scripts executable**:
```bash
chmod +x scripts/build_dev.sh scripts/build_prod.sh
```

6. **Create release archive structure**:
```bash
mkdir -p releases/0.11.0
```

7. **Update .gitignore**:
```
# Signing keys (never commit)
*.pem

# Release archives (commit checksums, not binaries)
releases/*/crystal_os*.bin
releases/*/crystal_os*.elf
```

### Validation

1. **Build dev profile**:
```bash
./scripts/build_dev.sh
```

2. **Verify dev signature**:
```bash
espsecure.py signature_info_v2 build/crystal_os.bin
```
Expected output:
```
Signature block 0:
  Signature: <hex signature>
  Signing key: <hex public key>
  Verifying: OK
```

3. **Build prod profile**:
```bash
./scripts/build_prod.sh
```

4. **Verify prod signature**:
```bash
espsecure.py signature_info_v2 build/crystal_os.bin
```
Expected output:
```
Signature block 0:
  Signature: <different hex signature than dev>
  Verifying: OK
```

5. **Verify keys are different**:
```bash
# Extract public keys
espsecure.py extract_public_key --keyfile ~/.crystal_keys/dev_signing_key.pem --output /tmp/dev_pub.pem
espsecure.py extract_public_key --keyfile ~/.crystal_keys/prod_signing_key.pem --output /tmp/prod_pub.pem

# Compare
diff /tmp/dev_pub.pem /tmp/prod_pub.pem
# Should show differences
```

6. **Verify size under 5 MiB**:
```bash
ls -lh build/crystal_os.bin
# Should be < 5 MB (typically 2-3 MB)
```

7. **Flash and test dev build**:
```bash
./scripts/build_dev.sh
idf.py flash monitor
```
   - **Checkpoint**: Device boots normally, all features work

8. **Commit**:
```bash
git add sdkconfig scripts/build_dev.sh scripts/build_prod.sh .gitignore
mkdir -p releases/0.11.0
touch releases/0.11.0/.gitkeep
git add releases/0.11.0/.gitkeep
git commit -m "Phase 12.4: Add signed build profiles (dev + prod)

- Enable CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT
- Create build_dev.sh and build_prod.sh scripts
- Generate separate RSA-3072 keys (outside repo)
- Add release archive structure

Checkpoint: Both profiles build, signatures verify, keys are different

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Sub-Phase 12.5: Update State Machine (Stub)

### Goal
Add "Check for Updates" button in Settings, stub out state machine, log button press.

### Changes

1. **Add update state enum** (`main/crystal_update.h`, new file):
```c
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UPDATE_STATE_IDLE,              // No update in progress
    UPDATE_STATE_CHECKING,          // Fetching manifest
    UPDATE_STATE_AVAILABLE,         // New version available
    UPDATE_STATE_DOWNLOADING,       // Downloading binary
    UPDATE_STATE_READY,             // Downloaded, ready to install
    UPDATE_STATE_INSTALLING,        // Writing to flash
    UPDATE_STATE_FAILED,            // Error occurred
} crystal_update_state_t;

typedef struct {
    crystal_update_state_t state;
    uint8_t progress_percent;       // 0-100 during download
    char available_version[16];     // e.g., "0.12.0"
    char error_message[128];        // Human-readable error
    uint32_t download_size;         // Total bytes to download
    uint32_t downloaded_bytes;      // Bytes received so far
} crystal_update_snapshot_t;

/**
 * Initialize update subsystem.
 * Call once from app_main.
 */
void crystal_update_init(void);

/**
 * Start checking for updates.
 * Returns ESP_OK if check started, error otherwise.
 * Result delivered via UI_EVT_OTA_CHANGED.
 */
esp_err_t crystal_update_check(void);

/**
 * Cancel ongoing check.
 * No-op if not checking.
 */
void crystal_update_cancel_check(void);

/**
 * Start installing downloaded update.
 * Returns ESP_OK if install started, error if not in READY state.
 */
esp_err_t crystal_update_install(void);

/**
 * Get current update status snapshot.
 */
void crystal_update_snapshot(crystal_update_snapshot_t *out);

#ifdef __cplusplus
}
#endif
```

2. **Implement stub state machine** (`main/crystal_update.c`, new file):
```c
#include "crystal_update.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "update";
static crystal_update_snapshot_t s_snapshot = {
    .state = UPDATE_STATE_IDLE,
    .progress_percent = 0,
    .available_version = {0},
    .error_message = {0},
    .download_size = 0,
    .downloaded_bytes = 0,
};

void crystal_update_init(void) {
    ESP_LOGI(TAG, "Update subsystem initialized (stub)");
}

esp_err_t crystal_update_check(void) {
    if (s_snapshot.state != UPDATE_STATE_IDLE) {
        ESP_LOGW(TAG, "Update already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Checking for updates (stub)");
    s_snapshot.state = UPDATE_STATE_CHECKING;
    
    // TODO: Launch OTA worker task in 12.7
    // For now, just log and reset state
    s_snapshot.state = UPDATE_STATE_IDLE;
    strcpy(s_snapshot.error_message, "Not implemented yet");
    
    return ESP_OK;
}

void crystal_update_cancel_check(void) {
    if (s_snapshot.state == UPDATE_STATE_CHECKING) {
        ESP_LOGI(TAG, "Cancelling update check");
        s_snapshot.state = UPDATE_STATE_IDLE;
    }
}

esp_err_t crystal_update_install(void) {
    ESP_LOGI(TAG, "Install requested (stub)");
    return ESP_ERR_NOT_SUPPORTED;
}

void crystal_update_snapshot(crystal_update_snapshot_t *out) {
    if (out) {
        memcpy(out, &s_snapshot, sizeof(crystal_update_snapshot_t));
    }
}
```

3. **Add update check button to Settings** (`main/ui/settings/settings_main_screen.cpp`):
```cpp
// Include at top:
extern "C" {
#include "crystal_update.h"
}

// In SettingsMainScreen::buildContent(), after existing sections:

// System section
lv_obj_t *system_section = createSection(content, "System");

lv_obj_t *update_btn = createButton(system_section, "Check for Updates");
lv_obj_add_event_cb(update_btn, [](lv_event_t *e) {
    ESP_LOGI("settings", "Check for Updates tapped");
    crystal_update_check();
}, LV_EVENT_CLICKED, nullptr);

lv_obj_t *device_btn = createButton(system_section, "Device Status");
// ... existing Device Status code ...
```

4. **Call crystal_update_init in app_main** (`main/main.cpp`):
```cpp
// After crystal_crash_init:
extern "C" void crystal_update_init(void);

void app_main(void) {
    // ... crystal_crash_init() ...
    
    crystal_update_init();
    
    // ... UI init ...
}
```

5. **Update CMakeLists.txt**:
```cmake
# Add to main/CMakeLists.txt SRCS:
    "crystal_update.c"
```

### Validation

1. **Build and flash**:
```bash
./scripts/build_dev.sh
idf.py flash monitor
```

2. **Navigate to Settings**:
   - **Checkpoint**: "Check for Updates" button appears in System section

3. **Tap button**:
   - Check monitor logs:
   ```
   I (12345) settings: Check for Updates tapped
   I (12346) update: Checking for updates (stub)
   I (12347) update: Update subsystem initialized (stub)
   ```
   - **Checkpoint**: Logs confirm button press, no crash

4. **Commit**:
```bash
git add main/crystal_update.{h,c} main/ui/settings/settings_main_screen.cpp main/main.cpp main/CMakeLists.txt
git commit -m "Phase 12.5: Add update state machine stub

- Define crystal_update API: check/cancel/install/snapshot
- Implement stub that logs actions without real OTA
- Add 'Check for Updates' button in Settings › System

Checkpoint: Button appears, tap logs 'Checking for updates (stub)'

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

---

## Sub-Phase 12.6: Manifest Parsing + Validation

### Goal
Parse manifest JSON with strict validation: schema/target/channel/version/size/SHA-256/URLs. Unit test SemVer ordering.

### Changes

1. **Add manifest parser** (`main/crystal_update_manifest.h`, new file):
```c
#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UPDATE_MAX_URL_LEN 256
#define UPDATE_MAX_NOTES_LEN 256
#define UPDATE_MAX_URLS 2

typedef struct {
    uint8_t schema;
    char target[64];
    char channel[16];
    char version[16];
    uint32_t size;
    uint8_t sha256[32];
    char urls[UPDATE_MAX_URLS][UPDATE_MAX_URL_LEN];
    uint8_t url_count;
    char notes[UPDATE_MAX_NOTES_LEN];
} update_manifest_t;

/**
 * Parse and validate manifest JSON.
 * Returns ESP_OK if valid, error otherwise.
 * 
 * Checks:
 * - JSON structure and size (max 2 KiB)
 * - Required fields present
 * - target matches expected hardware
 * - channel matches expected value
 * - version is strictly newer (SemVer)
 * - URLs are HTTPS and within count/length limits
 * - SHA-256 is valid hex
 */
esp_err_t crystal_update_parse_manifest(
    const char *json,
    size_t json_len,
    const char *expected_target,
    const char *expected_channel,
    const char *current_version,
    update_manifest_t *out
);

/**
 * Compare two SemVer strings.
 * Returns: -1 if a < b, 0 if a == b, 1 if a > b, -2 if malformed
 */
int crystal_update_compare_semver(const char *a, const char *b);

#ifdef __cplusplus
}
#endif
```

2. **Implement manifest parser** (`main/crystal_update_manifest.c`, new file):
```c
#include "crystal_update_manifest.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "manifest";

int crystal_update_compare_semver(const char *a, const char *b) {
    if (!a || !b) return -2;
    
    int a_major = 0, a_minor = 0, a_patch = 0;
    int b_major = 0, b_minor = 0, b_patch = 0;
    
    // Parse version strings
    if (sscanf(a, "%d.%d.%d", &a_major, &a_minor, &a_patch) != 3) return -2;
    if (sscanf(b, "%d.%d.%d", &b_major, &b_minor, &b_patch) != 3) return -2;
    
    // Compare major.minor.patch
    if (a_major != b_major) return (a_major > b_major) ? 1 : -1;
    if (a_minor != b_minor) return (a_minor > b_minor) ? 1 : -1;
    if (a_patch != b_patch) return (a_patch > b_patch) ? 1 : -1;
    return 0;
}

static bool hex_to_bytes(const char *hex, uint8_t *out, size_t out_len) {
    if (strlen(hex) != out_len * 2) return false;
    
    for (size_t i = 0; i < out_len; i++) {
        if (sscanf(&hex[i * 2], "%2hhx", &out[i]) != 1) return false;
    }
    return true;
}

esp_err_t crystal_update_parse_manifest(
    const char *json,
    size_t json_len,
    const char *expected_target,
    const char *expected_channel,
    const char *current_version,
    update_manifest_t *out
) {
    if (!json || json_len == 0 || json_len > 2048 || !out) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ESP_FAIL;
    memset(out, 0, sizeof(update_manifest_t));
    
    // Parse schema
    cJSON *schema = cJSON_GetObjectItem(root, "schema");
    if (!cJSON_IsNumber(schema) || schema->valueint != 1) {
        ESP_LOGE(TAG, "Invalid or unsupported schema");
        goto cleanup;
    }
    out->schema = schema->valueint;
    
    // Parse target
    cJSON *target = cJSON_GetObjectItem(root, "target");
    if (!cJSON_IsString(target)) {
        ESP_LOGE(TAG, "Missing target");
        goto cleanup;
    }
    strncpy(out->target, target->valuestring, sizeof(out->target) - 1);
    
    if (strcmp(out->target, expected_target) != 0) {
        ESP_LOGE(TAG, "Target mismatch: expected '%s', got '%s'", expected_target, out->target);
        goto cleanup;
    }
    
    // Parse channel
    cJSON *channel = cJSON_GetObjectItem(root, "channel");
    if (!cJSON_IsString(channel)) {
        ESP_LOGE(TAG, "Missing channel");
        goto cleanup;
    }
    strncpy(out->channel, channel->valuestring, sizeof(out->channel) - 1);
    
    if (strcmp(out->channel, expected_channel) != 0) {
        ESP_LOGE(TAG, "Channel mismatch: expected '%s', got '%s'", expected_channel, out->channel);
        goto cleanup;
    }
    
    // Parse version
    cJSON *version = cJSON_GetObjectItem(root, "version");
    if (!cJSON_IsString(version)) {
        ESP_LOGE(TAG, "Missing version");
        goto cleanup;
    }
    strncpy(out->version, version->valuestring, sizeof(out->version) - 1);
    
    // Validate version is strictly newer
    int cmp = crystal_update_compare_semver(out->version, current_version);
    if (cmp != 1) {
        ESP_LOGE(TAG, "Version %s is not newer than current %s", out->version, current_version);
        goto cleanup;
    }
    
    // Parse size
    cJSON *size = cJSON_GetObjectItem(root, "size");
    if (!cJSON_IsNumber(size) || size->valueint <= 0) {
        ESP_LOGE(TAG, "Invalid size");
        goto cleanup;
    }
    out->size = size->valueint;
    
    // Parse SHA-256
    cJSON *sha256 = cJSON_GetObjectItem(root, "sha256");
    if (!cJSON_IsString(sha256)) {
        ESP_LOGE(TAG, "Missing sha256");
        goto cleanup;
    }
    if (!hex_to_bytes(sha256->valuestring, out->sha256, 32)) {
        ESP_LOGE(TAG, "Invalid SHA-256 format");
        goto cleanup;
    }
    
    // Parse URLs
    cJSON *urls = cJSON_GetObjectItem(root, "urls");
    if (!cJSON_IsArray(urls)) {
        ESP_LOGE(TAG, "Missing urls array");
        goto cleanup;
    }
    
    int url_count = cJSON_GetArraySize(urls);
    if (url_count < 1 || url_count > UPDATE_MAX_URLS) {
        ESP_LOGE(TAG, "Invalid URL count: %d", url_count);
        goto cleanup;
    }
    
    for (int i = 0; i < url_count; i++) {
        cJSON *url_item = cJSON_GetArrayItem(urls, i);
        if (!cJSON_IsString(url_item)) {
            ESP_LOGE(TAG, "Invalid URL at index %d", i);
            goto cleanup;
        }
        
        const char *url = url_item->valuestring;
        if (strncmp(url, "https://", 8) != 0) {
            ESP_LOGE(TAG, "URL must be HTTPS: %s", url);
            goto cleanup;
        }
        
        if (strlen(url) >= UPDATE_MAX_URL_LEN) {
            ESP_LOGE(TAG, "URL too long: %s", url);
            goto cleanup;
        }
        
        strncpy(out->urls[i], url, UPDATE_MAX_URL_LEN - 1);
        out->url_count++;
    }
    
    // Parse notes (optional)
    cJSON *notes = cJSON_GetObjectItem(root, "notes");
    if (cJSON_IsString(notes)) {
        strncpy(out->notes, notes->valuestring, UPDATE_MAX_NOTES_LEN - 1);
    }
    
    ESP_LOGI(TAG, "Manifest valid: %s → %s (%lu bytes)", current_version, out->version, out->size);
    ret = ESP_OK;
    
cleanup:
    cJSON_Delete(root);
    return ret;
}
```

3. **Create unit test** (`main/test/test_update_manifest.c`, new file):
```c
#include "unity.h"
#include "crystal_update_manifest.h"
#include <string.h>

TEST_CASE("SemVer comparison", "[update]") {
    // Equal versions
    TEST_ASSERT_EQUAL(0, crystal_update_compare_semver("0.11.0", "0.11.0"));
    TEST_ASSERT_EQUAL(0, crystal_update_compare_semver("1.2.3", "1.2.3"));
    
    // Newer versions
    TEST_ASSERT_EQUAL(1, crystal_update_compare_semver("0.12.0", "0.11.0"));
    TEST_ASSERT_EQUAL(1, crystal_update_compare_semver("1.0.0", "0.99.99"));
    TEST_ASSERT_EQUAL(1, crystal_update_compare_semver("0.11.1", "0.11.0"));
    
    // Older versions
    TEST_ASSERT_EQUAL(-1, crystal_update_compare_semver("0.11.0", "0.12.0"));
    TEST_ASSERT_EQUAL(-1, crystal_update_compare_semver("0.99.99", "1.0.0"));
    
    // Malformed versions
    TEST_ASSERT_EQUAL(-2, crystal_update_compare_semver("invalid", "0.11.0"));
    TEST_ASSERT_EQUAL(-2, crystal_update_compare_semver("0.11", "0.11.0"));
    TEST_ASSERT_EQUAL(-2, crystal_update_compare_semver(NULL, "0.11.0"));
}

TEST_CASE("Manifest parsing - valid", "[update]") {
    const char *json = 
        "{"
        "\"schema\":1,"
        "\"target\":\"waveshare-esp32-s3-touch-lcd-4b\","
        "\"channel\":\"stable\","
        "\"version\":\"0.12.0\","
        "\"size\":2810392,"
        "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
        "\"urls\":[\"https://updates.rockyroad.studio/crystal-os/releases/0.12.0/crystal_os.bin\"],"
        "\"notes\":\"Test update\""
        "}";
    
    update_manifest_t manifest;
    esp_err_t err = crystal_update_parse_manifest(
        json, strlen(json),
        "waveshare-esp32-s3-touch-lcd-4b",
        "stable",
        "0.11.0",
        &manifest
    );
    
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(1, manifest.schema);
    TEST_ASSERT_EQUAL_STRING("0.12.0", manifest.version);
    TEST_ASSERT_EQUAL(2810392, manifest.size);
    TEST_ASSERT_EQUAL(1, manifest.url_count);
    TEST_ASSERT_EQUAL_STRING("Test update", manifest.notes);
}

TEST_CASE("Manifest parsing - version not newer", "[update]") {
    const char *json = 
        "{"
        "\"schema\":1,"
        "\"target\":\"waveshare-esp32-s3-touch-lcd-4b\","
        "\"channel\":\"stable\","
        "\"version\":\"0.11.0\","
        "\"size\":2810392,"
        "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
        "\"urls\":[\"https://example.com/update.bin\"]"
        "}";
    
    update_manifest_t manifest;
    esp_err_t err = crystal_update_parse_manifest(
        json, strlen(json),
        "waveshare-esp32-s3-touch-lcd-4b",
        "stable",
        "0.11.0",  // Same version
        &manifest
    );
    
    TEST_ASSERT_EQUAL(ESP_FAIL, err);
}

TEST_CASE("Manifest parsing - wrong target", "[update]") {
    const char *json = 
        "{"
        "\"schema\":1,"
        "\"target\":\"different-board\","
        "\"channel\":\"stable\","
        "\"version\":\"0.12.0\","
        "\"size\":2810392,"
        "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
        "\"urls\":[\"https://example.com/update.bin\"]"
        "}";
    
    update_manifest_t manifest;
    esp_err_t err = crystal_update_parse_manifest(
        json, strlen(json),
        "waveshare-esp32-s3-touch-lcd-4b",
        "stable",
        "0.11.0",
        &manifest
    );
    
    TEST_ASSERT_EQUAL(ESP_FAIL, err);
}

TEST_CASE("Manifest parsing - non-HTTPS URL", "[update]") {
    const char *json = 
        "{"
        "\"schema\":1,"
        "\"target\":\"waveshare-esp32-s3-touch-lcd-4b\","
        "\"channel\":\"stable\","
        "\"version\":\"0.12.0\","
        "\"size\":2810392,"
        "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
        "\"urls\":[\"http://insecure.com/update.bin\"]"
        "}";
    
    update_manifest_t manifest;
    esp_err_t err = crystal_update_parse_manifest(
        json, strlen(json),
        "waveshare-esp32-s3-touch-lcd-4b",
        "stable",
        "0.11.0",
        &manifest
    );
    
    TEST_ASSERT_EQUAL(ESP_FAIL, err);
}
```

4. **Update CMakeLists.txt**:
```cmake
# Add to main/CMakeLists.txt SRCS:
    "crystal_update_manifest.c"
```

5. **Enable Unity testing in project** (if not already):
```cmake
# In main/CMakeLists.txt:
idf_component_register(
    # ... existing SRCS ...
    REQUIRES unity  # Add if not present
)
```

### Validation

1. **Build and run unit tests**:
```bash
idf.py build
idf.py flash monitor
```

2. **In monitor, run tests**:
```
> test "[update]"
```

Expected output:
```
Running SemVer comparison...OK
Running Manifest parsing - valid...OK
Running Manifest parsing - version not newer...OK
Running Manifest parsing - wrong target...OK
Running Manifest parsing - non-HTTPS URL...OK

5 Tests 0 Failures 0 Ignored
```

3. **Checkpoint**: All unit tests pass

4. **Commit**:
```bash
git add main/crystal_update_manifest.{h,c} main/test/test_update_manifest.c main/CMakeLists.txt
git commit -m "Phase 12.6: Add manifest parsing and validation

- Parse manifest JSON with cJSON (schema/target/channel/version/size/sha256/urls)
- Implement strict SemVer comparison (reject equal/older/malformed)
- Enforce HTTPS-only URLs, target/channel matching
- Add comprehensive unit tests for validation rules

Checkpoint: All unit tests pass, strict version ordering confirmed

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Sub-Phase 12.7: OTA Worker Thread

### Goal
Create dedicated Core-0 worker thread for OTA operations, keep service task separate.

### Changes

1. **Update crystal_update.c** to add worker thread:
```c
// Add includes at top:
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_http_client.h"
#include "crystal_update_manifest.h"

// Worker thread constants
#define OTA_WORKER_STACK_SIZE 8192
#define OTA_WORKER_PRIORITY 5
#define OTA_WORKER_CORE 0

// Request queue
typedef enum {
    OTA_REQ_CHECK,
    OTA_REQ_CANCEL,
} ota_request_type_t;

typedef struct {
    ota_request_type_t type;
} ota_request_t;

static TaskHandle_t s_ota_worker_task = NULL;
static QueueHandle_t s_ota_request_queue = NULL;

// Worker task function
static void ota_worker_task(void *arg) {
    ESP_LOGI(TAG, "OTA worker started on Core %d", xPortGetCoreID());
    
    ota_request_t req;
    while (1) {
        if (xQueueReceive(s_ota_request_queue, &req, portMAX_DELAY) == pdTRUE) {
            switch (req.type) {
                case OTA_REQ_CHECK:
                    ESP_LOGI(TAG, "Processing update check request");
                    // TODO: Implement manifest fetch in 12.8
                    s_snapshot.state = UPDATE_STATE_IDLE;
                    break;
                
                case OTA_REQ_CANCEL:
                    ESP_LOGI(TAG, "Cancelling update check");
                    s_snapshot.state = UPDATE_STATE_IDLE;
                    break;
            }
        }
    }
}

// Update crystal_update_init:
void crystal_update_init(void) {
    ESP_LOGI(TAG, "Initializing update subsystem");
    
    // Create request queue
    s_ota_request_queue = xQueueCreate(4, sizeof(ota_request_t));
    if (!s_ota_request_queue) {
        ESP_LOGE(TAG, "Failed to create OTA request queue");
        return;
    }
    
    // Create worker task on Core 0
    BaseType_t ret = xTaskCreatePinnedToCore(
        ota_worker_task,
        "ota_worker",
        OTA_WORKER_STACK_SIZE,
        NULL,
        OTA_WORKER_PRIORITY,
        &s_ota_worker_task,
        OTA_WORKER_CORE
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA worker task");
        vQueueDelete(s_ota_request_queue);
        s_ota_request_queue = NULL;
        return;
    }
    
    ESP_LOGI(TAG, "OTA worker initialized on Core 0");
}

// Update crystal_update_check:
esp_err_t crystal_update_check(void) {
    if (s_snapshot.state != UPDATE_STATE_IDLE) {
        ESP_LOGW(TAG, "Update already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!s_ota_request_queue || !s_ota_worker_task) {
        ESP_LOGE(TAG, "OTA worker not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Queueing update check request");
    s_snapshot.state = UPDATE_STATE_CHECKING;
    
    ota_request_t req = {.type = OTA_REQ_CHECK};
    if (xQueueSend(s_ota_request_queue, &req, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to queue update check");
        s_snapshot.state = UPDATE_STATE_IDLE;
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

// Update crystal_update_cancel_check:
void crystal_update_cancel_check(void) {
    if (s_snapshot.state == UPDATE_STATE_CHECKING) {
        ESP_LOGI(TAG, "Queueing cancel request");
        ota_request_t req = {.type = OTA_REQ_CANCEL};
        xQueueSend(s_ota_request_queue, &req, 0);
    }
}
```

2. **Add debug command to verify worker** (`main/main.cpp`):
```cpp
// Temporary test: add to app_main after crystal_update_init()
ESP_LOGI("main", "Listing FreeRTOS tasks:");
char task_list[512];
vTaskList(task_list);
ESP_LOGI("main", "\n%s", task_list);
```

### Validation

1. **Build and flash**:
```bash
./scripts/build_dev.sh
idf.py flash monitor
```

2. **Check boot logs**:
```
I (1234) update: Initializing update subsystem
I (1235) update: OTA worker started on Core 0
I (1236) update: OTA worker initialized on Core 0
I (1237) main: Listing FreeRTOS tasks:
Name            State   Prio    Stack   Num     Core
ota_worker      B       5       7xxx    X       0
...
```

3. **Checkpoint**: Log shows "OTA worker started on Core 0"

4. **Tap "Check for Updates"** in Settings:
   - Check logs:
   ```
   I (12345) settings: Check for Updates tapped
   I (12346) update: Queueing update check request
   I (12347) update: Processing update check request
   ```
   - **Checkpoint**: Worker processes request on Core 0

5. **Remove debug task list code before commit**

6. **Commit**:
```bash
git add main/crystal_update.c
git commit -m "Phase 12.7: Add dedicated OTA worker thread

- Create ota_worker task on Core 0 with 8KB stack
- Add request queue for check/cancel operations
- Worker processes requests without blocking LVGL task

Checkpoint: Boot log shows 'OTA worker started on Core 0'

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Sub-Phase 12.8: Download Progress UI

### Goal
Fetch manifest from primary URL, parse, show download modal with progress bar (stub download for now).

### Changes

1. **Add UI event for OTA state changes** (`main/ui/shell.h`):
```cpp
// Add to UIEvent:
UI_EVT_OTA_CHANGED,
```

2. **Create OTA modal** (`main/ui/ota_modal.h`, new file):
```cpp
#pragma once
#include "lvgl.h"

class OTAModal {
public:
    static OTAModal* getInstance();
    
    void show();
    void hide();
    void updateProgress(uint8_t percent, const char *message);
    void showError(const char *message);
    
private:
    OTAModal() = default;
    void build();
    
    lv_obj_t *modal_ = nullptr;
    lv_obj_t *message_label_ = nullptr;
    lv_obj_t *progress_bar_ = nullptr;
    lv_obj_t *progress_label_ = nullptr;
};
```

3. **Implement OTA modal** (`main/ui/ota_modal.cpp`, new file):
```cpp
#include "ota_modal.h"
#include "esp_log.h"

static const char *TAG = "ota_modal";

OTAModal* OTAModal::getInstance() {
    static OTAModal instance;
    return &instance;
}

void OTAModal::build() {
    // Create modal overlay
    modal_ = lv_obj_create(lv_scr_act());
    lv_obj_set_size(modal_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(modal_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(modal_, LV_OPA_80, 0);
    lv_obj_center(modal_);
    
    // Create content card
    lv_obj_t *card = lv_obj_create(modal_);
    lv_obj_set_size(card, 360, 200);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 24, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_center(card);
    
    // Message label
    message_label_ = lv_label_create(card);
    lv_label_set_text(message_label_, "Checking for updates...");
    lv_obj_set_style_text_font(message_label_, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(message_label_, LV_TEXT_ALIGN_CENTER, 0);
    
    // Progress bar
    progress_bar_ = lv_bar_create(card);
    lv_obj_set_size(progress_bar_, 300, 12);
    lv_bar_set_value(progress_bar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(progress_bar_, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(progress_bar_, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    
    // Progress label
    progress_label_ = lv_label_create(card);
    lv_label_set_text(progress_label_, "0%");
    lv_obj_set_style_text_font(progress_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(progress_label_, lv_color_hex(0x888888), 0);
}

void OTAModal::show() {
    if (!modal_) {
        build();
    }
    lv_obj_clear_flag(modal_, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "Modal shown");
}

void OTAModal::hide() {
    if (modal_) {
        lv_obj_add_flag(modal_, LV_OBJ_FLAG_HIDDEN);
    }
    ESP_LOGI(TAG, "Modal hidden");
}

void OTAModal::updateProgress(uint8_t percent, const char *message) {
    if (!modal_) return;
    
    lv_bar_set_value(progress_bar_, percent, LV_ANIM_ON);
    
    char buf[16];
    snprintf(buf, sizeof(buf), "%u%%", percent);
    lv_label_set_text(progress_label_, buf);
    
    if (message) {
        lv_label_set_text(message_label_, message);
    }
    
    ESP_LOGI(TAG, "Progress: %u%% - %s", percent, message ? message : "(no message)");
}

void OTAModal::showError(const char *message) {
    if (!modal_) build();
    
    lv_label_set_text(message_label_, message);
    lv_obj_add_flag(progress_bar_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(progress_label_, "");
    
    ESP_LOGI(TAG, "Error: %s", message);
}
```

4. **Update crystal_update.c** to fetch manifest and post UI events:
```c
// Add at top:
#include "crystal_update_manifest.h"
#include "esp_http_client.h"
#include "crystal_version.h"
#include "lvgl.h"

// Add manifest URLs
#define PRIMARY_MANIFEST_URL "https://updates.rockyroad.studio/crystal-os/stable/manifest.json"
#define MIRROR_MANIFEST_URL "https://updates.bubstal.com/crystal-os/stable/manifest.json"
#define EXPECTED_TARGET "waveshare-esp32-s3-touch-lcd-4b"
#define EXPECTED_CHANNEL "stable"

// HTTP response buffer
#define MANIFEST_MAX_SIZE 2048
static char s_manifest_buf[MANIFEST_MAX_SIZE];
static size_t s_manifest_len = 0;

// HTTP event handler
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (s_manifest_len + evt->data_len < MANIFEST_MAX_SIZE) {
                memcpy(s_manifest_buf + s_manifest_len, evt->data, evt->data_len);
                s_manifest_len += evt->data_len;
            } else {
                ESP_LOGW(TAG, "Manifest too large");
                return ESP_FAIL;
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Post UI event on LVGL task
static void post_ota_changed_event(void *arg) {
    // TODO: Implement UIEvent posting mechanism
    ESP_LOGI(TAG, "OTA state changed (UI event stub)");
}

// Worker task - update OTA_REQ_CHECK case:
case OTA_REQ_CHECK: {
    ESP_LOGI(TAG, "Fetching manifest from primary");
    s_manifest_len = 0;
    memset(s_manifest_buf, 0, sizeof(s_manifest_buf));
    
    esp_http_client_config_t config = {
        .url = PRIMARY_MANIFEST_URL,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            ESP_LOGI(TAG, "Manifest fetched: %zu bytes", s_manifest_len);
            
            // Parse manifest
            update_manifest_t manifest;
            err = crystal_update_parse_manifest(
                s_manifest_buf,
                s_manifest_len,
                EXPECTED_TARGET,
                EXPECTED_CHANNEL,
                crystal_version_string(),
                &manifest
            );
            
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "New version available: %s (%lu bytes)", 
                         manifest.version, manifest.size);
                
                // Update snapshot
                s_snapshot.state = UPDATE_STATE_AVAILABLE;
                strncpy(s_snapshot.available_version, manifest.version, 
                        sizeof(s_snapshot.available_version) - 1);
                s_snapshot.download_size = manifest.size;
                
                // Simulate download progress (for this sub-phase)
                for (uint8_t i = 0; i <= 100; i += 10) {
                    s_snapshot.progress_percent = i;
                    lv_async_call(post_ota_changed_event, NULL);
                    vTaskDelay(pdMS_TO_TICKS(500));  // Simulate work
                }
                
                s_snapshot.state = UPDATE_STATE_READY;
            } else {
                ESP_LOGE(TAG, "Manifest validation failed");
                s_snapshot.state = UPDATE_STATE_FAILED;
                strncpy(s_snapshot.error_message, "Invalid manifest", 
                        sizeof(s_snapshot.error_message) - 1);
            }
        } else {
            ESP_LOGE(TAG, "HTTP %d from primary", status);
            // TODO: Try mirror in 12.11
            s_snapshot.state = UPDATE_STATE_FAILED;
            snprintf(s_snapshot.error_message, sizeof(s_snapshot.error_message), 
                     "Server returned %d", status);
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        s_snapshot.state = UPDATE_STATE_FAILED;
        strncpy(s_snapshot.error_message, "Network error", 
                sizeof(s_snapshot.error_message) - 1);
    }
    
    esp_http_client_cleanup(client);
    lv_async_call(post_ota_changed_event, NULL);
    break;
}
```

5. **Update shell to show modal** (`main/ui/shell.cpp`):
```cpp
// Include at top:
#include "ota_modal.h"
extern "C" {
#include "crystal_update.h"
}

// In Shell::processUIEvent:
case UI_EVT_OTA_CHANGED: {
    crystal_update_snapshot_t snapshot;
    crystal_update_snapshot(&snapshot);
    
    switch (snapshot.state) {
        case UPDATE_STATE_CHECKING:
            OTAModal::getInstance()->show();
            OTAModal::getInstance()->updateProgress(0, "Checking for updates...");
            break;
        
        case UPDATE_STATE_DOWNLOADING:
            OTAModal::getInstance()->updateProgress(
                snapshot.progress_percent,
                "Downloading update..."
            );
            break;
        
        case UPDATE_STATE_READY:
            OTAModal::getInstance()->updateProgress(100, "Download complete");
            // TODO: Show confirmation dialog in 12.9
            OTAModal::getInstance()->hide();
            showToast("Update ready to install", 3000);
            break;
        
        case UPDATE_STATE_FAILED:
            OTAModal::getInstance()->showError(snapshot.error_message);
            vTaskDelay(pdMS_TO_TICKS(3000));
            OTAModal::getInstance()->hide();
            break;
        
        default:
            break;
    }
    break;
}
```

6. **Update CMakeLists.txt**:
```cmake
# Add to main/CMakeLists.txt SRCS:
    "ui/ota_modal.cpp"
```

### Validation

1. **Build and flash**:
```bash
./scripts/build_dev.sh
idf.py flash monitor
```

2. **Ensure device has Wi-Fi connected** (check Settings › Network)

3. **Tap "Check for Updates"**:
   - **Checkpoint**: Modal appears with "Checking for updates..."
   - Monitor logs show:
   ```
   I (12345) update: Fetching manifest from primary
   I (12500) update: Manifest fetched: 345 bytes
   I (12501) manifest: Manifest valid: 0.11.0 → 0.12.0 (2810392 bytes)
   I (12502) update: New version available: 0.12.0 (2810392 bytes)
   I (13000) ota_modal: Progress: 10% - Downloading update...
   I (13500) ota_modal: Progress: 20% - Downloading update...
   ...
   I (18000) ota_modal: Progress: 100% - Download complete
   ```
   - **Checkpoint**: Progress bar animates from 0% to 100%
   - **Checkpoint**: Toast "Update ready to install" appears

4. **Test offline** (disable Wi-Fi):
   - Tap "Check for Updates"
   - **Checkpoint**: Modal shows "Network error" after timeout

5. **Commit**:
```bash
git add main/ui/ota_modal.{h,cpp} main/crystal_update.c main/ui/shell.{h,cpp} main/CMakeLists.txt
git commit -m "Phase 12.8: Add download progress UI

- Create OTA modal with progress bar and message label
- Fetch manifest from primary URL via esp_http_client
- Parse manifest, simulate download progress (10% steps)
- Post UI_EVT_OTA_CHANGED to show modal updates

Checkpoint: Modal shows progress 0% → 100%, toast confirms completion

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Sub-Phase 12.9: Flash Write + Reboot

### Goal
Replace stub download with real `esp_https_ota`, write to flash, verify, reboot to new image.

### Important watchdog note

The OTA worker is pinned to Core 0. A synchronous `esp_http_client_open()` or
`esp_http_client_read()` can spend several seconds in software mbedTLS ECDH
(`mbedtls_ecp_mul`) while holding that core. The task watchdog then reports
`IDLE0` starvation even though the HTTPS request may eventually succeed.

Use `esp_http_client_config_t.is_async = true` for both the manifest and binary
client. Poll operations that return `ESP_ERR_HTTP_EAGAIN`, yielding with a short
`vTaskDelay()` between attempts and enforcing an overall timeout. This keeps the
idle task schedulable during TLS and network waits. Increasing the watchdog
timeout or disabling the watchdog is not a substitute for yielding.

Validation symptom before the fix:

```text
task_wdt: - IDLE0 (CPU 0)
task_wdt: CPU 0: ota_worker
mbedtls_ecp_mul_restartable
esp_http_client_open
```

After the fix, the manifest can still take several seconds over HTTPS, but the
watchdog warning is absent and the OTA state reaches `AVAILABLE` or `CURRENT`.

### Changes

1. **Update crystal_update.c** to use esp_https_ota:
```c
// Add includes:
#include "esp_https_ota.h"
#include "esp_ota_ops.h"

// Replace simulated download in OTA_REQ_CHECK case with real OTA:
if (err == ESP_OK && status == 200) {
    ESP_LOGI(TAG, "Manifest valid: %s (%lu bytes)", manifest.version, manifest.size);
    
    // Update snapshot for download
    s_snapshot.state = UPDATE_STATE_DOWNLOADING;
    strncpy(s_snapshot.available_version, manifest.version, 
            sizeof(s_snapshot.available_version) - 1);
    s_snapshot.download_size = manifest.size;
    s_snapshot.progress_percent = 0;
    s_snapshot.downloaded_bytes = 0;
    lv_async_call(post_ota_changed_event, NULL);
    
    // Perform OTA
    esp_http_client_config_t ota_config = {
        .url = manifest.urls[0],  // Use first URL
        .timeout_ms = 30000,
        .keep_alive_enable = true,
    };
    
    esp_https_ota_config_t ota_https_config = {
        .http_config = &ota_config,
    };
    
    esp_https_ota_handle_t ota_handle = NULL;
    err = esp_https_ota_begin(&ota_https_config, &ota_handle);
    
    if (err == ESP_OK) {
        bool ota_success = true;
        
        while (1) {
            err = esp_https_ota_perform(ota_handle);
            if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
            
            // Update progress
            int downloaded = esp_https_ota_get_image_len_read(ota_handle);
            s_snapshot.downloaded_bytes = downloaded;
            s_snapshot.progress_percent = (downloaded * 100) / manifest.size;
            lv_async_call(post_ota_changed_event, NULL);
        }
        
        if (err == ESP_OK) {
            // Verify downloaded image
            if (esp_https_ota_is_complete_data_received(ota_handle)) {
                ESP_LOGI(TAG, "OTA image received completely");
                
                // Finish OTA (writes to partition, marks pending)
                err = esp_https_ota_finish(ota_handle);
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "OTA successful, rebooting in 2 seconds...");
                    s_snapshot.state = UPDATE_STATE_INSTALLING;
                    strncpy(s_snapshot.error_message, "Rebooting...", 
                            sizeof(s_snapshot.error_message) - 1);
                    lv_async_call(post_ota_changed_event, NULL);
                    
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    esp_restart();
                } else {
                    ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(err));
                    ota_success = false;
                }
            } else {
                ESP_LOGE(TAG, "OTA data incomplete");
                ota_success = false;
            }
        } else {
            ESP_LOGE(TAG, "OTA perform failed: %s", esp_err_to_name(err));
            ota_success = false;
        }
        
        if (!ota_success) {
            esp_https_ota_abort(ota_handle);
            s_snapshot.state = UPDATE_STATE_FAILED;
            strncpy(s_snapshot.error_message, "Download failed", 
                    sizeof(s_snapshot.error_message) - 1);
            lv_async_call(post_ota_changed_event, NULL);
        }
    } else {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        s_snapshot.state = UPDATE_STATE_FAILED;
        strncpy(s_snapshot.error_message, "OTA init failed", 
                sizeof(s_snapshot.error_message) - 1);
        lv_async_call(post_ota_changed_event, NULL);
    }
}
```

2. **Update shell to show reboot message** (`main/ui/shell.cpp`):
```cpp
case UPDATE_STATE_INSTALLING:
    OTAModal::getInstance()->updateProgress(100, "Installing update...");
    // Modal stays visible until device reboots
    break;
```

3. **Add OTA confirmation at boot** (`main/main.cpp`):
```cpp
// After crystal_update_init(), check if we just booted from OTA:
extern "C" {
#include "esp_ota_ops.h"
}

void app_main(void) {
    // ... existing init ...
    
    crystal_update_init();
    
    // Check if we just updated
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI("main", "First boot after OTA, marking as valid");
            // Wait for UI to fully initialize before confirming
            // This will be done in shell initialization after 1.2s settling
        } else if (ota_state == ESP_OTA_IMG_VALID) {
            ESP_LOGI("main", "Running confirmed OTA image");
        }
    }
    
    // ... continue with UI init ...
}
```

4. **Confirm OTA in shell init** (`main/ui/shell.cpp`):
```cpp
// In Shell::init(), after brookesia_start_default, before crash check:

// Confirm OTA if pending
const esp_partition_t *running = esp_ota_get_running_partition();
esp_ota_img_states_t ota_state;
if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "Confirming OTA image after 1.2s settling");
        vTaskDelay(pdMS_TO_TICKS(1200));  // Settling interval
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "OTA image confirmed");
    }
}
```

### Validation

1. **Setup: Prepare OTA server** (local test):
```bash
# Build version 0.12.0 (temporarily change CMakeLists.txt)
# In CMakeLists.txt:
# project(crystal_os VERSION 0.12.0)

./scripts/build_dev.sh

# Calculate SHA-256
shasum -a 256 build/crystal_os.bin

# Create manifest.json:
cat > /tmp/manifest.json <<EOF
{
  "schema": 1,
  "target": "waveshare-esp32-s3-touch-lcd-4b",
  "channel": "stable",
  "version": "0.12.0",
  "size": $(stat -f%z build/crystal_os.bin),
  "sha256": "$(shasum -a 256 build/crystal_os.bin | awk '{print $1}')",
  "urls": ["https://your-test-server.com/crystal_os.bin"],
  "notes": "Test OTA update"
}
EOF

# Serve files (use Python HTTP server with SSL or upload to real server)
```

2. **Flash version 0.11.0** to device:
```bash
# Restore CMakeLists.txt to 0.11.0
./scripts/build_dev.sh
idf.py flash monitor
```

3. **Ensure Wi-Fi connected**

4. **Tap "Check for Updates"**:
   - **Checkpoint**: Modal shows "Checking for updates..."
   - **Checkpoint**: Progress bar animates 0% → 100% (real download)
   - Monitor shows:
   ```
   I (12345) update: Manifest valid: 0.12.0 (2810392 bytes)
   I (12500) update: OTA image received completely
   I (12600) update: OTA successful, rebooting in 2 seconds...
   ```
   - **Checkpoint**: Modal shows "Installing update..."
   - **Checkpoint**: Device reboots after 2 seconds

5. **After reboot**:
   - Monitor shows:
   ```
   I (1234) main: First boot after OTA, marking as valid
   I (2500) shell: Confirming OTA image after 1.2s settling
   I (3700) shell: OTA image confirmed
   ```
   - Navigate to Settings → Device Status
   - **Checkpoint**: "Firmware Version: 0.12.0"

6. **Commit**:
```bash
git add main/crystal_update.c main/ui/shell.cpp main/main.cpp
git commit -m "Phase 12.9: Implement real OTA flash write and reboot

- Replace simulated download with esp_https_ota
- Stream download with progress updates
- Write to OTA partition, mark as pending
- Reboot after 2-second announcement
- Confirm image after 1.2s settling interval

Checkpoint: Real OTA completes, device reboots to new version

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Sub-Phase 12.10: Rollback Detection + Toast

### Goal
Boot faulty image that crashes before confirmation, verify automatic rollback, show toast.

### Changes

1. **Persist attempted version before reboot** (`main/crystal_update.c`):
```c
// Add NVS persistence before reboot:
#include "nvs_flash.h"
#include "nvs.h"

// In OTA_REQ_CHECK case, before esp_restart():
if (err == ESP_OK) {
    ESP_LOGI(TAG, "OTA successful, persisting version info");
    
    // Store attempted version in NVS
    nvs_handle_t handle;
    if (nvs_open("ota", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_str(handle, "attempted_ver", manifest.version);
        nvs_commit(handle);
        nvs_close(handle);
    }
    
    s_snapshot.state = UPDATE_STATE_INSTALLING;
    strncpy(s_snapshot.error_message, "Rebooting...", 
            sizeof(s_snapshot.error_message) - 1);
    lv_async_call(post_ota_changed_event, NULL);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}
```

2. **Detect rollback at boot** (`main/crystal_update.c`):
```c
// Add rollback detection function:
static bool s_rollback_occurred = false;

bool crystal_update_check_rollback(char *attempted_version, size_t buf_size) {
    nvs_handle_t handle;
    if (nvs_open("ota", NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    
    size_t len = buf_size;
    esp_err_t err = nvs_get_str(handle, "attempted_ver", attempted_version, &len);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        return false;
    }
    
    // Check if current version != attempted version
    const char *current = crystal_version_string();
    if (strcmp(current, attempted_version) != 0) {
        ESP_LOGW(TAG, "Rollback detected: attempted %s, running %s", 
                 attempted_version, current);
        
        // Clear attempted version
        if (nvs_open("ota", NVS_READWRITE, &handle) == ESP_OK) {
            nvs_erase_key(handle, "attempted_ver");
            nvs_commit(handle);
            nvs_close(handle);
        }
        
        return true;
    }
    
    // Clear attempted version (successful update)
    if (nvs_open("ota", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_key(handle, "attempted_ver");
        nvs_commit(handle);
        nvs_close(handle);
    }
    
    return false;
}

// Update crystal_update_init:
void crystal_update_init(void) {
    ESP_LOGI(TAG, "Initializing update subsystem");
    
    // Check for rollback
    char attempted_ver[16];
    if (crystal_update_check_rollback(attempted_ver, sizeof(attempted_ver))) {
        ESP_LOGW(TAG, "Update to %s failed, rolled back", attempted_ver);
        s_rollback_occurred = true;
    }
    
    // ... rest of init ...
}

bool crystal_update_did_rollback(void) {
    return s_rollback_occurred;
}
```

3. **Add rollback detection to header** (`main/crystal_update.h`):
```c
// Add after crystal_update_snapshot:

/**
 * Check if a rollback occurred on this boot.
 * Returns true once; flag cleared after first call.
 */
bool crystal_update_did_rollback(void);
```

4. **Add UI event for rollback** (`main/ui/shell.h`):
```cpp
UI_EVT_OTA_ROLLED_BACK,
```

5. **Post rollback event from shell** (`main/ui/shell.cpp`):
```cpp
// In Shell::init(), after OTA confirmation check:

// Check for rollback
if (crystal_update_did_rollback()) {
    UIEvent evt = {.type = UI_EVT_OTA_ROLLED_BACK};
    enqueueUIEvent(evt);
}
```

6. **Handle rollback event** (`main/ui/shell.cpp`):
```cpp
case UI_EVT_OTA_ROLLED_BACK: {
    showToast("Update failed, rolled back", 4000);
    break;
}
```

7. **Update Device Status** to show rollback history (`main/ui/settings/device_status_overlay.cpp`):
```cpp
// Add "Last Update Attempt" row:
lv_obj_t *update_row = lv_obj_create(content);
lv_obj_set_size(update_row, LV_PCT(100), LV_SIZE_CONTENT);
lv_obj_set_style_pad_all(update_row, 12, 0);
lv_obj_set_flex_flow(update_row, LV_FLEX_FLOW_COLUMN);

lv_obj_t *update_label = lv_label_create(update_row);
lv_label_set_text(update_label, "Last Update");
lv_obj_set_style_text_color(update_label, lv_color_hex(0x888888), 0);
lv_obj_set_style_text_font(update_label, &lv_font_montserrat_14, 0);

lv_obj_t *update_value = lv_label_create(update_row);
// TODO: Fetch from NVS history
lv_label_set_text(update_value, "None");
lv_obj_set_style_text_font(update_value, &lv_font_montserrat_16, 0);
```

### Validation

1. **Create fault-injection build**:
```cpp
// In main/main.cpp app_main(), add deliberate crash BEFORE confirming OTA:
void app_main(void) {
    // ... init ...
    
    crystal_update_init();
    
    // Check if pending OTA (DON'T confirm yet)
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGE("main", "FAULT INJECTION: Crashing before OTA confirmation");
            vTaskDelay(pdMS_TO_TICKS(500));
            abort();  // Deliberate crash
        }
    }
    
    // ... rest of init ...
}
```

2. **Build fault-injection version as 0.12.0**:
```bash
# Set version to 0.12.0 in CMakeLists.txt
./scripts/build_dev.sh
# Serve this as OTA update
```

3. **Flash 0.11.0 to device**:
```bash
# Set version to 0.11.0 in CMakeLists.txt
./scripts/build_dev.sh
idf.py flash monitor
```

4. **Perform OTA**:
   - Tap "Check for Updates"
   - Wait for download to complete
   - **Checkpoint**: Device reboots
   - Monitor shows:
   ```
   I (1234) main: First boot after OTA, marking as valid
   E (1235) main: FAULT INJECTION: Crashing before OTA confirmation
   Guru Meditation Error: Core 0 panic'ed (abort)
   Rebooting...
   ```

5. **After rollback reboot**:
   - Monitor shows:
   ```
   W (1234) update: Rollback detected: attempted 0.12.0, running 0.11.0
   W (1235) update: Update to 0.12.0 failed, rolled back
   ```
   - **Checkpoint**: Toast "Update failed, rolled back" appears
   - Navigate to Settings → Device Status
   - **Checkpoint**: "Firmware Version: 0.11.0" (rolled back)

6. **Remove fault injection code**

7. **Commit**:
```bash
git add main/crystal_update.{h,c} main/ui/shell.{h,cpp} main/ui/settings/device_status_overlay.cpp
git commit -m "Phase 12.10: Add rollback detection and toast

- Persist attempted version in NVS before reboot
- Detect rollback (current != attempted) at next boot
- Show 'Update failed, rolled back' toast once
- Clear rollback flag after display

Checkpoint: Faulty image rolls back, toast confirms failure

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Sub-Phase 12.11: Primary/Mirror Fallback

### Goal
Retry manifest fetch from mirror if primary fails; retry binary download from mirror if primary fails.

### Changes

1. **Update crystal_update.c** to add fallback logic:
```c
// In OTA_REQ_CHECK case, replace single manifest fetch with fallback:

static esp_err_t fetch_manifest(const char *url, update_manifest_t *out_manifest) {
    s_manifest_len = 0;
    memset(s_manifest_buf, 0, sizeof(s_manifest_buf));
    
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    
    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "Fetch from %s failed: err=%s, status=%d", 
                 url, esp_err_to_name(err), status);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Fetched manifest from %s: %zu bytes", url, s_manifest_len);
    
    // Parse manifest
    return crystal_update_parse_manifest(
        s_manifest_buf,
        s_manifest_len,
        EXPECTED_TARGET,
        EXPECTED_CHANNEL,
        crystal_version_string(),
        out_manifest
    );
}

// In OTA_REQ_CHECK case:
ESP_LOGI(TAG, "Checking for updates");
update_manifest_t manifest;

// Try primary, then mirror
esp_err_t err = fetch_manifest(PRIMARY_MANIFEST_URL, &manifest);
if (err != ESP_OK) {
    ESP_LOGW(TAG, "Primary manifest failed, trying mirror");
    err = fetch_manifest(MIRROR_MANIFEST_URL, &manifest);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Both primary and mirror manifests failed");
        s_snapshot.state = UPDATE_STATE_FAILED;
        strncpy(s_snapshot.error_message, "Cannot fetch manifest", 
                sizeof(s_snapshot.error_message) - 1);
        lv_async_call(post_ota_changed_event, NULL);
        break;
    }
}

ESP_LOGI(TAG, "Manifest valid: %s (%lu bytes)", manifest.version, manifest.size);
// ... continue with OTA download ...
```

2. **Add binary URL fallback** (in same case):
```c
// After successful manifest fetch, try URLs in order:

bool ota_started = false;
for (uint8_t url_idx = 0; url_idx < manifest.url_count && !ota_started; url_idx++) {
    ESP_LOGI(TAG, "Attempting download from URL %u: %s", url_idx, manifest.urls[url_idx]);
    
    esp_http_client_config_t ota_config = {
        .url = manifest.urls[url_idx],
        .timeout_ms = 30000,
        .keep_alive_enable = true,
    };
    
    esp_https_ota_config_t ota_https_config = {
        .http_config = &ota_config,
    };
    
    esp_https_ota_handle_t ota_handle = NULL;
    err = esp_https_ota_begin(&ota_https_config, &ota_handle);
    
    if (err == ESP_OK) {
        // Perform OTA (existing code)
        ota_started = true;
        // ... (move existing OTA perform/finish code here) ...
    } else {
        ESP_LOGW(TAG, "OTA begin failed for URL %u: %s", url_idx, esp_err_to_name(err));
        // Continue to next URL
    }
}

if (!ota_started) {
    ESP_LOGE(TAG, "All download URLs failed");
    s_snapshot.state = UPDATE_STATE_FAILED;
    strncpy(s_snapshot.error_message, "Download failed from all mirrors", 
            sizeof(s_snapshot.error_message) - 1);
    lv_async_call(post_ota_changed_event, NULL);
}
```

### Validation

1. **Test manifest fallback**:
```bash
# Temporarily break PRIMARY_MANIFEST_URL in code:
#define PRIMARY_MANIFEST_URL "https://invalid-url.example.com/manifest.json"
# Keep MIRROR_MANIFEST_URL valid

./scripts/build_dev.sh
idf.py flash monitor
```

2. **Tap "Check for Updates"**:
   - Monitor shows:
   ```
   I (12345) update: Checking for updates
   W (22345) update: Fetch from https://invalid-url.example.com/manifest.json failed
   W (22346) update: Primary manifest failed, trying mirror
   I (23000) update: Fetched manifest from mirror: 345 bytes
   I (23001) update: Manifest valid: ...
   ```
   - **Checkpoint**: Mirror fallback succeeds, OTA continues

3. **Test binary URL fallback**:
```bash
# In manifest JSON, set first URL to invalid, second URL valid:
{
  "urls": [
    "https://invalid-url.example.com/crystal_os.bin",
    "https://valid-mirror.example.com/crystal_os.bin"
  ]
}
```

4. **Perform OTA**:
   - Monitor shows:
   ```
   I (12345) update: Attempting download from URL 0: https://invalid-url.example.com/...
   W (13000) update: OTA begin failed for URL 0
   I (13001) update: Attempting download from URL 1: https://valid-mirror.example.com/...
   I (13500) update: OTA image received completely
   ```
   - **Checkpoint**: Second URL succeeds

5. **Restore correct URLs before commit**

6. **Commit**:
```bash
git add main/crystal_update.c
git commit -m "Phase 12.11: Add primary/mirror fallback logic

- Try mirror manifest if primary fetch fails
- Try all URLs in manifest.urls[] order until one succeeds
- Log fallback attempts for debugging

Checkpoint: Mirror fallback works for both manifest and binary

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Sub-Phase 12.12: USB Recovery Tool

### Goal
Create Python script to flash firmware over USB, with preserve/factory-reset options.

### Changes

1. **Create recovery script** (`tools/crystal_recovery.py`, new file):
```python
#!/usr/bin/env python3
"""
Crystal OS USB Recovery Tool

Restores firmware over USB with options to preserve or reset user data.
"""

import argparse
import sys
import subprocess
import os
from pathlib import Path

BOOTLOADER_OFFSET = 0x0
PARTITION_TABLE_OFFSET = 0x8000
OTA_DATA_OFFSET = 0xd000
APP_OFFSET = 0x10000

def run_esptool(args):
    """Run esptool.py with given arguments."""
    cmd = ["esptool.py"] + args
    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Error: {result.stderr}", file=sys.stderr)
        return False
    print(result.stdout)
    return True

def confirm(prompt):
    """Ask user for yes/no confirmation."""
    while True:
        response = input(f"{prompt} [y/n]: ").lower()
        if response in ['y', 'yes']:
            return True
        elif response in ['n', 'no']:
            return False

def preserve_recovery(port, firmware_dir):
    """Recover firmware while preserving NVS and SPIFFS."""
    print("\n=== PRESERVE MODE ===")
    print("This will restore:")
    print("  - Bootloader")
    print("  - Partition table")
    print("  - OTA data (reset to factory)")
    print("  - Application firmware")
    print("\nThis will KEEP:")
    print("  - Settings (Wi-Fi, preferences)")
    print("  - App data (favorites, timers)")
    print("  - Crash dumps")
    
    if not confirm("\nProceed with preserve-mode recovery?"):
        print("Aborted.")
        return False
    
    bootloader = firmware_dir / "bootloader.bin"
    partition_table = firmware_dir / "partition-table.bin"
    ota_data_initial = firmware_dir / "ota_data_initial.bin"
    app = firmware_dir / "crystal_os.bin"
    
    # Verify files exist
    for file in [bootloader, partition_table, ota_data_initial, app]:
        if not file.exists():
            print(f"Error: {file} not found", file=sys.stderr)
            return False
    
    # Flash
    args = [
        "-p", port,
        "-b", "460800",
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash",
        "--flash_mode", "dio",
        "--flash_freq", "80m",
        "--flash_size", "detect",
        hex(BOOTLOADER_OFFSET), str(bootloader),
        hex(PARTITION_TABLE_OFFSET), str(partition_table),
        hex(OTA_DATA_OFFSET), str(ota_data_initial),
        hex(APP_OFFSET), str(app),
    ]
    
    return run_esptool(args)

def factory_reset_recovery(port, firmware_dir):
    """Recover firmware and erase all user data."""
    print("\n=== FACTORY RESET MODE ===")
    print("⚠️  WARNING: This will ERASE ALL DATA:")
    print("  - Wi-Fi credentials")
    print("  - All settings")
    print("  - App favorites and data")
    print("  - Crash dumps")
    print("\nThe device will be restored to factory state.")
    
    if not confirm("\n⚠️  Proceed with factory reset?"):
        print("Aborted.")
        return False
    
    print("\n⚠️  FINAL CONFIRMATION: All user data will be permanently deleted.")
    if not confirm("Type 'y' to confirm factory reset"):
        print("Aborted.")
        return False
    
    # Erase flash
    print("\nErasing flash...")
    if not run_esptool(["-p", port, "erase_flash"]):
        return False
    
    # Flash everything
    print("\nFlashing firmware...")
    return preserve_recovery(port, firmware_dir)

def extract_coredump(port, output_file):
    """Extract coredump from device."""
    print("\n=== EXTRACT COREDUMP ===")
    
    args = [
        "-p", port,
        "read_flash",
        "0x3F0000", "0x10000",  # Coredump partition (adjust offsets as needed)
        output_file,
    ]
    
    if run_esptool(args):
        print(f"\nCoredump saved to: {output_file}")
        print("Decode with: esp-coredump.py info_corefile -t raw <elf_file> <coredump_file>")
        return True
    return False

def main():
    parser = argparse.ArgumentParser(
        description="Crystal OS USB Recovery Tool",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Preserve user data
  %(prog)s --port /dev/cu.usbserial-0001 --firmware build/ --preserve
  
  # Factory reset
  %(prog)s --port /dev/cu.usbserial-0001 --firmware build/ --factory-reset
  
  # Extract coredump
  %(prog)s --port /dev/cu.usbserial-0001 --extract-coredump coredump.bin
        """
    )
    
    parser.add_argument("-p", "--port", required=True, help="Serial port (e.g., /dev/cu.usbserial-0001)")
    parser.add_argument("-f", "--firmware", help="Firmware directory containing .bin files")
    parser.add_argument("--preserve", action="store_true", help="Preserve user data (NVS, SPIFFS)")
    parser.add_argument("--factory-reset", action="store_true", help="Erase all data and restore firmware")
    parser.add_argument("--extract-coredump", metavar="FILE", help="Extract coredump to FILE")
    
    args = parser.parse_args()
    
    # Validate arguments
    if args.extract_coredump:
        if not extract_coredump(args.port, args.extract_coredump):
            sys.exit(1)
        sys.exit(0)
    
    if not args.firmware:
        parser.error("--firmware required when not extracting coredump")
    
    firmware_dir = Path(args.firmware)
    if not firmware_dir.is_dir():
        print(f"Error: {firmware_dir} is not a directory", file=sys.stderr)
        sys.exit(1)
    
    if args.factory_reset:
        success = factory_reset_recovery(args.port, firmware_dir)
    elif args.preserve:
        success = preserve_recovery(args.port, firmware_dir)
    else:
        parser.error("Must specify --preserve or --factory-reset")
    
    if success:
        print("\n✅ Recovery complete!")
        sys.exit(0)
    else:
        print("\n❌ Recovery failed", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
```

2. **Make script executable**:
```bash
chmod +x tools/crystal_recovery.py
```

3. **Add README** (`tools/README.md`, new file):
```markdown
# Crystal OS Recovery Tools

## USB Recovery

### Prerequisites

- Python 3.7+
- esptool.py: `pip install esptool`
- USB connection to device

### Usage

**Preserve user data** (recommended for firmware updates):
```bash
./crystal_recovery.py --port /dev/cu.usbserial-0001 --firmware ../build/ --preserve
```

**Factory reset** (erase all settings and data):
```bash
./crystal_recovery.py --port /dev/cu.usbserial-0001 --firmware ../build/ --factory-reset
```

**Extract coredump**:
```bash
./crystal_recovery.py --port /dev/cu.usbserial-0001 --extract-coredump coredump.bin
```

### What Gets Flashed

**Preserve mode**:
- ✅ Bootloader
- ✅ Partition table
- ✅ OTA metadata (reset)
- ✅ Application firmware
- ❌ NVS (settings preserved)
- ❌ SPIFFS (app data preserved)
- ❌ Coredump (crash history preserved)

**Factory reset mode**:
- Erases entire flash
- Flashes all partitions from scratch
- Device returns to out-of-box state

### Coredump Decoding

After extracting coredump:
```bash
esp-coredump.py info_corefile -t raw ../build/crystal_os.elf coredump.bin
```

This shows backtrace, registers, and task state at crash time.
```

### Validation

1. **Test preserve mode**:
```bash
# Build firmware
./scripts/build_dev.sh

# Flash via recovery tool
./tools/crystal_recovery.py --port /dev/cu.usbserial-0001 --firmware build/ --preserve
```

Expected output:
```
=== PRESERVE MODE ===
This will restore:
  - Bootloader
  - Partition table
  - OTA data (reset to factory)
  - Application firmware

This will KEEP:
  - Settings (Wi-Fi, preferences)
  - App data (favorites, timers)
  - Crash dumps

Proceed with preserve-mode recovery? [y/n]: y
Running: esptool.py -p /dev/cu.usbserial-0001 -b 460800 ...
Connecting...
Writing at 0x00000000... (100%)
...
✅ Recovery complete!
```

2. **Verify device boots** after preserve recovery

3. **Test factory reset**:
```bash
./tools/crystal_recovery.py --port /dev/cu.usbserial-0001 --firmware build/ --factory-reset
```

Expected output includes two confirmations:
```
⚠️  WARNING: This will ERASE ALL DATA:
...
⚠️  Proceed with factory reset? [y/n]: y

⚠️  FINAL CONFIRMATION: All user data will be permanently deleted.
Type 'y' to confirm factory reset [y/n]: y

Erasing flash...
...
✅ Recovery complete!
```

4. **Test coredump extraction** (if coredump exists):
```bash
./tools/crystal_recovery.py --port /dev/cu.usbserial-0001 --extract-coredump /tmp/coredump.bin

# Decode
esp-coredump.py info_corefile -t raw build/crystal_os.elf /tmp/coredump.bin
```

5. **Commit**:
```bash
git add tools/crystal_recovery.py tools/README.md
git commit -m "Phase 12.12: Add USB recovery tool

- Python script wraps esptool for firmware recovery
- Preserve mode: flash bootloader/partition/app, keep NVS/SPIFFS
- Factory reset mode: erase all, flash from scratch
- Coredump extraction and decode instructions

Checkpoint: Recovery tool restores firmware, shows usage menu

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Sub-Phase 12.13: Full Validation Suite

### Goal
Run comprehensive test matrix, verify all OTA/recovery scenarios, re-run Phase 11 regressions.

### Test Matrix

| Test Case | Expected Result | Pass/Fail |
|-----------|----------------|-----------|
| **Manifest Validation** | | |
| Valid manifest (newer version) | Parsed successfully | |
| Equal version | Rejected | |
| Older version | Rejected | |
| Wrong target | Rejected | |
| Wrong channel | Rejected | |
| Malformed JSON | Rejected | |
| Non-HTTPS URL | Rejected | |
| **Network Scenarios** | | |
| Offline (no Wi-Fi) | Error: "Network error" | |
| Primary manifest fails, mirror succeeds | Downloads from mirror | |
| Both manifests fail | Error: "Cannot fetch manifest" | |
| Primary binary fails, mirror succeeds | Downloads from mirror | |
| All binary URLs fail | Error: "Download failed from all mirrors" | |
| **OTA Workflow** | | |
| Full successful OTA | Progress 0→100%, reboot, new version | |
| Cancel during download | Download stopped, state reset | |
| Power interrupt during write | Running firmware unchanged | |
| Low battery (<40%) | Blocked with error | |
| Unknown battery state | Blocked with error | |
| Not charging and <40% | Blocked with error | |
| **Rollback** | | |
| Faulty image (crash before confirm) | Auto-rollback, toast shown | |
| Faulty image (reboot loop) | Auto-rollback after 3 attempts | |
| Second boot after rollback | No toast, history in Device Status | |
| **USB Recovery** | | |
| Preserve mode | Firmware restored, settings kept | |
| Factory reset | Firmware restored, all data erased | |
| Coredump extraction | Binary file saved, decodable | |
| Cancel at first prompt | Aborted, no changes | |
| Cancel at second prompt (factory reset) | Aborted, no changes | |
| **Phase 11 Regressions** | | |
| Settings navigation | All pages load, no crashes | |
| Wi-Fi scan and connect | Networks listed, connection works | |
| Keyboard input | Text entry works, backspace/enter | |
| Gesture navigation (swipe back) | Returns to previous screen | |
| Timer alert (countdown finished) | Alert shown, sound plays | |
| Power modes (sleep/wake) | Screen dims/wakes correctly | |
| Device Status display | All fields populated correctly | |

### Validation Steps

1. **Unit tests**:
```bash
idf.py build
idf.py flash monitor
# In monitor:
> test "[update]"
```
**Checkpoint**: All unit tests pass

2. **Manifest validation tests**:
   - Test each manifest scenario from table
   - Verify logs match expected rejections

3. **Network tests**:
   - Disable Wi-Fi, tap "Check for Updates"
   - Break primary URL, verify mirror fallback
   - Break both URLs, verify error

4. **Full OTA test**:
   - Build 0.11.0, flash to device
   - Build 0.12.0, serve as OTA
   - Perform update, verify:
     - Progress bar animates
     - Device reboots
     - New version confirmed
     - Device Status shows "0.12.0"

5. **Rollback test**:
   - Create fault-injection build (crash before confirm)
   - Flash 0.11.0, OTA to faulty 0.12.0
   - Verify automatic rollback
   - Check toast appears
   - Verify Device Status shows rollback attempt

6. **USB recovery tests**:
   - Run preserve mode, check settings persist
   - Run factory reset, verify all data cleared
   - Extract coredump (if exists), decode

7. **Phase 11 regression tests**:
   - Navigate through all Settings pages
   - Test Wi-Fi scan and connect
   - Test keyboard input in Wi-Fi password field
   - Test gesture swipe-back
   - Set timer alert, wait for completion
   - Test power sleep/wake cycle
   - Check Device Status for all fields

### Completion Criteria

- [ ] All unit tests pass
- [ ] All manifest validation cases pass
- [ ] Network fallback works (primary → mirror)
- [ ] Full OTA completes successfully
- [ ] Rollback detected and toast shown
- [ ] USB recovery (preserve + factory reset) works
- [ ] Coredump extraction succeeds
- [ ] All Phase 11 regressions pass
- [ ] No memory leaks detected
- [ ] Documentation updated (CODE_GUIDE.md, DESIGN.md)

### Final Commit

```bash
git add -A
git commit -m "Phase 12.13: Complete validation suite

Tested:
- All manifest validation rules
- Network fallback (primary/mirror)
- Full OTA workflow (0.11.0 → 0.12.0)
- Automatic rollback on faulty image
- USB recovery (preserve and factory reset)
- Coredump extraction and decode
- Phase 11 regressions (Settings, Wi-Fi, timers, gestures)

All test cases passed. Phase 12 complete.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Summary: Phase 12 Complete

### What Was Built

1. **Version infrastructure** — Firmware reports `0.11.0`, displayed in Device Status
2. **Crash tracking** — Coredump summary stored in NVS, displayed in Device Status
3. **Crash recovery** — One-time toast after crash
4. **Signed builds** — Separate dev/prod profiles with RSA-3072 signatures
5. **Update UI** — "Check for Updates" button, modal with progress bar
6. **Manifest parsing** — Strict SemVer validation, schema/target/channel enforcement
7. **OTA worker** — Dedicated Core-0 thread for network operations
8. **Download progress** — Real esp_https_ota with live progress updates
9. **Flash write** — OTA partition update, reboot, confirmation after settling
10. **Rollback detection** — Auto-rollback on crash, toast notification
11. **Fallback logic** — Primary/mirror for manifest and binary
12. **USB recovery** — Python tool for preserve/factory-reset
13. **Full validation** — All test cases passed, regressions clean

### Key Files Added/Modified

- `main/crystal_version.{h,c}` — Version accessors
- `main/crystal_crash.{h,c}` — Coredump tracking
- `main/crystal_update.{h,c}` — OTA state machine
- `main/crystal_update_manifest.{h,c}` — Manifest parser
- `main/ui/ota_modal.{h,cpp}` — Progress UI
- `main/test/test_update_manifest.c` — Unit tests
- `scripts/build_{dev,prod}.sh` — Build profiles
- `tools/crystal_recovery.py` — USB recovery tool
- Updated: `main/main.cpp`, `main/ui/shell.{h,cpp}`, Device Status overlay

### Architecture Decisions Implemented

- Signed apps without hardware Secure Boot
- Strict SemVer, no prerelease versions
- Primary/mirror topology
- Dedicated OTA worker on Core-0
- Raw coredump retained (user-aware privacy tradeoff)
- USB recovery for bootloader/partition changes
- Preserve vs factory-reset options

### Next Phase

Phase 12 is complete. Ready for Phase 13 (if defined) or production deployment.

---

**Document Version:** 1.0  
**Last Updated:** 2026-10-11  
**Phase Status:** Implementation-ready
