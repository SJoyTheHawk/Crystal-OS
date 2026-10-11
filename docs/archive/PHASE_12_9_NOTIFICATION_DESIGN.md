# Phase 12.9: OTA Update Notification & User Interaction Design

**Status**: Design & Implementation Guide  
**Date**: 2026-10-11  
**Dependencies**: Phase 12 (OTA reliability foundations)

## Problem Statement

Phase 12 implemented the OTA worker, manifest parsing, download/install pipeline, and a basic modal for the download/install progress. However, it lacks user-facing notifications for key update lifecycle events:

- ✅ **Has**: Modal shown during download/install (blocking, non-cancellable)
- ❌ **Missing**: Notification when "already up to date"
- ❌ **Missing**: Notification when update is available (with version info)
- ❌ **Missing**: User confirmation before starting download
- ❌ **Missing**: Cancellation feedback during check
- ❌ **Missing**: Failed check notification (non-modal)
- ❌ **Missing**: Download cancellation capability

## Design Goals

1. **Non-intrusive for "up to date"**: Show a toast, not a modal
2. **Actionable for "update available"**: Present version info and let user confirm or dismiss
3. **Transparent during operations**: Show what's happening (checking, downloading)
4. **Cancellable when safe**: Allow cancellation during check and download phases
5. **Blocking when necessary**: Modal only during actual download/install (existing behavior)
6. **Consistent UI patterns**: Use existing toast and dialog systems from Crystal Shell

## UI Notification Taxonomy

### 1. Toast Notifications (Non-blocking, Auto-dismiss)

**When to use**: Transient status updates that don't require user action

| State | Message | Duration | Trigger |
|-------|---------|----------|---------|
| `UPDATE_STATE_CURRENT` | "System is up to date" | 2.5s | After successful check finds no update |
| Check cancelled by user | "Update check cancelled" | 2.5s | User taps Cancel during `UPDATE_STATE_CHECKING` |
| `UPDATE_STATE_FAILED` (network) | "Update check failed - {error}" | 3.0s | Manifest fetch fails or parse error |

### 2. Update Available Dialog (Blocking, User Action Required)

**When to use**: Update found, user must choose to proceed or dismiss

**Layout** (360×280 card, centered):
```
┌────────────────────────────────────┐
│   Update Available                 │ ← Title (18pt, white)
│                                    │
│   Crystal OS v1.2.3                │ ← Version (16pt, #cbd5e1)
│   12.4 MB                          │ ← Size (14pt, #94a3b8)
│                                    │
│   • Fix memory leak in WiFi        │ ← Release notes
│   • Improve gesture response       │   (14pt, #cbd5e1, scrollable)
│   • Update timezone database       │   Max 3 lines visible
│                                    │
│   ┌──────────┐  ┌──────────┐     │
│   │  Cancel  │  │ Download │     │ ← Buttons (140×44)
│   └──────────┘  └──────────┘     │
└────────────────────────────────────┘
```

**Behavior**:
- Appears when `UPDATE_STATE_AVAILABLE` is reached
- Overlays current app (uses `lv_layer_top()`, not Settings)
- **Cancel**: Dismisses dialog, returns to `UPDATE_STATE_IDLE`, no toast
- **Download**: Starts download, transitions to progress modal (existing)
- BACK gesture dismisses (same as Cancel)
- HOME pill does NOT dismiss (modal behavior)

### 3. Download/Install Modal (Existing, Keep As-Is)

**When shown**: `UPDATE_STATE_DOWNLOADING` and `UPDATE_STATE_INSTALLING`

**Current behavior** (already implemented):
- Full-screen overlay with 80% black background
- 360×180 card with progress bar
- Non-dismissible (blocks BACK and HOME)
- Auto-transitions to reboot after install completes

**No changes needed** for this modal.

### 4. Check-in-Progress Indicator (New, In Settings)

**Where**: Settings → System → Software Update page

**UI additions**:
- Spinner icon next to "Check for Updates" button when `UPDATE_STATE_CHECKING`
- Button text changes to "Checking..." (disabled state)
- Small "Cancel" button appears next to spinner
- Cancel sets `s_cancel_requested = true` and shows toast

## State Machine & Notification Mapping

