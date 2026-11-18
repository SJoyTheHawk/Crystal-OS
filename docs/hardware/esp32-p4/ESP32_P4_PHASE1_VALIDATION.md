# Crystal OS ESP32-P4 Phase 1 validation

Date: 2026-11-14 (Asia/Hong_Kong). Branch: `feature/esp32-p4-port`.
Pre-implementation checkpoint: `419e2f5` (`Record ESP32-P4 migration baseline`).

Phase 1 implementation, signed builds, flashing and boot/network smoke tests
pass. The user confirmed on-device that the Crystal card interface displays,
app taps respond and swiping between cards works. Detailed visual and animation
acceptance remains open. The
[factory evidence](../../reference/esp32-p4-factory/README.md) is a separate earlier
baseline. Its Downloads workspace was used as a read-only reference.

## Implemented foundation

- Isolated `sdkconfig.defaults.common` / `sdkconfig.defaults.esp32p4` and
  `partitions.esp32p4.csv`; the root S3 defaults and active S3/16 MB `sdkconfig`
  remain intact. Explicit `IDF_TARGET=esp32p4` selects the P4 profile.
- Board Manager 0.7.5, Waveshare board pack 0.3.3, LVGL adapter 0.6.2,
  `esp_wifi_remote` 1.6.5 and `esp_hosted` 3.0.10. Crystal's local Brookesia
  0.4.2 and LVGL 8.4.0 are retained. Brookesia's internal startup version string
  still says 0.4.1; that string is not a dependency upgrade.
- `components/crystal_board_p4` maps Crystal's BSP calls to the Board Manager
  MIPI-DSI/GT911 path. It preserves the adapter's flush alignment and maps a
  zero-timeout display lock to the adapter's indefinite wait. UI setup holds
  the display lock, and the P4 LVGL task is subscribed to the watchdog.
- Crystal's dark square stylesheet is adapted to 720x720, retaining the existing
  card shell, transitions, gesture behavior and app lifecycle. Phase 1 adds no
  new animation language. PPA acceleration is not enabled.
- Existing Wi-Fi calls go through IDF's remote wrapper. No extra Wi-Fi stub or
  `esp_wifi_remote_init()` call is used. No C6 firmware was flashed or updated.
- Crystal signing, rollback configuration, encrypted NVS, SPIFFS, LittleFS and
  coredump support are retained. App-storage capacity comes from the partition
  table. RISC-V crash summaries retain the exception PC; decoded backtrace
  parity with Xtensa remains future work.
- The tested rev1.3 profile uses **360 MHz CPU, 32 MB flash and 32 MB HEX PSRAM
  at 200 MHz with XIP**. Energy Saving uses the valid 180 MHz point. Hardware
  AES remains disabled. Hardware ECDSA verification is disabled for this silicon
  revision; TLS certificate verification continues in software.

## Verified results

| Check | Result and scope |
| --- | --- |
| P4 IDF v6.1 build | Pass; signed image 4,526,080 bytes (`0x451000`), 46% of the 8 MiB OTA slot free |
| Target-switch reproducibility | P4 rebuild after the S3 regression succeeds and produces the same signed application digest |
| Initial P4 flash | Six generated images written; esptool verified every image hash |
| Final app flash | App only at `0x30000`; esptool hash verified after the PM, ECDSA and display-lock corrections |
| Boot/memory | P4 rev1.3 detected; PSRAM memory test passes; 32 MB flash and 200 MHz PSRAM reported |
| Display/touch drivers | JD9365DA detected, 720x720 panel initialized; GT911 ID `911`, configuration 91; LVGL display/input registered |
| Physical UI (user-confirmed) | Crystal card interface is visible; tapping an app and swiping between cards respond correctly |
| Crystal startup | Four bundled apps registered; Dev Tester created; active card 1/4 and 720x680 app snapshot logged; USB developer transport ready |
| First-frame milestone | Final capture: 1,091 ms after `app_main` begins, approximately 3.3 seconds after reset; main-task stack headroom 5,064 bytes |
| Storage | Encrypted NVS initialized; SPIFFS mounted; LittleFS mounted with zero installed packages |
| Wi-Fi | Existing saved credentials used; association and IPv4 succeeded with the original C6; final capture first retried reasons 7 and 205 |
| Time | SNTP synchronized the system clock and RTC |
| HTTPS/weather | TLS connection succeeded; HTTP 200, 468-byte JSON body accepted and weather parsed |
| HTTPS smoke | HTTP 200, 577-byte body; aggregate two requests/two successes/zero HTTP failures |
| Final boot stability | No panic, watchdog reset, invalid PM frequency or unsupported ECDSA-peripheral errors in the 40-second capture |
| S3 regression build | Pass under IDF v6.1 in `build/s3-idf61-phase1-regression`; signed image `0x301000`, fits its 4 MiB slot; no S3 flash performed |
| Host launcher regression | `python3 tools/test_launcher_swipe.py` passes real-page wipe, center/edge/icon/dot swipes, snap/flick/cancel/reversal/interruption, endpoint and click-suppression checks, app return and teardown |

