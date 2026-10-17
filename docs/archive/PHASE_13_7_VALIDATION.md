# Phase 13.7 — Apply changes live validation

Date: 2026-10-18
Status: complete — implementation, automated validation, and device acceptance.

## Implemented behavior

Settings > Manage Apps now applies an enable immediately. The registry keeps
the running phone and firmware catalog, constructs a newly enabled app, calls
`installApp()`, and adds the successful instance to its installed list. A
failed live install remains saved and reports that a restart is required.

Disabling remains a reboot-time change, as specified by the Phase 13 guide.
The page explains this distinction and keeps the existing restart prompt for
disable and reorder changes. Toggle updates preserve the current row and
scroll position.

## Automated evidence

- `python3 tools/test_crystal_registry.py`: PASS.
- `python3 tools/test_manage_apps_page.py`: PASS, including real LVGL layout,
  toggle persistence, disabled-app rendering, drag behavior, scroll handling,
  and teardown.
- `python3 tools/test_crystal_state.py`: PASS.
- `source /Users/szemy/.espressif/v6.1/esp-idf/export.sh && ./scripts/build_dev.sh`:
  PASS. Signed `build/crystal_os.bin` is `0x2c1000` bytes with 45% of the
  smallest app partition free.
- `git diff --check`: PASS.

## Device check procedure

Use this procedure with a development board and keep the serial monitor open
so the live-apply log can be verified.

### 1. Build and flash

From the repository root:

```bash
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
./scripts/build_dev.sh
idf.py -p /dev/cu.usbmodem101 flash monitor
```

Wait for the launcher to finish booting. If Calculator is already disabled,
enable it once and reboot so the starting state for this procedure is known.

### 2. Verify disable remains a reboot change

1. Open **Settings**.
2. Open **Manage Apps**.
3. Find **Calculator** and tap **Installed**.
4. Confirm the button changes to **Install**.
5. Confirm the page explains that disabling requires a restart.
6. Leave Manage Apps and return to the launcher without rebooting.
7. Confirm **Calculator is still visible**. This is expected for Phase 13.7.
8. Restart the device.
9. Confirm **Calculator is absent** from the launcher.

Record a failure if Calculator disappears before the restart or remains after
the restart.

### 3. Verify enable applies immediately

1. Open **Settings > Manage Apps** again.
2. Find Calculator and tap **Install**.
3. Confirm the button changes to **Installed**.
4. In the serial monitor, confirm a line containing both messages appears:

   ```text
   Applying registry changes live
   Applied changes live, calculator should appear in launcher
   ```

5. Leave Manage Apps and return to the launcher without rebooting.
6. Confirm **Calculator appears immediately**.
7. Open Calculator and verify it launches and responds normally.

Record a failure if a reboot is required before Calculator appears, the live
apply log is absent, or Calculator cannot be opened.

### 4. Verify persistence

1. Restart the device after the successful live enable.
2. Confirm Calculator is still present in the launcher.
3. Open **Settings > Manage Apps**.
4. Confirm Calculator still shows **Installed**.

### 5. Record results

| Check | Result | Notes |
|---|---|---|
| Disable stays visible until reboot | Pass | User device test |
| Disable is absent after reboot | Pass | User device test |
| Enable logs live apply | Pass | User device test |
| Enable appears without reboot | Pass | User device test |
| Enabled app opens normally | Pass | User device test |
| Enabled state persists after reboot | Pass | User device test |

All six device checks passed. Phase 13.7 is closed.