```
                    ┌─────────────┐
          ┌────────→│    IDLE     │←─────────┐
          │         └──────┬──────┘          │
          │                │                 │
     (cancel)         (check_update)         │
          │                ↓                 │
          │         ┌─────────────┐          │
          ├────────│  CHECKING   │          │
          │         └──────┬──────┘          │
          │                │                 │
          │       ┌────────┴────────┐        │
          │       │                 │        │
     (cancel)     ↓                 ↓        │
          │  ┌─────────┐      ┌──────────┐  │
          ├─│AVAILABLE │      │ CURRENT  │──┘
          │  └────┬────┘      └──────────┘
          │       │                ↑
          │   (download)      (no update)
          │       ↓                │
          │  ┌──────────┐     Toast: "System
          │  │DOWNLOAD- │     is up to date"
          │  │  ING     │
          │  └────┬─────┘
          │       │
     (cancel)     ↓
          │  ┌──────────┐
          └─│INSTALLING │──→ (reboot)
             └──────────┘

Dialog/Modal Rules:
• CHECKING → No modal (Settings shows spinner)
• AVAILABLE → Update Available Dialog (user choice)
• DOWNLOADING/INSTALLING → Progress Modal (existing, blocking)
• CURRENT → Toast only
• FAILED → Toast only
• Cancel during CHECKING → Toast
```

## Implementation Plan

### Step 1: Add Toast Helpers to Crystal Core

**File**: `components/crystal_core/src/crystal_core.cpp`

```cpp
// Add to existing show_toast() usages in event loop:

} else if (message.type == UI_EVT_OTA_CHANGED) {
    crystal_update_snapshot_t snapshot = {};
    crystal_update_snapshot(&snapshot);
    
    // Toast for terminal states that don't need modals
    if (snapshot.state == UPDATE_STATE_CURRENT) {
        show_toast("System is up to date");
    } else if (snapshot.state == UPDATE_STATE_FAILED && 
               snapshot.error_message[0] != '\0') {
        // Failed states show toast unless it's a cancellation
        if (strstr(snapshot.error_message, "cancelled") == nullptr) {
            show_toast(snapshot.error_message);
        } else {
            show_toast("Update check cancelled");
        }
    }
    
    crystal_shell_ota_event();  // Existing call for modal/dialog
}
```

### Step 2: Create Update Available Dialog

**File**: `components/crystal_shell/src/crystal_shell.cpp`

Add new static objects:
```cpp
static lv_obj_t *s_update_dialog = nullptr;
static lv_obj_t *s_update_version_label = nullptr;
static lv_obj_t *s_update_size_label = nullptr;
static lv_obj_t *s_update_notes_label = nullptr;
```

