# App Update Implementation Guide

**Version:** 1.0
**Date:** 2026-11-18
**Status:** Design + implementation guide for Phase 18+

---

## 1. Problem Statement

Currently, Crystal OS rejects installation of a `.capp` package when an app with the same ID already exists, even if the new version is higher. This forces users through an uninstall/reinstall cycle that **destroys all app data** in `/apps/<id>/data/`.

### Current Behavior

```cpp
// components/crystal_package/src/crystal_package.cpp:509
if (find_record(m.id) != s_records.end())
    return fail(error, "App already installed; uninstall before replacing");
```

This rejection happens in `crystal_package_install()` before any version comparison.

### User Impact

1. **Data loss**: Uninstall removes `/apps/<id>/data/state.json` and all cached files
2. **Poor UX**: Two-step process (uninstall → install) instead of one-click update
3. **No rollback**: Cannot revert to previous version if update breaks
4. **Settings reset**: All preferences, favorites, cached data lost

---

## 2. Design Goals

### 2.1 Core Requirements

1. **Preserve data across updates**: `/apps/<id>/data/` survives version upgrades
2. **Atomic updates**: Either complete success or rollback to old version
3. **Version validation**: Only allow upgrades (higher version), not downgrades by default
4. **Rollback support**: Keep previous version temporarily for emergency recovery
5. **Control panel integration**: Update button in web UI and on-device Manage Apps

### 2.2 Non-Goals (Deferred)

- **Delta updates**: Full `.capp` replacement only (no binary diffing)
- **Background updates**: User-initiated only; no auto-update daemon
- **Multi-app batching**: One app at a time
- **Partial updates**: No asset-only or script-only patches

---

## 3. Architecture

### 3.1 Version Comparison

Add semantic version comparison to `crystal_package.cpp`:

```cpp
enum class VersionRelation { OLDER, SAME, NEWER, INCOMPATIBLE };

VersionRelation compare_versions(const std::string &installed, const std::string &incoming) {
    std::array<unsigned, 3> v1, v2;
    if (!semver(installed, &v1) || !semver(incoming, &v2))
        return VersionRelation::INCOMPATIBLE;

    for (size_t i = 0; i < 3; ++i) {
        if (v1[i] < v2[i]) return VersionRelation::NEWER;
        if (v1[i] > v2[i]) return VersionRelation::OLDER;
    }
    return VersionRelation::SAME;
}
```

**Semver rules:**
- `1.0.0` → `1.0.1`: NEWER (patch upgrade)
- `1.0.0` → `1.1.0`: NEWER (minor upgrade)
- `1.0.0` → `2.0.0`: NEWER (major upgrade, may break data)
- `1.0.1` → `1.0.0`: OLDER (downgrade)
- `1.0.0` → `1.0.0`: SAME (reinstall)

### 3.2 Update Flow

```
User clicks "Update" in Control Panel
    ↓
1. Validate incoming .capp
    ├─ Parse manifest
    ├─ Verify signature
    └─ Compare version (must be NEWER)
    ↓
2. Stop running instance (if active)
    └─ Call onPause() → onDestroy()
    ↓
3. Atomic swap
    ├─ Extract .capp to /apps/.staging/<id>/
    ├─ Move /apps/<id>/ → /apps/.rollback.<id>/
    ├─ Move /apps/.staging/<id>/ → /apps/<id>/
    └─ Copy data: /apps/.rollback.<id>/data/ → /apps/<id>/data/
    ↓
4. Update registry
    ├─ Update version, installed_at
    ├─ Reset error_count
    └─ Save registry
    ↓
5. Cleanup
    └─ Queue .rollback.<id> for deferred removal (keep 1 boot cycle)
    ↓
Success: User sees updated version in launcher
```

### 3.3 Rollback Flow (Emergency)

If the updated app crashes 3 times consecutively:

```
Auto-disable after 3 crashes (existing behavior)
    ↓
User sees "App stopped working" in Manage Apps
    ↓
User clicks "Rollback to Previous Version" button
    ↓
1. Stop current instance (if running)
2. Move /apps/<id>/ → /apps/.failed.<id>/
3. Move /apps/.rollback.<id>/ → /apps/<id>/
4. Update registry version to old version
5. Clear error_count
6. Re-enable app
    ↓
User sees old version restored with data intact
```