The first-frame measurement is a firmware milestone, not a measured display
latency or frame-rate benchmark. Driver initialization and host gesture tests
do not establish physical touch coordinates or perceived animation quality.

Final signed application SHA-256:
`17fc78c931f5eb2c7c2adb215b6ddeca8de68cb6cab1b0573f36c0a2560a6400`.
The binary embeds build metadata; a later rebuild may have a different digest.
The current bootloader is `0x5b90` bytes with `0x470` bytes (5%) free before
the partition table; future bootloader/security additions must recheck that limit.

Selected boot evidence is retained in
[the sanitized serial excerpt](../../test-evidence/p4-phase1/2026-11-14-boot.txt).
Local complete logs are `/private/tmp/crystal-os-p4-phase1-final-build.log`,
`/private/tmp/crystal-os-p4-phase1-final-flash.log`,
`/private/tmp/crystal-os-p4-phase1-final-boot.txt`,
`/private/tmp/crystal-os-s3-phase1-regression-build.log` and
`/private/tmp/crystal-os-p4-launcher-test.log`. Temporary files are not durable
release artifacts. The final boot excerpt corresponds to the digest above.
The matching app binary, ELF, sdkconfig and flash arguments are also retained
locally in `/private/tmp/crystal-os-p4-phase1-tested/`.

## Build from the repository root

### Everyday P4 commands (local setup updated 2026-11-14)

The local root `sdkconfig` now contains the working P4 configuration copied
from `build/p4-idf61-brightness/sdkconfig`. CMake resolves the saved target
before choosing its defaults, so the standard `build/` directory can be used:

```bash
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
idf.py build
idf.py flash
# For subsequent application-only updates:
idf.py app-flash monitor
```

Run either full `flash` or `app-flash` as appropriate; both commands build
automatically. The monitor baud is saved as 1000000. Add `-p <port>` when
multiple serial devices are connected. No repeated `set-target`, `-B`,
`-D SDKCONFIG`, or preflight bypass is needed for this root P4 configuration.
ESP-IDF activation is per shell session; sourcing its `export.sh` does not
reconfigure the project.

The root `sdkconfig` is ignored by Git, so this local selection is not
automatically transferred to another checkout. Keep the tracked S3 defaults
for S3 builds and the common/P4 defaults for P4 builds. The incomplete former
root build was archived as `build-before-p4-default-20261114-195027/`; the
isolated builds retain their original paths.

Validation: a full `idf.py build` and a second incremental `idf.py build`
both passed without target/configuration flags or a preflight bypass. Generated
`build/project_description.json` confirms target `esp32p4`, root `sdkconfig`,
and monitor baud `1000000`. This setup change was build-tested; it did not
flash the device.

### Original isolated Phase 1 build

The commands below describe the original setup with an S3 root configuration.
The preflight exception applies to that isolated setup only.

```bash
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
ESP_BMGR_ASSIST_PREFLIGHT=0 idf.py \
  -B build/p4-idf61-phase1c \
  -D IDF_TARGET=esp32p4 \
  -D SDKCONFIG="$PWD/build/p4-idf61-phase1c/sdkconfig" build
```

The build loads common, P4 and `components/gen_bmgr_codes/board_manager.defaults`
in that order. The included generated board sources are deliberately gated to
P4 in CMake, Kconfig and dependency rules. Regenerating Board Manager files must
retain those guards and relative board-source paths. Switching targets resolves
the shared ignored `managed_components/` and `dependencies.lock` again; run S3
and P4 IDF builds sequentially.

