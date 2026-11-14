# ESP32-P4 factory baseline for Crystal OS

Reconciled on 2026-11-14 (Asia/Hong_Kong). This documentation update does not
implement the Crystal OS port or repeat hardware tests.

This page describes the earlier factory handoff. Subsequent Crystal port work
and hardware tests are recorded separately in the
[Crystal Phase 1 validation](../../ESP32_P4_PHASE1_VALIDATION.md). Statements
below about unmodified implementation inputs and unexecuted next steps refer
to the handoff checkpoint, not the current migration status. The two copied
source records remain unchanged.

## Evidence and provenance

- [Session handoff](CRYSTAL_OS_P4_SESSION_HANDOFF.md)
- [Factory IDF v6.1 validation](factory-idf6-validation.md)

Both records were copied byte-for-byte from the tested workspace's `docs/`.
The destination directory did not exist; no existing documents were overwritten.
The results below are reported by the other session, not independently retested here.

Keep `/Users/szemy/Downloads/ESP32-P4-WIFI6-Touch-LCD-4B-main` separate as the
tested workspace. Its factory project is `examples/factory/` and artifacts are
in `build/idf6-rev1_x/`. Relative `build/...` and `examples/...` paths in the
copied records refer to that workspace, not this documentation directory.
The downloaded archive contains local setup/validation artifacts and has no
root Git repository; an exact upstream commit is not established. Do not equate
this repository's existing vendor copy under `reference/` with the tested workspace.

## Confirmed in the validation record

| Area | Recorded result |
| --- | --- |
| Hardware | ESP32-P4 rev1.3, 32 MB flash; `rev1_x` silicon bounds 1.00–1.99; 200 MHz PSRAM configuration |
| Build | ESP-IDF v6.1; Board Manager generation, dependencies, CMake and compilation passed without factory application source changes; helper `esp-bmgr-assist==0.8.4` |
| Image | `factory-rev1x`, 6,728,400 bytes; matching ELF, manifest and dependency snapshot retained in factory build directory |
| Flash | 460800 baud; all six generated images passed esptool hash verification |
| Boot/display | Runtime reports v6.1, revision 103, JD9365DA, 720x720; framebuffer captured and decoded |
| Touch/UI | Settings/WLAN interaction and touch coordinates observed; Calculator opened/closed and returned to launcher |
| Wi-Fi | Scan and connection with original C6; `associated=yes ipv4=yes result=ESP_OK`; SNTP sync message observed |
| P4 host dependencies | `esp_wifi_remote` 1.6.5 and `esp_hosted` 3.0.10; full resolution in `dependencies.lock.snapshot` |

Application SHA-256:
`d6c79476b1a03ec49616806cefc557d68eb597039cb32f2546326dfdd3424c31`.
Factory smoke testing completes the prerequisite for migration planning; it
does not establish that Crystal OS builds or runs on P4/IDF v6.1.

## Preserve the existing C6

Keep the working C6 firmware. Its actual version is unknown: version discovery
times out and the P4 logs `coprocessor=0.0.0` with a major-version warning.
Those diagnostics do not negate the successful scan, association and IPv4 tests.
Do not infer that the installed C6 is version 3.0.10 from the P4 host dependency.
No C6 firmware was changed. Replacement is not part of this migration baseline,
and the warning alone is not a reason to replace it.

## Limitations and recovery facts

- Reconnection, sustained traffic, TLS/application protocols and soak remain untested.
- Camera, audio quality, Ethernet traffic and other peripherals remain unqualified.
- microSD mount timed out; card presence was unconfirmed.
- `factory_i2c` reports a legacy cached handle as `not_initialized`; Board Manager
  initialized I2C for touch/audio. This is not evidence of physical bus failure.
- Revision 3 runtime compatibility was not tested.
- Full original-flash backup failed from serial packet errors. There is no verified
  complete pre-flash backup; the successful partition-table read is not a full backup.
- Factory writes preserved NVS. Storage grew from 6 MiB to 8 MiB at `0xa00000`
  and assets were replaced; this is not a Crystal OS partition design.
- Retain matching images, ELF, logs, sdkconfig, flash arguments, dependency snapshot
  and manifest in the factory workspace; do not import SDKs or generated build trees.

## Reconciliation of the migration plans

Applies to [the detailed v2 plan](../../CRYSTAL_OS_V2_P4_MIGRATION_PLAN.md) and
[the migration overview](../../MIGRATION_PLAN_ESP32_P4.md).

| Earlier assumption | Planning correction |
| --- | --- |
| IDF 5.5/5.5.0 target or factory 5.5.6 requirement | v6.1 factory build/flash/basic-device baseline passed on rev1.3; use v6.1 for the planned Crystal OS port and validate its dependencies |
| `idf >=5.5.6` proves compatibility | A manifest constraint is not test evidence; the factory record supplies limited v6.1 evidence |
| Generic P4 or revision 3 interchangeability | Start with tested rev1.3 / `rev1_x` and matching silicon bounds |
| `esp_wifi_remote` 0.* or a simple `esp_wifi_remote_init()` substitution | Inspect actual hosted initialization and generated board configuration; tested P4 host versions are 1.6.5 / 3.0.10 |
| C6 must match the bundled host version | Original C6 passed basic Wi-Fi tests; preserve it despite failed version discovery |
| P4 BSP is built-in, same display API, unknown resolution | Review Board Manager plus local BSP/`bsp_extra`; tested panel is JD9365DA at 720x720 |
| Brookesia 0.4.* is latest or a drop-in upgrade | Crystal manifest pins Brookesia 0.4.2 and LVGL 8.4.0; audit tested source and resolved dependencies before selecting port versions |
| Old `Work/Manual/.../11_esp_brookesia_phone` reference | Use separate tested Downloads workspace and retained artifacts |
| Replace partitions with factory/proposed expanded layout | Preserve Crystal OTA/security/persistence requirements; audit offsets, capacity and image sizes first |
| 80% unchanged, 60 fps/performance gains and 3–4 weeks are established | Unverified estimates/targets; factory success does not validate Crystal APIs, timings, security, packages or OTA |

Current implementation inputs still select S3/16 MB flash, S3 BSP, Brookesia
0.4.2, LVGL 8.4.0 and IDF >=5.3.0. They were inspected but not changed.

## Next implementation sequence (not executed)

1. Preserve current Crystal OS work and establish an S3 baseline; audit dependency/API,
   signing, persistence and OTA requirements under IDF v6.1.
2. Establish separate P4 build/sdkconfig paths for rev1.3 / `rev1_x`, referencing
   tested settings. Keep factory source and artifacts intact.
3. Port minimal board, display, touch and PSRAM initialization to boot Crystal OS.
4. Integrate tested hosted Wi-Fi while retaining the original C6 firmware.
5. Validate Crystal lifecycle, NVS, reconnect/TLS, packages, control panel and OTA
   incrementally, before optional peripherals and UI enhancements.

Import only needed source/configuration adaptations with attribution. Do not
copy the vendor tree, managed components, installed SDKs or generated builds.
The S3 command guide's target/partition/provisioning commands are not P4 instructions.
This documentation update performs no hardware flashing, C6 changes or eFuse operations.
