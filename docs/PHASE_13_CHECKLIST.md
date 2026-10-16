# Phase 13 Implementation Checklist

**Purpose:** Track progress through Phase 13 sub-phases with visible validation checkpoints.

**How to use:** Check off each item as you complete it. Each sub-phase must pass its validation before moving to the next.

---

## Progress Overview

- [x] 13.1 Registry Query API (verified 2026-10-15)
- [x] 13.2 CrystalState Clear (verified 2026-10-15)
- [x] 13.3 Manage Apps Page UI (3h)
- [x] 13.4 Install/Uninstall Logic (3h)
- [x] 13.5 Reorder with Drag Handles (3h)
- [ ] 13.6 Clear Data Confirmation (2h)
- [ ] 13.7 Apply Changes Live (4h)
- [ ] 13.8 Full Validation Suite (4h)

**Current status:** Phases 13.1–13.5 implemented and validated; Phase 13.6 and later remain pending

**Total: ~23 hours (~3 days)**

---

## Sub-Phase 13.1: Registry Query API — complete

### Tasks
- [x] Review existing registry API in crystal_registry.hpp
- [x] Add temporary test function to verify APIs
- [x] Test crystal_registry_installed_count()
- [x] Test crystal_registry_installed_id() iteration
- [x] Test crystal_registry_enabled() get/set
- [x] Test crystal_registry_slot() get/set

### Validation Checkpoints
- [x] Build succeeds
- [x] Test logs show all installed apps
- [x] Set/get roundtrip works for enabled flag
- [x] All functions return expected values

Evidence: [Phase 13.1 validation](PHASE_13_1_VALIDATION.md). Host regression
coverage and 32 device checks passed; temporary boot diagnostics were removed
and the final signed firmware was rebuilt, flashed, and boot-checked.

### Commit Message
```
Phase 13.1: Verify registry query API

- Confirmed crystal_registry_installed_count() works
- Confirmed crystal_registry_installed_id() iteration works
- Confirmed crystal_registry_enabled() get/set works
- Confirmed crystal_registry_slot() get/set works

Checkpoint: All registry query functions return expected values
```

---

## Sub-Phase 13.2: CrystalState Clear — complete

### Tasks
- [x] Add clear() declaration to crystal_app.hpp (CrystalState class)
- [x] Implement clear() in crystal_app.cpp
- [x] Iterate NVS namespace
- [x] Filter keys by app prefix
- [x] Erase collected keys and commit
- [x] Add temporary test function

### Validation Checkpoints
- [x] Build succeeds
- [x] Test writes keys to NVS
- [x] clear() returns true
- [x] get() returns false after clear (keys erased)
- [x] Test logs show "Cleared N keys under prefix"

Evidence: [Phase 13.2 validation](PHASE_13_2_VALIDATION.md). Host tests and
27 device checks passed; temporary tests removed and final firmware flashed.

### Commit Message
```
Phase 13.2: Add CrystalState::clear()

- Implement clear() to erase all keys under app's prefix
- Iterate NVS namespace and filter by prefix
- Erase collected keys and commit

Checkpoint: Unit test confirms all keys under prefix are erased
```

---

## Sub-Phase 13.3: Manage Apps Page UI ✓

### Tasks
- [x] Create manage_apps_page.{h,cpp} in crystal_shell/src
- [x] Implement manage_apps_page_open()
- [x] Build page with header and back button
- [x] Create scrollable list
- [x] Iterate full firmware catalog (including disabled apps)
- [x] Build rows: ≡ handle, app name, Install/Installed button, Clear button
- [x] Add "Manage Apps" button to Settings
- [x] Update CMakeLists.txt to include new files

### Validation Checkpoints
- [x] Build succeeds
- [x] Settings shows "Manage Apps" button
- [x] Click event opens Manage Apps page (original device automation; root placement source-checked)
- [x] All apps listed (clock, weather, calculator, dev_tester)
- [x] Enabled apps show "Installed" and "Clear"
- [x] Disabled apps show "Install" only (real-LVGL host test)
- [x] Back button returns to Settings

Manual touchscreen/gesture feel remains a user acceptance check. Automated
checks and rendering limits are recorded in [validation](PHASE_13_3_VALIDATION.md).

### Commit Message
```
Phase 13.3: Create Manage Apps page UI

- Add manage_apps_page_open() to shell
- Build list of all apps with current enabled state
- Show ≡ drag handle, app name, Install/Installed button
- Show Clear button for enabled apps only
- Add 'Manage Apps' button in Settings

Checkpoint: Page displays all apps with correct enabled state
```

---

## Sub-Phase 13.4: Install/Uninstall Logic ✓