Add dialog builder:
```cpp
static void update_dialog_build(const crystal_update_snapshot_t &snapshot)
{
    if (s_update_dialog != nullptr) {
        lv_obj_del(s_update_dialog);
    }
    
    // Semi-transparent full-screen background
    s_update_dialog = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_update_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_update_dialog, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_update_dialog, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_update_dialog, 0, 0);
    lv_obj_clear_flag(s_update_dialog, LV_OBJ_FLAG_SCROLLABLE);
    
    // Card
    lv_obj_t *card = lv_obj_create(s_update_dialog);
    lv_obj_set_size(card, 360, 280);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x252a30), 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 24, 0);
    lv_obj_center(card);
    
    // Title
    lv_obj_t *title = lv_label_create(card);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_label_set_text(title, "Update Available");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
    
    // Version
    s_update_version_label = lv_label_create(card);
    lv_obj_set_style_text_color(s_update_version_label, lv_color_hex(0xcbd5e1), 0);
    lv_obj_set_style_text_font(s_update_version_label, &lv_font_montserrat_16, 0);
    char version_text[64];
    snprintf(version_text, sizeof(version_text), "Crystal OS %s", 
             snapshot.available_version);
    lv_label_set_text(s_update_version_label, version_text);
    lv_obj_align(s_update_version_label, LV_ALIGN_TOP_MID, 0, 32);
    
    // Size
    s_update_size_label = lv_label_create(card);
    lv_obj_set_style_text_color(s_update_size_label, lv_color_hex(0x94a3b8), 0);
    lv_obj_set_style_text_font(s_update_size_label, &lv_font_montserrat_14, 0);
    char size_text[32];
    float size_mb = snapshot.download_size / (1024.0f * 1024.0f);
    snprintf(size_text, sizeof(size_text), "%.1f MB", size_mb);
    lv_label_set_text(s_update_size_label, size_text);
    lv_obj_align(s_update_size_label, LV_ALIGN_TOP_MID, 0, 58);
    
    // Release notes (scrollable, max 3 lines visible)
    lv_obj_t *notes_container = lv_obj_create(card);
    lv_obj_set_size(notes_container, 312, 80);
    lv_obj_set_style_bg_opa(notes_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(notes_container, 0, 0);
    lv_obj_set_style_pad_all(notes_container, 0, 0);
    lv_obj_align(notes_container, LV_ALIGN_TOP_LEFT, 0, 90);
    
    s_update_notes_label = lv_label_create(notes_container);
    lv_obj_set_width(s_update_notes_label, 300);
    lv_obj_set_style_text_color(s_update_notes_label, lv_color_hex(0xcbd5e1), 0);
    lv_obj_set_style_text_font(s_update_notes_label, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(s_update_notes_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_update_notes_label, snapshot.notes);
    
    // Buttons
    lv_obj_t *cancel_btn = lv_btn_create(card);
    lv_obj_set_size(cancel_btn, 140, 44);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x424852), 0);
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t *e) {
        if (s_update_dialog != nullptr) {
            lv_obj_del(s_update_dialog);
            s_update_dialog = nullptr;
        }
        crystal_shell_set_modal_open(false);
        // Reset to IDLE so user can check again later
        crystal_update_cancel_check();
    }, LV_EVENT_CLICKED, nullptr);
    
    lv_obj_t *download_btn = lv_btn_create(card);
    lv_obj_set_size(download_btn, 140, 44);
    lv_obj_align(download_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(download_btn, lv_color_hex(0x3b82f6), 0);
    lv_obj_t *download_label = lv_label_create(download_btn);
    lv_label_set_text(download_label, "Download");
    lv_obj_center(download_label);
    lv_obj_add_event_cb(download_btn, [](lv_event_t *e) {
        // Dialog will be hidden by ota_modal_update when DOWNLOADING starts
        // The download was already initiated in ota_worker_task
        // We just need to confirm and let it proceed (no cancel)
        ESP_LOGI(TAG, "User confirmed download");
    }, LV_EVENT_CLICKED, nullptr);
    
    crystal_shell_set_modal_open(true);
}

static void update_dialog_close()
{
    if (s_update_dialog != nullptr) {
        lv_obj_del(s_update_dialog);
        s_update_dialog = nullptr;
        crystal_shell_set_modal_open(false);
    }
}
```

### Step 3: Modify OTA Event Handler

**File**: `components/crystal_shell/src/crystal_shell.cpp`

Update `crystal_shell_ota_event()`:

```cpp
void crystal_shell_ota_event()
{
    crystal_update_snapshot_t snapshot = {};
    crystal_update_snapshot(&snapshot);
    
    // IDLE: hide everything
    if (snapshot.state == UPDATE_STATE_IDLE) {
        update_dialog_close();
        if (s_ota_modal != nullptr) lv_obj_add_flag(s_ota_modal, LV_OBJ_FLAG_HIDDEN);
        crystal_shell_set_modal_open(false);
        return;
    }
    
    // AVAILABLE: show confirmation dialog
    if (snapshot.state == UPDATE_STATE_AVAILABLE) {
        update_dialog_build(snapshot);
        if (s_ota_modal != nullptr) lv_obj_add_flag(s_ota_modal, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    
    // CHECKING, CURRENT, FAILED: hide both dialog and modal
    // (Toasts are handled in crystal_core.cpp)
    if (snapshot.state == UPDATE_STATE_CHECKING ||
        snapshot.state == UPDATE_STATE_CURRENT ||
        snapshot.state == UPDATE_STATE_FAILED) {
        update_dialog_close();
        if (s_ota_modal != nullptr) lv_obj_add_flag(s_ota_modal, LV_OBJ_FLAG_HIDDEN);
        crystal_shell_set_modal_open(false);
        return;
    }
    
    // DOWNLOADING/INSTALLING: show progress modal (existing logic)
    update_dialog_close();
    ota_modal_update(snapshot);
    ESP_LOGI(TAG, "OTA modal state=%d progress=%u", 
             snapshot.state, snapshot.progress_percent);
}
```