**Rollback retention policy:**
- Keep `.rollback.<id>` for 1 boot cycle or 24 hours, whichever is longer
- After expiry, queue for `crystal_package_cleanup_step()`
- Only one rollback per app (no multi-version history)

---

## 4. Implementation Plan

### Phase 1: Version Comparison Logic

**File:** `components/crystal_package/src/crystal_package.cpp`

1. Add `compare_versions()` helper function
2. Add unit tests in `tools/test_phase18_update.cpp`

**Test cases:**
```cpp
assert(compare_versions("1.0.0", "1.0.1") == VersionRelation::NEWER);
assert(compare_versions("1.0.0", "2.0.0") == VersionRelation::NEWER);
assert(compare_versions("1.0.1", "1.0.0") == VersionRelation::OLDER);
assert(compare_versions("1.0.0", "1.0.0") == VersionRelation::SAME);
assert(compare_versions("invalid", "1.0.0") == VersionRelation::INCOMPATIBLE);
```

### Phase 2: Atomic Update Function

**New function:** `crystal_package_update()`

```cpp
bool crystal_package_update(FILE *stream, bool allow_downgrade,
                           std::string *error,
                           crystal_package_verify_t verifier = nullptr);
```

**Implementation steps:**

1. **Parse and validate incoming manifest**
   ```cpp
   CrystalPackageManifest new_manifest;
   if (!parse_manifest_from_stream(stream, &new_manifest, error)) return false;

   auto it = find_record(new_manifest.id);
   if (it == s_records.end()) {
       return fail(error, "App not installed; use install instead of update");
   }

   VersionRelation rel = compare_versions(it->manifest.version, new_manifest.version);
   if (rel == VersionRelation::SAME) {
       return fail(error, "Same version already installed");
   }
   if (rel == VersionRelation::OLDER && !allow_downgrade) {
       return fail(error, "Downgrade not allowed; enable Developer Mode to override");
   }
   ```

2. **Stop running instance**
   ```cpp
   // Hook into crystal_registry to stop the app if running
   if (!crystal_registry_stop_if_running(new_manifest.id.c_str())) {
       return fail(error, "Could not stop running app");
   }
   ```

3. **Atomic directory swap**
   ```cpp
   const std::string app_dir = s_root + "/" + new_manifest.id;
   const std::string rollback_dir = s_root + "/.rollback." + new_manifest.id;
   const std::string staging_dir = s_root + "/.staging";

   // Extract new version to staging
   if (!extract_package_to_staging(stream, new_manifest, staging_dir, verifier, error)) {
       return false;
   }

   // Atomic swap
   if (rename(app_dir.c_str(), rollback_dir.c_str()) != 0) {
       return fail(error, "Could not move old version to rollback");
   }

   if (rename(staging_dir.c_str(), app_dir.c_str()) != 0) {
       // Rollback: restore old version
       rename(rollback_dir.c_str(), app_dir.c_str());
       return fail(error, "Could not commit new version");
   }
   ```

4. **Preserve app data**
   ```cpp
   // Copy data/ from rollback to new version
   const std::string old_data = rollback_dir + "/data";
   const std::string new_data = app_dir + "/data";

   if (!copy_directory(old_data, new_data)) {
       // Non-fatal: warn but don't fail the update
       ESP_LOGW("crystal_package", "Could not copy data from previous version");
   }
   ```

5. **Update registry**
   ```cpp
   it->manifest = new_manifest;
   it->installed_at = static_cast<uint32_t>(time(nullptr));
   it->error_count = 0;  // Reset crash counter

   if (!save_records()) {
       // Critical: rollback the filesystem changes
       remove_tree(app_dir);
       rename(rollback_dir.c_str(), app_dir.c_str());
       return fail(error, "Could not update registry");
   }
   ```

6. **Queue rollback cleanup**
   ```cpp
   // Keep .rollback.<id> for 1 boot cycle
   s_rollback_retention[new_manifest.id] = time(nullptr) + 86400; // 24 hours
   ```

### Phase 3: Control Panel Integration

**Files to modify:**