### Tasks
- [x] Add toggle_btn_cb() event handler
- [x] Get current enabled state
- [x] Call crystal_registry_set_enabled() with flipped value
- [x] Refresh target row after successful toggle (preserves scroll/event target)
- [x] Bind catalog entries to row callbacks; avoid persistent widget pointers

### Validation Checkpoints
- [x] Build succeeds
- [x] Tap "Installed" changes to "Install"
- [x] "Clear" button disappears when disabled
- [x] Tap "Install" changes back to "Installed"
- [x] "Clear" button reappears when enabled
- [x] Logs show "Toggling app: X -> Y"
- [x] Toggle state persists across reboot

### Commit Message
```
Phase 13.4: Wire install/uninstall toggle

- Add toggle_btn_cb() to handle Install/Installed button
- Call crystal_registry_set_enabled() to toggle flag
- Refresh the target row after a successful save
- Keep at least one app enabled and report save failures

Checkpoint: Toggle changes enabled flag and persists to NVS
```

Validation: [Phase 13.4 results](PHASE_13_4_VALIDATION.md). Changes apply after
restart; Clear and drag remain pending. Manage Apps stays directly under Settings.

---

## Sub-Phase 13.5: Reorder with Drag Handles ✓

### Tasks
- [x] Make ≡ handle interactive (LV_OBJ_FLAG_CLICKABLE)
- [x] Add handle_drag_start() callback
- [x] Lift the complete row and leave a placeholder in the list
- [x] Shift rows as the dragged row crosses their centers
- [x] Add handle_drag_end() callback
- [x] Save the resulting row order as slot values
- [x] Restore the previous order if a slot write fails
- [x] Keep the saved order visible after release

### Validation Checkpoints
- [x] Build succeeds
- [x] Press and drag ≡ handle
- [x] The complete row follows the finger
- [x] Placeholder and intervening rows shift during movement
- [x] Release places the row at its new position
- [x] New slot order is written and survives a rebuild
- [ ] Verify the physical board gesture and reboot after flashing

### Commit Message
```
Phase 13.5: Add drag-to-reorder

- Make ≡ handle interactive (LV_OBJ_FLAG_CLICKABLE)
- Add drag_start/move/end handlers
- Swap slot numbers on drop
- Rebuild list to show new order immediately

Checkpoint: Drag handle moves app position, persists to NVS
```

Validation: [Phase 13.5 preparation](PHASE_13_5_VALIDATION.md). Manage Apps remains directly under Settings; 13.6 clear confirmation is next.

---

## Sub-Phase 13.6: Clear Data Confirmation ✓ / ✗

### Tasks
- [ ] Add show_clear_confirmation() function
- [ ] Create modal dialog with backdrop
- [ ] Show app name and warning message
- [ ] Add Cancel and Clear Data buttons
- [ ] Wire clear_data_confirmed() callback
- [ ] Call CrystalState::clear() on confirmation
- [ ] Wire clear_data_cancelled() callback
- [ ] Wire Clear button to show_clear_confirmation()

### Validation Checkpoints
- [ ] Build succeeds
- [ ] Tap "Clear" on enabled app
- [ ] Dialog appears with "Clear App Data" title
- [ ] Message shows correct app name
- [ ] Warning about irreversibility shown
- [ ] Tap "Cancel" closes dialog without erasing
- [ ] Tap "Clear Data" closes dialog and erases
- [ ] Logs show "Clearing data for X"
- [ ] Logs show "Successfully cleared data for X"
- [ ] Open app confirms state reset

### Commit Message
```
Phase 13.6: Add clear data confirmation

- Create confirmation dialog with Cancel and Clear Data buttons
- Call CrystalState::clear() on confirmation
- Show warning message about irreversibility
- Wire Clear button to show_clear_confirmation()

Checkpoint: Clear-data shows dialog and erases on confirm
```

---

## Sub-Phase 13.7: Apply Changes Live ✓ / ✗

### Tasks
- [ ] Add crystal_registry_apply_changes() to registry
- [ ] Save phone pointer in crystal_registry_install()
- [ ] Iterate kApps and check enabled state
- [ ] Install newly enabled apps immediately
- [ ] Note that uninstall requires reboot
- [ ] Call apply_changes() from toggle_btn_cb() when enabling
- [ ] Add UI note about enable vs disable behavior
- [ ] Update header file with new function

### Validation Checkpoints
- [ ] Build succeeds
- [ ] Disable calculator (note about reboot appears)
- [ ] Exit to launcher (calculator still visible)
- [ ] Reboot device
- [ ] Calculator not in launcher (confirm disable worked)
- [ ] Enable calculator in Manage Apps
- [ ] Logs show "Applied changes live"
- [ ] Exit to launcher
- [ ] Calculator appears immediately (no reboot needed)
- [ ] Open calculator, verify it works

