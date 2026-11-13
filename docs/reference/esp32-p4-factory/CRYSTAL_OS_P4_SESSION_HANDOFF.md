# Handoff: Crystal OS P4 migration after IDF 6.1 factory validation

Date: 2026-11-14 (Asia/Hong_Kong).

## Objective and current decision

The user wants to migrate Crystal OS to the ESP32-P4 board after first testing
the factory firmware with ESP-IDF v6.1. That build/flash/basic-device-test
prerequisite has passed. The Crystal OS migration itself has not started in
this session, and no files in the Crystal OS repository were modified.

Keep the tested factory workspace separate. Copy this handoff and its validation
record into Crystal OS, then port only the source and configuration needed for
each migration step. Do not import the entire vendor tree into Crystal OS.
The factory folder is a downloaded source archive without a root `.git`
repository; it now contains local setup and validation artifacts and is not
an untouched original package. Do not claim it corresponds to an exact upstream
commit unless that provenance is independently established.

## Locations

- Tested reference workspace: `/Users/szemy/Downloads/ESP32-P4-WIFI6-Touch-LCD-4B-main`
- Factory project: `examples/factory/` within that workspace.
- Destination repository: `/Users/szemy/Workspace/ESP32 Crystal OS`
- Validation record: `docs/factory-idf6-validation.md` in the reference workspace.
- Existing Crystal OS plans: `docs/CRYSTAL_OS_V2_P4_MIGRATION_PLAN.md` and
  `docs/MIGRATION_PLAN_ESP32_P4.md`.
- Existing command guide: `docs/ESP_IDF_COMMAND_GUIDE.md` in Crystal OS.

## Recommended first action in the Crystal OS session

Read the destination repository's applicable instructions and inspect its
working-tree state. The user requested this handoff so that the destination
session can copy it if useful. The following copies only the two reference
documents; it does not start the migration or flash any device.

```sh
FACTORY_REF='/Users/szemy/Downloads/ESP32-P4-WIFI6-Touch-LCD-4B-main'
CRYSTAL_REPO='/Users/szemy/Workspace/ESP32 Crystal OS'
mkdir -p "$CRYSTAL_REPO/docs/reference/esp32-p4-factory"
cp -n "$FACTORY_REF/docs/factory-idf6-validation.md" "$CRYSTAL_REPO/docs/reference/esp32-p4-factory/"
cp -n "$FACTORY_REF/docs/CRYSTAL_OS_P4_SESSION_HANDOFF.md" "$CRYSTAL_REPO/docs/reference/esp32-p4-factory/"
```

If a destination file already exists, compare it before replacing or merging;
`cp -n` deliberately does not overwrite it. Relative `build/...` and
`examples/...` paths in these documents refer to the reference workspace,
not the destination directory containing the copied documents.

## Verified baseline

| Item | Observed result |
| --- | --- |
| Device | ESP32-P4 revision 1.3, 32 MB flash |
| Profile | `rev1_x`, silicon bounds 1.00-1.99, 200 MHz PSRAM configuration |
| Framework | Existing local ESP-IDF v6.1 installation |
| Factory source | Built without application source changes |
| Flash | All six generated images passed esptool hash verification |
| Boot/display | Launcher runs; JD9365DA identified; 720x720 framebuffer captured |
| Touch | User navigated Settings/WLAN; diagnostic returned touch coordinates |
| App lifecycle | Calculator opened and closed, returning to launcher |
| Wi-Fi | User scanned and connected; `associated=yes ipv4=yes result=ESP_OK` |
| C6 | Original firmware retained; actual installed version unknown |

The factory app is `factory-rev1x`, 6,728,400 bytes, with SHA-256
`d6c79476b1a03ec49616806cefc557d68eb597039cb32f2546326dfdd3424c31`.
The build uses `esp_wifi_remote` 1.6.5 and `esp_hosted` 3.0.10. Consult the
resolved dependency snapshot for the complete versions, rather than treating
the old migration plans or every vendor document as current.

## Preserve the working C6 firmware

The user's preference is explicit: do not replace firmware if Wi-Fi works.
The C6 version query times out and the P4 logs a major-version mismatch with
`coprocessor=0.0.0`. This is an unknown version, not proof of unusable Wi-Fi.
Actual scanning, association, IPv4 assignment, and an SNTP sync message were
observed with the existing C6. Do not update it merely to silence the warning.

