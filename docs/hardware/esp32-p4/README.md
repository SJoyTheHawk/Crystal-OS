# ESP32-P4

Current hardware is the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B, rev1.3 with a
720 × 720 display. Use the tested commands and validation records before
copying snippets from the original migration proposals.

## Build and validation

- [P4.1 build, flash and bring-up](ESP32_P4_PHASE1_VALIDATION.md)
- [P4.2 visual and interaction acceptance](ESP32_P4_PHASE2_VALIDATION.md)
- [P4.3 network expansion](ESP32_P4_PHASE3_VALIDATION.md)
- [Factory baseline and assumption corrections](../../reference/esp32-p4-factory/README.md)
- [Sanitized P4.1 boot evidence](../../test-evidence/p4-phase1/2026-11-14-boot.txt)

P4.1–P4.3 acceptance covers the documented scopes. P4.4 performance, soak,
package/OTA and remaining feature qualification are still open.

## Migration plans

- [Migration overview](MIGRATION_PLAN_ESP32_P4.md)
- [Detailed migration proposal](CRYSTAL_OS_V2_P4_MIGRATION_PLAN.md)

Both user-supplied plans retain their original dates, reconciliations and
proposed later work. Their older stack assumptions and code examples are not
a substitute for the verified baseline above.

For the preserved S3 target, see the [S3 command guide](../../guides/ESP_IDF_COMMAND_GUIDE.md).
Return to the [documentation index](../../README.md).
