# Phase 13 Quick Reference

**One-page summary of Phase 13 sub-phases with visible checkpoints.**

---

## Sub-Phase Quick View

| # | Name | Checkpoint | Time |
|---|------|-----------|------|
| 13.1 | Registry Query API | Query functions return correct app states | 2h |
| 13.2 | CrystalState Clear | Clear-data erases app's NVS prefix | 2h |
| 13.3 | Manage Apps Page UI | Page displays all apps with current state | 3h |
| 13.4 | Install/Uninstall Logic | Toggle saves enabled flag; restart applies | 3h |
| 13.5 | Reorder with Drag Handles | Drag the whole row; rows shift and restart applies | 3h |
| 13.6 | Clear Data Confirmation — complete | Confirmation resets saved app data immediately; device verified 2026-10-17 | 2h |
| 13.7 | Apply Changes Live | Enable reflects without reboot (disable needs reboot) | 4h |
| 13.8 | Validation Suite | All test cases pass | 4h |

---

## Key Commands

### Build
```bash
# Dev build
./scripts/build_dev.sh

# Flash and monitor
idf.py flash monitor
```

### Testing (Phase 13.1)
```bash
python3 tools/test_crystal_registry.py
```

The serial monitor displays logs; it is not a C++ REPL. Device tests run
temporarily after registry installation and before shell initialization. Remove
the test, rebuild, and reflash after validation. Preserve saved enabled/slot
values and originally absent keys. See [validation results](PHASE_13_1_VALIDATION.md).

Installed queries list only successfully installed, enabled apps; they do not
expose disabled firmware entries. Phase 13.3 uses catalog_count/catalog_entry
for all firmware entries. Read with each catalog entry's defaults.

---

## API Summary

### Registry Query (Phase 13.1)
```c
// Already exists in crystal_registry.hpp:
bool     crystal_registry_enabled(const char *id, bool default_value);
uint16_t crystal_registry_slot(const char *id, uint16_t default_value);
bool     crystal_registry_set_enabled(const char *id, bool enabled);
bool     crystal_registry_set_slot(const char *id, uint16_t slot);
size_t   crystal_registry_installed_count();
const char *crystal_registry_installed_id(size_t index);
```

### State Management (Phase 13.2)
```cpp
// Implemented in Phase 13.2:
bool clear();   // Erase all keys under this app's prefix
```

Host test: `python3 tools/test_crystal_state.py`. Clear returns true for an
empty prefix, false on storage errors (possibly after partial erasure). Use
`app->state().clear()` with app writers stopped; prefixes use constructor names,
not registry IDs. This erases persisted app state only.

---

## Manage Apps (Phases 13.3–13.5)

Open Settings > Manage Apps. All firmware entries appear, including disabled
apps, sorted by saved slot (entry defaults if absent). Tap Installed to uninstall
or Install to enable; the selection is saved immediately and applied on restart.
Clear appears only for enabled entries but remains disabled. Drag a bars handle
to lift the whole row; rows shift as it crosses other rows and release saves the
new launcher order. The UI reports save errors and keeps at least one app enabled so the
shell remains bootable. Uninstall preserves app data.

Host render/check: python3 tools/test_manage_apps_page.py.
Results: [Phase 13.4 validation](PHASE_13_4_VALIDATION.md).

## Full Phase 13 UI Flow (later actions pending)

```
Settings › Manage Apps
    |
    +-> List of all apps (enabled + disabled)
    |   |
    |   +-> [≡] App Name    [Installed] [Clear]    (enabled)
    |   +-> [≡] App Name    [Install  ]            (disabled)
    |
    +-> Drag the whole row by its ≡ handle; rows shift as it crosses (13.5)
    |
    +-> Tap [Install]/[Installed] to toggle (restart to apply)
    |
    +-> Tap [Clear] → confirmation dialog → reset saved app data immediately (13.6 complete)
```

---

## File Structure

### New Files
- `components/crystal_shell/src/manage_apps_page.{h,cpp}` — Manage Apps UI
- `main/test/test_registry.c` — Registry unit tests (optional)

### Modified Files
- `components/crystal_app/include/crystal_app.hpp` — Add clear() to CrystalState
- `components/crystal_app/src/crystal_app.cpp` — Implement clear()
- `components/crystal_registry/include/crystal_registry.hpp` — Add apply_changes()
- `components/crystal_registry/src/crystal_registry.cpp` — Implement apply_changes()
- `components/crystal_shell/src/crystal_shell.cpp` — Add "Manage Apps" button
- `components/crystal_shell/CMakeLists.txt` — Include new files

---

## Validation Checklist

### Quick Pre-Flight
- [ ] Phase 12 complete and stable
- [ ] All apps launch correctly
- [ ] Registry APIs accessible
- [ ] Settings navigation works

### Quick Post-Flight
- [ ] Manage Apps page opens from Settings
- [ ] All apps listed with correct enabled state
- [ ] Install/uninstall toggles enabled flag
- [ ] Reorder changes slot numbers
- [ ] Clear-data erases app's NVS keys
- [ ] Changes take effect (immediately or at boot)
- [ ] No Phase 12 regressions

---

## Troubleshooting

### Issue: App doesn't appear in list
- **Check:** Is it in `kApps` in main/main.cpp?
- **Check:** Does `crystal_registry_catalog_count()` include it? Installed queries exclude disabled apps.

### Issue: Toggle doesn't persist
- **Check:** Is NVS write succeeding? (check logs)
- **Check:** Is the hashed key under 15 characters?

### Issue: Reorder doesn't work
- **Check:** Are slot values being written to NVS?
- **Check:** Is registry re-sorting on next boot?

### Issue: Clear-data doesn't erase everything
- **Check:** Is the prefix correctly calculated?
- **Check:** Are all keys under that prefix being iterated?

### Issue: Enable takes effect but app crashes
- **Check:** Does the app's onCreate() work correctly?
- **Check:** Are there lifecycle violations?

---

## Commit Template

```
Phase 13.X: <Sub-phase name>

<What changed>

Checkpoint: <Visible validation result>

```

---

## Support Documents

- **Full guide:** `PHASE_13_DETAILED_GUIDE.md` (step-by-step implementation)
- **Checklist:** `PHASE_13_CHECKLIST.md` (progress tracking)
- **This doc:** `PHASE_13_QUICK_REFERENCE.md` (one-page summary)

---

**Version:** 1.0  
**Date:** 2026-10-13  
**Status:** Ready for implementation