### Commit Message
```
Phase 13.7: Apply enable changes live (disable requires reboot)

- Add crystal_registry_apply_changes() to install newly enabled apps
- Call from toggle when enabling an app
- Note in UI that disable requires reboot (Brookesia limitation)
- Newly enabled apps appear in launcher immediately

Checkpoint: Enable app appears in launcher without reboot
```

---

## Sub-Phase 13.8: Full Validation Suite ✓ / ✗

### Pre-Flight Checklist
- [ ] All previous sub-phases completed
- [ ] All sub-phase validations passed
- [ ] Phase 12 still stable
- [ ] No build warnings

### UI Navigation Tests
- [ ] Open Manage Apps from Settings
- [ ] Back button returns to Settings
- [ ] Page shows all apps correctly

### App List Display Tests
- [ ] All kApps apps shown
- [ ] Enabled apps show "Installed"
- [ ] Disabled apps show "Install"
- [ ] Enabled apps show "Clear" button
- [ ] Disabled apps hide "Clear" button

### Install/Uninstall Tests
- [ ] Tap "Install" on disabled app
- [ ] Button changes to "Installed", Clear appears
- [ ] Newly enabled app appears in launcher
- [ ] Open newly enabled app works
- [ ] Tap "Installed" on enabled app
- [ ] Button changes to "Install", Clear disappears
- [ ] Reboot, disabled app not in launcher
- [ ] NVS persists enabled state

### Reorder Tests
- [ ] Drag ≡ handle shows overlay
- [ ] Drop on different position swaps
- [ ] New order persists across reboot
- [ ] Launcher reflects new order

### Clear Data Tests
- [ ] Tap "Clear" shows dialog
- [ ] Dialog shows correct app name
- [ ] Tap "Cancel" closes without erasing
- [ ] Tap "Clear Data" erases data
- [ ] App state reset confirmed
- [ ] Re-open app starts fresh

### Edge Case Tests
- [ ] Enable all apps
- [ ] Disable all apps
- [ ] Reorder with only 2 apps
- [ ] Clear data on app with no data
- [ ] Rapid toggle no race condition

### Phase 12 Regression Tests
- [ ] OTA update still works
- [ ] Device Status shows version
- [ ] Last Crash row present
- [ ] Settings navigation intact
- [ ] Launcher app switching works
- [ ] WiFi scan and connect work

### Documentation Updates
- [ ] Update CODE_GUIDE.md with Manage Apps section
- [ ] Update DESIGN.md with registry management
- [ ] Create release notes for Phase 13

### Final Commit
```
Phase 13.8: Complete validation suite

Tested:
- All UI navigation and display
- Install/uninstall with live enable
- Drag-to-reorder with persistence
- Clear data with confirmation
- Edge cases (all enabled, all disabled, etc.)
- Phase 12 regressions (OTA, Device Status, crash tracking)

All test cases passed. Phase 13 complete.
```

---

## Final Checklist

### Before Marking Phase 13 Complete
- [ ] All 8 sub-phases completed
- [ ] All validation checkpoints passed
- [ ] All commits pushed to repository
- [ ] Documentation updated (CODE_GUIDE.md, DESIGN.md)
- [ ] No known regressions
- [ ] Phase 12 features still work

### Phase 13 Deliverables
- [ ] Manage Apps page functional
- [ ] Install/uninstall works
- [ ] Drag-to-reorder works
- [ ] Clear data works
- [ ] Enable takes effect immediately
- [ ] Disable takes effect on reboot
- [ ] All state persists to NVS
- [ ] Comprehensive test results documented

---

## Notes

**Safety Principle:** Each sub-phase produces a visible, testable change. Commit after passing validation.

**If a Sub-Phase Fails:**
1. Review the detailed guide in `PHASE_13_DETAILED_GUIDE.md`
2. Check logs for specific error messages
3. Verify all prerequisites from previous sub-phases
4. Test each validation checkpoint individually
5. Do not proceed to next sub-phase until current one passes

**Known Limitations:**
- Uninstall requires reboot (Brookesia doesn't expose uninstallApp() by ID)
- Can only install what shipped in firmware (download in v2)
- Reorder may require registry re-sort on next boot

**Estimated Timeline:**
- Ideal case: 3 days (23 hours)
- With testing/debugging: 4-5 days
- First-time implementation: 1 week

**Document Version:** 1.0  
**Created:** 2026-10-13
