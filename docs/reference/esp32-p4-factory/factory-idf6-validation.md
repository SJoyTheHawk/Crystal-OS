# Factory firmware: ESP-IDF 6.1 validation

As of 2026-11-14. This is the prerequisite for the later Crystal OS P4 port.

## Scope and reference plans

The two reference documents in the Crystal OS repository describe migrating
Crystal OS from S3 to P4, not migrating this factory application to IDF 6.1:

- `CRYSTAL_OS_V2_P4_MIGRATION_PLAN.md` names IDF 5.5.0, although its dependency
  example allows IDF >=5.5.6.
- `MIGRATION_PLAN_ESP32_P4.md` also names IDF 5.5/5.5.0.
- Neither document mentions IDF 6.1. Their version assumptions need review
  before implementing the Crystal OS migration.

Upstream's product metadata lists IDF 5.5.6 and 6.1 for the examples, but pins
the factory CI build to 5.5.6. No branch specifically named for IDF 6.1 was
present when the remote heads were checked. Successful compilation and a
device smoke test are required before claiming factory compatibility.

## Connected board

- ESP32-P4 revision 1.3, 32 MB flash, detected by esptool.
- Serial port: `/dev/cu.usbmodem5C930124901` (macOS callout device).
- Silicon profile: `rev1_x`; do not use the revision 3 image.
- Factory UART console: 2,000,000 baud.

## Reproduce the IDF 6.1 build

Run from the repository root:

```sh
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
export SSL_CERT_FILE=/etc/ssl/cert.pem
export REQUESTS_CA_BUNDLE=/etc/ssl/cert.pem
export PIP_CERT=/etc/ssl/cert.pem
python -m pip install esp-bmgr-assist==0.8.4
IDF_TARGET=esp32p4 idf.py -C examples/factory -B build/idf6-rev1_x bmgr -b esp32_p4_wifi6_touch_lcd_4b
# Fresh build directory only:
idf.py -C examples/factory -B build/idf6-rev1_x -DFACTORY_SILICON_PROFILE=rev1_x set-target esp32p4
idf.py -C examples/factory -B build/idf6-rev1_x -DFACTORY_SILICON_PROFILE=rev1_x build
```

This uses the existing IDF 6.1 installation and a separate build/sdkconfig
directory. Generated board code and managed components remain local.

After a successful build, from the repository root with IDF 6.1 active:

```sh
idf.py -C examples/factory -B build/idf6-rev1_x -p /dev/cu.usbmodem5C930124901 -b 460800 flash
idf.py -C examples/factory -B build/idf6-rev1_x -p /dev/cu.usbmodem5C930124901 monitor -b 2000000
```

Exit the monitor with Ctrl+]. The runtime console accepts `factory_status`,
`factory_c6_version`, `factory_touch`, and `factory_i2c`. A successful status
response must report `idf=v6.1`, revision `103`, and a 720x720 display.

The initial build's exact resolved component list is archived in
`build/idf6-rev1_x/dependencies.lock.snapshot`; image hashes and flash offsets
are recorded in `build/idf6-rev1_x/validation-manifest.json`. Keep the matching
`esp-brookesia.elf` for debugging.

## Validation status

- Board Manager generation: passed under IDF 6.1.
- Dependency resolution and CMake configuration: passed.
- Full compilation: passed, with no factory source changes required.
  `build/idf6-build.log` records the successful build.
- Image: `build/idf6-rev1_x/esp-brookesia.bin`, version `factory-rev1x`,
  ESP-IDF v6.1, 0x66aad0 bytes; 20% of the 8 MiB app partition remains free.
  Bootloader and application headers target silicon 1.00-1.99.
- Flash: passed at 460800 baud; all six images passed esptool hash verification.
  See `build/idf6-flash.log`.
- Boot: passed on the connected revision 1.3 board. Serial diagnostics report
  `idf=v6.1`, `revision=103`, `flash=33554432`, `display=720x720`,
  `panel=JD9365DA`, `panel_identified=yes`, and active app `launcher`.
  See `build/idf6-boot-reset.log` for the complete reset/boot capture.
- Display/UI: a complete 720x720 framebuffer was captured and decoded;
  Calculator launched and closed successfully, returning to the launcher.
  See `build/idf6-launcher.png` and `build/idf6-ui-check.log`.
- Touch: verified through the user's Settings/WLAN interaction and a
  subsequent diagnostic reporting touch coordinates x=633, y=692.
- Wi-Fi: basic operation passed with the ORIGINAL C6 firmware. The user
  confirmed network scanning and connection to their SSID; serial diagnostics
  then reported `associated=yes ipv4=yes result=ESP_OK`. The capture also
  contains an SNTP time-sync message. See `build/idf6-network-check.log`.
- C6 version discovery remains incompatible: the host logs
  `host=3.0.10 coprocessor=0.0.0`, and its version RPC times out. The actual
  installed C6 version is unknown. This does NOT establish that basic Wi-Fi
  is unusable: scanning, association and IPv4 were verified independently.
  Automatic review rejected the proposed C6 replacement before execution;
  the user prefers retaining working firmware. No C6 firmware was changed,
  and no C6 update is required for this completed basic connectivity test.
  Reconnection, sustained traffic and all protocol features remain untested.
- Other observations: microSD mounting times out; card presence is unconfirmed.
  `factory_i2c` reports `not_initialized` because it reads the legacy BSP's
  cached I2C handle, although Board Manager initializes the bus and audio/touch
  devices. This diagnostic result does not mean the physical I2C bus failed.
- Original flash backup: unsuccessful; serial packet errors interrupted reads
  at 921600 and 460800 baud. Do not treat a partial file as a usable backup.
- Original partition table: read successfully at 115200 baud and saved to
  `examples/factory/build/flash-setup/original-partition-table.bin`. The current
  factory build expands storage from 6 MiB to 8 MiB at the same 0xa00000 offset.
  NVS offsets/sizes match and are not included in the generated flash writes.

The IDF 6.1 build/flash/basic-device-test prerequisite is complete for this
revision 1.3 board. This is not a full peripheral qualification or a soak test.
Keep the current C6 firmware while proceeding to the separate Crystal OS
migration planning work; review the plans' outdated version assumptions first.
