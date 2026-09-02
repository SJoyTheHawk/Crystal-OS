# Phase 0 bring-up — complete

Phase 0 was completed on the physical Waveshare ESP32-S3-Touch-LCD-4B on
2026-09-02.

## Before connecting the board

1. Install ESP-IDF 5.3 or newer and activate its shell environment.
2. Run `idf.py set-target esp32s3` from the repository root.
3. Run `idf.py build`. The first build downloads the pinned managed components.
4. Review the downloaded dependency license files under `managed_components/`
   and update `NOTICE` if their terms require additional text.

## Hardware bring-up record

1. Board port: `/dev/cu.usbmodem1101`.
2. `idf.py -p /dev/cu.usbmodem1101 flash monitor` completed successfully.
3. The Brookesia launcher appears with the **Hello** app.
4. **Hello** opens, displays `Hello, Crystal!`, and **Return to launcher** works.
5. Log measurement: `crystal_boot: first-frame baseline: 1933 ms`.

The launcher currently makes only the icon image the active hit target; its
label and visual affordance will be improved in a later UI phase. This is a
known usability follow-up, not a Phase 0 bring-up failure.

On the first boot with NVS encryption enabled, ESP-IDF may report that the HMAC
key is missing and then generate it. It provisions the configured eFuse key
slot (currently HMAC key ID `0`), which is a one-time irreversible hardware
operation. Do not change that key ID or erase eFuses after this step.

An `Incorrect size of core dump image` message on the first boot usually means
the newly allocated coredump partition still contains erased or stale flash
contents. It is not an application crash; Phase 12 will add explicit coredump
validation and cleanup.

Phase 0 is complete. The notes below are retained as operational warnings for
future reflashes and factory-reset planning.