Board Manager 0.7.5's Python preflight checks `<project>/sdkconfig` directly, even
when a different `-D SDKCONFIG` is passed. It therefore reports missing P4 board
options in the preserved S3 root configuration. This warning is not a reason to
overwrite root `sdkconfig`; inspect the generated P4 sdkconfig/header and CMake
output. `ESP_BMGR_ASSIST_PREFLIGHT=0` skips only the optional assistant preflight.

## Flash layout and recovery facts

| Image | Offset | Partition size |
| --- | --- | --- |
| Bootloader | `0x2000` | Fits below partition table |
| Partition table | `0x8000` | 4 KiB reserved |
| OTA data initial | `0x10000` | 8 KiB |
| `ota_0` application | `0x30000` | 8 MiB |
| `ota_1` | `0x830000` | 8 MiB |
| SPIFFS `storage` | `0x1030000` | 4 MiB |
| LittleFS `apps` | `0x1430000` | 10 MiB |

The initial six-image migration flash replaced the factory app/assets and
initialized Crystal's apps partition. Subsequent corrections wrote only the app.
NVS was not explicitly erased by the flash command; the runtime reports encrypted
NVS and successfully loads the saved Wi-Fi credentials. General factory-to-Crystal
state migration has not been qualified.

Before the Crystal flash, the full **current factory-session P4 flash** was read:
`/private/tmp/crystal-os-p4-before-phase1.bin`, 33,554,432 bytes. SHA-256:
`1c09e16876c9dcdc9375be61f5ab8cf263b893704bc15464ba2c7ceb740f32ae`.
This contains private NVS data and must stay out of version control. Retain it
securely before temporary-directory cleanup. It is not a backup of the original
untouched firmware, and P4 flash backup does not back up C6 flash or eFuses.
No explicit eFuse provisioning tool was run. Crystal enables IDF HMAC-based NVS
encryption; its provider can provision the configured eFuse key if absent. This
session did not capture a before/after eFuse audit, so it does not establish that
eFuses were unchanged or that raw flash restore alone restores all device state.

On the current tested layout, an application-only update was performed using:

```bash
python -m esptool --chip esp32p4 -p /dev/cu.usbmodem5C930124901 -b 2000000 \
  --before default-reset --after hard-reset write-flash \
  --flash-mode dio --flash-size keep --flash-freq 80m \
  0x30000 build/p4-idf61-phase1c/crystal_os.bin
```

That offset is valid for the current `ota_0` baseline, not a general substitute
for Crystal's OTA flow after slots switch. Initial migration images and offsets
are recorded in the build's `flasher_args.json`. A complete project `flash` writes
filesystem images too; do not use it as an app-only update. Serial console baud
is 1,000,000. Use a normal hard reset for a boot capture; a download-mode reset
does not start the app.

## Remaining acceptance and limitations

- Inspect first-frame appearance, layout/clipping, typography and status bar;
  exercise every bundled app and center/edge/interrupted/reversed gestures.
  Basic card display, app taps and between-card swipes are confirmed; animation
  continuity/quality and a 60 fps claim remain unverified.
- Repeat Crystal Wi-Fi scanning and deliberate AP-loss/reconnect tests. Startup
  retries followed by successful connection are limited recovery evidence.
- Validate lifecycle/state across reboot, signed package installation/removal,
  declarative apps, control panel, OTA confirmation/rollback and crash recovery.
  No package signing key is provisioned in this image; packages are rejected
  until the existing trusted-key provisioning flow is used.
- `coprocessor=0.0.0` and the hosted major-version warning persist. The installed
  C6 version remains unknown. The warning is a diagnostic recommendation, not
  evidence that an OTA ran. Preserve the working C6 firmware.
- Board-pack GPIO27 reset ownership and unsupported panel `swap_xy` diagnostics
  remain. Normal-rotation initialization completes; rotated operation is untested.
- Audio/amp support in the new facade is explicitly unavailable. Battery/PMIC,
  reset-button behavior, camera, SD, Ethernet, optional peripherals and rev3
  silicon are not qualified. The S3 reset-button GPIO is not enabled on P4.
- Energy Saving's P4 clock selection compiles; interactive power/brightness
  transitions, sustained networking, memory stability and long soaks remain open.

The two migration plans retain later visual/network/feature work as proposals.
Phase 1 preserves Crystal's UX contract; it does not close physical UX acceptance
or the later feature-parity milestones.