A proposed bundled C6 update was rejected by automatic approval review before
execution. The user did not approve it and instead asked to test existing
functionality, which succeeded. No C6 firmware was changed. This handoff does
not grant approval for a future C6 replacement or irreversible eFuse changes.

## Build setup and evidence to retain

- IDF activation: `source /Users/szemy/.espressif/v6.1/esp-idf/export.sh`
- Board Manager helper installed: `esp-bmgr-assist==0.8.4`.
- Verified build directory: `<reference>/build/idf6-rev1_x/`.
- Exact commands: see `factory-idf6-validation.md`; use separate build and
  sdkconfig paths for each IDF version and silicon profile.
- macOS serial port observed: `/dev/cu.usbmodem5C930124901`; rediscover it
  before use because ports can change. Flash: 460800 baud; console: 2000000.

Preserve the complete tested build directory locally, particularly:

- `esp-brookesia.bin` and matching `esp-brookesia.elf`.
- `bootloader/bootloader.bin`, `partition_table/partition-table.bin`,
  `ota_data_initial.bin`, `srmodels/srmodels.bin`, and `storage.bin`.
- `sdkconfig`, `flash_args`, `flasher_args.json`, `validation-manifest.json`,
  and `dependencies.lock.snapshot`.

Logs under `<reference>/build/`: `idf6-build.log`, `idf6-flash.log`,
`idf6-boot-reset.log`, `idf6-ui-check.log`, `idf6-network-check.log`, and
`idf6-launcher.png`. The snapshot and manifest are evidence, not instructions
to overwrite Crystal OS's dependency lock or configuration. Avoid committing
large build outputs, installed SDKs, or logs containing private network data.

ESP-IDF 5.5.6 was also downloaded and installed under the reference workspace's
`build/` during initial setup. It was not used for the successful v6.1 test.
Do not copy these toolchain installations into Crystal OS.

## Limits and recovery context

- Full original flash backup failed at both 921600 and 460800 baud due to
  serial packet errors. There is no verified complete pre-flash backup.
- Original partition table was read successfully at 115200 baud and saved at
  `<reference>/examples/factory/build/flash-setup/original-partition-table.bin`.
- Factory flashing preserved NVS partitions. Storage grew from 6 MiB to 8 MiB
  at offset 0xa00000 and its assets were replaced.
- Reconnect behavior, long-duration networking, TLS/application protocols,
  camera, audio quality, Ethernet traffic, and other peripherals are not
  comprehensively qualified. No soak test was performed.
- microSD mount timed out; card presence was unconfirmed.
- `factory_i2c` reads a legacy cached BSP handle and reports `not_initialized`,
  while Board Manager initializes I2C for touch/audio. Do not conclude physical
  bus failure solely from that diagnostic.
- Revision 3 runtime compatibility was not tested.

## How this changes the Crystal OS migration plans

Both existing plans target IDF 5.5/5.5.0 and neither mentions 6.1. One manifest
example permits IDF >=5.5.6; that is not a completed v6.1 migration plan.
Their Brookesia, BSP, Wi-Fi, API examples, reference paths, and estimates need
comparison with the current Crystal OS source and this tested reference.
The command guide targets S3; do not copy its target/memory/partition settings
or signing/provisioning steps into the P4 test merely because they are listed.
Treat all three documents as references; the user's current request sets scope.

Suggested next sequence, within the user's authorized migration scope:

1. Preserve the current Crystal OS work and inspect its architecture, SDK,
   dependencies, security settings, and S3 baseline.
2. Update/reconcile the migration plan around IDF v6.1 and this exact P4 rev1.3
   baseline. Separate factory smoke-test evidence from Crystal OS port evidence.
3. Establish a separate P4 configuration/build directory. Port the minimal
   board/display/touch/PSRAM initialization needed to boot Crystal OS.
4. Integrate the tested hosted Wi-Fi arrangement, keeping the working C6.
5. Validate Crystal OS lifecycle, persistence, networking, packages and OTA
   incrementally before importing UI enhancements or optional peripherals.

Review selected files under `examples/factory/main`, the local BSP
`components/esp32_p4_wifi6_touch_lcd_4b`, `components/bsp_extra`, profile
defaults, component manifests, and generated Board Manager configuration.
Use manifests/Board Manager to reproduce managed dependencies. Copy only
needed first-party adaptations with their license/attribution retained.
Do not replace Crystal OS's architecture or OTA/security partition layout with
the factory application's layout, and do not copy `managed_components/` or
generated build directories into its source tree.
