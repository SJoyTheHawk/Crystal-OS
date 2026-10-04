# Phase 12: Reliability, Signed OTA, and Recovery

## Summary

Implement crash reporting, signed full-image OTA, automatic rollback, stable/staging release infrastructure, and USB recovery.

OTA distributes the signed `crystal_os.bin` application image, not a patch, ZIP, ELF, bootloader, partition table, or SPIFFS image. Bootloader and partition-table changes require USB recovery.

## Update Contract

- Production source:
  - Primary: `https://updates.rockyroad.studio/crystal-os/stable/manifest.json`
  - Mirror: `https://updates.bubstal.com/crystal-os/stable/manifest.json`
  - UI label: `Stable`
- Development source:
  - `https://updates.rockyroad.studio/crystal-os/staging/manifest.json`
  - No channel label in the UI.
- Development and production builds use the same `MAJOR.MINOR.PATCH` version, but different source configurations and RSA-3072 signing keys.
- Accept only a strictly newer three-part SemVer. Reject equal, older, malformed, prerelease, wrong-target, or manifest/image-version mismatches.
- Keep the existing `project(crystal_os VERSION ...)` version source; it already embeds `0.11.0` correctly.

Use this bounded JSON schema:

```json
{
  "schema": 1,
  "target": "waveshare-esp32-s3-touch-lcd-4b",
  "channel": "stable",
  "version": "0.12.0",
  "size": 2810392,
  "sha256": "<SHA-256 of exact signed binary>",
  "urls": [
    "https://updates.rockyroad.studio/crystal-os/releases/0.12.0/crystal_os.bin",
    "https://updates.bubstal.com/crystal-os/releases/0.12.0/crystal_os.bin"
  ],
  "notes": "Reliability and recovery update"
}
```

- Parse with `cJSON`, using a 2 KiB response limit, HTTPS-only URLs, fixed target/channel values, at most two 256-byte URLs, and notes capped at 256 bytes.
- Require `Content-Length`, match it to `size`, calculate SHA-256 over the downloaded signed file, then let ESP-IDF validate the image and RSA signature.
- Use primary then mirror for manifest and binary failures. Do not implement download resumption in Phase 12.
- Serve manifests as `application/json` with no-cache headers and binaries as `application/octet-stream` with immutable caching.
- Publish binaries to both production hosts first, verify size/hash, then update the primary stable manifest followed by the mirror manifest. Never overwrite a versioned binary.

## Firmware Changes

- Enable `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, explicitly pin `CONFIG_ESP_COREDUMP_CHECK_BOOT=y`, and enable `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT`.
- Do not enable hardware Secure Boot, flash encryption, or eFuse anti-rollback in Phase 12. Signed-app verification protects OTA against remote substitution without irreversible provisioning. [ESP-IDF signing guidance](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/security/secure-boot-v2.html)
- Create separate dev and production build profiles. Keep both private keys outside Git; dev builds may sign locally, while production release signing receives the key through an external secret path.
- Add a dedicated Core-0 OTA worker. It owns manifest requests and `esp_https_ota`; the existing service task continues timer expiry, battery polling, power work, and weather scheduling. Weather requests pause while OTA is active.
- Add `CrystalUpdateState`, `CrystalUpdateSnapshot`, `crystal_update_check()`, `crystal_update_cancel_check()`, `crystal_update_install()`, and `crystal_update_snapshot()`. Only one update operation may exist at once.
- Add compact `UI_EVT_OTA_CHANGED`, `UI_EVT_CRASH_RECOVERED`, and `UI_EVT_OTA_ROLLED_BACK` events. The shell obtains strings and detailed state through the snapshot API rather than queue payloads.
- Add a non-persistent power-inhibit API. OTA holds the screen awake without rewriting the user’s Auto Dimming setting and releases the inhibitor on every success/failure path.
- Before writing, require Wi-Fi with an IP address and either charging power or at least 40% battery. Unknown battery state blocks installation.
- Show a confirmation warning: the device will restart and unsaved app progress, including active countdown timers, may be lost. After confirmation, block navigation and cancellation during flash writing.
- Report integer download percentage using ESP-IDF’s received/total byte APIs. On success, announce the restart, wait about two seconds, then reboot automatically.
- Confirm a pending image only after explicit shell/first-frame readiness and the existing 1.2-second settling interval. Otherwise the next reset rolls back automatically. [ESP-IDF OTA rollback states](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/ota.html)
- Persist the attempted/current versions before reboot. A restored image detects rollback, shows one quiet toast, and retains the result in Device Status.

## Crash and Recovery

- At boot, log the mapped reset reason and read the coredump summary into a versioned NVS blob containing task, reset reason, PC, up to 16 backtrace addresses, ELF SHA, corruption flag, and a timestamp only when credible.
- Show `Recovered from an error` once; clear the notice flag only after the UI displays it. Keep `Last Crash` in Device Status until overwritten.
- Retain the raw coredump on all devices, as requested. Document that it can contain stack-resident credentials and remains physically readable until erased or overwritten.
- Archive every signed `.bin` with its exact `.elf`, manifest, file hash, build flavor, source commit, and ELF SHA.
- Add a cross-platform Python USB tool around `esptool`. It supports firmware recovery and coredump extraction/decoding.
- Recovery prompts every run between preserving NVS/SPIFFS and factory reset. Preserve is the default selection; factory reset requires a second destructive confirmation.
- Preserve-mode recovery restores bootloader, partition table, OTA metadata, and signed app while leaving NVS, SPIFFS, and coredump untouched. Refuse incompatible partition layouts.
- The initial Phase 12 installation must be performed over USB so each fleet receives the correct signed-app bootloader and first signed application.

## Validation

- Unit-test strict SemVer ordering, bounded manifest parsing, target/channel enforcement, URL allowlists, size/hash checks, and primary/mirror selection.
- Build both profiles and verify their embedded version, signing key separation, signature validity, size below 5 MiB, and absence of private keys from artifacts/Git.
- Test offline, malformed/stale/equal/older manifests, wrong target, mismatched embedded version, invalid signature, bad hash, truncated download, Wi-Fi loss, insufficient battery, and mirror fallback.
- Interrupt power during writing and verify the running firmware remains unchanged.
- Install a good OTA and verify progress, modal navigation, screen-awake behavior, announced reboot, first-frame confirmation, and reported new version.
- Install a signed dev fault-injection image that crashes before confirmation; verify automatic rollback, the one-time rollback toast, and Device Status history.
- Trigger a deliberate crash, verify the one-time recovery toast and persistent details, then decode the retained raw dump against the archived exact ELF.
- Exercise USB recovery in both preserve and factory-reset modes, including cancellation at each prompt.
- Re-run Phase 11 Settings, power, timer-alert, Wi-Fi, keyboard, gesture, and card-lifecycle regressions before closing Phase 12.

## Assumptions

- DNS and public-CA HTTPS certificates for both proposed hosts will exist before hardware OTA validation.
- Development devices trust only the development key and staging feed; production devices trust only the production key and stable feed.
- SPIFFS assets, bootloader OTA, delta updates, automatic background installation, public beta channels, and hardware Secure Boot remain out of Phase 12.
- Update [`CODE_GUIDE.md`](/Users/szemy/Workspace/ESP32%20Crystal%20OS/docs/CODE_GUIDE.md:1598) and [`DESIGN.md`](/Users/szemy/Workspace/ESP32%20Crystal%20OS/docs/DESIGN.md:604) to reflect the decisions above, especially strict version ordering, signed images, dedicated OTA worker, retained raw dumps, source topology, and recovery behavior.