1. **HTTP API** (`components/crystal_control/src/crystal_control.cpp`)

   Add new endpoint:
   ```cpp
   POST /api/v1/apps/{id}/update

   Request: multipart/form-data with .capp file
   Response: {"success": true, "new_version": "1.2.0", "old_version": "1.1.0"}
   ```

   Implementation:
   ```cpp
   if (request->uri == "/api/v1/apps/" + id + "/update") {
       FILE *stream = fopen(uploaded_file, "rb");
       std::string error;
       bool ok = crystal_package_update(stream, developer_mode, &error);
       fclose(stream);

       if (!ok) {
           send_json_response(request, 400, {{"error", error}});
       } else {
           crystal_registry_refresh_catalog();
           send_json_response(request, 200, {
               {"success", true},
               {"new_version", updated_record->manifest.version},
               {"old_version", old_version}
           });
       }
   }
   ```

2. **Web UI** (`components/crystal_control/web/apps.html`)

   Add "Update" button when new version detected:
   ```html
   <div class="app-actions">
       <button v-if="app.update_available" @click="updateApp(app.id)">
           Update to {{app.latest_version}}
       </button>
       <button @click="launchApp(app.id)">Launch</button>
       <button @click="uninstallApp(app.id)">Uninstall</button>
   </div>
   ```

   ```javascript
   async updateApp(id) {
       const file = await selectFile('.capp');
       const formData = new FormData();
       formData.append('package', file);

       const response = await fetch(`/api/v1/apps/${id}/update`, {
           method: 'POST',
           headers: {'Authorization': `Bearer ${token}`},
           body: formData
       });

       if (response.ok) {
           const result = await response.json();
           alert(`Updated from ${result.old_version} to ${result.new_version}`);
           this.refreshAppList();
       } else {
           const error = await response.json();
           alert(`Update failed: ${error.error}`);
       }
   }
   ```

3. **CLI** (`tools/crystal.py`)

   Add `crystal update` command:
   ```bash
   crystal update --usb ./com.example.myapp-1.2.0.capp
   crystal update --wifi http://192.168.1.20/ --token TOKEN ./myapp-1.2.0.capp
   ```

   Implementation:
   ```python
   def cmd_update(args):
       if args.usb:
           # USB serial transport
           with open(args.package, 'rb') as f:
               response = usb_send_update(f.read())
       else:
           # WiFi HTTP transport
           with open(args.package, 'rb') as f:
               response = requests.post(
                   f"{args.wifi}/api/v1/apps/{extract_id(args.package)}/update",
                   headers={'Authorization': f'Bearer {args.token}'},
                   files={'package': f}
               )

       if response.status_code == 200:
           data = response.json()
           print(f"✓ Updated from {data['old_version']} to {data['new_version']}")
       else:
           print(f"✗ Update failed: {response.json()['error']}")
           sys.exit(1)
   ```

### Phase 4: On-Device Manage Apps

**File:** `components/crystal_shell/src/manage_apps_page.cpp`

Add "Update" action to app detail page when newer version is available:

```cpp
// In app detail page button matrix
if (app_has_update_available(app_id)) {
    add_button("Update to v" + pending_version, BTN_UPDATE);
}
add_button("Launch", BTN_LAUNCH);
add_button("Disable", BTN_DISABLE);
add_button("Clear Data", BTN_CLEAR_DATA);
add_button("Uninstall", BTN_UNINSTALL);
```

**Update trigger flow:**

1. User transfers new `.capp` via WiFi panel
2. On-device panel detects version mismatch
3. "Update" button appears
4. Tap → confirmation dialog → update process
5. Show progress bar (reuse install progress)
6. Success → "Updated to v1.2.0"

### Phase 5: Rollback Support

**Files to modify:**

1. **Add rollback retention tracking**

   ```cpp
   // In crystal_package.cpp
   struct RollbackEntry {
       std::string id;
       time_t expires_at;
       std::string old_version;
   };
   std::map<std::string, RollbackEntry> s_rollback_retention;

   bool crystal_package_rollback(const std::string &id, std::string *error);
   ```