### Step 4: Fix OTA Worker Flow for User Confirmation

**Problem**: Currently, the worker auto-downloads when an update is found. We need a two-stage flow:
1. Check → `UPDATE_STATE_AVAILABLE` → wait for user
2. User clicks Download → `UPDATE_STATE_DOWNLOADING` → proceed

**File**: `main/crystal_update.c`

**Option A: Separate Check and Download Actions** (Recommended)

Modify state machine to pause at `AVAILABLE`:

```c
// Add new state
typedef enum {
    UPDATE_STATE_IDLE,
    UPDATE_STATE_CHECKING,
    UPDATE_STATE_AVAILABLE,        // NEW: waiting for user confirmation
    UPDATE_STATE_DOWNLOADING,
    UPDATE_STATE_READY,
    UPDATE_STATE_INSTALLING,
    UPDATE_STATE_FAILED,
    UPDATE_STATE_CURRENT,
} crystal_update_state_t;

// Add new request flag
static volatile bool s_download_requested;

static void ota_worker_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "OTA worker started on Core %d", xPortGetCoreID());
    update_manifest_t cached_manifest = {};
    bool have_manifest = false;
    
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Check request: fetch manifest and stop at AVAILABLE
        if (s_check_requested && !have_manifest) {
            s_check_requested = false;
            ESP_LOGI(TAG, "Processing update check request");
            s_cancel_requested = false;
            s_snapshot.state = UPDATE_STATE_CHECKING;
            s_snapshot.progress_percent = 0;
            notify_ota_ui();
            
            memset(s_manifest_json, 0, sizeof(s_manifest_json));
            size_t manifest_len = 0;
            esp_err_t err = fetch_manifest(MANIFEST_PRIMARY, s_manifest_json, &manifest_len);
            if (err != ESP_OK) err = fetch_manifest(MANIFEST_MIRROR, s_manifest_json, &manifest_len);
            
            if (err != ESP_OK) {
                set_error("Update source unavailable");
                continue;
            }
            
            memset(&cached_manifest, 0, sizeof(cached_manifest));
            err = crystal_update_parse_manifest(s_manifest_json, manifest_len, 
                                                EXPECTED_TARGET, EXPECTED_CHANNEL, 
                                                crystal_version_string(), &cached_manifest);
            
            if (err == ESP_ERR_NOT_FOUND) {
                s_snapshot.state = UPDATE_STATE_CURRENT;
                notify_ota_ui();
                have_manifest = false;
                continue;
            }
            
            if (err != ESP_OK) {
                set_error("Invalid update manifest");
                have_manifest = false;
                continue;
            }
            
            // Manifest is valid and newer
            ESP_LOGI(TAG, "Update available: %s (%lu bytes)", 
                     cached_manifest.version, (unsigned long)cached_manifest.size);
            memcpy(s_snapshot.available_version, cached_manifest.version, 
                   sizeof(s_snapshot.available_version));
            strlcpy(s_snapshot.notes, cached_manifest.notes, sizeof(s_snapshot.notes));
            s_snapshot.download_size = cached_manifest.size;
            s_snapshot.downloaded_bytes = 0;
            s_snapshot.progress_percent = 0;
            s_snapshot.state = UPDATE_STATE_AVAILABLE;
            notify_ota_ui();
            have_manifest = true;
            // STOP HERE - wait for download confirmation
        }
        
        // Download request: user confirmed, proceed with install
        if (s_download_requested && have_manifest) {
            s_download_requested = false;
            ESP_LOGI(TAG, "User confirmed download, starting transfer");
            s_snapshot.state = UPDATE_STATE_DOWNLOADING;
            notify_ota_ui();
            
            esp_err_t err = install_image(&cached_manifest);
            have_manifest = false;  // Clear cached manifest after use
            
            if (err != ESP_OK) {
                set_error(s_cancel_requested ? "Update cancelled" : 
                         "Update failed, nothing changed");
                continue;
            }
            
            s_snapshot.state = UPDATE_STATE_INSTALLING;
            s_snapshot.progress_percent = 100;
            strlcpy(s_snapshot.error_message, "Restarting to apply update", 
                    sizeof(s_snapshot.error_message));
            notify_ota_ui();
            ESP_LOGI(TAG, "OTA successful, rebooting in 2 seconds...");
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        }
        
        // Cancellation during AVAILABLE state
        if (s_cancel_requested && have_manifest) {
            s_cancel_requested = false;
            have_manifest = false;
            s_snapshot.state = UPDATE_STATE_IDLE;
            notify_ota_ui();
        }
    }
}

// Add new API
esp_err_t crystal_update_start_download(void)
{
    if (s_snapshot.state != UPDATE_STATE_AVAILABLE) return ESP_ERR_INVALID_STATE;
    s_download_requested = true;
    return ESP_OK;
}
```