2. **Rollback function**

   ```cpp
   bool crystal_package_rollback(const std::string &id, std::string *error) {
       auto it = s_rollback_retention.find(id);
       if (it == s_rollback_retention.end()) {
           return fail(error, "No rollback available for this app");
       }

       if (time(nullptr) > it->second.expires_at) {
           return fail(error, "Rollback expired");
       }

       const std::string app_dir = s_root + "/" + id;
       const std::string rollback_dir = s_root + "/.rollback." + id;
       const std::string failed_dir = s_root + "/.failed." + id;

       // Stop running instance
       crystal_registry_stop_if_running(id.c_str());

       // Move failed version aside
       if (rename(app_dir.c_str(), failed_dir.c_str()) != 0) {
           return fail(error, "Could not move failed version");
       }

       // Restore old version
       if (rename(rollback_dir.c_str(), app_dir.c_str()) != 0) {
           // Try to restore failed version
           rename(failed_dir.c_str(), app_dir.c_str());
           return fail(error, "Could not restore previous version");
       }

       // Update registry
       auto record = find_record(id);
       if (record != s_records.end()) {
           record->manifest.version = it->second.old_version;
           record->error_count = 0;
           record->enabled = true;
           save_records();
       }

       // Queue failed version for cleanup
       s_removals.push_back(failed_dir);
       s_removal_pending = true;

       s_rollback_retention.erase(it);
       return true;
   }
   ```

3. **Manage Apps UI**

   Add "Rollback" button when app is disabled due to crashes and rollback is available:

   ```cpp
   if (!app_enabled && app_error_count >= 3 && has_rollback(app_id)) {
       add_button("Rollback to Previous Version", BTN_ROLLBACK);
   }
   ```

### Phase 6: Boot Cleanup

**Modify boot reconciliation** to handle rollback expiry:

```cpp
// In crystal_packages_init()

// Scan for expired rollbacks
for (auto it = s_rollback_retention.begin(); it != s_rollback_retention.end();) {
    if (time(nullptr) > it->second.expires_at) {
        const std::string rollback_dir = s_root + "/.rollback." + it->first;
        s_removals.push_back(rollback_dir);
        it = s_rollback_retention.erase(it);
    } else {
        ++it;
    }
}

// Clean up orphaned rollback directories
DIR *dir = opendir(s_root.c_str());
struct dirent *entry;
while ((entry = readdir(dir)) != nullptr) {
    if (strncmp(entry->d_name, ".rollback.", 10) == 0) {
        std::string id = entry->d_name + 10;
        if (s_rollback_retention.find(id) == s_rollback_retention.end()) {
            // Orphaned rollback, clean it up
            s_removals.push_back(s_root + "/" + entry->d_name);
        }
    }
}
closedir(dir);

if (!s_removals.empty()) s_removal_pending = true;
```

---

## 5. Data Migration Considerations

### 5.1 Breaking Changes

When a major version update changes the data schema:

**Option A: App-handled migration**

Apps implement optional lifecycle hook:

```lua
-- In main.lua
return {
    onMigrate = function(old_version, new_version)
        if old_version:match("^1%.") and new_version:match("^2%.") then
            -- Migrate v1 -> v2 schema
            local old_data = crystal.state.get("favorites")
            local new_data = migrate_favorites_v1_to_v2(old_data)
            crystal.state.set("favorites", new_data)
        end
        return true
    end,

    onCreate = function() ... end,
    ...
}
```

Host calls `onMigrate` once after update, before first `onCreate`.

**Option B: Versioned state keys**

Apps namespace their state by schema version:

```lua
-- Read with fallback
local data = crystal.state.get("favorites_v2") or
             migrate_v1_to_v2(crystal.state.get("favorites_v1"))

-- Always write to current version
crystal.state.set("favorites_v2", data)
```

**Recommendation:** Start with Option B (simpler, no new ABI). Add Option A in Phase 19 if needed.

### 5.2 Asset Compatibility

If new version removes an asset that cached data references:

```lua
-- Defensive loading
local icon_path = "assets/" .. item.icon
if not crystal.fs.exists(icon_path) then
    icon_path = "assets/icon_default.bin"  -- Fallback
end
crystal.ui.image(parent, {src = icon_path})
```

Apps should handle missing assets gracefully, not crash.

---

## 6. Security Considerations

### 6.1 Signature Verification

Updates must verify signatures with the same rigor as fresh installs:

```cpp
// In crystal_package_update()
if (!verifier || !verifier(stream, signed_length, signature, key)) {
    return fail(error, "Update signature not trusted");
}
```

**Key rotation:** If developer changes signing key, old apps cannot update. Require uninstall/reinstall for key changes.

### 6.2 Downgrade Protection

By default, reject downgrades to prevent rollback attacks:

```cpp
if (rel == VersionRelation::OLDER && !developer_mode) {
    return fail(error, "Downgrade not allowed without Developer Mode");
}
```

Developer Mode allows downgrades for testing.

### 6.3 Data Preservation Risks

Copying `data/` preserves potential malicious state. Mitigations:

1. **Size check**: Reject updates if `old_data/` + `new_assets/` exceed storage quota
2. **Quota reset**: Apply new manifest's `storage_kb` limit to preserved data
3. **State validation**: Apps should validate loaded state on each launch (already recommended)

---

## 7. Testing Plan

### 7.1 Host Tests

**File:** `tools/test_phase18_update.cpp`

Test cases:

```cpp
TEST(PackageUpdate, AcceptsNewerVersion) {
    // Install v1.0.0
    install_package("com.example.test", "1.0.0");

    // Update to v1.0.1
    ASSERT_TRUE(crystal_package_update(stream_v101, false, &error));

    // Verify registry shows v1.0.1
    auto record = find_record("com.example.test");
    ASSERT_EQ(record->manifest.version, "1.0.1");
}

TEST(PackageUpdate, RejectsDowngrade) {
    install_package("com.example.test", "1.0.1");

    // Try to downgrade to v1.0.0
    ASSERT_FALSE(crystal_package_update(stream_v100, false, &error));
    ASSERT_STREQ(error.c_str(), "Downgrade not allowed; enable Developer Mode to override");
}

TEST(PackageUpdate, PreservesData) {
    install_package("com.example.test", "1.0.0");

    // Write test data
    const char *value = "test_value";
    crystal_package_state_set("com.example.test", "key1", value, strlen(value));

    // Update to v1.0.1
    ASSERT_TRUE(crystal_package_update(stream_v101, false, &error));

    // Verify data still readable
    char buffer[32];
    size_t len = sizeof(buffer);
    ASSERT_TRUE(crystal_package_state_get("com.example.test", "key1", buffer, &len));
    ASSERT_STREQ(buffer, "test_value");
}

TEST(PackageUpdate, RollbackRestoresOldVersion) {
    install_package("com.example.test", "1.0.0");
    crystal_package_update(stream_v101, false, &error);

    // Simulate 3 crashes
    for (int i = 0; i < 3; ++i) {
        crystal_package_launch_result("com.example.test", false);
    }

    // Rollback
    ASSERT_TRUE(crystal_package_rollback("com.example.test", &error));

    // Verify registry shows v1.0.0
    auto record = find_record("com.example.test");
    ASSERT_EQ(record->manifest.version, "1.0.0");
}
```

### 7.2 Device Tests

**Scenario 1: Clock app update**

1. Install `examples/clock3p-1.0.0.capp`
2. Launch, set a timer, pause
3. Update to `clock3p-1.1.0.capp` (adds alarm feature)
4. Launch, verify timer still present
5. Verify new alarm UI appears

**Scenario 2: Rollback after crash**

1. Install Bus Lite v0.2.2
2. Add 3 favorite stops
3. Update to v0.3.0 (simulated buggy version)
4. Launch 3 times, each time trigger crash (infinite loop)
5. App auto-disabled
6. Open Manage Apps, tap "Rollback"
7. Verify v0.2.2 restored and 3 favorites intact

**Scenario 3: WiFi panel update**

1. Connect to device WiFi panel
2. Navigate to Apps page
3. Upload `weather-2.0.0.capp` (existing is v1.5.0)
4. Click "Update" button
5. Observe progress bar
6. Verify success message and new version shown
7. Launch Weather, verify data preserved (saved location)

### 7.3 Edge Cases

- **Power loss during update**: `.staging` or `.rollback.*` directories recovered at boot
- **Full storage during update**: Admission control rejects before starting
- **Corrupt new package**: Signature verification fails, old version untouched
- **Network timeout during WiFi update**: HTTP layer handles; package remains incomplete
- **Update to same version**: Rejected with "Same version already installed"

---

## 8. Performance Impact

### 8.1 Time Estimates

| Operation | Time (typical) | Notes |
|-----------|----------------|-------|
| Version comparison | <1ms | Semver parse + compare |
| Stop running app | 50-200ms | `onPause()` + `onDestroy()` |
| Extract to staging | 2-5s | 500 KB package over WiFi |
| Directory rename | 10-50ms | LittleFS atomic operation |
| Copy data/ | 100-500ms | Depends on cached file count |
| Registry write | 20-50ms | NVS blob write |
| **Total update time** | **3-8s** | For typical 500 KB package |

### 8.2 Storage Overhead

During update, peak storage usage:

```
Old version:     /apps/<id>/          (e.g., 500 KB)
New version:     /apps/.staging/      (e.g., 520 KB)
Rollback copy:   /apps/.rollback.<id>/ (e.g., 500 KB)

Peak usage: 1.52 MB (3× app size)
```

**Admission control** must check: `3 * new_package_size < available_storage`

### 8.3 Memory Overhead

Update runs on the management task (same as install):

- Manifest parsing: ~8 KB heap
- Signature verification: ~16 KB stack
- File buffers: 2 KB
- **Total: ~26 KB additional RAM** during update

---

## 9. Future Enhancements (Phase 19+)

### 9.1 Catalog Update Notifications

Fetch catalog manifest periodically:

```json
{
  "apps": [
    {
      "id": "com.example.weather",
      "latest_version": "2.1.0",
      "download_url": "https://catalog.example.com/weather-2.1.0.capp",
      "changelog": "Added hourly forecast"
    }
  ]
}
```

Show badge in Manage Apps when updates available.

### 9.2 Background Updates

With user consent, download and stage updates during idle:

1. WiFi connected + plugged in → fetch catalog
2. Download to `/apps/.pending.<id>.capp`
3. User sees "Update ready" notification
4. Tap → apply staged update (no download wait)

### 9.3 Multi-App Batch Updates

"Update All" button:

```
For each app with update available:
    Download → stage → verify
If all succeed:
    Apply all updates atomically
Else:
    Rollback all, report which failed
```

Requires transaction log to handle power loss mid-batch.

### 9.4 Delta Updates

Binary diffing to reduce bandwidth:

```
Old: weather-1.0.0.capp (500 KB)
New: weather-1.0.1.capp (520 KB)
Delta: weather-1.0.0-to-1.0.1.delta (50 KB)
```

Apply with `bsdiff`/`bspatch`. Saves WiFi bandwidth for large apps.

---

## 10. Summary

### Changes Required

1. **`crystal_package.cpp`**:
   - Add `compare_versions()` helper
   - Add `crystal_package_update()` function
   - Add `crystal_package_rollback()` function
   - Add rollback retention tracking
   - Modify boot cleanup to handle `.rollback.*` directories

2. **`crystal_control.cpp`**:
   - Add `POST /api/v1/apps/{id}/update` endpoint
   - Add `POST /api/v1/apps/{id}/rollback` endpoint

3. **Control panel web UI**:
   - Add "Update" button when new version detected
   - Add "Rollback" button for failed updates

4. **`crystal.py`**:
   - Add `crystal update` command
   - Add `crystal rollback` command

5. **Manage Apps** (`manage_apps_page.cpp`):
   - Add "Update" action in app detail page
   - Add "Rollback" action when crashes detected

6. **Tests**:
   - Host unit tests in `tools/test_phase18_update.cpp`
   - Device integration tests with Clock and Bus Lite
   - Edge case testing (power loss, network timeout, rollback expiry)

### Exit Criteria

- [x] Version comparison implemented and tested
- [x] `crystal_package_update()` works end-to-end on host
- [ ] Device test: Update Clock v1.0.0 → v1.1.0, timer data preserved
- [ ] Device test: Update Bus Lite, favorites preserved
- [ ] WiFi panel "Update" button functional
- [ ] CLI `crystal update` works over USB and WiFi
- [ ] Rollback after 3 crashes restores old version with data intact
- [ ] Boot cleanup removes expired rollbacks
- [ ] No resource leaks detected across 10 update cycles

### Estimated Effort

- **Phase 1-2 (Core logic)**: 2-3 days
- **Phase 3 (Control panel integration)**: 2 days
- **Phase 4 (On-device UI)**: 1 day
- **Phase 5 (Rollback)**: 2 days
- **Phase 6 (Boot cleanup)**: 1 day
- **Testing**: 2-3 days
- **Total**: ~10-12 days for one developer

---

## 11. Open Questions

1. **Rollback retention**: 24 hours or 1 boot cycle? Current proposal: whichever is longer.
2. **Auto-update consent**: Should OS support opt-in auto-update, or always manual?
3. **Data migration ABI**: Add `onMigrate()` lifecycle hook now, or defer to Phase 19?
4. **Downgrade policy**: Always require Developer Mode, or allow user override in Manage Apps?
5. **Update progress granularity**: Show bytes transferred, or just spinner?

**Decision needed before implementation**: Confirm update flow with Bus app team, since they're the primary user reporting the pain.