Add to header `crystal_update_view.h`:
```c
esp_err_t crystal_update_start_download(void);
```

Update the Download button callback in crystal_shell.cpp:
```cpp
lv_obj_add_event_cb(download_btn, [](lv_event_t *e) {
    if (crystal_update_start_download() == ESP_OK) {
        ESP_LOGI(TAG, "Download initiated by user");
    }
}, LV_EVENT_CLICKED, nullptr);
```

### Step 5: Settings Page Integration (Optional but Recommended)

Add a "Software Update" tile/button in Settings → System that:
- Shows current version
- Shows "Check for Updates" button
- Shows spinner when `UPDATE_STATE_CHECKING`
- Allows cancellation during check

This is **out of scope for Phase 12.9** but should be in Phase 13 (Settings UI).

## Testing Checklist

### Scenario 1: Already Up to Date
1. Trigger update check
2. ✅ Toast appears: "System is up to date"
3. ✅ No modal or dialog shown
4. ✅ State returns to IDLE

### Scenario 2: Update Available
1. Trigger update check
2. ✅ Dialog appears with version, size, notes
3. ✅ Click "Cancel" → Dialog dismisses, no download
4. ✅ Click "Download" → Progress modal appears
5. ✅ Download completes → Device reboots

### Scenario 3: Network Failure
1. Disconnect WiFi
2. Trigger update check
3. ✅ Toast appears: "Update source unavailable"
4. ✅ No modal shown
5. ✅ State returns to IDLE (or FAILED)

### Scenario 4: Cancel During Check
1. Trigger update check
2. Immediately call `crystal_update_cancel_check()`
3. ✅ Toast appears: "Update check cancelled"
4. ✅ Worker stops checking
5. ✅ State returns to IDLE

### Scenario 5: Cancel During Download
1. Start download from Update Available dialog
2. Call `crystal_update_cancel_check()` (via Cancel button if added to modal)
3. ✅ Download stops
4. ✅ Toast: "Update cancelled"
5. ✅ No partial OTA applied

## Files to Modify

| File | Changes | Lines Changed |
|------|---------|---------------|
| `components/crystal_core/src/crystal_core.cpp` | Add toast logic in UI_EVT_OTA_CHANGED handler | ~15 |
| `components/crystal_shell/src/crystal_shell.cpp` | Add update_dialog_build(), update crystal_shell_ota_event() | ~120 |
| `main/crystal_update.c` | Split check/download logic, add s_download_requested flag | ~60 |
| `components/crystal_core/include/crystal_update_view.h` | Add crystal_update_start_download() declaration | ~1 |

**Total estimated**: ~200 lines of new/modified code

## Future Enhancements (Post-12.9)

1. **Automatic update checks**: Background timer to check daily
2. **Download progress cancellation**: Add Cancel button to download modal
3. **Settings integration**: Dedicated Software Update page
4. **Update scheduling**: "Install tonight" option
5. **Delta updates**: Only download changed partitions
6. **Rollback UI**: Show "Update failed, rolled back to X.X.X" toast on boot

## References

- Phase 12 Checklist: `docs/PHASE_12_CHECKLIST.md`
- Phase 12 Detailed Guide: `docs/PHASE_12_DETAILED_GUIDE.md`
- Crystal OS Design: `docs/DESIGN.md` (layers, gesture ownership)
- Existing code:
  - `main/crystal_update.c` (OTA worker)
  - `components/crystal_shell/src/crystal_shell.cpp` (existing modal)
  - `components/crystal_core/src/crystal_core.cpp` (toast system)
